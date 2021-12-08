// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Marvell 88E6xxx SERDES manipulation, via SMI bus
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

static int mv88e6352_serdes_read(struct mv88e6xxx_chip *chip, int reg,
				 u16 *val)
{
	return mv88e6xxx_phy_page_read(chip, MV88E6352_ADDR_SERDES,
				       MV88E6352_SERDES_PAGE_FIBER,
				       reg, val);
}

static int mv88e6352_serdes_write(struct mv88e6xxx_chip *chip, int reg,
				  u16 val)
{
	return mv88e6xxx_phy_page_write(chip, MV88E6352_ADDR_SERDES,
					MV88E6352_SERDES_PAGE_FIBER,
					reg, val);
}

static int mv88e6390_serdes_read(struct mv88e6xxx_chip *chip,
				 int lane, int device, int reg, u16 *val)
{
	int reg_c45 = MII_ADDR_C45 | device << 16 | reg;

	return mv88e6xxx_phy_read(chip, lane, reg_c45, val);
}

static int mv88e6390_serdes_write(struct mv88e6xxx_chip *chip,
				  int lane, int device, int reg, u16 val)
{
	int reg_c45 = MII_ADDR_C45 | device << 16 | reg;

	return mv88e6xxx_phy_write(chip, lane, reg_c45, val);
}

static int mv88e6xxx_serdes_pcs_get_state(struct mv88e6xxx_chip *chip,
					  u16 ctrl, u16 status, u16 lpa,
					  struct phylink_link_state *state)
{
	state->link = !!(status & MV88E6390_SGMII_PHY_STATUS_LINK);

	if (status & MV88E6390_SGMII_PHY_STATUS_SPD_DPL_VALID) {
		/* The Spped and Duplex Resolved register is 1 if AN is enabled
		 * and complete, or if AN is disabled. So with disabled AN we
		 * still get here on link up. But we want to set an_complete
		 * only if AN was enabled, thus we look at BMCR_ANENABLE.
		 * (According to 802.3-2008 section 22.2.4.2.10, we should be
		 *  able to get this same value from BMSR_ANEGCAPABLE, but tests
		 *  show that these Marvell PHYs don't conform to this part of
		 *  the specificaion - BMSR_ANEGCAPABLE is simply always 1.)
		 */
		state->an_complete = !!(ctrl & BMCR_ANENABLE);
		state->duplex = status &
				MV88E6390_SGMII_PHY_STATUS_DUPLEX_FULL ?
			                         DUPLEX_FULL : DUPLEX_HALF;

		if (status & MV88E6390_SGMII_PHY_STATUS_TX_PAUSE)
			state->pause |= MLO_PAUSE_TX;
		if (status & MV88E6390_SGMII_PHY_STATUS_RX_PAUSE)
			state->pause |= MLO_PAUSE_RX;

		switch (status & MV88E6390_SGMII_PHY_STATUS_SPEED_MASK) {
		case MV88E6390_SGMII_PHY_STATUS_SPEED_1000:
			if (state->interface == PHY_INTERFACE_MODE_2500BASEX)
				state->speed = SPEED_2500;
			else
				state->speed = SPEED_1000;
			break;
		case MV88E6390_SGMII_PHY_STATUS_SPEED_100:
			state->speed = SPEED_100;
			break;
		case MV88E6390_SGMII_PHY_STATUS_SPEED_10:
			state->speed = SPEED_10;
			break;
		default:
			dev_err(chip->dev, "invalid PHY speed\n");
			return -EINVAL;
		}
	} else if (state->link &&
		   state->interface != PHY_INTERFACE_MODE_SGMII) {
		/* If Speed and Duplex Resolved register is 0 and link is up, it
		 * means that AN was enabled, but link partner had it disabled
		 * and the PHY invoked the Auto-Negotiation Bypass feature and
		 * linked anyway.
		 */
		state->duplex = DUPLEX_FULL;
		if (state->interface == PHY_INTERFACE_MODE_2500BASEX)
			state->speed = SPEED_2500;
		else
			state->speed = SPEED_1000;
	} else {
		state->link = false;
	}

	if (state->interface == PHY_INTERFACE_MODE_2500BASEX)
		mii_lpa_mod_linkmode_x(state->lp_advertising, lpa,
				       ETHTOOL_LINK_MODE_2500baseX_Full_BIT);
	else if (state->interface == PHY_INTERFACE_MODE_1000BASEX)
		mii_lpa_mod_linkmode_x(state->lp_advertising, lpa,
				       ETHTOOL_LINK_MODE_1000baseX_Full_BIT);

	return 0;
}

int mv88e6352_serdes_power(struct mv88e6xxx_chip *chip, int port, int lane,
			   bool up)
{
	u16 val, new_val;
	int err;

	err = mv88e6352_serdes_read(chip, MII_BMCR, &val);
	if (err)
		return err;

	if (up)
		new_val = val & ~BMCR_PDOWN;
	else
		new_val = val | BMCR_PDOWN;

	if (val != new_val)
		err = mv88e6352_serdes_write(chip, MII_BMCR, new_val);

	return err;
}

int mv88e6352_serdes_pcs_config(struct mv88e6xxx_chip *chip, int port,
				int lane, unsigned int mode,
				phy_interface_t interface,
				const unsigned long *advertise)
{
	u16 adv, bmcr, val;
	bool changed;
	int err;

	switch (interface) {
	case PHY_INTERFACE_MODE_SGMII:
		adv = 0x0001;
		break;

	case PHY_INTERFACE_MODE_1000BASEX:
		adv = linkmode_adv_to_mii_adv_x(advertise,
					ETHTOOL_LINK_MODE_1000baseX_Full_BIT);
		break;

	default:
		return 0;
	}

	err = mv88e6352_serdes_read(chip, MII_ADVERTISE, &val);
	if (err)
		return err;

	changed = val != adv;
	if (changed) {
		err = mv88e6352_serdes_write(chip, MII_ADVERTISE, adv);
		if (err)
			return err;
	}

	err = mv88e6352_serdes_read(chip, MII_BMCR, &val);
	if (err)
		return err;

	if (phylink_autoneg_inband(mode))
		bmcr = val | BMCR_ANENABLE;
	else
		bmcr = val & ~BMCR_ANENABLE;

	if (bmcr == val)
		return changed;

	return mv88e6352_serdes_write(chip, MII_BMCR, bmcr);
}

int mv88e6352_serdes_pcs_get_state(struct mv88e6xxx_chip *chip, int port,
				   int lane, struct phylink_link_state *state)
{
	u16 lpa, status, ctrl;
	int err;

	err = mv88e6352_serdes_read(chip, MII_BMCR, &ctrl);
	if (err) {
		dev_err(chip->dev, "can't read Serdes PHY control: %d\n", err);
		return err;
	}

	err = mv88e6352_serdes_read(chip, 0x11, &status);
	if (err) {
		dev_err(chip->dev, "can't read Serdes PHY status: %d\n", err);
		return err;
	}

	err = mv88e6352_serdes_read(chip, MII_LPA, &lpa);
	if (err) {
		dev_err(chip->dev, "can't read Serdes PHY LPA: %d\n", err);
		return err;
	}

	return mv88e6xxx_serdes_pcs_get_state(chip, ctrl, status, lpa, state);
}

int mv88e6352_serdes_pcs_an_restart(struct mv88e6xxx_chip *chip, int port,
				    int lane)
{
	u16 bmcr;
	int err;

	err = mv88e6352_serdes_read(chip, MII_BMCR, &bmcr);
	if (err)
		return err;

	return mv88e6352_serdes_write(chip, MII_BMCR, bmcr | BMCR_ANRESTART);
}

int mv88e6352_serdes_pcs_link_up(struct mv88e6xxx_chip *chip, int port,
				 int lane, int speed, int duplex)
{
	u16 val, bmcr;
	int err;

	err = mv88e6352_serdes_read(chip, MII_BMCR, &val);
	if (err)
		return err;

	bmcr = val & ~(BMCR_SPEED100 | BMCR_FULLDPLX | BMCR_SPEED1000);
	switch (speed) {
	case SPEED_1000:
		bmcr |= BMCR_SPEED1000;
		break;
	case SPEED_100:
		bmcr |= BMCR_SPEED100;
		break;
	case SPEED_10:
		break;
	}

	if (duplex == DUPLEX_FULL)
		bmcr |= BMCR_FULLDPLX;

	if (bmcr == val)
		return 0;

	return mv88e6352_serdes_write(chip, MII_BMCR, bmcr);
}

int mv88e6352_serdes_get_lane(struct mv88e6xxx_chip *chip, int port)
{
	u8 cmode = chip->ports[port].cmode;
	int lane = -ENODEV;

	if ((cmode == MV88E6XXX_PORT_STS_CMODE_100BASEX) ||
	    (cmode == MV88E6XXX_PORT_STS_CMODE_1000BASEX) ||
	    (cmode == MV88E6XXX_PORT_STS_CMODE_SGMII))
		lane = 0xff; /* Unused */

	return lane;
}

static bool mv88e6352_port_has_serdes(struct mv88e6xxx_chip *chip, int port)
{
	if (mv88e6xxx_serdes_get_lane(chip, port) >= 0)
		return true;

	return false;
}

struct mv88e6352_serdes_hw_stat {
	char string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int reg;
};

static struct mv88e6352_serdes_hw_stat mv88e6352_serdes_hw_stats[] = {
	{ "serdes_fibre_rx_error", 16, 21 },
	{ "serdes_PRBS_error", 32, 24 },
};

int mv88e6352_serdes_get_sset_count(struct mv88e6xxx_chip *chip, int port)
{
	if (mv88e6352_port_has_serdes(chip, port))
		return ARRAY_SIZE(mv88e6352_serdes_hw_stats);

	return 0;
}

int mv88e6352_serdes_get_strings(struct mv88e6xxx_chip *chip,
				 int port, uint8_t *data)
{
	struct mv88e6352_serdes_hw_stat *stat;
	int i;

	if (!mv88e6352_port_has_serdes(chip, port))
		return 0;

	for (i = 0; i < ARRAY_SIZE(mv88e6352_serdes_hw_stats); i++) {
		stat = &mv88e6352_serdes_hw_stats[i];
		memcpy(data + i * ETH_GSTRING_LEN, stat->string,
		       ETH_GSTRING_LEN);
	}
	return ARRAY_SIZE(mv88e6352_serdes_hw_stats);
}

static uint64_t mv88e6352_serdes_get_stat(struct mv88e6xxx_chip *chip,
					  struct mv88e6352_serdes_hw_stat *stat)
{
	u64 val = 0;
	u16 reg;
	int err;

	err = mv88e6352_serdes_read(chip, stat->reg, &reg);
	if (err) {
		dev_err(chip->dev, "failed to read statistic\n");
		return 0;
	}

	val = reg;

	if (stat->sizeof_stat == 32) {
		err = mv88e6352_serdes_read(chip, stat->reg + 1, &reg);
		if (err) {
			dev_err(chip->dev, "failed to read statistic\n");
			return 0;
		}
		val = val << 16 | reg;
	}

	return val;
}

int mv88e6352_serdes_get_stats(struct mv88e6xxx_chip *chip, int port,
			       uint64_t *data)
{
	struct mv88e6xxx_port *mv88e6xxx_port = &chip->ports[port];
	struct mv88e6352_serdes_hw_stat *stat;
	u64 value;
	int i;

	if (!mv88e6352_port_has_serdes(chip, port))
		return 0;

	BUILD_BUG_ON(ARRAY_SIZE(mv88e6352_serdes_hw_stats) >
		     ARRAY_SIZE(mv88e6xxx_port->serdes_stats));

	for (i = 0; i < ARRAY_SIZE(mv88e6352_serdes_hw_stats); i++) {
		stat = &mv88e6352_serdes_hw_stats[i];
		value = mv88e6352_serdes_get_stat(chip, stat);
		mv88e6xxx_port->serdes_stats[i] += value;
		data[i] = mv88e6xxx_port->serdes_stats[i];
	}

	return ARRAY_SIZE(mv88e6352_serdes_hw_stats);
}

static void mv88e6352_serdes_irq_link(struct mv88e6xxx_chip *chip, int port)
{
	u16 bmsr;
	int err;

	/* If the link has dropped, we want to know about it. */
	err = mv88e6352_serdes_read(chip, MII_BMSR, &bmsr);
	if (err) {
		dev_err(chip->dev, "can't read Serdes BMSR: %d\n", err);
		return;
	}

	dsa_port_phylink_mac_change(chip->ds, port, !!(bmsr & BMSR_LSTATUS));
}

irqreturn_t mv88e6352_serdes_irq_status(struct mv88e6xxx_chip *chip, int port,
					int lane)
{
	irqreturn_t ret = IRQ_NONE;
	u16 status;
	int err;

	err = mv88e6352_serdes_read(chip, MV88E6352_SERDES_INT_STATUS, &status);
	if (err)
		return ret;

	if (status & MV88E6352_SERDES_INT_LINK_CHANGE) {
		ret = IRQ_HANDLED;
		mv88e6352_serdes_irq_link(chip, port);
	}

	return ret;
}

int mv88e6352_serdes_irq_enable(struct mv88e6xxx_chip *chip, int port, int lane,
				bool enable)
{
	u16 val = 0;

	if (enable)
		val |= MV88E6352_SERDES_INT_LINK_CHANGE;

	return mv88e6352_serdes_write(chip, MV88E6352_SERDES_INT_ENABLE, val);
}

unsigned int mv88e6352_serdes_irq_mapping(struct mv88e6xxx_chip *chip, int port)
{
	return irq_find_mapping(chip->g2_irq.domain, MV88E6352_SERDES_IRQ);
}

int mv88e6352_serdes_get_regs_len(struct mv88e6xxx_chip *chip, int port)
{
	if (!mv88e6352_port_has_serdes(chip, port))
		return 0;

	return 32 * sizeof(u16);
}

void mv88e6352_serdes_get_regs(struct mv88e6xxx_chip *chip, int port, void *_p)
{
	u16 *p = _p;
	u16 reg;
	int err;
	int i;

	if (!mv88e6352_port_has_serdes(chip, port))
		return;

	for (i = 0 ; i < 32; i++) {
		err = mv88e6352_serdes_read(chip, i, &reg);
		if (!err)
			p[i] = reg;
	}
}

int mv88e6341_serdes_get_lane(struct mv88e6xxx_chip *chip, int port)
{
	u8 cmode = chip->ports[port].cmode;
	int lane = -ENODEV;

	switch (port) {
	case 5:
		if (cmode == MV88E6XXX_PORT_STS_CMODE_1000BASEX ||
		    cmode == MV88E6XXX_PORT_STS_CMODE_SGMII ||
		    cmode == MV88E6XXX_PORT_STS_CMODE_2500BASEX)
			lane = MV88E6341_PORT5_LANE;
		break;
	}

	return lane;
}

int mv88e6185_serdes_power(struct mv88e6xxx_chip *chip, int port, int lane,
			   bool up)
{
	/* The serdes power can't be controlled on this switch chip but we need
	 * to supply this function to avoid returning -EOPNOTSUPP in
	 * mv88e6xxx_serdes_power_up/mv88e6xxx_serdes_power_down
	 */
	return 0;
}

int mv88e6185_serdes_get_lane(struct mv88e6xxx_chip *chip, int port)
{
	/* There are no configurable serdes lanes on this switch chip but we
	 * need to return a non-negative lane number so that callers of
	 * mv88e6xxx_serdes_get_lane() know this is a serdes port.
	 */
	switch (chip->ports[port].cmode) {
	case MV88E6185_PORT_STS_CMODE_SERDES:
	case MV88E6185_PORT_STS_CMODE_1000BASE_X:
		return 0;
	default:
		return -ENODEV;
	}
}

int mv88e6185_serdes_pcs_get_state(struct mv88e6xxx_chip *chip, int port,
				   int lane, struct phylink_link_state *state)
{
	int err;
	u16 status;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_STS, &status);
	if (err)
		return err;

	state->link = !!(status & MV88E6XXX_PORT_STS_LINK);

	if (state->link) {
		state->duplex = status & MV88E6XXX_PORT_STS_DUPLEX ? DUPLEX_FULL : DUPLEX_HALF;

		switch (status &  MV88E6XXX_PORT_STS_SPEED_MASK) {
		case MV88E6XXX_PORT_STS_SPEED_1000:
			state->speed = SPEED_1000;
			break;
		case MV88E6XXX_PORT_STS_SPEED_100:
			state->speed = SPEED_100;
			break;
		case MV88E6XXX_PORT_STS_SPEED_10:
			state->speed = SPEED_10;
			break;
		default:
			dev_err(chip->dev, "invalid PHY speed\n");
			return -EINVAL;
		}
	} else {
		state->duplex = DUPLEX_UNKNOWN;
		state->speed = SPEED_UNKNOWN;
	}

	return 0;
}

int mv88e6097_serdes_irq_enable(struct mv88e6xxx_chip *chip, int port, int lane,
				bool enable)
{
	u8 cmode = chip->ports[port].cmode;

	/* The serdes interrupts are enabled in the G2_INT_MASK register. We
	 * need to return 0 to avoid returning -EOPNOTSUPP in
	 * mv88e6xxx_serdes_irq_enable/mv88e6xxx_serdes_irq_disable
	 */
	switch (cmode) {
	case MV88E6185_PORT_STS_CMODE_SERDES:
	case MV88E6185_PORT_STS_CMODE_1000BASE_X:
		return 0;
	}

	return -EOPNOTSUPP;
}

static void mv88e6097_serdes_irq_link(struct mv88e6xxx_chip *chip, int port)
{
	u16 status;
	int err;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_STS, &status);
	if (err) {
		dev_err(chip->dev, "can't read port status: %d\n", err);
		return;
	}

	dsa_port_phylink_mac_change(chip->ds, port, !!(status & MV88E6XXX_PORT_STS_LINK));
}

irqreturn_t mv88e6097_serdes_irq_status(struct mv88e6xxx_chip *chip, int port,
					int lane)
{
	u8 cmode = chip->ports[port].cmode;

	switch (cmode) {
	case MV88E6185_PORT_STS_CMODE_SERDES:
	case MV88E6185_PORT_STS_CMODE_1000BASE_X:
		mv88e6097_serdes_irq_link(chip, port);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

int mv88e6390_serdes_get_lane(struct mv88e6xxx_chip *chip, int port)
{
	u8 cmode = chip->ports[port].cmode;
	int lane = -ENODEV;

	switch (port) {
	case 9:
		if (cmode == MV88E6XXX_PORT_STS_CMODE_1000BASEX ||
		    cmode == MV88E6XXX_PORT_STS_CMODE_SGMII ||
		    cmode == MV88E6XXX_PORT_STS_CMODE_2500BASEX)
			lane = MV88E6390_PORT9_LANE0;
		break;
	case 10:
		if (cmode == MV88E6XXX_PORT_STS_CMODE_1000BASEX ||
		    cmode == MV88E6XXX_PORT_STS_CMODE_SGMII ||
		    cmode == MV88E6XXX_PORT_STS_CMODE_2500BASEX)
			lane = MV88E6390_PORT10_LANE0;
		break;
	}

	return lane;
}

int mv88e6390x_serdes_get_lane(struct mv88e6xxx_chip *chip, int port)
{
	u8 cmode_port = chip->ports[port].cmode;
	u8 cmode_port10 = chip->ports[10].cmode;
	u8 cmode_port9 = chip->ports[9].cmode;
	int lane = -ENODEV;

	switch (port) {
	case 2:
		if (cmode_port9 == MV88E6XXX_PORT_STS_CMODE_1000BASEX ||
		    cmode_port9 == MV88E6XXX_PORT_STS_CMODE_SGMII ||
		    cmode_port9 == MV88E6XXX_PORT_STS_CMODE_2500BASEX)
			if (cmode_port == MV88E6XXX_PORT_STS_CMODE_1000BASEX)
				lane = MV88E6390_PORT9_LANE1;
		break;
	case 3:
		if (cmode_port9 == MV88E6XXX_PORT_STS_CMODE_1000BASEX ||
		    cmode_port9 == MV88E6XXX_PORT_STS_CMODE_SGMII ||
		    cmode_port9 == MV88E6XXX_PORT_STS_CMODE_2500BASEX ||
		    cmode_port9 == MV88E6XXX_PORT_STS_CMODE_RXAUI)
			if (cmode_port == MV88E6XXX_PORT_STS_CMODE_1000BASEX)
				lane = MV88E6390_PORT9_LANE2;
		break;
	case 4:
		if (cmode_port9 == MV88E6XXX_PORT_STS_CMODE_1000BASEX ||
		    cmode_port9 == MV88E6XXX_PORT_STS_CMODE_SGMII ||
		    cmode_port9 == MV88E6XXX_PORT_STS_CMODE_2500BASEX ||
		    cmode_port9 == MV88E6XXX_PORT_STS_CMODE_RXAUI)
			if (cmode_port == MV88E6XXX_PORT_STS_CMODE_1000BASEX)
				lane = MV88E6390_PORT9_LANE3;
		break;
	case 5:
		if (cmode_port10 == MV88E6XXX_PORT_STS_CMODE_1000BASEX ||
		    cmode_port10 == MV88E6XXX_PORT_STS_CMODE_SGMII ||
		    cmode_port10 == MV88E6XXX_PORT_STS_CMODE_2500BASEX)
			if (cmode_port == MV88E6XXX_PORT_STS_CMODE_1000BASEX)
				lane = MV88E6390_PORT10_LANE1;
		break;
	case 6:
		if (cmode_port10 == MV88E6XXX_PORT_STS_CMODE_1000BASEX ||
		    cmode_port10 == MV88E6XXX_PORT_STS_CMODE_SGMII ||
		    cmode_port10 == MV88E6XXX_PORT_STS_CMODE_2500BASEX ||
		    cmode_port10 == MV88E6XXX_PORT_STS_CMODE_RXAUI)
			if (cmode_port == MV88E6XXX_PORT_STS_CMODE_1000BASEX)
				lane = MV88E6390_PORT10_LANE2;
		break;
	case 7:
		if (cmode_port10 == MV88E6XXX_PORT_STS_CMODE_1000BASEX ||
		    cmode_port10 == MV88E6XXX_PORT_STS_CMODE_SGMII ||
		    cmode_port10 == MV88E6XXX_PORT_STS_CMODE_2500BASEX ||
		    cmode_port10 == MV88E6XXX_PORT_STS_CMODE_RXAUI)
			if (cmode_port == MV88E6XXX_PORT_STS_CMODE_1000BASEX)
				lane = MV88E6390_PORT10_LANE3;
		break;
	case 9:
		if (cmode_port9 == MV88E6XXX_PORT_STS_CMODE_1000BASEX ||
		    cmode_port9 == MV88E6XXX_PORT_STS_CMODE_SGMII ||
		    cmode_port9 == MV88E6XXX_PORT_STS_CMODE_2500BASEX ||
		    cmode_port9 == MV88E6XXX_PORT_STS_CMODE_XAUI ||
		    cmode_port9 == MV88E6XXX_PORT_STS_CMODE_RXAUI)
			lane = MV88E6390_PORT9_LANE0;
		break;
	case 10:
		if (cmode_port10 == MV88E6XXX_PORT_STS_CMODE_1000BASEX ||
		    cmode_port10 == MV88E6XXX_PORT_STS_CMODE_SGMII ||
		    cmode_port10 == MV88E6XXX_PORT_STS_CMODE_2500BASEX ||
		    cmode_port10 == MV88E6XXX_PORT_STS_CMODE_XAUI ||
		    cmode_port10 == MV88E6XXX_PORT_STS_CMODE_RXAUI)
			lane = MV88E6390_PORT10_LANE0;
		break;
	}

	return lane;
}

/* Only Ports 0, 9 and 10 have SERDES lanes. Return the SERDES lane address
 * a port is using else Returns -ENODEV.
 */
int mv88e6393x_serdes_get_lane(struct mv88e6xxx_chip *chip, int port)
{
	u8 cmode = chip->ports[port].cmode;
	int lane = -ENODEV;

	if (port != 0 && port != 9 && port != 10)
		return -EOPNOTSUPP;

	if (cmode == MV88E6XXX_PORT_STS_CMODE_1000BASEX ||
	    cmode == MV88E6XXX_PORT_STS_CMODE_SGMII ||
	    cmode == MV88E6XXX_PORT_STS_CMODE_2500BASEX ||
	    cmode == MV88E6393X_PORT_STS_CMODE_5GBASER ||
	    cmode == MV88E6393X_PORT_STS_CMODE_10GBASER)
		lane = port;

	return lane;
}

/* Set power up/down for 10GBASE-R and 10GBASE-X4/X2 */
static int mv88e6390_serdes_power_10g(struct mv88e6xxx_chip *chip, int lane,
				      bool up)
{
	u16 val, new_val;
	int err;

	err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				    MV88E6390_10G_CTRL1, &val);

	if (err)
		return err;

	if (up)
		new_val = val & ~(MDIO_CTRL1_RESET |
				  MDIO_PCS_CTRL1_LOOPBACK |
				  MDIO_CTRL1_LPOWER);
	else
		new_val = val | MDIO_CTRL1_LPOWER;

	if (val != new_val)
		err = mv88e6390_serdes_write(chip, lane, MDIO_MMD_PHYXS,
					     MV88E6390_10G_CTRL1, new_val);

	return err;
}

/* Set power up/down for SGMII and 1000Base-X */
static int mv88e6390_serdes_power_sgmii(struct mv88e6xxx_chip *chip, int lane,
					bool up)
{
	u16 val, new_val;
	int err;

	err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				    MV88E6390_SGMII_BMCR, &val);
	if (err)
		return err;

	if (up)
		new_val = val & ~(BMCR_RESET | BMCR_LOOPBACK | BMCR_PDOWN);
	else
		new_val = val | BMCR_PDOWN;

	if (val != new_val)
		err = mv88e6390_serdes_write(chip, lane, MDIO_MMD_PHYXS,
					     MV88E6390_SGMII_BMCR, new_val);

	return err;
}

struct mv88e6390_serdes_hw_stat {
	char string[ETH_GSTRING_LEN];
	int reg;
};

static struct mv88e6390_serdes_hw_stat mv88e6390_serdes_hw_stats[] = {
	{ "serdes_rx_pkts", 0xf021 },
	{ "serdes_rx_bytes", 0xf024 },
	{ "serdes_rx_pkts_error", 0xf027 },
};

int mv88e6390_serdes_get_sset_count(struct mv88e6xxx_chip *chip, int port)
{
	if (mv88e6xxx_serdes_get_lane(chip, port) < 0)
		return 0;

	return ARRAY_SIZE(mv88e6390_serdes_hw_stats);
}

int mv88e6390_serdes_get_strings(struct mv88e6xxx_chip *chip,
				 int port, uint8_t *data)
{
	struct mv88e6390_serdes_hw_stat *stat;
	int i;

	if (mv88e6xxx_serdes_get_lane(chip, port) < 0)
		return 0;

	for (i = 0; i < ARRAY_SIZE(mv88e6390_serdes_hw_stats); i++) {
		stat = &mv88e6390_serdes_hw_stats[i];
		memcpy(data + i * ETH_GSTRING_LEN, stat->string,
		       ETH_GSTRING_LEN);
	}
	return ARRAY_SIZE(mv88e6390_serdes_hw_stats);
}

static uint64_t mv88e6390_serdes_get_stat(struct mv88e6xxx_chip *chip, int lane,
					  struct mv88e6390_serdes_hw_stat *stat)
{
	u16 reg[3];
	int err, i;

	for (i = 0; i < 3; i++) {
		err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
					    stat->reg + i, &reg[i]);
		if (err) {
			dev_err(chip->dev, "failed to read statistic\n");
			return 0;
		}
	}

	return reg[0] | ((u64)reg[1] << 16) | ((u64)reg[2] << 32);
}

int mv88e6390_serdes_get_stats(struct mv88e6xxx_chip *chip, int port,
			       uint64_t *data)
{
	struct mv88e6390_serdes_hw_stat *stat;
	int lane;
	int i;

	lane = mv88e6xxx_serdes_get_lane(chip, port);
	if (lane < 0)
		return 0;

	for (i = 0; i < ARRAY_SIZE(mv88e6390_serdes_hw_stats); i++) {
		stat = &mv88e6390_serdes_hw_stats[i];
		data[i] = mv88e6390_serdes_get_stat(chip, lane, stat);
	}

	return ARRAY_SIZE(mv88e6390_serdes_hw_stats);
}

static int mv88e6390_serdes_enable_checker(struct mv88e6xxx_chip *chip, int lane)
{
	u16 reg;
	int err;

	err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				    MV88E6390_PG_CONTROL, &reg);
	if (err)
		return err;

	reg |= MV88E6390_PG_CONTROL_ENABLE_PC;
	return mv88e6390_serdes_write(chip, lane, MDIO_MMD_PHYXS,
				      MV88E6390_PG_CONTROL, reg);
}

int mv88e6390_serdes_power(struct mv88e6xxx_chip *chip, int port, int lane,
			   bool up)
{
	u8 cmode = chip->ports[port].cmode;
	int err = 0;

	switch (cmode) {
	case MV88E6XXX_PORT_STS_CMODE_SGMII:
	case MV88E6XXX_PORT_STS_CMODE_1000BASEX:
	case MV88E6XXX_PORT_STS_CMODE_2500BASEX:
		err = mv88e6390_serdes_power_sgmii(chip, lane, up);
		break;
	case MV88E6XXX_PORT_STS_CMODE_XAUI:
	case MV88E6XXX_PORT_STS_CMODE_RXAUI:
		err = mv88e6390_serdes_power_10g(chip, lane, up);
		break;
	}

	if (!err && up)
		err = mv88e6390_serdes_enable_checker(chip, lane);

	return err;
}

int mv88e6390_serdes_pcs_config(struct mv88e6xxx_chip *chip, int port,
				int lane, unsigned int mode,
				phy_interface_t interface,
				const unsigned long *advertise)
{
	u16 val, bmcr, adv;
	bool changed;
	int err;

	switch (interface) {
	case PHY_INTERFACE_MODE_SGMII:
		adv = 0x0001;
		break;

	case PHY_INTERFACE_MODE_1000BASEX:
		adv = linkmode_adv_to_mii_adv_x(advertise,
					ETHTOOL_LINK_MODE_1000baseX_Full_BIT);
		break;

	case PHY_INTERFACE_MODE_2500BASEX:
		adv = linkmode_adv_to_mii_adv_x(advertise,
					ETHTOOL_LINK_MODE_2500baseX_Full_BIT);
		break;

	default:
		return 0;
	}

	err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				    MV88E6390_SGMII_ADVERTISE, &val);
	if (err)
		return err;

	changed = val != adv;
	if (changed) {
		err = mv88e6390_serdes_write(chip, lane, MDIO_MMD_PHYXS,
					     MV88E6390_SGMII_ADVERTISE, adv);
		if (err)
			return err;
	}

	err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				    MV88E6390_SGMII_BMCR, &val);
	if (err)
		return err;

	if (phylink_autoneg_inband(mode))
		bmcr = val | BMCR_ANENABLE;
	else
		bmcr = val & ~BMCR_ANENABLE;

	/* setting ANENABLE triggers a restart of negotiation */
	if (bmcr == val)
		return changed;

	return mv88e6390_serdes_write(chip, lane, MDIO_MMD_PHYXS,
				      MV88E6390_SGMII_BMCR, bmcr);
}

static int mv88e6390_serdes_pcs_get_state_sgmii(struct mv88e6xxx_chip *chip,
	int port, int lane, struct phylink_link_state *state)
{
	u16 lpa, status, ctrl;
	int err;

	err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				    MV88E6390_SGMII_BMCR, &ctrl);
	if (err) {
		dev_err(chip->dev, "can't read Serdes PHY control: %d\n", err);
		return err;
	}

	err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				    MV88E6390_SGMII_PHY_STATUS, &status);
	if (err) {
		dev_err(chip->dev, "can't read Serdes PHY status: %d\n", err);
		return err;
	}

	err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				    MV88E6390_SGMII_LPA, &lpa);
	if (err) {
		dev_err(chip->dev, "can't read Serdes PHY LPA: %d\n", err);
		return err;
	}

	return mv88e6xxx_serdes_pcs_get_state(chip, ctrl, status, lpa, state);
}

static int mv88e6390_serdes_pcs_get_state_10g(struct mv88e6xxx_chip *chip,
	int port, int lane, struct phylink_link_state *state)
{
	u16 status;
	int err;

	err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				    MV88E6390_10G_STAT1, &status);
	if (err)
		return err;

	state->link = !!(status & MDIO_STAT1_LSTATUS);
	if (state->link) {
		state->speed = SPEED_10000;
		state->duplex = DUPLEX_FULL;
	}

	return 0;
}

static int mv88e6393x_serdes_pcs_get_state_10g(struct mv88e6xxx_chip *chip,
					       int port, int lane,
					       struct phylink_link_state *state)
{
	u16 status;
	int err;

	err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				    MV88E6390_10G_STAT1, &status);
	if (err)
		return err;

	state->link = !!(status & MDIO_STAT1_LSTATUS);
	if (state->link) {
		if (state->interface == PHY_INTERFACE_MODE_5GBASER)
			state->speed = SPEED_5000;
		else
			state->speed = SPEED_10000;
		state->duplex = DUPLEX_FULL;
	}

	return 0;
}

int mv88e6390_serdes_pcs_get_state(struct mv88e6xxx_chip *chip, int port,
				   int lane, struct phylink_link_state *state)
{
	switch (state->interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
		return mv88e6390_serdes_pcs_get_state_sgmii(chip, port, lane,
							    state);
	case PHY_INTERFACE_MODE_XAUI:
	case PHY_INTERFACE_MODE_RXAUI:
		return mv88e6390_serdes_pcs_get_state_10g(chip, port, lane,
							  state);

	default:
		return -EOPNOTSUPP;
	}
}

int mv88e6393x_serdes_pcs_get_state(struct mv88e6xxx_chip *chip, int port,
				    int lane, struct phylink_link_state *state)
{
	switch (state->interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
		return mv88e6390_serdes_pcs_get_state_sgmii(chip, port, lane,
							    state);
	case PHY_INTERFACE_MODE_5GBASER:
	case PHY_INTERFACE_MODE_10GBASER:
		return mv88e6393x_serdes_pcs_get_state_10g(chip, port, lane,
							   state);

	default:
		return -EOPNOTSUPP;
	}
}

int mv88e6390_serdes_pcs_an_restart(struct mv88e6xxx_chip *chip, int port,
				    int lane)
{
	u16 bmcr;
	int err;

	err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				    MV88E6390_SGMII_BMCR, &bmcr);
	if (err)
		return err;

	return mv88e6390_serdes_write(chip, lane, MDIO_MMD_PHYXS,
				      MV88E6390_SGMII_BMCR,
				      bmcr | BMCR_ANRESTART);
}

int mv88e6390_serdes_pcs_link_up(struct mv88e6xxx_chip *chip, int port,
				 int lane, int speed, int duplex)
{
	u16 val, bmcr;
	int err;

	err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				    MV88E6390_SGMII_BMCR, &val);
	if (err)
		return err;

	bmcr = val & ~(BMCR_SPEED100 | BMCR_FULLDPLX | BMCR_SPEED1000);
	switch (speed) {
	case SPEED_2500:
	case SPEED_1000:
		bmcr |= BMCR_SPEED1000;
		break;
	case SPEED_100:
		bmcr |= BMCR_SPEED100;
		break;
	case SPEED_10:
		break;
	}

	if (duplex == DUPLEX_FULL)
		bmcr |= BMCR_FULLDPLX;

	if (bmcr == val)
		return 0;

	return mv88e6390_serdes_write(chip, lane, MDIO_MMD_PHYXS,
				      MV88E6390_SGMII_BMCR, bmcr);
}

static void mv88e6390_serdes_irq_link_sgmii(struct mv88e6xxx_chip *chip,
					    int port, int lane)
{
	u16 bmsr;
	int err;

	/* If the link has dropped, we want to know about it. */
	err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				    MV88E6390_SGMII_BMSR, &bmsr);
	if (err) {
		dev_err(chip->dev, "can't read Serdes BMSR: %d\n", err);
		return;
	}

	dsa_port_phylink_mac_change(chip->ds, port, !!(bmsr & BMSR_LSTATUS));
}

static void mv88e6393x_serdes_irq_link_10g(struct mv88e6xxx_chip *chip,
					   int port, u8 lane)
{
	u16 status;
	int err;

	/* If the link has dropped, we want to know about it. */
	err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				    MV88E6390_10G_STAT1, &status);
	if (err) {
		dev_err(chip->dev, "can't read Serdes STAT1: %d\n", err);
		return;
	}

	dsa_port_phylink_mac_change(chip->ds, port, !!(status & MDIO_STAT1_LSTATUS));
}

static int mv88e6390_serdes_irq_enable_sgmii(struct mv88e6xxx_chip *chip,
					     int lane, bool enable)
{
	u16 val = 0;

	if (enable)
		val |= MV88E6390_SGMII_INT_LINK_DOWN |
			MV88E6390_SGMII_INT_LINK_UP;

	return mv88e6390_serdes_write(chip, lane, MDIO_MMD_PHYXS,
				      MV88E6390_SGMII_INT_ENABLE, val);
}

int mv88e6390_serdes_irq_enable(struct mv88e6xxx_chip *chip, int port, int lane,
				bool enable)
{
	u8 cmode = chip->ports[port].cmode;

	switch (cmode) {
	case MV88E6XXX_PORT_STS_CMODE_SGMII:
	case MV88E6XXX_PORT_STS_CMODE_1000BASEX:
	case MV88E6XXX_PORT_STS_CMODE_2500BASEX:
		return mv88e6390_serdes_irq_enable_sgmii(chip, lane, enable);
	}

	return 0;
}

static int mv88e6390_serdes_irq_status_sgmii(struct mv88e6xxx_chip *chip,
					     int lane, u16 *status)
{
	int err;

	err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				    MV88E6390_SGMII_INT_STATUS, status);

	return err;
}

static int mv88e6393x_serdes_irq_enable_10g(struct mv88e6xxx_chip *chip,
					    u8 lane, bool enable)
{
	u16 val = 0;

	if (enable)
		val |= MV88E6393X_10G_INT_LINK_CHANGE;

	return mv88e6390_serdes_write(chip, lane, MDIO_MMD_PHYXS,
				      MV88E6393X_10G_INT_ENABLE, val);
}

int mv88e6393x_serdes_irq_enable(struct mv88e6xxx_chip *chip, int port,
				 int lane, bool enable)
{
	u8 cmode = chip->ports[port].cmode;

	switch (cmode) {
	case MV88E6XXX_PORT_STS_CMODE_SGMII:
	case MV88E6XXX_PORT_STS_CMODE_1000BASEX:
	case MV88E6XXX_PORT_STS_CMODE_2500BASEX:
		return mv88e6390_serdes_irq_enable_sgmii(chip, lane, enable);
	case MV88E6393X_PORT_STS_CMODE_5GBASER:
	case MV88E6393X_PORT_STS_CMODE_10GBASER:
		return mv88e6393x_serdes_irq_enable_10g(chip, lane, enable);
	}

	return 0;
}

static int mv88e6393x_serdes_irq_status_10g(struct mv88e6xxx_chip *chip,
					    u8 lane, u16 *status)
{
	int err;

	err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				    MV88E6393X_10G_INT_STATUS, status);

	return err;
}

irqreturn_t mv88e6393x_serdes_irq_status(struct mv88e6xxx_chip *chip, int port,
					 int lane)
{
	u8 cmode = chip->ports[port].cmode;
	irqreturn_t ret = IRQ_NONE;
	u16 status;
	int err;

	switch (cmode) {
	case MV88E6XXX_PORT_STS_CMODE_SGMII:
	case MV88E6XXX_PORT_STS_CMODE_1000BASEX:
	case MV88E6XXX_PORT_STS_CMODE_2500BASEX:
		err = mv88e6390_serdes_irq_status_sgmii(chip, lane, &status);
		if (err)
			return ret;
		if (status & (MV88E6390_SGMII_INT_LINK_DOWN |
			      MV88E6390_SGMII_INT_LINK_UP)) {
			ret = IRQ_HANDLED;
			mv88e6390_serdes_irq_link_sgmii(chip, port, lane);
		}
		break;
	case MV88E6393X_PORT_STS_CMODE_5GBASER:
	case MV88E6393X_PORT_STS_CMODE_10GBASER:
		err = mv88e6393x_serdes_irq_status_10g(chip, lane, &status);
		if (err)
			return err;
		if (status & MV88E6393X_10G_INT_LINK_CHANGE) {
			ret = IRQ_HANDLED;
			mv88e6393x_serdes_irq_link_10g(chip, port, lane);
		}
		break;
	}

	return ret;
}

irqreturn_t mv88e6390_serdes_irq_status(struct mv88e6xxx_chip *chip, int port,
					int lane)
{
	u8 cmode = chip->ports[port].cmode;
	irqreturn_t ret = IRQ_NONE;
	u16 status;
	int err;

	switch (cmode) {
	case MV88E6XXX_PORT_STS_CMODE_SGMII:
	case MV88E6XXX_PORT_STS_CMODE_1000BASEX:
	case MV88E6XXX_PORT_STS_CMODE_2500BASEX:
		err = mv88e6390_serdes_irq_status_sgmii(chip, lane, &status);
		if (err)
			return ret;
		if (status & (MV88E6390_SGMII_INT_LINK_DOWN |
			      MV88E6390_SGMII_INT_LINK_UP)) {
			ret = IRQ_HANDLED;
			mv88e6390_serdes_irq_link_sgmii(chip, port, lane);
		}
	}

	return ret;
}

unsigned int mv88e6390_serdes_irq_mapping(struct mv88e6xxx_chip *chip, int port)
{
	return irq_find_mapping(chip->g2_irq.domain, port);
}

static const u16 mv88e6390_serdes_regs[] = {
	/* SERDES common registers */
	0xf00a, 0xf00b, 0xf00c,
	0xf010, 0xf011, 0xf012, 0xf013,
	0xf016, 0xf017, 0xf018,
	0xf01b, 0xf01c, 0xf01d, 0xf01e, 0xf01f,
	0xf020, 0xf021, 0xf022, 0xf023, 0xf024, 0xf025, 0xf026, 0xf027,
	0xf028, 0xf029,
	0xf030, 0xf031, 0xf032, 0xf033, 0xf034, 0xf035, 0xf036, 0xf037,
	0xf038, 0xf039,
	/* SGMII */
	0x2000, 0x2001, 0x2002, 0x2003, 0x2004, 0x2005, 0x2006, 0x2007,
	0x2008,
	0x200f,
	0xa000, 0xa001, 0xa002, 0xa003,
	/* 10Gbase-X */
	0x1000, 0x1001, 0x1002, 0x1003, 0x1004, 0x1005, 0x1006, 0x1007,
	0x1008,
	0x100e, 0x100f,
	0x1018, 0x1019,
	0x9000, 0x9001, 0x9002, 0x9003, 0x9004,
	0x9006,
	0x9010, 0x9011, 0x9012, 0x9013, 0x9014, 0x9015, 0x9016,
	/* 10Gbase-R */
	0x1020, 0x1021, 0x1022, 0x1023, 0x1024, 0x1025, 0x1026, 0x1027,
	0x1028, 0x1029, 0x102a, 0x102b,
};

int mv88e6390_serdes_get_regs_len(struct mv88e6xxx_chip *chip, int port)
{
	if (mv88e6xxx_serdes_get_lane(chip, port) < 0)
		return 0;

	return ARRAY_SIZE(mv88e6390_serdes_regs) * sizeof(u16);
}

void mv88e6390_serdes_get_regs(struct mv88e6xxx_chip *chip, int port, void *_p)
{
	u16 *p = _p;
	int lane;
	u16 reg;
	int err;
	int i;

	lane = mv88e6xxx_serdes_get_lane(chip, port);
	if (lane < 0)
		return;

	for (i = 0 ; i < ARRAY_SIZE(mv88e6390_serdes_regs); i++) {
		err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
					    mv88e6390_serdes_regs[i], &reg);
		if (!err)
			p[i] = reg;
	}
}

static int mv88e6393x_serdes_power_lane(struct mv88e6xxx_chip *chip, int lane,
					bool on)
{
	u16 reg;
	int err;

	err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				    MV88E6393X_SERDES_CTRL1, &reg);
	if (err)
		return err;

	if (on)
		reg &= ~(MV88E6393X_SERDES_CTRL1_TX_PDOWN |
			 MV88E6393X_SERDES_CTRL1_RX_PDOWN);
	else
		reg |= MV88E6393X_SERDES_CTRL1_TX_PDOWN |
		       MV88E6393X_SERDES_CTRL1_RX_PDOWN;

	return mv88e6390_serdes_write(chip, lane, MDIO_MMD_PHYXS,
				      MV88E6393X_SERDES_CTRL1, reg);
}

static int mv88e6393x_serdes_erratum_4_6(struct mv88e6xxx_chip *chip, int lane)
{
	u16 reg;
	int err;

	/* mv88e6393x family errata 4.6:
	 * Cannot clear PwrDn bit on SERDES if device is configured CPU_MGD
	 * mode or P0_mode is configured for [x]MII.
	 * Workaround: Set SERDES register 4.F002 bit 5=0 and bit 15=1.
	 *
	 * It seems that after this workaround the SERDES is automatically
	 * powered up (the bit is cleared), so power it down.
	 */
	err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				    MV88E6393X_SERDES_POC, &reg);
	if (err)
		return err;

	reg &= ~MV88E6393X_SERDES_POC_PDOWN;
	reg |= MV88E6393X_SERDES_POC_RESET;

	err = mv88e6390_serdes_write(chip, lane, MDIO_MMD_PHYXS,
				     MV88E6393X_SERDES_POC, reg);
	if (err)
		return err;

	err = mv88e6390_serdes_power_sgmii(chip, lane, false);
	if (err)
		return err;

	return mv88e6393x_serdes_power_lane(chip, lane, false);
}

int mv88e6393x_serdes_setup_errata(struct mv88e6xxx_chip *chip)
{
	int err;

	err = mv88e6393x_serdes_erratum_4_6(chip, MV88E6393X_PORT0_LANE);
	if (err)
		return err;

	err = mv88e6393x_serdes_erratum_4_6(chip, MV88E6393X_PORT9_LANE);
	if (err)
		return err;

	return mv88e6393x_serdes_erratum_4_6(chip, MV88E6393X_PORT10_LANE);
}

static int mv88e6393x_serdes_erratum_4_8(struct mv88e6xxx_chip *chip, int lane)
{
	u16 reg, pcs;
	int err;

	/* mv88e6393x family errata 4.8:
	 * When a SERDES port is operating in 1000BASE-X or SGMII mode link may
	 * not come up after hardware reset or software reset of SERDES core.
	 * Workaround is to write SERDES register 4.F074.14=1 for only those
	 * modes and 0 in all other modes.
	 */
	err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				    MV88E6393X_SERDES_POC, &pcs);
	if (err)
		return err;

	pcs &= MV88E6393X_SERDES_POC_PCS_MASK;

	err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				    MV88E6393X_ERRATA_4_8_REG, &reg);
	if (err)
		return err;

	if (pcs == MV88E6393X_SERDES_POC_PCS_1000BASEX ||
	    pcs == MV88E6393X_SERDES_POC_PCS_SGMII_PHY ||
	    pcs == MV88E6393X_SERDES_POC_PCS_SGMII_MAC)
		reg |= MV88E6393X_ERRATA_4_8_BIT;
	else
		reg &= ~MV88E6393X_ERRATA_4_8_BIT;

	return mv88e6390_serdes_write(chip, lane, MDIO_MMD_PHYXS,
				      MV88E6393X_ERRATA_4_8_REG, reg);
}

static int mv88e6393x_serdes_erratum_5_2(struct mv88e6xxx_chip *chip, int lane,
					 u8 cmode)
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
	u16 reg;

	/* mv88e6393x family errata 5.2:
	 * For optimal signal integrity the following sequence should be applied
	 * to SERDES operating in 10G mode. These registers only apply to 10G
	 * operation and have no effect on other speeds.
	 */
	if (cmode != MV88E6393X_PORT_STS_CMODE_10GBASER)
		return 0;

	for (i = 0; i < ARRAY_SIZE(fixes); ++i) {
		err = mv88e6390_serdes_read(chip, lane, fixes[i].dev,
					    fixes[i].reg, &reg);
		if (err)
			return err;

		reg &= ~fixes[i].mask;
		reg |= fixes[i].val;

		err = mv88e6390_serdes_write(chip, lane, fixes[i].dev,
					     fixes[i].reg, reg);
		if (err)
			return err;
	}

	return 0;
}

static int mv88e6393x_serdes_fix_2500basex_an(struct mv88e6xxx_chip *chip,
					      int lane, u8 cmode, bool on)
{
	u16 reg;
	int err;

	if (cmode != MV88E6XXX_PORT_STS_CMODE_2500BASEX)
		return 0;

	/* Inband AN is broken on Amethyst in 2500base-x mode when set by
	 * standard mechanism (via cmode).
	 * We can get around this by configuring the PCS mode to 1000base-x
	 * and then writing value 0x58 to register 1e.8000. (This must be done
	 * while SerDes receiver and transmitter are disabled, which is, when
	 * this function is called.)
	 * It seem that when we do this configuration to 2500base-x mode (by
	 * changing PCS mode to 1000base-x and frequency to 3.125 GHz from
	 * 1.25 GHz) and then configure to sgmii or 1000base-x, the device
	 * thinks that it already has SerDes at 1.25 GHz and does not change
	 * the 1e.8000 register, leaving SerDes at 3.125 GHz.
	 * To avoid this, change PCS mode back to 2500base-x when disabling
	 * SerDes from 2500base-x mode.
	 */
	err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				    MV88E6393X_SERDES_POC, &reg);
	if (err)
		return err;

	reg &= ~(MV88E6393X_SERDES_POC_PCS_MASK | MV88E6393X_SERDES_POC_AN);
	if (on)
		reg |= MV88E6393X_SERDES_POC_PCS_1000BASEX |
		       MV88E6393X_SERDES_POC_AN;
	else
		reg |= MV88E6393X_SERDES_POC_PCS_2500BASEX;
	reg |= MV88E6393X_SERDES_POC_RESET;

	err = mv88e6390_serdes_write(chip, lane, MDIO_MMD_PHYXS,
				     MV88E6393X_SERDES_POC, reg);
	if (err)
		return err;

	err = mv88e6390_serdes_write(chip, lane, MDIO_MMD_VEND1, 0x8000, 0x58);
	if (err)
		return err;

	return 0;
}

int mv88e6393x_serdes_power(struct mv88e6xxx_chip *chip, int port, int lane,
			    bool on)
{
	u8 cmode = chip->ports[port].cmode;
	int err;

	if (port != 0 && port != 9 && port != 10)
		return -EOPNOTSUPP;

	if (on) {
		err = mv88e6393x_serdes_erratum_4_8(chip, lane);
		if (err)
			return err;

		err = mv88e6393x_serdes_erratum_5_2(chip, lane, cmode);
		if (err)
			return err;

		err = mv88e6393x_serdes_fix_2500basex_an(chip, lane, cmode,
							 true);
		if (err)
			return err;

		err = mv88e6393x_serdes_power_lane(chip, lane, true);
		if (err)
			return err;
	}

	switch (cmode) {
	case MV88E6XXX_PORT_STS_CMODE_SGMII:
	case MV88E6XXX_PORT_STS_CMODE_1000BASEX:
	case MV88E6XXX_PORT_STS_CMODE_2500BASEX:
		err = mv88e6390_serdes_power_sgmii(chip, lane, on);
		break;
	case MV88E6393X_PORT_STS_CMODE_5GBASER:
	case MV88E6393X_PORT_STS_CMODE_10GBASER:
		err = mv88e6390_serdes_power_10g(chip, lane, on);
		break;
	}

	if (err)
		return err;

	if (!on) {
		err = mv88e6393x_serdes_power_lane(chip, lane, false);
		if (err)
			return err;

		err = mv88e6393x_serdes_fix_2500basex_an(chip, lane, cmode,
							 false);
	}

	return err;
}
