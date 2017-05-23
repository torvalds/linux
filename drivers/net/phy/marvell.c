/*
 * drivers/net/phy/marvell.c
 *
 * Driver for Marvell PHYs
 *
 * Author: Andy Fleming
 *
 * Copyright (c) 2004 Freescale Semiconductor, Inc.
 *
 * Copyright (c) 2013 Michael Stapelberg <michael@stapelberg.de>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/marvell_phy.h>
#include <linux/of.h>

#include <linux/io.h>
#include <asm/irq.h>
#include <linux/uaccess.h>

#define MII_MARVELL_PHY_PAGE		22

#define MII_M1011_IEVENT		0x13
#define MII_M1011_IEVENT_CLEAR		0x0000

#define MII_M1011_IMASK			0x12
#define MII_M1011_IMASK_INIT		0x6400
#define MII_M1011_IMASK_CLEAR		0x0000

#define MII_M1011_PHY_SCR		0x10
#define MII_M1011_PHY_SCR_MDI		0x0000
#define MII_M1011_PHY_SCR_MDI_X		0x0020
#define MII_M1011_PHY_SCR_AUTO_CROSS	0x0060

#define MII_M1145_PHY_EXT_ADDR_PAGE	0x16
#define MII_M1145_PHY_EXT_SR		0x1b
#define MII_M1145_PHY_EXT_CR		0x14
#define MII_M1145_RGMII_RX_DELAY	0x0080
#define MII_M1145_RGMII_TX_DELAY	0x0002
#define MII_M1145_HWCFG_MODE_SGMII_NO_CLK	0x4
#define MII_M1145_HWCFG_MODE_MASK		0xf
#define MII_M1145_HWCFG_FIBER_COPPER_AUTO	0x8000

#define MII_M1145_HWCFG_MODE_SGMII_NO_CLK	0x4
#define MII_M1145_HWCFG_MODE_MASK		0xf
#define MII_M1145_HWCFG_FIBER_COPPER_AUTO	0x8000

#define MII_M1111_PHY_LED_CONTROL	0x18
#define MII_M1111_PHY_LED_DIRECT	0x4100
#define MII_M1111_PHY_LED_COMBINE	0x411c
#define MII_M1111_PHY_EXT_CR		0x14
#define MII_M1111_RX_DELAY		0x80
#define MII_M1111_TX_DELAY		0x2
#define MII_M1111_PHY_EXT_SR		0x1b

#define MII_M1111_HWCFG_MODE_MASK		0xf
#define MII_M1111_HWCFG_MODE_COPPER_RGMII	0xb
#define MII_M1111_HWCFG_MODE_FIBER_RGMII	0x3
#define MII_M1111_HWCFG_MODE_SGMII_NO_CLK	0x4
#define MII_M1111_HWCFG_MODE_COPPER_RTBI	0x9
#define MII_M1111_HWCFG_FIBER_COPPER_AUTO	0x8000
#define MII_M1111_HWCFG_FIBER_COPPER_RES	0x2000

#define MII_M1111_COPPER		0
#define MII_M1111_FIBER			1

#define MII_88E1121_PHY_MSCR_PAGE	2
#define MII_88E1121_PHY_MSCR_REG	21
#define MII_88E1121_PHY_MSCR_RX_DELAY	BIT(5)
#define MII_88E1121_PHY_MSCR_TX_DELAY	BIT(4)
#define MII_88E1121_PHY_MSCR_DELAY_MASK	(~(0x3 << 4))

#define MII_88E1318S_PHY_MSCR1_REG	16
#define MII_88E1318S_PHY_MSCR1_PAD_ODD	BIT(6)

/* Copper Specific Interrupt Enable Register */
#define MII_88E1318S_PHY_CSIER                              0x12
/* WOL Event Interrupt Enable */
#define MII_88E1318S_PHY_CSIER_WOL_EIE                      BIT(7)

/* LED Timer Control Register */
#define MII_88E1318S_PHY_LED_PAGE                           0x03
#define MII_88E1318S_PHY_LED_TCR                            0x12
#define MII_88E1318S_PHY_LED_TCR_FORCE_INT                  BIT(15)
#define MII_88E1318S_PHY_LED_TCR_INTn_ENABLE                BIT(7)
#define MII_88E1318S_PHY_LED_TCR_INT_ACTIVE_LOW             BIT(11)

/* Magic Packet MAC address registers */
#define MII_88E1318S_PHY_MAGIC_PACKET_WORD2                 0x17
#define MII_88E1318S_PHY_MAGIC_PACKET_WORD1                 0x18
#define MII_88E1318S_PHY_MAGIC_PACKET_WORD0                 0x19

#define MII_88E1318S_PHY_WOL_PAGE                           0x11
#define MII_88E1318S_PHY_WOL_CTRL                           0x10
#define MII_88E1318S_PHY_WOL_CTRL_CLEAR_WOL_STATUS          BIT(12)
#define MII_88E1318S_PHY_WOL_CTRL_MAGIC_PACKET_MATCH_ENABLE BIT(14)

#define MII_88E1121_PHY_LED_CTRL	16
#define MII_88E1121_PHY_LED_PAGE	3
#define MII_88E1121_PHY_LED_DEF		0x0030

#define MII_M1011_PHY_STATUS		0x11
#define MII_M1011_PHY_STATUS_1000	0x8000
#define MII_M1011_PHY_STATUS_100	0x4000
#define MII_M1011_PHY_STATUS_SPD_MASK	0xc000
#define MII_M1011_PHY_STATUS_FULLDUPLEX	0x2000
#define MII_M1011_PHY_STATUS_RESOLVED	0x0800
#define MII_M1011_PHY_STATUS_LINK	0x0400

#define MII_M1116R_CONTROL_REG_MAC	21

#define MII_88E3016_PHY_SPEC_CTRL	0x10
#define MII_88E3016_DISABLE_SCRAMBLER	0x0200
#define MII_88E3016_AUTO_MDIX_CROSSOVER	0x0030

#define MII_88E1510_GEN_CTRL_REG_1		0x14
#define MII_88E1510_GEN_CTRL_REG_1_MODE_MASK	0x7
#define MII_88E1510_GEN_CTRL_REG_1_MODE_SGMII	0x1	/* SGMII to copper */
#define MII_88E1510_GEN_CTRL_REG_1_RESET	0x8000	/* Soft reset */

#define LPA_FIBER_1000HALF	0x40
#define LPA_FIBER_1000FULL	0x20

#define LPA_PAUSE_FIBER	0x180
#define LPA_PAUSE_ASYM_FIBER	0x100

#define ADVERTISE_FIBER_1000HALF	0x40
#define ADVERTISE_FIBER_1000FULL	0x20

#define ADVERTISE_PAUSE_FIBER		0x180
#define ADVERTISE_PAUSE_ASYM_FIBER	0x100

#define REGISTER_LINK_STATUS	0x400
#define NB_FIBER_STATS	1

MODULE_DESCRIPTION("Marvell PHY driver");
MODULE_AUTHOR("Andy Fleming");
MODULE_LICENSE("GPL");

struct marvell_hw_stat {
	const char *string;
	u8 page;
	u8 reg;
	u8 bits;
};

static struct marvell_hw_stat marvell_hw_stats[] = {
	{ "phy_receive_errors_copper", 0, 21, 16},
	{ "phy_idle_errors", 0, 10, 8 },
	{ "phy_receive_errors_fiber", 1, 21, 16},
};

struct marvell_priv {
	u64 stats[ARRAY_SIZE(marvell_hw_stats)];
};

static int marvell_ack_interrupt(struct phy_device *phydev)
{
	int err;

	/* Clear the interrupts by reading the reg */
	err = phy_read(phydev, MII_M1011_IEVENT);

	if (err < 0)
		return err;

	return 0;
}

static int marvell_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_write(phydev, MII_M1011_IMASK, MII_M1011_IMASK_INIT);
	else
		err = phy_write(phydev, MII_M1011_IMASK, MII_M1011_IMASK_CLEAR);

	return err;
}

static int marvell_set_polarity(struct phy_device *phydev, int polarity)
{
	int reg;
	int err;
	int val;

	/* get the current settings */
	reg = phy_read(phydev, MII_M1011_PHY_SCR);
	if (reg < 0)
		return reg;

	val = reg;
	val &= ~MII_M1011_PHY_SCR_AUTO_CROSS;
	switch (polarity) {
	case ETH_TP_MDI:
		val |= MII_M1011_PHY_SCR_MDI;
		break;
	case ETH_TP_MDI_X:
		val |= MII_M1011_PHY_SCR_MDI_X;
		break;
	case ETH_TP_MDI_AUTO:
	case ETH_TP_MDI_INVALID:
	default:
		val |= MII_M1011_PHY_SCR_AUTO_CROSS;
		break;
	}

	if (val != reg) {
		/* Set the new polarity value in the register */
		err = phy_write(phydev, MII_M1011_PHY_SCR, val);
		if (err)
			return err;
	}

	return 0;
}

static int marvell_config_aneg(struct phy_device *phydev)
{
	int err;

	err = marvell_set_polarity(phydev, phydev->mdix);
	if (err < 0)
		return err;

	err = phy_write(phydev, MII_M1111_PHY_LED_CONTROL,
			MII_M1111_PHY_LED_DIRECT);
	if (err < 0)
		return err;

	err = genphy_config_aneg(phydev);
	if (err < 0)
		return err;

	if (phydev->autoneg != AUTONEG_ENABLE) {
		int bmcr;

		/*
		 * A write to speed/duplex bits (that is performed by
		 * genphy_config_aneg() call above) must be followed by
		 * a software reset. Otherwise, the write has no effect.
		 */
		bmcr = phy_read(phydev, MII_BMCR);
		if (bmcr < 0)
			return bmcr;

		err = phy_write(phydev, MII_BMCR, bmcr | BMCR_RESET);
		if (err < 0)
			return err;
	}

	return 0;
}

static int m88e1101_config_aneg(struct phy_device *phydev)
{
	int err;

	/* This Marvell PHY has an errata which requires
	 * that certain registers get written in order
	 * to restart autonegotiation
	 */
	err = phy_write(phydev, MII_BMCR, BMCR_RESET);

	if (err < 0)
		return err;

	err = phy_write(phydev, 0x1d, 0x1f);
	if (err < 0)
		return err;

	err = phy_write(phydev, 0x1e, 0x200c);
	if (err < 0)
		return err;

	err = phy_write(phydev, 0x1d, 0x5);
	if (err < 0)
		return err;

	err = phy_write(phydev, 0x1e, 0);
	if (err < 0)
		return err;

	err = phy_write(phydev, 0x1e, 0x100);
	if (err < 0)
		return err;

	return marvell_config_aneg(phydev);
}

static int m88e1111_config_aneg(struct phy_device *phydev)
{
	int err;

	/* The Marvell PHY has an errata which requires
	 * that certain registers get written in order
	 * to restart autonegotiation
	 */
	err = phy_write(phydev, MII_BMCR, BMCR_RESET);

	err = marvell_set_polarity(phydev, phydev->mdix);
	if (err < 0)
		return err;

	err = phy_write(phydev, MII_M1111_PHY_LED_CONTROL,
			MII_M1111_PHY_LED_DIRECT);
	if (err < 0)
		return err;

	err = genphy_config_aneg(phydev);
	if (err < 0)
		return err;

	if (phydev->autoneg != AUTONEG_ENABLE) {
		int bmcr;

		/* A write to speed/duplex bits (that is performed by
		 * genphy_config_aneg() call above) must be followed by
		 * a software reset. Otherwise, the write has no effect.
		 */
		bmcr = phy_read(phydev, MII_BMCR);
		if (bmcr < 0)
			return bmcr;

		err = phy_write(phydev, MII_BMCR, bmcr | BMCR_RESET);
		if (err < 0)
			return err;
	}

	return 0;
}

#ifdef CONFIG_OF_MDIO
/*
 * Set and/or override some configuration registers based on the
 * marvell,reg-init property stored in the of_node for the phydev.
 *
 * marvell,reg-init = <reg-page reg mask value>,...;
 *
 * There may be one or more sets of <reg-page reg mask value>:
 *
 * reg-page: which register bank to use.
 * reg: the register.
 * mask: if non-zero, ANDed with existing register value.
 * value: ORed with the masked value and written to the regiser.
 *
 */
static int marvell_of_reg_init(struct phy_device *phydev)
{
	const __be32 *paddr;
	int len, i, saved_page, current_page, page_changed, ret;

	if (!phydev->mdio.dev.of_node)
		return 0;

	paddr = of_get_property(phydev->mdio.dev.of_node,
				"marvell,reg-init", &len);
	if (!paddr || len < (4 * sizeof(*paddr)))
		return 0;

	saved_page = phy_read(phydev, MII_MARVELL_PHY_PAGE);
	if (saved_page < 0)
		return saved_page;
	page_changed = 0;
	current_page = saved_page;

	ret = 0;
	len /= sizeof(*paddr);
	for (i = 0; i < len - 3; i += 4) {
		u16 reg_page = be32_to_cpup(paddr + i);
		u16 reg = be32_to_cpup(paddr + i + 1);
		u16 mask = be32_to_cpup(paddr + i + 2);
		u16 val_bits = be32_to_cpup(paddr + i + 3);
		int val;

		if (reg_page != current_page) {
			current_page = reg_page;
			page_changed = 1;
			ret = phy_write(phydev, MII_MARVELL_PHY_PAGE, reg_page);
			if (ret < 0)
				goto err;
		}

		val = 0;
		if (mask) {
			val = phy_read(phydev, reg);
			if (val < 0) {
				ret = val;
				goto err;
			}
			val &= mask;
		}
		val |= val_bits;

		ret = phy_write(phydev, reg, val);
		if (ret < 0)
			goto err;

	}
err:
	if (page_changed) {
		i = phy_write(phydev, MII_MARVELL_PHY_PAGE, saved_page);
		if (ret == 0)
			ret = i;
	}
	return ret;
}
#else
static int marvell_of_reg_init(struct phy_device *phydev)
{
	return 0;
}
#endif /* CONFIG_OF_MDIO */

static int m88e1121_config_aneg(struct phy_device *phydev)
{
	int err, oldpage, mscr;

	oldpage = phy_read(phydev, MII_MARVELL_PHY_PAGE);

	err = phy_write(phydev, MII_MARVELL_PHY_PAGE,
			MII_88E1121_PHY_MSCR_PAGE);
	if (err < 0)
		return err;

	if (phy_interface_is_rgmii(phydev)) {

		mscr = phy_read(phydev, MII_88E1121_PHY_MSCR_REG) &
			MII_88E1121_PHY_MSCR_DELAY_MASK;

		if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID)
			mscr |= (MII_88E1121_PHY_MSCR_RX_DELAY |
				 MII_88E1121_PHY_MSCR_TX_DELAY);
		else if (phydev->interface == PHY_INTERFACE_MODE_RGMII_RXID)
			mscr |= MII_88E1121_PHY_MSCR_RX_DELAY;
		else if (phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID)
			mscr |= MII_88E1121_PHY_MSCR_TX_DELAY;

		err = phy_write(phydev, MII_88E1121_PHY_MSCR_REG, mscr);
		if (err < 0)
			return err;
	}

	phy_write(phydev, MII_MARVELL_PHY_PAGE, oldpage);

	err = phy_write(phydev, MII_BMCR, BMCR_RESET);
	if (err < 0)
		return err;

	err = phy_write(phydev, MII_M1011_PHY_SCR,
			MII_M1011_PHY_SCR_AUTO_CROSS);
	if (err < 0)
		return err;

	return genphy_config_aneg(phydev);
}

static int m88e1318_config_aneg(struct phy_device *phydev)
{
	int err, oldpage, mscr;

	oldpage = phy_read(phydev, MII_MARVELL_PHY_PAGE);

	err = phy_write(phydev, MII_MARVELL_PHY_PAGE,
			MII_88E1121_PHY_MSCR_PAGE);
	if (err < 0)
		return err;

	mscr = phy_read(phydev, MII_88E1318S_PHY_MSCR1_REG);
	mscr |= MII_88E1318S_PHY_MSCR1_PAD_ODD;

	err = phy_write(phydev, MII_88E1318S_PHY_MSCR1_REG, mscr);
	if (err < 0)
		return err;

	err = phy_write(phydev, MII_MARVELL_PHY_PAGE, oldpage);
	if (err < 0)
		return err;

	return m88e1121_config_aneg(phydev);
}

/**
 * ethtool_adv_to_fiber_adv_t
 * @ethadv: the ethtool advertisement settings
 *
 * A small helper function that translates ethtool advertisement
 * settings to phy autonegotiation advertisements for the
 * MII_ADV register for fiber link.
 */
static inline u32 ethtool_adv_to_fiber_adv_t(u32 ethadv)
{
	u32 result = 0;

	if (ethadv & ADVERTISED_1000baseT_Half)
		result |= ADVERTISE_FIBER_1000HALF;
	if (ethadv & ADVERTISED_1000baseT_Full)
		result |= ADVERTISE_FIBER_1000FULL;

	if ((ethadv & ADVERTISE_PAUSE_ASYM) && (ethadv & ADVERTISE_PAUSE_CAP))
		result |= LPA_PAUSE_ASYM_FIBER;
	else if (ethadv & ADVERTISE_PAUSE_CAP)
		result |= (ADVERTISE_PAUSE_FIBER
			   & (~ADVERTISE_PAUSE_ASYM_FIBER));

	return result;
}

/**
 * marvell_config_aneg_fiber - restart auto-negotiation or write BMCR
 * @phydev: target phy_device struct
 *
 * Description: If auto-negotiation is enabled, we configure the
 *   advertising, and then restart auto-negotiation.  If it is not
 *   enabled, then we write the BMCR. Adapted for fiber link in
 *   some Marvell's devices.
 */
static int marvell_config_aneg_fiber(struct phy_device *phydev)
{
	int changed = 0;
	int err;
	int adv, oldadv;
	u32 advertise;

	if (phydev->autoneg != AUTONEG_ENABLE)
		return genphy_setup_forced(phydev);

	/* Only allow advertising what this PHY supports */
	phydev->advertising &= phydev->supported;
	advertise = phydev->advertising;

	/* Setup fiber advertisement */
	adv = phy_read(phydev, MII_ADVERTISE);
	if (adv < 0)
		return adv;

	oldadv = adv;
	adv &= ~(ADVERTISE_FIBER_1000HALF | ADVERTISE_FIBER_1000FULL
		| LPA_PAUSE_FIBER);
	adv |= ethtool_adv_to_fiber_adv_t(advertise);

	if (adv != oldadv) {
		err = phy_write(phydev, MII_ADVERTISE, adv);
		if (err < 0)
			return err;

		changed = 1;
	}

	if (changed == 0) {
		/* Advertisement hasn't changed, but maybe aneg was never on to
		 * begin with?  Or maybe phy was isolated?
		 */
		int ctl = phy_read(phydev, MII_BMCR);

		if (ctl < 0)
			return ctl;

		if (!(ctl & BMCR_ANENABLE) || (ctl & BMCR_ISOLATE))
			changed = 1; /* do restart aneg */
	}

	/* Only restart aneg if we are advertising something different
	 * than we were before.
	 */
	if (changed > 0)
		changed = genphy_restart_aneg(phydev);

	return changed;
}

static int m88e1510_config_aneg(struct phy_device *phydev)
{
	int err;

	err = phy_write(phydev, MII_MARVELL_PHY_PAGE, MII_M1111_COPPER);
	if (err < 0)
		goto error;

	/* Configure the copper link first */
	err = m88e1318_config_aneg(phydev);
	if (err < 0)
		goto error;

	/* Then the fiber link */
	err = phy_write(phydev, MII_MARVELL_PHY_PAGE, MII_M1111_FIBER);
	if (err < 0)
		goto error;

	err = marvell_config_aneg_fiber(phydev);
	if (err < 0)
		goto error;

	return phy_write(phydev, MII_MARVELL_PHY_PAGE, MII_M1111_COPPER);

error:
	phy_write(phydev, MII_MARVELL_PHY_PAGE, MII_M1111_COPPER);
	return err;
}

static int marvell_config_init(struct phy_device *phydev)
{
	/* Set registers from marvell,reg-init DT property */
	return marvell_of_reg_init(phydev);
}

static int m88e1116r_config_init(struct phy_device *phydev)
{
	int temp;
	int err;

	temp = phy_read(phydev, MII_BMCR);
	temp |= BMCR_RESET;
	err = phy_write(phydev, MII_BMCR, temp);
	if (err < 0)
		return err;

	mdelay(500);

	err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0);
	if (err < 0)
		return err;

	temp = phy_read(phydev, MII_M1011_PHY_SCR);
	temp |= (7 << 12);	/* max number of gigabit attempts */
	temp |= (1 << 11);	/* enable downshift */
	temp |= MII_M1011_PHY_SCR_AUTO_CROSS;
	err = phy_write(phydev, MII_M1011_PHY_SCR, temp);
	if (err < 0)
		return err;

	err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 2);
	if (err < 0)
		return err;
	temp = phy_read(phydev, MII_M1116R_CONTROL_REG_MAC);
	temp |= (1 << 5);
	temp |= (1 << 4);
	err = phy_write(phydev, MII_M1116R_CONTROL_REG_MAC, temp);
	if (err < 0)
		return err;
	err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0);
	if (err < 0)
		return err;

	temp = phy_read(phydev, MII_BMCR);
	temp |= BMCR_RESET;
	err = phy_write(phydev, MII_BMCR, temp);
	if (err < 0)
		return err;

	mdelay(500);

	return marvell_config_init(phydev);
}

static int m88e3016_config_init(struct phy_device *phydev)
{
	int reg;

	/* Enable Scrambler and Auto-Crossover */
	reg = phy_read(phydev, MII_88E3016_PHY_SPEC_CTRL);
	if (reg < 0)
		return reg;

	reg &= ~MII_88E3016_DISABLE_SCRAMBLER;
	reg |= MII_88E3016_AUTO_MDIX_CROSSOVER;

	reg = phy_write(phydev, MII_88E3016_PHY_SPEC_CTRL, reg);
	if (reg < 0)
		return reg;

	return marvell_config_init(phydev);
}

static int m88e1111_config_init(struct phy_device *phydev)
{
	int err;
	int temp;

	if (phy_interface_is_rgmii(phydev)) {

		temp = phy_read(phydev, MII_M1111_PHY_EXT_CR);
		if (temp < 0)
			return temp;

		if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID) {
			temp |= (MII_M1111_RX_DELAY | MII_M1111_TX_DELAY);
		} else if (phydev->interface == PHY_INTERFACE_MODE_RGMII_RXID) {
			temp &= ~MII_M1111_TX_DELAY;
			temp |= MII_M1111_RX_DELAY;
		} else if (phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID) {
			temp &= ~MII_M1111_RX_DELAY;
			temp |= MII_M1111_TX_DELAY;
		}

		err = phy_write(phydev, MII_M1111_PHY_EXT_CR, temp);
		if (err < 0)
			return err;

		temp = phy_read(phydev, MII_M1111_PHY_EXT_SR);
		if (temp < 0)
			return temp;

		temp &= ~(MII_M1111_HWCFG_MODE_MASK);

		if (temp & MII_M1111_HWCFG_FIBER_COPPER_RES)
			temp |= MII_M1111_HWCFG_MODE_FIBER_RGMII;
		else
			temp |= MII_M1111_HWCFG_MODE_COPPER_RGMII;

		err = phy_write(phydev, MII_M1111_PHY_EXT_SR, temp);
		if (err < 0)
			return err;
	}

	if (phydev->interface == PHY_INTERFACE_MODE_SGMII) {
		temp = phy_read(phydev, MII_M1111_PHY_EXT_SR);
		if (temp < 0)
			return temp;

		temp &= ~(MII_M1111_HWCFG_MODE_MASK);
		temp |= MII_M1111_HWCFG_MODE_SGMII_NO_CLK;
		temp |= MII_M1111_HWCFG_FIBER_COPPER_AUTO;

		err = phy_write(phydev, MII_M1111_PHY_EXT_SR, temp);
		if (err < 0)
			return err;

		/* make sure copper is selected */
		err = phy_read(phydev, MII_M1145_PHY_EXT_ADDR_PAGE);
		if (err < 0)
			return err;

		err = phy_write(phydev, MII_M1145_PHY_EXT_ADDR_PAGE,
				err & (~0xff));
		if (err < 0)
			return err;
	}

	if (phydev->interface == PHY_INTERFACE_MODE_RTBI) {
		temp = phy_read(phydev, MII_M1111_PHY_EXT_CR);
		if (temp < 0)
			return temp;
		temp |= (MII_M1111_RX_DELAY | MII_M1111_TX_DELAY);
		err = phy_write(phydev, MII_M1111_PHY_EXT_CR, temp);
		if (err < 0)
			return err;

		temp = phy_read(phydev, MII_M1111_PHY_EXT_SR);
		if (temp < 0)
			return temp;
		temp &= ~(MII_M1111_HWCFG_MODE_MASK | MII_M1111_HWCFG_FIBER_COPPER_RES);
		temp |= 0x7 | MII_M1111_HWCFG_FIBER_COPPER_AUTO;
		err = phy_write(phydev, MII_M1111_PHY_EXT_SR, temp);
		if (err < 0)
			return err;

		/* soft reset */
		err = phy_write(phydev, MII_BMCR, BMCR_RESET);
		if (err < 0)
			return err;
		do
			temp = phy_read(phydev, MII_BMCR);
		while (temp & BMCR_RESET);

		temp = phy_read(phydev, MII_M1111_PHY_EXT_SR);
		if (temp < 0)
			return temp;
		temp &= ~(MII_M1111_HWCFG_MODE_MASK | MII_M1111_HWCFG_FIBER_COPPER_RES);
		temp |= MII_M1111_HWCFG_MODE_COPPER_RTBI | MII_M1111_HWCFG_FIBER_COPPER_AUTO;
		err = phy_write(phydev, MII_M1111_PHY_EXT_SR, temp);
		if (err < 0)
			return err;
	}

	err = marvell_of_reg_init(phydev);
	if (err < 0)
		return err;

	return phy_write(phydev, MII_BMCR, BMCR_RESET);
}

static int m88e1121_config_init(struct phy_device *phydev)
{
	int err, oldpage;

	oldpage = phy_read(phydev, MII_MARVELL_PHY_PAGE);

	err = phy_write(phydev, MII_MARVELL_PHY_PAGE, MII_88E1121_PHY_LED_PAGE);
	if (err < 0)
		return err;

	/* Default PHY LED config: LED[0] .. Link, LED[1] .. Activity */
	err = phy_write(phydev, MII_88E1121_PHY_LED_CTRL,
			MII_88E1121_PHY_LED_DEF);
	if (err < 0)
		return err;

	phy_write(phydev, MII_MARVELL_PHY_PAGE, oldpage);

	/* Set marvell,reg-init configuration from device tree */
	return marvell_config_init(phydev);
}

static int m88e1510_config_init(struct phy_device *phydev)
{
	int err;
	int temp;

	/* SGMII-to-Copper mode initialization */
	if (phydev->interface == PHY_INTERFACE_MODE_SGMII) {
		/* Select page 18 */
		err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 18);
		if (err < 0)
			return err;

		/* In reg 20, write MODE[2:0] = 0x1 (SGMII to Copper) */
		temp = phy_read(phydev, MII_88E1510_GEN_CTRL_REG_1);
		temp &= ~MII_88E1510_GEN_CTRL_REG_1_MODE_MASK;
		temp |= MII_88E1510_GEN_CTRL_REG_1_MODE_SGMII;
		err = phy_write(phydev, MII_88E1510_GEN_CTRL_REG_1, temp);
		if (err < 0)
			return err;

		/* PHY reset is necessary after changing MODE[2:0] */
		temp |= MII_88E1510_GEN_CTRL_REG_1_RESET;
		err = phy_write(phydev, MII_88E1510_GEN_CTRL_REG_1, temp);
		if (err < 0)
			return err;

		/* Reset page selection */
		err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0);
		if (err < 0)
			return err;
	}

	return m88e1121_config_init(phydev);
}

static int m88e1118_config_aneg(struct phy_device *phydev)
{
	int err;

	err = phy_write(phydev, MII_BMCR, BMCR_RESET);
	if (err < 0)
		return err;

	err = phy_write(phydev, MII_M1011_PHY_SCR,
			MII_M1011_PHY_SCR_AUTO_CROSS);
	if (err < 0)
		return err;

	err = genphy_config_aneg(phydev);
	return 0;
}

static int m88e1118_config_init(struct phy_device *phydev)
{
	int err;

	/* Change address */
	err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0x0002);
	if (err < 0)
		return err;

	/* Enable 1000 Mbit */
	err = phy_write(phydev, 0x15, 0x1070);
	if (err < 0)
		return err;

	/* Change address */
	err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0x0003);
	if (err < 0)
		return err;

	/* Adjust LED Control */
	if (phydev->dev_flags & MARVELL_PHY_M1118_DNS323_LEDS)
		err = phy_write(phydev, 0x10, 0x1100);
	else
		err = phy_write(phydev, 0x10, 0x021e);
	if (err < 0)
		return err;

	err = marvell_of_reg_init(phydev);
	if (err < 0)
		return err;

	/* Reset address */
	err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0x0);
	if (err < 0)
		return err;

	return phy_write(phydev, MII_BMCR, BMCR_RESET);
}

static int m88e1149_config_init(struct phy_device *phydev)
{
	int err;

	/* Change address */
	err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0x0002);
	if (err < 0)
		return err;

	/* Enable 1000 Mbit */
	err = phy_write(phydev, 0x15, 0x1048);
	if (err < 0)
		return err;

	err = marvell_of_reg_init(phydev);
	if (err < 0)
		return err;

	/* Reset address */
	err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0x0);
	if (err < 0)
		return err;

	return phy_write(phydev, MII_BMCR, BMCR_RESET);
}

static int m88e1145_config_init(struct phy_device *phydev)
{
	int err;
	int temp;

	/* Take care of errata E0 & E1 */
	err = phy_write(phydev, 0x1d, 0x001b);
	if (err < 0)
		return err;

	err = phy_write(phydev, 0x1e, 0x418f);
	if (err < 0)
		return err;

	err = phy_write(phydev, 0x1d, 0x0016);
	if (err < 0)
		return err;

	err = phy_write(phydev, 0x1e, 0xa2da);
	if (err < 0)
		return err;

	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID) {
		int temp = phy_read(phydev, MII_M1145_PHY_EXT_CR);
		if (temp < 0)
			return temp;

		temp |= (MII_M1145_RGMII_RX_DELAY | MII_M1145_RGMII_TX_DELAY);

		err = phy_write(phydev, MII_M1145_PHY_EXT_CR, temp);
		if (err < 0)
			return err;

		if (phydev->dev_flags & MARVELL_PHY_M1145_FLAGS_RESISTANCE) {
			err = phy_write(phydev, 0x1d, 0x0012);
			if (err < 0)
				return err;

			temp = phy_read(phydev, 0x1e);
			if (temp < 0)
				return temp;

			temp &= 0xf03f;
			temp |= 2 << 9;	/* 36 ohm */
			temp |= 2 << 6;	/* 39 ohm */

			err = phy_write(phydev, 0x1e, temp);
			if (err < 0)
				return err;

			err = phy_write(phydev, 0x1d, 0x3);
			if (err < 0)
				return err;

			err = phy_write(phydev, 0x1e, 0x8000);
			if (err < 0)
				return err;
		}
	}

	if (phydev->interface == PHY_INTERFACE_MODE_SGMII) {
		temp = phy_read(phydev, MII_M1145_PHY_EXT_SR);
		if (temp < 0)
			return temp;

		temp &= ~MII_M1145_HWCFG_MODE_MASK;
		temp |= MII_M1145_HWCFG_MODE_SGMII_NO_CLK;
		temp |= MII_M1145_HWCFG_FIBER_COPPER_AUTO;

		err = phy_write(phydev, MII_M1145_PHY_EXT_SR, temp);
		if (err < 0)
			return err;
	}

	err = marvell_of_reg_init(phydev);
	if (err < 0)
		return err;

	return 0;
}

/**
 * fiber_lpa_to_ethtool_lpa_t
 * @lpa: value of the MII_LPA register for fiber link
 *
 * A small helper function that translates MII_LPA
 * bits to ethtool LP advertisement settings.
 */
static u32 fiber_lpa_to_ethtool_lpa_t(u32 lpa)
{
	u32 result = 0;

	if (lpa & LPA_FIBER_1000HALF)
		result |= ADVERTISED_1000baseT_Half;
	if (lpa & LPA_FIBER_1000FULL)
		result |= ADVERTISED_1000baseT_Full;

	return result;
}

/**
 * marvell_update_link - update link status in real time in @phydev
 * @phydev: target phy_device struct
 *
 * Description: Update the value in phydev->link to reflect the
 *   current link value.
 */
static int marvell_update_link(struct phy_device *phydev, int fiber)
{
	int status;

	/* Use the generic register for copper link, or specific
	 * register for fiber case */
	if (fiber) {
		status = phy_read(phydev, MII_M1011_PHY_STATUS);
		if (status < 0)
			return status;

		if ((status & REGISTER_LINK_STATUS) == 0)
			phydev->link = 0;
		else
			phydev->link = 1;
	} else {
		return genphy_update_link(phydev);
	}

	return 0;
}

/* marvell_read_status_page
 *
 * Description:
 *   Check the link, then figure out the current state
 *   by comparing what we advertise with what the link partner
 *   advertises.  Start by checking the gigabit possibilities,
 *   then move on to 10/100.
 */
static int marvell_read_status_page(struct phy_device *phydev, int page)
{
	int adv;
	int err;
	int lpa;
	int lpagb;
	int status = 0;
	int fiber;

	/* Detect and update the link, but return if there
	 * was an error */
	if (page == MII_M1111_FIBER)
		fiber = 1;
	else
		fiber = 0;

	err = marvell_update_link(phydev, fiber);
	if (err)
		return err;

	if (AUTONEG_ENABLE == phydev->autoneg) {
		status = phy_read(phydev, MII_M1011_PHY_STATUS);
		if (status < 0)
			return status;

		lpa = phy_read(phydev, MII_LPA);
		if (lpa < 0)
			return lpa;

		lpagb = phy_read(phydev, MII_STAT1000);
		if (lpagb < 0)
			return lpagb;

		adv = phy_read(phydev, MII_ADVERTISE);
		if (adv < 0)
			return adv;

		lpa &= adv;

		if (status & MII_M1011_PHY_STATUS_FULLDUPLEX)
			phydev->duplex = DUPLEX_FULL;
		else
			phydev->duplex = DUPLEX_HALF;

		status = status & MII_M1011_PHY_STATUS_SPD_MASK;
		phydev->pause = phydev->asym_pause = 0;

		switch (status) {
		case MII_M1011_PHY_STATUS_1000:
			phydev->speed = SPEED_1000;
			break;

		case MII_M1011_PHY_STATUS_100:
			phydev->speed = SPEED_100;
			break;

		default:
			phydev->speed = SPEED_10;
			break;
		}

		if (!fiber) {
			phydev->lp_advertising = mii_stat1000_to_ethtool_lpa_t(lpagb) |
					 mii_lpa_to_ethtool_lpa_t(lpa);

			if (phydev->duplex == DUPLEX_FULL) {
				phydev->pause = lpa & LPA_PAUSE_CAP ? 1 : 0;
				phydev->asym_pause = lpa & LPA_PAUSE_ASYM ? 1 : 0;
			}
		} else {
			/* The fiber link is only 1000M capable */
			phydev->lp_advertising = fiber_lpa_to_ethtool_lpa_t(lpa);

			if (phydev->duplex == DUPLEX_FULL) {
				if (!(lpa & LPA_PAUSE_FIBER)) {
					phydev->pause = 0;
					phydev->asym_pause = 0;
				} else if ((lpa & LPA_PAUSE_ASYM_FIBER)) {
					phydev->pause = 1;
					phydev->asym_pause = 1;
				} else {
					phydev->pause = 1;
					phydev->asym_pause = 0;
				}
			}
		}
	} else {
		int bmcr = phy_read(phydev, MII_BMCR);

		if (bmcr < 0)
			return bmcr;

		if (bmcr & BMCR_FULLDPLX)
			phydev->duplex = DUPLEX_FULL;
		else
			phydev->duplex = DUPLEX_HALF;

		if (bmcr & BMCR_SPEED1000)
			phydev->speed = SPEED_1000;
		else if (bmcr & BMCR_SPEED100)
			phydev->speed = SPEED_100;
		else
			phydev->speed = SPEED_10;

		phydev->pause = phydev->asym_pause = 0;
		phydev->lp_advertising = 0;
	}

	return 0;
}

/* marvell_read_status
 *
 * Some Marvell's phys have two modes: fiber and copper.
 * Both need status checked.
 * Description:
 *   First, check the fiber link and status.
 *   If the fiber link is down, check the copper link and status which
 *   will be the default value if both link are down.
 */
static int marvell_read_status(struct phy_device *phydev)
{
	int err;

	/* Check the fiber mode first */
	if (phydev->supported & SUPPORTED_FIBRE) {
		err = phy_write(phydev, MII_MARVELL_PHY_PAGE, MII_M1111_FIBER);
		if (err < 0)
			goto error;

		err = marvell_read_status_page(phydev, MII_M1111_FIBER);
		if (err < 0)
			goto error;

		/* If the fiber link is up, it is the selected and used link.
		 * In this case, we need to stay in the fiber page.
		 * Please to be careful about that, avoid to restore Copper page
		 * in other functions which could break the behaviour
		 * for some fiber phy like 88E1512.
		 * */
		if (phydev->link)
			return 0;

		/* If fiber link is down, check and save copper mode state */
		err = phy_write(phydev, MII_MARVELL_PHY_PAGE, MII_M1111_COPPER);
		if (err < 0)
			goto error;
	}

	return marvell_read_status_page(phydev, MII_M1111_COPPER);

error:
	phy_write(phydev, MII_MARVELL_PHY_PAGE, MII_M1111_COPPER);
	return err;
}

/* marvell_suspend
 *
 * Some Marvell's phys have two modes: fiber and copper.
 * Both need to be suspended
 */
static int marvell_suspend(struct phy_device *phydev)
{
	int err;

	/* Suspend the fiber mode first */
	if (!(phydev->supported & SUPPORTED_FIBRE)) {
		err = phy_write(phydev, MII_MARVELL_PHY_PAGE, MII_M1111_FIBER);
		if (err < 0)
			goto error;

		/* With the page set, use the generic suspend */
		err = genphy_suspend(phydev);
		if (err < 0)
			goto error;

		/* Then, the copper link */
		err = phy_write(phydev, MII_MARVELL_PHY_PAGE, MII_M1111_COPPER);
		if (err < 0)
			goto error;
	}

	/* With the page set, use the generic suspend */
	return genphy_suspend(phydev);

error:
	phy_write(phydev, MII_MARVELL_PHY_PAGE, MII_M1111_COPPER);
	return err;
}

/* marvell_resume
 *
 * Some Marvell's phys have two modes: fiber and copper.
 * Both need to be resumed
 */
static int marvell_resume(struct phy_device *phydev)
{
	int err;

	/* Resume the fiber mode first */
	if (!(phydev->supported & SUPPORTED_FIBRE)) {
		err = phy_write(phydev, MII_MARVELL_PHY_PAGE, MII_M1111_FIBER);
		if (err < 0)
			goto error;

		/* With the page set, use the generic resume */
		err = genphy_resume(phydev);
		if (err < 0)
			goto error;

		/* Then, the copper link */
		err = phy_write(phydev, MII_MARVELL_PHY_PAGE, MII_M1111_COPPER);
		if (err < 0)
			goto error;
	}

	/* With the page set, use the generic resume */
	return genphy_resume(phydev);

error:
	phy_write(phydev, MII_MARVELL_PHY_PAGE, MII_M1111_COPPER);
	return err;
}

static int marvell_aneg_done(struct phy_device *phydev)
{
	int retval = phy_read(phydev, MII_M1011_PHY_STATUS);
	return (retval < 0) ? retval : (retval & MII_M1011_PHY_STATUS_RESOLVED);
}

static int m88e1121_did_interrupt(struct phy_device *phydev)
{
	int imask;

	imask = phy_read(phydev, MII_M1011_IEVENT);

	if (imask & MII_M1011_IMASK_INIT)
		return 1;

	return 0;
}

static void m88e1318_get_wol(struct phy_device *phydev, struct ethtool_wolinfo *wol)
{
	wol->supported = WAKE_MAGIC;
	wol->wolopts = 0;

	if (phy_write(phydev, MII_MARVELL_PHY_PAGE,
		      MII_88E1318S_PHY_WOL_PAGE) < 0)
		return;

	if (phy_read(phydev, MII_88E1318S_PHY_WOL_CTRL) &
	    MII_88E1318S_PHY_WOL_CTRL_MAGIC_PACKET_MATCH_ENABLE)
		wol->wolopts |= WAKE_MAGIC;

	if (phy_write(phydev, MII_MARVELL_PHY_PAGE, 0x00) < 0)
		return;
}

static int m88e1318_set_wol(struct phy_device *phydev, struct ethtool_wolinfo *wol)
{
	int err, oldpage, temp;

	oldpage = phy_read(phydev, MII_MARVELL_PHY_PAGE);

	if (wol->wolopts & WAKE_MAGIC) {
		/* Explicitly switch to page 0x00, just to be sure */
		err = phy_write(phydev, MII_MARVELL_PHY_PAGE, 0x00);
		if (err < 0)
			return err;

		/* Enable the WOL interrupt */
		temp = phy_read(phydev, MII_88E1318S_PHY_CSIER);
		temp |= MII_88E1318S_PHY_CSIER_WOL_EIE;
		err = phy_write(phydev, MII_88E1318S_PHY_CSIER, temp);
		if (err < 0)
			return err;

		err = phy_write(phydev, MII_MARVELL_PHY_PAGE,
				MII_88E1318S_PHY_LED_PAGE);
		if (err < 0)
			return err;

		/* Setup LED[2] as interrupt pin (active low) */
		temp = phy_read(phydev, MII_88E1318S_PHY_LED_TCR);
		temp &= ~MII_88E1318S_PHY_LED_TCR_FORCE_INT;
		temp |= MII_88E1318S_PHY_LED_TCR_INTn_ENABLE;
		temp |= MII_88E1318S_PHY_LED_TCR_INT_ACTIVE_LOW;
		err = phy_write(phydev, MII_88E1318S_PHY_LED_TCR, temp);
		if (err < 0)
			return err;

		err = phy_write(phydev, MII_MARVELL_PHY_PAGE,
				MII_88E1318S_PHY_WOL_PAGE);
		if (err < 0)
			return err;

		/* Store the device address for the magic packet */
		err = phy_write(phydev, MII_88E1318S_PHY_MAGIC_PACKET_WORD2,
				((phydev->attached_dev->dev_addr[5] << 8) |
				 phydev->attached_dev->dev_addr[4]));
		if (err < 0)
			return err;
		err = phy_write(phydev, MII_88E1318S_PHY_MAGIC_PACKET_WORD1,
				((phydev->attached_dev->dev_addr[3] << 8) |
				 phydev->attached_dev->dev_addr[2]));
		if (err < 0)
			return err;
		err = phy_write(phydev, MII_88E1318S_PHY_MAGIC_PACKET_WORD0,
				((phydev->attached_dev->dev_addr[1] << 8) |
				 phydev->attached_dev->dev_addr[0]));
		if (err < 0)
			return err;

		/* Clear WOL status and enable magic packet matching */
		temp = phy_read(phydev, MII_88E1318S_PHY_WOL_CTRL);
		temp |= MII_88E1318S_PHY_WOL_CTRL_CLEAR_WOL_STATUS;
		temp |= MII_88E1318S_PHY_WOL_CTRL_MAGIC_PACKET_MATCH_ENABLE;
		err = phy_write(phydev, MII_88E1318S_PHY_WOL_CTRL, temp);
		if (err < 0)
			return err;
	} else {
		err = phy_write(phydev, MII_MARVELL_PHY_PAGE,
				MII_88E1318S_PHY_WOL_PAGE);
		if (err < 0)
			return err;

		/* Clear WOL status and disable magic packet matching */
		temp = phy_read(phydev, MII_88E1318S_PHY_WOL_CTRL);
		temp |= MII_88E1318S_PHY_WOL_CTRL_CLEAR_WOL_STATUS;
		temp &= ~MII_88E1318S_PHY_WOL_CTRL_MAGIC_PACKET_MATCH_ENABLE;
		err = phy_write(phydev, MII_88E1318S_PHY_WOL_CTRL, temp);
		if (err < 0)
			return err;
	}

	err = phy_write(phydev, MII_MARVELL_PHY_PAGE, oldpage);
	if (err < 0)
		return err;

	return 0;
}

static int marvell_get_sset_count(struct phy_device *phydev)
{
	if (phydev->supported & SUPPORTED_FIBRE)
		return ARRAY_SIZE(marvell_hw_stats);
	else
		return ARRAY_SIZE(marvell_hw_stats) - NB_FIBER_STATS;
}

static void marvell_get_strings(struct phy_device *phydev, u8 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(marvell_hw_stats); i++) {
		memcpy(data + i * ETH_GSTRING_LEN,
		       marvell_hw_stats[i].string, ETH_GSTRING_LEN);
	}
}

#ifndef UINT64_MAX
#define UINT64_MAX              (u64)(~((u64)0))
#endif
static u64 marvell_get_stat(struct phy_device *phydev, int i)
{
	struct marvell_hw_stat stat = marvell_hw_stats[i];
	struct marvell_priv *priv = phydev->priv;
	int err, oldpage, val;
	u64 ret;

	oldpage = phy_read(phydev, MII_MARVELL_PHY_PAGE);
	err = phy_write(phydev, MII_MARVELL_PHY_PAGE,
			stat.page);
	if (err < 0)
		return UINT64_MAX;

	val = phy_read(phydev, stat.reg);
	if (val < 0) {
		ret = UINT64_MAX;
	} else {
		val = val & ((1 << stat.bits) - 1);
		priv->stats[i] += val;
		ret = priv->stats[i];
	}

	phy_write(phydev, MII_MARVELL_PHY_PAGE, oldpage);

	return ret;
}

static void marvell_get_stats(struct phy_device *phydev,
			      struct ethtool_stats *stats, u64 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(marvell_hw_stats); i++)
		data[i] = marvell_get_stat(phydev, i);
}

static int marvell_probe(struct phy_device *phydev)
{
	struct marvell_priv *priv;

	priv = devm_kzalloc(&phydev->mdio.dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	return 0;
}

static struct phy_driver marvell_drivers[] = {
	{
		.phy_id = MARVELL_PHY_ID_88E1101,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1101",
		.features = PHY_GBIT_FEATURES,
		.probe = marvell_probe,
		.flags = PHY_HAS_INTERRUPT,
		.config_init = &marvell_config_init,
		.config_aneg = &m88e1101_config_aneg,
		.read_status = &genphy_read_status,
		.ack_interrupt = &marvell_ack_interrupt,
		.config_intr = &marvell_config_intr,
		.resume = &genphy_resume,
		.suspend = &genphy_suspend,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1112,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1112",
		.features = PHY_GBIT_FEATURES,
		.flags = PHY_HAS_INTERRUPT,
		.probe = marvell_probe,
		.config_init = &m88e1111_config_init,
		.config_aneg = &marvell_config_aneg,
		.read_status = &genphy_read_status,
		.ack_interrupt = &marvell_ack_interrupt,
		.config_intr = &marvell_config_intr,
		.resume = &genphy_resume,
		.suspend = &genphy_suspend,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1111,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1111",
		.features = PHY_GBIT_FEATURES,
		.flags = PHY_HAS_INTERRUPT,
		.probe = marvell_probe,
		.config_init = &m88e1111_config_init,
		.config_aneg = &m88e1111_config_aneg,
		.read_status = &marvell_read_status,
		.ack_interrupt = &marvell_ack_interrupt,
		.config_intr = &marvell_config_intr,
		.resume = &genphy_resume,
		.suspend = &genphy_suspend,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1118,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1118",
		.features = PHY_GBIT_FEATURES,
		.flags = PHY_HAS_INTERRUPT,
		.probe = marvell_probe,
		.config_init = &m88e1118_config_init,
		.config_aneg = &m88e1118_config_aneg,
		.read_status = &genphy_read_status,
		.ack_interrupt = &marvell_ack_interrupt,
		.config_intr = &marvell_config_intr,
		.resume = &genphy_resume,
		.suspend = &genphy_suspend,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1121R,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1121R",
		.features = PHY_GBIT_FEATURES,
		.flags = PHY_HAS_INTERRUPT,
		.probe = marvell_probe,
		.config_init = &m88e1121_config_init,
		.config_aneg = &m88e1121_config_aneg,
		.read_status = &marvell_read_status,
		.ack_interrupt = &marvell_ack_interrupt,
		.config_intr = &marvell_config_intr,
		.did_interrupt = &m88e1121_did_interrupt,
		.resume = &genphy_resume,
		.suspend = &genphy_suspend,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1318S,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1318S",
		.features = PHY_GBIT_FEATURES,
		.flags = PHY_HAS_INTERRUPT,
		.probe = marvell_probe,
		.config_init = &m88e1121_config_init,
		.config_aneg = &m88e1318_config_aneg,
		.read_status = &marvell_read_status,
		.ack_interrupt = &marvell_ack_interrupt,
		.config_intr = &marvell_config_intr,
		.did_interrupt = &m88e1121_did_interrupt,
		.get_wol = &m88e1318_get_wol,
		.set_wol = &m88e1318_set_wol,
		.resume = &genphy_resume,
		.suspend = &genphy_suspend,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1145,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1145",
		.features = PHY_GBIT_FEATURES,
		.flags = PHY_HAS_INTERRUPT,
		.probe = marvell_probe,
		.config_init = &m88e1145_config_init,
		.config_aneg = &marvell_config_aneg,
		.read_status = &genphy_read_status,
		.ack_interrupt = &marvell_ack_interrupt,
		.config_intr = &marvell_config_intr,
		.resume = &genphy_resume,
		.suspend = &genphy_suspend,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1149R,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1149R",
		.features = PHY_GBIT_FEATURES,
		.flags = PHY_HAS_INTERRUPT,
		.probe = marvell_probe,
		.config_init = &m88e1149_config_init,
		.config_aneg = &m88e1118_config_aneg,
		.read_status = &genphy_read_status,
		.ack_interrupt = &marvell_ack_interrupt,
		.config_intr = &marvell_config_intr,
		.resume = &genphy_resume,
		.suspend = &genphy_suspend,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1240,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1240",
		.features = PHY_GBIT_FEATURES,
		.flags = PHY_HAS_INTERRUPT,
		.probe = marvell_probe,
		.config_init = &m88e1111_config_init,
		.config_aneg = &marvell_config_aneg,
		.read_status = &genphy_read_status,
		.ack_interrupt = &marvell_ack_interrupt,
		.config_intr = &marvell_config_intr,
		.resume = &genphy_resume,
		.suspend = &genphy_suspend,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1116R,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1116R",
		.features = PHY_GBIT_FEATURES,
		.flags = PHY_HAS_INTERRUPT,
		.probe = marvell_probe,
		.config_init = &m88e1116r_config_init,
		.config_aneg = &genphy_config_aneg,
		.read_status = &genphy_read_status,
		.ack_interrupt = &marvell_ack_interrupt,
		.config_intr = &marvell_config_intr,
		.resume = &genphy_resume,
		.suspend = &genphy_suspend,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1510,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1510",
		.features = PHY_GBIT_FEATURES | SUPPORTED_FIBRE,
		.flags = PHY_HAS_INTERRUPT,
		.probe = marvell_probe,
		.config_init = &m88e1510_config_init,
		.config_aneg = &m88e1510_config_aneg,
		.read_status = &marvell_read_status,
		.ack_interrupt = &marvell_ack_interrupt,
		.config_intr = &marvell_config_intr,
		.did_interrupt = &m88e1121_did_interrupt,
		.resume = &marvell_resume,
		.suspend = &marvell_suspend,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1540,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1540",
		.features = PHY_GBIT_FEATURES,
		.flags = PHY_HAS_INTERRUPT,
		.probe = marvell_probe,
		.config_init = &marvell_config_init,
		.config_aneg = &m88e1510_config_aneg,
		.read_status = &marvell_read_status,
		.ack_interrupt = &marvell_ack_interrupt,
		.config_intr = &marvell_config_intr,
		.did_interrupt = &m88e1121_did_interrupt,
		.resume = &genphy_resume,
		.suspend = &genphy_suspend,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E3016,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E3016",
		.features = PHY_BASIC_FEATURES,
		.flags = PHY_HAS_INTERRUPT,
		.probe = marvell_probe,
		.config_aneg = &genphy_config_aneg,
		.config_init = &m88e3016_config_init,
		.aneg_done = &marvell_aneg_done,
		.read_status = &marvell_read_status,
		.ack_interrupt = &marvell_ack_interrupt,
		.config_intr = &marvell_config_intr,
		.did_interrupt = &m88e1121_did_interrupt,
		.resume = &genphy_resume,
		.suspend = &genphy_suspend,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
};

module_phy_driver(marvell_drivers);

static struct mdio_device_id __maybe_unused marvell_tbl[] = {
	{ MARVELL_PHY_ID_88E1101, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E1112, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E1111, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E1118, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E1121R, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E1145, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E1149R, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E1240, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E1318S, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E1116R, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E1510, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E1540, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E3016, MARVELL_PHY_ID_MASK },
	{ }
};

MODULE_DEVICE_TABLE(mdio, marvell_tbl);
