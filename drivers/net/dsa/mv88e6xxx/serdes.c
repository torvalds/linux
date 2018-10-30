/*
 * Marvell 88E6xxx SERDES manipulation, via SMI bus
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2017 Andrew Lunn <andrew@lunn.ch>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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

static int mv88e6352_serdes_power_set(struct mv88e6xxx_chip *chip, bool on)
{
	u16 val, new_val;
	int err;

	err = mv88e6352_serdes_read(chip, MII_BMCR, &val);
	if (err)
		return err;

	if (on)
		new_val = val & ~BMCR_PDOWN;
	else
		new_val = val | BMCR_PDOWN;

	if (val != new_val)
		err = mv88e6352_serdes_write(chip, MII_BMCR, new_val);

	return err;
}

static bool mv88e6352_port_has_serdes(struct mv88e6xxx_chip *chip, int port)
{
	u8 cmode = chip->ports[port].cmode;

	if ((cmode == MV88E6XXX_PORT_STS_CMODE_100BASE_X) ||
	    (cmode == MV88E6XXX_PORT_STS_CMODE_1000BASE_X) ||
	    (cmode == MV88E6XXX_PORT_STS_CMODE_SGMII))
		return true;

	return false;
}

int mv88e6352_serdes_power(struct mv88e6xxx_chip *chip, int port, bool on)
{
	int err;

	if (mv88e6352_port_has_serdes(chip, port)) {
		err = mv88e6352_serdes_power_set(chip, on);
		if (err < 0)
			return err;
	}

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

	mv88e6352_serdes_read(chip, MII_BMSR, &status);

	/* Status must be read twice in order to give the current link
	 * status. Otherwise the change in link status since the last
	 * read of the register is returned.
	 */
	mv88e6352_serdes_read(chip, MII_BMSR, &status);

	up = status & BMSR_LSTATUS;

	dsa_port_phylink_mac_change(ds, port, up);
}

static irqreturn_t mv88e6352_serdes_thread_fn(int irq, void *dev_id)
{
	struct mv88e6xxx_port *port = dev_id;
	struct mv88e6xxx_chip *chip = port->chip;
	irqreturn_t ret = IRQ_NONE;
	u16 status;
	int err;

	mutex_lock(&chip->reg_lock);

	err = mv88e6352_serdes_read(chip, MV88E6352_SERDES_INT_STATUS, &status);
	if (err)
		goto out;

	if (status & MV88E6352_SERDES_INT_LINK_CHANGE) {
		ret = IRQ_HANDLED;
		mv88e6352_serdes_irq_link(chip, port->port);
	}
out:
	mutex_unlock(&chip->reg_lock);

	return ret;
}

static int mv88e6352_serdes_irq_enable(struct mv88e6xxx_chip *chip)
{
	return mv88e6352_serdes_write(chip, MV88E6352_SERDES_INT_ENABLE,
				      MV88E6352_SERDES_INT_LINK_CHANGE);
}

static int mv88e6352_serdes_irq_disable(struct mv88e6xxx_chip *chip)
{
	return mv88e6352_serdes_write(chip, MV88E6352_SERDES_INT_ENABLE, 0);
}

int mv88e6352_serdes_irq_setup(struct mv88e6xxx_chip *chip, int port)
{
	int err;

	if (!mv88e6352_port_has_serdes(chip, port))
		return 0;

	chip->ports[port].serdes_irq = irq_find_mapping(chip->g2_irq.domain,
							MV88E6352_SERDES_IRQ);
	if (chip->ports[port].serdes_irq < 0) {
		dev_err(chip->dev, "Unable to map SERDES irq: %d\n",
			chip->ports[port].serdes_irq);
		return chip->ports[port].serdes_irq;
	}

	/* Requesting the IRQ will trigger irq callbacks. So we cannot
	 * hold the reg_lock.
	 */
	mutex_unlock(&chip->reg_lock);
	err = request_threaded_irq(chip->ports[port].serdes_irq, NULL,
				   mv88e6352_serdes_thread_fn,
				   IRQF_ONESHOT, "mv88e6xxx-serdes",
				   &chip->ports[port]);
	mutex_lock(&chip->reg_lock);

	if (err) {
		dev_err(chip->dev, "Unable to request SERDES interrupt: %d\n",
			err);
		return err;
	}

	return mv88e6352_serdes_irq_enable(chip);
}

void mv88e6352_serdes_irq_free(struct mv88e6xxx_chip *chip, int port)
{
	if (!mv88e6352_port_has_serdes(chip, port))
		return;

	mv88e6352_serdes_irq_disable(chip);

	/* Freeing the IRQ will trigger irq callbacks. So we cannot
	 * hold the reg_lock.
	 */
	mutex_unlock(&chip->reg_lock);
	free_irq(chip->ports[port].serdes_irq, &chip->ports[port]);
	mutex_lock(&chip->reg_lock);

	chip->ports[port].serdes_irq = 0;
}

/* Return the SERDES lane address a port is using. Only Ports 9 and 10
 * have SERDES lanes. Returns -ENODEV if a port does not have a lane.
 */
static int mv88e6390_serdes_get_lane(struct mv88e6xxx_chip *chip, int port)
{
	u8 cmode = chip->ports[port].cmode;

	switch (port) {
	case 9:
		if (cmode == MV88E6XXX_PORT_STS_CMODE_1000BASE_X ||
		    cmode == MV88E6XXX_PORT_STS_CMODE_SGMII ||
		    cmode == MV88E6XXX_PORT_STS_CMODE_2500BASEX)
			return MV88E6390_PORT9_LANE0;
		return -ENODEV;
	case 10:
		if (cmode == MV88E6XXX_PORT_STS_CMODE_1000BASE_X ||
		    cmode == MV88E6XXX_PORT_STS_CMODE_SGMII ||
		    cmode == MV88E6XXX_PORT_STS_CMODE_2500BASEX)
			return MV88E6390_PORT10_LANE0;
		return -ENODEV;
	default:
		return -ENODEV;
	}
}

/* Return the SERDES lane address a port is using. Ports 9 and 10 can
 * use multiple lanes. If so, return the first lane the port uses.
 * Returns -ENODEV if a port does not have a lane.
 */
int mv88e6390x_serdes_get_lane(struct mv88e6xxx_chip *chip, int port)
{
	u8 cmode_port9, cmode_port10, cmode_port;

	cmode_port9 = chip->ports[9].cmode;
	cmode_port10 = chip->ports[10].cmode;
	cmode_port = chip->ports[port].cmode;

	switch (port) {
	case 2:
		if (cmode_port9 == MV88E6XXX_PORT_STS_CMODE_1000BASE_X ||
		    cmode_port9 == MV88E6XXX_PORT_STS_CMODE_SGMII ||
		    cmode_port9 == MV88E6XXX_PORT_STS_CMODE_2500BASEX)
			if (cmode_port == MV88E6XXX_PORT_STS_CMODE_1000BASE_X)
				return MV88E6390_PORT9_LANE1;
		return -ENODEV;
	case 3:
		if (cmode_port9 == MV88E6XXX_PORT_STS_CMODE_1000BASE_X ||
		    cmode_port9 == MV88E6XXX_PORT_STS_CMODE_SGMII ||
		    cmode_port9 == MV88E6XXX_PORT_STS_CMODE_2500BASEX ||
		    cmode_port9 == MV88E6XXX_PORT_STS_CMODE_RXAUI)
			if (cmode_port == MV88E6XXX_PORT_STS_CMODE_1000BASE_X)
				return MV88E6390_PORT9_LANE2;
		return -ENODEV;
	case 4:
		if (cmode_port9 == MV88E6XXX_PORT_STS_CMODE_1000BASE_X ||
		    cmode_port9 == MV88E6XXX_PORT_STS_CMODE_SGMII ||
		    cmode_port9 == MV88E6XXX_PORT_STS_CMODE_2500BASEX ||
		    cmode_port9 == MV88E6XXX_PORT_STS_CMODE_RXAUI)
			if (cmode_port == MV88E6XXX_PORT_STS_CMODE_1000BASE_X)
				return MV88E6390_PORT9_LANE3;
		return -ENODEV;
	case 5:
		if (cmode_port10 == MV88E6XXX_PORT_STS_CMODE_1000BASE_X ||
		    cmode_port10 == MV88E6XXX_PORT_STS_CMODE_SGMII ||
		    cmode_port10 == MV88E6XXX_PORT_STS_CMODE_2500BASEX)
			if (cmode_port == MV88E6XXX_PORT_STS_CMODE_1000BASE_X)
				return MV88E6390_PORT10_LANE1;
		return -ENODEV;
	case 6:
		if (cmode_port10 == MV88E6XXX_PORT_STS_CMODE_1000BASE_X ||
		    cmode_port10 == MV88E6XXX_PORT_STS_CMODE_SGMII ||
		    cmode_port10 == MV88E6XXX_PORT_STS_CMODE_2500BASEX ||
		    cmode_port10 == MV88E6XXX_PORT_STS_CMODE_RXAUI)
			if (cmode_port == MV88E6XXX_PORT_STS_CMODE_1000BASE_X)
				return MV88E6390_PORT10_LANE2;
		return -ENODEV;
	case 7:
		if (cmode_port10 == MV88E6XXX_PORT_STS_CMODE_1000BASE_X ||
		    cmode_port10 == MV88E6XXX_PORT_STS_CMODE_SGMII ||
		    cmode_port10 == MV88E6XXX_PORT_STS_CMODE_2500BASEX ||
		    cmode_port10 == MV88E6XXX_PORT_STS_CMODE_RXAUI)
			if (cmode_port == MV88E6XXX_PORT_STS_CMODE_1000BASE_X)
				return MV88E6390_PORT10_LANE3;
		return -ENODEV;
	case 9:
		if (cmode_port9 == MV88E6XXX_PORT_STS_CMODE_1000BASE_X ||
		    cmode_port9 == MV88E6XXX_PORT_STS_CMODE_SGMII ||
		    cmode_port9 == MV88E6XXX_PORT_STS_CMODE_2500BASEX ||
		    cmode_port9 == MV88E6XXX_PORT_STS_CMODE_XAUI ||
		    cmode_port9 == MV88E6XXX_PORT_STS_CMODE_RXAUI)
			return MV88E6390_PORT9_LANE0;
		return -ENODEV;
	case 10:
		if (cmode_port10 == MV88E6XXX_PORT_STS_CMODE_1000BASE_X ||
		    cmode_port10 == MV88E6XXX_PORT_STS_CMODE_SGMII ||
		    cmode_port10 == MV88E6XXX_PORT_STS_CMODE_2500BASEX ||
		    cmode_port10 == MV88E6XXX_PORT_STS_CMODE_XAUI ||
		    cmode_port10 == MV88E6XXX_PORT_STS_CMODE_RXAUI)
			return MV88E6390_PORT10_LANE0;
		return -ENODEV;
	default:
		return -ENODEV;
	}
}

/* Set the power on/off for 10GBASE-R and 10GBASE-X4/X2 */
static int mv88e6390_serdes_power_10g(struct mv88e6xxx_chip *chip, int lane,
				      bool on)
{
	u16 val, new_val;
	int err;

	err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				    MV88E6390_PCS_CONTROL_1, &val);

	if (err)
		return err;

	if (on)
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

/* Set the power on/off for SGMII and 1000Base-X */
static int mv88e6390_serdes_power_sgmii(struct mv88e6xxx_chip *chip, int lane,
					bool on)
{
	u16 val, new_val;
	int err;

	err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				    MV88E6390_SGMII_CONTROL, &val);
	if (err)
		return err;

	if (on)
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

static int mv88e6390_serdes_power_lane(struct mv88e6xxx_chip *chip, int port,
				       int lane, bool on)
{
	u8 cmode = chip->ports[port].cmode;

	switch (cmode) {
	case MV88E6XXX_PORT_STS_CMODE_SGMII:
	case MV88E6XXX_PORT_STS_CMODE_1000BASE_X:
	case MV88E6XXX_PORT_STS_CMODE_2500BASEX:
		return mv88e6390_serdes_power_sgmii(chip, lane, on);
	case MV88E6XXX_PORT_STS_CMODE_XAUI:
	case MV88E6XXX_PORT_STS_CMODE_RXAUI:
		return mv88e6390_serdes_power_10g(chip, lane, on);
	}

	return 0;
}

int mv88e6390_serdes_power(struct mv88e6xxx_chip *chip, int port, bool on)
{
	int lane;

	lane = mv88e6390_serdes_get_lane(chip, port);
	if (lane == -ENODEV)
		return 0;

	if (lane < 0)
		return lane;

	switch (port) {
	case 9 ... 10:
		return mv88e6390_serdes_power_lane(chip, port, lane, on);
	}

	return 0;
}

int mv88e6390x_serdes_power(struct mv88e6xxx_chip *chip, int port, bool on)
{
	int lane;

	lane = mv88e6390x_serdes_get_lane(chip, port);
	if (lane == -ENODEV)
		return 0;

	if (lane < 0)
		return lane;

	switch (port) {
	case 2 ... 4:
	case 5 ... 7:
	case 9 ... 10:
		return mv88e6390_serdes_power_lane(chip, port, lane, on);
	}

	return 0;
}

static void mv88e6390_serdes_irq_link_sgmii(struct mv88e6xxx_chip *chip,
					    int port, int lane)
{
	struct dsa_switch *ds = chip->ds;
	u16 status;
	bool up;

	mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
			      MV88E6390_SGMII_STATUS, &status);

	/* Status must be read twice in order to give the current link
	 * status. Otherwise the change in link status since the last
	 * read of the register is returned.
	 */
	mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
			      MV88E6390_SGMII_STATUS, &status);
	up = status & MV88E6390_SGMII_STATUS_LINK;

	dsa_port_phylink_mac_change(ds, port, up);
}

static int mv88e6390_serdes_irq_enable_sgmii(struct mv88e6xxx_chip *chip,
					     int lane)
{
	return mv88e6390_serdes_write(chip, lane, MDIO_MMD_PHYXS,
				      MV88E6390_SGMII_INT_ENABLE,
				      MV88E6390_SGMII_INT_LINK_DOWN |
				      MV88E6390_SGMII_INT_LINK_UP);
}

static int mv88e6390_serdes_irq_disable_sgmii(struct mv88e6xxx_chip *chip,
					      int lane)
{
	return mv88e6390_serdes_write(chip, lane, MDIO_MMD_PHYXS,
				      MV88E6390_SGMII_INT_ENABLE, 0);
}

int mv88e6390_serdes_irq_enable(struct mv88e6xxx_chip *chip, int port,
				int lane)
{
	u8 cmode = chip->ports[port].cmode;
	int err = 0;

	switch (cmode) {
	case MV88E6XXX_PORT_STS_CMODE_SGMII:
	case MV88E6XXX_PORT_STS_CMODE_1000BASE_X:
	case MV88E6XXX_PORT_STS_CMODE_2500BASEX:
		err = mv88e6390_serdes_irq_enable_sgmii(chip, lane);
	}

	return err;
}

int mv88e6390_serdes_irq_disable(struct mv88e6xxx_chip *chip, int port,
				 int lane)
{
	u8 cmode = chip->ports[port].cmode;
	int err = 0;

	switch (cmode) {
	case MV88E6XXX_PORT_STS_CMODE_SGMII:
	case MV88E6XXX_PORT_STS_CMODE_1000BASE_X:
	case MV88E6XXX_PORT_STS_CMODE_2500BASEX:
		err = mv88e6390_serdes_irq_disable_sgmii(chip, lane);
	}

	return err;
}

static int mv88e6390_serdes_irq_status_sgmii(struct mv88e6xxx_chip *chip,
					     int lane, u16 *status)
{
	int err;

	err = mv88e6390_serdes_read(chip, lane, MDIO_MMD_PHYXS,
				    MV88E6390_SGMII_INT_STATUS, status);

	return err;
}

static irqreturn_t mv88e6390_serdes_thread_fn(int irq, void *dev_id)
{
	struct mv88e6xxx_port *port = dev_id;
	struct mv88e6xxx_chip *chip = port->chip;
	irqreturn_t ret = IRQ_NONE;
	u8 cmode = port->cmode;
	u16 status;
	int lane;
	int err;

	lane = mv88e6390x_serdes_get_lane(chip, port->port);

	mutex_lock(&chip->reg_lock);

	switch (cmode) {
	case MV88E6XXX_PORT_STS_CMODE_SGMII:
	case MV88E6XXX_PORT_STS_CMODE_1000BASE_X:
	case MV88E6XXX_PORT_STS_CMODE_2500BASEX:
		err = mv88e6390_serdes_irq_status_sgmii(chip, lane, &status);
		if (err)
			goto out;
		if (status & (MV88E6390_SGMII_INT_LINK_DOWN |
			      MV88E6390_SGMII_INT_LINK_UP)) {
			ret = IRQ_HANDLED;
			mv88e6390_serdes_irq_link_sgmii(chip, port->port, lane);
		}
	}
out:
	mutex_unlock(&chip->reg_lock);

	return ret;
}

int mv88e6390_serdes_irq_setup(struct mv88e6xxx_chip *chip, int port)
{
	int lane;
	int err;

	/* Only support ports 9 and 10 at the moment */
	if (port < 9)
		return 0;

	lane = mv88e6390x_serdes_get_lane(chip, port);

	if (lane == -ENODEV)
		return 0;

	if (lane < 0)
		return lane;

	chip->ports[port].serdes_irq = irq_find_mapping(chip->g2_irq.domain,
							port);
	if (chip->ports[port].serdes_irq < 0) {
		dev_err(chip->dev, "Unable to map SERDES irq: %d\n",
			chip->ports[port].serdes_irq);
		return chip->ports[port].serdes_irq;
	}

	/* Requesting the IRQ will trigger irq callbacks. So we cannot
	 * hold the reg_lock.
	 */
	mutex_unlock(&chip->reg_lock);
	err = request_threaded_irq(chip->ports[port].serdes_irq, NULL,
				   mv88e6390_serdes_thread_fn,
				   IRQF_ONESHOT, "mv88e6xxx-serdes",
				   &chip->ports[port]);
	mutex_lock(&chip->reg_lock);

	if (err) {
		dev_err(chip->dev, "Unable to request SERDES interrupt: %d\n",
			err);
		return err;
	}

	return mv88e6390_serdes_irq_enable(chip, port, lane);
}

void mv88e6390_serdes_irq_free(struct mv88e6xxx_chip *chip, int port)
{
	int lane = mv88e6390x_serdes_get_lane(chip, port);

	if (port < 9)
		return;

	if (lane < 0)
		return;

	mv88e6390_serdes_irq_disable(chip, port, lane);

	/* Freeing the IRQ will trigger irq callbacks. So we cannot
	 * hold the reg_lock.
	 */
	mutex_unlock(&chip->reg_lock);
	free_irq(chip->ports[port].serdes_irq, &chip->ports[port]);
	mutex_lock(&chip->reg_lock);

	chip->ports[port].serdes_irq = 0;
}

int mv88e6341_serdes_power(struct mv88e6xxx_chip *chip, int port, bool on)
{
	u8 cmode = chip->ports[port].cmode;

	if (port != 5)
		return 0;

	if (cmode == MV88E6XXX_PORT_STS_CMODE_1000BASE_X ||
	    cmode == MV88E6XXX_PORT_STS_CMODE_SGMII ||
	    cmode == MV88E6XXX_PORT_STS_CMODE_2500BASEX)
		return mv88e6390_serdes_power_sgmii(chip, MV88E6341_ADDR_SERDES,
						    on);

	return 0;
}
