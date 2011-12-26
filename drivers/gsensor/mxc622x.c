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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/sysctl.h>
#include <asm/uaccess.h>

#include <mach/system.h>
#include <mach/hardware.h>
#include <mach/sys_config.h>
#include "mxc622x.h"

#include <linux/earlysuspend.h>

#define DEBUG			0
#define MAX_FAILURE_COUNT	3

#define MXC622X_DELAY_PWRON	300	/* ms, >= 300 ms */
#define MXC622X_DELAY_PWRDN	1	/* ms */
#define MXC622X_DELAY_SETDETECTION	MXC622X_DELAY_PWRON

#define MXC622X_RETRY_COUNT	3
#define SENSOR_NAME	MXC622X_I2C_NAME
static struct i2c_client *this_client;

struct mxc622x_data {
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
} this_data;


/* Addresses to scan */
static union{
	unsigned short dirty_addr_buf[2];
	const unsigned short normal_i2c[2];
}u_i2c_addr = {{0x00},};
static __u32 twi_id = 0;

/**
 * gsensor_fetch_sysconfig_para - get config info from sysconfig.fex file.
 * return value:  
 *                    = 0; success;
 *                    < 0; err
 */
static int gsensor_fetch_sysconfig_para(void)
{
	int ret = -1;
	int device_used = -1;
	__u32 twi_addr = 0;
	char name[I2C_NAME_SIZE];
	script_parser_value_type_t type = SCIRPT_PARSER_VALUE_TYPE_STRING;
		
	printk("========%s===================\n", __func__);
	 
	if(SCRIPT_PARSER_OK != (ret = script_parser_fetch("gsensor_para", "gsensor_used", &device_used, 1))){
	                pr_err("%s: script_parser_fetch err.ret = %d. \n", __func__, ret);
	                goto script_parser_fetch_err;
	}
	if(1 == device_used){
		if(SCRIPT_PARSER_OK != script_parser_fetch_ex("gsensor_para", "gsensor_name", (int *)(&name), &type, sizeof(name)/sizeof(int))){
			pr_err("%s: line: %d script_parser_fetch err. \n", __func__, __LINE__);
			goto script_parser_fetch_err;
		}
		if(strcmp(SENSOR_NAME, name)){
			pr_err("%s: name %s does not match SENSOR_NAME. \n", __func__, name);
			pr_err(SENSOR_NAME);
			//ret = 1;
			return ret;
		}
		if(SCRIPT_PARSER_OK != script_parser_fetch("gsensor_para", "gsensor_twi_addr", &twi_addr, sizeof(twi_addr)/sizeof(__u32))){
			pr_err("%s: line: %d: script_parser_fetch err. \n", name, __LINE__);
			goto script_parser_fetch_err;
		}
		u_i2c_addr.dirty_addr_buf[0] = twi_addr;
		u_i2c_addr.dirty_addr_buf[1] = I2C_CLIENT_END;
		printk("%s: after: gsensor_twi_addr is 0x%x, dirty_addr_buf: 0x%hx. dirty_addr_buf[1]: 0x%hx \n", \
			__func__, twi_addr, u_i2c_addr.dirty_addr_buf[0], u_i2c_addr.dirty_addr_buf[1]);

		if(SCRIPT_PARSER_OK != script_parser_fetch("gsensor_para", "gsensor_twi_id", &twi_id, 1)){
			pr_err("%s: script_parser_fetch err. \n", name);
			goto script_parser_fetch_err;
		}
		printk("%s: tkey_twi_id is %d. \n", __func__, twi_id);

		ret = 0;
		
	}else{
		pr_err("%s: gsensor_unused. \n",  __func__);
		ret = -1;
	}

	return ret;

script_parser_fetch_err:
	pr_notice("=========script_parser_fetch_err============\n");
	return ret;

}

/**
 * gsensor_detect - Device detection callback for automatic device creation
 * return value:  
 *                    = 0; success;
 *                    < 0; err
 */
int gsensor_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	
	if(twi_id == adapter->nr){
		pr_info("%s: Detected chip %s at adapter %d, address 0x%02x\n",
			 __func__, SENSOR_NAME, i2c_adapter_id(adapter), client->addr);

		strlcpy(info->type, SENSOR_NAME, I2C_NAME_SIZE);
		return 0;
	}else{
		return -ENODEV;
	}
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void mxc622x_early_suspend(struct early_suspend *h);
static void mxc622x_late_resume(struct early_suspend *h);
#endif


static int mxc622x_i2c_rx_data(char *buf, int len)
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

	for (i = 0; i < MXC622X_RETRY_COUNT; i++) {
		if (i2c_transfer(this_client->adapter, msgs, 2) > 0) {
			break;
		}
		mdelay(10);
	}

	if (i >= MXC622X_RETRY_COUNT) {
		pr_err("%s: retry over %d\n", __FUNCTION__, MXC622X_RETRY_COUNT);
		return -EIO;
	}

	return 0;
}

static int mxc622x_i2c_tx_data(char *buf, int len)
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
	
	for (i = 0; i < MXC622X_RETRY_COUNT; i++) {
		if (i2c_transfer(this_client->adapter, msg, 1) > 0) {
			break;
		}
		mdelay(10);
	}

	if (i >= MXC622X_RETRY_COUNT) {
		pr_err("%s: retry over %d\n", __FUNCTION__, MXC622X_RETRY_COUNT);
		return -EIO;
	}

	return 0;
}

static int mxc622x_open(struct inode *inode, struct file *file)
{
	return nonseekable_open(inode, file);
}

static int mxc622x_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long mxc622x_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *pa = (void __user *)arg;
	unsigned char data[16] = {0};
	int vec[3] = {0};

	switch (cmd) {
	case MXC622X_IOC_PWRON:
		data[0] = MXC622X_REG_CTRL;
		data[1] = MXC622X_CTRL_PWRON;
		if (mxc622x_i2c_tx_data(data, 2) < 0) {
			return -EFAULT;
		}
		/* wait PWRON done */
		msleep(MXC622X_DELAY_PWRON);
		break;
	case MXC622X_IOC_PWRDN:
		data[0] = MXC622X_REG_CTRL;
		data[1] = MXC622X_CTRL_PWRDN;
		if (mxc622x_i2c_tx_data(data, 2) < 0) {
			return -EFAULT;
		}
		/* wait PWRDN done */
		msleep(MXC622X_DELAY_PWRDN);
		break;
	case MXC622X_IOC_READXYZ:
		data[0] = MXC622X_REG_DATA;
		if (mxc622x_i2c_rx_data(data, 2) < 0) {
			return -EFAULT;
		}
		vec[0] = (int)data[0];
		vec[1] = (int)data[1];
		vec[2] = (int)data[2];
	#if DEBUG
		printk("[X - %04x] [Y - %04x] [Z - %04x]\n", 
			vec[0], vec[1], vec[2]);
	#endif
		if (copy_to_user(pa, vec, sizeof(vec))) {
			return -EFAULT;
		}
		break;
	case MXC622X_IOC_READSTATUS:
		data[0] = MXC622X_REG_DATA;
		if (mxc622x_i2c_rx_data(data, 3) < 0) {
			return -EFAULT;
		}
		vec[0] = (int)data[0];
		vec[1] = (int)data[1];
		vec[2] = (unsigned int)data[2];
	#if DEBUG
		printk("[X - %04x] [Y - %04x] [STATUS - %04x]\n", 
			vec[0], vec[1], vec[2]);
	#endif
		if (copy_to_user(pa, vec, sizeof(vec))) {
			return -EFAULT;
		}
		break;
	case MXC622X_IOC_SETDETECTION:
		data[0] = MXC622X_REG_CTRL;
		if (copy_from_user(&(data[1]), pa, sizeof(unsigned char))) {
			return -EFAULT;
		}
		if (mxc622x_i2c_tx_data(data, 2) < 0) {
			return -EFAULT;
		}
		/* wait SETDETECTION done */
		msleep(MXC622X_DELAY_SETDETECTION);
		break;
	default:
		break;
	}

	return 0;
}

static ssize_t mxc622x_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	sprintf(buf, "MXC622X");
	ret = strlen(buf) + 1;

	return ret;
}

static DEVICE_ATTR(mxc622x, S_IRUGO, mxc622x_show, NULL);

static struct file_operations mxc622x_fops = {
	.owner		= THIS_MODULE,
	.open		= mxc622x_open,
	.release	= mxc622x_release,
	.unlocked_ioctl		= mxc622x_ioctl,
};

static struct miscdevice mxc622x_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = MXC622X_I2C_NAME,
	.fops = &mxc622x_fops,
};

int mxc622x_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int res = 0;

	printk("%s, line is: %d. \n", __func__, __LINE__);
	
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("%s: functionality check failed\n", __FUNCTION__);
		res = -ENODEV;
		goto out;
	}
	this_client = client;

	res = misc_register(&mxc622x_device);
	if (res) {
		pr_err("%s: mxc622x_device register failed\n", __FUNCTION__);
		goto out;
	}
	res = device_create_file(&client->dev, &dev_attr_mxc622x);
	if (res) {
		pr_err("%s: device_create_file failed\n", __FUNCTION__);
		goto out_deregister;
	}
#ifdef CONFIG_HAS_EARLYSUSPEND
	this_data.early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	this_data.early_suspend.suspend = mxc622x_early_suspend;
	this_data.early_suspend.resume = mxc622x_late_resume;
	register_early_suspend(&this_data.early_suspend);
#endif

	return 0;

out_deregister:
	misc_deregister(&mxc622x_device);
out:
	return res;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void mxc622x_early_suspend(struct early_suspend *h)
{
	unsigned char data[4] = {0};

	data[0] = MXC622X_REG_CTRL;
	data[1] = MXC622X_CTRL_PWRDN;
	if (mxc622x_i2c_tx_data(data, 2) < 0) {
		pr_warning("mxc622x_early_suspend: power down mxc622x err\n");
	}
	/* wait PWRDN done */
	msleep(MXC622X_DELAY_PWRDN);
}

static void mxc622x_late_resume(struct early_suspend *h)
{
	unsigned char data[4] = {0};

	data[0] = MXC622X_REG_CTRL;
	data[1] = MXC622X_CTRL_PWRON;
	if (mxc622x_i2c_tx_data(data, 2) < 0) {
		pr_err("mxc622x_late_resume: power on mxc622x err\n");
	}
	/* wait PWRON done */
	msleep(MXC622X_DELAY_PWRON);
}
#endif /* CONFIG_HAS_EARLYSUSPEND */

static int mxc622x_remove(struct i2c_client *client)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&this_data.early_suspend);
#endif
	device_remove_file(&client->dev, &dev_attr_mxc622x);
	misc_deregister(&mxc622x_device);

	return 0;
}

static const struct i2c_device_id mxc622x_id[] = {
	{ MXC622X_I2C_NAME, 0 },
	{ }
};

static struct i2c_driver mxc622x_driver = {
	.class = I2C_CLASS_HWMON,
	.probe 		= mxc622x_probe,
	.remove 	= mxc622x_remove,
	.id_table	= mxc622x_id,
	.driver 	= {
		.owner	= THIS_MODULE,
		.name = MXC622X_I2C_NAME,
	},
	.address_list	= u_i2c_addr.normal_i2c,
};

static int __init mxc622x_init(void)
{
	int ret = -1;
	pr_info("mxc622x driver: init\n");
	
	if(gsensor_fetch_sysconfig_para()){
		printk("%s: err.\n", __func__);
		return -1;
	}

	printk("%s: after fetch_sysconfig_para:  normal_i2c: 0x%hx. normal_i2c[1]: 0x%hx \n", \
	__func__, u_i2c_addr.normal_i2c[0], u_i2c_addr.normal_i2c[1]);

	mxc622x_driver.detect = gsensor_detect;
	
	ret = i2c_add_driver(&mxc622x_driver);

	return ret;
}

static void __exit mxc622x_exit(void)
{
	pr_info("mxc622x driver: exit\n");
	i2c_del_driver(&mxc622x_driver);
}

module_init(mxc622x_init);
module_exit(mxc622x_exit);

MODULE_AUTHOR("Robbie Cao<hjcao@memsic.com>");
MODULE_DESCRIPTION("MEMSIC MXC622X (DTOS) Accelerometer Sensor Driver");
MODULE_LICENSE("GPL");

