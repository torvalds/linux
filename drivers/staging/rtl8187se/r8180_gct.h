/*
	This is part of rtl8180 OpenSource driver - v 0.20
	Copyright (C) Andrea Merello 2004  <andreamrl@tiscali.it>
	Released under the terms of GPL (General Public Licence)

	Parts of this driver are based on the GPL part of the official realtek driver
	Parts of this driver are based on the rtl8180 driver skeleton from Patric Schenke & Andres Salomon
	Parts of this driver are based on the Intel Pro Wireless 2100 GPL driver

	We want to tanks the Authors of such projects and the Ndiswrapper project Authors.
*/

#define GCT_ANTENNA 0xA3


// we use the untouched eeprom value- cross your finger ;-)
#define GCT_ANAPARAM_PWR1_ON ??
#define GCT_ANAPARAM_PWR0_ON ??



void gct_rf_init(struct net_device *dev);
void gct_rf_set_chan(struct net_device *dev,short ch);

void gct_rf_close(struct net_device *dev);
