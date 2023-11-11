// SPDX-License-Identifier: GPL-2.0
/* $Date: 2005/10/24 23:18:13 $ $RCSfile: mv88e1xxx.c,v $ $Revision: 1.49 $ */
#include "common.h"
#include "mv88e1xxx.h"
#include "cphy.h"
#include "elmer0.h"

/* MV88E1XXX MDI crossover register values */
#define CROSSOVER_MDI   0
#define CROSSOVER_MDIX  1
#define CROSSOVER_AUTO  3

#define INTR_ENABLE_MASK 0x6CA0

/*
 * Set the bits given by 'bitval' in PHY register 'reg'.
 */
static void mdio_set_bit(struct cphy *cphy, int reg, u32 bitval)
{
	u32 val;

	(void) simple_mdio_read(cphy, reg, &val);
	(void) simple_mdio_write(cphy, reg, val | bitval);
}

/*
 * Clear the bits given by 'bitval' in PHY register 'reg'.
 */
static void mdio_clear_bit(struct cphy *cphy, int reg, u32 bitval)
{
	u32 val;

	(void) simple_mdio_read(cphy, reg, &val);
	(void) simple_mdio_write(cphy, reg, val & ~bitval);
}

/*
 * NAME:   phy_reset
 *
 * DESC:   Reset the given PHY's port. NOTE: This is not a global
 *         chip reset.
 *
 * PARAMS: cphy     - Pointer to PHY instance data.
 *
 * RETURN:  0 - Successful reset.
 *         -1 - Timeout.
 */
static int mv88e1xxx_reset(struct cphy *cphy, int wait)
{
	u32 ctl;
	int time_out = 1000;

	mdio_set_bit(cphy, MII_BMCR, BMCR_RESET);

	do {
		(void) simple_mdio_read(cphy, MII_BMCR, &ctl);
		ctl &= BMCR_RESET;
		if (ctl)
			udelay(1);
	} while (ctl && --time_out);

	return ctl ? -1 : 0;
}

static int mv88e1xxx_interrupt_enable(struct cphy *cphy)
{
	/* Enable PHY interrupts. */
	(void) simple_mdio_write(cphy, MV88E1XXX_INTERRUPT_ENABLE_REGISTER,
		   INTR_ENABLE_MASK);

	/* Enable Marvell interrupts through Elmer0. */
	if (t1_is_asic(cphy->adapter)) {
		u32 elmer;

		t1_tpi_read(cphy->adapter, A_ELMER0_INT_ENABLE, &elmer);
		elmer |= ELMER0_GP_BIT1;
		if (is_T2(cphy->adapter))
		    elmer |= ELMER0_GP_BIT2 | ELMER0_GP_BIT3 | ELMER0_GP_BIT4;
		t1_tpi_write(cphy->adapter, A_ELMER0_INT_ENABLE, elmer);
	}
	return 0;
}

static int mv88e1xxx_interrupt_disable(struct cphy *cphy)
{
	/* Disable all phy interrupts. */
	(void) simple_mdio_write(cphy, MV88E1XXX_INTERRUPT_ENABLE_REGISTER, 0);

	/* Disable Marvell interrupts through Elmer0. */
	if (t1_is_asic(cphy->adapter)) {
		u32 elmer;

		t1_tpi_read(cphy->adapter, A_ELMER0_INT_ENABLE, &elmer);
		elmer &= ~ELMER0_GP_BIT1;
		if (is_T2(cphy->adapter))
		    elmer &= ~(ELMER0_GP_BIT2|ELMER0_GP_BIT3|ELMER0_GP_BIT4);
		t1_tpi_write(cphy->adapter, A_ELMER0_INT_ENABLE, elmer);
	}
	return 0;
}

static int mv88e1xxx_interrupt_clear(struct cphy *cphy)
{
	u32 elmer;

	/* Clear PHY interrupts by reading the register. */
	(void) simple_mdio_read(cphy,
			MV88E1XXX_INTERRUPT_STATUS_REGISTER, &elmer);

	/* Clear Marvell interrupts through Elmer0. */
	if (t1_is_asic(cphy->adapter)) {
		t1_tpi_read(cphy->adapter, A_ELMER0_INT_CAUSE, &elmer);
		elmer |= ELMER0_GP_BIT1;
		if (is_T2(cphy->adapter))
		    elmer |= ELMER0_GP_BIT2|ELMER0_GP_BIT3|ELMER0_GP_BIT4;
		t1_tpi_write(cphy->adapter, A_ELMER0_INT_CAUSE, elmer);
	}
	return 0;
}

/*
 * Set the PHY speed and duplex.  This also disables auto-negotiation, except
 * for 1Gb/s, where auto-negotiation is mandatory.
 */
static int mv88e1xxx_set_speed_duplex(struct cphy *phy, int speed, int duplex)
{
	u32 ctl;

	(void) simple_mdio_read(phy, MII_BMCR, &ctl);
	if (speed >= 0) {
		ctl &= ~(BMCR_SPEED100 | BMCR_SPEED1000 | BMCR_ANENABLE);
		if (speed == SPEED_100)
			ctl |= BMCR_SPEED100;
		else if (speed == SPEED_1000)
			ctl |= BMCR_SPEED1000;
	}
	if (duplex >= 0) {
		ctl &= ~(BMCR_FULLDPLX | BMCR_ANENABLE);
		if (duplex == DUPLEX_FULL)
			ctl |= BMCR_FULLDPLX;
	}
	if (ctl & BMCR_SPEED1000)  /* auto-negotiation required for 1Gb/s */
		ctl |= BMCR_ANENABLE;
	(void) simple_mdio_write(phy, MII_BMCR, ctl);
	return 0;
}

static int mv88e1xxx_crossover_set(struct cphy *cphy, int crossover)
{
	u32 data32;

	(void) simple_mdio_read(cphy,
			MV88E1XXX_SPECIFIC_CNTRL_REGISTER, &data32);
	data32 &= ~V_PSCR_MDI_XOVER_MODE(M_PSCR_MDI_XOVER_MODE);
	data32 |= V_PSCR_MDI_XOVER_MODE(crossover);
	(void) simple_mdio_write(cphy,
			MV88E1XXX_SPECIFIC_CNTRL_REGISTER, data32);
	return 0;
}

static int mv88e1xxx_autoneg_enable(struct cphy *cphy)
{
	u32 ctl;

	(void) mv88e1xxx_crossover_set(cphy, CROSSOVER_AUTO);

	(void) simple_mdio_read(cphy, MII_BMCR, &ctl);
	/* restart autoneg for change to take effect */
	ctl |= BMCR_ANENABLE | BMCR_ANRESTART;
	(void) simple_mdio_write(cphy, MII_BMCR, ctl);
	return 0;
}

static int mv88e1xxx_autoneg_disable(struct cphy *cphy)
{
	u32 ctl;

	/*
	 * Crossover *must* be set to manual in order to disable auto-neg.
	 * The Alaska FAQs document highlights this point.
	 */
	(void) mv88e1xxx_crossover_set(cphy, CROSSOVER_MDI);

	/*
	 * Must include autoneg reset when disabling auto-neg. This
	 * is described in the Alaska FAQ document.
	 */
	(void) simple_mdio_read(cphy, MII_BMCR, &ctl);
	ctl &= ~BMCR_ANENABLE;
	(void) simple_mdio_write(cphy, MII_BMCR, ctl | BMCR_ANRESTART);
	return 0;
}

static int mv88e1xxx_autoneg_restart(struct cphy *cphy)
{
	mdio_set_bit(cphy, MII_BMCR, BMCR_ANRESTART);
	return 0;
}

static int mv88e1xxx_advertise(struct cphy *phy, unsigned int advertise_map)
{
	u32 val = 0;

	if (advertise_map &
	    (ADVERTISED_1000baseT_Half | ADVERTISED_1000baseT_Full)) {
		(void) simple_mdio_read(phy, MII_GBCR, &val);
		val &= ~(GBCR_ADV_1000HALF | GBCR_ADV_1000FULL);
		if (advertise_map & ADVERTISED_1000baseT_Half)
			val |= GBCR_ADV_1000HALF;
		if (advertise_map & ADVERTISED_1000baseT_Full)
			val |= GBCR_ADV_1000FULL;
	}
	(void) simple_mdio_write(phy, MII_GBCR, val);

	val = 1;
	if (advertise_map & ADVERTISED_10baseT_Half)
		val |= ADVERTISE_10HALF;
	if (advertise_map & ADVERTISED_10baseT_Full)
		val |= ADVERTISE_10FULL;
	if (advertise_map & ADVERTISED_100baseT_Half)
		val |= ADVERTISE_100HALF;
	if (advertise_map & ADVERTISED_100baseT_Full)
		val |= ADVERTISE_100FULL;
	if (advertise_map & ADVERTISED_PAUSE)
		val |= ADVERTISE_PAUSE;
	if (advertise_map & ADVERTISED_ASYM_PAUSE)
		val |= ADVERTISE_PAUSE_ASYM;
	(void) simple_mdio_write(phy, MII_ADVERTISE, val);
	return 0;
}

static int mv88e1xxx_set_loopback(struct cphy *cphy, int on)
{
	if (on)
		mdio_set_bit(cphy, MII_BMCR, BMCR_LOOPBACK);
	else
		mdio_clear_bit(cphy, MII_BMCR, BMCR_LOOPBACK);
	return 0;
}

static int mv88e1xxx_get_link_status(struct cphy *cphy, int *link_ok,
				     int *speed, int *duplex, int *fc)
{
	u32 status;
	int sp = -1, dplx = -1, pause = 0;

	(void) simple_mdio_read(cphy,
			MV88E1XXX_SPECIFIC_STATUS_REGISTER, &status);
	if ((status & V_PSSR_STATUS_RESOLVED) != 0) {
		if (status & V_PSSR_RX_PAUSE)
			pause |= PAUSE_RX;
		if (status & V_PSSR_TX_PAUSE)
			pause |= PAUSE_TX;
		dplx = (status & V_PSSR_DUPLEX) ? DUPLEX_FULL : DUPLEX_HALF;
		sp = G_PSSR_SPEED(status);
		if (sp == 0)
			sp = SPEED_10;
		else if (sp == 1)
			sp = SPEED_100;
		else
			sp = SPEED_1000;
	}
	if (link_ok)
		*link_ok = (status & V_PSSR_LINK) != 0;
	if (speed)
		*speed = sp;
	if (duplex)
		*duplex = dplx;
	if (fc)
		*fc = pause;
	return 0;
}

static int mv88e1xxx_downshift_set(struct cphy *cphy, int downshift_enable)
{
	u32 val;

	(void) simple_mdio_read(cphy,
		MV88E1XXX_EXT_PHY_SPECIFIC_CNTRL_REGISTER, &val);

	/*
	 * Set the downshift counter to 2 so we try to establish Gb link
	 * twice before downshifting.
	 */
	val &= ~(V_DOWNSHIFT_ENABLE | V_DOWNSHIFT_CNT(M_DOWNSHIFT_CNT));

	if (downshift_enable)
		val |= V_DOWNSHIFT_ENABLE | V_DOWNSHIFT_CNT(2);
	(void) simple_mdio_write(cphy,
			MV88E1XXX_EXT_PHY_SPECIFIC_CNTRL_REGISTER, val);
	return 0;
}

static int mv88e1xxx_interrupt_handler(struct cphy *cphy)
{
	int cphy_cause = 0;
	u32 status;

	/*
	 * Loop until cause reads zero. Need to handle bouncing interrupts.
	 */
	while (1) {
		u32 cause;

		(void) simple_mdio_read(cphy,
				MV88E1XXX_INTERRUPT_STATUS_REGISTER,
				&cause);
		cause &= INTR_ENABLE_MASK;
		if (!cause)
			break;

		if (cause & MV88E1XXX_INTR_LINK_CHNG) {
			(void) simple_mdio_read(cphy,
				MV88E1XXX_SPECIFIC_STATUS_REGISTER, &status);

			if (status & MV88E1XXX_INTR_LINK_CHNG)
				cphy->state |= PHY_LINK_UP;
			else {
				cphy->state &= ~PHY_LINK_UP;
				if (cphy->state & PHY_AUTONEG_EN)
					cphy->state &= ~PHY_AUTONEG_RDY;
				cphy_cause |= cphy_cause_link_change;
			}
		}

		if (cause & MV88E1XXX_INTR_AUTONEG_DONE)
			cphy->state |= PHY_AUTONEG_RDY;

		if ((cphy->state & (PHY_LINK_UP | PHY_AUTONEG_RDY)) ==
			(PHY_LINK_UP | PHY_AUTONEG_RDY))
				cphy_cause |= cphy_cause_link_change;
	}
	return cphy_cause;
}

static void mv88e1xxx_destroy(struct cphy *cphy)
{
	kfree(cphy);
}

static const struct cphy_ops mv88e1xxx_ops = {
	.destroy              = mv88e1xxx_destroy,
	.reset                = mv88e1xxx_reset,
	.interrupt_enable     = mv88e1xxx_interrupt_enable,
	.interrupt_disable    = mv88e1xxx_interrupt_disable,
	.interrupt_clear      = mv88e1xxx_interrupt_clear,
	.interrupt_handler    = mv88e1xxx_interrupt_handler,
	.autoneg_enable       = mv88e1xxx_autoneg_enable,
	.autoneg_disable      = mv88e1xxx_autoneg_disable,
	.autoneg_restart      = mv88e1xxx_autoneg_restart,
	.advertise            = mv88e1xxx_advertise,
	.set_loopback         = mv88e1xxx_set_loopback,
	.set_speed_duplex     = mv88e1xxx_set_speed_duplex,
	.get_link_status      = mv88e1xxx_get_link_status,
};

static struct cphy *mv88e1xxx_phy_create(struct net_device *dev, int phy_addr,
					 const struct mdio_ops *mdio_ops)
{
	struct adapter *adapter = netdev_priv(dev);
	struct cphy *cphy = kzalloc(sizeof(*cphy), GFP_KERNEL);

	if (!cphy)
		return NULL;

	cphy_init(cphy, dev, phy_addr, &mv88e1xxx_ops, mdio_ops);

	/* Configure particular PHY's to run in a different mode. */
	if ((board_info(adapter)->caps & SUPPORTED_TP) &&
	    board_info(adapter)->chip_phy == CHBT_PHY_88E1111) {
		/*
		 * Configure the PHY transmitter as class A to reduce EMI.
		 */
		(void) simple_mdio_write(cphy,
				MV88E1XXX_EXTENDED_ADDR_REGISTER, 0xB);
		(void) simple_mdio_write(cphy,
				MV88E1XXX_EXTENDED_REGISTER, 0x8004);
	}
	(void) mv88e1xxx_downshift_set(cphy, 1);   /* Enable downshift */

	/* LED */
	if (is_T2(adapter)) {
		(void) simple_mdio_write(cphy,
				MV88E1XXX_LED_CONTROL_REGISTER, 0x1);
	}

	return cphy;
}

static int mv88e1xxx_phy_reset(adapter_t* adapter)
{
	return 0;
}

const struct gphy t1_mv88e1xxx_ops = {
	.create = mv88e1xxx_phy_create,
	.reset =  mv88e1xxx_phy_reset
};
