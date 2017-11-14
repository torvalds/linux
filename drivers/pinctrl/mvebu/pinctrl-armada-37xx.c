/*
 * Marvell 37xx SoC pinctrl driver
 *
 * Copyright (C) 2017 Marvell
 *
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2 or later. This program is licensed "as is"
 * without any warranty of any kind, whether express or implied.
 */

#include <linux/gpio/driver.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "../pinctrl-utils.h"

#define OUTPUT_EN	0x0
#define INPUT_VAL	0x10
#define OUTPUT_VAL	0x18
#define OUTPUT_CTL	0x20
#define SELECTION	0x30

#define IRQ_EN		0x0
#define IRQ_POL		0x08
#define IRQ_STATUS	0x10
#define IRQ_WKUP	0x18

#define NB_FUNCS 3
#define GPIO_PER_REG	32

/**
 * struct armada_37xx_pin_group: represents group of pins of a pinmux function.
 * The pins of a pinmux groups are composed of one or two groups of contiguous
 * pins.
 * @name:	Name of the pin group, used to lookup the group.
 * @start_pins:	Index of the first pin of the main range of pins belonging to
 *		the group
 * @npins:	Number of pins included in the first range
 * @reg_mask:	Bit mask matching the group in the selection register
 * @extra_pins:	Index of the first pin of the optional second range of pins
 *		belonging to the group
 * @npins:	Number of pins included in the second optional range
 * @funcs:	A list of pinmux functions that can be selected for this group.
 * @pins:	List of the pins included in the group
 */
struct armada_37xx_pin_group {
	const char	*name;
	unsigned int	start_pin;
	unsigned int	npins;
	u32		reg_mask;
	u32		val[NB_FUNCS];
	unsigned int	extra_pin;
	unsigned int	extra_npins;
	const char	*funcs[NB_FUNCS];
	unsigned int	*pins;
};

struct armada_37xx_pin_data {
	u8				nr_pins;
	char				*name;
	struct armada_37xx_pin_group	*groups;
	int				ngroups;
};

struct armada_37xx_pmx_func {
	const char		*name;
	const char		**groups;
	unsigned int		ngroups;
};

struct armada_37xx_pinctrl {
	struct regmap			*regmap;
	void __iomem			*base;
	const struct armada_37xx_pin_data	*data;
	struct device			*dev;
	struct gpio_chip		gpio_chip;
	struct irq_chip			irq_chip;
	spinlock_t			irq_lock;
	struct pinctrl_desc		pctl;
	struct pinctrl_dev		*pctl_dev;
	struct armada_37xx_pin_group	*groups;
	unsigned int			ngroups;
	struct armada_37xx_pmx_func	*funcs;
	unsigned int			nfuncs;
};

#define PIN_GRP(_name, _start, _nr, _mask, _func1, _func2)	\
	{					\
		.name = _name,			\
		.start_pin = _start,		\
		.npins = _nr,			\
		.reg_mask = _mask,		\
		.val = {0, _mask},		\
		.funcs = {_func1, _func2}	\
	}

#define PIN_GRP_GPIO(_name, _start, _nr, _mask, _func1)	\
	{					\
		.name = _name,			\
		.start_pin = _start,		\
		.npins = _nr,			\
		.reg_mask = _mask,		\
		.val = {0, _mask},		\
		.funcs = {_func1, "gpio"}	\
	}

#define PIN_GRP_GPIO_2(_name, _start, _nr, _mask, _val1, _val2, _func1)   \
	{					\
		.name = _name,			\
		.start_pin = _start,		\
		.npins = _nr,			\
		.reg_mask = _mask,		\
		.val = {_val1, _val2},		\
		.funcs = {_func1, "gpio"}	\
	}

#define PIN_GRP_GPIO_3(_name, _start, _nr, _mask, _v1, _v2, _v3, _f1, _f2) \
	{					\
		.name = _name,			\
		.start_pin = _start,		\
		.npins = _nr,			\
		.reg_mask = _mask,		\
		.val = {_v1, _v2, _v3},	\
		.funcs = {_f1, _f2, "gpio"}	\
	}

#define PIN_GRP_EXTRA(_name, _start, _nr, _mask, _v1, _v2, _start2, _nr2, \
		      _f1, _f2)				\
	{						\
		.name = _name,				\
		.start_pin = _start,			\
		.npins = _nr,				\
		.reg_mask = _mask,			\
		.val = {_v1, _v2},			\
		.extra_pin = _start2,			\
		.extra_npins = _nr2,			\
		.funcs = {_f1, _f2}			\
	}

static struct armada_37xx_pin_group armada_37xx_nb_groups[] = {
	PIN_GRP_GPIO("jtag", 20, 5, BIT(0), "jtag"),
	PIN_GRP_GPIO("sdio0", 8, 3, BIT(1), "sdio"),
	PIN_GRP_GPIO("emmc_nb", 27, 9, BIT(2), "emmc"),
	PIN_GRP_GPIO("pwm0", 11, 1, BIT(3), "pwm"),
	PIN_GRP_GPIO("pwm1", 12, 1, BIT(4), "pwm"),
	PIN_GRP_GPIO("pwm2", 13, 1, BIT(5), "pwm"),
	PIN_GRP_GPIO("pwm3", 14, 1, BIT(6), "pwm"),
	PIN_GRP_GPIO("pmic1", 17, 1, BIT(7), "pmic"),
	PIN_GRP_GPIO("pmic0", 16, 1, BIT(8), "pmic"),
	PIN_GRP_GPIO("i2c2", 2, 2, BIT(9), "i2c"),
	PIN_GRP_GPIO("i2c1", 0, 2, BIT(10), "i2c"),
	PIN_GRP_GPIO("spi_cs1", 17, 1, BIT(12), "spi"),
	PIN_GRP_GPIO_2("spi_cs2", 18, 1, BIT(13) | BIT(19), 0, BIT(13), "spi"),
	PIN_GRP_GPIO_2("spi_cs3", 19, 1, BIT(14) | BIT(19), 0, BIT(14), "spi"),
	PIN_GRP_GPIO("onewire", 4, 1, BIT(16), "onewire"),
	PIN_GRP_GPIO("uart1", 25, 2, BIT(17), "uart"),
	PIN_GRP_GPIO("spi_quad", 15, 2, BIT(18), "spi"),
	PIN_GRP_EXTRA("uart2", 9, 2, BIT(1) | BIT(13) | BIT(14) | BIT(19),
		      BIT(1) | BIT(13) | BIT(14), BIT(1) | BIT(19),
		      18, 2, "gpio", "uart"),
	PIN_GRP_GPIO("led0_od", 11, 1, BIT(20), "led"),
	PIN_GRP_GPIO("led1_od", 12, 1, BIT(21), "led"),
	PIN_GRP_GPIO("led2_od", 13, 1, BIT(22), "led"),
	PIN_GRP_GPIO("led3_od", 14, 1, BIT(23), "led"),

};

static struct armada_37xx_pin_group armada_37xx_sb_groups[] = {
	PIN_GRP_GPIO("usb32_drvvbus0", 0, 1, BIT(0), "drvbus"),
	PIN_GRP_GPIO("usb2_drvvbus1", 1, 1, BIT(1), "drvbus"),
	PIN_GRP_GPIO("sdio_sb", 24, 6, BIT(2), "sdio"),
	PIN_GRP_GPIO("rgmii", 6, 12, BIT(3), "mii"),
	PIN_GRP_GPIO("pcie1", 3, 2, BIT(4), "pcie"),
	PIN_GRP_GPIO("ptp", 20, 3, BIT(5), "ptp"),
	PIN_GRP("ptp_clk", 21, 1, BIT(6), "ptp", "mii"),
	PIN_GRP("ptp_trig", 22, 1, BIT(7), "ptp", "mii"),
	PIN_GRP_GPIO_3("mii_col", 23, 1, BIT(8) | BIT(14), 0, BIT(8), BIT(14),
		       "mii", "mii_err"),
};

static const struct armada_37xx_pin_data armada_37xx_pin_nb = {
	.nr_pins = 36,
	.name = "GPIO1",
	.groups = armada_37xx_nb_groups,
	.ngroups = ARRAY_SIZE(armada_37xx_nb_groups),
};

static const struct armada_37xx_pin_data armada_37xx_pin_sb = {
	.nr_pins = 30,
	.name = "GPIO2",
	.groups = armada_37xx_sb_groups,
	.ngroups = ARRAY_SIZE(armada_37xx_sb_groups),
};

static inline void armada_37xx_update_reg(unsigned int *reg,
					  unsigned int offset)
{
	/* We never have more than 2 registers */
	if (offset >= GPIO_PER_REG) {
		offset -= GPIO_PER_REG;
		*reg += sizeof(u32);
	}
}

static int armada_37xx_get_func_reg(struct armada_37xx_pin_group *grp,
				    const char *func)
{
	int f;

	for (f = 0; (f < NB_FUNCS) && grp->funcs[f]; f++)
		if (!strcmp(grp->funcs[f], func))
			return f;

	return -ENOTSUPP;
}

static struct armada_37xx_pin_group *armada_37xx_find_next_grp_by_pin(
	struct armada_37xx_pinctrl *info, int pin, int *grp)
{
	while (*grp < info->ngroups) {
		struct armada_37xx_pin_group *group = &info->groups[*grp];
		int j;

		*grp = *grp + 1;
		for (j = 0; j < (group->npins + group->extra_npins); j++)
			if (group->pins[j] == pin)
				return group;
	}
	return NULL;
}

static int armada_37xx_pin_config_group_get(struct pinctrl_dev *pctldev,
			    unsigned int selector, unsigned long *config)
{
	return -ENOTSUPP;
}

static int armada_37xx_pin_config_group_set(struct pinctrl_dev *pctldev,
			    unsigned int selector, unsigned long *configs,
			    unsigned int num_configs)
{
	return -ENOTSUPP;
}

static const struct pinconf_ops armada_37xx_pinconf_ops = {
	.is_generic = true,
	.pin_config_group_get = armada_37xx_pin_config_group_get,
	.pin_config_group_set = armada_37xx_pin_config_group_set,
};

static int armada_37xx_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct armada_37xx_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	return info->ngroups;
}

static const char *armada_37xx_get_group_name(struct pinctrl_dev *pctldev,
					      unsigned int group)
{
	struct armada_37xx_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	return info->groups[group].name;
}

static int armada_37xx_get_group_pins(struct pinctrl_dev *pctldev,
				      unsigned int selector,
				      const unsigned int **pins,
				      unsigned int *npins)
{
	struct armada_37xx_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	if (selector >= info->ngroups)
		return -EINVAL;

	*pins = info->groups[selector].pins;
	*npins = info->groups[selector].npins +
		info->groups[selector].extra_npins;

	return 0;
}

static const struct pinctrl_ops armada_37xx_pctrl_ops = {
	.get_groups_count	= armada_37xx_get_groups_count,
	.get_group_name		= armada_37xx_get_group_name,
	.get_group_pins		= armada_37xx_get_group_pins,
	.dt_node_to_map		= pinconf_generic_dt_node_to_map_group,
	.dt_free_map		= pinctrl_utils_free_map,
};

/*
 * Pinmux_ops handling
 */

static int armada_37xx_pmx_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct armada_37xx_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	return info->nfuncs;
}

static const char *armada_37xx_pmx_get_func_name(struct pinctrl_dev *pctldev,
						 unsigned int selector)
{
	struct armada_37xx_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	return info->funcs[selector].name;
}

static int armada_37xx_pmx_get_groups(struct pinctrl_dev *pctldev,
				      unsigned int selector,
				      const char * const **groups,
				      unsigned int * const num_groups)
{
	struct armada_37xx_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	*groups = info->funcs[selector].groups;
	*num_groups = info->funcs[selector].ngroups;

	return 0;
}

static int armada_37xx_pmx_set_by_name(struct pinctrl_dev *pctldev,
				       const char *name,
				       struct armada_37xx_pin_group *grp)
{
	struct armada_37xx_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	unsigned int reg = SELECTION;
	unsigned int mask = grp->reg_mask;
	int func, val;

	dev_dbg(info->dev, "enable function %s group %s\n",
		name, grp->name);

	func = armada_37xx_get_func_reg(grp, name);

	if (func < 0)
		return func;

	val = grp->val[func];

	regmap_update_bits(info->regmap, reg, mask, val);

	return 0;
}

static int armada_37xx_pmx_set(struct pinctrl_dev *pctldev,
			       unsigned int selector,
			       unsigned int group)
{

	struct armada_37xx_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	struct armada_37xx_pin_group *grp = &info->groups[group];
	const char *name = info->funcs[selector].name;

	return armada_37xx_pmx_set_by_name(pctldev, name, grp);
}

static inline void armada_37xx_irq_update_reg(unsigned int *reg,
					  struct irq_data *d)
{
	int offset = irqd_to_hwirq(d);

	armada_37xx_update_reg(reg, offset);
}

static int armada_37xx_gpio_direction_input(struct gpio_chip *chip,
					    unsigned int offset)
{
	struct armada_37xx_pinctrl *info = gpiochip_get_data(chip);
	unsigned int reg = OUTPUT_EN;
	unsigned int mask;

	armada_37xx_update_reg(&reg, offset);
	mask = BIT(offset);

	return regmap_update_bits(info->regmap, reg, mask, 0);
}

static int armada_37xx_gpio_get_direction(struct gpio_chip *chip,
					  unsigned int offset)
{
	struct armada_37xx_pinctrl *info = gpiochip_get_data(chip);
	unsigned int reg = OUTPUT_EN;
	unsigned int val, mask;

	armada_37xx_update_reg(&reg, offset);
	mask = BIT(offset);
	regmap_read(info->regmap, reg, &val);

	return !(val & mask);
}

static int armada_37xx_gpio_direction_output(struct gpio_chip *chip,
					     unsigned int offset, int value)
{
	struct armada_37xx_pinctrl *info = gpiochip_get_data(chip);
	unsigned int reg = OUTPUT_EN;
	unsigned int mask, val, ret;

	armada_37xx_update_reg(&reg, offset);
	mask = BIT(offset);

	ret = regmap_update_bits(info->regmap, reg, mask, mask);

	if (ret)
		return ret;

	reg = OUTPUT_VAL;
	val = value ? mask : 0;
	regmap_update_bits(info->regmap, reg, mask, val);

	return 0;
}

static int armada_37xx_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct armada_37xx_pinctrl *info = gpiochip_get_data(chip);
	unsigned int reg = INPUT_VAL;
	unsigned int val, mask;

	armada_37xx_update_reg(&reg, offset);
	mask = BIT(offset);

	regmap_read(info->regmap, reg, &val);

	return (val & mask) != 0;
}

static void armada_37xx_gpio_set(struct gpio_chip *chip, unsigned int offset,
				 int value)
{
	struct armada_37xx_pinctrl *info = gpiochip_get_data(chip);
	unsigned int reg = OUTPUT_VAL;
	unsigned int mask, val;

	armada_37xx_update_reg(&reg, offset);
	mask = BIT(offset);
	val = value ? mask : 0;

	regmap_update_bits(info->regmap, reg, mask, val);
}

static int armada_37xx_pmx_gpio_set_direction(struct pinctrl_dev *pctldev,
					      struct pinctrl_gpio_range *range,
					      unsigned int offset, bool input)
{
	struct armada_37xx_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	struct gpio_chip *chip = range->gc;

	dev_dbg(info->dev, "gpio_direction for pin %u as %s-%d to %s\n",
		offset, range->name, offset, input ? "input" : "output");

	if (input)
		armada_37xx_gpio_direction_input(chip, offset);
	else
		armada_37xx_gpio_direction_output(chip, offset, 0);

	return 0;
}

static int armada_37xx_gpio_request_enable(struct pinctrl_dev *pctldev,
					   struct pinctrl_gpio_range *range,
					   unsigned int offset)
{
	struct armada_37xx_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	struct armada_37xx_pin_group *group;
	int grp = 0;

	dev_dbg(info->dev, "requesting gpio %d\n", offset);

	while ((group = armada_37xx_find_next_grp_by_pin(info, offset, &grp)))
		armada_37xx_pmx_set_by_name(pctldev, "gpio", group);

	return 0;
}

static const struct pinmux_ops armada_37xx_pmx_ops = {
	.get_functions_count	= armada_37xx_pmx_get_funcs_count,
	.get_function_name	= armada_37xx_pmx_get_func_name,
	.get_function_groups	= armada_37xx_pmx_get_groups,
	.set_mux		= armada_37xx_pmx_set,
	.gpio_request_enable	= armada_37xx_gpio_request_enable,
	.gpio_set_direction	= armada_37xx_pmx_gpio_set_direction,
};

static const struct gpio_chip armada_37xx_gpiolib_chip = {
	.request = gpiochip_generic_request,
	.free = gpiochip_generic_free,
	.set = armada_37xx_gpio_set,
	.get = armada_37xx_gpio_get,
	.get_direction	= armada_37xx_gpio_get_direction,
	.direction_input = armada_37xx_gpio_direction_input,
	.direction_output = armada_37xx_gpio_direction_output,
	.owner = THIS_MODULE,
};

static void armada_37xx_irq_ack(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);
	struct armada_37xx_pinctrl *info = gpiochip_get_data(chip);
	u32 reg = IRQ_STATUS;
	unsigned long flags;

	armada_37xx_irq_update_reg(&reg, d);
	spin_lock_irqsave(&info->irq_lock, flags);
	writel(d->mask, info->base + reg);
	spin_unlock_irqrestore(&info->irq_lock, flags);
}

static void armada_37xx_irq_mask(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);
	struct armada_37xx_pinctrl *info = gpiochip_get_data(chip);
	u32 val, reg = IRQ_EN;
	unsigned long flags;

	armada_37xx_irq_update_reg(&reg, d);
	spin_lock_irqsave(&info->irq_lock, flags);
	val = readl(info->base + reg);
	writel(val & ~d->mask, info->base + reg);
	spin_unlock_irqrestore(&info->irq_lock, flags);
}

static void armada_37xx_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);
	struct armada_37xx_pinctrl *info = gpiochip_get_data(chip);
	u32 val, reg = IRQ_EN;
	unsigned long flags;

	armada_37xx_irq_update_reg(&reg, d);
	spin_lock_irqsave(&info->irq_lock, flags);
	val = readl(info->base + reg);
	writel(val | d->mask, info->base + reg);
	spin_unlock_irqrestore(&info->irq_lock, flags);
}

static int armada_37xx_irq_set_wake(struct irq_data *d, unsigned int on)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);
	struct armada_37xx_pinctrl *info = gpiochip_get_data(chip);
	u32 val, reg = IRQ_WKUP;
	unsigned long flags;

	armada_37xx_irq_update_reg(&reg, d);
	spin_lock_irqsave(&info->irq_lock, flags);
	val = readl(info->base + reg);
	if (on)
		val |= (BIT(d->hwirq % GPIO_PER_REG));
	else
		val &= ~(BIT(d->hwirq % GPIO_PER_REG));
	writel(val, info->base + reg);
	spin_unlock_irqrestore(&info->irq_lock, flags);

	return 0;
}

static int armada_37xx_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);
	struct armada_37xx_pinctrl *info = gpiochip_get_data(chip);
	u32 val, reg = IRQ_POL;
	unsigned long flags;

	spin_lock_irqsave(&info->irq_lock, flags);
	armada_37xx_irq_update_reg(&reg, d);
	val = readl(info->base + reg);
	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		val &= ~(BIT(d->hwirq % GPIO_PER_REG));
		break;
	case IRQ_TYPE_EDGE_FALLING:
		val |= (BIT(d->hwirq % GPIO_PER_REG));
		break;
	default:
		spin_unlock_irqrestore(&info->irq_lock, flags);
		return -EINVAL;
	}
	writel(val, info->base + reg);
	spin_unlock_irqrestore(&info->irq_lock, flags);

	return 0;
}


static void armada_37xx_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct armada_37xx_pinctrl *info = gpiochip_get_data(gc);
	struct irq_domain *d = gc->irqdomain;
	int i;

	chained_irq_enter(chip, desc);
	for (i = 0; i <= d->revmap_size / GPIO_PER_REG; i++) {
		u32 status;
		unsigned long flags;

		spin_lock_irqsave(&info->irq_lock, flags);
		status = readl_relaxed(info->base + IRQ_STATUS + 4 * i);
		/* Manage only the interrupt that was enabled */
		status &= readl_relaxed(info->base + IRQ_EN + 4 * i);
		spin_unlock_irqrestore(&info->irq_lock, flags);
		while (status) {
			u32 hwirq = ffs(status) - 1;
			u32 virq = irq_find_mapping(d, hwirq +
						     i * GPIO_PER_REG);

			generic_handle_irq(virq);

			/* Update status in case a new IRQ appears */
			spin_lock_irqsave(&info->irq_lock, flags);
			status = readl_relaxed(info->base +
					       IRQ_STATUS + 4 * i);
			/* Manage only the interrupt that was enabled */
			status &= readl_relaxed(info->base + IRQ_EN + 4 * i);
			spin_unlock_irqrestore(&info->irq_lock, flags);
		}
	}
	chained_irq_exit(chip, desc);
}

static unsigned int armada_37xx_irq_startup(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);
	int irq = d->hwirq - chip->irq_base;
	/*
	 * The mask field is a "precomputed bitmask for accessing the
	 * chip registers" which was introduced for the generic
	 * irqchip framework. As we don't use this framework, we can
	 * reuse this field for our own usage.
	 */
	d->mask = BIT(irq % GPIO_PER_REG);

	armada_37xx_irq_unmask(d);

	return 0;
}

static int armada_37xx_irqchip_register(struct platform_device *pdev,
					struct armada_37xx_pinctrl *info)
{
	struct device_node *np = info->dev->of_node;
	struct gpio_chip *gc = &info->gpio_chip;
	struct irq_chip *irqchip = &info->irq_chip;
	struct resource res;
	int ret = -ENODEV, i, nr_irq_parent;

	/* Check if we have at least one gpio-controller child node */
	for_each_child_of_node(info->dev->of_node, np) {
		if (of_property_read_bool(np, "gpio-controller")) {
			ret = 0;
			break;
		}
	};
	if (ret)
		return ret;

	nr_irq_parent = of_irq_count(np);
	spin_lock_init(&info->irq_lock);

	if (!nr_irq_parent) {
		dev_err(&pdev->dev, "Invalid or no IRQ\n");
		return 0;
	}

	if (of_address_to_resource(info->dev->of_node, 1, &res)) {
		dev_err(info->dev, "cannot find IO resource\n");
		return -ENOENT;
	}

	info->base = devm_ioremap_resource(info->dev, &res);
	if (IS_ERR(info->base))
		return PTR_ERR(info->base);

	irqchip->irq_ack = armada_37xx_irq_ack;
	irqchip->irq_mask = armada_37xx_irq_mask;
	irqchip->irq_unmask = armada_37xx_irq_unmask;
	irqchip->irq_set_wake = armada_37xx_irq_set_wake;
	irqchip->irq_set_type = armada_37xx_irq_set_type;
	irqchip->irq_startup = armada_37xx_irq_startup;
	irqchip->name = info->data->name;
	ret = gpiochip_irqchip_add(gc, irqchip, 0,
				   handle_edge_irq, IRQ_TYPE_NONE);
	if (ret) {
		dev_info(&pdev->dev, "could not add irqchip\n");
		return ret;
	}

	/*
	 * Many interrupts are connected to the parent interrupt
	 * controller. But we do not take advantage of this and use
	 * the chained irq with all of them.
	 */
	for (i = 0; i < nr_irq_parent; i++) {
		int irq = irq_of_parse_and_map(np, i);

		if (irq < 0)
			continue;

		gpiochip_set_chained_irqchip(gc, irqchip, irq,
					     armada_37xx_irq_handler);
	}

	return 0;
}

static int armada_37xx_gpiochip_register(struct platform_device *pdev,
					struct armada_37xx_pinctrl *info)
{
	struct device_node *np;
	struct gpio_chip *gc;
	int ret = -ENODEV;

	for_each_child_of_node(info->dev->of_node, np) {
		if (of_find_property(np, "gpio-controller", NULL)) {
			ret = 0;
			break;
		}
	};
	if (ret)
		return ret;

	info->gpio_chip = armada_37xx_gpiolib_chip;

	gc = &info->gpio_chip;
	gc->ngpio = info->data->nr_pins;
	gc->parent = &pdev->dev;
	gc->base = -1;
	gc->of_node = np;
	gc->label = info->data->name;

	ret = devm_gpiochip_add_data(&pdev->dev, gc, info);
	if (ret)
		return ret;
	ret = armada_37xx_irqchip_register(pdev, info);
	if (ret)
		return ret;

	return 0;
}

/**
 * armada_37xx_add_function() - Add a new function to the list
 * @funcs: array of function to add the new one
 * @funcsize: size of the remaining space for the function
 * @name: name of the function to add
 *
 * If it is a new function then create it by adding its name else
 * increment the number of group associated to this function.
 */
static int armada_37xx_add_function(struct armada_37xx_pmx_func *funcs,
				    int *funcsize, const char *name)
{
	int i = 0;

	if (*funcsize <= 0)
		return -EOVERFLOW;

	while (funcs->ngroups) {
		/* function already there */
		if (strcmp(funcs->name, name) == 0) {
			funcs->ngroups++;

			return -EEXIST;
		}
		funcs++;
		i++;
	}

	/* append new unique function */
	funcs->name = name;
	funcs->ngroups = 1;
	(*funcsize)--;

	return 0;
}

/**
 * armada_37xx_fill_group() - complete the group array
 * @info: info driver instance
 *
 * Based on the data available from the armada_37xx_pin_group array
 * completes the last member of the struct for each function: the list
 * of the groups associated to this function.
 *
 */
static int armada_37xx_fill_group(struct armada_37xx_pinctrl *info)
{
	int n, num = 0, funcsize = info->data->nr_pins;

	for (n = 0; n < info->ngroups; n++) {
		struct armada_37xx_pin_group *grp = &info->groups[n];
		int i, j, f;

		grp->pins = devm_kzalloc(info->dev,
					 (grp->npins + grp->extra_npins) *
					 sizeof(*grp->pins), GFP_KERNEL);
		if (!grp->pins)
			return -ENOMEM;

		for (i = 0; i < grp->npins; i++)
			grp->pins[i] = grp->start_pin + i;

		for (j = 0; j < grp->extra_npins; j++)
			grp->pins[i+j] = grp->extra_pin + j;

		for (f = 0; (f < NB_FUNCS) && grp->funcs[f]; f++) {
			int ret;
			/* check for unique functions and count groups */
			ret = armada_37xx_add_function(info->funcs, &funcsize,
					    grp->funcs[f]);
			if (ret == -EOVERFLOW)
				dev_err(info->dev,
					"More functions than pins(%d)\n",
					info->data->nr_pins);
			if (ret < 0)
				continue;
			num++;
		}
	}

	info->nfuncs = num;

	return 0;
}

/**
 * armada_37xx_fill_funcs() - complete the funcs array
 * @info: info driver instance
 *
 * Based on the data available from the armada_37xx_pin_group array
 * completes the last two member of the struct for each group:
 * - the list of the pins included in the group
 * - the list of pinmux functions that can be selected for this group
 *
 */
static int armada_37xx_fill_func(struct armada_37xx_pinctrl *info)
{
	struct armada_37xx_pmx_func *funcs = info->funcs;
	int n;

	for (n = 0; n < info->nfuncs; n++) {
		const char *name = funcs[n].name;
		const char **groups;
		int g;

		funcs[n].groups = devm_kzalloc(info->dev, funcs[n].ngroups *
					       sizeof(*(funcs[n].groups)),
					       GFP_KERNEL);
		if (!funcs[n].groups)
			return -ENOMEM;

		groups = funcs[n].groups;

		for (g = 0; g < info->ngroups; g++) {
			struct armada_37xx_pin_group *gp = &info->groups[g];
			int f;

			for (f = 0; (f < NB_FUNCS) && gp->funcs[f]; f++) {
				if (strcmp(gp->funcs[f], name) == 0) {
					*groups = gp->name;
					groups++;
				}
			}
		}
	}
	return 0;
}

static int armada_37xx_pinctrl_register(struct platform_device *pdev,
					struct armada_37xx_pinctrl *info)
{
	const struct armada_37xx_pin_data *pin_data = info->data;
	struct pinctrl_desc *ctrldesc = &info->pctl;
	struct pinctrl_pin_desc *pindesc, *pdesc;
	int pin, ret;

	info->groups = pin_data->groups;
	info->ngroups = pin_data->ngroups;

	ctrldesc->name = "armada_37xx-pinctrl";
	ctrldesc->owner = THIS_MODULE;
	ctrldesc->pctlops = &armada_37xx_pctrl_ops;
	ctrldesc->pmxops = &armada_37xx_pmx_ops;
	ctrldesc->confops = &armada_37xx_pinconf_ops;

	pindesc = devm_kzalloc(&pdev->dev, sizeof(*pindesc) *
			       pin_data->nr_pins, GFP_KERNEL);
	if (!pindesc)
		return -ENOMEM;

	ctrldesc->pins = pindesc;
	ctrldesc->npins = pin_data->nr_pins;

	pdesc = pindesc;
	for (pin = 0; pin < pin_data->nr_pins; pin++) {
		pdesc->number = pin;
		pdesc->name = kasprintf(GFP_KERNEL, "%s-%d",
					pin_data->name, pin);
		pdesc++;
	}

	/*
	 * we allocate functions for number of pins and hope there are
	 * fewer unique functions than pins available
	 */
	info->funcs = devm_kzalloc(&pdev->dev, pin_data->nr_pins *
			   sizeof(struct armada_37xx_pmx_func), GFP_KERNEL);
	if (!info->funcs)
		return -ENOMEM;


	ret = armada_37xx_fill_group(info);
	if (ret)
		return ret;

	ret = armada_37xx_fill_func(info);
	if (ret)
		return ret;

	info->pctl_dev = devm_pinctrl_register(&pdev->dev, ctrldesc, info);
	if (IS_ERR(info->pctl_dev)) {
		dev_err(&pdev->dev, "could not register pinctrl driver\n");
		return PTR_ERR(info->pctl_dev);
	}

	return 0;
}

static const struct of_device_id armada_37xx_pinctrl_of_match[] = {
	{
		.compatible = "marvell,armada3710-sb-pinctrl",
		.data = (void *)&armada_37xx_pin_sb,
	},
	{
		.compatible = "marvell,armada3710-nb-pinctrl",
		.data = (void *)&armada_37xx_pin_nb,
	},
	{ },
};

static int __init armada_37xx_pinctrl_probe(struct platform_device *pdev)
{
	struct armada_37xx_pinctrl *info;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct regmap *regmap;
	int ret;

	info = devm_kzalloc(dev, sizeof(struct armada_37xx_pinctrl),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = dev;

	regmap = syscon_node_to_regmap(np);
	if (IS_ERR(regmap)) {
		dev_err(&pdev->dev, "cannot get regmap\n");
		return PTR_ERR(regmap);
	}
	info->regmap = regmap;

	info->data = of_device_get_match_data(dev);

	ret = armada_37xx_pinctrl_register(pdev, info);
	if (ret)
		return ret;

	ret = armada_37xx_gpiochip_register(pdev, info);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, info);

	return 0;
}

static struct platform_driver armada_37xx_pinctrl_driver = {
	.driver = {
		.name = "armada-37xx-pinctrl",
		.of_match_table = armada_37xx_pinctrl_of_match,
	},
};

builtin_platform_driver_probe(armada_37xx_pinctrl_driver,
			      armada_37xx_pinctrl_probe);
