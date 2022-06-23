/* SPDX-License-Identifier: GPL-2.0 */

/*
 * xHCI host controller driver
 *
 * Copyright (C) 2008 Intel Corp.
 *
 * Author: Sarah Sharp
 * Some code borrowed from the Linux EHCI driver.
 */

#ifndef __LINUX_XHCI_HCD_H
#define __LINUX_XHCI_HCD_H

#include <linux/usb.h>
#include <linux/timer.h>
#include <linux/kernel.h>
#include <linux/usb/hcd.h>
#include <linux/io-64-nonatomic-lo-hi.h>

/* Code sharing between pci-quirks and xhci hcd */
#include	"xhci-ext-caps.h"
#include "pci-quirks.h"

/* max buffer size for trace and debug messages */
#define XHCI_MSG_MAX		500

/* xHCI PCI Configuration Registers */
#define XHCI_SBRN_OFFSET	(0x60)

/* Max number of USB devices for any host controller - limit in section 6.1 */
#define MAX_HC_SLOTS		256
/* Section 5.3.3 - MaxPorts */
#define MAX_HC_PORTS		127

/*
 * xHCI register interface.
 * This corresponds to the eXtensible Host Controller Interface (xHCI)
 * Revision 0.95 specification
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
 * @hcc_params2:	HCCPARAMS2 Capability Parameters 2, xhci 1.1 only
 */
struct xhci_cap_regs {
	__le32	hc_capbase;
	__le32	hcs_params1;
	__le32	hcs_params2;
	__le32	hcs_params3;
	__le32	hcc_params;
	__le32	db_off;
	__le32	run_regs_off;
	__le32	hcc_params2; /* xhci 1.1 */
	/* Reserved up to (CAPLENGTH - 0x1C) */
};

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
/* bits 21:25 Hi 5 bits of Scratchpad buffers SW must allocate for the HW */
/* bit 26 Scratchpad restore - for save/restore HW state - not used yet */
/* bits 27:31 Lo 5 bits of Scratchpad buffers SW must allocate for the HW */
#define HCS_MAX_SCRATCHPAD(p)   ((((p) >> 16) & 0x3e0) | (((p) >> 27) & 0x1f))

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
/* true: HC supports Stopped - Short Packet */
#define HCC_SPC(p)		((p) & (1 << 9))
/* true: HC has Contiguous Frame ID Capability */
#define HCC_CFC(p)		((p) & (1 << 11))
/* Max size for Primary Stream Arrays - 2^(n+1), where n is bits 12:15 */
#define HCC_MAX_PSA(p)		(1 << ((((p) >> 12) & 0xf) + 1))
/* Extended Capabilities pointer from PCI base - section 5.3.6 */
#define HCC_EXT_CAPS(p)		XHCI_HCC_EXT_CAPS(p)

#define CTX_SIZE(_hcc)		(HCC_64BYTE_CONTEXT(_hcc) ? 64 : 32)

/* db_off bitmask - bits 0:1 reserved */
#define	DBOFF_MASK	(~0x3)

/* run_regs_off bitmask - bits 0:4 reserved */
#define	RTSOFF_MASK	(~0x1f)

/* HCCPARAMS2 - hcc_params2 - bitmasks */
/* true: HC supports U3 entry Capability */
#define	HCC2_U3C(p)		((p) & (1 << 0))
/* true: HC supports Configure endpoint command Max exit latency too large */
#define	HCC2_CMC(p)		((p) & (1 << 1))
/* true: HC supports Force Save context Capability */
#define	HCC2_FSC(p)		((p) & (1 << 2))
/* true: HC supports Compliance Transition Capability */
#define	HCC2_CTC(p)		((p) & (1 << 3))
/* true: HC support Large ESIT payload Capability > 48k */
#define	HCC2_LEC(p)		((p) & (1 << 4))
/* true: HC support Configuration Information Capability */
#define	HCC2_CIC(p)		((p) & (1 << 5))
/* true: HC support Extended TBC Capability, Isoc burst count > 65535 */
#define	HCC2_ETC(p)		((p) & (1 << 6))

/* Number of registers per port */
#define	NUM_PORT_REGS	4

#define PORTSC		0
#define PORTPMSC	1
#define PORTLI		2
#define PORTHLPMC	3

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
	__le32	command;
	__le32	status;
	__le32	page_size;
	__le32	reserved1;
	__le32	reserved2;
	__le32	dev_notification;
	__le64	cmd_ring;
	/* rsvd: offset 0x20-2F */
	__le32	reserved3[4];
	__le64	dcbaa_ptr;
	__le32	config_reg;
	/* rsvd: offset 0x3C-3FF */
	__le32	reserved4[241];
	/* port 1 registers, which serve as a base address for other ports */
	__le32	port_status_base;
	__le32	port_power_base;
	__le32	port_link_base;
	__le32	reserved5;
	/* registers for ports 2-255 */
	__le32	reserved6[NUM_PORT_REGS*254];
};

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
/* host controller save/restore state. */
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
/* bit 14 Extended TBC Enable, changes Isoc TRB fields to support larger TBC */
#define CMD_ETE		(1 << 14)
/* bits 15:31 are reserved (and should be preserved on writes). */

#define XHCI_RESET_LONG_USEC		(10 * 1000 * 1000)
#define XHCI_RESET_SHORT_USEC		(250 * 1000)

/* IMAN - Interrupt Management Register */
#define IMAN_IE		(1 << 1)
#define IMAN_IP		(1 << 0)

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
#define ENABLE_DEV_NOTE(x)	(1 << (x))
/* Most of the device notification types should only be used for debug.
 * SW does need to pay attention to function wake notifications.
 */
#define	DEV_NOTE_FWAKE		ENABLE_DEV_NOTE(1)

/* CRCR - Command Ring Control Register - cmd_ring bitmasks */
/* bit 0 is the command ring cycle state */
/* stop ring operation after completion of the currently executing command */
#define CMD_RING_PAUSE		(1 << 1)
/* stop ring immediately - abort the currently executing command */
#define CMD_RING_ABORT		(1 << 2)
/* true: command ring is running */
#define CMD_RING_RUNNING	(1 << 3)
/* bits 4:5 reserved and should be preserved */
/* Command Ring pointer - bit mask for the lower 32 bits. */
#define CMD_RING_RSVD_BITS	(0x3f)

/* CONFIG - Configure Register - config_reg bitmasks */
/* bits 0:7 - maximum number of device slots enabled (NumSlotsEn) */
#define MAX_DEVS(p)	((p) & 0xff)
/* bit 8: U3 Entry Enabled, assert PLC when root port enters U3, xhci 1.1 */
#define CONFIG_U3E		(1 << 8)
/* bit 9: Configuration Information Enable, xhci 1.1 */
#define CONFIG_CIE		(1 << 9)
/* bits 10:31 - reserved and should be preserved */

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
#define PORT_PLS_MASK	(0xf << 5)
#define XDEV_U0		(0x0 << 5)
#define XDEV_U1		(0x1 << 5)
#define XDEV_U2		(0x2 << 5)
#define XDEV_U3		(0x3 << 5)
#define XDEV_DISABLED	(0x4 << 5)
#define XDEV_RXDETECT	(0x5 << 5)
#define XDEV_INACTIVE	(0x6 << 5)
#define XDEV_POLLING	(0x7 << 5)
#define XDEV_RECOVERY	(0x8 << 5)
#define XDEV_HOT_RESET	(0x9 << 5)
#define XDEV_COMP_MODE	(0xa << 5)
#define XDEV_TEST_MODE	(0xb << 5)
#define XDEV_RESUME	(0xf << 5)

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
#define DEV_SPEED_MASK		(0xf << 10)
#define	XDEV_FS			(0x1 << 10)
#define	XDEV_LS			(0x2 << 10)
#define	XDEV_HS			(0x3 << 10)
#define	XDEV_SS			(0x4 << 10)
#define	XDEV_SSP		(0x5 << 10)
#define DEV_UNDEFSPEED(p)	(((p) & DEV_SPEED_MASK) == (0x0<<10))
#define DEV_FULLSPEED(p)	(((p) & DEV_SPEED_MASK) == XDEV_FS)
#define DEV_LOWSPEED(p)		(((p) & DEV_SPEED_MASK) == XDEV_LS)
#define DEV_HIGHSPEED(p)	(((p) & DEV_SPEED_MASK) == XDEV_HS)
#define DEV_SUPERSPEED(p)	(((p) & DEV_SPEED_MASK) == XDEV_SS)
#define DEV_SUPERSPEEDPLUS(p)	(((p) & DEV_SPEED_MASK) == XDEV_SSP)
#define DEV_SUPERSPEED_ANY(p)	(((p) & DEV_SPEED_MASK) >= XDEV_SS)
#define DEV_PORT_SPEED(p)	(((p) >> 10) & 0x0f)

/* Bits 20:23 in the Slot Context are the speed for the device */
#define	SLOT_SPEED_FS		(XDEV_FS << 10)
#define	SLOT_SPEED_LS		(XDEV_LS << 10)
#define	SLOT_SPEED_HS		(XDEV_HS << 10)
#define	SLOT_SPEED_SS		(XDEV_SS << 10)
#define	SLOT_SPEED_SSP		(XDEV_SSP << 10)
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
#define PORT_CHANGE_MASK	(PORT_CSC | PORT_PEC | PORT_WRC | PORT_OCC | \
				 PORT_RC | PORT_PLC | PORT_CEC)


/* Cold Attach Status - xHC can set this bit to report device attached during
 * Sx state. Warm port reset should be perfomed to clear this bit and move port
 * to connected state.
 */
#define PORT_CAS	(1 << 24)
/* wake on connect (enable) */
#define PORT_WKCONN_E	(1 << 25)
/* wake on disconnect (enable) */
#define PORT_WKDISC_E	(1 << 26)
/* wake on over-current (enable) */
#define PORT_WKOC_E	(1 << 27)
/* bits 28:29 reserved */
/* true: device is non-removable - for USB 3.0 roothub emulation */
#define PORT_DEV_REMOVE	(1 << 30)
/* Initiate a warm port reset - complete when PORT_WRC is '1' */
#define PORT_WR		(1 << 31)

/* We mark duplicate entries with -1 */
#define DUPLICATE_ENTRY ((u8)(-1))

/* Port Power Management Status and Control - port_power_base bitmasks */
/* Inactivity timer value for transitions into U1, in microseconds.
 * Timeout can be up to 127us.  0xFF means an infinite timeout.
 */
#define PORT_U1_TIMEOUT(p)	((p) & 0xff)
#define PORT_U1_TIMEOUT_MASK	0xff
/* Inactivity timer value for transitions into U2 */
#define PORT_U2_TIMEOUT(p)	(((p) & 0xff) << 8)
#define PORT_U2_TIMEOUT_MASK	(0xff << 8)
/* Bits 24:31 for port testing */

/* USB2 Protocol PORTSPMSC */
#define	PORT_L1S_MASK		7
#define	PORT_L1S_SUCCESS	1
#define	PORT_RWE		(1 << 3)
#define	PORT_HIRD(p)		(((p) & 0xf) << 4)
#define	PORT_HIRD_MASK		(0xf << 4)
#define	PORT_L1DS_MASK		(0xff << 8)
#define	PORT_L1DS(p)		(((p) & 0xff) << 8)
#define	PORT_HLE		(1 << 16)
#define PORT_TEST_MODE_SHIFT	28

/* USB3 Protocol PORTLI  Port Link Information */
#define PORT_RX_LANES(p)	(((p) >> 16) & 0xf)
#define PORT_TX_LANES(p)	(((p) >> 20) & 0xf)

/* USB2 Protocol PORTHLPMC */
#define PORT_HIRDM(p)((p) & 3)
#define PORT_L1_TIMEOUT(p)(((p) & 0xff) << 2)
#define PORT_BESLD(p)(((p) & 0xf) << 10)

/* use 512 microseconds as USB2 LPM L1 default timeout. */
#define XHCI_L1_TIMEOUT		512

/* Set default HIRD/BESL value to 4 (350/400us) for USB2 L1 LPM resume latency.
 * Safe to use with mixed HIRD and BESL systems (host and device) and is used
 * by other operating systems.
 *
 * XHCI 1.0 errata 8/14/12 Table 13 notes:
 * "Software should choose xHC BESL/BESLD field values that do not violate a
 * device's resume latency requirements,
 * e.g. not program values > '4' if BLC = '1' and a HIRD device is attached,
 * or not program values < '4' if BLC = '0' and a BESL device is attached.
 */
#define XHCI_DEFAULT_BESL	4

/*
 * USB3 specification define a 360ms tPollingLFPSTiemout for USB3 ports
 * to complete link training. usually link trainig completes much faster
 * so check status 10 times with 36ms sleep in places we need to wait for
 * polling to complete.
 */
#define XHCI_PORT_POLLING_LFPS_TIME  36

/**
 * struct xhci_intr_reg - Interrupt Register Set
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
struct xhci_intr_reg {
	__le32	irq_pending;
	__le32	irq_control;
	__le32	erst_size;
	__le32	rsvd;
	__le64	erst_base;
	__le64	erst_dequeue;
};

/* irq_pending bitmasks */
#define	ER_IRQ_PENDING(p)	((p) & 0x1)
/* bits 2:31 need to be preserved */
/* THIS IS BUGGY - FIXME - IP IS WRITE 1 TO CLEAR */
#define	ER_IRQ_CLEAR(p)		((p) & 0xfffffffe)
#define	ER_IRQ_ENABLE(p)	((ER_IRQ_CLEAR(p)) | 0x2)
#define	ER_IRQ_DISABLE(p)	((ER_IRQ_CLEAR(p)) & ~(0x2))

/* irq_control bitmasks */
/* Minimum interval between interrupts (in 250ns intervals).  The interval
 * between interrupts will be longer if there are no events on the event ring.
 * Default is 4000 (1 ms).
 */
#define ER_IRQ_INTERVAL_MASK	(0xffff)
/* Counter used to count down the time to the next interrupt - HW use only */
#define ER_IRQ_COUNTER_MASK	(0xffff << 16)

/* erst_size bitmasks */
/* Preserve bits 16:31 of erst_size */
#define	ERST_SIZE_MASK		(0xffff << 16)

/* erst_dequeue bitmasks */
/* Dequeue ERST Segment Index (DESI) - Segment number (or alias)
 * where the current dequeue pointer lies.  This is an optional HW hint.
 */
#define ERST_DESI_MASK		(0x7)
/* Event Handler Busy (EHB) - is the event ring scheduled to be serviced by
 * a work queue (or delayed service routine)?
 */
#define ERST_EHB		(1 << 3)
#define ERST_PTR_MASK		(0xf)

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
	__le32			microframe_index;
	__le32			rsvd[7];
	struct xhci_intr_reg	ir_set[128];
};

/**
 * struct doorbell_array
 *
 * Bits  0 -  7: Endpoint target
 * Bits  8 - 15: RsvdZ
 * Bits 16 - 31: Stream ID
 *
 * Section 5.6
 */
struct xhci_doorbell_array {
	__le32	doorbell[256];
};

#define DB_VALUE(ep, stream)	((((ep) + 1) & 0xff) | ((stream) << 16))
#define DB_VALUE_HOST		0x00000000

/**
 * struct xhci_protocol_caps
 * @revision:		major revision, minor revision, capability ID,
 *			and next capability pointer.
 * @name_string:	Four ASCII characters to say which spec this xHC
 *			follows, typically "USB ".
 * @port_info:		Port offset, count, and protocol-defined information.
 */
struct xhci_protocol_caps {
	u32	revision;
	u32	name_string;
	u32	port_info;
};

#define	XHCI_EXT_PORT_MAJOR(x)	(((x) >> 24) & 0xff)
#define	XHCI_EXT_PORT_MINOR(x)	(((x) >> 16) & 0xff)
#define	XHCI_EXT_PORT_PSIC(x)	(((x) >> 28) & 0x0f)
#define	XHCI_EXT_PORT_OFF(x)	((x) & 0xff)
#define	XHCI_EXT_PORT_COUNT(x)	(((x) >> 8) & 0xff)

#define	XHCI_EXT_PORT_PSIV(x)	(((x) >> 0) & 0x0f)
#define	XHCI_EXT_PORT_PSIE(x)	(((x) >> 4) & 0x03)
#define	XHCI_EXT_PORT_PLT(x)	(((x) >> 6) & 0x03)
#define	XHCI_EXT_PORT_PFD(x)	(((x) >> 8) & 0x01)
#define	XHCI_EXT_PORT_LP(x)	(((x) >> 14) & 0x03)
#define	XHCI_EXT_PORT_PSIM(x)	(((x) >> 16) & 0xffff)

#define PLT_MASK        (0x03 << 6)
#define PLT_SYM         (0x00 << 6)
#define PLT_ASYM_RX     (0x02 << 6)
#define PLT_ASYM_TX     (0x03 << 6)

/**
 * struct xhci_container_ctx
 * @type: Type of context.  Used to calculated offsets to contained contexts.
 * @size: Size of the context data
 * @bytes: The raw context data given to HW
 * @dma: dma address of the bytes
 *
 * Represents either a Device or Input context.  Holds a pointer to the raw
 * memory used for the context (bytes) and dma address of it (dma).
 */
struct xhci_container_ctx {
	unsigned type;
#define XHCI_CTX_TYPE_DEVICE  0x1
#define XHCI_CTX_TYPE_INPUT   0x2

	int size;

	u8 *bytes;
	dma_addr_t dma;
};

/**
 * struct xhci_slot_ctx
 * @dev_info:	Route string, device speed, hub info, and last valid endpoint
 * @dev_info2:	Max exit latency for device number, root hub port number
 * @tt_info:	tt_info is used to construct split transaction tokens
 * @dev_state:	slot state and device address
 *
 * Slot Context - section 6.2.1.1.  This assumes the HC uses 32-byte context
 * structures.  If the HC uses 64-byte contexts, there is an additional 32 bytes
 * reserved at the end of the slot context for HC internal use.
 */
struct xhci_slot_ctx {
	__le32	dev_info;
	__le32	dev_info2;
	__le32	tt_info;
	__le32	dev_state;
	/* offset 0x10 to 0x1f reserved for HC internal use */
	__le32	reserved[4];
};

/* dev_info bitmasks */
/* Route String - 0:19 */
#define ROUTE_STRING_MASK	(0xfffff)
/* Device speed - values defined by PORTSC Device Speed field - 20:23 */
#define DEV_SPEED	(0xf << 20)
#define GET_DEV_SPEED(n) (((n) & DEV_SPEED) >> 20)
/* bit 24 reserved */
/* Is this LS/FS device connected through a HS hub? - bit 25 */
#define DEV_MTT		(0x1 << 25)
/* Set if the device is a hub - bit 26 */
#define DEV_HUB		(0x1 << 26)
/* Index of the last valid endpoint context in this device context - 27:31 */
#define LAST_CTX_MASK	(0x1f << 27)
#define LAST_CTX(p)	((p) << 27)
#define LAST_CTX_TO_EP_NUM(p)	(((p) >> 27) - 1)
#define SLOT_FLAG	(1 << 0)
#define EP0_FLAG	(1 << 1)

/* dev_info2 bitmasks */
/* Max Exit Latency (ms) - worst case time to wake up all links in dev path */
#define MAX_EXIT	(0xffff)
/* Root hub port number that is needed to access the USB device */
#define ROOT_HUB_PORT(p)	(((p) & 0xff) << 16)
#define DEVINFO_TO_ROOT_HUB_PORT(p)	(((p) >> 16) & 0xff)
/* Maximum number of ports under a hub device */
#define XHCI_MAX_PORTS(p)	(((p) & 0xff) << 24)
#define DEVINFO_TO_MAX_PORTS(p)	(((p) & (0xff << 24)) >> 24)

/* tt_info bitmasks */
/*
 * TT Hub Slot ID - for low or full speed devices attached to a high-speed hub
 * The Slot ID of the hub that isolates the high speed signaling from
 * this low or full-speed device.  '0' if attached to root hub port.
 */
#define TT_SLOT		(0xff)
/*
 * The number of the downstream facing port of the high-speed hub
 * '0' if the device is not low or full speed.
 */
#define TT_PORT		(0xff << 8)
#define TT_THINK_TIME(p)	(((p) & 0x3) << 16)
#define GET_TT_THINK_TIME(p)	(((p) & (0x3 << 16)) >> 16)

/* dev_state bitmasks */
/* USB device address - assigned by the HC */
#define DEV_ADDR_MASK	(0xff)
/* bits 8:26 reserved */
/* Slot state */
#define SLOT_STATE	(0x1f << 27)
#define GET_SLOT_STATE(p)	(((p) & (0x1f << 27)) >> 27)

#define SLOT_STATE_DISABLED	0
#define SLOT_STATE_ENABLED	SLOT_STATE_DISABLED
#define SLOT_STATE_DEFAULT	1
#define SLOT_STATE_ADDRESSED	2
#define SLOT_STATE_CONFIGURED	3

/**
 * struct xhci_ep_ctx
 * @ep_info:	endpoint state, streams, mult, and interval information.
 * @ep_info2:	information on endpoint type, max packet size, max burst size,
 * 		error count, and whether the HC will force an event for all
 * 		transactions.
 * @deq:	64-bit ring dequeue pointer address.  If the endpoint only
 * 		defines one stream, this points to the endpoint transfer ring.
 * 		Otherwise, it points to a stream context array, which has a
 * 		ring pointer for each flow.
 * @tx_info:
 * 		Average TRB lengths for the endpoint ring and
 * 		max payload within an Endpoint Service Interval Time (ESIT).
 *
 * Endpoint Context - section 6.2.1.2.  This assumes the HC uses 32-byte context
 * structures.  If the HC uses 64-byte contexts, there is an additional 32 bytes
 * reserved at the end of the endpoint context for HC internal use.
 */
struct xhci_ep_ctx {
	__le32	ep_info;
	__le32	ep_info2;
	__le64	deq;
	__le32	tx_info;
	/* offset 0x14 - 0x1f reserved for HC internal use */
	__le32	reserved[3];
};

/* ep_info bitmasks */
/*
 * Endpoint State - bits 0:2
 * 0 - disabled
 * 1 - running
 * 2 - halted due to halt condition - ok to manipulate endpoint ring
 * 3 - stopped
 * 4 - TRB error
 * 5-7 - reserved
 */
#define EP_STATE_MASK		(0x7)
#define EP_STATE_DISABLED	0
#define EP_STATE_RUNNING	1
#define EP_STATE_HALTED		2
#define EP_STATE_STOPPED	3
#define EP_STATE_ERROR		4
#define GET_EP_CTX_STATE(ctx)	(le32_to_cpu((ctx)->ep_info) & EP_STATE_MASK)

/* Mult - Max number of burtst within an interval, in EP companion desc. */
#define EP_MULT(p)		(((p) & 0x3) << 8)
#define CTX_TO_EP_MULT(p)	(((p) >> 8) & 0x3)
/* bits 10:14 are Max Primary Streams */
/* bit 15 is Linear Stream Array */
/* Interval - period between requests to an endpoint - 125u increments. */
#define EP_INTERVAL(p)			(((p) & 0xff) << 16)
#define EP_INTERVAL_TO_UFRAMES(p)	(1 << (((p) >> 16) & 0xff))
#define CTX_TO_EP_INTERVAL(p)		(((p) >> 16) & 0xff)
#define EP_MAXPSTREAMS_MASK		(0x1f << 10)
#define EP_MAXPSTREAMS(p)		(((p) << 10) & EP_MAXPSTREAMS_MASK)
#define CTX_TO_EP_MAXPSTREAMS(p)	(((p) & EP_MAXPSTREAMS_MASK) >> 10)
/* Endpoint is set up with a Linear Stream Array (vs. Secondary Stream Array) */
#define	EP_HAS_LSA		(1 << 15)
/* hosts with LEC=1 use bits 31:24 as ESIT high bits. */
#define CTX_TO_MAX_ESIT_PAYLOAD_HI(p)	(((p) >> 24) & 0xff)

/* ep_info2 bitmasks */
/*
 * Force Event - generate transfer events for all TRBs for this endpoint
 * This will tell the HC to ignore the IOC and ISP flags (for debugging only).
 */
#define	FORCE_EVENT	(0x1)
#define ERROR_COUNT(p)	(((p) & 0x3) << 1)
#define CTX_TO_EP_TYPE(p)	(((p) >> 3) & 0x7)
#define EP_TYPE(p)	((p) << 3)
#define ISOC_OUT_EP	1
#define BULK_OUT_EP	2
#define INT_OUT_EP	3
#define CTRL_EP		4
#define ISOC_IN_EP	5
#define BULK_IN_EP	6
#define INT_IN_EP	7
/* bit 6 reserved */
/* bit 7 is Host Initiate Disable - for disabling stream selection */
#define MAX_BURST(p)	(((p)&0xff) << 8)
#define CTX_TO_MAX_BURST(p)	(((p) >> 8) & 0xff)
#define MAX_PACKET(p)	(((p)&0xffff) << 16)
#define MAX_PACKET_MASK		(0xffff << 16)
#define MAX_PACKET_DECODED(p)	(((p) >> 16) & 0xffff)

/* tx_info bitmasks */
#define EP_AVG_TRB_LENGTH(p)		((p) & 0xffff)
#define EP_MAX_ESIT_PAYLOAD_LO(p)	(((p) & 0xffff) << 16)
#define EP_MAX_ESIT_PAYLOAD_HI(p)	((((p) >> 16) & 0xff) << 24)
#define CTX_TO_MAX_ESIT_PAYLOAD(p)	(((p) >> 16) & 0xffff)

/* deq bitmasks */
#define EP_CTX_CYCLE_MASK		(1 << 0)
#define SCTX_DEQ_MASK			(~0xfL)


/**
 * struct xhci_input_control_context
 * Input control context; see section 6.2.5.
 *
 * @drop_context:	set the bit of the endpoint context you want to disable
 * @add_context:	set the bit of the endpoint context you want to enable
 */
struct xhci_input_control_ctx {
	__le32	drop_flags;
	__le32	add_flags;
	__le32	rsvd2[6];
};

#define	EP_IS_ADDED(ctrl_ctx, i) \
	(le32_to_cpu(ctrl_ctx->add_flags) & (1 << (i + 1)))
#define	EP_IS_DROPPED(ctrl_ctx, i)       \
	(le32_to_cpu(ctrl_ctx->drop_flags) & (1 << (i + 1)))

/* Represents everything that is needed to issue a command on the command ring.
 * It's useful to pre-allocate these for commands that cannot fail due to
 * out-of-memory errors, like freeing streams.
 */
struct xhci_command {
	/* Input context for changing device state */
	struct xhci_container_ctx	*in_ctx;
	u32				status;
	int				slot_id;
	/* If completion is null, no one is waiting on this command
	 * and the structure can be freed after the command completes.
	 */
	struct completion		*completion;
	union xhci_trb			*command_trb;
	struct list_head		cmd_list;
};

/* drop context bitmasks */
#define	DROP_EP(x)	(0x1 << x)
/* add context bitmasks */
#define	ADD_EP(x)	(0x1 << x)

struct xhci_stream_ctx {
	/* 64-bit stream ring address, cycle state, and stream type */
	__le64	stream_ring;
	/* offset 0x14 - 0x1f reserved for HC internal use */
	__le32	reserved[2];
};

/* Stream Context Types (section 6.4.1) - bits 3:1 of stream ctx deq ptr */
#define	SCT_FOR_CTX(p)		(((p) & 0x7) << 1)
/* Secondary stream array type, dequeue pointer is to a transfer ring */
#define	SCT_SEC_TR		0
/* Primary stream array type, dequeue pointer is to a transfer ring */
#define	SCT_PRI_TR		1
/* Dequeue pointer is for a secondary stream array (SSA) with 8 entries */
#define SCT_SSA_8		2
#define SCT_SSA_16		3
#define SCT_SSA_32		4
#define SCT_SSA_64		5
#define SCT_SSA_128		6
#define SCT_SSA_256		7

/* Assume no secondary streams for now */
struct xhci_stream_info {
	struct xhci_ring		**stream_rings;
	/* Number of streams, including stream 0 (which drivers can't use) */
	unsigned int			num_streams;
	/* The stream context array may be bigger than
	 * the number of streams the driver asked for
	 */
	struct xhci_stream_ctx		*stream_ctx_array;
	unsigned int			num_stream_ctxs;
	dma_addr_t			ctx_array_dma;
	/* For mapping physical TRB addresses to segments in stream rings */
	struct radix_tree_root		trb_address_map;
	struct xhci_command		*free_streams_command;
};

#define	SMALL_STREAM_ARRAY_SIZE		256
#define	MEDIUM_STREAM_ARRAY_SIZE	1024

/* Some Intel xHCI host controllers need software to keep track of the bus
 * bandwidth.  Keep track of endpoint info here.  Each root port is allocated
 * the full bus bandwidth.  We must also treat TTs (including each port under a
 * multi-TT hub) as a separate bandwidth domain.  The direct memory interface
 * (DMI) also limits the total bandwidth (across all domains) that can be used.
 */
struct xhci_bw_info {
	/* ep_interval is zero-based */
	unsigned int		ep_interval;
	/* mult and num_packets are one-based */
	unsigned int		mult;
	unsigned int		num_packets;
	unsigned int		max_packet_size;
	unsigned int		max_esit_payload;
	unsigned int		type;
};

/* "Block" sizes in bytes the hardware uses for different device speeds.
 * The logic in this part of the hardware limits the number of bits the hardware
 * can use, so must represent bandwidth in a less precise manner to mimic what
 * the scheduler hardware computes.
 */
#define	FS_BLOCK	1
#define	HS_BLOCK	4
#define	SS_BLOCK	16
#define	DMI_BLOCK	32

/* Each device speed has a protocol overhead (CRC, bit stuffing, etc) associated
 * with each byte transferred.  SuperSpeed devices have an initial overhead to
 * set up bursts.  These are in blocks, see above.  LS overhead has already been
 * translated into FS blocks.
 */
#define DMI_OVERHEAD 8
#define DMI_OVERHEAD_BURST 4
#define SS_OVERHEAD 8
#define SS_OVERHEAD_BURST 32
#define HS_OVERHEAD 26
#define FS_OVERHEAD 20
#define LS_OVERHEAD 128
/* The TTs need to claim roughly twice as much bandwidth (94 bytes per
 * microframe ~= 24Mbps) of the HS bus as the devices can actually use because
 * of overhead associated with split transfers crossing microframe boundaries.
 * 31 blocks is pure protocol overhead.
 */
#define TT_HS_OVERHEAD (31 + 94)
#define TT_DMI_OVERHEAD (25 + 12)

/* Bandwidth limits in blocks */
#define FS_BW_LIMIT		1285
#define TT_BW_LIMIT		1320
#define HS_BW_LIMIT		1607
#define SS_BW_LIMIT_IN		3906
#define DMI_BW_LIMIT_IN		3906
#define SS_BW_LIMIT_OUT		3906
#define DMI_BW_LIMIT_OUT	3906

/* Percentage of bus bandwidth reserved for non-periodic transfers */
#define FS_BW_RESERVED		10
#define HS_BW_RESERVED		20
#define SS_BW_RESERVED		10

struct xhci_virt_ep {
	struct xhci_virt_device		*vdev;	/* parent */
	unsigned int			ep_index;
	struct xhci_ring		*ring;
	/* Related to endpoints that are configured to use stream IDs only */
	struct xhci_stream_info		*stream_info;
	/* Temporary storage in case the configure endpoint command fails and we
	 * have to restore the device state to the previous state
	 */
	struct xhci_ring		*new_ring;
	unsigned int			ep_state;
#define SET_DEQ_PENDING		(1 << 0)
#define EP_HALTED		(1 << 1)	/* For stall handling */
#define EP_STOP_CMD_PENDING	(1 << 2)	/* For URB cancellation */
/* Transitioning the endpoint to using streams, don't enqueue URBs */
#define EP_GETTING_STREAMS	(1 << 3)
#define EP_HAS_STREAMS		(1 << 4)
/* Transitioning the endpoint to not using streams, don't enqueue URBs */
#define EP_GETTING_NO_STREAMS	(1 << 5)
#define EP_HARD_CLEAR_TOGGLE	(1 << 6)
#define EP_SOFT_CLEAR_TOGGLE	(1 << 7)
/* usb_hub_clear_tt_buffer is in progress */
#define EP_CLEARING_TT		(1 << 8)
	/* ----  Related to URB cancellation ---- */
	struct list_head	cancelled_td_list;
	/* Watchdog timer for stop endpoint command to cancel URBs */
	struct timer_list	stop_cmd_timer;
	struct xhci_hcd		*xhci;
	/* Dequeue pointer and dequeue segment for a submitted Set TR Dequeue
	 * command.  We'll need to update the ring's dequeue segment and dequeue
	 * pointer after the command completes.
	 */
	struct xhci_segment	*queued_deq_seg;
	union xhci_trb		*queued_deq_ptr;
	/*
	 * Sometimes the xHC can not process isochronous endpoint ring quickly
	 * enough, and it will miss some isoc tds on the ring and generate
	 * a Missed Service Error Event.
	 * Set skip flag when receive a Missed Service Error Event and
	 * process the missed tds on the endpoint ring.
	 */
	bool			skip;
	/* Bandwidth checking storage */
	struct xhci_bw_info	bw_info;
	struct list_head	bw_endpoint_list;
	/* Isoch Frame ID checking storage */
	int			next_frame_id;
	/* Use new Isoch TRB layout needed for extended TBC support */
	bool			use_extended_tbc;
};

enum xhci_overhead_type {
	LS_OVERHEAD_TYPE = 0,
	FS_OVERHEAD_TYPE,
	HS_OVERHEAD_TYPE,
};

struct xhci_interval_bw {
	unsigned int		num_packets;
	/* Sorted by max packet size.
	 * Head of the list is the greatest max packet size.
	 */
	struct list_head	endpoints;
	/* How many endpoints of each speed are present. */
	unsigned int		overhead[3];
};

#define	XHCI_MAX_INTERVAL	16

struct xhci_interval_bw_table {
	unsigned int		interval0_esit_payload;
	struct xhci_interval_bw	interval_bw[XHCI_MAX_INTERVAL];
	/* Includes reserved bandwidth for async endpoints */
	unsigned int		bw_used;
	unsigned int		ss_bw_in;
	unsigned int		ss_bw_out;
};

#define EP_CTX_PER_DEV		31

struct xhci_virt_device {
	int				slot_id;
	struct usb_device		*udev;
	/*
	 * Commands to the hardware are passed an "input context" that
	 * tells the hardware what to change in its data structures.
	 * The hardware will return changes in an "output context" that
	 * software must allocate for the hardware.  We need to keep
	 * track of input and output contexts separately because
	 * these commands might fail and we don't trust the hardware.
	 */
	struct xhci_container_ctx       *out_ctx;
	/* Used for addressing devices and configuration changes */
	struct xhci_container_ctx       *in_ctx;
	struct xhci_virt_ep		eps[EP_CTX_PER_DEV];
	u8				fake_port;
	u8				real_port;
	struct xhci_interval_bw_table	*bw_table;
	struct xhci_tt_bw_info		*tt_info;
	/*
	 * flags for state tracking based on events and issued commands.
	 * Software can not rely on states from output contexts because of
	 * latency between events and xHC updating output context values.
	 * See xhci 1.1 section 4.8.3 for more details
	 */
	unsigned long			flags;
#define VDEV_PORT_ERROR			BIT(0) /* Port error, link inactive */

	/* The current max exit latency for the enabled USB3 link states. */
	u16				current_mel;
	/* Used for the debugfs interfaces. */
	void				*debugfs_private;
};

/*
 * For each roothub, keep track of the bandwidth information for each periodic
 * interval.
 *
 * If a high speed hub is attached to the roothub, each TT associated with that
 * hub is a separate bandwidth domain.  The interval information for the
 * endpoints on the devices under that TT will appear in the TT structure.
 */
struct xhci_root_port_bw_info {
	struct list_head		tts;
	unsigned int			num_active_tts;
	struct xhci_interval_bw_table	bw_table;
};

struct xhci_tt_bw_info {
	struct list_head		tt_list;
	int				slot_id;
	int				ttport;
	struct xhci_interval_bw_table	bw_table;
	int				active_eps;
};


/**
 * struct xhci_device_context_array
 * @dev_context_ptr	array of 64-bit DMA addresses for device contexts
 */
struct xhci_device_context_array {
	/* 64-bit device addresses; we only write 32-bit addresses */
	__le64			dev_context_ptrs[MAX_HC_SLOTS];
	/* private xHCD pointers */
	dma_addr_t	dma;
};
/* TODO: write function to set the 64-bit device DMA address */
/*
 * TODO: change this to be dynamically sized at HC mem init time since the HC
 * might not be able to handle the maximum number of devices possible.
 */


struct xhci_transfer_event {
	/* 64-bit buffer address, or immediate data */
	__le64	buffer;
	__le32	transfer_len;
	/* This field is interpreted differently based on the type of TRB */
	__le32	flags;
};

/* Transfer event TRB length bit mask */
/* bits 0:23 */
#define	EVENT_TRB_LEN(p)		((p) & 0xffffff)

/** Transfer Event bit fields **/
#define	TRB_TO_EP_ID(p)	(((p) >> 16) & 0x1f)

/* Completion Code - only applicable for some types of TRBs */
#define	COMP_CODE_MASK		(0xff << 24)
#define GET_COMP_CODE(p)	(((p) & COMP_CODE_MASK) >> 24)
#define COMP_INVALID				0
#define COMP_SUCCESS				1
#define COMP_DATA_BUFFER_ERROR			2
#define COMP_BABBLE_DETECTED_ERROR		3
#define COMP_USB_TRANSACTION_ERROR		4
#define COMP_TRB_ERROR				5
#define COMP_STALL_ERROR			6
#define COMP_RESOURCE_ERROR			7
#define COMP_BANDWIDTH_ERROR			8
#define COMP_NO_SLOTS_AVAILABLE_ERROR		9
#define COMP_INVALID_STREAM_TYPE_ERROR		10
#define COMP_SLOT_NOT_ENABLED_ERROR		11
#define COMP_ENDPOINT_NOT_ENABLED_ERROR		12
#define COMP_SHORT_PACKET			13
#define COMP_RING_UNDERRUN			14
#define COMP_RING_OVERRUN			15
#define COMP_VF_EVENT_RING_FULL_ERROR		16
#define COMP_PARAMETER_ERROR			17
#define COMP_BANDWIDTH_OVERRUN_ERROR		18
#define COMP_CONTEXT_STATE_ERROR		19
#define COMP_NO_PING_RESPONSE_ERROR		20
#define COMP_EVENT_RING_FULL_ERROR		21
#define COMP_INCOMPATIBLE_DEVICE_ERROR		22
#define COMP_MISSED_SERVICE_ERROR		23
#define COMP_COMMAND_RING_STOPPED		24
#define COMP_COMMAND_ABORTED			25
#define COMP_STOPPED				26
#define COMP_STOPPED_LENGTH_INVALID		27
#define COMP_STOPPED_SHORT_PACKET		28
#define COMP_MAX_EXIT_LATENCY_TOO_LARGE_ERROR	29
#define COMP_ISOCH_BUFFER_OVERRUN		31
#define COMP_EVENT_LOST_ERROR			32
#define COMP_UNDEFINED_ERROR			33
#define COMP_INVALID_STREAM_ID_ERROR		34
#define COMP_SECONDARY_BANDWIDTH_ERROR		35
#define COMP_SPLIT_TRANSACTION_ERROR		36

static inline const char *xhci_trb_comp_code_string(u8 status)
{
	switch (status) {
	case COMP_INVALID:
		return "Invalid";
	case COMP_SUCCESS:
		return "Success";
	case COMP_DATA_BUFFER_ERROR:
		return "Data Buffer Error";
	case COMP_BABBLE_DETECTED_ERROR:
		return "Babble Detected";
	case COMP_USB_TRANSACTION_ERROR:
		return "USB Transaction Error";
	case COMP_TRB_ERROR:
		return "TRB Error";
	case COMP_STALL_ERROR:
		return "Stall Error";
	case COMP_RESOURCE_ERROR:
		return "Resource Error";
	case COMP_BANDWIDTH_ERROR:
		return "Bandwidth Error";
	case COMP_NO_SLOTS_AVAILABLE_ERROR:
		return "No Slots Available Error";
	case COMP_INVALID_STREAM_TYPE_ERROR:
		return "Invalid Stream Type Error";
	case COMP_SLOT_NOT_ENABLED_ERROR:
		return "Slot Not Enabled Error";
	case COMP_ENDPOINT_NOT_ENABLED_ERROR:
		return "Endpoint Not Enabled Error";
	case COMP_SHORT_PACKET:
		return "Short Packet";
	case COMP_RING_UNDERRUN:
		return "Ring Underrun";
	case COMP_RING_OVERRUN:
		return "Ring Overrun";
	case COMP_VF_EVENT_RING_FULL_ERROR:
		return "VF Event Ring Full Error";
	case COMP_PARAMETER_ERROR:
		return "Parameter Error";
	case COMP_BANDWIDTH_OVERRUN_ERROR:
		return "Bandwidth Overrun Error";
	case COMP_CONTEXT_STATE_ERROR:
		return "Context State Error";
	case COMP_NO_PING_RESPONSE_ERROR:
		return "No Ping Response Error";
	case COMP_EVENT_RING_FULL_ERROR:
		return "Event Ring Full Error";
	case COMP_INCOMPATIBLE_DEVICE_ERROR:
		return "Incompatible Device Error";
	case COMP_MISSED_SERVICE_ERROR:
		return "Missed Service Error";
	case COMP_COMMAND_RING_STOPPED:
		return "Command Ring Stopped";
	case COMP_COMMAND_ABORTED:
		return "Command Aborted";
	case COMP_STOPPED:
		return "Stopped";
	case COMP_STOPPED_LENGTH_INVALID:
		return "Stopped - Length Invalid";
	case COMP_STOPPED_SHORT_PACKET:
		return "Stopped - Short Packet";
	case COMP_MAX_EXIT_LATENCY_TOO_LARGE_ERROR:
		return "Max Exit Latency Too Large Error";
	case COMP_ISOCH_BUFFER_OVERRUN:
		return "Isoch Buffer Overrun";
	case COMP_EVENT_LOST_ERROR:
		return "Event Lost Error";
	case COMP_UNDEFINED_ERROR:
		return "Undefined Error";
	case COMP_INVALID_STREAM_ID_ERROR:
		return "Invalid Stream ID Error";
	case COMP_SECONDARY_BANDWIDTH_ERROR:
		return "Secondary Bandwidth Error";
	case COMP_SPLIT_TRANSACTION_ERROR:
		return "Split Transaction Error";
	default:
		return "Unknown!!";
	}
}

struct xhci_link_trb {
	/* 64-bit segment pointer*/
	__le64 segment_ptr;
	__le32 intr_target;
	__le32 control;
};

/* control bitfields */
#define LINK_TOGGLE	(0x1<<1)

/* Command completion event TRB */
struct xhci_event_cmd {
	/* Pointer to command TRB, or the value passed by the event data trb */
	__le64 cmd_trb;
	__le32 status;
	__le32 flags;
};

/* flags bitmasks */

/* Address device - disable SetAddress */
#define TRB_BSR		(1<<9)

/* Configure Endpoint - Deconfigure */
#define TRB_DC		(1<<9)

/* Stop Ring - Transfer State Preserve */
#define TRB_TSP		(1<<9)

enum xhci_ep_reset_type {
	EP_HARD_RESET,
	EP_SOFT_RESET,
};

/* Force Event */
#define TRB_TO_VF_INTR_TARGET(p)	(((p) & (0x3ff << 22)) >> 22)
#define TRB_TO_VF_ID(p)			(((p) & (0xff << 16)) >> 16)

/* Set Latency Tolerance Value */
#define TRB_TO_BELT(p)			(((p) & (0xfff << 16)) >> 16)

/* Get Port Bandwidth */
#define TRB_TO_DEV_SPEED(p)		(((p) & (0xf << 16)) >> 16)

/* Force Header */
#define TRB_TO_PACKET_TYPE(p)		((p) & 0x1f)
#define TRB_TO_ROOTHUB_PORT(p)		(((p) & (0xff << 24)) >> 24)

enum xhci_setup_dev {
	SETUP_CONTEXT_ONLY,
	SETUP_CONTEXT_ADDRESS,
};

/* bits 16:23 are the virtual function ID */
/* bits 24:31 are the slot ID */
#define TRB_TO_SLOT_ID(p)	(((p) & (0xff<<24)) >> 24)
#define SLOT_ID_FOR_TRB(p)	(((p) & 0xff) << 24)

/* Stop Endpoint TRB - ep_index to endpoint ID for this TRB */
#define TRB_TO_EP_INDEX(p)		((((p) & (0x1f << 16)) >> 16) - 1)
#define	EP_ID_FOR_TRB(p)		((((p) + 1) & 0x1f) << 16)

#define SUSPEND_PORT_FOR_TRB(p)		(((p) & 1) << 23)
#define TRB_TO_SUSPEND_PORT(p)		(((p) & (1 << 23)) >> 23)
#define LAST_EP_INDEX			30

/* Set TR Dequeue Pointer command TRB fields, 6.4.3.9 */
#define TRB_TO_STREAM_ID(p)		((((p) & (0xffff << 16)) >> 16))
#define STREAM_ID_FOR_TRB(p)		((((p)) & 0xffff) << 16)
#define SCT_FOR_TRB(p)			(((p) << 1) & 0x7)

/* Link TRB specific fields */
#define TRB_TC			(1<<1)

/* Port Status Change Event TRB fields */
/* Port ID - bits 31:24 */
#define GET_PORT_ID(p)		(((p) & (0xff << 24)) >> 24)

#define EVENT_DATA		(1 << 2)

/* Normal TRB fields */
/* transfer_len bitmasks - bits 0:16 */
#define	TRB_LEN(p)		((p) & 0x1ffff)
/* TD Size, packets remaining in this TD, bits 21:17 (5 bits, so max 31) */
#define TRB_TD_SIZE(p)          (min((p), (u32)31) << 17)
#define GET_TD_SIZE(p)		(((p) & 0x3e0000) >> 17)
/* xhci 1.1 uses the TD_SIZE field for TBC if Extended TBC is enabled (ETE) */
#define TRB_TD_SIZE_TBC(p)      (min((p), (u32)31) << 17)
/* Interrupter Target - which MSI-X vector to target the completion event at */
#define TRB_INTR_TARGET(p)	(((p) & 0x3ff) << 22)
#define GET_INTR_TARGET(p)	(((p) >> 22) & 0x3ff)
/* Total burst count field, Rsvdz on xhci 1.1 with Extended TBC enabled (ETE) */
#define TRB_TBC(p)		(((p) & 0x3) << 7)
#define TRB_TLBPC(p)		(((p) & 0xf) << 16)

/* Cycle bit - indicates TRB ownership by HC or HCD */
#define TRB_CYCLE		(1<<0)
/*
 * Force next event data TRB to be evaluated before task switch.
 * Used to pass OS data back after a TD completes.
 */
#define TRB_ENT			(1<<1)
/* Interrupt on short packet */
#define TRB_ISP			(1<<2)
/* Set PCIe no snoop attribute */
#define TRB_NO_SNOOP		(1<<3)
/* Chain multiple TRBs into a TD */
#define TRB_CHAIN		(1<<4)
/* Interrupt on completion */
#define TRB_IOC			(1<<5)
/* The buffer pointer contains immediate data */
#define TRB_IDT			(1<<6)
/* TDs smaller than this might use IDT */
#define TRB_IDT_MAX_SIZE	8

/* Block Event Interrupt */
#define	TRB_BEI			(1<<9)

/* Control transfer TRB specific fields */
#define TRB_DIR_IN		(1<<16)
#define	TRB_TX_TYPE(p)		((p) << 16)
#define	TRB_DATA_OUT		2
#define	TRB_DATA_IN		3

/* Isochronous TRB specific fields */
#define TRB_SIA			(1<<31)
#define TRB_FRAME_ID(p)		(((p) & 0x7ff) << 20)

/* TRB cache size for xHC with TRB cache */
#define TRB_CACHE_SIZE_HS	8
#define TRB_CACHE_SIZE_SS	16

struct xhci_generic_trb {
	__le32 field[4];
};

union xhci_trb {
	struct xhci_link_trb		link;
	struct xhci_transfer_event	trans_event;
	struct xhci_event_cmd		event_cmd;
	struct xhci_generic_trb		generic;
};

/* TRB bit mask */
#define	TRB_TYPE_BITMASK	(0xfc00)
#define TRB_TYPE(p)		((p) << 10)
#define TRB_FIELD_TO_TYPE(p)	(((p) & TRB_TYPE_BITMASK) >> 10)
/* TRB type IDs */
/* bulk, interrupt, isoc scatter/gather, and control data stage */
#define TRB_NORMAL		1
/* setup stage for control transfers */
#define TRB_SETUP		2
/* data stage for control transfers */
#define TRB_DATA		3
/* status stage for control transfers */
#define TRB_STATUS		4
/* isoc transfers */
#define TRB_ISOC		5
/* TRB for linking ring segments */
#define TRB_LINK		6
#define TRB_EVENT_DATA		7
/* Transfer Ring No-op (not for the command ring) */
#define TRB_TR_NOOP		8
/* Command TRBs */
/* Enable Slot Command */
#define TRB_ENABLE_SLOT		9
/* Disable Slot Command */
#define TRB_DISABLE_SLOT	10
/* Address Device Command */
#define TRB_ADDR_DEV		11
/* Configure Endpoint Command */
#define TRB_CONFIG_EP		12
/* Evaluate Context Command */
#define TRB_EVAL_CONTEXT	13
/* Reset Endpoint Command */
#define TRB_RESET_EP		14
/* Stop Transfer Ring Command */
#define TRB_STOP_RING		15
/* Set Transfer Ring Dequeue Pointer Command */
#define TRB_SET_DEQ		16
/* Reset Device Command */
#define TRB_RESET_DEV		17
/* Force Event Command (opt) */
#define TRB_FORCE_EVENT		18
/* Negotiate Bandwidth Command (opt) */
#define TRB_NEG_BANDWIDTH	19
/* Set Latency Tolerance Value Command (opt) */
#define TRB_SET_LT		20
/* Get port bandwidth Command */
#define TRB_GET_BW		21
/* Force Header Command - generate a transaction or link management packet */
#define TRB_FORCE_HEADER	22
/* No-op Command - not for transfer rings */
#define TRB_CMD_NOOP		23
/* TRB IDs 24-31 reserved */
/* Event TRBS */
/* Transfer Event */
#define TRB_TRANSFER		32
/* Command Completion Event */
#define TRB_COMPLETION		33
/* Port Status Change Event */
#define TRB_PORT_STATUS		34
/* Bandwidth Request Event (opt) */
#define TRB_BANDWIDTH_EVENT	35
/* Doorbell Event (opt) */
#define TRB_DOORBELL		36
/* Host Controller Event */
#define TRB_HC_EVENT		37
/* Device Notification Event - device sent function wake notification */
#define TRB_DEV_NOTE		38
/* MFINDEX Wrap Event - microframe counter wrapped */
#define TRB_MFINDEX_WRAP	39
/* TRB IDs 40-47 reserved, 48-63 is vendor-defined */
#define TRB_VENDOR_DEFINED_LOW	48
/* Nec vendor-specific command completion event. */
#define	TRB_NEC_CMD_COMP	48
/* Get NEC firmware revision. */
#define	TRB_NEC_GET_FW		49

static inline const char *xhci_trb_type_string(u8 type)
{
	switch (type) {
	case TRB_NORMAL:
		return "Normal";
	case TRB_SETUP:
		return "Setup Stage";
	case TRB_DATA:
		return "Data Stage";
	case TRB_STATUS:
		return "Status Stage";
	case TRB_ISOC:
		return "Isoch";
	case TRB_LINK:
		return "Link";
	case TRB_EVENT_DATA:
		return "Event Data";
	case TRB_TR_NOOP:
		return "No-Op";
	case TRB_ENABLE_SLOT:
		return "Enable Slot Command";
	case TRB_DISABLE_SLOT:
		return "Disable Slot Command";
	case TRB_ADDR_DEV:
		return "Address Device Command";
	case TRB_CONFIG_EP:
		return "Configure Endpoint Command";
	case TRB_EVAL_CONTEXT:
		return "Evaluate Context Command";
	case TRB_RESET_EP:
		return "Reset Endpoint Command";
	case TRB_STOP_RING:
		return "Stop Ring Command";
	case TRB_SET_DEQ:
		return "Set TR Dequeue Pointer Command";
	case TRB_RESET_DEV:
		return "Reset Device Command";
	case TRB_FORCE_EVENT:
		return "Force Event Command";
	case TRB_NEG_BANDWIDTH:
		return "Negotiate Bandwidth Command";
	case TRB_SET_LT:
		return "Set Latency Tolerance Value Command";
	case TRB_GET_BW:
		return "Get Port Bandwidth Command";
	case TRB_FORCE_HEADER:
		return "Force Header Command";
	case TRB_CMD_NOOP:
		return "No-Op Command";
	case TRB_TRANSFER:
		return "Transfer Event";
	case TRB_COMPLETION:
		return "Command Completion Event";
	case TRB_PORT_STATUS:
		return "Port Status Change Event";
	case TRB_BANDWIDTH_EVENT:
		return "Bandwidth Request Event";
	case TRB_DOORBELL:
		return "Doorbell Event";
	case TRB_HC_EVENT:
		return "Host Controller Event";
	case TRB_DEV_NOTE:
		return "Device Notification Event";
	case TRB_MFINDEX_WRAP:
		return "MFINDEX Wrap Event";
	case TRB_NEC_CMD_COMP:
		return "NEC Command Completion Event";
	case TRB_NEC_GET_FW:
		return "NET Get Firmware Revision Command";
	default:
		return "UNKNOWN";
	}
}

#define TRB_TYPE_LINK(x)	(((x) & TRB_TYPE_BITMASK) == TRB_TYPE(TRB_LINK))
/* Above, but for __le32 types -- can avoid work by swapping constants: */
#define TRB_TYPE_LINK_LE32(x)	(((x) & cpu_to_le32(TRB_TYPE_BITMASK)) == \
				 cpu_to_le32(TRB_TYPE(TRB_LINK)))
#define TRB_TYPE_NOOP_LE32(x)	(((x) & cpu_to_le32(TRB_TYPE_BITMASK)) == \
				 cpu_to_le32(TRB_TYPE(TRB_TR_NOOP)))

#define NEC_FW_MINOR(p)		(((p) >> 0) & 0xff)
#define NEC_FW_MAJOR(p)		(((p) >> 8) & 0xff)

/*
 * TRBS_PER_SEGMENT must be a multiple of 4,
 * since the command ring is 64-byte aligned.
 * It must also be greater than 16.
 */
#define TRBS_PER_SEGMENT	256
/* Allow two commands + a link TRB, along with any reserved command TRBs */
#define MAX_RSVD_CMD_TRBS	(TRBS_PER_SEGMENT - 3)
#define TRB_SEGMENT_SIZE	(TRBS_PER_SEGMENT*16)
#define TRB_SEGMENT_SHIFT	(ilog2(TRB_SEGMENT_SIZE))
/* TRB buffer pointers can't cross 64KB boundaries */
#define TRB_MAX_BUFF_SHIFT		16
#define TRB_MAX_BUFF_SIZE	(1 << TRB_MAX_BUFF_SHIFT)
/* How much data is left before the 64KB boundary? */
#define TRB_BUFF_LEN_UP_TO_BOUNDARY(addr)	(TRB_MAX_BUFF_SIZE - \
					(addr & (TRB_MAX_BUFF_SIZE - 1)))
#define MAX_SOFT_RETRY		3
/*
 * Limits of consecutive isoc trbs that can Block Event Interrupt (BEI) if
 * XHCI_AVOID_BEI quirk is in use.
 */
#define AVOID_BEI_INTERVAL_MIN	8
#define AVOID_BEI_INTERVAL_MAX	32

struct xhci_segment {
	union xhci_trb		*trbs;
	/* private to HCD */
	struct xhci_segment	*next;
	dma_addr_t		dma;
	/* Max packet sized bounce buffer for td-fragmant alignment */
	dma_addr_t		bounce_dma;
	void			*bounce_buf;
	unsigned int		bounce_offs;
	unsigned int		bounce_len;
};

enum xhci_cancelled_td_status {
	TD_DIRTY = 0,
	TD_HALTED,
	TD_CLEARING_CACHE,
	TD_CLEARED,
};

struct xhci_td {
	struct list_head	td_list;
	struct list_head	cancelled_td_list;
	int			status;
	enum xhci_cancelled_td_status	cancel_status;
	struct urb		*urb;
	struct xhci_segment	*start_seg;
	union xhci_trb		*first_trb;
	union xhci_trb		*last_trb;
	struct xhci_segment	*last_trb_seg;
	struct xhci_segment	*bounce_seg;
	/* actual_length of the URB has already been set */
	bool			urb_length_set;
	unsigned int		num_trbs;
};

/* xHCI command default timeout value */
#define XHCI_CMD_DEFAULT_TIMEOUT	(5 * HZ)

/* command descriptor */
struct xhci_cd {
	struct xhci_command	*command;
	union xhci_trb		*cmd_trb;
};

enum xhci_ring_type {
	TYPE_CTRL = 0,
	TYPE_ISOC,
	TYPE_BULK,
	TYPE_INTR,
	TYPE_STREAM,
	TYPE_COMMAND,
	TYPE_EVENT,
};

static inline const char *xhci_ring_type_string(enum xhci_ring_type type)
{
	switch (type) {
	case TYPE_CTRL:
		return "CTRL";
	case TYPE_ISOC:
		return "ISOC";
	case TYPE_BULK:
		return "BULK";
	case TYPE_INTR:
		return "INTR";
	case TYPE_STREAM:
		return "STREAM";
	case TYPE_COMMAND:
		return "CMD";
	case TYPE_EVENT:
		return "EVENT";
	}

	return "UNKNOWN";
}

struct xhci_ring {
	struct xhci_segment	*first_seg;
	struct xhci_segment	*last_seg;
	union  xhci_trb		*enqueue;
	struct xhci_segment	*enq_seg;
	union  xhci_trb		*dequeue;
	struct xhci_segment	*deq_seg;
	struct list_head	td_list;
	/*
	 * Write the cycle state into the TRB cycle field to give ownership of
	 * the TRB to the host controller (if we are the producer), or to check
	 * if we own the TRB (if we are the consumer).  See section 4.9.1.
	 */
	u32			cycle_state;
	unsigned int            err_count;
	unsigned int		stream_id;
	unsigned int		num_segs;
	unsigned int		num_trbs_free;
	unsigned int		num_trbs_free_temp;
	unsigned int		bounce_buf_len;
	enum xhci_ring_type	type;
	bool			last_td_was_short;
	struct radix_tree_root	*trb_address_map;
};

struct xhci_erst_entry {
	/* 64-bit event ring segment address */
	__le64	seg_addr;
	__le32	seg_size;
	/* Set to zero */
	__le32	rsvd;
};

struct xhci_erst {
	struct xhci_erst_entry	*entries;
	unsigned int		num_entries;
	/* xhci->event_ring keeps track of segment dma addresses */
	dma_addr_t		erst_dma_addr;
	/* Num entries the ERST can contain */
	unsigned int		erst_size;
};

struct xhci_scratchpad {
	u64 *sp_array;
	dma_addr_t sp_dma;
	void **sp_buffers;
};

struct urb_priv {
	int	num_tds;
	int	num_tds_done;
	struct	xhci_td	td[];
};

/*
 * Each segment table entry is 4*32bits long.  1K seems like an ok size:
 * (1K bytes * 8bytes/bit) / (4*32 bits) = 64 segment entries in the table,
 * meaning 64 ring segments.
 * Initial allocated size of the ERST, in number of entries */
#define	ERST_NUM_SEGS	1
/* Poll every 60 seconds */
#define	POLL_TIMEOUT	60
/* Stop endpoint command timeout (secs) for URB cancellation watchdog timer */
#define XHCI_STOP_EP_CMD_TIMEOUT	5
/* XXX: Make these module parameters */

struct s3_save {
	u32	command;
	u32	dev_nt;
	u64	dcbaa_ptr;
	u32	config_reg;
	u32	irq_pending;
	u32	irq_control;
	u32	erst_size;
	u64	erst_base;
	u64	erst_dequeue;
};

/* Use for lpm */
struct dev_info {
	u32			dev_id;
	struct	list_head	list;
};

struct xhci_bus_state {
	unsigned long		bus_suspended;
	unsigned long		next_statechange;

	/* Port suspend arrays are indexed by the portnum of the fake roothub */
	/* ports suspend status arrays - max 31 ports for USB2, 15 for USB3 */
	u32			port_c_suspend;
	u32			suspended_ports;
	u32			port_remote_wakeup;
	unsigned long		resume_done[USB_MAXCHILDREN];
	/* which ports have started to resume */
	unsigned long		resuming_ports;
	/* Which ports are waiting on RExit to U0 transition. */
	unsigned long		rexit_ports;
	struct completion	rexit_done[USB_MAXCHILDREN];
	struct completion	u3exit_done[USB_MAXCHILDREN];
};


/*
 * It can take up to 20 ms to transition from RExit to U0 on the
 * Intel Lynx Point LP xHCI host.
 */
#define	XHCI_MAX_REXIT_TIMEOUT_MS	20
struct xhci_port_cap {
	u32			*psi;	/* array of protocol speed ID entries */
	u8			psi_count;
	u8			psi_uid_count;
	u8			maj_rev;
	u8			min_rev;
};

struct xhci_port {
	__le32 __iomem		*addr;
	int			hw_portnum;
	int			hcd_portnum;
	struct xhci_hub		*rhub;
	struct xhci_port_cap	*port_cap;
};

struct xhci_hub {
	struct xhci_port	**ports;
	unsigned int		num_ports;
	struct usb_hcd		*hcd;
	/* keep track of bus suspend info */
	struct xhci_bus_state   bus_state;
	/* supported prococol extended capabiliy values */
	u8			maj_rev;
	u8			min_rev;
};

/* There is one xhci_hcd structure per controller */
struct xhci_hcd {
	struct usb_hcd *main_hcd;
	struct usb_hcd *shared_hcd;
	/* glue to PCI and HCD framework */
	struct xhci_cap_regs __iomem *cap_regs;
	struct xhci_op_regs __iomem *op_regs;
	struct xhci_run_regs __iomem *run_regs;
	struct xhci_doorbell_array __iomem *dba;
	/* Our HCD's current interrupter register set */
	struct	xhci_intr_reg __iomem *ir_set;

	/* Cached register copies of read-only HC data */
	__u32		hcs_params1;
	__u32		hcs_params2;
	__u32		hcs_params3;
	__u32		hcc_params;
	__u32		hcc_params2;

	spinlock_t	lock;

	/* packed release number */
	u8		sbrn;
	u16		hci_version;
	u8		max_slots;
	u8		max_interrupters;
	u8		max_ports;
	u8		isoc_threshold;
	/* imod_interval in ns (I * 250ns) */
	u32		imod_interval;
	u32		isoc_bei_interval;
	int		event_ring_max;
	/* 4KB min, 128MB max */
	int		page_size;
	/* Valid values are 12 to 20, inclusive */
	int		page_shift;
	/* msi-x vectors */
	int		msix_count;
	/* optional clocks */
	struct clk		*clk;
	struct clk		*reg_clk;
	/* optional reset controller */
	struct reset_control *reset;
	/* data structures */
	struct xhci_device_context_array *dcbaa;
	struct xhci_ring	*cmd_ring;
	unsigned int            cmd_ring_state;
#define CMD_RING_STATE_RUNNING         (1 << 0)
#define CMD_RING_STATE_ABORTED         (1 << 1)
#define CMD_RING_STATE_STOPPED         (1 << 2)
	struct list_head        cmd_list;
	unsigned int		cmd_ring_reserved_trbs;
	struct delayed_work	cmd_timer;
	struct completion	cmd_ring_stop_completion;
	struct xhci_command	*current_cmd;
	struct xhci_ring	*event_ring;
	struct xhci_erst	erst;
	/* Scratchpad */
	struct xhci_scratchpad  *scratchpad;
	/* Store LPM test failed devices' information */
	struct list_head	lpm_failed_devs;

	/* slot enabling and address device helpers */
	/* these are not thread safe so use mutex */
	struct mutex mutex;
	/* For USB 3.0 LPM enable/disable. */
	struct xhci_command		*lpm_command;
	/* Internal mirror of the HW's dcbaa */
	struct xhci_virt_device	*devs[MAX_HC_SLOTS];
	/* For keeping track of bandwidth domains per roothub. */
	struct xhci_root_port_bw_info	*rh_bw;

	/* DMA pools */
	struct dma_pool	*device_pool;
	struct dma_pool	*segment_pool;
	struct dma_pool	*small_streams_pool;
	struct dma_pool	*medium_streams_pool;

	/* Host controller watchdog timer structures */
	unsigned int		xhc_state;

	u32			command;
	struct s3_save		s3;
/* Host controller is dying - not responding to commands. "I'm not dead yet!"
 *
 * xHC interrupts have been disabled and a watchdog timer will (or has already)
 * halt the xHCI host, and complete all URBs with an -ESHUTDOWN code.  Any code
 * that sees this status (other than the timer that set it) should stop touching
 * hardware immediately.  Interrupt handlers should return immediately when
 * they see this status (any time they drop and re-acquire xhci->lock).
 * xhci_urb_dequeue() should call usb_hcd_check_unlink_urb() and return without
 * putting the TD on the canceled list, etc.
 *
 * There are no reports of xHCI host controllers that display this issue.
 */
#define XHCI_STATE_DYING	(1 << 0)
#define XHCI_STATE_HALTED	(1 << 1)
#define XHCI_STATE_REMOVING	(1 << 2)
	unsigned long long	quirks;
#define	XHCI_LINK_TRB_QUIRK	BIT_ULL(0)
#define XHCI_RESET_EP_QUIRK	BIT_ULL(1)
#define XHCI_NEC_HOST		BIT_ULL(2)
#define XHCI_AMD_PLL_FIX	BIT_ULL(3)
#define XHCI_SPURIOUS_SUCCESS	BIT_ULL(4)
/*
 * Certain Intel host controllers have a limit to the number of endpoint
 * contexts they can handle.  Ideally, they would signal that they can't handle
 * anymore endpoint contexts by returning a Resource Error for the Configure
 * Endpoint command, but they don't.  Instead they expect software to keep track
 * of the number of active endpoints for them, across configure endpoint
 * commands, reset device commands, disable slot commands, and address device
 * commands.
 */
#define XHCI_EP_LIMIT_QUIRK	BIT_ULL(5)
#define XHCI_BROKEN_MSI		BIT_ULL(6)
#define XHCI_RESET_ON_RESUME	BIT_ULL(7)
#define	XHCI_SW_BW_CHECKING	BIT_ULL(8)
#define XHCI_AMD_0x96_HOST	BIT_ULL(9)
#define XHCI_TRUST_TX_LENGTH	BIT_ULL(10)
#define XHCI_LPM_SUPPORT	BIT_ULL(11)
#define XHCI_INTEL_HOST		BIT_ULL(12)
#define XHCI_SPURIOUS_REBOOT	BIT_ULL(13)
#define XHCI_COMP_MODE_QUIRK	BIT_ULL(14)
#define XHCI_AVOID_BEI		BIT_ULL(15)
#define XHCI_PLAT		BIT_ULL(16)
#define XHCI_SLOW_SUSPEND	BIT_ULL(17)
#define XHCI_SPURIOUS_WAKEUP	BIT_ULL(18)
/* For controllers with a broken beyond repair streams implementation */
#define XHCI_BROKEN_STREAMS	BIT_ULL(19)
#define XHCI_PME_STUCK_QUIRK	BIT_ULL(20)
#define XHCI_MTK_HOST		BIT_ULL(21)
#define XHCI_SSIC_PORT_UNUSED	BIT_ULL(22)
#define XHCI_NO_64BIT_SUPPORT	BIT_ULL(23)
#define XHCI_MISSING_CAS	BIT_ULL(24)
/* For controller with a broken Port Disable implementation */
#define XHCI_BROKEN_PORT_PED	BIT_ULL(25)
#define XHCI_LIMIT_ENDPOINT_INTERVAL_7	BIT_ULL(26)
#define XHCI_U2_DISABLE_WAKE	BIT_ULL(27)
#define XHCI_ASMEDIA_MODIFY_FLOWCONTROL	BIT_ULL(28)
#define XHCI_HW_LPM_DISABLE	BIT_ULL(29)
#define XHCI_SUSPEND_DELAY	BIT_ULL(30)
#define XHCI_INTEL_USB_ROLE_SW	BIT_ULL(31)
#define XHCI_ZERO_64B_REGS	BIT_ULL(32)
#define XHCI_DEFAULT_PM_RUNTIME_ALLOW	BIT_ULL(33)
#define XHCI_RESET_PLL_ON_DISCONNECT	BIT_ULL(34)
#define XHCI_SNPS_BROKEN_SUSPEND    BIT_ULL(35)
#define XHCI_RENESAS_FW_QUIRK	BIT_ULL(36)
#define XHCI_SKIP_PHY_INIT	BIT_ULL(37)
#define XHCI_DISABLE_SPARSE	BIT_ULL(38)
#define XHCI_SG_TRB_CACHE_SIZE_QUIRK	BIT_ULL(39)
#define XHCI_NO_SOFT_RETRY	BIT_ULL(40)
#define XHCI_BROKEN_D3COLD	BIT_ULL(41)
#define XHCI_EP_CTX_BROKEN_DCS	BIT_ULL(42)

	unsigned int		num_active_eps;
	unsigned int		limit_active_eps;
	struct xhci_port	*hw_ports;
	struct xhci_hub		usb2_rhub;
	struct xhci_hub		usb3_rhub;
	/* support xHCI 1.0 spec USB2 hardware LPM */
	unsigned		hw_lpm_support:1;
	/* Broken Suspend flag for SNPS Suspend resume issue */
	unsigned		broken_suspend:1;
	/* cached usb2 extened protocol capabilites */
	u32                     *ext_caps;
	unsigned int            num_ext_caps;
	/* cached extended protocol port capabilities */
	struct xhci_port_cap	*port_caps;
	unsigned int		num_port_caps;
	/* Compliance Mode Recovery Data */
	struct timer_list	comp_mode_recovery_timer;
	u32			port_status_u0;
	u16			test_mode;
/* Compliance Mode Timer Triggered every 2 seconds */
#define COMP_MODE_RCVRY_MSECS 2000

	struct dentry		*debugfs_root;
	struct dentry		*debugfs_slots;
	struct list_head	regset_list;

	void			*dbc;
	/* platform-specific data -- must come last */
	unsigned long		priv[] __aligned(sizeof(s64));
};

/* Platform specific overrides to generic XHCI hc_driver ops */
struct xhci_driver_overrides {
	size_t extra_priv_size;
	int (*reset)(struct usb_hcd *hcd);
	int (*start)(struct usb_hcd *hcd);
	int (*add_endpoint)(struct usb_hcd *hcd, struct usb_device *udev,
			    struct usb_host_endpoint *ep);
	int (*drop_endpoint)(struct usb_hcd *hcd, struct usb_device *udev,
			     struct usb_host_endpoint *ep);
	int (*check_bandwidth)(struct usb_hcd *, struct usb_device *);
	void (*reset_bandwidth)(struct usb_hcd *, struct usb_device *);
};

#define	XHCI_CFC_DELAY		10

/* convert between an HCD pointer and the corresponding EHCI_HCD */
static inline struct xhci_hcd *hcd_to_xhci(struct usb_hcd *hcd)
{
	struct usb_hcd *primary_hcd;

	if (usb_hcd_is_primary_hcd(hcd))
		primary_hcd = hcd;
	else
		primary_hcd = hcd->primary_hcd;

	return (struct xhci_hcd *) (primary_hcd->hcd_priv);
}

static inline struct usb_hcd *xhci_to_hcd(struct xhci_hcd *xhci)
{
	return xhci->main_hcd;
}

#define xhci_dbg(xhci, fmt, args...) \
	dev_dbg(xhci_to_hcd(xhci)->self.controller , fmt , ## args)
#define xhci_err(xhci, fmt, args...) \
	dev_err(xhci_to_hcd(xhci)->self.controller , fmt , ## args)
#define xhci_warn(xhci, fmt, args...) \
	dev_warn(xhci_to_hcd(xhci)->self.controller , fmt , ## args)
#define xhci_warn_ratelimited(xhci, fmt, args...) \
	dev_warn_ratelimited(xhci_to_hcd(xhci)->self.controller , fmt , ## args)
#define xhci_info(xhci, fmt, args...) \
	dev_info(xhci_to_hcd(xhci)->self.controller , fmt , ## args)

/*
 * Registers should always be accessed with double word or quad word accesses.
 *
 * Some xHCI implementations may support 64-bit address pointers.  Registers
 * with 64-bit address pointers should be written to with dword accesses by
 * writing the low dword first (ptr[0]), then the high dword (ptr[1]) second.
 * xHCI implementations that do not support 64-bit address pointers will ignore
 * the high dword, and write order is irrelevant.
 */
static inline u64 xhci_read_64(const struct xhci_hcd *xhci,
		__le64 __iomem *regs)
{
	return lo_hi_readq(regs);
}
static inline void xhci_write_64(struct xhci_hcd *xhci,
				 const u64 val, __le64 __iomem *regs)
{
	lo_hi_writeq(val, regs);
}

static inline int xhci_link_trb_quirk(struct xhci_hcd *xhci)
{
	return xhci->quirks & XHCI_LINK_TRB_QUIRK;
}

/* xHCI debugging */
char *xhci_get_slot_state(struct xhci_hcd *xhci,
		struct xhci_container_ctx *ctx);
void xhci_dbg_trace(struct xhci_hcd *xhci, void (*trace)(struct va_format *),
			const char *fmt, ...);

/* xHCI memory management */
void xhci_mem_cleanup(struct xhci_hcd *xhci);
int xhci_mem_init(struct xhci_hcd *xhci, gfp_t flags);
void xhci_free_virt_device(struct xhci_hcd *xhci, int slot_id);
int xhci_alloc_virt_device(struct xhci_hcd *xhci, int slot_id, struct usb_device *udev, gfp_t flags);
int xhci_setup_addressable_virt_dev(struct xhci_hcd *xhci, struct usb_device *udev);
void xhci_copy_ep0_dequeue_into_input_ctx(struct xhci_hcd *xhci,
		struct usb_device *udev);
unsigned int xhci_get_endpoint_index(struct usb_endpoint_descriptor *desc);
unsigned int xhci_get_endpoint_address(unsigned int ep_index);
unsigned int xhci_last_valid_endpoint(u32 added_ctxs);
void xhci_endpoint_zero(struct xhci_hcd *xhci, struct xhci_virt_device *virt_dev, struct usb_host_endpoint *ep);
void xhci_update_tt_active_eps(struct xhci_hcd *xhci,
		struct xhci_virt_device *virt_dev,
		int old_active_eps);
void xhci_clear_endpoint_bw_info(struct xhci_bw_info *bw_info);
void xhci_update_bw_info(struct xhci_hcd *xhci,
		struct xhci_container_ctx *in_ctx,
		struct xhci_input_control_ctx *ctrl_ctx,
		struct xhci_virt_device *virt_dev);
void xhci_endpoint_copy(struct xhci_hcd *xhci,
		struct xhci_container_ctx *in_ctx,
		struct xhci_container_ctx *out_ctx,
		unsigned int ep_index);
void xhci_slot_copy(struct xhci_hcd *xhci,
		struct xhci_container_ctx *in_ctx,
		struct xhci_container_ctx *out_ctx);
int xhci_endpoint_init(struct xhci_hcd *xhci, struct xhci_virt_device *virt_dev,
		struct usb_device *udev, struct usb_host_endpoint *ep,
		gfp_t mem_flags);
struct xhci_ring *xhci_ring_alloc(struct xhci_hcd *xhci,
		unsigned int num_segs, unsigned int cycle_state,
		enum xhci_ring_type type, unsigned int max_packet, gfp_t flags);
void xhci_ring_free(struct xhci_hcd *xhci, struct xhci_ring *ring);
int xhci_ring_expansion(struct xhci_hcd *xhci, struct xhci_ring *ring,
		unsigned int num_trbs, gfp_t flags);
int xhci_alloc_erst(struct xhci_hcd *xhci,
		struct xhci_ring *evt_ring,
		struct xhci_erst *erst,
		gfp_t flags);
void xhci_initialize_ring_info(struct xhci_ring *ring,
			unsigned int cycle_state);
void xhci_free_erst(struct xhci_hcd *xhci, struct xhci_erst *erst);
void xhci_free_endpoint_ring(struct xhci_hcd *xhci,
		struct xhci_virt_device *virt_dev,
		unsigned int ep_index);
struct xhci_stream_info *xhci_alloc_stream_info(struct xhci_hcd *xhci,
		unsigned int num_stream_ctxs,
		unsigned int num_streams,
		unsigned int max_packet, gfp_t flags);
void xhci_free_stream_info(struct xhci_hcd *xhci,
		struct xhci_stream_info *stream_info);
void xhci_setup_streams_ep_input_ctx(struct xhci_hcd *xhci,
		struct xhci_ep_ctx *ep_ctx,
		struct xhci_stream_info *stream_info);
void xhci_setup_no_streams_ep_input_ctx(struct xhci_ep_ctx *ep_ctx,
		struct xhci_virt_ep *ep);
void xhci_free_device_endpoint_resources(struct xhci_hcd *xhci,
	struct xhci_virt_device *virt_dev, bool drop_control_ep);
struct xhci_ring *xhci_dma_to_transfer_ring(
		struct xhci_virt_ep *ep,
		u64 address);
struct xhci_command *xhci_alloc_command(struct xhci_hcd *xhci,
		bool allocate_completion, gfp_t mem_flags);
struct xhci_command *xhci_alloc_command_with_ctx(struct xhci_hcd *xhci,
		bool allocate_completion, gfp_t mem_flags);
void xhci_urb_free_priv(struct urb_priv *urb_priv);
void xhci_free_command(struct xhci_hcd *xhci,
		struct xhci_command *command);
struct xhci_container_ctx *xhci_alloc_container_ctx(struct xhci_hcd *xhci,
		int type, gfp_t flags);
void xhci_free_container_ctx(struct xhci_hcd *xhci,
		struct xhci_container_ctx *ctx);

/* xHCI host controller glue */
typedef void (*xhci_get_quirks_t)(struct device *, struct xhci_hcd *);
int xhci_handshake(void __iomem *ptr, u32 mask, u32 done, u64 timeout_us);
void xhci_quiesce(struct xhci_hcd *xhci);
int xhci_halt(struct xhci_hcd *xhci);
int xhci_start(struct xhci_hcd *xhci);
int xhci_reset(struct xhci_hcd *xhci, u64 timeout_us);
int xhci_run(struct usb_hcd *hcd);
int xhci_gen_setup(struct usb_hcd *hcd, xhci_get_quirks_t get_quirks);
void xhci_shutdown(struct usb_hcd *hcd);
void xhci_init_driver(struct hc_driver *drv,
		      const struct xhci_driver_overrides *over);
int xhci_add_endpoint(struct usb_hcd *hcd, struct usb_device *udev,
		      struct usb_host_endpoint *ep);
int xhci_drop_endpoint(struct usb_hcd *hcd, struct usb_device *udev,
		       struct usb_host_endpoint *ep);
int xhci_check_bandwidth(struct usb_hcd *hcd, struct usb_device *udev);
void xhci_reset_bandwidth(struct usb_hcd *hcd, struct usb_device *udev);
int xhci_disable_slot(struct xhci_hcd *xhci, u32 slot_id);
int xhci_ext_cap_init(struct xhci_hcd *xhci);

int xhci_suspend(struct xhci_hcd *xhci, bool do_wakeup);
int xhci_resume(struct xhci_hcd *xhci, bool hibernated);

irqreturn_t xhci_irq(struct usb_hcd *hcd);
irqreturn_t xhci_msi_irq(int irq, void *hcd);
int xhci_alloc_dev(struct usb_hcd *hcd, struct usb_device *udev);
int xhci_alloc_tt_info(struct xhci_hcd *xhci,
		struct xhci_virt_device *virt_dev,
		struct usb_device *hdev,
		struct usb_tt *tt, gfp_t mem_flags);

/* xHCI ring, segment, TRB, and TD functions */
dma_addr_t xhci_trb_virt_to_dma(struct xhci_segment *seg, union xhci_trb *trb);
struct xhci_segment *trb_in_td(struct xhci_hcd *xhci,
		struct xhci_segment *start_seg, union xhci_trb *start_trb,
		union xhci_trb *end_trb, dma_addr_t suspect_dma, bool debug);
int xhci_is_vendor_info_code(struct xhci_hcd *xhci, unsigned int trb_comp_code);
void xhci_ring_cmd_db(struct xhci_hcd *xhci);
int xhci_queue_slot_control(struct xhci_hcd *xhci, struct xhci_command *cmd,
		u32 trb_type, u32 slot_id);
int xhci_queue_address_device(struct xhci_hcd *xhci, struct xhci_command *cmd,
		dma_addr_t in_ctx_ptr, u32 slot_id, enum xhci_setup_dev);
int xhci_queue_vendor_command(struct xhci_hcd *xhci, struct xhci_command *cmd,
		u32 field1, u32 field2, u32 field3, u32 field4);
int xhci_queue_stop_endpoint(struct xhci_hcd *xhci, struct xhci_command *cmd,
		int slot_id, unsigned int ep_index, int suspend);
int xhci_queue_ctrl_tx(struct xhci_hcd *xhci, gfp_t mem_flags, struct urb *urb,
		int slot_id, unsigned int ep_index);
int xhci_queue_bulk_tx(struct xhci_hcd *xhci, gfp_t mem_flags, struct urb *urb,
		int slot_id, unsigned int ep_index);
int xhci_queue_intr_tx(struct xhci_hcd *xhci, gfp_t mem_flags, struct urb *urb,
		int slot_id, unsigned int ep_index);
int xhci_queue_isoc_tx_prepare(struct xhci_hcd *xhci, gfp_t mem_flags,
		struct urb *urb, int slot_id, unsigned int ep_index);
int xhci_queue_configure_endpoint(struct xhci_hcd *xhci,
		struct xhci_command *cmd, dma_addr_t in_ctx_ptr, u32 slot_id,
		bool command_must_succeed);
int xhci_queue_evaluate_context(struct xhci_hcd *xhci, struct xhci_command *cmd,
		dma_addr_t in_ctx_ptr, u32 slot_id, bool command_must_succeed);
int xhci_queue_reset_ep(struct xhci_hcd *xhci, struct xhci_command *cmd,
		int slot_id, unsigned int ep_index,
		enum xhci_ep_reset_type reset_type);
int xhci_queue_reset_device(struct xhci_hcd *xhci, struct xhci_command *cmd,
		u32 slot_id);
void xhci_cleanup_stalled_ring(struct xhci_hcd *xhci, unsigned int slot_id,
			       unsigned int ep_index, unsigned int stream_id,
			       struct xhci_td *td);
void xhci_stop_endpoint_command_watchdog(struct timer_list *t);
void xhci_handle_command_timeout(struct work_struct *work);

void xhci_ring_ep_doorbell(struct xhci_hcd *xhci, unsigned int slot_id,
		unsigned int ep_index, unsigned int stream_id);
void xhci_ring_doorbell_for_active_rings(struct xhci_hcd *xhci,
		unsigned int slot_id,
		unsigned int ep_index);
void xhci_cleanup_command_queue(struct xhci_hcd *xhci);
void inc_deq(struct xhci_hcd *xhci, struct xhci_ring *ring);
unsigned int count_trbs(u64 addr, u64 len);

/* xHCI roothub code */
void xhci_set_link_state(struct xhci_hcd *xhci, struct xhci_port *port,
				u32 link_state);
void xhci_test_and_clear_bit(struct xhci_hcd *xhci, struct xhci_port *port,
				u32 port_bit);
int xhci_hub_control(struct usb_hcd *hcd, u16 typeReq, u16 wValue, u16 wIndex,
		char *buf, u16 wLength);
int xhci_hub_status_data(struct usb_hcd *hcd, char *buf);
int xhci_find_raw_port_number(struct usb_hcd *hcd, int port1);
struct xhci_hub *xhci_get_rhub(struct usb_hcd *hcd);
void xhci_set_port_power(struct xhci_hcd *xhci, struct usb_hcd *hcd, u16 index,
			 bool on, unsigned long *flags);

void xhci_hc_died(struct xhci_hcd *xhci);

#ifdef CONFIG_PM
int xhci_bus_suspend(struct usb_hcd *hcd);
int xhci_bus_resume(struct usb_hcd *hcd);
unsigned long xhci_get_resuming_ports(struct usb_hcd *hcd);
#else
#define	xhci_bus_suspend	NULL
#define	xhci_bus_resume		NULL
#define	xhci_get_resuming_ports	NULL
#endif	/* CONFIG_PM */

u32 xhci_port_state_to_neutral(u32 state);
int xhci_find_slot_id_by_port(struct usb_hcd *hcd, struct xhci_hcd *xhci,
		u16 port);
void xhci_ring_device(struct xhci_hcd *xhci, int slot_id);

/* xHCI contexts */
struct xhci_input_control_ctx *xhci_get_input_control_ctx(struct xhci_container_ctx *ctx);
struct xhci_slot_ctx *xhci_get_slot_ctx(struct xhci_hcd *xhci, struct xhci_container_ctx *ctx);
struct xhci_ep_ctx *xhci_get_ep_ctx(struct xhci_hcd *xhci, struct xhci_container_ctx *ctx, unsigned int ep_index);

struct xhci_ring *xhci_triad_to_transfer_ring(struct xhci_hcd *xhci,
		unsigned int slot_id, unsigned int ep_index,
		unsigned int stream_id);

static inline struct xhci_ring *xhci_urb_to_transfer_ring(struct xhci_hcd *xhci,
								struct urb *urb)
{
	return xhci_triad_to_transfer_ring(xhci, urb->dev->slot_id,
					xhci_get_endpoint_index(&urb->ep->desc),
					urb->stream_id);
}

/*
 * TODO: As per spec Isochronous IDT transmissions are supported. We bypass
 * them anyways as we where unable to find a device that matches the
 * constraints.
 */
static inline bool xhci_urb_suitable_for_idt(struct urb *urb)
{
	if (!usb_endpoint_xfer_isoc(&urb->ep->desc) && usb_urb_dir_out(urb) &&
	    usb_endpoint_maxp(&urb->ep->desc) >= TRB_IDT_MAX_SIZE &&
	    urb->transfer_buffer_length <= TRB_IDT_MAX_SIZE &&
	    !(urb->transfer_flags & URB_NO_TRANSFER_DMA_MAP) &&
	    !urb->num_sgs)
		return true;

	return false;
}

static inline char *xhci_slot_state_string(u32 state)
{
	switch (state) {
	case SLOT_STATE_ENABLED:
		return "enabled/disabled";
	case SLOT_STATE_DEFAULT:
		return "default";
	case SLOT_STATE_ADDRESSED:
		return "addressed";
	case SLOT_STATE_CONFIGURED:
		return "configured";
	default:
		return "reserved";
	}
}

static inline const char *xhci_decode_trb(char *str, size_t size,
					  u32 field0, u32 field1, u32 field2, u32 field3)
{
	int type = TRB_FIELD_TO_TYPE(field3);

	switch (type) {
	case TRB_LINK:
		snprintf(str, size,
			"LINK %08x%08x intr %d type '%s' flags %c:%c:%c:%c",
			field1, field0, GET_INTR_TARGET(field2),
			xhci_trb_type_string(type),
			field3 & TRB_IOC ? 'I' : 'i',
			field3 & TRB_CHAIN ? 'C' : 'c',
			field3 & TRB_TC ? 'T' : 't',
			field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_TRANSFER:
	case TRB_COMPLETION:
	case TRB_PORT_STATUS:
	case TRB_BANDWIDTH_EVENT:
	case TRB_DOORBELL:
	case TRB_HC_EVENT:
	case TRB_DEV_NOTE:
	case TRB_MFINDEX_WRAP:
		snprintf(str, size,
			"TRB %08x%08x status '%s' len %d slot %d ep %d type '%s' flags %c:%c",
			field1, field0,
			xhci_trb_comp_code_string(GET_COMP_CODE(field2)),
			EVENT_TRB_LEN(field2), TRB_TO_SLOT_ID(field3),
			/* Macro decrements 1, maybe it shouldn't?!? */
			TRB_TO_EP_INDEX(field3) + 1,
			xhci_trb_type_string(type),
			field3 & EVENT_DATA ? 'E' : 'e',
			field3 & TRB_CYCLE ? 'C' : 'c');

		break;
	case TRB_SETUP:
		snprintf(str, size,
			"bRequestType %02x bRequest %02x wValue %02x%02x wIndex %02x%02x wLength %d length %d TD size %d intr %d type '%s' flags %c:%c:%c",
				field0 & 0xff,
				(field0 & 0xff00) >> 8,
				(field0 & 0xff000000) >> 24,
				(field0 & 0xff0000) >> 16,
				(field1 & 0xff00) >> 8,
				field1 & 0xff,
				(field1 & 0xff000000) >> 16 |
				(field1 & 0xff0000) >> 16,
				TRB_LEN(field2), GET_TD_SIZE(field2),
				GET_INTR_TARGET(field2),
				xhci_trb_type_string(type),
				field3 & TRB_IDT ? 'I' : 'i',
				field3 & TRB_IOC ? 'I' : 'i',
				field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_DATA:
		snprintf(str, size,
			 "Buffer %08x%08x length %d TD size %d intr %d type '%s' flags %c:%c:%c:%c:%c:%c:%c",
				field1, field0, TRB_LEN(field2), GET_TD_SIZE(field2),
				GET_INTR_TARGET(field2),
				xhci_trb_type_string(type),
				field3 & TRB_IDT ? 'I' : 'i',
				field3 & TRB_IOC ? 'I' : 'i',
				field3 & TRB_CHAIN ? 'C' : 'c',
				field3 & TRB_NO_SNOOP ? 'S' : 's',
				field3 & TRB_ISP ? 'I' : 'i',
				field3 & TRB_ENT ? 'E' : 'e',
				field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_STATUS:
		snprintf(str, size,
			 "Buffer %08x%08x length %d TD size %d intr %d type '%s' flags %c:%c:%c:%c",
				field1, field0, TRB_LEN(field2), GET_TD_SIZE(field2),
				GET_INTR_TARGET(field2),
				xhci_trb_type_string(type),
				field3 & TRB_IOC ? 'I' : 'i',
				field3 & TRB_CHAIN ? 'C' : 'c',
				field3 & TRB_ENT ? 'E' : 'e',
				field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_NORMAL:
	case TRB_ISOC:
	case TRB_EVENT_DATA:
	case TRB_TR_NOOP:
		snprintf(str, size,
			"Buffer %08x%08x length %d TD size %d intr %d type '%s' flags %c:%c:%c:%c:%c:%c:%c:%c",
			field1, field0, TRB_LEN(field2), GET_TD_SIZE(field2),
			GET_INTR_TARGET(field2),
			xhci_trb_type_string(type),
			field3 & TRB_BEI ? 'B' : 'b',
			field3 & TRB_IDT ? 'I' : 'i',
			field3 & TRB_IOC ? 'I' : 'i',
			field3 & TRB_CHAIN ? 'C' : 'c',
			field3 & TRB_NO_SNOOP ? 'S' : 's',
			field3 & TRB_ISP ? 'I' : 'i',
			field3 & TRB_ENT ? 'E' : 'e',
			field3 & TRB_CYCLE ? 'C' : 'c');
		break;

	case TRB_CMD_NOOP:
	case TRB_ENABLE_SLOT:
		snprintf(str, size,
			"%s: flags %c",
			xhci_trb_type_string(type),
			field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_DISABLE_SLOT:
	case TRB_NEG_BANDWIDTH:
		snprintf(str, size,
			"%s: slot %d flags %c",
			xhci_trb_type_string(type),
			TRB_TO_SLOT_ID(field3),
			field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_ADDR_DEV:
		snprintf(str, size,
			"%s: ctx %08x%08x slot %d flags %c:%c",
			xhci_trb_type_string(type),
			field1, field0,
			TRB_TO_SLOT_ID(field3),
			field3 & TRB_BSR ? 'B' : 'b',
			field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_CONFIG_EP:
		snprintf(str, size,
			"%s: ctx %08x%08x slot %d flags %c:%c",
			xhci_trb_type_string(type),
			field1, field0,
			TRB_TO_SLOT_ID(field3),
			field3 & TRB_DC ? 'D' : 'd',
			field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_EVAL_CONTEXT:
		snprintf(str, size,
			"%s: ctx %08x%08x slot %d flags %c",
			xhci_trb_type_string(type),
			field1, field0,
			TRB_TO_SLOT_ID(field3),
			field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_RESET_EP:
		snprintf(str, size,
			"%s: ctx %08x%08x slot %d ep %d flags %c:%c",
			xhci_trb_type_string(type),
			field1, field0,
			TRB_TO_SLOT_ID(field3),
			/* Macro decrements 1, maybe it shouldn't?!? */
			TRB_TO_EP_INDEX(field3) + 1,
			field3 & TRB_TSP ? 'T' : 't',
			field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_STOP_RING:
		sprintf(str,
			"%s: slot %d sp %d ep %d flags %c",
			xhci_trb_type_string(type),
			TRB_TO_SLOT_ID(field3),
			TRB_TO_SUSPEND_PORT(field3),
			/* Macro decrements 1, maybe it shouldn't?!? */
			TRB_TO_EP_INDEX(field3) + 1,
			field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_SET_DEQ:
		snprintf(str, size,
			"%s: deq %08x%08x stream %d slot %d ep %d flags %c",
			xhci_trb_type_string(type),
			field1, field0,
			TRB_TO_STREAM_ID(field2),
			TRB_TO_SLOT_ID(field3),
			/* Macro decrements 1, maybe it shouldn't?!? */
			TRB_TO_EP_INDEX(field3) + 1,
			field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_RESET_DEV:
		snprintf(str, size,
			"%s: slot %d flags %c",
			xhci_trb_type_string(type),
			TRB_TO_SLOT_ID(field3),
			field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_FORCE_EVENT:
		snprintf(str, size,
			"%s: event %08x%08x vf intr %d vf id %d flags %c",
			xhci_trb_type_string(type),
			field1, field0,
			TRB_TO_VF_INTR_TARGET(field2),
			TRB_TO_VF_ID(field3),
			field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_SET_LT:
		snprintf(str, size,
			"%s: belt %d flags %c",
			xhci_trb_type_string(type),
			TRB_TO_BELT(field3),
			field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_GET_BW:
		snprintf(str, size,
			"%s: ctx %08x%08x slot %d speed %d flags %c",
			xhci_trb_type_string(type),
			field1, field0,
			TRB_TO_SLOT_ID(field3),
			TRB_TO_DEV_SPEED(field3),
			field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	case TRB_FORCE_HEADER:
		snprintf(str, size,
			"%s: info %08x%08x%08x pkt type %d roothub port %d flags %c",
			xhci_trb_type_string(type),
			field2, field1, field0 & 0xffffffe0,
			TRB_TO_PACKET_TYPE(field0),
			TRB_TO_ROOTHUB_PORT(field3),
			field3 & TRB_CYCLE ? 'C' : 'c');
		break;
	default:
		snprintf(str, size,
			"type '%s' -> raw %08x %08x %08x %08x",
			xhci_trb_type_string(type),
			field0, field1, field2, field3);
	}

	return str;
}

static inline const char *xhci_decode_ctrl_ctx(char *str,
		unsigned long drop, unsigned long add)
{
	unsigned int	bit;
	int		ret = 0;

	str[0] = '\0';

	if (drop) {
		ret = sprintf(str, "Drop:");
		for_each_set_bit(bit, &drop, 32)
			ret += sprintf(str + ret, " %d%s",
				       bit / 2,
				       bit % 2 ? "in":"out");
		ret += sprintf(str + ret, ", ");
	}

	if (add) {
		ret += sprintf(str + ret, "Add:%s%s",
			       (add & SLOT_FLAG) ? " slot":"",
			       (add & EP0_FLAG) ? " ep0":"");
		add &= ~(SLOT_FLAG | EP0_FLAG);
		for_each_set_bit(bit, &add, 32)
			ret += sprintf(str + ret, " %d%s",
				       bit / 2,
				       bit % 2 ? "in":"out");
	}
	return str;
}

static inline const char *xhci_decode_slot_context(char *str,
		u32 info, u32 info2, u32 tt_info, u32 state)
{
	u32 speed;
	u32 hub;
	u32 mtt;
	int ret = 0;

	speed = info & DEV_SPEED;
	hub = info & DEV_HUB;
	mtt = info & DEV_MTT;

	ret = sprintf(str, "RS %05x %s%s%s Ctx Entries %d MEL %d us Port# %d/%d",
			info & ROUTE_STRING_MASK,
			({ char *s;
			switch (speed) {
			case SLOT_SPEED_FS:
				s = "full-speed";
				break;
			case SLOT_SPEED_LS:
				s = "low-speed";
				break;
			case SLOT_SPEED_HS:
				s = "high-speed";
				break;
			case SLOT_SPEED_SS:
				s = "super-speed";
				break;
			case SLOT_SPEED_SSP:
				s = "super-speed plus";
				break;
			default:
				s = "UNKNOWN speed";
			} s; }),
			mtt ? " multi-TT" : "",
			hub ? " Hub" : "",
			(info & LAST_CTX_MASK) >> 27,
			info2 & MAX_EXIT,
			DEVINFO_TO_ROOT_HUB_PORT(info2),
			DEVINFO_TO_MAX_PORTS(info2));

	ret += sprintf(str + ret, " [TT Slot %d Port# %d TTT %d Intr %d] Addr %d State %s",
			tt_info & TT_SLOT, (tt_info & TT_PORT) >> 8,
			GET_TT_THINK_TIME(tt_info), GET_INTR_TARGET(tt_info),
			state & DEV_ADDR_MASK,
			xhci_slot_state_string(GET_SLOT_STATE(state)));

	return str;
}


static inline const char *xhci_portsc_link_state_string(u32 portsc)
{
	switch (portsc & PORT_PLS_MASK) {
	case XDEV_U0:
		return "U0";
	case XDEV_U1:
		return "U1";
	case XDEV_U2:
		return "U2";
	case XDEV_U3:
		return "U3";
	case XDEV_DISABLED:
		return "Disabled";
	case XDEV_RXDETECT:
		return "RxDetect";
	case XDEV_INACTIVE:
		return "Inactive";
	case XDEV_POLLING:
		return "Polling";
	case XDEV_RECOVERY:
		return "Recovery";
	case XDEV_HOT_RESET:
		return "Hot Reset";
	case XDEV_COMP_MODE:
		return "Compliance mode";
	case XDEV_TEST_MODE:
		return "Test mode";
	case XDEV_RESUME:
		return "Resume";
	default:
		break;
	}
	return "Unknown";
}

static inline const char *xhci_decode_portsc(char *str, u32 portsc)
{
	int ret;

	ret = sprintf(str, "%s %s %s Link:%s PortSpeed:%d ",
		      portsc & PORT_POWER	? "Powered" : "Powered-off",
		      portsc & PORT_CONNECT	? "Connected" : "Not-connected",
		      portsc & PORT_PE		? "Enabled" : "Disabled",
		      xhci_portsc_link_state_string(portsc),
		      DEV_PORT_SPEED(portsc));

	if (portsc & PORT_OC)
		ret += sprintf(str + ret, "OverCurrent ");
	if (portsc & PORT_RESET)
		ret += sprintf(str + ret, "In-Reset ");

	ret += sprintf(str + ret, "Change: ");
	if (portsc & PORT_CSC)
		ret += sprintf(str + ret, "CSC ");
	if (portsc & PORT_PEC)
		ret += sprintf(str + ret, "PEC ");
	if (portsc & PORT_WRC)
		ret += sprintf(str + ret, "WRC ");
	if (portsc & PORT_OCC)
		ret += sprintf(str + ret, "OCC ");
	if (portsc & PORT_RC)
		ret += sprintf(str + ret, "PRC ");
	if (portsc & PORT_PLC)
		ret += sprintf(str + ret, "PLC ");
	if (portsc & PORT_CEC)
		ret += sprintf(str + ret, "CEC ");
	if (portsc & PORT_CAS)
		ret += sprintf(str + ret, "CAS ");

	ret += sprintf(str + ret, "Wake: ");
	if (portsc & PORT_WKCONN_E)
		ret += sprintf(str + ret, "WCE ");
	if (portsc & PORT_WKDISC_E)
		ret += sprintf(str + ret, "WDE ");
	if (portsc & PORT_WKOC_E)
		ret += sprintf(str + ret, "WOE ");

	return str;
}

static inline const char *xhci_decode_usbsts(char *str, u32 usbsts)
{
	int ret = 0;

	ret = sprintf(str, " 0x%08x", usbsts);

	if (usbsts == ~(u32)0)
		return str;

	if (usbsts & STS_HALT)
		ret += sprintf(str + ret, " HCHalted");
	if (usbsts & STS_FATAL)
		ret += sprintf(str + ret, " HSE");
	if (usbsts & STS_EINT)
		ret += sprintf(str + ret, " EINT");
	if (usbsts & STS_PORT)
		ret += sprintf(str + ret, " PCD");
	if (usbsts & STS_SAVE)
		ret += sprintf(str + ret, " SSS");
	if (usbsts & STS_RESTORE)
		ret += sprintf(str + ret, " RSS");
	if (usbsts & STS_SRE)
		ret += sprintf(str + ret, " SRE");
	if (usbsts & STS_CNR)
		ret += sprintf(str + ret, " CNR");
	if (usbsts & STS_HCE)
		ret += sprintf(str + ret, " HCE");

	return str;
}

static inline const char *xhci_decode_doorbell(char *str, u32 slot, u32 doorbell)
{
	u8 ep;
	u16 stream;
	int ret;

	ep = (doorbell & 0xff);
	stream = doorbell >> 16;

	if (slot == 0) {
		sprintf(str, "Command Ring %d", doorbell);
		return str;
	}
	ret = sprintf(str, "Slot %d ", slot);
	if (ep > 0 && ep < 32)
		ret = sprintf(str + ret, "ep%d%s",
			      ep / 2,
			      ep % 2 ? "in" : "out");
	else if (ep == 0 || ep < 248)
		ret = sprintf(str + ret, "Reserved %d", ep);
	else
		ret = sprintf(str + ret, "Vendor Defined %d", ep);
	if (stream)
		ret = sprintf(str + ret, " Stream %d", stream);

	return str;
}

static inline const char *xhci_ep_state_string(u8 state)
{
	switch (state) {
	case EP_STATE_DISABLED:
		return "disabled";
	case EP_STATE_RUNNING:
		return "running";
	case EP_STATE_HALTED:
		return "halted";
	case EP_STATE_STOPPED:
		return "stopped";
	case EP_STATE_ERROR:
		return "error";
	default:
		return "INVALID";
	}
}

static inline const char *xhci_ep_type_string(u8 type)
{
	switch (type) {
	case ISOC_OUT_EP:
		return "Isoc OUT";
	case BULK_OUT_EP:
		return "Bulk OUT";
	case INT_OUT_EP:
		return "Int OUT";
	case CTRL_EP:
		return "Ctrl";
	case ISOC_IN_EP:
		return "Isoc IN";
	case BULK_IN_EP:
		return "Bulk IN";
	case INT_IN_EP:
		return "Int IN";
	default:
		return "INVALID";
	}
}

static inline const char *xhci_decode_ep_context(char *str, u32 info,
		u32 info2, u64 deq, u32 tx_info)
{
	int ret;

	u32 esit;
	u16 maxp;
	u16 avg;

	u8 max_pstr;
	u8 ep_state;
	u8 interval;
	u8 ep_type;
	u8 burst;
	u8 cerr;
	u8 mult;

	bool lsa;
	bool hid;

	esit = CTX_TO_MAX_ESIT_PAYLOAD_HI(info) << 16 |
		CTX_TO_MAX_ESIT_PAYLOAD(tx_info);

	ep_state = info & EP_STATE_MASK;
	max_pstr = CTX_TO_EP_MAXPSTREAMS(info);
	interval = CTX_TO_EP_INTERVAL(info);
	mult = CTX_TO_EP_MULT(info) + 1;
	lsa = !!(info & EP_HAS_LSA);

	cerr = (info2 & (3 << 1)) >> 1;
	ep_type = CTX_TO_EP_TYPE(info2);
	hid = !!(info2 & (1 << 7));
	burst = CTX_TO_MAX_BURST(info2);
	maxp = MAX_PACKET_DECODED(info2);

	avg = EP_AVG_TRB_LENGTH(tx_info);

	ret = sprintf(str, "State %s mult %d max P. Streams %d %s",
			xhci_ep_state_string(ep_state), mult,
			max_pstr, lsa ? "LSA " : "");

	ret += sprintf(str + ret, "interval %d us max ESIT payload %d CErr %d ",
			(1 << interval) * 125, esit, cerr);

	ret += sprintf(str + ret, "Type %s %sburst %d maxp %d deq %016llx ",
			xhci_ep_type_string(ep_type), hid ? "HID" : "",
			burst, maxp, deq);

	ret += sprintf(str + ret, "avg trb len %d", avg);

	return str;
}

#endif /* __LINUX_XHCI_HCD_H */
