/*
 * drivers/net/ucc_geth_mii.h
 *
 * QE UCC Gigabit Ethernet Driver -- MII Management Bus Implementation
 * Provides Bus interface for MII Management regs in the UCC register space
 *
 * Copyright (C) 2007 Freescale Semiconductor, Inc.
 *
 * Authors: Li Yang <leoli@freescale.com>
 *	    Kim Phillips <kim.phillips@freescale.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#ifndef __UEC_MII_H
#define __UEC_MII_H

/* UCC GETH MIIMCFG (MII Management Configuration Register) */
#define MIIMCFG_RESET_MANAGEMENT                0x80000000	/* Reset
								   management */
#define MIIMCFG_NO_PREAMBLE                     0x00000010	/* Preamble
								   suppress */
#define MIIMCFG_CLOCK_DIVIDE_SHIFT              (31 - 31)	/* clock divide
								   << shift */
#define MIIMCFG_CLOCK_DIVIDE_MAX                0xf	/* max clock divide */
#define MIIMCFG_MANAGEMENT_CLOCK_DIVIDE_BY_2    0x00000000
#define MIIMCFG_MANAGEMENT_CLOCK_DIVIDE_BY_4    0x00000001
#define MIIMCFG_MANAGEMENT_CLOCK_DIVIDE_BY_6    0x00000002
#define MIIMCFG_MANAGEMENT_CLOCK_DIVIDE_BY_8    0x00000003
#define MIIMCFG_MANAGEMENT_CLOCK_DIVIDE_BY_10   0x00000004
#define MIIMCFG_MANAGEMENT_CLOCK_DIVIDE_BY_14   0x00000005
#define MIIMCFG_MANAGEMENT_CLOCK_DIVIDE_BY_16   0x00000008
#define MIIMCFG_MANAGEMENT_CLOCK_DIVIDE_BY_20   0x00000006
#define MIIMCFG_MANAGEMENT_CLOCK_DIVIDE_BY_28   0x00000007
#define MIIMCFG_MANAGEMENT_CLOCK_DIVIDE_BY_32   0x00000009
#define MIIMCFG_MANAGEMENT_CLOCK_DIVIDE_BY_48   0x0000000a
#define MIIMCFG_MANAGEMENT_CLOCK_DIVIDE_BY_64   0x0000000b
#define MIIMCFG_MANAGEMENT_CLOCK_DIVIDE_BY_80   0x0000000c
#define MIIMCFG_MANAGEMENT_CLOCK_DIVIDE_BY_112  0x0000000d
#define MIIMCFG_MANAGEMENT_CLOCK_DIVIDE_BY_160  0x0000000e
#define MIIMCFG_MANAGEMENT_CLOCK_DIVIDE_BY_224  0x0000000f

/* UCC GETH MIIMCOM (MII Management Command Register) */
#define MIIMCOM_SCAN_CYCLE                      0x00000002	/* Scan cycle */
#define MIIMCOM_READ_CYCLE                      0x00000001	/* Read cycle */

/* UCC GETH MIIMADD (MII Management Address Register) */
#define MIIMADD_PHY_ADDRESS_SHIFT               (31 - 23)	/* PHY Address
								   << shift */
#define MIIMADD_PHY_REGISTER_SHIFT              (31 - 31)	/* PHY Register
								   << shift */

/* UCC GETH MIIMCON (MII Management Control Register) */
#define MIIMCON_PHY_CONTROL_SHIFT               (31 - 31)	/* PHY Control
								   << shift */
#define MIIMCON_PHY_STATUS_SHIFT                (31 - 31)	/* PHY Status
								   << shift */

/* UCC GETH MIIMIND (MII Management Indicator Register) */
#define MIIMIND_NOT_VALID                       0x00000004	/* Not valid */
#define MIIMIND_SCAN                            0x00000002	/* Scan in
								   progress */
#define MIIMIND_BUSY                            0x00000001

/* Initial TBI Physical Address */
#define UTBIPAR_INIT_TBIPA			0x1f

struct ucc_mii_mng {
	u32 miimcfg;		/* MII management configuration reg */
	u32 miimcom;		/* MII management command reg */
	u32 miimadd;		/* MII management address reg */
	u32 miimcon;		/* MII management control reg */
	u32 miimstat;		/* MII management status reg */
	u32 miimind;		/* MII management indication reg */
	u8 notcare[28];		/* Space holder */
	u32 utbipar;		/* TBI phy address reg */
} __attribute__ ((packed));

/* TBI / MII Set Register */
enum enet_tbi_mii_reg {
	ENET_TBI_MII_CR = 0x00,	/* Control */
	ENET_TBI_MII_SR = 0x01,	/* Status */
	ENET_TBI_MII_ANA = 0x04,	/* AN advertisement */
	ENET_TBI_MII_ANLPBPA = 0x05,	/* AN link partner base page ability */
	ENET_TBI_MII_ANEX = 0x06,	/* AN expansion */
	ENET_TBI_MII_ANNPT = 0x07,	/* AN next page transmit */
	ENET_TBI_MII_ANLPANP = 0x08,	/* AN link partner ability next page */
	ENET_TBI_MII_EXST = 0x0F,	/* Extended status */
	ENET_TBI_MII_JD = 0x10,	/* Jitter diagnostics */
	ENET_TBI_MII_TBICON = 0x11	/* TBI control */
};

int uec_mdio_read(struct mii_bus *bus, int mii_id, int regnum);
int uec_mdio_write(struct mii_bus *bus, int mii_id, int regnum, u16 value);
int __init uec_mdio_init(void);
void uec_mdio_exit(void);
#endif				/* __UEC_MII_H */
