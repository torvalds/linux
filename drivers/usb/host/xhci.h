/*
 * xHCI host controller driver
 *
 * Copyright (C) 2008 Intel Corp.
 *
 * Author: Sarah Sharp
 * Some code borrowed from the Linux EHCI driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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

#ifndef __LINUX_XHCI_HCD_H
#define __LINUX_XHCI_HCD_H

#include <linux/usb.h>

#include "../core/hcd.h"
/* Code sharing between pci-quirks and xhci hcd */
#include	"xhci-ext-caps.h"

/* xHCI PCI Configuration Registers */
#define XHCI_SBRN_OFFSET	(0x60)

/*
 * xHCI register interface.
 * This corresponds to the eXtensible Host Controller Interface (xHCI)
 * Revision 0.95 specification
 *
 * Registers should always be accessed with double word or quad word accesses.
 *
 * Some xHCI implementations may support 64-bit address pointers.  Registers
 * with 64-bit address pointers should be written to with dword accesses by
 * writing the low dword first (ptr[0]), then the high dword (ptr[1]) second.
 * xHCI implementations that do not support 64-bit address pointers will ignore
 * the high dword, and write order is irrelevant.
 */

/**
 * struct xhci_cap_regs - xHCI Host Controller Capability Registers.
 * @hc_capbase:		length of the capabilities register and HC version number
 * @hcs_params1:	HCSPARAMS1 - Structural Parameters 1
 * @hcs_params2:	HCSPARAMS2 - Structural Parameters 2
 * @hcs_params3:	HCSPARAMS3 - Structural Parameters 3
 * @hcc_params:		HCCPARAMS - Capability Parameters
 * @db_off:		DBOFF - Doorbell array offset
 * @run_regs_off:	RTSOFF - Runtime register space offset
 */
struct xhci_cap_regs {
	u32	hc_capbase;
	u32	hcs_params1;
	u32	hcs_params2;
	u32	hcs_params3;
	u32	hcc_params;
	u32	db_off;
	u32	run_regs_off;
	/* Reserved up to (CAPLENGTH - 0x1C) */
} __attribute__ ((packed));

/* hc_capbase bitmasks */
/* bits 7:0 - how long is the Capabilities register */
#define HC_LENGTH(p)		XHCI_HC_LENGTH(p)
/* bits 31:16	*/
#define HC_VERSION(p)		(((p) >> 16) & 0xffff)

/* HCSPARAMS1 - hcs_params1 - bitmasks */
/* bits 0:7, Max Device Slots */
#define HCS_MAX_SLOTS(p)	(((p) >> 0) & 0xff)
#define HCS_SLOTS_MASK		0xff
/* bits 8:18, Max Interrupters */
#define HCS_MAX_INTRS(p)	(((p) >> 8) & 0x7ff)
/* bits 24:31, Max Ports - max value is 0x7F = 127 ports */
#define HCS_MAX_PORTS(p)	(((p) >> 24) & 0x7f)

/* HCSPARAMS2 - hcs_params2 - bitmasks */
/* bits 0:3, frames or uframes that SW needs to queue transactions
 * ahead of the HW to meet periodic deadlines */
#define HCS_IST(p)		(((p) >> 0) & 0xf)
/* bits 4:7, max number of Event Ring segments */
#define HCS_ERST_MAX(p)		(((p) >> 4) & 0xf)
/* bit 26 Scratchpad restore - for save/restore HW state - not used yet */
/* bits 27:31 number of Scratchpad buffers SW must allocate for the HW */

/* HCSPARAMS3 - hcs_params3 - bitmasks */
/* bits 0:7, Max U1 to U0 latency for the roothub ports */
#define HCS_U1_LATENCY(p)	(((p) >> 0) & 0xff)
/* bits 16:31, Max U2 to U0 latency for the roothub ports */
#define HCS_U2_LATENCY(p)	(((p) >> 16) & 0xffff)

/* HCCPARAMS - hcc_params - bitmasks */
/* true: HC can use 64-bit address pointers */
#define HCC_64BIT_ADDR(p)	((p) & (1 << 0))
/* true: HC can do bandwidth negotiation */
#define HCC_BANDWIDTH_NEG(p)	((p) & (1 << 1))
/* true: HC uses 64-byte Device Context structures
 * FIXME 64-byte context structures aren't supported yet.
 */
#define HCC_64BYTE_CONTEXT(p)	((p) & (1 << 2))
/* true: HC has port power switches */
#define HCC_PPC(p)		((p) & (1 << 3))
/* true: HC has port indicators */
#define HCS_INDICATOR(p)	((p) & (1 << 4))
/* true: HC has Light HC Reset Capability */
#define HCC_LIGHT_RESET(p)	((p) & (1 << 5))
/* true: HC supports latency tolerance messaging */
#define HCC_LTC(p)		((p) & (1 << 6))
/* true: no secondary Stream ID Support */
#define HCC_NSS(p)		((p) & (1 << 7))
/* Max size for Primary Stream Arrays - 2^(n+1), where n is bits 12:15 */
#define HCC_MAX_PSA		(1 << ((((p) >> 12) & 0xf) + 1))
/* Extended Capabilities pointer from PCI base - section 5.3.6 */
#define HCC_EXT_CAPS(p)		XHCI_HCC_EXT_CAPS(p)

/* db_off bitmask - bits 0:1 reserved */
#define	DBOFF_MASK	(~0x3)

/* run_regs_off bitmask - bits 0:4 reserved */
#define	RTSOFF_MASK	(~0x1f)


/* Number of registers per port */
#define	NUM_PORT_REGS	4

/**
 * struct xhci_op_regs - xHCI Host Controller Operational Registers.
 * @command:		USBCMD - xHC command register
 * @status:		USBSTS - xHC status register
 * @page_size:		This indicates the page size that the host controller
 * 			supports.  If bit n is set, the HC supports a page size
 * 			of 2^(n+12), up to a 128MB page size.
 * 			4K is the minimum page size.
 * @cmd_ring:		CRP - 64-bit Command Ring Pointer
 * @dcbaa_ptr:		DCBAAP - 64-bit Device Context Base Address Array Pointer
 * @config_reg:		CONFIG - Configure Register
 * @port_status_base:	PORTSCn - base address for Port Status and Control
 * 			Each port has a Port Status and Control register,
 * 			followed by a Port Power Management Status and Control
 * 			register, a Port Link Info register, and a reserved
 * 			register.
 * @port_power_base:	PORTPMSCn - base address for
 * 			Port Power Management Status and Control
 * @port_link_base:	PORTLIn - base address for Port Link Info (current
 * 			Link PM state and control) for USB 2.1 and USB 3.0
 * 			devices.
 */
struct xhci_op_regs {
	u32	command;
	u32	status;
	u32	page_size;
	u32	reserved1;
	u32	reserved2;
	u32	dev_notification;
	u32	cmd_ring[2];
	/* rsvd: offset 0x20-2F */
	u32	reserved3[4];
	u32	dcbaa_ptr[2];
	u32	config_reg;
	/* rsvd: offset 0x3C-3FF */
	u32	reserved4[241];
	/* port 1 registers, which serve as a base address for other ports */
	u32	port_status_base;
	u32	port_power_base;
	u32	port_link_base;
	u32	reserved5;
	/* registers for ports 2-255 */
	u32	reserved6[NUM_PORT_REGS*254];
} __attribute__ ((packed));

/* USBCMD - USB command - command bitmasks */
/* start/stop HC execution - do not write unless HC is halted*/
#define CMD_RUN		XHCI_CMD_RUN
/* Reset HC - resets internal HC state machine and all registers (except
 * PCI config regs).  HC does NOT drive a USB reset on the downstream ports.
 * The xHCI driver must reinitialize the xHC after setting this bit.
 */
#define CMD_RESET	(1 << 1)
/* Event Interrupt Enable - a '1' allows interrupts from the host controller */
#define CMD_EIE		XHCI_CMD_EIE
/* Host System Error Interrupt Enable - get out-of-band signal for HC errors */
#define CMD_HSEIE	XHCI_CMD_HSEIE
/* bits 4:6 are reserved (and should be preserved on writes). */
/* light reset (port status stays unchanged) - reset completed when this is 0 */
#define CMD_LRESET	(1 << 7)
/* FIXME: ignoring host controller save/restore state for now. */
#define CMD_CSS		(1 << 8)
#define CMD_CRS		(1 << 9)
/* Enable Wrap Event - '1' means xHC generates an event when MFINDEX wraps. */
#define CMD_EWE		XHCI_CMD_EWE
/* MFINDEX power management - '1' means xHC can stop MFINDEX counter if all root
 * hubs are in U3 (selective suspend), disconnect, disabled, or powered-off.
 * '0' means the xHC can power it off if all ports are in the disconnect,
 * disabled, or powered-off state.
 */
#define CMD_PM_INDEX	(1 << 11)
/* bits 12:31 are reserved (and should be preserved on writes). */

/* USBSTS - USB status - status bitmasks */
/* HC not running - set to 1 when run/stop bit is cleared. */
#define STS_HALT	XHCI_STS_HALT
/* serious error, e.g. PCI parity error.  The HC will clear the run/stop bit. */
#define STS_FATAL	(1 << 2)
/* event interrupt - clear this prior to clearing any IP flags in IR set*/
#define STS_EINT	(1 << 3)
/* port change detect */
#define STS_PORT	(1 << 4)
/* bits 5:7 reserved and zeroed */
/* save state status - '1' means xHC is saving state */
#define STS_SAVE	(1 << 8)
/* restore state status - '1' means xHC is restoring state */
#define STS_RESTORE	(1 << 9)
/* true: save or restore error */
#define STS_SRE		(1 << 10)
/* true: Controller Not Ready to accept doorbell or op reg writes after reset */
#define STS_CNR		XHCI_STS_CNR
/* true: internal Host Controller Error - SW needs to reset and reinitialize */
#define STS_HCE		(1 << 12)
/* bits 13:31 reserved and should be preserved */

/*
 * DNCTRL - Device Notification Control Register - dev_notification bitmasks
 * Generate a device notification event when the HC sees a transaction with a
 * notification type that matches a bit set in this bit field.
 */
#define	DEV_NOTE_MASK		(0xffff)
#define ENABLE_DEV_NOTE(x)	(1 << x)
/* Most of the device notification types should only be used for debug.
 * SW does need to pay attention to function wake notifications.
 */
#define	DEV_NOTE_FWAKE		ENABLE_DEV_NOTE(1)

/* CONFIG - Configure Register - config_reg bitmasks */
/* bits 0:7 - maximum number of device slots enabled (NumSlotsEn) */
#define MAX_DEVS(p)	((p) & 0xff)
/* bits 8:31 - reserved and should be preserved */

/* PORTSC - Port Status and Control Register - port_status_base bitmasks */
/* true: device connected */
#define PORT_CONNECT	(1 << 0)
/* true: port enabled */
#define PORT_PE		(1 << 1)
/* bit 2 reserved and zeroed */
/* true: port has an over-current condition */
#define PORT_OC		(1 << 3)
/* true: port reset signaling asserted */
#define PORT_RESET	(1 << 4)
/* Port Link State - bits 5:8
 * A read gives the current link PM state of the port,
 * a write with Link State Write Strobe set sets the link state.
 */
/* true: port has power (see HCC_PPC) */
#define PORT_POWER	(1 << 9)
/* bits 10:13 indicate device speed:
 * 0 - undefined speed - port hasn't be initialized by a reset yet
 * 1 - full speed
 * 2 - low speed
 * 3 - high speed
 * 4 - super speed
 * 5-15 reserved
 */
#define DEV_SPEED_MASK		(0xf<<10)
#define DEV_UNDEFSPEED(p)	(((p) & DEV_SPEED_MASK) == (0x0<<10))
#define DEV_FULLSPEED(p)	(((p) & DEV_SPEED_MASK) == (0x1<<10))
#define DEV_LOWSPEED(p)		(((p) & DEV_SPEED_MASK) == (0x2<<10))
#define DEV_HIGHSPEED(p)	(((p) & DEV_SPEED_MASK) == (0x3<<10))
#define DEV_SUPERSPEED(p)	(((p) & DEV_SPEED_MASK) == (0x4<<10))
/* Port Indicator Control */
#define PORT_LED_OFF	(0 << 14)
#define PORT_LED_AMBER	(1 << 14)
#define PORT_LED_GREEN	(2 << 14)
#define PORT_LED_MASK	(3 << 14)
/* Port Link State Write Strobe - set this when changing link state */
#define PORT_LINK_STROBE	(1 << 16)
/* true: connect status change */
#define PORT_CSC	(1 << 17)
/* true: port enable change */
#define PORT_PEC	(1 << 18)
/* true: warm reset for a USB 3.0 device is done.  A "hot" reset puts the port
 * into an enabled state, and the device into the default state.  A "warm" reset
 * also resets the link, forcing the device through the link training sequence.
 * SW can also look at the Port Reset register to see when warm reset is done.
 */
#define PORT_WRC	(1 << 19)
/* true: over-current change */
#define PORT_OCC	(1 << 20)
/* true: reset change - 1 to 0 transition of PORT_RESET */
#define PORT_RC		(1 << 21)
/* port link status change - set on some port link state transitions:
 *  Transition				Reason
 *  ------------------------------------------------------------------------------
 *  - U3 to Resume			Wakeup signaling from a device
 *  - Resume to Recovery to U0		USB 3.0 device resume
 *  - Resume to U0			USB 2.0 device resume
 *  - U3 to Recovery to U0		Software resume of USB 3.0 device complete
 *  - U3 to U0				Software resume of USB 2.0 device complete
 *  - U2 to U0				L1 resume of USB 2.1 device complete
 *  - U0 to U0 (???)			L1 entry rejection by USB 2.1 device
 *  - U0 to disabled			L1 entry error with USB 2.1 device
 *  - Any state to inactive		Error on USB 3.0 port
 */
#define PORT_PLC	(1 << 22)
/* port configure error change - port failed to configure its link partner */
#define PORT_CEC	(1 << 23)
/* bit 24 reserved */
/* wake on connect (enable) */
#define PORT_WKCONN_E	(1 << 25)
/* wake on disconnect (enable) */
#define PORT_WKDISC_E	(1 << 26)
/* wake on over-current (enable) */
#define PORT_WKOC_E	(1 << 27)
/* bits 28:29 reserved */
/* true: device is removable - for USB 3.0 roothub emulation */
#define PORT_DEV_REMOVE	(1 << 30)
/* Initiate a warm port reset - complete when PORT_WRC is '1' */
#define PORT_WR		(1 << 31)

/* Port Power Management Status and Control - port_power_base bitmasks */
/* Inactivity timer value for transitions into U1, in microseconds.
 * Timeout can be up to 127us.  0xFF means an infinite timeout.
 */
#define PORT_U1_TIMEOUT(p)	((p) & 0xff)
/* Inactivity timer value for transitions into U2 */
#define PORT_U2_TIMEOUT(p)	(((p) & 0xff) << 8)
/* Bits 24:31 for port testing */


/**
 * struct intr_reg - Interrupt Register Set
 * @irq_pending:	IMAN - Interrupt Management Register.  Used to enable
 *			interrupts and check for pending interrupts.
 * @irq_control:	IMOD - Interrupt Moderation Register.
 * 			Used to throttle interrupts.
 * @erst_size:		Number of segments in the Event Ring Segment Table (ERST).
 * @erst_base:		ERST base address.
 * @erst_dequeue:	Event ring dequeue pointer.
 *
 * Each interrupter (defined by a MSI-X vector) has an event ring and an Event
 * Ring Segment Table (ERST) associated with it.  The event ring is comprised of
 * multiple segments of the same size.  The HC places events on the ring and
 * "updates the Cycle bit in the TRBs to indicate to software the current
 * position of the Enqueue Pointer." The HCD (Linux) processes those events and
 * updates the dequeue pointer.
 */
struct intr_reg {
	u32	irq_pending;
	u32	irq_control;
	u32	erst_size;
	u32	rsvd;
	u32	erst_base[2];
	u32	erst_dequeue[2];
} __attribute__ ((packed));

#define	ER_IRQ_PENDING(p)	((p) & 0x1)
#define	ER_IRQ_ENABLE(p)	((p) | 0x2)
/* Preserve bits 16:31 of erst_size */
#define	ERST_SIZE_MASK	(0xffff<<16)

/**
 * struct xhci_run_regs
 * @microframe_index:
 * 		MFINDEX - current microframe number
 *
 * Section 5.5 Host Controller Runtime Registers:
 * "Software should read and write these registers using only Dword (32 bit)
 * or larger accesses"
 */
struct xhci_run_regs {
	u32	microframe_index;
	u32	rsvd[7];
	struct intr_reg	ir_set[128];
} __attribute__ ((packed));


/* There is one ehci_hci structure per controller */
struct xhci_hcd {
	/* glue to PCI and HCD framework */
	struct xhci_cap_regs __iomem *cap_regs;
	struct xhci_op_regs __iomem *op_regs;
	struct xhci_run_regs __iomem *run_regs;

	/* Cached register copies of read-only HC data */
	__u32		hcs_params1;
	__u32		hcs_params2;
	__u32		hcs_params3;
	__u32		hcc_params;

	spinlock_t	lock;

	/* packed release number */
	u8		sbrn;
	u16		hci_version;
	u8		max_slots;
	u8		max_interrupters;
	u8		max_ports;
	u8		isoc_threshold;
	int		event_ring_max;
	int		addr_64;
	int		page_size;
};

/* convert between an HCD pointer and the corresponding EHCI_HCD */
static inline struct xhci_hcd *hcd_to_xhci(struct usb_hcd *hcd)
{
	return (struct xhci_hcd *) (hcd->hcd_priv);
}

static inline struct usb_hcd *xhci_to_hcd(struct xhci_hcd *xhci)
{
	return container_of((void *) xhci, struct usb_hcd, hcd_priv);
}

#ifdef CONFIG_USB_XHCI_HCD_DEBUGGING
#define XHCI_DEBUG	1
#else
#define XHCI_DEBUG	0
#endif

#define xhci_dbg(xhci, fmt, args...) \
	do { if (XHCI_DEBUG) dev_dbg(xhci_to_hcd(xhci)->self.controller , fmt , ## args); } while (0)
#define xhci_info(xhci, fmt, args...) \
	do { if (XHCI_DEBUG) dev_info(xhci_to_hcd(xhci)->self.controller , fmt , ## args); } while (0)
#define xhci_err(xhci, fmt, args...) \
	dev_err(xhci_to_hcd(xhci)->self.controller , fmt , ## args)
#define xhci_warn(xhci, fmt, args...) \
	dev_warn(xhci_to_hcd(xhci)->self.controller , fmt , ## args)

/* TODO: copied from ehci.h - can be refactored? */
/* xHCI spec says all registers are little endian */
static inline unsigned int xhci_readl(const struct xhci_hcd *xhci,
		__u32 __iomem *regs)
{
	return readl(regs);
}
static inline void xhci_writel(const struct xhci_hcd *xhci,
		const unsigned int val, __u32 __iomem *regs)
{
	if (!in_interrupt())
		xhci_dbg(xhci, "`MEM_WRITE_DWORD(3'b000, 32'h%0x, 32'h%0x, 4'hf);\n",
				(unsigned int) regs, val);
	writel(val, regs);
}

#endif /* __LINUX_XHCI_HCD_H */
