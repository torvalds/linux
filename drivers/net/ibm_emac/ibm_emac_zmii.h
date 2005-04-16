/*
 * ocp_zmii.h
 *
 * Defines for the IBM ZMII bridge
 *
 *      Armin Kuster akuster@mvista.com
 *      Dec, 2001
 *
 * Copyright 2001 MontaVista Softare Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _IBM_EMAC_ZMII_H_
#define _IBM_EMAC_ZMII_H_

#include <linux/config.h>

/* ZMII bridge registers */
struct zmii_regs {
	u32 fer;		/* Function enable reg */
	u32 ssr;		/* Speed select reg */
	u32 smiirs;		/* SMII status reg */
};

#define ZMII_INPUTS	4

/* ZMII device */
struct ibm_ocp_zmii {
	struct zmii_regs *base;
	int mode[ZMII_INPUTS];
	int users;		/* number of EMACs using this ZMII bridge */
};

/* Fuctional Enable Reg */

#define ZMII_FER_MASK(x)	(0xf0000000 >> (4*x))

#define ZMII_MDI0	0x80000000
#define ZMII_SMII0	0x40000000
#define ZMII_RMII0	0x20000000
#define ZMII_MII0	0x10000000
#define ZMII_MDI1	0x08000000
#define ZMII_SMII1	0x04000000
#define ZMII_RMII1	0x02000000
#define ZMII_MII1	0x01000000
#define ZMII_MDI2	0x00800000
#define ZMII_SMII2	0x00400000
#define ZMII_RMII2	0x00200000
#define ZMII_MII2	0x00100000
#define ZMII_MDI3	0x00080000
#define ZMII_SMII3	0x00040000
#define ZMII_RMII3	0x00020000
#define ZMII_MII3	0x00010000

/* Speed Selection reg */

#define ZMII_SCI0	0x40000000
#define ZMII_FSS0	0x20000000
#define ZMII_SP0	0x10000000
#define ZMII_SCI1	0x04000000
#define ZMII_FSS1	0x02000000
#define ZMII_SP1	0x01000000
#define ZMII_SCI2	0x00400000
#define ZMII_FSS2	0x00200000
#define ZMII_SP2	0x00100000
#define ZMII_SCI3	0x00040000
#define ZMII_FSS3	0x00020000
#define ZMII_SP3	0x00010000

#define ZMII_MII0_100MB	ZMII_SP0
#define ZMII_MII0_10MB	~ZMII_SP0
#define ZMII_MII1_100MB	ZMII_SP1
#define ZMII_MII1_10MB	~ZMII_SP1
#define ZMII_MII2_100MB	ZMII_SP2
#define ZMII_MII2_10MB	~ZMII_SP2
#define ZMII_MII3_100MB	ZMII_SP3
#define ZMII_MII3_10MB	~ZMII_SP3

/* SMII Status reg */

#define ZMII_STS0 0xFF000000	/* EMAC0 smii status mask */
#define ZMII_STS1 0x00FF0000	/* EMAC1 smii status mask */

#define SMII	0
#define RMII	1
#define MII	2
#define MDI	3

#endif				/* _IBM_EMAC_ZMII_H_ */
