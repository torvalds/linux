/*
 * Host interface registers
 */

#define OXU_DEVICEID			0x00
	#define OXU_REV_MASK		0xffff0000
	#define OXU_REV_SHIFT		16
	#define OXU_REV_2100		0x2100
	#define OXU_BO_SHIFT		8
	#define OXU_BO_MASK		(0x3 << OXU_BO_SHIFT)
	#define OXU_MAJ_REV_SHIFT	4
	#define OXU_MAJ_REV_MASK	(0xf << OXU_MAJ_REV_SHIFT)
	#define OXU_MIN_REV_SHIFT	0
	#define OXU_MIN_REV_MASK	(0xf << OXU_MIN_REV_SHIFT)
#define OXU_HOSTIFCONFIG		0x04
#define OXU_SOFTRESET			0x08
	#define OXU_SRESET		(1 << 0)

#define OXU_PIOBURSTREADCTRL		0x0C

#define OXU_CHIPIRQSTATUS		0x10
#define OXU_CHIPIRQEN_SET		0x14
#define OXU_CHIPIRQEN_CLR		0x18
	#define OXU_USBSPHLPWUI		0x00000080
	#define OXU_USBOTGLPWUI		0x00000040
	#define OXU_USBSPHI		0x00000002
	#define OXU_USBOTGI		0x00000001

#define OXU_CLKCTRL_SET			0x1C
	#define OXU_SYSCLKEN		0x00000008
	#define OXU_USBSPHCLKEN		0x00000002
	#define OXU_USBOTGCLKEN		0x00000001

#define OXU_ASO				0x68
	#define OXU_SPHPOEN		0x00000100
	#define OXU_OVRCCURPUPDEN	0x00000800
	#define OXU_ASO_OP		(1 << 10)
	#define OXU_COMPARATOR		0x000004000

#define OXU_USBMODE			0x1A8
	#define OXU_VBPS		0x00000020
	#define OXU_ES_LITTLE		0x00000000
	#define OXU_CM_HOST_ONLY	0x00000003

/*
 * Proper EHCI structs & defines
 */

/* Magic numbers that can affect system performance */
#define EHCI_TUNE_CERR		3	/* 0-3 qtd retries; 0 == don't stop */
#define EHCI_TUNE_RL_HS		4	/* nak throttle; see 4.9 */
#define EHCI_TUNE_RL_TT		0
#define EHCI_TUNE_MULT_HS	1	/* 1-3 transactions/uframe; 4.10.3 */
#define EHCI_TUNE_MULT_TT	1
#define EHCI_TUNE_FLS		2	/* (small) 256 frame schedule */

struct oxu_hcd;

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
	u8		portroute[8];	 /* nibbles for routing - offset 0xC */
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

#define INTR_MASK (STS_IAA | STS_FATAL | STS_PCD | STS_ERR | STS_INT)

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

	u32		reserved[9];

	/* CONFIGFLAG: offset 0x40 */
	u32		configured_flag;
#define FLAG_CF		(1<<0)		/* true: we'll support "high speed" */

	/* PORTSC: offset 0x44 */
	u32		port_status[0];	/* up to N_PORTS */
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
#define PORT_USB11(x) (((x)&(3<<10)) == (1<<10))	/* USB 1.1 device */
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
#define DBGP_PID_SET(data, tok)	(((data)<<8)|(tok))
	u32	data03;
	u32	data47;
	u32	address;
#define DBGP_EPADDR(dev, ep)	(((dev)<<8)|(ep))
} __attribute__ ((packed));


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
	__le32			hw_next;		/* see EHCI 3.5.1 */
	__le32			hw_alt_next;		/* see EHCI 3.5.2 */
	__le32			hw_token;		/* see EHCI 3.5.3 */
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
	__le32			hw_buf[5];		/* see EHCI 3.5.4 */
	__le32			hw_buf_hi[5];		/* Appendix B */

	/* the rest is HCD-private */
	dma_addr_t		qtd_dma;		/* qtd address */
	struct list_head	qtd_list;		/* sw qtd list */
	struct urb		*urb;			/* qtd's urb */
	size_t			length;			/* length of buffer */

	u32			qtd_buffer_len;
	void			*buffer;
	dma_addr_t		buffer_dma;
	void			*transfer_buffer;
	void			*transfer_dma;
} __attribute__ ((aligned(32)));

/* mask NakCnt+T in qh->hw_alt_next */
#define QTD_MASK cpu_to_le32 (~0x1f)

#define IS_SHORT_READ(token) (QTD_LENGTH(token) != 0 && QTD_PID(token) == 1)

/* Type tag from {qh, itd, sitd, fstn}->hw_next */
#define Q_NEXT_TYPE(dma) ((dma) & cpu_to_le32 (3 << 1))

/* values for that type tag */
#define Q_TYPE_QH	cpu_to_le32 (1 << 1)

/* next async queue entry, or pointer to interrupt/periodic QH */
#define	QH_NEXT(dma)	(cpu_to_le32(((u32)dma)&~0x01f)|Q_TYPE_QH)

/* for periodic/async schedules and qtd lists, mark end of list */
#define	EHCI_LIST_END	cpu_to_le32(1) /* "null pointer" to hw */

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
	__le32			*hw_next;	/* (all types) */
	void			*ptr;
};

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
	__le32			hw_info1;	/* see EHCI 3.6.2 */
#define	QH_HEAD		0x00008000
	__le32			hw_info2;	/* see EHCI 3.6.2 */
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
	__le32			hw_buf[5];
	__le32			hw_buf_hi[5];

	/* the rest is HCD-private */
	dma_addr_t		qh_dma;		/* address of qh */
	union ehci_shadow	qh_next;	/* ptr to qh; or periodic */
	struct list_head	qtd_list;	/* sw qtd list */
	struct ehci_qtd		*dummy;
	struct ehci_qh		*reclaim;	/* next to reclaim */

	struct oxu_hcd		*oxu;
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
} __attribute__ ((aligned(32)));

/*
 * Proper OXU210HP structs
 */

#define OXU_OTG_CORE_OFFSET	0x00400
#define OXU_OTG_CAP_OFFSET	(OXU_OTG_CORE_OFFSET + 0x100)
#define OXU_SPH_CORE_OFFSET	0x00800
#define OXU_SPH_CAP_OFFSET	(OXU_SPH_CORE_OFFSET + 0x100)

#define OXU_OTG_MEM		0xE000
#define OXU_SPH_MEM		0x16000

/* Only how many elements & element structure are specifies here. */
/* 2 host controllers are enabled - total size <= 28 kbytes */
#define	DEFAULT_I_TDPS		1024
#define QHEAD_NUM		16
#define QTD_NUM			32
#define SITD_NUM		8
#define MURB_NUM		8

#define BUFFER_NUM		8
#define BUFFER_SIZE		512

struct oxu_info {
	struct usb_hcd *hcd[2];
};

struct oxu_buf {
	u8			buffer[BUFFER_SIZE];
} __attribute__ ((aligned(BUFFER_SIZE)));

struct oxu_onchip_mem {
	struct oxu_buf		db_pool[BUFFER_NUM];

	u32			frame_list[DEFAULT_I_TDPS];
	struct ehci_qh		qh_pool[QHEAD_NUM];
	struct ehci_qtd		qtd_pool[QTD_NUM];
} __attribute__ ((aligned(4 << 10)));

#define	EHCI_MAX_ROOT_PORTS	15		/* see HCS_N_PORTS */

struct oxu_murb {
	struct urb		urb;
	struct urb		*main;
	u8			last;
};

struct oxu_hcd {				/* one per controller */
	unsigned int		is_otg:1;

	u8			qh_used[QHEAD_NUM];
	u8			qtd_used[QTD_NUM];
	u8			db_used[BUFFER_NUM];
	u8			murb_used[MURB_NUM];

	struct oxu_onchip_mem	__iomem *mem;
	spinlock_t		mem_lock;

	struct timer_list	urb_timer;

	struct ehci_caps __iomem *caps;
	struct ehci_regs __iomem *regs;

	__u32			hcs_params;	/* cached register copy */
	spinlock_t		lock;

	/* async schedule support */
	struct ehci_qh		*async;
	struct ehci_qh		*reclaim;
	unsigned		reclaim_ready:1;
	unsigned		scanning:1;

	/* periodic schedule support */
	unsigned		periodic_size;
	__le32			*periodic;	/* hw periodic table */
	dma_addr_t		periodic_dma;
	unsigned		i_thresh;	/* uframes HC might cache */

	union ehci_shadow	*pshadow;	/* mirror hw periodic table */
	int			next_uframe;	/* scan periodic, start here */
	unsigned		periodic_sched;	/* periodic activity count */

	/* per root hub port */
	unsigned long		reset_done[EHCI_MAX_ROOT_PORTS];
	/* bit vectors (one bit per port) */
	unsigned long		bus_suspended;	/* which ports were
						 * already suspended at the
						 * start of a bus suspend
						 */
	unsigned long		companion_ports;/* which ports are dedicated
						 * to the companion controller
						 */

	struct timer_list	watchdog;
	unsigned long		actions;
	unsigned		stamp;
	unsigned long		next_statechange;
	u32			command;

	/* SILICON QUIRKS */
	struct list_head	urb_list;	/* this is the head to urb
						 * queue that didn't get enough
						 * resources
						 */
	struct oxu_murb		*murb_pool;	/* murb per split big urb */
	unsigned urb_len;

	u8			sbrn;		/* packed release number */
};

#define EHCI_IAA_JIFFIES	(HZ/100)	/* arbitrary; ~10 msec */
#define EHCI_IO_JIFFIES	 	(HZ/10)		/* io watchdog > irq_thresh */
#define EHCI_ASYNC_JIFFIES      (HZ/20)		/* async idle timeout */
#define EHCI_SHRINK_JIFFIES     (HZ/200)	/* async qh unlink delay */

enum ehci_timer_action {
	TIMER_IO_WATCHDOG,
	TIMER_IAA_WATCHDOG,
	TIMER_ASYNC_SHRINK,
	TIMER_ASYNC_OFF,
};

#include <linux/oxu210hp.h>
