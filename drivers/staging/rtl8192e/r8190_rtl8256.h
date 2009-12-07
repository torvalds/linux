/*
  This is part of the rtl8180-sa2400 driver
  released under the GPL (See file COPYING for details).
  Copyright (c) 2005 Andrea Merello <andreamrl@tiscali.it>

  This files contains programming code for the rtl8256
  radio frontend.

  *Many* thanks to Realtek Corp. for their great support!

*/

#ifndef RTL8225H
#define RTL8225H

#ifdef RTL8190P
#define RTL819X_TOTAL_RF_PATH	4
#else
#define RTL819X_TOTAL_RF_PATH 2 //for 8192E
#endif
extern void PHY_SetRF8256Bandwidth(struct net_device* dev , HT_CHANNEL_WIDTH Bandwidth);
extern RT_STATUS PHY_RF8256_Config(struct net_device* dev);
extern RT_STATUS phy_RF8256_Config_ParaFile(struct net_device* dev);
extern void PHY_SetRF8256CCKTxPower(struct net_device*	dev, u8	powerlevel);
extern void PHY_SetRF8256OFDMTxPower(struct net_device* dev, u8 powerlevel);
extern bool MgntActSet_RF_State(struct net_device* dev, RT_RF_POWER_STATE StateToSet, RT_RF_CHANGE_SOURCE ChangeSource);

#endif
