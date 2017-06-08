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

#include "chip.h"

#define PORT_STATUS		0x00
#define PORT_STATUS_PAUSE_EN	BIT(15)
#define PORT_STATUS_MY_PAUSE	BIT(14)
#define PORT_STATUS_HD_FLOW	BIT(13)
#define PORT_STATUS_PHY_DETECT	BIT(12)
#define PORT_STATUS_LINK	BIT(11)
#define PORT_STATUS_DUPLEX	BIT(10)
#define PORT_STATUS_SPEED_MASK	0x0300
#define PORT_STATUS_SPEED_10	0x0000
#define PORT_STATUS_SPEED_100	0x0100
#define PORT_STATUS_SPEED_1000	0x0200
#define PORT_STATUS_EEE		BIT(6) /* 6352 */
#define PORT_STATUS_AM_DIS	BIT(6) /* 6165 */
#define PORT_STATUS_MGMII	BIT(6) /* 6185 */
#define PORT_STATUS_TX_PAUSED	BIT(5)
#define PORT_STATUS_FLOW_CTRL	BIT(4)
#define PORT_STATUS_CMODE_MASK	0x0f
#define PORT_STATUS_CMODE_100BASE_X	0x8
#define PORT_STATUS_CMODE_1000BASE_X	0x9
#define PORT_STATUS_CMODE_SGMII		0xa
#define PORT_STATUS_CMODE_2500BASEX	0xb
#define PORT_STATUS_CMODE_XAUI		0xc
#define PORT_STATUS_CMODE_RXAUI		0xd
#define PORT_PCS_CTRL		0x01
#define PORT_PCS_CTRL_RGMII_DELAY_RXCLK	BIT(15)
#define PORT_PCS_CTRL_RGMII_DELAY_TXCLK	BIT(14)
#define PORT_PCS_CTRL_FORCE_SPEED	BIT(13) /* 6390 */
#define PORT_PCS_CTRL_ALTSPEED		BIT(12) /* 6390 */
#define PORT_PCS_CTRL_200BASE		BIT(12) /* 6352 */
#define PORT_PCS_CTRL_FC		BIT(7)
#define PORT_PCS_CTRL_FORCE_FC		BIT(6)
#define PORT_PCS_CTRL_LINK_UP		BIT(5)
#define PORT_PCS_CTRL_FORCE_LINK	BIT(4)
#define PORT_PCS_CTRL_DUPLEX_FULL	BIT(3)
#define PORT_PCS_CTRL_FORCE_DUPLEX	BIT(2)
#define PORT_PCS_CTRL_SPEED_MASK	(0x03)
#define PORT_PCS_CTRL_SPEED_10		(0x00)
#define PORT_PCS_CTRL_SPEED_100		(0x01)
#define PORT_PCS_CTRL_SPEED_200		(0x02) /* 6065 and non Gb chips */
#define PORT_PCS_CTRL_SPEED_1000	(0x02)
#define PORT_PCS_CTRL_SPEED_10000	(0x03) /* 6390X */
#define PORT_PCS_CTRL_SPEED_UNFORCED	(0x03)
#define PORT_PAUSE_CTRL		0x02
#define PORT_FLOW_CTRL_LIMIT_IN		((0x00 << 8) | BIT(15))
#define PORT_FLOW_CTRL_LIMIT_OUT	((0x01 << 8) | BIT(15))
#define PORT_SWITCH_ID		0x03
#define PORT_SWITCH_ID_PROD_NUM_6085	0x04a
#define PORT_SWITCH_ID_PROD_NUM_6095	0x095
#define PORT_SWITCH_ID_PROD_NUM_6097	0x099
#define PORT_SWITCH_ID_PROD_NUM_6131	0x106
#define PORT_SWITCH_ID_PROD_NUM_6320	0x115
#define PORT_SWITCH_ID_PROD_NUM_6123	0x121
#define PORT_SWITCH_ID_PROD_NUM_6141	0x340
#define PORT_SWITCH_ID_PROD_NUM_6161	0x161
#define PORT_SWITCH_ID_PROD_NUM_6165	0x165
#define PORT_SWITCH_ID_PROD_NUM_6171	0x171
#define PORT_SWITCH_ID_PROD_NUM_6172	0x172
#define PORT_SWITCH_ID_PROD_NUM_6175	0x175
#define PORT_SWITCH_ID_PROD_NUM_6176	0x176
#define PORT_SWITCH_ID_PROD_NUM_6185	0x1a7
#define PORT_SWITCH_ID_PROD_NUM_6190	0x190
#define PORT_SWITCH_ID_PROD_NUM_6190X	0x0a0
#define PORT_SWITCH_ID_PROD_NUM_6191	0x191
#define PORT_SWITCH_ID_PROD_NUM_6240	0x240
#define PORT_SWITCH_ID_PROD_NUM_6290	0x290
#define PORT_SWITCH_ID_PROD_NUM_6321	0x310
#define PORT_SWITCH_ID_PROD_NUM_6341	0x341
#define PORT_SWITCH_ID_PROD_NUM_6352	0x352
#define PORT_SWITCH_ID_PROD_NUM_6350	0x371
#define PORT_SWITCH_ID_PROD_NUM_6351	0x375
#define PORT_SWITCH_ID_PROD_NUM_6390	0x390
#define PORT_SWITCH_ID_PROD_NUM_6390X	0x0a1
#define PORT_CONTROL		0x04
#define PORT_CONTROL_USE_CORE_TAG	BIT(15)
#define PORT_CONTROL_DROP_ON_LOCK	BIT(14)
#define PORT_CONTROL_EGRESS_UNMODIFIED	(0x0 << 12)
#define PORT_CONTROL_EGRESS_UNTAGGED	(0x1 << 12)
#define PORT_CONTROL_EGRESS_TAGGED	(0x2 << 12)
#define PORT_CONTROL_EGRESS_ADD_TAG	(0x3 << 12)
#define PORT_CONTROL_EGRESS_MASK	(0x3 << 12)
#define PORT_CONTROL_HEADER		BIT(11)
#define PORT_CONTROL_IGMP_MLD_SNOOP	BIT(10)
#define PORT_CONTROL_DOUBLE_TAG		BIT(9)
#define PORT_CONTROL_FRAME_MODE_NORMAL		(0x0 << 8)
#define PORT_CONTROL_FRAME_MODE_DSA		(0x1 << 8)
#define PORT_CONTROL_FRAME_MODE_PROVIDER	(0x2 << 8)
#define PORT_CONTROL_FRAME_ETHER_TYPE_DSA	(0x3 << 8)
#define PORT_CONTROL_FRAME_MASK			(0x3 << 8)
#define PORT_CONTROL_DSA_TAG		BIT(8)
#define PORT_CONTROL_VLAN_TUNNEL	BIT(7)
#define PORT_CONTROL_TAG_IF_BOTH	BIT(6)
#define PORT_CONTROL_USE_IP		BIT(5)
#define PORT_CONTROL_USE_TAG		BIT(4)
#define PORT_CONTROL_FORWARD_UNKNOWN	BIT(2)
#define PORT_CONTROL_EGRESS_FLOODS_MASK			(0x3 << 2)
#define PORT_CONTROL_EGRESS_FLOODS_NO_UNKNOWN_DA	(0x0 << 2)
#define PORT_CONTROL_EGRESS_FLOODS_NO_UNKNOWN_MC_DA	(0x1 << 2)
#define PORT_CONTROL_EGRESS_FLOODS_NO_UNKNOWN_UC_DA	(0x2 << 2)
#define PORT_CONTROL_EGRESS_FLOODS_ALL_UNKNOWN_DA	(0x3 << 2)
#define PORT_CONTROL_STATE_MASK		0x03
#define PORT_CONTROL_STATE_DISABLED	0x00
#define PORT_CONTROL_STATE_BLOCKING	0x01
#define PORT_CONTROL_STATE_LEARNING	0x02
#define PORT_CONTROL_STATE_FORWARDING	0x03
#define PORT_CONTROL_1		0x05
#define PORT_CONTROL_1_MESSAGE_PORT	BIT(15)
#define PORT_CONTROL_1_FID_11_4_MASK	(0xff << 0)
#define PORT_BASE_VLAN		0x06
#define PORT_BASE_VLAN_FID_3_0_MASK	(0xf << 12)
#define PORT_DEFAULT_VLAN	0x07
#define PORT_DEFAULT_VLAN_MASK	0xfff
#define PORT_CONTROL_2		0x08
#define PORT_CONTROL_2_IGNORE_FCS	BIT(15)
#define PORT_CONTROL_2_VTU_PRI_OVERRIDE	BIT(14)
#define PORT_CONTROL_2_SA_PRIO_OVERRIDE	BIT(13)
#define PORT_CONTROL_2_DA_PRIO_OVERRIDE	BIT(12)
#define PORT_CONTROL_2_JUMBO_MASK	(0x03 << 12)
#define PORT_CONTROL_2_JUMBO_1522	(0x00 << 12)
#define PORT_CONTROL_2_JUMBO_2048	(0x01 << 12)
#define PORT_CONTROL_2_JUMBO_10240	(0x02 << 12)
#define PORT_CONTROL_2_8021Q_MASK	(0x03 << 10)
#define PORT_CONTROL_2_8021Q_DISABLED	(0x00 << 10)
#define PORT_CONTROL_2_8021Q_FALLBACK	(0x01 << 10)
#define PORT_CONTROL_2_8021Q_CHECK	(0x02 << 10)
#define PORT_CONTROL_2_8021Q_SECURE	(0x03 << 10)
#define PORT_CONTROL_2_DISCARD_TAGGED	BIT(9)
#define PORT_CONTROL_2_DISCARD_UNTAGGED	BIT(8)
#define PORT_CONTROL_2_MAP_DA		BIT(7)
#define PORT_CONTROL_2_DEFAULT_FORWARD	BIT(6)
#define PORT_CONTROL_2_EGRESS_MONITOR	BIT(5)
#define PORT_CONTROL_2_INGRESS_MONITOR	BIT(4)
#define PORT_CONTROL_2_UPSTREAM_MASK	0x0f
#define PORT_RATE_CONTROL	0x09
#define PORT_RATE_CONTROL_2	0x0a
#define PORT_ASSOC_VECTOR	0x0b
#define PORT_ASSOC_VECTOR_HOLD_AT_1		BIT(15)
#define PORT_ASSOC_VECTOR_INT_AGE_OUT		BIT(14)
#define PORT_ASSOC_VECTOR_LOCKED_PORT		BIT(13)
#define PORT_ASSOC_VECTOR_IGNORE_WRONG		BIT(12)
#define PORT_ASSOC_VECTOR_REFRESH_LOCKED	BIT(11)
#define PORT_ATU_CONTROL	0x0c
#define PORT_PRI_OVERRIDE	0x0d
#define PORT_ETH_TYPE		0x0f
#define PORT_ETH_TYPE_DEFAULT	0x9100
#define PORT_IN_DISCARD_LO	0x10
#define PORT_IN_DISCARD_HI	0x11
#define PORT_IN_FILTERED	0x12
#define PORT_OUT_FILTERED	0x13
#define PORT_TAG_REGMAP_0123	0x18
#define PORT_TAG_REGMAP_4567	0x19
#define PORT_IEEE_PRIO_MAP_TABLE	0x18    /* 6390 */
#define PORT_IEEE_PRIO_MAP_TABLE_UPDATE		BIT(15)
#define PORT_IEEE_PRIO_MAP_TABLE_INGRESS_PCP		(0x0 << 12)
#define PORT_IEEE_PRIO_MAP_TABLE_EGRESS_GREEN_PCP	(0x1 << 12)
#define PORT_IEEE_PRIO_MAP_TABLE_EGRESS_YELLOW_PCP	(0x2 << 12)
#define PORT_IEEE_PRIO_MAP_TABLE_EGRESS_AVB_PCP		(0x3 << 12)
#define PORT_IEEE_PRIO_MAP_TABLE_EGRESS_GREEN_DSCP	(0x5 << 12)
#define PORT_IEEE_PRIO_MAP_TABLE_EGRESS_YELLOW_DSCP	(0x6 << 12)
#define PORT_IEEE_PRIO_MAP_TABLE_EGRESS_AVB_DSCP	(0x7 << 12)
#define PORT_IEEE_PRIO_MAP_TABLE_POINTER_SHIFT		9

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
				   enum mv88e6xxx_egress_mode mode);
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
int mv88e6165_port_set_jumbo_size(struct mv88e6xxx_chip *chip, int port,
				  size_t size);
int mv88e6095_port_egress_rate_limiting(struct mv88e6xxx_chip *chip, int port);
int mv88e6097_port_egress_rate_limiting(struct mv88e6xxx_chip *chip, int port);
int mv88e6097_port_pause_limit(struct mv88e6xxx_chip *chip, int port, u8 in,
			       u8 out);
int mv88e6390_port_pause_limit(struct mv88e6xxx_chip *chip, int port, u8 in,
			       u8 out);
int mv88e6390x_port_set_cmode(struct mv88e6xxx_chip *chip, int port,
			      phy_interface_t mode);
int mv88e6xxx_port_get_cmode(struct mv88e6xxx_chip *chip, int port, u8 *cmode);
int mv88e6xxx_port_set_map_da(struct mv88e6xxx_chip *chip, int port);
int mv88e6095_port_set_upstream_port(struct mv88e6xxx_chip *chip, int port,
				     int upstream_port);

int mv88e6xxx_port_disable_learn_limit(struct mv88e6xxx_chip *chip, int port);
int mv88e6xxx_port_disable_pri_override(struct mv88e6xxx_chip *chip, int port);

#endif /* _MV88E6XXX_PORT_H */
