/*
 * Marvell Wireless LAN device driver: generic data structures and APIs
 *
 * Copyright (C) 2011-2014, Marvell International Ltd.
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
 */

#ifndef _MWIFIEX_DECL_H_
#define _MWIFIEX_DECL_H_

#undef pr_fmt
#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/wait.h>
#include <linux/timer.h>
#include <linux/ieee80211.h>
#include <uapi/linux/if_arp.h>
#include <net/mac80211.h>


#define MWIFIEX_MAX_BSS_NUM         (3)

#define MWIFIEX_DMA_ALIGN_SZ	    64
#define MWIFIEX_RX_HEADROOM	    64
#define MAX_TXPD_SZ		    32
#define INTF_HDR_ALIGN		     4

#define MWIFIEX_MIN_DATA_HEADER_LEN (MWIFIEX_DMA_ALIGN_SZ + INTF_HDR_ALIGN + \
				     MAX_TXPD_SZ)
#define MWIFIEX_MGMT_FRAME_HEADER_SIZE	8	/* sizeof(pkt_type)
						 *   + sizeof(tx_control)
						 */

#define MWIFIEX_MAX_TX_BASTREAM_SUPPORTED	2
#define MWIFIEX_MAX_RX_BASTREAM_SUPPORTED	16
#define MWIFIEX_MAX_TDLS_PEER_SUPPORTED 8

#define MWIFIEX_STA_AMPDU_DEF_TXWINSIZE        64
#define MWIFIEX_STA_AMPDU_DEF_RXWINSIZE        64
#define MWIFIEX_UAP_AMPDU_DEF_TXWINSIZE        32
#define MWIFIEX_UAP_AMPDU_DEF_RXWINSIZE        16
#define MWIFIEX_11AC_STA_AMPDU_DEF_TXWINSIZE   64
#define MWIFIEX_11AC_STA_AMPDU_DEF_RXWINSIZE   64
#define MWIFIEX_11AC_UAP_AMPDU_DEF_TXWINSIZE   64
#define MWIFIEX_11AC_UAP_AMPDU_DEF_RXWINSIZE   64

#define MWIFIEX_DEFAULT_BLOCK_ACK_TIMEOUT  0xffff

#define MWIFIEX_RATE_BITMAP_MCS0   32

#define MWIFIEX_RX_DATA_BUF_SIZE     (4 * 1024)
#define MWIFIEX_RX_CMD_BUF_SIZE	     (2 * 1024)

#define MAX_BEACON_PERIOD                  (4000)
#define MIN_BEACON_PERIOD                  (50)
#define MAX_DTIM_PERIOD                    (100)
#define MIN_DTIM_PERIOD                    (1)

#define MWIFIEX_RTS_MIN_VALUE              (0)
#define MWIFIEX_RTS_MAX_VALUE              (2347)
#define MWIFIEX_FRAG_MIN_VALUE             (256)
#define MWIFIEX_FRAG_MAX_VALUE             (2346)
#define MWIFIEX_WMM_VERSION                0x01
#define MWIFIEX_WMM_SUBTYPE                0x01

#define MWIFIEX_RETRY_LIMIT                14
#define MWIFIEX_SDIO_BLOCK_SIZE            256

#define MWIFIEX_BUF_FLAG_REQUEUED_PKT      BIT(0)
#define MWIFIEX_BUF_FLAG_BRIDGED_PKT	   BIT(1)
#define MWIFIEX_BUF_FLAG_TDLS_PKT	   BIT(2)
#define MWIFIEX_BUF_FLAG_EAPOL_TX_STATUS   BIT(3)
#define MWIFIEX_BUF_FLAG_ACTION_TX_STATUS  BIT(4)

#define MWIFIEX_BRIDGED_PKTS_THR_HIGH      1024
#define MWIFIEX_BRIDGED_PKTS_THR_LOW        128

#define MWIFIEX_TDLS_DISABLE_LINK             0x00
#define MWIFIEX_TDLS_ENABLE_LINK              0x01
#define MWIFIEX_TDLS_CREATE_LINK              0x02
#define MWIFIEX_TDLS_CONFIG_LINK              0x03

#define MWIFIEX_TDLS_RSSI_HIGH		50
#define MWIFIEX_TDLS_RSSI_LOW		55
#define MWIFIEX_TDLS_MAX_FAIL_COUNT      4
#define MWIFIEX_AUTO_TDLS_IDLE_TIME     10

/* 54M rates, index from 0 to 11 */
#define MWIFIEX_RATE_INDEX_MCS0 12
/* 12-27=MCS0-15(BW20) */
#define MWIFIEX_BW20_MCS_NUM 15

/* Rate index for OFDM 0 */
#define MWIFIEX_RATE_INDEX_OFDM0   4

#define MWIFIEX_MAX_STA_NUM		1
#define MWIFIEX_MAX_UAP_NUM		1
#define MWIFIEX_MAX_P2P_NUM		1

#define MWIFIEX_A_BAND_START_FREQ	5000

enum mwifiex_bss_type {
	MWIFIEX_BSS_TYPE_STA = 0,
	MWIFIEX_BSS_TYPE_UAP = 1,
	MWIFIEX_BSS_TYPE_P2P = 2,
	MWIFIEX_BSS_TYPE_ANY = 0xff,
};

enum mwifiex_bss_role {
	MWIFIEX_BSS_ROLE_STA = 0,
	MWIFIEX_BSS_ROLE_UAP = 1,
	MWIFIEX_BSS_ROLE_ANY = 0xff,
};

enum mwifiex_tdls_status {
	TDLS_NOT_SETUP = 0,
	TDLS_SETUP_INPROGRESS,
	TDLS_SETUP_COMPLETE,
	TDLS_SETUP_FAILURE,
	TDLS_LINK_TEARDOWN,
};

enum mwifiex_tdls_error_code {
	TDLS_ERR_NO_ERROR = 0,
	TDLS_ERR_INTERNAL_ERROR,
	TDLS_ERR_MAX_LINKS_EST,
	TDLS_ERR_LINK_EXISTS,
	TDLS_ERR_LINK_NONEXISTENT,
	TDLS_ERR_PEER_STA_UNREACHABLE = 25,
};

#define BSS_ROLE_BIT_MASK    BIT(0)

#define GET_BSS_ROLE(priv)   ((priv)->bss_role & BSS_ROLE_BIT_MASK)

enum mwifiex_data_frame_type {
	MWIFIEX_DATA_FRAME_TYPE_ETH_II = 0,
	MWIFIEX_DATA_FRAME_TYPE_802_11,
};

struct mwifiex_fw_image {
	u8 *helper_buf;
	u32 helper_len;
	u8 *fw_buf;
	u32 fw_len;
};

struct mwifiex_802_11_ssid {
	u32 ssid_len;
	u8 ssid[IEEE80211_MAX_SSID_LEN];
};

struct mwifiex_wait_queue {
	wait_queue_head_t wait;
	int status;
};

struct mwifiex_rxinfo {
	u8 bss_num;
	u8 bss_type;
	struct sk_buff *parent;
	u8 use_count;
};

struct mwifiex_txinfo {
	u32 status_code;
	u8 flags;
	u8 bss_num;
	u8 bss_type;
	u32 pkt_len;
	u8 ack_frame_id;
	u64 cookie;
};

enum mwifiex_wmm_ac_e {
	WMM_AC_BK,
	WMM_AC_BE,
	WMM_AC_VI,
	WMM_AC_VO
} __packed;

struct ieee_types_wmm_ac_parameters {
	u8 aci_aifsn_bitmap;
	u8 ecw_bitmap;
	__le16 tx_op_limit;
} __packed;

struct mwifiex_types_wmm_info {
	u8 oui[4];
	u8 subtype;
	u8 version;
	u8 qos_info;
	u8 reserved;
	struct ieee_types_wmm_ac_parameters ac_params[IEEE80211_NUM_ACS];
} __packed;

struct mwifiex_arp_eth_header {
	struct arphdr hdr;
	u8 ar_sha[ETH_ALEN];
	u8 ar_sip[4];
	u8 ar_tha[ETH_ALEN];
	u8 ar_tip[4];
} __packed;

struct mwifiex_chan_stats {
	u8 chan_num;
	u8 bandcfg;
	u8 flags;
	s8 noise;
	u16 total_bss;
	u16 cca_scan_dur;
	u16 cca_busy_dur;
} __packed;

#define MWIFIEX_HIST_MAX_SAMPLES	1048576
#define MWIFIEX_MAX_RX_RATES		     44
#define MWIFIEX_MAX_AC_RX_RATES		     74
#define MWIFIEX_MAX_SNR			    256
#define MWIFIEX_MAX_NOISE_FLR		    256
#define MWIFIEX_MAX_SIG_STRENGTH	    256

struct mwifiex_histogram_data {
	atomic_t rx_rate[MWIFIEX_MAX_AC_RX_RATES];
	atomic_t snr[MWIFIEX_MAX_SNR];
	atomic_t noise_flr[MWIFIEX_MAX_NOISE_FLR];
	atomic_t sig_str[MWIFIEX_MAX_SIG_STRENGTH];
	atomic_t num_samples;
};

struct mwifiex_iface_comb {
	u8 sta_intf;
	u8 uap_intf;
	u8 p2p_intf;
};

struct mwifiex_radar_params {
	struct cfg80211_chan_def *chandef;
	u32 cac_time_ms;
} __packed;

struct mwifiex_11h_intf_state {
	bool is_11h_enabled;
	bool is_11h_active;
} __packed;
#endif /* !_MWIFIEX_DECL_H_ */
