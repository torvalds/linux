// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Microchip Technology

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/sort.h>
#include <linux/ethtool.h>
#include <linux/ethtool_netlink.h>
#include <linux/bitfield.h>
#include "microchip_rds_ptp.h"

#define PHY_ID_LAN87XX				0x0007c150
#define PHY_ID_LAN937X				0x0007c180
#define PHY_ID_LAN887X				0x0007c1f0

#define MCHP_RDS_PTP_LTC_BASE_ADDR		0xe000
#define MCHP_RDS_PTP_PORT_BASE_ADDR	    (MCHP_RDS_PTP_LTC_BASE_ADDR + 0x800)

/* External Register Control Register */
#define LAN87XX_EXT_REG_CTL                     (0x14)
#define LAN87XX_EXT_REG_CTL_RD_CTL              (0x1000)
#define LAN87XX_EXT_REG_CTL_WR_CTL              (0x0800)
#define LAN87XX_REG_BANK_SEL_MASK		GENMASK(10, 8)
#define LAN87XX_REG_ADDR_MASK			GENMASK(7, 0)

/* External Register Read Data Register */
#define LAN87XX_EXT_REG_RD_DATA                 (0x15)

/* External Register Write Data Register */
#define LAN87XX_EXT_REG_WR_DATA                 (0x16)

/* Interrupt Source Register */
#define LAN87XX_INTERRUPT_SOURCE                (0x18)
#define LAN87XX_INTERRUPT_SOURCE_2              (0x08)

/* Interrupt Mask Register */
#define LAN87XX_INTERRUPT_MASK                  (0x19)
#define LAN87XX_MASK_LINK_UP                    (0x0004)
#define LAN87XX_MASK_LINK_DOWN                  (0x0002)

#define LAN87XX_INTERRUPT_MASK_2                (0x09)
#define LAN87XX_MASK_COMM_RDY			BIT(10)

/* MISC Control 1 Register */
#define LAN87XX_CTRL_1                          (0x11)
#define LAN87XX_MASK_RGMII_TXC_DLY_EN           (0x4000)
#define LAN87XX_MASK_RGMII_RXC_DLY_EN           (0x2000)

/* phyaccess nested types */
#define	PHYACC_ATTR_MODE_READ		0
#define	PHYACC_ATTR_MODE_WRITE		1
#define	PHYACC_ATTR_MODE_MODIFY		2
#define	PHYACC_ATTR_MODE_POLL		3

#define	PHYACC_ATTR_BANK_SMI		0
#define	PHYACC_ATTR_BANK_MISC		1
#define	PHYACC_ATTR_BANK_PCS		2
#define	PHYACC_ATTR_BANK_AFE		3
#define	PHYACC_ATTR_BANK_DSP		4
#define	PHYACC_ATTR_BANK_MAX		7

/* measurement defines */
#define	LAN87XX_CABLE_TEST_OK		0
#define	LAN87XX_CABLE_TEST_OPEN	1
#define	LAN87XX_CABLE_TEST_SAME_SHORT	2

/* T1 Registers */
#define T1_AFE_PORT_CFG1_REG		0x0B
#define T1_POWER_DOWN_CONTROL_REG	0x1A
#define T1_SLV_FD_MULT_CFG_REG		0x18
#define T1_CDR_CFG_PRE_LOCK_REG		0x05
#define T1_CDR_CFG_POST_LOCK_REG	0x06
#define T1_LCK_STG2_MUFACT_CFG_REG	0x1A
#define T1_LCK_STG3_MUFACT_CFG_REG	0x1B
#define T1_POST_LCK_MUFACT_CFG_REG	0x1C
#define T1_TX_RX_FIFO_CFG_REG		0x02
#define T1_TX_LPF_FIR_CFG_REG		0x55
#define T1_COEF_CLK_PWR_DN_CFG		0x04
#define T1_COEF_RW_CTL_CFG		0x0D
#define T1_SQI_CONFIG_REG		0x2E
#define T1_SQI_CONFIG2_REG		0x4A
#define T1_DCQ_SQI_REG			0xC3
#define T1_DCQ_SQI_MSK			GENMASK(3, 1)
#define T1_MDIO_CONTROL2_REG		0x10
#define T1_INTERRUPT_SOURCE_REG		0x18
#define T1_INTERRUPT2_SOURCE_REG	0x08
#define T1_EQ_FD_STG1_FRZ_CFG		0x69
#define T1_EQ_FD_STG2_FRZ_CFG		0x6A
#define T1_EQ_FD_STG3_FRZ_CFG		0x6B
#define T1_EQ_FD_STG4_FRZ_CFG		0x6C
#define T1_EQ_WT_FD_LCK_FRZ_CFG		0x6D
#define T1_PST_EQ_LCK_STG1_FRZ_CFG	0x6E

#define T1_MODE_STAT_REG		0x11
#define T1_LINK_UP_MSK			BIT(0)

/* SQI defines */
#define LAN87XX_MAX_SQI			0x07

/* Chiptop registers */
#define LAN887X_PMA_EXT_ABILITY_2		0x12
#define LAN887X_PMA_EXT_ABILITY_2_1000T1	BIT(1)
#define LAN887X_PMA_EXT_ABILITY_2_100T1		BIT(0)

/* DSP 100M registers */
#define LAN887x_CDR_CONFIG1_100			0x0405
#define LAN887x_LOCK1_EQLSR_CONFIG_100		0x0411
#define LAN887x_SLV_HD_MUFAC_CONFIG_100		0x0417
#define LAN887x_PLOCK_MUFAC_CONFIG_100		0x041c
#define LAN887x_PROT_DISABLE_100		0x0425
#define LAN887x_KF_LOOP_SAT_CONFIG_100		0x0454

/* DSP 1000M registers */
#define LAN887X_LOCK1_EQLSR_CONFIG		0x0811
#define LAN887X_LOCK3_EQLSR_CONFIG		0x0813
#define LAN887X_PROT_DISABLE			0x0825
#define LAN887X_FFE_GAIN6			0x0843
#define LAN887X_FFE_GAIN7			0x0844
#define LAN887X_FFE_GAIN8			0x0845
#define LAN887X_FFE_GAIN9			0x0846
#define LAN887X_ECHO_DELAY_CONFIG		0x08ec
#define LAN887X_FFE_MAX_CONFIG			0x08ee

/* PCS 1000M registers */
#define LAN887X_SCR_CONFIG_3			0x8043
#define LAN887X_INFO_FLD_CONFIG_5		0x8048

/* T1 afe registers */
#define LAN887X_ZQCAL_CONTROL_1			0x8080
#define LAN887X_AFE_PORT_TESTBUS_CTRL2		0x8089
#define LAN887X_AFE_PORT_TESTBUS_CTRL4		0x808b
#define LAN887X_AFE_PORT_TESTBUS_CTRL6		0x808d
#define LAN887X_TX_AMPLT_1000T1_REG		0x80b0
#define LAN887X_INIT_COEFF_DFE1_100		0x0422

/* PMA registers */
#define LAN887X_DSP_PMA_CONTROL			0x810e
#define LAN887X_DSP_PMA_CONTROL_LNK_SYNC	BIT(4)

/* PCS 100M registers */
#define LAN887X_IDLE_ERR_TIMER_WIN		0x8204
#define LAN887X_IDLE_ERR_CNT_THRESH		0x8213

/* Misc registers */
#define LAN887X_REG_REG26			0x001a
#define LAN887X_REG_REG26_HW_INIT_SEQ_EN	BIT(8)

/* Mis registers */
#define LAN887X_MIS_CFG_REG0			0xa00
#define LAN887X_MIS_CFG_REG0_RCLKOUT_DIS	BIT(5)
#define LAN887X_MIS_CFG_REG0_MAC_MODE_SEL	GENMASK(1, 0)

#define LAN887X_MAC_MODE_RGMII			0x01
#define LAN887X_MAC_MODE_SGMII			0x03

#define LAN887X_MIS_DLL_CFG_REG0		0xa01
#define LAN887X_MIS_DLL_CFG_REG1		0xa02

#define LAN887X_MIS_DLL_DELAY_EN		BIT(15)
#define LAN887X_MIS_DLL_EN			BIT(0)
#define LAN887X_MIS_DLL_CONF	(LAN887X_MIS_DLL_DELAY_EN |\
				 LAN887X_MIS_DLL_EN)

#define LAN887X_MIS_CFG_REG2			0xa03
#define LAN887X_MIS_CFG_REG2_FE_LPBK_EN		BIT(2)

#define LAN887X_MIS_PKT_STAT_REG0		0xa06
#define LAN887X_MIS_PKT_STAT_REG1		0xa07
#define LAN887X_MIS_PKT_STAT_REG3		0xa09
#define LAN887X_MIS_PKT_STAT_REG4		0xa0a
#define LAN887X_MIS_PKT_STAT_REG5		0xa0b
#define LAN887X_MIS_PKT_STAT_REG6		0xa0c

/* Chiptop common registers */
#define LAN887X_COMMON_LED3_LED2		0xc05
#define LAN887X_COMMON_LED2_MODE_SEL_MASK	GENMASK(4, 0)
#define LAN887X_LED_LINK_ACT_ANY_SPEED		0x0

/* MX chip top registers */
#define LAN887X_CHIP_HARD_RST			0xf03e
#define LAN887X_CHIP_HARD_RST_RESET		BIT(0)

#define LAN887X_CHIP_SOFT_RST			0xf03f
#define LAN887X_CHIP_SOFT_RST_RESET		BIT(0)

#define LAN887X_SGMII_CTL			0xf01a
#define LAN887X_SGMII_CTL_SGMII_MUX_EN		BIT(0)

#define LAN887X_SGMII_PCS_CFG			0xf034
#define LAN887X_SGMII_PCS_CFG_PCS_ENA		BIT(9)

#define LAN887X_EFUSE_READ_DAT9			0xf209
#define LAN887X_EFUSE_READ_DAT9_SGMII_DIS	BIT(9)
#define LAN887X_EFUSE_READ_DAT9_MAC_MODE	GENMASK(1, 0)

#define LAN887X_CALIB_CONFIG_100		0x437
#define LAN887X_CALIB_CONFIG_100_CBL_DIAG_USE_LOCAL_SMPL	BIT(5)
#define LAN887X_CALIB_CONFIG_100_CBL_DIAG_STB_SYNC_MODE		BIT(4)
#define LAN887X_CALIB_CONFIG_100_CBL_DIAG_CLK_ALGN_MODE		BIT(3)
#define LAN887X_CALIB_CONFIG_100_VAL \
	(LAN887X_CALIB_CONFIG_100_CBL_DIAG_CLK_ALGN_MODE |\
	LAN887X_CALIB_CONFIG_100_CBL_DIAG_STB_SYNC_MODE |\
	LAN887X_CALIB_CONFIG_100_CBL_DIAG_USE_LOCAL_SMPL)

#define LAN887X_MAX_PGA_GAIN_100		0x44f
#define LAN887X_MIN_PGA_GAIN_100		0x450
#define LAN887X_START_CBL_DIAG_100		0x45a
#define LAN887X_CBL_DIAG_DONE			BIT(1)
#define LAN887X_CBL_DIAG_START			BIT(0)
#define LAN887X_CBL_DIAG_STOP			0x0

#define LAN887X_CBL_DIAG_TDR_THRESH_100		0x45b
#define LAN887X_CBL_DIAG_AGC_THRESH_100		0x45c
#define LAN887X_CBL_DIAG_MIN_WAIT_CONFIG_100	0x45d
#define LAN887X_CBL_DIAG_MAX_WAIT_CONFIG_100	0x45e
#define LAN887X_CBL_DIAG_CYC_CONFIG_100		0x45f
#define LAN887X_CBL_DIAG_TX_PULSE_CONFIG_100	0x460
#define LAN887X_CBL_DIAG_MIN_PGA_GAIN_100	0x462
#define LAN887X_CBL_DIAG_AGC_GAIN_100		0x497
#define LAN887X_CBL_DIAG_POS_PEAK_VALUE_100	0x499
#define LAN887X_CBL_DIAG_NEG_PEAK_VALUE_100	0x49a
#define LAN887X_CBL_DIAG_POS_PEAK_TIME_100	0x49c
#define LAN887X_CBL_DIAG_NEG_PEAK_TIME_100	0x49d

#define MICROCHIP_CABLE_NOISE_MARGIN		20
#define MICROCHIP_CABLE_TIME_MARGIN		89
#define MICROCHIP_CABLE_MIN_TIME_DIFF		96
#define MICROCHIP_CABLE_MAX_TIME_DIFF	\
	(MICROCHIP_CABLE_MIN_TIME_DIFF + MICROCHIP_CABLE_TIME_MARGIN)

#define LAN887X_INT_STS				0xf000
#define LAN887X_INT_MSK				0xf001
#define LAN887X_INT_MSK_P1588_MOD_INT_MSK	BIT(3)
#define LAN887X_INT_MSK_T1_PHY_INT_MSK		BIT(2)
#define LAN887X_INT_MSK_LINK_UP_MSK		BIT(1)
#define LAN887X_INT_MSK_LINK_DOWN_MSK		BIT(0)

#define LAN887X_MX_CHIP_TOP_REG_CONTROL1	0xF002
#define LAN887X_MX_CHIP_TOP_REG_CONTROL1_EVT_EN	BIT(8)

#define LAN887X_MX_CHIP_TOP_LINK_MSK	(LAN887X_INT_MSK_LINK_UP_MSK |\
					 LAN887X_INT_MSK_LINK_DOWN_MSK)

#define LAN887X_MX_CHIP_TOP_ALL_MSK	(LAN887X_INT_MSK_T1_PHY_INT_MSK |\
					 LAN887X_MX_CHIP_TOP_LINK_MSK)

#define LAN887X_COEFF_PWR_DN_CONFIG_100		0x0404
#define LAN887X_COEFF_PWR_DN_CONFIG_100_V	0x16d6
#define LAN887X_SQI_CONFIG_100			0x042e
#define LAN887X_SQI_CONFIG_100_V		0x9572
#define LAN887X_SQI_MSE_100			0x483

#define LAN887X_POKE_PEEK_100			0x040d
#define LAN887X_POKE_PEEK_100_EN		BIT(0)

#define LAN887X_COEFF_MOD_CONFIG		0x080d
#define LAN887X_COEFF_MOD_CONFIG_DCQ_COEFF_EN	BIT(8)

#define LAN887X_DCQ_SQI_STATUS			0x08b2

/* SQI raw samples count */
#define SQI_SAMPLES 200

/* Samples percentage considered for SQI calculation */
#define SQI_INLINERS_PERCENT 60

/* Samples count considered for SQI calculation */
#define SQI_INLIERS_NUM (SQI_SAMPLES * SQI_INLINERS_PERCENT / 100)

/* Start offset of samples */
#define SQI_INLIERS_START ((SQI_SAMPLES - SQI_INLIERS_NUM) / 2)

/* End offset of samples */
#define SQI_INLIERS_END (SQI_INLIERS_START + SQI_INLIERS_NUM)

#define DRIVER_AUTHOR	"Nisar Sayed <nisar.sayed@microchip.com>"
#define DRIVER_DESC	"Microchip LAN87XX/LAN937x/LAN887x T1 PHY driver"

/* TEST_MODE_NORMAL: Non-hybrid results to calculate cable status(open/short/ok)
 * TEST_MODE_HYBRID: Hybrid results to calculate distance to fault
 */
enum cable_diag_mode {
	TEST_MODE_NORMAL,
	TEST_MODE_HYBRID
};

/* CD_TEST_INIT: Cable test is initated
 * CD_TEST_DONE: Cable test is done
 */
enum cable_diag_state {
	CD_TEST_INIT,
	CD_TEST_DONE
};

struct access_ereg_val {
	u8  mode;
	u8  bank;
	u8  offset;
	u16 val;
	u16 mask;
};

struct lan887x_hw_stat {
	const char *string;
	u8 mmd;
	u16 reg;
	u8 bits;
};

static const struct lan887x_hw_stat lan887x_hw_stats[] = {
	{ "TX Good Count",                      MDIO_MMD_VEND1, LAN887X_MIS_PKT_STAT_REG0, 14},
	{ "RX Good Count",                      MDIO_MMD_VEND1, LAN887X_MIS_PKT_STAT_REG1, 14},
	{ "RX ERR Count detected by PCS",       MDIO_MMD_VEND1, LAN887X_MIS_PKT_STAT_REG3, 16},
	{ "TX CRC ERR Count",                   MDIO_MMD_VEND1, LAN887X_MIS_PKT_STAT_REG4, 8},
	{ "RX CRC ERR Count",                   MDIO_MMD_VEND1, LAN887X_MIS_PKT_STAT_REG5, 8},
	{ "RX ERR Count for SGMII MII2GMII",    MDIO_MMD_VEND1, LAN887X_MIS_PKT_STAT_REG6, 8},
};

struct lan887x_regwr_map {
	u8  mmd;
	u16 reg;
	u16 val;
};

struct lan887x_priv {
	u64 stats[ARRAY_SIZE(lan887x_hw_stats)];
	struct mchp_rds_ptp_clock *clock;
	bool init_done;
};

static int lan937x_dsp_workaround(struct phy_device *phydev, u16 ereg, u8 bank)
{
	u8 prev_bank;
	int rc = 0;
	u16 val;

	mutex_lock(&phydev->lock);
	/* Read previous selected bank */
	rc = phy_read(phydev, LAN87XX_EXT_REG_CTL);
	if (rc < 0)
		goto out_unlock;

	/* store the prev_bank */
	prev_bank = FIELD_GET(LAN87XX_REG_BANK_SEL_MASK, rc);

	if (bank != prev_bank && bank == PHYACC_ATTR_BANK_DSP) {
		val = ereg & ~LAN87XX_REG_ADDR_MASK;

		val &= ~LAN87XX_EXT_REG_CTL_WR_CTL;
		val |= LAN87XX_EXT_REG_CTL_RD_CTL;

		/* access twice for DSP bank change,dummy access */
		rc = phy_write(phydev, LAN87XX_EXT_REG_CTL, val);
	}

out_unlock:
	mutex_unlock(&phydev->lock);

	return rc;
}

static int access_ereg(struct phy_device *phydev, u8 mode, u8 bank,
		       u8 offset, u16 val)
{
	u16 ereg = 0;
	int rc = 0;

	if (mode > PHYACC_ATTR_MODE_WRITE || bank > PHYACC_ATTR_BANK_MAX)
		return -EINVAL;

	if (bank == PHYACC_ATTR_BANK_SMI) {
		if (mode == PHYACC_ATTR_MODE_WRITE)
			rc = phy_write(phydev, offset, val);
		else
			rc = phy_read(phydev, offset);
		return rc;
	}

	if (mode == PHYACC_ATTR_MODE_WRITE) {
		ereg = LAN87XX_EXT_REG_CTL_WR_CTL;
		rc = phy_write(phydev, LAN87XX_EXT_REG_WR_DATA, val);
		if (rc < 0)
			return rc;
	} else {
		ereg = LAN87XX_EXT_REG_CTL_RD_CTL;
	}

	ereg |= (bank << 8) | offset;

	/* DSP bank access workaround for lan937x */
	if (phydev->phy_id == PHY_ID_LAN937X) {
		rc = lan937x_dsp_workaround(phydev, ereg, bank);
		if (rc < 0)
			return rc;
	}

	rc = phy_write(phydev, LAN87XX_EXT_REG_CTL, ereg);
	if (rc < 0)
		return rc;

	if (mode == PHYACC_ATTR_MODE_READ)
		rc = phy_read(phydev, LAN87XX_EXT_REG_RD_DATA);

	return rc;
}

static int access_ereg_modify_changed(struct phy_device *phydev,
				      u8 bank, u8 offset, u16 val, u16 mask)
{
	int new = 0, rc = 0;

	if (bank > PHYACC_ATTR_BANK_MAX)
		return -EINVAL;

	rc = access_ereg(phydev, PHYACC_ATTR_MODE_READ, bank, offset, val);
	if (rc < 0)
		return rc;

	new = val | (rc & (mask ^ 0xFFFF));
	rc = access_ereg(phydev, PHYACC_ATTR_MODE_WRITE, bank, offset, new);

	return rc;
}

static int access_smi_poll_timeout(struct phy_device *phydev,
				   u8 offset, u16 mask, u16 clr)
{
	int val;

	return phy_read_poll_timeout(phydev, offset, val, (val & mask) == clr,
				     150, 30000, true);
}

static int lan87xx_config_rgmii_delay(struct phy_device *phydev)
{
	int rc;

	if (!phy_interface_is_rgmii(phydev))
		return 0;

	rc = access_ereg(phydev, PHYACC_ATTR_MODE_READ,
			 PHYACC_ATTR_BANK_MISC, LAN87XX_CTRL_1, 0);
	if (rc < 0)
		return rc;

	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		rc &= ~LAN87XX_MASK_RGMII_TXC_DLY_EN;
		rc &= ~LAN87XX_MASK_RGMII_RXC_DLY_EN;
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		rc |= LAN87XX_MASK_RGMII_TXC_DLY_EN;
		rc |= LAN87XX_MASK_RGMII_RXC_DLY_EN;
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		rc &= ~LAN87XX_MASK_RGMII_TXC_DLY_EN;
		rc |= LAN87XX_MASK_RGMII_RXC_DLY_EN;
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		rc |= LAN87XX_MASK_RGMII_TXC_DLY_EN;
		rc &= ~LAN87XX_MASK_RGMII_RXC_DLY_EN;
		break;
	default:
		return 0;
	}

	return access_ereg(phydev, PHYACC_ATTR_MODE_WRITE,
			   PHYACC_ATTR_BANK_MISC, LAN87XX_CTRL_1, rc);
}

static int lan87xx_phy_init_cmd(struct phy_device *phydev,
				const struct access_ereg_val *cmd_seq, int cnt)
{
	int ret, i;

	for (i = 0; i < cnt; i++) {
		if (cmd_seq[i].mode == PHYACC_ATTR_MODE_POLL &&
		    cmd_seq[i].bank == PHYACC_ATTR_BANK_SMI) {
			ret = access_smi_poll_timeout(phydev,
						      cmd_seq[i].offset,
						      cmd_seq[i].val,
						      cmd_seq[i].mask);
		} else {
			ret = access_ereg(phydev, cmd_seq[i].mode,
					  cmd_seq[i].bank, cmd_seq[i].offset,
					  cmd_seq[i].val);
		}
		if (ret < 0)
			return ret;
	}

	return ret;
}

static int lan87xx_phy_init(struct phy_device *phydev)
{
	static const struct access_ereg_val hw_init[] = {
		/* TXPD/TXAMP6 Configs */
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_AFE,
		  T1_AFE_PORT_CFG1_REG,       0x002D,  0 },
		/* HW_Init Hi and Force_ED */
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_SMI,
		  T1_POWER_DOWN_CONTROL_REG,  0x0308,  0 },
	};

	static const struct access_ereg_val slave_init[] = {
		/* Equalizer Full Duplex Freeze - T1 Slave */
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_EQ_FD_STG1_FRZ_CFG,     0x0002,  0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_EQ_FD_STG2_FRZ_CFG,     0x0002,  0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_EQ_FD_STG3_FRZ_CFG,     0x0002,  0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_EQ_FD_STG4_FRZ_CFG,     0x0002,  0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_EQ_WT_FD_LCK_FRZ_CFG,    0x0002,  0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_PST_EQ_LCK_STG1_FRZ_CFG, 0x0002,  0 },
	};

	static const struct access_ereg_val phy_init[] = {
		/* Slave Full Duplex Multi Configs */
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_SLV_FD_MULT_CFG_REG,     0x0D53,  0 },
		/* CDR Pre and Post Lock Configs */
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_CDR_CFG_PRE_LOCK_REG,    0x0AB2,  0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_CDR_CFG_POST_LOCK_REG,   0x0AB3,  0 },
		/* Lock Stage 2-3 Multi Factor Config */
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_LCK_STG2_MUFACT_CFG_REG, 0x0AEA,  0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_LCK_STG3_MUFACT_CFG_REG, 0x0AEB,  0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_POST_LCK_MUFACT_CFG_REG, 0x0AEB,  0 },
		/* Pointer delay */
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_RX_FIFO_CFG_REG, 0x1C00, 0 },
		/* Tx iir edits */
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x1000, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x1861, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x1061, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x1922, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x1122, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x1983, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x1183, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x1944, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x1144, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x18c5, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x10c5, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x1846, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x1046, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x1807, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x1007, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x1808, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x1008, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x1809, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x1009, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x180A, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x100A, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x180B, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x100B, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x180C, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x100C, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x180D, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x100D, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x180E, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x100E, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x180F, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x100F, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x1810, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x1010, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x1811, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x1011, 0 },
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_TX_LPF_FIR_CFG_REG, 0x1000, 0 },
		/* Setup SQI measurement */
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_COEF_CLK_PWR_DN_CFG,	0x16d6, 0 },
		/* SQI enable */
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_SQI_CONFIG_REG,		0x9572, 0 },
		/* SQI select mode 5 */
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_SQI_CONFIG2_REG,		0x0001, 0 },
		/* Throws the first SQI reading */
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP,
		  T1_COEF_RW_CTL_CFG,		0x0301,	0 },
		{ PHYACC_ATTR_MODE_READ, PHYACC_ATTR_BANK_DSP,
		  T1_DCQ_SQI_REG,		0,	0 },
		/* Flag LPS and WUR as idle errors */
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_SMI,
		  T1_MDIO_CONTROL2_REG,		0x0014, 0 },
		/* HW_Init toggle, undo force ED, TXPD off */
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_SMI,
		  T1_POWER_DOWN_CONTROL_REG,	0x0200, 0 },
		/* Reset PCS to trigger hardware initialization */
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_SMI,
		  T1_MDIO_CONTROL2_REG,		0x0094, 0 },
		/* Poll till Hardware is initialized */
		{ PHYACC_ATTR_MODE_POLL, PHYACC_ATTR_BANK_SMI,
		  T1_MDIO_CONTROL2_REG,		0x0080, 0 },
		/* Tx AMP - 0x06  */
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_AFE,
		  T1_AFE_PORT_CFG1_REG,		0x000C, 0 },
		/* Read INTERRUPT_SOURCE Register */
		{ PHYACC_ATTR_MODE_READ, PHYACC_ATTR_BANK_SMI,
		  T1_INTERRUPT_SOURCE_REG,	0,	0 },
		/* Read INTERRUPT_SOURCE Register */
		{ PHYACC_ATTR_MODE_READ, PHYACC_ATTR_BANK_MISC,
		  T1_INTERRUPT2_SOURCE_REG,	0,	0 },
		/* HW_Init Hi */
		{ PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_SMI,
		  T1_POWER_DOWN_CONTROL_REG,	0x0300, 0 },
	};
	int rc;

	/* phy Soft reset */
	rc = genphy_soft_reset(phydev);
	if (rc < 0)
		return rc;

	/* PHY Initialization */
	rc = lan87xx_phy_init_cmd(phydev, hw_init, ARRAY_SIZE(hw_init));
	if (rc < 0)
		return rc;

	rc = genphy_read_master_slave(phydev);
	if (rc)
		return rc;

	/* The following squence needs to run only if phydev is in
	 * slave mode.
	 */
	if (phydev->master_slave_state == MASTER_SLAVE_STATE_SLAVE) {
		rc = lan87xx_phy_init_cmd(phydev, slave_init,
					  ARRAY_SIZE(slave_init));
		if (rc < 0)
			return rc;
	}

	rc = lan87xx_phy_init_cmd(phydev, phy_init, ARRAY_SIZE(phy_init));
	if (rc < 0)
		return rc;

	return lan87xx_config_rgmii_delay(phydev);
}

static int lan87xx_phy_config_intr(struct phy_device *phydev)
{
	int rc, val = 0;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		/* clear all interrupt */
		rc = phy_write(phydev, LAN87XX_INTERRUPT_MASK, val);
		if (rc < 0)
			return rc;

		rc = phy_read(phydev, LAN87XX_INTERRUPT_SOURCE);
		if (rc < 0)
			return rc;

		rc = access_ereg(phydev, PHYACC_ATTR_MODE_WRITE,
				 PHYACC_ATTR_BANK_MISC,
				 LAN87XX_INTERRUPT_MASK_2, val);
		if (rc < 0)
			return rc;

		rc = access_ereg(phydev, PHYACC_ATTR_MODE_READ,
				 PHYACC_ATTR_BANK_MISC,
				 LAN87XX_INTERRUPT_SOURCE_2, 0);
		if (rc < 0)
			return rc;

		/* enable link down and comm ready interrupt */
		val = LAN87XX_MASK_LINK_DOWN;
		rc = phy_write(phydev, LAN87XX_INTERRUPT_MASK, val);
		if (rc < 0)
			return rc;

		val = LAN87XX_MASK_COMM_RDY;
		rc = access_ereg(phydev, PHYACC_ATTR_MODE_WRITE,
				 PHYACC_ATTR_BANK_MISC,
				 LAN87XX_INTERRUPT_MASK_2, val);
	} else {
		rc = phy_write(phydev, LAN87XX_INTERRUPT_MASK, val);
		if (rc < 0)
			return rc;

		rc = phy_read(phydev, LAN87XX_INTERRUPT_SOURCE);
		if (rc < 0)
			return rc;

		rc = access_ereg(phydev, PHYACC_ATTR_MODE_WRITE,
				 PHYACC_ATTR_BANK_MISC,
				 LAN87XX_INTERRUPT_MASK_2, val);
		if (rc < 0)
			return rc;

		rc = access_ereg(phydev, PHYACC_ATTR_MODE_READ,
				 PHYACC_ATTR_BANK_MISC,
				 LAN87XX_INTERRUPT_SOURCE_2, 0);
	}

	return rc < 0 ? rc : 0;
}

static irqreturn_t lan87xx_handle_interrupt(struct phy_device *phydev)
{
	int irq_status;

	irq_status  = access_ereg(phydev, PHYACC_ATTR_MODE_READ,
				  PHYACC_ATTR_BANK_MISC,
				  LAN87XX_INTERRUPT_SOURCE_2, 0);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	irq_status = phy_read(phydev, LAN87XX_INTERRUPT_SOURCE);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if (irq_status == 0)
		return IRQ_NONE;

	phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}

static int lan87xx_config_init(struct phy_device *phydev)
{
	int rc = lan87xx_phy_init(phydev);

	return rc < 0 ? rc : 0;
}

static int microchip_cable_test_start_common(struct phy_device *phydev)
{
	int bmcr, bmsr, ret;

	/* If auto-negotiation is enabled, but not complete, the cable
	 * test never completes. So disable auto-neg.
	 */
	bmcr = phy_read(phydev, MII_BMCR);
	if (bmcr < 0)
		return bmcr;

	bmsr = phy_read(phydev, MII_BMSR);

	if (bmsr < 0)
		return bmsr;

	if (bmcr & BMCR_ANENABLE) {
		ret =  phy_modify(phydev, MII_BMCR, BMCR_ANENABLE, 0);
		if (ret < 0)
			return ret;
		ret = genphy_soft_reset(phydev);
		if (ret < 0)
			return ret;
	}

	/* If the link is up, allow it some time to go down */
	if (bmsr & BMSR_LSTATUS)
		msleep(1500);

	return 0;
}

static int lan87xx_cable_test_start(struct phy_device *phydev)
{
	static const struct access_ereg_val cable_test[] = {
		/* min wait */
		{PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP, 93,
		 0, 0},
		/* max wait */
		{PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP, 94,
		 10, 0},
		/* pulse cycle */
		{PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP, 95,
		 90, 0},
		/* cable diag thresh */
		{PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP, 92,
		 60, 0},
		/* max gain */
		{PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP, 79,
		 31, 0},
		/* clock align for each iteration */
		{PHYACC_ATTR_MODE_MODIFY, PHYACC_ATTR_BANK_DSP, 55,
		 0, 0x0038},
		/* max cycle wait config */
		{PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP, 94,
		 70, 0},
		/* start cable diag*/
		{PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_DSP, 90,
		 1, 0},
	};
	int rc, i;

	rc = microchip_cable_test_start_common(phydev);
	if (rc < 0)
		return rc;

	/* start cable diag */
	/* check if part is alive - if not, return diagnostic error */
	rc = access_ereg(phydev, PHYACC_ATTR_MODE_READ, PHYACC_ATTR_BANK_SMI,
			 0x00, 0);
	if (rc < 0)
		return rc;

	/* master/slave specific configs */
	rc = access_ereg(phydev, PHYACC_ATTR_MODE_READ, PHYACC_ATTR_BANK_SMI,
			 0x0A, 0);
	if (rc < 0)
		return rc;

	if ((rc & 0x4000) != 0x4000) {
		/* DUT is Slave */
		rc = access_ereg_modify_changed(phydev, PHYACC_ATTR_BANK_AFE,
						0x0E, 0x5, 0x7);
		if (rc < 0)
			return rc;
		rc = access_ereg_modify_changed(phydev, PHYACC_ATTR_BANK_SMI,
						0x1A, 0x8, 0x8);
		if (rc < 0)
			return rc;
	} else {
		/* DUT is Master */
		rc = access_ereg_modify_changed(phydev, PHYACC_ATTR_BANK_SMI,
						0x10, 0x8, 0x40);
		if (rc < 0)
			return rc;
	}

	for (i = 0; i < ARRAY_SIZE(cable_test); i++) {
		if (cable_test[i].mode == PHYACC_ATTR_MODE_MODIFY) {
			rc = access_ereg_modify_changed(phydev,
							cable_test[i].bank,
							cable_test[i].offset,
							cable_test[i].val,
							cable_test[i].mask);
			/* wait 50ms */
			msleep(50);
		} else {
			rc = access_ereg(phydev, cable_test[i].mode,
					 cable_test[i].bank,
					 cable_test[i].offset,
					 cable_test[i].val);
		}
		if (rc < 0)
			return rc;
	}
	/* cable diag started */

	return 0;
}

static int lan87xx_cable_test_report_trans(u32 result)
{
	switch (result) {
	case LAN87XX_CABLE_TEST_OK:
		return ETHTOOL_A_CABLE_RESULT_CODE_OK;
	case LAN87XX_CABLE_TEST_OPEN:
		return ETHTOOL_A_CABLE_RESULT_CODE_OPEN;
	case LAN87XX_CABLE_TEST_SAME_SHORT:
		return ETHTOOL_A_CABLE_RESULT_CODE_SAME_SHORT;
	default:
		/* DIAGNOSTIC_ERROR */
		return ETHTOOL_A_CABLE_RESULT_CODE_UNSPEC;
	}
}

static int lan87xx_cable_test_report(struct phy_device *phydev)
{
	int pos_peak_cycle = 0, pos_peak_in_phases = 0, pos_peak_phase = 0;
	int neg_peak_cycle = 0, neg_peak_in_phases = 0, neg_peak_phase = 0;
	int noise_margin = 20, time_margin = 89, jitter_var = 30;
	int min_time_diff = 96, max_time_diff = 96 + time_margin;
	bool fault = false, check_a = false, check_b = false;
	int gain_idx = 0, pos_peak = 0, neg_peak = 0;
	int pos_peak_time = 0, neg_peak_time = 0;
	int pos_peak_in_phases_hybrid = 0;
	int detect = -1;

	gain_idx = access_ereg(phydev, PHYACC_ATTR_MODE_READ,
			       PHYACC_ATTR_BANK_DSP, 151, 0);
	/* read non-hybrid results */
	pos_peak = access_ereg(phydev, PHYACC_ATTR_MODE_READ,
			       PHYACC_ATTR_BANK_DSP, 153, 0);
	neg_peak = access_ereg(phydev, PHYACC_ATTR_MODE_READ,
			       PHYACC_ATTR_BANK_DSP, 154, 0);
	pos_peak_time = access_ereg(phydev, PHYACC_ATTR_MODE_READ,
				    PHYACC_ATTR_BANK_DSP, 156, 0);
	neg_peak_time = access_ereg(phydev, PHYACC_ATTR_MODE_READ,
				    PHYACC_ATTR_BANK_DSP, 157, 0);

	pos_peak_cycle = (pos_peak_time >> 7) & 0x7F;
	/* calculate non-hybrid values */
	pos_peak_phase = pos_peak_time & 0x7F;
	pos_peak_in_phases = (pos_peak_cycle * 96) + pos_peak_phase;
	neg_peak_cycle = (neg_peak_time >> 7) & 0x7F;
	neg_peak_phase = neg_peak_time & 0x7F;
	neg_peak_in_phases = (neg_peak_cycle * 96) + neg_peak_phase;

	/* process values */
	check_a =
		((pos_peak_in_phases - neg_peak_in_phases) >= min_time_diff) &&
		((pos_peak_in_phases - neg_peak_in_phases) < max_time_diff) &&
		pos_peak_in_phases_hybrid < pos_peak_in_phases &&
		(pos_peak_in_phases_hybrid < (neg_peak_in_phases + jitter_var));
	check_b =
		((neg_peak_in_phases - pos_peak_in_phases) >= min_time_diff) &&
		((neg_peak_in_phases - pos_peak_in_phases) < max_time_diff) &&
		pos_peak_in_phases_hybrid < neg_peak_in_phases &&
		(pos_peak_in_phases_hybrid < (pos_peak_in_phases + jitter_var));

	if (pos_peak_in_phases > neg_peak_in_phases && check_a)
		detect = 2;
	else if ((neg_peak_in_phases > pos_peak_in_phases) && check_b)
		detect = 1;

	if (pos_peak > noise_margin && neg_peak > noise_margin &&
	    gain_idx >= 0) {
		if (detect == 1 || detect == 2)
			fault = true;
	}

	if (!fault)
		detect = 0;

	ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_A,
				lan87xx_cable_test_report_trans(detect));

	return phy_init_hw(phydev);
}

static int lan87xx_cable_test_get_status(struct phy_device *phydev,
					 bool *finished)
{
	int rc = 0;

	*finished = false;

	/* check if cable diag was finished */
	rc = access_ereg(phydev, PHYACC_ATTR_MODE_READ, PHYACC_ATTR_BANK_DSP,
			 90, 0);
	if (rc < 0)
		return rc;

	if ((rc & 2) == 2) {
		/* stop cable diag*/
		rc = access_ereg(phydev, PHYACC_ATTR_MODE_WRITE,
				 PHYACC_ATTR_BANK_DSP,
				 90, 0);
		if (rc < 0)
			return rc;

		*finished = true;

		return lan87xx_cable_test_report(phydev);
	}

	return 0;
}

static int lan87xx_read_status(struct phy_device *phydev)
{
	int rc = 0;

	rc = phy_read(phydev, T1_MODE_STAT_REG);
	if (rc < 0)
		return rc;

	if (rc & T1_LINK_UP_MSK)
		phydev->link = 1;
	else
		phydev->link = 0;

	phydev->speed = SPEED_UNKNOWN;
	phydev->duplex = DUPLEX_UNKNOWN;
	phydev->pause = 0;
	phydev->asym_pause = 0;

	rc = genphy_read_master_slave(phydev);
	if (rc < 0)
		return rc;

	rc = genphy_read_status_fixed(phydev);
	if (rc < 0)
		return rc;

	return rc;
}

static int lan87xx_config_aneg(struct phy_device *phydev)
{
	u16 ctl = 0;
	int ret;

	switch (phydev->master_slave_set) {
	case MASTER_SLAVE_CFG_MASTER_FORCE:
		ctl |= CTL1000_AS_MASTER;
		break;
	case MASTER_SLAVE_CFG_SLAVE_FORCE:
		break;
	case MASTER_SLAVE_CFG_UNKNOWN:
	case MASTER_SLAVE_CFG_UNSUPPORTED:
		return 0;
	default:
		phydev_warn(phydev, "Unsupported Master/Slave mode\n");
		return -EOPNOTSUPP;
	}

	ret = phy_modify_changed(phydev, MII_CTRL1000, CTL1000_AS_MASTER, ctl);
	if (ret == 1)
		return phy_init_hw(phydev);

	return ret;
}

static int lan87xx_get_sqi(struct phy_device *phydev)
{
	u8 sqi_value = 0;
	int rc;

	rc = access_ereg(phydev, PHYACC_ATTR_MODE_WRITE,
			 PHYACC_ATTR_BANK_DSP, T1_COEF_RW_CTL_CFG, 0x0301);
	if (rc < 0)
		return rc;

	rc = access_ereg(phydev, PHYACC_ATTR_MODE_READ,
			 PHYACC_ATTR_BANK_DSP, T1_DCQ_SQI_REG, 0x0);
	if (rc < 0)
		return rc;

	sqi_value = FIELD_GET(T1_DCQ_SQI_MSK, rc);

	return sqi_value;
}

static int lan87xx_get_sqi_max(struct phy_device *phydev)
{
	return LAN87XX_MAX_SQI;
}

static int lan887x_rgmii_init(struct phy_device *phydev)
{
	int ret;

	/* SGMII mux disable */
	ret = phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1,
				 LAN887X_SGMII_CTL,
				 LAN887X_SGMII_CTL_SGMII_MUX_EN);
	if (ret < 0)
		return ret;

	/* Select MAC_MODE as RGMII */
	ret = phy_modify_mmd(phydev, MDIO_MMD_VEND1, LAN887X_MIS_CFG_REG0,
			     LAN887X_MIS_CFG_REG0_MAC_MODE_SEL,
			     LAN887X_MAC_MODE_RGMII);
	if (ret < 0)
		return ret;

	/* Disable PCS */
	ret = phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1,
				 LAN887X_SGMII_PCS_CFG,
				 LAN887X_SGMII_PCS_CFG_PCS_ENA);
	if (ret < 0)
		return ret;

	/* LAN887x Errata: RGMII rx clock active in SGMII mode
	 * Disabled it for SGMII mode
	 * Re-enabling it for RGMII mode
	 */
	return phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1,
				  LAN887X_MIS_CFG_REG0,
				  LAN887X_MIS_CFG_REG0_RCLKOUT_DIS);
}

static int lan887x_sgmii_init(struct phy_device *phydev)
{
	int ret;

	/* SGMII mux enable */
	ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND1,
			       LAN887X_SGMII_CTL,
			       LAN887X_SGMII_CTL_SGMII_MUX_EN);
	if (ret < 0)
		return ret;

	/* Select MAC_MODE as SGMII */
	ret = phy_modify_mmd(phydev, MDIO_MMD_VEND1, LAN887X_MIS_CFG_REG0,
			     LAN887X_MIS_CFG_REG0_MAC_MODE_SEL,
			     LAN887X_MAC_MODE_SGMII);
	if (ret < 0)
		return ret;

	/* LAN887x Errata: RGMII rx clock active in SGMII mode.
	 * So disabling it for SGMII mode
	 */
	ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, LAN887X_MIS_CFG_REG0,
			       LAN887X_MIS_CFG_REG0_RCLKOUT_DIS);
	if (ret < 0)
		return ret;

	/* Enable PCS */
	return phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, LAN887X_SGMII_PCS_CFG,
				LAN887X_SGMII_PCS_CFG_PCS_ENA);
}

static int lan887x_config_rgmii_en(struct phy_device *phydev)
{
	int txc;
	int rxc;
	int ret;

	ret = lan887x_rgmii_init(phydev);
	if (ret < 0)
		return ret;

	/* Control bit to enable/disable TX DLL delay line in signal path */
	txc = phy_read_mmd(phydev, MDIO_MMD_VEND1, LAN887X_MIS_DLL_CFG_REG0);
	if (txc < 0)
		return txc;

	/* Control bit to enable/disable RX DLL delay line in signal path */
	rxc = phy_read_mmd(phydev, MDIO_MMD_VEND1, LAN887X_MIS_DLL_CFG_REG1);
	if (rxc < 0)
		return rxc;

	/* Configures the phy to enable RX/TX delay
	 * RGMII        - TX & RX delays are either added by MAC or not needed,
	 *                phy should not add
	 * RGMII_ID     - Configures phy to enable TX & RX delays, MAC shouldn't add
	 * RGMII_RX_ID  - Configures the PHY to enable the RX delay.
	 *                The MAC shouldn't add the RX delay
	 * RGMII_TX_ID  - Configures the PHY to enable the TX delay.
	 *                The MAC shouldn't add the TX delay in this case
	 */
	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		txc &= ~LAN887X_MIS_DLL_CONF;
		rxc &= ~LAN887X_MIS_DLL_CONF;
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		txc |= LAN887X_MIS_DLL_CONF;
		rxc |= LAN887X_MIS_DLL_CONF;
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		txc &= ~LAN887X_MIS_DLL_CONF;
		rxc |= LAN887X_MIS_DLL_CONF;
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		txc |= LAN887X_MIS_DLL_CONF;
		rxc &= ~LAN887X_MIS_DLL_CONF;
		break;
	default:
		WARN_ONCE(1, "Invalid phydev interface %d\n", phydev->interface);
		return 0;
	}

	/* Configures the PHY to enable/disable RX delay in signal path */
	ret = phy_modify_mmd(phydev, MDIO_MMD_VEND1, LAN887X_MIS_DLL_CFG_REG1,
			     LAN887X_MIS_DLL_CONF, rxc);
	if (ret < 0)
		return ret;

	/* Configures the PHY to enable/disable the TX delay in signal path */
	return phy_modify_mmd(phydev, MDIO_MMD_VEND1, LAN887X_MIS_DLL_CFG_REG0,
			      LAN887X_MIS_DLL_CONF, txc);
}

static int lan887x_config_phy_interface(struct phy_device *phydev)
{
	int interface_mode;
	int sgmii_dis;
	int ret;

	/* Read sku efuse data for interfaces supported by sku */
	ret = phy_read_mmd(phydev, MDIO_MMD_VEND1, LAN887X_EFUSE_READ_DAT9);
	if (ret < 0)
		return ret;

	/* If interface_mode is 1 then efuse sets RGMII operations.
	 * If interface mode is 3 then efuse sets SGMII operations.
	 */
	interface_mode = ret & LAN887X_EFUSE_READ_DAT9_MAC_MODE;
	/* SGMII disable is set for RGMII operations */
	sgmii_dis = ret & LAN887X_EFUSE_READ_DAT9_SGMII_DIS;

	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		/* Reject RGMII settings for SGMII only sku */
		ret = -EOPNOTSUPP;

		if (!((interface_mode & LAN887X_MAC_MODE_SGMII) ==
		    LAN887X_MAC_MODE_SGMII))
			ret = lan887x_config_rgmii_en(phydev);
		break;
	case PHY_INTERFACE_MODE_SGMII:
		/* Reject SGMII setting for RGMII only sku */
		ret = -EOPNOTSUPP;

		if (!sgmii_dis)
			ret = lan887x_sgmii_init(phydev);
		break;
	default:
		/* Reject setting for unsupported interfaces */
		ret = -EOPNOTSUPP;
	}

	return ret;
}

static int lan887x_get_features(struct phy_device *phydev)
{
	int ret;

	ret = genphy_c45_pma_read_abilities(phydev);
	if (ret < 0)
		return ret;

	/* Enable twisted pair */
	linkmode_set_bit(ETHTOOL_LINK_MODE_TP_BIT, phydev->supported);

	/* First patch only supports 100Mbps and 1000Mbps force-mode.
	 * T1 Auto-Negotiation (Clause 98 of IEEE 802.3) will be added later.
	 */
	linkmode_clear_bit(ETHTOOL_LINK_MODE_Autoneg_BIT, phydev->supported);

	return 0;
}

static int lan887x_phy_init(struct phy_device *phydev)
{
	struct lan887x_priv *priv = phydev->priv;
	int ret;

	if (!priv->init_done && phy_interrupt_is_valid(phydev)) {
		priv->clock = mchp_rds_ptp_probe(phydev, MDIO_MMD_VEND1,
						 MCHP_RDS_PTP_LTC_BASE_ADDR,
						 MCHP_RDS_PTP_PORT_BASE_ADDR);
		if (IS_ERR(priv->clock))
			return PTR_ERR(priv->clock);

		/* Enable pin mux for EVT */
		phy_modify_mmd(phydev, MDIO_MMD_VEND1,
			       LAN887X_MX_CHIP_TOP_REG_CONTROL1,
			       LAN887X_MX_CHIP_TOP_REG_CONTROL1_EVT_EN,
			       LAN887X_MX_CHIP_TOP_REG_CONTROL1_EVT_EN);

		/* Initialize pin numbers specific to PEROUT */
		priv->clock->event_pin = 3;

		priv->init_done = true;
	}

	/* Clear loopback */
	ret = phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1,
				 LAN887X_MIS_CFG_REG2,
				 LAN887X_MIS_CFG_REG2_FE_LPBK_EN);
	if (ret < 0)
		return ret;

	/* Configure default behavior of led to link and activity for any
	 * speed
	 */
	ret = phy_modify_mmd(phydev, MDIO_MMD_VEND1,
			     LAN887X_COMMON_LED3_LED2,
			     LAN887X_COMMON_LED2_MODE_SEL_MASK,
			     LAN887X_LED_LINK_ACT_ANY_SPEED);
	if (ret < 0)
		return ret;

	/* PHY interface setup */
	return lan887x_config_phy_interface(phydev);
}

static int lan887x_phy_config(struct phy_device *phydev,
			      const struct lan887x_regwr_map *reg_map, int cnt)
{
	int ret;

	for (int i = 0; i < cnt; i++) {
		ret = phy_write_mmd(phydev, reg_map[i].mmd,
				    reg_map[i].reg, reg_map[i].val);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int lan887x_phy_setup(struct phy_device *phydev)
{
	static const struct lan887x_regwr_map phy_cfg[] = {
		/* PORT_AFE writes */
		{MDIO_MMD_PMAPMD, LAN887X_ZQCAL_CONTROL_1, 0x4008},
		{MDIO_MMD_PMAPMD, LAN887X_AFE_PORT_TESTBUS_CTRL2, 0x0000},
		{MDIO_MMD_PMAPMD, LAN887X_AFE_PORT_TESTBUS_CTRL6, 0x0040},
		/* 100T1_PCS_VENDOR writes */
		{MDIO_MMD_PCS,	  LAN887X_IDLE_ERR_CNT_THRESH, 0x0008},
		{MDIO_MMD_PCS,	  LAN887X_IDLE_ERR_TIMER_WIN, 0x800d},
		/* 100T1 DSP writes */
		{MDIO_MMD_VEND1,  LAN887x_CDR_CONFIG1_100, 0x0ab1},
		{MDIO_MMD_VEND1,  LAN887x_LOCK1_EQLSR_CONFIG_100, 0x5274},
		{MDIO_MMD_VEND1,  LAN887x_SLV_HD_MUFAC_CONFIG_100, 0x0d74},
		{MDIO_MMD_VEND1,  LAN887x_PLOCK_MUFAC_CONFIG_100, 0x0aea},
		{MDIO_MMD_VEND1,  LAN887x_PROT_DISABLE_100, 0x0360},
		{MDIO_MMD_VEND1,  LAN887x_KF_LOOP_SAT_CONFIG_100, 0x0c30},
		/* 1000T1 DSP writes */
		{MDIO_MMD_VEND1,  LAN887X_LOCK1_EQLSR_CONFIG, 0x2a78},
		{MDIO_MMD_VEND1,  LAN887X_LOCK3_EQLSR_CONFIG, 0x1368},
		{MDIO_MMD_VEND1,  LAN887X_PROT_DISABLE, 0x1354},
		{MDIO_MMD_VEND1,  LAN887X_FFE_GAIN6, 0x3C84},
		{MDIO_MMD_VEND1,  LAN887X_FFE_GAIN7, 0x3ca5},
		{MDIO_MMD_VEND1,  LAN887X_FFE_GAIN8, 0x3ca5},
		{MDIO_MMD_VEND1,  LAN887X_FFE_GAIN9, 0x3ca5},
		{MDIO_MMD_VEND1,  LAN887X_ECHO_DELAY_CONFIG, 0x0024},
		{MDIO_MMD_VEND1,  LAN887X_FFE_MAX_CONFIG, 0x227f},
		/* 1000T1 PCS writes */
		{MDIO_MMD_PCS,    LAN887X_SCR_CONFIG_3, 0x1e00},
		{MDIO_MMD_PCS,    LAN887X_INFO_FLD_CONFIG_5, 0x0fa1},
	};

	return lan887x_phy_config(phydev, phy_cfg, ARRAY_SIZE(phy_cfg));
}

static int lan887x_100M_setup(struct phy_device *phydev)
{
	int ret;

	/* (Re)configure the speed/mode dependent T1 settings */
	if (phydev->master_slave_set == MASTER_SLAVE_CFG_MASTER_FORCE ||
	    phydev->master_slave_set == MASTER_SLAVE_CFG_MASTER_PREFERRED){
		static const struct lan887x_regwr_map phy_cfg[] = {
			{MDIO_MMD_PMAPMD, LAN887X_AFE_PORT_TESTBUS_CTRL4, 0x00b8},
			{MDIO_MMD_PMAPMD, LAN887X_TX_AMPLT_1000T1_REG, 0x0038},
			{MDIO_MMD_VEND1,  LAN887X_INIT_COEFF_DFE1_100, 0x000f},
		};

		ret = lan887x_phy_config(phydev, phy_cfg, ARRAY_SIZE(phy_cfg));
	} else {
		static const struct lan887x_regwr_map phy_cfg[] = {
			{MDIO_MMD_PMAPMD, LAN887X_AFE_PORT_TESTBUS_CTRL4, 0x0038},
			{MDIO_MMD_VEND1, LAN887X_INIT_COEFF_DFE1_100, 0x0014},
		};

		ret = lan887x_phy_config(phydev, phy_cfg, ARRAY_SIZE(phy_cfg));
	}
	if (ret < 0)
		return ret;

	return phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, LAN887X_REG_REG26,
				LAN887X_REG_REG26_HW_INIT_SEQ_EN);
}

static int lan887x_1000M_setup(struct phy_device *phydev)
{
	static const struct lan887x_regwr_map phy_cfg[] = {
		{MDIO_MMD_PMAPMD, LAN887X_TX_AMPLT_1000T1_REG, 0x003f},
		{MDIO_MMD_PMAPMD, LAN887X_AFE_PORT_TESTBUS_CTRL4, 0x00b8},
	};
	int ret;

	/* (Re)configure the speed/mode dependent T1 settings */
	ret = lan887x_phy_config(phydev, phy_cfg, ARRAY_SIZE(phy_cfg));
	if (ret < 0)
		return ret;

	return phy_set_bits_mmd(phydev, MDIO_MMD_PMAPMD, LAN887X_DSP_PMA_CONTROL,
				LAN887X_DSP_PMA_CONTROL_LNK_SYNC);
}

static int lan887x_link_setup(struct phy_device *phydev)
{
	int ret = -EINVAL;

	if (phydev->speed == SPEED_1000)
		ret = lan887x_1000M_setup(phydev);
	else if (phydev->speed == SPEED_100)
		ret = lan887x_100M_setup(phydev);

	return ret;
}

/* LAN887x Errata: speed configuration changes require soft reset
 * and chip soft reset
 */
static int lan887x_phy_reset(struct phy_device *phydev)
{
	int ret, val;

	/* Clear 1000M link sync */
	ret = phy_clear_bits_mmd(phydev, MDIO_MMD_PMAPMD, LAN887X_DSP_PMA_CONTROL,
				 LAN887X_DSP_PMA_CONTROL_LNK_SYNC);
	if (ret < 0)
		return ret;

	/* Clear 100M link sync */
	ret = phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1, LAN887X_REG_REG26,
				 LAN887X_REG_REG26_HW_INIT_SEQ_EN);
	if (ret < 0)
		return ret;

	/* Chiptop soft-reset to allow the speed/mode change */
	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, LAN887X_CHIP_SOFT_RST,
			    LAN887X_CHIP_SOFT_RST_RESET);
	if (ret < 0)
		return ret;

	/* CL22 soft-reset to let the link re-train */
	ret = phy_modify(phydev, MII_BMCR, BMCR_RESET, BMCR_RESET);
	if (ret < 0)
		return ret;

	/* Wait for reset complete or timeout if > 10ms */
	return phy_read_poll_timeout(phydev, MII_BMCR, val, !(val & BMCR_RESET),
				    5000, 10000, true);
}

static int lan887x_phy_reconfig(struct phy_device *phydev)
{
	int ret;

	linkmode_zero(phydev->advertising);

	ret = genphy_c45_pma_setup_forced(phydev);
	if (ret < 0)
		return ret;

	return lan887x_link_setup(phydev);
}

static int lan887x_config_aneg(struct phy_device *phydev)
{
	int ret;

	/* LAN887x Errata: speed configuration changes require soft reset
	 * and chip soft reset
	 */
	ret = lan887x_phy_reset(phydev);
	if (ret < 0)
		return ret;

	return lan887x_phy_reconfig(phydev);
}

static int lan887x_probe(struct phy_device *phydev)
{
	struct lan887x_priv *priv;

	priv = devm_kzalloc(&phydev->mdio.dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->init_done = false;
	phydev->priv = priv;

	return lan887x_phy_setup(phydev);
}

static u64 lan887x_get_stat(struct phy_device *phydev, int i)
{
	struct lan887x_hw_stat stat = lan887x_hw_stats[i];
	struct lan887x_priv *priv = phydev->priv;
	int val;
	u64 ret;

	if (stat.mmd)
		val = phy_read_mmd(phydev, stat.mmd, stat.reg);
	else
		val = phy_read(phydev, stat.reg);

	if (val < 0) {
		ret = U64_MAX;
	} else {
		val = val & ((1 << stat.bits) - 1);
		priv->stats[i] += val;
		ret = priv->stats[i];
	}

	return ret;
}

static void lan887x_get_stats(struct phy_device *phydev,
			      struct ethtool_stats *stats, u64 *data)
{
	for (int i = 0; i < ARRAY_SIZE(lan887x_hw_stats); i++)
		data[i] = lan887x_get_stat(phydev, i);
}

static int lan887x_get_sset_count(struct phy_device *phydev)
{
	return ARRAY_SIZE(lan887x_hw_stats);
}

static void lan887x_get_strings(struct phy_device *phydev, u8 *data)
{
	for (int i = 0; i < ARRAY_SIZE(lan887x_hw_stats); i++)
		ethtool_puts(&data, lan887x_hw_stats[i].string);
}

static int lan887x_config_intr(struct phy_device *phydev)
{
	struct lan887x_priv *priv = phydev->priv;
	int rc;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		/* Clear the interrupt status before enabling interrupts */
		rc = phy_read_mmd(phydev, MDIO_MMD_VEND1, LAN887X_INT_STS);
		if (rc < 0)
			return rc;

		/* Unmask for enabling interrupt */
		rc = phy_write_mmd(phydev, MDIO_MMD_VEND1, LAN887X_INT_MSK,
				   (u16)~LAN887X_MX_CHIP_TOP_ALL_MSK);
	} else {
		rc = phy_write_mmd(phydev, MDIO_MMD_VEND1, LAN887X_INT_MSK,
				   GENMASK(15, 0));
		if (rc < 0)
			return rc;

		rc = phy_read_mmd(phydev, MDIO_MMD_VEND1, LAN887X_INT_STS);
	}
	if (rc < 0)
		return rc;

	if (phy_is_default_hwtstamp(phydev)) {
		return mchp_rds_ptp_top_config_intr(priv->clock,
					LAN887X_INT_MSK,
					LAN887X_INT_MSK_P1588_MOD_INT_MSK,
					(phydev->interrupts ==
					 PHY_INTERRUPT_ENABLED));
	}

	return 0;
}

static irqreturn_t lan887x_handle_interrupt(struct phy_device *phydev)
{
	struct lan887x_priv *priv = phydev->priv;
	int rc = IRQ_NONE;
	int irq_status;

	irq_status = phy_read_mmd(phydev, MDIO_MMD_VEND1, LAN887X_INT_STS);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if (irq_status & LAN887X_MX_CHIP_TOP_LINK_MSK) {
		phy_trigger_machine(phydev);
		rc = IRQ_HANDLED;
	}

	if (irq_status & LAN887X_INT_MSK_P1588_MOD_INT_MSK)
		rc = mchp_rds_ptp_handle_interrupt(priv->clock);

	return rc;
}

static int lan887x_cd_reset(struct phy_device *phydev,
			    enum cable_diag_state cd_done)
{
	u16 val;
	int rc;

	/* Chip hard-reset */
	rc = phy_write_mmd(phydev, MDIO_MMD_VEND1, LAN887X_CHIP_HARD_RST,
			   LAN887X_CHIP_HARD_RST_RESET);
	if (rc < 0)
		return rc;

	/* Wait for reset to complete */
	rc = phy_read_poll_timeout(phydev, MII_PHYSID2, val,
				   ((val & GENMASK(15, 4)) ==
				    (PHY_ID_LAN887X & GENMASK(15, 4))),
				   5000, 50000, true);
	if (rc < 0)
		return rc;

	if (cd_done == CD_TEST_DONE) {
		/* Cable diagnostics complete. Restore PHY. */
		rc = lan887x_phy_setup(phydev);
		if (rc < 0)
			return rc;

		rc = lan887x_phy_init(phydev);
		if (rc < 0)
			return rc;

		rc = lan887x_config_intr(phydev);
		if (rc < 0)
			return rc;

		rc = lan887x_phy_reconfig(phydev);
		if (rc < 0)
			return rc;
	}

	return 0;
}

static int lan887x_cable_test_prep(struct phy_device *phydev,
				   enum cable_diag_mode mode)
{
	static const struct lan887x_regwr_map values[] = {
		{MDIO_MMD_VEND1, LAN887X_MAX_PGA_GAIN_100, 0x1f},
		{MDIO_MMD_VEND1, LAN887X_MIN_PGA_GAIN_100, 0x0},
		{MDIO_MMD_VEND1, LAN887X_CBL_DIAG_TDR_THRESH_100, 0x1},
		{MDIO_MMD_VEND1, LAN887X_CBL_DIAG_AGC_THRESH_100, 0x3c},
		{MDIO_MMD_VEND1, LAN887X_CBL_DIAG_MIN_WAIT_CONFIG_100, 0x0},
		{MDIO_MMD_VEND1, LAN887X_CBL_DIAG_MAX_WAIT_CONFIG_100, 0x46},
		{MDIO_MMD_VEND1, LAN887X_CBL_DIAG_CYC_CONFIG_100, 0x5a},
		{MDIO_MMD_VEND1, LAN887X_CBL_DIAG_TX_PULSE_CONFIG_100, 0x44d5},
		{MDIO_MMD_VEND1, LAN887X_CBL_DIAG_MIN_PGA_GAIN_100, 0x0},

	};
	int rc;

	rc = lan887x_cd_reset(phydev, CD_TEST_INIT);
	if (rc < 0)
		return rc;

	/* Forcing DUT to master mode, as we don't care about
	 * mode during diagnostics
	 */
	rc = phy_write_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_PMA_PMD_BT1_CTRL,
			   MDIO_PMA_PMD_BT1_CTRL_CFG_MST);
	if (rc < 0)
		return rc;

	rc = phy_write_mmd(phydev, MDIO_MMD_PMAPMD, 0x80b0, 0x0038);
	if (rc < 0)
		return rc;

	rc = phy_modify_mmd(phydev, MDIO_MMD_VEND1,
			    LAN887X_CALIB_CONFIG_100, 0,
			    LAN887X_CALIB_CONFIG_100_VAL);
	if (rc < 0)
		return rc;

	for (int i = 0; i < ARRAY_SIZE(values); i++) {
		rc = phy_write_mmd(phydev, values[i].mmd, values[i].reg,
				   values[i].val);
		if (rc < 0)
			return rc;

		if (mode &&
		    values[i].reg == LAN887X_CBL_DIAG_MAX_WAIT_CONFIG_100) {
			rc = phy_write_mmd(phydev, values[i].mmd,
					   values[i].reg, 0xa);
			if (rc < 0)
				return rc;
		}
	}

	if (mode == TEST_MODE_HYBRID) {
		rc = phy_modify_mmd(phydev, MDIO_MMD_PMAPMD,
				    LAN887X_AFE_PORT_TESTBUS_CTRL4,
				    BIT(0), BIT(0));
		if (rc < 0)
			return rc;
	}

	/* HW_INIT 100T1, Get DUT running in 100T1 mode */
	rc = phy_modify_mmd(phydev, MDIO_MMD_VEND1, LAN887X_REG_REG26,
			    LAN887X_REG_REG26_HW_INIT_SEQ_EN,
			    LAN887X_REG_REG26_HW_INIT_SEQ_EN);
	if (rc < 0)
		return rc;

	/* Cable diag requires hard reset and is sensitive regarding the delays.
	 * Hard reset is expected into and out of cable diag.
	 * Wait for 50ms
	 */
	msleep(50);

	/* Start cable diag */
	return phy_write_mmd(phydev, MDIO_MMD_VEND1,
			   LAN887X_START_CBL_DIAG_100,
			   LAN887X_CBL_DIAG_START);
}

static int lan887x_cable_test_chk(struct phy_device *phydev,
				  enum cable_diag_mode mode)
{
	int val;
	int rc;

	if (mode == TEST_MODE_HYBRID) {
		/* Cable diag requires hard reset and is sensitive regarding the delays.
		 * Hard reset is expected into and out of cable diag.
		 * Wait for cable diag to complete.
		 * Minimum wait time is 50ms if the condition is not a match.
		 */
		rc = phy_read_mmd_poll_timeout(phydev, MDIO_MMD_VEND1,
					       LAN887X_START_CBL_DIAG_100, val,
					       ((val & LAN887X_CBL_DIAG_DONE) ==
						LAN887X_CBL_DIAG_DONE),
					       50000, 500000, false);
		if (rc < 0)
			return rc;
	} else {
		rc = phy_read_mmd(phydev, MDIO_MMD_VEND1,
				  LAN887X_START_CBL_DIAG_100);
		if (rc < 0)
			return rc;

		if ((rc & LAN887X_CBL_DIAG_DONE) != LAN887X_CBL_DIAG_DONE)
			return -EAGAIN;
	}

	/* Stop cable diag */
	return phy_write_mmd(phydev, MDIO_MMD_VEND1,
			     LAN887X_START_CBL_DIAG_100,
			     LAN887X_CBL_DIAG_STOP);
}

static int lan887x_cable_test_start(struct phy_device *phydev)
{
	int rc, ret;

	rc = lan887x_cable_test_prep(phydev, TEST_MODE_NORMAL);
	if (rc < 0) {
		ret = lan887x_cd_reset(phydev, CD_TEST_DONE);
		if (ret < 0)
			return ret;

		return rc;
	}

	return 0;
}

static int lan887x_cable_test_report(struct phy_device *phydev)
{
	int pos_peak_cycle, pos_peak_cycle_hybrid, pos_peak_in_phases;
	int pos_peak_time, pos_peak_time_hybrid, neg_peak_time;
	int neg_peak_cycle, neg_peak_in_phases;
	int pos_peak_in_phases_hybrid;
	int gain_idx, gain_idx_hybrid;
	int pos_peak_phase_hybrid;
	int pos_peak, neg_peak;
	int distance;
	int detect;
	int length;
	int ret;
	int rc;

	/* Read non-hybrid results */
	gain_idx = phy_read_mmd(phydev, MDIO_MMD_VEND1,
				LAN887X_CBL_DIAG_AGC_GAIN_100);
	if (gain_idx < 0) {
		rc = gain_idx;
		goto error;
	}

	pos_peak = phy_read_mmd(phydev, MDIO_MMD_VEND1,
				LAN887X_CBL_DIAG_POS_PEAK_VALUE_100);
	if (pos_peak < 0) {
		rc = pos_peak;
		goto error;
	}

	neg_peak = phy_read_mmd(phydev, MDIO_MMD_VEND1,
				LAN887X_CBL_DIAG_NEG_PEAK_VALUE_100);
	if (neg_peak < 0) {
		rc = neg_peak;
		goto error;
	}

	pos_peak_time = phy_read_mmd(phydev, MDIO_MMD_VEND1,
				     LAN887X_CBL_DIAG_POS_PEAK_TIME_100);
	if (pos_peak_time < 0) {
		rc = pos_peak_time;
		goto error;
	}

	neg_peak_time = phy_read_mmd(phydev, MDIO_MMD_VEND1,
				     LAN887X_CBL_DIAG_NEG_PEAK_TIME_100);
	if (neg_peak_time < 0) {
		rc = neg_peak_time;
		goto error;
	}

	/* Calculate non-hybrid values */
	pos_peak_cycle = (pos_peak_time >> 7) & 0x7f;
	pos_peak_in_phases = (pos_peak_cycle * 96) + (pos_peak_time & 0x7f);
	neg_peak_cycle = (neg_peak_time >> 7) & 0x7f;
	neg_peak_in_phases = (neg_peak_cycle * 96) + (neg_peak_time & 0x7f);

	/* Deriving the status of cable */
	if (pos_peak > MICROCHIP_CABLE_NOISE_MARGIN &&
	    neg_peak > MICROCHIP_CABLE_NOISE_MARGIN && gain_idx >= 0) {
		if (pos_peak_in_phases > neg_peak_in_phases &&
		    ((pos_peak_in_phases - neg_peak_in_phases) >=
		     MICROCHIP_CABLE_MIN_TIME_DIFF) &&
		    ((pos_peak_in_phases - neg_peak_in_phases) <
		     MICROCHIP_CABLE_MAX_TIME_DIFF) &&
		    pos_peak_in_phases > 0) {
			detect = LAN87XX_CABLE_TEST_SAME_SHORT;
		} else if (neg_peak_in_phases > pos_peak_in_phases &&
			   ((neg_peak_in_phases - pos_peak_in_phases) >=
			    MICROCHIP_CABLE_MIN_TIME_DIFF) &&
			   ((neg_peak_in_phases - pos_peak_in_phases) <
			    MICROCHIP_CABLE_MAX_TIME_DIFF) &&
			   neg_peak_in_phases > 0) {
			detect = LAN87XX_CABLE_TEST_OPEN;
		} else {
			detect = LAN87XX_CABLE_TEST_OK;
		}
	} else {
		detect = LAN87XX_CABLE_TEST_OK;
	}

	if (detect == LAN87XX_CABLE_TEST_OK) {
		distance = 0;
		goto get_len;
	}

	/* Re-initialize PHY and start cable diag test */
	rc = lan887x_cable_test_prep(phydev, TEST_MODE_HYBRID);
	if (rc < 0)
		goto cd_stop;

	/* Wait for cable diag test completion */
	rc = lan887x_cable_test_chk(phydev, TEST_MODE_HYBRID);
	if (rc < 0)
		goto cd_stop;

	/* Read hybrid results */
	gain_idx_hybrid = phy_read_mmd(phydev, MDIO_MMD_VEND1,
				       LAN887X_CBL_DIAG_AGC_GAIN_100);
	if (gain_idx_hybrid < 0) {
		rc = gain_idx_hybrid;
		goto error;
	}

	pos_peak_time_hybrid = phy_read_mmd(phydev, MDIO_MMD_VEND1,
					    LAN887X_CBL_DIAG_POS_PEAK_TIME_100);
	if (pos_peak_time_hybrid < 0) {
		rc = pos_peak_time_hybrid;
		goto error;
	}

	/* Calculate hybrid values to derive cable length to fault */
	pos_peak_cycle_hybrid = (pos_peak_time_hybrid >> 7) & 0x7f;
	pos_peak_phase_hybrid = pos_peak_time_hybrid & 0x7f;
	pos_peak_in_phases_hybrid = pos_peak_cycle_hybrid * 96 +
				    pos_peak_phase_hybrid;

	/* Distance to fault calculation.
	 * distance = (peak_in_phases - peak_in_phases_hybrid) *
	 *             propagationconstant.
	 * constant to convert number of phases to meters
	 * propagationconstant = 0.015953
	 *                       (0.6811 * 2.9979 * 156.2499 * 0.0001 * 0.5)
	 * Applying constant 1.5953 as ethtool further devides by 100 to
	 * convert to meters.
	 */
	if (detect == LAN87XX_CABLE_TEST_OPEN) {
		distance = (((pos_peak_in_phases - pos_peak_in_phases_hybrid)
			     * 15953) / 10000);
	} else if (detect == LAN87XX_CABLE_TEST_SAME_SHORT) {
		distance = (((neg_peak_in_phases - pos_peak_in_phases_hybrid)
			     * 15953) / 10000);
	} else {
		distance = 0;
	}

get_len:
	rc = lan887x_cd_reset(phydev, CD_TEST_DONE);
	if (rc < 0)
		return rc;

	length = ((u32)distance & GENMASK(15, 0));
	ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_A,
				lan87xx_cable_test_report_trans(detect));
	ethnl_cable_test_fault_length(phydev, ETHTOOL_A_CABLE_PAIR_A, length);

	return 0;

cd_stop:
	/* Stop cable diag */
	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1,
			    LAN887X_START_CBL_DIAG_100,
			    LAN887X_CBL_DIAG_STOP);
	if (ret < 0)
		return ret;

error:
	/* Cable diag test failed */
	ret = lan887x_cd_reset(phydev, CD_TEST_DONE);
	if (ret < 0)
		return ret;

	/* Return error in failure case */
	return rc;
}

static int lan887x_cable_test_get_status(struct phy_device *phydev,
					 bool *finished)
{
	int rc;

	rc = lan887x_cable_test_chk(phydev, TEST_MODE_NORMAL);
	if (rc < 0) {
		/* Let PHY statemachine poll again */
		if (rc == -EAGAIN)
			return 0;
		return rc;
	}

	/* Cable diag test complete */
	*finished = true;

	/* Retrieve test status and cable length to fault */
	return lan887x_cable_test_report(phydev);
}

/* Compare block to sort in ascending order */
static int sqi_compare(const void *a, const void *b)
{
	return  *(u16 *)a - *(u16 *)b;
}

static int lan887x_get_sqi_100M(struct phy_device *phydev)
{
	u16 rawtable[SQI_SAMPLES];
	u32 sqiavg = 0;
	u8 sqinum = 0;
	int rc, i;

	/* Configuration of SQI 100M */
	rc = phy_write_mmd(phydev, MDIO_MMD_VEND1,
			   LAN887X_COEFF_PWR_DN_CONFIG_100,
			   LAN887X_COEFF_PWR_DN_CONFIG_100_V);
	if (rc < 0)
		return rc;

	rc = phy_write_mmd(phydev, MDIO_MMD_VEND1, LAN887X_SQI_CONFIG_100,
			   LAN887X_SQI_CONFIG_100_V);
	if (rc < 0)
		return rc;

	rc = phy_read_mmd(phydev, MDIO_MMD_VEND1, LAN887X_SQI_CONFIG_100);
	if (rc != LAN887X_SQI_CONFIG_100_V)
		return -EINVAL;

	rc = phy_modify_mmd(phydev, MDIO_MMD_VEND1, LAN887X_POKE_PEEK_100,
			    LAN887X_POKE_PEEK_100_EN,
			    LAN887X_POKE_PEEK_100_EN);
	if (rc < 0)
		return rc;

	/* Required before reading register
	 * otherwise it will return high value
	 */
	msleep(50);

	/* Link check before raw readings */
	rc = genphy_c45_read_link(phydev);
	if (rc < 0)
		return rc;

	if (!phydev->link)
		return -ENETDOWN;

	/* Get 200 SQI raw readings */
	for (i = 0; i < SQI_SAMPLES; i++) {
		rc = phy_write_mmd(phydev, MDIO_MMD_VEND1,
				   LAN887X_POKE_PEEK_100,
				   LAN887X_POKE_PEEK_100_EN);
		if (rc < 0)
			return rc;

		rc = phy_read_mmd(phydev, MDIO_MMD_VEND1,
				  LAN887X_SQI_MSE_100);
		if (rc < 0)
			return rc;

		rawtable[i] = (u16)rc;
	}

	/* Link check after raw readings */
	rc = genphy_c45_read_link(phydev);
	if (rc < 0)
		return rc;

	if (!phydev->link)
		return -ENETDOWN;

	/* Sort SQI raw readings in ascending order */
	sort(rawtable, SQI_SAMPLES, sizeof(u16), sqi_compare, NULL);

	/* Keep inliers and discard outliers */
	for (i = SQI_INLIERS_START; i < SQI_INLIERS_END; i++)
		sqiavg += rawtable[i];

	/* Handle invalid samples */
	if (sqiavg != 0) {
		/* Get SQI average */
		sqiavg /= SQI_INLIERS_NUM;

		if (sqiavg < 75)
			sqinum = 7;
		else if (sqiavg < 94)
			sqinum = 6;
		else if (sqiavg < 119)
			sqinum = 5;
		else if (sqiavg < 150)
			sqinum = 4;
		else if (sqiavg < 189)
			sqinum = 3;
		else if (sqiavg < 237)
			sqinum = 2;
		else if (sqiavg < 299)
			sqinum = 1;
		else
			sqinum = 0;
	}

	return sqinum;
}

static int lan887x_get_sqi(struct phy_device *phydev)
{
	int rc, val;

	if (phydev->speed != SPEED_1000 &&
	    phydev->speed != SPEED_100)
		return -ENETDOWN;

	if (phydev->speed == SPEED_100)
		return lan887x_get_sqi_100M(phydev);

	/* Writing DCQ_COEFF_EN to trigger a SQI read */
	rc = phy_set_bits_mmd(phydev, MDIO_MMD_VEND1,
			      LAN887X_COEFF_MOD_CONFIG,
			      LAN887X_COEFF_MOD_CONFIG_DCQ_COEFF_EN);
	if (rc < 0)
		return rc;

	/* Wait for DCQ done */
	rc = phy_read_mmd_poll_timeout(phydev, MDIO_MMD_VEND1,
				       LAN887X_COEFF_MOD_CONFIG, val, ((val &
				       LAN887X_COEFF_MOD_CONFIG_DCQ_COEFF_EN) !=
				       LAN887X_COEFF_MOD_CONFIG_DCQ_COEFF_EN),
				       10, 200, true);
	if (rc < 0)
		return rc;

	rc = phy_read_mmd(phydev, MDIO_MMD_VEND1, LAN887X_DCQ_SQI_STATUS);
	if (rc < 0)
		return rc;

	return FIELD_GET(T1_DCQ_SQI_MSK, rc);
}

static struct phy_driver microchip_t1_phy_driver[] = {
	{
		PHY_ID_MATCH_MODEL(PHY_ID_LAN87XX),
		.name           = "Microchip LAN87xx T1",
		.flags          = PHY_POLL_CABLE_TEST,
		.features       = PHY_BASIC_T1_FEATURES,
		.config_init	= lan87xx_config_init,
		.config_intr    = lan87xx_phy_config_intr,
		.handle_interrupt = lan87xx_handle_interrupt,
		.suspend        = genphy_suspend,
		.resume         = genphy_resume,
		.config_aneg    = lan87xx_config_aneg,
		.read_status	= lan87xx_read_status,
		.get_sqi	= lan87xx_get_sqi,
		.get_sqi_max	= lan87xx_get_sqi_max,
		.cable_test_start = lan87xx_cable_test_start,
		.cable_test_get_status = lan87xx_cable_test_get_status,
	},
	{
		PHY_ID_MATCH_MODEL(PHY_ID_LAN937X),
		.name		= "Microchip LAN937x T1",
		.flags          = PHY_POLL_CABLE_TEST,
		.features	= PHY_BASIC_T1_FEATURES,
		.config_init	= lan87xx_config_init,
		.config_intr    = lan87xx_phy_config_intr,
		.handle_interrupt = lan87xx_handle_interrupt,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.config_aneg    = lan87xx_config_aneg,
		.read_status	= lan87xx_read_status,
		.get_sqi	= lan87xx_get_sqi,
		.get_sqi_max	= lan87xx_get_sqi_max,
		.cable_test_start = lan87xx_cable_test_start,
		.cable_test_get_status = lan87xx_cable_test_get_status,
	},
	{
		PHY_ID_MATCH_MODEL(PHY_ID_LAN887X),
		.name		= "Microchip LAN887x T1 PHY",
		.flags          = PHY_POLL_CABLE_TEST,
		.probe		= lan887x_probe,
		.get_features	= lan887x_get_features,
		.config_init    = lan887x_phy_init,
		.config_aneg    = lan887x_config_aneg,
		.get_stats      = lan887x_get_stats,
		.get_sset_count = lan887x_get_sset_count,
		.get_strings    = lan887x_get_strings,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.read_status	= genphy_c45_read_status,
		.cable_test_start = lan887x_cable_test_start,
		.cable_test_get_status = lan887x_cable_test_get_status,
		.config_intr    = lan887x_config_intr,
		.handle_interrupt = lan887x_handle_interrupt,
		.get_sqi	= lan887x_get_sqi,
		.get_sqi_max	= lan87xx_get_sqi_max,
		.set_loopback	= genphy_c45_loopback,
	}
};

module_phy_driver(microchip_t1_phy_driver);

static struct mdio_device_id __maybe_unused microchip_t1_tbl[] = {
	{ PHY_ID_MATCH_MODEL(PHY_ID_LAN87XX) },
	{ PHY_ID_MATCH_MODEL(PHY_ID_LAN937X) },
	{ PHY_ID_MATCH_MODEL(PHY_ID_LAN887X) },
	{ }
};

MODULE_DEVICE_TABLE(mdio, microchip_t1_tbl);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
