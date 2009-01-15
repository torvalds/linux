/*
	This is part of rtl8180 OpenSource driver
	Copyright (C) Andrea Merello 2004-2005  <andreamrl@tiscali.it>
	Released under the terms of GPL (General Public Licence)

	Parts of this driver are based on the GPL part of the official realtek driver
	Parts of this driver are based on the rtl8180 driver skeleton from Patric Schenke & Andres Salomon
	Parts of this driver are based on the Intel Pro Wireless 2100 GPL driver

	We want to tanks the Authors of such projects and the Ndiswrapper project Authors.
*/

#define MAXIM_ANTENNA 0xb3
#define MAXIM_ANAPARAM_PWR1_ON 0x8
#define MAXIM_ANAPARAM_PWR0_ON 0x0


void maxim_rf_init(struct net_device *dev);
void maxim_rf_set_chan(struct net_device *dev,short ch);

void maxim_rf_close(struct net_device *dev);
