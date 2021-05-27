// SPDX-License-Identifier: GPL-2.0+
/*
 * USB Peripheral Controller driver for Aeroflex Gaisler GRUSBDC.
 *
 * 2013 (c) Aeroflex Gaisler AB
 *
 * This driver supports GRUSBDC USB Device Controller cores available in the
 * GRLIB VHDL IP core library.
 *
 * Full documentation of the GRUSBDC core can be found here:
 * https://www.gaisler.com/products/grlib/grip.pdf
 *
 * Contributors:
 * - Andreas Larsson <andreas@gaisler.com>
 * - Marko Isomaki
 */

/*
 * A GRUSBDC core can have up to 16 IN endpoints and 16 OUT endpoints each
 * individually configurable to any of the four USB transfer types. This driver
 * only supports cores in DMA mode.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/usb.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>

#include <asm/byteorder.h>

#include "gr_udc.h"

#define	DRIVER_NAME	"gr_udc"
#define	DRIVER_DESC	"Aeroflex Gaisler GRUSBDC USB Peripheral Controller"

static const char driver_name[] = DRIVER_NAME;

#define gr_read32(x) (ioread32be((x)))
#define gr_write32(x, v) (iowrite32be((v), (x)))

/* USB speed and corresponding string calculated from status register value */
#define GR_SPEED(status) \
	((status & GR_STATUS_SP) ? USB_SPEED_FULL : USB_SPEED_HIGH)
#define GR_SPEED_STR(status) usb_speed_string(GR_SPEED(status))

/* Size of hardware buffer calculated from epctrl register value */
#define GR_BUFFER_SIZE(epctrl)					      \
	((((epctrl) & GR_EPCTRL_BUFSZ_MASK) >> GR_EPCTRL_BUFSZ_POS) * \
	 GR_EPCTRL_BUFSZ_SCALER)

/* ---------------------------------------------------------------------- */
/* Debug printout functionality */

static const char * const gr_modestring[] = {"control", "iso", "bulk", "int"};

static const char *gr_ep0state_string(enum gr_ep0state state)
{
	static const char *const names[] = {
		[GR_EP0_DISCONNECT] = "disconnect",
		[GR_EP0_SETUP] = "setup",
		[GR_EP0_IDATA] = "idata",
		[GR_EP0_ODATA] = "odata",
		[GR_EP0_ISTATUS] = "istatus",
		[GR_EP0_OSTATUS] = "ostatus",
		[GR_EP0_STALL] = "stall",
		[GR_EP0_SUSPEND] = "suspend",
	};

	if (state < 0 || state >= ARRAY_SIZE(names))
		return "UNKNOWN";

	return names[state];
}

#ifdef VERBOSE_DEBUG

static void gr_dbgprint_request(const char *str, struct gr_ep *ep,
				struct gr_request *req)
{
	int buflen = ep->is_in ? req->req.length : req->req.actual;
	int rowlen = 32;
	int plen = min(rowlen, buflen);

	dev_dbg(ep->dev->dev, "%s: 0x%p, %d bytes data%s:\n", str, req, buflen,
		(buflen > plen ? " (truncated)" : ""));
	print_hex_dump_debug("   ", DUMP_PREFIX_NONE,
			     rowlen, 4, req->req.buf, plen, false);
}

static void gr_dbgprint_devreq(struct gr_udc *dev, u8 type, u8 request,
			       u16 value, u16 index, u16 length)
{
	dev_vdbg(dev->dev, "REQ: %02x.%02x v%04x i%04x l%04x\n",
		 type, request, value, index, length);
}
#else /* !VERBOSE_DEBUG */

static void gr_dbgprint_request(const char *str, struct gr_ep *ep,
				struct gr_request *req) {}

static void gr_dbgprint_devreq(struct gr_udc *dev, u8 type, u8 request,
			       u16 value, u16 index, u16 length) {}

#endif /* VERBOSE_DEBUG */

/* ---------------------------------------------------------------------- */
/* Debugfs functionality */

#ifdef CONFIG_USB_GADGET_DEBUG_FS

static void gr_seq_ep_show(struct seq_file *seq, struct gr_ep *ep)
{
	u32 epctrl = gr_read32(&ep->regs->epctrl);
	u32 epstat = gr_read32(&ep->regs->epstat);
	int mode = (epctrl & GR_EPCTRL_TT_MASK) >> GR_EPCTRL_TT_POS;
	struct gr_request *req;

	seq_printf(seq, "%s:\n", ep->ep.name);
	seq_printf(seq, "  mode = %s\n", gr_modestring[mode]);
	seq_printf(seq, "  halted: %d\n", !!(epctrl & GR_EPCTRL_EH));
	seq_printf(seq, "  disabled: %d\n", !!(epctrl & GR_EPCTRL_ED));
	seq_printf(seq, "  valid: %d\n", !!(epctrl & GR_EPCTRL_EV));
	seq_printf(seq, "  dma_start = %d\n", ep->dma_start);
	seq_printf(seq, "  stopped = %d\n", ep->stopped);
	seq_printf(seq, "  wedged = %d\n", ep->wedged);
	seq_printf(seq, "  callback = %d\n", ep->callback);
	seq_printf(seq, "  maxpacket = %d\n", ep->ep.maxpacket);
	seq_printf(seq, "  maxpacket_limit = %d\n", ep->ep.maxpacket_limit);
	seq_printf(seq, "  bytes_per_buffer = %d\n", ep->bytes_per_buffer);
	if (mode == 1 || mode == 3)
		seq_printf(seq, "  nt = %d\n",
			   (epctrl & GR_EPCTRL_NT_MASK) >> GR_EPCTRL_NT_POS);

	seq_printf(seq, "  Buffer 0: %s %s%d\n",
		   epstat & GR_EPSTAT_B0 ? "valid" : "invalid",
		   epstat & GR_EPSTAT_BS ? " " : "selected ",
		   (epstat & GR_EPSTAT_B0CNT_MASK) >> GR_EPSTAT_B0CNT_POS);
	seq_printf(seq, "  Buffer 1: %s %s%d\n",
		   epstat & GR_EPSTAT_B1 ? "valid" : "invalid",
		   epstat & GR_EPSTAT_BS ? "selected " : " ",
		   (epstat & GR_EPSTAT_B1CNT_MASK) >> GR_EPSTAT_B1CNT_POS);

	if (list_empty(&ep->queue)) {
		seq_puts(seq, "  Queue: empty\n\n");
		return;
	}

	seq_puts(seq, "  Queue:\n");
	list_for_each_entry(req, &ep->queue, queue) {
		struct gr_dma_desc *desc;
		struct gr_dma_desc *next;

		seq_printf(seq, "    0x%p: 0x%p %d %d\n", req,
			   &req->req.buf, req->req.actual, req->req.length);

		next = req->first_desc;
		do {
			desc = next;
			next = desc->next_desc;
			seq_printf(seq, "    %c 0x%p (0x%08x): 0x%05x 0x%08x\n",
				   desc == req->curr_desc ? 'c' : ' ',
				   desc, desc->paddr, desc->ctrl, desc->data);
		} while (desc != req->last_desc);
	}
	seq_puts(seq, "\n");
}

static int gr_dfs_show(struct seq_file *seq, void *v)
{
	struct gr_udc *dev = seq->private;
	u32 control = gr_read32(&dev->regs->control);
	u32 status = gr_read32(&dev->regs->status);
	struct gr_ep *ep;

	seq_printf(seq, "usb state = %s\n",
		   usb_state_string(dev->gadget.state));
	seq_printf(seq, "address = %d\n",
		   (control & GR_CONTROL_UA_MASK) >> GR_CONTROL_UA_POS);
	seq_printf(seq, "speed = %s\n", GR_SPEED_STR(status));
	seq_printf(seq, "ep0state = %s\n", gr_ep0state_string(dev->ep0state));
	seq_printf(seq, "irq_enabled = %d\n", dev->irq_enabled);
	seq_printf(seq, "remote_wakeup = %d\n", dev->remote_wakeup);
	seq_printf(seq, "test_mode = %d\n", dev->test_mode);
	seq_puts(seq, "\n");

	list_for_each_entry(ep, &dev->ep_list, ep_list)
		gr_seq_ep_show(seq, ep);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(gr_dfs);

static void gr_dfs_create(struct gr_udc *dev)
{
	const char *name = "gr_udc_state";
	struct dentry *root;

	root = debugfs_create_dir(dev_name(dev->dev), usb_debug_root);
	debugfs_create_file(name, 0444, root, dev, &gr_dfs_fops);
}

static void gr_dfs_delete(struct gr_udc *dev)
{
	debugfs_remove(debugfs_lookup(dev_name(dev->dev), usb_debug_root));
}

#else /* !CONFIG_USB_GADGET_DEBUG_FS */

static void gr_dfs_create(struct gr_udc *dev) {}
static void gr_dfs_delete(struct gr_udc *dev) {}

#endif /* CONFIG_USB_GADGET_DEBUG_FS */

/* ---------------------------------------------------------------------- */
/* DMA and request handling */

/* Allocates a new struct gr_dma_desc, sets paddr and zeroes the rest */
static struct gr_dma_desc *gr_alloc_dma_desc(struct gr_ep *ep, gfp_t gfp_flags)
{
	dma_addr_t paddr;
	struct gr_dma_desc *dma_desc;

	dma_desc = dma_pool_zalloc(ep->dev->desc_pool, gfp_flags, &paddr);
	if (!dma_desc) {
		dev_err(ep->dev->dev, "Could not allocate from DMA pool\n");
		return NULL;
	}

	dma_desc->paddr = paddr;

	return dma_desc;
}

static inline void gr_free_dma_desc(struct gr_udc *dev,
				    struct gr_dma_desc *desc)
{
	dma_pool_free(dev->desc_pool, desc, (dma_addr_t)desc->paddr);
}

/* Frees the chain of struct gr_dma_desc for the given request */
static void gr_free_dma_desc_chain(struct gr_udc *dev, struct gr_request *req)
{
	struct gr_dma_desc *desc;
	struct gr_dma_desc *next;

	next = req->first_desc;
	if (!next)
		return;

	do {
		desc = next;
		next = desc->next_desc;
		gr_free_dma_desc(dev, desc);
	} while (desc != req->last_desc);

	req->first_desc = NULL;
	req->curr_desc = NULL;
	req->last_desc = NULL;
}

static void gr_ep0_setup(struct gr_udc *dev, struct gr_request *req);

/*
 * Frees allocated resources and calls the appropriate completion function/setup
 * package handler for a finished request.
 *
 * Must be called with dev->lock held and irqs disabled.
 */
static void gr_finish_request(struct gr_ep *ep, struct gr_request *req,
			      int status)
	__releases(&dev->lock)
	__acquires(&dev->lock)
{
	struct gr_udc *dev;

	list_del_init(&req->queue);

	if (likely(req->req.status == -EINPROGRESS))
		req->req.status = status;
	else
		status = req->req.status;

	dev = ep->dev;
	usb_gadget_unmap_request(&dev->gadget, &req->req, ep->is_in);
	gr_free_dma_desc_chain(dev, req);

	if (ep->is_in) { /* For OUT, req->req.actual gets updated bit by bit */
		req->req.actual = req->req.length;
	} else if (req->oddlen && req->req.actual > req->evenlen) {
		/*
		 * Copy to user buffer in this case where length was not evenly
		 * divisible by ep->ep.maxpacket and the last descriptor was
		 * actually used.
		 */
		char *buftail = ((char *)req->req.buf + req->evenlen);

		memcpy(buftail, ep->tailbuf, req->oddlen);

		if (req->req.actual > req->req.length) {
			/* We got more data than was requested */
			dev_dbg(ep->dev->dev, "Overflow for ep %s\n",
				ep->ep.name);
			gr_dbgprint_request("OVFL", ep, req);
			req->req.status = -EOVERFLOW;
		}
	}

	if (!status) {
		if (ep->is_in)
			gr_dbgprint_request("SENT", ep, req);
		else
			gr_dbgprint_request("RECV", ep, req);
	}

	/* Prevent changes to ep->queue during callback */
	ep->callback = 1;
	if (req == dev->ep0reqo && !status) {
		if (req->setup)
			gr_ep0_setup(dev, req);
		else
			dev_err(dev->dev,
				"Unexpected non setup packet on ep0in\n");
	} else if (req->req.complete) {
		spin_unlock(&dev->lock);

		usb_gadget_giveback_request(&ep->ep, &req->req);

		spin_lock(&dev->lock);
	}
	ep->callback = 0;
}

static struct usb_request *gr_alloc_request(struct usb_ep *_ep, gfp_t gfp_flags)
{
	struct gr_request *req;

	req = kzalloc(sizeof(*req), gfp_flags);
	if (!req)
		return NULL;

	INIT_LIST_HEAD(&req->queue);

	return &req->req;
}

/*
 * Starts DMA for endpoint ep if there are requests in the queue.
 *
 * Must be called with dev->lock held and with !ep->stopped.
 */
static void gr_start_dma(struct gr_ep *ep)
{
	struct gr_request *req;
	u32 dmactrl;

	if (list_empty(&ep->queue)) {
		ep->dma_start = 0;
		return;
	}

	req = list_first_entry(&ep->queue, struct gr_request, queue);

	/* A descriptor should already have been allocated */
	BUG_ON(!req->curr_desc);

	/*
	 * The DMA controller can not handle smaller OUT buffers than
	 * ep->ep.maxpacket. It could lead to buffer overruns if an unexpectedly
	 * long packet are received. Therefore an internal bounce buffer gets
	 * used when such a request gets enabled.
	 */
	if (!ep->is_in && req->oddlen)
		req->last_desc->data = ep->tailbuf_paddr;

	wmb(); /* Make sure all is settled before handing it over to DMA */

	/* Set the descriptor pointer in the hardware */
	gr_write32(&ep->regs->dmaaddr, req->curr_desc->paddr);

	/* Announce available descriptors */
	dmactrl = gr_read32(&ep->regs->dmactrl);
	gr_write32(&ep->regs->dmactrl, dmactrl | GR_DMACTRL_DA);

	ep->dma_start = 1;
}

/*
 * Finishes the first request in the ep's queue and, if available, starts the
 * next request in queue.
 *
 * Must be called with dev->lock held, irqs disabled and with !ep->stopped.
 */
static void gr_dma_advance(struct gr_ep *ep, int status)
{
	struct gr_request *req;

	req = list_first_entry(&ep->queue, struct gr_request, queue);
	gr_finish_request(ep, req, status);
	gr_start_dma(ep); /* Regardless of ep->dma_start */
}

/*
 * Abort DMA for an endpoint. Sets the abort DMA bit which causes an ongoing DMA
 * transfer to be canceled and clears GR_DMACTRL_DA.
 *
 * Must be called with dev->lock held.
 */
static void gr_abort_dma(struct gr_ep *ep)
{
	u32 dmactrl;

	dmactrl = gr_read32(&ep->regs->dmactrl);
	gr_write32(&ep->regs->dmactrl, dmactrl | GR_DMACTRL_AD);
}

/*
 * Allocates and sets up a struct gr_dma_desc and putting it on the descriptor
 * chain.
 *
 * Size is not used for OUT endpoints. Hardware can not be instructed to handle
 * smaller buffer than MAXPL in the OUT direction.
 */
static int gr_add_dma_desc(struct gr_ep *ep, struct gr_request *req,
			   dma_addr_t data, unsigned size, gfp_t gfp_flags)
{
	struct gr_dma_desc *desc;

	desc = gr_alloc_dma_desc(ep, gfp_flags);
	if (!desc)
		return -ENOMEM;

	desc->data = data;
	if (ep->is_in)
		desc->ctrl =
			(GR_DESC_IN_CTRL_LEN_MASK & size) | GR_DESC_IN_CTRL_EN;
	else
		desc->ctrl = GR_DESC_OUT_CTRL_IE;

	if (!req->first_desc) {
		req->first_desc = desc;
		req->curr_desc = desc;
	} else {
		req->last_desc->next_desc = desc;
		req->last_desc->next = desc->paddr;
		req->last_desc->ctrl |= GR_DESC_OUT_CTRL_NX;
	}
	req->last_desc = desc;

	return 0;
}

/*
 * Sets up a chain of struct gr_dma_descriptors pointing to buffers that
 * together covers req->req.length bytes of the buffer at DMA address
 * req->req.dma for the OUT direction.
 *
 * The first descriptor in the chain is enabled, the rest disabled. The
 * interrupt handler will later enable them one by one when needed so we can
 * find out when the transfer is finished. For OUT endpoints, all descriptors
 * therefore generate interrutps.
 */
static int gr_setup_out_desc_list(struct gr_ep *ep, struct gr_request *req,
				  gfp_t gfp_flags)
{
	u16 bytes_left; /* Bytes left to provide descriptors for */
	u16 bytes_used; /* Bytes accommodated for */
	int ret = 0;

	req->first_desc = NULL; /* Signals that no allocation is done yet */
	bytes_left = req->req.length;
	bytes_used = 0;
	while (bytes_left > 0) {
		dma_addr_t start = req->req.dma + bytes_used;
		u16 size = min(bytes_left, ep->bytes_per_buffer);

		if (size < ep->bytes_per_buffer) {
			/* Prepare using bounce buffer */
			req->evenlen = req->req.length - bytes_left;
			req->oddlen = size;
		}

		ret = gr_add_dma_desc(ep, req, start, size, gfp_flags);
		if (ret)
			goto alloc_err;

		bytes_left -= size;
		bytes_used += size;
	}

	req->first_desc->ctrl |= GR_DESC_OUT_CTRL_EN;

	return 0;

alloc_err:
	gr_free_dma_desc_chain(ep->dev, req);

	return ret;
}

/*
 * Sets up a chain of struct gr_dma_descriptors pointing to buffers that
 * together covers req->req.length bytes of the buffer at DMA address
 * req->req.dma for the IN direction.
 *
 * When more data is provided than the maximum payload size, the hardware splits
 * this up into several payloads automatically. Moreover, ep->bytes_per_buffer
 * is always set to a multiple of the maximum payload (restricted to the valid
 * number of maximum payloads during high bandwidth isochronous or interrupt
 * transfers)
 *
 * All descriptors are enabled from the beginning and we only generate an
 * interrupt for the last one indicating that the entire request has been pushed
 * to hardware.
 */
static int gr_setup_in_desc_list(struct gr_ep *ep, struct gr_request *req,
				 gfp_t gfp_flags)
{
	u16 bytes_left; /* Bytes left in req to provide descriptors for */
	u16 bytes_used; /* Bytes in req accommodated for */
	int ret = 0;

	req->first_desc = NULL; /* Signals that no allocation is done yet */
	bytes_left = req->req.length;
	bytes_used = 0;
	do { /* Allow for zero length packets */
		dma_addr_t start = req->req.dma + bytes_used;
		u16 size = min(bytes_left, ep->bytes_per_buffer);

		ret = gr_add_dma_desc(ep, req, start, size, gfp_flags);
		if (ret)
			goto alloc_err;

		bytes_left -= size;
		bytes_used += size;
	} while (bytes_left > 0);

	/*
	 * Send an extra zero length packet to indicate that no more data is
	 * available when req->req.zero is set and the data length is even
	 * multiples of ep->ep.maxpacket.
	 */
	if (req->req.zero && (req->req.length % ep->ep.maxpacket == 0)) {
		ret = gr_add_dma_desc(ep, req, 0, 0, gfp_flags);
		if (ret)
			goto alloc_err;
	}

	/*
	 * For IN packets we only want to know when the last packet has been
	 * transmitted (not just put into internal buffers).
	 */
	req->last_desc->ctrl |= GR_DESC_IN_CTRL_PI;

	return 0;

alloc_err:
	gr_free_dma_desc_chain(ep->dev, req);

	return ret;
}

/* Must be called with dev->lock held */
static int gr_queue(struct gr_ep *ep, struct gr_request *req, gfp_t gfp_flags)
{
	struct gr_udc *dev = ep->dev;
	int ret;

	if (unlikely(!ep->ep.desc && ep->num != 0)) {
		dev_err(dev->dev, "No ep descriptor for %s\n", ep->ep.name);
		return -EINVAL;
	}

	if (unlikely(!req->req.buf || !list_empty(&req->queue))) {
		dev_err(dev->dev,
			"Invalid request for %s: buf=%p list_empty=%d\n",
			ep->ep.name, req->req.buf, list_empty(&req->queue));
		return -EINVAL;
	}

	if (unlikely(!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN)) {
		dev_err(dev->dev, "-ESHUTDOWN");
		return -ESHUTDOWN;
	}

	/* Can't touch registers when suspended */
	if (dev->ep0state == GR_EP0_SUSPEND) {
		dev_err(dev->dev, "-EBUSY");
		return -EBUSY;
	}

	/* Set up DMA mapping in case the caller didn't */
	ret = usb_gadget_map_request(&dev->gadget, &req->req, ep->is_in);
	if (ret) {
		dev_err(dev->dev, "usb_gadget_map_request");
		return ret;
	}

	if (ep->is_in)
		ret = gr_setup_in_desc_list(ep, req, gfp_flags);
	else
		ret = gr_setup_out_desc_list(ep, req, gfp_flags);
	if (ret)
		return ret;

	req->req.status = -EINPROGRESS;
	req->req.actual = 0;
	list_add_tail(&req->queue, &ep->queue);

	/* Start DMA if not started, otherwise interrupt handler handles it */
	if (!ep->dma_start && likely(!ep->stopped))
		gr_start_dma(ep);

	return 0;
}

/*
 * Queue a request from within the driver.
 *
 * Must be called with dev->lock held.
 */
static inline int gr_queue_int(struct gr_ep *ep, struct gr_request *req,
			       gfp_t gfp_flags)
{
	if (ep->is_in)
		gr_dbgprint_request("RESP", ep, req);

	return gr_queue(ep, req, gfp_flags);
}

/* ---------------------------------------------------------------------- */
/* General helper functions */

/*
 * Dequeue ALL requests.
 *
 * Must be called with dev->lock held and irqs disabled.
 */
static void gr_ep_nuke(struct gr_ep *ep)
{
	struct gr_request *req;

	ep->stopped = 1;
	ep->dma_start = 0;
	gr_abort_dma(ep);

	while (!list_empty(&ep->queue)) {
		req = list_first_entry(&ep->queue, struct gr_request, queue);
		gr_finish_request(ep, req, -ESHUTDOWN);
	}
}

/*
 * Reset the hardware state of this endpoint.
 *
 * Must be called with dev->lock held.
 */
static void gr_ep_reset(struct gr_ep *ep)
{
	gr_write32(&ep->regs->epctrl, 0);
	gr_write32(&ep->regs->dmactrl, 0);

	ep->ep.maxpacket = MAX_CTRL_PL_SIZE;
	ep->ep.desc = NULL;
	ep->stopped = 1;
	ep->dma_start = 0;
}

/*
 * Generate STALL on ep0in/out.
 *
 * Must be called with dev->lock held.
 */
static void gr_control_stall(struct gr_udc *dev)
{
	u32 epctrl;

	epctrl = gr_read32(&dev->epo[0].regs->epctrl);
	gr_write32(&dev->epo[0].regs->epctrl, epctrl | GR_EPCTRL_CS);
	epctrl = gr_read32(&dev->epi[0].regs->epctrl);
	gr_write32(&dev->epi[0].regs->epctrl, epctrl | GR_EPCTRL_CS);

	dev->ep0state = GR_EP0_STALL;
}

/*
 * Halts, halts and wedges, or clears halt for an endpoint.
 *
 * Must be called with dev->lock held.
 */
static int gr_ep_halt_wedge(struct gr_ep *ep, int halt, int wedge, int fromhost)
{
	u32 epctrl;
	int retval = 0;

	if (ep->num && !ep->ep.desc)
		return -EINVAL;

	if (ep->num && ep->ep.desc->bmAttributes == USB_ENDPOINT_XFER_ISOC)
		return -EOPNOTSUPP;

	/* Never actually halt ep0, and therefore never clear halt for ep0 */
	if (!ep->num) {
		if (halt && !fromhost) {
			/* ep0 halt from gadget - generate protocol stall */
			gr_control_stall(ep->dev);
			dev_dbg(ep->dev->dev, "EP: stall ep0\n");
			return 0;
		}
		return -EINVAL;
	}

	dev_dbg(ep->dev->dev, "EP: %s halt %s\n",
		(halt ? (wedge ? "wedge" : "set") : "clear"), ep->ep.name);

	epctrl = gr_read32(&ep->regs->epctrl);
	if (halt) {
		/* Set HALT */
		gr_write32(&ep->regs->epctrl, epctrl | GR_EPCTRL_EH);
		ep->stopped = 1;
		if (wedge)
			ep->wedged = 1;
	} else {
		gr_write32(&ep->regs->epctrl, epctrl & ~GR_EPCTRL_EH);
		ep->stopped = 0;
		ep->wedged = 0;

		/* Things might have been queued up in the meantime */
		if (!ep->dma_start)
			gr_start_dma(ep);
	}

	return retval;
}

/* Must be called with dev->lock held */
static inline void gr_set_ep0state(struct gr_udc *dev, enum gr_ep0state value)
{
	if (dev->ep0state != value)
		dev_vdbg(dev->dev, "STATE:  ep0state=%s\n",
			 gr_ep0state_string(value));
	dev->ep0state = value;
}

/*
 * Should only be called when endpoints can not generate interrupts.
 *
 * Must be called with dev->lock held.
 */
static void gr_disable_interrupts_and_pullup(struct gr_udc *dev)
{
	gr_write32(&dev->regs->control, 0);
	wmb(); /* Make sure that we do not deny one of our interrupts */
	dev->irq_enabled = 0;
}

/*
 * Stop all device activity and disable data line pullup.
 *
 * Must be called with dev->lock held and irqs disabled.
 */
static void gr_stop_activity(struct gr_udc *dev)
{
	struct gr_ep *ep;

	list_for_each_entry(ep, &dev->ep_list, ep_list)
		gr_ep_nuke(ep);

	gr_disable_interrupts_and_pullup(dev);

	gr_set_ep0state(dev, GR_EP0_DISCONNECT);
	usb_gadget_set_state(&dev->gadget, USB_STATE_NOTATTACHED);
}

/* ---------------------------------------------------------------------- */
/* ep0 setup packet handling */

static void gr_ep0_testmode_complete(struct usb_ep *_ep,
				     struct usb_request *_req)
{
	struct gr_ep *ep;
	struct gr_udc *dev;
	u32 control;

	ep = container_of(_ep, struct gr_ep, ep);
	dev = ep->dev;

	spin_lock(&dev->lock);

	control = gr_read32(&dev->regs->control);
	control |= GR_CONTROL_TM | (dev->test_mode << GR_CONTROL_TS_POS);
	gr_write32(&dev->regs->control, control);

	spin_unlock(&dev->lock);
}

static void gr_ep0_dummy_complete(struct usb_ep *_ep, struct usb_request *_req)
{
	/* Nothing needs to be done here */
}

/*
 * Queue a response on ep0in.
 *
 * Must be called with dev->lock held.
 */
static int gr_ep0_respond(struct gr_udc *dev, u8 *buf, int length,
			  void (*complete)(struct usb_ep *ep,
					   struct usb_request *req))
{
	u8 *reqbuf = dev->ep0reqi->req.buf;
	int status;
	int i;

	for (i = 0; i < length; i++)
		reqbuf[i] = buf[i];
	dev->ep0reqi->req.length = length;
	dev->ep0reqi->req.complete = complete;

	status = gr_queue_int(&dev->epi[0], dev->ep0reqi, GFP_ATOMIC);
	if (status < 0)
		dev_err(dev->dev,
			"Could not queue ep0in setup response: %d\n", status);

	return status;
}

/*
 * Queue a 2 byte response on ep0in.
 *
 * Must be called with dev->lock held.
 */
static inline int gr_ep0_respond_u16(struct gr_udc *dev, u16 response)
{
	__le16 le_response = cpu_to_le16(response);

	return gr_ep0_respond(dev, (u8 *)&le_response, 2,
			      gr_ep0_dummy_complete);
}

/*
 * Queue a ZLP response on ep0in.
 *
 * Must be called with dev->lock held.
 */
static inline int gr_ep0_respond_empty(struct gr_udc *dev)
{
	return gr_ep0_respond(dev, NULL, 0, gr_ep0_dummy_complete);
}

/*
 * This is run when a SET_ADDRESS request is received. First writes
 * the new address to the control register which is updated internally
 * when the next IN packet is ACKED.
 *
 * Must be called with dev->lock held.
 */
static void gr_set_address(struct gr_udc *dev, u8 address)
{
	u32 control;

	control = gr_read32(&dev->regs->control) & ~GR_CONTROL_UA_MASK;
	control |= (address << GR_CONTROL_UA_POS) & GR_CONTROL_UA_MASK;
	control |= GR_CONTROL_SU;
	gr_write32(&dev->regs->control, control);
}

/*
 * Returns negative for STALL, 0 for successful handling and positive for
 * delegation.
 *
 * Must be called with dev->lock held.
 */
static int gr_device_request(struct gr_udc *dev, u8 type, u8 request,
			     u16 value, u16 index)
{
	u16 response;
	u8 test;

	switch (request) {
	case USB_REQ_SET_ADDRESS:
		dev_dbg(dev->dev, "STATUS: address %d\n", value & 0xff);
		gr_set_address(dev, value & 0xff);
		if (value)
			usb_gadget_set_state(&dev->gadget, USB_STATE_ADDRESS);
		else
			usb_gadget_set_state(&dev->gadget, USB_STATE_DEFAULT);
		return gr_ep0_respond_empty(dev);

	case USB_REQ_GET_STATUS:
		/* Self powered | remote wakeup */
		response = 0x0001 | (dev->remote_wakeup ? 0x0002 : 0);
		return gr_ep0_respond_u16(dev, response);

	case USB_REQ_SET_FEATURE:
		switch (value) {
		case USB_DEVICE_REMOTE_WAKEUP:
			/* Allow remote wakeup */
			dev->remote_wakeup = 1;
			return gr_ep0_respond_empty(dev);

		case USB_DEVICE_TEST_MODE:
			/* The hardware does not support USB_TEST_FORCE_ENABLE */
			test = index >> 8;
			if (test >= USB_TEST_J && test <= USB_TEST_PACKET) {
				dev->test_mode = test;
				return gr_ep0_respond(dev, NULL, 0,
						      gr_ep0_testmode_complete);
			}
		}
		break;

	case USB_REQ_CLEAR_FEATURE:
		switch (value) {
		case USB_DEVICE_REMOTE_WAKEUP:
			/* Disallow remote wakeup */
			dev->remote_wakeup = 0;
			return gr_ep0_respond_empty(dev);
		}
		break;
	}

	return 1; /* Delegate the rest */
}

/*
 * Returns negative for STALL, 0 for successful handling and positive for
 * delegation.
 *
 * Must be called with dev->lock held.
 */
static int gr_interface_request(struct gr_udc *dev, u8 type, u8 request,
				u16 value, u16 index)
{
	if (dev->gadget.state != USB_STATE_CONFIGURED)
		return -1;

	/*
	 * Should return STALL for invalid interfaces, but udc driver does not
	 * know anything about that. However, many gadget drivers do not handle
	 * GET_STATUS so we need to take care of that.
	 */

	switch (request) {
	case USB_REQ_GET_STATUS:
		return gr_ep0_respond_u16(dev, 0x0000);

	case USB_REQ_SET_FEATURE:
	case USB_REQ_CLEAR_FEATURE:
		/*
		 * No possible valid standard requests. Still let gadget drivers
		 * have a go at it.
		 */
		break;
	}

	return 1; /* Delegate the rest */
}

/*
 * Returns negative for STALL, 0 for successful handling and positive for
 * delegation.
 *
 * Must be called with dev->lock held.
 */
static int gr_endpoint_request(struct gr_udc *dev, u8 type, u8 request,
			       u16 value, u16 index)
{
	struct gr_ep *ep;
	int status;
	int halted;
	u8 epnum = index & USB_ENDPOINT_NUMBER_MASK;
	u8 is_in = index & USB_ENDPOINT_DIR_MASK;

	if ((is_in && epnum >= dev->nepi) || (!is_in && epnum >= dev->nepo))
		return -1;

	if (dev->gadget.state != USB_STATE_CONFIGURED && epnum != 0)
		return -1;

	ep = (is_in ? &dev->epi[epnum] : &dev->epo[epnum]);

	switch (request) {
	case USB_REQ_GET_STATUS:
		halted = gr_read32(&ep->regs->epctrl) & GR_EPCTRL_EH;
		return gr_ep0_respond_u16(dev, halted ? 0x0001 : 0);

	case USB_REQ_SET_FEATURE:
		switch (value) {
		case USB_ENDPOINT_HALT:
			status = gr_ep_halt_wedge(ep, 1, 0, 1);
			if (status >= 0)
				status = gr_ep0_respond_empty(dev);
			return status;
		}
		break;

	case USB_REQ_CLEAR_FEATURE:
		switch (value) {
		case USB_ENDPOINT_HALT:
			if (ep->wedged)
				return -1;
			status = gr_ep_halt_wedge(ep, 0, 0, 1);
			if (status >= 0)
				status = gr_ep0_respond_empty(dev);
			return status;
		}
		break;
	}

	return 1; /* Delegate the rest */
}

/* Must be called with dev->lock held */
static void gr_ep0out_requeue(struct gr_udc *dev)
{
	int ret = gr_queue_int(&dev->epo[0], dev->ep0reqo, GFP_ATOMIC);

	if (ret)
		dev_err(dev->dev, "Could not queue ep0out setup request: %d\n",
			ret);
}

/*
 * The main function dealing with setup requests on ep0.
 *
 * Must be called with dev->lock held and irqs disabled
 */
static void gr_ep0_setup(struct gr_udc *dev, struct gr_request *req)
	__releases(&dev->lock)
	__acquires(&dev->lock)
{
	union {
		struct usb_ctrlrequest ctrl;
		u8 raw[8];
		u32 word[2];
	} u;
	u8 type;
	u8 request;
	u16 value;
	u16 index;
	u16 length;
	int i;
	int status;

	/* Restore from ep0 halt */
	if (dev->ep0state == GR_EP0_STALL) {
		gr_set_ep0state(dev, GR_EP0_SETUP);
		if (!req->req.actual)
			goto out;
	}

	if (dev->ep0state == GR_EP0_ISTATUS) {
		gr_set_ep0state(dev, GR_EP0_SETUP);
		if (req->req.actual > 0)
			dev_dbg(dev->dev,
				"Unexpected setup packet at state %s\n",
				gr_ep0state_string(GR_EP0_ISTATUS));
		else
			goto out; /* Got expected ZLP */
	} else if (dev->ep0state != GR_EP0_SETUP) {
		dev_info(dev->dev,
			 "Unexpected ep0out request at state %s - stalling\n",
			 gr_ep0state_string(dev->ep0state));
		gr_control_stall(dev);
		gr_set_ep0state(dev, GR_EP0_SETUP);
		goto out;
	} else if (!req->req.actual) {
		dev_dbg(dev->dev, "Unexpected ZLP at state %s\n",
			gr_ep0state_string(dev->ep0state));
		goto out;
	}

	/* Handle SETUP packet */
	for (i = 0; i < req->req.actual; i++)
		u.raw[i] = ((u8 *)req->req.buf)[i];

	type = u.ctrl.bRequestType;
	request = u.ctrl.bRequest;
	value = le16_to_cpu(u.ctrl.wValue);
	index = le16_to_cpu(u.ctrl.wIndex);
	length = le16_to_cpu(u.ctrl.wLength);

	gr_dbgprint_devreq(dev, type, request, value, index, length);

	/* Check for data stage */
	if (length) {
		if (type & USB_DIR_IN)
			gr_set_ep0state(dev, GR_EP0_IDATA);
		else
			gr_set_ep0state(dev, GR_EP0_ODATA);
	}

	status = 1; /* Positive status flags delegation */
	if ((type & USB_TYPE_MASK) == USB_TYPE_STANDARD) {
		switch (type & USB_RECIP_MASK) {
		case USB_RECIP_DEVICE:
			status = gr_device_request(dev, type, request,
						   value, index);
			break;
		case USB_RECIP_ENDPOINT:
			status =  gr_endpoint_request(dev, type, request,
						      value, index);
			break;
		case USB_RECIP_INTERFACE:
			status = gr_interface_request(dev, type, request,
						      value, index);
			break;
		}
	}

	if (status > 0) {
		spin_unlock(&dev->lock);

		dev_vdbg(dev->dev, "DELEGATE\n");
		status = dev->driver->setup(&dev->gadget, &u.ctrl);

		spin_lock(&dev->lock);
	}

	/* Generate STALL on both ep0out and ep0in if requested */
	if (unlikely(status < 0)) {
		dev_vdbg(dev->dev, "STALL\n");
		gr_control_stall(dev);
	}

	if ((type & USB_TYPE_MASK) == USB_TYPE_STANDARD &&
	    request == USB_REQ_SET_CONFIGURATION) {
		if (!value) {
			dev_dbg(dev->dev, "STATUS: deconfigured\n");
			usb_gadget_set_state(&dev->gadget, USB_STATE_ADDRESS);
		} else if (status >= 0) {
			/* Not configured unless gadget OK:s it */
			dev_dbg(dev->dev, "STATUS: configured: %d\n", value);
			usb_gadget_set_state(&dev->gadget,
					     USB_STATE_CONFIGURED);
		}
	}

	/* Get ready for next stage */
	if (dev->ep0state == GR_EP0_ODATA)
		gr_set_ep0state(dev, GR_EP0_OSTATUS);
	else if (dev->ep0state == GR_EP0_IDATA)
		gr_set_ep0state(dev, GR_EP0_ISTATUS);
	else
		gr_set_ep0state(dev, GR_EP0_SETUP);

out:
	gr_ep0out_requeue(dev);
}

/* ---------------------------------------------------------------------- */
/* VBUS and USB reset handling */

/* Must be called with dev->lock held and irqs disabled  */
static void gr_vbus_connected(struct gr_udc *dev, u32 status)
{
	u32 control;

	dev->gadget.speed = GR_SPEED(status);
	usb_gadget_set_state(&dev->gadget, USB_STATE_POWERED);

	/* Turn on full interrupts and pullup */
	control = (GR_CONTROL_SI | GR_CONTROL_UI | GR_CONTROL_VI |
		   GR_CONTROL_SP | GR_CONTROL_EP);
	gr_write32(&dev->regs->control, control);
}

/* Must be called with dev->lock held */
static void gr_enable_vbus_detect(struct gr_udc *dev)
{
	u32 status;

	dev->irq_enabled = 1;
	wmb(); /* Make sure we do not ignore an interrupt */
	gr_write32(&dev->regs->control, GR_CONTROL_VI);

	/* Take care of the case we are already plugged in at this point */
	status = gr_read32(&dev->regs->status);
	if (status & GR_STATUS_VB)
		gr_vbus_connected(dev, status);
}

/* Must be called with dev->lock held and irqs disabled */
static void gr_vbus_disconnected(struct gr_udc *dev)
{
	gr_stop_activity(dev);

	/* Report disconnect */
	if (dev->driver && dev->driver->disconnect) {
		spin_unlock(&dev->lock);

		dev->driver->disconnect(&dev->gadget);

		spin_lock(&dev->lock);
	}

	gr_enable_vbus_detect(dev);
}

/* Must be called with dev->lock held and irqs disabled */
static void gr_udc_usbreset(struct gr_udc *dev, u32 status)
{
	gr_set_address(dev, 0);
	gr_set_ep0state(dev, GR_EP0_SETUP);
	usb_gadget_set_state(&dev->gadget, USB_STATE_DEFAULT);
	dev->gadget.speed = GR_SPEED(status);

	gr_ep_nuke(&dev->epo[0]);
	gr_ep_nuke(&dev->epi[0]);
	dev->epo[0].stopped = 0;
	dev->epi[0].stopped = 0;
	gr_ep0out_requeue(dev);
}

/* ---------------------------------------------------------------------- */
/* Irq handling */

/*
 * Handles interrupts from in endpoints. Returns whether something was handled.
 *
 * Must be called with dev->lock held, irqs disabled and with !ep->stopped.
 */
static int gr_handle_in_ep(struct gr_ep *ep)
{
	struct gr_request *req;

	req = list_first_entry(&ep->queue, struct gr_request, queue);
	if (!req->last_desc)
		return 0;

	if (READ_ONCE(req->last_desc->ctrl) & GR_DESC_IN_CTRL_EN)
		return 0; /* Not put in hardware buffers yet */

	if (gr_read32(&ep->regs->epstat) & (GR_EPSTAT_B1 | GR_EPSTAT_B0))
		return 0; /* Not transmitted yet, still in hardware buffers */

	/* Write complete */
	gr_dma_advance(ep, 0);

	return 1;
}

/*
 * Handles interrupts from out endpoints. Returns whether something was handled.
 *
 * Must be called with dev->lock held, irqs disabled and with !ep->stopped.
 */
static int gr_handle_out_ep(struct gr_ep *ep)
{
	u32 ep_dmactrl;
	u32 ctrl;
	u16 len;
	struct gr_request *req;
	struct gr_udc *dev = ep->dev;

	req = list_first_entry(&ep->queue, struct gr_request, queue);
	if (!req->curr_desc)
		return 0;

	ctrl = READ_ONCE(req->curr_desc->ctrl);
	if (ctrl & GR_DESC_OUT_CTRL_EN)
		return 0; /* Not received yet */

	/* Read complete */
	len = ctrl & GR_DESC_OUT_CTRL_LEN_MASK;
	req->req.actual += len;
	if (ctrl & GR_DESC_OUT_CTRL_SE)
		req->setup = 1;

	if (len < ep->ep.maxpacket || req->req.actual >= req->req.length) {
		/* Short packet or >= expected size - we are done */

		if ((ep == &dev->epo[0]) && (dev->ep0state == GR_EP0_OSTATUS)) {
			/*
			 * Send a status stage ZLP to ack the DATA stage in the
			 * OUT direction. This needs to be done before
			 * gr_dma_advance as that can lead to a call to
			 * ep0_setup that can change dev->ep0state.
			 */
			gr_ep0_respond_empty(dev);
			gr_set_ep0state(dev, GR_EP0_SETUP);
		}

		gr_dma_advance(ep, 0);
	} else {
		/* Not done yet. Enable the next descriptor to receive more. */
		req->curr_desc = req->curr_desc->next_desc;
		req->curr_desc->ctrl |= GR_DESC_OUT_CTRL_EN;

		ep_dmactrl = gr_read32(&ep->regs->dmactrl);
		gr_write32(&ep->regs->dmactrl, ep_dmactrl | GR_DMACTRL_DA);
	}

	return 1;
}

/*
 * Handle state changes. Returns whether something was handled.
 *
 * Must be called with dev->lock held and irqs disabled.
 */
static int gr_handle_state_changes(struct gr_udc *dev)
{
	u32 status = gr_read32(&dev->regs->status);
	int handled = 0;
	int powstate = !(dev->gadget.state == USB_STATE_NOTATTACHED ||
			 dev->gadget.state == USB_STATE_ATTACHED);

	/* VBUS valid detected */
	if (!powstate && (status & GR_STATUS_VB)) {
		dev_dbg(dev->dev, "STATUS: vbus valid detected\n");
		gr_vbus_connected(dev, status);
		handled = 1;
	}

	/* Disconnect */
	if (powstate && !(status & GR_STATUS_VB)) {
		dev_dbg(dev->dev, "STATUS: vbus invalid detected\n");
		gr_vbus_disconnected(dev);
		handled = 1;
	}

	/* USB reset detected */
	if (status & GR_STATUS_UR) {
		dev_dbg(dev->dev, "STATUS: USB reset - speed is %s\n",
			GR_SPEED_STR(status));
		gr_write32(&dev->regs->status, GR_STATUS_UR);
		gr_udc_usbreset(dev, status);
		handled = 1;
	}

	/* Speed change */
	if (dev->gadget.speed != GR_SPEED(status)) {
		dev_dbg(dev->dev, "STATUS: USB Speed change to %s\n",
			GR_SPEED_STR(status));
		dev->gadget.speed = GR_SPEED(status);
		handled = 1;
	}

	/* Going into suspend */
	if ((dev->ep0state != GR_EP0_SUSPEND) && !(status & GR_STATUS_SU)) {
		dev_dbg(dev->dev, "STATUS: USB suspend\n");
		gr_set_ep0state(dev, GR_EP0_SUSPEND);
		dev->suspended_from = dev->gadget.state;
		usb_gadget_set_state(&dev->gadget, USB_STATE_SUSPENDED);

		if ((dev->gadget.speed != USB_SPEED_UNKNOWN) &&
		    dev->driver && dev->driver->suspend) {
			spin_unlock(&dev->lock);

			dev->driver->suspend(&dev->gadget);

			spin_lock(&dev->lock);
		}
		handled = 1;
	}

	/* Coming out of suspend */
	if ((dev->ep0state == GR_EP0_SUSPEND) && (status & GR_STATUS_SU)) {
		dev_dbg(dev->dev, "STATUS: USB resume\n");
		if (dev->suspended_from == USB_STATE_POWERED)
			gr_set_ep0state(dev, GR_EP0_DISCONNECT);
		else
			gr_set_ep0state(dev, GR_EP0_SETUP);
		usb_gadget_set_state(&dev->gadget, dev->suspended_from);

		if ((dev->gadget.speed != USB_SPEED_UNKNOWN) &&
		    dev->driver && dev->driver->resume) {
			spin_unlock(&dev->lock);

			dev->driver->resume(&dev->gadget);

			spin_lock(&dev->lock);
		}
		handled = 1;
	}

	return handled;
}

/* Non-interrupt context irq handler */
static irqreturn_t gr_irq_handler(int irq, void *_dev)
{
	struct gr_udc *dev = _dev;
	struct gr_ep *ep;
	int handled = 0;
	int i;
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);

	if (!dev->irq_enabled)
		goto out;

	/*
	 * Check IN ep interrupts. We check these before the OUT eps because
	 * some gadgets reuse the request that might already be currently
	 * outstanding and needs to be completed (mainly setup requests).
	 */
	for (i = 0; i < dev->nepi; i++) {
		ep = &dev->epi[i];
		if (!ep->stopped && !ep->callback && !list_empty(&ep->queue))
			handled = gr_handle_in_ep(ep) || handled;
	}

	/* Check OUT ep interrupts */
	for (i = 0; i < dev->nepo; i++) {
		ep = &dev->epo[i];
		if (!ep->stopped && !ep->callback && !list_empty(&ep->queue))
			handled = gr_handle_out_ep(ep) || handled;
	}

	/* Check status interrupts */
	handled = gr_handle_state_changes(dev) || handled;

	/*
	 * Check AMBA DMA errors. Only check if we didn't find anything else to
	 * handle because this shouldn't happen if we did everything right.
	 */
	if (!handled) {
		list_for_each_entry(ep, &dev->ep_list, ep_list) {
			if (gr_read32(&ep->regs->dmactrl) & GR_DMACTRL_AE) {
				dev_err(dev->dev,
					"AMBA Error occurred for %s\n",
					ep->ep.name);
				handled = 1;
			}
		}
	}

out:
	spin_unlock_irqrestore(&dev->lock, flags);

	return handled ? IRQ_HANDLED : IRQ_NONE;
}

/* Interrupt context irq handler */
static irqreturn_t gr_irq(int irq, void *_dev)
{
	struct gr_udc *dev = _dev;

	if (!dev->irq_enabled)
		return IRQ_NONE;

	return IRQ_WAKE_THREAD;
}

/* ---------------------------------------------------------------------- */
/* USB ep ops */

/* Enable endpoint. Not for ep0in and ep0out that are handled separately. */
static int gr_ep_enable(struct usb_ep *_ep,
			const struct usb_endpoint_descriptor *desc)
{
	struct gr_udc *dev;
	struct gr_ep *ep;
	u8 mode;
	u8 nt;
	u16 max;
	u16 buffer_size = 0;
	u32 epctrl;

	ep = container_of(_ep, struct gr_ep, ep);
	if (!_ep || !desc || desc->bDescriptorType != USB_DT_ENDPOINT)
		return -EINVAL;

	dev = ep->dev;

	/* 'ep0' IN and OUT are reserved */
	if (ep == &dev->epo[0] || ep == &dev->epi[0])
		return -EINVAL;

	if (!dev->driver || dev->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	/* Make sure we are clear for enabling */
	epctrl = gr_read32(&ep->regs->epctrl);
	if (epctrl & GR_EPCTRL_EV)
		return -EBUSY;

	/* Check that directions match */
	if (!ep->is_in != !usb_endpoint_dir_in(desc))
		return -EINVAL;

	/* Check ep num */
	if ((!ep->is_in && ep->num >= dev->nepo) ||
	    (ep->is_in && ep->num >= dev->nepi))
		return -EINVAL;

	if (usb_endpoint_xfer_control(desc)) {
		mode = 0;
	} else if (usb_endpoint_xfer_isoc(desc)) {
		mode = 1;
	} else if (usb_endpoint_xfer_bulk(desc)) {
		mode = 2;
	} else if (usb_endpoint_xfer_int(desc)) {
		mode = 3;
	} else {
		dev_err(dev->dev, "Unknown transfer type for %s\n",
			ep->ep.name);
		return -EINVAL;
	}

	/*
	 * Bits 10-0 set the max payload. 12-11 set the number of
	 * additional transactions.
	 */
	max = usb_endpoint_maxp(desc);
	nt = usb_endpoint_maxp_mult(desc) - 1;
	buffer_size = GR_BUFFER_SIZE(epctrl);
	if (nt && (mode == 0 || mode == 2)) {
		dev_err(dev->dev,
			"%s mode: multiple trans./microframe not valid\n",
			(mode == 2 ? "Bulk" : "Control"));
		return -EINVAL;
	} else if (nt == 0x3) {
		dev_err(dev->dev,
			"Invalid value 0x3 for additional trans./microframe\n");
		return -EINVAL;
	} else if ((nt + 1) * max > buffer_size) {
		dev_err(dev->dev, "Hw buffer size %d < max payload %d * %d\n",
			buffer_size, (nt + 1), max);
		return -EINVAL;
	} else if (max == 0) {
		dev_err(dev->dev, "Max payload cannot be set to 0\n");
		return -EINVAL;
	} else if (max > ep->ep.maxpacket_limit) {
		dev_err(dev->dev, "Requested max payload %d > limit %d\n",
			max, ep->ep.maxpacket_limit);
		return -EINVAL;
	}

	spin_lock(&ep->dev->lock);

	if (!ep->stopped) {
		spin_unlock(&ep->dev->lock);
		return -EBUSY;
	}

	ep->stopped = 0;
	ep->wedged = 0;
	ep->ep.desc = desc;
	ep->ep.maxpacket = max;
	ep->dma_start = 0;


	if (nt) {
		/*
		 * Maximum possible size of all payloads in one microframe
		 * regardless of direction when using high-bandwidth mode.
		 */
		ep->bytes_per_buffer = (nt + 1) * max;
	} else if (ep->is_in) {
		/*
		 * The biggest multiple of maximum packet size that fits into
		 * the buffer. The hardware will split up into many packets in
		 * the IN direction.
		 */
		ep->bytes_per_buffer = (buffer_size / max) * max;
	} else {
		/*
		 * Only single packets will be placed the buffers in the OUT
		 * direction.
		 */
		ep->bytes_per_buffer = max;
	}

	epctrl = (max << GR_EPCTRL_MAXPL_POS)
		| (nt << GR_EPCTRL_NT_POS)
		| (mode << GR_EPCTRL_TT_POS)
		| GR_EPCTRL_EV;
	if (ep->is_in)
		epctrl |= GR_EPCTRL_PI;
	gr_write32(&ep->regs->epctrl, epctrl);

	gr_write32(&ep->regs->dmactrl, GR_DMACTRL_IE | GR_DMACTRL_AI);

	spin_unlock(&ep->dev->lock);

	dev_dbg(ep->dev->dev, "EP: %s enabled - %s with %d bytes/buffer\n",
		ep->ep.name, gr_modestring[mode], ep->bytes_per_buffer);
	return 0;
}

/* Disable endpoint. Not for ep0in and ep0out that are handled separately. */
static int gr_ep_disable(struct usb_ep *_ep)
{
	struct gr_ep *ep;
	struct gr_udc *dev;
	unsigned long flags;

	ep = container_of(_ep, struct gr_ep, ep);
	if (!_ep || !ep->ep.desc)
		return -ENODEV;

	dev = ep->dev;

	/* 'ep0' IN and OUT are reserved */
	if (ep == &dev->epo[0] || ep == &dev->epi[0])
		return -EINVAL;

	if (dev->ep0state == GR_EP0_SUSPEND)
		return -EBUSY;

	dev_dbg(ep->dev->dev, "EP: disable %s\n", ep->ep.name);

	spin_lock_irqsave(&dev->lock, flags);

	gr_ep_nuke(ep);
	gr_ep_reset(ep);
	ep->ep.desc = NULL;

	spin_unlock_irqrestore(&dev->lock, flags);

	return 0;
}

/*
 * Frees a request, but not any DMA buffers associated with it
 * (gr_finish_request should already have taken care of that).
 */
static void gr_free_request(struct usb_ep *_ep, struct usb_request *_req)
{
	struct gr_request *req;

	if (!_ep || !_req)
		return;
	req = container_of(_req, struct gr_request, req);

	/* Leads to memory leak */
	WARN(!list_empty(&req->queue),
	     "request not dequeued properly before freeing\n");

	kfree(req);
}

/* Queue a request from the gadget */
static int gr_queue_ext(struct usb_ep *_ep, struct usb_request *_req,
			gfp_t gfp_flags)
{
	struct gr_ep *ep;
	struct gr_request *req;
	struct gr_udc *dev;
	int ret;

	if (unlikely(!_ep || !_req))
		return -EINVAL;

	ep = container_of(_ep, struct gr_ep, ep);
	req = container_of(_req, struct gr_request, req);
	dev = ep->dev;

	spin_lock(&ep->dev->lock);

	/*
	 * The ep0 pointer in the gadget struct is used both for ep0in and
	 * ep0out. In a data stage in the out direction ep0out needs to be used
	 * instead of the default ep0in. Completion functions might use
	 * driver_data, so that needs to be copied as well.
	 */
	if ((ep == &dev->epi[0]) && (dev->ep0state == GR_EP0_ODATA)) {
		ep = &dev->epo[0];
		ep->ep.driver_data = dev->epi[0].ep.driver_data;
	}

	if (ep->is_in)
		gr_dbgprint_request("EXTERN", ep, req);

	ret = gr_queue(ep, req, GFP_ATOMIC);

	spin_unlock(&ep->dev->lock);

	return ret;
}

/* Dequeue JUST ONE request */
static int gr_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct gr_request *req;
	struct gr_ep *ep;
	struct gr_udc *dev;
	int ret = 0;
	unsigned long flags;

	ep = container_of(_ep, struct gr_ep, ep);
	if (!_ep || !_req || (!ep->ep.desc && ep->num != 0))
		return -EINVAL;
	dev = ep->dev;
	if (!dev->driver)
		return -ESHUTDOWN;

	/* We can't touch (DMA) registers when suspended */
	if (dev->ep0state == GR_EP0_SUSPEND)
		return -EBUSY;

	spin_lock_irqsave(&dev->lock, flags);

	/* Make sure it's actually queued on this endpoint */
	list_for_each_entry(req, &ep->queue, queue) {
		if (&req->req == _req)
			break;
	}
	if (&req->req != _req) {
		ret = -EINVAL;
		goto out;
	}

	if (list_first_entry(&ep->queue, struct gr_request, queue) == req) {
		/* This request is currently being processed */
		gr_abort_dma(ep);
		if (ep->stopped)
			gr_finish_request(ep, req, -ECONNRESET);
		else
			gr_dma_advance(ep, -ECONNRESET);
	} else if (!list_empty(&req->queue)) {
		/* Not being processed - gr_finish_request dequeues it */
		gr_finish_request(ep, req, -ECONNRESET);
	} else {
		ret = -EOPNOTSUPP;
	}

out:
	spin_unlock_irqrestore(&dev->lock, flags);

	return ret;
}

/* Helper for gr_set_halt and gr_set_wedge */
static int gr_set_halt_wedge(struct usb_ep *_ep, int halt, int wedge)
{
	int ret;
	struct gr_ep *ep;

	if (!_ep)
		return -ENODEV;
	ep = container_of(_ep, struct gr_ep, ep);

	spin_lock(&ep->dev->lock);

	/* Halting an IN endpoint should fail if queue is not empty */
	if (halt && ep->is_in && !list_empty(&ep->queue)) {
		ret = -EAGAIN;
		goto out;
	}

	ret = gr_ep_halt_wedge(ep, halt, wedge, 0);

out:
	spin_unlock(&ep->dev->lock);

	return ret;
}

/* Halt endpoint */
static int gr_set_halt(struct usb_ep *_ep, int halt)
{
	return gr_set_halt_wedge(_ep, halt, 0);
}

/* Halt and wedge endpoint */
static int gr_set_wedge(struct usb_ep *_ep)
{
	return gr_set_halt_wedge(_ep, 1, 1);
}

/*
 * Return the total number of bytes currently stored in the internal buffers of
 * the endpoint.
 */
static int gr_fifo_status(struct usb_ep *_ep)
{
	struct gr_ep *ep;
	u32 epstat;
	u32 bytes = 0;

	if (!_ep)
		return -ENODEV;
	ep = container_of(_ep, struct gr_ep, ep);

	epstat = gr_read32(&ep->regs->epstat);

	if (epstat & GR_EPSTAT_B0)
		bytes += (epstat & GR_EPSTAT_B0CNT_MASK) >> GR_EPSTAT_B0CNT_POS;
	if (epstat & GR_EPSTAT_B1)
		bytes += (epstat & GR_EPSTAT_B1CNT_MASK) >> GR_EPSTAT_B1CNT_POS;

	return bytes;
}


/* Empty data from internal buffers of an endpoint. */
static void gr_fifo_flush(struct usb_ep *_ep)
{
	struct gr_ep *ep;
	u32 epctrl;

	if (!_ep)
		return;
	ep = container_of(_ep, struct gr_ep, ep);
	dev_vdbg(ep->dev->dev, "EP: flush fifo %s\n", ep->ep.name);

	spin_lock(&ep->dev->lock);

	epctrl = gr_read32(&ep->regs->epctrl);
	epctrl |= GR_EPCTRL_CB;
	gr_write32(&ep->regs->epctrl, epctrl);

	spin_unlock(&ep->dev->lock);
}

static const struct usb_ep_ops gr_ep_ops = {
	.enable		= gr_ep_enable,
	.disable	= gr_ep_disable,

	.alloc_request	= gr_alloc_request,
	.free_request	= gr_free_request,

	.queue		= gr_queue_ext,
	.dequeue	= gr_dequeue,

	.set_halt	= gr_set_halt,
	.set_wedge	= gr_set_wedge,
	.fifo_status	= gr_fifo_status,
	.fifo_flush	= gr_fifo_flush,
};

/* ---------------------------------------------------------------------- */
/* USB Gadget ops */

static int gr_get_frame(struct usb_gadget *_gadget)
{
	struct gr_udc *dev;

	if (!_gadget)
		return -ENODEV;
	dev = container_of(_gadget, struct gr_udc, gadget);
	return gr_read32(&dev->regs->status) & GR_STATUS_FN_MASK;
}

static int gr_wakeup(struct usb_gadget *_gadget)
{
	struct gr_udc *dev;

	if (!_gadget)
		return -ENODEV;
	dev = container_of(_gadget, struct gr_udc, gadget);

	/* Remote wakeup feature not enabled by host*/
	if (!dev->remote_wakeup)
		return -EINVAL;

	spin_lock(&dev->lock);

	gr_write32(&dev->regs->control,
		   gr_read32(&dev->regs->control) | GR_CONTROL_RW);

	spin_unlock(&dev->lock);

	return 0;
}

static int gr_pullup(struct usb_gadget *_gadget, int is_on)
{
	struct gr_udc *dev;
	u32 control;

	if (!_gadget)
		return -ENODEV;
	dev = container_of(_gadget, struct gr_udc, gadget);

	spin_lock(&dev->lock);

	control = gr_read32(&dev->regs->control);
	if (is_on)
		control |= GR_CONTROL_EP;
	else
		control &= ~GR_CONTROL_EP;
	gr_write32(&dev->regs->control, control);

	spin_unlock(&dev->lock);

	return 0;
}

static int gr_udc_start(struct usb_gadget *gadget,
			struct usb_gadget_driver *driver)
{
	struct gr_udc *dev = to_gr_udc(gadget);

	spin_lock(&dev->lock);

	/* Hook up the driver */
	driver->driver.bus = NULL;
	dev->driver = driver;

	/* Get ready for host detection */
	gr_enable_vbus_detect(dev);

	spin_unlock(&dev->lock);

	return 0;
}

static int gr_udc_stop(struct usb_gadget *gadget)
{
	struct gr_udc *dev = to_gr_udc(gadget);
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);

	dev->driver = NULL;
	gr_stop_activity(dev);

	spin_unlock_irqrestore(&dev->lock, flags);

	return 0;
}

static const struct usb_gadget_ops gr_ops = {
	.get_frame	= gr_get_frame,
	.wakeup         = gr_wakeup,
	.pullup         = gr_pullup,
	.udc_start	= gr_udc_start,
	.udc_stop	= gr_udc_stop,
	/* Other operations not supported */
};

/* ---------------------------------------------------------------------- */
/* Module probe, removal and of-matching */

static const char * const onames[] = {
	"ep0out", "ep1out", "ep2out", "ep3out", "ep4out", "ep5out",
	"ep6out", "ep7out", "ep8out", "ep9out", "ep10out", "ep11out",
	"ep12out", "ep13out", "ep14out", "ep15out"
};

static const char * const inames[] = {
	"ep0in", "ep1in", "ep2in", "ep3in", "ep4in", "ep5in",
	"ep6in", "ep7in", "ep8in", "ep9in", "ep10in", "ep11in",
	"ep12in", "ep13in", "ep14in", "ep15in"
};

/* Must be called with dev->lock held */
static int gr_ep_init(struct gr_udc *dev, int num, int is_in, u32 maxplimit)
{
	struct gr_ep *ep;
	struct gr_request *req;
	struct usb_request *_req;
	void *buf;

	if (is_in) {
		ep = &dev->epi[num];
		ep->ep.name = inames[num];
		ep->regs = &dev->regs->epi[num];
	} else {
		ep = &dev->epo[num];
		ep->ep.name = onames[num];
		ep->regs = &dev->regs->epo[num];
	}

	gr_ep_reset(ep);
	ep->num = num;
	ep->is_in = is_in;
	ep->dev = dev;
	ep->ep.ops = &gr_ep_ops;
	INIT_LIST_HEAD(&ep->queue);

	if (num == 0) {
		_req = gr_alloc_request(&ep->ep, GFP_ATOMIC);
		if (!_req)
			return -ENOMEM;

		buf = devm_kzalloc(dev->dev, PAGE_SIZE, GFP_DMA | GFP_ATOMIC);
		if (!buf) {
			gr_free_request(&ep->ep, _req);
			return -ENOMEM;
		}

		req = container_of(_req, struct gr_request, req);
		req->req.buf = buf;
		req->req.length = MAX_CTRL_PL_SIZE;

		if (is_in)
			dev->ep0reqi = req; /* Complete gets set as used */
		else
			dev->ep0reqo = req; /* Completion treated separately */

		usb_ep_set_maxpacket_limit(&ep->ep, MAX_CTRL_PL_SIZE);
		ep->bytes_per_buffer = MAX_CTRL_PL_SIZE;

		ep->ep.caps.type_control = true;
	} else {
		usb_ep_set_maxpacket_limit(&ep->ep, (u16)maxplimit);
		list_add_tail(&ep->ep.ep_list, &dev->gadget.ep_list);

		ep->ep.caps.type_iso = true;
		ep->ep.caps.type_bulk = true;
		ep->ep.caps.type_int = true;
	}
	list_add_tail(&ep->ep_list, &dev->ep_list);

	if (is_in)
		ep->ep.caps.dir_in = true;
	else
		ep->ep.caps.dir_out = true;

	ep->tailbuf = dma_alloc_coherent(dev->dev, ep->ep.maxpacket_limit,
					 &ep->tailbuf_paddr, GFP_ATOMIC);
	if (!ep->tailbuf)
		return -ENOMEM;

	return 0;
}

/* Must be called with dev->lock held */
static int gr_udc_init(struct gr_udc *dev)
{
	struct device_node *np = dev->dev->of_node;
	u32 epctrl_val;
	u32 dmactrl_val;
	int i;
	int ret = 0;
	u32 bufsize;

	gr_set_address(dev, 0);

	INIT_LIST_HEAD(&dev->gadget.ep_list);
	dev->gadget.speed = USB_SPEED_UNKNOWN;
	dev->gadget.ep0 = &dev->epi[0].ep;

	INIT_LIST_HEAD(&dev->ep_list);
	gr_set_ep0state(dev, GR_EP0_DISCONNECT);

	for (i = 0; i < dev->nepo; i++) {
		if (of_property_read_u32_index(np, "epobufsizes", i, &bufsize))
			bufsize = 1024;
		ret = gr_ep_init(dev, i, 0, bufsize);
		if (ret)
			return ret;
	}

	for (i = 0; i < dev->nepi; i++) {
		if (of_property_read_u32_index(np, "epibufsizes", i, &bufsize))
			bufsize = 1024;
		ret = gr_ep_init(dev, i, 1, bufsize);
		if (ret)
			return ret;
	}

	/* Must be disabled by default */
	dev->remote_wakeup = 0;

	/* Enable ep0out and ep0in */
	epctrl_val = (MAX_CTRL_PL_SIZE << GR_EPCTRL_MAXPL_POS) | GR_EPCTRL_EV;
	dmactrl_val = GR_DMACTRL_IE | GR_DMACTRL_AI;
	gr_write32(&dev->epo[0].regs->epctrl, epctrl_val);
	gr_write32(&dev->epi[0].regs->epctrl, epctrl_val | GR_EPCTRL_PI);
	gr_write32(&dev->epo[0].regs->dmactrl, dmactrl_val);
	gr_write32(&dev->epi[0].regs->dmactrl, dmactrl_val);

	return 0;
}

static void gr_ep_remove(struct gr_udc *dev, int num, int is_in)
{
	struct gr_ep *ep;

	if (is_in)
		ep = &dev->epi[num];
	else
		ep = &dev->epo[num];

	if (ep->tailbuf)
		dma_free_coherent(dev->dev, ep->ep.maxpacket_limit,
				  ep->tailbuf, ep->tailbuf_paddr);
}

static int gr_remove(struct platform_device *pdev)
{
	struct gr_udc *dev = platform_get_drvdata(pdev);
	int i;

	if (dev->added)
		usb_del_gadget_udc(&dev->gadget); /* Shuts everything down */
	if (dev->driver)
		return -EBUSY;

	gr_dfs_delete(dev);
	dma_pool_destroy(dev->desc_pool);
	platform_set_drvdata(pdev, NULL);

	gr_free_request(&dev->epi[0].ep, &dev->ep0reqi->req);
	gr_free_request(&dev->epo[0].ep, &dev->ep0reqo->req);

	for (i = 0; i < dev->nepo; i++)
		gr_ep_remove(dev, i, 0);
	for (i = 0; i < dev->nepi; i++)
		gr_ep_remove(dev, i, 1);

	return 0;
}
static int gr_request_irq(struct gr_udc *dev, int irq)
{
	return devm_request_threaded_irq(dev->dev, irq, gr_irq, gr_irq_handler,
					 IRQF_SHARED, driver_name, dev);
}

static int gr_probe(struct platform_device *pdev)
{
	struct gr_udc *dev;
	struct gr_regs __iomem *regs;
	int retval;
	u32 status;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	dev->dev = &pdev->dev;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	dev->irq = platform_get_irq(pdev, 0);
	if (dev->irq <= 0)
		return -ENODEV;

	/* Some core configurations has separate irqs for IN and OUT events */
	dev->irqi = platform_get_irq(pdev, 1);
	if (dev->irqi > 0) {
		dev->irqo = platform_get_irq(pdev, 2);
		if (dev->irqo <= 0)
			return -ENODEV;
	} else {
		dev->irqi = 0;
	}

	dev->gadget.name = driver_name;
	dev->gadget.max_speed = USB_SPEED_HIGH;
	dev->gadget.ops = &gr_ops;

	spin_lock_init(&dev->lock);
	dev->regs = regs;

	platform_set_drvdata(pdev, dev);

	/* Determine number of endpoints and data interface mode */
	status = gr_read32(&dev->regs->status);
	dev->nepi = ((status & GR_STATUS_NEPI_MASK) >> GR_STATUS_NEPI_POS) + 1;
	dev->nepo = ((status & GR_STATUS_NEPO_MASK) >> GR_STATUS_NEPO_POS) + 1;

	if (!(status & GR_STATUS_DM)) {
		dev_err(dev->dev, "Slave mode cores are not supported\n");
		return -ENODEV;
	}

	/* --- Effects of the following calls might need explicit cleanup --- */

	/* Create DMA pool for descriptors */
	dev->desc_pool = dma_pool_create("desc_pool", dev->dev,
					 sizeof(struct gr_dma_desc), 4, 0);
	if (!dev->desc_pool) {
		dev_err(dev->dev, "Could not allocate DMA pool");
		return -ENOMEM;
	}

	/* Inside lock so that no gadget can use this udc until probe is done */
	retval = usb_add_gadget_udc(dev->dev, &dev->gadget);
	if (retval) {
		dev_err(dev->dev, "Could not add gadget udc");
		goto out;
	}
	dev->added = 1;

	spin_lock(&dev->lock);

	retval = gr_udc_init(dev);
	if (retval) {
		spin_unlock(&dev->lock);
		goto out;
	}

	/* Clear all interrupt enables that might be left on since last boot */
	gr_disable_interrupts_and_pullup(dev);

	spin_unlock(&dev->lock);

	gr_dfs_create(dev);

	retval = gr_request_irq(dev, dev->irq);
	if (retval) {
		dev_err(dev->dev, "Failed to request irq %d\n", dev->irq);
		goto out;
	}

	if (dev->irqi) {
		retval = gr_request_irq(dev, dev->irqi);
		if (retval) {
			dev_err(dev->dev, "Failed to request irqi %d\n",
				dev->irqi);
			goto out;
		}
		retval = gr_request_irq(dev, dev->irqo);
		if (retval) {
			dev_err(dev->dev, "Failed to request irqo %d\n",
				dev->irqo);
			goto out;
		}
	}

	if (dev->irqi)
		dev_info(dev->dev, "regs: %p, irqs %d, %d, %d\n", dev->regs,
			 dev->irq, dev->irqi, dev->irqo);
	else
		dev_info(dev->dev, "regs: %p, irq %d\n", dev->regs, dev->irq);

out:
	if (retval)
		gr_remove(pdev);

	return retval;
}

static const struct of_device_id gr_match[] = {
	{.name = "GAISLER_USBDC"},
	{.name = "01_021"},
	{},
};
MODULE_DEVICE_TABLE(of, gr_match);

static struct platform_driver gr_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = gr_match,
	},
	.probe = gr_probe,
	.remove = gr_remove,
};
module_platform_driver(gr_driver);

MODULE_AUTHOR("Aeroflex Gaisler AB.");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
