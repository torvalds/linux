/*
 * Marvell 88E6xxx Switch Port Registers support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2016-2017 Savoir-faire Linux Inc.
 *	Vivien Didelot <vivien.didelot@savoirfairelinux.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _MV88E6XXX_PORT_H
#define _MV88E6XXX_PORT_H

#include "mv88e6xxx.h"

int mv88e6xxx_port_read(struct mv88e6xxx_chip *chip, int port, int reg,
			u16 *val);
int mv88e6xxx_port_write(struct mv88e6xxx_chip *chip, int port, int reg,
			 u16 val);

int mv88e6352_port_set_rgmii_delay(struct mv88e6xxx_chip *chip, int port,
				   phy_interface_t mode);
int mv88e6390_port_set_rgmii_delay(struct mv88e6xxx_chip *chip, int port,
				   phy_interface_t mode);

int mv88e6xxx_port_set_link(struct mv88e6xxx_chip *chip, int port, int link);

int mv88e6xxx_port_set_duplex(struct mv88e6xxx_chip *chip, int port, int dup);

int mv88e6065_port_set_speed(struct mv88e6xxx_chip *chip, int port, int speed);
int mv88e6185_port_set_speed(struct mv88e6xxx_chip *chip, int port, int speed);
int mv88e6352_port_set_speed(struct mv88e6xxx_chip *chip, int port, int speed);
int mv88e6390_port_set_speed(struct mv88e6xxx_chip *chip, int port, int speed);
int mv88e6390x_port_set_speed(struct mv88e6xxx_chip *chip, int port, int speed);

int mv88e6xxx_port_set_state(struct mv88e6xxx_chip *chip, int port, u8 state);

int mv88e6xxx_port_set_vlan_map(struct mv88e6xxx_chip *chip, int port, u16 map);

int mv88e6xxx_port_get_fid(struct mv88e6xxx_chip *chip, int port, u16 *fid);
int mv88e6xxx_port_set_fid(struct mv88e6xxx_chip *chip, int port, u16 fid);

int mv88e6xxx_port_get_pvid(struct mv88e6xxx_chip *chip, int port, u16 *pvid);
int mv88e6xxx_port_set_pvid(struct mv88e6xxx_chip *chip, int port, u16 pvid);

int mv88e6xxx_port_set_8021q_mode(struct mv88e6xxx_chip *chip, int port,
				  u16 mode);
int mv88e6095_port_tag_remap(struct mv88e6xxx_chip *chip, int port);
int mv88e6390_port_tag_remap(struct mv88e6xxx_chip *chip, int port);
int mv88e6xxx_port_set_egress_mode(struct mv88e6xxx_chip *chip, int port,
				   u16 mode);
int mv88e6085_port_set_frame_mode(struct mv88e6xxx_chip *chip, int port,
				  enum mv88e6xxx_frame_mode mode);
int mv88e6351_port_set_frame_mode(struct mv88e6xxx_chip *chip, int port,
				  enum mv88e6xxx_frame_mode mode);
int mv88e6185_port_set_egress_floods(struct mv88e6xxx_chip *chip, int port,
				     bool unicast, bool multicast);
int mv88e6352_port_set_egress_floods(struct mv88e6xxx_chip *chip, int port,
				     bool unicast, bool multicast);
int mv88e6351_port_set_ether_type(struct mv88e6xxx_chip *chip, int port,
				  u16 etype);
int mv88e6xxx_port_set_message_port(struct mv88e6xxx_chip *chip, int port,
				    bool message_port);
int mv88e6165_port_jumbo_config(struct mv88e6xxx_chip *chip, int port);
int mv88e6095_port_egress_rate_limiting(struct mv88e6xxx_chip *chip, int port);
int mv88e6097_port_egress_rate_limiting(struct mv88e6xxx_chip *chip, int port);
int mv88e6097_port_pause_config(struct mv88e6xxx_chip *chip, int port);
int mv88e6390_port_pause_config(struct mv88e6xxx_chip *chip, int port);
int mv88e6390x_port_set_cmode(struct mv88e6xxx_chip *chip, int port,
			      phy_interface_t mode);
int mv88e6xxx_port_get_cmode(struct mv88e6xxx_chip *chip, int port, u8 *cmode);
int mv88e6xxx_port_set_map_da(struct mv88e6xxx_chip *chip, int port);
int mv88e6095_port_set_upstream_port(struct mv88e6xxx_chip *chip, int port,
				     int upstream_port);

int mv88e6xxx_port_disable_learn_limit(struct mv88e6xxx_chip *chip, int port);
int mv88e6xxx_port_disable_pri_override(struct mv88e6xxx_chip *chip, int port);

#endif /* _MV88E6XXX_PORT_H */
