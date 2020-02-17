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

int mv88e6352_serdes_power(struct mv88e6xxx_chip *chip, int port, u8 lane,
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

u8 mv88e6352_serdes_get_lane(struct mv88e6xxx_chip *chip, int port)
{
	u8 cmode = chip->ports[port].cmode;
	u8 lane = 0;

	if ((cmode == MV88E6XXX_PORT_STS_CMODE_100BASEX) ||
	    (cmode == MV88E6XXX_PORT_STS_CMODE_1000BASEX) ||
	    (cmode == MV88E6XXX_PORT_STS_CMODE_SGMII))
		lane = 0xff; /* Unused */

	return lane;
}

static bool mv88e6352_port_has_serdes(struct mv88e6xxx_chip *chip, int port)
{
	if (mv88e6xxx_serdes_get_lane(chip, port))
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
	struct dsa_switch *ds = chip->ds;
	u16 status;
	bool up;
	int err;

	err = mv88e6352_serdes_read(chip, MII_BMSR, &status);
	if (err)
		return;

	/* Status must be read twice in order to give the current link
	 * status. Otherwise the change in link status since the last
	 * read of the register is returned.
	 */
	err = mv88e6352_serdes_read(chip, MII_BMSR, &status);
	if (err)
		return;

	up = status & BMSR_LSTATUS;

	dsa_port_phylink_mac_change(ds, port, up);
}

irqreturn_t mv88e6352_serdes_irq_status(struct mv88e6xxx_chip *chip, int port,
					u8 lane)
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

int mv88e6352_serdes_irq_enable(struct mv88e6xxx_chip *chip, int port, u8 lane,
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
	int i;

	if (!mv88e6352_port_has_serdes(chip, port))
		return;

	for (i = 0 ; i < 32; i++) {
		mv88e6352_serdes_read(chip, i, &reg);
		p[i] = reg;
	}
}

u8 mv88e6341_serdes_get_lane(struct mv88e6xxx_chip *chip, int port)
{
	u8 cmode = chip->ports[port].cmode;
	u8 lane = 0;

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

u8 mv88e6390_serdes_get_lane(struct mv88e6xxx_chip *chip, int port)
{
	u8 cmode = chip->ports[port].cmode;
	u8 lane = 0;

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

u8 mv88e6390x_serdes_get_lane(struct mv88e6xxx_chip *chip, int port)
{
	u8 cmode_port = chip->ports[port].cmode;
	u8 cmode_port10 = chip->ports[10].cmode;
	u8 cmode_port9 = chip->ports[9].cmode;
	u8 lane = 0;

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

/* Set power up/down for 10GBASE-R and 10GBASE-X4/X2 */
static int mv88e6390_serdes_power_10g(struct mv88e6xxx_chip *chip, u8 lane,
				      bool up)
{
	u16 val, new_val;
	int err;

	err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				    MV88E6390_PCS_CONTROL_1, &val);

	if (err)
		return err;

	if (up)
		new_val = val & ~(MV88E6390_PCS_CONTROL_1_RESET |
				  MV88E6390_PCS_CONTROL_1_LOOPBACK |
				  MV88E6390_PCS_CONTROL_1_PDOWN);
	else
		new_val = val | MV88E6390_PCS_CONTROL_1_PDOWN;

	if (val != new_val)
		err = mv88e6390_serdes_write(chip, lane, MDIO_MMD_PHYXS,
					     MV88E6390_PCS_CONTROL_1, new_val);

	return err;
}

/* Set power up/down for SGMII and 1000Base-X */
static int mv88e6390_serdes_power_sgmii(struct mv88e6xxx_chip *chip, u8 lane,
					bool up)
{
	u16 val, new_val;
	int err;

	err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				    MV88E6390_SGMII_CONTROL, &val);
	if (err)
		return err;

	if (up)
		new_val = val & ~(MV88E6390_SGMII_CONTROL_RESET |
				  MV88E6390_SGMII_CONTROL_LOOPBACK |
				  MV88E6390_SGMII_CONTROL_PDOWN);
	else
		new_val = val | MV88E6390_SGMII_CONTROL_PDOWN;

	if (val != new_val)
		err = mv88e6390_serdes_write(chip, lane, MDIO_MMD_PHYXS,
					     MV88E6390_SGMII_CONTROL, new_val);

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
	if (mv88e6390_serdes_get_lane(chip, port) == 0)
		return 0;

	return ARRAY_SIZE(mv88e6390_serdes_hw_stats);
}

int mv88e6390_serdes_get_strings(struct mv88e6xxx_chip *chip,
				 int port, uint8_t *data)
{
	struct mv88e6390_serdes_hw_stat *stat;
	int i;

	if (mv88e6390_serdes_get_lane(chip, port) == 0)
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

	lane = mv88e6390_serdes_get_lane(chip, port);
	if (lane == 0)
		return 0;

	for (i = 0; i < ARRAY_SIZE(mv88e6390_serdes_hw_stats); i++) {
		stat = &mv88e6390_serdes_hw_stats[i];
		data[i] = mv88e6390_serdes_get_stat(chip, lane, stat);
	}

	return ARRAY_SIZE(mv88e6390_serdes_hw_stats);
}

static int mv88e6390_serdes_enable_checker(struct mv88e6xxx_chip *chip, u8 lane)
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

int mv88e6390_serdes_power(struct mv88e6xxx_chip *chip, int port, u8 lane,
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

static void mv88e6390_serdes_irq_link_sgmii(struct mv88e6xxx_chip *chip,
					    int port, u8 lane)
{
	u8 cmode = chip->ports[port].cmode;
	struct dsa_switch *ds = chip->ds;
	int duplex = DUPLEX_UNKNOWN;
	int speed = SPEED_UNKNOWN;
	phy_interface_t mode;
	int link, err;
	u16 status;

	err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				    MV88E6390_SGMII_PHY_STATUS, &status);
	if (err) {
		dev_err(chip->dev, "can't read SGMII PHY status: %d\n", err);
		return;
	}

	link = status & MV88E6390_SGMII_PHY_STATUS_LINK ?
	       LINK_FORCED_UP : LINK_FORCED_DOWN;

	if (status & MV88E6390_SGMII_PHY_STATUS_SPD_DPL_VALID) {
		duplex = status & MV88E6390_SGMII_PHY_STATUS_DUPLEX_FULL ?
			 DUPLEX_FULL : DUPLEX_HALF;

		switch (status & MV88E6390_SGMII_PHY_STATUS_SPEED_MASK) {
		case MV88E6390_SGMII_PHY_STATUS_SPEED_1000:
			if (cmode == MV88E6XXX_PORT_STS_CMODE_2500BASEX)
				speed = SPEED_2500;
			else
				speed = SPEED_1000;
			break;
		case MV88E6390_SGMII_PHY_STATUS_SPEED_100:
			speed = SPEED_100;
			break;
		case MV88E6390_SGMII_PHY_STATUS_SPEED_10:
			speed = SPEED_10;
			break;
		default:
			dev_err(chip->dev, "invalid PHY speed\n");
			return;
		}
	}

	switch (cmode) {
	case MV88E6XXX_PORT_STS_CMODE_SGMII:
		mode = PHY_INTERFACE_MODE_SGMII;
		break;
	case MV88E6XXX_PORT_STS_CMODE_1000BASEX:
		mode = PHY_INTERFACE_MODE_1000BASEX;
		break;
	case MV88E6XXX_PORT_STS_CMODE_2500BASEX:
		mode = PHY_INTERFACE_MODE_2500BASEX;
		break;
	default:
		mode = PHY_INTERFACE_MODE_NA;
	}

	err = mv88e6xxx_port_setup_mac(chip, port, link, speed, duplex,
				       PAUSE_OFF, mode);
	if (err)
		dev_err(chip->dev, "can't propagate PHY settings to MAC: %d\n",
			err);
	else
		dsa_port_phylink_mac_change(ds, port, link == LINK_FORCED_UP);
}

static int mv88e6390_serdes_irq_enable_sgmii(struct mv88e6xxx_chip *chip,
					     u8 lane, bool enable)
{
	u16 val = 0;

	if (enable)
		val |= MV88E6390_SGMII_INT_LINK_DOWN |
			MV88E6390_SGMII_INT_LINK_UP;

	return mv88e6390_serdes_write(chip, lane, MDIO_MMD_PHYXS,
				      MV88E6390_SGMII_INT_ENABLE, val);
}

int mv88e6390_serdes_irq_enable(struct mv88e6xxx_chip *chip, int port, u8 lane,
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
					     u8 lane, u16 *status)
{
	int err;

	err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				    MV88E6390_SGMII_INT_STATUS, status);

	return err;
}

irqreturn_t mv88e6390_serdes_irq_status(struct mv88e6xxx_chip *chip, int port,
					u8 lane)
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
	if (mv88e6xxx_serdes_get_lane(chip, port) == 0)
		return 0;

	return ARRAY_SIZE(mv88e6390_serdes_regs) * sizeof(u16);
}

void mv88e6390_serdes_get_regs(struct mv88e6xxx_chip *chip, int port, void *_p)
{
	u16 *p = _p;
	int lane;
	u16 reg;
	int i;

	lane = mv88e6xxx_serdes_get_lane(chip, port);
	if (lane == 0)
		return;

	for (i = 0 ; i < ARRAY_SIZE(mv88e6390_serdes_regs); i++) {
		mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				      mv88e6390_serdes_regs[i], &reg);
		p[i] = reg;
	}
}
