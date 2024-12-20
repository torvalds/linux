// SPDX-License-Identifier: GPL-2.0
/* Driver for the Texas Instruments DP83822, DP83825 and DP83826 PHYs.
 *
 * Copyright (C) 2017 Texas Instruments Inc.
 */

#include <linux/ethtool.h>
#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/netdevice.h>
#include <linux/bitfield.h>

#define DP83822_PHY_ID	        0x2000a240
#define DP83825S_PHY_ID		0x2000a140
#define DP83825I_PHY_ID		0x2000a150
#define DP83825CM_PHY_ID	0x2000a160
#define DP83825CS_PHY_ID	0x2000a170
#define DP83826C_PHY_ID		0x2000a130
#define DP83826NC_PHY_ID	0x2000a110

#define DP83822_DEVADDR		0x1f

#define MII_DP83822_CTRL_2	0x0a
#define MII_DP83822_PHYSTS	0x10
#define MII_DP83822_PHYSCR	0x11
#define MII_DP83822_MISR1	0x12
#define MII_DP83822_MISR2	0x13
#define MII_DP83822_FCSCR	0x14
#define MII_DP83822_RCSR	0x17
#define MII_DP83822_RESET_CTRL	0x1f
#define MII_DP83822_GENCFG	0x465
#define MII_DP83822_SOR1	0x467

/* DP83826 specific registers */
#define MII_DP83826_VOD_CFG1	0x30b
#define MII_DP83826_VOD_CFG2	0x30c

/* GENCFG */
#define DP83822_SIG_DET_LOW	BIT(0)

/* Control Register 2 bits */
#define DP83822_FX_ENABLE	BIT(14)

#define DP83822_SW_RESET	BIT(15)
#define DP83822_DIG_RESTART	BIT(14)

/* PHY STS bits */
#define DP83822_PHYSTS_DUPLEX			BIT(2)
#define DP83822_PHYSTS_10			BIT(1)
#define DP83822_PHYSTS_LINK			BIT(0)

/* PHYSCR Register Fields */
#define DP83822_PHYSCR_INT_OE		BIT(0) /* Interrupt Output Enable */
#define DP83822_PHYSCR_INTEN		BIT(1) /* Interrupt Enable */

/* MISR1 bits */
#define DP83822_RX_ERR_HF_INT_EN	BIT(0)
#define DP83822_FALSE_CARRIER_HF_INT_EN	BIT(1)
#define DP83822_ANEG_COMPLETE_INT_EN	BIT(2)
#define DP83822_DUP_MODE_CHANGE_INT_EN	BIT(3)
#define DP83822_SPEED_CHANGED_INT_EN	BIT(4)
#define DP83822_LINK_STAT_INT_EN	BIT(5)
#define DP83822_ENERGY_DET_INT_EN	BIT(6)
#define DP83822_LINK_QUAL_INT_EN	BIT(7)

/* MISR2 bits */
#define DP83822_JABBER_DET_INT_EN	BIT(0)
#define DP83822_WOL_PKT_INT_EN		BIT(1)
#define DP83822_SLEEP_MODE_INT_EN	BIT(2)
#define DP83822_MDI_XOVER_INT_EN	BIT(3)
#define DP83822_LB_FIFO_INT_EN		BIT(4)
#define DP83822_PAGE_RX_INT_EN		BIT(5)
#define DP83822_ANEG_ERR_INT_EN		BIT(6)
#define DP83822_EEE_ERROR_CHANGE_INT_EN	BIT(7)

/* INT_STAT1 bits */
#define DP83822_WOL_INT_EN	BIT(4)
#define DP83822_WOL_INT_STAT	BIT(12)

#define MII_DP83822_RXSOP1	0x04a5
#define	MII_DP83822_RXSOP2	0x04a6
#define	MII_DP83822_RXSOP3	0x04a7

/* WoL Registers */
#define	MII_DP83822_WOL_CFG	0x04a0
#define	MII_DP83822_WOL_STAT	0x04a1
#define	MII_DP83822_WOL_DA1	0x04a2
#define	MII_DP83822_WOL_DA2	0x04a3
#define	MII_DP83822_WOL_DA3	0x04a4

/* WoL bits */
#define DP83822_WOL_MAGIC_EN	BIT(0)
#define DP83822_WOL_SECURE_ON	BIT(5)
#define DP83822_WOL_EN		BIT(7)
#define DP83822_WOL_INDICATION_SEL BIT(8)
#define DP83822_WOL_CLR_INDICATION BIT(11)

/* RCSR bits */
#define DP83822_RMII_MODE_EN	BIT(5)
#define DP83822_RMII_MODE_SEL	BIT(7)
#define DP83822_RGMII_MODE_EN	BIT(9)
#define DP83822_RX_CLK_SHIFT	BIT(12)
#define DP83822_TX_CLK_SHIFT	BIT(11)

/* SOR1 mode */
#define DP83822_STRAP_MODE1	0
#define DP83822_STRAP_MODE2	BIT(0)
#define DP83822_STRAP_MODE3	BIT(1)
#define DP83822_STRAP_MODE4	GENMASK(1, 0)

#define DP83822_COL_STRAP_MASK	GENMASK(11, 10)
#define DP83822_COL_SHIFT	10
#define DP83822_RX_ER_STR_MASK	GENMASK(9, 8)
#define DP83822_RX_ER_SHIFT	8

/* DP83826: VOD_CFG1 & VOD_CFG2 */
#define DP83826_VOD_CFG1_MINUS_MDIX_MASK	GENMASK(13, 12)
#define DP83826_VOD_CFG1_MINUS_MDI_MASK		GENMASK(11, 6)
#define DP83826_VOD_CFG2_MINUS_MDIX_MASK	GENMASK(15, 12)
#define DP83826_VOD_CFG2_PLUS_MDIX_MASK		GENMASK(11, 6)
#define DP83826_VOD_CFG2_PLUS_MDI_MASK		GENMASK(5, 0)
#define DP83826_CFG_DAC_MINUS_MDIX_5_TO_4	GENMASK(5, 4)
#define DP83826_CFG_DAC_MINUS_MDIX_3_TO_0	GENMASK(3, 0)
#define DP83826_CFG_DAC_PERCENT_PER_STEP	625
#define DP83826_CFG_DAC_PERCENT_DEFAULT		10000
#define DP83826_CFG_DAC_MINUS_DEFAULT		0x30
#define DP83826_CFG_DAC_PLUS_DEFAULT		0x10

#define MII_DP83822_FIBER_ADVERTISE    (ADVERTISED_TP | ADVERTISED_MII | \
					ADVERTISED_FIBRE | \
					ADVERTISED_Pause | ADVERTISED_Asym_Pause)

struct dp83822_private {
	bool fx_signal_det_low;
	int fx_enabled;
	u16 fx_sd_enable;
	u8 cfg_dac_minus;
	u8 cfg_dac_plus;
	struct ethtool_wolinfo wol;
};

static int dp83822_config_wol(struct phy_device *phydev,
			      struct ethtool_wolinfo *wol)
{
	struct net_device *ndev = phydev->attached_dev;
	u16 value;
	const u8 *mac;

	if (wol->wolopts & (WAKE_MAGIC | WAKE_MAGICSECURE)) {
		mac = (const u8 *)ndev->dev_addr;

		if (!is_valid_ether_addr(mac))
			return -EINVAL;

		/* MAC addresses start with byte 5, but stored in mac[0].
		 * 822 PHYs store bytes 4|5, 2|3, 0|1
		 */
		phy_write_mmd(phydev, DP83822_DEVADDR, MII_DP83822_WOL_DA1,
			      (mac[1] << 8) | mac[0]);
		phy_write_mmd(phydev, DP83822_DEVADDR, MII_DP83822_WOL_DA2,
			      (mac[3] << 8) | mac[2]);
		phy_write_mmd(phydev, DP83822_DEVADDR, MII_DP83822_WOL_DA3,
			      (mac[5] << 8) | mac[4]);

		value = phy_read_mmd(phydev, DP83822_DEVADDR,
				     MII_DP83822_WOL_CFG);
		if (wol->wolopts & WAKE_MAGIC)
			value |= DP83822_WOL_MAGIC_EN;
		else
			value &= ~DP83822_WOL_MAGIC_EN;

		if (wol->wolopts & WAKE_MAGICSECURE) {
			phy_write_mmd(phydev, DP83822_DEVADDR,
				      MII_DP83822_RXSOP1,
				      (wol->sopass[1] << 8) | wol->sopass[0]);
			phy_write_mmd(phydev, DP83822_DEVADDR,
				      MII_DP83822_RXSOP2,
				      (wol->sopass[3] << 8) | wol->sopass[2]);
			phy_write_mmd(phydev, DP83822_DEVADDR,
				      MII_DP83822_RXSOP3,
				      (wol->sopass[5] << 8) | wol->sopass[4]);
			value |= DP83822_WOL_SECURE_ON;
		} else {
			value &= ~DP83822_WOL_SECURE_ON;
		}

		/* Clear any pending WoL interrupt */
		phy_read(phydev, MII_DP83822_MISR2);

		value |= DP83822_WOL_EN | DP83822_WOL_INDICATION_SEL |
			 DP83822_WOL_CLR_INDICATION;

		return phy_write_mmd(phydev, DP83822_DEVADDR,
				     MII_DP83822_WOL_CFG, value);
	} else {
		return phy_clear_bits_mmd(phydev, DP83822_DEVADDR,
					  MII_DP83822_WOL_CFG,
					  DP83822_WOL_EN |
					  DP83822_WOL_MAGIC_EN |
					  DP83822_WOL_SECURE_ON);
	}
}

static int dp83822_set_wol(struct phy_device *phydev,
			   struct ethtool_wolinfo *wol)
{
	struct dp83822_private *dp83822 = phydev->priv;
	int ret;

	ret = dp83822_config_wol(phydev, wol);
	if (!ret)
		memcpy(&dp83822->wol, wol, sizeof(*wol));
	return ret;
}

static void dp83822_get_wol(struct phy_device *phydev,
			    struct ethtool_wolinfo *wol)
{
	int value;
	u16 sopass_val;

	wol->supported = (WAKE_MAGIC | WAKE_MAGICSECURE);
	wol->wolopts = 0;

	value = phy_read_mmd(phydev, DP83822_DEVADDR, MII_DP83822_WOL_CFG);

	if (value & DP83822_WOL_MAGIC_EN)
		wol->wolopts |= WAKE_MAGIC;

	if (value & DP83822_WOL_SECURE_ON) {
		sopass_val = phy_read_mmd(phydev, DP83822_DEVADDR,
					  MII_DP83822_RXSOP1);
		wol->sopass[0] = (sopass_val & 0xff);
		wol->sopass[1] = (sopass_val >> 8);

		sopass_val = phy_read_mmd(phydev, DP83822_DEVADDR,
					  MII_DP83822_RXSOP2);
		wol->sopass[2] = (sopass_val & 0xff);
		wol->sopass[3] = (sopass_val >> 8);

		sopass_val = phy_read_mmd(phydev, DP83822_DEVADDR,
					  MII_DP83822_RXSOP3);
		wol->sopass[4] = (sopass_val & 0xff);
		wol->sopass[5] = (sopass_val >> 8);

		wol->wolopts |= WAKE_MAGICSECURE;
	}

	/* WoL is not enabled so set wolopts to 0 */
	if (!(value & DP83822_WOL_EN))
		wol->wolopts = 0;
}

static int dp83822_config_intr(struct phy_device *phydev)
{
	struct dp83822_private *dp83822 = phydev->priv;
	int misr_status;
	int physcr_status;
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		misr_status = phy_read(phydev, MII_DP83822_MISR1);
		if (misr_status < 0)
			return misr_status;

		misr_status |= (DP83822_LINK_STAT_INT_EN |
				DP83822_ENERGY_DET_INT_EN |
				DP83822_LINK_QUAL_INT_EN);

		if (!dp83822->fx_enabled)
			misr_status |= DP83822_ANEG_COMPLETE_INT_EN |
				       DP83822_DUP_MODE_CHANGE_INT_EN |
				       DP83822_SPEED_CHANGED_INT_EN;


		err = phy_write(phydev, MII_DP83822_MISR1, misr_status);
		if (err < 0)
			return err;

		misr_status = phy_read(phydev, MII_DP83822_MISR2);
		if (misr_status < 0)
			return misr_status;

		misr_status |= (DP83822_JABBER_DET_INT_EN |
				DP83822_SLEEP_MODE_INT_EN |
				DP83822_LB_FIFO_INT_EN |
				DP83822_PAGE_RX_INT_EN |
				DP83822_EEE_ERROR_CHANGE_INT_EN);

		if (!dp83822->fx_enabled)
			misr_status |= DP83822_ANEG_ERR_INT_EN |
				       DP83822_WOL_PKT_INT_EN;

		err = phy_write(phydev, MII_DP83822_MISR2, misr_status);
		if (err < 0)
			return err;

		physcr_status = phy_read(phydev, MII_DP83822_PHYSCR);
		if (physcr_status < 0)
			return physcr_status;

		physcr_status |= DP83822_PHYSCR_INT_OE | DP83822_PHYSCR_INTEN;

	} else {
		err = phy_write(phydev, MII_DP83822_MISR1, 0);
		if (err < 0)
			return err;

		err = phy_write(phydev, MII_DP83822_MISR2, 0);
		if (err < 0)
			return err;

		physcr_status = phy_read(phydev, MII_DP83822_PHYSCR);
		if (physcr_status < 0)
			return physcr_status;

		physcr_status &= ~DP83822_PHYSCR_INTEN;
	}

	return phy_write(phydev, MII_DP83822_PHYSCR, physcr_status);
}

static irqreturn_t dp83822_handle_interrupt(struct phy_device *phydev)
{
	bool trigger_machine = false;
	int irq_status;

	/* The MISR1 and MISR2 registers are holding the interrupt status in
	 * the upper half (15:8), while the lower half (7:0) is used for
	 * controlling the interrupt enable state of those individual interrupt
	 * sources. To determine the possible interrupt sources, just read the
	 * MISR* register and use it directly to know which interrupts have
	 * been enabled previously or not.
	 */
	irq_status = phy_read(phydev, MII_DP83822_MISR1);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}
	if (irq_status & ((irq_status & GENMASK(7, 0)) << 8))
		trigger_machine = true;

	irq_status = phy_read(phydev, MII_DP83822_MISR2);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}
	if (irq_status & ((irq_status & GENMASK(7, 0)) << 8))
		trigger_machine = true;

	if (!trigger_machine)
		return IRQ_NONE;

	phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}

static int dp83822_read_status(struct phy_device *phydev)
{
	struct dp83822_private *dp83822 = phydev->priv;
	int status = phy_read(phydev, MII_DP83822_PHYSTS);
	int ctrl2;
	int ret;

	if (dp83822->fx_enabled) {
		if (status & DP83822_PHYSTS_LINK) {
			phydev->speed = SPEED_UNKNOWN;
			phydev->duplex = DUPLEX_UNKNOWN;
		} else {
			ctrl2 = phy_read(phydev, MII_DP83822_CTRL_2);
			if (ctrl2 < 0)
				return ctrl2;

			if (!(ctrl2 & DP83822_FX_ENABLE)) {
				ret = phy_write(phydev, MII_DP83822_CTRL_2,
						DP83822_FX_ENABLE | ctrl2);
				if (ret < 0)
					return ret;
			}
		}
	}

	ret = genphy_read_status(phydev);
	if (ret)
		return ret;

	if (status < 0)
		return status;

	if (status & DP83822_PHYSTS_DUPLEX)
		phydev->duplex = DUPLEX_FULL;
	else
		phydev->duplex = DUPLEX_HALF;

	if (status & DP83822_PHYSTS_10)
		phydev->speed = SPEED_10;
	else
		phydev->speed = SPEED_100;

	return 0;
}

static int dp83822_config_init(struct phy_device *phydev)
{
	struct dp83822_private *dp83822 = phydev->priv;
	struct device *dev = &phydev->mdio.dev;
	int rgmii_delay = 0;
	s32 rx_int_delay;
	s32 tx_int_delay;
	int err = 0;
	int bmcr;

	if (phy_interface_is_rgmii(phydev)) {
		rx_int_delay = phy_get_internal_delay(phydev, dev, NULL, 0,
						      true);

		/* Set DP83822_RX_CLK_SHIFT to enable rx clk internal delay */
		if (rx_int_delay > 0)
			rgmii_delay |= DP83822_RX_CLK_SHIFT;

		tx_int_delay = phy_get_internal_delay(phydev, dev, NULL, 0,
						      false);

		/* Set DP83822_TX_CLK_SHIFT to disable tx clk internal delay */
		if (tx_int_delay <= 0)
			rgmii_delay |= DP83822_TX_CLK_SHIFT;

		err = phy_modify_mmd(phydev, DP83822_DEVADDR, MII_DP83822_RCSR,
				     DP83822_RX_CLK_SHIFT | DP83822_TX_CLK_SHIFT, rgmii_delay);
		if (err)
			return err;

		err = phy_set_bits_mmd(phydev, DP83822_DEVADDR,
				       MII_DP83822_RCSR, DP83822_RGMII_MODE_EN);

		if (err)
			return err;
	} else {
		err = phy_clear_bits_mmd(phydev, DP83822_DEVADDR,
					 MII_DP83822_RCSR, DP83822_RGMII_MODE_EN);

		if (err)
			return err;
	}

	if (dp83822->fx_enabled) {
		err = phy_modify(phydev, MII_DP83822_CTRL_2,
				 DP83822_FX_ENABLE, 1);
		if (err < 0)
			return err;

		/* Only allow advertising what this PHY supports */
		linkmode_and(phydev->advertising, phydev->advertising,
			     phydev->supported);

		linkmode_set_bit(ETHTOOL_LINK_MODE_FIBRE_BIT,
				 phydev->supported);
		linkmode_set_bit(ETHTOOL_LINK_MODE_FIBRE_BIT,
				 phydev->advertising);
		linkmode_set_bit(ETHTOOL_LINK_MODE_100baseFX_Full_BIT,
				 phydev->supported);
		linkmode_set_bit(ETHTOOL_LINK_MODE_100baseFX_Half_BIT,
				 phydev->supported);
		linkmode_set_bit(ETHTOOL_LINK_MODE_100baseFX_Full_BIT,
				 phydev->advertising);
		linkmode_set_bit(ETHTOOL_LINK_MODE_100baseFX_Half_BIT,
				 phydev->advertising);

		/* Auto neg is not supported in fiber mode */
		bmcr = phy_read(phydev, MII_BMCR);
		if (bmcr < 0)
			return bmcr;

		if (bmcr & BMCR_ANENABLE) {
			err =  phy_modify(phydev, MII_BMCR, BMCR_ANENABLE, 0);
			if (err < 0)
				return err;
		}
		phydev->autoneg = AUTONEG_DISABLE;
		linkmode_clear_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
				   phydev->supported);
		linkmode_clear_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
				   phydev->advertising);

		/* Setup fiber advertisement */
		err = phy_modify_changed(phydev, MII_ADVERTISE,
					 MII_DP83822_FIBER_ADVERTISE,
					 MII_DP83822_FIBER_ADVERTISE);

		if (err < 0)
			return err;

		if (dp83822->fx_signal_det_low) {
			err = phy_set_bits_mmd(phydev, DP83822_DEVADDR,
					       MII_DP83822_GENCFG,
					       DP83822_SIG_DET_LOW);
			if (err)
				return err;
		}
	}
	return dp83822_config_wol(phydev, &dp83822->wol);
}

static int dp8382x_config_rmii_mode(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	const char *of_val;
	int ret;

	if (!device_property_read_string(dev, "ti,rmii-mode", &of_val)) {
		if (strcmp(of_val, "master") == 0) {
			ret = phy_clear_bits_mmd(phydev, DP83822_DEVADDR, MII_DP83822_RCSR,
						 DP83822_RMII_MODE_SEL);
		} else if (strcmp(of_val, "slave") == 0) {
			ret = phy_set_bits_mmd(phydev, DP83822_DEVADDR, MII_DP83822_RCSR,
					       DP83822_RMII_MODE_SEL);
		} else {
			phydev_err(phydev, "Invalid value for ti,rmii-mode property (%s)\n",
				   of_val);
			ret = -EINVAL;
		}

		if (ret)
			return ret;
	}

	return 0;
}

static int dp83826_config_init(struct phy_device *phydev)
{
	struct dp83822_private *dp83822 = phydev->priv;
	u16 val, mask;
	int ret;

	if (phydev->interface == PHY_INTERFACE_MODE_RMII) {
		ret = phy_set_bits_mmd(phydev, DP83822_DEVADDR, MII_DP83822_RCSR,
				       DP83822_RMII_MODE_EN);
		if (ret)
			return ret;

		ret = dp8382x_config_rmii_mode(phydev);
		if (ret)
			return ret;
	} else {
		ret = phy_clear_bits_mmd(phydev, DP83822_DEVADDR, MII_DP83822_RCSR,
					 DP83822_RMII_MODE_EN);
		if (ret)
			return ret;
	}

	if (dp83822->cfg_dac_minus != DP83826_CFG_DAC_MINUS_DEFAULT) {
		val = FIELD_PREP(DP83826_VOD_CFG1_MINUS_MDI_MASK, dp83822->cfg_dac_minus) |
		      FIELD_PREP(DP83826_VOD_CFG1_MINUS_MDIX_MASK,
				 FIELD_GET(DP83826_CFG_DAC_MINUS_MDIX_5_TO_4,
					   dp83822->cfg_dac_minus));
		mask = DP83826_VOD_CFG1_MINUS_MDIX_MASK | DP83826_VOD_CFG1_MINUS_MDI_MASK;
		ret = phy_modify_mmd(phydev, DP83822_DEVADDR, MII_DP83826_VOD_CFG1, mask, val);
		if (ret)
			return ret;

		val = FIELD_PREP(DP83826_VOD_CFG2_MINUS_MDIX_MASK,
				 FIELD_GET(DP83826_CFG_DAC_MINUS_MDIX_3_TO_0,
					   dp83822->cfg_dac_minus));
		mask = DP83826_VOD_CFG2_MINUS_MDIX_MASK;
		ret = phy_modify_mmd(phydev, DP83822_DEVADDR, MII_DP83826_VOD_CFG2, mask, val);
		if (ret)
			return ret;
	}

	if (dp83822->cfg_dac_plus != DP83826_CFG_DAC_PLUS_DEFAULT) {
		val = FIELD_PREP(DP83826_VOD_CFG2_PLUS_MDIX_MASK, dp83822->cfg_dac_plus) |
		      FIELD_PREP(DP83826_VOD_CFG2_PLUS_MDI_MASK, dp83822->cfg_dac_plus);
		mask = DP83826_VOD_CFG2_PLUS_MDIX_MASK | DP83826_VOD_CFG2_PLUS_MDI_MASK;
		ret = phy_modify_mmd(phydev, DP83822_DEVADDR, MII_DP83826_VOD_CFG2, mask, val);
		if (ret)
			return ret;
	}

	return dp83822_config_wol(phydev, &dp83822->wol);
}

static int dp83825_config_init(struct phy_device *phydev)
{
	struct dp83822_private *dp83822 = phydev->priv;
	int ret;

	ret = dp8382x_config_rmii_mode(phydev);
	if (ret)
		return ret;

	return dp83822_config_wol(phydev, &dp83822->wol);
}

static int dp83822_phy_reset(struct phy_device *phydev)
{
	int err;

	err = phy_write(phydev, MII_DP83822_RESET_CTRL, DP83822_SW_RESET);
	if (err < 0)
		return err;

	return phydev->drv->config_init(phydev);
}

#ifdef CONFIG_OF_MDIO
static int dp83822_of_init(struct phy_device *phydev)
{
	struct dp83822_private *dp83822 = phydev->priv;
	struct device *dev = &phydev->mdio.dev;

	/* Signal detection for the PHY is only enabled if the FX_EN and the
	 * SD_EN pins are strapped. Signal detection can only enabled if FX_EN
	 * is strapped otherwise signal detection is disabled for the PHY.
	 */
	if (dp83822->fx_enabled && dp83822->fx_sd_enable)
		dp83822->fx_signal_det_low = device_property_present(dev,
								     "ti,link-loss-low");
	if (!dp83822->fx_enabled)
		dp83822->fx_enabled = device_property_present(dev,
							      "ti,fiber-mode");

	return 0;
}

static int dp83826_to_dac_minus_one_regval(int percent)
{
	int tmp = DP83826_CFG_DAC_PERCENT_DEFAULT - percent;

	return tmp / DP83826_CFG_DAC_PERCENT_PER_STEP;
}

static int dp83826_to_dac_plus_one_regval(int percent)
{
	int tmp = percent - DP83826_CFG_DAC_PERCENT_DEFAULT;

	return tmp / DP83826_CFG_DAC_PERCENT_PER_STEP;
}

static void dp83826_of_init(struct phy_device *phydev)
{
	struct dp83822_private *dp83822 = phydev->priv;
	struct device *dev = &phydev->mdio.dev;
	u32 val;

	dp83822->cfg_dac_minus = DP83826_CFG_DAC_MINUS_DEFAULT;
	if (!device_property_read_u32(dev, "ti,cfg-dac-minus-one-bp", &val))
		dp83822->cfg_dac_minus += dp83826_to_dac_minus_one_regval(val);

	dp83822->cfg_dac_plus = DP83826_CFG_DAC_PLUS_DEFAULT;
	if (!device_property_read_u32(dev, "ti,cfg-dac-plus-one-bp", &val))
		dp83822->cfg_dac_plus += dp83826_to_dac_plus_one_regval(val);
}
#else
static int dp83822_of_init(struct phy_device *phydev)
{
	return 0;
}

static void dp83826_of_init(struct phy_device *phydev)
{
}
#endif /* CONFIG_OF_MDIO */

static int dp83822_read_straps(struct phy_device *phydev)
{
	struct dp83822_private *dp83822 = phydev->priv;
	int fx_enabled, fx_sd_enable;
	int val;

	val = phy_read_mmd(phydev, DP83822_DEVADDR, MII_DP83822_SOR1);
	if (val < 0)
		return val;

	phydev_dbg(phydev, "SOR1 strap register: 0x%04x\n", val);

	fx_enabled = (val & DP83822_COL_STRAP_MASK) >> DP83822_COL_SHIFT;
	if (fx_enabled == DP83822_STRAP_MODE2 ||
	    fx_enabled == DP83822_STRAP_MODE3)
		dp83822->fx_enabled = 1;

	if (dp83822->fx_enabled) {
		fx_sd_enable = (val & DP83822_RX_ER_STR_MASK) >> DP83822_RX_ER_SHIFT;
		if (fx_sd_enable == DP83822_STRAP_MODE3 ||
		    fx_sd_enable == DP83822_STRAP_MODE4)
			dp83822->fx_sd_enable = 1;
	}

	return 0;
}

static int dp8382x_probe(struct phy_device *phydev)
{
	struct dp83822_private *dp83822;

	dp83822 = devm_kzalloc(&phydev->mdio.dev, sizeof(*dp83822),
			       GFP_KERNEL);
	if (!dp83822)
		return -ENOMEM;

	phydev->priv = dp83822;

	return 0;
}

static int dp83822_probe(struct phy_device *phydev)
{
	struct dp83822_private *dp83822;
	int ret;

	ret = dp8382x_probe(phydev);
	if (ret)
		return ret;

	dp83822 = phydev->priv;

	ret = dp83822_read_straps(phydev);
	if (ret)
		return ret;

	dp83822_of_init(phydev);

	if (dp83822->fx_enabled)
		phydev->port = PORT_FIBRE;

	return 0;
}

static int dp83826_probe(struct phy_device *phydev)
{
	int ret;

	ret = dp8382x_probe(phydev);
	if (ret)
		return ret;

	dp83826_of_init(phydev);

	return 0;
}

static int dp83822_suspend(struct phy_device *phydev)
{
	int value;

	value = phy_read_mmd(phydev, DP83822_DEVADDR, MII_DP83822_WOL_CFG);

	if (!(value & DP83822_WOL_EN))
		genphy_suspend(phydev);

	return 0;
}

static int dp83822_resume(struct phy_device *phydev)
{
	int value;

	genphy_resume(phydev);

	value = phy_read_mmd(phydev, DP83822_DEVADDR, MII_DP83822_WOL_CFG);

	phy_write_mmd(phydev, DP83822_DEVADDR, MII_DP83822_WOL_CFG, value |
		      DP83822_WOL_CLR_INDICATION);

	return 0;
}

#define DP83822_PHY_DRIVER(_id, _name)				\
	{							\
		PHY_ID_MATCH_MODEL(_id),			\
		.name		= (_name),			\
		/* PHY_BASIC_FEATURES */			\
		.probe          = dp83822_probe,		\
		.soft_reset	= dp83822_phy_reset,		\
		.config_init	= dp83822_config_init,		\
		.read_status	= dp83822_read_status,		\
		.get_wol = dp83822_get_wol,			\
		.set_wol = dp83822_set_wol,			\
		.config_intr = dp83822_config_intr,		\
		.handle_interrupt = dp83822_handle_interrupt,	\
		.suspend = dp83822_suspend,			\
		.resume = dp83822_resume,			\
	}

#define DP83825_PHY_DRIVER(_id, _name)				\
	{							\
		PHY_ID_MATCH_MODEL(_id),			\
		.name		= (_name),			\
		/* PHY_BASIC_FEATURES */			\
		.probe          = dp8382x_probe,		\
		.soft_reset	= dp83822_phy_reset,		\
		.config_init	= dp83825_config_init,		\
		.get_wol = dp83822_get_wol,			\
		.set_wol = dp83822_set_wol,			\
		.config_intr = dp83822_config_intr,		\
		.handle_interrupt = dp83822_handle_interrupt,	\
		.suspend = dp83822_suspend,			\
		.resume = dp83822_resume,			\
	}

#define DP83826_PHY_DRIVER(_id, _name)				\
	{							\
		PHY_ID_MATCH_MODEL(_id),			\
		.name		= (_name),			\
		/* PHY_BASIC_FEATURES */			\
		.probe          = dp83826_probe,		\
		.soft_reset	= dp83822_phy_reset,		\
		.config_init	= dp83826_config_init,		\
		.get_wol = dp83822_get_wol,			\
		.set_wol = dp83822_set_wol,			\
		.config_intr = dp83822_config_intr,		\
		.handle_interrupt = dp83822_handle_interrupt,	\
		.suspend = dp83822_suspend,			\
		.resume = dp83822_resume,			\
	}

static struct phy_driver dp83822_driver[] = {
	DP83822_PHY_DRIVER(DP83822_PHY_ID, "TI DP83822"),
	DP83825_PHY_DRIVER(DP83825I_PHY_ID, "TI DP83825I"),
	DP83825_PHY_DRIVER(DP83825S_PHY_ID, "TI DP83825S"),
	DP83825_PHY_DRIVER(DP83825CM_PHY_ID, "TI DP83825M"),
	DP83825_PHY_DRIVER(DP83825CS_PHY_ID, "TI DP83825CS"),
	DP83826_PHY_DRIVER(DP83826C_PHY_ID, "TI DP83826C"),
	DP83826_PHY_DRIVER(DP83826NC_PHY_ID, "TI DP83826NC"),
};
module_phy_driver(dp83822_driver);

static struct mdio_device_id __maybe_unused dp83822_tbl[] = {
	{ DP83822_PHY_ID, 0xfffffff0 },
	{ DP83825I_PHY_ID, 0xfffffff0 },
	{ DP83826C_PHY_ID, 0xfffffff0 },
	{ DP83826NC_PHY_ID, 0xfffffff0 },
	{ DP83825S_PHY_ID, 0xfffffff0 },
	{ DP83825CM_PHY_ID, 0xfffffff0 },
	{ DP83825CS_PHY_ID, 0xfffffff0 },
	{ },
};
MODULE_DEVICE_TABLE(mdio, dp83822_tbl);

MODULE_DESCRIPTION("Texas Instruments DP83822 PHY driver");
MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com");
MODULE_LICENSE("GPL v2");
