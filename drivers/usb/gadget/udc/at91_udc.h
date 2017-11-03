// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2004 by Thomas Rathbone, HP Labs
 * Copyright (C) 2005 by Ivan Kokshaysky
 * Copyright (C) 2006 by SAN People
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef AT91_UDC_H
#define AT91_UDC_H

/*
 * USB Device Port (UDP) registers.
 * Based on AT91RM9200 datasheet revision E.
 */

#define AT91_UDP_FRM_NUM	0x00		/* Frame Number Register */
#define     AT91_UDP_NUM	(0x7ff <<  0)	/* Frame Number */
#define     AT91_UDP_FRM_ERR	(1     << 16)	/* Frame Error */
#define     AT91_UDP_FRM_OK	(1     << 17)	/* Frame OK */

#define AT91_UDP_GLB_STAT	0x04		/* Global State Register */
#define     AT91_UDP_FADDEN	(1 <<  0)	/* Function Address Enable */
#define     AT91_UDP_CONFG	(1 <<  1)	/* Configured */
#define     AT91_UDP_ESR	(1 <<  2)	/* Enable Send Resume */
#define     AT91_UDP_RSMINPR	(1 <<  3)	/* Resume has been sent */
#define     AT91_UDP_RMWUPE	(1 <<  4)	/* Remote Wake Up Enable */

#define AT91_UDP_FADDR		0x08		/* Function Address Register */
#define     AT91_UDP_FADD	(0x7f << 0)	/* Function Address Value */
#define     AT91_UDP_FEN	(1    << 8)	/* Function Enable */

#define AT91_UDP_IER		0x10		/* Interrupt Enable Register */
#define AT91_UDP_IDR		0x14		/* Interrupt Disable Register */
#define AT91_UDP_IMR		0x18		/* Interrupt Mask Register */

#define AT91_UDP_ISR		0x1c		/* Interrupt Status Register */
#define     AT91_UDP_EP(n)	(1 << (n))	/* Endpoint Interrupt Status */
#define     AT91_UDP_RXSUSP	(1 <<  8) 	/* USB Suspend Interrupt Status */
#define     AT91_UDP_RXRSM	(1 <<  9)	/* USB Resume Interrupt Status */
#define     AT91_UDP_EXTRSM	(1 << 10)	/* External Resume Interrupt Status [AT91RM9200 only] */
#define     AT91_UDP_SOFINT	(1 << 11)	/* Start of Frame Interrupt Status */
#define     AT91_UDP_ENDBUSRES	(1 << 12)	/* End of Bus Reset Interrupt Status */
#define     AT91_UDP_WAKEUP	(1 << 13)	/* USB Wakeup Interrupt Status [AT91RM9200 only] */

#define AT91_UDP_ICR		0x20		/* Interrupt Clear Register */
#define AT91_UDP_RST_EP		0x28		/* Reset Endpoint Register */

#define AT91_UDP_CSR(n)		(0x30+((n)*4))	/* Endpoint Control/Status Registers 0-7 */
#define     AT91_UDP_TXCOMP	(1 <<  0)	/* Generates IN packet with data previously written in DPR */
#define     AT91_UDP_RX_DATA_BK0 (1 <<  1)	/* Receive Data Bank 0 */
#define     AT91_UDP_RXSETUP	(1 <<  2)	/* Send STALL to the host */
#define     AT91_UDP_STALLSENT	(1 <<  3)	/* Stall Sent / Isochronous error (Isochronous endpoints) */
#define     AT91_UDP_TXPKTRDY	(1 <<  4)	/* Transmit Packet Ready */
#define     AT91_UDP_FORCESTALL	(1 <<  5)	/* Force Stall */
#define     AT91_UDP_RX_DATA_BK1 (1 <<  6)	/* Receive Data Bank 1 */
#define     AT91_UDP_DIR	(1 <<  7)	/* Transfer Direction */
#define     AT91_UDP_EPTYPE	(7 <<  8)	/* Endpoint Type */
#define		AT91_UDP_EPTYPE_CTRL		(0 <<  8)
#define		AT91_UDP_EPTYPE_ISO_OUT		(1 <<  8)
#define		AT91_UDP_EPTYPE_BULK_OUT	(2 <<  8)
#define		AT91_UDP_EPTYPE_INT_OUT		(3 <<  8)
#define		AT91_UDP_EPTYPE_ISO_IN		(5 <<  8)
#define		AT91_UDP_EPTYPE_BULK_IN		(6 <<  8)
#define		AT91_UDP_EPTYPE_INT_IN		(7 <<  8)
#define     AT91_UDP_DTGLE	(1 << 11)	/* Data Toggle */
#define     AT91_UDP_EPEDS	(1 << 15)	/* Endpoint Enable/Disable */
#define     AT91_UDP_RXBYTECNT	(0x7ff << 16)	/* Number of bytes in FIFO */

#define AT91_UDP_FDR(n)		(0x50+((n)*4))	/* Endpoint FIFO Data Registers 0-7 */

#define AT91_UDP_TXVC		0x74		/* Transceiver Control Register */
#define     AT91_UDP_TXVC_TXVDIS (1 << 8)	/* Transceiver Disable */
#define     AT91_UDP_TXVC_PUON   (1 << 9)	/* PullUp On [AT91SAM9260 only] */

/*-------------------------------------------------------------------------*/

/*
 * controller driver data structures
 */

#define	NUM_ENDPOINTS	6

/*
 * hardware won't disable bus reset, or resume while the controller
 * is suspended ... watching suspend helps keep the logic symmetric.
 */
#define	MINIMUS_INTERRUPTUS \
	(AT91_UDP_ENDBUSRES | AT91_UDP_RXRSM | AT91_UDP_RXSUSP)

struct at91_ep {
	struct usb_ep			ep;
	struct list_head		queue;
	struct at91_udc			*udc;
	void __iomem			*creg;

	unsigned			maxpacket:16;
	u8				int_mask;
	unsigned			is_pingpong:1;

	unsigned			stopped:1;
	unsigned			is_in:1;
	unsigned			is_iso:1;
	unsigned			fifo_bank:1;
};

struct at91_udc_caps {
	int (*init)(struct at91_udc *udc);
	void (*pullup)(struct at91_udc *udc, int is_on);
};

struct at91_udc_data {
	int	vbus_pin;		/* high == host powering us */
	u8	vbus_active_low;	/* vbus polarity */
	u8	vbus_polled;		/* Use polling, not interrupt */
	int	pullup_pin;		/* active == D+ pulled up */
	u8	pullup_active_low;	/* true == pullup_pin is active low */
};

/*
 * driver is non-SMP, and just blocks IRQs whenever it needs
 * access protection for chip registers or driver state
 */
struct at91_udc {
	struct usb_gadget		gadget;
	struct at91_ep			ep[NUM_ENDPOINTS];
	struct usb_gadget_driver	*driver;
	const struct at91_udc_caps	*caps;
	unsigned			vbus:1;
	unsigned			enabled:1;
	unsigned			clocked:1;
	unsigned			suspended:1;
	unsigned			req_pending:1;
	unsigned			wait_for_addr_ack:1;
	unsigned			wait_for_config_ack:1;
	unsigned			active_suspend:1;
	u8				addr;
	struct at91_udc_data		board;
	struct clk			*iclk, *fclk;
	struct platform_device		*pdev;
	struct proc_dir_entry		*pde;
	void __iomem			*udp_baseaddr;
	int				udp_irq;
	spinlock_t			lock;
	struct timer_list		vbus_timer;
	struct work_struct		vbus_timer_work;
	struct regmap			*matrix;
};

static inline struct at91_udc *to_udc(struct usb_gadget *g)
{
	return container_of(g, struct at91_udc, gadget);
}

struct at91_request {
	struct usb_request		req;
	struct list_head		queue;
};

/*-------------------------------------------------------------------------*/

#ifdef VERBOSE_DEBUG
#    define VDBG		DBG
#else
#    define VDBG(stuff...)	do{}while(0)
#endif

#ifdef PACKET_TRACE
#    define PACKET		VDBG
#else
#    define PACKET(stuff...)	do{}while(0)
#endif

#define ERR(stuff...)		pr_err("udc: " stuff)
#define WARNING(stuff...)	pr_warn("udc: " stuff)
#define INFO(stuff...)		pr_info("udc: " stuff)
#define DBG(stuff...)		pr_debug("udc: " stuff)

#endif

