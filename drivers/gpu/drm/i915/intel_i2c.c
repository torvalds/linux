/*
 * Copyright (c) 2006 Dave Airlie <airlied@linux.ie>
 * Copyright © 2006-2008,2010 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	Eric Anholt <eric@anholt.net>
 *	Chris Wilson <chris@chris-wilson.co.uk>
 */
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include "drmP.h"
#include "drm.h"
#include "intel_drv.h"
#include "i915_drm.h"
#include "i915_drv.h"

/* Intel GPIO access functions */

#define I2C_RISEFALL_TIME 20

static inline struct intel_gmbus *
to_intel_gmbus(struct i2c_adapter *i2c)
{
	return container_of(i2c, struct intel_gmbus, adapter);
}

struct intel_gpio {
	struct i2c_adapter adapter;
	struct i2c_algo_bit_data algo;
	struct drm_i915_private *dev_priv;
	u32 reg;
};

void
intel_i2c_reset(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	if (HAS_PCH_SPLIT(dev))
		I915_WRITE(PCH_GMBUS0, 0);
	else
		I915_WRITE(GMBUS0, 0);
}

static void intel_i2c_quirk_set(struct drm_i915_private *dev_priv, bool enable)
{
	u32 val;

	/* When using bit bashing for I2C, this bit needs to be set to 1 */
	if (!IS_PINEVIEW(dev_priv->dev))
		return;

	val = I915_READ(DSPCLK_GATE_D);
	if (enable)
		val |= DPCUNIT_CLOCK_GATE_DISABLE;
	else
		val &= ~DPCUNIT_CLOCK_GATE_DISABLE;
	I915_WRITE(DSPCLK_GATE_D, val);
}

static u32 get_reserved(struct intel_gpio *gpio)
{
	struct drm_i915_private *dev_priv = gpio->dev_priv;
	struct drm_device *dev = dev_priv->dev;
	u32 reserved = 0;

	/* On most chips, these bits must be preserved in software. */
	if (!IS_I830(dev) && !IS_845G(dev))
		reserved = I915_READ(gpio->reg) & (GPIO_DATA_PULLUP_DISABLE |
						   GPIO_CLOCK_PULLUP_DISABLE);

	return reserved;
}

static int get_clock(void *data)
{
	struct intel_gpio *gpio = data;
	struct drm_i915_private *dev_priv = gpio->dev_priv;
	u32 reserved = get_reserved(gpio);
	I915_WRITE(gpio->reg, reserved | GPIO_CLOCK_DIR_MASK);
	I915_WRITE(gpio->reg, reserved);
	return (I915_READ(gpio->reg) & GPIO_CLOCK_VAL_IN) != 0;
}

static int get_data(void *data)
{
	struct intel_gpio *gpio = data;
	struct drm_i915_private *dev_priv = gpio->dev_priv;
	u32 reserved = get_reserved(gpio);
	I915_WRITE(gpio->reg, reserved | GPIO_DATA_DIR_MASK);
	I915_WRITE(gpio->reg, reserved);
	return (I915_READ(gpio->reg) & GPIO_DATA_VAL_IN) != 0;
}

static void set_clock(void *data, int state_high)
{
	struct intel_gpio *gpio = data;
	struct drm_i915_private *dev_priv = gpio->dev_priv;
	u32 reserved = get_reserved(gpio);
	u32 clock_bits;

	if (state_high)
		clock_bits = GPIO_CLOCK_DIR_IN | GPIO_CLOCK_DIR_MASK;
	else
		clock_bits = GPIO_CLOCK_DIR_OUT | GPIO_CLOCK_DIR_MASK |
			GPIO_CLOCK_VAL_MASK;

	I915_WRITE(gpio->reg, reserved | clock_bits);
	POSTING_READ(gpio->reg);
}

static void set_data(void *data, int state_high)
{
	struct intel_gpio *gpio = data;
	struct drm_i915_private *dev_priv = gpio->dev_priv;
	u32 reserved = get_reserved(gpio);
	u32 data_bits;

	if (state_high)
		data_bits = GPIO_DATA_DIR_IN | GPIO_DATA_DIR_MASK;
	else
		data_bits = GPIO_DATA_DIR_OUT | GPIO_DATA_DIR_MASK |
			GPIO_DATA_VAL_MASK;

	I915_WRITE(gpio->reg, reserved | data_bits);
	POSTING_READ(gpio->reg);
}

static struct i2c_adapter *
intel_gpio_create(struct drm_i915_private *dev_priv, u32 pin)
{
	static const int map_pin_to_reg[] = {
		0,
		GPIOB,
		GPIOA,
		GPIOC,
		GPIOD,
		GPIOE,
		0,
		GPIOF,
	};
	struct intel_gpio *gpio;

	if (pin < 1 || pin > 7)
		return NULL;

	gpio = kzalloc(sizeof(struct intel_gpio), GFP_KERNEL);
	if (gpio == NULL)
		return NULL;

	gpio->reg = map_pin_to_reg[pin];
	if (HAS_PCH_SPLIT(dev_priv->dev))
		gpio->reg += PCH_GPIOA - GPIOA;
	gpio->dev_priv = dev_priv;

	snprintf(gpio->adapter.name, I2C_NAME_SIZE, "GPIO%c", "?BACDEF?"[pin]);
	gpio->adapter.owner = THIS_MODULE;
	gpio->adapter.algo_data	= &gpio->algo;
	gpio->adapter.dev.parent = &dev_priv->dev->pdev->dev;
	gpio->algo.setsda = set_data;
	gpio->algo.setscl = set_clock;
	gpio->algo.getsda = get_data;
	gpio->algo.getscl = get_clock;
	gpio->algo.udelay = I2C_RISEFALL_TIME;
	gpio->algo.timeout = usecs_to_jiffies(2200);
	gpio->algo.data = gpio;

	if (i2c_bit_add_bus(&gpio->adapter))
		goto out_free;

	return &gpio->adapter;

out_free:
	kfree(gpio);
	return NULL;
}

static int
intel_i2c_quirk_xfer(struct drm_i915_private *dev_priv,
		     struct i2c_adapter *adapter,
		     struct i2c_msg *msgs,
		     int num)
{
	struct intel_gpio *gpio = container_of(adapter,
					       struct intel_gpio,
					       adapter);
	int ret;

	intel_i2c_reset(dev_priv->dev);

	intel_i2c_quirk_set(dev_priv, true);
	set_data(gpio, 1);
	set_clock(gpio, 1);
	udelay(I2C_RISEFALL_TIME);

	ret = adapter->algo->master_xfer(adapter, msgs, num);

	set_data(gpio, 1);
	set_clock(gpio, 1);
	intel_i2c_quirk_set(dev_priv, false);

	return ret;
}

static int
gmbus_xfer(struct i2c_adapter *adapter,
	   struct i2c_msg *msgs,
	   int num)
{
	struct intel_gmbus *bus = container_of(adapter,
					       struct intel_gmbus,
					       adapter);
	struct drm_i915_private *dev_priv = adapter->algo_data;
	int i, reg_offset;

	if (bus->force_bit)
		return intel_i2c_quirk_xfer(dev_priv,
					    bus->force_bit, msgs, num);

	reg_offset = HAS_PCH_SPLIT(dev_priv->dev) ? PCH_GMBUS0 - GMBUS0 : 0;

	I915_WRITE(GMBUS0 + reg_offset, bus->reg0);

	for (i = 0; i < num; i++) {
		u16 len = msgs[i].len;
		u8 *buf = msgs[i].buf;

		if (msgs[i].flags & I2C_M_RD) {
			I915_WRITE(GMBUS1 + reg_offset,
				   GMBUS_CYCLE_WAIT | (i + 1 == num ? GMBUS_CYCLE_STOP : 0) |
				   (len << GMBUS_BYTE_COUNT_SHIFT) |
				   (msgs[i].addr << GMBUS_SLAVE_ADDR_SHIFT) |
				   GMBUS_SLAVE_READ | GMBUS_SW_RDY);
			POSTING_READ(GMBUS2+reg_offset);
			do {
				u32 val, loop = 0;

				if (wait_for(I915_READ(GMBUS2 + reg_offset) & (GMBUS_SATOER | GMBUS_HW_RDY), 50))
					goto timeout;
				if (I915_READ(GMBUS2 + reg_offset) & GMBUS_SATOER)
					return 0;

				val = I915_READ(GMBUS3 + reg_offset);
				do {
					*buf++ = val & 0xff;
					val >>= 8;
				} while (--len && ++loop < 4);
			} while (len);
		} else {
			u32 val, loop;

			val = loop = 0;
			do {
				val |= *buf++ << (8 * loop);
			} while (--len && ++loop < 4);

			I915_WRITE(GMBUS3 + reg_offset, val);
			I915_WRITE(GMBUS1 + reg_offset,
				   (i + 1 == num ? GMBUS_CYCLE_STOP : GMBUS_CYCLE_WAIT) |
				   (msgs[i].len << GMBUS_BYTE_COUNT_SHIFT) |
				   (msgs[i].addr << GMBUS_SLAVE_ADDR_SHIFT) |
				   GMBUS_SLAVE_WRITE | GMBUS_SW_RDY);
			POSTING_READ(GMBUS2+reg_offset);

			while (len) {
				if (wait_for(I915_READ(GMBUS2 + reg_offset) & (GMBUS_SATOER | GMBUS_HW_RDY), 50))
					goto timeout;
				if (I915_READ(GMBUS2 + reg_offset) & GMBUS_SATOER)
					return 0;

				val = loop = 0;
				do {
					val |= *buf++ << (8 * loop);
				} while (--len && ++loop < 4);

				I915_WRITE(GMBUS3 + reg_offset, val);
				POSTING_READ(GMBUS2+reg_offset);
			}
		}

		if (i + 1 < num && wait_for(I915_READ(GMBUS2 + reg_offset) & (GMBUS_SATOER | GMBUS_HW_WAIT_PHASE), 50))
			goto timeout;
		if (I915_READ(GMBUS2 + reg_offset) & GMBUS_SATOER)
			return 0;
	}

	return num;

timeout:
	DRM_INFO("GMBUS timed out, falling back to bit banging on pin %d [%s]\n",
		 bus->reg0 & 0xff, bus->adapter.name);
	/* Hardware may not support GMBUS over these pins? Try GPIO bitbanging instead. */
	bus->force_bit = intel_gpio_create(dev_priv, bus->reg0 & 0xff);
	if (!bus->force_bit)
		return -ENOMEM;

	return intel_i2c_quirk_xfer(dev_priv, bus->force_bit, msgs, num);
}

static u32 gmbus_func(struct i2c_adapter *adapter)
{
	struct intel_gmbus *bus = container_of(adapter,
					       struct intel_gmbus,
					       adapter);

	if (bus->force_bit)
		bus->force_bit->algo->functionality(bus->force_bit);

	return (I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL |
		/* I2C_FUNC_10BIT_ADDR | */
		I2C_FUNC_SMBUS_READ_BLOCK_DATA |
		I2C_FUNC_SMBUS_BLOCK_PROC_CALL);
}

static const struct i2c_algorithm gmbus_algorithm = {
	.master_xfer	= gmbus_xfer,
	.functionality	= gmbus_func
};

/**
 * intel_gmbus_setup - instantiate all Intel i2c GMBuses
 * @dev: DRM device
 */
int intel_setup_gmbus(struct drm_device *dev)
{
	static const char *names[GMBUS_NUM_PORTS] = {
		"disabled",
		"ssc",
		"vga",
		"panel",
		"dpc",
		"dpb",
		"reserved"
		"dpd",
	};
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret, i;

	dev_priv->gmbus = kcalloc(sizeof(struct intel_gmbus), GMBUS_NUM_PORTS,
				  GFP_KERNEL);
	if (dev_priv->gmbus == NULL)
		return -ENOMEM;

	for (i = 0; i < GMBUS_NUM_PORTS; i++) {
		struct intel_gmbus *bus = &dev_priv->gmbus[i];

		bus->adapter.owner = THIS_MODULE;
		bus->adapter.class = I2C_CLASS_DDC;
		snprintf(bus->adapter.name,
			 I2C_NAME_SIZE,
			 "gmbus %s",
			 names[i]);

		bus->adapter.dev.parent = &dev->pdev->dev;
		bus->adapter.algo_data	= dev_priv;

		bus->adapter.algo = &gmbus_algorithm;
		ret = i2c_add_adapter(&bus->adapter);
		if (ret)
			goto err;

		/* By default use a conservative clock rate */
		bus->reg0 = i | GMBUS_RATE_100KHZ;

		/* XXX force bit banging until GMBUS is fully debugged */
		bus->force_bit = intel_gpio_create(dev_priv, i);
	}

	intel_i2c_reset(dev_priv->dev);

	return 0;

err:
	while (--i) {
		struct intel_gmbus *bus = &dev_priv->gmbus[i];
		i2c_del_adapter(&bus->adapter);
	}
	kfree(dev_priv->gmbus);
	dev_priv->gmbus = NULL;
	return ret;
}

void intel_gmbus_set_speed(struct i2c_adapter *adapter, int speed)
{
	struct intel_gmbus *bus = to_intel_gmbus(adapter);

	/* speed:
	 * 0x0 = 100 KHz
	 * 0x1 = 50 KHz
	 * 0x2 = 400 KHz
	 * 0x3 = 1000 Khz
	 */
	bus->reg0 = (bus->reg0 & ~(0x3 << 8)) | (speed << 8);
}

void intel_gmbus_force_bit(struct i2c_adapter *adapter, bool force_bit)
{
	struct intel_gmbus *bus = to_intel_gmbus(adapter);

	if (force_bit) {
		if (bus->force_bit == NULL) {
			struct drm_i915_private *dev_priv = adapter->algo_data;
			bus->force_bit = intel_gpio_create(dev_priv,
							   bus->reg0 & 0xff);
		}
	} else {
		if (bus->force_bit) {
			i2c_del_adapter(bus->force_bit);
			kfree(bus->force_bit);
			bus->force_bit = NULL;
		}
	}
}

void intel_teardown_gmbus(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;

	if (dev_priv->gmbus == NULL)
		return;

	for (i = 0; i < GMBUS_NUM_PORTS; i++) {
		struct intel_gmbus *bus = &dev_priv->gmbus[i];
		if (bus->force_bit) {
			i2c_del_adapter(bus->force_bit);
			kfree(bus->force_bit);
		}
		i2c_del_adapter(&bus->adapter);
	}

	kfree(dev_priv->gmbus);
	dev_priv->gmbus = NULL;
}
