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

/* Offset 0x01: MAC (or PCS or Physical) Control Register
 *
 * Link, Duplex and Flow Control have one force bit, one value bit.
 */

int mv88e6xxx_port_set_link(struct mv88e6xxx_chip *chip, int port, int link)
{
	u16 reg;
	int err;

	err = mv88e6xxx_port_read(chip, port, PORT_PCS_CTRL, &reg);
	if (err)
		return err;

	reg &= ~(PORT_PCS_CTRL_FORCE_LINK | PORT_PCS_CTRL_LINK_UP);

	switch (link) {
	case LINK_FORCED_DOWN:
		reg |= PORT_PCS_CTRL_FORCE_LINK;
		break;
	case LINK_FORCED_UP:
		reg |= PORT_PCS_CTRL_FORCE_LINK | PORT_PCS_CTRL_LINK_UP;
		break;
	case LINK_UNFORCED:
		/* normal link detection */
		break;
	default:
		return -EINVAL;
	}

	err = mv88e6xxx_port_write(chip, port, PORT_PCS_CTRL, reg);
	if (err)
		return err;

	netdev_dbg(chip->ds->ports[port].netdev, "%s link %s\n",
		   reg & PORT_PCS_CTRL_FORCE_LINK ? "Force" : "Unforce",
		   reg & PORT_PCS_CTRL_LINK_UP ? "up" : "down");

	return 0;
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

/* Offset 0x05: Port Control 1 */

/* Offset 0x06: Port Based VLAN Map */

int mv88e6xxx_port_set_vlan_map(struct mv88e6xxx_chip *chip, int port, u16 map)
{
	const u16 mask = GENMASK(mv88e6xxx_num_ports(chip) - 1, 0);
	u16 reg;
	int err;

	err = mv88e6xxx_port_read(chip, port, PORT_BASE_VLAN, &reg);
	if (err)
		return err;

	reg &= ~mask;
	reg |= map & mask;

	err = mv88e6xxx_port_write(chip, port, PORT_BASE_VLAN, reg);
	if (err)
		return err;

	netdev_dbg(chip->ds->ports[port].netdev, "VLANTable set to %.3x\n",
		   map);

	return 0;
}

int mv88e6xxx_port_get_fid(struct mv88e6xxx_chip *chip, int port, u16 *fid)
{
	const u16 upper_mask = (mv88e6xxx_num_databases(chip) - 1) >> 4;
	u16 reg;
	int err;

	/* Port's default FID lower 4 bits are located in reg 0x06, offset 12 */
	err = mv88e6xxx_port_read(chip, port, PORT_BASE_VLAN, &reg);
	if (err)
		return err;

	*fid = (reg & 0xf000) >> 12;

	/* Port's default FID upper bits are located in reg 0x05, offset 0 */
	if (upper_mask) {
		err = mv88e6xxx_port_read(chip, port, PORT_CONTROL_1, &reg);
		if (err)
			return err;

		*fid |= (reg & upper_mask) << 4;
	}

	return 0;
}

int mv88e6xxx_port_set_fid(struct mv88e6xxx_chip *chip, int port, u16 fid)
{
	const u16 upper_mask = (mv88e6xxx_num_databases(chip) - 1) >> 4;
	u16 reg;
	int err;

	if (fid >= mv88e6xxx_num_databases(chip))
		return -EINVAL;

	/* Port's default FID lower 4 bits are located in reg 0x06, offset 12 */
	err = mv88e6xxx_port_read(chip, port, PORT_BASE_VLAN, &reg);
	if (err)
		return err;

	reg &= 0x0fff;
	reg |= (fid & 0x000f) << 12;

	err = mv88e6xxx_port_write(chip, port, PORT_BASE_VLAN, reg);
	if (err)
		return err;

	/* Port's default FID upper bits are located in reg 0x05, offset 0 */
	if (upper_mask) {
		err = mv88e6xxx_port_read(chip, port, PORT_CONTROL_1, &reg);
		if (err)
			return err;

		reg &= ~upper_mask;
		reg |= (fid >> 4) & upper_mask;

		err = mv88e6xxx_port_write(chip, port, PORT_CONTROL_1, reg);
		if (err)
			return err;
	}

	netdev_dbg(chip->ds->ports[port].netdev, "FID set to %u\n", fid);

	return 0;
}

/* Offset 0x07: Default Port VLAN ID & Priority */

int mv88e6xxx_port_get_pvid(struct mv88e6xxx_chip *chip, int port, u16 *pvid)
{
	u16 reg;
	int err;

	err = mv88e6xxx_port_read(chip, port, PORT_DEFAULT_VLAN, &reg);
	if (err)
		return err;

	*pvid = reg & PORT_DEFAULT_VLAN_MASK;

	return 0;
}

int mv88e6xxx_port_set_pvid(struct mv88e6xxx_chip *chip, int port, u16 pvid)
{
	u16 reg;
	int err;

	err = mv88e6xxx_port_read(chip, port, PORT_DEFAULT_VLAN, &reg);
	if (err)
		return err;

	reg &= ~PORT_DEFAULT_VLAN_MASK;
	reg |= pvid & PORT_DEFAULT_VLAN_MASK;

	err = mv88e6xxx_port_write(chip, port, PORT_DEFAULT_VLAN, reg);
	if (err)
		return err;

	netdev_dbg(chip->ds->ports[port].netdev, "DefaultVID set to %u\n",
		   pvid);

	return 0;
}

/* Offset 0x08: Port Control 2 Register */

static const char * const mv88e6xxx_port_8021q_mode_names[] = {
	[PORT_CONTROL_2_8021Q_DISABLED] = "Disabled",
	[PORT_CONTROL_2_8021Q_FALLBACK] = "Fallback",
	[PORT_CONTROL_2_8021Q_CHECK] = "Check",
	[PORT_CONTROL_2_8021Q_SECURE] = "Secure",
};

int mv88e6xxx_port_set_8021q_mode(struct mv88e6xxx_chip *chip, int port,
				  u16 mode)
{
	u16 reg;
	int err;

	err = mv88e6xxx_port_read(chip, port, PORT_CONTROL_2, &reg);
	if (err)
		return err;

	reg &= ~PORT_CONTROL_2_8021Q_MASK;
	reg |= mode & PORT_CONTROL_2_8021Q_MASK;

	err = mv88e6xxx_port_write(chip, port, PORT_CONTROL_2, reg);
	if (err)
		return err;

	netdev_dbg(chip->ds->ports[port].netdev, "802.1QMode set to %s\n",
		   mv88e6xxx_port_8021q_mode_names[mode]);

	return 0;
}
