/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Marvell 88E6xxx Switch hardware timestamping support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2017 National Instruments
 *      Erik Hons <erik.hons@ni.com>
 *      Brandon Streiff <brandon.streiff@ni.com>
 *      Dane Wagner <dane.wagner@ni.com>
 */

#ifndef _MV88E6XXX_HWTSTAMP_H
#define _MV88E6XXX_HWTSTAMP_H

#include "chip.h"

/* Global 6352 PTP registers */
/* Offset 0x00: PTP EtherType */
#define MV88E6XXX_PTP_ETHERTYPE	0x00

/* Offset 0x01: Message Type Timestamp Enables */
#define MV88E6XXX_PTP_MSGTYPE			0x01
#define MV88E6XXX_PTP_MSGTYPE_SYNC		0x0001
#define MV88E6XXX_PTP_MSGTYPE_DELAY_REQ		0x0002
#define MV88E6XXX_PTP_MSGTYPE_PDLAY_REQ		0x0004
#define MV88E6XXX_PTP_MSGTYPE_PDLAY_RES		0x0008
#define MV88E6XXX_PTP_MSGTYPE_ALL_EVENT		0x000f

/* Offset 0x02: Timestamp Arrival Capture Pointers */
#define MV88E6XXX_PTP_TS_ARRIVAL_PTR	0x02

/* Offset 0x05: PTP Global Configuration */
#define MV88E6165_PTP_CFG			0x05
#define MV88E6165_PTP_CFG_TSPEC_MASK		0xf000
#define MV88E6165_PTP_CFG_DISABLE_TS_OVERWRITE	BIT(1)
#define MV88E6165_PTP_CFG_DISABLE_PTP		BIT(0)

/* Offset 0x07: PTP Global Configuration */
#define MV88E6341_PTP_CFG			0x07
#define MV88E6341_PTP_CFG_UPDATE		0x8000
#define MV88E6341_PTP_CFG_IDX_MASK		0x7f00
#define MV88E6341_PTP_CFG_DATA_MASK		0x00ff
#define MV88E6341_PTP_CFG_MODE_IDX		0x0
#define MV88E6341_PTP_CFG_MODE_TS_AT_PHY	0x00
#define MV88E6341_PTP_CFG_MODE_TS_AT_MAC	0x80

/* Offset 0x08: PTP Interrupt Status */
#define MV88E6XXX_PTP_IRQ_STATUS	0x08

/* Per-Port 6352 PTP Registers */
/* Offset 0x00: PTP Configuration 0 */
#define MV88E6XXX_PORT_PTP_CFG0				0x00
#define MV88E6XXX_PORT_PTP_CFG0_TSPEC_SHIFT		12
#define MV88E6XXX_PORT_PTP_CFG0_TSPEC_MASK		0xf000
#define MV88E6XXX_PORT_PTP_CFG0_TSPEC_1588		0x0000
#define MV88E6XXX_PORT_PTP_CFG0_TSPEC_8021AS		0x1000
#define MV88E6XXX_PORT_PTP_CFG0_DISABLE_TSPEC_MATCH	0x0800
#define MV88E6XXX_PORT_PTP_CFG0_DISABLE_OVERWRITE	0x0002
#define MV88E6XXX_PORT_PTP_CFG0_DISABLE_PTP		0x0001

/* Offset 0x01: PTP Configuration 1 */
#define MV88E6XXX_PORT_PTP_CFG1	0x01

/* Offset 0x02: PTP Configuration 2 */
#define MV88E6XXX_PORT_PTP_CFG2				0x02
#define MV88E6XXX_PORT_PTP_CFG2_EMBED_ARRIVAL		0x1000
#define MV88E6XXX_PORT_PTP_CFG2_DEP_IRQ_EN		0x0002
#define MV88E6XXX_PORT_PTP_CFG2_ARR_IRQ_EN		0x0001

/* Offset 0x03: PTP LED Configuration */
#define MV88E6XXX_PORT_PTP_LED_CFG	0x03

/* Offset 0x08: PTP Arrival 0 Status */
#define MV88E6XXX_PORT_PTP_ARR0_STS	0x08

/* Offset 0x09/0x0A: PTP Arrival 0 Time */
#define MV88E6XXX_PORT_PTP_ARR0_TIME_LO	0x09
#define MV88E6XXX_PORT_PTP_ARR0_TIME_HI	0x0a

/* Offset 0x0B: PTP Arrival 0 Sequence ID */
#define MV88E6XXX_PORT_PTP_ARR0_SEQID	0x0b

/* Offset 0x0C: PTP Arrival 1 Status */
#define MV88E6XXX_PORT_PTP_ARR1_STS	0x0c

/* Offset 0x0D/0x0E: PTP Arrival 1 Time */
#define MV88E6XXX_PORT_PTP_ARR1_TIME_LO	0x0d
#define MV88E6XXX_PORT_PTP_ARR1_TIME_HI	0x0e

/* Offset 0x0F: PTP Arrival 1 Sequence ID */
#define MV88E6XXX_PORT_PTP_ARR1_SEQID	0x0f

/* Offset 0x10: PTP Departure Status */
#define MV88E6XXX_PORT_PTP_DEP_STS	0x10

/* Offset 0x11/0x12: PTP Deperture Time */
#define MV88E6XXX_PORT_PTP_DEP_TIME_LO	0x11
#define MV88E6XXX_PORT_PTP_DEP_TIME_HI	0x12

/* Offset 0x13: PTP Departure Sequence ID */
#define MV88E6XXX_PORT_PTP_DEP_SEQID	0x13

/* Status fields for arrival and depature timestamp status registers */
#define MV88E6XXX_PTP_TS_STATUS_MASK		0x0006
#define MV88E6XXX_PTP_TS_STATUS_NORMAL		0x0000
#define MV88E6XXX_PTP_TS_STATUS_OVERWITTEN	0x0002
#define MV88E6XXX_PTP_TS_STATUS_DISCARDED	0x0004
#define MV88E6XXX_PTP_TS_VALID			0x0001

#ifdef CONFIG_NET_DSA_MV88E6XXX_PTP

int mv88e6xxx_port_hwtstamp_set(struct dsa_switch *ds, int port,
				struct ifreq *ifr);
int mv88e6xxx_port_hwtstamp_get(struct dsa_switch *ds, int port,
				struct ifreq *ifr);

bool mv88e6xxx_port_rxtstamp(struct dsa_switch *ds, int port,
			     struct sk_buff *clone, unsigned int type);
bool mv88e6xxx_port_txtstamp(struct dsa_switch *ds, int port,
			     struct sk_buff *clone, unsigned int type);

int mv88e6xxx_get_ts_info(struct dsa_switch *ds, int port,
			  struct ethtool_ts_info *info);

int mv88e6xxx_hwtstamp_setup(struct mv88e6xxx_chip *chip);
void mv88e6xxx_hwtstamp_free(struct mv88e6xxx_chip *chip);
int mv88e6352_hwtstamp_port_enable(struct mv88e6xxx_chip *chip, int port);
int mv88e6352_hwtstamp_port_disable(struct mv88e6xxx_chip *chip, int port);
int mv88e6165_global_enable(struct mv88e6xxx_chip *chip);
int mv88e6165_global_disable(struct mv88e6xxx_chip *chip);

#else /* !CONFIG_NET_DSA_MV88E6XXX_PTP */

static inline int mv88e6xxx_port_hwtstamp_set(struct dsa_switch *ds,
					      int port, struct ifreq *ifr)
{
	return -EOPNOTSUPP;
}

static inline int mv88e6xxx_port_hwtstamp_get(struct dsa_switch *ds,
					      int port, struct ifreq *ifr)
{
	return -EOPNOTSUPP;
}

static inline bool mv88e6xxx_port_rxtstamp(struct dsa_switch *ds, int port,
					   struct sk_buff *clone,
					   unsigned int type)
{
	return false;
}

static inline bool mv88e6xxx_port_txtstamp(struct dsa_switch *ds, int port,
					   struct sk_buff *clone,
					   unsigned int type)
{
	return false;
}

static inline int mv88e6xxx_get_ts_info(struct dsa_switch *ds, int port,
					struct ethtool_ts_info *info)
{
	return -EOPNOTSUPP;
}

static inline int mv88e6xxx_hwtstamp_setup(struct mv88e6xxx_chip *chip)
{
	return 0;
}

static inline void mv88e6xxx_hwtstamp_free(struct mv88e6xxx_chip *chip)
{
}

#endif /* CONFIG_NET_DSA_MV88E6XXX_PTP */

#endif /* _MV88E6XXX_HWTSTAMP_H */
