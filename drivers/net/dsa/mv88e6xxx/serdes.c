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
	u8 cmode;
	int err;

	err = mv88e6xxx_port_get_cmode(chip, port, &cmode);
	if (err) {
		dev_err(chip->dev, "failed to read cmode\n");
		return false;
	}

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

/* Set the power on/off for 10GBASE-R and 10GBASE-X4/X2 */
static int mv88e6390_serdes_10g(struct mv88e6xxx_chip *chip, int addr, bool on)
{
	u16 val, new_val;
	int reg_c45;
	int err;

	reg_c45 = MII_ADDR_C45 | MV88E6390_SERDES_DEVICE |
		MV88E6390_PCS_CONTROL_1;
	err = mv88e6xxx_phy_read(chip, addr, reg_c45, &val);
	if (err)
		return err;

	if (on)
		new_val = val & ~(MV88E6390_PCS_CONTROL_1_RESET |
				  MV88E6390_PCS_CONTROL_1_LOOPBACK |
				  MV88E6390_PCS_CONTROL_1_PDOWN);
	else
		new_val = val | MV88E6390_PCS_CONTROL_1_PDOWN;

	if (val != new_val)
		err = mv88e6xxx_phy_write(chip, addr, reg_c45, new_val);

	return err;
}

/* Set the power on/off for 10GBASE-R and 10GBASE-X4/X2 */
static int mv88e6390_serdes_sgmii(struct mv88e6xxx_chip *chip, int addr,
				  bool on)
{
	u16 val, new_val;
	int reg_c45;
	int err;

	reg_c45 = MII_ADDR_C45 | MV88E6390_SERDES_DEVICE |
		MV88E6390_SGMII_CONTROL;
	err = mv88e6xxx_phy_read(chip, addr, reg_c45, &val);
	if (err)
		return err;

	if (on)
		new_val = val & ~(MV88E6390_SGMII_CONTROL_RESET |
				  MV88E6390_SGMII_CONTROL_LOOPBACK |
				  MV88E6390_SGMII_CONTROL_PDOWN);
	else
		new_val = val | MV88E6390_SGMII_CONTROL_PDOWN;

	if (val != new_val)
		err = mv88e6xxx_phy_write(chip, addr, reg_c45, new_val);

	return err;
}

static int mv88e6390_serdes_lower(struct mv88e6xxx_chip *chip, u8 cmode,
				  int port_donor, int lane, bool rxaui, bool on)
{
	int err;
	u8 cmode_donor;

	err = mv88e6xxx_port_get_cmode(chip, port_donor, &cmode_donor);
	if (err)
		return err;

	switch (cmode_donor) {
	case MV88E6XXX_PORT_STS_CMODE_RXAUI:
		if (!rxaui)
			break;
		/* Fall through */
	case MV88E6XXX_PORT_STS_CMODE_1000BASE_X:
	case MV88E6XXX_PORT_STS_CMODE_SGMII:
	case MV88E6XXX_PORT_STS_CMODE_2500BASEX:
		if (cmode == MV88E6XXX_PORT_STS_CMODE_1000BASE_X ||
		    cmode == MV88E6XXX_PORT_STS_CMODE_SGMII)
			return	mv88e6390_serdes_sgmii(chip, lane, on);
	}
	return 0;
}

static int mv88e6390_serdes_port9(struct mv88e6xxx_chip *chip, u8 cmode,
				  bool on)
{
	switch (cmode) {
	case MV88E6XXX_PORT_STS_CMODE_1000BASE_X:
	case MV88E6XXX_PORT_STS_CMODE_SGMII:
		return mv88e6390_serdes_sgmii(chip, MV88E6390_PORT9_LANE0, on);
	case MV88E6XXX_PORT_STS_CMODE_XAUI:
	case MV88E6XXX_PORT_STS_CMODE_RXAUI:
	case MV88E6XXX_PORT_STS_CMODE_2500BASEX:
		return mv88e6390_serdes_10g(chip, MV88E6390_PORT9_LANE0, on);
	}

	return 0;
}

static int mv88e6390_serdes_port10(struct mv88e6xxx_chip *chip, u8 cmode,
				   bool on)
{
	switch (cmode) {
	case MV88E6XXX_PORT_STS_CMODE_SGMII:
		return mv88e6390_serdes_sgmii(chip, MV88E6390_PORT10_LANE0, on);
	case MV88E6XXX_PORT_STS_CMODE_XAUI:
	case MV88E6XXX_PORT_STS_CMODE_RXAUI:
	case MV88E6XXX_PORT_STS_CMODE_1000BASE_X:
	case MV88E6XXX_PORT_STS_CMODE_2500BASEX:
		return mv88e6390_serdes_10g(chip, MV88E6390_PORT10_LANE0, on);
	}

	return 0;
}

int mv88e6390_serdes_power(struct mv88e6xxx_chip *chip, int port, bool on)
{
	u8 cmode;
	int err;

	err = mv88e6xxx_port_get_cmode(chip, port, &cmode);
	if (err)
		return err;

	switch (port) {
	case 2:
		return mv88e6390_serdes_lower(chip, cmode, 9,
					      MV88E6390_PORT9_LANE1,
					      false, on);
	case 3:
		return mv88e6390_serdes_lower(chip, cmode, 9,
					      MV88E6390_PORT9_LANE2,
					      true, on);
	case 4:
		return mv88e6390_serdes_lower(chip, cmode, 9,
					      MV88E6390_PORT9_LANE3,
					      true, on);
	case 5:
		return mv88e6390_serdes_lower(chip, cmode, 10,
					      MV88E6390_PORT10_LANE1,
					      false, on);
	case 6:
		return mv88e6390_serdes_lower(chip, cmode, 10,
					      MV88E6390_PORT10_LANE2,
					      true, on);
	case 7:
		return mv88e6390_serdes_lower(chip, cmode, 10,
					      MV88E6390_PORT10_LANE3,
					      true, on);
	case 9:
		return mv88e6390_serdes_port9(chip, cmode, on);
	case 10:
		return mv88e6390_serdes_port10(chip, cmode, on);
	}

	return 0;
}

int mv88e6341_serdes_power(struct mv88e6xxx_chip *chip, int port, bool on)
{
	int err;
	u8 cmode;

	if (port != 5)
		return 0;

	err = mv88e6xxx_port_get_cmode(chip, port, &cmode);
	if (err)
		return err;

	if (cmode == MV88E6XXX_PORT_STS_CMODE_1000BASE_X ||
	    cmode == MV88E6XXX_PORT_STS_CMODE_SGMII ||
	    cmode == MV88E6XXX_PORT_STS_CMODE_2500BASEX)
		return mv88e6390_serdes_sgmii(chip, MV88E6341_ADDR_SERDES, on);

	return 0;
}
