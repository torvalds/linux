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
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/hwmon.h>
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
#define MII_MARVELL_COPPER_PAGE		0x00
#define MII_MARVELL_FIBER_PAGE		0x01
#define MII_MARVELL_MSCR_PAGE		0x02
#define MII_MARVELL_LED_PAGE		0x03
#define MII_MARVELL_MISC_TEST_PAGE	0x06
#define MII_MARVELL_WOL_PAGE		0x11

#define MII_M1011_IEVENT		0x13
#define MII_M1011_IEVENT_CLEAR		0x0000

#define MII_M1011_IMASK			0x12
#define MII_M1011_IMASK_INIT		0x6400
#define MII_M1011_IMASK_CLEAR		0x0000

#define MII_M1011_PHY_SCR			0x10
#define MII_M1011_PHY_SCR_DOWNSHIFT_EN		BIT(11)
#define MII_M1011_PHY_SCR_DOWNSHIFT_SHIFT	12
#define MII_M1011_PHY_SRC_DOWNSHIFT_MASK	0x7800
#define MII_M1011_PHY_SCR_MDI			(0x0 << 5)
#define MII_M1011_PHY_SCR_MDI_X			(0x1 << 5)
#define MII_M1011_PHY_SCR_AUTO_CROSS		(0x3 << 5)

#define MII_M1111_PHY_LED_CONTROL	0x18
#define MII_M1111_PHY_LED_DIRECT	0x4100
#define MII_M1111_PHY_LED_COMBINE	0x411c
#define MII_M1111_PHY_EXT_CR		0x14
#define MII_M1111_RGMII_RX_DELAY	BIT(7)
#define MII_M1111_RGMII_TX_DELAY	BIT(1)
#define MII_M1111_PHY_EXT_SR		0x1b

#define MII_M1111_HWCFG_MODE_MASK		0xf
#define MII_M1111_HWCFG_MODE_FIBER_RGMII	0x3
#define MII_M1111_HWCFG_MODE_SGMII_NO_CLK	0x4
#define MII_M1111_HWCFG_MODE_RTBI		0x7
#define MII_M1111_HWCFG_MODE_COPPER_RTBI	0x9
#define MII_M1111_HWCFG_MODE_COPPER_RGMII	0xb
#define MII_M1111_HWCFG_FIBER_COPPER_RES	BIT(13)
#define MII_M1111_HWCFG_FIBER_COPPER_AUTO	BIT(15)

#define MII_88E1121_PHY_MSCR_REG	21
#define MII_88E1121_PHY_MSCR_RX_DELAY	BIT(5)
#define MII_88E1121_PHY_MSCR_TX_DELAY	BIT(4)
#define MII_88E1121_PHY_MSCR_DELAY_MASK	(BIT(5) | BIT(4))

#define MII_88E1121_MISC_TEST				0x1a
#define MII_88E1510_MISC_TEST_TEMP_THRESHOLD_MASK	0x1f00
#define MII_88E1510_MISC_TEST_TEMP_THRESHOLD_SHIFT	8
#define MII_88E1510_MISC_TEST_TEMP_IRQ_EN		BIT(7)
#define MII_88E1510_MISC_TEST_TEMP_IRQ			BIT(6)
#define MII_88E1121_MISC_TEST_TEMP_SENSOR_EN		BIT(5)
#define MII_88E1121_MISC_TEST_TEMP_MASK			0x1f

#define MII_88E1510_TEMP_SENSOR		0x1b
#define MII_88E1510_TEMP_SENSOR_MASK	0xff

#define MII_88E6390_MISC_TEST		0x1b
#define MII_88E6390_MISC_TEST_SAMPLE_1S		0
#define MII_88E6390_MISC_TEST_SAMPLE_10MS	BIT(14)
#define MII_88E6390_MISC_TEST_SAMPLE_DISABLE	BIT(15)
#define MII_88E6390_MISC_TEST_SAMPLE_ENABLE	0
#define MII_88E6390_MISC_TEST_SAMPLE_MASK	(0x3 << 14)

#define MII_88E6390_TEMP_SENSOR		0x1c
#define MII_88E6390_TEMP_SENSOR_MASK	0xff
#define MII_88E6390_TEMP_SENSOR_SAMPLES 10

#define MII_88E1318S_PHY_MSCR1_REG	16
#define MII_88E1318S_PHY_MSCR1_PAD_ODD	BIT(6)

/* Copper Specific Interrupt Enable Register */
#define MII_88E1318S_PHY_CSIER				0x12
/* WOL Event Interrupt Enable */
#define MII_88E1318S_PHY_CSIER_WOL_EIE			BIT(7)

/* LED Timer Control Register */
#define MII_88E1318S_PHY_LED_TCR			0x12
#define MII_88E1318S_PHY_LED_TCR_FORCE_INT		BIT(15)
#define MII_88E1318S_PHY_LED_TCR_INTn_ENABLE		BIT(7)
#define MII_88E1318S_PHY_LED_TCR_INT_ACTIVE_LOW		BIT(11)

/* Magic Packet MAC address registers */
#define MII_88E1318S_PHY_MAGIC_PACKET_WORD2		0x17
#define MII_88E1318S_PHY_MAGIC_PACKET_WORD1		0x18
#define MII_88E1318S_PHY_MAGIC_PACKET_WORD0		0x19

#define MII_88E1318S_PHY_WOL_CTRL				0x10
#define MII_88E1318S_PHY_WOL_CTRL_CLEAR_WOL_STATUS		BIT(12)
#define MII_88E1318S_PHY_WOL_CTRL_MAGIC_PACKET_MATCH_ENABLE	BIT(14)

#define MII_PHY_LED_CTRL	        16
#define MII_88E1121_PHY_LED_DEF		0x0030
#define MII_88E1510_PHY_LED_DEF		0x1177

#define MII_M1011_PHY_STATUS		0x11
#define MII_M1011_PHY_STATUS_1000	0x8000
#define MII_M1011_PHY_STATUS_100	0x4000
#define MII_M1011_PHY_STATUS_SPD_MASK	0xc000
#define MII_M1011_PHY_STATUS_FULLDUPLEX	0x2000
#define MII_M1011_PHY_STATUS_RESOLVED	0x0800
#define MII_M1011_PHY_STATUS_LINK	0x0400

#define MII_88E3016_PHY_SPEC_CTRL	0x10
#define MII_88E3016_DISABLE_SCRAMBLER	0x0200
#define MII_88E3016_AUTO_MDIX_CROSSOVER	0x0030

#define MII_88E1510_GEN_CTRL_REG_1		0x14
#define MII_88E1510_GEN_CTRL_REG_1_MODE_MASK	0x7
#define MII_88E1510_GEN_CTRL_REG_1_MODE_SGMII	0x1	/* SGMII to copper */
#define MII_88E1510_GEN_CTRL_REG_1_RESET	0x8000	/* Soft reset */

#define LPA_FIBER_1000HALF	0x40
#define LPA_FIBER_1000FULL	0x20

#define LPA_PAUSE_FIBER		0x180
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
	char *hwmon_name;
	struct device *hwmon_dev;
};

static int marvell_read_page(struct phy_device *phydev)
{
	return __phy_read(phydev, MII_MARVELL_PHY_PAGE);
}

static int marvell_write_page(struct phy_device *phydev, int page)
{
	return __phy_write(phydev, MII_MARVELL_PHY_PAGE, page);
}

static int marvell_set_page(struct phy_device *phydev, int page)
{
	return phy_write(phydev, MII_MARVELL_PHY_PAGE, page);
}

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
		err = phy_write(phydev, MII_M1011_IMASK,
				MII_M1011_IMASK_INIT);
	else
		err = phy_write(phydev, MII_M1011_IMASK,
				MII_M1011_IMASK_CLEAR);

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

	return val != reg;
}

static int marvell_set_downshift(struct phy_device *phydev, bool enable,
				 u8 retries)
{
	int reg;

	reg = phy_read(phydev, MII_M1011_PHY_SCR);
	if (reg < 0)
		return reg;

	reg &= MII_M1011_PHY_SRC_DOWNSHIFT_MASK;
	reg |= ((retries - 1) << MII_M1011_PHY_SCR_DOWNSHIFT_SHIFT);
	if (enable)
		reg |= MII_M1011_PHY_SCR_DOWNSHIFT_EN;

	return phy_write(phydev, MII_M1011_PHY_SCR, reg);
}

static int marvell_config_aneg(struct phy_device *phydev)
{
	int changed = 0;
	int err;

	err = marvell_set_polarity(phydev, phydev->mdix_ctrl);
	if (err < 0)
		return err;

	changed = err;

	err = phy_write(phydev, MII_M1111_PHY_LED_CONTROL,
			MII_M1111_PHY_LED_DIRECT);
	if (err < 0)
		return err;

	err = genphy_config_aneg(phydev);
	if (err < 0)
		return err;

	if (phydev->autoneg != AUTONEG_ENABLE || changed) {
		/* A write to speed/duplex bits (that is performed by
		 * genphy_config_aneg() call above) must be followed by
		 * a software reset. Otherwise, the write has no effect.
		 */
		err = genphy_soft_reset(phydev);
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
	err = genphy_soft_reset(phydev);
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

#ifdef CONFIG_OF_MDIO
/* Set and/or override some configuration registers based on the
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
	int len, i, saved_page, current_page, ret = 0;

	if (!phydev->mdio.dev.of_node)
		return 0;

	paddr = of_get_property(phydev->mdio.dev.of_node,
				"marvell,reg-init", &len);
	if (!paddr || len < (4 * sizeof(*paddr)))
		return 0;

	saved_page = phy_save_page(phydev);
	if (saved_page < 0)
		goto err;
	current_page = saved_page;

	len /= sizeof(*paddr);
	for (i = 0; i < len - 3; i += 4) {
		u16 page = be32_to_cpup(paddr + i);
		u16 reg = be32_to_cpup(paddr + i + 1);
		u16 mask = be32_to_cpup(paddr + i + 2);
		u16 val_bits = be32_to_cpup(paddr + i + 3);
		int val;

		if (page != current_page) {
			current_page = page;
			ret = marvell_write_page(phydev, page);
			if (ret < 0)
				goto err;
		}

		val = 0;
		if (mask) {
			val = __phy_read(phydev, reg);
			if (val < 0) {
				ret = val;
				goto err;
			}
			val &= mask;
		}
		val |= val_bits;

		ret = __phy_write(phydev, reg, val);
		if (ret < 0)
			goto err;
	}
err:
	return phy_restore_page(phydev, saved_page, ret);
}
#else
static int marvell_of_reg_init(struct phy_device *phydev)
{
	return 0;
}
#endif /* CONFIG_OF_MDIO */

static int m88e1121_config_aneg_rgmii_delays(struct phy_device *phydev)
{
	int mscr;

	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID)
		mscr = MII_88E1121_PHY_MSCR_RX_DELAY |
		       MII_88E1121_PHY_MSCR_TX_DELAY;
	else if (phydev->interface == PHY_INTERFACE_MODE_RGMII_RXID)
		mscr = MII_88E1121_PHY_MSCR_RX_DELAY;
	else if (phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID)
		mscr = MII_88E1121_PHY_MSCR_TX_DELAY;
	else
		mscr = 0;

	return phy_modify_paged(phydev, MII_MARVELL_MSCR_PAGE,
				MII_88E1121_PHY_MSCR_REG,
				MII_88E1121_PHY_MSCR_DELAY_MASK, mscr);
}

static int m88e1121_config_aneg(struct phy_device *phydev)
{
	int changed = 0;
	int err = 0;

	if (phy_interface_is_rgmii(phydev)) {
		err = m88e1121_config_aneg_rgmii_delays(phydev);
		if (err < 0)
			return err;
	}

	err = marvell_set_polarity(phydev, phydev->mdix_ctrl);
	if (err < 0)
		return err;

	changed = err;

	err = genphy_config_aneg(phydev);
	if (err < 0)
		return err;

	if (phydev->autoneg != AUTONEG_ENABLE || changed) {
		/* A software reset is used to ensure a "commit" of the
		 * changes is done.
		 */
		err = genphy_soft_reset(phydev);
		if (err < 0)
			return err;
	}

	return 0;
}

static int m88e1318_config_aneg(struct phy_device *phydev)
{
	int err;

	err = phy_modify_paged(phydev, MII_MARVELL_MSCR_PAGE,
			       MII_88E1318S_PHY_MSCR1_REG,
			       0, MII_88E1318S_PHY_MSCR1_PAD_ODD);
	if (err < 0)
		return err;

	return m88e1121_config_aneg(phydev);
}

/**
 * linkmode_adv_to_fiber_adv_t
 * @advertise: the linkmode advertisement settings
 *
 * A small helper function that translates linkmode advertisement
 * settings to phy autonegotiation advertisements for the MII_ADV
 * register for fiber link.
 */
static inline u32 linkmode_adv_to_fiber_adv_t(unsigned long *advertise)
{
	u32 result = 0;

	if (linkmode_test_bit(ETHTOOL_LINK_MODE_1000baseT_Half_BIT, advertise))
		result |= ADVERTISE_FIBER_1000HALF;
	if (linkmode_test_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT, advertise))
		result |= ADVERTISE_FIBER_1000FULL;

	if (linkmode_test_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT, advertise) &&
	    linkmode_test_bit(ETHTOOL_LINK_MODE_Pause_BIT, advertise))
		result |= LPA_PAUSE_ASYM_FIBER;
	else if (linkmode_test_bit(ETHTOOL_LINK_MODE_Pause_BIT, advertise))
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

	if (phydev->autoneg != AUTONEG_ENABLE)
		return genphy_setup_forced(phydev);

	/* Only allow advertising what this PHY supports */
	linkmode_and(phydev->advertising, phydev->advertising,
		     phydev->supported);

	/* Setup fiber advertisement */
	adv = phy_read(phydev, MII_ADVERTISE);
	if (adv < 0)
		return adv;

	oldadv = adv;
	adv &= ~(ADVERTISE_FIBER_1000HALF | ADVERTISE_FIBER_1000FULL
		| LPA_PAUSE_FIBER);
	adv |= linkmode_adv_to_fiber_adv_t(phydev->advertising);

	if (adv != oldadv) {
		err = phy_write(phydev, MII_ADVERTISE, adv);
		if (err < 0)
			return err;

		changed = 1;
	}

	if (changed == 0) {
		/* Advertisement hasn't changed, but maybe aneg was never on to
		 * begin with?	Or maybe phy was isolated?
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

	err = marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
	if (err < 0)
		goto error;

	/* Configure the copper link first */
	err = m88e1318_config_aneg(phydev);
	if (err < 0)
		goto error;

	/* Do not touch the fiber page if we're in copper->sgmii mode */
	if (phydev->interface == PHY_INTERFACE_MODE_SGMII)
		return 0;

	/* Then the fiber link */
	err = marvell_set_page(phydev, MII_MARVELL_FIBER_PAGE);
	if (err < 0)
		goto error;

	err = marvell_config_aneg_fiber(phydev);
	if (err < 0)
		goto error;

	return marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);

error:
	marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
	return err;
}

static void marvell_config_led(struct phy_device *phydev)
{
	u16 def_config;
	int err;

	switch (MARVELL_PHY_FAMILY_ID(phydev->phy_id)) {
	/* Default PHY LED config: LED[0] .. Link, LED[1] .. Activity */
	case MARVELL_PHY_FAMILY_ID(MARVELL_PHY_ID_88E1121R):
	case MARVELL_PHY_FAMILY_ID(MARVELL_PHY_ID_88E1318S):
		def_config = MII_88E1121_PHY_LED_DEF;
		break;
	/* Default PHY LED config:
	 * LED[0] .. 1000Mbps Link
	 * LED[1] .. 100Mbps Link
	 * LED[2] .. Blink, Activity
	 */
	case MARVELL_PHY_FAMILY_ID(MARVELL_PHY_ID_88E1510):
		def_config = MII_88E1510_PHY_LED_DEF;
		break;
	default:
		return;
	}

	err = phy_write_paged(phydev, MII_MARVELL_LED_PAGE, MII_PHY_LED_CTRL,
			      def_config);
	if (err < 0)
		phydev_warn(phydev, "Fail to config marvell phy LED.\n");
}

static int marvell_config_init(struct phy_device *phydev)
{
	/* Set defalut LED */
	marvell_config_led(phydev);

	/* Set registers from marvell,reg-init DT property */
	return marvell_of_reg_init(phydev);
}

static int m88e1116r_config_init(struct phy_device *phydev)
{
	int err;

	err = genphy_soft_reset(phydev);
	if (err < 0)
		return err;

	msleep(500);

	err = marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
	if (err < 0)
		return err;

	err = marvell_set_polarity(phydev, phydev->mdix_ctrl);
	if (err < 0)
		return err;

	err = marvell_set_downshift(phydev, true, 8);
	if (err < 0)
		return err;

	if (phy_interface_is_rgmii(phydev)) {
		err = m88e1121_config_aneg_rgmii_delays(phydev);
		if (err < 0)
			return err;
	}

	err = genphy_soft_reset(phydev);
	if (err < 0)
		return err;

	return marvell_config_init(phydev);
}

static int m88e3016_config_init(struct phy_device *phydev)
{
	int ret;

	/* Enable Scrambler and Auto-Crossover */
	ret = phy_modify(phydev, MII_88E3016_PHY_SPEC_CTRL,
			 MII_88E3016_DISABLE_SCRAMBLER,
			 MII_88E3016_AUTO_MDIX_CROSSOVER);
	if (ret < 0)
		return ret;

	return marvell_config_init(phydev);
}

static int m88e1111_config_init_hwcfg_mode(struct phy_device *phydev,
					   u16 mode,
					   int fibre_copper_auto)
{
	if (fibre_copper_auto)
		mode |= MII_M1111_HWCFG_FIBER_COPPER_AUTO;

	return phy_modify(phydev, MII_M1111_PHY_EXT_SR,
			  MII_M1111_HWCFG_MODE_MASK |
			  MII_M1111_HWCFG_FIBER_COPPER_AUTO |
			  MII_M1111_HWCFG_FIBER_COPPER_RES,
			  mode);
}

static int m88e1111_config_init_rgmii_delays(struct phy_device *phydev)
{
	int delay;

	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID) {
		delay = MII_M1111_RGMII_RX_DELAY | MII_M1111_RGMII_TX_DELAY;
	} else if (phydev->interface == PHY_INTERFACE_MODE_RGMII_RXID) {
		delay = MII_M1111_RGMII_RX_DELAY;
	} else if (phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID) {
		delay = MII_M1111_RGMII_TX_DELAY;
	} else {
		delay = 0;
	}

	return phy_modify(phydev, MII_M1111_PHY_EXT_CR,
			  MII_M1111_RGMII_RX_DELAY | MII_M1111_RGMII_TX_DELAY,
			  delay);
}

static int m88e1111_config_init_rgmii(struct phy_device *phydev)
{
	int temp;
	int err;

	err = m88e1111_config_init_rgmii_delays(phydev);
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

	return phy_write(phydev, MII_M1111_PHY_EXT_SR, temp);
}

static int m88e1111_config_init_sgmii(struct phy_device *phydev)
{
	int err;

	err = m88e1111_config_init_hwcfg_mode(
		phydev,
		MII_M1111_HWCFG_MODE_SGMII_NO_CLK,
		MII_M1111_HWCFG_FIBER_COPPER_AUTO);
	if (err < 0)
		return err;

	/* make sure copper is selected */
	return marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
}

static int m88e1111_config_init_rtbi(struct phy_device *phydev)
{
	int err;

	err = m88e1111_config_init_rgmii_delays(phydev);
	if (err < 0)
		return err;

	err = m88e1111_config_init_hwcfg_mode(
		phydev,
		MII_M1111_HWCFG_MODE_RTBI,
		MII_M1111_HWCFG_FIBER_COPPER_AUTO);
	if (err < 0)
		return err;

	/* soft reset */
	err = genphy_soft_reset(phydev);
	if (err < 0)
		return err;

	return m88e1111_config_init_hwcfg_mode(
		phydev,
		MII_M1111_HWCFG_MODE_RTBI,
		MII_M1111_HWCFG_FIBER_COPPER_AUTO);
}

static int m88e1111_config_init(struct phy_device *phydev)
{
	int err;

	if (phy_interface_is_rgmii(phydev)) {
		err = m88e1111_config_init_rgmii(phydev);
		if (err < 0)
			return err;
	}

	if (phydev->interface == PHY_INTERFACE_MODE_SGMII) {
		err = m88e1111_config_init_sgmii(phydev);
		if (err < 0)
			return err;
	}

	if (phydev->interface == PHY_INTERFACE_MODE_RTBI) {
		err = m88e1111_config_init_rtbi(phydev);
		if (err < 0)
			return err;
	}

	err = marvell_of_reg_init(phydev);
	if (err < 0)
		return err;

	return genphy_soft_reset(phydev);
}

static int m88e1318_config_init(struct phy_device *phydev)
{
	if (phy_interrupt_is_valid(phydev)) {
		int err = phy_modify_paged(
			phydev, MII_MARVELL_LED_PAGE,
			MII_88E1318S_PHY_LED_TCR,
			MII_88E1318S_PHY_LED_TCR_FORCE_INT,
			MII_88E1318S_PHY_LED_TCR_INTn_ENABLE |
			MII_88E1318S_PHY_LED_TCR_INT_ACTIVE_LOW);
		if (err < 0)
			return err;
	}

	return marvell_config_init(phydev);
}

static int m88e1510_config_init(struct phy_device *phydev)
{
	int err;

	/* SGMII-to-Copper mode initialization */
	if (phydev->interface == PHY_INTERFACE_MODE_SGMII) {

		/* Select page 18 */
		err = marvell_set_page(phydev, 18);
		if (err < 0)
			return err;

		/* In reg 20, write MODE[2:0] = 0x1 (SGMII to Copper) */
		err = phy_modify(phydev, MII_88E1510_GEN_CTRL_REG_1,
				 MII_88E1510_GEN_CTRL_REG_1_MODE_MASK,
				 MII_88E1510_GEN_CTRL_REG_1_MODE_SGMII);
		if (err < 0)
			return err;

		/* PHY reset is necessary after changing MODE[2:0] */
		err = phy_modify(phydev, MII_88E1510_GEN_CTRL_REG_1, 0,
				 MII_88E1510_GEN_CTRL_REG_1_RESET);
		if (err < 0)
			return err;

		/* Reset page selection */
		err = marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
		if (err < 0)
			return err;

		/* There appears to be a bug in the 88e1512 when used in
		 * SGMII to copper mode, where the AN advertisement register
		 * clears the pause bits each time a negotiation occurs.
		 * This means we can never be truely sure what was advertised,
		 * so disable Pause support.
		 */
		linkmode_clear_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT,
				   phydev->supported);
		linkmode_clear_bit(ETHTOOL_LINK_MODE_Pause_BIT,
				   phydev->supported);
		linkmode_clear_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT,
				   phydev->advertising);
		linkmode_clear_bit(ETHTOOL_LINK_MODE_Pause_BIT,
				   phydev->advertising);
	}

	return m88e1318_config_init(phydev);
}

static int m88e1118_config_aneg(struct phy_device *phydev)
{
	int err;

	err = genphy_soft_reset(phydev);
	if (err < 0)
		return err;

	err = marvell_set_polarity(phydev, phydev->mdix_ctrl);
	if (err < 0)
		return err;

	err = genphy_config_aneg(phydev);
	return 0;
}

static int m88e1118_config_init(struct phy_device *phydev)
{
	int err;

	/* Change address */
	err = marvell_set_page(phydev, MII_MARVELL_MSCR_PAGE);
	if (err < 0)
		return err;

	/* Enable 1000 Mbit */
	err = phy_write(phydev, 0x15, 0x1070);
	if (err < 0)
		return err;

	/* Change address */
	err = marvell_set_page(phydev, MII_MARVELL_LED_PAGE);
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
	err = marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
	if (err < 0)
		return err;

	return genphy_soft_reset(phydev);
}

static int m88e1149_config_init(struct phy_device *phydev)
{
	int err;

	/* Change address */
	err = marvell_set_page(phydev, MII_MARVELL_MSCR_PAGE);
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
	err = marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
	if (err < 0)
		return err;

	return genphy_soft_reset(phydev);
}

static int m88e1145_config_init_rgmii(struct phy_device *phydev)
{
	int err;

	err = m88e1111_config_init_rgmii_delays(phydev);
	if (err < 0)
		return err;

	if (phydev->dev_flags & MARVELL_PHY_M1145_FLAGS_RESISTANCE) {
		err = phy_write(phydev, 0x1d, 0x0012);
		if (err < 0)
			return err;

		err = phy_modify(phydev, 0x1e, 0x0fc0,
				 2 << 9 | /* 36 ohm */
				 2 << 6); /* 39 ohm */
		if (err < 0)
			return err;

		err = phy_write(phydev, 0x1d, 0x3);
		if (err < 0)
			return err;

		err = phy_write(phydev, 0x1e, 0x8000);
	}
	return err;
}

static int m88e1145_config_init_sgmii(struct phy_device *phydev)
{
	return m88e1111_config_init_hwcfg_mode(
		phydev, MII_M1111_HWCFG_MODE_SGMII_NO_CLK,
		MII_M1111_HWCFG_FIBER_COPPER_AUTO);
}

static int m88e1145_config_init(struct phy_device *phydev)
{
	int err;

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
		err = m88e1145_config_init_rgmii(phydev);
		if (err < 0)
			return err;
	}

	if (phydev->interface == PHY_INTERFACE_MODE_SGMII) {
		err = m88e1145_config_init_sgmii(phydev);
		if (err < 0)
			return err;
	}

	err = marvell_of_reg_init(phydev);
	if (err < 0)
		return err;

	return 0;
}

/**
 * fiber_lpa_mod_linkmode_lpa_t
 * @advertising: the linkmode advertisement settings
 * @lpa: value of the MII_LPA register for fiber link
 *
 * A small helper function that translates MII_LPA bits to linkmode LP
 * advertisement settings. Other bits in advertising are left
 * unchanged.
 */
static void fiber_lpa_mod_linkmode_lpa_t(unsigned long *advertising, u32 lpa)
{
	linkmode_mod_bit(ETHTOOL_LINK_MODE_1000baseT_Half_BIT,
			 advertising, lpa & LPA_FIBER_1000HALF);

	linkmode_mod_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
			 advertising, lpa & LPA_FIBER_1000FULL);
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
	 * register for fiber case
	 */
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

static int marvell_read_status_page_an(struct phy_device *phydev,
				       int fiber)
{
	int status;
	int lpa;
	int lpagb;

	status = phy_read(phydev, MII_M1011_PHY_STATUS);
	if (status < 0)
		return status;

	lpa = phy_read(phydev, MII_LPA);
	if (lpa < 0)
		return lpa;

	lpagb = phy_read(phydev, MII_STAT1000);
	if (lpagb < 0)
		return lpagb;

	if (status & MII_M1011_PHY_STATUS_FULLDUPLEX)
		phydev->duplex = DUPLEX_FULL;
	else
		phydev->duplex = DUPLEX_HALF;

	status = status & MII_M1011_PHY_STATUS_SPD_MASK;
	phydev->pause = 0;
	phydev->asym_pause = 0;

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
		mii_lpa_to_linkmode_lpa_t(phydev->lp_advertising, lpa);
		mii_stat1000_mod_linkmode_lpa_t(phydev->lp_advertising, lpagb);

		if (phydev->duplex == DUPLEX_FULL) {
			phydev->pause = lpa & LPA_PAUSE_CAP ? 1 : 0;
			phydev->asym_pause = lpa & LPA_PAUSE_ASYM ? 1 : 0;
		}
	} else {
		/* The fiber link is only 1000M capable */
		fiber_lpa_mod_linkmode_lpa_t(phydev->lp_advertising, lpa);

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
	return 0;
}

static int marvell_read_status_page_fixed(struct phy_device *phydev)
{
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

	phydev->pause = 0;
	phydev->asym_pause = 0;
	linkmode_zero(phydev->lp_advertising);

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
	int fiber;
	int err;

	/* Detect and update the link, but return if there
	 * was an error
	 */
	if (page == MII_MARVELL_FIBER_PAGE)
		fiber = 1;
	else
		fiber = 0;

	err = marvell_update_link(phydev, fiber);
	if (err)
		return err;

	if (phydev->autoneg == AUTONEG_ENABLE)
		err = marvell_read_status_page_an(phydev, fiber);
	else
		err = marvell_read_status_page_fixed(phydev);

	return err;
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
	if (linkmode_test_bit(ETHTOOL_LINK_MODE_FIBRE_BIT,
			      phydev->supported) &&
	    phydev->interface != PHY_INTERFACE_MODE_SGMII) {
		err = marvell_set_page(phydev, MII_MARVELL_FIBER_PAGE);
		if (err < 0)
			goto error;

		err = marvell_read_status_page(phydev, MII_MARVELL_FIBER_PAGE);
		if (err < 0)
			goto error;

		/* If the fiber link is up, it is the selected and
		 * used link. In this case, we need to stay in the
		 * fiber page. Please to be careful about that, avoid
		 * to restore Copper page in other functions which
		 * could break the behaviour for some fiber phy like
		 * 88E1512.
		 */
		if (phydev->link)
			return 0;

		/* If fiber link is down, check and save copper mode state */
		err = marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
		if (err < 0)
			goto error;
	}

	return marvell_read_status_page(phydev, MII_MARVELL_COPPER_PAGE);

error:
	marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
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
	if (!linkmode_test_bit(ETHTOOL_LINK_MODE_FIBRE_BIT,
			       phydev->supported)) {
		err = marvell_set_page(phydev, MII_MARVELL_FIBER_PAGE);
		if (err < 0)
			goto error;

		/* With the page set, use the generic suspend */
		err = genphy_suspend(phydev);
		if (err < 0)
			goto error;

		/* Then, the copper link */
		err = marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
		if (err < 0)
			goto error;
	}

	/* With the page set, use the generic suspend */
	return genphy_suspend(phydev);

error:
	marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
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
	if (!linkmode_test_bit(ETHTOOL_LINK_MODE_FIBRE_BIT,
			       phydev->supported)) {
		err = marvell_set_page(phydev, MII_MARVELL_FIBER_PAGE);
		if (err < 0)
			goto error;

		/* With the page set, use the generic resume */
		err = genphy_resume(phydev);
		if (err < 0)
			goto error;

		/* Then, the copper link */
		err = marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
		if (err < 0)
			goto error;
	}

	/* With the page set, use the generic resume */
	return genphy_resume(phydev);

error:
	marvell_set_page(phydev, MII_MARVELL_COPPER_PAGE);
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

static void m88e1318_get_wol(struct phy_device *phydev,
			     struct ethtool_wolinfo *wol)
{
	int oldpage, ret = 0;

	wol->supported = WAKE_MAGIC;
	wol->wolopts = 0;

	oldpage = phy_select_page(phydev, MII_MARVELL_WOL_PAGE);
	if (oldpage < 0)
		goto error;

	ret = __phy_read(phydev, MII_88E1318S_PHY_WOL_CTRL);
	if (ret & MII_88E1318S_PHY_WOL_CTRL_MAGIC_PACKET_MATCH_ENABLE)
		wol->wolopts |= WAKE_MAGIC;

error:
	phy_restore_page(phydev, oldpage, ret);
}

static int m88e1318_set_wol(struct phy_device *phydev,
			    struct ethtool_wolinfo *wol)
{
	int err = 0, oldpage;

	oldpage = phy_save_page(phydev);
	if (oldpage < 0)
		goto error;

	if (wol->wolopts & WAKE_MAGIC) {
		/* Explicitly switch to page 0x00, just to be sure */
		err = marvell_write_page(phydev, MII_MARVELL_COPPER_PAGE);
		if (err < 0)
			goto error;

		/* If WOL event happened once, the LED[2] interrupt pin
		 * will not be cleared unless we reading the interrupt status
		 * register. If interrupts are in use, the normal interrupt
		 * handling will clear the WOL event. Clear the WOL event
		 * before enabling it if !phy_interrupt_is_valid()
		 */
		if (!phy_interrupt_is_valid(phydev))
			phy_read(phydev, MII_M1011_IEVENT);

		/* Enable the WOL interrupt */
		err = __phy_modify(phydev, MII_88E1318S_PHY_CSIER, 0,
				   MII_88E1318S_PHY_CSIER_WOL_EIE);
		if (err < 0)
			goto error;

		err = marvell_write_page(phydev, MII_MARVELL_LED_PAGE);
		if (err < 0)
			goto error;

		/* Setup LED[2] as interrupt pin (active low) */
		err = __phy_modify(phydev, MII_88E1318S_PHY_LED_TCR,
				   MII_88E1318S_PHY_LED_TCR_FORCE_INT,
				   MII_88E1318S_PHY_LED_TCR_INTn_ENABLE |
				   MII_88E1318S_PHY_LED_TCR_INT_ACTIVE_LOW);
		if (err < 0)
			goto error;

		err = marvell_write_page(phydev, MII_MARVELL_WOL_PAGE);
		if (err < 0)
			goto error;

		/* Store the device address for the magic packet */
		err = __phy_write(phydev, MII_88E1318S_PHY_MAGIC_PACKET_WORD2,
				((phydev->attached_dev->dev_addr[5] << 8) |
				 phydev->attached_dev->dev_addr[4]));
		if (err < 0)
			goto error;
		err = __phy_write(phydev, MII_88E1318S_PHY_MAGIC_PACKET_WORD1,
				((phydev->attached_dev->dev_addr[3] << 8) |
				 phydev->attached_dev->dev_addr[2]));
		if (err < 0)
			goto error;
		err = __phy_write(phydev, MII_88E1318S_PHY_MAGIC_PACKET_WORD0,
				((phydev->attached_dev->dev_addr[1] << 8) |
				 phydev->attached_dev->dev_addr[0]));
		if (err < 0)
			goto error;

		/* Clear WOL status and enable magic packet matching */
		err = __phy_modify(phydev, MII_88E1318S_PHY_WOL_CTRL, 0,
				   MII_88E1318S_PHY_WOL_CTRL_CLEAR_WOL_STATUS |
				   MII_88E1318S_PHY_WOL_CTRL_MAGIC_PACKET_MATCH_ENABLE);
		if (err < 0)
			goto error;
	} else {
		err = marvell_write_page(phydev, MII_MARVELL_WOL_PAGE);
		if (err < 0)
			goto error;

		/* Clear WOL status and disable magic packet matching */
		err = __phy_modify(phydev, MII_88E1318S_PHY_WOL_CTRL,
				   MII_88E1318S_PHY_WOL_CTRL_MAGIC_PACKET_MATCH_ENABLE,
				   MII_88E1318S_PHY_WOL_CTRL_CLEAR_WOL_STATUS);
		if (err < 0)
			goto error;
	}

error:
	return phy_restore_page(phydev, oldpage, err);
}

static int marvell_get_sset_count(struct phy_device *phydev)
{
	if (linkmode_test_bit(ETHTOOL_LINK_MODE_FIBRE_BIT,
			      phydev->supported))
		return ARRAY_SIZE(marvell_hw_stats);
	else
		return ARRAY_SIZE(marvell_hw_stats) - NB_FIBER_STATS;
}

static void marvell_get_strings(struct phy_device *phydev, u8 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(marvell_hw_stats); i++) {
		strlcpy(data + i * ETH_GSTRING_LEN,
			marvell_hw_stats[i].string, ETH_GSTRING_LEN);
	}
}

static u64 marvell_get_stat(struct phy_device *phydev, int i)
{
	struct marvell_hw_stat stat = marvell_hw_stats[i];
	struct marvell_priv *priv = phydev->priv;
	int val;
	u64 ret;

	val = phy_read_paged(phydev, stat.page, stat.reg);
	if (val < 0) {
		ret = U64_MAX;
	} else {
		val = val & ((1 << stat.bits) - 1);
		priv->stats[i] += val;
		ret = priv->stats[i];
	}

	return ret;
}

static void marvell_get_stats(struct phy_device *phydev,
			      struct ethtool_stats *stats, u64 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(marvell_hw_stats); i++)
		data[i] = marvell_get_stat(phydev, i);
}

#ifdef CONFIG_HWMON
static int m88e1121_get_temp(struct phy_device *phydev, long *temp)
{
	int oldpage;
	int ret = 0;
	int val;

	*temp = 0;

	oldpage = phy_select_page(phydev, MII_MARVELL_MISC_TEST_PAGE);
	if (oldpage < 0)
		goto error;

	/* Enable temperature sensor */
	ret = __phy_read(phydev, MII_88E1121_MISC_TEST);
	if (ret < 0)
		goto error;

	ret = __phy_write(phydev, MII_88E1121_MISC_TEST,
			  ret | MII_88E1121_MISC_TEST_TEMP_SENSOR_EN);
	if (ret < 0)
		goto error;

	/* Wait for temperature to stabilize */
	usleep_range(10000, 12000);

	val = __phy_read(phydev, MII_88E1121_MISC_TEST);
	if (val < 0) {
		ret = val;
		goto error;
	}

	/* Disable temperature sensor */
	ret = __phy_write(phydev, MII_88E1121_MISC_TEST,
			  ret & ~MII_88E1121_MISC_TEST_TEMP_SENSOR_EN);
	if (ret < 0)
		goto error;

	*temp = ((val & MII_88E1121_MISC_TEST_TEMP_MASK) - 5) * 5000;

error:
	return phy_restore_page(phydev, oldpage, ret);
}

static int m88e1121_hwmon_read(struct device *dev,
			       enum hwmon_sensor_types type,
			       u32 attr, int channel, long *temp)
{
	struct phy_device *phydev = dev_get_drvdata(dev);
	int err;

	switch (attr) {
	case hwmon_temp_input:
		err = m88e1121_get_temp(phydev, temp);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return err;
}

static umode_t m88e1121_hwmon_is_visible(const void *data,
					 enum hwmon_sensor_types type,
					 u32 attr, int channel)
{
	if (type != hwmon_temp)
		return 0;

	switch (attr) {
	case hwmon_temp_input:
		return 0444;
	default:
		return 0;
	}
}

static u32 m88e1121_hwmon_chip_config[] = {
	HWMON_C_REGISTER_TZ,
	0
};

static const struct hwmon_channel_info m88e1121_hwmon_chip = {
	.type = hwmon_chip,
	.config = m88e1121_hwmon_chip_config,
};

static u32 m88e1121_hwmon_temp_config[] = {
	HWMON_T_INPUT,
	0
};

static const struct hwmon_channel_info m88e1121_hwmon_temp = {
	.type = hwmon_temp,
	.config = m88e1121_hwmon_temp_config,
};

static const struct hwmon_channel_info *m88e1121_hwmon_info[] = {
	&m88e1121_hwmon_chip,
	&m88e1121_hwmon_temp,
	NULL
};

static const struct hwmon_ops m88e1121_hwmon_hwmon_ops = {
	.is_visible = m88e1121_hwmon_is_visible,
	.read = m88e1121_hwmon_read,
};

static const struct hwmon_chip_info m88e1121_hwmon_chip_info = {
	.ops = &m88e1121_hwmon_hwmon_ops,
	.info = m88e1121_hwmon_info,
};

static int m88e1510_get_temp(struct phy_device *phydev, long *temp)
{
	int ret;

	*temp = 0;

	ret = phy_read_paged(phydev, MII_MARVELL_MISC_TEST_PAGE,
			     MII_88E1510_TEMP_SENSOR);
	if (ret < 0)
		return ret;

	*temp = ((ret & MII_88E1510_TEMP_SENSOR_MASK) - 25) * 1000;

	return 0;
}

static int m88e1510_get_temp_critical(struct phy_device *phydev, long *temp)
{
	int ret;

	*temp = 0;

	ret = phy_read_paged(phydev, MII_MARVELL_MISC_TEST_PAGE,
			     MII_88E1121_MISC_TEST);
	if (ret < 0)
		return ret;

	*temp = (((ret & MII_88E1510_MISC_TEST_TEMP_THRESHOLD_MASK) >>
		  MII_88E1510_MISC_TEST_TEMP_THRESHOLD_SHIFT) * 5) - 25;
	/* convert to mC */
	*temp *= 1000;

	return 0;
}

static int m88e1510_set_temp_critical(struct phy_device *phydev, long temp)
{
	temp = temp / 1000;
	temp = clamp_val(DIV_ROUND_CLOSEST(temp, 5) + 5, 0, 0x1f);

	return phy_modify_paged(phydev, MII_MARVELL_MISC_TEST_PAGE,
				MII_88E1121_MISC_TEST,
				MII_88E1510_MISC_TEST_TEMP_THRESHOLD_MASK,
				temp << MII_88E1510_MISC_TEST_TEMP_THRESHOLD_SHIFT);
}

static int m88e1510_get_temp_alarm(struct phy_device *phydev, long *alarm)
{
	int ret;

	*alarm = false;

	ret = phy_read_paged(phydev, MII_MARVELL_MISC_TEST_PAGE,
			     MII_88E1121_MISC_TEST);
	if (ret < 0)
		return ret;

	*alarm = !!(ret & MII_88E1510_MISC_TEST_TEMP_IRQ);

	return 0;
}

static int m88e1510_hwmon_read(struct device *dev,
			       enum hwmon_sensor_types type,
			       u32 attr, int channel, long *temp)
{
	struct phy_device *phydev = dev_get_drvdata(dev);
	int err;

	switch (attr) {
	case hwmon_temp_input:
		err = m88e1510_get_temp(phydev, temp);
		break;
	case hwmon_temp_crit:
		err = m88e1510_get_temp_critical(phydev, temp);
		break;
	case hwmon_temp_max_alarm:
		err = m88e1510_get_temp_alarm(phydev, temp);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return err;
}

static int m88e1510_hwmon_write(struct device *dev,
				enum hwmon_sensor_types type,
				u32 attr, int channel, long temp)
{
	struct phy_device *phydev = dev_get_drvdata(dev);
	int err;

	switch (attr) {
	case hwmon_temp_crit:
		err = m88e1510_set_temp_critical(phydev, temp);
		break;
	default:
		return -EOPNOTSUPP;
	}
	return err;
}

static umode_t m88e1510_hwmon_is_visible(const void *data,
					 enum hwmon_sensor_types type,
					 u32 attr, int channel)
{
	if (type != hwmon_temp)
		return 0;

	switch (attr) {
	case hwmon_temp_input:
	case hwmon_temp_max_alarm:
		return 0444;
	case hwmon_temp_crit:
		return 0644;
	default:
		return 0;
	}
}

static u32 m88e1510_hwmon_temp_config[] = {
	HWMON_T_INPUT | HWMON_T_CRIT | HWMON_T_MAX_ALARM,
	0
};

static const struct hwmon_channel_info m88e1510_hwmon_temp = {
	.type = hwmon_temp,
	.config = m88e1510_hwmon_temp_config,
};

static const struct hwmon_channel_info *m88e1510_hwmon_info[] = {
	&m88e1121_hwmon_chip,
	&m88e1510_hwmon_temp,
	NULL
};

static const struct hwmon_ops m88e1510_hwmon_hwmon_ops = {
	.is_visible = m88e1510_hwmon_is_visible,
	.read = m88e1510_hwmon_read,
	.write = m88e1510_hwmon_write,
};

static const struct hwmon_chip_info m88e1510_hwmon_chip_info = {
	.ops = &m88e1510_hwmon_hwmon_ops,
	.info = m88e1510_hwmon_info,
};

static int m88e6390_get_temp(struct phy_device *phydev, long *temp)
{
	int sum = 0;
	int oldpage;
	int ret = 0;
	int i;

	*temp = 0;

	oldpage = phy_select_page(phydev, MII_MARVELL_MISC_TEST_PAGE);
	if (oldpage < 0)
		goto error;

	/* Enable temperature sensor */
	ret = __phy_read(phydev, MII_88E6390_MISC_TEST);
	if (ret < 0)
		goto error;

	ret = ret & ~MII_88E6390_MISC_TEST_SAMPLE_MASK;
	ret |= MII_88E6390_MISC_TEST_SAMPLE_ENABLE |
		MII_88E6390_MISC_TEST_SAMPLE_1S;

	ret = __phy_write(phydev, MII_88E6390_MISC_TEST, ret);
	if (ret < 0)
		goto error;

	/* Wait for temperature to stabilize */
	usleep_range(10000, 12000);

	/* Reading the temperature sense has an errata. You need to read
	 * a number of times and take an average.
	 */
	for (i = 0; i < MII_88E6390_TEMP_SENSOR_SAMPLES; i++) {
		ret = __phy_read(phydev, MII_88E6390_TEMP_SENSOR);
		if (ret < 0)
			goto error;
		sum += ret & MII_88E6390_TEMP_SENSOR_MASK;
	}

	sum /= MII_88E6390_TEMP_SENSOR_SAMPLES;
	*temp = (sum  - 75) * 1000;

	/* Disable temperature sensor */
	ret = __phy_read(phydev, MII_88E6390_MISC_TEST);
	if (ret < 0)
		goto error;

	ret = ret & ~MII_88E6390_MISC_TEST_SAMPLE_MASK;
	ret |= MII_88E6390_MISC_TEST_SAMPLE_DISABLE;

	ret = __phy_write(phydev, MII_88E6390_MISC_TEST, ret);

error:
	phy_restore_page(phydev, oldpage, ret);

	return ret;
}

static int m88e6390_hwmon_read(struct device *dev,
			       enum hwmon_sensor_types type,
			       u32 attr, int channel, long *temp)
{
	struct phy_device *phydev = dev_get_drvdata(dev);
	int err;

	switch (attr) {
	case hwmon_temp_input:
		err = m88e6390_get_temp(phydev, temp);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return err;
}

static umode_t m88e6390_hwmon_is_visible(const void *data,
					 enum hwmon_sensor_types type,
					 u32 attr, int channel)
{
	if (type != hwmon_temp)
		return 0;

	switch (attr) {
	case hwmon_temp_input:
		return 0444;
	default:
		return 0;
	}
}

static u32 m88e6390_hwmon_temp_config[] = {
	HWMON_T_INPUT,
	0
};

static const struct hwmon_channel_info m88e6390_hwmon_temp = {
	.type = hwmon_temp,
	.config = m88e6390_hwmon_temp_config,
};

static const struct hwmon_channel_info *m88e6390_hwmon_info[] = {
	&m88e1121_hwmon_chip,
	&m88e6390_hwmon_temp,
	NULL
};

static const struct hwmon_ops m88e6390_hwmon_hwmon_ops = {
	.is_visible = m88e6390_hwmon_is_visible,
	.read = m88e6390_hwmon_read,
};

static const struct hwmon_chip_info m88e6390_hwmon_chip_info = {
	.ops = &m88e6390_hwmon_hwmon_ops,
	.info = m88e6390_hwmon_info,
};

static int marvell_hwmon_name(struct phy_device *phydev)
{
	struct marvell_priv *priv = phydev->priv;
	struct device *dev = &phydev->mdio.dev;
	const char *devname = dev_name(dev);
	size_t len = strlen(devname);
	int i, j;

	priv->hwmon_name = devm_kzalloc(dev, len, GFP_KERNEL);
	if (!priv->hwmon_name)
		return -ENOMEM;

	for (i = j = 0; i < len && devname[i]; i++) {
		if (isalnum(devname[i]))
			priv->hwmon_name[j++] = devname[i];
	}

	return 0;
}

static int marvell_hwmon_probe(struct phy_device *phydev,
			       const struct hwmon_chip_info *chip)
{
	struct marvell_priv *priv = phydev->priv;
	struct device *dev = &phydev->mdio.dev;
	int err;

	err = marvell_hwmon_name(phydev);
	if (err)
		return err;

	priv->hwmon_dev = devm_hwmon_device_register_with_info(
		dev, priv->hwmon_name, phydev, chip, NULL);

	return PTR_ERR_OR_ZERO(priv->hwmon_dev);
}

static int m88e1121_hwmon_probe(struct phy_device *phydev)
{
	return marvell_hwmon_probe(phydev, &m88e1121_hwmon_chip_info);
}

static int m88e1510_hwmon_probe(struct phy_device *phydev)
{
	return marvell_hwmon_probe(phydev, &m88e1510_hwmon_chip_info);
}

static int m88e6390_hwmon_probe(struct phy_device *phydev)
{
	return marvell_hwmon_probe(phydev, &m88e6390_hwmon_chip_info);
}
#else
static int m88e1121_hwmon_probe(struct phy_device *phydev)
{
	return 0;
}

static int m88e1510_hwmon_probe(struct phy_device *phydev)
{
	return 0;
}

static int m88e6390_hwmon_probe(struct phy_device *phydev)
{
	return 0;
}
#endif

static int marvell_probe(struct phy_device *phydev)
{
	struct marvell_priv *priv;

	priv = devm_kzalloc(&phydev->mdio.dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	return 0;
}

static int m88e1121_probe(struct phy_device *phydev)
{
	int err;

	err = marvell_probe(phydev);
	if (err)
		return err;

	return m88e1121_hwmon_probe(phydev);
}

static int m88e1510_probe(struct phy_device *phydev)
{
	int err;

	err = marvell_probe(phydev);
	if (err)
		return err;

	return m88e1510_hwmon_probe(phydev);
}

static int m88e6390_probe(struct phy_device *phydev)
{
	int err;

	err = marvell_probe(phydev);
	if (err)
		return err;

	return m88e6390_hwmon_probe(phydev);
}

static struct phy_driver marvell_drivers[] = {
	{
		.phy_id = MARVELL_PHY_ID_88E1101,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1101",
		.features = PHY_GBIT_FEATURES,
		.probe = marvell_probe,
		.config_init = &marvell_config_init,
		.config_aneg = &m88e1101_config_aneg,
		.ack_interrupt = &marvell_ack_interrupt,
		.config_intr = &marvell_config_intr,
		.resume = &genphy_resume,
		.suspend = &genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1112,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1112",
		.features = PHY_GBIT_FEATURES,
		.probe = marvell_probe,
		.config_init = &m88e1111_config_init,
		.config_aneg = &marvell_config_aneg,
		.ack_interrupt = &marvell_ack_interrupt,
		.config_intr = &marvell_config_intr,
		.resume = &genphy_resume,
		.suspend = &genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1111,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1111",
		.features = PHY_GBIT_FEATURES,
		.probe = marvell_probe,
		.config_init = &m88e1111_config_init,
		.config_aneg = &marvell_config_aneg,
		.read_status = &marvell_read_status,
		.ack_interrupt = &marvell_ack_interrupt,
		.config_intr = &marvell_config_intr,
		.resume = &genphy_resume,
		.suspend = &genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1118,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1118",
		.features = PHY_GBIT_FEATURES,
		.probe = marvell_probe,
		.config_init = &m88e1118_config_init,
		.config_aneg = &m88e1118_config_aneg,
		.ack_interrupt = &marvell_ack_interrupt,
		.config_intr = &marvell_config_intr,
		.resume = &genphy_resume,
		.suspend = &genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1121R,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1121R",
		.features = PHY_GBIT_FEATURES,
		.probe = &m88e1121_probe,
		.config_init = &marvell_config_init,
		.config_aneg = &m88e1121_config_aneg,
		.read_status = &marvell_read_status,
		.ack_interrupt = &marvell_ack_interrupt,
		.config_intr = &marvell_config_intr,
		.did_interrupt = &m88e1121_did_interrupt,
		.resume = &genphy_resume,
		.suspend = &genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1318S,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1318S",
		.features = PHY_GBIT_FEATURES,
		.probe = marvell_probe,
		.config_init = &m88e1318_config_init,
		.config_aneg = &m88e1318_config_aneg,
		.read_status = &marvell_read_status,
		.ack_interrupt = &marvell_ack_interrupt,
		.config_intr = &marvell_config_intr,
		.did_interrupt = &m88e1121_did_interrupt,
		.get_wol = &m88e1318_get_wol,
		.set_wol = &m88e1318_set_wol,
		.resume = &genphy_resume,
		.suspend = &genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1145,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1145",
		.features = PHY_GBIT_FEATURES,
		.probe = marvell_probe,
		.config_init = &m88e1145_config_init,
		.config_aneg = &m88e1101_config_aneg,
		.read_status = &genphy_read_status,
		.ack_interrupt = &marvell_ack_interrupt,
		.config_intr = &marvell_config_intr,
		.resume = &genphy_resume,
		.suspend = &genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1149R,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1149R",
		.features = PHY_GBIT_FEATURES,
		.probe = marvell_probe,
		.config_init = &m88e1149_config_init,
		.config_aneg = &m88e1118_config_aneg,
		.ack_interrupt = &marvell_ack_interrupt,
		.config_intr = &marvell_config_intr,
		.resume = &genphy_resume,
		.suspend = &genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1240,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1240",
		.features = PHY_GBIT_FEATURES,
		.probe = marvell_probe,
		.config_init = &m88e1111_config_init,
		.config_aneg = &marvell_config_aneg,
		.ack_interrupt = &marvell_ack_interrupt,
		.config_intr = &marvell_config_intr,
		.resume = &genphy_resume,
		.suspend = &genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1116R,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1116R",
		.features = PHY_GBIT_FEATURES,
		.probe = marvell_probe,
		.config_init = &m88e1116r_config_init,
		.ack_interrupt = &marvell_ack_interrupt,
		.config_intr = &marvell_config_intr,
		.resume = &genphy_resume,
		.suspend = &genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1510,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1510",
		.features = PHY_GBIT_FIBRE_FEATURES,
		.probe = &m88e1510_probe,
		.config_init = &m88e1510_config_init,
		.config_aneg = &m88e1510_config_aneg,
		.read_status = &marvell_read_status,
		.ack_interrupt = &marvell_ack_interrupt,
		.config_intr = &marvell_config_intr,
		.did_interrupt = &m88e1121_did_interrupt,
		.get_wol = &m88e1318_get_wol,
		.set_wol = &m88e1318_set_wol,
		.resume = &marvell_resume,
		.suspend = &marvell_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
		.set_loopback = genphy_loopback,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1540,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1540",
		.features = PHY_GBIT_FEATURES,
		.probe = m88e1510_probe,
		.config_init = &marvell_config_init,
		.config_aneg = &m88e1510_config_aneg,
		.read_status = &marvell_read_status,
		.ack_interrupt = &marvell_ack_interrupt,
		.config_intr = &marvell_config_intr,
		.did_interrupt = &m88e1121_did_interrupt,
		.resume = &genphy_resume,
		.suspend = &genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E1545,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E1545",
		.probe = m88e1510_probe,
		.features = PHY_GBIT_FEATURES,
		.config_init = &marvell_config_init,
		.config_aneg = &m88e1510_config_aneg,
		.read_status = &marvell_read_status,
		.ack_interrupt = &marvell_ack_interrupt,
		.config_intr = &marvell_config_intr,
		.did_interrupt = &m88e1121_did_interrupt,
		.resume = &genphy_resume,
		.suspend = &genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E3016,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E3016",
		.features = PHY_BASIC_FEATURES,
		.probe = marvell_probe,
		.config_init = &m88e3016_config_init,
		.aneg_done = &marvell_aneg_done,
		.read_status = &marvell_read_status,
		.ack_interrupt = &marvell_ack_interrupt,
		.config_intr = &marvell_config_intr,
		.did_interrupt = &m88e1121_did_interrupt,
		.resume = &genphy_resume,
		.suspend = &genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
		.get_sset_count = marvell_get_sset_count,
		.get_strings = marvell_get_strings,
		.get_stats = marvell_get_stats,
	},
	{
		.phy_id = MARVELL_PHY_ID_88E6390,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88E6390",
		.features = PHY_GBIT_FEATURES,
		.probe = m88e6390_probe,
		.config_init = &marvell_config_init,
		.config_aneg = &m88e1510_config_aneg,
		.read_status = &marvell_read_status,
		.ack_interrupt = &marvell_ack_interrupt,
		.config_intr = &marvell_config_intr,
		.did_interrupt = &m88e1121_did_interrupt,
		.resume = &genphy_resume,
		.suspend = &genphy_suspend,
		.read_page = marvell_read_page,
		.write_page = marvell_write_page,
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
	{ MARVELL_PHY_ID_88E1545, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E3016, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E6390, MARVELL_PHY_ID_MASK },
	{ }
};

MODULE_DEVICE_TABLE(mdio, marvell_tbl);
