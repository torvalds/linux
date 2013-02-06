/*
 * driver/barcode_emul Barcode emulator driver
 *
 * Copyright (C) 2012 Samsung Electronics
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/earlysuspend.h>
#include <linux/spinlock.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include "barcode_emul_ice4.h"

#if defined(TEST_DEBUG)
#define pr_barcode	pr_emerg
#else
#define pr_barcode	pr_info
#endif

#define LOOP_BACK	48
#define TEST_CODE1	49
#define TEST_CODE2	50
#define FW_VER_ADDR	0x80
#define BEAM_STATUS_ADDR	0xFE
#define BARCODE_EMUL_MAX_FW_PATH	255
#define BARCODE_EMUL_FW_FILENAME		"i2c_top_bitmap.bin"

struct barcode_emul_data {
	struct i2c_client		*client;
};

static struct workqueue_struct *barcode_init_wq;
static void barcode_init_workfunc(struct work_struct *work);
static DECLARE_WORK(barcode_init_work, barcode_init_workfunc);
/*
 * Send barcode emulator firmware data thougth spi communication
 */
static int barcode_send_firmware_data(unsigned char *data)
{
	unsigned int i,j;	
	unsigned char spibit;

	i=0;
	while (i < CONFIGURATION_SIZE) {
		j=0;
		spibit = data[i];
		while (j < 8) {
			gpio_set_value(GPIO_FPGA_SPI_CLK,GPIO_LEVEL_LOW);

			if (spibit & 0x80) 
			{
				gpio_set_value(GPIO_FPGA_SPI_SI,GPIO_LEVEL_HIGH);
			}
			else
			{
				gpio_set_value(GPIO_FPGA_SPI_SI,GPIO_LEVEL_LOW);
			}
			
			j = j+1;
			gpio_set_value(GPIO_FPGA_SPI_CLK,GPIO_LEVEL_HIGH);
			spibit = spibit<<1;
		}
		i = i+1;
	}

	i = 0;
	while (i < 200) {
		gpio_set_value(GPIO_FPGA_SPI_CLK,GPIO_LEVEL_LOW);
		i = i+1;
		gpio_set_value(GPIO_FPGA_SPI_CLK,GPIO_LEVEL_HIGH);
	}    
	return 0;
}

static int check_fpga_cdone(void)
{
    /* Device in Operation when CDONE='1'; Device Failed when CDONE='0'. */
	if (gpio_get_value(GPIO_FPGA_CDONE) != 1) {
		pr_barcode("CDONE_FAIL\n");
		return 0;
	} else {
		pr_barcode("CDONE_SUCCESS\n");
		return 1;
	}
}

static int barcode_fpga_fimrware_update_start(unsigned char *data)
{
	pr_barcode("%s\n", __func__);

	gpio_request_one(GPIO_FPGA_CDONE, GPIOF_IN, "FPGA_CDONE");
	gpio_request_one(GPIO_FPGA_SPI_CLK, GPIOF_OUT_INIT_LOW, "FPGA_SPI_CLK");
	gpio_request_one(GPIO_FPGA_SPI_SI, GPIOF_OUT_INIT_LOW, "FPGA_SPI_SI");
	gpio_request_one(GPIO_FPGA_SPI_EN, GPIOF_OUT_INIT_LOW, "FPGA_SPI_EN");
	gpio_request_one(GPIO_FPGA_CRESET_B, GPIOF_OUT_INIT_HIGH, "FPGA_CRESET_B");
	gpio_request_one(GPIO_FPGA_RST_N, GPIOF_OUT_INIT_LOW, "FPGA_RST_N");

	gpio_set_value(GPIO_FPGA_CRESET_B, GPIO_LEVEL_LOW);
	udelay(30);
	    
	gpio_set_value(GPIO_FPGA_CRESET_B, GPIO_LEVEL_HIGH);
	udelay(1000);

	check_fpga_cdone();

	barcode_send_firmware_data(data);
	udelay(50);

	if (check_fpga_cdone()) {
		udelay(5);
		gpio_set_value(GPIO_FPGA_RST_N, GPIO_LEVEL_HIGH);
		pr_barcode("barcode firmware update success\n");
	} else {
		pr_barcode("barcode firmware update fail\n");
	}
	return 0;
}

void barcode_fpga_firmware_update(void)
{
	barcode_fpga_fimrware_update_start(spiword);
}

static ssize_t barcode_emul_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	int ret;
	struct barcode_emul_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	if (gpio_get_value(GPIO_FPGA_CDONE) != 1) {
		pr_err("%s: cdone fail !!\n", __func__);
		return 0;
	}

	ret = i2c_master_send(client, buf, size);
	if (ret < 0) {
		pr_err("%s: i2c err1 %d\n", __func__, ret);
		ret = i2c_master_send(client, buf, size);
		if (ret < 0)
			pr_err("%s: i2c err2 %d\n", __func__, ret);
	}

	return size;
}

static ssize_t barcode_emul_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return strlen(buf);
}
static DEVICE_ATTR(barcode_send, 0664, barcode_emul_show, barcode_emul_store);

static ssize_t barcode_emul_fw_update_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct file *fp = NULL;
	long fsize = 0, nread = 0;
	const u8 *buff = 0;
	char fw_path[BARCODE_EMUL_MAX_FW_PATH+1];
	mm_segment_t old_fs = get_fs();

	pr_barcode("%s\n", __func__);

	old_fs = get_fs();
	set_fs(get_ds());

	snprintf(fw_path, BARCODE_EMUL_MAX_FW_PATH,
		"/sdcard/%s", BARCODE_EMUL_FW_FILENAME);

	fp = filp_open(fw_path, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		pr_err("file %s open error:%d\n",
			fw_path, (s32)fp);
		goto err_open;
	}

	fsize = fp->f_path.dentry->d_inode->i_size;
	pr_barcode("barcode firmware size: %ld\n", fsize);

	buff = kzalloc((size_t)fsize, GFP_KERNEL);
	if (!buff) {
		pr_err("fail to alloc buffer for fw\n");
		goto err_alloc;
	}

	nread = vfs_read(fp, (char __user *)buff, fsize, &fp->f_pos);
	if (nread != fsize) {
		pr_err("fail to read file %s (nread = %ld)\n",
			fw_path, nread);
		goto err_fw_size;
	}

	barcode_fpga_fimrware_update_start((unsigned char *)buff);

	if (fp != NULL) {
	err_fw_size:
		kfree(buff);
	err_alloc:
		filp_close(fp, NULL);
	err_open:
		set_fs(old_fs);
	}

	return size;
}

static ssize_t barcode_emul_fw_update_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return strlen(buf);
}
static DEVICE_ATTR(barcode_fw_update, 0664, barcode_emul_fw_update_show, barcode_emul_fw_update_store);

static int barcode_emul_read(struct i2c_client *client, u16 addr, u8 length, u8 *value)
{

	struct i2c_msg msg[2];
	int ret;

	msg[0].addr  = client->addr;
	msg[0].flags = 0x00;
	msg[0].len   = 1;
	msg[0].buf   = (u8 *) &addr;

	msg[1].addr  = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len   = length;
	msg[1].buf   = (u8 *) value;

	ret = i2c_transfer(client->adapter, msg, 2);
	if  (ret == 2) {
		return 0;
	} else {
		pr_barcode("%s: err1 %d\n", __func__, ret);
		return -EIO;
	}
}

static ssize_t barcode_emul_test_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	int ret, i;
	struct barcode_emul_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	struct {
			unsigned char addr;
			unsigned char data[20];
		} i2c_block_transfer;
	unsigned char barcode_data[14] = {0xFF, 0xAC, 0xDB, 0x36, 0x42, 0x85, 0x0A, 0xA8, 0xD1, 0xA3, 0x46, 0xC5, 0xDA, 0xFF};

	if (gpio_get_value(GPIO_FPGA_CDONE) != 1) {
		pr_err("%s: cdone fail !!\n", __func__);
		return 0;
	}

	if (buf[0] == LOOP_BACK) {
		for (i = 0; i < size; i++)
			i2c_block_transfer.data[i] = *buf++;

		i2c_block_transfer.addr = 0x01;
		
		pr_barcode("%s: write addr: %d, value: %d\n", __func__,
			i2c_block_transfer.addr, i2c_block_transfer.data[0]);

		ret = i2c_master_send(client, (unsigned char *) &i2c_block_transfer, 2);
		if (ret < 0) {
			pr_barcode("%s: err1 %d\n", __func__, ret);
			ret = i2c_master_send(client, (unsigned char *) &i2c_block_transfer, 2);
			if (ret < 0)
				pr_barcode("%s: err2 %d\n", __func__, ret);
		}

	} else if (buf[0] == TEST_CODE1) {
		unsigned char BSR_data[6] =\
			{0xC8, 0x00, 0x32, 0x01, 0x00, 0x32};

		pr_barcode("barcode test code start\n");

		/* send NH */
		i2c_block_transfer.addr = 0x80;
		i2c_block_transfer.data[0] = 0x05;
		ret = i2c_master_send(client, (unsigned char *) &i2c_block_transfer, 2);
		if (ret < 0) {
			pr_err("%s: err1 %d\n", __func__, ret);
			ret = i2c_master_send(client, (unsigned char *) &i2c_block_transfer, 2);
			if (ret < 0)
				pr_err("%s: err2 %d\n", __func__, ret);
		}

		/* setup BSR data */
		for (i = 0; i < 6; i++)
			i2c_block_transfer.data[i+1] = BSR_data[i];

		/* send BSR1 => NS 1= 200, ISD 1 = 100us, IPD 1 = 200us, BL 1=14, BW 1=4MHZ */
		i2c_block_transfer.addr = 0x81;
		i2c_block_transfer.data[0] = 0x00;

		ret = i2c_master_send(client, (unsigned char *) &i2c_block_transfer, 8);
		if (ret < 0) {
			pr_err("%s: err1 %d\n", __func__, ret);
			ret = i2c_master_send(client, (unsigned char *) &i2c_block_transfer, 8);
			if (ret < 0)
				pr_err("%s: err2 %d\n", __func__, ret);
		}

		/* send BSR2 => NS 2= 200, ISD 2 = 100us, IPD 2 = 200us, BL 2=14, BW 2=2MHZ*/
		i2c_block_transfer.addr = 0x88;
		i2c_block_transfer.data[0] = 0x01;

		ret = i2c_master_send(client, (unsigned char *) &i2c_block_transfer, 8);
		if (ret < 0) {
			pr_err("%s: err1 %d\n", __func__, ret);
			ret = i2c_master_send(client, (unsigned char *) &i2c_block_transfer, 8);
			if (ret < 0)
				pr_err("%s: err2 %d\n", __func__, ret);
		}


		/* send BSR3 => NS 3= 200, ISD 3 = 100us, IPD 3 = 200us, BL 3=14, BW 3=1MHZ*/
		i2c_block_transfer.addr = 0x8F;
		i2c_block_transfer.data[0] = 0x02;

		ret = i2c_master_send(client, (unsigned char *) &i2c_block_transfer, 8);
		if (ret < 0) {
			pr_err("%s: err1 %d\n", __func__, ret);
			ret = i2c_master_send(client, (unsigned char *) &i2c_block_transfer, 8);
			if (ret < 0)
				pr_err("%s: err2 %d\n", __func__, ret);
		}

		/* send BSR4 => NS 4= 200, ISD 4 = 100us, IPD 4 = 200us, BL 4=14, BW 4=500KHZ*/
		i2c_block_transfer.addr = 0x96;
		i2c_block_transfer.data[0] = 0x04;

		ret = i2c_master_send(client, (unsigned char *) &i2c_block_transfer, 8);
		if (ret < 0) {
			pr_err("%s: err1 %d\n", __func__, ret);
			ret = i2c_master_send(client, (unsigned char *) &i2c_block_transfer, 8);
			if (ret < 0)
				pr_err("%s: err2 %d\n", __func__, ret);
		}

		/* send BSR5 => NS 5= 200, ISD 5 = 100us, IPD 5 = 200us, BL 5=14, BW 5=250KHZ*/
		i2c_block_transfer.addr = 0x9D;
		i2c_block_transfer.data[0] = 0x08;

		ret = i2c_master_send(client, (unsigned char *) &i2c_block_transfer, 8);
		if (ret < 0) {
			pr_err("%s: err1 %d\n", __func__, ret);
			ret = i2c_master_send(client, (unsigned char *) &i2c_block_transfer, 8);
			if (ret < 0)
				pr_err("%s: err2 %d\n", __func__, ret);
		}

		/* send barcode data */
		i2c_block_transfer.addr = 0x00;
		for (i = 0; i < 14; i++)
			i2c_block_transfer.data[i] = barcode_data[i];

		ret = i2c_master_send(client, (unsigned char *) &i2c_block_transfer, 15);
		if (ret < 0) {
			pr_err("%s: err1 %d\n", __func__, ret);
			ret = i2c_master_send(client, (unsigned char *) &i2c_block_transfer, 15);
			if (ret < 0)
				pr_err("%s: err2 %d\n", __func__, ret);
		}

		/* send START */
		i2c_block_transfer.addr = 0xFF;
		i2c_block_transfer.data[0] = 0x0E;
		ret = i2c_master_send(client, (unsigned char *) &i2c_block_transfer, 2);
		if (ret < 0) {
			pr_err("%s: err1 %d\n", __func__, ret);
			ret = i2c_master_send(client, (unsigned char *) &i2c_block_transfer, 2);
			if (ret < 0)
				pr_err("%s: err2 %d\n", __func__, ret);
		}

	} else if (buf[0] == TEST_CODE2) {
		pr_barcode("barcode test code stop\n");

		i2c_block_transfer.addr = 0xFF;
		i2c_block_transfer.data[0] = 0x00;
		ret = i2c_master_send(client, (unsigned char *) &i2c_block_transfer, 2);
		if (ret < 0) {
			pr_err("%s: err1 %d\n", __func__, ret);
			ret = i2c_master_send(client, (unsigned char *) &i2c_block_transfer, 2);
			if (ret < 0)
				pr_err("%s: err2 %d\n", __func__, ret);
		}
		/*
		pr_barcode("barcode test code send 02\n");

		i2c_block_transfer.addr = 0x00;
		i2c_block_transfer.data[0] = 0x00;
		ret = i2c_master_send(client, (unsigned char *) &i2c_block_transfer, 2);
		if (ret < 0) {
			pr_err("%s: err1 %d\n", __func__, ret);
			ret = i2c_master_send(client, (unsigned char *) &i2c_block_transfer, 2);
			if (ret < 0)
				pr_err("%s: err2 %d\n", __func__, ret);
		}

		i2c_block_transfer.addr = 0x01;
		i2c_block_transfer.data[0] = 0x01;
		ret = i2c_master_send(client, (unsigned char *) &i2c_block_transfer, 2);
		if (ret < 0) {
			pr_err("%s: err1 %d\n", __func__, ret);
			ret = i2c_master_send(client, (unsigned char *) &i2c_block_transfer, 2);
			if (ret < 0)
				pr_err("%s: err2 %d\n", __func__, ret);
		}

		i2c_block_transfer.addr = 0x02;
		for (i = 0; i < 14; i++)
			i2c_block_transfer.data[i] = barcode_data[i];

		ret = i2c_master_send(client, (unsigned char *) &i2c_block_transfer, 15);
		if (ret < 0) {
			pr_err("%s: err1 %d\n", __func__, ret);
			ret = i2c_master_send(client, (unsigned char *) &i2c_block_transfer, 15);
			if (ret < 0)
				pr_err("%s: err2 %d\n", __func__, ret);
		}

		i2c_block_transfer.addr = 0x00;
		i2c_block_transfer.data[0] = 0x01;
		ret = i2c_master_send(client, (unsigned char *) &i2c_block_transfer, 2);
		if (ret < 0) {
			pr_err("%s: err1 %d\n", __func__, ret);
			ret = i2c_master_send(client, (unsigned char *) &i2c_block_transfer, 2);
			if (ret < 0)
				pr_err("%s: err2 %d\n", __func__, ret);
		}
		*/
	}
	return size;
}

static ssize_t barcode_emul_test_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return strlen(buf);
}
static DEVICE_ATTR(barcode_test_send, 0664, barcode_emul_test_show, barcode_emul_test_store);

static ssize_t barcode_ver_check_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct barcode_emul_data *data = dev_get_drvdata(dev);
	u8 fw_ver;

	barcode_emul_read(data->client, FW_VER_ADDR, 1, &fw_ver);
	fw_ver = (fw_ver >> 5) & 0x7;
	return sprintf(buf, "%d\n", fw_ver+14);

}
static DEVICE_ATTR(barcode_ver_check, 0664, barcode_ver_check_show, NULL);

static ssize_t barcode_led_status_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct barcode_emul_data *data = dev_get_drvdata(dev);
	u8 status;

	barcode_emul_read(data->client, BEAM_STATUS_ADDR, 1, &status);
	status = status & 0x1;
	return sprintf(buf, "%d\n", status);

}
static DEVICE_ATTR(barcode_led_status, 0664, barcode_led_status_show, NULL);

static int __devinit barcode_emul_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct barcode_emul_data *data;
	struct device *barcode_emul_dev;
	int error;

	pr_barcode("%s probe!\n", __func__);

	if (gpio_get_value(GPIO_FPGA_CDONE) != 1) {
		pr_err("%s: cdone fail !!\n", __func__);
		return 0;
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C))
		return -EIO;

	data = kzalloc(sizeof(struct barcode_emul_data), GFP_KERNEL);
	if (NULL == data) {
		pr_err("Failed to data allocate %s\n", __func__);
		error = -ENOMEM;
		goto err_free_mem;
	}
	data->client = client;
	i2c_set_clientdata(client, data);
	
	barcode_emul_dev = device_create(sec_class, NULL, 0, data, "sec_barcode_emul");

	if (IS_ERR(barcode_emul_dev))
		pr_err("Failed to create barcode_emul_dev device\n");

	if (device_create_file(barcode_emul_dev, &dev_attr_barcode_send) < 0)
		pr_err("Failed to create device file(%s)!\n",
				dev_attr_barcode_send.attr.name);
	if (device_create_file(barcode_emul_dev, &dev_attr_barcode_test_send) < 0)
		pr_err("Failed to create device file(%s)!\n",
				dev_attr_barcode_test_send.attr.name);
	if (device_create_file(barcode_emul_dev, &dev_attr_barcode_fw_update) < 0)
		pr_err("Failed to create device file(%s)!\n",
				dev_attr_barcode_fw_update.attr.name);
	if (device_create_file(barcode_emul_dev, &dev_attr_barcode_ver_check) < 0)
		pr_err("Failed to create device file(%s)!\n",
				dev_attr_barcode_ver_check.attr.name);
	if (device_create_file(barcode_emul_dev, &dev_attr_barcode_led_status) < 0)
		pr_err("Failed to create device file(%s)!\n",
				dev_attr_barcode_led_status.attr.name);

	return 0;

err_free_mem:
	kfree(data);
	return error;
}

static int __devexit barcode_emul_remove(struct i2c_client *client)
{
	struct barcode_emul_data *data = i2c_get_clientdata(client);

	i2c_set_clientdata(client, NULL);
	kfree(data);
	return 0;
}

#ifdef CONFIG_PM
static int barcode_emul_suspend(struct device *dev)
{
	gpio_set_value(GPIO_FPGA_RST_N, GPIO_LEVEL_LOW);
	return 0;
}

static int barcode_emul_resume(struct device *dev)
{
	gpio_set_value(GPIO_FPGA_RST_N, GPIO_LEVEL_HIGH);
	return 0;
}

static const struct dev_pm_ops barcode_emul_pm_ops = {
	.suspend	= barcode_emul_suspend,
	.resume	= barcode_emul_resume,
};
#endif

static const struct i2c_device_id ice4_id[] = {
	{"ice4", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, ice4_id);

static struct i2c_driver ice4_i2c_driver = {
	.driver = {
		.name = "ice4",
#ifdef CONFIG_PM
		.pm	= &barcode_emul_pm_ops,
#endif
	},
	.probe = barcode_emul_probe,
	.remove = __devexit_p(barcode_emul_remove),
	.id_table = ice4_id,
};

static void barcode_init_workfunc(struct work_struct *work)
{
	barcode_fpga_firmware_update();
	i2c_add_driver(&ice4_i2c_driver);
}

static int __init barcode_emul_init(void)
{
	barcode_init_wq = create_singlethread_workqueue("barcode_init");
	queue_work(barcode_init_wq, &barcode_init_work);

	return 0;
}
late_initcall(barcode_emul_init);

static void __exit barcode_emul_exit(void)
{
	i2c_del_driver(&ice4_i2c_driver);
	destroy_workqueue(barcode_init_wq);
}
module_exit(barcode_emul_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SEC Barcode emulator");
