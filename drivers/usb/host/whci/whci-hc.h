/*
 * Wireless Host Controller (WHC) data structures.
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */
#ifndef _WHCI_WHCI_HC_H
#define _WHCI_WHCI_HC_H

#include <linux/list.h>

/**
 * WHCI_PAGE_SIZE - page size use by WHCI
 *
 * WHCI assumes that host system uses pages of 4096 octets.
 */
#define WHCI_PAGE_SIZE 4096


/**
 * QTD_MAX_TXFER_SIZE - max number of bytes to transfer with a single
 * qtd.
 *
 * This is 2^20 - 1.
 */
#define QTD_MAX_XFER_SIZE 1048575


/**
 * struct whc_qtd - Queue Element Transfer Descriptors (qTD)
 *
 * This describes the data for a bulk, control or interrupt transfer.
 *
 * [WHCI] section 3.2.4
 */
struct whc_qtd {
	__le32 status; /*< remaining transfer len and transfer status */
	__le32 options;
	__le64 page_list_ptr; /*< physical pointer to data buffer page list*/
	__u8   setup[8];      /*< setup data for control transfers */
} __attribute__((packed));

#define QTD_STS_ACTIVE     (1 << 31)  /* enable execution of transaction */
#define QTD_STS_HALTED     (1 << 30)  /* transfer halted */
#define QTD_STS_DBE        (1 << 29)  /* data buffer error */
#define QTD_STS_BABBLE     (1 << 28)  /* babble detected */
#define QTD_STS_RCE        (1 << 27)  /* retry count exceeded */
#define QTD_STS_LAST_PKT   (1 << 26)  /* set Last Packet Flag in WUSB header */
#define QTD_STS_INACTIVE   (1 << 25)  /* queue set is marked inactive */
#define QTD_STS_IALT_VALID (1 << 23)                          /* iAlt field is valid */
#define QTD_STS_IALT(i)    (QTD_STS_IALT_VALID | ((i) << 20)) /* iAlt field */
#define QTD_STS_LEN(l)     ((l) << 0) /* transfer length */
#define QTD_STS_TO_LEN(s)  ((s) & 0x000fffff)

#define QTD_OPT_IOC      (1 << 1) /* page_list_ptr points to buffer directly */
#define QTD_OPT_SMALL    (1 << 0) /* interrupt on complete */

/**
 * struct whc_itd - Isochronous Queue Element Transfer Descriptors (iTD)
 *
 * This describes the data and other parameters for an isochronous
 * transfer.
 *
 * [WHCI] section 3.2.5
 */
struct whc_itd {
	__le16 presentation_time;    /*< presentation time for OUT transfers */
	__u8   num_segments;         /*< number of data segments in segment list */
	__u8   status;               /*< command execution status */
	__le32 options;              /*< misc transfer options */
	__le64 page_list_ptr;        /*< physical pointer to data buffer page list */
	__le64 seg_list_ptr;         /*< physical pointer to segment list */
} __attribute__((packed));

#define ITD_STS_ACTIVE   (1 << 7) /* enable execution of transaction */
#define ITD_STS_DBE      (1 << 5) /* data buffer error */
#define ITD_STS_BABBLE   (1 << 4) /* babble detected */
#define ITD_STS_INACTIVE (1 << 1) /* queue set is marked inactive */

#define ITD_OPT_IOC      (1 << 1) /* interrupt on complete */
#define ITD_OPT_SMALL    (1 << 0) /* page_list_ptr points to buffer directly */

/**
 * Page list entry.
 *
 * A TD's page list must contain sufficient page list entries for the
 * total data length in the TD.
 *
 * [WHCI] section 3.2.4.3
 */
struct whc_page_list_entry {
	__le64 buf_ptr; /*< physical pointer to buffer */
} __attribute__((packed));

/**
 * struct whc_seg_list_entry - Segment list entry.
 *
 * Describes a portion of the data buffer described in the containing
 * qTD's page list.
 *
 * seg_ptr = qtd->page_list_ptr[qtd->seg_list_ptr[seg].idx].buf_ptr
 *           + qtd->seg_list_ptr[seg].offset;
 *
 * Segments can't cross page boundries.
 *
 * [WHCI] section 3.2.5.5
 */
struct whc_seg_list_entry {
	__le16 len;    /*< segment length */
	__u8   idx;    /*< index into page list */
	__u8   status; /*< segment status */
	__le16 offset; /*< 12 bit offset into page */
} __attribute__((packed));

/**
 * struct whc_qhead - endpoint and status information for a qset.
 *
 * [WHCI] section 3.2.6
 */
struct whc_qhead {
	__le64 link; /*< next qset in list */
	__le32 info1;
	__le32 info2;
	__le32 info3;
	__le16 status;
	__le16 err_count;  /*< transaction error count */
	__le32 cur_window;
	__le32 scratch[3]; /*< h/w scratch area */
	union {
		struct whc_qtd qtd;
		struct whc_itd itd;
	} overlay;
} __attribute__((packed));

#define QH_LINK_PTR_MASK (~0x03Full)
#define QH_LINK_PTR(ptr) ((ptr) & QH_LINK_PTR_MASK)
#define QH_LINK_IQS      (1 << 4) /* isochronous queue set */
#define QH_LINK_NTDS(n)  (((n) - 1) << 1) /* number of TDs in queue set */
#define QH_LINK_T        (1 << 0) /* last queue set in periodic schedule list */

#define QH_INFO1_EP(e)           ((e) << 0)  /* endpoint number */
#define QH_INFO1_DIR_IN          (1 << 4)    /* IN transfer */
#define QH_INFO1_DIR_OUT         (0 << 4)    /* OUT transfer */
#define QH_INFO1_TR_TYPE_CTRL    (0x0 << 5)  /* control transfer */
#define QH_INFO1_TR_TYPE_ISOC    (0x1 << 5)  /* isochronous transfer */
#define QH_INFO1_TR_TYPE_BULK    (0x2 << 5)  /* bulk transfer */
#define QH_INFO1_TR_TYPE_INT     (0x3 << 5)  /* interrupt */
#define QH_INFO1_TR_TYPE_LP_INT  (0x7 << 5)  /* low power interrupt */
#define QH_INFO1_DEV_INFO_IDX(i) ((i) << 8)  /* index into device info buffer */
#define QH_INFO1_SET_INACTIVE    (1 << 15)   /* set inactive after transfer */
#define QH_INFO1_MAX_PKT_LEN(l)  ((l) << 16) /* maximum packet length */

#define QH_INFO2_BURST(b)        ((b) << 0)  /* maximum burst length */
#define QH_INFO2_DBP(p)          ((p) << 5)  /* data burst policy (see [WUSB] table 5-7) */
#define QH_INFO2_MAX_COUNT(c)    ((c) << 8)  /* max isoc/int pkts per zone */
#define QH_INFO2_RQS             (1 << 15)   /* reactivate queue set */
#define QH_INFO2_MAX_RETRY(r)    ((r) << 16) /* maximum transaction retries */
#define QH_INFO2_MAX_SEQ(s)      ((s) << 20) /* maximum sequence number */
#define QH_INFO3_MAX_DELAY(d)    ((d) << 0)  /* maximum stream delay in 125 us units (isoc only) */
#define QH_INFO3_INTERVAL(i)     ((i) << 16) /* segment interval in 125 us units (isoc only) */

#define QH_INFO3_TX_RATE_53_3    (0 << 24)
#define QH_INFO3_TX_RATE_80      (1 << 24)
#define QH_INFO3_TX_RATE_106_7   (2 << 24)
#define QH_INFO3_TX_RATE_160     (3 << 24)
#define QH_INFO3_TX_RATE_200     (4 << 24)
#define QH_INFO3_TX_RATE_320     (5 << 24)
#define QH_INFO3_TX_RATE_400     (6 << 24)
#define QH_INFO3_TX_RATE_480     (7 << 24)
#define QH_INFO3_TX_PWR(p)       ((p) << 29) /* transmit power (see [WUSB] section 5.2.1.2) */

#define QH_STATUS_FLOW_CTRL      (1 << 15)
#define QH_STATUS_ICUR(i)        ((i) << 5)
#define QH_STATUS_TO_ICUR(s)     (((s) >> 5) & 0x7)
#define QH_STATUS_SEQ_MASK       0x1f

/**
 * usb_pipe_to_qh_type - USB core pipe type to QH transfer type
 *
 * Returns the QH type field for a USB core pipe type.
 */
static inline unsigned usb_pipe_to_qh_type(unsigned pipe)
{
	static const unsigned type[] = {
		[PIPE_ISOCHRONOUS] = QH_INFO1_TR_TYPE_ISOC,
		[PIPE_INTERRUPT]   = QH_INFO1_TR_TYPE_INT,
		[PIPE_CONTROL]     = QH_INFO1_TR_TYPE_CTRL,
		[PIPE_BULK]        = QH_INFO1_TR_TYPE_BULK,
	};
	return type[usb_pipetype(pipe)];
}

/**
 * Maxiumum number of TDs in a qset.
 */
#define WHCI_QSET_TD_MAX 8

/**
 * struct whc_qset - WUSB data transfers to a specific endpoint
 * @qh: the QHead of this qset
 * @qtd: up to 8 qTDs (for qsets for control, bulk and interrupt
 * transfers)
 * @itd: up to 8 iTDs (for qsets for isochronous transfers)
 * @qset_dma: DMA address for this qset
 * @whc: WHCI HC this qset is for
 * @ep: endpoint
 * @stds: list of sTDs queued to this qset
 * @ntds: number of qTDs queued (not necessarily the same as nTDs
 * field in the QH)
 * @td_start: index of the first qTD in the list
 * @td_end: index of next free qTD in the list (provided
 *          ntds < WHCI_QSET_TD_MAX)
 *
 * Queue Sets (qsets) are added to the asynchronous schedule list
 * (ASL) or the periodic zone list (PZL).
 *
 * qsets may contain up to 8 TDs (either qTDs or iTDs as appropriate).
 * Each TD may refer to at most 1 MiB of data. If a single transfer
 * has > 8MiB of data, TDs can be reused as they are completed since
 * the TD list is used as a circular buffer.  Similarly, several
 * (smaller) transfers may be queued in a qset.
 *
 * WHCI controllers may cache portions of the qsets in the ASL and
 * PZL, requiring the WHCD to inform the WHC that the lists have been
 * updated (fields changed or qsets inserted or removed).  For safe
 * insertion and removal of qsets from the lists the schedule must be
 * stopped to avoid races in updating the QH link pointers.
 *
 * Since the HC is free to execute qsets in any order, all transfers
 * to an endpoint should use the same qset to ensure transfers are
 * executed in the order they're submitted.
 *
 * [WHCI] section 3.2.3
 */
struct whc_qset {
	struct whc_qhead qh;
	union {
		struct whc_qtd qtd[WHCI_QSET_TD_MAX];
		struct whc_itd itd[WHCI_QSET_TD_MAX];
	};

	/* private data for WHCD */
	dma_addr_t qset_dma;
	struct whc *whc;
	struct usb_host_endpoint *ep;
	struct list_head stds;
	int ntds;
	int td_start;
	int td_end;
	struct list_head list_node;
	unsigned in_sw_list:1;
	unsigned in_hw_list:1;
	unsigned remove:1;
	struct urb *pause_after_urb;
	struct completion remove_complete;
	int max_burst;
	int max_seq;
};

static inline void whc_qset_set_link_ptr(u64 *ptr, u64 target)
{
	if (target)
		*ptr = (*ptr & ~(QH_LINK_PTR_MASK | QH_LINK_T)) | QH_LINK_PTR(target);
	else
		*ptr = QH_LINK_T;
}

/**
 * struct di_buf_entry - Device Information (DI) buffer entry.
 *
 * There's one of these per connected device.
 */
struct di_buf_entry {
	__le32 availability_info[8]; /*< MAS availability information, one MAS per bit */
	__le32 addr_sec_info;        /*< addressing and security info */
	__le32 reserved[7];
} __attribute__((packed));

#define WHC_DI_SECURE           (1 << 31)
#define WHC_DI_DISABLE          (1 << 30)
#define WHC_DI_KEY_IDX(k)       ((k) << 8)
#define WHC_DI_KEY_IDX_MASK     0x0000ff00
#define WHC_DI_DEV_ADDR(a)      ((a) << 0)
#define WHC_DI_DEV_ADDR_MASK    0x000000ff

/**
 * struct dn_buf_entry - Device Notification (DN) buffer entry.
 *
 * [WHCI] section 3.2.8
 */
struct dn_buf_entry {
	__u8   msg_size;    /*< number of octets of valid DN data */
	__u8   reserved1;
	__u8   src_addr;    /*< source address */
	__u8   status;      /*< buffer entry status */
	__le32 tkid;        /*< TKID for source device, valid if secure bit is set */
	__u8   dn_data[56]; /*< up to 56 octets of DN data */
} __attribute__((packed));

#define WHC_DN_STATUS_VALID  (1 << 7) /* buffer entry is valid */
#define WHC_DN_STATUS_SECURE (1 << 6) /* notification received using secure frame */

#define WHC_N_DN_ENTRIES (4096 / sizeof(struct dn_buf_entry))

/* The Add MMC IE WUSB Generic Command may take up to 256 bytes of
   data. [WHCI] section 2.4.7. */
#define WHC_GEN_CMD_DATA_LEN 256

/*
 * HC registers.
 *
 * [WHCI] section 2.4
 */

#define WHCIVERSION          0x00

#define WHCSPARAMS           0x04
#  define WHCSPARAMS_TO_N_MMC_IES(p) (((p) >> 16) & 0xff)
#  define WHCSPARAMS_TO_N_KEYS(p)    (((p) >> 8) & 0xff)
#  define WHCSPARAMS_TO_N_DEVICES(p) (((p) >> 0) & 0x7f)

#define WUSBCMD              0x08
#  define WUSBCMD_BCID(b)            ((b) << 16)
#  define WUSBCMD_BCID_MASK          (0xff << 16)
#  define WUSBCMD_ASYNC_QSET_RM      (1 << 12)
#  define WUSBCMD_PERIODIC_QSET_RM   (1 << 11)
#  define WUSBCMD_WUSBSI(s)          ((s) << 8)
#  define WUSBCMD_WUSBSI_MASK        (0x7 << 8)
#  define WUSBCMD_ASYNC_SYNCED_DB    (1 << 7)
#  define WUSBCMD_PERIODIC_SYNCED_DB (1 << 6)
#  define WUSBCMD_ASYNC_UPDATED      (1 << 5)
#  define WUSBCMD_PERIODIC_UPDATED   (1 << 4)
#  define WUSBCMD_ASYNC_EN           (1 << 3)
#  define WUSBCMD_PERIODIC_EN        (1 << 2)
#  define WUSBCMD_WHCRESET           (1 << 1)
#  define WUSBCMD_RUN                (1 << 0)

#define WUSBSTS              0x0c
#  define WUSBSTS_ASYNC_SCHED             (1 << 15)
#  define WUSBSTS_PERIODIC_SCHED          (1 << 14)
#  define WUSBSTS_DNTS_SCHED              (1 << 13)
#  define WUSBSTS_HCHALTED                (1 << 12)
#  define WUSBSTS_GEN_CMD_DONE            (1 << 9)
#  define WUSBSTS_CHAN_TIME_ROLLOVER      (1 << 8)
#  define WUSBSTS_DNTS_OVERFLOW           (1 << 7)
#  define WUSBSTS_BPST_ADJUSTMENT_CHANGED (1 << 6)
#  define WUSBSTS_HOST_ERR                (1 << 5)
#  define WUSBSTS_ASYNC_SCHED_SYNCED      (1 << 4)
#  define WUSBSTS_PERIODIC_SCHED_SYNCED   (1 << 3)
#  define WUSBSTS_DNTS_INT                (1 << 2)
#  define WUSBSTS_ERR_INT                 (1 << 1)
#  define WUSBSTS_INT                     (1 << 0)
#  define WUSBSTS_INT_MASK                0x3ff

#define WUSBINTR             0x10
#  define WUSBINTR_GEN_CMD_DONE             (1 << 9)
#  define WUSBINTR_CHAN_TIME_ROLLOVER       (1 << 8)
#  define WUSBINTR_DNTS_OVERFLOW            (1 << 7)
#  define WUSBINTR_BPST_ADJUSTMENT_CHANGED  (1 << 6)
#  define WUSBINTR_HOST_ERR                 (1 << 5)
#  define WUSBINTR_ASYNC_SCHED_SYNCED       (1 << 4)
#  define WUSBINTR_PERIODIC_SCHED_SYNCED    (1 << 3)
#  define WUSBINTR_DNTS_INT                 (1 << 2)
#  define WUSBINTR_ERR_INT                  (1 << 1)
#  define WUSBINTR_INT                      (1 << 0)
#  define WUSBINTR_ALL 0x3ff

#define WUSBGENCMDSTS        0x14
#  define WUSBGENCMDSTS_ACTIVE (1 << 31)
#  define WUSBGENCMDSTS_ERROR  (1 << 24)
#  define WUSBGENCMDSTS_IOC    (1 << 23)
#  define WUSBGENCMDSTS_MMCIE_ADD 0x01
#  define WUSBGENCMDSTS_MMCIE_RM  0x02
#  define WUSBGENCMDSTS_SET_MAS   0x03
#  define WUSBGENCMDSTS_CHAN_STOP 0x04
#  define WUSBGENCMDSTS_RWP_EN    0x05

#define WUSBGENCMDPARAMS     0x18
#define WUSBGENADDR          0x20
#define WUSBASYNCLISTADDR    0x28
#define WUSBDNTSBUFADDR      0x30
#define WUSBDEVICEINFOADDR   0x38

#define WUSBSETSECKEYCMD     0x40
#  define WUSBSETSECKEYCMD_SET    (1 << 31)
#  define WUSBSETSECKEYCMD_ERASE  (1 << 30)
#  define WUSBSETSECKEYCMD_GTK    (1 << 8)
#  define WUSBSETSECKEYCMD_IDX(i) ((i) << 0)

#define WUSBTKID             0x44
#define WUSBSECKEY           0x48
#define WUSBPERIODICLISTBASE 0x58
#define WUSBMASINDEX         0x60

#define WUSBDNTSCTRL         0x64
#  define WUSBDNTSCTRL_ACTIVE      (1 << 31)
#  define WUSBDNTSCTRL_INTERVAL(i) ((i) << 8)
#  define WUSBDNTSCTRL_SLOTS(s)    ((s) << 0)

#define WUSBTIME             0x68
#  define WUSBTIME_CHANNEL_TIME_MASK 0x00ffffff

#define WUSBBPST             0x6c
#define WUSBDIBUPDATED       0x70

#endif /* #ifndef _WHCI_WHCI_HC_H */
