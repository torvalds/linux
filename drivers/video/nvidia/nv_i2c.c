/*
 * linux/drivers/video/nvidia/nvidia-i2c.c - nVidia i2c
 *
 * Copyright 2004 Antonino A. Daplas <adaplas @pol.net>
 *
 * Based on rivafb-i2c.c
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

#include <asm/io.h>

#include "nv_type.h"
#include "nv_local.h"
#include "nv_proto.h"

#include "../edid.h"

static void nvidia_gpio_setscl(void *data, int state)
{
	struct nvidia_i2c_chan *chan = data;
	struct nvidia_par *par = chan->par;
	u32 val;

	VGA_WR08(par->PCIO, 0x3d4, chan->ddc_base + 1);
	val = VGA_RD08(par->PCIO, 0x3d5) & 0xf0;

	if (state)
		val |= 0x20;
	else
		val &= ~0x20;

	VGA_WR08(par->PCIO, 0x3d4, chan->ddc_base + 1);
	VGA_WR08(par->PCIO, 0x3d5, val | 0x1);
}

static void nvidia_gpio_setsda(void *data, int state)
{
	struct nvidia_i2c_chan *chan = data;
	struct nvidia_par *par = chan->par;
	u32 val;

	VGA_WR08(par->PCIO, 0x3d4, chan->ddc_base + 1);
	val = VGA_RD08(par->PCIO, 0x3d5) & 0xf0;

	if (state)
		val |= 0x10;
	else
		val &= ~0x10;

	VGA_WR08(par->PCIO, 0x3d4, chan->ddc_base + 1);
	VGA_WR08(par->PCIO, 0x3d5, val | 0x1);
}

static int nvidia_gpio_getscl(void *data)
{
	struct nvidia_i2c_chan *chan = data;
	struct nvidia_par *par = chan->par;
	u32 val = 0;

	VGA_WR08(par->PCIO, 0x3d4, chan->ddc_base);
	if (VGA_RD08(par->PCIO, 0x3d5) & 0x04)
		val = 1;

	return val;
}

static int nvidia_gpio_getsda(void *data)
{
	struct nvidia_i2c_chan *chan = data;
	struct nvidia_par *par = chan->par;
	u32 val = 0;

	VGA_WR08(par->PCIO, 0x3d4, chan->ddc_base);
	if (VGA_RD08(par->PCIO, 0x3d5) & 0x08)
		val = 1;

	return val;
}

static int nvidia_setup_i2c_bus(struct nvidia_i2c_chan *chan, const char *name)
{
	int rc;

	strcpy(chan->adapter.name, name);
	chan->adapter.owner = THIS_MODULE;
	chan->adapter.id = I2C_HW_B_NVIDIA;
	chan->adapter.algo_data = &chan->algo;
	chan->adapter.dev.parent = &chan->par->pci_dev->dev;
	chan->algo.setsda = nvidia_gpio_setsda;
	chan->algo.setscl = nvidia_gpio_setscl;
	chan->algo.getsda = nvidia_gpio_getsda;
	chan->algo.getscl = nvidia_gpio_getscl;
	chan->algo.udelay = 40;
	chan->algo.timeout = msecs_to_jiffies(2);
	chan->algo.data = chan;

	i2c_set_adapdata(&chan->adapter, chan);

	/* Raise SCL and SDA */
	nvidia_gpio_setsda(chan, 1);
	nvidia_gpio_setscl(chan, 1);
	udelay(20);

	rc = i2c_bit_add_bus(&chan->adapter);
	if (rc == 0)
		dev_dbg(&chan->par->pci_dev->dev,
			"I2C bus %s registered.\n", name);
	else {
		dev_warn(&chan->par->pci_dev->dev,
			 "Failed to register I2C bus %s.\n", name);
		chan->par = NULL;
	}

	return rc;
}

void nvidia_create_i2c_busses(struct nvidia_par *par)
{
	par->bus = 3;

	par->chan[0].par = par;
	par->chan[1].par = par;
	par->chan[2].par = par;

	par->chan[0].ddc_base = 0x3e;
	nvidia_setup_i2c_bus(&par->chan[0], "nvidia #0");

	par->chan[1].ddc_base = 0x36;
	nvidia_setup_i2c_bus(&par->chan[1], "nvidia #1");

	par->chan[2].ddc_base = 0x50;
	nvidia_setup_i2c_bus(&par->chan[2], "nvidia #2");
}

void nvidia_delete_i2c_busses(struct nvidia_par *par)
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

int nvidia_probe_i2c_connector(struct fb_info *info, int conn, u8 **out_edid)
{
	struct nvidia_par *par = info->par;
	u8 *edid = NULL;

	if (par->chan[conn - 1].par)
		edid = fb_ddc_read(&par->chan[conn - 1].adapter);

	if (!edid && conn == 1) {
		/* try to get from firmware */
		const u8 *e = fb_firmware_edid(info->device);

		if (e != NULL)
			edid = kmemdup(e, EDID_LENGTH, GFP_KERNEL);
	}

	*out_edid = edid;

	return (edid) ? 0 : 1;
}
