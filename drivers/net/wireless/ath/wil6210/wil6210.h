/*
 * Copyright (c) 2012-2017 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __WIL6210_H__
#define __WIL6210_H__

#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/wireless.h>
#include <net/cfg80211.h>
#include <linux/timex.h>
#include <linux/types.h>
#include "wmi.h"
#include "wil_platform.h"

extern bool no_fw_recovery;
extern unsigned int mtu_max;
extern unsigned short rx_ring_overflow_thrsh;
extern int agg_wsize;
extern bool rx_align_2;
extern bool rx_large_buf;
extern bool debug_fw;
extern bool disable_ap_sme;

#define WIL_NAME "wil6210"

#define WIL_FW_NAME_DEFAULT "wil6210.fw"
#define WIL_FW_NAME_FTM_DEFAULT "wil6210_ftm.fw"

#define WIL_FW_NAME_SPARROW_PLUS "wil6210_sparrow_plus.fw"
#define WIL_FW_NAME_FTM_SPARROW_PLUS "wil6210_sparrow_plus_ftm.fw"

#define WIL_BOARD_FILE_NAME "wil6210.brd" /* board & radio parameters */

#define WIL_DEFAULT_BUS_REQUEST_KBPS 128000 /* ~1Gbps */
#define WIL_MAX_BUS_REQUEST_KBPS 800000 /* ~6.1Gbps */

/**
 * extract bits [@b0:@b1] (inclusive) from the value @x
 * it should be @b0 <= @b1, or result is incorrect
 */
static inline u32 WIL_GET_BITS(u32 x, int b0, int b1)
{
	return (x >> b0) & ((1 << (b1 - b0 + 1)) - 1);
}

#define WIL6210_MIN_MEM_SIZE (2 * 1024 * 1024UL)
#define WIL6210_MAX_MEM_SIZE (4 * 1024 * 1024UL)

#define WIL_TX_Q_LEN_DEFAULT		(4000)
#define WIL_RX_RING_SIZE_ORDER_DEFAULT	(10)
#define WIL_TX_RING_SIZE_ORDER_DEFAULT	(12)
#define WIL_BCAST_RING_SIZE_ORDER_DEFAULT	(7)
#define WIL_BCAST_MCS0_LIMIT		(1024) /* limit for MCS0 frame size */
/* limit ring size in range [32..32k] */
#define WIL_RING_SIZE_ORDER_MIN	(5)
#define WIL_RING_SIZE_ORDER_MAX	(15)
#define WIL6210_MAX_TX_RINGS	(24) /* HW limit */
#define WIL6210_MAX_CID		(8) /* HW limit */
#define WIL6210_NAPI_BUDGET	(16) /* arbitrary */
#define WIL_MAX_AMPDU_SIZE	(64 * 1024) /* FW/HW limit */
#define WIL_MAX_AGG_WSIZE	(32) /* FW/HW limit */
/* Hardware offload block adds the following:
 * 26 bytes - 3-address QoS data header
 *  8 bytes - IV + EIV (for GCMP)
 *  8 bytes - SNAP
 * 16 bytes - MIC (for GCMP)
 *  4 bytes - CRC
 */
#define WIL_MAX_MPDU_OVERHEAD	(62)

struct wil_suspend_stats {
	unsigned long successful_suspends;
	unsigned long failed_suspends;
	unsigned long successful_resumes;
	unsigned long failed_resumes;
	unsigned long rejected_by_device;
	unsigned long rejected_by_host;
	unsigned long long total_suspend_time;
	unsigned long long min_suspend_time;
	unsigned long long max_suspend_time;
	ktime_t collection_start;
	ktime_t suspend_start_time;
};

/* Calculate MAC buffer size for the firmware. It includes all overhead,
 * as it will go over the air, and need to be 8 byte aligned
 */
static inline u32 wil_mtu2macbuf(u32 mtu)
{
	return ALIGN(mtu + WIL_MAX_MPDU_OVERHEAD, 8);
}

/* MTU for Ethernet need to take into account 8-byte SNAP header
 * to be added when encapsulating Ethernet frame into 802.11
 */
#define WIL_MAX_ETH_MTU		(IEEE80211_MAX_DATA_LEN_DMG - 8)
/* Max supported by wil6210 value for interrupt threshold is 5sec. */
#define WIL6210_ITR_TRSH_MAX (5000000)
#define WIL6210_ITR_TX_INTERFRAME_TIMEOUT_DEFAULT (13) /* usec */
#define WIL6210_ITR_RX_INTERFRAME_TIMEOUT_DEFAULT (13) /* usec */
#define WIL6210_ITR_TX_MAX_BURST_DURATION_DEFAULT (500) /* usec */
#define WIL6210_ITR_RX_MAX_BURST_DURATION_DEFAULT (500) /* usec */
#define WIL6210_FW_RECOVERY_RETRIES	(5) /* try to recover this many times */
#define WIL6210_FW_RECOVERY_TO	msecs_to_jiffies(5000)
#define WIL6210_SCAN_TO		msecs_to_jiffies(10000)
#define WIL6210_DISCONNECT_TO_MS (2000)
#define WIL6210_RX_HIGH_TRSH_INIT		(0)
#define WIL6210_RX_HIGH_TRSH_DEFAULT \
				(1 << (WIL_RX_RING_SIZE_ORDER_DEFAULT - 3))
#define WIL_MAX_DMG_AID 254 /* for DMG only 1-254 allowed (see
			     * 802.11REVmc/D5.0, section 9.4.1.8)
			     */
/* Hardware definitions begin */

/*
 * Mapping
 * RGF File      | Host addr    |  FW addr
 *               |              |
 * user_rgf      | 0x000000     | 0x880000
 *  dma_rgf      | 0x001000     | 0x881000
 * pcie_rgf      | 0x002000     | 0x882000
 *               |              |
 */

/* Where various structures placed in host address space */
#define WIL6210_FW_HOST_OFF      (0x880000UL)

#define HOSTADDR(fwaddr)        (fwaddr - WIL6210_FW_HOST_OFF)

/*
 * Interrupt control registers block
 *
 * each interrupt controlled by the same bit in all registers
 */
struct RGF_ICR {
	u32 ICC; /* Cause Control, RW: 0 - W1C, 1 - COR */
	u32 ICR; /* Cause, W1C/COR depending on ICC */
	u32 ICM; /* Cause masked (ICR & ~IMV), W1C/COR depending on ICC */
	u32 ICS; /* Cause Set, WO */
	u32 IMV; /* Mask, RW+S/C */
	u32 IMS; /* Mask Set, write 1 to set */
	u32 IMC; /* Mask Clear, write 1 to clear */
} __packed;

/* registers - FW addresses */
#define RGF_USER_USAGE_1		(0x880004)
#define RGF_USER_USAGE_6		(0x880018)
	#define BIT_USER_OOB_MODE		BIT(31)
	#define BIT_USER_OOB_R2_MODE		BIT(30)
#define RGF_USER_HW_MACHINE_STATE	(0x8801dc)
	#define HW_MACHINE_BOOT_DONE	(0x3fffffd)
#define RGF_USER_USER_CPU_0		(0x8801e0)
	#define BIT_USER_USER_CPU_MAN_RST	BIT(1) /* user_cpu_man_rst */
#define RGF_USER_MAC_CPU_0		(0x8801fc)
	#define BIT_USER_MAC_CPU_MAN_RST	BIT(1) /* mac_cpu_man_rst */
#define RGF_USER_USER_SCRATCH_PAD	(0x8802bc)
#define RGF_USER_BL			(0x880A3C) /* Boot Loader */
#define RGF_USER_FW_REV_ID		(0x880a8c) /* chip revision */
#define RGF_USER_FW_CALIB_RESULT	(0x880a90) /* b0-7:result
						    * b8-15:signature
						    */
	#define CALIB_RESULT_SIGNATURE	(0x11)
#define RGF_USER_CLKS_CTL_0		(0x880abc)
	#define BIT_USER_CLKS_CAR_AHB_SW_SEL	BIT(1) /* ref clk/PLL */
	#define BIT_USER_CLKS_RST_PWGD	BIT(11) /* reset on "power good" */
#define RGF_USER_CLKS_CTL_SW_RST_VEC_0	(0x880b04)
#define RGF_USER_CLKS_CTL_SW_RST_VEC_1	(0x880b08)
#define RGF_USER_CLKS_CTL_SW_RST_VEC_2	(0x880b0c)
#define RGF_USER_CLKS_CTL_SW_RST_VEC_3	(0x880b10)
#define RGF_USER_CLKS_CTL_SW_RST_MASK_0	(0x880b14)
	#define BIT_HPAL_PERST_FROM_PAD	BIT(6)
	#define BIT_CAR_PERST_RST	BIT(7)
#define RGF_USER_USER_ICR		(0x880b4c) /* struct RGF_ICR */
	#define BIT_USER_USER_ICR_SW_INT_2	BIT(18)
#define RGF_USER_CLKS_CTL_EXT_SW_RST_VEC_0	(0x880c18)
#define RGF_USER_CLKS_CTL_EXT_SW_RST_VEC_1	(0x880c2c)
#define RGF_USER_SPARROW_M_4			(0x880c50) /* Sparrow */
	#define BIT_SPARROW_M_4_SEL_SLEEP_OR_REF	BIT(2)

#define RGF_DMA_EP_TX_ICR		(0x881bb4) /* struct RGF_ICR */
	#define BIT_DMA_EP_TX_ICR_TX_DONE	BIT(0)
	#define BIT_DMA_EP_TX_ICR_TX_DONE_N(n)	BIT(n+1) /* n = [0..23] */
#define RGF_DMA_EP_RX_ICR		(0x881bd0) /* struct RGF_ICR */
	#define BIT_DMA_EP_RX_ICR_RX_DONE	BIT(0)
	#define BIT_DMA_EP_RX_ICR_RX_HTRSH	BIT(1)
#define RGF_DMA_EP_MISC_ICR		(0x881bec) /* struct RGF_ICR */
	#define BIT_DMA_EP_MISC_ICR_RX_HTRSH	BIT(0)
	#define BIT_DMA_EP_MISC_ICR_TX_NO_ACT	BIT(1)
	#define BIT_DMA_EP_MISC_ICR_HALP	BIT(27)
	#define BIT_DMA_EP_MISC_ICR_FW_INT(n)	BIT(28+n) /* n = [0..3] */

/* Legacy interrupt moderation control (before Sparrow v2)*/
#define RGF_DMA_ITR_CNT_TRSH		(0x881c5c)
#define RGF_DMA_ITR_CNT_DATA		(0x881c60)
#define RGF_DMA_ITR_CNT_CRL		(0x881c64)
	#define BIT_DMA_ITR_CNT_CRL_EN		BIT(0)
	#define BIT_DMA_ITR_CNT_CRL_EXT_TICK	BIT(1)
	#define BIT_DMA_ITR_CNT_CRL_FOREVER	BIT(2)
	#define BIT_DMA_ITR_CNT_CRL_CLR		BIT(3)
	#define BIT_DMA_ITR_CNT_CRL_REACH_TRSH	BIT(4)

/* Offload control (Sparrow B0+) */
#define RGF_DMA_OFUL_NID_0		(0x881cd4)
	#define BIT_DMA_OFUL_NID_0_RX_EXT_TR_EN		BIT(0)
	#define BIT_DMA_OFUL_NID_0_TX_EXT_TR_EN		BIT(1)
	#define BIT_DMA_OFUL_NID_0_RX_EXT_A3_SRC	BIT(2)
	#define BIT_DMA_OFUL_NID_0_TX_EXT_A3_SRC	BIT(3)

/* New (sparrow v2+) interrupt moderation control */
#define RGF_DMA_ITR_TX_DESQ_NO_MOD		(0x881d40)
#define RGF_DMA_ITR_TX_CNT_TRSH			(0x881d34)
#define RGF_DMA_ITR_TX_CNT_DATA			(0x881d38)
#define RGF_DMA_ITR_TX_CNT_CTL			(0x881d3c)
	#define BIT_DMA_ITR_TX_CNT_CTL_EN		BIT(0)
	#define BIT_DMA_ITR_TX_CNT_CTL_EXT_TIC_SEL	BIT(1)
	#define BIT_DMA_ITR_TX_CNT_CTL_FOREVER		BIT(2)
	#define BIT_DMA_ITR_TX_CNT_CTL_CLR		BIT(3)
	#define BIT_DMA_ITR_TX_CNT_CTL_REACHED_TRESH	BIT(4)
	#define BIT_DMA_ITR_TX_CNT_CTL_CROSS_EN		BIT(5)
	#define BIT_DMA_ITR_TX_CNT_CTL_FREE_RUNNIG	BIT(6)
#define RGF_DMA_ITR_TX_IDL_CNT_TRSH			(0x881d60)
#define RGF_DMA_ITR_TX_IDL_CNT_DATA			(0x881d64)
#define RGF_DMA_ITR_TX_IDL_CNT_CTL			(0x881d68)
	#define BIT_DMA_ITR_TX_IDL_CNT_CTL_EN			BIT(0)
	#define BIT_DMA_ITR_TX_IDL_CNT_CTL_EXT_TIC_SEL		BIT(1)
	#define BIT_DMA_ITR_TX_IDL_CNT_CTL_FOREVER		BIT(2)
	#define BIT_DMA_ITR_TX_IDL_CNT_CTL_CLR			BIT(3)
	#define BIT_DMA_ITR_TX_IDL_CNT_CTL_REACHED_TRESH	BIT(4)
#define RGF_DMA_ITR_RX_DESQ_NO_MOD		(0x881d50)
#define RGF_DMA_ITR_RX_CNT_TRSH			(0x881d44)
#define RGF_DMA_ITR_RX_CNT_DATA			(0x881d48)
#define RGF_DMA_ITR_RX_CNT_CTL			(0x881d4c)
	#define BIT_DMA_ITR_RX_CNT_CTL_EN		BIT(0)
	#define BIT_DMA_ITR_RX_CNT_CTL_EXT_TIC_SEL	BIT(1)
	#define BIT_DMA_ITR_RX_CNT_CTL_FOREVER		BIT(2)
	#define BIT_DMA_ITR_RX_CNT_CTL_CLR		BIT(3)
	#define BIT_DMA_ITR_RX_CNT_CTL_REACHED_TRESH	BIT(4)
	#define BIT_DMA_ITR_RX_CNT_CTL_CROSS_EN		BIT(5)
	#define BIT_DMA_ITR_RX_CNT_CTL_FREE_RUNNIG	BIT(6)
#define RGF_DMA_ITR_RX_IDL_CNT_TRSH			(0x881d54)
#define RGF_DMA_ITR_RX_IDL_CNT_DATA			(0x881d58)
#define RGF_DMA_ITR_RX_IDL_CNT_CTL			(0x881d5c)
	#define BIT_DMA_ITR_RX_IDL_CNT_CTL_EN			BIT(0)
	#define BIT_DMA_ITR_RX_IDL_CNT_CTL_EXT_TIC_SEL		BIT(1)
	#define BIT_DMA_ITR_RX_IDL_CNT_CTL_FOREVER		BIT(2)
	#define BIT_DMA_ITR_RX_IDL_CNT_CTL_CLR			BIT(3)
	#define BIT_DMA_ITR_RX_IDL_CNT_CTL_REACHED_TRESH	BIT(4)

#define RGF_DMA_PSEUDO_CAUSE		(0x881c68)
#define RGF_DMA_PSEUDO_CAUSE_MASK_SW	(0x881c6c)
#define RGF_DMA_PSEUDO_CAUSE_MASK_FW	(0x881c70)
	#define BIT_DMA_PSEUDO_CAUSE_RX		BIT(0)
	#define BIT_DMA_PSEUDO_CAUSE_TX		BIT(1)
	#define BIT_DMA_PSEUDO_CAUSE_MISC	BIT(2)

#define RGF_HP_CTRL			(0x88265c)
#define RGF_PAL_UNIT_ICR		(0x88266c) /* struct RGF_ICR */
#define RGF_PCIE_LOS_COUNTER_CTL	(0x882dc4)

/* MAC timer, usec, for packet lifetime */
#define RGF_MAC_MTRL_COUNTER_0		(0x886aa8)

#define RGF_CAF_ICR			(0x88946c) /* struct RGF_ICR */
#define RGF_CAF_OSC_CONTROL		(0x88afa4)
	#define BIT_CAF_OSC_XTAL_EN		BIT(0)
#define RGF_CAF_PLL_LOCK_STATUS		(0x88afec)
	#define BIT_CAF_OSC_DIG_XTAL_STABLE	BIT(0)

#define RGF_USER_JTAG_DEV_ID	(0x880b34) /* device ID */
	#define JTAG_DEV_ID_SPARROW	(0x2632072f)

#define RGF_USER_REVISION_ID		(0x88afe4)
#define RGF_USER_REVISION_ID_MASK	(3)
	#define REVISION_ID_SPARROW_B0	(0x0)
	#define REVISION_ID_SPARROW_D0	(0x3)

/* crash codes for FW/Ucode stored here */
#define RGF_FW_ASSERT_CODE		(0x91f020)
#define RGF_UCODE_ASSERT_CODE		(0x91f028)

enum {
	HW_VER_UNKNOWN,
	HW_VER_SPARROW_B0, /* REVISION_ID_SPARROW_B0 */
	HW_VER_SPARROW_D0, /* REVISION_ID_SPARROW_D0 */
};

/* popular locations */
#define RGF_MBOX   RGF_USER_USER_SCRATCH_PAD
#define HOST_MBOX   HOSTADDR(RGF_MBOX)
#define SW_INT_MBOX BIT_USER_USER_ICR_SW_INT_2

/* ISR register bits */
#define ISR_MISC_FW_READY	BIT_DMA_EP_MISC_ICR_FW_INT(0)
#define ISR_MISC_MBOX_EVT	BIT_DMA_EP_MISC_ICR_FW_INT(1)
#define ISR_MISC_FW_ERROR	BIT_DMA_EP_MISC_ICR_FW_INT(3)

#define WIL_DATA_COMPLETION_TO_MS 200

/* Hardware definitions end */
struct fw_map {
	u32 from; /* linker address - from, inclusive */
	u32 to;   /* linker address - to, exclusive */
	u32 host; /* PCI/Host address - BAR0 + 0x880000 */
	const char *name; /* for debugfs */
	bool fw; /* true if FW mapping, false if UCODE mapping */
};

/* array size should be in sync with actual definition in the wmi.c */
extern const struct fw_map fw_mapping[10];

/**
 * mk_cidxtid - construct @cidxtid field
 * @cid: CID value
 * @tid: TID value
 *
 * @cidxtid field encoded as bits 0..3 - CID; 4..7 - TID
 */
static inline u8 mk_cidxtid(u8 cid, u8 tid)
{
	return ((tid & 0xf) << 4) | (cid & 0xf);
}

/**
 * parse_cidxtid - parse @cidxtid field
 * @cid: store CID value here
 * @tid: store TID value here
 *
 * @cidxtid field encoded as bits 0..3 - CID; 4..7 - TID
 */
static inline void parse_cidxtid(u8 cidxtid, u8 *cid, u8 *tid)
{
	*cid = cidxtid & 0xf;
	*tid = (cidxtid >> 4) & 0xf;
}

struct wil6210_mbox_ring {
	u32 base;
	u16 entry_size; /* max. size of mbox entry, incl. all headers */
	u16 size;
	u32 tail;
	u32 head;
} __packed;

struct wil6210_mbox_ring_desc {
	__le32 sync;
	__le32 addr;
} __packed;

/* at HOST_OFF_WIL6210_MBOX_CTL */
struct wil6210_mbox_ctl {
	struct wil6210_mbox_ring tx;
	struct wil6210_mbox_ring rx;
} __packed;

struct wil6210_mbox_hdr {
	__le16 seq;
	__le16 len; /* payload, bytes after this header */
	__le16 type;
	u8 flags;
	u8 reserved;
} __packed;

#define WIL_MBOX_HDR_TYPE_WMI (0)

/* max. value for wil6210_mbox_hdr.len */
#define MAX_MBOXITEM_SIZE   (240)

struct pending_wmi_event {
	struct list_head list;
	struct {
		struct wil6210_mbox_hdr hdr;
		struct wmi_cmd_hdr wmi;
		u8 data[0];
	} __packed event;
};

enum { /* for wil_ctx.mapped_as */
	wil_mapped_as_none = 0,
	wil_mapped_as_single = 1,
	wil_mapped_as_page = 2,
};

/**
 * struct wil_ctx - software context for Vring descriptor
 */
struct wil_ctx {
	struct sk_buff *skb;
	u8 nr_frags;
	u8 mapped_as;
};

union vring_desc;

struct vring {
	dma_addr_t pa;
	volatile union vring_desc *va; /* vring_desc[size], WriteBack by DMA */
	u16 size; /* number of vring_desc elements */
	u32 swtail;
	u32 swhead;
	u32 hwtail; /* write here to inform hw */
	struct wil_ctx *ctx; /* ctx[size] - software context */
};

/**
 * Additional data for Tx Vring
 */
struct vring_tx_data {
	bool dot1x_open;
	int enabled;
	cycles_t idle, last_idle, begin;
	u8 agg_wsize; /* agreed aggregation window, 0 - no agg */
	u16 agg_timeout;
	u8 agg_amsdu;
	bool addba_in_progress; /* if set, agg_xxx is for request in progress */
	spinlock_t lock;
};

enum { /* for wil6210_priv.status */
	wil_status_fwready = 0, /* FW operational */
	wil_status_fwconnecting,
	wil_status_fwconnected,
	wil_status_dontscan,
	wil_status_mbox_ready, /* MBOX structures ready */
	wil_status_irqen, /* FIXME: interrupts enabled - for debug */
	wil_status_napi_en, /* NAPI enabled protected by wil->mutex */
	wil_status_resetting, /* reset in progress */
	wil_status_suspending, /* suspend in progress */
	wil_status_suspended, /* suspend completed, device is suspended */
	wil_status_resuming, /* resume in progress */
	wil_status_last /* keep last */
};

struct pci_dev;

/**
 * struct tid_ampdu_rx - TID aggregation information (Rx).
 *
 * @reorder_buf: buffer to reorder incoming aggregated MPDUs
 * @reorder_time: jiffies when skb was added
 * @session_timer: check if peer keeps Tx-ing on the TID (by timeout value)
 * @reorder_timer: releases expired frames from the reorder buffer.
 * @last_rx: jiffies of last rx activity
 * @head_seq_num: head sequence number in reordering buffer.
 * @stored_mpdu_num: number of MPDUs in reordering buffer
 * @ssn: Starting Sequence Number expected to be aggregated.
 * @buf_size: buffer size for incoming A-MPDUs
 * @timeout: reset timer value (in TUs).
 * @ssn_last_drop: SSN of the last dropped frame
 * @total: total number of processed incoming frames
 * @drop_dup: duplicate frames dropped for this reorder buffer
 * @drop_old: old frames dropped for this reorder buffer
 * @dialog_token: dialog token for aggregation session
 * @first_time: true when this buffer used 1-st time
 */
struct wil_tid_ampdu_rx {
	struct sk_buff **reorder_buf;
	unsigned long *reorder_time;
	struct timer_list session_timer;
	struct timer_list reorder_timer;
	unsigned long last_rx;
	u16 head_seq_num;
	u16 stored_mpdu_num;
	u16 ssn;
	u16 buf_size;
	u16 timeout;
	u16 ssn_last_drop;
	unsigned long long total; /* frames processed */
	unsigned long long drop_dup;
	unsigned long long drop_old;
	u8 dialog_token;
	bool first_time; /* is it 1-st time this buffer used? */
};

/**
 * struct wil_tid_crypto_rx_single - TID crypto information (Rx).
 *
 * @pn: GCMP PN for the session
 * @key_set: valid key present
 */
struct wil_tid_crypto_rx_single {
	u8 pn[IEEE80211_GCMP_PN_LEN];
	bool key_set;
};

struct wil_tid_crypto_rx {
	struct wil_tid_crypto_rx_single key_id[4];
};

struct wil_p2p_info {
	struct ieee80211_channel listen_chan;
	u8 discovery_started;
	u8 p2p_dev_started;
	u64 cookie;
	struct wireless_dev *pending_listen_wdev;
	unsigned int listen_duration;
	struct timer_list discovery_timer; /* listen/search duration */
	struct work_struct discovery_expired_work; /* listen/search expire */
	struct work_struct delayed_listen_work; /* listen after scan done */
};

enum wil_sta_status {
	wil_sta_unused = 0,
	wil_sta_conn_pending = 1,
	wil_sta_connected = 2,
};

#define WIL_STA_TID_NUM (16)
#define WIL_MCS_MAX (12) /* Maximum MCS supported */

struct wil_net_stats {
	unsigned long	rx_packets;
	unsigned long	tx_packets;
	unsigned long	rx_bytes;
	unsigned long	tx_bytes;
	unsigned long	tx_errors;
	unsigned long	rx_dropped;
	unsigned long	rx_non_data_frame;
	unsigned long	rx_short_frame;
	unsigned long	rx_large_frame;
	unsigned long	rx_replay;
	u16 last_mcs_rx;
	u64 rx_per_mcs[WIL_MCS_MAX + 1];
};

/**
 * struct wil_sta_info - data for peer
 *
 * Peer identified by its CID (connection ID)
 * NIC performs beam forming for each peer;
 * if no beam forming done, frame exchange is not
 * possible.
 */
struct wil_sta_info {
	u8 addr[ETH_ALEN];
	enum wil_sta_status status;
	struct wil_net_stats stats;
	/* Rx BACK */
	struct wil_tid_ampdu_rx *tid_rx[WIL_STA_TID_NUM];
	spinlock_t tid_rx_lock; /* guarding tid_rx array */
	unsigned long tid_rx_timer_expired[BITS_TO_LONGS(WIL_STA_TID_NUM)];
	unsigned long tid_rx_stop_requested[BITS_TO_LONGS(WIL_STA_TID_NUM)];
	struct wil_tid_crypto_rx tid_crypto_rx[WIL_STA_TID_NUM];
	struct wil_tid_crypto_rx group_crypto_rx;
	u8 aid; /* 1-254; 0 if unknown/not reported */
};

enum {
	fw_recovery_idle = 0,
	fw_recovery_pending = 1,
	fw_recovery_running = 2,
};

enum {
	hw_capability_last
};

struct wil_probe_client_req {
	struct list_head list;
	u64 cookie;
	u8 cid;
};

struct pmc_ctx {
	/* alloc, free, and read operations must own the lock */
	struct mutex		lock;
	struct vring_tx_desc	*pring_va;
	dma_addr_t		pring_pa;
	struct desc_alloc_info  *descriptors;
	int			last_cmd_status;
	int			num_descriptors;
	int			descriptor_size;
};

struct wil_halp {
	struct mutex		lock; /* protect halp ref_cnt */
	unsigned int		ref_cnt;
	struct completion	comp;
};

struct wil_blob_wrapper {
	struct wil6210_priv *wil;
	struct debugfs_blob_wrapper blob;
};

#define WIL_LED_MAX_ID			(2)
#define WIL_LED_INVALID_ID		(0xF)
#define WIL_LED_BLINK_ON_SLOW_MS	(300)
#define WIL_LED_BLINK_OFF_SLOW_MS	(300)
#define WIL_LED_BLINK_ON_MED_MS		(200)
#define WIL_LED_BLINK_OFF_MED_MS	(200)
#define WIL_LED_BLINK_ON_FAST_MS	(100)
#define WIL_LED_BLINK_OFF_FAST_MS	(100)
enum {
	WIL_LED_TIME_SLOW = 0,
	WIL_LED_TIME_MED,
	WIL_LED_TIME_FAST,
	WIL_LED_TIME_LAST,
};

struct blink_on_off_time {
	u32 on_ms;
	u32 off_ms;
};

struct wil_debugfs_iomem_data {
	void *offset;
	struct wil6210_priv *wil;
};

struct wil_debugfs_data {
	struct wil_debugfs_iomem_data *data_arr;
	int iomem_data_count;
};

extern struct blink_on_off_time led_blink_time[WIL_LED_TIME_LAST];
extern u8 led_id;
extern u8 led_polarity;

struct wil6210_priv {
	struct pci_dev *pdev;
	u32 bar_size;
	struct wireless_dev *wdev;
	void __iomem *csr;
	DECLARE_BITMAP(status, wil_status_last);
	u8 fw_version[ETHTOOL_FWVERS_LEN];
	u32 hw_version;
	u8 chip_revision;
	const char *hw_name;
	const char *wil_fw_name;
	DECLARE_BITMAP(hw_capabilities, hw_capability_last);
	DECLARE_BITMAP(fw_capabilities, WMI_FW_CAPABILITY_MAX);
	u8 n_mids; /* number of additional MIDs as reported by FW */
	u32 recovery_count; /* num of FW recovery attempts in a short time */
	u32 recovery_state; /* FW recovery state machine */
	unsigned long last_fw_recovery; /* jiffies of last fw recovery */
	wait_queue_head_t wq; /* for all wait_event() use */
	/* profile */
	u32 monitor_flags;
	u32 privacy; /* secure connection? */
	u8 hidden_ssid; /* relevant in AP mode */
	u16 channel; /* relevant in AP mode */
	int sinfo_gen;
	u32 ap_isolate; /* no intra-BSS communication */
	struct cfg80211_bss *bss; /* connected bss, relevant in STA mode */
	int locally_generated_disc; /* relevant in STA mode */
	/* interrupt moderation */
	u32 tx_max_burst_duration;
	u32 tx_interframe_timeout;
	u32 rx_max_burst_duration;
	u32 rx_interframe_timeout;
	/* cached ISR registers */
	u32 isr_misc;
	/* mailbox related */
	struct mutex wmi_mutex;
	struct wil6210_mbox_ctl mbox_ctl;
	struct completion wmi_ready;
	struct completion wmi_call;
	u16 wmi_seq;
	u16 reply_id; /**< wait for this WMI event */
	void *reply_buf;
	u16 reply_size;
	struct workqueue_struct *wmi_wq; /* for deferred calls */
	struct work_struct wmi_event_worker;
	struct workqueue_struct *wq_service;
	struct work_struct disconnect_worker;
	struct work_struct fw_error_worker;	/* for FW error recovery */
	struct timer_list connect_timer;
	struct timer_list scan_timer; /* detect scan timeout */
	struct list_head pending_wmi_ev;
	/*
	 * protect pending_wmi_ev
	 * - fill in IRQ from wil6210_irq_misc,
	 * - consumed in thread by wmi_event_worker
	 */
	spinlock_t wmi_ev_lock;
	spinlock_t net_queue_lock; /* guarding stop/wake netif queue */
	int net_queue_stopped; /* netif_tx_stop_all_queues invoked */
	struct napi_struct napi_rx;
	struct napi_struct napi_tx;
	/* keep alive */
	struct list_head probe_client_pending;
	struct mutex probe_client_mutex; /* protect @probe_client_pending */
	struct work_struct probe_client_worker;
	/* DMA related */
	struct vring vring_rx;
	unsigned int rx_buf_len;
	struct vring vring_tx[WIL6210_MAX_TX_RINGS];
	struct vring_tx_data vring_tx_data[WIL6210_MAX_TX_RINGS];
	u8 vring2cid_tid[WIL6210_MAX_TX_RINGS][2]; /* [0] - CID, [1] - TID */
	struct wil_sta_info sta[WIL6210_MAX_CID];
	int bcast_vring;
	u32 vring_idle_trsh; /* HW fetches up to 16 descriptors at once  */
	bool use_extended_dma_addr; /* indicates whether we are using 48 bits */
	/* scan */
	struct cfg80211_scan_request *scan_request;

	struct mutex mutex; /* for wil6210_priv access in wil_{up|down} */
	/* statistics */
	atomic_t isr_count_rx, isr_count_tx;
	/* debugfs */
	struct dentry *debug;
	struct wil_blob_wrapper blobs[ARRAY_SIZE(fw_mapping)];
	u8 discovery_mode;
	u8 abft_len;
	u8 wakeup_trigger;
	struct wil_suspend_stats suspend_stats;
	struct wil_debugfs_data dbg_data;

	void *platform_handle;
	struct wil_platform_ops platform_ops;
	bool keep_radio_on_during_sleep;

	struct pmc_ctx pmc;

	bool pbss;

	struct wil_p2p_info p2p;

	/* P2P_DEVICE vif */
	struct wireless_dev *p2p_wdev;
	struct mutex p2p_wdev_mutex; /* protect @p2p_wdev and @scan_request */
	struct wireless_dev *radio_wdev;

	/* High Access Latency Policy voting */
	struct wil_halp halp;

	enum wmi_ps_profile_type ps_profile;

	int fw_calib_result;

#ifdef CONFIG_PM
	struct notifier_block pm_notify;
#endif /* CONFIG_PM */

	bool suspend_resp_rcvd;
	bool suspend_resp_comp;
	u32 bus_request_kbps;
	u32 bus_request_kbps_pre_suspend;
};

#define wil_to_wiphy(i) (i->wdev->wiphy)
#define wil_to_dev(i) (wiphy_dev(wil_to_wiphy(i)))
#define wiphy_to_wil(w) (struct wil6210_priv *)(wiphy_priv(w))
#define wil_to_wdev(i) (i->wdev)
#define wdev_to_wil(w) (struct wil6210_priv *)(wdev_priv(w))
#define wil_to_ndev(i) (wil_to_wdev(i)->netdev)
#define ndev_to_wil(n) (wdev_to_wil(n->ieee80211_ptr))

__printf(2, 3)
void wil_dbg_trace(struct wil6210_priv *wil, const char *fmt, ...);
__printf(2, 3)
void __wil_err(struct wil6210_priv *wil, const char *fmt, ...);
__printf(2, 3)
void __wil_err_ratelimited(struct wil6210_priv *wil, const char *fmt, ...);
__printf(2, 3)
void __wil_info(struct wil6210_priv *wil, const char *fmt, ...);
__printf(2, 3)
void wil_dbg_ratelimited(const struct wil6210_priv *wil, const char *fmt, ...);
#define wil_dbg(wil, fmt, arg...) do { \
	netdev_dbg(wil_to_ndev(wil), fmt, ##arg); \
	wil_dbg_trace(wil, fmt, ##arg); \
} while (0)

#define wil_dbg_irq(wil, fmt, arg...) wil_dbg(wil, "DBG[ IRQ]" fmt, ##arg)
#define wil_dbg_txrx(wil, fmt, arg...) wil_dbg(wil, "DBG[TXRX]" fmt, ##arg)
#define wil_dbg_wmi(wil, fmt, arg...) wil_dbg(wil, "DBG[ WMI]" fmt, ##arg)
#define wil_dbg_misc(wil, fmt, arg...) wil_dbg(wil, "DBG[MISC]" fmt, ##arg)
#define wil_dbg_pm(wil, fmt, arg...) wil_dbg(wil, "DBG[ PM ]" fmt, ##arg)
#define wil_err(wil, fmt, arg...) __wil_err(wil, "%s: " fmt, __func__, ##arg)
#define wil_info(wil, fmt, arg...) __wil_info(wil, "%s: " fmt, __func__, ##arg)
#define wil_err_ratelimited(wil, fmt, arg...) \
	__wil_err_ratelimited(wil, "%s: " fmt, __func__, ##arg)

/* target operations */
/* register read */
static inline u32 wil_r(struct wil6210_priv *wil, u32 reg)
{
	return readl(wil->csr + HOSTADDR(reg));
}

/* register write. wmb() to make sure it is completed */
static inline void wil_w(struct wil6210_priv *wil, u32 reg, u32 val)
{
	writel(val, wil->csr + HOSTADDR(reg));
	wmb(); /* wait for write to propagate to the HW */
}

/* register set = read, OR, write */
static inline void wil_s(struct wil6210_priv *wil, u32 reg, u32 val)
{
	wil_w(wil, reg, wil_r(wil, reg) | val);
}

/* register clear = read, AND with inverted, write */
static inline void wil_c(struct wil6210_priv *wil, u32 reg, u32 val)
{
	wil_w(wil, reg, wil_r(wil, reg) & ~val);
}

#if defined(CONFIG_DYNAMIC_DEBUG)
#define wil_hex_dump_txrx(prefix_str, prefix_type, rowsize,	\
			  groupsize, buf, len, ascii)		\
			  print_hex_dump_debug("DBG[TXRX]" prefix_str,\
					 prefix_type, rowsize,	\
					 groupsize, buf, len, ascii)

#define wil_hex_dump_wmi(prefix_str, prefix_type, rowsize,	\
			 groupsize, buf, len, ascii)		\
			 print_hex_dump_debug("DBG[ WMI]" prefix_str,\
					prefix_type, rowsize,	\
					groupsize, buf, len, ascii)

#define wil_hex_dump_misc(prefix_str, prefix_type, rowsize,	\
			  groupsize, buf, len, ascii)		\
			  print_hex_dump_debug("DBG[MISC]" prefix_str,\
					prefix_type, rowsize,	\
					groupsize, buf, len, ascii)
#else /* defined(CONFIG_DYNAMIC_DEBUG) */
static inline
void wil_hex_dump_txrx(const char *prefix_str, int prefix_type, int rowsize,
		       int groupsize, const void *buf, size_t len, bool ascii)
{
}

static inline
void wil_hex_dump_wmi(const char *prefix_str, int prefix_type, int rowsize,
		      int groupsize, const void *buf, size_t len, bool ascii)
{
}

static inline
void wil_hex_dump_misc(const char *prefix_str, int prefix_type, int rowsize,
		       int groupsize, const void *buf, size_t len, bool ascii)
{
}
#endif /* defined(CONFIG_DYNAMIC_DEBUG) */

void wil_memcpy_fromio_32(void *dst, const volatile void __iomem *src,
			  size_t count);
void wil_memcpy_toio_32(volatile void __iomem *dst, const void *src,
			size_t count);

void *wil_if_alloc(struct device *dev);
void wil_if_free(struct wil6210_priv *wil);
int wil_if_add(struct wil6210_priv *wil);
void wil_if_remove(struct wil6210_priv *wil);
int wil_priv_init(struct wil6210_priv *wil);
void wil_priv_deinit(struct wil6210_priv *wil);
int wil_ps_update(struct wil6210_priv *wil,
		  enum wmi_ps_profile_type ps_profile);
int wil_reset(struct wil6210_priv *wil, bool no_fw);
void wil_fw_error_recovery(struct wil6210_priv *wil);
void wil_set_recovery_state(struct wil6210_priv *wil, int state);
bool wil_is_recovery_blocked(struct wil6210_priv *wil);
int wil_up(struct wil6210_priv *wil);
int __wil_up(struct wil6210_priv *wil);
int wil_down(struct wil6210_priv *wil);
int __wil_down(struct wil6210_priv *wil);
void wil_mbox_ring_le2cpus(struct wil6210_mbox_ring *r);
int wil_find_cid(struct wil6210_priv *wil, const u8 *mac);
void wil_set_ethtoolops(struct net_device *ndev);

void __iomem *wmi_buffer(struct wil6210_priv *wil, __le32 ptr);
void __iomem *wmi_addr(struct wil6210_priv *wil, u32 ptr);
int wmi_read_hdr(struct wil6210_priv *wil, __le32 ptr,
		 struct wil6210_mbox_hdr *hdr);
int wmi_send(struct wil6210_priv *wil, u16 cmdid, void *buf, u16 len);
void wmi_recv_cmd(struct wil6210_priv *wil);
int wmi_call(struct wil6210_priv *wil, u16 cmdid, void *buf, u16 len,
	     u16 reply_id, void *reply, u8 reply_size, int to_msec);
void wmi_event_worker(struct work_struct *work);
void wmi_event_flush(struct wil6210_priv *wil);
int wmi_set_ssid(struct wil6210_priv *wil, u8 ssid_len, const void *ssid);
int wmi_get_ssid(struct wil6210_priv *wil, u8 *ssid_len, void *ssid);
int wmi_set_channel(struct wil6210_priv *wil, int channel);
int wmi_get_channel(struct wil6210_priv *wil, int *channel);
int wmi_del_cipher_key(struct wil6210_priv *wil, u8 key_index,
		       const void *mac_addr, int key_usage);
int wmi_add_cipher_key(struct wil6210_priv *wil, u8 key_index,
		       const void *mac_addr, int key_len, const void *key,
		       int key_usage);
int wmi_echo(struct wil6210_priv *wil);
int wmi_set_ie(struct wil6210_priv *wil, u8 type, u16 ie_len, const void *ie);
int wmi_rx_chain_add(struct wil6210_priv *wil, struct vring *vring);
int wmi_rxon(struct wil6210_priv *wil, bool on);
int wmi_get_temperature(struct wil6210_priv *wil, u32 *t_m, u32 *t_r);
int wmi_disconnect_sta(struct wil6210_priv *wil, const u8 *mac,
		       u16 reason, bool full_disconnect, bool del_sta);
int wmi_addba(struct wil6210_priv *wil, u8 ringid, u8 size, u16 timeout);
int wmi_delba_tx(struct wil6210_priv *wil, u8 ringid, u16 reason);
int wmi_delba_rx(struct wil6210_priv *wil, u8 cidxtid, u16 reason);
int wmi_addba_rx_resp(struct wil6210_priv *wil, u8 cid, u8 tid, u8 token,
		      u16 status, bool amsdu, u16 agg_wsize, u16 timeout);
int wmi_ps_dev_profile_cfg(struct wil6210_priv *wil,
			   enum wmi_ps_profile_type ps_profile);
int wmi_set_mgmt_retry(struct wil6210_priv *wil, u8 retry_short);
int wmi_get_mgmt_retry(struct wil6210_priv *wil, u8 *retry_short);
int wmi_new_sta(struct wil6210_priv *wil, const u8 *mac, u8 aid);
int wil_addba_rx_request(struct wil6210_priv *wil, u8 cidxtid,
			 u8 dialog_token, __le16 ba_param_set,
			 __le16 ba_timeout, __le16 ba_seq_ctrl);
int wil_addba_tx_request(struct wil6210_priv *wil, u8 ringid, u16 wsize);

void wil6210_clear_irq(struct wil6210_priv *wil);
int wil6210_init_irq(struct wil6210_priv *wil, int irq, bool use_msi);
void wil6210_fini_irq(struct wil6210_priv *wil, int irq);
void wil_mask_irq(struct wil6210_priv *wil);
void wil_unmask_irq(struct wil6210_priv *wil);
void wil_configure_interrupt_moderation(struct wil6210_priv *wil);
void wil_disable_irq(struct wil6210_priv *wil);
void wil_enable_irq(struct wil6210_priv *wil);
void wil6210_mask_halp(struct wil6210_priv *wil);

/* P2P */
bool wil_p2p_is_social_scan(struct cfg80211_scan_request *request);
void wil_p2p_discovery_timer_fn(struct timer_list *t);
int wil_p2p_search(struct wil6210_priv *wil,
		   struct cfg80211_scan_request *request);
int wil_p2p_listen(struct wil6210_priv *wil, struct wireless_dev *wdev,
		   unsigned int duration, struct ieee80211_channel *chan,
		   u64 *cookie);
u8 wil_p2p_stop_discovery(struct wil6210_priv *wil);
int wil_p2p_cancel_listen(struct wil6210_priv *wil, u64 cookie);
void wil_p2p_listen_expired(struct work_struct *work);
void wil_p2p_search_expired(struct work_struct *work);
void wil_p2p_stop_radio_operations(struct wil6210_priv *wil);
void wil_p2p_delayed_listen_work(struct work_struct *work);

/* WMI for P2P */
int wmi_p2p_cfg(struct wil6210_priv *wil, int channel, int bi);
int wmi_start_listen(struct wil6210_priv *wil);
int wmi_start_search(struct wil6210_priv *wil);
int wmi_stop_discovery(struct wil6210_priv *wil);

int wil_cfg80211_mgmt_tx(struct wiphy *wiphy, struct wireless_dev *wdev,
			 struct cfg80211_mgmt_tx_params *params,
			 u64 *cookie);

#if defined(CONFIG_WIL6210_DEBUGFS)
int wil6210_debugfs_init(struct wil6210_priv *wil);
void wil6210_debugfs_remove(struct wil6210_priv *wil);
#else
static inline int wil6210_debugfs_init(struct wil6210_priv *wil) { return 0; }
static inline void wil6210_debugfs_remove(struct wil6210_priv *wil) {}
#endif

int wil_cid_fill_sinfo(struct wil6210_priv *wil, int cid,
		       struct station_info *sinfo);

struct wireless_dev *wil_cfg80211_init(struct device *dev);
void wil_wdev_free(struct wil6210_priv *wil);
void wil_p2p_wdev_free(struct wil6210_priv *wil);

int wmi_set_mac_address(struct wil6210_priv *wil, void *addr);
int wmi_pcp_start(struct wil6210_priv *wil, int bi, u8 wmi_nettype,
		  u8 chan, u8 hidden_ssid, u8 is_go);
int wmi_pcp_stop(struct wil6210_priv *wil);
int wmi_led_cfg(struct wil6210_priv *wil, bool enable);
int wmi_abort_scan(struct wil6210_priv *wil);
void wil_abort_scan(struct wil6210_priv *wil, bool sync);
void wil6210_bus_request(struct wil6210_priv *wil, u32 kbps);
void wil6210_disconnect(struct wil6210_priv *wil, const u8 *bssid,
			u16 reason_code, bool from_event);
void wil_probe_client_flush(struct wil6210_priv *wil);
void wil_probe_client_worker(struct work_struct *work);

int wil_rx_init(struct wil6210_priv *wil, u16 size);
void wil_rx_fini(struct wil6210_priv *wil);

/* TX API */
int wil_vring_init_tx(struct wil6210_priv *wil, int id, int size,
		      int cid, int tid);
void wil_vring_fini_tx(struct wil6210_priv *wil, int id);
int wil_tx_init(struct wil6210_priv *wil, int cid);
int wil_vring_init_bcast(struct wil6210_priv *wil, int id, int size);
int wil_bcast_init(struct wil6210_priv *wil);
void wil_bcast_fini(struct wil6210_priv *wil);

void wil_update_net_queues(struct wil6210_priv *wil, struct vring *vring,
			   bool should_stop);
void wil_update_net_queues_bh(struct wil6210_priv *wil, struct vring *vring,
			      bool check_stop);
netdev_tx_t wil_start_xmit(struct sk_buff *skb, struct net_device *ndev);
int wil_tx_complete(struct wil6210_priv *wil, int ringid);
void wil6210_unmask_irq_tx(struct wil6210_priv *wil);

/* RX API */
void wil_rx_handle(struct wil6210_priv *wil, int *quota);
void wil6210_unmask_irq_rx(struct wil6210_priv *wil);

int wil_iftype_nl2wmi(enum nl80211_iftype type);

int wil_request_firmware(struct wil6210_priv *wil, const char *name,
			 bool load);
bool wil_fw_verify_file_exists(struct wil6210_priv *wil, const char *name);

void wil_pm_runtime_allow(struct wil6210_priv *wil);
void wil_pm_runtime_forbid(struct wil6210_priv *wil);
int wil_pm_runtime_get(struct wil6210_priv *wil);
void wil_pm_runtime_put(struct wil6210_priv *wil);

int wil_can_suspend(struct wil6210_priv *wil, bool is_runtime);
int wil_suspend(struct wil6210_priv *wil, bool is_runtime);
int wil_resume(struct wil6210_priv *wil, bool is_runtime);
bool wil_is_wmi_idle(struct wil6210_priv *wil);
int wmi_resume(struct wil6210_priv *wil);
int wmi_suspend(struct wil6210_priv *wil);
bool wil_is_tx_idle(struct wil6210_priv *wil);
bool wil_is_rx_idle(struct wil6210_priv *wil);

int wil_fw_copy_crash_dump(struct wil6210_priv *wil, void *dest, u32 size);
void wil_fw_core_dump(struct wil6210_priv *wil);

void wil_halp_vote(struct wil6210_priv *wil);
void wil_halp_unvote(struct wil6210_priv *wil);
void wil6210_set_halp(struct wil6210_priv *wil);
void wil6210_clear_halp(struct wil6210_priv *wil);

#endif /* __WIL6210_H__ */
