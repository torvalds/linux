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
#include <linux/export.h>
#include <drm/drmP.h>
#include "intel_drv.h"
#include <drm/i915_drm.h>
#include "i915_drv.h"

struct gmbus_pin {
	const char *name;
	i915_reg_t reg;
};

/* Map gmbus pin pairs to names and registers. */
static const struct gmbus_pin gmbus_pins[] = {
	[GMBUS_PIN_SSC] = { "ssc", GPIOB },
	[GMBUS_PIN_VGADDC] = { "vga", GPIOA },
	[GMBUS_PIN_PANEL] = { "panel", GPIOC },
	[GMBUS_PIN_DPC] = { "dpc", GPIOD },
	[GMBUS_PIN_DPB] = { "dpb", GPIOE },
	[GMBUS_PIN_DPD] = { "dpd", GPIOF },
};

static const struct gmbus_pin gmbus_pins_bdw[] = {
	[GMBUS_PIN_VGADDC] = { "vga", GPIOA },
	[GMBUS_PIN_DPC] = { "dpc", GPIOD },
	[GMBUS_PIN_DPB] = { "dpb", GPIOE },
	[GMBUS_PIN_DPD] = { "dpd", GPIOF },
};

static const struct gmbus_pin gmbus_pins_skl[] = {
	[GMBUS_PIN_DPC] = { "dpc", GPIOD },
	[GMBUS_PIN_DPB] = { "dpb", GPIOE },
	[GMBUS_PIN_DPD] = { "dpd", GPIOF },
};

static const struct gmbus_pin gmbus_pins_bxt[] = {
	[GMBUS_PIN_1_BXT] = { "dpb", GPIOB },
	[GMBUS_PIN_2_BXT] = { "dpc", GPIOC },
	[GMBUS_PIN_3_BXT] = { "misc", GPIOD },
};

/* pin is expected to be valid */
static const struct gmbus_pin *get_gmbus_pin(struct drm_i915_private *dev_priv,
					     unsigned int pin)
{
	if (IS_BROXTON(dev_priv))
		return &gmbus_pins_bxt[pin];
	else if (IS_SKYLAKE(dev_priv) || IS_KABYLAKE(dev_priv))
		return &gmbus_pins_skl[pin];
	else if (IS_BROADWELL(dev_priv))
		return &gmbus_pins_bdw[pin];
	else
		return &gmbus_pins[pin];
}

bool intel_gmbus_is_valid_pin(struct drm_i915_private *dev_priv,
			      unsigned int pin)
{
	unsigned int size;

	if (IS_BROXTON(dev_priv))
		size = ARRAY_SIZE(gmbus_pins_bxt);
	else if (IS_SKYLAKE(dev_priv) || IS_KABYLAKE(dev_priv))
		size = ARRAY_SIZE(gmbus_pins_skl);
	else if (IS_BROADWELL(dev_priv))
		size = ARRAY_SIZE(gmbus_pins_bdw);
	else
		size = ARRAY_SIZE(gmbus_pins);

	return pin < size &&
		i915_mmio_reg_valid(get_gmbus_pin(dev_priv, pin)->reg);
}

/* Intel GPIO access functions */

#define I2C_RISEFALL_TIME 10

static inline struct intel_gmbus *
to_intel_gmbus(struct i2c_adapter *i2c)
{
	return container_of(i2c, struct intel_gmbus, adapter);
}

void
intel_i2c_reset(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	I915_WRITE(GMBUS0, 0);
	I915_WRITE(GMBUS4, 0);
}

static void intel_i2c_quirk_set(struct drm_i915_private *dev_priv, bool enable)
{
	u32 val;

	/* When using bit bashing for I2C, this bit needs to be set to 1 */
	if (!IS_PINEVIEW(dev_priv))
		return;

	val = I915_READ(DSPCLK_GATE_D);
	if (enable)
		val |= DPCUNIT_CLOCK_GATE_DISABLE;
	else
		val &= ~DPCUNIT_CLOCK_GATE_DISABLE;
	I915_WRITE(DSPCLK_GATE_D, val);
}

static u32 get_reserved(struct intel_gmbus *bus)
{
	struct drm_i915_private *dev_priv = bus->dev_priv;
	struct drm_device *dev = dev_priv->dev;
	u32 reserved = 0;

	/* On most chips, these bits must be preserved in software. */
	if (!IS_I830(dev) && !IS_845G(dev))
		reserved = I915_READ_NOTRACE(bus->gpio_reg) &
					     (GPIO_DATA_PULLUP_DISABLE |
					      GPIO_CLOCK_PULLUP_DISABLE);

	return reserved;
}

static int get_clock(void *data)
{
	struct intel_gmbus *bus = data;
	struct drm_i915_private *dev_priv = bus->dev_priv;
	u32 reserved = get_reserved(bus);
	I915_WRITE_NOTRACE(bus->gpio_reg, reserved | GPIO_CLOCK_DIR_MASK);
	I915_WRITE_NOTRACE(bus->gpio_reg, reserved);
	return (I915_READ_NOTRACE(bus->gpio_reg) & GPIO_CLOCK_VAL_IN) != 0;
}

static int get_data(void *data)
{
	struct intel_gmbus *bus = data;
	struct drm_i915_private *dev_priv = bus->dev_priv;
	u32 reserved = get_reserved(bus);
	I915_WRITE_NOTRACE(bus->gpio_reg, reserved | GPIO_DATA_DIR_MASK);
	I915_WRITE_NOTRACE(bus->gpio_reg, reserved);
	return (I915_READ_NOTRACE(bus->gpio_reg) & GPIO_DATA_VAL_IN) != 0;
}

static void set_clock(void *data, int state_high)
{
	struct intel_gmbus *bus = data;
	struct drm_i915_private *dev_priv = bus->dev_priv;
	u32 reserved = get_reserved(bus);
	u32 clock_bits;

	if (state_high)
		clock_bits = GPIO_CLOCK_DIR_IN | GPIO_CLOCK_DIR_MASK;
	else
		clock_bits = GPIO_CLOCK_DIR_OUT | GPIO_CLOCK_DIR_MASK |
			GPIO_CLOCK_VAL_MASK;

	I915_WRITE_NOTRACE(bus->gpio_reg, reserved | clock_bits);
	POSTING_READ(bus->gpio_reg);
}

static void set_data(void *data, int state_high)
{
	struct intel_gmbus *bus = data;
	struct drm_i915_private *dev_priv = bus->dev_priv;
	u32 reserved = get_reserved(bus);
	u32 data_bits;

	if (state_high)
		data_bits = GPIO_DATA_DIR_IN | GPIO_DATA_DIR_MASK;
	else
		data_bits = GPIO_DATA_DIR_OUT | GPIO_DATA_DIR_MASK |
			GPIO_DATA_VAL_MASK;

	I915_WRITE_NOTRACE(bus->gpio_reg, reserved | data_bits);
	POSTING_READ(bus->gpio_reg);
}

static int
intel_gpio_pre_xfer(struct i2c_adapter *adapter)
{
	struct intel_gmbus *bus = container_of(adapter,
					       struct intel_gmbus,
					       adapter);
	struct drm_i915_private *dev_priv = bus->dev_priv;

	intel_i2c_reset(dev_priv->dev);
	intel_i2c_quirk_set(dev_priv, true);
	set_data(bus, 1);
	set_clock(bus, 1);
	udelay(I2C_RISEFALL_TIME);
	return 0;
}

static void
intel_gpio_post_xfer(struct i2c_adapter *adapter)
{
	struct intel_gmbus *bus = container_of(adapter,
					       struct intel_gmbus,
					       adapter);
	struct drm_i915_private *dev_priv = bus->dev_priv;

	set_data(bus, 1);
	set_clock(bus, 1);
	intel_i2c_quirk_set(dev_priv, false);
}

static void
intel_gpio_setup(struct intel_gmbus *bus, unsigned int pin)
{
	struct drm_i915_private *dev_priv = bus->dev_priv;
	struct i2c_algo_bit_data *algo;

	algo = &bus->bit_algo;

	bus->gpio_reg = _MMIO(dev_priv->gpio_mmio_base +
			      i915_mmio_reg_offset(get_gmbus_pin(dev_priv, pin)->reg));
	bus->adapter.algo_data = algo;
	algo->setsda = set_data;
	algo->setscl = set_clock;
	algo->getsda = get_data;
	algo->getscl = get_clock;
	algo->pre_xfer = intel_gpio_pre_xfer;
	algo->post_xfer = intel_gpio_post_xfer;
	algo->udelay = I2C_RISEFALL_TIME;
	algo->timeout = usecs_to_jiffies(2200);
	algo->data = bus;
}

static int
gmbus_wait_hw_status(struct drm_i915_private *dev_priv,
		     u32 gmbus2_status,
		     u32 gmbus4_irq_en)
{
	int i;
	u32 gmbus2 = 0;
	DEFINE_WAIT(wait);

	if (!HAS_GMBUS_IRQ(dev_priv))
		gmbus4_irq_en = 0;

	/* Important: The hw handles only the first bit, so set only one! Since
	 * we also need to check for NAKs besides the hw ready/idle signal, we
	 * need to wake up periodically and check that ourselves. */
	I915_WRITE(GMBUS4, gmbus4_irq_en);

	for (i = 0; i < msecs_to_jiffies_timeout(50); i++) {
		prepare_to_wait(&dev_priv->gmbus_wait_queue, &wait,
				TASK_UNINTERRUPTIBLE);

		gmbus2 = I915_READ_NOTRACE(GMBUS2);
		if (gmbus2 & (GMBUS_SATOER | gmbus2_status))
			break;

		schedule_timeout(1);
	}
	finish_wait(&dev_priv->gmbus_wait_queue, &wait);

	I915_WRITE(GMBUS4, 0);

	if (gmbus2 & GMBUS_SATOER)
		return -ENXIO;
	if (gmbus2 & gmbus2_status)
		return 0;
	return -ETIMEDOUT;
}

static int
gmbus_wait_idle(struct drm_i915_private *dev_priv)
{
	int ret;

#define C ((I915_READ_NOTRACE(GMBUS2) & GMBUS_ACTIVE) == 0)

	if (!HAS_GMBUS_IRQ(dev_priv))
		return wait_for(C, 10);

	/* Important: The hw handles only the first bit, so set only one! */
	I915_WRITE(GMBUS4, GMBUS_IDLE_EN);

	ret = wait_event_timeout(dev_priv->gmbus_wait_queue, C,
				 msecs_to_jiffies_timeout(10));

	I915_WRITE(GMBUS4, 0);

	if (ret)
		return 0;
	else
		return -ETIMEDOUT;
#undef C
}

static int
gmbus_xfer_read_chunk(struct drm_i915_private *dev_priv,
		      unsigned short addr, u8 *buf, unsigned int len,
		      u32 gmbus1_index)
{
	I915_WRITE(GMBUS1,
		   gmbus1_index |
		   GMBUS_CYCLE_WAIT |
		   (len << GMBUS_BYTE_COUNT_SHIFT) |
		   (addr << GMBUS_SLAVE_ADDR_SHIFT) |
		   GMBUS_SLAVE_READ | GMBUS_SW_RDY);
	while (len) {
		int ret;
		u32 val, loop = 0;

		ret = gmbus_wait_hw_status(dev_priv, GMBUS_HW_RDY,
					   GMBUS_HW_RDY_EN);
		if (ret)
			return ret;

		val = I915_READ(GMBUS3);
		do {
			*buf++ = val & 0xff;
			val >>= 8;
		} while (--len && ++loop < 4);
	}

	return 0;
}

static int
gmbus_xfer_read(struct drm_i915_private *dev_priv, struct i2c_msg *msg,
		u32 gmbus1_index)
{
	u8 *buf = msg->buf;
	unsigned int rx_size = msg->len;
	unsigned int len;
	int ret;

	do {
		len = min(rx_size, GMBUS_BYTE_COUNT_MAX);

		ret = gmbus_xfer_read_chunk(dev_priv, msg->addr,
					    buf, len, gmbus1_index);
		if (ret)
			return ret;

		rx_size -= len;
		buf += len;
	} while (rx_size != 0);

	return 0;
}

static int
gmbus_xfer_write_chunk(struct drm_i915_private *dev_priv,
		       unsigned short addr, u8 *buf, unsigned int len)
{
	unsigned int chunk_size = len;
	u32 val, loop;

	val = loop = 0;
	while (len && loop < 4) {
		val |= *buf++ << (8 * loop++);
		len -= 1;
	}

	I915_WRITE(GMBUS3, val);
	I915_WRITE(GMBUS1,
		   GMBUS_CYCLE_WAIT |
		   (chunk_size << GMBUS_BYTE_COUNT_SHIFT) |
		   (addr << GMBUS_SLAVE_ADDR_SHIFT) |
		   GMBUS_SLAVE_WRITE | GMBUS_SW_RDY);
	while (len) {
		int ret;

		val = loop = 0;
		do {
			val |= *buf++ << (8 * loop);
		} while (--len && ++loop < 4);

		I915_WRITE(GMBUS3, val);

		ret = gmbus_wait_hw_status(dev_priv, GMBUS_HW_RDY,
					   GMBUS_HW_RDY_EN);
		if (ret)
			return ret;
	}

	return 0;
}

static int
gmbus_xfer_write(struct drm_i915_private *dev_priv, struct i2c_msg *msg)
{
	u8 *buf = msg->buf;
	unsigned int tx_size = msg->len;
	unsigned int len;
	int ret;

	do {
		len = min(tx_size, GMBUS_BYTE_COUNT_MAX);

		ret = gmbus_xfer_write_chunk(dev_priv, msg->addr, buf, len);
		if (ret)
			return ret;

		buf += len;
		tx_size -= len;
	} while (tx_size != 0);

	return 0;
}

/*
 * The gmbus controller can combine a 1 or 2 byte write with a read that
 * immediately follows it by using an "INDEX" cycle.
 */
static bool
gmbus_is_index_read(struct i2c_msg *msgs, int i, int num)
{
	return (i + 1 < num &&
		!(msgs[i].flags & I2C_M_RD) && msgs[i].len <= 2 &&
		(msgs[i + 1].flags & I2C_M_RD));
}

static int
gmbus_xfer_index_read(struct drm_i915_private *dev_priv, struct i2c_msg *msgs)
{
	u32 gmbus1_index = 0;
	u32 gmbus5 = 0;
	int ret;

	if (msgs[0].len == 2)
		gmbus5 = GMBUS_2BYTE_INDEX_EN |
			 msgs[0].buf[1] | (msgs[0].buf[0] << 8);
	if (msgs[0].len == 1)
		gmbus1_index = GMBUS_CYCLE_INDEX |
			       (msgs[0].buf[0] << GMBUS_SLAVE_INDEX_SHIFT);

	/* GMBUS5 holds 16-bit index */
	if (gmbus5)
		I915_WRITE(GMBUS5, gmbus5);

	ret = gmbus_xfer_read(dev_priv, &msgs[1], gmbus1_index);

	/* Clear GMBUS5 after each index transfer */
	if (gmbus5)
		I915_WRITE(GMBUS5, 0);

	return ret;
}

static int
do_gmbus_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs, int num)
{
	struct intel_gmbus *bus = container_of(adapter,
					       struct intel_gmbus,
					       adapter);
	struct drm_i915_private *dev_priv = bus->dev_priv;
	int i = 0, inc, try = 0;
	int ret = 0;

retry:
	I915_WRITE(GMBUS0, bus->reg0);

	for (; i < num; i += inc) {
		inc = 1;
		if (gmbus_is_index_read(msgs, i, num)) {
			ret = gmbus_xfer_index_read(dev_priv, &msgs[i]);
			inc = 2; /* an index read is two msgs */
		} else if (msgs[i].flags & I2C_M_RD) {
			ret = gmbus_xfer_read(dev_priv, &msgs[i], 0);
		} else {
			ret = gmbus_xfer_write(dev_priv, &msgs[i]);
		}

		if (!ret)
			ret = gmbus_wait_hw_status(dev_priv, GMBUS_HW_WAIT_PHASE,
						   GMBUS_HW_WAIT_EN);
		if (ret == -ETIMEDOUT)
			goto timeout;
		else if (ret)
			goto clear_err;
	}

	/* Generate a STOP condition on the bus. Note that gmbus can't generata
	 * a STOP on the very first cycle. To simplify the code we
	 * unconditionally generate the STOP condition with an additional gmbus
	 * cycle. */
	I915_WRITE(GMBUS1, GMBUS_CYCLE_STOP | GMBUS_SW_RDY);

	/* Mark the GMBUS interface as disabled after waiting for idle.
	 * We will re-enable it at the start of the next xfer,
	 * till then let it sleep.
	 */
	if (gmbus_wait_idle(dev_priv)) {
		DRM_DEBUG_KMS("GMBUS [%s] timed out waiting for idle\n",
			 adapter->name);
		ret = -ETIMEDOUT;
	}
	I915_WRITE(GMBUS0, 0);
	ret = ret ?: i;
	goto out;

clear_err:
	/*
	 * Wait for bus to IDLE before clearing NAK.
	 * If we clear the NAK while bus is still active, then it will stay
	 * active and the next transaction may fail.
	 *
	 * If no ACK is received during the address phase of a transaction, the
	 * adapter must report -ENXIO. It is not clear what to return if no ACK
	 * is received at other times. But we have to be careful to not return
	 * spurious -ENXIO because that will prevent i2c and drm edid functions
	 * from retrying. So return -ENXIO only when gmbus properly quiescents -
	 * timing out seems to happen when there _is_ a ddc chip present, but
	 * it's slow responding and only answers on the 2nd retry.
	 */
	ret = -ENXIO;
	if (gmbus_wait_idle(dev_priv)) {
		DRM_DEBUG_KMS("GMBUS [%s] timed out after NAK\n",
			      adapter->name);
		ret = -ETIMEDOUT;
	}

	/* Toggle the Software Clear Interrupt bit. This has the effect
	 * of resetting the GMBUS controller and so clearing the
	 * BUS_ERROR raised by the slave's NAK.
	 */
	I915_WRITE(GMBUS1, GMBUS_SW_CLR_INT);
	I915_WRITE(GMBUS1, 0);
	I915_WRITE(GMBUS0, 0);

	DRM_DEBUG_KMS("GMBUS [%s] NAK for addr: %04x %c(%d)\n",
			 adapter->name, msgs[i].addr,
			 (msgs[i].flags & I2C_M_RD) ? 'r' : 'w', msgs[i].len);

	/*
	 * Passive adapters sometimes NAK the first probe. Retry the first
	 * message once on -ENXIO for GMBUS transfers; the bit banging algorithm
	 * has retries internally. See also the retry loop in
	 * drm_do_probe_ddc_edid, which bails out on the first -ENXIO.
	 */
	if (ret == -ENXIO && i == 0 && try++ == 0) {
		DRM_DEBUG_KMS("GMBUS [%s] NAK on first message, retry\n",
			      adapter->name);
		goto retry;
	}

	goto out;

timeout:
	DRM_DEBUG_KMS("GMBUS [%s] timed out, falling back to bit banging on pin %d\n",
		      bus->adapter.name, bus->reg0 & 0xff);
	I915_WRITE(GMBUS0, 0);

	/*
	 * Hardware may not support GMBUS over these pins? Try GPIO bitbanging
	 * instead. Use EAGAIN to have i2c core retry.
	 */
	ret = -EAGAIN;

out:
	return ret;
}

static int
gmbus_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs, int num)
{
	struct intel_gmbus *bus = container_of(adapter, struct intel_gmbus,
					       adapter);
	struct drm_i915_private *dev_priv = bus->dev_priv;
	int ret;

	intel_display_power_get(dev_priv, POWER_DOMAIN_GMBUS);
	mutex_lock(&dev_priv->gmbus_mutex);

	if (bus->force_bit) {
		ret = i2c_bit_algo.master_xfer(adapter, msgs, num);
		if (ret < 0)
			bus->force_bit &= ~GMBUS_FORCE_BIT_RETRY;
	} else {
		ret = do_gmbus_xfer(adapter, msgs, num);
		if (ret == -EAGAIN)
			bus->force_bit |= GMBUS_FORCE_BIT_RETRY;
	}

	mutex_unlock(&dev_priv->gmbus_mutex);
	intel_display_power_put(dev_priv, POWER_DOMAIN_GMBUS);

	return ret;
}

static u32 gmbus_func(struct i2c_adapter *adapter)
{
	return i2c_bit_algo.functionality(adapter) &
		(I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL |
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
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_gmbus *bus;
	unsigned int pin;
	int ret;

	if (HAS_PCH_NOP(dev))
		return 0;

	if (IS_VALLEYVIEW(dev) || IS_CHERRYVIEW(dev))
		dev_priv->gpio_mmio_base = VLV_DISPLAY_BASE;
	else if (!HAS_GMCH_DISPLAY(dev_priv))
		dev_priv->gpio_mmio_base =
			i915_mmio_reg_offset(PCH_GPIOA) -
			i915_mmio_reg_offset(GPIOA);

	mutex_init(&dev_priv->gmbus_mutex);
	init_waitqueue_head(&dev_priv->gmbus_wait_queue);

	for (pin = 0; pin < ARRAY_SIZE(dev_priv->gmbus); pin++) {
		if (!intel_gmbus_is_valid_pin(dev_priv, pin))
			continue;

		bus = &dev_priv->gmbus[pin];

		bus->adapter.owner = THIS_MODULE;
		bus->adapter.class = I2C_CLASS_DDC;
		snprintf(bus->adapter.name,
			 sizeof(bus->adapter.name),
			 "i915 gmbus %s",
			 get_gmbus_pin(dev_priv, pin)->name);

		bus->adapter.dev.parent = &dev->pdev->dev;
		bus->dev_priv = dev_priv;

		bus->adapter.algo = &gmbus_algorithm;

		/*
		 * We wish to retry with bit banging
		 * after a timed out GMBUS attempt.
		 */
		bus->adapter.retries = 1;

		/* By default use a conservative clock rate */
		bus->reg0 = pin | GMBUS_RATE_100KHZ;

		/* gmbus seems to be broken on i830 */
		if (IS_I830(dev))
			bus->force_bit = 1;

		intel_gpio_setup(bus, pin);

		ret = i2c_add_adapter(&bus->adapter);
		if (ret)
			goto err;
	}

	intel_i2c_reset(dev_priv->dev);

	return 0;

err:
	while (pin--) {
		if (!intel_gmbus_is_valid_pin(dev_priv, pin))
			continue;

		bus = &dev_priv->gmbus[pin];
		i2c_del_adapter(&bus->adapter);
	}
	return ret;
}

struct i2c_adapter *intel_gmbus_get_adapter(struct drm_i915_private *dev_priv,
					    unsigned int pin)
{
	if (WARN_ON(!intel_gmbus_is_valid_pin(dev_priv, pin)))
		return NULL;

	return &dev_priv->gmbus[pin].adapter;
}

void intel_gmbus_set_speed(struct i2c_adapter *adapter, int speed)
{
	struct intel_gmbus *bus = to_intel_gmbus(adapter);

	bus->reg0 = (bus->reg0 & ~(0x3 << 8)) | speed;
}

void intel_gmbus_force_bit(struct i2c_adapter *adapter, bool force_bit)
{
	struct intel_gmbus *bus = to_intel_gmbus(adapter);
	struct drm_i915_private *dev_priv = bus->dev_priv;

	mutex_lock(&dev_priv->gmbus_mutex);

	bus->force_bit += force_bit ? 1 : -1;
	DRM_DEBUG_KMS("%sabling bit-banging on %s. force bit now %d\n",
		      force_bit ? "en" : "dis", adapter->name,
		      bus->force_bit);

	mutex_unlock(&dev_priv->gmbus_mutex);
}

void intel_teardown_gmbus(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_gmbus *bus;
	unsigned int pin;

	for (pin = 0; pin < ARRAY_SIZE(dev_priv->gmbus); pin++) {
		if (!intel_gmbus_is_valid_pin(dev_priv, pin))
			continue;

		bus = &dev_priv->gmbus[pin];
		i2c_del_adapter(&bus->adapter);
	}
}
