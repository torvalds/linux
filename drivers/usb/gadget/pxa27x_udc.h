/*
 * linux/drivers/usb/gadget/pxa27x_udc.h
 * Intel PXA27x on-chip full speed USB device controller
 *
 * Inspired by original driver by Frank Becker, David Brownell, and others.
 * Copyright (C) 2008 Robert Jarzmik
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307	 USA
 */

#ifndef __LINUX_USB_GADGET_PXA27X_H
#define __LINUX_USB_GADGET_PXA27X_H

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/usb/otg.h>

/*
 * Register definitions
 */
/* Offsets */
#define UDCCR		0x0000		/* UDC Control Register */
#define UDCICR0		0x0004		/* UDC Interrupt Control Register0 */
#define UDCICR1		0x0008		/* UDC Interrupt Control Register1 */
#define UDCISR0		0x000C		/* UDC Interrupt Status Register 0 */
#define UDCISR1		0x0010		/* UDC Interrupt Status Register 1 */
#define UDCFNR		0x0014		/* UDC Frame Number Register */
#define UDCOTGICR	0x0018		/* UDC On-The-Go interrupt control */
#define UP2OCR		0x0020		/* USB Port 2 Output Control register */
#define UP3OCR		0x0024		/* USB Port 3 Output Control register */
#define UDCCSRn(x)	(0x0100 + ((x)<<2)) /* UDC Control/Status register */
#define UDCBCRn(x)	(0x0200 + ((x)<<2)) /* UDC Byte Count Register */
#define UDCDRn(x)	(0x0300 + ((x)<<2)) /* UDC Data Register  */
#define UDCCRn(x)	(0x0400 + ((x)<<2)) /* UDC Control Register */

#define UDCCR_OEN	(1 << 31)	/* On-the-Go Enable */
#define UDCCR_AALTHNP	(1 << 30)	/* A-device Alternate Host Negotiation
					   Protocol Port Support */
#define UDCCR_AHNP	(1 << 29)	/* A-device Host Negotiation Protocol
					   Support */
#define UDCCR_BHNP	(1 << 28)	/* B-device Host Negotiation Protocol
					   Enable */
#define UDCCR_DWRE	(1 << 16)	/* Device Remote Wake-up Enable */
#define UDCCR_ACN	(0x03 << 11)	/* Active UDC configuration Number */
#define UDCCR_ACN_S	11
#define UDCCR_AIN	(0x07 << 8)	/* Active UDC interface Number */
#define UDCCR_AIN_S	8
#define UDCCR_AAISN	(0x07 << 5)	/* Active UDC Alternate Interface
					   Setting Number */
#define UDCCR_AAISN_S	5
#define UDCCR_SMAC	(1 << 4)	/* Switch Endpoint Memory to Active
					   Configuration */
#define UDCCR_EMCE	(1 << 3)	/* Endpoint Memory Configuration
					   Error */
#define UDCCR_UDR	(1 << 2)	/* UDC Resume */
#define UDCCR_UDA	(1 << 1)	/* UDC Active */
#define UDCCR_UDE	(1 << 0)	/* UDC Enable */

#define UDCICR_INT(n, intr) (((intr) & 0x03) << (((n) & 0x0F) * 2))
#define UDCICR1_IECC	(1 << 31)	/* IntEn - Configuration Change */
#define UDCICR1_IESOF	(1 << 30)	/* IntEn - Start of Frame */
#define UDCICR1_IERU	(1 << 29)	/* IntEn - Resume */
#define UDCICR1_IESU	(1 << 28)	/* IntEn - Suspend */
#define UDCICR1_IERS	(1 << 27)	/* IntEn - Reset */
#define UDCICR_FIFOERR	(1 << 1)	/* FIFO Error interrupt for EP */
#define UDCICR_PKTCOMPL	(1 << 0)	/* Packet Complete interrupt for EP */
#define UDCICR_INT_MASK	(UDCICR_FIFOERR | UDCICR_PKTCOMPL)

#define UDCISR_INT(n, intr) (((intr) & 0x03) << (((n) & 0x0F) * 2))
#define UDCISR1_IRCC	(1 << 31)	/* IntReq - Configuration Change */
#define UDCISR1_IRSOF	(1 << 30)	/* IntReq - Start of Frame */
#define UDCISR1_IRRU	(1 << 29)	/* IntReq - Resume */
#define UDCISR1_IRSU	(1 << 28)	/* IntReq - Suspend */
#define UDCISR1_IRRS	(1 << 27)	/* IntReq - Reset */
#define UDCISR_INT_MASK	(UDCICR_FIFOERR | UDCICR_PKTCOMPL)

#define UDCOTGICR_IESF	(1 << 24)	/* OTG SET_FEATURE command recvd */
#define UDCOTGICR_IEXR	(1 << 17)	/* Extra Transciever Interrupt
					   Rising Edge Interrupt Enable */
#define UDCOTGICR_IEXF	(1 << 16)	/* Extra Transciever Interrupt
					   Falling Edge Interrupt Enable */
#define UDCOTGICR_IEVV40R (1 << 9)	/* OTG Vbus Valid 4.0V Rising Edge
					   Interrupt Enable */
#define UDCOTGICR_IEVV40F (1 << 8)	/* OTG Vbus Valid 4.0V Falling Edge
					   Interrupt Enable */
#define UDCOTGICR_IEVV44R (1 << 7)	/* OTG Vbus Valid 4.4V Rising Edge
					   Interrupt Enable */
#define UDCOTGICR_IEVV44F (1 << 6)	/* OTG Vbus Valid 4.4V Falling Edge
					   Interrupt Enable */
#define UDCOTGICR_IESVR	(1 << 5)	/* OTG Session Valid Rising Edge
					   Interrupt Enable */
#define UDCOTGICR_IESVF	(1 << 4)	/* OTG Session Valid Falling Edge
					   Interrupt Enable */
#define UDCOTGICR_IESDR	(1 << 3)	/* OTG A-Device SRP Detect Rising
					   Edge Interrupt Enable */
#define UDCOTGICR_IESDF	(1 << 2)	/* OTG A-Device SRP Detect Falling
					   Edge Interrupt Enable */
#define UDCOTGICR_IEIDR	(1 << 1)	/* OTG ID Change Rising Edge
					   Interrupt Enable */
#define UDCOTGICR_IEIDF	(1 << 0)	/* OTG ID Change Falling Edge
					   Interrupt Enable */

/* Host Port 2 field bits */
#define UP2OCR_CPVEN	(1 << 0)	/* Charge Pump Vbus Enable */
#define UP2OCR_CPVPE	(1 << 1)	/* Charge Pump Vbus Pulse Enable */
					/* Transceiver enablers */
#define UP2OCR_DPPDE	(1 << 2)	/*   D+ Pull Down Enable */
#define UP2OCR_DMPDE	(1 << 3)	/*   D- Pull Down Enable */
#define UP2OCR_DPPUE	(1 << 4)	/*   D+ Pull Up Enable */
#define UP2OCR_DMPUE	(1 << 5)	/*   D- Pull Up Enable */
#define UP2OCR_DPPUBE	(1 << 6)	/*   D+ Pull Up Bypass Enable */
#define UP2OCR_DMPUBE	(1 << 7)	/*   D- Pull Up Bypass Enable */
#define UP2OCR_EXSP	(1 << 8)	/* External Transceiver Speed Control */
#define UP2OCR_EXSUS	(1 << 9)	/* External Transceiver Speed Enable */
#define UP2OCR_IDON	(1 << 10)	/* OTG ID Read Enable */
#define UP2OCR_HXS	(1 << 16)	/* Transceiver Output Select */
#define UP2OCR_HXOE	(1 << 17)	/* Transceiver Output Enable */
#define UP2OCR_SEOS	(1 << 24)	/* Single-Ended Output Select */

#define UDCCSR0_ACM	(1 << 9)	/* Ack Control Mode */
#define UDCCSR0_AREN	(1 << 8)	/* Ack Response Enable */
#define UDCCSR0_SA	(1 << 7)	/* Setup Active */
#define UDCCSR0_RNE	(1 << 6)	/* Receive FIFO Not Empty */
#define UDCCSR0_FST	(1 << 5)	/* Force Stall */
#define UDCCSR0_SST	(1 << 4)	/* Sent Stall */
#define UDCCSR0_DME	(1 << 3)	/* DMA Enable */
#define UDCCSR0_FTF	(1 << 2)	/* Flush Transmit FIFO */
#define UDCCSR0_IPR	(1 << 1)	/* IN Packet Ready */
#define UDCCSR0_OPC	(1 << 0)	/* OUT Packet Complete */

#define UDCCSR_DPE	(1 << 9)	/* Data Packet Error */
#define UDCCSR_FEF	(1 << 8)	/* Flush Endpoint FIFO */
#define UDCCSR_SP	(1 << 7)	/* Short Packet Control/Status */
#define UDCCSR_BNE	(1 << 6)	/* Buffer Not Empty (IN endpoints) */
#define UDCCSR_BNF	(1 << 6)	/* Buffer Not Full (OUT endpoints) */
#define UDCCSR_FST	(1 << 5)	/* Force STALL */
#define UDCCSR_SST	(1 << 4)	/* Sent STALL */
#define UDCCSR_DME	(1 << 3)	/* DMA Enable */
#define UDCCSR_TRN	(1 << 2)	/* Tx/Rx NAK */
#define UDCCSR_PC	(1 << 1)	/* Packet Complete */
#define UDCCSR_FS	(1 << 0)	/* FIFO needs service */

#define UDCCONR_CN	(0x03 << 25)	/* Configuration Number */
#define UDCCONR_CN_S	25
#define UDCCONR_IN	(0x07 << 22)	/* Interface Number */
#define UDCCONR_IN_S	22
#define UDCCONR_AISN	(0x07 << 19)	/* Alternate Interface Number */
#define UDCCONR_AISN_S	19
#define UDCCONR_EN	(0x0f << 15)	/* Endpoint Number */
#define UDCCONR_EN_S	15
#define UDCCONR_ET	(0x03 << 13)	/* Endpoint Type: */
#define UDCCONR_ET_S	13
#define UDCCONR_ET_INT	(0x03 << 13)	/*   Interrupt */
#define UDCCONR_ET_BULK	(0x02 << 13)	/*   Bulk */
#define UDCCONR_ET_ISO	(0x01 << 13)	/*   Isochronous */
#define UDCCONR_ET_NU	(0x00 << 13)	/*   Not used */
#define UDCCONR_ED	(1 << 12)	/* Endpoint Direction */
#define UDCCONR_MPS	(0x3ff << 2)	/* Maximum Packet Size */
#define UDCCONR_MPS_S	2
#define UDCCONR_DE	(1 << 1)	/* Double Buffering Enable */
#define UDCCONR_EE	(1 << 0)	/* Endpoint Enable */

#define UDCCR_MASK_BITS (UDCCR_OEN | UDCCR_SMAC | UDCCR_UDR | UDCCR_UDE)
#define UDCCSR_WR_MASK	(UDCCSR_DME | UDCCSR_FST)
#define UDC_FNR_MASK	(0x7ff)
#define UDC_BCR_MASK	(0x3ff)

/*
 * UDCCR = UDC Endpoint Configuration Registers
 * UDCCSR = UDC Control/Status Register for this EP
 * UDCBCR = UDC Byte Count Remaining (contents of OUT fifo)
 * UDCDR = UDC Endpoint Data Register (the fifo)
 */
#define ofs_UDCCR(ep)	(UDCCRn(ep->idx))
#define ofs_UDCCSR(ep)	(UDCCSRn(ep->idx))
#define ofs_UDCBCR(ep)	(UDCBCRn(ep->idx))
#define ofs_UDCDR(ep)	(UDCDRn(ep->idx))

/* Register access macros */
#define udc_ep_readl(ep, reg)	\
	__raw_readl((ep)->dev->regs + ofs_##reg(ep))
#define udc_ep_writel(ep, reg, value)	\
	__raw_writel((value), ep->dev->regs + ofs_##reg(ep))
#define udc_ep_readb(ep, reg)	\
	__raw_readb((ep)->dev->regs + ofs_##reg(ep))
#define udc_ep_writeb(ep, reg, value)	\
	__raw_writeb((value), ep->dev->regs + ofs_##reg(ep))
#define udc_readl(dev, reg)	\
	__raw_readl((dev)->regs + (reg))
#define udc_writel(udc, reg, value)	\
	__raw_writel((value), (udc)->regs + (reg))

#define UDCCSR_MASK		(UDCCSR_FST | UDCCSR_DME)
#define UDCCISR0_EP_MASK	~0
#define UDCCISR1_EP_MASK	0xffff
#define UDCCSR0_CTRL_REQ_MASK	(UDCCSR0_OPC | UDCCSR0_SA | UDCCSR0_RNE)

#define EPIDX(ep)	(ep->idx)
#define EPADDR(ep)	(ep->addr)
#define EPXFERTYPE(ep)	(ep->type)
#define EPNAME(ep)	(ep->name)
#define is_ep0(ep)	(!ep->idx)
#define EPXFERTYPE_is_ISO(ep) (EPXFERTYPE(ep) == USB_ENDPOINT_XFER_ISOC)

/*
 * Endpoint definitions
 *
 * Once enabled, pxa endpoint configuration is freezed, and cannot change
 * unless a reset happens or the udc is disabled.
 * Therefore, we must define all pxa potential endpoint definitions needed for
 * all gadget and set them up before the udc is enabled.
 *
 * As the architecture chosen is fully static, meaning the pxa endpoint
 * configurations are set up once and for all, we must provide a way to match
 * one usb endpoint (usb_ep) to several pxa endpoints. The reason is that gadget
 * layer autoconf doesn't choose the usb_ep endpoint on (config, interface, alt)
 * criteria, while the pxa architecture requires that.
 *
 * The solution is to define several pxa endpoints matching one usb_ep. Ex:
 *   - "ep1-in" matches pxa endpoint EPA (which is an IN ep at addr 1, when
 *     the udc talks on (config=3, interface=0, alt=0)
 *   - "ep1-in" matches pxa endpoint EPB (which is an IN ep at addr 1, when
 *     the udc talks on (config=3, interface=0, alt=1)
 *   - "ep1-in" matches pxa endpoint EPC (which is an IN ep at addr 1, when
 *     the udc talks on (config=2, interface=0, alt=0)
 *
 * We'll define the pxa endpoint by its index (EPA => idx=1, EPB => idx=2, ...)
 */

/*
 * Endpoint definition helpers
 */
#define USB_EP_DEF(addr, bname, dir, type, maxpkt) \
{ .usb_ep = { .name = bname, .ops = &pxa_ep_ops, .maxpacket = maxpkt, }, \
  .desc = {	.bEndpointAddress = addr | (dir ? USB_DIR_IN : 0), \
		.bmAttributes = type, \
		.wMaxPacketSize = maxpkt, }, \
  .dev = &memory \
}
#define USB_EP_BULK(addr, bname, dir) \
  USB_EP_DEF(addr, bname, dir, USB_ENDPOINT_XFER_BULK, BULK_FIFO_SIZE)
#define USB_EP_ISO(addr, bname, dir) \
  USB_EP_DEF(addr, bname, dir, USB_ENDPOINT_XFER_ISOC, ISO_FIFO_SIZE)
#define USB_EP_INT(addr, bname, dir) \
  USB_EP_DEF(addr, bname, dir, USB_ENDPOINT_XFER_INT, INT_FIFO_SIZE)
#define USB_EP_IN_BULK(n)	USB_EP_BULK(n, "ep" #n "in-bulk", 1)
#define USB_EP_OUT_BULK(n)	USB_EP_BULK(n, "ep" #n "out-bulk", 0)
#define USB_EP_IN_ISO(n)	USB_EP_ISO(n,  "ep" #n "in-iso", 1)
#define USB_EP_OUT_ISO(n)	USB_EP_ISO(n,  "ep" #n "out-iso", 0)
#define USB_EP_IN_INT(n)	USB_EP_INT(n,  "ep" #n "in-int", 1)
#define USB_EP_CTRL		USB_EP_DEF(0,  "ep0", 0, 0, EP0_FIFO_SIZE)

#define PXA_EP_DEF(_idx, _addr, dir, _type, maxpkt, _config, iface, altset) \
{ \
	.dev = &memory, \
	.name = "ep" #_idx, \
	.idx = _idx, .enabled = 0, \
	.dir_in = dir, .addr = _addr, \
	.config = _config, .interface = iface, .alternate = altset, \
	.type = _type, .fifo_size = maxpkt, \
}
#define PXA_EP_BULK(_idx, addr, dir, config, iface, alt) \
  PXA_EP_DEF(_idx, addr, dir, USB_ENDPOINT_XFER_BULK, BULK_FIFO_SIZE, \
		config, iface, alt)
#define PXA_EP_ISO(_idx, addr, dir, config, iface, alt) \
  PXA_EP_DEF(_idx, addr, dir, USB_ENDPOINT_XFER_ISOC, ISO_FIFO_SIZE, \
		config, iface, alt)
#define PXA_EP_INT(_idx, addr, dir, config, iface, alt) \
  PXA_EP_DEF(_idx, addr, dir, USB_ENDPOINT_XFER_INT, INT_FIFO_SIZE, \
		config, iface, alt)
#define PXA_EP_IN_BULK(i, adr, c, f, a)		PXA_EP_BULK(i, adr, 1, c, f, a)
#define PXA_EP_OUT_BULK(i, adr, c, f, a)	PXA_EP_BULK(i, adr, 0, c, f, a)
#define PXA_EP_IN_ISO(i, adr, c, f, a)		PXA_EP_ISO(i, adr, 1, c, f, a)
#define PXA_EP_OUT_ISO(i, adr, c, f, a)		PXA_EP_ISO(i, adr, 0, c, f, a)
#define PXA_EP_IN_INT(i, adr, c, f, a)		PXA_EP_INT(i, adr, 1, c, f, a)
#define PXA_EP_CTRL	PXA_EP_DEF(0, 0, 0, 0, EP0_FIFO_SIZE, 0, 0, 0)

struct pxa27x_udc;

struct stats {
	unsigned long in_ops;
	unsigned long out_ops;
	unsigned long in_bytes;
	unsigned long out_bytes;
	unsigned long irqs;
};

/**
 * struct udc_usb_ep - container of each usb_ep structure
 * @usb_ep: usb endpoint
 * @desc: usb descriptor, especially type and address
 * @dev: udc managing this endpoint
 * @pxa_ep: matching pxa_ep (cache of find_pxa_ep() call)
 */
struct udc_usb_ep {
	struct usb_ep usb_ep;
	struct usb_endpoint_descriptor desc;
	struct pxa_udc *dev;
	struct pxa_ep *pxa_ep;
};

/**
 * struct pxa_ep - pxa endpoint
 * @dev: udc device
 * @queue: requests queue
 * @lock: lock to pxa_ep data (queues and stats)
 * @enabled: true when endpoint enabled (not stopped by gadget layer)
 * @in_handle_ep: number of recursions of handle_ep() function
 * 	Prevents deadlocks or infinite recursions of types :
 *	  irq->handle_ep()->req_done()->req.complete()->pxa_ep_queue()->handle_ep()
 *      or
 *        pxa_ep_queue()->handle_ep()->req_done()->req.complete()->pxa_ep_queue()
 * @idx: endpoint index (1 => epA, 2 => epB, ..., 24 => epX)
 * @name: endpoint name (for trace/debug purpose)
 * @dir_in: 1 if IN endpoint, 0 if OUT endpoint
 * @addr: usb endpoint number
 * @config: configuration in which this endpoint is active
 * @interface: interface in which this endpoint is active
 * @alternate: altsetting in which this endpoitn is active
 * @fifo_size: max packet size in the endpoint fifo
 * @type: endpoint type (bulk, iso, int, ...)
 * @udccsr_value: save register of UDCCSR0 for suspend/resume
 * @udccr_value: save register of UDCCR for suspend/resume
 * @stats: endpoint statistics
 *
 * The *PROBLEM* is that pxa's endpoint configuration scheme is both misdesigned
 * (cares about config/interface/altsetting, thus placing needless limits on
 * device capability) and full of implementation bugs forcing it to be set up
 * for use more or less like a pxa255.
 *
 * As we define the pxa_ep statically, we must guess all needed pxa_ep for all
 * gadget which may work with this udc driver.
 */
struct pxa_ep {
	struct pxa_udc		*dev;

	struct list_head	queue;
	spinlock_t		lock;		/* Protects this structure */
						/* (queues, stats) */
	unsigned		enabled:1;
	unsigned		in_handle_ep:1;

	unsigned		idx:5;
	char			*name;

	/*
	 * Specific pxa endpoint data, needed for hardware initialization
	 */
	unsigned		dir_in:1;
	unsigned		addr:3;
	unsigned		config:2;
	unsigned		interface:3;
	unsigned		alternate:3;
	unsigned		fifo_size;
	unsigned		type;

#ifdef CONFIG_PM
	u32			udccsr_value;
	u32			udccr_value;
#endif
	struct stats		stats;
};

/**
 * struct pxa27x_request - container of each usb_request structure
 * @req: usb request
 * @udc_usb_ep: usb endpoint the request was submitted on
 * @in_use: sanity check if request already queued on an pxa_ep
 * @queue: linked list of requests, linked on pxa_ep->queue
 */
struct pxa27x_request {
	struct usb_request			req;
	struct udc_usb_ep			*udc_usb_ep;
	unsigned				in_use:1;
	struct list_head			queue;
};

enum ep0_state {
	WAIT_FOR_SETUP,
	SETUP_STAGE,
	IN_DATA_STAGE,
	OUT_DATA_STAGE,
	IN_STATUS_STAGE,
	OUT_STATUS_STAGE,
	STALL,
	WAIT_ACK_SET_CONF_INTERF
};

static char *ep0_state_name[] = {
	"WAIT_FOR_SETUP", "SETUP_STAGE", "IN_DATA_STAGE", "OUT_DATA_STAGE",
	"IN_STATUS_STAGE", "OUT_STATUS_STAGE", "STALL",
	"WAIT_ACK_SET_CONF_INTERF"
};
#define EP0_STNAME(udc) ep0_state_name[(udc)->ep0state]

#define EP0_FIFO_SIZE	16U
#define BULK_FIFO_SIZE	64U
#define ISO_FIFO_SIZE	256U
#define INT_FIFO_SIZE	16U

struct udc_stats {
	unsigned long	irqs_reset;
	unsigned long	irqs_suspend;
	unsigned long	irqs_resume;
	unsigned long	irqs_reconfig;
};

#define NR_USB_ENDPOINTS (1 + 5)	/* ep0 + ep1in-bulk + .. + ep3in-iso */
#define NR_PXA_ENDPOINTS (1 + 14)	/* ep0 + epA + epB + .. + epX */

/**
 * struct pxa_udc - udc structure
 * @regs: mapped IO space
 * @irq: udc irq
 * @clk: udc clock
 * @usb_gadget: udc gadget structure
 * @driver: bound gadget (zero, g_ether, g_file_storage, ...)
 * @dev: device
 * @mach: machine info, used to activate specific GPIO
 * @transceiver: external transceiver to handle vbus sense and D+ pullup
 * @ep0state: control endpoint state machine state
 * @stats: statistics on udc usage
 * @udc_usb_ep: array of usb endpoints offered by the gadget
 * @pxa_ep: array of pxa available endpoints
 * @enabled: UDC was enabled by a previous udc_enable()
 * @pullup_on: if pullup resistor connected to D+ pin
 * @pullup_resume: if pullup resistor should be connected to D+ pin on resume
 * @config: UDC active configuration
 * @last_interface: UDC interface of the last SET_INTERFACE host request
 * @last_alternate: UDC altsetting of the last SET_INTERFACE host request
 * @udccsr0: save of udccsr0 in case of suspend
 * @debugfs_root: root entry of debug filesystem
 * @debugfs_state: debugfs entry for "udcstate"
 * @debugfs_queues: debugfs entry for "queues"
 * @debugfs_eps: debugfs entry for "epstate"
 */
struct pxa_udc {
	void __iomem				*regs;
	int					irq;
	struct clk				*clk;

	struct usb_gadget			gadget;
	struct usb_gadget_driver		*driver;
	struct device				*dev;
	struct pxa2xx_udc_mach_info		*mach;
	struct otg_transceiver			*transceiver;

	enum ep0_state				ep0state;
	struct udc_stats			stats;

	struct udc_usb_ep			udc_usb_ep[NR_USB_ENDPOINTS];
	struct pxa_ep				pxa_ep[NR_PXA_ENDPOINTS];

	unsigned				enabled:1;
	unsigned				pullup_on:1;
	unsigned				pullup_resume:1;
	unsigned				vbus_sensed:1;
	unsigned				config:2;
	unsigned				last_interface:3;
	unsigned				last_alternate:3;

#ifdef CONFIG_PM
	unsigned				udccsr0;
#endif
#ifdef CONFIG_USB_GADGET_DEBUG_FS
	struct dentry				*debugfs_root;
	struct dentry				*debugfs_state;
	struct dentry				*debugfs_queues;
	struct dentry				*debugfs_eps;
#endif
};

static inline struct pxa_udc *to_gadget_udc(struct usb_gadget *gadget)
{
	return container_of(gadget, struct pxa_udc, gadget);
}

/*
 * Debugging/message support
 */
#define ep_dbg(ep, fmt, arg...) \
	dev_dbg(ep->dev->dev, "%s:%s: " fmt, EPNAME(ep), __func__, ## arg)
#define ep_vdbg(ep, fmt, arg...) \
	dev_vdbg(ep->dev->dev, "%s:%s: " fmt, EPNAME(ep), __func__, ## arg)
#define ep_err(ep, fmt, arg...) \
	dev_err(ep->dev->dev, "%s:%s: " fmt, EPNAME(ep), __func__, ## arg)
#define ep_info(ep, fmt, arg...) \
	dev_info(ep->dev->dev, "%s:%s: " fmt, EPNAME(ep), __func__, ## arg)
#define ep_warn(ep, fmt, arg...) \
	dev_warn(ep->dev->dev, "%s:%s:" fmt, EPNAME(ep), __func__, ## arg)

#endif /* __LINUX_USB_GADGET_PXA27X_H */
