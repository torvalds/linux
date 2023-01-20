// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Microchip Technology

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/ethtool.h>
#include <linux/ethtool_netlink.h>
#include <linux/bitfield.h>

#define PHY_ID_LAN87XX				0x0007c150
#define PHY_ID_LAN937X				0x0007c180

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

#define DRIVER_AUTHOR	"Nisar Sayed <nisar.sayed@microchip.com>"
#define DRIVER_DESC	"Microchip LAN87XX/LAN937x T1 PHY driver"

struct access_ereg_val {
	u8  mode;
	u8  bank;
	u8  offset;
	u16 val;
	u16 mask;
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

	return 0;
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
	}
};

module_phy_driver(microchip_t1_phy_driver);

static struct mdio_device_id __maybe_unused microchip_t1_tbl[] = {
	{ PHY_ID_MATCH_MODEL(PHY_ID_LAN87XX) },
	{ PHY_ID_MATCH_MODEL(PHY_ID_LAN937X) },
	{ }
};

MODULE_DEVICE_TABLE(mdio, microchip_t1_tbl);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
