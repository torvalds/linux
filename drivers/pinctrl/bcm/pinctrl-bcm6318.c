// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for BCM6318 GPIO unit (pinctrl + GPIO)
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

#define BCM6318_NUM_GPIOS	50
#define BCM6318_NUM_MUX		48

#define BCM6318_MODE_REG	0x18
#define BCM6318_MUX_REG		0x1c
#define  BCM6328_MUX_MASK	GENMASK(1, 0)
#define BCM6318_PAD_REG		0x54
#define  BCM6328_PAD_MASK	GENMASK(3, 0)

struct bcm6318_function {
	const char *name;
	const char * const *groups;
	const unsigned num_groups;

	unsigned mode_val:1;
	unsigned mux_val:2;
};

static const struct pinctrl_pin_desc bcm6318_pins[] = {
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
	PINCTRL_PIN(48, "gpio48"),
	PINCTRL_PIN(49, "gpio49"),
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
static unsigned gpio48_pins[] = { 48 };
static unsigned gpio49_pins[] = { 49 };

static struct pingroup bcm6318_groups[] = {
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
	BCM_PIN_GROUP(gpio32),
	BCM_PIN_GROUP(gpio33),
	BCM_PIN_GROUP(gpio34),
	BCM_PIN_GROUP(gpio35),
	BCM_PIN_GROUP(gpio36),
	BCM_PIN_GROUP(gpio37),
	BCM_PIN_GROUP(gpio38),
	BCM_PIN_GROUP(gpio39),
	BCM_PIN_GROUP(gpio40),
	BCM_PIN_GROUP(gpio41),
	BCM_PIN_GROUP(gpio42),
	BCM_PIN_GROUP(gpio43),
	BCM_PIN_GROUP(gpio44),
	BCM_PIN_GROUP(gpio45),
	BCM_PIN_GROUP(gpio46),
	BCM_PIN_GROUP(gpio47),
	BCM_PIN_GROUP(gpio48),
	BCM_PIN_GROUP(gpio49),
};

/* GPIO_MODE */
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

/* PINMUX_SEL */
static const char * const ephy0_spd_led_groups[] = {
	"gpio0",
};

static const char * const ephy1_spd_led_groups[] = {
	"gpio1",
};

static const char * const ephy2_spd_led_groups[] = {
	"gpio2",
};

static const char * const ephy3_spd_led_groups[] = {
	"gpio3",
};

static const char * const ephy0_act_led_groups[] = {
	"gpio4",
};

static const char * const ephy1_act_led_groups[] = {
	"gpio5",
};

static const char * const ephy2_act_led_groups[] = {
	"gpio6",
};

static const char * const ephy3_act_led_groups[] = {
	"gpio7",
};

static const char * const serial_led_data_groups[] = {
	"gpio6",
};

static const char * const serial_led_clk_groups[] = {
	"gpio7",
};

static const char * const inet_act_led_groups[] = {
	"gpio8",
};

static const char * const inet_fail_led_groups[] = {
	"gpio9",
};

static const char * const dsl_led_groups[] = {
	"gpio10",
};

static const char * const post_fail_led_groups[] = {
	"gpio11",
};

static const char * const wlan_wps_led_groups[] = {
	"gpio12",
};

static const char * const usb_pwron_groups[] = {
	"gpio13",
};

static const char * const usb_device_led_groups[] = {
	"gpio13",
};

static const char * const usb_active_groups[] = {
	"gpio40",
};

#define BCM6318_MODE_FUN(n)				\
	{						\
		.name = #n,				\
		.groups = n##_groups,			\
		.num_groups = ARRAY_SIZE(n##_groups),	\
		.mode_val = 1,				\
	}

#define BCM6318_MUX_FUN(n, mux)				\
	{						\
		.name = #n,				\
		.groups = n##_groups,			\
		.num_groups = ARRAY_SIZE(n##_groups),	\
		.mux_val = mux,				\
	}

static const struct bcm6318_function bcm6318_funcs[] = {
	BCM6318_MODE_FUN(led),
	BCM6318_MUX_FUN(ephy0_spd_led, 1),
	BCM6318_MUX_FUN(ephy1_spd_led, 1),
	BCM6318_MUX_FUN(ephy2_spd_led, 1),
	BCM6318_MUX_FUN(ephy3_spd_led, 1),
	BCM6318_MUX_FUN(ephy0_act_led, 1),
	BCM6318_MUX_FUN(ephy1_act_led, 1),
	BCM6318_MUX_FUN(ephy2_act_led, 1),
	BCM6318_MUX_FUN(ephy3_act_led, 1),
	BCM6318_MUX_FUN(serial_led_data, 3),
	BCM6318_MUX_FUN(serial_led_clk, 3),
	BCM6318_MUX_FUN(inet_act_led, 1),
	BCM6318_MUX_FUN(inet_fail_led, 1),
	BCM6318_MUX_FUN(dsl_led, 1),
	BCM6318_MUX_FUN(post_fail_led, 1),
	BCM6318_MUX_FUN(wlan_wps_led, 1),
	BCM6318_MUX_FUN(usb_pwron, 1),
	BCM6318_MUX_FUN(usb_device_led, 2),
	BCM6318_MUX_FUN(usb_active, 2),
};

static inline unsigned int bcm6318_mux_off(unsigned int pin)
{
	return BCM6318_MUX_REG + (pin / 16) * 4;
}

static inline unsigned int bcm6318_pad_off(unsigned int pin)
{
	return BCM6318_PAD_REG + (pin / 8) * 4;
}

static int bcm6318_pinctrl_get_group_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(bcm6318_groups);
}

static const char *bcm6318_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						  unsigned group)
{
	return bcm6318_groups[group].name;
}

static int bcm6318_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
					  unsigned group, const unsigned **pins,
					  unsigned *npins)
{
	*pins = bcm6318_groups[group].pins;
	*npins = bcm6318_groups[group].npins;

	return 0;
}

static int bcm6318_pinctrl_get_func_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(bcm6318_funcs);
}

static const char *bcm6318_pinctrl_get_func_name(struct pinctrl_dev *pctldev,
						 unsigned selector)
{
	return bcm6318_funcs[selector].name;
}

static int bcm6318_pinctrl_get_groups(struct pinctrl_dev *pctldev,
				      unsigned selector,
				      const char * const **groups,
				      unsigned * const num_groups)
{
	*groups = bcm6318_funcs[selector].groups;
	*num_groups = bcm6318_funcs[selector].num_groups;

	return 0;
}

static inline void bcm6318_rmw_mux(struct bcm63xx_pinctrl *pc, unsigned pin,
				   unsigned int mode, unsigned int mux)
{
	if (pin < BCM63XX_BANK_GPIOS)
		regmap_update_bits(pc->regs, BCM6318_MODE_REG, BIT(pin),
				   mode ? BIT(pin) : 0);

	if (pin < BCM6318_NUM_MUX)
		regmap_update_bits(pc->regs,
				   bcm6318_mux_off(pin),
				   BCM6328_MUX_MASK << ((pin % 16) * 2),
				   mux << ((pin % 16) * 2));
}

static inline void bcm6318_set_pad(struct bcm63xx_pinctrl *pc, unsigned pin,
				   uint8_t val)
{
	regmap_update_bits(pc->regs, bcm6318_pad_off(pin),
			   BCM6328_PAD_MASK << ((pin % 8) * 4),
			   val << ((pin % 8) * 4));
}

static int bcm6318_pinctrl_set_mux(struct pinctrl_dev *pctldev,
				   unsigned selector, unsigned group)
{
	struct bcm63xx_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);
	const struct pingroup *pg = &bcm6318_groups[group];
	const struct bcm6318_function *f = &bcm6318_funcs[selector];

	bcm6318_rmw_mux(pc, pg->pins[0], f->mode_val, f->mux_val);

	return 0;
}

static int bcm6318_gpio_request_enable(struct pinctrl_dev *pctldev,
				       struct pinctrl_gpio_range *range,
				       unsigned offset)
{
	struct bcm63xx_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);

	/* disable all functions using this pin */
	if (offset < 13) {
		/* GPIOs 0-12 use mux 0 as GPIO function */
		bcm6318_rmw_mux(pc, offset, 0, 0);
	} else if (offset < 42) {
		/* GPIOs 13-41 use mux 3 as GPIO function */
		bcm6318_rmw_mux(pc, offset, 0, 3);

		bcm6318_set_pad(pc, offset, 0);
	}

	return 0;
}

static const struct pinctrl_ops bcm6318_pctl_ops = {
	.dt_free_map = pinctrl_utils_free_map,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.get_group_name = bcm6318_pinctrl_get_group_name,
	.get_group_pins = bcm6318_pinctrl_get_group_pins,
	.get_groups_count = bcm6318_pinctrl_get_group_count,
};

static const struct pinmux_ops bcm6318_pmx_ops = {
	.get_function_groups = bcm6318_pinctrl_get_groups,
	.get_function_name = bcm6318_pinctrl_get_func_name,
	.get_functions_count = bcm6318_pinctrl_get_func_count,
	.gpio_request_enable = bcm6318_gpio_request_enable,
	.set_mux = bcm6318_pinctrl_set_mux,
	.strict = true,
};

static const struct bcm63xx_pinctrl_soc bcm6318_soc = {
	.ngpios = BCM6318_NUM_GPIOS,
	.npins = ARRAY_SIZE(bcm6318_pins),
	.pctl_ops = &bcm6318_pctl_ops,
	.pins = bcm6318_pins,
	.pmx_ops = &bcm6318_pmx_ops,
};

static int bcm6318_pinctrl_probe(struct platform_device *pdev)
{
	return bcm63xx_pinctrl_probe(pdev, &bcm6318_soc, NULL);
}

static const struct of_device_id bcm6318_pinctrl_match[] = {
	{ .compatible = "brcm,bcm6318-pinctrl", },
	{ /* sentinel */ }
};

static struct platform_driver bcm6318_pinctrl_driver = {
	.probe = bcm6318_pinctrl_probe,
	.driver = {
		.name = "bcm6318-pinctrl",
		.of_match_table = bcm6318_pinctrl_match,
	},
};

builtin_platform_driver(bcm6318_pinctrl_driver);
