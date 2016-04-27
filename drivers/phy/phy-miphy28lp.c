/*
 * Copyright (C) 2014 STMicroelectronics
 *
 * STMicroelectronics PHY driver MiPHY28lp (for SoC STiH407).
 *
 * Author: Alexandre Torgue <alexandre.torgue@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */

#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/clk.h>
#include <linux/phy/phy.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include <dt-bindings/phy/phy.h>

/* MiPHY registers */
#define MIPHY_CONF_RESET		0x00
#define RST_APPLI_SW		BIT(0)
#define RST_CONF_SW		BIT(1)
#define RST_MACRO_SW		BIT(2)

#define MIPHY_RESET			0x01
#define RST_PLL_SW		BIT(0)
#define RST_COMP_SW		BIT(2)

#define MIPHY_STATUS_1			0x02
#define PHY_RDY			BIT(0)
#define HFC_RDY			BIT(1)
#define HFC_PLL			BIT(2)

#define MIPHY_CONTROL			0x04
#define TERM_EN_SW		BIT(2)
#define DIS_LINK_RST		BIT(3)
#define AUTO_RST_RX		BIT(4)
#define PX_RX_POL		BIT(5)

#define MIPHY_BOUNDARY_SEL		0x0a
#define TX_SEL			BIT(6)
#define SSC_SEL			BIT(4)
#define GENSEL_SEL		BIT(0)

#define MIPHY_BOUNDARY_1		0x0b
#define MIPHY_BOUNDARY_2		0x0c
#define SSC_EN_SW		BIT(2)

#define MIPHY_PLL_CLKREF_FREQ		0x0d
#define MIPHY_SPEED			0x0e
#define TX_SPDSEL_80DEC		0
#define TX_SPDSEL_40DEC		1
#define TX_SPDSEL_20DEC		2
#define RX_SPDSEL_80DEC		0
#define RX_SPDSEL_40DEC		(1 << 2)
#define RX_SPDSEL_20DEC		(2 << 2)

#define MIPHY_CONF			0x0f
#define MIPHY_CTRL_TEST_SEL		0x20
#define MIPHY_CTRL_TEST_1		0x21
#define MIPHY_CTRL_TEST_2		0x22
#define MIPHY_CTRL_TEST_3		0x23
#define MIPHY_CTRL_TEST_4		0x24
#define MIPHY_FEEDBACK_TEST		0x25
#define MIPHY_DEBUG_BUS			0x26
#define MIPHY_DEBUG_STATUS_MSB		0x27
#define MIPHY_DEBUG_STATUS_LSB		0x28
#define MIPHY_PWR_RAIL_1		0x29
#define MIPHY_PWR_RAIL_2		0x2a
#define MIPHY_SYNCHAR_CONTROL		0x30

#define MIPHY_COMP_FSM_1		0x3a
#define COMP_START		BIT(6)

#define MIPHY_COMP_FSM_6		0x3f
#define COMP_DONE		BIT(7)

#define MIPHY_COMP_POSTP		0x42
#define MIPHY_TX_CTRL_1			0x49
#define TX_REG_STEP_0V		0
#define TX_REG_STEP_P_25MV	1
#define TX_REG_STEP_P_50MV	2
#define TX_REG_STEP_N_25MV	7
#define TX_REG_STEP_N_50MV	6
#define TX_REG_STEP_N_75MV	5

#define MIPHY_TX_CTRL_2			0x4a
#define TX_SLEW_SW_40_PS	0
#define TX_SLEW_SW_80_PS	1
#define TX_SLEW_SW_120_PS	2

#define MIPHY_TX_CTRL_3			0x4b
#define MIPHY_TX_CAL_MAN		0x4e
#define TX_SLEW_CAL_MAN_EN	BIT(0)

#define MIPHY_TST_BIAS_BOOST_2		0x62
#define MIPHY_BIAS_BOOST_1		0x63
#define MIPHY_BIAS_BOOST_2		0x64
#define MIPHY_RX_DESBUFF_FDB_2		0x67
#define MIPHY_RX_DESBUFF_FDB_3		0x68
#define MIPHY_SIGDET_COMPENS1		0x69
#define MIPHY_SIGDET_COMPENS2		0x6a
#define MIPHY_JITTER_PERIOD		0x6b
#define MIPHY_JITTER_AMPLITUDE_1	0x6c
#define MIPHY_JITTER_AMPLITUDE_2	0x6d
#define MIPHY_JITTER_AMPLITUDE_3	0x6e
#define MIPHY_RX_K_GAIN			0x78
#define MIPHY_RX_BUFFER_CTRL		0x7a
#define VGA_GAIN		BIT(0)
#define EQ_DC_GAIN		BIT(2)
#define EQ_BOOST_GAIN		BIT(3)

#define MIPHY_RX_VGA_GAIN		0x7b
#define MIPHY_RX_EQU_GAIN_1		0x7f
#define MIPHY_RX_EQU_GAIN_2		0x80
#define MIPHY_RX_EQU_GAIN_3		0x81
#define MIPHY_RX_CAL_CTRL_1		0x97
#define MIPHY_RX_CAL_CTRL_2		0x98

#define MIPHY_RX_CAL_OFFSET_CTRL	0x99
#define CAL_OFFSET_VGA_64	(0x03 << 0)
#define CAL_OFFSET_THRESHOLD_64	(0x03 << 2)
#define VGA_OFFSET_POLARITY	BIT(4)
#define OFFSET_COMPENSATION_EN	BIT(6)

#define MIPHY_RX_CAL_VGA_STEP		0x9a
#define MIPHY_RX_CAL_EYE_MIN		0x9d
#define MIPHY_RX_CAL_OPT_LENGTH		0x9f
#define MIPHY_RX_LOCK_CTRL_1		0xc1
#define MIPHY_RX_LOCK_SETTINGS_OPT	0xc2
#define MIPHY_RX_LOCK_STEP		0xc4

#define MIPHY_RX_SIGDET_SLEEP_OA	0xc9
#define MIPHY_RX_SIGDET_SLEEP_SEL	0xca
#define MIPHY_RX_SIGDET_WAIT_SEL	0xcb
#define MIPHY_RX_SIGDET_DATA_SEL	0xcc
#define EN_ULTRA_LOW_POWER	BIT(0)
#define EN_FIRST_HALF		BIT(1)
#define EN_SECOND_HALF		BIT(2)
#define EN_DIGIT_SIGNAL_CHECK	BIT(3)

#define MIPHY_RX_POWER_CTRL_1		0xcd
#define MIPHY_RX_POWER_CTRL_2		0xce
#define MIPHY_PLL_CALSET_CTRL		0xd3
#define MIPHY_PLL_CALSET_1		0xd4
#define MIPHY_PLL_CALSET_2		0xd5
#define MIPHY_PLL_CALSET_3		0xd6
#define MIPHY_PLL_CALSET_4		0xd7
#define MIPHY_PLL_SBR_1			0xe3
#define SET_NEW_CHANGE		BIT(1)

#define MIPHY_PLL_SBR_2			0xe4
#define MIPHY_PLL_SBR_3			0xe5
#define MIPHY_PLL_SBR_4			0xe6
#define MIPHY_PLL_COMMON_MISC_2		0xe9
#define START_ACT_FILT		BIT(6)

#define MIPHY_PLL_SPAREIN		0xeb

/*
 * On STiH407 the glue logic can be different among MiPHY devices; for example:
 * MiPHY0: OSC_FORCE_EXT means:
 *  0: 30MHz crystal clk - 1: 100MHz ext clk routed through MiPHY1
 * MiPHY1: OSC_FORCE_EXT means:
 *  1: 30MHz crystal clk - 0: 100MHz ext clk routed through MiPHY1
 * Some devices have not the possibility to check if the osc is ready.
 */
#define MIPHY_OSC_FORCE_EXT	BIT(3)
#define MIPHY_OSC_RDY		BIT(5)

#define MIPHY_CTRL_MASK		0x0f
#define MIPHY_CTRL_DEFAULT	0
#define MIPHY_CTRL_SYNC_D_EN	BIT(2)

/* SATA / PCIe defines */
#define SATA_CTRL_MASK		0x07
#define PCIE_CTRL_MASK		0xff
#define SATA_CTRL_SELECT_SATA	1
#define SATA_CTRL_SELECT_PCIE	0
#define SYSCFG_PCIE_PCIE_VAL	0x80
#define SATA_SPDMODE		1

#define MIPHY_SATA_BANK_NB	3
#define MIPHY_PCIE_BANK_NB	2

enum {
	SYSCFG_CTRL,
	SYSCFG_STATUS,
	SYSCFG_PCI,
	SYSCFG_SATA,
	SYSCFG_REG_MAX,
};

struct miphy28lp_phy {
	struct phy *phy;
	struct miphy28lp_dev *phydev;
	void __iomem *base;
	void __iomem *pipebase;

	bool osc_force_ext;
	bool osc_rdy;
	bool px_rx_pol_inv;
	bool ssc;
	bool tx_impedance;

	struct reset_control *miphy_rst;

	u32 sata_gen;

	/* Sysconfig registers offsets needed to configure the device */
	u32 syscfg_reg[SYSCFG_REG_MAX];
	u8 type;
};

struct miphy28lp_dev {
	struct device *dev;
	struct regmap *regmap;
	struct mutex miphy_mutex;
	struct miphy28lp_phy **phys;
	int nphys;
};

struct miphy_initval {
	u16 reg;
	u16 val;
};

enum miphy_sata_gen { SATA_GEN1, SATA_GEN2, SATA_GEN3 };

static char *PHY_TYPE_name[] = { "sata-up", "pcie-up", "", "usb3-up" };

struct pll_ratio {
	int clk_ref;
	int calset_1;
	int calset_2;
	int calset_3;
	int calset_4;
	int cal_ctrl;
};

static struct pll_ratio sata_pll_ratio = {
	.clk_ref = 0x1e,
	.calset_1 = 0xc8,
	.calset_2 = 0x00,
	.calset_3 = 0x00,
	.calset_4 = 0x00,
	.cal_ctrl = 0x00,
};

static struct pll_ratio pcie_pll_ratio = {
	.clk_ref = 0x1e,
	.calset_1 = 0xa6,
	.calset_2 = 0xaa,
	.calset_3 = 0xaa,
	.calset_4 = 0x00,
	.cal_ctrl = 0x00,
};

static struct pll_ratio usb3_pll_ratio = {
	.clk_ref = 0x1e,
	.calset_1 = 0xa6,
	.calset_2 = 0xaa,
	.calset_3 = 0xaa,
	.calset_4 = 0x04,
	.cal_ctrl = 0x00,
};

struct miphy28lp_pll_gen {
	int bank;
	int speed;
	int bias_boost_1;
	int bias_boost_2;
	int tx_ctrl_1;
	int tx_ctrl_2;
	int tx_ctrl_3;
	int rx_k_gain;
	int rx_vga_gain;
	int rx_equ_gain_1;
	int rx_equ_gain_2;
	int rx_equ_gain_3;
	int rx_buff_ctrl;
};

static struct miphy28lp_pll_gen sata_pll_gen[] = {
	{
		.bank		= 0x00,
		.speed		= TX_SPDSEL_80DEC | RX_SPDSEL_80DEC,
		.bias_boost_1	= 0x00,
		.bias_boost_2	= 0xae,
		.tx_ctrl_2	= 0x53,
		.tx_ctrl_3	= 0x00,
		.rx_buff_ctrl	= EQ_BOOST_GAIN | EQ_DC_GAIN | VGA_GAIN,
		.rx_vga_gain	= 0x00,
		.rx_equ_gain_1	= 0x7d,
		.rx_equ_gain_2	= 0x56,
		.rx_equ_gain_3	= 0x00,
	},
	{
		.bank		= 0x01,
		.speed		= TX_SPDSEL_40DEC | RX_SPDSEL_40DEC,
		.bias_boost_1	= 0x00,
		.bias_boost_2	= 0xae,
		.tx_ctrl_2	= 0x72,
		.tx_ctrl_3	= 0x20,
		.rx_buff_ctrl	= EQ_BOOST_GAIN | EQ_DC_GAIN | VGA_GAIN,
		.rx_vga_gain	= 0x00,
		.rx_equ_gain_1	= 0x7d,
		.rx_equ_gain_2	= 0x56,
		.rx_equ_gain_3	= 0x00,
	},
	{
		.bank		= 0x02,
		.speed		= TX_SPDSEL_20DEC | RX_SPDSEL_20DEC,
		.bias_boost_1	= 0x00,
		.bias_boost_2	= 0xae,
		.tx_ctrl_2	= 0xc0,
		.tx_ctrl_3	= 0x20,
		.rx_buff_ctrl	= EQ_BOOST_GAIN | EQ_DC_GAIN | VGA_GAIN,
		.rx_vga_gain	= 0x00,
		.rx_equ_gain_1	= 0x7d,
		.rx_equ_gain_2	= 0x56,
		.rx_equ_gain_3	= 0x00,
	},
};

static struct miphy28lp_pll_gen pcie_pll_gen[] = {
	{
		.bank		= 0x00,
		.speed		= TX_SPDSEL_40DEC | RX_SPDSEL_40DEC,
		.bias_boost_1	= 0x00,
		.bias_boost_2	= 0xa5,
		.tx_ctrl_1	= TX_REG_STEP_N_25MV,
		.tx_ctrl_2	= 0x71,
		.tx_ctrl_3	= 0x60,
		.rx_k_gain	= 0x98,
		.rx_buff_ctrl	= EQ_BOOST_GAIN | EQ_DC_GAIN | VGA_GAIN,
		.rx_vga_gain	= 0x00,
		.rx_equ_gain_1	= 0x79,
		.rx_equ_gain_2	= 0x56,
	},
	{
		.bank		= 0x01,
		.speed		= TX_SPDSEL_20DEC | RX_SPDSEL_20DEC,
		.bias_boost_1	= 0x00,
		.bias_boost_2	= 0xa5,
		.tx_ctrl_1	= TX_REG_STEP_N_25MV,
		.tx_ctrl_2	= 0x70,
		.tx_ctrl_3	= 0x60,
		.rx_k_gain	= 0xcc,
		.rx_buff_ctrl	= EQ_BOOST_GAIN | EQ_DC_GAIN | VGA_GAIN,
		.rx_vga_gain	= 0x00,
		.rx_equ_gain_1	= 0x78,
		.rx_equ_gain_2	= 0x07,
	},
};

static inline void miphy28lp_set_reset(struct miphy28lp_phy *miphy_phy)
{
	void __iomem *base = miphy_phy->base;
	u8 val;

	/* Putting Macro in reset */
	writeb_relaxed(RST_APPLI_SW, base + MIPHY_CONF_RESET);

	val = RST_APPLI_SW | RST_CONF_SW;
	writeb_relaxed(val, base + MIPHY_CONF_RESET);

	writeb_relaxed(RST_APPLI_SW, base + MIPHY_CONF_RESET);

	/* Bringing the MIPHY-CPU registers out of reset */
	if (miphy_phy->type == PHY_TYPE_PCIE) {
		val = AUTO_RST_RX | TERM_EN_SW;
		writeb_relaxed(val, base + MIPHY_CONTROL);
	} else {
		val = AUTO_RST_RX | TERM_EN_SW | DIS_LINK_RST;
		writeb_relaxed(val, base + MIPHY_CONTROL);
	}
}

static inline void miphy28lp_pll_calibration(struct miphy28lp_phy *miphy_phy,
		struct pll_ratio *pll_ratio)
{
	void __iomem *base = miphy_phy->base;
	u8 val;

	/* Applying PLL Settings */
	writeb_relaxed(0x1d, base + MIPHY_PLL_SPAREIN);
	writeb_relaxed(pll_ratio->clk_ref, base + MIPHY_PLL_CLKREF_FREQ);

	/* PLL Ratio */
	writeb_relaxed(pll_ratio->calset_1, base + MIPHY_PLL_CALSET_1);
	writeb_relaxed(pll_ratio->calset_2, base + MIPHY_PLL_CALSET_2);
	writeb_relaxed(pll_ratio->calset_3, base + MIPHY_PLL_CALSET_3);
	writeb_relaxed(pll_ratio->calset_4, base + MIPHY_PLL_CALSET_4);
	writeb_relaxed(pll_ratio->cal_ctrl, base + MIPHY_PLL_CALSET_CTRL);

	writeb_relaxed(TX_SEL, base + MIPHY_BOUNDARY_SEL);

	val = (0x68 << 1) | TX_SLEW_CAL_MAN_EN;
	writeb_relaxed(val, base + MIPHY_TX_CAL_MAN);

	val = VGA_OFFSET_POLARITY | CAL_OFFSET_THRESHOLD_64 | CAL_OFFSET_VGA_64;

	if (miphy_phy->type != PHY_TYPE_SATA)
		val |= OFFSET_COMPENSATION_EN;

	writeb_relaxed(val, base + MIPHY_RX_CAL_OFFSET_CTRL);

	if (miphy_phy->type == PHY_TYPE_USB3) {
		writeb_relaxed(0x00, base + MIPHY_CONF);
		writeb_relaxed(0x70, base + MIPHY_RX_LOCK_STEP);
		writeb_relaxed(EN_FIRST_HALF, base + MIPHY_RX_SIGDET_SLEEP_OA);
		writeb_relaxed(EN_FIRST_HALF, base + MIPHY_RX_SIGDET_SLEEP_SEL);
		writeb_relaxed(EN_FIRST_HALF, base + MIPHY_RX_SIGDET_WAIT_SEL);

		val = EN_DIGIT_SIGNAL_CHECK | EN_FIRST_HALF;
		writeb_relaxed(val, base + MIPHY_RX_SIGDET_DATA_SEL);
	}

}

static inline void miphy28lp_sata_config_gen(struct miphy28lp_phy *miphy_phy)
{
	void __iomem *base = miphy_phy->base;
	int i;

	for (i = 0; i < ARRAY_SIZE(sata_pll_gen); i++) {
		struct miphy28lp_pll_gen *gen = &sata_pll_gen[i];

		/* Banked settings */
		writeb_relaxed(gen->bank, base + MIPHY_CONF);
		writeb_relaxed(gen->speed, base + MIPHY_SPEED);
		writeb_relaxed(gen->bias_boost_1, base + MIPHY_BIAS_BOOST_1);
		writeb_relaxed(gen->bias_boost_2, base + MIPHY_BIAS_BOOST_2);

		/* TX buffer Settings */
		writeb_relaxed(gen->tx_ctrl_2, base + MIPHY_TX_CTRL_2);
		writeb_relaxed(gen->tx_ctrl_3, base + MIPHY_TX_CTRL_3);

		/* RX Buffer Settings */
		writeb_relaxed(gen->rx_buff_ctrl, base + MIPHY_RX_BUFFER_CTRL);
		writeb_relaxed(gen->rx_vga_gain, base + MIPHY_RX_VGA_GAIN);
		writeb_relaxed(gen->rx_equ_gain_1, base + MIPHY_RX_EQU_GAIN_1);
		writeb_relaxed(gen->rx_equ_gain_2, base + MIPHY_RX_EQU_GAIN_2);
		writeb_relaxed(gen->rx_equ_gain_3, base + MIPHY_RX_EQU_GAIN_3);
	}
}

static inline void miphy28lp_pcie_config_gen(struct miphy28lp_phy *miphy_phy)
{
	void __iomem *base = miphy_phy->base;
	int i;

	for (i = 0; i < ARRAY_SIZE(pcie_pll_gen); i++) {
		struct miphy28lp_pll_gen *gen = &pcie_pll_gen[i];

		/* Banked settings */
		writeb_relaxed(gen->bank, base + MIPHY_CONF);
		writeb_relaxed(gen->speed, base + MIPHY_SPEED);
		writeb_relaxed(gen->bias_boost_1, base + MIPHY_BIAS_BOOST_1);
		writeb_relaxed(gen->bias_boost_2, base + MIPHY_BIAS_BOOST_2);

		/* TX buffer Settings */
		writeb_relaxed(gen->tx_ctrl_1, base + MIPHY_TX_CTRL_1);
		writeb_relaxed(gen->tx_ctrl_2, base + MIPHY_TX_CTRL_2);
		writeb_relaxed(gen->tx_ctrl_3, base + MIPHY_TX_CTRL_3);

		writeb_relaxed(gen->rx_k_gain, base + MIPHY_RX_K_GAIN);

		/* RX Buffer Settings */
		writeb_relaxed(gen->rx_buff_ctrl, base + MIPHY_RX_BUFFER_CTRL);
		writeb_relaxed(gen->rx_vga_gain, base + MIPHY_RX_VGA_GAIN);
		writeb_relaxed(gen->rx_equ_gain_1, base + MIPHY_RX_EQU_GAIN_1);
		writeb_relaxed(gen->rx_equ_gain_2, base + MIPHY_RX_EQU_GAIN_2);
	}
}

static inline int miphy28lp_wait_compensation(struct miphy28lp_phy *miphy_phy)
{
	unsigned long finish = jiffies + 5 * HZ;
	u8 val;

	/* Waiting for Compensation to complete */
	do {
		val = readb_relaxed(miphy_phy->base + MIPHY_COMP_FSM_6);

		if (time_after_eq(jiffies, finish))
			return -EBUSY;
		cpu_relax();
	} while (!(val & COMP_DONE));

	return 0;
}


static inline int miphy28lp_compensation(struct miphy28lp_phy *miphy_phy,
		struct pll_ratio *pll_ratio)
{
	void __iomem *base = miphy_phy->base;

	/* Poll for HFC ready after reset release */
	/* Compensation measurement */
	writeb_relaxed(RST_PLL_SW | RST_COMP_SW, base + MIPHY_RESET);

	writeb_relaxed(0x00, base + MIPHY_PLL_COMMON_MISC_2);
	writeb_relaxed(pll_ratio->clk_ref, base + MIPHY_PLL_CLKREF_FREQ);
	writeb_relaxed(COMP_START, base + MIPHY_COMP_FSM_1);

	if (miphy_phy->type == PHY_TYPE_PCIE)
		writeb_relaxed(RST_PLL_SW, base + MIPHY_RESET);

	writeb_relaxed(0x00, base + MIPHY_RESET);
	writeb_relaxed(START_ACT_FILT, base + MIPHY_PLL_COMMON_MISC_2);
	writeb_relaxed(SET_NEW_CHANGE, base + MIPHY_PLL_SBR_1);

	/* TX compensation offset to re-center TX impedance */
	writeb_relaxed(0x00, base + MIPHY_COMP_POSTP);

	if (miphy_phy->type == PHY_TYPE_PCIE)
		return miphy28lp_wait_compensation(miphy_phy);

	return 0;
}

static inline void miphy28_usb3_miphy_reset(struct miphy28lp_phy *miphy_phy)
{
	void __iomem *base = miphy_phy->base;
	u8 val;

	/* MIPHY Reset */
	writeb_relaxed(RST_APPLI_SW, base + MIPHY_CONF_RESET);
	writeb_relaxed(0x00, base + MIPHY_CONF_RESET);
	writeb_relaxed(RST_COMP_SW, base + MIPHY_RESET);

	val = RST_COMP_SW | RST_PLL_SW;
	writeb_relaxed(val, base + MIPHY_RESET);

	writeb_relaxed(0x00, base + MIPHY_PLL_COMMON_MISC_2);
	writeb_relaxed(0x1e, base + MIPHY_PLL_CLKREF_FREQ);
	writeb_relaxed(COMP_START, base + MIPHY_COMP_FSM_1);
	writeb_relaxed(RST_PLL_SW, base + MIPHY_RESET);
	writeb_relaxed(0x00, base + MIPHY_RESET);
	writeb_relaxed(START_ACT_FILT, base + MIPHY_PLL_COMMON_MISC_2);
	writeb_relaxed(0x00, base + MIPHY_CONF);
	writeb_relaxed(0x00, base + MIPHY_BOUNDARY_1);
	writeb_relaxed(0x00, base + MIPHY_TST_BIAS_BOOST_2);
	writeb_relaxed(0x00, base + MIPHY_CONF);
	writeb_relaxed(SET_NEW_CHANGE, base + MIPHY_PLL_SBR_1);
	writeb_relaxed(0xa5, base + MIPHY_DEBUG_BUS);
	writeb_relaxed(0x00, base + MIPHY_CONF);
}

static void miphy_sata_tune_ssc(struct miphy28lp_phy *miphy_phy)
{
	void __iomem *base = miphy_phy->base;
	u8 val;

	/* Compensate Tx impedance to avoid out of range values */
	/*
	 * Enable the SSC on PLL for all banks
	 * SSC Modulation @ 31 KHz and 4000 ppm modulation amp
	 */
	val = readb_relaxed(base + MIPHY_BOUNDARY_2);
	val |= SSC_EN_SW;
	writeb_relaxed(val, base + MIPHY_BOUNDARY_2);

	val = readb_relaxed(base + MIPHY_BOUNDARY_SEL);
	val |= SSC_SEL;
	writeb_relaxed(val, base + MIPHY_BOUNDARY_SEL);

	for (val = 0; val < MIPHY_SATA_BANK_NB; val++) {
		writeb_relaxed(val, base + MIPHY_CONF);

		/* Add value to each reference clock cycle  */
		/* and define the period length of the SSC */
		writeb_relaxed(0x3c, base + MIPHY_PLL_SBR_2);
		writeb_relaxed(0x6c, base + MIPHY_PLL_SBR_3);
		writeb_relaxed(0x81, base + MIPHY_PLL_SBR_4);

		/* Clear any previous request */
		writeb_relaxed(0x00, base + MIPHY_PLL_SBR_1);

		/* requests the PLL to take in account new parameters */
		writeb_relaxed(SET_NEW_CHANGE, base + MIPHY_PLL_SBR_1);

		/* To be sure there is no other pending requests */
		writeb_relaxed(0x00, base + MIPHY_PLL_SBR_1);
	}
}

static void miphy_pcie_tune_ssc(struct miphy28lp_phy *miphy_phy)
{
	void __iomem *base = miphy_phy->base;
	u8 val;

	/* Compensate Tx impedance to avoid out of range values */
	/*
	 * Enable the SSC on PLL for all banks
	 * SSC Modulation @ 31 KHz and 4000 ppm modulation amp
	 */
	val = readb_relaxed(base + MIPHY_BOUNDARY_2);
	val |= SSC_EN_SW;
	writeb_relaxed(val, base + MIPHY_BOUNDARY_2);

	val = readb_relaxed(base + MIPHY_BOUNDARY_SEL);
	val |= SSC_SEL;
	writeb_relaxed(val, base + MIPHY_BOUNDARY_SEL);

	for (val = 0; val < MIPHY_PCIE_BANK_NB; val++) {
		writeb_relaxed(val, base + MIPHY_CONF);

		/* Validate Step component */
		writeb_relaxed(0x69, base + MIPHY_PLL_SBR_3);
		writeb_relaxed(0x21, base + MIPHY_PLL_SBR_4);

		/* Validate Period component */
		writeb_relaxed(0x3c, base + MIPHY_PLL_SBR_2);
		writeb_relaxed(0x21, base + MIPHY_PLL_SBR_4);

		/* Clear any previous request */
		writeb_relaxed(0x00, base + MIPHY_PLL_SBR_1);

		/* requests the PLL to take in account new parameters */
		writeb_relaxed(SET_NEW_CHANGE, base + MIPHY_PLL_SBR_1);

		/* To be sure there is no other pending requests */
		writeb_relaxed(0x00, base + MIPHY_PLL_SBR_1);
	}
}

static inline void miphy_tune_tx_impedance(struct miphy28lp_phy *miphy_phy)
{
	/* Compensate Tx impedance to avoid out of range values */
	writeb_relaxed(0x02, miphy_phy->base + MIPHY_COMP_POSTP);
}

static inline int miphy28lp_configure_sata(struct miphy28lp_phy *miphy_phy)
{
	void __iomem *base = miphy_phy->base;
	int err;
	u8 val;

	/* Putting Macro in reset */
	miphy28lp_set_reset(miphy_phy);

	/* PLL calibration */
	miphy28lp_pll_calibration(miphy_phy, &sata_pll_ratio);

	/* Banked settings Gen1/Gen2/Gen3 */
	miphy28lp_sata_config_gen(miphy_phy);

	/* Power control */
	/* Input bridge enable, manual input bridge control */
	writeb_relaxed(0x21, base + MIPHY_RX_POWER_CTRL_1);

	/* Macro out of reset */
	writeb_relaxed(0x00, base + MIPHY_CONF_RESET);

	/* Poll for HFC ready after reset release */
	/* Compensation measurement */
	err = miphy28lp_compensation(miphy_phy, &sata_pll_ratio);
	if (err)
		return err;

	if (miphy_phy->px_rx_pol_inv) {
		/* Invert Rx polarity */
		val = readb_relaxed(miphy_phy->base + MIPHY_CONTROL);
		val |= PX_RX_POL;
		writeb_relaxed(val, miphy_phy->base + MIPHY_CONTROL);
	}

	if (miphy_phy->ssc)
		miphy_sata_tune_ssc(miphy_phy);

	if (miphy_phy->tx_impedance)
		miphy_tune_tx_impedance(miphy_phy);

	return 0;
}

static inline int miphy28lp_configure_pcie(struct miphy28lp_phy *miphy_phy)
{
	void __iomem *base = miphy_phy->base;
	int err;

	/* Putting Macro in reset */
	miphy28lp_set_reset(miphy_phy);

	/* PLL calibration */
	miphy28lp_pll_calibration(miphy_phy, &pcie_pll_ratio);

	/* Banked settings Gen1/Gen2 */
	miphy28lp_pcie_config_gen(miphy_phy);

	/* Power control */
	/* Input bridge enable, manual input bridge control */
	writeb_relaxed(0x21, base + MIPHY_RX_POWER_CTRL_1);

	/* Macro out of reset */
	writeb_relaxed(0x00, base + MIPHY_CONF_RESET);

	/* Poll for HFC ready after reset release */
	/* Compensation measurement */
	err = miphy28lp_compensation(miphy_phy, &pcie_pll_ratio);
	if (err)
		return err;

	if (miphy_phy->ssc)
		miphy_pcie_tune_ssc(miphy_phy);

	if (miphy_phy->tx_impedance)
		miphy_tune_tx_impedance(miphy_phy);

	return 0;
}


static inline void miphy28lp_configure_usb3(struct miphy28lp_phy *miphy_phy)
{
	void __iomem *base = miphy_phy->base;
	u8 val;

	/* Putting Macro in reset */
	miphy28lp_set_reset(miphy_phy);

	/* PLL calibration */
	miphy28lp_pll_calibration(miphy_phy, &usb3_pll_ratio);

	/* Writing The Speed Rate */
	writeb_relaxed(0x00, base + MIPHY_CONF);

	val = RX_SPDSEL_20DEC | TX_SPDSEL_20DEC;
	writeb_relaxed(val, base + MIPHY_SPEED);

	/* RX Channel compensation and calibration */
	writeb_relaxed(0x1c, base + MIPHY_RX_LOCK_SETTINGS_OPT);
	writeb_relaxed(0x51, base + MIPHY_RX_CAL_CTRL_1);
	writeb_relaxed(0x70, base + MIPHY_RX_CAL_CTRL_2);

	val = OFFSET_COMPENSATION_EN | VGA_OFFSET_POLARITY |
	      CAL_OFFSET_THRESHOLD_64 | CAL_OFFSET_VGA_64;
	writeb_relaxed(val, base + MIPHY_RX_CAL_OFFSET_CTRL);
	writeb_relaxed(0x22, base + MIPHY_RX_CAL_VGA_STEP);
	writeb_relaxed(0x0e, base + MIPHY_RX_CAL_OPT_LENGTH);

	val = EQ_DC_GAIN | VGA_GAIN;
	writeb_relaxed(val, base + MIPHY_RX_BUFFER_CTRL);
	writeb_relaxed(0x78, base + MIPHY_RX_EQU_GAIN_1);
	writeb_relaxed(0x1b, base + MIPHY_SYNCHAR_CONTROL);

	/* TX compensation offset to re-center TX impedance */
	writeb_relaxed(0x02, base + MIPHY_COMP_POSTP);

	/* Enable GENSEL_SEL and SSC */
	/* TX_SEL=0 swing preemp forced by pipe registres */
	val = SSC_SEL | GENSEL_SEL;
	writeb_relaxed(val, base + MIPHY_BOUNDARY_SEL);

	/* MIPHY Bias boost */
	writeb_relaxed(0x00, base + MIPHY_BIAS_BOOST_1);
	writeb_relaxed(0xa7, base + MIPHY_BIAS_BOOST_2);

	/* SSC modulation */
	writeb_relaxed(SSC_EN_SW, base + MIPHY_BOUNDARY_2);

	/* MIPHY TX control */
	writeb_relaxed(0x00, base + MIPHY_CONF);

	/* Validate Step component */
	writeb_relaxed(0x5a, base + MIPHY_PLL_SBR_3);
	writeb_relaxed(0xa0, base + MIPHY_PLL_SBR_4);

	/* Validate Period component */
	writeb_relaxed(0x3c, base + MIPHY_PLL_SBR_2);
	writeb_relaxed(0xa1, base + MIPHY_PLL_SBR_4);

	/* Clear any previous request */
	writeb_relaxed(0x00, base + MIPHY_PLL_SBR_1);

	/* requests the PLL to take in account new parameters */
	writeb_relaxed(0x02, base + MIPHY_PLL_SBR_1);

	/* To be sure there is no other pending requests */
	writeb_relaxed(0x00, base + MIPHY_PLL_SBR_1);

	/* Rx PI controller settings */
	writeb_relaxed(0xca, base + MIPHY_RX_K_GAIN);

	/* MIPHY RX input bridge control */
	/* INPUT_BRIDGE_EN_SW=1, manual input bridge control[0]=1 */
	writeb_relaxed(0x21, base + MIPHY_RX_POWER_CTRL_1);
	writeb_relaxed(0x29, base + MIPHY_RX_POWER_CTRL_1);
	writeb_relaxed(0x1a, base + MIPHY_RX_POWER_CTRL_2);

	/* MIPHY Reset for usb3 */
	miphy28_usb3_miphy_reset(miphy_phy);
}

static inline int miphy_is_ready(struct miphy28lp_phy *miphy_phy)
{
	unsigned long finish = jiffies + 5 * HZ;
	u8 mask = HFC_PLL | HFC_RDY;
	u8 val;

	/*
	 * For PCIe and USB3 check only that PLL and HFC are ready
	 * For SATA check also that phy is ready!
	 */
	if (miphy_phy->type == PHY_TYPE_SATA)
		mask |= PHY_RDY;

	do {
		val = readb_relaxed(miphy_phy->base + MIPHY_STATUS_1);
		if ((val & mask) != mask)
			cpu_relax();
		else
			return 0;
	} while (!time_after_eq(jiffies, finish));

	return -EBUSY;
}

static int miphy_osc_is_ready(struct miphy28lp_phy *miphy_phy)
{
	struct miphy28lp_dev *miphy_dev = miphy_phy->phydev;
	unsigned long finish = jiffies + 5 * HZ;
	u32 val;

	if (!miphy_phy->osc_rdy)
		return 0;

	if (!miphy_phy->syscfg_reg[SYSCFG_STATUS])
		return -EINVAL;

	do {
		regmap_read(miphy_dev->regmap,
				miphy_phy->syscfg_reg[SYSCFG_STATUS], &val);

		if ((val & MIPHY_OSC_RDY) != MIPHY_OSC_RDY)
			cpu_relax();
		else
			return 0;
	} while (!time_after_eq(jiffies, finish));

	return -EBUSY;
}

static int miphy28lp_get_resource_byname(struct device_node *child,
					  char *rname, struct resource *res)
{
	int index;

	index = of_property_match_string(child, "reg-names", rname);
	if (index < 0)
		return -ENODEV;

	return of_address_to_resource(child, index, res);
}

static int miphy28lp_get_one_addr(struct device *dev,
				  struct device_node *child, char *rname,
				  void __iomem **base)
{
	struct resource res;
	int ret;

	ret = miphy28lp_get_resource_byname(child, rname, &res);
	if (!ret) {
		*base = devm_ioremap(dev, res.start, resource_size(&res));
		if (!*base) {
			dev_err(dev, "failed to ioremap %s address region\n"
					, rname);
			return -ENOENT;
		}
	}

	return 0;
}

/* MiPHY reset and sysconf setup */
static int miphy28lp_setup(struct miphy28lp_phy *miphy_phy, u32 miphy_val)
{
	int err;
	struct miphy28lp_dev *miphy_dev = miphy_phy->phydev;

	if (!miphy_phy->syscfg_reg[SYSCFG_CTRL])
		return -EINVAL;

	err = reset_control_assert(miphy_phy->miphy_rst);
	if (err) {
		dev_err(miphy_dev->dev, "unable to bring out of miphy reset\n");
		return err;
	}

	if (miphy_phy->osc_force_ext)
		miphy_val |= MIPHY_OSC_FORCE_EXT;

	regmap_update_bits(miphy_dev->regmap,
			   miphy_phy->syscfg_reg[SYSCFG_CTRL],
			   MIPHY_CTRL_MASK, miphy_val);

	err = reset_control_deassert(miphy_phy->miphy_rst);
	if (err) {
		dev_err(miphy_dev->dev, "unable to bring out of miphy reset\n");
		return err;
	}

	return miphy_osc_is_ready(miphy_phy);
}

static int miphy28lp_init_sata(struct miphy28lp_phy *miphy_phy)
{
	struct miphy28lp_dev *miphy_dev = miphy_phy->phydev;
	int err, sata_conf = SATA_CTRL_SELECT_SATA;

	if ((!miphy_phy->syscfg_reg[SYSCFG_SATA]) ||
			(!miphy_phy->syscfg_reg[SYSCFG_PCI]) ||
			(!miphy_phy->base))
		return -EINVAL;

	dev_info(miphy_dev->dev, "sata-up mode, addr 0x%p\n", miphy_phy->base);

	/* Configure the glue-logic */
	sata_conf |= ((miphy_phy->sata_gen - SATA_GEN1) << SATA_SPDMODE);

	regmap_update_bits(miphy_dev->regmap,
			   miphy_phy->syscfg_reg[SYSCFG_SATA],
			   SATA_CTRL_MASK, sata_conf);

	regmap_update_bits(miphy_dev->regmap, miphy_phy->syscfg_reg[SYSCFG_PCI],
			   PCIE_CTRL_MASK, SATA_CTRL_SELECT_PCIE);

	/* MiPHY path and clocking init */
	err = miphy28lp_setup(miphy_phy, MIPHY_CTRL_DEFAULT);

	if (err) {
		dev_err(miphy_dev->dev, "SATA phy setup failed\n");
		return err;
	}

	/* initialize miphy */
	miphy28lp_configure_sata(miphy_phy);

	return miphy_is_ready(miphy_phy);
}

static int miphy28lp_init_pcie(struct miphy28lp_phy *miphy_phy)
{
	struct miphy28lp_dev *miphy_dev = miphy_phy->phydev;
	int err;

	if ((!miphy_phy->syscfg_reg[SYSCFG_SATA]) ||
			(!miphy_phy->syscfg_reg[SYSCFG_PCI])
		|| (!miphy_phy->base) || (!miphy_phy->pipebase))
		return -EINVAL;

	dev_info(miphy_dev->dev, "pcie-up mode, addr 0x%p\n", miphy_phy->base);

	/* Configure the glue-logic */
	regmap_update_bits(miphy_dev->regmap,
			   miphy_phy->syscfg_reg[SYSCFG_SATA],
			   SATA_CTRL_MASK, SATA_CTRL_SELECT_PCIE);

	regmap_update_bits(miphy_dev->regmap, miphy_phy->syscfg_reg[SYSCFG_PCI],
			   PCIE_CTRL_MASK, SYSCFG_PCIE_PCIE_VAL);

	/* MiPHY path and clocking init */
	err = miphy28lp_setup(miphy_phy, MIPHY_CTRL_DEFAULT);

	if (err) {
		dev_err(miphy_dev->dev, "PCIe phy setup failed\n");
		return err;
	}

	/* initialize miphy */
	err = miphy28lp_configure_pcie(miphy_phy);
	if (err)
		return err;

	/* PIPE Wrapper Configuration */
	writeb_relaxed(0x68, miphy_phy->pipebase + 0x104); /* Rise_0 */
	writeb_relaxed(0x61, miphy_phy->pipebase + 0x105); /* Rise_1 */
	writeb_relaxed(0x68, miphy_phy->pipebase + 0x108); /* Fall_0 */
	writeb_relaxed(0x61, miphy_phy->pipebase + 0x109); /* Fall-1 */
	writeb_relaxed(0x68, miphy_phy->pipebase + 0x10c); /* Threshold_0 */
	writeb_relaxed(0x60, miphy_phy->pipebase + 0x10d); /* Threshold_1 */

	/* Wait for phy_ready */
	return miphy_is_ready(miphy_phy);
}

static int miphy28lp_init_usb3(struct miphy28lp_phy *miphy_phy)
{
	struct miphy28lp_dev *miphy_dev = miphy_phy->phydev;
	int err;

	if ((!miphy_phy->base) || (!miphy_phy->pipebase))
		return -EINVAL;

	dev_info(miphy_dev->dev, "usb3-up mode, addr 0x%p\n", miphy_phy->base);

	/* MiPHY path and clocking init */
	err = miphy28lp_setup(miphy_phy, MIPHY_CTRL_SYNC_D_EN);
	if (err) {
		dev_err(miphy_dev->dev, "USB3 phy setup failed\n");
		return err;
	}

	/* initialize miphy */
	miphy28lp_configure_usb3(miphy_phy);

	/* PIPE Wrapper Configuration */
	writeb_relaxed(0x68, miphy_phy->pipebase + 0x23);
	writeb_relaxed(0x61, miphy_phy->pipebase + 0x24);
	writeb_relaxed(0x68, miphy_phy->pipebase + 0x26);
	writeb_relaxed(0x61, miphy_phy->pipebase + 0x27);
	writeb_relaxed(0x18, miphy_phy->pipebase + 0x29);
	writeb_relaxed(0x61, miphy_phy->pipebase + 0x2a);

	/* pipe Wrapper usb3 TX swing de-emph margin PREEMPH[7:4], SWING[3:0] */
	writeb_relaxed(0X67, miphy_phy->pipebase + 0x68);
	writeb_relaxed(0x0d, miphy_phy->pipebase + 0x69);
	writeb_relaxed(0X67, miphy_phy->pipebase + 0x6a);
	writeb_relaxed(0X0d, miphy_phy->pipebase + 0x6b);
	writeb_relaxed(0X67, miphy_phy->pipebase + 0x6c);
	writeb_relaxed(0X0d, miphy_phy->pipebase + 0x6d);
	writeb_relaxed(0X67, miphy_phy->pipebase + 0x6e);
	writeb_relaxed(0X0d, miphy_phy->pipebase + 0x6f);

	return miphy_is_ready(miphy_phy);
}

static int miphy28lp_init(struct phy *phy)
{
	struct miphy28lp_phy *miphy_phy = phy_get_drvdata(phy);
	struct miphy28lp_dev *miphy_dev = miphy_phy->phydev;
	int ret;

	mutex_lock(&miphy_dev->miphy_mutex);

	switch (miphy_phy->type) {

	case PHY_TYPE_SATA:
		ret = miphy28lp_init_sata(miphy_phy);
		break;
	case PHY_TYPE_PCIE:
		ret = miphy28lp_init_pcie(miphy_phy);
		break;
	case PHY_TYPE_USB3:
		ret = miphy28lp_init_usb3(miphy_phy);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&miphy_dev->miphy_mutex);

	return ret;
}

static int miphy28lp_get_addr(struct miphy28lp_phy *miphy_phy)
{
	struct miphy28lp_dev *miphy_dev = miphy_phy->phydev;
	struct device_node *phynode = miphy_phy->phy->dev.of_node;
	int err;

	if ((miphy_phy->type != PHY_TYPE_SATA) &&
	    (miphy_phy->type != PHY_TYPE_PCIE) &&
	    (miphy_phy->type != PHY_TYPE_USB3)) {
		return -EINVAL;
	}

	err = miphy28lp_get_one_addr(miphy_dev->dev, phynode,
			PHY_TYPE_name[miphy_phy->type - PHY_TYPE_SATA],
			&miphy_phy->base);
	if (err)
		return err;

	if ((miphy_phy->type == PHY_TYPE_PCIE) ||
	    (miphy_phy->type == PHY_TYPE_USB3)) {
		err = miphy28lp_get_one_addr(miphy_dev->dev, phynode, "pipew",
					     &miphy_phy->pipebase);
		if (err)
			return err;
	}

	return 0;
}

static struct phy *miphy28lp_xlate(struct device *dev,
				   struct of_phandle_args *args)
{
	struct miphy28lp_dev *miphy_dev = dev_get_drvdata(dev);
	struct miphy28lp_phy *miphy_phy = NULL;
	struct device_node *phynode = args->np;
	int ret, index = 0;

	if (args->args_count != 1) {
		dev_err(dev, "Invalid number of cells in 'phy' property\n");
		return ERR_PTR(-EINVAL);
	}

	for (index = 0; index < miphy_dev->nphys; index++)
		if (phynode == miphy_dev->phys[index]->phy->dev.of_node) {
			miphy_phy = miphy_dev->phys[index];
			break;
		}

	if (!miphy_phy) {
		dev_err(dev, "Failed to find appropriate phy\n");
		return ERR_PTR(-EINVAL);
	}

	miphy_phy->type = args->args[0];

	ret = miphy28lp_get_addr(miphy_phy);
	if (ret < 0)
		return ERR_PTR(ret);

	return miphy_phy->phy;
}

static const struct phy_ops miphy28lp_ops = {
	.init = miphy28lp_init,
	.owner = THIS_MODULE,
};

static int miphy28lp_probe_resets(struct device_node *node,
				  struct miphy28lp_phy *miphy_phy)
{
	struct miphy28lp_dev *miphy_dev = miphy_phy->phydev;
	int err;

	miphy_phy->miphy_rst = of_reset_control_get(node, "miphy-sw-rst");

	if (IS_ERR(miphy_phy->miphy_rst)) {
		dev_err(miphy_dev->dev,
				"miphy soft reset control not defined\n");
		return PTR_ERR(miphy_phy->miphy_rst);
	}

	err = reset_control_deassert(miphy_phy->miphy_rst);
	if (err) {
		dev_err(miphy_dev->dev, "unable to bring out of miphy reset\n");
		return err;
	}

	return 0;
}

static int miphy28lp_of_probe(struct device_node *np,
			      struct miphy28lp_phy *miphy_phy)
{
	int i;
	u32 ctrlreg;

	miphy_phy->osc_force_ext =
		of_property_read_bool(np, "st,osc-force-ext");

	miphy_phy->osc_rdy = of_property_read_bool(np, "st,osc-rdy");

	miphy_phy->px_rx_pol_inv =
		of_property_read_bool(np, "st,px_rx_pol_inv");

	miphy_phy->ssc = of_property_read_bool(np, "st,ssc-on");

	miphy_phy->tx_impedance =
		of_property_read_bool(np, "st,tx-impedance-comp");

	of_property_read_u32(np, "st,sata-gen", &miphy_phy->sata_gen);
	if (!miphy_phy->sata_gen)
		miphy_phy->sata_gen = SATA_GEN1;

	for (i = 0; i < SYSCFG_REG_MAX; i++) {
		if (!of_property_read_u32_index(np, "st,syscfg", i, &ctrlreg))
			miphy_phy->syscfg_reg[i] = ctrlreg;
	}

	return 0;
}

static int miphy28lp_probe(struct platform_device *pdev)
{
	struct device_node *child, *np = pdev->dev.of_node;
	struct miphy28lp_dev *miphy_dev;
	struct phy_provider *provider;
	struct phy *phy;
	int ret, port = 0;

	miphy_dev = devm_kzalloc(&pdev->dev, sizeof(*miphy_dev), GFP_KERNEL);
	if (!miphy_dev)
		return -ENOMEM;

	miphy_dev->nphys = of_get_child_count(np);
	miphy_dev->phys = devm_kcalloc(&pdev->dev, miphy_dev->nphys,
				       sizeof(*miphy_dev->phys), GFP_KERNEL);
	if (!miphy_dev->phys)
		return -ENOMEM;

	miphy_dev->regmap = syscon_regmap_lookup_by_phandle(np, "st,syscfg");
	if (IS_ERR(miphy_dev->regmap)) {
		dev_err(miphy_dev->dev, "No syscfg phandle specified\n");
		return PTR_ERR(miphy_dev->regmap);
	}

	miphy_dev->dev = &pdev->dev;

	dev_set_drvdata(&pdev->dev, miphy_dev);

	mutex_init(&miphy_dev->miphy_mutex);

	for_each_child_of_node(np, child) {
		struct miphy28lp_phy *miphy_phy;

		miphy_phy = devm_kzalloc(&pdev->dev, sizeof(*miphy_phy),
					 GFP_KERNEL);
		if (!miphy_phy) {
			ret = -ENOMEM;
			goto put_child;
		}

		miphy_dev->phys[port] = miphy_phy;

		phy = devm_phy_create(&pdev->dev, child, &miphy28lp_ops);
		if (IS_ERR(phy)) {
			dev_err(&pdev->dev, "failed to create PHY\n");
			ret = PTR_ERR(phy);
			goto put_child;
		}

		miphy_dev->phys[port]->phy = phy;
		miphy_dev->phys[port]->phydev = miphy_dev;

		ret = miphy28lp_of_probe(child, miphy_phy);
		if (ret)
			goto put_child;

		ret = miphy28lp_probe_resets(child, miphy_dev->phys[port]);
		if (ret)
			goto put_child;

		phy_set_drvdata(phy, miphy_dev->phys[port]);
		port++;

	}

	provider = devm_of_phy_provider_register(&pdev->dev, miphy28lp_xlate);
	return PTR_ERR_OR_ZERO(provider);
put_child:
	of_node_put(child);
	return ret;
}

static const struct of_device_id miphy28lp_of_match[] = {
	{.compatible = "st,miphy28lp-phy", },
	{},
};

MODULE_DEVICE_TABLE(of, miphy28lp_of_match);

static struct platform_driver miphy28lp_driver = {
	.probe = miphy28lp_probe,
	.driver = {
		.name = "miphy28lp-phy",
		.of_match_table = miphy28lp_of_match,
	}
};

module_platform_driver(miphy28lp_driver);

MODULE_AUTHOR("Alexandre Torgue <alexandre.torgue@st.com>");
MODULE_DESCRIPTION("STMicroelectronics miphy28lp driver");
MODULE_LICENSE("GPL v2");
