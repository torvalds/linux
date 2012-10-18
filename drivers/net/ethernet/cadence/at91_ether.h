/*
 * Ethernet driver for the Atmel AT91RM9200 (Thunder)
 *
 *  Copyright (C) SAN People (Pty) Ltd
 *
 * Based on an earlier Atmel EMAC macrocell driver by Atmel and Lineo Inc.
 * Initial version by Rick Bronson.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef AT91_ETHERNET
#define AT91_ETHERNET


/* Davicom 9161 PHY */
#define MII_DM9161_ID		0x0181b880
#define MII_DM9161A_ID		0x0181b8a0
#define MII_DSCR_REG		16
#define MII_DSCSR_REG		17
#define MII_DSINTR_REG		21

/* Intel LXT971A PHY */
#define MII_LXT971A_ID		0x001378E0
#define MII_ISINTE_REG		18
#define MII_ISINTS_REG		19
#define MII_LEDCTRL_REG		20

/* Realtek RTL8201 PHY */
#define MII_RTL8201_ID		0x00008200

/* Broadcom BCM5221 PHY */
#define MII_BCM5221_ID		0x004061e0
#define MII_BCMINTR_REG		26

/* National Semiconductor DP83847 */
#define MII_DP83847_ID		0x20005c30

/* National Semiconductor DP83848 */
#define MII_DP83848_ID		0x20005c90
#define MII_DPPHYSTS_REG	16
#define MII_DPMICR_REG		17
#define MII_DPMISR_REG		18

/* Altima AC101L PHY */
#define MII_AC101L_ID		0x00225520

/* Micrel KS8721 PHY */
#define MII_KS8721_ID		0x00221610

/* Teridian 78Q2123/78Q2133 */
#define MII_T78Q21x3_ID		0x000e7230
#define MII_T78Q21INT_REG	17

/* SMSC LAN83C185 */
#define MII_LAN83C185_ID	0x0007C0A0

#endif
