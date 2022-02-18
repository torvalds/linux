// SPDX-License-Identifier: GPL-2.0
/* Driver for the Texas Instruments DP83869 PHY
 * Copyright (C) 2019 Texas Instruments Inc.
 */

#include <linux/ethtool.h>
#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/delay.h>
#include <linux/bitfield.h>

#include <dt-bindings/net/ti-dp83869.h>

#define DP83869_PHY_ID		0x2000a0f1
#define DP83561_PHY_ID		0x2000a1a4
#define DP83869_DEVADDR		0x1f

#define MII_DP83869_PHYCTRL	0x10
#define MII_DP83869_MICR	0x12
#define MII_DP83869_ISR		0x13
#define DP83869_CFG2		0x14
#define DP83869_CTRL		0x1f
#define DP83869_CFG4		0x1e

/* Extended Registers */
#define DP83869_GEN_CFG3        0x0031
#define DP83869_RGMIICTL	0x0032
#define DP83869_STRAP_STS1	0x006e
#define DP83869_RGMIIDCTL	0x0086
#define DP83869_RXFCFG		0x0134
#define DP83869_RXFPMD1		0x0136
#define DP83869_RXFPMD2		0x0137
#define DP83869_RXFPMD3		0x0138
#define DP83869_RXFSOP1		0x0139
#define DP83869_RXFSOP2		0x013A
#define DP83869_RXFSOP3		0x013B
#define DP83869_IO_MUX_CFG	0x0170
#define DP83869_OP_MODE		0x01df
#define DP83869_FX_CTRL		0x0c00

#define DP83869_SW_RESET	BIT(15)
#define DP83869_SW_RESTART	BIT(14)

/* MICR Interrupt bits */
#define MII_DP83869_MICR_AN_ERR_INT_EN		BIT(15)
#define MII_DP83869_MICR_SPEED_CHNG_INT_EN	BIT(14)
#define MII_DP83869_MICR_DUP_MODE_CHNG_INT_EN	BIT(13)
#define MII_DP83869_MICR_PAGE_RXD_INT_EN	BIT(12)
#define MII_DP83869_MICR_AUTONEG_COMP_INT_EN	BIT(11)
#define MII_DP83869_MICR_LINK_STS_CHNG_INT_EN	BIT(10)
#define MII_DP83869_MICR_FALSE_CARRIER_INT_EN	BIT(8)
#define MII_DP83869_MICR_SLEEP_MODE_CHNG_INT_EN	BIT(4)
#define MII_DP83869_MICR_WOL_INT_EN		BIT(3)
#define MII_DP83869_MICR_XGMII_ERR_INT_EN	BIT(2)
#define MII_DP83869_MICR_POL_CHNG_INT_EN	BIT(1)
#define MII_DP83869_MICR_JABBER_INT_EN		BIT(0)

#define MII_DP83869_BMCR_DEFAULT	(BMCR_ANENABLE | \
					 BMCR_FULLDPLX | \
					 BMCR_SPEED1000)

#define MII_DP83869_FIBER_ADVERTISE    (ADVERTISED_FIBRE | \
					ADVERTISED_Pause | \
					ADVERTISED_Asym_Pause)

/* This is the same bit mask as the BMCR so re-use the BMCR default */
#define DP83869_FX_CTRL_DEFAULT	MII_DP83869_BMCR_DEFAULT

/* CFG1 bits */
#define DP83869_CFG1_DEFAULT	(ADVERTISE_1000HALF | \
				 ADVERTISE_1000FULL | \
				 CTL1000_AS_MASTER)

/* RGMIICTL bits */
#define DP83869_RGMII_TX_CLK_DELAY_EN		BIT(1)
#define DP83869_RGMII_RX_CLK_DELAY_EN		BIT(0)

/* RGMIIDCTL */
#define DP83869_RGMII_CLK_DELAY_SHIFT		4
#define DP83869_CLK_DELAY_DEF			7

/* STRAP_STS1 bits */
#define DP83869_STRAP_OP_MODE_MASK		GENMASK(2, 0)
#define DP83869_STRAP_STS1_RESERVED		BIT(11)
#define DP83869_STRAP_MIRROR_ENABLED           BIT(12)

/* PHYCTRL bits */
#define DP83869_RX_FIFO_SHIFT	12
#define DP83869_TX_FIFO_SHIFT	14

/* PHY_CTRL lower bytes 0x48 are declared as reserved */
#define DP83869_PHY_CTRL_DEFAULT	0x48
#define DP83869_PHYCR_FIFO_DEPTH_MASK	GENMASK(15, 12)
#define DP83869_PHYCR_RESERVED_MASK	BIT(11)

/* IO_MUX_CFG bits */
#define DP83869_IO_MUX_CFG_IO_IMPEDANCE_CTRL	0x1f

#define DP83869_IO_MUX_CFG_IO_IMPEDANCE_MAX	0x0
#define DP83869_IO_MUX_CFG_IO_IMPEDANCE_MIN	0x1f
#define DP83869_IO_MUX_CFG_CLK_O_SEL_MASK	(0x1f << 8)
#define DP83869_IO_MUX_CFG_CLK_O_SEL_SHIFT	8

/* CFG3 bits */
#define DP83869_CFG3_PORT_MIRROR_EN              BIT(0)

/* CFG4 bits */
#define DP83869_INT_OE	BIT(7)

/* OP MODE */
#define DP83869_OP_MODE_MII			BIT(5)
#define DP83869_SGMII_RGMII_BRIDGE		BIT(6)

/* RXFCFG bits*/
#define DP83869_WOL_MAGIC_EN		BIT(0)
#define DP83869_WOL_PATTERN_EN		BIT(1)
#define DP83869_WOL_BCAST_EN		BIT(2)
#define DP83869_WOL_UCAST_EN		BIT(4)
#define DP83869_WOL_SEC_EN		BIT(5)
#define DP83869_WOL_ENH_MAC		BIT(7)

/* CFG2 bits */
#define DP83869_DOWNSHIFT_EN		(BIT(8) | BIT(9))
#define DP83869_DOWNSHIFT_ATTEMPT_MASK	(BIT(10) | BIT(11))
#define DP83869_DOWNSHIFT_1_COUNT_VAL	0
#define DP83869_DOWNSHIFT_2_COUNT_VAL	1
#define DP83869_DOWNSHIFT_4_COUNT_VAL	2
#define DP83869_DOWNSHIFT_8_COUNT_VAL	3
#define DP83869_DOWNSHIFT_1_COUNT	1
#define DP83869_DOWNSHIFT_2_COUNT	2
#define DP83869_DOWNSHIFT_4_COUNT	4
#define DP83869_DOWNSHIFT_8_COUNT	8

enum {
	DP83869_PORT_MIRRORING_KEEP,
	DP83869_PORT_MIRRORING_EN,
	DP83869_PORT_MIRRORING_DIS,
};

struct dp83869_private {
	int tx_fifo_depth;
	int rx_fifo_depth;
	s32 rx_int_delay;
	s32 tx_int_delay;
	int io_impedance;
	int port_mirroring;
	bool rxctrl_strap_quirk;
	int clk_output_sel;
	int mode;
};

static int dp83869_read_status(struct phy_device *phydev)
{
	struct dp83869_private *dp83869 = phydev->priv;
	int ret;

	ret = genphy_read_status(phydev);
	if (ret)
		return ret;

	if (linkmode_test_bit(ETHTOOL_LINK_MODE_FIBRE_BIT, phydev->supported)) {
		if (phydev->link) {
			if (dp83869->mode == DP83869_RGMII_100_BASE)
				phydev->speed = SPEED_100;
		} else {
			phydev->speed = SPEED_UNKNOWN;
			phydev->duplex = DUPLEX_UNKNOWN;
		}
	}

	return 0;
}

static int dp83869_ack_interrupt(struct phy_device *phydev)
{
	int err = phy_read(phydev, MII_DP83869_ISR);

	if (err < 0)
		return err;

	return 0;
}

static int dp83869_config_intr(struct phy_device *phydev)
{
	int micr_status = 0, err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		err = dp83869_ack_interrupt(phydev);
		if (err)
			return err;

		micr_status = phy_read(phydev, MII_DP83869_MICR);
		if (micr_status < 0)
			return micr_status;

		micr_status |=
			(MII_DP83869_MICR_AN_ERR_INT_EN |
			MII_DP83869_MICR_SPEED_CHNG_INT_EN |
			MII_DP83869_MICR_AUTONEG_COMP_INT_EN |
			MII_DP83869_MICR_LINK_STS_CHNG_INT_EN |
			MII_DP83869_MICR_DUP_MODE_CHNG_INT_EN |
			MII_DP83869_MICR_SLEEP_MODE_CHNG_INT_EN);

		err = phy_write(phydev, MII_DP83869_MICR, micr_status);
	} else {
		err = phy_write(phydev, MII_DP83869_MICR, micr_status);
		if (err)
			return err;

		err = dp83869_ack_interrupt(phydev);
	}

	return err;
}

static irqreturn_t dp83869_handle_interrupt(struct phy_device *phydev)
{
	int irq_status, irq_enabled;

	irq_status = phy_read(phydev, MII_DP83869_ISR);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	irq_enabled = phy_read(phydev, MII_DP83869_MICR);
	if (irq_enabled < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if (!(irq_status & irq_enabled))
		return IRQ_NONE;

	phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}

static int dp83869_set_wol(struct phy_device *phydev,
			   struct ethtool_wolinfo *wol)
{
	struct net_device *ndev = phydev->attached_dev;
	int val_rxcfg, val_micr;
	const u8 *mac;
	int ret;

	val_rxcfg = phy_read_mmd(phydev, DP83869_DEVADDR, DP83869_RXFCFG);
	if (val_rxcfg < 0)
		return val_rxcfg;

	val_micr = phy_read(phydev, MII_DP83869_MICR);
	if (val_micr < 0)
		return val_micr;

	if (wol->wolopts & (WAKE_MAGIC | WAKE_MAGICSECURE | WAKE_UCAST |
			    WAKE_BCAST)) {
		val_rxcfg |= DP83869_WOL_ENH_MAC;
		val_micr |= MII_DP83869_MICR_WOL_INT_EN;

		if (wol->wolopts & WAKE_MAGIC ||
		    wol->wolopts & WAKE_MAGICSECURE) {
			mac = (const u8 *)ndev->dev_addr;

			if (!is_valid_ether_addr(mac))
				return -EINVAL;

			ret = phy_write_mmd(phydev, DP83869_DEVADDR,
					    DP83869_RXFPMD1,
					    mac[1] << 8 | mac[0]);
			if (ret)
				return ret;

			ret = phy_write_mmd(phydev, DP83869_DEVADDR,
					    DP83869_RXFPMD2,
					    mac[3] << 8 | mac[2]);
			if (ret)
				return ret;

			ret = phy_write_mmd(phydev, DP83869_DEVADDR,
					    DP83869_RXFPMD3,
					    mac[5] << 8 | mac[4]);
			if (ret)
				return ret;

			val_rxcfg |= DP83869_WOL_MAGIC_EN;
		} else {
			val_rxcfg &= ~DP83869_WOL_MAGIC_EN;
		}

		if (wol->wolopts & WAKE_MAGICSECURE) {
			ret = phy_write_mmd(phydev, DP83869_DEVADDR,
					    DP83869_RXFSOP1,
					    (wol->sopass[1] << 8) | wol->sopass[0]);
			if (ret)
				return ret;

			ret = phy_write_mmd(phydev, DP83869_DEVADDR,
					    DP83869_RXFSOP2,
					    (wol->sopass[3] << 8) | wol->sopass[2]);
			if (ret)
				return ret;
			ret = phy_write_mmd(phydev, DP83869_DEVADDR,
					    DP83869_RXFSOP3,
					    (wol->sopass[5] << 8) | wol->sopass[4]);
			if (ret)
				return ret;

			val_rxcfg |= DP83869_WOL_SEC_EN;
		} else {
			val_rxcfg &= ~DP83869_WOL_SEC_EN;
		}

		if (wol->wolopts & WAKE_UCAST)
			val_rxcfg |= DP83869_WOL_UCAST_EN;
		else
			val_rxcfg &= ~DP83869_WOL_UCAST_EN;

		if (wol->wolopts & WAKE_BCAST)
			val_rxcfg |= DP83869_WOL_BCAST_EN;
		else
			val_rxcfg &= ~DP83869_WOL_BCAST_EN;
	} else {
		val_rxcfg &= ~DP83869_WOL_ENH_MAC;
		val_micr &= ~MII_DP83869_MICR_WOL_INT_EN;
	}

	ret = phy_write_mmd(phydev, DP83869_DEVADDR, DP83869_RXFCFG, val_rxcfg);
	if (ret)
		return ret;

	return phy_write(phydev, MII_DP83869_MICR, val_micr);
}

static void dp83869_get_wol(struct phy_device *phydev,
			    struct ethtool_wolinfo *wol)
{
	int value, sopass_val;

	wol->supported = (WAKE_UCAST | WAKE_BCAST | WAKE_MAGIC |
			WAKE_MAGICSECURE);
	wol->wolopts = 0;

	value = phy_read_mmd(phydev, DP83869_DEVADDR, DP83869_RXFCFG);
	if (value < 0) {
		phydev_err(phydev, "Failed to read RX CFG\n");
		return;
	}

	if (value & DP83869_WOL_UCAST_EN)
		wol->wolopts |= WAKE_UCAST;

	if (value & DP83869_WOL_BCAST_EN)
		wol->wolopts |= WAKE_BCAST;

	if (value & DP83869_WOL_MAGIC_EN)
		wol->wolopts |= WAKE_MAGIC;

	if (value & DP83869_WOL_SEC_EN) {
		sopass_val = phy_read_mmd(phydev, DP83869_DEVADDR,
					  DP83869_RXFSOP1);
		if (sopass_val < 0) {
			phydev_err(phydev, "Failed to read RX SOP 1\n");
			return;
		}

		wol->sopass[0] = (sopass_val & 0xff);
		wol->sopass[1] = (sopass_val >> 8);

		sopass_val = phy_read_mmd(phydev, DP83869_DEVADDR,
					  DP83869_RXFSOP2);
		if (sopass_val < 0) {
			phydev_err(phydev, "Failed to read RX SOP 2\n");
			return;
		}

		wol->sopass[2] = (sopass_val & 0xff);
		wol->sopass[3] = (sopass_val >> 8);

		sopass_val = phy_read_mmd(phydev, DP83869_DEVADDR,
					  DP83869_RXFSOP3);
		if (sopass_val < 0) {
			phydev_err(phydev, "Failed to read RX SOP 3\n");
			return;
		}

		wol->sopass[4] = (sopass_val & 0xff);
		wol->sopass[5] = (sopass_val >> 8);

		wol->wolopts |= WAKE_MAGICSECURE;
	}

	if (!(value & DP83869_WOL_ENH_MAC))
		wol->wolopts = 0;
}

static int dp83869_get_downshift(struct phy_device *phydev, u8 *data)
{
	int val, cnt, enable, count;

	val = phy_read(phydev, DP83869_CFG2);
	if (val < 0)
		return val;

	enable = FIELD_GET(DP83869_DOWNSHIFT_EN, val);
	cnt = FIELD_GET(DP83869_DOWNSHIFT_ATTEMPT_MASK, val);

	switch (cnt) {
	case DP83869_DOWNSHIFT_1_COUNT_VAL:
		count = DP83869_DOWNSHIFT_1_COUNT;
		break;
	case DP83869_DOWNSHIFT_2_COUNT_VAL:
		count = DP83869_DOWNSHIFT_2_COUNT;
		break;
	case DP83869_DOWNSHIFT_4_COUNT_VAL:
		count = DP83869_DOWNSHIFT_4_COUNT;
		break;
	case DP83869_DOWNSHIFT_8_COUNT_VAL:
		count = DP83869_DOWNSHIFT_8_COUNT;
		break;
	default:
		return -EINVAL;
	}

	*data = enable ? count : DOWNSHIFT_DEV_DISABLE;

	return 0;
}

static int dp83869_set_downshift(struct phy_device *phydev, u8 cnt)
{
	int val, count;

	if (cnt > DP83869_DOWNSHIFT_8_COUNT)
		return -EINVAL;

	if (!cnt)
		return phy_clear_bits(phydev, DP83869_CFG2,
				      DP83869_DOWNSHIFT_EN);

	switch (cnt) {
	case DP83869_DOWNSHIFT_1_COUNT:
		count = DP83869_DOWNSHIFT_1_COUNT_VAL;
		break;
	case DP83869_DOWNSHIFT_2_COUNT:
		count = DP83869_DOWNSHIFT_2_COUNT_VAL;
		break;
	case DP83869_DOWNSHIFT_4_COUNT:
		count = DP83869_DOWNSHIFT_4_COUNT_VAL;
		break;
	case DP83869_DOWNSHIFT_8_COUNT:
		count = DP83869_DOWNSHIFT_8_COUNT_VAL;
		break;
	default:
		phydev_err(phydev,
			   "Downshift count must be 1, 2, 4 or 8\n");
		return -EINVAL;
	}

	val = DP83869_DOWNSHIFT_EN;
	val |= FIELD_PREP(DP83869_DOWNSHIFT_ATTEMPT_MASK, count);

	return phy_modify(phydev, DP83869_CFG2,
			  DP83869_DOWNSHIFT_EN | DP83869_DOWNSHIFT_ATTEMPT_MASK,
			  val);
}

static int dp83869_get_tunable(struct phy_device *phydev,
			       struct ethtool_tunable *tuna, void *data)
{
	switch (tuna->id) {
	case ETHTOOL_PHY_DOWNSHIFT:
		return dp83869_get_downshift(phydev, data);
	default:
		return -EOPNOTSUPP;
	}
}

static int dp83869_set_tunable(struct phy_device *phydev,
			       struct ethtool_tunable *tuna, const void *data)
{
	switch (tuna->id) {
	case ETHTOOL_PHY_DOWNSHIFT:
		return dp83869_set_downshift(phydev, *(const u8 *)data);
	default:
		return -EOPNOTSUPP;
	}
}

static int dp83869_config_port_mirroring(struct phy_device *phydev)
{
	struct dp83869_private *dp83869 = phydev->priv;

	if (dp83869->port_mirroring == DP83869_PORT_MIRRORING_EN)
		return phy_set_bits_mmd(phydev, DP83869_DEVADDR,
					DP83869_GEN_CFG3,
					DP83869_CFG3_PORT_MIRROR_EN);
	else
		return phy_clear_bits_mmd(phydev, DP83869_DEVADDR,
					  DP83869_GEN_CFG3,
					  DP83869_CFG3_PORT_MIRROR_EN);
}

static int dp83869_set_strapped_mode(struct phy_device *phydev)
{
	struct dp83869_private *dp83869 = phydev->priv;
	int val;

	val = phy_read_mmd(phydev, DP83869_DEVADDR, DP83869_STRAP_STS1);
	if (val < 0)
		return val;

	dp83869->mode = val & DP83869_STRAP_OP_MODE_MASK;

	return 0;
}

#if IS_ENABLED(CONFIG_OF_MDIO)
static const int dp83869_internal_delay[] = {250, 500, 750, 1000, 1250, 1500,
					     1750, 2000, 2250, 2500, 2750, 3000,
					     3250, 3500, 3750, 4000};

static int dp83869_of_init(struct phy_device *phydev)
{
	struct dp83869_private *dp83869 = phydev->priv;
	struct device *dev = &phydev->mdio.dev;
	struct device_node *of_node = dev->of_node;
	int delay_size = ARRAY_SIZE(dp83869_internal_delay);
	int ret;

	if (!of_node)
		return -ENODEV;

	dp83869->io_impedance = -EINVAL;

	/* Optional configuration */
	ret = of_property_read_u32(of_node, "ti,clk-output-sel",
				   &dp83869->clk_output_sel);
	if (ret || dp83869->clk_output_sel > DP83869_CLK_O_SEL_REF_CLK)
		dp83869->clk_output_sel = DP83869_CLK_O_SEL_REF_CLK;

	ret = of_property_read_u32(of_node, "ti,op-mode", &dp83869->mode);
	if (ret == 0) {
		if (dp83869->mode < DP83869_RGMII_COPPER_ETHERNET ||
		    dp83869->mode > DP83869_SGMII_COPPER_ETHERNET)
			return -EINVAL;
	} else {
		ret = dp83869_set_strapped_mode(phydev);
		if (ret)
			return ret;
	}

	if (of_property_read_bool(of_node, "ti,max-output-impedance"))
		dp83869->io_impedance = DP83869_IO_MUX_CFG_IO_IMPEDANCE_MAX;
	else if (of_property_read_bool(of_node, "ti,min-output-impedance"))
		dp83869->io_impedance = DP83869_IO_MUX_CFG_IO_IMPEDANCE_MIN;

	if (of_property_read_bool(of_node, "enet-phy-lane-swap")) {
		dp83869->port_mirroring = DP83869_PORT_MIRRORING_EN;
	} else {
		/* If the lane swap is not in the DT then check the straps */
		ret = phy_read_mmd(phydev, DP83869_DEVADDR, DP83869_STRAP_STS1);
		if (ret < 0)
			return ret;

		if (ret & DP83869_STRAP_MIRROR_ENABLED)
			dp83869->port_mirroring = DP83869_PORT_MIRRORING_EN;
		else
			dp83869->port_mirroring = DP83869_PORT_MIRRORING_DIS;

		ret = 0;
	}

	if (of_property_read_u32(of_node, "rx-fifo-depth",
				 &dp83869->rx_fifo_depth))
		dp83869->rx_fifo_depth = DP83869_PHYCR_FIFO_DEPTH_4_B_NIB;

	if (of_property_read_u32(of_node, "tx-fifo-depth",
				 &dp83869->tx_fifo_depth))
		dp83869->tx_fifo_depth = DP83869_PHYCR_FIFO_DEPTH_4_B_NIB;

	dp83869->rx_int_delay = phy_get_internal_delay(phydev, dev,
						       &dp83869_internal_delay[0],
						       delay_size, true);
	if (dp83869->rx_int_delay < 0)
		dp83869->rx_int_delay =
				dp83869_internal_delay[DP83869_CLK_DELAY_DEF];

	dp83869->tx_int_delay = phy_get_internal_delay(phydev, dev,
						       &dp83869_internal_delay[0],
						       delay_size, false);
	if (dp83869->tx_int_delay < 0)
		dp83869->tx_int_delay =
				dp83869_internal_delay[DP83869_CLK_DELAY_DEF];

	return ret;
}
#else
static int dp83869_of_init(struct phy_device *phydev)
{
	return dp83869_set_strapped_mode(phydev);
}
#endif /* CONFIG_OF_MDIO */

static int dp83869_configure_rgmii(struct phy_device *phydev,
				   struct dp83869_private *dp83869)
{
	int ret = 0, val;

	if (phy_interface_is_rgmii(phydev)) {
		val = phy_read(phydev, MII_DP83869_PHYCTRL);
		if (val < 0)
			return val;

		val &= ~DP83869_PHYCR_FIFO_DEPTH_MASK;
		val |= (dp83869->tx_fifo_depth << DP83869_TX_FIFO_SHIFT);
		val |= (dp83869->rx_fifo_depth << DP83869_RX_FIFO_SHIFT);

		ret = phy_write(phydev, MII_DP83869_PHYCTRL, val);
		if (ret)
			return ret;
	}

	if (dp83869->io_impedance >= 0)
		ret = phy_modify_mmd(phydev, DP83869_DEVADDR,
				     DP83869_IO_MUX_CFG,
				     DP83869_IO_MUX_CFG_IO_IMPEDANCE_CTRL,
				     dp83869->io_impedance &
				     DP83869_IO_MUX_CFG_IO_IMPEDANCE_CTRL);

	return ret;
}

static int dp83869_configure_fiber(struct phy_device *phydev,
				   struct dp83869_private *dp83869)
{
	int bmcr;
	int ret;

	/* Only allow advertising what this PHY supports */
	linkmode_and(phydev->advertising, phydev->advertising,
		     phydev->supported);

	linkmode_set_bit(ETHTOOL_LINK_MODE_FIBRE_BIT, phydev->supported);
	linkmode_set_bit(ADVERTISED_FIBRE, phydev->advertising);

	if (dp83869->mode == DP83869_RGMII_1000_BASE) {
		linkmode_set_bit(ETHTOOL_LINK_MODE_1000baseX_Full_BIT,
				 phydev->supported);
	} else {
		linkmode_set_bit(ETHTOOL_LINK_MODE_100baseFX_Full_BIT,
				 phydev->supported);
		linkmode_set_bit(ETHTOOL_LINK_MODE_100baseFX_Half_BIT,
				 phydev->supported);

		/* Auto neg is not supported in 100base FX mode */
		bmcr = phy_read(phydev, MII_BMCR);
		if (bmcr < 0)
			return bmcr;

		phydev->autoneg = AUTONEG_DISABLE;
		linkmode_clear_bit(ETHTOOL_LINK_MODE_Autoneg_BIT, phydev->supported);
		linkmode_clear_bit(ETHTOOL_LINK_MODE_Autoneg_BIT, phydev->advertising);

		if (bmcr & BMCR_ANENABLE) {
			ret =  phy_modify(phydev, MII_BMCR, BMCR_ANENABLE, 0);
			if (ret < 0)
				return ret;
		}
	}

	/* Update advertising from supported */
	linkmode_or(phydev->advertising, phydev->advertising,
		    phydev->supported);

	return 0;
}

static int dp83869_configure_mode(struct phy_device *phydev,
				  struct dp83869_private *dp83869)
{
	int phy_ctrl_val;
	int ret;

	if (dp83869->mode < DP83869_RGMII_COPPER_ETHERNET ||
	    dp83869->mode > DP83869_SGMII_COPPER_ETHERNET)
		return -EINVAL;

	/* Below init sequence for each operational mode is defined in
	 * section 9.4.8 of the datasheet.
	 */
	ret = phy_write_mmd(phydev, DP83869_DEVADDR, DP83869_OP_MODE,
			    dp83869->mode);
	if (ret)
		return ret;

	ret = phy_write(phydev, MII_BMCR, MII_DP83869_BMCR_DEFAULT);
	if (ret)
		return ret;

	phy_ctrl_val = (dp83869->rx_fifo_depth << DP83869_RX_FIFO_SHIFT |
			dp83869->tx_fifo_depth << DP83869_TX_FIFO_SHIFT |
			DP83869_PHY_CTRL_DEFAULT);

	switch (dp83869->mode) {
	case DP83869_RGMII_COPPER_ETHERNET:
		ret = phy_write(phydev, MII_DP83869_PHYCTRL,
				phy_ctrl_val);
		if (ret)
			return ret;

		ret = phy_write(phydev, MII_CTRL1000, DP83869_CFG1_DEFAULT);
		if (ret)
			return ret;

		ret = dp83869_configure_rgmii(phydev, dp83869);
		if (ret)
			return ret;
		break;
	case DP83869_RGMII_SGMII_BRIDGE:
		ret = phy_modify_mmd(phydev, DP83869_DEVADDR, DP83869_OP_MODE,
				     DP83869_SGMII_RGMII_BRIDGE,
				     DP83869_SGMII_RGMII_BRIDGE);
		if (ret)
			return ret;

		ret = phy_write_mmd(phydev, DP83869_DEVADDR,
				    DP83869_FX_CTRL, DP83869_FX_CTRL_DEFAULT);
		if (ret)
			return ret;

		break;
	case DP83869_1000M_MEDIA_CONVERT:
		ret = phy_write(phydev, MII_DP83869_PHYCTRL,
				phy_ctrl_val);
		if (ret)
			return ret;

		ret = phy_write_mmd(phydev, DP83869_DEVADDR,
				    DP83869_FX_CTRL, DP83869_FX_CTRL_DEFAULT);
		if (ret)
			return ret;
		break;
	case DP83869_100M_MEDIA_CONVERT:
		ret = phy_write(phydev, MII_DP83869_PHYCTRL,
				phy_ctrl_val);
		if (ret)
			return ret;
		break;
	case DP83869_SGMII_COPPER_ETHERNET:
		ret = phy_write(phydev, MII_DP83869_PHYCTRL,
				phy_ctrl_val);
		if (ret)
			return ret;

		ret = phy_write(phydev, MII_CTRL1000, DP83869_CFG1_DEFAULT);
		if (ret)
			return ret;

		ret = phy_write_mmd(phydev, DP83869_DEVADDR,
				    DP83869_FX_CTRL, DP83869_FX_CTRL_DEFAULT);
		if (ret)
			return ret;

		break;
	case DP83869_RGMII_1000_BASE:
	case DP83869_RGMII_100_BASE:
		ret = dp83869_configure_fiber(phydev, dp83869);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int dp83869_config_init(struct phy_device *phydev)
{
	struct dp83869_private *dp83869 = phydev->priv;
	int ret, val;

	/* Force speed optimization for the PHY even if it strapped */
	ret = phy_modify(phydev, DP83869_CFG2, DP83869_DOWNSHIFT_EN,
			 DP83869_DOWNSHIFT_EN);
	if (ret)
		return ret;

	ret = dp83869_configure_mode(phydev, dp83869);
	if (ret)
		return ret;

	/* Enable Interrupt output INT_OE in CFG4 register */
	if (phy_interrupt_is_valid(phydev)) {
		val = phy_read(phydev, DP83869_CFG4);
		val |= DP83869_INT_OE;
		phy_write(phydev, DP83869_CFG4, val);
	}

	if (dp83869->port_mirroring != DP83869_PORT_MIRRORING_KEEP)
		dp83869_config_port_mirroring(phydev);

	/* Clock output selection if muxing property is set */
	if (dp83869->clk_output_sel != DP83869_CLK_O_SEL_REF_CLK)
		ret = phy_modify_mmd(phydev,
				     DP83869_DEVADDR, DP83869_IO_MUX_CFG,
				     DP83869_IO_MUX_CFG_CLK_O_SEL_MASK,
				     dp83869->clk_output_sel <<
				     DP83869_IO_MUX_CFG_CLK_O_SEL_SHIFT);

	if (phy_interface_is_rgmii(phydev)) {
		ret = phy_write_mmd(phydev, DP83869_DEVADDR, DP83869_RGMIIDCTL,
				    dp83869->rx_int_delay |
			dp83869->tx_int_delay << DP83869_RGMII_CLK_DELAY_SHIFT);
		if (ret)
			return ret;

		val = phy_read_mmd(phydev, DP83869_DEVADDR, DP83869_RGMIICTL);
		val |= (DP83869_RGMII_TX_CLK_DELAY_EN |
			DP83869_RGMII_RX_CLK_DELAY_EN);

		if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID)
			val &= ~(DP83869_RGMII_TX_CLK_DELAY_EN |
				 DP83869_RGMII_RX_CLK_DELAY_EN);

		if (phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID)
			val &= ~DP83869_RGMII_TX_CLK_DELAY_EN;

		if (phydev->interface == PHY_INTERFACE_MODE_RGMII_RXID)
			val &= ~DP83869_RGMII_RX_CLK_DELAY_EN;

		ret = phy_write_mmd(phydev, DP83869_DEVADDR, DP83869_RGMIICTL,
				    val);
	}

	return ret;
}

static int dp83869_probe(struct phy_device *phydev)
{
	struct dp83869_private *dp83869;
	int ret;

	dp83869 = devm_kzalloc(&phydev->mdio.dev, sizeof(*dp83869),
			       GFP_KERNEL);
	if (!dp83869)
		return -ENOMEM;

	phydev->priv = dp83869;

	ret = dp83869_of_init(phydev);
	if (ret)
		return ret;

	if (dp83869->mode == DP83869_RGMII_100_BASE ||
	    dp83869->mode == DP83869_RGMII_1000_BASE)
		phydev->port = PORT_FIBRE;

	return dp83869_config_init(phydev);
}

static int dp83869_phy_reset(struct phy_device *phydev)
{
	int ret;

	ret = phy_write(phydev, DP83869_CTRL, DP83869_SW_RESET);
	if (ret < 0)
		return ret;

	usleep_range(10, 20);

	/* Global sw reset sets all registers to default.
	 * Need to set the registers in the PHY to the right config.
	 */
	return dp83869_config_init(phydev);
}


#define DP83869_PHY_DRIVER(_id, _name)				\
{								\
	PHY_ID_MATCH_MODEL(_id),				\
	.name		= (_name),				\
	.probe          = dp83869_probe,			\
	.config_init	= dp83869_config_init,			\
	.soft_reset	= dp83869_phy_reset,			\
	.config_intr	= dp83869_config_intr,			\
	.handle_interrupt = dp83869_handle_interrupt,		\
	.read_status	= dp83869_read_status,			\
	.get_tunable	= dp83869_get_tunable,			\
	.set_tunable	= dp83869_set_tunable,			\
	.get_wol	= dp83869_get_wol,			\
	.set_wol	= dp83869_set_wol,			\
	.suspend	= genphy_suspend,			\
	.resume		= genphy_resume,			\
}

static struct phy_driver dp83869_driver[] = {
	DP83869_PHY_DRIVER(DP83869_PHY_ID, "TI DP83869"),
	DP83869_PHY_DRIVER(DP83561_PHY_ID, "TI DP83561-SP"),

};
module_phy_driver(dp83869_driver);

static struct mdio_device_id __maybe_unused dp83869_tbl[] = {
	{ PHY_ID_MATCH_MODEL(DP83869_PHY_ID) },
	{ PHY_ID_MATCH_MODEL(DP83561_PHY_ID) },
	{ }
};
MODULE_DEVICE_TABLE(mdio, dp83869_tbl);

MODULE_DESCRIPTION("Texas Instruments DP83869 PHY driver");
MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com");
MODULE_LICENSE("GPL v2");
