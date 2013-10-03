/*
 * Merged with mainline ieee80211.h in Aug 2004.  Original ieee802_11
 * remains copyright by the original authors
 *
 * Portions of the merged code are based on Host AP (software wireless
 * LAN access point) driver for Intersil Prism2/2.5/3.
 *
 * Copyright (c) 2001-2002, SSH Communications Security Corp and Jouni Malinen
 * <jkmaline@cc.hut.fi>
 * Copyright (c) 2002-2003, Jouni Malinen <jkmaline@cc.hut.fi>
 *
 * Adaption to a generic IEEE 802.11 stack by James Ketrenos
 * <jketreno@linux.intel.com>
 * Copyright (c) 2004, Intel Corporation
 *
 * Modified for Realtek's wi-fi cards by Andrea Merello
 * <andrea.merello@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. See README and COPYING for
 * more details.
 */
#ifndef IEEE80211_H
#define IEEE80211_H
#include <linux/if_ether.h> /* ETH_ALEN */
#include <linux/kernel.h>   /* ARRAY_SIZE */
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/wireless.h>
#include <linux/ieee80211.h>
#include <linux/interrupt.h>

#define KEY_TYPE_NA		0x0
#define KEY_TYPE_WEP40 		0x1
#define KEY_TYPE_TKIP		0x2
#define KEY_TYPE_CCMP		0x4
#define KEY_TYPE_WEP104		0x5

#define aSifsTime					10

#define MGMT_QUEUE_NUM 5


#define IEEE_CMD_SET_WPA_PARAM			1
#define	IEEE_CMD_SET_WPA_IE			2
#define IEEE_CMD_SET_ENCRYPTION			3
#define IEEE_CMD_MLME				4

#define IEEE_PARAM_WPA_ENABLED			1
#define IEEE_PARAM_TKIP_COUNTERMEASURES		2
#define IEEE_PARAM_DROP_UNENCRYPTED		3
#define IEEE_PARAM_PRIVACY_INVOKED		4
#define IEEE_PARAM_AUTH_ALGS			5
#define IEEE_PARAM_IEEE_802_1X			6
//It should consistent with the driver_XXX.c
//   David, 2006.9.26
#define IEEE_PARAM_WPAX_SELECT			7
//Added for notify the encryption type selection
//   David, 2006.9.26
#define IEEE_PROTO_WPA				1
#define IEEE_PROTO_RSN				2
//Added for notify the encryption type selection
//   David, 2006.9.26
#define IEEE_WPAX_USEGROUP			0
#define IEEE_WPAX_WEP40				1
#define IEEE_WPAX_TKIP				2
#define IEEE_WPAX_WRAP   			3
#define IEEE_WPAX_CCMP				4
#define IEEE_WPAX_WEP104			5

#define IEEE_KEY_MGMT_IEEE8021X			1
#define IEEE_KEY_MGMT_PSK			2



#define IEEE_MLME_STA_DEAUTH			1
#define IEEE_MLME_STA_DISASSOC			2


#define IEEE_CRYPT_ERR_UNKNOWN_ALG		2
#define IEEE_CRYPT_ERR_UNKNOWN_ADDR		3
#define IEEE_CRYPT_ERR_CRYPT_INIT_FAILED		4
#define IEEE_CRYPT_ERR_KEY_SET_FAILED		5
#define IEEE_CRYPT_ERR_TX_KEY_SET_FAILED		6
#define IEEE_CRYPT_ERR_CARD_CONF_FAILED		7


#define	IEEE_CRYPT_ALG_NAME_LEN			16

//by amy for ps
typedef struct ieee_param {
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
	        struct{
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
}ieee_param;


#define MSECS(t) msecs_to_jiffies(t)
#define msleep_interruptible_rtl  msleep_interruptible

#define IEEE80211_DATA_LEN		2304
/* Maximum size for the MA-UNITDATA primitive, 802.11 standard section
   6.2.1.1.2.

   The figure in section 7.1.2 suggests a body size of up to 2312
   bytes is allowed, which is a bit confusing, I suspect this
   represents the 2304 bytes of real data, plus a possible 8 bytes of
   WEP IV and ICV. (this interpretation suggested by Ramiro Barreiro) */

#define IEEE80211_3ADDR_LEN 24
#define IEEE80211_4ADDR_LEN 30
#define IEEE80211_FCS_LEN    4
#define IEEE80211_HLEN			IEEE80211_4ADDR_LEN
#define IEEE80211_FRAME_LEN		(IEEE80211_DATA_LEN + IEEE80211_HLEN)
#define IEEE80211_MGMT_HDR_LEN 24
#define IEEE80211_DATA_HDR3_LEN 24
#define IEEE80211_DATA_HDR4_LEN 30

#define MIN_FRAG_THRESHOLD     256U
#define	MAX_FRAG_THRESHOLD     2346U

/* Frame control field constants */
#define IEEE80211_FCTL_DSTODS		0x0300 //added by david
#define IEEE80211_FCTL_WEP		0x4000

/* debug macros */

#ifdef CONFIG_IEEE80211_DEBUG
extern u32 ieee80211_debug_level;
#define IEEE80211_DEBUG(level, fmt, args...) \
do { if (ieee80211_debug_level & (level)) \
  printk(KERN_DEBUG "ieee80211: %c %s " fmt, \
         in_interrupt() ? 'I' : 'U', __func__ , ## args); } while (0)
#else
#define IEEE80211_DEBUG(level, fmt, args...) do {} while (0)
#endif	/* CONFIG_IEEE80211_DEBUG */

/*
 * To use the debug system;
 *
 * If you are defining a new debug classification, simply add it to the #define
 * list here in the form of:
 *
 * #define IEEE80211_DL_xxxx VALUE
 *
 * shifting value to the left one bit from the previous entry.  xxxx should be
 * the name of the classification (for example, WEP)
 *
 * You then need to either add a IEEE80211_xxxx_DEBUG() macro definition for your
 * classification, or use IEEE80211_DEBUG(IEEE80211_DL_xxxx, ...) whenever you want
 * to send output to that classification.
 *
 * To add your debug level to the list of levels seen when you perform
 *
 * % cat /proc/net/ipw/debug_level
 *
 * you simply need to add your entry to the ipw_debug_levels array.
 *
 * If you do not see debug_level in /proc/net/ipw then you do not have
 * CONFIG_IEEE80211_DEBUG defined in your kernel configuration
 *
 */

#define IEEE80211_DL_INFO          (1<<0)
#define IEEE80211_DL_WX            (1<<1)
#define IEEE80211_DL_SCAN          (1<<2)
#define IEEE80211_DL_STATE         (1<<3)
#define IEEE80211_DL_MGMT          (1<<4)
#define IEEE80211_DL_FRAG          (1<<5)
#define IEEE80211_DL_EAP           (1<<6)
#define IEEE80211_DL_DROP          (1<<7)

#define IEEE80211_DL_TX            (1<<8)
#define IEEE80211_DL_RX            (1<<9)

#define IEEE80211_ERROR(f, a...) printk(KERN_ERR "ieee80211: " f, ## a)
#define IEEE80211_WARNING(f, a...) printk(KERN_WARNING "ieee80211: " f, ## a)
#define IEEE80211_DEBUG_INFO(f, a...)   IEEE80211_DEBUG(IEEE80211_DL_INFO, f, ## a)

#define IEEE80211_DEBUG_WX(f, a...)     IEEE80211_DEBUG(IEEE80211_DL_WX, f, ## a)
#define IEEE80211_DEBUG_SCAN(f, a...)   IEEE80211_DEBUG(IEEE80211_DL_SCAN, f, ## a)
//#define IEEE_DEBUG_SCAN  IEEE80211_WARNING
#define IEEE80211_DEBUG_STATE(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_STATE, f, ## a)
#define IEEE80211_DEBUG_MGMT(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_MGMT, f, ## a)
#define IEEE80211_DEBUG_FRAG(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_FRAG, f, ## a)
#define IEEE80211_DEBUG_EAP(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_EAP, f, ## a)
#define IEEE80211_DEBUG_DROP(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_DROP, f, ## a)
#define IEEE80211_DEBUG_TX(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_TX, f, ## a)
#define IEEE80211_DEBUG_RX(f, a...)  IEEE80211_DEBUG(IEEE80211_DL_RX, f, ## a)
#include <linux/netdevice.h>
#include <linux/if_arp.h> /* ARPHRD_ETHER */

#ifndef WIRELESS_SPY
#define WIRELESS_SPY		// enable iwspy support
#endif
#include <net/iw_handler.h>	// new driver API

#ifndef ETH_P_PAE
#define ETH_P_PAE 0x888E /* Port Access Entity (IEEE 802.1X) */
#endif /* ETH_P_PAE */

#define ETH_P_PREAUTH 0x88C7 /* IEEE 802.11i pre-authentication */

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

} __attribute__ ((packed));

#define SNAP_SIZE sizeof(struct ieee80211_snap_hdr)

#define WLAN_FC_GET_TYPE(fc) ((fc) & IEEE80211_FCTL_FTYPE)
#define WLAN_FC_GET_STYPE(fc) ((fc) & IEEE80211_FCTL_STYPE)

#define WLAN_GET_SEQ_FRAG(seq) ((seq) & IEEE80211_SCTL_FRAG)
#define WLAN_GET_SEQ_SEQ(seq)  ((seq) & IEEE80211_SCTL_SEQ)

#define WLAN_CAPABILITY_BSS (1<<0)
#define WLAN_CAPABILITY_SHORT_SLOT (1<<10)

#define IEEE80211_STATMASK_SIGNAL (1<<0)
#define IEEE80211_STATMASK_RSSI (1<<1)
#define IEEE80211_STATMASK_NOISE (1<<2)
#define IEEE80211_STATMASK_RATE (1<<3)
#define IEEE80211_STATMASK_WEMASK 0x7


#define IEEE80211_CCK_MODULATION    (1<<0)
#define IEEE80211_OFDM_MODULATION   (1<<1)

#define IEEE80211_24GHZ_BAND     (1<<0)
#define IEEE80211_52GHZ_BAND     (1<<1)

#define IEEE80211_CCK_RATE_LEN  		4
#define IEEE80211_CCK_RATE_1MB		        0x02
#define IEEE80211_CCK_RATE_2MB		        0x04
#define IEEE80211_CCK_RATE_5MB		        0x0B
#define IEEE80211_CCK_RATE_11MB		        0x16
#define IEEE80211_OFDM_RATE_LEN 		8
#define IEEE80211_OFDM_RATE_6MB		        0x0C
#define IEEE80211_OFDM_RATE_9MB		        0x12
#define IEEE80211_OFDM_RATE_12MB		0x18
#define IEEE80211_OFDM_RATE_18MB		0x24
#define IEEE80211_OFDM_RATE_24MB		0x30
#define IEEE80211_OFDM_RATE_36MB		0x48
#define IEEE80211_OFDM_RATE_48MB		0x60
#define IEEE80211_OFDM_RATE_54MB		0x6C
#define IEEE80211_BASIC_RATE_MASK		0x80

#define IEEE80211_CCK_RATE_1MB_MASK		(1<<0)
#define IEEE80211_CCK_RATE_2MB_MASK		(1<<1)
#define IEEE80211_CCK_RATE_5MB_MASK		(1<<2)
#define IEEE80211_CCK_RATE_11MB_MASK		(1<<3)
#define IEEE80211_OFDM_RATE_6MB_MASK		(1<<4)
#define IEEE80211_OFDM_RATE_9MB_MASK		(1<<5)
#define IEEE80211_OFDM_RATE_12MB_MASK		(1<<6)
#define IEEE80211_OFDM_RATE_18MB_MASK		(1<<7)
#define IEEE80211_OFDM_RATE_24MB_MASK		(1<<8)
#define IEEE80211_OFDM_RATE_36MB_MASK		(1<<9)
#define IEEE80211_OFDM_RATE_48MB_MASK		(1<<10)
#define IEEE80211_OFDM_RATE_54MB_MASK		(1<<11)

#define IEEE80211_CCK_RATES_MASK	        0x0000000F
#define IEEE80211_CCK_BASIC_RATES_MASK	(IEEE80211_CCK_RATE_1MB_MASK | \
	IEEE80211_CCK_RATE_2MB_MASK)
#define IEEE80211_CCK_DEFAULT_RATES_MASK	(IEEE80211_CCK_BASIC_RATES_MASK | \
        IEEE80211_CCK_RATE_5MB_MASK | \
        IEEE80211_CCK_RATE_11MB_MASK)

#define IEEE80211_OFDM_RATES_MASK		0x00000FF0
#define IEEE80211_OFDM_BASIC_RATES_MASK	(IEEE80211_OFDM_RATE_6MB_MASK | \
	IEEE80211_OFDM_RATE_12MB_MASK | \
	IEEE80211_OFDM_RATE_24MB_MASK)
#define IEEE80211_OFDM_DEFAULT_RATES_MASK	(IEEE80211_OFDM_BASIC_RATES_MASK | \
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

/* this is stolen and modified from the madwifi driver*/
#define IEEE80211_FC0_TYPE_MASK		0x0c
#define IEEE80211_FC0_TYPE_DATA		0x08
#define IEEE80211_FC0_SUBTYPE_MASK	0xB0
#define IEEE80211_FC0_SUBTYPE_QOS	0x80

#define IEEE80211_QOS_HAS_SEQ(fc) \
	(((fc) & (IEEE80211_FC0_TYPE_MASK | IEEE80211_FC0_SUBTYPE_MASK)) == \
	 (IEEE80211_FC0_TYPE_DATA | IEEE80211_FC0_SUBTYPE_QOS))

/* this is stolen from ipw2200 driver */
#define IEEE_IBSS_MAC_HASH_SIZE 31
struct ieee_ibss_seq {
	u8 mac[ETH_ALEN];
	u16 seq_num[17];
	u16 frag_num[17];
	unsigned long packet_time[17];
	struct list_head list;
};

/* NOTE: This data is for statistical purposes; not all hardware provides this
 *       information for frames received.  Not setting these will not cause
 *       any adverse affects. */
struct ieee80211_rx_stats {
	u32 mac_time[2];
	u8 signalstrength;
	s8 rssi;
	u8 signal;
	u8 noise;
	u16 rate; /* in 100 kbps */
	u8 received_channel;
	u8 control;
	u8 mask;
	u8 freq;
	u16 len;
	u8 nic_type;
};

/* IEEE 802.11 requires that STA supports concurrent reception of at least
 * three fragmented frames. This define can be increased to support more
 * concurrent frames, but it should be noted that each entry can consume about
 * 2 kB of RAM and increasing cache size will slow down frame reassembly. */
#define IEEE80211_FRAG_CACHE_LEN 4

struct ieee80211_frag_entry {
	unsigned long first_frag_time;
	unsigned int seq;
	unsigned int last_frag;
	struct sk_buff *skb;
	u8 src_addr[ETH_ALEN];
	u8 dst_addr[ETH_ALEN];
};

struct ieee80211_stats {
	unsigned int tx_unicast_frames;
	unsigned int tx_multicast_frames;
	unsigned int tx_fragments;
	unsigned int tx_unicast_octets;
	unsigned int tx_multicast_octets;
	unsigned int tx_deferred_transmissions;
	unsigned int tx_single_retry_frames;
	unsigned int tx_multiple_retry_frames;
	unsigned int tx_retry_limit_exceeded;
	unsigned int tx_discards;
	unsigned int rx_unicast_frames;
	unsigned int rx_multicast_frames;
	unsigned int rx_fragments;
	unsigned int rx_unicast_octets;
	unsigned int rx_multicast_octets;
	unsigned int rx_fcs_errors;
	unsigned int rx_discards_no_buffer;
	unsigned int tx_discards_wrong_sa;
	unsigned int rx_discards_undecryptable;
	unsigned int rx_message_in_msg_fragments;
	unsigned int rx_message_in_bad_msg_fragments;
};

struct ieee80211_device;

#include "ieee80211_crypt.h"

#define SEC_KEY_1         (1<<0)
#define SEC_KEY_2         (1<<1)
#define SEC_KEY_3         (1<<2)
#define SEC_KEY_4         (1<<3)
#define SEC_ACTIVE_KEY    (1<<4)
#define SEC_AUTH_MODE     (1<<5)
#define SEC_UNICAST_GROUP (1<<6)
#define SEC_LEVEL         (1<<7)
#define SEC_ENABLED       (1<<8)

#define SEC_LEVEL_0      0 /* None */
#define SEC_LEVEL_1      1 /* WEP 40 and 104 bit */
#define SEC_LEVEL_2      2 /* Level 1 + TKIP */
#define SEC_LEVEL_2_CKIP 3 /* Level 1 + CKIP */
#define SEC_LEVEL_3      4 /* Level 2 + CCMP */

#define WEP_KEYS 4
#define WEP_KEY_LEN 13

#define WEP_KEY_LEN_MODIF 32

struct ieee80211_security {
	u16 active_key:2,
            enabled:1,
	    auth_mode:2,
            auth_algo:4,
            unicast_uses_group:1;
	u8 key_sizes[WEP_KEYS];
	u8 keys[WEP_KEYS][WEP_KEY_LEN_MODIF];
	u8 level;
	u16 flags;
} __attribute__ ((packed));


/*

 802.11 data frame from AP

      ,-------------------------------------------------------------------.
Bytes |  2   |  2   |    6    |    6    |    6    |  2   | 0..2312 |   4  |
      |------|------|---------|---------|---------|------|---------|------|
Desc. | ctrl | dura |  DA/RA  |   TA    |    SA   | Sequ |  frame  |  fcs |
      |      | tion | (BSSID) |         |         | ence |  data   |      |
      `-------------------------------------------------------------------'

Total: 28-2340 bytes

*/

/* Management Frame Information Element Types */
enum {
	MFIE_TYPE_SSID = 0,
	MFIE_TYPE_RATES = 1,
	MFIE_TYPE_FH_SET = 2,
	MFIE_TYPE_DS_SET = 3,
	MFIE_TYPE_CF_SET = 4,
	MFIE_TYPE_TIM = 5,
	MFIE_TYPE_IBSS_SET = 6,
	MFIE_TYPE_COUNTRY = 7,
	MFIE_TYPE_CHALLENGE = 16,
	MFIE_TYPE_ERP = 42,
	MFIE_TYPE_RSN = 48,
	MFIE_TYPE_RATES_EX = 50,
	MFIE_TYPE_GENERIC = 221,
};

struct ieee80211_header_data {
	u16 frame_ctl;
	u16 duration_id;
	u8 addr1[6];
	u8 addr2[6];
	u8 addr3[6];
	u16 seq_ctrl;
};

struct ieee80211_hdr_4addr {
	u16 frame_ctl;
	u16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	u16 seq_ctl;
	u8 addr4[ETH_ALEN];
} __attribute__ ((packed));

struct ieee80211_hdr_3addrqos {
	u16 frame_ctl;
	u16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	u16 seq_ctl;
	u16 qos_ctl;
} __attribute__ ((packed));

struct ieee80211_hdr_4addrqos {
	u16 frame_ctl;
	u16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	u16 seq_ctl;
	u8 addr4[ETH_ALEN];
	u16 qos_ctl;
} __attribute__ ((packed));

struct ieee80211_info_element_hdr {
	u8 id;
	u8 len;
} __attribute__ ((packed));

struct ieee80211_info_element {
	u8 id;
	u8 len;
	u8 data[0];
} __attribute__ ((packed));

struct ieee80211_authentication {
	struct ieee80211_header_data header;
	u16 algorithm;
	u16 transaction;
	u16 status;
	//struct ieee80211_info_element_hdr info_element;
} __attribute__ ((packed));

struct ieee80211_disassoc_frame {
	struct ieee80211_hdr_3addr header;
	u16    reasoncode;
} __attribute__ ((packed));

struct ieee80211_probe_request {
	struct ieee80211_header_data header;
	/* struct ieee80211_info_element info_element; */
} __attribute__ ((packed));

struct ieee80211_probe_response {
	struct ieee80211_header_data header;
	u32 time_stamp[2];
	u16 beacon_interval;
	u16 capability;
	struct ieee80211_info_element info_element;
} __attribute__ ((packed));

struct ieee80211_assoc_request_frame {
	struct ieee80211_hdr_3addr header;
	u16 capability;
	u16 listen_interval;
	//u8 current_ap[ETH_ALEN];
	struct ieee80211_info_element_hdr info_element;
} __attribute__ ((packed));

struct ieee80211_assoc_response_frame {
	struct ieee80211_hdr_3addr header;
	u16 capability;
	u16 status;
	u16 aid;
	struct ieee80211_info_element info_element; /* supported rates */
} __attribute__ ((packed));

struct ieee80211_txb {
	u8 nr_frags;
	u8 encrypted;
	u16 reserved;
	u16 frag_size;
	u16 payload_size;
	struct sk_buff *fragments[0];
};

/* SWEEP TABLE ENTRIES NUMBER */
#define MAX_SWEEP_TAB_ENTRIES			42
#define MAX_SWEEP_TAB_ENTRIES_PER_PACKET	7

/* MAX_RATES_LENGTH needs to be 12.  The spec says 8, and many APs
 * only use 8, and then use extended rates for the remaining supported
 * rates.  Other APs, however, stick all of their supported rates on the
 * main rates information element... */
#define MAX_RATES_LENGTH			((u8)12)
#define MAX_RATES_EX_LENGTH			((u8)16)

#define MAX_NETWORK_COUNT			128

#define MAX_CHANNEL_NUMBER			165

#define IEEE80211_SOFTMAC_SCAN_TIME		100 /* (HZ / 2) */
#define IEEE80211_SOFTMAC_ASSOC_RETRY_TIME	(HZ * 2)

#define CRC_LENGTH	4U

#define MAX_WPA_IE_LEN	64

#define NETWORK_EMPTY_ESSID	(1 << 0)
#define NETWORK_HAS_OFDM	(1 << 1)
#define NETWORK_HAS_CCK		(1 << 2)

struct ieee80211_wmm_ac_param {
	u8 ac_aci_acm_aifsn;
	u8 ac_ecwmin_ecwmax;
	u16 ac_txop_limit;
};

struct ieee80211_wmm_ts_info {
	u8 ac_dir_tid;
	u8 ac_up_psb;
	u8 reserved;
} __attribute__ ((packed));

struct ieee80211_wmm_tspec_elem {
	struct ieee80211_wmm_ts_info ts_info;
	u16 norm_msdu_size;
	u16 max_msdu_size;
	u32 min_serv_inter;
	u32 max_serv_inter;
	u32 inact_inter;
	u32 suspen_inter;
	u32 serv_start_time;
	u32 min_data_rate;
	u32 mean_data_rate;
	u32 peak_data_rate;
	u32 max_burst_size;
	u32 delay_bound;
	u32 min_phy_rate;
	u16 surp_band_allow;
	u16 medium_time;
}__attribute__((packed));

enum eap_type {
	EAP_PACKET = 0,
	EAPOL_START,
	EAPOL_LOGOFF,
	EAPOL_KEY,
	EAPOL_ENCAP_ASF_ALERT
};

static const char *eap_types[] = {
	[EAP_PACKET]		= "EAP-Packet",
	[EAPOL_START]		= "EAPOL-Start",
	[EAPOL_LOGOFF]		= "EAPOL-Logoff",
	[EAPOL_KEY]		= "EAPOL-Key",
	[EAPOL_ENCAP_ASF_ALERT]	= "EAPOL-Encap-ASF-Alert"
};

static inline const char *eap_get_type(int type)
{
	return (type >= ARRAY_SIZE(eap_types)) ? "Unknown" : eap_types[type];
}

struct eapol {
	u8 snap[6];
	u16 ethertype;
	u8 version;
	u8 type;
	u16 length;
} __attribute__ ((packed));

struct ieee80211_softmac_stats {
	unsigned int rx_ass_ok;
	unsigned int rx_ass_err;
	unsigned int rx_probe_rq;
	unsigned int tx_probe_rs;
	unsigned int tx_beacons;
	unsigned int rx_auth_rq;
	unsigned int rx_auth_rs_ok;
	unsigned int rx_auth_rs_err;
	unsigned int tx_auth_rq;
	unsigned int no_auth_rs;
	unsigned int no_ass_rs;
	unsigned int tx_ass_rq;
	unsigned int rx_ass_rq;
	unsigned int tx_probe_rq;
	unsigned int reassoc;
	unsigned int swtxstop;
	unsigned int swtxawake;
};

#define BEACON_PROBE_SSID_ID_POSITION 12

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
	} __attribute__ ((packed));
	u32 time_stamp[2];
	u16 reason;
	u16 status;
*/

#define IEEE80211_DEFAULT_TX_ESSID "Penguin"
#define IEEE80211_DEFAULT_BASIC_RATE 10

enum {WMM_all_frame, WMM_two_frame, WMM_four_frame, WMM_six_frame};
#define MAX_SP_Len  (WMM_all_frame << 4)
#define IEEE80211_QOS_TID 0x0f
#define QOS_CTL_NOTCONTAIN_ACK (0x01 << 5)

#define MAX_IE_LEN						0xFF //+YJ,080625

typedef struct _CHANNEL_LIST{
	u8	Channel[MAX_CHANNEL_NUMBER + 1];
	u8	Len;
}CHANNEL_LIST, *PCHANNEL_LIST;

//by amy for ps
#define IEEE80211_WATCH_DOG_TIME    2000
//by amy for ps
//by amy for antenna
#define ANTENNA_DIVERSITY_TIMER_PERIOD		1000 // 1000 m
//by amy for antenna

#define IEEE80211_DTIM_MBCAST 4
#define IEEE80211_DTIM_UCAST 2
#define IEEE80211_DTIM_VALID 1
#define IEEE80211_DTIM_INVALID 0

#define IEEE80211_PS_DISABLED 0
#define IEEE80211_PS_UNICAST IEEE80211_DTIM_UCAST
#define IEEE80211_PS_MBCAST IEEE80211_DTIM_MBCAST
#define IEEE80211_PS_ENABLE   IEEE80211_DTIM_VALID
//added by David for QoS 2006/6/30
//#define WMM_Hang_8187
#ifdef WMM_Hang_8187
#undef WMM_Hang_8187
#endif

#define WME_AC_BE   0x00
#define WME_AC_BK   0x01
#define WME_AC_VI   0x02
#define WME_AC_VO   0x03
#define WME_ACI_MASK 0x03
#define WME_AIFSN_MASK 0x03
#define WME_AC_PRAM_LEN 16

//UP Mapping to AC, using in MgntQuery_SequenceNumber() and maybe for DSCP
//#define UP2AC(up)	((up<3) ? ((up==0)?1:0) : (up>>1))
#define UP2AC(up) (		   \
	((up) < 1) ? WME_AC_BE : \
	((up) < 3) ? WME_AC_BK : \
	((up) < 4) ? WME_AC_BE : \
	((up) < 6) ? WME_AC_VI : \
	WME_AC_VO)
//AC Mapping to UP, using in Tx part for selecting the corresponding TX queue
#define AC2UP(_ac)	(       \
	((_ac) == WME_AC_VO) ? 6 : \
	((_ac) == WME_AC_VI) ? 5 : \
	((_ac) == WME_AC_BK) ? 1 : \
	0)

#define	ETHER_ADDR_LEN		6	/* length of an Ethernet address */
struct	ether_header {
	u8 ether_dhost[ETHER_ADDR_LEN];
	u8 ether_shost[ETHER_ADDR_LEN];
	u16 ether_type;
} __attribute__((packed));

#ifndef ETHERTYPE_PAE
#define	ETHERTYPE_PAE	0x888e		/* EAPOL PAE/802.1x */
#endif
#ifndef ETHERTYPE_IP
#define	ETHERTYPE_IP	0x0800		/* IP protocol */
#endif

struct ieee80211_network {
	/* These entries are used to identify a unique network */
	u8 bssid[ETH_ALEN];
	u8 channel;
	/* Ensure null-terminated for any debug msgs */
	u8 ssid[IW_ESSID_MAX_SIZE + 1];
	u8 ssid_len;

	/* These are network statistics */
	struct ieee80211_rx_stats stats;
	u16 capability;
	u8 rates[MAX_RATES_LENGTH];
	u8 rates_len;
	u8 rates_ex[MAX_RATES_EX_LENGTH];
	u8 rates_ex_len;
	unsigned long last_scanned;
	u8 mode;
	u8 flags;
	u32 last_associate;
	u32 time_stamp[2];
	u16 beacon_interval;
	u16 listen_interval;
	u16 atim_window;
	u8 wpa_ie[MAX_WPA_IE_LEN];
	size_t wpa_ie_len;
	u8 rsn_ie[MAX_WPA_IE_LEN];
	size_t rsn_ie_len;
	u8 dtim_period;
	u8 dtim_data;
	u32 last_dtim_sta_time[2];
	struct list_head list;
	//appeded for QoS
	u8 wmm_info;
	struct ieee80211_wmm_ac_param wmm_param[4];
	u8 QoS_Enable;
	u8 SignalStrength;
//by amy 080312
	u8 HighestOperaRate;
//by amy 080312
	u8 Turbo_Enable;//enable turbo mode, added by thomas
	u16 CountryIeLen;
	u8 CountryIeBuf[MAX_IE_LEN];
};

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

#define CFG_IEEE80211_RESERVE_FCS (1<<0)
#define CFG_IEEE80211_COMPUTE_FCS (1<<1)

typedef struct tx_pending_t{
	int frag;
	struct ieee80211_txb *txb;
}tx_pending_t;

enum {
	COUNTRY_CODE_FCC = 0,
	COUNTRY_CODE_IC = 1,
	COUNTRY_CODE_ETSI = 2,
	COUNTRY_CODE_SPAIN = 3,
	COUNTRY_CODE_FRANCE = 4,
	COUNTRY_CODE_MKK = 5,
	COUNTRY_CODE_MKK1 = 6,
	COUNTRY_CODE_ISRAEL = 7,
	COUNTRY_CODE_TELEC = 8,
	COUNTRY_CODE_GLOBAL_DOMAIN = 9,
	COUNTRY_CODE_WORLD_WIDE_13_INDEX = 10
};

struct ieee80211_device {
	struct net_device *dev;

	/* Bookkeeping structures */
	struct net_device_stats stats;
	struct ieee80211_stats ieee_stats;
	struct ieee80211_softmac_stats softmac_stats;

	/* Probe / Beacon management */
	struct list_head network_free_list;
	struct list_head network_list;
	struct ieee80211_network *networks;
	int scans;
	int scan_age;

	int iw_mode; /* operating mode (IW_MODE_*) */

	spinlock_t lock;
	spinlock_t wpax_suitlist_lock;

	int tx_headroom; /* Set to size of any additional room needed at front
			  * of allocated Tx SKBs */
	u32 config;

	/* WEP and other encryption related settings at the device level */
	int open_wep; /* Set to 1 to allow unencrypted frames */

	int reset_on_keychange; /* Set to 1 if the HW needs to be reset on
				 * WEP key changes */

	/* If the host performs {en,de}cryption, then set to 1 */
	int host_encrypt;
	int host_decrypt;
	int ieee802_1x; /* is IEEE 802.1X used */

	/* WPA data */
	int wpa_enabled;
	int drop_unencrypted;
	int tkip_countermeasures;
	int privacy_invoked;
	size_t wpa_ie_len;
	u8 *wpa_ie;

	u8 ap_mac_addr[6];
	u16 pairwise_key_type;
	u16 broadcast_key_type;

	struct list_head crypt_deinit_list;
	struct ieee80211_crypt_data *crypt[WEP_KEYS];
	int tx_keyidx; /* default TX key index (crypt[tx_keyidx]) */
	struct timer_list crypt_deinit_timer;

	int bcrx_sta_key; /* use individual keys to override default keys even
			   * with RX of broad/multicast frames */

	/* Fragmentation structures */
	/* each stream contains an entry */
	struct ieee80211_frag_entry frag_cache[17][IEEE80211_FRAG_CACHE_LEN];
	unsigned int frag_next_idx[17];
	u16 fts; /* Fragmentation Threshold */

	/* This stores infos for the current network.
	 * Either the network we are associated in INFRASTRUCTURE
	 * or the network that we are creating in MASTER mode.
	 * ad-hoc is a mixture ;-).
	 * Note that in infrastructure mode, even when not associated,
	 * fields bssid and essid may be valid (if wpa_set and essid_set
	 * are true) as thy carry the value set by the user via iwconfig
	 */
	struct ieee80211_network current_network;


	enum ieee80211_state state;

	int short_slot;
	int mode;       /* A, B, G */
	int modulation; /* CCK, OFDM */
	int freq_band;  /* 2.4Ghz, 5.2Ghz, Mixed */
	int abg_true;   /* ABG flag              */

	/* used for forcing the ibss workqueue to terminate
	 * without wait for the syncro scan to terminate
	 */
	short sync_scan_hurryup;

	void * pDot11dInfo;
	bool bGlobalDomain;

	// For Liteon Ch12~13 passive scan
	u8	MinPassiveChnlNum;
	u8	IbssStartChnl;

	int rate;       /* current rate */
	int basic_rate;
	//FIXME: please callback, see if redundant with softmac_features
	short active_scan;

	/* this contains flags for selectively enable softmac support */
	u16 softmac_features;

	/* if the sequence control field is not filled by HW */
	u16 seq_ctrl[5];

	/* association procedure transaction sequence number */
	u16 associate_seq;

	/* AID for RTXed association responses */
	u16 assoc_id;

	/* power save mode related*/
	short ps;
	short sta_sleep;
	int ps_timeout;
	struct tasklet_struct ps_task;
	u32 ps_th;
	u32 ps_tl;

	short raw_tx;
	/* used if IEEE_SOFTMAC_TX_QUEUE is set */
	short queue_stop;
	short scanning;
	short proto_started;

	struct semaphore wx_sem;
	struct semaphore scan_sem;

	spinlock_t mgmt_tx_lock;
	spinlock_t beacon_lock;

	short beacon_txing;

	short wap_set;
	short ssid_set;

	u8  wpax_type_set;    //{added by David, 2006.9.28}
	u32 wpax_type_notify; //{added by David, 2006.9.26}

	/* QoS related flag */
	char init_wmmparam_flag;

	/* for discarding duplicated packets in IBSS */
	struct list_head ibss_mac_hash[IEEE_IBSS_MAC_HASH_SIZE];

	/* for discarding duplicated packets in BSS */
	u16 last_rxseq_num[17]; /* rx seq previous per-tid */
	u16 last_rxfrag_num[17];/* tx frag previous per-tid */
	unsigned long last_packet_time[17];

	/* for PS mode */
	unsigned long last_rx_ps_time;

	/* used if IEEE_SOFTMAC_SINGLE_QUEUE is set */
	struct sk_buff *mgmt_queue_ring[MGMT_QUEUE_NUM];
	int mgmt_queue_head;
	int mgmt_queue_tail;


	/* used if IEEE_SOFTMAC_TX_QUEUE is set */
	struct  tx_pending_t tx_pending;

	/* used if IEEE_SOFTMAC_ASSOCIATE is set */
	struct timer_list associate_timer;

	/* used if IEEE_SOFTMAC_BEACONS is set */
	struct timer_list beacon_timer;

	struct work_struct associate_complete_wq;
//	struct work_struct associate_retry_wq;
	struct work_struct associate_procedure_wq;
//	struct work_struct softmac_scan_wq;
	struct work_struct wx_sync_scan_wq;
	struct work_struct wmm_param_update_wq;
	struct work_struct ps_request_tx_ack_wq;//for ps
//	struct work_struct hw_wakeup_wq;
//	struct work_struct hw_sleep_wq;
//	struct work_struct watch_dog_wq;
	bool bInactivePs;
	bool actscanning;
	bool beinretry;
	u16 ListenInterval;
	unsigned long NumRxDataInPeriod; //YJ,add,080828
	unsigned long NumRxBcnInPeriod;  //YJ,add,080828
	unsigned long NumRxOkTotal;
	unsigned long NumRxUnicast;//YJ,add,080828,for keep alive
	bool bHwRadioOff;
        struct delayed_work softmac_scan_wq;
        struct delayed_work associate_retry_wq;
	struct delayed_work hw_wakeup_wq;
	struct delayed_work hw_sleep_wq;//+by amy 080324
	struct delayed_work watch_dog_wq;
	struct delayed_work sw_antenna_wq;
	struct delayed_work  start_ibss_wq;
//by amy for rate adaptive 080312
    struct delayed_work rate_adapter_wq;
//by amy for rate adaptive
	struct delayed_work hw_dig_wq;
	struct delayed_work tx_pw_wq;

//Added for RF power on power off by lizhaoming 080512
	struct delayed_work GPIOChangeRFWorkItem;

	struct workqueue_struct *wq;

	/* Callback functions */
	void (*set_security)(struct net_device *dev,
			     struct ieee80211_security *sec);

	/* Used to TX data frame by using txb structs.
	 * this is not used if in the softmac_features
	 * is set the flag IEEE_SOFTMAC_TX_QUEUE
	 */
	int (*hard_start_xmit)(struct ieee80211_txb *txb,
			       struct net_device *dev);

	int (*reset_port)(struct net_device *dev);

	/* Softmac-generated frames (management) are TXed via this
	 * callback if the flag IEEE_SOFTMAC_SINGLE_QUEUE is
	 * not set. As some cards may have different HW queues that
	 * one might want to use for data and management frames
	 * the option to have two callbacks might be useful.
	 * This function can't sleep.
	 */
	int (*softmac_hard_start_xmit)(struct sk_buff *skb,
			       struct net_device *dev);

	/* used instead of hard_start_xmit (not softmac_hard_start_xmit)
	 * if the IEEE_SOFTMAC_TX_QUEUE feature is used to TX data
	 * frames. If the option IEEE_SOFTMAC_SINGLE_QUEUE is also set
	 * then also management frames are sent via this callback.
	 * This function can't sleep.
	 */
	void (*softmac_data_hard_start_xmit)(struct sk_buff *skb,
			       struct net_device *dev,int rate);

	/* stops the HW queue for DATA frames. Useful to avoid
	 * waste time to TX data frame when we are reassociating
	 * This function can sleep.
	 */
	void (*data_hard_stop)(struct net_device *dev);

	/* OK this is complementar to data_poll_hard_stop */
	void (*data_hard_resume)(struct net_device *dev);

	/* ask to the driver to retune the radio .
	 * This function can sleep. the driver should ensure
	 * the radio has been switched before return.
	 */
	void (*set_chan)(struct net_device *dev,short ch);

	/* These are not used if the ieee stack takes care of
	 * scanning (IEEE_SOFTMAC_SCAN feature set).
	 * In this case only the set_chan is used.
	 *
	 * The syncro version is similar to the start_scan but
	 * does not return until all channels has been scanned.
	 * this is called in user context and should sleep,
	 * it is called in a work_queue when switching to ad-hoc mode
	 * or in behalf of iwlist scan when the card is associated
	 * and root user ask for a scan.
	 * the function stop_scan should stop both the syncro and
	 * background scanning and can sleep.
	 * The function start_scan should initiate the background
	 * scanning and can't sleep.
	 */
	void (*scan_syncro)(struct net_device *dev);
	void (*start_scan)(struct net_device *dev);
	void (*stop_scan)(struct net_device *dev);

	/* indicate the driver that the link state is changed
	 * for example it may indicate the card is associated now.
	 * Driver might be interested in this to apply RX filter
	 * rules or simply light the LINK led
	 */
	void (*link_change)(struct net_device *dev);

	/* these two function indicates to the HW when to start
	 * and stop to send beacons. This is used when the
	 * IEEE_SOFTMAC_BEACONS is not set. For now the
	 * stop_send_bacons is NOT guaranteed to be called only
	 * after start_send_beacons.
	 */
	void (*start_send_beacons) (struct net_device *dev);
	void (*stop_send_beacons) (struct net_device *dev);

	/* power save mode related */
	void (*sta_wake_up) (struct net_device *dev);
	void (*ps_request_tx_ack) (struct net_device *dev);
	void (*enter_sleep_state) (struct net_device *dev, u32 th, u32 tl);
	short (*ps_is_queue_empty) (struct net_device *dev);

	/* QoS related */
	//void (*wmm_param_update) (struct net_device *dev, u8 *ac_param);
	//void (*wmm_param_update) (struct ieee80211_device *ieee);

	/* This must be the last item so that it points to the data
	 * allocated beyond this structure by alloc_ieee80211 */
	u8 priv[0];
};

#define IEEE_A            (1<<0)
#define IEEE_B            (1<<1)
#define IEEE_G            (1<<2)
#define IEEE_MODE_MASK    (IEEE_A|IEEE_B|IEEE_G)

/* Generate a 802.11 header */

/* Uses the channel change callback directly
 * instead of [start/stop] scan callbacks
 */
#define IEEE_SOFTMAC_SCAN (1<<2)

/* Perform authentication and association handshake */
#define IEEE_SOFTMAC_ASSOCIATE (1<<3)

/* Generate probe requests */
#define IEEE_SOFTMAC_PROBERQ (1<<4)

/* Generate response to probe requests */
#define IEEE_SOFTMAC_PROBERS (1<<5)

/* The ieee802.11 stack will manages the netif queue
 * wake/stop for the driver, taking care of 802.11
 * fragmentation. See softmac.c for details. */
#define IEEE_SOFTMAC_TX_QUEUE (1<<7)

/* Uses only the softmac_data_hard_start_xmit
 * even for TX management frames.
 */
#define IEEE_SOFTMAC_SINGLE_QUEUE (1<<8)

/* Generate beacons.  The stack will enqueue beacons
 * to the card
 */
#define IEEE_SOFTMAC_BEACONS (1<<6)



static inline void *ieee80211_priv(struct net_device *dev)
{
	return ((struct ieee80211_device *)netdev_priv(dev))->priv;
}

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

static inline int ieee80211_is_valid_mode(struct ieee80211_device *ieee, int mode)
{
	/*
	 * It is possible for both access points and our device to support
	 * combinations of modes, so as long as there is one valid combination
	 * of ap/device supported modes, then return success
	 *
	 */
	if ((mode & IEEE_A) &&
	    (ieee->modulation & IEEE80211_OFDM_MODULATION) &&
	    (ieee->freq_band & IEEE80211_52GHZ_BAND))
		return 1;

	if ((mode & IEEE_G) &&
	    (ieee->modulation & IEEE80211_OFDM_MODULATION) &&
	    (ieee->freq_band & IEEE80211_24GHZ_BAND))
		return 1;

	if ((mode & IEEE_B) &&
	    (ieee->modulation & IEEE80211_CCK_MODULATION) &&
	    (ieee->freq_band & IEEE80211_24GHZ_BAND))
		return 1;

	return 0;
}

static inline int ieee80211_get_hdrlen(u16 fc)
{
	int hdrlen = 24;

	switch (WLAN_FC_GET_TYPE(fc)) {
	case IEEE80211_FTYPE_DATA:
		if ((fc & IEEE80211_FCTL_FROMDS) && (fc & IEEE80211_FCTL_TODS))
			hdrlen = 30; /* Addr4 */
		if(IEEE80211_QOS_HAS_SEQ(fc))
			hdrlen += 2; /* QOS ctrl*/
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



/* ieee80211.c */
extern void free_ieee80211(struct net_device *dev);
extern struct net_device *alloc_ieee80211(int sizeof_priv);

extern int ieee80211_set_encryption(struct ieee80211_device *ieee);

/* ieee80211_tx.c */

extern int ieee80211_encrypt_fragment(
	struct ieee80211_device *ieee,
	struct sk_buff *frag,
	int hdr_len);

extern int ieee80211_rtl_xmit(struct sk_buff *skb,
			  struct net_device *dev);
extern void ieee80211_txb_free(struct ieee80211_txb *);


/* ieee80211_rx.c */
extern int ieee80211_rtl_rx(struct ieee80211_device *ieee, struct sk_buff *skb,
			struct ieee80211_rx_stats *rx_stats);
extern void ieee80211_rx_mgt(struct ieee80211_device *ieee,
			     struct ieee80211_hdr_4addr *header,
			     struct ieee80211_rx_stats *stats);

/* ieee80211_wx.c */
extern int ieee80211_wx_get_scan(struct ieee80211_device *ieee,
				 struct iw_request_info *info,
				 union iwreq_data *wrqu, char *key);
extern int ieee80211_wx_set_encode(struct ieee80211_device *ieee,
				   struct iw_request_info *info,
				   union iwreq_data *wrqu, char *key);
extern int ieee80211_wx_get_encode(struct ieee80211_device *ieee,
				   struct iw_request_info *info,
				   union iwreq_data *wrqu, char *key);
extern int ieee80211_wx_set_encode_ext(struct ieee80211_device *ieee,
                            struct iw_request_info *info,
                            union iwreq_data* wrqu, char *extra);
int ieee80211_wx_set_auth(struct ieee80211_device *ieee,
                               struct iw_request_info *info,
                               struct iw_param *data, char *extra);
int ieee80211_wx_set_mlme(struct ieee80211_device *ieee,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra);

int ieee80211_wx_set_gen_ie(struct ieee80211_device *ieee, u8 *ie, size_t len);
/* ieee80211_softmac.c */
extern short ieee80211_is_54g(const struct ieee80211_network *net);
extern short ieee80211_is_shortslot(const struct ieee80211_network *net);
extern int ieee80211_rx_frame_softmac(struct ieee80211_device *ieee, struct sk_buff *skb,
			struct ieee80211_rx_stats *rx_stats, u16 type,
			u16 stype);
extern void ieee80211_softmac_new_net(struct ieee80211_device *ieee, struct ieee80211_network *net);

extern void ieee80211_softmac_xmit(struct ieee80211_txb *txb, struct ieee80211_device *ieee);
extern void ieee80211_softmac_check_all_nets(struct ieee80211_device *ieee);
extern void ieee80211_start_bss(struct ieee80211_device *ieee);
extern void ieee80211_start_master_bss(struct ieee80211_device *ieee);
extern void ieee80211_start_ibss(struct ieee80211_device *ieee);
extern void ieee80211_softmac_init(struct ieee80211_device *ieee);
extern void ieee80211_softmac_free(struct ieee80211_device *ieee);
extern void ieee80211_associate_abort(struct ieee80211_device *ieee);
extern void ieee80211_disassociate(struct ieee80211_device *ieee);
extern void ieee80211_stop_scan(struct ieee80211_device *ieee);
extern void ieee80211_start_scan_syncro(struct ieee80211_device *ieee);
extern void ieee80211_check_all_nets(struct ieee80211_device *ieee);
extern void ieee80211_start_protocol(struct ieee80211_device *ieee);
extern void ieee80211_stop_protocol(struct ieee80211_device *ieee);
extern void ieee80211_softmac_start_protocol(struct ieee80211_device *ieee);
extern void ieee80211_softmac_stop_protocol(struct ieee80211_device *ieee);
extern void ieee80211_reset_queue(struct ieee80211_device *ieee);
extern void ieee80211_rtl_wake_queue(struct ieee80211_device *ieee);
extern void ieee80211_rtl_stop_queue(struct ieee80211_device *ieee);
extern struct sk_buff *ieee80211_get_beacon(struct ieee80211_device *ieee);
extern void ieee80211_start_send_beacons(struct ieee80211_device *ieee);
extern void ieee80211_stop_send_beacons(struct ieee80211_device *ieee);
extern int ieee80211_wpa_supplicant_ioctl(struct ieee80211_device *ieee, struct iw_point *p);
extern void notify_wx_assoc_event(struct ieee80211_device *ieee);
extern void ieee80211_ps_tx_ack(struct ieee80211_device *ieee, short success);
extern void SendDisassociation(struct ieee80211_device *ieee,u8* asSta,u8 asRsn);
extern void ieee80211_rtl_start_scan(struct ieee80211_device *ieee);

//Add for RF power on power off by lizhaoming 080512
extern void SendDisassociation(struct ieee80211_device *ieee,
       			 u8*                     asSta,
        		 u8                      asRsn);

/* ieee80211_crypt_ccmp&tkip&wep.c */
extern void ieee80211_tkip_null(void);
extern void ieee80211_wep_null(void);
extern void ieee80211_ccmp_null(void);
/* ieee80211_softmac_wx.c */

extern int ieee80211_wx_get_wap(struct ieee80211_device *ieee,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *ext);

extern int ieee80211_wx_set_wap(struct ieee80211_device *ieee,
			 struct iw_request_info *info,
			 union iwreq_data *awrq,
			 char *extra);

extern int ieee80211_wx_get_essid(struct ieee80211_device *ieee, struct iw_request_info *a,union iwreq_data *wrqu,char *b);

extern int ieee80211_wx_set_rate(struct ieee80211_device *ieee,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra);

extern int ieee80211_wx_get_rate(struct ieee80211_device *ieee,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra);

extern int ieee80211_wx_set_mode(struct ieee80211_device *ieee, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *b);

extern int ieee80211_wx_set_scan(struct ieee80211_device *ieee, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *b);

extern int ieee80211_wx_set_essid(struct ieee80211_device *ieee,
			      struct iw_request_info *a,
			      union iwreq_data *wrqu, char *extra);

extern int ieee80211_wx_get_mode(struct ieee80211_device *ieee, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *b);

extern int ieee80211_wx_set_freq(struct ieee80211_device *ieee, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *b);

extern int ieee80211_wx_get_freq(struct ieee80211_device *ieee, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *b);

extern void ieee80211_wx_sync_scan_wq(struct work_struct *work);

extern int ieee80211_wx_set_rawtx(struct ieee80211_device *ieee,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra);

extern int ieee80211_wx_get_name(struct ieee80211_device *ieee,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra);

extern int ieee80211_wx_set_power(struct ieee80211_device *ieee,
				 struct iw_request_info *info,
				 union iwreq_data *wrqu, char *extra);

extern int ieee80211_wx_get_power(struct ieee80211_device *ieee,
				 struct iw_request_info *info,
				 union iwreq_data *wrqu, char *extra);

extern void ieee80211_softmac_ips_scan_syncro(struct ieee80211_device *ieee);

extern void ieee80211_sta_ps_send_null_frame(struct ieee80211_device *ieee, short pwr);

extern const long ieee80211_wlan_frequencies[];

extern inline void ieee80211_increment_scans(struct ieee80211_device *ieee)
{
	ieee->scans++;
}

extern inline int ieee80211_get_scans(struct ieee80211_device *ieee)
{
	return ieee->scans;
}

static inline const char *escape_essid(const char *essid, u8 essid_len) {
	static char escaped[IW_ESSID_MAX_SIZE * 2 + 1];
	const char *s = essid;
	char *d = escaped;

	if (ieee80211_is_empty_essid(essid, essid_len)) {
		memcpy(escaped, "<hidden>", sizeof("<hidden>"));
		return escaped;
	}

	essid_len = min(essid_len, (u8)IW_ESSID_MAX_SIZE);
	while (essid_len--) {
		if (*s == '\0') {
			*d++ = '\\';
			*d++ = '0';
			s++;
		} else {
			*d++ = *s++;
		}
	}
	*d = '\0';
	return escaped;
}
#endif /* IEEE80211_H */
