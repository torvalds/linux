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

/* statistics can be kept for for tuning/monitoring */
struct ehci_stats {
	/* irq usage */
	unsigned long		normal;
	unsigned long		error;
	unsigned long		reclaim;
	unsigned long		lost_iaa;

	/* termination of urbs from core */
	unsigned long		complete;
	unsigned long		unlink;
};

/* ehci_hcd->lock guards shared data against other CPUs:
 *   ehci_hcd:	async, reclaim, periodic (and shadow), ...
 *   usb_host_endpoint: hcpriv
 *   ehci_qh:	qh_next, qtd_list
 *   ehci_qtd:	qtd_list
 *
 * Also, hold this lock when talking to HC registers or
 * when updating hw_* fields in shared qh/qtd/... structures.
 */

#define	EHCI_MAX_ROOT_PORTS	15		/* see HCS_N_PORTS */

struct ehci_hcd {			/* one per controller */
	/* glue to PCI and HCD framework */
	struct ehci_caps __iomem *caps;
	struct ehci_regs __iomem *regs;
	struct ehci_dbg_port __iomem *debug;

	__u32			hcs_params;	/* cached register copy */
	spinlock_t		lock;

	/* async schedule support */
	struct ehci_qh		*async;
	struct ehci_qh		*reclaim;
	unsigned		reclaim_ready : 1;
	unsigned		scanning : 1;

	/* periodic schedule support */
#define	DEFAULT_I_TDPS		1024		/* some HCs can do less */
	unsigned		periodic_size;
	__le32			*periodic;	/* hw periodic table */
	dma_addr_t		periodic_dma;
	unsigned		i_thresh;	/* uframes HC might cache */

	union ehci_shadow	*pshadow;	/* mirror hw periodic table */
	int			next_uframe;	/* scan periodic, start here */
	unsigned		periodic_sched;	/* periodic activity count */

	/* per root hub port */
	unsigned long		reset_done [EHCI_MAX_ROOT_PORTS];

	/* per-HC memory pools (could be per-bus, but ...) */
	struct dma_pool		*qh_pool;	/* qh per active urb */
	struct dma_pool		*qtd_pool;	/* one or more per qh */
	struct dma_pool		*itd_pool;	/* itd per iso urb */
	struct dma_pool		*sitd_pool;	/* sitd per split iso urb */

	struct timer_list	watchdog;
	struct notifier_block	reboot_notifier;
	unsigned long		actions;
	unsigned		stamp;
	unsigned long		next_statechange;
	u32			command;

	/* SILICON QUIRKS */
	unsigned		is_tdi_rh_tt:1;	/* TDI roothub with TT */
	unsigned		no_selective_suspend:1;
	unsigned		has_fsl_port_bug:1; /* FreeScale */

	u8			sbrn;		/* packed release number */

	/* irq statistics */
#ifdef EHCI_STATS
	struct ehci_stats	stats;
#	define COUNT(x) do { (x)++; } while (0)
#else
#	define COUNT(x) do {} while (0)
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


enum ehci_timer_action {
	TIMER_IO_WATCHDOG,
	TIMER_IAA_WATCHDOG,
	TIMER_ASYNC_SHRINK,
	TIMER_ASYNC_OFF,
};

static inline void
timer_action_done (struct ehci_hcd *ehci, enum ehci_timer_action action)
{
	clear_bit (action, &ehci->actions);
}

static inline void
timer_action (struct ehci_hcd *ehci, enum ehci_timer_action action)
{
	if (!test_and_set_bit (action, &ehci->actions)) {
		unsigned long t;

		switch (action) {
		case TIMER_IAA_WATCHDOG:
			t = EHCI_IAA_JIFFIES;
			break;
		case TIMER_IO_WATCHDOG:
			t = EHCI_IO_JIFFIES;
			break;
		case TIMER_ASYNC_OFF:
			t = EHCI_ASYNC_JIFFIES;
			break;
		// case TIMER_ASYNC_SHRINK:
		default:
			t = EHCI_SHRINK_JIFFIES;
			break;
		}
		t += jiffies;
		// all timings except IAA watchdog can be overridden.
		// async queue SHRINK often precedes IAA.  while it's ready
		// to go OFF neither can matter, and afterwards the IO
		// watchdog stops unless there's still periodic traffic.
		if (action != TIMER_IAA_WATCHDOG
				&& t > ehci->watchdog.expires
				&& timer_pending (&ehci->watchdog))
			return;
		mod_timer (&ehci->watchdog, t);
	}
}

/*-------------------------------------------------------------------------*/

/* EHCI register interface, corresponds to EHCI Revision 0.95 specification */

/* Section 2.2 Host Controller Capability Registers */
struct ehci_caps {
	/* these fields are specified as 8 and 16 bit registers,
	 * but some hosts can't perform 8 or 16 bit PCI accesses.
	 */
	u32		hc_capbase;
#define HC_LENGTH(p)		(((p)>>00)&0x00ff)	/* bits 7:0 */
#define HC_VERSION(p)		(((p)>>16)&0xffff)	/* bits 31:16 */
	u32		hcs_params;     /* HCSPARAMS - offset 0x4 */
#define HCS_DEBUG_PORT(p)	(((p)>>20)&0xf)	/* bits 23:20, debug port? */
#define HCS_INDICATOR(p)	((p)&(1 << 16))	/* true: has port indicators */
#define HCS_N_CC(p)		(((p)>>12)&0xf)	/* bits 15:12, #companion HCs */
#define HCS_N_PCC(p)		(((p)>>8)&0xf)	/* bits 11:8, ports per CC */
#define HCS_PORTROUTED(p)	((p)&(1 << 7))	/* true: port routing */ 
#define HCS_PPC(p)		((p)&(1 << 4))	/* true: port power control */ 
#define HCS_N_PORTS(p)		(((p)>>0)&0xf)	/* bits 3:0, ports on HC */

	u32		hcc_params;      /* HCCPARAMS - offset 0x8 */
#define HCC_EXT_CAPS(p)		(((p)>>8)&0xff)	/* for pci extended caps */
#define HCC_ISOC_CACHE(p)       ((p)&(1 << 7))  /* true: can cache isoc frame */
#define HCC_ISOC_THRES(p)       (((p)>>4)&0x7)  /* bits 6:4, uframes cached */
#define HCC_CANPARK(p)		((p)&(1 << 2))  /* true: can park on async qh */
#define HCC_PGM_FRAMELISTLEN(p) ((p)&(1 << 1))  /* true: periodic_size changes*/
#define HCC_64BIT_ADDR(p)       ((p)&(1))       /* true: can use 64-bit addr */
	u8		portroute [8];	 /* nibbles for routing - offset 0xC */
} __attribute__ ((packed));


/* Section 2.3 Host Controller Operational Registers */
struct ehci_regs {

	/* USBCMD: offset 0x00 */
	u32		command;
/* 23:16 is r/w intr rate, in microframes; default "8" == 1/msec */
#define CMD_PARK	(1<<11)		/* enable "park" on async qh */
#define CMD_PARK_CNT(c)	(((c)>>8)&3)	/* how many transfers to park for */
#define CMD_LRESET	(1<<7)		/* partial reset (no ports, etc) */
#define CMD_IAAD	(1<<6)		/* "doorbell" interrupt async advance */
#define CMD_ASE		(1<<5)		/* async schedule enable */
#define CMD_PSE  	(1<<4)		/* periodic schedule enable */
/* 3:2 is periodic frame list size */
#define CMD_RESET	(1<<1)		/* reset HC not bus */
#define CMD_RUN		(1<<0)		/* start/stop HC */

	/* USBSTS: offset 0x04 */
	u32		status;
#define STS_ASS		(1<<15)		/* Async Schedule Status */
#define STS_PSS		(1<<14)		/* Periodic Schedule Status */
#define STS_RECL	(1<<13)		/* Reclamation */
#define STS_HALT	(1<<12)		/* Not running (any reason) */
/* some bits reserved */
	/* these STS_* flags are also intr_enable bits (USBINTR) */
#define STS_IAA		(1<<5)		/* Interrupted on async advance */
#define STS_FATAL	(1<<4)		/* such as some PCI access errors */
#define STS_FLR		(1<<3)		/* frame list rolled over */
#define STS_PCD		(1<<2)		/* port change detect */
#define STS_ERR		(1<<1)		/* "error" completion (overflow, ...) */
#define STS_INT		(1<<0)		/* "normal" completion (short, ...) */

	/* USBINTR: offset 0x08 */
	u32		intr_enable;

	/* FRINDEX: offset 0x0C */
	u32		frame_index;	/* current microframe number */
	/* CTRLDSSEGMENT: offset 0x10 */
	u32		segment; 	/* address bits 63:32 if needed */
	/* PERIODICLISTBASE: offset 0x14 */
	u32		frame_list; 	/* points to periodic list */
	/* ASYNCLISTADDR: offset 0x18 */
	u32		async_next;	/* address of next async queue head */

	u32		reserved [9];

	/* CONFIGFLAG: offset 0x40 */
	u32		configured_flag;
#define FLAG_CF		(1<<0)		/* true: we'll support "high speed" */

	/* PORTSC: offset 0x44 */
	u32		port_status [0];	/* up to N_PORTS */
/* 31:23 reserved */
#define PORT_WKOC_E	(1<<22)		/* wake on overcurrent (enable) */
#define PORT_WKDISC_E	(1<<21)		/* wake on disconnect (enable) */
#define PORT_WKCONN_E	(1<<20)		/* wake on connect (enable) */
/* 19:16 for port testing */
#define PORT_LED_OFF	(0<<14)
#define PORT_LED_AMBER	(1<<14)
#define PORT_LED_GREEN	(2<<14)
#define PORT_LED_MASK	(3<<14)
#define PORT_OWNER	(1<<13)		/* true: companion hc owns this port */
#define PORT_POWER	(1<<12)		/* true: has power (see PPC) */
#define PORT_USB11(x) (((x)&(3<<10))==(1<<10))	/* USB 1.1 device */
/* 11:10 for detecting lowspeed devices (reset vs release ownership) */
/* 9 reserved */
#define PORT_RESET	(1<<8)		/* reset port */
#define PORT_SUSPEND	(1<<7)		/* suspend port */
#define PORT_RESUME	(1<<6)		/* resume it */
#define PORT_OCC	(1<<5)		/* over current change */
#define PORT_OC		(1<<4)		/* over current active */
#define PORT_PEC	(1<<3)		/* port enable change */
#define PORT_PE		(1<<2)		/* port enable */
#define PORT_CSC	(1<<1)		/* connect status change */
#define PORT_CONNECT	(1<<0)		/* device connected */
#define PORT_RWC_BITS   (PORT_CSC | PORT_PEC | PORT_OCC)
} __attribute__ ((packed));

/* Appendix C, Debug port ... intended for use with special "debug devices"
 * that can help if there's no serial console.  (nonstandard enumeration.)
 */
struct ehci_dbg_port {
	u32	control;
#define DBGP_OWNER	(1<<30)
#define DBGP_ENABLED	(1<<28)
#define DBGP_DONE	(1<<16)
#define DBGP_INUSE	(1<<10)
#define DBGP_ERRCODE(x)	(((x)>>7)&0x07)
#	define DBGP_ERR_BAD	1
#	define DBGP_ERR_SIGNAL	2
#define DBGP_ERROR	(1<<6)
#define DBGP_GO		(1<<5)
#define DBGP_OUT	(1<<4)
#define DBGP_LEN(x)	(((x)>>0)&0x0f)
	u32	pids;
#define DBGP_PID_GET(x)		(((x)>>16)&0xff)
#define DBGP_PID_SET(data,tok)	(((data)<<8)|(tok))
	u32	data03;
	u32	data47;
	u32	address;
#define DBGP_EPADDR(dev,ep)	(((dev)<<8)|(ep))
} __attribute__ ((packed));

/*-------------------------------------------------------------------------*/

#define	QTD_NEXT(dma)	cpu_to_le32((u32)dma)

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
	__le32			hw_next;	  /* see EHCI 3.5.1 */
	__le32			hw_alt_next;      /* see EHCI 3.5.2 */
	__le32			hw_token;         /* see EHCI 3.5.3 */       
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
	__le32			hw_buf [5];        /* see EHCI 3.5.4 */
	__le32			hw_buf_hi [5];        /* Appendix B */

	/* the rest is HCD-private */
	dma_addr_t		qtd_dma;		/* qtd address */
	struct list_head	qtd_list;		/* sw qtd list */
	struct urb		*urb;			/* qtd's urb */
	size_t			length;			/* length of buffer */
} __attribute__ ((aligned (32)));

/* mask NakCnt+T in qh->hw_alt_next */
#define QTD_MASK __constant_cpu_to_le32 (~0x1f)

#define IS_SHORT_READ(token) (QTD_LENGTH (token) != 0 && QTD_PID (token) == 1)

/*-------------------------------------------------------------------------*/

/* type tag from {qh,itd,sitd,fstn}->hw_next */
#define Q_NEXT_TYPE(dma) ((dma) & __constant_cpu_to_le32 (3 << 1))

/* values for that type tag */
#define Q_TYPE_ITD	__constant_cpu_to_le32 (0 << 1)
#define Q_TYPE_QH	__constant_cpu_to_le32 (1 << 1)
#define Q_TYPE_SITD 	__constant_cpu_to_le32 (2 << 1)
#define Q_TYPE_FSTN 	__constant_cpu_to_le32 (3 << 1)

/* next async queue entry, or pointer to interrupt/periodic QH */
#define	QH_NEXT(dma)	(cpu_to_le32(((u32)dma)&~0x01f)|Q_TYPE_QH)

/* for periodic/async schedules and qtd lists, mark end of list */
#define	EHCI_LIST_END	__constant_cpu_to_le32(1) /* "null pointer" to hw */

/*
 * Entries in periodic shadow table are pointers to one of four kinds
 * of data structure.  That's dictated by the hardware; a type tag is
 * encoded in the low bits of the hardware's periodic schedule.  Use
 * Q_NEXT_TYPE to get the tag.
 *
 * For entries in the async schedule, the type tag always says "qh".
 */
union ehci_shadow {
	struct ehci_qh 		*qh;		/* Q_TYPE_QH */
	struct ehci_itd		*itd;		/* Q_TYPE_ITD */
	struct ehci_sitd	*sitd;		/* Q_TYPE_SITD */
	struct ehci_fstn	*fstn;		/* Q_TYPE_FSTN */
	__le32			*hw_next;	/* (all types) */
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

struct ehci_qh {
	/* first part defined by EHCI spec */
	__le32			hw_next;	 /* see EHCI 3.6.1 */
	__le32			hw_info1;        /* see EHCI 3.6.2 */
#define	QH_HEAD		0x00008000
	__le32			hw_info2;        /* see EHCI 3.6.2 */
#define	QH_SMASK	0x000000ff
#define	QH_CMASK	0x0000ff00
#define	QH_HUBADDR	0x007f0000
#define	QH_HUBPORT	0x3f800000
#define	QH_MULT		0xc0000000
	__le32			hw_current;	 /* qtd list - see EHCI 3.6.4 */
	
	/* qtd overlay (hardware parts of a struct ehci_qtd) */
	__le32			hw_qtd_next;
	__le32			hw_alt_next;
	__le32			hw_token;
	__le32			hw_buf [5];
	__le32			hw_buf_hi [5];

	/* the rest is HCD-private */
	dma_addr_t		qh_dma;		/* address of qh */
	union ehci_shadow	qh_next;	/* ptr to qh; or periodic */
	struct list_head	qtd_list;	/* sw qtd list */
	struct ehci_qtd		*dummy;
	struct ehci_qh		*reclaim;	/* next to reclaim */

	struct ehci_hcd		*ehci;
	struct kref		kref;
	unsigned		stamp;

	u8			qh_state;
#define	QH_STATE_LINKED		1		/* HC sees this */
#define	QH_STATE_UNLINK		2		/* HC may still see this */
#define	QH_STATE_IDLE		3		/* HC doesn't see this */
#define	QH_STATE_UNLINK_WAIT	4		/* LINKED and on reclaim q */
#define	QH_STATE_COMPLETING	5		/* don't touch token.HALT */

	/* periodic schedule info */
	u8			usecs;		/* intr bandwidth */
	u8			gap_uf;		/* uframes split/csplit gap */
	u8			c_usecs;	/* ... split completion bw */
	u16			tt_usecs;	/* tt downstream bandwidth */
	unsigned short		period;		/* polling interval */
	unsigned short		start;		/* where polling starts */
#define NO_FRAME ((unsigned short)~0)			/* pick new start */
	struct usb_device	*dev;		/* access to TT */
} __attribute__ ((aligned (32)));

/*-------------------------------------------------------------------------*/

/* description of one iso transaction (up to 3 KB data if highspeed) */
struct ehci_iso_packet {
	/* These will be copied to iTD when scheduling */
	u64			bufp;		/* itd->hw_bufp{,_hi}[pg] |= */
	__le32			transaction;	/* itd->hw_transaction[i] |= */
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
	/* first two fields match QH, but info1 == 0 */
	__le32			hw_next;
	__le32			hw_info1;

	u32			refcount;
	u8			bEndpointAddress;
	u8			highspeed;
	u16			depth;		/* depth in uframes */
	struct list_head	td_list;	/* queued itds/sitds */
	struct list_head	free_list;	/* list of unused itds/sitds */
	struct usb_device	*udev;
 	struct usb_host_endpoint *ep;

	/* output of (re)scheduling */
	unsigned long		start;		/* jiffies */
	unsigned long		rescheduled;
	int			next_uframe;
	__le32			splits;

	/* the rest is derived from the endpoint descriptor,
	 * trusting urb->interval == f(epdesc->bInterval) and
	 * including the extra info for hw_bufp[0..2]
	 */
	u8			interval;
	u8			usecs, c_usecs;
	u16			tt_usecs;
	u16			maxp;
	u16			raw_mask;
	unsigned		bandwidth;

	/* This is used to initialize iTD's hw_bufp fields */
	__le32			buf0;		
	__le32			buf1;		
	__le32			buf2;

	/* this is used to initialize sITD's tt info */
	__le32			address;
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
	__le32			hw_next;           /* see EHCI 3.3.1 */
	__le32			hw_transaction [8]; /* see EHCI 3.3.2 */
#define EHCI_ISOC_ACTIVE        (1<<31)        /* activate transfer this slot */
#define EHCI_ISOC_BUF_ERR       (1<<30)        /* Data buffer error */
#define EHCI_ISOC_BABBLE        (1<<29)        /* babble detected */
#define EHCI_ISOC_XACTERR       (1<<28)        /* XactErr - transaction error */
#define	EHCI_ITD_LENGTH(tok)	(((tok)>>16) & 0x0fff)
#define	EHCI_ITD_IOC		(1 << 15)	/* interrupt on complete */

#define ITD_ACTIVE	__constant_cpu_to_le32(EHCI_ISOC_ACTIVE)

	__le32			hw_bufp [7];	/* see EHCI 3.3.3 */ 
	__le32			hw_bufp_hi [7];	/* Appendix B */

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
	u8			usecs[8];
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
	__le32			hw_next;
/* uses bit field macros above - see EHCI 0.95 Table 3-8 */
	__le32			hw_fullspeed_ep;	/* EHCI table 3-9 */
	__le32			hw_uframe;		/* EHCI table 3-10 */
	__le32			hw_results;		/* EHCI table 3-11 */
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

#define SITD_ACTIVE	__constant_cpu_to_le32(SITD_STS_ACTIVE)

	__le32			hw_buf [2];		/* EHCI table 3-12 */
	__le32			hw_backpointer;		/* EHCI table 3-13 */
	__le32			hw_buf_hi [2];		/* Appendix B */

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
	__le32			hw_next;	/* any periodic q entry */
	__le32			hw_prev;	/* qh or EHCI_LIST_END */

	/* the rest is HCD-private */
	dma_addr_t		fstn_dma;
	union ehci_shadow	fstn_next;	/* ptr to periodic q entry */
} __attribute__ ((aligned (32)));

/*-------------------------------------------------------------------------*/

#ifdef CONFIG_USB_EHCI_ROOT_HUB_TT

/*
 * Some EHCI controllers have a Transaction Translator built into the
 * root hub. This is a non-standard feature.  Each controller will need
 * to add code to the following inline functions, and call them as
 * needed (mostly in root hub code).
 */

#define	ehci_is_TDI(e)			((e)->is_tdi_rh_tt)

/* Returns the speed of a device attached to a port on the root hub. */
static inline unsigned int
ehci_port_speed(struct ehci_hcd *ehci, unsigned int portsc)
{
	if (ehci_is_TDI(ehci)) {
		switch ((portsc>>26)&3) {
		case 0:
			return 0;
		case 1:
			return (1<<USB_PORT_FEAT_LOWSPEED);
		case 2:
		default:
			return (1<<USB_PORT_FEAT_HIGHSPEED);
		}
	}
	return (1<<USB_PORT_FEAT_HIGHSPEED);
}

#else

#define	ehci_is_TDI(e)			(0)

#define	ehci_port_speed(ehci, portsc)	(1<<USB_PORT_FEAT_HIGHSPEED)
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


/*-------------------------------------------------------------------------*/

#ifndef DEBUG
#define STUB_DEBUG_FILES
#endif	/* DEBUG */

/*-------------------------------------------------------------------------*/

#endif /* __LINUX_EHCI_HCD_H */
