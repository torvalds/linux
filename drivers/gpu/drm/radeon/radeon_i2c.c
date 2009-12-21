/*
 * Copyright 2007-8 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 */
#include "drmP.h"
#include "radeon_drm.h"
#include "radeon.h"

/**
 * radeon_ddc_probe
 *
 */
bool radeon_ddc_probe(struct radeon_connector *radeon_connector)
{
	u8 out_buf[] = { 0x0, 0x0};
	u8 buf[2];
	int ret;
	struct i2c_msg msgs[] = {
		{
			.addr = 0x50,
			.flags = 0,
			.len = 1,
			.buf = out_buf,
		},
		{
			.addr = 0x50,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = buf,
		}
	};

	ret = i2c_transfer(&radeon_connector->ddc_bus->adapter, msgs, 2);
	if (ret == 2)
		return true;

	return false;
}


void radeon_i2c_do_lock(struct radeon_i2c_chan *i2c, int lock_state)
{
	struct radeon_device *rdev = i2c->dev->dev_private;
	struct radeon_i2c_bus_rec *rec = &i2c->rec;
	uint32_t temp;

	/* RV410 appears to have a bug where the hw i2c in reset
	 * holds the i2c port in a bad state - switch hw i2c away before
	 * doing DDC - do this for all r200s/r300s/r400s for safety sake
	 */
	if (rec->hw_capable) {
		if ((rdev->family >= CHIP_R200) && !ASIC_IS_AVIVO(rdev)) {
			if (rec->a_clk_reg == RADEON_GPIO_MONID) {
				WREG32(RADEON_DVI_I2C_CNTL_0, (RADEON_I2C_SOFT_RST |
							       R200_DVI_I2C_PIN_SEL(R200_SEL_DDC1)));
			} else {
				WREG32(RADEON_DVI_I2C_CNTL_0, (RADEON_I2C_SOFT_RST |
							       R200_DVI_I2C_PIN_SEL(R200_SEL_DDC3)));
			}
		}
	}

	/* clear the output pin values */
	temp = RREG32(rec->a_clk_reg) & ~rec->a_clk_mask;
	WREG32(rec->a_clk_reg, temp);

	temp = RREG32(rec->a_data_reg) & ~rec->a_data_mask;
	WREG32(rec->a_data_reg, temp);

	/* set the pins to input */
	temp = RREG32(rec->en_clk_reg) & ~rec->en_clk_mask;
	WREG32(rec->en_clk_reg, temp);

	temp = RREG32(rec->en_data_reg) & ~rec->en_data_mask;
	WREG32(rec->en_data_reg, temp);

	/* mask the gpio pins for software use */
	temp = RREG32(rec->mask_clk_reg);
	if (lock_state)
		temp |= rec->mask_clk_mask;
	else
		temp &= ~rec->mask_clk_mask;
	WREG32(rec->mask_clk_reg, temp);
	temp = RREG32(rec->mask_clk_reg);

	temp = RREG32(rec->mask_data_reg);
	if (lock_state)
		temp |= rec->mask_data_mask;
	else
		temp &= ~rec->mask_data_mask;
	WREG32(rec->mask_data_reg, temp);
	temp = RREG32(rec->mask_data_reg);
}

static int get_clock(void *i2c_priv)
{
	struct radeon_i2c_chan *i2c = i2c_priv;
	struct radeon_device *rdev = i2c->dev->dev_private;
	struct radeon_i2c_bus_rec *rec = &i2c->rec;
	uint32_t val;

	/* read the value off the pin */
	val = RREG32(rec->y_clk_reg);
	val &= rec->y_clk_mask;

	return (val != 0);
}


static int get_data(void *i2c_priv)
{
	struct radeon_i2c_chan *i2c = i2c_priv;
	struct radeon_device *rdev = i2c->dev->dev_private;
	struct radeon_i2c_bus_rec *rec = &i2c->rec;
	uint32_t val;

	/* read the value off the pin */
	val = RREG32(rec->y_data_reg);
	val &= rec->y_data_mask;

	return (val != 0);
}

static void set_clock(void *i2c_priv, int clock)
{
	struct radeon_i2c_chan *i2c = i2c_priv;
	struct radeon_device *rdev = i2c->dev->dev_private;
	struct radeon_i2c_bus_rec *rec = &i2c->rec;
	uint32_t val;

	/* set pin direction */
	val = RREG32(rec->en_clk_reg) & ~rec->en_clk_mask;
	val |= clock ? 0 : rec->en_clk_mask;
	WREG32(rec->en_clk_reg, val);
}

static void set_data(void *i2c_priv, int data)
{
	struct radeon_i2c_chan *i2c = i2c_priv;
	struct radeon_device *rdev = i2c->dev->dev_private;
	struct radeon_i2c_bus_rec *rec = &i2c->rec;
	uint32_t val;

	/* set pin direction */
	val = RREG32(rec->en_data_reg) & ~rec->en_data_mask;
	val |= data ? 0 : rec->en_data_mask;
	WREG32(rec->en_data_reg, val);
}

struct radeon_i2c_chan *radeon_i2c_create(struct drm_device *dev,
					  struct radeon_i2c_bus_rec *rec,
					  const char *name)
{
	struct radeon_i2c_chan *i2c;
	int ret;

	i2c = kzalloc(sizeof(struct radeon_i2c_chan), GFP_KERNEL);
	if (i2c == NULL)
		return NULL;

	i2c->adapter.owner = THIS_MODULE;
	i2c->dev = dev;
	i2c_set_adapdata(&i2c->adapter, i2c);
	i2c->adapter.algo_data = &i2c->algo.bit;
	i2c->algo.bit.setsda = set_data;
	i2c->algo.bit.setscl = set_clock;
	i2c->algo.bit.getsda = get_data;
	i2c->algo.bit.getscl = get_clock;
	i2c->algo.bit.udelay = 20;
	/* vesa says 2.2 ms is enough, 1 jiffy doesn't seem to always
	 * make this, 2 jiffies is a lot more reliable */
	i2c->algo.bit.timeout = 2;
	i2c->algo.bit.data = i2c;
	i2c->rec = *rec;
	ret = i2c_bit_add_bus(&i2c->adapter);
	if (ret) {
		DRM_INFO("Failed to register i2c %s\n", name);
		goto out_free;
	}

	return i2c;
out_free:
	kfree(i2c);
	return NULL;

}

struct radeon_i2c_chan *radeon_i2c_create_dp(struct drm_device *dev,
					     struct radeon_i2c_bus_rec *rec,
					     const char *name)
{
	struct radeon_i2c_chan *i2c;
	int ret;

	i2c = kzalloc(sizeof(struct radeon_i2c_chan), GFP_KERNEL);
	if (i2c == NULL)
		return NULL;

	i2c->rec = *rec;
	i2c->adapter.owner = THIS_MODULE;
	i2c->dev = dev;
	i2c_set_adapdata(&i2c->adapter, i2c);
	i2c->adapter.algo_data = &i2c->algo.dp;
	i2c->algo.dp.aux_ch = radeon_dp_i2c_aux_ch;
	i2c->algo.dp.address = 0;
	ret = i2c_dp_aux_add_bus(&i2c->adapter);
	if (ret) {
		DRM_INFO("Failed to register i2c %s\n", name);
		goto out_free;
	}

	return i2c;
out_free:
	kfree(i2c);
	return NULL;

}


void radeon_i2c_destroy(struct radeon_i2c_chan *i2c)
{
	if (!i2c)
		return;

	i2c_del_adapter(&i2c->adapter);
	kfree(i2c);
}

struct drm_encoder *radeon_best_encoder(struct drm_connector *connector)
{
	return NULL;
}

void radeon_i2c_sw_get_byte(struct radeon_i2c_chan *i2c_bus,
			    u8 slave_addr,
			    u8 addr,
			    u8 *val)
{
	u8 out_buf[2];
	u8 in_buf[2];
	struct i2c_msg msgs[] = {
		{
			.addr = slave_addr,
			.flags = 0,
			.len = 1,
			.buf = out_buf,
		},
		{
			.addr = slave_addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = in_buf,
		}
	};

	out_buf[0] = addr;
	out_buf[1] = 0;

	if (i2c_transfer(&i2c_bus->adapter, msgs, 2) == 2) {
		*val = in_buf[0];
		DRM_DEBUG("val = 0x%02x\n", *val);
	} else {
		DRM_ERROR("i2c 0x%02x 0x%02x read failed\n",
			  addr, *val);
	}
}

void radeon_i2c_sw_put_byte(struct radeon_i2c_chan *i2c_bus,
			    u8 slave_addr,
			    u8 addr,
			    u8 val)
{
	uint8_t out_buf[2];
	struct i2c_msg msg = {
		.addr = slave_addr,
		.flags = 0,
		.len = 2,
		.buf = out_buf,
	};

	out_buf[0] = addr;
	out_buf[1] = val;

	if (i2c_transfer(&i2c_bus->adapter, &msg, 1) != 1)
		DRM_ERROR("i2c 0x%02x 0x%02x write failed\n",
			  addr, val);
}

