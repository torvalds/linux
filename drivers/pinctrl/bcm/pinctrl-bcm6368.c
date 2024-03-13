// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for BCM6368 GPIO unit (pinctrl + GPIO)
 *
 * Copyright (C) 2021 Álvaro Fernández Rojas <noltari@gmail.com>
 * Copyright (C) 2016 Jonas Gorski <jonas.gorski@gmail.com>
 */

#include <linux/bits.h>
#include <linux/gpio/driver.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "../pinctrl-utils.h"

#include "pinctrl-bcm63xx.h"

#define BCM6368_NUM_GPIOS	38

#define BCM6368_MODE_REG	0x18
#define BCM6368_BASEMODE_REG	0x38
#define  BCM6368_BASEMODE_MASK	0x7
#define  BCM6368_BASEMODE_GPIO	0x0
#define  BCM6368_BASEMODE_UART1	0x1

struct bcm6368_function {
	const char *name;
	const char * const *groups;
	const unsigned num_groups;

	unsigned dir_out:16;
	unsigned basemode:3;
};

struct bcm6368_priv {
	struct regmap_field *overlays;
};

#define BCM6368_BASEMODE_PIN(a, b)		\
	{					\
		.number = a,			\
		.name = b,			\
		.drv_data = (void *)true	\
	}

static const struct pinctrl_pin_desc bcm6368_pins[] = {
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
	PINCTRL_PIN(16, "gpio16"),
	PINCTRL_PIN(17, "gpio17"),
	PINCTRL_PIN(18, "gpio18"),
	PINCTRL_PIN(19, "gpio19"),
	PINCTRL_PIN(20, "gpio20"),
	PINCTRL_PIN(21, "gpio21"),
	PINCTRL_PIN(22, "gpio22"),
	PINCTRL_PIN(23, "gpio23"),
	PINCTRL_PIN(24, "gpio24"),
	PINCTRL_PIN(25, "gpio25"),
	PINCTRL_PIN(26, "gpio26"),
	PINCTRL_PIN(27, "gpio27"),
	PINCTRL_PIN(28, "gpio28"),
	PINCTRL_PIN(29, "gpio29"),
	BCM6368_BASEMODE_PIN(30, "gpio30"),
	BCM6368_BASEMODE_PIN(31, "gpio31"),
	BCM6368_BASEMODE_PIN(32, "gpio32"),
	BCM6368_BASEMODE_PIN(33, "gpio33"),
	PINCTRL_PIN(34, "gpio34"),
	PINCTRL_PIN(35, "gpio35"),
	PINCTRL_PIN(36, "gpio36"),
	PINCTRL_PIN(37, "gpio37"),
};

static unsigned gpio0_pins[] = { 0 };
static unsigned gpio1_pins[] = { 1 };
static unsigned gpio2_pins[] = { 2 };
static unsigned gpio3_pins[] = { 3 };
static unsigned gpio4_pins[] = { 4 };
static unsigned gpio5_pins[] = { 5 };
static unsigned gpio6_pins[] = { 6 };
static unsigned gpio7_pins[] = { 7 };
static unsigned gpio8_pins[] = { 8 };
static unsigned gpio9_pins[] = { 9 };
static unsigned gpio10_pins[] = { 10 };
static unsigned gpio11_pins[] = { 11 };
static unsigned gpio12_pins[] = { 12 };
static unsigned gpio13_pins[] = { 13 };
static unsigned gpio14_pins[] = { 14 };
static unsigned gpio15_pins[] = { 15 };
static unsigned gpio16_pins[] = { 16 };
static unsigned gpio17_pins[] = { 17 };
static unsigned gpio18_pins[] = { 18 };
static unsigned gpio19_pins[] = { 19 };
static unsigned gpio20_pins[] = { 20 };
static unsigned gpio21_pins[] = { 21 };
static unsigned gpio22_pins[] = { 22 };
static unsigned gpio23_pins[] = { 23 };
static unsigned gpio24_pins[] = { 24 };
static unsigned gpio25_pins[] = { 25 };
static unsigned gpio26_pins[] = { 26 };
static unsigned gpio27_pins[] = { 27 };
static unsigned gpio28_pins[] = { 28 };
static unsigned gpio29_pins[] = { 29 };
static unsigned gpio30_pins[] = { 30 };
static unsigned gpio31_pins[] = { 31 };
static unsigned uart1_grp_pins[] = { 30, 31, 32, 33 };

static struct pingroup bcm6368_groups[] = {
	BCM_PIN_GROUP(gpio0),
	BCM_PIN_GROUP(gpio1),
	BCM_PIN_GROUP(gpio2),
	BCM_PIN_GROUP(gpio3),
	BCM_PIN_GROUP(gpio4),
	BCM_PIN_GROUP(gpio5),
	BCM_PIN_GROUP(gpio6),
	BCM_PIN_GROUP(gpio7),
	BCM_PIN_GROUP(gpio8),
	BCM_PIN_GROUP(gpio9),
	BCM_PIN_GROUP(gpio10),
	BCM_PIN_GROUP(gpio11),
	BCM_PIN_GROUP(gpio12),
	BCM_PIN_GROUP(gpio13),
	BCM_PIN_GROUP(gpio14),
	BCM_PIN_GROUP(gpio15),
	BCM_PIN_GROUP(gpio16),
	BCM_PIN_GROUP(gpio17),
	BCM_PIN_GROUP(gpio18),
	BCM_PIN_GROUP(gpio19),
	BCM_PIN_GROUP(gpio20),
	BCM_PIN_GROUP(gpio21),
	BCM_PIN_GROUP(gpio22),
	BCM_PIN_GROUP(gpio23),
	BCM_PIN_GROUP(gpio24),
	BCM_PIN_GROUP(gpio25),
	BCM_PIN_GROUP(gpio26),
	BCM_PIN_GROUP(gpio27),
	BCM_PIN_GROUP(gpio28),
	BCM_PIN_GROUP(gpio29),
	BCM_PIN_GROUP(gpio30),
	BCM_PIN_GROUP(gpio31),
	BCM_PIN_GROUP(uart1_grp),
};

static const char * const analog_afe_0_groups[] = {
	"gpio0",
};

static const char * const analog_afe_1_groups[] = {
	"gpio1",
};

static const char * const sys_irq_groups[] = {
	"gpio2",
};

static const char * const serial_led_data_groups[] = {
	"gpio3",
};

static const char * const serial_led_clk_groups[] = {
	"gpio4",
};

static const char * const inet_led_groups[] = {
	"gpio5",
};

static const char * const ephy0_led_groups[] = {
	"gpio6",
};

static const char * const ephy1_led_groups[] = {
	"gpio7",
};

static const char * const ephy2_led_groups[] = {
	"gpio8",
};

static const char * const ephy3_led_groups[] = {
	"gpio9",
};

static const char * const robosw_led_data_groups[] = {
	"gpio10",
};

static const char * const robosw_led_clk_groups[] = {
	"gpio11",
};

static const char * const robosw_led0_groups[] = {
	"gpio12",
};

static const char * const robosw_led1_groups[] = {
	"gpio13",
};

static const char * const usb_device_led_groups[] = {
	"gpio14",
};

static const char * const pci_req1_groups[] = {
	"gpio16",
};

static const char * const pci_gnt1_groups[] = {
	"gpio17",
};

static const char * const pci_intb_groups[] = {
	"gpio18",
};

static const char * const pci_req0_groups[] = {
	"gpio19",
};

static const char * const pci_gnt0_groups[] = {
	"gpio20",
};

static const char * const pcmcia_cd1_groups[] = {
	"gpio22",
};

static const char * const pcmcia_cd2_groups[] = {
	"gpio23",
};

static const char * const pcmcia_vs1_groups[] = {
	"gpio24",
};

static const char * const pcmcia_vs2_groups[] = {
	"gpio25",
};

static const char * const ebi_cs2_groups[] = {
	"gpio26",
};

static const char * const ebi_cs3_groups[] = {
	"gpio27",
};

static const char * const spi_cs2_groups[] = {
	"gpio28",
};

static const char * const spi_cs3_groups[] = {
	"gpio29",
};

static const char * const spi_cs4_groups[] = {
	"gpio30",
};

static const char * const spi_cs5_groups[] = {
	"gpio31",
};

static const char * const uart1_groups[] = {
	"uart1_grp",
};

#define BCM6368_FUN(n, out)				\
	{						\
		.name = #n,				\
		.groups = n##_groups,			\
		.num_groups = ARRAY_SIZE(n##_groups),	\
		.dir_out = out,				\
	}

#define BCM6368_BASEMODE_FUN(n, val, out)		\
	{						\
		.name = #n,				\
		.groups = n##_groups,			\
		.num_groups = ARRAY_SIZE(n##_groups),	\
		.basemode = BCM6368_BASEMODE_##val,	\
		.dir_out = out,				\
	}

static const struct bcm6368_function bcm6368_funcs[] = {
	BCM6368_FUN(analog_afe_0, 1),
	BCM6368_FUN(analog_afe_1, 1),
	BCM6368_FUN(sys_irq, 1),
	BCM6368_FUN(serial_led_data, 1),
	BCM6368_FUN(serial_led_clk, 1),
	BCM6368_FUN(inet_led, 1),
	BCM6368_FUN(ephy0_led, 1),
	BCM6368_FUN(ephy1_led, 1),
	BCM6368_FUN(ephy2_led, 1),
	BCM6368_FUN(ephy3_led, 1),
	BCM6368_FUN(robosw_led_data, 1),
	BCM6368_FUN(robosw_led_clk, 1),
	BCM6368_FUN(robosw_led0, 1),
	BCM6368_FUN(robosw_led1, 1),
	BCM6368_FUN(usb_device_led, 1),
	BCM6368_FUN(pci_req1, 0),
	BCM6368_FUN(pci_gnt1, 0),
	BCM6368_FUN(pci_intb, 0),
	BCM6368_FUN(pci_req0, 0),
	BCM6368_FUN(pci_gnt0, 0),
	BCM6368_FUN(pcmcia_cd1, 0),
	BCM6368_FUN(pcmcia_cd2, 0),
	BCM6368_FUN(pcmcia_vs1, 0),
	BCM6368_FUN(pcmcia_vs2, 0),
	BCM6368_FUN(ebi_cs2, 1),
	BCM6368_FUN(ebi_cs3, 1),
	BCM6368_FUN(spi_cs2, 1),
	BCM6368_FUN(spi_cs3, 1),
	BCM6368_FUN(spi_cs4, 1),
	BCM6368_FUN(spi_cs5, 1),
	BCM6368_BASEMODE_FUN(uart1, UART1, 0x6),
};

static int bcm6368_pinctrl_get_group_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(bcm6368_groups);
}

static const char *bcm6368_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						  unsigned group)
{
	return bcm6368_groups[group].name;
}

static int bcm6368_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
					  unsigned group, const unsigned **pins,
					  unsigned *npins)
{
	*pins = bcm6368_groups[group].pins;
	*npins = bcm6368_groups[group].npins;

	return 0;
}

static int bcm6368_pinctrl_get_func_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(bcm6368_funcs);
}

static const char *bcm6368_pinctrl_get_func_name(struct pinctrl_dev *pctldev,
						 unsigned selector)
{
	return bcm6368_funcs[selector].name;
}

static int bcm6368_pinctrl_get_groups(struct pinctrl_dev *pctldev,
				      unsigned selector,
				      const char * const **groups,
				      unsigned * const num_groups)
{
	*groups = bcm6368_funcs[selector].groups;
	*num_groups = bcm6368_funcs[selector].num_groups;

	return 0;
}

static int bcm6368_pinctrl_set_mux(struct pinctrl_dev *pctldev,
				   unsigned selector, unsigned group)
{
	struct bcm63xx_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);
	struct bcm6368_priv *priv = pc->driver_data;
	const struct pingroup *pg = &bcm6368_groups[group];
	const struct bcm6368_function *fun = &bcm6368_funcs[selector];
	int i, pin;

	if (fun->basemode) {
		unsigned int mask = 0;

		for (i = 0; i < pg->npins; i++) {
			pin = pg->pins[i];
			if (pin < BCM63XX_BANK_GPIOS)
				mask |= BIT(pin);
		}

		regmap_update_bits(pc->regs, BCM6368_MODE_REG, mask, 0);
		regmap_field_write(priv->overlays, fun->basemode);
	} else {
		pin = pg->pins[0];

		if (bcm6368_pins[pin].drv_data)
			regmap_field_write(priv->overlays,
					   BCM6368_BASEMODE_GPIO);

		regmap_update_bits(pc->regs, BCM6368_MODE_REG, BIT(pin),
				   BIT(pin));
	}

	for (pin = 0; pin < pg->npins; pin++) {
		struct pinctrl_gpio_range *range;
		int hw_gpio = bcm6368_pins[pin].number;

		range = pinctrl_find_gpio_range_from_pin(pctldev, hw_gpio);
		if (range) {
			struct gpio_chip *gc = range->gc;

			if (fun->dir_out & BIT(pin))
				gc->direction_output(gc, hw_gpio, 0);
			else
				gc->direction_input(gc, hw_gpio);
		}
	}

	return 0;
}

static int bcm6368_gpio_request_enable(struct pinctrl_dev *pctldev,
				       struct pinctrl_gpio_range *range,
				       unsigned offset)
{
	struct bcm63xx_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);
	struct bcm6368_priv *priv = pc->driver_data;

	if (offset >= BCM63XX_BANK_GPIOS && !bcm6368_pins[offset].drv_data)
		return 0;

	/* disable all functions using this pin */
	if (offset < BCM63XX_BANK_GPIOS)
		regmap_update_bits(pc->regs, BCM6368_MODE_REG, BIT(offset), 0);

	if (bcm6368_pins[offset].drv_data)
		regmap_field_write(priv->overlays, BCM6368_BASEMODE_GPIO);

	return 0;
}

static const struct pinctrl_ops bcm6368_pctl_ops = {
	.dt_free_map = pinctrl_utils_free_map,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.get_group_name = bcm6368_pinctrl_get_group_name,
	.get_group_pins = bcm6368_pinctrl_get_group_pins,
	.get_groups_count = bcm6368_pinctrl_get_group_count,
};

static const struct pinmux_ops bcm6368_pmx_ops = {
	.get_function_groups = bcm6368_pinctrl_get_groups,
	.get_function_name = bcm6368_pinctrl_get_func_name,
	.get_functions_count = bcm6368_pinctrl_get_func_count,
	.gpio_request_enable = bcm6368_gpio_request_enable,
	.set_mux = bcm6368_pinctrl_set_mux,
	.strict = true,
};

static const struct bcm63xx_pinctrl_soc bcm6368_soc = {
	.ngpios = BCM6368_NUM_GPIOS,
	.npins = ARRAY_SIZE(bcm6368_pins),
	.pctl_ops = &bcm6368_pctl_ops,
	.pins = bcm6368_pins,
	.pmx_ops = &bcm6368_pmx_ops,
};

static int bcm6368_pinctrl_probe(struct platform_device *pdev)
{
	struct reg_field overlays = REG_FIELD(BCM6368_BASEMODE_REG, 0, 15);
	struct device *dev = &pdev->dev;
	struct bcm63xx_pinctrl *pc;
	struct bcm6368_priv *priv;
	int err;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	err = bcm63xx_pinctrl_probe(pdev, &bcm6368_soc, (void *) priv);
	if (err)
		return err;

	pc = platform_get_drvdata(pdev);

	priv->overlays = devm_regmap_field_alloc(dev, pc->regs, overlays);
	if (IS_ERR(priv->overlays))
		return PTR_ERR(priv->overlays);

	return 0;
}

static const struct of_device_id bcm6368_pinctrl_match[] = {
	{ .compatible = "brcm,bcm6368-pinctrl", },
	{ /* sentinel */ }
};

static struct platform_driver bcm6368_pinctrl_driver = {
	.probe = bcm6368_pinctrl_probe,
	.driver = {
		.name = "bcm6368-pinctrl",
		.of_match_table = bcm6368_pinctrl_match,
	},
};

builtin_platform_driver(bcm6368_pinctrl_driver);
