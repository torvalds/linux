/*
 * linux/drivers/usb/gadget/s3c2410_udc.c
 *
 * Samsung S3C24xx series on-chip full speed USB device controllers
 *
 * Copyright (C) 2004-2007 Herbert Pötzl - Arnaud Patard
 *	Additional cleanups by Ben Dooks <ben-linux@fluff.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#define pr_fmt(fmt) "s3c2410_udc: " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/prefetch.h>
#include <linux/io.h>

#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <linux/usb.h>
#include <linux/usb/gadget.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/unaligned.h>
#include <mach/irqs.h>

#include <mach/hardware.h>

#include <plat/regs-udc.h>
#include <plat/udc.h>


#include "s3c2410_udc.h"

#define DRIVER_DESC	"S3C2410 USB Device Controller Gadget"
#define DRIVER_VERSION	"29 Apr 2007"
#define DRIVER_AUTHOR	"Herbert Pötzl <herbert@13thfloor.at>, " \
			"Arnaud Patard <arnaud.patard@rtp-net.org>"

static const char		gadget_name[] = "s3c2410_udc";
static const char		driver_desc[] = DRIVER_DESC;

static struct s3c2410_udc	*the_controller;
static struct clk		*udc_clock;
static struct clk		*usb_bus_clock;
static void __iomem		*base_addr;
static u64			rsrc_start;
static u64			rsrc_len;
static struct dentry		*s3c2410_udc_debugfs_root;

static inline u32 udc_read(u32 reg)
{
	return readb(base_addr + reg);
}

static inline void udc_write(u32 value, u32 reg)
{
	writeb(value, base_addr + reg);
}

static inline void udc_writeb(void __iomem *base, u32 value, u32 reg)
{
	writeb(value, base + reg);
}

static struct s3c2410_udc_mach_info *udc_info;

/*************************** DEBUG FUNCTION ***************************/
#define DEBUG_NORMAL	1
#define DEBUG_VERBOSE	2

#ifdef CONFIG_USB_S3C2410_DEBUG
#define USB_S3C2410_DEBUG_LEVEL 0

static uint32_t s3c2410_ticks = 0;

static int dprintk(int level, const char *fmt, ...)
{
	static char printk_buf[1024];
	static long prevticks;
	static int invocation;
	va_list args;
	int len;

	if (level > USB_S3C2410_DEBUG_LEVEL)
		return 0;

	if (s3c2410_ticks != prevticks) {
		prevticks = s3c2410_ticks;
		invocation = 0;
	}

	len = scnprintf(printk_buf,
			sizeof(printk_buf), "%1lu.%02d USB: ",
			prevticks, invocation++);

	va_start(args, fmt);
	len = vscnprintf(printk_buf+len,
			sizeof(printk_buf)-len, fmt, args);
	va_end(args);

	return pr_debug("%s", printk_buf);
}
#else
static int dprintk(int level, const char *fmt, ...)
{
	return 0;
}
#endif
static int s3c2410_udc_debugfs_seq_show(struct seq_file *m, void *p)
{
	u32 addr_reg, pwr_reg, ep_int_reg, usb_int_reg;
	u32 ep_int_en_reg, usb_int_en_reg, ep0_csr;
	u32 ep1_i_csr1, ep1_i_csr2, ep1_o_csr1, ep1_o_csr2;
	u32 ep2_i_csr1, ep2_i_csr2, ep2_o_csr1, ep2_o_csr2;

	addr_reg       = udc_read(S3C2410_UDC_FUNC_ADDR_REG);
	pwr_reg        = udc_read(S3C2410_UDC_PWR_REG);
	ep_int_reg     = udc_read(S3C2410_UDC_EP_INT_REG);
	usb_int_reg    = udc_read(S3C2410_UDC_USB_INT_REG);
	ep_int_en_reg  = udc_read(S3C2410_UDC_EP_INT_EN_REG);
	usb_int_en_reg = udc_read(S3C2410_UDC_USB_INT_EN_REG);
	udc_write(0, S3C2410_UDC_INDEX_REG);
	ep0_csr        = udc_read(S3C2410_UDC_IN_CSR1_REG);
	udc_write(1, S3C2410_UDC_INDEX_REG);
	ep1_i_csr1     = udc_read(S3C2410_UDC_IN_CSR1_REG);
	ep1_i_csr2     = udc_read(S3C2410_UDC_IN_CSR2_REG);
	ep1_o_csr1     = udc_read(S3C2410_UDC_IN_CSR1_REG);
	ep1_o_csr2     = udc_read(S3C2410_UDC_IN_CSR2_REG);
	udc_write(2, S3C2410_UDC_INDEX_REG);
	ep2_i_csr1     = udc_read(S3C2410_UDC_IN_CSR1_REG);
	ep2_i_csr2     = udc_read(S3C2410_UDC_IN_CSR2_REG);
	ep2_o_csr1     = udc_read(S3C2410_UDC_IN_CSR1_REG);
	ep2_o_csr2     = udc_read(S3C2410_UDC_IN_CSR2_REG);

	seq_printf(m, "FUNC_ADDR_REG  : 0x%04X\n"
		 "PWR_REG        : 0x%04X\n"
		 "EP_INT_REG     : 0x%04X\n"
		 "USB_INT_REG    : 0x%04X\n"
		 "EP_INT_EN_REG  : 0x%04X\n"
		 "USB_INT_EN_REG : 0x%04X\n"
		 "EP0_CSR        : 0x%04X\n"
		 "EP1_I_CSR1     : 0x%04X\n"
		 "EP1_I_CSR2     : 0x%04X\n"
		 "EP1_O_CSR1     : 0x%04X\n"
		 "EP1_O_CSR2     : 0x%04X\n"
		 "EP2_I_CSR1     : 0x%04X\n"
		 "EP2_I_CSR2     : 0x%04X\n"
		 "EP2_O_CSR1     : 0x%04X\n"
		 "EP2_O_CSR2     : 0x%04X\n",
			addr_reg, pwr_reg, ep_int_reg, usb_int_reg,
			ep_int_en_reg, usb_int_en_reg, ep0_csr,
			ep1_i_csr1, ep1_i_csr2, ep1_o_csr1, ep1_o_csr2,
			ep2_i_csr1, ep2_i_csr2, ep2_o_csr1, ep2_o_csr2
		);

	return 0;
}

static int s3c2410_udc_debugfs_fops_open(struct inode *inode,
					 struct file *file)
{
	return single_open(file, s3c2410_udc_debugfs_seq_show, NULL);
}

static const struct file_operations s3c2410_udc_debugfs_fops = {
	.open		= s3c2410_udc_debugfs_fops_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.owner		= THIS_MODULE,
};

/* io macros */

static inline void s3c2410_udc_clear_ep0_opr(void __iomem *base)
{
	udc_writeb(base, S3C2410_UDC_INDEX_EP0, S3C2410_UDC_INDEX_REG);
	udc_writeb(base, S3C2410_UDC_EP0_CSR_SOPKTRDY,
			S3C2410_UDC_EP0_CSR_REG);
}

static inline void s3c2410_udc_clear_ep0_sst(void __iomem *base)
{
	udc_writeb(base, S3C2410_UDC_INDEX_EP0, S3C2410_UDC_INDEX_REG);
	writeb(0x00, base + S3C2410_UDC_EP0_CSR_REG);
}

static inline void s3c2410_udc_clear_ep0_se(void __iomem *base)
{
	udc_writeb(base, S3C2410_UDC_INDEX_EP0, S3C2410_UDC_INDEX_REG);
	udc_writeb(base, S3C2410_UDC_EP0_CSR_SSE, S3C2410_UDC_EP0_CSR_REG);
}

static inline void s3c2410_udc_set_ep0_ipr(void __iomem *base)
{
	udc_writeb(base, S3C2410_UDC_INDEX_EP0, S3C2410_UDC_INDEX_REG);
	udc_writeb(base, S3C2410_UDC_EP0_CSR_IPKRDY, S3C2410_UDC_EP0_CSR_REG);
}

static inline void s3c2410_udc_set_ep0_de(void __iomem *base)
{
	udc_writeb(base, S3C2410_UDC_INDEX_EP0, S3C2410_UDC_INDEX_REG);
	udc_writeb(base, S3C2410_UDC_EP0_CSR_DE, S3C2410_UDC_EP0_CSR_REG);
}

inline void s3c2410_udc_set_ep0_ss(void __iomem *b)
{
	udc_writeb(b, S3C2410_UDC_INDEX_EP0, S3C2410_UDC_INDEX_REG);
	udc_writeb(b, S3C2410_UDC_EP0_CSR_SENDSTL, S3C2410_UDC_EP0_CSR_REG);
}

static inline void s3c2410_udc_set_ep0_de_out(void __iomem *base)
{
	udc_writeb(base, S3C2410_UDC_INDEX_EP0, S3C2410_UDC_INDEX_REG);

	udc_writeb(base, (S3C2410_UDC_EP0_CSR_SOPKTRDY
				| S3C2410_UDC_EP0_CSR_DE),
			S3C2410_UDC_EP0_CSR_REG);
}

static inline void s3c2410_udc_set_ep0_sse_out(void __iomem *base)
{
	udc_writeb(base, S3C2410_UDC_INDEX_EP0, S3C2410_UDC_INDEX_REG);
	udc_writeb(base, (S3C2410_UDC_EP0_CSR_SOPKTRDY
				| S3C2410_UDC_EP0_CSR_SSE),
			S3C2410_UDC_EP0_CSR_REG);
}

static inline void s3c2410_udc_set_ep0_de_in(void __iomem *base)
{
	udc_writeb(base, S3C2410_UDC_INDEX_EP0, S3C2410_UDC_INDEX_REG);
	udc_writeb(base, (S3C2410_UDC_EP0_CSR_IPKRDY
			| S3C2410_UDC_EP0_CSR_DE),
		S3C2410_UDC_EP0_CSR_REG);
}

/*------------------------- I/O ----------------------------------*/

/*
 *	s3c2410_udc_done
 */
static void s3c2410_udc_done(struct s3c2410_ep *ep,
		struct s3c2410_request *req, int status)
{
	unsigned halted = ep->halted;

	list_del_init(&req->queue);

	if (likely(req->req.status == -EINPROGRESS))
		req->req.status = status;
	else
		status = req->req.status;

	ep->halted = 1;
	req->req.complete(&ep->ep, &req->req);
	ep->halted = halted;
}

static void s3c2410_udc_nuke(struct s3c2410_udc *udc,
		struct s3c2410_ep *ep, int status)
{
	/* Sanity check */
	if (&ep->queue == NULL)
		return;

	while (!list_empty(&ep->queue)) {
		struct s3c2410_request *req;
		req = list_entry(ep->queue.next, struct s3c2410_request,
				queue);
		s3c2410_udc_done(ep, req, status);
	}
}

static inline void s3c2410_udc_clear_ep_state(struct s3c2410_udc *dev)
{
	unsigned i;

	/* hardware SET_{CONFIGURATION,INTERFACE} automagic resets endpoint
	 * fifos, and pending transactions mustn't be continued in any case.
	 */

	for (i = 1; i < S3C2410_ENDPOINTS; i++)
		s3c2410_udc_nuke(dev, &dev->ep[i], -ECONNABORTED);
}

static inline int s3c2410_udc_fifo_count_out(void)
{
	int tmp;

	tmp = udc_read(S3C2410_UDC_OUT_FIFO_CNT2_REG) << 8;
	tmp |= udc_read(S3C2410_UDC_OUT_FIFO_CNT1_REG);
	return tmp;
}

/*
 *	s3c2410_udc_write_packet
 */
static inline int s3c2410_udc_write_packet(int fifo,
		struct s3c2410_request *req,
		unsigned max)
{
	unsigned len = min(req->req.length - req->req.actual, max);
	u8 *buf = req->req.buf + req->req.actual;

	prefetch(buf);

	dprintk(DEBUG_VERBOSE, "%s %d %d %d %d\n", __func__,
		req->req.actual, req->req.length, len, req->req.actual + len);

	req->req.actual += len;

	udelay(5);
	writesb(base_addr + fifo, buf, len);
	return len;
}

/*
 *	s3c2410_udc_write_fifo
 *
 * return:  0 = still running, 1 = completed, negative = errno
 */
static int s3c2410_udc_write_fifo(struct s3c2410_ep *ep,
		struct s3c2410_request *req)
{
	unsigned	count;
	int		is_last;
	u32		idx;
	int		fifo_reg;
	u32		ep_csr;

	idx = ep->bEndpointAddress & 0x7F;
	switch (idx) {
	default:
		idx = 0;
	case 0:
		fifo_reg = S3C2410_UDC_EP0_FIFO_REG;
		break;
	case 1:
		fifo_reg = S3C2410_UDC_EP1_FIFO_REG;
		break;
	case 2:
		fifo_reg = S3C2410_UDC_EP2_FIFO_REG;
		break;
	case 3:
		fifo_reg = S3C2410_UDC_EP3_FIFO_REG;
		break;
	case 4:
		fifo_reg = S3C2410_UDC_EP4_FIFO_REG;
		break;
	}

	count = s3c2410_udc_write_packet(fifo_reg, req, ep->ep.maxpacket);

	/* last packet is often short (sometimes a zlp) */
	if (count != ep->ep.maxpacket)
		is_last = 1;
	else if (req->req.length != req->req.actual || req->req.zero)
		is_last = 0;
	else
		is_last = 2;

	/* Only ep0 debug messages are interesting */
	if (idx == 0)
		dprintk(DEBUG_NORMAL,
			"Written ep%d %d.%d of %d b [last %d,z %d]\n",
			idx, count, req->req.actual, req->req.length,
			is_last, req->req.zero);

	if (is_last) {
		/* The order is important. It prevents sending 2 packets
		 * at the same time */

		if (idx == 0) {
			/* Reset signal => no need to say 'data sent' */
			if (!(udc_read(S3C2410_UDC_USB_INT_REG)
					& S3C2410_UDC_USBINT_RESET))
				s3c2410_udc_set_ep0_de_in(base_addr);
			ep->dev->ep0state = EP0_IDLE;
		} else {
			udc_write(idx, S3C2410_UDC_INDEX_REG);
			ep_csr = udc_read(S3C2410_UDC_IN_CSR1_REG);
			udc_write(idx, S3C2410_UDC_INDEX_REG);
			udc_write(ep_csr | S3C2410_UDC_ICSR1_PKTRDY,
					S3C2410_UDC_IN_CSR1_REG);
		}

		s3c2410_udc_done(ep, req, 0);
		is_last = 1;
	} else {
		if (idx == 0) {
			/* Reset signal => no need to say 'data sent' */
			if (!(udc_read(S3C2410_UDC_USB_INT_REG)
					& S3C2410_UDC_USBINT_RESET))
				s3c2410_udc_set_ep0_ipr(base_addr);
		} else {
			udc_write(idx, S3C2410_UDC_INDEX_REG);
			ep_csr = udc_read(S3C2410_UDC_IN_CSR1_REG);
			udc_write(idx, S3C2410_UDC_INDEX_REG);
			udc_write(ep_csr | S3C2410_UDC_ICSR1_PKTRDY,
					S3C2410_UDC_IN_CSR1_REG);
		}
	}

	return is_last;
}

static inline int s3c2410_udc_read_packet(int fifo, u8 *buf,
		struct s3c2410_request *req, unsigned avail)
{
	unsigned len;

	len = min(req->req.length - req->req.actual, avail);
	req->req.actual += len;

	readsb(fifo + base_addr, buf, len);
	return len;
}

/*
 * return:  0 = still running, 1 = queue empty, negative = errno
 */
static int s3c2410_udc_read_fifo(struct s3c2410_ep *ep,
				 struct s3c2410_request *req)
{
	u8		*buf;
	u32		ep_csr;
	unsigned	bufferspace;
	int		is_last = 1;
	unsigned	avail;
	int		fifo_count = 0;
	u32		idx;
	int		fifo_reg;

	idx = ep->bEndpointAddress & 0x7F;

	switch (idx) {
	default:
		idx = 0;
	case 0:
		fifo_reg = S3C2410_UDC_EP0_FIFO_REG;
		break;
	case 1:
		fifo_reg = S3C2410_UDC_EP1_FIFO_REG;
		break;
	case 2:
		fifo_reg = S3C2410_UDC_EP2_FIFO_REG;
		break;
	case 3:
		fifo_reg = S3C2410_UDC_EP3_FIFO_REG;
		break;
	case 4:
		fifo_reg = S3C2410_UDC_EP4_FIFO_REG;
		break;
	}

	if (!req->req.length)
		return 1;

	buf = req->req.buf + req->req.actual;
	bufferspace = req->req.length - req->req.actual;
	if (!bufferspace) {
		dprintk(DEBUG_NORMAL, "%s: buffer full!\n", __func__);
		return -1;
	}

	udc_write(idx, S3C2410_UDC_INDEX_REG);

	fifo_count = s3c2410_udc_fifo_count_out();
	dprintk(DEBUG_NORMAL, "%s fifo count : %d\n", __func__, fifo_count);

	if (fifo_count > ep->ep.maxpacket)
		avail = ep->ep.maxpacket;
	else
		avail = fifo_count;

	fifo_count = s3c2410_udc_read_packet(fifo_reg, buf, req, avail);

	/* checking this with ep0 is not accurate as we already
	 * read a control request
	 **/
	if (idx != 0 && fifo_count < ep->ep.maxpacket) {
		is_last = 1;
		/* overflowed this request?  flush extra data */
		if (fifo_count != avail)
			req->req.status = -EOVERFLOW;
	} else {
		is_last = (req->req.length <= req->req.actual) ? 1 : 0;
	}

	udc_write(idx, S3C2410_UDC_INDEX_REG);
	fifo_count = s3c2410_udc_fifo_count_out();

	/* Only ep0 debug messages are interesting */
	if (idx == 0)
		dprintk(DEBUG_VERBOSE, "%s fifo count : %d [last %d]\n",
			__func__, fifo_count, is_last);

	if (is_last) {
		if (idx == 0) {
			s3c2410_udc_set_ep0_de_out(base_addr);
			ep->dev->ep0state = EP0_IDLE;
		} else {
			udc_write(idx, S3C2410_UDC_INDEX_REG);
			ep_csr = udc_read(S3C2410_UDC_OUT_CSR1_REG);
			udc_write(idx, S3C2410_UDC_INDEX_REG);
			udc_write(ep_csr & ~S3C2410_UDC_OCSR1_PKTRDY,
					S3C2410_UDC_OUT_CSR1_REG);
		}

		s3c2410_udc_done(ep, req, 0);
	} else {
		if (idx == 0) {
			s3c2410_udc_clear_ep0_opr(base_addr);
		} else {
			udc_write(idx, S3C2410_UDC_INDEX_REG);
			ep_csr = udc_read(S3C2410_UDC_OUT_CSR1_REG);
			udc_write(idx, S3C2410_UDC_INDEX_REG);
			udc_write(ep_csr & ~S3C2410_UDC_OCSR1_PKTRDY,
					S3C2410_UDC_OUT_CSR1_REG);
		}
	}

	return is_last;
}

static int s3c2410_udc_read_fifo_crq(struct usb_ctrlrequest *crq)
{
	unsigned char *outbuf = (unsigned char *)crq;
	int bytes_read = 0;

	udc_write(0, S3C2410_UDC_INDEX_REG);

	bytes_read = s3c2410_udc_fifo_count_out();

	dprintk(DEBUG_NORMAL, "%s: fifo_count=%d\n", __func__, bytes_read);

	if (bytes_read > sizeof(struct usb_ctrlrequest))
		bytes_read = sizeof(struct usb_ctrlrequest);

	readsb(S3C2410_UDC_EP0_FIFO_REG + base_addr, outbuf, bytes_read);

	dprintk(DEBUG_VERBOSE, "%s: len=%d %02x:%02x {%x,%x,%x}\n", __func__,
		bytes_read, crq->bRequest, crq->bRequestType,
		crq->wValue, crq->wIndex, crq->wLength);

	return bytes_read;
}

static int s3c2410_udc_get_status(struct s3c2410_udc *dev,
		struct usb_ctrlrequest *crq)
{
	u16 status = 0;
	u8 ep_num = crq->wIndex & 0x7F;
	u8 is_in = crq->wIndex & USB_DIR_IN;

	switch (crq->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_INTERFACE:
		break;

	case USB_RECIP_DEVICE:
		status = dev->devstatus;
		break;

	case USB_RECIP_ENDPOINT:
		if (ep_num > 4 || crq->wLength > 2)
			return 1;

		if (ep_num == 0) {
			udc_write(0, S3C2410_UDC_INDEX_REG);
			status = udc_read(S3C2410_UDC_IN_CSR1_REG);
			status = status & S3C2410_UDC_EP0_CSR_SENDSTL;
		} else {
			udc_write(ep_num, S3C2410_UDC_INDEX_REG);
			if (is_in) {
				status = udc_read(S3C2410_UDC_IN_CSR1_REG);
				status = status & S3C2410_UDC_ICSR1_SENDSTL;
			} else {
				status = udc_read(S3C2410_UDC_OUT_CSR1_REG);
				status = status & S3C2410_UDC_OCSR1_SENDSTL;
			}
		}

		status = status ? 1 : 0;
		break;

	default:
		return 1;
	}

	/* Seems to be needed to get it working. ouch :( */
	udelay(5);
	udc_write(status & 0xFF, S3C2410_UDC_EP0_FIFO_REG);
	udc_write(status >> 8, S3C2410_UDC_EP0_FIFO_REG);
	s3c2410_udc_set_ep0_de_in(base_addr);

	return 0;
}
/*------------------------- usb state machine -------------------------------*/
static int s3c2410_udc_set_halt(struct usb_ep *_ep, int value);

static void s3c2410_udc_handle_ep0_idle(struct s3c2410_udc *dev,
					struct s3c2410_ep *ep,
					struct usb_ctrlrequest *crq,
					u32 ep0csr)
{
	int len, ret, tmp;

	/* start control request? */
	if (!(ep0csr & S3C2410_UDC_EP0_CSR_OPKRDY))
		return;

	s3c2410_udc_nuke(dev, ep, -EPROTO);

	len = s3c2410_udc_read_fifo_crq(crq);
	if (len != sizeof(*crq)) {
		dprintk(DEBUG_NORMAL, "setup begin: fifo READ ERROR"
			" wanted %d bytes got %d. Stalling out...\n",
			sizeof(*crq), len);
		s3c2410_udc_set_ep0_ss(base_addr);
		return;
	}

	dprintk(DEBUG_NORMAL, "bRequest = %d bRequestType %d wLength = %d\n",
		crq->bRequest, crq->bRequestType, crq->wLength);

	/* cope with automagic for some standard requests. */
	dev->req_std = (crq->bRequestType & USB_TYPE_MASK)
		== USB_TYPE_STANDARD;
	dev->req_config = 0;
	dev->req_pending = 1;

	switch (crq->bRequest) {
	case USB_REQ_SET_CONFIGURATION:
		dprintk(DEBUG_NORMAL, "USB_REQ_SET_CONFIGURATION ...\n");

		if (crq->bRequestType == USB_RECIP_DEVICE) {
			dev->req_config = 1;
			s3c2410_udc_set_ep0_de_out(base_addr);
		}
		break;

	case USB_REQ_SET_INTERFACE:
		dprintk(DEBUG_NORMAL, "USB_REQ_SET_INTERFACE ...\n");

		if (crq->bRequestType == USB_RECIP_INTERFACE) {
			dev->req_config = 1;
			s3c2410_udc_set_ep0_de_out(base_addr);
		}
		break;

	case USB_REQ_SET_ADDRESS:
		dprintk(DEBUG_NORMAL, "USB_REQ_SET_ADDRESS ...\n");

		if (crq->bRequestType == USB_RECIP_DEVICE) {
			tmp = crq->wValue & 0x7F;
			dev->address = tmp;
			udc_write((tmp | S3C2410_UDC_FUNCADDR_UPDATE),
					S3C2410_UDC_FUNC_ADDR_REG);
			s3c2410_udc_set_ep0_de_out(base_addr);
			return;
		}
		break;

	case USB_REQ_GET_STATUS:
		dprintk(DEBUG_NORMAL, "USB_REQ_GET_STATUS ...\n");
		s3c2410_udc_clear_ep0_opr(base_addr);

		if (dev->req_std) {
			if (!s3c2410_udc_get_status(dev, crq))
				return;
		}
		break;

	case USB_REQ_CLEAR_FEATURE:
		s3c2410_udc_clear_ep0_opr(base_addr);

		if (crq->bRequestType != USB_RECIP_ENDPOINT)
			break;

		if (crq->wValue != USB_ENDPOINT_HALT || crq->wLength != 0)
			break;

		s3c2410_udc_set_halt(&dev->ep[crq->wIndex & 0x7f].ep, 0);
		s3c2410_udc_set_ep0_de_out(base_addr);
		return;

	case USB_REQ_SET_FEATURE:
		s3c2410_udc_clear_ep0_opr(base_addr);

		if (crq->bRequestType != USB_RECIP_ENDPOINT)
			break;

		if (crq->wValue != USB_ENDPOINT_HALT || crq->wLength != 0)
			break;

		s3c2410_udc_set_halt(&dev->ep[crq->wIndex & 0x7f].ep, 1);
		s3c2410_udc_set_ep0_de_out(base_addr);
		return;

	default:
		s3c2410_udc_clear_ep0_opr(base_addr);
		break;
	}

	if (crq->bRequestType & USB_DIR_IN)
		dev->ep0state = EP0_IN_DATA_PHASE;
	else
		dev->ep0state = EP0_OUT_DATA_PHASE;

	if (!dev->driver)
		return;

	/* deliver the request to the gadget driver */
	ret = dev->driver->setup(&dev->gadget, crq);
	if (ret < 0) {
		if (dev->req_config) {
			dprintk(DEBUG_NORMAL, "config change %02x fail %d?\n",
				crq->bRequest, ret);
			return;
		}

		if (ret == -EOPNOTSUPP)
			dprintk(DEBUG_NORMAL, "Operation not supported\n");
		else
			dprintk(DEBUG_NORMAL,
				"dev->driver->setup failed. (%d)\n", ret);

		udelay(5);
		s3c2410_udc_set_ep0_ss(base_addr);
		s3c2410_udc_set_ep0_de_out(base_addr);
		dev->ep0state = EP0_IDLE;
		/* deferred i/o == no response yet */
	} else if (dev->req_pending) {
		dprintk(DEBUG_VERBOSE, "dev->req_pending... what now?\n");
		dev->req_pending = 0;
	}

	dprintk(DEBUG_VERBOSE, "ep0state %s\n", ep0states[dev->ep0state]);
}

static void s3c2410_udc_handle_ep0(struct s3c2410_udc *dev)
{
	u32			ep0csr;
	struct s3c2410_ep	*ep = &dev->ep[0];
	struct s3c2410_request	*req;
	struct usb_ctrlrequest	crq;

	if (list_empty(&ep->queue))
		req = NULL;
	else
		req = list_entry(ep->queue.next, struct s3c2410_request, queue);

	/* We make the assumption that S3C2410_UDC_IN_CSR1_REG equal to
	 * S3C2410_UDC_EP0_CSR_REG when index is zero */

	udc_write(0, S3C2410_UDC_INDEX_REG);
	ep0csr = udc_read(S3C2410_UDC_IN_CSR1_REG);

	dprintk(DEBUG_NORMAL, "ep0csr %x ep0state %s\n",
		ep0csr, ep0states[dev->ep0state]);

	/* clear stall status */
	if (ep0csr & S3C2410_UDC_EP0_CSR_SENTSTL) {
		s3c2410_udc_nuke(dev, ep, -EPIPE);
		dprintk(DEBUG_NORMAL, "... clear SENT_STALL ...\n");
		s3c2410_udc_clear_ep0_sst(base_addr);
		dev->ep0state = EP0_IDLE;
		return;
	}

	/* clear setup end */
	if (ep0csr & S3C2410_UDC_EP0_CSR_SE) {
		dprintk(DEBUG_NORMAL, "... serviced SETUP_END ...\n");
		s3c2410_udc_nuke(dev, ep, 0);
		s3c2410_udc_clear_ep0_se(base_addr);
		dev->ep0state = EP0_IDLE;
	}

	switch (dev->ep0state) {
	case EP0_IDLE:
		s3c2410_udc_handle_ep0_idle(dev, ep, &crq, ep0csr);
		break;

	case EP0_IN_DATA_PHASE:			/* GET_DESCRIPTOR etc */
		dprintk(DEBUG_NORMAL, "EP0_IN_DATA_PHASE ... what now?\n");
		if (!(ep0csr & S3C2410_UDC_EP0_CSR_IPKRDY) && req)
			s3c2410_udc_write_fifo(ep, req);
		break;

	case EP0_OUT_DATA_PHASE:		/* SET_DESCRIPTOR etc */
		dprintk(DEBUG_NORMAL, "EP0_OUT_DATA_PHASE ... what now?\n");
		if ((ep0csr & S3C2410_UDC_EP0_CSR_OPKRDY) && req)
			s3c2410_udc_read_fifo(ep, req);
		break;

	case EP0_END_XFER:
		dprintk(DEBUG_NORMAL, "EP0_END_XFER ... what now?\n");
		dev->ep0state = EP0_IDLE;
		break;

	case EP0_STALL:
		dprintk(DEBUG_NORMAL, "EP0_STALL ... what now?\n");
		dev->ep0state = EP0_IDLE;
		break;
	}
}

/*
 *	handle_ep - Manage I/O endpoints
 */

static void s3c2410_udc_handle_ep(struct s3c2410_ep *ep)
{
	struct s3c2410_request	*req;
	int			is_in = ep->bEndpointAddress & USB_DIR_IN;
	u32			ep_csr1;
	u32			idx;

	if (likely(!list_empty(&ep->queue)))
		req = list_entry(ep->queue.next,
				struct s3c2410_request, queue);
	else
		req = NULL;

	idx = ep->bEndpointAddress & 0x7F;

	if (is_in) {
		udc_write(idx, S3C2410_UDC_INDEX_REG);
		ep_csr1 = udc_read(S3C2410_UDC_IN_CSR1_REG);
		dprintk(DEBUG_VERBOSE, "ep%01d write csr:%02x %d\n",
			idx, ep_csr1, req ? 1 : 0);

		if (ep_csr1 & S3C2410_UDC_ICSR1_SENTSTL) {
			dprintk(DEBUG_VERBOSE, "st\n");
			udc_write(idx, S3C2410_UDC_INDEX_REG);
			udc_write(ep_csr1 & ~S3C2410_UDC_ICSR1_SENTSTL,
					S3C2410_UDC_IN_CSR1_REG);
			return;
		}

		if (!(ep_csr1 & S3C2410_UDC_ICSR1_PKTRDY) && req)
			s3c2410_udc_write_fifo(ep, req);
	} else {
		udc_write(idx, S3C2410_UDC_INDEX_REG);
		ep_csr1 = udc_read(S3C2410_UDC_OUT_CSR1_REG);
		dprintk(DEBUG_VERBOSE, "ep%01d rd csr:%02x\n", idx, ep_csr1);

		if (ep_csr1 & S3C2410_UDC_OCSR1_SENTSTL) {
			udc_write(idx, S3C2410_UDC_INDEX_REG);
			udc_write(ep_csr1 & ~S3C2410_UDC_OCSR1_SENTSTL,
					S3C2410_UDC_OUT_CSR1_REG);
			return;
		}

		if ((ep_csr1 & S3C2410_UDC_OCSR1_PKTRDY) && req)
			s3c2410_udc_read_fifo(ep, req);
	}
}

#include <mach/regs-irq.h>

/*
 *	s3c2410_udc_irq - interrupt handler
 */
static irqreturn_t s3c2410_udc_irq(int dummy, void *_dev)
{
	struct s3c2410_udc *dev = _dev;
	int usb_status;
	int usbd_status;
	int pwr_reg;
	int ep0csr;
	int i;
	u32 idx, idx2;
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);

	/* Driver connected ? */
	if (!dev->driver) {
		/* Clear interrupts */
		udc_write(udc_read(S3C2410_UDC_USB_INT_REG),
				S3C2410_UDC_USB_INT_REG);
		udc_write(udc_read(S3C2410_UDC_EP_INT_REG),
				S3C2410_UDC_EP_INT_REG);
	}

	/* Save index */
	idx = udc_read(S3C2410_UDC_INDEX_REG);

	/* Read status registers */
	usb_status = udc_read(S3C2410_UDC_USB_INT_REG);
	usbd_status = udc_read(S3C2410_UDC_EP_INT_REG);
	pwr_reg = udc_read(S3C2410_UDC_PWR_REG);

	udc_writeb(base_addr, S3C2410_UDC_INDEX_EP0, S3C2410_UDC_INDEX_REG);
	ep0csr = udc_read(S3C2410_UDC_IN_CSR1_REG);

	dprintk(DEBUG_NORMAL, "usbs=%02x, usbds=%02x, pwr=%02x ep0csr=%02x\n",
		usb_status, usbd_status, pwr_reg, ep0csr);

	/*
	 * Now, handle interrupts. There's two types :
	 * - Reset, Resume, Suspend coming -> usb_int_reg
	 * - EP -> ep_int_reg
	 */

	/* RESET */
	if (usb_status & S3C2410_UDC_USBINT_RESET) {
		/* two kind of reset :
		 * - reset start -> pwr reg = 8
		 * - reset end   -> pwr reg = 0
		 **/
		dprintk(DEBUG_NORMAL, "USB reset csr %x pwr %x\n",
			ep0csr, pwr_reg);

		dev->gadget.speed = USB_SPEED_UNKNOWN;
		udc_write(0x00, S3C2410_UDC_INDEX_REG);
		udc_write((dev->ep[0].ep.maxpacket & 0x7ff) >> 3,
				S3C2410_UDC_MAXP_REG);
		dev->address = 0;

		dev->ep0state = EP0_IDLE;
		dev->gadget.speed = USB_SPEED_FULL;

		/* clear interrupt */
		udc_write(S3C2410_UDC_USBINT_RESET,
				S3C2410_UDC_USB_INT_REG);

		udc_write(idx, S3C2410_UDC_INDEX_REG);
		spin_unlock_irqrestore(&dev->lock, flags);
		return IRQ_HANDLED;
	}

	/* RESUME */
	if (usb_status & S3C2410_UDC_USBINT_RESUME) {
		dprintk(DEBUG_NORMAL, "USB resume\n");

		/* clear interrupt */
		udc_write(S3C2410_UDC_USBINT_RESUME,
				S3C2410_UDC_USB_INT_REG);

		if (dev->gadget.speed != USB_SPEED_UNKNOWN
				&& dev->driver
				&& dev->driver->resume)
			dev->driver->resume(&dev->gadget);
	}

	/* SUSPEND */
	if (usb_status & S3C2410_UDC_USBINT_SUSPEND) {
		dprintk(DEBUG_NORMAL, "USB suspend\n");

		/* clear interrupt */
		udc_write(S3C2410_UDC_USBINT_SUSPEND,
				S3C2410_UDC_USB_INT_REG);

		if (dev->gadget.speed != USB_SPEED_UNKNOWN
				&& dev->driver
				&& dev->driver->suspend)
			dev->driver->suspend(&dev->gadget);

		dev->ep0state = EP0_IDLE;
	}

	/* EP */
	/* control traffic */
	/* check on ep0csr != 0 is not a good idea as clearing in_pkt_ready
	 * generate an interrupt
	 */
	if (usbd_status & S3C2410_UDC_INT_EP0) {
		dprintk(DEBUG_VERBOSE, "USB ep0 irq\n");
		/* Clear the interrupt bit by setting it to 1 */
		udc_write(S3C2410_UDC_INT_EP0, S3C2410_UDC_EP_INT_REG);
		s3c2410_udc_handle_ep0(dev);
	}

	/* endpoint data transfers */
	for (i = 1; i < S3C2410_ENDPOINTS; i++) {
		u32 tmp = 1 << i;
		if (usbd_status & tmp) {
			dprintk(DEBUG_VERBOSE, "USB ep%d irq\n", i);

			/* Clear the interrupt bit by setting it to 1 */
			udc_write(tmp, S3C2410_UDC_EP_INT_REG);
			s3c2410_udc_handle_ep(&dev->ep[i]);
		}
	}

	/* what else causes this interrupt? a receive! who is it? */
	if (!usb_status && !usbd_status && !pwr_reg && !ep0csr) {
		for (i = 1; i < S3C2410_ENDPOINTS; i++) {
			idx2 = udc_read(S3C2410_UDC_INDEX_REG);
			udc_write(i, S3C2410_UDC_INDEX_REG);

			if (udc_read(S3C2410_UDC_OUT_CSR1_REG) & 0x1)
				s3c2410_udc_handle_ep(&dev->ep[i]);

			/* restore index */
			udc_write(idx2, S3C2410_UDC_INDEX_REG);
		}
	}

	dprintk(DEBUG_VERBOSE, "irq: %d s3c2410_udc_done.\n", IRQ_USBD);

	/* Restore old index */
	udc_write(idx, S3C2410_UDC_INDEX_REG);

	spin_unlock_irqrestore(&dev->lock, flags);

	return IRQ_HANDLED;
}
/*------------------------- s3c2410_ep_ops ----------------------------------*/

static inline struct s3c2410_ep *to_s3c2410_ep(struct usb_ep *ep)
{
	return container_of(ep, struct s3c2410_ep, ep);
}

static inline struct s3c2410_udc *to_s3c2410_udc(struct usb_gadget *gadget)
{
	return container_of(gadget, struct s3c2410_udc, gadget);
}

static inline struct s3c2410_request *to_s3c2410_req(struct usb_request *req)
{
	return container_of(req, struct s3c2410_request, req);
}

/*
 *	s3c2410_udc_ep_enable
 */
static int s3c2410_udc_ep_enable(struct usb_ep *_ep,
				 const struct usb_endpoint_descriptor *desc)
{
	struct s3c2410_udc	*dev;
	struct s3c2410_ep	*ep;
	u32			max, tmp;
	unsigned long		flags;
	u32			csr1, csr2;
	u32			int_en_reg;

	ep = to_s3c2410_ep(_ep);

	if (!_ep || !desc
			|| _ep->name == ep0name
			|| desc->bDescriptorType != USB_DT_ENDPOINT)
		return -EINVAL;

	dev = ep->dev;
	if (!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	max = usb_endpoint_maxp(desc) & 0x1fff;

	local_irq_save(flags);
	_ep->maxpacket = max & 0x7ff;
	ep->ep.desc = desc;
	ep->halted = 0;
	ep->bEndpointAddress = desc->bEndpointAddress;

	/* set max packet */
	udc_write(ep->num, S3C2410_UDC_INDEX_REG);
	udc_write(max >> 3, S3C2410_UDC_MAXP_REG);

	/* set type, direction, address; reset fifo counters */
	if (desc->bEndpointAddress & USB_DIR_IN) {
		csr1 = S3C2410_UDC_ICSR1_FFLUSH|S3C2410_UDC_ICSR1_CLRDT;
		csr2 = S3C2410_UDC_ICSR2_MODEIN|S3C2410_UDC_ICSR2_DMAIEN;

		udc_write(ep->num, S3C2410_UDC_INDEX_REG);
		udc_write(csr1, S3C2410_UDC_IN_CSR1_REG);
		udc_write(ep->num, S3C2410_UDC_INDEX_REG);
		udc_write(csr2, S3C2410_UDC_IN_CSR2_REG);
	} else {
		/* don't flush in fifo or it will cause endpoint interrupt */
		csr1 = S3C2410_UDC_ICSR1_CLRDT;
		csr2 = S3C2410_UDC_ICSR2_DMAIEN;

		udc_write(ep->num, S3C2410_UDC_INDEX_REG);
		udc_write(csr1, S3C2410_UDC_IN_CSR1_REG);
		udc_write(ep->num, S3C2410_UDC_INDEX_REG);
		udc_write(csr2, S3C2410_UDC_IN_CSR2_REG);

		csr1 = S3C2410_UDC_OCSR1_FFLUSH | S3C2410_UDC_OCSR1_CLRDT;
		csr2 = S3C2410_UDC_OCSR2_DMAIEN;

		udc_write(ep->num, S3C2410_UDC_INDEX_REG);
		udc_write(csr1, S3C2410_UDC_OUT_CSR1_REG);
		udc_write(ep->num, S3C2410_UDC_INDEX_REG);
		udc_write(csr2, S3C2410_UDC_OUT_CSR2_REG);
	}

	/* enable irqs */
	int_en_reg = udc_read(S3C2410_UDC_EP_INT_EN_REG);
	udc_write(int_en_reg | (1 << ep->num), S3C2410_UDC_EP_INT_EN_REG);

	/* print some debug message */
	tmp = desc->bEndpointAddress;
	dprintk(DEBUG_NORMAL, "enable %s(%d) ep%x%s-blk max %02x\n",
		 _ep->name, ep->num, tmp,
		 desc->bEndpointAddress & USB_DIR_IN ? "in" : "out", max);

	local_irq_restore(flags);
	s3c2410_udc_set_halt(_ep, 0);

	return 0;
}

/*
 * s3c2410_udc_ep_disable
 */
static int s3c2410_udc_ep_disable(struct usb_ep *_ep)
{
	struct s3c2410_ep *ep = to_s3c2410_ep(_ep);
	unsigned long flags;
	u32 int_en_reg;

	if (!_ep || !ep->ep.desc) {
		dprintk(DEBUG_NORMAL, "%s not enabled\n",
			_ep ? ep->ep.name : NULL);
		return -EINVAL;
	}

	local_irq_save(flags);

	dprintk(DEBUG_NORMAL, "ep_disable: %s\n", _ep->name);

	ep->ep.desc = NULL;
	ep->halted = 1;

	s3c2410_udc_nuke(ep->dev, ep, -ESHUTDOWN);

	/* disable irqs */
	int_en_reg = udc_read(S3C2410_UDC_EP_INT_EN_REG);
	udc_write(int_en_reg & ~(1<<ep->num), S3C2410_UDC_EP_INT_EN_REG);

	local_irq_restore(flags);

	dprintk(DEBUG_NORMAL, "%s disabled\n", _ep->name);

	return 0;
}

/*
 * s3c2410_udc_alloc_request
 */
static struct usb_request *
s3c2410_udc_alloc_request(struct usb_ep *_ep, gfp_t mem_flags)
{
	struct s3c2410_request *req;

	dprintk(DEBUG_VERBOSE, "%s(%p,%d)\n", __func__, _ep, mem_flags);

	if (!_ep)
		return NULL;

	req = kzalloc(sizeof(struct s3c2410_request), mem_flags);
	if (!req)
		return NULL;

	INIT_LIST_HEAD(&req->queue);
	return &req->req;
}

/*
 * s3c2410_udc_free_request
 */
static void
s3c2410_udc_free_request(struct usb_ep *_ep, struct usb_request *_req)
{
	struct s3c2410_ep	*ep = to_s3c2410_ep(_ep);
	struct s3c2410_request	*req = to_s3c2410_req(_req);

	dprintk(DEBUG_VERBOSE, "%s(%p,%p)\n", __func__, _ep, _req);

	if (!ep || !_req || (!ep->ep.desc && _ep->name != ep0name))
		return;

	WARN_ON(!list_empty(&req->queue));
	kfree(req);
}

/*
 *	s3c2410_udc_queue
 */
static int s3c2410_udc_queue(struct usb_ep *_ep, struct usb_request *_req,
		gfp_t gfp_flags)
{
	struct s3c2410_request	*req = to_s3c2410_req(_req);
	struct s3c2410_ep	*ep = to_s3c2410_ep(_ep);
	struct s3c2410_udc	*dev;
	u32			ep_csr = 0;
	int			fifo_count = 0;
	unsigned long		flags;

	if (unlikely(!_ep || (!ep->ep.desc && ep->ep.name != ep0name))) {
		dprintk(DEBUG_NORMAL, "%s: invalid args\n", __func__);
		return -EINVAL;
	}

	dev = ep->dev;
	if (unlikely(!dev->driver
			|| dev->gadget.speed == USB_SPEED_UNKNOWN)) {
		return -ESHUTDOWN;
	}

	local_irq_save(flags);

	if (unlikely(!_req || !_req->complete
			|| !_req->buf || !list_empty(&req->queue))) {
		if (!_req)
			dprintk(DEBUG_NORMAL, "%s: 1 X X X\n", __func__);
		else {
			dprintk(DEBUG_NORMAL, "%s: 0 %01d %01d %01d\n",
				__func__, !_req->complete, !_req->buf,
				!list_empty(&req->queue));
		}

		local_irq_restore(flags);
		return -EINVAL;
	}

	_req->status = -EINPROGRESS;
	_req->actual = 0;

	dprintk(DEBUG_VERBOSE, "%s: ep%x len %d\n",
		 __func__, ep->bEndpointAddress, _req->length);

	if (ep->bEndpointAddress) {
		udc_write(ep->bEndpointAddress & 0x7F, S3C2410_UDC_INDEX_REG);

		ep_csr = udc_read((ep->bEndpointAddress & USB_DIR_IN)
				? S3C2410_UDC_IN_CSR1_REG
				: S3C2410_UDC_OUT_CSR1_REG);
		fifo_count = s3c2410_udc_fifo_count_out();
	} else {
		udc_write(0, S3C2410_UDC_INDEX_REG);
		ep_csr = udc_read(S3C2410_UDC_IN_CSR1_REG);
		fifo_count = s3c2410_udc_fifo_count_out();
	}

	/* kickstart this i/o queue? */
	if (list_empty(&ep->queue) && !ep->halted) {
		if (ep->bEndpointAddress == 0 /* ep0 */) {
			switch (dev->ep0state) {
			case EP0_IN_DATA_PHASE:
				if (!(ep_csr&S3C2410_UDC_EP0_CSR_IPKRDY)
						&& s3c2410_udc_write_fifo(ep,
							req)) {
					dev->ep0state = EP0_IDLE;
					req = NULL;
				}
				break;

			case EP0_OUT_DATA_PHASE:
				if ((!_req->length)
					|| ((ep_csr & S3C2410_UDC_OCSR1_PKTRDY)
						&& s3c2410_udc_read_fifo(ep,
							req))) {
					dev->ep0state = EP0_IDLE;
					req = NULL;
				}
				break;

			default:
				local_irq_restore(flags);
				return -EL2HLT;
			}
		} else if ((ep->bEndpointAddress & USB_DIR_IN) != 0
				&& (!(ep_csr&S3C2410_UDC_OCSR1_PKTRDY))
				&& s3c2410_udc_write_fifo(ep, req)) {
			req = NULL;
		} else if ((ep_csr & S3C2410_UDC_OCSR1_PKTRDY)
				&& fifo_count
				&& s3c2410_udc_read_fifo(ep, req)) {
			req = NULL;
		}
	}

	/* pio or dma irq handler advances the queue. */
	if (likely(req))
		list_add_tail(&req->queue, &ep->queue);

	local_irq_restore(flags);

	dprintk(DEBUG_VERBOSE, "%s ok\n", __func__);
	return 0;
}

/*
 *	s3c2410_udc_dequeue
 */
static int s3c2410_udc_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct s3c2410_ep	*ep = to_s3c2410_ep(_ep);
	struct s3c2410_udc	*udc;
	int			retval = -EINVAL;
	unsigned long		flags;
	struct s3c2410_request	*req = NULL;

	dprintk(DEBUG_VERBOSE, "%s(%p,%p)\n", __func__, _ep, _req);

	if (!the_controller->driver)
		return -ESHUTDOWN;

	if (!_ep || !_req)
		return retval;

	udc = to_s3c2410_udc(ep->gadget);

	local_irq_save(flags);

	list_for_each_entry(req, &ep->queue, queue) {
		if (&req->req == _req) {
			list_del_init(&req->queue);
			_req->status = -ECONNRESET;
			retval = 0;
			break;
		}
	}

	if (retval == 0) {
		dprintk(DEBUG_VERBOSE,
			"dequeued req %p from %s, len %d buf %p\n",
			req, _ep->name, _req->length, _req->buf);

		s3c2410_udc_done(ep, req, -ECONNRESET);
	}

	local_irq_restore(flags);
	return retval;
}

/*
 * s3c2410_udc_set_halt
 */
static int s3c2410_udc_set_halt(struct usb_ep *_ep, int value)
{
	struct s3c2410_ep	*ep = to_s3c2410_ep(_ep);
	u32			ep_csr = 0;
	unsigned long		flags;
	u32			idx;

	if (unlikely(!_ep || (!ep->ep.desc && ep->ep.name != ep0name))) {
		dprintk(DEBUG_NORMAL, "%s: inval 2\n", __func__);
		return -EINVAL;
	}

	local_irq_save(flags);

	idx = ep->bEndpointAddress & 0x7F;

	if (idx == 0) {
		s3c2410_udc_set_ep0_ss(base_addr);
		s3c2410_udc_set_ep0_de_out(base_addr);
	} else {
		udc_write(idx, S3C2410_UDC_INDEX_REG);
		ep_csr = udc_read((ep->bEndpointAddress & USB_DIR_IN)
				? S3C2410_UDC_IN_CSR1_REG
				: S3C2410_UDC_OUT_CSR1_REG);

		if ((ep->bEndpointAddress & USB_DIR_IN) != 0) {
			if (value)
				udc_write(ep_csr | S3C2410_UDC_ICSR1_SENDSTL,
					S3C2410_UDC_IN_CSR1_REG);
			else {
				ep_csr &= ~S3C2410_UDC_ICSR1_SENDSTL;
				udc_write(ep_csr, S3C2410_UDC_IN_CSR1_REG);
				ep_csr |= S3C2410_UDC_ICSR1_CLRDT;
				udc_write(ep_csr, S3C2410_UDC_IN_CSR1_REG);
			}
		} else {
			if (value)
				udc_write(ep_csr | S3C2410_UDC_OCSR1_SENDSTL,
					S3C2410_UDC_OUT_CSR1_REG);
			else {
				ep_csr &= ~S3C2410_UDC_OCSR1_SENDSTL;
				udc_write(ep_csr, S3C2410_UDC_OUT_CSR1_REG);
				ep_csr |= S3C2410_UDC_OCSR1_CLRDT;
				udc_write(ep_csr, S3C2410_UDC_OUT_CSR1_REG);
			}
		}
	}

	ep->halted = value ? 1 : 0;
	local_irq_restore(flags);

	return 0;
}

static const struct usb_ep_ops s3c2410_ep_ops = {
	.enable		= s3c2410_udc_ep_enable,
	.disable	= s3c2410_udc_ep_disable,

	.alloc_request	= s3c2410_udc_alloc_request,
	.free_request	= s3c2410_udc_free_request,

	.queue		= s3c2410_udc_queue,
	.dequeue	= s3c2410_udc_dequeue,

	.set_halt	= s3c2410_udc_set_halt,
};

/*------------------------- usb_gadget_ops ----------------------------------*/

/*
 *	s3c2410_udc_get_frame
 */
static int s3c2410_udc_get_frame(struct usb_gadget *_gadget)
{
	int tmp;

	dprintk(DEBUG_VERBOSE, "%s()\n", __func__);

	tmp = udc_read(S3C2410_UDC_FRAME_NUM2_REG) << 8;
	tmp |= udc_read(S3C2410_UDC_FRAME_NUM1_REG);
	return tmp;
}

/*
 *	s3c2410_udc_wakeup
 */
static int s3c2410_udc_wakeup(struct usb_gadget *_gadget)
{
	dprintk(DEBUG_NORMAL, "%s()\n", __func__);
	return 0;
}

/*
 *	s3c2410_udc_set_selfpowered
 */
static int s3c2410_udc_set_selfpowered(struct usb_gadget *gadget, int value)
{
	struct s3c2410_udc *udc = to_s3c2410_udc(gadget);

	dprintk(DEBUG_NORMAL, "%s()\n", __func__);

	if (value)
		udc->devstatus |= (1 << USB_DEVICE_SELF_POWERED);
	else
		udc->devstatus &= ~(1 << USB_DEVICE_SELF_POWERED);

	return 0;
}

static void s3c2410_udc_disable(struct s3c2410_udc *dev);
static void s3c2410_udc_enable(struct s3c2410_udc *dev);

static int s3c2410_udc_set_pullup(struct s3c2410_udc *udc, int is_on)
{
	dprintk(DEBUG_NORMAL, "%s()\n", __func__);

	if (udc_info && (udc_info->udc_command ||
		gpio_is_valid(udc_info->pullup_pin))) {

		if (is_on)
			s3c2410_udc_enable(udc);
		else {
			if (udc->gadget.speed != USB_SPEED_UNKNOWN) {
				if (udc->driver && udc->driver->disconnect)
					udc->driver->disconnect(&udc->gadget);

			}
			s3c2410_udc_disable(udc);
		}
	} else {
		return -EOPNOTSUPP;
	}

	return 0;
}

static int s3c2410_udc_vbus_session(struct usb_gadget *gadget, int is_active)
{
	struct s3c2410_udc *udc = to_s3c2410_udc(gadget);

	dprintk(DEBUG_NORMAL, "%s()\n", __func__);

	udc->vbus = (is_active != 0);
	s3c2410_udc_set_pullup(udc, is_active);
	return 0;
}

static int s3c2410_udc_pullup(struct usb_gadget *gadget, int is_on)
{
	struct s3c2410_udc *udc = to_s3c2410_udc(gadget);

	dprintk(DEBUG_NORMAL, "%s()\n", __func__);

	s3c2410_udc_set_pullup(udc, is_on ? 0 : 1);
	return 0;
}

static irqreturn_t s3c2410_udc_vbus_irq(int irq, void *_dev)
{
	struct s3c2410_udc	*dev = _dev;
	unsigned int		value;

	dprintk(DEBUG_NORMAL, "%s()\n", __func__);

	value = gpio_get_value(udc_info->vbus_pin) ? 1 : 0;
	if (udc_info->vbus_pin_inverted)
		value = !value;

	if (value != dev->vbus)
		s3c2410_udc_vbus_session(&dev->gadget, value);

	return IRQ_HANDLED;
}

static int s3c2410_vbus_draw(struct usb_gadget *_gadget, unsigned ma)
{
	dprintk(DEBUG_NORMAL, "%s()\n", __func__);

	if (udc_info && udc_info->vbus_draw) {
		udc_info->vbus_draw(ma);
		return 0;
	}

	return -ENOTSUPP;
}

static int s3c2410_udc_start(struct usb_gadget_driver *driver,
		int (*bind)(struct usb_gadget *, struct usb_gadget_driver *));
static int s3c2410_udc_stop(struct usb_gadget_driver *driver);

static const struct usb_gadget_ops s3c2410_ops = {
	.get_frame		= s3c2410_udc_get_frame,
	.wakeup			= s3c2410_udc_wakeup,
	.set_selfpowered	= s3c2410_udc_set_selfpowered,
	.pullup			= s3c2410_udc_pullup,
	.vbus_session		= s3c2410_udc_vbus_session,
	.vbus_draw		= s3c2410_vbus_draw,
	.start			= s3c2410_udc_start,
	.stop			= s3c2410_udc_stop,
};

static void s3c2410_udc_command(enum s3c2410_udc_cmd_e cmd)
{
	if (!udc_info)
		return;

	if (udc_info->udc_command) {
		udc_info->udc_command(cmd);
	} else if (gpio_is_valid(udc_info->pullup_pin)) {
		int value;

		switch (cmd) {
		case S3C2410_UDC_P_ENABLE:
			value = 1;
			break;
		case S3C2410_UDC_P_DISABLE:
			value = 0;
			break;
		default:
			return;
		}
		value ^= udc_info->pullup_pin_inverted;

		gpio_set_value(udc_info->pullup_pin, value);
	}
}

/*------------------------- gadget driver handling---------------------------*/
/*
 * s3c2410_udc_disable
 */
static void s3c2410_udc_disable(struct s3c2410_udc *dev)
{
	dprintk(DEBUG_NORMAL, "%s()\n", __func__);

	/* Disable all interrupts */
	udc_write(0x00, S3C2410_UDC_USB_INT_EN_REG);
	udc_write(0x00, S3C2410_UDC_EP_INT_EN_REG);

	/* Clear the interrupt registers */
	udc_write(S3C2410_UDC_USBINT_RESET
				| S3C2410_UDC_USBINT_RESUME
				| S3C2410_UDC_USBINT_SUSPEND,
			S3C2410_UDC_USB_INT_REG);

	udc_write(0x1F, S3C2410_UDC_EP_INT_REG);

	/* Good bye, cruel world */
	s3c2410_udc_command(S3C2410_UDC_P_DISABLE);

	/* Set speed to unknown */
	dev->gadget.speed = USB_SPEED_UNKNOWN;
}

/*
 * s3c2410_udc_reinit
 */
static void s3c2410_udc_reinit(struct s3c2410_udc *dev)
{
	u32 i;

	/* device/ep0 records init */
	INIT_LIST_HEAD(&dev->gadget.ep_list);
	INIT_LIST_HEAD(&dev->gadget.ep0->ep_list);
	dev->ep0state = EP0_IDLE;

	for (i = 0; i < S3C2410_ENDPOINTS; i++) {
		struct s3c2410_ep *ep = &dev->ep[i];

		if (i != 0)
			list_add_tail(&ep->ep.ep_list, &dev->gadget.ep_list);

		ep->dev = dev;
		ep->ep.desc = NULL;
		ep->halted = 0;
		INIT_LIST_HEAD(&ep->queue);
	}
}

/*
 * s3c2410_udc_enable
 */
static void s3c2410_udc_enable(struct s3c2410_udc *dev)
{
	int i;

	dprintk(DEBUG_NORMAL, "s3c2410_udc_enable called\n");

	/* dev->gadget.speed = USB_SPEED_UNKNOWN; */
	dev->gadget.speed = USB_SPEED_FULL;

	/* Set MAXP for all endpoints */
	for (i = 0; i < S3C2410_ENDPOINTS; i++) {
		udc_write(i, S3C2410_UDC_INDEX_REG);
		udc_write((dev->ep[i].ep.maxpacket & 0x7ff) >> 3,
				S3C2410_UDC_MAXP_REG);
	}

	/* Set default power state */
	udc_write(DEFAULT_POWER_STATE, S3C2410_UDC_PWR_REG);

	/* Enable reset and suspend interrupt interrupts */
	udc_write(S3C2410_UDC_USBINT_RESET | S3C2410_UDC_USBINT_SUSPEND,
			S3C2410_UDC_USB_INT_EN_REG);

	/* Enable ep0 interrupt */
	udc_write(S3C2410_UDC_INT_EP0, S3C2410_UDC_EP_INT_EN_REG);

	/* time to say "hello, world" */
	s3c2410_udc_command(S3C2410_UDC_P_ENABLE);
}

static int s3c2410_udc_start(struct usb_gadget_driver *driver,
		int (*bind)(struct usb_gadget *, struct usb_gadget_driver *))
{
	struct s3c2410_udc *udc = the_controller;
	int		retval;

	dprintk(DEBUG_NORMAL, "%s() '%s'\n", __func__, driver->driver.name);

	/* Sanity checks */
	if (!udc)
		return -ENODEV;

	if (udc->driver)
		return -EBUSY;

	if (!bind || !driver->setup || driver->max_speed < USB_SPEED_FULL) {
		dev_err(&udc->gadget.dev, "Invalid driver: bind %p setup %p speed %d\n",
			bind, driver->setup, driver->max_speed);
		return -EINVAL;
	}
#if defined(MODULE)
	if (!driver->unbind) {
		dev_err(&udc->gadget.dev, "Invalid driver: no unbind method\n");
		return -EINVAL;
	}
#endif

	/* Hook the driver */
	udc->driver = driver;
	udc->gadget.dev.driver = &driver->driver;

	/* Bind the driver */
	retval = device_add(&udc->gadget.dev);
	if (retval) {
		dev_err(&udc->gadget.dev, "Error in device_add() : %d\n", retval);
		goto register_error;
	}

	dprintk(DEBUG_NORMAL, "binding gadget driver '%s'\n",
		driver->driver.name);

	retval = bind(&udc->gadget, driver);
	if (retval) {
		device_del(&udc->gadget.dev);
		goto register_error;
	}

	/* Enable udc */
	s3c2410_udc_enable(udc);

	return 0;

register_error:
	udc->driver = NULL;
	udc->gadget.dev.driver = NULL;
	return retval;
}

static int s3c2410_udc_stop(struct usb_gadget_driver *driver)
{
	struct s3c2410_udc *udc = the_controller;

	if (!udc)
		return -ENODEV;

	if (!driver || driver != udc->driver || !driver->unbind)
		return -EINVAL;

	dprintk(DEBUG_NORMAL, "usb_gadget_unregister_driver() '%s'\n",
		driver->driver.name);

	/* report disconnect */
	if (driver->disconnect)
		driver->disconnect(&udc->gadget);

	driver->unbind(&udc->gadget);

	device_del(&udc->gadget.dev);
	udc->driver = NULL;

	/* Disable udc */
	s3c2410_udc_disable(udc);

	return 0;
}

/*---------------------------------------------------------------------------*/
static struct s3c2410_udc memory = {
	.gadget = {
		.ops		= &s3c2410_ops,
		.ep0		= &memory.ep[0].ep,
		.name		= gadget_name,
		.dev = {
			.init_name	= "gadget",
		},
	},

	/* control endpoint */
	.ep[0] = {
		.num		= 0,
		.ep = {
			.name		= ep0name,
			.ops		= &s3c2410_ep_ops,
			.maxpacket	= EP0_FIFO_SIZE,
		},
		.dev		= &memory,
	},

	/* first group of endpoints */
	.ep[1] = {
		.num		= 1,
		.ep = {
			.name		= "ep1-bulk",
			.ops		= &s3c2410_ep_ops,
			.maxpacket	= EP_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= EP_FIFO_SIZE,
		.bEndpointAddress = 1,
		.bmAttributes	= USB_ENDPOINT_XFER_BULK,
	},
	.ep[2] = {
		.num		= 2,
		.ep = {
			.name		= "ep2-bulk",
			.ops		= &s3c2410_ep_ops,
			.maxpacket	= EP_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= EP_FIFO_SIZE,
		.bEndpointAddress = 2,
		.bmAttributes	= USB_ENDPOINT_XFER_BULK,
	},
	.ep[3] = {
		.num		= 3,
		.ep = {
			.name		= "ep3-bulk",
			.ops		= &s3c2410_ep_ops,
			.maxpacket	= EP_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= EP_FIFO_SIZE,
		.bEndpointAddress = 3,
		.bmAttributes	= USB_ENDPOINT_XFER_BULK,
	},
	.ep[4] = {
		.num		= 4,
		.ep = {
			.name		= "ep4-bulk",
			.ops		= &s3c2410_ep_ops,
			.maxpacket	= EP_FIFO_SIZE,
		},
		.dev		= &memory,
		.fifo_size	= EP_FIFO_SIZE,
		.bEndpointAddress = 4,
		.bmAttributes	= USB_ENDPOINT_XFER_BULK,
	}

};

/*
 *	probe - binds to the platform device
 */
static int s3c2410_udc_probe(struct platform_device *pdev)
{
	struct s3c2410_udc *udc = &memory;
	struct device *dev = &pdev->dev;
	int retval;
	int irq;

	dev_dbg(dev, "%s()\n", __func__);

	usb_bus_clock = clk_get(NULL, "usb-bus-gadget");
	if (IS_ERR(usb_bus_clock)) {
		dev_err(dev, "failed to get usb bus clock source\n");
		return PTR_ERR(usb_bus_clock);
	}

	clk_enable(usb_bus_clock);

	udc_clock = clk_get(NULL, "usb-device");
	if (IS_ERR(udc_clock)) {
		dev_err(dev, "failed to get udc clock source\n");
		return PTR_ERR(udc_clock);
	}

	clk_enable(udc_clock);

	mdelay(10);

	dev_dbg(dev, "got and enabled clocks\n");

	if (strncmp(pdev->name, "s3c2440", 7) == 0) {
		dev_info(dev, "S3C2440: increasing FIFO to 128 bytes\n");
		memory.ep[1].fifo_size = S3C2440_EP_FIFO_SIZE;
		memory.ep[2].fifo_size = S3C2440_EP_FIFO_SIZE;
		memory.ep[3].fifo_size = S3C2440_EP_FIFO_SIZE;
		memory.ep[4].fifo_size = S3C2440_EP_FIFO_SIZE;
	}

	spin_lock_init(&udc->lock);
	udc_info = pdev->dev.platform_data;

	rsrc_start = S3C2410_PA_USBDEV;
	rsrc_len   = S3C24XX_SZ_USBDEV;

	if (!request_mem_region(rsrc_start, rsrc_len, gadget_name))
		return -EBUSY;

	base_addr = ioremap(rsrc_start, rsrc_len);
	if (!base_addr) {
		retval = -ENOMEM;
		goto err_mem;
	}

	device_initialize(&udc->gadget.dev);
	udc->gadget.dev.parent = &pdev->dev;
	udc->gadget.dev.dma_mask = pdev->dev.dma_mask;

	the_controller = udc;
	platform_set_drvdata(pdev, udc);

	s3c2410_udc_disable(udc);
	s3c2410_udc_reinit(udc);

	/* irq setup after old hardware state is cleaned up */
	retval = request_irq(IRQ_USBD, s3c2410_udc_irq,
			     0, gadget_name, udc);

	if (retval != 0) {
		dev_err(dev, "cannot get irq %i, err %d\n", IRQ_USBD, retval);
		retval = -EBUSY;
		goto err_map;
	}

	dev_dbg(dev, "got irq %i\n", IRQ_USBD);

	if (udc_info && udc_info->vbus_pin > 0) {
		retval = gpio_request(udc_info->vbus_pin, "udc vbus");
		if (retval < 0) {
			dev_err(dev, "cannot claim vbus pin\n");
			goto err_int;
		}

		irq = gpio_to_irq(udc_info->vbus_pin);
		if (irq < 0) {
			dev_err(dev, "no irq for gpio vbus pin\n");
			goto err_gpio_claim;
		}

		retval = request_irq(irq, s3c2410_udc_vbus_irq,
				     IRQF_TRIGGER_RISING
				     | IRQF_TRIGGER_FALLING | IRQF_SHARED,
				     gadget_name, udc);

		if (retval != 0) {
			dev_err(dev, "can't get vbus irq %d, err %d\n",
				irq, retval);
			retval = -EBUSY;
			goto err_gpio_claim;
		}

		dev_dbg(dev, "got irq %i\n", irq);
	} else {
		udc->vbus = 1;
	}

	if (udc_info && !udc_info->udc_command &&
		gpio_is_valid(udc_info->pullup_pin)) {

		retval = gpio_request_one(udc_info->pullup_pin,
				udc_info->vbus_pin_inverted ?
				GPIOF_OUT_INIT_HIGH : GPIOF_OUT_INIT_LOW,
				"udc pullup");
		if (retval)
			goto err_vbus_irq;
	}

	retval = usb_add_gadget_udc(&pdev->dev, &udc->gadget);
	if (retval)
		goto err_add_udc;

	if (s3c2410_udc_debugfs_root) {
		udc->regs_info = debugfs_create_file("registers", S_IRUGO,
				s3c2410_udc_debugfs_root,
				udc, &s3c2410_udc_debugfs_fops);
		if (!udc->regs_info)
			dev_warn(dev, "debugfs file creation failed\n");
	}

	dev_dbg(dev, "probe ok\n");

	return 0;

err_add_udc:
	if (udc_info && !udc_info->udc_command &&
			gpio_is_valid(udc_info->pullup_pin))
		gpio_free(udc_info->pullup_pin);
err_vbus_irq:
	if (udc_info && udc_info->vbus_pin > 0)
		free_irq(gpio_to_irq(udc_info->vbus_pin), udc);
err_gpio_claim:
	if (udc_info && udc_info->vbus_pin > 0)
		gpio_free(udc_info->vbus_pin);
err_int:
	free_irq(IRQ_USBD, udc);
err_map:
	iounmap(base_addr);
err_mem:
	release_mem_region(rsrc_start, rsrc_len);

	return retval;
}

/*
 *	s3c2410_udc_remove
 */
static int s3c2410_udc_remove(struct platform_device *pdev)
{
	struct s3c2410_udc *udc = platform_get_drvdata(pdev);
	unsigned int irq;

	dev_dbg(&pdev->dev, "%s()\n", __func__);

	usb_del_gadget_udc(&udc->gadget);
	if (udc->driver)
		return -EBUSY;

	debugfs_remove(udc->regs_info);

	if (udc_info && !udc_info->udc_command &&
		gpio_is_valid(udc_info->pullup_pin))
		gpio_free(udc_info->pullup_pin);

	if (udc_info && udc_info->vbus_pin > 0) {
		irq = gpio_to_irq(udc_info->vbus_pin);
		free_irq(irq, udc);
	}

	free_irq(IRQ_USBD, udc);

	iounmap(base_addr);
	release_mem_region(rsrc_start, rsrc_len);

	platform_set_drvdata(pdev, NULL);

	if (!IS_ERR(udc_clock) && udc_clock != NULL) {
		clk_disable(udc_clock);
		clk_put(udc_clock);
		udc_clock = NULL;
	}

	if (!IS_ERR(usb_bus_clock) && usb_bus_clock != NULL) {
		clk_disable(usb_bus_clock);
		clk_put(usb_bus_clock);
		usb_bus_clock = NULL;
	}

	dev_dbg(&pdev->dev, "%s: remove ok\n", __func__);
	return 0;
}

#ifdef CONFIG_PM
static int
s3c2410_udc_suspend(struct platform_device *pdev, pm_message_t message)
{
	s3c2410_udc_command(S3C2410_UDC_P_DISABLE);

	return 0;
}

static int s3c2410_udc_resume(struct platform_device *pdev)
{
	s3c2410_udc_command(S3C2410_UDC_P_ENABLE);

	return 0;
}
#else
#define s3c2410_udc_suspend	NULL
#define s3c2410_udc_resume	NULL
#endif

static const struct platform_device_id s3c_udc_ids[] = {
	{ "s3c2410-usbgadget", },
	{ "s3c2440-usbgadget", },
	{ }
};
MODULE_DEVICE_TABLE(platform, s3c_udc_ids);

static struct platform_driver udc_driver_24x0 = {
	.driver		= {
		.name	= "s3c24x0-usbgadget",
		.owner	= THIS_MODULE,
	},
	.probe		= s3c2410_udc_probe,
	.remove		= s3c2410_udc_remove,
	.suspend	= s3c2410_udc_suspend,
	.resume		= s3c2410_udc_resume,
	.id_table	= s3c_udc_ids,
};

static int __init udc_init(void)
{
	int retval;

	dprintk(DEBUG_NORMAL, "%s: version %s\n", gadget_name, DRIVER_VERSION);

	s3c2410_udc_debugfs_root = debugfs_create_dir(gadget_name, NULL);
	if (IS_ERR(s3c2410_udc_debugfs_root)) {
		pr_err("%s: debugfs dir creation failed %ld\n",
			gadget_name, PTR_ERR(s3c2410_udc_debugfs_root));
		s3c2410_udc_debugfs_root = NULL;
	}

	retval = platform_driver_register(&udc_driver_24x0);
	if (retval)
		goto err;

	return 0;

err:
	debugfs_remove(s3c2410_udc_debugfs_root);
	return retval;
}

static void __exit udc_exit(void)
{
	platform_driver_unregister(&udc_driver_24x0);
	debugfs_remove(s3c2410_udc_debugfs_root);
}

module_init(udc_init);
module_exit(udc_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
