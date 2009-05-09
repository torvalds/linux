/*
 *	Copyright (C) 2005 Mike Lee(eemike@gmail.com)
 *
 *	This udc driver is now under testing and code is based on pxa2xx_udc.h
 *	Please use it with your own risk!
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 */

#ifndef __LINUX_USB_GADGET_IMX_H
#define __LINUX_USB_GADGET_IMX_H

#include <linux/types.h>

/* Helper macros */
#define EP_NO(ep)	((ep->bEndpointAddress) & ~USB_DIR_IN) /* IN:1, OUT:0 */
#define EP_DIR(ep)	((ep->bEndpointAddress) & USB_DIR_IN ? 1 : 0)
#define irq_to_ep(irq)	(((irq) >= USBD_INT0) || ((irq) <= USBD_INT6) \
		? ((irq) - USBD_INT0) : (USBD_INT6)) /*should not happen*/
#define ep_to_irq(ep)	(EP_NO((ep)) + USBD_INT0)
#define IMX_USB_NB_EP	6

/* Driver structures */
struct imx_request {
	struct usb_request			req;
	struct list_head			queue;
	unsigned int				in_use;
};

enum ep0_state {
	EP0_IDLE,
	EP0_IN_DATA_PHASE,
	EP0_OUT_DATA_PHASE,
	EP0_CONFIG,
	EP0_STALL,
};

struct imx_ep_struct {
	struct usb_ep				ep;
	struct imx_udc_struct			*imx_usb;
	struct list_head			queue;
	unsigned char				stopped;
	unsigned char				fifosize;
	unsigned char				bEndpointAddress;
	unsigned char				bmAttributes;
};

struct imx_udc_struct {
	struct usb_gadget			gadget;
	struct usb_gadget_driver		*driver;
	struct device				*dev;
	struct imx_ep_struct			imx_ep[IMX_USB_NB_EP];
	struct clk				*clk;
	struct timer_list			timer;
	enum ep0_state				ep0state;
	struct resource				*res;
	void __iomem				*base;
	unsigned char				set_config;
	int					cfg,
						intf,
						alt,
						usbd_int[7];
};

/* USB registers */
#define  USB_FRAME		(0x00)	/* USB frame */
#define  USB_SPEC		(0x04)	/* USB Spec */
#define  USB_STAT		(0x08)	/* USB Status */
#define  USB_CTRL		(0x0C)	/* USB Control */
#define  USB_DADR		(0x10)	/* USB Desc RAM addr */
#define  USB_DDAT		(0x14)	/* USB Desc RAM/EP buffer data */
#define  USB_INTR		(0x18)	/* USB interrupt */
#define  USB_MASK		(0x1C)	/* USB Mask */
#define  USB_ENAB		(0x24)	/* USB Enable */
#define  USB_EP_STAT(x)		(0x30 + (x*0x30)) /* USB status/control */
#define  USB_EP_INTR(x)		(0x34 + (x*0x30)) /* USB interrupt */
#define  USB_EP_MASK(x)		(0x38 + (x*0x30)) /* USB mask */
#define  USB_EP_FDAT(x)		(0x3C + (x*0x30)) /* USB FIFO data */
#define  USB_EP_FDAT0(x)	(0x3C + (x*0x30)) /* USB FIFO data */
#define  USB_EP_FDAT1(x)	(0x3D + (x*0x30)) /* USB FIFO data */
#define  USB_EP_FDAT2(x)	(0x3E + (x*0x30)) /* USB FIFO data */
#define  USB_EP_FDAT3(x)	(0x3F + (x*0x30)) /* USB FIFO data */
#define  USB_EP_FSTAT(x)	(0x40 + (x*0x30)) /* USB FIFO status */
#define  USB_EP_FCTRL(x)	(0x44 + (x*0x30)) /* USB FIFO control */
#define  USB_EP_LRFP(x)		(0x48 + (x*0x30)) /* USB last rd f. pointer */
#define  USB_EP_LWFP(x)		(0x4C + (x*0x30)) /* USB last wr f. pointer */
#define  USB_EP_FALRM(x)	(0x50 + (x*0x30)) /* USB FIFO alarm */
#define  USB_EP_FRDP(x)		(0x54 + (x*0x30)) /* USB FIFO read pointer */
#define  USB_EP_FWRP(x)		(0x58 + (x*0x30)) /* USB FIFO write pointer */
/* USB Control Register Bit Fields.*/
#define CTRL_CMDOVER		(1<<6)	/* UDC status */
#define CTRL_CMDERROR		(1<<5)	/* UDC status */
#define CTRL_FE_ENA		(1<<3)	/* Enable Font End logic */
#define CTRL_UDC_RST		(1<<2)	/* UDC reset */
#define CTRL_AFE_ENA		(1<<1)	/* Analog Font end enable */
#define CTRL_RESUME		(1<<0)	/* UDC resume */
/* USB Status Register Bit Fields.*/
#define STAT_RST		(1<<8)
#define STAT_SUSP		(1<<7)
#define STAT_CFG		(3<<5)
#define STAT_INTF		(3<<3)
#define STAT_ALTSET		(7<<0)
/* USB Interrupt Status/Mask Registers Bit fields */
#define INTR_WAKEUP		(1<<31)	/* Wake up Interrupt */
#define INTR_MSOF		(1<<7)	/* Missed Start of Frame */
#define INTR_SOF		(1<<6)	/* Start of Frame */
#define INTR_RESET_STOP		(1<<5)	/* Reset Signaling stop */
#define INTR_RESET_START	(1<<4)	/* Reset Signaling start */
#define INTR_RESUME		(1<<3)	/* Suspend to resume */
#define INTR_SUSPEND		(1<<2)	/* Active to suspend */
#define INTR_FRAME_MATCH	(1<<1)	/* Frame matched */
#define INTR_CFG_CHG		(1<<0)	/* Configuration change occurred */
/* USB Enable Register Bit Fields.*/
#define ENAB_RST		(1<<31)	/* Reset USB modules */
#define ENAB_ENAB		(1<<30)	/* Enable USB modules*/
#define ENAB_SUSPEND		(1<<29)	/* Suspend USB modules */
#define ENAB_ENDIAN		(1<<28)	/* Endian of USB modules */
#define ENAB_PWRMD		(1<<0)	/* Power mode of USB modules */
/* USB Descriptor Ram Address Register bit fields */
#define DADR_CFG		(1<<31)	/* Configuration */
#define DADR_BSY		(1<<30)	/* Busy status */
#define DADR_DADR		(0x1FF)	/* Descriptor Ram Address */
/* USB Descriptor RAM/Endpoint Buffer Data Register bit fields */
#define DDAT_DDAT		(0xFF)	/* Descriptor Endpoint Buffer */
/* USB Endpoint Status Register bit fields */
#define EPSTAT_BCOUNT		(0x7F<<16)	/* Endpoint FIFO byte count */
#define EPSTAT_SIP		(1<<8)	/* Endpoint setup in progress */
#define EPSTAT_DIR		(1<<7)	/* Endpoint transfer direction */
#define EPSTAT_MAX		(3<<5)	/* Endpoint Max packet size */
#define EPSTAT_TYP		(3<<3)	/* Endpoint type */
#define EPSTAT_ZLPS		(1<<2)	/* Send zero length packet */
#define EPSTAT_FLUSH		(1<<1)	/* Endpoint FIFO Flush */
#define EPSTAT_STALL		(1<<0)	/* Force stall */
/* USB Endpoint FIFO Status Register bit fields */
#define FSTAT_FRAME_STAT	(0xF<<24)	/* Frame status bit [0-3] */
#define FSTAT_ERR		(1<<22)	/* FIFO error */
#define FSTAT_UF		(1<<21)	/* FIFO underflow */
#define FSTAT_OF		(1<<20)	/* FIFO overflow */
#define FSTAT_FR		(1<<19)	/* FIFO frame ready */
#define FSTAT_FULL		(1<<18)	/* FIFO full */
#define FSTAT_ALRM		(1<<17)	/* FIFO alarm */
#define FSTAT_EMPTY		(1<<16)	/* FIFO empty */
/* USB Endpoint FIFO Control Register bit fields */
#define FCTRL_WFR		(1<<29)	/* Write frame end */
/* USB Endpoint Interrupt Status Regsiter bit fields */
#define EPINTR_FIFO_FULL	(1<<8)	/* fifo full */
#define EPINTR_FIFO_EMPTY	(1<<7)	/* fifo empty */
#define EPINTR_FIFO_ERROR	(1<<6)	/* fifo error */
#define EPINTR_FIFO_HIGH	(1<<5)	/* fifo high */
#define EPINTR_FIFO_LOW		(1<<4)	/* fifo low */
#define EPINTR_MDEVREQ		(1<<3)	/* multi Device request */
#define EPINTR_EOT		(1<<2)	/* fifo end of transfer */
#define EPINTR_DEVREQ		(1<<1)	/* Device request */
#define EPINTR_EOF		(1<<0)	/* fifo end of frame */

/* Debug macros */
#ifdef DEBUG

/* #define DEBUG_REQ */
/* #define DEBUG_TRX */
/* #define DEBUG_INIT */
/* #define DEBUG_EP0 */
/* #define DEBUG_EPX */
/* #define DEBUG_IRQ */
/* #define DEBUG_EPIRQ */
/* #define DEBUG_DUMP */
/* #define DEBUG_ERR */

#ifdef DEBUG_REQ
	#define D_REQ(dev, args...)	dev_dbg(dev, ## args)
#else
	#define D_REQ(dev, args...)	do {} while (0)
#endif /* DEBUG_REQ */

#ifdef DEBUG_TRX
	#define D_TRX(dev, args...)	dev_dbg(dev, ## args)
#else
	#define D_TRX(dev, args...)	do {} while (0)
#endif /* DEBUG_TRX */

#ifdef DEBUG_INIT
	#define D_INI(dev, args...)	dev_dbg(dev, ## args)
#else
	#define D_INI(dev, args...)	do {} while (0)
#endif /* DEBUG_INIT */

#ifdef DEBUG_EP0
	static const char *state_name[] = {
		"EP0_IDLE",
		"EP0_IN_DATA_PHASE",
		"EP0_OUT_DATA_PHASE",
		"EP0_CONFIG",
		"EP0_STALL"
	};
	#define D_EP0(dev, args...)	dev_dbg(dev, ## args)
#else
	#define D_EP0(dev, args...)	do {} while (0)
#endif /* DEBUG_EP0 */

#ifdef DEBUG_EPX
	#define D_EPX(dev, args...)	dev_dbg(dev, ## args)
#else
	#define D_EPX(dev, args...)	do {} while (0)
#endif /* DEBUG_EP0 */

#ifdef DEBUG_IRQ
	static void dump_intr(const char *label, int irqreg, struct device *dev)
	{
		dev_dbg(dev, "<%s> USB_INTR=[%s%s%s%s%s%s%s%s%s]\n", label,
			(irqreg & INTR_WAKEUP) ? " wake" : "",
			(irqreg & INTR_MSOF) ? " msof" : "",
			(irqreg & INTR_SOF) ? " sof" : "",
			(irqreg & INTR_RESUME) ? " resume" : "",
			(irqreg & INTR_SUSPEND) ? " suspend" : "",
			(irqreg & INTR_RESET_STOP) ? " noreset" : "",
			(irqreg & INTR_RESET_START) ? " reset" : "",
			(irqreg & INTR_FRAME_MATCH) ? " fmatch" : "",
			(irqreg & INTR_CFG_CHG) ? " config" : "");
	}
#else
	#define dump_intr(x, y, z)		do {} while (0)
#endif /* DEBUG_IRQ */

#ifdef DEBUG_EPIRQ
	static void dump_ep_intr(const char *label, int nr, int irqreg,
							struct device *dev)
	{
		dev_dbg(dev, "<%s> EP%d_INTR=[%s%s%s%s%s%s%s%s%s]\n", label, nr,
			(irqreg & EPINTR_FIFO_FULL) ? " full" : "",
			(irqreg & EPINTR_FIFO_EMPTY) ? " fempty" : "",
			(irqreg & EPINTR_FIFO_ERROR) ? " ferr" : "",
			(irqreg & EPINTR_FIFO_HIGH) ? " fhigh" : "",
			(irqreg & EPINTR_FIFO_LOW) ? " flow" : "",
			(irqreg & EPINTR_MDEVREQ) ? " mreq" : "",
			(irqreg & EPINTR_EOF) ? " eof" : "",
			(irqreg & EPINTR_DEVREQ) ? " devreq" : "",
			(irqreg & EPINTR_EOT) ? " eot" : "");
	}
#else
	#define dump_ep_intr(x, y, z, i)	do {} while (0)
#endif /* DEBUG_IRQ */

#ifdef DEBUG_DUMP
	static void dump_usb_stat(const char *label,
						struct imx_udc_struct *imx_usb)
	{
		int temp = __raw_readl(imx_usb->base + USB_STAT);

		dev_dbg(imx_usb->dev,
			"<%s> USB_STAT=[%s%s CFG=%d, INTF=%d, ALTR=%d]\n", label,
			(temp & STAT_RST) ? " reset" : "",
			(temp & STAT_SUSP) ? " suspend" : "",
			(temp & STAT_CFG) >> 5,
			(temp & STAT_INTF) >> 3,
			(temp & STAT_ALTSET));
	}

	static void dump_ep_stat(const char *label,
						struct imx_ep_struct *imx_ep)
	{
		int temp = __raw_readl(imx_ep->imx_usb->base
						+ USB_EP_INTR(EP_NO(imx_ep)));

		dev_dbg(imx_ep->imx_usb->dev,
			"<%s> EP%d_INTR=[%s%s%s%s%s%s%s%s%s]\n",
			label, EP_NO(imx_ep),
			(temp & EPINTR_FIFO_FULL) ? " full" : "",
			(temp & EPINTR_FIFO_EMPTY) ? " fempty" : "",
			(temp & EPINTR_FIFO_ERROR) ? " ferr" : "",
			(temp & EPINTR_FIFO_HIGH) ? " fhigh" : "",
			(temp & EPINTR_FIFO_LOW) ? " flow" : "",
			(temp & EPINTR_MDEVREQ) ? " mreq" : "",
			(temp & EPINTR_EOF) ? " eof" : "",
			(temp & EPINTR_DEVREQ) ? " devreq" : "",
			(temp & EPINTR_EOT) ? " eot" : "");

		temp = __raw_readl(imx_ep->imx_usb->base
						+ USB_EP_STAT(EP_NO(imx_ep)));

		dev_dbg(imx_ep->imx_usb->dev,
			"<%s> EP%d_STAT=[%s%s bcount=%d]\n",
			label, EP_NO(imx_ep),
			(temp & EPSTAT_SIP) ? " sip" : "",
			(temp & EPSTAT_STALL) ? " stall" : "",
			(temp & EPSTAT_BCOUNT) >> 16);

		temp = __raw_readl(imx_ep->imx_usb->base
						+ USB_EP_FSTAT(EP_NO(imx_ep)));

		dev_dbg(imx_ep->imx_usb->dev,
			"<%s> EP%d_FSTAT=[%s%s%s%s%s%s%s]\n",
			label, EP_NO(imx_ep),
			(temp & FSTAT_ERR) ? " ferr" : "",
			(temp & FSTAT_UF) ? " funder" : "",
			(temp & FSTAT_OF) ? " fover" : "",
			(temp & FSTAT_FR) ? " fready" : "",
			(temp & FSTAT_FULL) ? " ffull" : "",
			(temp & FSTAT_ALRM) ? " falarm" : "",
			(temp & FSTAT_EMPTY) ? " fempty" : "");
	}

	static void dump_req(const char *label, struct imx_ep_struct *imx_ep,
							struct usb_request *req)
	{
		int i;

		if (!req || !req->buf) {
			dev_dbg(imx_ep->imx_usb->dev,
					"<%s> req or req buf is free\n", label);
			return;
		}

		if ((!EP_NO(imx_ep) && imx_ep->imx_usb->ep0state
			== EP0_IN_DATA_PHASE)
			|| (EP_NO(imx_ep) && EP_DIR(imx_ep))) {

			dev_dbg(imx_ep->imx_usb->dev,
						"<%s> request dump <", label);
			for (i = 0; i < req->length; i++)
				printk("%02x-", *((u8 *)req->buf + i));
			printk(">\n");
		}
	}

#else
	#define dump_ep_stat(x, y)		do {} while (0)
	#define dump_usb_stat(x, y)		do {} while (0)
	#define dump_req(x, y, z)		do {} while (0)
#endif /* DEBUG_DUMP */

#ifdef DEBUG_ERR
	#define D_ERR(dev, args...)	dev_dbg(dev, ## args)
#else
	#define D_ERR(dev, args...)	do {} while (0)
#endif

#else
	#define D_REQ(dev, args...)		do {} while (0)
	#define D_TRX(dev, args...)		do {} while (0)
	#define D_INI(dev, args...)		do {} while (0)
	#define D_EP0(dev, args...)		do {} while (0)
	#define D_EPX(dev, args...)		do {} while (0)
	#define dump_ep_intr(x, y, z, i)	do {} while (0)
	#define dump_intr(x, y, z)		do {} while (0)
	#define dump_ep_stat(x, y)		do {} while (0)
	#define dump_usb_stat(x, y)		do {} while (0)
	#define dump_req(x, y, z)		do {} while (0)
	#define D_ERR(dev, args...)		do {} while (0)
#endif /* DEBUG */

#endif /* __LINUX_USB_GADGET_IMX_H */
