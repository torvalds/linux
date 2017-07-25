/*
 * Marvell 88E6xxx SERDES manipulation, via SMI bus
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2016 Andrew Lunn <andrew@lunn.ch>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _MV88E6XXX_SERDES_H
#define _MV88E6XXX_SERDES_H

#include "chip.h"

#define MV88E6352_ADDR_SERDES		0x0f
#define MV88E6352_SERDES_PAGE_FIBER	0x01

#define MV88E6390_PORT9_LANE0		0x09
#define MV88E6390_PORT9_LANE1		0x12
#define MV88E6390_PORT9_LANE2		0x13
#define MV88E6390_PORT9_LANE3		0x14
#define MV88E6390_PORT10_LANE0		0x0a
#define MV88E6390_PORT10_LANE1		0x15
#define MV88E6390_PORT10_LANE2		0x16
#define MV88E6390_PORT10_LANE3		0x17
#define MV88E6390_SERDES_DEVICE		(4 << 16)

/* 10GBASE-R and 10GBASE-X4/X2 */
#define MV88E6390_PCS_CONTROL_1		0x1000
#define MV88E6390_PCS_CONTROL_1_RESET		BIT(15)
#define MV88E6390_PCS_CONTROL_1_LOOPBACK	BIT(14)
#define MV88E6390_PCS_CONTROL_1_SPEED		BIT(13)
#define MV88E6390_PCS_CONTROL_1_PDOWN		BIT(11)

/* 1000BASE-X and SGMII */
#define MV88E6390_SGMII_CONTROL		0x2000
#define MV88E6390_SGMII_CONTROL_RESET		BIT(15)
#define MV88E6390_SGMII_CONTROL_LOOPBACK	BIT(14)
#define MV88E6390_SGMII_CONTROL_PDOWN		BIT(11)

int mv88e6352_serdes_power(struct mv88e6xxx_chip *chip, int port, bool on);
int mv88e6390_serdes_power(struct mv88e6xxx_chip *chip, int port, bool on);

#endif
