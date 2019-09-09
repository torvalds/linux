/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_UHCI_HCD_H
#define __LINUX_UHCI_HCD_H

#include <linux/list.h>
#include <linux/usb.h>
#include <linux/clk.h>

#define usb_packetid(pipe)	(usb_pipein(pipe) ? USB_PID_IN : USB_PID_OUT)
#define PIPE_DEVEP_MASK		0x0007ff00


/*
 * Universal Host Controller Interface data structures and defines
 */

/* Command register */
#define USBCMD		0
#define   USBCMD_RS		0x0001	/* Run/Stop */
#define   USBCMD_HCRESET	0x0002	/* Host reset */
#define   USBCMD_GRESET		0x0004	/* Global reset */
#define   USBCMD_EGSM		0x0008	/* Global Suspend Mode */
#define   USBCMD_FGR		0x0010	/* Force Global Resume */
#define   USBCMD_SWDBG		0x0020	/* SW Debug mode */
#define   USBCMD_CF		0x0040	/* Config Flag (sw only) */
#define   USBCMD_MAXP		0x0080	/* Max Packet (0 = 32, 1 = 64) */

/* Status register */
#define USBSTS		2
#define   USBSTS_USBINT		0x0001	/* Interrupt due to IOC */
#define   USBSTS_ERROR		0x0002	/* Interrupt due to error */
#define   USBSTS_RD		0x0004	/* Resume Detect */
#define   USBSTS_HSE		0x0008	/* Host System Error: PCI problems */
#define   USBSTS_HCPE		0x0010	/* Host Controller Process Error:
					 * the schedule is buggy */
#define   USBSTS_HCH		0x0020	/* HC Halted */

/* Interrupt enable register */
#define USBINTR		4
#define   USBINTR_TIMEOUT	0x0001	/* Timeout/CRC error enable */
#define   USBINTR_RESUME	0x0002	/* Resume interrupt enable */
#define   USBINTR_IOC		0x0004	/* Interrupt On Complete enable */
#define   USBINTR_SP		0x0008	/* Short packet interrupt enable */

#define USBFRNUM	6
#define USBFLBASEADD	8
#define USBSOF		12
#define   USBSOF_DEFAULT	64	/* Frame length is exactly 1 ms */

/* USB port status and control registers */
#define USBPORTSC1	16
#define USBPORTSC2	18
#define USBPORTSC3	20
#define USBPORTSC4	22
#define   USBPORTSC_CCS		0x0001	/* Current Connect Status
					 * ("device present") */
#define   USBPORTSC_CSC		0x0002	/* Connect Status Change */
#define   USBPORTSC_PE		0x0004	/* Port Enable */
#define   USBPORTSC_PEC		0x0008	/* Port Enable Change */
#define   USBPORTSC_DPLUS	0x0010	/* D+ high (line status) */
#define   USBPORTSC_DMINUS	0x0020	/* D- high (line status) */
#define   USBPORTSC_RD		0x0040	/* Resume Detect */
#define   USBPORTSC_RES1	0x0080	/* reserved, always 1 */
#define   USBPORTSC_LSDA	0x0100	/* Low Speed Device Attached */
#define   USBPORTSC_PR		0x0200	/* Port Reset */
/* OC and OCC from Intel 430TX and later (not UHCI 1.1d spec) */
#define   USBPORTSC_OC		0x0400	/* Over Current condition */
#define   USBPORTSC_OCC		0x0800	/* Over Current Change R/WC */
#define   USBPORTSC_SUSP	0x1000	/* Suspend */
#define   USBPORTSC_RES2	0x2000	/* reserved, write zeroes */
#define   USBPORTSC_RES3	0x4000	/* reserved, write zeroes */
#define   USBPORTSC_RES4	0x8000	/* reserved, write zeroes */

/* PCI legacy support register */
#define USBLEGSUP		0xc0
#define   USBLEGSUP_DEFAULT	0x2000	/* only PIRQ enable set */
#define   USBLEGSUP_RWC		0x8f00	/* the R/WC bits */
#define   USBLEGSUP_RO		0x5040	/* R/O and reserved bits */

/* PCI Intel-specific resume-enable register */
#define USBRES_INTEL		0xc4
#define   USBPORT1EN		0x01
#define   USBPORT2EN		0x02

#define UHCI_PTR_BITS(uhci)	cpu_to_hc32((uhci), 0x000F)
#define UHCI_PTR_TERM(uhci)	cpu_to_hc32((uhci), 0x0001)
#define UHCI_PTR_QH(uhci)	cpu_to_hc32((uhci), 0x0002)
#define UHCI_PTR_DEPTH(uhci)	cpu_to_hc32((uhci), 0x0004)
#define UHCI_PTR_BREADTH(uhci)	cpu_to_hc32((uhci), 0x0000)

#define UHCI_NUMFRAMES		1024	/* in the frame list [array] */
#define UHCI_MAX_SOF_NUMBER	2047	/* in an SOF packet */
#define CAN_SCHEDULE_FRAMES	1000	/* how far in the future frames
					 * can be scheduled */
#define MAX_PHASE		32	/* Periodic scheduling length */

/* When no queues need Full-Speed Bandwidth Reclamation,
 * delay this long before turning FSBR off */
#define FSBR_OFF_DELAY		msecs_to_jiffies(10)

/* If a queue hasn't advanced after this much time, assume it is stuck */
#define QH_WAIT_TIMEOUT		msecs_to_jiffies(200)


/*
 * __hc32 and __hc16 are "Host Controller" types, they may be equivalent to
 * __leXX (normally) or __beXX (given UHCI_BIG_ENDIAN_DESC), depending on
 * the host controller implementation.
 *
 * To facilitate the strongest possible byte-order checking from "sparse"
 * and so on, we use __leXX unless that's not practical.
 */
#ifdef CONFIG_USB_UHCI_BIG_ENDIAN_DESC
typedef __u32 __bitwise __hc32;
typedef __u16 __bitwise __hc16;
#else
#define __hc32	__le32
#define __hc16	__le16
#endif

/*
 *	Queue Headers
 */

/*
 * One role of a QH is to hold a queue of TDs for some endpoint.  One QH goes
 * with each endpoint, and qh->element (updated by the HC) is either:
 *   - the next unprocessed TD in the endpoint's queue, or
 *   - UHCI_PTR_TERM (when there's no more traffic for this endpoint).
 *
 * The other role of a QH is to serve as a "skeleton" framelist entry, so we
 * can easily splice a QH for some endpoint into the schedule at the right
 * place.  Then qh->element is UHCI_PTR_TERM.
 *
 * In the schedule, qh->link maintains a list of QHs seen by the HC:
 *     skel1 --> ep1-qh --> ep2-qh --> ... --> skel2 --> ...
 *
 * qh->node is the software equivalent of qh->link.  The differences
 * are that the software list is doubly-linked and QHs in the UNLINKING
 * state are on the software list but not the hardware schedule.
 *
 * For bookkeeping purposes we maintain QHs even for Isochronous endpoints,
 * but they never get added to the hardware schedule.
 */
#define QH_STATE_IDLE		1	/* QH is not being used */
#define QH_STATE_UNLINKING	2	/* QH has been removed from the
					 * schedule but the hardware may
					 * still be using it */
#define QH_STATE_ACTIVE		3	/* QH is on the schedule */

struct uhci_qh {
	/* Hardware fields */
	__hc32 link;			/* Next QH in the schedule */
	__hc32 element;			/* Queue element (TD) pointer */

	/* Software fields */
	dma_addr_t dma_handle;

	struct list_head node;		/* Node in the list of QHs */
	struct usb_host_endpoint *hep;	/* Endpoint information */
	struct usb_device *udev;
	struct list_head queue;		/* Queue of urbps for this QH */
	struct uhci_td *dummy_td;	/* Dummy TD to end the queue */
	struct uhci_td *post_td;	/* Last TD completed */

	struct usb_iso_packet_descriptor *iso_packet_desc;
					/* Next urb->iso_frame_desc entry */
	unsigned long advance_jiffies;	/* Time of last queue advance */
	unsigned int unlink_frame;	/* When the QH was unlinked */
	unsigned int period;		/* For Interrupt and Isochronous QHs */
	short phase;			/* Between 0 and period-1 */
	short load;			/* Periodic time requirement, in us */
	unsigned int iso_frame;		/* Frame # for iso_packet_desc */

	int state;			/* QH_STATE_xxx; see above */
	int type;			/* Queue type (control, bulk, etc) */
	int skel;			/* Skeleton queue number */

	unsigned int initial_toggle:1;	/* Endpoint's current toggle value */
	unsigned int needs_fixup:1;	/* Must fix the TD toggle values */
	unsigned int is_stopped:1;	/* Queue was stopped by error/unlink */
	unsigned int wait_expired:1;	/* QH_WAIT_TIMEOUT has expired */
	unsigned int bandwidth_reserved:1;	/* Periodic bandwidth has
						 * been allocated */
} __attribute__((aligned(16)));

/*
 * We need a special accessor for the element pointer because it is
 * subject to asynchronous updates by the controller.
 */
#define qh_element(qh)		READ_ONCE((qh)->element)

#define LINK_TO_QH(uhci, qh)	(UHCI_PTR_QH((uhci)) | \
				cpu_to_hc32((uhci), (qh)->dma_handle))


/*
 *	Transfer Descriptors
 */

/*
 * for TD <status>:
 */
#define TD_CTRL_SPD		(1 << 29)	/* Short Packet Detect */
#define TD_CTRL_C_ERR_MASK	(3 << 27)	/* Error Counter bits */
#define TD_CTRL_C_ERR_SHIFT	27
#define TD_CTRL_LS		(1 << 26)	/* Low Speed Device */
#define TD_CTRL_IOS		(1 << 25)	/* Isochronous Select */
#define TD_CTRL_IOC		(1 << 24)	/* Interrupt on Complete */
#define TD_CTRL_ACTIVE		(1 << 23)	/* TD Active */
#define TD_CTRL_STALLED		(1 << 22)	/* TD Stalled */
#define TD_CTRL_DBUFERR		(1 << 21)	/* Data Buffer Error */
#define TD_CTRL_BABBLE		(1 << 20)	/* Babble Detected */
#define TD_CTRL_NAK		(1 << 19)	/* NAK Received */
#define TD_CTRL_CRCTIMEO	(1 << 18)	/* CRC/Time Out Error */
#define TD_CTRL_BITSTUFF	(1 << 17)	/* Bit Stuff Error */
#define TD_CTRL_ACTLEN_MASK	0x7FF	/* actual length, encoded as n - 1 */

#define uhci_maxerr(err)		((err) << TD_CTRL_C_ERR_SHIFT)
#define uhci_status_bits(ctrl_sts)	((ctrl_sts) & 0xF60000)
#define uhci_actual_length(ctrl_sts)	(((ctrl_sts) + 1) & \
			TD_CTRL_ACTLEN_MASK)	/* 1-based */

/*
 * for TD <info>: (a.k.a. Token)
 */
#define td_token(uhci, td)	hc32_to_cpu((uhci), (td)->token)
#define TD_TOKEN_DEVADDR_SHIFT	8
#define TD_TOKEN_TOGGLE_SHIFT	19
#define TD_TOKEN_TOGGLE		(1 << 19)
#define TD_TOKEN_EXPLEN_SHIFT	21
#define TD_TOKEN_EXPLEN_MASK	0x7FF	/* expected length, encoded as n-1 */
#define TD_TOKEN_PID_MASK	0xFF

#define uhci_explen(len)	((((len) - 1) & TD_TOKEN_EXPLEN_MASK) << \
					TD_TOKEN_EXPLEN_SHIFT)

#define uhci_expected_length(token) ((((token) >> TD_TOKEN_EXPLEN_SHIFT) + \
					1) & TD_TOKEN_EXPLEN_MASK)
#define uhci_toggle(token)	(((token) >> TD_TOKEN_TOGGLE_SHIFT) & 1)
#define uhci_endpoint(token)	(((token) >> 15) & 0xf)
#define uhci_devaddr(token)	(((token) >> TD_TOKEN_DEVADDR_SHIFT) & 0x7f)
#define uhci_devep(token)	(((token) >> TD_TOKEN_DEVADDR_SHIFT) & 0x7ff)
#define uhci_packetid(token)	((token) & TD_TOKEN_PID_MASK)
#define uhci_packetout(token)	(uhci_packetid(token) != USB_PID_IN)
#define uhci_packetin(token)	(uhci_packetid(token) == USB_PID_IN)

/*
 * The documentation says "4 words for hardware, 4 words for software".
 *
 * That's silly, the hardware doesn't care. The hardware only cares that
 * the hardware words are 16-byte aligned, and we can have any amount of
 * sw space after the TD entry.
 *
 * td->link points to either another TD (not necessarily for the same urb or
 * even the same endpoint), or nothing (PTR_TERM), or a QH.
 */
struct uhci_td {
	/* Hardware fields */
	__hc32 link;
	__hc32 status;
	__hc32 token;
	__hc32 buffer;

	/* Software fields */
	dma_addr_t dma_handle;

	struct list_head list;

	int frame;			/* for iso: what frame? */
	struct list_head fl_list;
} __attribute__((aligned(16)));

/*
 * We need a special accessor for the control/status word because it is
 * subject to asynchronous updates by the controller.
 */
#define td_status(uhci, td)		hc32_to_cpu((uhci), \
						READ_ONCE((td)->status))

#define LINK_TO_TD(uhci, td)		(cpu_to_hc32((uhci), (td)->dma_handle))


/*
 *	Skeleton Queue Headers
 */

/*
 * The UHCI driver uses QHs with Interrupt, Control and Bulk URBs for
 * automatic queuing. To make it easy to insert entries into the schedule,
 * we have a skeleton of QHs for each predefined Interrupt latency.
 * Asynchronous QHs (low-speed control, full-speed control, and bulk)
 * go onto the period-1 interrupt list, since they all get accessed on
 * every frame.
 *
 * When we want to add a new QH, we add it to the list starting from the
 * appropriate skeleton QH.  For instance, the schedule can look like this:
 *
 * skel int128 QH
 * dev 1 interrupt QH
 * dev 5 interrupt QH
 * skel int64 QH
 * skel int32 QH
 * ...
 * skel int1 + async QH
 * dev 5 low-speed control QH
 * dev 1 bulk QH
 * dev 2 bulk QH
 *
 * There is a special terminating QH used to keep full-speed bandwidth
 * reclamation active when no full-speed control or bulk QHs are linked
 * into the schedule.  It has an inactive TD (to work around a PIIX bug,
 * see the Intel errata) and it points back to itself.
 *
 * There's a special skeleton QH for Isochronous QHs which never appears
 * on the schedule.  Isochronous TDs go on the schedule before the
 * the skeleton QHs.  The hardware accesses them directly rather than
 * through their QH, which is used only for bookkeeping purposes.
 * While the UHCI spec doesn't forbid the use of QHs for Isochronous,
 * it doesn't use them either.  And the spec says that queues never
 * advance on an error completion status, which makes them totally
 * unsuitable for Isochronous transfers.
 *
 * There's also a special skeleton QH used for QHs which are in the process
 * of unlinking and so may still be in use by the hardware.  It too never
 * appears on the schedule.
 */

#define UHCI_NUM_SKELQH		11
#define SKEL_UNLINK		0
#define skel_unlink_qh		skelqh[SKEL_UNLINK]
#define SKEL_ISO		1
#define skel_iso_qh		skelqh[SKEL_ISO]
	/* int128, int64, ..., int1 = 2, 3, ..., 9 */
#define SKEL_INDEX(exponent)	(9 - exponent)
#define SKEL_ASYNC		9
#define skel_async_qh		skelqh[SKEL_ASYNC]
#define SKEL_TERM		10
#define skel_term_qh		skelqh[SKEL_TERM]

/* The following entries refer to sublists of skel_async_qh */
#define SKEL_LS_CONTROL		20
#define SKEL_FS_CONTROL		21
#define SKEL_FSBR		SKEL_FS_CONTROL
#define SKEL_BULK		22

/*
 *	The UHCI controller and root hub
 */

/*
 * States for the root hub:
 *
 * To prevent "bouncing" in the presence of electrical noise,
 * when there are no devices attached we delay for 1 second in the
 * RUNNING_NODEVS state before switching to the AUTO_STOPPED state.
 * 
 * (Note that the AUTO_STOPPED state won't be necessary once the hub
 * driver learns to autosuspend.)
 */
enum uhci_rh_state {
	/* In the following states the HC must be halted.
	 * These two must come first. */
	UHCI_RH_RESET,
	UHCI_RH_SUSPENDED,

	UHCI_RH_AUTO_STOPPED,
	UHCI_RH_RESUMING,

	/* In this state the HC changes from running to halted,
	 * so it can legally appear either way. */
	UHCI_RH_SUSPENDING,

	/* In the following states it's an error if the HC is halted.
	 * These two must come last. */
	UHCI_RH_RUNNING,		/* The normal state */
	UHCI_RH_RUNNING_NODEVS,		/* Running with no devices attached */
};

/*
 * The full UHCI controller information:
 */
struct uhci_hcd {

	/* debugfs */
	struct dentry *dentry;

	/* Grabbed from PCI */
	unsigned long io_addr;

	/* Used when registers are memory mapped */
	void __iomem *regs;

	struct dma_pool *qh_pool;
	struct dma_pool *td_pool;

	struct uhci_td *term_td;	/* Terminating TD, see UHCI bug */
	struct uhci_qh *skelqh[UHCI_NUM_SKELQH];	/* Skeleton QHs */
	struct uhci_qh *next_qh;	/* Next QH to scan */

	spinlock_t lock;

	dma_addr_t frame_dma_handle;	/* Hardware frame list */
	__hc32 *frame;
	void **frame_cpu;		/* CPU's frame list */

	enum uhci_rh_state rh_state;
	unsigned long auto_stop_time;		/* When to AUTO_STOP */

	unsigned int frame_number;		/* As of last check */
	unsigned int is_stopped;
#define UHCI_IS_STOPPED		9999		/* Larger than a frame # */
	unsigned int last_iso_frame;		/* Frame of last scan */
	unsigned int cur_iso_frame;		/* Frame for current scan */

	unsigned int scan_in_progress:1;	/* Schedule scan is running */
	unsigned int need_rescan:1;		/* Redo the schedule scan */
	unsigned int dead:1;			/* Controller has died */
	unsigned int RD_enable:1;		/* Suspended root hub with
						   Resume-Detect interrupts
						   enabled */
	unsigned int is_initialized:1;		/* Data structure is usable */
	unsigned int fsbr_is_on:1;		/* FSBR is turned on */
	unsigned int fsbr_is_wanted:1;		/* Does any URB want FSBR? */
	unsigned int fsbr_expiring:1;		/* FSBR is timing out */

	struct timer_list fsbr_timer;		/* For turning off FBSR */

	/* Silicon quirks */
	unsigned int oc_low:1;			/* OverCurrent bit active low */
	unsigned int wait_for_hp:1;		/* Wait for HP port reset */
	unsigned int big_endian_mmio:1;		/* Big endian registers */
	unsigned int big_endian_desc:1;		/* Big endian descriptors */
	unsigned int is_aspeed:1;		/* Aspeed impl. workarounds */

	/* Support for port suspend/resume/reset */
	unsigned long port_c_suspend;		/* Bit-arrays of ports */
	unsigned long resuming_ports;
	unsigned long ports_timeout;		/* Time to stop signalling */

	struct list_head idle_qh_list;		/* Where the idle QHs live */

	int rh_numports;			/* Number of root-hub ports */

	wait_queue_head_t waitqh;		/* endpoint_disable waiters */
	int num_waiting;			/* Number of waiters */

	int total_load;				/* Sum of array values */
	short load[MAX_PHASE];			/* Periodic allocations */

	struct clk *clk;			/* (optional) clock source */

	/* Reset host controller */
	void	(*reset_hc) (struct uhci_hcd *uhci);
	int	(*check_and_reset_hc) (struct uhci_hcd *uhci);
	/* configure_hc should perform arch specific settings, if needed */
	void	(*configure_hc) (struct uhci_hcd *uhci);
	/* Check for broken resume detect interrupts */
	int	(*resume_detect_interrupts_are_broken) (struct uhci_hcd *uhci);
	/* Check for broken global suspend */
	int	(*global_suspend_mode_is_broken) (struct uhci_hcd *uhci);
};

/* Convert between a usb_hcd pointer and the corresponding uhci_hcd */
static inline struct uhci_hcd *hcd_to_uhci(struct usb_hcd *hcd)
{
	return (struct uhci_hcd *) (hcd->hcd_priv);
}
static inline struct usb_hcd *uhci_to_hcd(struct uhci_hcd *uhci)
{
	return container_of((void *) uhci, struct usb_hcd, hcd_priv);
}

#define uhci_dev(u)	(uhci_to_hcd(u)->self.controller)

/* Utility macro for comparing frame numbers */
#define uhci_frame_before_eq(f1, f2)	(0 <= (int) ((f2) - (f1)))


/*
 *	Private per-URB data
 */
struct urb_priv {
	struct list_head node;		/* Node in the QH's urbp list */

	struct urb *urb;

	struct uhci_qh *qh;		/* QH for this URB */
	struct list_head td_list;

	unsigned fsbr:1;		/* URB wants FSBR */
};


/* Some special IDs */

#define PCI_VENDOR_ID_GENESYS		0x17a0
#define PCI_DEVICE_ID_GL880S_UHCI	0x8083

/* Aspeed SoC needs some quirks */
static inline bool uhci_is_aspeed(const struct uhci_hcd *uhci)
{
	return IS_ENABLED(CONFIG_USB_UHCI_ASPEED) && uhci->is_aspeed;
}

/*
 * Functions used to access controller registers. The UCHI spec says that host
 * controller I/O registers are mapped into PCI I/O space. For non-PCI hosts
 * we use memory mapped registers.
 */

#ifndef CONFIG_USB_UHCI_SUPPORT_NON_PCI_HC
/* Support PCI only */
static inline u32 uhci_readl(const struct uhci_hcd *uhci, int reg)
{
	return inl(uhci->io_addr + reg);
}

static inline void uhci_writel(const struct uhci_hcd *uhci, u32 val, int reg)
{
	outl(val, uhci->io_addr + reg);
}

static inline u16 uhci_readw(const struct uhci_hcd *uhci, int reg)
{
	return inw(uhci->io_addr + reg);
}

static inline void uhci_writew(const struct uhci_hcd *uhci, u16 val, int reg)
{
	outw(val, uhci->io_addr + reg);
}

static inline u8 uhci_readb(const struct uhci_hcd *uhci, int reg)
{
	return inb(uhci->io_addr + reg);
}

static inline void uhci_writeb(const struct uhci_hcd *uhci, u8 val, int reg)
{
	outb(val, uhci->io_addr + reg);
}

#else
/* Support non-PCI host controllers */
#ifdef CONFIG_USB_PCI
/* Support PCI and non-PCI host controllers */
#define uhci_has_pci_registers(u)	((u)->io_addr != 0)
#else
/* Support non-PCI host controllers only */
#define uhci_has_pci_registers(u)	0
#endif

#ifdef CONFIG_USB_UHCI_BIG_ENDIAN_MMIO
/* Support (non-PCI) big endian host controllers */
#define uhci_big_endian_mmio(u)		((u)->big_endian_mmio)
#else
#define uhci_big_endian_mmio(u)		0
#endif

static inline int uhci_aspeed_reg(unsigned int reg)
{
	switch (reg) {
	case USBCMD:
		return 00;
	case USBSTS:
		return 0x04;
	case USBINTR:
		return 0x08;
	case USBFRNUM:
		return 0x80;
	case USBFLBASEADD:
		return 0x0c;
	case USBSOF:
		return 0x84;
	case USBPORTSC1:
		return 0x88;
	case USBPORTSC2:
		return 0x8c;
	case USBPORTSC3:
		return 0x90;
	case USBPORTSC4:
		return 0x94;
	default:
		pr_warn("UHCI: Unsupported register 0x%02x on Aspeed\n", reg);
		/* Return an unimplemented register */
		return 0x10;
	}
}

static inline u32 uhci_readl(const struct uhci_hcd *uhci, int reg)
{
	if (uhci_has_pci_registers(uhci))
		return inl(uhci->io_addr + reg);
	else if (uhci_is_aspeed(uhci))
		return readl(uhci->regs + uhci_aspeed_reg(reg));
#ifdef CONFIG_USB_UHCI_BIG_ENDIAN_MMIO
	else if (uhci_big_endian_mmio(uhci))
		return readl_be(uhci->regs + reg);
#endif
	else
		return readl(uhci->regs + reg);
}

static inline void uhci_writel(const struct uhci_hcd *uhci, u32 val, int reg)
{
	if (uhci_has_pci_registers(uhci))
		outl(val, uhci->io_addr + reg);
	else if (uhci_is_aspeed(uhci))
		writel(val, uhci->regs + uhci_aspeed_reg(reg));
#ifdef CONFIG_USB_UHCI_BIG_ENDIAN_MMIO
	else if (uhci_big_endian_mmio(uhci))
		writel_be(val, uhci->regs + reg);
#endif
	else
		writel(val, uhci->regs + reg);
}

static inline u16 uhci_readw(const struct uhci_hcd *uhci, int reg)
{
	if (uhci_has_pci_registers(uhci))
		return inw(uhci->io_addr + reg);
	else if (uhci_is_aspeed(uhci))
		return readl(uhci->regs + uhci_aspeed_reg(reg));
#ifdef CONFIG_USB_UHCI_BIG_ENDIAN_MMIO
	else if (uhci_big_endian_mmio(uhci))
		return readw_be(uhci->regs + reg);
#endif
	else
		return readw(uhci->regs + reg);
}

static inline void uhci_writew(const struct uhci_hcd *uhci, u16 val, int reg)
{
	if (uhci_has_pci_registers(uhci))
		outw(val, uhci->io_addr + reg);
	else if (uhci_is_aspeed(uhci))
		writel(val, uhci->regs + uhci_aspeed_reg(reg));
#ifdef CONFIG_USB_UHCI_BIG_ENDIAN_MMIO
	else if (uhci_big_endian_mmio(uhci))
		writew_be(val, uhci->regs + reg);
#endif
	else
		writew(val, uhci->regs + reg);
}

static inline u8 uhci_readb(const struct uhci_hcd *uhci, int reg)
{
	if (uhci_has_pci_registers(uhci))
		return inb(uhci->io_addr + reg);
	else if (uhci_is_aspeed(uhci))
		return readl(uhci->regs + uhci_aspeed_reg(reg));
#ifdef CONFIG_USB_UHCI_BIG_ENDIAN_MMIO
	else if (uhci_big_endian_mmio(uhci))
		return readb_be(uhci->regs + reg);
#endif
	else
		return readb(uhci->regs + reg);
}

static inline void uhci_writeb(const struct uhci_hcd *uhci, u8 val, int reg)
{
	if (uhci_has_pci_registers(uhci))
		outb(val, uhci->io_addr + reg);
	else if (uhci_is_aspeed(uhci))
		writel(val, uhci->regs + uhci_aspeed_reg(reg));
#ifdef CONFIG_USB_UHCI_BIG_ENDIAN_MMIO
	else if (uhci_big_endian_mmio(uhci))
		writeb_be(val, uhci->regs + reg);
#endif
	else
		writeb(val, uhci->regs + reg);
}
#endif /* CONFIG_USB_UHCI_SUPPORT_NON_PCI_HC */

/*
 * The GRLIB GRUSBHC controller can use big endian format for its descriptors.
 *
 * UHCI controllers accessed through PCI work normally (little-endian
 * everywhere), so we don't bother supporting a BE-only mode.
 */
#ifdef CONFIG_USB_UHCI_BIG_ENDIAN_DESC
#define uhci_big_endian_desc(u)		((u)->big_endian_desc)

/* cpu to uhci */
static inline __hc32 cpu_to_hc32(const struct uhci_hcd *uhci, const u32 x)
{
	return uhci_big_endian_desc(uhci)
		? (__force __hc32)cpu_to_be32(x)
		: (__force __hc32)cpu_to_le32(x);
}

/* uhci to cpu */
static inline u32 hc32_to_cpu(const struct uhci_hcd *uhci, const __hc32 x)
{
	return uhci_big_endian_desc(uhci)
		? be32_to_cpu((__force __be32)x)
		: le32_to_cpu((__force __le32)x);
}

#else
/* cpu to uhci */
static inline __hc32 cpu_to_hc32(const struct uhci_hcd *uhci, const u32 x)
{
	return cpu_to_le32(x);
}

/* uhci to cpu */
static inline u32 hc32_to_cpu(const struct uhci_hcd *uhci, const __hc32 x)
{
	return le32_to_cpu(x);
}
#endif

#endif
