// SPDX-License-Identifier: GPL-2.0
/* Driver for the Texas Instruments DP83867 PHY
 *
 * Copyright (C) 2015 Texas Instruments Inc.
 */

#include <linux/ethtool.h>
#include <linux/kernel.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/bitfield.h>

#include <dt-bindings/net/ti-dp83867.h>

#define DP83867_PHY_ID		0x2000a231
#define DP83867_DEVADDR		0x1f

#define MII_DP83867_PHYCTRL	0x10
#define MII_DP83867_PHYSTS	0x11
#define MII_DP83867_MICR	0x12
#define MII_DP83867_ISR		0x13
#define DP83867_CFG2		0x14
#define DP83867_CFG3		0x1e
#define DP83867_CTRL		0x1f

/* Extended Registers */
#define DP83867_FLD_THR_CFG	0x002e
#define DP83867_CFG4		0x0031
#define DP83867_CFG4_SGMII_ANEG_MASK (BIT(5) | BIT(6))
#define DP83867_CFG4_SGMII_ANEG_TIMER_11MS   (3 << 5)
#define DP83867_CFG4_SGMII_ANEG_TIMER_800US  (2 << 5)
#define DP83867_CFG4_SGMII_ANEG_TIMER_2US    (1 << 5)
#define DP83867_CFG4_SGMII_ANEG_TIMER_16MS   (0 << 5)

#define DP83867_RGMIICTL	0x0032
#define DP83867_STRAP_STS1	0x006E
#define DP83867_STRAP_STS2	0x006f
#define DP83867_RGMIIDCTL	0x0086
#define DP83867_RXFCFG		0x0134
#define DP83867_RXFPMD1	0x0136
#define DP83867_RXFPMD2	0x0137
#define DP83867_RXFPMD3	0x0138
#define DP83867_RXFSOP1	0x0139
#define DP83867_RXFSOP2	0x013A
#define DP83867_RXFSOP3	0x013B
#define DP83867_IO_MUX_CFG	0x0170
#define DP83867_SGMIICTL	0x00D3
#define DP83867_10M_SGMII_CFG   0x016F
#define DP83867_10M_SGMII_RATE_ADAPT_MASK BIT(7)

#define DP83867_SW_RESET	BIT(15)
#define DP83867_SW_RESTART	BIT(14)

/* MICR Interrupt bits */
#define MII_DP83867_MICR_AN_ERR_INT_EN		BIT(15)
#define MII_DP83867_MICR_SPEED_CHNG_INT_EN	BIT(14)
#define MII_DP83867_MICR_DUP_MODE_CHNG_INT_EN	BIT(13)
#define MII_DP83867_MICR_PAGE_RXD_INT_EN	BIT(12)
#define MII_DP83867_MICR_AUTONEG_COMP_INT_EN	BIT(11)
#define MII_DP83867_MICR_LINK_STS_CHNG_INT_EN	BIT(10)
#define MII_DP83867_MICR_FALSE_CARRIER_INT_EN	BIT(8)
#define MII_DP83867_MICR_SLEEP_MODE_CHNG_INT_EN	BIT(4)
#define MII_DP83867_MICR_WOL_INT_EN		BIT(3)
#define MII_DP83867_MICR_XGMII_ERR_INT_EN	BIT(2)
#define MII_DP83867_MICR_POL_CHNG_INT_EN	BIT(1)
#define MII_DP83867_MICR_JABBER_INT_EN		BIT(0)

/* RGMIICTL bits */
#define DP83867_RGMII_TX_CLK_DELAY_EN		BIT(1)
#define DP83867_RGMII_RX_CLK_DELAY_EN		BIT(0)

/* SGMIICTL bits */
#define DP83867_SGMII_TYPE		BIT(14)

/* RXFCFG bits*/
#define DP83867_WOL_MAGIC_EN		BIT(0)
#define DP83867_WOL_BCAST_EN		BIT(2)
#define DP83867_WOL_UCAST_EN		BIT(4)
#define DP83867_WOL_SEC_EN		BIT(5)
#define DP83867_WOL_ENH_MAC		BIT(7)

/* STRAP_STS1 bits */
#define DP83867_STRAP_STS1_RESERVED		BIT(11)

/* STRAP_STS2 bits */
#define DP83867_STRAP_STS2_CLK_SKEW_TX_MASK	GENMASK(6, 4)
#define DP83867_STRAP_STS2_CLK_SKEW_TX_SHIFT	4
#define DP83867_STRAP_STS2_CLK_SKEW_RX_MASK	GENMASK(2, 0)
#define DP83867_STRAP_STS2_CLK_SKEW_RX_SHIFT	0
#define DP83867_STRAP_STS2_CLK_SKEW_NONE	BIT(2)
#define DP83867_STRAP_STS2_STRAP_FLD		BIT(10)

/* PHY CTRL bits */
#define DP83867_PHYCR_TX_FIFO_DEPTH_SHIFT	14
#define DP83867_PHYCR_RX_FIFO_DEPTH_SHIFT	12
#define DP83867_PHYCR_FIFO_DEPTH_MAX		0x03
#define DP83867_PHYCR_TX_FIFO_DEPTH_MASK	GENMASK(15, 14)
#define DP83867_PHYCR_RX_FIFO_DEPTH_MASK	GENMASK(13, 12)
#define DP83867_PHYCR_RESERVED_MASK		BIT(11)
#define DP83867_PHYCR_FORCE_LINK_GOOD		BIT(10)

/* RGMIIDCTL bits */
#define DP83867_RGMII_TX_CLK_DELAY_MAX		0xf
#define DP83867_RGMII_TX_CLK_DELAY_SHIFT	4
#define DP83867_RGMII_TX_CLK_DELAY_INV	(DP83867_RGMII_TX_CLK_DELAY_MAX + 1)
#define DP83867_RGMII_RX_CLK_DELAY_MAX		0xf
#define DP83867_RGMII_RX_CLK_DELAY_SHIFT	0
#define DP83867_RGMII_RX_CLK_DELAY_INV	(DP83867_RGMII_RX_CLK_DELAY_MAX + 1)

/* IO_MUX_CFG bits */
#define DP83867_IO_MUX_CFG_IO_IMPEDANCE_MASK	0x1f
#define DP83867_IO_MUX_CFG_IO_IMPEDANCE_MAX	0x0
#define DP83867_IO_MUX_CFG_IO_IMPEDANCE_MIN	0x1f
#define DP83867_IO_MUX_CFG_CLK_O_DISABLE	BIT(6)
#define DP83867_IO_MUX_CFG_CLK_O_SEL_MASK	(0x1f << 8)
#define DP83867_IO_MUX_CFG_CLK_O_SEL_SHIFT	8

/* PHY STS bits */
#define DP83867_PHYSTS_1000			BIT(15)
#define DP83867_PHYSTS_100			BIT(14)
#define DP83867_PHYSTS_DUPLEX			BIT(13)
#define DP83867_PHYSTS_LINK			BIT(10)

/* CFG2 bits */
#define DP83867_DOWNSHIFT_EN		(BIT(8) | BIT(9))
#define DP83867_DOWNSHIFT_ATTEMPT_MASK	(BIT(10) | BIT(11))
#define DP83867_DOWNSHIFT_1_COUNT_VAL	0
#define DP83867_DOWNSHIFT_2_COUNT_VAL	1
#define DP83867_DOWNSHIFT_4_COUNT_VAL	2
#define DP83867_DOWNSHIFT_8_COUNT_VAL	3
#define DP83867_DOWNSHIFT_1_COUNT	1
#define DP83867_DOWNSHIFT_2_COUNT	2
#define DP83867_DOWNSHIFT_4_COUNT	4
#define DP83867_DOWNSHIFT_8_COUNT	8

/* CFG3 bits */
#define DP83867_CFG3_INT_OE			BIT(7)
#define DP83867_CFG3_ROBUST_AUTO_MDIX		BIT(9)

/* CFG4 bits */
#define DP83867_CFG4_PORT_MIRROR_EN              BIT(0)

/* FLD_THR_CFG */
#define DP83867_FLD_THR_CFG_ENERGY_LOST_THR_MASK	0x7

enum {
	DP83867_PORT_MIRROING_KEEP,
	DP83867_PORT_MIRROING_EN,
	DP83867_PORT_MIRROING_DIS,
};

struct dp83867_private {
	u32 rx_id_delay;
	u32 tx_id_delay;
	u32 tx_fifo_depth;
	u32 rx_fifo_depth;
	int io_impedance;
	int port_mirroring;
	bool rxctrl_strap_quirk;
	bool set_clk_output;
	u32 clk_output_sel;
	bool sgmii_ref_clk_en;
};

static int dp83867_ack_interrupt(struct phy_device *phydev)
{
	int err = phy_read(phydev, MII_DP83867_ISR);

	if (err < 0)
		return err;

	return 0;
}

static int dp83867_set_wol(struct phy_device *phydev,
			   struct ethtool_wolinfo *wol)
{
	struct net_device *ndev = phydev->attached_dev;
	u16 val_rxcfg, val_micr;
	u8 *mac;

	val_rxcfg = phy_read_mmd(phydev, DP83867_DEVADDR, DP83867_RXFCFG);
	val_micr = phy_read(phydev, MII_DP83867_MICR);

	if (wol->wolopts & (WAKE_MAGIC | WAKE_MAGICSECURE | WAKE_UCAST |
			    WAKE_BCAST)) {
		val_rxcfg |= DP83867_WOL_ENH_MAC;
		val_micr |= MII_DP83867_MICR_WOL_INT_EN;

		if (wol->wolopts & WAKE_MAGIC) {
			mac = (u8 *)ndev->dev_addr;

			if (!is_valid_ether_addr(mac))
				return -EINVAL;

			phy_write_mmd(phydev, DP83867_DEVADDR, DP83867_RXFPMD1,
				      (mac[1] << 8 | mac[0]));
			phy_write_mmd(phydev, DP83867_DEVADDR, DP83867_RXFPMD2,
				      (mac[3] << 8 | mac[2]));
			phy_write_mmd(phydev, DP83867_DEVADDR, DP83867_RXFPMD3,
				      (mac[5] << 8 | mac[4]));

			val_rxcfg |= DP83867_WOL_MAGIC_EN;
		} else {
			val_rxcfg &= ~DP83867_WOL_MAGIC_EN;
		}

		if (wol->wolopts & WAKE_MAGICSECURE) {
			phy_write_mmd(phydev, DP83867_DEVADDR, DP83867_RXFSOP1,
				      (wol->sopass[1] << 8) | wol->sopass[0]);
			phy_write_mmd(phydev, DP83867_DEVADDR, DP83867_RXFSOP2,
				      (wol->sopass[3] << 8) | wol->sopass[2]);
			phy_write_mmd(phydev, DP83867_DEVADDR, DP83867_RXFSOP3,
				      (wol->sopass[5] << 8) | wol->sopass[4]);

			val_rxcfg |= DP83867_WOL_SEC_EN;
		} else {
			val_rxcfg &= ~DP83867_WOL_SEC_EN;
		}

		if (wol->wolopts & WAKE_UCAST)
			val_rxcfg |= DP83867_WOL_UCAST_EN;
		else
			val_rxcfg &= ~DP83867_WOL_UCAST_EN;

		if (wol->wolopts & WAKE_BCAST)
			val_rxcfg |= DP83867_WOL_BCAST_EN;
		else
			val_rxcfg &= ~DP83867_WOL_BCAST_EN;
	} else {
		val_rxcfg &= ~DP83867_WOL_ENH_MAC;
		val_micr &= ~MII_DP83867_MICR_WOL_INT_EN;
	}

	phy_write_mmd(phydev, DP83867_DEVADDR, DP83867_RXFCFG, val_rxcfg);
	phy_write(phydev, MII_DP83867_MICR, val_micr);

	return 0;
}

static void dp83867_get_wol(struct phy_device *phydev,
			    struct ethtool_wolinfo *wol)
{
	u16 value, sopass_val;

	wol->supported = (WAKE_UCAST | WAKE_BCAST | WAKE_MAGIC |
			WAKE_MAGICSECURE);
	wol->wolopts = 0;

	value = phy_read_mmd(phydev, DP83867_DEVADDR, DP83867_RXFCFG);

	if (value & DP83867_WOL_UCAST_EN)
		wol->wolopts |= WAKE_UCAST;

	if (value & DP83867_WOL_BCAST_EN)
		wol->wolopts |= WAKE_BCAST;

	if (value & DP83867_WOL_MAGIC_EN)
		wol->wolopts |= WAKE_MAGIC;

	if (value & DP83867_WOL_SEC_EN) {
		sopass_val = phy_read_mmd(phydev, DP83867_DEVADDR,
					  DP83867_RXFSOP1);
		wol->sopass[0] = (sopass_val & 0xff);
		wol->sopass[1] = (sopass_val >> 8);

		sopass_val = phy_read_mmd(phydev, DP83867_DEVADDR,
					  DP83867_RXFSOP2);
		wol->sopass[2] = (sopass_val & 0xff);
		wol->sopass[3] = (sopass_val >> 8);

		sopass_val = phy_read_mmd(phydev, DP83867_DEVADDR,
					  DP83867_RXFSOP3);
		wol->sopass[4] = (sopass_val & 0xff);
		wol->sopass[5] = (sopass_val >> 8);

		wol->wolopts |= WAKE_MAGICSECURE;
	}

	if (!(value & DP83867_WOL_ENH_MAC))
		wol->wolopts = 0;
}

static int dp83867_config_intr(struct phy_device *phydev)
{
	int micr_status, err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		err = dp83867_ack_interrupt(phydev);
		if (err)
			return err;

		micr_status = phy_read(phydev, MII_DP83867_MICR);
		if (micr_status < 0)
			return micr_status;

		micr_status |=
			(MII_DP83867_MICR_AN_ERR_INT_EN |
			MII_DP83867_MICR_SPEED_CHNG_INT_EN |
			MII_DP83867_MICR_AUTONEG_COMP_INT_EN |
			MII_DP83867_MICR_LINK_STS_CHNG_INT_EN |
			MII_DP83867_MICR_DUP_MODE_CHNG_INT_EN |
			MII_DP83867_MICR_SLEEP_MODE_CHNG_INT_EN);

		err = phy_write(phydev, MII_DP83867_MICR, micr_status);
	} else {
		micr_status = 0x0;
		err = phy_write(phydev, MII_DP83867_MICR, micr_status);
		if (err)
			return err;

		err = dp83867_ack_interrupt(phydev);
	}

	return err;
}

static irqreturn_t dp83867_handle_interrupt(struct phy_device *phydev)
{
	int irq_status, irq_enabled;

	irq_status = phy_read(phydev, MII_DP83867_ISR);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	irq_enabled = phy_read(phydev, MII_DP83867_MICR);
	if (irq_enabled < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if (!(irq_status & irq_enabled))
		return IRQ_NONE;

	phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}

static int dp83867_read_status(struct phy_device *phydev)
{
	int status = phy_read(phydev, MII_DP83867_PHYSTS);
	int ret;

	ret = genphy_read_status(phydev);
	if (ret)
		return ret;

	if (status < 0)
		return status;

	if (status & DP83867_PHYSTS_DUPLEX)
		phydev->duplex = DUPLEX_FULL;
	else
		phydev->duplex = DUPLEX_HALF;

	if (status & DP83867_PHYSTS_1000)
		phydev->speed = SPEED_1000;
	else if (status & DP83867_PHYSTS_100)
		phydev->speed = SPEED_100;
	else
		phydev->speed = SPEED_10;

	return 0;
}

static int dp83867_get_downshift(struct phy_device *phydev, u8 *data)
{
	int val, cnt, enable, count;

	val = phy_read(phydev, DP83867_CFG2);
	if (val < 0)
		return val;

	enable = FIELD_GET(DP83867_DOWNSHIFT_EN, val);
	cnt = FIELD_GET(DP83867_DOWNSHIFT_ATTEMPT_MASK, val);

	switch (cnt) {
	case DP83867_DOWNSHIFT_1_COUNT_VAL:
		count = DP83867_DOWNSHIFT_1_COUNT;
		break;
	case DP83867_DOWNSHIFT_2_COUNT_VAL:
		count = DP83867_DOWNSHIFT_2_COUNT;
		break;
	case DP83867_DOWNSHIFT_4_COUNT_VAL:
		count = DP83867_DOWNSHIFT_4_COUNT;
		break;
	case DP83867_DOWNSHIFT_8_COUNT_VAL:
		count = DP83867_DOWNSHIFT_8_COUNT;
		break;
	default:
		return -EINVAL;
	}

	*data = enable ? count : DOWNSHIFT_DEV_DISABLE;

	return 0;
}

static int dp83867_set_downshift(struct phy_device *phydev, u8 cnt)
{
	int val, count;

	if (cnt > DP83867_DOWNSHIFT_8_COUNT)
		return -E2BIG;

	if (!cnt)
		return phy_clear_bits(phydev, DP83867_CFG2,
				      DP83867_DOWNSHIFT_EN);

	switch (cnt) {
	case DP83867_DOWNSHIFT_1_COUNT:
		count = DP83867_DOWNSHIFT_1_COUNT_VAL;
		break;
	case DP83867_DOWNSHIFT_2_COUNT:
		count = DP83867_DOWNSHIFT_2_COUNT_VAL;
		break;
	case DP83867_DOWNSHIFT_4_COUNT:
		count = DP83867_DOWNSHIFT_4_COUNT_VAL;
		break;
	case DP83867_DOWNSHIFT_8_COUNT:
		count = DP83867_DOWNSHIFT_8_COUNT_VAL;
		break;
	default:
		phydev_err(phydev,
			   "Downshift count must be 1, 2, 4 or 8\n");
		return -EINVAL;
	}

	val = DP83867_DOWNSHIFT_EN;
	val |= FIELD_PREP(DP83867_DOWNSHIFT_ATTEMPT_MASK, count);

	return phy_modify(phydev, DP83867_CFG2,
			  DP83867_DOWNSHIFT_EN | DP83867_DOWNSHIFT_ATTEMPT_MASK,
			  val);
}

static int dp83867_get_tunable(struct phy_device *phydev,
			       struct ethtool_tunable *tuna, void *data)
{
	switch (tuna->id) {
	case ETHTOOL_PHY_DOWNSHIFT:
		return dp83867_get_downshift(phydev, data);
	default:
		return -EOPNOTSUPP;
	}
}

static int dp83867_set_tunable(struct phy_device *phydev,
			       struct ethtool_tunable *tuna, const void *data)
{
	switch (tuna->id) {
	case ETHTOOL_PHY_DOWNSHIFT:
		return dp83867_set_downshift(phydev, *(const u8 *)data);
	default:
		return -EOPNOTSUPP;
	}
}

static int dp83867_config_port_mirroring(struct phy_device *phydev)
{
	struct dp83867_private *dp83867 =
		(struct dp83867_private *)phydev->priv;

	if (dp83867->port_mirroring == DP83867_PORT_MIRROING_EN)
		phy_set_bits_mmd(phydev, DP83867_DEVADDR, DP83867_CFG4,
				 DP83867_CFG4_PORT_MIRROR_EN);
	else
		phy_clear_bits_mmd(phydev, DP83867_DEVADDR, DP83867_CFG4,
				   DP83867_CFG4_PORT_MIRROR_EN);
	return 0;
}

static int dp83867_verify_rgmii_cfg(struct phy_device *phydev)
{
	struct dp83867_private *dp83867 = phydev->priv;

	/* Existing behavior was to use default pin strapping delay in rgmii
	 * mode, but rgmii should have meant no delay.  Warn existing users.
	 */
	if (phydev->interface == PHY_INTERFACE_MODE_RGMII) {
		const u16 val = phy_read_mmd(phydev, DP83867_DEVADDR,
					     DP83867_STRAP_STS2);
		const u16 txskew = (val & DP83867_STRAP_STS2_CLK_SKEW_TX_MASK) >>
				   DP83867_STRAP_STS2_CLK_SKEW_TX_SHIFT;
		const u16 rxskew = (val & DP83867_STRAP_STS2_CLK_SKEW_RX_MASK) >>
				   DP83867_STRAP_STS2_CLK_SKEW_RX_SHIFT;

		if (txskew != DP83867_STRAP_STS2_CLK_SKEW_NONE ||
		    rxskew != DP83867_STRAP_STS2_CLK_SKEW_NONE)
			phydev_warn(phydev,
				    "PHY has delays via pin strapping, but phy-mode = 'rgmii'\n"
				    "Should be 'rgmii-id' to use internal delays txskew:%x rxskew:%x\n",
				    txskew, rxskew);
	}

	/* RX delay *must* be specified if internal delay of RX is used. */
	if ((phydev->interface == PHY_INTERFACE_MODE_RGMII_ID ||
	     phydev->interface == PHY_INTERFACE_MODE_RGMII_RXID) &&
	     dp83867->rx_id_delay == DP83867_RGMII_RX_CLK_DELAY_INV) {
		phydev_err(phydev, "ti,rx-internal-delay must be specified\n");
		return -EINVAL;
	}

	/* TX delay *must* be specified if internal delay of TX is used. */
	if ((phydev->interface == PHY_INTERFACE_MODE_RGMII_ID ||
	     phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID) &&
	     dp83867->tx_id_delay == DP83867_RGMII_TX_CLK_DELAY_INV) {
		phydev_err(phydev, "ti,tx-internal-delay must be specified\n");
		return -EINVAL;
	}

	return 0;
}

#if IS_ENABLED(CONFIG_OF_MDIO)
static int dp83867_of_init(struct phy_device *phydev)
{
	struct dp83867_private *dp83867 = phydev->priv;
	struct device *dev = &phydev->mdio.dev;
	struct device_node *of_node = dev->of_node;
	int ret;

	if (!of_node)
		return -ENODEV;

	/* Optional configuration */
	ret = of_property_read_u32(of_node, "ti,clk-output-sel",
				   &dp83867->clk_output_sel);
	/* If not set, keep default */
	if (!ret) {
		dp83867->set_clk_output = true;
		/* Valid values are 0 to DP83867_CLK_O_SEL_REF_CLK or
		 * DP83867_CLK_O_SEL_OFF.
		 */
		if (dp83867->clk_output_sel > DP83867_CLK_O_SEL_REF_CLK &&
		    dp83867->clk_output_sel != DP83867_CLK_O_SEL_OFF) {
			phydev_err(phydev, "ti,clk-output-sel value %u out of range\n",
				   dp83867->clk_output_sel);
			return -EINVAL;
		}
	}

	if (of_property_read_bool(of_node, "ti,max-output-impedance"))
		dp83867->io_impedance = DP83867_IO_MUX_CFG_IO_IMPEDANCE_MAX;
	else if (of_property_read_bool(of_node, "ti,min-output-impedance"))
		dp83867->io_impedance = DP83867_IO_MUX_CFG_IO_IMPEDANCE_MIN;
	else
		dp83867->io_impedance = -1; /* leave at default */

	dp83867->rxctrl_strap_quirk = of_property_read_bool(of_node,
							    "ti,dp83867-rxctrl-strap-quirk");

	dp83867->sgmii_ref_clk_en = of_property_read_bool(of_node,
							  "ti,sgmii-ref-clock-output-enable");

	dp83867->rx_id_delay = DP83867_RGMII_RX_CLK_DELAY_INV;
	ret = of_property_read_u32(of_node, "ti,rx-internal-delay",
				   &dp83867->rx_id_delay);
	if (!ret && dp83867->rx_id_delay > DP83867_RGMII_RX_CLK_DELAY_MAX) {
		phydev_err(phydev,
			   "ti,rx-internal-delay value of %u out of range\n",
			   dp83867->rx_id_delay);
		return -EINVAL;
	}

	dp83867->tx_id_delay = DP83867_RGMII_TX_CLK_DELAY_INV;
	ret = of_property_read_u32(of_node, "ti,tx-internal-delay",
				   &dp83867->tx_id_delay);
	if (!ret && dp83867->tx_id_delay > DP83867_RGMII_TX_CLK_DELAY_MAX) {
		phydev_err(phydev,
			   "ti,tx-internal-delay value of %u out of range\n",
			   dp83867->tx_id_delay);
		return -EINVAL;
	}

	if (of_property_read_bool(of_node, "enet-phy-lane-swap"))
		dp83867->port_mirroring = DP83867_PORT_MIRROING_EN;

	if (of_property_read_bool(of_node, "enet-phy-lane-no-swap"))
		dp83867->port_mirroring = DP83867_PORT_MIRROING_DIS;

	ret = of_property_read_u32(of_node, "ti,fifo-depth",
				   &dp83867->tx_fifo_depth);
	if (ret) {
		ret = of_property_read_u32(of_node, "tx-fifo-depth",
					   &dp83867->tx_fifo_depth);
		if (ret)
			dp83867->tx_fifo_depth =
					DP83867_PHYCR_FIFO_DEPTH_4_B_NIB;
	}

	if (dp83867->tx_fifo_depth > DP83867_PHYCR_FIFO_DEPTH_MAX) {
		phydev_err(phydev, "tx-fifo-depth value %u out of range\n",
			   dp83867->tx_fifo_depth);
		return -EINVAL;
	}

	ret = of_property_read_u32(of_node, "rx-fifo-depth",
				   &dp83867->rx_fifo_depth);
	if (ret)
		dp83867->rx_fifo_depth = DP83867_PHYCR_FIFO_DEPTH_4_B_NIB;

	if (dp83867->rx_fifo_depth > DP83867_PHYCR_FIFO_DEPTH_MAX) {
		phydev_err(phydev, "rx-fifo-depth value %u out of range\n",
			   dp83867->rx_fifo_depth);
		return -EINVAL;
	}

	return 0;
}
#else
static int dp83867_of_init(struct phy_device *phydev)
{
	return 0;
}
#endif /* CONFIG_OF_MDIO */

static int dp83867_probe(struct phy_device *phydev)
{
	struct dp83867_private *dp83867;

	dp83867 = devm_kzalloc(&phydev->mdio.dev, sizeof(*dp83867),
			       GFP_KERNEL);
	if (!dp83867)
		return -ENOMEM;

	phydev->priv = dp83867;

	return dp83867_of_init(phydev);
}

static int dp83867_config_init(struct phy_device *phydev)
{
	struct dp83867_private *dp83867 = phydev->priv;
	int ret, val, bs;
	u16 delay;

	/* Force speed optimization for the PHY even if it strapped */
	ret = phy_modify(phydev, DP83867_CFG2, DP83867_DOWNSHIFT_EN,
			 DP83867_DOWNSHIFT_EN);
	if (ret)
		return ret;

	ret = dp83867_verify_rgmii_cfg(phydev);
	if (ret)
		return ret;

	/* RX_DV/RX_CTRL strapped in mode 1 or mode 2 workaround */
	if (dp83867->rxctrl_strap_quirk)
		phy_clear_bits_mmd(phydev, DP83867_DEVADDR, DP83867_CFG4,
				   BIT(7));

	bs = phy_read_mmd(phydev, DP83867_DEVADDR, DP83867_STRAP_STS2);
	if (bs & DP83867_STRAP_STS2_STRAP_FLD) {
		/* When using strap to enable FLD, the ENERGY_LOST_FLD_THR will
		 * be set to 0x2. This may causes the PHY link to be unstable -
		 * the default value 0x1 need to be restored.
		 */
		ret = phy_modify_mmd(phydev, DP83867_DEVADDR,
				     DP83867_FLD_THR_CFG,
				     DP83867_FLD_THR_CFG_ENERGY_LOST_THR_MASK,
				     0x1);
		if (ret)
			return ret;
	}

	if (phy_interface_is_rgmii(phydev) ||
	    phydev->interface == PHY_INTERFACE_MODE_SGMII) {
		val = phy_read(phydev, MII_DP83867_PHYCTRL);
		if (val < 0)
			return val;

		val &= ~DP83867_PHYCR_TX_FIFO_DEPTH_MASK;
		val |= (dp83867->tx_fifo_depth <<
			DP83867_PHYCR_TX_FIFO_DEPTH_SHIFT);

		if (phydev->interface == PHY_INTERFACE_MODE_SGMII) {
			val &= ~DP83867_PHYCR_RX_FIFO_DEPTH_MASK;
			val |= (dp83867->rx_fifo_depth <<
				DP83867_PHYCR_RX_FIFO_DEPTH_SHIFT);
		}

		ret = phy_write(phydev, MII_DP83867_PHYCTRL, val);
		if (ret)
			return ret;
	}

	if (phy_interface_is_rgmii(phydev)) {
		val = phy_read(phydev, MII_DP83867_PHYCTRL);
		if (val < 0)
			return val;

		/* The code below checks if "port mirroring" N/A MODE4 has been
		 * enabled during power on bootstrap.
		 *
		 * Such N/A mode enabled by mistake can put PHY IC in some
		 * internal testing mode and disable RGMII transmission.
		 *
		 * In this particular case one needs to check STRAP_STS1
		 * register's bit 11 (marked as RESERVED).
		 */

		bs = phy_read_mmd(phydev, DP83867_DEVADDR, DP83867_STRAP_STS1);
		if (bs & DP83867_STRAP_STS1_RESERVED)
			val &= ~DP83867_PHYCR_RESERVED_MASK;

		ret = phy_write(phydev, MII_DP83867_PHYCTRL, val);
		if (ret)
			return ret;

		/* If rgmii mode with no internal delay is selected, we do NOT use
		 * aligned mode as one might expect.  Instead we use the PHY's default
		 * based on pin strapping.  And the "mode 0" default is to *use*
		 * internal delay with a value of 7 (2.00 ns).
		 *
		 * Set up RGMII delays
		 */
		val = phy_read_mmd(phydev, DP83867_DEVADDR, DP83867_RGMIICTL);

		val &= ~(DP83867_RGMII_TX_CLK_DELAY_EN | DP83867_RGMII_RX_CLK_DELAY_EN);
		if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID)
			val |= (DP83867_RGMII_TX_CLK_DELAY_EN | DP83867_RGMII_RX_CLK_DELAY_EN);

		if (phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID)
			val |= DP83867_RGMII_TX_CLK_DELAY_EN;

		if (phydev->interface == PHY_INTERFACE_MODE_RGMII_RXID)
			val |= DP83867_RGMII_RX_CLK_DELAY_EN;

		phy_write_mmd(phydev, DP83867_DEVADDR, DP83867_RGMIICTL, val);

		delay = 0;
		if (dp83867->rx_id_delay != DP83867_RGMII_RX_CLK_DELAY_INV)
			delay |= dp83867->rx_id_delay;
		if (dp83867->tx_id_delay != DP83867_RGMII_TX_CLK_DELAY_INV)
			delay |= dp83867->tx_id_delay <<
				 DP83867_RGMII_TX_CLK_DELAY_SHIFT;

		phy_write_mmd(phydev, DP83867_DEVADDR, DP83867_RGMIIDCTL,
			      delay);
	}

	/* If specified, set io impedance */
	if (dp83867->io_impedance >= 0)
		phy_modify_mmd(phydev, DP83867_DEVADDR, DP83867_IO_MUX_CFG,
			       DP83867_IO_MUX_CFG_IO_IMPEDANCE_MASK,
			       dp83867->io_impedance);

	if (phydev->interface == PHY_INTERFACE_MODE_SGMII) {
		/* For support SPEED_10 in SGMII mode
		 * DP83867_10M_SGMII_RATE_ADAPT bit
		 * has to be cleared by software. That
		 * does not affect SPEED_100 and
		 * SPEED_1000.
		 */
		ret = phy_modify_mmd(phydev, DP83867_DEVADDR,
				     DP83867_10M_SGMII_CFG,
				     DP83867_10M_SGMII_RATE_ADAPT_MASK,
				     0);
		if (ret)
			return ret;

		/* After reset SGMII Autoneg timer is set to 2us (bits 6 and 5
		 * are 01). That is not enough to finalize autoneg on some
		 * devices. Increase this timer duration to maximum 16ms.
		 */
		ret = phy_modify_mmd(phydev, DP83867_DEVADDR,
				     DP83867_CFG4,
				     DP83867_CFG4_SGMII_ANEG_MASK,
				     DP83867_CFG4_SGMII_ANEG_TIMER_16MS);

		if (ret)
			return ret;

		val = phy_read_mmd(phydev, DP83867_DEVADDR, DP83867_SGMIICTL);
		/* SGMII type is set to 4-wire mode by default.
		 * If we place appropriate property in dts (see above)
		 * switch on 6-wire mode.
		 */
		if (dp83867->sgmii_ref_clk_en)
			val |= DP83867_SGMII_TYPE;
		else
			val &= ~DP83867_SGMII_TYPE;
		phy_write_mmd(phydev, DP83867_DEVADDR, DP83867_SGMIICTL, val);
	}

	val = phy_read(phydev, DP83867_CFG3);
	/* Enable Interrupt output INT_OE in CFG3 register */
	if (phy_interrupt_is_valid(phydev))
		val |= DP83867_CFG3_INT_OE;

	val |= DP83867_CFG3_ROBUST_AUTO_MDIX;
	phy_write(phydev, DP83867_CFG3, val);

	if (dp83867->port_mirroring != DP83867_PORT_MIRROING_KEEP)
		dp83867_config_port_mirroring(phydev);

	/* Clock output selection if muxing property is set */
	if (dp83867->set_clk_output) {
		u16 mask = DP83867_IO_MUX_CFG_CLK_O_DISABLE;

		if (dp83867->clk_output_sel == DP83867_CLK_O_SEL_OFF) {
			val = DP83867_IO_MUX_CFG_CLK_O_DISABLE;
		} else {
			mask |= DP83867_IO_MUX_CFG_CLK_O_SEL_MASK;
			val = dp83867->clk_output_sel <<
			      DP83867_IO_MUX_CFG_CLK_O_SEL_SHIFT;
		}

		phy_modify_mmd(phydev, DP83867_DEVADDR, DP83867_IO_MUX_CFG,
			       mask, val);
	}

	return 0;
}

static int dp83867_phy_reset(struct phy_device *phydev)
{
	int err;

	err = phy_write(phydev, DP83867_CTRL, DP83867_SW_RESTART);
	if (err < 0)
		return err;

	usleep_range(10, 20);

	return phy_modify(phydev, MII_DP83867_PHYCTRL,
			 DP83867_PHYCR_FORCE_LINK_GOOD, 0);
}

static struct phy_driver dp83867_driver[] = {
	{
		.phy_id		= DP83867_PHY_ID,
		.phy_id_mask	= 0xfffffff0,
		.name		= "TI DP83867",
		/* PHY_GBIT_FEATURES */

		.probe          = dp83867_probe,
		.config_init	= dp83867_config_init,
		.soft_reset	= dp83867_phy_reset,

		.read_status	= dp83867_read_status,
		.get_tunable	= dp83867_get_tunable,
		.set_tunable	= dp83867_set_tunable,

		.get_wol	= dp83867_get_wol,
		.set_wol	= dp83867_set_wol,

		/* IRQ related */
		.config_intr	= dp83867_config_intr,
		.handle_interrupt = dp83867_handle_interrupt,

		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
	},
};
module_phy_driver(dp83867_driver);

static struct mdio_device_id __maybe_unused dp83867_tbl[] = {
	{ DP83867_PHY_ID, 0xfffffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, dp83867_tbl);

MODULE_DESCRIPTION("Texas Instruments DP83867 PHY driver");
MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com");
MODULE_LICENSE("GPL v2");
