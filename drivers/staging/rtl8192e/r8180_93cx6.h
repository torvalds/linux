/* r8180_93cx6.h - 93c46 or 93c56 eeprom card programming routines
 *
 * This is part of rtl8187 OpenSource driver
 * Copyright (C) Andrea Merello 2004-2005  <andreamrl@tiscali.it>
 * Released under the terms of GPL (General Public Licence)
 * Parts of this driver are based on the GPL part of the official realtek driver
 *
 * Parts of this driver are based on the rtl8180 driver skeleton from
 * Patric Schenke & Andres Salomon.
 *
 * Parts of this driver are based on the Intel Pro Wireless 2100 GPL driver
 *
 * We want to thank the authors of the above mentioned projects and to
 * the authors of the Ndiswrapper project.
 */

#include "r8192E.h"
#include "r8192E_hw.h"

#define EPROM_DELAY 10

#define EPROM_ANAPARAM_ADDRLWORD 0xd
#define EPROM_ANAPARAM_ADDRHWORD 0xe

#define EPROM_RFCHIPID 0x6
#define EPROM_TXPW_BASE 0x05
#define EPROM_RFCHIPID_RTL8225U 5
#define EPROM_RF_PARAM 0x4
#define EPROM_CONFIG2 0xc

#define EPROM_VERSION 0x1E
#define MAC_ADR 0x7

#define CIS 0x18

#define EPROM_TXPW0 0x16
#define EPROM_TXPW2 0x1b
#define EPROM_TXPW1 0x3d

/* Reads a 16 bits word. */
u32 eprom_read(struct net_device *dev, u32 addr);
