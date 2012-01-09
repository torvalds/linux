/******************************************************************************
 *
 * Copyright(c) 2009-2010  Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
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
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __RTL92C_MAC_H__
#define __RTL92C_MAC_H__

#define LLT_LAST_ENTRY_OF_TX_PKT_BUFFER		255
#define DRIVER_EARLY_INT_TIME					0x05
#define BCN_DMA_ATIME_INT_TIME				0x02

void rtl92c_read_chip_version(struct ieee80211_hw *hw);
bool rtl92c_llt_write(struct ieee80211_hw *hw, u32 address, u32 data);
bool rtl92c_init_llt_table(struct ieee80211_hw *hw, u32 boundary);
void rtl92c_set_key(struct ieee80211_hw *hw, u32 key_index,
		     u8 *p_macaddr, bool is_group, u8 enc_algo,
		     bool is_wepkey, bool clear_all);
void rtl92c_enable_interrupt(struct ieee80211_hw *hw);
void rtl92c_disable_interrupt(struct ieee80211_hw *hw);
void rtl92c_set_qos(struct ieee80211_hw *hw, int aci);


/*---------------------------------------------------------------
 *	Hardware init functions
 *---------------------------------------------------------------*/
void rtl92c_set_mac_addr(struct ieee80211_hw *hw, const u8 *addr);
void rtl92c_init_interrupt(struct ieee80211_hw *hw);
void rtl92c_init_driver_info_size(struct ieee80211_hw *hw, u8 size);

int rtl92c_set_network_type(struct ieee80211_hw *hw, enum nl80211_iftype type);
void rtl92c_init_network_type(struct ieee80211_hw *hw);
void rtl92c_init_adaptive_ctrl(struct ieee80211_hw *hw);
void rtl92c_init_rate_fallback(struct ieee80211_hw *hw);

void rtl92c_init_edca_param(struct ieee80211_hw *hw,
							u16 queue,
							u16 txop,
							u8 ecwmax,
							u8 ecwmin,
							u8 aifs);

void rtl92c_init_edca(struct ieee80211_hw *hw);
void rtl92c_init_ampdu_aggregation(struct ieee80211_hw *hw);
void rtl92c_init_beacon_max_error(struct ieee80211_hw *hw, bool infra_mode);
void rtl92c_init_rdg_setting(struct ieee80211_hw *hw);
void rtl92c_init_retry_function(struct ieee80211_hw *hw);

void rtl92c_init_beacon_parameters(struct ieee80211_hw *hw,
				   enum version_8192c version);

void rtl92c_disable_fast_edca(struct ieee80211_hw *hw);
void rtl92c_set_min_space(struct ieee80211_hw *hw, bool is2T);

/* For filter */
u16 rtl92c_get_mgt_filter(struct ieee80211_hw *hw);
void rtl92c_set_mgt_filter(struct ieee80211_hw *hw, u16 filter);
u16 rtl92c_get_ctrl_filter(struct ieee80211_hw *hw);
void rtl92c_set_ctrl_filter(struct ieee80211_hw *hw, u16 filter);
u16 rtl92c_get_data_filter(struct ieee80211_hw *hw);
void rtl92c_set_data_filter(struct ieee80211_hw *hw, u16 filter);


u32 rtl92c_get_txdma_status(struct ieee80211_hw *hw);

struct rx_fwinfo_92c {
	u8 gain_trsw[4];
	u8 pwdb_all;
	u8 cfosho[4];
	u8 cfotail[4];
	char rxevm[2];
	char rxsnr[4];
	u8 pdsnr[2];
	u8 csi_current[2];
	u8 csi_target[2];
	u8 sigevm;
	u8 max_ex_pwr;
	u8 ex_intf_flag:1;
	u8 sgi_en:1;
	u8 rxsc:2;
	u8 reserve:4;
} __packed;

struct rx_desc_92c {
	u32 length:14;
	u32 crc32:1;
	u32 icverror:1;
	u32 drv_infosize:4;
	u32 security:3;
	u32 qos:1;
	u32 shift:2;
	u32 phystatus:1;
	u32 swdec:1;
	u32 lastseg:1;
	u32 firstseg:1;
	u32 eor:1;
	u32 own:1;
	u32 macid:5;	/* word 1 */
	u32 tid:4;
	u32 hwrsvd:5;
	u32 paggr:1;
	u32 faggr:1;
	u32 a1_fit:4;
	u32 a2_fit:4;
	u32 pam:1;
	u32 pwr:1;
	u32 moredata:1;
	u32 morefrag:1;
	u32 type:2;
	u32 mc:1;
	u32 bc:1;
	u32 seq:12;	/* word 2 */
	u32 frag:4;
	u32 nextpktlen:14;
	u32 nextind:1;
	u32 rsvd:1;
	u32 rxmcs:6;	/* word 3 */
	u32 rxht:1;
	u32 amsdu:1;
	u32 splcp:1;
	u32 bandwidth:1;
	u32 htc:1;
	u32 tcpchk_rpt:1;
	u32 ipcchk_rpt:1;
	u32 tcpchk_valid:1;
	u32 hwpcerr:1;
	u32 hwpcind:1;
	u32 iv0:16;
	u32 iv1;	/* word 4 */
	u32 tsfl;	/* word 5 */
	u32 bufferaddress;	/* word 6 */
	u32 bufferaddress64;	/* word 7 */
} __packed;

enum rtl_desc_qsel rtl92c_map_hwqueue_to_fwqueue(u16 fc,
							  unsigned int
							  skb_queue);
void rtl92c_translate_rx_signal_stuff(struct ieee80211_hw *hw,
				      struct sk_buff *skb,
				      struct rtl_stats *pstats,
				      struct rx_desc_92c *pdesc,
				      struct rx_fwinfo_92c *p_drvinfo);

/*---------------------------------------------------------------
 *	Card disable functions
 *---------------------------------------------------------------*/



#endif
