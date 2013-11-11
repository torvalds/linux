/*
 * Blackfin Performance Monitor definitions
 *
 * Copyright 2005-2011 Analog Devices Inc.
 *
 * Licensed under the Clear BSD license or GPL-2 (or later).
 */

#ifndef __ASM_BFIN_PFMON_H__
#define __ASM_BFIN_PFMON_H__

/* PFCTL Masks */
#define PFMON_MASK	0xff
#define PFCEN_MASK	0x3
#define PFCEN_DISABLE	0x0
#define PFCEN_ENABLE_USER	0x1
#define PFCEN_ENABLE_SUPV	0x2
#define PFCEN_ENABLE_ALL	(PFCEN_ENABLE_USER | PFCEN_ENABLE_SUPV)

#define PFPWR_P	0
#define PEMUSW0_P	2
#define PFCEN0_P	3
#define PFMON0_P	5
#define PEMUSW1_P	13
#define PFCEN1_P	14
#define PFMON1_P	16
#define PFCNT0_P	24
#define PFCNT1_P	25

#define PFPWR	(1 << PFPWR_P)
#define PEMUSW(n, x)	((x) << ((n) ? PEMUSW1_P : PEMUSW0_P))
#define PEMUSW0	PEMUSW(0, 1)
#define PEMUSW1	PEMUSW(1, 1)
#define PFCEN(n, x)	((x) << ((n) ? PFCEN1_P : PFCEN0_P))
#define PFCEN0	PFCEN(0, PFCEN_MASK)
#define PFCEN1	PFCEN(1, PFCEN_MASK)
#define PFCNT(n, x)	((x) << ((n) ? PFCNT1_P : PFCNT0_P))
#define PFCNT0	PFCNT(0, 1)
#define PFCNT1	PFCNT(1, 1)
#define PFMON(n, x)	((x) << ((n) ? PFMON1_P : PFMON0_P))
#define PFMON0	PFMON(0, PFMON_MASK)
#define PFMON1	PFMON(1, PFMON_MASK)

#endif
