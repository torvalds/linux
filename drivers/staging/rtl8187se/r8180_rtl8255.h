/*
  This is part of the rtl8180-sa2400 driver
  released under the GPL (See file COPYING for details).
  Copyright (c) 2005 Andrea Merello <andreamrl@tiscali.it>

  This files contains programming code for the rtl8255
  radio frontend.

  *Many* thanks to Realtek Corp. for their great support!

*/

#define RTL8255_ANAPARAM_ON 0xa0000b59
#define RTL8255_ANAPARAM2_ON 0x840cf311


void rtl8255_rf_init(struct net_device *dev);
void rtl8255_rf_set_chan(struct net_device *dev,short ch);
void rtl8255_rf_close(struct net_device *dev);
