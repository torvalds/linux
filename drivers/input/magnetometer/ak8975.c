/* drivers/i2c/chips/akm8975.c - akm8975 compass driver
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

/*
 * Revised by AKM 2009/04/02
 * 
 */

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <linux/earlysuspend.h>
#include "ak8975.h"

#define AKM8975_DEBUG		1
#define AKM8975_DEBUG_MSG	0
#define AKM8975_DEBUG_FUNC	0
#define AKM8975_DEBUG_DATA	0
#define MAX_FAILURE_COUNT	3
#define AKM8975_RETRY_COUNT	10
#define AKM8975_DEFAULT_DELAY	100

#if AKM8975_DEBUG_MSG
#define AKMDBG(format, ...)	printk(KERN_INFO "AKM8975 " format "\n", ## __VA_ARGS__)
#else
#define AKMDBG(format, ...)
#endif

#if AKM8975_DEBUG_FUNC
#define AKMFUNC(func) printk(KERN_INFO "AKM8975 " func " is called\n")
#else
#define AKMFUNC(func)
#endif

static struct i2c_client *this_client;

struct akm8975_data {
	struct i2c_client *client; 
	struct input_dev *input_dev;
	struct work_struct work;
	struct early_suspend akm_early_suspend;
	int eoc_irq; 
};

/* Addresses to scan -- protected by sense_data_mutex */
static char sense_data[SENSOR_DATA_SIZE];
static struct mutex sense_data_mutex;
static DECLARE_WAIT_QUEUE_HEAD(data_ready_wq);
static DECLARE_WAIT_QUEUE_HEAD(open_wq);

static atomic_t data_ready;
static atomic_t open_count;
static atomic_t open_flag;
static atomic_t reserve_open_flag;

static atomic_t m_flag;
static atomic_t a_flag;
static atomic_t mv_flag;

static int failure_count = 0;

static short akmd_delay = AKM8975_DEFAULT_DELAY;

static atomic_t suspend_flag = ATOMIC_INIT(0);

//static struct akm8975_platform_data *pdata;

#define AKM_I2C_RATE 100*1000

static int AKI2C_RxData(char *rxData, int length)
{
	uint8_t loop_i;
	struct i2c_msg msgs[] = {
		{
			.addr = this_client->addr,
			.flags = 0,
			.len = 1,
			.buf = rxData,
			.scl_rate = AKM_I2C_RATE,
		},
		{
			.addr = this_client->addr,
			.flags = I2C_M_RD,
			.len = length,
			.buf = rxData,
			.scl_rate = AKM_I2C_RATE,
		},
	};
#if AKM8975_DEBUG_DATA
	int i;
	char addr = rxData[0];
#endif
#ifdef AKM8975_DEBUG
	/* Caller should check parameter validity.*/
	if ((rxData == NULL) || (length < 1)) {
		return -EINVAL;
	}
#endif
	for (loop_i = 0; loop_i < AKM8975_RETRY_COUNT; loop_i++) {
		if (i2c_transfer(this_client->adapter, msgs, 2) > 0) {
			break;
		}
		mdelay(10);
	}
	
	if (loop_i >= AKM8975_RETRY_COUNT) {
		printk(KERN_ERR "%s retry over %d\n", __func__, AKM8975_RETRY_COUNT);
		return -EIO;
	}
#if AKM8975_DEBUG_DATA
	printk(KERN_INFO "RxData: len=%02x, addr=%02x\n  data=", length, addr);
	for (i = 0; i < length; i++) {
		printk(KERN_INFO " %02x", rxData[i]);
	}
    printk(KERN_INFO "\n");
#endif
	return 0;
}

static int AKI2C_TxData(char *txData, int length)
{
	uint8_t loop_i;
	struct i2c_msg msg[] = {
		{
			.addr = this_client->addr,
			.flags = 0,
			.len = length,
			.buf = txData,
			.scl_rate = AKM_I2C_RATE,
		},
	};
#if AKM8975_DEBUG_DATA
	int i;
#endif
#ifdef AKM8975_DEBUG
	/* Caller should check parameter validity.*/
	if ((txData == NULL) || (length < 2)) {
		return -EINVAL;
	}
#endif	
	for (loop_i = 0; loop_i < AKM8975_RETRY_COUNT; loop_i++) {
		if (i2c_transfer(this_client->adapter, msg, 1) > 0) {
			break;
		}
		mdelay(10);
	}
	
	if (loop_i >= AKM8975_RETRY_COUNT) {
		printk(KERN_ERR "%s retry over %d\n", __func__, AKM8975_RETRY_COUNT);
		return -EIO;
	}
#if AKM8975_DEBUG_DATA
	printk(KERN_INFO "TxData: len=%02x, addr=%02x\n  data=", length, txData[0]);
	for (i = 0; i < (length-1); i++) {
		printk(KERN_INFO " %02x", txData[i + 1]);
	}
	printk(KERN_INFO "\n");
#endif
	return 0;
}

static int AKECS_SetMode_SngMeasure(void)
{
	char buffer[2];
	
	AKMDBG("enter %s\n", __func__);
	
	atomic_set(&data_ready, 0);
	
	/* Set measure mode */
	buffer[0] = AK8975_REG_CNTL;
	buffer[1] = AK8975_MODE_SNG_MEASURE;
	
	/* Set data */
	return AKI2C_TxData(buffer, 2);
}

static int AKECS_SetMode_SelfTest(void)
{
	char buffer[2];
	AKMDBG("enter %s\n", __func__);
	
	/* Set measure mode */
	buffer[0] = AK8975_REG_CNTL;
	buffer[1] = AK8975_MODE_SELF_TEST;
	/* Set data */
	return AKI2C_TxData(buffer, 2);
}

static int AKECS_SetMode_FUSEAccess(void)
{
	char buffer[2];
	AKMDBG("enter %s\n", __func__);
	
	/* Set measure mode */
	buffer[0] = AK8975_REG_CNTL;
	buffer[1] = AK8975_MODE_FUSE_ACCESS;
	/* Set data */
	return AKI2C_TxData(buffer, 2);
}

static int AKECS_SetMode_PowerDown(void)
{
	char buffer[2];
	AKMDBG("enter %s\n", __func__);
	
	/* Set powerdown mode */
	buffer[0] = AK8975_REG_CNTL;
	buffer[1] = AK8975_MODE_POWERDOWN;
	/* Set data */
	return AKI2C_TxData(buffer, 2);
}

static int AKECS_SetMode(char mode)
{
	int ret;
	AKMDBG("enter %s\n", __func__);
	
	switch (mode) {
		case AK8975_MODE_SNG_MEASURE:
			ret = AKECS_SetMode_SngMeasure();
			break;
		case AK8975_MODE_SELF_TEST:
			ret = AKECS_SetMode_SelfTest();
			break;
		case AK8975_MODE_FUSE_ACCESS:
			ret = AKECS_SetMode_FUSEAccess();
			break;
		case AK8975_MODE_POWERDOWN:
			ret = AKECS_SetMode_PowerDown();
			/* wait at least 100us after changing mode */
			udelay(100);
			break;
		default:
			AKMDBG("%s: Unknown mode(%d)", __func__, mode);
			return -EINVAL;
	}

	return ret;
}

static int AKECS_CheckDevice(void)
{
	char buffer[2];
	int ret;
	AKMDBG("enter %s\n", __func__);
	
	/* Set measure mode */
	buffer[0] = AK8975_REG_WIA;
	
	/* Read data */
	ret = AKI2C_RxData(buffer, 1);
	if (ret < 0) {
		return ret;
	}
	/* Check read data */
	if (buffer[0] != 0x48) {
		return -ENXIO;
	}
	
	return 0;
}

static int AKECS_GetData(char *rbuf, int size)
{
#ifdef AKM8975_DEBUG
	/* This function is not exposed, so parameters 
	 should be checked internally.*/
	if ((rbuf == NULL) || (size < SENSOR_DATA_SIZE)) {
		return -EINVAL;
	}
#endif
	AKMDBG("enter %s\n", __func__);
	wait_event_interruptible_timeout(data_ready_wq,
									 atomic_read(&data_ready), 1000);
	if (!atomic_read(&data_ready)) {
		AKMDBG("%s: data_ready is not set.", __func__);
		if (!atomic_read(&suspend_flag)) {
			AKMDBG("%s: suspend_flag is not set.", __func__);
			failure_count++;
			if (failure_count >= MAX_FAILURE_COUNT) {
				printk(KERN_ERR
				       "AKM8975 AKECS_GetData: successive %d failure.\n",
				       failure_count);
				atomic_set(&open_flag, -1);
				wake_up(&open_wq);
				failure_count = 0;
			}
		}
		return -1;
	}
	
	mutex_lock(&sense_data_mutex);
	memcpy(rbuf, sense_data, size);
	atomic_set(&data_ready, 0);
	mutex_unlock(&sense_data_mutex);
	
	failure_count = 0;
	return 0;
}

static void AKECS_SetYPR(short *rbuf)
{
	struct akm8975_data *data = i2c_get_clientdata(this_client);
#if AKM8975_DEBUG_DATA
	printk(KERN_INFO "AKM8975 %s:\n", __func__);
	printk(KERN_INFO "  yaw =%6d, pitch =%6d, roll =%6d\n",
		   rbuf[0], rbuf[1], rbuf[2]);
	printk(KERN_INFO "  tmp =%6d, m_stat =%6d, g_stat =%6d\n",
		   rbuf[3], rbuf[4], rbuf[5]);
	printk(KERN_INFO "  Acceleration[LSB]: %6d,%6d,%6d\n",
	       rbuf[6], rbuf[7], rbuf[8]);
	printk(KERN_INFO "  Geomagnetism[LSB]: %6d,%6d,%6d\n",
	       rbuf[9], rbuf[10], rbuf[11]);
#endif
	AKMDBG("enter %s\n", __func__);
	/* Report magnetic sensor information */
	if (atomic_read(&m_flag)) {
		input_report_abs(data->input_dev, ABS_RX, rbuf[0]);
		input_report_abs(data->input_dev, ABS_RY, rbuf[1]);
		input_report_abs(data->input_dev, ABS_RZ, rbuf[2]);
		input_report_abs(data->input_dev, ABS_RUDDER, rbuf[4]);
	}
	
	/* Report acceleration sensor information */
	if (atomic_read(&a_flag)) {
		input_report_abs(data->input_dev, ABS_X, rbuf[6]);
		input_report_abs(data->input_dev, ABS_Y, rbuf[7]);
		input_report_abs(data->input_dev, ABS_Z, rbuf[8]);
		input_report_abs(data->input_dev, ABS_WHEEL, rbuf[5]);
	}
	
	/* Report magnetic vector information */
	if (atomic_read(&mv_flag)) {
		input_report_abs(data->input_dev, ABS_HAT0X, rbuf[9]);
		input_report_abs(data->input_dev, ABS_HAT0Y, rbuf[10]);
		input_report_abs(data->input_dev, ABS_BRAKE, rbuf[11]);
	}
	
	input_sync(data->input_dev);
}

static int AKECS_GetOpenStatus(void)
{
	AKMDBG("enter %s\n", __func__);
	wait_event_interruptible(open_wq, (atomic_read(&open_flag) != 0));
	return atomic_read(&open_flag);
}

static int AKECS_GetCloseStatus(void)
{
	AKMDBG("enter %s\n", __func__);
	wait_event_interruptible(open_wq, (atomic_read(&open_flag) <= 0));
	return atomic_read(&open_flag);
}

static void AKECS_CloseDone(void)
{
	AKMDBG("enter %s\n", __func__);
	atomic_set(&m_flag, 1);
	atomic_set(&a_flag, 1);
	atomic_set(&mv_flag, 1);
}

/***** akm_aot functions ***************************************/
static int akm_aot_open(struct inode *inode, struct file *file)
{
	int ret = -1;

	AKMFUNC("akm_aot_open");
	if (atomic_cmpxchg(&open_count, 0, 1) == 0) {
		if (atomic_cmpxchg(&open_flag, 0, 1) == 0) {
			atomic_set(&reserve_open_flag, 1);
			wake_up(&open_wq);
			ret = 0;
		}
	}
	return ret;
}

static int akm_aot_release(struct inode *inode, struct file *file)
{
	AKMFUNC("akm_aot_release");
	atomic_set(&reserve_open_flag, 0);
	atomic_set(&open_flag, 0);
	atomic_set(&open_count, 0);
	wake_up(&open_wq);
	return 0;
}

//static int
//akm_aot_ioctl(struct inode *inode, struct file *file,
//			  unsigned int cmd, unsigned long arg)
static long akm_aot_ioctl(struct file *file,unsigned int cmd, unsigned long arg)			  
{
	void __user *argp = (void __user *)arg;
	short flag;
	AKMDBG("enter %s\n", __func__);
	
	switch (cmd) {
		case ECS_IOCTL_APP_SET_MFLAG:
		case ECS_IOCTL_APP_SET_AFLAG:
		case ECS_IOCTL_APP_SET_MVFLAG:
			if (copy_from_user(&flag, argp, sizeof(flag))) {
				return -EFAULT;
			}
			if (flag < 0 || flag > 1) {
				return -EINVAL;
			}
			break;
		case ECS_IOCTL_APP_SET_DELAY:
			if (copy_from_user(&flag, argp, sizeof(flag))) {
				return -EFAULT;
			}
			break;
		default:
			break;
	}
	
	switch (cmd) {
		case ECS_IOCTL_APP_SET_MFLAG:
			atomic_set(&m_flag, flag);
			AKMDBG("MFLAG is set to %d", flag);
			break;
		case ECS_IOCTL_APP_GET_MFLAG:
			flag = atomic_read(&m_flag);
			break;
		case ECS_IOCTL_APP_SET_AFLAG:
			atomic_set(&a_flag, flag);
			AKMDBG("AFLAG is set to %d", flag);
			break;
		case ECS_IOCTL_APP_GET_AFLAG:
			flag = atomic_read(&a_flag);
			break;
		case ECS_IOCTL_APP_SET_MVFLAG:
			atomic_set(&mv_flag, flag);
			AKMDBG("MVFLAG is set to %d", flag);
			break;
		case ECS_IOCTL_APP_GET_MVFLAG:
			flag = atomic_read(&mv_flag);
			break;
		case ECS_IOCTL_APP_SET_DELAY:
			akmd_delay = flag;
			AKMDBG("Delay is set to %d", flag);
			break;
		case ECS_IOCTL_APP_GET_DELAY:
			flag = akmd_delay;
			break;
		default:
			return -ENOTTY;
	}
	
	switch (cmd) {
		case ECS_IOCTL_APP_GET_MFLAG:
		case ECS_IOCTL_APP_GET_AFLAG:
		case ECS_IOCTL_APP_GET_MVFLAG:
		case ECS_IOCTL_APP_GET_DELAY:
			if (copy_to_user(argp, &flag, sizeof(flag))) {
				return -EFAULT;
			}
			break;
		default:
			break;
	}
	
	return 0;
}

/***** akmd functions ********************************************/
static int akmd_open(struct inode *inode, struct file *file)
{
	AKMFUNC("akmd_open");
	return nonseekable_open(inode, file);
}

static int akmd_release(struct inode *inode, struct file *file)
{
	AKMFUNC("akmd_release");
	AKECS_CloseDone();
	return 0;
}

//static int akmd_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
//		   unsigned long arg)

static long akmd_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	AKMDBG("enter %s\n", __func__);
	
	/* NOTE: In this function the size of "char" should be 1-byte. */
	char sData[SENSOR_DATA_SIZE];/* for GETDATA */
	char rwbuf[RWBUF_SIZE];		/* for READ/WRITE */
	char mode;					/* for SET_MODE*/
	short value[12];			/* for SET_YPR */
	short delay;				/* for GET_DELAY */
	int status;					/* for OPEN/CLOSE_STATUS */
	int ret = -1;				/* Return value. */
	/*AKMDBG("%s (0x%08X).", __func__, cmd);*/
	
	switch (cmd) {
		case ECS_IOCTL_WRITE:
		case ECS_IOCTL_READ:
			if (argp == NULL) {
				AKMDBG("invalid argument.");
				return -EINVAL;
			}
			if (copy_from_user(&rwbuf, argp, sizeof(rwbuf))) {
				AKMDBG("copy_from_user failed.");
				return -EFAULT;
			}
			break;
		case ECS_IOCTL_SET_MODE:
			if (argp == NULL) {
				AKMDBG("invalid argument.");
				return -EINVAL;
			}
			if (copy_from_user(&mode, argp, sizeof(mode))) {
				AKMDBG("copy_from_user failed.");
				return -EFAULT;
			}
			break;
		case ECS_IOCTL_SET_YPR:
			if (argp == NULL) {
				AKMDBG("invalid argument.");
				return -EINVAL;
			}
			if (copy_from_user(&value, argp, sizeof(value))) {
				AKMDBG("copy_from_user failed.");
				return -EFAULT;
			}
			break;
		default:
			break;
	}
	
	switch (cmd) {
		case ECS_IOCTL_WRITE:
			AKMFUNC("IOCTL_WRITE");
			if ((rwbuf[0] < 2) || (rwbuf[0] > (RWBUF_SIZE-1))) {
				AKMDBG("invalid argument.");
				return -EINVAL;
			}
			ret = AKI2C_TxData(&rwbuf[1], rwbuf[0]);
			if (ret < 0) {
				return ret;
			}
			break;
		case ECS_IOCTL_READ:
			AKMFUNC("IOCTL_READ");
			if ((rwbuf[0] < 1) || (rwbuf[0] > (RWBUF_SIZE-1))) {
				AKMDBG("invalid argument.");
				return -EINVAL;
			}
			ret = AKI2C_RxData(&rwbuf[1], rwbuf[0]);
			if (ret < 0) {
				return ret;
			}
			break;
		case ECS_IOCTL_SET_MODE:
			AKMFUNC("IOCTL_SET_MODE");
			ret = AKECS_SetMode(mode);
			if (ret < 0) {
				return ret;
			}
			break;
		case ECS_IOCTL_GETDATA:
			AKMFUNC("IOCTL_GET_DATA");
			ret = AKECS_GetData(sData, SENSOR_DATA_SIZE);
			if (ret < 0) {
				return ret;
			}
			break;
		case ECS_IOCTL_SET_YPR:
			AKECS_SetYPR(value);
			break;
		case ECS_IOCTL_GET_OPEN_STATUS:
			AKMFUNC("IOCTL_GET_OPEN_STATUS");
			status = AKECS_GetOpenStatus();
			AKMDBG("AKECS_GetOpenStatus returned (%d)", status);
			break;
		case ECS_IOCTL_GET_CLOSE_STATUS:
			AKMFUNC("IOCTL_GET_CLOSE_STATUS");
			status = AKECS_GetCloseStatus();
			AKMDBG("AKECS_GetCloseStatus returned (%d)", status);
			break;
		case ECS_IOCTL_GET_DELAY:
			AKMFUNC("IOCTL_GET_DELAY");
			delay = akmd_delay;
			break;
		default:
			return -ENOTTY;
	}
	
	switch (cmd) {
		case ECS_IOCTL_READ:
			if (copy_to_user(argp, &rwbuf, rwbuf[0]+1)) {
				AKMDBG("copy_to_user failed.");
				return -EFAULT;
			}
			break;
		case ECS_IOCTL_GETDATA:
			if (copy_to_user(argp, &sData, sizeof(sData))) {
				AKMDBG("copy_to_user failed.");
				return -EFAULT;
			}
			break;
		case ECS_IOCTL_GET_OPEN_STATUS:
		case ECS_IOCTL_GET_CLOSE_STATUS:
			if (copy_to_user(argp, &status, sizeof(status))) {
				AKMDBG("copy_to_user failed.");
				return -EFAULT;
			}
			break;
		case ECS_IOCTL_GET_DELAY:
			if (copy_to_user(argp, &delay, sizeof(delay))) {
				AKMDBG("copy_to_user failed.");
				return -EFAULT;
			}
			break;
		default:
			break;
	}
	
	return 0;
}

static void akm8975_work_func(struct work_struct *work)
{
	char buffer[SENSOR_DATA_SIZE];
	int ret;
	AKMDBG("enter %s\n", __func__);
	
	memset(buffer, 0, SENSOR_DATA_SIZE);
	buffer[0] = AK8975_REG_ST1;
	ret = AKI2C_RxData(buffer, SENSOR_DATA_SIZE);
	if (ret < 0) {
		printk(KERN_ERR "AKM8975 akm8975_work_func: I2C failed\n");
		return;
	}
	/* Check ST bit */
	if ((buffer[0] & 0x01) != 0x01) {
		printk(KERN_ERR "AKM8975 akm8975_work_func: ST is not set\n");
		return;
	}
	
	mutex_lock(&sense_data_mutex);
	memcpy(sense_data, buffer, SENSOR_DATA_SIZE);
	atomic_set(&data_ready, 1);
	wake_up(&data_ready_wq);
	mutex_unlock(&sense_data_mutex);
	
	enable_irq(this_client->irq);
	
	AKMFUNC("akm8975_work_func");
}

static irqreturn_t akm8975_interrupt(int irq, void *dev_id)
{
	struct akm8975_data *data = dev_id;
	AKMFUNC("akm8975_interrupt");
	disable_irq_nosync(this_client->irq);
	schedule_work(&data->work);
	AKMDBG("exit %s\n", __func__);
	return IRQ_HANDLED;
}

static void akm8975_early_suspend(struct early_suspend *handler)
{
	AKMFUNC("akm8975_early_suspend");
	atomic_set(&suspend_flag, 1);
	atomic_set(&reserve_open_flag, atomic_read(&open_flag));
	atomic_set(&open_flag, 0);
	wake_up(&open_wq);
	disable_irq(this_client->irq);
	AKMDBG("suspended with flag=%d", 
	       atomic_read(&reserve_open_flag));
}

static void akm8975_early_resume(struct early_suspend *handler)
{
	AKMFUNC("akm8975_early_resume");
	enable_irq(this_client->irq);
	atomic_set(&suspend_flag, 0);
	atomic_set(&open_flag, atomic_read(&reserve_open_flag));
	wake_up(&open_wq);
	AKMDBG("resumed with flag=%d", 
	       atomic_read(&reserve_open_flag));
}
static int akm8975_suspend(struct i2c_client *client, pm_message_t mesg)
{
	AKMFUNC("akm8975_early_suspend");
	atomic_set(&suspend_flag, 1);
	atomic_set(&reserve_open_flag, atomic_read(&open_flag));
	atomic_set(&open_flag, 0);
	wake_up(&open_wq);
	disable_irq(this_client->irq);
	AKMDBG("suspended with flag=%d", 
	       atomic_read(&reserve_open_flag));
	return 0;
}

static int akm8975_resume(struct i2c_client *client)
{
	AKMFUNC("akm8975_early_resume");
	enable_irq(this_client->irq);
	atomic_set(&suspend_flag, 0);
	atomic_set(&open_flag, atomic_read(&reserve_open_flag));
	wake_up(&open_wq);
	AKMDBG("resumed with flag=%d", 
	       atomic_read(&reserve_open_flag));
	return 0;
}
/*********************************************/
static struct file_operations akmd_fops = {
	.owner = THIS_MODULE,
	.open = akmd_open,
	.release = akmd_release,
	.unlocked_ioctl = akmd_ioctl,
};

static struct file_operations akm_aot_fops = {
	.owner = THIS_MODULE,
	.open = akm_aot_open,
	.release = akm_aot_release,
	.unlocked_ioctl = akm_aot_ioctl,
};

static struct miscdevice akmd_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "akm8975_dev",
	.fops = &akmd_fops,
};

static struct miscdevice akm_aot_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "akm8975_aot",
	.fops = &akm_aot_fops,
};

/*********************************************/
int akm8975_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct akm8975_data *akm;
	int err = 0;
	
	AKMFUNC("akm8975_probe");

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		printk(KERN_ERR "AKM8975 akm8975_probe: check_functionality failed.\n");
		err = -ENODEV;
		goto exit0;
	}
	
	/* Allocate memory for driver data */
	akm = kzalloc(sizeof(struct akm8975_data), GFP_KERNEL);
	if (!akm) {
		printk(KERN_ERR "AKM8975 akm8975_probe: memory allocation failed.\n");
		err = -ENOMEM;
		goto exit1;
	}
	
	INIT_WORK(&akm->work, akm8975_work_func);
	i2c_set_clientdata(client, akm);
	
	this_client = client;
	
	/* Check connection */
	err = AKECS_CheckDevice();
	if (err < 0) {
		printk(KERN_ERR "AKM8975 akm8975_probe: set power down mode error\n");
		goto exit3;
	}
	akm->eoc_irq = client->irq; 
	if (!akm->eoc_irq) {
		dev_dbg(&akm->client->dev, "no IRQ?\n");
		return -ENODEV;
	}else{
		AKMDBG("gpio %d to irq %d\n", akm->eoc_irq, gpio_to_irq(akm->eoc_irq));
		akm->eoc_irq = gpio_to_irq(akm->eoc_irq);
	} 
	err = gpio_request(client->irq, "ak_8975"); 
	if (err < 0) { 
		dev_err(&client->dev, "failed to request GPIO, error %d\n", err); 
		goto exit3; 
	}

	err = gpio_direction_input(client->irq);
	if (err) {
		dev_err(&client->dev, "failed to set GPIO direction, error %d\n", err); 
		goto exit3; 
	}
	gpio_pull_updown(client->irq, GPIOPullDown);
	
	/* IRQ */
	err = request_irq(akm->eoc_irq, akm8975_interrupt, IRQ_TYPE_EDGE_RISING,
					  "akm8975_DRDY", akm);
	if (err < 0) {
		printk(KERN_ERR "AKM8975 akm8975_probe: request irq failed\n");
		goto exit4;
	}
	
	client->irq = akm->eoc_irq;
	
	/* Declare input device */
	akm->input_dev = input_allocate_device();
	if (!akm->input_dev) {
		err = -ENOMEM;
		printk(KERN_ERR
		       "AKM8975 akm8975_probe: Failed to allocate input device\n");
		goto exit5;
	}
	/* Setup input device */
	set_bit(EV_ABS, akm->input_dev->evbit);
	/* yaw (0, 360) */
	input_set_abs_params(akm->input_dev, ABS_RX, 0, 23040, 0, 0);
	/* pitch (-180, 180) */
	input_set_abs_params(akm->input_dev, ABS_RY, -11520, 11520, 0, 0);
	/* roll (-90, 90) */
	input_set_abs_params(akm->input_dev, ABS_RZ, -5760, 5760, 0, 0);
	/* x-axis acceleration (720 x 8G) */
	input_set_abs_params(akm->input_dev, ABS_X, -5760, 5760, 0, 0);
	/* y-axis acceleration (720 x 8G) */
	input_set_abs_params(akm->input_dev, ABS_Y, -5760, 5760, 0, 0);
	/* z-axis acceleration (720 x 8G) */
	input_set_abs_params(akm->input_dev, ABS_Z, -5760, 5760, 0, 0);
	/* temparature */
	/*
	input_set_abs_params(akm->input_dev, ABS_THROTTLE, -30, 85, 0, 0);
	 */
	/* status of magnetic sensor */
	input_set_abs_params(akm->input_dev, ABS_RUDDER, -32768, 3, 0, 0);
	/* status of acceleration sensor */
	input_set_abs_params(akm->input_dev, ABS_WHEEL, -32768, 3, 0, 0);
	/* x-axis of raw magnetic vector (-4096, 4095) */
	input_set_abs_params(akm->input_dev, ABS_HAT0X, -20480, 20479, 0, 0);
	/* y-axis of raw magnetic vector (-4096, 4095) */
	input_set_abs_params(akm->input_dev, ABS_HAT0Y, -20480, 20479, 0, 0);
	/* z-axis of raw magnetic vector (-4096, 4095) */
	input_set_abs_params(akm->input_dev, ABS_BRAKE, -20480, 20479, 0, 0);
	/* Set name */
	akm->input_dev->name = "compass";
	
	/* Register */
	err = input_register_device(akm->input_dev);
	if (err) {
		printk(KERN_ERR
		       "AKM8975 akm8975_probe: Unable to register input device\n");
		goto exit6;
	}
	
	err = misc_register(&akmd_device);
	if (err) {
		printk(KERN_ERR
			   "AKM8975 akm8975_probe: akmd_device register failed\n");
		goto exit7;
	}
	
	err = misc_register(&akm_aot_device);
	if (err) {
		printk(KERN_ERR
		       "AKM8975 akm8975_probe: akm_aot_device register failed\n");
		goto exit8;
	}
	
	mutex_init(&sense_data_mutex);
	
	init_waitqueue_head(&data_ready_wq);
	init_waitqueue_head(&open_wq);
	
	/* As default, report all information */
	atomic_set(&m_flag, 1);
	atomic_set(&a_flag, 1);
	atomic_set(&mv_flag, 1);
	
	//akm->akm_early_suspend.suspend = akm8975_early_suspend;
	//akm->akm_early_suspend.resume = akm8975_early_resume;
	//register_early_suspend(&akm->akm_early_suspend);
	
	AKMDBG("successfully probed.");
	return 0;
	
exit8:
	misc_deregister(&akmd_device);
exit7:
	input_unregister_device(akm->input_dev);
exit6:
	input_free_device(akm->input_dev);
exit5:
	free_irq(client->irq, akm);
exit4:
exit3:
	kfree(akm);
exit1:
exit0:
	return err;
	
}

static int akm8975_remove(struct i2c_client *client)
{
	struct akm8975_data *akm = i2c_get_clientdata(client);
	AKMFUNC("akm8975_remove");
	unregister_early_suspend(&akm->akm_early_suspend);
	misc_deregister(&akm_aot_device);
	misc_deregister(&akmd_device);
	input_unregister_device(akm->input_dev);
	free_irq(client->irq, akm);
	kfree(akm);
	AKMDBG("successfully removed.");
	return 0;
}

static const struct i2c_device_id akm8975_id[] = {
	{AKM8975_I2C_NAME, 0 },
	{ }
};

static struct i2c_driver akm8975_driver = {
	.probe		= akm8975_probe,
	.remove 	= akm8975_remove,
	.suspend = akm8975_suspend,
	.resume = akm8975_resume,
	.id_table	= akm8975_id,
	.driver = {
		.name = AKM8975_I2C_NAME,
	},
};

static int __init akm8975_init(void)
{
	printk(KERN_INFO "AKM8975 compass driver: initialize\n");
	return i2c_add_driver(&akm8975_driver);
}

static void __exit akm8975_exit(void)
{
	printk(KERN_INFO "AKM8975 compass driver: release\n");
	i2c_del_driver(&akm8975_driver);
}

module_init(akm8975_init);
module_exit(akm8975_exit);

MODULE_AUTHOR("viral wang <viral_wang@htc.com>");
MODULE_DESCRIPTION("AKM8975 compass driver");
MODULE_LICENSE("GPL");

