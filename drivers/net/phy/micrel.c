// SPDX-License-Identifier: GPL-2.0+
/*
 * drivers/net/phy/micrel.c
 *
 * Driver for Micrel PHYs
 *
 * Author: David J. Choi
 *
 * Copyright (c) 2010-2013 Micrel, Inc.
 * Copyright (c) 2014 Johan Hovold <johan@kernel.org>
 *
 * Support : Micrel Phys:
 *		Giga phys: ksz9021, ksz9031, ksz9131
 *		100/10 Phys : ksz8001, ksz8721, ksz8737, ksz8041
 *			   ksz8021, ksz8031, ksz8051,
 *			   ksz8081, ksz8091,
 *			   ksz8061,
 *		Switch : ksz8873, ksz886x
 *			 ksz9477
 */

#include <linux/bitfield.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/micrel_phy.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/delay.h>

/* Operation Mode Strap Override */
#define MII_KSZPHY_OMSO				0x16
#define KSZPHY_OMSO_FACTORY_TEST		BIT(15)
#define KSZPHY_OMSO_B_CAST_OFF			BIT(9)
#define KSZPHY_OMSO_NAND_TREE_ON		BIT(5)
#define KSZPHY_OMSO_RMII_OVERRIDE		BIT(1)
#define KSZPHY_OMSO_MII_OVERRIDE		BIT(0)

/* general Interrupt control/status reg in vendor specific block. */
#define MII_KSZPHY_INTCS			0x1B
#define	KSZPHY_INTCS_JABBER			BIT(15)
#define	KSZPHY_INTCS_RECEIVE_ERR		BIT(14)
#define	KSZPHY_INTCS_PAGE_RECEIVE		BIT(13)
#define	KSZPHY_INTCS_PARELLEL			BIT(12)
#define	KSZPHY_INTCS_LINK_PARTNER_ACK		BIT(11)
#define	KSZPHY_INTCS_LINK_DOWN			BIT(10)
#define	KSZPHY_INTCS_REMOTE_FAULT		BIT(9)
#define	KSZPHY_INTCS_LINK_UP			BIT(8)
#define	KSZPHY_INTCS_ALL			(KSZPHY_INTCS_LINK_UP |\
						KSZPHY_INTCS_LINK_DOWN)

/* PHY Control 1 */
#define	MII_KSZPHY_CTRL_1			0x1e

/* PHY Control 2 / PHY Control (if no PHY Control 1) */
#define	MII_KSZPHY_CTRL_2			0x1f
#define	MII_KSZPHY_CTRL				MII_KSZPHY_CTRL_2
/* bitmap of PHY register to set interrupt mode */
#define KSZPHY_CTRL_INT_ACTIVE_HIGH		BIT(9)
#define KSZPHY_RMII_REF_CLK_SEL			BIT(7)

/* Write/read to/from extended registers */
#define MII_KSZPHY_EXTREG                       0x0b
#define KSZPHY_EXTREG_WRITE                     0x8000

#define MII_KSZPHY_EXTREG_WRITE                 0x0c
#define MII_KSZPHY_EXTREG_READ                  0x0d

/* Extended registers */
#define MII_KSZPHY_CLK_CONTROL_PAD_SKEW         0x104
#define MII_KSZPHY_RX_DATA_PAD_SKEW             0x105
#define MII_KSZPHY_TX_DATA_PAD_SKEW             0x106

#define PS_TO_REG				200

struct kszphy_hw_stat {
	const char *string;
	u8 reg;
	u8 bits;
};

static struct kszphy_hw_stat kszphy_hw_stats[] = {
	{ "phy_receive_errors", 21, 16},
	{ "phy_idle_errors", 10, 8 },
};

struct kszphy_type {
	u32 led_mode_reg;
	u16 interrupt_level_mask;
	bool has_broadcast_disable;
	bool has_nand_tree_disable;
	bool has_rmii_ref_clk_sel;
};

struct kszphy_priv {
	const struct kszphy_type *type;
	int led_mode;
	bool rmii_ref_clk_sel;
	bool rmii_ref_clk_sel_val;
	u64 stats[ARRAY_SIZE(kszphy_hw_stats)];
};

static const struct kszphy_type ksz8021_type = {
	.led_mode_reg		= MII_KSZPHY_CTRL_2,
	.has_broadcast_disable	= true,
	.has_nand_tree_disable	= true,
	.has_rmii_ref_clk_sel	= true,
};

static const struct kszphy_type ksz8041_type = {
	.led_mode_reg		= MII_KSZPHY_CTRL_1,
};

static const struct kszphy_type ksz8051_type = {
	.led_mode_reg		= MII_KSZPHY_CTRL_2,
	.has_nand_tree_disable	= true,
};

static const struct kszphy_type ksz8081_type = {
	.led_mode_reg		= MII_KSZPHY_CTRL_2,
	.has_broadcast_disable	= true,
	.has_nand_tree_disable	= true,
	.has_rmii_ref_clk_sel	= true,
};

static const struct kszphy_type ks8737_type = {
	.interrupt_level_mask	= BIT(14),
};

static const struct kszphy_type ksz9021_type = {
	.interrupt_level_mask	= BIT(14),
};

static int kszphy_extended_write(struct phy_device *phydev,
				u32 regnum, u16 val)
{
	phy_write(phydev, MII_KSZPHY_EXTREG, KSZPHY_EXTREG_WRITE | regnum);
	return phy_write(phydev, MII_KSZPHY_EXTREG_WRITE, val);
}

static int kszphy_extended_read(struct phy_device *phydev,
				u32 regnum)
{
	phy_write(phydev, MII_KSZPHY_EXTREG, regnum);
	return phy_read(phydev, MII_KSZPHY_EXTREG_READ);
}

static int kszphy_ack_interrupt(struct phy_device *phydev)
{
	/* bit[7..0] int status, which is a read and clear register. */
	int rc;

	rc = phy_read(phydev, MII_KSZPHY_INTCS);

	return (rc < 0) ? rc : 0;
}

static int kszphy_config_intr(struct phy_device *phydev)
{
	const struct kszphy_type *type = phydev->drv->driver_data;
	int temp;
	u16 mask;

	if (type && type->interrupt_level_mask)
		mask = type->interrupt_level_mask;
	else
		mask = KSZPHY_CTRL_INT_ACTIVE_HIGH;

	/* set the interrupt pin active low */
	temp = phy_read(phydev, MII_KSZPHY_CTRL);
	if (temp < 0)
		return temp;
	temp &= ~mask;
	phy_write(phydev, MII_KSZPHY_CTRL, temp);

	/* enable / disable interrupts */
	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		temp = KSZPHY_INTCS_ALL;
	else
		temp = 0;

	return phy_write(phydev, MII_KSZPHY_INTCS, temp);
}

static int kszphy_rmii_clk_sel(struct phy_device *phydev, bool val)
{
	int ctrl;

	ctrl = phy_read(phydev, MII_KSZPHY_CTRL);
	if (ctrl < 0)
		return ctrl;

	if (val)
		ctrl |= KSZPHY_RMII_REF_CLK_SEL;
	else
		ctrl &= ~KSZPHY_RMII_REF_CLK_SEL;

	return phy_write(phydev, MII_KSZPHY_CTRL, ctrl);
}

static int kszphy_setup_led(struct phy_device *phydev, u32 reg, int val)
{
	int rc, temp, shift;

	switch (reg) {
	case MII_KSZPHY_CTRL_1:
		shift = 14;
		break;
	case MII_KSZPHY_CTRL_2:
		shift = 4;
		break;
	default:
		return -EINVAL;
	}

	temp = phy_read(phydev, reg);
	if (temp < 0) {
		rc = temp;
		goto out;
	}

	temp &= ~(3 << shift);
	temp |= val << shift;
	rc = phy_write(phydev, reg, temp);
out:
	if (rc < 0)
		phydev_err(phydev, "failed to set led mode\n");

	return rc;
}

/* Disable PHY address 0 as the broadcast address, so that it can be used as a
 * unique (non-broadcast) address on a shared bus.
 */
static int kszphy_broadcast_disable(struct phy_device *phydev)
{
	int ret;

	ret = phy_read(phydev, MII_KSZPHY_OMSO);
	if (ret < 0)
		goto out;

	ret = phy_write(phydev, MII_KSZPHY_OMSO, ret | KSZPHY_OMSO_B_CAST_OFF);
out:
	if (ret)
		phydev_err(phydev, "failed to disable broadcast address\n");

	return ret;
}

static int kszphy_nand_tree_disable(struct phy_device *phydev)
{
	int ret;

	ret = phy_read(phydev, MII_KSZPHY_OMSO);
	if (ret < 0)
		goto out;

	if (!(ret & KSZPHY_OMSO_NAND_TREE_ON))
		return 0;

	ret = phy_write(phydev, MII_KSZPHY_OMSO,
			ret & ~KSZPHY_OMSO_NAND_TREE_ON);
out:
	if (ret)
		phydev_err(phydev, "failed to disable NAND tree mode\n");

	return ret;
}

/* Some config bits need to be set again on resume, handle them here. */
static int kszphy_config_reset(struct phy_device *phydev)
{
	struct kszphy_priv *priv = phydev->priv;
	int ret;

	if (priv->rmii_ref_clk_sel) {
		ret = kszphy_rmii_clk_sel(phydev, priv->rmii_ref_clk_sel_val);
		if (ret) {
			phydev_err(phydev,
				   "failed to set rmii reference clock\n");
			return ret;
		}
	}

	if (priv->led_mode >= 0)
		kszphy_setup_led(phydev, priv->type->led_mode_reg, priv->led_mode);

	return 0;
}

static int kszphy_config_init(struct phy_device *phydev)
{
	struct kszphy_priv *priv = phydev->priv;
	const struct kszphy_type *type;

	if (!priv)
		return 0;

	type = priv->type;

	if (type->has_broadcast_disable)
		kszphy_broadcast_disable(phydev);

	if (type->has_nand_tree_disable)
		kszphy_nand_tree_disable(phydev);

	return kszphy_config_reset(phydev);
}

static int ksz8041_fiber_mode(struct phy_device *phydev)
{
	struct device_node *of_node = phydev->mdio.dev.of_node;

	return of_property_read_bool(of_node, "micrel,fiber-mode");
}

static int ksz8041_config_init(struct phy_device *phydev)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mask) = { 0, };

	/* Limit supported and advertised modes in fiber mode */
	if (ksz8041_fiber_mode(phydev)) {
		phydev->dev_flags |= MICREL_PHY_FXEN;
		linkmode_set_bit(ETHTOOL_LINK_MODE_100baseT_Full_BIT, mask);
		linkmode_set_bit(ETHTOOL_LINK_MODE_100baseT_Half_BIT, mask);

		linkmode_and(phydev->supported, phydev->supported, mask);
		linkmode_set_bit(ETHTOOL_LINK_MODE_FIBRE_BIT,
				 phydev->supported);
		linkmode_and(phydev->advertising, phydev->advertising, mask);
		linkmode_set_bit(ETHTOOL_LINK_MODE_FIBRE_BIT,
				 phydev->advertising);
		phydev->autoneg = AUTONEG_DISABLE;
	}

	return kszphy_config_init(phydev);
}

static int ksz8041_config_aneg(struct phy_device *phydev)
{
	/* Skip auto-negotiation in fiber mode */
	if (phydev->dev_flags & MICREL_PHY_FXEN) {
		phydev->speed = SPEED_100;
		return 0;
	}

	return genphy_config_aneg(phydev);
}

static int ksz8051_ksz8795_match_phy_device(struct phy_device *phydev,
					    const bool ksz_8051)
{
	int ret;

	if ((phydev->phy_id & MICREL_PHY_ID_MASK) != PHY_ID_KSZ8051)
		return 0;

	ret = phy_read(phydev, MII_BMSR);
	if (ret < 0)
		return ret;

	/* KSZ8051 PHY and KSZ8794/KSZ8795/KSZ8765 switch share the same
	 * exact PHY ID. However, they can be told apart by the extended
	 * capability registers presence. The KSZ8051 PHY has them while
	 * the switch does not.
	 */
	ret &= BMSR_ERCAP;
	if (ksz_8051)
		return ret;
	else
		return !ret;
}

static int ksz8051_match_phy_device(struct phy_device *phydev)
{
	return ksz8051_ksz8795_match_phy_device(phydev, true);
}

static int ksz8081_config_init(struct phy_device *phydev)
{
	/* KSZPHY_OMSO_FACTORY_TEST is set at de-assertion of the reset line
	 * based on the RXER (KSZ8081RNA/RND) or TXC (KSZ8081MNX/RNB) pin. If a
	 * pull-down is missing, the factory test mode should be cleared by
	 * manually writing a 0.
	 */
	phy_clear_bits(phydev, MII_KSZPHY_OMSO, KSZPHY_OMSO_FACTORY_TEST);

	return kszphy_config_init(phydev);
}

static int ksz8061_config_init(struct phy_device *phydev)
{
	int ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_DEVID1, 0xB61A);
	if (ret)
		return ret;

	return kszphy_config_init(phydev);
}

static int ksz8795_match_phy_device(struct phy_device *phydev)
{
	return ksz8051_ksz8795_match_phy_device(phydev, false);
}

static int ksz9021_load_values_from_of(struct phy_device *phydev,
				       const struct device_node *of_node,
				       u16 reg,
				       const char *field1, const char *field2,
				       const char *field3, const char *field4)
{
	int val1 = -1;
	int val2 = -2;
	int val3 = -3;
	int val4 = -4;
	int newval;
	int matches = 0;

	if (!of_property_read_u32(of_node, field1, &val1))
		matches++;

	if (!of_property_read_u32(of_node, field2, &val2))
		matches++;

	if (!of_property_read_u32(of_node, field3, &val3))
		matches++;

	if (!of_property_read_u32(of_node, field4, &val4))
		matches++;

	if (!matches)
		return 0;

	if (matches < 4)
		newval = kszphy_extended_read(phydev, reg);
	else
		newval = 0;

	if (val1 != -1)
		newval = ((newval & 0xfff0) | ((val1 / PS_TO_REG) & 0xf) << 0);

	if (val2 != -2)
		newval = ((newval & 0xff0f) | ((val2 / PS_TO_REG) & 0xf) << 4);

	if (val3 != -3)
		newval = ((newval & 0xf0ff) | ((val3 / PS_TO_REG) & 0xf) << 8);

	if (val4 != -4)
		newval = ((newval & 0x0fff) | ((val4 / PS_TO_REG) & 0xf) << 12);

	return kszphy_extended_write(phydev, reg, newval);
}

static int ksz9021_config_init(struct phy_device *phydev)
{
	const struct device *dev = &phydev->mdio.dev;
	const struct device_node *of_node = dev->of_node;
	const struct device *dev_walker;

	/* The Micrel driver has a deprecated option to place phy OF
	 * properties in the MAC node. Walk up the tree of devices to
	 * find a device with an OF node.
	 */
	dev_walker = &phydev->mdio.dev;
	do {
		of_node = dev_walker->of_node;
		dev_walker = dev_walker->parent;

	} while (!of_node && dev_walker);

	if (of_node) {
		ksz9021_load_values_from_of(phydev, of_node,
				    MII_KSZPHY_CLK_CONTROL_PAD_SKEW,
				    "txen-skew-ps", "txc-skew-ps",
				    "rxdv-skew-ps", "rxc-skew-ps");
		ksz9021_load_values_from_of(phydev, of_node,
				    MII_KSZPHY_RX_DATA_PAD_SKEW,
				    "rxd0-skew-ps", "rxd1-skew-ps",
				    "rxd2-skew-ps", "rxd3-skew-ps");
		ksz9021_load_values_from_of(phydev, of_node,
				    MII_KSZPHY_TX_DATA_PAD_SKEW,
				    "txd0-skew-ps", "txd1-skew-ps",
				    "txd2-skew-ps", "txd3-skew-ps");
	}
	return 0;
}

#define KSZ9031_PS_TO_REG		60

/* Extended registers */
/* MMD Address 0x0 */
#define MII_KSZ9031RN_FLP_BURST_TX_LO	3
#define MII_KSZ9031RN_FLP_BURST_TX_HI	4

/* MMD Address 0x2 */
#define MII_KSZ9031RN_CONTROL_PAD_SKEW	4
#define MII_KSZ9031RN_RX_CTL_M		GENMASK(7, 4)
#define MII_KSZ9031RN_TX_CTL_M		GENMASK(3, 0)

#define MII_KSZ9031RN_RX_DATA_PAD_SKEW	5
#define MII_KSZ9031RN_RXD3		GENMASK(15, 12)
#define MII_KSZ9031RN_RXD2		GENMASK(11, 8)
#define MII_KSZ9031RN_RXD1		GENMASK(7, 4)
#define MII_KSZ9031RN_RXD0		GENMASK(3, 0)

#define MII_KSZ9031RN_TX_DATA_PAD_SKEW	6
#define MII_KSZ9031RN_TXD3		GENMASK(15, 12)
#define MII_KSZ9031RN_TXD2		GENMASK(11, 8)
#define MII_KSZ9031RN_TXD1		GENMASK(7, 4)
#define MII_KSZ9031RN_TXD0		GENMASK(3, 0)

#define MII_KSZ9031RN_CLK_PAD_SKEW	8
#define MII_KSZ9031RN_GTX_CLK		GENMASK(9, 5)
#define MII_KSZ9031RN_RX_CLK		GENMASK(4, 0)

/* KSZ9031 has internal RGMII_IDRX = 1.2ns and RGMII_IDTX = 0ns. To
 * provide different RGMII options we need to configure delay offset
 * for each pad relative to build in delay.
 */
/* keep rx as "No delay adjustment" and set rx_clk to +0.60ns to get delays of
 * 1.80ns
 */
#define RX_ID				0x7
#define RX_CLK_ID			0x19

/* set rx to +0.30ns and rx_clk to -0.90ns to compensate the
 * internal 1.2ns delay.
 */
#define RX_ND				0xc
#define RX_CLK_ND			0x0

/* set tx to -0.42ns and tx_clk to +0.96ns to get 1.38ns delay */
#define TX_ID				0x0
#define TX_CLK_ID			0x1f

/* set tx and tx_clk to "No delay adjustment" to keep 0ns
 * dealy
 */
#define TX_ND				0x7
#define TX_CLK_ND			0xf

/* MMD Address 0x1C */
#define MII_KSZ9031RN_EDPD		0x23
#define MII_KSZ9031RN_EDPD_ENABLE	BIT(0)

static int ksz9031_of_load_skew_values(struct phy_device *phydev,
				       const struct device_node *of_node,
				       u16 reg, size_t field_sz,
				       const char *field[], u8 numfields,
				       bool *update)
{
	int val[4] = {-1, -2, -3, -4};
	int matches = 0;
	u16 mask;
	u16 maxval;
	u16 newval;
	int i;

	for (i = 0; i < numfields; i++)
		if (!of_property_read_u32(of_node, field[i], val + i))
			matches++;

	if (!matches)
		return 0;

	*update |= true;

	if (matches < numfields)
		newval = phy_read_mmd(phydev, 2, reg);
	else
		newval = 0;

	maxval = (field_sz == 4) ? 0xf : 0x1f;
	for (i = 0; i < numfields; i++)
		if (val[i] != -(i + 1)) {
			mask = 0xffff;
			mask ^= maxval << (field_sz * i);
			newval = (newval & mask) |
				(((val[i] / KSZ9031_PS_TO_REG) & maxval)
					<< (field_sz * i));
		}

	return phy_write_mmd(phydev, 2, reg, newval);
}

/* Center KSZ9031RNX FLP timing at 16ms. */
static int ksz9031_center_flp_timing(struct phy_device *phydev)
{
	int result;

	result = phy_write_mmd(phydev, 0, MII_KSZ9031RN_FLP_BURST_TX_HI,
			       0x0006);
	if (result)
		return result;

	result = phy_write_mmd(phydev, 0, MII_KSZ9031RN_FLP_BURST_TX_LO,
			       0x1A80);
	if (result)
		return result;

	return genphy_restart_aneg(phydev);
}

/* Enable energy-detect power-down mode */
static int ksz9031_enable_edpd(struct phy_device *phydev)
{
	int reg;

	reg = phy_read_mmd(phydev, 0x1C, MII_KSZ9031RN_EDPD);
	if (reg < 0)
		return reg;
	return phy_write_mmd(phydev, 0x1C, MII_KSZ9031RN_EDPD,
			     reg | MII_KSZ9031RN_EDPD_ENABLE);
}

static int ksz9031_config_rgmii_delay(struct phy_device *phydev)
{
	u16 rx, tx, rx_clk, tx_clk;
	int ret;

	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		tx = TX_ND;
		tx_clk = TX_CLK_ND;
		rx = RX_ND;
		rx_clk = RX_CLK_ND;
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		tx = TX_ID;
		tx_clk = TX_CLK_ID;
		rx = RX_ID;
		rx_clk = RX_CLK_ID;
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		tx = TX_ND;
		tx_clk = TX_CLK_ND;
		rx = RX_ID;
		rx_clk = RX_CLK_ID;
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		tx = TX_ID;
		tx_clk = TX_CLK_ID;
		rx = RX_ND;
		rx_clk = RX_CLK_ND;
		break;
	default:
		return 0;
	}

	ret = phy_write_mmd(phydev, 2, MII_KSZ9031RN_CONTROL_PAD_SKEW,
			    FIELD_PREP(MII_KSZ9031RN_RX_CTL_M, rx) |
			    FIELD_PREP(MII_KSZ9031RN_TX_CTL_M, tx));
	if (ret < 0)
		return ret;

	ret = phy_write_mmd(phydev, 2, MII_KSZ9031RN_RX_DATA_PAD_SKEW,
			    FIELD_PREP(MII_KSZ9031RN_RXD3, rx) |
			    FIELD_PREP(MII_KSZ9031RN_RXD2, rx) |
			    FIELD_PREP(MII_KSZ9031RN_RXD1, rx) |
			    FIELD_PREP(MII_KSZ9031RN_RXD0, rx));
	if (ret < 0)
		return ret;

	ret = phy_write_mmd(phydev, 2, MII_KSZ9031RN_TX_DATA_PAD_SKEW,
			    FIELD_PREP(MII_KSZ9031RN_TXD3, tx) |
			    FIELD_PREP(MII_KSZ9031RN_TXD2, tx) |
			    FIELD_PREP(MII_KSZ9031RN_TXD1, tx) |
			    FIELD_PREP(MII_KSZ9031RN_TXD0, tx));
	if (ret < 0)
		return ret;

	return phy_write_mmd(phydev, 2, MII_KSZ9031RN_CLK_PAD_SKEW,
			     FIELD_PREP(MII_KSZ9031RN_GTX_CLK, tx_clk) |
			     FIELD_PREP(MII_KSZ9031RN_RX_CLK, rx_clk));
}

static int ksz9031_config_init(struct phy_device *phydev)
{
	const struct device *dev = &phydev->mdio.dev;
	const struct device_node *of_node = dev->of_node;
	static const char *clk_skews[2] = {"rxc-skew-ps", "txc-skew-ps"};
	static const char *rx_data_skews[4] = {
		"rxd0-skew-ps", "rxd1-skew-ps",
		"rxd2-skew-ps", "rxd3-skew-ps"
	};
	static const char *tx_data_skews[4] = {
		"txd0-skew-ps", "txd1-skew-ps",
		"txd2-skew-ps", "txd3-skew-ps"
	};
	static const char *control_skews[2] = {"txen-skew-ps", "rxdv-skew-ps"};
	const struct device *dev_walker;
	int result;

	result = ksz9031_enable_edpd(phydev);
	if (result < 0)
		return result;

	/* The Micrel driver has a deprecated option to place phy OF
	 * properties in the MAC node. Walk up the tree of devices to
	 * find a device with an OF node.
	 */
	dev_walker = &phydev->mdio.dev;
	do {
		of_node = dev_walker->of_node;
		dev_walker = dev_walker->parent;
	} while (!of_node && dev_walker);

	if (of_node) {
		bool update = false;

		if (phy_interface_is_rgmii(phydev)) {
			result = ksz9031_config_rgmii_delay(phydev);
			if (result < 0)
				return result;
		}

		ksz9031_of_load_skew_values(phydev, of_node,
				MII_KSZ9031RN_CLK_PAD_SKEW, 5,
				clk_skews, 2, &update);

		ksz9031_of_load_skew_values(phydev, of_node,
				MII_KSZ9031RN_CONTROL_PAD_SKEW, 4,
				control_skews, 2, &update);

		ksz9031_of_load_skew_values(phydev, of_node,
				MII_KSZ9031RN_RX_DATA_PAD_SKEW, 4,
				rx_data_skews, 4, &update);

		ksz9031_of_load_skew_values(phydev, of_node,
				MII_KSZ9031RN_TX_DATA_PAD_SKEW, 4,
				tx_data_skews, 4, &update);

		if (update && phydev->interface != PHY_INTERFACE_MODE_RGMII)
			phydev_warn(phydev,
				    "*-skew-ps values should be used only with phy-mode = \"rgmii\"\n");

		/* Silicon Errata Sheet (DS80000691D or DS80000692D):
		 * When the device links in the 1000BASE-T slave mode only,
		 * the optional 125MHz reference output clock (CLK125_NDO)
		 * has wide duty cycle variation.
		 *
		 * The optional CLK125_NDO clock does not meet the RGMII
		 * 45/55 percent (min/max) duty cycle requirement and therefore
		 * cannot be used directly by the MAC side for clocking
		 * applications that have setup/hold time requirements on
		 * rising and falling clock edges.
		 *
		 * Workaround:
		 * Force the phy to be the master to receive a stable clock
		 * which meets the duty cycle requirement.
		 */
		if (of_property_read_bool(of_node, "micrel,force-master")) {
			result = phy_read(phydev, MII_CTRL1000);
			if (result < 0)
				goto err_force_master;

			/* enable master mode, config & prefer master */
			result |= CTL1000_ENABLE_MASTER | CTL1000_AS_MASTER;
			result = phy_write(phydev, MII_CTRL1000, result);
			if (result < 0)
				goto err_force_master;
		}
	}

	return ksz9031_center_flp_timing(phydev);

err_force_master:
	phydev_err(phydev, "failed to force the phy to master mode\n");
	return result;
}

#define KSZ9131_SKEW_5BIT_MAX	2400
#define KSZ9131_SKEW_4BIT_MAX	800
#define KSZ9131_OFFSET		700
#define KSZ9131_STEP		100

static int ksz9131_of_load_skew_values(struct phy_device *phydev,
				       struct device_node *of_node,
				       u16 reg, size_t field_sz,
				       char *field[], u8 numfields)
{
	int val[4] = {-(1 + KSZ9131_OFFSET), -(2 + KSZ9131_OFFSET),
		      -(3 + KSZ9131_OFFSET), -(4 + KSZ9131_OFFSET)};
	int skewval, skewmax = 0;
	int matches = 0;
	u16 maxval;
	u16 newval;
	u16 mask;
	int i;

	/* psec properties in dts should mean x pico seconds */
	if (field_sz == 5)
		skewmax = KSZ9131_SKEW_5BIT_MAX;
	else
		skewmax = KSZ9131_SKEW_4BIT_MAX;

	for (i = 0; i < numfields; i++)
		if (!of_property_read_s32(of_node, field[i], &skewval)) {
			if (skewval < -KSZ9131_OFFSET)
				skewval = -KSZ9131_OFFSET;
			else if (skewval > skewmax)
				skewval = skewmax;

			val[i] = skewval + KSZ9131_OFFSET;
			matches++;
		}

	if (!matches)
		return 0;

	if (matches < numfields)
		newval = phy_read_mmd(phydev, 2, reg);
	else
		newval = 0;

	maxval = (field_sz == 4) ? 0xf : 0x1f;
	for (i = 0; i < numfields; i++)
		if (val[i] != -(i + 1 + KSZ9131_OFFSET)) {
			mask = 0xffff;
			mask ^= maxval << (field_sz * i);
			newval = (newval & mask) |
				(((val[i] / KSZ9131_STEP) & maxval)
					<< (field_sz * i));
		}

	return phy_write_mmd(phydev, 2, reg, newval);
}

#define KSZ9131RN_MMD_COMMON_CTRL_REG	2
#define KSZ9131RN_RXC_DLL_CTRL		76
#define KSZ9131RN_TXC_DLL_CTRL		77
#define KSZ9131RN_DLL_CTRL_BYPASS	BIT_MASK(12)
#define KSZ9131RN_DLL_ENABLE_DELAY	0
#define KSZ9131RN_DLL_DISABLE_DELAY	BIT(12)

static int ksz9131_config_rgmii_delay(struct phy_device *phydev)
{
	u16 rxcdll_val, txcdll_val;
	int ret;

	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		rxcdll_val = KSZ9131RN_DLL_DISABLE_DELAY;
		txcdll_val = KSZ9131RN_DLL_DISABLE_DELAY;
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		rxcdll_val = KSZ9131RN_DLL_ENABLE_DELAY;
		txcdll_val = KSZ9131RN_DLL_ENABLE_DELAY;
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		rxcdll_val = KSZ9131RN_DLL_ENABLE_DELAY;
		txcdll_val = KSZ9131RN_DLL_DISABLE_DELAY;
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		rxcdll_val = KSZ9131RN_DLL_DISABLE_DELAY;
		txcdll_val = KSZ9131RN_DLL_ENABLE_DELAY;
		break;
	default:
		return 0;
	}

	ret = phy_modify_mmd(phydev, KSZ9131RN_MMD_COMMON_CTRL_REG,
			     KSZ9131RN_RXC_DLL_CTRL, KSZ9131RN_DLL_CTRL_BYPASS,
			     rxcdll_val);
	if (ret < 0)
		return ret;

	return phy_modify_mmd(phydev, KSZ9131RN_MMD_COMMON_CTRL_REG,
			      KSZ9131RN_TXC_DLL_CTRL, KSZ9131RN_DLL_CTRL_BYPASS,
			      txcdll_val);
}

static int ksz9131_config_init(struct phy_device *phydev)
{
	const struct device *dev = &phydev->mdio.dev;
	struct device_node *of_node = dev->of_node;
	char *clk_skews[2] = {"rxc-skew-psec", "txc-skew-psec"};
	char *rx_data_skews[4] = {
		"rxd0-skew-psec", "rxd1-skew-psec",
		"rxd2-skew-psec", "rxd3-skew-psec"
	};
	char *tx_data_skews[4] = {
		"txd0-skew-psec", "txd1-skew-psec",
		"txd2-skew-psec", "txd3-skew-psec"
	};
	char *control_skews[2] = {"txen-skew-psec", "rxdv-skew-psec"};
	const struct device *dev_walker;
	int ret;

	dev_walker = &phydev->mdio.dev;
	do {
		of_node = dev_walker->of_node;
		dev_walker = dev_walker->parent;
	} while (!of_node && dev_walker);

	if (!of_node)
		return 0;

	if (phy_interface_is_rgmii(phydev)) {
		ret = ksz9131_config_rgmii_delay(phydev);
		if (ret < 0)
			return ret;
	}

	ret = ksz9131_of_load_skew_values(phydev, of_node,
					  MII_KSZ9031RN_CLK_PAD_SKEW, 5,
					  clk_skews, 2);
	if (ret < 0)
		return ret;

	ret = ksz9131_of_load_skew_values(phydev, of_node,
					  MII_KSZ9031RN_CONTROL_PAD_SKEW, 4,
					  control_skews, 2);
	if (ret < 0)
		return ret;

	ret = ksz9131_of_load_skew_values(phydev, of_node,
					  MII_KSZ9031RN_RX_DATA_PAD_SKEW, 4,
					  rx_data_skews, 4);
	if (ret < 0)
		return ret;

	ret = ksz9131_of_load_skew_values(phydev, of_node,
					  MII_KSZ9031RN_TX_DATA_PAD_SKEW, 4,
					  tx_data_skews, 4);
	if (ret < 0)
		return ret;

	return 0;
}

#define KSZ8873MLL_GLOBAL_CONTROL_4	0x06
#define KSZ8873MLL_GLOBAL_CONTROL_4_DUPLEX	BIT(6)
#define KSZ8873MLL_GLOBAL_CONTROL_4_SPEED	BIT(4)
static int ksz8873mll_read_status(struct phy_device *phydev)
{
	int regval;

	/* dummy read */
	regval = phy_read(phydev, KSZ8873MLL_GLOBAL_CONTROL_4);

	regval = phy_read(phydev, KSZ8873MLL_GLOBAL_CONTROL_4);

	if (regval & KSZ8873MLL_GLOBAL_CONTROL_4_DUPLEX)
		phydev->duplex = DUPLEX_HALF;
	else
		phydev->duplex = DUPLEX_FULL;

	if (regval & KSZ8873MLL_GLOBAL_CONTROL_4_SPEED)
		phydev->speed = SPEED_10;
	else
		phydev->speed = SPEED_100;

	phydev->link = 1;
	phydev->pause = phydev->asym_pause = 0;

	return 0;
}

static int ksz9031_get_features(struct phy_device *phydev)
{
	int ret;

	ret = genphy_read_abilities(phydev);
	if (ret < 0)
		return ret;

	/* Silicon Errata Sheet (DS80000691D or DS80000692D):
	 * Whenever the device's Asymmetric Pause capability is set to 1,
	 * link-up may fail after a link-up to link-down transition.
	 *
	 * The Errata Sheet is for ksz9031, but ksz9021 has the same issue
	 *
	 * Workaround:
	 * Do not enable the Asymmetric Pause capability bit.
	 */
	linkmode_clear_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT, phydev->supported);

	/* We force setting the Pause capability as the core will force the
	 * Asymmetric Pause capability to 1 otherwise.
	 */
	linkmode_set_bit(ETHTOOL_LINK_MODE_Pause_BIT, phydev->supported);

	return 0;
}

static int ksz9031_read_status(struct phy_device *phydev)
{
	int err;
	int regval;

	err = genphy_read_status(phydev);
	if (err)
		return err;

	/* Make sure the PHY is not broken. Read idle error count,
	 * and reset the PHY if it is maxed out.
	 */
	regval = phy_read(phydev, MII_STAT1000);
	if ((regval & 0xFF) == 0xFF) {
		phy_init_hw(phydev);
		phydev->link = 0;
		if (phydev->drv->config_intr && phy_interrupt_is_valid(phydev))
			phydev->drv->config_intr(phydev);
		return genphy_config_aneg(phydev);
	}

	return 0;
}

static int ksz8873mll_config_aneg(struct phy_device *phydev)
{
	return 0;
}

static int kszphy_get_sset_count(struct phy_device *phydev)
{
	return ARRAY_SIZE(kszphy_hw_stats);
}

static void kszphy_get_strings(struct phy_device *phydev, u8 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(kszphy_hw_stats); i++) {
		strlcpy(data + i * ETH_GSTRING_LEN,
			kszphy_hw_stats[i].string, ETH_GSTRING_LEN);
	}
}

static u64 kszphy_get_stat(struct phy_device *phydev, int i)
{
	struct kszphy_hw_stat stat = kszphy_hw_stats[i];
	struct kszphy_priv *priv = phydev->priv;
	int val;
	u64 ret;

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

static void kszphy_get_stats(struct phy_device *phydev,
			     struct ethtool_stats *stats, u64 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(kszphy_hw_stats); i++)
		data[i] = kszphy_get_stat(phydev, i);
}

static int kszphy_suspend(struct phy_device *phydev)
{
	/* Disable PHY Interrupts */
	if (phy_interrupt_is_valid(phydev)) {
		phydev->interrupts = PHY_INTERRUPT_DISABLED;
		if (phydev->drv->config_intr)
			phydev->drv->config_intr(phydev);
	}

	return genphy_suspend(phydev);
}

static int kszphy_resume(struct phy_device *phydev)
{
	int ret;

	genphy_resume(phydev);

	/* After switching from power-down to normal mode, an internal global
	 * reset is automatically generated. Wait a minimum of 1 ms before
	 * read/write access to the PHY registers.
	 */
	usleep_range(1000, 2000);

	ret = kszphy_config_reset(phydev);
	if (ret)
		return ret;

	/* Enable PHY Interrupts */
	if (phy_interrupt_is_valid(phydev)) {
		phydev->interrupts = PHY_INTERRUPT_ENABLED;
		if (phydev->drv->config_intr)
			phydev->drv->config_intr(phydev);
	}

	return 0;
}

static int kszphy_probe(struct phy_device *phydev)
{
	const struct kszphy_type *type = phydev->drv->driver_data;
	const struct device_node *np = phydev->mdio.dev.of_node;
	struct kszphy_priv *priv;
	struct clk *clk;
	int ret;

	priv = devm_kzalloc(&phydev->mdio.dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	priv->type = type;

	if (type->led_mode_reg) {
		ret = of_property_read_u32(np, "micrel,led-mode",
				&priv->led_mode);
		if (ret)
			priv->led_mode = -1;

		if (priv->led_mode > 3) {
			phydev_err(phydev, "invalid led mode: 0x%02x\n",
				   priv->led_mode);
			priv->led_mode = -1;
		}
	} else {
		priv->led_mode = -1;
	}

	clk = devm_clk_get(&phydev->mdio.dev, "rmii-ref");
	/* NOTE: clk may be NULL if building without CONFIG_HAVE_CLK */
	if (!IS_ERR_OR_NULL(clk)) {
		unsigned long rate = clk_get_rate(clk);
		bool rmii_ref_clk_sel_25_mhz;

		priv->rmii_ref_clk_sel = type->has_rmii_ref_clk_sel;
		rmii_ref_clk_sel_25_mhz = of_property_read_bool(np,
				"micrel,rmii-reference-clock-select-25-mhz");

		if (rate > 24500000 && rate < 25500000) {
			priv->rmii_ref_clk_sel_val = rmii_ref_clk_sel_25_mhz;
		} else if (rate > 49500000 && rate < 50500000) {
			priv->rmii_ref_clk_sel_val = !rmii_ref_clk_sel_25_mhz;
		} else {
			phydev_err(phydev, "Clock rate out of range: %ld\n",
				   rate);
			return -EINVAL;
		}
	}

	if (ksz8041_fiber_mode(phydev))
		phydev->port = PORT_FIBRE;

	/* Support legacy board-file configuration */
	if (phydev->dev_flags & MICREL_PHY_50MHZ_CLK) {
		priv->rmii_ref_clk_sel = true;
		priv->rmii_ref_clk_sel_val = true;
	}

	return 0;
}

static struct phy_driver ksphy_driver[] = {
{
	.phy_id		= PHY_ID_KS8737,
	.phy_id_mask	= MICREL_PHY_ID_MASK,
	.name		= "Micrel KS8737",
	/* PHY_BASIC_FEATURES */
	.driver_data	= &ks8737_type,
	.config_init	= kszphy_config_init,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	.phy_id		= PHY_ID_KSZ8021,
	.phy_id_mask	= 0x00ffffff,
	.name		= "Micrel KSZ8021 or KSZ8031",
	/* PHY_BASIC_FEATURES */
	.driver_data	= &ksz8021_type,
	.probe		= kszphy_probe,
	.config_init	= kszphy_config_init,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.get_sset_count = kszphy_get_sset_count,
	.get_strings	= kszphy_get_strings,
	.get_stats	= kszphy_get_stats,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	.phy_id		= PHY_ID_KSZ8031,
	.phy_id_mask	= 0x00ffffff,
	.name		= "Micrel KSZ8031",
	/* PHY_BASIC_FEATURES */
	.driver_data	= &ksz8021_type,
	.probe		= kszphy_probe,
	.config_init	= kszphy_config_init,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.get_sset_count = kszphy_get_sset_count,
	.get_strings	= kszphy_get_strings,
	.get_stats	= kszphy_get_stats,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	.phy_id		= PHY_ID_KSZ8041,
	.phy_id_mask	= MICREL_PHY_ID_MASK,
	.name		= "Micrel KSZ8041",
	/* PHY_BASIC_FEATURES */
	.driver_data	= &ksz8041_type,
	.probe		= kszphy_probe,
	.config_init	= ksz8041_config_init,
	.config_aneg	= ksz8041_config_aneg,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.get_sset_count = kszphy_get_sset_count,
	.get_strings	= kszphy_get_strings,
	.get_stats	= kszphy_get_stats,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	.phy_id		= PHY_ID_KSZ8041RNLI,
	.phy_id_mask	= MICREL_PHY_ID_MASK,
	.name		= "Micrel KSZ8041RNLI",
	/* PHY_BASIC_FEATURES */
	.driver_data	= &ksz8041_type,
	.probe		= kszphy_probe,
	.config_init	= kszphy_config_init,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.get_sset_count = kszphy_get_sset_count,
	.get_strings	= kszphy_get_strings,
	.get_stats	= kszphy_get_stats,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	.name		= "Micrel KSZ8051",
	/* PHY_BASIC_FEATURES */
	.driver_data	= &ksz8051_type,
	.probe		= kszphy_probe,
	.config_init	= kszphy_config_init,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.get_sset_count = kszphy_get_sset_count,
	.get_strings	= kszphy_get_strings,
	.get_stats	= kszphy_get_stats,
	.match_phy_device = ksz8051_match_phy_device,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	.phy_id		= PHY_ID_KSZ8001,
	.name		= "Micrel KSZ8001 or KS8721",
	.phy_id_mask	= 0x00fffffc,
	/* PHY_BASIC_FEATURES */
	.driver_data	= &ksz8041_type,
	.probe		= kszphy_probe,
	.config_init	= kszphy_config_init,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.get_sset_count = kszphy_get_sset_count,
	.get_strings	= kszphy_get_strings,
	.get_stats	= kszphy_get_stats,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	.phy_id		= PHY_ID_KSZ8081,
	.name		= "Micrel KSZ8081 or KSZ8091",
	.phy_id_mask	= MICREL_PHY_ID_MASK,
	/* PHY_BASIC_FEATURES */
	.driver_data	= &ksz8081_type,
	.probe		= kszphy_probe,
	.config_init	= ksz8081_config_init,
	.ack_interrupt	= kszphy_ack_interrupt,
	.soft_reset	= genphy_soft_reset,
	.config_intr	= kszphy_config_intr,
	.get_sset_count = kszphy_get_sset_count,
	.get_strings	= kszphy_get_strings,
	.get_stats	= kszphy_get_stats,
	.suspend	= kszphy_suspend,
	.resume		= kszphy_resume,
}, {
	.phy_id		= PHY_ID_KSZ8061,
	.name		= "Micrel KSZ8061",
	.phy_id_mask	= MICREL_PHY_ID_MASK,
	/* PHY_BASIC_FEATURES */
	.config_init	= ksz8061_config_init,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	.phy_id		= PHY_ID_KSZ9021,
	.phy_id_mask	= 0x000ffffe,
	.name		= "Micrel KSZ9021 Gigabit PHY",
	/* PHY_GBIT_FEATURES */
	.driver_data	= &ksz9021_type,
	.probe		= kszphy_probe,
	.get_features	= ksz9031_get_features,
	.config_init	= ksz9021_config_init,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.get_sset_count = kszphy_get_sset_count,
	.get_strings	= kszphy_get_strings,
	.get_stats	= kszphy_get_stats,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.read_mmd	= genphy_read_mmd_unsupported,
	.write_mmd	= genphy_write_mmd_unsupported,
}, {
	.phy_id		= PHY_ID_KSZ9031,
	.phy_id_mask	= MICREL_PHY_ID_MASK,
	.name		= "Micrel KSZ9031 Gigabit PHY",
	.driver_data	= &ksz9021_type,
	.probe		= kszphy_probe,
	.get_features	= ksz9031_get_features,
	.config_init	= ksz9031_config_init,
	.soft_reset	= genphy_soft_reset,
	.read_status	= ksz9031_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.get_sset_count = kszphy_get_sset_count,
	.get_strings	= kszphy_get_strings,
	.get_stats	= kszphy_get_stats,
	.suspend	= genphy_suspend,
	.resume		= kszphy_resume,
}, {
	.phy_id		= PHY_ID_LAN8814,
	.phy_id_mask	= MICREL_PHY_ID_MASK,
	.name		= "Microchip INDY Gigabit Quad PHY",
	.driver_data	= &ksz9021_type,
	.probe		= kszphy_probe,
	.soft_reset	= genphy_soft_reset,
	.read_status	= ksz9031_read_status,
	.get_sset_count	= kszphy_get_sset_count,
	.get_strings	= kszphy_get_strings,
	.get_stats	= kszphy_get_stats,
	.suspend	= genphy_suspend,
	.resume		= kszphy_resume,
}, {
	.phy_id		= PHY_ID_KSZ9131,
	.phy_id_mask	= MICREL_PHY_ID_MASK,
	.name		= "Microchip KSZ9131 Gigabit PHY",
	/* PHY_GBIT_FEATURES */
	.driver_data	= &ksz9021_type,
	.probe		= kszphy_probe,
	.config_init	= ksz9131_config_init,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.get_sset_count = kszphy_get_sset_count,
	.get_strings	= kszphy_get_strings,
	.get_stats	= kszphy_get_stats,
	.suspend	= genphy_suspend,
	.resume		= kszphy_resume,
}, {
	.phy_id		= PHY_ID_KSZ8873MLL,
	.phy_id_mask	= MICREL_PHY_ID_MASK,
	.name		= "Micrel KSZ8873MLL Switch",
	/* PHY_BASIC_FEATURES */
	.config_init	= kszphy_config_init,
	.config_aneg	= ksz8873mll_config_aneg,
	.read_status	= ksz8873mll_read_status,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	.phy_id		= PHY_ID_KSZ886X,
	.phy_id_mask	= MICREL_PHY_ID_MASK,
	.name		= "Micrel KSZ886X Switch",
	/* PHY_BASIC_FEATURES */
	.config_init	= kszphy_config_init,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	.name		= "Micrel KSZ87XX Switch",
	/* PHY_BASIC_FEATURES */
	.config_init	= kszphy_config_init,
	.config_aneg	= ksz8873mll_config_aneg,
	.read_status	= ksz8873mll_read_status,
	.match_phy_device = ksz8795_match_phy_device,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
}, {
	.phy_id		= PHY_ID_KSZ9477,
	.phy_id_mask	= MICREL_PHY_ID_MASK,
	.name		= "Microchip KSZ9477",
	/* PHY_GBIT_FEATURES */
	.config_init	= kszphy_config_init,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
} };

module_phy_driver(ksphy_driver);

MODULE_DESCRIPTION("Micrel PHY driver");
MODULE_AUTHOR("David J. Choi");
MODULE_LICENSE("GPL");

static struct mdio_device_id __maybe_unused micrel_tbl[] = {
	{ PHY_ID_KSZ9021, 0x000ffffe },
	{ PHY_ID_KSZ9031, MICREL_PHY_ID_MASK },
	{ PHY_ID_KSZ9131, MICREL_PHY_ID_MASK },
	{ PHY_ID_KSZ8001, 0x00fffffc },
	{ PHY_ID_KS8737, MICREL_PHY_ID_MASK },
	{ PHY_ID_KSZ8021, 0x00ffffff },
	{ PHY_ID_KSZ8031, 0x00ffffff },
	{ PHY_ID_KSZ8041, MICREL_PHY_ID_MASK },
	{ PHY_ID_KSZ8051, MICREL_PHY_ID_MASK },
	{ PHY_ID_KSZ8061, MICREL_PHY_ID_MASK },
	{ PHY_ID_KSZ8081, MICREL_PHY_ID_MASK },
	{ PHY_ID_KSZ8873MLL, MICREL_PHY_ID_MASK },
	{ PHY_ID_KSZ886X, MICREL_PHY_ID_MASK },
	{ PHY_ID_LAN8814, MICREL_PHY_ID_MASK },
	{ }
};

MODULE_DEVICE_TABLE(mdio, micrel_tbl);
