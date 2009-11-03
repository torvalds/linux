/*
	This is part of rtl8180 OpenSource driver
	Copyright (C) Andrea Merello 2004-2005  <andreamrl@tiscali.it>
	Released under the terms of GPL (General Public Licence)

	Parts of this driver are based on the GPL part of the official realtek driver
	Parts of this driver are based on the rtl8180 driver skeleton from Patric Schenke & Andres Salomon
	Parts of this driver are based on the Intel Pro Wireless 2100 GPL driver

	We want to tanks the Authors of such projects and the Ndiswrapper project Authors.
*/

/*This files contains card eeprom (93c46 or 93c56) programming routines*/
/*memory is addressed by WORDS*/

#include "r8180.h"
#include "r8180_hw.h"

#define EPROM_DELAY 10

#define EPROM_ANAPARAM_ADDRLWORD 0xd
#define EPROM_ANAPARAM_ADDRHWORD 0xe

#define RFCHIPID 0x6
#define	RFCHIPID_INTERSIL 1
#define	RFCHIPID_RFMD 2
#define	RFCHIPID_PHILIPS 3
#define	RFCHIPID_MAXIM 4
#define	RFCHIPID_GCT 5
#define RFCHIPID_RTL8225 9
#define RF_ZEBRA2 11
#define EPROM_TXPW_BASE 0x05
#define RF_ZEBRA4 12
#define RFCHIPID_RTL8255 0xa
#define RF_PARAM 0x19
#define RF_PARAM_DIGPHY_SHIFT 0
#define RF_PARAM_ANTBDEFAULT_SHIFT 1
#define RF_PARAM_CARRIERSENSE_SHIFT 2
#define RF_PARAM_CARRIERSENSE_MASK (3<<2)
#define ENERGY_TRESHOLD 0x17
#define EPROM_VERSION 0x1E
#define MAC_ADR 0x7

#define CIS 0x18

#define	EPROM_TXPW_OFDM_CH1_2 0x20

//#define	EPROM_TXPW_CH1_2 0x10
#define  EPROM_TXPW_CH1_2 0x30
#define	EPROM_TXPW_CH3_4 0x11
#define	EPROM_TXPW_CH5_6 0x12
#define	EPROM_TXPW_CH7_8 0x13
#define	EPROM_TXPW_CH9_10 0x14
#define	EPROM_TXPW_CH11_12 0x15
#define	EPROM_TXPW_CH13_14 0x16

u32 eprom_read(struct net_device *dev,u32 addr); //reads a 16 bits word
