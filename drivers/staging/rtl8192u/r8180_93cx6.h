/*
 *	This is part of rtl8187 OpenSource driver
 *	Copyright (C) Andrea Merello 2004-2005  <andrea.merello@gmail.com>
 *	Released under the terms of GPL (General Public Licence)
 *
 *	Parts of this driver are based on the GPL part of the
 *	official realtek driver
 *	Parts of this driver are based on the rtl8180 driver skeleton
 *	from Patric Schenke & Andres Salomon
 *	Parts of this driver are based on the Intel Pro Wireless 2100 GPL driver
 *
 *	We want to thank the Authors of such projects and the Ndiswrapper
 *	project Authors.
 */

/*This files contains card eeprom (93c46 or 93c56) programming routines*/
/*memory is addressed by WORDS*/

#include "r8192U.h"
#include "r8192U_hw.h"

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

int eprom_read(struct net_device *dev, u32 addr); /* reads a 16 bits word */
