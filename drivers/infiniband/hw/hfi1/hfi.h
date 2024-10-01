/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright(c) 2020-2023 Cornelis Networks, Inc.
 * Copyright(c) 2015-2020 Intel Corporation.
 */

#ifndef _HFI1_KERNEL_H
#define _HFI1_KERNEL_H

#include <linux/refcount.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/completion.h>
#include <linux/kref.h>
#include <linux/sched.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/xarray.h>
#include <rdma/ib_hdrs.h>
#include <rdma/opa_addr.h>
#include <linux/rhashtable.h>
#include <rdma/rdma_vt.h>

#include "chip_registers.h"
#include "common.h"
#include "opfn.h"
#include "verbs.h"
#include "pio.h"
#include "chip.h"
#include "mad.h"
#include "qsfp.h"
#include "platform.h"
#include "affinity.h"
#include "msix.h"

/* bumped 1 from s/w major version of TrueScale */
#define HFI1_CHIP_VERS_MAJ 3U

/* don't care about this except printing */
#define HFI1_CHIP_VERS_MIN 0U

/* The Organization Unique Identifier (Mfg code), and its position in GUID */
#define HFI1_OUI 0x001175
#define HFI1_OUI_LSB 40

#define DROP_PACKET_OFF		0
#define DROP_PACKET_ON		1

#define NEIGHBOR_TYPE_HFI		0
#define NEIGHBOR_TYPE_SWITCH	1

#define HFI1_MAX_ACTIVE_WORKQUEUE_ENTRIES 5

extern unsigned long hfi1_cap_mask;
#define HFI1_CAP_KGET_MASK(mask, cap) ((mask) & HFI1_CAP_##cap)
#define HFI1_CAP_UGET_MASK(mask, cap) \
	(((mask) >> HFI1_CAP_USER_SHIFT) & HFI1_CAP_##cap)
#define HFI1_CAP_KGET(cap) (HFI1_CAP_KGET_MASK(hfi1_cap_mask, cap))
#define HFI1_CAP_UGET(cap) (HFI1_CAP_UGET_MASK(hfi1_cap_mask, cap))
#define HFI1_CAP_IS_KSET(cap) (!!HFI1_CAP_KGET(cap))
#define HFI1_CAP_IS_USET(cap) (!!HFI1_CAP_UGET(cap))
#define HFI1_MISC_GET() ((hfi1_cap_mask >> HFI1_CAP_MISC_SHIFT) & \
			HFI1_CAP_MISC_MASK)
/* Offline Disabled Reason is 4-bits */
#define HFI1_ODR_MASK(rsn) ((rsn) & OPA_PI_MASK_OFFLINE_REASON)

/*
 * Control context is always 0 and handles the error packets.
 * It also handles the VL15 and multicast packets.
 */
#define HFI1_CTRL_CTXT    0

/*
 * Driver context will store software counters for each of the events
 * associated with these status registers
 */
#define NUM_CCE_ERR_STATUS_COUNTERS 41
#define NUM_RCV_ERR_STATUS_COUNTERS 64
#define NUM_MISC_ERR_STATUS_COUNTERS 13
#define NUM_SEND_PIO_ERR_STATUS_COUNTERS 36
#define NUM_SEND_DMA_ERR_STATUS_COUNTERS 4
#define NUM_SEND_EGRESS_ERR_STATUS_COUNTERS 64
#define NUM_SEND_ERR_STATUS_COUNTERS 3
#define NUM_SEND_CTXT_ERR_STATUS_COUNTERS 5
#define NUM_SEND_DMA_ENG_ERR_STATUS_COUNTERS 24

/*
 * per driver stats, either not device nor port-specific, or
 * summed over all of the devices and ports.
 * They are described by name via ipathfs filesystem, so layout
 * and number of elements can change without breaking compatibility.
 * If members are added or deleted hfi1_statnames[] in debugfs.c must
 * change to match.
 */
struct hfi1_ib_stats {
	__u64 sps_ints; /* number of interrupts handled */
	__u64 sps_errints; /* number of error interrupts */
	__u64 sps_txerrs; /* tx-related packet errors */
	__u64 sps_rcverrs; /* non-crc rcv packet errors */
	__u64 sps_hwerrs; /* hardware errors reported (parity, etc.) */
	__u64 sps_nopiobufs; /* no pio bufs avail from kernel */
	__u64 sps_ctxts; /* number of contexts currently open */
	__u64 sps_lenerrs; /* number of kernel packets where RHF != LRH len */
	__u64 sps_buffull;
	__u64 sps_hdrfull;
};

extern struct hfi1_ib_stats hfi1_stats;
extern const struct pci_error_handlers hfi1_pci_err_handler;

extern int num_driver_cntrs;

/*
 * First-cut criterion for "device is active" is
 * two thousand dwords combined Tx, Rx traffic per
 * 5-second interval. SMA packets are 64 dwords,
 * and occur "a few per second", presumably each way.
 */
#define HFI1_TRAFFIC_ACTIVE_THRESHOLD (2000)

/*
 * Below contains all data related to a single context (formerly called port).
 */

struct hfi1_opcode_stats_perctx;

struct ctxt_eager_bufs {
	struct eager_buffer {
		void *addr;
		dma_addr_t dma;
		ssize_t len;
	} *buffers;
	struct {
		void *addr;
		dma_addr_t dma;
	} *rcvtids;
	u32 size;                /* total size of eager buffers */
	u32 rcvtid_size;         /* size of each eager rcv tid */
	u16 count;               /* size of buffers array */
	u16 numbufs;             /* number of buffers allocated */
	u16 alloced;             /* number of rcvarray entries used */
	u16 threshold;           /* head update threshold */
};

struct exp_tid_set {
	struct list_head list;
	u32 count;
};

struct hfi1_ctxtdata;
typedef int (*intr_handler)(struct hfi1_ctxtdata *rcd, int data);
typedef void (*rhf_rcv_function_ptr)(struct hfi1_packet *packet);

struct tid_queue {
	struct list_head queue_head;
			/* queue head for QP TID resource waiters */
	u32 enqueue;	/* count of tid enqueues */
	u32 dequeue;	/* count of tid dequeues */
};

struct hfi1_ctxtdata {
	/* rcvhdrq base, needs mmap before useful */
	void *rcvhdrq;
	/* kernel virtual address where hdrqtail is updated */
	volatile __le64 *rcvhdrtail_kvaddr;
	/* so functions that need physical port can get it easily */
	struct hfi1_pportdata *ppd;
	/* so file ops can get at unit */
	struct hfi1_devdata *dd;
	/* this receive context's assigned PIO ACK send context */
	struct send_context *sc;
	/* per context recv functions */
	const rhf_rcv_function_ptr *rhf_rcv_function_map;
	/*
	 * The interrupt handler for a particular receive context can vary
	 * throughout it's lifetime. This is not a lock protected data member so
	 * it must be updated atomically and the prev and new value must always
	 * be valid. Worst case is we process an extra interrupt and up to 64
	 * packets with the wrong interrupt handler.
	 */
	intr_handler do_interrupt;
	/** fast handler after autoactive */
	intr_handler fast_handler;
	/** slow handler */
	intr_handler slow_handler;
	/* napi pointer assiociated with netdev */
	struct napi_struct *napi;
	/* verbs rx_stats per rcd */
	struct hfi1_opcode_stats_perctx *opstats;
	/* clear interrupt mask */
	u64 imask;
	/* ctxt rcvhdrq head offset */
	u32 head;
	/* number of rcvhdrq entries */
	u16 rcvhdrq_cnt;
	u8 ireg;	/* clear interrupt register */
	/* receive packet sequence counter */
	u8 seq_cnt;
	/* size of each of the rcvhdrq entries */
	u8 rcvhdrqentsize;
	/* offset of RHF within receive header entry */
	u8 rhf_offset;
	/* dynamic receive available interrupt timeout */
	u8 rcvavail_timeout;
	/* Indicates that this is vnic context */
	bool is_vnic;
	/* vnic queue index this context is mapped to */
	u8 vnic_q_idx;
	/* Is ASPM interrupt supported for this context */
	bool aspm_intr_supported;
	/* ASPM state (enabled/disabled) for this context */
	bool aspm_enabled;
	/* Is ASPM processing enabled for this context (in intr context) */
	bool aspm_intr_enable;
	struct ctxt_eager_bufs egrbufs;
	/* QPs waiting for context processing */
	struct list_head qp_wait_list;
	/* tid allocation lists */
	struct exp_tid_set tid_group_list;
	struct exp_tid_set tid_used_list;
	struct exp_tid_set tid_full_list;

	/* Timer for re-enabling ASPM if interrupt activity quiets down */
	struct timer_list aspm_timer;
	/* per-context configuration flags */
	unsigned long flags;
	/* array of tid_groups */
	struct tid_group  *groups;
	/* mmap of hdrq, must fit in 44 bits */
	dma_addr_t rcvhdrq_dma;
	dma_addr_t rcvhdrqtailaddr_dma;
	/* Last interrupt timestamp */
	ktime_t aspm_ts_last_intr;
	/* Last timestamp at which we scheduled a timer for this context */
	ktime_t aspm_ts_timer_sched;
	/* Lock to serialize between intr, timer intr and user threads */
	spinlock_t aspm_lock;
	/* Reference count the base context usage */
	struct kref kref;
	/* numa node of this context */
	int numa_id;
	/* associated msix interrupt. */
	s16 msix_intr;
	/* job key */
	u16 jkey;
	/* number of RcvArray groups for this context. */
	u16 rcv_array_groups;
	/* index of first eager TID entry. */
	u16 eager_base;
	/* number of expected TID entries */
	u16 expected_count;
	/* index of first expected TID entry. */
	u16 expected_base;
	/* Device context index */
	u8 ctxt;

	/* PSM Specific fields */
	/* lock protecting all Expected TID data */
	struct mutex exp_mutex;
	/* lock protecting all Expected TID data of kernel contexts */
	spinlock_t exp_lock;
	/* Queue for QP's waiting for HW TID flows */
	struct tid_queue flow_queue;
	/* Queue for QP's waiting for HW receive array entries */
	struct tid_queue rarr_queue;
	/* when waiting for rcv or pioavail */
	wait_queue_head_t wait;
	/* uuid from PSM */
	u8 uuid[16];
	/* same size as task_struct .comm[], command that opened context */
	char comm[TASK_COMM_LEN];
	/* Bitmask of in use context(s) */
	DECLARE_BITMAP(in_use_ctxts, HFI1_MAX_SHARED_CTXTS);
	/* per-context event flags for fileops/intr communication */
	unsigned long event_flags;
	/* A page of memory for rcvhdrhead, rcvegrhead, rcvegrtail * N */
	void *subctxt_uregbase;
	/* An array of pages for the eager receive buffers * N */
	void *subctxt_rcvegrbuf;
	/* An array of pages for the eager header queue entries * N */
	void *subctxt_rcvhdr_base;
	/* total number of polled urgent packets */
	u32 urgent;
	/* saved total number of polled urgent packets for poll edge trigger */
	u32 urgent_poll;
	/* Type of packets or conditions we want to poll for */
	u16 poll_type;
	/* non-zero if ctxt is being shared. */
	u16 subctxt_id;
	/* The version of the library which opened this ctxt */
	u32 userversion;
	/*
	 * non-zero if ctxt can be shared, and defines the maximum number of
	 * sub-contexts for this device context.
	 */
	u8 subctxt_cnt;

	/* Bit mask to track free TID RDMA HW flows */
	unsigned long flow_mask;
	struct tid_flow_state flows[RXE_NUM_TID_FLOWS];
};

/**
 * rcvhdrq_size - return total size in bytes for header queue
 * @rcd: the receive context
 *
 * rcvhdrqentsize is in DWs, so we have to convert to bytes
 *
 */
static inline u32 rcvhdrq_size(struct hfi1_ctxtdata *rcd)
{
	return PAGE_ALIGN(rcd->rcvhdrq_cnt *
			  rcd->rcvhdrqentsize * sizeof(u32));
}

/*
 * Represents a single packet at a high level. Put commonly computed things in
 * here so we do not have to keep doing them over and over. The rule of thumb is
 * if something is used one time to derive some value, store that something in
 * here. If it is used multiple times, then store the result of that derivation
 * in here.
 */
struct hfi1_packet {
	void *ebuf;
	void *hdr;
	void *payload;
	struct hfi1_ctxtdata *rcd;
	__le32 *rhf_addr;
	struct rvt_qp *qp;
	struct ib_other_headers *ohdr;
	struct ib_grh *grh;
	struct opa_16b_mgmt *mgmt;
	u64 rhf;
	u32 maxcnt;
	u32 rhqoff;
	u32 dlid;
	u32 slid;
	int numpkt;
	u16 tlen;
	s16 etail;
	u16 pkey;
	u8 hlen;
	u8 rsize;
	u8 updegr;
	u8 etype;
	u8 extra_byte;
	u8 pad;
	u8 sc;
	u8 sl;
	u8 opcode;
	bool migrated;
};

/* Packet types */
#define HFI1_PKT_TYPE_9B  0
#define HFI1_PKT_TYPE_16B 1

/*
 * OPA 16B Header
 */
#define OPA_16B_L4_MASK		0xFFull
#define OPA_16B_SC_MASK		0x1F00000ull
#define OPA_16B_SC_SHIFT	20
#define OPA_16B_LID_MASK	0xFFFFFull
#define OPA_16B_DLID_MASK	0xF000ull
#define OPA_16B_DLID_SHIFT	20
#define OPA_16B_DLID_HIGH_SHIFT	12
#define OPA_16B_SLID_MASK	0xF00ull
#define OPA_16B_SLID_SHIFT	20
#define OPA_16B_SLID_HIGH_SHIFT	8
#define OPA_16B_BECN_MASK       0x80000000ull
#define OPA_16B_BECN_SHIFT      31
#define OPA_16B_FECN_MASK       0x10000000ull
#define OPA_16B_FECN_SHIFT      28
#define OPA_16B_L2_MASK		0x60000000ull
#define OPA_16B_L2_SHIFT	29
#define OPA_16B_PKEY_MASK	0xFFFF0000ull
#define OPA_16B_PKEY_SHIFT	16
#define OPA_16B_LEN_MASK	0x7FF00000ull
#define OPA_16B_LEN_SHIFT	20
#define OPA_16B_RC_MASK		0xE000000ull
#define OPA_16B_RC_SHIFT	25
#define OPA_16B_AGE_MASK	0xFF0000ull
#define OPA_16B_AGE_SHIFT	16
#define OPA_16B_ENTROPY_MASK	0xFFFFull

/*
 * OPA 16B L2/L4 Encodings
 */
#define OPA_16B_L4_9B		0x00
#define OPA_16B_L2_TYPE		0x02
#define OPA_16B_L4_FM		0x08
#define OPA_16B_L4_IB_LOCAL	0x09
#define OPA_16B_L4_IB_GLOBAL	0x0A
#define OPA_16B_L4_ETHR		OPA_VNIC_L4_ETHR

/*
 * OPA 16B Management
 */
#define OPA_16B_L4_FM_PAD	3  /* fixed 3B pad */
#define OPA_16B_L4_FM_HLEN	24 /* 16B(16) + L4_FM(8) */

static inline u8 hfi1_16B_get_l4(struct hfi1_16b_header *hdr)
{
	return (u8)(hdr->lrh[2] & OPA_16B_L4_MASK);
}

static inline u8 hfi1_16B_get_sc(struct hfi1_16b_header *hdr)
{
	return (u8)((hdr->lrh[1] & OPA_16B_SC_MASK) >> OPA_16B_SC_SHIFT);
}

static inline u32 hfi1_16B_get_dlid(struct hfi1_16b_header *hdr)
{
	return (u32)((hdr->lrh[1] & OPA_16B_LID_MASK) |
		     (((hdr->lrh[2] & OPA_16B_DLID_MASK) >>
		     OPA_16B_DLID_HIGH_SHIFT) << OPA_16B_DLID_SHIFT));
}

static inline u32 hfi1_16B_get_slid(struct hfi1_16b_header *hdr)
{
	return (u32)((hdr->lrh[0] & OPA_16B_LID_MASK) |
		     (((hdr->lrh[2] & OPA_16B_SLID_MASK) >>
		     OPA_16B_SLID_HIGH_SHIFT) << OPA_16B_SLID_SHIFT));
}

static inline u8 hfi1_16B_get_becn(struct hfi1_16b_header *hdr)
{
	return (u8)((hdr->lrh[0] & OPA_16B_BECN_MASK) >> OPA_16B_BECN_SHIFT);
}

static inline u8 hfi1_16B_get_fecn(struct hfi1_16b_header *hdr)
{
	return (u8)((hdr->lrh[1] & OPA_16B_FECN_MASK) >> OPA_16B_FECN_SHIFT);
}

static inline u8 hfi1_16B_get_l2(struct hfi1_16b_header *hdr)
{
	return (u8)((hdr->lrh[1] & OPA_16B_L2_MASK) >> OPA_16B_L2_SHIFT);
}

static inline u16 hfi1_16B_get_pkey(struct hfi1_16b_header *hdr)
{
	return (u16)((hdr->lrh[2] & OPA_16B_PKEY_MASK) >> OPA_16B_PKEY_SHIFT);
}

static inline u8 hfi1_16B_get_rc(struct hfi1_16b_header *hdr)
{
	return (u8)((hdr->lrh[1] & OPA_16B_RC_MASK) >> OPA_16B_RC_SHIFT);
}

static inline u8 hfi1_16B_get_age(struct hfi1_16b_header *hdr)
{
	return (u8)((hdr->lrh[3] & OPA_16B_AGE_MASK) >> OPA_16B_AGE_SHIFT);
}

static inline u16 hfi1_16B_get_len(struct hfi1_16b_header *hdr)
{
	return (u16)((hdr->lrh[0] & OPA_16B_LEN_MASK) >> OPA_16B_LEN_SHIFT);
}

static inline u16 hfi1_16B_get_entropy(struct hfi1_16b_header *hdr)
{
	return (u16)(hdr->lrh[3] & OPA_16B_ENTROPY_MASK);
}

#define OPA_16B_MAKE_QW(low_dw, high_dw) (((u64)(high_dw) << 32) | (low_dw))

/*
 * BTH
 */
#define OPA_16B_BTH_PAD_MASK	7
static inline u8 hfi1_16B_bth_get_pad(struct ib_other_headers *ohdr)
{
	return (u8)((be32_to_cpu(ohdr->bth[0]) >> IB_BTH_PAD_SHIFT) &
		   OPA_16B_BTH_PAD_MASK);
}

/*
 * 16B Management
 */
#define OPA_16B_MGMT_QPN_MASK	0xFFFFFF
static inline u32 hfi1_16B_get_dest_qpn(struct opa_16b_mgmt *mgmt)
{
	return be32_to_cpu(mgmt->dest_qpn) & OPA_16B_MGMT_QPN_MASK;
}

static inline u32 hfi1_16B_get_src_qpn(struct opa_16b_mgmt *mgmt)
{
	return be32_to_cpu(mgmt->src_qpn) & OPA_16B_MGMT_QPN_MASK;
}

static inline void hfi1_16B_set_qpn(struct opa_16b_mgmt *mgmt,
				    u32 dest_qp, u32 src_qp)
{
	mgmt->dest_qpn = cpu_to_be32(dest_qp & OPA_16B_MGMT_QPN_MASK);
	mgmt->src_qpn = cpu_to_be32(src_qp & OPA_16B_MGMT_QPN_MASK);
}

/**
 * hfi1_get_rc_ohdr - get extended header
 * @opah - the opaheader
 */
static inline struct ib_other_headers *
hfi1_get_rc_ohdr(struct hfi1_opa_header *opah)
{
	struct ib_other_headers *ohdr;
	struct ib_header *hdr = NULL;
	struct hfi1_16b_header *hdr_16b = NULL;

	/* Find out where the BTH is */
	if (opah->hdr_type == HFI1_PKT_TYPE_9B) {
		hdr = &opah->ibh;
		if (ib_get_lnh(hdr) == HFI1_LRH_BTH)
			ohdr = &hdr->u.oth;
		else
			ohdr = &hdr->u.l.oth;
	} else {
		u8 l4;

		hdr_16b = &opah->opah;
		l4  = hfi1_16B_get_l4(hdr_16b);
		if (l4 == OPA_16B_L4_IB_LOCAL)
			ohdr = &hdr_16b->u.oth;
		else
			ohdr = &hdr_16b->u.l.oth;
	}
	return ohdr;
}

struct rvt_sge_state;

/*
 * Get/Set IB link-level config parameters for f_get/set_ib_cfg()
 * Mostly for MADs that set or query link parameters, also ipath
 * config interfaces
 */
#define HFI1_IB_CFG_LIDLMC 0 /* LID (LS16b) and Mask (MS16b) */
#define HFI1_IB_CFG_LWID_DG_ENB 1 /* allowed Link-width downgrade */
#define HFI1_IB_CFG_LWID_ENB 2 /* allowed Link-width */
#define HFI1_IB_CFG_LWID 3 /* currently active Link-width */
#define HFI1_IB_CFG_SPD_ENB 4 /* allowed Link speeds */
#define HFI1_IB_CFG_SPD 5 /* current Link spd */
#define HFI1_IB_CFG_RXPOL_ENB 6 /* Auto-RX-polarity enable */
#define HFI1_IB_CFG_LREV_ENB 7 /* Auto-Lane-reversal enable */
#define HFI1_IB_CFG_LINKLATENCY 8 /* Link Latency (IB1.2 only) */
#define HFI1_IB_CFG_HRTBT 9 /* IB heartbeat off/enable/auto; DDR/QDR only */
#define HFI1_IB_CFG_OP_VLS 10 /* operational VLs */
#define HFI1_IB_CFG_VL_HIGH_CAP 11 /* num of VL high priority weights */
#define HFI1_IB_CFG_VL_LOW_CAP 12 /* num of VL low priority weights */
#define HFI1_IB_CFG_OVERRUN_THRESH 13 /* IB overrun threshold */
#define HFI1_IB_CFG_PHYERR_THRESH 14 /* IB PHY error threshold */
#define HFI1_IB_CFG_LINKDEFAULT 15 /* IB link default (sleep/poll) */
#define HFI1_IB_CFG_PKEYS 16 /* update partition keys */
#define HFI1_IB_CFG_MTU 17 /* update MTU in IBC */
#define HFI1_IB_CFG_VL_HIGH_LIMIT 19
#define HFI1_IB_CFG_PMA_TICKS 20 /* PMA sample tick resolution */
#define HFI1_IB_CFG_PORT 21 /* switch port we are connected to */

/*
 * HFI or Host Link States
 *
 * These describe the states the driver thinks the logical and physical
 * states are in.  Used as an argument to set_link_state().  Implemented
 * as bits for easy multi-state checking.  The actual state can only be
 * one.
 */
#define __HLS_UP_INIT_BP	0
#define __HLS_UP_ARMED_BP	1
#define __HLS_UP_ACTIVE_BP	2
#define __HLS_DN_DOWNDEF_BP	3	/* link down default */
#define __HLS_DN_POLL_BP	4
#define __HLS_DN_DISABLE_BP	5
#define __HLS_DN_OFFLINE_BP	6
#define __HLS_VERIFY_CAP_BP	7
#define __HLS_GOING_UP_BP	8
#define __HLS_GOING_OFFLINE_BP  9
#define __HLS_LINK_COOLDOWN_BP 10

#define HLS_UP_INIT	  BIT(__HLS_UP_INIT_BP)
#define HLS_UP_ARMED	  BIT(__HLS_UP_ARMED_BP)
#define HLS_UP_ACTIVE	  BIT(__HLS_UP_ACTIVE_BP)
#define HLS_DN_DOWNDEF	  BIT(__HLS_DN_DOWNDEF_BP) /* link down default */
#define HLS_DN_POLL	  BIT(__HLS_DN_POLL_BP)
#define HLS_DN_DISABLE	  BIT(__HLS_DN_DISABLE_BP)
#define HLS_DN_OFFLINE	  BIT(__HLS_DN_OFFLINE_BP)
#define HLS_VERIFY_CAP	  BIT(__HLS_VERIFY_CAP_BP)
#define HLS_GOING_UP	  BIT(__HLS_GOING_UP_BP)
#define HLS_GOING_OFFLINE BIT(__HLS_GOING_OFFLINE_BP)
#define HLS_LINK_COOLDOWN BIT(__HLS_LINK_COOLDOWN_BP)

#define HLS_UP (HLS_UP_INIT | HLS_UP_ARMED | HLS_UP_ACTIVE)
#define HLS_DOWN ~(HLS_UP)

#define HLS_DEFAULT HLS_DN_POLL

/* use this MTU size if none other is given */
#define HFI1_DEFAULT_ACTIVE_MTU 10240
/* use this MTU size as the default maximum */
#define HFI1_DEFAULT_MAX_MTU 10240
/* default partition key */
#define DEFAULT_PKEY 0xffff

/*
 * Possible fabric manager config parameters for fm_{get,set}_table()
 */
#define FM_TBL_VL_HIGH_ARB		1 /* Get/set VL high prio weights */
#define FM_TBL_VL_LOW_ARB		2 /* Get/set VL low prio weights */
#define FM_TBL_BUFFER_CONTROL		3 /* Get/set Buffer Control */
#define FM_TBL_SC2VLNT			4 /* Get/set SC->VLnt */
#define FM_TBL_VL_PREEMPT_ELEMS		5 /* Get (no set) VL preempt elems */
#define FM_TBL_VL_PREEMPT_MATRIX	6 /* Get (no set) VL preempt matrix */

/*
 * Possible "operations" for f_rcvctrl(ppd, op, ctxt)
 * these are bits so they can be combined, e.g.
 * HFI1_RCVCTRL_INTRAVAIL_ENB | HFI1_RCVCTRL_CTXT_ENB
 */
#define HFI1_RCVCTRL_TAILUPD_ENB 0x01
#define HFI1_RCVCTRL_TAILUPD_DIS 0x02
#define HFI1_RCVCTRL_CTXT_ENB 0x04
#define HFI1_RCVCTRL_CTXT_DIS 0x08
#define HFI1_RCVCTRL_INTRAVAIL_ENB 0x10
#define HFI1_RCVCTRL_INTRAVAIL_DIS 0x20
#define HFI1_RCVCTRL_PKEY_ENB 0x40  /* Note, default is enabled */
#define HFI1_RCVCTRL_PKEY_DIS 0x80
#define HFI1_RCVCTRL_TIDFLOW_ENB 0x0400
#define HFI1_RCVCTRL_TIDFLOW_DIS 0x0800
#define HFI1_RCVCTRL_ONE_PKT_EGR_ENB 0x1000
#define HFI1_RCVCTRL_ONE_PKT_EGR_DIS 0x2000
#define HFI1_RCVCTRL_NO_RHQ_DROP_ENB 0x4000
#define HFI1_RCVCTRL_NO_RHQ_DROP_DIS 0x8000
#define HFI1_RCVCTRL_NO_EGR_DROP_ENB 0x10000
#define HFI1_RCVCTRL_NO_EGR_DROP_DIS 0x20000
#define HFI1_RCVCTRL_URGENT_ENB 0x40000
#define HFI1_RCVCTRL_URGENT_DIS 0x80000

/* partition enforcement flags */
#define HFI1_PART_ENFORCE_IN	0x1
#define HFI1_PART_ENFORCE_OUT	0x2

/* how often we check for synthetic counter wrap around */
#define SYNTH_CNT_TIME 3

/* Counter flags */
#define CNTR_NORMAL		0x0 /* Normal counters, just read register */
#define CNTR_SYNTH		0x1 /* Synthetic counters, saturate at all 1s */
#define CNTR_DISABLED		0x2 /* Disable this counter */
#define CNTR_32BIT		0x4 /* Simulate 64 bits for this counter */
#define CNTR_VL			0x8 /* Per VL counter */
#define CNTR_SDMA              0x10
#define CNTR_INVALID_VL		-1  /* Specifies invalid VL */
#define CNTR_MODE_W		0x0
#define CNTR_MODE_R		0x1

/* VLs Supported/Operational */
#define HFI1_MIN_VLS_SUPPORTED 1
#define HFI1_MAX_VLS_SUPPORTED 8

#define HFI1_GUIDS_PER_PORT  5
#define HFI1_PORT_GUID_INDEX 0

static inline void incr_cntr64(u64 *cntr)
{
	if (*cntr < (u64)-1LL)
		(*cntr)++;
}

#define MAX_NAME_SIZE 64
struct hfi1_msix_entry {
	enum irq_type type;
	int irq;
	void *arg;
	cpumask_t mask;
	struct irq_affinity_notify notify;
};

struct hfi1_msix_info {
	/* lock to synchronize in_use_msix access */
	spinlock_t msix_lock;
	DECLARE_BITMAP(in_use_msix, CCE_NUM_MSIX_VECTORS);
	struct hfi1_msix_entry *msix_entries;
	u16 max_requested;
};

/* per-SL CCA information */
struct cca_timer {
	struct hrtimer hrtimer;
	struct hfi1_pportdata *ppd; /* read-only */
	int sl; /* read-only */
	u16 ccti; /* read/write - current value of CCTI */
};

struct link_down_reason {
	/*
	 * SMA-facing value.  Should be set from .latest when
	 * HLS_UP_* -> HLS_DN_* transition actually occurs.
	 */
	u8 sma;
	u8 latest;
};

enum {
	LO_PRIO_TABLE,
	HI_PRIO_TABLE,
	MAX_PRIO_TABLE
};

struct vl_arb_cache {
	/* protect vl arb cache */
	spinlock_t lock;
	struct ib_vl_weight_elem table[VL_ARB_TABLE_SIZE];
};

/*
 * The structure below encapsulates data relevant to a physical IB Port.
 * Current chips support only one such port, but the separation
 * clarifies things a bit. Note that to conform to IB conventions,
 * port-numbers are one-based. The first or only port is port1.
 */
struct hfi1_pportdata {
	struct hfi1_ibport ibport_data;

	struct hfi1_devdata *dd;

	/* PHY support */
	struct qsfp_data qsfp_info;
	/* Values for SI tuning of SerDes */
	u32 port_type;
	u32 tx_preset_eq;
	u32 tx_preset_noeq;
	u32 rx_preset;
	u8  local_atten;
	u8  remote_atten;
	u8  default_atten;
	u8  max_power_class;

	/* did we read platform config from scratch registers? */
	bool config_from_scratch;

	/* GUIDs for this interface, in host order, guids[0] is a port guid */
	u64 guids[HFI1_GUIDS_PER_PORT];

	/* GUID for peer interface, in host order */
	u64 neighbor_guid;

	/* up or down physical link state */
	u32 linkup;

	/*
	 * this address is mapped read-only into user processes so they can
	 * get status cheaply, whenever they want.  One qword of status per port
	 */
	u64 *statusp;

	/* SendDMA related entries */

	struct workqueue_struct *hfi1_wq;
	struct workqueue_struct *link_wq;

	/* move out of interrupt context */
	struct work_struct link_vc_work;
	struct work_struct link_up_work;
	struct work_struct link_down_work;
	struct work_struct sma_message_work;
	struct work_struct freeze_work;
	struct work_struct link_downgrade_work;
	struct work_struct link_bounce_work;
	struct delayed_work start_link_work;
	/* host link state variables */
	struct mutex hls_lock;
	u32 host_link_state;

	/* these are the "32 bit" regs */

	u32 ibmtu; /* The MTU programmed for this unit */
	/*
	 * Current max size IB packet (in bytes) including IB headers, that
	 * we can send. Changes when ibmtu changes.
	 */
	u32 ibmaxlen;
	u32 current_egress_rate; /* units [10^6 bits/sec] */
	/* LID programmed for this instance */
	u32 lid;
	/* list of pkeys programmed; 0 if not set */
	u16 pkeys[MAX_PKEY_VALUES];
	u16 link_width_supported;
	u16 link_width_downgrade_supported;
	u16 link_speed_supported;
	u16 link_width_enabled;
	u16 link_width_downgrade_enabled;
	u16 link_speed_enabled;
	u16 link_width_active;
	u16 link_width_downgrade_tx_active;
	u16 link_width_downgrade_rx_active;
	u16 link_speed_active;
	u8 vls_supported;
	u8 vls_operational;
	u8 actual_vls_operational;
	/* LID mask control */
	u8 lmc;
	/* Rx Polarity inversion (compensate for ~tx on partner) */
	u8 rx_pol_inv;

	u8 hw_pidx;     /* physical port index */
	u32 port;        /* IB port number and index into dd->pports - 1 */
	/* type of neighbor node */
	u8 neighbor_type;
	u8 neighbor_normal;
	u8 neighbor_fm_security; /* 1 if firmware checking is disabled */
	u8 neighbor_port_number;
	u8 is_sm_config_started;
	u8 offline_disabled_reason;
	u8 is_active_optimize_enabled;
	u8 driver_link_ready;	/* driver ready for active link */
	u8 link_enabled;	/* link enabled? */
	u8 linkinit_reason;
	u8 local_tx_rate;	/* rate given to 8051 firmware */
	u8 qsfp_retry_count;

	/* placeholders for IB MAD packet settings */
	u8 overrun_threshold;
	u8 phy_error_threshold;
	unsigned int is_link_down_queued;

	/* Used to override LED behavior for things like maintenance beaconing*/
	/*
	 * Alternates per phase of blink
	 * [0] holds LED off duration, [1] holds LED on duration
	 */
	unsigned long led_override_vals[2];
	u8 led_override_phase; /* LSB picks from vals[] */
	atomic_t led_override_timer_active;
	/* Used to flash LEDs in override mode */
	struct timer_list led_override_timer;

	u32 sm_trap_qp;
	u32 sa_qp;

	/*
	 * cca_timer_lock protects access to the per-SL cca_timer
	 * structures (specifically the ccti member).
	 */
	spinlock_t cca_timer_lock ____cacheline_aligned_in_smp;
	struct cca_timer cca_timer[OPA_MAX_SLS];

	/* List of congestion control table entries */
	struct ib_cc_table_entry_shadow ccti_entries[CC_TABLE_SHADOW_MAX];

	/* congestion entries, each entry corresponding to a SL */
	struct opa_congestion_setting_entry_shadow
		congestion_entries[OPA_MAX_SLS];

	/*
	 * cc_state_lock protects (write) access to the per-port
	 * struct cc_state.
	 */
	spinlock_t cc_state_lock ____cacheline_aligned_in_smp;

	struct cc_state __rcu *cc_state;

	/* Total number of congestion control table entries */
	u16 total_cct_entry;

	/* Bit map identifying service level */
	u32 cc_sl_control_map;

	/* CA's max number of 64 entry units in the congestion control table */
	u8 cc_max_table_entries;

	/*
	 * begin congestion log related entries
	 * cc_log_lock protects all congestion log related data
	 */
	spinlock_t cc_log_lock ____cacheline_aligned_in_smp;
	u8 threshold_cong_event_map[OPA_MAX_SLS / 8];
	u16 threshold_event_counter;
	struct opa_hfi1_cong_log_event_internal cc_events[OPA_CONG_LOG_ELEMS];
	int cc_log_idx; /* index for logging events */
	int cc_mad_idx; /* index for reporting events */
	/* end congestion log related entries */

	struct vl_arb_cache vl_arb_cache[MAX_PRIO_TABLE];

	/* port relative counter buffer */
	u64 *cntrs;
	/* port relative synthetic counter buffer */
	u64 *scntrs;
	/* port_xmit_discards are synthesized from different egress errors */
	u64 port_xmit_discards;
	u64 port_xmit_discards_vl[C_VL_COUNT];
	u64 port_xmit_constraint_errors;
	u64 port_rcv_constraint_errors;
	/* count of 'link_err' interrupts from DC */
	u64 link_downed;
	/* number of times link retrained successfully */
	u64 link_up;
	/* number of times a link unknown frame was reported */
	u64 unknown_frame_count;
	/* port_ltp_crc_mode is returned in 'portinfo' MADs */
	u16 port_ltp_crc_mode;
	/* port_crc_mode_enabled is the crc we support */
	u8 port_crc_mode_enabled;
	/* mgmt_allowed is also returned in 'portinfo' MADs */
	u8 mgmt_allowed;
	u8 part_enforce; /* partition enforcement flags */
	struct link_down_reason local_link_down_reason;
	struct link_down_reason neigh_link_down_reason;
	/* Value to be sent to link peer on LinkDown .*/
	u8 remote_link_down_reason;
	/* Error events that will cause a port bounce. */
	u32 port_error_action;
	struct work_struct linkstate_active_work;
	/* Does this port need to prescan for FECNs */
	bool cc_prescan;
	/*
	 * Sample sendWaitCnt & sendWaitVlCnt during link transition
	 * and counter request.
	 */
	u64 port_vl_xmit_wait_last[C_VL_COUNT + 1];
	u16 prev_link_width;
	u64 vl_xmit_flit_cnt[C_VL_COUNT + 1];
};

typedef void (*opcode_handler)(struct hfi1_packet *packet);
typedef void (*hfi1_make_req)(struct rvt_qp *qp,
			      struct hfi1_pkt_state *ps,
			      struct rvt_swqe *wqe);
extern const rhf_rcv_function_ptr normal_rhf_rcv_functions[];
extern const rhf_rcv_function_ptr netdev_rhf_rcv_functions[];

/* return values for the RHF receive functions */
#define RHF_RCV_CONTINUE  0	/* keep going */
#define RHF_RCV_DONE	  1	/* stop, this packet processed */
#define RHF_RCV_REPROCESS 2	/* stop. retain this packet */

struct rcv_array_data {
	u16 ngroups;
	u16 nctxt_extra;
	u8 group_size;
};

struct per_vl_data {
	u16 mtu;
	struct send_context *sc;
};

/* 16 to directly index */
#define PER_VL_SEND_CONTEXTS 16

struct err_info_rcvport {
	u8 status_and_code;
	u64 packet_flit1;
	u64 packet_flit2;
};

struct err_info_constraint {
	u8 status;
	u16 pkey;
	u32 slid;
};

struct hfi1_temp {
	unsigned int curr;       /* current temperature */
	unsigned int lo_lim;     /* low temperature limit */
	unsigned int hi_lim;     /* high temperature limit */
	unsigned int crit_lim;   /* critical temperature limit */
	u8 triggers;      /* temperature triggers */
};

struct hfi1_i2c_bus {
	struct hfi1_devdata *controlling_dd; /* current controlling device */
	struct i2c_adapter adapter;	/* bus details */
	struct i2c_algo_bit_data algo;	/* bus algorithm details */
	int num;			/* bus number, 0 or 1 */
};

/* common data between shared ASIC HFIs */
struct hfi1_asic_data {
	struct hfi1_devdata *dds[2];	/* back pointers */
	struct mutex asic_resource_mutex;
	struct hfi1_i2c_bus *i2c_bus0;
	struct hfi1_i2c_bus *i2c_bus1;
};

/* sizes for both the QP and RSM map tables */
#define NUM_MAP_ENTRIES	 256
#define NUM_MAP_REGS      32

/* Virtual NIC information */
struct hfi1_vnic_data {
	struct kmem_cache *txreq_cache;
	u8 num_vports;
};

struct hfi1_vnic_vport_info;

/* device data struct now contains only "general per-device" info.
 * fields related to a physical IB port are in a hfi1_pportdata struct.
 */
struct sdma_engine;
struct sdma_vl_map;

#define BOARD_VERS_MAX 96 /* how long the version string can be */
#define SERIAL_MAX 16 /* length of the serial number */

typedef int (*send_routine)(struct rvt_qp *, struct hfi1_pkt_state *, u64);
struct hfi1_netdev_rx;
struct hfi1_devdata {
	struct hfi1_ibdev verbs_dev;     /* must be first */
	/* pointers to related structs for this device */
	/* pci access data structure */
	struct pci_dev *pcidev;
	struct cdev user_cdev;
	struct cdev diag_cdev;
	struct cdev ui_cdev;
	struct device *user_device;
	struct device *diag_device;
	struct device *ui_device;

	/* first mapping up to RcvArray */
	u8 __iomem *kregbase1;
	resource_size_t physaddr;

	/* second uncached mapping from RcvArray to pio send buffers */
	u8 __iomem *kregbase2;
	/* for detecting offset above kregbase2 address */
	u32 base2_start;

	/* Per VL data. Enough for all VLs but not all elements are set/used. */
	struct per_vl_data vld[PER_VL_SEND_CONTEXTS];
	/* send context data */
	struct send_context_info *send_contexts;
	/* map hardware send contexts to software index */
	u8 *hw_to_sw;
	/* spinlock for allocating and releasing send context resources */
	spinlock_t sc_lock;
	/* lock for pio_map */
	spinlock_t pio_map_lock;
	/* Send Context initialization lock. */
	spinlock_t sc_init_lock;
	/* lock for sdma_map */
	spinlock_t                          sde_map_lock;
	/* array of kernel send contexts */
	struct send_context **kernel_send_context;
	/* array of vl maps */
	struct pio_vl_map __rcu *pio_map;
	/* default flags to last descriptor */
	u64 default_desc1;

	/* fields common to all SDMA engines */

	volatile __le64                    *sdma_heads_dma; /* DMA'ed by chip */
	dma_addr_t                          sdma_heads_phys;
	void                               *sdma_pad_dma; /* DMA'ed by chip */
	dma_addr_t                          sdma_pad_phys;
	/* for deallocation */
	size_t                              sdma_heads_size;
	/* num used */
	u32                                 num_sdma;
	/* array of engines sized by num_sdma */
	struct sdma_engine                 *per_sdma;
	/* array of vl maps */
	struct sdma_vl_map __rcu           *sdma_map;
	/* SPC freeze waitqueue and variable */
	wait_queue_head_t		  sdma_unfreeze_wq;
	atomic_t			  sdma_unfreeze_count;

	u32 lcb_access_count;		/* count of LCB users */

	/* common data between shared ASIC HFIs in this OS */
	struct hfi1_asic_data *asic_data;

	/* mem-mapped pointer to base of PIO buffers */
	void __iomem *piobase;
	/*
	 * write-combining mem-mapped pointer to base of RcvArray
	 * memory.
	 */
	void __iomem *rcvarray_wc;
	/*
	 * credit return base - a per-NUMA range of DMA address that
	 * the chip will use to update the per-context free counter
	 */
	struct credit_return_base *cr_base;

	/* send context numbers and sizes for each type */
	struct sc_config_sizes sc_sizes[SC_MAX];

	char *boardname; /* human readable board info */

	u64 ctx0_seq_drop;

	/* reset value */
	u64 z_int_counter;
	u64 z_rcv_limit;
	u64 z_send_schedule;

	u64 __percpu *send_schedule;
	/* number of reserved contexts for netdev usage */
	u16 num_netdev_contexts;
	/* number of receive contexts in use by the driver */
	u32 num_rcv_contexts;
	/* number of pio send contexts in use by the driver */
	u32 num_send_contexts;
	/*
	 * number of ctxts available for PSM open
	 */
	u32 freectxts;
	/* total number of available user/PSM contexts */
	u32 num_user_contexts;
	/* base receive interrupt timeout, in CSR units */
	u32 rcv_intr_timeout_csr;

	spinlock_t sendctrl_lock; /* protect changes to SendCtrl */
	spinlock_t rcvctrl_lock; /* protect changes to RcvCtrl */
	spinlock_t uctxt_lock; /* protect rcd changes */
	struct mutex dc8051_lock; /* exclusive access to 8051 */
	struct workqueue_struct *update_cntr_wq;
	struct work_struct update_cntr_work;
	/* exclusive access to 8051 memory */
	spinlock_t dc8051_memlock;
	int dc8051_timed_out;	/* remember if the 8051 timed out */
	/*
	 * A page that will hold event notification bitmaps for all
	 * contexts. This page will be mapped into all processes.
	 */
	unsigned long *events;
	/*
	 * per unit status, see also portdata statusp
	 * mapped read-only into user processes so they can get unit and
	 * IB link status cheaply
	 */
	struct hfi1_status *status;

	/* revision register shadow */
	u64 revision;
	/* Base GUID for device (network order) */
	u64 base_guid;

	/* both sides of the PCIe link are gen3 capable */
	u8 link_gen3_capable;
	u8 dc_shutdown;
	/* localbus width (1, 2,4,8,16,32) from config space  */
	u32 lbus_width;
	/* localbus speed in MHz */
	u32 lbus_speed;
	int unit; /* unit # of this chip */
	int node; /* home node of this chip */

	/* save these PCI fields to restore after a reset */
	u32 pcibar0;
	u32 pcibar1;
	u32 pci_rom;
	u16 pci_command;
	u16 pcie_devctl;
	u16 pcie_lnkctl;
	u16 pcie_devctl2;
	u32 pci_msix0;
	u32 pci_tph2;

	/*
	 * ASCII serial number, from flash, large enough for original
	 * all digit strings, and longer serial number format
	 */
	u8 serial[SERIAL_MAX];
	/* human readable board version */
	u8 boardversion[BOARD_VERS_MAX];
	u8 lbus_info[32]; /* human readable localbus info */
	/* chip major rev, from CceRevision */
	u8 majrev;
	/* chip minor rev, from CceRevision */
	u8 minrev;
	/* hardware ID */
	u8 hfi1_id;
	/* implementation code */
	u8 icode;
	/* vAU of this device */
	u8 vau;
	/* vCU of this device */
	u8 vcu;
	/* link credits of this device */
	u16 link_credits;
	/* initial vl15 credits to use */
	u16 vl15_init;

	/*
	 * Cached value for vl15buf, read during verify cap interrupt. VL15
	 * credits are to be kept at 0 and set when handling the link-up
	 * interrupt. This removes the possibility of receiving VL15 MAD
	 * packets before this HFI is ready.
	 */
	u16 vl15buf_cached;

	/* Misc small ints */
	u8 n_krcv_queues;
	u8 qos_shift;

	u16 irev;	/* implementation revision */
	u32 dc8051_ver; /* 8051 firmware version */

	spinlock_t hfi1_diag_trans_lock; /* protect diag observer ops */
	struct platform_config platform_config;
	struct platform_config_cache pcfg_cache;

	struct diag_client *diag_client;

	/* general interrupt: mask of handled interrupts */
	u64 gi_mask[CCE_NUM_INT_CSRS];

	struct rcv_array_data rcv_entries;

	/* cycle length of PS* counters in HW (in picoseconds) */
	u16 psxmitwait_check_rate;

	/*
	 * 64 bit synthetic counters
	 */
	struct timer_list synth_stats_timer;

	/* MSI-X information */
	struct hfi1_msix_info msix_info;

	/*
	 * device counters
	 */
	char *cntrnames;
	size_t cntrnameslen;
	size_t ndevcntrs;
	u64 *cntrs;
	u64 *scntrs;

	/*
	 * remembered values for synthetic counters
	 */
	u64 last_tx;
	u64 last_rx;

	/*
	 * per-port counters
	 */
	size_t nportcntrs;
	char *portcntrnames;
	size_t portcntrnameslen;

	struct err_info_rcvport err_info_rcvport;
	struct err_info_constraint err_info_rcv_constraint;
	struct err_info_constraint err_info_xmit_constraint;

	atomic_t drop_packet;
	bool do_drop;
	u8 err_info_uncorrectable;
	u8 err_info_fmconfig;

	/*
	 * Software counters for the status bits defined by the
	 * associated error status registers
	 */
	u64 cce_err_status_cnt[NUM_CCE_ERR_STATUS_COUNTERS];
	u64 rcv_err_status_cnt[NUM_RCV_ERR_STATUS_COUNTERS];
	u64 misc_err_status_cnt[NUM_MISC_ERR_STATUS_COUNTERS];
	u64 send_pio_err_status_cnt[NUM_SEND_PIO_ERR_STATUS_COUNTERS];
	u64 send_dma_err_status_cnt[NUM_SEND_DMA_ERR_STATUS_COUNTERS];
	u64 send_egress_err_status_cnt[NUM_SEND_EGRESS_ERR_STATUS_COUNTERS];
	u64 send_err_status_cnt[NUM_SEND_ERR_STATUS_COUNTERS];

	/* Software counter that spans all contexts */
	u64 sw_ctxt_err_status_cnt[NUM_SEND_CTXT_ERR_STATUS_COUNTERS];
	/* Software counter that spans all DMA engines */
	u64 sw_send_dma_eng_err_status_cnt[
		NUM_SEND_DMA_ENG_ERR_STATUS_COUNTERS];
	/* Software counter that aggregates all cce_err_status errors */
	u64 sw_cce_err_status_aggregate;
	/* Software counter that aggregates all bypass packet rcv errors */
	u64 sw_rcv_bypass_packet_errors;

	/* Save the enabled LCB error bits */
	u64 lcb_err_en;
	struct cpu_mask_set *comp_vect;
	int *comp_vect_mappings;
	u32 comp_vect_possible_cpus;

	/*
	 * Capability to have different send engines simply by changing a
	 * pointer value.
	 */
	send_routine process_pio_send ____cacheline_aligned_in_smp;
	send_routine process_dma_send;
	void (*pio_inline_send)(struct hfi1_devdata *dd, struct pio_buf *pbuf,
				u64 pbc, const void *from, size_t count);
	int (*process_vnic_dma_send)(struct hfi1_devdata *dd, u8 q_idx,
				     struct hfi1_vnic_vport_info *vinfo,
				     struct sk_buff *skb, u64 pbc, u8 plen);
	/* hfi1_pportdata, points to array of (physical) port-specific
	 * data structs, indexed by pidx (0..n-1)
	 */
	struct hfi1_pportdata *pport;
	/* receive context data */
	struct hfi1_ctxtdata **rcd;
	u64 __percpu *int_counter;
	/* verbs tx opcode stats */
	struct hfi1_opcode_stats_perctx __percpu *tx_opstats;
	/* device (not port) flags, basically device capabilities */
	u16 flags;
	/* Number of physical ports available */
	u8 num_pports;
	/* Lowest context number which can be used by user processes or VNIC */
	u8 first_dyn_alloc_ctxt;
	/* adding a new field here would make it part of this cacheline */

	/* seqlock for sc2vl */
	seqlock_t sc2vl_lock ____cacheline_aligned_in_smp;
	u64 sc2vl[4];
	u64 __percpu *rcv_limit;
	/* adding a new field here would make it part of this cacheline */

	/* OUI comes from the HW. Used everywhere as 3 separate bytes. */
	u8 oui1;
	u8 oui2;
	u8 oui3;

	/* Timer and counter used to detect RcvBufOvflCnt changes */
	struct timer_list rcverr_timer;

	wait_queue_head_t event_queue;

	/* receive context tail dummy address */
	__le64 *rcvhdrtail_dummy_kvaddr;
	dma_addr_t rcvhdrtail_dummy_dma;

	u32 rcv_ovfl_cnt;
	/* Serialize ASPM enable/disable between multiple verbs contexts */
	spinlock_t aspm_lock;
	/* Number of verbs contexts which have disabled ASPM */
	atomic_t aspm_disabled_cnt;
	/* Keeps track of user space clients */
	refcount_t user_refcount;
	/* Used to wait for outstanding user space clients before dev removal */
	struct completion user_comp;

	bool eprom_available;	/* true if EPROM is available for this device */
	bool aspm_supported;	/* Does HW support ASPM */
	bool aspm_enabled;	/* ASPM state: enabled/disabled */
	struct rhashtable *sdma_rht;

	/* vnic data */
	struct hfi1_vnic_data vnic;
	/* Lock to protect IRQ SRC register access */
	spinlock_t irq_src_lock;
	int vnic_num_vports;
	struct hfi1_netdev_rx *netdev_rx;
	struct hfi1_affinity_node *affinity_entry;

	/* Keeps track of IPoIB RSM rule users */
	atomic_t ipoib_rsm_usr_num;
};

/* 8051 firmware version helper */
#define dc8051_ver(a, b, c) ((a) << 16 | (b) << 8 | (c))
#define dc8051_ver_maj(a) (((a) & 0xff0000) >> 16)
#define dc8051_ver_min(a) (((a) & 0x00ff00) >> 8)
#define dc8051_ver_patch(a) ((a) & 0x0000ff)

/* f_put_tid types */
#define PT_EXPECTED       0
#define PT_EAGER          1
#define PT_INVALID_FLUSH  2
#define PT_INVALID        3

struct tid_rb_node;

/* Private data for file operations */
struct hfi1_filedata {
	struct srcu_struct pq_srcu;
	struct hfi1_devdata *dd;
	struct hfi1_ctxtdata *uctxt;
	struct hfi1_user_sdma_comp_q *cq;
	/* update side lock for SRCU */
	spinlock_t pq_rcu_lock;
	struct hfi1_user_sdma_pkt_q __rcu *pq;
	u16 subctxt;
	/* for cpu affinity; -1 if none */
	int rec_cpu_num;
	u32 tid_n_pinned;
	bool use_mn;
	struct tid_rb_node **entry_to_rb;
	spinlock_t tid_lock; /* protect tid_[limit,used] counters */
	u32 tid_limit;
	u32 tid_used;
	u32 *invalid_tids;
	u32 invalid_tid_idx;
	/* protect invalid_tids array and invalid_tid_idx */
	spinlock_t invalid_lock;
};

extern struct xarray hfi1_dev_table;
struct hfi1_devdata *hfi1_lookup(int unit);

static inline unsigned long uctxt_offset(struct hfi1_ctxtdata *uctxt)
{
	return (uctxt->ctxt - uctxt->dd->first_dyn_alloc_ctxt) *
		HFI1_MAX_SHARED_CTXTS;
}

int hfi1_init(struct hfi1_devdata *dd, int reinit);
int hfi1_count_active_units(void);

int hfi1_diag_add(struct hfi1_devdata *dd);
void hfi1_diag_remove(struct hfi1_devdata *dd);
void handle_linkup_change(struct hfi1_devdata *dd, u32 linkup);

void handle_user_interrupt(struct hfi1_ctxtdata *rcd);

int hfi1_create_rcvhdrq(struct hfi1_devdata *dd, struct hfi1_ctxtdata *rcd);
int hfi1_setup_eagerbufs(struct hfi1_ctxtdata *rcd);
int hfi1_create_kctxts(struct hfi1_devdata *dd);
int hfi1_create_ctxtdata(struct hfi1_pportdata *ppd, int numa,
			 struct hfi1_ctxtdata **rcd);
void hfi1_free_ctxt(struct hfi1_ctxtdata *rcd);
void hfi1_init_pportdata(struct pci_dev *pdev, struct hfi1_pportdata *ppd,
			 struct hfi1_devdata *dd, u8 hw_pidx, u32 port);
void hfi1_free_ctxtdata(struct hfi1_devdata *dd, struct hfi1_ctxtdata *rcd);
int hfi1_rcd_put(struct hfi1_ctxtdata *rcd);
int hfi1_rcd_get(struct hfi1_ctxtdata *rcd);
struct hfi1_ctxtdata *hfi1_rcd_get_by_index_safe(struct hfi1_devdata *dd,
						 u16 ctxt);
struct hfi1_ctxtdata *hfi1_rcd_get_by_index(struct hfi1_devdata *dd, u16 ctxt);
int handle_receive_interrupt(struct hfi1_ctxtdata *rcd, int thread);
int handle_receive_interrupt_nodma_rtail(struct hfi1_ctxtdata *rcd, int thread);
int handle_receive_interrupt_dma_rtail(struct hfi1_ctxtdata *rcd, int thread);
int handle_receive_interrupt_napi_fp(struct hfi1_ctxtdata *rcd, int budget);
int handle_receive_interrupt_napi_sp(struct hfi1_ctxtdata *rcd, int budget);
void set_all_slowpath(struct hfi1_devdata *dd);

extern const struct pci_device_id hfi1_pci_tbl[];
void hfi1_make_ud_req_9B(struct rvt_qp *qp,
			 struct hfi1_pkt_state *ps,
			 struct rvt_swqe *wqe);

void hfi1_make_ud_req_16B(struct rvt_qp *qp,
			  struct hfi1_pkt_state *ps,
			  struct rvt_swqe *wqe);

/* receive packet handler dispositions */
#define RCV_PKT_OK      0x0 /* keep going */
#define RCV_PKT_LIMIT   0x1 /* stop, hit limit, start thread */
#define RCV_PKT_DONE    0x2 /* stop, no more packets detected */

/**
 * hfi1_rcd_head - add accessor for rcd head
 * @rcd: the context
 */
static inline u32 hfi1_rcd_head(struct hfi1_ctxtdata *rcd)
{
	return rcd->head;
}

/**
 * hfi1_set_rcd_head - add accessor for rcd head
 * @rcd: the context
 * @head: the new head
 */
static inline void hfi1_set_rcd_head(struct hfi1_ctxtdata *rcd, u32 head)
{
	rcd->head = head;
}

/* calculate the current RHF address */
static inline __le32 *get_rhf_addr(struct hfi1_ctxtdata *rcd)
{
	return (__le32 *)rcd->rcvhdrq + rcd->head + rcd->rhf_offset;
}

/* return DMA_RTAIL configuration */
static inline bool get_dma_rtail_setting(struct hfi1_ctxtdata *rcd)
{
	return !!HFI1_CAP_KGET_MASK(rcd->flags, DMA_RTAIL);
}

/**
 * hfi1_seq_incr_wrap - wrapping increment for sequence
 * @seq: the current sequence number
 *
 * Returns: the incremented seq
 */
static inline u8 hfi1_seq_incr_wrap(u8 seq)
{
	if (++seq > RHF_MAX_SEQ)
		seq = 1;
	return seq;
}

/**
 * hfi1_seq_cnt - return seq_cnt member
 * @rcd: the receive context
 *
 * Return seq_cnt member
 */
static inline u8 hfi1_seq_cnt(struct hfi1_ctxtdata *rcd)
{
	return rcd->seq_cnt;
}

/**
 * hfi1_set_seq_cnt - return seq_cnt member
 * @rcd: the receive context
 *
 * Return seq_cnt member
 */
static inline void hfi1_set_seq_cnt(struct hfi1_ctxtdata *rcd, u8 cnt)
{
	rcd->seq_cnt = cnt;
}

/**
 * last_rcv_seq - is last
 * @rcd: the receive context
 * @seq: sequence
 *
 * return true if last packet
 */
static inline bool last_rcv_seq(struct hfi1_ctxtdata *rcd, u32 seq)
{
	return seq != rcd->seq_cnt;
}

/**
 * rcd_seq_incr - increment context sequence number
 * @rcd: the receive context
 * @seq: the current sequence number
 *
 * Returns: true if the this was the last packet
 */
static inline bool hfi1_seq_incr(struct hfi1_ctxtdata *rcd, u32 seq)
{
	rcd->seq_cnt = hfi1_seq_incr_wrap(rcd->seq_cnt);
	return last_rcv_seq(rcd, seq);
}

/**
 * get_hdrqentsize - return hdrq entry size
 * @rcd: the receive context
 */
static inline u8 get_hdrqentsize(struct hfi1_ctxtdata *rcd)
{
	return rcd->rcvhdrqentsize;
}

/**
 * get_hdrq_cnt - return hdrq count
 * @rcd: the receive context
 */
static inline u16 get_hdrq_cnt(struct hfi1_ctxtdata *rcd)
{
	return rcd->rcvhdrq_cnt;
}

/**
 * hfi1_is_slowpath - check if this context is slow path
 * @rcd: the receive context
 */
static inline bool hfi1_is_slowpath(struct hfi1_ctxtdata *rcd)
{
	return rcd->do_interrupt == rcd->slow_handler;
}

/**
 * hfi1_is_fastpath - check if this context is fast path
 * @rcd: the receive context
 */
static inline bool hfi1_is_fastpath(struct hfi1_ctxtdata *rcd)
{
	if (rcd->ctxt == HFI1_CTRL_CTXT)
		return false;

	return rcd->do_interrupt == rcd->fast_handler;
}

/**
 * hfi1_set_fast - change to the fast handler
 * @rcd: the receive context
 */
static inline void hfi1_set_fast(struct hfi1_ctxtdata *rcd)
{
	if (unlikely(!rcd))
		return;
	if (unlikely(!hfi1_is_fastpath(rcd)))
		rcd->do_interrupt = rcd->fast_handler;
}

int hfi1_reset_device(int);

void receive_interrupt_work(struct work_struct *work);

/* extract service channel from header and rhf */
static inline int hfi1_9B_get_sc5(struct ib_header *hdr, u64 rhf)
{
	return ib_get_sc(hdr) | ((!!(rhf_dc_info(rhf))) << 4);
}

#define HFI1_JKEY_WIDTH       16
#define HFI1_JKEY_MASK        (BIT(16) - 1)
#define HFI1_ADMIN_JKEY_RANGE 32

/*
 * J_KEYs are split and allocated in the following groups:
 *   0 - 31    - users with administrator privileges
 *  32 - 63    - kernel protocols using KDETH packets
 *  64 - 65535 - all other users using KDETH packets
 */
static inline u16 generate_jkey(kuid_t uid)
{
	u16 jkey = from_kuid(current_user_ns(), uid) & HFI1_JKEY_MASK;

	if (capable(CAP_SYS_ADMIN))
		jkey &= HFI1_ADMIN_JKEY_RANGE - 1;
	else if (jkey < 64)
		jkey |= BIT(HFI1_JKEY_WIDTH - 1);

	return jkey;
}

/*
 * active_egress_rate
 *
 * returns the active egress rate in units of [10^6 bits/sec]
 */
static inline u32 active_egress_rate(struct hfi1_pportdata *ppd)
{
	u16 link_speed = ppd->link_speed_active;
	u16 link_width = ppd->link_width_active;
	u32 egress_rate;

	if (link_speed == OPA_LINK_SPEED_25G)
		egress_rate = 25000;
	else /* assume OPA_LINK_SPEED_12_5G */
		egress_rate = 12500;

	switch (link_width) {
	case OPA_LINK_WIDTH_4X:
		egress_rate *= 4;
		break;
	case OPA_LINK_WIDTH_3X:
		egress_rate *= 3;
		break;
	case OPA_LINK_WIDTH_2X:
		egress_rate *= 2;
		break;
	default:
		/* assume IB_WIDTH_1X */
		break;
	}

	return egress_rate;
}

/*
 * egress_cycles
 *
 * Returns the number of 'fabric clock cycles' to egress a packet
 * of length 'len' bytes, at 'rate' Mbit/s. Since the fabric clock
 * rate is (approximately) 805 MHz, the units of the returned value
 * are (1/805 MHz).
 */
static inline u32 egress_cycles(u32 len, u32 rate)
{
	u32 cycles;

	/*
	 * cycles is:
	 *
	 *          (length) [bits] / (rate) [bits/sec]
	 *  ---------------------------------------------------
	 *  fabric_clock_period == 1 /(805 * 10^6) [cycles/sec]
	 */

	cycles = len * 8; /* bits */
	cycles *= 805;
	cycles /= rate;

	return cycles;
}

void set_link_ipg(struct hfi1_pportdata *ppd);
void process_becn(struct hfi1_pportdata *ppd, u8 sl, u32 rlid, u32 lqpn,
		  u32 rqpn, u8 svc_type);
void return_cnp(struct hfi1_ibport *ibp, struct rvt_qp *qp, u32 remote_qpn,
		u16 pkey, u32 slid, u32 dlid, u8 sc5,
		const struct ib_grh *old_grh);
void return_cnp_16B(struct hfi1_ibport *ibp, struct rvt_qp *qp,
		    u32 remote_qpn, u16 pkey, u32 slid, u32 dlid,
		    u8 sc5, const struct ib_grh *old_grh);
typedef void (*hfi1_handle_cnp)(struct hfi1_ibport *ibp, struct rvt_qp *qp,
				u32 remote_qpn, u16 pkey, u32 slid, u32 dlid,
				u8 sc5, const struct ib_grh *old_grh);

#define PKEY_CHECK_INVALID -1
int egress_pkey_check(struct hfi1_pportdata *ppd, u32 slid, u16 pkey,
		      u8 sc5, int8_t s_pkey_index);

#define PACKET_EGRESS_TIMEOUT 350
static inline void pause_for_credit_return(struct hfi1_devdata *dd)
{
	/* Pause at least 1us, to ensure chip returns all credits */
	u32 usec = cclock_to_ns(dd, PACKET_EGRESS_TIMEOUT) / 1000;

	udelay(usec ? usec : 1);
}

/**
 * sc_to_vlt() - reverse lookup sc to vl
 * @dd - devdata
 * @sc5 - 5 bit sc
 */
static inline u8 sc_to_vlt(struct hfi1_devdata *dd, u8 sc5)
{
	unsigned seq;
	u8 rval;

	if (sc5 >= OPA_MAX_SCS)
		return (u8)(0xff);

	do {
		seq = read_seqbegin(&dd->sc2vl_lock);
		rval = *(((u8 *)dd->sc2vl) + sc5);
	} while (read_seqretry(&dd->sc2vl_lock, seq));

	return rval;
}

#define PKEY_MEMBER_MASK 0x8000
#define PKEY_LOW_15_MASK 0x7fff

/*
 * ingress_pkey_matches_entry - return 1 if the pkey matches ent (ent
 * being an entry from the ingress partition key table), return 0
 * otherwise. Use the matching criteria for ingress partition keys
 * specified in the OPAv1 spec., section 9.10.14.
 */
static inline int ingress_pkey_matches_entry(u16 pkey, u16 ent)
{
	u16 mkey = pkey & PKEY_LOW_15_MASK;
	u16 ment = ent & PKEY_LOW_15_MASK;

	if (mkey == ment) {
		/*
		 * If pkey[15] is clear (limited partition member),
		 * is bit 15 in the corresponding table element
		 * clear (limited member)?
		 */
		if (!(pkey & PKEY_MEMBER_MASK))
			return !!(ent & PKEY_MEMBER_MASK);
		return 1;
	}
	return 0;
}

/*
 * ingress_pkey_table_search - search the entire pkey table for
 * an entry which matches 'pkey'. return 0 if a match is found,
 * and 1 otherwise.
 */
static int ingress_pkey_table_search(struct hfi1_pportdata *ppd, u16 pkey)
{
	int i;

	for (i = 0; i < MAX_PKEY_VALUES; i++) {
		if (ingress_pkey_matches_entry(pkey, ppd->pkeys[i]))
			return 0;
	}
	return 1;
}

/*
 * ingress_pkey_table_fail - record a failure of ingress pkey validation,
 * i.e., increment port_rcv_constraint_errors for the port, and record
 * the 'error info' for this failure.
 */
static void ingress_pkey_table_fail(struct hfi1_pportdata *ppd, u16 pkey,
				    u32 slid)
{
	struct hfi1_devdata *dd = ppd->dd;

	incr_cntr64(&ppd->port_rcv_constraint_errors);
	if (!(dd->err_info_rcv_constraint.status & OPA_EI_STATUS_SMASK)) {
		dd->err_info_rcv_constraint.status |= OPA_EI_STATUS_SMASK;
		dd->err_info_rcv_constraint.slid = slid;
		dd->err_info_rcv_constraint.pkey = pkey;
	}
}

/*
 * ingress_pkey_check - Return 0 if the ingress pkey is valid, return 1
 * otherwise. Use the criteria in the OPAv1 spec, section 9.10.14. idx
 * is a hint as to the best place in the partition key table to begin
 * searching. This function should not be called on the data path because
 * of performance reasons. On datapath pkey check is expected to be done
 * by HW and rcv_pkey_check function should be called instead.
 */
static inline int ingress_pkey_check(struct hfi1_pportdata *ppd, u16 pkey,
				     u8 sc5, u8 idx, u32 slid, bool force)
{
	if (!(force) && !(ppd->part_enforce & HFI1_PART_ENFORCE_IN))
		return 0;

	/* If SC15, pkey[0:14] must be 0x7fff */
	if ((sc5 == 0xf) && ((pkey & PKEY_LOW_15_MASK) != PKEY_LOW_15_MASK))
		goto bad;

	/* Is the pkey = 0x0, or 0x8000? */
	if ((pkey & PKEY_LOW_15_MASK) == 0)
		goto bad;

	/* The most likely matching pkey has index 'idx' */
	if (ingress_pkey_matches_entry(pkey, ppd->pkeys[idx]))
		return 0;

	/* no match - try the whole table */
	if (!ingress_pkey_table_search(ppd, pkey))
		return 0;

bad:
	ingress_pkey_table_fail(ppd, pkey, slid);
	return 1;
}

/*
 * rcv_pkey_check - Return 0 if the ingress pkey is valid, return 1
 * otherwise. It only ensures pkey is vlid for QP0. This function
 * should be called on the data path instead of ingress_pkey_check
 * as on data path, pkey check is done by HW (except for QP0).
 */
static inline int rcv_pkey_check(struct hfi1_pportdata *ppd, u16 pkey,
				 u8 sc5, u16 slid)
{
	if (!(ppd->part_enforce & HFI1_PART_ENFORCE_IN))
		return 0;

	/* If SC15, pkey[0:14] must be 0x7fff */
	if ((sc5 == 0xf) && ((pkey & PKEY_LOW_15_MASK) != PKEY_LOW_15_MASK))
		goto bad;

	return 0;
bad:
	ingress_pkey_table_fail(ppd, pkey, slid);
	return 1;
}

/* MTU handling */

/* MTU enumeration, 256-4k match IB */
#define OPA_MTU_0     0
#define OPA_MTU_256   1
#define OPA_MTU_512   2
#define OPA_MTU_1024  3
#define OPA_MTU_2048  4
#define OPA_MTU_4096  5

u32 lrh_max_header_bytes(struct hfi1_devdata *dd);
int mtu_to_enum(u32 mtu, int default_if_bad);
u16 enum_to_mtu(int mtu);
static inline int valid_ib_mtu(unsigned int mtu)
{
	return mtu == 256 || mtu == 512 ||
		mtu == 1024 || mtu == 2048 ||
		mtu == 4096;
}

static inline int valid_opa_max_mtu(unsigned int mtu)
{
	return mtu >= 2048 &&
		(valid_ib_mtu(mtu) || mtu == 8192 || mtu == 10240);
}

int set_mtu(struct hfi1_pportdata *ppd);

int hfi1_set_lid(struct hfi1_pportdata *ppd, u32 lid, u8 lmc);
void hfi1_disable_after_error(struct hfi1_devdata *dd);
int hfi1_set_uevent_bits(struct hfi1_pportdata *ppd, const int evtbit);
int hfi1_rcvbuf_validate(u32 size, u8 type, u16 *encode);

int fm_get_table(struct hfi1_pportdata *ppd, int which, void *t);
int fm_set_table(struct hfi1_pportdata *ppd, int which, void *t);

void set_up_vau(struct hfi1_devdata *dd, u8 vau);
void set_up_vl15(struct hfi1_devdata *dd, u16 vl15buf);
void reset_link_credits(struct hfi1_devdata *dd);
void assign_remote_cm_au_table(struct hfi1_devdata *dd, u8 vcu);

int set_buffer_control(struct hfi1_pportdata *ppd, struct buffer_control *bc);

static inline struct hfi1_devdata *dd_from_ppd(struct hfi1_pportdata *ppd)
{
	return ppd->dd;
}

static inline struct hfi1_devdata *dd_from_dev(struct hfi1_ibdev *dev)
{
	return container_of(dev, struct hfi1_devdata, verbs_dev);
}

static inline struct hfi1_devdata *dd_from_ibdev(struct ib_device *ibdev)
{
	return dd_from_dev(to_idev(ibdev));
}

static inline struct hfi1_pportdata *ppd_from_ibp(struct hfi1_ibport *ibp)
{
	return container_of(ibp, struct hfi1_pportdata, ibport_data);
}

static inline struct hfi1_ibdev *dev_from_rdi(struct rvt_dev_info *rdi)
{
	return container_of(rdi, struct hfi1_ibdev, rdi);
}

static inline struct hfi1_ibport *to_iport(struct ib_device *ibdev, u32 port)
{
	struct hfi1_devdata *dd = dd_from_ibdev(ibdev);
	u32 pidx = port - 1; /* IB number port from 1, hdw from 0 */

	WARN_ON(pidx >= dd->num_pports);
	return &dd->pport[pidx].ibport_data;
}

static inline struct hfi1_ibport *rcd_to_iport(struct hfi1_ctxtdata *rcd)
{
	return &rcd->ppd->ibport_data;
}

/**
 * hfi1_may_ecn - Check whether FECN or BECN processing should be done
 * @pkt: the packet to be evaluated
 *
 * Check whether the FECN or BECN bits in the packet's header are
 * enabled, depending on packet type.
 *
 * This function only checks for FECN and BECN bits. Additional checks
 * are done in the slowpath (hfi1_process_ecn_slowpath()) in order to
 * ensure correct handling.
 */
static inline bool hfi1_may_ecn(struct hfi1_packet *pkt)
{
	bool fecn, becn;

	if (pkt->etype == RHF_RCV_TYPE_BYPASS) {
		fecn = hfi1_16B_get_fecn(pkt->hdr);
		becn = hfi1_16B_get_becn(pkt->hdr);
	} else {
		fecn = ib_bth_get_fecn(pkt->ohdr);
		becn = ib_bth_get_becn(pkt->ohdr);
	}
	return fecn || becn;
}

bool hfi1_process_ecn_slowpath(struct rvt_qp *qp, struct hfi1_packet *pkt,
			       bool prescan);
static inline bool process_ecn(struct rvt_qp *qp, struct hfi1_packet *pkt)
{
	bool do_work;

	do_work = hfi1_may_ecn(pkt);
	if (unlikely(do_work))
		return hfi1_process_ecn_slowpath(qp, pkt, false);
	return false;
}

/*
 * Return the indexed PKEY from the port PKEY table.
 */
static inline u16 hfi1_get_pkey(struct hfi1_ibport *ibp, unsigned index)
{
	struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);
	u16 ret;

	if (index >= ARRAY_SIZE(ppd->pkeys))
		ret = 0;
	else
		ret = ppd->pkeys[index];

	return ret;
}

/*
 * Return the indexed GUID from the port GUIDs table.
 */
static inline __be64 get_sguid(struct hfi1_ibport *ibp, unsigned int index)
{
	struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);

	WARN_ON(index >= HFI1_GUIDS_PER_PORT);
	return cpu_to_be64(ppd->guids[index]);
}

/*
 * Called by readers of cc_state only, must call under rcu_read_lock().
 */
static inline struct cc_state *get_cc_state(struct hfi1_pportdata *ppd)
{
	return rcu_dereference(ppd->cc_state);
}

/*
 * Called by writers of cc_state only,  must call under cc_state_lock.
 */
static inline
struct cc_state *get_cc_state_protected(struct hfi1_pportdata *ppd)
{
	return rcu_dereference_protected(ppd->cc_state,
					 lockdep_is_held(&ppd->cc_state_lock));
}

/*
 * values for dd->flags (_device_ related flags)
 */
#define HFI1_INITTED           0x1    /* chip and driver up and initted */
#define HFI1_PRESENT           0x2    /* chip accesses can be done */
#define HFI1_FROZEN            0x4    /* chip in SPC freeze */
#define HFI1_HAS_SDMA_TIMEOUT  0x8
#define HFI1_HAS_SEND_DMA      0x10   /* Supports Send DMA */
#define HFI1_FORCED_FREEZE     0x80   /* driver forced freeze mode */
#define HFI1_SHUTDOWN          0x100  /* device is shutting down */

/* IB dword length mask in PBC (lower 11 bits); same for all chips */
#define HFI1_PBC_LENGTH_MASK                     ((1 << 11) - 1)

/* ctxt_flag bit offsets */
		/* base context has not finished initializing */
#define HFI1_CTXT_BASE_UNINIT 1
		/* base context initaliation failed */
#define HFI1_CTXT_BASE_FAILED 2
		/* waiting for a packet to arrive */
#define HFI1_CTXT_WAITING_RCV 3
		/* waiting for an urgent packet to arrive */
#define HFI1_CTXT_WAITING_URG 4

/* free up any allocated data at closes */
int hfi1_init_dd(struct hfi1_devdata *dd);
void hfi1_free_devdata(struct hfi1_devdata *dd);

/* LED beaconing functions */
void hfi1_start_led_override(struct hfi1_pportdata *ppd, unsigned int timeon,
			     unsigned int timeoff);
void shutdown_led_override(struct hfi1_pportdata *ppd);

#define HFI1_CREDIT_RETURN_RATE (100)

/*
 * The number of words for the KDETH protocol field.  If this is
 * larger then the actual field used, then part of the payload
 * will be in the header.
 *
 * Optimally, we want this sized so that a typical case will
 * use full cache lines.  The typical local KDETH header would
 * be:
 *
 *	Bytes	Field
 *	  8	LRH
 *	 12	BHT
 *	 ??	KDETH
 *	  8	RHF
 *	---
 *	 28 + KDETH
 *
 * For a 64-byte cache line, KDETH would need to be 36 bytes or 9 DWORDS
 */
#define DEFAULT_RCVHDRSIZE 9

/*
 * Maximal header byte count:
 *
 *	Bytes	Field
 *	  8	LRH
 *	 40	GRH (optional)
 *	 12	BTH
 *	 ??	KDETH
 *	  8	RHF
 *	---
 *	 68 + KDETH
 *
 * We also want to maintain a cache line alignment to assist DMA'ing
 * of the header bytes.  Round up to a good size.
 */
#define DEFAULT_RCVHDR_ENTSIZE 32

bool hfi1_can_pin_pages(struct hfi1_devdata *dd, struct mm_struct *mm,
			u32 nlocked, u32 npages);
int hfi1_acquire_user_pages(struct mm_struct *mm, unsigned long vaddr,
			    size_t npages, bool writable, struct page **pages);
void hfi1_release_user_pages(struct mm_struct *mm, struct page **p,
			     size_t npages, bool dirty);

/**
 * hfi1_rcvhdrtail_kvaddr - return tail kvaddr
 * @rcd - the receive context
 */
static inline __le64 *hfi1_rcvhdrtail_kvaddr(const struct hfi1_ctxtdata *rcd)
{
	return (__le64 *)rcd->rcvhdrtail_kvaddr;
}

static inline void clear_rcvhdrtail(const struct hfi1_ctxtdata *rcd)
{
	u64 *kv = (u64 *)hfi1_rcvhdrtail_kvaddr(rcd);

	if (kv)
		*kv = 0ULL;
}

static inline u32 get_rcvhdrtail(const struct hfi1_ctxtdata *rcd)
{
	/*
	 * volatile because it's a DMA target from the chip, routine is
	 * inlined, and don't want register caching or reordering.
	 */
	return (u32)le64_to_cpu(*hfi1_rcvhdrtail_kvaddr(rcd));
}

static inline bool hfi1_packet_present(struct hfi1_ctxtdata *rcd)
{
	if (likely(!rcd->rcvhdrtail_kvaddr)) {
		u32 seq = rhf_rcv_seq(rhf_to_cpu(get_rhf_addr(rcd)));

		return !last_rcv_seq(rcd, seq);
	}
	return hfi1_rcd_head(rcd) != get_rcvhdrtail(rcd);
}

/*
 * sysfs interface.
 */

extern const char ib_hfi1_version[];
extern const struct attribute_group ib_hfi1_attr_group;
extern const struct attribute_group *hfi1_attr_port_groups[];

int hfi1_device_create(struct hfi1_devdata *dd);
void hfi1_device_remove(struct hfi1_devdata *dd);

int hfi1_verbs_register_sysfs(struct hfi1_devdata *dd);
void hfi1_verbs_unregister_sysfs(struct hfi1_devdata *dd);
/* Hook for sysfs read of QSFP */
int qsfp_dump(struct hfi1_pportdata *ppd, char *buf, int len);

int hfi1_pcie_init(struct hfi1_devdata *dd);
void hfi1_pcie_cleanup(struct pci_dev *pdev);
int hfi1_pcie_ddinit(struct hfi1_devdata *dd, struct pci_dev *pdev);
void hfi1_pcie_ddcleanup(struct hfi1_devdata *);
int pcie_speeds(struct hfi1_devdata *dd);
int restore_pci_variables(struct hfi1_devdata *dd);
int save_pci_variables(struct hfi1_devdata *dd);
int do_pcie_gen3_transition(struct hfi1_devdata *dd);
void tune_pcie_caps(struct hfi1_devdata *dd);
int parse_platform_config(struct hfi1_devdata *dd);
int get_platform_config_field(struct hfi1_devdata *dd,
			      enum platform_config_table_type_encoding
			      table_type, int table_index, int field_index,
			      u32 *data, u32 len);

struct pci_dev *get_pci_dev(struct rvt_dev_info *rdi);

/*
 * Flush write combining store buffers (if present) and perform a write
 * barrier.
 */
static inline void flush_wc(void)
{
	asm volatile("sfence" : : : "memory");
}

void handle_eflags(struct hfi1_packet *packet);
void seqfile_dump_rcd(struct seq_file *s, struct hfi1_ctxtdata *rcd);

/* global module parameter variables */
extern unsigned int hfi1_max_mtu;
extern unsigned int hfi1_cu;
extern unsigned int user_credit_return_threshold;
extern int num_user_contexts;
extern unsigned long n_krcvqs;
extern uint krcvqs[];
extern int krcvqsset;
extern uint loopback;
extern uint quick_linkup;
extern uint rcv_intr_timeout;
extern uint rcv_intr_count;
extern uint rcv_intr_dynamic;
extern ushort link_crc_mask;

extern struct mutex hfi1_mutex;

/* Number of seconds before our card status check...  */
#define STATUS_TIMEOUT 60

#define DRIVER_NAME		"hfi1"
#define HFI1_USER_MINOR_BASE     0
#define HFI1_TRACE_MINOR         127
#define HFI1_NMINORS             255

#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_DEVICE_ID_INTEL0 0x24f0
#define PCI_DEVICE_ID_INTEL1 0x24f1

#define HFI1_PKT_USER_SC_INTEGRITY					    \
	(SEND_CTXT_CHECK_ENABLE_DISALLOW_NON_KDETH_PACKETS_SMASK	    \
	| SEND_CTXT_CHECK_ENABLE_DISALLOW_KDETH_PACKETS_SMASK		\
	| SEND_CTXT_CHECK_ENABLE_DISALLOW_BYPASS_SMASK		    \
	| SEND_CTXT_CHECK_ENABLE_DISALLOW_GRH_SMASK)

#define HFI1_PKT_KERNEL_SC_INTEGRITY					    \
	(SEND_CTXT_CHECK_ENABLE_DISALLOW_KDETH_PACKETS_SMASK)

static inline u64 hfi1_pkt_default_send_ctxt_mask(struct hfi1_devdata *dd,
						  u16 ctxt_type)
{
	u64 base_sc_integrity;

	/* No integrity checks if HFI1_CAP_NO_INTEGRITY is set */
	if (HFI1_CAP_IS_KSET(NO_INTEGRITY))
		return 0;

	base_sc_integrity =
	SEND_CTXT_CHECK_ENABLE_DISALLOW_BYPASS_BAD_PKT_LEN_SMASK
	| SEND_CTXT_CHECK_ENABLE_DISALLOW_PBC_STATIC_RATE_CONTROL_SMASK
	| SEND_CTXT_CHECK_ENABLE_DISALLOW_TOO_LONG_BYPASS_PACKETS_SMASK
	| SEND_CTXT_CHECK_ENABLE_DISALLOW_TOO_LONG_IB_PACKETS_SMASK
	| SEND_CTXT_CHECK_ENABLE_DISALLOW_BAD_PKT_LEN_SMASK
#ifndef CONFIG_FAULT_INJECTION
	| SEND_CTXT_CHECK_ENABLE_DISALLOW_PBC_TEST_SMASK
#endif
	| SEND_CTXT_CHECK_ENABLE_DISALLOW_TOO_SMALL_BYPASS_PACKETS_SMASK
	| SEND_CTXT_CHECK_ENABLE_DISALLOW_TOO_SMALL_IB_PACKETS_SMASK
	| SEND_CTXT_CHECK_ENABLE_DISALLOW_RAW_IPV6_SMASK
	| SEND_CTXT_CHECK_ENABLE_DISALLOW_RAW_SMASK
	| SEND_CTXT_CHECK_ENABLE_CHECK_BYPASS_VL_MAPPING_SMASK
	| SEND_CTXT_CHECK_ENABLE_CHECK_VL_MAPPING_SMASK
	| SEND_CTXT_CHECK_ENABLE_CHECK_OPCODE_SMASK
	| SEND_CTXT_CHECK_ENABLE_CHECK_SLID_SMASK
	| SEND_CTXT_CHECK_ENABLE_CHECK_VL_SMASK
	| SEND_CTXT_CHECK_ENABLE_CHECK_ENABLE_SMASK;

	if (ctxt_type == SC_USER)
		base_sc_integrity |=
#ifndef CONFIG_FAULT_INJECTION
			SEND_CTXT_CHECK_ENABLE_DISALLOW_PBC_TEST_SMASK |
#endif
			HFI1_PKT_USER_SC_INTEGRITY;
	else if (ctxt_type != SC_KERNEL)
		base_sc_integrity |= HFI1_PKT_KERNEL_SC_INTEGRITY;

	/* turn on send-side job key checks if !A0 */
	if (!is_ax(dd))
		base_sc_integrity |= SEND_CTXT_CHECK_ENABLE_CHECK_JOB_KEY_SMASK;

	return base_sc_integrity;
}

static inline u64 hfi1_pkt_base_sdma_integrity(struct hfi1_devdata *dd)
{
	u64 base_sdma_integrity;

	/* No integrity checks if HFI1_CAP_NO_INTEGRITY is set */
	if (HFI1_CAP_IS_KSET(NO_INTEGRITY))
		return 0;

	base_sdma_integrity =
	SEND_DMA_CHECK_ENABLE_DISALLOW_BYPASS_BAD_PKT_LEN_SMASK
	| SEND_DMA_CHECK_ENABLE_DISALLOW_TOO_LONG_BYPASS_PACKETS_SMASK
	| SEND_DMA_CHECK_ENABLE_DISALLOW_TOO_LONG_IB_PACKETS_SMASK
	| SEND_DMA_CHECK_ENABLE_DISALLOW_BAD_PKT_LEN_SMASK
	| SEND_DMA_CHECK_ENABLE_DISALLOW_TOO_SMALL_BYPASS_PACKETS_SMASK
	| SEND_DMA_CHECK_ENABLE_DISALLOW_TOO_SMALL_IB_PACKETS_SMASK
	| SEND_DMA_CHECK_ENABLE_DISALLOW_RAW_IPV6_SMASK
	| SEND_DMA_CHECK_ENABLE_DISALLOW_RAW_SMASK
	| SEND_DMA_CHECK_ENABLE_CHECK_BYPASS_VL_MAPPING_SMASK
	| SEND_DMA_CHECK_ENABLE_CHECK_VL_MAPPING_SMASK
	| SEND_DMA_CHECK_ENABLE_CHECK_OPCODE_SMASK
	| SEND_DMA_CHECK_ENABLE_CHECK_SLID_SMASK
	| SEND_DMA_CHECK_ENABLE_CHECK_VL_SMASK
	| SEND_DMA_CHECK_ENABLE_CHECK_ENABLE_SMASK;

	if (!HFI1_CAP_IS_KSET(STATIC_RATE_CTRL))
		base_sdma_integrity |=
		SEND_DMA_CHECK_ENABLE_DISALLOW_PBC_STATIC_RATE_CONTROL_SMASK;

	/* turn on send-side job key checks if !A0 */
	if (!is_ax(dd))
		base_sdma_integrity |=
			SEND_DMA_CHECK_ENABLE_CHECK_JOB_KEY_SMASK;

	return base_sdma_integrity;
}

#define dd_dev_emerg(dd, fmt, ...) \
	dev_emerg(&(dd)->pcidev->dev, "%s: " fmt, \
		  rvt_get_ibdev_name(&(dd)->verbs_dev.rdi), ##__VA_ARGS__)

#define dd_dev_err(dd, fmt, ...) \
	dev_err(&(dd)->pcidev->dev, "%s: " fmt, \
		rvt_get_ibdev_name(&(dd)->verbs_dev.rdi), ##__VA_ARGS__)

#define dd_dev_err_ratelimited(dd, fmt, ...) \
	dev_err_ratelimited(&(dd)->pcidev->dev, "%s: " fmt, \
			    rvt_get_ibdev_name(&(dd)->verbs_dev.rdi), \
			    ##__VA_ARGS__)

#define dd_dev_warn(dd, fmt, ...) \
	dev_warn(&(dd)->pcidev->dev, "%s: " fmt, \
		 rvt_get_ibdev_name(&(dd)->verbs_dev.rdi), ##__VA_ARGS__)

#define dd_dev_warn_ratelimited(dd, fmt, ...) \
	dev_warn_ratelimited(&(dd)->pcidev->dev, "%s: " fmt, \
			     rvt_get_ibdev_name(&(dd)->verbs_dev.rdi), \
			     ##__VA_ARGS__)

#define dd_dev_info(dd, fmt, ...) \
	dev_info(&(dd)->pcidev->dev, "%s: " fmt, \
		 rvt_get_ibdev_name(&(dd)->verbs_dev.rdi), ##__VA_ARGS__)

#define dd_dev_info_ratelimited(dd, fmt, ...) \
	dev_info_ratelimited(&(dd)->pcidev->dev, "%s: " fmt, \
			     rvt_get_ibdev_name(&(dd)->verbs_dev.rdi), \
			     ##__VA_ARGS__)

#define dd_dev_dbg(dd, fmt, ...) \
	dev_dbg(&(dd)->pcidev->dev, "%s: " fmt, \
		rvt_get_ibdev_name(&(dd)->verbs_dev.rdi), ##__VA_ARGS__)

#define hfi1_dev_porterr(dd, port, fmt, ...) \
	dev_err(&(dd)->pcidev->dev, "%s: port %u: " fmt, \
		rvt_get_ibdev_name(&(dd)->verbs_dev.rdi), (port), ##__VA_ARGS__)

/*
 * this is used for formatting hw error messages...
 */
struct hfi1_hwerror_msgs {
	u64 mask;
	const char *msg;
	size_t sz;
};

/* in intr.c... */
void hfi1_format_hwerrors(u64 hwerrs,
			  const struct hfi1_hwerror_msgs *hwerrmsgs,
			  size_t nhwerrmsgs, char *msg, size_t lmsg);

#define USER_OPCODE_CHECK_VAL 0xC0
#define USER_OPCODE_CHECK_MASK 0xC0
#define OPCODE_CHECK_VAL_DISABLED 0x0
#define OPCODE_CHECK_MASK_DISABLED 0x0

static inline void hfi1_reset_cpu_counters(struct hfi1_devdata *dd)
{
	struct hfi1_pportdata *ppd;
	int i;

	dd->z_int_counter = get_all_cpu_total(dd->int_counter);
	dd->z_rcv_limit = get_all_cpu_total(dd->rcv_limit);
	dd->z_send_schedule = get_all_cpu_total(dd->send_schedule);

	ppd = (struct hfi1_pportdata *)(dd + 1);
	for (i = 0; i < dd->num_pports; i++, ppd++) {
		ppd->ibport_data.rvp.z_rc_acks =
			get_all_cpu_total(ppd->ibport_data.rvp.rc_acks);
		ppd->ibport_data.rvp.z_rc_qacks =
			get_all_cpu_total(ppd->ibport_data.rvp.rc_qacks);
	}
}

/* Control LED state */
static inline void setextled(struct hfi1_devdata *dd, u32 on)
{
	if (on)
		write_csr(dd, DCC_CFG_LED_CNTRL, 0x1F);
	else
		write_csr(dd, DCC_CFG_LED_CNTRL, 0x10);
}

/* return the i2c resource given the target */
static inline u32 i2c_target(u32 target)
{
	return target ? CR_I2C2 : CR_I2C1;
}

/* return the i2c chain chip resource that this HFI uses for QSFP */
static inline u32 qsfp_resource(struct hfi1_devdata *dd)
{
	return i2c_target(dd->hfi1_id);
}

/* Is this device integrated or discrete? */
static inline bool is_integrated(struct hfi1_devdata *dd)
{
	return dd->pcidev->device == PCI_DEVICE_ID_INTEL1;
}

/**
 * hfi1_need_drop - detect need for drop
 * @dd: - the device
 *
 * In some cases, the first packet needs to be dropped.
 *
 * Return true is the current packet needs to be dropped and false otherwise.
 */
static inline bool hfi1_need_drop(struct hfi1_devdata *dd)
{
	if (unlikely(dd->do_drop &&
		     atomic_xchg(&dd->drop_packet, DROP_PACKET_OFF) ==
		     DROP_PACKET_ON)) {
		dd->do_drop = false;
		return true;
	}
	return false;
}

int hfi1_tempsense_rd(struct hfi1_devdata *dd, struct hfi1_temp *temp);

#define DD_DEV_ENTRY(dd)       __string(dev, dev_name(&(dd)->pcidev->dev))
#define DD_DEV_ASSIGN(dd)      __assign_str(dev)

static inline void hfi1_update_ah_attr(struct ib_device *ibdev,
				       struct rdma_ah_attr *attr)
{
	struct hfi1_pportdata *ppd;
	struct hfi1_ibport *ibp;
	u32 dlid = rdma_ah_get_dlid(attr);

	/*
	 * Kernel clients may not have setup GRH information
	 * Set that here.
	 */
	ibp = to_iport(ibdev, rdma_ah_get_port_num(attr));
	ppd = ppd_from_ibp(ibp);
	if ((((dlid >= be16_to_cpu(IB_MULTICAST_LID_BASE)) ||
	      (ppd->lid >= be16_to_cpu(IB_MULTICAST_LID_BASE))) &&
	    (dlid != be32_to_cpu(OPA_LID_PERMISSIVE)) &&
	    (dlid != be16_to_cpu(IB_LID_PERMISSIVE)) &&
	    (!(rdma_ah_get_ah_flags(attr) & IB_AH_GRH))) ||
	    (rdma_ah_get_make_grd(attr))) {
		rdma_ah_set_ah_flags(attr, IB_AH_GRH);
		rdma_ah_set_interface_id(attr, OPA_MAKE_ID(dlid));
		rdma_ah_set_subnet_prefix(attr, ibp->rvp.gid_prefix);
	}
}

/*
 * hfi1_check_mcast- Check if the given lid is
 * in the OPA multicast range.
 *
 * The LID might either reside in ah.dlid or might be
 * in the GRH of the address handle as DGID if extended
 * addresses are in use.
 */
static inline bool hfi1_check_mcast(u32 lid)
{
	return ((lid >= opa_get_mcast_base(OPA_MCAST_NR)) &&
		(lid != be32_to_cpu(OPA_LID_PERMISSIVE)));
}

#define opa_get_lid(lid, format)	\
	__opa_get_lid(lid, OPA_PORT_PACKET_FORMAT_##format)

/* Convert a lid to a specific lid space */
static inline u32 __opa_get_lid(u32 lid, u8 format)
{
	bool is_mcast = hfi1_check_mcast(lid);

	switch (format) {
	case OPA_PORT_PACKET_FORMAT_8B:
	case OPA_PORT_PACKET_FORMAT_10B:
		if (is_mcast)
			return (lid - opa_get_mcast_base(OPA_MCAST_NR) +
				0xF0000);
		return lid & 0xFFFFF;
	case OPA_PORT_PACKET_FORMAT_16B:
		if (is_mcast)
			return (lid - opa_get_mcast_base(OPA_MCAST_NR) +
				0xF00000);
		return lid & 0xFFFFFF;
	case OPA_PORT_PACKET_FORMAT_9B:
		if (is_mcast)
			return (lid -
				opa_get_mcast_base(OPA_MCAST_NR) +
				be16_to_cpu(IB_MULTICAST_LID_BASE));
		else
			return lid & 0xFFFF;
	default:
		return lid;
	}
}

/* Return true if the given lid is the OPA 16B multicast range */
static inline bool hfi1_is_16B_mcast(u32 lid)
{
	return ((lid >=
		opa_get_lid(opa_get_mcast_base(OPA_MCAST_NR), 16B)) &&
		(lid != opa_get_lid(be32_to_cpu(OPA_LID_PERMISSIVE), 16B)));
}

static inline void hfi1_make_opa_lid(struct rdma_ah_attr *attr)
{
	const struct ib_global_route *grh = rdma_ah_read_grh(attr);
	u32 dlid = rdma_ah_get_dlid(attr);

	/* Modify ah_attr.dlid to be in the 32 bit LID space.
	 * This is how the address will be laid out:
	 * Assuming MCAST_NR to be 4,
	 * 32 bit permissive LID = 0xFFFFFFFF
	 * Multicast LID range = 0xFFFFFFFE to 0xF0000000
	 * Unicast LID range = 0xEFFFFFFF to 1
	 * Invalid LID = 0
	 */
	if (ib_is_opa_gid(&grh->dgid))
		dlid = opa_get_lid_from_gid(&grh->dgid);
	else if ((dlid >= be16_to_cpu(IB_MULTICAST_LID_BASE)) &&
		 (dlid != be16_to_cpu(IB_LID_PERMISSIVE)) &&
		 (dlid != be32_to_cpu(OPA_LID_PERMISSIVE)))
		dlid = dlid - be16_to_cpu(IB_MULTICAST_LID_BASE) +
			opa_get_mcast_base(OPA_MCAST_NR);
	else if (dlid == be16_to_cpu(IB_LID_PERMISSIVE))
		dlid = be32_to_cpu(OPA_LID_PERMISSIVE);

	rdma_ah_set_dlid(attr, dlid);
}

static inline u8 hfi1_get_packet_type(u32 lid)
{
	/* 9B if lid > 0xF0000000 */
	if (lid >= opa_get_mcast_base(OPA_MCAST_NR))
		return HFI1_PKT_TYPE_9B;

	/* 16B if lid > 0xC000 */
	if (lid >= opa_get_lid(opa_get_mcast_base(OPA_MCAST_NR), 9B))
		return HFI1_PKT_TYPE_16B;

	return HFI1_PKT_TYPE_9B;
}

static inline bool hfi1_get_hdr_type(u32 lid, struct rdma_ah_attr *attr)
{
	/*
	 * If there was an incoming 16B packet with permissive
	 * LIDs, OPA GIDs would have been programmed when those
	 * packets were received. A 16B packet will have to
	 * be sent in response to that packet. Return a 16B
	 * header type if that's the case.
	 */
	if (rdma_ah_get_dlid(attr) == be32_to_cpu(OPA_LID_PERMISSIVE))
		return (ib_is_opa_gid(&rdma_ah_read_grh(attr)->dgid)) ?
			HFI1_PKT_TYPE_16B : HFI1_PKT_TYPE_9B;

	/*
	 * Return a 16B header type if either the destination
	 * or source lid is extended.
	 */
	if (hfi1_get_packet_type(rdma_ah_get_dlid(attr)) == HFI1_PKT_TYPE_16B)
		return HFI1_PKT_TYPE_16B;

	return hfi1_get_packet_type(lid);
}

static inline void hfi1_make_ext_grh(struct hfi1_packet *packet,
				     struct ib_grh *grh, u32 slid,
				     u32 dlid)
{
	struct hfi1_ibport *ibp = &packet->rcd->ppd->ibport_data;
	struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);

	if (!ibp)
		return;

	grh->hop_limit = 1;
	grh->sgid.global.subnet_prefix = ibp->rvp.gid_prefix;
	if (slid == opa_get_lid(be32_to_cpu(OPA_LID_PERMISSIVE), 16B))
		grh->sgid.global.interface_id =
			OPA_MAKE_ID(be32_to_cpu(OPA_LID_PERMISSIVE));
	else
		grh->sgid.global.interface_id = OPA_MAKE_ID(slid);

	/*
	 * Upper layers (like mad) may compare the dgid in the
	 * wc that is obtained here with the sgid_index in
	 * the wr. Since sgid_index in wr is always 0 for
	 * extended lids, set the dgid here to the default
	 * IB gid.
	 */
	grh->dgid.global.subnet_prefix = ibp->rvp.gid_prefix;
	grh->dgid.global.interface_id =
		cpu_to_be64(ppd->guids[HFI1_PORT_GUID_INDEX]);
}

static inline int hfi1_get_16b_padding(u32 hdr_size, u32 payload)
{
	return -(hdr_size + payload + (SIZE_OF_CRC << 2) +
		     SIZE_OF_LT) & 0x7;
}

static inline void hfi1_make_ib_hdr(struct ib_header *hdr,
				    u16 lrh0, u16 len,
				    u16 dlid, u16 slid)
{
	hdr->lrh[0] = cpu_to_be16(lrh0);
	hdr->lrh[1] = cpu_to_be16(dlid);
	hdr->lrh[2] = cpu_to_be16(len);
	hdr->lrh[3] = cpu_to_be16(slid);
}

static inline void hfi1_make_16b_hdr(struct hfi1_16b_header *hdr,
				     u32 slid, u32 dlid,
				     u16 len, u16 pkey,
				     bool becn, bool fecn, u8 l4,
				     u8 sc)
{
	u32 lrh0 = 0;
	u32 lrh1 = 0x40000000;
	u32 lrh2 = 0;
	u32 lrh3 = 0;

	lrh0 = (lrh0 & ~OPA_16B_BECN_MASK) | (becn << OPA_16B_BECN_SHIFT);
	lrh0 = (lrh0 & ~OPA_16B_LEN_MASK) | (len << OPA_16B_LEN_SHIFT);
	lrh0 = (lrh0 & ~OPA_16B_LID_MASK)  | (slid & OPA_16B_LID_MASK);
	lrh1 = (lrh1 & ~OPA_16B_FECN_MASK) | (fecn << OPA_16B_FECN_SHIFT);
	lrh1 = (lrh1 & ~OPA_16B_SC_MASK) | (sc << OPA_16B_SC_SHIFT);
	lrh1 = (lrh1 & ~OPA_16B_LID_MASK) | (dlid & OPA_16B_LID_MASK);
	lrh2 = (lrh2 & ~OPA_16B_SLID_MASK) |
		((slid >> OPA_16B_SLID_SHIFT) << OPA_16B_SLID_HIGH_SHIFT);
	lrh2 = (lrh2 & ~OPA_16B_DLID_MASK) |
		((dlid >> OPA_16B_DLID_SHIFT) << OPA_16B_DLID_HIGH_SHIFT);
	lrh2 = (lrh2 & ~OPA_16B_PKEY_MASK) | ((u32)pkey << OPA_16B_PKEY_SHIFT);
	lrh2 = (lrh2 & ~OPA_16B_L4_MASK) | l4;

	hdr->lrh[0] = lrh0;
	hdr->lrh[1] = lrh1;
	hdr->lrh[2] = lrh2;
	hdr->lrh[3] = lrh3;
}
#endif                          /* _HFI1_KERNEL_H */
