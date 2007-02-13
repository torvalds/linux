/*
 * This file is part of the Chelsio T2 Ethernet driver.
 *
 * Copyright (C) 2005 Chelsio Communications.  All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the LICENSE file included in this
 * release for licensing terms and conditions.
 */

#include "common.h"
#include "cphy.h"
#include "elmer0.h"

#ifndef ADVERTISE_PAUSE_CAP
# define ADVERTISE_PAUSE_CAP 0x400
#endif
#ifndef ADVERTISE_PAUSE_ASYM
# define ADVERTISE_PAUSE_ASYM 0x800
#endif

/* Gigabit MII registers */
#ifndef MII_CTRL1000
# define MII_CTRL1000 9
#endif

#ifndef ADVERTISE_1000FULL
# define ADVERTISE_1000FULL 0x200
# define ADVERTISE_1000HALF 0x100
#endif

/* VSC8244 PHY specific registers. */
enum {
	VSC8244_INTR_ENABLE   = 25,
	VSC8244_INTR_STATUS   = 26,
	VSC8244_AUX_CTRL_STAT = 28,
};

enum {
	VSC_INTR_RX_ERR     = 1 << 0,
	VSC_INTR_MS_ERR     = 1 << 1,  /* master/slave resolution error */
	VSC_INTR_CABLE      = 1 << 2,  /* cable impairment */
	VSC_INTR_FALSE_CARR = 1 << 3,  /* false carrier */
	VSC_INTR_MEDIA_CHG  = 1 << 4,  /* AMS media change */
	VSC_INTR_RX_FIFO    = 1 << 5,  /* Rx FIFO over/underflow */
	VSC_INTR_TX_FIFO    = 1 << 6,  /* Tx FIFO over/underflow */
	VSC_INTR_DESCRAMBL  = 1 << 7,  /* descrambler lock-lost */
	VSC_INTR_SYMBOL_ERR = 1 << 8,  /* symbol error */
	VSC_INTR_NEG_DONE   = 1 << 10, /* autoneg done */
	VSC_INTR_NEG_ERR    = 1 << 11, /* autoneg error */
	VSC_INTR_LINK_CHG   = 1 << 13, /* link change */
	VSC_INTR_ENABLE     = 1 << 15, /* interrupt enable */
};

#define CFG_CHG_INTR_MASK (VSC_INTR_LINK_CHG | VSC_INTR_NEG_ERR | \
			   VSC_INTR_NEG_DONE)
#define INTR_MASK (CFG_CHG_INTR_MASK | VSC_INTR_TX_FIFO | VSC_INTR_RX_FIFO | \
		   VSC_INTR_ENABLE)

/* PHY specific auxiliary control & status register fields */
#define S_ACSR_ACTIPHY_TMR    0
#define M_ACSR_ACTIPHY_TMR    0x3
#define V_ACSR_ACTIPHY_TMR(x) ((x) << S_ACSR_ACTIPHY_TMR)

#define S_ACSR_SPEED    3
#define M_ACSR_SPEED    0x3
#define G_ACSR_SPEED(x) (((x) >> S_ACSR_SPEED) & M_ACSR_SPEED)

#define S_ACSR_DUPLEX 5
#define F_ACSR_DUPLEX (1 << S_ACSR_DUPLEX)

#define S_ACSR_ACTIPHY 6
#define F_ACSR_ACTIPHY (1 << S_ACSR_ACTIPHY)

/*
 * Reset the PHY.  This PHY completes reset immediately so we never wait.
 */
static int vsc8244_reset(struct cphy *cphy, int wait)
{
	int err;
	unsigned int ctl;

	err = simple_mdio_read(cphy, MII_BMCR, &ctl);
	if (err)
		return err;

	ctl &= ~BMCR_PDOWN;
	ctl |= BMCR_RESET;
	return simple_mdio_write(cphy, MII_BMCR, ctl);
}

static int vsc8244_intr_enable(struct cphy *cphy)
{
	simple_mdio_write(cphy, VSC8244_INTR_ENABLE, INTR_MASK);

	/* Enable interrupts through Elmer */
	if (t1_is_asic(cphy->adapter)) {
		u32 elmer;

		t1_tpi_read(cphy->adapter, A_ELMER0_INT_ENABLE, &elmer);
		elmer |= ELMER0_GP_BIT1;
		if (is_T2(cphy->adapter))
		    elmer |= ELMER0_GP_BIT2|ELMER0_GP_BIT3|ELMER0_GP_BIT4;
		t1_tpi_write(cphy->adapter, A_ELMER0_INT_ENABLE, elmer);
	}

	return 0;
}

static int vsc8244_intr_disable(struct cphy *cphy)
{
	simple_mdio_write(cphy, VSC8244_INTR_ENABLE, 0);

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

static int vsc8244_intr_clear(struct cphy *cphy)
{
	u32 val;
	u32 elmer;

	/* Clear PHY interrupts by reading the register. */
	simple_mdio_read(cphy, VSC8244_INTR_ENABLE, &val);

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
 * Force the PHY speed and duplex.  This also disables auto-negotiation, except
 * for 1Gb/s, where auto-negotiation is mandatory.
 */
static int vsc8244_set_speed_duplex(struct cphy *phy, int speed, int duplex)
{
	int err;
	unsigned int ctl;

	err = simple_mdio_read(phy, MII_BMCR, &ctl);
	if (err)
		return err;

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
	return simple_mdio_write(phy, MII_BMCR, ctl);
}

int t1_mdio_set_bits(struct cphy *phy, int mmd, int reg, unsigned int bits)
{
	int ret;
	unsigned int val;

	ret = mdio_read(phy, mmd, reg, &val);
	if (!ret)
		ret = mdio_write(phy, mmd, reg, val | bits);
	return ret;
}

static int vsc8244_autoneg_enable(struct cphy *cphy)
{
	return t1_mdio_set_bits(cphy, 0, MII_BMCR,
				BMCR_ANENABLE | BMCR_ANRESTART);
}

static int vsc8244_autoneg_restart(struct cphy *cphy)
{
	return t1_mdio_set_bits(cphy, 0, MII_BMCR, BMCR_ANRESTART);
}

static int vsc8244_advertise(struct cphy *phy, unsigned int advertise_map)
{
	int err;
	unsigned int val = 0;

	err = simple_mdio_read(phy, MII_CTRL1000, &val);
	if (err)
		return err;

	val &= ~(ADVERTISE_1000HALF | ADVERTISE_1000FULL);
	if (advertise_map & ADVERTISED_1000baseT_Half)
		val |= ADVERTISE_1000HALF;
	if (advertise_map & ADVERTISED_1000baseT_Full)
		val |= ADVERTISE_1000FULL;

	err = simple_mdio_write(phy, MII_CTRL1000, val);
	if (err)
		return err;

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
		val |= ADVERTISE_PAUSE_CAP;
	if (advertise_map & ADVERTISED_ASYM_PAUSE)
		val |= ADVERTISE_PAUSE_ASYM;
	return simple_mdio_write(phy, MII_ADVERTISE, val);
}

static int vsc8244_get_link_status(struct cphy *cphy, int *link_ok,
				   int *speed, int *duplex, int *fc)
{
	unsigned int bmcr, status, lpa, adv;
	int err, sp = -1, dplx = -1, pause = 0;

	err = simple_mdio_read(cphy, MII_BMCR, &bmcr);
	if (!err)
		err = simple_mdio_read(cphy, MII_BMSR, &status);
	if (err)
		return err;

	if (link_ok) {
		/*
		 * BMSR_LSTATUS is latch-low, so if it is 0 we need to read it
		 * once more to get the current link state.
		 */
		if (!(status & BMSR_LSTATUS))
			err = simple_mdio_read(cphy, MII_BMSR, &status);
		if (err)
			return err;
		*link_ok = (status & BMSR_LSTATUS) != 0;
	}
	if (!(bmcr & BMCR_ANENABLE)) {
		dplx = (bmcr & BMCR_FULLDPLX) ? DUPLEX_FULL : DUPLEX_HALF;
		if (bmcr & BMCR_SPEED1000)
			sp = SPEED_1000;
		else if (bmcr & BMCR_SPEED100)
			sp = SPEED_100;
		else
			sp = SPEED_10;
	} else if (status & BMSR_ANEGCOMPLETE) {
		err = simple_mdio_read(cphy, VSC8244_AUX_CTRL_STAT, &status);
		if (err)
			return err;

		dplx = (status & F_ACSR_DUPLEX) ? DUPLEX_FULL : DUPLEX_HALF;
		sp = G_ACSR_SPEED(status);
		if (sp == 0)
			sp = SPEED_10;
		else if (sp == 1)
			sp = SPEED_100;
		else
			sp = SPEED_1000;

		if (fc && dplx == DUPLEX_FULL) {
			err = simple_mdio_read(cphy, MII_LPA, &lpa);
			if (!err)
				err = simple_mdio_read(cphy, MII_ADVERTISE,
						       &adv);
			if (err)
				return err;

			if (lpa & adv & ADVERTISE_PAUSE_CAP)
				pause = PAUSE_RX | PAUSE_TX;
			else if ((lpa & ADVERTISE_PAUSE_CAP) &&
				 (lpa & ADVERTISE_PAUSE_ASYM) &&
				 (adv & ADVERTISE_PAUSE_ASYM))
				pause = PAUSE_TX;
			else if ((lpa & ADVERTISE_PAUSE_ASYM) &&
				 (adv & ADVERTISE_PAUSE_CAP))
				pause = PAUSE_RX;
		}
	}
	if (speed)
		*speed = sp;
	if (duplex)
		*duplex = dplx;
	if (fc)
		*fc = pause;
	return 0;
}

static int vsc8244_intr_handler(struct cphy *cphy)
{
	unsigned int cause;
	int err, cphy_cause = 0;

	err = simple_mdio_read(cphy, VSC8244_INTR_STATUS, &cause);
	if (err)
		return err;

	cause &= INTR_MASK;
	if (cause & CFG_CHG_INTR_MASK)
		cphy_cause |= cphy_cause_link_change;
	if (cause & (VSC_INTR_RX_FIFO | VSC_INTR_TX_FIFO))
		cphy_cause |= cphy_cause_fifo_error;
	return cphy_cause;
}

static void vsc8244_destroy(struct cphy *cphy)
{
	kfree(cphy);
}

static struct cphy_ops vsc8244_ops = {
	.destroy              = vsc8244_destroy,
	.reset                = vsc8244_reset,
	.interrupt_enable     = vsc8244_intr_enable,
	.interrupt_disable    = vsc8244_intr_disable,
	.interrupt_clear      = vsc8244_intr_clear,
	.interrupt_handler    = vsc8244_intr_handler,
	.autoneg_enable       = vsc8244_autoneg_enable,
	.autoneg_restart      = vsc8244_autoneg_restart,
	.advertise            = vsc8244_advertise,
	.set_speed_duplex     = vsc8244_set_speed_duplex,
	.get_link_status      = vsc8244_get_link_status
};

static struct cphy* vsc8244_phy_create(adapter_t *adapter, int phy_addr,
				       struct mdio_ops *mdio_ops)
{
	struct cphy *cphy = kzalloc(sizeof(*cphy), GFP_KERNEL);

	if (!cphy)
		return NULL;

	cphy_init(cphy, adapter, phy_addr, &vsc8244_ops, mdio_ops);

	return cphy;
}


static int vsc8244_phy_reset(adapter_t* adapter)
{
	return 0;
}

struct gphy t1_vsc8244_ops = {
	vsc8244_phy_create,
	vsc8244_phy_reset
};


