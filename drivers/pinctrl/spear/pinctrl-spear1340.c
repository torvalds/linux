/*
 * Driver for the ST Microelectronics SPEAr1340 pinmux
 *
 * Copyright (C) 2012 ST Microelectronics
 * Viresh Kumar <viresh.linux@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include "pinctrl-spear.h"

#define DRIVER_NAME "spear1340-pinmux"

/* pins */
static const struct pinctrl_pin_desc spear1340_pins[] = {
	SPEAR_PIN_0_TO_101,
	SPEAR_PIN_102_TO_245,
	PINCTRL_PIN(246, "PLGPIO246"),
	PINCTRL_PIN(247, "PLGPIO247"),
	PINCTRL_PIN(248, "PLGPIO248"),
	PINCTRL_PIN(249, "PLGPIO249"),
	PINCTRL_PIN(250, "PLGPIO250"),
	PINCTRL_PIN(251, "PLGPIO251"),
};

/* In SPEAr1340 there are two levels of pad muxing */
/* - pads as gpio OR peripherals */
#define PAD_FUNCTION_EN_1			0x668
#define PAD_FUNCTION_EN_2			0x66C
#define PAD_FUNCTION_EN_3			0x670
#define PAD_FUNCTION_EN_4			0x674
#define PAD_FUNCTION_EN_5			0x690
#define PAD_FUNCTION_EN_6			0x694
#define PAD_FUNCTION_EN_7			0x698
#define PAD_FUNCTION_EN_8			0x69C

/* - If peripherals, then primary OR alternate peripheral */
#define PAD_SHARED_IP_EN_1			0x6A0
#define PAD_SHARED_IP_EN_2			0x6A4

/*
 * Macro's for first level of pmx - pads as gpio OR peripherals. There are 8
 * registers with 32 bits each for handling gpio pads, register 8 has only 26
 * relevant bits.
 */
/* macro's for making pads as gpio's */
#define PADS_AS_GPIO_REG0_MASK			0xFFFFFFFE
#define PADS_AS_GPIO_REGS_MASK			0xFFFFFFFF
#define PADS_AS_GPIO_REG7_MASK			0x07FFFFFF

/* macro's for making pads as peripherals */
#define FSMC_16_BIT_AND_KBD_ROW_COL_REG0_MASK	0x00000FFE
#define UART0_ENH_AND_GPT_REG0_MASK		0x0003F000
#define PWM1_AND_KBD_COL5_REG0_MASK		0x00040000
#define I2C1_REG0_MASK				0x01080000
#define SPDIF_IN_REG0_MASK			0x00100000
#define PWM2_AND_GPT0_TMR0_CPT_REG0_MASK	0x00400000
#define PWM3_AND_GPT0_TMR1_CLK_REG0_MASK	0x00800000
#define PWM0_AND_SSP0_CS1_REG0_MASK		0x02000000
#define VIP_AND_CAM3_REG0_MASK			0xFC200000
#define VIP_AND_CAM3_REG1_MASK			0x0000000F
#define VIP_REG1_MASK				0x00001EF0
#define VIP_AND_CAM2_REG1_MASK			0x007FE100
#define VIP_AND_CAM1_REG1_MASK			0xFF800000
#define VIP_AND_CAM1_REG2_MASK			0x00000003
#define VIP_AND_CAM0_REG2_MASK			0x00001FFC
#define SMI_REG2_MASK				0x0021E000
#define SSP0_REG2_MASK				0x001E0000
#define TS_AND_SSP0_CS2_REG2_MASK		0x00400000
#define UART0_REG2_MASK				0x01800000
#define UART1_REG2_MASK				0x06000000
#define I2S_IN_REG2_MASK			0xF8000000
#define DEVS_GRP_AND_MIPHY_DBG_REG3_MASK	0x000001FE
#define I2S_OUT_REG3_MASK			0x000001EF
#define I2S_IN_REG3_MASK			0x00000010
#define GMAC_REG3_MASK				0xFFFFFE00
#define GMAC_REG4_MASK				0x0000001F
#define DEVS_GRP_AND_MIPHY_DBG_REG4_MASK	0x7FFFFF20
#define SSP0_CS3_REG4_MASK			0x00000020
#define I2C0_REG4_MASK				0x000000C0
#define CEC0_REG4_MASK				0x00000100
#define CEC1_REG4_MASK				0x00000200
#define SPDIF_OUT_REG4_MASK			0x00000400
#define CLCD_REG4_MASK				0x7FFFF800
#define CLCD_AND_ARM_TRACE_REG4_MASK		0x80000000
#define CLCD_AND_ARM_TRACE_REG5_MASK		0xFFFFFFFF
#define CLCD_AND_ARM_TRACE_REG6_MASK		0x00000001
#define FSMC_PNOR_AND_MCIF_REG6_MASK		0x073FFFFE
#define MCIF_REG6_MASK				0xF8C00000
#define MCIF_REG7_MASK				0x000043FF
#define FSMC_8BIT_REG7_MASK			0x07FFBC00

/* other registers */
#define PERIP_CFG				0x42C
	/* PERIP_CFG register masks */
	#define SSP_CS_CTL_HW			0
	#define SSP_CS_CTL_SW			1
	#define SSP_CS_CTL_MASK			1
	#define SSP_CS_CTL_SHIFT		21
	#define SSP_CS_VAL_MASK			1
	#define SSP_CS_VAL_SHIFT		20
	#define SSP_CS_SEL_CS0			0
	#define SSP_CS_SEL_CS1			1
	#define SSP_CS_SEL_CS2			2
	#define SSP_CS_SEL_MASK			3
	#define SSP_CS_SEL_SHIFT		18

	#define I2S_CHNL_2_0			(0)
	#define I2S_CHNL_3_1			(1)
	#define I2S_CHNL_5_1			(2)
	#define I2S_CHNL_7_1			(3)
	#define I2S_CHNL_PLAY_SHIFT		(4)
	#define I2S_CHNL_PLAY_MASK		(3 << 4)
	#define I2S_CHNL_REC_SHIFT		(6)
	#define I2S_CHNL_REC_MASK		(3 << 6)

	#define SPDIF_OUT_ENB_MASK		(1 << 2)
	#define SPDIF_OUT_ENB_SHIFT		2

	#define MCIF_SEL_SD			1
	#define MCIF_SEL_CF			2
	#define MCIF_SEL_XD			3
	#define MCIF_SEL_MASK			3
	#define MCIF_SEL_SHIFT			0

#define GMAC_CLK_CFG				0x248
	#define GMAC_PHY_IF_GMII_VAL		(0 << 3)
	#define GMAC_PHY_IF_RGMII_VAL		(1 << 3)
	#define GMAC_PHY_IF_SGMII_VAL		(2 << 3)
	#define GMAC_PHY_IF_RMII_VAL		(4 << 3)
	#define GMAC_PHY_IF_SEL_MASK		(7 << 3)
	#define GMAC_PHY_INPUT_ENB_VAL		0
	#define GMAC_PHY_SYNT_ENB_VAL		1
	#define GMAC_PHY_CLK_MASK		1
	#define GMAC_PHY_CLK_SHIFT		2
	#define GMAC_PHY_125M_PAD_VAL		0
	#define GMAC_PHY_PLL2_VAL		1
	#define GMAC_PHY_OSC3_VAL		2
	#define GMAC_PHY_INPUT_CLK_MASK		3
	#define GMAC_PHY_INPUT_CLK_SHIFT	0

#define PCIE_SATA_CFG				0x424
	/* PCIE CFG MASks */
	#define PCIE_CFG_DEVICE_PRESENT		(1 << 11)
	#define PCIE_CFG_POWERUP_RESET		(1 << 10)
	#define PCIE_CFG_CORE_CLK_EN		(1 << 9)
	#define PCIE_CFG_AUX_CLK_EN		(1 << 8)
	#define SATA_CFG_TX_CLK_EN		(1 << 4)
	#define SATA_CFG_RX_CLK_EN		(1 << 3)
	#define SATA_CFG_POWERUP_RESET		(1 << 2)
	#define SATA_CFG_PM_CLK_EN		(1 << 1)
	#define PCIE_SATA_SEL_PCIE		(0)
	#define PCIE_SATA_SEL_SATA		(1)
	#define SATA_PCIE_CFG_MASK		0xF1F
	#define PCIE_CFG_VAL	(PCIE_SATA_SEL_PCIE | PCIE_CFG_AUX_CLK_EN | \
				PCIE_CFG_CORE_CLK_EN | PCIE_CFG_POWERUP_RESET |\
				PCIE_CFG_DEVICE_PRESENT)
	#define SATA_CFG_VAL	(PCIE_SATA_SEL_SATA | SATA_CFG_PM_CLK_EN | \
				SATA_CFG_POWERUP_RESET | SATA_CFG_RX_CLK_EN | \
				SATA_CFG_TX_CLK_EN)

/* Macro's for second level of pmx - pads as primary OR alternate peripheral */
/* Write 0 to enable FSMC_16_BIT */
#define KBD_ROW_COL_MASK			(1 << 0)

/* Write 0 to enable UART0_ENH */
#define GPT_MASK				(1 << 1) /* Only clk & cpt */

/* Write 0 to enable PWM1 */
#define KBD_COL5_MASK				(1 << 2)

/* Write 0 to enable PWM2 */
#define GPT0_TMR0_CPT_MASK			(1 << 3) /* Only clk & cpt */

/* Write 0 to enable PWM3 */
#define GPT0_TMR1_CLK_MASK			(1 << 4) /* Only clk & cpt */

/* Write 0 to enable PWM0 */
#define SSP0_CS1_MASK				(1 << 5)

/* Write 0 to enable VIP */
#define CAM3_MASK				(1 << 6)

/* Write 0 to enable VIP */
#define CAM2_MASK				(1 << 7)

/* Write 0 to enable VIP */
#define CAM1_MASK				(1 << 8)

/* Write 0 to enable VIP */
#define CAM0_MASK				(1 << 9)

/* Write 0 to enable TS */
#define SSP0_CS2_MASK				(1 << 10)

/* Write 0 to enable FSMC PNOR */
#define MCIF_MASK				(1 << 11)

/* Write 0 to enable CLCD */
#define ARM_TRACE_MASK				(1 << 12)

/* Write 0 to enable I2S, SSP0_CS2, CEC0, 1, SPDIF out, CLCD */
#define MIPHY_DBG_MASK				(1 << 13)

/*
 * Pad multiplexing for making all pads as gpio's. This is done to override the
 * values passed from bootloader and start from scratch.
 */
static const unsigned pads_as_gpio_pins[] = { 12, 88, 89, 251 };
static struct spear_muxreg pads_as_gpio_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_1,
		.mask = PADS_AS_GPIO_REG0_MASK,
		.val = 0x0,
	}, {
		.reg = PAD_FUNCTION_EN_2,
		.mask = PADS_AS_GPIO_REGS_MASK,
		.val = 0x0,
	}, {
		.reg = PAD_FUNCTION_EN_3,
		.mask = PADS_AS_GPIO_REGS_MASK,
		.val = 0x0,
	}, {
		.reg = PAD_FUNCTION_EN_4,
		.mask = PADS_AS_GPIO_REGS_MASK,
		.val = 0x0,
	}, {
		.reg = PAD_FUNCTION_EN_5,
		.mask = PADS_AS_GPIO_REGS_MASK,
		.val = 0x0,
	}, {
		.reg = PAD_FUNCTION_EN_6,
		.mask = PADS_AS_GPIO_REGS_MASK,
		.val = 0x0,
	}, {
		.reg = PAD_FUNCTION_EN_7,
		.mask = PADS_AS_GPIO_REGS_MASK,
		.val = 0x0,
	}, {
		.reg = PAD_FUNCTION_EN_8,
		.mask = PADS_AS_GPIO_REG7_MASK,
		.val = 0x0,
	},
};

static struct spear_modemux pads_as_gpio_modemux[] = {
	{
		.muxregs = pads_as_gpio_muxreg,
		.nmuxregs = ARRAY_SIZE(pads_as_gpio_muxreg),
	},
};

static struct spear_pingroup pads_as_gpio_pingroup = {
	.name = "pads_as_gpio_grp",
	.pins = pads_as_gpio_pins,
	.npins = ARRAY_SIZE(pads_as_gpio_pins),
	.modemuxs = pads_as_gpio_modemux,
	.nmodemuxs = ARRAY_SIZE(pads_as_gpio_modemux),
};

static const char *const pads_as_gpio_grps[] = { "pads_as_gpio_grp" };
static struct spear_function pads_as_gpio_function = {
	.name = "pads_as_gpio",
	.groups = pads_as_gpio_grps,
	.ngroups = ARRAY_SIZE(pads_as_gpio_grps),
};

/* Pad multiplexing for fsmc_8bit device */
static const unsigned fsmc_8bit_pins[] = { 233, 234, 235, 236, 238, 239, 240,
	241, 242, 243, 244, 245, 246, 247, 248, 249 };
static struct spear_muxreg fsmc_8bit_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_8,
		.mask = FSMC_8BIT_REG7_MASK,
		.val = FSMC_8BIT_REG7_MASK,
	}
};

static struct spear_modemux fsmc_8bit_modemux[] = {
	{
		.muxregs = fsmc_8bit_muxreg,
		.nmuxregs = ARRAY_SIZE(fsmc_8bit_muxreg),
	},
};

static struct spear_pingroup fsmc_8bit_pingroup = {
	.name = "fsmc_8bit_grp",
	.pins = fsmc_8bit_pins,
	.npins = ARRAY_SIZE(fsmc_8bit_pins),
	.modemuxs = fsmc_8bit_modemux,
	.nmodemuxs = ARRAY_SIZE(fsmc_8bit_modemux),
};

/* Pad multiplexing for fsmc_16bit device */
static const unsigned fsmc_16bit_pins[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
static struct spear_muxreg fsmc_16bit_muxreg[] = {
	{
		.reg = PAD_SHARED_IP_EN_1,
		.mask = KBD_ROW_COL_MASK,
		.val = 0,
	}, {
		.reg = PAD_FUNCTION_EN_1,
		.mask = FSMC_16_BIT_AND_KBD_ROW_COL_REG0_MASK,
		.val = FSMC_16_BIT_AND_KBD_ROW_COL_REG0_MASK,
	},
};

static struct spear_modemux fsmc_16bit_modemux[] = {
	{
		.muxregs = fsmc_16bit_muxreg,
		.nmuxregs = ARRAY_SIZE(fsmc_16bit_muxreg),
	},
};

static struct spear_pingroup fsmc_16bit_pingroup = {
	.name = "fsmc_16bit_grp",
	.pins = fsmc_16bit_pins,
	.npins = ARRAY_SIZE(fsmc_16bit_pins),
	.modemuxs = fsmc_16bit_modemux,
	.nmodemuxs = ARRAY_SIZE(fsmc_16bit_modemux),
};

/* pad multiplexing for fsmc_pnor device */
static const unsigned fsmc_pnor_pins[] = { 192, 193, 194, 195, 196, 197, 198,
	199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212,
	215, 216, 217 };
static struct spear_muxreg fsmc_pnor_muxreg[] = {
	{
		.reg = PAD_SHARED_IP_EN_1,
		.mask = MCIF_MASK,
		.val = 0,
	}, {
		.reg = PAD_FUNCTION_EN_7,
		.mask = FSMC_PNOR_AND_MCIF_REG6_MASK,
		.val = FSMC_PNOR_AND_MCIF_REG6_MASK,
	},
};

static struct spear_modemux fsmc_pnor_modemux[] = {
	{
		.muxregs = fsmc_pnor_muxreg,
		.nmuxregs = ARRAY_SIZE(fsmc_pnor_muxreg),
	},
};

static struct spear_pingroup fsmc_pnor_pingroup = {
	.name = "fsmc_pnor_grp",
	.pins = fsmc_pnor_pins,
	.npins = ARRAY_SIZE(fsmc_pnor_pins),
	.modemuxs = fsmc_pnor_modemux,
	.nmodemuxs = ARRAY_SIZE(fsmc_pnor_modemux),
};

static const char *const fsmc_grps[] = { "fsmc_8bit_grp", "fsmc_16bit_grp",
	"fsmc_pnor_grp" };
static struct spear_function fsmc_function = {
	.name = "fsmc",
	.groups = fsmc_grps,
	.ngroups = ARRAY_SIZE(fsmc_grps),
};

/* pad multiplexing for keyboard rows-cols device */
static const unsigned keyboard_row_col_pins[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
	10 };
static struct spear_muxreg keyboard_row_col_muxreg[] = {
	{
		.reg = PAD_SHARED_IP_EN_1,
		.mask = KBD_ROW_COL_MASK,
		.val = KBD_ROW_COL_MASK,
	}, {
		.reg = PAD_FUNCTION_EN_1,
		.mask = FSMC_16_BIT_AND_KBD_ROW_COL_REG0_MASK,
		.val = FSMC_16_BIT_AND_KBD_ROW_COL_REG0_MASK,
	},
};

static struct spear_modemux keyboard_row_col_modemux[] = {
	{
		.muxregs = keyboard_row_col_muxreg,
		.nmuxregs = ARRAY_SIZE(keyboard_row_col_muxreg),
	},
};

static struct spear_pingroup keyboard_row_col_pingroup = {
	.name = "keyboard_row_col_grp",
	.pins = keyboard_row_col_pins,
	.npins = ARRAY_SIZE(keyboard_row_col_pins),
	.modemuxs = keyboard_row_col_modemux,
	.nmodemuxs = ARRAY_SIZE(keyboard_row_col_modemux),
};

/* pad multiplexing for keyboard col5 device */
static const unsigned keyboard_col5_pins[] = { 17 };
static struct spear_muxreg keyboard_col5_muxreg[] = {
	{
		.reg = PAD_SHARED_IP_EN_1,
		.mask = KBD_COL5_MASK,
		.val = KBD_COL5_MASK,
	}, {
		.reg = PAD_FUNCTION_EN_1,
		.mask = PWM1_AND_KBD_COL5_REG0_MASK,
		.val = PWM1_AND_KBD_COL5_REG0_MASK,
	},
};

static struct spear_modemux keyboard_col5_modemux[] = {
	{
		.muxregs = keyboard_col5_muxreg,
		.nmuxregs = ARRAY_SIZE(keyboard_col5_muxreg),
	},
};

static struct spear_pingroup keyboard_col5_pingroup = {
	.name = "keyboard_col5_grp",
	.pins = keyboard_col5_pins,
	.npins = ARRAY_SIZE(keyboard_col5_pins),
	.modemuxs = keyboard_col5_modemux,
	.nmodemuxs = ARRAY_SIZE(keyboard_col5_modemux),
};

static const char *const keyboard_grps[] = { "keyboard_row_col_grp",
	"keyboard_col5_grp" };
static struct spear_function keyboard_function = {
	.name = "keyboard",
	.groups = keyboard_grps,
	.ngroups = ARRAY_SIZE(keyboard_grps),
};

/* pad multiplexing for spdif_in device */
static const unsigned spdif_in_pins[] = { 19 };
static struct spear_muxreg spdif_in_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_1,
		.mask = SPDIF_IN_REG0_MASK,
		.val = SPDIF_IN_REG0_MASK,
	},
};

static struct spear_modemux spdif_in_modemux[] = {
	{
		.muxregs = spdif_in_muxreg,
		.nmuxregs = ARRAY_SIZE(spdif_in_muxreg),
	},
};

static struct spear_pingroup spdif_in_pingroup = {
	.name = "spdif_in_grp",
	.pins = spdif_in_pins,
	.npins = ARRAY_SIZE(spdif_in_pins),
	.modemuxs = spdif_in_modemux,
	.nmodemuxs = ARRAY_SIZE(spdif_in_modemux),
};

static const char *const spdif_in_grps[] = { "spdif_in_grp" };
static struct spear_function spdif_in_function = {
	.name = "spdif_in",
	.groups = spdif_in_grps,
	.ngroups = ARRAY_SIZE(spdif_in_grps),
};

/* pad multiplexing for spdif_out device */
static const unsigned spdif_out_pins[] = { 137 };
static struct spear_muxreg spdif_out_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_5,
		.mask = SPDIF_OUT_REG4_MASK,
		.val = SPDIF_OUT_REG4_MASK,
	}, {
		.reg = PERIP_CFG,
		.mask = SPDIF_OUT_ENB_MASK,
		.val = SPDIF_OUT_ENB_MASK,
	}
};

static struct spear_modemux spdif_out_modemux[] = {
	{
		.muxregs = spdif_out_muxreg,
		.nmuxregs = ARRAY_SIZE(spdif_out_muxreg),
	},
};

static struct spear_pingroup spdif_out_pingroup = {
	.name = "spdif_out_grp",
	.pins = spdif_out_pins,
	.npins = ARRAY_SIZE(spdif_out_pins),
	.modemuxs = spdif_out_modemux,
	.nmodemuxs = ARRAY_SIZE(spdif_out_modemux),
};

static const char *const spdif_out_grps[] = { "spdif_out_grp" };
static struct spear_function spdif_out_function = {
	.name = "spdif_out",
	.groups = spdif_out_grps,
	.ngroups = ARRAY_SIZE(spdif_out_grps),
};

/* pad multiplexing for gpt_0_1 device */
static const unsigned gpt_0_1_pins[] = { 11, 12, 13, 14, 15, 16, 21, 22 };
static struct spear_muxreg gpt_0_1_muxreg[] = {
	{
		.reg = PAD_SHARED_IP_EN_1,
		.mask = GPT_MASK | GPT0_TMR0_CPT_MASK | GPT0_TMR1_CLK_MASK,
		.val = GPT_MASK | GPT0_TMR0_CPT_MASK | GPT0_TMR1_CLK_MASK,
	}, {
		.reg = PAD_FUNCTION_EN_1,
		.mask = UART0_ENH_AND_GPT_REG0_MASK |
			PWM2_AND_GPT0_TMR0_CPT_REG0_MASK |
			PWM3_AND_GPT0_TMR1_CLK_REG0_MASK,
		.val = UART0_ENH_AND_GPT_REG0_MASK |
			PWM2_AND_GPT0_TMR0_CPT_REG0_MASK |
			PWM3_AND_GPT0_TMR1_CLK_REG0_MASK,
	},
};

static struct spear_modemux gpt_0_1_modemux[] = {
	{
		.muxregs = gpt_0_1_muxreg,
		.nmuxregs = ARRAY_SIZE(gpt_0_1_muxreg),
	},
};

static struct spear_pingroup gpt_0_1_pingroup = {
	.name = "gpt_0_1_grp",
	.pins = gpt_0_1_pins,
	.npins = ARRAY_SIZE(gpt_0_1_pins),
	.modemuxs = gpt_0_1_modemux,
	.nmodemuxs = ARRAY_SIZE(gpt_0_1_modemux),
};

static const char *const gpt_0_1_grps[] = { "gpt_0_1_grp" };
static struct spear_function gpt_0_1_function = {
	.name = "gpt_0_1",
	.groups = gpt_0_1_grps,
	.ngroups = ARRAY_SIZE(gpt_0_1_grps),
};

/* pad multiplexing for pwm0 device */
static const unsigned pwm0_pins[] = { 24 };
static struct spear_muxreg pwm0_muxreg[] = {
	{
		.reg = PAD_SHARED_IP_EN_1,
		.mask = SSP0_CS1_MASK,
		.val = 0,
	}, {
		.reg = PAD_FUNCTION_EN_1,
		.mask = PWM0_AND_SSP0_CS1_REG0_MASK,
		.val = PWM0_AND_SSP0_CS1_REG0_MASK,
	},
};

static struct spear_modemux pwm0_modemux[] = {
	{
		.muxregs = pwm0_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm0_muxreg),
	},
};

static struct spear_pingroup pwm0_pingroup = {
	.name = "pwm0_grp",
	.pins = pwm0_pins,
	.npins = ARRAY_SIZE(pwm0_pins),
	.modemuxs = pwm0_modemux,
	.nmodemuxs = ARRAY_SIZE(pwm0_modemux),
};

/* pad multiplexing for pwm1 device */
static const unsigned pwm1_pins[] = { 17 };
static struct spear_muxreg pwm1_muxreg[] = {
	{
		.reg = PAD_SHARED_IP_EN_1,
		.mask = KBD_COL5_MASK,
		.val = 0,
	}, {
		.reg = PAD_FUNCTION_EN_1,
		.mask = PWM1_AND_KBD_COL5_REG0_MASK,
		.val = PWM1_AND_KBD_COL5_REG0_MASK,
	},
};

static struct spear_modemux pwm1_modemux[] = {
	{
		.muxregs = pwm1_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm1_muxreg),
	},
};

static struct spear_pingroup pwm1_pingroup = {
	.name = "pwm1_grp",
	.pins = pwm1_pins,
	.npins = ARRAY_SIZE(pwm1_pins),
	.modemuxs = pwm1_modemux,
	.nmodemuxs = ARRAY_SIZE(pwm1_modemux),
};

/* pad multiplexing for pwm2 device */
static const unsigned pwm2_pins[] = { 21 };
static struct spear_muxreg pwm2_muxreg[] = {
	{
		.reg = PAD_SHARED_IP_EN_1,
		.mask = GPT0_TMR0_CPT_MASK,
		.val = 0,
	}, {
		.reg = PAD_FUNCTION_EN_1,
		.mask = PWM2_AND_GPT0_TMR0_CPT_REG0_MASK,
		.val = PWM2_AND_GPT0_TMR0_CPT_REG0_MASK,
	},
};

static struct spear_modemux pwm2_modemux[] = {
	{
		.muxregs = pwm2_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm2_muxreg),
	},
};

static struct spear_pingroup pwm2_pingroup = {
	.name = "pwm2_grp",
	.pins = pwm2_pins,
	.npins = ARRAY_SIZE(pwm2_pins),
	.modemuxs = pwm2_modemux,
	.nmodemuxs = ARRAY_SIZE(pwm2_modemux),
};

/* pad multiplexing for pwm3 device */
static const unsigned pwm3_pins[] = { 22 };
static struct spear_muxreg pwm3_muxreg[] = {
	{
		.reg = PAD_SHARED_IP_EN_1,
		.mask = GPT0_TMR1_CLK_MASK,
		.val = 0,
	}, {
		.reg = PAD_FUNCTION_EN_1,
		.mask = PWM3_AND_GPT0_TMR1_CLK_REG0_MASK,
		.val = PWM3_AND_GPT0_TMR1_CLK_REG0_MASK,
	},
};

static struct spear_modemux pwm3_modemux[] = {
	{
		.muxregs = pwm3_muxreg,
		.nmuxregs = ARRAY_SIZE(pwm3_muxreg),
	},
};

static struct spear_pingroup pwm3_pingroup = {
	.name = "pwm3_grp",
	.pins = pwm3_pins,
	.npins = ARRAY_SIZE(pwm3_pins),
	.modemuxs = pwm3_modemux,
	.nmodemuxs = ARRAY_SIZE(pwm3_modemux),
};

static const char *const pwm_grps[] = { "pwm0_grp", "pwm1_grp", "pwm2_grp",
	"pwm3_grp" };
static struct spear_function pwm_function = {
	.name = "pwm",
	.groups = pwm_grps,
	.ngroups = ARRAY_SIZE(pwm_grps),
};

/* pad multiplexing for vip_mux device */
static const unsigned vip_mux_pins[] = { 35, 36, 37, 38, 40, 41, 42, 43 };
static struct spear_muxreg vip_mux_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_2,
		.mask = VIP_REG1_MASK,
		.val = VIP_REG1_MASK,
	},
};

static struct spear_modemux vip_mux_modemux[] = {
	{
		.muxregs = vip_mux_muxreg,
		.nmuxregs = ARRAY_SIZE(vip_mux_muxreg),
	},
};

static struct spear_pingroup vip_mux_pingroup = {
	.name = "vip_mux_grp",
	.pins = vip_mux_pins,
	.npins = ARRAY_SIZE(vip_mux_pins),
	.modemuxs = vip_mux_modemux,
	.nmodemuxs = ARRAY_SIZE(vip_mux_modemux),
};

/* pad multiplexing for vip_mux_cam0 (disables cam0) device */
static const unsigned vip_mux_cam0_pins[] = { 65, 66, 67, 68, 69, 70, 71, 72,
	73, 74, 75 };
static struct spear_muxreg vip_mux_cam0_muxreg[] = {
	{
		.reg = PAD_SHARED_IP_EN_1,
		.mask = CAM0_MASK,
		.val = 0,
	}, {
		.reg = PAD_FUNCTION_EN_3,
		.mask = VIP_AND_CAM0_REG2_MASK,
		.val = VIP_AND_CAM0_REG2_MASK,
	},
};

static struct spear_modemux vip_mux_cam0_modemux[] = {
	{
		.muxregs = vip_mux_cam0_muxreg,
		.nmuxregs = ARRAY_SIZE(vip_mux_cam0_muxreg),
	},
};

static struct spear_pingroup vip_mux_cam0_pingroup = {
	.name = "vip_mux_cam0_grp",
	.pins = vip_mux_cam0_pins,
	.npins = ARRAY_SIZE(vip_mux_cam0_pins),
	.modemuxs = vip_mux_cam0_modemux,
	.nmodemuxs = ARRAY_SIZE(vip_mux_cam0_modemux),
};

/* pad multiplexing for vip_mux_cam1 (disables cam1) device */
static const unsigned vip_mux_cam1_pins[] = { 54, 55, 56, 57, 58, 59, 60, 61,
	62, 63, 64 };
static struct spear_muxreg vip_mux_cam1_muxreg[] = {
	{
		.reg = PAD_SHARED_IP_EN_1,
		.mask = CAM1_MASK,
		.val = 0,
	}, {
		.reg = PAD_FUNCTION_EN_2,
		.mask = VIP_AND_CAM1_REG1_MASK,
		.val = VIP_AND_CAM1_REG1_MASK,
	}, {
		.reg = PAD_FUNCTION_EN_3,
		.mask = VIP_AND_CAM1_REG2_MASK,
		.val = VIP_AND_CAM1_REG2_MASK,
	},
};

static struct spear_modemux vip_mux_cam1_modemux[] = {
	{
		.muxregs = vip_mux_cam1_muxreg,
		.nmuxregs = ARRAY_SIZE(vip_mux_cam1_muxreg),
	},
};

static struct spear_pingroup vip_mux_cam1_pingroup = {
	.name = "vip_mux_cam1_grp",
	.pins = vip_mux_cam1_pins,
	.npins = ARRAY_SIZE(vip_mux_cam1_pins),
	.modemuxs = vip_mux_cam1_modemux,
	.nmodemuxs = ARRAY_SIZE(vip_mux_cam1_modemux),
};

/* pad multiplexing for vip_mux_cam2 (disables cam2) device */
static const unsigned vip_mux_cam2_pins[] = { 39, 44, 45, 46, 47, 48, 49, 50,
	51, 52, 53 };
static struct spear_muxreg vip_mux_cam2_muxreg[] = {
	{
		.reg = PAD_SHARED_IP_EN_1,
		.mask = CAM2_MASK,
		.val = 0,
	}, {
		.reg = PAD_FUNCTION_EN_2,
		.mask = VIP_AND_CAM2_REG1_MASK,
		.val = VIP_AND_CAM2_REG1_MASK,
	},
};

static struct spear_modemux vip_mux_cam2_modemux[] = {
	{
		.muxregs = vip_mux_cam2_muxreg,
		.nmuxregs = ARRAY_SIZE(vip_mux_cam2_muxreg),
	},
};

static struct spear_pingroup vip_mux_cam2_pingroup = {
	.name = "vip_mux_cam2_grp",
	.pins = vip_mux_cam2_pins,
	.npins = ARRAY_SIZE(vip_mux_cam2_pins),
	.modemuxs = vip_mux_cam2_modemux,
	.nmodemuxs = ARRAY_SIZE(vip_mux_cam2_modemux),
};

/* pad multiplexing for vip_mux_cam3 (disables cam3) device */
static const unsigned vip_mux_cam3_pins[] = { 20, 25, 26, 27, 28, 29, 30, 31,
	32, 33, 34 };
static struct spear_muxreg vip_mux_cam3_muxreg[] = {
	{
		.reg = PAD_SHARED_IP_EN_1,
		.mask = CAM3_MASK,
		.val = 0,
	}, {
		.reg = PAD_FUNCTION_EN_1,
		.mask = VIP_AND_CAM3_REG0_MASK,
		.val = VIP_AND_CAM3_REG0_MASK,
	}, {
		.reg = PAD_FUNCTION_EN_2,
		.mask = VIP_AND_CAM3_REG1_MASK,
		.val = VIP_AND_CAM3_REG1_MASK,
	},
};

static struct spear_modemux vip_mux_cam3_modemux[] = {
	{
		.muxregs = vip_mux_cam3_muxreg,
		.nmuxregs = ARRAY_SIZE(vip_mux_cam3_muxreg),
	},
};

static struct spear_pingroup vip_mux_cam3_pingroup = {
	.name = "vip_mux_cam3_grp",
	.pins = vip_mux_cam3_pins,
	.npins = ARRAY_SIZE(vip_mux_cam3_pins),
	.modemuxs = vip_mux_cam3_modemux,
	.nmodemuxs = ARRAY_SIZE(vip_mux_cam3_modemux),
};

static const char *const vip_grps[] = { "vip_mux_grp", "vip_mux_cam0_grp" ,
	"vip_mux_cam1_grp" , "vip_mux_cam2_grp", "vip_mux_cam3_grp" };
static struct spear_function vip_function = {
	.name = "vip",
	.groups = vip_grps,
	.ngroups = ARRAY_SIZE(vip_grps),
};

/* pad multiplexing for cam0 device */
static const unsigned cam0_pins[] = { 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75
};
static struct spear_muxreg cam0_muxreg[] = {
	{
		.reg = PAD_SHARED_IP_EN_1,
		.mask = CAM0_MASK,
		.val = CAM0_MASK,
	}, {
		.reg = PAD_FUNCTION_EN_3,
		.mask = VIP_AND_CAM0_REG2_MASK,
		.val = VIP_AND_CAM0_REG2_MASK,
	},
};

static struct spear_modemux cam0_modemux[] = {
	{
		.muxregs = cam0_muxreg,
		.nmuxregs = ARRAY_SIZE(cam0_muxreg),
	},
};

static struct spear_pingroup cam0_pingroup = {
	.name = "cam0_grp",
	.pins = cam0_pins,
	.npins = ARRAY_SIZE(cam0_pins),
	.modemuxs = cam0_modemux,
	.nmodemuxs = ARRAY_SIZE(cam0_modemux),
};

static const char *const cam0_grps[] = { "cam0_grp" };
static struct spear_function cam0_function = {
	.name = "cam0",
	.groups = cam0_grps,
	.ngroups = ARRAY_SIZE(cam0_grps),
};

/* pad multiplexing for cam1 device */
static const unsigned cam1_pins[] = { 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64
};
static struct spear_muxreg cam1_muxreg[] = {
	{
		.reg = PAD_SHARED_IP_EN_1,
		.mask = CAM1_MASK,
		.val = CAM1_MASK,
	}, {
		.reg = PAD_FUNCTION_EN_2,
		.mask = VIP_AND_CAM1_REG1_MASK,
		.val = VIP_AND_CAM1_REG1_MASK,
	}, {
		.reg = PAD_FUNCTION_EN_3,
		.mask = VIP_AND_CAM1_REG2_MASK,
		.val = VIP_AND_CAM1_REG2_MASK,
	},
};

static struct spear_modemux cam1_modemux[] = {
	{
		.muxregs = cam1_muxreg,
		.nmuxregs = ARRAY_SIZE(cam1_muxreg),
	},
};

static struct spear_pingroup cam1_pingroup = {
	.name = "cam1_grp",
	.pins = cam1_pins,
	.npins = ARRAY_SIZE(cam1_pins),
	.modemuxs = cam1_modemux,
	.nmodemuxs = ARRAY_SIZE(cam1_modemux),
};

static const char *const cam1_grps[] = { "cam1_grp" };
static struct spear_function cam1_function = {
	.name = "cam1",
	.groups = cam1_grps,
	.ngroups = ARRAY_SIZE(cam1_grps),
};

/* pad multiplexing for cam2 device */
static const unsigned cam2_pins[] = { 39, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53
};
static struct spear_muxreg cam2_muxreg[] = {
	{
		.reg = PAD_SHARED_IP_EN_1,
		.mask = CAM2_MASK,
		.val = CAM2_MASK,
	}, {
		.reg = PAD_FUNCTION_EN_2,
		.mask = VIP_AND_CAM2_REG1_MASK,
		.val = VIP_AND_CAM2_REG1_MASK,
	},
};

static struct spear_modemux cam2_modemux[] = {
	{
		.muxregs = cam2_muxreg,
		.nmuxregs = ARRAY_SIZE(cam2_muxreg),
	},
};

static struct spear_pingroup cam2_pingroup = {
	.name = "cam2_grp",
	.pins = cam2_pins,
	.npins = ARRAY_SIZE(cam2_pins),
	.modemuxs = cam2_modemux,
	.nmodemuxs = ARRAY_SIZE(cam2_modemux),
};

static const char *const cam2_grps[] = { "cam2_grp" };
static struct spear_function cam2_function = {
	.name = "cam2",
	.groups = cam2_grps,
	.ngroups = ARRAY_SIZE(cam2_grps),
};

/* pad multiplexing for cam3 device */
static const unsigned cam3_pins[] = { 20, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34
};
static struct spear_muxreg cam3_muxreg[] = {
	{
		.reg = PAD_SHARED_IP_EN_1,
		.mask = CAM3_MASK,
		.val = CAM3_MASK,
	}, {
		.reg = PAD_FUNCTION_EN_1,
		.mask = VIP_AND_CAM3_REG0_MASK,
		.val = VIP_AND_CAM3_REG0_MASK,
	}, {
		.reg = PAD_FUNCTION_EN_2,
		.mask = VIP_AND_CAM3_REG1_MASK,
		.val = VIP_AND_CAM3_REG1_MASK,
	},
};

static struct spear_modemux cam3_modemux[] = {
	{
		.muxregs = cam3_muxreg,
		.nmuxregs = ARRAY_SIZE(cam3_muxreg),
	},
};

static struct spear_pingroup cam3_pingroup = {
	.name = "cam3_grp",
	.pins = cam3_pins,
	.npins = ARRAY_SIZE(cam3_pins),
	.modemuxs = cam3_modemux,
	.nmodemuxs = ARRAY_SIZE(cam3_modemux),
};

static const char *const cam3_grps[] = { "cam3_grp" };
static struct spear_function cam3_function = {
	.name = "cam3",
	.groups = cam3_grps,
	.ngroups = ARRAY_SIZE(cam3_grps),
};

/* pad multiplexing for smi device */
static const unsigned smi_pins[] = { 76, 77, 78, 79, 84 };
static struct spear_muxreg smi_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_3,
		.mask = SMI_REG2_MASK,
		.val = SMI_REG2_MASK,
	},
};

static struct spear_modemux smi_modemux[] = {
	{
		.muxregs = smi_muxreg,
		.nmuxregs = ARRAY_SIZE(smi_muxreg),
	},
};

static struct spear_pingroup smi_pingroup = {
	.name = "smi_grp",
	.pins = smi_pins,
	.npins = ARRAY_SIZE(smi_pins),
	.modemuxs = smi_modemux,
	.nmodemuxs = ARRAY_SIZE(smi_modemux),
};

static const char *const smi_grps[] = { "smi_grp" };
static struct spear_function smi_function = {
	.name = "smi",
	.groups = smi_grps,
	.ngroups = ARRAY_SIZE(smi_grps),
};

/* pad multiplexing for ssp0 device */
static const unsigned ssp0_pins[] = { 80, 81, 82, 83 };
static struct spear_muxreg ssp0_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_3,
		.mask = SSP0_REG2_MASK,
		.val = SSP0_REG2_MASK,
	},
};

static struct spear_modemux ssp0_modemux[] = {
	{
		.muxregs = ssp0_muxreg,
		.nmuxregs = ARRAY_SIZE(ssp0_muxreg),
	},
};

static struct spear_pingroup ssp0_pingroup = {
	.name = "ssp0_grp",
	.pins = ssp0_pins,
	.npins = ARRAY_SIZE(ssp0_pins),
	.modemuxs = ssp0_modemux,
	.nmodemuxs = ARRAY_SIZE(ssp0_modemux),
};

/* pad multiplexing for ssp0_cs1 device */
static const unsigned ssp0_cs1_pins[] = { 24 };
static struct spear_muxreg ssp0_cs1_muxreg[] = {
	{
		.reg = PAD_SHARED_IP_EN_1,
		.mask = SSP0_CS1_MASK,
		.val = SSP0_CS1_MASK,
	}, {
		.reg = PAD_FUNCTION_EN_1,
		.mask = PWM0_AND_SSP0_CS1_REG0_MASK,
		.val = PWM0_AND_SSP0_CS1_REG0_MASK,
	},
};

static struct spear_modemux ssp0_cs1_modemux[] = {
	{
		.muxregs = ssp0_cs1_muxreg,
		.nmuxregs = ARRAY_SIZE(ssp0_cs1_muxreg),
	},
};

static struct spear_pingroup ssp0_cs1_pingroup = {
	.name = "ssp0_cs1_grp",
	.pins = ssp0_cs1_pins,
	.npins = ARRAY_SIZE(ssp0_cs1_pins),
	.modemuxs = ssp0_cs1_modemux,
	.nmodemuxs = ARRAY_SIZE(ssp0_cs1_modemux),
};

/* pad multiplexing for ssp0_cs2 device */
static const unsigned ssp0_cs2_pins[] = { 85 };
static struct spear_muxreg ssp0_cs2_muxreg[] = {
	{
		.reg = PAD_SHARED_IP_EN_1,
		.mask = SSP0_CS2_MASK,
		.val = SSP0_CS2_MASK,
	}, {
		.reg = PAD_FUNCTION_EN_3,
		.mask = TS_AND_SSP0_CS2_REG2_MASK,
		.val = TS_AND_SSP0_CS2_REG2_MASK,
	},
};

static struct spear_modemux ssp0_cs2_modemux[] = {
	{
		.muxregs = ssp0_cs2_muxreg,
		.nmuxregs = ARRAY_SIZE(ssp0_cs2_muxreg),
	},
};

static struct spear_pingroup ssp0_cs2_pingroup = {
	.name = "ssp0_cs2_grp",
	.pins = ssp0_cs2_pins,
	.npins = ARRAY_SIZE(ssp0_cs2_pins),
	.modemuxs = ssp0_cs2_modemux,
	.nmodemuxs = ARRAY_SIZE(ssp0_cs2_modemux),
};

/* pad multiplexing for ssp0_cs3 device */
static const unsigned ssp0_cs3_pins[] = { 132 };
static struct spear_muxreg ssp0_cs3_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_5,
		.mask = SSP0_CS3_REG4_MASK,
		.val = SSP0_CS3_REG4_MASK,
	},
};

static struct spear_modemux ssp0_cs3_modemux[] = {
	{
		.muxregs = ssp0_cs3_muxreg,
		.nmuxregs = ARRAY_SIZE(ssp0_cs3_muxreg),
	},
};

static struct spear_pingroup ssp0_cs3_pingroup = {
	.name = "ssp0_cs3_grp",
	.pins = ssp0_cs3_pins,
	.npins = ARRAY_SIZE(ssp0_cs3_pins),
	.modemuxs = ssp0_cs3_modemux,
	.nmodemuxs = ARRAY_SIZE(ssp0_cs3_modemux),
};

static const char *const ssp0_grps[] = { "ssp0_grp", "ssp0_cs1_grp",
	"ssp0_cs2_grp", "ssp0_cs3_grp" };
static struct spear_function ssp0_function = {
	.name = "ssp0",
	.groups = ssp0_grps,
	.ngroups = ARRAY_SIZE(ssp0_grps),
};

/* pad multiplexing for uart0 device */
static const unsigned uart0_pins[] = { 86, 87 };
static struct spear_muxreg uart0_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_3,
		.mask = UART0_REG2_MASK,
		.val = UART0_REG2_MASK,
	},
};

static struct spear_modemux uart0_modemux[] = {
	{
		.muxregs = uart0_muxreg,
		.nmuxregs = ARRAY_SIZE(uart0_muxreg),
	},
};

static struct spear_pingroup uart0_pingroup = {
	.name = "uart0_grp",
	.pins = uart0_pins,
	.npins = ARRAY_SIZE(uart0_pins),
	.modemuxs = uart0_modemux,
	.nmodemuxs = ARRAY_SIZE(uart0_modemux),
};

/* pad multiplexing for uart0_enh device */
static const unsigned uart0_enh_pins[] = { 11, 12, 13, 14, 15, 16 };
static struct spear_muxreg uart0_enh_muxreg[] = {
	{
		.reg = PAD_SHARED_IP_EN_1,
		.mask = GPT_MASK,
		.val = 0,
	}, {
		.reg = PAD_FUNCTION_EN_1,
		.mask = UART0_ENH_AND_GPT_REG0_MASK,
		.val = UART0_ENH_AND_GPT_REG0_MASK,
	},
};

static struct spear_modemux uart0_enh_modemux[] = {
	{
		.muxregs = uart0_enh_muxreg,
		.nmuxregs = ARRAY_SIZE(uart0_enh_muxreg),
	},
};

static struct spear_pingroup uart0_enh_pingroup = {
	.name = "uart0_enh_grp",
	.pins = uart0_enh_pins,
	.npins = ARRAY_SIZE(uart0_enh_pins),
	.modemuxs = uart0_enh_modemux,
	.nmodemuxs = ARRAY_SIZE(uart0_enh_modemux),
};

static const char *const uart0_grps[] = { "uart0_grp", "uart0_enh_grp" };
static struct spear_function uart0_function = {
	.name = "uart0",
	.groups = uart0_grps,
	.ngroups = ARRAY_SIZE(uart0_grps),
};

/* pad multiplexing for uart1 device */
static const unsigned uart1_pins[] = { 88, 89 };
static struct spear_muxreg uart1_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_3,
		.mask = UART1_REG2_MASK,
		.val = UART1_REG2_MASK,
	},
};

static struct spear_modemux uart1_modemux[] = {
	{
		.muxregs = uart1_muxreg,
		.nmuxregs = ARRAY_SIZE(uart1_muxreg),
	},
};

static struct spear_pingroup uart1_pingroup = {
	.name = "uart1_grp",
	.pins = uart1_pins,
	.npins = ARRAY_SIZE(uart1_pins),
	.modemuxs = uart1_modemux,
	.nmodemuxs = ARRAY_SIZE(uart1_modemux),
};

static const char *const uart1_grps[] = { "uart1_grp" };
static struct spear_function uart1_function = {
	.name = "uart1",
	.groups = uart1_grps,
	.ngroups = ARRAY_SIZE(uart1_grps),
};

/* pad multiplexing for i2s_in device */
static const unsigned i2s_in_pins[] = { 90, 91, 92, 93, 94, 99 };
static struct spear_muxreg i2s_in_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_3,
		.mask = I2S_IN_REG2_MASK,
		.val = I2S_IN_REG2_MASK,
	}, {
		.reg = PAD_FUNCTION_EN_4,
		.mask = I2S_IN_REG3_MASK,
		.val = I2S_IN_REG3_MASK,
	},
};

static struct spear_modemux i2s_in_modemux[] = {
	{
		.muxregs = i2s_in_muxreg,
		.nmuxregs = ARRAY_SIZE(i2s_in_muxreg),
	},
};

static struct spear_pingroup i2s_in_pingroup = {
	.name = "i2s_in_grp",
	.pins = i2s_in_pins,
	.npins = ARRAY_SIZE(i2s_in_pins),
	.modemuxs = i2s_in_modemux,
	.nmodemuxs = ARRAY_SIZE(i2s_in_modemux),
};

/* pad multiplexing for i2s_out device */
static const unsigned i2s_out_pins[] = { 95, 96, 97, 98, 100, 101, 102, 103 };
static struct spear_muxreg i2s_out_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_4,
		.mask = I2S_OUT_REG3_MASK,
		.val = I2S_OUT_REG3_MASK,
	},
};

static struct spear_modemux i2s_out_modemux[] = {
	{
		.muxregs = i2s_out_muxreg,
		.nmuxregs = ARRAY_SIZE(i2s_out_muxreg),
	},
};

static struct spear_pingroup i2s_out_pingroup = {
	.name = "i2s_out_grp",
	.pins = i2s_out_pins,
	.npins = ARRAY_SIZE(i2s_out_pins),
	.modemuxs = i2s_out_modemux,
	.nmodemuxs = ARRAY_SIZE(i2s_out_modemux),
};

static const char *const i2s_grps[] = { "i2s_in_grp", "i2s_out_grp" };
static struct spear_function i2s_function = {
	.name = "i2s",
	.groups = i2s_grps,
	.ngroups = ARRAY_SIZE(i2s_grps),
};

/* pad multiplexing for gmac device */
static const unsigned gmac_pins[] = { 104, 105, 106, 107, 108, 109, 110, 111,
	112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125,
	126, 127, 128, 129, 130, 131 };
#define GMAC_MUXREG				\
	{					\
		.reg = PAD_FUNCTION_EN_4,	\
		.mask = GMAC_REG3_MASK,		\
		.val = GMAC_REG3_MASK,		\
	}, {					\
		.reg = PAD_FUNCTION_EN_5,	\
		.mask = GMAC_REG4_MASK,		\
		.val = GMAC_REG4_MASK,		\
	}

/* pad multiplexing for gmii device */
static struct spear_muxreg gmii_muxreg[] = {
	GMAC_MUXREG,
	{
		.reg = GMAC_CLK_CFG,
		.mask = GMAC_PHY_IF_SEL_MASK,
		.val = GMAC_PHY_IF_GMII_VAL,
	},
};

static struct spear_modemux gmii_modemux[] = {
	{
		.muxregs = gmii_muxreg,
		.nmuxregs = ARRAY_SIZE(gmii_muxreg),
	},
};

static struct spear_pingroup gmii_pingroup = {
	.name = "gmii_grp",
	.pins = gmac_pins,
	.npins = ARRAY_SIZE(gmac_pins),
	.modemuxs = gmii_modemux,
	.nmodemuxs = ARRAY_SIZE(gmii_modemux),
};

/* pad multiplexing for rgmii device */
static struct spear_muxreg rgmii_muxreg[] = {
	GMAC_MUXREG,
	{
		.reg = GMAC_CLK_CFG,
		.mask = GMAC_PHY_IF_SEL_MASK,
		.val = GMAC_PHY_IF_RGMII_VAL,
	},
};

static struct spear_modemux rgmii_modemux[] = {
	{
		.muxregs = rgmii_muxreg,
		.nmuxregs = ARRAY_SIZE(rgmii_muxreg),
	},
};

static struct spear_pingroup rgmii_pingroup = {
	.name = "rgmii_grp",
	.pins = gmac_pins,
	.npins = ARRAY_SIZE(gmac_pins),
	.modemuxs = rgmii_modemux,
	.nmodemuxs = ARRAY_SIZE(rgmii_modemux),
};

/* pad multiplexing for rmii device */
static struct spear_muxreg rmii_muxreg[] = {
	GMAC_MUXREG,
	{
		.reg = GMAC_CLK_CFG,
		.mask = GMAC_PHY_IF_SEL_MASK,
		.val = GMAC_PHY_IF_RMII_VAL,
	},
};

static struct spear_modemux rmii_modemux[] = {
	{
		.muxregs = rmii_muxreg,
		.nmuxregs = ARRAY_SIZE(rmii_muxreg),
	},
};

static struct spear_pingroup rmii_pingroup = {
	.name = "rmii_grp",
	.pins = gmac_pins,
	.npins = ARRAY_SIZE(gmac_pins),
	.modemuxs = rmii_modemux,
	.nmodemuxs = ARRAY_SIZE(rmii_modemux),
};

/* pad multiplexing for sgmii device */
static struct spear_muxreg sgmii_muxreg[] = {
	GMAC_MUXREG,
	{
		.reg = GMAC_CLK_CFG,
		.mask = GMAC_PHY_IF_SEL_MASK,
		.val = GMAC_PHY_IF_SGMII_VAL,
	},
};

static struct spear_modemux sgmii_modemux[] = {
	{
		.muxregs = sgmii_muxreg,
		.nmuxregs = ARRAY_SIZE(sgmii_muxreg),
	},
};

static struct spear_pingroup sgmii_pingroup = {
	.name = "sgmii_grp",
	.pins = gmac_pins,
	.npins = ARRAY_SIZE(gmac_pins),
	.modemuxs = sgmii_modemux,
	.nmodemuxs = ARRAY_SIZE(sgmii_modemux),
};

static const char *const gmac_grps[] = { "gmii_grp", "rgmii_grp", "rmii_grp",
	"sgmii_grp" };
static struct spear_function gmac_function = {
	.name = "gmac",
	.groups = gmac_grps,
	.ngroups = ARRAY_SIZE(gmac_grps),
};

/* pad multiplexing for i2c0 device */
static const unsigned i2c0_pins[] = { 133, 134 };
static struct spear_muxreg i2c0_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_5,
		.mask = I2C0_REG4_MASK,
		.val = I2C0_REG4_MASK,
	},
};

static struct spear_modemux i2c0_modemux[] = {
	{
		.muxregs = i2c0_muxreg,
		.nmuxregs = ARRAY_SIZE(i2c0_muxreg),
	},
};

static struct spear_pingroup i2c0_pingroup = {
	.name = "i2c0_grp",
	.pins = i2c0_pins,
	.npins = ARRAY_SIZE(i2c0_pins),
	.modemuxs = i2c0_modemux,
	.nmodemuxs = ARRAY_SIZE(i2c0_modemux),
};

static const char *const i2c0_grps[] = { "i2c0_grp" };
static struct spear_function i2c0_function = {
	.name = "i2c0",
	.groups = i2c0_grps,
	.ngroups = ARRAY_SIZE(i2c0_grps),
};

/* pad multiplexing for i2c1 device */
static const unsigned i2c1_pins[] = { 18, 23 };
static struct spear_muxreg i2c1_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_1,
		.mask = I2C1_REG0_MASK,
		.val = I2C1_REG0_MASK,
	},
};

static struct spear_modemux i2c1_modemux[] = {
	{
		.muxregs = i2c1_muxreg,
		.nmuxregs = ARRAY_SIZE(i2c1_muxreg),
	},
};

static struct spear_pingroup i2c1_pingroup = {
	.name = "i2c1_grp",
	.pins = i2c1_pins,
	.npins = ARRAY_SIZE(i2c1_pins),
	.modemuxs = i2c1_modemux,
	.nmodemuxs = ARRAY_SIZE(i2c1_modemux),
};

static const char *const i2c1_grps[] = { "i2c1_grp" };
static struct spear_function i2c1_function = {
	.name = "i2c1",
	.groups = i2c1_grps,
	.ngroups = ARRAY_SIZE(i2c1_grps),
};

/* pad multiplexing for cec0 device */
static const unsigned cec0_pins[] = { 135 };
static struct spear_muxreg cec0_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_5,
		.mask = CEC0_REG4_MASK,
		.val = CEC0_REG4_MASK,
	},
};

static struct spear_modemux cec0_modemux[] = {
	{
		.muxregs = cec0_muxreg,
		.nmuxregs = ARRAY_SIZE(cec0_muxreg),
	},
};

static struct spear_pingroup cec0_pingroup = {
	.name = "cec0_grp",
	.pins = cec0_pins,
	.npins = ARRAY_SIZE(cec0_pins),
	.modemuxs = cec0_modemux,
	.nmodemuxs = ARRAY_SIZE(cec0_modemux),
};

static const char *const cec0_grps[] = { "cec0_grp" };
static struct spear_function cec0_function = {
	.name = "cec0",
	.groups = cec0_grps,
	.ngroups = ARRAY_SIZE(cec0_grps),
};

/* pad multiplexing for cec1 device */
static const unsigned cec1_pins[] = { 136 };
static struct spear_muxreg cec1_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_5,
		.mask = CEC1_REG4_MASK,
		.val = CEC1_REG4_MASK,
	},
};

static struct spear_modemux cec1_modemux[] = {
	{
		.muxregs = cec1_muxreg,
		.nmuxregs = ARRAY_SIZE(cec1_muxreg),
	},
};

static struct spear_pingroup cec1_pingroup = {
	.name = "cec1_grp",
	.pins = cec1_pins,
	.npins = ARRAY_SIZE(cec1_pins),
	.modemuxs = cec1_modemux,
	.nmodemuxs = ARRAY_SIZE(cec1_modemux),
};

static const char *const cec1_grps[] = { "cec1_grp" };
static struct spear_function cec1_function = {
	.name = "cec1",
	.groups = cec1_grps,
	.ngroups = ARRAY_SIZE(cec1_grps),
};

/* pad multiplexing for mcif devices */
static const unsigned mcif_pins[] = { 193, 194, 195, 196, 197, 198, 199, 200,
	201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214,
	215, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228,
	229, 230, 231, 232, 237 };
#define MCIF_MUXREG							\
	{								\
		.reg = PAD_SHARED_IP_EN_1,				\
		.mask = MCIF_MASK,					\
		.val = MCIF_MASK,					\
	}, {								\
		.reg = PAD_FUNCTION_EN_7,				\
		.mask = FSMC_PNOR_AND_MCIF_REG6_MASK | MCIF_REG6_MASK,	\
		.val = FSMC_PNOR_AND_MCIF_REG6_MASK | MCIF_REG6_MASK,	\
	}, {								\
		.reg = PAD_FUNCTION_EN_8,				\
		.mask = MCIF_REG7_MASK,					\
		.val = MCIF_REG7_MASK,					\
	}

/* Pad multiplexing for sdhci device */
static struct spear_muxreg sdhci_muxreg[] = {
	MCIF_MUXREG,
	{
		.reg = PERIP_CFG,
		.mask = MCIF_SEL_MASK,
		.val = MCIF_SEL_SD,
	},
};

static struct spear_modemux sdhci_modemux[] = {
	{
		.muxregs = sdhci_muxreg,
		.nmuxregs = ARRAY_SIZE(sdhci_muxreg),
	},
};

static struct spear_pingroup sdhci_pingroup = {
	.name = "sdhci_grp",
	.pins = mcif_pins,
	.npins = ARRAY_SIZE(mcif_pins),
	.modemuxs = sdhci_modemux,
	.nmodemuxs = ARRAY_SIZE(sdhci_modemux),
};

static const char *const sdhci_grps[] = { "sdhci_grp" };
static struct spear_function sdhci_function = {
	.name = "sdhci",
	.groups = sdhci_grps,
	.ngroups = ARRAY_SIZE(sdhci_grps),
};

/* Pad multiplexing for cf device */
static struct spear_muxreg cf_muxreg[] = {
	MCIF_MUXREG,
	{
		.reg = PERIP_CFG,
		.mask = MCIF_SEL_MASK,
		.val = MCIF_SEL_CF,
	},
};

static struct spear_modemux cf_modemux[] = {
	{
		.muxregs = cf_muxreg,
		.nmuxregs = ARRAY_SIZE(cf_muxreg),
	},
};

static struct spear_pingroup cf_pingroup = {
	.name = "cf_grp",
	.pins = mcif_pins,
	.npins = ARRAY_SIZE(mcif_pins),
	.modemuxs = cf_modemux,
	.nmodemuxs = ARRAY_SIZE(cf_modemux),
};

static const char *const cf_grps[] = { "cf_grp" };
static struct spear_function cf_function = {
	.name = "cf",
	.groups = cf_grps,
	.ngroups = ARRAY_SIZE(cf_grps),
};

/* Pad multiplexing for xd device */
static struct spear_muxreg xd_muxreg[] = {
	MCIF_MUXREG,
	{
		.reg = PERIP_CFG,
		.mask = MCIF_SEL_MASK,
		.val = MCIF_SEL_XD,
	},
};

static struct spear_modemux xd_modemux[] = {
	{
		.muxregs = xd_muxreg,
		.nmuxregs = ARRAY_SIZE(xd_muxreg),
	},
};

static struct spear_pingroup xd_pingroup = {
	.name = "xd_grp",
	.pins = mcif_pins,
	.npins = ARRAY_SIZE(mcif_pins),
	.modemuxs = xd_modemux,
	.nmodemuxs = ARRAY_SIZE(xd_modemux),
};

static const char *const xd_grps[] = { "xd_grp" };
static struct spear_function xd_function = {
	.name = "xd",
	.groups = xd_grps,
	.ngroups = ARRAY_SIZE(xd_grps),
};

/* pad multiplexing for clcd device */
static const unsigned clcd_pins[] = { 138, 139, 140, 141, 142, 143, 144, 145,
	146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159,
	160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173,
	174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187,
	188, 189, 190, 191 };
static struct spear_muxreg clcd_muxreg[] = {
	{
		.reg = PAD_SHARED_IP_EN_1,
		.mask = ARM_TRACE_MASK | MIPHY_DBG_MASK,
		.val = 0,
	}, {
		.reg = PAD_FUNCTION_EN_5,
		.mask = CLCD_REG4_MASK | CLCD_AND_ARM_TRACE_REG4_MASK,
		.val = CLCD_REG4_MASK | CLCD_AND_ARM_TRACE_REG4_MASK,
	}, {
		.reg = PAD_FUNCTION_EN_6,
		.mask = CLCD_AND_ARM_TRACE_REG5_MASK,
		.val = CLCD_AND_ARM_TRACE_REG5_MASK,
	}, {
		.reg = PAD_FUNCTION_EN_7,
		.mask = CLCD_AND_ARM_TRACE_REG6_MASK,
		.val = CLCD_AND_ARM_TRACE_REG6_MASK,
	},
};

static struct spear_modemux clcd_modemux[] = {
	{
		.muxregs = clcd_muxreg,
		.nmuxregs = ARRAY_SIZE(clcd_muxreg),
	},
};

static struct spear_pingroup clcd_pingroup = {
	.name = "clcd_grp",
	.pins = clcd_pins,
	.npins = ARRAY_SIZE(clcd_pins),
	.modemuxs = clcd_modemux,
	.nmodemuxs = ARRAY_SIZE(clcd_modemux),
};

/* Disable cld runtime to save panel damage */
static struct spear_muxreg clcd_sleep_muxreg[] = {
	{
		.reg = PAD_SHARED_IP_EN_1,
		.mask = ARM_TRACE_MASK | MIPHY_DBG_MASK,
		.val = 0,
	}, {
		.reg = PAD_FUNCTION_EN_5,
		.mask = CLCD_REG4_MASK | CLCD_AND_ARM_TRACE_REG4_MASK,
		.val = 0x0,
	}, {
		.reg = PAD_FUNCTION_EN_6,
		.mask = CLCD_AND_ARM_TRACE_REG5_MASK,
		.val = 0x0,
	}, {
		.reg = PAD_FUNCTION_EN_7,
		.mask = CLCD_AND_ARM_TRACE_REG6_MASK,
		.val = 0x0,
	},
};

static struct spear_modemux clcd_sleep_modemux[] = {
	{
		.muxregs = clcd_sleep_muxreg,
		.nmuxregs = ARRAY_SIZE(clcd_sleep_muxreg),
	},
};

static struct spear_pingroup clcd_sleep_pingroup = {
	.name = "clcd_sleep_grp",
	.pins = clcd_pins,
	.npins = ARRAY_SIZE(clcd_pins),
	.modemuxs = clcd_sleep_modemux,
	.nmodemuxs = ARRAY_SIZE(clcd_sleep_modemux),
};

static const char *const clcd_grps[] = { "clcd_grp", "clcd_sleep_grp" };
static struct spear_function clcd_function = {
	.name = "clcd",
	.groups = clcd_grps,
	.ngroups = ARRAY_SIZE(clcd_grps),
};

/* pad multiplexing for arm_trace device */
static const unsigned arm_trace_pins[] = { 158, 159, 160, 161, 162, 163, 164,
	165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178,
	179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192,
	193, 194, 195, 196, 197, 198, 199, 200 };
static struct spear_muxreg arm_trace_muxreg[] = {
	{
		.reg = PAD_SHARED_IP_EN_1,
		.mask = ARM_TRACE_MASK,
		.val = ARM_TRACE_MASK,
	}, {
		.reg = PAD_FUNCTION_EN_5,
		.mask = CLCD_AND_ARM_TRACE_REG4_MASK,
		.val = CLCD_AND_ARM_TRACE_REG4_MASK,
	}, {
		.reg = PAD_FUNCTION_EN_6,
		.mask = CLCD_AND_ARM_TRACE_REG5_MASK,
		.val = CLCD_AND_ARM_TRACE_REG5_MASK,
	}, {
		.reg = PAD_FUNCTION_EN_7,
		.mask = CLCD_AND_ARM_TRACE_REG6_MASK,
		.val = CLCD_AND_ARM_TRACE_REG6_MASK,
	},
};

static struct spear_modemux arm_trace_modemux[] = {
	{
		.muxregs = arm_trace_muxreg,
		.nmuxregs = ARRAY_SIZE(arm_trace_muxreg),
	},
};

static struct spear_pingroup arm_trace_pingroup = {
	.name = "arm_trace_grp",
	.pins = arm_trace_pins,
	.npins = ARRAY_SIZE(arm_trace_pins),
	.modemuxs = arm_trace_modemux,
	.nmodemuxs = ARRAY_SIZE(arm_trace_modemux),
};

static const char *const arm_trace_grps[] = { "arm_trace_grp" };
static struct spear_function arm_trace_function = {
	.name = "arm_trace",
	.groups = arm_trace_grps,
	.ngroups = ARRAY_SIZE(arm_trace_grps),
};

/* pad multiplexing for miphy_dbg device */
static const unsigned miphy_dbg_pins[] = { 96, 97, 98, 99, 100, 101, 102, 103,
	132, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147,
	148, 149, 150, 151, 152, 153, 154, 155, 156, 157 };
static struct spear_muxreg miphy_dbg_muxreg[] = {
	{
		.reg = PAD_SHARED_IP_EN_1,
		.mask = MIPHY_DBG_MASK,
		.val = MIPHY_DBG_MASK,
	}, {
		.reg = PAD_FUNCTION_EN_5,
		.mask = DEVS_GRP_AND_MIPHY_DBG_REG4_MASK,
		.val = DEVS_GRP_AND_MIPHY_DBG_REG4_MASK,
	},
};

static struct spear_modemux miphy_dbg_modemux[] = {
	{
		.muxregs = miphy_dbg_muxreg,
		.nmuxregs = ARRAY_SIZE(miphy_dbg_muxreg),
	},
};

static struct spear_pingroup miphy_dbg_pingroup = {
	.name = "miphy_dbg_grp",
	.pins = miphy_dbg_pins,
	.npins = ARRAY_SIZE(miphy_dbg_pins),
	.modemuxs = miphy_dbg_modemux,
	.nmodemuxs = ARRAY_SIZE(miphy_dbg_modemux),
};

static const char *const miphy_dbg_grps[] = { "miphy_dbg_grp" };
static struct spear_function miphy_dbg_function = {
	.name = "miphy_dbg",
	.groups = miphy_dbg_grps,
	.ngroups = ARRAY_SIZE(miphy_dbg_grps),
};

/* pad multiplexing for pcie device */
static const unsigned pcie_pins[] = { 250 };
static struct spear_muxreg pcie_muxreg[] = {
	{
		.reg = PCIE_SATA_CFG,
		.mask = SATA_PCIE_CFG_MASK,
		.val = PCIE_CFG_VAL,
	},
};

static struct spear_modemux pcie_modemux[] = {
	{
		.muxregs = pcie_muxreg,
		.nmuxregs = ARRAY_SIZE(pcie_muxreg),
	},
};

static struct spear_pingroup pcie_pingroup = {
	.name = "pcie_grp",
	.pins = pcie_pins,
	.npins = ARRAY_SIZE(pcie_pins),
	.modemuxs = pcie_modemux,
	.nmodemuxs = ARRAY_SIZE(pcie_modemux),
};

static const char *const pcie_grps[] = { "pcie_grp" };
static struct spear_function pcie_function = {
	.name = "pcie",
	.groups = pcie_grps,
	.ngroups = ARRAY_SIZE(pcie_grps),
};

/* pad multiplexing for sata device */
static const unsigned sata_pins[] = { 250 };
static struct spear_muxreg sata_muxreg[] = {
	{
		.reg = PCIE_SATA_CFG,
		.mask = SATA_PCIE_CFG_MASK,
		.val = SATA_CFG_VAL,
	},
};

static struct spear_modemux sata_modemux[] = {
	{
		.muxregs = sata_muxreg,
		.nmuxregs = ARRAY_SIZE(sata_muxreg),
	},
};

static struct spear_pingroup sata_pingroup = {
	.name = "sata_grp",
	.pins = sata_pins,
	.npins = ARRAY_SIZE(sata_pins),
	.modemuxs = sata_modemux,
	.nmodemuxs = ARRAY_SIZE(sata_modemux),
};

static const char *const sata_grps[] = { "sata_grp" };
static struct spear_function sata_function = {
	.name = "sata",
	.groups = sata_grps,
	.ngroups = ARRAY_SIZE(sata_grps),
};

/* pingroups */
static struct spear_pingroup *spear1340_pingroups[] = {
	&pads_as_gpio_pingroup,
	&fsmc_8bit_pingroup,
	&fsmc_16bit_pingroup,
	&fsmc_pnor_pingroup,
	&keyboard_row_col_pingroup,
	&keyboard_col5_pingroup,
	&spdif_in_pingroup,
	&spdif_out_pingroup,
	&gpt_0_1_pingroup,
	&pwm0_pingroup,
	&pwm1_pingroup,
	&pwm2_pingroup,
	&pwm3_pingroup,
	&vip_mux_pingroup,
	&vip_mux_cam0_pingroup,
	&vip_mux_cam1_pingroup,
	&vip_mux_cam2_pingroup,
	&vip_mux_cam3_pingroup,
	&cam0_pingroup,
	&cam1_pingroup,
	&cam2_pingroup,
	&cam3_pingroup,
	&smi_pingroup,
	&ssp0_pingroup,
	&ssp0_cs1_pingroup,
	&ssp0_cs2_pingroup,
	&ssp0_cs3_pingroup,
	&uart0_pingroup,
	&uart0_enh_pingroup,
	&uart1_pingroup,
	&i2s_in_pingroup,
	&i2s_out_pingroup,
	&gmii_pingroup,
	&rgmii_pingroup,
	&rmii_pingroup,
	&sgmii_pingroup,
	&i2c0_pingroup,
	&i2c1_pingroup,
	&cec0_pingroup,
	&cec1_pingroup,
	&sdhci_pingroup,
	&cf_pingroup,
	&xd_pingroup,
	&clcd_sleep_pingroup,
	&clcd_pingroup,
	&arm_trace_pingroup,
	&miphy_dbg_pingroup,
	&pcie_pingroup,
	&sata_pingroup,
};

/* functions */
static struct spear_function *spear1340_functions[] = {
	&pads_as_gpio_function,
	&fsmc_function,
	&keyboard_function,
	&spdif_in_function,
	&spdif_out_function,
	&gpt_0_1_function,
	&pwm_function,
	&vip_function,
	&cam0_function,
	&cam1_function,
	&cam2_function,
	&cam3_function,
	&smi_function,
	&ssp0_function,
	&uart0_function,
	&uart1_function,
	&i2s_function,
	&gmac_function,
	&i2c0_function,
	&i2c1_function,
	&cec0_function,
	&cec1_function,
	&sdhci_function,
	&cf_function,
	&xd_function,
	&clcd_function,
	&arm_trace_function,
	&miphy_dbg_function,
	&pcie_function,
	&sata_function,
};

static void gpio_request_endisable(struct spear_pmx *pmx, int pin,
		bool enable)
{
	unsigned int regoffset, regindex, bitoffset;
	unsigned int val;

	/* pin++ as gpio configuration starts from 2nd bit of base register */
	pin++;

	regindex = pin / 32;
	bitoffset = pin % 32;

	if (regindex <= 3)
		regoffset = PAD_FUNCTION_EN_1 + regindex * sizeof(int *);
	else
		regoffset = PAD_FUNCTION_EN_5 + (regindex - 4) * sizeof(int *);

	val = pmx_readl(pmx, regoffset);
	if (enable)
		val &= ~(0x1 << bitoffset);
	else
		val |= 0x1 << bitoffset;

	pmx_writel(pmx, val, regoffset);
}

static struct spear_pinctrl_machdata spear1340_machdata = {
	.pins = spear1340_pins,
	.npins = ARRAY_SIZE(spear1340_pins),
	.groups = spear1340_pingroups,
	.ngroups = ARRAY_SIZE(spear1340_pingroups),
	.functions = spear1340_functions,
	.nfunctions = ARRAY_SIZE(spear1340_functions),
	.gpio_request_endisable = gpio_request_endisable,
	.modes_supported = false,
};

static struct of_device_id spear1340_pinctrl_of_match[] __devinitdata = {
	{
		.compatible = "st,spear1340-pinmux",
	},
	{},
};

static int __devinit spear1340_pinctrl_probe(struct platform_device *pdev)
{
	return spear_pinctrl_probe(pdev, &spear1340_machdata);
}

static int __devexit spear1340_pinctrl_remove(struct platform_device *pdev)
{
	return spear_pinctrl_remove(pdev);
}

static struct platform_driver spear1340_pinctrl_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = spear1340_pinctrl_of_match,
	},
	.probe = spear1340_pinctrl_probe,
	.remove = __devexit_p(spear1340_pinctrl_remove),
};

static int __init spear1340_pinctrl_init(void)
{
	return platform_driver_register(&spear1340_pinctrl_driver);
}
arch_initcall(spear1340_pinctrl_init);

static void __exit spear1340_pinctrl_exit(void)
{
	platform_driver_unregister(&spear1340_pinctrl_driver);
}
module_exit(spear1340_pinctrl_exit);

MODULE_AUTHOR("Viresh Kumar <viresh.linux@gmail.com>");
MODULE_DESCRIPTION("ST Microelectronics SPEAr1340 pinctrl driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, spear1340_pinctrl_of_match);
