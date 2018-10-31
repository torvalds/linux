/*
 * Copyright (c) 2012-2017 Qualcomm Atheros, Inc.
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
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
#include <linux/irqreturn.h>
#include "wmi.h"
#include "wil_platform.h"
#include "fw.h"

extern bool no_fw_recovery;
extern unsigned int mtu_max;
extern unsigned short rx_ring_overflow_thrsh;
extern int agg_wsize;
extern bool rx_align_2;
extern bool rx_large_buf;
extern bool debug_fw;
extern bool disable_ap_sme;
extern bool ftm_mode;

struct wil6210_priv;
struct wil6210_vif;
union wil_tx_desc;

#define WIL_NAME "wil6210"

#define WIL_FW_NAME_DEFAULT "wil6210.fw"
#define WIL_FW_NAME_FTM_DEFAULT "wil6210_ftm.fw"

#define WIL_FW_NAME_SPARROW_PLUS "wil6210_sparrow_plus.fw"
#define WIL_FW_NAME_FTM_SPARROW_PLUS "wil6210_sparrow_plus_ftm.fw"

#define WIL_FW_NAME_TALYN "wil6436.fw"
#define WIL_FW_NAME_FTM_TALYN "wil6436_ftm.fw"
#define WIL_BRD_NAME_TALYN "wil6436.brd"

#define WIL_BOARD_FILE_NAME "wil6210.brd" /* board & radio parameters */

#define WIL_DEFAULT_BUS_REQUEST_KBPS 128000 /* ~1Gbps */
#define WIL_MAX_BUS_REQUEST_KBPS 800000 /* ~6.1Gbps */

#define WIL_NUM_LATENCY_BINS 200

/* maximum number of virtual interfaces the driver supports
 * (including the main interface)
 */
#define WIL_MAX_VIFS 4

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
#define WIL_MAX_AMPDU_SIZE_128	(128 * 1024) /* FW/HW limit */
#define WIL_MAX_AGG_WSIZE_64	(64) /* FW/HW limit */
#define WIL6210_MAX_STATUS_RINGS	(8)

/* Hardware offload block adds the following:
 * 26 bytes - 3-address QoS data header
 *  8 bytes - IV + EIV (for GCMP)
 *  8 bytes - SNAP
 * 16 bytes - MIC (for GCMP)
 *  4 bytes - CRC
 */
#define WIL_MAX_MPDU_OVERHEAD	(62)

struct wil_suspend_count_stats {
	unsigned long successful_suspends;
	unsigned long successful_resumes;
	unsigned long failed_suspends;
	unsigned long failed_resumes;
};

struct wil_suspend_stats {
	struct wil_suspend_count_stats r_off;
	struct wil_suspend_count_stats r_on;
	unsigned long rejected_by_device; /* only radio on */
	unsigned long rejected_by_host;
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
#define RGF_USER_USAGE_8		(0x880020)
	#define BIT_USER_PREVENT_DEEP_SLEEP	BIT(0)
	#define BIT_USER_SUPPORT_T_POWER_ON_0	BIT(1)
	#define BIT_USER_EXT_CLK		BIT(2)
#define RGF_USER_HW_MACHINE_STATE	(0x8801dc)
	#define HW_MACHINE_BOOT_DONE	(0x3fffffd)
#define RGF_USER_USER_CPU_0		(0x8801e0)
	#define BIT_USER_USER_CPU_MAN_RST	BIT(1) /* user_cpu_man_rst */
#define RGF_USER_CPU_PC			(0x8801e8)
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
#define RGF_USER_OTP_HW_RD_MACHINE_1	(0x880ce0)
	#define BIT_OTP_SIGNATURE_ERR_TALYN_MB		BIT(0)
	#define BIT_OTP_HW_SECTION_DONE_TALYN_MB	BIT(2)
	#define BIT_NO_FLASH_INDICATION			BIT(8)
#define RGF_USER_XPM_IFC_RD_TIME1	(0x880cec)
#define RGF_USER_XPM_IFC_RD_TIME2	(0x880cf0)
#define RGF_USER_XPM_IFC_RD_TIME3	(0x880cf4)
#define RGF_USER_XPM_IFC_RD_TIME4	(0x880cf8)
#define RGF_USER_XPM_IFC_RD_TIME5	(0x880cfc)
#define RGF_USER_XPM_IFC_RD_TIME6	(0x880d00)
#define RGF_USER_XPM_IFC_RD_TIME7	(0x880d04)
#define RGF_USER_XPM_IFC_RD_TIME8	(0x880d08)
#define RGF_USER_XPM_IFC_RD_TIME9	(0x880d0c)
#define RGF_USER_XPM_IFC_RD_TIME10	(0x880d10)
#define RGF_USER_XPM_RD_DOUT_SAMPLE_TIME (0x880d64)

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
#define RGF_DMA_MISC_CTL				(0x881d6c)
	#define BIT_OFUL34_RDY_VALID_BUG_FIX_EN			BIT(7)

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

#define RGF_CAF_ICR_TALYN_MB		(0x8893d4) /* struct RGF_ICR */
#define RGF_CAF_ICR			(0x88946c) /* struct RGF_ICR */
#define RGF_CAF_OSC_CONTROL		(0x88afa4)
	#define BIT_CAF_OSC_XTAL_EN		BIT(0)
#define RGF_CAF_PLL_LOCK_STATUS		(0x88afec)
	#define BIT_CAF_OSC_DIG_XTAL_STABLE	BIT(0)

#define RGF_OTP_QC_SECURED		(0x8a0038)
	#define BIT_BOOT_FROM_ROM		BIT(31)

/* eDMA */
#define RGF_INT_COUNT_ON_SPECIAL_EVT	(0x8b62d8)

#define RGF_INT_CTRL_INT_GEN_CFG_0	(0x8bc000)
#define RGF_INT_CTRL_INT_GEN_CFG_1	(0x8bc004)
#define RGF_INT_GEN_TIME_UNIT_LIMIT	(0x8bc0c8)

#define RGF_INT_GEN_CTRL		(0x8bc0ec)
	#define BIT_CONTROL_0			BIT(0)

/* eDMA status interrupts */
#define RGF_INT_GEN_RX_ICR		(0x8bc0f4)
	#define BIT_RX_STATUS_IRQ BIT(WIL_RX_STATUS_IRQ_IDX)
#define RGF_INT_GEN_TX_ICR		(0x8bc110)
	#define BIT_TX_STATUS_IRQ BIT(WIL_TX_STATUS_IRQ_IDX)
#define RGF_INT_CTRL_RX_INT_MASK	(0x8bc12c)
#define RGF_INT_CTRL_TX_INT_MASK	(0x8bc130)

#define RGF_INT_GEN_IDLE_TIME_LIMIT	(0x8bc134)

#define USER_EXT_USER_PMU_3		(0x88d00c)
	#define BIT_PMU_DEVICE_RDY		BIT(0)

#define RGF_USER_JTAG_DEV_ID	(0x880b34) /* device ID */
	#define JTAG_DEV_ID_SPARROW	(0x2632072f)
	#define JTAG_DEV_ID_TALYN	(0x7e0e1)
	#define JTAG_DEV_ID_TALYN_MB	(0x1007e0e1)

#define RGF_USER_REVISION_ID		(0x88afe4)
#define RGF_USER_REVISION_ID_MASK	(3)
	#define REVISION_ID_SPARROW_B0	(0x0)
	#define REVISION_ID_SPARROW_D0	(0x3)

#define RGF_OTP_MAC_TALYN_MB		(0x8a0304)
#define RGF_OTP_MAC			(0x8a0620)

/* Talyn-MB */
#define RGF_USER_USER_CPU_0_TALYN_MB	(0x8c0138)
#define RGF_USER_MAC_CPU_0_TALYN_MB	(0x8c0154)

/* crash codes for FW/Ucode stored here */

/* ASSERT RGFs */
#define SPARROW_RGF_FW_ASSERT_CODE	(0x91f020)
#define SPARROW_RGF_UCODE_ASSERT_CODE	(0x91f028)
#define TALYN_RGF_FW_ASSERT_CODE	(0xa37020)
#define TALYN_RGF_UCODE_ASSERT_CODE	(0xa37028)

enum {
	HW_VER_UNKNOWN,
	HW_VER_SPARROW_B0, /* REVISION_ID_SPARROW_B0 */
	HW_VER_SPARROW_D0, /* REVISION_ID_SPARROW_D0 */
	HW_VER_TALYN,	/* JTAG_DEV_ID_TALYN */
	HW_VER_TALYN_MB	/* JTAG_DEV_ID_TALYN_MB */
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
#define SPARROW_FW_MAPPING_TABLE_SIZE 10
#define TALYN_FW_MAPPING_TABLE_SIZE 13
#define TALYN_MB_FW_MAPPING_TABLE_SIZE 19
#define MAX_FW_MAPPING_TABLE_SIZE 19

/* Common representation of physical address in wil ring */
struct wil_ring_dma_addr {
	__le32 addr_low;
	__le16 addr_high;
} __packed;

struct fw_map {
	u32 from; /* linker address - from, inclusive */
	u32 to;   /* linker address - to, exclusive */
	u32 host; /* PCI/Host address - BAR0 + 0x880000 */
	const char *name; /* for debugfs */
	bool fw; /* true if FW mapping, false if UCODE mapping */
	bool crash_dump; /* true if should be dumped during crash dump */
};

/* array size should be in sync with actual definition in the wmi.c */
extern const struct fw_map sparrow_fw_mapping[SPARROW_FW_MAPPING_TABLE_SIZE];
extern const struct fw_map sparrow_d0_mac_rgf_ext;
extern const struct fw_map talyn_fw_mapping[TALYN_FW_MAPPING_TABLE_SIZE];
extern const struct fw_map talyn_mb_fw_mapping[TALYN_MB_FW_MAPPING_TABLE_SIZE];
extern struct fw_map fw_mapping[MAX_FW_MAPPING_TABLE_SIZE];

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
 * struct wil_ctx - software context for ring descriptor
 */
struct wil_ctx {
	struct sk_buff *skb;
	u8 nr_frags;
	u8 mapped_as;
};

struct wil_desc_ring_rx_swtail { /* relevant for enhanced DMA only */
	u32 *va;
	dma_addr_t pa;
};

/**
 * A general ring structure, used for RX and TX.
 * In legacy DMA it represents the vring,
 * In enahnced DMA it represents the descriptor ring (vrings are handled by FW)
 */
struct wil_ring {
	dma_addr_t pa;
	volatile union wil_ring_desc *va;
	u16 size; /* number of wil_ring_desc elements */
	u32 swtail;
	u32 swhead;
	u32 hwtail; /* write here to inform hw */
	struct wil_ctx *ctx; /* ctx[size] - software context */
	struct wil_desc_ring_rx_swtail edma_rx_swtail;
	bool is_rx;
};

/**
 * Additional data for Rx ring.
 * Used for enhanced DMA RX chaining.
 */
struct wil_ring_rx_data {
	/* the skb being assembled */
	struct sk_buff *skb;
	/* true if we are skipping a bad fragmented packet */
	bool skipping;
	u16 buff_size;
};

/**
 * Status ring structure, used for enhanced DMA completions for RX and TX.
 */
struct wil_status_ring {
	dma_addr_t pa;
	void *va; /* pointer to ring_[tr]x_status elements */
	u16 size; /* number of status elements */
	size_t elem_size; /* status element size in bytes */
	u32 swhead;
	u32 hwtail; /* write here to inform hw */
	bool is_rx;
	u8 desc_rdy_pol; /* Expected descriptor ready bit polarity */
	struct wil_ring_rx_data rx_data;
};

#define WIL_STA_TID_NUM (16)
#define WIL_MCS_MAX (12) /* Maximum MCS supported */

struct wil_net_stats {
	unsigned long	rx_packets;
	unsigned long	tx_packets;
	unsigned long	rx_bytes;
	unsigned long	tx_bytes;
	unsigned long	tx_errors;
	u32 tx_latency_min_us;
	u32 tx_latency_max_us;
	u64 tx_latency_total_us;
	unsigned long	rx_dropped;
	unsigned long	rx_non_data_frame;
	unsigned long	rx_short_frame;
	unsigned long	rx_large_frame;
	unsigned long	rx_replay;
	unsigned long	rx_mic_error;
	unsigned long	rx_key_error; /* eDMA specific */
	unsigned long	rx_amsdu_error; /* eDMA specific */
	unsigned long	rx_csum_err;
	u16 last_mcs_rx;
	u64 rx_per_mcs[WIL_MCS_MAX + 1];
};

/**
 * struct tx_rx_ops - different TX/RX ops for legacy and enhanced
 * DMA flow
 */
struct wil_txrx_ops {
	void (*configure_interrupt_moderation)(struct wil6210_priv *wil);
	/* TX ops */
	int (*ring_init_tx)(struct wil6210_vif *vif, int ring_id,
			    int size, int cid, int tid);
	void (*ring_fini_tx)(struct wil6210_priv *wil, struct wil_ring *ring);
	int (*ring_init_bcast)(struct wil6210_vif *vif, int id, int size);
	int (*tx_init)(struct wil6210_priv *wil);
	void (*tx_fini)(struct wil6210_priv *wil);
	int (*tx_desc_map)(union wil_tx_desc *desc, dma_addr_t pa,
			   u32 len, int ring_index);
	void (*tx_desc_unmap)(struct device *dev,
			      union wil_tx_desc *desc,
			      struct wil_ctx *ctx);
	int (*tx_ring_tso)(struct wil6210_priv *wil, struct wil6210_vif *vif,
			   struct wil_ring *ring, struct sk_buff *skb);
	irqreturn_t (*irq_tx)(int irq, void *cookie);
	/* RX ops */
	int (*rx_init)(struct wil6210_priv *wil, uint ring_order);
	void (*rx_fini)(struct wil6210_priv *wil);
	int (*wmi_addba_rx_resp)(struct wil6210_priv *wil, u8 mid, u8 cid,
				 u8 tid, u8 token, u16 status, bool amsdu,
				 u16 agg_wsize, u16 timeout);
	void (*get_reorder_params)(struct wil6210_priv *wil,
				   struct sk_buff *skb, int *tid, int *cid,
				   int *mid, u16 *seq, int *mcast, int *retry);
	void (*get_netif_rx_params)(struct sk_buff *skb,
				    int *cid, int *security);
	int (*rx_crypto_check)(struct wil6210_priv *wil, struct sk_buff *skb);
	int (*rx_error_check)(struct wil6210_priv *wil, struct sk_buff *skb,
			      struct wil_net_stats *stats);
	bool (*is_rx_idle)(struct wil6210_priv *wil);
	irqreturn_t (*irq_rx)(int irq, void *cookie);
};

/**
 * Additional data for Tx ring
 */
struct wil_ring_tx_data {
	bool dot1x_open;
	int enabled;
	cycles_t idle, last_idle, begin;
	u8 agg_wsize; /* agreed aggregation window, 0 - no agg */
	u16 agg_timeout;
	u8 agg_amsdu;
	bool addba_in_progress; /* if set, agg_xxx is for request in progress */
	u8 mid;
	spinlock_t lock;
};

enum { /* for wil6210_priv.status */
	wil_status_fwready = 0, /* FW operational */
	wil_status_dontscan,
	wil_status_mbox_ready, /* MBOX structures ready */
	wil_status_irqen, /* interrupts enabled - for debug */
	wil_status_napi_en, /* NAPI enabled protected by wil->mutex */
	wil_status_resetting, /* reset in progress */
	wil_status_suspending, /* suspend in progress */
	wil_status_suspended, /* suspend completed, device is suspended */
	wil_status_resuming, /* resume in progress */
	wil_status_collecting_dumps, /* crashdump collection in progress */
	wil_status_last /* keep last */
};

struct pci_dev;

/**
 * struct tid_ampdu_rx - TID aggregation information (Rx).
 *
 * @reorder_buf: buffer to reorder incoming aggregated MPDUs
 * @last_rx: jiffies of last rx activity
 * @head_seq_num: head sequence number in reordering buffer.
 * @stored_mpdu_num: number of MPDUs in reordering buffer
 * @ssn: Starting Sequence Number expected to be aggregated.
 * @buf_size: buffer size for incoming A-MPDUs
 * @ssn_last_drop: SSN of the last dropped frame
 * @total: total number of processed incoming frames
 * @drop_dup: duplicate frames dropped for this reorder buffer
 * @drop_old: old frames dropped for this reorder buffer
 * @first_time: true when this buffer used 1-st time
 * @mcast_last_seq: sequence number (SN) of last received multicast packet
 * @drop_dup_mcast: duplicate multicast frames dropped for this reorder buffer
 */
struct wil_tid_ampdu_rx {
	struct sk_buff **reorder_buf;
	unsigned long last_rx;
	u16 head_seq_num;
	u16 stored_mpdu_num;
	u16 ssn;
	u16 buf_size;
	u16 ssn_last_drop;
	unsigned long long total; /* frames processed */
	unsigned long long drop_dup;
	unsigned long long drop_old;
	bool first_time; /* is it 1-st time this buffer used? */
	u16 mcast_last_seq; /* multicast dup detection */
	unsigned long long drop_dup_mcast;
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
	u8 mid;
	enum wil_sta_status status;
	struct wil_net_stats stats;
	/**
	 * 20 latency bins. 1st bin counts packets with latency
	 * of 0..tx_latency_res, last bin counts packets with latency
	 * of 19*tx_latency_res and above.
	 * tx_latency_res is configured from "tx_latency" debug-fs.
	 */
	u64 *tx_latency_bins;
	struct wmi_link_stats_basic fw_stats_basic;
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
	hw_capa_no_flash,
	hw_capa_last
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
	u8			handle_icr;
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

enum wil6210_vif_status {
	wil_vif_fwconnecting,
	wil_vif_fwconnected,
	wil_vif_status_last /* keep last */
};

struct wil6210_vif {
	struct wireless_dev wdev;
	struct net_device *ndev;
	struct wil6210_priv *wil;
	u8 mid;
	DECLARE_BITMAP(status, wil_vif_status_last);
	u32 privacy; /* secure connection? */
	u16 channel; /* relevant in AP mode */
	u8 hidden_ssid; /* relevant in AP mode */
	u32 ap_isolate; /* no intra-BSS communication */
	bool pbss;
	int bcast_ring;
	struct cfg80211_bss *bss; /* connected bss, relevant in STA mode */
	int locally_generated_disc; /* relevant in STA mode */
	struct timer_list connect_timer;
	struct work_struct disconnect_worker;
	/* scan */
	struct cfg80211_scan_request *scan_request;
	struct timer_list scan_timer; /* detect scan timeout */
	struct wil_p2p_info p2p;
	/* keep alive */
	struct list_head probe_client_pending;
	struct mutex probe_client_mutex; /* protect @probe_client_pending */
	struct work_struct probe_client_worker;
	int net_queue_stopped; /* netif_tx_stop_all_queues invoked */
	bool fw_stats_ready; /* per-cid statistics are ready inside sta_info */
	u64 fw_stats_tsf; /* measurement timestamp */
};

/**
 * RX buffer allocated for enhanced DMA RX descriptors
 */
struct wil_rx_buff {
	struct sk_buff *skb;
	struct list_head list;
	int id;
};

/**
 * During Rx completion processing, the driver extracts a buffer ID which
 * is used as an index to the rx_buff_mgmt.buff_arr array and then the SKB
 * is given to the network stack and the buffer is moved from the 'active'
 * list to the 'free' list.
 * During Rx refill, SKBs are attached to free buffers and moved to the
 * 'active' list.
 */
struct wil_rx_buff_mgmt {
	struct wil_rx_buff *buff_arr;
	size_t size; /* number of items in buff_arr */
	struct list_head active;
	struct list_head free;
	unsigned long free_list_empty_cnt; /* statistics */
};

struct wil_fw_stats_global {
	bool ready;
	u64 tsf; /* measurement timestamp */
	struct wmi_link_stats_global stats;
};

struct wil6210_priv {
	struct pci_dev *pdev;
	u32 bar_size;
	struct wiphy *wiphy;
	struct net_device *main_ndev;
	int n_msi;
	void __iomem *csr;
	DECLARE_BITMAP(status, wil_status_last);
	u8 fw_version[ETHTOOL_FWVERS_LEN];
	u32 hw_version;
	u8 chip_revision;
	const char *hw_name;
	const char *wil_fw_name;
	char *board_file;
	u32 brd_file_addr;
	u32 brd_file_max_size;
	DECLARE_BITMAP(hw_capa, hw_capa_last);
	DECLARE_BITMAP(fw_capabilities, WMI_FW_CAPABILITY_MAX);
	DECLARE_BITMAP(platform_capa, WIL_PLATFORM_CAPA_MAX);
	u32 recovery_count; /* num of FW recovery attempts in a short time */
	u32 recovery_state; /* FW recovery state machine */
	unsigned long last_fw_recovery; /* jiffies of last fw recovery */
	wait_queue_head_t wq; /* for all wait_event() use */
	u8 max_vifs; /* maximum number of interfaces, including main */
	struct wil6210_vif *vifs[WIL_MAX_VIFS];
	struct mutex vif_mutex; /* protects access to VIF entries */
	atomic_t connected_vifs;
	/* profile */
	struct cfg80211_chan_def monitor_chandef;
	u32 monitor_flags;
	int sinfo_gen;
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
	u8 reply_mid;
	void *reply_buf;
	u16 reply_size;
	struct workqueue_struct *wmi_wq; /* for deferred calls */
	struct work_struct wmi_event_worker;
	struct workqueue_struct *wq_service;
	struct work_struct fw_error_worker;	/* for FW error recovery */
	struct list_head pending_wmi_ev;
	/*
	 * protect pending_wmi_ev
	 * - fill in IRQ from wil6210_irq_misc,
	 * - consumed in thread by wmi_event_worker
	 */
	spinlock_t wmi_ev_lock;
	spinlock_t net_queue_lock; /* guarding stop/wake netif queue */
	struct napi_struct napi_rx;
	struct napi_struct napi_tx;
	struct net_device napi_ndev; /* dummy net_device serving all VIFs */

	/* DMA related */
	struct wil_ring ring_rx;
	unsigned int rx_buf_len;
	struct wil_ring ring_tx[WIL6210_MAX_TX_RINGS];
	struct wil_ring_tx_data ring_tx_data[WIL6210_MAX_TX_RINGS];
	struct wil_status_ring srings[WIL6210_MAX_STATUS_RINGS];
	u8 num_rx_status_rings;
	int tx_sring_idx;
	u8 ring2cid_tid[WIL6210_MAX_TX_RINGS][2]; /* [0] - CID, [1] - TID */
	struct wil_sta_info sta[WIL6210_MAX_CID];
	u32 ring_idle_trsh; /* HW fetches up to 16 descriptors at once  */
	u32 dma_addr_size; /* indicates dma addr size */
	struct wil_rx_buff_mgmt rx_buff_mgmt;
	bool use_enhanced_dma_hw;
	struct wil_txrx_ops txrx_ops;

	struct mutex mutex; /* for wil6210_priv access in wil_{up|down} */
	/* statistics */
	atomic_t isr_count_rx, isr_count_tx;
	/* debugfs */
	struct dentry *debug;
	struct wil_blob_wrapper blobs[MAX_FW_MAPPING_TABLE_SIZE];
	u8 discovery_mode;
	u8 abft_len;
	u8 wakeup_trigger;
	struct wil_suspend_stats suspend_stats;
	struct wil_debugfs_data dbg_data;
	bool tx_latency; /* collect TX latency measurements */
	size_t tx_latency_res; /* bin resolution in usec */

	void *platform_handle;
	struct wil_platform_ops platform_ops;
	bool keep_radio_on_during_sleep;

	struct pmc_ctx pmc;

	u8 p2p_dev_started;

	/* P2P_DEVICE vif */
	struct wireless_dev *p2p_wdev;
	struct wireless_dev *radio_wdev;

	/* High Access Latency Policy voting */
	struct wil_halp halp;

	enum wmi_ps_profile_type ps_profile;

	int fw_calib_result;

	struct notifier_block pm_notify;

	bool suspend_resp_rcvd;
	bool suspend_resp_comp;
	u32 bus_request_kbps;
	u32 bus_request_kbps_pre_suspend;

	u32 rgf_fw_assert_code_addr;
	u32 rgf_ucode_assert_code_addr;
	u32 iccm_base;

	/* relevant only for eDMA */
	bool use_compressed_rx_status;
	u32 rx_status_ring_order;
	u32 tx_status_ring_order;
	u32 rx_buff_id_count;
	bool amsdu_en;
	bool use_rx_hw_reordering;
	bool secured_boot;
	u8 boot_config;

	struct wil_fw_stats_global fw_stats_global;

	u32 max_agg_wsize;
	u32 max_ampdu_size;
};

#define wil_to_wiphy(i) (i->wiphy)
#define wil_to_dev(i) (wiphy_dev(wil_to_wiphy(i)))
#define wiphy_to_wil(w) (struct wil6210_priv *)(wiphy_priv(w))
#define wdev_to_wil(w) (struct wil6210_priv *)(wdev_priv(w))
#define ndev_to_wil(n) (wdev_to_wil(n->ieee80211_ptr))
#define ndev_to_vif(n) (struct wil6210_vif *)(netdev_priv(n))
#define vif_to_wil(v) (v->wil)
#define vif_to_ndev(v) (v->ndev)
#define vif_to_wdev(v) (&v->wdev)

static inline struct wil6210_vif *wdev_to_vif(struct wil6210_priv *wil,
					      struct wireless_dev *wdev)
{
	/* main interface is shared with P2P device */
	if (wdev == wil->p2p_wdev)
		return ndev_to_vif(wil->main_ndev);
	else
		return container_of(wdev, struct wil6210_vif, wdev);
}

static inline struct wireless_dev *
vif_to_radio_wdev(struct wil6210_priv *wil, struct wil6210_vif *vif)
{
	/* main interface is shared with P2P device */
	if (vif->mid)
		return vif_to_wdev(vif);
	else
		return wil->radio_wdev;
}

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
	netdev_dbg(wil->main_ndev, fmt, ##arg); \
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

void wil_get_board_file(struct wil6210_priv *wil, char *buf, size_t len);

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

struct wil6210_vif *
wil_vif_alloc(struct wil6210_priv *wil, const char *name,
	      unsigned char name_assign_type, enum nl80211_iftype iftype);
void wil_vif_free(struct wil6210_vif *vif);
void *wil_if_alloc(struct device *dev);
bool wil_has_other_active_ifaces(struct wil6210_priv *wil,
				 struct net_device *ndev, bool up, bool ok);
bool wil_has_active_ifaces(struct wil6210_priv *wil, bool up, bool ok);
void wil_if_free(struct wil6210_priv *wil);
int wil_vif_add(struct wil6210_priv *wil, struct wil6210_vif *vif);
int wil_if_add(struct wil6210_priv *wil);
void wil_vif_remove(struct wil6210_priv *wil, u8 mid);
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
void wil_refresh_fw_capabilities(struct wil6210_priv *wil);
void wil_mbox_ring_le2cpus(struct wil6210_mbox_ring *r);
int wil_find_cid(struct wil6210_priv *wil, u8 mid, const u8 *mac);
void wil_set_ethtoolops(struct net_device *ndev);

struct fw_map *wil_find_fw_mapping(const char *section);
void __iomem *wmi_buffer_block(struct wil6210_priv *wil, __le32 ptr, u32 size);
void __iomem *wmi_buffer(struct wil6210_priv *wil, __le32 ptr);
void __iomem *wmi_addr(struct wil6210_priv *wil, u32 ptr);
int wmi_read_hdr(struct wil6210_priv *wil, __le32 ptr,
		 struct wil6210_mbox_hdr *hdr);
int wmi_send(struct wil6210_priv *wil, u16 cmdid, u8 mid, void *buf, u16 len);
void wmi_recv_cmd(struct wil6210_priv *wil);
int wmi_call(struct wil6210_priv *wil, u16 cmdid, u8 mid, void *buf, u16 len,
	     u16 reply_id, void *reply, u16 reply_size, int to_msec);
void wmi_event_worker(struct work_struct *work);
void wmi_event_flush(struct wil6210_priv *wil);
int wmi_set_ssid(struct wil6210_vif *vif, u8 ssid_len, const void *ssid);
int wmi_get_ssid(struct wil6210_vif *vif, u8 *ssid_len, void *ssid);
int wmi_set_channel(struct wil6210_priv *wil, int channel);
int wmi_get_channel(struct wil6210_priv *wil, int *channel);
int wmi_del_cipher_key(struct wil6210_vif *vif, u8 key_index,
		       const void *mac_addr, int key_usage);
int wmi_add_cipher_key(struct wil6210_vif *vif, u8 key_index,
		       const void *mac_addr, int key_len, const void *key,
		       int key_usage);
int wmi_echo(struct wil6210_priv *wil);
int wmi_set_ie(struct wil6210_vif *vif, u8 type, u16 ie_len, const void *ie);
int wmi_rx_chain_add(struct wil6210_priv *wil, struct wil_ring *vring);
int wmi_rxon(struct wil6210_priv *wil, bool on);
int wmi_get_temperature(struct wil6210_priv *wil, u32 *t_m, u32 *t_r);
int wmi_disconnect_sta(struct wil6210_vif *vif, const u8 *mac,
		       u16 reason, bool full_disconnect, bool del_sta);
int wmi_addba(struct wil6210_priv *wil, u8 mid,
	      u8 ringid, u8 size, u16 timeout);
int wmi_delba_tx(struct wil6210_priv *wil, u8 mid, u8 ringid, u16 reason);
int wmi_delba_rx(struct wil6210_priv *wil, u8 mid, u8 cidxtid, u16 reason);
int wmi_addba_rx_resp(struct wil6210_priv *wil,
		      u8 mid, u8 cid, u8 tid, u8 token,
		      u16 status, bool amsdu, u16 agg_wsize, u16 timeout);
int wmi_ps_dev_profile_cfg(struct wil6210_priv *wil,
			   enum wmi_ps_profile_type ps_profile);
int wmi_set_mgmt_retry(struct wil6210_priv *wil, u8 retry_short);
int wmi_get_mgmt_retry(struct wil6210_priv *wil, u8 *retry_short);
int wmi_new_sta(struct wil6210_vif *vif, const u8 *mac, u8 aid);
int wmi_port_allocate(struct wil6210_priv *wil, u8 mid,
		      const u8 *mac, enum nl80211_iftype iftype);
int wmi_port_delete(struct wil6210_priv *wil, u8 mid);
int wmi_link_stats_cfg(struct wil6210_vif *vif, u32 type, u8 cid, u32 interval);
int wil_addba_rx_request(struct wil6210_priv *wil, u8 mid,
			 u8 cidxtid, u8 dialog_token, __le16 ba_param_set,
			 __le16 ba_timeout, __le16 ba_seq_ctrl);
int wil_addba_tx_request(struct wil6210_priv *wil, u8 ringid, u16 wsize);

void wil6210_clear_irq(struct wil6210_priv *wil);
int wil6210_init_irq(struct wil6210_priv *wil, int irq);
void wil6210_fini_irq(struct wil6210_priv *wil, int irq);
void wil_mask_irq(struct wil6210_priv *wil);
void wil_unmask_irq(struct wil6210_priv *wil);
void wil_configure_interrupt_moderation(struct wil6210_priv *wil);
void wil_disable_irq(struct wil6210_priv *wil);
void wil_enable_irq(struct wil6210_priv *wil);
void wil6210_mask_halp(struct wil6210_priv *wil);

/* P2P */
bool wil_p2p_is_social_scan(struct cfg80211_scan_request *request);
int wil_p2p_search(struct wil6210_vif *vif,
		   struct cfg80211_scan_request *request);
int wil_p2p_listen(struct wil6210_priv *wil, struct wireless_dev *wdev,
		   unsigned int duration, struct ieee80211_channel *chan,
		   u64 *cookie);
u8 wil_p2p_stop_discovery(struct wil6210_vif *vif);
int wil_p2p_cancel_listen(struct wil6210_vif *vif, u64 cookie);
void wil_p2p_listen_expired(struct work_struct *work);
void wil_p2p_search_expired(struct work_struct *work);
void wil_p2p_stop_radio_operations(struct wil6210_priv *wil);
void wil_p2p_delayed_listen_work(struct work_struct *work);

/* WMI for P2P */
int wmi_p2p_cfg(struct wil6210_vif *vif, int channel, int bi);
int wmi_start_listen(struct wil6210_vif *vif);
int wmi_start_search(struct wil6210_vif *vif);
int wmi_stop_discovery(struct wil6210_vif *vif);

int wil_cfg80211_mgmt_tx(struct wiphy *wiphy, struct wireless_dev *wdev,
			 struct cfg80211_mgmt_tx_params *params,
			 u64 *cookie);
int wil_cfg80211_iface_combinations_from_fw(
	struct wil6210_priv *wil,
	const struct wil_fw_record_concurrency *conc);
int wil_vif_prepare_stop(struct wil6210_vif *vif);

#if defined(CONFIG_WIL6210_DEBUGFS)
int wil6210_debugfs_init(struct wil6210_priv *wil);
void wil6210_debugfs_remove(struct wil6210_priv *wil);
#else
static inline int wil6210_debugfs_init(struct wil6210_priv *wil) { return 0; }
static inline void wil6210_debugfs_remove(struct wil6210_priv *wil) {}
#endif

int wil_cid_fill_sinfo(struct wil6210_vif *vif, int cid,
		       struct station_info *sinfo);

struct wil6210_priv *wil_cfg80211_init(struct device *dev);
void wil_cfg80211_deinit(struct wil6210_priv *wil);
void wil_p2p_wdev_free(struct wil6210_priv *wil);

int wmi_set_mac_address(struct wil6210_priv *wil, void *addr);
int wmi_pcp_start(struct wil6210_vif *vif, int bi, u8 wmi_nettype, u8 chan,
		  u8 hidden_ssid, u8 is_go);
int wmi_pcp_stop(struct wil6210_vif *vif);
int wmi_led_cfg(struct wil6210_priv *wil, bool enable);
int wmi_abort_scan(struct wil6210_vif *vif);
void wil_abort_scan(struct wil6210_vif *vif, bool sync);
void wil_abort_scan_all_vifs(struct wil6210_priv *wil, bool sync);
void wil6210_bus_request(struct wil6210_priv *wil, u32 kbps);
void wil6210_disconnect(struct wil6210_vif *vif, const u8 *bssid,
			u16 reason_code, bool from_event);
void wil_probe_client_flush(struct wil6210_vif *vif);
void wil_probe_client_worker(struct work_struct *work);
void wil_disconnect_worker(struct work_struct *work);

void wil_init_txrx_ops(struct wil6210_priv *wil);

/* TX API */
int wil_ring_init_tx(struct wil6210_vif *vif, int cid);
int wil_vring_init_bcast(struct wil6210_vif *vif, int id, int size);
int wil_bcast_init(struct wil6210_vif *vif);
void wil_bcast_fini(struct wil6210_vif *vif);
void wil_bcast_fini_all(struct wil6210_priv *wil);

void wil_update_net_queues(struct wil6210_priv *wil, struct wil6210_vif *vif,
			   struct wil_ring *ring, bool should_stop);
void wil_update_net_queues_bh(struct wil6210_priv *wil, struct wil6210_vif *vif,
			      struct wil_ring *ring, bool check_stop);
netdev_tx_t wil_start_xmit(struct sk_buff *skb, struct net_device *ndev);
int wil_tx_complete(struct wil6210_vif *vif, int ringid);
void wil6210_unmask_irq_tx(struct wil6210_priv *wil);
void wil6210_unmask_irq_tx_edma(struct wil6210_priv *wil);

/* RX API */
void wil_rx_handle(struct wil6210_priv *wil, int *quota);
void wil6210_unmask_irq_rx(struct wil6210_priv *wil);
void wil6210_unmask_irq_rx_edma(struct wil6210_priv *wil);

int wil_iftype_nl2wmi(enum nl80211_iftype type);

int wil_request_firmware(struct wil6210_priv *wil, const char *name,
			 bool load);
int wil_request_board(struct wil6210_priv *wil, const char *name);
bool wil_fw_verify_file_exists(struct wil6210_priv *wil, const char *name);

void wil_pm_runtime_allow(struct wil6210_priv *wil);
void wil_pm_runtime_forbid(struct wil6210_priv *wil);
int wil_pm_runtime_get(struct wil6210_priv *wil);
void wil_pm_runtime_put(struct wil6210_priv *wil);

int wil_can_suspend(struct wil6210_priv *wil, bool is_runtime);
int wil_suspend(struct wil6210_priv *wil, bool is_runtime, bool keep_radio_on);
int wil_resume(struct wil6210_priv *wil, bool is_runtime, bool keep_radio_on);
bool wil_is_wmi_idle(struct wil6210_priv *wil);
int wmi_resume(struct wil6210_priv *wil);
int wmi_suspend(struct wil6210_priv *wil);
bool wil_is_tx_idle(struct wil6210_priv *wil);

int wil_fw_copy_crash_dump(struct wil6210_priv *wil, void *dest, u32 size);
void wil_fw_core_dump(struct wil6210_priv *wil);

void wil_halp_vote(struct wil6210_priv *wil);
void wil_halp_unvote(struct wil6210_priv *wil);
void wil6210_set_halp(struct wil6210_priv *wil);
void wil6210_clear_halp(struct wil6210_priv *wil);

int wmi_start_sched_scan(struct wil6210_priv *wil,
			 struct cfg80211_sched_scan_request *request);
int wmi_stop_sched_scan(struct wil6210_priv *wil);
int wmi_mgmt_tx(struct wil6210_vif *vif, const u8 *buf, size_t len);
int wmi_mgmt_tx_ext(struct wil6210_vif *vif, const u8 *buf, size_t len,
		    u8 channel, u16 duration_ms);

int reverse_memcmp(const void *cs, const void *ct, size_t count);

/* WMI for enhanced DMA */
int wil_wmi_tx_sring_cfg(struct wil6210_priv *wil, int ring_id);
int wil_wmi_cfg_def_rx_offload(struct wil6210_priv *wil,
			       u16 max_rx_pl_per_desc);
int wil_wmi_rx_sring_add(struct wil6210_priv *wil, u16 ring_id);
int wil_wmi_rx_desc_ring_add(struct wil6210_priv *wil, int status_ring_id);
int wil_wmi_tx_desc_ring_add(struct wil6210_vif *vif, int ring_id, int cid,
			     int tid);
int wil_wmi_bcast_desc_ring_add(struct wil6210_vif *vif, int ring_id);
int wmi_addba_rx_resp_edma(struct wil6210_priv *wil, u8 mid, u8 cid,
			   u8 tid, u8 token, u16 status, bool amsdu,
			   u16 agg_wsize, u16 timeout);

#endif /* __WIL6210_H__ */
