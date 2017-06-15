/*
 * Marvell 88E6xxx Switch Global (1) Registers support
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

#ifndef _MV88E6XXX_GLOBAL1_H
#define _MV88E6XXX_GLOBAL1_H

#include "chip.h"

/* Offset 0x00: Switch Global Status Register */
#define MV88E6XXX_G1_STS				0x00
#define MV88E6352_G1_STS_PPU_STATE			0x8000
#define MV88E6185_G1_STS_PPU_STATE_MASK			0xc000
#define MV88E6185_G1_STS_PPU_STATE_DISABLED_RST		0x0000
#define MV88E6185_G1_STS_PPU_STATE_INITIALIZING		0x4000
#define MV88E6185_G1_STS_PPU_STATE_DISABLED		0x8000
#define MV88E6185_G1_STS_PPU_STATE_POLLING		0xc000
#define MV88E6XXX_G1_STS_INIT_READY			0x0800
#define MV88E6XXX_G1_STS_IRQ_AVB			8
#define MV88E6XXX_G1_STS_IRQ_DEVICE			7
#define MV88E6XXX_G1_STS_IRQ_STATS			6
#define MV88E6XXX_G1_STS_IRQ_VTU_PROBLEM		5
#define MV88E6XXX_G1_STS_IRQ_VTU_DONE			4
#define MV88E6XXX_G1_STS_IRQ_ATU_PROBLEM		3
#define MV88E6XXX_G1_STS_IRQ_ATU_DONE			2
#define MV88E6XXX_G1_STS_IRQ_TCAM_DONE			1
#define MV88E6XXX_G1_STS_IRQ_EEPROM_DONE		0

/* Offset 0x01: Switch MAC Address Register Bytes 0 & 1
 * Offset 0x02: Switch MAC Address Register Bytes 2 & 3
 * Offset 0x03: Switch MAC Address Register Bytes 4 & 5
 */
#define MV88E6XXX_G1_MAC_01		0x01
#define MV88E6XXX_G1_MAC_23		0x02
#define MV88E6XXX_G1_MAC_45		0x03

/* Offset 0x01: ATU FID Register */
#define MV88E6352_G1_ATU_FID		0x01

/* Offset 0x02: VTU FID Register */
#define MV88E6352_G1_VTU_FID		0x02
#define MV88E6352_G1_VTU_FID_MASK	0x0fff

/* Offset 0x03: VTU SID Register */
#define MV88E6352_G1_VTU_SID		0x03
#define MV88E6352_G1_VTU_SID_MASK	0x3f

#define GLOBAL_CONTROL		0x04
#define GLOBAL_CONTROL_SW_RESET		BIT(15)
#define GLOBAL_CONTROL_PPU_ENABLE	BIT(14)
#define GLOBAL_CONTROL_DISCARD_EXCESS	BIT(13) /* 6352 */
#define GLOBAL_CONTROL_SCHED_PRIO	BIT(11) /* 6152 */
#define GLOBAL_CONTROL_MAX_FRAME_1632	BIT(10) /* 6152 */
#define GLOBAL_CONTROL_RELOAD_EEPROM	BIT(9)	/* 6152 */
#define GLOBAL_CONTROL_DEVICE_EN	BIT(7)
#define GLOBAL_CONTROL_STATS_DONE_EN	BIT(6)
#define GLOBAL_CONTROL_VTU_PROBLEM_EN	BIT(5)
#define GLOBAL_CONTROL_VTU_DONE_EN	BIT(4)
#define GLOBAL_CONTROL_ATU_PROBLEM_EN	BIT(3)
#define GLOBAL_CONTROL_ATU_DONE_EN	BIT(2)
#define GLOBAL_CONTROL_TCAM_EN		BIT(1)
#define GLOBAL_CONTROL_EEPROM_DONE_EN	BIT(0)

/* Offset 0x05: VTU Operation Register */
#define MV88E6XXX_G1_VTU_OP			0x05
#define MV88E6XXX_G1_VTU_OP_BUSY		0x8000
#define MV88E6XXX_G1_VTU_OP_MASK		0x7000
#define MV88E6XXX_G1_VTU_OP_FLUSH_ALL		0x1000
#define MV88E6XXX_G1_VTU_OP_NOOP		0x2000
#define MV88E6XXX_G1_VTU_OP_VTU_LOAD_PURGE	0x3000
#define MV88E6XXX_G1_VTU_OP_VTU_GET_NEXT	0x4000
#define MV88E6XXX_G1_VTU_OP_STU_LOAD_PURGE	0x5000
#define MV88E6XXX_G1_VTU_OP_STU_GET_NEXT	0x6000

/* Offset 0x06: VTU VID Register */
#define MV88E6XXX_G1_VTU_VID		0x06
#define MV88E6XXX_G1_VTU_VID_MASK	0x0fff
#define MV88E6390_G1_VTU_VID_PAGE	0x2000
#define MV88E6XXX_G1_VTU_VID_VALID	0x1000

/* Offset 0x07: VTU/STU Data Register 1
 * Offset 0x08: VTU/STU Data Register 2
 * Offset 0x09: VTU/STU Data Register 3
 */
#define MV88E6XXX_G1_VTU_DATA1				0x07
#define MV88E6XXX_G1_VTU_DATA2				0x08
#define MV88E6XXX_G1_VTU_DATA3				0x09
#define MV88E6XXX_G1_VTU_STU_DATA_MASK			0x0003
#define MV88E6XXX_G1_VTU_DATA_MEMBER_TAG_UNMODIFIED	0x0000
#define MV88E6XXX_G1_VTU_DATA_MEMBER_TAG_UNTAGGED	0x0001
#define MV88E6XXX_G1_VTU_DATA_MEMBER_TAG_TAGGED		0x0002
#define MV88E6XXX_G1_VTU_DATA_MEMBER_TAG_NON_MEMBER	0x0003
#define MV88E6XXX_G1_STU_DATA_PORT_STATE_DISABLED	0x0000
#define MV88E6XXX_G1_STU_DATA_PORT_STATE_BLOCKING	0x0001
#define MV88E6XXX_G1_STU_DATA_PORT_STATE_LEARNING	0x0002
#define MV88E6XXX_G1_STU_DATA_PORT_STATE_FORWARDING	0x0003

/* Offset 0x0A: ATU Control Register */
#define MV88E6XXX_G1_ATU_CTL		0x0a
#define MV88E6XXX_G1_ATU_CTL_LEARN2ALL	0x0008

/* Offset 0x0B: ATU Operation Register */
#define MV88E6XXX_G1_ATU_OP				0x0b
#define MV88E6XXX_G1_ATU_OP_BUSY			0x8000
#define MV88E6XXX_G1_ATU_OP_MASK			0x7000
#define MV88E6XXX_G1_ATU_OP_NOOP			0x0000
#define MV88E6XXX_G1_ATU_OP_FLUSH_MOVE_ALL		0x1000
#define MV88E6XXX_G1_ATU_OP_FLUSH_MOVE_NON_STATIC	0x2000
#define MV88E6XXX_G1_ATU_OP_LOAD_DB			0x3000
#define MV88E6XXX_G1_ATU_OP_GET_NEXT_DB			0x4000
#define MV88E6XXX_G1_ATU_OP_FLUSH_MOVE_ALL_DB		0x5000
#define MV88E6XXX_G1_ATU_OP_FLUSH_MOVE_NON_STATIC_DB	0x6000
#define MV88E6XXX_G1_ATU_OP_GET_CLR_VIOLATION		0x7000

/* Offset 0x0C: ATU Data Register */
#define MV88E6XXX_G1_ATU_DATA				0x0c
#define MV88E6XXX_G1_ATU_DATA_TRUNK			0x8000
#define MV88E6XXX_G1_ATU_DATA_TRUNK_ID_MASK		0x00f0
#define MV88E6XXX_G1_ATU_DATA_PORT_VECTOR_MASK		0x3ff0
#define MV88E6XXX_G1_ATU_DATA_STATE_MASK		0x000f
#define MV88E6XXX_G1_ATU_DATA_STATE_UNUSED		0x0000
#define MV88E6XXX_G1_ATU_DATA_STATE_UC_MGMT		0x000d
#define MV88E6XXX_G1_ATU_DATA_STATE_UC_STATIC		0x000e
#define MV88E6XXX_G1_ATU_DATA_STATE_UC_PRIO_OVER	0x000f
#define MV88E6XXX_G1_ATU_DATA_STATE_MC_NONE_RATE	0x0005
#define MV88E6XXX_G1_ATU_DATA_STATE_MC_STATIC		0x0007
#define MV88E6XXX_G1_ATU_DATA_STATE_MC_MGMT		0x000e
#define MV88E6XXX_G1_ATU_DATA_STATE_MC_PRIO_OVER	0x000f

/* Offset 0x0D: ATU MAC Address Register Bytes 0 & 1
 * Offset 0x0E: ATU MAC Address Register Bytes 2 & 3
 * Offset 0x0F: ATU MAC Address Register Bytes 4 & 5
 */
#define MV88E6XXX_G1_ATU_MAC01		0x0d
#define MV88E6XXX_G1_ATU_MAC23		0x0e
#define MV88E6XXX_G1_ATU_MAC45		0x0f

#define GLOBAL_IP_PRI_0		0x10
#define GLOBAL_IP_PRI_1		0x11
#define GLOBAL_IP_PRI_2		0x12
#define GLOBAL_IP_PRI_3		0x13
#define GLOBAL_IP_PRI_4		0x14
#define GLOBAL_IP_PRI_5		0x15
#define GLOBAL_IP_PRI_6		0x16
#define GLOBAL_IP_PRI_7		0x17
#define GLOBAL_IEEE_PRI		0x18
#define GLOBAL_CORE_TAG_TYPE	0x19
#define GLOBAL_MONITOR_CONTROL	0x1a
#define GLOBAL_MONITOR_CONTROL_INGRESS_SHIFT	12
#define GLOBAL_MONITOR_CONTROL_INGRESS_MASK	(0xf << 12)
#define GLOBAL_MONITOR_CONTROL_EGRESS_SHIFT	8
#define GLOBAL_MONITOR_CONTROL_EGRESS_MASK	(0xf << 8)
#define GLOBAL_MONITOR_CONTROL_ARP_SHIFT	4
#define GLOBAL_MONITOR_CONTROL_ARP_MASK	        (0xf << 4)
#define GLOBAL_MONITOR_CONTROL_MIRROR_SHIFT	0
#define GLOBAL_MONITOR_CONTROL_ARP_DISABLED	(0xf0)
#define GLOBAL_MONITOR_CONTROL_UPDATE			BIT(15)
#define GLOBAL_MONITOR_CONTROL_0180C280000000XLO	(0x00 << 8)
#define GLOBAL_MONITOR_CONTROL_0180C280000000XHI	(0x01 << 8)
#define GLOBAL_MONITOR_CONTROL_0180C280000002XLO	(0x02 << 8)
#define GLOBAL_MONITOR_CONTROL_0180C280000002XHI	(0x03 << 8)
#define GLOBAL_MONITOR_CONTROL_INGRESS			(0x20 << 8)
#define GLOBAL_MONITOR_CONTROL_EGRESS			(0x21 << 8)
#define GLOBAL_MONITOR_CONTROL_CPU_DEST			(0x30 << 8)
#define GLOBAL_CONTROL_2	0x1c
#define GLOBAL_CONTROL_2_NO_CASCADE		0xe000
#define GLOBAL_CONTROL_2_MULTIPLE_CASCADE	0xf000
#define GLOBAL_CONTROL_2_HIST_RX	       (0x1 << 6)
#define GLOBAL_CONTROL_2_HIST_TX	       (0x2 << 6)
#define GLOBAL_CONTROL_2_HIST_RX_TX	       (0x3 << 6)
#define GLOBAL_STATS_OP		0x1d
#define GLOBAL_STATS_OP_BUSY	BIT(15)
#define GLOBAL_STATS_OP_NOP		(0 << 12)
#define GLOBAL_STATS_OP_FLUSH_ALL	((1 << 12) | GLOBAL_STATS_OP_BUSY)
#define GLOBAL_STATS_OP_FLUSH_PORT	((2 << 12) | GLOBAL_STATS_OP_BUSY)
#define GLOBAL_STATS_OP_READ_CAPTURED	((4 << 12) | GLOBAL_STATS_OP_BUSY)
#define GLOBAL_STATS_OP_CAPTURE_PORT	((5 << 12) | GLOBAL_STATS_OP_BUSY)
#define GLOBAL_STATS_OP_HIST_RX		((1 << 10) | GLOBAL_STATS_OP_BUSY)
#define GLOBAL_STATS_OP_HIST_TX		((2 << 10) | GLOBAL_STATS_OP_BUSY)
#define GLOBAL_STATS_OP_HIST_RX_TX	((3 << 10) | GLOBAL_STATS_OP_BUSY)
#define GLOBAL_STATS_OP_BANK_1_BIT_9	BIT(9)
#define GLOBAL_STATS_OP_BANK_1_BIT_10	BIT(10)
#define GLOBAL_STATS_COUNTER_32	0x1e
#define GLOBAL_STATS_COUNTER_01	0x1f

int mv88e6xxx_g1_read(struct mv88e6xxx_chip *chip, int reg, u16 *val);
int mv88e6xxx_g1_write(struct mv88e6xxx_chip *chip, int reg, u16 val);
int mv88e6xxx_g1_wait(struct mv88e6xxx_chip *chip, int reg, u16 mask);

int mv88e6xxx_g1_set_switch_mac(struct mv88e6xxx_chip *chip, u8 *addr);

int mv88e6185_g1_reset(struct mv88e6xxx_chip *chip);
int mv88e6352_g1_reset(struct mv88e6xxx_chip *chip);

int mv88e6185_g1_ppu_enable(struct mv88e6xxx_chip *chip);
int mv88e6185_g1_ppu_disable(struct mv88e6xxx_chip *chip);

int mv88e6xxx_g1_stats_wait(struct mv88e6xxx_chip *chip);
int mv88e6xxx_g1_stats_snapshot(struct mv88e6xxx_chip *chip, int port);
int mv88e6320_g1_stats_snapshot(struct mv88e6xxx_chip *chip, int port);
int mv88e6390_g1_stats_snapshot(struct mv88e6xxx_chip *chip, int port);
int mv88e6390_g1_stats_set_histogram(struct mv88e6xxx_chip *chip);
void mv88e6xxx_g1_stats_read(struct mv88e6xxx_chip *chip, int stat, u32 *val);
int mv88e6095_g1_set_egress_port(struct mv88e6xxx_chip *chip, int port);
int mv88e6390_g1_set_egress_port(struct mv88e6xxx_chip *chip, int port);
int mv88e6095_g1_set_cpu_port(struct mv88e6xxx_chip *chip, int port);
int mv88e6390_g1_set_cpu_port(struct mv88e6xxx_chip *chip, int port);
int mv88e6390_g1_mgmt_rsvd2cpu(struct mv88e6xxx_chip *chip);

int mv88e6xxx_g1_atu_set_learn2all(struct mv88e6xxx_chip *chip, bool learn2all);
int mv88e6xxx_g1_atu_set_age_time(struct mv88e6xxx_chip *chip,
				  unsigned int msecs);
int mv88e6xxx_g1_atu_getnext(struct mv88e6xxx_chip *chip, u16 fid,
			     struct mv88e6xxx_atu_entry *entry);
int mv88e6xxx_g1_atu_loadpurge(struct mv88e6xxx_chip *chip, u16 fid,
			       struct mv88e6xxx_atu_entry *entry);
int mv88e6xxx_g1_atu_flush(struct mv88e6xxx_chip *chip, u16 fid, bool all);
int mv88e6xxx_g1_atu_remove(struct mv88e6xxx_chip *chip, u16 fid, int port,
			    bool all);

int mv88e6185_g1_vtu_getnext(struct mv88e6xxx_chip *chip,
			     struct mv88e6xxx_vtu_entry *entry);
int mv88e6185_g1_vtu_loadpurge(struct mv88e6xxx_chip *chip,
			       struct mv88e6xxx_vtu_entry *entry);
int mv88e6352_g1_vtu_getnext(struct mv88e6xxx_chip *chip,
			     struct mv88e6xxx_vtu_entry *entry);
int mv88e6352_g1_vtu_loadpurge(struct mv88e6xxx_chip *chip,
			       struct mv88e6xxx_vtu_entry *entry);
int mv88e6390_g1_vtu_getnext(struct mv88e6xxx_chip *chip,
			     struct mv88e6xxx_vtu_entry *entry);
int mv88e6390_g1_vtu_loadpurge(struct mv88e6xxx_chip *chip,
			       struct mv88e6xxx_vtu_entry *entry);
int mv88e6xxx_g1_vtu_flush(struct mv88e6xxx_chip *chip);

#endif /* _MV88E6XXX_GLOBAL1_H */
