/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Marvell 88E6xxx Switch PTP support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2017 National Instruments
 *      Erik Hons <erik.hons@ni.com>
 *      Brandon Streiff <brandon.streiff@ni.com>
 *      Dane Wagner <dane.wagner@ni.com>
 */

#ifndef _MV88E6XXX_PTP_H
#define _MV88E6XXX_PTP_H

#include "chip.h"

/* Offset 0x00: TAI Global Config */
#define MV88E6352_TAI_CFG			0x00
#define MV88E6352_TAI_CFG_CAP_OVERWRITE		0x8000
#define MV88E6352_TAI_CFG_CAP_CTR_START		0x4000
#define MV88E6352_TAI_CFG_EVREQ_FALLING		0x2000
#define MV88E6352_TAI_CFG_TRIG_ACTIVE_LO	0x1000
#define MV88E6352_TAI_CFG_IRL_ENABLE		0x0400
#define MV88E6352_TAI_CFG_TRIG_IRQ_EN		0x0200
#define MV88E6352_TAI_CFG_EVREQ_IRQ_EN		0x0100
#define MV88E6352_TAI_CFG_TRIG_LOCK		0x0080
#define MV88E6352_TAI_CFG_BLOCK_UPDATE		0x0008
#define MV88E6352_TAI_CFG_MULTI_PTP		0x0004
#define MV88E6352_TAI_CFG_TRIG_MODE_ONESHOT	0x0002
#define MV88E6352_TAI_CFG_TRIG_ENABLE		0x0001

/* Offset 0x01: Timestamp Clock Period (ps) */
#define MV88E6XXX_TAI_CLOCK_PERIOD		0x01

/* Offset 0x09: Event Status */
#define MV88E6352_TAI_EVENT_STATUS		0x09
#define MV88E6352_TAI_EVENT_STATUS_ERROR	0x0200
#define MV88E6352_TAI_EVENT_STATUS_VALID	0x0100
#define MV88E6352_TAI_EVENT_STATUS_CTR_MASK	0x00ff
/* Offset 0x0A/0x0B: Event Time Lo/Hi. Always read with Event Status. */

/* Offset 0x0E/0x0F: PTP Global Time */
#define MV88E6352_TAI_TIME_LO			0x0e
#define MV88E6352_TAI_TIME_HI			0x0f

/* 6165 Global Control Registers */
/* Offset 0x9/0xa: Global Time */
#define MV88E6165_PTP_GC_TIME_LO		0x09
#define MV88E6165_PTP_GC_TIME_HI		0x0A

/* 6165 Per Port Registers. The arrival and departure registers are a
 * common block consisting of status, two time registers and the sequence ID
 */
/* Offset 0: Arrival Time 0 Status */
#define MV88E6165_PORT_PTP_ARR0_STS	0x00

/* Offset 0x04: PTP Arrival 1 Status */
#define MV88E6165_PORT_PTP_ARR1_STS	0x04

/* Offset 0x08: PTP Departure Status */
#define MV88E6165_PORT_PTP_DEP_STS	0x08

/* Offset 0x0d: Port Status */
#define MV88E6164_PORT_STATUS		0x0d

#ifdef CONFIG_NET_DSA_MV88E6XXX_PTP

int mv88e6xxx_ptp_setup(struct mv88e6xxx_chip *chip);
void mv88e6xxx_ptp_free(struct mv88e6xxx_chip *chip);

#define ptp_to_chip(ptp) container_of(ptp, struct mv88e6xxx_chip,	\
				      ptp_clock_info)

extern const struct mv88e6xxx_ptp_ops mv88e6165_ptp_ops;
extern const struct mv88e6xxx_ptp_ops mv88e6352_ptp_ops;
extern const struct mv88e6xxx_ptp_ops mv88e6390_ptp_ops;

#else /* !CONFIG_NET_DSA_MV88E6XXX_PTP */

static inline int mv88e6xxx_ptp_setup(struct mv88e6xxx_chip *chip)
{
	return 0;
}

static inline void mv88e6xxx_ptp_free(struct mv88e6xxx_chip *chip)
{
}

static const struct mv88e6xxx_ptp_ops mv88e6165_ptp_ops = {};
static const struct mv88e6xxx_ptp_ops mv88e6352_ptp_ops = {};
static const struct mv88e6xxx_ptp_ops mv88e6390_ptp_ops = {};

#endif /* CONFIG_NET_DSA_MV88E6XXX_PTP */

#endif /* _MV88E6XXX_PTP_H */
