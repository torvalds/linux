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
#define MV88E6XXX_TAI_CFG			0x00
#define MV88E6XXX_TAI_CFG_CAP_OVERWRITE		0x8000
#define MV88E6XXX_TAI_CFG_CAP_CTR_START		0x4000
#define MV88E6XXX_TAI_CFG_EVREQ_FALLING		0x2000
#define MV88E6XXX_TAI_CFG_TRIG_ACTIVE_LO	0x1000
#define MV88E6XXX_TAI_CFG_IRL_ENABLE		0x0400
#define MV88E6XXX_TAI_CFG_TRIG_IRQ_EN		0x0200
#define MV88E6XXX_TAI_CFG_EVREQ_IRQ_EN		0x0100
#define MV88E6XXX_TAI_CFG_TRIG_LOCK		0x0080
#define MV88E6XXX_TAI_CFG_BLOCK_UPDATE		0x0008
#define MV88E6XXX_TAI_CFG_MULTI_PTP		0x0004
#define MV88E6XXX_TAI_CFG_TRIG_MODE_ONESHOT	0x0002
#define MV88E6XXX_TAI_CFG_TRIG_ENABLE		0x0001

/* Offset 0x01: Timestamp Clock Period (ps) */
#define MV88E6XXX_TAI_CLOCK_PERIOD		0x01

/* Offset 0x02/0x03: Trigger Generation Amount */
#define MV88E6XXX_TAI_TRIG_GEN_AMOUNT_LO	0x02
#define MV88E6XXX_TAI_TRIG_GEN_AMOUNT_HI	0x03

/* Offset 0x04: Clock Compensation */
#define MV88E6XXX_TAI_TRIG_CLOCK_COMP		0x04

/* Offset 0x05: Trigger Configuration */
#define MV88E6XXX_TAI_TRIG_CFG			0x05

/* Offset 0x06: Ingress Rate Limiter Clock Generation Amount */
#define MV88E6XXX_TAI_IRL_AMOUNT		0x06

/* Offset 0x07: Ingress Rate Limiter Compensation */
#define MV88E6XXX_TAI_IRL_COMP			0x07

/* Offset 0x08: Ingress Rate Limiter Compensation */
#define MV88E6XXX_TAI_IRL_COMP_PS		0x08

/* Offset 0x09: Event Status */
#define MV88E6XXX_TAI_EVENT_STATUS		0x09
#define MV88E6XXX_TAI_EVENT_STATUS_CAP_TRIG	0x4000
#define MV88E6XXX_TAI_EVENT_STATUS_ERROR	0x0200
#define MV88E6XXX_TAI_EVENT_STATUS_VALID	0x0100
#define MV88E6XXX_TAI_EVENT_STATUS_CTR_MASK	0x00ff

/* Offset 0x0A/0x0B: Event Time */
#define MV88E6XXX_TAI_EVENT_TIME_LO		0x0a
#define MV88E6XXX_TAI_EVENT_TYPE_HI		0x0b

/* Offset 0x0E/0x0F: PTP Global Time */
#define MV88E6XXX_TAI_TIME_LO			0x0e
#define MV88E6XXX_TAI_TIME_HI			0x0f

/* Offset 0x10/0x11: Trig Generation Time */
#define MV88E6XXX_TAI_TRIG_TIME_LO		0x10
#define MV88E6XXX_TAI_TRIG_TIME_HI		0x11

/* Offset 0x12: Lock Status */
#define MV88E6XXX_TAI_LOCK_STATUS		0x12

/* Offset 0x00: Ether Type */
#define MV88E6XXX_PTP_GC_ETYPE			0x00

/* 6165 Global Control Registers */
/* Offset 0x00: Ether Type */
#define MV88E6XXX_PTP_GC_ETYPE			0x00

/* Offset 0x01: Message ID */
#define MV88E6XXX_PTP_GC_MESSAGE_ID		0x01

/* Offset 0x02: Time Stamp Arrive Time */
#define MV88E6XXX_PTP_GC_TS_ARR_PTR		0x02

/* Offset 0x03: Port Arrival Interrupt Enable */
#define MV88E6XXX_PTP_GC_PORT_ARR_INT_EN	0x03

/* Offset 0x04: Port Departure Interrupt Enable */
#define MV88E6XXX_PTP_GC_PORT_DEP_INT_EN	0x04

/* Offset 0x05: Configuration */
#define MV88E6XXX_PTP_GC_CONFIG			0x05
#define MV88E6XXX_PTP_GC_CONFIG_DIS_OVERWRITE	BIT(1)
#define MV88E6XXX_PTP_GC_CONFIG_DIS_TS		BIT(0)

/* Offset 0x8: Interrupt Status */
#define MV88E6XXX_PTP_GC_INT_STATUS		0x08

/* Offset 0x9/0xa: Global Time */
#define MV88E6XXX_PTP_GC_TIME_LO		0x09
#define MV88E6XXX_PTP_GC_TIME_HI		0x0A

/* 6165 Per Port Registers */
/* Offset 0: Arrival Time 0 Status */
#define MV88E6165_PORT_PTP_ARR0_STS	0x00

/* Offset 0x01/0x02: PTP Arrival 0 Time */
#define MV88E6165_PORT_PTP_ARR0_TIME_LO	0x01
#define MV88E6165_PORT_PTP_ARR0_TIME_HI	0x02

/* Offset 0x03: PTP Arrival 0 Sequence ID */
#define MV88E6165_PORT_PTP_ARR0_SEQID	0x03

/* Offset 0x04: PTP Arrival 1 Status */
#define MV88E6165_PORT_PTP_ARR1_STS	0x04

/* Offset 0x05/0x6E: PTP Arrival 1 Time */
#define MV88E6165_PORT_PTP_ARR1_TIME_LO	0x05
#define MV88E6165_PORT_PTP_ARR1_TIME_HI	0x06

/* Offset 0x07: PTP Arrival 1 Sequence ID */
#define MV88E6165_PORT_PTP_ARR1_SEQID	0x07

/* Offset 0x08: PTP Departure Status */
#define MV88E6165_PORT_PTP_DEP_STS	0x08

/* Offset 0x09/0x0a: PTP Deperture Time */
#define MV88E6165_PORT_PTP_DEP_TIME_LO	0x09
#define MV88E6165_PORT_PTP_DEP_TIME_HI	0x0a

/* Offset 0x0b: PTP Departure Sequence ID */
#define MV88E6165_PORT_PTP_DEP_SEQID	0x0b

/* Offset 0x0d: Port Status */
#define MV88E6164_PORT_STATUS		0x0d

#ifdef CONFIG_NET_DSA_MV88E6XXX_PTP

long mv88e6xxx_hwtstamp_work(struct ptp_clock_info *ptp);
int mv88e6xxx_ptp_setup(struct mv88e6xxx_chip *chip);
void mv88e6xxx_ptp_free(struct mv88e6xxx_chip *chip);

#define ptp_to_chip(ptp) container_of(ptp, struct mv88e6xxx_chip,	\
				      ptp_clock_info)

extern const struct mv88e6xxx_ptp_ops mv88e6165_ptp_ops;
extern const struct mv88e6xxx_ptp_ops mv88e6250_ptp_ops;
extern const struct mv88e6xxx_ptp_ops mv88e6352_ptp_ops;

#else /* !CONFIG_NET_DSA_MV88E6XXX_PTP */

static inline long mv88e6xxx_hwtstamp_work(struct ptp_clock_info *ptp)
{
	return -1;
}

static inline int mv88e6xxx_ptp_setup(struct mv88e6xxx_chip *chip)
{
	return 0;
}

static inline void mv88e6xxx_ptp_free(struct mv88e6xxx_chip *chip)
{
}

static const struct mv88e6xxx_ptp_ops mv88e6165_ptp_ops = {};
static const struct mv88e6xxx_ptp_ops mv88e6250_ptp_ops = {};
static const struct mv88e6xxx_ptp_ops mv88e6352_ptp_ops = {};

#endif /* CONFIG_NET_DSA_MV88E6XXX_PTP */

#endif /* _MV88E6XXX_PTP_H */
