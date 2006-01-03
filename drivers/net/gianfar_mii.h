/* 
 * drivers/net/gianfar_mii.h
 *
 * Gianfar Ethernet Driver -- MII Management Bus Implementation
 * Driver for the MDIO bus controller in the Gianfar register space
 *
 * Author: Andy Fleming
 * Maintainer: Kumar Gala
 *
 * Copyright (c) 2002-2004 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#ifndef __GIANFAR_MII_H
#define __GIANFAR_MII_H

#define MIIMIND_BUSY            0x00000001
#define MIIMIND_NOTVALID        0x00000004

#define MII_READ_COMMAND       0x00000001

#define GFAR_SUPPORTED (SUPPORTED_10baseT_Half \
		| SUPPORTED_100baseT_Half \
		| SUPPORTED_100baseT_Full \
		| SUPPORTED_Autoneg \
		| SUPPORTED_MII)

struct gfar_mii {
	u32	miimcfg;	/* 0x.520 - MII Management Config Register */
	u32	miimcom;	/* 0x.524 - MII Management Command Register */
	u32	miimadd;	/* 0x.528 - MII Management Address Register */
	u32	miimcon;	/* 0x.52c - MII Management Control Register */
	u32	miimstat;	/* 0x.530 - MII Management Status Register */
	u32	miimind;	/* 0x.534 - MII Management Indicator Register */
};

int gfar_mdio_read(struct mii_bus *bus, int mii_id, int regnum);
int gfar_mdio_write(struct mii_bus *bus, int mii_id, int regnum, u16 value);
int __init gfar_mdio_init(void);
void __exit gfar_mdio_exit(void);
#endif /* GIANFAR_PHY_H */
