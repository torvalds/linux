/*
 * linux/drivers/video/riva/fbdev-i2c.c - nVidia i2c
 *
 * Maintained by Ani Joshi <ajoshi@shell.unixbox.com>
 *
 * Copyright 2004 Antonino A. Daplas <adaplas @pol.net>
 *
 * Based on radeonfb-i2c.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/fb.h>
#include <linux/jiffies.h>

#include <asm/io.h>

#include "rivafb.h"
#include "../edid.h"

static void riva_gpio_setscl(void* data, int state)
{
	struct riva_i2c_chan 	*chan = data;
	struct riva_par 	*par = chan->par;
	u32			val;

	VGA_WR08(par->riva.PCIO, 0x3d4, chan->ddc_base + 1);
	val = VGA_RD08(par->riva.PCIO, 0x3d5) & 0xf0;

	if (state)
		val |= 0x20;
	else
		val &= ~0x20;

	VGA_WR08(par->riva.PCIO, 0x3d4, chan->ddc_base + 1);
	VGA_WR08(par->riva.PCIO, 0x3d5, val | 0x1);
}

static void riva_gpio_setsda(void* data, int state)
{
	struct riva_i2c_chan 	*chan = data;
	struct riva_par 	*par = chan->par;
	u32			val;

	VGA_WR08(par->riva.PCIO, 0x3d4, chan->ddc_base + 1);
	val = VGA_RD08(par->riva.PCIO, 0x3d5) & 0xf0;

	if (state)
		val |= 0x10;
	else
		val &= ~0x10;

	VGA_WR08(par->riva.PCIO, 0x3d4, chan->ddc_base + 1);
	VGA_WR08(par->riva.PCIO, 0x3d5, val | 0x1);
}

static int riva_gpio_getscl(void* data)
{
	struct riva_i2c_chan 	*chan = data;
	struct riva_par 	*par = chan->par;
	u32			val = 0;

	VGA_WR08(par->riva.PCIO, 0x3d4, chan->ddc_base);
	if (VGA_RD08(par->riva.PCIO, 0x3d5) & 0x04)
		val = 1;

	val = VGA_RD08(par->riva.PCIO, 0x3d5);

	return val;
}

static int riva_gpio_getsda(void* data)
{
	struct riva_i2c_chan 	*chan = data;
	struct riva_par 	*par = chan->par;
	u32			val = 0;

	VGA_WR08(par->riva.PCIO, 0x3d4, chan->ddc_base);
	if (VGA_RD08(par->riva.PCIO, 0x3d5) & 0x08)
		val = 1;

	return val;
}

static int riva_setup_i2c_bus(struct riva_i2c_chan *chan, const char *name,
			      unsigned int i2c_class)
{
	int rc;

	strcpy(chan->adapter.name, name);
	chan->adapter.owner		= THIS_MODULE;
	chan->adapter.id		= I2C_HW_B_RIVA;
	chan->adapter.class		= i2c_class;
	chan->adapter.algo_data		= &chan->algo;
	chan->adapter.dev.parent	= &chan->par->pdev->dev;
	chan->algo.setsda		= riva_gpio_setsda;
	chan->algo.setscl		= riva_gpio_setscl;
	chan->algo.getsda		= riva_gpio_getsda;
	chan->algo.getscl		= riva_gpio_getscl;
	chan->algo.udelay		= 40;
	chan->algo.timeout		= msecs_to_jiffies(2);
	chan->algo.data 		= chan;

	i2c_set_adapdata(&chan->adapter, chan);

	/* Raise SCL and SDA */
	riva_gpio_setsda(chan, 1);
	riva_gpio_setscl(chan, 1);
	udelay(20);

	rc = i2c_bit_add_bus(&chan->adapter);
	if (rc == 0)
		dev_dbg(&chan->par->pdev->dev, "I2C bus %s registered.\n", name);
	else {
		dev_warn(&chan->par->pdev->dev,
			 "Failed to register I2C bus %s.\n", name);
		chan->par = NULL;
	}

	return rc;
}

void riva_create_i2c_busses(struct riva_par *par)
{
	par->bus = 3;

	par->chan[0].par	= par;
	par->chan[1].par	= par;
	par->chan[2].par        = par;

	par->chan[0].ddc_base = 0x3e;
	par->chan[1].ddc_base = 0x36;
	par->chan[2].ddc_base = 0x50;
	riva_setup_i2c_bus(&par->chan[0], "BUS1", 0);
	riva_setup_i2c_bus(&par->chan[1], "BUS2", I2C_CLASS_HWMON);
	riva_setup_i2c_bus(&par->chan[2], "BUS3", 0);
}

void riva_delete_i2c_busses(struct riva_par *par)
{
	if (par->chan[0].par)
		i2c_del_adapter(&par->chan[0].adapter);
	par->chan[0].par = NULL;

	if (par->chan[1].par)
		i2c_del_adapter(&par->chan[1].adapter);
	par->chan[1].par = NULL;

	if (par->chan[2].par)
		i2c_del_adapter(&par->chan[2].adapter);
	par->chan[2].par = NULL;
}

int riva_probe_i2c_connector(struct riva_par *par, int conn, u8 **out_edid)
{
	u8 *edid = NULL;

	edid = fb_ddc_read(&par->chan[conn-1].adapter);

	if (out_edid)
		*out_edid = edid;
	if (!edid)
		return 1;

	return 0;
}

