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
#define MV88E6XXX_G1_STS_IRQ_VTU_PROB			5
#define MV88E6XXX_G1_STS_IRQ_VTU_DONE			4
#define MV88E6XXX_G1_STS_IRQ_ATU_PROB			3
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

/* Offset 0x04: Switch Global Control Register */
#define MV88E6XXX_G1_CTL1			0x04
#define MV88E6XXX_G1_CTL1_SW_RESET		0x8000
#define MV88E6XXX_G1_CTL1_PPU_ENABLE		0x4000
#define MV88E6352_G1_CTL1_DISCARD_EXCESS	0x2000
#define MV88E6185_G1_CTL1_SCHED_PRIO		0x0800
#define MV88E6185_G1_CTL1_MAX_FRAME_1632	0x0400
#define MV88E6185_G1_CTL1_RELOAD_EEPROM		0x0200
#define MV88E6XXX_G1_CTL1_DEVICE_EN		0x0080
#define MV88E6XXX_G1_CTL1_STATS_DONE_EN		0x0040
#define MV88E6XXX_G1_CTL1_VTU_PROBLEM_EN	0x0020
#define MV88E6XXX_G1_CTL1_VTU_DONE_EN		0x0010
#define MV88E6XXX_G1_CTL1_ATU_PROBLEM_EN	0x0008
#define MV88E6XXX_G1_CTL1_ATU_DONE_EN		0x0004
#define MV88E6XXX_G1_CTL1_TCAM_EN		0x0002
#define MV88E6XXX_G1_CTL1_EEPROM_DONE_EN	0x0001

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
#define MV88E6XXX_G1_VTU_OP_GET_CLR_VIOLATION	0x7000
#define MV88E6XXX_G1_VTU_OP_MEMBER_VIOLATION	BIT(6)
#define MV88E6XXX_G1_VTU_OP_MISS_VIOLATION	BIT(5)
#define MV88E6XXX_G1_VTU_OP_SPID_MASK		0xf

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
#define MV88E6XXX_G1_ATU_OP_AGE_OUT_VIOLATION		BIT(7)
#define MV88E6XXX_G1_ATU_OP_MEMBER_VIOLATION		BIT(6)
#define MV88E6XXX_G1_ATU_OP_MISS_VIOLTATION		BIT(5)
#define MV88E6XXX_G1_ATU_OP_FULL_VIOLATION		BIT(4)

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

/* Offset 0x10: IP-PRI Mapping Register 0
 * Offset 0x11: IP-PRI Mapping Register 1
 * Offset 0x12: IP-PRI Mapping Register 2
 * Offset 0x13: IP-PRI Mapping Register 3
 * Offset 0x14: IP-PRI Mapping Register 4
 * Offset 0x15: IP-PRI Mapping Register 5
 * Offset 0x16: IP-PRI Mapping Register 6
 * Offset 0x17: IP-PRI Mapping Register 7
 */
#define MV88E6XXX_G1_IP_PRI_0	0x10
#define MV88E6XXX_G1_IP_PRI_1	0x11
#define MV88E6XXX_G1_IP_PRI_2	0x12
#define MV88E6XXX_G1_IP_PRI_3	0x13
#define MV88E6XXX_G1_IP_PRI_4	0x14
#define MV88E6XXX_G1_IP_PRI_5	0x15
#define MV88E6XXX_G1_IP_PRI_6	0x16
#define MV88E6XXX_G1_IP_PRI_7	0x17

/* Offset 0x18: IEEE-PRI Register */
#define MV88E6XXX_G1_IEEE_PRI	0x18

/* Offset 0x19: Core Tag Type */
#define MV88E6185_G1_CORE_TAG_TYPE	0x19

/* Offset 0x1A: Monitor Control */
#define MV88E6185_G1_MONITOR_CTL			0x1a
#define MV88E6185_G1_MONITOR_CTL_INGRESS_DEST_MASK	0xf000
#define MV88E6185_G1_MONITOR_CTL_EGRESS_DEST_MASK	0x0f00
#define MV88E6185_G1_MONITOR_CTL_ARP_DEST_MASK	        0x00f0
#define MV88E6352_G1_MONITOR_CTL_CPU_DEST_MASK	        0x00f0
#define MV88E6352_G1_MONITOR_CTL_MIRROR_DEST_MASK	0x000f

/* Offset 0x1A: Monitor & MGMT Control Register */
#define MV88E6390_G1_MONITOR_MGMT_CTL				0x1a
#define MV88E6390_G1_MONITOR_MGMT_CTL_UPDATE			0x8000
#define MV88E6390_G1_MONITOR_MGMT_CTL_PTR_MASK			0x3f00
#define MV88E6390_G1_MONITOR_MGMT_CTL_PTR_0180C280000000XLO	0x0000
#define MV88E6390_G1_MONITOR_MGMT_CTL_PTR_0180C280000000XHI	0x0100
#define MV88E6390_G1_MONITOR_MGMT_CTL_PTR_0180C280000002XLO	0x0200
#define MV88E6390_G1_MONITOR_MGMT_CTL_PTR_0180C280000002XHI	0x0300
#define MV88E6390_G1_MONITOR_MGMT_CTL_PTR_INGRESS_DEST		0x2000
#define MV88E6390_G1_MONITOR_MGMT_CTL_PTR_EGRESS_DEST		0x2100
#define MV88E6390_G1_MONITOR_MGMT_CTL_PTR_CPU_DEST		0x3000
#define MV88E6390_G1_MONITOR_MGMT_CTL_DATA_MASK			0x00ff

/* Offset 0x1C: Global Control 2 */
#define MV88E6XXX_G1_CTL2			0x1c
#define MV88E6XXX_G1_CTL2_HIST_RX		0x0040
#define MV88E6XXX_G1_CTL2_HIST_TX		0x0080
#define MV88E6XXX_G1_CTL2_HIST_RX_TX		0x00c0
#define MV88E6185_G1_CTL2_CASCADE_PORT_MASK	0xf000
#define MV88E6185_G1_CTL2_CASCADE_PORT_NONE	0xe000
#define MV88E6185_G1_CTL2_CASCADE_PORT_MULTI	0xf000
#define MV88E6XXX_G1_CTL2_DEVICE_NUMBER_MASK	0x001f

/* Offset 0x1D: Stats Operation Register */
#define MV88E6XXX_G1_STATS_OP			0x1d
#define MV88E6XXX_G1_STATS_OP_BUSY		0x8000
#define MV88E6XXX_G1_STATS_OP_NOP		0x0000
#define MV88E6XXX_G1_STATS_OP_FLUSH_ALL		0x1000
#define MV88E6XXX_G1_STATS_OP_FLUSH_PORT	0x2000
#define MV88E6XXX_G1_STATS_OP_READ_CAPTURED	0x4000
#define MV88E6XXX_G1_STATS_OP_CAPTURE_PORT	0x5000
#define MV88E6XXX_G1_STATS_OP_HIST_RX		0x0400
#define MV88E6XXX_G1_STATS_OP_HIST_TX		0x0800
#define MV88E6XXX_G1_STATS_OP_HIST_RX_TX	0x0c00
#define MV88E6XXX_G1_STATS_OP_BANK_1_BIT_9	0x0200
#define MV88E6XXX_G1_STATS_OP_BANK_1_BIT_10	0x0400

/* Offset 0x1E: Stats Counter Register Bytes 3 & 2
 * Offset 0x1F: Stats Counter Register Bytes 1 & 0
 */
#define MV88E6XXX_G1_STATS_COUNTER_32	0x1e
#define MV88E6XXX_G1_STATS_COUNTER_01	0x1f

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
int mv88e6095_g1_stats_set_histogram(struct mv88e6xxx_chip *chip);
int mv88e6390_g1_stats_set_histogram(struct mv88e6xxx_chip *chip);
void mv88e6xxx_g1_stats_read(struct mv88e6xxx_chip *chip, int stat, u32 *val);
int mv88e6xxx_g1_stats_clear(struct mv88e6xxx_chip *chip);
int mv88e6095_g1_set_egress_port(struct mv88e6xxx_chip *chip, int port);
int mv88e6390_g1_set_egress_port(struct mv88e6xxx_chip *chip, int port);
int mv88e6095_g1_set_cpu_port(struct mv88e6xxx_chip *chip, int port);
int mv88e6390_g1_set_cpu_port(struct mv88e6xxx_chip *chip, int port);
int mv88e6390_g1_mgmt_rsvd2cpu(struct mv88e6xxx_chip *chip);

int mv88e6185_g1_set_cascade_port(struct mv88e6xxx_chip *chip, int port);

int mv88e6xxx_g1_set_device_number(struct mv88e6xxx_chip *chip, int index);

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
int mv88e6xxx_g1_atu_prob_irq_setup(struct mv88e6xxx_chip *chip);
void mv88e6xxx_g1_atu_prob_irq_free(struct mv88e6xxx_chip *chip);

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
int mv88e6xxx_g1_vtu_prob_irq_setup(struct mv88e6xxx_chip *chip);
void mv88e6xxx_g1_vtu_prob_irq_free(struct mv88e6xxx_chip *chip);

#endif /* _MV88E6XXX_GLOBAL1_H */
