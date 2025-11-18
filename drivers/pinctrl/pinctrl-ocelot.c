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
#include <linux/mfd/ocelot.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "core.h"
#include "pinconf.h"
#include "pinmux.h"

#define ocelot_clrsetbits(addr, clear, set) \
	writel((readl(addr) & ~(clear)) | (set), (addr))

enum {
	PINCONF_BIAS,
	PINCONF_SCHMITT,
	PINCONF_DRIVE_STRENGTH,
};

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
	FUNC_CAN0_a,
	FUNC_CAN0_b,
	FUNC_CAN1,
	FUNC_CLKMON,
	FUNC_NONE,
	FUNC_FAN,
	FUNC_FC,
	FUNC_FC0_a,
	FUNC_FC0_b,
	FUNC_FC0_c,
	FUNC_FC1_a,
	FUNC_FC1_b,
	FUNC_FC1_c,
	FUNC_FC2_a,
	FUNC_FC2_b,
	FUNC_FC3_a,
	FUNC_FC3_b,
	FUNC_FC3_c,
	FUNC_FC4_a,
	FUNC_FC4_b,
	FUNC_FC4_c,
	FUNC_FC_SHRD,
	FUNC_FC_SHRD0,
	FUNC_FC_SHRD1,
	FUNC_FC_SHRD2,
	FUNC_FC_SHRD3,
	FUNC_FC_SHRD4,
	FUNC_FC_SHRD5,
	FUNC_FC_SHRD6,
	FUNC_FC_SHRD7,
	FUNC_FC_SHRD8,
	FUNC_FC_SHRD9,
	FUNC_FC_SHRD10,
	FUNC_FC_SHRD11,
	FUNC_FC_SHRD12,
	FUNC_FC_SHRD13,
	FUNC_FC_SHRD14,
	FUNC_FC_SHRD15,
	FUNC_FC_SHRD16,
	FUNC_FC_SHRD17,
	FUNC_FC_SHRD18,
	FUNC_FC_SHRD19,
	FUNC_FC_SHRD20,
	FUNC_FUSA,
	FUNC_GPIO,
	FUNC_IB_TRG_a,
	FUNC_IB_TRG_b,
	FUNC_IB_TRG_c,
	FUNC_IRQ0,
	FUNC_IRQ_IN_a,
	FUNC_IRQ_IN_b,
	FUNC_IRQ_IN_c,
	FUNC_IRQ0_IN,
	FUNC_IRQ_OUT_a,
	FUNC_IRQ_OUT_b,
	FUNC_IRQ_OUT_c,
	FUNC_IRQ0_OUT,
	FUNC_IRQ1,
	FUNC_IRQ1_IN,
	FUNC_IRQ1_OUT,
	FUNC_IRQ3,
	FUNC_IRQ4,
	FUNC_EXT_IRQ,
	FUNC_MIIM,
	FUNC_MIIM_a,
	FUNC_MIIM_b,
	FUNC_MIIM_c,
	FUNC_MIIM_Sa,
	FUNC_MIIM_Sb,
	FUNC_MIIM_IRQ,
	FUNC_OB_TRG,
	FUNC_OB_TRG_a,
	FUNC_OB_TRG_b,
	FUNC_PHY_LED,
	FUNC_PCI_WAKE,
	FUNC_MD,
	FUNC_PCIE_PERST,
	FUNC_PTP0,
	FUNC_PTP1,
	FUNC_PTP2,
	FUNC_PTP3,
	FUNC_PTPSYNC_0,
	FUNC_PTPSYNC_1,
	FUNC_PTPSYNC_2,
	FUNC_PTPSYNC_3,
	FUNC_PTPSYNC_4,
	FUNC_PTPSYNC_5,
	FUNC_PTPSYNC_6,
	FUNC_PTPSYNC_7,
	FUNC_PWM,
	FUNC_PWM_a,
	FUNC_PWM_b,
	FUNC_QSPI1,
	FUNC_QSPI2,
	FUNC_R,
	FUNC_RECO_a,
	FUNC_RECO_b,
	FUNC_RECO_CLK,
	FUNC_SD,
	FUNC_SFP,
	FUNC_SFP_SD,
	FUNC_SG0,
	FUNC_SG1,
	FUNC_SG2,
	FUNC_SGPIO_a,
	FUNC_SGPIO_b,
	FUNC_SI,
	FUNC_SI2,
	FUNC_SYNCE,
	FUNC_TACHO,
	FUNC_TACHO_a,
	FUNC_TACHO_b,
	FUNC_TWI,
	FUNC_TWI2,
	FUNC_TWI3,
	FUNC_TWI_SCL_M,
	FUNC_TWI_SLC_GATE,
	FUNC_TWI_SLC_GATE_AD,
	FUNC_UART,
	FUNC_UART2,
	FUNC_UART3,
	FUNC_USB_H_a,
	FUNC_USB_H_b,
	FUNC_USB_H_c,
	FUNC_USB_S_a,
	FUNC_USB_S_b,
	FUNC_USB_S_c,
	FUNC_USB_POWER,
	FUNC_USB2PHY_RST,
	FUNC_USB_OVER_DETECT,
	FUNC_USB_ULPI,
	FUNC_PLL_STAT,
	FUNC_EMMC,
	FUNC_EMMC_SD,
	FUNC_REF_CLK,
	FUNC_RCVRD_CLK,
	FUNC_MAX
};

static const char *const ocelot_function_names[] = {
	[FUNC_CAN0_a]		= "can0_a",
	[FUNC_CAN0_b]		= "can0_b",
	[FUNC_CAN1]		= "can1",
	[FUNC_CLKMON]		= "clkmon",
	[FUNC_NONE]		= "none",
	[FUNC_FAN]		= "fan",
	[FUNC_FC]		= "fc",
	[FUNC_FC0_a]		= "fc0_a",
	[FUNC_FC0_b]		= "fc0_b",
	[FUNC_FC0_c]		= "fc0_c",
	[FUNC_FC1_a]		= "fc1_a",
	[FUNC_FC1_b]		= "fc1_b",
	[FUNC_FC1_c]		= "fc1_c",
	[FUNC_FC2_a]		= "fc2_a",
	[FUNC_FC2_b]		= "fc2_b",
	[FUNC_FC3_a]		= "fc3_a",
	[FUNC_FC3_b]		= "fc3_b",
	[FUNC_FC3_c]		= "fc3_c",
	[FUNC_FC4_a]		= "fc4_a",
	[FUNC_FC4_b]		= "fc4_b",
	[FUNC_FC4_c]		= "fc4_c",
	[FUNC_FC_SHRD]		= "fc_shrd",
	[FUNC_FC_SHRD0]		= "fc_shrd0",
	[FUNC_FC_SHRD1]		= "fc_shrd1",
	[FUNC_FC_SHRD2]		= "fc_shrd2",
	[FUNC_FC_SHRD3]		= "fc_shrd3",
	[FUNC_FC_SHRD4]		= "fc_shrd4",
	[FUNC_FC_SHRD5]		= "fc_shrd5",
	[FUNC_FC_SHRD6]		= "fc_shrd6",
	[FUNC_FC_SHRD7]		= "fc_shrd7",
	[FUNC_FC_SHRD8]		= "fc_shrd8",
	[FUNC_FC_SHRD9]		= "fc_shrd9",
	[FUNC_FC_SHRD10]	= "fc_shrd10",
	[FUNC_FC_SHRD11]	= "fc_shrd11",
	[FUNC_FC_SHRD12]	= "fc_shrd12",
	[FUNC_FC_SHRD13]	= "fc_shrd13",
	[FUNC_FC_SHRD14]	= "fc_shrd14",
	[FUNC_FC_SHRD15]	= "fc_shrd15",
	[FUNC_FC_SHRD16]	= "fc_shrd16",
	[FUNC_FC_SHRD17]	= "fc_shrd17",
	[FUNC_FC_SHRD18]	= "fc_shrd18",
	[FUNC_FC_SHRD19]	= "fc_shrd19",
	[FUNC_FC_SHRD20]	= "fc_shrd20",
	[FUNC_FUSA]		= "fusa",
	[FUNC_GPIO]		= "gpio",
	[FUNC_IB_TRG_a]		= "ib_trig_a",
	[FUNC_IB_TRG_b]		= "ib_trig_b",
	[FUNC_IB_TRG_c]		= "ib_trig_c",
	[FUNC_IRQ0]		= "irq0",
	[FUNC_IRQ_IN_a]		= "irq_in_a",
	[FUNC_IRQ_IN_b]		= "irq_in_b",
	[FUNC_IRQ_IN_c]		= "irq_in_c",
	[FUNC_IRQ0_IN]		= "irq0_in",
	[FUNC_IRQ_OUT_a]	= "irq_out_a",
	[FUNC_IRQ_OUT_b]	= "irq_out_b",
	[FUNC_IRQ_OUT_c]	= "irq_out_c",
	[FUNC_IRQ0_OUT]		= "irq0_out",
	[FUNC_IRQ1]		= "irq1",
	[FUNC_IRQ1_IN]		= "irq1_in",
	[FUNC_IRQ1_OUT]		= "irq1_out",
	[FUNC_IRQ3]		= "irq3",
	[FUNC_IRQ4]		= "irq4",
	[FUNC_EXT_IRQ]		= "ext_irq",
	[FUNC_MIIM]		= "miim",
	[FUNC_MIIM_a]		= "miim_a",
	[FUNC_MIIM_b]		= "miim_b",
	[FUNC_MIIM_c]		= "miim_c",
	[FUNC_MIIM_Sa]		= "miim_slave_a",
	[FUNC_MIIM_Sb]		= "miim_slave_b",
	[FUNC_MIIM_IRQ]		= "miim_irq",
	[FUNC_PHY_LED]		= "phy_led",
	[FUNC_PCI_WAKE]		= "pci_wake",
	[FUNC_PCIE_PERST]	= "pcie_perst",
	[FUNC_MD]		= "md",
	[FUNC_OB_TRG]		= "ob_trig",
	[FUNC_OB_TRG_a]		= "ob_trig_a",
	[FUNC_OB_TRG_b]		= "ob_trig_b",
	[FUNC_PTP0]		= "ptp0",
	[FUNC_PTP1]		= "ptp1",
	[FUNC_PTP2]		= "ptp2",
	[FUNC_PTP3]		= "ptp3",
	[FUNC_PTPSYNC_0]	= "ptpsync_0",
	[FUNC_PTPSYNC_1]	= "ptpsync_1",
	[FUNC_PTPSYNC_2]	= "ptpsync_2",
	[FUNC_PTPSYNC_3]	= "ptpsync_3",
	[FUNC_PTPSYNC_4]	= "ptpsync_4",
	[FUNC_PTPSYNC_5]	= "ptpsync_5",
	[FUNC_PTPSYNC_6]	= "ptpsync_6",
	[FUNC_PTPSYNC_7]	= "ptpsync_7",
	[FUNC_PWM]		= "pwm",
	[FUNC_PWM_a]		= "pwm_a",
	[FUNC_PWM_b]		= "pwm_b",
	[FUNC_QSPI1]		= "qspi1",
	[FUNC_QSPI2]		= "qspi2",
	[FUNC_R]		= "reserved",
	[FUNC_RECO_a]		= "reco_a",
	[FUNC_RECO_b]		= "reco_b",
	[FUNC_RECO_CLK]		= "reco_clk",
	[FUNC_SD]		= "sd",
	[FUNC_SFP]		= "sfp",
	[FUNC_SFP_SD]		= "sfp_sd",
	[FUNC_SG0]		= "sg0",
	[FUNC_SG1]		= "sg1",
	[FUNC_SG2]		= "sg2",
	[FUNC_SGPIO_a]		= "sgpio_a",
	[FUNC_SGPIO_b]		= "sgpio_b",
	[FUNC_SI]		= "si",
	[FUNC_SI2]		= "si2",
	[FUNC_SYNCE]		= "synce",
	[FUNC_TACHO]		= "tacho",
	[FUNC_TACHO_a]		= "tacho_a",
	[FUNC_TACHO_b]		= "tacho_b",
	[FUNC_TWI]		= "twi",
	[FUNC_TWI2]		= "twi2",
	[FUNC_TWI3]		= "twi3",
	[FUNC_TWI_SCL_M]	= "twi_scl_m",
	[FUNC_TWI_SLC_GATE]	= "twi_slc_gate",
	[FUNC_TWI_SLC_GATE_AD]	= "twi_slc_gate_ad",
	[FUNC_USB_H_a]		= "usb_host_a",
	[FUNC_USB_H_b]		= "usb_host_b",
	[FUNC_USB_H_c]		= "usb_host_c",
	[FUNC_USB_S_a]		= "usb_slave_a",
	[FUNC_USB_S_b]		= "usb_slave_b",
	[FUNC_USB_S_c]		= "usb_slave_c",
	[FUNC_USB_POWER]	= "usb_power",
	[FUNC_USB2PHY_RST]	= "usb2phy_rst",
	[FUNC_USB_OVER_DETECT]	= "usb_over_detect",
	[FUNC_USB_ULPI]		= "usb_ulpi",
	[FUNC_UART]		= "uart",
	[FUNC_UART2]		= "uart2",
	[FUNC_UART3]		= "uart3",
	[FUNC_PLL_STAT]		= "pll_stat",
	[FUNC_EMMC]		= "emmc",
	[FUNC_EMMC_SD]		= "emmc_sd",
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
	unsigned char a_functions[OCELOT_FUNC_PER_PIN];	/* Additional functions */
};

struct ocelot_pincfg_data {
	u8 pd_bit;
	u8 pu_bit;
	u8 drive_bits;
	u8 schmitt_bit;
};

struct ocelot_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctl;
	struct gpio_chip gpio_chip;
	struct regmap *map;
	struct regmap *pincfg;
	struct pinctrl_desc *desc;
	const struct ocelot_pincfg_data *pincfg_data;
	struct ocelot_pmx_func func[FUNC_MAX];
	u8 stride;
	struct workqueue_struct *wq;
};

struct ocelot_match_data {
	struct pinctrl_desc desc;
	struct ocelot_pincfg_data pincfg_data;
};

struct ocelot_irq_work {
	struct work_struct irq_work;
	struct irq_desc *irq_desc;
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

#define SERVALT_P(p, f0, f1, f2)					\
static struct ocelot_pin_caps servalt_pin_##p = {			\
	.pin = p,							\
	.functions = {							\
		FUNC_GPIO, FUNC_##f0, FUNC_##f1, FUNC_##f2		\
	},								\
}

SERVALT_P(0,  SG0,        NONE,      NONE);
SERVALT_P(1,  SG0,        NONE,      NONE);
SERVALT_P(2,  SG0,        NONE,      NONE);
SERVALT_P(3,  SG0,        NONE,      NONE);
SERVALT_P(4,  IRQ0_IN,    IRQ0_OUT,  TWI_SCL_M);
SERVALT_P(5,  IRQ1_IN,    IRQ1_OUT,  TWI_SCL_M);
SERVALT_P(6,  UART,       NONE,      NONE);
SERVALT_P(7,  UART,       NONE,      NONE);
SERVALT_P(8,  SI,         SFP,       TWI_SCL_M);
SERVALT_P(9,  PCI_WAKE,   SFP,       SI);
SERVALT_P(10, PTP0,       SFP,       TWI_SCL_M);
SERVALT_P(11, PTP1,       SFP,       TWI_SCL_M);
SERVALT_P(12, REF_CLK,    SFP,       TWI_SCL_M);
SERVALT_P(13, REF_CLK,    SFP,       TWI_SCL_M);
SERVALT_P(14, REF_CLK,    IRQ0_OUT,  SI);
SERVALT_P(15, REF_CLK,    IRQ1_OUT,  SI);
SERVALT_P(16, TACHO,      SFP,       SI);
SERVALT_P(17, PWM,        NONE,      TWI_SCL_M);
SERVALT_P(18, PTP2,       SFP,       SI);
SERVALT_P(19, PTP3,       SFP,       SI);
SERVALT_P(20, UART2,      SFP,       SI);
SERVALT_P(21, UART2,      NONE,      NONE);
SERVALT_P(22, MIIM,       SFP,       TWI2);
SERVALT_P(23, MIIM,       SFP,       TWI2);
SERVALT_P(24, TWI,        NONE,      NONE);
SERVALT_P(25, TWI,        SFP,       TWI_SCL_M);
SERVALT_P(26, TWI_SCL_M,  SFP,       SI);
SERVALT_P(27, TWI_SCL_M,  SFP,       SI);
SERVALT_P(28, TWI_SCL_M,  SFP,       SI);
SERVALT_P(29, TWI_SCL_M,  NONE,      NONE);
SERVALT_P(30, TWI_SCL_M,  NONE,      NONE);
SERVALT_P(31, TWI_SCL_M,  NONE,      NONE);
SERVALT_P(32, TWI_SCL_M,  NONE,      NONE);
SERVALT_P(33, RCVRD_CLK,  NONE,      NONE);
SERVALT_P(34, RCVRD_CLK,  NONE,      NONE);
SERVALT_P(35, RCVRD_CLK,  NONE,      NONE);
SERVALT_P(36, RCVRD_CLK,  NONE,      NONE);

#define SERVALT_PIN(n) {					\
	.number = n,						\
	.name = "GPIO_"#n,					\
	.drv_data = &servalt_pin_##n				\
}

static const struct pinctrl_pin_desc servalt_pins[] = {
	SERVALT_PIN(0),
	SERVALT_PIN(1),
	SERVALT_PIN(2),
	SERVALT_PIN(3),
	SERVALT_PIN(4),
	SERVALT_PIN(5),
	SERVALT_PIN(6),
	SERVALT_PIN(7),
	SERVALT_PIN(8),
	SERVALT_PIN(9),
	SERVALT_PIN(10),
	SERVALT_PIN(11),
	SERVALT_PIN(12),
	SERVALT_PIN(13),
	SERVALT_PIN(14),
	SERVALT_PIN(15),
	SERVALT_PIN(16),
	SERVALT_PIN(17),
	SERVALT_PIN(18),
	SERVALT_PIN(19),
	SERVALT_PIN(20),
	SERVALT_PIN(21),
	SERVALT_PIN(22),
	SERVALT_PIN(23),
	SERVALT_PIN(24),
	SERVALT_PIN(25),
	SERVALT_PIN(26),
	SERVALT_PIN(27),
	SERVALT_PIN(28),
	SERVALT_PIN(29),
	SERVALT_PIN(30),
	SERVALT_PIN(31),
	SERVALT_PIN(32),
	SERVALT_PIN(33),
	SERVALT_PIN(34),
	SERVALT_PIN(35),
	SERVALT_PIN(36),
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

#define LAN966X_P(p, f0, f1, f2, f3, f4, f5, f6, f7)           \
static struct ocelot_pin_caps lan966x_pin_##p = {              \
	.pin = p,                                              \
	.functions = {                                         \
		FUNC_##f0, FUNC_##f1, FUNC_##f2,               \
		FUNC_##f3                                      \
	},                                                     \
	.a_functions = {                                       \
		FUNC_##f4, FUNC_##f5, FUNC_##f6,               \
		FUNC_##f7                                      \
	},                                                     \
}

/* Pinmuxing table taken from data sheet */
/*        Pin   FUNC0    FUNC1     FUNC2      FUNC3     FUNC4     FUNC5      FUNC6    FUNC7 */
LAN966X_P(0,    GPIO,    NONE,     NONE,      NONE,     NONE,     NONE,      NONE,        R);
LAN966X_P(1,    GPIO,    NONE,     NONE,      NONE,     NONE,     NONE,      NONE,        R);
LAN966X_P(2,    GPIO,    NONE,     NONE,      NONE,     NONE,     NONE,      NONE,        R);
LAN966X_P(3,    GPIO,    NONE,     NONE,      NONE,     NONE,     NONE,      NONE,        R);
LAN966X_P(4,    GPIO,    NONE,     NONE,      NONE,     NONE,     NONE,      NONE,        R);
LAN966X_P(5,    GPIO,    NONE,     NONE,      NONE,     NONE,     NONE,      NONE,        R);
LAN966X_P(6,    GPIO,    NONE,     NONE,      NONE,     NONE,     NONE,      NONE,        R);
LAN966X_P(7,    GPIO,    NONE,     NONE,      NONE,     NONE,     NONE,      NONE,        R);
LAN966X_P(8,    GPIO,   FC0_a,  USB_H_b,      NONE,  USB_S_b,     NONE,      NONE,        R);
LAN966X_P(9,    GPIO,   FC0_a,  USB_H_b,      NONE,     NONE,     NONE,      NONE,        R);
LAN966X_P(10,   GPIO,   FC0_a,     NONE,      NONE,     NONE,     NONE,      NONE,        R);
LAN966X_P(11,   GPIO,   FC1_a,     NONE,      NONE,     NONE,     NONE,      NONE,        R);
LAN966X_P(12,   GPIO,   FC1_a,     NONE,      NONE,     NONE,     NONE,      NONE,        R);
LAN966X_P(13,   GPIO,   FC1_a,     NONE,      NONE,     NONE,     NONE,      NONE,        R);
LAN966X_P(14,   GPIO,   FC2_a,     NONE,      NONE,     NONE,     NONE,      NONE,        R);
LAN966X_P(15,   GPIO,   FC2_a,     NONE,      NONE,     NONE,     NONE,      NONE,        R);
LAN966X_P(16,   GPIO,   FC2_a, IB_TRG_a,      NONE, OB_TRG_a, IRQ_IN_c, IRQ_OUT_c,        R);
LAN966X_P(17,   GPIO,   FC3_a, IB_TRG_a,      NONE, OB_TRG_a, IRQ_IN_c, IRQ_OUT_c,        R);
LAN966X_P(18,   GPIO,   FC3_a, IB_TRG_a,      NONE, OB_TRG_a, IRQ_IN_c, IRQ_OUT_c,        R);
LAN966X_P(19,   GPIO,   FC3_a, IB_TRG_a,      NONE, OB_TRG_a, IRQ_IN_c, IRQ_OUT_c,        R);
LAN966X_P(20,   GPIO,   FC4_a, IB_TRG_a,      NONE, OB_TRG_a, IRQ_IN_c,      NONE,        R);
LAN966X_P(21,   GPIO,   FC4_a,     NONE,      NONE, OB_TRG_a,     NONE,      NONE,        R);
LAN966X_P(22,   GPIO,   FC4_a,     NONE,      NONE, OB_TRG_a,     NONE,      NONE,        R);
LAN966X_P(23,   GPIO,    NONE,     NONE,      NONE, OB_TRG_a,     NONE,      NONE,        R);
LAN966X_P(24,   GPIO,   FC0_b, IB_TRG_a,   USB_H_c, OB_TRG_a, IRQ_IN_c,   TACHO_a,        R);
LAN966X_P(25,   GPIO,   FC0_b, IB_TRG_a,   USB_H_c, OB_TRG_a, IRQ_OUT_c,   SFP_SD,        R);
LAN966X_P(26,   GPIO,   FC0_b, IB_TRG_a,   USB_S_c, OB_TRG_a,   CAN0_a,    SFP_SD,        R);
LAN966X_P(27,   GPIO,    NONE,     NONE,      NONE, OB_TRG_a,   CAN0_a,     PWM_a,        R);
LAN966X_P(28,   GPIO,  MIIM_a,     NONE,      NONE, OB_TRG_a, IRQ_OUT_c,   SFP_SD,        R);
LAN966X_P(29,   GPIO,  MIIM_a,     NONE,      NONE, OB_TRG_a,     NONE,      NONE,        R);
LAN966X_P(30,   GPIO,   FC3_c,     CAN1,    CLKMON,   OB_TRG,   RECO_b,      NONE,        R);
LAN966X_P(31,   GPIO,   FC3_c,     CAN1,    CLKMON,   OB_TRG,   RECO_b,      NONE,        R);
LAN966X_P(32,   GPIO,   FC3_c,     NONE,   SGPIO_a,     NONE,  MIIM_Sa,      NONE,        R);
LAN966X_P(33,   GPIO,   FC1_b,     NONE,   SGPIO_a,     NONE,  MIIM_Sa,    MIIM_b,        R);
LAN966X_P(34,   GPIO,   FC1_b,     NONE,   SGPIO_a,     NONE,  MIIM_Sa,    MIIM_b,        R);
LAN966X_P(35,   GPIO,   FC1_b,  PTPSYNC_0, SGPIO_a,   CAN0_b,     NONE,      NONE,        R);
LAN966X_P(36,   GPIO,    NONE,  PTPSYNC_1,    NONE,   CAN0_b,     NONE,      NONE,        R);
LAN966X_P(37,   GPIO, FC_SHRD0, PTPSYNC_2, TWI_SLC_GATE_AD, NONE, NONE,      NONE,        R);
LAN966X_P(38,   GPIO,    NONE,  PTPSYNC_3,    NONE,     NONE,     NONE,      NONE,        R);
LAN966X_P(39,   GPIO,    NONE,  PTPSYNC_4,    NONE,     NONE,     NONE,      NONE,        R);
LAN966X_P(40,   GPIO, FC_SHRD1, PTPSYNC_5,    NONE,     NONE,     NONE,      NONE,        R);
LAN966X_P(41,   GPIO, FC_SHRD2, PTPSYNC_6, TWI_SLC_GATE_AD, NONE, NONE,      NONE,        R);
LAN966X_P(42,   GPIO, FC_SHRD3, PTPSYNC_7, TWI_SLC_GATE_AD, NONE, NONE,      NONE,        R);
LAN966X_P(43,   GPIO,   FC2_b,   OB_TRG_b, IB_TRG_b, IRQ_OUT_a,  RECO_a,  IRQ_IN_a,       R);
LAN966X_P(44,   GPIO,   FC2_b,   OB_TRG_b, IB_TRG_b, IRQ_OUT_a,  RECO_a,  IRQ_IN_a,       R);
LAN966X_P(45,   GPIO,   FC2_b,   OB_TRG_b, IB_TRG_b, IRQ_OUT_a,    NONE,  IRQ_IN_a,       R);
LAN966X_P(46,   GPIO,   FC1_c,   OB_TRG_b, IB_TRG_b, IRQ_OUT_a, FC_SHRD4, IRQ_IN_a,       R);
LAN966X_P(47,   GPIO,   FC1_c,   OB_TRG_b, IB_TRG_b, IRQ_OUT_a, FC_SHRD5, IRQ_IN_a,       R);
LAN966X_P(48,   GPIO,   FC1_c,   OB_TRG_b, IB_TRG_b, IRQ_OUT_a, FC_SHRD6, IRQ_IN_a,       R);
LAN966X_P(49,   GPIO, FC_SHRD7,  OB_TRG_b, IB_TRG_b, IRQ_OUT_a, TWI_SLC_GATE, IRQ_IN_a,   R);
LAN966X_P(50,   GPIO, FC_SHRD16, OB_TRG_b, IB_TRG_b, IRQ_OUT_a, TWI_SLC_GATE, NONE,       R);
LAN966X_P(51,   GPIO,   FC3_b,   OB_TRG_b, IB_TRG_c, IRQ_OUT_b,   PWM_b,  IRQ_IN_b,       R);
LAN966X_P(52,   GPIO,   FC3_b,   OB_TRG_b, IB_TRG_c, IRQ_OUT_b, TACHO_b,  IRQ_IN_b,       R);
LAN966X_P(53,   GPIO,   FC3_b,   OB_TRG_b, IB_TRG_c, IRQ_OUT_b,    NONE,  IRQ_IN_b,       R);
LAN966X_P(54,   GPIO, FC_SHRD8,  OB_TRG_b, IB_TRG_c, IRQ_OUT_b, TWI_SLC_GATE, IRQ_IN_b,   R);
LAN966X_P(55,   GPIO, FC_SHRD9,  OB_TRG_b, IB_TRG_c, IRQ_OUT_b, TWI_SLC_GATE, IRQ_IN_b,   R);
LAN966X_P(56,   GPIO,   FC4_b,   OB_TRG_b, IB_TRG_c, IRQ_OUT_b, FC_SHRD10,    IRQ_IN_b,   R);
LAN966X_P(57,   GPIO,   FC4_b, TWI_SLC_GATE, IB_TRG_c, IRQ_OUT_b, FC_SHRD11, IRQ_IN_b,    R);
LAN966X_P(58,   GPIO,   FC4_b, TWI_SLC_GATE, IB_TRG_c, IRQ_OUT_b, FC_SHRD12, IRQ_IN_b,    R);
LAN966X_P(59,   GPIO,   QSPI1,   MIIM_c,      NONE,     NONE,  MIIM_Sb,      NONE,        R);
LAN966X_P(60,   GPIO,   QSPI1,   MIIM_c,      NONE,     NONE,  MIIM_Sb,      NONE,        R);
LAN966X_P(61,   GPIO,   QSPI1,     NONE,   SGPIO_b,    FC0_c,  MIIM_Sb,      NONE,        R);
LAN966X_P(62,   GPIO,   QSPI1, FC_SHRD13,  SGPIO_b,    FC0_c, TWI_SLC_GATE,  SFP_SD,      R);
LAN966X_P(63,   GPIO,   QSPI1, FC_SHRD14,  SGPIO_b,    FC0_c, TWI_SLC_GATE,  SFP_SD,      R);
LAN966X_P(64,   GPIO,   QSPI1,    FC4_c,   SGPIO_b, FC_SHRD15, TWI_SLC_GATE, SFP_SD,      R);
LAN966X_P(65,   GPIO, USB_H_a,    FC4_c,      NONE, IRQ_OUT_c, TWI_SLC_GATE_AD, NONE,     R);
LAN966X_P(66,   GPIO, USB_H_a,    FC4_c,   USB_S_a, IRQ_OUT_c, IRQ_IN_c,     NONE,        R);
LAN966X_P(67,   GPIO, EMMC_SD,     NONE,     QSPI2,     NONE,     NONE,      NONE,        R);
LAN966X_P(68,   GPIO, EMMC_SD,     NONE,     QSPI2,     NONE,     NONE,      NONE,        R);
LAN966X_P(69,   GPIO, EMMC_SD,     NONE,     QSPI2,     NONE,     NONE,      NONE,        R);
LAN966X_P(70,   GPIO, EMMC_SD,     NONE,     QSPI2,     NONE,     NONE,      NONE,        R);
LAN966X_P(71,   GPIO, EMMC_SD,     NONE,     QSPI2,     NONE,     NONE,      NONE,        R);
LAN966X_P(72,   GPIO, EMMC_SD,     NONE,     QSPI2,     NONE,     NONE,      NONE,        R);
LAN966X_P(73,   GPIO,    EMMC,     NONE,      NONE,       SD,     NONE,      NONE,        R);
LAN966X_P(74,   GPIO,    EMMC,     NONE, FC_SHRD17,       SD, TWI_SLC_GATE,  NONE,        R);
LAN966X_P(75,   GPIO,    EMMC,     NONE, FC_SHRD18,       SD, TWI_SLC_GATE,  NONE,        R);
LAN966X_P(76,   GPIO,    EMMC,     NONE, FC_SHRD19,       SD, TWI_SLC_GATE,  NONE,        R);
LAN966X_P(77,   GPIO, EMMC_SD,     NONE, FC_SHRD20,     NONE, TWI_SLC_GATE,  NONE,        R);

#define LAN966X_PIN(n) {                                       \
	.number = n,                                           \
	.name = "GPIO_"#n,                                     \
	.drv_data = &lan966x_pin_##n                           \
}

static const struct pinctrl_pin_desc lan966x_pins[] = {
	LAN966X_PIN(0),
	LAN966X_PIN(1),
	LAN966X_PIN(2),
	LAN966X_PIN(3),
	LAN966X_PIN(4),
	LAN966X_PIN(5),
	LAN966X_PIN(6),
	LAN966X_PIN(7),
	LAN966X_PIN(8),
	LAN966X_PIN(9),
	LAN966X_PIN(10),
	LAN966X_PIN(11),
	LAN966X_PIN(12),
	LAN966X_PIN(13),
	LAN966X_PIN(14),
	LAN966X_PIN(15),
	LAN966X_PIN(16),
	LAN966X_PIN(17),
	LAN966X_PIN(18),
	LAN966X_PIN(19),
	LAN966X_PIN(20),
	LAN966X_PIN(21),
	LAN966X_PIN(22),
	LAN966X_PIN(23),
	LAN966X_PIN(24),
	LAN966X_PIN(25),
	LAN966X_PIN(26),
	LAN966X_PIN(27),
	LAN966X_PIN(28),
	LAN966X_PIN(29),
	LAN966X_PIN(30),
	LAN966X_PIN(31),
	LAN966X_PIN(32),
	LAN966X_PIN(33),
	LAN966X_PIN(34),
	LAN966X_PIN(35),
	LAN966X_PIN(36),
	LAN966X_PIN(37),
	LAN966X_PIN(38),
	LAN966X_PIN(39),
	LAN966X_PIN(40),
	LAN966X_PIN(41),
	LAN966X_PIN(42),
	LAN966X_PIN(43),
	LAN966X_PIN(44),
	LAN966X_PIN(45),
	LAN966X_PIN(46),
	LAN966X_PIN(47),
	LAN966X_PIN(48),
	LAN966X_PIN(49),
	LAN966X_PIN(50),
	LAN966X_PIN(51),
	LAN966X_PIN(52),
	LAN966X_PIN(53),
	LAN966X_PIN(54),
	LAN966X_PIN(55),
	LAN966X_PIN(56),
	LAN966X_PIN(57),
	LAN966X_PIN(58),
	LAN966X_PIN(59),
	LAN966X_PIN(60),
	LAN966X_PIN(61),
	LAN966X_PIN(62),
	LAN966X_PIN(63),
	LAN966X_PIN(64),
	LAN966X_PIN(65),
	LAN966X_PIN(66),
	LAN966X_PIN(67),
	LAN966X_PIN(68),
	LAN966X_PIN(69),
	LAN966X_PIN(70),
	LAN966X_PIN(71),
	LAN966X_PIN(72),
	LAN966X_PIN(73),
	LAN966X_PIN(74),
	LAN966X_PIN(75),
	LAN966X_PIN(76),
	LAN966X_PIN(77),
};

#define LAN969X_P(p, f0, f1, f2, f3, f4, f5, f6, f7)           \
static struct ocelot_pin_caps lan969x_pin_##p = {              \
	.pin = p,                                              \
	.functions = {                                         \
		FUNC_##f0, FUNC_##f1, FUNC_##f2,               \
		FUNC_##f3                                      \
	},                                                     \
	.a_functions = {                                       \
		FUNC_##f4, FUNC_##f5, FUNC_##f6,               \
		FUNC_##f7                                      \
	},                                                     \
}

/* Pinmuxing table taken from data sheet */
/*        Pin   FUNC0      FUNC1   FUNC2         FUNC3                  FUNC4     FUNC5      FUNC6        FUNC7 */
LAN969X_P(0,    GPIO,      IRQ0,   FC_SHRD,      PCIE_PERST,            NONE,     NONE,      NONE,        R);
LAN969X_P(1,    GPIO,      IRQ1,   FC_SHRD,       USB_POWER,            NONE,     NONE,      NONE,        R);
LAN969X_P(2,    GPIO,        FC,      NONE,            NONE,            NONE,     NONE,      NONE,        R);
LAN969X_P(3,    GPIO,        FC,      NONE,            NONE,            NONE,     NONE,      NONE,        R);
LAN969X_P(4,    GPIO,        FC,      NONE,            NONE,            NONE,     NONE,      NONE,        R);
LAN969X_P(5,    GPIO,   SGPIO_a,      NONE,          CLKMON,            NONE,     NONE,      NONE,        R);
LAN969X_P(6,    GPIO,   SGPIO_a,      NONE,          CLKMON,            NONE,     NONE,      NONE,        R);
LAN969X_P(7,    GPIO,   SGPIO_a,      NONE,          CLKMON,            NONE,     NONE,      NONE,        R);
LAN969X_P(8,    GPIO,   SGPIO_a,      NONE,          CLKMON,            NONE,     NONE,      NONE,        R);
LAN969X_P(9,    GPIO,      MIIM,   MIIM_Sa,          CLKMON,            NONE,     NONE,      NONE,        R);
LAN969X_P(10,   GPIO,      MIIM,   MIIM_Sa,          CLKMON,            NONE,     NONE,      NONE,        R);
LAN969X_P(11,   GPIO,  MIIM_IRQ,   MIIM_Sa,          CLKMON,            NONE,     NONE,      NONE,        R);
LAN969X_P(12,   GPIO,      IRQ3,   FC_SHRD,     USB2PHY_RST,            NONE,     NONE,      NONE,        R);
LAN969X_P(13,   GPIO,      IRQ4,   FC_SHRD, USB_OVER_DETECT,            NONE,     NONE,      NONE,        R);
LAN969X_P(14,   GPIO,   EMMC_SD,     QSPI1,              FC,            NONE,     NONE,      NONE,        R);
LAN969X_P(15,   GPIO,   EMMC_SD,     QSPI1,              FC,            NONE,     NONE,      NONE,        R);
LAN969X_P(16,   GPIO,   EMMC_SD,     QSPI1,              FC,            NONE,     NONE,      NONE,        R);
LAN969X_P(17,   GPIO,   EMMC_SD,     QSPI1,       PTPSYNC_0,       USB_POWER,     NONE,      NONE,        R);
LAN969X_P(18,   GPIO,   EMMC_SD,     QSPI1,       PTPSYNC_1,     USB2PHY_RST,     NONE,      NONE,        R);
LAN969X_P(19,   GPIO,   EMMC_SD,     QSPI1,       PTPSYNC_2, USB_OVER_DETECT,     NONE,      NONE,        R);
LAN969X_P(20,   GPIO,   EMMC_SD,      NONE,         FC_SHRD,            NONE,     NONE,      NONE,        R);
LAN969X_P(21,   GPIO,   EMMC_SD,      NONE,         FC_SHRD,            NONE,     NONE,      NONE,        R);
LAN969X_P(22,   GPIO,   EMMC_SD,      NONE,         FC_SHRD,            NONE,     NONE,      NONE,        R);
LAN969X_P(23,   GPIO,   EMMC_SD,      NONE,         FC_SHRD,            NONE,     NONE,      NONE,        R);
LAN969X_P(24,   GPIO,   EMMC_SD,      NONE,            NONE,            NONE,     NONE,      NONE,        R);
LAN969X_P(25,   GPIO,       FAN,      FUSA,          CAN0_a,           QSPI1,     NONE,      NONE,        R);
LAN969X_P(26,   GPIO,       FAN,      FUSA,          CAN0_a,           QSPI1,     NONE,      NONE,        R);
LAN969X_P(27,   GPIO,     SYNCE,        FC,            MIIM,           QSPI1,     NONE,      NONE,        R);
LAN969X_P(28,   GPIO,     SYNCE,        FC,            MIIM,           QSPI1,     NONE,      NONE,        R);
LAN969X_P(29,   GPIO,     SYNCE,        FC,        MIIM_IRQ,           QSPI1,     NONE,      NONE,        R);
LAN969X_P(30,   GPIO, PTPSYNC_0,  USB_ULPI,         FC_SHRD,           QSPI1,     NONE,      NONE,        R);
LAN969X_P(31,   GPIO, PTPSYNC_1,  USB_ULPI,         FC_SHRD,            NONE,     NONE,      NONE,        R);
LAN969X_P(32,   GPIO, PTPSYNC_2,  USB_ULPI,         FC_SHRD,            NONE,     NONE,      NONE,        R);
LAN969X_P(33,   GPIO,        SD,  USB_ULPI,         FC_SHRD,            NONE,     NONE,      NONE,        R);
LAN969X_P(34,   GPIO,        SD,  USB_ULPI,            CAN1,         FC_SHRD,     NONE,      NONE,        R);
LAN969X_P(35,   GPIO,        SD,  USB_ULPI,            CAN1,         FC_SHRD,     NONE,      NONE,        R);
LAN969X_P(36,   GPIO,        SD,  USB_ULPI,      PCIE_PERST,         FC_SHRD,     NONE,      NONE,        R);
LAN969X_P(37,   GPIO,        SD,  USB_ULPI,          CAN0_b,            NONE,     NONE,      NONE,        R);
LAN969X_P(38,   GPIO,        SD,  USB_ULPI,          CAN0_b,            NONE,     NONE,      NONE,        R);
LAN969X_P(39,   GPIO,        SD,  USB_ULPI,            MIIM,            NONE,     NONE,      NONE,        R);
LAN969X_P(40,   GPIO,        SD,  USB_ULPI,            MIIM,            NONE,     NONE,      NONE,        R);
LAN969X_P(41,   GPIO,        SD,  USB_ULPI,        MIIM_IRQ,            NONE,     NONE,      NONE,        R);
LAN969X_P(42,   GPIO, PTPSYNC_3,      CAN1,            NONE,            NONE,     NONE,      NONE,        R);
LAN969X_P(43,   GPIO, PTPSYNC_4,      CAN1,            NONE,            NONE,     NONE,      NONE,        R);
LAN969X_P(44,   GPIO, PTPSYNC_5,    SFP_SD,            NONE,            NONE,     NONE,      NONE,        R);
LAN969X_P(45,   GPIO, PTPSYNC_6,    SFP_SD,            NONE,            NONE,     NONE,      NONE,        R);
LAN969X_P(46,   GPIO, PTPSYNC_7,    SFP_SD,            NONE,            NONE,     NONE,      NONE,        R);
LAN969X_P(47,   GPIO,      NONE,    SFP_SD,            NONE,            NONE,     NONE,      NONE,        R);
LAN969X_P(48,   GPIO,      NONE,    SFP_SD,            NONE,            NONE,     NONE,      NONE,        R);
LAN969X_P(49,   GPIO,      NONE,    SFP_SD,            NONE,            NONE,     NONE,      NONE,        R);
LAN969X_P(50,   GPIO,      NONE,    SFP_SD,            NONE,            NONE,     NONE,      NONE,        R);
LAN969X_P(51,   GPIO,      NONE,    SFP_SD,            NONE,            NONE,     NONE,      NONE,        R);
LAN969X_P(52,   GPIO,       FAN,    SFP_SD,            NONE,            NONE,     NONE,      NONE,        R);
LAN969X_P(53,   GPIO,       FAN,    SFP_SD,            NONE,            NONE,     NONE,      NONE,        R);
LAN969X_P(54,   GPIO,     SYNCE,        FC,            NONE,            NONE,     NONE,      NONE,        R);
LAN969X_P(55,   GPIO,     SYNCE,        FC,            NONE,            NONE,     NONE,      NONE,        R);
LAN969X_P(56,   GPIO,     SYNCE,        FC,            NONE,            NONE,     NONE,      NONE,        R);
LAN969X_P(57,   GPIO,    SFP_SD,   FC_SHRD,             TWI,       PTPSYNC_3,     NONE,      NONE,        R);
LAN969X_P(58,   GPIO,    SFP_SD,   FC_SHRD,             TWI,       PTPSYNC_4,     NONE,      NONE,        R);
LAN969X_P(59,   GPIO,    SFP_SD,   FC_SHRD,             TWI,       PTPSYNC_5,     NONE,      NONE,        R);
LAN969X_P(60,   GPIO,    SFP_SD,   FC_SHRD,             TWI,       PTPSYNC_6,     NONE,      NONE,        R);
LAN969X_P(61,   GPIO,      MIIM,   FC_SHRD,             TWI,            NONE,     NONE,      NONE,        R);
LAN969X_P(62,   GPIO,      MIIM,   FC_SHRD,             TWI,            NONE,     NONE,      NONE,        R);
LAN969X_P(63,   GPIO,  MIIM_IRQ,   FC_SHRD,             TWI,            NONE,     NONE,      NONE,        R);
LAN969X_P(64,   GPIO,        FC,   FC_SHRD,             TWI,            NONE,     NONE,      NONE,        R);
LAN969X_P(65,   GPIO,        FC,   FC_SHRD,             TWI,            NONE,     NONE,      NONE,        R);
LAN969X_P(66,   GPIO,        FC,   FC_SHRD,             TWI,            NONE,     NONE,      NONE,        R);

#define LAN969X_PIN(n) {                                       \
	.number = n,                                           \
	.name = "GPIO_"#n,                                     \
	.drv_data = &lan969x_pin_##n                           \
}

static const struct pinctrl_pin_desc lan969x_pins[] = {
	LAN969X_PIN(0),
	LAN969X_PIN(1),
	LAN969X_PIN(2),
	LAN969X_PIN(3),
	LAN969X_PIN(4),
	LAN969X_PIN(5),
	LAN969X_PIN(6),
	LAN969X_PIN(7),
	LAN969X_PIN(8),
	LAN969X_PIN(9),
	LAN969X_PIN(10),
	LAN969X_PIN(11),
	LAN969X_PIN(12),
	LAN969X_PIN(13),
	LAN969X_PIN(14),
	LAN969X_PIN(15),
	LAN969X_PIN(16),
	LAN969X_PIN(17),
	LAN969X_PIN(18),
	LAN969X_PIN(19),
	LAN969X_PIN(20),
	LAN969X_PIN(21),
	LAN969X_PIN(22),
	LAN969X_PIN(23),
	LAN969X_PIN(24),
	LAN969X_PIN(25),
	LAN969X_PIN(26),
	LAN969X_PIN(27),
	LAN969X_PIN(28),
	LAN969X_PIN(29),
	LAN969X_PIN(30),
	LAN969X_PIN(31),
	LAN969X_PIN(32),
	LAN969X_PIN(33),
	LAN969X_PIN(34),
	LAN969X_PIN(35),
	LAN969X_PIN(36),
	LAN969X_PIN(37),
	LAN969X_PIN(38),
	LAN969X_PIN(39),
	LAN969X_PIN(40),
	LAN969X_PIN(41),
	LAN969X_PIN(42),
	LAN969X_PIN(43),
	LAN969X_PIN(44),
	LAN969X_PIN(45),
	LAN969X_PIN(46),
	LAN969X_PIN(47),
	LAN969X_PIN(48),
	LAN969X_PIN(49),
	LAN969X_PIN(50),
	LAN969X_PIN(51),
	LAN969X_PIN(52),
	LAN969X_PIN(53),
	LAN969X_PIN(54),
	LAN969X_PIN(55),
	LAN969X_PIN(56),
	LAN969X_PIN(57),
	LAN969X_PIN(58),
	LAN969X_PIN(59),
	LAN969X_PIN(60),
	LAN969X_PIN(61),
	LAN969X_PIN(62),
	LAN969X_PIN(63),
	LAN969X_PIN(64),
	LAN969X_PIN(65),
	LAN969X_PIN(66),
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

		if (function == p->a_functions[i])
			return i + OCELOT_FUNC_PER_PIN;
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
			   BIT(p), (f >> 1) << p);

	return 0;
}

static int lan966x_pinmux_set_mux(struct pinctrl_dev *pctldev,
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
	 * f is encoded on three bits.
	 * bit 0 of f goes in BIT(pin) of ALT[0], bit 1 of f goes in BIT(pin) of
	 * ALT[1], bit 2 of f goes in BIT(pin) of ALT[2]
	 * This is racy because three registers can't be updated at the same time
	 * but it doesn't matter much for now.
	 * Note: ALT0/ALT1/ALT2 are organized specially for 78 gpio targets
	 */
	regmap_update_bits(info->map, REG_ALT(0, info, pin->pin),
			   BIT(p), f << p);
	regmap_update_bits(info->map, REG_ALT(1, info, pin->pin),
			   BIT(p), (f >> 1) << p);
	regmap_update_bits(info->map, REG_ALT(2, info, pin->pin),
			   BIT(p), (f >> 2) << p);

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

static int lan966x_gpio_request_enable(struct pinctrl_dev *pctldev,
				       struct pinctrl_gpio_range *range,
				       unsigned int offset)
{
	struct ocelot_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	unsigned int p = offset % 32;

	regmap_update_bits(info->map, REG_ALT(0, info, offset),
			   BIT(p), 0);
	regmap_update_bits(info->map, REG_ALT(1, info, offset),
			   BIT(p), 0);
	regmap_update_bits(info->map, REG_ALT(2, info, offset),
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

static const struct pinmux_ops lan966x_pmx_ops = {
	.get_functions_count = ocelot_get_functions_count,
	.get_function_name = ocelot_get_function_name,
	.get_function_groups = ocelot_get_function_groups,
	.set_mux = lan966x_pinmux_set_mux,
	.gpio_set_direction = ocelot_gpio_set_direction,
	.gpio_request_enable = lan966x_gpio_request_enable,
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
		const struct ocelot_pincfg_data *opd = info->pincfg_data;
		u32 regcfg;

		ret = regmap_read(info->pincfg,
				  pin * regmap_get_reg_stride(info->pincfg),
				  &regcfg);
		if (ret)
			return ret;

		ret = 0;
		switch (reg) {
		case PINCONF_BIAS:
			*val = regcfg & (opd->pd_bit | opd->pu_bit);
			break;

		case PINCONF_SCHMITT:
			*val = regcfg & opd->schmitt_bit;
			break;

		case PINCONF_DRIVE_STRENGTH:
			*val = regcfg & opd->drive_bits;
			break;

		default:
			ret = -EOPNOTSUPP;
			break;
		}
	}
	return ret;
}

static int ocelot_pincfg_clrsetbits(struct ocelot_pinctrl *info, u32 regaddr,
				    u32 clrbits, u32 setbits)
{
	u32 val;
	int ret;

	ret = regmap_read(info->pincfg,
			  regaddr * regmap_get_reg_stride(info->pincfg),
			  &val);
	if (ret)
		return ret;

	val &= ~clrbits;
	val |= setbits;

	ret = regmap_write(info->pincfg,
			   regaddr * regmap_get_reg_stride(info->pincfg),
			   val);

	return ret;
}

static int ocelot_hw_set_value(struct ocelot_pinctrl *info,
			       unsigned int pin,
			       unsigned int reg,
			       int val)
{
	int ret = -EOPNOTSUPP;

	if (info->pincfg) {
		const struct ocelot_pincfg_data *opd = info->pincfg_data;

		switch (reg) {
		case PINCONF_BIAS:
			ret = ocelot_pincfg_clrsetbits(info, pin,
						       opd->pd_bit | opd->pu_bit,
						       val);
			break;

		case PINCONF_SCHMITT:
			ret = ocelot_pincfg_clrsetbits(info, pin,
						       opd->schmitt_bit,
						       val);
			break;

		case PINCONF_DRIVE_STRENGTH:
			if (val <= 3)
				ret = ocelot_pincfg_clrsetbits(info, pin,
							       opd->drive_bits,
							       val);
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
			val = !!(val & info->pincfg_data->pd_bit);
		else    /* PIN_CONFIG_BIAS_PULL_UP */
			val = !!(val & info->pincfg_data->pu_bit);
		break;

	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		if (!info->pincfg_data->schmitt_bit)
			return -EOPNOTSUPP;

		err = ocelot_hw_get_value(info, pin, PINCONF_SCHMITT, &val);
		if (err)
			return err;

		val = !!(val & info->pincfg_data->schmitt_bit);
		break;

	case PIN_CONFIG_DRIVE_STRENGTH:
		err = ocelot_hw_get_value(info, pin, PINCONF_DRIVE_STRENGTH,
					  &val);
		if (err)
			return err;
		break;

	case PIN_CONFIG_LEVEL:
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
	const struct ocelot_pincfg_data *opd = info->pincfg_data;
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
			      (param == PIN_CONFIG_BIAS_PULL_UP) ?
				opd->pu_bit : opd->pd_bit;

			err = ocelot_hw_set_value(info, pin, PINCONF_BIAS, arg);
			if (err)
				goto err;

			break;

		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			if (!opd->schmitt_bit)
				return -EOPNOTSUPP;

			arg = arg ? opd->schmitt_bit : 0;
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
		case PIN_CONFIG_LEVEL:
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

static const struct ocelot_match_data luton_desc = {
	.desc = {
		.name = "luton-pinctrl",
		.pins = luton_pins,
		.npins = ARRAY_SIZE(luton_pins),
		.pctlops = &ocelot_pctl_ops,
		.pmxops = &ocelot_pmx_ops,
		.owner = THIS_MODULE,
	},
};

static const struct ocelot_match_data serval_desc = {
	.desc = {
		.name = "serval-pinctrl",
		.pins = serval_pins,
		.npins = ARRAY_SIZE(serval_pins),
		.pctlops = &ocelot_pctl_ops,
		.pmxops = &ocelot_pmx_ops,
		.owner = THIS_MODULE,
	},
};

static const struct ocelot_match_data ocelot_desc = {
	.desc = {
		.name = "ocelot-pinctrl",
		.pins = ocelot_pins,
		.npins = ARRAY_SIZE(ocelot_pins),
		.pctlops = &ocelot_pctl_ops,
		.pmxops = &ocelot_pmx_ops,
		.owner = THIS_MODULE,
	},
};

static const struct ocelot_match_data jaguar2_desc = {
	.desc = {
		.name = "jaguar2-pinctrl",
		.pins = jaguar2_pins,
		.npins = ARRAY_SIZE(jaguar2_pins),
		.pctlops = &ocelot_pctl_ops,
		.pmxops = &ocelot_pmx_ops,
		.owner = THIS_MODULE,
	},
};

static const struct ocelot_match_data servalt_desc = {
	.desc = {
		.name = "servalt-pinctrl",
		.pins = servalt_pins,
		.npins = ARRAY_SIZE(servalt_pins),
		.pctlops = &ocelot_pctl_ops,
		.pmxops = &ocelot_pmx_ops,
		.owner = THIS_MODULE,
	},
};

static const struct ocelot_match_data sparx5_desc = {
	.desc = {
		.name = "sparx5-pinctrl",
		.pins = sparx5_pins,
		.npins = ARRAY_SIZE(sparx5_pins),
		.pctlops = &ocelot_pctl_ops,
		.pmxops = &ocelot_pmx_ops,
		.confops = &ocelot_confops,
		.owner = THIS_MODULE,
	},
	.pincfg_data = {
		.pd_bit = BIT(4),
		.pu_bit = BIT(3),
		.drive_bits = GENMASK(1, 0),
		.schmitt_bit = BIT(2),
	},
};

static const struct ocelot_match_data lan966x_desc = {
	.desc = {
		.name = "lan966x-pinctrl",
		.pins = lan966x_pins,
		.npins = ARRAY_SIZE(lan966x_pins),
		.pctlops = &ocelot_pctl_ops,
		.pmxops = &lan966x_pmx_ops,
		.confops = &ocelot_confops,
		.owner = THIS_MODULE,
	},
	.pincfg_data = {
		.pd_bit = BIT(3),
		.pu_bit = BIT(2),
		.drive_bits = GENMASK(1, 0),
	},
};

static const struct ocelot_match_data lan969x_desc = {
	.desc = {
		.name = "lan969x-pinctrl",
		.pins = lan969x_pins,
		.npins = ARRAY_SIZE(lan969x_pins),
		.pctlops = &ocelot_pctl_ops,
		.pmxops = &lan966x_pmx_ops,
		.confops = &ocelot_confops,
		.owner = THIS_MODULE,
	},
	.pincfg_data = {
		.pd_bit = BIT(3),
		.pu_bit = BIT(2),
		.drive_bits = GENMASK(1, 0),
	},
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

static int ocelot_gpio_set(struct gpio_chip *chip, unsigned int offset,
			   int value)
{
	struct ocelot_pinctrl *info = gpiochip_get_data(chip);

	if (value)
		return regmap_write(info->map,
				    REG(OCELOT_GPIO_OUT_SET, info, offset),
				    BIT(offset % 32));

	return regmap_write(info->map, REG(OCELOT_GPIO_OUT_CLR, info, offset),
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

	return pinctrl_gpio_direction_output(chip, offset);
}

static const struct gpio_chip ocelot_gpiolib_chip = {
	.request = gpiochip_generic_request,
	.free = gpiochip_generic_free,
	.set = ocelot_gpio_set,
	.get = ocelot_gpio_get,
	.get_direction = ocelot_gpio_get_direction,
	.direction_input = pinctrl_gpio_direction_input,
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
	gpiochip_disable_irq(chip, gpio);
}

static void ocelot_irq_work(struct work_struct *work)
{
	struct ocelot_irq_work *w = container_of(work, struct ocelot_irq_work, irq_work);
	struct irq_chip *parent_chip = irq_desc_get_chip(w->irq_desc);
	struct gpio_chip *chip = irq_desc_get_chip_data(w->irq_desc);
	struct irq_data *data = irq_desc_get_irq_data(w->irq_desc);
	unsigned int gpio = irqd_to_hwirq(data);

	local_irq_disable();
	chained_irq_enter(parent_chip, w->irq_desc);
	generic_handle_domain_irq(chip->irq.domain, gpio);
	chained_irq_exit(parent_chip, w->irq_desc);
	local_irq_enable();

	kfree(w);
}

static void ocelot_irq_unmask_level(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct ocelot_pinctrl *info = gpiochip_get_data(chip);
	struct irq_desc *desc = irq_data_to_desc(data);
	unsigned int gpio = irqd_to_hwirq(data);
	unsigned int bit = BIT(gpio % 32);
	bool ack = false, active = false;
	u8 trigger_level;
	int val;

	trigger_level = irqd_get_trigger_type(data);

	/* Check if the interrupt line is still active. */
	regmap_read(info->map, REG(OCELOT_GPIO_IN, info, gpio), &val);
	if ((!(val & bit) && trigger_level == IRQ_TYPE_LEVEL_LOW) ||
	      (val & bit && trigger_level == IRQ_TYPE_LEVEL_HIGH))
		active = true;

	/*
	 * Check if the interrupt controller has seen any changes in the
	 * interrupt line.
	 */
	regmap_read(info->map, REG(OCELOT_GPIO_INTR, info, gpio), &val);
	if (val & bit)
		ack = true;

	/* Try to clear any rising edges */
	if (!active && ack)
		regmap_write_bits(info->map, REG(OCELOT_GPIO_INTR, info, gpio),
				  bit, bit);

	/* Enable the interrupt now */
	gpiochip_enable_irq(chip, gpio);
	regmap_update_bits(info->map, REG(OCELOT_GPIO_INTR_ENA, info, gpio),
			   bit, bit);

	/*
	 * In case the interrupt line is still active then it means that
	 * there happen another interrupt while the line was active.
	 * So we missed that one, so we need to kick the interrupt again
	 * handler.
	 */
	regmap_read(info->map, REG(OCELOT_GPIO_IN, info, gpio), &val);
	if ((!(val & bit) && trigger_level == IRQ_TYPE_LEVEL_LOW) ||
	      (val & bit && trigger_level == IRQ_TYPE_LEVEL_HIGH))
		active = true;

	if (active) {
		struct ocelot_irq_work *work;

		work = kmalloc(sizeof(*work), GFP_ATOMIC);
		if (!work)
			return;

		work->irq_desc = desc;
		INIT_WORK(&work->irq_work, ocelot_irq_work);
		queue_work(info->wq, &work->irq_work);
	}
}

static void ocelot_irq_unmask(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct ocelot_pinctrl *info = gpiochip_get_data(chip);
	unsigned int gpio = irqd_to_hwirq(data);

	gpiochip_enable_irq(chip, gpio);
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

static const struct irq_chip ocelot_level_irqchip = {
	.name		= "gpio",
	.irq_mask	= ocelot_irq_mask,
	.irq_ack	= ocelot_irq_ack,
	.irq_unmask	= ocelot_irq_unmask_level,
	.flags		= IRQCHIP_IMMUTABLE,
	.irq_set_type	= ocelot_irq_set_type,
	GPIOCHIP_IRQ_RESOURCE_HELPERS
};

static const struct irq_chip ocelot_irqchip = {
	.name		= "gpio",
	.irq_mask	= ocelot_irq_mask,
	.irq_ack	= ocelot_irq_ack,
	.irq_unmask	= ocelot_irq_unmask,
	.irq_set_type	= ocelot_irq_set_type,
	.flags          = IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS
};

static int ocelot_irq_set_type(struct irq_data *data, unsigned int type)
{
	if (type & (IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW))
		irq_set_chip_handler_name_locked(data, &ocelot_level_irqchip,
						 handle_level_irq, NULL);
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

	chained_irq_enter(parent_chip, desc);

	for (i = 0; i < info->stride; i++) {
		regmap_read(info->map, id_reg + 4 * i, &reg);
		if (!reg)
			continue;

		irqs = reg;

		for_each_set_bit(irq, &irqs,
				 min(32U, info->desc->npins - 32 * i))
			generic_handle_domain_irq(chip->irq.domain, irq + 32 * i);
	}

	chained_irq_exit(parent_chip, desc);
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
	gc->base = -1;
	gc->label = "ocelot-gpio";

	irq = platform_get_irq_optional(pdev, 0);
	if (irq > 0) {
		girq = &gc->irq;
		gpio_irq_chip_set_chip(girq, &ocelot_irqchip);
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
	{ .compatible = "mscc,servalt-pinctrl", .data = &servalt_desc },
	{ .compatible = "microchip,sparx5-pinctrl", .data = &sparx5_desc },
	{ .compatible = "microchip,lan966x-pinctrl", .data = &lan966x_desc },
	{ .compatible = "microchip,lan9691-pinctrl", .data = &lan969x_desc },
	{},
};
MODULE_DEVICE_TABLE(of, ocelot_pinctrl_of_match);

static struct regmap *ocelot_pinctrl_create_pincfg(struct platform_device *pdev,
						   const struct ocelot_pinctrl *info)
{
	void __iomem *base;

	const struct regmap_config regmap_config = {
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
		.max_register = info->desc->npins * 4,
		.name = "pincfg",
	};

	base = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(base)) {
		dev_dbg(&pdev->dev, "Failed to ioremap config registers (no extended pinconf)\n");
		return NULL;
	}

	return devm_regmap_init_mmio(&pdev->dev, base, &regmap_config);
}

static void ocelot_destroy_workqueue(void *data)
{
	destroy_workqueue(data);
}

static int ocelot_pinctrl_probe(struct platform_device *pdev)
{
	const struct ocelot_match_data *data;
	struct device *dev = &pdev->dev;
	struct ocelot_pinctrl *info;
	struct reset_control *reset;
	struct regmap *pincfg;
	int ret;
	struct regmap_config regmap_config = {
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
	};

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	data = device_get_match_data(dev);
	if (!data)
		return -EINVAL;

	info->desc = devm_kmemdup(dev, &data->desc, sizeof(*info->desc),
				  GFP_KERNEL);
	if (!info->desc)
		return -ENOMEM;

	info->wq = alloc_ordered_workqueue("ocelot_ordered", 0);
	if (!info->wq)
		return -ENOMEM;

	ret = devm_add_action_or_reset(dev, ocelot_destroy_workqueue,
				       info->wq);
	if (ret)
		return ret;

	info->pincfg_data = &data->pincfg_data;

	reset = devm_reset_control_get_optional_shared(dev, "switch");
	if (IS_ERR(reset))
		return dev_err_probe(dev, PTR_ERR(reset),
				     "Failed to get reset\n");
	reset_control_reset(reset);

	info->stride = 1 + (info->desc->npins - 1) / 32;

	regmap_config.max_register = OCELOT_GPIO_SD_MAP * info->stride + 15 * 4;

	info->map = ocelot_regmap_from_resource(pdev, 0, &regmap_config);
	if (IS_ERR(info->map))
		return dev_err_probe(dev, PTR_ERR(info->map),
				     "Failed to create regmap\n");
	dev_set_drvdata(dev, info);
	info->dev = dev;

	/* Pinconf registers */
	if (info->desc->confops) {
		pincfg = ocelot_pinctrl_create_pincfg(pdev, info);
		if (IS_ERR(pincfg))
			dev_dbg(dev, "Failed to create pincfg regmap\n");
		else
			info->pincfg = pincfg;
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
module_platform_driver(ocelot_pinctrl_driver);

MODULE_DESCRIPTION("Ocelot Chip Pinctrl Driver");
MODULE_LICENSE("Dual MIT/GPL");
