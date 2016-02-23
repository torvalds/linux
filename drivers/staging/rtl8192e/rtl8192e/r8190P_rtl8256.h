/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
******************************************************************************/

#ifndef RTL8225H
#define RTL8225H

#define RTL819X_TOTAL_RF_PATH 2
void rtl92e_set_bandwidth(struct net_device *dev,
			  enum ht_channel_width Bandwidth);
bool rtl92e_config_rf(struct net_device *dev);
void rtl92e_set_cck_tx_power(struct net_device *dev, u8	powerlevel);
void rtl92e_set_ofdm_tx_power(struct net_device *dev, u8 powerlevel);

#endif
