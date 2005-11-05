/*
 * linux/drivers/usb/gadget/lh7a40x_udc.c
 * Sharp LH7A40x on-chip full speed USB device controllers
 *
 * Copyright (C) 2004 Mikko Lahteenmaki, Nordic ID
 * Copyright (C) 2004 Bo Henriksen, Nordic ID
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/platform_device.h>

#include "lh7a40x_udc.h"

//#define DEBUG printk
//#define DEBUG_EP0 printk
//#define DEBUG_SETUP printk

#ifndef DEBUG_EP0
# define DEBUG_EP0(fmt,args...)
#endif
#ifndef DEBUG_SETUP
# define DEBUG_SETUP(fmt,args...)
#endif
#ifndef DEBUG
# define NO_STATES
# define DEBUG(fmt,args...)
#endif

#define	DRIVER_DESC			"LH7A40x USB Device Controller"
#define	DRIVER_VERSION		__DATE__

#ifndef _BIT			/* FIXME - what happended to _BIT in 2.6.7bk18? */
#define _BIT(x) (1<<(x))
#endif

struct lh7a40x_udc *the_controller;

static const char driver_name[] = "lh7a40x_udc";
static const char driver_desc[] = DRIVER_DESC;
static const char ep0name[] = "ep0-control";

/*
  Local definintions.
*/

#ifndef NO_STATES
static char *state_names[] = {
	"WAIT_FOR_SETUP",
	"DATA_STATE_XMIT",
	"DATA_STATE_NEED_ZLP",
	"WAIT_FOR_OUT_STATUS",
	"DATA_STATE_RECV"
};
#endif

/*
  Local declarations.
*/
static int lh7a40x_ep_enable(struct usb_ep *ep,
			     const struct usb_endpoint_descriptor *);
static int lh7a40x_ep_disable(struct usb_ep *ep);
static struct usb_request *lh7a40x_alloc_request(struct usb_ep *ep, gfp_t);
static void lh7a40x_free_request(struct usb_ep *ep, struct usb_request *);
static void *lh7a40x_alloc_buffer(struct usb_ep *ep, unsigned, dma_addr_t *,
				  gfp_t);
static void lh7a40x_free_buffer(struct usb_ep *ep, void *, dma_addr_t,
				unsigned);
static int lh7a40x_queue(struct usb_ep *ep, struct usb_request *, gfp_t);
static int lh7a40x_dequeue(struct usb_ep *ep, struct usb_request *);
static int lh7a40x_set_halt(struct usb_ep *ep, int);
static int lh7a40x_fifo_status(struct usb_ep *ep);
static int lh7a40x_fifo_status(struct usb_ep *ep);
static void lh7a40x_fifo_flush(struct usb_ep *ep);
static void lh7a40x_ep0_kick(struct lh7a40x_udc *dev, struct lh7a40x_ep *ep);
static void lh7a40x_handle_ep0(struct lh7a40x_udc *dev, u32 intr);

static void done(struct lh7a40x_ep *ep, struct lh7a40x_request *req,
		 int status);
static void pio_irq_enable(int bEndpointAddress);
static void pio_irq_disable(int bEndpointAddress);
static void stop_activity(struct lh7a40x_udc *dev,
			  struct usb_gadget_driver *driver);
static void flush(struct lh7a40x_ep *ep);
static void udc_enable(struct lh7a40x_udc *dev);
static void udc_set_address(struct lh7a40x_udc *dev, unsigned char address);

static struct usb_ep_ops lh7a40x_ep_ops = {
	.enable = lh7a40x_ep_enable,
	.disable = lh7a40x_ep_disable,

	.alloc_request = lh7a40x_alloc_request,
	.free_request = lh7a40x_free_request,

	.alloc_buffer = lh7a40x_alloc_buffer,
	.free_buffer = lh7a40x_free_buffer,

	.queue = lh7a40x_queue,
	.dequeue = lh7a40x_dequeue,

	.set_halt = lh7a40x_set_halt,
	.fifo_status = lh7a40x_fifo_status,
	.fifo_flush = lh7a40x_fifo_flush,
};

/* Inline code */

static __inline__ int write_packet(struct lh7a40x_ep *ep,
				   struct lh7a40x_request *req, int max)
{
	u8 *buf;
	int length, count;
	volatile u32 *fifo = (volatile u32 *)ep->fifo;

	buf = req->req.buf + req->req.actual;
	prefetch(buf);

	length = req->req.length - req->req.actual;
	length = min(length, max);
	req->req.actual += length;

	DEBUG("Write %d (max %d), fifo %p\n", length, max, fifo);

	count = length;
	while (count--) {
		*fifo = *buf++;
	}

	return length;
}

static __inline__ void usb_set_index(u32 ep)
{
	*(volatile u32 *)io_p2v(USB_INDEX) = ep;
}

static __inline__ u32 usb_read(u32 port)
{
	return *(volatile u32 *)io_p2v(port);
}

static __inline__ void usb_write(u32 val, u32 port)
{
	*(volatile u32 *)io_p2v(port) = val;
}

static __inline__ void usb_set(u32 val, u32 port)
{
	volatile u32 *ioport = (volatile u32 *)io_p2v(port);
	u32 after = (*ioport) | val;
	*ioport = after;
}

static __inline__ void usb_clear(u32 val, u32 port)
{
	volatile u32 *ioport = (volatile u32 *)io_p2v(port);
	u32 after = (*ioport) & ~val;
	*ioport = after;
}

/*-------------------------------------------------------------------------*/

#define GPIO_PORTC_DR 	(0x80000E08)
#define GPIO_PORTC_DDR 	(0x80000E18)
#define GPIO_PORTC_PDR 	(0x80000E70)

/* get port C pin data register */
#define get_portc_pdr(bit) 		((usb_read(GPIO_PORTC_PDR) & _BIT(bit)) != 0)
/* get port C data direction register */
#define get_portc_ddr(bit) 		((usb_read(GPIO_PORTC_DDR) & _BIT(bit)) != 0)
/* set port C data register */
#define set_portc_dr(bit, val) 	(val ? usb_set(_BIT(bit), GPIO_PORTC_DR) : usb_clear(_BIT(bit), GPIO_PORTC_DR))
/* set port C data direction register */
#define set_portc_ddr(bit, val) (val ? usb_set(_BIT(bit), GPIO_PORTC_DDR) : usb_clear(_BIT(bit), GPIO_PORTC_DDR))

/*
 * LPD7A404 GPIO's:
 * Port C bit 1 = USB Port 1 Power Enable
 * Port C bit 2 = USB Port 1 Data Carrier Detect
 */
#define is_usb_connected() 		get_portc_pdr(2)

#ifdef CONFIG_USB_GADGET_DEBUG_FILES

static const char proc_node_name[] = "driver/udc";

static int
udc_proc_read(char *page, char **start, off_t off, int count,
	      int *eof, void *_dev)
{
	char *buf = page;
	struct lh7a40x_udc *dev = _dev;
	char *next = buf;
	unsigned size = count;
	unsigned long flags;
	int t;

	if (off != 0)
		return 0;

	local_irq_save(flags);

	/* basic device status */
	t = scnprintf(next, size,
		      DRIVER_DESC "\n"
		      "%s version: %s\n"
		      "Gadget driver: %s\n"
		      "Host: %s\n\n",
		      driver_name, DRIVER_VERSION,
		      dev->driver ? dev->driver->driver.name : "(none)",
		      is_usb_connected()? "full speed" : "disconnected");
	size -= t;
	next += t;

	t = scnprintf(next, size,
		      "GPIO:\n"
		      " Port C bit 1: %d, dir %d\n"
		      " Port C bit 2: %d, dir %d\n\n",
		      get_portc_pdr(1), get_portc_ddr(1),
		      get_portc_pdr(2), get_portc_ddr(2)
	    );
	size -= t;
	next += t;

	t = scnprintf(next, size,
		      "DCP pullup: %d\n\n",
		      (usb_read(USB_PM) & PM_USB_DCP) != 0);
	size -= t;
	next += t;

	local_irq_restore(flags);
	*eof = 1;
	return count - size;
}

#define create_proc_files() 	create_proc_read_entry(proc_node_name, 0, NULL, udc_proc_read, dev)
#define remove_proc_files() 	remove_proc_entry(proc_node_name, NULL)

#else	/* !CONFIG_USB_GADGET_DEBUG_FILES */

#define create_proc_files() do {} while (0)
#define remove_proc_files() do {} while (0)

#endif	/* CONFIG_USB_GADGET_DEBUG_FILES */

/*
 * 	udc_disable - disable USB device controller
 */
static void udc_disable(struct lh7a40x_udc *dev)
{
	DEBUG("%s, %p\n", __FUNCTION__, dev);

	udc_set_address(dev, 0);

	/* Disable interrupts */
	usb_write(0, USB_IN_INT_EN);
	usb_write(0, USB_OUT_INT_EN);
	usb_write(0, USB_INT_EN);

	/* Disable the USB */
	usb_write(0, USB_PM);

#ifdef CONFIG_ARCH_LH7A404
	/* Disable USB power */
	set_portc_dr(1, 0);
#endif

	/* if hardware supports it, disconnect from usb */
	/* make_usb_disappear(); */

	dev->ep0state = WAIT_FOR_SETUP;
	dev->gadget.speed = USB_SPEED_UNKNOWN;
	dev->usb_address = 0;
}

/*
 * 	udc_reinit - initialize software state
 */
static void udc_reinit(struct lh7a40x_udc *dev)
{
	u32 i;

	DEBUG("%s, %p\n", __FUNCTION__, dev);

	/* device/ep0 records init */
	INIT_LIST_HEAD(&dev->gadget.ep_list);
	INIT_LIST_HEAD(&dev->gadget.ep0->ep_list);
	dev->ep0state = WAIT_FOR_SETUP;

	/* basic endpoint records init */
	for (i = 0; i < UDC_MAX_ENDPOINTS; i++) {
		struct lh7a40x_ep *ep = &dev->ep[i];

		if (i != 0)
			list_add_tail(&ep->ep.ep_list, &dev->gadget.ep_list);

		ep->desc = 0;
		ep->stopped = 0;
		INIT_LIST_HEAD(&ep->queue);
		ep->pio_irqs = 0;
	}

	/* the rest was statically initialized, and is read-only */
}

#define BYTES2MAXP(x)	(x / 8)
#define MAXP2BYTES(x)	(x * 8)

/* until it's enabled, this UDC should be completely invisible
 * to any USB host.
 */
static void udc_enable(struct lh7a40x_udc *dev)
{
	int ep;

	DEBUG("%s, %p\n", __FUNCTION__, dev);

	dev->gadget.speed = USB_SPEED_UNKNOWN;

#ifdef CONFIG_ARCH_LH7A404
	/* Set Port C bit 1 & 2 as output */
	set_portc_ddr(1, 1);
	set_portc_ddr(2, 1);

	/* Enable USB power */
	set_portc_dr(1, 0);
#endif

	/*
	 * C.f Chapter 18.1.3.1 Initializing the USB
	 */

	/* Disable the USB */
	usb_clear(PM_USB_ENABLE, USB_PM);

	/* Reset APB & I/O sides of the USB */
	usb_set(USB_RESET_APB | USB_RESET_IO, USB_RESET);
	mdelay(5);
	usb_clear(USB_RESET_APB | USB_RESET_IO, USB_RESET);

	/* Set MAXP values for each */
	for (ep = 0; ep < UDC_MAX_ENDPOINTS; ep++) {
		struct lh7a40x_ep *ep_reg = &dev->ep[ep];
		u32 csr;

		usb_set_index(ep);

		switch (ep_reg->ep_type) {
		case ep_bulk_in:
		case ep_interrupt:
			usb_clear(USB_IN_CSR2_USB_DMA_EN | USB_IN_CSR2_AUTO_SET,
				  ep_reg->csr2);
			/* Fall through */
		case ep_control:
			usb_write(BYTES2MAXP(ep_maxpacket(ep_reg)),
				  USB_IN_MAXP);
			break;
		case ep_bulk_out:
			usb_clear(USB_OUT_CSR2_USB_DMA_EN |
				  USB_OUT_CSR2_AUTO_CLR, ep_reg->csr2);
			usb_write(BYTES2MAXP(ep_maxpacket(ep_reg)),
				  USB_OUT_MAXP);
			break;
		}

		/* Read & Write CSR1, just in case */
		csr = usb_read(ep_reg->csr1);
		usb_write(csr, ep_reg->csr1);

		flush(ep_reg);
	}

	/* Disable interrupts */
	usb_write(0, USB_IN_INT_EN);
	usb_write(0, USB_OUT_INT_EN);
	usb_write(0, USB_INT_EN);

	/* Enable interrupts */
	usb_set(USB_IN_INT_EP0, USB_IN_INT_EN);
	usb_set(USB_INT_RESET_INT | USB_INT_RESUME_INT, USB_INT_EN);
	/* Dont enable rest of the interrupts */
	/* usb_set(USB_IN_INT_EP3 | USB_IN_INT_EP1 | USB_IN_INT_EP0, USB_IN_INT_EN);
	   usb_set(USB_OUT_INT_EP2, USB_OUT_INT_EN); */

	/* Enable SUSPEND */
	usb_set(PM_ENABLE_SUSPEND, USB_PM);

	/* Enable the USB */
	usb_set(PM_USB_ENABLE, USB_PM);

#ifdef CONFIG_ARCH_LH7A404
	/* NOTE: DOES NOT WORK! */
	/* Let host detect UDC:
	 * Software must write a 0 to the PMR:DCP_CTRL bit to turn this
	 * transistor on and pull the USBDP pin HIGH.
	 */
	/* usb_clear(PM_USB_DCP, USB_PM);
	   usb_set(PM_USB_DCP, USB_PM); */
#endif
}

/*
  Register entry point for the peripheral controller driver.
*/
int usb_gadget_register_driver(struct usb_gadget_driver *driver)
{
	struct lh7a40x_udc *dev = the_controller;
	int retval;

	DEBUG("%s: %s\n", __FUNCTION__, driver->driver.name);

	if (!driver
	    || driver->speed != USB_SPEED_FULL
	    || !driver->bind
	    || !driver->unbind || !driver->disconnect || !driver->setup)
		return -EINVAL;
	if (!dev)
		return -ENODEV;
	if (dev->driver)
		return -EBUSY;

	/* first hook up the driver ... */
	dev->driver = driver;
	dev->gadget.dev.driver = &driver->driver;

	device_add(&dev->gadget.dev);
	retval = driver->bind(&dev->gadget);
	if (retval) {
		printk("%s: bind to driver %s --> error %d\n", dev->gadget.name,
		       driver->driver.name, retval);
		device_del(&dev->gadget.dev);

		dev->driver = 0;
		dev->gadget.dev.driver = 0;
		return retval;
	}

	/* ... then enable host detection and ep0; and we're ready
	 * for set_configuration as well as eventual disconnect.
	 * NOTE:  this shouldn't power up until later.
	 */
	printk("%s: registered gadget driver '%s'\n", dev->gadget.name,
	       driver->driver.name);

	udc_enable(dev);

	return 0;
}

EXPORT_SYMBOL(usb_gadget_register_driver);

/*
  Unregister entry point for the peripheral controller driver.
*/
int usb_gadget_unregister_driver(struct usb_gadget_driver *driver)
{
	struct lh7a40x_udc *dev = the_controller;
	unsigned long flags;

	if (!dev)
		return -ENODEV;
	if (!driver || driver != dev->driver)
		return -EINVAL;

	spin_lock_irqsave(&dev->lock, flags);
	dev->driver = 0;
	stop_activity(dev, driver);
	spin_unlock_irqrestore(&dev->lock, flags);

	driver->unbind(&dev->gadget);
	device_del(&dev->gadget.dev);

	udc_disable(dev);

	DEBUG("unregistered gadget driver '%s'\n", driver->driver.name);
	return 0;
}

EXPORT_SYMBOL(usb_gadget_unregister_driver);

/*-------------------------------------------------------------------------*/

/** Write request to FIFO (max write == maxp size)
 *  Return:  0 = still running, 1 = completed, negative = errno
 *  NOTE: INDEX register must be set for EP
 */
static int write_fifo(struct lh7a40x_ep *ep, struct lh7a40x_request *req)
{
	u32 max;
	u32 csr;

	max = le16_to_cpu(ep->desc->wMaxPacketSize);

	csr = usb_read(ep->csr1);
	DEBUG("CSR: %x %d\n", csr, csr & USB_IN_CSR1_FIFO_NOT_EMPTY);

	if (!(csr & USB_IN_CSR1_FIFO_NOT_EMPTY)) {
		unsigned count;
		int is_last, is_short;

		count = write_packet(ep, req, max);
		usb_set(USB_IN_CSR1_IN_PKT_RDY, ep->csr1);

		/* last packet is usually short (or a zlp) */
		if (unlikely(count != max))
			is_last = is_short = 1;
		else {
			if (likely(req->req.length != req->req.actual)
			    || req->req.zero)
				is_last = 0;
			else
				is_last = 1;
			/* interrupt/iso maxpacket may not fill the fifo */
			is_short = unlikely(max < ep_maxpacket(ep));
		}

		DEBUG("%s: wrote %s %d bytes%s%s %d left %p\n", __FUNCTION__,
		      ep->ep.name, count,
		      is_last ? "/L" : "", is_short ? "/S" : "",
		      req->req.length - req->req.actual, req);

		/* requests complete when all IN data is in the FIFO */
		if (is_last) {
			done(ep, req, 0);
			if (list_empty(&ep->queue)) {
				pio_irq_disable(ep_index(ep));
			}
			return 1;
		}
	} else {
		DEBUG("Hmm.. %d ep FIFO is not empty!\n", ep_index(ep));
	}

	return 0;
}

/** Read to request from FIFO (max read == bytes in fifo)
 *  Return:  0 = still running, 1 = completed, negative = errno
 *  NOTE: INDEX register must be set for EP
 */
static int read_fifo(struct lh7a40x_ep *ep, struct lh7a40x_request *req)
{
	u32 csr;
	u8 *buf;
	unsigned bufferspace, count, is_short;
	volatile u32 *fifo = (volatile u32 *)ep->fifo;

	/* make sure there's a packet in the FIFO. */
	csr = usb_read(ep->csr1);
	if (!(csr & USB_OUT_CSR1_OUT_PKT_RDY)) {
		DEBUG("%s: Packet NOT ready!\n", __FUNCTION__);
		return -EINVAL;
	}

	buf = req->req.buf + req->req.actual;
	prefetchw(buf);
	bufferspace = req->req.length - req->req.actual;

	/* read all bytes from this packet */
	count = usb_read(USB_OUT_FIFO_WC1);
	req->req.actual += min(count, bufferspace);

	is_short = (count < ep->ep.maxpacket);
	DEBUG("read %s %02x, %d bytes%s req %p %d/%d\n",
	      ep->ep.name, csr, count,
	      is_short ? "/S" : "", req, req->req.actual, req->req.length);

	while (likely(count-- != 0)) {
		u8 byte = (u8) (*fifo & 0xff);

		if (unlikely(bufferspace == 0)) {
			/* this happens when the driver's buffer
			 * is smaller than what the host sent.
			 * discard the extra data.
			 */
			if (req->req.status != -EOVERFLOW)
				printk("%s overflow %d\n", ep->ep.name, count);
			req->req.status = -EOVERFLOW;
		} else {
			*buf++ = byte;
			bufferspace--;
		}
	}

	usb_clear(USB_OUT_CSR1_OUT_PKT_RDY, ep->csr1);

	/* completion */
	if (is_short || req->req.actual == req->req.length) {
		done(ep, req, 0);
		usb_set(USB_OUT_CSR1_FIFO_FLUSH, ep->csr1);

		if (list_empty(&ep->queue))
			pio_irq_disable(ep_index(ep));
		return 1;
	}

	/* finished that packet.  the next one may be waiting... */
	return 0;
}

/*
 *	done - retire a request; caller blocked irqs
 *  INDEX register is preserved to keep same
 */
static void done(struct lh7a40x_ep *ep, struct lh7a40x_request *req, int status)
{
	unsigned int stopped = ep->stopped;
	u32 index;

	DEBUG("%s, %p\n", __FUNCTION__, ep);
	list_del_init(&req->queue);

	if (likely(req->req.status == -EINPROGRESS))
		req->req.status = status;
	else
		status = req->req.status;

	if (status && status != -ESHUTDOWN)
		DEBUG("complete %s req %p stat %d len %u/%u\n",
		      ep->ep.name, &req->req, status,
		      req->req.actual, req->req.length);

	/* don't modify queue heads during completion callback */
	ep->stopped = 1;
	/* Read current index (completion may modify it) */
	index = usb_read(USB_INDEX);

	spin_unlock(&ep->dev->lock);
	req->req.complete(&ep->ep, &req->req);
	spin_lock(&ep->dev->lock);

	/* Restore index */
	usb_set_index(index);
	ep->stopped = stopped;
}

/** Enable EP interrupt */
static void pio_irq_enable(int ep)
{
	DEBUG("%s: %d\n", __FUNCTION__, ep);

	switch (ep) {
	case 1:
		usb_set(USB_IN_INT_EP1, USB_IN_INT_EN);
		break;
	case 2:
		usb_set(USB_OUT_INT_EP2, USB_OUT_INT_EN);
		break;
	case 3:
		usb_set(USB_IN_INT_EP3, USB_IN_INT_EN);
		break;
	default:
		DEBUG("Unknown endpoint: %d\n", ep);
		break;
	}
}

/** Disable EP interrupt */
static void pio_irq_disable(int ep)
{
	DEBUG("%s: %d\n", __FUNCTION__, ep);

	switch (ep) {
	case 1:
		usb_clear(USB_IN_INT_EP1, USB_IN_INT_EN);
		break;
	case 2:
		usb_clear(USB_OUT_INT_EP2, USB_OUT_INT_EN);
		break;
	case 3:
		usb_clear(USB_IN_INT_EP3, USB_IN_INT_EN);
		break;
	default:
		DEBUG("Unknown endpoint: %d\n", ep);
		break;
	}
}

/*
 * 	nuke - dequeue ALL requests
 */
void nuke(struct lh7a40x_ep *ep, int status)
{
	struct lh7a40x_request *req;

	DEBUG("%s, %p\n", __FUNCTION__, ep);

	/* Flush FIFO */
	flush(ep);

	/* called with irqs blocked */
	while (!list_empty(&ep->queue)) {
		req = list_entry(ep->queue.next, struct lh7a40x_request, queue);
		done(ep, req, status);
	}

	/* Disable IRQ if EP is enabled (has descriptor) */
	if (ep->desc)
		pio_irq_disable(ep_index(ep));
}

/*
void nuke_all(struct lh7a40x_udc *dev)
{
	int n;
	for(n=0; n<UDC_MAX_ENDPOINTS; n++) {
		struct lh7a40x_ep *ep = &dev->ep[n];
		usb_set_index(n);
		nuke(ep, 0);
	}
}*/

/*
static void flush_all(struct lh7a40x_udc *dev)
{
	int n;
    for (n = 0; n < UDC_MAX_ENDPOINTS; n++)
    {
		struct lh7a40x_ep *ep = &dev->ep[n];
		flush(ep);
    }
}
*/

/** Flush EP
 * NOTE: INDEX register must be set before this call
 */
static void flush(struct lh7a40x_ep *ep)
{
	DEBUG("%s, %p\n", __FUNCTION__, ep);

	switch (ep->ep_type) {
	case ep_control:
		/* check, by implication c.f. 15.1.2.11 */
		break;

	case ep_bulk_in:
	case ep_interrupt:
		/* if(csr & USB_IN_CSR1_IN_PKT_RDY) */
		usb_set(USB_IN_CSR1_FIFO_FLUSH, ep->csr1);
		break;

	case ep_bulk_out:
		/* if(csr & USB_OUT_CSR1_OUT_PKT_RDY) */
		usb_set(USB_OUT_CSR1_FIFO_FLUSH, ep->csr1);
		break;
	}
}

/**
 * lh7a40x_in_epn - handle IN interrupt
 */
static void lh7a40x_in_epn(struct lh7a40x_udc *dev, u32 ep_idx, u32 intr)
{
	u32 csr;
	struct lh7a40x_ep *ep = &dev->ep[ep_idx];
	struct lh7a40x_request *req;

	usb_set_index(ep_idx);

	csr = usb_read(ep->csr1);
	DEBUG("%s: %d, csr %x\n", __FUNCTION__, ep_idx, csr);

	if (csr & USB_IN_CSR1_SENT_STALL) {
		DEBUG("USB_IN_CSR1_SENT_STALL\n");
		usb_set(USB_IN_CSR1_SENT_STALL /*|USB_IN_CSR1_SEND_STALL */ ,
			ep->csr1);
		return;
	}

	if (!ep->desc) {
		DEBUG("%s: NO EP DESC\n", __FUNCTION__);
		return;
	}

	if (list_empty(&ep->queue))
		req = 0;
	else
		req = list_entry(ep->queue.next, struct lh7a40x_request, queue);

	DEBUG("req: %p\n", req);

	if (!req)
		return;

	write_fifo(ep, req);
}

/* ********************************************************************************************* */
/* Bulk OUT (recv)
 */

static void lh7a40x_out_epn(struct lh7a40x_udc *dev, u32 ep_idx, u32 intr)
{
	struct lh7a40x_ep *ep = &dev->ep[ep_idx];
	struct lh7a40x_request *req;

	DEBUG("%s: %d\n", __FUNCTION__, ep_idx);

	usb_set_index(ep_idx);

	if (ep->desc) {
		u32 csr;
		csr = usb_read(ep->csr1);

		while ((csr =
			usb_read(ep->
				 csr1)) & (USB_OUT_CSR1_OUT_PKT_RDY |
					   USB_OUT_CSR1_SENT_STALL)) {
			DEBUG("%s: %x\n", __FUNCTION__, csr);

			if (csr & USB_OUT_CSR1_SENT_STALL) {
				DEBUG("%s: stall sent, flush fifo\n",
				      __FUNCTION__);
				/* usb_set(USB_OUT_CSR1_FIFO_FLUSH, ep->csr1); */
				flush(ep);
			} else if (csr & USB_OUT_CSR1_OUT_PKT_RDY) {
				if (list_empty(&ep->queue))
					req = 0;
				else
					req =
					    list_entry(ep->queue.next,
						       struct lh7a40x_request,
						       queue);

				if (!req) {
					printk("%s: NULL REQ %d\n",
					       __FUNCTION__, ep_idx);
					flush(ep);
					break;
				} else {
					read_fifo(ep, req);
				}
			}

		}

	} else {
		/* Throw packet away.. */
		printk("%s: No descriptor?!?\n", __FUNCTION__);
		flush(ep);
	}
}

static void stop_activity(struct lh7a40x_udc *dev,
			  struct usb_gadget_driver *driver)
{
	int i;

	/* don't disconnect drivers more than once */
	if (dev->gadget.speed == USB_SPEED_UNKNOWN)
		driver = 0;
	dev->gadget.speed = USB_SPEED_UNKNOWN;

	/* prevent new request submissions, kill any outstanding requests  */
	for (i = 0; i < UDC_MAX_ENDPOINTS; i++) {
		struct lh7a40x_ep *ep = &dev->ep[i];
		ep->stopped = 1;

		usb_set_index(i);
		nuke(ep, -ESHUTDOWN);
	}

	/* report disconnect; the driver is already quiesced */
	if (driver) {
		spin_unlock(&dev->lock);
		driver->disconnect(&dev->gadget);
		spin_lock(&dev->lock);
	}

	/* re-init driver-visible data structures */
	udc_reinit(dev);
}

/** Handle USB RESET interrupt
 */
static void lh7a40x_reset_intr(struct lh7a40x_udc *dev)
{
#if 0				/* def CONFIG_ARCH_LH7A404 */
	/* Does not work always... */

	DEBUG("%s: %d\n", __FUNCTION__, dev->usb_address);

	if (!dev->usb_address) {
		/*usb_set(USB_RESET_IO, USB_RESET);
		   mdelay(5);
		   usb_clear(USB_RESET_IO, USB_RESET); */
		return;
	}
	/* Put the USB controller into reset. */
	usb_set(USB_RESET_IO, USB_RESET);

	/* Set Device ID to 0 */
	udc_set_address(dev, 0);

	/* Let PLL2 settle down */
	mdelay(5);

	/* Release the USB controller from reset */
	usb_clear(USB_RESET_IO, USB_RESET);

	/* Re-enable UDC */
	udc_enable(dev);

#endif
	dev->gadget.speed = USB_SPEED_FULL;
}

/*
 *	lh7a40x usb client interrupt handler.
 */
static irqreturn_t lh7a40x_udc_irq(int irq, void *_dev, struct pt_regs *r)
{
	struct lh7a40x_udc *dev = _dev;

	DEBUG("\n\n");

	spin_lock(&dev->lock);

	for (;;) {
		u32 intr_in = usb_read(USB_IN_INT);
		u32 intr_out = usb_read(USB_OUT_INT);
		u32 intr_int = usb_read(USB_INT);

		/* Test also against enable bits.. (lh7a40x errata).. Sigh.. */
		u32 in_en = usb_read(USB_IN_INT_EN);
		u32 out_en = usb_read(USB_OUT_INT_EN);

		if (!intr_out && !intr_in && !intr_int)
			break;

		DEBUG("%s (on state %s)\n", __FUNCTION__,
		      state_names[dev->ep0state]);
		DEBUG("intr_out = %x\n", intr_out);
		DEBUG("intr_in  = %x\n", intr_in);
		DEBUG("intr_int = %x\n", intr_int);

		if (intr_in) {
			usb_write(intr_in, USB_IN_INT);

			if ((intr_in & USB_IN_INT_EP1)
			    && (in_en & USB_IN_INT_EP1)) {
				DEBUG("USB_IN_INT_EP1\n");
				lh7a40x_in_epn(dev, 1, intr_in);
			}
			if ((intr_in & USB_IN_INT_EP3)
			    && (in_en & USB_IN_INT_EP3)) {
				DEBUG("USB_IN_INT_EP3\n");
				lh7a40x_in_epn(dev, 3, intr_in);
			}
			if (intr_in & USB_IN_INT_EP0) {
				DEBUG("USB_IN_INT_EP0 (control)\n");
				lh7a40x_handle_ep0(dev, intr_in);
			}
		}

		if (intr_out) {
			usb_write(intr_out, USB_OUT_INT);

			if ((intr_out & USB_OUT_INT_EP2)
			    && (out_en & USB_OUT_INT_EP2)) {
				DEBUG("USB_OUT_INT_EP2\n");
				lh7a40x_out_epn(dev, 2, intr_out);
			}
		}

		if (intr_int) {
			usb_write(intr_int, USB_INT);

			if (intr_int & USB_INT_RESET_INT) {
				lh7a40x_reset_intr(dev);
			}

			if (intr_int & USB_INT_RESUME_INT) {
				DEBUG("USB resume\n");

				if (dev->gadget.speed != USB_SPEED_UNKNOWN
				    && dev->driver
				    && dev->driver->resume
				    && is_usb_connected()) {
					dev->driver->resume(&dev->gadget);
				}
			}

			if (intr_int & USB_INT_SUSPEND_INT) {
				DEBUG("USB suspend%s\n",
				      is_usb_connected()? "" : "+disconnect");
				if (!is_usb_connected()) {
					stop_activity(dev, dev->driver);
				} else if (dev->gadget.speed !=
					   USB_SPEED_UNKNOWN && dev->driver
					   && dev->driver->suspend) {
					dev->driver->suspend(&dev->gadget);
				}
			}

		}
	}

	spin_unlock(&dev->lock);

	return IRQ_HANDLED;
}

static int lh7a40x_ep_enable(struct usb_ep *_ep,
			     const struct usb_endpoint_descriptor *desc)
{
	struct lh7a40x_ep *ep;
	struct lh7a40x_udc *dev;
	unsigned long flags;

	DEBUG("%s, %p\n", __FUNCTION__, _ep);

	ep = container_of(_ep, struct lh7a40x_ep, ep);
	if (!_ep || !desc || ep->desc || _ep->name == ep0name
	    || desc->bDescriptorType != USB_DT_ENDPOINT
	    || ep->bEndpointAddress != desc->bEndpointAddress
	    || ep_maxpacket(ep) < le16_to_cpu(desc->wMaxPacketSize)) {
		DEBUG("%s, bad ep or descriptor\n", __FUNCTION__);
		return -EINVAL;
	}

	/* xfer types must match, except that interrupt ~= bulk */
	if (ep->bmAttributes != desc->bmAttributes
	    && ep->bmAttributes != USB_ENDPOINT_XFER_BULK
	    && desc->bmAttributes != USB_ENDPOINT_XFER_INT) {
		DEBUG("%s, %s type mismatch\n", __FUNCTION__, _ep->name);
		return -EINVAL;
	}

	/* hardware _could_ do smaller, but driver doesn't */
	if ((desc->bmAttributes == USB_ENDPOINT_XFER_BULK
	     && le16_to_cpu(desc->wMaxPacketSize) != ep_maxpacket(ep))
	    || !desc->wMaxPacketSize) {
		DEBUG("%s, bad %s maxpacket\n", __FUNCTION__, _ep->name);
		return -ERANGE;
	}

	dev = ep->dev;
	if (!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN) {
		DEBUG("%s, bogus device state\n", __FUNCTION__);
		return -ESHUTDOWN;
	}

	spin_lock_irqsave(&ep->dev->lock, flags);

	ep->stopped = 0;
	ep->desc = desc;
	ep->pio_irqs = 0;
	ep->ep.maxpacket = le16_to_cpu(desc->wMaxPacketSize);

	/* Reset halt state (does flush) */
	lh7a40x_set_halt(_ep, 0);

	spin_unlock_irqrestore(&ep->dev->lock, flags);

	DEBUG("%s: enabled %s\n", __FUNCTION__, _ep->name);
	return 0;
}

/** Disable EP
 *  NOTE: Sets INDEX register
 */
static int lh7a40x_ep_disable(struct usb_ep *_ep)
{
	struct lh7a40x_ep *ep;
	unsigned long flags;

	DEBUG("%s, %p\n", __FUNCTION__, _ep);

	ep = container_of(_ep, struct lh7a40x_ep, ep);
	if (!_ep || !ep->desc) {
		DEBUG("%s, %s not enabled\n", __FUNCTION__,
		      _ep ? ep->ep.name : NULL);
		return -EINVAL;
	}

	spin_lock_irqsave(&ep->dev->lock, flags);

	usb_set_index(ep_index(ep));

	/* Nuke all pending requests (does flush) */
	nuke(ep, -ESHUTDOWN);

	/* Disable ep IRQ */
	pio_irq_disable(ep_index(ep));

	ep->desc = 0;
	ep->stopped = 1;

	spin_unlock_irqrestore(&ep->dev->lock, flags);

	DEBUG("%s: disabled %s\n", __FUNCTION__, _ep->name);
	return 0;
}

static struct usb_request *lh7a40x_alloc_request(struct usb_ep *ep,
						 gfp_t gfp_flags)
{
	struct lh7a40x_request *req;

	DEBUG("%s, %p\n", __FUNCTION__, ep);

	req = kmalloc(sizeof *req, gfp_flags);
	if (!req)
		return 0;

	memset(req, 0, sizeof *req);
	INIT_LIST_HEAD(&req->queue);

	return &req->req;
}

static void lh7a40x_free_request(struct usb_ep *ep, struct usb_request *_req)
{
	struct lh7a40x_request *req;

	DEBUG("%s, %p\n", __FUNCTION__, ep);

	req = container_of(_req, struct lh7a40x_request, req);
	WARN_ON(!list_empty(&req->queue));
	kfree(req);
}

static void *lh7a40x_alloc_buffer(struct usb_ep *ep, unsigned bytes,
				  dma_addr_t * dma, gfp_t gfp_flags)
{
	char *retval;

	DEBUG("%s (%p, %d, %d)\n", __FUNCTION__, ep, bytes, gfp_flags);

	retval = kmalloc(bytes, gfp_flags & ~(__GFP_DMA | __GFP_HIGHMEM));
	if (retval)
		*dma = virt_to_bus(retval);
	return retval;
}

static void lh7a40x_free_buffer(struct usb_ep *ep, void *buf, dma_addr_t dma,
				unsigned bytes)
{
	DEBUG("%s, %p\n", __FUNCTION__, ep);
	kfree(buf);
}

/** Queue one request
 *  Kickstart transfer if needed
 *  NOTE: Sets INDEX register
 */
static int lh7a40x_queue(struct usb_ep *_ep, struct usb_request *_req,
			 gfp_t gfp_flags)
{
	struct lh7a40x_request *req;
	struct lh7a40x_ep *ep;
	struct lh7a40x_udc *dev;
	unsigned long flags;

	DEBUG("\n\n\n%s, %p\n", __FUNCTION__, _ep);

	req = container_of(_req, struct lh7a40x_request, req);
	if (unlikely
	    (!_req || !_req->complete || !_req->buf
	     || !list_empty(&req->queue))) {
		DEBUG("%s, bad params\n", __FUNCTION__);
		return -EINVAL;
	}

	ep = container_of(_ep, struct lh7a40x_ep, ep);
	if (unlikely(!_ep || (!ep->desc && ep->ep.name != ep0name))) {
		DEBUG("%s, bad ep\n", __FUNCTION__);
		return -EINVAL;
	}

	dev = ep->dev;
	if (unlikely(!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN)) {
		DEBUG("%s, bogus device state %p\n", __FUNCTION__, dev->driver);
		return -ESHUTDOWN;
	}

	DEBUG("%s queue req %p, len %d buf %p\n", _ep->name, _req, _req->length,
	      _req->buf);

	spin_lock_irqsave(&dev->lock, flags);

	_req->status = -EINPROGRESS;
	_req->actual = 0;

	/* kickstart this i/o queue? */
	DEBUG("Add to %d Q %d %d\n", ep_index(ep), list_empty(&ep->queue),
	      ep->stopped);
	if (list_empty(&ep->queue) && likely(!ep->stopped)) {
		u32 csr;

		if (unlikely(ep_index(ep) == 0)) {
			/* EP0 */
			list_add_tail(&req->queue, &ep->queue);
			lh7a40x_ep0_kick(dev, ep);
			req = 0;
		} else if (ep_is_in(ep)) {
			/* EP1 & EP3 */
			usb_set_index(ep_index(ep));
			csr = usb_read(ep->csr1);
			pio_irq_enable(ep_index(ep));
			if ((csr & USB_IN_CSR1_FIFO_NOT_EMPTY) == 0) {
				if (write_fifo(ep, req) == 1)
					req = 0;
			}
		} else {
			/* EP2 */
			usb_set_index(ep_index(ep));
			csr = usb_read(ep->csr1);
			pio_irq_enable(ep_index(ep));
			if (!(csr & USB_OUT_CSR1_FIFO_FULL)) {
				if (read_fifo(ep, req) == 1)
					req = 0;
			}
		}
	}

	/* pio or dma irq handler advances the queue. */
	if (likely(req != 0))
		list_add_tail(&req->queue, &ep->queue);

	spin_unlock_irqrestore(&dev->lock, flags);

	return 0;
}

/* dequeue JUST ONE request */
static int lh7a40x_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct lh7a40x_ep *ep;
	struct lh7a40x_request *req;
	unsigned long flags;

	DEBUG("%s, %p\n", __FUNCTION__, _ep);

	ep = container_of(_ep, struct lh7a40x_ep, ep);
	if (!_ep || ep->ep.name == ep0name)
		return -EINVAL;

	spin_lock_irqsave(&ep->dev->lock, flags);

	/* make sure it's actually queued on this endpoint */
	list_for_each_entry(req, &ep->queue, queue) {
		if (&req->req == _req)
			break;
	}
	if (&req->req != _req) {
		spin_unlock_irqrestore(&ep->dev->lock, flags);
		return -EINVAL;
	}

	done(ep, req, -ECONNRESET);

	spin_unlock_irqrestore(&ep->dev->lock, flags);
	return 0;
}

/** Halt specific EP
 *  Return 0 if success
 *  NOTE: Sets INDEX register to EP !
 */
static int lh7a40x_set_halt(struct usb_ep *_ep, int value)
{
	struct lh7a40x_ep *ep;
	unsigned long flags;

	ep = container_of(_ep, struct lh7a40x_ep, ep);
	if (unlikely(!_ep || (!ep->desc && ep->ep.name != ep0name))) {
		DEBUG("%s, bad ep\n", __FUNCTION__);
		return -EINVAL;
	}

	usb_set_index(ep_index(ep));

	DEBUG("%s, ep %d, val %d\n", __FUNCTION__, ep_index(ep), value);

	spin_lock_irqsave(&ep->dev->lock, flags);

	if (ep_index(ep) == 0) {
		/* EP0 */
		usb_set(EP0_SEND_STALL, ep->csr1);
	} else if (ep_is_in(ep)) {
		u32 csr = usb_read(ep->csr1);
		if (value && ((csr & USB_IN_CSR1_FIFO_NOT_EMPTY)
			      || !list_empty(&ep->queue))) {
			/*
			 * Attempts to halt IN endpoints will fail (returning -EAGAIN)
			 * if any transfer requests are still queued, or if the controller
			 * FIFO still holds bytes that the host hasn’t collected.
			 */
			spin_unlock_irqrestore(&ep->dev->lock, flags);
			DEBUG
			    ("Attempt to halt IN endpoint failed (returning -EAGAIN) %d %d\n",
			     (csr & USB_IN_CSR1_FIFO_NOT_EMPTY),
			     !list_empty(&ep->queue));
			return -EAGAIN;
		}
		flush(ep);
		if (value)
			usb_set(USB_IN_CSR1_SEND_STALL, ep->csr1);
		else {
			usb_clear(USB_IN_CSR1_SEND_STALL, ep->csr1);
			usb_set(USB_IN_CSR1_CLR_DATA_TOGGLE, ep->csr1);
		}

	} else {

		flush(ep);
		if (value)
			usb_set(USB_OUT_CSR1_SEND_STALL, ep->csr1);
		else {
			usb_clear(USB_OUT_CSR1_SEND_STALL, ep->csr1);
			usb_set(USB_OUT_CSR1_CLR_DATA_REG, ep->csr1);
		}
	}

	if (value) {
		ep->stopped = 1;
	} else {
		ep->stopped = 0;
	}

	spin_unlock_irqrestore(&ep->dev->lock, flags);

	DEBUG("%s %s halted\n", _ep->name, value == 0 ? "NOT" : "IS");

	return 0;
}

/** Return bytes in EP FIFO
 *  NOTE: Sets INDEX register to EP
 */
static int lh7a40x_fifo_status(struct usb_ep *_ep)
{
	u32 csr;
	int count = 0;
	struct lh7a40x_ep *ep;

	ep = container_of(_ep, struct lh7a40x_ep, ep);
	if (!_ep) {
		DEBUG("%s, bad ep\n", __FUNCTION__);
		return -ENODEV;
	}

	DEBUG("%s, %d\n", __FUNCTION__, ep_index(ep));

	/* LPD can't report unclaimed bytes from IN fifos */
	if (ep_is_in(ep))
		return -EOPNOTSUPP;

	usb_set_index(ep_index(ep));

	csr = usb_read(ep->csr1);
	if (ep->dev->gadget.speed != USB_SPEED_UNKNOWN ||
	    csr & USB_OUT_CSR1_OUT_PKT_RDY) {
		count = usb_read(USB_OUT_FIFO_WC1);
	}

	return count;
}

/** Flush EP FIFO
 *  NOTE: Sets INDEX register to EP
 */
static void lh7a40x_fifo_flush(struct usb_ep *_ep)
{
	struct lh7a40x_ep *ep;

	ep = container_of(_ep, struct lh7a40x_ep, ep);
	if (unlikely(!_ep || (!ep->desc && ep->ep.name != ep0name))) {
		DEBUG("%s, bad ep\n", __FUNCTION__);
		return;
	}

	usb_set_index(ep_index(ep));
	flush(ep);
}

/****************************************************************/
/* End Point 0 related functions                                */
/****************************************************************/

/* return:  0 = still running, 1 = completed, negative = errno */
static int write_fifo_ep0(struct lh7a40x_ep *ep, struct lh7a40x_request *req)
{
	u32 max;
	unsigned count;
	int is_last;

	max = ep_maxpacket(ep);

	DEBUG_EP0("%s\n", __FUNCTION__);

	count = write_packet(ep, req, max);

	/* last packet is usually short (or a zlp) */
	if (unlikely(count != max))
		is_last = 1;
	else {
		if (likely(req->req.length != req->req.actual) || req->req.zero)
			is_last = 0;
		else
			is_last = 1;
	}

	DEBUG_EP0("%s: wrote %s %d bytes%s %d left %p\n", __FUNCTION__,
		  ep->ep.name, count,
		  is_last ? "/L" : "", req->req.length - req->req.actual, req);

	/* requests complete when all IN data is in the FIFO */
	if (is_last) {
		done(ep, req, 0);
		return 1;
	}

	return 0;
}

static __inline__ int lh7a40x_fifo_read(struct lh7a40x_ep *ep,
					unsigned char *cp, int max)
{
	int bytes;
	int count = usb_read(USB_OUT_FIFO_WC1);
	volatile u32 *fifo = (volatile u32 *)ep->fifo;

	if (count > max)
		count = max;
	bytes = count;
	while (count--)
		*cp++ = *fifo & 0xFF;
	return bytes;
}

static __inline__ void lh7a40x_fifo_write(struct lh7a40x_ep *ep,
					  unsigned char *cp, int count)
{
	volatile u32 *fifo = (volatile u32 *)ep->fifo;
	DEBUG_EP0("fifo_write: %d %d\n", ep_index(ep), count);
	while (count--)
		*fifo = *cp++;
}

static int read_fifo_ep0(struct lh7a40x_ep *ep, struct lh7a40x_request *req)
{
	u32 csr;
	u8 *buf;
	unsigned bufferspace, count, is_short;
	volatile u32 *fifo = (volatile u32 *)ep->fifo;

	DEBUG_EP0("%s\n", __FUNCTION__);

	csr = usb_read(USB_EP0_CSR);
	if (!(csr & USB_OUT_CSR1_OUT_PKT_RDY))
		return 0;

	buf = req->req.buf + req->req.actual;
	prefetchw(buf);
	bufferspace = req->req.length - req->req.actual;

	/* read all bytes from this packet */
	if (likely(csr & EP0_OUT_PKT_RDY)) {
		count = usb_read(USB_OUT_FIFO_WC1);
		req->req.actual += min(count, bufferspace);
	} else			/* zlp */
		count = 0;

	is_short = (count < ep->ep.maxpacket);
	DEBUG_EP0("read %s %02x, %d bytes%s req %p %d/%d\n",
		  ep->ep.name, csr, count,
		  is_short ? "/S" : "", req, req->req.actual, req->req.length);

	while (likely(count-- != 0)) {
		u8 byte = (u8) (*fifo & 0xff);

		if (unlikely(bufferspace == 0)) {
			/* this happens when the driver's buffer
			 * is smaller than what the host sent.
			 * discard the extra data.
			 */
			if (req->req.status != -EOVERFLOW)
				DEBUG_EP0("%s overflow %d\n", ep->ep.name,
					  count);
			req->req.status = -EOVERFLOW;
		} else {
			*buf++ = byte;
			bufferspace--;
		}
	}

	/* completion */
	if (is_short || req->req.actual == req->req.length) {
		done(ep, req, 0);
		return 1;
	}

	/* finished that packet.  the next one may be waiting... */
	return 0;
}

/**
 * udc_set_address - set the USB address for this device
 * @address:
 *
 * Called from control endpoint function after it decodes a set address setup packet.
 */
static void udc_set_address(struct lh7a40x_udc *dev, unsigned char address)
{
	DEBUG_EP0("%s: %d\n", __FUNCTION__, address);
	/* c.f. 15.1.2.2 Table 15-4 address will be used after DATA_END is set */
	dev->usb_address = address;
	usb_set((address & USB_FA_FUNCTION_ADDR), USB_FA);
	usb_set(USB_FA_ADDR_UPDATE | (address & USB_FA_FUNCTION_ADDR), USB_FA);
	/* usb_read(USB_FA); */
}

/*
 * DATA_STATE_RECV (OUT_PKT_RDY)
 *      - if error
 *              set EP0_CLR_OUT | EP0_DATA_END | EP0_SEND_STALL bits
 *      - else
 *              set EP0_CLR_OUT bit
 				if last set EP0_DATA_END bit
 */
static void lh7a40x_ep0_out(struct lh7a40x_udc *dev, u32 csr)
{
	struct lh7a40x_request *req;
	struct lh7a40x_ep *ep = &dev->ep[0];
	int ret;

	DEBUG_EP0("%s: %x\n", __FUNCTION__, csr);

	if (list_empty(&ep->queue))
		req = 0;
	else
		req = list_entry(ep->queue.next, struct lh7a40x_request, queue);

	if (req) {

		if (req->req.length == 0) {
			DEBUG_EP0("ZERO LENGTH OUT!\n");
			usb_set((EP0_CLR_OUT | EP0_DATA_END), USB_EP0_CSR);
			dev->ep0state = WAIT_FOR_SETUP;
			return;
		}
		ret = read_fifo_ep0(ep, req);
		if (ret) {
			/* Done! */
			DEBUG_EP0("%s: finished, waiting for status\n",
				  __FUNCTION__);

			usb_set((EP0_CLR_OUT | EP0_DATA_END), USB_EP0_CSR);
			dev->ep0state = WAIT_FOR_SETUP;
		} else {
			/* Not done yet.. */
			DEBUG_EP0("%s: not finished\n", __FUNCTION__);
			usb_set(EP0_CLR_OUT, USB_EP0_CSR);
		}
	} else {
		DEBUG_EP0("NO REQ??!\n");
	}
}

/*
 * DATA_STATE_XMIT
 */
static int lh7a40x_ep0_in(struct lh7a40x_udc *dev, u32 csr)
{
	struct lh7a40x_request *req;
	struct lh7a40x_ep *ep = &dev->ep[0];
	int ret, need_zlp = 0;

	DEBUG_EP0("%s: %x\n", __FUNCTION__, csr);

	if (list_empty(&ep->queue))
		req = 0;
	else
		req = list_entry(ep->queue.next, struct lh7a40x_request, queue);

	if (!req) {
		DEBUG_EP0("%s: NULL REQ\n", __FUNCTION__);
		return 0;
	}

	if (req->req.length == 0) {

		usb_set((EP0_IN_PKT_RDY | EP0_DATA_END), USB_EP0_CSR);
		dev->ep0state = WAIT_FOR_SETUP;
		return 1;
	}

	if (req->req.length - req->req.actual == EP0_PACKETSIZE) {
		/* Next write will end with the packet size, */
		/* so we need Zero-length-packet */
		need_zlp = 1;
	}

	ret = write_fifo_ep0(ep, req);

	if (ret == 1 && !need_zlp) {
		/* Last packet */
		DEBUG_EP0("%s: finished, waiting for status\n", __FUNCTION__);

		usb_set((EP0_IN_PKT_RDY | EP0_DATA_END), USB_EP0_CSR);
		dev->ep0state = WAIT_FOR_SETUP;
	} else {
		DEBUG_EP0("%s: not finished\n", __FUNCTION__);
		usb_set(EP0_IN_PKT_RDY, USB_EP0_CSR);
	}

	if (need_zlp) {
		DEBUG_EP0("%s: Need ZLP!\n", __FUNCTION__);
		usb_set(EP0_IN_PKT_RDY, USB_EP0_CSR);
		dev->ep0state = DATA_STATE_NEED_ZLP;
	}

	return 1;
}

static int lh7a40x_handle_get_status(struct lh7a40x_udc *dev,
				     struct usb_ctrlrequest *ctrl)
{
	struct lh7a40x_ep *ep0 = &dev->ep[0];
	struct lh7a40x_ep *qep;
	int reqtype = (ctrl->bRequestType & USB_RECIP_MASK);
	u16 val = 0;

	if (reqtype == USB_RECIP_INTERFACE) {
		/* This is not supported.
		 * And according to the USB spec, this one does nothing..
		 * Just return 0
		 */
		DEBUG_SETUP("GET_STATUS: USB_RECIP_INTERFACE\n");
	} else if (reqtype == USB_RECIP_DEVICE) {
		DEBUG_SETUP("GET_STATUS: USB_RECIP_DEVICE\n");
		val |= (1 << 0);	/* Self powered */
		/*val |= (1<<1); *//* Remote wakeup */
	} else if (reqtype == USB_RECIP_ENDPOINT) {
		int ep_num = (ctrl->wIndex & ~USB_DIR_IN);

		DEBUG_SETUP
		    ("GET_STATUS: USB_RECIP_ENDPOINT (%d), ctrl->wLength = %d\n",
		     ep_num, ctrl->wLength);

		if (ctrl->wLength > 2 || ep_num > 3)
			return -EOPNOTSUPP;

		qep = &dev->ep[ep_num];
		if (ep_is_in(qep) != ((ctrl->wIndex & USB_DIR_IN) ? 1 : 0)
		    && ep_index(qep) != 0) {
			return -EOPNOTSUPP;
		}

		usb_set_index(ep_index(qep));

		/* Return status on next IN token */
		switch (qep->ep_type) {
		case ep_control:
			val =
			    (usb_read(qep->csr1) & EP0_SEND_STALL) ==
			    EP0_SEND_STALL;
			break;
		case ep_bulk_in:
		case ep_interrupt:
			val =
			    (usb_read(qep->csr1) & USB_IN_CSR1_SEND_STALL) ==
			    USB_IN_CSR1_SEND_STALL;
			break;
		case ep_bulk_out:
			val =
			    (usb_read(qep->csr1) & USB_OUT_CSR1_SEND_STALL) ==
			    USB_OUT_CSR1_SEND_STALL;
			break;
		}

		/* Back to EP0 index */
		usb_set_index(0);

		DEBUG_SETUP("GET_STATUS, ep: %d (%x), val = %d\n", ep_num,
			    ctrl->wIndex, val);
	} else {
		DEBUG_SETUP("Unknown REQ TYPE: %d\n", reqtype);
		return -EOPNOTSUPP;
	}

	/* Clear "out packet ready" */
	usb_set((EP0_CLR_OUT), USB_EP0_CSR);
	/* Put status to FIFO */
	lh7a40x_fifo_write(ep0, (u8 *) & val, sizeof(val));
	/* Issue "In packet ready" */
	usb_set((EP0_IN_PKT_RDY | EP0_DATA_END), USB_EP0_CSR);

	return 0;
}

/*
 * WAIT_FOR_SETUP (OUT_PKT_RDY)
 *      - read data packet from EP0 FIFO
 *      - decode command
 *      - if error
 *              set EP0_CLR_OUT | EP0_DATA_END | EP0_SEND_STALL bits
 *      - else
 *              set EP0_CLR_OUT | EP0_DATA_END bits
 */
static void lh7a40x_ep0_setup(struct lh7a40x_udc *dev, u32 csr)
{
	struct lh7a40x_ep *ep = &dev->ep[0];
	struct usb_ctrlrequest ctrl;
	int i, bytes, is_in;

	DEBUG_SETUP("%s: %x\n", __FUNCTION__, csr);

	/* Nuke all previous transfers */
	nuke(ep, -EPROTO);

	/* read control req from fifo (8 bytes) */
	bytes = lh7a40x_fifo_read(ep, (unsigned char *)&ctrl, 8);

	DEBUG_SETUP("Read CTRL REQ %d bytes\n", bytes);
	DEBUG_SETUP("CTRL.bRequestType = %d (is_in %d)\n", ctrl.bRequestType,
		    ctrl.bRequestType == USB_DIR_IN);
	DEBUG_SETUP("CTRL.bRequest = %d\n", ctrl.bRequest);
	DEBUG_SETUP("CTRL.wLength = %d\n", ctrl.wLength);
	DEBUG_SETUP("CTRL.wValue = %d (%d)\n", ctrl.wValue, ctrl.wValue >> 8);
	DEBUG_SETUP("CTRL.wIndex = %d\n", ctrl.wIndex);

	/* Set direction of EP0 */
	if (likely(ctrl.bRequestType & USB_DIR_IN)) {
		ep->bEndpointAddress |= USB_DIR_IN;
		is_in = 1;
	} else {
		ep->bEndpointAddress &= ~USB_DIR_IN;
		is_in = 0;
	}

	dev->req_pending = 1;

	/* Handle some SETUP packets ourselves */
	switch (ctrl.bRequest) {
	case USB_REQ_SET_ADDRESS:
		if (ctrl.bRequestType != (USB_TYPE_STANDARD | USB_RECIP_DEVICE))
			break;

		DEBUG_SETUP("USB_REQ_SET_ADDRESS (%d)\n", ctrl.wValue);
		udc_set_address(dev, ctrl.wValue);
		usb_set((EP0_CLR_OUT | EP0_DATA_END), USB_EP0_CSR);
		return;

	case USB_REQ_GET_STATUS:{
			if (lh7a40x_handle_get_status(dev, &ctrl) == 0)
				return;

	case USB_REQ_CLEAR_FEATURE:
	case USB_REQ_SET_FEATURE:
			if (ctrl.bRequestType == USB_RECIP_ENDPOINT) {
				struct lh7a40x_ep *qep;
				int ep_num = (ctrl.wIndex & 0x0f);

				/* Support only HALT feature */
				if (ctrl.wValue != 0 || ctrl.wLength != 0
				    || ep_num > 3 || ep_num < 1)
					break;

				qep = &dev->ep[ep_num];
				if (ctrl.bRequest == USB_REQ_SET_FEATURE) {
					DEBUG_SETUP("SET_FEATURE (%d)\n",
						    ep_num);
					lh7a40x_set_halt(&qep->ep, 1);
				} else {
					DEBUG_SETUP("CLR_FEATURE (%d)\n",
						    ep_num);
					lh7a40x_set_halt(&qep->ep, 0);
				}
				usb_set_index(0);

				/* Reply with a ZLP on next IN token */
				usb_set((EP0_CLR_OUT | EP0_DATA_END),
					USB_EP0_CSR);
				return;
			}
			break;
		}

	default:
		break;
	}

	if (likely(dev->driver)) {
		/* device-2-host (IN) or no data setup command, process immediately */
		spin_unlock(&dev->lock);
		i = dev->driver->setup(&dev->gadget, &ctrl);
		spin_lock(&dev->lock);

		if (i < 0) {
			/* setup processing failed, force stall */
			DEBUG_SETUP
			    ("  --> ERROR: gadget setup FAILED (stalling), setup returned %d\n",
			     i);
			usb_set_index(0);
			usb_set((EP0_CLR_OUT | EP0_DATA_END | EP0_SEND_STALL),
				USB_EP0_CSR);

			/* ep->stopped = 1; */
			dev->ep0state = WAIT_FOR_SETUP;
		}
	}
}

/*
 * DATA_STATE_NEED_ZLP
 */
static void lh7a40x_ep0_in_zlp(struct lh7a40x_udc *dev, u32 csr)
{
	DEBUG_EP0("%s: %x\n", __FUNCTION__, csr);

	/* c.f. Table 15-14 */
	usb_set((EP0_IN_PKT_RDY | EP0_DATA_END), USB_EP0_CSR);
	dev->ep0state = WAIT_FOR_SETUP;
}

/*
 * handle ep0 interrupt
 */
static void lh7a40x_handle_ep0(struct lh7a40x_udc *dev, u32 intr)
{
	struct lh7a40x_ep *ep = &dev->ep[0];
	u32 csr;

	/* Set index 0 */
	usb_set_index(0);
 	csr = usb_read(USB_EP0_CSR);

	DEBUG_EP0("%s: csr = %x\n", __FUNCTION__, csr);

	/*
	 * For overview of what we should be doing see c.f. Chapter 18.1.2.4
	 * We will follow that outline here modified by our own global state
	 * indication which provides hints as to what we think should be
	 * happening..
	 */

	/*
	 * if SENT_STALL is set
	 *      - clear the SENT_STALL bit
	 */
	if (csr & EP0_SENT_STALL) {
		DEBUG_EP0("%s: EP0_SENT_STALL is set: %x\n", __FUNCTION__, csr);
		usb_clear((EP0_SENT_STALL | EP0_SEND_STALL), USB_EP0_CSR);
		nuke(ep, -ECONNABORTED);
		dev->ep0state = WAIT_FOR_SETUP;
		return;
	}

	/*
	 * if a transfer is in progress && IN_PKT_RDY and OUT_PKT_RDY are clear
	 *      - fill EP0 FIFO
	 *      - if last packet
	 *      -       set IN_PKT_RDY | DATA_END
	 *      - else
	 *              set IN_PKT_RDY
	 */
	if (!(csr & (EP0_IN_PKT_RDY | EP0_OUT_PKT_RDY))) {
		DEBUG_EP0("%s: IN_PKT_RDY and OUT_PKT_RDY are clear\n",
			  __FUNCTION__);

		switch (dev->ep0state) {
		case DATA_STATE_XMIT:
			DEBUG_EP0("continue with DATA_STATE_XMIT\n");
			lh7a40x_ep0_in(dev, csr);
			return;
		case DATA_STATE_NEED_ZLP:
			DEBUG_EP0("continue with DATA_STATE_NEED_ZLP\n");
			lh7a40x_ep0_in_zlp(dev, csr);
			return;
		default:
			/* Stall? */
			DEBUG_EP0("Odd state!! state = %s\n",
				  state_names[dev->ep0state]);
			dev->ep0state = WAIT_FOR_SETUP;
			/* nuke(ep, 0); */
			/* usb_set(EP0_SEND_STALL, ep->csr1); */
			break;
		}
	}

	/*
	 * if SETUP_END is set
	 *      - abort the last transfer
	 *      - set SERVICED_SETUP_END_BIT
	 */
	if (csr & EP0_SETUP_END) {
		DEBUG_EP0("%s: EP0_SETUP_END is set: %x\n", __FUNCTION__, csr);

		usb_set(EP0_CLR_SETUP_END, USB_EP0_CSR);

		nuke(ep, 0);
		dev->ep0state = WAIT_FOR_SETUP;
	}

	/*
	 * if EP0_OUT_PKT_RDY is set
	 *      - read data packet from EP0 FIFO
	 *      - decode command
	 *      - if error
	 *              set SERVICED_OUT_PKT_RDY | DATA_END bits | SEND_STALL
	 *      - else
	 *              set SERVICED_OUT_PKT_RDY | DATA_END bits
	 */
	if (csr & EP0_OUT_PKT_RDY) {

		DEBUG_EP0("%s: EP0_OUT_PKT_RDY is set: %x\n", __FUNCTION__,
			  csr);

		switch (dev->ep0state) {
		case WAIT_FOR_SETUP:
			DEBUG_EP0("WAIT_FOR_SETUP\n");
			lh7a40x_ep0_setup(dev, csr);
			break;

		case DATA_STATE_RECV:
			DEBUG_EP0("DATA_STATE_RECV\n");
			lh7a40x_ep0_out(dev, csr);
			break;

		default:
			/* send stall? */
			DEBUG_EP0("strange state!! 2. send stall? state = %d\n",
				  dev->ep0state);
			break;
		}
	}
}

static void lh7a40x_ep0_kick(struct lh7a40x_udc *dev, struct lh7a40x_ep *ep)
{
	u32 csr;

	usb_set_index(0);
	csr = usb_read(USB_EP0_CSR);

	DEBUG_EP0("%s: %x\n", __FUNCTION__, csr);

	/* Clear "out packet ready" */
	usb_set(EP0_CLR_OUT, USB_EP0_CSR);

	if (ep_is_in(ep)) {
		dev->ep0state = DATA_STATE_XMIT;
		lh7a40x_ep0_in(dev, csr);
	} else {
		dev->ep0state = DATA_STATE_RECV;
		lh7a40x_ep0_out(dev, csr);
	}
}

/* ---------------------------------------------------------------------------
 * 	device-scoped parts of the api to the usb controller hardware
 * ---------------------------------------------------------------------------
 */

static int lh7a40x_udc_get_frame(struct usb_gadget *_gadget)
{
	u32 frame1 = usb_read(USB_FRM_NUM1);	/* Least significant 8 bits */
	u32 frame2 = usb_read(USB_FRM_NUM2);	/* Most significant 3 bits */
	DEBUG("%s, %p\n", __FUNCTION__, _gadget);
	return ((frame2 & 0x07) << 8) | (frame1 & 0xff);
}

static int lh7a40x_udc_wakeup(struct usb_gadget *_gadget)
{
	/* host may not have enabled remote wakeup */
	/*if ((UDCCS0 & UDCCS0_DRWF) == 0)
	   return -EHOSTUNREACH;
	   udc_set_mask_UDCCR(UDCCR_RSM); */
	return -ENOTSUPP;
}

static const struct usb_gadget_ops lh7a40x_udc_ops = {
	.get_frame = lh7a40x_udc_get_frame,
	.wakeup = lh7a40x_udc_wakeup,
	/* current versions must always be self-powered */
};

static void nop_release(struct device *dev)
{
	DEBUG("%s %s\n", __FUNCTION__, dev->bus_id);
}

static struct lh7a40x_udc memory = {
	.usb_address = 0,

	.gadget = {
		   .ops = &lh7a40x_udc_ops,
		   .ep0 = &memory.ep[0].ep,
		   .name = driver_name,
		   .dev = {
			   .bus_id = "gadget",
			   .release = nop_release,
			   },
		   },

	/* control endpoint */
	.ep[0] = {
		  .ep = {
			 .name = ep0name,
			 .ops = &lh7a40x_ep_ops,
			 .maxpacket = EP0_PACKETSIZE,
			 },
		  .dev = &memory,

		  .bEndpointAddress = 0,
		  .bmAttributes = 0,

		  .ep_type = ep_control,
		  .fifo = io_p2v(USB_EP0_FIFO),
		  .csr1 = USB_EP0_CSR,
		  .csr2 = USB_EP0_CSR,
		  },

	/* first group of endpoints */
	.ep[1] = {
		  .ep = {
			 .name = "ep1in-bulk",
			 .ops = &lh7a40x_ep_ops,
			 .maxpacket = 64,
			 },
		  .dev = &memory,

		  .bEndpointAddress = USB_DIR_IN | 1,
		  .bmAttributes = USB_ENDPOINT_XFER_BULK,

		  .ep_type = ep_bulk_in,
		  .fifo = io_p2v(USB_EP1_FIFO),
		  .csr1 = USB_IN_CSR1,
		  .csr2 = USB_IN_CSR2,
		  },

	.ep[2] = {
		  .ep = {
			 .name = "ep2out-bulk",
			 .ops = &lh7a40x_ep_ops,
			 .maxpacket = 64,
			 },
		  .dev = &memory,

		  .bEndpointAddress = 2,
		  .bmAttributes = USB_ENDPOINT_XFER_BULK,

		  .ep_type = ep_bulk_out,
		  .fifo = io_p2v(USB_EP2_FIFO),
		  .csr1 = USB_OUT_CSR1,
		  .csr2 = USB_OUT_CSR2,
		  },

	.ep[3] = {
		  .ep = {
			 .name = "ep3in-int",
			 .ops = &lh7a40x_ep_ops,
			 .maxpacket = 64,
			 },
		  .dev = &memory,

		  .bEndpointAddress = USB_DIR_IN | 3,
		  .bmAttributes = USB_ENDPOINT_XFER_INT,

		  .ep_type = ep_interrupt,
		  .fifo = io_p2v(USB_EP3_FIFO),
		  .csr1 = USB_IN_CSR1,
		  .csr2 = USB_IN_CSR2,
		  },
};

/*
 * 	probe - binds to the platform device
 */
static int lh7a40x_udc_probe(struct device *_dev)
{
	struct lh7a40x_udc *dev = &memory;
	int retval;

	DEBUG("%s: %p\n", __FUNCTION__, _dev);

	spin_lock_init(&dev->lock);
	dev->dev = _dev;

	device_initialize(&dev->gadget.dev);
	dev->gadget.dev.parent = _dev;

	the_controller = dev;
	dev_set_drvdata(_dev, dev);

	udc_disable(dev);
	udc_reinit(dev);

	/* irq setup after old hardware state is cleaned up */
	retval =
	    request_irq(IRQ_USBINTR, lh7a40x_udc_irq, SA_INTERRUPT, driver_name,
			dev);
	if (retval != 0) {
		DEBUG(KERN_ERR "%s: can't get irq %i, err %d\n", driver_name,
		      IRQ_USBINTR, retval);
		return -EBUSY;
	}

	create_proc_files();

	return retval;
}

static int lh7a40x_udc_remove(struct device *_dev)
{
	struct lh7a40x_udc *dev = _dev->driver_data;

	DEBUG("%s: %p\n", __FUNCTION__, dev);

	udc_disable(dev);
	remove_proc_files();
	usb_gadget_unregister_driver(dev->driver);

	free_irq(IRQ_USBINTR, dev);

	dev_set_drvdata(_dev, 0);

	the_controller = 0;

	return 0;
}

/*-------------------------------------------------------------------------*/

static struct device_driver udc_driver = {
	.name = (char *)driver_name,
	.owner = THIS_MODULE,
	.bus = &platform_bus_type,
	.probe = lh7a40x_udc_probe,
	.remove = lh7a40x_udc_remove
	    /* FIXME power management support */
	    /* .suspend = ... disable UDC */
	    /* .resume = ... re-enable UDC */
};

static int __init udc_init(void)
{
	DEBUG("%s: %s version %s\n", __FUNCTION__, driver_name, DRIVER_VERSION);
	return driver_register(&udc_driver);
}

static void __exit udc_exit(void)
{
	driver_unregister(&udc_driver);
}

module_init(udc_init);
module_exit(udc_exit);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Mikko Lahteenmaki, Bo Henriksen");
MODULE_LICENSE("GPL");
