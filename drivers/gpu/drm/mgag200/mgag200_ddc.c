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

#include <drm/drm_managed.h>

#include "mgag200_ddc.h"
#include "mgag200_drv.h"

struct mgag200_ddc {
	struct mga_device *mdev;

	int data;
	int clock;

	struct i2c_algo_bit_data bit;
	struct i2c_adapter adapter;
};

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

static void mgag200_ddc_algo_bit_data_setsda(void *data, int state)
{
	struct mgag200_ddc *ddc = data;

	mga_i2c_set(ddc->mdev, ddc->data, state);
}

static void mgag200_ddc_algo_bit_data_setscl(void *data, int state)
{
	struct mgag200_ddc *ddc = data;

	mga_i2c_set(ddc->mdev, ddc->clock, state);
}

static int mgag200_ddc_algo_bit_data_getsda(void *data)
{
	struct mgag200_ddc *ddc = data;

	return (mga_i2c_read_gpio(ddc->mdev) & ddc->data) ? 1 : 0;
}

static int mgag200_ddc_algo_bit_data_getscl(void *data)
{
	struct mgag200_ddc *ddc = data;

	return (mga_i2c_read_gpio(ddc->mdev) & ddc->clock) ? 1 : 0;
}

static int mgag200_ddc_algo_bit_data_pre_xfer(struct i2c_adapter *adapter)
{
	struct mgag200_ddc *ddc = i2c_get_adapdata(adapter);
	struct mga_device *mdev = ddc->mdev;

	/*
	 * Protect access to I/O registers from concurrent modesetting
	 * by acquiring the I/O-register lock.
	 */
	mutex_lock(&mdev->rmmio_lock);

	return 0;
}

static void mgag200_ddc_algo_bit_data_post_xfer(struct i2c_adapter *adapter)
{
	struct mgag200_ddc *ddc = i2c_get_adapdata(adapter);
	struct mga_device *mdev = ddc->mdev;

	mutex_unlock(&mdev->rmmio_lock);
}

static void mgag200_ddc_release(struct drm_device *dev, void *res)
{
	struct mgag200_ddc *ddc = res;

	i2c_del_adapter(&ddc->adapter);
}

struct i2c_adapter *mgag200_ddc_create(struct mga_device *mdev)
{
	struct drm_device *dev = &mdev->base;
	const struct mgag200_device_info *info = mdev->info;
	struct mgag200_ddc *ddc;
	struct i2c_algo_bit_data *bit;
	struct i2c_adapter *adapter;
	int ret;

	ddc = drmm_kzalloc(dev, sizeof(*ddc), GFP_KERNEL);
	if (!ddc)
		return ERR_PTR(-ENOMEM);

	WREG_DAC(MGA1064_GEN_IO_CTL2, 1);
	WREG_DAC(MGA1064_GEN_IO_DATA, 0xff);
	WREG_DAC(MGA1064_GEN_IO_CTL, 0);

	ddc->mdev = mdev;
	ddc->data = BIT(info->i2c.data_bit);
	ddc->clock = BIT(info->i2c.clock_bit);

	bit = &ddc->bit;
	bit->data = ddc;
	bit->setsda = mgag200_ddc_algo_bit_data_setsda;
	bit->setscl = mgag200_ddc_algo_bit_data_setscl;
	bit->getsda = mgag200_ddc_algo_bit_data_getsda;
	bit->getscl = mgag200_ddc_algo_bit_data_getscl;
	bit->pre_xfer = mgag200_ddc_algo_bit_data_pre_xfer;
	bit->post_xfer = mgag200_ddc_algo_bit_data_post_xfer;
	bit->udelay = 10;
	bit->timeout = usecs_to_jiffies(2200);

	adapter = &ddc->adapter;
	adapter->owner = THIS_MODULE;
	adapter->algo_data = bit;
	adapter->dev.parent = dev->dev;
	snprintf(adapter->name, sizeof(adapter->name), "Matrox DDC bus");
	i2c_set_adapdata(adapter, ddc);

	ret = i2c_bit_add_bus(adapter);
	if (ret)
		return ERR_PTR(ret);

	ret = drmm_add_action_or_reset(dev, mgag200_ddc_release, ddc);
	if (ret)
		return ERR_PTR(ret);

	return adapter;
}
