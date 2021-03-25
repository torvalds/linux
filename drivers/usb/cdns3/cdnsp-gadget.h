/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Cadence CDNSP DRD Driver.
 *
 * Copyright (C) 2020 Cadence.
 *
 * Author: Pawel Laszczak <pawell@cadence.com>
 *
 * Code based on Linux XHCI driver.
 * Origin: Copyright (C) 2008 Intel Corp.
 */
#ifndef __LINUX_CDNSP_GADGET_H
#define __LINUX_CDNSP_GADGET_H

#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/usb/gadget.h>
#include <linux/irq.h>

/* Max number slots - only 1 is allowed. */
#define CDNSP_DEV_MAX_SLOTS	1

#define CDNSP_EP0_SETUP_SIZE	512

/* One control and 15 for in and 15 for out endpoints. */
#define CDNSP_ENDPOINTS_NUM	31

/* Best Effort Service Latency. */
#define CDNSP_DEFAULT_BESL	0

/* Device Controller command default timeout value in us */
#define CDNSP_CMD_TIMEOUT	(15 * 1000)

/* Up to 16 ms to halt an device controller */
#define CDNSP_MAX_HALT_USEC	(16 * 1000)

#define CDNSP_CTX_SIZE	2112

/*
 * Controller register interface.
 */

/**
 * struct cdnsp_cap_regs - CDNSP Registers.
 * @hc_capbase:	Length of the capabilities register and controller
 *              version number
 * @hcs_params1: HCSPARAMS1 - Structural Parameters 1
 * @hcs_params2: HCSPARAMS2 - Structural Parameters 2
 * @hcs_params3: HCSPARAMS3 - Structural Parameters 3
 * @hcc_params: HCCPARAMS - Capability Parameters
 * @db_off: DBOFF - Doorbell array offset
 * @run_regs_off: RTSOFF - Runtime register space offset
 * @hcc_params2: HCCPARAMS2 Capability Parameters 2,
 */
struct cdnsp_cap_regs {
	__le32 hc_capbase;
	__le32 hcs_params1;
	__le32 hcs_params2;
	__le32 hcs_params3;
	__le32 hcc_params;
	__le32 db_off;
	__le32 run_regs_off;
	__le32 hcc_params2;
	/* Reserved up to (CAPLENGTH - 0x1C) */
};

/* hc_capbase bitmasks. */
/* bits 7:0 - how long is the Capabilities register. */
#define HC_LENGTH(p)		(((p) >> 00) & GENMASK(7, 0))
/* bits 31:16	*/
#define HC_VERSION(p)		(((p) >> 16) & GENMASK(15, 1))

/* HCSPARAMS1 - hcs_params1 - bitmasks */
/* bits 0:7, Max Device Endpoints */
#define HCS_ENDPOINTS_MASK	GENMASK(7, 0)
#define HCS_ENDPOINTS(p)	(((p) & HCS_ENDPOINTS_MASK) >> 0)

/* HCCPARAMS offset from PCI base address */
#define HCC_PARAMS_OFFSET	0x10

/* HCCPARAMS - hcc_params - bitmasks */
/* 1: device controller can use 64-bit address pointers. */
#define HCC_64BIT_ADDR(p)	((p) & BIT(0))
/* 1: device controller uses 64-byte Device Context structures. */
#define HCC_64BYTE_CONTEXT(p)	((p) & BIT(2))
/* Max size for Primary Stream Arrays - 2^(n+1), where n is bits 12:15. */
#define HCC_MAX_PSA(p)		((((p) >> 12) & 0xf) + 1)
/* Extended Capabilities pointer from PCI base. */
#define HCC_EXT_CAPS(p)		(((p) & GENMASK(31, 16)) >> 16)

#define CTX_SIZE(_hcc)		(HCC_64BYTE_CONTEXT(_hcc) ? 64 : 32)

/* db_off bitmask - bits 0:1 reserved. */
#define DBOFF_MASK	GENMASK(31, 2)

/* run_regs_off bitmask - bits 0:4 reserved. */
#define RTSOFF_MASK	GENMASK(31, 5)

/**
 * struct cdnsp_op_regs - Device Controller Operational Registers.
 * @command: USBCMD - Controller command register.
 * @status: USBSTS - Controller status register.
 * @page_size: This indicates the page size that the device controller supports.
 *             If bit n is set, the controller supports a page size of 2^(n+12),
 *             up to a 128MB page size. 4K is the minimum page size.
 * @dnctrl: DNCTRL - Device notification control register.
 * @cmd_ring: CRP - 64-bit Command Ring Pointer.
 * @dcbaa_ptr: DCBAAP - 64-bit Device Context Base Address Array Pointer.
 * @config_reg: CONFIG - Configure Register
 * @port_reg_base: PORTSCn - base address for Port Status and Control
 *                 Each port has a Port Status and Control register,
 *                 followed by a Port Power Management Status and Control
 *                 register, a Port Link Info register, and a reserved
 *                 register.
 */
struct cdnsp_op_regs {
	__le32 command;
	__le32 status;
	__le32 page_size;
	__le32 reserved1;
	__le32 reserved2;
	__le32 dnctrl;
	__le64 cmd_ring;
	/* rsvd: offset 0x20-2F. */
	__le32 reserved3[4];
	__le64 dcbaa_ptr;
	__le32 config_reg;
	/* rsvd: offset 0x3C-3FF. */
	__le32 reserved4[241];
	/* port 1 registers, which serve as a base address for other ports. */
	__le32 port_reg_base;
};

/* Number of registers per port. */
#define NUM_PORT_REGS	4

/**
 * struct cdnsp_port_regs - Port Registers.
 * @portsc: PORTSC - Port Status and Control Register.
 * @portpmsc: PORTPMSC - Port Power Managements Status and Control Register.
 * @portli: PORTLI - Port Link Info register.
 */
struct cdnsp_port_regs {
	__le32 portsc;
	__le32 portpmsc;
	__le32 portli;
	__le32 reserved;
};

/*
 * These bits are Read Only (RO) and should be saved and written to the
 * registers: 0 (connect status) and  10:13 (port speed).
 * These bits are also sticky - meaning they're in the AUX well and they aren't
 * changed by a hot and warm.
 */
#define CDNSP_PORT_RO	(PORT_CONNECT | DEV_SPEED_MASK)

/*
 * These bits are RW; writing a 0 clears the bit, writing a 1 sets the bit:
 * bits 5:8 (link state), 25:26  ("wake on" enable state)
 */
#define CDNSP_PORT_RWS	(PORT_PLS_MASK | PORT_WKCONN_E | PORT_WKDISC_E)

/*
 * These bits are RW; writing a 1 clears the bit, writing a 0 has no effect:
 * bits 1 (port enable/disable), 17  ( connect changed),
 * 21 (port reset changed) , 22 (Port Link State Change),
 */
#define CDNSP_PORT_RW1CS (PORT_PED | PORT_CSC | PORT_RC | PORT_PLC)

/* USBCMD - USB command - bitmasks. */
/* Run/Stop, controller execution - do not write unless controller is halted.*/
#define CMD_R_S		BIT(0)
/*
 * Reset device controller - resets internal controller state machine and all
 * registers (except PCI config regs).
 */
#define CMD_RESET	BIT(1)
/* Event Interrupt Enable - a '1' allows interrupts from the controller. */
#define CMD_INTE	BIT(2)
/*
 * Device System Error Interrupt Enable - get out-of-band signal for
 * controller errors.
 */
#define CMD_DSEIE	BIT(3)
/* device controller save/restore state. */
#define CMD_CSS		BIT(8)
#define CMD_CRS		BIT(9)
/*
 * Enable Wrap Event - '1' means device controller generates an event
 * when MFINDEX wraps.
 */
#define CMD_EWE		BIT(10)
/* 1: device enabled */
#define CMD_DEVEN	BIT(17)
/* bits 18:31 are reserved (and should be preserved on writes). */

/* Command register values to disable interrupts. */
#define CDNSP_IRQS	(CMD_INTE | CMD_DSEIE | CMD_EWE)

/* USBSTS - USB status - bitmasks */
/* controller not running - set to 1 when run/stop bit is cleared. */
#define STS_HALT	BIT(0)
/*
 * serious error, e.g. PCI parity error. The controller will clear
 * the run/stop bit.
 */
#define STS_FATAL	BIT(2)
/* event interrupt - clear this prior to clearing any IP flags in IR set.*/
#define STS_EINT	BIT(3)
/* port change detect */
#define STS_PCD		BIT(4)
/* save state status - '1' means device controller is saving state. */
#define STS_SSS		BIT(8)
/* restore state status - '1' means controllers is restoring state. */
#define STS_RSS		BIT(9)
/* 1: save or restore error */
#define STS_SRE		BIT(10)
/* 1: device Not Ready to accept doorbell or op reg writes after reset. */
#define STS_CNR		BIT(11)
/* 1: internal Device Controller Error.*/
#define STS_HCE		BIT(12)

/* CRCR - Command Ring Control Register - cmd_ring bitmasks. */
/* bit 0 is the command ring cycle state. */
#define CMD_RING_CS		BIT(0)
/* stop ring immediately - abort the currently executing command. */
#define CMD_RING_ABORT		BIT(2)
/*
 * Command Ring Busy.
 * Set when Doorbell register is written with DB for command and cleared when
 * the controller reached end of CR.
 */
#define CMD_RING_BUSY(p)	((p) & BIT(4))
/* 1: command ring is running */
#define CMD_RING_RUNNING	BIT(3)
/* Command Ring pointer - bit mask for the lower 32 bits. */
#define CMD_RING_RSVD_BITS	GENMASK(5, 0)

/* CONFIG - Configure Register - config_reg bitmasks. */
/* bits 0:7 - maximum number of device slots enabled. */
#define MAX_DEVS		GENMASK(7, 0)
/* bit 8: U3 Entry Enabled, assert PLC when controller enters U3. */
#define CONFIG_U3E		BIT(8)

/* PORTSC - Port Status and Control Register - port_reg_base bitmasks */
/* 1: device connected. */
#define PORT_CONNECT		BIT(0)
/* 1: port enabled. */
#define PORT_PED		BIT(1)
/* 1: port reset signaling asserted. */
#define PORT_RESET		BIT(4)
/*
 * Port Link State - bits 5:8
 * A read gives the current link PM state of the port,
 * a write with Link State Write Strobe sets the link state.
 */
#define PORT_PLS_MASK		GENMASK(8, 5)
#define XDEV_U0			(0x0 << 5)
#define XDEV_U1			(0x1 << 5)
#define XDEV_U2			(0x2 << 5)
#define XDEV_U3			(0x3 << 5)
#define XDEV_DISABLED		(0x4 << 5)
#define XDEV_RXDETECT		(0x5 << 5)
#define XDEV_INACTIVE		(0x6 << 5)
#define XDEV_POLLING		(0x7 << 5)
#define XDEV_RECOVERY		(0x8 << 5)
#define XDEV_HOT_RESET		(0x9 << 5)
#define XDEV_COMP_MODE		(0xa << 5)
#define XDEV_TEST_MODE		(0xb << 5)
#define XDEV_RESUME		(0xf << 5)
/* 1: port has power. */
#define PORT_POWER		BIT(9)
/*
 * bits 10:13 indicate device speed:
 * 0 - undefined speed - port hasn't be initialized by a reset yet
 * 1 - full speed
 * 2 - Reserved (Low Speed not supported
 * 3 - high speed
 * 4 - super speed
 * 5 - super speed
 * 6-15 reserved
 */
#define DEV_SPEED_MASK		GENMASK(13, 10)
#define XDEV_FS			(0x1 << 10)
#define XDEV_HS			(0x3 << 10)
#define XDEV_SS			(0x4 << 10)
#define XDEV_SSP		(0x5 << 10)
#define DEV_UNDEFSPEED(p)	(((p) & DEV_SPEED_MASK) == (0x0 << 10))
#define DEV_FULLSPEED(p)	(((p) & DEV_SPEED_MASK) == XDEV_FS)
#define DEV_HIGHSPEED(p)	(((p) & DEV_SPEED_MASK) == XDEV_HS)
#define DEV_SUPERSPEED(p)	(((p) & DEV_SPEED_MASK) == XDEV_SS)
#define DEV_SUPERSPEEDPLUS(p)	(((p) & DEV_SPEED_MASK) == XDEV_SSP)
#define DEV_SUPERSPEED_ANY(p)	(((p) & DEV_SPEED_MASK) >= XDEV_SS)
#define DEV_PORT_SPEED(p)	(((p) >> 10) & 0x0f)
/* Port Link State Write Strobe - set this when changing link state */
#define PORT_LINK_STROBE	BIT(16)
/* 1: connect status change */
#define PORT_CSC		BIT(17)
/* 1: warm reset for a USB 3.0 device is done. */
#define PORT_WRC		BIT(19)
/* 1: reset change - 1 to 0 transition of PORT_RESET */
#define PORT_RC			BIT(21)
/*
 * port link status change - set on some port link state transitions:
 * Transition			Reason
 * ----------------------------------------------------------------------------
 * - U3 to Resume		Wakeup signaling from a device
 * - Resume to Recovery to U0	USB 3.0 device resume
 * - Resume to U0		USB 2.0 device resume
 * - U3 to Recovery to U0	Software resume of USB 3.0 device complete
 * - U3 to U0			Software resume of USB 2.0 device complete
 * - U2 to U0			L1 resume of USB 2.1 device complete
 * - U0 to U0			L1 entry rejection by USB 2.1 device
 * - U0 to disabled		L1 entry error with USB 2.1 device
 * - Any state to inactive	Error on USB 3.0 port
 */
#define PORT_PLC		BIT(22)
/* Port configure error change - port failed to configure its link partner. */
#define PORT_CEC		BIT(23)
/* Wake on connect (enable). */
#define PORT_WKCONN_E		BIT(25)
/* Wake on disconnect (enable). */
#define PORT_WKDISC_E		BIT(26)
/* Indicates if Warm Reset is being received. */
#define PORT_WR			BIT(31)

#define PORT_CHANGE_BITS (PORT_CSC | PORT_WRC | PORT_RC | PORT_PLC | PORT_CEC)

/* PORTPMSCUSB3 - Port Power Management Status and Control - bitmasks. */
/*  Enables U1 entry. */
#define PORT_U1_TIMEOUT_MASK	GENMASK(7, 0)
#define PORT_U1_TIMEOUT(p)	((p) & PORT_U1_TIMEOUT_MASK)
/* Enables U2 entry .*/
#define PORT_U2_TIMEOUT_MASK	GENMASK(14, 8)
#define PORT_U2_TIMEOUT(p)	(((p) << 8) & PORT_U2_TIMEOUT_MASK)

/* PORTPMSCUSB2 - Port Power Management Status and Control - bitmasks. */
#define PORT_L1S_MASK		GENMASK(2, 0)
#define PORT_L1S(p)		((p) & PORT_L1S_MASK)
#define PORT_L1S_ACK		PORT_L1S(1)
#define PORT_L1S_NYET		PORT_L1S(2)
#define PORT_L1S_STALL		PORT_L1S(3)
#define PORT_L1S_TIMEOUT	PORT_L1S(4)
/* Remote Wake Enable. */
#define PORT_RWE		BIT(3)
/* Best Effort Service Latency (BESL). */
#define PORT_BESL(p)		(((p) << 4) & GENMASK(7, 4))
/* Hardware LPM Enable (HLE). */
#define PORT_HLE		BIT(16)
/* Received Best Effort Service Latency (BESL). */
#define PORT_RRBESL(p)		(((p) & GENMASK(20, 17)) >> 17)
/* Port Test Control. */
#define PORT_TEST_MODE_MASK	GENMASK(31, 28)
#define PORT_TEST_MODE(p)	(((p) << 28) & PORT_TEST_MODE_MASK)

/**
 * struct cdnsp_intr_reg - Interrupt Register Set.
 * @irq_pending: IMAN - Interrupt Management Register. Used to enable
 *               interrupts and check for pending interrupts.
 * @irq_control: IMOD - Interrupt Moderation Register.
 *               Used to throttle interrupts.
 * @erst_size: Number of segments in the Event Ring Segment Table (ERST).
 * @erst_base: ERST base address.
 * @erst_dequeue: Event ring dequeue pointer.
 *
 * Each interrupter (defined by a MSI-X vector) has an event ring and an Event
 * Ring Segment Table (ERST) associated with it. The event ring is comprised of
 * multiple segments of the same size. The controller places events on the ring
 * and "updates the Cycle bit in the TRBs to indicate to software the current
 * position of the Enqueue Pointer." The driver processes those events and
 * updates the dequeue pointer.
 */
struct cdnsp_intr_reg {
	__le32 irq_pending;
	__le32 irq_control;
	__le32 erst_size;
	__le32 rsvd;
	__le64 erst_base;
	__le64 erst_dequeue;
};

/* IMAN - Interrupt Management Register - irq_pending bitmasks l. */
#define IMAN_IE			BIT(1)
#define IMAN_IP			BIT(0)
/* bits 2:31 need to be preserved */
#define IMAN_IE_SET(p)		(((p) & IMAN_IE) | 0x2)
#define IMAN_IE_CLEAR(p)	(((p) & IMAN_IE) & ~(0x2))

/* IMOD - Interrupter Moderation Register - irq_control bitmasks. */
/*
 * Minimum interval between interrupts (in 250ns intervals). The interval
 * between interrupts will be longer if there are no events on the event ring.
 * Default is 4000 (1 ms).
 */
#define IMOD_INTERVAL_MASK	GENMASK(15, 0)
/* Counter used to count down the time to the next interrupt - HW use only */
#define IMOD_COUNTER_MASK	GENMASK(31, 16)
#define IMOD_DEFAULT_INTERVAL	0

/* erst_size bitmasks. */
/* Preserve bits 16:31 of erst_size. */
#define ERST_SIZE_MASK		GENMASK(31, 16)

/* erst_dequeue bitmasks. */
/*
 * Dequeue ERST Segment Index (DESI) - Segment number (or alias)
 * where the current dequeue pointer lies. This is an optional HW hint.
 */
#define ERST_DESI_MASK		GENMASK(2, 0)
/* Event Handler Busy (EHB) - is the event ring scheduled to be serviced. */
#define ERST_EHB		BIT(3)
#define ERST_PTR_MASK		GENMASK(3, 0)

/**
 * struct cdnsp_run_regs
 * @microframe_index: MFINDEX - current microframe number.
 * @ir_set: Array of Interrupter registers.
 *
 * Device Controller Runtime Registers:
 * "Software should read and write these registers using only Dword (32 bit)
 * or larger accesses"
 */
struct cdnsp_run_regs {
	__le32 microframe_index;
	__le32 rsvd[7];
	struct cdnsp_intr_reg ir_set[128];
};

/**
 * USB2.0 Port Peripheral Configuration Registers.
 * @ext_cap: Header register for Extended Capability.
 * @port_reg1: Timer Configuration Register.
 * @port_reg2: Timer Configuration Register.
 * @port_reg3: Timer Configuration Register.
 * @port_reg4: Timer Configuration Register.
 * @port_reg5: Timer Configuration Register.
 * @port_reg6: Chicken bits for USB20PPP.
 */
struct cdnsp_20port_cap {
	__le32 ext_cap;
	__le32 port_reg1;
	__le32 port_reg2;
	__le32 port_reg3;
	__le32 port_reg4;
	__le32 port_reg5;
	__le32 port_reg6;
};

/* Extended capability register fields */
#define EXT_CAPS_ID(p)			(((p) >> 0) & GENMASK(7, 0))
#define EXT_CAPS_NEXT(p)		(((p) >> 8) & GENMASK(7, 0))
/* Extended capability IDs - ID 0 reserved */
#define EXT_CAPS_PROTOCOL		2

/* USB 2.0 Port Peripheral Configuration Extended Capability */
#define EXT_CAP_CFG_DEV_20PORT_CAP_ID	0xC1
/*
 * Setting this bit to '1' enables automatic wakeup from L1 state on transfer
 * TRB prepared when USBSSP operates in USB2.0 mode.
 */
#define PORT_REG6_L1_L0_HW_EN		BIT(1)
/*
 * Setting this bit to '1' forces Full Speed when USBSSP operates in USB2.0
 * mode (disables High Speed).
 */
#define PORT_REG6_FORCE_FS		BIT(0)

/**
 * USB3.x Port Peripheral Configuration Registers.
 * @ext_cap: Header register for Extended Capability.
 * @mode_addr: Miscellaneous 3xPORT operation mode configuration register.
 * @mode_2: 3x Port Control Register 2.
 */
struct cdnsp_3xport_cap {
	__le32 ext_cap;
	__le32 mode_addr;
	__le32 reserved[52];
	__le32 mode_2;
};

/* Extended Capability Header for 3XPort Configuration Registers. */
#define D_XEC_CFG_3XPORT_CAP		0xC0
#define CFG_3XPORT_SSP_SUPPORT		BIT(31)
#define CFG_3XPORT_U1_PIPE_CLK_GATE_EN	BIT(0)

/* Revision Extended Capability ID */
#define RTL_REV_CAP			0xC4
#define RTL_REV_CAP_RX_BUFF_CMD_SIZE	BITMASK(31, 24)
#define RTL_REV_CAP_RX_BUFF_SIZE	BITMASK(15, 0)
#define RTL_REV_CAP_TX_BUFF_CMD_SIZE	BITMASK(31, 24)
#define RTL_REV_CAP_TX_BUFF_SIZE	BITMASK(15, 0)

#define CDNSP_VER_1 0x00000000
#define CDNSP_VER_2 0x10000000

#define CDNSP_IF_EP_EXIST(pdev, ep_num, dir) \
			 (readl(&(pdev)->rev_cap->ep_supported) & \
			 (BIT(ep_num) << ((dir) ? 0 : 16)))

/**
 * struct cdnsp_rev_cap - controller capabilities.
 * @ext_cap: Header for RTL Revision Extended Capability.
 * @rtl_revision: RTL revision.
 * @rx_buff_size: Rx buffer sizes.
 * @tx_buff_size: Tx buffer sizes.
 * @ep_supported: Supported endpoints.
 * @ctrl_revision: Controller revision ID.
 */
struct cdnsp_rev_cap {
	__le32 ext_cap;
	__le32 rtl_revision;
	__le32 rx_buff_size;
	__le32 tx_buff_size;
	__le32 ep_supported;
	__le32 ctrl_revision;
};

/* USB2.0 Port Peripheral Configuration Registers. */
#define D_XEC_PRE_REGS_CAP		0xC8
#define REG_CHICKEN_BITS_2_OFFSET	0x48
#define CHICKEN_XDMA_2_TP_CACHE_DIS	BIT(28)

/* XBUF Extended Capability ID. */
#define XBUF_CAP_ID			0xCB
#define XBUF_RX_TAG_MASK_0_OFFSET	0x1C
#define XBUF_RX_TAG_MASK_1_OFFSET	0x24
#define XBUF_TX_CMD_OFFSET		0x2C

/**
 * struct cdnsp_doorbell_array.
 * @cmd_db: Command ring doorbell register.
 * @ep_db: Endpoint ring doorbell register.
 *         Bits 0 - 7: Endpoint target.
 *         Bits 8 - 15: RsvdZ.
 *         Bits 16 - 31: Stream ID.
 */
struct cdnsp_doorbell_array {
	__le32 cmd_db;
	__le32 ep_db;
};

#define DB_VALUE(ep, stream)		((((ep) + 1) & 0xff) | ((stream) << 16))
#define DB_VALUE_EP0_OUT(ep, stream)	((ep) & 0xff)
#define DB_VALUE_CMD			0x00000000

/**
 * struct cdnsp_container_ctx.
 * @type: Type of context. Used to calculated offsets to contained contexts.
 * @size: Size of the context data.
 * @ctx_size: context data structure size - 64 or 32 bits.
 * @dma: dma address of the bytes.
 * @bytes: The raw context data given to HW.
 *
 * Represents either a Device or Input context. Holds a pointer to the raw
 * memory used for the context (bytes) and dma address of it (dma).
 */
struct cdnsp_container_ctx {
	unsigned int type;
#define CDNSP_CTX_TYPE_DEVICE	0x1
#define CDNSP_CTX_TYPE_INPUT	0x2
	int size;
	int ctx_size;
	dma_addr_t dma;
	u8 *bytes;
};

/**
 * struct cdnsp_slot_ctx
 * @dev_info: Device speed, and last valid endpoint.
 * @dev_port: Device port number that is needed to access the USB device.
 * @int_target: Interrupter target number.
 * @dev_state: Slot state and device address.
 *
 * Slot Context - This assumes the controller uses 32-byte context
 * structures. If the controller uses 64-byte contexts, there is an additional
 * 32 bytes reserved at the end of the slot context for controller internal use.
 */
struct cdnsp_slot_ctx {
	__le32 dev_info;
	__le32 dev_port;
	__le32 int_target;
	__le32 dev_state;
	/* offset 0x10 to 0x1f reserved for controller internal use. */
	__le32 reserved[4];
};

/* Bits 20:23 in the Slot Context are the speed for the device. */
#define SLOT_SPEED_FS		(XDEV_FS << 10)
#define SLOT_SPEED_HS		(XDEV_HS << 10)
#define SLOT_SPEED_SS		(XDEV_SS << 10)
#define SLOT_SPEED_SSP		(XDEV_SSP << 10)

/* dev_info bitmasks. */
/* Device speed - values defined by PORTSC Device Speed field - 20:23. */
#define DEV_SPEED		GENMASK(23, 20)
#define GET_DEV_SPEED(n)	(((n) & DEV_SPEED) >> 20)
/* Index of the last valid endpoint context in this device context - 27:31. */
#define LAST_CTX_MASK		((unsigned int)GENMASK(31, 27))
#define LAST_CTX(p)		((p) << 27)
#define LAST_CTX_TO_EP_NUM(p)	(((p) >> 27) - 1)
#define SLOT_FLAG		BIT(0)
#define EP0_FLAG		BIT(1)

/* dev_port bitmasks */
/* Device port number that is needed to access the USB device. */
#define DEV_PORT(p)		(((p) & 0xff) << 16)

/* dev_state bitmasks */
/* USB device address - assigned by the controller. */
#define DEV_ADDR_MASK		GENMASK(7, 0)
/* Slot state */
#define SLOT_STATE		GENMASK(31, 27)
#define GET_SLOT_STATE(p)	(((p) & SLOT_STATE) >> 27)

#define SLOT_STATE_DISABLED	0
#define SLOT_STATE_ENABLED	SLOT_STATE_DISABLED
#define SLOT_STATE_DEFAULT	1
#define SLOT_STATE_ADDRESSED	2
#define SLOT_STATE_CONFIGURED	3

/**
 * struct cdnsp_ep_ctx.
 * @ep_info: Endpoint state, streams, mult, and interval information.
 * @ep_info2: Information on endpoint type, max packet size, max burst size,
 *            error count, and whether the controller will force an event for
 *            all transactions.
 * @deq: 64-bit ring dequeue pointer address. If the endpoint only
 *       defines one stream, this points to the endpoint transfer ring.
 *       Otherwise, it points to a stream context array, which has a
 *       ring pointer for each flow.
 * @tx_info: Average TRB lengths for the endpoint ring and
 *	     max payload within an Endpoint Service Interval Time (ESIT).
 *
 * Endpoint Context - This assumes the controller uses 32-byte context
 * structures. If the controller uses 64-byte contexts, there is an additional
 * 32 bytes reserved at the end of the endpoint context for controller internal
 * use.
 */
struct cdnsp_ep_ctx {
	__le32 ep_info;
	__le32 ep_info2;
	__le64 deq;
	__le32 tx_info;
	/* offset 0x14 - 0x1f reserved for controller internal use. */
	__le32 reserved[3];
};

/* ep_info bitmasks. */
/*
 * Endpoint State - bits 0:2:
 * 0 - disabled
 * 1 - running
 * 2 - halted due to halt condition
 * 3 - stopped
 * 4 - TRB error
 * 5-7 - reserved
 */
#define EP_STATE_MASK		GENMASK(3, 0)
#define EP_STATE_DISABLED	0
#define EP_STATE_RUNNING	1
#define EP_STATE_HALTED		2
#define EP_STATE_STOPPED	3
#define EP_STATE_ERROR		4
#define GET_EP_CTX_STATE(ctx)	(le32_to_cpu((ctx)->ep_info) & EP_STATE_MASK)

/* Mult - Max number of burst within an interval, in EP companion desc. */
#define EP_MULT(p)			(((p) << 8) & GENMASK(9, 8))
#define CTX_TO_EP_MULT(p)		(((p) & GENMASK(9, 8)) >> 8)
/* bits 10:14 are Max Primary Streams. */
/* bit 15 is Linear Stream Array. */
/* Interval - period between requests to an endpoint - 125u increments. */
#define EP_INTERVAL(p)			(((p) << 16) & GENMASK(23, 16))
#define EP_INTERVAL_TO_UFRAMES(p)	(1 << (((p) & GENMASK(23, 16)) >> 16))
#define CTX_TO_EP_INTERVAL(p)		(((p) & GENMASK(23, 16)) >> 16)
#define EP_MAXPSTREAMS_MASK		GENMASK(14, 10)
#define EP_MAXPSTREAMS(p)		(((p) << 10) & EP_MAXPSTREAMS_MASK)
#define CTX_TO_EP_MAXPSTREAMS(p)	(((p) & EP_MAXPSTREAMS_MASK) >> 10)
/* Endpoint is set up with a Linear Stream Array (vs. Secondary Stream Array) */
#define EP_HAS_LSA			BIT(15)

/* ep_info2 bitmasks */
#define ERROR_COUNT(p)		(((p) & 0x3) << 1)
#define CTX_TO_EP_TYPE(p)	(((p) >> 3) & 0x7)
#define EP_TYPE(p)		((p) << 3)
#define ISOC_OUT_EP		1
#define BULK_OUT_EP		2
#define INT_OUT_EP		3
#define CTRL_EP			4
#define ISOC_IN_EP		5
#define BULK_IN_EP		6
#define INT_IN_EP		7
/* bit 6 reserved. */
/* bit 7 is Device Initiate Disable - for disabling stream selection. */
#define MAX_BURST(p)		(((p) << 8) & GENMASK(15, 8))
#define CTX_TO_MAX_BURST(p)	(((p) & GENMASK(15, 8)) >> 8)
#define MAX_PACKET(p)		(((p) << 16) & GENMASK(31, 16))
#define MAX_PACKET_MASK		GENMASK(31, 16)
#define MAX_PACKET_DECODED(p)	(((p) & GENMASK(31, 16)) >> 16)

/* tx_info bitmasks. */
#define EP_AVG_TRB_LENGTH(p)		((p) & GENMASK(15, 0))
#define EP_MAX_ESIT_PAYLOAD_LO(p)	(((p) << 16) & GENMASK(31, 16))
#define EP_MAX_ESIT_PAYLOAD_HI(p)	((((p) & GENMASK(23, 16)) >> 16) << 24)
#define CTX_TO_MAX_ESIT_PAYLOAD_LO(p)	(((p) & GENMASK(31, 16)) >> 16)
#define CTX_TO_MAX_ESIT_PAYLOAD_HI(p)	(((p) & GENMASK(31, 24)) >> 24)

/* deq bitmasks. */
#define EP_CTX_CYCLE_MASK		BIT(0)
#define CTX_DEQ_MASK			(~0xfL)

/**
 * struct cdnsp_input_control_context
 * Input control context;
 *
 * @drop_context: Set the bit of the endpoint context you want to disable.
 * @add_context: Set the bit of the endpoint context you want to enable.
 */
struct cdnsp_input_control_ctx {
	__le32 drop_flags;
	__le32 add_flags;
	__le32 rsvd2[6];
};

/**
 * Represents everything that is needed to issue a command on the command ring.
 *
 * @in_ctx: Pointer to input context structure.
 * @status: Command Completion Code for last command.
 * @command_trb: Pointer to command TRB.
 */
struct cdnsp_command {
	/* Input context for changing device state. */
	struct cdnsp_container_ctx *in_ctx;
	u32 status;
	union cdnsp_trb *command_trb;
};

/**
 * Stream context structure.
 *
 * @stream_ring: 64-bit stream ring address, cycle state, and stream type.
 * @reserved: offset 0x14 - 0x1f reserved for controller internal use.
 */
struct cdnsp_stream_ctx {
	__le64 stream_ring;
	__le32 reserved[2];
};

/* Stream Context Types - bits 3:1 of stream ctx deq ptr. */
#define SCT_FOR_CTX(p)		(((p) << 1) & GENMASK(3, 1))
/* Secondary stream array type, dequeue pointer is to a transfer ring. */
#define SCT_SEC_TR		0
/* Primary stream array type, dequeue pointer is to a transfer ring. */
#define SCT_PRI_TR		1

/**
 *  struct cdnsp_stream_info: Representing everything that is needed to
 *                            supports stream capable endpoints.
 *  @stream_rings: Array of pointers containing Transfer rings for all
 *                 supported streams.
 *  @num_streams: Number of streams, including stream 0.
 *  @stream_ctx_array: The stream context array may be bigger than the number
 *                     of streams the driver asked for.
 *  @num_stream_ctxs: Number of streams.
 *  @ctx_array_dma: Dma address of Context Stream Array.
 *  @trb_address_map: For mapping physical TRB addresses to segments in
 *                    stream rings.
 *  @td_count: Number of TDs associated with endpoint.
 *  @first_prime_det: First PRIME packet detected.
 *  @drbls_count: Number of allowed doorbells.
 */
struct cdnsp_stream_info {
	struct cdnsp_ring **stream_rings;
	unsigned int num_streams;
	struct cdnsp_stream_ctx *stream_ctx_array;
	unsigned int num_stream_ctxs;
	dma_addr_t ctx_array_dma;
	struct radix_tree_root trb_address_map;
	int td_count;
	u8 first_prime_det;
#define STREAM_DRBL_FIFO_DEPTH 2
	u8 drbls_count;
};

#define STREAM_LOG_STREAMS 4
#define STREAM_NUM_STREAMS BIT(STREAM_LOG_STREAMS)

#if STREAM_LOG_STREAMS > 16 && STREAM_LOG_STREAMS < 1
#error "Not suupported stream value"
#endif

/**
 * struct cdnsp_ep - extended device side representation of USB endpoint.
 * @endpoint: usb endpoint
 * @pending_req_list: List of requests queuing on transfer ring.
 * @pdev: Device associated with this endpoint.
 * @number: Endpoint number (1 - 15).
 * idx: The device context index (DCI).
 * interval: Interval between packets used for ISOC endpoint.
 * @name: A human readable name e.g. ep1out.
 * @direction: Endpoint direction.
 * @buffering: Number of on-chip buffers related to endpoint.
 * @buffering_period; Number of on-chip buffers related to periodic endpoint.
 * @in_ctx: Pointer to input endpoint context structure.
 * @out_ctx: Pointer to output endpoint context structure.
 * @ring: Pointer to transfer ring.
 * @stream_info: Hold stream information.
 * @ep_state: Current state of endpoint.
 * @skip: Sometimes the controller can not process isochronous endpoint ring
 *        quickly enough, and it will miss some isoc tds on the ring and
 *        generate Missed Service Error Event.
 *        Set skip flag when receive a Missed Service Error Event and
 *        process the missed tds on the endpoint ring.
 */
struct cdnsp_ep {
	struct usb_ep endpoint;
	struct list_head pending_list;
	struct cdnsp_device *pdev;
	u8 number;
	u8 idx;
	u32 interval;
	char name[20];
	u8 direction;
	u8 buffering;
	u8 buffering_period;
	struct cdnsp_ep_ctx *in_ctx;
	struct cdnsp_ep_ctx *out_ctx;
	struct cdnsp_ring *ring;
	struct cdnsp_stream_info stream_info;
	unsigned int ep_state;
#define EP_ENABLED		BIT(0)
#define EP_DIS_IN_RROGRESS	BIT(1)
#define EP_HALTED		BIT(2)
#define EP_STOPPED		BIT(3)
#define EP_WEDGE		BIT(4)
#define EP0_HALTED_STATUS	BIT(5)
#define EP_HAS_STREAMS		BIT(6)

	bool skip;
};

/**
 * struct cdnsp_device_context_array
 * @dev_context_ptr: Array of 64-bit DMA addresses for device contexts.
 * @dma: DMA address for device contexts structure.
 */
struct cdnsp_device_context_array {
	__le64 dev_context_ptrs[CDNSP_DEV_MAX_SLOTS + 1];
	dma_addr_t dma;
};

/**
 * struct cdnsp_transfer_event.
 * @buffer: 64-bit buffer address, or immediate data.
 * @transfer_len: Data length transferred.
 * @flags: Field is interpreted differently based on the type of TRB.
 */
struct cdnsp_transfer_event {
	__le64 buffer;
	__le32 transfer_len;
	__le32 flags;
};

/* Invalidate event after disabling endpoint. */
#define TRB_EVENT_INVALIDATE 8

/* Transfer event TRB length bit mask. */
/* bits 0:23 */
#define EVENT_TRB_LEN(p)			((p) & GENMASK(23, 0))
/* Completion Code - only applicable for some types of TRBs */
#define COMP_CODE_MASK				(0xff << 24)
#define GET_COMP_CODE(p)			(((p) & COMP_CODE_MASK) >> 24)
#define COMP_INVALID				0
#define COMP_SUCCESS				1
#define COMP_DATA_BUFFER_ERROR			2
#define COMP_BABBLE_DETECTED_ERROR		3
#define COMP_TRB_ERROR				5
#define COMP_RESOURCE_ERROR			7
#define COMP_NO_SLOTS_AVAILABLE_ERROR		9
#define COMP_INVALID_STREAM_TYPE_ERROR		10
#define COMP_SLOT_NOT_ENABLED_ERROR		11
#define COMP_ENDPOINT_NOT_ENABLED_ERROR		12
#define COMP_SHORT_PACKET			13
#define COMP_RING_UNDERRUN			14
#define COMP_RING_OVERRUN			15
#define COMP_VF_EVENT_RING_FULL_ERROR		16
#define COMP_PARAMETER_ERROR			17
#define COMP_CONTEXT_STATE_ERROR		19
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

/*Transfer Event NRDY bit fields */
#define TRB_TO_DEV_STREAM(p)			((p) & GENMASK(16, 0))
#define TRB_TO_HOST_STREAM(p)			((p) & GENMASK(16, 0))
#define STREAM_PRIME_ACK			0xFFFE
#define STREAM_REJECTED				0xFFFF

/** Transfer Event bit fields **/
#define TRB_TO_EP_ID(p)				(((p) & GENMASK(20, 16)) >> 16)

/**
 * struct cdnsp_link_trb
 * @segment_ptr: 64-bit segment pointer.
 * @intr_target: Interrupter target.
 * @control: Flags.
 */
struct cdnsp_link_trb {
	__le64 segment_ptr;
	__le32 intr_target;
	__le32 control;
};

/* control bitfields */
#define LINK_TOGGLE	BIT(1)

/**
 * struct cdnsp_event_cmd - Command completion event TRB.
 * cmd_trb: Pointer to command TRB, or the value passed by the event data trb
 * status: Command completion parameters and error code.
 * flags: Flags.
 */
struct cdnsp_event_cmd {
	__le64 cmd_trb;
	__le32 status;
	__le32 flags;
};

/* flags bitmasks */

/* Address device - disable SetAddress. */
#define TRB_BSR		BIT(9)

/* Configure Endpoint - Deconfigure. */
#define TRB_DC		BIT(9)

/* Force Header */
#define TRB_FH_TO_PACKET_TYPE(p)	((p) & GENMASK(4, 0))
#define TRB_FH_TR_PACKET		0x4
#define TRB_FH_TO_DEVICE_ADDRESS(p)	(((p) << 25) & GENMASK(31, 25))
#define TRB_FH_TR_PACKET_DEV_NOT	0x6
#define TRB_FH_TO_NOT_TYPE(p)		(((p) << 4) & GENMASK(7, 4))
#define TRB_FH_TR_PACKET_FUNCTION_WAKE	0x1
#define TRB_FH_TO_INTERFACE(p)		(((p) << 8) & GENMASK(15, 8))

enum cdnsp_setup_dev {
	SETUP_CONTEXT_ONLY,
	SETUP_CONTEXT_ADDRESS,
};

/* bits 24:31 are the slot ID. */
#define TRB_TO_SLOT_ID(p)		(((p) & GENMASK(31, 24)) >> 24)
#define SLOT_ID_FOR_TRB(p)		(((p) << 24) & GENMASK(31, 24))

/* Stop Endpoint TRB - ep_index to endpoint ID for this TRB. */
#define TRB_TO_EP_INDEX(p)		(((p) >> 16) & 0x1f)

#define EP_ID_FOR_TRB(p)		((((p) + 1) << 16) & GENMASK(20, 16))

#define SUSPEND_PORT_FOR_TRB(p)		(((p) & 1) << 23)
#define TRB_TO_SUSPEND_PORT(p)		(((p) >> 23) & 0x1)
#define LAST_EP_INDEX			30

/* Set TR Dequeue Pointer command TRB fields. */
#define TRB_TO_STREAM_ID(p)		((((p) & GENMASK(31, 16)) >> 16))
#define STREAM_ID_FOR_TRB(p)		((((p)) << 16) & GENMASK(31, 16))
#define SCT_FOR_TRB(p)			(((p) << 1) & 0x7)

/* Link TRB specific fields. */
#define TRB_TC				BIT(1)

/* Port Status Change Event TRB fields. */
/* Port ID - bits 31:24. */
#define GET_PORT_ID(p)			(((p) & GENMASK(31, 24)) >> 24)
#define SET_PORT_ID(p)			(((p) << 24) & GENMASK(31, 24))
#define EVENT_DATA			BIT(2)

/* Normal TRB fields. */
/* transfer_len bitmasks - bits 0:16. */
#define TRB_LEN(p)			((p) & GENMASK(16, 0))
/* TD Size, packets remaining in this TD, bits 21:17 (5 bits, so max 31). */
#define TRB_TD_SIZE(p)			(min((p), (u32)31) << 17)
#define GET_TD_SIZE(p)			(((p) & GENMASK(21, 17)) >> 17)
/*
 * Controller uses the TD_SIZE field for TBC if Extended TBC
 * is enabled (ETE).
 */
#define TRB_TD_SIZE_TBC(p)		(min((p), (u32)31) << 17)
/* Interrupter Target - which MSI-X vector to target the completion event at. */
#define TRB_INTR_TARGET(p)		(((p) << 22) & GENMASK(31, 22))
#define GET_INTR_TARGET(p)		(((p) & GENMASK(31, 22)) >> 22)
/*
 * Total burst count field, Rsvdz on controller with Extended TBC
 * enabled (ETE).
 */
#define TRB_TBC(p)			(((p) & 0x3) << 7)
#define TRB_TLBPC(p)			(((p) & 0xf) << 16)

/* Cycle bit - indicates TRB ownership by driver or driver.*/
#define TRB_CYCLE			BIT(0)
/*
 * Force next event data TRB to be evaluated before task switch.
 * Used to pass OS data back after a TD completes.
 */
#define TRB_ENT				BIT(1)
/* Interrupt on short packet. */
#define TRB_ISP				BIT(2)
/* Set PCIe no snoop attribute. */
#define TRB_NO_SNOOP			BIT(3)
/* Chain multiple TRBs into a TD. */
#define TRB_CHAIN			BIT(4)
/* Interrupt on completion. */
#define TRB_IOC				BIT(5)
/* The buffer pointer contains immediate data. */
#define TRB_IDT				BIT(6)
/* 0 - NRDY during data stage, 1 - NRDY during status stage (only control). */
#define TRB_STAT			BIT(7)
/* Block Event Interrupt. */
#define TRB_BEI				BIT(9)

/* Control transfer TRB specific fields. */
#define TRB_DIR_IN			BIT(16)

/* TRB bit mask in Data Stage TRB */
#define TRB_SETUPID_BITMASK		GENMASK(9, 8)
#define TRB_SETUPID(p)			((p) << 8)
#define TRB_SETUPID_TO_TYPE(p)		(((p) & TRB_SETUPID_BITMASK) >> 8)

#define TRB_SETUP_SPEEDID_USB3		0x1
#define TRB_SETUP_SPEEDID_USB2		0x0
#define TRB_SETUP_SPEEDID(p)		((p) & (1 << 7))

#define TRB_SETUPSTAT_ACK		0x1
#define TRB_SETUPSTAT_STALL		0x0
#define TRB_SETUPSTAT(p)		((p) << 6)

/* Isochronous TRB specific fields */
#define TRB_SIA				BIT(31)
#define TRB_FRAME_ID(p)			(((p) << 20) & GENMASK(30, 20))

struct cdnsp_generic_trb {
	__le32 field[4];
};

union cdnsp_trb {
	struct cdnsp_link_trb link;
	struct cdnsp_transfer_event trans_event;
	struct cdnsp_event_cmd event_cmd;
	struct cdnsp_generic_trb generic;
};

/* TRB bit mask. */
#define TRB_TYPE_BITMASK	GENMASK(15, 10)
#define TRB_TYPE(p)		((p) << 10)
#define TRB_FIELD_TO_TYPE(p)	(((p) & TRB_TYPE_BITMASK) >> 10)

/* TRB type IDs. */
/* bulk, interrupt, isoc scatter/gather, and control data stage. */
#define TRB_NORMAL		1
/* Setup Stage for control transfers. */
#define TRB_SETUP		2
/* Data Stage for control transfers. */
#define TRB_DATA		3
/* Status Stage for control transfers. */
#define TRB_STATUS		4
/* ISOC transfers. */
#define TRB_ISOC		5
/* TRB for linking ring segments. */
#define TRB_LINK		6
#define TRB_EVENT_DATA		7
/* Transfer Ring No-op (not for the command ring). */
#define TRB_TR_NOOP		8

/* Command TRBs */
/* Enable Slot Command. */
#define TRB_ENABLE_SLOT		9
/* Disable Slot Command. */
#define TRB_DISABLE_SLOT	10
/* Address Device Command. */
#define TRB_ADDR_DEV		11
/* Configure Endpoint Command. */
#define TRB_CONFIG_EP		12
/* Evaluate Context Command. */
#define TRB_EVAL_CONTEXT	13
/* Reset Endpoint Command. */
#define TRB_RESET_EP		14
/* Stop Transfer Ring Command. */
#define TRB_STOP_RING		15
/* Set Transfer Ring Dequeue Pointer Command. */
#define TRB_SET_DEQ		16
/* Reset Device Command. */
#define TRB_RESET_DEV		17
/* Force Event Command (opt). */
#define TRB_FORCE_EVENT		18
/* Force Header Command - generate a transaction or link management packet. */
#define TRB_FORCE_HEADER	22
/* No-op Command - not for transfer rings. */
#define TRB_CMD_NOOP		23
/* TRB IDs 24-31 reserved. */

/* Event TRBS. */
/* Transfer Event. */
#define TRB_TRANSFER		32
/* Command Completion Event. */
#define TRB_COMPLETION		33
/* Port Status Change Event. */
#define TRB_PORT_STATUS		34
/* Device Controller Event. */
#define TRB_HC_EVENT		37
/* MFINDEX Wrap Event - microframe counter wrapped. */
#define TRB_MFINDEX_WRAP	39
/* TRB IDs 40-47 reserved. */
/* Endpoint Not Ready Event. */
#define TRB_ENDPOINT_NRDY	48
/* TRB IDs 49-53 reserved. */
/* Halt Endpoint Command. */
#define TRB_HALT_ENDPOINT	54
/* Doorbell Overflow Event. */
#define TRB_DRB_OVERFLOW	57
/* Flush Endpoint Command. */
#define TRB_FLUSH_ENDPOINT	58

#define TRB_TYPE_LINK(x)	(((x) & TRB_TYPE_BITMASK) == TRB_TYPE(TRB_LINK))
#define TRB_TYPE_LINK_LE32(x)	(((x) & cpu_to_le32(TRB_TYPE_BITMASK)) == \
					cpu_to_le32(TRB_TYPE(TRB_LINK)))
#define TRB_TYPE_NOOP_LE32(x)	(((x) & cpu_to_le32(TRB_TYPE_BITMASK)) == \
					cpu_to_le32(TRB_TYPE(TRB_TR_NOOP)))

/*
 * TRBS_PER_SEGMENT must be a multiple of 4.
 * The command ring is 64-byte aligned, so it must also be greater than 16.
 */
#define TRBS_PER_SEGMENT		256
#define TRBS_PER_EVENT_SEGMENT		256
#define TRBS_PER_EV_DEQ_UPDATE		100
#define TRB_SEGMENT_SIZE		(TRBS_PER_SEGMENT * 16)
#define TRB_SEGMENT_SHIFT		(ilog2(TRB_SEGMENT_SIZE))
/* TRB buffer pointers can't cross 64KB boundaries. */
#define TRB_MAX_BUFF_SHIFT		16
#define TRB_MAX_BUFF_SIZE		BIT(TRB_MAX_BUFF_SHIFT)
/* How much data is left before the 64KB boundary? */
#define TRB_BUFF_LEN_UP_TO_BOUNDARY(addr) (TRB_MAX_BUFF_SIZE - \
					((addr) & (TRB_MAX_BUFF_SIZE - 1)))

/**
 * struct cdnsp_segment - segment related data.
 * @trbs: Array of Transfer Request Blocks.
 * @next: Pointer to the next segment.
 * @dma: DMA address of current segment.
 * @bounce_dma: Bounce  buffer DMA address .
 * @bounce_buf: Bounce buffer virtual address.
 * bounce_offs: Bounce buffer offset.
 * bounce_len: Bounce buffer length.
 */
struct cdnsp_segment {
	union cdnsp_trb *trbs;
	struct cdnsp_segment *next;
	dma_addr_t dma;
	/* Max packet sized bounce buffer for td-fragmant alignment */
	dma_addr_t bounce_dma;
	void *bounce_buf;
	unsigned int bounce_offs;
	unsigned int bounce_len;
};

/**
 * struct cdnsp_td - Transfer Descriptor object.
 * @td_list: Used for binding TD with ep_ring->td_list.
 * @preq: Request associated with this TD
 * @start_seg: Segment containing the first_trb in TD.
 * @first_trb: First TRB for this TD.
 * @last_trb: Last TRB related with TD.
 * @bounce_seg: Bounce segment for this TD.
 * @request_length_set: actual_length of the request has already been set.
 * @drbl - TD has been added to HW scheduler - only for stream capable
 *         endpoints.
 */
struct cdnsp_td {
	struct list_head td_list;
	struct cdnsp_request *preq;
	struct cdnsp_segment *start_seg;
	union cdnsp_trb *first_trb;
	union cdnsp_trb *last_trb;
	struct cdnsp_segment *bounce_seg;
	bool request_length_set;
	bool drbl;
};

/**
 * struct cdnsp_dequeue_state - New dequeue pointer for Transfer Ring.
 * @new_deq_seg: New dequeue segment.
 * @new_deq_ptr: New dequeue pointer.
 * @new_cycle_state: New cycle state.
 * @stream_id: stream id for which new dequeue pointer has been selected.
 */
struct cdnsp_dequeue_state {
	struct cdnsp_segment *new_deq_seg;
	union cdnsp_trb *new_deq_ptr;
	int new_cycle_state;
	unsigned int stream_id;
};

enum cdnsp_ring_type {
	TYPE_CTRL = 0,
	TYPE_ISOC,
	TYPE_BULK,
	TYPE_INTR,
	TYPE_STREAM,
	TYPE_COMMAND,
	TYPE_EVENT,
};

/**
 * struct cdnsp_ring - information describing transfer, command or event ring.
 * @first_seg: First segment on transfer ring.
 * @last_seg: Last segment on transfer ring.
 * @enqueue: SW enqueue pointer address.
 * @enq_seg: SW enqueue segment address.
 * @dequeue: SW dequeue pointer address.
 * @deq_seg: SW dequeue segment address.
 * @td_list: transfer descriptor list associated with this ring.
 * @cycle_state: Current cycle bit. Write the cycle state into the TRB cycle
 *               field to give ownership of the TRB to the device controller
 *               (if we are the producer) or to check if we own the TRB
 *               (if we are the consumer).
 * @stream_id: Stream id
 * @stream_active: Stream is active - PRIME packet has been detected.
 * @stream_rejected: This ring has been rejected by host.
 * @num_tds: Number of TDs associated with ring.
 * @num_segs: Number of segments.
 * @num_trbs_free: Number of free TRBs on the ring.
 * @bounce_buf_len: Length of bounce buffer.
 * @type: Ring type - event, transfer, or command ring.
 * @last_td_was_short - TD is short TD.
 * @trb_address_map: For mapping physical TRB addresses to segments in
 *                   stream rings.
 */
struct cdnsp_ring {
	struct cdnsp_segment *first_seg;
	struct cdnsp_segment *last_seg;
	union cdnsp_trb	 *enqueue;
	struct cdnsp_segment *enq_seg;
	union cdnsp_trb	 *dequeue;
	struct cdnsp_segment *deq_seg;
	struct list_head td_list;
	u32 cycle_state;
	unsigned int stream_id;
	unsigned int stream_active;
	unsigned int stream_rejected;
	int num_tds;
	unsigned int num_segs;
	unsigned int num_trbs_free;
	unsigned int bounce_buf_len;
	enum cdnsp_ring_type type;
	bool last_td_was_short;
	struct radix_tree_root *trb_address_map;
};

/**
 * struct cdnsp_erst_entry - even ring segment table entry object.
 * @seg_addr: 64-bit event ring segment address.
 * seg_size: Number of TRBs in segment.;
 */
struct cdnsp_erst_entry {
	__le64 seg_addr;
	__le32 seg_size;
	/* Set to zero */
	__le32 rsvd;
};

/**
 * struct cdnsp_erst - even ring segment table for event ring.
 * @entries: Array of event ring segments
 * @num_entries: Number of segments in entries array.
 * @erst_dma_addr: DMA address for entries array.
 */
struct cdnsp_erst {
	struct cdnsp_erst_entry *entries;
	unsigned int num_entries;
	dma_addr_t erst_dma_addr;
};

/**
 * struct cdnsp_request - extended device side representation of usb_request
 *                        object .
 * @td: Transfer descriptor associated with this request.
 * @request: Generic usb_request object describing single I/O request.
 * @list: Used to adding request to endpoint pending_list.
 * @pep: Extended representation of usb_ep object
 * @epnum: Endpoint number associated with usb request.
 * @direction: Endpoint direction for usb request.
 */
struct cdnsp_request {
	struct	cdnsp_td td;
	struct usb_request request;
	struct list_head list;
	struct cdnsp_ep	 *pep;
	u8 epnum;
	unsigned direction:1;
};

#define	ERST_NUM_SEGS	1

/* Stages used during enumeration process.*/
enum cdnsp_ep0_stage {
	CDNSP_SETUP_STAGE,
	CDNSP_DATA_STAGE,
	CDNSP_STATUS_STAGE,
};

/**
 * struct cdnsp_port - holds information about detected ports.
 * @port_num: Port number.
 * @exist: Indicate if port exist.
 * maj_rev: Major revision.
 * min_rev: Minor revision.
 */
struct cdnsp_port {
	struct cdnsp_port_regs __iomem *regs;
	u8 port_num;
	u8 exist;
	u8 maj_rev;
	u8 min_rev;
};

#define CDNSP_EXT_PORT_MAJOR(x)		(((x) >> 24) & 0xff)
#define CDNSP_EXT_PORT_MINOR(x)		(((x) >> 16) & 0xff)
#define CDNSP_EXT_PORT_OFF(x)		((x) & 0xff)
#define CDNSP_EXT_PORT_COUNT(x)		(((x) >> 8) & 0xff)

/**
 * struct cdnsp_device - represent USB device.
 * @dev: Pointer to device structure associated whit this controller.
 * @gadget: Device side representation of the peripheral controller.
 * @gadget_driver: Pointer to the gadget driver.
 * @irq: IRQ line number used by device side.
 * @regs:IO device memory.
 * @cap_regs: Capability registers.
 * @op_regs: Operational registers.
 * @run_regs: Runtime registers.
 * @dba: Device base address register.
 * @ir_set: Current interrupter register set.
 * @port20_regs: Port 2.0 Peripheral Configuration Registers.
 * @port3x_regs: USB3.x Port Peripheral Configuration Registers.
 * @rev_cap: Controller Capabilities Registers.
 * @hcs_params1: Cached register copies of read-only HCSPARAMS1
 * @hcc_params: Cached register copies of read-only HCCPARAMS1
 * @setup: Temporary buffer for setup packet.
 * @ep0_preq: Internal allocated request used during enumeration.
 * @ep0_stage: ep0 stage during enumeration process.
 * @three_stage_setup: Three state or two state setup.
 * @ep0_expect_in: Data IN expected for control transfer.
 * @setup_id: Setup identifier.
 * @setup_speed - Speed detected for current SETUP packet.
 * @setup_buf: Buffer for SETUP packet.
 * @device_address: Current device address.
 * @may_wakeup: remote wakeup enabled/disabled.
 * @lock: Lock used in interrupt thread context.
 * @hci_version: device controller version.
 * @dcbaa: Device context base address array.
 * @cmd_ring: Command ring.
 * @cmd: Represent all what is needed to issue command on Command Ring.
 * @event_ring: Event ring.
 * @erst: Event Ring Segment table
 * @slot_id: Current Slot ID. Should be 0 or 1.
 * @out_ctx: Output context.
 * @in_ctx: Input context.
 * @eps: array of endpoints object associated with device.
 * @usb2_hw_lpm_capable: hardware lpm is enabled;
 * @u1_allowed: Allow device transition to U1 state.
 * @u2_allowed: Allow device transition to U2 state
 * @device_pool: DMA pool for allocating input and output context.
 * @segment_pool: DMA pool for allocating new segments.
 * @cdnsp_state: Current state of controller.
 * @link_state: Current link state.
 * @usb2_port - Port USB 2.0.
 * @usb3_port - Port USB 3.0.
 * @active_port - Current selected Port.
 * @test_mode: selected Test Mode.
 */
struct cdnsp_device {
	struct device *dev;
	struct usb_gadget gadget;
	struct usb_gadget_driver *gadget_driver;
	unsigned int irq;
	void __iomem *regs;

	/* Registers map */
	struct cdnsp_cap_regs __iomem *cap_regs;
	struct cdnsp_op_regs __iomem *op_regs;
	struct cdnsp_run_regs __iomem *run_regs;
	struct cdnsp_doorbell_array __iomem *dba;
	struct	cdnsp_intr_reg __iomem *ir_set;
	struct cdnsp_20port_cap __iomem *port20_regs;
	struct cdnsp_3xport_cap __iomem *port3x_regs;
	struct cdnsp_rev_cap __iomem *rev_cap;

	/* Cached register copies of read-only CDNSP data */
	__u32 hcs_params1;
	__u32 hcs_params3;
	__u32 hcc_params;
	/* Lock used in interrupt thread context. */
	spinlock_t lock;
	struct usb_ctrlrequest setup;
	struct cdnsp_request ep0_preq;
	enum cdnsp_ep0_stage ep0_stage;
	u8 three_stage_setup;
	u8 ep0_expect_in;
	u8 setup_id;
	u8 setup_speed;
	void *setup_buf;
	u8 device_address;
	int may_wakeup;
	u16 hci_version;

	/* data structures */
	struct cdnsp_device_context_array *dcbaa;
	struct cdnsp_ring *cmd_ring;
	struct cdnsp_command cmd;
	struct cdnsp_ring *event_ring;
	struct cdnsp_erst erst;
	int slot_id;

	/*
	 * Commands to the hardware are passed an "input context" that
	 * tells the hardware what to change in its data structures.
	 * The hardware will return changes in an "output context" that
	 * software must allocate for the hardware. .
	 */
	struct cdnsp_container_ctx out_ctx;
	struct cdnsp_container_ctx in_ctx;
	struct cdnsp_ep eps[CDNSP_ENDPOINTS_NUM];
	u8 usb2_hw_lpm_capable:1;
	u8 u1_allowed:1;
	u8 u2_allowed:1;

	/* DMA pools */
	struct dma_pool *device_pool;
	struct dma_pool	*segment_pool;

#define CDNSP_STATE_HALTED		BIT(1)
#define CDNSP_STATE_DYING		BIT(2)
#define CDNSP_STATE_DISCONNECT_PENDING	BIT(3)
#define CDNSP_WAKEUP_PENDING		BIT(4)
	unsigned int cdnsp_state;
	unsigned int link_state;

	struct cdnsp_port usb2_port;
	struct cdnsp_port usb3_port;
	struct cdnsp_port *active_port;
	u16 test_mode;
};

/*
 * Registers should always be accessed with double word or quad word accesses.
 *
 * Registers with 64-bit address pointers should be written to with
 * dword accesses by writing the low dword first (ptr[0]), then the high dword
 * (ptr[1]) second. controller implementations that do not support 64-bit
 * address pointers will ignore the high dword, and write order is irrelevant.
 */
static inline u64 cdnsp_read_64(__le64 __iomem *regs)
{
	return lo_hi_readq(regs);
}

static inline void cdnsp_write_64(const u64 val, __le64 __iomem *regs)
{
	lo_hi_writeq(val, regs);
}

/* CDNSP memory management functions. */
void cdnsp_mem_cleanup(struct cdnsp_device *pdev);
int cdnsp_mem_init(struct cdnsp_device *pdev);
int cdnsp_setup_addressable_priv_dev(struct cdnsp_device *pdev);
void cdnsp_copy_ep0_dequeue_into_input_ctx(struct cdnsp_device *pdev);
void cdnsp_endpoint_zero(struct cdnsp_device *pdev, struct cdnsp_ep *ep);
int cdnsp_endpoint_init(struct cdnsp_device *pdev,
			struct cdnsp_ep *pep,
			gfp_t mem_flags);
int cdnsp_ring_expansion(struct cdnsp_device *pdev,
			 struct cdnsp_ring *ring,
			 unsigned int num_trbs, gfp_t flags);
struct cdnsp_ring *cdnsp_dma_to_transfer_ring(struct cdnsp_ep *ep, u64 address);
int cdnsp_alloc_stream_info(struct cdnsp_device *pdev,
			    struct cdnsp_ep *pep,
			    unsigned int num_stream_ctxs,
			    unsigned int num_streams);
int cdnsp_alloc_streams(struct cdnsp_device *pdev, struct cdnsp_ep *pep);
void cdnsp_free_endpoint_rings(struct cdnsp_device *pdev, struct cdnsp_ep *pep);

/* Device controller glue. */
int cdnsp_find_next_ext_cap(void __iomem *base, u32 start, int id);
int cdnsp_halt(struct cdnsp_device *pdev);
void cdnsp_died(struct cdnsp_device *pdev);
int cdnsp_reset(struct cdnsp_device *pdev);
irqreturn_t cdnsp_irq_handler(int irq, void *priv);
int cdnsp_setup_device(struct cdnsp_device *pdev, enum cdnsp_setup_dev setup);
void cdnsp_set_usb2_hardware_lpm(struct cdnsp_device *usbsssp_data,
				 struct usb_request *req, int enable);
irqreturn_t cdnsp_thread_irq_handler(int irq, void *data);

/* Ring, segment, TRB, and TD functions. */
dma_addr_t cdnsp_trb_virt_to_dma(struct cdnsp_segment *seg,
				 union cdnsp_trb *trb);
bool cdnsp_last_trb_on_seg(struct cdnsp_segment *seg, union cdnsp_trb *trb);
bool cdnsp_last_trb_on_ring(struct cdnsp_ring *ring,
			    struct cdnsp_segment *seg,
			    union cdnsp_trb *trb);
int cdnsp_wait_for_cmd_compl(struct cdnsp_device *pdev);
void cdnsp_update_erst_dequeue(struct cdnsp_device *pdev,
			       union cdnsp_trb *event_ring_deq,
			       u8 clear_ehb);
void cdnsp_initialize_ring_info(struct cdnsp_ring *ring);
void cdnsp_ring_cmd_db(struct cdnsp_device *pdev);
void cdnsp_queue_slot_control(struct cdnsp_device *pdev, u32 trb_type);
void cdnsp_queue_address_device(struct cdnsp_device *pdev,
				dma_addr_t in_ctx_ptr,
				enum cdnsp_setup_dev setup);
void cdnsp_queue_stop_endpoint(struct cdnsp_device *pdev,
			       unsigned int ep_index);
int cdnsp_queue_ctrl_tx(struct cdnsp_device *pdev, struct cdnsp_request *preq);
int cdnsp_queue_bulk_tx(struct cdnsp_device *pdev, struct cdnsp_request *preq);
int cdnsp_queue_isoc_tx_prepare(struct cdnsp_device *pdev,
				struct cdnsp_request *preq);
void cdnsp_queue_configure_endpoint(struct cdnsp_device *pdev,
				    dma_addr_t in_ctx_ptr);
void cdnsp_queue_reset_ep(struct cdnsp_device *pdev, unsigned int ep_index);
void cdnsp_queue_halt_endpoint(struct cdnsp_device *pdev,
			       unsigned int ep_index);
void cdnsp_queue_flush_endpoint(struct cdnsp_device *pdev,
				unsigned int ep_index);
void cdnsp_force_header_wakeup(struct cdnsp_device *pdev, int intf_num);
void cdnsp_queue_reset_device(struct cdnsp_device *pdev);
void cdnsp_queue_new_dequeue_state(struct cdnsp_device *pdev,
				   struct cdnsp_ep *pep,
				   struct cdnsp_dequeue_state *deq_state);
void cdnsp_ring_doorbell_for_active_rings(struct cdnsp_device *pdev,
					  struct cdnsp_ep *pep);
void cdnsp_inc_deq(struct cdnsp_device *pdev, struct cdnsp_ring *ring);
void cdnsp_set_link_state(struct cdnsp_device *pdev,
			  __le32 __iomem *port_regs, u32 link_state);
u32 cdnsp_port_state_to_neutral(u32 state);

/* CDNSP device controller contexts. */
int cdnsp_enable_slot(struct cdnsp_device *pdev);
int cdnsp_disable_slot(struct cdnsp_device *pdev);
struct cdnsp_input_control_ctx
	*cdnsp_get_input_control_ctx(struct cdnsp_container_ctx *ctx);
struct cdnsp_slot_ctx *cdnsp_get_slot_ctx(struct cdnsp_container_ctx *ctx);
struct cdnsp_ep_ctx *cdnsp_get_ep_ctx(struct cdnsp_container_ctx *ctx,
				      unsigned int ep_index);
/* CDNSP gadget interface. */
void cdnsp_suspend_gadget(struct cdnsp_device *pdev);
void cdnsp_resume_gadget(struct cdnsp_device *pdev);
void cdnsp_disconnect_gadget(struct cdnsp_device *pdev);
void cdnsp_gadget_giveback(struct cdnsp_ep *pep, struct cdnsp_request *preq,
			   int status);
int cdnsp_ep_enqueue(struct cdnsp_ep *pep, struct cdnsp_request *preq);
int cdnsp_ep_dequeue(struct cdnsp_ep *pep, struct cdnsp_request *preq);
unsigned int cdnsp_port_speed(unsigned int port_status);
void cdnsp_irq_reset(struct cdnsp_device *pdev);
int cdnsp_halt_endpoint(struct cdnsp_device *pdev,
			struct cdnsp_ep *pep, int value);
int cdnsp_cmd_stop_ep(struct cdnsp_device *pdev, struct cdnsp_ep *pep);
int cdnsp_cmd_flush_ep(struct cdnsp_device *pdev, struct cdnsp_ep *pep);
void cdnsp_setup_analyze(struct cdnsp_device *pdev);
int cdnsp_status_stage(struct cdnsp_device *pdev);
int cdnsp_reset_device(struct cdnsp_device *pdev);

/**
 * next_request - gets the next request on the given list
 * @list: the request list to operate on
 *
 * Caller should take care of locking. This function return NULL or the first
 * request available on list.
 */
static inline struct cdnsp_request *next_request(struct list_head *list)
{
	return list_first_entry_or_null(list, struct cdnsp_request, list);
}

#define to_cdnsp_ep(ep) (container_of(ep, struct cdnsp_ep, endpoint))
#define gadget_to_cdnsp(g) (container_of(g, struct cdnsp_device, gadget))
#define request_to_cdnsp_request(r) (container_of(r, struct cdnsp_request, \
				     request))
#define to_cdnsp_request(r) (container_of(r, struct cdnsp_request, request))
int cdnsp_remove_request(struct cdnsp_device *pdev, struct cdnsp_request *preq,
			 struct cdnsp_ep *pep);

#endif /* __LINUX_CDNSP_GADGET_H */
