/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#ifndef _R819XU_PHY_H
#define _R819XU_PHY_H

#define MAX_DOZE_WAITING_TIMES_9x 64

enum hw90_block {
	HW90_BLOCK_MAC = 0,
	HW90_BLOCK_PHY0 = 1,
	HW90_BLOCK_PHY1 = 2,
	HW90_BLOCK_RF = 3,
	HW90_BLOCK_MAXIMUM = 4,
};

enum rf90_radio_path {
	RF90_PATH_A = 0,
	RF90_PATH_B = 1,
	RF90_PATH_C = 2,
	RF90_PATH_D = 3,
	RF90_PATH_MAX
};

void rtl92e_set_bb_reg(struct net_device *dev, u32 dwRegAddr,
		       u32 dwBitMask, u32 dwData);
u32 rtl92e_get_bb_reg(struct net_device *dev, u32 dwRegAddr, u32 dwBitMask);
void rtl92e_set_rf_reg(struct net_device *dev, enum rf90_radio_path eRFPath,
		       u32 RegAddr, u32 BitMask, u32 Data);
u32 rtl92e_get_rf_reg(struct net_device *dev, enum rf90_radio_path eRFPath,
		      u32 RegAddr, u32 BitMask);
void rtl92e_config_mac(struct net_device *dev);
bool rtl92e_check_bb_and_rf(struct net_device *dev,
			    enum hw90_block CheckBlock,
			    enum rf90_radio_path eRFPath);
bool rtl92e_config_bb(struct net_device *dev);
void rtl92e_get_tx_power(struct net_device *dev);
void rtl92e_set_tx_power(struct net_device *dev, u8 channel);
u8 rtl92e_config_rf_path(struct net_device *dev, enum rf90_radio_path eRFPath);

u8 rtl92e_set_channel(struct net_device *dev, u8 channel);
void rtl92e_set_bw_mode(struct net_device *dev,
			enum ht_channel_width bandwidth,
			enum ht_extchnl_offset Offset);
void rtl92e_init_gain(struct net_device *dev, u8 Operation);

void rtl92e_set_rf_off(struct net_device *dev);

bool rtl92e_set_rf_power_state(struct net_device *dev,
			       enum rt_rf_power_state rf_power_state);

void rtl92e_scan_op_backup(struct net_device *dev, u8 Operation);

#endif
