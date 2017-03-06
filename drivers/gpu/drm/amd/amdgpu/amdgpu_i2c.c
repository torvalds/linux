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
#include <linux/export.h>

#include <drm/drmP.h>
#include <drm/drm_edid.h>
#include <drm/amdgpu_drm.h>
#include "amdgpu.h"
#include "amdgpu_i2c.h"
#include "amdgpu_atombios.h"
#include "atom.h"
#include "atombios_dp.h"
#include "atombios_i2c.h"

/* bit banging i2c */
static int amdgpu_i2c_pre_xfer(struct i2c_adapter *i2c_adap)
{
	struct amdgpu_i2c_chan *i2c = i2c_get_adapdata(i2c_adap);
	struct amdgpu_device *adev = i2c->dev->dev_private;
	struct amdgpu_i2c_bus_rec *rec = &i2c->rec;
	uint32_t temp;

	mutex_lock(&i2c->mutex);

	/* switch the pads to ddc mode */
	if (rec->hw_capable) {
		temp = RREG32(rec->mask_clk_reg);
		temp &= ~(1 << 16);
		WREG32(rec->mask_clk_reg, temp);
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
	temp = RREG32(rec->mask_clk_reg) | rec->mask_clk_mask;
	WREG32(rec->mask_clk_reg, temp);
	temp = RREG32(rec->mask_clk_reg);

	temp = RREG32(rec->mask_data_reg) | rec->mask_data_mask;
	WREG32(rec->mask_data_reg, temp);
	temp = RREG32(rec->mask_data_reg);

	return 0;
}

static void amdgpu_i2c_post_xfer(struct i2c_adapter *i2c_adap)
{
	struct amdgpu_i2c_chan *i2c = i2c_get_adapdata(i2c_adap);
	struct amdgpu_device *adev = i2c->dev->dev_private;
	struct amdgpu_i2c_bus_rec *rec = &i2c->rec;
	uint32_t temp;

	/* unmask the gpio pins for software use */
	temp = RREG32(rec->mask_clk_reg) & ~rec->mask_clk_mask;
	WREG32(rec->mask_clk_reg, temp);
	temp = RREG32(rec->mask_clk_reg);

	temp = RREG32(rec->mask_data_reg) & ~rec->mask_data_mask;
	WREG32(rec->mask_data_reg, temp);
	temp = RREG32(rec->mask_data_reg);

	mutex_unlock(&i2c->mutex);
}

static int amdgpu_i2c_get_clock(void *i2c_priv)
{
	struct amdgpu_i2c_chan *i2c = i2c_priv;
	struct amdgpu_device *adev = i2c->dev->dev_private;
	struct amdgpu_i2c_bus_rec *rec = &i2c->rec;
	uint32_t val;

	/* read the value off the pin */
	val = RREG32(rec->y_clk_reg);
	val &= rec->y_clk_mask;

	return (val != 0);
}


static int amdgpu_i2c_get_data(void *i2c_priv)
{
	struct amdgpu_i2c_chan *i2c = i2c_priv;
	struct amdgpu_device *adev = i2c->dev->dev_private;
	struct amdgpu_i2c_bus_rec *rec = &i2c->rec;
	uint32_t val;

	/* read the value off the pin */
	val = RREG32(rec->y_data_reg);
	val &= rec->y_data_mask;

	return (val != 0);
}

static void amdgpu_i2c_set_clock(void *i2c_priv, int clock)
{
	struct amdgpu_i2c_chan *i2c = i2c_priv;
	struct amdgpu_device *adev = i2c->dev->dev_private;
	struct amdgpu_i2c_bus_rec *rec = &i2c->rec;
	uint32_t val;

	/* set pin direction */
	val = RREG32(rec->en_clk_reg) & ~rec->en_clk_mask;
	val |= clock ? 0 : rec->en_clk_mask;
	WREG32(rec->en_clk_reg, val);
}

static void amdgpu_i2c_set_data(void *i2c_priv, int data)
{
	struct amdgpu_i2c_chan *i2c = i2c_priv;
	struct amdgpu_device *adev = i2c->dev->dev_private;
	struct amdgpu_i2c_bus_rec *rec = &i2c->rec;
	uint32_t val;

	/* set pin direction */
	val = RREG32(rec->en_data_reg) & ~rec->en_data_mask;
	val |= data ? 0 : rec->en_data_mask;
	WREG32(rec->en_data_reg, val);
}

static const struct i2c_algorithm amdgpu_atombios_i2c_algo = {
	.master_xfer = amdgpu_atombios_i2c_xfer,
	.functionality = amdgpu_atombios_i2c_func,
};

struct amdgpu_i2c_chan *amdgpu_i2c_create(struct drm_device *dev,
					  const struct amdgpu_i2c_bus_rec *rec,
					  const char *name)
{
	struct amdgpu_i2c_chan *i2c;
	int ret;

	/* don't add the mm_i2c bus unless hw_i2c is enabled */
	if (rec->mm_i2c && (amdgpu_hw_i2c == 0))
		return NULL;

	i2c = kzalloc(sizeof(struct amdgpu_i2c_chan), GFP_KERNEL);
	if (i2c == NULL)
		return NULL;

	i2c->rec = *rec;
	i2c->adapter.owner = THIS_MODULE;
	i2c->adapter.class = I2C_CLASS_DDC;
	i2c->adapter.dev.parent = &dev->pdev->dev;
	i2c->dev = dev;
	i2c_set_adapdata(&i2c->adapter, i2c);
	mutex_init(&i2c->mutex);
	if (rec->hw_capable &&
	    amdgpu_hw_i2c) {
		/* hw i2c using atom */
		snprintf(i2c->adapter.name, sizeof(i2c->adapter.name),
			 "AMDGPU i2c hw bus %s", name);
		i2c->adapter.algo = &amdgpu_atombios_i2c_algo;
		ret = i2c_add_adapter(&i2c->adapter);
		if (ret)
			goto out_free;
	} else {
		/* set the amdgpu bit adapter */
		snprintf(i2c->adapter.name, sizeof(i2c->adapter.name),
			 "AMDGPU i2c bit bus %s", name);
		i2c->adapter.algo_data = &i2c->bit;
		i2c->bit.pre_xfer = amdgpu_i2c_pre_xfer;
		i2c->bit.post_xfer = amdgpu_i2c_post_xfer;
		i2c->bit.setsda = amdgpu_i2c_set_data;
		i2c->bit.setscl = amdgpu_i2c_set_clock;
		i2c->bit.getsda = amdgpu_i2c_get_data;
		i2c->bit.getscl = amdgpu_i2c_get_clock;
		i2c->bit.udelay = 10;
		i2c->bit.timeout = usecs_to_jiffies(2200);	/* from VESA */
		i2c->bit.data = i2c;
		ret = i2c_bit_add_bus(&i2c->adapter);
		if (ret) {
			DRM_ERROR("Failed to register bit i2c %s\n", name);
			goto out_free;
		}
	}

	return i2c;
out_free:
	kfree(i2c);
	return NULL;

}

void amdgpu_i2c_destroy(struct amdgpu_i2c_chan *i2c)
{
	if (!i2c)
		return;
	WARN_ON(i2c->has_aux);
	i2c_del_adapter(&i2c->adapter);
	kfree(i2c);
}

/* Add the default buses */
void amdgpu_i2c_init(struct amdgpu_device *adev)
{
	if (amdgpu_hw_i2c)
		DRM_INFO("hw_i2c forced on, you may experience display detection problems!\n");

	amdgpu_atombios_i2c_init(adev);
}

/* remove all the buses */
void amdgpu_i2c_fini(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < AMDGPU_MAX_I2C_BUS; i++) {
		if (adev->i2c_bus[i]) {
			amdgpu_i2c_destroy(adev->i2c_bus[i]);
			adev->i2c_bus[i] = NULL;
		}
	}
}

/* Add additional buses */
void amdgpu_i2c_add(struct amdgpu_device *adev,
		    const struct amdgpu_i2c_bus_rec *rec,
		    const char *name)
{
	struct drm_device *dev = adev->ddev;
	int i;

	for (i = 0; i < AMDGPU_MAX_I2C_BUS; i++) {
		if (!adev->i2c_bus[i]) {
			adev->i2c_bus[i] = amdgpu_i2c_create(dev, rec, name);
			return;
		}
	}
}

/* looks up bus based on id */
struct amdgpu_i2c_chan *
amdgpu_i2c_lookup(struct amdgpu_device *adev,
		  const struct amdgpu_i2c_bus_rec *i2c_bus)
{
	int i;

	for (i = 0; i < AMDGPU_MAX_I2C_BUS; i++) {
		if (adev->i2c_bus[i] &&
		    (adev->i2c_bus[i]->rec.i2c_id == i2c_bus->i2c_id)) {
			return adev->i2c_bus[i];
		}
	}
	return NULL;
}

static void amdgpu_i2c_get_byte(struct amdgpu_i2c_chan *i2c_bus,
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
		DRM_DEBUG("i2c 0x%02x 0x%02x read failed\n",
			  addr, *val);
	}
}

static void amdgpu_i2c_put_byte(struct amdgpu_i2c_chan *i2c_bus,
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
		DRM_DEBUG("i2c 0x%02x 0x%02x write failed\n",
			  addr, val);
}

/* ddc router switching */
void
amdgpu_i2c_router_select_ddc_port(const struct amdgpu_connector *amdgpu_connector)
{
	u8 val;

	if (!amdgpu_connector->router.ddc_valid)
		return;

	if (!amdgpu_connector->router_bus)
		return;

	amdgpu_i2c_get_byte(amdgpu_connector->router_bus,
			    amdgpu_connector->router.i2c_addr,
			    0x3, &val);
	val &= ~amdgpu_connector->router.ddc_mux_control_pin;
	amdgpu_i2c_put_byte(amdgpu_connector->router_bus,
			    amdgpu_connector->router.i2c_addr,
			    0x3, val);
	amdgpu_i2c_get_byte(amdgpu_connector->router_bus,
			    amdgpu_connector->router.i2c_addr,
			    0x1, &val);
	val &= ~amdgpu_connector->router.ddc_mux_control_pin;
	val |= amdgpu_connector->router.ddc_mux_state;
	amdgpu_i2c_put_byte(amdgpu_connector->router_bus,
			    amdgpu_connector->router.i2c_addr,
			    0x1, val);
}

/* clock/data router switching */
void
amdgpu_i2c_router_select_cd_port(const struct amdgpu_connector *amdgpu_connector)
{
	u8 val;

	if (!amdgpu_connector->router.cd_valid)
		return;

	if (!amdgpu_connector->router_bus)
		return;

	amdgpu_i2c_get_byte(amdgpu_connector->router_bus,
			    amdgpu_connector->router.i2c_addr,
			    0x3, &val);
	val &= ~amdgpu_connector->router.cd_mux_control_pin;
	amdgpu_i2c_put_byte(amdgpu_connector->router_bus,
			    amdgpu_connector->router.i2c_addr,
			    0x3, val);
	amdgpu_i2c_get_byte(amdgpu_connector->router_bus,
			    amdgpu_connector->router.i2c_addr,
			    0x1, &val);
	val &= ~amdgpu_connector->router.cd_mux_control_pin;
	val |= amdgpu_connector->router.cd_mux_state;
	amdgpu_i2c_put_byte(amdgpu_connector->router_bus,
			    amdgpu_connector->router.i2c_addr,
			    0x1, val);
}
