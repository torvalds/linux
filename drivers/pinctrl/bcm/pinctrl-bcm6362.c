// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for BCM6362 GPIO unit (pinctrl + GPIO)
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

#define BCM6362_BANK_GPIOS	32
#define BCM6362_NUM_GPIOS	48
#define BCM6362_NUM_LEDS	24

#define BCM6362_LED_REG		0x10
#define BCM6362_MODE_REG	0x18
#define BCM6362_CTRL_REG	0x1c
#define BCM6362_BASEMODE_REG	0x38
#define  BASEMODE_NAND		BIT(2)

enum bcm6362_pinctrl_reg {
	BCM6362_LEDCTRL,
	BCM6362_MODE,
	BCM6362_CTRL,
	BCM6362_BASEMODE,
};

struct bcm6362_pingroup {
	const char *name;
	const unsigned * const pins;
	const unsigned num_pins;
};

struct bcm6362_function {
	const char *name;
	const char * const *groups;
	const unsigned num_groups;

	enum bcm6362_pinctrl_reg reg;
	uint32_t basemode_mask;
};

#define BCM6362_PIN(a, b, mask)			\
	{					\
		.number = a,			\
		.name = b,			\
		.drv_data = (void *)(mask),	\
	}

static const struct pinctrl_pin_desc bcm6362_pins[] = {
	PINCTRL_PIN(0, "gpio0"),
	PINCTRL_PIN(1, "gpio1"),
	PINCTRL_PIN(2, "gpio2"),
	PINCTRL_PIN(3, "gpio3"),
	PINCTRL_PIN(4, "gpio4"),
	PINCTRL_PIN(5, "gpio5"),
	PINCTRL_PIN(6, "gpio6"),
	PINCTRL_PIN(7, "gpio7"),
	BCM6362_PIN(8, "gpio8", BASEMODE_NAND),
	PINCTRL_PIN(9, "gpio9"),
	PINCTRL_PIN(10, "gpio10"),
	PINCTRL_PIN(11, "gpio11"),
	BCM6362_PIN(12, "gpio12", BASEMODE_NAND),
	BCM6362_PIN(13, "gpio13", BASEMODE_NAND),
	BCM6362_PIN(14, "gpio14", BASEMODE_NAND),
	BCM6362_PIN(15, "gpio15", BASEMODE_NAND),
	BCM6362_PIN(16, "gpio16", BASEMODE_NAND),
	BCM6362_PIN(17, "gpio17", BASEMODE_NAND),
	BCM6362_PIN(18, "gpio18", BASEMODE_NAND),
	BCM6362_PIN(19, "gpio19", BASEMODE_NAND),
	BCM6362_PIN(20, "gpio20", BASEMODE_NAND),
	BCM6362_PIN(21, "gpio21", BASEMODE_NAND),
	BCM6362_PIN(22, "gpio22", BASEMODE_NAND),
	BCM6362_PIN(23, "gpio23", BASEMODE_NAND),
	PINCTRL_PIN(24, "gpio24"),
	PINCTRL_PIN(25, "gpio25"),
	PINCTRL_PIN(26, "gpio26"),
	BCM6362_PIN(27, "gpio27", BASEMODE_NAND),
	PINCTRL_PIN(28, "gpio28"),
	PINCTRL_PIN(29, "gpio29"),
	PINCTRL_PIN(30, "gpio30"),
	PINCTRL_PIN(31, "gpio31"),
	PINCTRL_PIN(32, "gpio32"),
	PINCTRL_PIN(33, "gpio33"),
	PINCTRL_PIN(34, "gpio34"),
	PINCTRL_PIN(35, "gpio35"),
	PINCTRL_PIN(36, "gpio36"),
	PINCTRL_PIN(37, "gpio37"),
	PINCTRL_PIN(38, "gpio38"),
	PINCTRL_PIN(39, "gpio39"),
	PINCTRL_PIN(40, "gpio40"),
	PINCTRL_PIN(41, "gpio41"),
	PINCTRL_PIN(42, "gpio42"),
	PINCTRL_PIN(43, "gpio43"),
	PINCTRL_PIN(44, "gpio44"),
	PINCTRL_PIN(45, "gpio45"),
	PINCTRL_PIN(46, "gpio46"),
	PINCTRL_PIN(47, "gpio47"),
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
static unsigned gpio32_pins[] = { 32 };
static unsigned gpio33_pins[] = { 33 };
static unsigned gpio34_pins[] = { 34 };
static unsigned gpio35_pins[] = { 35 };
static unsigned gpio36_pins[] = { 36 };
static unsigned gpio37_pins[] = { 37 };
static unsigned gpio38_pins[] = { 38 };
static unsigned gpio39_pins[] = { 39 };
static unsigned gpio40_pins[] = { 40 };
static unsigned gpio41_pins[] = { 41 };
static unsigned gpio42_pins[] = { 42 };
static unsigned gpio43_pins[] = { 43 };
static unsigned gpio44_pins[] = { 44 };
static unsigned gpio45_pins[] = { 45 };
static unsigned gpio46_pins[] = { 46 };
static unsigned gpio47_pins[] = { 47 };

static unsigned nand_grp_pins[] = {
	8, 12, 13, 14, 15, 16, 17,
	18, 19, 20, 21, 22, 23, 27,
};

#define BCM6362_GROUP(n)				\
	{						\
		.name = #n,				\
		.pins = n##_pins,			\
		.num_pins = ARRAY_SIZE(n##_pins),	\
	}

static struct bcm6362_pingroup bcm6362_groups[] = {
	BCM6362_GROUP(gpio0),
	BCM6362_GROUP(gpio1),
	BCM6362_GROUP(gpio2),
	BCM6362_GROUP(gpio3),
	BCM6362_GROUP(gpio4),
	BCM6362_GROUP(gpio5),
	BCM6362_GROUP(gpio6),
	BCM6362_GROUP(gpio7),
	BCM6362_GROUP(gpio8),
	BCM6362_GROUP(gpio9),
	BCM6362_GROUP(gpio10),
	BCM6362_GROUP(gpio11),
	BCM6362_GROUP(gpio12),
	BCM6362_GROUP(gpio13),
	BCM6362_GROUP(gpio14),
	BCM6362_GROUP(gpio15),
	BCM6362_GROUP(gpio16),
	BCM6362_GROUP(gpio17),
	BCM6362_GROUP(gpio18),
	BCM6362_GROUP(gpio19),
	BCM6362_GROUP(gpio20),
	BCM6362_GROUP(gpio21),
	BCM6362_GROUP(gpio22),
	BCM6362_GROUP(gpio23),
	BCM6362_GROUP(gpio24),
	BCM6362_GROUP(gpio25),
	BCM6362_GROUP(gpio26),
	BCM6362_GROUP(gpio27),
	BCM6362_GROUP(gpio28),
	BCM6362_GROUP(gpio29),
	BCM6362_GROUP(gpio30),
	BCM6362_GROUP(gpio31),
	BCM6362_GROUP(gpio32),
	BCM6362_GROUP(gpio33),
	BCM6362_GROUP(gpio34),
	BCM6362_GROUP(gpio35),
	BCM6362_GROUP(gpio36),
	BCM6362_GROUP(gpio37),
	BCM6362_GROUP(gpio38),
	BCM6362_GROUP(gpio39),
	BCM6362_GROUP(gpio40),
	BCM6362_GROUP(gpio41),
	BCM6362_GROUP(gpio42),
	BCM6362_GROUP(gpio43),
	BCM6362_GROUP(gpio44),
	BCM6362_GROUP(gpio45),
	BCM6362_GROUP(gpio46),
	BCM6362_GROUP(gpio47),
	BCM6362_GROUP(nand_grp),
};

static const char * const led_groups[] = {
	"gpio0",
	"gpio1",
	"gpio2",
	"gpio3",
	"gpio4",
	"gpio5",
	"gpio6",
	"gpio7",
	"gpio8",
	"gpio9",
	"gpio10",
	"gpio11",
	"gpio12",
	"gpio13",
	"gpio14",
	"gpio15",
	"gpio16",
	"gpio17",
	"gpio18",
	"gpio19",
	"gpio20",
	"gpio21",
	"gpio22",
	"gpio23",
};

static const char * const usb_device_led_groups[] = {
	"gpio0",
};

static const char * const sys_irq_groups[] = {
	"gpio1",
};

static const char * const serial_led_clk_groups[] = {
	"gpio2",
};

static const char * const serial_led_data_groups[] = {
	"gpio3",
};

static const char * const robosw_led_data_groups[] = {
	"gpio4",
};

static const char * const robosw_led_clk_groups[] = {
	"gpio5",
};

static const char * const robosw_led0_groups[] = {
	"gpio6",
};

static const char * const robosw_led1_groups[] = {
	"gpio7",
};

static const char * const inet_led_groups[] = {
	"gpio8",
};

static const char * const spi_cs2_groups[] = {
	"gpio9",
};

static const char * const spi_cs3_groups[] = {
	"gpio10",
};

static const char * const ntr_pulse_groups[] = {
	"gpio11",
};

static const char * const uart1_scts_groups[] = {
	"gpio12",
};

static const char * const uart1_srts_groups[] = {
	"gpio13",
};

static const char * const uart1_sdin_groups[] = {
	"gpio14",
};

static const char * const uart1_sdout_groups[] = {
	"gpio15",
};

static const char * const adsl_spi_miso_groups[] = {
	"gpio16",
};

static const char * const adsl_spi_mosi_groups[] = {
	"gpio17",
};

static const char * const adsl_spi_clk_groups[] = {
	"gpio18",
};

static const char * const adsl_spi_cs_groups[] = {
	"gpio19",
};

static const char * const ephy0_led_groups[] = {
	"gpio20",
};

static const char * const ephy1_led_groups[] = {
	"gpio21",
};

static const char * const ephy2_led_groups[] = {
	"gpio22",
};

static const char * const ephy3_led_groups[] = {
	"gpio23",
};

static const char * const ext_irq0_groups[] = {
	"gpio24",
};

static const char * const ext_irq1_groups[] = {
	"gpio25",
};

static const char * const ext_irq2_groups[] = {
	"gpio26",
};

static const char * const ext_irq3_groups[] = {
	"gpio27",
};

static const char * const wifi_groups[] = {
	"gpio32",
	"gpio33",
	"gpio34",
	"gpio35",
	"gpio36",
	"gpio37",
	"gpio38",
	"gpio39",
	"gpio40",
	"gpio41",
	"gpio42",
	"gpio43",
	"gpio44",
	"gpio45",
	"gpio46",
	"gpio47",
};

static const char * const nand_groups[] = {
	"nand_grp",
};

#define BCM6362_LED_FUN(n)				\
	{						\
		.name = #n,				\
		.groups = n##_groups,			\
		.num_groups = ARRAY_SIZE(n##_groups),	\
		.reg = BCM6362_LEDCTRL,			\
	}

#define BCM6362_MODE_FUN(n)				\
	{						\
		.name = #n,				\
		.groups = n##_groups,			\
		.num_groups = ARRAY_SIZE(n##_groups),	\
		.reg = BCM6362_MODE,			\
	}

#define BCM6362_CTRL_FUN(n)				\
	{						\
		.name = #n,				\
		.groups = n##_groups,			\
		.num_groups = ARRAY_SIZE(n##_groups),	\
		.reg = BCM6362_CTRL,			\
	}

#define BCM6362_BASEMODE_FUN(n, mask)			\
	{						\
		.name = #n,				\
		.groups = n##_groups,			\
		.num_groups = ARRAY_SIZE(n##_groups),	\
		.reg = BCM6362_BASEMODE,		\
		.basemode_mask = (mask),		\
	}

static const struct bcm6362_function bcm6362_funcs[] = {
	BCM6362_LED_FUN(led),
	BCM6362_MODE_FUN(usb_device_led),
	BCM6362_MODE_FUN(sys_irq),
	BCM6362_MODE_FUN(serial_led_clk),
	BCM6362_MODE_FUN(serial_led_data),
	BCM6362_MODE_FUN(robosw_led_data),
	BCM6362_MODE_FUN(robosw_led_clk),
	BCM6362_MODE_FUN(robosw_led0),
	BCM6362_MODE_FUN(robosw_led1),
	BCM6362_MODE_FUN(inet_led),
	BCM6362_MODE_FUN(spi_cs2),
	BCM6362_MODE_FUN(spi_cs3),
	BCM6362_MODE_FUN(ntr_pulse),
	BCM6362_MODE_FUN(uart1_scts),
	BCM6362_MODE_FUN(uart1_srts),
	BCM6362_MODE_FUN(uart1_sdin),
	BCM6362_MODE_FUN(uart1_sdout),
	BCM6362_MODE_FUN(adsl_spi_miso),
	BCM6362_MODE_FUN(adsl_spi_mosi),
	BCM6362_MODE_FUN(adsl_spi_clk),
	BCM6362_MODE_FUN(adsl_spi_cs),
	BCM6362_MODE_FUN(ephy0_led),
	BCM6362_MODE_FUN(ephy1_led),
	BCM6362_MODE_FUN(ephy2_led),
	BCM6362_MODE_FUN(ephy3_led),
	BCM6362_MODE_FUN(ext_irq0),
	BCM6362_MODE_FUN(ext_irq1),
	BCM6362_MODE_FUN(ext_irq2),
	BCM6362_MODE_FUN(ext_irq3),
	BCM6362_CTRL_FUN(wifi),
	BCM6362_BASEMODE_FUN(nand, BASEMODE_NAND),
};

static int bcm6362_pinctrl_get_group_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(bcm6362_groups);
}

static const char *bcm6362_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						  unsigned group)
{
	return bcm6362_groups[group].name;
}

static int bcm6362_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
					  unsigned group, const unsigned **pins,
					  unsigned *num_pins)
{
	*pins = bcm6362_groups[group].pins;
	*num_pins = bcm6362_groups[group].num_pins;

	return 0;
}

static int bcm6362_pinctrl_get_func_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(bcm6362_funcs);
}

static const char *bcm6362_pinctrl_get_func_name(struct pinctrl_dev *pctldev,
						 unsigned selector)
{
	return bcm6362_funcs[selector].name;
}

static int bcm6362_pinctrl_get_groups(struct pinctrl_dev *pctldev,
				      unsigned selector,
				      const char * const **groups,
				      unsigned * const num_groups)
{
	*groups = bcm6362_funcs[selector].groups;
	*num_groups = bcm6362_funcs[selector].num_groups;

	return 0;
}

static void bcm6362_set_gpio(struct bcm63xx_pinctrl *pc, unsigned pin)
{
	const struct pinctrl_pin_desc *desc = &bcm6362_pins[pin];
	unsigned int basemode = (uintptr_t)desc->drv_data;
	unsigned int mask = bcm63xx_bank_pin(pin);

	if (basemode)
		regmap_update_bits(pc->regs, BCM6362_BASEMODE_REG, basemode, 0);

	if (pin < BCM63XX_BANK_GPIOS) {
		/* base mode 0 => gpio 1 => mux function */
		regmap_update_bits(pc->regs, BCM6362_MODE_REG, mask, 0);

		/* pins 0-23 might be muxed to led */
		if (pin < BCM6362_NUM_LEDS)
			regmap_update_bits(pc->regs, BCM6362_LED_REG, mask, 0);
	} else {
		/* ctrl reg 0 => wifi function 1 => gpio */
		regmap_update_bits(pc->regs, BCM6362_CTRL_REG, mask, mask);
	}
}

static int bcm6362_pinctrl_set_mux(struct pinctrl_dev *pctldev,
				   unsigned selector, unsigned group)
{
	struct bcm63xx_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);
	const struct bcm6362_pingroup *pg = &bcm6362_groups[group];
	const struct bcm6362_function *f = &bcm6362_funcs[selector];
	unsigned i;
	unsigned int reg;
	unsigned int val, mask;

	for (i = 0; i < pg->num_pins; i++)
		bcm6362_set_gpio(pc, pg->pins[i]);

	switch (f->reg) {
	case BCM6362_LEDCTRL:
		reg = BCM6362_LED_REG;
		mask = BIT(pg->pins[0]);
		val = BIT(pg->pins[0]);
		break;
	case BCM6362_MODE:
		reg = BCM6362_MODE_REG;
		mask = BIT(pg->pins[0]);
		val = BIT(pg->pins[0]);
		break;
	case BCM6362_CTRL:
		reg = BCM6362_CTRL_REG;
		mask = BIT(pg->pins[0]);
		val = 0;
		break;
	case BCM6362_BASEMODE:
		reg = BCM6362_BASEMODE_REG;
		mask = f->basemode_mask;
		val = f->basemode_mask;
		break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	regmap_update_bits(pc->regs, reg, mask, val);

	return 0;
}

static int bcm6362_gpio_request_enable(struct pinctrl_dev *pctldev,
				       struct pinctrl_gpio_range *range,
				       unsigned offset)
{
	struct bcm63xx_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);

	/* disable all functions using this pin */
	bcm6362_set_gpio(pc, offset);

	return 0;
}

static struct pinctrl_ops bcm6362_pctl_ops = {
	.dt_free_map = pinctrl_utils_free_map,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.get_group_name = bcm6362_pinctrl_get_group_name,
	.get_group_pins = bcm6362_pinctrl_get_group_pins,
	.get_groups_count = bcm6362_pinctrl_get_group_count,
};

static struct pinmux_ops bcm6362_pmx_ops = {
	.get_function_groups = bcm6362_pinctrl_get_groups,
	.get_function_name = bcm6362_pinctrl_get_func_name,
	.get_functions_count = bcm6362_pinctrl_get_func_count,
	.gpio_request_enable = bcm6362_gpio_request_enable,
	.set_mux = bcm6362_pinctrl_set_mux,
	.strict = true,
};

static const struct bcm63xx_pinctrl_soc bcm6362_soc = {
	.ngpios = BCM6362_NUM_GPIOS,
	.npins = ARRAY_SIZE(bcm6362_pins),
	.pctl_ops = &bcm6362_pctl_ops,
	.pins = bcm6362_pins,
	.pmx_ops = &bcm6362_pmx_ops,
};

static int bcm6362_pinctrl_probe(struct platform_device *pdev)
{
	return bcm63xx_pinctrl_probe(pdev, &bcm6362_soc, NULL);
}

static const struct of_device_id bcm6362_pinctrl_match[] = {
	{ .compatible = "brcm,bcm6362-pinctrl", },
	{ /* sentinel */ }
};

static struct platform_driver bcm6362_pinctrl_driver = {
	.probe = bcm6362_pinctrl_probe,
	.driver = {
		.name = "bcm6362-pinctrl",
		.of_match_table = bcm6362_pinctrl_match,
	},
};

builtin_platform_driver(bcm6362_pinctrl_driver);
