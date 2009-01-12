/*
	This is part of rtl8180 OpenSource driver - v 0.7
	Copyright (C) Andrea Merello 2004  <andreamrl@tiscali.it>
	Released under the terms of GPL (General Public Licence)

	Parts of this driver are based on the GPL part of the official realtek driver
	Parts of this driver are based on the rtl8180 driver skeleton from Patric Schenke & Andres Salomon
	Parts of this driver are based on the Intel Pro Wireless 2100 GPL driver

	We want to tanks the Authors of such projects and the Ndiswrapper project Authors.
*/

#define SA2400_ANTENNA 0x91
#define SA2400_DIG_ANAPARAM_PWR1_ON 0x8
#define SA2400_ANA_ANAPARAM_PWR1_ON 0x28
#define SA2400_ANAPARAM_PWR0_ON 0x3

#define SA2400_RF_MAX_SENS 85
#define SA2400_RF_DEF_SENS 80

#define SA2400_REG4_FIRDAC_SHIFT 7

void sa2400_rf_init(struct net_device *dev);
void sa2400_rf_set_chan(struct net_device *dev,short ch);
short sa2400_rf_set_sens(struct net_device *dev,short sens);
void sa2400_rf_close(struct net_device *dev);
