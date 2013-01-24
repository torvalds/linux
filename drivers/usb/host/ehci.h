/*
 * Copyright (c) 2001-2002 by David Brownell
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __LINUX_EHCI_HCD_H
#define __LINUX_EHCI_HCD_H

/* definitions used for the EHCI driver */

/*
 * __hc32 and __hc16 are "Host Controller" types, they may be equivalent to
 * __leXX (normally) or __beXX (given EHCI_BIG_ENDIAN_DESC), depending on
 * the host controller implementation.
 *
 * To facilitate the strongest possible byte-order checking from "sparse"
 * and so on, we use __leXX unless that's not practical.
 */
#ifdef CONFIG_USB_EHCI_BIG_ENDIAN_DESC
typedef __u32 __bitwise __hc32;
typedef __u16 __bitwise __hc16;
#else
#define __hc32	__le32
#define __hc16	__le16
#endif

/* statistics can be kept for tuning/monitoring */
struct ehci_stats {
	/* irq usage */
	unsigned long		normal;
	unsigned long		error;
	unsigned long		iaa;
	unsigned long		lost_iaa;

	/* termination of urbs from core */
	unsigned long		complete;
	unsigned long		unlink;
};

/* ehci_hcd->lock guards shared data against other CPUs:
 *   ehci_hcd:	async, unlink, periodic (and shadow), ...
 *   usb_host_endpoint: hcpriv
 *   ehci_qh:	qh_next, qtd_list
 *   ehci_qtd:	qtd_list
 *
 * Also, hold this lock when talking to HC registers or
 * when updating hw_* fields in shared qh/qtd/... structures.
 */

#define	EHCI_MAX_ROOT_PORTS	15		/* see HCS_N_PORTS */

/*
 * ehci_rh_state values of EHCI_RH_RUNNING or above mean that the
 * controller may be doing DMA.  Lower values mean there's no DMA.
 */
enum ehci_rh_state {
	EHCI_RH_HALTED,
	EHCI_RH_SUSPENDED,
	EHCI_RH_RUNNING,
	EHCI_RH_STOPPING
};

/*
 * Timer events, ordered by increasing delay length.
 * Always update event_delays_ns[] and event_handlers[] (defined in
 * ehci-timer.c) in parallel with this list.
 */
enum ehci_hrtimer_event {
	EHCI_HRTIMER_POLL_ASS,		/* Poll for async schedule off */
	EHCI_HRTIMER_POLL_PSS,		/* Poll for periodic schedule off */
	EHCI_HRTIMER_POLL_DEAD,		/* Wait for dead controller to stop */
	EHCI_HRTIMER_UNLINK_INTR,	/* Wait for interrupt QH unlink */
	EHCI_HRTIMER_FREE_ITDS,		/* Wait for unused iTDs and siTDs */
	EHCI_HRTIMER_ASYNC_UNLINKS,	/* Unlink empty async QHs */
	EHCI_HRTIMER_IAA_WATCHDOG,	/* Handle lost IAA interrupts */
	EHCI_HRTIMER_DISABLE_PERIODIC,	/* Wait to disable periodic sched */
	EHCI_HRTIMER_DISABLE_ASYNC,	/* Wait to disable async sched */
	EHCI_HRTIMER_IO_WATCHDOG,	/* Check for missing IRQs */
	EHCI_HRTIMER_NUM_EVENTS		/* Must come last */
};
#define EHCI_HRTIMER_NO_EVENT	99

struct ehci_hcd {			/* one per controller */
	/* timing support */
	enum ehci_hrtimer_event	next_hrtimer_event;
	unsigned		enabled_hrtimer_events;
	ktime_t			hr_timeouts[EHCI_HRTIMER_NUM_EVENTS];
	struct hrtimer		hrtimer;

	int			PSS_poll_count;
	int			ASS_poll_count;
	int			died_poll_count;

	/* glue to PCI and HCD framework */
	struct ehci_caps __iomem *caps;
	struct ehci_regs __iomem *regs;
	struct ehci_dbg_port __iomem *debug;

	__u32			hcs_params;	/* cached register copy */
	spinlock_t		lock;
	enum ehci_rh_state	rh_state;

	/* general schedule support */
	bool			scanning:1;
	bool			need_rescan:1;
	bool			intr_unlinking:1;
	bool			async_unlinking:1;
	bool			shutdown:1;
	struct ehci_qh		*qh_scan_next;

	/* async schedule support */
	struct ehci_qh		*async;
	struct ehci_qh		*dummy;		/* For AMD quirk use */
	struct ehci_qh		*async_unlink;
	struct ehci_qh		*async_unlink_last;
	struct ehci_qh		*async_iaa;
	unsigned		async_unlink_cycle;
	unsigned		async_count;	/* async activity count */

	/* periodic schedule support */
#define	DEFAULT_I_TDPS		1024		/* some HCs can do less */
	unsigned		periodic_size;
	__hc32			*periodic;	/* hw periodic table */
	dma_addr_t		periodic_dma;
	struct list_head	intr_qh_list;
	unsigned		i_thresh;	/* uframes HC might cache */

	union ehci_shadow	*pshadow;	/* mirror hw periodic table */
	struct ehci_qh		*intr_unlink;
	struct ehci_qh		*intr_unlink_last;
	unsigned		intr_unlink_cycle;
	unsigned		now_frame;	/* frame from HC hardware */
	unsigned		last_iso_frame;	/* last frame scanned for iso */
	unsigned		intr_count;	/* intr activity count */
	unsigned		isoc_count;	/* isoc activity count */
	unsigned		periodic_count;	/* periodic activity count */
	unsigned		uframe_periodic_max; /* max periodic time per uframe */


	/* list of itds & sitds completed while now_frame was still active */
	struct list_head	cached_itd_list;
	struct ehci_itd		*last_itd_to_free;
	struct list_head	cached_sitd_list;
	struct ehci_sitd	*last_sitd_to_free;

	/* per root hub port */
	unsigned long		reset_done [EHCI_MAX_ROOT_PORTS];

	/* bit vectors (one bit per port) */
	unsigned long		bus_suspended;		/* which ports were
			already suspended at the start of a bus suspend */
	unsigned long		companion_ports;	/* which ports are
			dedicated to the companion controller */
	unsigned long		owned_ports;		/* which ports are
			owned by the companion during a bus suspend */
	unsigned long		port_c_suspend;		/* which ports have
			the change-suspend feature turned on */
	unsigned long		suspended_ports;	/* which ports are
			suspended */
	unsigned long		resuming_ports;		/* which ports have
			started to resume */

	/* per-HC memory pools (could be per-bus, but ...) */
	struct dma_pool		*qh_pool;	/* qh per active urb */
	struct dma_pool		*qtd_pool;	/* one or more per qh */
	struct dma_pool		*itd_pool;	/* itd per iso urb */
	struct dma_pool		*sitd_pool;	/* sitd per split iso urb */

	unsigned		random_frame;
	unsigned long		next_statechange;
	ktime_t			last_periodic_enable;
	u32			command;

	/* SILICON QUIRKS */
	unsigned		no_selective_suspend:1;
	unsigned		has_fsl_port_bug:1; /* FreeScale */
	unsigned		big_endian_mmio:1;
	unsigned		big_endian_desc:1;
	unsigned		big_endian_capbase:1;
	unsigned		has_amcc_usb23:1;
	unsigned		need_io_watchdog:1;
	unsigned		amd_pll_fix:1;
	unsigned		use_dummy_qh:1;	/* AMD Frame List table quirk*/
	unsigned		has_synopsys_hc_bug:1; /* Synopsys HC */
	unsigned		frame_index_bug:1; /* MosChip (AKA NetMos) */

	/* required for usb32 quirk */
	#define OHCI_CTRL_HCFS          (3 << 6)
	#define OHCI_USB_OPER           (2 << 6)
	#define OHCI_USB_SUSPEND        (3 << 6)

	#define OHCI_HCCTRL_OFFSET      0x4
	#define OHCI_HCCTRL_LEN         0x4
	__hc32			*ohci_hcctrl_reg;
	unsigned		has_hostpc:1;
	unsigned		has_ppcd:1; /* support per-port change bits */
	u8			sbrn;		/* packed release number */

	/* irq statistics */
#ifdef EHCI_STATS
	struct ehci_stats	stats;
#	define COUNT(x) do { (x)++; } while (0)
#else
#	define COUNT(x) do {} while (0)
#endif

	/* debug files */
#ifdef DEBUG
	struct dentry		*debug_dir;
#endif
};

/* convert between an HCD pointer and the corresponding EHCI_HCD */
static inline struct ehci_hcd *hcd_to_ehci (struct usb_hcd *hcd)
{
	return (struct ehci_hcd *) (hcd->hcd_priv);
}
static inline struct usb_hcd *ehci_to_hcd (struct ehci_hcd *ehci)
{
	return container_of ((void *) ehci, struct usb_hcd, hcd_priv);
}

/*-------------------------------------------------------------------------*/

#include <linux/usb/ehci_def.h>

/*-------------------------------------------------------------------------*/

#define	QTD_NEXT(ehci, dma)	cpu_to_hc32(ehci, (u32)dma)

/*
 * EHCI Specification 0.95 Section 3.5
 * QTD: describe data transfer components (buffer, direction, ...)
 * See Fig 3-6 "Queue Element Transfer Descriptor Block Diagram".
 *
 * These are associated only with "QH" (Queue Head) structures,
 * used with control, bulk, and interrupt transfers.
 */
struct ehci_qtd {
	/* first part defined by EHCI spec */
	__hc32			hw_next;	/* see EHCI 3.5.1 */
	__hc32			hw_alt_next;    /* see EHCI 3.5.2 */
	__hc32			hw_token;       /* see EHCI 3.5.3 */
#define	QTD_TOGGLE	(1 << 31)	/* data toggle */
#define	QTD_LENGTH(tok)	(((tok)>>16) & 0x7fff)
#define	QTD_IOC		(1 << 15)	/* interrupt on complete */
#define	QTD_CERR(tok)	(((tok)>>10) & 0x3)
#define	QTD_PID(tok)	(((tok)>>8) & 0x3)
#define	QTD_STS_ACTIVE	(1 << 7)	/* HC may execute this */
#define	QTD_STS_HALT	(1 << 6)	/* halted on error */
#define	QTD_STS_DBE	(1 << 5)	/* data buffer error (in HC) */
#define	QTD_STS_BABBLE	(1 << 4)	/* device was babbling (qtd halted) */
#define	QTD_STS_XACT	(1 << 3)	/* device gave illegal response */
#define	QTD_STS_MMF	(1 << 2)	/* incomplete split transaction */
#define	QTD_STS_STS	(1 << 1)	/* split transaction state */
#define	QTD_STS_PING	(1 << 0)	/* issue PING? */

#define ACTIVE_BIT(ehci)	cpu_to_hc32(ehci, QTD_STS_ACTIVE)
#define HALT_BIT(ehci)		cpu_to_hc32(ehci, QTD_STS_HALT)
#define STATUS_BIT(ehci)	cpu_to_hc32(ehci, QTD_STS_STS)

	__hc32			hw_buf [5];        /* see EHCI 3.5.4 */
	__hc32			hw_buf_hi [5];        /* Appendix B */

	/* the rest is HCD-private */
	dma_addr_t		qtd_dma;		/* qtd address */
	struct list_head	qtd_list;		/* sw qtd list */
	struct urb		*urb;			/* qtd's urb */
	size_t			length;			/* length of buffer */
} __attribute__ ((aligned (32)));

/* mask NakCnt+T in qh->hw_alt_next */
#define QTD_MASK(ehci)	cpu_to_hc32 (ehci, ~0x1f)

#define IS_SHORT_READ(token) (QTD_LENGTH (token) != 0 && QTD_PID (token) == 1)

/*-------------------------------------------------------------------------*/

/* type tag from {qh,itd,sitd,fstn}->hw_next */
#define Q_NEXT_TYPE(ehci,dma)	((dma) & cpu_to_hc32(ehci, 3 << 1))

/*
 * Now the following defines are not converted using the
 * cpu_to_le32() macro anymore, since we have to support
 * "dynamic" switching between be and le support, so that the driver
 * can be used on one system with SoC EHCI controller using big-endian
 * descriptors as well as a normal little-endian PCI EHCI controller.
 */
/* values for that type tag */
#define Q_TYPE_ITD	(0 << 1)
#define Q_TYPE_QH	(1 << 1)
#define Q_TYPE_SITD	(2 << 1)
#define Q_TYPE_FSTN	(3 << 1)

/* next async queue entry, or pointer to interrupt/periodic QH */
#define QH_NEXT(ehci,dma)	(cpu_to_hc32(ehci, (((u32)dma)&~0x01f)|Q_TYPE_QH))

/* for periodic/async schedules and qtd lists, mark end of list */
#define EHCI_LIST_END(ehci)	cpu_to_hc32(ehci, 1) /* "null pointer" to hw */

/*
 * Entries in periodic shadow table are pointers to one of four kinds
 * of data structure.  That's dictated by the hardware; a type tag is
 * encoded in the low bits of the hardware's periodic schedule.  Use
 * Q_NEXT_TYPE to get the tag.
 *
 * For entries in the async schedule, the type tag always says "qh".
 */
union ehci_shadow {
	struct ehci_qh		*qh;		/* Q_TYPE_QH */
	struct ehci_itd		*itd;		/* Q_TYPE_ITD */
	struct ehci_sitd	*sitd;		/* Q_TYPE_SITD */
	struct ehci_fstn	*fstn;		/* Q_TYPE_FSTN */
	__hc32			*hw_next;	/* (all types) */
	void			*ptr;
};

/*-------------------------------------------------------------------------*/

/*
 * EHCI Specification 0.95 Section 3.6
 * QH: describes control/bulk/interrupt endpoints
 * See Fig 3-7 "Queue Head Structure Layout".
 *
 * These appear in both the async and (for interrupt) periodic schedules.
 */

/* first part defined by EHCI spec */
struct ehci_qh_hw {
	__hc32			hw_next;	/* see EHCI 3.6.1 */
	__hc32			hw_info1;       /* see EHCI 3.6.2 */
#define	QH_CONTROL_EP	(1 << 27)	/* FS/LS control endpoint */
#define	QH_HEAD		(1 << 15)	/* Head of async reclamation list */
#define	QH_TOGGLE_CTL	(1 << 14)	/* Data toggle control */
#define	QH_HIGH_SPEED	(2 << 12)	/* Endpoint speed */
#define	QH_LOW_SPEED	(1 << 12)
#define	QH_FULL_SPEED	(0 << 12)
#define	QH_INACTIVATE	(1 << 7)	/* Inactivate on next transaction */
	__hc32			hw_info2;        /* see EHCI 3.6.2 */
#define	QH_SMASK	0x000000ff
#define	QH_CMASK	0x0000ff00
#define	QH_HUBADDR	0x007f0000
#define	QH_HUBPORT	0x3f800000
#define	QH_MULT		0xc0000000
	__hc32			hw_current;	/* qtd list - see EHCI 3.6.4 */

	/* qtd overlay (hardware parts of a struct ehci_qtd) */
	__hc32			hw_qtd_next;
	__hc32			hw_alt_next;
	__hc32			hw_token;
	__hc32			hw_buf [5];
	__hc32			hw_buf_hi [5];
} __attribute__ ((aligned(32)));

struct ehci_qh {
	struct ehci_qh_hw	*hw;		/* Must come first */
	/* the rest is HCD-private */
	dma_addr_t		qh_dma;		/* address of qh */
	union ehci_shadow	qh_next;	/* ptr to qh; or periodic */
	struct list_head	qtd_list;	/* sw qtd list */
	struct list_head	intr_node;	/* list of intr QHs */
	struct ehci_qtd		*dummy;
	struct ehci_qh		*unlink_next;	/* next on unlink list */

	unsigned		unlink_cycle;

	u8			needs_rescan;	/* Dequeue during giveback */
	u8			qh_state;
#define	QH_STATE_LINKED		1		/* HC sees this */
#define	QH_STATE_UNLINK		2		/* HC may still see this */
#define	QH_STATE_IDLE		3		/* HC doesn't see this */
#define	QH_STATE_UNLINK_WAIT	4		/* LINKED and on unlink q */
#define	QH_STATE_COMPLETING	5		/* don't touch token.HALT */

	u8			xacterrs;	/* XactErr retry counter */
#define	QH_XACTERR_MAX		32		/* XactErr retry limit */

	/* periodic schedule info */
	u8			usecs;		/* intr bandwidth */
	u8			gap_uf;		/* uframes split/csplit gap */
	u8			c_usecs;	/* ... split completion bw */
	u16			tt_usecs;	/* tt downstream bandwidth */
	unsigned short		period;		/* polling interval */
	unsigned short		start;		/* where polling starts */
#define NO_FRAME ((unsigned short)~0)			/* pick new start */

	struct usb_device	*dev;		/* access to TT */
	unsigned		is_out:1;	/* bulk or intr OUT */
	unsigned		clearing_tt:1;	/* Clear-TT-Buf in progress */
};

/*-------------------------------------------------------------------------*/

/* description of one iso transaction (up to 3 KB data if highspeed) */
struct ehci_iso_packet {
	/* These will be copied to iTD when scheduling */
	u64			bufp;		/* itd->hw_bufp{,_hi}[pg] |= */
	__hc32			transaction;	/* itd->hw_transaction[i] |= */
	u8			cross;		/* buf crosses pages */
	/* for full speed OUT splits */
	u32			buf1;
};

/* temporary schedule data for packets from iso urbs (both speeds)
 * each packet is one logical usb transaction to the device (not TT),
 * beginning at stream->next_uframe
 */
struct ehci_iso_sched {
	struct list_head	td_list;
	unsigned		span;
	struct ehci_iso_packet	packet [0];
};

/*
 * ehci_iso_stream - groups all (s)itds for this endpoint.
 * acts like a qh would, if EHCI had them for ISO.
 */
struct ehci_iso_stream {
	/* first field matches ehci_hq, but is NULL */
	struct ehci_qh_hw	*hw;

	u8			bEndpointAddress;
	u8			highspeed;
	struct list_head	td_list;	/* queued itds/sitds */
	struct list_head	free_list;	/* list of unused itds/sitds */
	struct usb_device	*udev;
	struct usb_host_endpoint *ep;

	/* output of (re)scheduling */
	int			next_uframe;
	__hc32			splits;

	/* the rest is derived from the endpoint descriptor,
	 * trusting urb->interval == f(epdesc->bInterval) and
	 * including the extra info for hw_bufp[0..2]
	 */
	u8			usecs, c_usecs;
	u16			interval;
	u16			tt_usecs;
	u16			maxp;
	u16			raw_mask;
	unsigned		bandwidth;

	/* This is used to initialize iTD's hw_bufp fields */
	__hc32			buf0;
	__hc32			buf1;
	__hc32			buf2;

	/* this is used to initialize sITD's tt info */
	__hc32			address;
};

/*-------------------------------------------------------------------------*/

/*
 * EHCI Specification 0.95 Section 3.3
 * Fig 3-4 "Isochronous Transaction Descriptor (iTD)"
 *
 * Schedule records for high speed iso xfers
 */
struct ehci_itd {
	/* first part defined by EHCI spec */
	__hc32			hw_next;           /* see EHCI 3.3.1 */
	__hc32			hw_transaction [8]; /* see EHCI 3.3.2 */
#define EHCI_ISOC_ACTIVE        (1<<31)        /* activate transfer this slot */
#define EHCI_ISOC_BUF_ERR       (1<<30)        /* Data buffer error */
#define EHCI_ISOC_BABBLE        (1<<29)        /* babble detected */
#define EHCI_ISOC_XACTERR       (1<<28)        /* XactErr - transaction error */
#define	EHCI_ITD_LENGTH(tok)	(((tok)>>16) & 0x0fff)
#define	EHCI_ITD_IOC		(1 << 15)	/* interrupt on complete */

#define ITD_ACTIVE(ehci)	cpu_to_hc32(ehci, EHCI_ISOC_ACTIVE)

	__hc32			hw_bufp [7];	/* see EHCI 3.3.3 */
	__hc32			hw_bufp_hi [7];	/* Appendix B */

	/* the rest is HCD-private */
	dma_addr_t		itd_dma;	/* for this itd */
	union ehci_shadow	itd_next;	/* ptr to periodic q entry */

	struct urb		*urb;
	struct ehci_iso_stream	*stream;	/* endpoint's queue */
	struct list_head	itd_list;	/* list of stream's itds */

	/* any/all hw_transactions here may be used by that urb */
	unsigned		frame;		/* where scheduled */
	unsigned		pg;
	unsigned		index[8];	/* in urb->iso_frame_desc */
} __attribute__ ((aligned (32)));

/*-------------------------------------------------------------------------*/

/*
 * EHCI Specification 0.95 Section 3.4
 * siTD, aka split-transaction isochronous Transfer Descriptor
 *       ... describe full speed iso xfers through TT in hubs
 * see Figure 3-5 "Split-transaction Isochronous Transaction Descriptor (siTD)
 */
struct ehci_sitd {
	/* first part defined by EHCI spec */
	__hc32			hw_next;
/* uses bit field macros above - see EHCI 0.95 Table 3-8 */
	__hc32			hw_fullspeed_ep;	/* EHCI table 3-9 */
	__hc32			hw_uframe;		/* EHCI table 3-10 */
	__hc32			hw_results;		/* EHCI table 3-11 */
#define	SITD_IOC	(1 << 31)	/* interrupt on completion */
#define	SITD_PAGE	(1 << 30)	/* buffer 0/1 */
#define	SITD_LENGTH(x)	(0x3ff & ((x)>>16))
#define	SITD_STS_ACTIVE	(1 << 7)	/* HC may execute this */
#define	SITD_STS_ERR	(1 << 6)	/* error from TT */
#define	SITD_STS_DBE	(1 << 5)	/* data buffer error (in HC) */
#define	SITD_STS_BABBLE	(1 << 4)	/* device was babbling */
#define	SITD_STS_XACT	(1 << 3)	/* illegal IN response */
#define	SITD_STS_MMF	(1 << 2)	/* incomplete split transaction */
#define	SITD_STS_STS	(1 << 1)	/* split transaction state */

#define SITD_ACTIVE(ehci)	cpu_to_hc32(ehci, SITD_STS_ACTIVE)

	__hc32			hw_buf [2];		/* EHCI table 3-12 */
	__hc32			hw_backpointer;		/* EHCI table 3-13 */
	__hc32			hw_buf_hi [2];		/* Appendix B */

	/* the rest is HCD-private */
	dma_addr_t		sitd_dma;
	union ehci_shadow	sitd_next;	/* ptr to periodic q entry */

	struct urb		*urb;
	struct ehci_iso_stream	*stream;	/* endpoint's queue */
	struct list_head	sitd_list;	/* list of stream's sitds */
	unsigned		frame;
	unsigned		index;
} __attribute__ ((aligned (32)));

/*-------------------------------------------------------------------------*/

/*
 * EHCI Specification 0.96 Section 3.7
 * Periodic Frame Span Traversal Node (FSTN)
 *
 * Manages split interrupt transactions (using TT) that span frame boundaries
 * into uframes 0/1; see 4.12.2.2.  In those uframes, a "save place" FSTN
 * makes the HC jump (back) to a QH to scan for fs/ls QH completions until
 * it hits a "restore" FSTN; then it returns to finish other uframe 0/1 work.
 */
struct ehci_fstn {
	__hc32			hw_next;	/* any periodic q entry */
	__hc32			hw_prev;	/* qh or EHCI_LIST_END */

	/* the rest is HCD-private */
	dma_addr_t		fstn_dma;
	union ehci_shadow	fstn_next;	/* ptr to periodic q entry */
} __attribute__ ((aligned (32)));

/*-------------------------------------------------------------------------*/

/* Prepare the PORTSC wakeup flags during controller suspend/resume */

#define ehci_prepare_ports_for_controller_suspend(ehci, do_wakeup)	\
		ehci_adjust_port_wakeup_flags(ehci, true, do_wakeup);

#define ehci_prepare_ports_for_controller_resume(ehci)			\
		ehci_adjust_port_wakeup_flags(ehci, false, false);

/*-------------------------------------------------------------------------*/

#ifdef CONFIG_USB_EHCI_ROOT_HUB_TT

/*
 * Some EHCI controllers have a Transaction Translator built into the
 * root hub. This is a non-standard feature.  Each controller will need
 * to add code to the following inline functions, and call them as
 * needed (mostly in root hub code).
 */

#define	ehci_is_TDI(e)			(ehci_to_hcd(e)->has_tt)

/* Returns the speed of a device attached to a port on the root hub. */
static inline unsigned int
ehci_port_speed(struct ehci_hcd *ehci, unsigned int portsc)
{
	if (ehci_is_TDI(ehci)) {
		switch ((portsc >> (ehci->has_hostpc ? 25 : 26)) & 3) {
		case 0:
			return 0;
		case 1:
			return USB_PORT_STAT_LOW_SPEED;
		case 2:
		default:
			return USB_PORT_STAT_HIGH_SPEED;
		}
	}
	return USB_PORT_STAT_HIGH_SPEED;
}

#else

#define	ehci_is_TDI(e)			(0)

#define	ehci_port_speed(ehci, portsc)	USB_PORT_STAT_HIGH_SPEED
#endif

/*-------------------------------------------------------------------------*/

#ifdef CONFIG_PPC_83xx
/* Some Freescale processors have an erratum in which the TT
 * port number in the queue head was 0..N-1 instead of 1..N.
 */
#define	ehci_has_fsl_portno_bug(e)		((e)->has_fsl_port_bug)
#else
#define	ehci_has_fsl_portno_bug(e)		(0)
#endif

/*
 * While most USB host controllers implement their registers in
 * little-endian format, a minority (celleb companion chip) implement
 * them in big endian format.
 *
 * This attempts to support either format at compile time without a
 * runtime penalty, or both formats with the additional overhead
 * of checking a flag bit.
 *
 * ehci_big_endian_capbase is a special quirk for controllers that
 * implement the HC capability registers as separate registers and not
 * as fields of a 32-bit register.
 */

#ifdef CONFIG_USB_EHCI_BIG_ENDIAN_MMIO
#define ehci_big_endian_mmio(e)		((e)->big_endian_mmio)
#define ehci_big_endian_capbase(e)	((e)->big_endian_capbase)
#else
#define ehci_big_endian_mmio(e)		0
#define ehci_big_endian_capbase(e)	0
#endif

/*
 * Big-endian read/write functions are arch-specific.
 * Other arches can be added if/when they're needed.
 */
#if defined(CONFIG_ARM) && defined(CONFIG_ARCH_IXP4XX)
#define readl_be(addr)		__raw_readl((__force unsigned *)addr)
#define writel_be(val, addr)	__raw_writel(val, (__force unsigned *)addr)
#endif

static inline unsigned int ehci_readl(const struct ehci_hcd *ehci,
		__u32 __iomem * regs)
{
#ifdef CONFIG_USB_EHCI_BIG_ENDIAN_MMIO
	return ehci_big_endian_mmio(ehci) ?
		readl_be(regs) :
		readl(regs);
#else
	return readl(regs);
#endif
}

static inline void ehci_writel(const struct ehci_hcd *ehci,
		const unsigned int val, __u32 __iomem *regs)
{
#ifdef CONFIG_USB_EHCI_BIG_ENDIAN_MMIO
	ehci_big_endian_mmio(ehci) ?
		writel_be(val, regs) :
		writel(val, regs);
#else
	writel(val, regs);
#endif
}

/*
 * On certain ppc-44x SoC there is a HW issue, that could only worked around with
 * explicit suspend/operate of OHCI. This function hereby makes sense only on that arch.
 * Other common bits are dependent on has_amcc_usb23 quirk flag.
 */
#ifdef CONFIG_44x
static inline void set_ohci_hcfs(struct ehci_hcd *ehci, int operational)
{
	u32 hc_control;

	hc_control = (readl_be(ehci->ohci_hcctrl_reg) & ~OHCI_CTRL_HCFS);
	if (operational)
		hc_control |= OHCI_USB_OPER;
	else
		hc_control |= OHCI_USB_SUSPEND;

	writel_be(hc_control, ehci->ohci_hcctrl_reg);
	(void) readl_be(ehci->ohci_hcctrl_reg);
}
#else
static inline void set_ohci_hcfs(struct ehci_hcd *ehci, int operational)
{ }
#endif

/*-------------------------------------------------------------------------*/

/*
 * The AMCC 440EPx not only implements its EHCI registers in big-endian
 * format, but also its DMA data structures (descriptors).
 *
 * EHCI controllers accessed through PCI work normally (little-endian
 * everywhere), so we won't bother supporting a BE-only mode for now.
 */
#ifdef CONFIG_USB_EHCI_BIG_ENDIAN_DESC
#define ehci_big_endian_desc(e)		((e)->big_endian_desc)

/* cpu to ehci */
static inline __hc32 cpu_to_hc32 (const struct ehci_hcd *ehci, const u32 x)
{
	return ehci_big_endian_desc(ehci)
		? (__force __hc32)cpu_to_be32(x)
		: (__force __hc32)cpu_to_le32(x);
}

/* ehci to cpu */
static inline u32 hc32_to_cpu (const struct ehci_hcd *ehci, const __hc32 x)
{
	return ehci_big_endian_desc(ehci)
		? be32_to_cpu((__force __be32)x)
		: le32_to_cpu((__force __le32)x);
}

static inline u32 hc32_to_cpup (const struct ehci_hcd *ehci, const __hc32 *x)
{
	return ehci_big_endian_desc(ehci)
		? be32_to_cpup((__force __be32 *)x)
		: le32_to_cpup((__force __le32 *)x);
}

#else

/* cpu to ehci */
static inline __hc32 cpu_to_hc32 (const struct ehci_hcd *ehci, const u32 x)
{
	return cpu_to_le32(x);
}

/* ehci to cpu */
static inline u32 hc32_to_cpu (const struct ehci_hcd *ehci, const __hc32 x)
{
	return le32_to_cpu(x);
}

static inline u32 hc32_to_cpup (const struct ehci_hcd *ehci, const __hc32 *x)
{
	return le32_to_cpup(x);
}

#endif

/*-------------------------------------------------------------------------*/

#define ehci_dbg(ehci, fmt, args...) \
	dev_dbg(ehci_to_hcd(ehci)->self.controller , fmt , ## args)
#define ehci_err(ehci, fmt, args...) \
	dev_err(ehci_to_hcd(ehci)->self.controller , fmt , ## args)
#define ehci_info(ehci, fmt, args...) \
	dev_info(ehci_to_hcd(ehci)->self.controller , fmt , ## args)
#define ehci_warn(ehci, fmt, args...) \
	dev_warn(ehci_to_hcd(ehci)->self.controller , fmt , ## args)

#ifdef VERBOSE_DEBUG
#	define ehci_vdbg ehci_dbg
#else
	static inline void ehci_vdbg(struct ehci_hcd *ehci, ...) {}
#endif

#ifndef DEBUG
#define STUB_DEBUG_FILES
#endif	/* DEBUG */

/*-------------------------------------------------------------------------*/

/* Declarations of things exported for use by ehci platform drivers */

struct ehci_driver_overrides {
	size_t		extra_priv_size;
	int		(*reset)(struct usb_hcd *hcd);
};

extern void	ehci_init_driver(struct hc_driver *drv,
				const struct ehci_driver_overrides *over);
extern int	ehci_setup(struct usb_hcd *hcd);

#ifdef CONFIG_PM
extern int	ehci_suspend(struct usb_hcd *hcd, bool do_wakeup);
extern int	ehci_resume(struct usb_hcd *hcd, bool hibernated);
#endif	/* CONFIG_PM */

#endif /* __LINUX_EHCI_HCD_H */
