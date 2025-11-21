// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Nuvoton Technology Corp.
 *
 * Author: Shan-Chun Hung <schung@nuvoton.com>
 * *       Jacky Huang <ychuang3@nuvoton.com>
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/gpio/driver.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>

#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include "../core.h"
#include "../pinconf.h"
#include "pinctrl-ma35.h"

#define MA35_MFP_REG_BASE		0x80
#define MA35_MFP_REG_SZ_PER_BANK	8
#define MA35_MFP_BITS_PER_PORT		4

#define MA35_GPIO_BANK_MAX		14
#define MA35_GPIO_PORT_MAX		16

/* GPIO control registers */
#define MA35_GP_REG_MODE		0x00
#define MA35_GP_REG_DINOFF		0x04
#define MA35_GP_REG_DOUT		0x08
#define MA35_GP_REG_DATMSK		0x0c
#define MA35_GP_REG_PIN			0x10
#define MA35_GP_REG_DBEN		0x14
#define MA35_GP_REG_INTTYPE		0x18
#define MA35_GP_REG_INTEN		0x1c
#define MA35_GP_REG_INTSRC		0x20
#define MA35_GP_REG_SMTEN		0x24
#define MA35_GP_REG_SLEWCTL		0x28
#define MA35_GP_REG_SPW			0x2c
#define MA35_GP_REG_PUSEL		0x30
#define MA35_GP_REG_DSL			0x38
#define MA35_GP_REG_DSH			0x3c

/* GPIO mode control */
#define MA35_GP_MODE_INPUT		0x0
#define MA35_GP_MODE_OUTPUT		0x1
#define MA35_GP_MODE_OPEN_DRAIN		0x2
#define MA35_GP_MODE_QUASI		0x3
#define MA35_GP_MODE_MASK(n)		GENMASK(n * 2 + 1, n * 2)

#define MA35_GP_SLEWCTL_MASK(n)		GENMASK(n * 2 + 1, n * 2)

/* GPIO pull-up and pull-down selection control */
#define MA35_GP_PUSEL_DISABLE		0x0
#define MA35_GP_PUSEL_PULL_UP		0x1
#define MA35_GP_PUSEL_PULL_DOWN		0x2
#define MA35_GP_PUSEL_MASK(n)		GENMASK(n * 2 + 1, n * 2)

/*
 * The MA35_GP_REG_INTEN bits 0 ~ 15 control low-level or falling edge trigger,
 * while bits 16 ~ 31 control high-level or rising edge trigger.
 */
#define MA35_GP_INTEN_L(n)		BIT(n)
#define MA35_GP_INTEN_H(n)		BIT(n + 16)
#define MA35_GP_INTEN_BOTH(n)		(MA35_GP_INTEN_H(n) | MA35_GP_INTEN_L(n))

/*
 * The MA35_GP_REG_DSL register controls ports 0 to 7, while the MA35_GP_REG_DSH
 * register controls ports 8 to 15. Each port occupies a width of 4 bits, with 3
 * bits being effective.
 */
#define MA35_GP_DS_REG(n)		(n < 8 ? MA35_GP_REG_DSL : MA35_GP_REG_DSH)
#define MA35_GP_DS_MASK(n)		GENMASK((n % 8) * 4 + 3, (n % 8) * 4)

#define MVOLT_1800			0
#define MVOLT_3300			1

/* Non-constant mask variant of FIELD_GET() and FIELD_PREP() */
#define field_get(_mask, _reg)	(((_reg) & (_mask)) >> (ffs(_mask) - 1))
#define field_prep(_mask, _val)	(((_val) << (ffs(_mask) - 1)) & (_mask))

static const char * const gpio_group_name[] = {
	"gpioa", "gpiob", "gpioc", "gpiod", "gpioe", "gpiof", "gpiog",
	"gpioh", "gpioi", "gpioj", "gpiok", "gpiol", "gpiom", "gpion",
};

static const u32 ds_1800mv_tbl[] = {
	2900, 4400, 5800, 7300, 8600, 10100, 11500, 13000,
};

static const u32 ds_3300mv_tbl[] = {
	17100, 25600, 34100, 42800, 48000, 56000, 77000, 82000,
};

struct ma35_pin_setting {
	u32			offset;
	u32			shift;
	u32			muxval;
	unsigned long		*configs;
	unsigned int		nconfigs;
};

struct ma35_pin_bank {
	void __iomem		*reg_base;
	struct clk		*clk;
	int			irq;
	u8			bank_num;
	u8			nr_pins;
	bool			valid;
	const char		*name;
	struct fwnode_handle	*fwnode;
	struct gpio_chip	chip;
	u32			irqtype;
	u32			irqinten;
	struct regmap		*regmap;
	struct device		*dev;
};

struct ma35_pin_ctrl {
	struct ma35_pin_bank	*pin_banks;
	u32			nr_banks;
	u32			nr_pins;
};

struct ma35_pinctrl {
	struct device		*dev;
	struct ma35_pin_ctrl	*ctrl;
	struct pinctrl_dev	*pctl;
	const struct ma35_pinctrl_soc_info *info;
	struct regmap		*regmap;
	struct group_desc	*groups;
	unsigned int		ngroups;
	struct pinfunction	*functions;
	unsigned int		nfunctions;
};

static DEFINE_RAW_SPINLOCK(ma35_lock);

static int ma35_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct ma35_pinctrl *npctl = pinctrl_dev_get_drvdata(pctldev);

	return npctl->ngroups;
}

static const char *ma35_get_group_name(struct pinctrl_dev *pctldev, unsigned int selector)
{
	struct ma35_pinctrl *npctl = pinctrl_dev_get_drvdata(pctldev);

	return npctl->groups[selector].grp.name;
}

static int ma35_get_group_pins(struct pinctrl_dev *pctldev, unsigned int selector,
			       const unsigned int **pins, unsigned int *npins)
{
	struct ma35_pinctrl *npctl = pinctrl_dev_get_drvdata(pctldev);

	if (selector >= npctl->ngroups)
		return -EINVAL;

	*pins = npctl->groups[selector].grp.pins;
	*npins = npctl->groups[selector].grp.npins;

	return 0;
}

static struct group_desc *
ma35_pinctrl_find_group_by_name(const struct ma35_pinctrl *npctl, const char *name)
{
	int i;

	for (i = 0; i < npctl->ngroups; i++) {
		if (!strcmp(npctl->groups[i].grp.name, name))
			return &npctl->groups[i];
	}
	return NULL;
}

static int ma35_pinctrl_dt_node_to_map_func(struct pinctrl_dev *pctldev,
					    struct device_node *np,
					    struct pinctrl_map **map,
					    unsigned int *num_maps)
{
	struct ma35_pinctrl *npctl = pinctrl_dev_get_drvdata(pctldev);
	struct ma35_pin_setting *setting;
	struct pinctrl_map *new_map;
	struct device_node *parent;
	struct group_desc *grp;
	int map_num = 1;
	int i;

	/*
	 * first find the group of this node and check if we need create
	 * config maps for pins
	 */
	grp = ma35_pinctrl_find_group_by_name(npctl, np->name);
	if (!grp) {
		dev_err(npctl->dev, "unable to find group for node %s\n", np->name);
		return -EINVAL;
	}

	map_num += grp->grp.npins;
	new_map = kcalloc(map_num, sizeof(*new_map), GFP_KERNEL);
	if (!new_map)
		return -ENOMEM;

	*map = new_map;
	*num_maps = map_num;
	/* create mux map */
	parent = of_get_parent(np);
	if (!parent)
		return -EINVAL;

	setting = grp->data;

	new_map[0].type = PIN_MAP_TYPE_MUX_GROUP;
	new_map[0].data.mux.function = parent->name;
	new_map[0].data.mux.group = np->name;
	of_node_put(parent);

	new_map++;
	for (i = 0; i < grp->grp.npins; i++) {
		new_map[i].type = PIN_MAP_TYPE_CONFIGS_PIN;
		new_map[i].data.configs.group_or_pin = pin_get_name(pctldev, grp->grp.pins[i]);
		new_map[i].data.configs.configs = setting[i].configs;
		new_map[i].data.configs.num_configs = setting[i].nconfigs;
	}
	dev_dbg(pctldev->dev, "maps: function %s group %s num %d\n",
		(*map)->data.mux.function, (*map)->data.mux.group, map_num);

	return 0;
}

static const struct pinctrl_ops ma35_pctrl_ops = {
	.get_groups_count = ma35_get_groups_count,
	.get_group_name = ma35_get_group_name,
	.get_group_pins = ma35_get_group_pins,
	.dt_node_to_map = ma35_pinctrl_dt_node_to_map_func,
	.dt_free_map = pinconf_generic_dt_free_map,
};

static int ma35_pinmux_get_func_count(struct pinctrl_dev *pctldev)
{
	struct ma35_pinctrl *npctl = pinctrl_dev_get_drvdata(pctldev);

	return npctl->nfunctions;
}

static const char *ma35_pinmux_get_func_name(struct pinctrl_dev *pctldev,
					     unsigned int selector)
{
	struct ma35_pinctrl *npctl = pinctrl_dev_get_drvdata(pctldev);

	return npctl->functions[selector].name;
}

static int ma35_pinmux_get_func_groups(struct pinctrl_dev *pctldev,
				       unsigned int function,
				       const char *const **groups,
				       unsigned int *const num_groups)
{
	struct ma35_pinctrl *npctl = pinctrl_dev_get_drvdata(pctldev);

	*groups = npctl->functions[function].groups;
	*num_groups = npctl->functions[function].ngroups;

	return 0;
}

static int ma35_pinmux_set_mux(struct pinctrl_dev *pctldev, unsigned int selector,
			       unsigned int group)
{
	struct ma35_pinctrl *npctl = pinctrl_dev_get_drvdata(pctldev);
	struct group_desc *grp = &npctl->groups[group];
	struct ma35_pin_setting *setting = grp->data;
	u32 i, regval;

	dev_dbg(npctl->dev, "enable function %s group %s\n",
		npctl->functions[selector].name, grp->grp.name);

	for (i = 0; i < grp->grp.npins; i++) {
		regmap_read(npctl->regmap, setting->offset, &regval);
		regval &= ~GENMASK(setting->shift + MA35_MFP_BITS_PER_PORT - 1,
				   setting->shift);
		regval |= setting->muxval << setting->shift;
		regmap_write(npctl->regmap, setting->offset, regval);
		setting++;
	}
	return 0;
}

static const struct pinmux_ops ma35_pmx_ops = {
	.get_functions_count = ma35_pinmux_get_func_count,
	.get_function_name = ma35_pinmux_get_func_name,
	.get_function_groups = ma35_pinmux_get_func_groups,
	.set_mux = ma35_pinmux_set_mux,
	.strict = true,
};

static void ma35_gpio_set_mode(void __iomem *reg_mode, unsigned int gpio, u32 mode)
{
	u32 regval = readl(reg_mode);

	regval &= ~MA35_GP_MODE_MASK(gpio);
	regval |= field_prep(MA35_GP_MODE_MASK(gpio), mode);

	writel(regval, reg_mode);
}

static u32 ma35_gpio_get_mode(void __iomem *reg_mode, unsigned int gpio)
{
	u32 regval = readl(reg_mode);

	return field_get(MA35_GP_MODE_MASK(gpio), regval);
}

static int ma35_gpio_core_direction_in(struct gpio_chip *gc, unsigned int gpio)
{
	struct ma35_pin_bank *bank = gpiochip_get_data(gc);
	void __iomem *reg_mode = bank->reg_base + MA35_GP_REG_MODE;

	guard(raw_spinlock_irqsave)(&ma35_lock);

	ma35_gpio_set_mode(reg_mode, gpio, MA35_GP_MODE_INPUT);

	return 0;
}

static int ma35_gpio_core_direction_out(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct ma35_pin_bank *bank = gpiochip_get_data(gc);
	void __iomem *reg_dout = bank->reg_base + MA35_GP_REG_DOUT;
	void __iomem *reg_mode = bank->reg_base + MA35_GP_REG_MODE;
	unsigned int regval;

	guard(raw_spinlock_irqsave)(&ma35_lock);

	regval = readl(reg_dout);
	if (val)
		regval |= BIT(gpio);
	else
		regval &= ~BIT(gpio);
	writel(regval, reg_dout);

	ma35_gpio_set_mode(reg_mode, gpio, MA35_GP_MODE_OUTPUT);

	return 0;
}

static int ma35_gpio_core_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct ma35_pin_bank *bank = gpiochip_get_data(gc);
	void __iomem *reg_pin = bank->reg_base + MA35_GP_REG_PIN;

	return !!(readl(reg_pin) & BIT(gpio));
}

static int ma35_gpio_core_set(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct ma35_pin_bank *bank = gpiochip_get_data(gc);
	void __iomem *reg_dout = bank->reg_base + MA35_GP_REG_DOUT;
	u32 regval;

	if (val)
		regval = readl(reg_dout) | BIT(gpio);
	else
		regval = readl(reg_dout) & ~BIT(gpio);

	writel(regval, reg_dout);

	return 0;
}

static int ma35_gpio_core_to_request(struct gpio_chip *gc, unsigned int gpio)
{
	struct ma35_pin_bank *bank = gpiochip_get_data(gc);
	u32 reg_offs, bit_offs, regval;

	if (gpio < 8) {
		/* The MFP low register controls port 0 ~ 7 */
		reg_offs = bank->bank_num * MA35_MFP_REG_SZ_PER_BANK;
		bit_offs = gpio * MA35_MFP_BITS_PER_PORT;
	} else {
		/* The MFP high register controls port 8 ~ 15 */
		reg_offs = bank->bank_num * MA35_MFP_REG_SZ_PER_BANK + 4;
		bit_offs = (gpio - 8) * MA35_MFP_BITS_PER_PORT;
	}

	regmap_read(bank->regmap, MA35_MFP_REG_BASE + reg_offs, &regval);
	regval &= ~GENMASK(bit_offs + MA35_MFP_BITS_PER_PORT - 1, bit_offs);
	regmap_write(bank->regmap, MA35_MFP_REG_BASE + reg_offs, regval);

	return 0;
}

static void ma35_irq_gpio_ack(struct irq_data *d)
{
	struct ma35_pin_bank *bank = gpiochip_get_data(irq_data_get_irq_chip_data(d));
	void __iomem *reg_intsrc = bank->reg_base + MA35_GP_REG_INTSRC;
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	writel(BIT(hwirq), reg_intsrc);
}

static void ma35_irq_gpio_mask(struct irq_data *d)
{
	struct ma35_pin_bank *bank = gpiochip_get_data(irq_data_get_irq_chip_data(d));
	void __iomem *reg_ien = bank->reg_base + MA35_GP_REG_INTEN;
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	u32 regval;

	regval = readl(reg_ien);

	regval &= ~MA35_GP_INTEN_BOTH(hwirq);

	writel(regval, reg_ien);
}

static void ma35_irq_gpio_unmask(struct irq_data *d)
{
	struct ma35_pin_bank *bank = gpiochip_get_data(irq_data_get_irq_chip_data(d));
	void __iomem *reg_itype = bank->reg_base + MA35_GP_REG_INTTYPE;
	void __iomem *reg_ien = bank->reg_base + MA35_GP_REG_INTEN;
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	u32 bval, regval;

	bval = bank->irqtype & BIT(hwirq);
	regval = readl(reg_itype);
	regval &= ~BIT(hwirq);
	writel(regval | bval, reg_itype);

	bval = bank->irqinten & MA35_GP_INTEN_BOTH(hwirq);
	regval = readl(reg_ien);
	regval &= ~MA35_GP_INTEN_BOTH(hwirq);
	writel(regval | bval, reg_ien);
}

static int ma35_irq_irqtype(struct irq_data *d, unsigned int type)
{
	struct ma35_pin_bank *bank = gpiochip_get_data(irq_data_get_irq_chip_data(d));
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	switch (type) {
	case IRQ_TYPE_EDGE_BOTH:
		irq_set_handler_locked(d, handle_edge_irq);
		bank->irqtype &= ~BIT(hwirq);
		bank->irqinten |= MA35_GP_INTEN_BOTH(hwirq);
		break;
	case IRQ_TYPE_EDGE_RISING:
	case IRQ_TYPE_LEVEL_HIGH:
		irq_set_handler_locked(d, handle_edge_irq);
		bank->irqtype &= ~BIT(hwirq);
		bank->irqinten |= MA35_GP_INTEN_H(hwirq);
		bank->irqinten &= ~MA35_GP_INTEN_L(hwirq);
		break;
	case IRQ_TYPE_EDGE_FALLING:
	case IRQ_TYPE_LEVEL_LOW:
		irq_set_handler_locked(d, handle_edge_irq);
		bank->irqtype &= ~BIT(hwirq);
		bank->irqinten |= MA35_GP_INTEN_L(hwirq);
		bank->irqinten &= ~MA35_GP_INTEN_H(hwirq);
		break;
	default:
		return -EINVAL;
	}

	writel(bank->irqtype, bank->reg_base + MA35_GP_REG_INTTYPE);
	writel(bank->irqinten, bank->reg_base + MA35_GP_REG_INTEN);

	return 0;
}

static struct irq_chip ma35_gpio_irqchip = {
	.name = "MA35-GPIO-IRQ",
	.irq_disable = ma35_irq_gpio_mask,
	.irq_enable = ma35_irq_gpio_unmask,
	.irq_ack = ma35_irq_gpio_ack,
	.irq_mask = ma35_irq_gpio_mask,
	.irq_unmask = ma35_irq_gpio_unmask,
	.irq_set_type = ma35_irq_irqtype,
	.flags = IRQCHIP_MASK_ON_SUSPEND | IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static void ma35_irq_demux_intgroup(struct irq_desc *desc)
{
	struct ma35_pin_bank *bank = gpiochip_get_data(irq_desc_get_handler_data(desc));
	struct irq_domain *irqdomain = bank->chip.irq.domain;
	struct irq_chip *irqchip = irq_desc_get_chip(desc);
	unsigned long isr;
	int offset;

	chained_irq_enter(irqchip, desc);

	isr = readl(bank->reg_base + MA35_GP_REG_INTSRC);

	for_each_set_bit(offset, &isr, bank->nr_pins)
		generic_handle_irq(irq_find_mapping(irqdomain, offset));

	chained_irq_exit(irqchip, desc);
}

static int ma35_gpiolib_register(struct platform_device *pdev, struct ma35_pinctrl *npctl)
{
	struct ma35_pin_ctrl *ctrl = npctl->ctrl;
	struct ma35_pin_bank *bank = ctrl->pin_banks;
	int ret;
	int i;

	for (i = 0; i < ctrl->nr_banks; i++, bank++) {
		if (!bank->valid) {
			dev_warn(&pdev->dev, "%pfw: bank is not valid\n", bank->fwnode);
			continue;
		}
		bank->irqtype = 0;
		bank->irqinten = 0;
		bank->chip.label = bank->name;
		bank->chip.parent = &pdev->dev;
		bank->chip.request = ma35_gpio_core_to_request;
		bank->chip.direction_input = ma35_gpio_core_direction_in;
		bank->chip.direction_output = ma35_gpio_core_direction_out;
		bank->chip.get = ma35_gpio_core_get;
		bank->chip.set = ma35_gpio_core_set;
		bank->chip.base = -1;
		bank->chip.ngpio = bank->nr_pins;
		bank->chip.can_sleep = false;

		if (bank->irq > 0) {
			struct gpio_irq_chip *girq;

			girq = &bank->chip.irq;
			gpio_irq_chip_set_chip(girq, &ma35_gpio_irqchip);
			girq->parent_handler = ma35_irq_demux_intgroup;
			girq->num_parents = 1;

			girq->parents = devm_kcalloc(&pdev->dev, girq->num_parents,
						     sizeof(*girq->parents), GFP_KERNEL);
			if (!girq->parents)
				return -ENOMEM;

			girq->parents[0] = bank->irq;
			girq->default_type = IRQ_TYPE_NONE;
			girq->handler = handle_bad_irq;
		}

		ret = devm_gpiochip_add_data(&pdev->dev, &bank->chip, bank);
		if (ret) {
			dev_err(&pdev->dev, "failed to register gpio_chip %s, error code: %d\n",
				bank->chip.label, ret);
			return ret;
		}
	}
	return 0;
}

static int ma35_get_bank_data(struct ma35_pin_bank *bank)
{
	bank->reg_base = fwnode_iomap(bank->fwnode, 0);
	if (!bank->reg_base)
		return -ENOMEM;

	bank->irq = fwnode_irq_get(bank->fwnode, 0);

	bank->nr_pins = MA35_GPIO_PORT_MAX;

	bank->clk = of_clk_get(to_of_node(bank->fwnode), 0);
	if (IS_ERR(bank->clk))
		return PTR_ERR(bank->clk);

	return clk_prepare_enable(bank->clk);
}

static int ma35_pinctrl_get_soc_data(struct ma35_pinctrl *pctl, struct platform_device *pdev)
{
	struct fwnode_handle *child;
	struct ma35_pin_ctrl *ctrl;
	struct ma35_pin_bank *bank;
	int i, id = 0;

	ctrl = pctl->ctrl;
	ctrl->nr_banks = MA35_GPIO_BANK_MAX;

	ctrl->pin_banks = devm_kcalloc(&pdev->dev, ctrl->nr_banks,
				       sizeof(*ctrl->pin_banks), GFP_KERNEL);
	if (!ctrl->pin_banks)
		return -ENOMEM;

	for (i = 0; i < ctrl->nr_banks; i++) {
		ctrl->pin_banks[i].bank_num = i;
		ctrl->pin_banks[i].name = gpio_group_name[i];
	}

	for_each_gpiochip_node(&pdev->dev, child) {
		bank = &ctrl->pin_banks[id];
		bank->fwnode = child;
		bank->regmap = pctl->regmap;
		bank->dev = &pdev->dev;
		if (!ma35_get_bank_data(bank))
			bank->valid = true;
		id++;
	}
	return 0;
}

static void ma35_gpio_cla_port(unsigned int gpio_num, unsigned int *group,
			       unsigned int *num)
{
	*group = gpio_num / MA35_GPIO_PORT_MAX;
	*num = gpio_num % MA35_GPIO_PORT_MAX;
}

static int ma35_pinconf_set_pull(struct ma35_pinctrl *npctl, unsigned int pin,
				 int pull_up)
{
	unsigned int port, group_num;
	void __iomem *base;
	u32 regval, pull_sel = MA35_GP_PUSEL_DISABLE;

	ma35_gpio_cla_port(pin, &group_num, &port);
	base = npctl->ctrl->pin_banks[group_num].reg_base;

	regval = readl(base + MA35_GP_REG_PUSEL);
	regval &= ~MA35_GP_PUSEL_MASK(port);

	switch (pull_up) {
	case PIN_CONFIG_BIAS_PULL_UP:
		pull_sel = MA35_GP_PUSEL_PULL_UP;
		break;

	case PIN_CONFIG_BIAS_PULL_DOWN:
		pull_sel = MA35_GP_PUSEL_PULL_DOWN;
		break;

	case PIN_CONFIG_BIAS_DISABLE:
		pull_sel = MA35_GP_PUSEL_DISABLE;
		break;
	}

	regval |= field_prep(MA35_GP_PUSEL_MASK(port), pull_sel);
	writel(regval, base + MA35_GP_REG_PUSEL);

	return 0;
}

static int ma35_pinconf_get_output(struct ma35_pinctrl *npctl, unsigned int pin)
{
	unsigned int port, group_num;
	void __iomem *base;
	u32 mode;

	ma35_gpio_cla_port(pin, &group_num, &port);
	base = npctl->ctrl->pin_banks[group_num].reg_base;

	mode = ma35_gpio_get_mode(base + MA35_GP_REG_MODE, port);
	if (mode == MA35_GP_MODE_OUTPUT)
		return 1;

	return 0;
}

static int ma35_pinconf_get_pull(struct ma35_pinctrl *npctl, unsigned int pin)
{
	unsigned int port, group_num;
	void __iomem *base;
	u32 regval, pull_sel;

	ma35_gpio_cla_port(pin, &group_num, &port);
	base = npctl->ctrl->pin_banks[group_num].reg_base;

	regval = readl(base + MA35_GP_REG_PUSEL);

	pull_sel = field_get(MA35_GP_PUSEL_MASK(port), regval);

	switch (pull_sel) {
	case MA35_GP_PUSEL_PULL_UP:
		return PIN_CONFIG_BIAS_PULL_UP;

	case MA35_GP_PUSEL_PULL_DOWN:
		return PIN_CONFIG_BIAS_PULL_DOWN;

	case MA35_GP_PUSEL_DISABLE:
		return PIN_CONFIG_BIAS_DISABLE;
	}

	return PIN_CONFIG_BIAS_DISABLE;
}

static int ma35_pinconf_set_output(struct ma35_pinctrl *npctl, unsigned int pin, bool out)
{
	unsigned int port, group_num;
	void __iomem *base;

	ma35_gpio_cla_port(pin, &group_num, &port);
	base = npctl->ctrl->pin_banks[group_num].reg_base;

	ma35_gpio_set_mode(base + MA35_GP_REG_MODE, port, MA35_GP_MODE_OUTPUT);

	return 0;
}

static int ma35_pinconf_get_power_source(struct ma35_pinctrl *npctl, unsigned int pin)
{
	unsigned int port, group_num;
	void __iomem *base;
	u32 regval;

	ma35_gpio_cla_port(pin, &group_num, &port);
	base = npctl->ctrl->pin_banks[group_num].reg_base;

	regval = readl(base + MA35_GP_REG_SPW);

	if (regval & BIT(port))
		return MVOLT_3300;
	else
		return MVOLT_1800;
}

static int ma35_pinconf_set_power_source(struct ma35_pinctrl *npctl,
					 unsigned int pin, int arg)
{
	unsigned int port, group_num;
	void __iomem *base;
	u32 regval;

	if ((arg != MVOLT_1800) && (arg != MVOLT_3300))
		return -EINVAL;

	ma35_gpio_cla_port(pin, &group_num, &port);
	base = npctl->ctrl->pin_banks[group_num].reg_base;

	regval = readl(base + MA35_GP_REG_SPW);

	if (arg == MVOLT_1800)
		regval &= ~BIT(port);
	else
		regval |= BIT(port);

	writel(regval, base + MA35_GP_REG_SPW);

	return 0;
}

static int ma35_pinconf_get_drive_strength(struct ma35_pinctrl *npctl, unsigned int pin,
					   u32 *strength)
{
	unsigned int port, group_num;
	void __iomem *base;
	u32 regval, ds_val;

	ma35_gpio_cla_port(pin, &group_num, &port);
	base = npctl->ctrl->pin_banks[group_num].reg_base;

	regval = readl(base + MA35_GP_DS_REG(port));
	ds_val = field_get(MA35_GP_DS_MASK(port), regval);

	if (ma35_pinconf_get_power_source(npctl, pin) == MVOLT_1800)
		*strength = ds_1800mv_tbl[ds_val];
	else
		*strength = ds_3300mv_tbl[ds_val];

	return 0;
}

static int ma35_pinconf_set_drive_strength(struct ma35_pinctrl *npctl, unsigned int pin,
					   int strength)
{
	unsigned int port, group_num;
	void __iomem *base;
	int i, ds_val = -1;
	u32 regval;

	if (ma35_pinconf_get_power_source(npctl, pin) == MVOLT_1800) {
		for (i = 0; i < ARRAY_SIZE(ds_1800mv_tbl); i++) {
			if (ds_1800mv_tbl[i] == strength) {
				ds_val = i;
				break;
			}
		}
	} else {
		for (i = 0; i < ARRAY_SIZE(ds_3300mv_tbl); i++) {
			if (ds_3300mv_tbl[i] == strength) {
				ds_val = i;
				break;
			}
		}
	}
	if (ds_val == -1)
		return -EINVAL;

	ma35_gpio_cla_port(pin, &group_num, &port);
	base = npctl->ctrl->pin_banks[group_num].reg_base;

	regval = readl(base + MA35_GP_DS_REG(port));
	regval &= ~MA35_GP_DS_MASK(port);
	regval |= field_prep(MA35_GP_DS_MASK(port), ds_val);

	writel(regval, base + MA35_GP_DS_REG(port));

	return 0;
}

static int ma35_pinconf_get_schmitt_enable(struct ma35_pinctrl *npctl, unsigned int pin)
{
	unsigned int port, group_num;
	void __iomem *base;
	u32 regval;

	ma35_gpio_cla_port(pin, &group_num, &port);
	base = npctl->ctrl->pin_banks[group_num].reg_base;

	regval = readl(base + MA35_GP_REG_SMTEN);

	return !!(regval & BIT(port));
}

static int ma35_pinconf_set_schmitt(struct ma35_pinctrl *npctl, unsigned int pin, int enable)
{
	unsigned int port, group_num;
	void __iomem *base;
	u32 regval;

	ma35_gpio_cla_port(pin, &group_num, &port);
	base = npctl->ctrl->pin_banks[group_num].reg_base;

	regval = readl(base + MA35_GP_REG_SMTEN);

	if (enable)
		regval |= BIT(port);
	else
		regval &= ~BIT(port);

	writel(regval, base + MA35_GP_REG_SMTEN);

	return 0;
}

static int ma35_pinconf_get_slew_rate(struct ma35_pinctrl *npctl, unsigned int pin)
{
	unsigned int port, group_num;
	void __iomem *base;
	u32 regval;

	ma35_gpio_cla_port(pin, &group_num, &port);
	base = npctl->ctrl->pin_banks[group_num].reg_base;

	regval = readl(base + MA35_GP_REG_SLEWCTL);

	return field_get(MA35_GP_SLEWCTL_MASK(port), regval);
}

static int ma35_pinconf_set_slew_rate(struct ma35_pinctrl *npctl, unsigned int pin, int rate)
{
	unsigned int port, group_num;
	void __iomem *base;
	u32 regval;

	ma35_gpio_cla_port(pin, &group_num, &port);
	base = npctl->ctrl->pin_banks[group_num].reg_base;

	regval = readl(base + MA35_GP_REG_SLEWCTL);
	regval &= ~MA35_GP_SLEWCTL_MASK(port);
	regval |= field_prep(MA35_GP_SLEWCTL_MASK(port), rate);

	writel(regval, base + MA35_GP_REG_SLEWCTL);

	return 0;
}

static int ma35_pinconf_get(struct pinctrl_dev *pctldev, unsigned int pin, unsigned long *config)
{
	struct ma35_pinctrl *npctl = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	u32 arg;
	int ret;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_DOWN:
	case PIN_CONFIG_BIAS_PULL_UP:
		if (ma35_pinconf_get_pull(npctl, pin) != param)
			return -EINVAL;
		arg = 1;
		break;

	case PIN_CONFIG_DRIVE_STRENGTH:
		ret = ma35_pinconf_get_drive_strength(npctl, pin, &arg);
		if (ret)
			return ret;
		break;

	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		arg = ma35_pinconf_get_schmitt_enable(npctl, pin);
		break;

	case PIN_CONFIG_SLEW_RATE:
		arg = ma35_pinconf_get_slew_rate(npctl, pin);
		break;

	case PIN_CONFIG_OUTPUT_ENABLE:
		arg = ma35_pinconf_get_output(npctl, pin);
		break;

	case PIN_CONFIG_POWER_SOURCE:
		arg = ma35_pinconf_get_power_source(npctl, pin);
		break;

	default:
		return -EINVAL;
	}
	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int ma35_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
			    unsigned long *configs, unsigned int num_configs)
{
	struct ma35_pinctrl *npctl = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param;
	unsigned int arg = 0;
	int i, ret = 0;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
		case PIN_CONFIG_BIAS_PULL_UP:
		case PIN_CONFIG_BIAS_PULL_DOWN:
			ret = ma35_pinconf_set_pull(npctl, pin, param);
			break;

		case PIN_CONFIG_DRIVE_STRENGTH:
			ret = ma35_pinconf_set_drive_strength(npctl, pin, arg);
			break;

		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			ret = ma35_pinconf_set_schmitt(npctl, pin, 1);
			break;

		case PIN_CONFIG_INPUT_SCHMITT:
			ret = ma35_pinconf_set_schmitt(npctl, pin, arg);
			break;

		case PIN_CONFIG_SLEW_RATE:
			ret = ma35_pinconf_set_slew_rate(npctl, pin, arg);
			break;

		case PIN_CONFIG_OUTPUT_ENABLE:
			ret = ma35_pinconf_set_output(npctl, pin, arg);
			break;

		case PIN_CONFIG_POWER_SOURCE:
			ret = ma35_pinconf_set_power_source(npctl, pin, arg);
			break;

		default:
			return -EINVAL;
		}

		if (ret)
			break;
	}
	return ret;
}

static const struct pinconf_ops ma35_pinconf_ops = {
	.pin_config_get = ma35_pinconf_get,
	.pin_config_set = ma35_pinconf_set,
	.is_generic = true,
};

static int ma35_pinctrl_parse_groups(struct fwnode_handle *fwnode, struct group_desc *grp,
				     struct ma35_pinctrl *npctl, u32 index)
{
	struct device_node *np = to_of_node(fwnode);
	struct ma35_pin_setting *pin;
	unsigned long *configs;
	unsigned int nconfigs;
	unsigned int *pins;
	int i, j, count, ret;
	u32 *elems;

	ret = pinconf_generic_parse_dt_config(np, NULL, &configs, &nconfigs);
	if (ret)
		return ret;

	count = fwnode_property_count_u32(fwnode, "nuvoton,pins");
	if (!count || count % 3)
		return -EINVAL;

	elems = devm_kmalloc_array(npctl->dev, count, sizeof(u32), GFP_KERNEL);
	if (!elems)
		return -ENOMEM;

	grp->grp.name = np->name;

	ret = fwnode_property_read_u32_array(fwnode, "nuvoton,pins", elems, count);
	if (ret)
		return -EINVAL;
	grp->grp.npins = count / 3;

	pins = devm_kcalloc(npctl->dev, grp->grp.npins, sizeof(*pins), GFP_KERNEL);
	if (!pins)
		return -ENOMEM;
	grp->grp.pins = pins;

	pin = devm_kcalloc(npctl->dev, grp->grp.npins, sizeof(*pin), GFP_KERNEL);
	if (!pin)
		return -ENOMEM;
	grp->data = pin;

	for (i = 0, j = 0; i < count; i += 3, j++) {
		pin->offset = elems[i] * MA35_MFP_REG_SZ_PER_BANK + MA35_MFP_REG_BASE;
		pin->shift = (elems[i + 1] * MA35_MFP_BITS_PER_PORT) % 32;
		pin->muxval = elems[i + 2];
		pin->configs = configs;
		pin->nconfigs = nconfigs;
		pins[j] = npctl->info->get_pin_num(pin->offset, pin->shift);
		pin++;
	}
	return 0;
}

static int ma35_pinctrl_parse_functions(struct fwnode_handle *fwnode, struct ma35_pinctrl *npctl,
					u32 index)
{
	struct device_node *np = to_of_node(fwnode);
	struct fwnode_handle *child;
	struct pinfunction *func;
	struct group_desc *grp;
	static u32 grp_index;
	const char **groups;
	u32 i = 0;
	int ret;

	dev_dbg(npctl->dev, "parse function(%d): %s\n", index, np->name);

	func = &npctl->functions[index];
	func->name = np->name;
	func->ngroups = of_get_child_count(np);

	if (func->ngroups <= 0)
		return 0;

	groups = devm_kcalloc(npctl->dev, func->ngroups, sizeof(*groups), GFP_KERNEL);
	if (!groups)
		return -ENOMEM;

	fwnode_for_each_child_node(fwnode, child) {
		struct device_node *node = to_of_node(child);

		groups[i] = node->name;
		grp = &npctl->groups[grp_index++];
		ret = ma35_pinctrl_parse_groups(child, grp, npctl, i++);
		if (ret) {
			fwnode_handle_put(child);
			return ret;
		}
	}

	func->groups = groups;
	return 0;
}

static int ma35_pinctrl_probe_dt(struct platform_device *pdev, struct ma35_pinctrl *npctl)
{
	struct device *dev = &pdev->dev;
	struct fwnode_handle *child;
	u32 idx = 0;
	int ret;

	device_for_each_child_node(dev, child) {
		if (fwnode_property_present(child, "gpio-controller"))
			continue;

		npctl->nfunctions++;
		npctl->ngroups += of_get_child_count(to_of_node(child));
	}

	if (!npctl->nfunctions)
		return -EINVAL;

	npctl->functions = devm_kcalloc(&pdev->dev, npctl->nfunctions,
					sizeof(*npctl->functions), GFP_KERNEL);
	if (!npctl->functions)
		return -ENOMEM;

	npctl->groups = devm_kcalloc(&pdev->dev, npctl->ngroups,
				     sizeof(*npctl->groups), GFP_KERNEL);
	if (!npctl->groups)
		return -ENOMEM;

	device_for_each_child_node(dev, child) {
		if (fwnode_property_present(child, "gpio-controller"))
			continue;

		ret = ma35_pinctrl_parse_functions(child, npctl, idx++);
		if (ret) {
			fwnode_handle_put(child);
			dev_err(&pdev->dev, "failed to parse function\n");
			return ret;
		}
	}
	return 0;
}

int ma35_pinctrl_probe(struct platform_device *pdev, const struct ma35_pinctrl_soc_info *info)
{
	struct pinctrl_desc *ma35_pinctrl_desc;
	struct device *dev = &pdev->dev;
	struct ma35_pinctrl *npctl;
	int ret;

	if (!info || !info->pins || !info->npins) {
		dev_err(&pdev->dev, "wrong pinctrl info\n");
		return -EINVAL;
	}

	npctl = devm_kzalloc(&pdev->dev, sizeof(*npctl), GFP_KERNEL);
	if (!npctl)
		return -ENOMEM;

	ma35_pinctrl_desc = devm_kzalloc(&pdev->dev, sizeof(*ma35_pinctrl_desc), GFP_KERNEL);
	if (!ma35_pinctrl_desc)
		return -ENOMEM;

	npctl->ctrl = devm_kzalloc(&pdev->dev, sizeof(*npctl->ctrl), GFP_KERNEL);
	if (!npctl->ctrl)
		return -ENOMEM;

	ma35_pinctrl_desc->name = dev_name(&pdev->dev);
	ma35_pinctrl_desc->pins = info->pins;
	ma35_pinctrl_desc->npins = info->npins;
	ma35_pinctrl_desc->pctlops = &ma35_pctrl_ops;
	ma35_pinctrl_desc->pmxops = &ma35_pmx_ops;
	ma35_pinctrl_desc->confops = &ma35_pinconf_ops;
	ma35_pinctrl_desc->owner = THIS_MODULE;

	npctl->info = info;
	npctl->dev = &pdev->dev;

	npctl->regmap = syscon_regmap_lookup_by_phandle(dev_of_node(dev), "nuvoton,sys");
	if (IS_ERR(npctl->regmap))
		return dev_err_probe(&pdev->dev, PTR_ERR(npctl->regmap),
				     "No syscfg phandle specified\n");

	ret = ma35_pinctrl_get_soc_data(npctl, pdev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "fail to get soc data\n");

	platform_set_drvdata(pdev, npctl);

	ret = ma35_pinctrl_probe_dt(pdev, npctl);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "fail to probe MA35 pinctrl dt\n");

	ret = devm_pinctrl_register_and_init(dev, ma35_pinctrl_desc, npctl, &npctl->pctl);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "fail to register MA35 pinctrl\n");

	ret = pinctrl_enable(npctl->pctl);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "fail to enable MA35 pinctrl\n");

	return ma35_gpiolib_register(pdev, npctl);
}

int ma35_pinctrl_suspend(struct device *dev)
{
	struct ma35_pinctrl *npctl = dev_get_drvdata(dev);

	return pinctrl_force_sleep(npctl->pctl);
}

int ma35_pinctrl_resume(struct device *dev)
{
	struct ma35_pinctrl *npctl = dev_get_drvdata(dev);

	return pinctrl_force_default(npctl->pctl);
}
