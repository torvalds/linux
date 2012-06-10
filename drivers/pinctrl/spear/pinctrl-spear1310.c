/*
 * Driver for the ST Microelectronics SPEAr1310 pinmux
 *
 * Copyright (C) 2012 ST Microelectronics
 * Viresh Kumar <viresh.kumar@st.com>
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

#define DRIVER_NAME "spear1310-pinmux"

/* pins */
static const struct pinctrl_pin_desc spear1310_pins[] = {
	SPEAR_PIN_0_TO_101,
	SPEAR_PIN_102_TO_245,
};

/* registers */
#define PERIP_CFG					0x32C
	#define MCIF_SEL_SHIFT				3
	#define MCIF_SEL_SD				(0x1 << MCIF_SEL_SHIFT)
	#define MCIF_SEL_CF				(0x2 << MCIF_SEL_SHIFT)
	#define MCIF_SEL_XD				(0x3 << MCIF_SEL_SHIFT)
	#define MCIF_SEL_MASK				(0x3 << MCIF_SEL_SHIFT)

#define PCIE_SATA_CFG					0x3A4
	#define PCIE_SATA2_SEL_PCIE			(0 << 31)
	#define PCIE_SATA1_SEL_PCIE			(0 << 30)
	#define PCIE_SATA0_SEL_PCIE			(0 << 29)
	#define PCIE_SATA2_SEL_SATA			(1 << 31)
	#define PCIE_SATA1_SEL_SATA			(1 << 30)
	#define PCIE_SATA0_SEL_SATA			(1 << 29)
	#define SATA2_CFG_TX_CLK_EN			(1 << 27)
	#define SATA2_CFG_RX_CLK_EN			(1 << 26)
	#define SATA2_CFG_POWERUP_RESET			(1 << 25)
	#define SATA2_CFG_PM_CLK_EN			(1 << 24)
	#define SATA1_CFG_TX_CLK_EN			(1 << 23)
	#define SATA1_CFG_RX_CLK_EN			(1 << 22)
	#define SATA1_CFG_POWERUP_RESET			(1 << 21)
	#define SATA1_CFG_PM_CLK_EN			(1 << 20)
	#define SATA0_CFG_TX_CLK_EN			(1 << 19)
	#define SATA0_CFG_RX_CLK_EN			(1 << 18)
	#define SATA0_CFG_POWERUP_RESET			(1 << 17)
	#define SATA0_CFG_PM_CLK_EN			(1 << 16)
	#define PCIE2_CFG_DEVICE_PRESENT		(1 << 11)
	#define PCIE2_CFG_POWERUP_RESET			(1 << 10)
	#define PCIE2_CFG_CORE_CLK_EN			(1 << 9)
	#define PCIE2_CFG_AUX_CLK_EN			(1 << 8)
	#define PCIE1_CFG_DEVICE_PRESENT		(1 << 7)
	#define PCIE1_CFG_POWERUP_RESET			(1 << 6)
	#define PCIE1_CFG_CORE_CLK_EN			(1 << 5)
	#define PCIE1_CFG_AUX_CLK_EN			(1 << 4)
	#define PCIE0_CFG_DEVICE_PRESENT		(1 << 3)
	#define PCIE0_CFG_POWERUP_RESET			(1 << 2)
	#define PCIE0_CFG_CORE_CLK_EN			(1 << 1)
	#define PCIE0_CFG_AUX_CLK_EN			(1 << 0)

#define PAD_FUNCTION_EN_0				0x650
	#define PMX_UART0_MASK				(1 << 1)
	#define PMX_I2C0_MASK				(1 << 2)
	#define PMX_I2S0_MASK				(1 << 3)
	#define PMX_SSP0_MASK				(1 << 4)
	#define PMX_CLCD1_MASK				(1 << 5)
	#define PMX_EGPIO00_MASK			(1 << 6)
	#define PMX_EGPIO01_MASK			(1 << 7)
	#define PMX_EGPIO02_MASK			(1 << 8)
	#define PMX_EGPIO03_MASK			(1 << 9)
	#define PMX_EGPIO04_MASK			(1 << 10)
	#define PMX_EGPIO05_MASK			(1 << 11)
	#define PMX_EGPIO06_MASK			(1 << 12)
	#define PMX_EGPIO07_MASK			(1 << 13)
	#define PMX_EGPIO08_MASK			(1 << 14)
	#define PMX_EGPIO09_MASK			(1 << 15)
	#define PMX_SMI_MASK				(1 << 16)
	#define PMX_NAND8_MASK				(1 << 17)
	#define PMX_GMIICLK_MASK			(1 << 18)
	#define PMX_GMIICOL_CRS_XFERER_MIITXCLK_MASK	(1 << 19)
	#define PMX_RXCLK_RDV_TXEN_D03_MASK		(1 << 20)
	#define PMX_GMIID47_MASK			(1 << 21)
	#define PMX_MDC_MDIO_MASK			(1 << 22)
	#define PMX_MCI_DATA8_15_MASK			(1 << 23)
	#define PMX_NFAD23_MASK				(1 << 24)
	#define PMX_NFAD24_MASK				(1 << 25)
	#define PMX_NFAD25_MASK				(1 << 26)
	#define PMX_NFCE3_MASK				(1 << 27)
	#define PMX_NFWPRT3_MASK			(1 << 28)
	#define PMX_NFRSTPWDWN0_MASK			(1 << 29)
	#define PMX_NFRSTPWDWN1_MASK			(1 << 30)
	#define PMX_NFRSTPWDWN2_MASK			(1 << 31)

#define PAD_FUNCTION_EN_1				0x654
	#define PMX_NFRSTPWDWN3_MASK			(1 << 0)
	#define PMX_SMINCS2_MASK			(1 << 1)
	#define PMX_SMINCS3_MASK			(1 << 2)
	#define PMX_CLCD2_MASK				(1 << 3)
	#define PMX_KBD_ROWCOL68_MASK			(1 << 4)
	#define PMX_EGPIO10_MASK			(1 << 5)
	#define PMX_EGPIO11_MASK			(1 << 6)
	#define PMX_EGPIO12_MASK			(1 << 7)
	#define PMX_EGPIO13_MASK			(1 << 8)
	#define PMX_EGPIO14_MASK			(1 << 9)
	#define PMX_EGPIO15_MASK			(1 << 10)
	#define PMX_UART0_MODEM_MASK			(1 << 11)
	#define PMX_GPT0_TMR0_MASK			(1 << 12)
	#define PMX_GPT0_TMR1_MASK			(1 << 13)
	#define PMX_GPT1_TMR0_MASK			(1 << 14)
	#define PMX_GPT1_TMR1_MASK			(1 << 15)
	#define PMX_I2S1_MASK				(1 << 16)
	#define PMX_KBD_ROWCOL25_MASK			(1 << 17)
	#define PMX_NFIO8_15_MASK			(1 << 18)
	#define PMX_KBD_COL1_MASK			(1 << 19)
	#define PMX_NFCE1_MASK				(1 << 20)
	#define PMX_KBD_COL0_MASK			(1 << 21)
	#define PMX_NFCE2_MASK				(1 << 22)
	#define PMX_KBD_ROW1_MASK			(1 << 23)
	#define PMX_NFWPRT1_MASK			(1 << 24)
	#define PMX_KBD_ROW0_MASK			(1 << 25)
	#define PMX_NFWPRT2_MASK			(1 << 26)
	#define PMX_MCIDATA0_MASK			(1 << 27)
	#define PMX_MCIDATA1_MASK			(1 << 28)
	#define PMX_MCIDATA2_MASK			(1 << 29)
	#define PMX_MCIDATA3_MASK			(1 << 30)
	#define PMX_MCIDATA4_MASK			(1 << 31)

#define PAD_FUNCTION_EN_2				0x658
	#define PMX_MCIDATA5_MASK			(1 << 0)
	#define PMX_MCIDATA6_MASK			(1 << 1)
	#define PMX_MCIDATA7_MASK			(1 << 2)
	#define PMX_MCIDATA1SD_MASK			(1 << 3)
	#define PMX_MCIDATA2SD_MASK			(1 << 4)
	#define PMX_MCIDATA3SD_MASK			(1 << 5)
	#define PMX_MCIADDR0ALE_MASK			(1 << 6)
	#define PMX_MCIADDR1CLECLK_MASK			(1 << 7)
	#define PMX_MCIADDR2_MASK			(1 << 8)
	#define PMX_MCICECF_MASK			(1 << 9)
	#define PMX_MCICEXD_MASK			(1 << 10)
	#define PMX_MCICESDMMC_MASK			(1 << 11)
	#define PMX_MCICDCF1_MASK			(1 << 12)
	#define PMX_MCICDCF2_MASK			(1 << 13)
	#define PMX_MCICDXD_MASK			(1 << 14)
	#define PMX_MCICDSDMMC_MASK			(1 << 15)
	#define PMX_MCIDATADIR_MASK			(1 << 16)
	#define PMX_MCIDMARQWP_MASK			(1 << 17)
	#define PMX_MCIIORDRE_MASK			(1 << 18)
	#define PMX_MCIIOWRWE_MASK			(1 << 19)
	#define PMX_MCIRESETCF_MASK			(1 << 20)
	#define PMX_MCICS0CE_MASK			(1 << 21)
	#define PMX_MCICFINTR_MASK			(1 << 22)
	#define PMX_MCIIORDY_MASK			(1 << 23)
	#define PMX_MCICS1_MASK				(1 << 24)
	#define PMX_MCIDMAACK_MASK			(1 << 25)
	#define PMX_MCISDCMD_MASK			(1 << 26)
	#define PMX_MCILEDS_MASK			(1 << 27)
	#define PMX_TOUCH_XY_MASK			(1 << 28)
	#define PMX_SSP0_CS0_MASK			(1 << 29)
	#define PMX_SSP0_CS1_2_MASK			(1 << 30)

/* combined macros */
#define PMX_GMII_MASK		(PMX_GMIICLK_MASK |			\
				PMX_GMIICOL_CRS_XFERER_MIITXCLK_MASK |	\
				PMX_RXCLK_RDV_TXEN_D03_MASK |		\
				PMX_GMIID47_MASK | PMX_MDC_MDIO_MASK)

#define PMX_EGPIO_0_GRP_MASK	(PMX_EGPIO00_MASK | PMX_EGPIO01_MASK |	\
				PMX_EGPIO02_MASK |			\
				PMX_EGPIO03_MASK | PMX_EGPIO04_MASK |	\
				PMX_EGPIO05_MASK | PMX_EGPIO06_MASK |	\
				PMX_EGPIO07_MASK | PMX_EGPIO08_MASK |	\
				PMX_EGPIO09_MASK)
#define PMX_EGPIO_1_GRP_MASK	(PMX_EGPIO10_MASK | PMX_EGPIO11_MASK |	\
				PMX_EGPIO12_MASK | PMX_EGPIO13_MASK |	\
				PMX_EGPIO14_MASK | PMX_EGPIO15_MASK)

#define PMX_KEYBOARD_6X6_MASK	(PMX_KBD_ROW0_MASK | PMX_KBD_ROW1_MASK | \
				PMX_KBD_ROWCOL25_MASK | PMX_KBD_COL0_MASK | \
				PMX_KBD_COL1_MASK)

#define PMX_NAND8BIT_0_MASK	(PMX_NAND8_MASK | PMX_NFAD23_MASK |	\
				PMX_NFAD24_MASK | PMX_NFAD25_MASK |	\
				PMX_NFWPRT3_MASK | PMX_NFRSTPWDWN0_MASK | \
				PMX_NFRSTPWDWN1_MASK | PMX_NFRSTPWDWN2_MASK | \
				PMX_NFCE3_MASK)
#define PMX_NAND8BIT_1_MASK	PMX_NFRSTPWDWN3_MASK

#define PMX_NAND16BIT_1_MASK	(PMX_KBD_ROWCOL25_MASK | PMX_NFIO8_15_MASK)
#define PMX_NAND_4CHIPS_MASK	(PMX_NFCE1_MASK | PMX_NFCE2_MASK |	\
				PMX_NFWPRT1_MASK | PMX_NFWPRT2_MASK |	\
				PMX_KBD_ROW0_MASK | PMX_KBD_ROW1_MASK |	\
				PMX_KBD_COL0_MASK | PMX_KBD_COL1_MASK)

#define PMX_MCIFALL_1_MASK	0xF8000000
#define PMX_MCIFALL_2_MASK	0x0FFFFFFF

#define PMX_PCI_REG1_MASK	(PMX_SMINCS2_MASK | PMX_SMINCS3_MASK |	\
				PMX_CLCD2_MASK | PMX_KBD_ROWCOL68_MASK | \
				PMX_EGPIO_1_GRP_MASK | PMX_GPT0_TMR0_MASK | \
				PMX_GPT0_TMR1_MASK | PMX_GPT1_TMR0_MASK | \
				PMX_GPT1_TMR1_MASK | PMX_I2S1_MASK |	\
				PMX_NFCE2_MASK)
#define PMX_PCI_REG2_MASK	(PMX_TOUCH_XY_MASK | PMX_SSP0_CS0_MASK | \
				PMX_SSP0_CS1_2_MASK)

#define PMX_SMII_0_1_2_MASK	(PMX_CLCD2_MASK | PMX_KBD_ROWCOL68_MASK)
#define PMX_RGMII_REG0_MASK	(PMX_MCI_DATA8_15_MASK |		\
				PMX_GMIICOL_CRS_XFERER_MIITXCLK_MASK |	\
				PMX_GMIID47_MASK)
#define PMX_RGMII_REG1_MASK	(PMX_KBD_ROWCOL68_MASK | PMX_EGPIO_1_GRP_MASK |\
				PMX_KBD_ROW1_MASK | PMX_NFWPRT1_MASK |	\
				PMX_KBD_ROW0_MASK | PMX_NFWPRT2_MASK)
#define PMX_RGMII_REG2_MASK	(PMX_TOUCH_XY_MASK | PMX_SSP0_CS0_MASK | \
				PMX_SSP0_CS1_2_MASK)

#define PCIE_CFG_VAL(x)		(PCIE_SATA##x##_SEL_PCIE |	\
				PCIE##x##_CFG_AUX_CLK_EN |	\
				PCIE##x##_CFG_CORE_CLK_EN |	\
				PCIE##x##_CFG_POWERUP_RESET |	\
				PCIE##x##_CFG_DEVICE_PRESENT)
#define SATA_CFG_VAL(x)		(PCIE_SATA##x##_SEL_SATA |	\
				SATA##x##_CFG_PM_CLK_EN |	\
				SATA##x##_CFG_POWERUP_RESET |	\
				SATA##x##_CFG_RX_CLK_EN |	\
				SATA##x##_CFG_TX_CLK_EN)

/* Pad multiplexing for i2c0 device */
static const unsigned i2c0_pins[] = { 102, 103 };
static struct spear_muxreg i2c0_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_0,
		.mask = PMX_I2C0_MASK,
		.val = PMX_I2C0_MASK,
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

/* Pad multiplexing for ssp0 device */
static const unsigned ssp0_pins[] = { 109, 110, 111, 112 };
static struct spear_muxreg ssp0_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_0,
		.mask = PMX_SSP0_MASK,
		.val = PMX_SSP0_MASK,
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

/* Pad multiplexing for ssp0_cs0 device */
static const unsigned ssp0_cs0_pins[] = { 96 };
static struct spear_muxreg ssp0_cs0_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_2,
		.mask = PMX_SSP0_CS0_MASK,
		.val = PMX_SSP0_CS0_MASK,
	},
};

static struct spear_modemux ssp0_cs0_modemux[] = {
	{
		.muxregs = ssp0_cs0_muxreg,
		.nmuxregs = ARRAY_SIZE(ssp0_cs0_muxreg),
	},
};

static struct spear_pingroup ssp0_cs0_pingroup = {
	.name = "ssp0_cs0_grp",
	.pins = ssp0_cs0_pins,
	.npins = ARRAY_SIZE(ssp0_cs0_pins),
	.modemuxs = ssp0_cs0_modemux,
	.nmodemuxs = ARRAY_SIZE(ssp0_cs0_modemux),
};

/* ssp0_cs1_2 device */
static const unsigned ssp0_cs1_2_pins[] = { 94, 95 };
static struct spear_muxreg ssp0_cs1_2_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_2,
		.mask = PMX_SSP0_CS1_2_MASK,
		.val = PMX_SSP0_CS1_2_MASK,
	},
};

static struct spear_modemux ssp0_cs1_2_modemux[] = {
	{
		.muxregs = ssp0_cs1_2_muxreg,
		.nmuxregs = ARRAY_SIZE(ssp0_cs1_2_muxreg),
	},
};

static struct spear_pingroup ssp0_cs1_2_pingroup = {
	.name = "ssp0_cs1_2_grp",
	.pins = ssp0_cs1_2_pins,
	.npins = ARRAY_SIZE(ssp0_cs1_2_pins),
	.modemuxs = ssp0_cs1_2_modemux,
	.nmodemuxs = ARRAY_SIZE(ssp0_cs1_2_modemux),
};

static const char *const ssp0_grps[] = { "ssp0_grp", "ssp0_cs0_grp",
	"ssp0_cs1_2_grp" };
static struct spear_function ssp0_function = {
	.name = "ssp0",
	.groups = ssp0_grps,
	.ngroups = ARRAY_SIZE(ssp0_grps),
};

/* Pad multiplexing for i2s0 device */
static const unsigned i2s0_pins[] = { 104, 105, 106, 107, 108 };
static struct spear_muxreg i2s0_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_0,
		.mask = PMX_I2S0_MASK,
		.val = PMX_I2S0_MASK,
	},
};

static struct spear_modemux i2s0_modemux[] = {
	{
		.muxregs = i2s0_muxreg,
		.nmuxregs = ARRAY_SIZE(i2s0_muxreg),
	},
};

static struct spear_pingroup i2s0_pingroup = {
	.name = "i2s0_grp",
	.pins = i2s0_pins,
	.npins = ARRAY_SIZE(i2s0_pins),
	.modemuxs = i2s0_modemux,
	.nmodemuxs = ARRAY_SIZE(i2s0_modemux),
};

static const char *const i2s0_grps[] = { "i2s0_grp" };
static struct spear_function i2s0_function = {
	.name = "i2s0",
	.groups = i2s0_grps,
	.ngroups = ARRAY_SIZE(i2s0_grps),
};

/* Pad multiplexing for i2s1 device */
static const unsigned i2s1_pins[] = { 0, 1, 2, 3 };
static struct spear_muxreg i2s1_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_1,
		.mask = PMX_I2S1_MASK,
		.val = PMX_I2S1_MASK,
	},
};

static struct spear_modemux i2s1_modemux[] = {
	{
		.muxregs = i2s1_muxreg,
		.nmuxregs = ARRAY_SIZE(i2s1_muxreg),
	},
};

static struct spear_pingroup i2s1_pingroup = {
	.name = "i2s1_grp",
	.pins = i2s1_pins,
	.npins = ARRAY_SIZE(i2s1_pins),
	.modemuxs = i2s1_modemux,
	.nmodemuxs = ARRAY_SIZE(i2s1_modemux),
};

static const char *const i2s1_grps[] = { "i2s1_grp" };
static struct spear_function i2s1_function = {
	.name = "i2s1",
	.groups = i2s1_grps,
	.ngroups = ARRAY_SIZE(i2s1_grps),
};

/* Pad multiplexing for clcd device */
static const unsigned clcd_pins[] = { 113, 114, 115, 116, 117, 118, 119, 120,
	121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134,
	135, 136, 137, 138, 139, 140, 141, 142 };
static struct spear_muxreg clcd_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_0,
		.mask = PMX_CLCD1_MASK,
		.val = PMX_CLCD1_MASK,
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

static const unsigned clcd_high_res_pins[] = { 30, 31, 32, 33, 34, 35, 36, 37,
	38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53 };
static struct spear_muxreg clcd_high_res_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_1,
		.mask = PMX_CLCD2_MASK,
		.val = PMX_CLCD2_MASK,
	},
};

static struct spear_modemux clcd_high_res_modemux[] = {
	{
		.muxregs = clcd_high_res_muxreg,
		.nmuxregs = ARRAY_SIZE(clcd_high_res_muxreg),
	},
};

static struct spear_pingroup clcd_high_res_pingroup = {
	.name = "clcd_high_res_grp",
	.pins = clcd_high_res_pins,
	.npins = ARRAY_SIZE(clcd_high_res_pins),
	.modemuxs = clcd_high_res_modemux,
	.nmodemuxs = ARRAY_SIZE(clcd_high_res_modemux),
};

static const char *const clcd_grps[] = { "clcd_grp", "clcd_high_res" };
static struct spear_function clcd_function = {
	.name = "clcd",
	.groups = clcd_grps,
	.ngroups = ARRAY_SIZE(clcd_grps),
};

static const unsigned arm_gpio_pins[] = { 18, 19, 20, 21, 22, 23, 143, 144, 145,
	146, 147, 148, 149, 150, 151, 152 };
static struct spear_muxreg arm_gpio_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_0,
		.mask = PMX_EGPIO_0_GRP_MASK,
		.val = PMX_EGPIO_0_GRP_MASK,
	}, {
		.reg = PAD_FUNCTION_EN_1,
		.mask = PMX_EGPIO_1_GRP_MASK,
		.val = PMX_EGPIO_1_GRP_MASK,
	},
};

static struct spear_modemux arm_gpio_modemux[] = {
	{
		.muxregs = arm_gpio_muxreg,
		.nmuxregs = ARRAY_SIZE(arm_gpio_muxreg),
	},
};

static struct spear_pingroup arm_gpio_pingroup = {
	.name = "arm_gpio_grp",
	.pins = arm_gpio_pins,
	.npins = ARRAY_SIZE(arm_gpio_pins),
	.modemuxs = arm_gpio_modemux,
	.nmodemuxs = ARRAY_SIZE(arm_gpio_modemux),
};

static const char *const arm_gpio_grps[] = { "arm_gpio_grp" };
static struct spear_function arm_gpio_function = {
	.name = "arm_gpio",
	.groups = arm_gpio_grps,
	.ngroups = ARRAY_SIZE(arm_gpio_grps),
};

/* Pad multiplexing for smi 2 chips device */
static const unsigned smi_2_chips_pins[] = { 153, 154, 155, 156, 157 };
static struct spear_muxreg smi_2_chips_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_0,
		.mask = PMX_SMI_MASK,
		.val = PMX_SMI_MASK,
	},
};

static struct spear_modemux smi_2_chips_modemux[] = {
	{
		.muxregs = smi_2_chips_muxreg,
		.nmuxregs = ARRAY_SIZE(smi_2_chips_muxreg),
	},
};

static struct spear_pingroup smi_2_chips_pingroup = {
	.name = "smi_2_chips_grp",
	.pins = smi_2_chips_pins,
	.npins = ARRAY_SIZE(smi_2_chips_pins),
	.modemuxs = smi_2_chips_modemux,
	.nmodemuxs = ARRAY_SIZE(smi_2_chips_modemux),
};

static const unsigned smi_4_chips_pins[] = { 54, 55 };
static struct spear_muxreg smi_4_chips_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_0,
		.mask = PMX_SMI_MASK,
		.val = PMX_SMI_MASK,
	}, {
		.reg = PAD_FUNCTION_EN_1,
		.mask = PMX_SMINCS2_MASK | PMX_SMINCS3_MASK,
		.val = PMX_SMINCS2_MASK | PMX_SMINCS3_MASK,
	},
};

static struct spear_modemux smi_4_chips_modemux[] = {
	{
		.muxregs = smi_4_chips_muxreg,
		.nmuxregs = ARRAY_SIZE(smi_4_chips_muxreg),
	},
};

static struct spear_pingroup smi_4_chips_pingroup = {
	.name = "smi_4_chips_grp",
	.pins = smi_4_chips_pins,
	.npins = ARRAY_SIZE(smi_4_chips_pins),
	.modemuxs = smi_4_chips_modemux,
	.nmodemuxs = ARRAY_SIZE(smi_4_chips_modemux),
};

static const char *const smi_grps[] = { "smi_2_chips_grp", "smi_4_chips_grp" };
static struct spear_function smi_function = {
	.name = "smi",
	.groups = smi_grps,
	.ngroups = ARRAY_SIZE(smi_grps),
};

/* Pad multiplexing for gmii device */
static const unsigned gmii_pins[] = { 173, 174, 175, 176, 177, 178, 179, 180,
	181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194,
	195, 196, 197, 198, 199, 200 };
static struct spear_muxreg gmii_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_0,
		.mask = PMX_GMII_MASK,
		.val = PMX_GMII_MASK,
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
	.pins = gmii_pins,
	.npins = ARRAY_SIZE(gmii_pins),
	.modemuxs = gmii_modemux,
	.nmodemuxs = ARRAY_SIZE(gmii_modemux),
};

static const char *const gmii_grps[] = { "gmii_grp" };
static struct spear_function gmii_function = {
	.name = "gmii",
	.groups = gmii_grps,
	.ngroups = ARRAY_SIZE(gmii_grps),
};

/* Pad multiplexing for rgmii device */
static const unsigned rgmii_pins[] = { 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
	28, 29, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 175,
	180, 181, 182, 183, 185, 188, 193, 194, 195, 196, 197, 198, 211, 212 };
static struct spear_muxreg rgmii_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_0,
		.mask = PMX_RGMII_REG0_MASK,
		.val = 0,
	}, {
		.reg = PAD_FUNCTION_EN_1,
		.mask = PMX_RGMII_REG1_MASK,
		.val = 0,
	}, {
		.reg = PAD_FUNCTION_EN_2,
		.mask = PMX_RGMII_REG2_MASK,
		.val = 0,
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
	.pins = rgmii_pins,
	.npins = ARRAY_SIZE(rgmii_pins),
	.modemuxs = rgmii_modemux,
	.nmodemuxs = ARRAY_SIZE(rgmii_modemux),
};

static const char *const rgmii_grps[] = { "rgmii_grp" };
static struct spear_function rgmii_function = {
	.name = "rgmii",
	.groups = rgmii_grps,
	.ngroups = ARRAY_SIZE(rgmii_grps),
};

/* Pad multiplexing for smii_0_1_2 device */
static const unsigned smii_0_1_2_pins[] = { 24, 25, 26, 27, 28, 29, 30, 31, 32,
	33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
	51, 52, 53, 54, 55 };
static struct spear_muxreg smii_0_1_2_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_1,
		.mask = PMX_SMII_0_1_2_MASK,
		.val = 0,
	},
};

static struct spear_modemux smii_0_1_2_modemux[] = {
	{
		.muxregs = smii_0_1_2_muxreg,
		.nmuxregs = ARRAY_SIZE(smii_0_1_2_muxreg),
	},
};

static struct spear_pingroup smii_0_1_2_pingroup = {
	.name = "smii_0_1_2_grp",
	.pins = smii_0_1_2_pins,
	.npins = ARRAY_SIZE(smii_0_1_2_pins),
	.modemuxs = smii_0_1_2_modemux,
	.nmodemuxs = ARRAY_SIZE(smii_0_1_2_modemux),
};

static const char *const smii_0_1_2_grps[] = { "smii_0_1_2_grp" };
static struct spear_function smii_0_1_2_function = {
	.name = "smii_0_1_2",
	.groups = smii_0_1_2_grps,
	.ngroups = ARRAY_SIZE(smii_0_1_2_grps),
};

/* Pad multiplexing for ras_mii_txclk device */
static const unsigned ras_mii_txclk_pins[] = { 98, 99 };
static struct spear_muxreg ras_mii_txclk_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_1,
		.mask = PMX_NFCE2_MASK,
		.val = 0,
	},
};

static struct spear_modemux ras_mii_txclk_modemux[] = {
	{
		.muxregs = ras_mii_txclk_muxreg,
		.nmuxregs = ARRAY_SIZE(ras_mii_txclk_muxreg),
	},
};

static struct spear_pingroup ras_mii_txclk_pingroup = {
	.name = "ras_mii_txclk_grp",
	.pins = ras_mii_txclk_pins,
	.npins = ARRAY_SIZE(ras_mii_txclk_pins),
	.modemuxs = ras_mii_txclk_modemux,
	.nmodemuxs = ARRAY_SIZE(ras_mii_txclk_modemux),
};

static const char *const ras_mii_txclk_grps[] = { "ras_mii_txclk_grp" };
static struct spear_function ras_mii_txclk_function = {
	.name = "ras_mii_txclk",
	.groups = ras_mii_txclk_grps,
	.ngroups = ARRAY_SIZE(ras_mii_txclk_grps),
};

/* Pad multiplexing for nand 8bit device (cs0 only) */
static const unsigned nand_8bit_pins[] = { 56, 57, 58, 59, 60, 61, 62, 63, 64,
	65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82,
	83, 84, 85, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169,
	170, 171, 172, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211,
	212 };
static struct spear_muxreg nand_8bit_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_0,
		.mask = PMX_NAND8BIT_0_MASK,
		.val = PMX_NAND8BIT_0_MASK,
	}, {
		.reg = PAD_FUNCTION_EN_1,
		.mask = PMX_NAND8BIT_1_MASK,
		.val = PMX_NAND8BIT_1_MASK,
	},
};

static struct spear_modemux nand_8bit_modemux[] = {
	{
		.muxregs = nand_8bit_muxreg,
		.nmuxregs = ARRAY_SIZE(nand_8bit_muxreg),
	},
};

static struct spear_pingroup nand_8bit_pingroup = {
	.name = "nand_8bit_grp",
	.pins = nand_8bit_pins,
	.npins = ARRAY_SIZE(nand_8bit_pins),
	.modemuxs = nand_8bit_modemux,
	.nmodemuxs = ARRAY_SIZE(nand_8bit_modemux),
};

/* Pad multiplexing for nand 16bit device */
static const unsigned nand_16bit_pins[] = { 201, 202, 203, 204, 207, 208, 209,
	210 };
static struct spear_muxreg nand_16bit_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_1,
		.mask = PMX_NAND16BIT_1_MASK,
		.val = PMX_NAND16BIT_1_MASK,
	},
};

static struct spear_modemux nand_16bit_modemux[] = {
	{
		.muxregs = nand_16bit_muxreg,
		.nmuxregs = ARRAY_SIZE(nand_16bit_muxreg),
	},
};

static struct spear_pingroup nand_16bit_pingroup = {
	.name = "nand_16bit_grp",
	.pins = nand_16bit_pins,
	.npins = ARRAY_SIZE(nand_16bit_pins),
	.modemuxs = nand_16bit_modemux,
	.nmodemuxs = ARRAY_SIZE(nand_16bit_modemux),
};

/* Pad multiplexing for nand 4 chips */
static const unsigned nand_4_chips_pins[] = { 205, 206, 211, 212 };
static struct spear_muxreg nand_4_chips_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_1,
		.mask = PMX_NAND_4CHIPS_MASK,
		.val = PMX_NAND_4CHIPS_MASK,
	},
};

static struct spear_modemux nand_4_chips_modemux[] = {
	{
		.muxregs = nand_4_chips_muxreg,
		.nmuxregs = ARRAY_SIZE(nand_4_chips_muxreg),
	},
};

static struct spear_pingroup nand_4_chips_pingroup = {
	.name = "nand_4_chips_grp",
	.pins = nand_4_chips_pins,
	.npins = ARRAY_SIZE(nand_4_chips_pins),
	.modemuxs = nand_4_chips_modemux,
	.nmodemuxs = ARRAY_SIZE(nand_4_chips_modemux),
};

static const char *const nand_grps[] = { "nand_8bit_grp", "nand_16bit_grp",
	"nand_4_chips_grp" };
static struct spear_function nand_function = {
	.name = "nand",
	.groups = nand_grps,
	.ngroups = ARRAY_SIZE(nand_grps),
};

/* Pad multiplexing for keyboard_6x6 device */
static const unsigned keyboard_6x6_pins[] = { 201, 202, 203, 204, 205, 206, 207,
	208, 209, 210, 211, 212 };
static struct spear_muxreg keyboard_6x6_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_1,
		.mask = PMX_KEYBOARD_6X6_MASK | PMX_NFIO8_15_MASK |
			PMX_NFCE1_MASK | PMX_NFCE2_MASK | PMX_NFWPRT1_MASK |
			PMX_NFWPRT2_MASK,
		.val = PMX_KEYBOARD_6X6_MASK,
	},
};

static struct spear_modemux keyboard_6x6_modemux[] = {
	{
		.muxregs = keyboard_6x6_muxreg,
		.nmuxregs = ARRAY_SIZE(keyboard_6x6_muxreg),
	},
};

static struct spear_pingroup keyboard_6x6_pingroup = {
	.name = "keyboard_6x6_grp",
	.pins = keyboard_6x6_pins,
	.npins = ARRAY_SIZE(keyboard_6x6_pins),
	.modemuxs = keyboard_6x6_modemux,
	.nmodemuxs = ARRAY_SIZE(keyboard_6x6_modemux),
};

/* Pad multiplexing for keyboard_rowcol6_8 device */
static const unsigned keyboard_rowcol6_8_pins[] = { 24, 25, 26, 27, 28, 29 };
static struct spear_muxreg keyboard_rowcol6_8_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_1,
		.mask = PMX_KBD_ROWCOL68_MASK,
		.val = PMX_KBD_ROWCOL68_MASK,
	},
};

static struct spear_modemux keyboard_rowcol6_8_modemux[] = {
	{
		.muxregs = keyboard_rowcol6_8_muxreg,
		.nmuxregs = ARRAY_SIZE(keyboard_rowcol6_8_muxreg),
	},
};

static struct spear_pingroup keyboard_rowcol6_8_pingroup = {
	.name = "keyboard_rowcol6_8_grp",
	.pins = keyboard_rowcol6_8_pins,
	.npins = ARRAY_SIZE(keyboard_rowcol6_8_pins),
	.modemuxs = keyboard_rowcol6_8_modemux,
	.nmodemuxs = ARRAY_SIZE(keyboard_rowcol6_8_modemux),
};

static const char *const keyboard_grps[] = { "keyboard_6x6_grp",
	"keyboard_rowcol6_8_grp" };
static struct spear_function keyboard_function = {
	.name = "keyboard",
	.groups = keyboard_grps,
	.ngroups = ARRAY_SIZE(keyboard_grps),
};

/* Pad multiplexing for uart0 device */
static const unsigned uart0_pins[] = { 100, 101 };
static struct spear_muxreg uart0_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_0,
		.mask = PMX_UART0_MASK,
		.val = PMX_UART0_MASK,
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

/* Pad multiplexing for uart0_modem device */
static const unsigned uart0_modem_pins[] = { 12, 13, 14, 15, 16, 17 };
static struct spear_muxreg uart0_modem_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_1,
		.mask = PMX_UART0_MODEM_MASK,
		.val = PMX_UART0_MODEM_MASK,
	},
};

static struct spear_modemux uart0_modem_modemux[] = {
	{
		.muxregs = uart0_modem_muxreg,
		.nmuxregs = ARRAY_SIZE(uart0_modem_muxreg),
	},
};

static struct spear_pingroup uart0_modem_pingroup = {
	.name = "uart0_modem_grp",
	.pins = uart0_modem_pins,
	.npins = ARRAY_SIZE(uart0_modem_pins),
	.modemuxs = uart0_modem_modemux,
	.nmodemuxs = ARRAY_SIZE(uart0_modem_modemux),
};

static const char *const uart0_grps[] = { "uart0_grp", "uart0_modem_grp" };
static struct spear_function uart0_function = {
	.name = "uart0",
	.groups = uart0_grps,
	.ngroups = ARRAY_SIZE(uart0_grps),
};

/* Pad multiplexing for gpt0_tmr0 device */
static const unsigned gpt0_tmr0_pins[] = { 10, 11 };
static struct spear_muxreg gpt0_tmr0_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_1,
		.mask = PMX_GPT0_TMR0_MASK,
		.val = PMX_GPT0_TMR0_MASK,
	},
};

static struct spear_modemux gpt0_tmr0_modemux[] = {
	{
		.muxregs = gpt0_tmr0_muxreg,
		.nmuxregs = ARRAY_SIZE(gpt0_tmr0_muxreg),
	},
};

static struct spear_pingroup gpt0_tmr0_pingroup = {
	.name = "gpt0_tmr0_grp",
	.pins = gpt0_tmr0_pins,
	.npins = ARRAY_SIZE(gpt0_tmr0_pins),
	.modemuxs = gpt0_tmr0_modemux,
	.nmodemuxs = ARRAY_SIZE(gpt0_tmr0_modemux),
};

/* Pad multiplexing for gpt0_tmr1 device */
static const unsigned gpt0_tmr1_pins[] = { 8, 9 };
static struct spear_muxreg gpt0_tmr1_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_1,
		.mask = PMX_GPT0_TMR1_MASK,
		.val = PMX_GPT0_TMR1_MASK,
	},
};

static struct spear_modemux gpt0_tmr1_modemux[] = {
	{
		.muxregs = gpt0_tmr1_muxreg,
		.nmuxregs = ARRAY_SIZE(gpt0_tmr1_muxreg),
	},
};

static struct spear_pingroup gpt0_tmr1_pingroup = {
	.name = "gpt0_tmr1_grp",
	.pins = gpt0_tmr1_pins,
	.npins = ARRAY_SIZE(gpt0_tmr1_pins),
	.modemuxs = gpt0_tmr1_modemux,
	.nmodemuxs = ARRAY_SIZE(gpt0_tmr1_modemux),
};

static const char *const gpt0_grps[] = { "gpt0_tmr0_grp", "gpt0_tmr1_grp" };
static struct spear_function gpt0_function = {
	.name = "gpt0",
	.groups = gpt0_grps,
	.ngroups = ARRAY_SIZE(gpt0_grps),
};

/* Pad multiplexing for gpt1_tmr0 device */
static const unsigned gpt1_tmr0_pins[] = { 6, 7 };
static struct spear_muxreg gpt1_tmr0_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_1,
		.mask = PMX_GPT1_TMR0_MASK,
		.val = PMX_GPT1_TMR0_MASK,
	},
};

static struct spear_modemux gpt1_tmr0_modemux[] = {
	{
		.muxregs = gpt1_tmr0_muxreg,
		.nmuxregs = ARRAY_SIZE(gpt1_tmr0_muxreg),
	},
};

static struct spear_pingroup gpt1_tmr0_pingroup = {
	.name = "gpt1_tmr0_grp",
	.pins = gpt1_tmr0_pins,
	.npins = ARRAY_SIZE(gpt1_tmr0_pins),
	.modemuxs = gpt1_tmr0_modemux,
	.nmodemuxs = ARRAY_SIZE(gpt1_tmr0_modemux),
};

/* Pad multiplexing for gpt1_tmr1 device */
static const unsigned gpt1_tmr1_pins[] = { 4, 5 };
static struct spear_muxreg gpt1_tmr1_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_1,
		.mask = PMX_GPT1_TMR1_MASK,
		.val = PMX_GPT1_TMR1_MASK,
	},
};

static struct spear_modemux gpt1_tmr1_modemux[] = {
	{
		.muxregs = gpt1_tmr1_muxreg,
		.nmuxregs = ARRAY_SIZE(gpt1_tmr1_muxreg),
	},
};

static struct spear_pingroup gpt1_tmr1_pingroup = {
	.name = "gpt1_tmr1_grp",
	.pins = gpt1_tmr1_pins,
	.npins = ARRAY_SIZE(gpt1_tmr1_pins),
	.modemuxs = gpt1_tmr1_modemux,
	.nmodemuxs = ARRAY_SIZE(gpt1_tmr1_modemux),
};

static const char *const gpt1_grps[] = { "gpt1_tmr1_grp", "gpt1_tmr0_grp" };
static struct spear_function gpt1_function = {
	.name = "gpt1",
	.groups = gpt1_grps,
	.ngroups = ARRAY_SIZE(gpt1_grps),
};

/* Pad multiplexing for mcif device */
static const unsigned mcif_pins[] = { 86, 87, 88, 89, 90, 91, 92, 93, 213, 214,
	215, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228,
	229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242,
	243, 244, 245 };
#define MCIF_MUXREG						\
	{							\
		.reg = PAD_FUNCTION_EN_0,			\
		.mask = PMX_MCI_DATA8_15_MASK,			\
		.val = PMX_MCI_DATA8_15_MASK,			\
	}, {							\
		.reg = PAD_FUNCTION_EN_1,			\
		.mask = PMX_MCIFALL_1_MASK | PMX_NFWPRT1_MASK |	\
			PMX_NFWPRT2_MASK,			\
		.val = PMX_MCIFALL_1_MASK,			\
	}, {							\
		.reg = PAD_FUNCTION_EN_2,			\
		.mask = PMX_MCIFALL_2_MASK,			\
		.val = PMX_MCIFALL_2_MASK,			\
	}

/* sdhci device */
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

/* cf device */
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

/* xd device */
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

/* Pad multiplexing for touch_xy device */
static const unsigned touch_xy_pins[] = { 97 };
static struct spear_muxreg touch_xy_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_2,
		.mask = PMX_TOUCH_XY_MASK,
		.val = PMX_TOUCH_XY_MASK,
	},
};

static struct spear_modemux touch_xy_modemux[] = {
	{
		.muxregs = touch_xy_muxreg,
		.nmuxregs = ARRAY_SIZE(touch_xy_muxreg),
	},
};

static struct spear_pingroup touch_xy_pingroup = {
	.name = "touch_xy_grp",
	.pins = touch_xy_pins,
	.npins = ARRAY_SIZE(touch_xy_pins),
	.modemuxs = touch_xy_modemux,
	.nmodemuxs = ARRAY_SIZE(touch_xy_modemux),
};

static const char *const touch_xy_grps[] = { "touch_xy_grp" };
static struct spear_function touch_xy_function = {
	.name = "touchscreen",
	.groups = touch_xy_grps,
	.ngroups = ARRAY_SIZE(touch_xy_grps),
};

/* Pad multiplexing for uart1 device */
/* Muxed with I2C */
static const unsigned uart1_dis_i2c_pins[] = { 102, 103 };
static struct spear_muxreg uart1_dis_i2c_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_0,
		.mask = PMX_I2C0_MASK,
		.val = 0,
	},
};

static struct spear_modemux uart1_dis_i2c_modemux[] = {
	{
		.muxregs = uart1_dis_i2c_muxreg,
		.nmuxregs = ARRAY_SIZE(uart1_dis_i2c_muxreg),
	},
};

static struct spear_pingroup uart_1_dis_i2c_pingroup = {
	.name = "uart1_disable_i2c_grp",
	.pins = uart1_dis_i2c_pins,
	.npins = ARRAY_SIZE(uart1_dis_i2c_pins),
	.modemuxs = uart1_dis_i2c_modemux,
	.nmodemuxs = ARRAY_SIZE(uart1_dis_i2c_modemux),
};

/* Muxed with SD/MMC */
static const unsigned uart1_dis_sd_pins[] = { 214, 215 };
static struct spear_muxreg uart1_dis_sd_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_1,
		.mask = PMX_MCIDATA1_MASK |
			PMX_MCIDATA2_MASK,
		.val = 0,
	},
};

static struct spear_modemux uart1_dis_sd_modemux[] = {
	{
		.muxregs = uart1_dis_sd_muxreg,
		.nmuxregs = ARRAY_SIZE(uart1_dis_sd_muxreg),
	},
};

static struct spear_pingroup uart_1_dis_sd_pingroup = {
	.name = "uart1_disable_sd_grp",
	.pins = uart1_dis_sd_pins,
	.npins = ARRAY_SIZE(uart1_dis_sd_pins),
	.modemuxs = uart1_dis_sd_modemux,
	.nmodemuxs = ARRAY_SIZE(uart1_dis_sd_modemux),
};

static const char *const uart1_grps[] = { "uart1_disable_i2c_grp",
	"uart1_disable_sd_grp" };
static struct spear_function uart1_function = {
	.name = "uart1",
	.groups = uart1_grps,
	.ngroups = ARRAY_SIZE(uart1_grps),
};

/* Pad multiplexing for uart2_3 device */
static const unsigned uart2_3_pins[] = { 104, 105, 106, 107 };
static struct spear_muxreg uart2_3_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_0,
		.mask = PMX_I2S0_MASK,
		.val = 0,
	},
};

static struct spear_modemux uart2_3_modemux[] = {
	{
		.muxregs = uart2_3_muxreg,
		.nmuxregs = ARRAY_SIZE(uart2_3_muxreg),
	},
};

static struct spear_pingroup uart_2_3_pingroup = {
	.name = "uart2_3_grp",
	.pins = uart2_3_pins,
	.npins = ARRAY_SIZE(uart2_3_pins),
	.modemuxs = uart2_3_modemux,
	.nmodemuxs = ARRAY_SIZE(uart2_3_modemux),
};

static const char *const uart2_3_grps[] = { "uart2_3_grp" };
static struct spear_function uart2_3_function = {
	.name = "uart2_3",
	.groups = uart2_3_grps,
	.ngroups = ARRAY_SIZE(uart2_3_grps),
};

/* Pad multiplexing for uart4 device */
static const unsigned uart4_pins[] = { 108, 113 };
static struct spear_muxreg uart4_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_0,
		.mask = PMX_I2S0_MASK | PMX_CLCD1_MASK,
		.val = 0,
	},
};

static struct spear_modemux uart4_modemux[] = {
	{
		.muxregs = uart4_muxreg,
		.nmuxregs = ARRAY_SIZE(uart4_muxreg),
	},
};

static struct spear_pingroup uart_4_pingroup = {
	.name = "uart4_grp",
	.pins = uart4_pins,
	.npins = ARRAY_SIZE(uart4_pins),
	.modemuxs = uart4_modemux,
	.nmodemuxs = ARRAY_SIZE(uart4_modemux),
};

static const char *const uart4_grps[] = { "uart4_grp" };
static struct spear_function uart4_function = {
	.name = "uart4",
	.groups = uart4_grps,
	.ngroups = ARRAY_SIZE(uart4_grps),
};

/* Pad multiplexing for uart5 device */
static const unsigned uart5_pins[] = { 114, 115 };
static struct spear_muxreg uart5_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_0,
		.mask = PMX_CLCD1_MASK,
		.val = 0,
	},
};

static struct spear_modemux uart5_modemux[] = {
	{
		.muxregs = uart5_muxreg,
		.nmuxregs = ARRAY_SIZE(uart5_muxreg),
	},
};

static struct spear_pingroup uart_5_pingroup = {
	.name = "uart5_grp",
	.pins = uart5_pins,
	.npins = ARRAY_SIZE(uart5_pins),
	.modemuxs = uart5_modemux,
	.nmodemuxs = ARRAY_SIZE(uart5_modemux),
};

static const char *const uart5_grps[] = { "uart5_grp" };
static struct spear_function uart5_function = {
	.name = "uart5",
	.groups = uart5_grps,
	.ngroups = ARRAY_SIZE(uart5_grps),
};

/* Pad multiplexing for rs485_0_1_tdm_0_1 device */
static const unsigned rs485_0_1_tdm_0_1_pins[] = { 116, 117, 118, 119, 120, 121,
	122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135,
	136, 137 };
static struct spear_muxreg rs485_0_1_tdm_0_1_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_0,
		.mask = PMX_CLCD1_MASK,
		.val = 0,
	},
};

static struct spear_modemux rs485_0_1_tdm_0_1_modemux[] = {
	{
		.muxregs = rs485_0_1_tdm_0_1_muxreg,
		.nmuxregs = ARRAY_SIZE(rs485_0_1_tdm_0_1_muxreg),
	},
};

static struct spear_pingroup rs485_0_1_tdm_0_1_pingroup = {
	.name = "rs485_0_1_tdm_0_1_grp",
	.pins = rs485_0_1_tdm_0_1_pins,
	.npins = ARRAY_SIZE(rs485_0_1_tdm_0_1_pins),
	.modemuxs = rs485_0_1_tdm_0_1_modemux,
	.nmodemuxs = ARRAY_SIZE(rs485_0_1_tdm_0_1_modemux),
};

static const char *const rs485_0_1_tdm_0_1_grps[] = { "rs485_0_1_tdm_0_1_grp" };
static struct spear_function rs485_0_1_tdm_0_1_function = {
	.name = "rs485_0_1_tdm_0_1",
	.groups = rs485_0_1_tdm_0_1_grps,
	.ngroups = ARRAY_SIZE(rs485_0_1_tdm_0_1_grps),
};

/* Pad multiplexing for i2c_1_2 device */
static const unsigned i2c_1_2_pins[] = { 138, 139, 140, 141 };
static struct spear_muxreg i2c_1_2_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_0,
		.mask = PMX_CLCD1_MASK,
		.val = 0,
	},
};

static struct spear_modemux i2c_1_2_modemux[] = {
	{
		.muxregs = i2c_1_2_muxreg,
		.nmuxregs = ARRAY_SIZE(i2c_1_2_muxreg),
	},
};

static struct spear_pingroup i2c_1_2_pingroup = {
	.name = "i2c_1_2_grp",
	.pins = i2c_1_2_pins,
	.npins = ARRAY_SIZE(i2c_1_2_pins),
	.modemuxs = i2c_1_2_modemux,
	.nmodemuxs = ARRAY_SIZE(i2c_1_2_modemux),
};

static const char *const i2c_1_2_grps[] = { "i2c_1_2_grp" };
static struct spear_function i2c_1_2_function = {
	.name = "i2c_1_2",
	.groups = i2c_1_2_grps,
	.ngroups = ARRAY_SIZE(i2c_1_2_grps),
};

/* Pad multiplexing for i2c3_dis_smi_clcd device */
/* Muxed with SMI & CLCD */
static const unsigned i2c3_dis_smi_clcd_pins[] = { 142, 153 };
static struct spear_muxreg i2c3_dis_smi_clcd_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_0,
		.mask = PMX_CLCD1_MASK | PMX_SMI_MASK,
		.val = 0,
	},
};

static struct spear_modemux i2c3_dis_smi_clcd_modemux[] = {
	{
		.muxregs = i2c3_dis_smi_clcd_muxreg,
		.nmuxregs = ARRAY_SIZE(i2c3_dis_smi_clcd_muxreg),
	},
};

static struct spear_pingroup i2c3_dis_smi_clcd_pingroup = {
	.name = "i2c3_dis_smi_clcd_grp",
	.pins = i2c3_dis_smi_clcd_pins,
	.npins = ARRAY_SIZE(i2c3_dis_smi_clcd_pins),
	.modemuxs = i2c3_dis_smi_clcd_modemux,
	.nmodemuxs = ARRAY_SIZE(i2c3_dis_smi_clcd_modemux),
};

/* Pad multiplexing for i2c3_dis_sd_i2s0 device */
/* Muxed with SD/MMC & I2S1 */
static const unsigned i2c3_dis_sd_i2s0_pins[] = { 0, 216 };
static struct spear_muxreg i2c3_dis_sd_i2s0_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_1,
		.mask = PMX_I2S1_MASK | PMX_MCIDATA3_MASK,
		.val = 0,
	},
};

static struct spear_modemux i2c3_dis_sd_i2s0_modemux[] = {
	{
		.muxregs = i2c3_dis_sd_i2s0_muxreg,
		.nmuxregs = ARRAY_SIZE(i2c3_dis_sd_i2s0_muxreg),
	},
};

static struct spear_pingroup i2c3_dis_sd_i2s0_pingroup = {
	.name = "i2c3_dis_sd_i2s0_grp",
	.pins = i2c3_dis_sd_i2s0_pins,
	.npins = ARRAY_SIZE(i2c3_dis_sd_i2s0_pins),
	.modemuxs = i2c3_dis_sd_i2s0_modemux,
	.nmodemuxs = ARRAY_SIZE(i2c3_dis_sd_i2s0_modemux),
};

static const char *const i2c3_grps[] = { "i2c3_dis_smi_clcd_grp",
	"i2c3_dis_sd_i2s0_grp" };
static struct spear_function i2c3_unction = {
	.name = "i2c3_i2s1",
	.groups = i2c3_grps,
	.ngroups = ARRAY_SIZE(i2c3_grps),
};

/* Pad multiplexing for i2c_4_5_dis_smi device */
/* Muxed with SMI */
static const unsigned i2c_4_5_dis_smi_pins[] = { 154, 155, 156, 157 };
static struct spear_muxreg i2c_4_5_dis_smi_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_0,
		.mask = PMX_SMI_MASK,
		.val = 0,
	},
};

static struct spear_modemux i2c_4_5_dis_smi_modemux[] = {
	{
		.muxregs = i2c_4_5_dis_smi_muxreg,
		.nmuxregs = ARRAY_SIZE(i2c_4_5_dis_smi_muxreg),
	},
};

static struct spear_pingroup i2c_4_5_dis_smi_pingroup = {
	.name = "i2c_4_5_dis_smi_grp",
	.pins = i2c_4_5_dis_smi_pins,
	.npins = ARRAY_SIZE(i2c_4_5_dis_smi_pins),
	.modemuxs = i2c_4_5_dis_smi_modemux,
	.nmodemuxs = ARRAY_SIZE(i2c_4_5_dis_smi_modemux),
};

/* Pad multiplexing for i2c4_dis_sd device */
/* Muxed with SD/MMC */
static const unsigned i2c4_dis_sd_pins[] = { 217, 218 };
static struct spear_muxreg i2c4_dis_sd_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_1,
		.mask = PMX_MCIDATA4_MASK,
		.val = 0,
	}, {
		.reg = PAD_FUNCTION_EN_2,
		.mask = PMX_MCIDATA5_MASK,
		.val = 0,
	},
};

static struct spear_modemux i2c4_dis_sd_modemux[] = {
	{
		.muxregs = i2c4_dis_sd_muxreg,
		.nmuxregs = ARRAY_SIZE(i2c4_dis_sd_muxreg),
	},
};

static struct spear_pingroup i2c4_dis_sd_pingroup = {
	.name = "i2c4_dis_sd_grp",
	.pins = i2c4_dis_sd_pins,
	.npins = ARRAY_SIZE(i2c4_dis_sd_pins),
	.modemuxs = i2c4_dis_sd_modemux,
	.nmodemuxs = ARRAY_SIZE(i2c4_dis_sd_modemux),
};

/* Pad multiplexing for i2c5_dis_sd device */
/* Muxed with SD/MMC */
static const unsigned i2c5_dis_sd_pins[] = { 219, 220 };
static struct spear_muxreg i2c5_dis_sd_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_2,
		.mask = PMX_MCIDATA6_MASK |
			PMX_MCIDATA7_MASK,
		.val = 0,
	},
};

static struct spear_modemux i2c5_dis_sd_modemux[] = {
	{
		.muxregs = i2c5_dis_sd_muxreg,
		.nmuxregs = ARRAY_SIZE(i2c5_dis_sd_muxreg),
	},
};

static struct spear_pingroup i2c5_dis_sd_pingroup = {
	.name = "i2c5_dis_sd_grp",
	.pins = i2c5_dis_sd_pins,
	.npins = ARRAY_SIZE(i2c5_dis_sd_pins),
	.modemuxs = i2c5_dis_sd_modemux,
	.nmodemuxs = ARRAY_SIZE(i2c5_dis_sd_modemux),
};

static const char *const i2c_4_5_grps[] = { "i2c5_dis_sd_grp",
	"i2c4_dis_sd_grp", "i2c_4_5_dis_smi_grp" };
static struct spear_function i2c_4_5_function = {
	.name = "i2c_4_5",
	.groups = i2c_4_5_grps,
	.ngroups = ARRAY_SIZE(i2c_4_5_grps),
};

/* Pad multiplexing for i2c_6_7_dis_kbd device */
/* Muxed with KBD */
static const unsigned i2c_6_7_dis_kbd_pins[] = { 207, 208, 209, 210 };
static struct spear_muxreg i2c_6_7_dis_kbd_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_1,
		.mask = PMX_KBD_ROWCOL25_MASK,
		.val = 0,
	},
};

static struct spear_modemux i2c_6_7_dis_kbd_modemux[] = {
	{
		.muxregs = i2c_6_7_dis_kbd_muxreg,
		.nmuxregs = ARRAY_SIZE(i2c_6_7_dis_kbd_muxreg),
	},
};

static struct spear_pingroup i2c_6_7_dis_kbd_pingroup = {
	.name = "i2c_6_7_dis_kbd_grp",
	.pins = i2c_6_7_dis_kbd_pins,
	.npins = ARRAY_SIZE(i2c_6_7_dis_kbd_pins),
	.modemuxs = i2c_6_7_dis_kbd_modemux,
	.nmodemuxs = ARRAY_SIZE(i2c_6_7_dis_kbd_modemux),
};

/* Pad multiplexing for i2c6_dis_sd device */
/* Muxed with SD/MMC */
static const unsigned i2c6_dis_sd_pins[] = { 236, 237 };
static struct spear_muxreg i2c6_dis_sd_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_2,
		.mask = PMX_MCIIORDRE_MASK |
			PMX_MCIIOWRWE_MASK,
		.val = 0,
	},
};

static struct spear_modemux i2c6_dis_sd_modemux[] = {
	{
		.muxregs = i2c6_dis_sd_muxreg,
		.nmuxregs = ARRAY_SIZE(i2c6_dis_sd_muxreg),
	},
};

static struct spear_pingroup i2c6_dis_sd_pingroup = {
	.name = "i2c6_dis_sd_grp",
	.pins = i2c6_dis_sd_pins,
	.npins = ARRAY_SIZE(i2c6_dis_sd_pins),
	.modemuxs = i2c6_dis_sd_modemux,
	.nmodemuxs = ARRAY_SIZE(i2c6_dis_sd_modemux),
};

/* Pad multiplexing for i2c7_dis_sd device */
static const unsigned i2c7_dis_sd_pins[] = { 238, 239 };
static struct spear_muxreg i2c7_dis_sd_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_2,
		.mask = PMX_MCIRESETCF_MASK |
			PMX_MCICS0CE_MASK,
		.val = 0,
	},
};

static struct spear_modemux i2c7_dis_sd_modemux[] = {
	{
		.muxregs = i2c7_dis_sd_muxreg,
		.nmuxregs = ARRAY_SIZE(i2c7_dis_sd_muxreg),
	},
};

static struct spear_pingroup i2c7_dis_sd_pingroup = {
	.name = "i2c7_dis_sd_grp",
	.pins = i2c7_dis_sd_pins,
	.npins = ARRAY_SIZE(i2c7_dis_sd_pins),
	.modemuxs = i2c7_dis_sd_modemux,
	.nmodemuxs = ARRAY_SIZE(i2c7_dis_sd_modemux),
};

static const char *const i2c_6_7_grps[] = { "i2c6_dis_sd_grp",
	"i2c7_dis_sd_grp", "i2c_6_7_dis_kbd_grp" };
static struct spear_function i2c_6_7_function = {
	.name = "i2c_6_7",
	.groups = i2c_6_7_grps,
	.ngroups = ARRAY_SIZE(i2c_6_7_grps),
};

/* Pad multiplexing for can0_dis_nor device */
/* Muxed with NOR */
static const unsigned can0_dis_nor_pins[] = { 56, 57 };
static struct spear_muxreg can0_dis_nor_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_0,
		.mask = PMX_NFRSTPWDWN2_MASK,
		.val = 0,
	}, {
		.reg = PAD_FUNCTION_EN_1,
		.mask = PMX_NFRSTPWDWN3_MASK,
		.val = 0,
	},
};

static struct spear_modemux can0_dis_nor_modemux[] = {
	{
		.muxregs = can0_dis_nor_muxreg,
		.nmuxregs = ARRAY_SIZE(can0_dis_nor_muxreg),
	},
};

static struct spear_pingroup can0_dis_nor_pingroup = {
	.name = "can0_dis_nor_grp",
	.pins = can0_dis_nor_pins,
	.npins = ARRAY_SIZE(can0_dis_nor_pins),
	.modemuxs = can0_dis_nor_modemux,
	.nmodemuxs = ARRAY_SIZE(can0_dis_nor_modemux),
};

/* Pad multiplexing for can0_dis_sd device */
/* Muxed with SD/MMC */
static const unsigned can0_dis_sd_pins[] = { 240, 241 };
static struct spear_muxreg can0_dis_sd_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_2,
		.mask = PMX_MCICFINTR_MASK | PMX_MCIIORDY_MASK,
		.val = 0,
	},
};

static struct spear_modemux can0_dis_sd_modemux[] = {
	{
		.muxregs = can0_dis_sd_muxreg,
		.nmuxregs = ARRAY_SIZE(can0_dis_sd_muxreg),
	},
};

static struct spear_pingroup can0_dis_sd_pingroup = {
	.name = "can0_dis_sd_grp",
	.pins = can0_dis_sd_pins,
	.npins = ARRAY_SIZE(can0_dis_sd_pins),
	.modemuxs = can0_dis_sd_modemux,
	.nmodemuxs = ARRAY_SIZE(can0_dis_sd_modemux),
};

static const char *const can0_grps[] = { "can0_dis_nor_grp", "can0_dis_sd_grp"
};
static struct spear_function can0_function = {
	.name = "can0",
	.groups = can0_grps,
	.ngroups = ARRAY_SIZE(can0_grps),
};

/* Pad multiplexing for can1_dis_sd device */
/* Muxed with SD/MMC */
static const unsigned can1_dis_sd_pins[] = { 242, 243 };
static struct spear_muxreg can1_dis_sd_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_2,
		.mask = PMX_MCICS1_MASK | PMX_MCIDMAACK_MASK,
		.val = 0,
	},
};

static struct spear_modemux can1_dis_sd_modemux[] = {
	{
		.muxregs = can1_dis_sd_muxreg,
		.nmuxregs = ARRAY_SIZE(can1_dis_sd_muxreg),
	},
};

static struct spear_pingroup can1_dis_sd_pingroup = {
	.name = "can1_dis_sd_grp",
	.pins = can1_dis_sd_pins,
	.npins = ARRAY_SIZE(can1_dis_sd_pins),
	.modemuxs = can1_dis_sd_modemux,
	.nmodemuxs = ARRAY_SIZE(can1_dis_sd_modemux),
};

/* Pad multiplexing for can1_dis_kbd device */
/* Muxed with KBD */
static const unsigned can1_dis_kbd_pins[] = { 201, 202 };
static struct spear_muxreg can1_dis_kbd_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_1,
		.mask = PMX_KBD_ROWCOL25_MASK,
		.val = 0,
	},
};

static struct spear_modemux can1_dis_kbd_modemux[] = {
	{
		.muxregs = can1_dis_kbd_muxreg,
		.nmuxregs = ARRAY_SIZE(can1_dis_kbd_muxreg),
	},
};

static struct spear_pingroup can1_dis_kbd_pingroup = {
	.name = "can1_dis_kbd_grp",
	.pins = can1_dis_kbd_pins,
	.npins = ARRAY_SIZE(can1_dis_kbd_pins),
	.modemuxs = can1_dis_kbd_modemux,
	.nmodemuxs = ARRAY_SIZE(can1_dis_kbd_modemux),
};

static const char *const can1_grps[] = { "can1_dis_sd_grp", "can1_dis_kbd_grp"
};
static struct spear_function can1_function = {
	.name = "can1",
	.groups = can1_grps,
	.ngroups = ARRAY_SIZE(can1_grps),
};

/* Pad multiplexing for pci device */
static const unsigned pci_sata_pins[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 18,
	19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
	37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54,
	55, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99 };
#define PCI_SATA_MUXREG				\
	{					\
		.reg = PAD_FUNCTION_EN_0,	\
		.mask = PMX_MCI_DATA8_15_MASK,	\
		.val = 0,			\
	}, {					\
		.reg = PAD_FUNCTION_EN_1,	\
		.mask = PMX_PCI_REG1_MASK,	\
		.val = 0,			\
	}, {					\
		.reg = PAD_FUNCTION_EN_2,	\
		.mask = PMX_PCI_REG2_MASK,	\
		.val = 0,			\
	}

/* pad multiplexing for pcie0 device */
static struct spear_muxreg pcie0_muxreg[] = {
	PCI_SATA_MUXREG,
	{
		.reg = PCIE_SATA_CFG,
		.mask = PCIE_CFG_VAL(0),
		.val = PCIE_CFG_VAL(0),
	},
};

static struct spear_modemux pcie0_modemux[] = {
	{
		.muxregs = pcie0_muxreg,
		.nmuxregs = ARRAY_SIZE(pcie0_muxreg),
	},
};

static struct spear_pingroup pcie0_pingroup = {
	.name = "pcie0_grp",
	.pins = pci_sata_pins,
	.npins = ARRAY_SIZE(pci_sata_pins),
	.modemuxs = pcie0_modemux,
	.nmodemuxs = ARRAY_SIZE(pcie0_modemux),
};

/* pad multiplexing for pcie1 device */
static struct spear_muxreg pcie1_muxreg[] = {
	PCI_SATA_MUXREG,
	{
		.reg = PCIE_SATA_CFG,
		.mask = PCIE_CFG_VAL(1),
		.val = PCIE_CFG_VAL(1),
	},
};

static struct spear_modemux pcie1_modemux[] = {
	{
		.muxregs = pcie1_muxreg,
		.nmuxregs = ARRAY_SIZE(pcie1_muxreg),
	},
};

static struct spear_pingroup pcie1_pingroup = {
	.name = "pcie1_grp",
	.pins = pci_sata_pins,
	.npins = ARRAY_SIZE(pci_sata_pins),
	.modemuxs = pcie1_modemux,
	.nmodemuxs = ARRAY_SIZE(pcie1_modemux),
};

/* pad multiplexing for pcie2 device */
static struct spear_muxreg pcie2_muxreg[] = {
	PCI_SATA_MUXREG,
	{
		.reg = PCIE_SATA_CFG,
		.mask = PCIE_CFG_VAL(2),
		.val = PCIE_CFG_VAL(2),
	},
};

static struct spear_modemux pcie2_modemux[] = {
	{
		.muxregs = pcie2_muxreg,
		.nmuxregs = ARRAY_SIZE(pcie2_muxreg),
	},
};

static struct spear_pingroup pcie2_pingroup = {
	.name = "pcie2_grp",
	.pins = pci_sata_pins,
	.npins = ARRAY_SIZE(pci_sata_pins),
	.modemuxs = pcie2_modemux,
	.nmodemuxs = ARRAY_SIZE(pcie2_modemux),
};

static const char *const pci_grps[] = { "pcie0_grp", "pcie1_grp", "pcie2_grp" };
static struct spear_function pci_function = {
	.name = "pci",
	.groups = pci_grps,
	.ngroups = ARRAY_SIZE(pci_grps),
};

/* pad multiplexing for sata0 device */
static struct spear_muxreg sata0_muxreg[] = {
	PCI_SATA_MUXREG,
	{
		.reg = PCIE_SATA_CFG,
		.mask = SATA_CFG_VAL(0),
		.val = SATA_CFG_VAL(0),
	},
};

static struct spear_modemux sata0_modemux[] = {
	{
		.muxregs = sata0_muxreg,
		.nmuxregs = ARRAY_SIZE(sata0_muxreg),
	},
};

static struct spear_pingroup sata0_pingroup = {
	.name = "sata0_grp",
	.pins = pci_sata_pins,
	.npins = ARRAY_SIZE(pci_sata_pins),
	.modemuxs = sata0_modemux,
	.nmodemuxs = ARRAY_SIZE(sata0_modemux),
};

/* pad multiplexing for sata1 device */
static struct spear_muxreg sata1_muxreg[] = {
	PCI_SATA_MUXREG,
	{
		.reg = PCIE_SATA_CFG,
		.mask = SATA_CFG_VAL(1),
		.val = SATA_CFG_VAL(1),
	},
};

static struct spear_modemux sata1_modemux[] = {
	{
		.muxregs = sata1_muxreg,
		.nmuxregs = ARRAY_SIZE(sata1_muxreg),
	},
};

static struct spear_pingroup sata1_pingroup = {
	.name = "sata1_grp",
	.pins = pci_sata_pins,
	.npins = ARRAY_SIZE(pci_sata_pins),
	.modemuxs = sata1_modemux,
	.nmodemuxs = ARRAY_SIZE(sata1_modemux),
};

/* pad multiplexing for sata2 device */
static struct spear_muxreg sata2_muxreg[] = {
	PCI_SATA_MUXREG,
	{
		.reg = PCIE_SATA_CFG,
		.mask = SATA_CFG_VAL(2),
		.val = SATA_CFG_VAL(2),
	},
};

static struct spear_modemux sata2_modemux[] = {
	{
		.muxregs = sata2_muxreg,
		.nmuxregs = ARRAY_SIZE(sata2_muxreg),
	},
};

static struct spear_pingroup sata2_pingroup = {
	.name = "sata2_grp",
	.pins = pci_sata_pins,
	.npins = ARRAY_SIZE(pci_sata_pins),
	.modemuxs = sata2_modemux,
	.nmodemuxs = ARRAY_SIZE(sata2_modemux),
};

static const char *const sata_grps[] = { "sata0_grp", "sata1_grp", "sata2_grp"
};
static struct spear_function sata_function = {
	.name = "sata",
	.groups = sata_grps,
	.ngroups = ARRAY_SIZE(sata_grps),
};

/* Pad multiplexing for ssp1_dis_kbd device */
static const unsigned ssp1_dis_kbd_pins[] = { 203, 204, 205, 206 };
static struct spear_muxreg ssp1_dis_kbd_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_1,
		.mask = PMX_KBD_ROWCOL25_MASK | PMX_KBD_COL1_MASK |
			PMX_KBD_COL0_MASK | PMX_NFIO8_15_MASK | PMX_NFCE1_MASK |
			PMX_NFCE2_MASK,
		.val = 0,
	},
};

static struct spear_modemux ssp1_dis_kbd_modemux[] = {
	{
		.muxregs = ssp1_dis_kbd_muxreg,
		.nmuxregs = ARRAY_SIZE(ssp1_dis_kbd_muxreg),
	},
};

static struct spear_pingroup ssp1_dis_kbd_pingroup = {
	.name = "ssp1_dis_kbd_grp",
	.pins = ssp1_dis_kbd_pins,
	.npins = ARRAY_SIZE(ssp1_dis_kbd_pins),
	.modemuxs = ssp1_dis_kbd_modemux,
	.nmodemuxs = ARRAY_SIZE(ssp1_dis_kbd_modemux),
};

/* Pad multiplexing for ssp1_dis_sd device */
static const unsigned ssp1_dis_sd_pins[] = { 224, 226, 227, 228 };
static struct spear_muxreg ssp1_dis_sd_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_2,
		.mask = PMX_MCIADDR0ALE_MASK | PMX_MCIADDR2_MASK |
			PMX_MCICECF_MASK | PMX_MCICEXD_MASK,
		.val = 0,
	},
};

static struct spear_modemux ssp1_dis_sd_modemux[] = {
	{
		.muxregs = ssp1_dis_sd_muxreg,
		.nmuxregs = ARRAY_SIZE(ssp1_dis_sd_muxreg),
	},
};

static struct spear_pingroup ssp1_dis_sd_pingroup = {
	.name = "ssp1_dis_sd_grp",
	.pins = ssp1_dis_sd_pins,
	.npins = ARRAY_SIZE(ssp1_dis_sd_pins),
	.modemuxs = ssp1_dis_sd_modemux,
	.nmodemuxs = ARRAY_SIZE(ssp1_dis_sd_modemux),
};

static const char *const ssp1_grps[] = { "ssp1_dis_kbd_grp",
	"ssp1_dis_sd_grp" };
static struct spear_function ssp1_function = {
	.name = "ssp1",
	.groups = ssp1_grps,
	.ngroups = ARRAY_SIZE(ssp1_grps),
};

/* Pad multiplexing for gpt64 device */
static const unsigned gpt64_pins[] = { 230, 231, 232, 245 };
static struct spear_muxreg gpt64_muxreg[] = {
	{
		.reg = PAD_FUNCTION_EN_2,
		.mask = PMX_MCICDCF1_MASK | PMX_MCICDCF2_MASK | PMX_MCICDXD_MASK
			| PMX_MCILEDS_MASK,
		.val = 0,
	},
};

static struct spear_modemux gpt64_modemux[] = {
	{
		.muxregs = gpt64_muxreg,
		.nmuxregs = ARRAY_SIZE(gpt64_muxreg),
	},
};

static struct spear_pingroup gpt64_pingroup = {
	.name = "gpt64_grp",
	.pins = gpt64_pins,
	.npins = ARRAY_SIZE(gpt64_pins),
	.modemuxs = gpt64_modemux,
	.nmodemuxs = ARRAY_SIZE(gpt64_modemux),
};

static const char *const gpt64_grps[] = { "gpt64_grp" };
static struct spear_function gpt64_function = {
	.name = "gpt64",
	.groups = gpt64_grps,
	.ngroups = ARRAY_SIZE(gpt64_grps),
};

/* pingroups */
static struct spear_pingroup *spear1310_pingroups[] = {
	&i2c0_pingroup,
	&ssp0_pingroup,
	&i2s0_pingroup,
	&i2s1_pingroup,
	&clcd_pingroup,
	&clcd_high_res_pingroup,
	&arm_gpio_pingroup,
	&smi_2_chips_pingroup,
	&smi_4_chips_pingroup,
	&gmii_pingroup,
	&rgmii_pingroup,
	&smii_0_1_2_pingroup,
	&ras_mii_txclk_pingroup,
	&nand_8bit_pingroup,
	&nand_16bit_pingroup,
	&nand_4_chips_pingroup,
	&keyboard_6x6_pingroup,
	&keyboard_rowcol6_8_pingroup,
	&uart0_pingroup,
	&uart0_modem_pingroup,
	&gpt0_tmr0_pingroup,
	&gpt0_tmr1_pingroup,
	&gpt1_tmr0_pingroup,
	&gpt1_tmr1_pingroup,
	&sdhci_pingroup,
	&cf_pingroup,
	&xd_pingroup,
	&touch_xy_pingroup,
	&ssp0_cs0_pingroup,
	&ssp0_cs1_2_pingroup,
	&uart_1_dis_i2c_pingroup,
	&uart_1_dis_sd_pingroup,
	&uart_2_3_pingroup,
	&uart_4_pingroup,
	&uart_5_pingroup,
	&rs485_0_1_tdm_0_1_pingroup,
	&i2c_1_2_pingroup,
	&i2c3_dis_smi_clcd_pingroup,
	&i2c3_dis_sd_i2s0_pingroup,
	&i2c_4_5_dis_smi_pingroup,
	&i2c4_dis_sd_pingroup,
	&i2c5_dis_sd_pingroup,
	&i2c_6_7_dis_kbd_pingroup,
	&i2c6_dis_sd_pingroup,
	&i2c7_dis_sd_pingroup,
	&can0_dis_nor_pingroup,
	&can0_dis_sd_pingroup,
	&can1_dis_sd_pingroup,
	&can1_dis_kbd_pingroup,
	&pcie0_pingroup,
	&pcie1_pingroup,
	&pcie2_pingroup,
	&sata0_pingroup,
	&sata1_pingroup,
	&sata2_pingroup,
	&ssp1_dis_kbd_pingroup,
	&ssp1_dis_sd_pingroup,
	&gpt64_pingroup,
};

/* functions */
static struct spear_function *spear1310_functions[] = {
	&i2c0_function,
	&ssp0_function,
	&i2s0_function,
	&i2s1_function,
	&clcd_function,
	&arm_gpio_function,
	&smi_function,
	&gmii_function,
	&rgmii_function,
	&smii_0_1_2_function,
	&ras_mii_txclk_function,
	&nand_function,
	&keyboard_function,
	&uart0_function,
	&gpt0_function,
	&gpt1_function,
	&sdhci_function,
	&cf_function,
	&xd_function,
	&touch_xy_function,
	&uart1_function,
	&uart2_3_function,
	&uart4_function,
	&uart5_function,
	&rs485_0_1_tdm_0_1_function,
	&i2c_1_2_function,
	&i2c3_unction,
	&i2c_4_5_function,
	&i2c_6_7_function,
	&can0_function,
	&can1_function,
	&pci_function,
	&sata_function,
	&ssp1_function,
	&gpt64_function,
};

static struct spear_pinctrl_machdata spear1310_machdata = {
	.pins = spear1310_pins,
	.npins = ARRAY_SIZE(spear1310_pins),
	.groups = spear1310_pingroups,
	.ngroups = ARRAY_SIZE(spear1310_pingroups),
	.functions = spear1310_functions,
	.nfunctions = ARRAY_SIZE(spear1310_functions),
	.modes_supported = false,
};

static struct of_device_id spear1310_pinctrl_of_match[] __devinitdata = {
	{
		.compatible = "st,spear1310-pinmux",
	},
	{},
};

static int __devinit spear1310_pinctrl_probe(struct platform_device *pdev)
{
	return spear_pinctrl_probe(pdev, &spear1310_machdata);
}

static int __devexit spear1310_pinctrl_remove(struct platform_device *pdev)
{
	return spear_pinctrl_remove(pdev);
}

static struct platform_driver spear1310_pinctrl_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = spear1310_pinctrl_of_match,
	},
	.probe = spear1310_pinctrl_probe,
	.remove = __devexit_p(spear1310_pinctrl_remove),
};

static int __init spear1310_pinctrl_init(void)
{
	return platform_driver_register(&spear1310_pinctrl_driver);
}
arch_initcall(spear1310_pinctrl_init);

static void __exit spear1310_pinctrl_exit(void)
{
	platform_driver_unregister(&spear1310_pinctrl_driver);
}
module_exit(spear1310_pinctrl_exit);

MODULE_AUTHOR("Viresh Kumar <viresh.kumar@st.com>");
MODULE_DESCRIPTION("ST Microelectronics SPEAr1310 pinctrl driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, spear1310_pinctrl_of_match);
