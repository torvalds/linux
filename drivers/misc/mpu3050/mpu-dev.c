/*
 $License:
    Copyright (C) 2010 InvenSense Corporation, All Rights Reserved.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
  $
 */
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/stat.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/signal.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/pm.h>
#include <linux/mutex.h>
#include <linux/suspend.h>
#include <linux/poll.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/uaccess.h>
#include <linux/io.h>

#include "mpuirq.h"
#include "slaveirq.h"
#include "mlsl.h"
#include "mpu-i2c.h"
#include "mldl_cfg.h"
#include "mpu.h"

#define MPU3050_EARLY_SUSPEND_IN_DRIVER 0

/* Platform data for the MPU */
struct mpu_private_data {
	struct mldl_cfg mldl_cfg;

#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
	struct mutex mutex;
	wait_queue_head_t mpu_event_wait;
	struct completion completion;
	struct timer_list timeout;
	struct notifier_block nb;
	struct mpuirq_data mpu_pm_event;
	int response_timeout;	/* In seconds */
	unsigned long event;
	int pid;
};

static struct i2c_client *this_client;


static void
mpu_pm_timeout(u_long data)
{
	struct mpu_private_data *mpu = (struct mpu_private_data *) data;
	dev_dbg(&this_client->adapter->dev, "%s\n", __func__);
	complete(&mpu->completion);
}

static int mpu_pm_notifier_callback(struct notifier_block *nb,
				    unsigned long event,
				    void *unused)
{
	struct mpu_private_data *mpu =
		container_of(nb, struct mpu_private_data, nb);
	struct timeval event_time;
	dev_dbg(&this_client->adapter->dev, "%s: %ld\n", __func__, event);

	/* Prevent the file handle from being closed before we initialize
	   the completion event */
	mutex_lock(&mpu->mutex);
	if (!(mpu->pid) ||
		(event != PM_SUSPEND_PREPARE && event != PM_POST_SUSPEND)) {
		mutex_unlock(&mpu->mutex);
		return NOTIFY_OK;
	}

	do_gettimeofday(&event_time);
	mpu->mpu_pm_event.interruptcount++;
	mpu->mpu_pm_event.irqtime =
		(((long long) event_time.tv_sec) << 32) +
		event_time.tv_usec;
	mpu->mpu_pm_event.data_type = MPUIRQ_DATA_TYPE_PM_EVENT;
	mpu->mpu_pm_event.data_size = sizeof(unsigned long);
	mpu->mpu_pm_event.data = &mpu->event;

	if (event == PM_SUSPEND_PREPARE)
		mpu->event = MPU_PM_EVENT_SUSPEND_PREPARE;
	if (event == PM_POST_SUSPEND)
		mpu->event = MPU_PM_EVENT_POST_SUSPEND;

	if (mpu->response_timeout > 0) {
		mpu->timeout.expires = jiffies + mpu->response_timeout * HZ;
		add_timer(&mpu->timeout);
	}
	INIT_COMPLETION(mpu->completion);
	mutex_unlock(&mpu->mutex);

	wake_up_interruptible(&mpu->mpu_event_wait);
	wait_for_completion(&mpu->completion);
	del_timer_sync(&mpu->timeout);
	dev_dbg(&this_client->adapter->dev, "%s: %ld DONE\n", __func__, event);
	return NOTIFY_OK;
}

static int mpu_open(struct inode *inode, struct file *file)
{
	struct mpu_private_data *mpu =
	    (struct mpu_private_data *) i2c_get_clientdata(this_client);
	struct mldl_cfg *mldl_cfg = &mpu->mldl_cfg;
	int result;
	dev_dbg(&this_client->adapter->dev, "mpu_open\n");
	dev_dbg(&this_client->adapter->dev, "current->pid %d\n",
		current->pid);
	mpu->pid = current->pid;
	file->private_data = this_client;
	/* we could do some checking on the flags supplied by "open" */
	/* i.e. O_NONBLOCK */
	/* -> set some flag to disable interruptible_sleep_on in mpu_read */

	/* Reset the sensors to the default */
	result = mutex_lock_interruptible(&mpu->mutex);
	if (result) {
		dev_err(&this_client->adapter->dev,
			"%s: mutex_lock_interruptible returned %d\n",
			__func__, result);
		return result;
	}
	mldl_cfg->requested_sensors = ML_THREE_AXIS_GYRO;
	if (mldl_cfg->accel && mldl_cfg->accel->resume)
		mldl_cfg->requested_sensors |= ML_THREE_AXIS_ACCEL;

	if (mldl_cfg->compass && mldl_cfg->compass->resume)
		mldl_cfg->requested_sensors |= ML_THREE_AXIS_COMPASS;

	if (mldl_cfg->pressure && mldl_cfg->pressure->resume)
		mldl_cfg->requested_sensors |= ML_THREE_AXIS_PRESSURE;
	mutex_unlock(&mpu->mutex);
	return 0;
}

/* close function - called when the "file" /dev/mpu is closed in userspace   */
static int mpu_release(struct inode *inode, struct file *file)
{
	struct i2c_client *client =
	    (struct i2c_client *) file->private_data;
	struct mpu_private_data *mpu =
	    (struct mpu_private_data *) i2c_get_clientdata(client);
	struct mldl_cfg *mldl_cfg = &mpu->mldl_cfg;
	struct i2c_adapter *accel_adapter;
	struct i2c_adapter *compass_adapter;
	struct i2c_adapter *pressure_adapter;
	int result = 0;

	accel_adapter = i2c_get_adapter(mldl_cfg->pdata->accel.adapt_num);
	compass_adapter = i2c_get_adapter(mldl_cfg->pdata->compass.adapt_num);
	pressure_adapter = i2c_get_adapter(mldl_cfg->pdata->pressure.adapt_num);

	mutex_lock(&mpu->mutex);
	result = mpu3050_suspend(mldl_cfg, client->adapter,
				 accel_adapter, compass_adapter,
				 pressure_adapter,
				 TRUE, TRUE, TRUE, TRUE);
	mpu->pid = 0;
	mutex_unlock(&mpu->mutex);
	complete(&mpu->completion);
	dev_dbg(&this_client->adapter->dev, "mpu_release\n");
	return result;
}

/* read function called when from /dev/mpu is read.  Read from the FIFO */
static ssize_t mpu_read(struct file *file,
			char __user *buf, size_t count, loff_t *offset)
{
	struct mpuirq_data local_mpu_pm_event;
	struct i2c_client *client =
	    (struct i2c_client *) file->private_data;
	struct mpu_private_data *mpu =
	    (struct mpu_private_data *) i2c_get_clientdata(client);
	size_t len = sizeof(mpu->mpu_pm_event) + sizeof(unsigned long);
	int err;

	if (!mpu->event && (!(file->f_flags & O_NONBLOCK)))
		wait_event_interruptible(mpu->mpu_event_wait, mpu->event);

	if (!mpu->event || NULL == buf
		|| count < sizeof(mpu->mpu_pm_event) + sizeof(unsigned long))
		return 0;

	err = copy_from_user(&local_mpu_pm_event, buf,
			sizeof(mpu->mpu_pm_event));
	if (err != 0) {
		dev_err(&this_client->adapter->dev,
			"Copy from user returned %d\n", err);
		return -EFAULT;
	}

	mpu->mpu_pm_event.data = local_mpu_pm_event.data;
	err = copy_to_user((unsigned long __user *)local_mpu_pm_event.data,
			&mpu->event,
			sizeof(mpu->event));
	if (err != 0) {
		dev_err(&this_client->adapter->dev,
			"Copy to user returned %d\n", err);
		return -EFAULT;
	}
	err = copy_to_user(buf, &mpu->mpu_pm_event, sizeof(mpu->mpu_pm_event));
	if (err != 0) {
		dev_err(&this_client->adapter->dev,
			"Copy to user returned %d\n", err);
		return -EFAULT;
	}
	mpu->event = 0;
	return len;
}

static unsigned int mpu_poll(struct file *file, struct poll_table_struct *poll)
{
	struct i2c_client *client =
	    (struct i2c_client *) file->private_data;
	struct mpu_private_data *mpu =
	    (struct mpu_private_data *) i2c_get_clientdata(client);
	int mask = 0;

	poll_wait(file, &mpu->mpu_event_wait, poll);
	if (mpu->event)
		mask |= POLLIN | POLLRDNORM;
	return mask;
}

static int
mpu_ioctl_set_mpu_pdata(struct i2c_client *client, unsigned long arg)
{
	int ii;
	struct mpu_private_data *mpu =
	    (struct mpu_private_data *) i2c_get_clientdata(client);
	struct mpu3050_platform_data *pdata = mpu->mldl_cfg.pdata;
	struct mpu3050_platform_data local_pdata;

	if (copy_from_user(&local_pdata, (unsigned char __user *) arg,
				sizeof(local_pdata)))
		return -EFAULT;

	pdata->int_config = local_pdata.int_config;
	for (ii = 0; ii < DIM(pdata->orientation); ii++)
		pdata->orientation[ii] = local_pdata.orientation[ii];
	pdata->level_shifter = local_pdata.level_shifter;

	pdata->accel.address = local_pdata.accel.address;
	for (ii = 0; ii < DIM(pdata->accel.orientation); ii++)
		pdata->accel.orientation[ii] =
			local_pdata.accel.orientation[ii];

	pdata->compass.address = local_pdata.compass.address;
	for (ii = 0; ii < DIM(pdata->compass.orientation); ii++)
		pdata->compass.orientation[ii] =
			local_pdata.compass.orientation[ii];

	pdata->pressure.address = local_pdata.pressure.address;
	for (ii = 0; ii < DIM(pdata->pressure.orientation); ii++)
		pdata->pressure.orientation[ii] =
			local_pdata.pressure.orientation[ii];

	dev_dbg(&client->adapter->dev, "%s\n", __func__);

	return ML_SUCCESS;
}

static int
mpu_ioctl_set_mpu_config(struct i2c_client *client, unsigned long arg)
{
	int ii;
	int result = ML_SUCCESS;
	struct mpu_private_data *mpu =
		(struct mpu_private_data *) i2c_get_clientdata(client);
	struct mldl_cfg *mldl_cfg = &mpu->mldl_cfg;
	struct mldl_cfg *temp_mldl_cfg;

	dev_dbg(&this_client->adapter->dev, "%s\n", __func__);

	temp_mldl_cfg = kzalloc(sizeof(struct mldl_cfg), GFP_KERNEL);
	if (NULL == temp_mldl_cfg)
		return -ENOMEM;

	/*
	 * User space is not allowed to modify accel compass pressure or
	 * pdata structs, as well as silicon_revision product_id or trim
	 */
	if (copy_from_user(temp_mldl_cfg, (struct mldl_cfg __user *) arg,
				offsetof(struct mldl_cfg, silicon_revision))) {
		result = -EFAULT;
		goto out;
	}

	if (mldl_cfg->gyro_is_suspended) {
		if (mldl_cfg->addr != temp_mldl_cfg->addr)
			mldl_cfg->gyro_needs_reset = TRUE;

		if (mldl_cfg->int_config != temp_mldl_cfg->int_config)
			mldl_cfg->gyro_needs_reset = TRUE;

		if (mldl_cfg->ext_sync != temp_mldl_cfg->ext_sync)
			mldl_cfg->gyro_needs_reset = TRUE;

		if (mldl_cfg->full_scale != temp_mldl_cfg->full_scale)
			mldl_cfg->gyro_needs_reset = TRUE;

		if (mldl_cfg->lpf != temp_mldl_cfg->lpf)
			mldl_cfg->gyro_needs_reset = TRUE;

		if (mldl_cfg->clk_src != temp_mldl_cfg->clk_src)
			mldl_cfg->gyro_needs_reset = TRUE;

		if (mldl_cfg->divider != temp_mldl_cfg->divider)
			mldl_cfg->gyro_needs_reset = TRUE;

		if (mldl_cfg->dmp_enable != temp_mldl_cfg->dmp_enable)
			mldl_cfg->gyro_needs_reset = TRUE;

		if (mldl_cfg->fifo_enable != temp_mldl_cfg->fifo_enable)
			mldl_cfg->gyro_needs_reset = TRUE;

		if (mldl_cfg->dmp_cfg1 != temp_mldl_cfg->dmp_cfg1)
			mldl_cfg->gyro_needs_reset = TRUE;

		if (mldl_cfg->dmp_cfg2 != temp_mldl_cfg->dmp_cfg2)
			mldl_cfg->gyro_needs_reset = TRUE;

		if (mldl_cfg->gyro_power != temp_mldl_cfg->gyro_power)
			mldl_cfg->gyro_needs_reset = TRUE;

		for (ii = 0; ii < MPU_NUM_AXES; ii++)
			if (mldl_cfg->offset_tc[ii] !=
			    temp_mldl_cfg->offset_tc[ii])
				mldl_cfg->gyro_needs_reset = TRUE;

		for (ii = 0; ii < MPU_NUM_AXES; ii++)
			if (mldl_cfg->offset[ii] != temp_mldl_cfg->offset[ii])
				mldl_cfg->gyro_needs_reset = TRUE;

		if (memcmp(mldl_cfg->ram, temp_mldl_cfg->ram,
				MPU_MEM_NUM_RAM_BANKS * MPU_MEM_BANK_SIZE *
				sizeof(unsigned char)))
			mldl_cfg->gyro_needs_reset = TRUE;
	}

	memcpy(mldl_cfg, temp_mldl_cfg,
		offsetof(struct mldl_cfg, silicon_revision));

out:
	kfree(temp_mldl_cfg);
	return result;
}

static int
mpu_ioctl_get_mpu_config(struct i2c_client *client, unsigned long arg)
{
	/* Have to be careful as there are 3 pointers in the mldl_cfg
	 * structure */
	struct mpu_private_data *mpu =
	    (struct mpu_private_data *) i2c_get_clientdata(client);
	struct mldl_cfg *mldl_cfg = &mpu->mldl_cfg;
	struct mldl_cfg *local_mldl_cfg;
	int retval = 0;

	local_mldl_cfg = kzalloc(sizeof(struct mldl_cfg), GFP_KERNEL);
	if (NULL == local_mldl_cfg)
		return -ENOMEM;

	retval =
	    copy_from_user(local_mldl_cfg, (struct mldl_cfg __user *) arg,
			   sizeof(struct mldl_cfg));
	if (retval) {
		dev_err(&this_client->adapter->dev,
			"%s|%s:%d: EFAULT on arg\n",
			__FILE__, __func__, __LINE__);
		retval = -EFAULT;
		goto out;
	}

	/* Fill in the accel, compass, pressure and pdata pointers */
	if (mldl_cfg->accel) {
		retval = copy_to_user((void __user *)local_mldl_cfg->accel,
				      mldl_cfg->accel,
				      sizeof(*mldl_cfg->accel));
		if (retval) {
			dev_err(&this_client->adapter->dev,
				"%s|%s:%d: EFAULT on accel\n",
				__FILE__, __func__, __LINE__);
			retval = -EFAULT;
			goto out;
		}
	}

	if (mldl_cfg->compass) {
		retval = copy_to_user((void __user *)local_mldl_cfg->compass,
				      mldl_cfg->compass,
				      sizeof(*mldl_cfg->compass));
		if (retval) {
			dev_err(&this_client->adapter->dev,
				"%s|%s:%d: EFAULT on compass\n",
				__FILE__, __func__, __LINE__);
			retval = -EFAULT;
			goto out;
		}
	}

	if (mldl_cfg->pressure) {
		retval = copy_to_user((void __user *)local_mldl_cfg->pressure,
				      mldl_cfg->pressure,
				      sizeof(*mldl_cfg->pressure));
		if (retval) {
			dev_err(&this_client->adapter->dev,
				"%s|%s:%d: EFAULT on pressure\n",
				__FILE__, __func__, __LINE__);
			retval = -EFAULT;
			goto out;
		}
	}

	if (mldl_cfg->pdata) {
		retval = copy_to_user((void __user *)local_mldl_cfg->pdata,
				      mldl_cfg->pdata,
				      sizeof(*mldl_cfg->pdata));
		if (retval) {
			dev_err(&this_client->adapter->dev,
				"%s|%s:%d: EFAULT on pdata\n",
				__FILE__, __func__, __LINE__);
			retval = -EFAULT;
			goto out;
		}
	}

	/* Do not modify the accel, compass, pressure and pdata pointers */
	retval = copy_to_user((struct mldl_cfg __user *) arg,
			      mldl_cfg, offsetof(struct mldl_cfg, accel));

	if (retval)
		retval = -EFAULT;
out:
	kfree(local_mldl_cfg);
	return retval;
}

/**
 * Pass a requested slave configuration to the slave sensor
 *
 * @param adapter the adaptor to use to communicate with the slave
 * @param mldl_cfg the mldl configuration structuer
 * @param slave pointer to the slave descriptor
 * @param usr_config The configuration to pass to the slave sensor
 *
 * @return 0 or non-zero error code
 */
static int slave_config(void *adapter,
			struct mldl_cfg *mldl_cfg,
			struct ext_slave_descr *slave,
			struct ext_slave_platform_data *pdata,
			struct ext_slave_config __user *usr_config)
{
	int retval = ML_SUCCESS;
	struct ext_slave_config config;
	if ((!slave) || (!slave->config))
		return retval;

	retval = copy_from_user(&config, usr_config, sizeof(config));
	if (retval)
		return -EFAULT;

	if (config.len && config.data) {
		int *data;
		data = kzalloc(config.len, GFP_KERNEL);
		if (!data)
			return ML_ERROR_MEMORY_EXAUSTED;

		retval = copy_from_user(data,
					(void __user *)config.data,
					config.len);
		if (retval) {
			retval = -EFAULT;
			kfree(data);
			return retval;
		}
		config.data = data;
	}
	retval = slave->config(adapter, slave, pdata, &config);
	kfree(config.data);
	return retval;
}

/**
 * Get a requested slave configuration from the slave sensor
 *
 * @param adapter the adaptor to use to communicate with the slave
 * @param mldl_cfg the mldl configuration structuer
 * @param slave pointer to the slave descriptor
 * @param usr_config The configuration for the slave to fill out
 *
 * @return 0 or non-zero error code
 */
static int slave_get_config(void *adapter,
			struct mldl_cfg *mldl_cfg,
			struct ext_slave_descr *slave,
			struct ext_slave_platform_data *pdata,
			struct ext_slave_config __user *usr_config)
{
	int retval = ML_SUCCESS;
	struct ext_slave_config config;
	void *user_data;
	if (!(slave) || !(slave->get_config))
		return ML_SUCCESS;

	retval = copy_from_user(&config, usr_config, sizeof(config));
	if (retval)
		return -EFAULT;

	user_data = config.data;
	if (config.len && config.data) {
		int *data;
		data = kzalloc(config.len, GFP_KERNEL);
		if (!data)
			return ML_ERROR_MEMORY_EXAUSTED;

		retval = copy_from_user(data,
					(void __user *)config.data,
					config.len);
		if (retval) {
			retval = -EFAULT;
			kfree(data);
			return retval;
		}
		config.data = data;
	}
	retval = slave->get_config(adapter, slave, pdata, &config);
	if (retval) {
		kfree(config.data);
		return retval;
	}
	retval = copy_to_user((unsigned char __user *) user_data,
			config.data,
			config.len);
	kfree(config.data);
	return retval;
}

static int mpu_handle_mlsl(void *sl_handle,
			unsigned char addr,
			unsigned int cmd,
			struct mpu_read_write __user *usr_msg)
{
	int retval = ML_SUCCESS;
	struct mpu_read_write msg;
	unsigned char *user_data;
	retval = copy_from_user(&msg, usr_msg, sizeof(msg));
	if (retval)
		return -EFAULT;

	user_data = msg.data;
	if (msg.length && msg.data) {
		unsigned char *data;
		data = kzalloc(msg.length, GFP_KERNEL);
		if (!data)
			return ML_ERROR_MEMORY_EXAUSTED;

		retval = copy_from_user(data,
					(void __user *)msg.data,
					msg.length);
		if (retval) {
			retval = -EFAULT;
			kfree(data);
			return retval;
		}
		msg.data = data;
	} else {
		return ML_ERROR_INVALID_PARAMETER;
	}

	switch (cmd) {
	case MPU_READ:
		retval = MLSLSerialRead(sl_handle, addr,
					msg.address, msg.length, msg.data);
		break;
	case MPU_WRITE:
		retval = MLSLSerialWrite(sl_handle, addr,
					msg.length, msg.data);
		break;
	case MPU_READ_MEM:
		retval = MLSLSerialReadMem(sl_handle, addr,
					msg.address, msg.length, msg.data);
		break;
	case MPU_WRITE_MEM:
		retval = MLSLSerialWriteMem(sl_handle, addr,
					msg.address, msg.length, msg.data);
		break;
	case MPU_READ_FIFO:
		retval = MLSLSerialReadFifo(sl_handle, addr,
					msg.length, msg.data);
		break;
	case MPU_WRITE_FIFO:
		retval = MLSLSerialWriteFifo(sl_handle, addr,
					msg.length, msg.data);
		break;

	};
	retval = copy_to_user((unsigned char __user *) user_data,
			msg.data,
			msg.length);
	kfree(msg.data);
	return retval;
}

/* ioctl - I/O control */
static long mpu_ioctl(struct file *file,
		      unsigned int cmd, unsigned long arg)
{
	struct i2c_client *client =
	    (struct i2c_client *) file->private_data;
	struct mpu_private_data *mpu =
	    (struct mpu_private_data *) i2c_get_clientdata(client);
	struct mldl_cfg *mldl_cfg = &mpu->mldl_cfg;
	int retval = 0;
	struct i2c_adapter *accel_adapter;
	struct i2c_adapter *compass_adapter;
	struct i2c_adapter *pressure_adapter;

	accel_adapter = i2c_get_adapter(mldl_cfg->pdata->accel.adapt_num);
	compass_adapter =
	    i2c_get_adapter(mldl_cfg->pdata->compass.adapt_num);
	pressure_adapter =
	    i2c_get_adapter(mldl_cfg->pdata->pressure.adapt_num);

	retval = mutex_lock_interruptible(&mpu->mutex);
	if (retval) {
		dev_err(&this_client->adapter->dev,
			"%s: mutex_lock_interruptible returned %d\n",
			__func__, retval);
		return retval;
	}

	switch (cmd) {
	case MPU_SET_MPU_CONFIG:
		retval = mpu_ioctl_set_mpu_config(client, arg);
		break;
	case MPU_SET_PLATFORM_DATA:
		retval = mpu_ioctl_set_mpu_pdata(client, arg);
		break;
	case MPU_GET_MPU_CONFIG:
		retval = mpu_ioctl_get_mpu_config(client, arg);
		break;
	case MPU_READ:
	case MPU_WRITE:
	case MPU_READ_MEM:
	case MPU_WRITE_MEM:
	case MPU_READ_FIFO:
	case MPU_WRITE_FIFO:
		retval = mpu_handle_mlsl(client->adapter, mldl_cfg->addr, cmd,
					(struct mpu_read_write __user *) arg);
		break;
	case MPU_CONFIG_ACCEL:
		retval = slave_config(accel_adapter, mldl_cfg,
				mldl_cfg->accel,
				&mldl_cfg->pdata->accel,
				(struct ext_slave_config __user *) arg);
		break;
	case MPU_CONFIG_COMPASS:
		retval = slave_config(compass_adapter, mldl_cfg,
				mldl_cfg->compass,
				&mldl_cfg->pdata->compass,
				(struct ext_slave_config __user *) arg);
		break;
	case MPU_CONFIG_PRESSURE:
		retval = slave_config(pressure_adapter, mldl_cfg,
				mldl_cfg->pressure,
				&mldl_cfg->pdata->pressure,
				(struct ext_slave_config __user *) arg);
		break;
	case MPU_GET_CONFIG_ACCEL:
		retval = slave_get_config(accel_adapter, mldl_cfg,
					mldl_cfg->accel,
					&mldl_cfg->pdata->accel,
					(struct ext_slave_config __user *) arg);
		break;
	case MPU_GET_CONFIG_COMPASS:
		retval = slave_get_config(compass_adapter, mldl_cfg,
					mldl_cfg->compass,
					&mldl_cfg->pdata->compass,
					(struct ext_slave_config __user *) arg);
		break;
	case MPU_GET_CONFIG_PRESSURE:
		retval = slave_get_config(pressure_adapter, mldl_cfg,
					mldl_cfg->pressure,
					&mldl_cfg->pdata->pressure,
					(struct ext_slave_config __user *) arg);
		break;
	case MPU_SUSPEND:
	{
		unsigned long sensors;
		sensors = ~(mldl_cfg->requested_sensors);
		retval = mpu3050_suspend(mldl_cfg,
					client->adapter,
					accel_adapter,
					compass_adapter,
					pressure_adapter,
					((sensors & ML_THREE_AXIS_GYRO)
						== ML_THREE_AXIS_GYRO),
					((sensors & ML_THREE_AXIS_ACCEL)
						== ML_THREE_AXIS_ACCEL),
					((sensors & ML_THREE_AXIS_COMPASS)
						== ML_THREE_AXIS_COMPASS),
					((sensors & ML_THREE_AXIS_PRESSURE)
						== ML_THREE_AXIS_PRESSURE));
	}
	break;
	case MPU_RESUME:
	{
		unsigned long sensors;
		sensors = mldl_cfg->requested_sensors;
		retval = mpu3050_resume(mldl_cfg,
					client->adapter,
					accel_adapter,
					compass_adapter,
					pressure_adapter,
					sensors & ML_THREE_AXIS_GYRO,
					sensors & ML_THREE_AXIS_ACCEL,
					sensors & ML_THREE_AXIS_COMPASS,
					sensors & ML_THREE_AXIS_PRESSURE);
	}
	break;
	case MPU_PM_EVENT_HANDLED:
		dev_dbg(&this_client->adapter->dev,
			"%s: %d\n", __func__, cmd);
		complete(&mpu->completion);
		break;
	case MPU_READ_ACCEL:
	{
		unsigned char data[6];
		retval = mpu3050_read_accel(mldl_cfg, client->adapter,
					    data);
		if ((ML_SUCCESS == retval) &&
		    (copy_to_user((unsigned char __user *) arg,
			    data, sizeof(data))))
			retval = -EFAULT;
	}
	break;
	case MPU_READ_COMPASS:
	{
		unsigned char data[6];
		struct i2c_adapter *compass_adapt =
			i2c_get_adapter(mldl_cfg->pdata->compass.
					adapt_num);
		retval = mpu3050_read_compass(mldl_cfg, compass_adapt,
						 data);
		if ((ML_SUCCESS == retval) &&
			(copy_to_user((unsigned char *) arg,
				data, sizeof(data))))
			retval = -EFAULT;
	}
	break;
	case MPU_READ_PRESSURE:
	{
		unsigned char data[3];
		struct i2c_adapter *pressure_adapt =
			i2c_get_adapter(mldl_cfg->pdata->pressure.
					adapt_num);
		retval =
			mpu3050_read_pressure(mldl_cfg, pressure_adapt,
					data);
		if ((ML_SUCCESS == retval) &&
		    (copy_to_user((unsigned char __user *) arg,
			    data, sizeof(data))))
			retval = -EFAULT;
	}
	break;
	default:
		dev_err(&this_client->adapter->dev,
			"%s: Unknown cmd %x, arg %lu\n", __func__, cmd,
			arg);
		retval = -EINVAL;
	}

	mutex_unlock(&mpu->mutex);
	return retval;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
void mpu3050_early_suspend(struct early_suspend *h)
{
	struct mpu_private_data *mpu = container_of(h,
						    struct
						    mpu_private_data,
						    early_suspend);
	struct mldl_cfg *mldl_cfg = &mpu->mldl_cfg;
	struct i2c_adapter *accel_adapter;
	struct i2c_adapter *compass_adapter;
	struct i2c_adapter *pressure_adapter;

	accel_adapter = i2c_get_adapter(mldl_cfg->pdata->accel.adapt_num);
	compass_adapter =
	    i2c_get_adapter(mldl_cfg->pdata->compass.adapt_num);
	pressure_adapter =
	    i2c_get_adapter(mldl_cfg->pdata->pressure.adapt_num);

	dev_dbg(&this_client->adapter->dev, "%s: %d, %d\n", __func__,
		h->level, mpu->mldl_cfg.gyro_is_suspended);
	if (MPU3050_EARLY_SUSPEND_IN_DRIVER) {
		mutex_lock(&mpu->mutex);
		(void) mpu3050_suspend(mldl_cfg, this_client->adapter,
				accel_adapter, compass_adapter,
				pressure_adapter, TRUE, TRUE, TRUE, TRUE);
		mutex_unlock(&mpu->mutex);
	}
}

void mpu3050_early_resume(struct early_suspend *h)
{
	struct mpu_private_data *mpu = container_of(h,
						    struct
						    mpu_private_data,
						    early_suspend);
	struct mldl_cfg *mldl_cfg = &mpu->mldl_cfg;
	struct i2c_adapter *accel_adapter;
	struct i2c_adapter *compass_adapter;
	struct i2c_adapter *pressure_adapter;

	accel_adapter = i2c_get_adapter(mldl_cfg->pdata->accel.adapt_num);
	compass_adapter =
	    i2c_get_adapter(mldl_cfg->pdata->compass.adapt_num);
	pressure_adapter =
	    i2c_get_adapter(mldl_cfg->pdata->pressure.adapt_num);

	if (MPU3050_EARLY_SUSPEND_IN_DRIVER) {
		if (mpu->pid) {
			unsigned long sensors = mldl_cfg->requested_sensors;
			mutex_lock(&mpu->mutex);
			(void) mpu3050_resume(mldl_cfg,
					this_client->adapter,
					accel_adapter,
					compass_adapter,
					pressure_adapter,
					sensors & ML_THREE_AXIS_GYRO,
					sensors & ML_THREE_AXIS_ACCEL,
					sensors & ML_THREE_AXIS_COMPASS,
					sensors & ML_THREE_AXIS_PRESSURE);
			mutex_unlock(&mpu->mutex);
			dev_dbg(&this_client->adapter->dev,
				"%s for pid %d\n", __func__, mpu->pid);
		}
	}
	dev_dbg(&this_client->adapter->dev, "%s: %d\n", __func__, h->level);
}
#endif

void mpu_shutdown(struct i2c_client *client)
{
	struct mpu_private_data *mpu =
	    (struct mpu_private_data *) i2c_get_clientdata(client);
	struct mldl_cfg *mldl_cfg = &mpu->mldl_cfg;
	struct i2c_adapter *accel_adapter;
	struct i2c_adapter *compass_adapter;
	struct i2c_adapter *pressure_adapter;

	accel_adapter = i2c_get_adapter(mldl_cfg->pdata->accel.adapt_num);
	compass_adapter =
	    i2c_get_adapter(mldl_cfg->pdata->compass.adapt_num);
	pressure_adapter =
	    i2c_get_adapter(mldl_cfg->pdata->pressure.adapt_num);

	mutex_lock(&mpu->mutex);
	(void) mpu3050_suspend(mldl_cfg, this_client->adapter,
			       accel_adapter, compass_adapter, pressure_adapter,
			       TRUE, TRUE, TRUE, TRUE);
	mutex_unlock(&mpu->mutex);
	dev_dbg(&this_client->adapter->dev, "%s\n", __func__);
}

int mpu_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct mpu_private_data *mpu =
	    (struct mpu_private_data *) i2c_get_clientdata(client);
	struct mldl_cfg *mldl_cfg = &mpu->mldl_cfg;
	struct i2c_adapter *accel_adapter;
	struct i2c_adapter *compass_adapter;
	struct i2c_adapter *pressure_adapter;

	accel_adapter = i2c_get_adapter(mldl_cfg->pdata->accel.adapt_num);
	compass_adapter =
	    i2c_get_adapter(mldl_cfg->pdata->compass.adapt_num);
	pressure_adapter =
	    i2c_get_adapter(mldl_cfg->pdata->pressure.adapt_num);

	mutex_lock(&mpu->mutex);
	if (!mldl_cfg->ignore_system_suspend) {
		dev_dbg(&this_client->adapter->dev,
			"%s: suspending on event %d\n", __func__,
			mesg.event);
		(void) mpu3050_suspend(mldl_cfg, this_client->adapter,
				       accel_adapter, compass_adapter,
				       pressure_adapter,
				       TRUE, TRUE, TRUE, TRUE);
	} else {
		dev_dbg(&this_client->adapter->dev,
			"%s: Already suspended %d\n", __func__,
			mesg.event);
	}
	mutex_unlock(&mpu->mutex);
	return 0;
}

int mpu_resume(struct i2c_client *client)
{
	struct mpu_private_data *mpu =
	    (struct mpu_private_data *) i2c_get_clientdata(client);
	struct mldl_cfg *mldl_cfg = &mpu->mldl_cfg;
	struct i2c_adapter *accel_adapter;
	struct i2c_adapter *compass_adapter;
	struct i2c_adapter *pressure_adapter;

	accel_adapter = i2c_get_adapter(mldl_cfg->pdata->accel.adapt_num);
	compass_adapter =
	    i2c_get_adapter(mldl_cfg->pdata->compass.adapt_num);
	pressure_adapter =
	    i2c_get_adapter(mldl_cfg->pdata->pressure.adapt_num);

	mutex_lock(&mpu->mutex);
	if (mpu->pid && !mldl_cfg->ignore_system_suspend) {
		unsigned long sensors = mldl_cfg->requested_sensors;
		(void) mpu3050_resume(mldl_cfg, this_client->adapter,
				      accel_adapter,
				      compass_adapter,
				      pressure_adapter,
				      sensors & ML_THREE_AXIS_GYRO,
				      sensors & ML_THREE_AXIS_ACCEL,
				      sensors & ML_THREE_AXIS_COMPASS,
				      sensors & ML_THREE_AXIS_PRESSURE);
		dev_dbg(&this_client->adapter->dev,
			"%s for pid %d\n", __func__, mpu->pid);
	}
	mutex_unlock(&mpu->mutex);
	return 0;
}

/* define which file operations are supported */
static const struct file_operations mpu_fops = {
	.owner = THIS_MODULE,
	.read = mpu_read,
	.poll = mpu_poll,

#if HAVE_COMPAT_IOCTL
	.compat_ioctl = mpu_ioctl,
#endif
#if HAVE_UNLOCKED_IOCTL
	.unlocked_ioctl = mpu_ioctl,
#endif
	.open = mpu_open,
	.release = mpu_release,
};

static unsigned short normal_i2c[] = { I2C_CLIENT_END };

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 32)
I2C_CLIENT_INSMOD;
#endif

static struct miscdevice i2c_mpu_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mpu", /* Same for both 3050 and 6000 */
	.fops = &mpu_fops,
};


int mpu3050_probe(struct i2c_client *client,
		  const struct i2c_device_id *devid)
{
	struct mpu3050_platform_data *pdata;
	struct mpu_private_data *mpu;
	struct mldl_cfg *mldl_cfg;
	int res = 0;
	struct i2c_adapter *accel_adapter = NULL;
	struct i2c_adapter *compass_adapter = NULL;
	struct i2c_adapter *pressure_adapter = NULL;

	dev_dbg(&client->adapter->dev, "%s\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		res = -ENODEV;
		goto out_check_functionality_failed;
	}

	mpu = kzalloc(sizeof(struct mpu_private_data), GFP_KERNEL);
	if (!mpu) {
		res = -ENOMEM;
		goto out_alloc_data_failed;
	}

	i2c_set_clientdata(client, mpu);
	this_client = client;
	mldl_cfg = &mpu->mldl_cfg;

	init_waitqueue_head(&mpu->mpu_event_wait);

	mutex_init(&mpu->mutex);
	init_completion(&mpu->completion);

	mpu->response_timeout = 60; /* Seconds */
	mpu->timeout.function = mpu_pm_timeout;
	mpu->timeout.data = (u_long) mpu;
	init_timer(&mpu->timeout);

	mpu->nb.notifier_call = mpu_pm_notifier_callback;
	mpu->nb.priority = 0;
	register_pm_notifier(&mpu->nb);

	pdata = (struct mpu3050_platform_data *) client->dev.platform_data;
	if (!pdata) {
		dev_WARN(&this_client->adapter->dev,
			 "Missing platform data for mpu3050\n");
	} else {
		mldl_cfg->pdata = pdata;

#if defined(CONFIG_MPU_SENSORS_MPU3050_MODULE) || \
    defined(CONFIG_MPU_SENSORS_MPU6000_MODULE)
		pdata->accel.get_slave_descr = get_accel_slave_descr;
		pdata->compass.get_slave_descr = get_compass_slave_descr;
		pdata->pressure.get_slave_descr = get_pressure_slave_descr;
#endif

		if (pdata->accel.get_slave_descr) {
			mldl_cfg->accel =
			    pdata->accel.get_slave_descr();
			dev_info(&this_client->adapter->dev,
				 "%s: +%s\n", MPU_NAME,
				 mldl_cfg->accel->name);
			accel_adapter =
				i2c_get_adapter(pdata->accel.adapt_num);
			if (pdata->accel.irq > 0) {
				dev_info(&this_client->adapter->dev,
					"Installing Accel irq using %d\n",
					pdata->accel.irq);
				res = slaveirq_init(accel_adapter,
						&pdata->accel,
						"accelirq");
				if (res)
					goto out_accelirq_failed;
			} else {
				dev_warn(&this_client->adapter->dev,
					"WARNING: Accel irq not assigned\n");
			}
		} else {
			dev_warn(&this_client->adapter->dev,
				 "%s: No Accel Present\n", MPU_NAME);
		}

		if (pdata->compass.get_slave_descr) {
			mldl_cfg->compass =
			    pdata->compass.get_slave_descr();
			dev_info(&this_client->adapter->dev,
				 "%s: +%s\n", MPU_NAME,
				 mldl_cfg->compass->name);
			compass_adapter =
				i2c_get_adapter(pdata->compass.adapt_num);
			if (pdata->compass.irq > 0) {
				dev_info(&this_client->adapter->dev,
					"Installing Compass irq using %d\n",
					pdata->compass.irq);
				res = slaveirq_init(compass_adapter,
						&pdata->compass,
						"compassirq");
				if (res)
					goto out_compassirq_failed;
			} else {
				dev_warn(&this_client->adapter->dev,
					"WARNING: Compass irq not assigned\n");
			}
		} else {
			dev_warn(&this_client->adapter->dev,
				 "%s: No Compass Present\n", MPU_NAME);
		}

		if (pdata->pressure.get_slave_descr) {
			mldl_cfg->pressure =
			    pdata->pressure.get_slave_descr();
			dev_info(&this_client->adapter->dev,
				 "%s: +%s\n", MPU_NAME,
				 mldl_cfg->pressure->name);
			pressure_adapter =
				i2c_get_adapter(pdata->pressure.adapt_num);

			if (pdata->pressure.irq > 0) {
				dev_info(&this_client->adapter->dev,
					"Installing Pressure irq using %d\n",
					pdata->pressure.irq);
				res = slaveirq_init(pressure_adapter,
						&pdata->pressure,
						"pressureirq");
				if (res)
					goto out_pressureirq_failed;
			} else {
				dev_warn(&this_client->adapter->dev,
					"WARNING: Pressure irq not assigned\n");
			}
		} else {
			dev_warn(&this_client->adapter->dev,
				 "%s: No Pressure Present\n", MPU_NAME);
		}
	}

	mldl_cfg->addr = client->addr;
	res = mpu3050_open(&mpu->mldl_cfg, client->adapter,
			accel_adapter, compass_adapter, pressure_adapter);

	if (res) {
		dev_err(&this_client->adapter->dev,
			"Unable to open %s %d\n", MPU_NAME, res);
		res = -ENODEV;
		goto out_whoami_failed;
	}

	res = misc_register(&i2c_mpu_device);
	if (res < 0) {
		dev_err(&this_client->adapter->dev,
			"ERROR: misc_register returned %d\n", res);
		goto out_misc_register_failed;
	}

	if (this_client->irq > 0) {
		dev_info(&this_client->adapter->dev,
			 "Installing irq using %d\n", this_client->irq);
		res = mpuirq_init(this_client);
		if (res)
			goto out_mpuirq_failed;
	} else {
		dev_WARN(&this_client->adapter->dev,
			"Missing %s IRQ\n", MPU_NAME);
	}


#ifdef CONFIG_HAS_EARLYSUSPEND
	mpu->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	mpu->early_suspend.suspend = mpu3050_early_suspend;
	mpu->early_suspend.resume = mpu3050_early_resume;
	register_early_suspend(&mpu->early_suspend);
#endif
	return res;

out_mpuirq_failed:
	misc_deregister(&i2c_mpu_device);
out_misc_register_failed:
	mpu3050_close(&mpu->mldl_cfg, client->adapter,
		accel_adapter, compass_adapter, pressure_adapter);
out_whoami_failed:
	if (pdata &&
	    pdata->pressure.get_slave_descr &&
	    pdata->pressure.irq)
		slaveirq_exit(&pdata->pressure);
out_pressureirq_failed:
	if (pdata &&
	    pdata->compass.get_slave_descr &&
	    pdata->compass.irq)
		slaveirq_exit(&pdata->compass);
out_compassirq_failed:
	if (pdata &&
	    pdata->accel.get_slave_descr &&
	    pdata->accel.irq)
		slaveirq_exit(&pdata->accel);
out_accelirq_failed:
	kfree(mpu);
out_alloc_data_failed:
out_check_functionality_failed:
	dev_err(&this_client->adapter->dev, "%s failed %d\n", __func__,
		res);
	return res;

}

static int mpu3050_remove(struct i2c_client *client)
{
	struct mpu_private_data *mpu = i2c_get_clientdata(client);
	struct i2c_adapter *accel_adapter;
	struct i2c_adapter *compass_adapter;
	struct i2c_adapter *pressure_adapter;
	struct mldl_cfg *mldl_cfg = &mpu->mldl_cfg;
	struct mpu3050_platform_data *pdata = mldl_cfg->pdata;

	accel_adapter = i2c_get_adapter(mldl_cfg->pdata->accel.adapt_num);
	compass_adapter =
	    i2c_get_adapter(mldl_cfg->pdata->compass.adapt_num);
	pressure_adapter =
	    i2c_get_adapter(mldl_cfg->pdata->pressure.adapt_num);

	dev_dbg(&client->adapter->dev, "%s\n", __func__);

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&mpu->early_suspend);
#endif
	mpu3050_close(mldl_cfg, client->adapter,
		accel_adapter, compass_adapter, pressure_adapter);

	if (client->irq)
		mpuirq_exit();

	if (pdata &&
	    pdata->pressure.get_slave_descr &&
	    pdata->pressure.irq)
		slaveirq_exit(&pdata->pressure);

	if (pdata &&
	    pdata->compass.get_slave_descr &&
	    pdata->compass.irq)
		slaveirq_exit(&pdata->compass);

	if (pdata &&
	    pdata->accel.get_slave_descr &&
	    pdata->accel.irq)
		slaveirq_exit(&pdata->accel);

	misc_deregister(&i2c_mpu_device);
	kfree(mpu);

	return 0;
}

static const struct i2c_device_id mpu3050_id[] = {
	{MPU_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, mpu3050_id);

static struct i2c_driver mpu3050_driver = {
	.class = I2C_CLASS_HWMON,
	.probe = mpu3050_probe,
	.remove = mpu3050_remove,
	.id_table = mpu3050_id,
	.driver = {
		   .owner = THIS_MODULE,
		   .name = MPU_NAME,
		   },
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 32)
	.address_data = &addr_data,
#else
	.address_list = normal_i2c,
#endif

	.shutdown = mpu_shutdown,	/* optional */
	.suspend = mpu_suspend,	/* optional */
	.resume = mpu_resume,	/* optional */

};

static int __init mpu_init(void)
{
	int res = i2c_add_driver(&mpu3050_driver);
	pr_debug("%s\n", __func__);
	if (res)
		pr_err("%s failed\n",
			__func__);
	return res;
}

static void __exit mpu_exit(void)
{
	pr_debug("%s\n", __func__);
	i2c_del_driver(&mpu3050_driver);
}

module_init(mpu_init);
module_exit(mpu_exit);

MODULE_AUTHOR("Invensense Corporation");
MODULE_DESCRIPTION("User space character device interface for MPU3050");
MODULE_LICENSE("GPL");
MODULE_ALIAS(MPU_NAME);
