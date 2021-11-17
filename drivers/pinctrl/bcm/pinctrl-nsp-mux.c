/* Copyright (C) 2015 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This file contains the Northstar plus (NSP) IOMUX driver that supports
 * group based PINMUX configuration. The Northstar plus IOMUX controller
 * allows pins to be individually muxed to GPIO function. The NAND and MMC is
 * a group based selection. The gpio_a 8 - 11 are muxed with gpio_b and pwm.
 * To select PWM, one need to enable the corresponding gpio_b as well.
 *
 *				gpio_a (8 - 11)
 *				+----------
 *				|
 *		gpio_a (8-11)	|	gpio_b (0 - 3)
 *	------------------------+-------+----------
 *					|
 *					|	pwm (0 - 3)
 *					+----------
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "../core.h"
#include "../pinctrl-utils.h"

#define NSP_MUX_BASE0	0x00
#define NSP_MUX_BASE1	0x01
#define NSP_MUX_BASE2	0x02
/*
 * nsp IOMUX register description
 *
 * @base: base 0 or base 1
 * @shift: bit shift for mux configuration of a group
 * @mask: bit mask of the function
 * @alt: alternate function to set to
 */
struct nsp_mux {
	unsigned int base;
	unsigned int shift;
	unsigned int mask;
	unsigned int alt;
};

/*
 * Keep track of nsp IOMUX configuration and prevent double configuration
 *
 * @nsp_mux: nsp IOMUX register description
 * @is_configured: flag to indicate whether a mux setting has already been
 * configured
 */
struct nsp_mux_log {
	struct nsp_mux mux;
	bool is_configured;
};

/*
 * Group based IOMUX configuration
 *
 * @name: name of the group
 * @pins: array of pins used by this group
 * @num_pins: total number of pins used by this group
 * @mux: nsp group based IOMUX configuration
 */
struct nsp_pin_group {
	const char *name;
	const unsigned int *pins;
	const unsigned int num_pins;
	const struct nsp_mux mux;
};

/*
 * nsp mux function and supported pin groups
 *
 * @name: name of the function
 * @groups: array of groups that can be supported by this function
 * @num_groups: total number of groups that can be supported by this function
 */
struct nsp_pin_function {
	const char *name;
	const char * const *groups;
	const unsigned int num_groups;
};

/*
 * nsp IOMUX pinctrl core
 *
 * @pctl: pointer to pinctrl_dev
 * @dev: pointer to device
 * @base0: first mux register
 * @base1: second mux register
 * @base2: third mux register
 * @groups: pointer to array of groups
 * @num_groups: total number of groups
 * @functions: pointer to array of functions
 * @num_functions: total number of functions
 * @mux_log: pointer to the array of mux logs
 * @lock: lock to protect register access
 */
struct nsp_pinctrl {
	struct pinctrl_dev *pctl;
	struct device *dev;
	void __iomem *base0;
	void __iomem *base1;
	void __iomem *base2;
	const struct nsp_pin_group *groups;
	unsigned int num_groups;
	const struct nsp_pin_function *functions;
	unsigned int num_functions;
	struct nsp_mux_log *mux_log;
	spinlock_t lock;
};

/*
 * Description of a pin in nsp
 *
 * @pin: pin number
 * @name: pin name
 * @gpio_select: reg data to select GPIO
 */
struct nsp_pin {
	unsigned int pin;
	char *name;
	unsigned int gpio_select;
};

#define NSP_PIN_DESC(p, n, g)		\
{					\
	.pin = p,			\
	.name = n,			\
	.gpio_select = g,		\
}

/*
 * List of muxable pins in nsp
 */
static struct nsp_pin nsp_pins[] = {
	NSP_PIN_DESC(0, "spi_clk", 1),
	NSP_PIN_DESC(1, "spi_ss", 1),
	NSP_PIN_DESC(2, "spi_mosi", 1),
	NSP_PIN_DESC(3, "spi_miso", 1),
	NSP_PIN_DESC(4, "scl", 1),
	NSP_PIN_DESC(5, "sda", 1),
	NSP_PIN_DESC(6, "mdc", 1),
	NSP_PIN_DESC(7, "mdio", 1),
	NSP_PIN_DESC(8, "pwm0", 1),
	NSP_PIN_DESC(9, "pwm1", 1),
	NSP_PIN_DESC(10, "pwm2", 1),
	NSP_PIN_DESC(11, "pwm3", 1),
	NSP_PIN_DESC(12, "uart1_rx", 1),
	NSP_PIN_DESC(13, "uart1_tx", 1),
	NSP_PIN_DESC(14, "uart1_cts", 1),
	NSP_PIN_DESC(15, "uart1_rts", 1),
	NSP_PIN_DESC(16, "uart2_rx", 1),
	NSP_PIN_DESC(17, "uart2_tx", 1),
	NSP_PIN_DESC(18, "synce", 0),
	NSP_PIN_DESC(19, "sata0_led", 0),
	NSP_PIN_DESC(20, "sata1_led", 0),
	NSP_PIN_DESC(21, "xtal_out", 1),
	NSP_PIN_DESC(22, "sdio_pwr", 1),
	NSP_PIN_DESC(23, "sdio_en_1p8v", 1),
	NSP_PIN_DESC(24, "gpio_24", 1),
	NSP_PIN_DESC(25, "gpio_25", 1),
	NSP_PIN_DESC(26, "p5_led0", 0),
	NSP_PIN_DESC(27, "p5_led1", 0),
	NSP_PIN_DESC(28, "gpio_28", 1),
	NSP_PIN_DESC(29, "gpio_29", 1),
	NSP_PIN_DESC(30, "gpio_30", 1),
	NSP_PIN_DESC(31, "gpio_31", 1),
	NSP_PIN_DESC(32, "nand_ale", 0),
	NSP_PIN_DESC(33, "nand_ce0", 0),
	NSP_PIN_DESC(34, "nand_r/b", 0),
	NSP_PIN_DESC(35, "nand_dq0", 0),
	NSP_PIN_DESC(36, "nand_dq1", 0),
	NSP_PIN_DESC(37, "nand_dq2", 0),
	NSP_PIN_DESC(38, "nand_dq3", 0),
	NSP_PIN_DESC(39, "nand_dq4", 0),
	NSP_PIN_DESC(40, "nand_dq5", 0),
	NSP_PIN_DESC(41, "nand_dq6", 0),
	NSP_PIN_DESC(42, "nand_dq7", 0),
};

/*
 * List of groups of pins
 */

static const unsigned int spi_pins[] = {0, 1, 2, 3};
static const unsigned int i2c_pins[] = {4, 5};
static const unsigned int mdio_pins[] = {6, 7};
static const unsigned int pwm0_pins[] = {8};
static const unsigned int gpio_b_0_pins[] = {8};
static const unsigned int pwm1_pins[] = {9};
static const unsigned int gpio_b_1_pins[] = {9};
static const unsigned int pwm2_pins[] = {10};
static const unsigned int gpio_b_2_pins[] = {10};
static const unsigned int pwm3_pins[] = {11};
static const unsigned int gpio_b_3_pins[] = {11};
static const unsigned int uart1_pins[] = {12, 13, 14, 15};
static const unsigned int uart2_pins[] = {16, 17};
static const unsigned int synce_pins[] = {18};
static const unsigned int sata0_led_pins[] = {19};
static const unsigned int sata1_led_pins[] = {20};
static const unsigned int xtal_out_pins[] = {21};
static const unsigned int sdio_pwr_pins[] = {22};
static const unsigned int sdio_1p8v_pins[] = {23};
static const unsigned int switch_p05_led0_pins[] = {26};
static const unsigned int switch_p05_led1_pins[] = {27};
static const unsigned int nand_pins[] = {32, 33, 34, 35, 36, 37, 38, 39,
							40, 41, 42};
static const unsigned int emmc_pins[] = {32, 33, 34, 35, 36, 37, 38, 39,
							40, 41, 42};

#define NSP_PIN_GROUP(group_name, ba, sh, ma, al)	\
{							\
	.name = __stringify(group_name) "_grp",		\
	.pins = group_name ## _pins,			\
	.num_pins = ARRAY_SIZE(group_name ## _pins),	\
	.mux = {					\
		.base = ba,				\
		.shift = sh,				\
		.mask = ma,				\
		.alt = al,				\
	}						\
}

/*
 * List of nsp pin groups
 */
static const struct nsp_pin_group nsp_pin_groups[] = {
	NSP_PIN_GROUP(spi, NSP_MUX_BASE0, 0, 0x0f, 0x00),
	NSP_PIN_GROUP(i2c, NSP_MUX_BASE0, 3, 0x03, 0x00),
	NSP_PIN_GROUP(mdio, NSP_MUX_BASE0, 5, 0x03, 0x00),
	NSP_PIN_GROUP(gpio_b_0, NSP_MUX_BASE0, 7, 0x01, 0x00),
	NSP_PIN_GROUP(pwm0, NSP_MUX_BASE1, 0, 0x01, 0x01),
	NSP_PIN_GROUP(gpio_b_1, NSP_MUX_BASE0, 8, 0x01, 0x00),
	NSP_PIN_GROUP(pwm1, NSP_MUX_BASE1, 1, 0x01, 0x01),
	NSP_PIN_GROUP(gpio_b_2, NSP_MUX_BASE0, 9, 0x01, 0x00),
	NSP_PIN_GROUP(pwm2, NSP_MUX_BASE1, 2, 0x01, 0x01),
	NSP_PIN_GROUP(gpio_b_3, NSP_MUX_BASE0, 10, 0x01, 0x00),
	NSP_PIN_GROUP(pwm3, NSP_MUX_BASE1, 3, 0x01, 0x01),
	NSP_PIN_GROUP(uart1, NSP_MUX_BASE0, 11, 0x0f, 0x00),
	NSP_PIN_GROUP(uart2, NSP_MUX_BASE0, 15, 0x03, 0x00),
	NSP_PIN_GROUP(synce, NSP_MUX_BASE0, 17, 0x01, 0x01),
	NSP_PIN_GROUP(sata0_led, NSP_MUX_BASE0, 18, 0x01, 0x01),
	NSP_PIN_GROUP(sata1_led, NSP_MUX_BASE0, 19, 0x01, 0x01),
	NSP_PIN_GROUP(xtal_out, NSP_MUX_BASE0, 20, 0x01, 0x00),
	NSP_PIN_GROUP(sdio_pwr, NSP_MUX_BASE0, 21, 0x01, 0x00),
	NSP_PIN_GROUP(sdio_1p8v, NSP_MUX_BASE0, 22, 0x01, 0x00),
	NSP_PIN_GROUP(switch_p05_led0, NSP_MUX_BASE0, 26, 0x01, 0x01),
	NSP_PIN_GROUP(switch_p05_led1, NSP_MUX_BASE0, 27, 0x01, 0x01),
	NSP_PIN_GROUP(nand, NSP_MUX_BASE2, 0, 0x01, 0x00),
	NSP_PIN_GROUP(emmc, NSP_MUX_BASE2, 0, 0x01, 0x01)
};

/*
 * List of groups supported by functions
 */

static const char * const spi_grps[] = {"spi_grp"};
static const char * const i2c_grps[] = {"i2c_grp"};
static const char * const mdio_grps[] = {"mdio_grp"};
static const char * const pwm_grps[] = {"pwm0_grp", "pwm1_grp", "pwm2_grp"
						, "pwm3_grp"};
static const char * const gpio_b_grps[] = {"gpio_b_0_grp", "gpio_b_1_grp",
					"gpio_b_2_grp", "gpio_b_3_grp"};
static const char * const uart1_grps[] = {"uart1_grp"};
static const char * const uart2_grps[] = {"uart2_grp"};
static const char * const synce_grps[] = {"synce_grp"};
static const char * const sata_led_grps[] = {"sata0_led_grp", "sata1_led_grp"};
static const char * const xtal_out_grps[] = {"xtal_out_grp"};
static const char * const sdio_grps[] = {"sdio_pwr_grp", "sdio_1p8v_grp"};
static const char * const switch_led_grps[] = {"switch_p05_led0_grp",
						"switch_p05_led1_grp"};
static const char * const nand_grps[] = {"nand_grp"};
static const char * const emmc_grps[] = {"emmc_grp"};

#define NSP_PIN_FUNCTION(func)				\
{							\
	.name = #func,					\
	.groups = func ## _grps,			\
	.num_groups = ARRAY_SIZE(func ## _grps),	\
}

/*
 * List of supported functions in nsp
 */
static const struct nsp_pin_function nsp_pin_functions[] = {
	NSP_PIN_FUNCTION(spi),
	NSP_PIN_FUNCTION(i2c),
	NSP_PIN_FUNCTION(mdio),
	NSP_PIN_FUNCTION(pwm),
	NSP_PIN_FUNCTION(gpio_b),
	NSP_PIN_FUNCTION(uart1),
	NSP_PIN_FUNCTION(uart2),
	NSP_PIN_FUNCTION(synce),
	NSP_PIN_FUNCTION(sata_led),
	NSP_PIN_FUNCTION(xtal_out),
	NSP_PIN_FUNCTION(sdio),
	NSP_PIN_FUNCTION(switch_led),
	NSP_PIN_FUNCTION(nand),
	NSP_PIN_FUNCTION(emmc)
};

static int nsp_get_groups_count(struct pinctrl_dev *pctrl_dev)
{
	struct nsp_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);

	return pinctrl->num_groups;
}

static const char *nsp_get_group_name(struct pinctrl_dev *pctrl_dev,
				      unsigned int selector)
{
	struct nsp_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);

	return pinctrl->groups[selector].name;
}

static int nsp_get_group_pins(struct pinctrl_dev *pctrl_dev,
			      unsigned int selector, const unsigned int **pins,
			      unsigned int *num_pins)
{
	struct nsp_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);

	*pins = pinctrl->groups[selector].pins;
	*num_pins = pinctrl->groups[selector].num_pins;

	return 0;
}

static void nsp_pin_dbg_show(struct pinctrl_dev *pctrl_dev,
			     struct seq_file *s, unsigned int offset)
{
	seq_printf(s, " %s", dev_name(pctrl_dev->dev));
}

static const struct pinctrl_ops nsp_pinctrl_ops = {
	.get_groups_count = nsp_get_groups_count,
	.get_group_name = nsp_get_group_name,
	.get_group_pins = nsp_get_group_pins,
	.pin_dbg_show = nsp_pin_dbg_show,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_group,
	.dt_free_map = pinctrl_utils_free_map,
};

static int nsp_get_functions_count(struct pinctrl_dev *pctrl_dev)
{
	struct nsp_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);

	return pinctrl->num_functions;
}

static const char *nsp_get_function_name(struct pinctrl_dev *pctrl_dev,
					 unsigned int selector)
{
	struct nsp_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);

	return pinctrl->functions[selector].name;
}

static int nsp_get_function_groups(struct pinctrl_dev *pctrl_dev,
				   unsigned int selector,
				   const char * const **groups,
				   unsigned * const num_groups)
{
	struct nsp_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);

	*groups = pinctrl->functions[selector].groups;
	*num_groups = pinctrl->functions[selector].num_groups;

	return 0;
}

static int nsp_pinmux_set(struct nsp_pinctrl *pinctrl,
			  const struct nsp_pin_function *func,
			  const struct nsp_pin_group *grp,
			  struct nsp_mux_log *mux_log)
{
	const struct nsp_mux *mux = &grp->mux;
	int i;
	u32 val, mask;
	unsigned long flags;
	void __iomem *base_address;

	for (i = 0; i < pinctrl->num_groups; i++) {
		if ((mux->shift != mux_log[i].mux.shift) ||
			(mux->base != mux_log[i].mux.base))
			continue;

		/* if this is a new configuration, just do it! */
		if (!mux_log[i].is_configured)
			break;

		/*
		 * IOMUX has been configured previously and one is trying to
		 * configure it to a different function
		 */
		if (mux_log[i].mux.alt != mux->alt) {
			dev_err(pinctrl->dev,
				"double configuration error detected!\n");
			dev_err(pinctrl->dev, "func:%s grp:%s\n",
				func->name, grp->name);
			return -EINVAL;
		}

		return 0;
	}
	if (i == pinctrl->num_groups)
		return -EINVAL;

	mask = mux->mask;
	mux_log[i].mux.alt = mux->alt;
	mux_log[i].is_configured = true;

	switch (mux->base) {
	case NSP_MUX_BASE0:
		base_address = pinctrl->base0;
		break;

	case NSP_MUX_BASE1:
		base_address = pinctrl->base1;
		break;

	case NSP_MUX_BASE2:
		base_address = pinctrl->base2;
		break;

	default:
		return -EINVAL;
	}

	spin_lock_irqsave(&pinctrl->lock, flags);
	val = readl(base_address);
	val &= ~(mask << grp->mux.shift);
	val |= grp->mux.alt << grp->mux.shift;
	writel(val, base_address);
	spin_unlock_irqrestore(&pinctrl->lock, flags);

	return 0;
}

static int nsp_pinmux_enable(struct pinctrl_dev *pctrl_dev,
			     unsigned int func_select, unsigned int grp_select)
{
	struct nsp_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);
	const struct nsp_pin_function *func;
	const struct nsp_pin_group *grp;

	if (grp_select >= pinctrl->num_groups ||
	    func_select >= pinctrl->num_functions)
		return -EINVAL;

	func = &pinctrl->functions[func_select];
	grp = &pinctrl->groups[grp_select];

	dev_dbg(pctrl_dev->dev, "func:%u name:%s grp:%u name:%s\n",
		func_select, func->name, grp_select, grp->name);

	dev_dbg(pctrl_dev->dev, "shift:%u alt:%u\n", grp->mux.shift,
		grp->mux.alt);

	return nsp_pinmux_set(pinctrl, func, grp, pinctrl->mux_log);
}


static int nsp_gpio_request_enable(struct pinctrl_dev *pctrl_dev,
				   struct pinctrl_gpio_range *range,
				   unsigned int pin)
{
	struct nsp_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);
	u32 *gpio_select = pctrl_dev->desc->pins[pin].drv_data;
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&pinctrl->lock, flags);
	val = readl(pinctrl->base0);
	if ((val & BIT(pin)) != (*gpio_select << pin)) {
		val &= ~BIT(pin);
		val |= *gpio_select << pin;
		writel(val, pinctrl->base0);
	}
	spin_unlock_irqrestore(&pinctrl->lock, flags);

	return 0;
}

static void nsp_gpio_disable_free(struct pinctrl_dev *pctrl_dev,
				  struct pinctrl_gpio_range *range,
				  unsigned int pin)
{
	struct nsp_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctrl_dev);
	u32 *gpio_select = pctrl_dev->desc->pins[pin].drv_data;
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&pinctrl->lock, flags);
	val = readl(pinctrl->base0);
	if ((val & (1 << pin)) == (*gpio_select << pin)) {
		val &= ~(1 << pin);
		if (!(*gpio_select))
			val |= (1 << pin);
		writel(val, pinctrl->base0);
	}
	spin_unlock_irqrestore(&pinctrl->lock, flags);
}

static const struct pinmux_ops nsp_pinmux_ops = {
	.get_functions_count = nsp_get_functions_count,
	.get_function_name = nsp_get_function_name,
	.get_function_groups = nsp_get_function_groups,
	.set_mux = nsp_pinmux_enable,
	.gpio_request_enable = nsp_gpio_request_enable,
	.gpio_disable_free = nsp_gpio_disable_free,
};

static struct pinctrl_desc nsp_pinctrl_desc = {
	.name = "nsp-pinmux",
	.pctlops = &nsp_pinctrl_ops,
	.pmxops = &nsp_pinmux_ops,
};

static int nsp_mux_log_init(struct nsp_pinctrl *pinctrl)
{
	struct nsp_mux_log *log;
	unsigned int i;
	u32 no_of_groups = ARRAY_SIZE(nsp_pin_groups);

	pinctrl->mux_log = devm_kcalloc(pinctrl->dev, no_of_groups,
					sizeof(struct nsp_mux_log),
					GFP_KERNEL);
	if (!pinctrl->mux_log)
		return -ENOMEM;

	for (i = 0; i < no_of_groups; i++) {
		log = &pinctrl->mux_log[i];
		log->mux.base = nsp_pin_groups[i].mux.base;
		log->mux.shift = nsp_pin_groups[i].mux.shift;
		log->mux.alt = 0;
		log->is_configured = false;
	}

	return 0;
}

static int nsp_pinmux_probe(struct platform_device *pdev)
{
	struct nsp_pinctrl *pinctrl;
	struct resource *res;
	int i, ret;
	struct pinctrl_pin_desc *pins;
	unsigned int num_pins = ARRAY_SIZE(nsp_pins);

	pinctrl = devm_kzalloc(&pdev->dev, sizeof(*pinctrl), GFP_KERNEL);
	if (!pinctrl)
		return -ENOMEM;
	pinctrl->dev = &pdev->dev;
	platform_set_drvdata(pdev, pinctrl);
	spin_lock_init(&pinctrl->lock);

	pinctrl->base0 = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pinctrl->base0))
		return PTR_ERR(pinctrl->base0);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res)
		return -EINVAL;
	pinctrl->base1 = devm_ioremap(&pdev->dev, res->start,
					      resource_size(res));
	if (!pinctrl->base1) {
		dev_err(&pdev->dev, "unable to map I/O space\n");
		return -ENOMEM;
	}

	pinctrl->base2 = devm_platform_ioremap_resource(pdev, 2);
	if (IS_ERR(pinctrl->base2))
		return PTR_ERR(pinctrl->base2);

	ret = nsp_mux_log_init(pinctrl);
	if (ret) {
		dev_err(&pdev->dev, "unable to initialize IOMUX log\n");
		return ret;
	}

	pins = devm_kcalloc(&pdev->dev, num_pins, sizeof(*pins), GFP_KERNEL);
	if (!pins)
		return -ENOMEM;

	for (i = 0; i < num_pins; i++) {
		pins[i].number = nsp_pins[i].pin;
		pins[i].name = nsp_pins[i].name;
		pins[i].drv_data = &nsp_pins[i].gpio_select;
	}

	pinctrl->groups = nsp_pin_groups;
	pinctrl->num_groups = ARRAY_SIZE(nsp_pin_groups);
	pinctrl->functions = nsp_pin_functions;
	pinctrl->num_functions = ARRAY_SIZE(nsp_pin_functions);
	nsp_pinctrl_desc.pins = pins;
	nsp_pinctrl_desc.npins = num_pins;

	pinctrl->pctl = devm_pinctrl_register(&pdev->dev, &nsp_pinctrl_desc,
					 pinctrl);
	if (IS_ERR(pinctrl->pctl)) {
		dev_err(&pdev->dev, "unable to register nsp IOMUX pinctrl\n");
		return PTR_ERR(pinctrl->pctl);
	}

	return 0;
}

static const struct of_device_id nsp_pinmux_of_match[] = {
	{ .compatible = "brcm,nsp-pinmux" },
	{ }
};

static struct platform_driver nsp_pinmux_driver = {
	.driver = {
		.name = "nsp-pinmux",
		.of_match_table = nsp_pinmux_of_match,
	},
	.probe = nsp_pinmux_probe,
};

static int __init nsp_pinmux_init(void)
{
	return platform_driver_register(&nsp_pinmux_driver);
}
arch_initcall(nsp_pinmux_init);
