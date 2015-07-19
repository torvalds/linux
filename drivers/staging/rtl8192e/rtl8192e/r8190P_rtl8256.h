/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
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
extern void rtl92e_set_bandwidth(struct net_device *dev,
				 enum ht_channel_width Bandwidth);
extern bool rtl92e_config_rf(struct net_device *dev);
extern void rtl92e_set_cck_tx_power(struct net_device *dev, u8	powerlevel);
extern void rtl92e_set_ofdm_tx_power(struct net_device *dev, u8 powerlevel);

#endif
