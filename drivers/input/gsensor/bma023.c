/* drivers/i2c/chips/bma023.c - bma023 compass driver
 *
 * Copyright (C) 2007-2008 HTC Corporation.
 * Author: Hou-Kun Chen <houkun.chen@gmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/freezer.h>
#include <mach/gpio.h>
#include <mach/board.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif


#if 0
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif


#define SENSOR_NAME 			"bma150"
#define GRAVITY_EARTH                   9806550
#define ABSMIN_2G                       (-GRAVITY_EARTH * 2)
#define ABSMAX_2G                       (GRAVITY_EARTH * 2)
#define BMA150_MAX_DELAY		200
#define BMA150_CHIP_ID			2
#define BMA150_RANGE_SET		0
#define BMA150_BW_SET			4



#define BMA150_CHIP_ID_REG			0x00
#define BMA150_X_AXIS_LSB_REG		0x02
#define BMA150_X_AXIS_MSB_REG		0x03
#define BMA150_Y_AXIS_LSB_REG		0x04
#define BMA150_Y_AXIS_MSB_REG		0x05
#define BMA150_Z_AXIS_LSB_REG		0x06
#define BMA150_Z_AXIS_MSB_REG		0x07
#define BMA150_STATUS_REG	0x09
#define BMA150_CTRL_REG		0x0a
#define BMA150_CONF1_REG	0x0b

#define BMA150_CUSTOMER1_REG		0x12
#define BMA150_CUSTOMER2_REG		0x13
#define BMA150_RANGE_BWIDTH_REG	0x14
#define BMA150_CONF2_REG	0x15

#define BMA150_OFFS_GAIN_X_REG		0x16
#define BMA150_OFFS_GAIN_Y_REG		0x17
#define BMA150_OFFS_GAIN_Z_REG		0x18
#define BMA150_OFFS_GAIN_T_REG		0x19
#define BMA150_OFFSET_X_REG		0x1a
#define BMA150_OFFSET_Y_REG		0x1b
#define BMA150_OFFSET_Z_REG		0x1c
#define BMA150_OFFSET_T_REG		0x1d

#define BMA150_CHIP_ID__POS		0
#define BMA150_CHIP_ID__MSK		0x07
#define BMA150_CHIP_ID__LEN		3
#define BMA150_CHIP_ID__REG		BMA150_CHIP_ID_REG

/* DATA REGISTERS */

#define BMA150_NEW_DATA_X__POS  	0
#define BMA150_NEW_DATA_X__LEN  	1
#define BMA150_NEW_DATA_X__MSK  	0x01
#define BMA150_NEW_DATA_X__REG		BMA150_X_AXIS_LSB_REG

#define BMA150_ACC_X_LSB__POS   	6
#define BMA150_ACC_X_LSB__LEN   	2
#define BMA150_ACC_X_LSB__MSK		0xC0
#define BMA150_ACC_X_LSB__REG		BMA150_X_AXIS_LSB_REG

#define BMA150_ACC_X_MSB__POS   	0
#define BMA150_ACC_X_MSB__LEN   	8
#define BMA150_ACC_X_MSB__MSK		0xFF
#define BMA150_ACC_X_MSB__REG		BMA150_X_AXIS_MSB_REG

#define BMA150_ACC_Y_LSB__POS   	6
#define BMA150_ACC_Y_LSB__LEN   	2
#define BMA150_ACC_Y_LSB__MSK   	0xC0
#define BMA150_ACC_Y_LSB__REG		BMA150_Y_AXIS_LSB_REG

#define BMA150_ACC_Y_MSB__POS   	0
#define BMA150_ACC_Y_MSB__LEN   	8
#define BMA150_ACC_Y_MSB__MSK   	0xFF
#define BMA150_ACC_Y_MSB__REG		BMA150_Y_AXIS_MSB_REG

#define BMA150_ACC_Z_LSB__POS   	6
#define BMA150_ACC_Z_LSB__LEN   	2
#define BMA150_ACC_Z_LSB__MSK		0xC0
#define BMA150_ACC_Z_LSB__REG		BMA150_Z_AXIS_LSB_REG

#define BMA150_ACC_Z_MSB__POS   	0
#define BMA150_ACC_Z_MSB__LEN   	8
#define BMA150_ACC_Z_MSB__MSK		0xFF
#define BMA150_ACC_Z_MSB__REG		BMA150_Z_AXIS_MSB_REG

/* CONTROL BITS */

#define BMA150_SLEEP__POS			0
#define BMA150_SLEEP__LEN			1
#define BMA150_SLEEP__MSK			0x01
#define BMA150_SLEEP__REG			BMA150_CTRL_REG

#define BMA150_SOFT_RESET__POS		1
#define BMA150_SOFT_RESET__LEN		1
#define BMA150_SOFT_RESET__MSK		0x02
#define BMA150_SOFT_RESET__REG		BMA150_CTRL_REG

#define BMA150_EE_W__POS			4
#define BMA150_EE_W__LEN			1
#define BMA150_EE_W__MSK			0x10
#define BMA150_EE_W__REG			BMA150_CTRL_REG

#define BMA150_UPDATE_IMAGE__POS	5
#define BMA150_UPDATE_IMAGE__LEN	1
#define BMA150_UPDATE_IMAGE__MSK	0x20
#define BMA150_UPDATE_IMAGE__REG	BMA150_CTRL_REG

#define BMA150_RESET_INT__POS		6
#define BMA150_RESET_INT__LEN		1
#define BMA150_RESET_INT__MSK		0x40
#define BMA150_RESET_INT__REG		BMA150_CTRL_REG

/* BANDWIDTH dependend definitions */

#define BMA150_BANDWIDTH__POS				0
#define BMA150_BANDWIDTH__LEN			 	3
#define BMA150_BANDWIDTH__MSK			 	0x07
#define BMA150_BANDWIDTH__REG				BMA150_RANGE_BWIDTH_REG

/* RANGE */

#define BMA150_RANGE__POS				3
#define BMA150_RANGE__LEN				2
#define BMA150_RANGE__MSK				0x18
#define BMA150_RANGE__REG				BMA150_RANGE_BWIDTH_REG

/* WAKE UP */

#define BMA150_WAKE_UP__POS			0
#define BMA150_WAKE_UP__LEN			1
#define BMA150_WAKE_UP__MSK			0x01
#define BMA150_WAKE_UP__REG			BMA150_CONF2_REG

#define BMA150_WAKE_UP_PAUSE__POS		1
#define BMA150_WAKE_UP_PAUSE__LEN		2
#define BMA150_WAKE_UP_PAUSE__MSK		0x06
#define BMA150_WAKE_UP_PAUSE__REG		BMA150_CONF2_REG

#define BMA150_GET_BITSLICE(regvar, bitname)\
	((regvar & bitname##__MSK) >> bitname##__POS)


#define BMA150_SET_BITSLICE(regvar, bitname, val)\
	((regvar & ~bitname##__MSK) | ((val<<bitname##__POS)&bitname##__MSK))

/* range and bandwidth */

#define BMA150_RANGE_2G			0
#define BMA150_RANGE_4G			1
#define BMA150_RANGE_8G			2

#define BMA150_RANGE			2000000
#define BMA150_PRECISION		10
#define BMA150_BOUNDARY			(0x1 << (BMA150_PRECISION - 1))
#define BMA150_GRAVITY_STEP		BMA150_RANGE / BMA150_BOUNDARY

#define BMA150_BW_25HZ		0
#define BMA150_BW_50HZ		1
#define BMA150_BW_100HZ		2
#define BMA150_BW_190HZ		3
#define BMA150_BW_375HZ		4
#define BMA150_BW_750HZ		5
#define BMA150_BW_1500HZ	6

/* mode settings */

#define BMA150_MODE_NORMAL      0
#define BMA150_MODE_SLEEP       2
#define BMA150_MODE_WAKE_UP     3

struct bma150acc{
	s64	x,
		y,
		z;
} ;
static struct  {
	int x;
	int y;
	int z;
}sense_data;
struct bma150_data {
	struct i2c_client *bma150_client;
	atomic_t delay;
	atomic_t enable;
	unsigned char mode;
	struct input_dev *input;
	struct bma150acc value;
	struct mutex value_mutex;
	struct mutex enable_mutex;
	struct mutex mode_mutex;
	struct delayed_work work;
	struct work_struct irq_work;
	struct early_suspend early_suspend;
};
#define RBUFF_SIZE		12	/* Rx buffer size */
#define BMAIO				0xA1

/* IOCTLs for MMA8452 library */
#define BMA_IOCTL_INIT                  _IO(BMAIO, 0x01)
#define BMA_IOCTL_RESET      	          _IO(BMAIO, 0x04)
#define BMA_IOCTL_CLOSE		           _IO(BMAIO, 0x02)
#define BMA_IOCTL_START		             _IO(BMAIO, 0x03)
#define BMA_IOCTL_GETDATA               _IOR(BMAIO, 0x08, char[RBUFF_SIZE+1])

/* IOCTLs for APPs */
#define BMA_IOCTL_APP_SET_RATE		_IOW(BMAIO, 0x10, char)

static long bma023_ioctl( struct file *file, unsigned int cmd, unsigned long arg);

static void bma150_early_suspend(struct early_suspend *h);
static void bma150_late_resume(struct early_suspend *h);

static int bma150_smbus_read_byte(struct i2c_client *client,
		unsigned char reg_addr, unsigned char *data)
{
	s32 dummy;
	dummy = i2c_smbus_read_byte_data(client, reg_addr);
	if (dummy < 0)
		return -1;
	*data = dummy & 0x000000ff;

	return 0;
}

static int bma150_smbus_write_byte(struct i2c_client *client,
		unsigned char reg_addr, unsigned char *data)
{
	s32 dummy;
	dummy = i2c_smbus_write_byte_data(client, reg_addr, *data);
	if (dummy < 0)
		return -1;
	return 0;
}

static int bma150_smbus_read_byte_block(struct i2c_client *client,
		unsigned char reg_addr, unsigned char *data, unsigned char len)
{
	s32 dummy;
	dummy = i2c_smbus_read_i2c_block_data(client, reg_addr, len, data);
	if (dummy < 0)
		return -1;
	return 0;
}

static int bma150_set_mode(struct i2c_client *client, unsigned char Mode)
{
	int comres = 0;
	unsigned char data1, data2;
	struct bma150_data *bma150 = i2c_get_clientdata(client);

	if (client == NULL) {
		comres = -1;
	} else{
		if (Mode < 4 && Mode != 1) {

			comres = bma150_smbus_read_byte(client,
						BMA150_WAKE_UP__REG, &data1);
			data1 = BMA150_SET_BITSLICE(data1,
						BMA150_WAKE_UP, Mode);
			comres += bma150_smbus_read_byte(client,
						BMA150_SLEEP__REG, &data2);
			data2 = BMA150_SET_BITSLICE(data2,
						BMA150_SLEEP, (Mode>>1));
			comres += bma150_smbus_write_byte(client,
						BMA150_WAKE_UP__REG, &data1);
			comres += bma150_smbus_write_byte(client,
						BMA150_SLEEP__REG, &data2);
			mutex_lock(&bma150->mode_mutex);
			bma150->mode = (unsigned char) Mode;
			mutex_unlock(&bma150->mode_mutex);

		} else{
			comres = -1;
		}
	}

	return comres;
}


static int bma150_set_range(struct i2c_client *client, unsigned char Range)
{
	int comres = 0;
	unsigned char data;

	if (client == NULL) {
		comres = -1;
	} else{
		if (Range < 3) {

			comres = bma150_smbus_read_byte(client,
						BMA150_RANGE__REG, &data);
			data = BMA150_SET_BITSLICE(data, BMA150_RANGE, Range);
			comres += bma150_smbus_write_byte(client,
						BMA150_RANGE__REG, &data);

		} else{
			comres = -1;
		}
	}

	return comres;
}

static int bma150_get_range(struct i2c_client *client, unsigned char *Range)
{
	int comres = 0;
	unsigned char data;

	if (client == NULL) {
		comres = -1;
	} else{
		comres = bma150_smbus_read_byte(client,
						BMA150_RANGE__REG, &data);

		*Range = BMA150_GET_BITSLICE(data, BMA150_RANGE);

	}

	return comres;
}



static int bma150_set_bandwidth(struct i2c_client *client, unsigned char BW)
{
	int comres = 0;
	unsigned char data;

	if (client == NULL) {
		comres = -1;
	} else{
		if (BW < 8) {
			comres = bma150_smbus_read_byte(client,
						BMA150_BANDWIDTH__REG, &data);
			data = BMA150_SET_BITSLICE(data, BMA150_BANDWIDTH, BW);
			comres += bma150_smbus_write_byte(client,
						BMA150_BANDWIDTH__REG, &data);

		} else{
			comres = -1;
		}
	}

	return comres;
}

static int bma150_get_bandwidth(struct i2c_client *client, unsigned char *BW)
{
	int comres = 0;
	unsigned char data;

	if (client == NULL) {
		comres = -1;
	} else{


		comres = bma150_smbus_read_byte(client,
						BMA150_BANDWIDTH__REG, &data);

		*BW = BMA150_GET_BITSLICE(data, BMA150_BANDWIDTH);


	}

	return comres;
}

static int bma150_read_accel_xyz(struct i2c_client *client,
		struct bma150acc *acc)
{
	int comres;
	unsigned char data[6];
	if (client == NULL) {
		comres = -1;
	} else{


		comres = bma150_smbus_read_byte_block(client,
					BMA150_ACC_X_LSB__REG, &data[0], 6);

		acc->x = BMA150_GET_BITSLICE(data[0], BMA150_ACC_X_LSB) |
			(BMA150_GET_BITSLICE(data[1], BMA150_ACC_X_MSB)<<
							BMA150_ACC_X_LSB__LEN);	
		if (acc->x < BMA150_BOUNDARY)
       			acc->x = acc->x * BMA150_GRAVITY_STEP;
    		else
       			acc->x = ~( ((~acc->x & (0x7fff>>(16-BMA150_PRECISION)) ) + 1) 
			   			* BMA150_GRAVITY_STEP) + 1;
#if 0
		acc->x = acc->x << (sizeof(short)*8-(BMA150_ACC_X_LSB__LEN+
							BMA150_ACC_X_MSB__LEN));
		acc->x = acc->x >> (sizeof(short)*8-(BMA150_ACC_X_LSB__LEN+
							BMA150_ACC_X_MSB__LEN));
#endif

		acc->y = BMA150_GET_BITSLICE(data[2], BMA150_ACC_Y_LSB) |
			(BMA150_GET_BITSLICE(data[3], BMA150_ACC_Y_MSB)<<
							BMA150_ACC_Y_LSB__LEN);
		if (acc->y < BMA150_BOUNDARY)
       			acc->y = acc->y * BMA150_GRAVITY_STEP;
    		else
       			acc->y = ~( ((~acc->y & (0x7fff>>(16-BMA150_PRECISION)) ) + 1) 
			   			* BMA150_GRAVITY_STEP) + 1;
#if 0
		acc->y = acc->y << (sizeof(short)*8-(BMA150_ACC_Y_LSB__LEN +
							BMA150_ACC_Y_MSB__LEN));
		acc->y = acc->y >> (sizeof(short)*8-(BMA150_ACC_Y_LSB__LEN +
							BMA150_ACC_Y_MSB__LEN));
#endif


		acc->z = BMA150_GET_BITSLICE(data[4], BMA150_ACC_Z_LSB);
		acc->z |= (BMA150_GET_BITSLICE(data[5], BMA150_ACC_Z_MSB)<<
							BMA150_ACC_Z_LSB__LEN);
		if (acc->z < BMA150_BOUNDARY)
       			acc->z = acc->z * BMA150_GRAVITY_STEP;
    		else
       			acc->z = ~( ((~acc->z & (0x7fff>>(16-BMA150_PRECISION)) ) + 1) 
			   			* BMA150_GRAVITY_STEP) + 1;
#if 0
		acc->z = acc->z << (sizeof(short)*8-(BMA150_ACC_Z_LSB__LEN+
							BMA150_ACC_Z_MSB__LEN));
		acc->z = acc->z >> (sizeof(short)*8-(BMA150_ACC_Z_LSB__LEN+
							BMA150_ACC_Z_MSB__LEN));
#endif

	}

	return comres;
}

static void bma150_work_func(struct work_struct *work)
{
	struct bma150_data *bma150 = container_of((struct delayed_work *)work,
			struct bma150_data, work);
	static struct bma150acc acc;
	s32	x,y,z;
	unsigned long delay = msecs_to_jiffies(atomic_read(&bma150->delay));
	struct bma023_platform_data *pdata = pdata = (bma150->bma150_client)->dev.platform_data;

	bma150_read_accel_xyz(bma150->bma150_client, &acc);
	if (pdata->swap_xyz) {
		x = (pdata->orientation[0])*acc.x + (pdata->orientation[1])*acc.y + (pdata->orientation[2])*acc.z;
		y = (pdata->orientation[3])*acc.x + (pdata->orientation[4])*acc.y + (pdata->orientation[5])*acc.z;
		z = (pdata->orientation[6])*acc.x + (pdata->orientation[7])*acc.y + (pdata->orientation[8])*acc.z;
	}
	else {
		x = acc.x;
		y = acc.y;
		z = acc.z;
	}
	input_report_abs(bma150->input, ABS_X, x);
	input_report_abs(bma150->input, ABS_Y, y);
	input_report_abs(bma150->input, ABS_Z, z);
	input_sync(bma150->input);
	mutex_lock(&bma150->value_mutex);
	bma150->value.x = x;
	bma150->value.y = y;
	bma150->value.z = z;
	mutex_unlock(&bma150->value_mutex);
	DBG("bma150_work_func   acc.x=%d,acc.y=%d,acc.z=%d\n",acc.x,acc.y,acc.z);
	schedule_delayed_work(&bma150->work, delay);

}
#if 0
static ssize_t bma150_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma150_data *bma150 = i2c_get_clientdata(client);

	mutex_lock(&bma150->mode_mutex);
	data = bma150->mode;
	mutex_unlock(&bma150->mode_mutex);

	return sprintf(buf, "%d\n", data);
}

static ssize_t bma150_mode_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma150_data *bma150 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	if (bma150_set_mode(bma150->bma150_client, (unsigned char) data) < 0)
		return -EINVAL;


	return count;
}
static ssize_t bma150_range_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma150_data *bma150 = i2c_get_clientdata(client);

	if (bma150_get_range(bma150->bma150_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);
}

static ssize_t bma150_range_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma150_data *bma150 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	if (bma150_set_range(bma150->bma150_client, (unsigned char) data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma150_bandwidth_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma150_data *bma150 = i2c_get_clientdata(client);

	if (bma150_get_bandwidth(bma150->bma150_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma150_bandwidth_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma150_data *bma150 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	if (bma150_set_bandwidth(bma150->bma150_client,
				(unsigned char) data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma150_value_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct input_dev *input = to_input_dev(dev);
	struct bma150_data *bma150 = input_get_drvdata(input);
	struct bma150acc acc_value;

	mutex_lock(&bma150->value_mutex);
	acc_value = bma150->value;
	mutex_unlock(&bma150->value_mutex);

	return sprintf(buf, "%ll %ll %ll\n", acc_value.x, acc_value.y,
			acc_value.z);
}



static ssize_t bma150_delay_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bma150_data *bma150 = i2c_get_clientdata(client);

	return sprintf(buf, "%d\n", atomic_read(&bma150->delay));

}

static ssize_t bma150_delay_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma150_data *bma150 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	if (data > BMA150_MAX_DELAY)
		data = BMA150_MAX_DELAY;
	atomic_set(&bma150->delay, (unsigned int) data);

	return count;
}

static ssize_t bma150_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bma150_data *bma150 = i2c_get_clientdata(client);

	return sprintf(buf, "%d\n", atomic_read(&bma150->enable));

}

static void bma150_set_enable(struct device *dev, int enable)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bma150_data *bma150 = i2c_get_clientdata(client);
	int pre_enable = atomic_read(&bma150->enable);

	mutex_lock(&bma150->enable_mutex);
	if (enable) {
		if (pre_enable ==0) {
			bma150_set_mode(bma150->bma150_client,
							BMA150_MODE_NORMAL);
			schedule_delayed_work(&bma150->work,
				msecs_to_jiffies(atomic_read(&bma150->delay)));
			atomic_set(&bma150->enable, 1);
		}

	} else {
		if (pre_enable ==1) {
			bma150_set_mode(bma150->bma150_client,
							BMA150_MODE_SLEEP);
			cancel_delayed_work_sync(&bma150->work);
			atomic_set(&bma150->enable, 0);
		}
	}
	mutex_unlock(&bma150->enable_mutex);

}

static ssize_t bma150_enable_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	if ((data == 0)||(data==1)) {
		bma150_set_enable(dev,data);
	}

	return count;
}

static DEVICE_ATTR(range, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma150_range_show, bma150_range_store);
static DEVICE_ATTR(bandwidth, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma150_bandwidth_show, bma150_bandwidth_store);
static DEVICE_ATTR(mode, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma150_mode_show, bma150_mode_store);
static DEVICE_ATTR(value, S_IRUGO|S_IWUSR|S_IWGRP,
		bma150_value_show, NULL);
static DEVICE_ATTR(delay, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma150_delay_show, bma150_delay_store);
static DEVICE_ATTR(enable, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma150_enable_show, bma150_enable_store);

static struct attribute *bma150_attributes[] = {
	&dev_attr_range.attr,
	&dev_attr_bandwidth.attr,
	&dev_attr_mode.attr,
	&dev_attr_value.attr,
	&dev_attr_delay.attr,
	&dev_attr_enable.attr,
	NULL
};

static struct attribute_group bma150_attribute_group = {
	.attrs = bma150_attributes
};
#endif
static int bma150_input_init(struct bma150_data *bma150)
{
	struct input_dev *dev;
	int err;

	dev = input_allocate_device();
	if (!dev)
		return -ENOMEM;
	dev->name = "gsensor";//SENSOR_NAME;
	dev->id.bustype = BUS_I2C;

	input_set_capability(dev, EV_ABS, ABS_MISC);
	input_set_abs_params(dev, ABS_X, ABSMIN_2G, ABSMAX_2G, 0, 0);
	input_set_abs_params(dev, ABS_Y, ABSMIN_2G, ABSMAX_2G, 0, 0);
	input_set_abs_params(dev, ABS_Z, ABSMIN_2G, ABSMAX_2G, 0, 0);
	input_set_drvdata(dev, bma150);

	err = input_register_device(dev);
	if (err < 0) {
		input_free_device(dev);
		return err;
	}
	bma150->input = dev;

	return 0;
}

static void bma150_input_delete(struct bma150_data *bma150)
{
	struct input_dev *dev = bma150->input;

	input_unregister_device(dev);
	input_free_device(dev);
}


static int bma023_open(struct inode *inode, struct file *file)
{
	printk("%s\n",__FUNCTION__);
	return 0;//nonseekable_open(inode, file);
}

static int bma023_release(struct inode *inode, struct file *file)
{
	return 0;
}
static struct file_operations bma023_fops = {
	.owner = THIS_MODULE,
	.open = bma023_open,
	.release = bma023_release,
	.unlocked_ioctl = bma023_ioctl,
};
static struct miscdevice bma023_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mma8452_daemon",//"mma8452_daemon",
	.fops = &bma023_fops,
};


static bma023_enable(struct i2c_client *client, int enable)
{
	
	struct bma150_data *bma150 = i2c_get_clientdata(client);
	int pre_enable = atomic_read(&bma150->enable);
	
	mutex_lock(&bma150->enable_mutex);
	
	if(enable)
	{
		if(pre_enable==0)
		{
			bma150_set_mode(client,BMA150_MODE_NORMAL);
			schedule_delayed_work(&bma150->work,
				msecs_to_jiffies(atomic_read(&bma150->delay)));
			atomic_set(&bma150->enable, 1);
		}
	}
	else	
	{
		bma150_set_mode(client,BMA150_MODE_SLEEP);
		cancel_delayed_work_sync(&bma150->work);
		atomic_set(&bma150->enable, 0);
	}
	
	mutex_unlock(&bma150->enable_mutex);

}


static long bma023_ioctl( struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;

	struct i2c_client *client = container_of(bma023_device.parent, struct i2c_client, dev);
	struct bma150_data *bma150 = i2c_get_clientdata(client);;
	switch (cmd) {
	case BMA_IOCTL_START:
		bma023_enable(client, 1);
		DBG("%s:%d,cmd=BMA_IOCTL_START\n",__FUNCTION__,__LINE__);
		break;

	case BMA_IOCTL_CLOSE:
		bma023_enable(client, 0);		
		DBG("%s:%d,cmd=BMA_IOCTL_CLOSE\n",__FUNCTION__,__LINE__);
		break;

	case BMA_IOCTL_APP_SET_RATE:	
		atomic_set(&bma150->delay, 20);//20ms	
		DBG("%s:%d,cmd=BMA_IOCTL_APP_SET_RATE\n",__FUNCTION__,__LINE__);
		break;
		
	case BMA_IOCTL_GETDATA:
		mutex_lock(&bma150->value_mutex);
		if(abs(sense_data.x-bma150->value.x)>40000)//·À¶¶¶¯
			sense_data.x=bma150->value.x;
		if(abs(sense_data.y-(bma150->value.y))>40000)//·À¶¶¶¯
			sense_data.y=bma150->value.y;
		if(abs(sense_data.z-(bma150->value.z))>40000)//·À¶¶¶¯
			sense_data.z=bma150->value.z;
	       //bma150->value = acc;
		mutex_unlock(&bma150->value_mutex);

		if ( copy_to_user(argp, &sense_data, sizeof(sense_data) ) ) {
            printk("failed to copy sense data to user space.");
			return -EFAULT;
        }

		DBG("%s:%d,cmd=BMA_IOCTL_GETDATA\n",__FUNCTION__,__LINE__);
			break;
	default:
		printk("%s:%d,error,cmd=%x\n",__FUNCTION__,__LINE__,cmd);
		break;
		}
	return 0;
}

static int bma150_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int err = 0;
	int tempvalue;
	struct bma150_data *data;
	printk(KERN_INFO "bma150_probe  \n");
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		printk(KERN_INFO "i2c_check_functionality error\n");
		goto exit;
	}
	data = kzalloc(sizeof(struct bma150_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}

	tempvalue = 0;
	tempvalue = i2c_smbus_read_word_data(client, BMA150_CHIP_ID_REG);

	if ((tempvalue&0x00FF) == BMA150_CHIP_ID) {
		printk(KERN_INFO "Bosch Sensortec Device detected!\n" \
				"BMA150 registered I2C driver!\n");
	} else{
		printk(KERN_INFO "Bosch Sensortec Device not found" \
				"i2c error %d \n", tempvalue);
		err = -1;
		goto kfree_exit;
	}
	i2c_set_clientdata(client, data);
	data->bma150_client = client;
	mutex_init(&data->value_mutex);
	mutex_init(&data->mode_mutex);
	mutex_init(&data->enable_mutex);
	bma150_set_bandwidth(client, BMA150_BW_SET);
	bma150_set_range(client, BMA150_RANGE_SET);


	INIT_DELAYED_WORK(&data->work, bma150_work_func);
	atomic_set(&data->delay, BMA150_MAX_DELAY);
	atomic_set(&data->enable, 0);
	err = bma150_input_init(data);
	if (err < 0)
		goto kfree_exit;
#if 0
	err = sysfs_create_group(&data->input->dev.kobj,
			&bma150_attribute_group);
	if (err < 0)
		goto error_sysfs;
#endif
	data->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	data->early_suspend.suspend = bma150_early_suspend;
	data->early_suspend.resume = bma150_late_resume;
	register_early_suspend(&data->early_suspend);
	bma023_device.parent = &client->dev;
	misc_register(&bma023_device);
	return 0;

//error_sysfs:
//	bma150_input_delete(data);

kfree_exit:
	kfree(data);
exit:
	return err;
}


static int bma150_remove(struct i2c_client *client)
{
	struct bma150_data *data = i2c_get_clientdata(client);

	bma023_enable(&client->dev, 0);
	unregister_early_suspend(&data->early_suspend);
	//sysfs_remove_group(&data->input->dev.kobj, &bma150_attribute_group);
	bma150_input_delete(data);
	kfree(data);

	return 0;
}




static void bma150_early_suspend(struct early_suspend *h)
{
	struct bma150_data *data =
		container_of(h, struct bma150_data, early_suspend);

	mutex_lock(&data->enable_mutex);
	if (atomic_read(&data->enable)==1) {
		bma150_set_mode(data->bma150_client, BMA150_MODE_SLEEP);
		cancel_delayed_work_sync(&data->work);
	}
	mutex_unlock(&data->enable_mutex);
}


static void bma150_late_resume(struct early_suspend *h)
{
	struct bma150_data *data =
		container_of(h, struct bma150_data, early_suspend);

	mutex_lock(&data->enable_mutex);
	if (atomic_read(&data->enable)==1) {
		bma150_set_mode(data->bma150_client, BMA150_MODE_NORMAL);
		schedule_delayed_work(&data->work,
			msecs_to_jiffies(atomic_read(&data->delay)));
	}
	mutex_unlock(&data->enable_mutex);
}

static const struct i2c_device_id bma150_id[] = {
	{ SENSOR_NAME, 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, bma150_id);

static struct i2c_driver bma150_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= SENSOR_NAME,
	},
	.id_table	= bma150_id,
	.probe		= bma150_probe,
	.remove		= bma150_remove,

};

static int __init BMA150_init(void)
{
	return i2c_add_driver(&bma150_driver);
}

static void __exit BMA150_exit(void)
{
	i2c_del_driver(&bma150_driver);
}

MODULE_AUTHOR("Lan Bin Yuan <lby@rock-chips.com>");
MODULE_DESCRIPTION("BMA150 driver");
MODULE_LICENSE("GPL");

module_init(BMA150_init);
module_exit(BMA150_exit);
