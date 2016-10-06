/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/
#ifndef __IEEE80211_H
#define __IEEE80211_H

#include "osdep_service.h"
#include "drv_types.h"
#include "wifi.h"
#include <linux/compiler.h>
#include <linux/wireless.h>

#define MGMT_QUEUE_NUM 5
#define ETH_ALEN	6
#define IEEE_CMD_SET_WPA_PARAM			1
#define IEEE_CMD_SET_WPA_IE			2
#define IEEE_CMD_SET_ENCRYPTION			3
#define IEEE_CMD_MLME				4

#define IEEE_PARAM_WPA_ENABLED			1
#define IEEE_PARAM_TKIP_COUNTERMEASURES		2
#define IEEE_PARAM_DROP_UNENCRYPTED		3
#define IEEE_PARAM_PRIVACY_INVOKED		4
#define IEEE_PARAM_AUTH_ALGS			5
#define IEEE_PARAM_IEEE_802_1X			6
#define IEEE_PARAM_WPAX_SELECT			7

#define AUTH_ALG_OPEN_SYSTEM			0x1
#define AUTH_ALG_SHARED_KEY			0x2
#define AUTH_ALG_LEAP				0x00000004

#define IEEE_MLME_STA_DEAUTH			1
#define IEEE_MLME_STA_DISASSOC			2

#define IEEE_CRYPT_ERR_UNKNOWN_ALG		2
#define IEEE_CRYPT_ERR_UNKNOWN_ADDR		3
#define IEEE_CRYPT_ERR_CRYPT_INIT_FAILED	4
#define IEEE_CRYPT_ERR_KEY_SET_FAILED		5
#define IEEE_CRYPT_ERR_TX_KEY_SET_FAILED	6
#define IEEE_CRYPT_ERR_CARD_CONF_FAILED		7

#define	IEEE_CRYPT_ALG_NAME_LEN			16

#define WPA_CIPHER_NONE				BIT(0)
#define WPA_CIPHER_WEP40			BIT(1)
#define WPA_CIPHER_WEP104			BIT(2)
#define WPA_CIPHER_TKIP				BIT(3)
#define WPA_CIPHER_CCMP				BIT(4)

#define WPA_SELECTOR_LEN			4
#define RSN_HEADER_LEN				4

#define RSN_SELECTOR_LEN 4

enum NETWORK_TYPE {
	WIRELESS_INVALID	= 0,
	WIRELESS_11B		= 1,
	WIRELESS_11G		= 2,
	WIRELESS_11BG		= (WIRELESS_11B | WIRELESS_11G),
	WIRELESS_11A		= 4,
	WIRELESS_11N		= 8,
	WIRELESS_11GN		= (WIRELESS_11G | WIRELESS_11N),
	WIRELESS_11BGN		= (WIRELESS_11B | WIRELESS_11G | WIRELESS_11N),
};

struct ieee_param {
	u32 cmd;
	u8 sta_addr[ETH_ALEN];
	union {
		struct {
			u8 name;
			u32 value;
		} wpa_param;
		struct {
			u32 len;
			u8 reserved[32];
			u8 data[0];
		} wpa_ie;
		struct {
			int command;
			int reason_code;
		} mlme;
		struct {
			u8 alg[IEEE_CRYPT_ALG_NAME_LEN];
			u8 set_tx;
			u32 err;
			u8 idx;
			u8 seq[8]; /* sequence counter (set: RX, get: TX) */
			u16 key_len;
			u8 key[0];
		} crypt;
	} u;
};

#define IEEE80211_DATA_LEN		2304
/* Maximum size for the MA-UNITDATA primitive, 802.11 standard section
 * 6.2.1.1.2.
 *
 * The figure in section 7.1.2 suggests a body size of up to 2312
 * bytes is allowed, which is a bit confusing, I suspect this
 * represents the 2304 bytes of real data, plus a possible 8 bytes of
 * WEP IV and ICV. (this interpretation suggested by Ramiro Barreiro)
 */

#define IEEE80211_HLEN			30
#define IEEE80211_FRAME_LEN		(IEEE80211_DATA_LEN + IEEE80211_HLEN)

/* this is stolen from ipw2200 driver */
#define IEEE_IBSS_MAC_HASH_SIZE 31

struct ieee_ibss_seq {
	u8 mac[ETH_ALEN];
	u16 seq_num;
	u16 frag_num;
	unsigned long packet_time;
	struct list_head list;
};

struct ieee80211_hdr {
	u16 frame_ctl;
	u16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	u16 seq_ctl;
	u8 addr4[ETH_ALEN];
} __packed;

struct ieee80211_hdr_3addr {
	u16 frame_ctl;
	u16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	u16 seq_ctl;
} __packed;

struct	ieee80211_hdr_qos {
	u16 frame_ctl;
	u16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	u16 seq_ctl;
	u8 addr4[ETH_ALEN];
	u16	qc;
}  __packed;

struct  ieee80211_hdr_3addr_qos {
	u16 frame_ctl;
	u16 duration_id;
	u8  addr1[ETH_ALEN];
	u8  addr2[ETH_ALEN];
	u8  addr3[ETH_ALEN];
	u16 seq_ctl;
	u16 qc;
}  __packed;

struct eapol {
	u8 snap[6];
	u16 ethertype;
	u8 version;
	u8 type;
	u16 length;
} __packed;

enum eap_type {
	EAP_PACKET = 0,
	EAPOL_START,
	EAPOL_LOGOFF,
	EAPOL_KEY,
	EAPOL_ENCAP_ASF_ALERT
};

#define IEEE80211_3ADDR_LEN 24
#define IEEE80211_4ADDR_LEN 30
#define IEEE80211_FCS_LEN    4

#define MIN_FRAG_THRESHOLD     256U
#define	MAX_FRAG_THRESHOLD     2346U

/* Frame control field constants */
#define IEEE80211_FCTL_VERS		0x0002
#define IEEE80211_FCTL_FTYPE		0x000c
#define IEEE80211_FCTL_STYPE		0x00f0
#define IEEE80211_FCTL_TODS		0x0100
#define IEEE80211_FCTL_FROMDS		0x0200
#define IEEE80211_FCTL_MOREFRAGS	0x0400
#define IEEE80211_FCTL_RETRY		0x0800
#define IEEE80211_FCTL_PM		0x1000
#define IEEE80211_FCTL_MOREDATA	0x2000
#define IEEE80211_FCTL_WEP		0x4000
#define IEEE80211_FCTL_ORDER		0x8000

#define IEEE80211_FTYPE_MGMT		0x0000
#define IEEE80211_FTYPE_CTL		0x0004
#define IEEE80211_FTYPE_DATA		0x0008

/* management */
#define IEEE80211_STYPE_ASSOC_REQ	0x0000
#define IEEE80211_STYPE_ASSOC_RESP	0x0010
#define IEEE80211_STYPE_REASSOC_REQ	0x0020
#define IEEE80211_STYPE_REASSOC_RESP	0x0030
#define IEEE80211_STYPE_PROBE_REQ	0x0040
#define IEEE80211_STYPE_PROBE_RESP	0x0050
#define IEEE80211_STYPE_BEACON		0x0080
#define IEEE80211_STYPE_ATIM		0x0090
#define IEEE80211_STYPE_DISASSOC	0x00A0
#define IEEE80211_STYPE_AUTH		0x00B0
#define IEEE80211_STYPE_DEAUTH		0x00C0

/* control */
#define IEEE80211_STYPE_PSPOLL		0x00A0
#define IEEE80211_STYPE_RTS		0x00B0
#define IEEE80211_STYPE_CTS		0x00C0
#define IEEE80211_STYPE_ACK		0x00D0
#define IEEE80211_STYPE_CFEND		0x00E0
#define IEEE80211_STYPE_CFENDACK	0x00F0

/* data */
#define IEEE80211_STYPE_DATA		0x0000
#define IEEE80211_STYPE_DATA_CFACK	0x0010
#define IEEE80211_STYPE_DATA_CFPOLL	0x0020
#define IEEE80211_STYPE_DATA_CFACKPOLL	0x0030
#define IEEE80211_STYPE_NULLFUNC	0x0040
#define IEEE80211_STYPE_CFACK		0x0050
#define IEEE80211_STYPE_CFPOLL		0x0060
#define IEEE80211_STYPE_CFACKPOLL	0x0070
#define IEEE80211_QOS_DATAGRP		0x0080

#define IEEE80211_SCTL_FRAG		0x000F
#define IEEE80211_SCTL_SEQ		0xFFF0

/* QoS,QOS */
#define NORMAL_ACK			0
#define NO_ACK				1
#define NON_EXPLICIT_ACK	2
#define BLOCK_ACK			3

#ifndef ETH_P_PAE
#define ETH_P_PAE 0x888E /* Port Access Entity (IEEE 802.1X) */
#endif /* ETH_P_PAE */

#define ETH_P_PREAUTH 0x88C7 /* IEEE 802.11i pre-authentication */

#define ETH_P_ECONET	0x0018

#ifndef ETH_P_80211_RAW
#define ETH_P_80211_RAW (ETH_P_ECONET + 1)
#endif

/* IEEE 802.11 defines */

#define P80211_OUI_LEN 3

struct ieee80211_snap_hdr {
	u8    dsap;   /* always 0xAA */
	u8    ssap;   /* always 0xAA */
	u8    ctrl;   /* always 0x03 */
	u8    oui[P80211_OUI_LEN];    /* organizational universal id */
} __packed;

#define SNAP_SIZE sizeof(struct ieee80211_snap_hdr)

#define WLAN_FC_GET_TYPE(fc) ((fc) & IEEE80211_FCTL_FTYPE)
#define WLAN_FC_GET_STYPE(fc) ((fc) & IEEE80211_FCTL_STYPE)

#define WLAN_QC_GET_TID(qc) ((qc) & 0x0f)

#define WLAN_GET_SEQ_FRAG(seq) ((seq) & IEEE80211_SCTL_FRAG)
#define WLAN_GET_SEQ_SEQ(seq)  ((seq) & IEEE80211_SCTL_SEQ)

/* Authentication algorithms */
#define WLAN_AUTH_OPEN 0
#define WLAN_AUTH_SHARED_KEY 1

#define WLAN_AUTH_CHALLENGE_LEN 128

#define WLAN_CAPABILITY_BSS BIT(0)
#define WLAN_CAPABILITY_IBSS BIT(1)
#define WLAN_CAPABILITY_CF_POLLABLE BIT(2)
#define WLAN_CAPABILITY_CF_POLL_REQUEST BIT(3)
#define WLAN_CAPABILITY_PRIVACY BIT(4)
#define WLAN_CAPABILITY_SHORT_PREAMBLE BIT(5)
#define WLAN_CAPABILITY_PBCC BIT(6)
#define WLAN_CAPABILITY_CHANNEL_AGILITY BIT(7)
#define WLAN_CAPABILITY_SHORT_SLOT BIT(10)

/* Information Element IDs */
#define WLAN_EID_SSID 0
#define WLAN_EID_SUPP_RATES 1
#define WLAN_EID_FH_PARAMS 2
#define WLAN_EID_DS_PARAMS 3
#define WLAN_EID_CF_PARAMS 4
#define WLAN_EID_TIM 5
#define WLAN_EID_IBSS_PARAMS 6
#define WLAN_EID_CHALLENGE 16
#define WLAN_EID_RSN 48
#define WLAN_EID_GENERIC 221

#define IEEE80211_MGMT_HDR_LEN 24
#define IEEE80211_DATA_HDR3_LEN 24
#define IEEE80211_DATA_HDR4_LEN 30

#define IEEE80211_STATMASK_SIGNAL BIT(0)
#define IEEE80211_STATMASK_RSSI BIT(1)
#define IEEE80211_STATMASK_NOISE BIT(2)
#define IEEE80211_STATMASK_RATE BIT(3)
#define IEEE80211_STATMASK_WEMASK 0x7

#define IEEE80211_CCK_MODULATION    BIT(0)
#define IEEE80211_OFDM_MODULATION   BIT(1)

#define IEEE80211_24GHZ_BAND     BIT(0)
#define IEEE80211_52GHZ_BAND     BIT(1)

#define IEEE80211_CCK_RATE_LEN			4
#define IEEE80211_NUM_OFDM_RATESLEN	8

#define IEEE80211_CCK_RATE_1MB		        0x02
#define IEEE80211_CCK_RATE_2MB		        0x04
#define IEEE80211_CCK_RATE_5MB		        0x0B
#define IEEE80211_CCK_RATE_11MB		        0x16
#define IEEE80211_OFDM_RATE_LEN			8
#define IEEE80211_OFDM_RATE_6MB		        0x0C
#define IEEE80211_OFDM_RATE_9MB		        0x12
#define IEEE80211_OFDM_RATE_12MB		0x18
#define IEEE80211_OFDM_RATE_18MB		0x24
#define IEEE80211_OFDM_RATE_24MB		0x30
#define IEEE80211_OFDM_RATE_36MB		0x48
#define IEEE80211_OFDM_RATE_48MB		0x60
#define IEEE80211_OFDM_RATE_54MB		0x6C
#define IEEE80211_BASIC_RATE_MASK		0x80

#define IEEE80211_CCK_RATE_1MB_MASK		BIT(0)
#define IEEE80211_CCK_RATE_2MB_MASK		BIT(1)
#define IEEE80211_CCK_RATE_5MB_MASK		BIT(2)
#define IEEE80211_CCK_RATE_11MB_MASK		BIT(3)
#define IEEE80211_OFDM_RATE_6MB_MASK		BIT(4)
#define IEEE80211_OFDM_RATE_9MB_MASK		BIT(5)
#define IEEE80211_OFDM_RATE_12MB_MASK		BIT(6)
#define IEEE80211_OFDM_RATE_18MB_MASK		BIT(7)
#define IEEE80211_OFDM_RATE_24MB_MASK		BIT(8)
#define IEEE80211_OFDM_RATE_36MB_MASK		BIT(9)
#define IEEE80211_OFDM_RATE_48MB_MASK		BIT(10)
#define IEEE80211_OFDM_RATE_54MB_MASK		BIT(11)

#define IEEE80211_CCK_RATES_MASK	        0x0000000F
#define IEEE80211_CCK_BASIC_RATES_MASK		(IEEE80211_CCK_RATE_1MB_MASK | \
	IEEE80211_CCK_RATE_2MB_MASK)
#define IEEE80211_CCK_DEFAULT_RATES_MASK   (IEEE80211_CCK_BASIC_RATES_MASK | \
					   IEEE80211_CCK_RATE_5MB_MASK | \
					   IEEE80211_CCK_RATE_11MB_MASK)

#define IEEE80211_OFDM_RATES_MASK		0x00000FF0
#define IEEE80211_OFDM_BASIC_RATES_MASK	(IEEE80211_OFDM_RATE_6MB_MASK | \
	IEEE80211_OFDM_RATE_12MB_MASK | \
	IEEE80211_OFDM_RATE_24MB_MASK)
#define IEEE80211_OFDM_DEFAULT_RATES_MASK  (IEEE80211_OFDM_BASIC_RATES_MASK | \
					   IEEE80211_OFDM_RATE_9MB_MASK  | \
					   IEEE80211_OFDM_RATE_18MB_MASK | \
					   IEEE80211_OFDM_RATE_36MB_MASK | \
					   IEEE80211_OFDM_RATE_48MB_MASK | \
					   IEEE80211_OFDM_RATE_54MB_MASK)
#define IEEE80211_DEFAULT_RATES_MASK (IEEE80211_OFDM_DEFAULT_RATES_MASK | \
				     IEEE80211_CCK_DEFAULT_RATES_MASK)

#define IEEE80211_NUM_OFDM_RATES	    8
#define IEEE80211_NUM_CCK_RATES	            4
#define IEEE80211_OFDM_SHIFT_MASK_A         4

/* NOTE: This data is for statistical purposes; not all hardware provides this
 *       information for frames received.  Not setting these will not cause
 *       any adverse affects.
 */
struct ieee80211_rx_stats {
	s8 rssi;
	u8 signal;
	u8 noise;
	u8 received_channel;
	u16 rate; /* in 100 kbps */
	u8 mask;
	u8 freq;
	u16 len;
};

/* IEEE 802.11 requires that STA supports concurrent reception of at least
 * three fragmented frames. This define can be increased to support more
 * concurrent frames, but it should be noted that each entry can consume about
 * 2 kB of RAM and increasing cache size will slow down frame reassembly.
 */
#define IEEE80211_FRAG_CACHE_LEN 4

struct ieee80211_frag_entry {
	u32 first_frag_time;
	uint seq;
	uint last_frag;
	uint qos;   /*jackson*/
	uint tid;	/*jackson*/
	struct sk_buff *skb;
	u8 src_addr[ETH_ALEN];
	u8 dst_addr[ETH_ALEN];
};

struct ieee80211_stats {
	uint tx_unicast_frames;
	uint tx_multicast_frames;
	uint tx_fragments;
	uint tx_unicast_octets;
	uint tx_multicast_octets;
	uint tx_deferred_transmissions;
	uint tx_single_retry_frames;
	uint tx_multiple_retry_frames;
	uint tx_retry_limit_exceeded;
	uint tx_discards;
	uint rx_unicast_frames;
	uint rx_multicast_frames;
	uint rx_fragments;
	uint rx_unicast_octets;
	uint rx_multicast_octets;
	uint rx_fcs_errors;
	uint rx_discards_no_buffer;
	uint tx_discards_wrong_sa;
	uint rx_discards_undecryptable;
	uint rx_message_in_msg_fragments;
	uint rx_message_in_bad_msg_fragments;
};

struct ieee80211_softmac_stats {
	uint rx_ass_ok;
	uint rx_ass_err;
	uint rx_probe_rq;
	uint tx_probe_rs;
	uint tx_beacons;
	uint rx_auth_rq;
	uint rx_auth_rs_ok;
	uint rx_auth_rs_err;
	uint tx_auth_rq;
	uint no_auth_rs;
	uint no_ass_rs;
	uint tx_ass_rq;
	uint rx_ass_rq;
	uint tx_probe_rq;
	uint reassoc;
	uint swtxstop;
	uint swtxawake;
};

#define SEC_KEY_1         BIT(0)
#define SEC_KEY_2         BIT(1)
#define SEC_KEY_3         BIT(2)
#define SEC_KEY_4         BIT(3)
#define SEC_ACTIVE_KEY    BIT(4)
#define SEC_AUTH_MODE     BIT(5)
#define SEC_UNICAST_GROUP BIT(6)
#define SEC_LEVEL         BIT(7)
#define SEC_ENABLED       BIT(8)

#define SEC_LEVEL_0      0 /* None */
#define SEC_LEVEL_1      1 /* WEP 40 and 104 bit */
#define SEC_LEVEL_2      2 /* Level 1 + TKIP */
#define SEC_LEVEL_2_CKIP 3 /* Level 1 + CKIP */
#define SEC_LEVEL_3      4 /* Level 2 + CCMP */

#define WEP_KEYS 4
#define WEP_KEY_LEN 13

struct ieee80211_security {
	u16 active_key:2,
	    enabled:1,
	    auth_mode:2,
	    auth_algo:4,
	    unicast_uses_group:1;
	u8 key_sizes[WEP_KEYS];
	u8 keys[WEP_KEYS][WEP_KEY_LEN];
	u8 level;
	u16 flags;
} __packed;

/*
 *
 * 802.11 data frame from AP
 *
 *       ,-------------------------------------------------------------------.
 * Bytes |  2   |  2   |    6    |    6    |    6    |  2   | 0..2312 |   4  |
 *       |------|------|---------|---------|---------|------|---------|------|
 * Desc. | ctrl | dura |  DA/RA  |   TA    |    SA   | Sequ |  frame  |  fcs |
 *       |      | tion | (BSSID) |         |         | ence |  data   |      |
 *       `-------------------------------------------------------------------'
 *
 * Total: 28-2340 bytes
 *
 */

struct ieee80211_header_data {
	u16 frame_ctl;
	u16 duration_id;
	u8 addr1[6];
	u8 addr2[6];
	u8 addr3[6];
	u16 seq_ctrl;
};

#define BEACON_PROBE_SSID_ID_POSITION 12

/* Management Frame Information Element Types */
#define MFIE_TYPE_SSID       0
#define MFIE_TYPE_RATES      1
#define MFIE_TYPE_FH_SET     2
#define MFIE_TYPE_DS_SET     3
#define MFIE_TYPE_CF_SET     4
#define MFIE_TYPE_TIM        5
#define MFIE_TYPE_IBSS_SET   6
#define MFIE_TYPE_CHALLENGE  16
#define MFIE_TYPE_ERP        42
#define MFIE_TYPE_RSN	     48
#define MFIE_TYPE_RATES_EX   50
#define MFIE_TYPE_GENERIC    221

struct ieee80211_info_element_hdr {
	u8 id;
	u8 len;
} __packed;

struct ieee80211_info_element {
	u8 id;
	u8 len;
	u8 data[0];
} __packed;

/*
 * These are the data types that can make up management packets
 *
	u16 auth_algorithm;
	u16 auth_sequence;
	u16 beacon_interval;
	u16 capability;
	u8 current_ap[ETH_ALEN];
	u16 listen_interval;
	struct {
		u16 association_id:14, reserved:2;
	} __packed;
	u32 time_stamp[2];
	u16 reason;
	u16 status;
*/

#define IEEE80211_DEFAULT_TX_ESSID "Penguin"
#define IEEE80211_DEFAULT_BASIC_RATE 10

struct ieee80211_authentication {
	struct ieee80211_header_data header;
	u16 algorithm;
	u16 transaction;
	u16 status;
} __packed;

struct ieee80211_probe_response {
	struct ieee80211_header_data header;
	u32 time_stamp[2];
	u16 beacon_interval;
	u16 capability;
	struct ieee80211_info_element info_element;
} __packed;

struct ieee80211_probe_request {
	struct ieee80211_header_data header;
} __packed;

struct ieee80211_assoc_request_frame {
	struct ieee80211_hdr_3addr header;
	u16 capability;
	u16 listen_interval;
	struct ieee80211_info_element_hdr info_element;
} __packed;

struct ieee80211_assoc_response_frame {
	struct ieee80211_hdr_3addr header;
	u16 capability;
	u16 status;
	u16 aid;
} __packed;

struct ieee80211_txb {
	u8 nr_frags;
	u8 encrypted;
	u16 reserved;
	u16 frag_size;
	u16 payload_size;
	struct sk_buff *fragments[0];
};

/* SWEEP TABLE ENTRIES NUMBER*/
#define MAX_SWEEP_TAB_ENTRIES		  42
#define MAX_SWEEP_TAB_ENTRIES_PER_PACKET  7
/* MAX_RATES_LENGTH needs to be 12.  The spec says 8, and many APs
 * only use 8, and then use extended rates for the remaining supported
 * rates.  Other APs, however, stick all of their supported rates on the
 * main rates information element...
 */
#define MAX_RATES_LENGTH                  ((u8)12)
#define MAX_RATES_EX_LENGTH               ((u8)16)
#define MAX_NETWORK_COUNT                  128
#define MAX_CHANNEL_NUMBER                 161
#define IEEE80211_SOFTMAC_SCAN_TIME	  400
/*(HZ / 2)*/
#define IEEE80211_SOFTMAC_ASSOC_RETRY_TIME (HZ * 2)

#define CRC_LENGTH                 4U

#define MAX_WPA_IE_LEN 128

#define NETWORK_EMPTY_ESSID BIT(0)
#define NETWORK_HAS_OFDM    BIT(1)
#define NETWORK_HAS_CCK     BIT(2)

#define IEEE80211_DTIM_MBCAST 4
#define IEEE80211_DTIM_UCAST 2
#define IEEE80211_DTIM_VALID 1
#define IEEE80211_DTIM_INVALID 0

#define IEEE80211_PS_DISABLED 0
#define IEEE80211_PS_UNICAST IEEE80211_DTIM_UCAST
#define IEEE80211_PS_MBCAST IEEE80211_DTIM_MBCAST
#define IW_ESSID_MAX_SIZE 32
/*
 * join_res:
 * -1: authentication fail
 * -2: association fail
 * > 0: TID
 */

enum ieee80211_state {
	/* the card is not linked at all */
	IEEE80211_NOLINK = 0,
	/* IEEE80211_ASSOCIATING* are for BSS client mode
	 * the driver shall not perform RX filtering unless
	 * the state is LINKED.
	 * The driver shall just check for the state LINKED and
	 * defaults to NOLINK for ALL the other states (including
	 * LINKED_SCANNING)
	 */
	/* the association procedure will start (wq scheduling)*/
	IEEE80211_ASSOCIATING,
	IEEE80211_ASSOCIATING_RETRY,
	/* the association procedure is sending AUTH request*/
	IEEE80211_ASSOCIATING_AUTHENTICATING,
	/* the association procedure has successfully authenticated
	 * and is sending association request
	 */
	IEEE80211_ASSOCIATING_AUTHENTICATED,
	/* the link is ok. the card associated to a BSS or linked
	 * to a ibss cell or acting as an AP and creating the bss
	 */
	IEEE80211_LINKED,
	/* same as LINKED, but the driver shall apply RX filter
	 * rules as we are in NO_LINK mode. As the card is still
	 * logically linked, but it is doing a syncro site survey
	 * then it will be back to LINKED state.
	 */
	IEEE80211_LINKED_SCANNING,
};

#define DEFAULT_MAX_SCAN_AGE (15 * HZ)
#define DEFAULT_FTS 2346

#define CFG_IEEE80211_RESERVE_FCS BIT(0)
#define CFG_IEEE80211_COMPUTE_FCS BIT(1)

#define MAXTID	16

#define IEEE_A            BIT(0)
#define IEEE_B            BIT(1)
#define IEEE_G            BIT(2)
#define IEEE_MODE_MASK    (IEEE_A | IEEE_B | IEEE_G)

static inline int ieee80211_is_empty_essid(const char *essid, int essid_len)
{
	/* Single white space is for Linksys APs */
	if (essid_len == 1 && essid[0] == ' ')
		return 1;
	/* Otherwise, if the entire essid is 0, we assume it is hidden */
	while (essid_len) {
		essid_len--;
		if (essid[essid_len] != '\0')
			return 0;
	}
	return 1;
}

static inline int ieee80211_get_hdrlen(u16 fc)
{
	int hdrlen = 24;

	switch (WLAN_FC_GET_TYPE(fc)) {
	case IEEE80211_FTYPE_DATA:
		if (fc & IEEE80211_QOS_DATAGRP)
			hdrlen += 2;
		if ((fc & IEEE80211_FCTL_FROMDS) && (fc & IEEE80211_FCTL_TODS))
			hdrlen += 6; /* Addr4 */
		break;
	case IEEE80211_FTYPE_CTL:
		switch (WLAN_FC_GET_STYPE(fc)) {
		case IEEE80211_STYPE_CTS:
		case IEEE80211_STYPE_ACK:
			hdrlen = 10;
			break;
		default:
			hdrlen = 16;
			break;
		}
		break;
	}
	return hdrlen;
}

struct registry_priv;

u8 *r8712_set_ie(u8 *pbuf, sint index, uint len, u8 *source, uint *frlen);
u8 *r8712_get_ie(u8 *pbuf, sint index, sint *len, sint limit);
unsigned char *r8712_get_wpa_ie(unsigned char *pie, int *rsn_ie_len, int limit);
unsigned char *r8712_get_wpa2_ie(unsigned char *pie, int *rsn_ie_len,
				 int limit);
int r8712_parse_wpa_ie(u8 *wpa_ie, int wpa_ie_len, int *group_cipher,
		       int *pairwise_cipher);
int r8712_parse_wpa2_ie(u8 *wpa_ie, int wpa_ie_len, int *group_cipher,
			int *pairwise_cipher);
int r8712_get_sec_ie(u8 *in_ie, uint in_len, u8 *rsn_ie, u16 *rsn_len,
		     u8 *wpa_ie, u16 *wpa_len);
int r8712_get_wps_ie(u8 *in_ie, uint in_len, u8 *wps_ie, uint *wps_ielen);
int r8712_generate_ie(struct registry_priv *pregistrypriv);
uint r8712_is_cckrates_included(u8 *rate);
uint r8712_is_cckratesonly_included(u8 *rate);

#endif /* IEEE80211_H */

