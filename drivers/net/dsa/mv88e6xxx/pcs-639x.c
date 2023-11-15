// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Marvell 88E6352 family SERDES PCS support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2017 Andrew Lunn <andrew@lunn.ch>
 */
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/mii.h>

#include "chip.h"
#include "global2.h"
#include "phy.h"
#include "port.h"
#include "serdes.h"

struct mv88e639x_pcs {
	struct mdio_device mdio;
	struct phylink_pcs sgmii_pcs;
	struct phylink_pcs xg_pcs;
	bool erratum_3_14;
	bool supports_5g;
	phy_interface_t interface;
	unsigned int irq;
	char name[64];
	irqreturn_t (*handle_irq)(struct mv88e639x_pcs *mpcs);
};

static int mv88e639x_read(struct mv88e639x_pcs *mpcs, u16 regnum, u16 *val)
{
	int err;

	err = mdiodev_c45_read(&mpcs->mdio, MDIO_MMD_PHYXS, regnum);
	if (err < 0)
		return err;

	*val = err;

	return 0;
}

static int mv88e639x_write(struct mv88e639x_pcs *mpcs, u16 regnum, u16 val)
{
	return mdiodev_c45_write(&mpcs->mdio, MDIO_MMD_PHYXS, regnum, val);
}

static int mv88e639x_modify(struct mv88e639x_pcs *mpcs, u16 regnum, u16 mask,
			    u16 val)
{
	return mdiodev_c45_modify(&mpcs->mdio, MDIO_MMD_PHYXS, regnum, mask,
				  val);
}

static int mv88e639x_modify_changed(struct mv88e639x_pcs *mpcs, u16 regnum,
				    u16 mask, u16 set)
{
	return mdiodev_c45_modify_changed(&mpcs->mdio, MDIO_MMD_PHYXS, regnum,
					  mask, set);
}

static struct mv88e639x_pcs *
mv88e639x_pcs_alloc(struct device *dev, struct mii_bus *bus, unsigned int addr,
		    int port)
{
	struct mv88e639x_pcs *mpcs;

	mpcs = kzalloc(sizeof(*mpcs), GFP_KERNEL);
	if (!mpcs)
		return NULL;

	mpcs->mdio.dev.parent = dev;
	mpcs->mdio.bus = bus;
	mpcs->mdio.addr = addr;

	snprintf(mpcs->name, sizeof(mpcs->name),
		 "mv88e6xxx-%s-serdes-%d", dev_name(dev), port);

	return mpcs;
}

static irqreturn_t mv88e639x_pcs_handle_irq(int irq, void *dev_id)
{
	struct mv88e639x_pcs *mpcs = dev_id;
	irqreturn_t (*handler)(struct mv88e639x_pcs *);

	handler = READ_ONCE(mpcs->handle_irq);
	if (!handler)
		return IRQ_NONE;

	return handler(mpcs);
}

static int mv88e639x_pcs_setup_irq(struct mv88e639x_pcs *mpcs,
				   struct mv88e6xxx_chip *chip, int port)
{
	unsigned int irq;

	irq = mv88e6xxx_serdes_irq_mapping(chip, port);
	if (!irq) {
		/* Use polling mode */
		mpcs->sgmii_pcs.poll = true;
		mpcs->xg_pcs.poll = true;
		return 0;
	}

	mpcs->irq = irq;

	return request_threaded_irq(irq, NULL, mv88e639x_pcs_handle_irq,
				    IRQF_ONESHOT, mpcs->name, mpcs);
}

static void mv88e639x_pcs_teardown(struct mv88e6xxx_chip *chip, int port)
{
	struct mv88e639x_pcs *mpcs = chip->ports[port].pcs_private;

	if (!mpcs)
		return;

	if (mpcs->irq)
		free_irq(mpcs->irq, mpcs);

	kfree(mpcs);

	chip->ports[port].pcs_private = NULL;
}

static struct mv88e639x_pcs *sgmii_pcs_to_mv88e639x_pcs(struct phylink_pcs *pcs)
{
	return container_of(pcs, struct mv88e639x_pcs, sgmii_pcs);
}

static irqreturn_t mv88e639x_sgmii_handle_irq(struct mv88e639x_pcs *mpcs)
{
	u16 int_status;
	int err;

	err = mv88e639x_read(mpcs, MV88E6390_SGMII_INT_STATUS, &int_status);
	if (err)
		return IRQ_NONE;

	if (int_status & (MV88E6390_SGMII_INT_LINK_DOWN |
			  MV88E6390_SGMII_INT_LINK_UP)) {
		phylink_pcs_change(&mpcs->sgmii_pcs,
				   int_status & MV88E6390_SGMII_INT_LINK_UP);

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int mv88e639x_sgmii_pcs_control_irq(struct mv88e639x_pcs *mpcs,
					   bool enable)
{
	u16 val = 0;

	if (enable)
		val |= MV88E6390_SGMII_INT_LINK_DOWN |
		       MV88E6390_SGMII_INT_LINK_UP;

	return mv88e639x_modify(mpcs, MV88E6390_SGMII_INT_ENABLE,
				MV88E6390_SGMII_INT_LINK_DOWN |
				MV88E6390_SGMII_INT_LINK_UP, val);
}

static int mv88e639x_sgmii_pcs_control_pwr(struct mv88e639x_pcs *mpcs,
					   bool enable)
{
	u16 mask, val;

	if (enable) {
		mask = BMCR_RESET | BMCR_LOOPBACK | BMCR_PDOWN;
		val = 0;
	} else {
		mask = val = BMCR_PDOWN;
	}

	return mv88e639x_modify(mpcs, MV88E6390_SGMII_BMCR, mask, val);
}

static int mv88e639x_sgmii_pcs_enable(struct phylink_pcs *pcs)
{
	struct mv88e639x_pcs *mpcs = sgmii_pcs_to_mv88e639x_pcs(pcs);

	/* power enable done in post_config */
	mpcs->handle_irq = mv88e639x_sgmii_handle_irq;

	return mv88e639x_sgmii_pcs_control_irq(mpcs, !!mpcs->irq);
}

static void mv88e639x_sgmii_pcs_disable(struct phylink_pcs *pcs)
{
	struct mv88e639x_pcs *mpcs = sgmii_pcs_to_mv88e639x_pcs(pcs);

	mv88e639x_sgmii_pcs_control_irq(mpcs, false);
	mv88e639x_sgmii_pcs_control_pwr(mpcs, false);
}

static void mv88e639x_sgmii_pcs_pre_config(struct phylink_pcs *pcs,
					   phy_interface_t interface)
{
	struct mv88e639x_pcs *mpcs = sgmii_pcs_to_mv88e639x_pcs(pcs);

	mv88e639x_sgmii_pcs_control_pwr(mpcs, false);
}

static int mv88e6390_erratum_3_14(struct mv88e639x_pcs *mpcs)
{
	static const int lanes[] = { MV88E6390_PORT9_LANE0, MV88E6390_PORT9_LANE1,
		MV88E6390_PORT9_LANE2, MV88E6390_PORT9_LANE3,
		MV88E6390_PORT10_LANE0, MV88E6390_PORT10_LANE1,
		MV88E6390_PORT10_LANE2, MV88E6390_PORT10_LANE3 };
	int err, i;

	/* 88e6190x and 88e6390x errata 3.14:
	 * After chip reset, SERDES reconfiguration or SERDES core
	 * Software Reset, the SERDES lanes may not be properly aligned
	 * resulting in CRC errors
	 */

	for (i = 0; i < ARRAY_SIZE(lanes); i++) {
		err = mdiobus_c45_write(mpcs->mdio.bus, lanes[i],
					MDIO_MMD_PHYXS,
					0xf054, 0x400C);
		if (err)
			return err;

		err = mdiobus_c45_write(mpcs->mdio.bus, lanes[i],
					MDIO_MMD_PHYXS,
					0xf054, 0x4000);
		if (err)
			return err;
	}

	return 0;
}

static int mv88e639x_sgmii_pcs_post_config(struct phylink_pcs *pcs,
					   phy_interface_t interface)
{
	struct mv88e639x_pcs *mpcs = sgmii_pcs_to_mv88e639x_pcs(pcs);
	int err;

	mv88e639x_sgmii_pcs_control_pwr(mpcs, true);

	if (mpcs->erratum_3_14) {
		err = mv88e6390_erratum_3_14(mpcs);
		if (err)
			dev_err(mpcs->mdio.dev.parent,
				"failed to apply erratum 3.14: %pe\n",
				ERR_PTR(err));
	}

	return 0;
}

static void mv88e639x_sgmii_pcs_get_state(struct phylink_pcs *pcs,
					  struct phylink_link_state *state)
{
	struct mv88e639x_pcs *mpcs = sgmii_pcs_to_mv88e639x_pcs(pcs);
	u16 bmsr, lpa, status;
	int err;

	err = mv88e639x_read(mpcs, MV88E6390_SGMII_BMSR, &bmsr);
	if (err) {
		dev_err(mpcs->mdio.dev.parent,
			"can't read Serdes PHY %s: %pe\n",
			"BMSR", ERR_PTR(err));
		state->link = false;
		return;
	}

	err = mv88e639x_read(mpcs, MV88E6390_SGMII_LPA, &lpa);
	if (err) {
		dev_err(mpcs->mdio.dev.parent,
			"can't read Serdes PHY %s: %pe\n",
			"LPA", ERR_PTR(err));
		state->link = false;
		return;
	}

	err = mv88e639x_read(mpcs, MV88E6390_SGMII_PHY_STATUS, &status);
	if (err) {
		dev_err(mpcs->mdio.dev.parent,
			"can't read Serdes PHY %s: %pe\n",
			"status", ERR_PTR(err));
		state->link = false;
		return;
	}

	mv88e6xxx_pcs_decode_state(mpcs->mdio.dev.parent, bmsr, lpa, status,
				   state);
}

static int mv88e639x_sgmii_pcs_config(struct phylink_pcs *pcs,
				      unsigned int neg_mode,
				      phy_interface_t interface,
				      const unsigned long *advertising,
				      bool permit_pause_to_mac)
{
	struct mv88e639x_pcs *mpcs = sgmii_pcs_to_mv88e639x_pcs(pcs);
	u16 val, bmcr;
	bool changed;
	int adv, err;

	adv = phylink_mii_c22_pcs_encode_advertisement(interface, advertising);
	if (adv < 0)
		return 0;

	mpcs->interface = interface;

	err = mv88e639x_modify_changed(mpcs, MV88E6390_SGMII_ADVERTISE,
				       0xffff, adv);
	if (err < 0)
		return err;

	changed = err > 0;

	err = mv88e639x_read(mpcs, MV88E6390_SGMII_BMCR, &val);
	if (err)
		return err;

	if (neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED)
		bmcr = val | BMCR_ANENABLE;
	else
		bmcr = val & ~BMCR_ANENABLE;

	/* setting ANENABLE triggers a restart of negotiation */
	if (bmcr == val)
		return changed;

	return mv88e639x_write(mpcs, MV88E6390_SGMII_BMCR, bmcr);
}

static void mv88e639x_sgmii_pcs_an_restart(struct phylink_pcs *pcs)
{
	struct mv88e639x_pcs *mpcs = sgmii_pcs_to_mv88e639x_pcs(pcs);

	mv88e639x_modify(mpcs, MV88E6390_SGMII_BMCR,
			 BMCR_ANRESTART, BMCR_ANRESTART);
}

static void mv88e639x_sgmii_pcs_link_up(struct phylink_pcs *pcs,
					unsigned int mode,
					phy_interface_t interface,
					int speed, int duplex)
{
	struct mv88e639x_pcs *mpcs = sgmii_pcs_to_mv88e639x_pcs(pcs);
	u16 bmcr;
	int err;

	if (phylink_autoneg_inband(mode))
		return;

	bmcr = mii_bmcr_encode_fixed(speed, duplex);

	err = mv88e639x_modify(mpcs, MV88E6390_SGMII_BMCR,
			       BMCR_SPEED1000 | BMCR_SPEED100 | BMCR_FULLDPLX,
			       bmcr);
	if (err)
		dev_err(mpcs->mdio.dev.parent,
			"can't access Serdes PHY %s: %pe\n",
			"BMCR", ERR_PTR(err));
}

static const struct phylink_pcs_ops mv88e639x_sgmii_pcs_ops = {
	.pcs_enable = mv88e639x_sgmii_pcs_enable,
	.pcs_disable = mv88e639x_sgmii_pcs_disable,
	.pcs_pre_config = mv88e639x_sgmii_pcs_pre_config,
	.pcs_post_config = mv88e639x_sgmii_pcs_post_config,
	.pcs_get_state = mv88e639x_sgmii_pcs_get_state,
	.pcs_an_restart = mv88e639x_sgmii_pcs_an_restart,
	.pcs_config = mv88e639x_sgmii_pcs_config,
	.pcs_link_up = mv88e639x_sgmii_pcs_link_up,
};

static struct mv88e639x_pcs *xg_pcs_to_mv88e639x_pcs(struct phylink_pcs *pcs)
{
	return container_of(pcs, struct mv88e639x_pcs, xg_pcs);
}

static int mv88e639x_xg_pcs_enable(struct mv88e639x_pcs *mpcs)
{
	return mv88e639x_modify(mpcs, MV88E6390_10G_CTRL1,
				MDIO_CTRL1_RESET | MDIO_PCS_CTRL1_LOOPBACK |
				MDIO_CTRL1_LPOWER, 0);
}

static void mv88e639x_xg_pcs_disable(struct mv88e639x_pcs *mpcs)
{
	mv88e639x_modify(mpcs, MV88E6390_10G_CTRL1, MDIO_CTRL1_LPOWER,
			 MDIO_CTRL1_LPOWER);
}

static void mv88e639x_xg_pcs_get_state(struct phylink_pcs *pcs,
				       struct phylink_link_state *state)
{
	struct mv88e639x_pcs *mpcs = xg_pcs_to_mv88e639x_pcs(pcs);
	u16 status;
	int err;

	state->link = false;

	err = mv88e639x_read(mpcs, MV88E6390_10G_STAT1, &status);
	if (err) {
		dev_err(mpcs->mdio.dev.parent,
			"can't read Serdes PHY %s: %pe\n",
			"STAT1", ERR_PTR(err));
		return;
	}

	state->link = !!(status & MDIO_STAT1_LSTATUS);
	if (state->link) {
		switch (state->interface) {
		case PHY_INTERFACE_MODE_5GBASER:
			state->speed = SPEED_5000;
			break;

		case PHY_INTERFACE_MODE_10GBASER:
		case PHY_INTERFACE_MODE_RXAUI:
		case PHY_INTERFACE_MODE_XAUI:
			state->speed = SPEED_10000;
			break;

		default:
			state->link = false;
			return;
		}

		state->duplex = DUPLEX_FULL;
	}
}

static int mv88e639x_xg_pcs_config(struct phylink_pcs *pcs,
				   unsigned int neg_mode,
				   phy_interface_t interface,
				   const unsigned long *advertising,
				   bool permit_pause_to_mac)
{
	return 0;
}

static struct phylink_pcs *
mv88e639x_pcs_select(struct mv88e6xxx_chip *chip, int port,
		     phy_interface_t mode)
{
	struct mv88e639x_pcs *mpcs;

	mpcs = chip->ports[port].pcs_private;
	if (!mpcs)
		return NULL;

	switch (mode) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
		return &mpcs->sgmii_pcs;

	case PHY_INTERFACE_MODE_5GBASER:
		if (!mpcs->supports_5g)
			return NULL;
		fallthrough;
	case PHY_INTERFACE_MODE_10GBASER:
	case PHY_INTERFACE_MODE_XAUI:
	case PHY_INTERFACE_MODE_RXAUI:
		return &mpcs->xg_pcs;

	default:
		return NULL;
	}
}

/* Marvell 88E6390 Specific support */

static irqreturn_t mv88e6390_xg_handle_irq(struct mv88e639x_pcs *mpcs)
{
	u16 int_status;
	int err;

	err = mv88e639x_read(mpcs, MV88E6390_10G_INT_STATUS, &int_status);
	if (err)
		return IRQ_NONE;

	if (int_status & (MV88E6390_10G_INT_LINK_DOWN |
			  MV88E6390_10G_INT_LINK_UP)) {
		phylink_pcs_change(&mpcs->xg_pcs,
				   int_status & MV88E6390_10G_INT_LINK_UP);

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int mv88e6390_xg_control_irq(struct mv88e639x_pcs *mpcs, bool enable)
{
	u16 val = 0;

	if (enable)
		val = MV88E6390_10G_INT_LINK_DOWN | MV88E6390_10G_INT_LINK_UP;

	return mv88e639x_modify(mpcs, MV88E6390_10G_INT_ENABLE,
				MV88E6390_10G_INT_LINK_DOWN |
				MV88E6390_10G_INT_LINK_UP, val);
}

static int mv88e6390_xg_pcs_enable(struct phylink_pcs *pcs)
{
	struct mv88e639x_pcs *mpcs = xg_pcs_to_mv88e639x_pcs(pcs);
	int err;

	err = mv88e639x_xg_pcs_enable(mpcs);
	if (err)
		return err;

	mpcs->handle_irq = mv88e6390_xg_handle_irq;

	return mv88e6390_xg_control_irq(mpcs, !!mpcs->irq);
}

static void mv88e6390_xg_pcs_disable(struct phylink_pcs *pcs)
{
	struct mv88e639x_pcs *mpcs = xg_pcs_to_mv88e639x_pcs(pcs);

	mv88e6390_xg_control_irq(mpcs, false);
	mv88e639x_xg_pcs_disable(mpcs);
}

static const struct phylink_pcs_ops mv88e6390_xg_pcs_ops = {
	.pcs_enable = mv88e6390_xg_pcs_enable,
	.pcs_disable = mv88e6390_xg_pcs_disable,
	.pcs_get_state = mv88e639x_xg_pcs_get_state,
	.pcs_config = mv88e639x_xg_pcs_config,
};

static int mv88e6390_pcs_enable_checker(struct mv88e639x_pcs *mpcs)
{
	return mv88e639x_modify(mpcs, MV88E6390_PG_CONTROL,
				MV88E6390_PG_CONTROL_ENABLE_PC,
				MV88E6390_PG_CONTROL_ENABLE_PC);
}

static int mv88e6390_pcs_init(struct mv88e6xxx_chip *chip, int port)
{
	struct mv88e639x_pcs *mpcs;
	struct mii_bus *bus;
	struct device *dev;
	int lane, err;

	lane = mv88e6xxx_serdes_get_lane(chip, port);
	if (lane < 0)
		return 0;

	bus = mv88e6xxx_default_mdio_bus(chip);
	dev = chip->dev;

	mpcs = mv88e639x_pcs_alloc(dev, bus, lane, port);
	if (!mpcs)
		return -ENOMEM;

	mpcs->sgmii_pcs.ops = &mv88e639x_sgmii_pcs_ops;
	mpcs->sgmii_pcs.neg_mode = true;
	mpcs->xg_pcs.ops = &mv88e6390_xg_pcs_ops;
	mpcs->xg_pcs.neg_mode = true;

	if (chip->info->prod_num == MV88E6XXX_PORT_SWITCH_ID_PROD_6190X ||
	    chip->info->prod_num == MV88E6XXX_PORT_SWITCH_ID_PROD_6390X)
		mpcs->erratum_3_14 = true;

	err = mv88e639x_pcs_setup_irq(mpcs, chip, port);
	if (err)
		goto err_free;

	/* 6390 and 6390x has the checker, 6393x doesn't appear to? */
	/* This is to enable gathering the statistics. Maybe this
	 * should call out to a helper? Or we could do this at init time.
	 */
	err = mv88e6390_pcs_enable_checker(mpcs);
	if (err)
		goto err_free;

	chip->ports[port].pcs_private = mpcs;

	return 0;

err_free:
	kfree(mpcs);
	return err;
}

const struct mv88e6xxx_pcs_ops mv88e6390_pcs_ops = {
	.pcs_init = mv88e6390_pcs_init,
	.pcs_teardown = mv88e639x_pcs_teardown,
	.pcs_select = mv88e639x_pcs_select,
};

/* Marvell 88E6393X Specific support */

static int mv88e6393x_power_lane(struct mv88e639x_pcs *mpcs, bool enable)
{
	u16 val = MV88E6393X_SERDES_CTRL1_TX_PDOWN |
		  MV88E6393X_SERDES_CTRL1_RX_PDOWN;

	return mv88e639x_modify(mpcs, MV88E6393X_SERDES_CTRL1, val,
				enable ? 0 : val);
}

/* mv88e6393x family errata 4.6:
 * Cannot clear PwrDn bit on SERDES if device is configured CPU_MGD mode or
 * P0_mode is configured for [x]MII.
 * Workaround: Set SERDES register 4.F002 bit 5=0 and bit 15=1.
 *
 * It seems that after this workaround the SERDES is automatically powered up
 * (the bit is cleared), so power it down.
 */
static int mv88e6393x_erratum_4_6(struct mv88e639x_pcs *mpcs)
{
	int err;

	err = mv88e639x_modify(mpcs, MV88E6393X_SERDES_POC,
			       MV88E6393X_SERDES_POC_PDOWN |
			       MV88E6393X_SERDES_POC_RESET,
			       MV88E6393X_SERDES_POC_RESET);
	if (err)
		return err;

	err = mv88e639x_modify(mpcs, MV88E6390_SGMII_BMCR,
			       BMCR_PDOWN, BMCR_PDOWN);
	if (err)
		return err;

	err = mv88e639x_sgmii_pcs_control_pwr(mpcs, false);
	if (err)
		return err;

	return mv88e6393x_power_lane(mpcs, false);
}

/* mv88e6393x family errata 4.8:
 * When a SERDES port is operating in 1000BASE-X or SGMII mode link may not
 * come up after hardware reset or software reset of SERDES core. Workaround
 * is to write SERDES register 4.F074.14=1 for only those modes and 0 in all
 * other modes.
 */
static int mv88e6393x_erratum_4_8(struct mv88e639x_pcs *mpcs)
{
	u16 reg, poc;
	int err;

	err = mv88e639x_read(mpcs, MV88E6393X_SERDES_POC, &poc);
	if (err)
		return err;

	poc &= MV88E6393X_SERDES_POC_PCS_MASK;
	if (poc == MV88E6393X_SERDES_POC_PCS_1000BASEX ||
	    poc == MV88E6393X_SERDES_POC_PCS_SGMII_PHY ||
	    poc == MV88E6393X_SERDES_POC_PCS_SGMII_MAC)
		reg = MV88E6393X_ERRATA_4_8_BIT;
	else
		reg = 0;

	return mv88e639x_modify(mpcs, MV88E6393X_ERRATA_4_8_REG,
				MV88E6393X_ERRATA_4_8_BIT, reg);
}

/* mv88e6393x family errata 5.2:
 * For optimal signal integrity the following sequence should be applied to
 * SERDES operating in 10G mode. These registers only apply to 10G operation
 * and have no effect on other speeds.
 */
static int mv88e6393x_erratum_5_2(struct mv88e639x_pcs *mpcs)
{
	static const struct {
		u16 dev, reg, val, mask;
	} fixes[] = {
		{ MDIO_MMD_VEND1, 0x8093, 0xcb5a, 0xffff },
		{ MDIO_MMD_VEND1, 0x8171, 0x7088, 0xffff },
		{ MDIO_MMD_VEND1, 0x80c9, 0x311a, 0xffff },
		{ MDIO_MMD_VEND1, 0x80a2, 0x8000, 0xff7f },
		{ MDIO_MMD_VEND1, 0x80a9, 0x0000, 0xfff0 },
		{ MDIO_MMD_VEND1, 0x80a3, 0x0000, 0xf8ff },
		{ MDIO_MMD_PHYXS, MV88E6393X_SERDES_POC,
		  MV88E6393X_SERDES_POC_RESET, MV88E6393X_SERDES_POC_RESET },
	};
	int err, i;

	for (i = 0; i < ARRAY_SIZE(fixes); ++i) {
		err = mdiodev_c45_modify(&mpcs->mdio, fixes[i].dev,
					 fixes[i].reg, fixes[i].mask,
					 fixes[i].val);
		if (err)
			return err;
	}

	return 0;
}

/* Inband AN is broken on Amethyst in 2500base-x mode when set by standard
 * mechanism (via cmode).
 * We can get around this by configuring the PCS mode to 1000base-x and then
 * writing value 0x58 to register 1e.8000. (This must be done while SerDes
 * receiver and transmitter are disabled, which is, when this function is
 * called.)
 * It seem that when we do this configuration to 2500base-x mode (by changing
 * PCS mode to 1000base-x and frequency to 3.125 GHz from 1.25 GHz) and then
 * configure to sgmii or 1000base-x, the device thinks that it already has
 * SerDes at 1.25 GHz and does not change the 1e.8000 register, leaving SerDes
 * at 3.125 GHz.
 * To avoid this, change PCS mode back to 2500base-x when disabling SerDes from
 * 2500base-x mode.
 */
static int mv88e6393x_fix_2500basex_an(struct mv88e639x_pcs *mpcs, bool on)
{
	u16 reg;
	int err;

	if (on)
		reg = MV88E6393X_SERDES_POC_PCS_1000BASEX |
		      MV88E6393X_SERDES_POC_AN;
	else
		reg = MV88E6393X_SERDES_POC_PCS_2500BASEX;

	reg |= MV88E6393X_SERDES_POC_RESET;

	err = mv88e639x_modify(mpcs, MV88E6393X_SERDES_POC,
			       MV88E6393X_SERDES_POC_PCS_MASK |
			       MV88E6393X_SERDES_POC_AN |
			       MV88E6393X_SERDES_POC_RESET, reg);
	if (err)
		return err;

	return mdiodev_c45_write(&mpcs->mdio, MDIO_MMD_VEND1, 0x8000, 0x58);
}

static int mv88e6393x_sgmii_apply_2500basex_an(struct mv88e639x_pcs *mpcs,
					       phy_interface_t interface,
					       bool enable)
{
	int err;

	if (interface != PHY_INTERFACE_MODE_2500BASEX)
		return 0;

	err = mv88e6393x_fix_2500basex_an(mpcs, enable);
	if (err)
		dev_err(mpcs->mdio.dev.parent,
			"failed to %s 2500basex fix: %pe\n",
			enable ? "enable" : "disable", ERR_PTR(err));

	return err;
}

static void mv88e6393x_sgmii_pcs_disable(struct phylink_pcs *pcs)
{
	struct mv88e639x_pcs *mpcs = sgmii_pcs_to_mv88e639x_pcs(pcs);

	mv88e639x_sgmii_pcs_disable(pcs);
	mv88e6393x_power_lane(mpcs, false);
	mv88e6393x_sgmii_apply_2500basex_an(mpcs, mpcs->interface, false);
}

static void mv88e6393x_sgmii_pcs_pre_config(struct phylink_pcs *pcs,
					    phy_interface_t interface)
{
	struct mv88e639x_pcs *mpcs = sgmii_pcs_to_mv88e639x_pcs(pcs);

	mv88e639x_sgmii_pcs_pre_config(pcs, interface);
	mv88e6393x_power_lane(mpcs, false);
	mv88e6393x_sgmii_apply_2500basex_an(mpcs, mpcs->interface, false);
}

static int mv88e6393x_sgmii_pcs_post_config(struct phylink_pcs *pcs,
					    phy_interface_t interface)
{
	struct mv88e639x_pcs *mpcs = sgmii_pcs_to_mv88e639x_pcs(pcs);
	int err;

	err = mv88e6393x_erratum_4_8(mpcs);
	if (err)
		return err;

	err = mv88e6393x_sgmii_apply_2500basex_an(mpcs, interface, true);
	if (err)
		return err;

	err = mv88e6393x_power_lane(mpcs, true);
	if (err)
		return err;

	return mv88e639x_sgmii_pcs_post_config(pcs, interface);
}

static const struct phylink_pcs_ops mv88e6393x_sgmii_pcs_ops = {
	.pcs_enable = mv88e639x_sgmii_pcs_enable,
	.pcs_disable = mv88e6393x_sgmii_pcs_disable,
	.pcs_pre_config = mv88e6393x_sgmii_pcs_pre_config,
	.pcs_post_config = mv88e6393x_sgmii_pcs_post_config,
	.pcs_get_state = mv88e639x_sgmii_pcs_get_state,
	.pcs_an_restart = mv88e639x_sgmii_pcs_an_restart,
	.pcs_config = mv88e639x_sgmii_pcs_config,
	.pcs_link_up = mv88e639x_sgmii_pcs_link_up,
};

static irqreturn_t mv88e6393x_xg_handle_irq(struct mv88e639x_pcs *mpcs)
{
	u16 int_status, stat1;
	bool link_down;
	int err;

	err = mv88e639x_read(mpcs, MV88E6393X_10G_INT_STATUS, &int_status);
	if (err)
		return IRQ_NONE;

	if (int_status & MV88E6393X_10G_INT_LINK_CHANGE) {
		err = mv88e639x_read(mpcs, MV88E6390_10G_STAT1, &stat1);
		if (err)
			return IRQ_NONE;

		link_down = !(stat1 & MDIO_STAT1_LSTATUS);

		phylink_pcs_change(&mpcs->xg_pcs, !link_down);

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int mv88e6393x_xg_control_irq(struct mv88e639x_pcs *mpcs, bool enable)
{
	u16 val = 0;

	if (enable)
		val = MV88E6393X_10G_INT_LINK_CHANGE;

	return mv88e639x_modify(mpcs, MV88E6393X_10G_INT_ENABLE,
				MV88E6393X_10G_INT_LINK_CHANGE, val);
}

static int mv88e6393x_xg_pcs_enable(struct phylink_pcs *pcs)
{
	struct mv88e639x_pcs *mpcs = xg_pcs_to_mv88e639x_pcs(pcs);

	mpcs->handle_irq = mv88e6393x_xg_handle_irq;

	return mv88e6393x_xg_control_irq(mpcs, !!mpcs->irq);
}

static void mv88e6393x_xg_pcs_disable(struct phylink_pcs *pcs)
{
	struct mv88e639x_pcs *mpcs = xg_pcs_to_mv88e639x_pcs(pcs);

	mv88e6393x_xg_control_irq(mpcs, false);
	mv88e639x_xg_pcs_disable(mpcs);
	mv88e6393x_power_lane(mpcs, false);
}

/* The PCS has to be powered down while CMODE is changed */
static void mv88e6393x_xg_pcs_pre_config(struct phylink_pcs *pcs,
					 phy_interface_t interface)
{
	struct mv88e639x_pcs *mpcs = xg_pcs_to_mv88e639x_pcs(pcs);

	mv88e639x_xg_pcs_disable(mpcs);
	mv88e6393x_power_lane(mpcs, false);
}

static int mv88e6393x_xg_pcs_post_config(struct phylink_pcs *pcs,
					 phy_interface_t interface)
{
	struct mv88e639x_pcs *mpcs = xg_pcs_to_mv88e639x_pcs(pcs);
	int err;

	if (interface == PHY_INTERFACE_MODE_10GBASER) {
		err = mv88e6393x_erratum_5_2(mpcs);
		if (err)
			return err;
	}

	err = mv88e6393x_power_lane(mpcs, true);
	if (err)
		return err;

	return mv88e639x_xg_pcs_enable(mpcs);
}

static const struct phylink_pcs_ops mv88e6393x_xg_pcs_ops = {
	.pcs_enable = mv88e6393x_xg_pcs_enable,
	.pcs_disable = mv88e6393x_xg_pcs_disable,
	.pcs_pre_config = mv88e6393x_xg_pcs_pre_config,
	.pcs_post_config = mv88e6393x_xg_pcs_post_config,
	.pcs_get_state = mv88e639x_xg_pcs_get_state,
	.pcs_config = mv88e639x_xg_pcs_config,
};

static int mv88e6393x_pcs_init(struct mv88e6xxx_chip *chip, int port)
{
	struct mv88e639x_pcs *mpcs;
	struct mii_bus *bus;
	struct device *dev;
	int lane, err;

	lane = mv88e6xxx_serdes_get_lane(chip, port);
	if (lane < 0)
		return 0;

	bus = mv88e6xxx_default_mdio_bus(chip);
	dev = chip->dev;

	mpcs = mv88e639x_pcs_alloc(dev, bus, lane, port);
	if (!mpcs)
		return -ENOMEM;

	mpcs->sgmii_pcs.ops = &mv88e6393x_sgmii_pcs_ops;
	mpcs->sgmii_pcs.neg_mode = true;
	mpcs->xg_pcs.ops = &mv88e6393x_xg_pcs_ops;
	mpcs->xg_pcs.neg_mode = true;
	mpcs->supports_5g = true;

	err = mv88e6393x_erratum_4_6(mpcs);
	if (err)
		goto err_free;

	err = mv88e639x_pcs_setup_irq(mpcs, chip, port);
	if (err)
		goto err_free;

	chip->ports[port].pcs_private = mpcs;

	return 0;

err_free:
	kfree(mpcs);
	return err;
}

const struct mv88e6xxx_pcs_ops mv88e6393x_pcs_ops = {
	.pcs_init = mv88e6393x_pcs_init,
	.pcs_teardown = mv88e639x_pcs_teardown,
	.pcs_select = mv88e639x_pcs_select,
};
