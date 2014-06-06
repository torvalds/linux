/*
 * OHCI HCD (Host Controller Driver) for USB.
 *
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 * (C) Copyright 2000-2002 David Brownell <dbrownell@users.sourceforge.net>
 *
 * This file is licenced under the GPL.
 */

/*
 * __hc32 and __hc16 are "Host Controller" types, they may be equivalent to
 * __leXX (normally) or __beXX (given OHCI_BIG_ENDIAN), depending on the
 * host controller implementation.
 */
typedef __u32 __bitwise __hc32;
typedef __u16 __bitwise __hc16;

/*
 * OHCI Endpoint Descriptor (ED) ... holds TD queue
 * See OHCI spec, section 4.2
 *
 * This is a "Queue Head" for those transfers, which is why
 * both EHCI and UHCI call similar structures a "QH".
 */
struct ed {
	/* first fields are hardware-specified */
	__hc32			hwINFO;      /* endpoint config bitmap */
	/* info bits defined by hcd */
#define ED_DEQUEUE	(1 << 27)
	/* info bits defined by the hardware */
#define ED_ISO		(1 << 15)
#define ED_SKIP		(1 << 14)
#define ED_LOWSPEED	(1 << 13)
#define ED_OUT		(0x01 << 11)
#define ED_IN		(0x02 << 11)
	__hc32			hwTailP;	/* tail of TD list */
	__hc32			hwHeadP;	/* head of TD list (hc r/w) */
#define ED_C		(0x02)			/* toggle carry */
#define ED_H		(0x01)			/* halted */
	__hc32			hwNextED;	/* next ED in list */

	/* rest are purely for the driver's use */
	dma_addr_t		dma;		/* addr of ED */
	struct td		*dummy;		/* next TD to activate */

	/* host's view of schedule */
	struct ed		*ed_next;	/* on schedule or rm_list */
	struct ed		*ed_prev;	/* for non-interrupt EDs */
	struct list_head	td_list;	/* "shadow list" of our TDs */

	/* create --> IDLE --> OPER --> ... --> IDLE --> destroy
	 * usually:  OPER --> UNLINK --> (IDLE | OPER) --> ...
	 */
	u8			state;		/* ED_{IDLE,UNLINK,OPER} */
#define ED_IDLE		0x00		/* NOT linked to HC */
#define ED_UNLINK	0x01		/* being unlinked from hc */
#define ED_OPER		0x02		/* IS linked to hc */

	u8			type;		/* PIPE_{BULK,...} */

	/* periodic scheduling params (for intr and iso) */
	u8			branch;
	u16			interval;
	u16			load;
	u16			last_iso;	/* iso only */

	/* HC may see EDs on rm_list until next frame (frame_no == tick) */
	u16			tick;
} __attribute__ ((aligned(16)));

#define ED_MASK	((u32)~0x0f)		/* strip hw status in low addr bits */


/*
 * OHCI Transfer Descriptor (TD) ... one per transfer segment
 * See OHCI spec, sections 4.3.1 (general = control/bulk/interrupt)
 * and 4.3.2 (iso)
 */
struct td {
	/* first fields are hardware-specified */
	__hc32		hwINFO;		/* transfer info bitmask */

	/* hwINFO bits for both general and iso tds: */
#define TD_CC       0xf0000000			/* condition code */
#define TD_CC_GET(td_p) ((td_p >>28) & 0x0f)
//#define TD_CC_SET(td_p, cc) (td_p) = ((td_p) & 0x0fffffff) | (((cc) & 0x0f) << 28)
#define TD_DI       0x00E00000			/* frames before interrupt */
#define TD_DI_SET(X) (((X) & 0x07)<< 21)
	/* these two bits are available for definition/use by HCDs in both
	 * general and iso tds ... others are available for only one type
	 */
#define TD_DONE     0x00020000			/* retired to donelist */
#define TD_ISO      0x00010000			/* copy of ED_ISO */

	/* hwINFO bits for general tds: */
#define TD_EC       0x0C000000			/* error count */
#define TD_T        0x03000000			/* data toggle state */
#define TD_T_DATA0  0x02000000				/* DATA0 */
#define TD_T_DATA1  0x03000000				/* DATA1 */
#define TD_T_TOGGLE 0x00000000				/* uses ED_C */
#define TD_DP       0x00180000			/* direction/pid */
#define TD_DP_SETUP 0x00000000			/* SETUP pid */
#define TD_DP_IN    0x00100000				/* IN pid */
#define TD_DP_OUT   0x00080000				/* OUT pid */
							/* 0x00180000 rsvd */
#define TD_R        0x00040000			/* round: short packets OK? */

	/* (no hwINFO #defines yet for iso tds) */

	__hc32		hwCBP;		/* Current Buffer Pointer (or 0) */
	__hc32		hwNextTD;	/* Next TD Pointer */
	__hc32		hwBE;		/* Memory Buffer End Pointer */

	/* PSW is only for ISO.  Only 1 PSW entry is used, but on
	 * big-endian PPC hardware that's the second entry.
	 */
#define MAXPSW	2
	__hc16		hwPSW [MAXPSW];

	/* rest are purely for the driver's use */
	__u8		index;
	struct ed	*ed;
	struct td	*td_hash;	/* dma-->td hashtable */
	struct td	*next_dl_td;
	struct urb	*urb;

	dma_addr_t	td_dma;		/* addr of this TD */
	dma_addr_t	data_dma;	/* addr of data it points to */

	struct list_head td_list;	/* "shadow list", TDs on same ED */
} __attribute__ ((aligned(32)));	/* c/b/i need 16; only iso needs 32 */

#define TD_MASK	((u32)~0x1f)		/* strip hw status in low addr bits */

/*
 * Hardware transfer status codes -- CC from td->hwINFO or td->hwPSW
 */
#define TD_CC_NOERROR      0x00
#define TD_CC_CRC          0x01
#define TD_CC_BITSTUFFING  0x02
#define TD_CC_DATATOGGLEM  0x03
#define TD_CC_STALL        0x04
#define TD_DEVNOTRESP      0x05
#define TD_PIDCHECKFAIL    0x06
#define TD_UNEXPECTEDPID   0x07
#define TD_DATAOVERRUN     0x08
#define TD_DATAUNDERRUN    0x09
    /* 0x0A, 0x0B reserved for hardware */
#define TD_BUFFEROVERRUN   0x0C
#define TD_BUFFERUNDERRUN  0x0D
    /* 0x0E, 0x0F reserved for HCD */
#define TD_NOTACCESSED     0x0F


/* map OHCI TD status codes (CC) to errno values */
static const int cc_to_error [16] = {
	/* No  Error  */               0,
	/* CRC Error  */               -EILSEQ,
	/* Bit Stuff  */               -EPROTO,
	/* Data Togg  */               -EILSEQ,
	/* Stall      */               -EPIPE,
	/* DevNotResp */               -ETIME,
	/* PIDCheck   */               -EPROTO,
	/* UnExpPID   */               -EPROTO,
	/* DataOver   */               -EOVERFLOW,
	/* DataUnder  */               -EREMOTEIO,
	/* (for hw)   */               -EIO,
	/* (for hw)   */               -EIO,
	/* BufferOver */               -ECOMM,
	/* BuffUnder  */               -ENOSR,
	/* (for HCD)  */               -EALREADY,
	/* (for HCD)  */               -EALREADY
};


/*
 * The HCCA (Host Controller Communications Area) is a 256 byte
 * structure defined section 4.4.1 of the OHCI spec. The HC is
 * told the base address of it.  It must be 256-byte aligned.
 */
struct ohci_hcca {
#define NUM_INTS 32
	__hc32	int_table [NUM_INTS];	/* periodic schedule */

	/*
	 * OHCI defines u16 frame_no, followed by u16 zero pad.
	 * Since some processors can't do 16 bit bus accesses,
	 * portable access must be a 32 bits wide.
	 */
	__hc32	frame_no;		/* current frame number */
	__hc32	done_head;		/* info returned for an interrupt */
	u8	reserved_for_hc [116];
	u8	what [4];		/* spec only identifies 252 bytes :) */
} __attribute__ ((aligned(256)));

/*
 * This is the structure of the OHCI controller's memory mapped I/O region.
 * You must use readl() and writel() (in <asm/io.h>) to access these fields!!
 * Layout is in section 7 (and appendix B) of the spec.
 */
struct ohci_regs {
	/* control and status registers (section 7.1) */
	__hc32	revision;
	__hc32	control;
	__hc32	cmdstatus;
	__hc32	intrstatus;
	__hc32	intrenable;
	__hc32	intrdisable;

	/* memory pointers (section 7.2) */
	__hc32	hcca;
	__hc32	ed_periodcurrent;
	__hc32	ed_controlhead;
	__hc32	ed_controlcurrent;
	__hc32	ed_bulkhead;
	__hc32	ed_bulkcurrent;
	__hc32	donehead;

	/* frame counters (section 7.3) */
	__hc32	fminterval;
	__hc32	fmremaining;
	__hc32	fmnumber;
	__hc32	periodicstart;
	__hc32	lsthresh;

	/* Root hub ports (section 7.4) */
	struct	ohci_roothub_regs {
		__hc32	a;
		__hc32	b;
		__hc32	status;
#define MAX_ROOT_PORTS	15	/* maximum OHCI root hub ports (RH_A_NDP) */
		__hc32	portstatus [MAX_ROOT_PORTS];
	} roothub;

	/* and optional "legacy support" registers (appendix B) at 0x0100 */

} __attribute__ ((aligned(32)));


/* OHCI CONTROL AND STATUS REGISTER MASKS */

/*
 * HcControl (control) register masks
 */
#define OHCI_CTRL_CBSR	(3 << 0)	/* control/bulk service ratio */
#define OHCI_CTRL_PLE	(1 << 2)	/* periodic list enable */
#define OHCI_CTRL_IE	(1 << 3)	/* isochronous enable */
#define OHCI_CTRL_CLE	(1 << 4)	/* control list enable */
#define OHCI_CTRL_BLE	(1 << 5)	/* bulk list enable */
#define OHCI_CTRL_HCFS	(3 << 6)	/* host controller functional state */
#define OHCI_CTRL_IR	(1 << 8)	/* interrupt routing */
#define OHCI_CTRL_RWC	(1 << 9)	/* remote wakeup connected */
#define OHCI_CTRL_RWE	(1 << 10)	/* remote wakeup enable */

/* pre-shifted values for HCFS */
#	define OHCI_USB_RESET	(0 << 6)
#	define OHCI_USB_RESUME	(1 << 6)
#	define OHCI_USB_OPER	(2 << 6)
#	define OHCI_USB_SUSPEND	(3 << 6)

/*
 * HcCommandStatus (cmdstatus) register masks
 */
#define OHCI_HCR	(1 << 0)	/* host controller reset */
#define OHCI_CLF	(1 << 1)	/* control list filled */
#define OHCI_BLF	(1 << 2)	/* bulk list filled */
#define OHCI_OCR	(1 << 3)	/* ownership change request */
#define OHCI_SOC	(3 << 16)	/* scheduling overrun count */

/*
 * masks used with interrupt registers:
 * HcInterruptStatus (intrstatus)
 * HcInterruptEnable (intrenable)
 * HcInterruptDisable (intrdisable)
 */
#define OHCI_INTR_SO	(1 << 0)	/* scheduling overrun */
#define OHCI_INTR_WDH	(1 << 1)	/* writeback of done_head */
#define OHCI_INTR_SF	(1 << 2)	/* start frame */
#define OHCI_INTR_RD	(1 << 3)	/* resume detect */
#define OHCI_INTR_UE	(1 << 4)	/* unrecoverable error */
#define OHCI_INTR_FNO	(1 << 5)	/* frame number overflow */
#define OHCI_INTR_RHSC	(1 << 6)	/* root hub status change */
#define OHCI_INTR_OC	(1 << 30)	/* ownership change */
#define OHCI_INTR_MIE	(1 << 31)	/* master interrupt enable */


/* OHCI ROOT HUB REGISTER MASKS */

/* roothub.portstatus [i] bits */
#define RH_PS_CCS            0x00000001		/* current connect status */
#define RH_PS_PES            0x00000002		/* port enable status*/
#define RH_PS_PSS            0x00000004		/* port suspend status */
#define RH_PS_POCI           0x00000008		/* port over current indicator */
#define RH_PS_PRS            0x00000010		/* port reset status */
#define RH_PS_PPS            0x00000100		/* port power status */
#define RH_PS_LSDA           0x00000200		/* low speed device attached */
#define RH_PS_CSC            0x00010000		/* connect status change */
#define RH_PS_PESC           0x00020000		/* port enable status change */
#define RH_PS_PSSC           0x00040000		/* port suspend status change */
#define RH_PS_OCIC           0x00080000		/* over current indicator change */
#define RH_PS_PRSC           0x00100000		/* port reset status change */

/* roothub.status bits */
#define RH_HS_LPS	     0x00000001		/* local power status */
#define RH_HS_OCI	     0x00000002		/* over current indicator */
#define RH_HS_DRWE	     0x00008000		/* device remote wakeup enable */
#define RH_HS_LPSC	     0x00010000		/* local power status change */
#define RH_HS_OCIC	     0x00020000		/* over current indicator change */
#define RH_HS_CRWE	     0x80000000		/* clear remote wakeup enable */

/* roothub.b masks */
#define RH_B_DR		0x0000ffff		/* device removable flags */
#define RH_B_PPCM	0xffff0000		/* port power control mask */

/* roothub.a masks */
#define	RH_A_NDP	(0xff << 0)		/* number of downstream ports */
#define	RH_A_PSM	(1 << 8)		/* power switching mode */
#define	RH_A_NPS	(1 << 9)		/* no power switching */
#define	RH_A_DT		(1 << 10)		/* device type (mbz) */
#define	RH_A_OCPM	(1 << 11)		/* over current protection mode */
#define	RH_A_NOCP	(1 << 12)		/* no over current protection */
#define	RH_A_POTPGT	(0xff << 24)		/* power on to power good time */


/* hcd-private per-urb state */
typedef struct urb_priv {
	struct ed		*ed;
	u16			length;		// # tds in this request
	u16			td_cnt;		// tds already serviced
	struct list_head	pending;
	struct td		*td [0];	// all TDs in this request

} urb_priv_t;

#define TD_HASH_SIZE    64    /* power'o'two */
// sizeof (struct td) ~= 64 == 2^6 ...
#define TD_HASH_FUNC(td_dma) ((td_dma ^ (td_dma >> 6)) % TD_HASH_SIZE)


/*
 * This is the full ohci controller description
 *
 * Note how the "proper" USB information is just
 * a subset of what the full implementation needs. (Linus)
 */

enum ohci_rh_state {
	OHCI_RH_HALTED,
	OHCI_RH_SUSPENDED,
	OHCI_RH_RUNNING
};

struct ohci_hcd {
	spinlock_t		lock;

	/*
	 * I/O memory used to communicate with the HC (dma-consistent)
	 */
	struct ohci_regs __iomem *regs;

	/*
	 * main memory used to communicate with the HC (dma-consistent).
	 * hcd adds to schedule for a live hc any time, but removals finish
	 * only at the start of the next frame.
	 */
	struct ohci_hcca	*hcca;
	dma_addr_t		hcca_dma;

	struct ed		*ed_rm_list;		/* to be removed */

	struct ed		*ed_bulktail;		/* last in bulk list */
	struct ed		*ed_controltail;	/* last in ctrl list */
	struct ed		*periodic [NUM_INTS];	/* shadow int_table */

	void (*start_hnp)(struct ohci_hcd *ohci);

	/*
	 * memory management for queue data structures
	 */
	struct dma_pool		*td_cache;
	struct dma_pool		*ed_cache;
	struct td		*td_hash [TD_HASH_SIZE];
	struct list_head	pending;

	/*
	 * driver state
	 */
	enum ohci_rh_state	rh_state;
	int			num_ports;
	int			load [NUM_INTS];
	u32			hc_control;	/* copy of hc control reg */
	unsigned long		next_statechange;	/* suspend/resume */
	u32			fminterval;		/* saved register */
	unsigned		autostop:1;	/* rh auto stopping/stopped */

	unsigned long		flags;		/* for HC bugs */
#define	OHCI_QUIRK_AMD756	0x01			/* erratum #4 */
#define	OHCI_QUIRK_SUPERIO	0x02			/* natsemi */
#define	OHCI_QUIRK_INITRESET	0x04			/* SiS, OPTi, ... */
#define	OHCI_QUIRK_BE_DESC	0x08			/* BE descriptors */
#define	OHCI_QUIRK_BE_MMIO	0x10			/* BE registers */
#define	OHCI_QUIRK_ZFMICRO	0x20			/* Compaq ZFMicro chipset*/
#define	OHCI_QUIRK_NEC		0x40			/* lost interrupts */
#define	OHCI_QUIRK_FRAME_NO	0x80			/* no big endian frame_no shift */
#define	OHCI_QUIRK_HUB_POWER	0x100			/* distrust firmware power/oc setup */
#define	OHCI_QUIRK_AMD_PLL	0x200			/* AMD PLL quirk*/
#define	OHCI_QUIRK_AMD_PREFETCH	0x400			/* pre-fetch for ISO transfer */
#define	OHCI_QUIRK_GLOBAL_SUSPEND	0x800		/* must suspend ports */

	// there are also chip quirks/bugs in init logic

	struct work_struct	nec_work;	/* Worker for NEC quirk */

	/* Needed for ZF Micro quirk */
	struct timer_list	unlink_watchdog;
	unsigned		eds_scheduled;
	struct ed		*ed_to_check;
	unsigned		zf_delay;

	struct dentry		*debug_dir;
	struct dentry		*debug_async;
	struct dentry		*debug_periodic;
	struct dentry		*debug_registers;

	/* platform-specific data -- must come last */
	unsigned long           priv[0] __aligned(sizeof(s64));

};

#ifdef CONFIG_PCI
static inline int quirk_nec(struct ohci_hcd *ohci)
{
	return ohci->flags & OHCI_QUIRK_NEC;
}
static inline int quirk_zfmicro(struct ohci_hcd *ohci)
{
	return ohci->flags & OHCI_QUIRK_ZFMICRO;
}
static inline int quirk_amdiso(struct ohci_hcd *ohci)
{
	return ohci->flags & OHCI_QUIRK_AMD_PLL;
}
static inline int quirk_amdprefetch(struct ohci_hcd *ohci)
{
	return ohci->flags & OHCI_QUIRK_AMD_PREFETCH;
}
#else
static inline int quirk_nec(struct ohci_hcd *ohci)
{
	return 0;
}
static inline int quirk_zfmicro(struct ohci_hcd *ohci)
{
	return 0;
}
static inline int quirk_amdiso(struct ohci_hcd *ohci)
{
	return 0;
}
static inline int quirk_amdprefetch(struct ohci_hcd *ohci)
{
	return 0;
}
#endif

/* convert between an hcd pointer and the corresponding ohci_hcd */
static inline struct ohci_hcd *hcd_to_ohci (struct usb_hcd *hcd)
{
	return (struct ohci_hcd *) (hcd->hcd_priv);
}
static inline struct usb_hcd *ohci_to_hcd (const struct ohci_hcd *ohci)
{
	return container_of ((void *) ohci, struct usb_hcd, hcd_priv);
}

/*-------------------------------------------------------------------------*/

#define ohci_dbg(ohci, fmt, args...) \
	dev_dbg (ohci_to_hcd(ohci)->self.controller , fmt , ## args )
#define ohci_err(ohci, fmt, args...) \
	dev_err (ohci_to_hcd(ohci)->self.controller , fmt , ## args )
#define ohci_info(ohci, fmt, args...) \
	dev_info (ohci_to_hcd(ohci)->self.controller , fmt , ## args )
#define ohci_warn(ohci, fmt, args...) \
	dev_warn (ohci_to_hcd(ohci)->self.controller , fmt , ## args )

/*-------------------------------------------------------------------------*/

/*
 * While most USB host controllers implement their registers and
 * in-memory communication descriptors in little-endian format,
 * a minority (notably the IBM STB04XXX and the Motorola MPC5200
 * processors) implement them in big endian format.
 *
 * In addition some more exotic implementations like the Toshiba
 * Spider (aka SCC) cell southbridge are "mixed" endian, that is,
 * they have a different endianness for registers vs. in-memory
 * descriptors.
 *
 * This attempts to support either format at compile time without a
 * runtime penalty, or both formats with the additional overhead
 * of checking a flag bit.
 *
 * That leads to some tricky Kconfig rules howevber. There are
 * different defaults based on some arch/ppc platforms, though
 * the basic rules are:
 *
 * Controller type              Kconfig options needed
 * ---------------              ----------------------
 * little endian                CONFIG_USB_OHCI_LITTLE_ENDIAN
 *
 * fully big endian             CONFIG_USB_OHCI_BIG_ENDIAN_DESC _and_
 *                              CONFIG_USB_OHCI_BIG_ENDIAN_MMIO
 *
 * mixed endian                 CONFIG_USB_OHCI_LITTLE_ENDIAN _and_
 *                              CONFIG_USB_OHCI_BIG_ENDIAN_{MMIO,DESC}
 *
 * (If you have a mixed endian controller, you -must- also define
 * CONFIG_USB_OHCI_LITTLE_ENDIAN or things will not work when building
 * both your mixed endian and a fully big endian controller support in
 * the same kernel image).
 */

#ifdef CONFIG_USB_OHCI_BIG_ENDIAN_DESC
#ifdef CONFIG_USB_OHCI_LITTLE_ENDIAN
#define big_endian_desc(ohci)	(ohci->flags & OHCI_QUIRK_BE_DESC)
#else
#define big_endian_desc(ohci)	1		/* only big endian */
#endif
#else
#define big_endian_desc(ohci)	0		/* only little endian */
#endif

#ifdef CONFIG_USB_OHCI_BIG_ENDIAN_MMIO
#ifdef CONFIG_USB_OHCI_LITTLE_ENDIAN
#define big_endian_mmio(ohci)	(ohci->flags & OHCI_QUIRK_BE_MMIO)
#else
#define big_endian_mmio(ohci)	1		/* only big endian */
#endif
#else
#define big_endian_mmio(ohci)	0		/* only little endian */
#endif

/*
 * Big-endian read/write functions are arch-specific.
 * Other arches can be added if/when they're needed.
 *
 */
static inline unsigned int _ohci_readl (const struct ohci_hcd *ohci,
					__hc32 __iomem * regs)
{
#ifdef CONFIG_USB_OHCI_BIG_ENDIAN_MMIO
	return big_endian_mmio(ohci) ?
		readl_be (regs) :
		readl (regs);
#else
	return readl (regs);
#endif
}

static inline void _ohci_writel (const struct ohci_hcd *ohci,
				 const unsigned int val, __hc32 __iomem *regs)
{
#ifdef CONFIG_USB_OHCI_BIG_ENDIAN_MMIO
	big_endian_mmio(ohci) ?
		writel_be (val, regs) :
		writel (val, regs);
#else
		writel (val, regs);
#endif
}

#define ohci_readl(o,r)		_ohci_readl(o,r)
#define ohci_writel(o,v,r)	_ohci_writel(o,v,r)


/*-------------------------------------------------------------------------*/

/* cpu to ohci */
static inline __hc16 cpu_to_hc16 (const struct ohci_hcd *ohci, const u16 x)
{
	return big_endian_desc(ohci) ?
		(__force __hc16)cpu_to_be16(x) :
		(__force __hc16)cpu_to_le16(x);
}

static inline __hc16 cpu_to_hc16p (const struct ohci_hcd *ohci, const u16 *x)
{
	return big_endian_desc(ohci) ?
		cpu_to_be16p(x) :
		cpu_to_le16p(x);
}

static inline __hc32 cpu_to_hc32 (const struct ohci_hcd *ohci, const u32 x)
{
	return big_endian_desc(ohci) ?
		(__force __hc32)cpu_to_be32(x) :
		(__force __hc32)cpu_to_le32(x);
}

static inline __hc32 cpu_to_hc32p (const struct ohci_hcd *ohci, const u32 *x)
{
	return big_endian_desc(ohci) ?
		cpu_to_be32p(x) :
		cpu_to_le32p(x);
}

/* ohci to cpu */
static inline u16 hc16_to_cpu (const struct ohci_hcd *ohci, const __hc16 x)
{
	return big_endian_desc(ohci) ?
		be16_to_cpu((__force __be16)x) :
		le16_to_cpu((__force __le16)x);
}

static inline u16 hc16_to_cpup (const struct ohci_hcd *ohci, const __hc16 *x)
{
	return big_endian_desc(ohci) ?
		be16_to_cpup((__force __be16 *)x) :
		le16_to_cpup((__force __le16 *)x);
}

static inline u32 hc32_to_cpu (const struct ohci_hcd *ohci, const __hc32 x)
{
	return big_endian_desc(ohci) ?
		be32_to_cpu((__force __be32)x) :
		le32_to_cpu((__force __le32)x);
}

static inline u32 hc32_to_cpup (const struct ohci_hcd *ohci, const __hc32 *x)
{
	return big_endian_desc(ohci) ?
		be32_to_cpup((__force __be32 *)x) :
		le32_to_cpup((__force __le32 *)x);
}

/*-------------------------------------------------------------------------*/

/* HCCA frame number is 16 bits, but is accessed as 32 bits since not all
 * hardware handles 16 bit reads.  That creates a different confusion on
 * some big-endian SOC implementations.  Same thing happens with PSW access.
 */

#ifdef CONFIG_PPC_MPC52xx
#define big_endian_frame_no_quirk(ohci)	(ohci->flags & OHCI_QUIRK_FRAME_NO)
#else
#define big_endian_frame_no_quirk(ohci)	0
#endif

static inline u16 ohci_frame_no(const struct ohci_hcd *ohci)
{
	u32 tmp;
	if (big_endian_desc(ohci)) {
		tmp = be32_to_cpup((__force __be32 *)&ohci->hcca->frame_no);
		if (!big_endian_frame_no_quirk(ohci))
			tmp >>= 16;
	} else
		tmp = le32_to_cpup((__force __le32 *)&ohci->hcca->frame_no);

	return (u16)tmp;
}

static inline __hc16 *ohci_hwPSWp(const struct ohci_hcd *ohci,
                                 const struct td *td, int index)
{
	return (__hc16 *)(big_endian_desc(ohci) ?
			&td->hwPSW[index ^ 1] : &td->hwPSW[index]);
}

static inline u16 ohci_hwPSW(const struct ohci_hcd *ohci,
                               const struct td *td, int index)
{
	return hc16_to_cpup(ohci, ohci_hwPSWp(ohci, td, index));
}

/*-------------------------------------------------------------------------*/

#define	FI			0x2edf		/* 12000 bits per frame (-1) */
#define	FSMP(fi)		(0x7fff & ((6 * ((fi) - 210)) / 7))
#define	FIT			(1 << 31)
#define LSTHRESH		0x628		/* lowspeed bit threshold */

static inline void periodic_reinit (struct ohci_hcd *ohci)
{
	u32	fi = ohci->fminterval & 0x03fff;
	u32	fit = ohci_readl(ohci, &ohci->regs->fminterval) & FIT;

	ohci_writel (ohci, (fit ^ FIT) | ohci->fminterval,
						&ohci->regs->fminterval);
	ohci_writel (ohci, ((9 * fi) / 10) & 0x3fff,
						&ohci->regs->periodicstart);
}

/* AMD-756 (D2 rev) reports corrupt register contents in some cases.
 * The erratum (#4) description is incorrect.  AMD's workaround waits
 * till some bits (mostly reserved) are clear; ok for all revs.
 */
#define read_roothub(hc, register, mask) ({ \
	u32 temp = ohci_readl (hc, &hc->regs->roothub.register); \
	if (temp == -1) \
		hc->rh_state = OHCI_RH_HALTED; \
	else if (hc->flags & OHCI_QUIRK_AMD756) \
		while (temp & mask) \
			temp = ohci_readl (hc, &hc->regs->roothub.register); \
	temp; })

static inline u32 roothub_a (struct ohci_hcd *hc)
	{ return read_roothub (hc, a, 0xfc0fe000); }
static inline u32 roothub_b (struct ohci_hcd *hc)
	{ return ohci_readl (hc, &hc->regs->roothub.b); }
static inline u32 roothub_status (struct ohci_hcd *hc)
	{ return ohci_readl (hc, &hc->regs->roothub.status); }
static inline u32 roothub_portstatus (struct ohci_hcd *hc, int i)
	{ return read_roothub (hc, portstatus [i], 0xffe0fce0); }

/* Declarations of things exported for use by ohci platform drivers */

struct ohci_driver_overrides {
	const char	*product_desc;
	size_t		extra_priv_size;
	int		(*reset)(struct usb_hcd *hcd);
};

extern void	ohci_init_driver(struct hc_driver *drv,
				const struct ohci_driver_overrides *over);
extern int	ohci_restart(struct ohci_hcd *ohci);
extern int	ohci_setup(struct usb_hcd *hcd);
#ifdef CONFIG_PM
extern int	ohci_suspend(struct usb_hcd *hcd, bool do_wakeup);
extern int	ohci_resume(struct usb_hcd *hcd, bool hibernated);
#endif
