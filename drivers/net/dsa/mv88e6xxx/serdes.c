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
	return mv88e6xxx_phy_read_c45(chip, lane, device, reg, val);
}

int mv88e6xxx_pcs_decode_state(struct device *dev, u16 bmsr, u16 lpa,
			       u16 status, struct phylink_link_state *state)
{
	state->link = false;

	/* If the BMSR reports that the link had failed, report this to
	 * phylink.
	 */
	if (!(bmsr & BMSR_LSTATUS))
		return 0;

	state->link = !!(status & MV88E6390_SGMII_PHY_STATUS_LINK);
	state->an_complete = !!(bmsr & BMSR_ANEGCOMPLETE);

	if (status & MV88E6390_SGMII_PHY_STATUS_SPD_DPL_VALID) {
		/* The Spped and Duplex Resolved register is 1 if AN is enabled
		 * and complete, or if AN is disabled. So with disabled AN we
		 * still get here on link up.
		 */
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
			dev_err(dev, "invalid PHY speed\n");
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
	int err;

	err = mv88e6352_g2_scratch_port_has_serdes(chip, port);
	if (err <= 0)
		return err;

	return ARRAY_SIZE(mv88e6352_serdes_hw_stats);
}

int mv88e6352_serdes_get_strings(struct mv88e6xxx_chip *chip,
				 int port, uint8_t *data)
{
	struct mv88e6352_serdes_hw_stat *stat;
	int err, i;

	err = mv88e6352_g2_scratch_port_has_serdes(chip, port);
	if (err <= 0)
		return err;

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
	int i, err;
	u64 value;

	err = mv88e6352_g2_scratch_port_has_serdes(chip, port);
	if (err <= 0)
		return err;

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

unsigned int mv88e6352_serdes_irq_mapping(struct mv88e6xxx_chip *chip, int port)
{
	return irq_find_mapping(chip->g2_irq.domain, MV88E6352_SERDES_IRQ);
}

int mv88e6352_serdes_get_regs_len(struct mv88e6xxx_chip *chip, int port)
{
	int err;

	mv88e6xxx_reg_lock(chip);
	err = mv88e6352_g2_scratch_port_has_serdes(chip, port);
	mv88e6xxx_reg_unlock(chip);
	if (err <= 0)
		return err;

	return 32 * sizeof(u16);
}

void mv88e6352_serdes_get_regs(struct mv88e6xxx_chip *chip, int port, void *_p)
{
	u16 *p = _p;
	u16 reg;
	int err;
	int i;

	err = mv88e6352_g2_scratch_port_has_serdes(chip, port);
	if (err <= 0)
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
	    cmode == MV88E6393X_PORT_STS_CMODE_10GBASER ||
	    cmode == MV88E6393X_PORT_STS_CMODE_USXGMII)
		lane = port;

	return lane;
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

static const int mv88e6352_serdes_p2p_to_reg[] = {
	/* Index of value in microvolts corresponds to the register value */
	14000, 112000, 210000, 308000, 406000, 504000, 602000, 700000,
};

int mv88e6352_serdes_set_tx_amplitude(struct mv88e6xxx_chip *chip, int port,
				      int val)
{
	bool found = false;
	u16 ctrl, reg;
	int err;
	int i;

	err = mv88e6352_g2_scratch_port_has_serdes(chip, port);
	if (err <= 0)
		return err;

	for (i = 0; i < ARRAY_SIZE(mv88e6352_serdes_p2p_to_reg); ++i) {
		if (mv88e6352_serdes_p2p_to_reg[i] == val) {
			reg = i;
			found = true;
			break;
		}
	}

	if (!found)
		return -EINVAL;

	err = mv88e6352_serdes_read(chip, MV88E6352_SERDES_SPEC_CTRL2, &ctrl);
	if (err)
		return err;

	ctrl &= ~MV88E6352_SERDES_OUT_AMP_MASK;
	ctrl |= reg;

	return mv88e6352_serdes_write(chip, MV88E6352_SERDES_SPEC_CTRL2, ctrl);
}
