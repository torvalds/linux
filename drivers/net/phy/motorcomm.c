// SPDX-License-Identifier: GPL-2.0+
/*
 * Motorcomm 8511/8521/8531/8531S/8821 PHY driver.
 *
 * Author: Peter Geis <pgwipeout@gmail.com>
 * Author: Frank <Frank.Sae@motor-comm.com>
 */

#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/of.h>

#define PHY_ID_YT8511		0x0000010a
#define PHY_ID_YT8521		0x0000011a
#define PHY_ID_YT8531		0x4f51e91b
#define PHY_ID_YT8531S		0x4f51e91a
#define PHY_ID_YT8821		0x4f51ea19
/* YT8521/YT8531S/YT8821 Register Overview
 *	UTP Register space	|	FIBER Register space
 *  ------------------------------------------------------------
 * |	UTP MII			|	FIBER MII		|
 * |	UTP MMD			|				|
 * |	UTP Extended		|	FIBER Extended		|
 *  ------------------------------------------------------------
 * |			Common Extended				|
 *  ------------------------------------------------------------
 */

/* 0x10 ~ 0x15 , 0x1E and 0x1F are common MII registers of yt phy */

/* Specific Function Control Register */
#define YTPHY_SPECIFIC_FUNCTION_CONTROL_REG	0x10

/* 2b00 Manual MDI configuration
 * 2b01 Manual MDIX configuration
 * 2b10 Reserved
 * 2b11 Enable automatic crossover for all modes  *default*
 */
#define YTPHY_SFCR_MDI_CROSSOVER_MODE_MASK	(BIT(6) | BIT(5))
#define YTPHY_SFCR_CROSSOVER_EN			BIT(3)
#define YTPHY_SFCR_SQE_TEST_EN			BIT(2)
#define YTPHY_SFCR_POLARITY_REVERSAL_EN		BIT(1)
#define YTPHY_SFCR_JABBER_DIS			BIT(0)

/* Specific Status Register */
#define YTPHY_SPECIFIC_STATUS_REG		0x11
#define YTPHY_SSR_SPEED_MASK			((0x3 << 14) | BIT(9))
#define YTPHY_SSR_SPEED_10M			((0x0 << 14))
#define YTPHY_SSR_SPEED_100M			((0x1 << 14))
#define YTPHY_SSR_SPEED_1000M			((0x2 << 14))
#define YTPHY_SSR_SPEED_10G			((0x3 << 14))
#define YTPHY_SSR_SPEED_2500M			((0x0 << 14) | BIT(9))
#define YTPHY_SSR_DUPLEX_OFFSET			13
#define YTPHY_SSR_DUPLEX			BIT(13)
#define YTPHY_SSR_PAGE_RECEIVED			BIT(12)
#define YTPHY_SSR_SPEED_DUPLEX_RESOLVED		BIT(11)
#define YTPHY_SSR_LINK				BIT(10)
#define YTPHY_SSR_MDIX_CROSSOVER		BIT(6)
#define YTPHY_SSR_DOWNGRADE			BIT(5)
#define YTPHY_SSR_TRANSMIT_PAUSE		BIT(3)
#define YTPHY_SSR_RECEIVE_PAUSE			BIT(2)
#define YTPHY_SSR_POLARITY			BIT(1)
#define YTPHY_SSR_JABBER			BIT(0)

/* Interrupt enable Register */
#define YTPHY_INTERRUPT_ENABLE_REG		0x12
#define YTPHY_IER_WOL				BIT(6)

/* Interrupt Status Register */
#define YTPHY_INTERRUPT_STATUS_REG		0x13
#define YTPHY_ISR_AUTONEG_ERR			BIT(15)
#define YTPHY_ISR_SPEED_CHANGED			BIT(14)
#define YTPHY_ISR_DUPLEX_CHANGED		BIT(13)
#define YTPHY_ISR_PAGE_RECEIVED			BIT(12)
#define YTPHY_ISR_LINK_FAILED			BIT(11)
#define YTPHY_ISR_LINK_SUCCESSED		BIT(10)
#define YTPHY_ISR_WOL				BIT(6)
#define YTPHY_ISR_WIRESPEED_DOWNGRADE		BIT(5)
#define YTPHY_ISR_SERDES_LINK_FAILED		BIT(3)
#define YTPHY_ISR_SERDES_LINK_SUCCESSED		BIT(2)
#define YTPHY_ISR_POLARITY_CHANGED		BIT(1)
#define YTPHY_ISR_JABBER_HAPPENED		BIT(0)

/* Speed Auto Downgrade Control Register */
#define YTPHY_SPEED_AUTO_DOWNGRADE_CONTROL_REG	0x14
#define YTPHY_SADCR_SPEED_DOWNGRADE_EN		BIT(5)

/* If these bits are set to 3, the PHY attempts five times ( 3(set value) +
 * additional 2) before downgrading, default 0x3
 */
#define YTPHY_SADCR_SPEED_RETRY_LIMIT		(0x3 << 2)

/* Rx Error Counter Register */
#define YTPHY_RX_ERROR_COUNTER_REG		0x15

/* Extended Register's Address Offset Register */
#define YTPHY_PAGE_SELECT			0x1E

/* Extended Register's Data Register */
#define YTPHY_PAGE_DATA				0x1F

/* FIBER Auto-Negotiation link partner ability */
#define YTPHY_FLPA_PAUSE			(0x3 << 7)
#define YTPHY_FLPA_ASYM_PAUSE			(0x2 << 7)

#define YT8511_PAGE_SELECT	0x1e
#define YT8511_PAGE		0x1f
#define YT8511_EXT_CLK_GATE	0x0c
#define YT8511_EXT_DELAY_DRIVE	0x0d
#define YT8511_EXT_SLEEP_CTRL	0x27

/* 2b00 25m from pll
 * 2b01 25m from xtl *default*
 * 2b10 62.m from pll
 * 2b11 125m from pll
 */
#define YT8511_CLK_125M		(BIT(2) | BIT(1))
#define YT8511_PLLON_SLP	BIT(14)

/* RX Delay enabled = 1.8ns 1000T, 8ns 10/100T */
#define YT8511_DELAY_RX		BIT(0)

/* TX Gig-E Delay is bits 7:4, default 0x5
 * TX Fast-E Delay is bits 15:12, default 0xf
 * Delay = 150ps * N - 250ps
 * On = 2000ps, off = 50ps
 */
#define YT8511_DELAY_GE_TX_EN	(0xf << 4)
#define YT8511_DELAY_GE_TX_DIS	(0x2 << 4)
#define YT8511_DELAY_FE_TX_EN	(0xf << 12)
#define YT8511_DELAY_FE_TX_DIS	(0x2 << 12)

/* Extended register is different from MMD Register and MII Register.
 * We can use ytphy_read_ext/ytphy_write_ext/ytphy_modify_ext function to
 * operate extended register.
 * Extended Register  start
 */

/* Phy gmii clock gating Register */
#define YT8521_CLOCK_GATING_REG			0xC
#define YT8521_CGR_RX_CLK_EN			BIT(12)

#define YT8521_EXTREG_SLEEP_CONTROL1_REG	0x27
#define YT8521_ESC1R_SLEEP_SW			BIT(15)
#define YT8521_ESC1R_PLLON_SLP			BIT(14)

/* Phy fiber Link timer cfg2 Register */
#define YT8521_LINK_TIMER_CFG2_REG		0xA5
#define YT8521_LTCR_EN_AUTOSEN			BIT(15)

/* 0xA000, 0xA001, 0xA003, 0xA006 ~ 0xA00A and 0xA012 are common ext registers
 * of yt8521 phy. There is no need to switch reg space when operating these
 * registers.
 */

#define YT8521_REG_SPACE_SELECT_REG		0xA000
#define YT8521_RSSR_SPACE_MASK			BIT(1)
#define YT8521_RSSR_FIBER_SPACE			(0x1 << 1)
#define YT8521_RSSR_UTP_SPACE			(0x0 << 1)
#define YT8521_RSSR_TO_BE_ARBITRATED		(0xFF)

#define YT8521_CHIP_CONFIG_REG			0xA001
#define YT8521_CCR_SW_RST			BIT(15)
#define YT8531_RGMII_LDO_VOL_MASK		GENMASK(5, 4)
#define YT8531_LDO_VOL_3V3			0x0
#define YT8531_LDO_VOL_1V8			0x2

/* 1b0 disable 1.9ns rxc clock delay  *default*
 * 1b1 enable 1.9ns rxc clock delay
 */
#define YT8521_CCR_RXC_DLY_EN			BIT(8)
#define YT8521_CCR_RXC_DLY_1_900_NS		1900

#define YT8521_CCR_MODE_SEL_MASK		(BIT(2) | BIT(1) | BIT(0))
#define YT8521_CCR_MODE_UTP_TO_RGMII		0
#define YT8521_CCR_MODE_FIBER_TO_RGMII		1
#define YT8521_CCR_MODE_UTP_FIBER_TO_RGMII	2
#define YT8521_CCR_MODE_UTP_TO_SGMII		3
#define YT8521_CCR_MODE_SGPHY_TO_RGMAC		4
#define YT8521_CCR_MODE_SGMAC_TO_RGPHY		5
#define YT8521_CCR_MODE_UTP_TO_FIBER_AUTO	6
#define YT8521_CCR_MODE_UTP_TO_FIBER_FORCE	7

/* 3 phy polling modes,poll mode combines utp and fiber mode*/
#define YT8521_MODE_FIBER			0x1
#define YT8521_MODE_UTP				0x2
#define YT8521_MODE_POLL			0x3

#define YT8521_RGMII_CONFIG1_REG		0xA003
/* 1b0 use original tx_clk_rgmii  *default*
 * 1b1 use inverted tx_clk_rgmii.
 */
#define YT8521_RC1R_TX_CLK_SEL_INVERTED		BIT(14)
#define YT8521_RC1R_RX_DELAY_MASK		GENMASK(13, 10)
#define YT8521_RC1R_FE_TX_DELAY_MASK		GENMASK(7, 4)
#define YT8521_RC1R_GE_TX_DELAY_MASK		GENMASK(3, 0)
#define YT8521_RC1R_RGMII_0_000_NS		0
#define YT8521_RC1R_RGMII_0_150_NS		1
#define YT8521_RC1R_RGMII_0_300_NS		2
#define YT8521_RC1R_RGMII_0_450_NS		3
#define YT8521_RC1R_RGMII_0_600_NS		4
#define YT8521_RC1R_RGMII_0_750_NS		5
#define YT8521_RC1R_RGMII_0_900_NS		6
#define YT8521_RC1R_RGMII_1_050_NS		7
#define YT8521_RC1R_RGMII_1_200_NS		8
#define YT8521_RC1R_RGMII_1_350_NS		9
#define YT8521_RC1R_RGMII_1_500_NS		10
#define YT8521_RC1R_RGMII_1_650_NS		11
#define YT8521_RC1R_RGMII_1_800_NS		12
#define YT8521_RC1R_RGMII_1_950_NS		13
#define YT8521_RC1R_RGMII_2_100_NS		14
#define YT8521_RC1R_RGMII_2_250_NS		15

/* LED CONFIG */
#define YT8521_MAX_LEDS				3
#define YT8521_LED0_CFG_REG			0xA00C
#define YT8521_LED1_CFG_REG			0xA00D
#define YT8521_LED2_CFG_REG			0xA00E
#define YT8521_LED_ACT_BLK_IND			BIT(13)
#define YT8521_LED_FDX_ON_EN			BIT(12)
#define YT8521_LED_HDX_ON_EN			BIT(11)
#define YT8521_LED_TXACT_BLK_EN			BIT(10)
#define YT8521_LED_RXACT_BLK_EN			BIT(9)
#define YT8521_LED_1000_ON_EN			BIT(6)
#define YT8521_LED_100_ON_EN			BIT(5)
#define YT8521_LED_10_ON_EN			BIT(4)

#define YTPHY_MISC_CONFIG_REG			0xA006
#define YTPHY_MCR_FIBER_SPEED_MASK		BIT(0)
#define YTPHY_MCR_FIBER_1000BX			(0x1 << 0)
#define YTPHY_MCR_FIBER_100FX			(0x0 << 0)

/* WOL MAC ADDR: MACADDR2(highest), MACADDR1(middle), MACADDR0(lowest) */
#define YTPHY_WOL_MACADDR2_REG			0xA007
#define YTPHY_WOL_MACADDR1_REG			0xA008
#define YTPHY_WOL_MACADDR0_REG			0xA009

#define YTPHY_WOL_CONFIG_REG			0xA00A
#define YTPHY_WCR_INTR_SEL			BIT(6)
#define YTPHY_WCR_ENABLE			BIT(3)

/* 2b00 84ms
 * 2b01 168ms  *default*
 * 2b10 336ms
 * 2b11 672ms
 */
#define YTPHY_WCR_PULSE_WIDTH_MASK		(BIT(2) | BIT(1))
#define YTPHY_WCR_PULSE_WIDTH_672MS		(BIT(2) | BIT(1))

/* 1b0 Interrupt and WOL events is level triggered and active LOW  *default*
 * 1b1 Interrupt and WOL events is pulse triggered and active LOW
 */
#define YTPHY_WCR_TYPE_PULSE			BIT(0)

#define YTPHY_PAD_DRIVE_STRENGTH_REG		0xA010
#define YT8531_RGMII_RXC_DS_MASK		GENMASK(15, 13)
#define YT8531_RGMII_RXD_DS_HI_MASK		BIT(12)		/* Bit 2 of rxd_ds */
#define YT8531_RGMII_RXD_DS_LOW_MASK		GENMASK(5, 4)	/* Bit 1/0 of rxd_ds */
#define YT8531_RGMII_RX_DS_DEFAULT		0x3

#define YTPHY_SYNCE_CFG_REG			0xA012
#define YT8521_SCR_SYNCE_ENABLE			BIT(5)
/* 1b0 output 25m clock
 * 1b1 output 125m clock  *default*
 */
#define YT8521_SCR_CLK_FRE_SEL_125M		BIT(3)
#define YT8521_SCR_CLK_SRC_MASK			GENMASK(2, 1)
#define YT8521_SCR_CLK_SRC_PLL_125M		0
#define YT8521_SCR_CLK_SRC_UTP_RX		1
#define YT8521_SCR_CLK_SRC_SDS_RX		2
#define YT8521_SCR_CLK_SRC_REF_25M		3
#define YT8531_SCR_SYNCE_ENABLE			BIT(6)
/* 1b0 output 25m clock   *default*
 * 1b1 output 125m clock
 */
#define YT8531_SCR_CLK_FRE_SEL_125M		BIT(4)
#define YT8531_SCR_CLK_SRC_MASK			GENMASK(3, 1)
#define YT8531_SCR_CLK_SRC_PLL_125M		0
#define YT8531_SCR_CLK_SRC_UTP_RX		1
#define YT8531_SCR_CLK_SRC_SDS_RX		2
#define YT8531_SCR_CLK_SRC_CLOCK_FROM_DIGITAL	3
#define YT8531_SCR_CLK_SRC_REF_25M		4
#define YT8531_SCR_CLK_SRC_SSC_25M		5

#define YT8821_SDS_EXT_CSR_CTRL_REG			0x23
#define YT8821_SDS_EXT_CSR_VCO_LDO_EN			BIT(15)
#define YT8821_SDS_EXT_CSR_VCO_BIAS_LPF_EN		BIT(8)

#define YT8821_UTP_EXT_PI_CTRL_REG			0x56
#define YT8821_UTP_EXT_PI_RST_N_FIFO			BIT(5)
#define YT8821_UTP_EXT_PI_TX_CLK_SEL_AFE		BIT(4)
#define YT8821_UTP_EXT_PI_RX_CLK_3_SEL_AFE		BIT(3)
#define YT8821_UTP_EXT_PI_RX_CLK_2_SEL_AFE		BIT(2)
#define YT8821_UTP_EXT_PI_RX_CLK_1_SEL_AFE		BIT(1)
#define YT8821_UTP_EXT_PI_RX_CLK_0_SEL_AFE		BIT(0)

#define YT8821_UTP_EXT_VCT_CFG6_CTRL_REG		0x97
#define YT8821_UTP_EXT_FECHO_AMP_TH_HUGE		GENMASK(15, 8)

#define YT8821_UTP_EXT_ECHO_CTRL_REG			0x336
#define YT8821_UTP_EXT_TRACE_LNG_GAIN_THR_1000		GENMASK(14, 8)

#define YT8821_UTP_EXT_GAIN_CTRL_REG			0x340
#define YT8821_UTP_EXT_TRACE_MED_GAIN_THR_1000		GENMASK(6, 0)

#define YT8821_UTP_EXT_RPDN_CTRL_REG			0x34E
#define YT8821_UTP_EXT_RPDN_BP_FFE_LNG_2500		BIT(15)
#define YT8821_UTP_EXT_RPDN_BP_FFE_SHT_2500		BIT(7)
#define YT8821_UTP_EXT_RPDN_IPR_SHT_2500		GENMASK(6, 0)

#define YT8821_UTP_EXT_TH_20DB_2500_CTRL_REG		0x36A
#define YT8821_UTP_EXT_TH_20DB_2500			GENMASK(15, 0)

#define YT8821_UTP_EXT_TRACE_CTRL_REG			0x372
#define YT8821_UTP_EXT_TRACE_LNG_GAIN_THE_2500		GENMASK(14, 8)
#define YT8821_UTP_EXT_TRACE_MED_GAIN_THE_2500		GENMASK(6, 0)

#define YT8821_UTP_EXT_ALPHA_IPR_CTRL_REG		0x374
#define YT8821_UTP_EXT_ALPHA_SHT_2500			GENMASK(14, 8)
#define YT8821_UTP_EXT_IPR_LNG_2500			GENMASK(6, 0)

#define YT8821_UTP_EXT_PLL_CTRL_REG			0x450
#define YT8821_UTP_EXT_PLL_SPARE_CFG			GENMASK(7, 0)

#define YT8821_UTP_EXT_DAC_IMID_CH_2_3_CTRL_REG		0x466
#define YT8821_UTP_EXT_DAC_IMID_CH_3_10_ORG		GENMASK(14, 8)
#define YT8821_UTP_EXT_DAC_IMID_CH_2_10_ORG		GENMASK(6, 0)

#define YT8821_UTP_EXT_DAC_IMID_CH_0_1_CTRL_REG		0x467
#define YT8821_UTP_EXT_DAC_IMID_CH_1_10_ORG		GENMASK(14, 8)
#define YT8821_UTP_EXT_DAC_IMID_CH_0_10_ORG		GENMASK(6, 0)

#define YT8821_UTP_EXT_DAC_IMSB_CH_2_3_CTRL_REG		0x468
#define YT8821_UTP_EXT_DAC_IMSB_CH_3_10_ORG		GENMASK(14, 8)
#define YT8821_UTP_EXT_DAC_IMSB_CH_2_10_ORG		GENMASK(6, 0)

#define YT8821_UTP_EXT_DAC_IMSB_CH_0_1_CTRL_REG		0x469
#define YT8821_UTP_EXT_DAC_IMSB_CH_1_10_ORG		GENMASK(14, 8)
#define YT8821_UTP_EXT_DAC_IMSB_CH_0_10_ORG		GENMASK(6, 0)

#define YT8821_UTP_EXT_MU_COARSE_FR_CTRL_REG		0x4B3
#define YT8821_UTP_EXT_MU_COARSE_FR_F_FFE		GENMASK(14, 12)
#define YT8821_UTP_EXT_MU_COARSE_FR_F_FBE		GENMASK(10, 8)

#define YT8821_UTP_EXT_MU_FINE_FR_CTRL_REG		0x4B5
#define YT8821_UTP_EXT_MU_FINE_FR_F_FFE			GENMASK(14, 12)
#define YT8821_UTP_EXT_MU_FINE_FR_F_FBE			GENMASK(10, 8)

#define YT8821_UTP_EXT_VGA_LPF1_CAP_CTRL_REG		0x4D2
#define YT8821_UTP_EXT_VGA_LPF1_CAP_OTHER		GENMASK(7, 4)
#define YT8821_UTP_EXT_VGA_LPF1_CAP_2500		GENMASK(3, 0)

#define YT8821_UTP_EXT_VGA_LPF2_CAP_CTRL_REG		0x4D3
#define YT8821_UTP_EXT_VGA_LPF2_CAP_OTHER		GENMASK(7, 4)
#define YT8821_UTP_EXT_VGA_LPF2_CAP_2500		GENMASK(3, 0)

#define YT8821_UTP_EXT_TXGE_NFR_FR_THP_CTRL_REG		0x660
#define YT8821_UTP_EXT_NFR_TX_ABILITY			BIT(3)
/* Extended Register  end */

#define YTPHY_DTS_OUTPUT_CLK_DIS		0
#define YTPHY_DTS_OUTPUT_CLK_25M		25000000
#define YTPHY_DTS_OUTPUT_CLK_125M		125000000

#define YT8821_CHIP_MODE_AUTO_BX2500_SGMII	0
#define YT8821_CHIP_MODE_FORCE_BX2500		1

struct yt8521_priv {
	/* combo_advertising is used for case of YT8521 in combo mode,
	 * this means that yt8521 may work in utp or fiber mode which depends
	 * on which media is connected (YT8521_RSSR_TO_BE_ARBITRATED).
	 */
	__ETHTOOL_DECLARE_LINK_MODE_MASK(combo_advertising);

	/* YT8521_MODE_FIBER / YT8521_MODE_UTP / YT8521_MODE_POLL*/
	u8 polling_mode;
	u8 strap_mode; /* 8 working modes  */
	/* current reg page of yt8521 phy:
	 * YT8521_RSSR_UTP_SPACE
	 * YT8521_RSSR_FIBER_SPACE
	 * YT8521_RSSR_TO_BE_ARBITRATED
	 */
	u8 reg_page;
};

/**
 * ytphy_read_ext() - read a PHY's extended register
 * @phydev: a pointer to a &struct phy_device
 * @regnum: register number to read
 *
 * NOTE:The caller must have taken the MDIO bus lock.
 *
 * returns the value of regnum reg or negative error code
 */
static int ytphy_read_ext(struct phy_device *phydev, u16 regnum)
{
	int ret;

	ret = __phy_write(phydev, YTPHY_PAGE_SELECT, regnum);
	if (ret < 0)
		return ret;

	return __phy_read(phydev, YTPHY_PAGE_DATA);
}

/**
 * ytphy_read_ext_with_lock() - read a PHY's extended register
 * @phydev: a pointer to a &struct phy_device
 * @regnum: register number to read
 *
 * returns the value of regnum reg or negative error code
 */
static int ytphy_read_ext_with_lock(struct phy_device *phydev, u16 regnum)
{
	int ret;

	phy_lock_mdio_bus(phydev);
	ret = ytphy_read_ext(phydev, regnum);
	phy_unlock_mdio_bus(phydev);

	return ret;
}

/**
 * ytphy_write_ext() - write a PHY's extended register
 * @phydev: a pointer to a &struct phy_device
 * @regnum: register number to write
 * @val: value to write to @regnum
 *
 * NOTE:The caller must have taken the MDIO bus lock.
 *
 * returns 0 or negative error code
 */
static int ytphy_write_ext(struct phy_device *phydev, u16 regnum, u16 val)
{
	int ret;

	ret = __phy_write(phydev, YTPHY_PAGE_SELECT, regnum);
	if (ret < 0)
		return ret;

	return __phy_write(phydev, YTPHY_PAGE_DATA, val);
}

/**
 * ytphy_write_ext_with_lock() - write a PHY's extended register
 * @phydev: a pointer to a &struct phy_device
 * @regnum: register number to write
 * @val: value to write to @regnum
 *
 * returns 0 or negative error code
 */
static int ytphy_write_ext_with_lock(struct phy_device *phydev, u16 regnum,
				     u16 val)
{
	int ret;

	phy_lock_mdio_bus(phydev);
	ret = ytphy_write_ext(phydev, regnum, val);
	phy_unlock_mdio_bus(phydev);

	return ret;
}

/**
 * ytphy_modify_ext() - bits modify a PHY's extended register
 * @phydev: a pointer to a &struct phy_device
 * @regnum: register number to write
 * @mask: bit mask of bits to clear
 * @set: bit mask of bits to set
 *
 * NOTE: Convenience function which allows a PHY's extended register to be
 * modified as new register value = (old register value & ~mask) | set.
 * The caller must have taken the MDIO bus lock.
 *
 * returns 0 or negative error code
 */
static int ytphy_modify_ext(struct phy_device *phydev, u16 regnum, u16 mask,
			    u16 set)
{
	int ret;

	ret = __phy_write(phydev, YTPHY_PAGE_SELECT, regnum);
	if (ret < 0)
		return ret;

	return __phy_modify(phydev, YTPHY_PAGE_DATA, mask, set);
}

/**
 * ytphy_modify_ext_with_lock() - bits modify a PHY's extended register
 * @phydev: a pointer to a &struct phy_device
 * @regnum: register number to write
 * @mask: bit mask of bits to clear
 * @set: bit mask of bits to set
 *
 * NOTE: Convenience function which allows a PHY's extended register to be
 * modified as new register value = (old register value & ~mask) | set.
 *
 * returns 0 or negative error code
 */
static int ytphy_modify_ext_with_lock(struct phy_device *phydev, u16 regnum,
				      u16 mask, u16 set)
{
	int ret;

	phy_lock_mdio_bus(phydev);
	ret = ytphy_modify_ext(phydev, regnum, mask, set);
	phy_unlock_mdio_bus(phydev);

	return ret;
}

/**
 * ytphy_get_wol() - report whether wake-on-lan is enabled
 * @phydev: a pointer to a &struct phy_device
 * @wol: a pointer to a &struct ethtool_wolinfo
 *
 * NOTE: YTPHY_WOL_CONFIG_REG is common ext reg.
 */
static void ytphy_get_wol(struct phy_device *phydev,
			  struct ethtool_wolinfo *wol)
{
	int wol_config;

	wol->supported = WAKE_MAGIC;
	wol->wolopts = 0;

	wol_config = ytphy_read_ext_with_lock(phydev, YTPHY_WOL_CONFIG_REG);
	if (wol_config < 0)
		return;

	if (wol_config & YTPHY_WCR_ENABLE)
		wol->wolopts |= WAKE_MAGIC;
}

/**
 * ytphy_set_wol() - turn wake-on-lan on or off
 * @phydev: a pointer to a &struct phy_device
 * @wol: a pointer to a &struct ethtool_wolinfo
 *
 * NOTE: YTPHY_WOL_CONFIG_REG, YTPHY_WOL_MACADDR2_REG, YTPHY_WOL_MACADDR1_REG
 * and YTPHY_WOL_MACADDR0_REG are common ext reg. The
 * YTPHY_INTERRUPT_ENABLE_REG of UTP is special, fiber also use this register.
 *
 * returns 0 or negative errno code
 */
static int ytphy_set_wol(struct phy_device *phydev, struct ethtool_wolinfo *wol)
{
	struct net_device *p_attached_dev;
	const u16 mac_addr_reg[] = {
		YTPHY_WOL_MACADDR2_REG,
		YTPHY_WOL_MACADDR1_REG,
		YTPHY_WOL_MACADDR0_REG,
	};
	const u8 *mac_addr;
	int old_page;
	int ret = 0;
	u16 mask;
	u16 val;
	u8 i;

	if (wol->wolopts & WAKE_MAGIC) {
		p_attached_dev = phydev->attached_dev;
		if (!p_attached_dev)
			return -ENODEV;

		mac_addr = (const u8 *)p_attached_dev->dev_addr;
		if (!is_valid_ether_addr(mac_addr))
			return -EINVAL;

		/* lock mdio bus then switch to utp reg space */
		old_page = phy_select_page(phydev, YT8521_RSSR_UTP_SPACE);
		if (old_page < 0)
			goto err_restore_page;

		/* Store the device address for the magic packet */
		for (i = 0; i < 3; i++) {
			ret = ytphy_write_ext(phydev, mac_addr_reg[i],
					      ((mac_addr[i * 2] << 8)) |
						      (mac_addr[i * 2 + 1]));
			if (ret < 0)
				goto err_restore_page;
		}

		/* Enable WOL feature */
		mask = YTPHY_WCR_PULSE_WIDTH_MASK | YTPHY_WCR_INTR_SEL;
		val = YTPHY_WCR_ENABLE | YTPHY_WCR_INTR_SEL;
		val |= YTPHY_WCR_TYPE_PULSE | YTPHY_WCR_PULSE_WIDTH_672MS;
		ret = ytphy_modify_ext(phydev, YTPHY_WOL_CONFIG_REG, mask, val);
		if (ret < 0)
			goto err_restore_page;

		/* Enable WOL interrupt */
		ret = __phy_modify(phydev, YTPHY_INTERRUPT_ENABLE_REG, 0,
				   YTPHY_IER_WOL);
		if (ret < 0)
			goto err_restore_page;

	} else {
		old_page = phy_select_page(phydev, YT8521_RSSR_UTP_SPACE);
		if (old_page < 0)
			goto err_restore_page;

		/* Disable WOL feature */
		mask = YTPHY_WCR_ENABLE | YTPHY_WCR_INTR_SEL;
		ret = ytphy_modify_ext(phydev, YTPHY_WOL_CONFIG_REG, mask, 0);

		/* Disable WOL interrupt */
		ret = __phy_modify(phydev, YTPHY_INTERRUPT_ENABLE_REG,
				   YTPHY_IER_WOL, 0);
		if (ret < 0)
			goto err_restore_page;
	}

err_restore_page:
	return phy_restore_page(phydev, old_page, ret);
}

static int yt8531_set_wol(struct phy_device *phydev,
			  struct ethtool_wolinfo *wol)
{
	const u16 mac_addr_reg[] = {
		YTPHY_WOL_MACADDR2_REG,
		YTPHY_WOL_MACADDR1_REG,
		YTPHY_WOL_MACADDR0_REG,
	};
	const u8 *mac_addr;
	u16 mask, val;
	int ret;
	u8 i;

	if (wol->wolopts & WAKE_MAGIC) {
		mac_addr = phydev->attached_dev->dev_addr;

		/* Store the device address for the magic packet */
		for (i = 0; i < 3; i++) {
			ret = ytphy_write_ext_with_lock(phydev, mac_addr_reg[i],
							((mac_addr[i * 2] << 8)) |
							(mac_addr[i * 2 + 1]));
			if (ret < 0)
				return ret;
		}

		/* Enable WOL feature */
		mask = YTPHY_WCR_PULSE_WIDTH_MASK | YTPHY_WCR_INTR_SEL;
		val = YTPHY_WCR_ENABLE | YTPHY_WCR_INTR_SEL;
		val |= YTPHY_WCR_TYPE_PULSE | YTPHY_WCR_PULSE_WIDTH_672MS;
		ret = ytphy_modify_ext_with_lock(phydev, YTPHY_WOL_CONFIG_REG,
						 mask, val);
		if (ret < 0)
			return ret;

		/* Enable WOL interrupt */
		ret = phy_modify(phydev, YTPHY_INTERRUPT_ENABLE_REG, 0,
				 YTPHY_IER_WOL);
		if (ret < 0)
			return ret;
	} else {
		/* Disable WOL feature */
		mask = YTPHY_WCR_ENABLE | YTPHY_WCR_INTR_SEL;
		ret = ytphy_modify_ext_with_lock(phydev, YTPHY_WOL_CONFIG_REG,
						 mask, 0);

		/* Disable WOL interrupt */
		ret = phy_modify(phydev, YTPHY_INTERRUPT_ENABLE_REG,
				 YTPHY_IER_WOL, 0);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int yt8511_read_page(struct phy_device *phydev)
{
	return __phy_read(phydev, YT8511_PAGE_SELECT);
};

static int yt8511_write_page(struct phy_device *phydev, int page)
{
	return __phy_write(phydev, YT8511_PAGE_SELECT, page);
};

static int yt8511_config_init(struct phy_device *phydev)
{
	int oldpage, ret = 0;
	unsigned int ge, fe;

	oldpage = phy_select_page(phydev, YT8511_EXT_CLK_GATE);
	if (oldpage < 0)
		goto err_restore_page;

	/* set rgmii delay mode */
	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		ge = YT8511_DELAY_GE_TX_DIS;
		fe = YT8511_DELAY_FE_TX_DIS;
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		ge = YT8511_DELAY_RX | YT8511_DELAY_GE_TX_DIS;
		fe = YT8511_DELAY_FE_TX_DIS;
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		ge = YT8511_DELAY_GE_TX_EN;
		fe = YT8511_DELAY_FE_TX_EN;
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		ge = YT8511_DELAY_RX | YT8511_DELAY_GE_TX_EN;
		fe = YT8511_DELAY_FE_TX_EN;
		break;
	default: /* do not support other modes */
		ret = -EOPNOTSUPP;
		goto err_restore_page;
	}

	ret = __phy_modify(phydev, YT8511_PAGE, (YT8511_DELAY_RX | YT8511_DELAY_GE_TX_EN), ge);
	if (ret < 0)
		goto err_restore_page;

	/* set clock mode to 125mhz */
	ret = __phy_modify(phydev, YT8511_PAGE, 0, YT8511_CLK_125M);
	if (ret < 0)
		goto err_restore_page;

	/* fast ethernet delay is in a separate page */
	ret = __phy_write(phydev, YT8511_PAGE_SELECT, YT8511_EXT_DELAY_DRIVE);
	if (ret < 0)
		goto err_restore_page;

	ret = __phy_modify(phydev, YT8511_PAGE, YT8511_DELAY_FE_TX_EN, fe);
	if (ret < 0)
		goto err_restore_page;

	/* leave pll enabled in sleep */
	ret = __phy_write(phydev, YT8511_PAGE_SELECT, YT8511_EXT_SLEEP_CTRL);
	if (ret < 0)
		goto err_restore_page;

	ret = __phy_modify(phydev, YT8511_PAGE, 0, YT8511_PLLON_SLP);
	if (ret < 0)
		goto err_restore_page;

err_restore_page:
	return phy_restore_page(phydev, oldpage, ret);
}

/**
 * yt8521_read_page() - read reg page
 * @phydev: a pointer to a &struct phy_device
 *
 * returns current reg space of yt8521 (YT8521_RSSR_FIBER_SPACE/
 * YT8521_RSSR_UTP_SPACE) or negative errno code
 */
static int yt8521_read_page(struct phy_device *phydev)
{
	int old_page;

	old_page = ytphy_read_ext(phydev, YT8521_REG_SPACE_SELECT_REG);
	if (old_page < 0)
		return old_page;

	if ((old_page & YT8521_RSSR_SPACE_MASK) == YT8521_RSSR_FIBER_SPACE)
		return YT8521_RSSR_FIBER_SPACE;

	return YT8521_RSSR_UTP_SPACE;
};

/**
 * yt8521_write_page() - write reg page
 * @phydev: a pointer to a &struct phy_device
 * @page: The reg page(YT8521_RSSR_FIBER_SPACE/YT8521_RSSR_UTP_SPACE) to write.
 *
 * returns 0 or negative errno code
 */
static int yt8521_write_page(struct phy_device *phydev, int page)
{
	int mask = YT8521_RSSR_SPACE_MASK;
	int set;

	if ((page & YT8521_RSSR_SPACE_MASK) == YT8521_RSSR_FIBER_SPACE)
		set = YT8521_RSSR_FIBER_SPACE;
	else
		set = YT8521_RSSR_UTP_SPACE;

	return ytphy_modify_ext(phydev, YT8521_REG_SPACE_SELECT_REG, mask, set);
};

/**
 * struct ytphy_cfg_reg_map - map a config value to a register value
 * @cfg: value in device configuration
 * @reg: value in the register
 */
struct ytphy_cfg_reg_map {
	u32 cfg;
	u32 reg;
};

static const struct ytphy_cfg_reg_map ytphy_rgmii_delays[] = {
	/* for tx delay / rx delay with YT8521_CCR_RXC_DLY_EN is not set. */
	{ 0,	YT8521_RC1R_RGMII_0_000_NS },
	{ 150,	YT8521_RC1R_RGMII_0_150_NS },
	{ 300,	YT8521_RC1R_RGMII_0_300_NS },
	{ 450,	YT8521_RC1R_RGMII_0_450_NS },
	{ 600,	YT8521_RC1R_RGMII_0_600_NS },
	{ 750,	YT8521_RC1R_RGMII_0_750_NS },
	{ 900,	YT8521_RC1R_RGMII_0_900_NS },
	{ 1050,	YT8521_RC1R_RGMII_1_050_NS },
	{ 1200,	YT8521_RC1R_RGMII_1_200_NS },
	{ 1350,	YT8521_RC1R_RGMII_1_350_NS },
	{ 1500,	YT8521_RC1R_RGMII_1_500_NS },
	{ 1650,	YT8521_RC1R_RGMII_1_650_NS },
	{ 1800,	YT8521_RC1R_RGMII_1_800_NS },
	{ 1950,	YT8521_RC1R_RGMII_1_950_NS },	/* default tx/rx delay */
	{ 2100,	YT8521_RC1R_RGMII_2_100_NS },
	{ 2250,	YT8521_RC1R_RGMII_2_250_NS },

	/* only for rx delay with YT8521_CCR_RXC_DLY_EN is set. */
	{ 0    + YT8521_CCR_RXC_DLY_1_900_NS,	YT8521_RC1R_RGMII_0_000_NS },
	{ 150  + YT8521_CCR_RXC_DLY_1_900_NS,	YT8521_RC1R_RGMII_0_150_NS },
	{ 300  + YT8521_CCR_RXC_DLY_1_900_NS,	YT8521_RC1R_RGMII_0_300_NS },
	{ 450  + YT8521_CCR_RXC_DLY_1_900_NS,	YT8521_RC1R_RGMII_0_450_NS },
	{ 600  + YT8521_CCR_RXC_DLY_1_900_NS,	YT8521_RC1R_RGMII_0_600_NS },
	{ 750  + YT8521_CCR_RXC_DLY_1_900_NS,	YT8521_RC1R_RGMII_0_750_NS },
	{ 900  + YT8521_CCR_RXC_DLY_1_900_NS,	YT8521_RC1R_RGMII_0_900_NS },
	{ 1050 + YT8521_CCR_RXC_DLY_1_900_NS,	YT8521_RC1R_RGMII_1_050_NS },
	{ 1200 + YT8521_CCR_RXC_DLY_1_900_NS,	YT8521_RC1R_RGMII_1_200_NS },
	{ 1350 + YT8521_CCR_RXC_DLY_1_900_NS,	YT8521_RC1R_RGMII_1_350_NS },
	{ 1500 + YT8521_CCR_RXC_DLY_1_900_NS,	YT8521_RC1R_RGMII_1_500_NS },
	{ 1650 + YT8521_CCR_RXC_DLY_1_900_NS,	YT8521_RC1R_RGMII_1_650_NS },
	{ 1800 + YT8521_CCR_RXC_DLY_1_900_NS,	YT8521_RC1R_RGMII_1_800_NS },
	{ 1950 + YT8521_CCR_RXC_DLY_1_900_NS,	YT8521_RC1R_RGMII_1_950_NS },
	{ 2100 + YT8521_CCR_RXC_DLY_1_900_NS,	YT8521_RC1R_RGMII_2_100_NS },
	{ 2250 + YT8521_CCR_RXC_DLY_1_900_NS,	YT8521_RC1R_RGMII_2_250_NS }
};

static u32 ytphy_get_delay_reg_value(struct phy_device *phydev,
				     const char *prop_name,
				     const struct ytphy_cfg_reg_map *tbl,
				     int tb_size,
				     u16 *rxc_dly_en,
				     u32 dflt)
{
	struct device_node *node = phydev->mdio.dev.of_node;
	int tb_size_half = tb_size / 2;
	u32 val;
	int i;

	if (of_property_read_u32(node, prop_name, &val))
		goto err_dts_val;

	/* when rxc_dly_en is NULL, it is get the delay for tx, only half of
	 * tb_size is valid.
	 */
	if (!rxc_dly_en)
		tb_size = tb_size_half;

	for (i = 0; i < tb_size; i++) {
		if (tbl[i].cfg == val) {
			if (rxc_dly_en && i < tb_size_half)
				*rxc_dly_en = 0;
			return tbl[i].reg;
		}
	}

	phydev_warn(phydev, "Unsupported value %d for %s using default (%u)\n",
		    val, prop_name, dflt);

err_dts_val:
	/* when rxc_dly_en is not NULL, it is get the delay for rx.
	 * The rx default in dts and ytphy_rgmii_clk_delay_config is 1950 ps,
	 * so YT8521_CCR_RXC_DLY_EN should not be set.
	 */
	if (rxc_dly_en)
		*rxc_dly_en = 0;

	return dflt;
}

static int ytphy_rgmii_clk_delay_config(struct phy_device *phydev)
{
	int tb_size = ARRAY_SIZE(ytphy_rgmii_delays);
	u16 rxc_dly_en = YT8521_CCR_RXC_DLY_EN;
	u32 rx_reg, tx_reg;
	u16 mask, val = 0;
	int ret;

	rx_reg = ytphy_get_delay_reg_value(phydev, "rx-internal-delay-ps",
					   ytphy_rgmii_delays, tb_size,
					   &rxc_dly_en,
					   YT8521_RC1R_RGMII_1_950_NS);
	tx_reg = ytphy_get_delay_reg_value(phydev, "tx-internal-delay-ps",
					   ytphy_rgmii_delays, tb_size, NULL,
					   YT8521_RC1R_RGMII_1_950_NS);

	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		rxc_dly_en = 0;
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		val |= FIELD_PREP(YT8521_RC1R_RX_DELAY_MASK, rx_reg);
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		rxc_dly_en = 0;
		val |= FIELD_PREP(YT8521_RC1R_GE_TX_DELAY_MASK, tx_reg);
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		val |= FIELD_PREP(YT8521_RC1R_RX_DELAY_MASK, rx_reg) |
		       FIELD_PREP(YT8521_RC1R_GE_TX_DELAY_MASK, tx_reg);
		break;
	default: /* do not support other modes */
		return -EOPNOTSUPP;
	}

	ret = ytphy_modify_ext(phydev, YT8521_CHIP_CONFIG_REG,
			       YT8521_CCR_RXC_DLY_EN, rxc_dly_en);
	if (ret < 0)
		return ret;

	/* Generally, it is not necessary to adjust YT8521_RC1R_FE_TX_DELAY */
	mask = YT8521_RC1R_RX_DELAY_MASK | YT8521_RC1R_GE_TX_DELAY_MASK;
	return ytphy_modify_ext(phydev, YT8521_RGMII_CONFIG1_REG, mask, val);
}

static int ytphy_rgmii_clk_delay_config_with_lock(struct phy_device *phydev)
{
	int ret;

	phy_lock_mdio_bus(phydev);
	ret = ytphy_rgmii_clk_delay_config(phydev);
	phy_unlock_mdio_bus(phydev);

	return ret;
}

/**
 * struct ytphy_ldo_vol_map - map a current value to a register value
 * @vol: ldo voltage
 * @ds:  value in the register
 * @cur: value in device configuration
 */
struct ytphy_ldo_vol_map {
	u32 vol;
	u32 ds;
	u32 cur;
};

static const struct ytphy_ldo_vol_map yt8531_ldo_vol[] = {
	{.vol = YT8531_LDO_VOL_1V8, .ds = 0, .cur = 1200},
	{.vol = YT8531_LDO_VOL_1V8, .ds = 1, .cur = 2100},
	{.vol = YT8531_LDO_VOL_1V8, .ds = 2, .cur = 2700},
	{.vol = YT8531_LDO_VOL_1V8, .ds = 3, .cur = 2910},
	{.vol = YT8531_LDO_VOL_1V8, .ds = 4, .cur = 3110},
	{.vol = YT8531_LDO_VOL_1V8, .ds = 5, .cur = 3600},
	{.vol = YT8531_LDO_VOL_1V8, .ds = 6, .cur = 3970},
	{.vol = YT8531_LDO_VOL_1V8, .ds = 7, .cur = 4350},
	{.vol = YT8531_LDO_VOL_3V3, .ds = 0, .cur = 3070},
	{.vol = YT8531_LDO_VOL_3V3, .ds = 1, .cur = 4080},
	{.vol = YT8531_LDO_VOL_3V3, .ds = 2, .cur = 4370},
	{.vol = YT8531_LDO_VOL_3V3, .ds = 3, .cur = 4680},
	{.vol = YT8531_LDO_VOL_3V3, .ds = 4, .cur = 5020},
	{.vol = YT8531_LDO_VOL_3V3, .ds = 5, .cur = 5450},
	{.vol = YT8531_LDO_VOL_3V3, .ds = 6, .cur = 5740},
	{.vol = YT8531_LDO_VOL_3V3, .ds = 7, .cur = 6140},
};

static u32 yt8531_get_ldo_vol(struct phy_device *phydev)
{
	u32 val;

	val = ytphy_read_ext_with_lock(phydev, YT8521_CHIP_CONFIG_REG);
	val = FIELD_GET(YT8531_RGMII_LDO_VOL_MASK, val);

	return val <= YT8531_LDO_VOL_1V8 ? val : YT8531_LDO_VOL_1V8;
}

static int yt8531_get_ds_map(struct phy_device *phydev, u32 cur)
{
	u32 vol;
	int i;

	vol = yt8531_get_ldo_vol(phydev);
	for (i = 0; i < ARRAY_SIZE(yt8531_ldo_vol); i++) {
		if (yt8531_ldo_vol[i].vol == vol && yt8531_ldo_vol[i].cur == cur)
			return yt8531_ldo_vol[i].ds;
	}

	return -EINVAL;
}

static int yt8531_set_ds(struct phy_device *phydev)
{
	struct device_node *node = phydev->mdio.dev.of_node;
	u32 ds_field_low, ds_field_hi, val;
	int ret, ds;

	/* set rgmii rx clk driver strength */
	if (!of_property_read_u32(node, "motorcomm,rx-clk-drv-microamp", &val)) {
		ds = yt8531_get_ds_map(phydev, val);
		if (ds < 0)
			return dev_err_probe(&phydev->mdio.dev, ds,
					     "No matching current value was found.\n");
	} else {
		ds = YT8531_RGMII_RX_DS_DEFAULT;
	}

	ret = ytphy_modify_ext_with_lock(phydev,
					 YTPHY_PAD_DRIVE_STRENGTH_REG,
					 YT8531_RGMII_RXC_DS_MASK,
					 FIELD_PREP(YT8531_RGMII_RXC_DS_MASK, ds));
	if (ret < 0)
		return ret;

	/* set rgmii rx data driver strength */
	if (!of_property_read_u32(node, "motorcomm,rx-data-drv-microamp", &val)) {
		ds = yt8531_get_ds_map(phydev, val);
		if (ds < 0)
			return dev_err_probe(&phydev->mdio.dev, ds,
					     "No matching current value was found.\n");
	} else {
		ds = YT8531_RGMII_RX_DS_DEFAULT;
	}

	ds_field_hi = FIELD_GET(BIT(2), ds);
	ds_field_hi = FIELD_PREP(YT8531_RGMII_RXD_DS_HI_MASK, ds_field_hi);

	ds_field_low = FIELD_GET(GENMASK(1, 0), ds);
	ds_field_low = FIELD_PREP(YT8531_RGMII_RXD_DS_LOW_MASK, ds_field_low);

	ret = ytphy_modify_ext_with_lock(phydev,
					 YTPHY_PAD_DRIVE_STRENGTH_REG,
					 YT8531_RGMII_RXD_DS_LOW_MASK | YT8531_RGMII_RXD_DS_HI_MASK,
					 ds_field_low | ds_field_hi);
	if (ret < 0)
		return ret;

	return 0;
}

/**
 * yt8521_probe() - read chip config then set suitable polling_mode
 * @phydev: a pointer to a &struct phy_device
 *
 * returns 0 or negative errno code
 */
static int yt8521_probe(struct phy_device *phydev)
{
	struct device_node *node = phydev->mdio.dev.of_node;
	struct device *dev = &phydev->mdio.dev;
	struct yt8521_priv *priv;
	int chip_config;
	u16 mask, val;
	u32 freq;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	chip_config = ytphy_read_ext_with_lock(phydev, YT8521_CHIP_CONFIG_REG);
	if (chip_config < 0)
		return chip_config;

	priv->strap_mode = chip_config & YT8521_CCR_MODE_SEL_MASK;
	switch (priv->strap_mode) {
	case YT8521_CCR_MODE_FIBER_TO_RGMII:
	case YT8521_CCR_MODE_SGPHY_TO_RGMAC:
	case YT8521_CCR_MODE_SGMAC_TO_RGPHY:
		priv->polling_mode = YT8521_MODE_FIBER;
		priv->reg_page = YT8521_RSSR_FIBER_SPACE;
		phydev->port = PORT_FIBRE;
		break;
	case YT8521_CCR_MODE_UTP_FIBER_TO_RGMII:
	case YT8521_CCR_MODE_UTP_TO_FIBER_AUTO:
	case YT8521_CCR_MODE_UTP_TO_FIBER_FORCE:
		priv->polling_mode = YT8521_MODE_POLL;
		priv->reg_page = YT8521_RSSR_TO_BE_ARBITRATED;
		phydev->port = PORT_NONE;
		break;
	case YT8521_CCR_MODE_UTP_TO_SGMII:
	case YT8521_CCR_MODE_UTP_TO_RGMII:
		priv->polling_mode = YT8521_MODE_UTP;
		priv->reg_page = YT8521_RSSR_UTP_SPACE;
		phydev->port = PORT_TP;
		break;
	}
	/* set default reg space */
	if (priv->reg_page != YT8521_RSSR_TO_BE_ARBITRATED) {
		ret = ytphy_write_ext_with_lock(phydev,
						YT8521_REG_SPACE_SELECT_REG,
						priv->reg_page);
		if (ret < 0)
			return ret;
	}

	if (of_property_read_u32(node, "motorcomm,clk-out-frequency-hz", &freq))
		freq = YTPHY_DTS_OUTPUT_CLK_DIS;

	if (phydev->drv->phy_id == PHY_ID_YT8521) {
		switch (freq) {
		case YTPHY_DTS_OUTPUT_CLK_DIS:
			mask = YT8521_SCR_SYNCE_ENABLE;
			val = 0;
			break;
		case YTPHY_DTS_OUTPUT_CLK_25M:
			mask = YT8521_SCR_SYNCE_ENABLE |
			       YT8521_SCR_CLK_SRC_MASK |
			       YT8521_SCR_CLK_FRE_SEL_125M;
			val = YT8521_SCR_SYNCE_ENABLE |
			      FIELD_PREP(YT8521_SCR_CLK_SRC_MASK,
					 YT8521_SCR_CLK_SRC_REF_25M);
			break;
		case YTPHY_DTS_OUTPUT_CLK_125M:
			mask = YT8521_SCR_SYNCE_ENABLE |
			       YT8521_SCR_CLK_SRC_MASK |
			       YT8521_SCR_CLK_FRE_SEL_125M;
			val = YT8521_SCR_SYNCE_ENABLE |
			      YT8521_SCR_CLK_FRE_SEL_125M |
			      FIELD_PREP(YT8521_SCR_CLK_SRC_MASK,
					 YT8521_SCR_CLK_SRC_PLL_125M);
			break;
		default:
			phydev_warn(phydev, "Freq err:%u\n", freq);
			return -EINVAL;
		}
	} else if (phydev->drv->phy_id == PHY_ID_YT8531S) {
		switch (freq) {
		case YTPHY_DTS_OUTPUT_CLK_DIS:
			mask = YT8531_SCR_SYNCE_ENABLE;
			val = 0;
			break;
		case YTPHY_DTS_OUTPUT_CLK_25M:
			mask = YT8531_SCR_SYNCE_ENABLE |
			       YT8531_SCR_CLK_SRC_MASK |
			       YT8531_SCR_CLK_FRE_SEL_125M;
			val = YT8531_SCR_SYNCE_ENABLE |
			      FIELD_PREP(YT8531_SCR_CLK_SRC_MASK,
					 YT8531_SCR_CLK_SRC_REF_25M);
			break;
		case YTPHY_DTS_OUTPUT_CLK_125M:
			mask = YT8531_SCR_SYNCE_ENABLE |
			       YT8531_SCR_CLK_SRC_MASK |
			       YT8531_SCR_CLK_FRE_SEL_125M;
			val = YT8531_SCR_SYNCE_ENABLE |
			      YT8531_SCR_CLK_FRE_SEL_125M |
			      FIELD_PREP(YT8531_SCR_CLK_SRC_MASK,
					 YT8531_SCR_CLK_SRC_PLL_125M);
			break;
		default:
			phydev_warn(phydev, "Freq err:%u\n", freq);
			return -EINVAL;
		}
	} else {
		phydev_warn(phydev, "PHY id err\n");
		return -EINVAL;
	}

	return ytphy_modify_ext_with_lock(phydev, YTPHY_SYNCE_CFG_REG, mask,
					  val);
}

static int yt8531_probe(struct phy_device *phydev)
{
	struct device_node *node = phydev->mdio.dev.of_node;
	u16 mask, val;
	u32 freq;

	if (of_property_read_u32(node, "motorcomm,clk-out-frequency-hz", &freq))
		freq = YTPHY_DTS_OUTPUT_CLK_DIS;

	switch (freq) {
	case YTPHY_DTS_OUTPUT_CLK_DIS:
		mask = YT8531_SCR_SYNCE_ENABLE;
		val = 0;
		break;
	case YTPHY_DTS_OUTPUT_CLK_25M:
		mask = YT8531_SCR_SYNCE_ENABLE | YT8531_SCR_CLK_SRC_MASK |
		       YT8531_SCR_CLK_FRE_SEL_125M;
		val = YT8531_SCR_SYNCE_ENABLE |
		      FIELD_PREP(YT8531_SCR_CLK_SRC_MASK,
				 YT8531_SCR_CLK_SRC_REF_25M);
		break;
	case YTPHY_DTS_OUTPUT_CLK_125M:
		mask = YT8531_SCR_SYNCE_ENABLE | YT8531_SCR_CLK_SRC_MASK |
		       YT8531_SCR_CLK_FRE_SEL_125M;
		val = YT8531_SCR_SYNCE_ENABLE | YT8531_SCR_CLK_FRE_SEL_125M |
		      FIELD_PREP(YT8531_SCR_CLK_SRC_MASK,
				 YT8531_SCR_CLK_SRC_PLL_125M);
		break;
	default:
		phydev_warn(phydev, "Freq err:%u\n", freq);
		return -EINVAL;
	}

	return ytphy_modify_ext_with_lock(phydev, YTPHY_SYNCE_CFG_REG, mask,
					  val);
}

/**
 * ytphy_utp_read_lpa() - read LPA then setup lp_advertising for utp
 * @phydev: a pointer to a &struct phy_device
 *
 * NOTE:The caller must have taken the MDIO bus lock.
 *
 * returns 0 or negative errno code
 */
static int ytphy_utp_read_lpa(struct phy_device *phydev)
{
	int lpa, lpagb;

	if (phydev->autoneg == AUTONEG_ENABLE) {
		if (!phydev->autoneg_complete) {
			mii_stat1000_mod_linkmode_lpa_t(phydev->lp_advertising,
							0);
			mii_lpa_mod_linkmode_lpa_t(phydev->lp_advertising, 0);
			return 0;
		}

		if (phydev->is_gigabit_capable) {
			lpagb = __phy_read(phydev, MII_STAT1000);
			if (lpagb < 0)
				return lpagb;

			if (lpagb & LPA_1000MSFAIL) {
				int adv = __phy_read(phydev, MII_CTRL1000);

				if (adv < 0)
					return adv;

				if (adv & CTL1000_ENABLE_MASTER)
					phydev_err(phydev, "Master/Slave resolution failed, maybe conflicting manual settings?\n");
				else
					phydev_err(phydev, "Master/Slave resolution failed\n");
				return -ENOLINK;
			}

			mii_stat1000_mod_linkmode_lpa_t(phydev->lp_advertising,
							lpagb);
		}

		lpa = __phy_read(phydev, MII_LPA);
		if (lpa < 0)
			return lpa;

		mii_lpa_mod_linkmode_lpa_t(phydev->lp_advertising, lpa);
	} else {
		linkmode_zero(phydev->lp_advertising);
	}

	return 0;
}

/**
 * yt8521_adjust_status() - update speed and duplex to phydev. when in fiber
 * mode, adjust speed and duplex.
 * @phydev: a pointer to a &struct phy_device
 * @status: yt8521 status read from YTPHY_SPECIFIC_STATUS_REG
 * @is_utp: false(yt8521 work in fiber mode) or true(yt8521 work in utp mode)
 *
 * NOTE:The caller must have taken the MDIO bus lock.
 *
 * returns 0
 */
static int yt8521_adjust_status(struct phy_device *phydev, int status,
				bool is_utp)
{
	int speed_mode, duplex;
	int speed;
	int err;
	int lpa;

	if (is_utp)
		duplex = (status & YTPHY_SSR_DUPLEX) >> YTPHY_SSR_DUPLEX_OFFSET;
	else
		duplex = DUPLEX_FULL;	/* for fiber, it always DUPLEX_FULL */

	speed_mode = status & YTPHY_SSR_SPEED_MASK;

	switch (speed_mode) {
	case YTPHY_SSR_SPEED_10M:
		if (is_utp)
			speed = SPEED_10;
		else
			/* for fiber, it will never run here, default to
			 * SPEED_UNKNOWN
			 */
			speed = SPEED_UNKNOWN;
		break;
	case YTPHY_SSR_SPEED_100M:
		speed = SPEED_100;
		break;
	case YTPHY_SSR_SPEED_1000M:
		speed = SPEED_1000;
		break;
	default:
		speed = SPEED_UNKNOWN;
		break;
	}

	phydev->speed = speed;
	phydev->duplex = duplex;

	if (is_utp) {
		err = ytphy_utp_read_lpa(phydev);
		if (err < 0)
			return err;

		phy_resolve_aneg_pause(phydev);
	} else {
		lpa = __phy_read(phydev, MII_LPA);
		if (lpa < 0)
			return lpa;

		/* only support 1000baseX Full */
		linkmode_mod_bit(ETHTOOL_LINK_MODE_1000baseX_Full_BIT,
				 phydev->lp_advertising, lpa & LPA_1000XFULL);

		if (!(lpa & YTPHY_FLPA_PAUSE)) {
			phydev->pause = 0;
			phydev->asym_pause = 0;
		} else if ((lpa & YTPHY_FLPA_ASYM_PAUSE)) {
			phydev->pause = 1;
			phydev->asym_pause = 1;
		} else {
			phydev->pause = 1;
			phydev->asym_pause = 0;
		}
	}

	return 0;
}

/**
 * yt8521_read_status_paged() -  determines the speed and duplex of one page
 * @phydev: a pointer to a &struct phy_device
 * @page: The reg page(YT8521_RSSR_FIBER_SPACE/YT8521_RSSR_UTP_SPACE) to
 * operate.
 *
 * returns 1 (utp or fiber link),0 (no link) or negative errno code
 */
static int yt8521_read_status_paged(struct phy_device *phydev, int page)
{
	int fiber_latch_val;
	int fiber_curr_val;
	int old_page;
	int ret = 0;
	int status;
	int link;

	linkmode_zero(phydev->lp_advertising);
	phydev->duplex = DUPLEX_UNKNOWN;
	phydev->speed = SPEED_UNKNOWN;
	phydev->asym_pause = 0;
	phydev->pause = 0;

	/* YT8521 has two reg space (utp/fiber) for linkup with utp/fiber
	 * respectively. but for utp/fiber combo mode, reg space should be
	 * arbitrated based on media priority. by default, utp takes
	 * priority. reg space should be properly set before read
	 * YTPHY_SPECIFIC_STATUS_REG.
	 */

	page &= YT8521_RSSR_SPACE_MASK;
	old_page = phy_select_page(phydev, page);
	if (old_page < 0)
		goto err_restore_page;

	/* Read YTPHY_SPECIFIC_STATUS_REG, which indicates the speed and duplex
	 * of the PHY is actually using.
	 */
	ret = __phy_read(phydev, YTPHY_SPECIFIC_STATUS_REG);
	if (ret < 0)
		goto err_restore_page;

	status = ret;
	link = !!(status & YTPHY_SSR_LINK);

	/* When PHY is in fiber mode, speed transferred from 1000Mbps to
	 * 100Mbps,there is not link down from YTPHY_SPECIFIC_STATUS_REG, so
	 * we need check MII_BMSR to identify such case.
	 */
	if (page == YT8521_RSSR_FIBER_SPACE) {
		ret = __phy_read(phydev, MII_BMSR);
		if (ret < 0)
			goto err_restore_page;

		fiber_latch_val = ret;
		ret = __phy_read(phydev, MII_BMSR);
		if (ret < 0)
			goto err_restore_page;

		fiber_curr_val = ret;
		if (link && fiber_latch_val != fiber_curr_val) {
			link = 0;
			phydev_info(phydev,
				    "%s, fiber link down detect, latch = %04x, curr = %04x\n",
				    __func__, fiber_latch_val, fiber_curr_val);
		}
	} else {
		/* Read autonegotiation status */
		ret = __phy_read(phydev, MII_BMSR);
		if (ret < 0)
			goto err_restore_page;

		phydev->autoneg_complete = ret & BMSR_ANEGCOMPLETE ? 1 : 0;
	}

	if (link) {
		if (page == YT8521_RSSR_UTP_SPACE)
			yt8521_adjust_status(phydev, status, true);
		else
			yt8521_adjust_status(phydev, status, false);
	}
	return phy_restore_page(phydev, old_page, link);

err_restore_page:
	return phy_restore_page(phydev, old_page, ret);
}

/**
 * yt8521_read_status() -  determines the negotiated speed and duplex
 * @phydev: a pointer to a &struct phy_device
 *
 * returns 0 or negative errno code
 */
static int yt8521_read_status(struct phy_device *phydev)
{
	struct yt8521_priv *priv = phydev->priv;
	int link_fiber = 0;
	int link_utp;
	int link;
	int ret;

	if (priv->reg_page != YT8521_RSSR_TO_BE_ARBITRATED) {
		link = yt8521_read_status_paged(phydev, priv->reg_page);
		if (link < 0)
			return link;
	} else {
		/* when page is YT8521_RSSR_TO_BE_ARBITRATED, arbitration is
		 * needed. by default, utp is higher priority.
		 */

		link_utp = yt8521_read_status_paged(phydev,
						    YT8521_RSSR_UTP_SPACE);
		if (link_utp < 0)
			return link_utp;

		if (!link_utp) {
			link_fiber = yt8521_read_status_paged(phydev,
							      YT8521_RSSR_FIBER_SPACE);
			if (link_fiber < 0)
				return link_fiber;
		}

		link = link_utp || link_fiber;
	}

	if (link) {
		if (phydev->link == 0) {
			/* arbitrate reg space based on linkup media type. */
			if (priv->polling_mode == YT8521_MODE_POLL &&
			    priv->reg_page == YT8521_RSSR_TO_BE_ARBITRATED) {
				if (link_fiber)
					priv->reg_page =
						YT8521_RSSR_FIBER_SPACE;
				else
					priv->reg_page = YT8521_RSSR_UTP_SPACE;

				ret = ytphy_write_ext_with_lock(phydev,
								YT8521_REG_SPACE_SELECT_REG,
								priv->reg_page);
				if (ret < 0)
					return ret;

				phydev->port = link_fiber ? PORT_FIBRE : PORT_TP;

				phydev_info(phydev, "%s, link up, media: %s\n",
					    __func__,
					    (phydev->port == PORT_TP) ?
					    "UTP" : "Fiber");
			}
		}
		phydev->link = 1;
	} else {
		if (phydev->link == 1) {
			phydev_info(phydev, "%s, link down, media: %s\n",
				    __func__, (phydev->port == PORT_TP) ?
				    "UTP" : "Fiber");

			/* When in YT8521_MODE_POLL mode, need prepare for next
			 * arbitration.
			 */
			if (priv->polling_mode == YT8521_MODE_POLL) {
				priv->reg_page = YT8521_RSSR_TO_BE_ARBITRATED;
				phydev->port = PORT_NONE;
			}
		}

		phydev->link = 0;
	}

	return 0;
}

/**
 * yt8521_modify_bmcr_paged - bits modify a PHY's BMCR register of one page
 * @phydev: the phy_device struct
 * @page: The reg page(YT8521_RSSR_FIBER_SPACE/YT8521_RSSR_UTP_SPACE) to operate
 * @mask: bit mask of bits to clear
 * @set: bit mask of bits to set
 *
 * NOTE: Convenience function which allows a PHY's BMCR register to be
 * modified as new register value = (old register value & ~mask) | set.
 * YT8521 has two space (utp/fiber) and three mode (utp/fiber/poll), each space
 * has MII_BMCR. poll mode combines utp and faber,so need do both.
 * If it is reset, it will wait for completion.
 *
 * returns 0 or negative errno code
 */
static int yt8521_modify_bmcr_paged(struct phy_device *phydev, int page,
				    u16 mask, u16 set)
{
	int max_cnt = 500; /* the max wait time of reset ~ 500 ms */
	int old_page;
	int ret = 0;

	old_page = phy_select_page(phydev, page & YT8521_RSSR_SPACE_MASK);
	if (old_page < 0)
		goto err_restore_page;

	ret = __phy_modify(phydev, MII_BMCR, mask, set);
	if (ret < 0)
		goto err_restore_page;

	/* If it is reset, need to wait for the reset to complete */
	if (set == BMCR_RESET) {
		while (max_cnt--) {
			usleep_range(1000, 1100);
			ret = __phy_read(phydev, MII_BMCR);
			if (ret < 0)
				goto err_restore_page;

			if (!(ret & BMCR_RESET))
				return phy_restore_page(phydev, old_page, 0);
		}
	}

err_restore_page:
	return phy_restore_page(phydev, old_page, ret);
}

/**
 * yt8521_modify_utp_fiber_bmcr - bits modify a PHY's BMCR register
 * @phydev: the phy_device struct
 * @mask: bit mask of bits to clear
 * @set: bit mask of bits to set
 *
 * NOTE: Convenience function which allows a PHY's BMCR register to be
 * modified as new register value = (old register value & ~mask) | set.
 * YT8521 has two space (utp/fiber) and three mode (utp/fiber/poll), each space
 * has MII_BMCR. poll mode combines utp and faber,so need do both.
 *
 * returns 0 or negative errno code
 */
static int yt8521_modify_utp_fiber_bmcr(struct phy_device *phydev, u16 mask,
					u16 set)
{
	struct yt8521_priv *priv = phydev->priv;
	int ret;

	if (priv->reg_page != YT8521_RSSR_TO_BE_ARBITRATED) {
		ret = yt8521_modify_bmcr_paged(phydev, priv->reg_page, mask,
					       set);
		if (ret < 0)
			return ret;
	} else {
		ret = yt8521_modify_bmcr_paged(phydev, YT8521_RSSR_UTP_SPACE,
					       mask, set);
		if (ret < 0)
			return ret;

		ret = yt8521_modify_bmcr_paged(phydev, YT8521_RSSR_FIBER_SPACE,
					       mask, set);
		if (ret < 0)
			return ret;
	}
	return 0;
}

/**
 * yt8521_soft_reset() - called to issue a PHY software reset
 * @phydev: a pointer to a &struct phy_device
 *
 * returns 0 or negative errno code
 */
static int yt8521_soft_reset(struct phy_device *phydev)
{
	return yt8521_modify_utp_fiber_bmcr(phydev, 0, BMCR_RESET);
}

/**
 * yt8521_suspend() - suspend the hardware
 * @phydev: a pointer to a &struct phy_device
 *
 * returns 0 or negative errno code
 */
static int yt8521_suspend(struct phy_device *phydev)
{
	int wol_config;

	/* YTPHY_WOL_CONFIG_REG is common ext reg */
	wol_config = ytphy_read_ext_with_lock(phydev, YTPHY_WOL_CONFIG_REG);
	if (wol_config < 0)
		return wol_config;

	/* if wol enable, do nothing */
	if (wol_config & YTPHY_WCR_ENABLE)
		return 0;

	return yt8521_modify_utp_fiber_bmcr(phydev, 0, BMCR_PDOWN);
}

/**
 * yt8521_resume() - resume the hardware
 * @phydev: a pointer to a &struct phy_device
 *
 * returns 0 or negative errno code
 */
static int yt8521_resume(struct phy_device *phydev)
{
	int ret;
	int wol_config;

	/* disable auto sleep */
	ret = ytphy_modify_ext_with_lock(phydev,
					 YT8521_EXTREG_SLEEP_CONTROL1_REG,
					 YT8521_ESC1R_SLEEP_SW, 0);
	if (ret < 0)
		return ret;

	wol_config = ytphy_read_ext_with_lock(phydev, YTPHY_WOL_CONFIG_REG);
	if (wol_config < 0)
		return wol_config;

	/* if wol enable, do nothing */
	if (wol_config & YTPHY_WCR_ENABLE)
		return 0;

	return yt8521_modify_utp_fiber_bmcr(phydev, BMCR_PDOWN, 0);
}

/**
 * yt8521_config_init() - called to initialize the PHY
 * @phydev: a pointer to a &struct phy_device
 *
 * returns 0 or negative errno code
 */
static int yt8521_config_init(struct phy_device *phydev)
{
	struct device_node *node = phydev->mdio.dev.of_node;
	int old_page;
	int ret = 0;

	old_page = phy_select_page(phydev, YT8521_RSSR_UTP_SPACE);
	if (old_page < 0)
		goto err_restore_page;

	/* set rgmii delay mode */
	if (phydev->interface != PHY_INTERFACE_MODE_SGMII) {
		ret = ytphy_rgmii_clk_delay_config(phydev);
		if (ret < 0)
			goto err_restore_page;
	}

	if (of_property_read_bool(node, "motorcomm,auto-sleep-disabled")) {
		/* disable auto sleep */
		ret = ytphy_modify_ext(phydev, YT8521_EXTREG_SLEEP_CONTROL1_REG,
				       YT8521_ESC1R_SLEEP_SW, 0);
		if (ret < 0)
			goto err_restore_page;
	}

	if (of_property_read_bool(node, "motorcomm,keep-pll-enabled")) {
		/* enable RXC clock when no wire plug */
		ret = ytphy_modify_ext(phydev, YT8521_CLOCK_GATING_REG,
				       YT8521_CGR_RX_CLK_EN, 0);
		if (ret < 0)
			goto err_restore_page;
	}
err_restore_page:
	return phy_restore_page(phydev, old_page, ret);
}

static const unsigned long supported_trgs = (BIT(TRIGGER_NETDEV_FULL_DUPLEX) |
					     BIT(TRIGGER_NETDEV_HALF_DUPLEX) |
					     BIT(TRIGGER_NETDEV_LINK)        |
					     BIT(TRIGGER_NETDEV_LINK_10)     |
					     BIT(TRIGGER_NETDEV_LINK_100)    |
					     BIT(TRIGGER_NETDEV_LINK_1000)   |
					     BIT(TRIGGER_NETDEV_RX)          |
					     BIT(TRIGGER_NETDEV_TX));

static int yt8521_led_hw_is_supported(struct phy_device *phydev, u8 index,
				      unsigned long rules)
{
	if (index >= YT8521_MAX_LEDS)
		return -EINVAL;

	/* All combinations of the supported triggers are allowed */
	if (rules & ~supported_trgs)
		return -EOPNOTSUPP;

	return 0;
}

static int yt8521_led_hw_control_set(struct phy_device *phydev, u8 index,
				     unsigned long rules)
{
	u16 val = 0;

	if (index >= YT8521_MAX_LEDS)
		return -EINVAL;

	if (test_bit(TRIGGER_NETDEV_LINK, &rules)) {
		val |= YT8521_LED_10_ON_EN;
		val |= YT8521_LED_100_ON_EN;
		val |= YT8521_LED_1000_ON_EN;
	}

	if (test_bit(TRIGGER_NETDEV_LINK_10, &rules))
		val |= YT8521_LED_10_ON_EN;

	if (test_bit(TRIGGER_NETDEV_LINK_100, &rules))
		val |= YT8521_LED_100_ON_EN;

	if (test_bit(TRIGGER_NETDEV_LINK_1000, &rules))
		val |= YT8521_LED_1000_ON_EN;

	if (test_bit(TRIGGER_NETDEV_FULL_DUPLEX, &rules))
		val |= YT8521_LED_HDX_ON_EN;

	if (test_bit(TRIGGER_NETDEV_HALF_DUPLEX, &rules))
		val |= YT8521_LED_FDX_ON_EN;

	if (test_bit(TRIGGER_NETDEV_TX, &rules) ||
	    test_bit(TRIGGER_NETDEV_RX, &rules))
		val |= YT8521_LED_ACT_BLK_IND;

	if (test_bit(TRIGGER_NETDEV_TX, &rules))
		val |= YT8521_LED_TXACT_BLK_EN;

	if (test_bit(TRIGGER_NETDEV_RX, &rules))
		val |= YT8521_LED_RXACT_BLK_EN;

	return ytphy_write_ext(phydev, YT8521_LED0_CFG_REG + index, val);
}

static int yt8521_led_hw_control_get(struct phy_device *phydev, u8 index,
				     unsigned long *rules)
{
	int val;

	if (index >= YT8521_MAX_LEDS)
		return -EINVAL;

	val = ytphy_read_ext(phydev, YT8521_LED0_CFG_REG + index);
	if (val < 0)
		return val;

	if (val & YT8521_LED_TXACT_BLK_EN || val & YT8521_LED_ACT_BLK_IND)
		__set_bit(TRIGGER_NETDEV_TX, rules);

	if (val & YT8521_LED_RXACT_BLK_EN || val & YT8521_LED_ACT_BLK_IND)
		__set_bit(TRIGGER_NETDEV_RX, rules);

	if (val & YT8521_LED_FDX_ON_EN)
		__set_bit(TRIGGER_NETDEV_FULL_DUPLEX, rules);

	if (val & YT8521_LED_HDX_ON_EN)
		__set_bit(TRIGGER_NETDEV_HALF_DUPLEX, rules);

	if (val & YT8521_LED_1000_ON_EN)
		__set_bit(TRIGGER_NETDEV_LINK_1000, rules);

	if (val & YT8521_LED_100_ON_EN)
		__set_bit(TRIGGER_NETDEV_LINK_100, rules);

	if (val & YT8521_LED_10_ON_EN)
		__set_bit(TRIGGER_NETDEV_LINK_10, rules);

	return 0;
}

static int yt8531_config_init(struct phy_device *phydev)
{
	struct device_node *node = phydev->mdio.dev.of_node;
	int ret;

	ret = ytphy_rgmii_clk_delay_config_with_lock(phydev);
	if (ret < 0)
		return ret;

	if (of_property_read_bool(node, "motorcomm,auto-sleep-disabled")) {
		/* disable auto sleep */
		ret = ytphy_modify_ext_with_lock(phydev,
						 YT8521_EXTREG_SLEEP_CONTROL1_REG,
						 YT8521_ESC1R_SLEEP_SW, 0);
		if (ret < 0)
			return ret;
	}

	if (of_property_read_bool(node, "motorcomm,keep-pll-enabled")) {
		/* enable RXC clock when no wire plug */
		ret = ytphy_modify_ext_with_lock(phydev,
						 YT8521_CLOCK_GATING_REG,
						 YT8521_CGR_RX_CLK_EN, 0);
		if (ret < 0)
			return ret;
	}

	ret = yt8531_set_ds(phydev);
	if (ret < 0)
		return ret;

	return 0;
}

/**
 * yt8531_link_change_notify() - Adjust the tx clock direction according to
 * the current speed and dts config.
 * @phydev: a pointer to a &struct phy_device
 *
 * NOTE: This function is only used to adapt to VF2 with JH7110 SoC. Please
 * keep "motorcomm,tx-clk-adj-enabled" not exist in dts when the soc is not
 * JH7110.
 */
static void yt8531_link_change_notify(struct phy_device *phydev)
{
	struct device_node *node = phydev->mdio.dev.of_node;
	bool tx_clk_1000_inverted = false;
	bool tx_clk_100_inverted = false;
	bool tx_clk_10_inverted = false;
	bool tx_clk_adj_enabled = false;
	u16 val = 0;
	int ret;

	if (of_property_read_bool(node, "motorcomm,tx-clk-adj-enabled"))
		tx_clk_adj_enabled = true;

	if (!tx_clk_adj_enabled)
		return;

	if (of_property_read_bool(node, "motorcomm,tx-clk-10-inverted"))
		tx_clk_10_inverted = true;
	if (of_property_read_bool(node, "motorcomm,tx-clk-100-inverted"))
		tx_clk_100_inverted = true;
	if (of_property_read_bool(node, "motorcomm,tx-clk-1000-inverted"))
		tx_clk_1000_inverted = true;

	if (phydev->speed < 0)
		return;

	switch (phydev->speed) {
	case SPEED_1000:
		if (tx_clk_1000_inverted)
			val = YT8521_RC1R_TX_CLK_SEL_INVERTED;
		break;
	case SPEED_100:
		if (tx_clk_100_inverted)
			val = YT8521_RC1R_TX_CLK_SEL_INVERTED;
		break;
	case SPEED_10:
		if (tx_clk_10_inverted)
			val = YT8521_RC1R_TX_CLK_SEL_INVERTED;
		break;
	default:
		return;
	}

	ret = ytphy_modify_ext_with_lock(phydev, YT8521_RGMII_CONFIG1_REG,
					 YT8521_RC1R_TX_CLK_SEL_INVERTED, val);
	if (ret < 0)
		phydev_warn(phydev, "Modify TX_CLK_SEL err:%d\n", ret);
}

/**
 * yt8521_prepare_fiber_features() -  A small helper function that setup
 * fiber's features.
 * @phydev: a pointer to a &struct phy_device
 * @dst: a pointer to store fiber's features
 */
static void yt8521_prepare_fiber_features(struct phy_device *phydev,
					  unsigned long *dst)
{
	linkmode_set_bit(ETHTOOL_LINK_MODE_100baseFX_Full_BIT, dst);
	linkmode_set_bit(ETHTOOL_LINK_MODE_1000baseX_Full_BIT, dst);
	linkmode_set_bit(ETHTOOL_LINK_MODE_Autoneg_BIT, dst);
	linkmode_set_bit(ETHTOOL_LINK_MODE_FIBRE_BIT, dst);
}

/**
 * yt8521_fiber_setup_forced - configures/forces speed from @phydev
 * @phydev: target phy_device struct
 *
 * NOTE:The caller must have taken the MDIO bus lock.
 *
 * returns 0 or negative errno code
 */
static int yt8521_fiber_setup_forced(struct phy_device *phydev)
{
	u16 val;
	int ret;

	if (phydev->speed == SPEED_1000)
		val = YTPHY_MCR_FIBER_1000BX;
	else if (phydev->speed == SPEED_100)
		val = YTPHY_MCR_FIBER_100FX;
	else
		return -EINVAL;

	ret =  __phy_modify(phydev, MII_BMCR, BMCR_ANENABLE, 0);
	if (ret < 0)
		return ret;

	/* disable Fiber auto sensing */
	ret =  ytphy_modify_ext(phydev, YT8521_LINK_TIMER_CFG2_REG,
				YT8521_LTCR_EN_AUTOSEN, 0);
	if (ret < 0)
		return ret;

	ret =  ytphy_modify_ext(phydev, YTPHY_MISC_CONFIG_REG,
				YTPHY_MCR_FIBER_SPEED_MASK, val);
	if (ret < 0)
		return ret;

	return ytphy_modify_ext(phydev, YT8521_CHIP_CONFIG_REG,
				YT8521_CCR_SW_RST, 0);
}

/**
 * ytphy_check_and_restart_aneg - Enable and restart auto-negotiation
 * @phydev: target phy_device struct
 * @restart: whether aneg restart is requested
 *
 * NOTE:The caller must have taken the MDIO bus lock.
 *
 * returns 0 or negative errno code
 */
static int ytphy_check_and_restart_aneg(struct phy_device *phydev, bool restart)
{
	int ret;

	if (!restart) {
		/* Advertisement hasn't changed, but maybe aneg was never on to
		 * begin with?  Or maybe phy was isolated?
		 */
		ret = __phy_read(phydev, MII_BMCR);
		if (ret < 0)
			return ret;

		if (!(ret & BMCR_ANENABLE) || (ret & BMCR_ISOLATE))
			restart = true;
	}
	/* Enable and Restart Autonegotiation
	 * Don't isolate the PHY if we're negotiating
	 */
	if (restart)
		return __phy_modify(phydev, MII_BMCR, BMCR_ISOLATE,
				    BMCR_ANENABLE | BMCR_ANRESTART);

	return 0;
}

/**
 * yt8521_fiber_config_aneg - restart auto-negotiation or write
 * YTPHY_MISC_CONFIG_REG.
 * @phydev: target phy_device struct
 *
 * NOTE:The caller must have taken the MDIO bus lock.
 *
 * returns 0 or negative errno code
 */
static int yt8521_fiber_config_aneg(struct phy_device *phydev)
{
	int err, changed = 0;
	int bmcr;
	u16 adv;

	if (phydev->autoneg != AUTONEG_ENABLE)
		return yt8521_fiber_setup_forced(phydev);

	/* enable Fiber auto sensing */
	err =  ytphy_modify_ext(phydev, YT8521_LINK_TIMER_CFG2_REG,
				0, YT8521_LTCR_EN_AUTOSEN);
	if (err < 0)
		return err;

	err =  ytphy_modify_ext(phydev, YT8521_CHIP_CONFIG_REG,
				YT8521_CCR_SW_RST, 0);
	if (err < 0)
		return err;

	bmcr = __phy_read(phydev, MII_BMCR);
	if (bmcr < 0)
		return bmcr;

	/* When it is coming from fiber forced mode, add bmcr power down
	 * and power up to let aneg work fine.
	 */
	if (!(bmcr & BMCR_ANENABLE)) {
		__phy_modify(phydev, MII_BMCR, 0, BMCR_PDOWN);
		usleep_range(1000, 1100);
		__phy_modify(phydev, MII_BMCR, BMCR_PDOWN, 0);
	}

	adv = linkmode_adv_to_mii_adv_x(phydev->advertising,
					ETHTOOL_LINK_MODE_1000baseX_Full_BIT);

	/* Setup fiber advertisement */
	err = __phy_modify_changed(phydev, MII_ADVERTISE,
				   ADVERTISE_1000XHALF | ADVERTISE_1000XFULL |
				   ADVERTISE_1000XPAUSE |
				   ADVERTISE_1000XPSE_ASYM,
				   adv);
	if (err < 0)
		return err;

	if (err > 0)
		changed = 1;

	return ytphy_check_and_restart_aneg(phydev, changed);
}

/**
 * ytphy_setup_master_slave
 * @phydev: target phy_device struct
 *
 * NOTE: The caller must have taken the MDIO bus lock.
 *
 * returns 0 or negative errno code
 */
static int ytphy_setup_master_slave(struct phy_device *phydev)
{
	u16 ctl = 0;

	if (!phydev->is_gigabit_capable)
		return 0;

	switch (phydev->master_slave_set) {
	case MASTER_SLAVE_CFG_MASTER_PREFERRED:
		ctl |= CTL1000_PREFER_MASTER;
		break;
	case MASTER_SLAVE_CFG_SLAVE_PREFERRED:
		break;
	case MASTER_SLAVE_CFG_MASTER_FORCE:
		ctl |= CTL1000_AS_MASTER;
		fallthrough;
	case MASTER_SLAVE_CFG_SLAVE_FORCE:
		ctl |= CTL1000_ENABLE_MASTER;
		break;
	case MASTER_SLAVE_CFG_UNKNOWN:
	case MASTER_SLAVE_CFG_UNSUPPORTED:
		return 0;
	default:
		phydev_warn(phydev, "Unsupported Master/Slave mode\n");
		return -EOPNOTSUPP;
	}

	return __phy_modify_changed(phydev, MII_CTRL1000,
				    (CTL1000_ENABLE_MASTER | CTL1000_AS_MASTER |
				    CTL1000_PREFER_MASTER), ctl);
}

/**
 * ytphy_utp_config_advert - sanitize and advertise auto-negotiation parameters
 * @phydev: target phy_device struct
 *
 * NOTE: Writes MII_ADVERTISE with the appropriate values,
 * after sanitizing the values to make sure we only advertise
 * what is supported.  Returns < 0 on error, 0 if the PHY's advertisement
 * hasn't changed, and > 0 if it has changed.
 * The caller must have taken the MDIO bus lock.
 *
 * returns 0 or negative errno code
 */
static int ytphy_utp_config_advert(struct phy_device *phydev)
{
	int err, bmsr, changed = 0;
	u32 adv;

	/* Only allow advertising what this PHY supports */
	linkmode_and(phydev->advertising, phydev->advertising,
		     phydev->supported);

	adv = linkmode_adv_to_mii_adv_t(phydev->advertising);

	/* Setup standard advertisement */
	err = __phy_modify_changed(phydev, MII_ADVERTISE,
				   ADVERTISE_ALL | ADVERTISE_100BASE4 |
				   ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM,
				   adv);
	if (err < 0)
		return err;
	if (err > 0)
		changed = 1;

	bmsr = __phy_read(phydev, MII_BMSR);
	if (bmsr < 0)
		return bmsr;

	/* Per 802.3-2008, Section 22.2.4.2.16 Extended status all
	 * 1000Mbits/sec capable PHYs shall have the BMSR_ESTATEN bit set to a
	 * logical 1.
	 */
	if (!(bmsr & BMSR_ESTATEN))
		return changed;

	adv = linkmode_adv_to_mii_ctrl1000_t(phydev->advertising);

	err = __phy_modify_changed(phydev, MII_CTRL1000,
				   ADVERTISE_1000FULL | ADVERTISE_1000HALF,
				   adv);
	if (err < 0)
		return err;
	if (err > 0)
		changed = 1;

	return changed;
}

/**
 * ytphy_utp_config_aneg - restart auto-negotiation or write BMCR
 * @phydev: target phy_device struct
 * @changed: whether autoneg is requested
 *
 * NOTE: If auto-negotiation is enabled, we configure the
 * advertising, and then restart auto-negotiation.  If it is not
 * enabled, then we write the BMCR.
 * The caller must have taken the MDIO bus lock.
 *
 * returns 0 or negative errno code
 */
static int ytphy_utp_config_aneg(struct phy_device *phydev, bool changed)
{
	int err;
	u16 ctl;

	err = ytphy_setup_master_slave(phydev);
	if (err < 0)
		return err;
	else if (err)
		changed = true;

	if (phydev->autoneg != AUTONEG_ENABLE) {
		/* configures/forces speed/duplex from @phydev */

		ctl = mii_bmcr_encode_fixed(phydev->speed, phydev->duplex);

		return __phy_modify(phydev, MII_BMCR, ~(BMCR_LOOPBACK |
				    BMCR_ISOLATE | BMCR_PDOWN), ctl);
	}

	err = ytphy_utp_config_advert(phydev);
	if (err < 0) /* error */
		return err;
	else if (err)
		changed = true;

	return ytphy_check_and_restart_aneg(phydev, changed);
}

/**
 * yt8521_config_aneg_paged() - switch reg space then call genphy_config_aneg
 * of one page
 * @phydev: a pointer to a &struct phy_device
 * @page: The reg page(YT8521_RSSR_FIBER_SPACE/YT8521_RSSR_UTP_SPACE) to
 * operate.
 *
 * returns 0 or negative errno code
 */
static int yt8521_config_aneg_paged(struct phy_device *phydev, int page)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(fiber_supported);
	struct yt8521_priv *priv = phydev->priv;
	int old_page;
	int ret = 0;

	page &= YT8521_RSSR_SPACE_MASK;

	old_page = phy_select_page(phydev, page);
	if (old_page < 0)
		goto err_restore_page;

	/* If reg_page is YT8521_RSSR_TO_BE_ARBITRATED,
	 * phydev->advertising should be updated.
	 */
	if (priv->reg_page == YT8521_RSSR_TO_BE_ARBITRATED) {
		linkmode_zero(fiber_supported);
		yt8521_prepare_fiber_features(phydev, fiber_supported);

		/* prepare fiber_supported, then setup advertising. */
		if (page == YT8521_RSSR_FIBER_SPACE) {
			linkmode_set_bit(ETHTOOL_LINK_MODE_Pause_BIT,
					 fiber_supported);
			linkmode_set_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT,
					 fiber_supported);
			linkmode_and(phydev->advertising,
				     priv->combo_advertising, fiber_supported);
		} else {
			/* ETHTOOL_LINK_MODE_Autoneg_BIT is also used in utp */
			linkmode_clear_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
					   fiber_supported);
			linkmode_andnot(phydev->advertising,
					priv->combo_advertising,
					fiber_supported);
		}
	}

	if (page == YT8521_RSSR_FIBER_SPACE)
		ret = yt8521_fiber_config_aneg(phydev);
	else
		ret = ytphy_utp_config_aneg(phydev, false);

err_restore_page:
	return phy_restore_page(phydev, old_page, ret);
}

/**
 * yt8521_config_aneg() - change reg space then call yt8521_config_aneg_paged
 * @phydev: a pointer to a &struct phy_device
 *
 * returns 0 or negative errno code
 */
static int yt8521_config_aneg(struct phy_device *phydev)
{
	struct yt8521_priv *priv = phydev->priv;
	int ret;

	if (priv->reg_page != YT8521_RSSR_TO_BE_ARBITRATED) {
		ret = yt8521_config_aneg_paged(phydev, priv->reg_page);
		if (ret < 0)
			return ret;
	} else {
		/* If reg_page is YT8521_RSSR_TO_BE_ARBITRATED,
		 * phydev->advertising need to be saved at first run.
		 * Because it contains the advertising which supported by both
		 * mac and yt8521(utp and fiber).
		 */
		if (linkmode_empty(priv->combo_advertising)) {
			linkmode_copy(priv->combo_advertising,
				      phydev->advertising);
		}

		ret = yt8521_config_aneg_paged(phydev, YT8521_RSSR_UTP_SPACE);
		if (ret < 0)
			return ret;

		ret = yt8521_config_aneg_paged(phydev, YT8521_RSSR_FIBER_SPACE);
		if (ret < 0)
			return ret;

		/* we don't known which will be link, so restore
		 * phydev->advertising as default value.
		 */
		linkmode_copy(phydev->advertising, priv->combo_advertising);
	}
	return 0;
}

/**
 * yt8521_aneg_done_paged() - determines the auto negotiation result of one
 * page.
 * @phydev: a pointer to a &struct phy_device
 * @page: The reg page(YT8521_RSSR_FIBER_SPACE/YT8521_RSSR_UTP_SPACE) to
 * operate.
 *
 * returns 0(no link)or 1(fiber or utp link) or negative errno code
 */
static int yt8521_aneg_done_paged(struct phy_device *phydev, int page)
{
	int old_page;
	int ret = 0;
	int link;

	old_page = phy_select_page(phydev, page & YT8521_RSSR_SPACE_MASK);
	if (old_page < 0)
		goto err_restore_page;

	ret = __phy_read(phydev, YTPHY_SPECIFIC_STATUS_REG);
	if (ret < 0)
		goto err_restore_page;

	link = !!(ret & YTPHY_SSR_LINK);
	ret = link;

err_restore_page:
	return phy_restore_page(phydev, old_page, ret);
}

/**
 * yt8521_aneg_done() - determines the auto negotiation result
 * @phydev: a pointer to a &struct phy_device
 *
 * returns 0(no link)or 1(fiber or utp link) or negative errno code
 */
static int yt8521_aneg_done(struct phy_device *phydev)
{
	struct yt8521_priv *priv = phydev->priv;
	int link_fiber = 0;
	int link_utp;
	int link;

	if (priv->reg_page != YT8521_RSSR_TO_BE_ARBITRATED) {
		link = yt8521_aneg_done_paged(phydev, priv->reg_page);
	} else {
		link_utp = yt8521_aneg_done_paged(phydev,
						  YT8521_RSSR_UTP_SPACE);
		if (link_utp < 0)
			return link_utp;

		if (!link_utp) {
			link_fiber = yt8521_aneg_done_paged(phydev,
							    YT8521_RSSR_FIBER_SPACE);
			if (link_fiber < 0)
				return link_fiber;
		}
		link = link_fiber || link_utp;
		phydev_info(phydev, "%s, link_fiber: %d, link_utp: %d\n",
			    __func__, link_fiber, link_utp);
	}

	return link;
}

/**
 * ytphy_utp_read_abilities - read PHY abilities from Clause 22 registers
 * @phydev: target phy_device struct
 *
 * NOTE: Reads the PHY's abilities and populates
 * phydev->supported accordingly.
 * The caller must have taken the MDIO bus lock.
 *
 * returns 0 or negative errno code
 */
static int ytphy_utp_read_abilities(struct phy_device *phydev)
{
	int val;

	linkmode_set_bit_array(phy_basic_ports_array,
			       ARRAY_SIZE(phy_basic_ports_array),
			       phydev->supported);

	val = __phy_read(phydev, MII_BMSR);
	if (val < 0)
		return val;

	linkmode_mod_bit(ETHTOOL_LINK_MODE_Autoneg_BIT, phydev->supported,
			 val & BMSR_ANEGCAPABLE);

	linkmode_mod_bit(ETHTOOL_LINK_MODE_100baseT_Full_BIT, phydev->supported,
			 val & BMSR_100FULL);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_100baseT_Half_BIT, phydev->supported,
			 val & BMSR_100HALF);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_10baseT_Full_BIT, phydev->supported,
			 val & BMSR_10FULL);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_10baseT_Half_BIT, phydev->supported,
			 val & BMSR_10HALF);

	if (val & BMSR_ESTATEN) {
		val = __phy_read(phydev, MII_ESTATUS);
		if (val < 0)
			return val;

		linkmode_mod_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
				 phydev->supported, val & ESTATUS_1000_TFULL);
		linkmode_mod_bit(ETHTOOL_LINK_MODE_1000baseT_Half_BIT,
				 phydev->supported, val & ESTATUS_1000_THALF);
		linkmode_mod_bit(ETHTOOL_LINK_MODE_1000baseX_Full_BIT,
				 phydev->supported, val & ESTATUS_1000_XFULL);
	}

	return 0;
}

/**
 * yt8521_get_features_paged() -  read supported link modes for one page
 * @phydev: a pointer to a &struct phy_device
 * @page: The reg page(YT8521_RSSR_FIBER_SPACE/YT8521_RSSR_UTP_SPACE) to
 * operate.
 *
 * returns 0 or negative errno code
 */
static int yt8521_get_features_paged(struct phy_device *phydev, int page)
{
	int old_page;
	int ret = 0;

	page &= YT8521_RSSR_SPACE_MASK;
	old_page = phy_select_page(phydev, page);
	if (old_page < 0)
		goto err_restore_page;

	if (page == YT8521_RSSR_FIBER_SPACE) {
		linkmode_zero(phydev->supported);
		yt8521_prepare_fiber_features(phydev, phydev->supported);
	} else {
		ret = ytphy_utp_read_abilities(phydev);
		if (ret < 0)
			goto err_restore_page;
	}

err_restore_page:
	return phy_restore_page(phydev, old_page, ret);
}

/**
 * yt8521_get_features - switch reg space then call yt8521_get_features_paged
 * @phydev: target phy_device struct
 *
 * returns 0 or negative errno code
 */
static int yt8521_get_features(struct phy_device *phydev)
{
	struct yt8521_priv *priv = phydev->priv;
	int ret;

	if (priv->reg_page != YT8521_RSSR_TO_BE_ARBITRATED) {
		ret = yt8521_get_features_paged(phydev, priv->reg_page);
	} else {
		ret = yt8521_get_features_paged(phydev,
						YT8521_RSSR_UTP_SPACE);
		if (ret < 0)
			return ret;

		/* add fiber's features to phydev->supported */
		yt8521_prepare_fiber_features(phydev, phydev->supported);
	}
	return ret;
}

/**
 * yt8821_get_features - read mmd register to get 2.5G capability
 * @phydev: target phy_device struct
 *
 * Returns: 0 or negative errno code
 */
static int yt8821_get_features(struct phy_device *phydev)
{
	int ret;

	ret = genphy_c45_pma_read_ext_abilities(phydev);
	if (ret < 0)
		return ret;

	return genphy_read_abilities(phydev);
}

/**
 * yt8821_get_rate_matching - read register to get phy chip mode
 * @phydev: target phy_device struct
 * @iface: PHY data interface type
 *
 * Returns: rate matching type or negative errno code
 */
static int yt8821_get_rate_matching(struct phy_device *phydev,
				    phy_interface_t iface)
{
	int val;

	val = ytphy_read_ext_with_lock(phydev, YT8521_CHIP_CONFIG_REG);
	if (val < 0)
		return val;

	if (FIELD_GET(YT8521_CCR_MODE_SEL_MASK, val) ==
	    YT8821_CHIP_MODE_FORCE_BX2500)
		return RATE_MATCH_PAUSE;

	return RATE_MATCH_NONE;
}

/**
 * yt8821_aneg_done() - determines the auto negotiation result
 * @phydev: a pointer to a &struct phy_device
 *
 * Returns: 0(no link)or 1(utp link) or negative errno code
 */
static int yt8821_aneg_done(struct phy_device *phydev)
{
	return yt8521_aneg_done_paged(phydev, YT8521_RSSR_UTP_SPACE);
}

/**
 * yt8821_serdes_init() - serdes init
 * @phydev: a pointer to a &struct phy_device
 *
 * Returns: 0 or negative errno code
 */
static int yt8821_serdes_init(struct phy_device *phydev)
{
	int old_page;
	int ret = 0;
	u16 mask;
	u16 set;

	old_page = phy_select_page(phydev, YT8521_RSSR_FIBER_SPACE);
	if (old_page < 0) {
		phydev_err(phydev, "Failed to select page: %d\n",
			   old_page);
		goto err_restore_page;
	}

	ret = __phy_modify(phydev, MII_BMCR, BMCR_ANENABLE, 0);
	if (ret < 0)
		goto err_restore_page;

	mask = YT8821_SDS_EXT_CSR_VCO_LDO_EN |
		YT8821_SDS_EXT_CSR_VCO_BIAS_LPF_EN;
	set = YT8821_SDS_EXT_CSR_VCO_LDO_EN;
	ret = ytphy_modify_ext(phydev, YT8821_SDS_EXT_CSR_CTRL_REG, mask,
			       set);

err_restore_page:
	return phy_restore_page(phydev, old_page, ret);
}

/**
 * yt8821_utp_init() - utp init
 * @phydev: a pointer to a &struct phy_device
 *
 * Returns: 0 or negative errno code
 */
static int yt8821_utp_init(struct phy_device *phydev)
{
	int old_page;
	int ret = 0;
	u16 mask;
	u16 save;
	u16 set;

	old_page = phy_select_page(phydev, YT8521_RSSR_UTP_SPACE);
	if (old_page < 0) {
		phydev_err(phydev, "Failed to select page: %d\n",
			   old_page);
		goto err_restore_page;
	}

	mask = YT8821_UTP_EXT_RPDN_BP_FFE_LNG_2500 |
		YT8821_UTP_EXT_RPDN_BP_FFE_SHT_2500 |
		YT8821_UTP_EXT_RPDN_IPR_SHT_2500;
	set = YT8821_UTP_EXT_RPDN_BP_FFE_LNG_2500 |
		YT8821_UTP_EXT_RPDN_BP_FFE_SHT_2500;
	ret = ytphy_modify_ext(phydev, YT8821_UTP_EXT_RPDN_CTRL_REG,
			       mask, set);
	if (ret < 0)
		goto err_restore_page;

	mask = YT8821_UTP_EXT_VGA_LPF1_CAP_OTHER |
		YT8821_UTP_EXT_VGA_LPF1_CAP_2500;
	ret = ytphy_modify_ext(phydev,
			       YT8821_UTP_EXT_VGA_LPF1_CAP_CTRL_REG,
			       mask, 0);
	if (ret < 0)
		goto err_restore_page;

	mask = YT8821_UTP_EXT_VGA_LPF2_CAP_OTHER |
		YT8821_UTP_EXT_VGA_LPF2_CAP_2500;
	ret = ytphy_modify_ext(phydev,
			       YT8821_UTP_EXT_VGA_LPF2_CAP_CTRL_REG,
			       mask, 0);
	if (ret < 0)
		goto err_restore_page;

	mask = YT8821_UTP_EXT_TRACE_LNG_GAIN_THE_2500 |
		YT8821_UTP_EXT_TRACE_MED_GAIN_THE_2500;
	set = FIELD_PREP(YT8821_UTP_EXT_TRACE_LNG_GAIN_THE_2500, 0x5a) |
		FIELD_PREP(YT8821_UTP_EXT_TRACE_MED_GAIN_THE_2500, 0x3c);
	ret = ytphy_modify_ext(phydev, YT8821_UTP_EXT_TRACE_CTRL_REG,
			       mask, set);
	if (ret < 0)
		goto err_restore_page;

	mask = YT8821_UTP_EXT_IPR_LNG_2500;
	set = FIELD_PREP(YT8821_UTP_EXT_IPR_LNG_2500, 0x6c);
	ret = ytphy_modify_ext(phydev,
			       YT8821_UTP_EXT_ALPHA_IPR_CTRL_REG,
			       mask, set);
	if (ret < 0)
		goto err_restore_page;

	mask = YT8821_UTP_EXT_TRACE_LNG_GAIN_THR_1000;
	set = FIELD_PREP(YT8821_UTP_EXT_TRACE_LNG_GAIN_THR_1000, 0x2a);
	ret = ytphy_modify_ext(phydev, YT8821_UTP_EXT_ECHO_CTRL_REG,
			       mask, set);
	if (ret < 0)
		goto err_restore_page;

	mask = YT8821_UTP_EXT_TRACE_MED_GAIN_THR_1000;
	set = FIELD_PREP(YT8821_UTP_EXT_TRACE_MED_GAIN_THR_1000, 0x22);
	ret = ytphy_modify_ext(phydev, YT8821_UTP_EXT_GAIN_CTRL_REG,
			       mask, set);
	if (ret < 0)
		goto err_restore_page;

	mask = YT8821_UTP_EXT_TH_20DB_2500;
	set = FIELD_PREP(YT8821_UTP_EXT_TH_20DB_2500, 0x8000);
	ret = ytphy_modify_ext(phydev,
			       YT8821_UTP_EXT_TH_20DB_2500_CTRL_REG,
			       mask, set);
	if (ret < 0)
		goto err_restore_page;

	mask = YT8821_UTP_EXT_MU_COARSE_FR_F_FFE |
		YT8821_UTP_EXT_MU_COARSE_FR_F_FBE;
	set = FIELD_PREP(YT8821_UTP_EXT_MU_COARSE_FR_F_FFE, 0x7) |
		FIELD_PREP(YT8821_UTP_EXT_MU_COARSE_FR_F_FBE, 0x7);
	ret = ytphy_modify_ext(phydev,
			       YT8821_UTP_EXT_MU_COARSE_FR_CTRL_REG,
			       mask, set);
	if (ret < 0)
		goto err_restore_page;

	mask = YT8821_UTP_EXT_MU_FINE_FR_F_FFE |
		YT8821_UTP_EXT_MU_FINE_FR_F_FBE;
	set = FIELD_PREP(YT8821_UTP_EXT_MU_FINE_FR_F_FFE, 0x2) |
		FIELD_PREP(YT8821_UTP_EXT_MU_FINE_FR_F_FBE, 0x2);
	ret = ytphy_modify_ext(phydev,
			       YT8821_UTP_EXT_MU_FINE_FR_CTRL_REG,
			       mask, set);
	if (ret < 0)
		goto err_restore_page;

	/* save YT8821_UTP_EXT_PI_CTRL_REG's val for use later */
	ret = ytphy_read_ext(phydev, YT8821_UTP_EXT_PI_CTRL_REG);
	if (ret < 0)
		goto err_restore_page;

	save = ret;

	mask = YT8821_UTP_EXT_PI_TX_CLK_SEL_AFE |
		YT8821_UTP_EXT_PI_RX_CLK_3_SEL_AFE |
		YT8821_UTP_EXT_PI_RX_CLK_2_SEL_AFE |
		YT8821_UTP_EXT_PI_RX_CLK_1_SEL_AFE |
		YT8821_UTP_EXT_PI_RX_CLK_0_SEL_AFE;
	ret = ytphy_modify_ext(phydev, YT8821_UTP_EXT_PI_CTRL_REG,
			       mask, 0);
	if (ret < 0)
		goto err_restore_page;

	/* restore YT8821_UTP_EXT_PI_CTRL_REG's val */
	ret = ytphy_write_ext(phydev, YT8821_UTP_EXT_PI_CTRL_REG, save);
	if (ret < 0)
		goto err_restore_page;

	mask = YT8821_UTP_EXT_FECHO_AMP_TH_HUGE;
	set = FIELD_PREP(YT8821_UTP_EXT_FECHO_AMP_TH_HUGE, 0x38);
	ret = ytphy_modify_ext(phydev, YT8821_UTP_EXT_VCT_CFG6_CTRL_REG,
			       mask, set);
	if (ret < 0)
		goto err_restore_page;

	mask = YT8821_UTP_EXT_NFR_TX_ABILITY;
	set = YT8821_UTP_EXT_NFR_TX_ABILITY;
	ret = ytphy_modify_ext(phydev,
			       YT8821_UTP_EXT_TXGE_NFR_FR_THP_CTRL_REG,
			       mask, set);
	if (ret < 0)
		goto err_restore_page;

	mask = YT8821_UTP_EXT_PLL_SPARE_CFG;
	set = FIELD_PREP(YT8821_UTP_EXT_PLL_SPARE_CFG, 0xe9);
	ret = ytphy_modify_ext(phydev, YT8821_UTP_EXT_PLL_CTRL_REG,
			       mask, set);
	if (ret < 0)
		goto err_restore_page;

	mask = YT8821_UTP_EXT_DAC_IMID_CH_3_10_ORG |
		YT8821_UTP_EXT_DAC_IMID_CH_2_10_ORG;
	set = FIELD_PREP(YT8821_UTP_EXT_DAC_IMID_CH_3_10_ORG, 0x64) |
		FIELD_PREP(YT8821_UTP_EXT_DAC_IMID_CH_2_10_ORG, 0x64);
	ret = ytphy_modify_ext(phydev,
			       YT8821_UTP_EXT_DAC_IMID_CH_2_3_CTRL_REG,
			       mask, set);
	if (ret < 0)
		goto err_restore_page;

	mask = YT8821_UTP_EXT_DAC_IMID_CH_1_10_ORG |
		YT8821_UTP_EXT_DAC_IMID_CH_0_10_ORG;
	set = FIELD_PREP(YT8821_UTP_EXT_DAC_IMID_CH_1_10_ORG, 0x64) |
		FIELD_PREP(YT8821_UTP_EXT_DAC_IMID_CH_0_10_ORG, 0x64);
	ret = ytphy_modify_ext(phydev,
			       YT8821_UTP_EXT_DAC_IMID_CH_0_1_CTRL_REG,
			       mask, set);
	if (ret < 0)
		goto err_restore_page;

	mask = YT8821_UTP_EXT_DAC_IMSB_CH_3_10_ORG |
		YT8821_UTP_EXT_DAC_IMSB_CH_2_10_ORG;
	set = FIELD_PREP(YT8821_UTP_EXT_DAC_IMSB_CH_3_10_ORG, 0x64) |
		FIELD_PREP(YT8821_UTP_EXT_DAC_IMSB_CH_2_10_ORG, 0x64);
	ret = ytphy_modify_ext(phydev,
			       YT8821_UTP_EXT_DAC_IMSB_CH_2_3_CTRL_REG,
			       mask, set);
	if (ret < 0)
		goto err_restore_page;

	mask = YT8821_UTP_EXT_DAC_IMSB_CH_1_10_ORG |
		YT8821_UTP_EXT_DAC_IMSB_CH_0_10_ORG;
	set = FIELD_PREP(YT8821_UTP_EXT_DAC_IMSB_CH_1_10_ORG, 0x64) |
		FIELD_PREP(YT8821_UTP_EXT_DAC_IMSB_CH_0_10_ORG, 0x64);
	ret = ytphy_modify_ext(phydev,
			       YT8821_UTP_EXT_DAC_IMSB_CH_0_1_CTRL_REG,
			       mask, set);

err_restore_page:
	return phy_restore_page(phydev, old_page, ret);
}

/**
 * yt8821_auto_sleep_config() - phy auto sleep config
 * @phydev: a pointer to a &struct phy_device
 * @enable: true enable auto sleep, false disable auto sleep
 *
 * Returns: 0 or negative errno code
 */
static int yt8821_auto_sleep_config(struct phy_device *phydev,
				    bool enable)
{
	int old_page;
	int ret = 0;

	old_page = phy_select_page(phydev, YT8521_RSSR_UTP_SPACE);
	if (old_page < 0) {
		phydev_err(phydev, "Failed to select page: %d\n",
			   old_page);
		goto err_restore_page;
	}

	ret = ytphy_modify_ext(phydev,
			       YT8521_EXTREG_SLEEP_CONTROL1_REG,
			       YT8521_ESC1R_SLEEP_SW,
			       enable ? 1 : 0);

err_restore_page:
	return phy_restore_page(phydev, old_page, ret);
}

/**
 * yt8821_soft_reset() - soft reset utp and serdes
 * @phydev: a pointer to a &struct phy_device
 *
 * Returns: 0 or negative errno code
 */
static int yt8821_soft_reset(struct phy_device *phydev)
{
	return ytphy_modify_ext_with_lock(phydev, YT8521_CHIP_CONFIG_REG,
					  YT8521_CCR_SW_RST, 0);
}

/**
 * yt8821_config_init() - phy initializatioin
 * @phydev: a pointer to a &struct phy_device
 *
 * Returns: 0 or negative errno code
 */
static int yt8821_config_init(struct phy_device *phydev)
{
	u8 mode = YT8821_CHIP_MODE_AUTO_BX2500_SGMII;
	int ret;
	u16 set;

	if (phydev->interface == PHY_INTERFACE_MODE_2500BASEX)
		mode = YT8821_CHIP_MODE_FORCE_BX2500;

	set = FIELD_PREP(YT8521_CCR_MODE_SEL_MASK, mode);
	ret = ytphy_modify_ext_with_lock(phydev,
					 YT8521_CHIP_CONFIG_REG,
					 YT8521_CCR_MODE_SEL_MASK,
					 set);
	if (ret < 0)
		return ret;

	__set_bit(PHY_INTERFACE_MODE_2500BASEX,
		  phydev->possible_interfaces);

	if (mode == YT8821_CHIP_MODE_AUTO_BX2500_SGMII) {
		__set_bit(PHY_INTERFACE_MODE_SGMII,
			  phydev->possible_interfaces);

		phydev->rate_matching = RATE_MATCH_NONE;
	} else if (mode == YT8821_CHIP_MODE_FORCE_BX2500) {
		phydev->rate_matching = RATE_MATCH_PAUSE;
	}

	ret = yt8821_serdes_init(phydev);
	if (ret < 0)
		return ret;

	ret = yt8821_utp_init(phydev);
	if (ret < 0)
		return ret;

	/* disable auto sleep */
	ret = yt8821_auto_sleep_config(phydev, false);
	if (ret < 0)
		return ret;

	/* soft reset */
	return yt8821_soft_reset(phydev);
}

/**
 * yt8821_adjust_status() - update speed and duplex to phydev
 * @phydev: a pointer to a &struct phy_device
 * @val: read from YTPHY_SPECIFIC_STATUS_REG
 */
static void yt8821_adjust_status(struct phy_device *phydev, int val)
{
	int speed, duplex;
	int speed_mode;

	duplex = FIELD_GET(YTPHY_SSR_DUPLEX, val);
	speed_mode = val & YTPHY_SSR_SPEED_MASK;
	switch (speed_mode) {
	case YTPHY_SSR_SPEED_10M:
		speed = SPEED_10;
		break;
	case YTPHY_SSR_SPEED_100M:
		speed = SPEED_100;
		break;
	case YTPHY_SSR_SPEED_1000M:
		speed = SPEED_1000;
		break;
	case YTPHY_SSR_SPEED_2500M:
		speed = SPEED_2500;
		break;
	default:
		speed = SPEED_UNKNOWN;
		break;
	}

	phydev->speed = speed;
	phydev->duplex = duplex;
}

/**
 * yt8821_update_interface() - update interface per current speed
 * @phydev: a pointer to a &struct phy_device
 */
static void yt8821_update_interface(struct phy_device *phydev)
{
	if (!phydev->link)
		return;

	switch (phydev->speed) {
	case SPEED_2500:
		phydev->interface = PHY_INTERFACE_MODE_2500BASEX;
		break;
	case SPEED_1000:
	case SPEED_100:
	case SPEED_10:
		phydev->interface = PHY_INTERFACE_MODE_SGMII;
		break;
	default:
		phydev_warn(phydev, "phy speed err :%d\n", phydev->speed);
		break;
	}
}

/**
 * yt8821_read_status() -  determines the negotiated speed and duplex
 * @phydev: a pointer to a &struct phy_device
 *
 * Returns: 0 or negative errno code
 */
static int yt8821_read_status(struct phy_device *phydev)
{
	int link;
	int ret;
	int val;

	ret = ytphy_write_ext_with_lock(phydev,
					YT8521_REG_SPACE_SELECT_REG,
					YT8521_RSSR_UTP_SPACE);
	if (ret < 0)
		return ret;

	ret = genphy_read_status(phydev);
	if (ret < 0)
		return ret;

	if (phydev->autoneg_complete) {
		ret = genphy_c45_read_lpa(phydev);
		if (ret < 0)
			return ret;
	}

	ret = phy_read(phydev, YTPHY_SPECIFIC_STATUS_REG);
	if (ret < 0)
		return ret;

	val = ret;

	link = val & YTPHY_SSR_LINK;
	if (link)
		yt8821_adjust_status(phydev, val);

	if (link) {
		if (phydev->link == 0)
			phydev_dbg(phydev,
				   "%s, phy addr: %d, link up\n",
				   __func__, phydev->mdio.addr);
		phydev->link = 1;
	} else {
		if (phydev->link == 1)
			phydev_dbg(phydev,
				   "%s, phy addr: %d, link down\n",
				   __func__, phydev->mdio.addr);
		phydev->link = 0;
	}

	val = ytphy_read_ext_with_lock(phydev, YT8521_CHIP_CONFIG_REG);
	if (val < 0)
		return val;

	if (FIELD_GET(YT8521_CCR_MODE_SEL_MASK, val) ==
	    YT8821_CHIP_MODE_AUTO_BX2500_SGMII)
		yt8821_update_interface(phydev);

	return 0;
}

/**
 * yt8821_modify_utp_fiber_bmcr - bits modify a PHY's BMCR register
 * @phydev: the phy_device struct
 * @mask: bit mask of bits to clear
 * @set: bit mask of bits to set
 *
 * NOTE: Convenience function which allows a PHY's BMCR register to be
 * modified as new register value = (old register value & ~mask) | set.
 *
 * Returns: 0 or negative errno code
 */
static int yt8821_modify_utp_fiber_bmcr(struct phy_device *phydev,
					u16 mask, u16 set)
{
	int ret;

	ret = yt8521_modify_bmcr_paged(phydev, YT8521_RSSR_UTP_SPACE,
				       mask, set);
	if (ret < 0)
		return ret;

	return yt8521_modify_bmcr_paged(phydev, YT8521_RSSR_FIBER_SPACE,
					mask, set);
}

/**
 * yt8821_suspend() - suspend the hardware
 * @phydev: a pointer to a &struct phy_device
 *
 * Returns: 0 or negative errno code
 */
static int yt8821_suspend(struct phy_device *phydev)
{
	int wol_config;

	wol_config = ytphy_read_ext_with_lock(phydev,
					      YTPHY_WOL_CONFIG_REG);
	if (wol_config < 0)
		return wol_config;

	/* if wol enable, do nothing */
	if (wol_config & YTPHY_WCR_ENABLE)
		return 0;

	return yt8821_modify_utp_fiber_bmcr(phydev, 0, BMCR_PDOWN);
}

/**
 * yt8821_resume() - resume the hardware
 * @phydev: a pointer to a &struct phy_device
 *
 * Returns: 0 or negative errno code
 */
static int yt8821_resume(struct phy_device *phydev)
{
	int wol_config;
	int ret;

	/* disable auto sleep */
	ret = yt8821_auto_sleep_config(phydev, false);
	if (ret < 0)
		return ret;

	wol_config = ytphy_read_ext_with_lock(phydev,
					      YTPHY_WOL_CONFIG_REG);
	if (wol_config < 0)
		return wol_config;

	/* if wol enable, do nothing */
	if (wol_config & YTPHY_WCR_ENABLE)
		return 0;

	return yt8821_modify_utp_fiber_bmcr(phydev, BMCR_PDOWN, 0);
}

static struct phy_driver motorcomm_phy_drvs[] = {
	{
		PHY_ID_MATCH_EXACT(PHY_ID_YT8511),
		.name		= "YT8511 Gigabit Ethernet",
		.config_init	= yt8511_config_init,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.read_page	= yt8511_read_page,
		.write_page	= yt8511_write_page,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_YT8521),
		.name		= "YT8521 Gigabit Ethernet",
		.get_features	= yt8521_get_features,
		.probe		= yt8521_probe,
		.read_page	= yt8521_read_page,
		.write_page	= yt8521_write_page,
		.get_wol	= ytphy_get_wol,
		.set_wol	= ytphy_set_wol,
		.config_aneg	= yt8521_config_aneg,
		.aneg_done	= yt8521_aneg_done,
		.config_init	= yt8521_config_init,
		.read_status	= yt8521_read_status,
		.soft_reset	= yt8521_soft_reset,
		.suspend	= yt8521_suspend,
		.resume		= yt8521_resume,
		.led_hw_is_supported = yt8521_led_hw_is_supported,
		.led_hw_control_set = yt8521_led_hw_control_set,
		.led_hw_control_get = yt8521_led_hw_control_get,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_YT8531),
		.name		= "YT8531 Gigabit Ethernet",
		.probe		= yt8531_probe,
		.config_init	= yt8531_config_init,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.get_wol	= ytphy_get_wol,
		.set_wol	= yt8531_set_wol,
		.link_change_notify = yt8531_link_change_notify,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_YT8531S),
		.name		= "YT8531S Gigabit Ethernet",
		.get_features	= yt8521_get_features,
		.probe		= yt8521_probe,
		.read_page	= yt8521_read_page,
		.write_page	= yt8521_write_page,
		.get_wol	= ytphy_get_wol,
		.set_wol	= ytphy_set_wol,
		.config_aneg	= yt8521_config_aneg,
		.aneg_done	= yt8521_aneg_done,
		.config_init	= yt8521_config_init,
		.read_status	= yt8521_read_status,
		.soft_reset	= yt8521_soft_reset,
		.suspend	= yt8521_suspend,
		.resume		= yt8521_resume,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_YT8821),
		.name			= "YT8821 2.5Gbps PHY",
		.get_features		= yt8821_get_features,
		.read_page		= yt8521_read_page,
		.write_page		= yt8521_write_page,
		.get_wol		= ytphy_get_wol,
		.set_wol		= ytphy_set_wol,
		.config_aneg		= genphy_config_aneg,
		.aneg_done		= yt8821_aneg_done,
		.config_init		= yt8821_config_init,
		.get_rate_matching	= yt8821_get_rate_matching,
		.read_status		= yt8821_read_status,
		.soft_reset		= yt8821_soft_reset,
		.suspend		= yt8821_suspend,
		.resume			= yt8821_resume,
	},
};

module_phy_driver(motorcomm_phy_drvs);

MODULE_DESCRIPTION("Motorcomm 8511/8521/8531/8531S/8821 PHY driver");
MODULE_AUTHOR("Peter Geis");
MODULE_AUTHOR("Frank");
MODULE_LICENSE("GPL");

static const struct mdio_device_id __maybe_unused motorcomm_tbl[] = {
	{ PHY_ID_MATCH_EXACT(PHY_ID_YT8511) },
	{ PHY_ID_MATCH_EXACT(PHY_ID_YT8521) },
	{ PHY_ID_MATCH_EXACT(PHY_ID_YT8531) },
	{ PHY_ID_MATCH_EXACT(PHY_ID_YT8531S) },
	{ PHY_ID_MATCH_EXACT(PHY_ID_YT8821) },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(mdio, motorcomm_tbl);
