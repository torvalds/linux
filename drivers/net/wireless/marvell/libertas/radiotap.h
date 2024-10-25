/* SPDX-License-Identifier: GPL-2.0 */
#include <net/ieee80211_radiotap.h>

struct tx_radiotap_hdr {
	struct ieee80211_radiotap_header_fixed hdr;
	u8 rate;
	u8 txpower;
	u8 rts_retries;
	u8 data_retries;
} __packed;

#define TX_RADIOTAP_PRESENT (				\
	(1 << IEEE80211_RADIOTAP_RATE) |		\
	(1 << IEEE80211_RADIOTAP_DBM_TX_POWER) |	\
	(1 << IEEE80211_RADIOTAP_RTS_RETRIES) |		\
	(1 << IEEE80211_RADIOTAP_DATA_RETRIES)  |	\
	0)

#define IEEE80211_FC_VERSION_MASK    0x0003
#define IEEE80211_FC_TYPE_MASK       0x000c
#define IEEE80211_FC_TYPE_MGT        0x0000
#define IEEE80211_FC_TYPE_CTL        0x0004
#define IEEE80211_FC_TYPE_DATA       0x0008
#define IEEE80211_FC_SUBTYPE_MASK    0x00f0
#define IEEE80211_FC_TOFROMDS_MASK   0x0300
#define IEEE80211_FC_TODS_MASK       0x0100
#define IEEE80211_FC_FROMDS_MASK     0x0200
#define IEEE80211_FC_NODS            0x0000
#define IEEE80211_FC_TODS            0x0100
#define IEEE80211_FC_FROMDS          0x0200
#define IEEE80211_FC_DSTODS          0x0300

struct rx_radiotap_hdr {
	struct ieee80211_radiotap_header_fixed hdr;
	u8 flags;
	u8 rate;
	u8 antsignal;
} __packed;

#define RX_RADIOTAP_PRESENT (			\
	(1 << IEEE80211_RADIOTAP_FLAGS) |	\
	(1 << IEEE80211_RADIOTAP_RATE) |	\
	(1 << IEEE80211_RADIOTAP_DB_ANTSIGNAL) |\
	0)

