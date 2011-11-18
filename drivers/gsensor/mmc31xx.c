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

#include <linux/mmc31xx.h>
#include <linux/platform_device.h>

#define DEBUG			0
#define MAX_FAILURE_COUNT	3

#define MMC31XX_DELAY_TM	10	/* ms */
#define MMC31XX_DELAY_SET	10	/* ms */
#define MMC31XX_DELAY_RST	10	/* ms */
#define MMC31XX_DELAY_STDN	1	/* ms */

#define MMC31XX_RETRY_COUNT	3
#define MMC31XX_RESET_INTV	10

#define MMC31XX_DEV_NAME	"mmc31xx"

static unsigned short mmc31xx_force[] = {2, MMC31XX_I2C_ADDR, I2C_CLIENT_END, I2C_CLIENT_END};
static const unsigned short *const mmc31xx_forces[] = { mmc31xx_force, NULL };
static struct i2c_client_address_data mmc31xx_addr_data = { .forces = mmc31xx_forces,};

static u32 read_idx = 0;

static struct i2c_client *this_client;

static int mmc31xx_i2c_rx_data(char *buf, int len)
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

	for (i = 0; i < MMC31XX_RETRY_COUNT; i++) {
		if (i2c_transfer(this_client->adapter, msgs, 2) >= 0) {
			break;
		}
		mdelay(10);
	}

	if (i >= MMC31XX_RETRY_COUNT) {
		pr_err("%s: retry over %d\n", __FUNCTION__, MMC31XX_RETRY_COUNT);
		return -EIO;
	}

	return 0;
}

static int mmc31xx_i2c_tx_data(char *buf, int len)
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
	
	for (i = 0; i < MMC31XX_RETRY_COUNT; i++) {
		if (i2c_transfer(this_client->adapter, msg, 1) >= 0) {
			break;
		}
		mdelay(10);
	}

	if (i >= MMC31XX_RETRY_COUNT) {
		pr_err("%s: retry over %d\n", __FUNCTION__, MMC31XX_RETRY_COUNT);
		return -EIO;
	}
	return 0;
}

static int mmc31xx_open(struct inode *inode, struct file *file)
{
	return nonseekable_open(inode, file);
}

static int mmc31xx_release(struct inode *inode, struct file *file)
{
	return 0;
}

static int mmc31xx_ioctl(struct inode *inode, struct file *file, 
	unsigned int cmd, unsigned long arg)
{
	void __user *pa = (void __user *)arg;
	unsigned char data[16] = {0};
	int vec[3] = {0};

	switch (cmd) {
	case MMC31XX_IOC_TM:
		data[0] = MMC31XX_REG_CTRL;
		data[1] = MMC31XX_CTRL_TM;
		if (mmc31xx_i2c_tx_data(data, 2) < 0) {
			return -EFAULT;
		}
		/* wait TM done for coming data read */
		msleep(MMC31XX_DELAY_TM);
		break;
	case MMC31XX_IOC_SET:
		data[0] = MMC31XX_REG_CTRL;
		data[1] = MMC31XX_CTRL_SET;
		if (mmc31xx_i2c_tx_data(data, 2) < 0) {
			return -EFAULT;
		}
		/* wait external capacitor charging done for next SET/RESET */
		msleep(MMC31XX_DELAY_SET);
		break;
	case MMC31XX_IOC_RESET:
		data[0] = MMC31XX_REG_CTRL;
		data[1] = MMC31XX_CTRL_RST;
		if (mmc31xx_i2c_tx_data(data, 2) < 0) {
			return -EFAULT;
		}
		/* wait external capacitor charging done for next SET/RESET */
		msleep(MMC31XX_DELAY_RST);
		break;
	case MMC31XX_IOC_READ:
		data[0] = MMC31XX_REG_DATA;
		if (mmc31xx_i2c_rx_data(data, 6) < 0) {
			return -EFAULT;
		}
		vec[0] = data[0] << 8 | data[1];
		vec[1] = data[2] << 8 | data[3];
		vec[2] = data[4] << 8 | data[5];
	#if DEBUG
		printk("[X - %04x] [Y - %04x] [Z - %04x]\n", 
			vec[0], vec[1], vec[2]);
	#endif
		if (copy_to_user(pa, vec, sizeof(vec))) {
			return -EFAULT;
		}
		break;
	case MMC31XX_IOC_READXYZ:
		/* do RESET/SET every MMC31XX_RESET_INTV times read */
		if (!(read_idx % MMC31XX_RESET_INTV)) {
			/* RESET */
			data[0] = MMC31XX_REG_CTRL;
			data[1] = MMC31XX_CTRL_RST;
			/* not check return value here, assume it always OK */
			mmc31xx_i2c_tx_data(data, 2);
			/* wait external capacitor charging done for next SET/RESET */
			msleep(MMC31XX_DELAY_SET);
			/* SET */
			data[0] = MMC31XX_REG_CTRL;
			data[1] = MMC31XX_CTRL_SET;
			/* not check return value here, assume it always OK */
			mmc31xx_i2c_tx_data(data, 2);
			msleep(MMC31XX_DELAY_STDN);
		}
		/* send TM cmd before read */
		data[0] = MMC31XX_REG_CTRL;
		data[1] = MMC31XX_CTRL_TM;
		/* not check return value here, assume it always OK */
		mmc31xx_i2c_tx_data(data, 2);
		/* wait TM done for coming data read */
		msleep(MMC31XX_DELAY_TM);
		/* read xyz raw data */
		read_idx++;
		data[0] = MMC31XX_REG_DATA;
		if (mmc31xx_i2c_rx_data(data, 6) < 0) {
			return -EFAULT;
		}
		vec[0] = data[0] << 8 | data[1];
		vec[1] = data[2] << 8 | data[3];
		vec[2] = data[4] << 8 | data[5];
	#if DEBUG
		printk("[X - %04x] [Y - %04x] [Z - %04x]\n", 
			vec[0], vec[1], vec[2]);
	#endif
		if (copy_to_user(pa, vec, sizeof(vec))) {
			return -EFAULT;
		}
		break;
	default:
		break;
	}

	return 0;
}

static ssize_t mmc31xx_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	sprintf(buf, "MMC31XX");
	ret = strlen(buf) + 1;

	return ret;
}

static DEVICE_ATTR(mmc31xx, S_IRUGO, mmc31xx_show, NULL);

static struct file_operations mmc31xx_fops = {
	.owner		= THIS_MODULE,
	.open		= mmc31xx_open,
	.release	= mmc31xx_release,
	.ioctl		= mmc31xx_ioctl,
};

static struct miscdevice mmc31xx_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = MMC31XX_DEV_NAME,
	.fops = &mmc31xx_fops,
};

static int mmc31xx_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	unsigned char data[16] = {0};
	int res = 0;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("%s: functionality check failed\n", __FUNCTION__);
		res = -ENODEV;
		goto out;
	}
	this_client = client;

	res = misc_register(&mmc31xx_device);
	if (res) {
		pr_err("%s: mmc31xx_device register failed\n", __FUNCTION__);
		goto out;
	}
	res = device_create_file(&client->dev, &dev_attr_mmc31xx);
	if (res) {
		pr_err("%s: device_create_file failed\n", __FUNCTION__);
		goto out_deregister;
	}

	/* send ST cmd to mag sensor first of all */
	data[0] = MMC31XX_REG_CTRL;
	data[1] = MMC31XX_CTRL_SET;
	if (mmc31xx_i2c_tx_data(data, 2) < 0) {
		/* assume SET always success */
	}
	/* wait external capacitor charging done for next SET/RESET */
	msleep(MMC31XX_DELAY_SET);

	return 0;

out_deregister:
	misc_deregister(&mmc31xx_device);
out:
	return res;
}

static int mmc31xx_remove(struct i2c_client *client)
{
	device_remove_file(&client->dev, &dev_attr_mmc31xx);
	misc_deregister(&mmc31xx_device);

	return 0;
}
static int mmc31xx_detect(struct i2c_client *client, int kind, struct i2c_board_info *info) 
{    
	strcpy(info->type, MMC31XX_DEV_NAME);
	return 0;
}
/*----------------------------------------------------------------------------*/
static const struct i2c_device_id mmc31xx_id[] = {
	{ MMC31XX_I2C_NAME, 0 },
	{ }
};


/*----------------------------------------------------------------------------*/
static struct i2c_driver mmc31xx_i2c_driver = {
    .driver = {
        .owner = THIS_MODULE, 
        .name  = MMC31XX_DEV_NAME,
    },
	.probe      = mmc31xx_probe,
	.remove     = mmc31xx_remove,
	.detect     = mmc31xx_detect,
#if 0
	.suspend    = mmc31xx_suspend,
	.resume     = mmc31xx_resume,
#endif 
	.id_table = mmc31xx_id,
	.address_data = &mmc31xx_addr_data,
};

static int mmc31xx_platform_probe(struct platform_device *pdev) 
{
	if(i2c_add_driver(&mmc31xx_i2c_driver))
	{
		pr_info("add driver error\n");
		return -1;
	} 
	return 0;
}

static int mmc31xx_platform_remove(struct platform_device *pdev)
{
	i2c_del_driver(&mmc31xx_i2c_driver);
	return 0;
}
/*----------------------------------------------------------------------------*/


/*
static struct i2c_driver mmc31xx_driver = {
	.probe 		= mmc31xx_probe,
	.remove 	= mmc31xx_remove,
	.id_table	= mmc31xx_id,
	.driver 	= {
		.owner	= THIS_MODULE,
		.name	= MMC31XX_I2C_NAME,
	},
};
*/
/*----------------------------------------------------------------------------*/
static struct platform_driver mmc31xx_sensor_driver = {
	.probe      = mmc31xx_platform_probe,
	.remove     = mmc31xx_platform_remove,    
	.driver     = {
		.name  = "mmc31xx",
		.owner = THIS_MODULE,
	}
};

static int __init mmc31xx_init(void)
{
	pr_info("mmc31xx driver: init\n");
	
	if(platform_driver_register(&mmc31xx_sensor_driver))
	{
		pr_info("failed to register driver");
		return 0;
	}
	return 0;   
	//return i2c_add_driver(&mmc31xx_driver);
}

static void __exit mmc31xx_exit(void)
{
	pr_info("mmc31xx driver: exit\n");
	
	platform_driver_unregister(&mmc31xx_sensor_driver);
	


	//i2c_del_driver(&mmc31xx_driver);
}

module_init(mmc31xx_init);
module_exit(mmc31xx_exit);

MODULE_AUTHOR("Robbie Cao<hjcao@memsic.com>");
MODULE_DESCRIPTION("MEMSIC MMC31XX Magnetic Sensor Driver");
MODULE_LICENSE("GPL");

