/*
    mpu-dev.c - mpu3050 char device interface

    Copyright (C) 1995-97 Simon G. Vogl
    Copyright (C) 1998-99 Frodo Looijaard <frodol@dds.nl>
    Copyright (C) 2003 Greg Kroah-Hartman <greg@kroah.com>
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
*/
/* Code inside mpudev_ioctl_rdrw is copied from i2c-dev.c
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
};

static int pid;
struct i2c_msg1 {
	__u16 addr;	/* slave address			*/
	__u16 flags;
#define I2C_M_TEN		0x0010	/* this is a ten bit chip address */
#define I2C_M_RD		0x0001	/* read data, from slave to master */
#define I2C_M_NOSTART		0x4000	/* if I2C_FUNC_PROTOCOL_MANGLING */
#define I2C_M_REV_DIR_ADDR	0x2000	/* if I2C_FUNC_PROTOCOL_MANGLING */
#define I2C_M_IGNORE_NAK	0x1000	/* if I2C_FUNC_PROTOCOL_MANGLING */
#define I2C_M_NO_RD_ACK		0x0800	/* if I2C_FUNC_PROTOCOL_MANGLING */
#define I2C_M_RECV_LEN		0x0400	/* length will be first received byte */
	__u16 len;		/* msg length				*/
	__u8 *buf;		/* pointer to msg data			*/
};
struct i2c_rdwr_ioctl_data1 {
	struct i2c_msg1 __user *msgs;	/* pointers to i2c_msgs */
	__u32 nmsgs;			/* number of i2c_msgs */
};
static struct i2c_client *this_client;

static int mpu_open(struct inode *inode, struct file *file)
{
	struct mpu_private_data *mpu =
	    (struct mpu_private_data *) i2c_get_clientdata(this_client);
	struct mldl_cfg *mldl_cfg = &mpu->mldl_cfg;

	dev_dbg(&this_client->adapter->dev, "mpu_open\n");
	dev_dbg(&this_client->adapter->dev, "current->pid %d\n",
		current->pid);
	pid = current->pid;
	file->private_data = this_client;
	/* we could do some checking on the flags supplied by "open" */
	/* i.e. O_NONBLOCK */
	/* -> set some flag to disable interruptible_sleep_on in mpu_read */

	/* Reset the sensors to the default */
	mldl_cfg->requested_sensors = ML_THREE_AXIS_GYRO;
	if (mldl_cfg->accel && mldl_cfg->accel->resume)
		mldl_cfg->requested_sensors |= ML_THREE_AXIS_ACCEL;

	if (mldl_cfg->compass && mldl_cfg->compass->resume)
		mldl_cfg->requested_sensors |= ML_THREE_AXIS_COMPASS;

	if (mldl_cfg->pressure && mldl_cfg->pressure->resume)
		mldl_cfg->requested_sensors |= ML_THREE_AXIS_PRESSURE;

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

	pid = 0;

	accel_adapter = i2c_get_adapter(mldl_cfg->pdata->accel.adapt_num);
	compass_adapter = i2c_get_adapter(mldl_cfg->pdata->compass.adapt_num);
	pressure_adapter = i2c_get_adapter(mldl_cfg->pdata->pressure.adapt_num);
	result = mpu3050_suspend(mldl_cfg, client->adapter,
				 accel_adapter, compass_adapter,
				 pressure_adapter,
				 TRUE, TRUE, TRUE, TRUE);

	dev_dbg(&this_client->adapter->dev, "mpu_release\n");
	return result;
}

static noinline int mpudev_ioctl_rdrw(struct i2c_client *client,
				      unsigned long arg)
{
	struct i2c_rdwr_ioctl_data1 rdwr_arg;
	struct i2c_msg1 *rdwr_pa;
	struct i2c_msg *msgs;
	u8 __user **data_ptrs;
	int i, res;

	if (copy_from_user(&rdwr_arg,
			   (struct i2c_rdwr_ioctl_data1 __user *) arg,
			   sizeof(rdwr_arg)))
		return -EFAULT;

	/* Put an arbitrary limit on the number of messages that can
	 * be sent at once */
	if (rdwr_arg.nmsgs > I2C_RDRW_IOCTL_MAX_MSGS)
		return -EINVAL;
	msgs = (struct i2c_msg *)kmalloc(rdwr_arg.nmsgs * sizeof(struct i2c_msg), GFP_KERNEL);
	rdwr_pa = (struct i2c_msg1 *)
	    kmalloc(rdwr_arg.nmsgs * sizeof(struct i2c_msg1), GFP_KERNEL);
	if (!rdwr_pa)
		return -ENOMEM;

	if (copy_from_user(rdwr_pa, rdwr_arg.msgs,
			   rdwr_arg.nmsgs * sizeof(struct i2c_msg1))) {
		kfree(rdwr_pa);
		return -EFAULT;
	}

	data_ptrs =
	    kmalloc(rdwr_arg.nmsgs * sizeof(u8 __user *), GFP_KERNEL);
	if (data_ptrs == NULL) {
		kfree(rdwr_pa);
		return -ENOMEM;
	}

	res = 0;
	for (i = 0; i < rdwr_arg.nmsgs; i++) {
		/* Limit the size of the message to a sane amount;
		 * and don't let length change either. */
		if ((rdwr_pa[i].len > 8192) ||
		    (rdwr_pa[i].flags & I2C_M_RECV_LEN)) {
			res = -EINVAL;
			break;
		}
		data_ptrs[i] = (u8 __user *) rdwr_pa[i].buf;
		rdwr_pa[i].buf = kmalloc(rdwr_pa[i].len, GFP_KERNEL);
		if (rdwr_pa[i].buf == NULL) {
			res = -ENOMEM;
			break;
		}
		if (copy_from_user(rdwr_pa[i].buf, data_ptrs[i],
				   rdwr_pa[i].len)) {
			++i;	/* Needs to be kfreed too */
			res = -EFAULT;
			break;
		}
		msgs[i].buf = rdwr_pa[i].buf;
		msgs[i].len = rdwr_pa[i].len;
		msgs[i].flags = rdwr_pa[i].flags;
		msgs[i].addr = rdwr_pa[i].addr;

		msgs[i].scl_rate = 400 * 1000;
	}
	if (res < 0) {
		int j;
		for (j = 0; j < i; ++j)
			kfree(rdwr_pa[j].buf);
		kfree(data_ptrs);
		kfree(rdwr_pa);
		kfree(msgs);
		return res;
	}

	res = i2c_transfer(client->adapter, msgs,rdwr_arg.nmsgs);
	while (i-- > 0) {
		if (res >= 0 && (rdwr_pa[i].flags & I2C_M_RD)) {
			if (copy_to_user(data_ptrs[i], rdwr_pa[i].buf,
					 rdwr_pa[i].len))
				res = -EFAULT;
		}
		kfree(rdwr_pa[i].buf);
	}
	kfree(data_ptrs);
	kfree(rdwr_pa);
	kfree(msgs);
	return res;
}

/* read function called when from /dev/mpu is read.  Read from the FIFO */
static ssize_t mpu_read(struct file *file,
			char __user *buf, size_t count, loff_t *offset)
{
	char *tmp;
	int ret;

	struct i2c_client *client =
	    (struct i2c_client *) file->private_data;

	if (count > 8192)
		count = 8192;

	tmp = kmalloc(count, GFP_KERNEL);
	if (tmp == NULL)
		return -ENOMEM;

	pr_debug("i2c-dev: i2c-%d reading %zu bytes.\n",
		 iminor(file->f_path.dentry->d_inode), count);

/* @todo fix this to do a i2c trasnfer from the FIFO */
	ret = i2c_master_recv(client, tmp, count);
	if (ret >= 0)
		ret = copy_to_user(buf, tmp, count) ? -EFAULT : ret;
	kfree(tmp);
	return ret;
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
	if (retval)
		goto out;

	/* Fill in the accel, compass, pressure and pdata pointers */
	if (mldl_cfg->accel) {
		retval = copy_to_user((void __user *)local_mldl_cfg->accel,
				      mldl_cfg->accel,
				      sizeof(*mldl_cfg->accel));
		if (retval)
			goto out;
	}

	if (mldl_cfg->compass) {
		retval = copy_to_user((void __user *)local_mldl_cfg->compass,
				      mldl_cfg->compass,
				      sizeof(*mldl_cfg->compass));
		if (retval)
			goto out;
	}

	if (mldl_cfg->pressure) {
		retval = copy_to_user(local_mldl_cfg->pressure,
				      mldl_cfg->pressure,
				      sizeof(*mldl_cfg->pressure));
		if (retval)
			goto out;
	}

	if (mldl_cfg->pdata) {
		retval = copy_to_user((void __user *)local_mldl_cfg->pdata,
				      mldl_cfg->pdata,
				      sizeof(*mldl_cfg->pdata));
		if (retval)
			goto out;
	}

	/* Do not modify the accel, compass, pressure and pdata pointers */
	retval = copy_to_user((struct mldl_cfg __user *) arg,
			      mldl_cfg, offsetof(struct mldl_cfg, accel));

out:
	kfree(local_mldl_cfg);
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

	switch (cmd) {
	case I2C_RDWR:
		mpudev_ioctl_rdrw(client, arg);
		break;
	case I2C_SLAVE:
		if ((arg & 0x7E) != (client->addr & 0x7E)) {
			dev_err(&this_client->adapter->dev,
				"%s: Invalid I2C_SLAVE arg %lu\n",
				__func__, arg);
		}
		break;
	case MPU_SET_MPU_CONFIG:
		retval = mpu_ioctl_set_mpu_config(client, arg);
		break;
	case MPU_SET_INT_CONFIG:
		mldl_cfg->int_config = (unsigned char) arg;
		break;
	case MPU_SET_EXT_SYNC:
		mldl_cfg->ext_sync = (enum mpu_ext_sync) arg;
		break;
	case MPU_SET_FULL_SCALE:
		mldl_cfg->full_scale = (enum mpu_fullscale) arg;
		break;
	case MPU_SET_LPF:
		mldl_cfg->lpf = (enum mpu_filter) arg;
		break;
	case MPU_SET_CLK_SRC:
		mldl_cfg->clk_src = (enum mpu_clock_sel) arg;
		break;
	case MPU_SET_DIVIDER:
		mldl_cfg->divider = (unsigned char) arg;
		break;
	case MPU_SET_LEVEL_SHIFTER:
		mldl_cfg->pdata->level_shifter = (unsigned char) arg;
		break;
	case MPU_SET_DMP_ENABLE:
		mldl_cfg->dmp_enable = (unsigned char) arg;
		break;
	case MPU_SET_FIFO_ENABLE:
		mldl_cfg->fifo_enable = (unsigned char) arg;
		break;
	case MPU_SET_DMP_CFG1:
		mldl_cfg->dmp_cfg1 = (unsigned char) arg;
		break;
	case MPU_SET_DMP_CFG2:
		mldl_cfg->dmp_cfg2 = (unsigned char) arg;
		break;
	case MPU_SET_OFFSET_TC:
		retval = copy_from_user(mldl_cfg->offset_tc,
					(unsigned char __user *) arg,
					sizeof(mldl_cfg->offset_tc));
		break;
	case MPU_SET_RAM:
		retval = copy_from_user(mldl_cfg->ram,
					(unsigned char __user *) arg,
					sizeof(mldl_cfg->ram));
		break;
	case MPU_SET_PLATFORM_DATA:
		retval = mpu_ioctl_set_mpu_pdata(client, arg);
		break;
	case MPU_GET_MPU_CONFIG:
		retval = mpu_ioctl_get_mpu_config(client, arg);
		break;
	case MPU_GET_INT_CONFIG:
		retval = put_user(mldl_cfg->int_config,
				  (unsigned char __user *) arg);
		break;
	case MPU_GET_EXT_SYNC:
		retval = put_user(mldl_cfg->ext_sync,
				  (unsigned char __user *) arg);
		break;
	case MPU_GET_FULL_SCALE:
		retval = put_user(mldl_cfg->full_scale,
				  (unsigned char __user *) arg);
		break;
	case MPU_GET_LPF:
		retval = put_user(mldl_cfg->lpf,
				  (unsigned char __user *) arg);
		break;
	case MPU_GET_CLK_SRC:
		retval = put_user(mldl_cfg->clk_src,
				  (unsigned char __user *) arg);
		break;
	case MPU_GET_DIVIDER:
		retval = put_user(mldl_cfg->divider,
				  (unsigned char __user *) arg);
		break;
	case MPU_GET_LEVEL_SHIFTER:
		retval = put_user(mldl_cfg->pdata->level_shifter,
				  (unsigned char __user *) arg);
		break;
	case MPU_GET_DMP_ENABLE:
		retval = put_user(mldl_cfg->dmp_enable,
				  (unsigned char __user *) arg);
		break;
	case MPU_GET_FIFO_ENABLE:
		retval = put_user(mldl_cfg->fifo_enable,
				  (unsigned char __user *) arg);
		break;
	case MPU_GET_DMP_CFG1:
		retval = put_user(mldl_cfg->dmp_cfg1,
				  (unsigned char __user *) arg);
		break;
	case MPU_GET_DMP_CFG2:
		retval = put_user(mldl_cfg->dmp_cfg2,
				  (unsigned char __user *) arg);
		break;
	case MPU_GET_OFFSET_TC:
		retval = copy_to_user((unsigned char __user *) arg,
				      mldl_cfg->offset_tc,
				      sizeof(mldl_cfg->offset_tc));
		break;
	case MPU_GET_RAM:
		retval = copy_to_user((unsigned char __user *) arg,
				      mldl_cfg->ram,
				      sizeof(mldl_cfg->ram));
		break;
	case MPU_CONFIG_ACCEL:
	{
		if ((mldl_cfg->accel) && (mldl_cfg->accel->config)) {
			struct ext_slave_config config;
			retval = copy_from_user(
				&config,
				(struct ext_slave_config *)arg,
				sizeof(config));
			if (retval)
				break;

			if (config.len && config.data) {
				int *data;
				data = kzalloc(config.len, GFP_KERNEL);
				if (!data) {
					retval = ML_ERROR_MEMORY_EXAUSTED;
					break;
				}
				retval = copy_from_user(data,
							(void *)config.data,
							config.len);
				if (retval) {
					kfree(data);
					break;
				}
				config.data = data;
			}
			retval = mldl_cfg->accel->config(
				accel_adapter,
				mldl_cfg->accel,
				&mldl_cfg->pdata->accel,
				&config);
			kfree(config.data);
		}
		break;
	}
	case MPU_CONFIG_COMPASS:
	{
		if ((mldl_cfg->compass) && (mldl_cfg->compass->config)) {
			struct ext_slave_config config;
			retval = copy_from_user(
				&config,
				(struct ext_slave_config *)arg,
				sizeof(config));
			if (retval)
				break;

			if (config.len && config.data) {
				int *data;
				data = kzalloc(config.len, GFP_KERNEL);
				if (!data) {
					retval = ML_ERROR_MEMORY_EXAUSTED;
					break;
				}
				retval = copy_from_user(data,
							(void *)config.data,
							config.len);
				if (retval) {
					kfree(data);
					break;
				}
				config.data = data;
			}
			retval = mldl_cfg->compass->config(
				compass_adapter,
				mldl_cfg->compass,
				&mldl_cfg->pdata->compass,
				&config);
			kfree(config.data);
		}
		break;
	}
	case MPU_CONFIG_PRESSURE:
	{
		if ((mldl_cfg->pressure) && (mldl_cfg->pressure->config)) {
			struct ext_slave_config config;
			retval = copy_from_user(
				&config,
				(struct ext_slave_config *)arg,
				sizeof(config));
			if (retval)
				break;

			if (config.len && config.data) {
				int *data;
				data = kzalloc(config.len, GFP_KERNEL);
				if (!data) {
					retval = ML_ERROR_MEMORY_EXAUSTED;
					break;
				}
				retval = copy_from_user(data,
							(void *)config.data,
							config.len);
				if (retval) {
					kfree(data);
					break;
				}
				config.data = data;
			}
			retval = mldl_cfg->pressure->config(
				pressure_adapter,
				mldl_cfg->pressure,
				&mldl_cfg->pdata->pressure,
				&config);
			kfree(config.data);
		}
		break;
	}
	case MPU_READ_MEMORY:
	case MPU_WRITE_MEMORY:
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
	case MPU_READ_ACCEL:
		{
			unsigned char data[6];
			retval =
			    mpu3050_read_accel(mldl_cfg, client->adapter,
					       data);
			if (ML_SUCCESS == retval)
				retval =
				    copy_to_user((unsigned char __user *) arg,
						 data, sizeof(data));
		}
		break;
	case MPU_READ_COMPASS:
		{
			unsigned char data[6];
			struct i2c_adapter *compass_adapt =
			    i2c_get_adapter(mldl_cfg->pdata->compass.
					    adapt_num);
			retval =
			    mpu3050_read_compass(mldl_cfg, compass_adapt,
						 data);
			if (ML_SUCCESS == retval)
				retval =
				    copy_to_user((unsigned char *) arg,
						 data, sizeof(data));
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
			if (ML_SUCCESS == retval)
				retval =
				    copy_to_user((unsigned char __user *) arg,
						 data, sizeof(data));
		}
		break;
	default:
		dev_err(&this_client->adapter->dev,
			"%s: Unknown cmd %d, arg %lu\n", __func__, cmd,
			arg);
		retval = -EINVAL;
	}

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
	if (MPU3050_EARLY_SUSPEND_IN_DRIVER)
		(void) mpu3050_suspend(mldl_cfg, this_client->adapter,
				accel_adapter, compass_adapter,
				pressure_adapter, TRUE, TRUE, TRUE, TRUE);
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
		if (pid) {
			unsigned long sensors = mldl_cfg->requested_sensors;
			(void) mpu3050_resume(mldl_cfg,
					this_client->adapter,
					accel_adapter,
					compass_adapter,
					pressure_adapter,
					sensors & ML_THREE_AXIS_GYRO,
					sensors & ML_THREE_AXIS_ACCEL,
					sensors & ML_THREE_AXIS_COMPASS,
					sensors & ML_THREE_AXIS_PRESSURE);
			dev_dbg(&this_client->adapter->dev,
				"%s for pid %d\n", __func__, pid);
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

	(void) mpu3050_suspend(mldl_cfg, this_client->adapter,
			       accel_adapter, compass_adapter, pressure_adapter,
			       TRUE, TRUE, TRUE, TRUE);
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

	if (!mpu->mldl_cfg.gyro_is_suspended) {
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

	if (pid) {
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
			"%s for pid %d\n", __func__, pid);
	}
	return 0;
}

/* define which file operations are supported */
static const struct file_operations mpu_fops = {
	.owner = THIS_MODULE,
	.read = mpu_read,
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
	pdata = (struct mpu3050_platform_data *) client->dev.platform_data;
	if (!pdata) {
		dev_warn(&this_client->adapter->dev,
			 "Warning no platform data for mpu3050\n");
	} else {
		mldl_cfg->pdata = pdata;

#if defined(CONFIG_SENSORS_MPU3050_MODULE) || \
    defined(CONFIG_SENSORS_MPU6000_MODULE)
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
		dev_warn(&this_client->adapter->dev,
			 "WARNING: %s irq not assigned\n", MPU_NAME);
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
	printk("xxm -> enter mpu_init\n");
	int res = i2c_add_driver(&mpu3050_driver);
	pid = 0;
	printk(KERN_DEBUG "%s\n", __func__);
	printk("xxm-> end mpu_init\n");
	if (res){
		dev_err(&this_client->adapter->dev, "%s\n",
			__func__);
	return res;
	}
}
static void __exit mpu_exit(void)
{
	printk(KERN_DEBUG "%s\n", __func__);
	i2c_del_driver(&mpu3050_driver);
}

module_init(mpu_init);
module_exit(mpu_exit);

MODULE_AUTHOR("Invensense Corporation");
MODULE_DESCRIPTION("User space character device interface for MPU3050");
MODULE_LICENSE("GPL");
MODULE_ALIAS(MPU_NAME);
