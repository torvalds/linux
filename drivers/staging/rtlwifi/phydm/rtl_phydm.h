/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef __RTL_PHYDM_H__
#define __RTL_PHYDM_H__

struct rtl_phydm_ops *rtl_phydm_get_ops_pointer(void);

#define rtlpriv_to_phydm(priv)                                                 \
	((struct phy_dm_struct *)((priv)->phydm.internal))

u8 phy_get_tx_power_index(void *adapter, u8 rf_path, u8 rate,
			  enum ht_channel_width bandwidth, u8 channel);
void phy_set_tx_power_index_by_rs(void *adapter, u8 ch, u8 path, u8 rs);
void phy_store_tx_power_by_rate(void *adapter, u32 band, u32 rfpath, u32 txnum,
				u32 regaddr, u32 bitmask, u32 data);
void phy_set_tx_power_limit(void *dm, u8 *regulation, u8 *band, u8 *bandwidth,
			    u8 *rate_section, u8 *rf_path, u8 *channel,
			    u8 *power_limit);

void rtl_hal_update_ra_mask(void *adapter, struct rtl_sta_info *psta,
			    u8 rssi_level);

#endif
