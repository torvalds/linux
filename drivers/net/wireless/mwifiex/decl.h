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


#define MWIFIEX_MAX_BSS_NUM         (2)

#define MWIFIEX_MIN_DATA_HEADER_LEN 36	/* sizeof(mwifiex_txpd)
					 *   + 4 byte alignment
					 */

#define MWIFIEX_MAX_TX_BASTREAM_SUPPORTED	2
#define MWIFIEX_MAX_RX_BASTREAM_SUPPORTED	16

#define MWIFIEX_AMPDU_DEF_TXWINSIZE        32
#define MWIFIEX_AMPDU_DEF_RXWINSIZE        16
#define MWIFIEX_DEFAULT_BLOCK_ACK_TIMEOUT  0xffff

#define MWIFIEX_RATE_INDEX_HRDSSS0 0
#define MWIFIEX_RATE_INDEX_HRDSSS3 3
#define MWIFIEX_RATE_INDEX_OFDM0   4
#define MWIFIEX_RATE_INDEX_OFDM7   11
#define MWIFIEX_RATE_INDEX_MCS0    12

#define MWIFIEX_RATE_BITMAP_OFDM0  16
#define MWIFIEX_RATE_BITMAP_OFDM7  23
#define MWIFIEX_RATE_BITMAP_MCS0   32
#define MWIFIEX_RATE_BITMAP_MCS127 159

#define MWIFIEX_RX_DATA_BUF_SIZE     (4 * 1024)
#define MWIFIEX_RX_CMD_BUF_SIZE	     (2 * 1024)

#define MWIFIEX_RTS_MIN_VALUE              (0)
#define MWIFIEX_RTS_MAX_VALUE              (2347)
#define MWIFIEX_FRAG_MIN_VALUE             (256)
#define MWIFIEX_FRAG_MAX_VALUE             (2346)

#define MWIFIEX_RETRY_LIMIT                14
#define MWIFIEX_SDIO_BLOCK_SIZE            256

#define MWIFIEX_BUF_FLAG_REQUEUED_PKT      BIT(0)

enum mwifiex_bss_type {
	MWIFIEX_BSS_TYPE_STA = 0,
	MWIFIEX_BSS_TYPE_UAP = 1,
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
#endif /* !_MWIFIEX_DECL_H_ */
