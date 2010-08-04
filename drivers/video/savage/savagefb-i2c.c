/*
 * linux/drivers/video/savage/savagefb-i2c.c - S3 Savage DDC2
 *
 * Copyright 2004 Antonino A. Daplas <adaplas @pol.net>
 *
 * Based partly on rivafb-i2c.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/gfp.h>
#include <linux/pci.h>
#include <linux/fb.h>

#include <asm/io.h>
#include "savagefb.h"

#define SAVAGE_DDC 	0x50

#define VGA_CR_IX	0x3d4
#define VGA_CR_DATA	0x3d5

#define CR_SERIAL1	0xa0	/* I2C serial communications interface */
#define MM_SERIAL1	0xff20
#define CR_SERIAL2	0xb1	/* DDC2 monitor communications interface */

/* based on vt8365 documentation */
#define PROSAVAGE_I2C_ENAB	0x10
#define PROSAVAGE_I2C_SCL_OUT	0x01
#define PROSAVAGE_I2C_SDA_OUT	0x02
#define PROSAVAGE_I2C_SCL_IN	0x04
#define PROSAVAGE_I2C_SDA_IN	0x08

#define SAVAGE4_I2C_ENAB	0x00000020
#define SAVAGE4_I2C_SCL_OUT	0x00000001
#define SAVAGE4_I2C_SDA_OUT	0x00000002
#define SAVAGE4_I2C_SCL_IN	0x00000008
#define SAVAGE4_I2C_SDA_IN	0x00000010

static void savage4_gpio_setscl(void *data, int val)
{
	struct savagefb_i2c_chan *chan = data;
	unsigned int r;

	r = readl(chan->ioaddr + chan->reg);
	if(val)
		r |= SAVAGE4_I2C_SCL_OUT;
	else
		r &= ~SAVAGE4_I2C_SCL_OUT;
	writel(r, chan->ioaddr + chan->reg);
	readl(chan->ioaddr + chan->reg);	/* flush posted write */
}

static void savage4_gpio_setsda(void *data, int val)
{
	struct savagefb_i2c_chan *chan = data;

	unsigned int r;
	r = readl(chan->ioaddr + chan->reg);
	if(val)
		r |= SAVAGE4_I2C_SDA_OUT;
	else
		r &= ~SAVAGE4_I2C_SDA_OUT;
	writel(r, chan->ioaddr + chan->reg);
	readl(chan->ioaddr + chan->reg);	/* flush posted write */
}

static int savage4_gpio_getscl(void *data)
{
	struct savagefb_i2c_chan *chan = data;

	return (0 != (readl(chan->ioaddr + chan->reg) & SAVAGE4_I2C_SCL_IN));
}

static int savage4_gpio_getsda(void *data)
{
	struct savagefb_i2c_chan *chan = data;

	return (0 != (readl(chan->ioaddr + chan->reg) & SAVAGE4_I2C_SDA_IN));
}

static void prosavage_gpio_setscl(void* data, int val)
{
	struct savagefb_i2c_chan *chan = data;
	u32			  r;

	r = VGArCR(chan->reg, chan->par);
	r |= PROSAVAGE_I2C_ENAB;
	if (val) {
		r |= PROSAVAGE_I2C_SCL_OUT;
	} else {
		r &= ~PROSAVAGE_I2C_SCL_OUT;
	}

	VGAwCR(chan->reg, r, chan->par);
}

static void prosavage_gpio_setsda(void* data, int val)
{
	struct savagefb_i2c_chan *chan = data;
	unsigned int r;

	r = VGArCR(chan->reg, chan->par);
	r |= PROSAVAGE_I2C_ENAB;
	if (val) {
		r |= PROSAVAGE_I2C_SDA_OUT;
	} else {
		r &= ~PROSAVAGE_I2C_SDA_OUT;
	}

	VGAwCR(chan->reg, r, chan->par);
}

static int prosavage_gpio_getscl(void* data)
{
	struct savagefb_i2c_chan *chan = data;

	return (VGArCR(chan->reg, chan->par) & PROSAVAGE_I2C_SCL_IN) ? 1 : 0;
}

static int prosavage_gpio_getsda(void* data)
{
	struct savagefb_i2c_chan *chan = data;

	return (VGArCR(chan->reg, chan->par) & PROSAVAGE_I2C_SDA_IN) ? 1 : 0;
}

static int savage_setup_i2c_bus(struct savagefb_i2c_chan *chan,
				const char *name)
{
	int rc = 0;

	if (chan->par) {
		strcpy(chan->adapter.name, name);
		chan->adapter.owner		= THIS_MODULE;
		chan->adapter.algo_data		= &chan->algo;
		chan->adapter.dev.parent	= &chan->par->pcidev->dev;
		chan->algo.udelay		= 10;
		chan->algo.timeout		= 20;
		chan->algo.data 		= chan;

		i2c_set_adapdata(&chan->adapter, chan);

		/* Raise SCL and SDA */
		chan->algo.setsda(chan, 1);
		chan->algo.setscl(chan, 1);
		udelay(20);

		rc = i2c_bit_add_bus(&chan->adapter);

		if (rc == 0)
			dev_dbg(&chan->par->pcidev->dev,
				"I2C bus %s registered.\n", name);
		else
			dev_warn(&chan->par->pcidev->dev,
				 "Failed to register I2C bus %s.\n", name);
	} else
		chan->par = NULL;

	return rc;
}

void savagefb_create_i2c_busses(struct fb_info *info)
{
	struct savagefb_par *par = info->par;
	par->chan.par	= par;

	switch(info->fix.accel) {
	case FB_ACCEL_PROSAVAGE_DDRK:
	case FB_ACCEL_PROSAVAGE_PM:
		par->chan.reg         = CR_SERIAL2;
		par->chan.ioaddr      = par->mmio.vbase;
		par->chan.algo.setsda = prosavage_gpio_setsda;
		par->chan.algo.setscl = prosavage_gpio_setscl;
		par->chan.algo.getsda = prosavage_gpio_getsda;
		par->chan.algo.getscl = prosavage_gpio_getscl;
		break;
	case FB_ACCEL_SAVAGE4:
	case FB_ACCEL_SAVAGE2000:
		par->chan.reg         = 0xff20;
		par->chan.ioaddr      = par->mmio.vbase;
		par->chan.algo.setsda = savage4_gpio_setsda;
		par->chan.algo.setscl = savage4_gpio_setscl;
		par->chan.algo.getsda = savage4_gpio_getsda;
		par->chan.algo.getscl = savage4_gpio_getscl;
		break;
	default:
		par->chan.par = NULL;
	}

	savage_setup_i2c_bus(&par->chan, "SAVAGE DDC2");
}

void savagefb_delete_i2c_busses(struct fb_info *info)
{
	struct savagefb_par *par = info->par;

	if (par->chan.par)
		i2c_del_adapter(&par->chan.adapter);

	par->chan.par = NULL;
}

int savagefb_probe_i2c_connector(struct fb_info *info, u8 **out_edid)
{
	struct savagefb_par *par = info->par;
	u8 *edid;

	if (par->chan.par)
		edid = fb_ddc_read(&par->chan.adapter);
	else
		edid = NULL;

	if (!edid) {
		/* try to get from firmware */
		const u8 *e = fb_firmware_edid(info->device);

		if (e)
			edid = kmemdup(e, EDID_LENGTH, GFP_KERNEL);
	}

	*out_edid = edid;

	return (edid) ? 0 : 1;
}

MODULE_LICENSE("GPL");
