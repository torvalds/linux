// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Marvell 88E6xxx Switch Hidden Registers support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2019 Andrew Lunn <andrew@lunn.ch>
 */

#include <linux/bitfield.h>

#include "chip.h"
#include "port.h"

/* The mv88e6390 and mv88e6341 have some hidden registers used for debug and
 * development. The errata also makes use of them.
 */
int mv88e6xxx_port_hidden_write(struct mv88e6xxx_chip *chip, int block,
				int port, int reg, u16 val)
{
	u16 ctrl;
	int err;

	err = mv88e6xxx_port_write(chip, MV88E6XXX_PORT_RESERVED_1A_DATA_PORT,
				   MV88E6XXX_PORT_RESERVED_1A, val);
	if (err)
		return err;

	ctrl = MV88E6XXX_PORT_RESERVED_1A_BUSY |
	       MV88E6XXX_PORT_RESERVED_1A_WRITE |
	       block << MV88E6XXX_PORT_RESERVED_1A_BLOCK_SHIFT |
	       port << MV88E6XXX_PORT_RESERVED_1A_PORT_SHIFT |
	       reg;

	return mv88e6xxx_port_write(chip, MV88E6XXX_PORT_RESERVED_1A_CTRL_PORT,
				    MV88E6XXX_PORT_RESERVED_1A, ctrl);
}

int mv88e6xxx_port_hidden_wait(struct mv88e6xxx_chip *chip)
{
	int bit = __bf_shf(MV88E6XXX_PORT_RESERVED_1A_BUSY);

	return mv88e6xxx_wait_bit(chip, MV88E6XXX_PORT_RESERVED_1A_CTRL_PORT,
				  MV88E6XXX_PORT_RESERVED_1A, bit, 0);
}

int mv88e6xxx_port_hidden_read(struct mv88e6xxx_chip *chip, int block, int port,
			       int reg, u16 *val)
{
	u16 ctrl;
	int err;

	ctrl = MV88E6XXX_PORT_RESERVED_1A_BUSY |
	       MV88E6XXX_PORT_RESERVED_1A_READ |
	       block << MV88E6XXX_PORT_RESERVED_1A_BLOCK_SHIFT |
	       port << MV88E6XXX_PORT_RESERVED_1A_PORT_SHIFT |
	       reg;

	err = mv88e6xxx_port_write(chip, MV88E6XXX_PORT_RESERVED_1A_CTRL_PORT,
				   MV88E6XXX_PORT_RESERVED_1A, ctrl);
	if (err)
		return err;

	err = mv88e6xxx_port_hidden_wait(chip);
	if (err)
		return err;

	return mv88e6xxx_port_read(chip, MV88E6XXX_PORT_RESERVED_1A_DATA_PORT,
				   MV88E6XXX_PORT_RESERVED_1A, val);
}
