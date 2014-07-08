/*
	$License:
	Copyright (C) 2011 InvenSense Corporation, All Rights Reserved.

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
#include "mldl_cfg.h"
#include <linux/mpu.h>

#include "accel/mpu6050.h"

#include <linux/syscalls.h>
#include <linux/freezer.h>
#include <linux/jiffies.h>

#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/iio/consumer.h>





#define CAB_NUM 1
#define M_CONST             49152
//#define MPU6050_CABLIC "/data/mpu6050_cablic.dat"
#define MPU6050_CABLIC "/dev/mtd/mtd6"

static int mpu6050_get = 0;
static bool Is_cab=false;
static int count_s=0;
static int countx=0;
static int countx_l=0;
static int county=0;
static int county_l=0;
static int countz=0;
static int countz_l=0;
static int get_cablic;
static int gsensor_clear = 0;


struct cal_data {
	int valid;
	long xoffset;
	long xoffset_l;
	long xoffset1;
	long xoffset1_l;
	long yoffset;
	long yoffset_l;
	long yoffset1;
	long yoffset1_l;
	long zoffset;
	long zoffset_l;
	long zoffset1;
	long zoffset1_l;
//	char magic[4];
	char reser[512];
};

static struct cal_data cablic_arry[CAB_NUM];
static struct kobject *android_gsensor_kobj;

struct mpu6050_in{
	//struct timer_list sn_timer;
	struct workqueue_struct *wq;
	struct delayed_work delay_work;
	struct work_struct sn_work;
};

/* Platform data for the MPU */
struct mpu_private_data {
	struct miscdevice dev;
	struct i2c_client *client;

	/* mldl_cfg data */
	struct mldl_cfg mldl_cfg;
	struct mpu_ram		mpu_ram;
	struct mpu_gyro_cfg	mpu_gyro_cfg;
	struct mpu_offsets	mpu_offsets;
	struct mpu_chip_info	mpu_chip_info;
	struct inv_mpu_cfg	inv_mpu_cfg;
	struct inv_mpu_state	inv_mpu_state;

	struct mutex mutex;
	wait_queue_head_t mpu_event_wait;
	struct completion completion;
	struct timer_list timeout;
	struct notifier_block nb;
	struct mpuirq_data mpu_pm_event;
	int response_timeout;	/* In seconds */
	unsigned long event;
	int pid;
	struct module *slave_modules[EXT_SLAVE_NUM_TYPES];
};

struct mpu_platform_data mpu_data; 
struct mpu_private_data *mpu_private_data;

static void mpu_pm_timeout(u_long data)
{
	struct mpu_private_data *mpu = (struct mpu_private_data *)data;
	struct i2c_client *client = mpu->client;
	dev_dbg(&client->adapter->dev, "%s\n", __func__);
	complete(&mpu->completion);
}

static int mpu_pm_notifier_callback(struct notifier_block *nb,
				    unsigned long event, void *unused)
{
	struct mpu_private_data *mpu =
	    container_of(nb, struct mpu_private_data, nb);
	struct i2c_client *client = mpu->client;
	struct timeval event_time;
	dev_dbg(&client->adapter->dev, "%s: %ld\n", __func__, event);

	/* Prevent the file handle from being closed before we initialize
	   the completion event */
	mutex_lock(&mpu->mutex);
	if (!(mpu->pid) ||
	    (event != PM_SUSPEND_PREPARE && event != PM_POST_SUSPEND)) {
		mutex_unlock(&mpu->mutex);
		return NOTIFY_OK;
	}

	if (event == PM_SUSPEND_PREPARE)
		mpu->event = MPU_PM_EVENT_SUSPEND_PREPARE;
	if (event == PM_POST_SUSPEND)
		mpu->event = MPU_PM_EVENT_POST_SUSPEND;

	do_gettimeofday(&event_time);
	mpu->mpu_pm_event.interruptcount++;
	mpu->mpu_pm_event.irqtime =
	    (((long long)event_time.tv_sec) << 32) + event_time.tv_usec;
	mpu->mpu_pm_event.data_type = MPUIRQ_DATA_TYPE_PM_EVENT;
	mpu->mpu_pm_event.data = mpu->event;

	if (mpu->response_timeout > 0) {
		mpu->timeout.expires = jiffies + mpu->response_timeout * HZ;
		add_timer(&mpu->timeout);
	}
	INIT_COMPLETION(mpu->completion);
	mutex_unlock(&mpu->mutex);

	wake_up_interruptible(&mpu->mpu_event_wait);
	wait_for_completion(&mpu->completion);
	del_timer_sync(&mpu->timeout);
	dev_dbg(&client->adapter->dev, "%s: %ld DONE\n", __func__, event);
	return NOTIFY_OK;
}

static int mpu_dev_open(struct inode *inode, struct file *file)
{
	struct mpu_private_data *mpu =
	    container_of(file->private_data, struct mpu_private_data, dev);
	struct i2c_client *client = mpu->client;
	int result;
	int ii;
	dev_dbg(&client->adapter->dev, "%s\n", __func__);
	dev_dbg(&client->adapter->dev, "current->pid %d\n", current->pid);

	result = mutex_lock_interruptible(&mpu->mutex);
	if (mpu->pid) {
		mutex_unlock(&mpu->mutex);
		return -EBUSY;
	}
	mpu->pid = current->pid;

	/* Reset the sensors to the default */
	if (result) {
		dev_err(&client->adapter->dev,
			"%s: mutex_lock_interruptible returned %d\n",
			__func__, result);
		return result;
	}

	for (ii = 0; ii < EXT_SLAVE_NUM_TYPES; ii++)
		__module_get(mpu->slave_modules[ii]);

	mutex_unlock(&mpu->mutex);
	return 0;
}

/* close function - called when the "file" /dev/mpu is closed in userspace   */
static int mpu_release(struct inode *inode, struct file *file)
{
	struct mpu_private_data *mpu =
	    container_of(file->private_data, struct mpu_private_data, dev);
	struct i2c_client *client = mpu->client;
	struct mldl_cfg *mldl_cfg = &mpu->mldl_cfg;
	int result = 0;
	int ii;
	struct i2c_adapter *slave_adapter[EXT_SLAVE_NUM_TYPES];
	struct ext_slave_platform_data **pdata_slave = mldl_cfg->pdata_slave;

	for (ii = 0; ii < EXT_SLAVE_NUM_TYPES; ii++) {
		if (!pdata_slave[ii])
			slave_adapter[ii] = NULL;
		else
			slave_adapter[ii] =
				i2c_get_adapter(pdata_slave[ii]->adapt_num);
	}
	slave_adapter[EXT_SLAVE_TYPE_GYROSCOPE] = client->adapter;

	mutex_lock(&mpu->mutex);
	mldl_cfg->inv_mpu_cfg->requested_sensors = 0;
	result = inv_mpu_suspend(mldl_cfg,
				slave_adapter[EXT_SLAVE_TYPE_GYROSCOPE],
				slave_adapter[EXT_SLAVE_TYPE_ACCEL],
				slave_adapter[EXT_SLAVE_TYPE_COMPASS],
				slave_adapter[EXT_SLAVE_TYPE_PRESSURE],
				INV_ALL_SENSORS);
	mpu->pid = 0;
	for (ii = 0; ii < EXT_SLAVE_NUM_TYPES; ii++)
		module_put(mpu->slave_modules[ii]);

	mutex_unlock(&mpu->mutex);
	complete(&mpu->completion);
	dev_dbg(&client->adapter->dev, "mpu_release\n");

	return result;
}

/* read function called when from /dev/mpu is read.  Read from the FIFO */
static ssize_t mpu_read(struct file *file,
			char __user *buf, size_t count, loff_t *offset)
{
	struct mpu_private_data *mpu =
	    container_of(file->private_data, struct mpu_private_data, dev);
	struct i2c_client *client = mpu->client;
	size_t len = sizeof(mpu->mpu_pm_event) + sizeof(unsigned long);
	int err;

	if (!mpu->event && (!(file->f_flags & O_NONBLOCK)))
		wait_event_interruptible(mpu->mpu_event_wait, mpu->event);

	if (!mpu->event || !buf
	    || count < sizeof(mpu->mpu_pm_event))
		return 0;

	err = copy_to_user(buf, &mpu->mpu_pm_event, sizeof(mpu->mpu_pm_event));
	if (err) {
		dev_err(&client->adapter->dev,
			"Copy to user returned %d\n", err);
		return -EFAULT;
	}
	mpu->event = 0;
	return len;
}

static unsigned int mpu_poll(struct file *file, struct poll_table_struct *poll)
{
	struct mpu_private_data *mpu =
	    container_of(file->private_data, struct mpu_private_data, dev);
	int mask = 0;

	poll_wait(file, &mpu->mpu_event_wait, poll);
	if (mpu->event)
		mask |= POLLIN | POLLRDNORM;
	return mask;
}

static int mpu_dev_ioctl_get_ext_slave_platform_data(
	struct i2c_client *client,
	struct ext_slave_platform_data __user *arg)
{
	struct mpu_private_data *mpu =
	    (struct mpu_private_data *)i2c_get_clientdata(client);
	struct ext_slave_platform_data *pdata_slave;
	struct ext_slave_platform_data local_pdata_slave;

	if (copy_from_user(&local_pdata_slave, arg, sizeof(local_pdata_slave)))
		return -EFAULT;

	if (local_pdata_slave.type >= EXT_SLAVE_NUM_TYPES)
		return -EINVAL;

	pdata_slave = mpu->mldl_cfg.pdata_slave[local_pdata_slave.type];
	/* All but private data and irq_data */
	if (!pdata_slave)
		return -ENODEV;
	if (copy_to_user(arg, pdata_slave, sizeof(*pdata_slave)))
		return -EFAULT;
	return 0;
}

static int mpu_dev_ioctl_get_mpu_platform_data(
	struct i2c_client *client,
	struct mpu_platform_data __user *arg)
{
	struct mpu_private_data *mpu =
	    (struct mpu_private_data *)i2c_get_clientdata(client);
	struct mpu_platform_data *pdata = mpu->mldl_cfg.pdata;

	if (copy_to_user(arg, pdata, sizeof(*pdata)))
		return -EFAULT;
	return 0;
}

static int mpu_dev_ioctl_get_ext_slave_descr(
	struct i2c_client *client,
	struct ext_slave_descr __user *arg)
{
	struct mpu_private_data *mpu =
	    (struct mpu_private_data *)i2c_get_clientdata(client);
	struct ext_slave_descr *slave;
	struct ext_slave_descr local_slave;

	if (copy_from_user(&local_slave, arg, sizeof(local_slave)))
		return -EFAULT;

	if (local_slave.type >= EXT_SLAVE_NUM_TYPES)
		return -EINVAL;

	slave = mpu->mldl_cfg.slave[local_slave.type];
	/* All but private data and irq_data */
	if (!slave)
		return -ENODEV;
	if (copy_to_user(arg, slave, sizeof(*slave)))
		return -EFAULT;
	return 0;
}


/**
 * slave_config() - Pass a requested slave configuration to the slave sensor
 *
 * @adapter the adaptor to use to communicate with the slave
 * @mldl_cfg the mldl configuration structuer
 * @slave pointer to the slave descriptor
 * @usr_config The configuration to pass to the slave sensor
 *
 * returns 0 or non-zero error code
 */
static int inv_mpu_config(struct mldl_cfg *mldl_cfg,
			void *gyro_adapter,
			struct ext_slave_config __user *usr_config)
{
	int retval = 0;
	struct ext_slave_config config;

	retval = copy_from_user(&config, usr_config, sizeof(config));
	if (retval)
		return -EFAULT;

	if (config.len && config.data) {
		void *data;
		data = kmalloc(config.len, GFP_KERNEL);
		if (!data)
			return -ENOMEM;

		retval = copy_from_user(data,
					(void __user *)config.data, config.len);
		if (retval) {
			retval = -EFAULT;
			kfree(data);
			return retval;
		}
		config.data = data;
	}
	retval = gyro_config(gyro_adapter, mldl_cfg, &config);
	kfree(config.data);
	return retval;
}

static int inv_mpu_get_config(struct mldl_cfg *mldl_cfg,
			    void *gyro_adapter,
			    struct ext_slave_config __user *usr_config)
{
	int retval = 0;
	struct ext_slave_config config;
	void *user_data;

	retval = copy_from_user(&config, usr_config, sizeof(config));
	if (retval){
		printk("inv_mpu_get_config----------------------\n");
		return -EFAULT;
	}

	user_data = config.data;
	if (config.len && config.data) {
		void *data;
		data = kmalloc(config.len, GFP_KERNEL);
		if (!data){
			printk("inv_mpu_get_config----------------------1\n");
			return -ENOMEM;
		}

		retval = copy_from_user(data,
					(void __user *)config.data, config.len);
		if (retval) {
			printk("inv_mpu_get_config----------------------2\n");
			retval = -EFAULT;
			kfree(data);
			return retval;
		}
		config.data = data;
	}
	retval = gyro_get_config(gyro_adapter, mldl_cfg, &config);
	if (!retval)
		retval = copy_to_user((unsigned char __user *)user_data,
				config.data, config.len);
	kfree(config.data);

	return retval;
}

static int slave_config(struct mldl_cfg *mldl_cfg,
			void *gyro_adapter,
			void *slave_adapter,
			struct ext_slave_descr *slave,
			struct ext_slave_platform_data *pdata,
			struct ext_slave_config __user *usr_config)
{
	int retval = 0;
	struct ext_slave_config config;
	if ((!slave) || (!slave->config))
		return -ENODEV;

	retval = copy_from_user(&config, usr_config, sizeof(config));
	if (retval)
		return -EFAULT;

	if (config.len && config.data) {
		void *data=NULL;
		data = kmalloc(config.len, GFP_KERNEL);
		if (!data)
			return -ENOMEM;

		retval = copy_from_user(data,
					(void __user *)config.data, config.len);
		if (retval) {
			retval = -EFAULT;
			kfree(data);
			return retval;
		}
		config.data = data;
	}
	retval = inv_mpu_slave_config(mldl_cfg, gyro_adapter, slave_adapter,
				      &config, slave, pdata);
	kfree(config.data);
	return retval;
}

static int slave_get_config(struct mldl_cfg *mldl_cfg,
			    void *gyro_adapter,
			    void *slave_adapter,
			    struct ext_slave_descr *slave,
			    struct ext_slave_platform_data *pdata,
			    struct ext_slave_config __user *usr_config)
{
	int retval = 0;
	struct ext_slave_config config;
	void *user_data;
	if (!(slave) || !(slave->get_config))
		return -ENODEV;

	retval = copy_from_user(&config, usr_config, sizeof(config));
	if (retval)
		return -EFAULT;

	user_data = config.data;
	if (config.len && config.data) {
		void *data;
		data = kmalloc(config.len, GFP_KERNEL);
		if (!data)
			return -ENOMEM;

		retval = copy_from_user(data,
					(void __user *)config.data, config.len);
		if (retval) {
			retval = -EFAULT;
			kfree(data);
			return retval;
		}
		config.data = data;
	}
	retval = inv_mpu_get_slave_config(mldl_cfg, gyro_adapter,
					  slave_adapter, &config, slave, pdata);
	if (retval) {
		kfree(config.data);
		return retval;
	}
	retval = copy_to_user((unsigned char __user *)user_data,
			      config.data, config.len);
	kfree(config.data);
	return retval;
}

static int mpu6050_load_cablic(const char *addr)
{
	int ret;
	long fd = sys_open(addr,O_RDONLY,0);
	printk("yemk:mpu6050_load_cablic\n");
	if(fd < 0){
		printk("mpu6050_load_cablic: open file %s\n", MPU6050_CABLIC);
		return -1;
	}
	ret = sys_read(fd,(char __user *)cablic_arry,sizeof(cablic_arry));
	sys_close(fd);

	return ret;
}

static void mpu6050_put_cablic(const char *addr)
{
	long fd = sys_open(addr,O_CREAT | O_RDWR | O_TRUNC,0);
	printk("yemk:mpu6050_put_cablic\n");
	if(fd<0){
		printk("mpu6050_put_cablic: open file%s\n",MPU6050_CABLIC);
		return;
	}
	sys_write(fd,(const char __user *)cablic_arry,sizeof(cablic_arry));

	sys_close(fd);
}
static void mpu_get(struct work_struct *mwork)
{
	struct mpu6050_in  *bat = container_of(container_of(mwork,struct delayed_work,work), \
			struct mpu6050_in, delay_work);

	if(mpu6050_get==1){
		mpu6050_put_cablic(MPU6050_CABLIC);
		mpu6050_get=0;
	}else{
		if(get_cablic){
			get_cablic=0;
			mpu6050_load_cablic(MPU6050_CABLIC);
		}
	}

	if(gsensor_clear==1){
		mpu6050_put_cablic(MPU6050_CABLIC);
		gsensor_clear=0;
	}
	queue_delayed_work(bat ->wq, &bat ->delay_work, msecs_to_jiffies(3000));
}

static int inv_slave_read(struct mldl_cfg *mldl_cfg,
			  void *gyro_adapter,
			  void *slave_adapter,
			  struct ext_slave_descr *slave,
			  struct ext_slave_platform_data *pdata,
			  void __user *usr_data)
{
	int retval;
	unsigned char *data;
	data = kzalloc(slave->read_len, GFP_KERNEL);
	if (!data)
		return -EFAULT;

	retval = inv_mpu_slave_read(mldl_cfg, gyro_adapter, slave_adapter,
				    slave, pdata, data);

	if ((!retval) &&
	    (copy_to_user((unsigned char __user *)usr_data,
			  data, slave->read_len)))
		retval = -EFAULT;

	kfree(data);
	return retval;
}

static int mpu_handle_mlsl(void *sl_handle,
			   unsigned char addr,
			   unsigned int cmd,
			   struct mpu_read_write __user *usr_msg)
{
	int retval = 0;
	struct mpu_read_write msg;
	unsigned char *user_data;
	retval = copy_from_user(&msg, usr_msg, sizeof(msg));
	if (retval)
		return -EFAULT;

	user_data = msg.data;
	if (msg.length && msg.data) {
		unsigned char *data;
		data = kmalloc(msg.length, GFP_KERNEL);
		if (!data)
			return -ENOMEM;

		retval = copy_from_user(data,
					(void __user *)msg.data, msg.length);
		if (retval) {
			retval = -EFAULT;
			kfree(data);
			return retval;
		}
		msg.data = data;
	} else {
		return -EPERM;
	}

	switch (cmd) {
	case MPU_READ:
		retval = inv_serial_read(sl_handle, addr,
					 msg.address, msg.length, msg.data);
		break;
	case MPU_WRITE:
		retval = inv_serial_write(sl_handle, addr,
					  msg.length, msg.data);
		break;
	case MPU_READ_MEM:
		retval = inv_serial_read_mem(sl_handle, addr,
					     msg.address, msg.length, msg.data);
		break;
	case MPU_WRITE_MEM:
		retval = inv_serial_write_mem(sl_handle, addr,
					      msg.address, msg.length,
					      msg.data);
		break;
	case MPU_READ_FIFO:
		retval = inv_serial_read_fifo(sl_handle, addr,
					      msg.length, msg.data);
		break;
	case MPU_WRITE_FIFO:
		retval = inv_serial_write_fifo(sl_handle, addr,
					       msg.length, msg.data);
		break;

	};
	if (retval) {
		dev_err(&((struct i2c_adapter *)sl_handle)->dev,
			"%s: i2c %d error %d\n",
			__func__, cmd, retval);
		kfree(msg.data);
		return retval;
	}
	retval = copy_to_user((unsigned char __user *)user_data,
			      msg.data, msg.length);
	kfree(msg.data);
	return retval;
}
static int mpu6050_check(unsigned long arg)
{
	char data[6];
	short int x,y,z;
	short int zoffs=0,xoffs=0,yoffs=0;

	if(copy_from_user(data, (unsigned char __user *)arg, sizeof(data)))
		return -EFAULT;

	x = data[0]*256 + data[1];
	y = data[2]*256 + data[3];
	z = data[4]*256 + data[5];
	xoffs = x;
	yoffs = y;
	zoffs = z-M_CONST;
	//printk("yemk1111 cablic_arry[0].zoffset_l=%8ld\n",cablic_arry[0].zoffset_l);
	if(Is_cab==true){
		if(zoffs>0){
			//printk("zheng cablic_arry[0].zoffset=%8ld\n",cablic_arry[0].zoffset);
			cablic_arry[0].zoffset+=zoffs;
			countz++;
		}else{
			//printk("zheng cablic_arry[0].zoffset_l=%8ld\n",cablic_arry[0].zoffset_l);
			cablic_arry[0].zoffset_l+=zoffs;
			countz_l++;
		}
		if(xoffs>0){
			cablic_arry[0].xoffset+=xoffs;
			countx++;
		}else{
			cablic_arry[0].xoffset_l+=xoffs;
			countx_l++;
		}
		if(yoffs>0){
			cablic_arry[0].yoffset+=yoffs;
			county++;
		}else{
			cablic_arry[0].yoffset_l+=yoffs;
			county_l++;
		}
		count_s++;
		//printk("yemk2222 z=%d,zoffs=%d\n",z,zoffs);
		//printk("yemk2222 x=%8ld,xoffs=%8ld\n",x,xoffs);
		//printk("yemk2222 y=%8ld,yoffs=%8ld\n",y,yoffs);
		if(count_s==20){
			//cablic_arry[0].xoffset=cablic_arry[0].xoffset/count_s;
			//cablic_arry[0].yoffset=cablic_arry[0].yoffset/count_s;
			//printk("yemk11:cab_check->zoffset=%8ld\n",cablic_arry[0].zoffset);
			if(countz)
				cablic_arry[0].zoffset=(cablic_arry[0].zoffset-cablic_arry[0].zoffset1)/countz;
			if(countz_l)
				cablic_arry[0].zoffset_l=(cablic_arry[0].zoffset_l-cablic_arry[0].zoffset1_l)/countz_l;
			if(countx)
				cablic_arry[0].xoffset=(cablic_arry[0].xoffset-cablic_arry[0].xoffset1)/countx;
			if(countx_l)
				cablic_arry[0].xoffset_l=(cablic_arry[0].xoffset_l-cablic_arry[0].xoffset1_l)/countx_l;
			if(county)
				cablic_arry[0].yoffset=(cablic_arry[0].yoffset-cablic_arry[0].yoffset1)/county;
			if(county_l)
					cablic_arry[0].yoffset_l=(cablic_arry[0].yoffset_l-cablic_arry[0].yoffset1_l)/county_l;
			//printk("yemk cab_check->zoffset=%8ld,zoffset1=%8ld\n",cablic_arry[0].zoffset,cablic_arry[0].zoffset1);
			//printk("yemk cab_check->zoffset=%d\n",cablic_arry[0].yoffset);
			//printk("yemk2222 cab_check->zoffset=%8ld,..%8ld\n",cablic_arry[0].zoffset,cablic_arry[0].zoffset_l);
			//printk("yemk2222 cab_check->yoffset=%8ld,..%8ld ..%d --%d\n",cablic_arry[0].yoffset,cablic_arry[0].yoffset_l,county,county_l);
			//printk("yemk2222 cab_check->zoffset=%8ld,..%8ld ..%d --%d\n",cablic_arry[0].zoffset,cablic_arry[0].zoffset_l,countz,countz_l);
			cablic_arry[0].zoffset1=cablic_arry[0].zoffset;
			cablic_arry[0].zoffset1_l=cablic_arry[0].zoffset_l;

			cablic_arry[0].xoffset1=cablic_arry[0].xoffset;
			cablic_arry[0].xoffset1_l=cablic_arry[0].xoffset_l;

			cablic_arry[0].yoffset1=cablic_arry[0].yoffset;
			cablic_arry[0].yoffset1_l=cablic_arry[0].yoffset_l;

			Is_cab=false;
			count_s=0;
			countx=0;
			countx_l=0;
			county=0;
			county_l=0;
			countz=0;
			countz_l=0;
			mpu6050_get=1;
			cablic_arry[0].valid=7654321;

		}
	}else{
		if(cablic_arry[0].valid == 7654321){
			if(zoffs>=0){
				//printk("zheng z=%d,zoffs=%d\n",z,zoffs);
				data[4] = (z-cablic_arry[0].zoffset)/256;
				data[5] = (z-cablic_arry[0].zoffset)%256;
			}else{
				//printk("fu z=%d,zoffs=%d\n",z,zoffs);
				data[4] = (z-cablic_arry[0].zoffset_l)/256;
				data[5] = (z-cablic_arry[0].zoffset_l)%256;
			}

			if(xoffs>=0){
				data[0] = (x-cablic_arry[0].xoffset)/256;
				data[1] = (x-cablic_arry[0].xoffset)%256;
			}else{
				data[0] = (x-cablic_arry[0].xoffset_l)/256;
				data[1] = (x-cablic_arry[0].xoffset_l)%256;
			}

			if(yoffs>=0){
				data[2] = (y-cablic_arry[0].yoffset)/256;
				data[3] = (y-cablic_arry[0].yoffset)%256;
			}else{
				data[2] = (y-cablic_arry[0].yoffset_l)/256;
				data[3] = (y-cablic_arry[0].yoffset_l)%256;
			}
		}
	}
	if(copy_to_user((unsigned char __user *)arg,data, sizeof(data)))
		return -EFAULT;
	return 0;
}

/* ioctl - I/O control */
static long mpu_dev_ioctl(struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	struct mpu_private_data *mpu =
	    container_of(file->private_data, struct mpu_private_data, dev);
	struct i2c_client *client = mpu->client;
	struct mldl_cfg *mldl_cfg = &mpu->mldl_cfg;
	int retval = 0;
	struct i2c_adapter *slave_adapter[EXT_SLAVE_NUM_TYPES];
	struct ext_slave_descr **slave = mldl_cfg->slave;
	struct ext_slave_platform_data **pdata_slave = mldl_cfg->pdata_slave;
	int ii;

	for (ii = 0; ii < EXT_SLAVE_NUM_TYPES; ii++) {
		if (!pdata_slave[ii])
			slave_adapter[ii] = NULL;
		else
			slave_adapter[ii] =
				i2c_get_adapter(pdata_slave[ii]->adapt_num);
	}
	slave_adapter[EXT_SLAVE_TYPE_GYROSCOPE] = client->adapter;

	retval = mutex_lock_interruptible(&mpu->mutex);
	if (retval) {
		dev_err(&client->adapter->dev,
			"%s: mutex_lock_interruptible returned %d\n",
			__func__, retval);
		return retval;
	}


	switch (cmd) {
	case MPU_GET_EXT_SLAVE_PLATFORM_DATA:
		retval = mpu_dev_ioctl_get_ext_slave_platform_data(
			client,
			(struct ext_slave_platform_data __user *)arg);
		break;
	case MPU_GET_MPU_PLATFORM_DATA:
		retval = mpu_dev_ioctl_get_mpu_platform_data(
			client,
			(struct mpu_platform_data __user *)arg);
		break;
	case MPU_GET_EXT_SLAVE_DESCR:
		retval = mpu_dev_ioctl_get_ext_slave_descr(
			client,
			(struct ext_slave_descr __user *)arg);
		break;
	case MPU_READ:
	case MPU_WRITE:
	case MPU_READ_MEM:
	case MPU_WRITE_MEM:
	case MPU_READ_FIFO:
	case MPU_WRITE_FIFO:
		retval = mpu_handle_mlsl(
			slave_adapter[EXT_SLAVE_TYPE_GYROSCOPE],
			mldl_cfg->mpu_chip_info->addr, cmd,
			(struct mpu_read_write __user *)arg);
		break;
	case MPU_CONFIG_GYRO:
		retval = inv_mpu_config(
			mldl_cfg,
			slave_adapter[EXT_SLAVE_TYPE_GYROSCOPE],
			(struct ext_slave_config __user *)arg);
		break;
	case MPU_CONFIG_ACCEL:
		retval = slave_config(
			mldl_cfg,
			slave_adapter[EXT_SLAVE_TYPE_GYROSCOPE],
			slave_adapter[EXT_SLAVE_TYPE_ACCEL],
			slave[EXT_SLAVE_TYPE_ACCEL],
			pdata_slave[EXT_SLAVE_TYPE_ACCEL],
			(struct ext_slave_config __user *)arg);
		break;
	case MPU_CONFIG_COMPASS:
		retval = slave_config(
			mldl_cfg,
			slave_adapter[EXT_SLAVE_TYPE_GYROSCOPE],
			slave_adapter[EXT_SLAVE_TYPE_COMPASS],
			slave[EXT_SLAVE_TYPE_COMPASS],
			pdata_slave[EXT_SLAVE_TYPE_COMPASS],
			(struct ext_slave_config __user *)arg);
		break;
	case MPU_CONFIG_PRESSURE:
		retval = slave_config(
			mldl_cfg,
			slave_adapter[EXT_SLAVE_TYPE_GYROSCOPE],
			slave_adapter[EXT_SLAVE_TYPE_PRESSURE],
			slave[EXT_SLAVE_TYPE_PRESSURE],
			pdata_slave[EXT_SLAVE_TYPE_PRESSURE],
			(struct ext_slave_config __user *)arg);
		break;
	case MPU_GET_CONFIG_GYRO:
		retval = inv_mpu_get_config(
			mldl_cfg,
			slave_adapter[EXT_SLAVE_TYPE_GYROSCOPE],
			(struct ext_slave_config __user *)arg);
		break;
	case MPU_GET_CONFIG_ACCEL:	
		retval = slave_get_config(
			mldl_cfg,
			slave_adapter[EXT_SLAVE_TYPE_GYROSCOPE],
			slave_adapter[EXT_SLAVE_TYPE_ACCEL],
			slave[EXT_SLAVE_TYPE_ACCEL],
			pdata_slave[EXT_SLAVE_TYPE_ACCEL],
			(struct ext_slave_config __user *)arg);
		break;
	case MPU_GET_CONFIG_COMPASS:
		retval = slave_get_config(
			mldl_cfg,
			slave_adapter[EXT_SLAVE_TYPE_GYROSCOPE],
			slave_adapter[EXT_SLAVE_TYPE_COMPASS],
			slave[EXT_SLAVE_TYPE_COMPASS],
			pdata_slave[EXT_SLAVE_TYPE_COMPASS],
			(struct ext_slave_config __user *)arg);
		break;
	case MPU_GET_CONFIG_PRESSURE:
		retval = slave_get_config(
			mldl_cfg,
			slave_adapter[EXT_SLAVE_TYPE_GYROSCOPE],
			slave_adapter[EXT_SLAVE_TYPE_PRESSURE],
			slave[EXT_SLAVE_TYPE_PRESSURE],
			pdata_slave[EXT_SLAVE_TYPE_PRESSURE],
			(struct ext_slave_config __user *)arg);
		break;
	case MPU_SUSPEND:
		retval = inv_mpu_suspend(
			mldl_cfg,
			slave_adapter[EXT_SLAVE_TYPE_GYROSCOPE],
			slave_adapter[EXT_SLAVE_TYPE_ACCEL],
			slave_adapter[EXT_SLAVE_TYPE_COMPASS],
			slave_adapter[EXT_SLAVE_TYPE_PRESSURE],
			arg);
		break;
	case MPU_RESUME:
		retval = inv_mpu_resume(
			mldl_cfg,
			slave_adapter[EXT_SLAVE_TYPE_GYROSCOPE],
			slave_adapter[EXT_SLAVE_TYPE_ACCEL],
			slave_adapter[EXT_SLAVE_TYPE_COMPASS],
			slave_adapter[EXT_SLAVE_TYPE_PRESSURE],
			arg);
		break;
	case MPU_PM_EVENT_HANDLED:
		dev_dbg(&client->adapter->dev, "%s: %d\n", __func__, cmd);
		complete(&mpu->completion);
		break;
	case MPU_READ_ACCEL:
		//printk("###########MPU_READ_ACCEL\n");
		retval = inv_slave_read(
			mldl_cfg,
			slave_adapter[EXT_SLAVE_TYPE_GYROSCOPE],
			slave_adapter[EXT_SLAVE_TYPE_ACCEL],
			slave[EXT_SLAVE_TYPE_ACCEL],
			pdata_slave[EXT_SLAVE_TYPE_ACCEL],
			(unsigned char __user *)arg);
		mpu6050_check(arg);
		break;
	case MPU_READ_COMPASS:
		retval = inv_slave_read(
			mldl_cfg,
			slave_adapter[EXT_SLAVE_TYPE_GYROSCOPE],
			slave_adapter[EXT_SLAVE_TYPE_COMPASS],
			slave[EXT_SLAVE_TYPE_COMPASS],
			pdata_slave[EXT_SLAVE_TYPE_COMPASS],
			(unsigned char __user *)arg);
		break;
	case MPU_READ_PRESSURE:
		retval = inv_slave_read(
			mldl_cfg,
			slave_adapter[EXT_SLAVE_TYPE_GYROSCOPE],
			slave_adapter[EXT_SLAVE_TYPE_PRESSURE],
			slave[EXT_SLAVE_TYPE_PRESSURE],
			pdata_slave[EXT_SLAVE_TYPE_PRESSURE],
			(unsigned char __user *)arg);
		break;
	case MPU_GET_REQUESTED_SENSORS:
		if (copy_to_user(
			   (__u32 __user *)arg,
			   &mldl_cfg->inv_mpu_cfg->requested_sensors,
			   sizeof(mldl_cfg->inv_mpu_cfg->requested_sensors)))
			retval = -EFAULT;
		break;
	case MPU_SET_REQUESTED_SENSORS:
		mldl_cfg->inv_mpu_cfg->requested_sensors = arg;
		break;
	case MPU_GET_IGNORE_SYSTEM_SUSPEND:
		if (copy_to_user(
			(unsigned char __user *)arg,
			&mldl_cfg->inv_mpu_cfg->ignore_system_suspend,
			sizeof(mldl_cfg->inv_mpu_cfg->ignore_system_suspend)))
			retval = -EFAULT;
		break;
	case MPU_SET_IGNORE_SYSTEM_SUSPEND:
		mldl_cfg->inv_mpu_cfg->ignore_system_suspend = arg;
		break;
	case MPU_GET_MLDL_STATUS:
		if (copy_to_user(
			(unsigned char __user *)arg,
			&mldl_cfg->inv_mpu_state->status,
			sizeof(mldl_cfg->inv_mpu_state->status)))
			retval = -EFAULT;
		break;
	case MPU_GET_I2C_SLAVES_ENABLED:
		if (copy_to_user(
			(unsigned char __user *)arg,
			&mldl_cfg->inv_mpu_state->i2c_slaves_enabled,
			sizeof(mldl_cfg->inv_mpu_state->i2c_slaves_enabled)))
			retval = -EFAULT;
		break;
	default:
		dev_err(&client->adapter->dev,
			"%s: Unknown cmd %x, arg %lu\n",
			__func__, cmd, arg);
		retval = -EINVAL;
	};

	mutex_unlock(&mpu->mutex);
	dev_dbg(&client->adapter->dev, "%s: %08x, %08lx, %d\n",
		__func__, cmd, arg, retval);

	if (retval > 0)
		retval = -retval;

	return retval;
}

void mpu_shutdown(struct i2c_client *client)
{
	struct mpu_private_data *mpu =
	    (struct mpu_private_data *)i2c_get_clientdata(client);
	struct mldl_cfg *mldl_cfg = &mpu->mldl_cfg;
	struct i2c_adapter *slave_adapter[EXT_SLAVE_NUM_TYPES];
	struct ext_slave_platform_data **pdata_slave = mldl_cfg->pdata_slave;
	int ii;

	for (ii = 0; ii < EXT_SLAVE_NUM_TYPES; ii++) {
		if (!pdata_slave[ii])
			slave_adapter[ii] = NULL;
		else
			slave_adapter[ii] =
				i2c_get_adapter(pdata_slave[ii]->adapt_num);
	}
	slave_adapter[EXT_SLAVE_TYPE_GYROSCOPE] = client->adapter;

	mutex_lock(&mpu->mutex);
	(void)inv_mpu_suspend(mldl_cfg,
			slave_adapter[EXT_SLAVE_TYPE_GYROSCOPE],
			slave_adapter[EXT_SLAVE_TYPE_ACCEL],
			slave_adapter[EXT_SLAVE_TYPE_COMPASS],
			slave_adapter[EXT_SLAVE_TYPE_PRESSURE],
			INV_ALL_SENSORS);
	mutex_unlock(&mpu->mutex);
	dev_dbg(&client->adapter->dev, "%s\n", __func__);
}

int mpu_dev_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct mpu_private_data *mpu =
	    (struct mpu_private_data *)i2c_get_clientdata(client);
	struct mldl_cfg *mldl_cfg = &mpu->mldl_cfg;
	struct i2c_adapter *slave_adapter[EXT_SLAVE_NUM_TYPES];
	struct ext_slave_platform_data **pdata_slave = mldl_cfg->pdata_slave;
	int ii;

	for (ii = 0; ii < EXT_SLAVE_NUM_TYPES; ii++) {
		if (!pdata_slave[ii])
			slave_adapter[ii] = NULL;
		else
			slave_adapter[ii] =
				i2c_get_adapter(pdata_slave[ii]->adapt_num);
	}
	slave_adapter[EXT_SLAVE_TYPE_GYROSCOPE] = client->adapter;

	mutex_lock(&mpu->mutex);
	if (!mldl_cfg->inv_mpu_cfg->ignore_system_suspend) {
		dev_dbg(&client->adapter->dev,
			"%s: suspending on event %d\n", __func__, mesg.event);
		(void)inv_mpu_suspend(mldl_cfg,
				slave_adapter[EXT_SLAVE_TYPE_GYROSCOPE],
				slave_adapter[EXT_SLAVE_TYPE_ACCEL],
				slave_adapter[EXT_SLAVE_TYPE_COMPASS],
				slave_adapter[EXT_SLAVE_TYPE_PRESSURE],
				INV_ALL_SENSORS);
	} else {
		dev_dbg(&client->adapter->dev,
			"%s: Already suspended %d\n", __func__, mesg.event);
	}
	mutex_unlock(&mpu->mutex);
	return 0;
}

int mpu_dev_resume(struct i2c_client *client)
{
	struct mpu_private_data *mpu =
	    (struct mpu_private_data *)i2c_get_clientdata(client);
	struct mldl_cfg *mldl_cfg = &mpu->mldl_cfg;
	struct i2c_adapter *slave_adapter[EXT_SLAVE_NUM_TYPES];
	struct ext_slave_platform_data **pdata_slave = mldl_cfg->pdata_slave;
	int ii;

	for (ii = 0; ii < EXT_SLAVE_NUM_TYPES; ii++) {
		if (!pdata_slave[ii])
			slave_adapter[ii] = NULL;
		else
			slave_adapter[ii] =
				i2c_get_adapter(pdata_slave[ii]->adapt_num);
	}
	slave_adapter[EXT_SLAVE_TYPE_GYROSCOPE] = client->adapter;

	mutex_lock(&mpu->mutex);
	if (mpu->pid && !mldl_cfg->inv_mpu_cfg->ignore_system_suspend) {
		(void)inv_mpu_resume(mldl_cfg,
				slave_adapter[EXT_SLAVE_TYPE_GYROSCOPE],
				slave_adapter[EXT_SLAVE_TYPE_ACCEL],
				slave_adapter[EXT_SLAVE_TYPE_COMPASS],
				slave_adapter[EXT_SLAVE_TYPE_PRESSURE],
				mldl_cfg->inv_mpu_cfg->requested_sensors);
		dev_dbg(&client->adapter->dev,
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
	.unlocked_ioctl = mpu_dev_ioctl,
	.open = mpu_dev_open,
	.release = mpu_release,
};

int inv_mpu_register_slave(struct module *slave_module,
			struct i2c_client *slave_client,
			struct ext_slave_platform_data *slave_pdata,
			struct ext_slave_descr *(*get_slave_descr)(void))
{
	struct mpu_private_data *mpu = mpu_private_data;
	struct mldl_cfg *mldl_cfg;
	struct ext_slave_descr *slave_descr;
	struct ext_slave_platform_data **pdata_slave;
	char *irq_name = NULL;
	int result = 0;

	if (!slave_client || !slave_pdata || !get_slave_descr)
		return -EINVAL;

	if (!mpu) {
		dev_err(&slave_client->adapter->dev,
			"%s: Null mpu_private_data\n", __func__);
		return -EINVAL;
	}
	mldl_cfg    = &mpu->mldl_cfg;
	pdata_slave = mldl_cfg->pdata_slave;
	slave_descr = get_slave_descr();

	if (!slave_descr) {
		dev_err(&slave_client->adapter->dev,
			"%s: Null ext_slave_descr\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&mpu->mutex);
	if (mpu->pid) {
		mutex_unlock(&mpu->mutex);
		return -EBUSY;
	}

	if (pdata_slave[slave_descr->type]) {
		result = -EBUSY;
		goto out_unlock_mutex;
	}

	slave_pdata->address	= slave_client->addr;
	slave_pdata->irq	= 0;
	slave_pdata->adapt_num	= i2c_adapter_id(slave_client->adapter);

	dev_info(&slave_client->adapter->dev,
		"%s: +%s Type %d: Addr: %2x IRQ: %2d, Adapt: %2d\n",
		__func__,
		slave_descr->name,
		slave_descr->type,
		slave_pdata->address,
		slave_pdata->irq,
		slave_pdata->adapt_num);

	switch (slave_descr->type) {
	case EXT_SLAVE_TYPE_ACCEL:
		irq_name = "accelirq";
		break;
	case EXT_SLAVE_TYPE_COMPASS:
		irq_name = "compassirq";
		break;
	case EXT_SLAVE_TYPE_PRESSURE:
		irq_name = "pressureirq";
		break;
	default:
		irq_name = "none";
	};
	if (slave_descr->init) {
		result = slave_descr->init(slave_client->adapter,
					slave_descr,
					slave_pdata);
		if (result) {
			dev_err(&slave_client->adapter->dev,
				"%s init failed %d\n",
				slave_descr->name, result);
			goto out_unlock_mutex;
		}
	}

	if (slave_descr->type == EXT_SLAVE_TYPE_ACCEL &&
	    slave_descr->id == ACCEL_ID_MPU6050 &&
	    slave_descr->config) {
	 
		/* pass a reference to the mldl_cfg data
		   structure to the mpu6050 accel "class" */
		struct ext_slave_config config;
		config.key = MPU_SLAVE_CONFIG_INTERNAL_REFERENCE;
		config.len = sizeof(struct mldl_cfg *);
		config.apply = true;
		config.data = mldl_cfg;
		result = slave_descr->config(
			slave_client->adapter, slave_descr,
			slave_pdata, &config);
		if (result) {
			LOG_RESULT_LOCATION(result);   
			goto out_slavedescr_exit;
		}
	}
	pdata_slave[slave_descr->type] = slave_pdata;
	mpu->slave_modules[slave_descr->type] = slave_module;
	mldl_cfg->slave[slave_descr->type] = slave_descr;

	goto out_unlock_mutex;

out_slavedescr_exit:
	if (slave_descr->exit)
		slave_descr->exit(slave_client->adapter,
				  slave_descr, slave_pdata);
out_unlock_mutex:
	mutex_unlock(&mpu->mutex);

	if (!result && irq_name && (slave_pdata->irq > 0)) {
		int warn_result;
		dev_info(&slave_client->adapter->dev,
			"Installing %s irq using %d\n",
			irq_name,
			slave_pdata->irq);
		warn_result = slaveirq_init(slave_client->adapter,
					slave_pdata, irq_name);
		if (result)
			dev_WARN(&slave_client->adapter->dev,
				"%s irq assigned error: %d\n",
				slave_descr->name, warn_result);
	} else {
/*		dev_WARN(&slave_client->adapter->dev,
			"%s irq not assigned: %d %d %d\n",
			slave_descr->name,
			result, (int)irq_name, slave_pdata->irq);*/
	}

	return result;
}
EXPORT_SYMBOL(inv_mpu_register_slave);

void inv_mpu_unregister_slave(struct i2c_client *slave_client,
			struct ext_slave_platform_data *slave_pdata,
			struct ext_slave_descr *(*get_slave_descr)(void))
{
	struct mpu_private_data *mpu = mpu_private_data;
	struct mldl_cfg *mldl_cfg = &mpu->mldl_cfg;
	struct ext_slave_descr *slave_descr;
	int result;

	dev_info(&slave_client->adapter->dev, "%s\n", __func__);

	if (!slave_client || !slave_pdata || !get_slave_descr)
		return;

	if (slave_pdata->irq)
		slaveirq_exit(slave_pdata);

	slave_descr = get_slave_descr();
	if (!slave_descr)
		return;

	mutex_lock(&mpu->mutex);

	if (slave_descr->exit) {
		result = slave_descr->exit(slave_client->adapter,
					slave_descr,
					slave_pdata);
		if (result)
			dev_err(&slave_client->adapter->dev,
				"Accel exit failed %d\n", result);
	}
	mldl_cfg->slave[slave_descr->type] = NULL;
	mldl_cfg->pdata_slave[slave_descr->type] = NULL;
	mpu->slave_modules[slave_descr->type] = NULL;

	mutex_unlock(&mpu->mutex);

}
EXPORT_SYMBOL(inv_mpu_unregister_slave);

static int mpu_parse_dt(struct i2c_client *client,
				  struct mpu_platform_data *data)
{
	int ret;
	struct device_node *np = client->dev.of_node;
	//enum of_gpio_flags gpioflags;
	int length = 0,size = 0;
	struct property *prop;
	int debug = 1;
	int i;
	int orig_x,orig_y,orig_z;
	enum of_gpio_flags irq_flags;
	int irq_pin;
	u32 orientation[GYRO_NUM_AXES * GYRO_NUM_AXES];


	irq_pin=of_get_named_gpio_flags(np, "irq-gpio", 0, (enum of_gpio_flags *)&irq_flags);
	if(irq_pin<0){
		dev_err(&client->dev, "get irq-gpio error\n");
		return -EIO;
	}

	if (gpio_request(irq_pin, "mpu-irq")) {
		dev_err(&client->dev, "mpu irq request failed\n");
		return -ENODEV;
	}
	client->irq=gpio_to_irq(irq_pin);

	ret = of_property_read_u32(np,"mpu-int_config",&data->int_config);
	if(ret!=0){
		dev_err(&client->dev, "get mpu-int_config error\n");
		return -EIO;
	}

	ret = of_property_read_u32(np,"mpu-level_shifter",&data->level_shifter);
	if(ret!=0){
		dev_err(&client->dev, "get mpu-level_shifter error\n");
		return -EIO;
	}
	
	
	prop = of_find_property(np, "mpu-orientation", &length);
	if (!prop){
		dev_err(&client->dev, "get mpu-orientation length error\n");
		return -EINVAL;
	}

	size = length / sizeof(u32);

	if((size > 0)&&(size <10)){
		ret = of_property_read_u32_array(np, "mpu-orientation",
					orientation,
					 size);
		
		if(ret<0){
			dev_err(&client->dev, "get mpu-orientation data error\n");
			return -EINVAL;
		}
	}
	else{
		printk(" use default orientation\n");
	}

	for(i=0;i<9;i++)
		data->orientation[i]= orientation[i];

	ret = of_property_read_u32(np,"orientation-x",&orig_x);
	if(ret!=0){
		dev_err(&client->dev, "get orientation-x error\n");
		return -EIO;
	}

	if(orig_x>0){
		for(i=0;i<3;i++)
			if(data->orientation[i])
				data->orientation[i]=-1;
	}

	ret = of_property_read_u32(np,"orientation-y",&orig_y);
	if(ret!=0){
		dev_err(&client->dev, "get orientation-y error\n");
		return -EIO;
	}

	if(orig_y>0){
		for(i=3;i<6;i++)
			if(data->orientation[i])
				data->orientation[i]=-1;
	}


	ret = of_property_read_u32(np,"orientation-z",&orig_z);
	if(ret!=0){
		dev_err(&client->dev, "get orientation-z error\n");
		return -EIO;
	}

	if(orig_z>0){
		for(i=6;i<9;i++)
			if(data->orientation[i])
				data->orientation[i]=-1;
	}


	ret = of_property_read_u32(np,"mpu-debug",&debug);
	if(ret!=0){
		dev_err(&client->dev, "get mpu-debug error\n");
		return -EINVAL;
	}

	if(debug){
		printk("int_config=%d,level_shifter=%d,client.addr=%x,client.irq=%x\n",data->int_config, \
			data->level_shifter,client->addr,client->irq);

		for(i=0;i<size;i++)
			printk("%d ",data->orientation[i]);	
		printk("\n");	

	}
	return 0;
}


static unsigned short normal_i2c[] = { I2C_CLIENT_END };

static const struct i2c_device_id mpu_id[] = {
	{"mpu3050", 0},
	{"mpu6050", 0},
	{"mpu6050_no_accel", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, mpu_id);

int mpu_probe(struct i2c_client *client, const struct i2c_device_id *devid)
{
	struct mpu_private_data *mpu;
	struct mldl_cfg *mldl_cfg;
	int res = 0;
	int ii = 0;
	int ret = 0;
	struct mpu6050_in *mpu6050_l;

	ret = mpu_parse_dt(client,&mpu_data);
	if(ret< 0)
		printk("parse mpu dts failed\n");

	dev_info(&client->adapter->dev, "%s: %d\n", __func__, ii++);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		res = -ENODEV;
		goto out_check_functionality_failed;
	}

	mpu = kzalloc(sizeof(struct mpu_private_data), GFP_KERNEL);
	if (!mpu) {
		res = -ENOMEM;
		goto out_alloc_data_failed;
	}
	mpu6050_l = kzalloc(sizeof(*mpu6050_l), GFP_KERNEL);
	if (!mpu6050_l) {
		printk("failed to allocate device info data in mpu6050_init\n");
		ret = -ENOMEM;
		return ret;
	}
	mldl_cfg = &mpu->mldl_cfg;
	mldl_cfg->mpu_ram	= &mpu->mpu_ram;
	mldl_cfg->mpu_gyro_cfg	= &mpu->mpu_gyro_cfg;
	mldl_cfg->mpu_offsets	= &mpu->mpu_offsets;
	mldl_cfg->mpu_chip_info	= &mpu->mpu_chip_info;
	mldl_cfg->inv_mpu_cfg	= &mpu->inv_mpu_cfg;
	mldl_cfg->inv_mpu_state	= &mpu->inv_mpu_state;

	mldl_cfg->mpu_ram->length = MPU_MEM_NUM_RAM_BANKS * MPU_MEM_BANK_SIZE;
	mldl_cfg->mpu_ram->ram = kzalloc(mldl_cfg->mpu_ram->length, GFP_KERNEL);
	if (!mldl_cfg->mpu_ram->ram) {
		res = -ENOMEM;
		goto out_alloc_ram_failed;
	}
	mpu_private_data = mpu;
	i2c_set_clientdata(client, mpu);
	mpu->client = client;

	init_waitqueue_head(&mpu->mpu_event_wait);
	mutex_init(&mpu->mutex);
	init_completion(&mpu->completion);

	mpu->response_timeout = 60;	/* Seconds */
	mpu->timeout.function = mpu_pm_timeout;
	mpu->timeout.data = (u_long) mpu;
	init_timer(&mpu->timeout);

	mpu->nb.notifier_call = mpu_pm_notifier_callback;
	mpu->nb.priority = 0;
	res = register_pm_notifier(&mpu->nb);
	if (res) {
		dev_err(&client->adapter->dev,
			"Unable to register pm_notifier %d\n", res);
		goto out_register_pm_notifier_failed;
	}
	mpu6050_l->wq = create_singlethread_workqueue("mpu6050_invensen");
	INIT_DELAYED_WORK(&mpu6050_l->delay_work, mpu_get);
	queue_delayed_work(mpu6050_l->wq, &mpu6050_l->delay_work, msecs_to_jiffies(3000));
	
	mldl_cfg->pdata = &mpu_data;

	mldl_cfg->mpu_chip_info->addr = client->addr;
	res = inv_mpu_open(&mpu->mldl_cfg, client->adapter, NULL, NULL, NULL);

	if (res) {
		dev_err(&client->adapter->dev,
			"Unable to open %s %d\n", MPU_NAME, res);
		res = -ENODEV;
		goto out_whoami_failed;
	}

	mpu->dev.minor = MISC_DYNAMIC_MINOR;
	mpu->dev.name = "mpu";
	mpu->dev.fops = &mpu_fops;
	res = misc_register(&mpu->dev);
	if (res < 0) {
		dev_err(&client->adapter->dev,
			"ERROR: misc_register returned %d\n", res);
		goto out_misc_register_failed;
	}

	if (client->irq) {
		dev_info(&client->adapter->dev,
			 "Installing irq using %d\n", client->irq);
		res = mpuirq_init(client, mldl_cfg);
		if (res)
			goto out_mpuirq_failed;
	} else {
		dev_WARN(&client->adapter->dev,
			 "Missing %s IRQ\n", MPU_NAME);
	}
	
	if (!strcmp(mpu_id[1].name, devid->name)) {
		/* Special case to re-use the inv_mpu_register_slave */
		struct ext_slave_platform_data *slave_pdata;
		slave_pdata = kzalloc(sizeof(*slave_pdata), GFP_KERNEL);
		if (!slave_pdata) {
			res = -ENOMEM;
			goto out_slave_pdata_kzalloc_failed;
		}
		slave_pdata->bus = EXT_SLAVE_BUS_PRIMARY;
		for (ii = 0; ii < 9; ii++)
			slave_pdata->orientation[ii] = mpu_data.orientation[ii];
		res = inv_mpu_register_slave(
			NULL, client,
			slave_pdata,
			mpu6050_get_slave_descr);
		if (res) {
			/* if inv_mpu_register_slave fails there are no pointer
			   references to the memory allocated to slave_pdata */
			kfree(slave_pdata);
			goto out_slave_pdata_kzalloc_failed;
		}
	}
	return res;

out_slave_pdata_kzalloc_failed:
	if (client->irq)
		mpuirq_exit();
out_mpuirq_failed:
	misc_deregister(&mpu->dev);
out_misc_register_failed:
	inv_mpu_close(&mpu->mldl_cfg, client->adapter, NULL, NULL, NULL);
out_whoami_failed:
	unregister_pm_notifier(&mpu->nb);
out_register_pm_notifier_failed:
	kfree(mldl_cfg->mpu_ram->ram);
	mpu_private_data = NULL;
out_alloc_ram_failed:
	kfree(mpu);
out_alloc_data_failed:
out_check_functionality_failed:
	dev_err(&client->adapter->dev, "%s failed %d\n", __func__, res);
	return res;

}

static int mpu_remove(struct i2c_client *client)
{
	struct mpu_private_data *mpu = i2c_get_clientdata(client);
	struct i2c_adapter *slave_adapter[EXT_SLAVE_NUM_TYPES];
	struct mldl_cfg *mldl_cfg = &mpu->mldl_cfg;
	struct ext_slave_platform_data **pdata_slave = mldl_cfg->pdata_slave;
	int ii;

	for (ii = 0; ii < EXT_SLAVE_NUM_TYPES; ii++) {
		if (!pdata_slave[ii])
			slave_adapter[ii] = NULL;
		else
			slave_adapter[ii] =
				i2c_get_adapter(pdata_slave[ii]->adapt_num);
	}

	slave_adapter[EXT_SLAVE_TYPE_GYROSCOPE] = client->adapter;
	dev_dbg(&client->adapter->dev, "%s\n", __func__);

	inv_mpu_close(mldl_cfg,
		slave_adapter[EXT_SLAVE_TYPE_GYROSCOPE],
		slave_adapter[EXT_SLAVE_TYPE_ACCEL],
		slave_adapter[EXT_SLAVE_TYPE_COMPASS],
		slave_adapter[EXT_SLAVE_TYPE_PRESSURE]);

	if (mldl_cfg->slave[EXT_SLAVE_TYPE_ACCEL] &&
		(mldl_cfg->slave[EXT_SLAVE_TYPE_ACCEL]->id ==
			ACCEL_ID_MPU6050)) {
		struct ext_slave_platform_data *slave_pdata =
			mldl_cfg->pdata_slave[EXT_SLAVE_TYPE_ACCEL];
		inv_mpu_unregister_slave(
			client,
			mldl_cfg->pdata_slave[EXT_SLAVE_TYPE_ACCEL],
			mpu6050_get_slave_descr);
		kfree(slave_pdata);
	}

	if (client->irq)
		mpuirq_exit();

	misc_deregister(&mpu->dev);

	unregister_pm_notifier(&mpu->nb);

	kfree(mpu->mldl_cfg.mpu_ram->ram);
	kfree(mpu);

	return 0;
}

static const struct of_device_id of_mpu_match[] = {
	{ .compatible = "mpu6050" },
	{ /* Sentinel */ }
};

static struct i2c_driver mpu_driver = {
	.class = I2C_CLASS_HWMON,
	.probe = mpu_probe,
	.remove = mpu_remove,
	.id_table = mpu_id,
	.driver = {
		   .owner = THIS_MODULE,
		   .name = MPU_NAME,
		   .of_match_table	= of_mpu_match,
		   },
	.address_list = normal_i2c,
	.shutdown = mpu_shutdown,	/* optional */
	.suspend = mpu_dev_suspend,	/* optional */
	.resume = mpu_dev_resume,	/* optional */

};

static ssize_t show_gsensor_check(struct device *dev, \
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", Is_cab);
}


static ssize_t gsensor_check_store(struct device *dev, \
		struct device_attribute *attr, const char *buf,size_t count)
{
	switch (buf[0]) {
	case '1':
		Is_cab=true;
		break;
	case '0':
		Is_cab=false;
		break;
	default:
		Is_cab=false;
		break;
	}
	return count;

}

static ssize_t gsensor_clear_store(struct device *dev,
		struct device_attribute *attr, const char *buf,size_t count)
{
	memset(cablic_arry, 0x00, sizeof(cablic_arry));
	gsensor_clear = 1;
	return count;

}

static DEVICE_ATTR(gsensorcheck, 0666, show_gsensor_check, gsensor_check_store);
static DEVICE_ATTR(gsensorclear, 0666, NULL, gsensor_clear_store);


static int gsensor_sysfs_init(void)
{
	int ret ;

	android_gsensor_kobj = kobject_create_and_add("android_gsensor", NULL);
	if (android_gsensor_kobj == NULL) {
		printk(KERN_ERR
		       "mpu6050 gsensor_sysfs_init:"\
		       "subsystem_register failed\n");
		ret = -ENOMEM;
		goto err;
	}
	ret = sysfs_create_file(android_gsensor_kobj, &dev_attr_gsensorclear.attr);   // "gsensorclear"
	if (ret) {
		printk(KERN_ERR
		       "mpu6050 gsensor_sysfs_init: gsensorclear"\
		       "sysfs_create_group failed\n");
		goto err4;
	}
	ret = sysfs_create_file(android_gsensor_kobj, &dev_attr_gsensorcheck.attr);
	if (ret) {
		printk(KERN_ERR
		       "mpu6050 gsensor_sysfs_init:"\
		       "sysfs_create_group failed\n");
		goto err4;
	}
	return 0 ;
err4:
	kobject_del(android_gsensor_kobj);
err:
	return ret ;
}

static int __init mpu_init(void)
{
	int res = i2c_add_driver(&mpu_driver);
	pr_info("%s: Probe name %s\n", __func__, MPU_NAME);
	if (res)
		pr_err("%s failed\n", __func__);

	memset(cablic_arry, 0x00, sizeof(cablic_arry));
	get_cablic=1;
	res=gsensor_sysfs_init();
	if (res)
		pr_err("%s failed\n", __func__);
	return res;
}

static void __exit mpu_exit(void)
{
	pr_info("%s\n", __func__);
	i2c_del_driver(&mpu_driver);
}

module_init(mpu_init);
module_exit(mpu_exit);

MODULE_AUTHOR("Invensense Corporation");
MODULE_DESCRIPTION("User space character device interface for MPU");
MODULE_LICENSE("GPL");
MODULE_ALIAS(MPU_NAME);
