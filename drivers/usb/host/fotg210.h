/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_FOTG210_H
#define __LINUX_FOTG210_H

#include <linux/usb/ehci-dbgp.h>

/* definitions used for the EHCI driver */

/*
 * __hc32 and __hc16 are "Host Controller" types, they may be equivalent to
 * __leXX (normally) or __beXX (given FOTG210_BIG_ENDIAN_DESC), depending on
 * the host controller implementation.
 *
 * To facilitate the strongest possible byte-order checking from "sparse"
 * and so on, we use __leXX unless that's not practical.
 */
#define __hc32	__le32
#define __hc16	__le16

/* statistics can be kept for tuning/monitoring */
struct fotg210_stats {
	/* irq usage */
	unsigned long		normal;
	unsigned long		error;
	unsigned long		iaa;
	unsigned long		lost_iaa;

	/* termination of urbs from core */
	unsigned long		complete;
	unsigned long		unlink;
};

/* fotg210_hcd->lock guards shared data against other CPUs:
 *   fotg210_hcd:	async, unlink, periodic (and shadow), ...
 *   usb_host_endpoint: hcpriv
 *   fotg210_qh:	qh_next, qtd_list
 *   fotg210_qtd:	qtd_list
 *
 * Also, hold this lock when talking to HC registers or
 * when updating hw_* fields in shared qh/qtd/... structures.
 */

#define	FOTG210_MAX_ROOT_PORTS	1		/* see HCS_N_PORTS */

/*
 * fotg210_rh_state values of FOTG210_RH_RUNNING or above mean that the
 * controller may be doing DMA.  Lower values mean there's no DMA.
 */
enum fotg210_rh_state {
	FOTG210_RH_HALTED,
	FOTG210_RH_SUSPENDED,
	FOTG210_RH_RUNNING,
	FOTG210_RH_STOPPING
};

/*
 * Timer events, ordered by increasing delay length.
 * Always update event_delays_ns[] and event_handlers[] (defined in
 * ehci-timer.c) in parallel with this list.
 */
enum fotg210_hrtimer_event {
	FOTG210_HRTIMER_POLL_ASS,	/* Poll for async schedule off */
	FOTG210_HRTIMER_POLL_PSS,	/* Poll for periodic schedule off */
	FOTG210_HRTIMER_POLL_DEAD,	/* Wait for dead controller to stop */
	FOTG210_HRTIMER_UNLINK_INTR,	/* Wait for interrupt QH unlink */
	FOTG210_HRTIMER_FREE_ITDS,	/* Wait for unused iTDs and siTDs */
	FOTG210_HRTIMER_ASYNC_UNLINKS,	/* Unlink empty async QHs */
	FOTG210_HRTIMER_IAA_WATCHDOG,	/* Handle lost IAA interrupts */
	FOTG210_HRTIMER_DISABLE_PERIODIC, /* Wait to disable periodic sched */
	FOTG210_HRTIMER_DISABLE_ASYNC,	/* Wait to disable async sched */
	FOTG210_HRTIMER_IO_WATCHDOG,	/* Check for missing IRQs */
	FOTG210_HRTIMER_NUM_EVENTS	/* Must come last */
};
#define FOTG210_HRTIMER_NO_EVENT	99

struct fotg210_hcd {			/* one per controller */
	/* timing support */
	enum fotg210_hrtimer_event	next_hrtimer_event;
	unsigned		enabled_hrtimer_events;
	ktime_t			hr_timeouts[FOTG210_HRTIMER_NUM_EVENTS];
	struct hrtimer		hrtimer;

	int			PSS_poll_count;
	int			ASS_poll_count;
	int			died_poll_count;

	/* glue to PCI and HCD framework */
	struct fotg210_caps __iomem *caps;
	struct fotg210_regs __iomem *regs;
	struct ehci_dbg_port __iomem *debug;

	__u32			hcs_params;	/* cached register copy */
	spinlock_t		lock;
	enum fotg210_rh_state	rh_state;

	/* general schedule support */
	bool			scanning:1;
	bool			need_rescan:1;
	bool			intr_unlinking:1;
	bool			async_unlinking:1;
	bool			shutdown:1;
	struct fotg210_qh		*qh_scan_next;

	/* async schedule support */
	struct fotg210_qh		*async;
	struct fotg210_qh		*dummy;		/* For AMD quirk use */
	struct fotg210_qh		*async_unlink;
	struct fotg210_qh		*async_unlink_last;
	struct fotg210_qh		*async_iaa;
	unsigned		async_unlink_cycle;
	unsigned		async_count;	/* async activity count */

	/* periodic schedule support */
#define	DEFAULT_I_TDPS		1024		/* some HCs can do less */
	unsigned		periodic_size;
	__hc32			*periodic;	/* hw periodic table */
	dma_addr_t		periodic_dma;
	struct list_head	intr_qh_list;
	unsigned		i_thresh;	/* uframes HC might cache */

	union fotg210_shadow	*pshadow;	/* mirror hw periodic table */
	struct fotg210_qh		*intr_unlink;
	struct fotg210_qh		*intr_unlink_last;
	unsigned		intr_unlink_cycle;
	unsigned		now_frame;	/* frame from HC hardware */
	unsigned		next_frame;	/* scan periodic, start here */
	unsigned		intr_count;	/* intr activity count */
	unsigned		isoc_count;	/* isoc activity count */
	unsigned		periodic_count;	/* periodic activity count */
	/* max periodic time per uframe */
	unsigned		uframe_periodic_max;


	/* list of itds completed while now_frame was still active */
	struct list_head	cached_itd_list;
	struct fotg210_itd	*last_itd_to_free;

	/* per root hub port */
	unsigned long		reset_done[FOTG210_MAX_ROOT_PORTS];

	/* bit vectors (one bit per port)
	 * which ports were already suspended at the start of a bus suspend
	 */
	unsigned long		bus_suspended;

	/* which ports are edicated to the companion controller */
	unsigned long		companion_ports;

	/* which ports are owned by the companion during a bus suspend */
	unsigned long		owned_ports;

	/* which ports have the change-suspend feature turned on */
	unsigned long		port_c_suspend;

	/* which ports are suspended */
	unsigned long		suspended_ports;

	/* which ports have started to resume */
	unsigned long		resuming_ports;

	/* per-HC memory pools (could be per-bus, but ...) */
	struct dma_pool		*qh_pool;	/* qh per active urb */
	struct dma_pool		*qtd_pool;	/* one or more per qh */
	struct dma_pool		*itd_pool;	/* itd per iso urb */

	unsigned		random_frame;
	unsigned long		next_statechange;
	ktime_t			last_periodic_enable;
	u32			command;

	/* SILICON QUIRKS */
	unsigned		need_io_watchdog:1;
	unsigned		fs_i_thresh:1;	/* Intel iso scheduling */

	u8			sbrn;		/* packed release number */

	/* irq statistics */
#ifdef FOTG210_STATS
	struct fotg210_stats	stats;
#	define INCR(x) ((x)++)
#else
#	define INCR(x) do {} while (0)
#endif

	/* silicon clock */
	struct clk		*pclk;
};

/* convert between an HCD pointer and the corresponding FOTG210_HCD */
static inline struct fotg210_hcd *hcd_to_fotg210(struct usb_hcd *hcd)
{
	return (struct fotg210_hcd *)(hcd->hcd_priv);
}
static inline struct usb_hcd *fotg210_to_hcd(struct fotg210_hcd *fotg210)
{
	return container_of((void *) fotg210, struct usb_hcd, hcd_priv);
}

/*-------------------------------------------------------------------------*/

/* EHCI register interface, corresponds to EHCI Revision 0.95 specification */

/* Section 2.2 Host Controller Capability Registers */
struct fotg210_caps {
	/* these fields are specified as 8 and 16 bit registers,
	 * but some hosts can't perform 8 or 16 bit PCI accesses.
	 * some hosts treat caplength and hciversion as parts of a 32-bit
	 * register, others treat them as two separate registers, this
	 * affects the memory map for big endian controllers.
	 */
	u32		hc_capbase;
#define HC_LENGTH(fotg210, p)	(0x00ff&((p) >> /* bits 7:0 / offset 00h */ \
				(fotg210_big_endian_capbase(fotg210) ? 24 : 0)))
#define HC_VERSION(fotg210, p)	(0xffff&((p) >> /* bits 31:16 / offset 02h */ \
				(fotg210_big_endian_capbase(fotg210) ? 0 : 16)))
	u32		hcs_params;     /* HCSPARAMS - offset 0x4 */
#define HCS_N_PORTS(p)		(((p)>>0)&0xf)	/* bits 3:0, ports on HC */

	u32		hcc_params;	/* HCCPARAMS - offset 0x8 */
#define HCC_CANPARK(p)		((p)&(1 << 2))  /* true: can park on async qh */
#define HCC_PGM_FRAMELISTLEN(p) ((p)&(1 << 1))  /* true: periodic_size changes*/
	u8		portroute[8];	 /* nibbles for routing - offset 0xC */
};


/* Section 2.3 Host Controller Operational Registers */
struct fotg210_regs {

	/* USBCMD: offset 0x00 */
	u32		command;

/* EHCI 1.1 addendum */
/* 23:16 is r/w intr rate, in microframes; default "8" == 1/msec */
#define CMD_PARK	(1<<11)		/* enable "park" on async qh */
#define CMD_PARK_CNT(c)	(((c)>>8)&3)	/* how many transfers to park for */
#define CMD_IAAD	(1<<6)		/* "doorbell" interrupt async advance */
#define CMD_ASE		(1<<5)		/* async schedule enable */
#define CMD_PSE		(1<<4)		/* periodic schedule enable */
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
	u32		segment;	/* address bits 63:32 if needed */
	/* PERIODICLISTBASE: offset 0x14 */
	u32		frame_list;	/* points to periodic list */
	/* ASYNCLISTADDR: offset 0x18 */
	u32		async_next;	/* address of next async queue head */

	u32	reserved1;
	/* PORTSC: offset 0x20 */
	u32	port_status;
/* 31:23 reserved */
#define PORT_USB11(x) (((x)&(3<<10)) == (1<<10))	/* USB 1.1 device */
#define PORT_RESET	(1<<8)		/* reset port */
#define PORT_SUSPEND	(1<<7)		/* suspend port */
#define PORT_RESUME	(1<<6)		/* resume it */
#define PORT_PEC	(1<<3)		/* port enable change */
#define PORT_PE		(1<<2)		/* port enable */
#define PORT_CSC	(1<<1)		/* connect status change */
#define PORT_CONNECT	(1<<0)		/* device connected */
#define PORT_RWC_BITS   (PORT_CSC | PORT_PEC)
	u32     reserved2[19];

	/* OTGCSR: offet 0x70 */
	u32     otgcsr;
#define OTGCSR_HOST_SPD_TYP     (3 << 22)
#define OTGCSR_A_BUS_DROP	(1 << 5)
#define OTGCSR_A_BUS_REQ	(1 << 4)

	/* OTGISR: offset 0x74 */
	u32     otgisr;
#define OTGISR_OVC	(1 << 10)

	u32     reserved3[15];

	/* GMIR: offset 0xB4 */
	u32     gmir;
#define GMIR_INT_POLARITY	(1 << 3) /*Active High*/
#define GMIR_MHC_INT		(1 << 2)
#define GMIR_MOTG_INT		(1 << 1)
#define GMIR_MDEV_INT	(1 << 0)
};

/*-------------------------------------------------------------------------*/

#define	QTD_NEXT(fotg210, dma)	cpu_to_hc32(fotg210, (u32)dma)

/*
 * EHCI Specification 0.95 Section 3.5
 * QTD: describe data transfer components (buffer, direction, ...)
 * See Fig 3-6 "Queue Element Transfer Descriptor Block Diagram".
 *
 * These are associated only with "QH" (Queue Head) structures,
 * used with control, bulk, and interrupt transfers.
 */
struct fotg210_qtd {
	/* first part defined by EHCI spec */
	__hc32			hw_next;	/* see EHCI 3.5.1 */
	__hc32			hw_alt_next;    /* see EHCI 3.5.2 */
	__hc32			hw_token;	/* see EHCI 3.5.3 */
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

#define ACTIVE_BIT(fotg210)	cpu_to_hc32(fotg210, QTD_STS_ACTIVE)
#define HALT_BIT(fotg210)		cpu_to_hc32(fotg210, QTD_STS_HALT)
#define STATUS_BIT(fotg210)	cpu_to_hc32(fotg210, QTD_STS_STS)

	__hc32			hw_buf[5];	/* see EHCI 3.5.4 */
	__hc32			hw_buf_hi[5];	/* Appendix B */

	/* the rest is HCD-private */
	dma_addr_t		qtd_dma;		/* qtd address */
	struct list_head	qtd_list;		/* sw qtd list */
	struct urb		*urb;			/* qtd's urb */
	size_t			length;			/* length of buffer */
} __aligned(32);

/* mask NakCnt+T in qh->hw_alt_next */
#define QTD_MASK(fotg210)	cpu_to_hc32(fotg210, ~0x1f)

#define IS_SHORT_READ(token) (QTD_LENGTH(token) != 0 && QTD_PID(token) == 1)

/*-------------------------------------------------------------------------*/

/* type tag from {qh,itd,fstn}->hw_next */
#define Q_NEXT_TYPE(fotg210, dma)	((dma) & cpu_to_hc32(fotg210, 3 << 1))

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
#define QH_NEXT(fotg210, dma) \
	(cpu_to_hc32(fotg210, (((u32)dma)&~0x01f)|Q_TYPE_QH))

/* for periodic/async schedules and qtd lists, mark end of list */
#define FOTG210_LIST_END(fotg210) \
	cpu_to_hc32(fotg210, 1) /* "null pointer" to hw */

/*
 * Entries in periodic shadow table are pointers to one of four kinds
 * of data structure.  That's dictated by the hardware; a type tag is
 * encoded in the low bits of the hardware's periodic schedule.  Use
 * Q_NEXT_TYPE to get the tag.
 *
 * For entries in the async schedule, the type tag always says "qh".
 */
union fotg210_shadow {
	struct fotg210_qh	*qh;		/* Q_TYPE_QH */
	struct fotg210_itd	*itd;		/* Q_TYPE_ITD */
	struct fotg210_fstn	*fstn;		/* Q_TYPE_FSTN */
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
struct fotg210_qh_hw {
	__hc32			hw_next;	/* see EHCI 3.6.1 */
	__hc32			hw_info1;	/* see EHCI 3.6.2 */
#define	QH_CONTROL_EP	(1 << 27)	/* FS/LS control endpoint */
#define	QH_HEAD		(1 << 15)	/* Head of async reclamation list */
#define	QH_TOGGLE_CTL	(1 << 14)	/* Data toggle control */
#define	QH_HIGH_SPEED	(2 << 12)	/* Endpoint speed */
#define	QH_LOW_SPEED	(1 << 12)
#define	QH_FULL_SPEED	(0 << 12)
#define	QH_INACTIVATE	(1 << 7)	/* Inactivate on next transaction */
	__hc32			hw_info2;	/* see EHCI 3.6.2 */
#define	QH_SMASK	0x000000ff
#define	QH_CMASK	0x0000ff00
#define	QH_HUBADDR	0x007f0000
#define	QH_HUBPORT	0x3f800000
#define	QH_MULT		0xc0000000
	__hc32			hw_current;	/* qtd list - see EHCI 3.6.4 */

	/* qtd overlay (hardware parts of a struct fotg210_qtd) */
	__hc32			hw_qtd_next;
	__hc32			hw_alt_next;
	__hc32			hw_token;
	__hc32			hw_buf[5];
	__hc32			hw_buf_hi[5];
} __aligned(32);

struct fotg210_qh {
	struct fotg210_qh_hw	*hw;		/* Must come first */
	/* the rest is HCD-private */
	dma_addr_t		qh_dma;		/* address of qh */
	union fotg210_shadow	qh_next;	/* ptr to qh; or periodic */
	struct list_head	qtd_list;	/* sw qtd list */
	struct list_head	intr_node;	/* list of intr QHs */
	struct fotg210_qtd	*dummy;
	struct fotg210_qh	*unlink_next;	/* next on unlink list */

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
struct fotg210_iso_packet {
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
struct fotg210_iso_sched {
	struct list_head	td_list;
	unsigned		span;
	struct fotg210_iso_packet	packet[];
};

/*
 * fotg210_iso_stream - groups all (s)itds for this endpoint.
 * acts like a qh would, if EHCI had them for ISO.
 */
struct fotg210_iso_stream {
	/* first field matches fotg210_hq, but is NULL */
	struct fotg210_qh_hw	*hw;

	u8			bEndpointAddress;
	u8			highspeed;
	struct list_head	td_list;	/* queued itds */
	struct list_head	free_list;	/* list of unused itds */
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
struct fotg210_itd {
	/* first part defined by EHCI spec */
	__hc32			hw_next;	/* see EHCI 3.3.1 */
	__hc32			hw_transaction[8]; /* see EHCI 3.3.2 */
#define FOTG210_ISOC_ACTIVE	(1<<31)	/* activate transfer this slot */
#define FOTG210_ISOC_BUF_ERR	(1<<30)	/* Data buffer error */
#define FOTG210_ISOC_BABBLE	(1<<29)	/* babble detected */
#define FOTG210_ISOC_XACTERR	(1<<28)	/* XactErr - transaction error */
#define	FOTG210_ITD_LENGTH(tok)	(((tok)>>16) & 0x0fff)
#define	FOTG210_ITD_IOC		(1 << 15)	/* interrupt on complete */

#define ITD_ACTIVE(fotg210)	cpu_to_hc32(fotg210, FOTG210_ISOC_ACTIVE)

	__hc32			hw_bufp[7];	/* see EHCI 3.3.3 */
	__hc32			hw_bufp_hi[7];	/* Appendix B */

	/* the rest is HCD-private */
	dma_addr_t		itd_dma;	/* for this itd */
	union fotg210_shadow	itd_next;	/* ptr to periodic q entry */

	struct urb		*urb;
	struct fotg210_iso_stream	*stream;	/* endpoint's queue */
	struct list_head	itd_list;	/* list of stream's itds */

	/* any/all hw_transactions here may be used by that urb */
	unsigned		frame;		/* where scheduled */
	unsigned		pg;
	unsigned		index[8];	/* in urb->iso_frame_desc */
} __aligned(32);

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
struct fotg210_fstn {
	__hc32			hw_next;	/* any periodic q entry */
	__hc32			hw_prev;	/* qh or FOTG210_LIST_END */

	/* the rest is HCD-private */
	dma_addr_t		fstn_dma;
	union fotg210_shadow	fstn_next;	/* ptr to periodic q entry */
} __aligned(32);

/*-------------------------------------------------------------------------*/

/* Prepare the PORTSC wakeup flags during controller suspend/resume */

#define fotg210_prepare_ports_for_controller_suspend(fotg210, do_wakeup) \
		fotg210_adjust_port_wakeup_flags(fotg210, true, do_wakeup)

#define fotg210_prepare_ports_for_controller_resume(fotg210)		\
		fotg210_adjust_port_wakeup_flags(fotg210, false, false)

/*-------------------------------------------------------------------------*/

/*
 * Some EHCI controllers have a Transaction Translator built into the
 * root hub. This is a non-standard feature.  Each controller will need
 * to add code to the following inline functions, and call them as
 * needed (mostly in root hub code).
 */

static inline unsigned int
fotg210_get_speed(struct fotg210_hcd *fotg210, unsigned int portsc)
{
	return (readl(&fotg210->regs->otgcsr)
		& OTGCSR_HOST_SPD_TYP) >> 22;
}

/* Returns the speed of a device attached to a port on the root hub. */
static inline unsigned int
fotg210_port_speed(struct fotg210_hcd *fotg210, unsigned int portsc)
{
	switch (fotg210_get_speed(fotg210, portsc)) {
	case 0:
		return 0;
	case 1:
		return USB_PORT_STAT_LOW_SPEED;
	case 2:
	default:
		return USB_PORT_STAT_HIGH_SPEED;
	}
}

/*-------------------------------------------------------------------------*/

#define	fotg210_has_fsl_portno_bug(e)		(0)

/*
 * While most USB host controllers implement their registers in
 * little-endian format, a minority (celleb companion chip) implement
 * them in big endian format.
 *
 * This attempts to support either format at compile time without a
 * runtime penalty, or both formats with the additional overhead
 * of checking a flag bit.
 *
 */

#define fotg210_big_endian_mmio(e)	0
#define fotg210_big_endian_capbase(e)	0

static inline unsigned int fotg210_readl(const struct fotg210_hcd *fotg210,
		__u32 __iomem *regs)
{
	return readl(regs);
}

static inline void fotg210_writel(const struct fotg210_hcd *fotg210,
		const unsigned int val, __u32 __iomem *regs)
{
	writel(val, regs);
}

/* cpu to fotg210 */
static inline __hc32 cpu_to_hc32(const struct fotg210_hcd *fotg210, const u32 x)
{
	return cpu_to_le32(x);
}

/* fotg210 to cpu */
static inline u32 hc32_to_cpu(const struct fotg210_hcd *fotg210, const __hc32 x)
{
	return le32_to_cpu(x);
}

static inline u32 hc32_to_cpup(const struct fotg210_hcd *fotg210,
			       const __hc32 *x)
{
	return le32_to_cpup(x);
}

/*-------------------------------------------------------------------------*/

static inline unsigned fotg210_read_frame_index(struct fotg210_hcd *fotg210)
{
	return fotg210_readl(fotg210, &fotg210->regs->frame_index);
}

#define fotg210_itdlen(urb, desc, t) ({			\
	usb_pipein((urb)->pipe) ?				\
	(desc)->length - FOTG210_ITD_LENGTH(t) :			\
	FOTG210_ITD_LENGTH(t);					\
})
/*-------------------------------------------------------------------------*/

#endif /* __LINUX_FOTG210_H */
