/*
 * MELFAS MMS500 Touchscreen
 *
 * Copyright (C) 2014 MELFAS Inc.
 *
 *
 * Test Functions (Optional)
 *
 */

#include "melfas_mms500.h"

#if MMS_USE_DEV_MODE

/**
 * Dev node output to user
 */
static ssize_t mms_dev_fs_read(struct file *fp, char *rbuf, size_t cnt, loff_t *fpos)
{
	struct mms_ts_info *info = fp->private_data;
	int ret = 0;
	ret = copy_to_user(rbuf, info->dev_fs_buf, cnt);
	return ret;
}

/**
 * Dev node input from user
 */
static ssize_t mms_dev_fs_write(struct file *fp, const char *wbuf, size_t cnt, loff_t *fpos)
{
	struct mms_ts_info *info = fp->private_data;
	u8 *buf;
	int ret = 0;
	int cmd = 0;

	buf = kzalloc(cnt + 1, GFP_KERNEL);

	if ((buf == NULL) || copy_from_user(buf, wbuf, cnt)) {
		input_err(true, &info->client->dev, "%s [ERROR] copy_from_user\n", __func__);
		ret = -EIO;
		goto EXIT;
	}

	cmd = buf[cnt - 1];

	if (cmd == 1) {
		if (mms_i2c_read(info, buf, (cnt - 2), info->dev_fs_buf, buf[cnt - 2]))
			input_err(true, &info->client->dev, "%s [ERROR] mms_i2c_read\n", __func__);
	} else if (cmd == 2) {
		if (mms_i2c_write(info, buf, (cnt - 1)))
			input_err(true, &info->client->dev, "%s [ERROR] mms_i2c_write\n", __func__);
	}

EXIT:
	kfree(buf);

	return ret;
}

/**
 * Open dev node
 */
static int mms_dev_fs_open(struct inode *node, struct file *fp)
{
	struct mms_ts_info *info = container_of(node->i_cdev, struct mms_ts_info, cdev);
	fp->private_data = info;
	info->dev_fs_buf = kzalloc(1024 * 4, GFP_KERNEL);
	return 0;
}

/**
 * Close dev node
 */
static int mms_dev_fs_release(struct inode *node, struct file *fp)
{
	struct mms_ts_info *info = fp->private_data;
	kfree(info->dev_fs_buf);
	return 0;
}

/**
 * Dev node info
 */
static struct file_operations mms_dev_fops = {
	.owner	= THIS_MODULE,
	.open	= mms_dev_fs_open,
	.release	= mms_dev_fs_release,
	.read	= mms_dev_fs_read,
	.write	= mms_dev_fs_write,
};

/**
 * Create dev node
 */
int mms_dev_create(struct mms_ts_info *info)
{
	struct i2c_client *client = info->client;
	int ret = 0;

	input_dbg(true, &info->client->dev, "%s [START]\n", __func__);

	if (alloc_chrdev_region(&info->mms_dev, 0, 1, MMS_DEVICE_NAME)) {
		input_err(true, &client->dev, "%s [ERROR] alloc_chrdev_region\n", __func__);
		ret = -ENOMEM;
		goto ERROR;
	}

	cdev_init(&info->cdev, &mms_dev_fops);
	info->cdev.owner = THIS_MODULE;

	if (cdev_add(&info->cdev, info->mms_dev, 1)) {
		input_err(true, &client->dev, "%s [ERROR] cdev_add\n", __func__);
		ret = -EIO;
		goto ERROR;
	}

	input_dbg(true, &info->client->dev, "%s [DONE]\n", __func__);
	return 0;

ERROR:
	input_err(true, &info->client->dev, "%s [ERROR]\n", __func__);
	return 0;
}

#endif

/**
 * Process table data
 */
static int mms_proc_table_data(struct mms_ts_info *info, u8 size,
		u8 data_type_size, u8 data_type_sign, u8 buf_addr_h, u8 buf_addr_l,
		u8 row_num, u8 col_num, u8 buf_col_num, u8 rotate, u8 key_num)
{
	char data[10];
	int i_col, i_row;
	int i_x, i_y;
	int lim_x, lim_y;
	int lim_col, lim_row;
	int max_x = 0;
	int max_y = 0;
	bool flip_x = false;
	int sValue = 0;
	unsigned int uValue = 0;
	int value = 0;
	u8 wbuf[8];
	u8 rbuf[512];
	unsigned int buf_addr;
	int offset;
	int data_size = data_type_size;
	int data_sign = data_type_sign;
	int has_key = 0;
	int size_screen = col_num * row_num;

	memset(data, 0, 10);

	input_dbg(true, &info->client->dev, "%s [START]\n", __func__);

	//set axis
	if (rotate == 0) {
		max_x = col_num;
		max_y = row_num;
		if (key_num > 0) {
			max_y += 1;
			has_key = 1;
		}
		flip_x = false;
	} else if (rotate == 1) {
		max_x = row_num;
		max_y = col_num;
		if (key_num > 0) {
			max_y += 1;
			has_key = 1;
		}
		flip_x = true;
	} else {
		input_err(true, &info->client->dev, "%s [ERROR] rotate [%d]\n", __func__, rotate);
		goto ERROR;
	}

	//get table data
	lim_row = row_num + has_key;
	for (i_row = 0; i_row < lim_row; i_row++) {
		//get line data
		offset = buf_col_num * data_type_size;
		size = col_num * data_type_size;

		buf_addr = (buf_addr_h << 8) | buf_addr_l | (offset * i_row);
		wbuf[0] = (buf_addr >> 8) & 0xFF;
		wbuf[1] = buf_addr & 0xFF;
		if (mms_i2c_read(info, wbuf, 2, rbuf, size)) {
			input_err(true, &info->client->dev, "%s [ERROR] Read data buffer\n", __func__);
			goto ERROR;
		}

		//save data
		if ((key_num > 0) && (i_row == (lim_row - 1)))
			lim_col = key_num;
		else
			lim_col = col_num;

		for (i_col = 0; i_col < lim_col; i_col++) {
			if (data_sign == 0) {
				//unsigned
				if (data_size == 1) {
					uValue = (u8)rbuf[i_col];
				} else if (data_size == 2) {
					uValue = (u16)(rbuf[data_size * i_col] |
						(rbuf[data_size * i_col + 1] << 8));
				} else if (data_size == 4) {
					uValue = (u32)(rbuf[data_size * i_col] |
						(rbuf[data_size * i_col + 1] << 8) |
						(rbuf[data_size * i_col + 2] << 16) |
						(rbuf[data_size * i_col + 3] << 24));
				} else {
					input_err(true, &info->client->dev,
						"%s [ERROR] data_size [%d]\n",
						__func__, data_size);
					goto ERROR;
				}
				value = (int)uValue;
			} else {
				//signed
				if (data_size == 1) {
					sValue = (s8)rbuf[i_col];
				} else if (data_size == 2) {
					sValue = (s16)(rbuf[data_size * i_col] |
						(rbuf[data_size * i_col + 1] << 8));
				} else if (data_size == 4) {
					sValue = (s32)(rbuf[data_size * i_col] |
						(rbuf[data_size * i_col + 1] << 8) |
						(rbuf[data_size * i_col + 2] << 16) |
						(rbuf[data_size * i_col + 3] << 24));
				} else {
					input_err(true, &info->client->dev,
						"%s [ERROR] data_size [%d]\n",
						__func__, data_size);
					goto ERROR;
				}
				value = (int)sValue;
			}

			switch (rotate) {
			case 0:
				info->image_buf[i_row * col_num + i_col] = value;
				break;
			case 1:
				if ((key_num > 0) && (i_row == (lim_row - 1))) {
					info->image_buf[size_screen + i_col] = value;
				} else {
					int tmp = i_col * row_num + (row_num - 1 - i_row);
					info->image_buf[tmp] = value;
				}
				break;
			default:
				input_err(true, &info->client->dev,
					"%s [ERROR] rotate [%d]\n", __func__, rotate);
				goto ERROR;
				break;
			}
		}
	}

	if (!info->read_all_data) {
		//print table header
#if defined(CONFIG_TOUCHSCREEN_DUMP_MODE)
		if (info->add_log_header == 1)
			printk("%s mms_tsp", SECLOG);
#endif
		printk("    ");
		sprintf(data, "    ");
		strcat(info->print_buf, data);
		memset(data, 0, 10);

		switch (data_size) {
		case 1:
			for (i_x = 0; i_x < max_x; i_x++) {
				printk("[%1d]", i_x);
				sprintf(data, "[%1d]", i_x);
				strcat(info->print_buf, data);
				memset(data, 0, 10);
			}
			break;
		case 2:
			for (i_x = 0; i_x < max_x; i_x++) {
				printk("[%3d]", i_x);
				sprintf(data, "[%3d]", i_x);
				strcat(info->print_buf, data);
				memset(data, 0, 10);
			}
			break;
		case 4:
			for (i_x = 0; i_x < max_x; i_x++) {
				printk("[%4d]", i_x);
				sprintf(data, "[%4d]", i_x);
				strcat(info->print_buf, data);
				memset(data, 0, 10);
			}
			break;
		default:
			input_err(true, &info->client->dev,"%s [ERROR] data_size [%d]\n",
				__func__, data_size);
			goto ERROR;
			break;
		}

		printk("\n");
		sprintf(data, "\n");
		strcat(info->print_buf, data);
		memset(data, 0, 10);
	}
	//print table

	lim_y = max_y;
	for (i_y = 0; i_y < lim_y; i_y++) {
		//print line header
		if (!info->read_all_data) {
#if defined(CONFIG_TOUCHSCREEN_DUMP_MODE)
			if (info->add_log_header == 1)
				printk("%s mms_tsp", SECLOG);
#endif
			if ((key_num > 0) && (i_y == (lim_y -1))) {
				printk("[TK]");
				sprintf(data, "[TK]");
			} else {
				printk("[%2d]", i_y);
				sprintf(data, "[%2d]", i_y);
			}
			strcat(info->print_buf, data);
			memset(data, 0, 10);
		}

		//print line
		if ((key_num > 0) && (i_y == (lim_y - 1)))
			lim_x = key_num;
		else
			lim_x = max_x;

		for (i_x = 0; i_x < lim_x; i_x++) {
			if (info->read_all_data) {
				sprintf(data, "%d,", info->image_buf[i_y * max_x + i_x]);

			} else {
				switch (data_size) {
				case 1:
					printk(" %2d", info->image_buf[i_y * max_x + i_x]);
					sprintf(data, " %2d", info->image_buf[i_y * max_x + i_x]);
					break;
				case 2:
					printk(" %4d", info->image_buf[i_y * max_x + i_x]);
					sprintf(data, " %4d", info->image_buf[i_y * max_x + i_x]);
					break;
				case 4:
					printk(" %5d", info->image_buf[i_y * max_x + i_x]);
					sprintf(data, " %5u", info->image_buf[i_y * max_x + i_x]);
					break;
				default:
					input_err(true, &info->client->dev, "%s [ERROR] data_size [%d]\n",
						__func__, data_size);
					goto ERROR;
					break;
				}
			}
			strcat(info->print_buf, data);
			memset(data, 0, 10);
		}

		if (!info->read_all_data) {
			printk("\n");
			sprintf(data, "\n");
			strcat(info->print_buf, data);
			memset(data, 0, 10);
		}
	}

	printk("\n");
	sprintf(data, "\n");
	strcat(info->print_buf, data);
	memset(data, 0, 10);

	input_dbg(true, &info->client->dev, "%s [DONE]\n", __func__);
	return 0;

ERROR:
	input_err(true, &info->client->dev, "%s [ERROR]\n", __func__);
	return 1;
}

/*
* Process vector data
*/
static int mms_proc_vector_data(struct mms_ts_info *info, u8 data_type_size, u8 data_type_sign, u8 buf_addr_h, u8 buf_addr_l, u8 key_num, u8 vector_num, u16 *vector_id, u16 *vector_elem_num, int table_size)
{
	char data[10];
	int i, i_line, i_vector, i_elem;
	int sValue = 0;
	unsigned int uValue = 0;
	int value = 0;
	int size = 0;
	u8 wbuf[8];
	u8 rbuf[512];
	unsigned int buf_addr;
	int key_exist = 0;
	int total_len = 0;
	int elem_len = 0;
	int vector_total_len = 0;

	input_dbg(true, &info->client->dev, "%s [START]\n", __func__);

	memset(data, 0, 10);

	for (i = 0; i < vector_num; i++) {
		vector_total_len += vector_elem_num[i];
	}
	total_len = key_num + vector_total_len;

	for (i = 0; i < vector_num; i++) {
		input_dbg(true, &info->client->dev, "%s - vector_elem_num(%d)[%d]\n", __func__, i, vector_elem_num[i]);
	}
	input_dbg(true, &info->client->dev, "%s - key_num[%d] total_len[%d]\n", __func__, key_num, total_len);

	if (key_num > 0) {
		key_exist = 1;
	} else {
		key_exist = 0;
	}

	//get line data
	size = (key_num + vector_total_len) * data_type_size;
	buf_addr = (buf_addr_h << 8) | buf_addr_l;
	
	wbuf[0] = (buf_addr >> 8) & 0xFF;
	wbuf[1] = buf_addr & 0xFF;
	if (mms_i2c_read(info, wbuf, 2, rbuf, size)) {
		input_err(true, &info->client->dev, "%s [ERROR] Read data buffer\n", __func__);
		goto error;
	}

	//save data
	for (i = 0; i < total_len; i++) {
		if (data_type_sign == 0) {
			//unsigned
			switch (data_type_size) {
			case 1:
				uValue = (u8)rbuf[i];
				break;
			case 2:
				uValue = (u16)(rbuf[data_type_size * i] | (rbuf[data_type_size * i + 1] << 8));
				break;
			case 4: 				
				uValue = (u32)(rbuf[data_type_size * i] | (rbuf[data_type_size * i + 1] << 8) | (rbuf[data_type_size * i + 2] << 16) | (rbuf[data_type_size * i + 3] << 24));
				break;
			default:
				input_err(true, &info->client->dev, "%s [ERROR] data_type_size[%d]\n", __func__, data_type_size);
				goto error;
				break;
			}
			value = (int)uValue;
		} else {
			//signed
			switch (data_type_size) {
			case 1:
				sValue = (s8)rbuf[i];
				break;
			case 2:
				sValue = (s16)(rbuf[data_type_size * i] | (rbuf[data_type_size * i + 1] << 8));
				break;
			case 4:
				sValue = (s32)(rbuf[data_type_size * i] | (rbuf[data_type_size * i + 1] << 8) | (rbuf[data_type_size * i + 2] << 16) | (rbuf[data_type_size * i + 3] << 24));
				break;
			default:
				input_err(true, &info->client->dev, "%s [ERROR] data_type_size[%d]\n", __func__, data_type_size);
				goto error;
				break;
			}
			value = (int)sValue;
		}
	
		info->image_buf[table_size + i] = value;
	}

	//print header
	i_vector = 0;
	i_elem = 0;
	for (i_line = 0; i_line < (key_exist + vector_num); i_line++) {
		if (!info->read_all_data) {
			if ((i_line == 0) && (key_exist == 1)) {
				elem_len = key_num;
				printk("[Key]");
				sprintf(data, "[Key]");
			} else {
				elem_len = vector_elem_num[i_vector];
				if (elem_len <= 0) {
					continue;
				}
				switch (vector_id[i_vector]) {
				case 1:
					printk("[Screen Rx]");
					sprintf(data, "[Screen Rx]");
					break;
				case 2:
					printk("[Screen Tx]");
					sprintf(data, "[Screen Tx]");
					break;
				case 3:
					printk("[Key Rx]");
					sprintf(data, "[Key Rx]");
					break;
				case 4:
					printk("[Key Tx]");
					sprintf(data, "[Key Tx]");
					break;
				case 5:
					printk("[Pressure]");
					sprintf(data, "[Pressure]");
					break;
				default:
					printk("[Vector %d]", i_vector);
					sprintf(data, "[Vector %d]", i_vector);
					break;
				}
				i_vector++;
			}
			strcat(info->print_buf, data);
			memset(data, 0, 10);
		}else {
			if ((i_line == 0) && (key_exist == 1)) {
				elem_len = key_num;
			} else {
				elem_len = vector_elem_num[i_vector];
				if (elem_len <= 0) {
					continue;
				}
				i_vector++;
			}
		}

		//print line
		for (i = i_elem; i < (i_elem + elem_len); i++) {
			switch (data_type_size) {
			case 1:
				printk(" %3d", info->image_buf[table_size + i]);
				sprintf(data, " %3d", info->image_buf[table_size + i]);
				break;
			case 2:
				printk(" %5d", info->image_buf[table_size + i]);
				sprintf(data, " %5d", info->image_buf[table_size + i]);
				break;
			case 4:
				printk(" %6d", info->image_buf[table_size + i]);
				sprintf(data, " %6d", info->image_buf[table_size + i]);
				break;
			default:
				input_err(true, &info->client->dev, "%s [ERROR] data_type_size[%d]\n", __func__, data_type_size);
				goto error;
				break;
			}

			strcat(info->print_buf, data);
			memset(data, 0, 10);
		}

		if (!info->read_all_data) {
			printk("\n");
			sprintf(data, "\n");
			strcat(info->print_buf, data);
			memset(data, 0, 10);
		}

		i_elem += elem_len;
	}

	if (info->read_all_data) {
		sprintf(data, "\n");
		strcat(info->print_buf, data);
		memset(data, 0, 10);
	}

	input_dbg(true, &info->client->dev, "%s [DONE]\n", __func__);
	return 0;

error:
	input_err(true, &info->client->dev, "%s [ERROR]\n", __func__);
	return 1;
}

/**
 * Run test
 */
int mms_run_test(struct mms_ts_info *info, u8 test_type)
{
	int busy_cnt = 50;
	int wait_cnt = 50;
	u8 wbuf[8];
	u8 rbuf[512];
	u8 size = 0;
	u8 row_num;
	u8 col_num;
	u8 buffer_col_num;
	u8 rotate;
	u8 key_num;
	u8 data_type;
	u8 data_type_size;
	u8 data_type_sign;
	u8 vector_num = 0;
	u16 vector_id[16];
	u16 vector_elem_num[16];
	u8 buf_addr_h; 
	u8 buf_addr_l;
	u16 buf_addr;
	int table_size;
	int i;
	int ret = 1;

	input_dbg(true, &info->client->dev, "%s [START]\n", __func__);
	input_info(true, &info->client->dev, "%s - test_type[%d]\n", __func__, test_type);

	while (busy_cnt--) {
		if (info->test_busy == false) {
			break;
		}
		msleep(10);
	}
	mutex_lock(&info->lock);
	info->test_busy = true;
	mutex_unlock(&info->lock);

	memset(info->print_buf, 0, PAGE_SIZE);

	disable_irq(info->client->irq);
	mms_clear_input(info);

	//disable touch event
	wbuf[0] = MIP_R0_CTRL;
	wbuf[1] = MIP_R1_CTRL_EVENT_TRIGGER_TYPE;
	wbuf[2] = MIP_TRIGGER_TYPE_NONE;
	if (mms_i2c_write(info, wbuf, 3)) {
		input_err(true, &info->client->dev, "%s [ERROR] Disable event\n", __func__);
		goto EXIT;
	}

	if (!info->read_all_data) {
		//check test type
		switch (test_type) {
		case MIP_TEST_TYPE_CM_DELTA:
			//printk("=== Cm Delta Test ===\n");
			sprintf(info->print_buf, "\n=== Cm Delta Test ===\n\n");
			break;
		case MIP_TEST_TYPE_CM_ABS:
			//printk("=== Cm Abs Test ===\n");
			sprintf(info->print_buf, "\n=== Cm Abs Test ===\n\n");
			break;
		case MIP_TEST_TYPE_CM_JITTER:
			//printk("=== Cm Jitter Test ===\n");
			sprintf(info->print_buf, "\n=== Cm Jitter Test ===\n\n");
			break;
		case MIP_TEST_TYPE_CP_SHORT:
			//printk("=== Cp Short Test ===\n");
			sprintf(info->print_buf, "\n=== Cp Short Test ===\n\n");
			break;
		case MIP_TEST_TYPE_CP_LPM:
			//printk("=== Cp (LPM) Test ===\n");
			sprintf(info->print_buf, "\n=== Cp (LPM) Test ===\n\n");
			break;
		default:
			input_err(true, &info->client->dev, "%s [ERROR] Unknown test type\n", __func__);
			sprintf(info->print_buf, "\nERROR : Unknown test type\n\n");
			goto EXIT;
			break;
		}
	}
	//set test mode
	wbuf[0] = MIP_R0_CTRL;
	wbuf[1] = MIP_R1_CTRL_MODE;
	wbuf[2] = MIP_CTRL_MODE_TEST_CM;
	if (mms_i2c_write(info, wbuf, 3)) {
		input_err(true, &info->client->dev, "%s [ERROR] Write test mode\n", __func__);
		goto EXIT;
	}

	//wait ready status
	wait_cnt = 50;
	while (wait_cnt--) {
		if (mms_get_ready_status(info) == MIP_CTRL_STATUS_READY) {
			break;
		}
		msleep(50);

		input_dbg(true, &info->client->dev, "%s - wait [%d]\n", __func__, wait_cnt);
	}

	if (wait_cnt <= 0) {
		input_err(true, &info->client->dev, "%s [ERROR] Wait timeout\n", __func__);
		goto EXIT;
	}

	input_info(true, &info->client->dev, "%s - set control mode\n", __func__);

	//set test type
	wbuf[0] = MIP_R0_TEST;
	wbuf[1] = MIP_R1_TEST_TYPE;
	wbuf[2] = test_type;
	if (mms_i2c_write(info, wbuf, 3)) {
		input_err(true, &info->client->dev, "%s [ERROR] Write test type\n", __func__);
		goto EXIT;
	}

	input_info(true, &info->client->dev, "%s - set test type\n", __func__);

	//wait ready status
	wait_cnt = 50;
	while (wait_cnt--) {
		if (mms_get_ready_status(info) == MIP_CTRL_STATUS_READY) {
			break;
		}
//		msleep(10);
		msleep(20);

		input_dbg(true, &info->client->dev, "%s - wait [%d]\n", __func__, wait_cnt);
	}

	if (wait_cnt <= 0) {
		input_err(true, &info->client->dev, "%s [ERROR] Wait timeout\n", __func__);
		goto EXIT;
	}

	input_dbg(true, &info->client->dev, "%s - ready\n", __func__);

	//data format
	wbuf[0] = MIP_R0_TEST;
	wbuf[1] = MIP_R1_TEST_DATA_FORMAT;
	if (mms_i2c_read(info, wbuf, 2, rbuf, 7)) {
		input_err(true, &info->client->dev, "%s [ERROR] Read data format\n", __func__);
		goto EXIT;
	}
	row_num = rbuf[0];
	col_num = rbuf[1];
	buffer_col_num = rbuf[2];
	rotate = rbuf[3];
	key_num = rbuf[4];
	data_type = rbuf[5];

	data_type_sign = (data_type & 0x80) >> 7;
	data_type_size = data_type & 0x7F;
	vector_num = rbuf[6];

	input_info(true, &info->client->dev,
		"%s - row_num[%d] col_num[%d] buffer_col_num[%d] rotate[%d] key_num[%d]\n",
		__func__, row_num, col_num, buffer_col_num, rotate, key_num);
	input_info(true, &info->client->dev,
		"%s - data_type[0x%02X] data_sign[%d] data_size[%d]\n",
		__func__, data_type, data_type_sign, data_type_size);
	input_info(true, &info->client->dev, "%s - vector_num[%d]\n",
		__func__, vector_num);

	if (vector_num > 0) {
		wbuf[0] = MIP_R0_TEST;
		wbuf[1] = MIP_R1_TEST_VECTOR_INFO;
		if (mms_i2c_read(info, wbuf, 2, rbuf, (vector_num * 4))) {
			input_err(true, &info->client->dev, "%s [ERROR] Read vector info\n", __func__);
			goto EXIT;
		}
		for (i = 0; i < vector_num; i++) {
			vector_id[i] = rbuf[i * 4 + 0] | (rbuf[i * 4 + 1] << 8);
			vector_elem_num[i] = rbuf[i * 4 + 2] | (rbuf[i * 4 + 3] << 8);
			input_dbg(true, &info->client->dev, "%s - vector[%d] : id[%d] elem_num[%d]\n", __func__, i, vector_id[i], vector_elem_num[i]);
		}
	}
	
	//get buf addr
	wbuf[0] = MIP_R0_TEST;
	wbuf[1] = MIP_R1_TEST_BUF_ADDR;
	if (mms_i2c_read(info, wbuf, 2, rbuf, 2)) {
		input_err(true, &info->client->dev, "%s [ERROR] Read buf addr\n", __func__);
		goto EXIT;
	}

	buf_addr_l = rbuf[0];
	buf_addr_h = rbuf[1];
	buf_addr = (buf_addr_h << 8) | buf_addr_l;
	input_info(true, &info->client->dev, "%s - buf_addr[0x%02X 0x%02X][0x%04X]\n",
		__func__, buf_addr_h, buf_addr_l, buf_addr);

	//print data
	table_size = row_num * col_num;
	if (mms_proc_table_data(info, size, data_type_size, data_type_sign,
		buf_addr_h, buf_addr_l, row_num, col_num, buffer_col_num, rotate, key_num)) {
		input_err(true, &info->client->dev, "%s [ERROR] mms_proc_table_data\n", __func__);
		goto EXIT;
	}
	if ((key_num > 0) || (vector_num > 0)) {
		if (table_size > 0) {
			buf_addr += (row_num * buffer_col_num * data_type_size);
		}

		buf_addr_l = buf_addr & 0xFF;
		buf_addr_h = (buf_addr >> 8) & 0xFF;
		input_dbg(true, &info->client->dev, "%s - vector buf_addr[0x%02X 0x%02X][0x%04X]\n", __func__, buf_addr_h, buf_addr_l, buf_addr);

		if (mms_proc_vector_data(info, data_type_size, data_type_sign, buf_addr_h, buf_addr_l, key_num, vector_num, vector_id, vector_elem_num, table_size)) {
			input_err(true, &info->client->dev, "%s [ERROR] mip4_ts_proc_vector_data\n", __func__);
			goto EXIT;
		}
	}

	//set normal mode
	wbuf[0] = MIP_R0_CTRL;
	wbuf[1] = MIP_R1_CTRL_MODE;
	wbuf[2] = MIP_CTRL_MODE_NORMAL;
	if (mms_i2c_write(info, wbuf, 3)) {
		input_err(true, &info->client->dev, "%s [ERROR] mms_i2c_write\n", __func__);
		goto EXIT;
	}

	//wait ready status
	wait_cnt = 50;
	while (wait_cnt--) {
		if (mms_get_ready_status(info) == MIP_CTRL_STATUS_READY) {
			break;
		}
		msleep(10);

		input_dbg(true, &info->client->dev, "%s - wait [%d]\n", __func__, wait_cnt);
	}

	if (wait_cnt <= 0) {
		input_err(true, &info->client->dev, "%s [ERROR] Wait timeout\n", __func__);
		goto EXIT;
	}

	input_info(true, &info->client->dev, "%s - set normal mode\n", __func__);
	ret = 0;

EXIT:
	//enable touch event
	wbuf[0] = MIP_R0_CTRL;
	wbuf[1] = MIP_R1_CTRL_EVENT_TRIGGER_TYPE;
	wbuf[2] = MIP_TRIGGER_TYPE_INTR;
	if (mms_i2c_write(info, wbuf, 3)) {
		input_err(true, &info->client->dev, "%s [ERROR] Enable event\n", __func__);
		ret = 1;
	}

	mms_reboot(info);
	enable_irq(info->client->irq);

	mutex_lock(&info->lock);
	info->test_busy = false;
	mutex_unlock(&info->lock);

	if (ret)
		input_err(true, &info->client->dev, "%s [ERROR]\n", __func__);
	else
		input_info(true, &info->client->dev, "%s [DONE]\n", __func__);

	return ret;
}

/**
 * Read image data
 */
int mms_get_image(struct mms_ts_info *info, u8 image_type)
{
	int busy_cnt = 50;
	int wait_cnt = 50;
	u8 wbuf[8];
	u8 rbuf[512];
	u8 size = 0;
	u8 row_num;
	u8 col_num;
	u8 buffer_col_num;
	u8 rotate;
	u8 key_num;
	u8 data_type;
	u8 data_type_size;
	u8 data_type_sign;
	u8 buf_addr_h;
	u8 buf_addr_l;
	int ret = 1;

	input_dbg(true, &info->client->dev, "%s [START]\n", __func__);
	input_info(true, &info->client->dev, "%s - image_type[%d]\n", __func__, image_type);

	while (busy_cnt--) {
		if (info->test_busy == false) {
			break;
		}
		msleep(10);
	}
	mutex_lock(&info->lock);
	info->test_busy = true;
	mutex_unlock(&info->lock);

	memset(info->print_buf, 0, PAGE_SIZE);

	if (!info->read_all_data) {
		//check image type
		switch (image_type) {
		case MIP_IMG_TYPE_INTENSITY:
			input_dbg(true, &info->client->dev, "=== Intensity Image ===\n");
			sprintf(info->print_buf, "\n=== Intensity Image ===\n\n");
			break;
		case MIP_IMG_TYPE_RAWDATA:
			input_dbg(true, &info->client->dev, "=== Rawdata Image ===\n");
			sprintf(info->print_buf, "\n=== Rawdata Image ===\n\n");
			break;
		default:
			input_err(true, &info->client->dev, "%s [ERROR] Unknown image type\n", __func__);
			sprintf(info->print_buf, "\nERROR : Unknown image type\n\n");
			goto ERROR;
			break;
		}
	}

	//set image type
	wbuf[0] = MIP_R0_IMAGE;
	wbuf[1] = MIP_R1_IMAGE_TYPE;
	wbuf[2] = image_type;
	if (mms_i2c_write(info, wbuf, 3)) {
		input_err(true, &info->client->dev, "%s [ERROR] Write image type\n", __func__);
		goto ERROR;
	}

	input_dbg(true, &info->client->dev, "%s - set image type\n", __func__);

	//wait ready status
	wait_cnt = 50;
	while (wait_cnt--) {
		if (mms_get_ready_status(info) == MIP_CTRL_STATUS_READY) {
			break;
		}
		msleep(10);

		input_dbg(true, &info->client->dev, "%s - wait [%d]\n", __func__, wait_cnt);
	}

	if (wait_cnt <= 0) {
		input_err(true, &info->client->dev, "%s [ERROR] Wait timeout\n", __func__);
		goto ERROR;
	}

	input_dbg(true, &info->client->dev, "%s - ready\n", __func__);

	//data format
	wbuf[0] = MIP_R0_IMAGE;
	wbuf[1] = MIP_R1_IMAGE_DATA_FORMAT;
	if (mms_i2c_read(info, wbuf, 2, rbuf, 6)) {
		input_err(true, &info->client->dev, "%s [ERROR] Read data format\n", __func__);
		goto ERROR;
	}
	row_num = rbuf[0];
	col_num = rbuf[1];
	buffer_col_num = rbuf[2];
	rotate = rbuf[3];
	key_num = rbuf[4];
	data_type = rbuf[5];

	data_type_sign = (data_type & 0x80) >> 7;
	data_type_size = data_type & 0x7F;

	input_info(true, &info->client->dev,
		"%s - row_num[%d] col_num[%d] buffer_col_num[%d] rotate[%d] key_num[%d]\n",
		__func__, row_num, col_num, buffer_col_num, rotate, key_num);
	input_info(true, &info->client->dev,
		"%s - data_type[0x%02X] data_sign[%d] data_size[%d]\n",
		__func__, data_type, data_type_sign, data_type_size);

	//get buf addr
	wbuf[0] = MIP_R0_IMAGE;
	wbuf[1] = MIP_R1_IMAGE_BUF_ADDR;
	if (mms_i2c_read(info, wbuf, 2, rbuf, 2)) {
		input_err(true, &info->client->dev, "%s [ERROR] Read buf addr\n", __func__);
		goto ERROR;
	}

	buf_addr_l = rbuf[0];
	buf_addr_h = rbuf[1];
	input_info(true, &info->client->dev, "%s - buf_addr[0x%02X 0x%02X]\n",
		__func__, buf_addr_h, buf_addr_l);

	//print data
	if (mms_proc_table_data(info, size, data_type_size, data_type_sign,
		buf_addr_h, buf_addr_l, row_num, col_num, buffer_col_num, rotate, key_num)) {
		input_err(true, &info->client->dev, "%s [ERROR] mms_proc_table_data\n", __func__);
		goto ERROR;
	}

	//clear image type
	wbuf[0] = MIP_R0_IMAGE;
	wbuf[1] = MIP_R1_IMAGE_TYPE;
	wbuf[2] = MIP_IMG_TYPE_NONE;
	if (mms_i2c_write(info, wbuf, 3)) {
		input_err(true, &info->client->dev, "%s [ERROR] Clear image type\n", __func__);
		goto ERROR;
	}

	ret = 0;
ERROR:
	mutex_lock(&info->lock);
	info->test_busy = false;
	mutex_unlock(&info->lock);

	if (ret)
		input_err(true, &info->client->dev, "%s [ERROR]\n", __func__);
	else
		input_info(true, &info->client->dev, "%s [DONE]\n", __func__);

	return ret;
}

#if MMS_USE_TEST_MODE
/**
 * Flash memory test
 */
static ssize_t mms_sys_flash_test(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	u8 data[255];
	int ret;
	char fw_path[64];
	const struct firmware *fw;

	dev_dbg(&info->client->dev, "%s [START]\n", __func__);

	mutex_lock(&info->lock);
	disable_irq(info->client->irq);

	snprintf(fw_path, 64, "%s", info->dtdata->fw_name);
	request_firmware(&fw, fw_path, &info->client->dev);

	if (!fw) {
		dev_err(&info->client->dev, "%s [ERROR] request_firmware\n", __func__);
		ret = fw_err_file_open;
		goto exit;
	}

	ret = mms_flash_test(info, fw->data, fw->size);

exit:
	release_firmware(fw);

	enable_irq(info->client->irq);
	mutex_unlock(&info->lock);

	switch (ret) {
	case fw_err_none:
		sprintf(data, "Result : Pass\n");
		break;
	case fw_err_download:
		sprintf(data, "Error : ISC\n");
		break;
	case fw_err_file_type:
		sprintf(data, "Error : File type\n");
		break;
	case fw_err_file_open:
		sprintf(data, "Error : File open\n");
		break;
	default:
		if (ret > 0) {
			sprintf(data, "Result : Fail @ Page #%d\n", ret);
		} else {
			sprintf(data, "Error : Unknown\n");
		}
		break;
	}

	dev_dbg(&info->client->dev, "%s [DONE]\n", __func__);

	ret = snprintf(buf, 255, "%s\n", data);
	return ret;
}

/**
 * Print chip firmware version
 */
static ssize_t mms_sys_fw_version(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	u8 data[255];
	int ret;
	u8 rbuf[16];

	memset(info->print_buf, 0, PAGE_SIZE);

	if (mms_get_fw_version(info, rbuf)) {
		input_err(true, &info->client->dev, "%s [ERROR] mms_get_fw_version\n", __func__);

		sprintf(data, "F/W Version : ERROR\n");
		goto ERROR;
	}

	input_info(true, &info->client->dev,
		"%s - F/W Version : %02X.%02X %02X.%02X %02X.%02X %02X.%02X\n",
		__func__, rbuf[0], rbuf[1], rbuf[2], rbuf[3],
		rbuf[4],rbuf[5], rbuf[6], rbuf[7]);
	sprintf(data, "F/W Version : %02X.%02X %02X.%02X %02X.%02X %02X.%02X\n",
		rbuf[0], rbuf[1], rbuf[2], rbuf[3], rbuf[4], rbuf[5], rbuf[6], rbuf[7]);

ERROR:
	strcat(info->print_buf, data);

	ret = snprintf(buf, PAGE_SIZE, "%s\n", info->print_buf);
	return ret;
}

/**
 * Print channel info
 */
static ssize_t mms_sys_info(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	u8 data[255];
	int ret;
	u8 rbuf[32];
	u8 wbuf[8];
	int res_x, res_y;

	memset(info->print_buf, 0, PAGE_SIZE);

	sprintf(data, "\n");
	strcat(info->print_buf, data);

	mms_get_fw_version(info, rbuf);
	sprintf(data, "F/W Version : %02X.%02X %02X.%02X %02X.%02X %02X.%02X\n",
		rbuf[0], rbuf[1], rbuf[2], rbuf[3], rbuf[4], rbuf[5], rbuf[6], rbuf[7]);
	strcat(info->print_buf, data);

	wbuf[0] = MIP_R0_INFO;
	wbuf[1] = MIP_R1_INFO_PRODUCT_NAME;
	mms_i2c_read(info, wbuf, 2, rbuf, 16);
	sprintf(data, "Product Name : %s\n", rbuf);
	strcat(info->print_buf, data);

	wbuf[0] = MIP_R0_INFO;
	wbuf[1] = MIP_R1_INFO_RESOLUTION_X;
	mms_i2c_read(info, wbuf, 2, rbuf, 7);
	res_x = (rbuf[0]) | (rbuf[1] << 8);
	res_y = (rbuf[2]) | (rbuf[3] << 8);
	sprintf(data, "Resolution : X[%d] Y[%d]\n", res_x, res_y);
	strcat(info->print_buf, data);

	sprintf(data, "Node Num : X[%d] Y[%d] Key[%d]\n", rbuf[4], rbuf[5], rbuf[6]);
	strcat(info->print_buf, data);

	ret = snprintf(buf, PAGE_SIZE, "%s\n", info->print_buf);
	return ret;

}

/**
 * Device enable
 */
static ssize_t mms_sys_device_enable(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = info->client;
	u8 data[255];
	int ret;

	memset(info->print_buf,0,PAGE_SIZE);

	mms_enable(info);

	input_info(true, &client->dev, "%s", __func__);

	sprintf(data, "Device : Enabled\n");
	strcat(info->print_buf,data);

	ret = snprintf(buf,PAGE_SIZE,"%s\n",info->print_buf);
	return ret;

}

/**
 * Device disable
 */
static ssize_t mms_sys_device_disable(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = info->client;
	u8 data[255];
	int ret;

	memset(info->print_buf,0,PAGE_SIZE);

	mms_disable(info);

	input_info(true, &client->dev, "%s", __func__);

	sprintf(data, "Device : Disabled\n");
	strcat(info->print_buf,data);

	ret = snprintf(buf,PAGE_SIZE,"%s\n",info->print_buf);
	return ret;

}

/**
 * Enable IRQ
 */
static ssize_t mms_sys_irq_enable(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = info->client;
	u8 data[255];
	int ret;

	memset(info->print_buf,0,PAGE_SIZE);

	enable_irq(info->irq);

	input_info(true, &client->dev, "%s\n", __func__);

	sprintf(data, "IRQ : Enabled\n");
	strcat(info->print_buf,data);

	ret = snprintf(buf,PAGE_SIZE,"%s\n",info->print_buf);
	return ret;

}

/**
 * Disable IRQ
 */
static ssize_t mms_sys_irq_disable(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = info->client;
	u8 data[255];
	int ret;

	memset(info->print_buf,0,PAGE_SIZE);

	disable_irq(info->irq);
	mms_clear_input(info);

	dev_info(&client->dev, "%s\n", __func__);

	sprintf(data, "IRQ : Disabled\n");
	strcat(info->print_buf,data);

	ret = snprintf(buf,PAGE_SIZE,"%s\n",info->print_buf);
	return ret;

}

/**
 * Power on
 */
static ssize_t mms_sys_power_on(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = info->client;
	u8 data[255];
	int ret;

	memset(info->print_buf,0,PAGE_SIZE);

	mms_power_control(info, 1);

	if (info->flip_enable)
		mms_set_cover_type(info);

	dev_info(&client->dev, "%s", __func__);

	sprintf(data, "Power : On\n");
	strcat(info->print_buf,data);

	ret = snprintf(buf,PAGE_SIZE,"%s\n",info->print_buf);
	return ret;

}

/**
 * Power off
 */
static ssize_t mms_sys_power_off(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = info->client;
	u8 data[255];
	int ret;

	memset(info->print_buf,0,PAGE_SIZE);

	mms_power_control(info, 0);

	dev_info(&client->dev, "%s", __func__);

	sprintf(data, "Power : Off\n");
	strcat(info->print_buf,data);

	ret = snprintf(buf,PAGE_SIZE,"%s\n",info->print_buf);
	return ret;

}

/**
 * Reboot chip
 */
static ssize_t mms_sys_reboot(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = info->client;
	u8 data[255];
	int ret;

	memset(info->print_buf, 0, PAGE_SIZE);

	input_info(true, &client->dev, "%s", __func__);

	disable_irq(info->irq);
	mms_clear_input(info);
	mms_reboot(info);
	enable_irq(info->irq);

	sprintf(data, "Reboot\n");
	strcat(info->print_buf,data);

	ret = snprintf(buf,PAGE_SIZE,"%s\n",info->print_buf);
	return ret;

}

/**
 * Set glove mode
 */
static ssize_t mms_sys_glove_mode_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	u8 wbuf[8];

	input_dbg(true, &info->client->dev, "%s [START] \n", __func__);

	wbuf[0] = MIP_R0_CTRL;
	wbuf[1] = MIP_R1_CTRL_GLOVE_MODE;
	wbuf[2] = buf[0];

	if ((buf[0] == 0) || (buf[0] == 1)) {
		if (mms_i2c_write(info, wbuf, 3))
			input_err(true, &info->client->dev, "%s [ERROR] mms_i2c_write\n", __func__);
		else
			input_info(true, &info->client->dev, "%s - value[%d]\n", __func__, buf[0]);
	} else
		input_err(true, &info->client->dev, "%s [ERROR] Unknown value\n", __func__);

	input_dbg(true, &info->client->dev, "%s [DONE] \n", __func__);

	return count;
}

/**
 * Get glove mode
 */
static ssize_t mms_sys_glove_mode_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	u8 data[255];
	int ret;
	u8 wbuf[8];
	u8 rbuf[4];

	memset(info->print_buf, 0, PAGE_SIZE);

	input_dbg(true, &info->client->dev, "%s [START] \n", __func__);

	wbuf[0] = MIP_R0_CTRL;
	wbuf[1] = MIP_R1_CTRL_GLOVE_MODE;

	if (mms_i2c_read(info, wbuf, 2, rbuf, 1)) {
		input_err(true, &info->client->dev, "%s [ERROR] mms_i2c_read\n", __func__);
		sprintf(data, "\nGlove Mode : ERROR\n");
	} else {
		input_info(true, &info->client->dev, "%s - value[%d]\n", __func__, rbuf[0]);
		sprintf(data, "\nGlove Mode : %d\n", rbuf[0]);
	}

	input_dbg(true, &info->client->dev, "%s [DONE] \n", __func__);

	strcat(info->print_buf, data);
	ret = snprintf(buf, PAGE_SIZE, "%s\n", info->print_buf);
	return ret;
}

/**
 * Set charger mode
 */
static ssize_t mms_sys_charger_mode_store(struct device *dev,
			struct device_attribute *attr, const char *buf, size_t count)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	u8 wbuf[8];

	input_dbg(true, &info->client->dev, "%s [START] \n", __func__);

	wbuf[0] = MIP_R0_CTRL;
	wbuf[1] = MIP_R1_CTRL_CHARGER_MODE;
	wbuf[2] = buf[0];

	if ((buf[0] == 0) || (buf[0] == 1)) {
		if (mms_i2c_write(info, wbuf, 3))
			input_err(true, &info->client->dev, "%s [ERROR] mms_i2c_write\n", __func__);
		else
			input_info(true, &info->client->dev, "%s - value[%d]\n", __func__, buf[0]);
	} else
		input_err(true, &info->client->dev, "%s [ERROR] Unknown value\n", __func__);

	input_dbg(true, &info->client->dev, "%s [DONE] \n", __func__);

	return count;
}

/**
 * Get charger mode
 */
static ssize_t mms_sys_charger_mode_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	u8 data[255];
	int ret;
	u8 wbuf[8];
	u8 rbuf[4];

	memset(info->print_buf, 0, PAGE_SIZE);

	input_dbg(true, &info->client->dev, "%s [START] \n", __func__);

	wbuf[0] = MIP_R0_CTRL;
	wbuf[1] = MIP_R1_CTRL_CHARGER_MODE;

	if (mms_i2c_read(info, wbuf, 2, rbuf, 1)) {
		input_err(true, &info->client->dev, "%s [ERROR] mms_i2c_read\n", __func__);
		sprintf(data, "\nCharger Mode : ERROR\n");
	} else {
		input_info(true, &info->client->dev, "%s - value[%d]\n", __func__, rbuf[0]);
		sprintf(data, "\nCharger Mode : %d\n", rbuf[0]);
	}

	input_dbg(true, &info->client->dev, "%s [DONE] \n", __func__);

	strcat(info->print_buf, data);
	ret = snprintf(buf, PAGE_SIZE, "%s\n", info->print_buf);
	return ret;
}

/**
 * Set cover window mode
 */
static ssize_t mms_sys_window_mode_store(struct device *dev,
			struct device_attribute *attr, const char *buf, size_t count)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	u8 wbuf[8];

	input_dbg(true, &info->client->dev, "%s [START] \n", __func__);

	wbuf[0] = MIP_R0_CTRL;
	wbuf[1] = MIP_R1_CTRL_WINDOW_MODE;
	wbuf[2] = buf[0];

	if ((buf[0] == 0) || (buf[0] == 1)) {
		if (mms_i2c_write(info, wbuf, 3))
			input_err(true, &info->client->dev, "%s [ERROR] mms_i2c_write\n", __func__);
		else
			input_info(true, &info->client->dev, "%s - value[%d]\n", __func__, buf[0]);
	} else
		input_err(true, &info->client->dev, "%s [ERROR] Unknown value\n", __func__);

	input_dbg(true, &info->client->dev, "%s [DONE] \n", __func__);

	return count;
}

/**
 * Get cover window mode
 */
static ssize_t mms_sys_window_mode_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	u8 data[255];
	int ret;
	u8 wbuf[8];
	u8 rbuf[4];

	memset(info->print_buf, 0, PAGE_SIZE);

	input_dbg(true, &info->client->dev, "%s [START] \n", __func__);

	wbuf[0] = MIP_R0_CTRL;
	wbuf[1] = MIP_R1_CTRL_WINDOW_MODE;

	if (mms_i2c_read(info, wbuf, 2, rbuf, 1)) {
		input_err(true, &info->client->dev, "%s [ERROR] mms_i2c_read\n", __func__);
		sprintf(data, "\nWindow Mode : ERROR\n");
	} else {
		input_info(true, &info->client->dev, "%s - value[%d]\n", __func__, rbuf[0]);
		sprintf(data, "\nWindow Mode : %d\n", rbuf[0]);
	}

	input_dbg(true, &info->client->dev, "%s [DONE] \n", __func__);

	strcat(info->print_buf, data);
	ret = snprintf(buf, PAGE_SIZE, "%s\n", info->print_buf);
	return ret;
}

/**
 * Set palm rejection mode
 */
static ssize_t mms_sys_palm_rejection_mode_store(struct device *dev,
			struct device_attribute *attr, const char *buf, size_t count)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	u8 wbuf[8];

	input_dbg(true, &info->client->dev, "%s [START] \n", __func__);

	wbuf[0] = MIP_R0_CTRL;
	wbuf[1] = MIP_R1_CTRL_PALM_REJECTION;
	wbuf[2] = buf[0];

	if ((buf[0] == 0) || (buf[0] == 1)) {
		if (mms_i2c_write(info, wbuf, 3))
			input_err(true, &info->client->dev, "%s [ERROR] mms_i2c_write\n", __func__);
		else
			input_info(true, &info->client->dev, "%s - value[%d]\n", __func__, buf[0]);
	} else
		input_err(true, &info->client->dev, "%s [ERROR] Unknown value\n", __func__);

	input_dbg(true, &info->client->dev, "%s [DONE] \n", __func__);

	return count;
}

/**
 * Get palm rejection mode
 */
static ssize_t mms_sys_palm_rejection_mode_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	u8 data[255];
	int ret;
	u8 wbuf[8];
	u8 rbuf[4];

	memset(info->print_buf, 0, PAGE_SIZE);

	input_dbg(true, &info->client->dev, "%s [START] \n", __func__);

	wbuf[0] = MIP_R0_CTRL;
	wbuf[1] = MIP_R1_CTRL_PALM_REJECTION;

	if (mms_i2c_read(info, wbuf, 2, rbuf, 1)) {
		input_err(true, &info->client->dev, "%s [ERROR] mms_i2c_read\n", __func__);
		sprintf(data, "\nPalm Rejection Mode : ERROR\n");
	} else {
		input_info(true, &info->client->dev, "%s - value[%d]\n", __func__, rbuf[0]);
		sprintf(data, "\nPalm Rejection Mode : %d\n", rbuf[0]);
	}

	input_dbg(true, &info->client->dev, "%s [DONE] \n", __func__);

	strcat(info->print_buf, data);
	ret = snprintf(buf, PAGE_SIZE, "%s\n", info->print_buf);
	return ret;
}

/**
 * Sysfs print intensity image
 */
static ssize_t mms_sys_intensity(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	int ret;

	input_dbg(true, &info->client->dev, "%s [START] \n", __func__);

	if (mms_get_image(info, MIP_IMG_TYPE_INTENSITY)) {
		input_err(true, &info->client->dev, "%s [ERROR] mms_get_image\n", __func__);
		return -1;
	}

	input_dbg(true, &info->client->dev, "%s [DONE] \n", __func__);

	ret = snprintf(buf, PAGE_SIZE, "%s\n", info->print_buf);
	return ret;
}

/**
 * Sysfs print rawdata image
 */
static ssize_t mms_sys_rawdata(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	int ret;

	input_dbg(true, &info->client->dev, "%s [START] \n", __func__);

	if (mms_get_image(info, MIP_IMG_TYPE_RAWDATA)) {
		input_err(true, &info->client->dev, "%s [ERROR] mms_get_image\n", __func__);
		return -1;
	}

	input_dbg(true, &info->client->dev, "%s [DONE] \n", __func__);

	ret = snprintf(buf, PAGE_SIZE, "%s\n", info->print_buf);
	return ret;
}

/**
 * Sysfs run cm delta test
 */
static ssize_t mms_sys_test_cm_delta(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	int ret;

	input_dbg(true, &info->client->dev, "%s [START] \n", __func__);

	if (mms_run_test(info, MIP_TEST_TYPE_CM_DELTA)) {
		input_err(true, &info->client->dev, "%s [ERROR] mms_run_test\n", __func__);
		return -1;
	}

	input_dbg(true, &info->client->dev, "%s [DONE] \n", __func__);

	ret = snprintf(buf, PAGE_SIZE, "%s\n", info->print_buf);
	return ret;
}

/**
 * Sysfs run cm abs test
 */
static ssize_t mms_sys_test_cm_abs(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	int ret;

	input_dbg(true, &info->client->dev, "%s [START] \n", __func__);

	if (mms_run_test(info, MIP_TEST_TYPE_CM_ABS)) {
		input_err(true, &info->client->dev, "%s [ERROR] mms_run_test\n", __func__);
		return -1;
	}

	input_dbg(true, &info->client->dev, "%s [DONE] \n", __func__);

	ret = snprintf(buf, PAGE_SIZE, "%s\n", info->print_buf);
	return ret;
}

/**
 * Sysfs run cm jitter test
 */
static ssize_t mms_sys_test_cm_jitter(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	int ret;

	input_dbg(true, &info->client->dev, "%s [START] \n", __func__);

	if (mms_run_test(info, MIP_TEST_TYPE_CM_JITTER)) {
		input_err(true, &info->client->dev, "%s [ERROR] mms_run_test\n", __func__);
		return -1;
	}

	input_dbg(true, &info->client->dev, "%s [DONE] \n", __func__);

	ret = snprintf(buf, PAGE_SIZE, "%s\n", info->print_buf);
	return ret;
}

/**
 * Sysfs run cp short test
 */
static ssize_t mms_sys_test_cp_short(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	int ret;

	input_dbg(true, &info->client->dev, "%s [START] \n", __func__);

	if (mms_run_test(info, MIP_TEST_TYPE_CP_SHORT)) {
		input_err(true, &info->client->dev, "%s [ERROR] mms_run_test\n", __func__);
		return -1;
	}

	input_dbg(true, &info->client->dev, "%s [DONE] \n", __func__);

	ret = snprintf(buf, PAGE_SIZE, "%s\n", info->print_buf);
	return ret;
}

/**
 * Sysfs run cp lpm test
 */
static ssize_t mms_sys_test_cp_lpm(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	int ret;

	input_dbg(true, &info->client->dev, "%s [START] \n", __func__);

	if (mms_run_test(info, MIP_TEST_TYPE_CP_LPM)) {
		input_err(true, &info->client->dev, "%s [ERROR] mms_run_test\n", __func__);
		return -1;
	}

	input_dbg(true, &info->client->dev, "%s [DONE] \n", __func__);

	ret = snprintf(buf, PAGE_SIZE, "%s\n", info->print_buf);
	return ret;
}

/**
 * Sysfs functions
 */
static DEVICE_ATTR(flash_test, S_IRUGO, mms_sys_flash_test, NULL);
static DEVICE_ATTR(fw_version, S_IRUGO, mms_sys_fw_version, NULL);
static DEVICE_ATTR(info, S_IRUGO, mms_sys_info, NULL);
static DEVICE_ATTR(device_enable, S_IRUGO, mms_sys_device_enable, NULL);
static DEVICE_ATTR(device_disable, S_IRUGO, mms_sys_device_disable, NULL);
static DEVICE_ATTR(irq_enable, S_IRUGO, mms_sys_irq_enable, NULL);
static DEVICE_ATTR(irq_disable, S_IRUGO, mms_sys_irq_disable, NULL);
static DEVICE_ATTR(power_on, S_IRUGO, mms_sys_power_on, NULL);
static DEVICE_ATTR(power_off, S_IRUGO, mms_sys_power_off, NULL);
static DEVICE_ATTR(reboot, S_IRUGO, mms_sys_reboot, NULL);
static DEVICE_ATTR(mode_glove, S_IRUGO | S_IWUSR | S_IWGRP, mms_sys_glove_mode_show, mms_sys_glove_mode_store);
static DEVICE_ATTR(mode_charger, S_IRUGO | S_IWUSR | S_IWGRP,
			mms_sys_charger_mode_show, mms_sys_charger_mode_store);
static DEVICE_ATTR(mode_cover_window, S_IRUGO | S_IWUSR | S_IWGRP,
			mms_sys_window_mode_show, mms_sys_window_mode_store);
static DEVICE_ATTR(mode_palm_rejection, S_IRUGO | S_IWUSR | S_IWGRP,
			mms_sys_palm_rejection_mode_show, mms_sys_palm_rejection_mode_store);
static DEVICE_ATTR(image_intensity, S_IRUGO, mms_sys_intensity, NULL);
static DEVICE_ATTR(image_rawdata, S_IRUGO, mms_sys_rawdata, NULL);
static DEVICE_ATTR(test_cm_delta, S_IRUGO, mms_sys_test_cm_delta, NULL);
static DEVICE_ATTR(test_cm_abs, S_IRUGO, mms_sys_test_cm_abs, NULL);
static DEVICE_ATTR(test_cm_jitter, S_IRUGO, mms_sys_test_cm_jitter, NULL);
static DEVICE_ATTR(test_cp_short, S_IRUGO, mms_sys_test_cp_short, NULL);
static DEVICE_ATTR(test_cp_lpm, S_IRUGO, mms_sys_test_cp_lpm, NULL);


/**
 * Sysfs attr list info
 */
static struct attribute *mms_test_attr[] = {
	&dev_attr_flash_test.attr,
	&dev_attr_fw_version.attr,
	&dev_attr_info.attr,
	&dev_attr_device_enable.attr,
	&dev_attr_device_disable.attr,
	&dev_attr_irq_enable.attr,
	&dev_attr_irq_disable.attr,
	&dev_attr_power_on.attr,
	&dev_attr_power_off.attr,
	&dev_attr_reboot.attr,
	&dev_attr_mode_glove.attr,
	&dev_attr_mode_charger.attr,
	&dev_attr_mode_cover_window.attr,
	&dev_attr_mode_palm_rejection.attr,
	&dev_attr_image_intensity.attr,
	&dev_attr_image_rawdata.attr,
	&dev_attr_test_cm_delta.attr,
	&dev_attr_test_cm_abs.attr,
	&dev_attr_test_cm_jitter.attr,
	&dev_attr_test_cp_short.attr,
	&dev_attr_test_cp_lpm.attr,
	NULL,
};

/**
 * Sysfs attr group info
 */
static const struct attribute_group mms_test_attr_group = {
	.attrs = mms_test_attr,
};

/**
 * Create sysfs test functions
 */
int mms_sysfs_create(struct mms_ts_info *info)
{
	struct i2c_client *client = info->client;

	input_dbg(true, &info->client->dev, "%s [START]\n", __func__);

	if (sysfs_create_group(&client->dev.kobj, &mms_test_attr_group)) {
		input_err(true, &client->dev, "%s [ERROR] sysfs_create_group\n", __func__);
		return -EAGAIN;
	}

#if !(MMS_USE_CMD_MODE)
	info->print_buf = kzalloc(sizeof(u8) * 4096, GFP_KERNEL);
#endif
	info->image_buf =
		kzalloc(sizeof(int) * ((info->node_x * info->node_y) + info->node_key),
			GFP_KERNEL);

	input_dbg(true, &info->client->dev, "%s [DONE]\n", __func__);

	return 0;
}

/**
 * Remove sysfs test functions
 */
void mms_sysfs_remove(struct mms_ts_info *info)
{
	input_dbg(true, &info->client->dev, "%s [START]\n", __func__);

	sysfs_remove_group(&info->client->dev.kobj, &mms_test_attr_group);

#if !(MMS_USE_CMD_MODE)
	kfree(info->print_buf);
#endif
	kfree(info->image_buf);

	input_dbg(true, &info->client->dev, "%s [DONE]\n", __func__);

	return;
}

#endif