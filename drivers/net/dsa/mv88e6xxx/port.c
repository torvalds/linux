/*
 * Marvell 88E6xxx Switch Port Registers support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2016 Vivien Didelot <vivien.didelot@savoirfairelinux.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "mv88e6xxx.h"
#include "port.h"

int mv88e6xxx_port_read(struct mv88e6xxx_chip *chip, int port, int reg,
			u16 *val)
{
	int addr = chip->info->port_base_addr + port;

	return mv88e6xxx_read(chip, addr, reg, val);
}

int mv88e6xxx_port_write(struct mv88e6xxx_chip *chip, int port, int reg,
			 u16 val)
{
	int addr = chip->info->port_base_addr + port;

	return mv88e6xxx_write(chip, addr, reg, val);
}

/* Offset 0x04: Port Control Register */

static const char * const mv88e6xxx_port_state_names[] = {
	[PORT_CONTROL_STATE_DISABLED] = "Disabled",
	[PORT_CONTROL_STATE_BLOCKING] = "Blocking/Listening",
	[PORT_CONTROL_STATE_LEARNING] = "Learning",
	[PORT_CONTROL_STATE_FORWARDING] = "Forwarding",
};

int mv88e6xxx_port_set_state(struct mv88e6xxx_chip *chip, int port, u8 state)
{
	u16 reg;
	int err;

	err = mv88e6xxx_port_read(chip, port, PORT_CONTROL, &reg);
	if (err)
		return err;

	reg &= ~PORT_CONTROL_STATE_MASK;
	reg |= state;

	err = mv88e6xxx_port_write(chip, port, PORT_CONTROL, reg);
	if (err)
		return err;

	netdev_dbg(chip->ds->ports[port].netdev, "PortState set to %s\n",
		   mv88e6xxx_port_state_names[state]);

	return 0;
}
