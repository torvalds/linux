 /*-*- linux-c -*-
 *  linux/drivers/video/i810-i2c.c -- Intel 810/815 I2C support
 *
 *      Copyright (C) 2004 Antonino Daplas<adaplas@pol.net>
 *      All Rights Reserved
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/fb.h>
#include "i810.h"
#include "i810_regs.h"
#include "../edid.h"

#define I810_DDC 0x50
/* bit locations in the registers */
#define SCL_DIR_MASK		0x0001
#define SCL_DIR			0x0002
#define SCL_VAL_MASK		0x0004
#define SCL_VAL_OUT		0x0008
#define SCL_VAL_IN		0x0010
#define SDA_DIR_MASK		0x0100
#define SDA_DIR			0x0200
#define SDA_VAL_MASK		0x0400
#define SDA_VAL_OUT		0x0800
#define SDA_VAL_IN		0x1000

#define DEBUG  /* define this for verbose EDID parsing output */

#ifdef DEBUG
#define DPRINTK(fmt, args...) printk(fmt,## args)
#else
#define DPRINTK(fmt, args...)
#endif

static void i810i2c_setscl(void *data, int state)
{
        struct i810fb_i2c_chan    *chan = (struct i810fb_i2c_chan *)data;
        struct i810fb_par         *par = chan->par;
	u8                        __iomem *mmio = par->mmio_start_virtual;

	i810_writel(mmio, GPIOB, (state ? SCL_VAL_OUT : 0) | SCL_DIR |
		    SCL_DIR_MASK | SCL_VAL_MASK);
	i810_readl(mmio, GPIOB);	/* flush posted write */
}

static void i810i2c_setsda(void *data, int state)
{
        struct i810fb_i2c_chan    *chan = (struct i810fb_i2c_chan *)data;
        struct i810fb_par         *par = chan->par;
	u8                        __iomem *mmio = par->mmio_start_virtual;

 	i810_writel(mmio, GPIOB, (state ? SDA_VAL_OUT : 0) | SDA_DIR |
		    SDA_DIR_MASK | SDA_VAL_MASK);
	i810_readl(mmio, GPIOB);	/* flush posted write */
}

static int i810i2c_getscl(void *data)
{
        struct i810fb_i2c_chan    *chan = (struct i810fb_i2c_chan *)data;
        struct i810fb_par         *par = chan->par;
	u8                        __iomem *mmio = par->mmio_start_virtual;

	i810_writel(mmio, GPIOB, SCL_DIR_MASK);
	i810_writel(mmio, GPIOB, 0);
	return (0 != (i810_readl(mmio, GPIOB) & SCL_VAL_IN));
}

static int i810i2c_getsda(void *data)
{
        struct i810fb_i2c_chan    *chan = (struct i810fb_i2c_chan *)data;
        struct i810fb_par         *par = chan->par;
	u8                        __iomem *mmio = par->mmio_start_virtual;

	i810_writel(mmio, GPIOB, SDA_DIR_MASK);
	i810_writel(mmio, GPIOB, 0);
	return (0 != (i810_readl(mmio, GPIOB) & SDA_VAL_IN));
}

static void i810ddc_setscl(void *data, int state)
{
        struct i810fb_i2c_chan    *chan = (struct i810fb_i2c_chan *)data;
        struct i810fb_par       *par = chan->par;
	u8                      __iomem *mmio = par->mmio_start_virtual;

	i810_writel(mmio, GPIOA, (state ? SCL_VAL_OUT : 0) | SCL_DIR |
		    SCL_DIR_MASK | SCL_VAL_MASK);
	i810_readl(mmio, GPIOA);	/* flush posted write */
}

static void i810ddc_setsda(void *data, int state)
{
        struct i810fb_i2c_chan    *chan = (struct i810fb_i2c_chan *)data;
        struct i810fb_par         *par = chan->par;
	u8                      __iomem *mmio = par->mmio_start_virtual;

 	i810_writel(mmio, GPIOA, (state ? SDA_VAL_OUT : 0) | SDA_DIR |
		    SDA_DIR_MASK | SDA_VAL_MASK);
	i810_readl(mmio, GPIOA);	/* flush posted write */
}

static int i810ddc_getscl(void *data)
{
        struct i810fb_i2c_chan    *chan = (struct i810fb_i2c_chan *)data;
        struct i810fb_par         *par = chan->par;
	u8                      __iomem *mmio = par->mmio_start_virtual;

	i810_writel(mmio, GPIOA, SCL_DIR_MASK);
	i810_writel(mmio, GPIOA, 0);
	return (0 != (i810_readl(mmio, GPIOA) & SCL_VAL_IN));
}

static int i810ddc_getsda(void *data)
{
        struct i810fb_i2c_chan    *chan = (struct i810fb_i2c_chan *)data;
        struct i810fb_par         *par = chan->par;
	u8                      __iomem *mmio = par->mmio_start_virtual;

	i810_writel(mmio, GPIOA, SDA_DIR_MASK);
	i810_writel(mmio, GPIOA, 0);
	return (0 != (i810_readl(mmio, GPIOA) & SDA_VAL_IN));
}

#define I2C_ALGO_DDC_I810   0x0e0000
#define I2C_ALGO_I2C_I810   0x0f0000
static int i810_setup_i2c_bus(struct i810fb_i2c_chan *chan, const char *name,
			      int conn)
{
        int rc;

        strcpy(chan->adapter.name, name);
        chan->adapter.owner             = THIS_MODULE;
        chan->adapter.algo_data         = &chan->algo;
        chan->adapter.dev.parent        = &chan->par->dev->dev;
	switch (conn) {
	case 1:
		chan->adapter.id                = I2C_ALGO_DDC_I810;
		chan->algo.setsda               = i810ddc_setsda;
		chan->algo.setscl               = i810ddc_setscl;
		chan->algo.getsda               = i810ddc_getsda;
		chan->algo.getscl               = i810ddc_getscl;
		break;
	case 2:
		chan->adapter.id                = I2C_ALGO_I2C_I810;
		chan->algo.setsda               = i810i2c_setsda;
		chan->algo.setscl               = i810i2c_setscl;
		chan->algo.getsda               = i810i2c_getsda;
		chan->algo.getscl               = i810i2c_getscl;
		break;
	}
	chan->algo.udelay               = 10;
	chan->algo.mdelay               = 10;
        chan->algo.timeout              = (HZ/2);
        chan->algo.data                 = chan;

        i2c_set_adapdata(&chan->adapter, chan);

        /* Raise SCL and SDA */
        chan->algo.setsda(chan, 1);
        chan->algo.setscl(chan, 1);
        udelay(20);

        rc = i2c_bit_add_bus(&chan->adapter);
        if (rc == 0)
                dev_dbg(&chan->par->dev->dev, "I2C bus %s registered.\n",name);
        else
                dev_warn(&chan->par->dev->dev, "Failed to register I2C bus "
			 "%s.\n", name);
        return rc;
}

void i810_create_i2c_busses(struct i810fb_par *par)
{
        par->chan[0].par        = par;
	par->chan[1].par        = par;
	i810_setup_i2c_bus(&par->chan[0], "I810-DDC", 1);
	i810_setup_i2c_bus(&par->chan[1], "I810-I2C", 2);
}

void i810_delete_i2c_busses(struct i810fb_par *par)
{
        if (par->chan[0].par)
                i2c_bit_del_bus(&par->chan[0].adapter);
        par->chan[0].par = NULL;
	if (par->chan[1].par)
		i2c_bit_del_bus(&par->chan[1].adapter);
	par->chan[1].par = NULL;
}

static u8 *i810_do_probe_i2c_edid(struct i810fb_i2c_chan *chan)
{
        u8 start = 0x0;
        struct i2c_msg msgs[] = {
                {
                        .addr   = I810_DDC,
                        .len    = 1,
                        .buf    = &start,
                }, {
                        .addr   = I810_DDC,
                        .flags  = I2C_M_RD,
                        .len    = EDID_LENGTH,
                },
        };
        u8 *buf;

        buf = kmalloc(EDID_LENGTH, GFP_KERNEL);
        if (!buf) {
		DPRINTK("i810-i2c: Failed to allocate memory\n");
                return NULL;
        }
        msgs[1].buf = buf;

        if (i2c_transfer(&chan->adapter, msgs, 2) == 2) {
		DPRINTK("i810-i2c: I2C Transfer successful\n");
                return buf;
	}
        DPRINTK("i810-i2c: Unable to read EDID block.\n");
        kfree(buf);
        return NULL;
}

int i810_probe_i2c_connector(struct fb_info *info, u8 **out_edid, int conn)
{
	struct i810fb_par *par = info->par;
        u8 *edid = NULL;
        int i;

	DPRINTK("i810-i2c: Probe DDC%i Bus\n", conn);
	if (conn < 3) {
		for (i = 0; i < 3; i++) {
			/* Do the real work */
			edid = i810_do_probe_i2c_edid(&par->chan[conn-1]);
			if (edid)
				break;
		}
	} else {
		DPRINTK("i810-i2c: Getting EDID from BIOS\n");
		edid = kmalloc(EDID_LENGTH, GFP_KERNEL);
		if (edid)
			memcpy(edid, fb_firmware_edid(info->device),
			       EDID_LENGTH);
	}

        if (out_edid)
                *out_edid = edid;

        return (edid) ? 0 : 1;
}


