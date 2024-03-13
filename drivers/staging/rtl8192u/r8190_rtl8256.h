/* SPDX-License-Identifier: GPL-2.0 */
/*
 *	 This is part of the rtl8180-sa2400 driver
 *	 released under the GPL (See file COPYING for details).
 *	 Copyright (c) 2005 Andrea Merello <andrea.merello@gmail.com>
 *
 *
 *	 This files contains programming code for the rtl8256
 *	 radio frontend.
 *
 *	 *Many* thanks to Realtek Corp. for their great support!
 */

#ifndef RTL8225H
#define RTL8225H

#define RTL819X_TOTAL_RF_PATH 2 /* for 8192U */
void phy_set_rf8256_bandwidth(struct net_device *dev,
			      enum ht_channel_width bandwidth);
void phy_rf8256_config(struct net_device *dev);
void phy_set_rf8256_cck_tx_power(struct net_device *dev, u8 powerlevel);
void phy_set_rf8256_ofdm_tx_power(struct net_device *dev, u8 powerlevel);

#endif
