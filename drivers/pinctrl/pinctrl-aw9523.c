// SPDX-License-Identifier: GPL-2.0-only
/*
 * Awinic AW9523B i2c pin controller driver
 * Copyright (c) 2020, AngeloGioacchino Del Regno <angelogioacchino.delregno@somainline.org>
 */

#include <linux/bitfield.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#define AW9523_MAX_FUNCS		2
#define AW9523_NUM_PORTS		2
#define AW9523_PINS_PER_PORT		8

/*
 * HW needs at least 20uS for reset and at least 1-2uS to recover from
 * reset, but we have to account for eventual board quirks, if any:
 * for this reason, keep reset asserted for 50uS and wait for 20uS
 * to recover from the reset.
 */
#define AW9523_HW_RESET_US		50
#define AW9523_HW_RESET_RECOVERY_US	20

/* Port 0: P0_0...P0_7 - Port 1: P1_0...P1_7 */
#define AW9523_PIN_TO_PORT(pin)		(pin >> 3)
#define AW9523_REG_IN_STATE(pin)	(0x00 + AW9523_PIN_TO_PORT(pin))
#define AW9523_REG_OUT_STATE(pin)	(0x02 + AW9523_PIN_TO_PORT(pin))
#define AW9523_REG_CONF_STATE(pin)	(0x04 + AW9523_PIN_TO_PORT(pin))
#define AW9523_REG_INTR_DIS(pin)	(0x06 + AW9523_PIN_TO_PORT(pin))
#define AW9523_REG_CHIPID		0x10
#define AW9523_VAL_EXPECTED_CHIPID	0x23

#define AW9523_REG_GCR			0x11
#define AW9523_GCR_ISEL_MASK		GENMASK(0, 1)
#define AW9523_GCR_GPOMD_MASK		BIT(4)

#define AW9523_REG_PORT_MODE(pin)	(0x12 + AW9523_PIN_TO_PORT(pin))
#define AW9523_REG_SOFT_RESET		0x7f
#define AW9523_VAL_RESET		0x00

/*
 * struct aw9523_irq - Interrupt controller structure
 * @lock: mutex locking for the irq bus
 * @cached_gpio: stores the previous gpio status for bit comparison
 */
struct aw9523_irq {
	struct mutex lock;
	u16 cached_gpio;
};

/*
 * struct aw9523 - Main driver structure
 * @dev: device handle
 * @regmap: regmap handle for current device
 * @i2c_lock: Mutex lock for i2c operations
 * @reset_gpio: Hardware reset (RSTN) signal GPIO
 * @vio_vreg: VCC regulator (Optional)
 * @pctl: pinctrl handle for current device
 * @gpio: structure holding gpiochip params
 * @irq: Interrupt controller structure
 */
struct aw9523 {
	struct device *dev;
	struct regmap *regmap;
	struct mutex i2c_lock;
	struct gpio_desc *reset_gpio;
	struct regulator *vio_vreg;
	struct pinctrl_dev *pctl;
	struct gpio_chip gpio;
	struct aw9523_irq *irq;
};

static const struct pinctrl_pin_desc aw9523_pins[] = {
	/* Port 0 */
	PINCTRL_PIN(0, "gpio0"),
	PINCTRL_PIN(1, "gpio1"),
	PINCTRL_PIN(2, "gpio2"),
	PINCTRL_PIN(3, "gpio3"),
	PINCTRL_PIN(4, "gpio4"),
	PINCTRL_PIN(5, "gpio5"),
	PINCTRL_PIN(6, "gpio6"),
	PINCTRL_PIN(7, "gpio7"),

	/* Port 1 */
	PINCTRL_PIN(8, "gpio8"),
	PINCTRL_PIN(9, "gpio9"),
	PINCTRL_PIN(10, "gpio10"),
	PINCTRL_PIN(11, "gpio11"),
	PINCTRL_PIN(12, "gpio12"),
	PINCTRL_PIN(13, "gpio13"),
	PINCTRL_PIN(14, "gpio14"),
	PINCTRL_PIN(15, "gpio15"),
};

static int aw9523_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(aw9523_pins);
}

static const char *aw9523_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						 unsigned int selector)
{
	return aw9523_pins[selector].name;
}

static int aw9523_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
					 unsigned int selector,
					 const unsigned int **pins,
					 unsigned int *num_pins)
{
	*pins = &aw9523_pins[selector].number;
	*num_pins = 1;
	return 0;
}

static const struct pinctrl_ops aw9523_pinctrl_ops = {
	.get_groups_count = aw9523_pinctrl_get_groups_count,
	.get_group_pins = aw9523_pinctrl_get_group_pins,
	.get_group_name = aw9523_pinctrl_get_group_name,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.dt_free_map = pinconf_generic_dt_free_map,
};

static const char * const gpio_pwm_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3",		/* 0-3 */
	"gpio4", "gpio5", "gpio6", "gpio7",		/* 4-7 */
	"gpio8", "gpio9", "gpio10", "gpio11",		/* 8-11 */
	"gpio12", "gpio13", "gpio14", "gpio15",		/* 11-15 */
};

/* Warning: Do NOT reorder this array */
static const struct pinfunction aw9523_pmx[] = {
	PINCTRL_PINFUNCTION("pwm", gpio_pwm_groups, ARRAY_SIZE(gpio_pwm_groups)),
	PINCTRL_PINFUNCTION("gpio", gpio_pwm_groups, ARRAY_SIZE(gpio_pwm_groups)),
};

static int aw9523_pmx_get_funcs_count(struct pinctrl_dev *pctl)
{
	return ARRAY_SIZE(aw9523_pmx);
}

static const char *aw9523_pmx_get_fname(struct pinctrl_dev *pctl,
					unsigned int sel)
{
	return aw9523_pmx[sel].name;
}

static int aw9523_pmx_get_groups(struct pinctrl_dev *pctl, unsigned int sel,
				 const char * const **groups,
				 unsigned int * const ngroups)
{
	*groups = aw9523_pmx[sel].groups;
	*ngroups = aw9523_pmx[sel].ngroups;
	return 0;
}

static int aw9523_pmx_set_mux(struct pinctrl_dev *pctl, unsigned int fsel,
			      unsigned int grp)
{
	struct aw9523 *awi = pinctrl_dev_get_drvdata(pctl);
	int ret, pin = aw9523_pins[grp].number % AW9523_PINS_PER_PORT;

	if (fsel >= ARRAY_SIZE(aw9523_pmx))
		return -EINVAL;

	/*
	 * This maps directly to the aw9523_pmx array: programming a
	 * high bit means "gpio" and a low bit means "pwm".
	 */
	mutex_lock(&awi->i2c_lock);
	ret = regmap_update_bits(awi->regmap, AW9523_REG_PORT_MODE(pin),
				 BIT(pin), (fsel ? BIT(pin) : 0));
	mutex_unlock(&awi->i2c_lock);
	return ret;
}

static const struct pinmux_ops aw9523_pinmux_ops = {
	.get_functions_count	= aw9523_pmx_get_funcs_count,
	.get_function_name	= aw9523_pmx_get_fname,
	.get_function_groups	= aw9523_pmx_get_groups,
	.set_mux		= aw9523_pmx_set_mux,
};

static int aw9523_pcfg_param_to_reg(enum pin_config_param pcp, int pin, u8 *r)
{
	u8 reg;

	switch (pcp) {
	case PIN_CONFIG_BIAS_PULL_PIN_DEFAULT:
	case PIN_CONFIG_BIAS_PULL_DOWN:
	case PIN_CONFIG_BIAS_PULL_UP:
		reg = AW9523_REG_IN_STATE(pin);
		break;
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		reg = AW9523_REG_GCR;
		break;
	case PIN_CONFIG_INPUT_ENABLE:
	case PIN_CONFIG_OUTPUT_ENABLE:
		reg = AW9523_REG_CONF_STATE(pin);
		break;
	case PIN_CONFIG_OUTPUT:
		reg = AW9523_REG_OUT_STATE(pin);
		break;
	default:
		return -ENOTSUPP;
	}
	*r = reg;

	return 0;
}

static int aw9523_pconf_get(struct pinctrl_dev *pctldev, unsigned int pin,
			    unsigned long *config)
{
	struct aw9523 *awi = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	int regbit = pin % AW9523_PINS_PER_PORT;
	unsigned int val;
	u8 reg;
	int rc;

	rc = aw9523_pcfg_param_to_reg(param, pin, &reg);
	if (rc)
		return rc;

	mutex_lock(&awi->i2c_lock);
	rc = regmap_read(awi->regmap, reg, &val);
	mutex_unlock(&awi->i2c_lock);
	if (rc)
		return rc;

	switch (param) {
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_INPUT_ENABLE:
	case PIN_CONFIG_OUTPUT:
		val &= BIT(regbit);
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
	case PIN_CONFIG_OUTPUT_ENABLE:
		val &= BIT(regbit);
		val = !val;
		break;
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		if (pin >= AW9523_PINS_PER_PORT)
			val = 0;
		else
			val = !FIELD_GET(AW9523_GCR_GPOMD_MASK, val);
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		if (pin >= AW9523_PINS_PER_PORT)
			val = 1;
		else
			val = FIELD_GET(AW9523_GCR_GPOMD_MASK, val);
		break;
	default:
		return -ENOTSUPP;
	}
	if (val < 1)
		return -EINVAL;

	*config = pinconf_to_config_packed(param, !!val);

	return rc;
}

static int aw9523_pconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
			    unsigned long *configs, unsigned int num_configs)
{
	struct aw9523 *awi = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param;
	int regbit = pin % AW9523_PINS_PER_PORT;
	u32 arg;
	u8 reg;
	unsigned int mask, val;
	int i, rc;

	mutex_lock(&awi->i2c_lock);
	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		rc = aw9523_pcfg_param_to_reg(param, pin, &reg);
		if (rc)
			goto end;

		switch (param) {
		case PIN_CONFIG_OUTPUT:
			/* First, enable pin output */
			rc = regmap_update_bits(awi->regmap,
						AW9523_REG_CONF_STATE(pin),
						BIT(regbit), 0);
			if (rc)
				goto end;

			/* Then, fall through to config output level */
			fallthrough;
		case PIN_CONFIG_OUTPUT_ENABLE:
			arg = !arg;
			fallthrough;
		case PIN_CONFIG_BIAS_PULL_PIN_DEFAULT:
		case PIN_CONFIG_BIAS_PULL_DOWN:
		case PIN_CONFIG_BIAS_PULL_UP:
		case PIN_CONFIG_INPUT_ENABLE:
			mask = BIT(regbit);
			val = arg ? BIT(regbit) : 0;
			break;
		case PIN_CONFIG_DRIVE_OPEN_DRAIN:
			/* Open-Drain is supported only on port 0 */
			if (pin >= AW9523_PINS_PER_PORT) {
				rc = -ENOTSUPP;
				goto end;
			}
			mask = AW9523_GCR_GPOMD_MASK;
			val = 0;
			break;
		case PIN_CONFIG_DRIVE_PUSH_PULL:
			/* Port 1 is always Push-Pull */
			if (pin >= AW9523_PINS_PER_PORT) {
				mask = 0;
				val = 0;
				continue;
			}
			mask = AW9523_GCR_GPOMD_MASK;
			val = AW9523_GCR_GPOMD_MASK;
			break;
		default:
			rc = -ENOTSUPP;
			goto end;
		}

		rc = regmap_update_bits(awi->regmap, reg, mask, val);
		if (rc)
			goto end;
	}
end:
	mutex_unlock(&awi->i2c_lock);
	return rc;
}

static const struct pinconf_ops aw9523_pinconf_ops = {
	.pin_config_get = aw9523_pconf_get,
	.pin_config_set = aw9523_pconf_set,
	.is_generic = true,
};

/*
 * aw9523_get_pin_direction - Get pin direction
 * @regmap: Regmap structure
 * @pin: gpiolib pin number
 * @n:   pin index in port register
 *
 * Return: Pin direction for success or negative number for error
 */
static int aw9523_get_pin_direction(struct regmap *regmap, u8 pin, u8 n)
{
	int ret;

	ret = regmap_test_bits(regmap, AW9523_REG_CONF_STATE(pin), BIT(n));
	if (ret < 0)
		return ret;

	return ret ? GPIO_LINE_DIRECTION_IN : GPIO_LINE_DIRECTION_OUT;
}

/*
 * aw9523_get_port_state - Get input or output state for entire port
 * @regmap: Regmap structure
 * @pin:    gpiolib pin number
 * @regbit: hw pin index, used to retrieve port number
 * @state:  returned port state
 *
 * Return: Zero for success or negative number for error
 */
static int aw9523_get_port_state(struct regmap *regmap, u8 pin, u8 regbit,
				 unsigned int *state)
{
	u8 reg;
	int dir;

	dir = aw9523_get_pin_direction(regmap, pin, regbit);
	if (dir < 0)
		return dir;

	if (dir == GPIO_LINE_DIRECTION_IN)
		reg = AW9523_REG_IN_STATE(pin);
	else
		reg = AW9523_REG_OUT_STATE(pin);

	return regmap_read(regmap, reg, state);
}

static int aw9523_gpio_irq_type(struct irq_data *d, unsigned int type)
{
	switch (type) {
	case IRQ_TYPE_NONE:
	case IRQ_TYPE_EDGE_BOTH:
		return 0;
	default:
		return -EINVAL;
	};
}

/*
 * aw9523_irq_mask - Mask interrupt
 * @d: irq data
 *
 * Sets which interrupt to mask in the bitmap;
 * The interrupt will be masked when unlocking the irq bus.
 */
static void aw9523_irq_mask(struct irq_data *d)
{
	struct aw9523 *awi = gpiochip_get_data(irq_data_get_irq_chip_data(d));
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	unsigned int n = hwirq % AW9523_PINS_PER_PORT;

	regmap_update_bits(awi->regmap, AW9523_REG_INTR_DIS(hwirq),
			   BIT(n), BIT(n));
	gpiochip_disable_irq(&awi->gpio, hwirq);
}

/*
 * aw9523_irq_unmask - Unmask interrupt
 * @d: irq data
 *
 * Sets which interrupt to unmask in the bitmap;
 * The interrupt will be masked when unlocking the irq bus.
 */
static void aw9523_irq_unmask(struct irq_data *d)
{
	struct aw9523 *awi = gpiochip_get_data(irq_data_get_irq_chip_data(d));
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	unsigned int n = hwirq % AW9523_PINS_PER_PORT;

	gpiochip_enable_irq(&awi->gpio, hwirq);
	regmap_update_bits(awi->regmap, AW9523_REG_INTR_DIS(hwirq),
			   BIT(n), 0);
}

static irqreturn_t aw9523_irq_thread_func(int irq, void *dev_id)
{
	struct aw9523 *awi = (struct aw9523 *)dev_id;
	unsigned long n, val = 0;
	unsigned long changed_gpio;
	unsigned int tmp, port_pin, i, ret;

	for (i = 0; i < AW9523_NUM_PORTS; i++) {
		port_pin = i * AW9523_PINS_PER_PORT;
		ret = regmap_read(awi->regmap,
				  AW9523_REG_IN_STATE(port_pin),
				  &tmp);
		if (ret)
			return ret;
		val |= (u8)tmp << (i * 8);
	}

	/* Handle GPIO input release interrupt as well */
	changed_gpio = awi->irq->cached_gpio ^ val;
	awi->irq->cached_gpio = val;

	/*
	 * To avoid up to four *slow* i2c reads from any driver hooked
	 * up to our interrupts, just check for the irq_find_mapping
	 * result: if the interrupt is not mapped, then we don't want
	 * to care about it.
	 */
	for_each_set_bit(n, &changed_gpio, awi->gpio.ngpio) {
		tmp = irq_find_mapping(awi->gpio.irq.domain, n);
		if (tmp <= 0)
			continue;
		handle_nested_irq(tmp);
	}

	return IRQ_HANDLED;
}

/*
 * aw9523_irq_bus_lock - Grab lock for interrupt operation
 * @d: irq data
 */
static void aw9523_irq_bus_lock(struct irq_data *d)
{
	struct aw9523 *awi = gpiochip_get_data(irq_data_get_irq_chip_data(d));

	mutex_lock(&awi->irq->lock);
	regcache_cache_only(awi->regmap, true);
}

/*
 * aw9523_irq_bus_sync_unlock - Synchronize state and unlock
 * @d: irq data
 *
 * Writes the interrupt mask bits (found in the bit map) to the
 * hardware, then unlocks the bus.
 */
static void aw9523_irq_bus_sync_unlock(struct irq_data *d)
{
	struct aw9523 *awi = gpiochip_get_data(irq_data_get_irq_chip_data(d));

	regcache_cache_only(awi->regmap, false);
	regcache_sync(awi->regmap);
	mutex_unlock(&awi->irq->lock);
}

static int aw9523_gpio_get_direction(struct gpio_chip *chip,
				     unsigned int offset)
{
	struct aw9523 *awi = gpiochip_get_data(chip);
	u8 regbit = offset % AW9523_PINS_PER_PORT;
	int ret;

	mutex_lock(&awi->i2c_lock);
	ret = aw9523_get_pin_direction(awi->regmap, offset, regbit);
	mutex_unlock(&awi->i2c_lock);

	return ret;
}

static int aw9523_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct aw9523 *awi = gpiochip_get_data(chip);
	u8 regbit = offset % AW9523_PINS_PER_PORT;
	unsigned int val;
	int ret;

	mutex_lock(&awi->i2c_lock);
	ret = aw9523_get_port_state(awi->regmap, offset, regbit, &val);
	mutex_unlock(&awi->i2c_lock);
	if (ret)
		return ret;

	return !!(val & BIT(regbit));
}

/**
 * _aw9523_gpio_get_multiple - Get I/O state for an entire port
 * @regmap: Regmap structure
 * @pin: gpiolib pin number
 * @regbit: hw pin index, used to retrieve port number
 * @state: returned port I/O state
 *
 * Return: Zero for success or negative number for error
 */
static int _aw9523_gpio_get_multiple(struct aw9523 *awi, u8 regbit,
				     u8 *state, u8 mask)
{
	u32 dir_in, val;
	u8 m;
	int ret;

	/* Registers are 8-bits wide */
	ret = regmap_read(awi->regmap, AW9523_REG_CONF_STATE(regbit), &dir_in);
	if (ret)
		return ret;
	*state = 0;

	m = mask & dir_in;
	if (m) {
		ret = regmap_read(awi->regmap, AW9523_REG_IN_STATE(regbit),
				  &val);
		if (ret)
			return ret;
		*state |= (u8)val & m;
	}

	m = mask & ~dir_in;
	if (m) {
		ret = regmap_read(awi->regmap, AW9523_REG_OUT_STATE(regbit),
				  &val);
		if (ret)
			return ret;
		*state |= (u8)val & m;
	}

	return 0;
}

static int aw9523_gpio_get_multiple(struct gpio_chip *chip,
				    unsigned long *mask,
				    unsigned long *bits)
{
	struct aw9523 *awi = gpiochip_get_data(chip);
	u8 m, state = 0;
	int ret;

	mutex_lock(&awi->i2c_lock);

	/* Port 0 (gpio 0-7) */
	m = *mask;
	if (m) {
		ret = _aw9523_gpio_get_multiple(awi, 0, &state, m);
		if (ret)
			goto out;
	}
	*bits = state;

	/* Port 1 (gpio 8-15) */
	m = *mask >> 8;
	if (m) {
		ret = _aw9523_gpio_get_multiple(awi, AW9523_PINS_PER_PORT,
						&state, m);
		if (ret)
			goto out;

		*bits |= (state << 8);
	}
out:
	mutex_unlock(&awi->i2c_lock);
	return ret;
}

static void aw9523_gpio_set_multiple(struct gpio_chip *chip,
				    unsigned long *mask,
				    unsigned long *bits)
{
	struct aw9523 *awi = gpiochip_get_data(chip);
	u8 mask_lo, mask_hi, bits_lo, bits_hi;
	unsigned int reg;
	int ret;

	mask_lo = *mask;
	mask_hi = *mask >> 8;
	bits_lo = *bits;
	bits_hi = *bits >> 8;

	mutex_lock(&awi->i2c_lock);
	if (mask_hi) {
		reg = AW9523_REG_OUT_STATE(AW9523_PINS_PER_PORT);
		ret = regmap_write_bits(awi->regmap, reg, mask_hi, bits_hi);
		if (ret)
			dev_warn(awi->dev, "Cannot write port1 out level\n");
	}
	if (mask_lo) {
		reg = AW9523_REG_OUT_STATE(0);
		ret = regmap_write_bits(awi->regmap, reg, mask_lo, bits_lo);
		if (ret)
			dev_warn(awi->dev, "Cannot write port0 out level\n");
	}
	mutex_unlock(&awi->i2c_lock);
}

static void aw9523_gpio_set(struct gpio_chip *chip,
			    unsigned int offset, int value)
{
	struct aw9523 *awi = gpiochip_get_data(chip);
	u8 regbit = offset % AW9523_PINS_PER_PORT;

	mutex_lock(&awi->i2c_lock);
	regmap_update_bits(awi->regmap, AW9523_REG_OUT_STATE(offset),
			   BIT(regbit), value ? BIT(regbit) : 0);
	mutex_unlock(&awi->i2c_lock);
}


static int aw9523_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	struct aw9523 *awi = gpiochip_get_data(chip);
	u8 regbit = offset % AW9523_PINS_PER_PORT;
	int ret;

	mutex_lock(&awi->i2c_lock);
	ret = regmap_update_bits(awi->regmap, AW9523_REG_CONF_STATE(offset),
				 BIT(regbit), BIT(regbit));
	mutex_unlock(&awi->i2c_lock);

	return ret;
}

static int aw9523_direction_output(struct gpio_chip *chip,
				   unsigned int offset, int value)
{
	struct aw9523 *awi = gpiochip_get_data(chip);
	u8 regbit = offset % AW9523_PINS_PER_PORT;
	int ret;

	mutex_lock(&awi->i2c_lock);
	ret = regmap_update_bits(awi->regmap, AW9523_REG_OUT_STATE(offset),
				 BIT(regbit), value ? BIT(regbit) : 0);
	if (ret)
		goto end;

	ret = regmap_update_bits(awi->regmap, AW9523_REG_CONF_STATE(offset),
				 BIT(regbit), 0);
end:
	mutex_unlock(&awi->i2c_lock);
	return ret;
}

static int aw9523_drive_reset_gpio(struct aw9523 *awi)
{
	unsigned int chip_id;
	int ret;

	/*
	 * If the chip is already configured for any reason, then we
	 * will probably succeed in sending the soft reset signal to
	 * the hardware through I2C: this operation takes less time
	 * compared to a full HW reset and it gives the same results.
	 */
	ret = regmap_write(awi->regmap, AW9523_REG_SOFT_RESET, 0);
	if (ret == 0)
		goto done;

	dev_dbg(awi->dev, "Cannot execute soft reset: trying hard reset\n");
	ret = gpiod_direction_output(awi->reset_gpio, 0);
	if (ret)
		return ret;

	/* The reset pulse has to be longer than 20uS due to deglitch */
	usleep_range(AW9523_HW_RESET_US, AW9523_HW_RESET_US + 1);

	ret = gpiod_direction_output(awi->reset_gpio, 1);
	if (ret)
		return ret;
done:
	/* The HW needs at least 1uS to reliably recover after reset */
	usleep_range(AW9523_HW_RESET_RECOVERY_US,
		     AW9523_HW_RESET_RECOVERY_US + 1);

	/* Check the ChipID */
	ret = regmap_read(awi->regmap, AW9523_REG_CHIPID, &chip_id);
	if (ret) {
		dev_err(awi->dev, "Cannot read Chip ID: %d\n", ret);
		return ret;
	}
	if (chip_id != AW9523_VAL_EXPECTED_CHIPID) {
		dev_err(awi->dev, "Bad ChipID; read 0x%x, expected 0x%x\n",
			chip_id, AW9523_VAL_EXPECTED_CHIPID);
		return -EINVAL;
	}

	return 0;
}

static int aw9523_hw_reset(struct aw9523 *awi)
{
	int ret, max_retries = 2;

	/* Sometimes the chip needs more than one reset cycle */
	do {
		ret = aw9523_drive_reset_gpio(awi);
		if (ret == 0)
			break;
		max_retries--;
	} while (max_retries);

	return ret;
}

static int aw9523_init_gpiochip(struct aw9523 *awi, unsigned int npins)
{
	struct device *dev = awi->dev;
	struct gpio_chip *gc = &awi->gpio;

	gc->label = devm_kstrdup(dev, dev_name(dev), GFP_KERNEL);
	if (!gc->label)
		return -ENOMEM;

	gc->base = -1;
	gc->ngpio = npins;
	gc->get_direction = aw9523_gpio_get_direction;
	gc->direction_input = aw9523_direction_input;
	gc->direction_output = aw9523_direction_output;
	gc->get = aw9523_gpio_get;
	gc->get_multiple = aw9523_gpio_get_multiple;
	gc->set = aw9523_gpio_set;
	gc->set_multiple = aw9523_gpio_set_multiple;
	gc->set_config = gpiochip_generic_config;
	gc->parent = dev;
	gc->owner = THIS_MODULE;
	gc->can_sleep = false;

	return 0;
}

static const struct irq_chip aw9523_irq_chip = {
	.name = "aw9523",
	.irq_mask = aw9523_irq_mask,
	.irq_unmask = aw9523_irq_unmask,
	.irq_bus_lock = aw9523_irq_bus_lock,
	.irq_bus_sync_unlock = aw9523_irq_bus_sync_unlock,
	.irq_set_type = aw9523_gpio_irq_type,
	.flags = IRQCHIP_IMMUTABLE,
        GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static int aw9523_init_irq(struct aw9523 *awi, int irq)
{
	struct device *dev = awi->dev;
	struct gpio_irq_chip *girq;
	int ret;

	if (!device_property_read_bool(dev, "interrupt-controller"))
		return 0;

	awi->irq = devm_kzalloc(dev, sizeof(*awi->irq), GFP_KERNEL);
	if (!awi->irq)
		return -ENOMEM;

	mutex_init(&awi->irq->lock);

	ret = devm_request_threaded_irq(dev, irq, NULL, aw9523_irq_thread_func,
					IRQF_ONESHOT, dev_name(dev), awi);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to request irq %d\n", irq);

	girq = &awi->gpio.irq;
	gpio_irq_chip_set_chip(girq, &aw9523_irq_chip);
	girq->parent_handler = NULL;
	girq->num_parents = 0;
	girq->parents = NULL;
	girq->default_type = IRQ_TYPE_EDGE_BOTH;
	girq->handler = handle_simple_irq;
	girq->threaded = true;

	return 0;
}

static bool aw9523_is_reg_hole(unsigned int reg)
{
	return (reg > AW9523_REG_PORT_MODE(AW9523_PINS_PER_PORT) &&
		reg < AW9523_REG_SOFT_RESET) ||
	       (reg > AW9523_REG_INTR_DIS(AW9523_PINS_PER_PORT) &&
		reg < AW9523_REG_CHIPID);
}

static bool aw9523_readable_reg(struct device *dev, unsigned int reg)
{
	/* All available registers (minus holes) can be read */
	return !aw9523_is_reg_hole(reg);
}

static bool aw9523_volatile_reg(struct device *dev, unsigned int reg)
{
	return aw9523_is_reg_hole(reg) ||
	       reg == AW9523_REG_IN_STATE(0) ||
	       reg == AW9523_REG_IN_STATE(AW9523_PINS_PER_PORT) ||
	       reg == AW9523_REG_CHIPID ||
	       reg == AW9523_REG_SOFT_RESET;
}

static bool aw9523_writeable_reg(struct device *dev, unsigned int reg)
{
	return !aw9523_is_reg_hole(reg) && reg != AW9523_REG_CHIPID;
}

static bool aw9523_precious_reg(struct device *dev, unsigned int reg)
{
	/* Reading AW9523_REG_IN_STATE clears interrupt status */
	return aw9523_is_reg_hole(reg) ||
	       reg == AW9523_REG_IN_STATE(0) ||
	       reg == AW9523_REG_IN_STATE(AW9523_PINS_PER_PORT);
}

static const struct regmap_config aw9523_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.reg_stride = 1,

	.precious_reg = aw9523_precious_reg,
	.readable_reg = aw9523_readable_reg,
	.volatile_reg = aw9523_volatile_reg,
	.writeable_reg = aw9523_writeable_reg,

	.cache_type = REGCACHE_FLAT,
	.disable_locking = true,

	.num_reg_defaults_raw = AW9523_REG_SOFT_RESET,
};

static int aw9523_hw_init(struct aw9523 *awi)
{
	u8 p1_pin = AW9523_PINS_PER_PORT;
	unsigned int val;
	int ret;

	/* No register caching during initialization */
	regcache_cache_bypass(awi->regmap, true);

	/* Bring up the chip */
	ret = aw9523_hw_reset(awi);
	if (ret) {
		dev_err(awi->dev, "HW Reset failed: %d\n", ret);
		return ret;
	}

	/*
	 * This is the expected chip and it is running: it's time to
	 * set a safe default configuration in case the user doesn't
	 * configure (all of the available) pins in this chip.
	 * P.S.: The writes order doesn't matter.
	 */

	/* Set all pins as GPIO */
	ret = regmap_write(awi->regmap, AW9523_REG_PORT_MODE(0), U8_MAX);
	if (ret)
		return ret;
	ret = regmap_write(awi->regmap, AW9523_REG_PORT_MODE(p1_pin), U8_MAX);
	if (ret)
		return ret;

	/* Set Open-Drain mode on Port 0 (Port 1 is always P-P) */
	ret = regmap_write(awi->regmap, AW9523_REG_GCR, 0);
	if (ret)
		return ret;

	/* Set all pins as inputs */
	ret = regmap_write(awi->regmap, AW9523_REG_CONF_STATE(0), U8_MAX);
	if (ret)
		return ret;
	ret = regmap_write(awi->regmap, AW9523_REG_CONF_STATE(p1_pin), U8_MAX);
	if (ret)
		return ret;

	/* Disable all interrupts to avoid unreasoned wakeups */
	ret = regmap_write(awi->regmap, AW9523_REG_INTR_DIS(0), U8_MAX);
	if (ret)
		return ret;
	ret = regmap_write(awi->regmap, AW9523_REG_INTR_DIS(p1_pin), U8_MAX);
	if (ret)
		return ret;

	/* Clear setup-generated interrupts by performing a port state read */
	ret = aw9523_get_port_state(awi->regmap, 0, 0, &val);
	if (ret)
		return ret;
	ret = aw9523_get_port_state(awi->regmap, p1_pin, 0, &val);
	if (ret)
		return ret;

	/* Everything went fine: activate and reinitialize register cache */
	regcache_cache_bypass(awi->regmap, false);
	return regmap_reinit_cache(awi->regmap, &aw9523_regmap);
}

static int aw9523_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct pinctrl_desc *pdesc;
	struct aw9523 *awi;
	int ret;

	awi = devm_kzalloc(dev, sizeof(*awi), GFP_KERNEL);
	if (!awi)
		return -ENOMEM;

	i2c_set_clientdata(client, awi);

	awi->dev = dev;
	awi->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(awi->reset_gpio))
		return PTR_ERR(awi->reset_gpio);
	gpiod_set_consumer_name(awi->reset_gpio, "aw9523 reset");

	awi->regmap = devm_regmap_init_i2c(client, &aw9523_regmap);
	if (IS_ERR(awi->regmap))
		return PTR_ERR(awi->regmap);

	awi->vio_vreg = devm_regulator_get_optional(dev, "vio");
	if (IS_ERR(awi->vio_vreg)) {
		if (PTR_ERR(awi->vio_vreg) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		awi->vio_vreg = NULL;
	} else {
		ret = regulator_enable(awi->vio_vreg);
		if (ret)
			return ret;
	}

	mutex_init(&awi->i2c_lock);
	lockdep_set_subclass(&awi->i2c_lock, i2c_adapter_depth(client->adapter));

	pdesc = devm_kzalloc(dev, sizeof(*pdesc), GFP_KERNEL);
	if (!pdesc)
		return -ENOMEM;

	ret = aw9523_hw_init(awi);
	if (ret)
		goto err_disable_vregs;

	pdesc->name = dev_name(dev);
	pdesc->owner = THIS_MODULE;
	pdesc->pctlops = &aw9523_pinctrl_ops;
	pdesc->pmxops  = &aw9523_pinmux_ops;
	pdesc->confops = &aw9523_pinconf_ops;
	pdesc->pins = aw9523_pins;
	pdesc->npins = ARRAY_SIZE(aw9523_pins);

	ret = aw9523_init_gpiochip(awi, pdesc->npins);
	if (ret)
		goto err_disable_vregs;

	if (client->irq) {
		ret = aw9523_init_irq(awi, client->irq);
		if (ret)
			goto err_disable_vregs;
	}

	awi->pctl = devm_pinctrl_register(dev, pdesc, awi);
	if (IS_ERR(awi->pctl)) {
		ret = dev_err_probe(dev, PTR_ERR(awi->pctl), "Cannot register pinctrl");
		goto err_disable_vregs;
	}

	ret = devm_gpiochip_add_data(dev, &awi->gpio, awi);
	if (ret)
		goto err_disable_vregs;

	return ret;

err_disable_vregs:
	if (awi->vio_vreg)
		regulator_disable(awi->vio_vreg);
	mutex_destroy(&awi->i2c_lock);
	return ret;
}

static void aw9523_remove(struct i2c_client *client)
{
	struct aw9523 *awi = i2c_get_clientdata(client);

	/*
	 * If the chip VIO is connected to a regulator that we can turn
	 * off, life is easy... otherwise, reinitialize the chip and
	 * set the pins to hardware defaults before removing the driver
	 * to leave it in a clean, safe and predictable state.
	 */
	if (awi->vio_vreg) {
		regulator_disable(awi->vio_vreg);
	} else {
		mutex_lock(&awi->i2c_lock);
		aw9523_hw_init(awi);
		mutex_unlock(&awi->i2c_lock);
	}

	mutex_destroy(&awi->i2c_lock);
}

static const struct i2c_device_id aw9523_i2c_id_table[] = {
	{ "aw9523_i2c", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, aw9523_i2c_id_table);

static const struct of_device_id of_aw9523_i2c_match[] = {
	{ .compatible = "awinic,aw9523-pinctrl", },
	{ }
};
MODULE_DEVICE_TABLE(of, of_aw9523_i2c_match);

static struct i2c_driver aw9523_driver = {
	.driver = {
		.name = "aw9523-pinctrl",
		.of_match_table = of_aw9523_i2c_match,
	},
	.probe = aw9523_probe,
	.remove = aw9523_remove,
	.id_table = aw9523_i2c_id_table,
};
module_i2c_driver(aw9523_driver);

MODULE_DESCRIPTION("Awinic AW9523 I2C GPIO Expander driver");
MODULE_AUTHOR("AngeloGioacchino Del Regno <angelogioacchino.delregno@somainline.org>");
MODULE_LICENSE("GPL v2");
