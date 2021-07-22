/*
 * Copyright (c) 2002-2010, Intel Corporation.
 * Copyright (c) 2014 ATRON electronic GmbH
 *   Author: Jan Safrata <jan.nikitenko@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <linux/delay.h>
#include <linux/i2c-algo-bit.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/types.h>

#include "psb_drv.h"
#include "psb_intel_reg.h"


/*
 * LPC GPIO based I2C bus for LVDS of Atom E6xx
 */

/*-----------------------------------------------------------------------------
 * LPC Register Offsets. Used for LVDS GPIO Bit Bashing. Registers are part
 * Atom E6xx [D31:F0]
 ----------------------------------------------------------------------------*/
#define RGEN    0x20
#define RGIO    0x24
#define RGLVL   0x28
#define RGTPE   0x2C
#define RGTNE   0x30
#define RGGPE   0x34
#define RGSMI   0x38
#define RGTS    0x3C

/* The LVDS GPIO clock lines are GPIOSUS[3]
 * The LVDS GPIO data lines are GPIOSUS[4]
 */
#define GPIO_CLOCK	0x08
#define GPIO_DATA	0x10

#define LPC_READ_REG(chan, r) inl((chan)->reg + (r))
#define LPC_WRITE_REG(chan, r, val) outl((val), (chan)->reg + (r))

static int get_clock(void *data)
{
	struct psb_intel_i2c_chan *chan = data;
	u32 val;

	val = LPC_READ_REG(chan, RGIO);
	val |= GPIO_CLOCK;
	LPC_WRITE_REG(chan, RGIO, val);
	LPC_READ_REG(chan, RGLVL);
	val = (LPC_READ_REG(chan, RGLVL) & GPIO_CLOCK) ? 1 : 0;

	return val;
}

static int get_data(void *data)
{
	struct psb_intel_i2c_chan *chan = data;
	u32 val;

	val = LPC_READ_REG(chan, RGIO);
	val |= GPIO_DATA;
	LPC_WRITE_REG(chan, RGIO, val);
	LPC_READ_REG(chan, RGLVL);
	val = (LPC_READ_REG(chan, RGLVL) & GPIO_DATA) ? 1 : 0;

	return val;
}

static void set_clock(void *data, int state_high)
{
	struct psb_intel_i2c_chan *chan = data;
	u32 val;

	if (state_high) {
		val = LPC_READ_REG(chan, RGIO);
		val |= GPIO_CLOCK;
		LPC_WRITE_REG(chan, RGIO, val);
	} else {
		val = LPC_READ_REG(chan, RGIO);
		val &= ~GPIO_CLOCK;
		LPC_WRITE_REG(chan, RGIO, val);
		val = LPC_READ_REG(chan, RGLVL);
		val &= ~GPIO_CLOCK;
		LPC_WRITE_REG(chan, RGLVL, val);
	}
}

static void set_data(void *data, int state_high)
{
	struct psb_intel_i2c_chan *chan = data;
	u32 val;

	if (state_high) {
		val = LPC_READ_REG(chan, RGIO);
		val |= GPIO_DATA;
		LPC_WRITE_REG(chan, RGIO, val);
	} else {
		val = LPC_READ_REG(chan, RGIO);
		val &= ~GPIO_DATA;
		LPC_WRITE_REG(chan, RGIO, val);
		val = LPC_READ_REG(chan, RGLVL);
		val &= ~GPIO_DATA;
		LPC_WRITE_REG(chan, RGLVL, val);
	}
}

void oaktrail_lvds_i2c_init(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct gma_encoder *gma_encoder = to_gma_encoder(encoder);
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_intel_i2c_chan *chan;

	chan = kzalloc(sizeof(struct psb_intel_i2c_chan), GFP_KERNEL);
	if (!chan)
		return;

	chan->drm_dev = dev;
	chan->reg = dev_priv->lpc_gpio_base;
	strncpy(chan->adapter.name, "gma500 LPC",  I2C_NAME_SIZE - 1);
	chan->adapter.owner = THIS_MODULE;
	chan->adapter.algo_data = &chan->algo;
	chan->adapter.dev.parent = dev->dev;
	chan->algo.setsda = set_data;
	chan->algo.setscl = set_clock;
	chan->algo.getsda = get_data;
	chan->algo.getscl = get_clock;
	chan->algo.udelay = 100;
	chan->algo.timeout = usecs_to_jiffies(2200);
	chan->algo.data = chan;

	i2c_set_adapdata(&chan->adapter, chan);

	set_data(chan, 1);
	set_clock(chan, 1);
	udelay(50);

	if (i2c_bit_add_bus(&chan->adapter)) {
		kfree(chan);
		return;
	}

	gma_encoder->ddc_bus = chan;
}
