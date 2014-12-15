/*
 * Copyright (C) 2010 MEMSIC, Inc.
 *
 * Initial Code:
 *	Robbie Cao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <../arch/arm/mach-omap2/mux.h>
#include <linux/sensor/mma8452.h>
#include <linux/sensor/sensor_common.h>

#define MMA8452_DEVICE_ID	0x2a
#define MMA8452_NAME        "mma8452"    

#define LOW_G_INTERRUPT				REL_Z
#define HIGH_G_INTERRUPT 			REL_HWHEEL
#define SLOP_INTERRUPT 				REL_DIAL
#define DOUBLE_TAP_INTERRUPT 			REL_WHEEL
#define SINGLE_TAP_INTERRUPT 			REL_MISC
#define ORIENT_INTERRUPT 			ABS_PRESSURE
#define FLAT_INTERRUPT 				ABS_DISTANCE
#define INPUT_FUZZ      	32
#define INPUT_FLAT			32
#define ABSMIN 				(-2048)
#define ABSMAX 				(2047)

#define MMA8452_BUF_SIZE	6
#define MMA8452_STATUS_ZYXDR	0x08

#define MMA8452_MAX_DELAY		200
#define MODE_CHANGE_DELAY_MS	100
#define MMA8452_DELAY_PWRON		10	
#define MMA8452_DELAY_PWRDN		1	/* ms */
#define MMA8452_DELAY_SETDETECTION	MMA8452_DELAY_PWRON

#define MMA8452_CTRL_PWRON_1_5MS		0x10	/* acceleration samples 5ms */
#define MMA8452_CTRL_PWRON_1_10MS		0x18	/* acceleration samples 10ms */

#define MMA8452_RETRY_COUNT		3

static struct i2c_client *this_client;

static int dbglevel = 0;

static int mma8452_i2c_rx_data(char *buf, int len)
{
	uint8_t i;
	struct i2c_msg msgs[] = {
		{
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= buf,
		},
		{
			.addr	= this_client->addr,
			.flags	= I2C_M_RD,
			.len	= len,
			.buf	= buf,
		}
	};

	for (i = 0; i < MMA8452_RETRY_COUNT; i++) {
		if (i2c_transfer(this_client->adapter, msgs, 2) > 0) {
			break;
		}
		mdelay(10);
	}

	if (i >= MMA8452_RETRY_COUNT) {
		pr_err("%s: retry over %d\n", __FUNCTION__, MMA8452_RETRY_COUNT);
		return -EIO;
	}

	return 0;
}

static int mma8452_i2c_tx_data(char *buf, int len)
{
	uint8_t i;
	struct i2c_msg msg[] = {
		{
			.addr	= this_client->addr,
			.flags	= 0,
			.len	= len,
			.buf	= buf,
		}
	};
	
	for (i = 0; i < MMA8452_RETRY_COUNT; i++) {
		if (i2c_transfer(this_client->adapter, msg, 1) > 0) {
			break;
		}
		mdelay(10);
	}

	if (i >= MMA8452_RETRY_COUNT) {
		pr_err("%s: retry over %d\n", __FUNCTION__, MMA8452_RETRY_COUNT);
		return -EIO;
	}

	return 0;
}

struct mma8452acc {
	short	x;
	short	y;
	short	z;
} ;

struct mma8452_data {
	struct i2c_client *mma8452_client;
	atomic_t delay;
	atomic_t enable;
	unsigned char mode;
	struct input_dev *input;
	struct mma8452acc value;
	struct mutex value_mutex;
	struct mutex enable_mutex;
	struct mutex mode_mutex;
	struct delayed_work work;
	struct work_struct irq_work;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
	int IRQ;
};

static short mma8452_charge(unsigned char Mdata, unsigned char Ldata)
{
	short data;
	data = Mdata << 8 | Ldata;
	data = data >> 4;
	
	return data;
}

static int mma8452_read_accel_xyz(struct i2c_client *client,
		struct mma8452acc *acc)
{
	unsigned char buf[MMA8452_BUF_SIZE];

	buf[0] = 0x00;
	if (mma8452_i2c_rx_data(buf, 1) < 0) {
		return -EFAULT;
	}

	if (buf[0] & MMA8452_STATUS_ZYXDR) { 
		buf[0] = MMA8452_REG_DATA;
		if (mma8452_i2c_rx_data(buf, MMA8452_BUF_SIZE) < 0) {
			return -EFAULT;
		}

		acc->x = mma8452_charge(buf[0], buf[1]);		
		acc->y = mma8452_charge(buf[2], buf[3]);		
		acc->z = mma8452_charge(buf[4], buf[5]);
		
		return 0;
	}
	return -EFAULT;
}

static void mma8452_work_func(struct work_struct *work)
{
	struct mma8452_data *mma8452 = container_of((struct delayed_work *)work,
			struct mma8452_data, work);
	static struct mma8452acc acc;

	unsigned long delay = msecs_to_jiffies(atomic_read(&mma8452->delay));

	if (mma8452_read_accel_xyz(mma8452->mma8452_client, &acc) == 0) {

        aml_sensor_report_acc(mma8452->mma8452_client, mma8452->input, acc.x, acc.y, acc.z);    

		mutex_lock(&mma8452->value_mutex);
		mma8452->value = acc;
		mutex_unlock(&mma8452->value_mutex);
		if (dbglevel > 0) {
			printk("xyz(%d,%d,%d)\n", acc.x, acc.y, acc.z);
		}
	}
	schedule_delayed_work(&mma8452->work, delay);
}

static int check_mma8452_device_id(void)
{
	unsigned char buf[6];
	buf[0] = 0x0D;
	if (mma8452_i2c_rx_data(buf, 1) < 0) {
		return -EINVAL;
	}
	if (buf[0] == MMA8452_DEVICE_ID){
		return 0;
	}
	return -EINVAL;
}

static void mma8452_poweron(void)
{
    char buf[4];
	
	buf[0] = MMA8452_REG_CTRL;
	buf[1] = MMA8452_CTRL_PWRON_1_5MS; 
	if (mma8452_i2c_tx_data(buf, 2) < 0) {
		printk("mma8452 power on fail...1\n");
	}
	buf[0] = MMA8452_XYZ_DATA_CFG;
	buf[1] = MMA8452_CTRL_MODE_2G;
	if (mma8452_i2c_tx_data(buf, 2) < 0) {
		printk("mma8452 power on fail...2\n");
	}
	buf[0] = MMA8452_REG_CTRL;
	buf[1] = MMA8452_CTRL_PWRON_1_5MS;
	buf[1] |= MMA8452_CTRL_ACTIVE;
	if (mma8452_i2c_tx_data(buf, 2) < 0) {
		printk("mma8452 power on fail...3\n");
	}
	/* wait PWRON done */
	msleep(MMA8452_DELAY_PWRON);
}

static void mma8452_powerdown(void)
{
    char buf[4];
	
	buf[0] = MMA8452_REG_CTRL;
	buf[1] = MMA8452_CTRL_PWRON_1_5MS;
	buf[1] &= ~MMA8452_CTRL_ACTIVE;	//STANDBY
	if (mma8452_i2c_tx_data(buf, 2) < 0) {
		printk("mma8452 standby fail\n");
	}
	/* wait PWRDN done */
	msleep(MMA8452_DELAY_PWRDN);
}

static ssize_t mma8452_delay_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mma8452_data *mma8452 = i2c_get_clientdata(client);

	return sprintf(buf, "%d\n", atomic_read(&mma8452->delay));

}

static ssize_t mma8452_delay_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct mma8452_data *mma8452 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	atomic_set(&mma8452->delay, (unsigned int) data);

	return count;
}

static ssize_t mma8452_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mma8452_data *mma8452 = i2c_get_clientdata(client);

	return sprintf(buf, "%d\n", atomic_read(&mma8452->enable));

}

static void mma8452_set_enable(struct device *dev, int enable)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mma8452_data *mma8452 = i2c_get_clientdata(client);
	int pre_enable = atomic_read(&mma8452->enable);

	mutex_lock(&mma8452->enable_mutex);
	if (enable) {
		if (pre_enable == 0) {
			mma8452_poweron();
			schedule_delayed_work(&mma8452->work,
				msecs_to_jiffies(atomic_read(&mma8452->delay)));
			atomic_set(&mma8452->enable, 1);
		}

	} else {
		if (pre_enable == 1) {
			mma8452_powerdown();
			cancel_delayed_work_sync(&mma8452->work);
			atomic_set(&mma8452->enable, 0);
		}
	}
	mutex_unlock(&mma8452->enable_mutex);

}

static ssize_t mma8452_enable_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	if ((data == 0) || (data == 1))
		mma8452_set_enable(dev, data);

	return count;
}

static ssize_t mma8452_debug_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	
	dbglevel = data;

	return count;
}

static ssize_t mma8452_debug_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", dbglevel);
}

/*change reg*/
static void enable_change_mma8452_reg(int enable) 
{
	char buf[4];
	buf[0] = MMA8452_REG_CTRL;
	if (mma8452_i2c_rx_data(buf, 1) < 0) {
		printk("read reg[0x%x] fail", buf[0]);
		return ;
	}
	if (enable) {
		buf[1] = buf[0] & ~MMA8452_CTRL_ACTIVE;
		buf[0] = MMA8452_REG_CTRL;
		if (mma8452_i2c_tx_data(buf, 2) < 0) {
			printk("mma8452 suspend fail\n");
		}
	} else {
		buf[1] = buf[0] | MMA8452_CTRL_ACTIVE;
		buf[0] = MMA8452_REG_CTRL;
		if (mma8452_i2c_tx_data(buf, 2) < 0) {
			printk("mma8452 active fail\n");
		}
	}
}

/**
	How to use
*/
static void reg_help(void)
{
	printk("Usage:\n");
	printk("w\n");
	printk("	write register\n");
	printk("r\n");
	printk("	read register\n");
	printk("Example:\n");
	printk("	echo \"r 00\" > /sys/devices/virtual/input/input4/reg\n");
	printk("	echo \"w 01 08\" > /sys/devices/virtual/input/input4/reg\n");
}

static ssize_t mma8452_read_write_reg_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
    char str[64];
	char *start=str;
	unsigned long reg;
	unsigned long value;
	unsigned char data[4];
	int flag;  //  0: read 1:write
	
	strcpy(str, buf);
	
	// 1.Get operation        
	while (*start == ' ')
			start++;
	if (!strncmp(start, "r", 1) || !strncmp(start, "R", 1)) {
		flag = 0;
	} else if (!strncmp(start, "w", 1) || !strncmp(start, "W", 1)) {
		flag = 1;
	} else {
		reg_help();
		return -EINVAL;
	}
	start++;

	// 2.reg address
	while (*start == ' ')
			start++;
	reg = simple_strtoul(start, &start, 16);
	
	data[0] = reg;

	// wirte reg
	if (flag) {
		// 3.reg value
		while (*start == ' ')
				start++;
		value = simple_strtoul(start, &start, 16);	
		data[1] = value;		
		printk("Write reg[0x%lx]=0x%x\n", reg, data[1]);
		enable_change_mma8452_reg(1);
		if (mma8452_i2c_tx_data(data, 2) < 0) {
			printk("write reg[%lx] fail", reg);
			return -EINVAL;
		}
		enable_change_mma8452_reg(0);
		// read back
		data[0] = reg;
		if (mma8452_i2c_rx_data(data, 1) < 0) {
			printk("read reg[0x%lx] fail", reg);
			return -EINVAL;
		}
		printk("Read reg[%lx]=0x%x\n", reg, data[0]);
	} else { 
		if (mma8452_i2c_rx_data(data, 1) < 0) {
			printk("read reg[0x%lx] fail", reg);
			return -EINVAL;
		}
		printk("Read reg[0x%lx]=0x%x\n", reg, data[0]);
	}
				
	return count;
}


static DEVICE_ATTR(delay, S_IRUGO|S_IWUSR|S_IWGRP,
		mma8452_delay_show, mma8452_delay_store);
static DEVICE_ATTR(enable, S_IRUGO|S_IWUSR|S_IWGRP,
		mma8452_enable_show, mma8452_enable_store);
static DEVICE_ATTR(debug, S_IRUGO|S_IWUSR|S_IWGRP,
		mma8452_debug_show, mma8452_debug_store);
static DEVICE_ATTR(reg, S_IRUGO|S_IWUSR|S_IWGRP,
		NULL, mma8452_read_write_reg_store);
		

static struct attribute *mma8452_attributes[] = {
	&dev_attr_delay.attr,
	&dev_attr_enable.attr,
	&dev_attr_debug.attr,
	&dev_attr_reg.attr,
	NULL
};

static struct attribute_group mma8452_attribute_group = {
	.attrs = mma8452_attributes
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static void mma8452_early_suspend(struct early_suspend *h)
{
    struct mma8452_data *data =
        container_of(h, struct mma8452_data, early_suspend);

    mutex_lock(&data->enable_mutex);
	
	mma8452_powerdown();
    cancel_delayed_work_sync(&data->work);
	atomic_set(&data->enable, 0);
	
    mutex_unlock(&data->enable_mutex);
}


static void mma8452_late_resume(struct early_suspend *h)
{
    struct mma8452_data *data =
    container_of(h, struct mma8452_data, early_suspend);

    mutex_lock(&data->enable_mutex);
    
	mma8452_poweron();
    schedule_delayed_work(&data->work,
        msecs_to_jiffies(atomic_read(&data->delay)));
	atomic_set(&data->enable, 1);
	
    mutex_unlock(&data->enable_mutex);
}
#endif

static int mma8452_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int err = 0;
	struct mma8452_data *data;
	struct input_dev *dev;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		printk(KERN_INFO "i2c_check_functionality error\n");
		goto exit;
	}
	data = kzalloc(sizeof(struct mma8452_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}

	i2c_set_clientdata(client, data);
	data->mma8452_client = client;
	this_client = client;
		
	if (check_mma8452_device_id()) {
		printk("check device id error\n");
		goto kfree_exit;
	}
	
	mutex_init(&data->value_mutex);
	mutex_init(&data->mode_mutex);
	mutex_init(&data->enable_mutex);
	
	INIT_DELAYED_WORK(&data->work, mma8452_work_func);
	atomic_set(&data->delay, MMA8452_MAX_DELAY);
	atomic_set(&data->enable, 0);

	dev = input_allocate_device();
	if (!dev) {
		goto kfree_exit;
	}

	dev->name = MMA8452_NAME;
	dev->id.bustype = BUS_I2C;

	input_set_capability(dev, EV_REL, LOW_G_INTERRUPT);
	input_set_capability(dev, EV_REL, HIGH_G_INTERRUPT);
	input_set_capability(dev, EV_REL, SLOP_INTERRUPT);
	input_set_capability(dev, EV_REL, DOUBLE_TAP_INTERRUPT);
	input_set_capability(dev, EV_REL, SINGLE_TAP_INTERRUPT);
	input_set_capability(dev, EV_ABS, ORIENT_INTERRUPT);
	input_set_capability(dev, EV_ABS, FLAT_INTERRUPT);
	input_set_abs_params(dev, ABS_X, ABSMIN, ABSMAX, 0, 0);
	input_set_abs_params(dev, ABS_Y, ABSMIN, ABSMAX, 0, 0);
	input_set_abs_params(dev, ABS_Z, ABSMIN, ABSMAX, 0, 0);

	input_set_drvdata(dev, data);

	err = input_register_device(dev);
	if (err < 0) {
		input_free_device(dev);
		goto kfree_exit;
	}

	data->input = dev;

	err = sysfs_create_group(&data->input->dev.kobj,
			&mma8452_attribute_group);
	if (err < 0)
		goto error_sysfs;

#ifdef CONFIG_HAS_EARLYSUSPEND
	data->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	data->early_suspend.suspend = mma8452_early_suspend;
	data->early_suspend.resume = mma8452_late_resume;
	register_early_suspend(&data->early_suspend);
#endif

	mutex_init(&data->value_mutex);
	mutex_init(&data->mode_mutex);
	mutex_init(&data->enable_mutex);

	return 0;

error_sysfs:
	input_unregister_device(data->input);

kfree_exit:
	kfree(data);
exit:
	return err;
}

static int mma8452_remove(struct i2c_client *client)
{
	struct mma8452_data *data = i2c_get_clientdata(client);

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&data->early_suspend);
#endif
	sysfs_remove_group(&data->input->dev.kobj, &mma8452_attribute_group);
	input_unregister_device(data->input);
	kfree(data);

	return 0;
}

static const struct i2c_device_id mma8452_id[] = {
    { MMA8452_NAME, 0 },
    { }
};

MODULE_DEVICE_TABLE(i2c, mma8452_id);

static struct i2c_driver mma8452_driver = {
    .driver = {
        .owner  = THIS_MODULE,
        .name   = MMA8452_NAME,
    },
    .suspend = NULL,
    .resume  = NULL,
    .id_table = mma8452_id,
    .probe = mma8452_probe,
    .remove = mma8452_remove,

};


static int __init mma8452_init(void)
{
	return i2c_add_driver(&mma8452_driver);
}

static void __exit mma8452_exit(void)
{
	i2c_del_driver(&mma8452_driver);

}

module_init(mma8452_init);
module_exit(mma8452_exit);

MODULE_AUTHOR("Robbie Cao<hjcao@memsic.com>");
MODULE_DESCRIPTION("MEMSIC MMA8452 (DTOS) Accelerometer Sensor Driver");
MODULE_LICENSE("GPL");

