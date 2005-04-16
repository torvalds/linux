/*
 * ibm_ocp_phy.c
 *
 * PHY drivers for the ibm ocp ethernet driver. Borrowed
 * from sungem_phy.c, though I only kept the generic MII
 * driver for now.
 * 
 * This file should be shared with other drivers or eventually
 * merged as the "low level" part of miilib
 * 
 * (c) 2003, Benjamin Herrenscmidt (benh@kernel.crashing.org)
 *
 */

#include <linux/config.h>

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/delay.h>

#include "ibm_emac_phy.h"

static int reset_one_mii_phy(struct mii_phy *phy, int phy_id)
{
	u16 val;
	int limit = 10000;

	val = __phy_read(phy, phy_id, MII_BMCR);
	val &= ~BMCR_ISOLATE;
	val |= BMCR_RESET;
	__phy_write(phy, phy_id, MII_BMCR, val);

	udelay(100);

	while (limit--) {
		val = __phy_read(phy, phy_id, MII_BMCR);
		if ((val & BMCR_RESET) == 0)
			break;
		udelay(10);
	}
	if ((val & BMCR_ISOLATE) && limit > 0)
		__phy_write(phy, phy_id, MII_BMCR, val & ~BMCR_ISOLATE);

	return (limit <= 0);
}

static int cis8201_init(struct mii_phy *phy)
{
	u16 epcr;

	epcr = phy_read(phy, MII_CIS8201_EPCR);
	epcr &= ~EPCR_MODE_MASK;

	switch (phy->mode) {
	case PHY_MODE_TBI:
		epcr |= EPCR_TBI_MODE;
		break;
	case PHY_MODE_RTBI:
		epcr |= EPCR_RTBI_MODE;
		break;
	case PHY_MODE_GMII:
		epcr |= EPCR_GMII_MODE;
		break;
	case PHY_MODE_RGMII:
	default:
		epcr |= EPCR_RGMII_MODE;
	}

	phy_write(phy, MII_CIS8201_EPCR, epcr);

	return 0;
}

static int genmii_setup_aneg(struct mii_phy *phy, u32 advertise)
{
	u16 ctl, adv;

	phy->autoneg = 1;
	phy->speed = SPEED_10;
	phy->duplex = DUPLEX_HALF;
	phy->pause = 0;
	phy->advertising = advertise;

	/* Setup standard advertise */
	adv = phy_read(phy, MII_ADVERTISE);
	adv &= ~(ADVERTISE_ALL | ADVERTISE_100BASE4);
	if (advertise & ADVERTISED_10baseT_Half)
		adv |= ADVERTISE_10HALF;
	if (advertise & ADVERTISED_10baseT_Full)
		adv |= ADVERTISE_10FULL;
	if (advertise & ADVERTISED_100baseT_Half)
		adv |= ADVERTISE_100HALF;
	if (advertise & ADVERTISED_100baseT_Full)
		adv |= ADVERTISE_100FULL;
	phy_write(phy, MII_ADVERTISE, adv);

	/* Start/Restart aneg */
	ctl = phy_read(phy, MII_BMCR);
	ctl |= (BMCR_ANENABLE | BMCR_ANRESTART);
	phy_write(phy, MII_BMCR, ctl);

	return 0;
}

static int genmii_setup_forced(struct mii_phy *phy, int speed, int fd)
{
	u16 ctl;

	phy->autoneg = 0;
	phy->speed = speed;
	phy->duplex = fd;
	phy->pause = 0;

	ctl = phy_read(phy, MII_BMCR);
	ctl &= ~(BMCR_FULLDPLX | BMCR_SPEED100 | BMCR_ANENABLE);

	/* First reset the PHY */
	phy_write(phy, MII_BMCR, ctl | BMCR_RESET);

	/* Select speed & duplex */
	switch (speed) {
	case SPEED_10:
		break;
	case SPEED_100:
		ctl |= BMCR_SPEED100;
		break;
	case SPEED_1000:
	default:
		return -EINVAL;
	}
	if (fd == DUPLEX_FULL)
		ctl |= BMCR_FULLDPLX;
	phy_write(phy, MII_BMCR, ctl);

	return 0;
}

static int genmii_poll_link(struct mii_phy *phy)
{
	u16 status;

	(void)phy_read(phy, MII_BMSR);
	status = phy_read(phy, MII_BMSR);
	if ((status & BMSR_LSTATUS) == 0)
		return 0;
	if (phy->autoneg && !(status & BMSR_ANEGCOMPLETE))
		return 0;
	return 1;
}

#define	MII_CIS8201_ACSR	0x1c
#define  ACSR_DUPLEX_STATUS	0x0020
#define  ACSR_SPEED_1000BASET	0x0010
#define  ACSR_SPEED_100BASET	0x0008

static int cis8201_read_link(struct mii_phy *phy)
{
	u16 acsr;

	if (phy->autoneg) {
		acsr = phy_read(phy, MII_CIS8201_ACSR);

		if (acsr & ACSR_DUPLEX_STATUS)
			phy->duplex = DUPLEX_FULL;
		else
			phy->duplex = DUPLEX_HALF;
		if (acsr & ACSR_SPEED_1000BASET) {
			phy->speed = SPEED_1000;
		} else if (acsr & ACSR_SPEED_100BASET)
			phy->speed = SPEED_100;
		else
			phy->speed = SPEED_10;
		phy->pause = 0;
	}
	/* On non-aneg, we assume what we put in BMCR is the speed,
	 * though magic-aneg shouldn't prevent this case from occurring
	 */

	return 0;
}

static int genmii_read_link(struct mii_phy *phy)
{
	u16 lpa;

	if (phy->autoneg) {
		lpa = phy_read(phy, MII_LPA) & phy_read(phy, MII_ADVERTISE);

		phy->speed = SPEED_10;
		phy->duplex = DUPLEX_HALF;
		phy->pause = 0;

		if (lpa & (LPA_100FULL | LPA_100HALF)) {
			phy->speed = SPEED_100;
			if (lpa & LPA_100FULL)
				phy->duplex = DUPLEX_FULL;
		} else if (lpa & LPA_10FULL)
			phy->duplex = DUPLEX_FULL;
	}
	/* On non-aneg, we assume what we put in BMCR is the speed,
	 * though magic-aneg shouldn't prevent this case from occurring
	 */

	return 0;
}

#define MII_BASIC_FEATURES	(SUPPORTED_10baseT_Half | SUPPORTED_10baseT_Full | \
				 SUPPORTED_100baseT_Half | SUPPORTED_100baseT_Full | \
				 SUPPORTED_Autoneg | SUPPORTED_TP | SUPPORTED_MII)
#define MII_GBIT_FEATURES	(MII_BASIC_FEATURES | \
				 SUPPORTED_1000baseT_Half | SUPPORTED_1000baseT_Full)

/* CIS8201 phy ops */
static struct mii_phy_ops cis8201_phy_ops = {
	init:cis8201_init,
	setup_aneg:genmii_setup_aneg,
	setup_forced:genmii_setup_forced,
	poll_link:genmii_poll_link,
	read_link:cis8201_read_link
};

/* Generic implementation for most 10/100 PHYs */
static struct mii_phy_ops generic_phy_ops = {
	setup_aneg:genmii_setup_aneg,
	setup_forced:genmii_setup_forced,
	poll_link:genmii_poll_link,
	read_link:genmii_read_link
};

static struct mii_phy_def cis8201_phy_def = {
	phy_id:0x000fc410,
	phy_id_mask:0x000ffff0,
	name:"CIS8201 Gigabit Ethernet",
	features:MII_GBIT_FEATURES,
	magic_aneg:0,
	ops:&cis8201_phy_ops
};

static struct mii_phy_def genmii_phy_def = {
	phy_id:0x00000000,
	phy_id_mask:0x00000000,
	name:"Generic MII",
	features:MII_BASIC_FEATURES,
	magic_aneg:0,
	ops:&generic_phy_ops
};

static struct mii_phy_def *mii_phy_table[] = {
	&cis8201_phy_def,
	&genmii_phy_def,
	NULL
};

int mii_phy_probe(struct mii_phy *phy, int mii_id)
{
	int rc;
	u32 id;
	struct mii_phy_def *def;
	int i;

	phy->autoneg = 0;
	phy->advertising = 0;
	phy->mii_id = mii_id;
	phy->speed = 0;
	phy->duplex = 0;
	phy->pause = 0;

	/* Take PHY out of isloate mode and reset it. */
	rc = reset_one_mii_phy(phy, mii_id);
	if (rc)
		return -ENODEV;

	/* Read ID and find matching entry */
	id = (phy_read(phy, MII_PHYSID1) << 16 | phy_read(phy, MII_PHYSID2))
	    & 0xfffffff0;
	for (i = 0; (def = mii_phy_table[i]) != NULL; i++)
		if ((id & def->phy_id_mask) == def->phy_id)
			break;
	/* Should never be NULL (we have a generic entry), but... */
	if (def == NULL)
		return -ENODEV;

	phy->def = def;

	/* Setup default advertising */
	phy->advertising = def->features;

	return 0;
}

MODULE_LICENSE("GPL");
