/** @file moal_sta_cfg80211.h
  *
  * @brief This file contains the STA CFG80211 specific defines.
  *
  * Copyright (C) 2014-2017, Marvell International Ltd.
  *
  * This software file (the "File") is distributed by Marvell International
  * Ltd. under the terms of the GNU General Public License Version 2, June 1991
  * (the "License").  You may use, redistribute and/or modify this File in
  * accordance with the terms and conditions of the License, a copy of which
  * is available by writing to the Free Software Foundation, Inc.,
  * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
  * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
  *
  * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
  * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
  * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
  * this warranty disclaimer.
  *
  */

#ifndef _MOAL_STA_CFG80211_H_
#define _MOAL_STA_CFG80211_H_

/** Convert RSSI signal strength from dBm to mBm (100*dBm) */
#define RSSI_DBM_TO_MDM(x)          ((x) * 100)

mlan_status woal_register_sta_cfg80211(struct net_device *dev, t_u8 bss_type);

mlan_status

woal_cfg80211_set_key(moal_private *priv, t_u8 is_enable_wep,
		      t_u32 cipher, const t_u8 *key, int key_len,
		      const t_u8 *seq, int seq_len, t_u8 key_index,
		      const t_u8 *addr, int disable, t_u8 wait_option);

mlan_status

woal_cfg80211_set_wep_keys(moal_private *priv, const t_u8 *key, int key_len,
			   t_u8 index, t_u8 wait_option);

#endif /* _MOAL_STA_CFG80211_H_ */
