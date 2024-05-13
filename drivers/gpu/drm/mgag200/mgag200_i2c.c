/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */
/*
 * Authors: Dave Airlie <airlied@redhat.com>
 */

#include <linux/export.h>
#include <linux/i2c-algo-bit.h>
#include <linux/i2c.h>
#include <linux/pci.h>

#include "mgag200_drv.h"

static int mga_i2c_read_gpio(struct mga_device *mdev)
{
	WREG8(DAC_INDEX, MGA1064_GEN_IO_DATA);
	return RREG8(DAC_DATA);
}

static void mga_i2c_set_gpio(struct mga_device *mdev, int mask, int val)
{
	int tmp;

	WREG8(DAC_INDEX, MGA1064_GEN_IO_CTL);
	tmp = (RREG8(DAC_DATA) & mask) | val;
	WREG_DAC(MGA1064_GEN_IO_CTL, tmp);
	WREG_DAC(MGA1064_GEN_IO_DATA, 0);
}

static inline void mga_i2c_set(struct mga_device *mdev, int mask, int state)
{
	if (state)
		state = 0;
	else
		state = mask;
	mga_i2c_set_gpio(mdev, ~mask, state);
}

static void mga_gpio_setsda(void *data, int state)
{
	struct mga_i2c_chan *i2c = data;
	struct mga_device *mdev = to_mga_device(i2c->dev);
	mga_i2c_set(mdev, i2c->data, state);
}

static void mga_gpio_setscl(void *data, int state)
{
	struct mga_i2c_chan *i2c = data;
	struct mga_device *mdev = to_mga_device(i2c->dev);
	mga_i2c_set(mdev, i2c->clock, state);
}

static int mga_gpio_getsda(void *data)
{
	struct mga_i2c_chan *i2c = data;
	struct mga_device *mdev = to_mga_device(i2c->dev);
	return (mga_i2c_read_gpio(mdev) & i2c->data) ? 1 : 0;
}

static int mga_gpio_getscl(void *data)
{
	struct mga_i2c_chan *i2c = data;
	struct mga_device *mdev = to_mga_device(i2c->dev);
	return (mga_i2c_read_gpio(mdev) & i2c->clock) ? 1 : 0;
}

static void mgag200_i2c_release(void *res)
{
	struct mga_i2c_chan *i2c = res;

	i2c_del_adapter(&i2c->adapter);
}

int mgag200_i2c_init(struct mga_device *mdev, struct mga_i2c_chan *i2c)
{
	struct drm_device *dev = &mdev->base;
	const struct mgag200_device_info *info = mdev->info;
	int ret;

	WREG_DAC(MGA1064_GEN_IO_CTL2, 1);
	WREG_DAC(MGA1064_GEN_IO_DATA, 0xff);
	WREG_DAC(MGA1064_GEN_IO_CTL, 0);

	i2c->data = BIT(info->i2c.data_bit);
	i2c->clock = BIT(info->i2c.clock_bit);
	i2c->adapter.owner = THIS_MODULE;
	i2c->adapter.dev.parent = dev->dev;
	i2c->dev = dev;
	i2c_set_adapdata(&i2c->adapter, i2c);
	snprintf(i2c->adapter.name, sizeof(i2c->adapter.name), "mga i2c");

	i2c->adapter.algo_data = &i2c->bit;

	i2c->bit.udelay = 10;
	i2c->bit.timeout = usecs_to_jiffies(2200);
	i2c->bit.data = i2c;
	i2c->bit.setsda		= mga_gpio_setsda;
	i2c->bit.setscl		= mga_gpio_setscl;
	i2c->bit.getsda		= mga_gpio_getsda;
	i2c->bit.getscl		= mga_gpio_getscl;

	ret = i2c_bit_add_bus(&i2c->adapter);
	if (ret)
		return ret;

	return devm_add_action_or_reset(dev->dev, mgag200_i2c_release, i2c);
}
