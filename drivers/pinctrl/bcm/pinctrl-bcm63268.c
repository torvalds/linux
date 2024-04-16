// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for BCM63268 GPIO unit (pinctrl + GPIO)
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

#define BCM63268_NUM_GPIOS		52
#define BCM63268_NUM_LEDS		24

#define BCM63268_LED_REG		0x10
#define BCM63268_MODE_REG		0x18
#define BCM63268_CTRL_REG		0x1c
#define BCM63268_BASEMODE_REG		0x38
#define  BCM63268_BASEMODE_NAND		BIT(2) /* GPIOs 2-7, 24-31 */
#define  BCM63268_BASEMODE_GPIO35	BIT(4) /* GPIO 35 */
#define  BCM63268_BASEMODE_DECTPD	BIT(5) /* GPIOs 8/9 */
#define  BCM63268_BASEMODE_VDSL_PHY_0	BIT(6) /* GPIOs 10/11 */
#define  BCM63268_BASEMODE_VDSL_PHY_1	BIT(7) /* GPIOs 12/13 */
#define  BCM63268_BASEMODE_VDSL_PHY_2	BIT(8) /* GPIOs 24/25 */
#define  BCM63268_BASEMODE_VDSL_PHY_3	BIT(9) /* GPIOs 26/27 */

enum bcm63268_pinctrl_reg {
	BCM63268_LEDCTRL,
	BCM63268_MODE,
	BCM63268_CTRL,
	BCM63268_BASEMODE,
};

struct bcm63268_function {
	const char *name;
	const char * const *groups;
	const unsigned num_groups;

	enum bcm63268_pinctrl_reg reg;
	uint32_t mask;
};

#define BCM63268_PIN(a, b, basemode)			\
	{						\
		.number = a,				\
		.name = b,				\
		.drv_data = (void *)(basemode)		\
	}

static const struct pinctrl_pin_desc bcm63268_pins[] = {
	PINCTRL_PIN(0, "gpio0"),
	PINCTRL_PIN(1, "gpio1"),
	BCM63268_PIN(2, "gpio2", BCM63268_BASEMODE_NAND),
	BCM63268_PIN(3, "gpio3", BCM63268_BASEMODE_NAND),
	BCM63268_PIN(4, "gpio4", BCM63268_BASEMODE_NAND),
	BCM63268_PIN(5, "gpio5", BCM63268_BASEMODE_NAND),
	BCM63268_PIN(6, "gpio6", BCM63268_BASEMODE_NAND),
	BCM63268_PIN(7, "gpio7", BCM63268_BASEMODE_NAND),
	BCM63268_PIN(8, "gpio8", BCM63268_BASEMODE_DECTPD),
	BCM63268_PIN(9, "gpio9", BCM63268_BASEMODE_DECTPD),
	BCM63268_PIN(10, "gpio10", BCM63268_BASEMODE_VDSL_PHY_0),
	BCM63268_PIN(11, "gpio11", BCM63268_BASEMODE_VDSL_PHY_0),
	BCM63268_PIN(12, "gpio12", BCM63268_BASEMODE_VDSL_PHY_1),
	BCM63268_PIN(13, "gpio13", BCM63268_BASEMODE_VDSL_PHY_1),
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
	BCM63268_PIN(24, "gpio24",
		     BCM63268_BASEMODE_NAND | BCM63268_BASEMODE_VDSL_PHY_2),
	BCM63268_PIN(25, "gpio25",
		     BCM63268_BASEMODE_NAND | BCM63268_BASEMODE_VDSL_PHY_2),
	BCM63268_PIN(26, "gpio26",
		     BCM63268_BASEMODE_NAND | BCM63268_BASEMODE_VDSL_PHY_3),
	BCM63268_PIN(27, "gpio27",
		     BCM63268_BASEMODE_NAND | BCM63268_BASEMODE_VDSL_PHY_3),
	BCM63268_PIN(28, "gpio28", BCM63268_BASEMODE_NAND),
	BCM63268_PIN(29, "gpio29", BCM63268_BASEMODE_NAND),
	BCM63268_PIN(30, "gpio30", BCM63268_BASEMODE_NAND),
	BCM63268_PIN(31, "gpio31", BCM63268_BASEMODE_NAND),
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
	PINCTRL_PIN(50, "gpio50"),
	PINCTRL_PIN(51, "gpio51"),
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
static unsigned gpio50_pins[] = { 50 };
static unsigned gpio51_pins[] = { 51 };

static unsigned nand_grp_pins[] = {
	2, 3, 4, 5, 6, 7, 24,
	25, 26, 27, 28, 29, 30, 31,
};

static unsigned dectpd_grp_pins[] = { 8, 9 };
static unsigned vdsl_phy0_grp_pins[] = { 10, 11 };
static unsigned vdsl_phy1_grp_pins[] = { 12, 13 };
static unsigned vdsl_phy2_grp_pins[] = { 24, 25 };
static unsigned vdsl_phy3_grp_pins[] = { 26, 27 };

static struct pingroup bcm63268_groups[] = {
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
	BCM_PIN_GROUP(gpio50),
	BCM_PIN_GROUP(gpio51),

	/* multi pin groups */
	BCM_PIN_GROUP(nand_grp),
	BCM_PIN_GROUP(dectpd_grp),
	BCM_PIN_GROUP(vdsl_phy0_grp),
	BCM_PIN_GROUP(vdsl_phy1_grp),
	BCM_PIN_GROUP(vdsl_phy2_grp),
	BCM_PIN_GROUP(vdsl_phy3_grp),
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

static const char * const serial_led_clk_groups[] = {
	"gpio0",
};

static const char * const serial_led_data_groups[] = {
	"gpio1",
};

static const char * const hsspi_cs4_groups[] = {
	"gpio16",
};

static const char * const hsspi_cs5_groups[] = {
	"gpio17",
};

static const char * const hsspi_cs6_groups[] = {
	"gpio8",
};

static const char * const hsspi_cs7_groups[] = {
	"gpio9",
};

static const char * const uart1_scts_groups[] = {
	"gpio10",
	"gpio24",
};

static const char * const uart1_srts_groups[] = {
	"gpio11",
	"gpio25",
};

static const char * const uart1_sdin_groups[] = {
	"gpio12",
	"gpio26",
};

static const char * const uart1_sdout_groups[] = {
	"gpio13",
	"gpio27",
};

static const char * const ntr_pulse_in_groups[] = {
	"gpio14",
	"gpio28",
};

static const char * const dsl_ntr_pulse_out_groups[] = {
	"gpio15",
	"gpio29",
};

static const char * const adsl_spi_miso_groups[] = {
	"gpio18",
};

static const char * const adsl_spi_mosi_groups[] = {
	"gpio19",
};

static const char * const vreg_clk_groups[] = {
	"gpio22",
};

static const char * const pcie_clkreq_b_groups[] = {
	"gpio23",
};

static const char * const switch_led_clk_groups[] = {
	"gpio30",
};

static const char * const switch_led_data_groups[] = {
	"gpio31",
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
	"gpio48",
	"gpio49",
	"gpio50",
	"gpio51",
};

static const char * const nand_groups[] = {
	"nand_grp",
};

static const char * const dectpd_groups[] = {
	"dectpd_grp",
};

static const char * const vdsl_phy_override_0_groups[] = {
	"vdsl_phy_override_0_grp",
};

static const char * const vdsl_phy_override_1_groups[] = {
	"vdsl_phy_override_1_grp",
};

static const char * const vdsl_phy_override_2_groups[] = {
	"vdsl_phy_override_2_grp",
};

static const char * const vdsl_phy_override_3_groups[] = {
	"vdsl_phy_override_3_grp",
};

#define BCM63268_LED_FUN(n)				\
	{						\
		.name = #n,				\
		.groups = n##_groups,			\
		.num_groups = ARRAY_SIZE(n##_groups),	\
		.reg = BCM63268_LEDCTRL,		\
	}

#define BCM63268_MODE_FUN(n)				\
	{						\
		.name = #n,				\
		.groups = n##_groups,			\
		.num_groups = ARRAY_SIZE(n##_groups),	\
		.reg = BCM63268_MODE,			\
	}

#define BCM63268_CTRL_FUN(n)				\
	{						\
		.name = #n,				\
		.groups = n##_groups,			\
		.num_groups = ARRAY_SIZE(n##_groups),	\
		.reg = BCM63268_CTRL,			\
	}

#define BCM63268_BASEMODE_FUN(n, val)			\
	{						\
		.name = #n,				\
		.groups = n##_groups,			\
		.num_groups = ARRAY_SIZE(n##_groups),	\
		.reg = BCM63268_BASEMODE,		\
		.mask = val,				\
	}

static const struct bcm63268_function bcm63268_funcs[] = {
	BCM63268_LED_FUN(led),
	BCM63268_MODE_FUN(serial_led_clk),
	BCM63268_MODE_FUN(serial_led_data),
	BCM63268_MODE_FUN(hsspi_cs6),
	BCM63268_MODE_FUN(hsspi_cs7),
	BCM63268_MODE_FUN(uart1_scts),
	BCM63268_MODE_FUN(uart1_srts),
	BCM63268_MODE_FUN(uart1_sdin),
	BCM63268_MODE_FUN(uart1_sdout),
	BCM63268_MODE_FUN(ntr_pulse_in),
	BCM63268_MODE_FUN(dsl_ntr_pulse_out),
	BCM63268_MODE_FUN(hsspi_cs4),
	BCM63268_MODE_FUN(hsspi_cs5),
	BCM63268_MODE_FUN(adsl_spi_miso),
	BCM63268_MODE_FUN(adsl_spi_mosi),
	BCM63268_MODE_FUN(vreg_clk),
	BCM63268_MODE_FUN(pcie_clkreq_b),
	BCM63268_MODE_FUN(switch_led_clk),
	BCM63268_MODE_FUN(switch_led_data),
	BCM63268_CTRL_FUN(wifi),
	BCM63268_BASEMODE_FUN(nand, BCM63268_BASEMODE_NAND),
	BCM63268_BASEMODE_FUN(dectpd, BCM63268_BASEMODE_DECTPD),
	BCM63268_BASEMODE_FUN(vdsl_phy_override_0,
			      BCM63268_BASEMODE_VDSL_PHY_0),
	BCM63268_BASEMODE_FUN(vdsl_phy_override_1,
			      BCM63268_BASEMODE_VDSL_PHY_1),
	BCM63268_BASEMODE_FUN(vdsl_phy_override_2,
			      BCM63268_BASEMODE_VDSL_PHY_2),
	BCM63268_BASEMODE_FUN(vdsl_phy_override_3,
			      BCM63268_BASEMODE_VDSL_PHY_3),
};

static int bcm63268_pinctrl_get_group_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(bcm63268_groups);
}

static const char *bcm63268_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						   unsigned group)
{
	return bcm63268_groups[group].name;
}

static int bcm63268_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
					   unsigned group,
					   const unsigned **pins,
					   unsigned *npins)
{
	*pins = bcm63268_groups[group].pins;
	*npins = bcm63268_groups[group].npins;

	return 0;
}

static int bcm63268_pinctrl_get_func_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(bcm63268_funcs);
}

static const char *bcm63268_pinctrl_get_func_name(struct pinctrl_dev *pctldev,
						  unsigned selector)
{
	return bcm63268_funcs[selector].name;
}

static int bcm63268_pinctrl_get_groups(struct pinctrl_dev *pctldev,
				       unsigned selector,
				       const char * const **groups,
				       unsigned * const num_groups)
{
	*groups = bcm63268_funcs[selector].groups;
	*num_groups = bcm63268_funcs[selector].num_groups;

	return 0;
}

static void bcm63268_set_gpio(struct bcm63xx_pinctrl *pc, unsigned pin)
{
	const struct pinctrl_pin_desc *desc = &bcm63268_pins[pin];
	unsigned int basemode = (unsigned long) desc->drv_data;
	unsigned int mask = BIT(bcm63xx_bank_pin(pin));

	if (basemode)
		regmap_update_bits(pc->regs, BCM63268_BASEMODE_REG, basemode,
				   0);

	if (pin < BCM63XX_BANK_GPIOS) {
		/* base mode: 0 => gpio, 1 => mux function */
		regmap_update_bits(pc->regs, BCM63268_MODE_REG, mask, 0);

		/* pins 0-23 might be muxed to led */
		if (pin < BCM63268_NUM_LEDS)
			regmap_update_bits(pc->regs, BCM63268_LED_REG, mask,
					   0);
	} else if (pin < BCM63268_NUM_GPIOS) {
		/* ctrl reg: 0 => wifi function, 1 => gpio */
		regmap_update_bits(pc->regs, BCM63268_CTRL_REG, mask, mask);
	}
}

static int bcm63268_pinctrl_set_mux(struct pinctrl_dev *pctldev,
				    unsigned selector, unsigned group)
{
	struct bcm63xx_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);
	const struct pingroup *pg = &bcm63268_groups[group];
	const struct bcm63268_function *f = &bcm63268_funcs[selector];
	unsigned i;
	unsigned int reg;
	unsigned int val, mask;

	for (i = 0; i < pg->npins; i++)
		bcm63268_set_gpio(pc, pg->pins[i]);

	switch (f->reg) {
	case BCM63268_LEDCTRL:
		reg = BCM63268_LED_REG;
		mask = BIT(pg->pins[0]);
		val = BIT(pg->pins[0]);
		break;
	case BCM63268_MODE:
		reg = BCM63268_MODE_REG;
		mask = BIT(pg->pins[0]);
		val = BIT(pg->pins[0]);
		break;
	case BCM63268_CTRL:
		reg = BCM63268_CTRL_REG;
		mask = BIT(pg->pins[0]);
		val = 0;
		break;
	case BCM63268_BASEMODE:
		reg = BCM63268_BASEMODE_REG;
		mask = f->mask;
		val = f->mask;
		break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	regmap_update_bits(pc->regs, reg, mask, val);

	return 0;
}

static int bcm63268_gpio_request_enable(struct pinctrl_dev *pctldev,
					struct pinctrl_gpio_range *range,
					unsigned offset)
{
	struct bcm63xx_pinctrl *pc = pinctrl_dev_get_drvdata(pctldev);

	/* disable all functions using this pin */
	bcm63268_set_gpio(pc, offset);

	return 0;
}

static const struct pinctrl_ops bcm63268_pctl_ops = {
	.dt_free_map = pinctrl_utils_free_map,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.get_group_name = bcm63268_pinctrl_get_group_name,
	.get_group_pins = bcm63268_pinctrl_get_group_pins,
	.get_groups_count = bcm63268_pinctrl_get_group_count,
};

static const struct pinmux_ops bcm63268_pmx_ops = {
	.get_function_groups = bcm63268_pinctrl_get_groups,
	.get_function_name = bcm63268_pinctrl_get_func_name,
	.get_functions_count = bcm63268_pinctrl_get_func_count,
	.gpio_request_enable = bcm63268_gpio_request_enable,
	.set_mux = bcm63268_pinctrl_set_mux,
	.strict = true,
};

static const struct bcm63xx_pinctrl_soc bcm63268_soc = {
	.ngpios = BCM63268_NUM_GPIOS,
	.npins = ARRAY_SIZE(bcm63268_pins),
	.pctl_ops = &bcm63268_pctl_ops,
	.pins = bcm63268_pins,
	.pmx_ops = &bcm63268_pmx_ops,
};

static int bcm63268_pinctrl_probe(struct platform_device *pdev)
{
	return bcm63xx_pinctrl_probe(pdev, &bcm63268_soc, NULL);
}

static const struct of_device_id bcm63268_pinctrl_match[] = {
	{ .compatible = "brcm,bcm63268-pinctrl", },
	{ /* sentinel */ }
};

static struct platform_driver bcm63268_pinctrl_driver = {
	.probe = bcm63268_pinctrl_probe,
	.driver = {
		.name = "bcm63268-pinctrl",
		.of_match_table = bcm63268_pinctrl_match,
	},
};

builtin_platform_driver(bcm63268_pinctrl_driver);
