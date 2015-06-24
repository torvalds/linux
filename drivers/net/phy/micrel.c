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
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Support : Micrel Phys:
 *		Giga phys: ksz9021, ksz9031
 *		100/10 Phys : ksz8001, ksz8721, ksz8737, ksz8041
 *			   ksz8021, ksz8031, ksz8051,
 *			   ksz8081, ksz8091,
 *			   ksz8061,
 *		Switch : ksz8873, ksz886x
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/micrel_phy.h>
#include <linux/of.h>
#include <linux/clk.h>

/* Operation Mode Strap Override */
#define MII_KSZPHY_OMSO				0x16
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
		dev_err(&phydev->dev, "failed to set led mode\n");

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
		dev_err(&phydev->dev, "failed to disable broadcast address\n");

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
		dev_err(&phydev->dev, "failed to disable NAND tree mode\n");

	return ret;
}

static int kszphy_config_init(struct phy_device *phydev)
{
	struct kszphy_priv *priv = phydev->priv;
	const struct kszphy_type *type;
	int ret;

	if (!priv)
		return 0;

	type = priv->type;

	if (type->has_broadcast_disable)
		kszphy_broadcast_disable(phydev);

	if (type->has_nand_tree_disable)
		kszphy_nand_tree_disable(phydev);

	if (priv->rmii_ref_clk_sel) {
		ret = kszphy_rmii_clk_sel(phydev, priv->rmii_ref_clk_sel_val);
		if (ret) {
			dev_err(&phydev->dev, "failed to set rmii reference clock\n");
			return ret;
		}
	}

	if (priv->led_mode >= 0)
		kszphy_setup_led(phydev, type->led_mode_reg, priv->led_mode);

	return 0;
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
	const struct device *dev = &phydev->dev;
	const struct device_node *of_node = dev->of_node;

	if (!of_node && dev->parent->of_node)
		of_node = dev->parent->of_node;

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

#define MII_KSZ9031RN_MMD_CTRL_REG	0x0d
#define MII_KSZ9031RN_MMD_REGDATA_REG	0x0e
#define OP_DATA				1
#define KSZ9031_PS_TO_REG		60

/* Extended registers */
/* MMD Address 0x0 */
#define MII_KSZ9031RN_FLP_BURST_TX_LO	3
#define MII_KSZ9031RN_FLP_BURST_TX_HI	4

/* MMD Address 0x2 */
#define MII_KSZ9031RN_CONTROL_PAD_SKEW	4
#define MII_KSZ9031RN_RX_DATA_PAD_SKEW	5
#define MII_KSZ9031RN_TX_DATA_PAD_SKEW	6
#define MII_KSZ9031RN_CLK_PAD_SKEW	8

static int ksz9031_extended_write(struct phy_device *phydev,
				  u8 mode, u32 dev_addr, u32 regnum, u16 val)
{
	phy_write(phydev, MII_KSZ9031RN_MMD_CTRL_REG, dev_addr);
	phy_write(phydev, MII_KSZ9031RN_MMD_REGDATA_REG, regnum);
	phy_write(phydev, MII_KSZ9031RN_MMD_CTRL_REG, (mode << 14) | dev_addr);
	return phy_write(phydev, MII_KSZ9031RN_MMD_REGDATA_REG, val);
}

static int ksz9031_extended_read(struct phy_device *phydev,
				 u8 mode, u32 dev_addr, u32 regnum)
{
	phy_write(phydev, MII_KSZ9031RN_MMD_CTRL_REG, dev_addr);
	phy_write(phydev, MII_KSZ9031RN_MMD_REGDATA_REG, regnum);
	phy_write(phydev, MII_KSZ9031RN_MMD_CTRL_REG, (mode << 14) | dev_addr);
	return phy_read(phydev, MII_KSZ9031RN_MMD_REGDATA_REG);
}

static int ksz9031_of_load_skew_values(struct phy_device *phydev,
				       const struct device_node *of_node,
				       u16 reg, size_t field_sz,
				       const char *field[], u8 numfields)
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

	if (matches < numfields)
		newval = ksz9031_extended_read(phydev, OP_DATA, 2, reg);
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

	return ksz9031_extended_write(phydev, OP_DATA, 2, reg, newval);
}

static int ksz9031_center_flp_timing(struct phy_device *phydev)
{
	int result;

	/* Center KSZ9031RNX FLP timing at 16ms. */
	result = ksz9031_extended_write(phydev, OP_DATA, 0,
					MII_KSZ9031RN_FLP_BURST_TX_HI, 0x0006);
	result = ksz9031_extended_write(phydev, OP_DATA, 0,
					MII_KSZ9031RN_FLP_BURST_TX_LO, 0x1A80);

	if (result)
		return result;

	return genphy_restart_aneg(phydev);
}

static int ksz9031_config_init(struct phy_device *phydev)
{
	const struct device *dev = &phydev->dev;
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

	if (!of_node && dev->parent->of_node)
		of_node = dev->parent->of_node;

	if (of_node) {
		ksz9031_of_load_skew_values(phydev, of_node,
				MII_KSZ9031RN_CLK_PAD_SKEW, 5,
				clk_skews, 2);

		ksz9031_of_load_skew_values(phydev, of_node,
				MII_KSZ9031RN_CONTROL_PAD_SKEW, 4,
				control_skews, 2);

		ksz9031_of_load_skew_values(phydev, of_node,
				MII_KSZ9031RN_RX_DATA_PAD_SKEW, 4,
				rx_data_skews, 4);

		ksz9031_of_load_skew_values(phydev, of_node,
				MII_KSZ9031RN_TX_DATA_PAD_SKEW, 4,
				tx_data_skews, 4);
	}

	return ksz9031_center_flp_timing(phydev);
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

static int ksz8873mll_config_aneg(struct phy_device *phydev)
{
	return 0;
}

/* This routine returns -1 as an indication to the caller that the
 * Micrel ksz9021 10/100/1000 PHY does not support standard IEEE
 * MMD extended PHY registers.
 */
static int
ksz9021_rd_mmd_phyreg(struct phy_device *phydev, int ptrad, int devnum,
		      int regnum)
{
	return -1;
}

/* This routine does nothing since the Micrel ksz9021 does not support
 * standard IEEE MMD extended PHY registers.
 */
static void
ksz9021_wr_mmd_phyreg(struct phy_device *phydev, int ptrad, int devnum,
		      int regnum, u32 val)
{
}

static int kszphy_probe(struct phy_device *phydev)
{
	const struct kszphy_type *type = phydev->drv->driver_data;
	const struct device_node *np = phydev->dev.of_node;
	struct kszphy_priv *priv;
	struct clk *clk;
	int ret;

	priv = devm_kzalloc(&phydev->dev, sizeof(*priv), GFP_KERNEL);
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
			dev_err(&phydev->dev, "invalid led mode: 0x%02x\n",
					priv->led_mode);
			priv->led_mode = -1;
		}
	} else {
		priv->led_mode = -1;
	}

	clk = devm_clk_get(&phydev->dev, "rmii-ref");
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
			dev_err(&phydev->dev, "Clock rate out of range: %ld\n", rate);
			return -EINVAL;
		}
	}

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
	.phy_id_mask	= 0x00fffff0,
	.name		= "Micrel KS8737",
	.features	= (PHY_BASIC_FEATURES | SUPPORTED_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.driver_data	= &ks8737_type,
	.config_init	= kszphy_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE,},
}, {
	.phy_id		= PHY_ID_KSZ8021,
	.phy_id_mask	= 0x00ffffff,
	.name		= "Micrel KSZ8021 or KSZ8031",
	.features	= (PHY_BASIC_FEATURES | SUPPORTED_Pause |
			   SUPPORTED_Asym_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.driver_data	= &ksz8021_type,
	.probe		= kszphy_probe,
	.config_init	= kszphy_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE,},
}, {
	.phy_id		= PHY_ID_KSZ8031,
	.phy_id_mask	= 0x00ffffff,
	.name		= "Micrel KSZ8031",
	.features	= (PHY_BASIC_FEATURES | SUPPORTED_Pause |
			   SUPPORTED_Asym_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.driver_data	= &ksz8021_type,
	.probe		= kszphy_probe,
	.config_init	= kszphy_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE,},
}, {
	.phy_id		= PHY_ID_KSZ8041,
	.phy_id_mask	= 0x00fffff0,
	.name		= "Micrel KSZ8041",
	.features	= (PHY_BASIC_FEATURES | SUPPORTED_Pause
				| SUPPORTED_Asym_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.driver_data	= &ksz8041_type,
	.probe		= kszphy_probe,
	.config_init	= kszphy_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE,},
}, {
	.phy_id		= PHY_ID_KSZ8041RNLI,
	.phy_id_mask	= 0x00fffff0,
	.name		= "Micrel KSZ8041RNLI",
	.features	= PHY_BASIC_FEATURES |
			  SUPPORTED_Pause | SUPPORTED_Asym_Pause,
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.driver_data	= &ksz8041_type,
	.probe		= kszphy_probe,
	.config_init	= kszphy_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE,},
}, {
	.phy_id		= PHY_ID_KSZ8051,
	.phy_id_mask	= 0x00fffff0,
	.name		= "Micrel KSZ8051",
	.features	= (PHY_BASIC_FEATURES | SUPPORTED_Pause
				| SUPPORTED_Asym_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.driver_data	= &ksz8051_type,
	.probe		= kszphy_probe,
	.config_init	= kszphy_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE,},
}, {
	.phy_id		= PHY_ID_KSZ8001,
	.name		= "Micrel KSZ8001 or KS8721",
	.phy_id_mask	= 0x00ffffff,
	.features	= (PHY_BASIC_FEATURES | SUPPORTED_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.driver_data	= &ksz8041_type,
	.probe		= kszphy_probe,
	.config_init	= kszphy_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE,},
}, {
	.phy_id		= PHY_ID_KSZ8081,
	.name		= "Micrel KSZ8081 or KSZ8091",
	.phy_id_mask	= 0x00fffff0,
	.features	= (PHY_BASIC_FEATURES | SUPPORTED_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.driver_data	= &ksz8081_type,
	.probe		= kszphy_probe,
	.config_init	= kszphy_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE,},
}, {
	.phy_id		= PHY_ID_KSZ8061,
	.name		= "Micrel KSZ8061",
	.phy_id_mask	= 0x00fffff0,
	.features	= (PHY_BASIC_FEATURES | SUPPORTED_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= kszphy_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE,},
}, {
	.phy_id		= PHY_ID_KSZ9021,
	.phy_id_mask	= 0x000ffffe,
	.name		= "Micrel KSZ9021 Gigabit PHY",
	.features	= (PHY_GBIT_FEATURES | SUPPORTED_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.driver_data	= &ksz9021_type,
	.config_init	= ksz9021_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.read_mmd_indirect = ksz9021_rd_mmd_phyreg,
	.write_mmd_indirect = ksz9021_wr_mmd_phyreg,
	.driver		= { .owner = THIS_MODULE, },
}, {
	.phy_id		= PHY_ID_KSZ9031,
	.phy_id_mask	= 0x00fffff0,
	.name		= "Micrel KSZ9031 Gigabit PHY",
	.features	= (PHY_GBIT_FEATURES | SUPPORTED_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.driver_data	= &ksz9021_type,
	.config_init	= ksz9031_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt	= kszphy_ack_interrupt,
	.config_intr	= kszphy_config_intr,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE, },
}, {
	.phy_id		= PHY_ID_KSZ8873MLL,
	.phy_id_mask	= 0x00fffff0,
	.name		= "Micrel KSZ8873MLL Switch",
	.features	= (SUPPORTED_Pause | SUPPORTED_Asym_Pause),
	.flags		= PHY_HAS_MAGICANEG,
	.config_init	= kszphy_config_init,
	.config_aneg	= ksz8873mll_config_aneg,
	.read_status	= ksz8873mll_read_status,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE, },
}, {
	.phy_id		= PHY_ID_KSZ886X,
	.phy_id_mask	= 0x00fffff0,
	.name		= "Micrel KSZ886X Switch",
	.features	= (PHY_BASIC_FEATURES | SUPPORTED_Pause),
	.flags		= PHY_HAS_MAGICANEG | PHY_HAS_INTERRUPT,
	.config_init	= kszphy_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE, },
} };

module_phy_driver(ksphy_driver);

MODULE_DESCRIPTION("Micrel PHY driver");
MODULE_AUTHOR("David J. Choi");
MODULE_LICENSE("GPL");

static struct mdio_device_id __maybe_unused micrel_tbl[] = {
	{ PHY_ID_KSZ9021, 0x000ffffe },
	{ PHY_ID_KSZ9031, 0x00fffff0 },
	{ PHY_ID_KSZ8001, 0x00ffffff },
	{ PHY_ID_KS8737, 0x00fffff0 },
	{ PHY_ID_KSZ8021, 0x00ffffff },
	{ PHY_ID_KSZ8031, 0x00ffffff },
	{ PHY_ID_KSZ8041, 0x00fffff0 },
	{ PHY_ID_KSZ8051, 0x00fffff0 },
	{ PHY_ID_KSZ8061, 0x00fffff0 },
	{ PHY_ID_KSZ8081, 0x00fffff0 },
	{ PHY_ID_KSZ8873MLL, 0x00fffff0 },
	{ PHY_ID_KSZ886X, 0x00fffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, micrel_tbl);
