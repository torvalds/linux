// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Microsemi SoCs pinctrl driver
 *
 * Author: <alexandre.belloni@free-electrons.com>
 * License: Dual MIT/GPL
 * Copyright (c) 2017 Microsemi Corporation
 */

#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "core.h"
#include "pinconf.h"
#include "pinmux.h"

#define ocelot_clrsetbits(addr, clear, set) \
	writel((readl(addr) & ~(clear)) | (set), (addr))

/* PINCONFIG bits (sparx5 only) */
enum {
	PINCONF_BIAS,
	PINCONF_SCHMITT,
	PINCONF_DRIVE_STRENGTH,
};

#define BIAS_PD_BIT BIT(4)
#define BIAS_PU_BIT BIT(3)
#define BIAS_BITS   (BIAS_PD_BIT|BIAS_PU_BIT)
#define SCHMITT_BIT BIT(2)
#define DRIVE_BITS  GENMASK(1, 0)

/* GPIO standard registers */
#define OCELOT_GPIO_OUT_SET	0x0
#define OCELOT_GPIO_OUT_CLR	0x4
#define OCELOT_GPIO_OUT		0x8
#define OCELOT_GPIO_IN		0xc
#define OCELOT_GPIO_OE		0x10
#define OCELOT_GPIO_INTR	0x14
#define OCELOT_GPIO_INTR_ENA	0x18
#define OCELOT_GPIO_INTR_IDENT	0x1c
#define OCELOT_GPIO_ALT0	0x20
#define OCELOT_GPIO_ALT1	0x24
#define OCELOT_GPIO_SD_MAP	0x28

#define OCELOT_FUNC_PER_PIN	4

enum {
	FUNC_NONE,
	FUNC_GPIO,
	FUNC_IRQ0,
	FUNC_IRQ0_IN,
	FUNC_IRQ0_OUT,
	FUNC_IRQ1,
	FUNC_IRQ1_IN,
	FUNC_IRQ1_OUT,
	FUNC_EXT_IRQ,
	FUNC_MIIM,
	FUNC_PHY_LED,
	FUNC_PCI_WAKE,
	FUNC_MD,
	FUNC_PTP0,
	FUNC_PTP1,
	FUNC_PTP2,
	FUNC_PTP3,
	FUNC_PWM,
	FUNC_RECO_CLK,
	FUNC_SFP,
	FUNC_SG0,
	FUNC_SG1,
	FUNC_SG2,
	FUNC_SI,
	FUNC_SI2,
	FUNC_TACHO,
	FUNC_TWI,
	FUNC_TWI2,
	FUNC_TWI3,
	FUNC_TWI_SCL_M,
	FUNC_UART,
	FUNC_UART2,
	FUNC_UART3,
	FUNC_PLL_STAT,
	FUNC_EMMC,
	FUNC_REF_CLK,
	FUNC_RCVRD_CLK,
	FUNC_MAX
};

static const char *const ocelot_function_names[] = {
	[FUNC_NONE]		= "none",
	[FUNC_GPIO]		= "gpio",
	[FUNC_IRQ0]		= "irq0",
	[FUNC_IRQ0_IN]		= "irq0_in",
	[FUNC_IRQ0_OUT]		= "irq0_out",
	[FUNC_IRQ1]		= "irq1",
	[FUNC_IRQ1_IN]		= "irq1_in",
	[FUNC_IRQ1_OUT]		= "irq1_out",
	[FUNC_EXT_IRQ]		= "ext_irq",
	[FUNC_MIIM]		= "miim",
	[FUNC_PHY_LED]		= "phy_led",
	[FUNC_PCI_WAKE]		= "pci_wake",
	[FUNC_MD]		= "md",
	[FUNC_PTP0]		= "ptp0",
	[FUNC_PTP1]		= "ptp1",
	[FUNC_PTP2]		= "ptp2",
	[FUNC_PTP3]		= "ptp3",
	[FUNC_PWM]		= "pwm",
	[FUNC_RECO_CLK]		= "reco_clk",
	[FUNC_SFP]		= "sfp",
	[FUNC_SG0]		= "sg0",
	[FUNC_SG1]		= "sg1",
	[FUNC_SG2]		= "sg2",
	[FUNC_SI]		= "si",
	[FUNC_SI2]		= "si2",
	[FUNC_TACHO]		= "tacho",
	[FUNC_TWI]		= "twi",
	[FUNC_TWI2]		= "twi2",
	[FUNC_TWI3]		= "twi3",
	[FUNC_TWI_SCL_M]	= "twi_scl_m",
	[FUNC_UART]		= "uart",
	[FUNC_UART2]		= "uart2",
	[FUNC_UART3]		= "uart3",
	[FUNC_PLL_STAT]		= "pll_stat",
	[FUNC_EMMC]		= "emmc",
	[FUNC_REF_CLK]		= "ref_clk",
	[FUNC_RCVRD_CLK]	= "rcvrd_clk",
};

struct ocelot_pmx_func {
	const char **groups;
	unsigned int ngroups;
};

struct ocelot_pin_caps {
	unsigned int pin;
	unsigned char functions[OCELOT_FUNC_PER_PIN];
};

struct ocelot_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctl;
	struct gpio_chip gpio_chip;
	struct regmap *map;
	void __iomem *pincfg;
	struct pinctrl_desc *desc;
	struct ocelot_pmx_func func[FUNC_MAX];
	u8 stride;
};

#define LUTON_P(p, f0, f1)						\
static struct ocelot_pin_caps luton_pin_##p = {				\
	.pin = p,							\
	.functions = {							\
			FUNC_GPIO, FUNC_##f0, FUNC_##f1, FUNC_NONE,	\
	},								\
}

LUTON_P(0,  SG0,       NONE);
LUTON_P(1,  SG0,       NONE);
LUTON_P(2,  SG0,       NONE);
LUTON_P(3,  SG0,       NONE);
LUTON_P(4,  TACHO,     NONE);
LUTON_P(5,  TWI,       PHY_LED);
LUTON_P(6,  TWI,       PHY_LED);
LUTON_P(7,  NONE,      PHY_LED);
LUTON_P(8,  EXT_IRQ,   PHY_LED);
LUTON_P(9,  EXT_IRQ,   PHY_LED);
LUTON_P(10, SFP,       PHY_LED);
LUTON_P(11, SFP,       PHY_LED);
LUTON_P(12, SFP,       PHY_LED);
LUTON_P(13, SFP,       PHY_LED);
LUTON_P(14, SI,        PHY_LED);
LUTON_P(15, SI,        PHY_LED);
LUTON_P(16, SI,        PHY_LED);
LUTON_P(17, SFP,       PHY_LED);
LUTON_P(18, SFP,       PHY_LED);
LUTON_P(19, SFP,       PHY_LED);
LUTON_P(20, SFP,       PHY_LED);
LUTON_P(21, SFP,       PHY_LED);
LUTON_P(22, SFP,       PHY_LED);
LUTON_P(23, SFP,       PHY_LED);
LUTON_P(24, SFP,       PHY_LED);
LUTON_P(25, SFP,       PHY_LED);
LUTON_P(26, SFP,       PHY_LED);
LUTON_P(27, SFP,       PHY_LED);
LUTON_P(28, SFP,       PHY_LED);
LUTON_P(29, PWM,       NONE);
LUTON_P(30, UART,      NONE);
LUTON_P(31, UART,      NONE);

#define LUTON_PIN(n) {						\
	.number = n,						\
	.name = "GPIO_"#n,					\
	.drv_data = &luton_pin_##n				\
}

static const struct pinctrl_pin_desc luton_pins[] = {
	LUTON_PIN(0),
	LUTON_PIN(1),
	LUTON_PIN(2),
	LUTON_PIN(3),
	LUTON_PIN(4),
	LUTON_PIN(5),
	LUTON_PIN(6),
	LUTON_PIN(7),
	LUTON_PIN(8),
	LUTON_PIN(9),
	LUTON_PIN(10),
	LUTON_PIN(11),
	LUTON_PIN(12),
	LUTON_PIN(13),
	LUTON_PIN(14),
	LUTON_PIN(15),
	LUTON_PIN(16),
	LUTON_PIN(17),
	LUTON_PIN(18),
	LUTON_PIN(19),
	LUTON_PIN(20),
	LUTON_PIN(21),
	LUTON_PIN(22),
	LUTON_PIN(23),
	LUTON_PIN(24),
	LUTON_PIN(25),
	LUTON_PIN(26),
	LUTON_PIN(27),
	LUTON_PIN(28),
	LUTON_PIN(29),
	LUTON_PIN(30),
	LUTON_PIN(31),
};

#define SERVAL_P(p, f0, f1, f2)						\
static struct ocelot_pin_caps serval_pin_##p = {			\
	.pin = p,							\
	.functions = {							\
			FUNC_GPIO, FUNC_##f0, FUNC_##f1, FUNC_##f2,	\
	},								\
}

SERVAL_P(0,  SG0,       NONE,      NONE);
SERVAL_P(1,  SG0,       NONE,      NONE);
SERVAL_P(2,  SG0,       NONE,      NONE);
SERVAL_P(3,  SG0,       NONE,      NONE);
SERVAL_P(4,  TACHO,     NONE,      NONE);
SERVAL_P(5,  PWM,       NONE,      NONE);
SERVAL_P(6,  TWI,       NONE,      NONE);
SERVAL_P(7,  TWI,       NONE,      NONE);
SERVAL_P(8,  SI,        NONE,      NONE);
SERVAL_P(9,  SI,        MD,        NONE);
SERVAL_P(10, SI,        MD,        NONE);
SERVAL_P(11, SFP,       MD,        TWI_SCL_M);
SERVAL_P(12, SFP,       MD,        TWI_SCL_M);
SERVAL_P(13, SFP,       UART2,     TWI_SCL_M);
SERVAL_P(14, SFP,       UART2,     TWI_SCL_M);
SERVAL_P(15, SFP,       PTP0,      TWI_SCL_M);
SERVAL_P(16, SFP,       PTP0,      TWI_SCL_M);
SERVAL_P(17, SFP,       PCI_WAKE,  TWI_SCL_M);
SERVAL_P(18, SFP,       NONE,      TWI_SCL_M);
SERVAL_P(19, SFP,       NONE,      TWI_SCL_M);
SERVAL_P(20, SFP,       NONE,      TWI_SCL_M);
SERVAL_P(21, SFP,       NONE,      TWI_SCL_M);
SERVAL_P(22, NONE,      NONE,      NONE);
SERVAL_P(23, NONE,      NONE,      NONE);
SERVAL_P(24, NONE,      NONE,      NONE);
SERVAL_P(25, NONE,      NONE,      NONE);
SERVAL_P(26, UART,      NONE,      NONE);
SERVAL_P(27, UART,      NONE,      NONE);
SERVAL_P(28, IRQ0,      NONE,      NONE);
SERVAL_P(29, IRQ1,      NONE,      NONE);
SERVAL_P(30, PTP0,      NONE,      NONE);
SERVAL_P(31, PTP0,      NONE,      NONE);

#define SERVAL_PIN(n) {						\
	.number = n,						\
	.name = "GPIO_"#n,					\
	.drv_data = &serval_pin_##n				\
}

static const struct pinctrl_pin_desc serval_pins[] = {
	SERVAL_PIN(0),
	SERVAL_PIN(1),
	SERVAL_PIN(2),
	SERVAL_PIN(3),
	SERVAL_PIN(4),
	SERVAL_PIN(5),
	SERVAL_PIN(6),
	SERVAL_PIN(7),
	SERVAL_PIN(8),
	SERVAL_PIN(9),
	SERVAL_PIN(10),
	SERVAL_PIN(11),
	SERVAL_PIN(12),
	SERVAL_PIN(13),
	SERVAL_PIN(14),
	SERVAL_PIN(15),
	SERVAL_PIN(16),
	SERVAL_PIN(17),
	SERVAL_PIN(18),
	SERVAL_PIN(19),
	SERVAL_PIN(20),
	SERVAL_PIN(21),
	SERVAL_PIN(22),
	SERVAL_PIN(23),
	SERVAL_PIN(24),
	SERVAL_PIN(25),
	SERVAL_PIN(26),
	SERVAL_PIN(27),
	SERVAL_PIN(28),
	SERVAL_PIN(29),
	SERVAL_PIN(30),
	SERVAL_PIN(31),
};

#define OCELOT_P(p, f0, f1, f2)						\
static struct ocelot_pin_caps ocelot_pin_##p = {			\
	.pin = p,							\
	.functions = {							\
			FUNC_GPIO, FUNC_##f0, FUNC_##f1, FUNC_##f2,	\
	},								\
}

OCELOT_P(0,  SG0,       NONE,      NONE);
OCELOT_P(1,  SG0,       NONE,      NONE);
OCELOT_P(2,  SG0,       NONE,      NONE);
OCELOT_P(3,  SG0,       NONE,      NONE);
OCELOT_P(4,  IRQ0_IN,   IRQ0_OUT,  TWI_SCL_M);
OCELOT_P(5,  IRQ1_IN,   IRQ1_OUT,  PCI_WAKE);
OCELOT_P(6,  UART,      TWI_SCL_M, NONE);
OCELOT_P(7,  UART,      TWI_SCL_M, NONE);
OCELOT_P(8,  SI,        TWI_SCL_M, IRQ0_OUT);
OCELOT_P(9,  SI,        TWI_SCL_M, IRQ1_OUT);
OCELOT_P(10, PTP2,      TWI_SCL_M, SFP);
OCELOT_P(11, PTP3,      TWI_SCL_M, SFP);
OCELOT_P(12, UART2,     TWI_SCL_M, SFP);
OCELOT_P(13, UART2,     TWI_SCL_M, SFP);
OCELOT_P(14, MIIM,      TWI_SCL_M, SFP);
OCELOT_P(15, MIIM,      TWI_SCL_M, SFP);
OCELOT_P(16, TWI,       NONE,      SI);
OCELOT_P(17, TWI,       TWI_SCL_M, SI);
OCELOT_P(18, PTP0,      TWI_SCL_M, NONE);
OCELOT_P(19, PTP1,      TWI_SCL_M, NONE);
OCELOT_P(20, RECO_CLK,  TACHO,     TWI_SCL_M);
OCELOT_P(21, RECO_CLK,  PWM,       TWI_SCL_M);

#define OCELOT_PIN(n) {						\
	.number = n,						\
	.name = "GPIO_"#n,					\
	.drv_data = &ocelot_pin_##n				\
}

static const struct pinctrl_pin_desc ocelot_pins[] = {
	OCELOT_PIN(0),
	OCELOT_PIN(1),
	OCELOT_PIN(2),
	OCELOT_PIN(3),
	OCELOT_PIN(4),
	OCELOT_PIN(5),
	OCELOT_PIN(6),
	OCELOT_PIN(7),
	OCELOT_PIN(8),
	OCELOT_PIN(9),
	OCELOT_PIN(10),
	OCELOT_PIN(11),
	OCELOT_PIN(12),
	OCELOT_PIN(13),
	OCELOT_PIN(14),
	OCELOT_PIN(15),
	OCELOT_PIN(16),
	OCELOT_PIN(17),
	OCELOT_PIN(18),
	OCELOT_PIN(19),
	OCELOT_PIN(20),
	OCELOT_PIN(21),
};

#define JAGUAR2_P(p, f0, f1)						\
static struct ocelot_pin_caps jaguar2_pin_##p = {			\
	.pin = p,							\
	.functions = {							\
			FUNC_GPIO, FUNC_##f0, FUNC_##f1, FUNC_NONE	\
	},								\
}

JAGUAR2_P(0,  SG0,       NONE);
JAGUAR2_P(1,  SG0,       NONE);
JAGUAR2_P(2,  SG0,       NONE);
JAGUAR2_P(3,  SG0,       NONE);
JAGUAR2_P(4,  SG1,       NONE);
JAGUAR2_P(5,  SG1,       NONE);
JAGUAR2_P(6,  IRQ0_IN,   IRQ0_OUT);
JAGUAR2_P(7,  IRQ1_IN,   IRQ1_OUT);
JAGUAR2_P(8,  PTP0,      NONE);
JAGUAR2_P(9,  PTP1,      NONE);
JAGUAR2_P(10, UART,      NONE);
JAGUAR2_P(11, UART,      NONE);
JAGUAR2_P(12, SG1,       NONE);
JAGUAR2_P(13, SG1,       NONE);
JAGUAR2_P(14, TWI,       TWI_SCL_M);
JAGUAR2_P(15, TWI,       NONE);
JAGUAR2_P(16, SI,        TWI_SCL_M);
JAGUAR2_P(17, SI,        TWI_SCL_M);
JAGUAR2_P(18, SI,        TWI_SCL_M);
JAGUAR2_P(19, PCI_WAKE,  NONE);
JAGUAR2_P(20, IRQ0_OUT,  TWI_SCL_M);
JAGUAR2_P(21, IRQ1_OUT,  TWI_SCL_M);
JAGUAR2_P(22, TACHO,     NONE);
JAGUAR2_P(23, PWM,       NONE);
JAGUAR2_P(24, UART2,     NONE);
JAGUAR2_P(25, UART2,     SI);
JAGUAR2_P(26, PTP2,      SI);
JAGUAR2_P(27, PTP3,      SI);
JAGUAR2_P(28, TWI2,      SI);
JAGUAR2_P(29, TWI2,      SI);
JAGUAR2_P(30, SG2,       SI);
JAGUAR2_P(31, SG2,       SI);
JAGUAR2_P(32, SG2,       SI);
JAGUAR2_P(33, SG2,       SI);
JAGUAR2_P(34, NONE,      TWI_SCL_M);
JAGUAR2_P(35, NONE,      TWI_SCL_M);
JAGUAR2_P(36, NONE,      TWI_SCL_M);
JAGUAR2_P(37, NONE,      TWI_SCL_M);
JAGUAR2_P(38, NONE,      TWI_SCL_M);
JAGUAR2_P(39, NONE,      TWI_SCL_M);
JAGUAR2_P(40, NONE,      TWI_SCL_M);
JAGUAR2_P(41, NONE,      TWI_SCL_M);
JAGUAR2_P(42, NONE,      TWI_SCL_M);
JAGUAR2_P(43, NONE,      TWI_SCL_M);
JAGUAR2_P(44, NONE,      SFP);
JAGUAR2_P(45, NONE,      SFP);
JAGUAR2_P(46, NONE,      SFP);
JAGUAR2_P(47, NONE,      SFP);
JAGUAR2_P(48, SFP,       NONE);
JAGUAR2_P(49, SFP,       SI);
JAGUAR2_P(50, SFP,       SI);
JAGUAR2_P(51, SFP,       SI);
JAGUAR2_P(52, SFP,       NONE);
JAGUAR2_P(53, SFP,       NONE);
JAGUAR2_P(54, SFP,       NONE);
JAGUAR2_P(55, SFP,       NONE);
JAGUAR2_P(56, MIIM,      SFP);
JAGUAR2_P(57, MIIM,      SFP);
JAGUAR2_P(58, MIIM,      SFP);
JAGUAR2_P(59, MIIM,      SFP);
JAGUAR2_P(60, NONE,      NONE);
JAGUAR2_P(61, NONE,      NONE);
JAGUAR2_P(62, NONE,      NONE);
JAGUAR2_P(63, NONE,      NONE);

#define JAGUAR2_PIN(n) {					\
	.number = n,						\
	.name = "GPIO_"#n,					\
	.drv_data = &jaguar2_pin_##n				\
}

static const struct pinctrl_pin_desc jaguar2_pins[] = {
	JAGUAR2_PIN(0),
	JAGUAR2_PIN(1),
	JAGUAR2_PIN(2),
	JAGUAR2_PIN(3),
	JAGUAR2_PIN(4),
	JAGUAR2_PIN(5),
	JAGUAR2_PIN(6),
	JAGUAR2_PIN(7),
	JAGUAR2_PIN(8),
	JAGUAR2_PIN(9),
	JAGUAR2_PIN(10),
	JAGUAR2_PIN(11),
	JAGUAR2_PIN(12),
	JAGUAR2_PIN(13),
	JAGUAR2_PIN(14),
	JAGUAR2_PIN(15),
	JAGUAR2_PIN(16),
	JAGUAR2_PIN(17),
	JAGUAR2_PIN(18),
	JAGUAR2_PIN(19),
	JAGUAR2_PIN(20),
	JAGUAR2_PIN(21),
	JAGUAR2_PIN(22),
	JAGUAR2_PIN(23),
	JAGUAR2_PIN(24),
	JAGUAR2_PIN(25),
	JAGUAR2_PIN(26),
	JAGUAR2_PIN(27),
	JAGUAR2_PIN(28),
	JAGUAR2_PIN(29),
	JAGUAR2_PIN(30),
	JAGUAR2_PIN(31),
	JAGUAR2_PIN(32),
	JAGUAR2_PIN(33),
	JAGUAR2_PIN(34),
	JAGUAR2_PIN(35),
	JAGUAR2_PIN(36),
	JAGUAR2_PIN(37),
	JAGUAR2_PIN(38),
	JAGUAR2_PIN(39),
	JAGUAR2_PIN(40),
	JAGUAR2_PIN(41),
	JAGUAR2_PIN(42),
	JAGUAR2_PIN(43),
	JAGUAR2_PIN(44),
	JAGUAR2_PIN(45),
	JAGUAR2_PIN(46),
	JAGUAR2_PIN(47),
	JAGUAR2_PIN(48),
	JAGUAR2_PIN(49),
	JAGUAR2_PIN(50),
	JAGUAR2_PIN(51),
	JAGUAR2_PIN(52),
	JAGUAR2_PIN(53),
	JAGUAR2_PIN(54),
	JAGUAR2_PIN(55),
	JAGUAR2_PIN(56),
	JAGUAR2_PIN(57),
	JAGUAR2_PIN(58),
	JAGUAR2_PIN(59),
	JAGUAR2_PIN(60),
	JAGUAR2_PIN(61),
	JAGUAR2_PIN(62),
	JAGUAR2_PIN(63),
};

#define SPARX5_P(p, f0, f1, f2)					\
static struct ocelot_pin_caps sparx5_pin_##p = {			\
	.pin = p,							\
	.functions = {							\
		FUNC_GPIO, FUNC_##f0, FUNC_##f1, FUNC_##f2		\
	},								\
}

SPARX5_P(0,  SG0,       PLL_STAT,  NONE);
SPARX5_P(1,  SG0,       NONE,      NONE);
SPARX5_P(2,  SG0,       NONE,      NONE);
SPARX5_P(3,  SG0,       NONE,      NONE);
SPARX5_P(4,  SG1,       NONE,      NONE);
SPARX5_P(5,  SG1,       NONE,      NONE);
SPARX5_P(6,  IRQ0_IN,   IRQ0_OUT,  SFP);
SPARX5_P(7,  IRQ1_IN,   IRQ1_OUT,  SFP);
SPARX5_P(8,  PTP0,      NONE,      SFP);
SPARX5_P(9,  PTP1,      SFP,       TWI_SCL_M);
SPARX5_P(10, UART,      NONE,      NONE);
SPARX5_P(11, UART,      NONE,      NONE);
SPARX5_P(12, SG1,       NONE,      NONE);
SPARX5_P(13, SG1,       NONE,      NONE);
SPARX5_P(14, TWI,       TWI_SCL_M, NONE);
SPARX5_P(15, TWI,       NONE,      NONE);
SPARX5_P(16, SI,        TWI_SCL_M, SFP);
SPARX5_P(17, SI,        TWI_SCL_M, SFP);
SPARX5_P(18, SI,        TWI_SCL_M, SFP);
SPARX5_P(19, PCI_WAKE,  TWI_SCL_M, SFP);
SPARX5_P(20, IRQ0_OUT,  TWI_SCL_M, SFP);
SPARX5_P(21, IRQ1_OUT,  TACHO,     SFP);
SPARX5_P(22, TACHO,     IRQ0_OUT,  TWI_SCL_M);
SPARX5_P(23, PWM,       UART3,     TWI_SCL_M);
SPARX5_P(24, PTP2,      UART3,     TWI_SCL_M);
SPARX5_P(25, PTP3,      SI,        TWI_SCL_M);
SPARX5_P(26, UART2,     SI,        TWI_SCL_M);
SPARX5_P(27, UART2,     SI,        TWI_SCL_M);
SPARX5_P(28, TWI2,      SI,        SFP);
SPARX5_P(29, TWI2,      SI,        SFP);
SPARX5_P(30, SG2,       SI,        PWM);
SPARX5_P(31, SG2,       SI,        TWI_SCL_M);
SPARX5_P(32, SG2,       SI,        TWI_SCL_M);
SPARX5_P(33, SG2,       SI,        SFP);
SPARX5_P(34, NONE,      TWI_SCL_M, EMMC);
SPARX5_P(35, SFP,       TWI_SCL_M, EMMC);
SPARX5_P(36, SFP,       TWI_SCL_M, EMMC);
SPARX5_P(37, SFP,       NONE,      EMMC);
SPARX5_P(38, NONE,      TWI_SCL_M, EMMC);
SPARX5_P(39, SI2,       TWI_SCL_M, EMMC);
SPARX5_P(40, SI2,       TWI_SCL_M, EMMC);
SPARX5_P(41, SI2,       TWI_SCL_M, EMMC);
SPARX5_P(42, SI2,       TWI_SCL_M, EMMC);
SPARX5_P(43, SI2,       TWI_SCL_M, EMMC);
SPARX5_P(44, SI,        SFP,       EMMC);
SPARX5_P(45, SI,        SFP,       EMMC);
SPARX5_P(46, NONE,      SFP,       EMMC);
SPARX5_P(47, NONE,      SFP,       EMMC);
SPARX5_P(48, TWI3,      SI,        SFP);
SPARX5_P(49, TWI3,      NONE,      SFP);
SPARX5_P(50, SFP,       NONE,      TWI_SCL_M);
SPARX5_P(51, SFP,       SI,        TWI_SCL_M);
SPARX5_P(52, SFP,       MIIM,      TWI_SCL_M);
SPARX5_P(53, SFP,       MIIM,      TWI_SCL_M);
SPARX5_P(54, SFP,       PTP2,      TWI_SCL_M);
SPARX5_P(55, SFP,       PTP3,      PCI_WAKE);
SPARX5_P(56, MIIM,      SFP,       TWI_SCL_M);
SPARX5_P(57, MIIM,      SFP,       TWI_SCL_M);
SPARX5_P(58, MIIM,      SFP,       TWI_SCL_M);
SPARX5_P(59, MIIM,      SFP,       NONE);
SPARX5_P(60, RECO_CLK,  NONE,      NONE);
SPARX5_P(61, RECO_CLK,  NONE,      NONE);
SPARX5_P(62, RECO_CLK,  PLL_STAT,  NONE);
SPARX5_P(63, RECO_CLK,  NONE,      NONE);

#define SPARX5_PIN(n) {					\
	.number = n,						\
	.name = "GPIO_"#n,					\
	.drv_data = &sparx5_pin_##n				\
}

static const struct pinctrl_pin_desc sparx5_pins[] = {
	SPARX5_PIN(0),
	SPARX5_PIN(1),
	SPARX5_PIN(2),
	SPARX5_PIN(3),
	SPARX5_PIN(4),
	SPARX5_PIN(5),
	SPARX5_PIN(6),
	SPARX5_PIN(7),
	SPARX5_PIN(8),
	SPARX5_PIN(9),
	SPARX5_PIN(10),
	SPARX5_PIN(11),
	SPARX5_PIN(12),
	SPARX5_PIN(13),
	SPARX5_PIN(14),
	SPARX5_PIN(15),
	SPARX5_PIN(16),
	SPARX5_PIN(17),
	SPARX5_PIN(18),
	SPARX5_PIN(19),
	SPARX5_PIN(20),
	SPARX5_PIN(21),
	SPARX5_PIN(22),
	SPARX5_PIN(23),
	SPARX5_PIN(24),
	SPARX5_PIN(25),
	SPARX5_PIN(26),
	SPARX5_PIN(27),
	SPARX5_PIN(28),
	SPARX5_PIN(29),
	SPARX5_PIN(30),
	SPARX5_PIN(31),
	SPARX5_PIN(32),
	SPARX5_PIN(33),
	SPARX5_PIN(34),
	SPARX5_PIN(35),
	SPARX5_PIN(36),
	SPARX5_PIN(37),
	SPARX5_PIN(38),
	SPARX5_PIN(39),
	SPARX5_PIN(40),
	SPARX5_PIN(41),
	SPARX5_PIN(42),
	SPARX5_PIN(43),
	SPARX5_PIN(44),
	SPARX5_PIN(45),
	SPARX5_PIN(46),
	SPARX5_PIN(47),
	SPARX5_PIN(48),
	SPARX5_PIN(49),
	SPARX5_PIN(50),
	SPARX5_PIN(51),
	SPARX5_PIN(52),
	SPARX5_PIN(53),
	SPARX5_PIN(54),
	SPARX5_PIN(55),
	SPARX5_PIN(56),
	SPARX5_PIN(57),
	SPARX5_PIN(58),
	SPARX5_PIN(59),
	SPARX5_PIN(60),
	SPARX5_PIN(61),
	SPARX5_PIN(62),
	SPARX5_PIN(63),
};

static int ocelot_get_functions_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(ocelot_function_names);
}

static const char *ocelot_get_function_name(struct pinctrl_dev *pctldev,
					    unsigned int function)
{
	return ocelot_function_names[function];
}

static int ocelot_get_function_groups(struct pinctrl_dev *pctldev,
				      unsigned int function,
				      const char *const **groups,
				      unsigned *const num_groups)
{
	struct ocelot_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	*groups  = info->func[function].groups;
	*num_groups = info->func[function].ngroups;

	return 0;
}

static int ocelot_pin_function_idx(struct ocelot_pinctrl *info,
				   unsigned int pin, unsigned int function)
{
	struct ocelot_pin_caps *p = info->desc->pins[pin].drv_data;
	int i;

	for (i = 0; i < OCELOT_FUNC_PER_PIN; i++) {
		if (function == p->functions[i])
			return i;
	}

	return -1;
}

#define REG_ALT(msb, info, p) (OCELOT_GPIO_ALT0 * (info)->stride + 4 * ((msb) + ((info)->stride * ((p) / 32))))

static int ocelot_pinmux_set_mux(struct pinctrl_dev *pctldev,
				 unsigned int selector, unsigned int group)
{
	struct ocelot_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	struct ocelot_pin_caps *pin = info->desc->pins[group].drv_data;
	unsigned int p = pin->pin % 32;
	int f;

	f = ocelot_pin_function_idx(info, group, selector);
	if (f < 0)
		return -EINVAL;

	/*
	 * f is encoded on two bits.
	 * bit 0 of f goes in BIT(pin) of ALT[0], bit 1 of f goes in BIT(pin) of
	 * ALT[1]
	 * This is racy because both registers can't be updated at the same time
	 * but it doesn't matter much for now.
	 * Note: ALT0/ALT1 are organized specially for 64 gpio targets
	 */
	regmap_update_bits(info->map, REG_ALT(0, info, pin->pin),
			   BIT(p), f << p);
	regmap_update_bits(info->map, REG_ALT(1, info, pin->pin),
			   BIT(p), f << (p - 1));

	return 0;
}

#define REG(r, info, p) ((r) * (info)->stride + (4 * ((p) / 32)))

static int ocelot_gpio_set_direction(struct pinctrl_dev *pctldev,
				     struct pinctrl_gpio_range *range,
				     unsigned int pin, bool input)
{
	struct ocelot_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	unsigned int p = pin % 32;

	regmap_update_bits(info->map, REG(OCELOT_GPIO_OE, info, pin), BIT(p),
			   input ? 0 : BIT(p));

	return 0;
}

static int ocelot_gpio_request_enable(struct pinctrl_dev *pctldev,
				      struct pinctrl_gpio_range *range,
				      unsigned int offset)
{
	struct ocelot_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	unsigned int p = offset % 32;

	regmap_update_bits(info->map, REG_ALT(0, info, offset),
			   BIT(p), 0);
	regmap_update_bits(info->map, REG_ALT(1, info, offset),
			   BIT(p), 0);

	return 0;
}

static const struct pinmux_ops ocelot_pmx_ops = {
	.get_functions_count = ocelot_get_functions_count,
	.get_function_name = ocelot_get_function_name,
	.get_function_groups = ocelot_get_function_groups,
	.set_mux = ocelot_pinmux_set_mux,
	.gpio_set_direction = ocelot_gpio_set_direction,
	.gpio_request_enable = ocelot_gpio_request_enable,
};

static int ocelot_pctl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct ocelot_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	return info->desc->npins;
}

static const char *ocelot_pctl_get_group_name(struct pinctrl_dev *pctldev,
					      unsigned int group)
{
	struct ocelot_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	return info->desc->pins[group].name;
}

static int ocelot_pctl_get_group_pins(struct pinctrl_dev *pctldev,
				      unsigned int group,
				      const unsigned int **pins,
				      unsigned int *num_pins)
{
	struct ocelot_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	*pins = &info->desc->pins[group].number;
	*num_pins = 1;

	return 0;
}

static int ocelot_hw_get_value(struct ocelot_pinctrl *info,
			       unsigned int pin,
			       unsigned int reg,
			       int *val)
{
	int ret = -EOPNOTSUPP;

	if (info->pincfg) {
		u32 regcfg = readl(info->pincfg + (pin * sizeof(u32)));

		ret = 0;
		switch (reg) {
		case PINCONF_BIAS:
			*val = regcfg & BIAS_BITS;
			break;

		case PINCONF_SCHMITT:
			*val = regcfg & SCHMITT_BIT;
			break;

		case PINCONF_DRIVE_STRENGTH:
			*val = regcfg & DRIVE_BITS;
			break;

		default:
			ret = -EOPNOTSUPP;
			break;
		}
	}
	return ret;
}

static int ocelot_hw_set_value(struct ocelot_pinctrl *info,
			       unsigned int pin,
			       unsigned int reg,
			       int val)
{
	int ret = -EOPNOTSUPP;

	if (info->pincfg) {
		void __iomem *regaddr = info->pincfg + (pin * sizeof(u32));

		ret = 0;
		switch (reg) {
		case PINCONF_BIAS:
			ocelot_clrsetbits(regaddr, BIAS_BITS, val);
			break;

		case PINCONF_SCHMITT:
			ocelot_clrsetbits(regaddr, SCHMITT_BIT, val);
			break;

		case PINCONF_DRIVE_STRENGTH:
			if (val <= 3)
				ocelot_clrsetbits(regaddr, DRIVE_BITS, val);
			else
				ret = -EINVAL;
			break;

		default:
			ret = -EOPNOTSUPP;
			break;
		}
	}
	return ret;
}

static int ocelot_pinconf_get(struct pinctrl_dev *pctldev,
			      unsigned int pin, unsigned long *config)
{
	struct ocelot_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	u32 param = pinconf_to_config_param(*config);
	int val, err;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_DOWN:
		err = ocelot_hw_get_value(info, pin, PINCONF_BIAS, &val);
		if (err)
			return err;
		if (param == PIN_CONFIG_BIAS_DISABLE)
			val = (val == 0);
		else if (param == PIN_CONFIG_BIAS_PULL_DOWN)
			val = (val & BIAS_PD_BIT ? true : false);
		else    /* PIN_CONFIG_BIAS_PULL_UP */
			val = (val & BIAS_PU_BIT ? true : false);
		break;

	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		err = ocelot_hw_get_value(info, pin, PINCONF_SCHMITT, &val);
		if (err)
			return err;

		val = (val & SCHMITT_BIT ? true : false);
		break;

	case PIN_CONFIG_DRIVE_STRENGTH:
		err = ocelot_hw_get_value(info, pin, PINCONF_DRIVE_STRENGTH,
					  &val);
		if (err)
			return err;
		break;

	case PIN_CONFIG_OUTPUT:
		err = regmap_read(info->map, REG(OCELOT_GPIO_OUT, info, pin),
				  &val);
		if (err)
			return err;
		val = !!(val & BIT(pin % 32));
		break;

	case PIN_CONFIG_INPUT_ENABLE:
	case PIN_CONFIG_OUTPUT_ENABLE:
		err = regmap_read(info->map, REG(OCELOT_GPIO_OE, info, pin),
				  &val);
		if (err)
			return err;
		val = val & BIT(pin % 32);
		if (param == PIN_CONFIG_OUTPUT_ENABLE)
			val = !!val;
		else
			val = !val;
		break;

	default:
		return -EOPNOTSUPP;
	}

	*config = pinconf_to_config_packed(param, val);

	return 0;
}

static int ocelot_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
			      unsigned long *configs, unsigned int num_configs)
{
	struct ocelot_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	u32 param, arg, p;
	int cfg, err = 0;

	for (cfg = 0; cfg < num_configs; cfg++) {
		param = pinconf_to_config_param(configs[cfg]);
		arg = pinconf_to_config_argument(configs[cfg]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
		case PIN_CONFIG_BIAS_PULL_UP:
		case PIN_CONFIG_BIAS_PULL_DOWN:
			arg = (param == PIN_CONFIG_BIAS_DISABLE) ? 0 :
			(param == PIN_CONFIG_BIAS_PULL_UP) ? BIAS_PU_BIT :
			BIAS_PD_BIT;

			err = ocelot_hw_set_value(info, pin, PINCONF_BIAS, arg);
			if (err)
				goto err;

			break;

		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			arg = arg ? SCHMITT_BIT : 0;
			err = ocelot_hw_set_value(info, pin, PINCONF_SCHMITT,
						  arg);
			if (err)
				goto err;

			break;

		case PIN_CONFIG_DRIVE_STRENGTH:
			err = ocelot_hw_set_value(info, pin,
						  PINCONF_DRIVE_STRENGTH,
						  arg);
			if (err)
				goto err;

			break;

		case PIN_CONFIG_OUTPUT_ENABLE:
		case PIN_CONFIG_INPUT_ENABLE:
		case PIN_CONFIG_OUTPUT:
			p = pin % 32;
			if (arg)
				regmap_write(info->map,
					     REG(OCELOT_GPIO_OUT_SET, info,
						 pin),
					     BIT(p));
			else
				regmap_write(info->map,
					     REG(OCELOT_GPIO_OUT_CLR, info,
						 pin),
					     BIT(p));
			regmap_update_bits(info->map,
					   REG(OCELOT_GPIO_OE, info, pin),
					   BIT(p),
					   param == PIN_CONFIG_INPUT_ENABLE ?
					   0 : BIT(p));
			break;

		default:
			err = -EOPNOTSUPP;
		}
	}
err:
	return err;
}

static const struct pinconf_ops ocelot_confops = {
	.is_generic = true,
	.pin_config_get = ocelot_pinconf_get,
	.pin_config_set = ocelot_pinconf_set,
	.pin_config_config_dbg_show = pinconf_generic_dump_config,
};

static const struct pinctrl_ops ocelot_pctl_ops = {
	.get_groups_count = ocelot_pctl_get_groups_count,
	.get_group_name = ocelot_pctl_get_group_name,
	.get_group_pins = ocelot_pctl_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.dt_free_map = pinconf_generic_dt_free_map,
};

static struct pinctrl_desc luton_desc = {
	.name = "luton-pinctrl",
	.pins = luton_pins,
	.npins = ARRAY_SIZE(luton_pins),
	.pctlops = &ocelot_pctl_ops,
	.pmxops = &ocelot_pmx_ops,
	.owner = THIS_MODULE,
};

static struct pinctrl_desc serval_desc = {
	.name = "serval-pinctrl",
	.pins = serval_pins,
	.npins = ARRAY_SIZE(serval_pins),
	.pctlops = &ocelot_pctl_ops,
	.pmxops = &ocelot_pmx_ops,
	.owner = THIS_MODULE,
};

static struct pinctrl_desc ocelot_desc = {
	.name = "ocelot-pinctrl",
	.pins = ocelot_pins,
	.npins = ARRAY_SIZE(ocelot_pins),
	.pctlops = &ocelot_pctl_ops,
	.pmxops = &ocelot_pmx_ops,
	.owner = THIS_MODULE,
};

static struct pinctrl_desc jaguar2_desc = {
	.name = "jaguar2-pinctrl",
	.pins = jaguar2_pins,
	.npins = ARRAY_SIZE(jaguar2_pins),
	.pctlops = &ocelot_pctl_ops,
	.pmxops = &ocelot_pmx_ops,
	.owner = THIS_MODULE,
};

static struct pinctrl_desc sparx5_desc = {
	.name = "sparx5-pinctrl",
	.pins = sparx5_pins,
	.npins = ARRAY_SIZE(sparx5_pins),
	.pctlops = &ocelot_pctl_ops,
	.pmxops = &ocelot_pmx_ops,
	.confops = &ocelot_confops,
	.owner = THIS_MODULE,
};

static int ocelot_create_group_func_map(struct device *dev,
					struct ocelot_pinctrl *info)
{
	int f, npins, i;
	u8 *pins = kcalloc(info->desc->npins, sizeof(u8), GFP_KERNEL);

	if (!pins)
		return -ENOMEM;

	for (f = 0; f < FUNC_MAX; f++) {
		for (npins = 0, i = 0; i < info->desc->npins; i++) {
			if (ocelot_pin_function_idx(info, i, f) >= 0)
				pins[npins++] = i;
		}

		if (!npins)
			continue;

		info->func[f].ngroups = npins;
		info->func[f].groups = devm_kcalloc(dev, npins, sizeof(char *),
						    GFP_KERNEL);
		if (!info->func[f].groups) {
			kfree(pins);
			return -ENOMEM;
		}

		for (i = 0; i < npins; i++)
			info->func[f].groups[i] =
				info->desc->pins[pins[i]].name;
	}

	kfree(pins);

	return 0;
}

static int ocelot_pinctrl_register(struct platform_device *pdev,
				   struct ocelot_pinctrl *info)
{
	int ret;

	ret = ocelot_create_group_func_map(&pdev->dev, info);
	if (ret) {
		dev_err(&pdev->dev, "Unable to create group func map.\n");
		return ret;
	}

	info->pctl = devm_pinctrl_register(&pdev->dev, info->desc, info);
	if (IS_ERR(info->pctl)) {
		dev_err(&pdev->dev, "Failed to register pinctrl\n");
		return PTR_ERR(info->pctl);
	}

	return 0;
}

static int ocelot_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct ocelot_pinctrl *info = gpiochip_get_data(chip);
	unsigned int val;

	regmap_read(info->map, REG(OCELOT_GPIO_IN, info, offset), &val);

	return !!(val & BIT(offset % 32));
}

static void ocelot_gpio_set(struct gpio_chip *chip, unsigned int offset,
			    int value)
{
	struct ocelot_pinctrl *info = gpiochip_get_data(chip);

	if (value)
		regmap_write(info->map, REG(OCELOT_GPIO_OUT_SET, info, offset),
			     BIT(offset % 32));
	else
		regmap_write(info->map, REG(OCELOT_GPIO_OUT_CLR, info, offset),
			     BIT(offset % 32));
}

static int ocelot_gpio_get_direction(struct gpio_chip *chip,
				     unsigned int offset)
{
	struct ocelot_pinctrl *info = gpiochip_get_data(chip);
	unsigned int val;

	regmap_read(info->map, REG(OCELOT_GPIO_OE, info, offset), &val);

	if (val & BIT(offset % 32))
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static int ocelot_gpio_direction_input(struct gpio_chip *chip,
				       unsigned int offset)
{
	return pinctrl_gpio_direction_input(chip->base + offset);
}

static int ocelot_gpio_direction_output(struct gpio_chip *chip,
					unsigned int offset, int value)
{
	struct ocelot_pinctrl *info = gpiochip_get_data(chip);
	unsigned int pin = BIT(offset % 32);

	if (value)
		regmap_write(info->map, REG(OCELOT_GPIO_OUT_SET, info, offset),
			     pin);
	else
		regmap_write(info->map, REG(OCELOT_GPIO_OUT_CLR, info, offset),
			     pin);

	return pinctrl_gpio_direction_output(chip->base + offset);
}

static const struct gpio_chip ocelot_gpiolib_chip = {
	.request = gpiochip_generic_request,
	.free = gpiochip_generic_free,
	.set = ocelot_gpio_set,
	.get = ocelot_gpio_get,
	.get_direction = ocelot_gpio_get_direction,
	.direction_input = ocelot_gpio_direction_input,
	.direction_output = ocelot_gpio_direction_output,
	.owner = THIS_MODULE,
};

static void ocelot_irq_mask(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct ocelot_pinctrl *info = gpiochip_get_data(chip);
	unsigned int gpio = irqd_to_hwirq(data);

	regmap_update_bits(info->map, REG(OCELOT_GPIO_INTR_ENA, info, gpio),
			   BIT(gpio % 32), 0);
}

static void ocelot_irq_unmask(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct ocelot_pinctrl *info = gpiochip_get_data(chip);
	unsigned int gpio = irqd_to_hwirq(data);

	regmap_update_bits(info->map, REG(OCELOT_GPIO_INTR_ENA, info, gpio),
			   BIT(gpio % 32), BIT(gpio % 32));
}

static void ocelot_irq_ack(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct ocelot_pinctrl *info = gpiochip_get_data(chip);
	unsigned int gpio = irqd_to_hwirq(data);

	regmap_write_bits(info->map, REG(OCELOT_GPIO_INTR, info, gpio),
			  BIT(gpio % 32), BIT(gpio % 32));
}

static int ocelot_irq_set_type(struct irq_data *data, unsigned int type);

static struct irq_chip ocelot_eoi_irqchip = {
	.name		= "gpio",
	.irq_mask	= ocelot_irq_mask,
	.irq_eoi	= ocelot_irq_ack,
	.irq_unmask	= ocelot_irq_unmask,
	.flags          = IRQCHIP_EOI_THREADED | IRQCHIP_EOI_IF_HANDLED,
	.irq_set_type	= ocelot_irq_set_type,
};

static struct irq_chip ocelot_irqchip = {
	.name		= "gpio",
	.irq_mask	= ocelot_irq_mask,
	.irq_ack	= ocelot_irq_ack,
	.irq_unmask	= ocelot_irq_unmask,
	.irq_set_type	= ocelot_irq_set_type,
};

static int ocelot_irq_set_type(struct irq_data *data, unsigned int type)
{
	type &= IRQ_TYPE_SENSE_MASK;

	if (!(type & (IRQ_TYPE_EDGE_BOTH | IRQ_TYPE_LEVEL_HIGH)))
		return -EINVAL;

	if (type & IRQ_TYPE_LEVEL_HIGH)
		irq_set_chip_handler_name_locked(data, &ocelot_eoi_irqchip,
						 handle_fasteoi_irq, NULL);
	if (type & IRQ_TYPE_EDGE_BOTH)
		irq_set_chip_handler_name_locked(data, &ocelot_irqchip,
						 handle_edge_irq, NULL);

	return 0;
}

static void ocelot_irq_handler(struct irq_desc *desc)
{
	struct irq_chip *parent_chip = irq_desc_get_chip(desc);
	struct gpio_chip *chip = irq_desc_get_handler_data(desc);
	struct ocelot_pinctrl *info = gpiochip_get_data(chip);
	unsigned int id_reg = OCELOT_GPIO_INTR_IDENT * info->stride;
	unsigned int reg = 0, irq, i;
	unsigned long irqs;

	for (i = 0; i < info->stride; i++) {
		regmap_read(info->map, id_reg + 4 * i, &reg);
		if (!reg)
			continue;

		chained_irq_enter(parent_chip, desc);

		irqs = reg;

		for_each_set_bit(irq, &irqs,
				 min(32U, info->desc->npins - 32 * i))
			generic_handle_irq(irq_linear_revmap(chip->irq.domain,
							     irq + 32 * i));

		chained_irq_exit(parent_chip, desc);
	}
}

static int ocelot_gpiochip_register(struct platform_device *pdev,
				    struct ocelot_pinctrl *info)
{
	struct gpio_chip *gc;
	struct gpio_irq_chip *girq;
	int irq;

	info->gpio_chip = ocelot_gpiolib_chip;

	gc = &info->gpio_chip;
	gc->ngpio = info->desc->npins;
	gc->parent = &pdev->dev;
	gc->base = 0;
	gc->of_node = info->dev->of_node;
	gc->label = "ocelot-gpio";

	irq = irq_of_parse_and_map(gc->of_node, 0);
	if (irq) {
		girq = &gc->irq;
		girq->chip = &ocelot_irqchip;
		girq->parent_handler = ocelot_irq_handler;
		girq->num_parents = 1;
		girq->parents = devm_kcalloc(&pdev->dev, 1,
					     sizeof(*girq->parents),
					     GFP_KERNEL);
		if (!girq->parents)
			return -ENOMEM;
		girq->parents[0] = irq;
		girq->default_type = IRQ_TYPE_NONE;
		girq->handler = handle_edge_irq;
	}

	return devm_gpiochip_add_data(&pdev->dev, gc, info);
}

static const struct of_device_id ocelot_pinctrl_of_match[] = {
	{ .compatible = "mscc,luton-pinctrl", .data = &luton_desc },
	{ .compatible = "mscc,serval-pinctrl", .data = &serval_desc },
	{ .compatible = "mscc,ocelot-pinctrl", .data = &ocelot_desc },
	{ .compatible = "mscc,jaguar2-pinctrl", .data = &jaguar2_desc },
	{ .compatible = "microchip,sparx5-pinctrl", .data = &sparx5_desc },
	{},
};

static int ocelot_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ocelot_pinctrl *info;
	void __iomem *base;
	struct resource *res;
	int ret;
	struct regmap_config regmap_config = {
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
	};

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->desc = (struct pinctrl_desc *)device_get_match_data(dev);

	base = devm_ioremap_resource(dev,
			platform_get_resource(pdev, IORESOURCE_MEM, 0));
	if (IS_ERR(base))
		return PTR_ERR(base);

	info->stride = 1 + (info->desc->npins - 1) / 32;

	regmap_config.max_register = OCELOT_GPIO_SD_MAP * info->stride + 15 * 4;

	info->map = devm_regmap_init_mmio(dev, base, &regmap_config);
	if (IS_ERR(info->map)) {
		dev_err(dev, "Failed to create regmap\n");
		return PTR_ERR(info->map);
	}
	dev_set_drvdata(dev, info->map);
	info->dev = dev;

	/* Pinconf registers */
	if (info->desc->confops) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		base = devm_ioremap_resource(dev, res);
		if (IS_ERR(base))
			dev_dbg(dev, "Failed to ioremap config registers (no extended pinconf)\n");
		else
			info->pincfg = base;
	}

	ret = ocelot_pinctrl_register(pdev, info);
	if (ret)
		return ret;

	ret = ocelot_gpiochip_register(pdev, info);
	if (ret)
		return ret;

	dev_info(dev, "driver registered\n");

	return 0;
}

static struct platform_driver ocelot_pinctrl_driver = {
	.driver = {
		.name = "pinctrl-ocelot",
		.of_match_table = of_match_ptr(ocelot_pinctrl_of_match),
		.suppress_bind_attrs = true,
	},
	.probe = ocelot_pinctrl_probe,
};
builtin_platform_driver(ocelot_pinctrl_driver);
