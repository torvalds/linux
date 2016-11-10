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
 *
 * For port's MAC speed, ForceSpd (or SpdValue) bits 1:0 program the value.
 * Alternative values require the 200BASE (or AltSpeed) bit 12 set.
 * Newer chips need a ForcedSpd bit 13 set to consider the value.
 */

static int mv88e6xxx_port_set_rgmii_delay(struct mv88e6xxx_chip *chip, int port,
					  phy_interface_t mode)
{
	u16 reg;
	int err;

	err = mv88e6xxx_port_read(chip, port, PORT_PCS_CTRL, &reg);
	if (err)
		return err;

	reg &= ~(PORT_PCS_CTRL_RGMII_DELAY_RXCLK |
		 PORT_PCS_CTRL_RGMII_DELAY_TXCLK);

	switch (mode) {
	case PHY_INTERFACE_MODE_RGMII_RXID:
		reg |= PORT_PCS_CTRL_RGMII_DELAY_RXCLK;
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		reg |= PORT_PCS_CTRL_RGMII_DELAY_TXCLK;
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		reg |= PORT_PCS_CTRL_RGMII_DELAY_RXCLK |
			PORT_PCS_CTRL_RGMII_DELAY_TXCLK;
		break;
	case PHY_INTERFACE_MODE_RGMII:
		break;
	default:
		return 0;
	}

	err = mv88e6xxx_port_write(chip, port, PORT_PCS_CTRL, reg);
	if (err)
		return err;

	netdev_dbg(chip->ds->ports[port].netdev, "delay RXCLK %s, TXCLK %s\n",
		   reg & PORT_PCS_CTRL_RGMII_DELAY_RXCLK ? "yes" : "no",
		   reg & PORT_PCS_CTRL_RGMII_DELAY_TXCLK ? "yes" : "no");

	return 0;
}

int mv88e6352_port_set_rgmii_delay(struct mv88e6xxx_chip *chip, int port,
				   phy_interface_t mode)
{
	if (port < 5)
		return -EOPNOTSUPP;

	return mv88e6xxx_port_set_rgmii_delay(chip, port, mode);
}

int mv88e6390_port_set_rgmii_delay(struct mv88e6xxx_chip *chip, int port,
				   phy_interface_t mode)
{
	if (port != 0)
		return -EOPNOTSUPP;

	return mv88e6xxx_port_set_rgmii_delay(chip, port, mode);
}

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

int mv88e6xxx_port_set_duplex(struct mv88e6xxx_chip *chip, int port, int dup)
{
	u16 reg;
	int err;

	err = mv88e6xxx_port_read(chip, port, PORT_PCS_CTRL, &reg);
	if (err)
		return err;

	reg &= ~(PORT_PCS_CTRL_FORCE_DUPLEX | PORT_PCS_CTRL_DUPLEX_FULL);

	switch (dup) {
	case DUPLEX_HALF:
		reg |= PORT_PCS_CTRL_FORCE_DUPLEX;
		break;
	case DUPLEX_FULL:
		reg |= PORT_PCS_CTRL_FORCE_DUPLEX | PORT_PCS_CTRL_DUPLEX_FULL;
		break;
	case DUPLEX_UNFORCED:
		/* normal duplex detection */
		break;
	default:
		return -EINVAL;
	}

	err = mv88e6xxx_port_write(chip, port, PORT_PCS_CTRL, reg);
	if (err)
		return err;

	netdev_dbg(chip->ds->ports[port].netdev, "%s %s duplex\n",
		   reg & PORT_PCS_CTRL_FORCE_DUPLEX ? "Force" : "Unforce",
		   reg & PORT_PCS_CTRL_DUPLEX_FULL ? "full" : "half");

	return 0;
}

static int mv88e6xxx_port_set_speed(struct mv88e6xxx_chip *chip, int port,
				    int speed, bool alt_bit, bool force_bit)
{
	u16 reg, ctrl;
	int err;

	switch (speed) {
	case 10:
		ctrl = PORT_PCS_CTRL_SPEED_10;
		break;
	case 100:
		ctrl = PORT_PCS_CTRL_SPEED_100;
		break;
	case 200:
		if (alt_bit)
			ctrl = PORT_PCS_CTRL_SPEED_100 | PORT_PCS_CTRL_ALTSPEED;
		else
			ctrl = PORT_PCS_CTRL_SPEED_200;
		break;
	case 1000:
		ctrl = PORT_PCS_CTRL_SPEED_1000;
		break;
	case 2500:
		ctrl = PORT_PCS_CTRL_SPEED_1000 | PORT_PCS_CTRL_ALTSPEED;
		break;
	case 10000:
		/* all bits set, fall through... */
	case SPEED_UNFORCED:
		ctrl = PORT_PCS_CTRL_SPEED_UNFORCED;
		break;
	default:
		return -EOPNOTSUPP;
	}

	err = mv88e6xxx_port_read(chip, port, PORT_PCS_CTRL, &reg);
	if (err)
		return err;

	reg &= ~PORT_PCS_CTRL_SPEED_MASK;
	if (alt_bit)
		reg &= ~PORT_PCS_CTRL_ALTSPEED;
	if (force_bit) {
		reg &= ~PORT_PCS_CTRL_FORCE_SPEED;
		if (speed)
			ctrl |= PORT_PCS_CTRL_FORCE_SPEED;
	}
	reg |= ctrl;

	err = mv88e6xxx_port_write(chip, port, PORT_PCS_CTRL, reg);
	if (err)
		return err;

	if (speed)
		netdev_dbg(chip->ds->ports[port].netdev,
			   "Speed set to %d Mbps\n", speed);
	else
		netdev_dbg(chip->ds->ports[port].netdev, "Speed unforced\n");

	return 0;
}

/* Support 10, 100, 200 Mbps (e.g. 88E6065 family) */
int mv88e6065_port_set_speed(struct mv88e6xxx_chip *chip, int port, int speed)
{
	if (speed == SPEED_MAX)
		speed = 200;

	if (speed > 200)
		return -EOPNOTSUPP;

	/* Setting 200 Mbps on port 0 to 3 selects 100 Mbps */
	return mv88e6xxx_port_set_speed(chip, port, speed, false, false);
}

/* Support 10, 100, 1000 Mbps (e.g. 88E6185 family) */
int mv88e6185_port_set_speed(struct mv88e6xxx_chip *chip, int port, int speed)
{
	if (speed == SPEED_MAX)
		speed = 1000;

	if (speed == 200 || speed > 1000)
		return -EOPNOTSUPP;

	return mv88e6xxx_port_set_speed(chip, port, speed, false, false);
}

/* Support 10, 100, 200, 1000 Mbps (e.g. 88E6352 family) */
int mv88e6352_port_set_speed(struct mv88e6xxx_chip *chip, int port, int speed)
{
	if (speed == SPEED_MAX)
		speed = 1000;

	if (speed > 1000)
		return -EOPNOTSUPP;

	if (speed == 200 && port < 5)
		return -EOPNOTSUPP;

	return mv88e6xxx_port_set_speed(chip, port, speed, true, false);
}

/* Support 10, 100, 200, 1000, 2500 Mbps (e.g. 88E6390) */
int mv88e6390_port_set_speed(struct mv88e6xxx_chip *chip, int port, int speed)
{
	if (speed == SPEED_MAX)
		speed = port < 9 ? 1000 : 2500;

	if (speed > 2500)
		return -EOPNOTSUPP;

	if (speed == 200 && port != 0)
		return -EOPNOTSUPP;

	if (speed == 2500 && port < 9)
		return -EOPNOTSUPP;

	return mv88e6xxx_port_set_speed(chip, port, speed, true, true);
}

/* Support 10, 100, 200, 1000, 2500, 10000 Mbps (e.g. 88E6190X) */
int mv88e6390x_port_set_speed(struct mv88e6xxx_chip *chip, int port, int speed)
{
	if (speed == SPEED_MAX)
		speed = port < 9 ? 1000 : 10000;

	if (speed == 200 && port != 0)
		return -EOPNOTSUPP;

	if (speed >= 2500 && port < 9)
		return -EOPNOTSUPP;

	return mv88e6xxx_port_set_speed(chip, port, speed, true, true);
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
