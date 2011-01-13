/******************************************************************************
 *
 * Copyright(c) 2009-2010  Realtek Corporation.
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
 *****************************************************************************/

#ifndef __RTL_BASE_H__
#define __RTL_BASE_H__

#define RTL_DUMMY_OFFSET	0
#define RTL_DUMMY_UNIT		8
#define RTL_TX_DUMMY_SIZE	(RTL_DUMMY_OFFSET * RTL_DUMMY_UNIT)
#define RTL_TX_DESC_SIZE	32
#define RTL_TX_HEADER_SIZE	(RTL_TX_DESC_SIZE + RTL_TX_DUMMY_SIZE)

#define HT_AMSDU_SIZE_4K	3839
#define HT_AMSDU_SIZE_8K	7935

#define MAX_BIT_RATE_40MHZ_MCS15	300	/* Mbps */
#define MAX_BIT_RATE_40MHZ_MCS7		150	/* Mbps */

#define RTL_RATE_COUNT_LEGACY		12
#define RTL_CHANNEL_COUNT		14

#define FRAME_OFFSET_FRAME_CONTROL	0
#define FRAME_OFFSET_DURATION		2
#define FRAME_OFFSET_ADDRESS1		4
#define FRAME_OFFSET_ADDRESS2		10
#define FRAME_OFFSET_ADDRESS3		16
#define FRAME_OFFSET_SEQUENCE		22
#define FRAME_OFFSET_ADDRESS4		24

#define SET_80211_HDR_FRAME_CONTROL(_hdr, _val)		\
	WRITEEF2BYTE(_hdr, _val)
#define SET_80211_HDR_TYPE_AND_SUBTYPE(_hdr, _val)	\
	WRITEEF1BYTE(_hdr, _val)
#define SET_80211_HDR_PWR_MGNT(_hdr, _val)		\
	SET_BITS_TO_LE_2BYTE(_hdr, 12, 1, _val)
#define SET_80211_HDR_TO_DS(_hdr, _val)			\
	SET_BITS_TO_LE_2BYTE(_hdr, 8, 1, _val)

#define SET_80211_PS_POLL_AID(_hdr, _val)		\
	WRITEEF2BYTE(((u8 *)(_hdr)) + 2, _val)
#define SET_80211_PS_POLL_BSSID(_hdr, _val)		\
	CP_MACADDR(((u8 *)(_hdr)) + 4, (u8 *)(_val))
#define SET_80211_PS_POLL_TA(_hdr, _val)		\
	CP_MACADDR(((u8 *)(_hdr)) + 10, (u8 *)(_val))

#define SET_80211_HDR_DURATION(_hdr, _val)	\
	WRITEEF2BYTE((u8 *)(_hdr)+FRAME_OFFSET_DURATION, _val)
#define SET_80211_HDR_ADDRESS1(_hdr, _val)	\
	CP_MACADDR((u8 *)(_hdr)+FRAME_OFFSET_ADDRESS1, (u8*)(_val))
#define SET_80211_HDR_ADDRESS2(_hdr, _val)	\
	CP_MACADDR((u8 *)(_hdr) + FRAME_OFFSET_ADDRESS2, (u8 *)(_val))
#define SET_80211_HDR_ADDRESS3(_hdr, _val)	\
	CP_MACADDR((u8 *)(_hdr)+FRAME_OFFSET_ADDRESS3, (u8 *)(_val))
#define SET_80211_HDR_FRAGMENT_SEQUENCE(_hdr, _val)  \
	WRITEEF2BYTE((u8 *)(_hdr)+FRAME_OFFSET_SEQUENCE, _val)

#define SET_BEACON_PROBE_RSP_TIME_STAMP_LOW(__phdr, __val)	\
	WRITEEF4BYTE(((u8 *)(__phdr)) + 24, __val)
#define SET_BEACON_PROBE_RSP_TIME_STAMP_HIGH(__phdr, __val) \
	WRITEEF4BYTE(((u8 *)(__phdr)) + 28, __val)
#define SET_BEACON_PROBE_RSP_BEACON_INTERVAL(__phdr, __val) \
	WRITEEF2BYTE(((u8 *)(__phdr)) + 32, __val)
#define GET_BEACON_PROBE_RSP_CAPABILITY_INFO(__phdr)	\
	READEF2BYTE(((u8 *)(__phdr)) + 34)
#define SET_BEACON_PROBE_RSP_CAPABILITY_INFO(__phdr, __val) \
	WRITEEF2BYTE(((u8 *)(__phdr)) + 34, __val)
#define MASK_BEACON_PROBE_RSP_CAPABILITY_INFO(__phdr, __val) \
	SET_BEACON_PROBE_RSP_CAPABILITY_INFO(__phdr, \
	(GET_BEACON_PROBE_RSP_CAPABILITY_INFO(__phdr) & (~(__val))))

int rtl_init_core(struct ieee80211_hw *hw);
void rtl_deinit_core(struct ieee80211_hw *hw);
void rtl_init_rx_config(struct ieee80211_hw *hw);
void rtl_init_rfkill(struct ieee80211_hw *hw);
void rtl_deinit_rfkill(struct ieee80211_hw *hw);

void rtl_watch_dog_timer_callback(unsigned long data);
void rtl_deinit_deferred_work(struct ieee80211_hw *hw);

bool rtl_action_proc(struct ieee80211_hw *hw, struct sk_buff *skb, u8 is_tx);
bool rtl_tx_mgmt_proc(struct ieee80211_hw *hw, struct sk_buff *skb);
u8 rtl_is_special_data(struct ieee80211_hw *hw, struct sk_buff *skb, u8 is_tx);

void rtl_watch_dog_timer_callback(unsigned long data);
int rtl_tx_agg_start(struct ieee80211_hw *hw, const u8 *ra,
		     u16 tid, u16 *ssn);
int rtl_tx_agg_stop(struct ieee80211_hw *hw, const u8 *ra, u16 tid);
void rtl_watchdog_wq_callback(void *data);

void rtl_get_tcb_desc(struct ieee80211_hw *hw,
		      struct ieee80211_tx_info *info,
		      struct sk_buff *skb, struct rtl_tcb_desc *tcb_desc);

extern struct attribute_group rtl_attribute_group;
#endif
