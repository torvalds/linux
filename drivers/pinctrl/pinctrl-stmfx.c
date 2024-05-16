// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for STMicroelectronics Multi-Function eXpander (STMFX) GPIO expander
 *
 * Copyright (C) 2019 STMicroelectronics
 * Author(s): Amelie Delaunay <amelie.delaunay@st.com>.
 */
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/mfd/stmfx.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>

#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinmux.h>

#include "core.h"
#include "pinctrl-utils.h"

/* GPIOs expander */
/* GPIO_STATE1 0x10, GPIO_STATE2 0x11, GPIO_STATE3 0x12 */
#define STMFX_REG_GPIO_STATE		STMFX_REG_GPIO_STATE1 /* R */
/* GPIO_DIR1 0x60, GPIO_DIR2 0x61, GPIO_DIR3 0x63 */
#define STMFX_REG_GPIO_DIR		STMFX_REG_GPIO_DIR1 /* RW */
/* GPIO_TYPE1 0x64, GPIO_TYPE2 0x65, GPIO_TYPE3 0x66 */
#define STMFX_REG_GPIO_TYPE		STMFX_REG_GPIO_TYPE1 /* RW */
/* GPIO_PUPD1 0x68, GPIO_PUPD2 0x69, GPIO_PUPD3 0x6A */
#define STMFX_REG_GPIO_PUPD		STMFX_REG_GPIO_PUPD1 /* RW */
/* GPO_SET1 0x6C, GPO_SET2 0x6D, GPO_SET3 0x6E */
#define STMFX_REG_GPO_SET		STMFX_REG_GPO_SET1 /* RW */
/* GPO_CLR1 0x70, GPO_CLR2 0x71, GPO_CLR3 0x72 */
#define STMFX_REG_GPO_CLR		STMFX_REG_GPO_CLR1 /* RW */
/* IRQ_GPI_SRC1 0x48, IRQ_GPI_SRC2 0x49, IRQ_GPI_SRC3 0x4A */
#define STMFX_REG_IRQ_GPI_SRC		STMFX_REG_IRQ_GPI_SRC1 /* RW */
/* IRQ_GPI_EVT1 0x4C, IRQ_GPI_EVT2 0x4D, IRQ_GPI_EVT3 0x4E */
#define STMFX_REG_IRQ_GPI_EVT		STMFX_REG_IRQ_GPI_EVT1 /* RW */
/* IRQ_GPI_TYPE1 0x50, IRQ_GPI_TYPE2 0x51, IRQ_GPI_TYPE3 0x52 */
#define STMFX_REG_IRQ_GPI_TYPE		STMFX_REG_IRQ_GPI_TYPE1 /* RW */
/* IRQ_GPI_PENDING1 0x0C, IRQ_GPI_PENDING2 0x0D, IRQ_GPI_PENDING3 0x0E*/
#define STMFX_REG_IRQ_GPI_PENDING	STMFX_REG_IRQ_GPI_PENDING1 /* R */
/* IRQ_GPI_ACK1 0x54, IRQ_GPI_ACK2 0x55, IRQ_GPI_ACK3 0x56 */
#define STMFX_REG_IRQ_GPI_ACK		STMFX_REG_IRQ_GPI_ACK1 /* RW */

#define NR_GPIO_REGS			3
#define NR_GPIOS_PER_REG		8
#define get_reg(offset)			((offset) / NR_GPIOS_PER_REG)
#define get_shift(offset)		((offset) % NR_GPIOS_PER_REG)
#define get_mask(offset)		(BIT(get_shift(offset)))

/*
 * STMFX pinctrl can have up to 24 pins if STMFX other functions are not used.
 * Pins availability is managed thanks to gpio-ranges property.
 */
static const struct pinctrl_pin_desc stmfx_pins[] = {
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
	PINCTRL_PIN(16, "agpio0"),
	PINCTRL_PIN(17, "agpio1"),
	PINCTRL_PIN(18, "agpio2"),
	PINCTRL_PIN(19, "agpio3"),
	PINCTRL_PIN(20, "agpio4"),
	PINCTRL_PIN(21, "agpio5"),
	PINCTRL_PIN(22, "agpio6"),
	PINCTRL_PIN(23, "agpio7"),
};

struct stmfx_pinctrl {
	struct device *dev;
	struct stmfx *stmfx;
	struct pinctrl_dev *pctl_dev;
	struct pinctrl_desc pctl_desc;
	struct gpio_chip gpio_chip;
	struct mutex lock; /* IRQ bus lock */
	unsigned long gpio_valid_mask;
	/* Cache of IRQ_GPI_* registers for bus_lock */
	u8 irq_gpi_src[NR_GPIO_REGS];
	u8 irq_gpi_type[NR_GPIO_REGS];
	u8 irq_gpi_evt[NR_GPIO_REGS];
	u8 irq_toggle_edge[NR_GPIO_REGS];
#ifdef CONFIG_PM
	/* Backup of GPIO_* registers for suspend/resume */
	u8 bkp_gpio_state[NR_GPIO_REGS];
	u8 bkp_gpio_dir[NR_GPIO_REGS];
	u8 bkp_gpio_type[NR_GPIO_REGS];
	u8 bkp_gpio_pupd[NR_GPIO_REGS];
#endif
};

static int stmfx_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct stmfx_pinctrl *pctl = gpiochip_get_data(gc);
	u32 reg = STMFX_REG_GPIO_STATE + get_reg(offset);
	u32 mask = get_mask(offset);
	u32 value;
	int ret;

	ret = regmap_read(pctl->stmfx->map, reg, &value);

	return ret ? ret : !!(value & mask);
}

static void stmfx_gpio_set(struct gpio_chip *gc, unsigned int offset, int value)
{
	struct stmfx_pinctrl *pctl = gpiochip_get_data(gc);
	u32 reg = value ? STMFX_REG_GPO_SET : STMFX_REG_GPO_CLR;
	u32 mask = get_mask(offset);

	regmap_write_bits(pctl->stmfx->map, reg + get_reg(offset),
			  mask, mask);
}

static int stmfx_gpio_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	struct stmfx_pinctrl *pctl = gpiochip_get_data(gc);
	u32 reg = STMFX_REG_GPIO_DIR + get_reg(offset);
	u32 mask = get_mask(offset);
	u32 val;
	int ret;

	ret = regmap_read(pctl->stmfx->map, reg, &val);
	/*
	 * On stmfx, gpio pins direction is (0)input, (1)output.
	 */
	if (ret)
		return ret;

	if (val & mask)
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static int stmfx_gpio_direction_input(struct gpio_chip *gc, unsigned int offset)
{
	struct stmfx_pinctrl *pctl = gpiochip_get_data(gc);
	u32 reg = STMFX_REG_GPIO_DIR + get_reg(offset);
	u32 mask = get_mask(offset);

	return regmap_write_bits(pctl->stmfx->map, reg, mask, 0);
}

static int stmfx_gpio_direction_output(struct gpio_chip *gc,
				       unsigned int offset, int value)
{
	struct stmfx_pinctrl *pctl = gpiochip_get_data(gc);
	u32 reg = STMFX_REG_GPIO_DIR + get_reg(offset);
	u32 mask = get_mask(offset);

	stmfx_gpio_set(gc, offset, value);

	return regmap_write_bits(pctl->stmfx->map, reg, mask, mask);
}

static int stmfx_pinconf_get_pupd(struct stmfx_pinctrl *pctl,
				  unsigned int offset)
{
	u32 reg = STMFX_REG_GPIO_PUPD + get_reg(offset);
	u32 pupd, mask = get_mask(offset);
	int ret;

	ret = regmap_read(pctl->stmfx->map, reg, &pupd);
	if (ret)
		return ret;

	return !!(pupd & mask);
}

static int stmfx_pinconf_set_pupd(struct stmfx_pinctrl *pctl,
				  unsigned int offset, u32 pupd)
{
	u32 reg = STMFX_REG_GPIO_PUPD + get_reg(offset);
	u32 mask = get_mask(offset);

	return regmap_write_bits(pctl->stmfx->map, reg, mask, pupd ? mask : 0);
}

static int stmfx_pinconf_get_type(struct stmfx_pinctrl *pctl,
				  unsigned int offset)
{
	u32 reg = STMFX_REG_GPIO_TYPE + get_reg(offset);
	u32 type, mask = get_mask(offset);
	int ret;

	ret = regmap_read(pctl->stmfx->map, reg, &type);
	if (ret)
		return ret;

	return !!(type & mask);
}

static int stmfx_pinconf_set_type(struct stmfx_pinctrl *pctl,
				  unsigned int offset, u32 type)
{
	u32 reg = STMFX_REG_GPIO_TYPE + get_reg(offset);
	u32 mask = get_mask(offset);

	return regmap_write_bits(pctl->stmfx->map, reg, mask, type ? mask : 0);
}

static int stmfx_pinconf_get(struct pinctrl_dev *pctldev,
			     unsigned int pin, unsigned long *config)
{
	struct stmfx_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	u32 param = pinconf_to_config_param(*config);
	struct pinctrl_gpio_range *range;
	u32 arg = 0;
	int ret, dir, type, pupd;

	range = pinctrl_find_gpio_range_from_pin_nolock(pctldev, pin);
	if (!range)
		return -EINVAL;

	dir = stmfx_gpio_get_direction(&pctl->gpio_chip, pin);
	if (dir < 0)
		return dir;

	/*
	 * Currently the gpiolib IN is 1 and OUT is 0 but let's not count
	 * on it just to be on the safe side also in the future :)
	 */
	dir = (dir == GPIO_LINE_DIRECTION_IN) ? 1 : 0;

	type = stmfx_pinconf_get_type(pctl, pin);
	if (type < 0)
		return type;
	pupd = stmfx_pinconf_get_pupd(pctl, pin);
	if (pupd < 0)
		return pupd;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		if ((!dir && (!type || !pupd)) || (dir && !type))
			arg = 1;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (dir && type && !pupd)
			arg = 1;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		if (type && pupd)
			arg = 1;
		break;
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		if ((!dir && type) || (dir && !type))
			arg = 1;
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		if ((!dir && !type) || (dir && type))
			arg = 1;
		break;
	case PIN_CONFIG_OUTPUT:
		if (dir)
			return -EINVAL;

		ret = stmfx_gpio_get(&pctl->gpio_chip, pin);
		if (ret < 0)
			return ret;

		arg = ret;
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int stmfx_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
			     unsigned long *configs, unsigned int num_configs)
{
	struct stmfx_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct pinctrl_gpio_range *range;
	enum pin_config_param param;
	u32 arg;
	int i, ret;

	range = pinctrl_find_gpio_range_from_pin_nolock(pctldev, pin);
	if (!range) {
		dev_err(pctldev->dev, "pin %d is not available\n", pin);
		return -EINVAL;
	}

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_PULL_PIN_DEFAULT:
		case PIN_CONFIG_BIAS_DISABLE:
		case PIN_CONFIG_DRIVE_PUSH_PULL:
			ret = stmfx_pinconf_set_type(pctl, pin, 0);
			if (ret)
				return ret;
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			ret = stmfx_pinconf_set_type(pctl, pin, 1);
			if (ret)
				return ret;
			ret = stmfx_pinconf_set_pupd(pctl, pin, 0);
			if (ret)
				return ret;
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			ret = stmfx_pinconf_set_type(pctl, pin, 1);
			if (ret)
				return ret;
			ret = stmfx_pinconf_set_pupd(pctl, pin, 1);
			if (ret)
				return ret;
			break;
		case PIN_CONFIG_DRIVE_OPEN_DRAIN:
			ret = stmfx_pinconf_set_type(pctl, pin, 1);
			if (ret)
				return ret;
			break;
		case PIN_CONFIG_OUTPUT:
			ret = stmfx_gpio_direction_output(&pctl->gpio_chip,
							  pin, arg);
			if (ret)
				return ret;
			break;
		default:
			return -ENOTSUPP;
		}
	}

	return 0;
}

static void stmfx_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				   struct seq_file *s, unsigned int offset)
{
	struct stmfx_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct pinctrl_gpio_range *range;
	int dir, type, pupd, val;

	range = pinctrl_find_gpio_range_from_pin_nolock(pctldev, offset);
	if (!range)
		return;

	dir = stmfx_gpio_get_direction(&pctl->gpio_chip, offset);
	if (dir < 0)
		return;
	type = stmfx_pinconf_get_type(pctl, offset);
	if (type < 0)
		return;
	pupd = stmfx_pinconf_get_pupd(pctl, offset);
	if (pupd < 0)
		return;
	val = stmfx_gpio_get(&pctl->gpio_chip, offset);
	if (val < 0)
		return;

	if (dir == GPIO_LINE_DIRECTION_OUT) {
		seq_printf(s, "output %s ", val ? "high" : "low");
		if (type)
			seq_printf(s, "open drain %s internal pull-up ",
				   pupd ? "with" : "without");
		else
			seq_puts(s, "push pull no pull ");
	} else {
		seq_printf(s, "input %s ", val ? "high" : "low");
		if (type)
			seq_printf(s, "with internal pull-%s ",
				   pupd ? "up" : "down");
		else
			seq_printf(s, "%s ", pupd ? "floating" : "analog");
	}
}

static const struct pinconf_ops stmfx_pinconf_ops = {
	.pin_config_get		= stmfx_pinconf_get,
	.pin_config_set		= stmfx_pinconf_set,
	.pin_config_dbg_show	= stmfx_pinconf_dbg_show,
};

static int stmfx_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	return 0;
}

static const char *stmfx_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						unsigned int selector)
{
	return NULL;
}

static int stmfx_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
					unsigned int selector,
					const unsigned int **pins,
					unsigned int *num_pins)
{
	return -ENOTSUPP;
}

static const struct pinctrl_ops stmfx_pinctrl_ops = {
	.get_groups_count = stmfx_pinctrl_get_groups_count,
	.get_group_name = stmfx_pinctrl_get_group_name,
	.get_group_pins = stmfx_pinctrl_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.dt_free_map = pinctrl_utils_free_map,
};

static void stmfx_pinctrl_irq_mask(struct irq_data *data)
{
	struct gpio_chip *gpio_chip = irq_data_get_irq_chip_data(data);
	struct stmfx_pinctrl *pctl = gpiochip_get_data(gpio_chip);
	u32 reg = get_reg(data->hwirq);
	u32 mask = get_mask(data->hwirq);

	pctl->irq_gpi_src[reg] &= ~mask;
	gpiochip_disable_irq(gpio_chip, irqd_to_hwirq(data));
}

static void stmfx_pinctrl_irq_unmask(struct irq_data *data)
{
	struct gpio_chip *gpio_chip = irq_data_get_irq_chip_data(data);
	struct stmfx_pinctrl *pctl = gpiochip_get_data(gpio_chip);
	u32 reg = get_reg(data->hwirq);
	u32 mask = get_mask(data->hwirq);

	gpiochip_enable_irq(gpio_chip, irqd_to_hwirq(data));
	pctl->irq_gpi_src[reg] |= mask;
}

static int stmfx_pinctrl_irq_set_type(struct irq_data *data, unsigned int type)
{
	struct gpio_chip *gpio_chip = irq_data_get_irq_chip_data(data);
	struct stmfx_pinctrl *pctl = gpiochip_get_data(gpio_chip);
	u32 reg = get_reg(data->hwirq);
	u32 mask = get_mask(data->hwirq);

	if (type == IRQ_TYPE_NONE)
		return -EINVAL;

	if (type & IRQ_TYPE_EDGE_BOTH) {
		pctl->irq_gpi_evt[reg] |= mask;
		irq_set_handler_locked(data, handle_edge_irq);
	} else {
		pctl->irq_gpi_evt[reg] &= ~mask;
		irq_set_handler_locked(data, handle_level_irq);
	}

	if ((type & IRQ_TYPE_EDGE_RISING) || (type & IRQ_TYPE_LEVEL_HIGH))
		pctl->irq_gpi_type[reg] |= mask;
	else
		pctl->irq_gpi_type[reg] &= ~mask;

	/*
	 * In case of (type & IRQ_TYPE_EDGE_BOTH), we need to know current
	 * GPIO value to set the right edge trigger. But in atomic context
	 * here we can't access registers over I2C. That's why (type &
	 * IRQ_TYPE_EDGE_BOTH) will be managed in .irq_sync_unlock.
	 */

	if ((type & IRQ_TYPE_EDGE_BOTH) == IRQ_TYPE_EDGE_BOTH)
		pctl->irq_toggle_edge[reg] |= mask;
	else
		pctl->irq_toggle_edge[reg] &= mask;

	return 0;
}

static void stmfx_pinctrl_irq_bus_lock(struct irq_data *data)
{
	struct gpio_chip *gpio_chip = irq_data_get_irq_chip_data(data);
	struct stmfx_pinctrl *pctl = gpiochip_get_data(gpio_chip);

	mutex_lock(&pctl->lock);
}

static void stmfx_pinctrl_irq_bus_sync_unlock(struct irq_data *data)
{
	struct gpio_chip *gpio_chip = irq_data_get_irq_chip_data(data);
	struct stmfx_pinctrl *pctl = gpiochip_get_data(gpio_chip);
	u32 reg = get_reg(data->hwirq);
	u32 mask = get_mask(data->hwirq);

	/*
	 * In case of IRQ_TYPE_EDGE_BOTH), read the current GPIO value
	 * (this couldn't be done in .irq_set_type because of atomic context)
	 * to set the right irq trigger type.
	 */
	if (pctl->irq_toggle_edge[reg] & mask) {
		if (stmfx_gpio_get(gpio_chip, data->hwirq))
			pctl->irq_gpi_type[reg] &= ~mask;
		else
			pctl->irq_gpi_type[reg] |= mask;
	}

	regmap_bulk_write(pctl->stmfx->map, STMFX_REG_IRQ_GPI_EVT,
			  pctl->irq_gpi_evt, NR_GPIO_REGS);
	regmap_bulk_write(pctl->stmfx->map, STMFX_REG_IRQ_GPI_TYPE,
			  pctl->irq_gpi_type, NR_GPIO_REGS);
	regmap_bulk_write(pctl->stmfx->map, STMFX_REG_IRQ_GPI_SRC,
			  pctl->irq_gpi_src, NR_GPIO_REGS);

	mutex_unlock(&pctl->lock);
}

static int stmfx_gpio_irq_request_resources(struct irq_data *data)
{
	struct gpio_chip *gpio_chip = irq_data_get_irq_chip_data(data);
	int ret;

	ret = stmfx_gpio_direction_input(gpio_chip, data->hwirq);
	if (ret)
		return ret;

	return gpiochip_reqres_irq(gpio_chip, data->hwirq);
}

static void stmfx_gpio_irq_release_resources(struct irq_data *data)
{
	struct gpio_chip *gpio_chip = irq_data_get_irq_chip_data(data);

	return gpiochip_relres_irq(gpio_chip, data->hwirq);
}

static void stmfx_pinctrl_irq_toggle_trigger(struct stmfx_pinctrl *pctl,
					     unsigned int offset)
{
	u32 reg = get_reg(offset);
	u32 mask = get_mask(offset);
	int val;

	if (!(pctl->irq_toggle_edge[reg] & mask))
		return;

	val = stmfx_gpio_get(&pctl->gpio_chip, offset);
	if (val < 0)
		return;

	if (val) {
		pctl->irq_gpi_type[reg] &= mask;
		regmap_write_bits(pctl->stmfx->map,
				  STMFX_REG_IRQ_GPI_TYPE + reg,
				  mask, 0);

	} else {
		pctl->irq_gpi_type[reg] |= mask;
		regmap_write_bits(pctl->stmfx->map,
				  STMFX_REG_IRQ_GPI_TYPE + reg,
				  mask, mask);
	}
}

static irqreturn_t stmfx_pinctrl_irq_thread_fn(int irq, void *dev_id)
{
	struct stmfx_pinctrl *pctl = (struct stmfx_pinctrl *)dev_id;
	struct gpio_chip *gc = &pctl->gpio_chip;
	u8 pending[NR_GPIO_REGS];
	u8 src[NR_GPIO_REGS] = {0, 0, 0};
	unsigned long n, status;
	int i, ret;

	ret = regmap_bulk_read(pctl->stmfx->map, STMFX_REG_IRQ_GPI_PENDING,
			       &pending, NR_GPIO_REGS);
	if (ret)
		return IRQ_NONE;

	regmap_bulk_write(pctl->stmfx->map, STMFX_REG_IRQ_GPI_SRC,
			  src, NR_GPIO_REGS);

	BUILD_BUG_ON(NR_GPIO_REGS > sizeof(status));
	for (i = 0, status = 0; i < NR_GPIO_REGS; i++)
		status |= (unsigned long)pending[i] << (i * 8);
	for_each_set_bit(n, &status, gc->ngpio) {
		handle_nested_irq(irq_find_mapping(gc->irq.domain, n));
		stmfx_pinctrl_irq_toggle_trigger(pctl, n);
	}

	regmap_bulk_write(pctl->stmfx->map, STMFX_REG_IRQ_GPI_SRC,
			  pctl->irq_gpi_src, NR_GPIO_REGS);

	return IRQ_HANDLED;
}

static void stmfx_pinctrl_irq_print_chip(struct irq_data *d, struct seq_file *p)
{
	struct gpio_chip *gpio_chip = irq_data_get_irq_chip_data(d);
	struct stmfx_pinctrl *pctl = gpiochip_get_data(gpio_chip);

	seq_printf(p, dev_name(pctl->dev));
}

static const struct irq_chip stmfx_pinctrl_irq_chip = {
	.irq_mask = stmfx_pinctrl_irq_mask,
	.irq_unmask = stmfx_pinctrl_irq_unmask,
	.irq_set_type = stmfx_pinctrl_irq_set_type,
	.irq_bus_lock = stmfx_pinctrl_irq_bus_lock,
	.irq_bus_sync_unlock = stmfx_pinctrl_irq_bus_sync_unlock,
	.irq_request_resources = stmfx_gpio_irq_request_resources,
	.irq_release_resources = stmfx_gpio_irq_release_resources,
	.irq_print_chip = stmfx_pinctrl_irq_print_chip,
	.flags = IRQCHIP_IMMUTABLE,
};

static int stmfx_pinctrl_gpio_function_enable(struct stmfx_pinctrl *pctl)
{
	struct pinctrl_gpio_range *gpio_range;
	struct pinctrl_dev *pctl_dev = pctl->pctl_dev;
	u32 func = STMFX_FUNC_GPIO;

	pctl->gpio_valid_mask = GENMASK(15, 0);

	gpio_range = pinctrl_find_gpio_range_from_pin(pctl_dev, 16);
	if (gpio_range) {
		func |= STMFX_FUNC_ALTGPIO_LOW;
		pctl->gpio_valid_mask |= GENMASK(19, 16);
	}

	gpio_range = pinctrl_find_gpio_range_from_pin(pctl_dev, 20);
	if (gpio_range) {
		func |= STMFX_FUNC_ALTGPIO_HIGH;
		pctl->gpio_valid_mask |= GENMASK(23, 20);
	}

	return stmfx_function_enable(pctl->stmfx, func);
}

static int stmfx_pinctrl_probe(struct platform_device *pdev)
{
	struct stmfx *stmfx = dev_get_drvdata(pdev->dev.parent);
	struct device_node *np = pdev->dev.of_node;
	struct stmfx_pinctrl *pctl;
	struct gpio_irq_chip *girq;
	int irq, ret;

	pctl = devm_kzalloc(stmfx->dev, sizeof(*pctl), GFP_KERNEL);
	if (!pctl)
		return -ENOMEM;

	platform_set_drvdata(pdev, pctl);

	pctl->dev = &pdev->dev;
	pctl->stmfx = stmfx;

	if (!of_property_present(np, "gpio-ranges")) {
		dev_err(pctl->dev, "missing required gpio-ranges property\n");
		return -EINVAL;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	mutex_init(&pctl->lock);

	/* Register pin controller */
	pctl->pctl_desc.name = "stmfx-pinctrl";
	pctl->pctl_desc.pctlops = &stmfx_pinctrl_ops;
	pctl->pctl_desc.confops = &stmfx_pinconf_ops;
	pctl->pctl_desc.pins = stmfx_pins;
	pctl->pctl_desc.npins = ARRAY_SIZE(stmfx_pins);
	pctl->pctl_desc.owner = THIS_MODULE;
	pctl->pctl_desc.link_consumers = true;

	ret = devm_pinctrl_register_and_init(pctl->dev, &pctl->pctl_desc,
					     pctl, &pctl->pctl_dev);
	if (ret) {
		dev_err(pctl->dev, "pinctrl registration failed\n");
		return ret;
	}

	ret = pinctrl_enable(pctl->pctl_dev);
	if (ret) {
		dev_err(pctl->dev, "pinctrl enable failed\n");
		return ret;
	}

	/* Register gpio controller */
	pctl->gpio_chip.label = "stmfx-gpio";
	pctl->gpio_chip.parent = pctl->dev;
	pctl->gpio_chip.get_direction = stmfx_gpio_get_direction;
	pctl->gpio_chip.direction_input = stmfx_gpio_direction_input;
	pctl->gpio_chip.direction_output = stmfx_gpio_direction_output;
	pctl->gpio_chip.get = stmfx_gpio_get;
	pctl->gpio_chip.set = stmfx_gpio_set;
	pctl->gpio_chip.set_config = gpiochip_generic_config;
	pctl->gpio_chip.base = -1;
	pctl->gpio_chip.ngpio = pctl->pctl_desc.npins;
	pctl->gpio_chip.can_sleep = true;

	girq = &pctl->gpio_chip.irq;
	gpio_irq_chip_set_chip(girq, &stmfx_pinctrl_irq_chip);
	/* This will let us handle the parent IRQ in the driver */
	girq->parent_handler = NULL;
	girq->num_parents = 0;
	girq->parents = NULL;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_bad_irq;
	girq->threaded = true;

	ret = devm_gpiochip_add_data(pctl->dev, &pctl->gpio_chip, pctl);
	if (ret) {
		dev_err(pctl->dev, "gpio_chip registration failed\n");
		return ret;
	}

	ret = stmfx_pinctrl_gpio_function_enable(pctl);
	if (ret)
		return ret;

	ret = devm_request_threaded_irq(pctl->dev, irq, NULL,
					stmfx_pinctrl_irq_thread_fn,
					IRQF_ONESHOT,
					dev_name(pctl->dev), pctl);
	if (ret) {
		dev_err(pctl->dev, "cannot request irq%d\n", irq);
		return ret;
	}

	dev_info(pctl->dev,
		 "%ld GPIOs available\n", hweight_long(pctl->gpio_valid_mask));

	return 0;
}

static void stmfx_pinctrl_remove(struct platform_device *pdev)
{
	struct stmfx *stmfx = dev_get_drvdata(pdev->dev.parent);
	int ret;

	ret = stmfx_function_disable(stmfx,
				     STMFX_FUNC_GPIO |
				     STMFX_FUNC_ALTGPIO_LOW |
				     STMFX_FUNC_ALTGPIO_HIGH);
	if (ret)
		dev_err(&pdev->dev, "Failed to disable pins (%pe)\n",
			ERR_PTR(ret));
}

#ifdef CONFIG_PM_SLEEP
static int stmfx_pinctrl_backup_regs(struct stmfx_pinctrl *pctl)
{
	int ret;

	ret = regmap_bulk_read(pctl->stmfx->map, STMFX_REG_GPIO_STATE,
			       &pctl->bkp_gpio_state, NR_GPIO_REGS);
	if (ret)
		return ret;
	ret = regmap_bulk_read(pctl->stmfx->map, STMFX_REG_GPIO_DIR,
			       &pctl->bkp_gpio_dir, NR_GPIO_REGS);
	if (ret)
		return ret;
	ret = regmap_bulk_read(pctl->stmfx->map, STMFX_REG_GPIO_TYPE,
			       &pctl->bkp_gpio_type, NR_GPIO_REGS);
	if (ret)
		return ret;
	ret = regmap_bulk_read(pctl->stmfx->map, STMFX_REG_GPIO_PUPD,
			       &pctl->bkp_gpio_pupd, NR_GPIO_REGS);
	if (ret)
		return ret;

	return 0;
}

static int stmfx_pinctrl_restore_regs(struct stmfx_pinctrl *pctl)
{
	int ret;

	ret = regmap_bulk_write(pctl->stmfx->map, STMFX_REG_GPIO_DIR,
				pctl->bkp_gpio_dir, NR_GPIO_REGS);
	if (ret)
		return ret;
	ret = regmap_bulk_write(pctl->stmfx->map, STMFX_REG_GPIO_TYPE,
				pctl->bkp_gpio_type, NR_GPIO_REGS);
	if (ret)
		return ret;
	ret = regmap_bulk_write(pctl->stmfx->map, STMFX_REG_GPIO_PUPD,
				pctl->bkp_gpio_pupd, NR_GPIO_REGS);
	if (ret)
		return ret;
	ret = regmap_bulk_write(pctl->stmfx->map, STMFX_REG_GPO_SET,
				pctl->bkp_gpio_state, NR_GPIO_REGS);
	if (ret)
		return ret;
	ret = regmap_bulk_write(pctl->stmfx->map, STMFX_REG_IRQ_GPI_EVT,
				pctl->irq_gpi_evt, NR_GPIO_REGS);
	if (ret)
		return ret;
	ret = regmap_bulk_write(pctl->stmfx->map, STMFX_REG_IRQ_GPI_TYPE,
				pctl->irq_gpi_type, NR_GPIO_REGS);
	if (ret)
		return ret;
	ret = regmap_bulk_write(pctl->stmfx->map, STMFX_REG_IRQ_GPI_SRC,
				pctl->irq_gpi_src, NR_GPIO_REGS);
	if (ret)
		return ret;

	return 0;
}

static int stmfx_pinctrl_suspend(struct device *dev)
{
	struct stmfx_pinctrl *pctl = dev_get_drvdata(dev);
	int ret;

	ret = stmfx_pinctrl_backup_regs(pctl);
	if (ret) {
		dev_err(pctl->dev, "registers backup failure\n");
		return ret;
	}

	return 0;
}

static int stmfx_pinctrl_resume(struct device *dev)
{
	struct stmfx_pinctrl *pctl = dev_get_drvdata(dev);
	int ret;

	ret = stmfx_pinctrl_restore_regs(pctl);
	if (ret) {
		dev_err(pctl->dev, "registers restoration failure\n");
		return ret;
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(stmfx_pinctrl_dev_pm_ops,
			 stmfx_pinctrl_suspend, stmfx_pinctrl_resume);

static const struct of_device_id stmfx_pinctrl_of_match[] = {
	{ .compatible = "st,stmfx-0300-pinctrl", },
	{},
};
MODULE_DEVICE_TABLE(of, stmfx_pinctrl_of_match);

static struct platform_driver stmfx_pinctrl_driver = {
	.driver = {
		.name = "stmfx-pinctrl",
		.of_match_table = stmfx_pinctrl_of_match,
		.pm = &stmfx_pinctrl_dev_pm_ops,
	},
	.probe = stmfx_pinctrl_probe,
	.remove_new = stmfx_pinctrl_remove,
};
module_platform_driver(stmfx_pinctrl_driver);

MODULE_DESCRIPTION("STMFX pinctrl/GPIO driver");
MODULE_AUTHOR("Amelie Delaunay <amelie.delaunay@st.com>");
MODULE_LICENSE("GPL v2");
