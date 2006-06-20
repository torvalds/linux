/*
 * PHY drivers for the sungem ethernet driver.
 * 
 * This file could be shared with other drivers.
 * 
 * (c) 2002, Benjamin Herrenscmidt (benh@kernel.crashing.org)
 *
 * TODO:
 *  - Implement WOL
 *  - Add support for PHYs that provide an IRQ line
 *  - Eventually moved the entire polling state machine in
 *    there (out of the eth driver), so that it can easily be
 *    skipped on PHYs that implement it in hardware.
 *  - On LXT971 & BCM5201, Apple uses some chip specific regs
 *    to read the link status. Figure out why and if it makes
 *    sense to do the same (magic aneg ?)
 *  - Apple has some additional power management code for some
 *    Broadcom PHYs that they "hide" from the OpenSource version
 *    of darwin, still need to reverse engineer that
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

#ifdef CONFIG_PPC_PMAC
#include <asm/prom.h>
#endif

#include "sungem_phy.h"

/* Link modes of the BCM5400 PHY */
static const int phy_BCM5400_link_table[8][3] = {
	{ 0, 0, 0 },	/* No link */
	{ 0, 0, 0 },	/* 10BT Half Duplex */
	{ 1, 0, 0 },	/* 10BT Full Duplex */
	{ 0, 1, 0 },	/* 100BT Half Duplex */
	{ 0, 1, 0 },	/* 100BT Half Duplex */
	{ 1, 1, 0 },	/* 100BT Full Duplex*/
	{ 1, 0, 1 },	/* 1000BT */
	{ 1, 0, 1 },	/* 1000BT */
};

static inline int __phy_read(struct mii_phy* phy, int id, int reg)
{
	return phy->mdio_read(phy->dev, id, reg);
}

static inline void __phy_write(struct mii_phy* phy, int id, int reg, int val)
{
	phy->mdio_write(phy->dev, id, reg, val);
}

static inline int phy_read(struct mii_phy* phy, int reg)
{
	return phy->mdio_read(phy->dev, phy->mii_id, reg);
}

static inline void phy_write(struct mii_phy* phy, int reg, int val)
{
	phy->mdio_write(phy->dev, phy->mii_id, reg, val);
}

static int reset_one_mii_phy(struct mii_phy* phy, int phy_id)
{
	u16 val;
	int limit = 10000;
	
	val = __phy_read(phy, phy_id, MII_BMCR);
	val &= ~(BMCR_ISOLATE | BMCR_PDOWN);
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

static int bcm5201_init(struct mii_phy* phy)
{
	u16 data;

	data = phy_read(phy, MII_BCM5201_MULTIPHY);
	data &= ~MII_BCM5201_MULTIPHY_SUPERISOLATE;
	phy_write(phy, MII_BCM5201_MULTIPHY, data);

	phy_write(phy, MII_BCM5201_INTERRUPT, 0);

	return 0;
}

static int bcm5201_suspend(struct mii_phy* phy)
{
	phy_write(phy, MII_BCM5201_INTERRUPT, 0);
	phy_write(phy, MII_BCM5201_MULTIPHY, MII_BCM5201_MULTIPHY_SUPERISOLATE);

	return 0;
}

static int bcm5221_init(struct mii_phy* phy)
{
	u16 data;

	data = phy_read(phy, MII_BCM5221_TEST);
	phy_write(phy, MII_BCM5221_TEST,
		data | MII_BCM5221_TEST_ENABLE_SHADOWS);

	data = phy_read(phy, MII_BCM5221_SHDOW_AUX_STAT2);
	phy_write(phy, MII_BCM5221_SHDOW_AUX_STAT2,
		data | MII_BCM5221_SHDOW_AUX_STAT2_APD);

	data = phy_read(phy, MII_BCM5221_SHDOW_AUX_MODE4);
	phy_write(phy, MII_BCM5221_SHDOW_AUX_MODE4,
		data | MII_BCM5221_SHDOW_AUX_MODE4_CLKLOPWR);

	data = phy_read(phy, MII_BCM5221_TEST);
	phy_write(phy, MII_BCM5221_TEST,
		data & ~MII_BCM5221_TEST_ENABLE_SHADOWS);

	return 0;
}

static int bcm5221_suspend(struct mii_phy* phy)
{
	u16 data;

	data = phy_read(phy, MII_BCM5221_TEST);
	phy_write(phy, MII_BCM5221_TEST,
		data | MII_BCM5221_TEST_ENABLE_SHADOWS);

	data = phy_read(phy, MII_BCM5221_SHDOW_AUX_MODE4);
	phy_write(phy, MII_BCM5221_SHDOW_AUX_MODE4,
		  data | MII_BCM5221_SHDOW_AUX_MODE4_IDDQMODE);

	return 0;
}

static int bcm5400_init(struct mii_phy* phy)
{
	u16 data;

	/* Configure for gigabit full duplex */
	data = phy_read(phy, MII_BCM5400_AUXCONTROL);
	data |= MII_BCM5400_AUXCONTROL_PWR10BASET;
	phy_write(phy, MII_BCM5400_AUXCONTROL, data);
	
	data = phy_read(phy, MII_BCM5400_GB_CONTROL);
	data |= MII_BCM5400_GB_CONTROL_FULLDUPLEXCAP;
	phy_write(phy, MII_BCM5400_GB_CONTROL, data);
	
	udelay(100);

	/* Reset and configure cascaded 10/100 PHY */
	(void)reset_one_mii_phy(phy, 0x1f);
	
	data = __phy_read(phy, 0x1f, MII_BCM5201_MULTIPHY);
	data |= MII_BCM5201_MULTIPHY_SERIALMODE;
	__phy_write(phy, 0x1f, MII_BCM5201_MULTIPHY, data);

	data = phy_read(phy, MII_BCM5400_AUXCONTROL);
	data &= ~MII_BCM5400_AUXCONTROL_PWR10BASET;
	phy_write(phy, MII_BCM5400_AUXCONTROL, data);

	return 0;
}

static int bcm5400_suspend(struct mii_phy* phy)
{
#if 0 /* Commented out in Darwin... someone has those dawn docs ? */
	phy_write(phy, MII_BMCR, BMCR_PDOWN);
#endif
	return 0;
}

static int bcm5401_init(struct mii_phy* phy)
{
	u16 data;
	int rev;

	rev = phy_read(phy, MII_PHYSID2) & 0x000f;
	if (rev == 0 || rev == 3) {
		/* Some revisions of 5401 appear to need this
		 * initialisation sequence to disable, according
		 * to OF, "tap power management"
		 * 
		 * WARNING ! OF and Darwin don't agree on the
		 * register addresses. OF seem to interpret the
		 * register numbers below as decimal
		 *
		 * Note: This should (and does) match tg3_init_5401phy_dsp
		 *       in the tg3.c driver. -DaveM
		 */
		phy_write(phy, 0x18, 0x0c20);
		phy_write(phy, 0x17, 0x0012);
		phy_write(phy, 0x15, 0x1804);
		phy_write(phy, 0x17, 0x0013);
		phy_write(phy, 0x15, 0x1204);
		phy_write(phy, 0x17, 0x8006);
		phy_write(phy, 0x15, 0x0132);
		phy_write(phy, 0x17, 0x8006);
		phy_write(phy, 0x15, 0x0232);
		phy_write(phy, 0x17, 0x201f);
		phy_write(phy, 0x15, 0x0a20);
	}
	
	/* Configure for gigabit full duplex */
	data = phy_read(phy, MII_BCM5400_GB_CONTROL);
	data |= MII_BCM5400_GB_CONTROL_FULLDUPLEXCAP;
	phy_write(phy, MII_BCM5400_GB_CONTROL, data);

	udelay(10);

	/* Reset and configure cascaded 10/100 PHY */
	(void)reset_one_mii_phy(phy, 0x1f);
	
	data = __phy_read(phy, 0x1f, MII_BCM5201_MULTIPHY);
	data |= MII_BCM5201_MULTIPHY_SERIALMODE;
	__phy_write(phy, 0x1f, MII_BCM5201_MULTIPHY, data);

	return 0;
}

static int bcm5401_suspend(struct mii_phy* phy)
{
#if 0 /* Commented out in Darwin... someone has those dawn docs ? */
	phy_write(phy, MII_BMCR, BMCR_PDOWN);
#endif
	return 0;
}

static int bcm5411_init(struct mii_phy* phy)
{
	u16 data;

	/* Here's some more Apple black magic to setup
	 * some voltage stuffs.
	 */
	phy_write(phy, 0x1c, 0x8c23);
	phy_write(phy, 0x1c, 0x8ca3);
	phy_write(phy, 0x1c, 0x8c23);

	/* Here, Apple seems to want to reset it, do
	 * it as well
	 */
	phy_write(phy, MII_BMCR, BMCR_RESET);
	phy_write(phy, MII_BMCR, 0x1340);

	data = phy_read(phy, MII_BCM5400_GB_CONTROL);
	data |= MII_BCM5400_GB_CONTROL_FULLDUPLEXCAP;
	phy_write(phy, MII_BCM5400_GB_CONTROL, data);

	udelay(10);

	/* Reset and configure cascaded 10/100 PHY */
	(void)reset_one_mii_phy(phy, 0x1f);
	
	return 0;
}

static int generic_suspend(struct mii_phy* phy)
{
	phy_write(phy, MII_BMCR, BMCR_PDOWN);

	return 0;
}

static int bcm5421_init(struct mii_phy* phy)
{
	u16 data;
	unsigned int id;

	id = (phy_read(phy, MII_PHYSID1) << 16 | phy_read(phy, MII_PHYSID2));

	/* Revision 0 of 5421 needs some fixups */
	if (id == 0x002060e0) {
		/* This is borrowed from MacOS
		 */
		phy_write(phy, 0x18, 0x1007);
		data = phy_read(phy, 0x18);
		phy_write(phy, 0x18, data | 0x0400);
		phy_write(phy, 0x18, 0x0007);
		data = phy_read(phy, 0x18);
		phy_write(phy, 0x18, data | 0x0800);
		phy_write(phy, 0x17, 0x000a);
		data = phy_read(phy, 0x15);
		phy_write(phy, 0x15, data | 0x0200);
	}

	/* Pick up some init code from OF for K2 version */
	if ((id & 0xfffffff0) == 0x002062e0) {
		phy_write(phy, 4, 0x01e1);
		phy_write(phy, 9, 0x0300);
	}

	/* Check if we can enable automatic low power */
#ifdef CONFIG_PPC_PMAC
	if (phy->platform_data) {
		struct device_node *np = of_get_parent(phy->platform_data);
		int can_low_power = 1;
		if (np == NULL || get_property(np, "no-autolowpower", NULL))
			can_low_power = 0;
		if (can_low_power) {
			/* Enable automatic low-power */
			phy_write(phy, 0x1c, 0x9002);
			phy_write(phy, 0x1c, 0xa821);
			phy_write(phy, 0x1c, 0x941d);
		}
	}
#endif /* CONFIG_PPC_PMAC */

	return 0;
}

static int bcm5421_enable_fiber(struct mii_phy* phy)
{
	/* enable fiber mode */
	phy_write(phy, MII_NCONFIG, 0x9020);
	/* LEDs active in both modes, autosense prio = fiber */
	phy_write(phy, MII_NCONFIG, 0x945f);

	/* switch off fibre autoneg */
	phy_write(phy, MII_NCONFIG, 0xfc01);
	phy_write(phy, 0x0b, 0x0004);

	return 0;
}

static int bcm5461_enable_fiber(struct mii_phy* phy)
{
	phy_write(phy, MII_NCONFIG, 0xfc0c);
	phy_write(phy, MII_BMCR, 0x4140);
	phy_write(phy, MII_NCONFIG, 0xfc0b);
	phy_write(phy, MII_BMCR, 0x0140);

	return 0;
}

static int bcm54xx_setup_aneg(struct mii_phy *phy, u32 advertise)
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

	/* Setup 1000BT advertise */
	adv = phy_read(phy, MII_1000BASETCONTROL);
	adv &= ~(MII_1000BASETCONTROL_FULLDUPLEXCAP|MII_1000BASETCONTROL_HALFDUPLEXCAP);
	if (advertise & SUPPORTED_1000baseT_Half)
		adv |= MII_1000BASETCONTROL_HALFDUPLEXCAP;
	if (advertise & SUPPORTED_1000baseT_Full)
		adv |= MII_1000BASETCONTROL_FULLDUPLEXCAP;
	phy_write(phy, MII_1000BASETCONTROL, adv);

	/* Start/Restart aneg */
	ctl = phy_read(phy, MII_BMCR);
	ctl |= (BMCR_ANENABLE | BMCR_ANRESTART);
	phy_write(phy, MII_BMCR, ctl);

	return 0;
}

static int bcm54xx_setup_forced(struct mii_phy *phy, int speed, int fd)
{
	u16 ctl;
	
	phy->autoneg = 0;
	phy->speed = speed;
	phy->duplex = fd;
	phy->pause = 0;

	ctl = phy_read(phy, MII_BMCR);
	ctl &= ~(BMCR_FULLDPLX|BMCR_SPEED100|BMCR_SPD2|BMCR_ANENABLE);

	/* First reset the PHY */
	phy_write(phy, MII_BMCR, ctl | BMCR_RESET);

	/* Select speed & duplex */
	switch(speed) {
	case SPEED_10:
		break;
	case SPEED_100:
		ctl |= BMCR_SPEED100;
		break;
	case SPEED_1000:
		ctl |= BMCR_SPD2;
	}
	if (fd == DUPLEX_FULL)
		ctl |= BMCR_FULLDPLX;

	// XXX Should we set the sungem to GII now on 1000BT ?
	
	phy_write(phy, MII_BMCR, ctl);

	return 0;
}

static int bcm54xx_read_link(struct mii_phy *phy)
{
	int link_mode;	
	u16 val;
	
	if (phy->autoneg) {
	    	val = phy_read(phy, MII_BCM5400_AUXSTATUS);
		link_mode = ((val & MII_BCM5400_AUXSTATUS_LINKMODE_MASK) >>
			     MII_BCM5400_AUXSTATUS_LINKMODE_SHIFT);
		phy->duplex = phy_BCM5400_link_table[link_mode][0] ? DUPLEX_FULL : DUPLEX_HALF;
		phy->speed = phy_BCM5400_link_table[link_mode][2] ?
				SPEED_1000 :
				(phy_BCM5400_link_table[link_mode][1] ? SPEED_100 : SPEED_10);
		val = phy_read(phy, MII_LPA);
		phy->pause = ((val & LPA_PAUSE) != 0);
	}
	/* On non-aneg, we assume what we put in BMCR is the speed,
	 * though magic-aneg shouldn't prevent this case from occurring
	 */

	return 0;
}

static int marvell_setup_aneg(struct mii_phy *phy, u32 advertise)
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

	/* Setup 1000BT advertise & enable crossover detect
	 * XXX How do we advertise 1000BT ? Darwin source is
	 * confusing here, they read from specific control and
	 * write to control... Someone has specs for those
	 * beasts ?
	 */
	adv = phy_read(phy, MII_M1011_PHY_SPEC_CONTROL);
	adv |= MII_M1011_PHY_SPEC_CONTROL_AUTO_MDIX;
	adv &= ~(MII_1000BASETCONTROL_FULLDUPLEXCAP |
			MII_1000BASETCONTROL_HALFDUPLEXCAP);
	if (advertise & SUPPORTED_1000baseT_Half)
		adv |= MII_1000BASETCONTROL_HALFDUPLEXCAP;
	if (advertise & SUPPORTED_1000baseT_Full)
		adv |= MII_1000BASETCONTROL_FULLDUPLEXCAP;
	phy_write(phy, MII_1000BASETCONTROL, adv);

	/* Start/Restart aneg */
	ctl = phy_read(phy, MII_BMCR);
	ctl |= (BMCR_ANENABLE | BMCR_ANRESTART);
	phy_write(phy, MII_BMCR, ctl);

	return 0;
}

static int marvell_setup_forced(struct mii_phy *phy, int speed, int fd)
{
	u16 ctl, ctl2;
	
	phy->autoneg = 0;
	phy->speed = speed;
	phy->duplex = fd;
	phy->pause = 0;

	ctl = phy_read(phy, MII_BMCR);
	ctl &= ~(BMCR_FULLDPLX|BMCR_SPEED100|BMCR_SPD2|BMCR_ANENABLE);
	ctl |= BMCR_RESET;

	/* Select speed & duplex */
	switch(speed) {
	case SPEED_10:
		break;
	case SPEED_100:
		ctl |= BMCR_SPEED100;
		break;
	/* I'm not sure about the one below, again, Darwin source is
	 * quite confusing and I lack chip specs
	 */
	case SPEED_1000:
		ctl |= BMCR_SPD2;
	}
	if (fd == DUPLEX_FULL)
		ctl |= BMCR_FULLDPLX;

	/* Disable crossover. Again, the way Apple does it is strange,
	 * though I don't assume they are wrong ;)
	 */
	ctl2 = phy_read(phy, MII_M1011_PHY_SPEC_CONTROL);
	ctl2 &= ~(MII_M1011_PHY_SPEC_CONTROL_MANUAL_MDIX |
		MII_M1011_PHY_SPEC_CONTROL_AUTO_MDIX |
		MII_1000BASETCONTROL_FULLDUPLEXCAP |
		MII_1000BASETCONTROL_HALFDUPLEXCAP);
	if (speed == SPEED_1000)
		ctl2 |= (fd == DUPLEX_FULL) ?
			MII_1000BASETCONTROL_FULLDUPLEXCAP :
			MII_1000BASETCONTROL_HALFDUPLEXCAP;
	phy_write(phy, MII_1000BASETCONTROL, ctl2);

	// XXX Should we set the sungem to GII now on 1000BT ?
	
	phy_write(phy, MII_BMCR, ctl);

	return 0;
}

static int marvell_read_link(struct mii_phy *phy)
{
	u16 status;

	if (phy->autoneg) {
		status = phy_read(phy, MII_M1011_PHY_SPEC_STATUS);
		if ((status & MII_M1011_PHY_SPEC_STATUS_RESOLVED) == 0)
			return -EAGAIN;
		if (status & MII_M1011_PHY_SPEC_STATUS_1000)
			phy->speed = SPEED_1000;
		else if (status & MII_M1011_PHY_SPEC_STATUS_100)
			phy->speed = SPEED_100;
		else
			phy->speed = SPEED_10;
		if (status & MII_M1011_PHY_SPEC_STATUS_FULLDUPLEX)
			phy->duplex = DUPLEX_FULL;
		else
			phy->duplex = DUPLEX_HALF;
		phy->pause = 0; /* XXX Check against spec ! */
	}
	/* On non-aneg, we assume what we put in BMCR is the speed,
	 * though magic-aneg shouldn't prevent this case from occurring
	 */

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
	ctl &= ~(BMCR_FULLDPLX|BMCR_SPEED100|BMCR_ANENABLE);

	/* First reset the PHY */
	phy_write(phy, MII_BMCR, ctl | BMCR_RESET);

	/* Select speed & duplex */
	switch(speed) {
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

static int genmii_read_link(struct mii_phy *phy)
{
	u16 lpa;

	if (phy->autoneg) {
		lpa = phy_read(phy, MII_LPA);

		if (lpa & (LPA_10FULL | LPA_100FULL))
			phy->duplex = DUPLEX_FULL;
		else
			phy->duplex = DUPLEX_HALF;
		if (lpa & (LPA_100FULL | LPA_100HALF))
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


#define MII_BASIC_FEATURES	(SUPPORTED_10baseT_Half | SUPPORTED_10baseT_Full | \
				 SUPPORTED_100baseT_Half | SUPPORTED_100baseT_Full | \
				 SUPPORTED_Autoneg | SUPPORTED_TP | SUPPORTED_MII)
#define MII_GBIT_FEATURES	(MII_BASIC_FEATURES | \
				 SUPPORTED_1000baseT_Half | SUPPORTED_1000baseT_Full)

/* Broadcom BCM 5201 */
static struct mii_phy_ops bcm5201_phy_ops = {
	.init		= bcm5201_init,
	.suspend	= bcm5201_suspend,
	.setup_aneg	= genmii_setup_aneg,
	.setup_forced	= genmii_setup_forced,
	.poll_link	= genmii_poll_link,
	.read_link	= genmii_read_link,
};

static struct mii_phy_def bcm5201_phy_def = {
	.phy_id		= 0x00406210,
	.phy_id_mask	= 0xfffffff0,
	.name		= "BCM5201",
	.features	= MII_BASIC_FEATURES,
	.magic_aneg	= 1,
	.ops		= &bcm5201_phy_ops
};

/* Broadcom BCM 5221 */
static struct mii_phy_ops bcm5221_phy_ops = {
	.suspend	= bcm5221_suspend,
	.init		= bcm5221_init,
	.setup_aneg	= genmii_setup_aneg,
	.setup_forced	= genmii_setup_forced,
	.poll_link	= genmii_poll_link,
	.read_link	= genmii_read_link,
};

static struct mii_phy_def bcm5221_phy_def = {
	.phy_id		= 0x004061e0,
	.phy_id_mask	= 0xfffffff0,
	.name		= "BCM5221",
	.features	= MII_BASIC_FEATURES,
	.magic_aneg	= 1,
	.ops		= &bcm5221_phy_ops
};

/* Broadcom BCM 5400 */
static struct mii_phy_ops bcm5400_phy_ops = {
	.init		= bcm5400_init,
	.suspend	= bcm5400_suspend,
	.setup_aneg	= bcm54xx_setup_aneg,
	.setup_forced	= bcm54xx_setup_forced,
	.poll_link	= genmii_poll_link,
	.read_link	= bcm54xx_read_link,
};

static struct mii_phy_def bcm5400_phy_def = {
	.phy_id		= 0x00206040,
	.phy_id_mask	= 0xfffffff0,
	.name		= "BCM5400",
	.features	= MII_GBIT_FEATURES,
	.magic_aneg	= 1,
	.ops		= &bcm5400_phy_ops
};

/* Broadcom BCM 5401 */
static struct mii_phy_ops bcm5401_phy_ops = {
	.init		= bcm5401_init,
	.suspend	= bcm5401_suspend,
	.setup_aneg	= bcm54xx_setup_aneg,
	.setup_forced	= bcm54xx_setup_forced,
	.poll_link	= genmii_poll_link,
	.read_link	= bcm54xx_read_link,
};

static struct mii_phy_def bcm5401_phy_def = {
	.phy_id		= 0x00206050,
	.phy_id_mask	= 0xfffffff0,
	.name		= "BCM5401",
	.features	= MII_GBIT_FEATURES,
	.magic_aneg	= 1,
	.ops		= &bcm5401_phy_ops
};

/* Broadcom BCM 5411 */
static struct mii_phy_ops bcm5411_phy_ops = {
	.init		= bcm5411_init,
	.suspend	= generic_suspend,
	.setup_aneg	= bcm54xx_setup_aneg,
	.setup_forced	= bcm54xx_setup_forced,
	.poll_link	= genmii_poll_link,
	.read_link	= bcm54xx_read_link,
};

static struct mii_phy_def bcm5411_phy_def = {
	.phy_id		= 0x00206070,
	.phy_id_mask	= 0xfffffff0,
	.name		= "BCM5411",
	.features	= MII_GBIT_FEATURES,
	.magic_aneg	= 1,
	.ops		= &bcm5411_phy_ops
};

/* Broadcom BCM 5421 */
static struct mii_phy_ops bcm5421_phy_ops = {
	.init		= bcm5421_init,
	.suspend	= generic_suspend,
	.setup_aneg	= bcm54xx_setup_aneg,
	.setup_forced	= bcm54xx_setup_forced,
	.poll_link	= genmii_poll_link,
	.read_link	= bcm54xx_read_link,
	.enable_fiber   = bcm5421_enable_fiber,
};

static struct mii_phy_def bcm5421_phy_def = {
	.phy_id		= 0x002060e0,
	.phy_id_mask	= 0xfffffff0,
	.name		= "BCM5421",
	.features	= MII_GBIT_FEATURES,
	.magic_aneg	= 1,
	.ops		= &bcm5421_phy_ops
};

/* Broadcom BCM 5421 built-in K2 */
static struct mii_phy_ops bcm5421k2_phy_ops = {
	.init		= bcm5421_init,
	.suspend	= generic_suspend,
	.setup_aneg	= bcm54xx_setup_aneg,
	.setup_forced	= bcm54xx_setup_forced,
	.poll_link	= genmii_poll_link,
	.read_link	= bcm54xx_read_link,
};

static struct mii_phy_def bcm5421k2_phy_def = {
	.phy_id		= 0x002062e0,
	.phy_id_mask	= 0xfffffff0,
	.name		= "BCM5421-K2",
	.features	= MII_GBIT_FEATURES,
	.magic_aneg	= 1,
	.ops		= &bcm5421k2_phy_ops
};

static struct mii_phy_ops bcm5461_phy_ops = {
	.init		= bcm5421_init,
	.suspend	= generic_suspend,
	.setup_aneg	= bcm54xx_setup_aneg,
	.setup_forced	= bcm54xx_setup_forced,
	.poll_link	= genmii_poll_link,
	.read_link	= bcm54xx_read_link,
	.enable_fiber   = bcm5461_enable_fiber,
};

static struct mii_phy_def bcm5461_phy_def = {
	.phy_id		= 0x002060c0,
	.phy_id_mask	= 0xfffffff0,
	.name		= "BCM5461",
	.features	= MII_GBIT_FEATURES,
	.magic_aneg	= 1,
	.ops		= &bcm5461_phy_ops
};

/* Broadcom BCM 5462 built-in Vesta */
static struct mii_phy_ops bcm5462V_phy_ops = {
	.init		= bcm5421_init,
	.suspend	= generic_suspend,
	.setup_aneg	= bcm54xx_setup_aneg,
	.setup_forced	= bcm54xx_setup_forced,
	.poll_link	= genmii_poll_link,
	.read_link	= bcm54xx_read_link,
};

static struct mii_phy_def bcm5462V_phy_def = {
	.phy_id		= 0x002060d0,
	.phy_id_mask	= 0xfffffff0,
	.name		= "BCM5462-Vesta",
	.features	= MII_GBIT_FEATURES,
	.magic_aneg	= 1,
	.ops		= &bcm5462V_phy_ops
};

/* Marvell 88E1101 (Apple seem to deal with 2 different revs,
 * I masked out the 8 last bits to get both, but some specs
 * would be useful here) --BenH.
 */
static struct mii_phy_ops marvell_phy_ops = {
	.suspend	= generic_suspend,
	.setup_aneg	= marvell_setup_aneg,
	.setup_forced	= marvell_setup_forced,
	.poll_link	= genmii_poll_link,
	.read_link	= marvell_read_link
};

static struct mii_phy_def marvell_phy_def = {
	.phy_id		= 0x01410c00,
	.phy_id_mask	= 0xffffff00,
	.name		= "Marvell 88E1101",
	.features	= MII_GBIT_FEATURES,
	.magic_aneg	= 1,
	.ops		= &marvell_phy_ops
};

/* Generic implementation for most 10/100 PHYs */
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
	.features	= MII_BASIC_FEATURES,
	.magic_aneg	= 0,
	.ops		= &generic_phy_ops
};

static struct mii_phy_def* mii_phy_table[] = {
	&bcm5201_phy_def,
	&bcm5221_phy_def,
	&bcm5400_phy_def,
	&bcm5401_phy_def,
	&bcm5411_phy_def,
	&bcm5421_phy_def,
	&bcm5421k2_phy_def,
	&bcm5461_phy_def,
	&bcm5462V_phy_def,
	&marvell_phy_def,
	&genmii_phy_def,
	NULL
};

int mii_phy_probe(struct mii_phy *phy, int mii_id)
{
	int rc;
	u32 id;
	struct mii_phy_def* def;
	int i;

	/* We do not reset the mii_phy structure as the driver
	 * may re-probe the PHY regulary
	 */
	phy->mii_id = mii_id;
	
	/* Take PHY out of isloate mode and reset it. */
	rc = reset_one_mii_phy(phy, mii_id);
	if (rc)
		goto fail;

	/* Read ID and find matching entry */	
	id = (phy_read(phy, MII_PHYSID1) << 16 | phy_read(phy, MII_PHYSID2));
	printk(KERN_DEBUG "PHY ID: %x, addr: %x\n", id, mii_id);
	for (i=0; (def = mii_phy_table[i]) != NULL; i++)
		if ((id & def->phy_id_mask) == def->phy_id)
			break;
	/* Should never be NULL (we have a generic entry), but... */
	if (def == NULL)
		goto fail;

	phy->def = def;
	
	return 0;
fail:
	phy->speed = 0;
	phy->duplex = 0;
	phy->pause = 0;
	phy->advertising = 0;
	return -ENODEV;
}

EXPORT_SYMBOL(mii_phy_probe);
MODULE_LICENSE("GPL");

