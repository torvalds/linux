/*
 * drivers/net/ibm_newemac/phy.c
 *
 * Driver for PowerPC 4xx on-chip ethernet controller, PHY support.
 * Borrowed from sungem_phy.c, though I only kept the generic MII
 * driver for now.
 *
 * This file should be shared with other drivers or eventually
 * merged as the "low level" part of miilib
 *
 * (c) 2003, Benjamin Herrenscmidt (benh@kernel.crashing.org)
 * (c) 2004-2005, Eugene Surovegin <ebs@ebshome.net>
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/delay.h>

#include "emac.h"
#include "phy.h"

static inline int phy_read(struct mii_phy *phy, int reg)
{
	return phy->mdio_read(phy->dev, phy->address, reg);
}

static inline void phy_write(struct mii_phy *phy, int reg, int val)
{
	phy->mdio_write(phy->dev, phy->address, reg, val);
}

int emac_mii_reset_phy(struct mii_phy *phy)
{
	int val;
	int limit = 10000;

	val = phy_read(phy, MII_BMCR);
	val &= ~(BMCR_ISOLATE | BMCR_ANENABLE);
	val |= BMCR_RESET;
	phy_write(phy, MII_BMCR, val);

	udelay(300);

	while (limit--) {
		val = phy_read(phy, MII_BMCR);
		if (val >= 0 && (val & BMCR_RESET) == 0)
			break;
		udelay(10);
	}
	if ((val & BMCR_ISOLATE) && limit > 0)
		phy_write(phy, MII_BMCR, val & ~BMCR_ISOLATE);

	return limit <= 0;
}

static int genmii_setup_aneg(struct mii_phy *phy, u32 advertise)
{
	int ctl, adv;

	phy->autoneg = AUTONEG_ENABLE;
	phy->speed = SPEED_10;
	phy->duplex = DUPLEX_HALF;
	phy->pause = phy->asym_pause = 0;
	phy->advertising = advertise;

	ctl = phy_read(phy, MII_BMCR);
	if (ctl < 0)
		return ctl;
	ctl &= ~(BMCR_FULLDPLX | BMCR_SPEED100 | BMCR_SPEED1000 | BMCR_ANENABLE);

	/* First clear the PHY */
	phy_write(phy, MII_BMCR, ctl);

	/* Setup standard advertise */
	adv = phy_read(phy, MII_ADVERTISE);
	if (adv < 0)
		return adv;
	adv &= ~(ADVERTISE_ALL | ADVERTISE_100BASE4 | ADVERTISE_PAUSE_CAP |
		 ADVERTISE_PAUSE_ASYM);
	if (advertise & ADVERTISED_10baseT_Half)
		adv |= ADVERTISE_10HALF;
	if (advertise & ADVERTISED_10baseT_Full)
		adv |= ADVERTISE_10FULL;
	if (advertise & ADVERTISED_100baseT_Half)
		adv |= ADVERTISE_100HALF;
	if (advertise & ADVERTISED_100baseT_Full)
		adv |= ADVERTISE_100FULL;
	if (advertise & ADVERTISED_Pause)
		adv |= ADVERTISE_PAUSE_CAP;
	if (advertise & ADVERTISED_Asym_Pause)
		adv |= ADVERTISE_PAUSE_ASYM;
	phy_write(phy, MII_ADVERTISE, adv);

	if (phy->features &
	    (SUPPORTED_1000baseT_Full | SUPPORTED_1000baseT_Half)) {
		adv = phy_read(phy, MII_CTRL1000);
		if (adv < 0)
			return adv;
		adv &= ~(ADVERTISE_1000FULL | ADVERTISE_1000HALF);
		if (advertise & ADVERTISED_1000baseT_Full)
			adv |= ADVERTISE_1000FULL;
		if (advertise & ADVERTISED_1000baseT_Half)
			adv |= ADVERTISE_1000HALF;
		phy_write(phy, MII_CTRL1000, adv);
	}

	/* Start/Restart aneg */
	ctl = phy_read(phy, MII_BMCR);
	ctl |= (BMCR_ANENABLE | BMCR_ANRESTART);
	phy_write(phy, MII_BMCR, ctl);

	return 0;
}

static int genmii_setup_forced(struct mii_phy *phy, int speed, int fd)
{
	int ctl;

	phy->autoneg = AUTONEG_DISABLE;
	phy->speed = speed;
	phy->duplex = fd;
	phy->pause = phy->asym_pause = 0;

	ctl = phy_read(phy, MII_BMCR);
	if (ctl < 0)
		return ctl;
	ctl &= ~(BMCR_FULLDPLX | BMCR_SPEED100 | BMCR_SPEED1000 | BMCR_ANENABLE);

	/* First clear the PHY */
	phy_write(phy, MII_BMCR, ctl | BMCR_RESET);

	/* Select speed & duplex */
	switch (speed) {
	case SPEED_10:
		break;
	case SPEED_100:
		ctl |= BMCR_SPEED100;
		break;
	case SPEED_1000:
		ctl |= BMCR_SPEED1000;
		break;
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
	int status;

	/* Clear latched value with dummy read */
	phy_read(phy, MII_BMSR);
	status = phy_read(phy, MII_BMSR);
	if (status < 0 || (status & BMSR_LSTATUS) == 0)
		return 0;
	if (phy->autoneg == AUTONEG_ENABLE && !(status & BMSR_ANEGCOMPLETE))
		return 0;
	return 1;
}

static int genmii_read_link(struct mii_phy *phy)
{
	if (phy->autoneg == AUTONEG_ENABLE) {
		int glpa = 0;
		int lpa = phy_read(phy, MII_LPA) & phy_read(phy, MII_ADVERTISE);
		if (lpa < 0)
			return lpa;

		if (phy->features &
		    (SUPPORTED_1000baseT_Full | SUPPORTED_1000baseT_Half)) {
			int adv = phy_read(phy, MII_CTRL1000);
			glpa = phy_read(phy, MII_STAT1000);

			if (glpa < 0 || adv < 0)
				return adv;

			glpa &= adv << 2;
		}

		phy->speed = SPEED_10;
		phy->duplex = DUPLEX_HALF;
		phy->pause = phy->asym_pause = 0;

		if (glpa & (LPA_1000FULL | LPA_1000HALF)) {
			phy->speed = SPEED_1000;
			if (glpa & LPA_1000FULL)
				phy->duplex = DUPLEX_FULL;
		} else if (lpa & (LPA_100FULL | LPA_100HALF)) {
			phy->speed = SPEED_100;
			if (lpa & LPA_100FULL)
				phy->duplex = DUPLEX_FULL;
		} else if (lpa & LPA_10FULL)
			phy->duplex = DUPLEX_FULL;

		if (phy->duplex == DUPLEX_FULL) {
			phy->pause = lpa & LPA_PAUSE_CAP ? 1 : 0;
			phy->asym_pause = lpa & LPA_PAUSE_ASYM ? 1 : 0;
		}
	} else {
		int bmcr = phy_read(phy, MII_BMCR);
		if (bmcr < 0)
			return bmcr;

		if (bmcr & BMCR_FULLDPLX)
			phy->duplex = DUPLEX_FULL;
		else
			phy->duplex = DUPLEX_HALF;
		if (bmcr & BMCR_SPEED1000)
			phy->speed = SPEED_1000;
		else if (bmcr & BMCR_SPEED100)
			phy->speed = SPEED_100;
		else
			phy->speed = SPEED_10;

		phy->pause = phy->asym_pause = 0;
	}
	return 0;
}

/* Generic implementation for most 10/100/1000 PHYs */
static struct mii_phy_ops generic_phy_ops = {
	.setup_aneg	= genmii_setup_aneg,
	.setup_forced	= genmii_setup_forced,
	.poll_link	= genmii_poll_link,
	.read_link	= genmii_read_link
};

static struct mii_phy_def genmii_phy_def = {
	.phy_id		= 0x00000000,
	.phy_id_mask	= 0x00000000,
	.name		= "Generic MII",
	.ops		= &generic_phy_ops
};

/* CIS8201 */
#define MII_CIS8201_10BTCSR	0x16
#define  TENBTCSR_ECHO_DISABLE	0x2000
#define MII_CIS8201_EPCR	0x17
#define  EPCR_MODE_MASK		0x3000
#define  EPCR_GMII_MODE		0x0000
#define  EPCR_RGMII_MODE	0x1000
#define  EPCR_TBI_MODE		0x2000
#define  EPCR_RTBI_MODE		0x3000
#define MII_CIS8201_ACSR	0x1c
#define  ACSR_PIN_PRIO_SELECT	0x0004

static int cis8201_init(struct mii_phy *phy)
{
	int epcr;

	epcr = phy_read(phy, MII_CIS8201_EPCR);
	if (epcr < 0)
		return epcr;

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

	/* MII regs override strap pins */
	phy_write(phy, MII_CIS8201_ACSR,
		  phy_read(phy, MII_CIS8201_ACSR) | ACSR_PIN_PRIO_SELECT);

	/* Disable TX_EN -> CRS echo mode, otherwise 10/HDX doesn't work */
	phy_write(phy, MII_CIS8201_10BTCSR,
		  phy_read(phy, MII_CIS8201_10BTCSR) | TENBTCSR_ECHO_DISABLE);

	return 0;
}

static struct mii_phy_ops cis8201_phy_ops = {
	.init		= cis8201_init,
	.setup_aneg	= genmii_setup_aneg,
	.setup_forced	= genmii_setup_forced,
	.poll_link	= genmii_poll_link,
	.read_link	= genmii_read_link
};

static struct mii_phy_def cis8201_phy_def = {
	.phy_id		= 0x000fc410,
	.phy_id_mask	= 0x000ffff0,
	.name		= "CIS8201 Gigabit Ethernet",
	.ops		= &cis8201_phy_ops
};

static struct mii_phy_def bcm5248_phy_def = {

	.phy_id		= 0x0143bc00,
	.phy_id_mask	= 0x0ffffff0,
	.name		= "BCM5248 10/100 SMII Ethernet",
	.ops		= &generic_phy_ops
};

static int m88e1111_init(struct mii_phy *phy)
{
	pr_debug("%s: Marvell 88E1111 Ethernet\n", __FUNCTION__);
	phy_write(phy, 0x14, 0x0ce3);
	phy_write(phy, 0x18, 0x4101);
	phy_write(phy, 0x09, 0x0e00);
	phy_write(phy, 0x04, 0x01e1);
	phy_write(phy, 0x00, 0x9140);
	phy_write(phy, 0x00, 0x1140);

	return  0;
}

static int et1011c_init(struct mii_phy *phy)
{
	u16 reg_short;

	reg_short = (u16)(phy_read(phy, 0x16));
	reg_short &= ~(0x7);
	reg_short |= 0x6;	/* RGMII Trace Delay*/
	phy_write(phy, 0x16, reg_short);

	reg_short = (u16)(phy_read(phy, 0x17));
	reg_short &= ~(0x40);
	phy_write(phy, 0x17, reg_short);

	phy_write(phy, 0x1c, 0x74f0);
	return 0;
}

static struct mii_phy_ops et1011c_phy_ops = {
	.init		= et1011c_init,
	.setup_aneg	= genmii_setup_aneg,
	.setup_forced	= genmii_setup_forced,
	.poll_link	= genmii_poll_link,
	.read_link	= genmii_read_link
};

static struct mii_phy_def et1011c_phy_def = {
	.phy_id		= 0x0282f000,
	.phy_id_mask	= 0x0fffff00,
	.name		= "ET1011C Gigabit Ethernet",
	.ops		= &et1011c_phy_ops
};





static struct mii_phy_ops m88e1111_phy_ops = {
	.init		= m88e1111_init,
	.setup_aneg	= genmii_setup_aneg,
	.setup_forced	= genmii_setup_forced,
	.poll_link	= genmii_poll_link,
	.read_link	= genmii_read_link
};

static struct mii_phy_def m88e1111_phy_def = {

	.phy_id		= 0x01410CC0,
	.phy_id_mask	= 0x0ffffff0,
	.name		= "Marvell 88E1111 Ethernet",
	.ops		= &m88e1111_phy_ops,
};

static struct mii_phy_def *mii_phy_table[] = {
	&et1011c_phy_def,
	&cis8201_phy_def,
	&bcm5248_phy_def,
	&m88e1111_phy_def,
	&genmii_phy_def,
	NULL
};

int emac_mii_phy_probe(struct mii_phy *phy, int address)
{
	struct mii_phy_def *def;
	int i;
	u32 id;

	phy->autoneg = AUTONEG_DISABLE;
	phy->advertising = 0;
	phy->address = address;
	phy->speed = SPEED_10;
	phy->duplex = DUPLEX_HALF;
	phy->pause = phy->asym_pause = 0;

	/* Take PHY out of isolate mode and reset it. */
	if (emac_mii_reset_phy(phy))
		return -ENODEV;

	/* Read ID and find matching entry */
	id = (phy_read(phy, MII_PHYSID1) << 16) | phy_read(phy, MII_PHYSID2);
	for (i = 0; (def = mii_phy_table[i]) != NULL; i++)
		if ((id & def->phy_id_mask) == def->phy_id)
			break;
	/* Should never be NULL (we have a generic entry), but... */
	if (!def)
		return -ENODEV;

	phy->def = def;

	/* Determine PHY features if needed */
	phy->features = def->features;
	if (!phy->features) {
		u16 bmsr = phy_read(phy, MII_BMSR);
		if (bmsr & BMSR_ANEGCAPABLE)
			phy->features |= SUPPORTED_Autoneg;
		if (bmsr & BMSR_10HALF)
			phy->features |= SUPPORTED_10baseT_Half;
		if (bmsr & BMSR_10FULL)
			phy->features |= SUPPORTED_10baseT_Full;
		if (bmsr & BMSR_100HALF)
			phy->features |= SUPPORTED_100baseT_Half;
		if (bmsr & BMSR_100FULL)
			phy->features |= SUPPORTED_100baseT_Full;
		if (bmsr & BMSR_ESTATEN) {
			u16 esr = phy_read(phy, MII_ESTATUS);
			if (esr & ESTATUS_1000_TFULL)
				phy->features |= SUPPORTED_1000baseT_Full;
			if (esr & ESTATUS_1000_THALF)
				phy->features |= SUPPORTED_1000baseT_Half;
		}
		phy->features |= SUPPORTED_MII;
	}

	/* Setup default advertising */
	phy->advertising = phy->features;

	return 0;
}

MODULE_LICENSE("GPL");
