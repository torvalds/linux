#ifndef _IPATH_KERNEL_H
#define _IPATH_KERNEL_H
/*
 * Copyright (c) 2006, 2007, 2008 QLogic Corporation. All rights reserved.
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
 * This header file is the base header file for infinipath kernel code
 * ipath_user.h serves a similar purpose for user code.
 */

#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/scatterlist.h>
#include <asm/io.h>
#include <rdma/ib_verbs.h>

#include "ipath_common.h"
#include "ipath_debug.h"
#include "ipath_registers.h"

/* only s/w major version of InfiniPath we can handle */
#define IPATH_CHIP_VERS_MAJ 2U

/* don't care about this except printing */
#define IPATH_CHIP_VERS_MIN 0U

/* temporary, maybe always */
extern struct infinipath_stats ipath_stats;

#define IPATH_CHIP_SWVERSION IPATH_CHIP_VERS_MAJ
/*
 * First-cut critierion for "device is active" is
 * two thousand dwords combined Tx, Rx traffic per
 * 5-second interval. SMA packets are 64 dwords,
 * and occur "a few per second", presumably each way.
 */
#define IPATH_TRAFFIC_ACTIVE_THRESHOLD (2000)
/*
 * Struct used to indicate which errors are logged in each of the
 * error-counters that are logged to EEPROM. A counter is incremented
 * _once_ (saturating at 255) for each event with any bits set in
 * the error or hwerror register masks below.
 */
#define IPATH_EEP_LOG_CNT (4)
struct ipath_eep_log_mask {
	u64 errs_to_log;
	u64 hwerrs_to_log;
};

struct ipath_portdata {
	void **port_rcvegrbuf;
	dma_addr_t *port_rcvegrbuf_phys;
	/* rcvhdrq base, needs mmap before useful */
	void *port_rcvhdrq;
	/* kernel virtual address where hdrqtail is updated */
	void *port_rcvhdrtail_kvaddr;
	/*
	 * temp buffer for expected send setup, allocated at open, instead
	 * of each setup call
	 */
	void *port_tid_pg_list;
	/* when waiting for rcv or pioavail */
	wait_queue_head_t port_wait;
	/*
	 * rcvegr bufs base, physical, must fit
	 * in 44 bits so 32 bit programs mmap64 44 bit works)
	 */
	dma_addr_t port_rcvegr_phys;
	/* mmap of hdrq, must fit in 44 bits */
	dma_addr_t port_rcvhdrq_phys;
	dma_addr_t port_rcvhdrqtailaddr_phys;
	/*
	 * number of opens (including slave subports) on this instance
	 * (ignoring forks, dup, etc. for now)
	 */
	int port_cnt;
	/*
	 * how much space to leave at start of eager TID entries for
	 * protocol use, on each TID
	 */
	/* instead of calculating it */
	unsigned port_port;
	/* non-zero if port is being shared. */
	u16 port_subport_cnt;
	/* non-zero if port is being shared. */
	u16 port_subport_id;
	/* number of pio bufs for this port (all procs, if shared) */
	u32 port_piocnt;
	/* first pio buffer for this port */
	u32 port_pio_base;
	/* chip offset of PIO buffers for this port */
	u32 port_piobufs;
	/* how many alloc_pages() chunks in port_rcvegrbuf_pages */
	u32 port_rcvegrbuf_chunks;
	/* how many egrbufs per chunk */
	u32 port_rcvegrbufs_perchunk;
	/* order for port_rcvegrbuf_pages */
	size_t port_rcvegrbuf_size;
	/* rcvhdrq size (for freeing) */
	size_t port_rcvhdrq_size;
	/* next expected TID to check when looking for free */
	u32 port_tidcursor;
	/* next expected TID to check */
	unsigned long port_flag;
	/* what happened */
	unsigned long int_flag;
	/* WAIT_RCV that timed out, no interrupt */
	u32 port_rcvwait_to;
	/* WAIT_PIO that timed out, no interrupt */
	u32 port_piowait_to;
	/* WAIT_RCV already happened, no wait */
	u32 port_rcvnowait;
	/* WAIT_PIO already happened, no wait */
	u32 port_pionowait;
	/* total number of rcvhdrqfull errors */
	u32 port_hdrqfull;
	/*
	 * Used to suppress multiple instances of same
	 * port staying stuck at same point.
	 */
	u32 port_lastrcvhdrqtail;
	/* saved total number of rcvhdrqfull errors for poll edge trigger */
	u32 port_hdrqfull_poll;
	/* total number of polled urgent packets */
	u32 port_urgent;
	/* saved total number of polled urgent packets for poll edge trigger */
	u32 port_urgent_poll;
	/* pid of process using this port */
	struct pid *port_pid;
	struct pid *port_subpid[INFINIPATH_MAX_SUBPORT];
	/* same size as task_struct .comm[] */
	char port_comm[16];
	/* pkeys set by this use of this port */
	u16 port_pkeys[4];
	/* so file ops can get at unit */
	struct ipath_devdata *port_dd;
	/* A page of memory for rcvhdrhead, rcvegrhead, rcvegrtail * N */
	void *subport_uregbase;
	/* An array of pages for the eager receive buffers * N */
	void *subport_rcvegrbuf;
	/* An array of pages for the eager header queue entries * N */
	void *subport_rcvhdr_base;
	/* The version of the library which opened this port */
	u32 userversion;
	/* Bitmask of active slaves */
	u32 active_slaves;
	/* Type of packets or conditions we want to poll for */
	u16 poll_type;
	/* port rcvhdrq head offset */
	u32 port_head;
	/* receive packet sequence counter */
	u32 port_seq_cnt;
};

struct sk_buff;
struct ipath_sge_state;
struct ipath_verbs_txreq;

/*
 * control information for layered drivers
 */
struct _ipath_layer {
	void *l_arg;
};

struct ipath_skbinfo {
	struct sk_buff *skb;
	dma_addr_t phys;
};

struct ipath_sdma_txreq {
	int                 flags;
	int                 sg_count;
	union {
		struct scatterlist *sg;
		void *map_addr;
	};
	void              (*callback)(void *, int);
	void               *callback_cookie;
	int                 callback_status;
	u16                 start_idx;  /* sdma private */
	u16                 next_descq_idx;  /* sdma private */
	struct list_head    list;       /* sdma private */
};

struct ipath_sdma_desc {
	__le64 qw[2];
};

#define IPATH_SDMA_TXREQ_F_USELARGEBUF  0x1
#define IPATH_SDMA_TXREQ_F_HEADTOHOST   0x2
#define IPATH_SDMA_TXREQ_F_INTREQ       0x4
#define IPATH_SDMA_TXREQ_F_FREEBUF      0x8
#define IPATH_SDMA_TXREQ_F_FREEDESC     0x10
#define IPATH_SDMA_TXREQ_F_VL15         0x20

#define IPATH_SDMA_TXREQ_S_OK        0
#define IPATH_SDMA_TXREQ_S_SENDERROR 1
#define IPATH_SDMA_TXREQ_S_ABORTED   2
#define IPATH_SDMA_TXREQ_S_SHUTDOWN  3

#define IPATH_SDMA_STATUS_SCORE_BOARD_DRAIN_IN_PROG	(1ull << 63)
#define IPATH_SDMA_STATUS_ABORT_IN_PROG			(1ull << 62)
#define IPATH_SDMA_STATUS_INTERNAL_SDMA_ENABLE		(1ull << 61)
#define IPATH_SDMA_STATUS_SCB_EMPTY			(1ull << 30)

/* max dwords in small buffer packet */
#define IPATH_SMALLBUF_DWORDS (dd->ipath_piosize2k >> 2)

/*
 * Possible IB config parameters for ipath_f_get/set_ib_cfg()
 */
#define IPATH_IB_CFG_LIDLMC 0 /* Get/set LID (LS16b) and Mask (MS16b) */
#define IPATH_IB_CFG_HRTBT 1 /* Get/set Heartbeat off/enable/auto */
#define IPATH_IB_HRTBT_ON 3 /* Heartbeat enabled, sent every 100msec */
#define IPATH_IB_HRTBT_OFF 0 /* Heartbeat off */
#define IPATH_IB_CFG_LWID_ENB 2 /* Get/set allowed Link-width */
#define IPATH_IB_CFG_LWID 3 /* Get currently active Link-width */
#define IPATH_IB_CFG_SPD_ENB 4 /* Get/set allowed Link speeds */
#define IPATH_IB_CFG_SPD 5 /* Get current Link spd */
#define IPATH_IB_CFG_RXPOL_ENB 6 /* Get/set Auto-RX-polarity enable */
#define IPATH_IB_CFG_LREV_ENB 7 /* Get/set Auto-Lane-reversal enable */
#define IPATH_IB_CFG_LINKLATENCY 8 /* Get Auto-Lane-reversal enable */


struct ipath_devdata {
	struct list_head ipath_list;

	struct ipath_kregs const *ipath_kregs;
	struct ipath_cregs const *ipath_cregs;

	/* mem-mapped pointer to base of chip regs */
	u64 __iomem *ipath_kregbase;
	/* end of mem-mapped chip space; range checking */
	u64 __iomem *ipath_kregend;
	/* physical address of chip for io_remap, etc. */
	unsigned long ipath_physaddr;
	/* base of memory alloced for ipath_kregbase, for free */
	u64 *ipath_kregalloc;
	/* ipath_cfgports pointers */
	struct ipath_portdata **ipath_pd;
	/* sk_buffs used by port 0 eager receive queue */
	struct ipath_skbinfo *ipath_port0_skbinfo;
	/* kvirt address of 1st 2k pio buffer */
	void __iomem *ipath_pio2kbase;
	/* kvirt address of 1st 4k pio buffer */
	void __iomem *ipath_pio4kbase;
	/*
	 * points to area where PIOavail registers will be DMA'ed.
	 * Has to be on a page of it's own, because the page will be
	 * mapped into user program space.  This copy is *ONLY* ever
	 * written by DMA, not by the driver!  Need a copy per device
	 * when we get to multiple devices
	 */
	volatile __le64 *ipath_pioavailregs_dma;
	/* physical address where updates occur */
	dma_addr_t ipath_pioavailregs_phys;
	struct _ipath_layer ipath_layer;
	/* setup intr */
	int (*ipath_f_intrsetup)(struct ipath_devdata *);
	/* fallback to alternate interrupt type if possible */
	int (*ipath_f_intr_fallback)(struct ipath_devdata *);
	/* setup on-chip bus config */
	int (*ipath_f_bus)(struct ipath_devdata *, struct pci_dev *);
	/* hard reset chip */
	int (*ipath_f_reset)(struct ipath_devdata *);
	int (*ipath_f_get_boardname)(struct ipath_devdata *, char *,
				     size_t);
	void (*ipath_f_init_hwerrors)(struct ipath_devdata *);
	void (*ipath_f_handle_hwerrors)(struct ipath_devdata *, char *,
					size_t);
	void (*ipath_f_quiet_serdes)(struct ipath_devdata *);
	int (*ipath_f_bringup_serdes)(struct ipath_devdata *);
	int (*ipath_f_early_init)(struct ipath_devdata *);
	void (*ipath_f_clear_tids)(struct ipath_devdata *, unsigned);
	void (*ipath_f_put_tid)(struct ipath_devdata *, u64 __iomem*,
				u32, unsigned long);
	void (*ipath_f_tidtemplate)(struct ipath_devdata *);
	void (*ipath_f_cleanup)(struct ipath_devdata *);
	void (*ipath_f_setextled)(struct ipath_devdata *, u64, u64);
	/* fill out chip-specific fields */
	int (*ipath_f_get_base_info)(struct ipath_portdata *, void *);
	/* free irq */
	void (*ipath_f_free_irq)(struct ipath_devdata *);
	struct ipath_message_header *(*ipath_f_get_msgheader)
					(struct ipath_devdata *, __le32 *);
	void (*ipath_f_config_ports)(struct ipath_devdata *, ushort);
	int (*ipath_f_get_ib_cfg)(struct ipath_devdata *, int);
	int (*ipath_f_set_ib_cfg)(struct ipath_devdata *, int, u32);
	void (*ipath_f_config_jint)(struct ipath_devdata *, u16 , u16);
	void (*ipath_f_read_counters)(struct ipath_devdata *,
					struct infinipath_counters *);
	void (*ipath_f_xgxs_reset)(struct ipath_devdata *);
	/* per chip actions needed for IB Link up/down changes */
	int (*ipath_f_ib_updown)(struct ipath_devdata *, int, u64);

	unsigned ipath_lastegr_idx;
	struct ipath_ibdev *verbs_dev;
	struct timer_list verbs_timer;
	/* total dwords sent (summed from counter) */
	u64 ipath_sword;
	/* total dwords rcvd (summed from counter) */
	u64 ipath_rword;
	/* total packets sent (summed from counter) */
	u64 ipath_spkts;
	/* total packets rcvd (summed from counter) */
	u64 ipath_rpkts;
	/* ipath_statusp initially points to this. */
	u64 _ipath_status;
	/* GUID for this interface, in network order */
	__be64 ipath_guid;
	/*
	 * aggregrate of error bits reported since last cleared, for
	 * limiting of error reporting
	 */
	ipath_err_t ipath_lasterror;
	/*
	 * aggregrate of error bits reported since last cleared, for
	 * limiting of hwerror reporting
	 */
	ipath_err_t ipath_lasthwerror;
	/* errors masked because they occur too fast */
	ipath_err_t ipath_maskederrs;
	u64 ipath_lastlinkrecov; /* link recoveries at last ACTIVE */
	/* these 5 fields are used to establish deltas for IB Symbol
	 * errors and linkrecovery errors. They can be reported on
	 * some chips during link negotiation prior to INIT, and with
	 * DDR when faking DDR negotiations with non-IBTA switches.
	 * The chip counters are adjusted at driver unload if there is
	 * a non-zero delta.
	 */
	u64 ibdeltainprog;
	u64 ibsymdelta;
	u64 ibsymsnap;
	u64 iblnkerrdelta;
	u64 iblnkerrsnap;

	/* time in jiffies at which to re-enable maskederrs */
	unsigned long ipath_unmasktime;
	/* count of egrfull errors, combined for all ports */
	u64 ipath_last_tidfull;
	/* for ipath_qcheck() */
	u64 ipath_lastport0rcv_cnt;
	/* template for writing TIDs  */
	u64 ipath_tidtemplate;
	/* value to write to free TIDs */
	u64 ipath_tidinvalid;
	/* IBA6120 rcv interrupt setup */
	u64 ipath_rhdrhead_intr_off;

	/* size of memory at ipath_kregbase */
	u32 ipath_kregsize;
	/* number of registers used for pioavail */
	u32 ipath_pioavregs;
	/* IPATH_POLL, etc. */
	u32 ipath_flags;
	/* ipath_flags driver is waiting for */
	u32 ipath_state_wanted;
	/* last buffer for user use, first buf for kernel use is this
	 * index. */
	u32 ipath_lastport_piobuf;
	/* is a stats timer active */
	u32 ipath_stats_timer_active;
	/* number of interrupts for this device -- saturates... */
	u32 ipath_int_counter;
	/* dwords sent read from counter */
	u32 ipath_lastsword;
	/* dwords received read from counter */
	u32 ipath_lastrword;
	/* sent packets read from counter */
	u32 ipath_lastspkts;
	/* received packets read from counter */
	u32 ipath_lastrpkts;
	/* pio bufs allocated per port */
	u32 ipath_pbufsport;
	/* if remainder on bufs/port, ports < extrabuf get 1 extra */
	u32 ipath_ports_extrabuf;
	u32 ipath_pioupd_thresh; /* update threshold, some chips */
	/*
	 * number of ports configured as max; zero is set to number chip
	 * supports, less gives more pio bufs/port, etc.
	 */
	u32 ipath_cfgports;
	/* count of port 0 hdrqfull errors */
	u32 ipath_p0_hdrqfull;
	/* port 0 number of receive eager buffers */
	u32 ipath_p0_rcvegrcnt;

	/*
	 * index of last piobuffer we used.  Speeds up searching, by
	 * starting at this point.  Doesn't matter if multiple cpu's use and
	 * update, last updater is only write that matters.  Whenever it
	 * wraps, we update shadow copies.  Need a copy per device when we
	 * get to multiple devices
	 */
	u32 ipath_lastpioindex;
	u32 ipath_lastpioindexl;
	/* max length of freezemsg */
	u32 ipath_freezelen;
	/*
	 * consecutive times we wanted a PIO buffer but were unable to
	 * get one
	 */
	u32 ipath_consec_nopiobuf;
	/*
	 * hint that we should update ipath_pioavailshadow before
	 * looking for a PIO buffer
	 */
	u32 ipath_upd_pio_shadow;
	/* so we can rewrite it after a chip reset */
	u32 ipath_pcibar0;
	/* so we can rewrite it after a chip reset */
	u32 ipath_pcibar1;
	u32 ipath_x1_fix_tries;
	u32 ipath_autoneg_tries;
	u32 serdes_first_init_done;

	struct ipath_relock {
		atomic_t ipath_relock_timer_active;
		struct timer_list ipath_relock_timer;
		unsigned int ipath_relock_interval; /* in jiffies */
	} ipath_relock_singleton;

	/* interrupt number */
	int ipath_irq;
	/* HT/PCI Vendor ID (here for NodeInfo) */
	u16 ipath_vendorid;
	/* HT/PCI Device ID (here for NodeInfo) */
	u16 ipath_deviceid;
	/* offset in HT config space of slave/primary interface block */
	u8 ipath_ht_slave_off;
	/* for write combining settings */
	unsigned long ipath_wc_cookie;
	unsigned long ipath_wc_base;
	unsigned long ipath_wc_len;
	/* ref count for each pkey */
	atomic_t ipath_pkeyrefs[4];
	/* shadow copy of struct page *'s for exp tid pages */
	struct page **ipath_pageshadow;
	/* shadow copy of dma handles for exp tid pages */
	dma_addr_t *ipath_physshadow;
	u64 __iomem *ipath_egrtidbase;
	/* lock to workaround chip bug 9437 and others */
	spinlock_t ipath_kernel_tid_lock;
	spinlock_t ipath_user_tid_lock;
	spinlock_t ipath_sendctrl_lock;
	/* around ipath_pd and (user ports) port_cnt use (intr vs free) */
	spinlock_t ipath_uctxt_lock;

	/*
	 * IPATH_STATUS_*,
	 * this address is mapped readonly into user processes so they can
	 * get status cheaply, whenever they want.
	 */
	u64 *ipath_statusp;
	/* freeze msg if hw error put chip in freeze */
	char *ipath_freezemsg;
	/* pci access data structure */
	struct pci_dev *pcidev;
	struct cdev *user_cdev;
	struct cdev *diag_cdev;
	struct device *user_dev;
	struct device *diag_dev;
	/* timer used to prevent stats overflow, error throttling, etc. */
	struct timer_list ipath_stats_timer;
	/* timer to verify interrupts work, and fallback if possible */
	struct timer_list ipath_intrchk_timer;
	void *ipath_dummy_hdrq;	/* used after port close */
	dma_addr_t ipath_dummy_hdrq_phys;

	/* SendDMA related entries */
	spinlock_t            ipath_sdma_lock;
	unsigned long         ipath_sdma_status;
	unsigned long         ipath_sdma_abort_jiffies;
	unsigned long         ipath_sdma_abort_intr_timeout;
	unsigned long         ipath_sdma_buf_jiffies;
	struct ipath_sdma_desc *ipath_sdma_descq;
	u64		      ipath_sdma_descq_added;
	u64		      ipath_sdma_descq_removed;
	int		      ipath_sdma_desc_nreserved;
	u16                   ipath_sdma_descq_cnt;
	u16                   ipath_sdma_descq_tail;
	u16                   ipath_sdma_descq_head;
	u16                   ipath_sdma_next_intr;
	u16                   ipath_sdma_reset_wait;
	u8                    ipath_sdma_generation;
	struct tasklet_struct ipath_sdma_abort_task;
	struct tasklet_struct ipath_sdma_notify_task;
	struct list_head      ipath_sdma_activelist;
	struct list_head      ipath_sdma_notifylist;
	atomic_t              ipath_sdma_vl15_count;
	struct timer_list     ipath_sdma_vl15_timer;

	dma_addr_t       ipath_sdma_descq_phys;
	volatile __le64 *ipath_sdma_head_dma;
	dma_addr_t       ipath_sdma_head_phys;

	unsigned long ipath_ureg_align; /* user register alignment */

	struct delayed_work ipath_autoneg_work;
	wait_queue_head_t ipath_autoneg_wait;

	/* HoL blocking / user app forward-progress state */
	unsigned          ipath_hol_state;
	unsigned          ipath_hol_next;
	struct timer_list ipath_hol_timer;

	/*
	 * Shadow copies of registers; size indicates read access size.
	 * Most of them are readonly, but some are write-only register,
	 * where we manipulate the bits in the shadow copy, and then write
	 * the shadow copy to infinipath.
	 *
	 * We deliberately make most of these 32 bits, since they have
	 * restricted range.  For any that we read, we won't to generate 32
	 * bit accesses, since Opteron will generate 2 separate 32 bit HT
	 * transactions for a 64 bit read, and we want to avoid unnecessary
	 * HT transactions.
	 */

	/* This is the 64 bit group */

	/*
	 * shadow of pioavail, check to be sure it's large enough at
	 * init time.
	 */
	unsigned long ipath_pioavailshadow[8];
	/* bitmap of send buffers available for the kernel to use with PIO. */
	unsigned long ipath_pioavailkernel[8];
	/* shadow of kr_gpio_out, for rmw ops */
	u64 ipath_gpio_out;
	/* shadow the gpio mask register */
	u64 ipath_gpio_mask;
	/* shadow the gpio output enable, etc... */
	u64 ipath_extctrl;
	/* kr_revision shadow */
	u64 ipath_revision;
	/*
	 * shadow of ibcctrl, for interrupt handling of link changes,
	 * etc.
	 */
	u64 ipath_ibcctrl;
	/*
	 * last ibcstatus, to suppress "duplicate" status change messages,
	 * mostly from 2 to 3
	 */
	u64 ipath_lastibcstat;
	/* hwerrmask shadow */
	ipath_err_t ipath_hwerrmask;
	ipath_err_t ipath_errormask; /* errormask shadow */
	/* interrupt config reg shadow */
	u64 ipath_intconfig;
	/* kr_sendpiobufbase value */
	u64 ipath_piobufbase;
	/* kr_ibcddrctrl shadow */
	u64 ipath_ibcddrctrl;

	/* these are the "32 bit" regs */

	/*
	 * number of GUIDs in the flash for this interface; may need some
	 * rethinking for setting on other ifaces
	 */
	u32 ipath_nguid;
	/*
	 * the following two are 32-bit bitmasks, but {test,clear,set}_bit
	 * all expect bit fields to be "unsigned long"
	 */
	/* shadow kr_rcvctrl */
	unsigned long ipath_rcvctrl;
	/* shadow kr_sendctrl */
	unsigned long ipath_sendctrl;
	/* to not count armlaunch after cancel */
	unsigned long ipath_lastcancel;
	/* count cases where special trigger was needed (double write) */
	unsigned long ipath_spectriggerhit;

	/* value we put in kr_rcvhdrcnt */
	u32 ipath_rcvhdrcnt;
	/* value we put in kr_rcvhdrsize */
	u32 ipath_rcvhdrsize;
	/* value we put in kr_rcvhdrentsize */
	u32 ipath_rcvhdrentsize;
	/* offset of last entry in rcvhdrq */
	u32 ipath_hdrqlast;
	/* kr_portcnt value */
	u32 ipath_portcnt;
	/* kr_pagealign value */
	u32 ipath_palign;
	/* number of "2KB" PIO buffers */
	u32 ipath_piobcnt2k;
	/* size in bytes of "2KB" PIO buffers */
	u32 ipath_piosize2k;
	/* number of "4KB" PIO buffers */
	u32 ipath_piobcnt4k;
	/* size in bytes of "4KB" PIO buffers */
	u32 ipath_piosize4k;
	u32 ipath_pioreserved; /* reserved special-inkernel; */
	/* kr_rcvegrbase value */
	u32 ipath_rcvegrbase;
	/* kr_rcvegrcnt value */
	u32 ipath_rcvegrcnt;
	/* kr_rcvtidbase value */
	u32 ipath_rcvtidbase;
	/* kr_rcvtidcnt value */
	u32 ipath_rcvtidcnt;
	/* kr_sendregbase */
	u32 ipath_sregbase;
	/* kr_userregbase */
	u32 ipath_uregbase;
	/* kr_counterregbase */
	u32 ipath_cregbase;
	/* shadow the control register contents */
	u32 ipath_control;
	/* PCI revision register (HTC rev on FPGA) */
	u32 ipath_pcirev;

	/* chip address space used by 4k pio buffers */
	u32 ipath_4kalign;
	/* The MTU programmed for this unit */
	u32 ipath_ibmtu;
	/*
	 * The max size IB packet, included IB headers that we can send.
	 * Starts same as ipath_piosize, but is affected when ibmtu is
	 * changed, or by size of eager buffers
	 */
	u32 ipath_ibmaxlen;
	/*
	 * ibmaxlen at init time, limited by chip and by receive buffer
	 * size.  Not changed after init.
	 */
	u32 ipath_init_ibmaxlen;
	/* size of each rcvegrbuffer */
	u32 ipath_rcvegrbufsize;
	/* localbus width (1, 2,4,8,16,32) from config space  */
	u32 ipath_lbus_width;
	/* localbus speed (HT: 200,400,800,1000; PCIe 2500) */
	u32 ipath_lbus_speed;
	/*
	 * number of sequential ibcstatus change for polling active/quiet
	 * (i.e., link not coming up).
	 */
	u32 ipath_ibpollcnt;
	/* low and high portions of MSI capability/vector */
	u32 ipath_msi_lo;
	/* saved after PCIe init for restore after reset */
	u32 ipath_msi_hi;
	/* MSI data (vector) saved for restore */
	u16 ipath_msi_data;
	/* MLID programmed for this instance */
	u16 ipath_mlid;
	/* LID programmed for this instance */
	u16 ipath_lid;
	/* list of pkeys programmed; 0 if not set */
	u16 ipath_pkeys[4];
	/*
	 * ASCII serial number, from flash, large enough for original
	 * all digit strings, and longer QLogic serial number format
	 */
	u8 ipath_serial[16];
	/* human readable board version */
	u8 ipath_boardversion[96];
	u8 ipath_lbus_info[32]; /* human readable localbus info */
	/* chip major rev, from ipath_revision */
	u8 ipath_majrev;
	/* chip minor rev, from ipath_revision */
	u8 ipath_minrev;
	/* board rev, from ipath_revision */
	u8 ipath_boardrev;
	/* saved for restore after reset */
	u8 ipath_pci_cacheline;
	/* LID mask control */
	u8 ipath_lmc;
	/* link width supported */
	u8 ipath_link_width_supported;
	/* link speed supported */
	u8 ipath_link_speed_supported;
	u8 ipath_link_width_enabled;
	u8 ipath_link_speed_enabled;
	u8 ipath_link_width_active;
	u8 ipath_link_speed_active;
	/* Rx Polarity inversion (compensate for ~tx on partner) */
	u8 ipath_rx_pol_inv;

	u8 ipath_r_portenable_shift;
	u8 ipath_r_intravail_shift;
	u8 ipath_r_tailupd_shift;
	u8 ipath_r_portcfg_shift;

	/* unit # of this chip, if present */
	int ipath_unit;

	/* local link integrity counter */
	u32 ipath_lli_counter;
	/* local link integrity errors */
	u32 ipath_lli_errors;
	/*
	 * Above counts only cases where _successive_ LocalLinkIntegrity
	 * errors were seen in the receive headers of kern-packets.
	 * Below are the three (monotonically increasing) counters
	 * maintained via GPIO interrupts on iba6120-rev2.
	 */
	u32 ipath_rxfc_unsupvl_errs;
	u32 ipath_overrun_thresh_errs;
	u32 ipath_lli_errs;

	/*
	 * Not all devices managed by a driver instance are the same
	 * type, so these fields must be per-device.
	 */
	u64 ipath_i_bitsextant;
	ipath_err_t ipath_e_bitsextant;
	ipath_err_t ipath_hwe_bitsextant;

	/*
	 * Below should be computable from number of ports,
	 * since they are never modified.
	 */
	u64 ipath_i_rcvavail_mask;
	u64 ipath_i_rcvurg_mask;
	u16 ipath_i_rcvurg_shift;
	u16 ipath_i_rcvavail_shift;

	/*
	 * Register bits for selecting i2c direction and values, used for
	 * I2C serial flash.
	 */
	u8 ipath_gpio_sda_num;
	u8 ipath_gpio_scl_num;
	u8 ipath_i2c_chain_type;
	u64 ipath_gpio_sda;
	u64 ipath_gpio_scl;

	/* lock for doing RMW of shadows/regs for ExtCtrl and GPIO */
	spinlock_t ipath_gpio_lock;

	/*
	 * IB link and linktraining states and masks that vary per chip in
	 * some way.  Set at init, to avoid each IB status change interrupt
	 */
	u8 ibcs_ls_shift;
	u8 ibcs_lts_mask;
	u32 ibcs_mask;
	u32 ib_init;
	u32 ib_arm;
	u32 ib_active;

	u16 ipath_rhf_offset; /* offset of RHF within receive header entry */

	/*
	 * shift/mask for linkcmd, linkinitcmd, maxpktlen in ibccontol
	 * reg. Changes for IBA7220
	 */
	u8 ibcc_lic_mask; /* LinkInitCmd */
	u8 ibcc_lc_shift; /* LinkCmd */
	u8 ibcc_mpl_shift; /* Maxpktlen */

	u8 delay_mult;

	/* used to override LED behavior */
	u8 ipath_led_override;  /* Substituted for normal value, if non-zero */
	u16 ipath_led_override_timeoff; /* delta to next timer event */
	u8 ipath_led_override_vals[2]; /* Alternates per blink-frame */
	u8 ipath_led_override_phase; /* Just counts, LSB picks from vals[] */
	atomic_t ipath_led_override_timer_active;
	/* Used to flash LEDs in override mode */
	struct timer_list ipath_led_override_timer;

	/* Support (including locks) for EEPROM logging of errors and time */
	/* control access to actual counters, timer */
	spinlock_t ipath_eep_st_lock;
	/* control high-level access to EEPROM */
	struct mutex ipath_eep_lock;
	/* Below inc'd by ipath_snap_cntrs(), locked by ipath_eep_st_lock */
	uint64_t ipath_traffic_wds;
	/* active time is kept in seconds, but logged in hours */
	atomic_t ipath_active_time;
	/* Below are nominal shadow of EEPROM, new since last EEPROM update */
	uint8_t ipath_eep_st_errs[IPATH_EEP_LOG_CNT];
	uint8_t ipath_eep_st_new_errs[IPATH_EEP_LOG_CNT];
	uint16_t ipath_eep_hrs;
	/*
	 * masks for which bits of errs, hwerrs that cause
	 * each of the counters to increment.
	 */
	struct ipath_eep_log_mask ipath_eep_st_masks[IPATH_EEP_LOG_CNT];

	/* interrupt mitigation reload register info */
	u16 ipath_jint_idle_ticks;	/* idle clock ticks */
	u16 ipath_jint_max_packets;	/* max packets across all ports */

	/*
	 * lock for access to SerDes, and flags to sequence preset
	 * versus steady-state. 7220-only at the moment.
	 */
	spinlock_t ipath_sdepb_lock;
	u8 ipath_presets_needed; /* Set if presets to be restored next DOWN */
};

/* ipath_hol_state values (stopping/starting user proc, send flushing) */
#define IPATH_HOL_UP       0
#define IPATH_HOL_DOWN     1
/* ipath_hol_next toggle values, used when hol_state IPATH_HOL_DOWN */
#define IPATH_HOL_DOWNSTOP 0
#define IPATH_HOL_DOWNCONT 1

/* bit positions for sdma_status */
#define IPATH_SDMA_ABORTING  0
#define IPATH_SDMA_DISARMED  1
#define IPATH_SDMA_DISABLED  2
#define IPATH_SDMA_LAYERBUF  3
#define IPATH_SDMA_RUNNING  30
#define IPATH_SDMA_SHUTDOWN 31

/* bit combinations that correspond to abort states */
#define IPATH_SDMA_ABORT_NONE 0
#define IPATH_SDMA_ABORT_ABORTING (1UL << IPATH_SDMA_ABORTING)
#define IPATH_SDMA_ABORT_DISARMED ((1UL << IPATH_SDMA_ABORTING) | \
	(1UL << IPATH_SDMA_DISARMED))
#define IPATH_SDMA_ABORT_DISABLED ((1UL << IPATH_SDMA_ABORTING) | \
	(1UL << IPATH_SDMA_DISABLED))
#define IPATH_SDMA_ABORT_ABORTED ((1UL << IPATH_SDMA_ABORTING) | \
	(1UL << IPATH_SDMA_DISARMED) | (1UL << IPATH_SDMA_DISABLED))
#define IPATH_SDMA_ABORT_MASK ((1UL<<IPATH_SDMA_ABORTING) | \
	(1UL << IPATH_SDMA_DISARMED) | (1UL << IPATH_SDMA_DISABLED))

#define IPATH_SDMA_BUF_NONE 0
#define IPATH_SDMA_BUF_MASK (1UL<<IPATH_SDMA_LAYERBUF)

/* Private data for file operations */
struct ipath_filedata {
	struct ipath_portdata *pd;
	unsigned subport;
	unsigned tidcursor;
	struct ipath_user_sdma_queue *pq;
};
extern struct list_head ipath_dev_list;
extern spinlock_t ipath_devs_lock;
extern struct ipath_devdata *ipath_lookup(int unit);

int ipath_init_chip(struct ipath_devdata *, int);
int ipath_enable_wc(struct ipath_devdata *dd);
void ipath_disable_wc(struct ipath_devdata *dd);
int ipath_count_units(int *npresentp, int *nupp, int *maxportsp);
void ipath_shutdown_device(struct ipath_devdata *);
void ipath_clear_freeze(struct ipath_devdata *);

struct file_operations;
int ipath_cdev_init(int minor, char *name, const struct file_operations *fops,
		    struct cdev **cdevp, struct device **devp);
void ipath_cdev_cleanup(struct cdev **cdevp,
			struct device **devp);

int ipath_diag_add(struct ipath_devdata *);
void ipath_diag_remove(struct ipath_devdata *);

extern wait_queue_head_t ipath_state_wait;

int ipath_user_add(struct ipath_devdata *dd);
void ipath_user_remove(struct ipath_devdata *dd);

struct sk_buff *ipath_alloc_skb(struct ipath_devdata *dd, gfp_t);

extern int ipath_diag_inuse;

irqreturn_t ipath_intr(int irq, void *devid);
int ipath_decode_err(struct ipath_devdata *dd, char *buf, size_t blen,
		     ipath_err_t err);
#if __IPATH_INFO || __IPATH_DBG
extern const char *ipath_ibcstatus_str[];
#endif

/* clean up any per-chip chip-specific stuff */
void ipath_chip_cleanup(struct ipath_devdata *);
/* clean up any chip type-specific stuff */
void ipath_chip_done(void);

/* check to see if we have to force ordering for write combining */
int ipath_unordered_wc(void);

void ipath_disarm_piobufs(struct ipath_devdata *, unsigned first,
			  unsigned cnt);
void ipath_cancel_sends(struct ipath_devdata *, int);

int ipath_create_rcvhdrq(struct ipath_devdata *, struct ipath_portdata *);
void ipath_free_pddata(struct ipath_devdata *, struct ipath_portdata *);

int ipath_parse_ushort(const char *str, unsigned short *valp);

void ipath_kreceive(struct ipath_portdata *);
int ipath_setrcvhdrsize(struct ipath_devdata *, unsigned);
int ipath_reset_device(int);
void ipath_get_faststats(unsigned long);
int ipath_wait_linkstate(struct ipath_devdata *, u32, int);
int ipath_set_linkstate(struct ipath_devdata *, u8);
int ipath_set_mtu(struct ipath_devdata *, u16);
int ipath_set_lid(struct ipath_devdata *, u32, u8);
int ipath_set_rx_pol_inv(struct ipath_devdata *dd, u8 new_pol_inv);
void ipath_enable_armlaunch(struct ipath_devdata *);
void ipath_disable_armlaunch(struct ipath_devdata *);
void ipath_hol_down(struct ipath_devdata *);
void ipath_hol_up(struct ipath_devdata *);
void ipath_hol_event(unsigned long);
void ipath_toggle_rclkrls(struct ipath_devdata *);
void ipath_sd7220_clr_ibpar(struct ipath_devdata *);
void ipath_set_relock_poll(struct ipath_devdata *, int);
void ipath_shutdown_relock_poll(struct ipath_devdata *);

/* for use in system calls, where we want to know device type, etc. */
#define port_fp(fp) ((struct ipath_filedata *)(fp)->private_data)->pd
#define subport_fp(fp) \
	((struct ipath_filedata *)(fp)->private_data)->subport
#define tidcursor_fp(fp) \
	((struct ipath_filedata *)(fp)->private_data)->tidcursor
#define user_sdma_queue_fp(fp) \
	((struct ipath_filedata *)(fp)->private_data)->pq

/*
 * values for ipath_flags
 */
		/* chip can report link latency (IB 1.2) */
#define IPATH_HAS_LINK_LATENCY 0x1
		/* The chip is up and initted */
#define IPATH_INITTED       0x2
		/* set if any user code has set kr_rcvhdrsize */
#define IPATH_RCVHDRSZ_SET  0x4
		/* The chip is present and valid for accesses */
#define IPATH_PRESENT       0x8
		/* HT link0 is only 8 bits wide, ignore upper byte crc
		 * errors, etc. */
#define IPATH_8BIT_IN_HT0   0x10
		/* HT link1 is only 8 bits wide, ignore upper byte crc
		 * errors, etc. */
#define IPATH_8BIT_IN_HT1   0x20
		/* The link is down */
#define IPATH_LINKDOWN      0x40
		/* The link level is up (0x11) */
#define IPATH_LINKINIT      0x80
		/* The link is in the armed (0x21) state */
#define IPATH_LINKARMED     0x100
		/* The link is in the active (0x31) state */
#define IPATH_LINKACTIVE    0x200
		/* link current state is unknown */
#define IPATH_LINKUNK       0x400
		/* Write combining flush needed for PIO */
#define IPATH_PIO_FLUSH_WC  0x1000
		/* DMA Receive tail pointer */
#define IPATH_NODMA_RTAIL   0x2000
		/* no IB cable, or no device on IB cable */
#define IPATH_NOCABLE       0x4000
		/* Supports port zero per packet receive interrupts via
		 * GPIO */
#define IPATH_GPIO_INTR     0x8000
		/* uses the coded 4byte TID, not 8 byte */
#define IPATH_4BYTE_TID     0x10000
		/* packet/word counters are 32 bit, else those 4 counters
		 * are 64bit */
#define IPATH_32BITCOUNTERS 0x20000
		/* Interrupt register is 64 bits */
#define IPATH_INTREG_64     0x40000
		/* can miss port0 rx interrupts */
#define IPATH_DISABLED      0x80000 /* administratively disabled */
		/* Use GPIO interrupts for new counters */
#define IPATH_GPIO_ERRINTRS 0x100000
#define IPATH_SWAP_PIOBUFS  0x200000
		/* Supports Send DMA */
#define IPATH_HAS_SEND_DMA  0x400000
		/* Supports Send Count (not just word count) in PBC */
#define IPATH_HAS_PBC_CNT   0x800000
		/* Suppress heartbeat, even if turning off loopback */
#define IPATH_NO_HRTBT      0x1000000
#define IPATH_HAS_THRESH_UPDATE 0x4000000
#define IPATH_HAS_MULT_IB_SPEED 0x8000000
#define IPATH_IB_AUTONEG_INPROG 0x10000000
#define IPATH_IB_AUTONEG_FAILED 0x20000000
		/* Linkdown-disable intentionally, Do not attempt to bring up */
#define IPATH_IB_LINK_DISABLED 0x40000000
#define IPATH_IB_FORCE_NOTIFY 0x80000000 /* force notify on next ib change */

/* Bits in GPIO for the added interrupts */
#define IPATH_GPIO_PORT0_BIT 2
#define IPATH_GPIO_RXUVL_BIT 3
#define IPATH_GPIO_OVRUN_BIT 4
#define IPATH_GPIO_LLI_BIT 5
#define IPATH_GPIO_ERRINTR_MASK 0x38

/* portdata flag bit offsets */
		/* waiting for a packet to arrive */
#define IPATH_PORT_WAITING_RCV   2
		/* master has not finished initializing */
#define IPATH_PORT_MASTER_UNINIT 4
		/* waiting for an urgent packet to arrive */
#define IPATH_PORT_WAITING_URG 5

/* free up any allocated data at closes */
void ipath_free_data(struct ipath_portdata *dd);
u32 __iomem *ipath_getpiobuf(struct ipath_devdata *, u32, u32 *);
void ipath_chg_pioavailkernel(struct ipath_devdata *dd, unsigned start,
				unsigned len, int avail);
void ipath_init_iba7220_funcs(struct ipath_devdata *);
void ipath_init_iba6120_funcs(struct ipath_devdata *);
void ipath_init_iba6110_funcs(struct ipath_devdata *);
void ipath_get_eeprom_info(struct ipath_devdata *);
int ipath_update_eeprom_log(struct ipath_devdata *dd);
void ipath_inc_eeprom_err(struct ipath_devdata *dd, u32 eidx, u32 incr);
u64 ipath_snap_cntr(struct ipath_devdata *, ipath_creg);
void ipath_disarm_senderrbufs(struct ipath_devdata *);
void ipath_force_pio_avail_update(struct ipath_devdata *);
void signal_ib_event(struct ipath_devdata *dd, enum ib_event_type ev);

/*
 * Set LED override, only the two LSBs have "public" meaning, but
 * any non-zero value substitutes them for the Link and LinkTrain
 * LED states.
 */
#define IPATH_LED_PHYS 1 /* Physical (linktraining) GREEN LED */
#define IPATH_LED_LOG 2  /* Logical (link) YELLOW LED */
void ipath_set_led_override(struct ipath_devdata *dd, unsigned int val);

/* send dma routines */
int setup_sdma(struct ipath_devdata *);
void teardown_sdma(struct ipath_devdata *);
void ipath_restart_sdma(struct ipath_devdata *);
void ipath_sdma_intr(struct ipath_devdata *);
int ipath_sdma_verbs_send(struct ipath_devdata *, struct ipath_sge_state *,
			  u32, struct ipath_verbs_txreq *);
/* ipath_sdma_lock should be locked before calling this. */
int ipath_sdma_make_progress(struct ipath_devdata *dd);

/* must be called under ipath_sdma_lock */
static inline u16 ipath_sdma_descq_freecnt(const struct ipath_devdata *dd)
{
	return dd->ipath_sdma_descq_cnt -
		(dd->ipath_sdma_descq_added - dd->ipath_sdma_descq_removed) -
		1 - dd->ipath_sdma_desc_nreserved;
}

static inline void ipath_sdma_desc_reserve(struct ipath_devdata *dd, u16 cnt)
{
	dd->ipath_sdma_desc_nreserved += cnt;
}

static inline void ipath_sdma_desc_unreserve(struct ipath_devdata *dd, u16 cnt)
{
	dd->ipath_sdma_desc_nreserved -= cnt;
}

/*
 * number of words used for protocol header if not set by ipath_userinit();
 */
#define IPATH_DFLT_RCVHDRSIZE 9

int ipath_get_user_pages(unsigned long, size_t, struct page **);
void ipath_release_user_pages(struct page **, size_t);
void ipath_release_user_pages_on_close(struct page **, size_t);
int ipath_eeprom_read(struct ipath_devdata *, u8, void *, int);
int ipath_eeprom_write(struct ipath_devdata *, u8, const void *, int);
int ipath_tempsense_read(struct ipath_devdata *, u8 regnum);
int ipath_tempsense_write(struct ipath_devdata *, u8 regnum, u8 data);

/* these are used for the registers that vary with port */
void ipath_write_kreg_port(const struct ipath_devdata *, ipath_kreg,
			   unsigned, u64);

/*
 * We could have a single register get/put routine, that takes a group type,
 * but this is somewhat clearer and cleaner.  It also gives us some error
 * checking.  64 bit register reads should always work, but are inefficient
 * on opteron (the northbridge always generates 2 separate HT 32 bit reads),
 * so we use kreg32 wherever possible.  User register and counter register
 * reads are always 32 bit reads, so only one form of those routines.
 */

/*
 * At the moment, none of the s-registers are writable, so no
 * ipath_write_sreg().
 */

/**
 * ipath_read_ureg32 - read 32-bit virtualized per-port register
 * @dd: device
 * @regno: register number
 * @port: port number
 *
 * Return the contents of a register that is virtualized to be per port.
 * Returns -1 on errors (not distinguishable from valid contents at
 * runtime; we may add a separate error variable at some point).
 */
static inline u32 ipath_read_ureg32(const struct ipath_devdata *dd,
				    ipath_ureg regno, int port)
{
	if (!dd->ipath_kregbase || !(dd->ipath_flags & IPATH_PRESENT))
		return 0;

	return readl(regno + (u64 __iomem *)
		     (dd->ipath_uregbase +
		      (char __iomem *)dd->ipath_kregbase +
		      dd->ipath_ureg_align * port));
}

/**
 * ipath_write_ureg - write 32-bit virtualized per-port register
 * @dd: device
 * @regno: register number
 * @value: value
 * @port: port
 *
 * Write the contents of a register that is virtualized to be per port.
 */
static inline void ipath_write_ureg(const struct ipath_devdata *dd,
				    ipath_ureg regno, u64 value, int port)
{
	u64 __iomem *ubase = (u64 __iomem *)
		(dd->ipath_uregbase + (char __iomem *) dd->ipath_kregbase +
		 dd->ipath_ureg_align * port);
	if (dd->ipath_kregbase)
		writeq(value, &ubase[regno]);
}

static inline u32 ipath_read_kreg32(const struct ipath_devdata *dd,
				    ipath_kreg regno)
{
	if (!dd->ipath_kregbase || !(dd->ipath_flags & IPATH_PRESENT))
		return -1;
	return readl((u32 __iomem *) & dd->ipath_kregbase[regno]);
}

static inline u64 ipath_read_kreg64(const struct ipath_devdata *dd,
				    ipath_kreg regno)
{
	if (!dd->ipath_kregbase || !(dd->ipath_flags & IPATH_PRESENT))
		return -1;

	return readq(&dd->ipath_kregbase[regno]);
}

static inline void ipath_write_kreg(const struct ipath_devdata *dd,
				    ipath_kreg regno, u64 value)
{
	if (dd->ipath_kregbase)
		writeq(value, &dd->ipath_kregbase[regno]);
}

static inline u64 ipath_read_creg(const struct ipath_devdata *dd,
				  ipath_sreg regno)
{
	if (!dd->ipath_kregbase || !(dd->ipath_flags & IPATH_PRESENT))
		return 0;

	return readq(regno + (u64 __iomem *)
		     (dd->ipath_cregbase +
		      (char __iomem *)dd->ipath_kregbase));
}

static inline u32 ipath_read_creg32(const struct ipath_devdata *dd,
					 ipath_sreg regno)
{
	if (!dd->ipath_kregbase || !(dd->ipath_flags & IPATH_PRESENT))
		return 0;
	return readl(regno + (u64 __iomem *)
		     (dd->ipath_cregbase +
		      (char __iomem *)dd->ipath_kregbase));
}

static inline void ipath_write_creg(const struct ipath_devdata *dd,
				    ipath_creg regno, u64 value)
{
	if (dd->ipath_kregbase)
		writeq(value, regno + (u64 __iomem *)
		       (dd->ipath_cregbase +
			(char __iomem *)dd->ipath_kregbase));
}

static inline void ipath_clear_rcvhdrtail(const struct ipath_portdata *pd)
{
	*((u64 *) pd->port_rcvhdrtail_kvaddr) = 0ULL;
}

static inline u32 ipath_get_rcvhdrtail(const struct ipath_portdata *pd)
{
	return (u32) le64_to_cpu(*((volatile __le64 *)
				pd->port_rcvhdrtail_kvaddr));
}

static inline u32 ipath_get_hdrqtail(const struct ipath_portdata *pd)
{
	const struct ipath_devdata *dd = pd->port_dd;
	u32 hdrqtail;

	if (dd->ipath_flags & IPATH_NODMA_RTAIL) {
		__le32 *rhf_addr;
		u32 seq;

		rhf_addr = (__le32 *) pd->port_rcvhdrq +
			pd->port_head + dd->ipath_rhf_offset;
		seq = ipath_hdrget_seq(rhf_addr);
		hdrqtail = pd->port_head;
		if (seq == pd->port_seq_cnt)
			hdrqtail++;
	} else
		hdrqtail = ipath_get_rcvhdrtail(pd);

	return hdrqtail;
}

static inline u64 ipath_read_ireg(const struct ipath_devdata *dd, ipath_kreg r)
{
	return (dd->ipath_flags & IPATH_INTREG_64) ?
		ipath_read_kreg64(dd, r) : ipath_read_kreg32(dd, r);
}

/*
 * from contents of IBCStatus (or a saved copy), return linkstate
 * Report ACTIVE_DEFER as ACTIVE, because we treat them the same
 * everywhere, anyway (and should be, for almost all purposes).
 */
static inline u32 ipath_ib_linkstate(struct ipath_devdata *dd, u64 ibcs)
{
	u32 state = (u32)(ibcs >> dd->ibcs_ls_shift) &
		INFINIPATH_IBCS_LINKSTATE_MASK;
	if (state == INFINIPATH_IBCS_L_STATE_ACT_DEFER)
		state = INFINIPATH_IBCS_L_STATE_ACTIVE;
	return state;
}

/* from contents of IBCStatus (or a saved copy), return linktrainingstate */
static inline u32 ipath_ib_linktrstate(struct ipath_devdata *dd, u64 ibcs)
{
	return (u32)(ibcs >> INFINIPATH_IBCS_LINKTRAININGSTATE_SHIFT) &
		dd->ibcs_lts_mask;
}

/*
 * from contents of IBCStatus (or a saved copy), return logical link state
 * combination of link state and linktraining state (down, active, init,
 * arm, etc.
 */
static inline u32 ipath_ib_state(struct ipath_devdata *dd, u64 ibcs)
{
	u32 ibs;
	ibs = (u32)(ibcs >> INFINIPATH_IBCS_LINKTRAININGSTATE_SHIFT) &
		dd->ibcs_lts_mask;
	ibs |= (u32)(ibcs &
		(INFINIPATH_IBCS_LINKSTATE_MASK << dd->ibcs_ls_shift));
	return ibs;
}

/*
 * sysfs interface.
 */

struct device_driver;

extern const char ib_ipath_version[];

extern struct attribute_group *ipath_driver_attr_groups[];

int ipath_device_create_group(struct device *, struct ipath_devdata *);
void ipath_device_remove_group(struct device *, struct ipath_devdata *);
int ipath_expose_reset(struct device *);

int ipath_init_ipathfs(void);
void ipath_exit_ipathfs(void);
int ipathfs_add_device(struct ipath_devdata *);
int ipathfs_remove_device(struct ipath_devdata *);

/*
 * dma_addr wrappers - all 0's invalid for hw
 */
dma_addr_t ipath_map_page(struct pci_dev *, struct page *, unsigned long,
			  size_t, int);
dma_addr_t ipath_map_single(struct pci_dev *, void *, size_t, int);
const char *ipath_get_unit_name(int unit);

/*
 * Flush write combining store buffers (if present) and perform a write
 * barrier.
 */
#if defined(CONFIG_X86_64)
#define ipath_flush_wc() asm volatile("sfence" ::: "memory")
#else
#define ipath_flush_wc() wmb()
#endif

extern unsigned ipath_debug; /* debugging bit mask */
extern unsigned ipath_linkrecovery;
extern unsigned ipath_mtu4096;
extern struct mutex ipath_mutex;

#define IPATH_DRV_NAME		"ib_ipath"
#define IPATH_MAJOR		233
#define IPATH_USER_MINOR_BASE	0
#define IPATH_DIAGPKT_MINOR	127
#define IPATH_DIAG_MINOR_BASE	129
#define IPATH_NMINORS		255

#define ipath_dev_err(dd,fmt,...) \
	do { \
		const struct ipath_devdata *__dd = (dd); \
		if (__dd->pcidev) \
			dev_err(&__dd->pcidev->dev, "%s: " fmt, \
				ipath_get_unit_name(__dd->ipath_unit), \
				##__VA_ARGS__); \
		else \
			printk(KERN_ERR IPATH_DRV_NAME ": %s: " fmt, \
			       ipath_get_unit_name(__dd->ipath_unit), \
			       ##__VA_ARGS__); \
	} while (0)

#if _IPATH_DEBUGGING

# define __IPATH_DBG_WHICH(which,fmt,...) \
	do { \
		if (unlikely(ipath_debug & (which))) \
			printk(KERN_DEBUG IPATH_DRV_NAME ": %s: " fmt, \
			       __func__,##__VA_ARGS__); \
	} while(0)

# define ipath_dbg(fmt,...) \
	__IPATH_DBG_WHICH(__IPATH_DBG,fmt,##__VA_ARGS__)
# define ipath_cdbg(which,fmt,...) \
	__IPATH_DBG_WHICH(__IPATH_##which##DBG,fmt,##__VA_ARGS__)

#else /* ! _IPATH_DEBUGGING */

# define ipath_dbg(fmt,...)
# define ipath_cdbg(which,fmt,...)

#endif /* _IPATH_DEBUGGING */

/*
 * this is used for formatting hw error messages...
 */
struct ipath_hwerror_msgs {
	u64 mask;
	const char *msg;
};

#define INFINIPATH_HWE_MSG(a, b) { .mask = INFINIPATH_HWE_##a, .msg = b }

/* in ipath_intr.c... */
void ipath_format_hwerrors(u64 hwerrs,
			   const struct ipath_hwerror_msgs *hwerrmsgs,
			   size_t nhwerrmsgs,
			   char *msg, size_t lmsg);

#endif				/* _IPATH_KERNEL_H */
