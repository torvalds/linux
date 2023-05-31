// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2023 Rockchip Electronics Co. Ltd.
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/notifier.h>
#include <linux/fb.h>
#include <linux/regmap.h>
#include <linux/clk.h>
#include "lt7911d-fw.h"

struct lt7911d {
	struct device *dev;
	struct regmap *regmap;
	struct serdes_init_seq *serdes_init_seq;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;
	struct notifier_block fb_notif;
	int fb_blank;
};

static int Datalen = 17594;
/*to save hdcp key */
static unsigned char HdcpKey[286];
/*the buffer to read flash, its size should be equal the size of bin, max size is 24KB*/
static unsigned char ReadFirmware[17594];
/*The buffer to read flash, hex->bin->txt*/
//static unsigned char FirmwareData[17594];

static int I2C_Write_Byte(struct lt7911d *lt7911d, unsigned char reg, unsigned char val)
{
	int ret;

	ret = regmap_write(lt7911d->regmap, reg, val);
	if (ret < 0) {
		pr_info("failed to write lt7911d register 0x%x: %d\n", reg, ret);
		return ret;
	}
	return 0;
}

static unsigned char I2C_Read_Byte(struct lt7911d *lt7911d, unsigned char reg)
{
	int ret;
	unsigned int val;

	ret = regmap_read(lt7911d->regmap, reg, &val);
	if (ret < 0) {
		pr_info("failed to read lt7911d register 0x%x: %d\n", reg, ret);
		return ret;
	}

	return (unsigned char)val;
}

static bool lt7911d_check_chip_id(struct lt7911d *lt7911d)
{
	unsigned char id_h, id_l;

	/*0x80ee=0x01 to enable i2c interface*/
	I2C_Write_Byte(lt7911d, 0xFF, 0x80);
	I2C_Write_Byte(lt7911d, 0xEE, 0x01);
	/*write bank 0xa0, read 0xa000 and 0xa001*/
	I2C_Write_Byte(lt7911d, 0xFF, 0xA0);
	id_h = I2C_Read_Byte(lt7911d, 0x00);
	id_l = I2C_Read_Byte(lt7911d, 0x01);

	/*chip id=0x1605*/
	if ((id_h == 0x16) && (id_l == 0x05)) {
		pr_info("%s chip id =0x1605\n", __func__);
		/*0x80ee=0x00 to disable i2c*/
		I2C_Write_Byte(lt7911d, 0xFF, 0x80);
		I2C_Write_Byte(lt7911d, 0xEE, 0x00);
		return true;
	} else {
		pr_info("%s chip id 0x%x is not 0x1605\n", __func__, (id_h << 8) | id_l);
		/*0x80ee=0x00 to disable i2c*/
		I2C_Write_Byte(lt7911d, 0xFF, 0x80);
		I2C_Write_Byte(lt7911d, 0xEE, 0x00);
		return false;
	}
}

static int lt7911d_check_fw_version(struct lt7911d *lt7911d)
{
	unsigned char fw;

	I2C_Write_Byte(lt7911d, 0xFF, 0x80);
	I2C_Write_Byte(lt7911d, 0xEE, 0x01);

	/*read 0xD211*/
	I2C_Write_Byte(lt7911d, 0xFF, 0xD2);
	fw = I2C_Read_Byte(lt7911d, 0x11);

	/*fw version address is 0x1dfb*/
	if (fw < FirmwareData[0x1dfb]) {
		pr_info("%s fw %d<%d, need to upgrade\n", __func__, fw, FirmwareData[0x1dfb]);
		I2C_Write_Byte(lt7911d, 0xFF, 0x80);
		I2C_Write_Byte(lt7911d, 0xEE, 0x00);
		return 0;
	} else {
		pr_info("%s fw %d>=%d, no need upgrade\n", __func__, fw, FirmwareData[0x1dfb]);
		I2C_Write_Byte(lt7911d, 0xFF, 0x80);
		I2C_Write_Byte(lt7911d, 0xEE, 0x00);
		return -1;
	}
}

static void lt7911d_config_para(struct lt7911d *lt7911d)
{
	I2C_Write_Byte(lt7911d, 0xFF, 0x80);
	I2C_Write_Byte(lt7911d, 0xEE, 0x01);
	I2C_Write_Byte(lt7911d, 0x5A, 0x82);
	I2C_Write_Byte(lt7911d, 0x5E, 0xC0);
	I2C_Write_Byte(lt7911d, 0x58, 0x00);
	I2C_Write_Byte(lt7911d, 0x59, 0x51);
	I2C_Write_Byte(lt7911d, 0x5A, 0x92);
	I2C_Write_Byte(lt7911d, 0x5A, 0x82);
}

static void lt7911d_block_erase(struct lt7911d *lt7911d)
{
	I2C_Write_Byte(lt7911d, 0xFF, 0x80);
	I2C_Write_Byte(lt7911d, 0xEE, 0x01);
	I2C_Write_Byte(lt7911d, 0x5A, 0x86);
	I2C_Write_Byte(lt7911d, 0x5A, 0x82);
	I2C_Write_Byte(lt7911d, 0x5B, 0x00);
	I2C_Write_Byte(lt7911d, 0x5C, 0x00);
	I2C_Write_Byte(lt7911d, 0x5D, 0x00);
	I2C_Write_Byte(lt7911d, 0x5A, 0x83);
	I2C_Write_Byte(lt7911d, 0x5A, 0x82);

	/*The time to waiting for earse flash*/
	msleep(500);
}

/*If earse flash will erase the hdcp key, so need to backup firstly*/
static void SaveHdcpKeyFromFlash(struct lt7911d *lt7911d)
{
	unsigned int StartAddr;
	unsigned int npage, i, j;
	unsigned char npagelen = 0;
	unsigned char addr[3] = {0};

	I2C_Write_Byte(lt7911d, 0xFF, 0x80);
	I2C_Write_Byte(lt7911d, 0xEE, 0x01);
	I2C_Write_Byte(lt7911d, 0xFF, 0x90);
	I2C_Write_Byte(lt7911d, 0x02, 0xdf);
	I2C_Write_Byte(lt7911d, 0x02, 0xff);
	I2C_Write_Byte(lt7911d, 0xFF, 0x80);
	I2C_Write_Byte(lt7911d, 0x5a, 0x86);
	I2C_Write_Byte(lt7911d, 0x5a, 0x82);

	/*The first address of HDCP KEY*/
	StartAddr = 0x006000;
	addr[0] = (StartAddr & 0xFF0000) >> 16;
	addr[1] = (StartAddr & 0xFF00) >> 8;
	addr[2] = StartAddr & 0xFF;

	/*hdcp key size is 286 byte*/
	npage = 18;
	npagelen = 16;

	for (i = 0; i < npage; i++) {
		I2C_Write_Byte(lt7911d, 0x5E, 0x6f);
		I2C_Write_Byte(lt7911d, 0x5A, 0xA2);
		I2C_Write_Byte(lt7911d, 0x5A, 0x82);
		I2C_Write_Byte(lt7911d, 0x5B, addr[0]);
		I2C_Write_Byte(lt7911d, 0x5C, addr[1]);
		I2C_Write_Byte(lt7911d, 0x5D, addr[2]);
		I2C_Write_Byte(lt7911d, 0x5A, 0x92);
		I2C_Write_Byte(lt7911d, 0x5A, 0x82);
		I2C_Write_Byte(lt7911d, 0x58, 0x01);

		if (i == 17)
			npagelen = 14;

		for (j = 0; j < npagelen; j++)
			HdcpKey[i * 16 + j] = I2C_Read_Byte(lt7911d, 0x5F);

		StartAddr += 16;
		addr[0] = (StartAddr & 0xFF0000) >> 16;
		addr[1] = (StartAddr & 0xFF00) >> 8;
		addr[2] = StartAddr & 0xFF;
	}

	I2C_Write_Byte(lt7911d, 0x5a, 0x8a);
	I2C_Write_Byte(lt7911d, 0x5a, 0x82);
}

static void lt7911d_write_firmware_to_flash(struct lt7911d *lt7911d)
{
	unsigned int StartAddr;
	unsigned int npage, i, j;
	unsigned char npagelen = 0;
	unsigned char addr[3] = {0};

	I2C_Write_Byte(lt7911d, 0xFF, 0x80);
	I2C_Write_Byte(lt7911d, 0xEE, 0x01);
	I2C_Write_Byte(lt7911d, 0xFF, 0x90);
	I2C_Write_Byte(lt7911d, 0x02, 0xdf);
	I2C_Write_Byte(lt7911d, 0x02, 0xff);
	I2C_Write_Byte(lt7911d, 0xFF, 0x80);
	I2C_Write_Byte(lt7911d, 0x5a, 0x86);
	I2C_Write_Byte(lt7911d, 0x5a, 0x82);

	/*The first address of flash£¬Max Size 24K*/
	StartAddr = 0x000000;
	addr[0] = (StartAddr & 0xFF0000) >> 16;
	addr[1] = (StartAddr & 0xFF00) >> 8;
	addr[2] = StartAddr & 0xFF;

	if (Datalen % 16) {
		/*Datalen is the length of the firmware.*/
		npage = Datalen / 16 + 1;
	} else {
		npage = Datalen / 16;
	}
	npagelen = 16;

	for (i = 0; i < npage; i++) {
		I2C_Write_Byte(lt7911d, 0x5A, 0x86);
		I2C_Write_Byte(lt7911d, 0x5A, 0x82);

		I2C_Write_Byte(lt7911d, 0x5E, 0xef);
		I2C_Write_Byte(lt7911d, 0x5A, 0xA2);
		I2C_Write_Byte(lt7911d, 0x5A, 0x82);
		I2C_Write_Byte(lt7911d, 0x58, 0x01);

		if ((Datalen - i * 16) < 16)
			npagelen = Datalen - i*16;

		for (j = 0; j < npagelen; j++) {
			/*please just continue to write data to 0x59,*/
			/*and lt7911d will increase the address auto use 0xff*/
			/*as insufficient data if datelen%16 is not zero*/
			I2C_Write_Byte(lt7911d, 0x59, FirmwareData[i*16 + j]);
		}

		/*change the first address*/
		I2C_Write_Byte(lt7911d, 0x5B, addr[0]);
		I2C_Write_Byte(lt7911d, 0x5C, addr[1]);
		I2C_Write_Byte(lt7911d, 0x5D, addr[2]);
		I2C_Write_Byte(lt7911d, 0x5E, 0xE0);
		I2C_Write_Byte(lt7911d, 0x5A, 0x92);
		I2C_Write_Byte(lt7911d, 0x5A, 0x82);

		StartAddr += 16;
		addr[0] = (StartAddr & 0xFF0000) >> 16;
		addr[1] = (StartAddr & 0xFF00) >> 8;
		addr[2] = StartAddr & 0xFF;
	}

	I2C_Write_Byte(lt7911d, 0x5a, 0x8a);
	I2C_Write_Byte(lt7911d, 0x5a, 0x82);

	/*reset fifo*/
	I2C_Write_Byte(lt7911d, 0xFF, 0x90);
	I2C_Write_Byte(lt7911d, 0x02, 0xDF);
	I2C_Write_Byte(lt7911d, 0x02, 0xFF);
	msleep(20);
}

static void lt7911d_write_hdcpkey_to_flash(struct lt7911d *lt7911d)
{
	unsigned int StartAddr;
	unsigned int npage, i, j;
	unsigned char npagelen = 0;
	unsigned char addr[3] = {0};

	I2C_Write_Byte(lt7911d, 0xFF, 0x80);
	I2C_Write_Byte(lt7911d, 0xEE, 0x01);
	I2C_Write_Byte(lt7911d, 0xFF, 0x90);
	I2C_Write_Byte(lt7911d, 0x02, 0xdf);
	I2C_Write_Byte(lt7911d, 0x02, 0xff);
	I2C_Write_Byte(lt7911d, 0xFF, 0x80);
	I2C_Write_Byte(lt7911d, 0x5a, 0x86);
	I2C_Write_Byte(lt7911d, 0x5a, 0x82);

	/*hdcp key first address*/
	StartAddr = 0x006000;
	addr[0] = (StartAddr & 0xFF0000) >> 16;
	addr[1] = (StartAddr & 0xFF00) >> 8;
	addr[2] = StartAddr & 0xFF;

	npage = 18;
	npagelen = 16;

	for (i = 0; i < npage; i++) {
		I2C_Write_Byte(lt7911d, 0x5A, 0x86);
		I2C_Write_Byte(lt7911d, 0x5A, 0x82);

		I2C_Write_Byte(lt7911d, 0x5E, 0xef);
		I2C_Write_Byte(lt7911d, 0x5A, 0xA2);
		I2C_Write_Byte(lt7911d, 0x5A, 0x82);
		I2C_Write_Byte(lt7911d, 0x58, 0x01);

		if (i == 17)
			npagelen = 14;

		for (j = 0; j < npagelen; j++) {
			/*please just continue to write data to 0x59,*/
			/*and lt7911d will increase the address auto use 0xff*/
			/*as insufficient data if datelen%16 is not zero .*/
			I2C_Write_Byte(lt7911d, 0x59, HdcpKey[i*16 + j]);
		}

		if (npagelen == 14) {
			I2C_Write_Byte(lt7911d, 0x59, 0xFF);
			I2C_Write_Byte(lt7911d, 0x59, 0xFF);
		}

		/*change the first address*/
		I2C_Write_Byte(lt7911d, 0x5B, addr[0]);
		I2C_Write_Byte(lt7911d, 0x5C, addr[1]);
		I2C_Write_Byte(lt7911d, 0x5D, addr[2]);
		I2C_Write_Byte(lt7911d, 0x5E, 0xE0);
		I2C_Write_Byte(lt7911d, 0x5A, 0x92);
		I2C_Write_Byte(lt7911d, 0x5A, 0x82);

		StartAddr += 16;
		addr[0] = (StartAddr & 0xFF0000) >> 16;
		addr[1] = (StartAddr & 0xFF00) >> 8;
		addr[2] = StartAddr & 0xFF;
	}

	I2C_Write_Byte(lt7911d, 0x5a, 0x8a);
	I2C_Write_Byte(lt7911d, 0x5a, 0x82);

	/*reset fifo*/
	I2C_Write_Byte(lt7911d, 0xFF, 0x90);
	I2C_Write_Byte(lt7911d, 0x02, 0xDF);
	I2C_Write_Byte(lt7911d, 0x02, 0xFF);
	msleep(20);
}

static void lt7911d_read_firmware_from_flash(struct lt7911d *lt7911d)
{
	unsigned int StartAddr;
	unsigned int npage, i, j;
	unsigned char npagelen = 0;
	unsigned char addr[3] = {0};

	memset(ReadFirmware, 0, sizeof(ReadFirmware));

	I2C_Write_Byte(lt7911d, 0xFF, 0x80);
	I2C_Write_Byte(lt7911d, 0xEE, 0x01);
	I2C_Write_Byte(lt7911d, 0xFF, 0x90);
	I2C_Write_Byte(lt7911d, 0x02, 0xdf);
	I2C_Write_Byte(lt7911d, 0x02, 0xff);
	I2C_Write_Byte(lt7911d, 0xFF, 0x80);
	I2C_Write_Byte(lt7911d, 0x5a, 0x86);
	I2C_Write_Byte(lt7911d, 0x5a, 0x82);

	/*the first address of firmware*/
	StartAddr = 0x000000;
	addr[0] = (StartAddr & 0xFF0000) >> 16;
	addr[1] = (StartAddr & 0xFF00) >> 8;
	addr[2] = StartAddr & 0xFF;

	if (Datalen % 16)
		npage = Datalen / 16 + 1;
	else
		npage = Datalen / 16;

	npagelen = 16;

	for (i = 0; i < npage; i++) {
		I2C_Write_Byte(lt7911d, 0x5E, 0x6f);
		I2C_Write_Byte(lt7911d, 0x5A, 0xA2);
		I2C_Write_Byte(lt7911d, 0x5A, 0x82);
		I2C_Write_Byte(lt7911d, 0x5B, addr[0]);
		I2C_Write_Byte(lt7911d, 0x5C, addr[1]);
		I2C_Write_Byte(lt7911d, 0x5D, addr[2]);
		I2C_Write_Byte(lt7911d, 0x5A, 0x92);
		I2C_Write_Byte(lt7911d, 0x5A, 0x82);
		I2C_Write_Byte(lt7911d, 0x58, 0x01);

		if ((Datalen - i * 16) < 16)
			npagelen = Datalen - i*16;

		for (j = 0; j < npagelen; j++) {
			/*please just continue to read data from 0x5f*/
			/*lt7911d will increase the address auto*/
			ReadFirmware[i*16 + j] = I2C_Read_Byte(lt7911d, 0x5F);
		}

		StartAddr += 16;
		/*change the first address*/
		addr[0] = (StartAddr & 0xFF0000) >> 16;
		addr[1] = (StartAddr & 0xFF00) >> 8;
		addr[2] = StartAddr & 0xFF;
	}

	I2C_Write_Byte(lt7911d, 0x5a, 0x8a);
	I2C_Write_Byte(lt7911d, 0x5a, 0x82);
}

static int lt7911_compare_firmware(struct lt7911d *lt7911d)
{
	unsigned int len;

	for (len = 0; len < Datalen; len++) {
		if (ReadFirmware[len] != FirmwareData[len]) {
			pr_info("%s: ReadFirmware[%d] 0x%x !=  0x%x FirmwareData[%d]\n",
					__func__, len, ReadFirmware[len], FirmwareData[len], len);
			return -1;
		}
	}
	return 0;
}

static int lt7911d_firmware_upgrade(struct lt7911d *lt7911d)
{
	int ret = 0;

	if (lt7911d_check_chip_id(lt7911d)) {
		if (lt7911d_check_fw_version(lt7911d) == 0) {
			lt7911d_config_para(lt7911d);
			SaveHdcpKeyFromFlash(lt7911d);
			lt7911d_block_erase(lt7911d);
			lt7911d_write_firmware_to_flash(lt7911d);
			lt7911d_write_hdcpkey_to_flash(lt7911d);
			lt7911d_read_firmware_from_flash(lt7911d);

			if (!lt7911_compare_firmware(lt7911d)) {
				pr_info("%s: upgrade success\n", __func__);
				ret = 0;
			} else {
				pr_info("%s: upgrade Fail\n", __func__);
				ret = -1;
			}
		}
	} else {
		pr_info("the chip lt7911d is offline\n");
		ret = 0;
	}

	I2C_Write_Byte(lt7911d, 0xFF, 0x80);
	I2C_Write_Byte(lt7911d, 0xEE, 0x00);

	return ret;
}

static const struct regmap_config lt7911d_regmap_config = {
	.name = "lt7911d",
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x100,
};

static int lt7911d_fb_notifier_callback(struct notifier_block *self,
					unsigned long event, void *data)
{
	struct lt7911d *lt7911d = container_of(self, struct lt7911d, fb_notif);
	struct fb_event *evdata = data;
	int fb_blank = *(int *)evdata->data;

	if (event != FB_EVENT_BLANK)
		return 0;

	if (lt7911d->fb_blank == fb_blank)
		return 0;

	if (fb_blank == FB_BLANK_UNBLANK) {
		if (lt7911d->reset_gpio) {
			gpiod_direction_output(lt7911d->reset_gpio, 1);
			msleep(20);
			gpiod_direction_output(lt7911d->reset_gpio, 0);
			msleep(400);
		}
	}

	lt7911d->fb_blank = fb_blank;

	return 0;
}

static int lt7911d_i2c_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct lt7911d *lt7911d;
	int ret = 0, i = 0;

	lt7911d = devm_kzalloc(dev, sizeof(*lt7911d), GFP_KERNEL);
	if (!lt7911d)
		return -ENOMEM;

	lt7911d->dev = dev;
	i2c_set_clientdata(client, lt7911d);

	lt7911d->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(lt7911d->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(lt7911d->reset_gpio),
				     "failed to acquire reset gpio\n");

	gpiod_set_consumer_name(lt7911d->reset_gpio, "lt7911d-reset");

	lt7911d->enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(lt7911d->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(lt7911d->enable_gpio),
				     "failed to acquire enable gpio\n");

	lt7911d->regmap = devm_regmap_init_i2c(client, &lt7911d_regmap_config);
	if (IS_ERR(lt7911d->regmap))
		return dev_err_probe(dev, PTR_ERR(lt7911d->regmap),
				     "failed to initialize regmap\n");

	lt7911d->fb_blank = FB_BLANK_UNBLANK;
	lt7911d->fb_notif.notifier_call = lt7911d_fb_notifier_callback;
	ret = fb_register_client(&lt7911d->fb_notif);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register fb client\n");

	for (i = 0; i < 3; i++) {
		if (!lt7911d_firmware_upgrade(lt7911d))
			break;
	}

	dev_info(dev, "%s end\n", __func__);

	return 0;
}

static void lt7911d_i2c_shutdown(struct i2c_client *client)
{
	struct lt7911d *lt7911d = i2c_get_clientdata(client);

	gpiod_direction_output(lt7911d->reset_gpio, 1);
	msleep(20);
}

static int lt7911d_i2c_remove(struct i2c_client *client)
{
	struct lt7911d *lt7911d = i2c_get_clientdata(client);

	fb_unregister_client(&lt7911d->fb_notif);

	return 0;
}

static const struct i2c_device_id lt7911d_i2c_table[] = {
	{ "lt7911d", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, lt7911d_i2c_table);

static const struct of_device_id lt7911d_of_match[] = {
	{ .compatible = "lontium,lt7911d-fb-notifier" },
	{}
};
MODULE_DEVICE_TABLE(of, lt7911d_of_match);

static struct i2c_driver lt7911d_i2c_driver = {
	.driver = {
		.name = "lt7911d",
		.of_match_table = lt7911d_of_match,
	},
	.probe = lt7911d_i2c_probe,
	.remove = lt7911d_i2c_remove,
	.shutdown = lt7911d_i2c_shutdown,
	.id_table = lt7911d_i2c_table,
};

static int __init lt7911d_i2c_driver_init(void)
{
	i2c_add_driver(&lt7911d_i2c_driver);

	return 0;
}
subsys_initcall_sync(lt7911d_i2c_driver_init);

static void __exit lt7911d_i2c_driver_exit(void)
{
	i2c_del_driver(&lt7911d_i2c_driver);
}
module_exit(lt7911d_i2c_driver_exit);

MODULE_DESCRIPTION("Lontium lt7911dD driver");
MODULE_LICENSE("GPL");
