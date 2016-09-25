#ifndef _HFI1_KERNEL_H
#define _HFI1_KERNEL_H
/*
 * Copyright(c) 2015, 2016 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

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
#include <rdma/ib_hdrs.h>
#include <rdma/rdma_vt.h>

#include "chip_registers.h"
#include "common.h"
#include "verbs.h"
#include "pio.h"
#include "chip.h"
#include "mad.h"
#include "qsfp.h"
#include "platform.h"
#include "affinity.h"

/* bumped 1 from s/w major version of TrueScale */
#define HFI1_CHIP_VERS_MAJ 3U

/* don't care about this except printing */
#define HFI1_CHIP_VERS_MIN 0U

/* The Organization Unique Identifier (Mfg code), and its position in GUID */
#define HFI1_OUI 0x001175
#define HFI1_OUI_LSB 40

#define DROP_PACKET_OFF		0
#define DROP_PACKET_ON		1

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

#ifdef CONFIG_DEBUG_FS
struct hfi1_opcode_stats_perctx;
#endif

struct ctxt_eager_bufs {
	ssize_t size;            /* total size of eager buffers */
	u32 count;               /* size of buffers array */
	u32 numbufs;             /* number of buffers allocated */
	u32 alloced;             /* number of rcvarray entries used */
	u32 rcvtid_size;         /* size of each eager rcv tid */
	u32 threshold;           /* head update threshold */
	struct eager_buffer {
		void *addr;
		dma_addr_t dma;
		ssize_t len;
	} *buffers;
	struct {
		void *addr;
		dma_addr_t dma;
	} *rcvtids;
};

struct exp_tid_set {
	struct list_head list;
	u32 count;
};

struct hfi1_ctxtdata {
	/* shadow the ctxt's RcvCtrl register */
	u64 rcvctrl;
	/* rcvhdrq base, needs mmap before useful */
	void *rcvhdrq;
	/* kernel virtual address where hdrqtail is updated */
	volatile __le64 *rcvhdrtail_kvaddr;
	/*
	 * Shared page for kernel to signal user processes that send buffers
	 * need disarming.  The process should call HFI1_CMD_DISARM_BUFS
	 * or HFI1_CMD_ACK_EVENT with IPATH_EVENT_DISARM_BUFS set.
	 */
	unsigned long *user_event_mask;
	/* when waiting for rcv or pioavail */
	wait_queue_head_t wait;
	/* rcvhdrq size (for freeing) */
	size_t rcvhdrq_size;
	/* number of rcvhdrq entries */
	u16 rcvhdrq_cnt;
	/* size of each of the rcvhdrq entries */
	u16 rcvhdrqentsize;
	/* mmap of hdrq, must fit in 44 bits */
	dma_addr_t rcvhdrq_dma;
	dma_addr_t rcvhdrqtailaddr_dma;
	struct ctxt_eager_bufs egrbufs;
	/* this receive context's assigned PIO ACK send context */
	struct send_context *sc;

	/* dynamic receive available interrupt timeout */
	u32 rcvavail_timeout;
	/*
	 * number of opens (including slave sub-contexts) on this instance
	 * (ignoring forks, dup, etc. for now)
	 */
	int cnt;
	/*
	 * how much space to leave at start of eager TID entries for
	 * protocol use, on each TID
	 */
	/* instead of calculating it */
	unsigned ctxt;
	/* non-zero if ctxt is being shared. */
	u16 subctxt_cnt;
	/* non-zero if ctxt is being shared. */
	u16 subctxt_id;
	u8 uuid[16];
	/* job key */
	u16 jkey;
	/* number of RcvArray groups for this context. */
	u32 rcv_array_groups;
	/* index of first eager TID entry. */
	u32 eager_base;
	/* number of expected TID entries */
	u32 expected_count;
	/* index of first expected TID entry. */
	u32 expected_base;

	struct exp_tid_set tid_group_list;
	struct exp_tid_set tid_used_list;
	struct exp_tid_set tid_full_list;

	/* lock protecting all Expected TID data */
	struct mutex exp_lock;
	/* number of pio bufs for this ctxt (all procs, if shared) */
	u32 piocnt;
	/* first pio buffer for this ctxt */
	u32 pio_base;
	/* chip offset of PIO buffers for this ctxt */
	u32 piobufs;
	/* per-context configuration flags */
	unsigned long flags;
	/* per-context event flags for fileops/intr communication */
	unsigned long event_flags;
	/* WAIT_RCV that timed out, no interrupt */
	u32 rcvwait_to;
	/* WAIT_PIO that timed out, no interrupt */
	u32 piowait_to;
	/* WAIT_RCV already happened, no wait */
	u32 rcvnowait;
	/* WAIT_PIO already happened, no wait */
	u32 pionowait;
	/* total number of polled urgent packets */
	u32 urgent;
	/* saved total number of polled urgent packets for poll edge trigger */
	u32 urgent_poll;
	/* same size as task_struct .comm[], command that opened context */
	char comm[TASK_COMM_LEN];
	/* so file ops can get at unit */
	struct hfi1_devdata *dd;
	/* so functions that need physical port can get it easily */
	struct hfi1_pportdata *ppd;
	/* A page of memory for rcvhdrhead, rcvegrhead, rcvegrtail * N */
	void *subctxt_uregbase;
	/* An array of pages for the eager receive buffers * N */
	void *subctxt_rcvegrbuf;
	/* An array of pages for the eager header queue entries * N */
	void *subctxt_rcvhdr_base;
	/* The version of the library which opened this ctxt */
	u32 userversion;
	/* Bitmask of active slaves */
	u32 active_slaves;
	/* Type of packets or conditions we want to poll for */
	u16 poll_type;
	/* receive packet sequence counter */
	u8 seq_cnt;
	u8 redirect_seq_cnt;
	/* ctxt rcvhdrq head offset */
	u32 head;
	u32 pkt_count;
	/* QPs waiting for context processing */
	struct list_head qp_wait_list;
	/* interrupt handling */
	u64 imask;	/* clear interrupt mask */
	int ireg;	/* clear interrupt register */
	unsigned numa_id; /* numa node of this context */
	/* verbs stats per CTX */
	struct hfi1_opcode_stats_perctx *opstats;
	/*
	 * This is the kernel thread that will keep making
	 * progress on the user sdma requests behind the scenes.
	 * There is one per context (shared contexts use the master's).
	 */
	struct task_struct *progress;
	struct list_head sdma_queues;
	/* protect sdma queues */
	spinlock_t sdma_qlock;

	/* Is ASPM interrupt supported for this context */
	bool aspm_intr_supported;
	/* ASPM state (enabled/disabled) for this context */
	bool aspm_enabled;
	/* Timer for re-enabling ASPM if interrupt activity quietens down */
	struct timer_list aspm_timer;
	/* Lock to serialize between intr, timer intr and user threads */
	spinlock_t aspm_lock;
	/* Is ASPM processing enabled for this context (in intr context) */
	bool aspm_intr_enable;
	/* Last interrupt timestamp */
	ktime_t aspm_ts_last_intr;
	/* Last timestamp at which we scheduled a timer for this context */
	ktime_t aspm_ts_timer_sched;

	/*
	 * The interrupt handler for a particular receive context can vary
	 * throughout it's lifetime. This is not a lock protected data member so
	 * it must be updated atomically and the prev and new value must always
	 * be valid. Worst case is we process an extra interrupt and up to 64
	 * packets with the wrong interrupt handler.
	 */
	int (*do_interrupt)(struct hfi1_ctxtdata *rcd, int threaded);
};

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
	struct hfi1_ctxtdata *rcd;
	__le32 *rhf_addr;
	struct rvt_qp *qp;
	struct ib_other_headers *ohdr;
	u64 rhf;
	u32 maxcnt;
	u32 rhqoff;
	u32 hdrqtail;
	int numpkt;
	u16 tlen;
	u16 hlen;
	s16 etail;
	u16 rsize;
	u8 updegr;
	u8 rcv_flags;
	u8 etype;
};

/*
 * Private data for snoop/capture support.
 */
struct hfi1_snoop_data {
	int mode_flag;
	struct cdev cdev;
	struct device *class_dev;
	/* protect snoop data */
	spinlock_t snoop_lock;
	struct list_head queue;
	wait_queue_head_t waitq;
	void *filter_value;
	int (*filter_callback)(void *hdr, void *data, void *value);
	u64 dcc_cfg; /* saved value of DCC Cfg register */
};

/* snoop mode_flag values */
#define HFI1_PORT_SNOOP_MODE     1U
#define HFI1_PORT_CAPTURE_MODE   2U

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

/* partition enforcement flags */
#define HFI1_PART_ENFORCE_IN	0x1
#define HFI1_PART_ENFORCE_OUT	0x2

/* how often we check for synthetic counter wrap around */
#define SYNTH_CNT_TIME 2

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

static inline void incr_cntr64(u64 *cntr)
{
	if (*cntr < (u64)-1LL)
		(*cntr)++;
}

static inline void incr_cntr32(u32 *cntr)
{
	if (*cntr < (u32)-1LL)
		(*cntr)++;
}

#define MAX_NAME_SIZE 64
struct hfi1_msix_entry {
	enum irq_type type;
	struct msix_entry msix;
	void *arg;
	char name[MAX_NAME_SIZE];
	cpumask_t mask;
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
	struct kobject pport_cc_kobj;
	struct kobject sc2vl_kobj;
	struct kobject sl2sc_kobj;
	struct kobject vl2mtu_kobj;

	/* PHY support */
	u32 port_type;
	struct qsfp_data qsfp_info;

	/* GUID for this interface, in host order */
	u64 guid;
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

	spinlock_t            sdma_alllock ____cacheline_aligned_in_smp;

	u32 lstate;	/* logical link state */

	/* these are the "32 bit" regs */

	u32 ibmtu; /* The MTU programmed for this unit */
	/*
	 * Current max size IB packet (in bytes) including IB headers, that
	 * we can send. Changes when ibmtu changes.
	 */
	u32 ibmaxlen;
	u32 current_egress_rate; /* units [10^6 bits/sec] */
	/* LID programmed for this instance */
	u16 lid;
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
	u8 port;        /* IB port number and index into dd->pports - 1 */
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
	u8 last_pstate;		/* info only */
	u8 qsfp_retry_count;

	/* placeholders for IB MAD packet settings */
	u8 overrun_threshold;
	u8 phy_error_threshold;

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
};

typedef int (*rhf_rcv_function_ptr)(struct hfi1_packet *packet);

typedef void (*opcode_handler)(struct hfi1_packet *packet);

/* return values for the RHF receive functions */
#define RHF_RCV_CONTINUE  0	/* keep going */
#define RHF_RCV_DONE	  1	/* stop, this packet processed */
#define RHF_RCV_REPROCESS 2	/* stop. retain this packet */

struct rcv_array_data {
	u8 group_size;
	u16 ngroups;
	u16 nctxt_extra;
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

/* device data struct now contains only "general per-device" info.
 * fields related to a physical IB port are in a hfi1_pportdata struct.
 */
struct sdma_engine;
struct sdma_vl_map;

#define BOARD_VERS_MAX 96 /* how long the version string can be */
#define SERIAL_MAX 16 /* length of the serial number */

typedef int (*send_routine)(struct rvt_qp *, struct hfi1_pkt_state *, u64);
struct hfi1_devdata {
	struct hfi1_ibdev verbs_dev;     /* must be first */
	struct list_head list;
	/* pointers to related structs for this device */
	/* pci access data structure */
	struct pci_dev *pcidev;
	struct cdev user_cdev;
	struct cdev diag_cdev;
	struct cdev ui_cdev;
	struct device *user_device;
	struct device *diag_device;
	struct device *ui_device;

	/* mem-mapped pointer to base of chip regs */
	u8 __iomem *kregbase;
	/* end of mem-mapped chip space excluding sendbuf and user regs */
	u8 __iomem *kregend;
	/* physical address of chip for io_remap, etc. */
	resource_size_t physaddr;
	/* receive context data */
	struct hfi1_ctxtdata **rcd;
	/* send context data */
	struct send_context_info *send_contexts;
	/* map hardware send contexts to software index */
	u8 *hw_to_sw;
	/* spinlock for allocating and releasing send context resources */
	spinlock_t sc_lock;
	/* Per VL data. Enough for all VLs but not all elements are set/used. */
	struct per_vl_data vld[PER_VL_SEND_CONTEXTS];
	/* lock for pio_map */
	spinlock_t pio_map_lock;
	/* array of kernel send contexts */
	struct send_context **kernel_send_context;
	/* array of vl maps */
	struct pio_vl_map __rcu *pio_map;
	/* seqlock for sc2vl */
	seqlock_t sc2vl_lock;
	u64 sc2vl[4];
	/* Send Context initialization lock. */
	spinlock_t sc_init_lock;

	/* fields common to all SDMA engines */

	/* default flags to last descriptor */
	u64 default_desc1;
	volatile __le64                    *sdma_heads_dma; /* DMA'ed by chip */
	dma_addr_t                          sdma_heads_phys;
	void                               *sdma_pad_dma; /* DMA'ed by chip */
	dma_addr_t                          sdma_pad_phys;
	/* for deallocation */
	size_t                              sdma_heads_size;
	/* number from the chip */
	u32                                 chip_sdma_engines;
	/* num used */
	u32                                 num_sdma;
	/* lock for sdma_map */
	spinlock_t                          sde_map_lock;
	/* array of engines sized by num_sdma */
	struct sdma_engine                 *per_sdma;
	/* array of vl maps */
	struct sdma_vl_map __rcu           *sdma_map;
	/* SPC freeze waitqueue and variable */
	wait_queue_head_t		  sdma_unfreeze_wq;
	atomic_t			  sdma_unfreeze_count;

	/* common data between shared ASIC HFIs in this OS */
	struct hfi1_asic_data *asic_data;

	/* hfi1_pportdata, points to array of (physical) port-specific
	 * data structs, indexed by pidx (0..n-1)
	 */
	struct hfi1_pportdata *pport;

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

	u32 lcb_access_count;		/* count of LCB users */

	char *boardname; /* human readable board info */

	/* device (not port) flags, basically device capabilities */
	u32 flags;

	/* reset value */
	u64 z_int_counter;
	u64 z_rcv_limit;
	u64 z_send_schedule;
	/* percpu int_counter */
	u64 __percpu *int_counter;
	u64 __percpu *rcv_limit;
	u64 __percpu *send_schedule;
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

	u64 __iomem *egrtidbase;
	spinlock_t sendctrl_lock; /* protect changes to SendCtrl */
	spinlock_t rcvctrl_lock; /* protect changes to RcvCtrl */
	/* around rcd and (user ctxts) ctxt_cnt use (intr vs free) */
	spinlock_t uctxt_lock; /* rcd and user context changes */
	/* exclusive access to 8051 */
	spinlock_t dc8051_lock;
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
	u32 freezelen; /* max length of freezemsg */

	/* revision register shadow */
	u64 revision;
	/* Base GUID for device (network order) */
	u64 base_guid;

	/* these are the "32 bit" regs */

	/* value we put in kr_rcvhdrsize */
	u32 rcvhdrsize;
	/* number of receive contexts the chip supports */
	u32 chip_rcv_contexts;
	/* number of receive array entries */
	u32 chip_rcv_array_count;
	/* number of PIO send contexts the chip supports */
	u32 chip_send_contexts;
	/* number of bytes in the PIO memory buffer */
	u32 chip_pio_mem_size;
	/* number of bytes in the SDMA memory buffer */
	u32 chip_sdma_mem_size;

	/* size of each rcvegrbuffer */
	u32 rcvegrbufsize;
	/* log2 of above */
	u16 rcvegrbufsize_shift;
	/* both sides of the PCIe link are gen3 capable */
	u8 link_gen3_capable;
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
	u32 pci_lnkctl3;
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
	/* default link down value (poll/sleep) */
	u8 link_default;
	/* vAU of this device */
	u8 vau;
	/* vCU of this device */
	u8 vcu;
	/* link credits of this device */
	u16 link_credits;
	/* initial vl15 credits to use */
	u16 vl15_init;

	/* Misc small ints */
	/* Number of physical ports available */
	u8 num_pports;
	/* Lowest context number which can be used by user processes */
	u8 first_user_ctxt;
	u8 n_krcv_queues;
	u8 qos_shift;
	u8 qpn_mask;

	u16 rhf_offset; /* offset of RHF within receive header entry */
	u16 irev;	/* implementation revision */
	u16 dc8051_ver; /* 8051 firmware version */

	struct platform_config platform_config;
	struct platform_config_cache pcfg_cache;

	struct diag_client *diag_client;
	spinlock_t hfi1_diag_trans_lock; /* protect diag observer ops */

	u8 psxmitwait_supported;
	/* cycle length of PS* counters in HW (in picoseconds) */
	u16 psxmitwait_check_rate;

	/* MSI-X information */
	struct hfi1_msix_entry *msix_entries;
	u32 num_msix_entries;

	/* INTx information */
	u32 requested_intx_irq;		/* did we request one? */
	char intx_name[MAX_NAME_SIZE];	/* INTx name */

	/* general interrupt: mask of handled interrupts */
	u64 gi_mask[CCE_NUM_INT_CSRS];

	struct rcv_array_data rcv_entries;

	/*
	 * 64 bit synthetic counters
	 */
	struct timer_list synth_stats_timer;

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

	struct hfi1_snoop_data hfi1_snoop;

	struct err_info_rcvport err_info_rcvport;
	struct err_info_constraint err_info_rcv_constraint;
	struct err_info_constraint err_info_xmit_constraint;
	u8 err_info_uncorrectable;
	u8 err_info_fmconfig;

	atomic_t drop_packet;
	u8 do_drop;

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
	/* receive interrupt functions */
	rhf_rcv_function_ptr *rhf_rcv_function_map;
	rhf_rcv_function_ptr normal_rhf_rcv_functions[8];

	/*
	 * Handlers for outgoing data so that snoop/capture does not
	 * have to have its hooks in the send path
	 */
	send_routine process_pio_send;
	send_routine process_dma_send;
	void (*pio_inline_send)(struct hfi1_devdata *dd, struct pio_buf *pbuf,
				u64 pbc, const void *from, size_t count);

	/* OUI comes from the HW. Used everywhere as 3 separate bytes. */
	u8 oui1;
	u8 oui2;
	u8 oui3;
	/* Timer and counter used to detect RcvBufOvflCnt changes */
	struct timer_list rcverr_timer;
	u32 rcv_ovfl_cnt;

	wait_queue_head_t event_queue;

	/* Save the enabled LCB error bits */
	u64 lcb_err_en;
	u8 dc_shutdown;

	/* receive context tail dummy address */
	__le64 *rcvhdrtail_dummy_kvaddr;
	dma_addr_t rcvhdrtail_dummy_dma;

	bool eprom_available;	/* true if EPROM is available for this device */
	bool aspm_supported;	/* Does HW support ASPM */
	bool aspm_enabled;	/* ASPM state: enabled/disabled */
	/* Serialize ASPM enable/disable between multiple verbs contexts */
	spinlock_t aspm_lock;
	/* Number of verbs contexts which have disabled ASPM */
	atomic_t aspm_disabled_cnt;

	struct hfi1_affinity *affinity;
	struct kobject kobj;
};

/* 8051 firmware version helper */
#define dc8051_ver(a, b) ((a) << 8 | (b))
#define dc8051_ver_maj(a) ((a & 0xff00) >> 8)
#define dc8051_ver_min(a)  (a & 0x00ff)

/* f_put_tid types */
#define PT_EXPECTED 0
#define PT_EAGER    1
#define PT_INVALID  2

struct tid_rb_node;
struct mmu_rb_node;
struct mmu_rb_handler;

/* Private data for file operations */
struct hfi1_filedata {
	struct hfi1_ctxtdata *uctxt;
	unsigned subctxt;
	struct hfi1_user_sdma_comp_q *cq;
	struct hfi1_user_sdma_pkt_q *pq;
	/* for cpu affinity; -1 if none */
	int rec_cpu_num;
	u32 tid_n_pinned;
	struct mmu_rb_handler *handler;
	struct tid_rb_node **entry_to_rb;
	spinlock_t tid_lock; /* protect tid_[limit,used] counters */
	u32 tid_limit;
	u32 tid_used;
	u32 *invalid_tids;
	u32 invalid_tid_idx;
	/* protect invalid_tids array and invalid_tid_idx */
	spinlock_t invalid_lock;
	struct mm_struct *mm;
};

extern struct list_head hfi1_dev_list;
extern spinlock_t hfi1_devs_lock;
struct hfi1_devdata *hfi1_lookup(int unit);
extern u32 hfi1_cpulist_count;
extern unsigned long *hfi1_cpulist;

extern unsigned int snoop_drop_send;
extern unsigned int snoop_force_capture;
int hfi1_init(struct hfi1_devdata *, int);
int hfi1_count_units(int *npresentp, int *nupp);
int hfi1_count_active_units(void);

int hfi1_diag_add(struct hfi1_devdata *);
void hfi1_diag_remove(struct hfi1_devdata *);
void handle_linkup_change(struct hfi1_devdata *dd, u32 linkup);

void handle_user_interrupt(struct hfi1_ctxtdata *rcd);

int hfi1_create_rcvhdrq(struct hfi1_devdata *, struct hfi1_ctxtdata *);
int hfi1_setup_eagerbufs(struct hfi1_ctxtdata *);
int hfi1_create_ctxts(struct hfi1_devdata *dd);
struct hfi1_ctxtdata *hfi1_create_ctxtdata(struct hfi1_pportdata *, u32, int);
void hfi1_init_pportdata(struct pci_dev *, struct hfi1_pportdata *,
			 struct hfi1_devdata *, u8, u8);
void hfi1_free_ctxtdata(struct hfi1_devdata *, struct hfi1_ctxtdata *);

int handle_receive_interrupt(struct hfi1_ctxtdata *, int);
int handle_receive_interrupt_nodma_rtail(struct hfi1_ctxtdata *, int);
int handle_receive_interrupt_dma_rtail(struct hfi1_ctxtdata *, int);
void set_all_slowpath(struct hfi1_devdata *dd);

extern const struct pci_device_id hfi1_pci_tbl[];

/* receive packet handler dispositions */
#define RCV_PKT_OK      0x0 /* keep going */
#define RCV_PKT_LIMIT   0x1 /* stop, hit limit, start thread */
#define RCV_PKT_DONE    0x2 /* stop, no more packets detected */

/* calculate the current RHF address */
static inline __le32 *get_rhf_addr(struct hfi1_ctxtdata *rcd)
{
	return (__le32 *)rcd->rcvhdrq + rcd->head + rcd->dd->rhf_offset;
}

int hfi1_reset_device(int);

/* return the driver's idea of the logical OPA port state */
static inline u32 driver_lstate(struct hfi1_pportdata *ppd)
{
	return ppd->lstate; /* use the cached value */
}

void receive_interrupt_work(struct work_struct *work);

/* extract service channel from header and rhf */
static inline int hdr2sc(struct ib_header *hdr, u64 rhf)
{
	return ((be16_to_cpu(hdr->lrh[0]) >> 12) & 0xf) |
	       ((!!(rhf_dc_info(rhf))) << 4);
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
void process_becn(struct hfi1_pportdata *ppd, u8 sl,  u16 rlid, u32 lqpn,
		  u32 rqpn, u8 svc_type);
void return_cnp(struct hfi1_ibport *ibp, struct rvt_qp *qp, u32 remote_qpn,
		u32 pkey, u32 slid, u32 dlid, u8 sc5,
		const struct ib_grh *old_grh);
#define PKEY_CHECK_INVALID -1
int egress_pkey_check(struct hfi1_pportdata *ppd, __be16 *lrh, __be32 *bth,
		      u8 sc5, int8_t s_pkey_index);

#define PACKET_EGRESS_TIMEOUT 350
static inline void pause_for_credit_return(struct hfi1_devdata *dd)
{
	/* Pause at least 1us, to ensure chip returns all credits */
	u32 usec = cclock_to_ns(dd, PACKET_EGRESS_TIMEOUT) / 1000;

	udelay(usec ? usec : 1);
}

/**
 * sc_to_vlt() reverse lookup sc to vl
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
				    u16 slid)
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
				     u8 sc5, u8 idx, u16 slid)
{
	if (!(ppd->part_enforce & HFI1_PART_ENFORCE_IN))
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
u16 enum_to_mtu(int);
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

int set_mtu(struct hfi1_pportdata *);

int hfi1_set_lid(struct hfi1_pportdata *, u32, u8);
void hfi1_disable_after_error(struct hfi1_devdata *);
int hfi1_set_uevent_bits(struct hfi1_pportdata *, const int);
int hfi1_rcvbuf_validate(u32, u8, u16 *);

int fm_get_table(struct hfi1_pportdata *, int, void *);
int fm_set_table(struct hfi1_pportdata *, int, void *);

void set_up_vl15(struct hfi1_devdata *dd, u8 vau, u16 vl15buf);
void reset_link_credits(struct hfi1_devdata *dd);
void assign_remote_cm_au_table(struct hfi1_devdata *dd, u8 vcu);

int snoop_recv_handler(struct hfi1_packet *packet);
int snoop_send_dma_handler(struct rvt_qp *qp, struct hfi1_pkt_state *ps,
			   u64 pbc);
int snoop_send_pio_handler(struct rvt_qp *qp, struct hfi1_pkt_state *ps,
			   u64 pbc);
void snoop_inline_pio_send(struct hfi1_devdata *dd, struct pio_buf *pbuf,
			   u64 pbc, const void *from, size_t count);
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

static inline struct hfi1_ibport *to_iport(struct ib_device *ibdev, u8 port)
{
	struct hfi1_devdata *dd = dd_from_ibdev(ibdev);
	unsigned pidx = port - 1; /* IB number port from 1, hdw from 0 */

	WARN_ON(pidx >= dd->num_pports);
	return &dd->pport[pidx].ibport_data;
}

void hfi1_process_ecn_slowpath(struct rvt_qp *qp, struct hfi1_packet *pkt,
			       bool do_cnp);
static inline bool process_ecn(struct rvt_qp *qp, struct hfi1_packet *pkt,
			       bool do_cnp)
{
	struct ib_other_headers *ohdr = pkt->ohdr;
	u32 bth1;

	bth1 = be32_to_cpu(ohdr->bth[1]);
	if (unlikely(bth1 & (HFI1_BECN_SMASK | HFI1_FECN_SMASK))) {
		hfi1_process_ecn_slowpath(qp, pkt, do_cnp);
		return bth1 & HFI1_FECN_SMASK;
	}
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

/* IB dword length mask in PBC (lower 11 bits); same for all chips */
#define HFI1_PBC_LENGTH_MASK                     ((1 << 11) - 1)

/* ctxt_flag bit offsets */
		/* context has been setup */
#define HFI1_CTXT_SETUP_DONE 1
		/* waiting for a packet to arrive */
#define HFI1_CTXT_WAITING_RCV   2
		/* master has not finished initializing */
#define HFI1_CTXT_MASTER_UNINIT 4
		/* waiting for an urgent packet to arrive */
#define HFI1_CTXT_WAITING_URG 5

/* free up any allocated data at closes */
struct hfi1_devdata *hfi1_init_dd(struct pci_dev *,
				  const struct pci_device_id *);
void hfi1_free_devdata(struct hfi1_devdata *);
struct hfi1_devdata *hfi1_alloc_devdata(struct pci_dev *pdev, size_t extra);

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

static inline void clear_rcvhdrtail(const struct hfi1_ctxtdata *rcd)
{
	*((u64 *)rcd->rcvhdrtail_kvaddr) = 0ULL;
}

static inline u32 get_rcvhdrtail(const struct hfi1_ctxtdata *rcd)
{
	/*
	 * volatile because it's a DMA target from the chip, routine is
	 * inlined, and don't want register caching or reordering.
	 */
	return (u32)le64_to_cpu(*rcd->rcvhdrtail_kvaddr);
}

/*
 * sysfs interface.
 */

extern const char ib_hfi1_version[];

int hfi1_device_create(struct hfi1_devdata *);
void hfi1_device_remove(struct hfi1_devdata *);

int hfi1_create_port_files(struct ib_device *ibdev, u8 port_num,
			   struct kobject *kobj);
int hfi1_verbs_register_sysfs(struct hfi1_devdata *);
void hfi1_verbs_unregister_sysfs(struct hfi1_devdata *);
/* Hook for sysfs read of QSFP */
int qsfp_dump(struct hfi1_pportdata *ppd, char *buf, int len);

int hfi1_pcie_init(struct pci_dev *, const struct pci_device_id *);
void hfi1_pcie_cleanup(struct pci_dev *);
int hfi1_pcie_ddinit(struct hfi1_devdata *, struct pci_dev *,
		     const struct pci_device_id *);
void hfi1_pcie_ddcleanup(struct hfi1_devdata *);
void hfi1_pcie_flr(struct hfi1_devdata *);
int pcie_speeds(struct hfi1_devdata *);
void request_msix(struct hfi1_devdata *, u32 *, struct hfi1_msix_entry *);
void hfi1_enable_intx(struct pci_dev *);
void restore_pci_variables(struct hfi1_devdata *dd);
int do_pcie_gen3_transition(struct hfi1_devdata *dd);
int parse_platform_config(struct hfi1_devdata *dd);
int get_platform_config_field(struct hfi1_devdata *dd,
			      enum platform_config_table_type_encoding
			      table_type, int table_index, int field_index,
			      u32 *data, u32 len);

const char *get_unit_name(int unit);
const char *get_card_name(struct rvt_dev_info *rdi);
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
int process_receive_ib(struct hfi1_packet *packet);
int process_receive_bypass(struct hfi1_packet *packet);
int process_receive_error(struct hfi1_packet *packet);
int kdeth_process_expected(struct hfi1_packet *packet);
int kdeth_process_eager(struct hfi1_packet *packet);
int process_receive_invalid(struct hfi1_packet *packet);

extern rhf_rcv_function_ptr snoop_rhf_rcv_functions[8];

void update_sge(struct rvt_sge_state *ss, u32 length);

/* global module parameter variables */
extern unsigned int hfi1_max_mtu;
extern unsigned int hfi1_cu;
extern unsigned int user_credit_return_threshold;
extern int num_user_contexts;
extern unsigned long n_krcvqs;
extern uint krcvqs[];
extern int krcvqsset;
extern uint kdeth_qp;
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
#define HFI1_DIAGPKT_MINOR       128
#define HFI1_DIAG_MINOR_BASE     129
#define HFI1_SNOOP_CAPTURE_BASE  200
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
	u64 base_sc_integrity =
	SEND_CTXT_CHECK_ENABLE_DISALLOW_BYPASS_BAD_PKT_LEN_SMASK
	| SEND_CTXT_CHECK_ENABLE_DISALLOW_PBC_STATIC_RATE_CONTROL_SMASK
	| SEND_CTXT_CHECK_ENABLE_DISALLOW_TOO_LONG_BYPASS_PACKETS_SMASK
	| SEND_CTXT_CHECK_ENABLE_DISALLOW_TOO_LONG_IB_PACKETS_SMASK
	| SEND_CTXT_CHECK_ENABLE_DISALLOW_BAD_PKT_LEN_SMASK
	| SEND_CTXT_CHECK_ENABLE_DISALLOW_PBC_TEST_SMASK
	| SEND_CTXT_CHECK_ENABLE_DISALLOW_TOO_SMALL_BYPASS_PACKETS_SMASK
	| SEND_CTXT_CHECK_ENABLE_DISALLOW_TOO_SMALL_IB_PACKETS_SMASK
	| SEND_CTXT_CHECK_ENABLE_DISALLOW_RAW_IPV6_SMASK
	| SEND_CTXT_CHECK_ENABLE_DISALLOW_RAW_SMASK
	| SEND_CTXT_CHECK_ENABLE_CHECK_BYPASS_VL_MAPPING_SMASK
	| SEND_CTXT_CHECK_ENABLE_CHECK_VL_MAPPING_SMASK
	| SEND_CTXT_CHECK_ENABLE_CHECK_OPCODE_SMASK
	| SEND_CTXT_CHECK_ENABLE_CHECK_SLID_SMASK
	| SEND_CTXT_CHECK_ENABLE_CHECK_JOB_KEY_SMASK
	| SEND_CTXT_CHECK_ENABLE_CHECK_VL_SMASK
	| SEND_CTXT_CHECK_ENABLE_CHECK_ENABLE_SMASK;

	if (ctxt_type == SC_USER)
		base_sc_integrity |= HFI1_PKT_USER_SC_INTEGRITY;
	else
		base_sc_integrity |= HFI1_PKT_KERNEL_SC_INTEGRITY;

	if (is_ax(dd))
		/* turn off send-side job key checks - A0 */
		return base_sc_integrity &
		       ~SEND_CTXT_CHECK_ENABLE_CHECK_JOB_KEY_SMASK;
	return base_sc_integrity;
}

static inline u64 hfi1_pkt_base_sdma_integrity(struct hfi1_devdata *dd)
{
	u64 base_sdma_integrity =
	SEND_DMA_CHECK_ENABLE_DISALLOW_BYPASS_BAD_PKT_LEN_SMASK
	| SEND_DMA_CHECK_ENABLE_DISALLOW_PBC_STATIC_RATE_CONTROL_SMASK
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
	| SEND_DMA_CHECK_ENABLE_CHECK_JOB_KEY_SMASK
	| SEND_DMA_CHECK_ENABLE_CHECK_VL_SMASK
	| SEND_DMA_CHECK_ENABLE_CHECK_ENABLE_SMASK;

	if (is_ax(dd))
		/* turn off send-side job key checks - A0 */
		return base_sdma_integrity &
		       ~SEND_DMA_CHECK_ENABLE_CHECK_JOB_KEY_SMASK;
	return base_sdma_integrity;
}

/*
 * hfi1_early_err is used (only!) to print early errors before devdata is
 * allocated, or when dd->pcidev may not be valid, and at the tail end of
 * cleanup when devdata may have been freed, etc.  hfi1_dev_porterr is
 * the same as dd_dev_err, but is used when the message really needs
 * the IB port# to be definitive as to what's happening..
 */
#define hfi1_early_err(dev, fmt, ...) \
	dev_err(dev, fmt, ##__VA_ARGS__)

#define hfi1_early_info(dev, fmt, ...) \
	dev_info(dev, fmt, ##__VA_ARGS__)

#define dd_dev_emerg(dd, fmt, ...) \
	dev_emerg(&(dd)->pcidev->dev, "%s: " fmt, \
		  get_unit_name((dd)->unit), ##__VA_ARGS__)
#define dd_dev_err(dd, fmt, ...) \
	dev_err(&(dd)->pcidev->dev, "%s: " fmt, \
			get_unit_name((dd)->unit), ##__VA_ARGS__)
#define dd_dev_warn(dd, fmt, ...) \
	dev_warn(&(dd)->pcidev->dev, "%s: " fmt, \
			get_unit_name((dd)->unit), ##__VA_ARGS__)

#define dd_dev_warn_ratelimited(dd, fmt, ...) \
	dev_warn_ratelimited(&(dd)->pcidev->dev, "%s: " fmt, \
			get_unit_name((dd)->unit), ##__VA_ARGS__)

#define dd_dev_info(dd, fmt, ...) \
	dev_info(&(dd)->pcidev->dev, "%s: " fmt, \
			get_unit_name((dd)->unit), ##__VA_ARGS__)

#define dd_dev_dbg(dd, fmt, ...) \
	dev_dbg(&(dd)->pcidev->dev, "%s: " fmt, \
		get_unit_name((dd)->unit), ##__VA_ARGS__)

#define hfi1_dev_porterr(dd, port, fmt, ...) \
	dev_err(&(dd)->pcidev->dev, "%s: port %u: " fmt, \
			get_unit_name((dd)->unit), (port), ##__VA_ARGS__)

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

int hfi1_tempsense_rd(struct hfi1_devdata *dd, struct hfi1_temp *temp);

#define DD_DEV_ENTRY(dd)       __string(dev, dev_name(&(dd)->pcidev->dev))
#define DD_DEV_ASSIGN(dd)      __assign_str(dev, dev_name(&(dd)->pcidev->dev))

#define packettype_name(etype) { RHF_RCV_TYPE_##etype, #etype }
#define show_packettype(etype)                  \
__print_symbolic(etype,                         \
	packettype_name(EXPECTED),              \
	packettype_name(EAGER),                 \
	packettype_name(IB),                    \
	packettype_name(ERROR),                 \
	packettype_name(BYPASS))

#define ib_opcode_name(opcode) { IB_OPCODE_##opcode, #opcode  }
#define show_ib_opcode(opcode)                             \
__print_symbolic(opcode,                                   \
	ib_opcode_name(RC_SEND_FIRST),                     \
	ib_opcode_name(RC_SEND_MIDDLE),                    \
	ib_opcode_name(RC_SEND_LAST),                      \
	ib_opcode_name(RC_SEND_LAST_WITH_IMMEDIATE),       \
	ib_opcode_name(RC_SEND_ONLY),                      \
	ib_opcode_name(RC_SEND_ONLY_WITH_IMMEDIATE),       \
	ib_opcode_name(RC_RDMA_WRITE_FIRST),               \
	ib_opcode_name(RC_RDMA_WRITE_MIDDLE),              \
	ib_opcode_name(RC_RDMA_WRITE_LAST),                \
	ib_opcode_name(RC_RDMA_WRITE_LAST_WITH_IMMEDIATE), \
	ib_opcode_name(RC_RDMA_WRITE_ONLY),                \
	ib_opcode_name(RC_RDMA_WRITE_ONLY_WITH_IMMEDIATE), \
	ib_opcode_name(RC_RDMA_READ_REQUEST),              \
	ib_opcode_name(RC_RDMA_READ_RESPONSE_FIRST),       \
	ib_opcode_name(RC_RDMA_READ_RESPONSE_MIDDLE),      \
	ib_opcode_name(RC_RDMA_READ_RESPONSE_LAST),        \
	ib_opcode_name(RC_RDMA_READ_RESPONSE_ONLY),        \
	ib_opcode_name(RC_ACKNOWLEDGE),                    \
	ib_opcode_name(RC_ATOMIC_ACKNOWLEDGE),             \
	ib_opcode_name(RC_COMPARE_SWAP),                   \
	ib_opcode_name(RC_FETCH_ADD),                      \
	ib_opcode_name(UC_SEND_FIRST),                     \
	ib_opcode_name(UC_SEND_MIDDLE),                    \
	ib_opcode_name(UC_SEND_LAST),                      \
	ib_opcode_name(UC_SEND_LAST_WITH_IMMEDIATE),       \
	ib_opcode_name(UC_SEND_ONLY),                      \
	ib_opcode_name(UC_SEND_ONLY_WITH_IMMEDIATE),       \
	ib_opcode_name(UC_RDMA_WRITE_FIRST),               \
	ib_opcode_name(UC_RDMA_WRITE_MIDDLE),              \
	ib_opcode_name(UC_RDMA_WRITE_LAST),                \
	ib_opcode_name(UC_RDMA_WRITE_LAST_WITH_IMMEDIATE), \
	ib_opcode_name(UC_RDMA_WRITE_ONLY),                \
	ib_opcode_name(UC_RDMA_WRITE_ONLY_WITH_IMMEDIATE), \
	ib_opcode_name(UD_SEND_ONLY),                      \
	ib_opcode_name(UD_SEND_ONLY_WITH_IMMEDIATE),       \
	ib_opcode_name(CNP))
#endif                          /* _HFI1_KERNEL_H */
