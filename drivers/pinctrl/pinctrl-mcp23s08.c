// SPDX-License-Identifier: GPL-2.0-only
/* MCP23S08 SPI/I2C GPIO driver */

#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/export.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/consumer.h>
#include <linux/slab.h>
#include <asm/byteorder.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>

#include "pinctrl-mcp23s08.h"

/* Registers are all 8 bits wide.
 *
 * The mcp23s17 has twice as many bits, and can be configured to work
 * with either 16 bit registers or with two adjacent 8 bit banks.
 */
#define MCP_IODIR	0x00		/* init/reset:  all ones */
#define MCP_IPOL	0x01
#define MCP_GPINTEN	0x02
#define MCP_DEFVAL	0x03
#define MCP_INTCON	0x04
#define MCP_IOCON	0x05
#	define IOCON_MIRROR	(1 << 6)
#	define IOCON_SEQOP	(1 << 5)
#	define IOCON_HAEN	(1 << 3)
#	define IOCON_ODR	(1 << 2)
#	define IOCON_INTPOL	(1 << 1)
#	define IOCON_INTCC	(1)
#define MCP_GPPU	0x06
#define MCP_INTF	0x07
#define MCP_INTCAP	0x08
#define MCP_GPIO	0x09
#define MCP_OLAT	0x0a

static const struct reg_default mcp23x08_defaults[] = {
	{.reg = MCP_IODIR,		.def = 0xff},
	{.reg = MCP_IPOL,		.def = 0x00},
	{.reg = MCP_GPINTEN,		.def = 0x00},
	{.reg = MCP_DEFVAL,		.def = 0x00},
	{.reg = MCP_INTCON,		.def = 0x00},
	{.reg = MCP_IOCON,		.def = 0x00},
	{.reg = MCP_GPPU,		.def = 0x00},
	{.reg = MCP_OLAT,		.def = 0x00},
};

static const struct regmap_range mcp23x08_volatile_range = {
	.range_min = MCP_INTF,
	.range_max = MCP_GPIO,
};

static const struct regmap_access_table mcp23x08_volatile_table = {
	.yes_ranges = &mcp23x08_volatile_range,
	.n_yes_ranges = 1,
};

static const struct regmap_range mcp23x08_precious_range = {
	.range_min = MCP_GPIO,
	.range_max = MCP_GPIO,
};

static const struct regmap_access_table mcp23x08_precious_table = {
	.yes_ranges = &mcp23x08_precious_range,
	.n_yes_ranges = 1,
};

const struct regmap_config mcp23x08_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.reg_stride = 1,
	.volatile_table = &mcp23x08_volatile_table,
	.precious_table = &mcp23x08_precious_table,
	.reg_defaults = mcp23x08_defaults,
	.num_reg_defaults = ARRAY_SIZE(mcp23x08_defaults),
	.cache_type = REGCACHE_FLAT,
	.max_register = MCP_OLAT,
};
EXPORT_SYMBOL_GPL(mcp23x08_regmap);

static const struct reg_default mcp23x17_defaults[] = {
	{.reg = MCP_IODIR << 1,		.def = 0xffff},
	{.reg = MCP_IPOL << 1,		.def = 0x0000},
	{.reg = MCP_GPINTEN << 1,	.def = 0x0000},
	{.reg = MCP_DEFVAL << 1,	.def = 0x0000},
	{.reg = MCP_INTCON << 1,	.def = 0x0000},
	{.reg = MCP_IOCON << 1,		.def = 0x0000},
	{.reg = MCP_GPPU << 1,		.def = 0x0000},
	{.reg = MCP_OLAT << 1,		.def = 0x0000},
};

static const struct regmap_range mcp23x17_volatile_range = {
	.range_min = MCP_INTF << 1,
	.range_max = MCP_GPIO << 1,
};

static const struct regmap_access_table mcp23x17_volatile_table = {
	.yes_ranges = &mcp23x17_volatile_range,
	.n_yes_ranges = 1,
};

static const struct regmap_range mcp23x17_precious_range = {
	.range_min = MCP_INTCAP << 1,
	.range_max = MCP_GPIO << 1,
};

static const struct regmap_access_table mcp23x17_precious_table = {
	.yes_ranges = &mcp23x17_precious_range,
	.n_yes_ranges = 1,
};

const struct regmap_config mcp23x17_regmap = {
	.reg_bits = 8,
	.val_bits = 16,

	.reg_stride = 2,
	.max_register = MCP_OLAT << 1,
	.volatile_table = &mcp23x17_volatile_table,
	.precious_table = &mcp23x17_precious_table,
	.reg_defaults = mcp23x17_defaults,
	.num_reg_defaults = ARRAY_SIZE(mcp23x17_defaults),
	.cache_type = REGCACHE_FLAT,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
};
EXPORT_SYMBOL_GPL(mcp23x17_regmap);

static int mcp_read(struct mcp23s08 *mcp, unsigned int reg, unsigned int *val)
{
	return regmap_read(mcp->regmap, reg << mcp->reg_shift, val);
}

static int mcp_write(struct mcp23s08 *mcp, unsigned int reg, unsigned int val)
{
	return regmap_write(mcp->regmap, reg << mcp->reg_shift, val);
}

static int mcp_set_mask(struct mcp23s08 *mcp, unsigned int reg,
		       unsigned int mask, bool enabled)
{
	u16 val  = enabled ? 0xffff : 0x0000;
	return regmap_update_bits(mcp->regmap, reg << mcp->reg_shift,
				  mask, val);
}

static int mcp_set_bit(struct mcp23s08 *mcp, unsigned int reg,
		       unsigned int pin, bool enabled)
{
	u16 mask = BIT(pin);
	return mcp_set_mask(mcp, reg, mask, enabled);
}

static const struct pinctrl_pin_desc mcp23x08_pins[] = {
	PINCTRL_PIN(0, "gpio0"),
	PINCTRL_PIN(1, "gpio1"),
	PINCTRL_PIN(2, "gpio2"),
	PINCTRL_PIN(3, "gpio3"),
	PINCTRL_PIN(4, "gpio4"),
	PINCTRL_PIN(5, "gpio5"),
	PINCTRL_PIN(6, "gpio6"),
	PINCTRL_PIN(7, "gpio7"),
};

static const struct pinctrl_pin_desc mcp23x17_pins[] = {
	PINCTRL_PIN(0, "gpio0"),
	PINCTRL_PIN(1, "gpio1"),
	PINCTRL_PIN(2, "gpio2"),
	PINCTRL_PIN(3, "gpio3"),
	PINCTRL_PIN(4, "gpio4"),
	PINCTRL_PIN(5, "gpio5"),
	PINCTRL_PIN(6, "gpio6"),
	PINCTRL_PIN(7, "gpio7"),
	PINCTRL_PIN(8, "gpio8"),
	PINCTRL_PIN(9, "gpio9"),
	PINCTRL_PIN(10, "gpio10"),
	PINCTRL_PIN(11, "gpio11"),
	PINCTRL_PIN(12, "gpio12"),
	PINCTRL_PIN(13, "gpio13"),
	PINCTRL_PIN(14, "gpio14"),
	PINCTRL_PIN(15, "gpio15"),
};

static int mcp_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	return 0;
}

static const char *mcp_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						unsigned int group)
{
	return NULL;
}

static int mcp_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
					unsigned int group,
					const unsigned int **pins,
					unsigned int *num_pins)
{
	return -ENOTSUPP;
}

static const struct pinctrl_ops mcp_pinctrl_ops = {
	.get_groups_count = mcp_pinctrl_get_groups_count,
	.get_group_name = mcp_pinctrl_get_group_name,
	.get_group_pins = mcp_pinctrl_get_group_pins,
#ifdef CONFIG_OF
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.dt_free_map = pinconf_generic_dt_free_map,
#endif
};

static int mcp_pinconf_get(struct pinctrl_dev *pctldev, unsigned int pin,
			      unsigned long *config)
{
	struct mcp23s08 *mcp = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	unsigned int data, status;
	int ret;

	switch (param) {
	case PIN_CONFIG_BIAS_PULL_UP:
		ret = mcp_read(mcp, MCP_GPPU, &data);
		if (ret < 0)
			return ret;
		status = (data & BIT(pin)) ? 1 : 0;
		break;
	default:
		return -ENOTSUPP;
	}

	*config = 0;

	return status ? 0 : -EINVAL;
}

static int mcp_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
			      unsigned long *configs, unsigned int num_configs)
{
	struct mcp23s08 *mcp = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param;
	u32 arg;
	int ret = 0;
	int i;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_PULL_UP:
			ret = mcp_set_bit(mcp, MCP_GPPU, pin, arg);
			break;
		default:
			dev_dbg(mcp->dev, "Invalid config param %04x\n", param);
			return -ENOTSUPP;
		}
	}

	return ret;
}

static const struct pinconf_ops mcp_pinconf_ops = {
	.pin_config_get = mcp_pinconf_get,
	.pin_config_set = mcp_pinconf_set,
	.is_generic = true,
};

/*----------------------------------------------------------------------*/

static int mcp23s08_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct mcp23s08	*mcp = gpiochip_get_data(chip);
	int status;

	mutex_lock(&mcp->lock);
	status = mcp_set_bit(mcp, MCP_IODIR, offset, true);
	mutex_unlock(&mcp->lock);

	return status;
}

static int mcp23s08_get(struct gpio_chip *chip, unsigned offset)
{
	struct mcp23s08	*mcp = gpiochip_get_data(chip);
	int status, ret;

	mutex_lock(&mcp->lock);

	/* REVISIT reading this clears any IRQ ... */
	ret = mcp_read(mcp, MCP_GPIO, &status);
	if (ret < 0)
		status = 0;
	else {
		mcp->cached_gpio = status;
		status = !!(status & (1 << offset));
	}

	mutex_unlock(&mcp->lock);
	return status;
}

static int __mcp23s08_set(struct mcp23s08 *mcp, unsigned mask, bool value)
{
	return mcp_set_mask(mcp, MCP_OLAT, mask, value);
}

static void mcp23s08_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct mcp23s08	*mcp = gpiochip_get_data(chip);
	unsigned mask = BIT(offset);

	mutex_lock(&mcp->lock);
	__mcp23s08_set(mcp, mask, !!value);
	mutex_unlock(&mcp->lock);
}

static int
mcp23s08_direction_output(struct gpio_chip *chip, unsigned offset, int value)
{
	struct mcp23s08	*mcp = gpiochip_get_data(chip);
	unsigned mask = BIT(offset);
	int status;

	mutex_lock(&mcp->lock);
	status = __mcp23s08_set(mcp, mask, value);
	if (status == 0) {
		status = mcp_set_mask(mcp, MCP_IODIR, mask, false);
	}
	mutex_unlock(&mcp->lock);
	return status;
}

/*----------------------------------------------------------------------*/
static irqreturn_t mcp23s08_irq(int irq, void *data)
{
	struct mcp23s08 *mcp = data;
	int intcap, intcon, intf, i, gpio, gpio_orig, intcap_mask, defval;
	unsigned int child_irq;
	bool intf_set, intcap_changed, gpio_bit_changed,
		defval_changed, gpio_set;

	mutex_lock(&mcp->lock);
	if (mcp_read(mcp, MCP_INTF, &intf))
		goto unlock;

	if (intf == 0) {
		/* There is no interrupt pending */
		goto unlock;
	}

	if (mcp_read(mcp, MCP_INTCAP, &intcap))
		goto unlock;

	if (mcp_read(mcp, MCP_INTCON, &intcon))
		goto unlock;

	if (mcp_read(mcp, MCP_DEFVAL, &defval))
		goto unlock;

	/* This clears the interrupt(configurable on S18) */
	if (mcp_read(mcp, MCP_GPIO, &gpio))
		goto unlock;

	gpio_orig = mcp->cached_gpio;
	mcp->cached_gpio = gpio;
	mutex_unlock(&mcp->lock);

	dev_dbg(mcp->chip.parent,
		"intcap 0x%04X intf 0x%04X gpio_orig 0x%04X gpio 0x%04X\n",
		intcap, intf, gpio_orig, gpio);

	for (i = 0; i < mcp->chip.ngpio; i++) {
		/* We must check all of the inputs on the chip,
		 * otherwise we may not notice a change on >=2 pins.
		 *
		 * On at least the mcp23s17, INTCAP is only updated
		 * one byte at a time(INTCAPA and INTCAPB are
		 * not written to at the same time - only on a per-bank
		 * basis).
		 *
		 * INTF only contains the single bit that caused the
		 * interrupt per-bank.  On the mcp23s17, there is
		 * INTFA and INTFB.  If two pins are changed on the A
		 * side at the same time, INTF will only have one bit
		 * set.  If one pin on the A side and one pin on the B
		 * side are changed at the same time, INTF will have
		 * two bits set.  Thus, INTF can't be the only check
		 * to see if the input has changed.
		 */

		intf_set = intf & BIT(i);
		if (i < 8 && intf_set)
			intcap_mask = 0x00FF;
		else if (i >= 8 && intf_set)
			intcap_mask = 0xFF00;
		else
			intcap_mask = 0x00;

		intcap_changed = (intcap_mask &
			(intcap & BIT(i))) !=
			(intcap_mask & (BIT(i) & gpio_orig));
		gpio_set = BIT(i) & gpio;
		gpio_bit_changed = (BIT(i) & gpio_orig) !=
			(BIT(i) & gpio);
		defval_changed = (BIT(i) & intcon) &&
			((BIT(i) & gpio) !=
			(BIT(i) & defval));

		if (((gpio_bit_changed || intcap_changed) &&
			(BIT(i) & mcp->irq_rise) && gpio_set) ||
		    ((gpio_bit_changed || intcap_changed) &&
			(BIT(i) & mcp->irq_fall) && !gpio_set) ||
		    defval_changed) {
			child_irq = irq_find_mapping(mcp->chip.irq.domain, i);
			handle_nested_irq(child_irq);
		}
	}

	return IRQ_HANDLED;

unlock:
	mutex_unlock(&mcp->lock);
	return IRQ_HANDLED;
}

static void mcp23s08_irq_mask(struct irq_data *data)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	struct mcp23s08 *mcp = gpiochip_get_data(gc);
	unsigned int pos = data->hwirq;

	mcp_set_bit(mcp, MCP_GPINTEN, pos, false);
}

static void mcp23s08_irq_unmask(struct irq_data *data)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	struct mcp23s08 *mcp = gpiochip_get_data(gc);
	unsigned int pos = data->hwirq;

	mcp_set_bit(mcp, MCP_GPINTEN, pos, true);
}

static int mcp23s08_irq_set_type(struct irq_data *data, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	struct mcp23s08 *mcp = gpiochip_get_data(gc);
	unsigned int pos = data->hwirq;

	if ((type & IRQ_TYPE_EDGE_BOTH) == IRQ_TYPE_EDGE_BOTH) {
		mcp_set_bit(mcp, MCP_INTCON, pos, false);
		mcp->irq_rise |= BIT(pos);
		mcp->irq_fall |= BIT(pos);
	} else if (type & IRQ_TYPE_EDGE_RISING) {
		mcp_set_bit(mcp, MCP_INTCON, pos, false);
		mcp->irq_rise |= BIT(pos);
		mcp->irq_fall &= ~BIT(pos);
	} else if (type & IRQ_TYPE_EDGE_FALLING) {
		mcp_set_bit(mcp, MCP_INTCON, pos, false);
		mcp->irq_rise &= ~BIT(pos);
		mcp->irq_fall |= BIT(pos);
	} else if (type & IRQ_TYPE_LEVEL_HIGH) {
		mcp_set_bit(mcp, MCP_INTCON, pos, true);
		mcp_set_bit(mcp, MCP_DEFVAL, pos, false);
	} else if (type & IRQ_TYPE_LEVEL_LOW) {
		mcp_set_bit(mcp, MCP_INTCON, pos, true);
		mcp_set_bit(mcp, MCP_DEFVAL, pos, true);
	} else
		return -EINVAL;

	return 0;
}

static void mcp23s08_irq_bus_lock(struct irq_data *data)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	struct mcp23s08 *mcp = gpiochip_get_data(gc);

	mutex_lock(&mcp->lock);
	regcache_cache_only(mcp->regmap, true);
}

static void mcp23s08_irq_bus_unlock(struct irq_data *data)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	struct mcp23s08 *mcp = gpiochip_get_data(gc);

	regcache_cache_only(mcp->regmap, false);
	regcache_sync(mcp->regmap);

	mutex_unlock(&mcp->lock);
}

static int mcp23s08_irq_setup(struct mcp23s08 *mcp)
{
	struct gpio_chip *chip = &mcp->chip;
	int err;
	unsigned long irqflags = IRQF_ONESHOT | IRQF_SHARED;

	if (mcp->irq_active_high)
		irqflags |= IRQF_TRIGGER_HIGH;
	else
		irqflags |= IRQF_TRIGGER_LOW;

	err = devm_request_threaded_irq(chip->parent, mcp->irq, NULL,
					mcp23s08_irq,
					irqflags, dev_name(chip->parent), mcp);
	if (err != 0) {
		dev_err(chip->parent, "unable to request IRQ#%d: %d\n",
			mcp->irq, err);
		return err;
	}

	return 0;
}

/*----------------------------------------------------------------------*/

int mcp23s08_probe_one(struct mcp23s08 *mcp, struct device *dev,
		       unsigned int addr, unsigned int type, unsigned int base)
{
	int status, ret;
	bool mirror = false;
	bool open_drain = false;

	mutex_init(&mcp->lock);

	mcp->dev = dev;
	mcp->addr = addr;

	mcp->irq_active_high = false;
	mcp->irq_chip.name = dev_name(dev);
	mcp->irq_chip.irq_mask = mcp23s08_irq_mask;
	mcp->irq_chip.irq_unmask = mcp23s08_irq_unmask;
	mcp->irq_chip.irq_set_type = mcp23s08_irq_set_type;
	mcp->irq_chip.irq_bus_lock = mcp23s08_irq_bus_lock;
	mcp->irq_chip.irq_bus_sync_unlock = mcp23s08_irq_bus_unlock;

	mcp->chip.direction_input = mcp23s08_direction_input;
	mcp->chip.get = mcp23s08_get;
	mcp->chip.direction_output = mcp23s08_direction_output;
	mcp->chip.set = mcp23s08_set;
#ifdef CONFIG_OF_GPIO
	mcp->chip.of_gpio_n_cells = 2;
	mcp->chip.of_node = dev->of_node;
#endif

	mcp->chip.base = base;
	mcp->chip.can_sleep = true;
	mcp->chip.parent = dev;
	mcp->chip.owner = THIS_MODULE;

	mcp->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);

	/* verify MCP_IOCON.SEQOP = 0, so sequential reads work,
	 * and MCP_IOCON.HAEN = 1, so we work with all chips.
	 */

	ret = mcp_read(mcp, MCP_IOCON, &status);
	if (ret < 0)
		return dev_err_probe(dev, ret, "can't identify chip %d\n", addr);

	mcp->irq_controller =
		device_property_read_bool(dev, "interrupt-controller");
	if (mcp->irq && mcp->irq_controller) {
		mcp->irq_active_high =
			device_property_read_bool(dev,
					      "microchip,irq-active-high");

		mirror = device_property_read_bool(dev, "microchip,irq-mirror");
		open_drain = device_property_read_bool(dev, "drive-open-drain");
	}

	if ((status & IOCON_SEQOP) || !(status & IOCON_HAEN) || mirror ||
	     mcp->irq_active_high || open_drain) {
		/* mcp23s17 has IOCON twice, make sure they are in sync */
		status &= ~(IOCON_SEQOP | (IOCON_SEQOP << 8));
		status |= IOCON_HAEN | (IOCON_HAEN << 8);
		if (mcp->irq_active_high)
			status |= IOCON_INTPOL | (IOCON_INTPOL << 8);
		else
			status &= ~(IOCON_INTPOL | (IOCON_INTPOL << 8));

		if (mirror)
			status |= IOCON_MIRROR | (IOCON_MIRROR << 8);

		if (open_drain)
			status |= IOCON_ODR | (IOCON_ODR << 8);

		if (type == MCP_TYPE_S18 || type == MCP_TYPE_018)
			status |= IOCON_INTCC | (IOCON_INTCC << 8);

		ret = mcp_write(mcp, MCP_IOCON, status);
		if (ret < 0)
			return dev_err_probe(dev, ret, "can't write IOCON %d\n", addr);
	}

	if (mcp->irq && mcp->irq_controller) {
		struct gpio_irq_chip *girq = &mcp->chip.irq;

		girq->chip = &mcp->irq_chip;
		/* This will let us handle the parent IRQ in the driver */
		girq->parent_handler = NULL;
		girq->num_parents = 0;
		girq->parents = NULL;
		girq->default_type = IRQ_TYPE_NONE;
		girq->handler = handle_simple_irq;
		girq->threaded = true;
	}

	ret = devm_gpiochip_add_data(dev, &mcp->chip, mcp);
	if (ret < 0)
		return dev_err_probe(dev, ret, "can't add GPIO chip\n");

	mcp->pinctrl_desc.pctlops = &mcp_pinctrl_ops;
	mcp->pinctrl_desc.confops = &mcp_pinconf_ops;
	mcp->pinctrl_desc.npins = mcp->chip.ngpio;
	if (mcp->pinctrl_desc.npins == 8)
		mcp->pinctrl_desc.pins = mcp23x08_pins;
	else if (mcp->pinctrl_desc.npins == 16)
		mcp->pinctrl_desc.pins = mcp23x17_pins;
	mcp->pinctrl_desc.owner = THIS_MODULE;

	mcp->pctldev = devm_pinctrl_register(dev, &mcp->pinctrl_desc, mcp);
	if (IS_ERR(mcp->pctldev))
		return dev_err_probe(dev, PTR_ERR(mcp->pctldev), "can't register controller\n");

	if (mcp->irq) {
		ret = mcp23s08_irq_setup(mcp);
		if (ret)
			return dev_err_probe(dev, ret, "can't setup IRQ\n");
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mcp23s08_probe_one);

MODULE_LICENSE("GPL");
