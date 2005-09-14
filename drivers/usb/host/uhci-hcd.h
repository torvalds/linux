#ifndef __LINUX_UHCI_HCD_H
#define __LINUX_UHCI_HCD_H

#include <linux/list.h>
#include <linux/usb.h>

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
#define   USBSTS_HSE		0x0008	/* Host System Error - basically PCI problems */
#define   USBSTS_HCPE		0x0010	/* Host Controller Process Error - the scripts were buggy */
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
#define   USBPORTSC_CCS		0x0001	/* Current Connect Status ("device present") */
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

/* Legacy support register */
#define USBLEGSUP		0xc0
#define   USBLEGSUP_DEFAULT	0x2000	/* only PIRQ enable set */
#define   USBLEGSUP_RWC		0x8f00	/* the R/WC bits */
#define   USBLEGSUP_RO		0x5040	/* R/O and reserved bits */

#define UHCI_NULL_DATA_SIZE	0x7FF	/* for UHCI controller TD */

#define UHCI_PTR_BITS		cpu_to_le32(0x000F)
#define UHCI_PTR_TERM		cpu_to_le32(0x0001)
#define UHCI_PTR_QH		cpu_to_le32(0x0002)
#define UHCI_PTR_DEPTH		cpu_to_le32(0x0004)
#define UHCI_PTR_BREADTH	cpu_to_le32(0x0000)

#define UHCI_NUMFRAMES		1024	/* in the frame list [array] */
#define UHCI_MAX_SOF_NUMBER	2047	/* in an SOF packet */
#define CAN_SCHEDULE_FRAMES	1000	/* how far future frames can be scheduled */

struct uhci_frame_list {
	__le32 frame[UHCI_NUMFRAMES];

	void *frame_cpu[UHCI_NUMFRAMES];

	dma_addr_t dma_handle;
};

struct urb_priv;

/*
 * One role of a QH is to hold a queue of TDs for some endpoint.  Each QH is
 * used with one URB, and qh->element (updated by the HC) is either:
 *   - the next unprocessed TD for the URB, or
 *   - UHCI_PTR_TERM (when there's no more traffic for this endpoint), or
 *   - the QH for the next URB queued to the same endpoint.
 *
 * The other role of a QH is to serve as a "skeleton" framelist entry, so we
 * can easily splice a QH for some endpoint into the schedule at the right
 * place.  Then qh->element is UHCI_PTR_TERM.
 *
 * In the frame list, qh->link maintains a list of QHs seen by the HC:
 *     skel1 --> ep1-qh --> ep2-qh --> ... --> skel2 --> ...
 */
struct uhci_qh {
	/* Hardware fields */
	__le32 link;			/* Next queue */
	__le32 element;			/* Queue element pointer */

	/* Software fields */
	dma_addr_t dma_handle;

	struct urb_priv *urbp;

	struct list_head list;		/* P: uhci->frame_list_lock */
	struct list_head remove_list;	/* P: uhci->remove_list_lock */
} __attribute__((aligned(16)));

/*
 * We need a special accessor for the element pointer because it is
 * subject to asynchronous updates by the controller
 */
static __le32 inline qh_element(struct uhci_qh *qh) {
	__le32 element = qh->element;

	barrier();
	return element;
}

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

#define TD_CTRL_ANY_ERROR	(TD_CTRL_STALLED | TD_CTRL_DBUFERR | \
				 TD_CTRL_BABBLE | TD_CTRL_CRCTIME | TD_CTRL_BITSTUFF)

#define uhci_maxerr(err)		((err) << TD_CTRL_C_ERR_SHIFT)
#define uhci_status_bits(ctrl_sts)	((ctrl_sts) & 0xF60000)
#define uhci_actual_length(ctrl_sts)	(((ctrl_sts) + 1) & TD_CTRL_ACTLEN_MASK) /* 1-based */

/*
 * for TD <info>: (a.k.a. Token)
 */
#define td_token(td)		le32_to_cpu((td)->token)
#define TD_TOKEN_DEVADDR_SHIFT	8
#define TD_TOKEN_TOGGLE_SHIFT	19
#define TD_TOKEN_TOGGLE		(1 << 19)
#define TD_TOKEN_EXPLEN_SHIFT	21
#define TD_TOKEN_EXPLEN_MASK	0x7FF		/* expected length, encoded as n - 1 */
#define TD_TOKEN_PID_MASK	0xFF

#define uhci_explen(len)	((len) << TD_TOKEN_EXPLEN_SHIFT)

#define uhci_expected_length(token) ((((token) >> 21) + 1) & TD_TOKEN_EXPLEN_MASK)
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
 * sw space after the TD entry as far as I can tell.
 *
 * But let's just go with the documentation, at least for 32-bit machines.
 * On 64-bit machines we probably want to take advantage of the fact that
 * hw doesn't really care about the size of the sw-only area.
 *
 * Alas, not anymore, we have more than 4 words for software, woops.
 * Everything still works tho, surprise! -jerdfelt
 *
 * td->link points to either another TD (not necessarily for the same urb or
 * even the same endpoint), or nothing (PTR_TERM), or a QH (for queued urbs)
 */
struct uhci_td {
	/* Hardware fields */
	__le32 link;
	__le32 status;
	__le32 token;
	__le32 buffer;

	/* Software fields */
	dma_addr_t dma_handle;

	struct urb *urb;

	struct list_head list;		/* P: urb->lock */
	struct list_head remove_list;	/* P: uhci->td_remove_list_lock */

	int frame;			/* for iso: what frame? */
	struct list_head fl_list;	/* P: uhci->frame_list_lock */
} __attribute__((aligned(16)));

/*
 * We need a special accessor for the control/status word because it is
 * subject to asynchronous updates by the controller
 */
static u32 inline td_status(struct uhci_td *td) {
	__le32 status = td->status;

	barrier();
	return le32_to_cpu(status);
}


/*
 * The UHCI driver places Interrupt, Control and Bulk into QH's both
 * to group together TD's for one transfer, and also to faciliate queuing
 * of URB's. To make it easy to insert entries into the schedule, we have
 * a skeleton of QH's for each predefined Interrupt latency, low-speed
 * control, full-speed control and terminating QH (see explanation for
 * the terminating QH below).
 *
 * When we want to add a new QH, we add it to the end of the list for the
 * skeleton QH.
 *
 * For instance, the queue can look like this:
 *
 * skel int128 QH
 * dev 1 interrupt QH
 * dev 5 interrupt QH
 * skel int64 QH
 * skel int32 QH
 * ...
 * skel int1 QH
 * skel low-speed control QH
 * dev 5 control QH
 * skel full-speed control QH
 * skel bulk QH
 * dev 1 bulk QH
 * dev 2 bulk QH
 * skel terminating QH
 *
 * The terminating QH is used for 2 reasons:
 * - To place a terminating TD which is used to workaround a PIIX bug
 *   (see Intel errata for explanation)
 * - To loop back to the full-speed control queue for full-speed bandwidth
 *   reclamation
 *
 * Isochronous transfers are stored before the start of the skeleton
 * schedule and don't use QH's. While the UHCI spec doesn't forbid the
 * use of QH's for Isochronous, it doesn't use them either. Since we don't
 * need to use them either, we follow the spec diagrams in hope that it'll
 * be more compatible with future UHCI implementations.
 */

#define UHCI_NUM_SKELQH		12
#define skel_int128_qh		skelqh[0]
#define skel_int64_qh		skelqh[1]
#define skel_int32_qh		skelqh[2]
#define skel_int16_qh		skelqh[3]
#define skel_int8_qh		skelqh[4]
#define skel_int4_qh		skelqh[5]
#define skel_int2_qh		skelqh[6]
#define skel_int1_qh		skelqh[7]
#define skel_ls_control_qh	skelqh[8]
#define skel_fs_control_qh	skelqh[9]
#define skel_bulk_qh		skelqh[10]
#define skel_term_qh		skelqh[11]

/*
 * Search tree for determining where <interval> fits in the skelqh[]
 * skeleton.
 *
 * An interrupt request should be placed into the slowest skelqh[]
 * which meets the interval/period/frequency requirement.
 * An interrupt request is allowed to be faster than <interval> but not slower.
 *
 * For a given <interval>, this function returns the appropriate/matching
 * skelqh[] index value.
 */
static inline int __interval_to_skel(int interval)
{
	if (interval < 16) {
		if (interval < 4) {
			if (interval < 2)
				return 7;	/* int1 for 0-1 ms */
			return 6;		/* int2 for 2-3 ms */
		}
		if (interval < 8)
			return 5;		/* int4 for 4-7 ms */
		return 4;			/* int8 for 8-15 ms */
	}
	if (interval < 64) {
		if (interval < 32)
			return 3;		/* int16 for 16-31 ms */
		return 2;			/* int32 for 32-63 ms */
	}
	if (interval < 128)
		return 1;			/* int64 for 64-127 ms */
	return 0;				/* int128 for 128-255 ms (Max.) */
}

/*
 * States for the root hub.
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
	 * These two must come first */
	UHCI_RH_RESET,
	UHCI_RH_SUSPENDED,

	UHCI_RH_AUTO_STOPPED,
	UHCI_RH_RESUMING,

	/* In this state the HC changes from running to halted,
	 * so it can legally appear either way. */
	UHCI_RH_SUSPENDING,

	/* In the following states it's an error if the HC is halted.
	 * These two must come last */
	UHCI_RH_RUNNING,		/* The normal state */
	UHCI_RH_RUNNING_NODEVS,		/* Running with no devices attached */
};

/*
 * This describes the full uhci information.
 */
struct uhci_hcd {

	/* debugfs */
	struct dentry *dentry;

	/* Grabbed from PCI */
	unsigned long io_addr;

	struct dma_pool *qh_pool;
	struct dma_pool *td_pool;

	struct uhci_td *term_td;	/* Terminating TD, see UHCI bug */
	struct uhci_qh *skelqh[UHCI_NUM_SKELQH];	/* Skeleton QH's */

	spinlock_t lock;
	struct uhci_frame_list *fl;		/* P: uhci->lock */
	int fsbr;				/* Full-speed bandwidth reclamation */
	unsigned long fsbrtimeout;		/* FSBR delay */

	enum uhci_rh_state rh_state;
	unsigned long auto_stop_time;		/* When to AUTO_STOP */

	unsigned int frame_number;		/* As of last check */
	unsigned int is_stopped;
#define UHCI_IS_STOPPED		9999		/* Larger than a frame # */

	unsigned int scan_in_progress:1;	/* Schedule scan is running */
	unsigned int need_rescan:1;		/* Redo the schedule scan */
	unsigned int hc_inaccessible:1;		/* HC is suspended or dead */
	unsigned int working_RD:1;		/* Suspended root hub doesn't
						   need to be polled */

	/* Support for port suspend/resume/reset */
	unsigned long port_c_suspend;		/* Bit-arrays of ports */
	unsigned long suspended_ports;
	unsigned long resuming_ports;
	unsigned long ports_timeout;		/* Time to stop signalling */

	/* Main list of URB's currently controlled by this HC */
	struct list_head urb_list;		/* P: uhci->lock */

	/* List of QH's that are done, but waiting to be unlinked (race) */
	struct list_head qh_remove_list;	/* P: uhci->lock */
	unsigned int qh_remove_age;		/* Age in frames */

	/* List of TD's that are done, but waiting to be freed (race) */
	struct list_head td_remove_list;	/* P: uhci->lock */
	unsigned int td_remove_age;		/* Age in frames */

	/* List of asynchronously unlinked URB's */
	struct list_head urb_remove_list;	/* P: uhci->lock */
	unsigned int urb_remove_age;		/* Age in frames */

	/* List of URB's awaiting completion callback */
	struct list_head complete_list;		/* P: uhci->lock */

	int rh_numports;			/* Number of root-hub ports */

	wait_queue_head_t waitqh;		/* endpoint_disable waiters */
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

struct urb_priv {
	struct list_head urb_list;

	struct urb *urb;

	struct uhci_qh *qh;		/* QH for this URB */
	struct list_head td_list;	/* P: urb->lock */

	unsigned fsbr : 1;		/* URB turned on FSBR */
	unsigned fsbr_timeout : 1;	/* URB timed out on FSBR */
	unsigned queued : 1;		/* QH was queued (not linked in) */
	unsigned short_control_packet : 1;	/* If we get a short packet during */
						/*  a control transfer, retrigger */
						/*  the status phase */

	unsigned long inserttime;	/* In jiffies */
	unsigned long fsbrtime;		/* In jiffies */

	struct list_head queue_list;	/* P: uhci->frame_list_lock */
};

/*
 * Locking in uhci.c
 *
 * Almost everything relating to the hardware schedule and processing
 * of URBs is protected by uhci->lock.  urb->status is protected by
 * urb->lock; that's the one exception.
 *
 * To prevent deadlocks, never lock uhci->lock while holding urb->lock.
 * The safe order of locking is:
 *
 * #1 uhci->lock
 * #2 urb->lock
 */


/* Some special IDs */

#define PCI_VENDOR_ID_GENESYS		0x17a0
#define PCI_DEVICE_ID_GL880S_UHCI	0x8083
#define PCI_DEVICE_ID_GL880S_EHCI	0x8084

#endif
