/*
 * Defines for the IBM RGMII bridge
 *
 * Based on ocp_zmii.h/ibm_emac_zmii.h
 * Armin Kuster akuster@mvista.com
 *
 * Copyright 2004 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _IBM_EMAC_RGMII_H_
#define _IBM_EMAC_RGMII_H_

#include <linux/config.h>

/* RGMII bridge */
typedef struct rgmii_regs {
	u32 fer;		/* Function enable register */
	u32 ssr;		/* Speed select register */
} rgmii_t;

#define RGMII_INPUTS			4

/* RGMII device */
struct ibm_ocp_rgmii {
	struct rgmii_regs *base;
	int mode[RGMII_INPUTS];
	int users;		/* number of EMACs using this RGMII bridge */
};

/* Fuctional Enable Reg */
#define RGMII_FER_MASK(x)		(0x00000007 << (4*x))
#define RGMII_RTBI			0x00000004
#define RGMII_RGMII			0x00000005
#define RGMII_TBI  			0x00000006
#define RGMII_GMII 			0x00000007

/* Speed Selection reg */

#define RGMII_SP2_100	0x00000002
#define RGMII_SP2_1000	0x00000004
#define RGMII_SP3_100	0x00000200
#define RGMII_SP3_1000	0x00000400

#define RGMII_MII2_SPDMASK	 0x00000007
#define RGMII_MII3_SPDMASK	 0x00000700

#define RGMII_MII2_100MB	 RGMII_SP2_100 & ~RGMII_SP2_1000
#define RGMII_MII2_1000MB 	 RGMII_SP2_1000 & ~RGMII_SP2_100
#define RGMII_MII2_10MB		 ~(RGMII_SP2_100 | RGMII_SP2_1000)
#define RGMII_MII3_100MB	 RGMII_SP3_100 & ~RGMII_SP3_1000
#define RGMII_MII3_1000MB 	 RGMII_SP3_1000 & ~RGMII_SP3_100
#define RGMII_MII3_10MB		 ~(RGMII_SP3_100 | RGMII_SP3_1000)

#define RTBI		0
#define RGMII		1
#define TBI		2
#define GMII		3

#endif				/* _IBM_EMAC_RGMII_H_ */
