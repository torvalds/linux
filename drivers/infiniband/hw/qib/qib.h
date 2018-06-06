#ifndef _QIB_KERNEL_H
#define _QIB_KERNEL_H
/*
 * Copyright (c) 2012 - 2017 Intel Corporation.  All rights reserved.
 * Copyright (c) 2006 - 2012 QLogic Corporation. All rights reserved.
 * Copyright (c) 2003, 2004, 2005, 2006 PathScale, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * This header file is the base header file for qlogic_ib kernel code
 * qib_user.h serves a similar purpose for user code.
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
#include <linux/kthread.h>
#include <rdma/ib_hdrs.h>
#include <rdma/rdma_vt.h>

#include "qib_common.h"
#include "qib_verbs.h"

/* only s/w major version of QLogic_IB we can handle */
#define QIB_CHIP_VERS_MAJ 2U

/* don't care about this except printing */
#define QIB_CHIP_VERS_MIN 0U

/* The Organization Unique Identifier (Mfg code), and its position in GUID */
#define QIB_OUI 0x001175
#define QIB_OUI_LSB 40

/*
 * per driver stats, either not device nor port-specific, or
 * summed over all of the devices and ports.
 * They are described by name via ipathfs filesystem, so layout
 * and number of elements can change without breaking compatibility.
 * If members are added or deleted qib_statnames[] in qib_fs.c must
 * change to match.
 */
struct qlogic_ib_stats {
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

extern struct qlogic_ib_stats qib_stats;
extern const struct pci_error_handlers qib_pci_err_handler;

#define QIB_CHIP_SWVERSION QIB_CHIP_VERS_MAJ
/*
 * First-cut critierion for "device is active" is
 * two thousand dwords combined Tx, Rx traffic per
 * 5-second interval. SMA packets are 64 dwords,
 * and occur "a few per second", presumably each way.
 */
#define QIB_TRAFFIC_ACTIVE_THRESHOLD (2000)

/*
 * Below contains all data related to a single context (formerly called port).
 */

#ifdef CONFIG_DEBUG_FS
struct qib_opcode_stats_perctx;
#endif

struct qib_ctxtdata {
	void **rcvegrbuf;
	dma_addr_t *rcvegrbuf_phys;
	/* rcvhdrq base, needs mmap before useful */
	void *rcvhdrq;
	/* kernel virtual address where hdrqtail is updated */
	void *rcvhdrtail_kvaddr;
	/*
	 * temp buffer for expected send setup, allocated at open, instead
	 * of each setup call
	 */
	void *tid_pg_list;
	/*
	 * Shared page for kernel to signal user processes that send buffers
	 * need disarming.  The process should call QIB_CMD_DISARM_BUFS
	 * or QIB_CMD_ACK_EVENT with IPATH_EVENT_DISARM_BUFS set.
	 */
	unsigned long *user_event_mask;
	/* when waiting for rcv or pioavail */
	wait_queue_head_t wait;
	/*
	 * rcvegr bufs base, physical, must fit
	 * in 44 bits so 32 bit programs mmap64 44 bit works)
	 */
	dma_addr_t rcvegr_phys;
	/* mmap of hdrq, must fit in 44 bits */
	dma_addr_t rcvhdrq_phys;
	dma_addr_t rcvhdrqtailaddr_phys;

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
	/* local node of context */
	int node_id;
	/* non-zero if ctxt is being shared. */
	u16 subctxt_cnt;
	/* non-zero if ctxt is being shared. */
	u16 subctxt_id;
	/* number of eager TID entries. */
	u16 rcvegrcnt;
	/* index of first eager TID entry. */
	u16 rcvegr_tid_base;
	/* number of pio bufs for this ctxt (all procs, if shared) */
	u32 piocnt;
	/* first pio buffer for this ctxt */
	u32 pio_base;
	/* chip offset of PIO buffers for this ctxt */
	u32 piobufs;
	/* how many alloc_pages() chunks in rcvegrbuf_pages */
	u32 rcvegrbuf_chunks;
	/* how many egrbufs per chunk */
	u16 rcvegrbufs_perchunk;
	/* ilog2 of above */
	u16 rcvegrbufs_perchunk_shift;
	/* order for rcvegrbuf_pages */
	size_t rcvegrbuf_size;
	/* rcvhdrq size (for freeing) */
	size_t rcvhdrq_size;
	/* per-context flags for fileops/intr communication */
	unsigned long flag;
	/* next expected TID to check when looking for free */
	u32 tidcursor;
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
	/* pid of process using this ctxt */
	pid_t pid;
	pid_t subpid[QLOGIC_IB_MAX_SUBCTXT];
	/* same size as task_struct .comm[], command that opened context */
	char comm[16];
	/* pkeys set by this use of this ctxt */
	u16 pkeys[4];
	/* so file ops can get at unit */
	struct qib_devdata *dd;
	/* so funcs that need physical port can get it easily */
	struct qib_pportdata *ppd;
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
	/* QPs waiting for context processing */
	struct list_head qp_wait_list;
#ifdef CONFIG_DEBUG_FS
	/* verbs stats per CTX */
	struct qib_opcode_stats_perctx *opstats;
#endif
};

struct rvt_sge_state;

struct qib_sdma_txreq {
	int                 flags;
	int                 sg_count;
	dma_addr_t          addr;
	void              (*callback)(struct qib_sdma_txreq *, int);
	u16                 start_idx;  /* sdma private */
	u16                 next_descq_idx;  /* sdma private */
	struct list_head    list;       /* sdma private */
};

struct qib_sdma_desc {
	__le64 qw[2];
};

struct qib_verbs_txreq {
	struct qib_sdma_txreq   txreq;
	struct rvt_qp           *qp;
	struct rvt_swqe         *wqe;
	u32                     dwords;
	u16                     hdr_dwords;
	u16                     hdr_inx;
	struct qib_pio_header	*align_buf;
	struct rvt_mregion	*mr;
	struct rvt_sge_state    *ss;
};

#define QIB_SDMA_TXREQ_F_USELARGEBUF  0x1
#define QIB_SDMA_TXREQ_F_HEADTOHOST   0x2
#define QIB_SDMA_TXREQ_F_INTREQ       0x4
#define QIB_SDMA_TXREQ_F_FREEBUF      0x8
#define QIB_SDMA_TXREQ_F_FREEDESC     0x10

#define QIB_SDMA_TXREQ_S_OK        0
#define QIB_SDMA_TXREQ_S_SENDERROR 1
#define QIB_SDMA_TXREQ_S_ABORTED   2
#define QIB_SDMA_TXREQ_S_SHUTDOWN  3

/*
 * Get/Set IB link-level config parameters for f_get/set_ib_cfg()
 * Mostly for MADs that set or query link parameters, also ipath
 * config interfaces
 */
#define QIB_IB_CFG_LIDLMC 0 /* LID (LS16b) and Mask (MS16b) */
#define QIB_IB_CFG_LWID_ENB 2 /* allowed Link-width */
#define QIB_IB_CFG_LWID 3 /* currently active Link-width */
#define QIB_IB_CFG_SPD_ENB 4 /* allowed Link speeds */
#define QIB_IB_CFG_SPD 5 /* current Link spd */
#define QIB_IB_CFG_RXPOL_ENB 6 /* Auto-RX-polarity enable */
#define QIB_IB_CFG_LREV_ENB 7 /* Auto-Lane-reversal enable */
#define QIB_IB_CFG_LINKLATENCY 8 /* Link Latency (IB1.2 only) */
#define QIB_IB_CFG_HRTBT 9 /* IB heartbeat off/enable/auto; DDR/QDR only */
#define QIB_IB_CFG_OP_VLS 10 /* operational VLs */
#define QIB_IB_CFG_VL_HIGH_CAP 11 /* num of VL high priority weights */
#define QIB_IB_CFG_VL_LOW_CAP 12 /* num of VL low priority weights */
#define QIB_IB_CFG_OVERRUN_THRESH 13 /* IB overrun threshold */
#define QIB_IB_CFG_PHYERR_THRESH 14 /* IB PHY error threshold */
#define QIB_IB_CFG_LINKDEFAULT 15 /* IB link default (sleep/poll) */
#define QIB_IB_CFG_PKEYS 16 /* update partition keys */
#define QIB_IB_CFG_MTU 17 /* update MTU in IBC */
#define QIB_IB_CFG_LSTATE 18 /* update linkcmd and linkinitcmd in IBC */
#define QIB_IB_CFG_VL_HIGH_LIMIT 19
#define QIB_IB_CFG_PMA_TICKS 20 /* PMA sample tick resolution */
#define QIB_IB_CFG_PORT 21 /* switch port we are connected to */

/*
 * for CFG_LSTATE: LINKCMD in upper 16 bits, LINKINITCMD in lower 16
 * IB_LINKINITCMD_POLL and SLEEP are also used as set/get values for
 * QIB_IB_CFG_LINKDEFAULT cmd
 */
#define   IB_LINKCMD_DOWN   (0 << 16)
#define   IB_LINKCMD_ARMED  (1 << 16)
#define   IB_LINKCMD_ACTIVE (2 << 16)
#define   IB_LINKINITCMD_NOP     0
#define   IB_LINKINITCMD_POLL    1
#define   IB_LINKINITCMD_SLEEP   2
#define   IB_LINKINITCMD_DISABLE 3

/*
 * valid states passed to qib_set_linkstate() user call
 */
#define QIB_IB_LINKDOWN         0
#define QIB_IB_LINKARM          1
#define QIB_IB_LINKACTIVE       2
#define QIB_IB_LINKDOWN_ONLY    3
#define QIB_IB_LINKDOWN_SLEEP   4
#define QIB_IB_LINKDOWN_DISABLE 5

/*
 * These 7 values (SDR, DDR, and QDR may be ORed for auto-speed
 * negotiation) are used for the 3rd argument to path_f_set_ib_cfg
 * with cmd QIB_IB_CFG_SPD_ENB, by direct calls or via sysfs.  They
 * are also the the possible values for qib_link_speed_enabled and active
 * The values were chosen to match values used within the IB spec.
 */
#define QIB_IB_SDR 1
#define QIB_IB_DDR 2
#define QIB_IB_QDR 4

#define QIB_DEFAULT_MTU 4096

/* max number of IB ports supported per HCA */
#define QIB_MAX_IB_PORTS 2

/*
 * Possible IB config parameters for f_get/set_ib_table()
 */
#define QIB_IB_TBL_VL_HIGH_ARB 1 /* Get/set VL high priority weights */
#define QIB_IB_TBL_VL_LOW_ARB 2 /* Get/set VL low priority weights */

/*
 * Possible "operations" for f_rcvctrl(ppd, op, ctxt)
 * these are bits so they can be combined, e.g.
 * QIB_RCVCTRL_INTRAVAIL_ENB | QIB_RCVCTRL_CTXT_ENB
 */
#define QIB_RCVCTRL_TAILUPD_ENB 0x01
#define QIB_RCVCTRL_TAILUPD_DIS 0x02
#define QIB_RCVCTRL_CTXT_ENB 0x04
#define QIB_RCVCTRL_CTXT_DIS 0x08
#define QIB_RCVCTRL_INTRAVAIL_ENB 0x10
#define QIB_RCVCTRL_INTRAVAIL_DIS 0x20
#define QIB_RCVCTRL_PKEY_ENB 0x40  /* Note, default is enabled */
#define QIB_RCVCTRL_PKEY_DIS 0x80
#define QIB_RCVCTRL_BP_ENB 0x0100
#define QIB_RCVCTRL_BP_DIS 0x0200
#define QIB_RCVCTRL_TIDFLOW_ENB 0x0400
#define QIB_RCVCTRL_TIDFLOW_DIS 0x0800

/*
 * Possible "operations" for f_sendctrl(ppd, op, var)
 * these are bits so they can be combined, e.g.
 * QIB_SENDCTRL_BUFAVAIL_ENB | QIB_SENDCTRL_ENB
 * Some operations (e.g. DISARM, ABORT) are known to
 * be "one-shot", so do not modify shadow.
 */
#define QIB_SENDCTRL_DISARM       (0x1000)
#define QIB_SENDCTRL_DISARM_BUF(bufn) ((bufn) | QIB_SENDCTRL_DISARM)
	/* available (0x2000) */
#define QIB_SENDCTRL_AVAIL_DIS    (0x4000)
#define QIB_SENDCTRL_AVAIL_ENB    (0x8000)
#define QIB_SENDCTRL_AVAIL_BLIP  (0x10000)
#define QIB_SENDCTRL_SEND_DIS    (0x20000)
#define QIB_SENDCTRL_SEND_ENB    (0x40000)
#define QIB_SENDCTRL_FLUSH       (0x80000)
#define QIB_SENDCTRL_CLEAR      (0x100000)
#define QIB_SENDCTRL_DISARM_ALL (0x200000)

/*
 * These are the generic indices for requesting per-port
 * counter values via the f_portcntr function.  They
 * are always returned as 64 bit values, although most
 * are 32 bit counters.
 */
/* send-related counters */
#define QIBPORTCNTR_PKTSEND         0U
#define QIBPORTCNTR_WORDSEND        1U
#define QIBPORTCNTR_PSXMITDATA      2U
#define QIBPORTCNTR_PSXMITPKTS      3U
#define QIBPORTCNTR_PSXMITWAIT      4U
#define QIBPORTCNTR_SENDSTALL       5U
/* receive-related counters */
#define QIBPORTCNTR_PKTRCV          6U
#define QIBPORTCNTR_PSRCVDATA       7U
#define QIBPORTCNTR_PSRCVPKTS       8U
#define QIBPORTCNTR_RCVEBP          9U
#define QIBPORTCNTR_RCVOVFL         10U
#define QIBPORTCNTR_WORDRCV         11U
/* IB link related error counters */
#define QIBPORTCNTR_RXLOCALPHYERR   12U
#define QIBPORTCNTR_RXVLERR         13U
#define QIBPORTCNTR_ERRICRC         14U
#define QIBPORTCNTR_ERRVCRC         15U
#define QIBPORTCNTR_ERRLPCRC        16U
#define QIBPORTCNTR_BADFORMAT       17U
#define QIBPORTCNTR_ERR_RLEN        18U
#define QIBPORTCNTR_IBSYMBOLERR     19U
#define QIBPORTCNTR_INVALIDRLEN     20U
#define QIBPORTCNTR_UNSUPVL         21U
#define QIBPORTCNTR_EXCESSBUFOVFL   22U
#define QIBPORTCNTR_ERRLINK         23U
#define QIBPORTCNTR_IBLINKDOWN      24U
#define QIBPORTCNTR_IBLINKERRRECOV  25U
#define QIBPORTCNTR_LLI             26U
/* other error counters */
#define QIBPORTCNTR_RXDROPPKT       27U
#define QIBPORTCNTR_VL15PKTDROP     28U
#define QIBPORTCNTR_ERRPKEY         29U
#define QIBPORTCNTR_KHDROVFL        30U
/* sampling counters (these are actually control registers) */
#define QIBPORTCNTR_PSINTERVAL      31U
#define QIBPORTCNTR_PSSTART         32U
#define QIBPORTCNTR_PSSTAT          33U

/* how often we check for packet activity for "power on hours (in seconds) */
#define ACTIVITY_TIMER 5

#define MAX_NAME_SIZE 64

#ifdef CONFIG_INFINIBAND_QIB_DCA
struct qib_irq_notify;
#endif

struct qib_msix_entry {
	void *arg;
#ifdef CONFIG_INFINIBAND_QIB_DCA
	int dca;
	int rcv;
	struct qib_irq_notify *notifier;
#endif
	cpumask_var_t mask;
};

/* Below is an opaque struct. Each chip (device) can maintain
 * private data needed for its operation, but not germane to the
 * rest of the driver.  For convenience, we define another that
 * is chip-specific, per-port
 */
struct qib_chip_specific;
struct qib_chipport_specific;

enum qib_sdma_states {
	qib_sdma_state_s00_hw_down,
	qib_sdma_state_s10_hw_start_up_wait,
	qib_sdma_state_s20_idle,
	qib_sdma_state_s30_sw_clean_up_wait,
	qib_sdma_state_s40_hw_clean_up_wait,
	qib_sdma_state_s50_hw_halt_wait,
	qib_sdma_state_s99_running,
};

enum qib_sdma_events {
	qib_sdma_event_e00_go_hw_down,
	qib_sdma_event_e10_go_hw_start,
	qib_sdma_event_e20_hw_started,
	qib_sdma_event_e30_go_running,
	qib_sdma_event_e40_sw_cleaned,
	qib_sdma_event_e50_hw_cleaned,
	qib_sdma_event_e60_hw_halted,
	qib_sdma_event_e70_go_idle,
	qib_sdma_event_e7220_err_halted,
	qib_sdma_event_e7322_err_halted,
	qib_sdma_event_e90_timer_tick,
};

struct sdma_set_state_action {
	unsigned op_enable:1;
	unsigned op_intenable:1;
	unsigned op_halt:1;
	unsigned op_drain:1;
	unsigned go_s99_running_tofalse:1;
	unsigned go_s99_running_totrue:1;
};

struct qib_sdma_state {
	struct kref          kref;
	struct completion    comp;
	enum qib_sdma_states current_state;
	struct sdma_set_state_action *set_state_action;
	unsigned             current_op;
	unsigned             go_s99_running;
	unsigned             first_sendbuf;
	unsigned             last_sendbuf; /* really last +1 */
	/* debugging/devel */
	enum qib_sdma_states previous_state;
	unsigned             previous_op;
	enum qib_sdma_events last_event;
};

struct xmit_wait {
	struct timer_list timer;
	u64 counter;
	u8 flags;
	struct cache {
		u64 psxmitdata;
		u64 psrcvdata;
		u64 psxmitpkts;
		u64 psrcvpkts;
		u64 psxmitwait;
	} counter_cache;
};

/*
 * The structure below encapsulates data relevant to a physical IB Port.
 * Current chips support only one such port, but the separation
 * clarifies things a bit. Note that to conform to IB conventions,
 * port-numbers are one-based. The first or only port is port1.
 */
struct qib_pportdata {
	struct qib_ibport ibport_data;

	struct qib_devdata *dd;
	struct qib_chippport_specific *cpspec; /* chip-specific per-port */
	struct kobject pport_kobj;
	struct kobject pport_cc_kobj;
	struct kobject sl2vl_kobj;
	struct kobject diagc_kobj;

	/* GUID for this interface, in network order */
	__be64 guid;

	/* QIB_POLL, etc. link-state specific flags, per port */
	u32 lflags;
	/* qib_lflags driver is waiting for */
	u32 state_wanted;
	spinlock_t lflags_lock;

	/* ref count for each pkey */
	atomic_t pkeyrefs[4];

	/*
	 * this address is mapped readonly into user processes so they can
	 * get status cheaply, whenever they want.  One qword of status per port
	 */
	u64 *statusp;

	/* SendDMA related entries */

	/* read mostly */
	struct qib_sdma_desc *sdma_descq;
	struct workqueue_struct *qib_wq;
	struct qib_sdma_state sdma_state;
	dma_addr_t       sdma_descq_phys;
	volatile __le64 *sdma_head_dma; /* DMA'ed by chip */
	dma_addr_t       sdma_head_phys;
	u16                   sdma_descq_cnt;

	/* read/write using lock */
	spinlock_t            sdma_lock ____cacheline_aligned_in_smp;
	struct list_head      sdma_activelist;
	struct list_head      sdma_userpending;
	u64                   sdma_descq_added;
	u64                   sdma_descq_removed;
	u16                   sdma_descq_tail;
	u16                   sdma_descq_head;
	u8                    sdma_generation;
	u8                    sdma_intrequest;

	struct tasklet_struct sdma_sw_clean_up_task
		____cacheline_aligned_in_smp;

	wait_queue_head_t state_wait; /* for state_wanted */

	/* HoL blocking for SMP replies */
	unsigned          hol_state;
	struct timer_list hol_timer;

	/*
	 * Shadow copies of registers; size indicates read access size.
	 * Most of them are readonly, but some are write-only register,
	 * where we manipulate the bits in the shadow copy, and then write
	 * the shadow copy to qlogic_ib.
	 *
	 * We deliberately make most of these 32 bits, since they have
	 * restricted range.  For any that we read, we won't to generate 32
	 * bit accesses, since Opteron will generate 2 separate 32 bit HT
	 * transactions for a 64 bit read, and we want to avoid unnecessary
	 * bus transactions.
	 */

	/* This is the 64 bit group */
	/* last ibcstatus.  opaque outside chip-specific code */
	u64 lastibcstat;

	/* these are the "32 bit" regs */

	/*
	 * the following two are 32-bit bitmasks, but {test,clear,set}_bit
	 * all expect bit fields to be "unsigned long"
	 */
	unsigned long p_rcvctrl; /* shadow per-port rcvctrl */
	unsigned long p_sendctrl; /* shadow per-port sendctrl */

	u32 ibmtu; /* The MTU programmed for this unit */
	/*
	 * Current max size IB packet (in bytes) including IB headers, that
	 * we can send. Changes when ibmtu changes.
	 */
	u32 ibmaxlen;
	/*
	 * ibmaxlen at init time, limited by chip and by receive buffer
	 * size.  Not changed after init.
	 */
	u32 init_ibmaxlen;
	/* LID programmed for this instance */
	u16 lid;
	/* list of pkeys programmed; 0 if not set */
	u16 pkeys[4];
	/* LID mask control */
	u8 lmc;
	u8 link_width_supported;
	u8 link_speed_supported;
	u8 link_width_enabled;
	u8 link_speed_enabled;
	u8 link_width_active;
	u8 link_speed_active;
	u8 vls_supported;
	u8 vls_operational;
	/* Rx Polarity inversion (compensate for ~tx on partner) */
	u8 rx_pol_inv;

	u8 hw_pidx;     /* physical port index */
	u8 port;        /* IB port number and index into dd->pports - 1 */

	u8 delay_mult;

	/* used to override LED behavior */
	u8 led_override;  /* Substituted for normal value, if non-zero */
	u16 led_override_timeoff; /* delta to next timer event */
	u8 led_override_vals[2]; /* Alternates per blink-frame */
	u8 led_override_phase; /* Just counts, LSB picks from vals[] */
	atomic_t led_override_timer_active;
	/* Used to flash LEDs in override mode */
	struct timer_list led_override_timer;
	struct xmit_wait cong_stats;
	struct timer_list symerr_clear_timer;

	/* Synchronize access between driver writes and sysfs reads */
	spinlock_t cc_shadow_lock
		____cacheline_aligned_in_smp;

	/* Shadow copy of the congestion control table */
	struct cc_table_shadow *ccti_entries_shadow;

	/* Shadow copy of the congestion control entries */
	struct ib_cc_congestion_setting_attr_shadow *congestion_entries_shadow;

	/* List of congestion control table entries */
	struct ib_cc_table_entry_shadow *ccti_entries;

	/* 16 congestion entries with each entry corresponding to a SL */
	struct ib_cc_congestion_entry_shadow *congestion_entries;

	/* Maximum number of congestion control entries that the agent expects
	 * the manager to send.
	 */
	u16 cc_supported_table_entries;

	/* Total number of congestion control table entries */
	u16 total_cct_entry;

	/* Bit map identifying service level */
	u16 cc_sl_control_map;

	/* maximum congestion control table index */
	u16 ccti_limit;

	/* CA's max number of 64 entry units in the congestion control table */
	u8 cc_max_table_entries;
};

/* Observers. Not to be taken lightly, possibly not to ship. */
/*
 * If a diag read or write is to (bottom <= offset <= top),
 * the "hoook" is called, allowing, e.g. shadows to be
 * updated in sync with the driver. struct diag_observer
 * is the "visible" part.
 */
struct diag_observer;

typedef int (*diag_hook) (struct qib_devdata *dd,
	const struct diag_observer *op,
	u32 offs, u64 *data, u64 mask, int only_32);

struct diag_observer {
	diag_hook hook;
	u32 bottom;
	u32 top;
};

extern int qib_register_observer(struct qib_devdata *dd,
	const struct diag_observer *op);

/* Only declared here, not defined. Private to diags */
struct diag_observer_list_elt;

/* device data struct now contains only "general per-device" info.
 * fields related to a physical IB port are in a qib_pportdata struct,
 * described above) while fields only used by a particular chip-type are in
 * a qib_chipdata struct, whose contents are opaque to this file.
 */
struct qib_devdata {
	struct qib_ibdev verbs_dev;     /* must be first */
	struct list_head list;
	/* pointers to related structs for this device */
	/* pci access data structure */
	struct pci_dev *pcidev;
	struct cdev *user_cdev;
	struct cdev *diag_cdev;
	struct device *user_device;
	struct device *diag_device;

	/* mem-mapped pointer to base of chip regs */
	u64 __iomem *kregbase;
	/* end of mem-mapped chip space excluding sendbuf and user regs */
	u64 __iomem *kregend;
	/* physical address of chip for io_remap, etc. */
	resource_size_t physaddr;
	/* qib_cfgctxts pointers */
	struct qib_ctxtdata **rcd; /* Receive Context Data */

	/* qib_pportdata, points to array of (physical) port-specific
	 * data structs, indexed by pidx (0..n-1)
	 */
	struct qib_pportdata *pport;
	struct qib_chip_specific *cspec; /* chip-specific */

	/* kvirt address of 1st 2k pio buffer */
	void __iomem *pio2kbase;
	/* kvirt address of 1st 4k pio buffer */
	void __iomem *pio4kbase;
	/* mem-mapped pointer to base of PIO buffers (if using WC PAT) */
	void __iomem *piobase;
	/* mem-mapped pointer to base of user chip regs (if using WC PAT) */
	u64 __iomem *userbase;
	void __iomem *piovl15base; /* base of VL15 buffers, if not WC */
	/*
	 * points to area where PIOavail registers will be DMA'ed.
	 * Has to be on a page of it's own, because the page will be
	 * mapped into user program space.  This copy is *ONLY* ever
	 * written by DMA, not by the driver!  Need a copy per device
	 * when we get to multiple devices
	 */
	volatile __le64 *pioavailregs_dma; /* DMA'ed by chip */
	/* physical address where updates occur */
	dma_addr_t pioavailregs_phys;

	/* device-specific implementations of functions needed by
	 * common code. Contrary to previous consensus, we can't
	 * really just point to a device-specific table, because we
	 * may need to "bend", e.g. *_f_put_tid
	 */
	/* fallback to alternate interrupt type if possible */
	int (*f_intr_fallback)(struct qib_devdata *);
	/* hard reset chip */
	int (*f_reset)(struct qib_devdata *);
	void (*f_quiet_serdes)(struct qib_pportdata *);
	int (*f_bringup_serdes)(struct qib_pportdata *);
	int (*f_early_init)(struct qib_devdata *);
	void (*f_clear_tids)(struct qib_devdata *, struct qib_ctxtdata *);
	void (*f_put_tid)(struct qib_devdata *, u64 __iomem*,
				u32, unsigned long);
	void (*f_cleanup)(struct qib_devdata *);
	void (*f_setextled)(struct qib_pportdata *, u32);
	/* fill out chip-specific fields */
	int (*f_get_base_info)(struct qib_ctxtdata *, struct qib_base_info *);
	/* free irq */
	void (*f_free_irq)(struct qib_devdata *);
	struct qib_message_header *(*f_get_msgheader)
					(struct qib_devdata *, __le32 *);
	void (*f_config_ctxts)(struct qib_devdata *);
	int (*f_get_ib_cfg)(struct qib_pportdata *, int);
	int (*f_set_ib_cfg)(struct qib_pportdata *, int, u32);
	int (*f_set_ib_loopback)(struct qib_pportdata *, const char *);
	int (*f_get_ib_table)(struct qib_pportdata *, int, void *);
	int (*f_set_ib_table)(struct qib_pportdata *, int, void *);
	u32 (*f_iblink_state)(u64);
	u8 (*f_ibphys_portstate)(u64);
	void (*f_xgxs_reset)(struct qib_pportdata *);
	/* per chip actions needed for IB Link up/down changes */
	int (*f_ib_updown)(struct qib_pportdata *, int, u64);
	u32 __iomem *(*f_getsendbuf)(struct qib_pportdata *, u64, u32 *);
	/* Read/modify/write of GPIO pins (potentially chip-specific */
	int (*f_gpio_mod)(struct qib_devdata *dd, u32 out, u32 dir,
		u32 mask);
	/* Enable writes to config EEPROM (if supported) */
	int (*f_eeprom_wen)(struct qib_devdata *dd, int wen);
	/*
	 * modify rcvctrl shadow[s] and write to appropriate chip-regs.
	 * see above QIB_RCVCTRL_xxx_ENB/DIS for operations.
	 * (ctxt == -1) means "all contexts", only meaningful for
	 * clearing. Could remove if chip_spec shutdown properly done.
	 */
	void (*f_rcvctrl)(struct qib_pportdata *, unsigned int op,
		int ctxt);
	/* Read/modify/write sendctrl appropriately for op and port. */
	void (*f_sendctrl)(struct qib_pportdata *, u32 op);
	void (*f_set_intr_state)(struct qib_devdata *, u32);
	void (*f_set_armlaunch)(struct qib_devdata *, u32);
	void (*f_wantpiobuf_intr)(struct qib_devdata *, u32);
	int (*f_late_initreg)(struct qib_devdata *);
	int (*f_init_sdma_regs)(struct qib_pportdata *);
	u16 (*f_sdma_gethead)(struct qib_pportdata *);
	int (*f_sdma_busy)(struct qib_pportdata *);
	void (*f_sdma_update_tail)(struct qib_pportdata *, u16);
	void (*f_sdma_set_desc_cnt)(struct qib_pportdata *, unsigned);
	void (*f_sdma_sendctrl)(struct qib_pportdata *, unsigned);
	void (*f_sdma_hw_clean_up)(struct qib_pportdata *);
	void (*f_sdma_hw_start_up)(struct qib_pportdata *);
	void (*f_sdma_init_early)(struct qib_pportdata *);
	void (*f_set_cntr_sample)(struct qib_pportdata *, u32, u32);
	void (*f_update_usrhead)(struct qib_ctxtdata *, u64, u32, u32, u32);
	u32 (*f_hdrqempty)(struct qib_ctxtdata *);
	u64 (*f_portcntr)(struct qib_pportdata *, u32);
	u32 (*f_read_cntrs)(struct qib_devdata *, loff_t, char **,
		u64 **);
	u32 (*f_read_portcntrs)(struct qib_devdata *, loff_t, u32,
		char **, u64 **);
	u32 (*f_setpbc_control)(struct qib_pportdata *, u32, u8, u8);
	void (*f_initvl15_bufs)(struct qib_devdata *);
	void (*f_init_ctxt)(struct qib_ctxtdata *);
	void (*f_txchk_change)(struct qib_devdata *, u32, u32, u32,
		struct qib_ctxtdata *);
	void (*f_writescratch)(struct qib_devdata *, u32);
	int (*f_tempsense_rd)(struct qib_devdata *, int regnum);
#ifdef CONFIG_INFINIBAND_QIB_DCA
	int (*f_notify_dca)(struct qib_devdata *, unsigned long event);
#endif

	char *boardname; /* human readable board info */

	/* template for writing TIDs  */
	u64 tidtemplate;
	/* value to write to free TIDs */
	u64 tidinvalid;

	/* number of registers used for pioavail */
	u32 pioavregs;
	/* device (not port) flags, basically device capabilities */
	u32 flags;
	/* last buffer for user use */
	u32 lastctxt_piobuf;

	/* reset value */
	u64 z_int_counter;
	/* percpu intcounter */
	u64 __percpu *int_counter;

	/* pio bufs allocated per ctxt */
	u32 pbufsctxt;
	/* if remainder on bufs/ctxt, ctxts < extrabuf get 1 extra */
	u32 ctxts_extrabuf;
	/*
	 * number of ctxts configured as max; zero is set to number chip
	 * supports, less gives more pio bufs/ctxt, etc.
	 */
	u32 cfgctxts;
	/*
	 * number of ctxts available for PSM open
	 */
	u32 freectxts;

	/*
	 * hint that we should update pioavailshadow before
	 * looking for a PIO buffer
	 */
	u32 upd_pio_shadow;

	/* internal debugging stats */
	u32 maxpkts_call;
	u32 avgpkts_call;
	u64 nopiobufs;

	/* PCI Vendor ID (here for NodeInfo) */
	u16 vendorid;
	/* PCI Device ID (here for NodeInfo) */
	u16 deviceid;
	/* for write combining settings */
	int wc_cookie;
	unsigned long wc_base;
	unsigned long wc_len;

	/* shadow copy of struct page *'s for exp tid pages */
	struct page **pageshadow;
	/* shadow copy of dma handles for exp tid pages */
	dma_addr_t *physshadow;
	u64 __iomem *egrtidbase;
	spinlock_t sendctrl_lock; /* protect changes to sendctrl shadow */
	/* around rcd and (user ctxts) ctxt_cnt use (intr vs free) */
	spinlock_t uctxt_lock; /* rcd and user context changes */
	/*
	 * per unit status, see also portdata statusp
	 * mapped readonly into user processes so they can get unit and
	 * IB link status cheaply
	 */
	u64 *devstatusp;
	char *freezemsg; /* freeze msg if hw error put chip in freeze */
	u32 freezelen; /* max length of freezemsg */
	/* timer used to prevent stats overflow, error throttling, etc. */
	struct timer_list stats_timer;

	/* timer to verify interrupts work, and fallback if possible */
	struct timer_list intrchk_timer;
	unsigned long ureg_align; /* user register alignment */

	/*
	 * Protects pioavailshadow, pioavailkernel, pio_need_disarm, and
	 * pio_writing.
	 */
	spinlock_t pioavail_lock;
	/*
	 * index of last buffer to optimize search for next
	 */
	u32 last_pio;
	/*
	 * min kernel pio buffer to optimize search
	 */
	u32 min_kernel_pio;
	/*
	 * Shadow copies of registers; size indicates read access size.
	 * Most of them are readonly, but some are write-only register,
	 * where we manipulate the bits in the shadow copy, and then write
	 * the shadow copy to qlogic_ib.
	 *
	 * We deliberately make most of these 32 bits, since they have
	 * restricted range.  For any that we read, we won't to generate 32
	 * bit accesses, since Opteron will generate 2 separate 32 bit HT
	 * transactions for a 64 bit read, and we want to avoid unnecessary
	 * bus transactions.
	 */

	/* This is the 64 bit group */

	unsigned long pioavailshadow[6];
	/* bitmap of send buffers available for the kernel to use with PIO. */
	unsigned long pioavailkernel[6];
	/* bitmap of send buffers which need to be disarmed. */
	unsigned long pio_need_disarm[3];
	/* bitmap of send buffers which are being written to. */
	unsigned long pio_writing[3];
	/* kr_revision shadow */
	u64 revision;
	/* Base GUID for device (from eeprom, network order) */
	__be64 base_guid;

	/*
	 * kr_sendpiobufbase value (chip offset of pio buffers), and the
	 * base of the 2KB buffer s(user processes only use 2K)
	 */
	u64 piobufbase;
	u32 pio2k_bufbase;

	/* these are the "32 bit" regs */

	/* number of GUIDs in the flash for this interface */
	u32 nguid;
	/*
	 * the following two are 32-bit bitmasks, but {test,clear,set}_bit
	 * all expect bit fields to be "unsigned long"
	 */
	unsigned long rcvctrl; /* shadow per device rcvctrl */
	unsigned long sendctrl; /* shadow per device sendctrl */

	/* value we put in kr_rcvhdrcnt */
	u32 rcvhdrcnt;
	/* value we put in kr_rcvhdrsize */
	u32 rcvhdrsize;
	/* value we put in kr_rcvhdrentsize */
	u32 rcvhdrentsize;
	/* kr_ctxtcnt value */
	u32 ctxtcnt;
	/* kr_pagealign value */
	u32 palign;
	/* number of "2KB" PIO buffers */
	u32 piobcnt2k;
	/* size in bytes of "2KB" PIO buffers */
	u32 piosize2k;
	/* max usable size in dwords of a "2KB" PIO buffer before going "4KB" */
	u32 piosize2kmax_dwords;
	/* number of "4KB" PIO buffers */
	u32 piobcnt4k;
	/* size in bytes of "4KB" PIO buffers */
	u32 piosize4k;
	/* kr_rcvegrbase value */
	u32 rcvegrbase;
	/* kr_rcvtidbase value */
	u32 rcvtidbase;
	/* kr_rcvtidcnt value */
	u32 rcvtidcnt;
	/* kr_userregbase */
	u32 uregbase;
	/* shadow the control register contents */
	u32 control;

	/* chip address space used by 4k pio buffers */
	u32 align4k;
	/* size of each rcvegrbuffer */
	u16 rcvegrbufsize;
	/* log2 of above */
	u16 rcvegrbufsize_shift;
	/* localbus width (1, 2,4,8,16,32) from config space  */
	u32 lbus_width;
	/* localbus speed in MHz */
	u32 lbus_speed;
	int unit; /* unit # of this chip */

	/* start of CHIP_SPEC move to chipspec, but need code changes */
	/* low and high portions of MSI capability/vector */
	u32 msi_lo;
	/* saved after PCIe init for restore after reset */
	u32 msi_hi;
	/* MSI data (vector) saved for restore */
	u16 msi_data;
	/* so we can rewrite it after a chip reset */
	u32 pcibar0;
	/* so we can rewrite it after a chip reset */
	u32 pcibar1;
	u64 rhdrhead_intr_off;

	/*
	 * ASCII serial number, from flash, large enough for original
	 * all digit strings, and longer QLogic serial number format
	 */
	u8 serial[16];
	/* human readable board version */
	u8 boardversion[96];
	u8 lbus_info[32]; /* human readable localbus info */
	/* chip major rev, from qib_revision */
	u8 majrev;
	/* chip minor rev, from qib_revision */
	u8 minrev;

	/* Misc small ints */
	/* Number of physical ports available */
	u8 num_pports;
	/* Lowest context number which can be used by user processes */
	u8 first_user_ctxt;
	u8 n_krcv_queues;
	u8 qpn_mask;
	u8 skip_kctxt_mask;

	u16 rhf_offset; /* offset of RHF within receive header entry */

	/*
	 * GPIO pins for twsi-connected devices, and device code for eeprom
	 */
	u8 gpio_sda_num;
	u8 gpio_scl_num;
	u8 twsi_eeprom_dev;
	u8 board_atten;

	/* Support (including locks) for EEPROM logging of errors and time */
	/* control access to actual counters, timer */
	spinlock_t eep_st_lock;
	/* control high-level access to EEPROM */
	struct mutex eep_lock;
	uint64_t traffic_wds;
	struct qib_diag_client *diag_client;
	spinlock_t qib_diag_trans_lock; /* protect diag observer ops */
	struct diag_observer_list_elt *diag_observer_list;

	u8 psxmitwait_supported;
	/* cycle length of PS* counters in HW (in picoseconds) */
	u16 psxmitwait_check_rate;
	/* high volume overflow errors defered to tasklet */
	struct tasklet_struct error_tasklet;

	int assigned_node_id; /* NUMA node closest to HCA */
};

/* hol_state values */
#define QIB_HOL_UP       0
#define QIB_HOL_INIT     1

#define QIB_SDMA_SENDCTRL_OP_ENABLE    (1U << 0)
#define QIB_SDMA_SENDCTRL_OP_INTENABLE (1U << 1)
#define QIB_SDMA_SENDCTRL_OP_HALT      (1U << 2)
#define QIB_SDMA_SENDCTRL_OP_CLEANUP   (1U << 3)
#define QIB_SDMA_SENDCTRL_OP_DRAIN     (1U << 4)

/* operation types for f_txchk_change() */
#define TXCHK_CHG_TYPE_DIS1  3
#define TXCHK_CHG_TYPE_ENAB1 2
#define TXCHK_CHG_TYPE_KERN  1
#define TXCHK_CHG_TYPE_USER  0

#define QIB_CHASE_TIME msecs_to_jiffies(145)
#define QIB_CHASE_DIS_TIME msecs_to_jiffies(160)

/* Private data for file operations */
struct qib_filedata {
	struct qib_ctxtdata *rcd;
	unsigned subctxt;
	unsigned tidcursor;
	struct qib_user_sdma_queue *pq;
	int rec_cpu_num; /* for cpu affinity; -1 if none */
};

extern struct list_head qib_dev_list;
extern spinlock_t qib_devs_lock;
extern struct qib_devdata *qib_lookup(int unit);
extern u32 qib_cpulist_count;
extern unsigned long *qib_cpulist;
extern unsigned qib_cc_table_size;

int qib_init(struct qib_devdata *, int);
int init_chip_wc_pat(struct qib_devdata *dd, u32);
int qib_enable_wc(struct qib_devdata *dd);
void qib_disable_wc(struct qib_devdata *dd);
int qib_count_units(int *npresentp, int *nupp);
int qib_count_active_units(void);

int qib_cdev_init(int minor, const char *name,
		  const struct file_operations *fops,
		  struct cdev **cdevp, struct device **devp);
void qib_cdev_cleanup(struct cdev **cdevp, struct device **devp);
int qib_dev_init(void);
void qib_dev_cleanup(void);

int qib_diag_add(struct qib_devdata *);
void qib_diag_remove(struct qib_devdata *);
void qib_handle_e_ibstatuschanged(struct qib_pportdata *, u64);
void qib_sdma_update_tail(struct qib_pportdata *, u16); /* hold sdma_lock */

int qib_decode_err(struct qib_devdata *dd, char *buf, size_t blen, u64 err);
void qib_bad_intrstatus(struct qib_devdata *);
void qib_handle_urcv(struct qib_devdata *, u64);

/* clean up any per-chip chip-specific stuff */
void qib_chip_cleanup(struct qib_devdata *);
/* clean up any chip type-specific stuff */
void qib_chip_done(void);

/* check to see if we have to force ordering for write combining */
int qib_unordered_wc(void);
void qib_pio_copy(void __iomem *to, const void *from, size_t count);

void qib_disarm_piobufs(struct qib_devdata *, unsigned, unsigned);
int qib_disarm_piobufs_ifneeded(struct qib_ctxtdata *);
void qib_disarm_piobufs_set(struct qib_devdata *, unsigned long *, unsigned);
void qib_cancel_sends(struct qib_pportdata *);

int qib_create_rcvhdrq(struct qib_devdata *, struct qib_ctxtdata *);
int qib_setup_eagerbufs(struct qib_ctxtdata *);
void qib_set_ctxtcnt(struct qib_devdata *);
int qib_create_ctxts(struct qib_devdata *dd);
struct qib_ctxtdata *qib_create_ctxtdata(struct qib_pportdata *, u32, int);
int qib_init_pportdata(struct qib_pportdata *, struct qib_devdata *, u8, u8);
void qib_free_ctxtdata(struct qib_devdata *, struct qib_ctxtdata *);

u32 qib_kreceive(struct qib_ctxtdata *, u32 *, u32 *);
int qib_reset_device(int);
int qib_wait_linkstate(struct qib_pportdata *, u32, int);
int qib_set_linkstate(struct qib_pportdata *, u8);
int qib_set_mtu(struct qib_pportdata *, u16);
int qib_set_lid(struct qib_pportdata *, u32, u8);
void qib_hol_down(struct qib_pportdata *);
void qib_hol_init(struct qib_pportdata *);
void qib_hol_up(struct qib_pportdata *);
void qib_hol_event(struct timer_list *);
void qib_disable_after_error(struct qib_devdata *);
int qib_set_uevent_bits(struct qib_pportdata *, const int);

/* for use in system calls, where we want to know device type, etc. */
#define ctxt_fp(fp) \
	(((struct qib_filedata *)(fp)->private_data)->rcd)
#define subctxt_fp(fp) \
	(((struct qib_filedata *)(fp)->private_data)->subctxt)
#define tidcursor_fp(fp) \
	(((struct qib_filedata *)(fp)->private_data)->tidcursor)
#define user_sdma_queue_fp(fp) \
	(((struct qib_filedata *)(fp)->private_data)->pq)

static inline struct qib_devdata *dd_from_ppd(struct qib_pportdata *ppd)
{
	return ppd->dd;
}

static inline struct qib_devdata *dd_from_dev(struct qib_ibdev *dev)
{
	return container_of(dev, struct qib_devdata, verbs_dev);
}

static inline struct qib_devdata *dd_from_ibdev(struct ib_device *ibdev)
{
	return dd_from_dev(to_idev(ibdev));
}

static inline struct qib_pportdata *ppd_from_ibp(struct qib_ibport *ibp)
{
	return container_of(ibp, struct qib_pportdata, ibport_data);
}

static inline struct qib_ibport *to_iport(struct ib_device *ibdev, u8 port)
{
	struct qib_devdata *dd = dd_from_ibdev(ibdev);
	unsigned pidx = port - 1; /* IB number port from 1, hdw from 0 */

	WARN_ON(pidx >= dd->num_pports);
	return &dd->pport[pidx].ibport_data;
}

/*
 * values for dd->flags (_device_ related flags) and
 */
#define QIB_HAS_LINK_LATENCY  0x1 /* supports link latency (IB 1.2) */
#define QIB_INITTED           0x2 /* chip and driver up and initted */
#define QIB_DOING_RESET       0x4  /* in the middle of doing chip reset */
#define QIB_PRESENT           0x8  /* chip accesses can be done */
#define QIB_PIO_FLUSH_WC      0x10 /* Needs Write combining flush for PIO */
#define QIB_HAS_THRESH_UPDATE 0x40
#define QIB_HAS_SDMA_TIMEOUT  0x80
#define QIB_USE_SPCL_TRIG     0x100 /* SpecialTrigger launch enabled */
#define QIB_NODMA_RTAIL       0x200 /* rcvhdrtail register DMA enabled */
#define QIB_HAS_INTX          0x800 /* Supports INTx interrupts */
#define QIB_HAS_SEND_DMA      0x1000 /* Supports Send DMA */
#define QIB_HAS_VLSUPP        0x2000 /* Supports multiple VLs; PBC different */
#define QIB_HAS_HDRSUPP       0x4000 /* Supports header suppression */
#define QIB_BADINTR           0x8000 /* severe interrupt problems */
#define QIB_DCA_ENABLED       0x10000 /* Direct Cache Access enabled */
#define QIB_HAS_QSFP          0x20000 /* device (card instance) has QSFP */

/*
 * values for ppd->lflags (_ib_port_ related flags)
 */
#define QIBL_LINKV             0x1 /* IB link state valid */
#define QIBL_LINKDOWN          0x8 /* IB link is down */
#define QIBL_LINKINIT          0x10 /* IB link level is up */
#define QIBL_LINKARMED         0x20 /* IB link is ARMED */
#define QIBL_LINKACTIVE        0x40 /* IB link is ACTIVE */
/* leave a gap for more IB-link state */
#define QIBL_IB_AUTONEG_INPROG 0x1000 /* non-IBTA DDR/QDR neg active */
#define QIBL_IB_AUTONEG_FAILED 0x2000 /* non-IBTA DDR/QDR neg failed */
#define QIBL_IB_LINK_DISABLED  0x4000 /* Linkdown-disable forced,
				       * Do not try to bring up */
#define QIBL_IB_FORCE_NOTIFY   0x8000 /* force notify on next ib change */

/* IB dword length mask in PBC (lower 11 bits); same for all chips */
#define QIB_PBC_LENGTH_MASK                     ((1 << 11) - 1)


/* ctxt_flag bit offsets */
		/* waiting for a packet to arrive */
#define QIB_CTXT_WAITING_RCV   2
		/* master has not finished initializing */
#define QIB_CTXT_MASTER_UNINIT 4
		/* waiting for an urgent packet to arrive */
#define QIB_CTXT_WAITING_URG 5

/* free up any allocated data at closes */
void qib_free_data(struct qib_ctxtdata *dd);
void qib_chg_pioavailkernel(struct qib_devdata *, unsigned, unsigned,
			    u32, struct qib_ctxtdata *);
struct qib_devdata *qib_init_iba7322_funcs(struct pci_dev *,
					   const struct pci_device_id *);
struct qib_devdata *qib_init_iba7220_funcs(struct pci_dev *,
					   const struct pci_device_id *);
struct qib_devdata *qib_init_iba6120_funcs(struct pci_dev *,
					   const struct pci_device_id *);
void qib_free_devdata(struct qib_devdata *);
struct qib_devdata *qib_alloc_devdata(struct pci_dev *pdev, size_t extra);

#define QIB_TWSI_NO_DEV 0xFF
/* Below qib_twsi_ functions must be called with eep_lock held */
int qib_twsi_reset(struct qib_devdata *dd);
int qib_twsi_blk_rd(struct qib_devdata *dd, int dev, int addr, void *buffer,
		    int len);
int qib_twsi_blk_wr(struct qib_devdata *dd, int dev, int addr,
		    const void *buffer, int len);
void qib_get_eeprom_info(struct qib_devdata *);
void qib_dump_lookup_output_queue(struct qib_devdata *);
void qib_force_pio_avail_update(struct qib_devdata *);
void qib_clear_symerror_on_linkup(struct timer_list *t);

/*
 * Set LED override, only the two LSBs have "public" meaning, but
 * any non-zero value substitutes them for the Link and LinkTrain
 * LED states.
 */
#define QIB_LED_PHYS 1 /* Physical (linktraining) GREEN LED */
#define QIB_LED_LOG 2  /* Logical (link) YELLOW LED */
void qib_set_led_override(struct qib_pportdata *ppd, unsigned int val);

/* send dma routines */
int qib_setup_sdma(struct qib_pportdata *);
void qib_teardown_sdma(struct qib_pportdata *);
void __qib_sdma_intr(struct qib_pportdata *);
void qib_sdma_intr(struct qib_pportdata *);
void qib_user_sdma_send_desc(struct qib_pportdata *dd,
			struct list_head *pktlist);
int qib_sdma_verbs_send(struct qib_pportdata *, struct rvt_sge_state *,
			u32, struct qib_verbs_txreq *);
/* ppd->sdma_lock should be locked before calling this. */
int qib_sdma_make_progress(struct qib_pportdata *dd);

static inline int qib_sdma_empty(const struct qib_pportdata *ppd)
{
	return ppd->sdma_descq_added == ppd->sdma_descq_removed;
}

/* must be called under qib_sdma_lock */
static inline u16 qib_sdma_descq_freecnt(const struct qib_pportdata *ppd)
{
	return ppd->sdma_descq_cnt -
		(ppd->sdma_descq_added - ppd->sdma_descq_removed) - 1;
}

static inline int __qib_sdma_running(struct qib_pportdata *ppd)
{
	return ppd->sdma_state.current_state == qib_sdma_state_s99_running;
}
int qib_sdma_running(struct qib_pportdata *);
void dump_sdma_state(struct qib_pportdata *ppd);
void __qib_sdma_process_event(struct qib_pportdata *, enum qib_sdma_events);
void qib_sdma_process_event(struct qib_pportdata *, enum qib_sdma_events);

/*
 * number of words used for protocol header if not set by qib_userinit();
 */
#define QIB_DFLT_RCVHDRSIZE 9

/*
 * We need to be able to handle an IB header of at least 24 dwords.
 * We need the rcvhdrq large enough to handle largest IB header, but
 * still have room for a 2KB MTU standard IB packet.
 * Additionally, some processor/memory controller combinations
 * benefit quite strongly from having the DMA'ed data be cacheline
 * aligned and a cacheline multiple, so we set the size to 32 dwords
 * (2 64-byte primary cachelines for pretty much all processors of
 * interest).  The alignment hurts nothing, other than using somewhat
 * more memory.
 */
#define QIB_RCVHDR_ENTSIZE 32

int qib_get_user_pages(unsigned long, size_t, struct page **);
void qib_release_user_pages(struct page **, size_t);
int qib_eeprom_read(struct qib_devdata *, u8, void *, int);
int qib_eeprom_write(struct qib_devdata *, u8, const void *, int);
u32 __iomem *qib_getsendbuf_range(struct qib_devdata *, u32 *, u32, u32);
void qib_sendbuf_done(struct qib_devdata *, unsigned);

static inline void qib_clear_rcvhdrtail(const struct qib_ctxtdata *rcd)
{
	*((u64 *) rcd->rcvhdrtail_kvaddr) = 0ULL;
}

static inline u32 qib_get_rcvhdrtail(const struct qib_ctxtdata *rcd)
{
	/*
	 * volatile because it's a DMA target from the chip, routine is
	 * inlined, and don't want register caching or reordering.
	 */
	return (u32) le64_to_cpu(
		*((volatile __le64 *)rcd->rcvhdrtail_kvaddr)); /* DMA'ed */
}

static inline u32 qib_get_hdrqtail(const struct qib_ctxtdata *rcd)
{
	const struct qib_devdata *dd = rcd->dd;
	u32 hdrqtail;

	if (dd->flags & QIB_NODMA_RTAIL) {
		__le32 *rhf_addr;
		u32 seq;

		rhf_addr = (__le32 *) rcd->rcvhdrq +
			rcd->head + dd->rhf_offset;
		seq = qib_hdrget_seq(rhf_addr);
		hdrqtail = rcd->head;
		if (seq == rcd->seq_cnt)
			hdrqtail++;
	} else
		hdrqtail = qib_get_rcvhdrtail(rcd);

	return hdrqtail;
}

/*
 * sysfs interface.
 */

extern const char ib_qib_version[];

int qib_device_create(struct qib_devdata *);
void qib_device_remove(struct qib_devdata *);

int qib_create_port_files(struct ib_device *ibdev, u8 port_num,
			  struct kobject *kobj);
int qib_verbs_register_sysfs(struct qib_devdata *);
void qib_verbs_unregister_sysfs(struct qib_devdata *);
/* Hook for sysfs read of QSFP */
extern int qib_qsfp_dump(struct qib_pportdata *ppd, char *buf, int len);

int __init qib_init_qibfs(void);
int __exit qib_exit_qibfs(void);

int qibfs_add(struct qib_devdata *);
int qibfs_remove(struct qib_devdata *);

int qib_pcie_init(struct pci_dev *, const struct pci_device_id *);
int qib_pcie_ddinit(struct qib_devdata *, struct pci_dev *,
		    const struct pci_device_id *);
void qib_pcie_ddcleanup(struct qib_devdata *);
int qib_pcie_params(struct qib_devdata *dd, u32 minw, u32 *nent);
void qib_free_irq(struct qib_devdata *dd);
int qib_reinit_intr(struct qib_devdata *dd);
void qib_pcie_getcmd(struct qib_devdata *, u16 *, u8 *, u8 *);
void qib_pcie_reenable(struct qib_devdata *, u16, u8, u8);
/* interrupts for device */
u64 qib_int_counter(struct qib_devdata *);
/* interrupt for all devices */
u64 qib_sps_ints(void);

/*
 * dma_addr wrappers - all 0's invalid for hw
 */
dma_addr_t qib_map_page(struct pci_dev *, struct page *, unsigned long,
			  size_t, int);
struct pci_dev *qib_get_pci_dev(struct rvt_dev_info *rdi);

/*
 * Flush write combining store buffers (if present) and perform a write
 * barrier.
 */
static inline void qib_flush_wc(void)
{
#if defined(CONFIG_X86_64)
	asm volatile("sfence" : : : "memory");
#else
	wmb(); /* no reorder around wc flush */
#endif
}

/* global module parameter variables */
extern unsigned qib_ibmtu;
extern ushort qib_cfgctxts;
extern ushort qib_num_cfg_vls;
extern ushort qib_mini_init; /* If set, do few (ideally 0) writes to chip */
extern unsigned qib_n_krcv_queues;
extern unsigned qib_sdma_fetch_arb;
extern unsigned qib_compat_ddr_negotiate;
extern int qib_special_trigger;
extern unsigned qib_numa_aware;

extern struct mutex qib_mutex;

/* Number of seconds before our card status check...  */
#define STATUS_TIMEOUT 60

#define QIB_DRV_NAME            "ib_qib"
#define QIB_USER_MINOR_BASE     0
#define QIB_TRACE_MINOR         127
#define QIB_DIAGPKT_MINOR       128
#define QIB_DIAG_MINOR_BASE     129
#define QIB_NMINORS             255

#define PCI_VENDOR_ID_PATHSCALE 0x1fc1
#define PCI_VENDOR_ID_QLOGIC 0x1077
#define PCI_DEVICE_ID_QLOGIC_IB_6120 0x10
#define PCI_DEVICE_ID_QLOGIC_IB_7220 0x7220
#define PCI_DEVICE_ID_QLOGIC_IB_7322 0x7322

/*
 * qib_early_err is used (only!) to print early errors before devdata is
 * allocated, or when dd->pcidev may not be valid, and at the tail end of
 * cleanup when devdata may have been freed, etc.  qib_dev_porterr is
 * the same as qib_dev_err, but is used when the message really needs
 * the IB port# to be definitive as to what's happening..
 * All of these go to the trace log, and the trace log entry is done
 * first to avoid possible serial port delays from printk.
 */
#define qib_early_err(dev, fmt, ...) \
	dev_err(dev, fmt, ##__VA_ARGS__)

#define qib_dev_err(dd, fmt, ...) \
	dev_err(&(dd)->pcidev->dev, "%s: " fmt, \
		rvt_get_ibdev_name(&(dd)->verbs_dev.rdi), ##__VA_ARGS__)

#define qib_dev_warn(dd, fmt, ...) \
	dev_warn(&(dd)->pcidev->dev, "%s: " fmt, \
		 rvt_get_ibdev_name(&(dd)->verbs_dev.rdi), ##__VA_ARGS__)

#define qib_dev_porterr(dd, port, fmt, ...) \
	dev_err(&(dd)->pcidev->dev, "%s: IB%u:%u " fmt, \
		rvt_get_ibdev_name(&(dd)->verbs_dev.rdi), (dd)->unit, (port), \
		##__VA_ARGS__)

#define qib_devinfo(pcidev, fmt, ...) \
	dev_info(&(pcidev)->dev, fmt, ##__VA_ARGS__)

/*
 * this is used for formatting hw error messages...
 */
struct qib_hwerror_msgs {
	u64 mask;
	const char *msg;
	size_t sz;
};

#define QLOGIC_IB_HWE_MSG(a, b) { .mask = a, .msg = b }

/* in qib_intr.c... */
void qib_format_hwerrors(u64 hwerrs,
			 const struct qib_hwerror_msgs *hwerrmsgs,
			 size_t nhwerrmsgs, char *msg, size_t lmsg);

void qib_stop_send_queue(struct rvt_qp *qp);
void qib_quiesce_qp(struct rvt_qp *qp);
void qib_flush_qp_waiters(struct rvt_qp *qp);
int qib_mtu_to_path_mtu(u32 mtu);
u32 qib_mtu_from_qp(struct rvt_dev_info *rdi, struct rvt_qp *qp, u32 pmtu);
void qib_notify_error_qp(struct rvt_qp *qp);
int qib_get_pmtu_from_attr(struct rvt_dev_info *rdi, struct rvt_qp *qp,
			   struct ib_qp_attr *attr);

#endif                          /* _QIB_KERNEL_H */
