// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Marvell 88E6352 family SERDES PCS support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2017 Andrew Lunn <andrew@lunn.ch>
 */
#include <linux/phylink.h>

#include "global2.h"
#include "port.h"
#include "serdes.h"

/* Definitions from drivers/net/phy/marvell.c, which would be good to reuse. */
#define MII_M1011_PHY_STATUS		17
#define MII_M1011_IMASK			18
#define MII_M1011_IMASK_LINK_CHANGE	BIT(10)
#define MII_M1011_IEVENT		19
#define MII_M1011_IEVENT_LINK_CHANGE	BIT(10)
#define MII_MARVELL_PHY_PAGE		22
#define MII_MARVELL_FIBER_PAGE		1

struct marvell_c22_pcs {
	struct mdio_device mdio;
	struct phylink_pcs phylink_pcs;
	unsigned int irq;
	char name[64];
	bool (*link_check)(struct marvell_c22_pcs *mpcs);
	struct mv88e6xxx_port *port;
};

static struct marvell_c22_pcs *pcs_to_marvell_c22_pcs(struct phylink_pcs *pcs)
{
	return container_of(pcs, struct marvell_c22_pcs, phylink_pcs);
}

static int marvell_c22_pcs_set_fiber_page(struct marvell_c22_pcs *mpcs)
{
	u16 page;
	int err;

	mutex_lock(&mpcs->mdio.bus->mdio_lock);

	err = __mdiodev_read(&mpcs->mdio, MII_MARVELL_PHY_PAGE);
	if (err < 0) {
		dev_err(mpcs->mdio.dev.parent,
			"%s: can't read Serdes page register: %pe\n",
			mpcs->name, ERR_PTR(err));
		return err;
	}

	page = err;

	err = __mdiodev_write(&mpcs->mdio, MII_MARVELL_PHY_PAGE,
			      MII_MARVELL_FIBER_PAGE);
	if (err) {
		dev_err(mpcs->mdio.dev.parent,
			"%s: can't set Serdes page register: %pe\n",
			mpcs->name, ERR_PTR(err));
		return err;
	}

	return page;
}

static int marvell_c22_pcs_restore_page(struct marvell_c22_pcs *mpcs,
					int oldpage, int ret)
{
	int err;

	if (oldpage >= 0) {
		err = __mdiodev_write(&mpcs->mdio, MII_MARVELL_PHY_PAGE,
				      oldpage);
		if (err)
			dev_err(mpcs->mdio.dev.parent,
				"%s: can't restore Serdes page register: %pe\n",
				mpcs->name, ERR_PTR(err));
		if (!err || ret < 0)
			err = ret;
	} else {
		err = oldpage;
	}
	mutex_unlock(&mpcs->mdio.bus->mdio_lock);

	return err;
}

static irqreturn_t marvell_c22_pcs_handle_irq(int irq, void *dev_id)
{
	struct marvell_c22_pcs *mpcs = dev_id;
	irqreturn_t status = IRQ_NONE;
	int err, oldpage;

	oldpage = marvell_c22_pcs_set_fiber_page(mpcs);
	if (oldpage < 0)
		goto fail;

	err = __mdiodev_read(&mpcs->mdio, MII_M1011_IEVENT);
	if (err >= 0 && err & MII_M1011_IEVENT_LINK_CHANGE) {
		phylink_pcs_change(&mpcs->phylink_pcs, true);
		status = IRQ_HANDLED;
	}

fail:
	marvell_c22_pcs_restore_page(mpcs, oldpage, 0);

	return status;
}

static int marvell_c22_pcs_modify(struct marvell_c22_pcs *mpcs, u8 reg,
				  u16 mask, u16 val)
{
	int oldpage, err = 0;

	oldpage = marvell_c22_pcs_set_fiber_page(mpcs);
	if (oldpage >= 0)
		err = __mdiodev_modify(&mpcs->mdio, reg, mask, val);

	return marvell_c22_pcs_restore_page(mpcs, oldpage, err);
}

static int marvell_c22_pcs_power(struct marvell_c22_pcs *mpcs,
				 bool on)
{
	u16 val = on ? 0 : BMCR_PDOWN;

	return marvell_c22_pcs_modify(mpcs, MII_BMCR, BMCR_PDOWN, val);
}

static int marvell_c22_pcs_control_irq(struct marvell_c22_pcs *mpcs,
				       bool enable)
{
	u16 val = enable ? MII_M1011_IMASK_LINK_CHANGE : 0;

	return marvell_c22_pcs_modify(mpcs, MII_M1011_IMASK,
				      MII_M1011_IMASK_LINK_CHANGE, val);
}

static int marvell_c22_pcs_enable(struct phylink_pcs *pcs)
{
	struct marvell_c22_pcs *mpcs = pcs_to_marvell_c22_pcs(pcs);
	int err;

	err = marvell_c22_pcs_power(mpcs, true);
	if (err)
		return err;

	return marvell_c22_pcs_control_irq(mpcs, !!mpcs->irq);
}

static void marvell_c22_pcs_disable(struct phylink_pcs *pcs)
{
	struct marvell_c22_pcs *mpcs = pcs_to_marvell_c22_pcs(pcs);

	marvell_c22_pcs_control_irq(mpcs, false);
	marvell_c22_pcs_power(mpcs, false);
}

static void marvell_c22_pcs_get_state(struct phylink_pcs *pcs,
				      unsigned int neg_mode,
				      struct phylink_link_state *state)
{
	struct marvell_c22_pcs *mpcs = pcs_to_marvell_c22_pcs(pcs);
	int oldpage, bmsr, lpa, status;

	state->link = false;

	if (mpcs->link_check && !mpcs->link_check(mpcs))
		return;

	oldpage = marvell_c22_pcs_set_fiber_page(mpcs);
	if (oldpage >= 0) {
		bmsr = __mdiodev_read(&mpcs->mdio, MII_BMSR);
		lpa = __mdiodev_read(&mpcs->mdio, MII_LPA);
		status = __mdiodev_read(&mpcs->mdio, MII_M1011_PHY_STATUS);
	}

	if (marvell_c22_pcs_restore_page(mpcs, oldpage, 0) >= 0 &&
	    bmsr >= 0 && lpa >= 0 && status >= 0)
		mv88e6xxx_pcs_decode_state(mpcs->mdio.dev.parent, bmsr, lpa,
					   status, state);
}

static int marvell_c22_pcs_config(struct phylink_pcs *pcs,
				  unsigned int neg_mode,
				  phy_interface_t interface,
				  const unsigned long *advertising,
				  bool permit_pause_to_mac)
{
	struct marvell_c22_pcs *mpcs = pcs_to_marvell_c22_pcs(pcs);
	int oldpage, adv, err, ret = 0;
	u16 bmcr;

	adv = phylink_mii_c22_pcs_encode_advertisement(interface, advertising);
	if (adv < 0)
		return 0;

	bmcr = neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED ? BMCR_ANENABLE : 0;

	oldpage = marvell_c22_pcs_set_fiber_page(mpcs);
	if (oldpage < 0)
		goto restore;

	err = __mdiodev_modify_changed(&mpcs->mdio, MII_ADVERTISE, 0xffff, adv);
	ret = err;
	if (err < 0)
		goto restore;

	err = __mdiodev_modify_changed(&mpcs->mdio, MII_BMCR, BMCR_ANENABLE,
				       bmcr);
	if (err < 0) {
		ret = err;
		goto restore;
	}

	/* If the ANENABLE bit was changed, the PHY will restart negotiation,
	 * so we don't need to flag a change to trigger its own restart.
	 */
	if (err)
		ret = 0;

restore:
	return marvell_c22_pcs_restore_page(mpcs, oldpage, ret);
}

static void marvell_c22_pcs_an_restart(struct phylink_pcs *pcs)
{
	struct marvell_c22_pcs *mpcs = pcs_to_marvell_c22_pcs(pcs);

	marvell_c22_pcs_modify(mpcs, MII_BMCR, BMCR_ANRESTART, BMCR_ANRESTART);
}

static void marvell_c22_pcs_link_up(struct phylink_pcs *pcs, unsigned int mode,
				    phy_interface_t interface, int speed,
				    int duplex)
{
	struct marvell_c22_pcs *mpcs = pcs_to_marvell_c22_pcs(pcs);
	u16 bmcr;
	int err;

	if (phylink_autoneg_inband(mode))
		return;

	bmcr = mii_bmcr_encode_fixed(speed, duplex);

	err = marvell_c22_pcs_modify(mpcs, MII_BMCR, BMCR_SPEED100 |
				     BMCR_FULLDPLX | BMCR_SPEED1000, bmcr);
	if (err)
		dev_err(mpcs->mdio.dev.parent,
			"%s: failed to configure mpcs: %pe\n", mpcs->name,
			ERR_PTR(err));
}

static const struct phylink_pcs_ops marvell_c22_pcs_ops = {
	.pcs_enable = marvell_c22_pcs_enable,
	.pcs_disable = marvell_c22_pcs_disable,
	.pcs_get_state = marvell_c22_pcs_get_state,
	.pcs_config = marvell_c22_pcs_config,
	.pcs_an_restart = marvell_c22_pcs_an_restart,
	.pcs_link_up = marvell_c22_pcs_link_up,
};

static struct marvell_c22_pcs *marvell_c22_pcs_alloc(struct device *dev,
						     struct mii_bus *bus,
						     unsigned int addr)
{
	struct marvell_c22_pcs *mpcs;

	mpcs = kzalloc(sizeof(*mpcs), GFP_KERNEL);
	if (!mpcs)
		return NULL;

	mpcs->mdio.dev.parent = dev;
	mpcs->mdio.bus = bus;
	mpcs->mdio.addr = addr;
	mpcs->phylink_pcs.ops = &marvell_c22_pcs_ops;

	return mpcs;
}

static int marvell_c22_pcs_setup_irq(struct marvell_c22_pcs *mpcs,
				     unsigned int irq)
{
	int err;

	mpcs->phylink_pcs.poll = !irq;
	mpcs->irq = irq;

	if (irq) {
		err = request_threaded_irq(irq, NULL,
					   marvell_c22_pcs_handle_irq,
					   IRQF_ONESHOT, mpcs->name, mpcs);
		if (err)
			return err;
	}

	return 0;
}

/* mv88e6352 specifics */

static bool mv88e6352_pcs_link_check(struct marvell_c22_pcs *mpcs)
{
	struct mv88e6xxx_port *port = mpcs->port;
	struct mv88e6xxx_chip *chip = port->chip;
	u8 cmode;

	/* Port 4 can be in auto-media mode. Check that the port is
	 * associated with the mpcs.
	 */
	mv88e6xxx_reg_lock(chip);
	chip->info->ops->port_get_cmode(chip, port->port, &cmode);
	mv88e6xxx_reg_unlock(chip);

	return cmode == MV88E6XXX_PORT_STS_CMODE_100BASEX ||
	       cmode == MV88E6XXX_PORT_STS_CMODE_1000BASEX ||
	       cmode == MV88E6XXX_PORT_STS_CMODE_SGMII;
}

static int mv88e6352_pcs_init(struct mv88e6xxx_chip *chip, int port)
{
	struct marvell_c22_pcs *mpcs;
	struct mii_bus *bus;
	struct device *dev;
	unsigned int irq;
	int err;

	mv88e6xxx_reg_lock(chip);
	err = mv88e6352_g2_scratch_port_has_serdes(chip, port);
	mv88e6xxx_reg_unlock(chip);
	if (err <= 0)
		return err;

	irq = mv88e6xxx_serdes_irq_mapping(chip, port);
	bus = mv88e6xxx_default_mdio_bus(chip);
	dev = chip->dev;

	mpcs = marvell_c22_pcs_alloc(dev, bus, MV88E6352_ADDR_SERDES);
	if (!mpcs)
		return -ENOMEM;

	snprintf(mpcs->name, sizeof(mpcs->name),
		 "mv88e6xxx-%s-serdes-%d", dev_name(dev), port);

	mpcs->link_check = mv88e6352_pcs_link_check;
	mpcs->port = &chip->ports[port];

	err = marvell_c22_pcs_setup_irq(mpcs, irq);
	if (err) {
		kfree(mpcs);
		return err;
	}

	chip->ports[port].pcs_private = &mpcs->phylink_pcs;

	return 0;
}

static void mv88e6352_pcs_teardown(struct mv88e6xxx_chip *chip, int port)
{
	struct marvell_c22_pcs *mpcs;
	struct phylink_pcs *pcs;

	pcs = chip->ports[port].pcs_private;
	if (!pcs)
		return;

	mpcs = pcs_to_marvell_c22_pcs(pcs);

	if (mpcs->irq)
		free_irq(mpcs->irq, mpcs);

	kfree(mpcs);

	chip->ports[port].pcs_private = NULL;
}

static struct phylink_pcs *mv88e6352_pcs_select(struct mv88e6xxx_chip *chip,
						int port,
						phy_interface_t interface)
{
	return chip->ports[port].pcs_private;
}

const struct mv88e6xxx_pcs_ops mv88e6352_pcs_ops = {
	.pcs_init = mv88e6352_pcs_init,
	.pcs_teardown = mv88e6352_pcs_teardown,
	.pcs_select = mv88e6352_pcs_select,
};
