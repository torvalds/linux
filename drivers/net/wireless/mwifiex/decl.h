/*
 * Marvell Wireless LAN device driver: generic data structures and APIs
 *
 * Copyright (C) 2011, Marvell International Ltd.
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
#include <net/mac80211.h>


#define MWIFIEX_MAX_BSS_NUM         (3)

#define MWIFIEX_MIN_DATA_HEADER_LEN 36	/* sizeof(mwifiex_txpd)
					 *   + 4 byte alignment
					 */
#define MWIFIEX_MGMT_FRAME_HEADER_SIZE	8	/* sizeof(pkt_type)
						 *   + sizeof(tx_control)
						 */

#define MWIFIEX_MAX_TX_BASTREAM_SUPPORTED	2
#define MWIFIEX_MAX_RX_BASTREAM_SUPPORTED	16

#define MWIFIEX_STA_AMPDU_DEF_TXWINSIZE        16
#define MWIFIEX_STA_AMPDU_DEF_RXWINSIZE        32
#define MWIFIEX_UAP_AMPDU_DEF_TXWINSIZE        32
#define MWIFIEX_UAP_AMPDU_DEF_RXWINSIZE        16
#define MWIFIEX_11AC_STA_AMPDU_DEF_TXWINSIZE   32
#define MWIFIEX_11AC_STA_AMPDU_DEF_RXWINSIZE   48
#define MWIFIEX_11AC_UAP_AMPDU_DEF_TXWINSIZE   48
#define MWIFIEX_11AC_UAP_AMPDU_DEF_RXWINSIZE   32

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

#define MWIFIEX_BRIDGED_PKTS_THRESHOLD     1024

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
#endif /* !_MWIFIEX_DECL_H_ */
