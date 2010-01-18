/* linux/drivers/usb/gadget/s3c-hsotg.c
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * S3C USB2.0 High-speed / OtG driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/io.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>

#include <mach/map.h>

#include <plat/regs-usb-hsotg-phy.h>
#include <plat/regs-usb-hsotg.h>
#include <plat/regs-sys.h>
#include <plat/udc-hs.h>

#define DMA_ADDR_INVALID (~((dma_addr_t)0))

/* EP0_MPS_LIMIT
 *
 * Unfortunately there seems to be a limit of the amount of data that can
 * be transfered by IN transactions on EP0. This is either 127 bytes or 3
 * packets (which practially means 1 packet and 63 bytes of data) when the
 * MPS is set to 64.
 *
 * This means if we are wanting to move >127 bytes of data, we need to
 * split the transactions up, but just doing one packet at a time does
 * not work (this may be an implicit DATA0 PID on first packet of the
 * transaction) and doing 2 packets is outside the controller's limits.
 *
 * If we try to lower the MPS size for EP0, then no transfers work properly
 * for EP0, and the system will fail basic enumeration. As no cause for this
 * has currently been found, we cannot support any large IN transfers for
 * EP0.
 */
#define EP0_MPS_LIMIT	64

struct s3c_hsotg;
struct s3c_hsotg_req;

/**
 * struct s3c_hsotg_ep - driver endpoint definition.
 * @ep: The gadget layer representation of the endpoint.
 * @name: The driver generated name for the endpoint.
 * @queue: Queue of requests for this endpoint.
 * @parent: Reference back to the parent device structure.
 * @req: The current request that the endpoint is processing. This is
 *       used to indicate an request has been loaded onto the endpoint
 *       and has yet to be completed (maybe due to data move, or simply
 *	 awaiting an ack from the core all the data has been completed).
 * @debugfs: File entry for debugfs file for this endpoint.
 * @lock: State lock to protect contents of endpoint.
 * @dir_in: Set to true if this endpoint is of the IN direction, which
 *	    means that it is sending data to the Host.
 * @index: The index for the endpoint registers.
 * @name: The name array passed to the USB core.
 * @halted: Set if the endpoint has been halted.
 * @periodic: Set if this is a periodic ep, such as Interrupt
 * @sent_zlp: Set if we've sent a zero-length packet.
 * @total_data: The total number of data bytes done.
 * @fifo_size: The size of the FIFO (for periodic IN endpoints)
 * @fifo_load: The amount of data loaded into the FIFO (periodic IN)
 * @last_load: The offset of data for the last start of request.
 * @size_loaded: The last loaded size for DxEPTSIZE for periodic IN
 *
 * This is the driver's state for each registered enpoint, allowing it
 * to keep track of transactions that need doing. Each endpoint has a
 * lock to protect the state, to try and avoid using an overall lock
 * for the host controller as much as possible.
 *
 * For periodic IN endpoints, we have fifo_size and fifo_load to try
 * and keep track of the amount of data in the periodic FIFO for each
 * of these as we don't have a status register that tells us how much
 * is in each of them.
 */
struct s3c_hsotg_ep {
	struct usb_ep		ep;
	struct list_head	queue;
	struct s3c_hsotg	*parent;
	struct s3c_hsotg_req	*req;
	struct dentry		*debugfs;

	spinlock_t		lock;

	unsigned long		total_data;
	unsigned int		size_loaded;
	unsigned int		last_load;
	unsigned int		fifo_load;
	unsigned short		fifo_size;

	unsigned char		dir_in;
	unsigned char		index;

	unsigned int		halted:1;
	unsigned int		periodic:1;
	unsigned int		sent_zlp:1;

	char			name[10];
};

#define S3C_HSOTG_EPS	(8+1)	/* limit to 9 for the moment */

/**
 * struct s3c_hsotg - driver state.
 * @dev: The parent device supplied to the probe function
 * @driver: USB gadget driver
 * @plat: The platform specific configuration data.
 * @regs: The memory area mapped for accessing registers.
 * @regs_res: The resource that was allocated when claiming register space.
 * @irq: The IRQ number we are using
 * @debug_root: root directrory for debugfs.
 * @debug_file: main status file for debugfs.
 * @debug_fifo: FIFO status file for debugfs.
 * @ep0_reply: Request used for ep0 reply.
 * @ep0_buff: Buffer for EP0 reply data, if needed.
 * @ctrl_buff: Buffer for EP0 control requests.
 * @ctrl_req: Request for EP0 control packets.
 * @eps: The endpoints being supplied to the gadget framework
 */
struct s3c_hsotg {
	struct device		 *dev;
	struct usb_gadget_driver *driver;
	struct s3c_hsotg_plat	 *plat;

	void __iomem		*regs;
	struct resource		*regs_res;
	int			irq;

	struct dentry		*debug_root;
	struct dentry		*debug_file;
	struct dentry		*debug_fifo;

	struct usb_request	*ep0_reply;
	struct usb_request	*ctrl_req;
	u8			ep0_buff[8];
	u8			ctrl_buff[8];

	struct usb_gadget	gadget;
	struct s3c_hsotg_ep	eps[];
};

/**
 * struct s3c_hsotg_req - data transfer request
 * @req: The USB gadget request
 * @queue: The list of requests for the endpoint this is queued for.
 * @in_progress: Has already had size/packets written to core
 * @mapped: DMA buffer for this request has been mapped via dma_map_single().
 */
struct s3c_hsotg_req {
	struct usb_request	req;
	struct list_head	queue;
	unsigned char		in_progress;
	unsigned char		mapped;
};

/* conversion functions */
static inline struct s3c_hsotg_req *our_req(struct usb_request *req)
{
	return container_of(req, struct s3c_hsotg_req, req);
}

static inline struct s3c_hsotg_ep *our_ep(struct usb_ep *ep)
{
	return container_of(ep, struct s3c_hsotg_ep, ep);
}

static inline struct s3c_hsotg *to_hsotg(struct usb_gadget *gadget)
{
	return container_of(gadget, struct s3c_hsotg, gadget);
}

static inline void __orr32(void __iomem *ptr, u32 val)
{
	writel(readl(ptr) | val, ptr);
}

static inline void __bic32(void __iomem *ptr, u32 val)
{
	writel(readl(ptr) & ~val, ptr);
}

/* forward decleration of functions */
static void s3c_hsotg_dump(struct s3c_hsotg *hsotg);

/**
 * using_dma - return the DMA status of the driver.
 * @hsotg: The driver state.
 *
 * Return true if we're using DMA.
 *
 * Currently, we have the DMA support code worked into everywhere
 * that needs it, but the AMBA DMA implementation in the hardware can
 * only DMA from 32bit aligned addresses. This means that gadgets such
 * as the CDC Ethernet cannot work as they often pass packets which are
 * not 32bit aligned.
 *
 * Unfortunately the choice to use DMA or not is global to the controller
 * and seems to be only settable when the controller is being put through
 * a core reset. This means we either need to fix the gadgets to take
 * account of DMA alignment, or add bounce buffers (yuerk).
 *
 * Until this issue is sorted out, we always return 'false'.
 */
static inline bool using_dma(struct s3c_hsotg *hsotg)
{
	return false;	/* support is not complete */
}

/**
 * s3c_hsotg_en_gsint - enable one or more of the general interrupt
 * @hsotg: The device state
 * @ints: A bitmask of the interrupts to enable
 */
static void s3c_hsotg_en_gsint(struct s3c_hsotg *hsotg, u32 ints)
{
	u32 gsintmsk = readl(hsotg->regs + S3C_GINTMSK);
	u32 new_gsintmsk;

	new_gsintmsk = gsintmsk | ints;

	if (new_gsintmsk != gsintmsk) {
		dev_dbg(hsotg->dev, "gsintmsk now 0x%08x\n", new_gsintmsk);
		writel(new_gsintmsk, hsotg->regs + S3C_GINTMSK);
	}
}

/**
 * s3c_hsotg_disable_gsint - disable one or more of the general interrupt
 * @hsotg: The device state
 * @ints: A bitmask of the interrupts to enable
 */
static void s3c_hsotg_disable_gsint(struct s3c_hsotg *hsotg, u32 ints)
{
	u32 gsintmsk = readl(hsotg->regs + S3C_GINTMSK);
	u32 new_gsintmsk;

	new_gsintmsk = gsintmsk & ~ints;

	if (new_gsintmsk != gsintmsk)
		writel(new_gsintmsk, hsotg->regs + S3C_GINTMSK);
}

/**
 * s3c_hsotg_ctrl_epint - enable/disable an endpoint irq
 * @hsotg: The device state
 * @ep: The endpoint index
 * @dir_in: True if direction is in.
 * @en: The enable value, true to enable
 *
 * Set or clear the mask for an individual endpoint's interrupt
 * request.
 */
static void s3c_hsotg_ctrl_epint(struct s3c_hsotg *hsotg,
				 unsigned int ep, unsigned int dir_in,
				 unsigned int en)
{
	unsigned long flags;
	u32 bit = 1 << ep;
	u32 daint;

	if (!dir_in)
		bit <<= 16;

	local_irq_save(flags);
	daint = readl(hsotg->regs + S3C_DAINTMSK);
	if (en)
		daint |= bit;
	else
		daint &= ~bit;
	writel(daint, hsotg->regs + S3C_DAINTMSK);
	local_irq_restore(flags);
}

/**
 * s3c_hsotg_init_fifo - initialise non-periodic FIFOs
 * @hsotg: The device instance.
 */
static void s3c_hsotg_init_fifo(struct s3c_hsotg *hsotg)
{
	/* the ryu 2.6.24 release ahs
	   writel(0x1C0, hsotg->regs + S3C_GRXFSIZ);
	   writel(S3C_GNPTXFSIZ_NPTxFStAddr(0x200) |
		S3C_GNPTXFSIZ_NPTxFDep(0x1C0),
		hsotg->regs + S3C_GNPTXFSIZ);
	*/

	/* set FIFO sizes to 2048/0x1C0 */

	writel(2048, hsotg->regs + S3C_GRXFSIZ);
	writel(S3C_GNPTXFSIZ_NPTxFStAddr(2048) |
	       S3C_GNPTXFSIZ_NPTxFDep(0x1C0),
	       hsotg->regs + S3C_GNPTXFSIZ);
}

/**
 * @ep: USB endpoint to allocate request for.
 * @flags: Allocation flags
 *
 * Allocate a new USB request structure appropriate for the specified endpoint
 */
static struct usb_request *s3c_hsotg_ep_alloc_request(struct usb_ep *ep,
						      gfp_t flags)
{
	struct s3c_hsotg_req *req;

	req = kzalloc(sizeof(struct s3c_hsotg_req), flags);
	if (!req)
		return NULL;

	INIT_LIST_HEAD(&req->queue);

	req->req.dma = DMA_ADDR_INVALID;
	return &req->req;
}

/**
 * is_ep_periodic - return true if the endpoint is in periodic mode.
 * @hs_ep: The endpoint to query.
 *
 * Returns true if the endpoint is in periodic mode, meaning it is being
 * used for an Interrupt or ISO transfer.
 */
static inline int is_ep_periodic(struct s3c_hsotg_ep *hs_ep)
{
	return hs_ep->periodic;
}

/**
 * s3c_hsotg_unmap_dma - unmap the DMA memory being used for the request
 * @hsotg: The device state.
 * @hs_ep: The endpoint for the request
 * @hs_req: The request being processed.
 *
 * This is the reverse of s3c_hsotg_map_dma(), called for the completion
 * of a request to ensure the buffer is ready for access by the caller.
*/
static void s3c_hsotg_unmap_dma(struct s3c_hsotg *hsotg,
				struct s3c_hsotg_ep *hs_ep,
				struct s3c_hsotg_req *hs_req)
{
	struct usb_request *req = &hs_req->req;
	enum dma_data_direction dir;

	dir = hs_ep->dir_in ? DMA_TO_DEVICE : DMA_FROM_DEVICE;

	/* ignore this if we're not moving any data */
	if (hs_req->req.length == 0)
		return;

	if (hs_req->mapped) {
		/* we mapped this, so unmap and remove the dma */

		dma_unmap_single(hsotg->dev, req->dma, req->length, dir);

		req->dma = DMA_ADDR_INVALID;
		hs_req->mapped = 0;
	} else {
		dma_sync_single(hsotg->dev, req->dma, req->length, dir);
	}
}

/**
 * s3c_hsotg_write_fifo - write packet Data to the TxFIFO
 * @hsotg: The controller state.
 * @hs_ep: The endpoint we're going to write for.
 * @hs_req: The request to write data for.
 *
 * This is called when the TxFIFO has some space in it to hold a new
 * transmission and we have something to give it. The actual setup of
 * the data size is done elsewhere, so all we have to do is to actually
 * write the data.
 *
 * The return value is zero if there is more space (or nothing was done)
 * otherwise -ENOSPC is returned if the FIFO space was used up.
 *
 * This routine is only needed for PIO
*/
static int s3c_hsotg_write_fifo(struct s3c_hsotg *hsotg,
				struct s3c_hsotg_ep *hs_ep,
				struct s3c_hsotg_req *hs_req)
{
	bool periodic = is_ep_periodic(hs_ep);
	u32 gnptxsts = readl(hsotg->regs + S3C_GNPTXSTS);
	int buf_pos = hs_req->req.actual;
	int to_write = hs_ep->size_loaded;
	void *data;
	int can_write;
	int pkt_round;

	to_write -= (buf_pos - hs_ep->last_load);

	/* if there's nothing to write, get out early */
	if (to_write == 0)
		return 0;

	if (periodic) {
		u32 epsize = readl(hsotg->regs + S3C_DIEPTSIZ(hs_ep->index));
		int size_left;
		int size_done;

		/* work out how much data was loaded so we can calculate
		 * how much data is left in the fifo. */

		size_left = S3C_DxEPTSIZ_XferSize_GET(epsize);

		dev_dbg(hsotg->dev, "%s: left=%d, load=%d, fifo=%d, size %d\n",
			__func__, size_left,
			hs_ep->size_loaded, hs_ep->fifo_load, hs_ep->fifo_size);

		/* how much of the data has moved */
		size_done = hs_ep->size_loaded - size_left;

		/* how much data is left in the fifo */
		can_write = hs_ep->fifo_load - size_done;
		dev_dbg(hsotg->dev, "%s: => can_write1=%d\n",
			__func__, can_write);

		can_write = hs_ep->fifo_size - can_write;
		dev_dbg(hsotg->dev, "%s: => can_write2=%d\n",
			__func__, can_write);

		if (can_write <= 0) {
			s3c_hsotg_en_gsint(hsotg, S3C_GINTSTS_PTxFEmp);
			return -ENOSPC;
		}
	} else {
		if (S3C_GNPTXSTS_NPTxQSpcAvail_GET(gnptxsts) == 0) {
			dev_dbg(hsotg->dev,
				"%s: no queue slots available (0x%08x)\n",
				__func__, gnptxsts);

			s3c_hsotg_en_gsint(hsotg, S3C_GINTSTS_NPTxFEmp);
			return -ENOSPC;
		}

		can_write = S3C_GNPTXSTS_NPTxFSpcAvail_GET(gnptxsts);
	}

	dev_dbg(hsotg->dev, "%s: GNPTXSTS=%08x, can=%d, to=%d, mps %d\n",
		 __func__, gnptxsts, can_write, to_write, hs_ep->ep.maxpacket);

	/* limit to 512 bytes of data, it seems at least on the non-periodic
	 * FIFO, requests of >512 cause the endpoint to get stuck with a
	 * fragment of the end of the transfer in it.
	 */
	if (can_write > 512)
		can_write = 512;

	/* see if we can write data */

	if (to_write > can_write) {
		to_write = can_write;
		pkt_round = to_write % hs_ep->ep.maxpacket;

		/* Not sure, but we probably shouldn't be writing partial
		 * packets into the FIFO, so round the write down to an
		 * exact number of packets.
		 *
		 * Note, we do not currently check to see if we can ever
		 * write a full packet or not to the FIFO.
		 */

		if (pkt_round)
			to_write -= pkt_round;

		/* enable correct FIFO interrupt to alert us when there
		 * is more room left. */

		s3c_hsotg_en_gsint(hsotg,
				   periodic ? S3C_GINTSTS_PTxFEmp :
				   S3C_GINTSTS_NPTxFEmp);
	}

	dev_dbg(hsotg->dev, "write %d/%d, can_write %d, done %d\n",
		 to_write, hs_req->req.length, can_write, buf_pos);

	if (to_write <= 0)
		return -ENOSPC;

	hs_req->req.actual = buf_pos + to_write;
	hs_ep->total_data += to_write;

	if (periodic)
		hs_ep->fifo_load += to_write;

	to_write = DIV_ROUND_UP(to_write, 4);
	data = hs_req->req.buf + buf_pos;

	writesl(hsotg->regs + S3C_EPFIFO(hs_ep->index), data, to_write);

	return (to_write >= can_write) ? -ENOSPC : 0;
}

/**
 * get_ep_limit - get the maximum data legnth for this endpoint
 * @hs_ep: The endpoint
 *
 * Return the maximum data that can be queued in one go on a given endpoint
 * so that transfers that are too long can be split.
 */
static unsigned get_ep_limit(struct s3c_hsotg_ep *hs_ep)
{
	int index = hs_ep->index;
	unsigned maxsize;
	unsigned maxpkt;

	if (index != 0) {
		maxsize = S3C_DxEPTSIZ_XferSize_LIMIT + 1;
		maxpkt = S3C_DxEPTSIZ_PktCnt_LIMIT + 1;
	} else {
		if (hs_ep->dir_in) {
			/* maxsize = S3C_DIEPTSIZ0_XferSize_LIMIT + 1; */
			maxsize = 64+64+1;
			maxpkt = S3C_DIEPTSIZ0_PktCnt_LIMIT + 1;
		} else {
			maxsize = 0x3f;
			maxpkt = 2;
		}
	}

	/* we made the constant loading easier above by using +1 */
	maxpkt--;
	maxsize--;

	/* constrain by packet count if maxpkts*pktsize is greater
	 * than the length register size. */

	if ((maxpkt * hs_ep->ep.maxpacket) < maxsize)
		maxsize = maxpkt * hs_ep->ep.maxpacket;

	return maxsize;
}

/**
 * s3c_hsotg_start_req - start a USB request from an endpoint's queue
 * @hsotg: The controller state.
 * @hs_ep: The endpoint to process a request for
 * @hs_req: The request to start.
 * @continuing: True if we are doing more for the current request.
 *
 * Start the given request running by setting the endpoint registers
 * appropriately, and writing any data to the FIFOs.
 */
static void s3c_hsotg_start_req(struct s3c_hsotg *hsotg,
				struct s3c_hsotg_ep *hs_ep,
				struct s3c_hsotg_req *hs_req,
				bool continuing)
{
	struct usb_request *ureq = &hs_req->req;
	int index = hs_ep->index;
	int dir_in = hs_ep->dir_in;
	u32 epctrl_reg;
	u32 epsize_reg;
	u32 epsize;
	u32 ctrl;
	unsigned length;
	unsigned packets;
	unsigned maxreq;

	if (index != 0) {
		if (hs_ep->req && !continuing) {
			dev_err(hsotg->dev, "%s: active request\n", __func__);
			WARN_ON(1);
			return;
		} else if (hs_ep->req != hs_req && continuing) {
			dev_err(hsotg->dev,
				"%s: continue different req\n", __func__);
			WARN_ON(1);
			return;
		}
	}

	epctrl_reg = dir_in ? S3C_DIEPCTL(index) : S3C_DOEPCTL(index);
	epsize_reg = dir_in ? S3C_DIEPTSIZ(index) : S3C_DOEPTSIZ(index);

	dev_dbg(hsotg->dev, "%s: DxEPCTL=0x%08x, ep %d, dir %s\n",
		__func__, readl(hsotg->regs + epctrl_reg), index,
		hs_ep->dir_in ? "in" : "out");

	length = ureq->length - ureq->actual;

	if (0)
		dev_dbg(hsotg->dev,
			"REQ buf %p len %d dma 0x%08x noi=%d zp=%d snok=%d\n",
			ureq->buf, length, ureq->dma,
			ureq->no_interrupt, ureq->zero, ureq->short_not_ok);

	maxreq = get_ep_limit(hs_ep);
	if (length > maxreq) {
		int round = maxreq % hs_ep->ep.maxpacket;

		dev_dbg(hsotg->dev, "%s: length %d, max-req %d, r %d\n",
			__func__, length, maxreq, round);

		/* round down to multiple of packets */
		if (round)
			maxreq -= round;

		length = maxreq;
	}

	if (length)
		packets = DIV_ROUND_UP(length, hs_ep->ep.maxpacket);
	else
		packets = 1;	/* send one packet if length is zero. */

	if (dir_in && index != 0)
		epsize = S3C_DxEPTSIZ_MC(1);
	else
		epsize = 0;

	if (index != 0 && ureq->zero) {
		/* test for the packets being exactly right for the
		 * transfer */

		if (length == (packets * hs_ep->ep.maxpacket))
			packets++;
	}

	epsize |= S3C_DxEPTSIZ_PktCnt(packets);
	epsize |= S3C_DxEPTSIZ_XferSize(length);

	dev_dbg(hsotg->dev, "%s: %d@%d/%d, 0x%08x => 0x%08x\n",
		__func__, packets, length, ureq->length, epsize, epsize_reg);

	/* store the request as the current one we're doing */
	hs_ep->req = hs_req;

	/* write size / packets */
	writel(epsize, hsotg->regs + epsize_reg);

	ctrl = readl(hsotg->regs + epctrl_reg);

	if (ctrl & S3C_DxEPCTL_Stall) {
		dev_warn(hsotg->dev, "%s: ep%d is stalled\n", __func__, index);

		/* not sure what we can do here, if it is EP0 then we should
		 * get this cleared once the endpoint has transmitted the
		 * STALL packet, otherwise it needs to be cleared by the
		 * host.
		 */
	}

	if (using_dma(hsotg)) {
		unsigned int dma_reg;

		/* write DMA address to control register, buffer already
		 * synced by s3c_hsotg_ep_queue().  */

		dma_reg = dir_in ? S3C_DIEPDMA(index) : S3C_DOEPDMA(index);
		writel(ureq->dma, hsotg->regs + dma_reg);

		dev_dbg(hsotg->dev, "%s: 0x%08x => 0x%08x\n",
			__func__, ureq->dma, dma_reg);
	}

	ctrl |= S3C_DxEPCTL_EPEna;	/* ensure ep enabled */
	ctrl |= S3C_DxEPCTL_USBActEp;
	ctrl |= S3C_DxEPCTL_CNAK;	/* clear NAK set by core */

	dev_dbg(hsotg->dev, "%s: DxEPCTL=0x%08x\n", __func__, ctrl);
	writel(ctrl, hsotg->regs + epctrl_reg);

	/* set these, it seems that DMA support increments past the end
	 * of the packet buffer so we need to calculate the length from
	 * this information. */
	hs_ep->size_loaded = length;
	hs_ep->last_load = ureq->actual;

	if (dir_in && !using_dma(hsotg)) {
		/* set these anyway, we may need them for non-periodic in */
		hs_ep->fifo_load = 0;

		s3c_hsotg_write_fifo(hsotg, hs_ep, hs_req);
	}

	/* clear the INTknTXFEmpMsk when we start request, more as a aide
	 * to debugging to see what is going on. */
	if (dir_in)
		writel(S3C_DIEPMSK_INTknTXFEmpMsk,
		       hsotg->regs + S3C_DIEPINT(index));

	/* Note, trying to clear the NAK here causes problems with transmit
	 * on the S3C6400 ending up with the TXFIFO becomming full. */

	/* check ep is enabled */
	if (!(readl(hsotg->regs + epctrl_reg) & S3C_DxEPCTL_EPEna))
		dev_warn(hsotg->dev,
			 "ep%d: failed to become enabled (DxEPCTL=0x%08x)?\n",
			 index, readl(hsotg->regs + epctrl_reg));

	dev_dbg(hsotg->dev, "%s: DxEPCTL=0x%08x\n",
		__func__, readl(hsotg->regs + epctrl_reg));
}

/**
 * s3c_hsotg_map_dma - map the DMA memory being used for the request
 * @hsotg: The device state.
 * @hs_ep: The endpoint the request is on.
 * @req: The request being processed.
 *
 * We've been asked to queue a request, so ensure that the memory buffer
 * is correctly setup for DMA. If we've been passed an extant DMA address
 * then ensure the buffer has been synced to memory. If our buffer has no
 * DMA memory, then we map the memory and mark our request to allow us to
 * cleanup on completion.
*/
static int s3c_hsotg_map_dma(struct s3c_hsotg *hsotg,
			     struct s3c_hsotg_ep *hs_ep,
			     struct usb_request *req)
{
	enum dma_data_direction dir;
	struct s3c_hsotg_req *hs_req = our_req(req);

	dir = hs_ep->dir_in ? DMA_TO_DEVICE : DMA_FROM_DEVICE;

	/* if the length is zero, ignore the DMA data */
	if (hs_req->req.length == 0)
		return 0;

	if (req->dma == DMA_ADDR_INVALID) {
		dma_addr_t dma;

		dma = dma_map_single(hsotg->dev, req->buf, req->length, dir);

		if (unlikely(dma_mapping_error(hsotg->dev, dma)))
			goto dma_error;

		if (dma & 3) {
			dev_err(hsotg->dev, "%s: unaligned dma buffer\n",
				__func__);

			dma_unmap_single(hsotg->dev, dma, req->length, dir);
			return -EINVAL;
		}

		hs_req->mapped = 1;
		req->dma = dma;
	} else {
		dma_sync_single(hsotg->dev, req->dma, req->length, dir);
		hs_req->mapped = 0;
	}

	return 0;

dma_error:
	dev_err(hsotg->dev, "%s: failed to map buffer %p, %d bytes\n",
		__func__, req->buf, req->length);

	return -EIO;
}

static int s3c_hsotg_ep_queue(struct usb_ep *ep, struct usb_request *req,
			      gfp_t gfp_flags)
{
	struct s3c_hsotg_req *hs_req = our_req(req);
	struct s3c_hsotg_ep *hs_ep = our_ep(ep);
	struct s3c_hsotg *hs = hs_ep->parent;
	unsigned long irqflags;
	bool first;

	dev_dbg(hs->dev, "%s: req %p: %d@%p, noi=%d, zero=%d, snok=%d\n",
		ep->name, req, req->length, req->buf, req->no_interrupt,
		req->zero, req->short_not_ok);

	/* initialise status of the request */
	INIT_LIST_HEAD(&hs_req->queue);
	req->actual = 0;
	req->status = -EINPROGRESS;

	/* if we're using DMA, sync the buffers as necessary */
	if (using_dma(hs)) {
		int ret = s3c_hsotg_map_dma(hs, hs_ep, req);
		if (ret)
			return ret;
	}

	spin_lock_irqsave(&hs_ep->lock, irqflags);

	first = list_empty(&hs_ep->queue);
	list_add_tail(&hs_req->queue, &hs_ep->queue);

	if (first)
		s3c_hsotg_start_req(hs, hs_ep, hs_req, false);

	spin_unlock_irqrestore(&hs_ep->lock, irqflags);

	return 0;
}

static void s3c_hsotg_ep_free_request(struct usb_ep *ep,
				      struct usb_request *req)
{
	struct s3c_hsotg_req *hs_req = our_req(req);

	kfree(hs_req);
}

/**
 * s3c_hsotg_complete_oursetup - setup completion callback
 * @ep: The endpoint the request was on.
 * @req: The request completed.
 *
 * Called on completion of any requests the driver itself
 * submitted that need cleaning up.
 */
static void s3c_hsotg_complete_oursetup(struct usb_ep *ep,
					struct usb_request *req)
{
	struct s3c_hsotg_ep *hs_ep = our_ep(ep);
	struct s3c_hsotg *hsotg = hs_ep->parent;

	dev_dbg(hsotg->dev, "%s: ep %p, req %p\n", __func__, ep, req);

	s3c_hsotg_ep_free_request(ep, req);
}

/**
 * ep_from_windex - convert control wIndex value to endpoint
 * @hsotg: The driver state.
 * @windex: The control request wIndex field (in host order).
 *
 * Convert the given wIndex into a pointer to an driver endpoint
 * structure, or return NULL if it is not a valid endpoint.
*/
static struct s3c_hsotg_ep *ep_from_windex(struct s3c_hsotg *hsotg,
					   u32 windex)
{
	struct s3c_hsotg_ep *ep = &hsotg->eps[windex & 0x7F];
	int dir = (windex & USB_DIR_IN) ? 1 : 0;
	int idx = windex & 0x7F;

	if (windex >= 0x100)
		return NULL;

	if (idx > S3C_HSOTG_EPS)
		return NULL;

	if (idx && ep->dir_in != dir)
		return NULL;

	return ep;
}

/**
 * s3c_hsotg_send_reply - send reply to control request
 * @hsotg: The device state
 * @ep: Endpoint 0
 * @buff: Buffer for request
 * @length: Length of reply.
 *
 * Create a request and queue it on the given endpoint. This is useful as
 * an internal method of sending replies to certain control requests, etc.
 */
static int s3c_hsotg_send_reply(struct s3c_hsotg *hsotg,
				struct s3c_hsotg_ep *ep,
				void *buff,
				int length)
{
	struct usb_request *req;
	int ret;

	dev_dbg(hsotg->dev, "%s: buff %p, len %d\n", __func__, buff, length);

	req = s3c_hsotg_ep_alloc_request(&ep->ep, GFP_ATOMIC);
	hsotg->ep0_reply = req;
	if (!req) {
		dev_warn(hsotg->dev, "%s: cannot alloc req\n", __func__);
		return -ENOMEM;
	}

	req->buf = hsotg->ep0_buff;
	req->length = length;
	req->zero = 1; /* always do zero-length final transfer */
	req->complete = s3c_hsotg_complete_oursetup;

	if (length)
		memcpy(req->buf, buff, length);
	else
		ep->sent_zlp = 1;

	ret = s3c_hsotg_ep_queue(&ep->ep, req, GFP_ATOMIC);
	if (ret) {
		dev_warn(hsotg->dev, "%s: cannot queue req\n", __func__);
		return ret;
	}

	return 0;
}

/**
 * s3c_hsotg_process_req_status - process request GET_STATUS
 * @hsotg: The device state
 * @ctrl: USB control request
 */
static int s3c_hsotg_process_req_status(struct s3c_hsotg *hsotg,
					struct usb_ctrlrequest *ctrl)
{
	struct s3c_hsotg_ep *ep0 = &hsotg->eps[0];
	struct s3c_hsotg_ep *ep;
	__le16 reply;
	int ret;

	dev_dbg(hsotg->dev, "%s: USB_REQ_GET_STATUS\n", __func__);

	if (!ep0->dir_in) {
		dev_warn(hsotg->dev, "%s: direction out?\n", __func__);
		return -EINVAL;
	}

	switch (ctrl->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		reply = cpu_to_le16(0); /* bit 0 => self powered,
					 * bit 1 => remote wakeup */
		break;

	case USB_RECIP_INTERFACE:
		/* currently, the data result should be zero */
		reply = cpu_to_le16(0);
		break;

	case USB_RECIP_ENDPOINT:
		ep = ep_from_windex(hsotg, le16_to_cpu(ctrl->wIndex));
		if (!ep)
			return -ENOENT;

		reply = cpu_to_le16(ep->halted ? 1 : 0);
		break;

	default:
		return 0;
	}

	if (le16_to_cpu(ctrl->wLength) != 2)
		return -EINVAL;

	ret = s3c_hsotg_send_reply(hsotg, ep0, &reply, 2);
	if (ret) {
		dev_err(hsotg->dev, "%s: failed to send reply\n", __func__);
		return ret;
	}

	return 1;
}

static int s3c_hsotg_ep_sethalt(struct usb_ep *ep, int value);

/**
 * s3c_hsotg_process_req_featire - process request {SET,CLEAR}_FEATURE
 * @hsotg: The device state
 * @ctrl: USB control request
 */
static int s3c_hsotg_process_req_feature(struct s3c_hsotg *hsotg,
					 struct usb_ctrlrequest *ctrl)
{
	bool set = (ctrl->bRequest == USB_REQ_SET_FEATURE);
	struct s3c_hsotg_ep *ep;

	dev_dbg(hsotg->dev, "%s: %s_FEATURE\n",
		__func__, set ? "SET" : "CLEAR");

	if (ctrl->bRequestType == USB_RECIP_ENDPOINT) {
		ep = ep_from_windex(hsotg, le16_to_cpu(ctrl->wIndex));
		if (!ep) {
			dev_dbg(hsotg->dev, "%s: no endpoint for 0x%04x\n",
				__func__, le16_to_cpu(ctrl->wIndex));
			return -ENOENT;
		}

		switch (le16_to_cpu(ctrl->wValue)) {
		case USB_ENDPOINT_HALT:
			s3c_hsotg_ep_sethalt(&ep->ep, set);
			break;

		default:
			return -ENOENT;
		}
	} else
		return -ENOENT;  /* currently only deal with endpoint */

	return 1;
}

/**
 * s3c_hsotg_process_control - process a control request
 * @hsotg: The device state
 * @ctrl: The control request received
 *
 * The controller has received the SETUP phase of a control request, and
 * needs to work out what to do next (and whether to pass it on to the
 * gadget driver).
 */
static void s3c_hsotg_process_control(struct s3c_hsotg *hsotg,
				      struct usb_ctrlrequest *ctrl)
{
	struct s3c_hsotg_ep *ep0 = &hsotg->eps[0];
	int ret = 0;
	u32 dcfg;

	ep0->sent_zlp = 0;

	dev_dbg(hsotg->dev, "ctrl Req=%02x, Type=%02x, V=%04x, L=%04x\n",
		 ctrl->bRequest, ctrl->bRequestType,
		 ctrl->wValue, ctrl->wLength);

	/* record the direction of the request, for later use when enquing
	 * packets onto EP0. */

	ep0->dir_in = (ctrl->bRequestType & USB_DIR_IN) ? 1 : 0;
	dev_dbg(hsotg->dev, "ctrl: dir_in=%d\n", ep0->dir_in);

	/* if we've no data with this request, then the last part of the
	 * transaction is going to implicitly be IN. */
	if (ctrl->wLength == 0)
		ep0->dir_in = 1;

	if ((ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD) {
		switch (ctrl->bRequest) {
		case USB_REQ_SET_ADDRESS:
			dcfg = readl(hsotg->regs + S3C_DCFG);
			dcfg &= ~S3C_DCFG_DevAddr_MASK;
			dcfg |= ctrl->wValue << S3C_DCFG_DevAddr_SHIFT;
			writel(dcfg, hsotg->regs + S3C_DCFG);

			dev_info(hsotg->dev, "new address %d\n", ctrl->wValue);

			ret = s3c_hsotg_send_reply(hsotg, ep0, NULL, 0);
			return;

		case USB_REQ_GET_STATUS:
			ret = s3c_hsotg_process_req_status(hsotg, ctrl);
			break;

		case USB_REQ_CLEAR_FEATURE:
		case USB_REQ_SET_FEATURE:
			ret = s3c_hsotg_process_req_feature(hsotg, ctrl);
			break;
		}
	}

	/* as a fallback, try delivering it to the driver to deal with */

	if (ret == 0 && hsotg->driver) {
		ret = hsotg->driver->setup(&hsotg->gadget, ctrl);
		if (ret < 0)
			dev_dbg(hsotg->dev, "driver->setup() ret %d\n", ret);
	}

	if (ret > 0) {
		if (!ep0->dir_in) {
			/* need to generate zlp in reply or take data */
			/* todo - deal with any data we might be sent? */
			ret = s3c_hsotg_send_reply(hsotg, ep0, NULL, 0);
		}
	}

	/* the request is either unhandlable, or is not formatted correctly
	 * so respond with a STALL for the status stage to indicate failure.
	 */

	if (ret < 0) {
		u32 reg;
		u32 ctrl;

		dev_dbg(hsotg->dev, "ep0 stall (dir=%d)\n", ep0->dir_in);
		reg = (ep0->dir_in) ? S3C_DIEPCTL0 : S3C_DOEPCTL0;

		/* S3C_DxEPCTL_Stall will be cleared by EP once it has
		 * taken effect, so no need to clear later. */

		ctrl = readl(hsotg->regs + reg);
		ctrl |= S3C_DxEPCTL_Stall;
		ctrl |= S3C_DxEPCTL_CNAK;
		writel(ctrl, hsotg->regs + reg);

		dev_dbg(hsotg->dev,
			"writen DxEPCTL=0x%08x to %08x (DxEPCTL=0x%08x)\n",
			ctrl, reg, readl(hsotg->regs + reg));

		/* don't belive we need to anything more to get the EP
		 * to reply with a STALL packet */
	}
}

static void s3c_hsotg_enqueue_setup(struct s3c_hsotg *hsotg);

/**
 * s3c_hsotg_complete_setup - completion of a setup transfer
 * @ep: The endpoint the request was on.
 * @req: The request completed.
 *
 * Called on completion of any requests the driver itself submitted for
 * EP0 setup packets
 */
static void s3c_hsotg_complete_setup(struct usb_ep *ep,
				     struct usb_request *req)
{
	struct s3c_hsotg_ep *hs_ep = our_ep(ep);
	struct s3c_hsotg *hsotg = hs_ep->parent;

	if (req->status < 0) {
		dev_dbg(hsotg->dev, "%s: failed %d\n", __func__, req->status);
		return;
	}

	if (req->actual == 0)
		s3c_hsotg_enqueue_setup(hsotg);
	else
		s3c_hsotg_process_control(hsotg, req->buf);
}

/**
 * s3c_hsotg_enqueue_setup - start a request for EP0 packets
 * @hsotg: The device state.
 *
 * Enqueue a request on EP0 if necessary to received any SETUP packets
 * received from the host.
 */
static void s3c_hsotg_enqueue_setup(struct s3c_hsotg *hsotg)
{
	struct usb_request *req = hsotg->ctrl_req;
	struct s3c_hsotg_req *hs_req = our_req(req);
	int ret;

	dev_dbg(hsotg->dev, "%s: queueing setup request\n", __func__);

	req->zero = 0;
	req->length = 8;
	req->buf = hsotg->ctrl_buff;
	req->complete = s3c_hsotg_complete_setup;

	if (!list_empty(&hs_req->queue)) {
		dev_dbg(hsotg->dev, "%s already queued???\n", __func__);
		return;
	}

	hsotg->eps[0].dir_in = 0;

	ret = s3c_hsotg_ep_queue(&hsotg->eps[0].ep, req, GFP_ATOMIC);
	if (ret < 0) {
		dev_err(hsotg->dev, "%s: failed queue (%d)\n", __func__, ret);
		/* Don't think there's much we can do other than watch the
		 * driver fail. */
	}
}

/**
 * get_ep_head - return the first request on the endpoint
 * @hs_ep: The controller endpoint to get
 *
 * Get the first request on the endpoint.
*/
static struct s3c_hsotg_req *get_ep_head(struct s3c_hsotg_ep *hs_ep)
{
	if (list_empty(&hs_ep->queue))
		return NULL;

	return list_first_entry(&hs_ep->queue, struct s3c_hsotg_req, queue);
}

/**
 * s3c_hsotg_complete_request - complete a request given to us
 * @hsotg: The device state.
 * @hs_ep: The endpoint the request was on.
 * @hs_req: The request to complete.
 * @result: The result code (0 => Ok, otherwise errno)
 *
 * The given request has finished, so call the necessary completion
 * if it has one and then look to see if we can start a new request
 * on the endpoint.
 *
 * Note, expects the ep to already be locked as appropriate.
*/
static void s3c_hsotg_complete_request(struct s3c_hsotg *hsotg,
				       struct s3c_hsotg_ep *hs_ep,
				       struct s3c_hsotg_req *hs_req,
				       int result)
{
	bool restart;

	if (!hs_req) {
		dev_dbg(hsotg->dev, "%s: nothing to complete?\n", __func__);
		return;
	}

	dev_dbg(hsotg->dev, "complete: ep %p %s, req %p, %d => %p\n",
		hs_ep, hs_ep->ep.name, hs_req, result, hs_req->req.complete);

	/* only replace the status if we've not already set an error
	 * from a previous transaction */

	if (hs_req->req.status == -EINPROGRESS)
		hs_req->req.status = result;

	hs_ep->req = NULL;
	list_del_init(&hs_req->queue);

	if (using_dma(hsotg))
		s3c_hsotg_unmap_dma(hsotg, hs_ep, hs_req);

	/* call the complete request with the locks off, just in case the
	 * request tries to queue more work for this endpoint. */

	if (hs_req->req.complete) {
		spin_unlock(&hs_ep->lock);
		hs_req->req.complete(&hs_ep->ep, &hs_req->req);
		spin_lock(&hs_ep->lock);
	}

	/* Look to see if there is anything else to do. Note, the completion
	 * of the previous request may have caused a new request to be started
	 * so be careful when doing this. */

	if (!hs_ep->req && result >= 0) {
		restart = !list_empty(&hs_ep->queue);
		if (restart) {
			hs_req = get_ep_head(hs_ep);
			s3c_hsotg_start_req(hsotg, hs_ep, hs_req, false);
		}
	}
}

/**
 * s3c_hsotg_complete_request_lock - complete a request given to us (locked)
 * @hsotg: The device state.
 * @hs_ep: The endpoint the request was on.
 * @hs_req: The request to complete.
 * @result: The result code (0 => Ok, otherwise errno)
 *
 * See s3c_hsotg_complete_request(), but called with the endpoint's
 * lock held.
*/
static void s3c_hsotg_complete_request_lock(struct s3c_hsotg *hsotg,
					    struct s3c_hsotg_ep *hs_ep,
					    struct s3c_hsotg_req *hs_req,
					    int result)
{
	unsigned long flags;

	spin_lock_irqsave(&hs_ep->lock, flags);
	s3c_hsotg_complete_request(hsotg, hs_ep, hs_req, result);
	spin_unlock_irqrestore(&hs_ep->lock, flags);
}

/**
 * s3c_hsotg_rx_data - receive data from the FIFO for an endpoint
 * @hsotg: The device state.
 * @ep_idx: The endpoint index for the data
 * @size: The size of data in the fifo, in bytes
 *
 * The FIFO status shows there is data to read from the FIFO for a given
 * endpoint, so sort out whether we need to read the data into a request
 * that has been made for that endpoint.
 */
static void s3c_hsotg_rx_data(struct s3c_hsotg *hsotg, int ep_idx, int size)
{
	struct s3c_hsotg_ep *hs_ep = &hsotg->eps[ep_idx];
	struct s3c_hsotg_req *hs_req = hs_ep->req;
	void __iomem *fifo = hsotg->regs + S3C_EPFIFO(ep_idx);
	int to_read;
	int max_req;
	int read_ptr;

	if (!hs_req) {
		u32 epctl = readl(hsotg->regs + S3C_DOEPCTL(ep_idx));
		int ptr;

		dev_warn(hsotg->dev,
			 "%s: FIFO %d bytes on ep%d but no req (DxEPCTl=0x%08x)\n",
			 __func__, size, ep_idx, epctl);

		/* dump the data from the FIFO, we've nothing we can do */
		for (ptr = 0; ptr < size; ptr += 4)
			(void)readl(fifo);

		return;
	}

	spin_lock(&hs_ep->lock);

	to_read = size;
	read_ptr = hs_req->req.actual;
	max_req = hs_req->req.length - read_ptr;

	if (to_read > max_req) {
		/* more data appeared than we where willing
		 * to deal with in this request.
		 */

		/* currently we don't deal this */
		WARN_ON_ONCE(1);
	}

	dev_dbg(hsotg->dev, "%s: read %d/%d, done %d/%d\n",
		__func__, to_read, max_req, read_ptr, hs_req->req.length);

	hs_ep->total_data += to_read;
	hs_req->req.actual += to_read;
	to_read = DIV_ROUND_UP(to_read, 4);

	/* note, we might over-write the buffer end by 3 bytes depending on
	 * alignment of the data. */
	readsl(fifo, hs_req->req.buf + read_ptr, to_read);

	spin_unlock(&hs_ep->lock);
}

/**
 * s3c_hsotg_send_zlp - send zero-length packet on control endpoint
 * @hsotg: The device instance
 * @req: The request currently on this endpoint
 *
 * Generate a zero-length IN packet request for terminating a SETUP
 * transaction.
 *
 * Note, since we don't write any data to the TxFIFO, then it is
 * currently belived that we do not need to wait for any space in
 * the TxFIFO.
 */
static void s3c_hsotg_send_zlp(struct s3c_hsotg *hsotg,
			       struct s3c_hsotg_req *req)
{
	u32 ctrl;

	if (!req) {
		dev_warn(hsotg->dev, "%s: no request?\n", __func__);
		return;
	}

	if (req->req.length == 0) {
		hsotg->eps[0].sent_zlp = 1;
		s3c_hsotg_enqueue_setup(hsotg);
		return;
	}

	hsotg->eps[0].dir_in = 1;
	hsotg->eps[0].sent_zlp = 1;

	dev_dbg(hsotg->dev, "sending zero-length packet\n");

	/* issue a zero-sized packet to terminate this */
	writel(S3C_DxEPTSIZ_MC(1) | S3C_DxEPTSIZ_PktCnt(1) |
	       S3C_DxEPTSIZ_XferSize(0), hsotg->regs + S3C_DIEPTSIZ(0));

	ctrl = readl(hsotg->regs + S3C_DIEPCTL0);
	ctrl |= S3C_DxEPCTL_CNAK;  /* clear NAK set by core */
	ctrl |= S3C_DxEPCTL_EPEna; /* ensure ep enabled */
	ctrl |= S3C_DxEPCTL_USBActEp;
	writel(ctrl, hsotg->regs + S3C_DIEPCTL0);
}

/**
 * s3c_hsotg_handle_outdone - handle receiving OutDone/SetupDone from RXFIFO
 * @hsotg: The device instance
 * @epnum: The endpoint received from
 * @was_setup: Set if processing a SetupDone event.
 *
 * The RXFIFO has delivered an OutDone event, which means that the data
 * transfer for an OUT endpoint has been completed, either by a short
 * packet or by the finish of a transfer.
*/
static void s3c_hsotg_handle_outdone(struct s3c_hsotg *hsotg,
				     int epnum, bool was_setup)
{
	struct s3c_hsotg_ep *hs_ep = &hsotg->eps[epnum];
	struct s3c_hsotg_req *hs_req = hs_ep->req;
	struct usb_request *req = &hs_req->req;
	int result = 0;

	if (!hs_req) {
		dev_dbg(hsotg->dev, "%s: no request active\n", __func__);
		return;
	}

	if (using_dma(hsotg)) {
		u32 epsize = readl(hsotg->regs + S3C_DOEPTSIZ(epnum));
		unsigned size_done;
		unsigned size_left;

		/* Calculate the size of the transfer by checking how much
		 * is left in the endpoint size register and then working it
		 * out from the amount we loaded for the transfer.
		 *
		 * We need to do this as DMA pointers are always 32bit aligned
		 * so may overshoot/undershoot the transfer.
		 */

		size_left = S3C_DxEPTSIZ_XferSize_GET(epsize);

		size_done = hs_ep->size_loaded - size_left;
		size_done += hs_ep->last_load;

		req->actual = size_done;
	}

	if (req->actual < req->length && req->short_not_ok) {
		dev_dbg(hsotg->dev, "%s: got %d/%d (short not ok) => error\n",
			__func__, req->actual, req->length);

		/* todo - what should we return here? there's no one else
		 * even bothering to check the status. */
	}

	if (epnum == 0) {
		if (!was_setup && req->complete != s3c_hsotg_complete_setup)
			s3c_hsotg_send_zlp(hsotg, hs_req);
	}

	s3c_hsotg_complete_request_lock(hsotg, hs_ep, hs_req, result);
}

/**
 * s3c_hsotg_read_frameno - read current frame number
 * @hsotg: The device instance
 *
 * Return the current frame number
*/
static u32 s3c_hsotg_read_frameno(struct s3c_hsotg *hsotg)
{
	u32 dsts;

	dsts = readl(hsotg->regs + S3C_DSTS);
	dsts &= S3C_DSTS_SOFFN_MASK;
	dsts >>= S3C_DSTS_SOFFN_SHIFT;

	return dsts;
}

/**
 * s3c_hsotg_handle_rx - RX FIFO has data
 * @hsotg: The device instance
 *
 * The IRQ handler has detected that the RX FIFO has some data in it
 * that requires processing, so find out what is in there and do the
 * appropriate read.
 *
 * The RXFIFO is a true FIFO, the packets comming out are still in packet
 * chunks, so if you have x packets received on an endpoint you'll get x
 * FIFO events delivered, each with a packet's worth of data in it.
 *
 * When using DMA, we should not be processing events from the RXFIFO
 * as the actual data should be sent to the memory directly and we turn
 * on the completion interrupts to get notifications of transfer completion.
 */
static void s3c_hsotg_handle_rx(struct s3c_hsotg *hsotg)
{
	u32 grxstsr = readl(hsotg->regs + S3C_GRXSTSP);
	u32 epnum, status, size;

	WARN_ON(using_dma(hsotg));

	epnum = grxstsr & S3C_GRXSTS_EPNum_MASK;
	status = grxstsr & S3C_GRXSTS_PktSts_MASK;

	size = grxstsr & S3C_GRXSTS_ByteCnt_MASK;
	size >>= S3C_GRXSTS_ByteCnt_SHIFT;

	if (1)
		dev_dbg(hsotg->dev, "%s: GRXSTSP=0x%08x (%d@%d)\n",
			__func__, grxstsr, size, epnum);

#define __status(x) ((x) >> S3C_GRXSTS_PktSts_SHIFT)

	switch (status >> S3C_GRXSTS_PktSts_SHIFT) {
	case __status(S3C_GRXSTS_PktSts_GlobalOutNAK):
		dev_dbg(hsotg->dev, "GlobalOutNAK\n");
		break;

	case __status(S3C_GRXSTS_PktSts_OutDone):
		dev_dbg(hsotg->dev, "OutDone (Frame=0x%08x)\n",
			s3c_hsotg_read_frameno(hsotg));

		if (!using_dma(hsotg))
			s3c_hsotg_handle_outdone(hsotg, epnum, false);
		break;

	case __status(S3C_GRXSTS_PktSts_SetupDone):
		dev_dbg(hsotg->dev,
			"SetupDone (Frame=0x%08x, DOPEPCTL=0x%08x)\n",
			s3c_hsotg_read_frameno(hsotg),
			readl(hsotg->regs + S3C_DOEPCTL(0)));

		s3c_hsotg_handle_outdone(hsotg, epnum, true);
		break;

	case __status(S3C_GRXSTS_PktSts_OutRX):
		s3c_hsotg_rx_data(hsotg, epnum, size);
		break;

	case __status(S3C_GRXSTS_PktSts_SetupRX):
		dev_dbg(hsotg->dev,
			"SetupRX (Frame=0x%08x, DOPEPCTL=0x%08x)\n",
			s3c_hsotg_read_frameno(hsotg),
			readl(hsotg->regs + S3C_DOEPCTL(0)));

		s3c_hsotg_rx_data(hsotg, epnum, size);
		break;

	default:
		dev_warn(hsotg->dev, "%s: unknown status %08x\n",
			 __func__, grxstsr);

		s3c_hsotg_dump(hsotg);
		break;
	}
}

/**
 * s3c_hsotg_ep0_mps - turn max packet size into register setting
 * @mps: The maximum packet size in bytes.
*/
static u32 s3c_hsotg_ep0_mps(unsigned int mps)
{
	switch (mps) {
	case 64:
		return S3C_D0EPCTL_MPS_64;
	case 32:
		return S3C_D0EPCTL_MPS_32;
	case 16:
		return S3C_D0EPCTL_MPS_16;
	case 8:
		return S3C_D0EPCTL_MPS_8;
	}

	/* bad max packet size, warn and return invalid result */
	WARN_ON(1);
	return (u32)-1;
}

/**
 * s3c_hsotg_set_ep_maxpacket - set endpoint's max-packet field
 * @hsotg: The driver state.
 * @ep: The index number of the endpoint
 * @mps: The maximum packet size in bytes
 *
 * Configure the maximum packet size for the given endpoint, updating
 * the hardware control registers to reflect this.
 */
static void s3c_hsotg_set_ep_maxpacket(struct s3c_hsotg *hsotg,
				       unsigned int ep, unsigned int mps)
{
	struct s3c_hsotg_ep *hs_ep = &hsotg->eps[ep];
	void __iomem *regs = hsotg->regs;
	u32 mpsval;
	u32 reg;

	if (ep == 0) {
		/* EP0 is a special case */
		mpsval = s3c_hsotg_ep0_mps(mps);
		if (mpsval > 3)
			goto bad_mps;
	} else {
		if (mps >= S3C_DxEPCTL_MPS_LIMIT+1)
			goto bad_mps;

		mpsval = mps;
	}

	hs_ep->ep.maxpacket = mps;

	/* update both the in and out endpoint controldir_ registers, even
	 * if one of the directions may not be in use. */

	reg = readl(regs + S3C_DIEPCTL(ep));
	reg &= ~S3C_DxEPCTL_MPS_MASK;
	reg |= mpsval;
	writel(reg, regs + S3C_DIEPCTL(ep));

	reg = readl(regs + S3C_DOEPCTL(ep));
	reg &= ~S3C_DxEPCTL_MPS_MASK;
	reg |= mpsval;
	writel(reg, regs + S3C_DOEPCTL(ep));

	return;

bad_mps:
	dev_err(hsotg->dev, "ep%d: bad mps of %d\n", ep, mps);
}


/**
 * s3c_hsotg_trytx - check to see if anything needs transmitting
 * @hsotg: The driver state
 * @hs_ep: The driver endpoint to check.
 *
 * Check to see if there is a request that has data to send, and if so
 * make an attempt to write data into the FIFO.
 */
static int s3c_hsotg_trytx(struct s3c_hsotg *hsotg,
			   struct s3c_hsotg_ep *hs_ep)
{
	struct s3c_hsotg_req *hs_req = hs_ep->req;

	if (!hs_ep->dir_in || !hs_req)
		return 0;

	if (hs_req->req.actual < hs_req->req.length) {
		dev_dbg(hsotg->dev, "trying to write more for ep%d\n",
			hs_ep->index);
		return s3c_hsotg_write_fifo(hsotg, hs_ep, hs_req);
	}

	return 0;
}

/**
 * s3c_hsotg_complete_in - complete IN transfer
 * @hsotg: The device state.
 * @hs_ep: The endpoint that has just completed.
 *
 * An IN transfer has been completed, update the transfer's state and then
 * call the relevant completion routines.
 */
static void s3c_hsotg_complete_in(struct s3c_hsotg *hsotg,
				  struct s3c_hsotg_ep *hs_ep)
{
	struct s3c_hsotg_req *hs_req = hs_ep->req;
	u32 epsize = readl(hsotg->regs + S3C_DIEPTSIZ(hs_ep->index));
	int size_left, size_done;

	if (!hs_req) {
		dev_dbg(hsotg->dev, "XferCompl but no req\n");
		return;
	}

	/* Calculate the size of the transfer by checking how much is left
	 * in the endpoint size register and then working it out from
	 * the amount we loaded for the transfer.
	 *
	 * We do this even for DMA, as the transfer may have incremented
	 * past the end of the buffer (DMA transfers are always 32bit
	 * aligned).
	 */

	size_left = S3C_DxEPTSIZ_XferSize_GET(epsize);

	size_done = hs_ep->size_loaded - size_left;
	size_done += hs_ep->last_load;

	if (hs_req->req.actual != size_done)
		dev_dbg(hsotg->dev, "%s: adjusting size done %d => %d\n",
			__func__, hs_req->req.actual, size_done);

	hs_req->req.actual = size_done;

	/* if we did all of the transfer, and there is more data left
	 * around, then try restarting the rest of the request */

	if (!size_left && hs_req->req.actual < hs_req->req.length) {
		dev_dbg(hsotg->dev, "%s trying more for req...\n", __func__);
		s3c_hsotg_start_req(hsotg, hs_ep, hs_req, true);
	} else
		s3c_hsotg_complete_request_lock(hsotg, hs_ep, hs_req, 0);
}

/**
 * s3c_hsotg_epint - handle an in/out endpoint interrupt
 * @hsotg: The driver state
 * @idx: The index for the endpoint (0..15)
 * @dir_in: Set if this is an IN endpoint
 *
 * Process and clear any interrupt pending for an individual endpoint
*/
static void s3c_hsotg_epint(struct s3c_hsotg *hsotg, unsigned int idx,
			    int dir_in)
{
	struct s3c_hsotg_ep *hs_ep = &hsotg->eps[idx];
	u32 epint_reg = dir_in ? S3C_DIEPINT(idx) : S3C_DOEPINT(idx);
	u32 epctl_reg = dir_in ? S3C_DIEPCTL(idx) : S3C_DOEPCTL(idx);
	u32 epsiz_reg = dir_in ? S3C_DIEPTSIZ(idx) : S3C_DOEPTSIZ(idx);
	u32 ints;
	u32 clear = 0;

	ints = readl(hsotg->regs + epint_reg);

	dev_dbg(hsotg->dev, "%s: ep%d(%s) DxEPINT=0x%08x\n",
		__func__, idx, dir_in ? "in" : "out", ints);

	if (ints & S3C_DxEPINT_XferCompl) {
		dev_dbg(hsotg->dev,
			"%s: XferCompl: DxEPCTL=0x%08x, DxEPTSIZ=%08x\n",
			__func__, readl(hsotg->regs + epctl_reg),
			readl(hsotg->regs + epsiz_reg));

		/* we get OutDone from the FIFO, so we only need to look
		 * at completing IN requests here */
		if (dir_in) {
			s3c_hsotg_complete_in(hsotg, hs_ep);

			if (idx == 0)
				s3c_hsotg_enqueue_setup(hsotg);
		} else if (using_dma(hsotg)) {
			/* We're using DMA, we need to fire an OutDone here
			 * as we ignore the RXFIFO. */

			s3c_hsotg_handle_outdone(hsotg, idx, false);
		}

		clear |= S3C_DxEPINT_XferCompl;
	}

	if (ints & S3C_DxEPINT_EPDisbld) {
		dev_dbg(hsotg->dev, "%s: EPDisbld\n", __func__);
		clear |= S3C_DxEPINT_EPDisbld;
	}

	if (ints & S3C_DxEPINT_AHBErr) {
		dev_dbg(hsotg->dev, "%s: AHBErr\n", __func__);
		clear |= S3C_DxEPINT_AHBErr;
	}

	if (ints & S3C_DxEPINT_Setup) {  /* Setup or Timeout */
		dev_dbg(hsotg->dev, "%s: Setup/Timeout\n",  __func__);

		if (using_dma(hsotg) && idx == 0) {
			/* this is the notification we've received a
			 * setup packet. In non-DMA mode we'd get this
			 * from the RXFIFO, instead we need to process
			 * the setup here. */

			if (dir_in)
				WARN_ON_ONCE(1);
			else
				s3c_hsotg_handle_outdone(hsotg, 0, true);
		}

		clear |= S3C_DxEPINT_Setup;
	}

	if (ints & S3C_DxEPINT_Back2BackSetup) {
		dev_dbg(hsotg->dev, "%s: B2BSetup/INEPNakEff\n", __func__);
		clear |= S3C_DxEPINT_Back2BackSetup;
	}

	if (dir_in) {
		/* not sure if this is important, but we'll clear it anyway
		 */
		if (ints & S3C_DIEPMSK_INTknTXFEmpMsk) {
			dev_dbg(hsotg->dev, "%s: ep%d: INTknTXFEmpMsk\n",
				__func__, idx);
			clear |= S3C_DIEPMSK_INTknTXFEmpMsk;
		}

		/* this probably means something bad is happening */
		if (ints & S3C_DIEPMSK_INTknEPMisMsk) {
			dev_warn(hsotg->dev, "%s: ep%d: INTknEP\n",
				 __func__, idx);
			clear |= S3C_DIEPMSK_INTknEPMisMsk;
		}
	}

	writel(clear, hsotg->regs + epint_reg);
}

/**
 * s3c_hsotg_irq_enumdone - Handle EnumDone interrupt (enumeration done)
 * @hsotg: The device state.
 *
 * Handle updating the device settings after the enumeration phase has
 * been completed.
*/
static void s3c_hsotg_irq_enumdone(struct s3c_hsotg *hsotg)
{
	u32 dsts = readl(hsotg->regs + S3C_DSTS);
	int ep0_mps = 0, ep_mps;

	/* This should signal the finish of the enumeration phase
	 * of the USB handshaking, so we should now know what rate
	 * we connected at. */

	dev_dbg(hsotg->dev, "EnumDone (DSTS=0x%08x)\n", dsts);

	/* note, since we're limited by the size of transfer on EP0, and
	 * it seems IN transfers must be a even number of packets we do
	 * not advertise a 64byte MPS on EP0. */

	/* catch both EnumSpd_FS and EnumSpd_FS48 */
	switch (dsts & S3C_DSTS_EnumSpd_MASK) {
	case S3C_DSTS_EnumSpd_FS:
	case S3C_DSTS_EnumSpd_FS48:
		hsotg->gadget.speed = USB_SPEED_FULL;
		dev_info(hsotg->dev, "new device is full-speed\n");

		ep0_mps = EP0_MPS_LIMIT;
		ep_mps = 64;
		break;

	case S3C_DSTS_EnumSpd_HS:
		dev_info(hsotg->dev, "new device is high-speed\n");
		hsotg->gadget.speed = USB_SPEED_HIGH;

		ep0_mps = EP0_MPS_LIMIT;
		ep_mps = 512;
		break;

	case S3C_DSTS_EnumSpd_LS:
		hsotg->gadget.speed = USB_SPEED_LOW;
		dev_info(hsotg->dev, "new device is low-speed\n");

		/* note, we don't actually support LS in this driver at the
		 * moment, and the documentation seems to imply that it isn't
		 * supported by the PHYs on some of the devices.
		 */
		break;
	}

	/* we should now know the maximum packet size for an
	 * endpoint, so set the endpoints to a default value. */

	if (ep0_mps) {
		int i;
		s3c_hsotg_set_ep_maxpacket(hsotg, 0, ep0_mps);
		for (i = 1; i < S3C_HSOTG_EPS; i++)
			s3c_hsotg_set_ep_maxpacket(hsotg, i, ep_mps);
	}

	/* ensure after enumeration our EP0 is active */

	s3c_hsotg_enqueue_setup(hsotg);

	dev_dbg(hsotg->dev, "EP0: DIEPCTL0=0x%08x, DOEPCTL0=0x%08x\n",
		readl(hsotg->regs + S3C_DIEPCTL0),
		readl(hsotg->regs + S3C_DOEPCTL0));
}

/**
 * kill_all_requests - remove all requests from the endpoint's queue
 * @hsotg: The device state.
 * @ep: The endpoint the requests may be on.
 * @result: The result code to use.
 * @force: Force removal of any current requests
 *
 * Go through the requests on the given endpoint and mark them
 * completed with the given result code.
 */
static void kill_all_requests(struct s3c_hsotg *hsotg,
			      struct s3c_hsotg_ep *ep,
			      int result, bool force)
{
	struct s3c_hsotg_req *req, *treq;
	unsigned long flags;

	spin_lock_irqsave(&ep->lock, flags);

	list_for_each_entry_safe(req, treq, &ep->queue, queue) {
		/* currently, we can't do much about an already
		 * running request on an in endpoint */

		if (ep->req == req && ep->dir_in && !force)
			continue;

		s3c_hsotg_complete_request(hsotg, ep, req,
					   result);
	}

	spin_unlock_irqrestore(&ep->lock, flags);
}

#define call_gadget(_hs, _entry) \
	if ((_hs)->gadget.speed != USB_SPEED_UNKNOWN &&	\
	    (_hs)->driver && (_hs)->driver->_entry)	\
		(_hs)->driver->_entry(&(_hs)->gadget);

/**
 * s3c_hsotg_disconnect_irq - disconnect irq service
 * @hsotg: The device state.
 *
 * A disconnect IRQ has been received, meaning that the host has
 * lost contact with the bus. Remove all current transactions
 * and signal the gadget driver that this has happened.
*/
static void s3c_hsotg_disconnect_irq(struct s3c_hsotg *hsotg)
{
	unsigned ep;

	for (ep = 0; ep < S3C_HSOTG_EPS; ep++)
		kill_all_requests(hsotg, &hsotg->eps[ep], -ESHUTDOWN, true);

	call_gadget(hsotg, disconnect);
}

/**
 * s3c_hsotg_irq_fifoempty - TX FIFO empty interrupt handler
 * @hsotg: The device state:
 * @periodic: True if this is a periodic FIFO interrupt
 */
static void s3c_hsotg_irq_fifoempty(struct s3c_hsotg *hsotg, bool periodic)
{
	struct s3c_hsotg_ep *ep;
	int epno, ret;

	/* look through for any more data to transmit */

	for (epno = 0; epno < S3C_HSOTG_EPS; epno++) {
		ep = &hsotg->eps[epno];

		if (!ep->dir_in)
			continue;

		if ((periodic && !ep->periodic) ||
		    (!periodic && ep->periodic))
			continue;

		ret = s3c_hsotg_trytx(hsotg, ep);
		if (ret < 0)
			break;
	}
}

static struct s3c_hsotg *our_hsotg;

/* IRQ flags which will trigger a retry around the IRQ loop */
#define IRQ_RETRY_MASK (S3C_GINTSTS_NPTxFEmp | \
			S3C_GINTSTS_PTxFEmp |  \
			S3C_GINTSTS_RxFLvl)

/**
 * s3c_hsotg_irq - handle device interrupt
 * @irq: The IRQ number triggered
 * @pw: The pw value when registered the handler.
 */
static irqreturn_t s3c_hsotg_irq(int irq, void *pw)
{
	struct s3c_hsotg *hsotg = pw;
	int retry_count = 8;
	u32 gintsts;
	u32 gintmsk;

irq_retry:
	gintsts = readl(hsotg->regs + S3C_GINTSTS);
	gintmsk = readl(hsotg->regs + S3C_GINTMSK);

	dev_dbg(hsotg->dev, "%s: %08x %08x (%08x) retry %d\n",
		__func__, gintsts, gintsts & gintmsk, gintmsk, retry_count);

	gintsts &= gintmsk;

	if (gintsts & S3C_GINTSTS_OTGInt) {
		u32 otgint = readl(hsotg->regs + S3C_GOTGINT);

		dev_info(hsotg->dev, "OTGInt: %08x\n", otgint);

		writel(otgint, hsotg->regs + S3C_GOTGINT);
		writel(S3C_GINTSTS_OTGInt, hsotg->regs + S3C_GINTSTS);
	}

	if (gintsts & S3C_GINTSTS_DisconnInt) {
		dev_dbg(hsotg->dev, "%s: DisconnInt\n", __func__);
		writel(S3C_GINTSTS_DisconnInt, hsotg->regs + S3C_GINTSTS);

		s3c_hsotg_disconnect_irq(hsotg);
	}

	if (gintsts & S3C_GINTSTS_SessReqInt) {
		dev_dbg(hsotg->dev, "%s: SessReqInt\n", __func__);
		writel(S3C_GINTSTS_SessReqInt, hsotg->regs + S3C_GINTSTS);
	}

	if (gintsts & S3C_GINTSTS_EnumDone) {
		s3c_hsotg_irq_enumdone(hsotg);
		writel(S3C_GINTSTS_EnumDone, hsotg->regs + S3C_GINTSTS);
	}

	if (gintsts & S3C_GINTSTS_ConIDStsChng) {
		dev_dbg(hsotg->dev, "ConIDStsChg (DSTS=0x%08x, GOTCTL=%08x)\n",
			readl(hsotg->regs + S3C_DSTS),
			readl(hsotg->regs + S3C_GOTGCTL));

		writel(S3C_GINTSTS_ConIDStsChng, hsotg->regs + S3C_GINTSTS);
	}

	if (gintsts & (S3C_GINTSTS_OEPInt | S3C_GINTSTS_IEPInt)) {
		u32 daint = readl(hsotg->regs + S3C_DAINT);
		u32 daint_out = daint >> S3C_DAINT_OutEP_SHIFT;
		u32 daint_in = daint & ~(daint_out << S3C_DAINT_OutEP_SHIFT);
		int ep;

		dev_dbg(hsotg->dev, "%s: daint=%08x\n", __func__, daint);

		for (ep = 0; ep < 15 && daint_out; ep++, daint_out >>= 1) {
			if (daint_out & 1)
				s3c_hsotg_epint(hsotg, ep, 0);
		}

		for (ep = 0; ep < 15 && daint_in; ep++, daint_in >>= 1) {
			if (daint_in & 1)
				s3c_hsotg_epint(hsotg, ep, 1);
		}

		writel(daint, hsotg->regs + S3C_DAINT);
		writel(gintsts & (S3C_GINTSTS_OEPInt | S3C_GINTSTS_IEPInt),
		       hsotg->regs + S3C_GINTSTS);
	}

	if (gintsts & S3C_GINTSTS_USBRst) {
		dev_info(hsotg->dev, "%s: USBRst\n", __func__);
		dev_dbg(hsotg->dev, "GNPTXSTS=%08x\n",
			readl(hsotg->regs + S3C_GNPTXSTS));

		kill_all_requests(hsotg, &hsotg->eps[0], -ECONNRESET, true);

		/* it seems after a reset we can end up with a situation
		 * where the TXFIFO still has data in it... try flushing
		 * it to remove anything that may still be in it.
		 */

		if (1) {
			writel(S3C_GRSTCTL_TxFNum(0) | S3C_GRSTCTL_TxFFlsh,
			       hsotg->regs + S3C_GRSTCTL);

			dev_info(hsotg->dev, "GNPTXSTS=%08x\n",
				 readl(hsotg->regs + S3C_GNPTXSTS));
		}

		s3c_hsotg_enqueue_setup(hsotg);

		writel(S3C_GINTSTS_USBRst, hsotg->regs + S3C_GINTSTS);
	}

	/* check both FIFOs */

	if (gintsts & S3C_GINTSTS_NPTxFEmp) {
		dev_dbg(hsotg->dev, "NPTxFEmp\n");

		/* Disable the interrupt to stop it happening again
		 * unless one of these endpoint routines decides that
		 * it needs re-enabling */

		s3c_hsotg_disable_gsint(hsotg, S3C_GINTSTS_NPTxFEmp);
		s3c_hsotg_irq_fifoempty(hsotg, false);

		writel(S3C_GINTSTS_NPTxFEmp, hsotg->regs + S3C_GINTSTS);
	}

	if (gintsts & S3C_GINTSTS_PTxFEmp) {
		dev_dbg(hsotg->dev, "PTxFEmp\n");

		/* See note in S3C_GINTSTS_NPTxFEmp */

		s3c_hsotg_disable_gsint(hsotg, S3C_GINTSTS_PTxFEmp);
		s3c_hsotg_irq_fifoempty(hsotg, true);

		writel(S3C_GINTSTS_PTxFEmp, hsotg->regs + S3C_GINTSTS);
	}

	if (gintsts & S3C_GINTSTS_RxFLvl) {
		/* note, since GINTSTS_RxFLvl doubles as FIFO-not-empty,
		 * we need to retry s3c_hsotg_handle_rx if this is still
		 * set. */

		s3c_hsotg_handle_rx(hsotg);
		writel(S3C_GINTSTS_RxFLvl, hsotg->regs + S3C_GINTSTS);
	}

	if (gintsts & S3C_GINTSTS_ModeMis) {
		dev_warn(hsotg->dev, "warning, mode mismatch triggered\n");
		writel(S3C_GINTSTS_ModeMis, hsotg->regs + S3C_GINTSTS);
	}

	if (gintsts & S3C_GINTSTS_USBSusp) {
		dev_info(hsotg->dev, "S3C_GINTSTS_USBSusp\n");
		writel(S3C_GINTSTS_USBSusp, hsotg->regs + S3C_GINTSTS);

		call_gadget(hsotg, suspend);
	}

	if (gintsts & S3C_GINTSTS_WkUpInt) {
		dev_info(hsotg->dev, "S3C_GINTSTS_WkUpIn\n");
		writel(S3C_GINTSTS_WkUpInt, hsotg->regs + S3C_GINTSTS);

		call_gadget(hsotg, resume);
	}

	if (gintsts & S3C_GINTSTS_ErlySusp) {
		dev_dbg(hsotg->dev, "S3C_GINTSTS_ErlySusp\n");
		writel(S3C_GINTSTS_ErlySusp, hsotg->regs + S3C_GINTSTS);
	}

	/* these next two seem to crop-up occasionally causing the core
	 * to shutdown the USB transfer, so try clearing them and logging
	 * the occurence. */

	if (gintsts & S3C_GINTSTS_GOUTNakEff) {
		dev_info(hsotg->dev, "GOUTNakEff triggered\n");

		s3c_hsotg_dump(hsotg);

		writel(S3C_DCTL_CGOUTNak, hsotg->regs + S3C_DCTL);
		writel(S3C_GINTSTS_GOUTNakEff, hsotg->regs + S3C_GINTSTS);
	}

	if (gintsts & S3C_GINTSTS_GINNakEff) {
		dev_info(hsotg->dev, "GINNakEff triggered\n");

		s3c_hsotg_dump(hsotg);

		writel(S3C_DCTL_CGNPInNAK, hsotg->regs + S3C_DCTL);
		writel(S3C_GINTSTS_GINNakEff, hsotg->regs + S3C_GINTSTS);
	}

	/* if we've had fifo events, we should try and go around the
	 * loop again to see if there's any point in returning yet. */

	if (gintsts & IRQ_RETRY_MASK && --retry_count > 0)
			goto irq_retry;

	return IRQ_HANDLED;
}

/**
 * s3c_hsotg_ep_enable - enable the given endpoint
 * @ep: The USB endpint to configure
 * @desc: The USB endpoint descriptor to configure with.
 *
 * This is called from the USB gadget code's usb_ep_enable().
*/
static int s3c_hsotg_ep_enable(struct usb_ep *ep,
			       const struct usb_endpoint_descriptor *desc)
{
	struct s3c_hsotg_ep *hs_ep = our_ep(ep);
	struct s3c_hsotg *hsotg = hs_ep->parent;
	unsigned long flags;
	int index = hs_ep->index;
	u32 epctrl_reg;
	u32 epctrl;
	u32 mps;
	int dir_in;

	dev_dbg(hsotg->dev,
		"%s: ep %s: a 0x%02x, attr 0x%02x, mps 0x%04x, intr %d\n",
		__func__, ep->name, desc->bEndpointAddress, desc->bmAttributes,
		desc->wMaxPacketSize, desc->bInterval);

	/* not to be called for EP0 */
	WARN_ON(index == 0);

	dir_in = (desc->bEndpointAddress & USB_ENDPOINT_DIR_MASK) ? 1 : 0;
	if (dir_in != hs_ep->dir_in) {
		dev_err(hsotg->dev, "%s: direction mismatch!\n", __func__);
		return -EINVAL;
	}

	mps = le16_to_cpu(desc->wMaxPacketSize);

	/* note, we handle this here instead of s3c_hsotg_set_ep_maxpacket */

	epctrl_reg = dir_in ? S3C_DIEPCTL(index) : S3C_DOEPCTL(index);
	epctrl = readl(hsotg->regs + epctrl_reg);

	dev_dbg(hsotg->dev, "%s: read DxEPCTL=0x%08x from 0x%08x\n",
		__func__, epctrl, epctrl_reg);

	spin_lock_irqsave(&hs_ep->lock, flags);

	epctrl &= ~(S3C_DxEPCTL_EPType_MASK | S3C_DxEPCTL_MPS_MASK);
	epctrl |= S3C_DxEPCTL_MPS(mps);

	/* mark the endpoint as active, otherwise the core may ignore
	 * transactions entirely for this endpoint */
	epctrl |= S3C_DxEPCTL_USBActEp;

	/* set the NAK status on the endpoint, otherwise we might try and
	 * do something with data that we've yet got a request to process
	 * since the RXFIFO will take data for an endpoint even if the
	 * size register hasn't been set.
	 */

	epctrl |= S3C_DxEPCTL_SNAK;

	/* update the endpoint state */
	hs_ep->ep.maxpacket = mps;

	/* default, set to non-periodic */
	hs_ep->periodic = 0;

	switch (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) {
	case USB_ENDPOINT_XFER_ISOC:
		dev_err(hsotg->dev, "no current ISOC support\n");
		return -EINVAL;

	case USB_ENDPOINT_XFER_BULK:
		epctrl |= S3C_DxEPCTL_EPType_Bulk;
		break;

	case USB_ENDPOINT_XFER_INT:
		if (dir_in) {
			/* Allocate our TxFNum by simply using the index
			 * of the endpoint for the moment. We could do
			 * something better if the host indicates how
			 * many FIFOs we are expecting to use. */

			hs_ep->periodic = 1;
			epctrl |= S3C_DxEPCTL_TxFNum(index);
		}

		epctrl |= S3C_DxEPCTL_EPType_Intterupt;
		break;

	case USB_ENDPOINT_XFER_CONTROL:
		epctrl |= S3C_DxEPCTL_EPType_Control;
		break;
	}

	/* for non control endpoints, set PID to D0 */
	if (index)
		epctrl |= S3C_DxEPCTL_SetD0PID;

	dev_dbg(hsotg->dev, "%s: write DxEPCTL=0x%08x\n",
		__func__, epctrl);

	writel(epctrl, hsotg->regs + epctrl_reg);
	dev_dbg(hsotg->dev, "%s: read DxEPCTL=0x%08x\n",
		__func__, readl(hsotg->regs + epctrl_reg));

	/* enable the endpoint interrupt */
	s3c_hsotg_ctrl_epint(hsotg, index, dir_in, 1);

	spin_unlock_irqrestore(&hs_ep->lock, flags);
	return 0;
}

static int s3c_hsotg_ep_disable(struct usb_ep *ep)
{
	struct s3c_hsotg_ep *hs_ep = our_ep(ep);
	struct s3c_hsotg *hsotg = hs_ep->parent;
	int dir_in = hs_ep->dir_in;
	int index = hs_ep->index;
	unsigned long flags;
	u32 epctrl_reg;
	u32 ctrl;

	dev_info(hsotg->dev, "%s(ep %p)\n", __func__, ep);

	if (ep == &hsotg->eps[0].ep) {
		dev_err(hsotg->dev, "%s: called for ep0\n", __func__);
		return -EINVAL;
	}

	epctrl_reg = dir_in ? S3C_DIEPCTL(index) : S3C_DOEPCTL(index);

	/* terminate all requests with shutdown */
	kill_all_requests(hsotg, hs_ep, -ESHUTDOWN, false);

	spin_lock_irqsave(&hs_ep->lock, flags);

	ctrl = readl(hsotg->regs + epctrl_reg);
	ctrl &= ~S3C_DxEPCTL_EPEna;
	ctrl &= ~S3C_DxEPCTL_USBActEp;
	ctrl |= S3C_DxEPCTL_SNAK;

	dev_dbg(hsotg->dev, "%s: DxEPCTL=0x%08x\n", __func__, ctrl);
	writel(ctrl, hsotg->regs + epctrl_reg);

	/* disable endpoint interrupts */
	s3c_hsotg_ctrl_epint(hsotg, hs_ep->index, hs_ep->dir_in, 0);

	spin_unlock_irqrestore(&hs_ep->lock, flags);
	return 0;
}

/**
 * on_list - check request is on the given endpoint
 * @ep: The endpoint to check.
 * @test: The request to test if it is on the endpoint.
*/
static bool on_list(struct s3c_hsotg_ep *ep, struct s3c_hsotg_req *test)
{
	struct s3c_hsotg_req *req, *treq;

	list_for_each_entry_safe(req, treq, &ep->queue, queue) {
		if (req == test)
			return true;
	}

	return false;
}

static int s3c_hsotg_ep_dequeue(struct usb_ep *ep, struct usb_request *req)
{
	struct s3c_hsotg_req *hs_req = our_req(req);
	struct s3c_hsotg_ep *hs_ep = our_ep(ep);
	struct s3c_hsotg *hs = hs_ep->parent;
	unsigned long flags;

	dev_info(hs->dev, "ep_dequeue(%p,%p)\n", ep, req);

	if (hs_req == hs_ep->req) {
		dev_dbg(hs->dev, "%s: already in progress\n", __func__);
		return -EINPROGRESS;
	}

	spin_lock_irqsave(&hs_ep->lock, flags);

	if (!on_list(hs_ep, hs_req)) {
		spin_unlock_irqrestore(&hs_ep->lock, flags);
		return -EINVAL;
	}

	s3c_hsotg_complete_request(hs, hs_ep, hs_req, -ECONNRESET);
	spin_unlock_irqrestore(&hs_ep->lock, flags);

	return 0;
}

static int s3c_hsotg_ep_sethalt(struct usb_ep *ep, int value)
{
	struct s3c_hsotg_ep *hs_ep = our_ep(ep);
	struct s3c_hsotg *hs = hs_ep->parent;
	int index = hs_ep->index;
	unsigned long irqflags;
	u32 epreg;
	u32 epctl;

	dev_info(hs->dev, "%s(ep %p %s, %d)\n", __func__, ep, ep->name, value);

	spin_lock_irqsave(&hs_ep->lock, irqflags);

	/* write both IN and OUT control registers */

	epreg = S3C_DIEPCTL(index);
	epctl = readl(hs->regs + epreg);

	if (value)
		epctl |= S3C_DxEPCTL_Stall;
	else
		epctl &= ~S3C_DxEPCTL_Stall;

	writel(epctl, hs->regs + epreg);

	epreg = S3C_DOEPCTL(index);
	epctl = readl(hs->regs + epreg);

	if (value)
		epctl |= S3C_DxEPCTL_Stall;
	else
		epctl &= ~S3C_DxEPCTL_Stall;

	writel(epctl, hs->regs + epreg);

	spin_unlock_irqrestore(&hs_ep->lock, irqflags);

	return 0;
}

static struct usb_ep_ops s3c_hsotg_ep_ops = {
	.enable		= s3c_hsotg_ep_enable,
	.disable	= s3c_hsotg_ep_disable,
	.alloc_request	= s3c_hsotg_ep_alloc_request,
	.free_request	= s3c_hsotg_ep_free_request,
	.queue		= s3c_hsotg_ep_queue,
	.dequeue	= s3c_hsotg_ep_dequeue,
	.set_halt	= s3c_hsotg_ep_sethalt,
	/* note, don't belive we have any call for the fifo routines */
};

/**
 * s3c_hsotg_corereset - issue softreset to the core
 * @hsotg: The device state
 *
 * Issue a soft reset to the core, and await the core finishing it.
*/
static int s3c_hsotg_corereset(struct s3c_hsotg *hsotg)
{
	int timeout;
	u32 grstctl;

	dev_dbg(hsotg->dev, "resetting core\n");

	/* issue soft reset */
	writel(S3C_GRSTCTL_CSftRst, hsotg->regs + S3C_GRSTCTL);

	timeout = 1000;
	do {
		grstctl = readl(hsotg->regs + S3C_GRSTCTL);
	} while (!(grstctl & S3C_GRSTCTL_CSftRst) && timeout-- > 0);

	if (!(grstctl & S3C_GRSTCTL_CSftRst)) {
		dev_err(hsotg->dev, "Failed to get CSftRst asserted\n");
		return -EINVAL;
	}

	timeout = 1000;

	while (1) {
		u32 grstctl = readl(hsotg->regs + S3C_GRSTCTL);

		if (timeout-- < 0) {
			dev_info(hsotg->dev,
				 "%s: reset failed, GRSTCTL=%08x\n",
				 __func__, grstctl);
			return -ETIMEDOUT;
		}

		if (grstctl & S3C_GRSTCTL_CSftRst)
			continue;

		if (!(grstctl & S3C_GRSTCTL_AHBIdle))
			continue;

		break; 		/* reset done */
	}

	dev_dbg(hsotg->dev, "reset successful\n");
	return 0;
}

int usb_gadget_register_driver(struct usb_gadget_driver *driver)
{
	struct s3c_hsotg *hsotg = our_hsotg;
	int ret;

	if (!hsotg) {
		printk(KERN_ERR "%s: called with no device\n", __func__);
		return -ENODEV;
	}

	if (!driver) {
		dev_err(hsotg->dev, "%s: no driver\n", __func__);
		return -EINVAL;
	}

	if (driver->speed != USB_SPEED_HIGH &&
	    driver->speed != USB_SPEED_FULL) {
		dev_err(hsotg->dev, "%s: bad speed\n", __func__);
	}

	if (!driver->bind || !driver->setup) {
		dev_err(hsotg->dev, "%s: missing entry points\n", __func__);
		return -EINVAL;
	}

	WARN_ON(hsotg->driver);

	driver->driver.bus = NULL;
	hsotg->driver = driver;
	hsotg->gadget.dev.driver = &driver->driver;
	hsotg->gadget.dev.dma_mask = hsotg->dev->dma_mask;
	hsotg->gadget.speed = USB_SPEED_UNKNOWN;

	ret = device_add(&hsotg->gadget.dev);
	if (ret) {
		dev_err(hsotg->dev, "failed to register gadget device\n");
		goto err;
	}

	ret = driver->bind(&hsotg->gadget);
	if (ret) {
		dev_err(hsotg->dev, "failed bind %s\n", driver->driver.name);

		hsotg->gadget.dev.driver = NULL;
		hsotg->driver = NULL;
		goto err;
	}

	/* we must now enable ep0 ready for host detection and then
	 * set configuration. */

	s3c_hsotg_corereset(hsotg);

	/* set the PLL on, remove the HNP/SRP and set the PHY */
	writel(S3C_GUSBCFG_PHYIf16 | S3C_GUSBCFG_TOutCal(7) |
	       (0x5 << 10), hsotg->regs + S3C_GUSBCFG);

	/* looks like soft-reset changes state of FIFOs */
	s3c_hsotg_init_fifo(hsotg);

	__orr32(hsotg->regs + S3C_DCTL, S3C_DCTL_SftDiscon);

	writel(1 << 18 | S3C_DCFG_DevSpd_HS,  hsotg->regs + S3C_DCFG);

	writel(S3C_GINTSTS_DisconnInt | S3C_GINTSTS_SessReqInt |
	       S3C_GINTSTS_ConIDStsChng | S3C_GINTSTS_USBRst |
	       S3C_GINTSTS_EnumDone | S3C_GINTSTS_OTGInt |
	       S3C_GINTSTS_USBSusp | S3C_GINTSTS_WkUpInt |
	       S3C_GINTSTS_GOUTNakEff | S3C_GINTSTS_GINNakEff |
	       S3C_GINTSTS_ErlySusp,
	       hsotg->regs + S3C_GINTMSK);

	if (using_dma(hsotg))
		writel(S3C_GAHBCFG_GlblIntrEn | S3C_GAHBCFG_DMAEn |
		       S3C_GAHBCFG_HBstLen_Incr4,
		       hsotg->regs + S3C_GAHBCFG);
	else
		writel(S3C_GAHBCFG_GlblIntrEn, hsotg->regs + S3C_GAHBCFG);

	/* Enabling INTknTXFEmpMsk here seems to be a big mistake, we end
	 * up being flooded with interrupts if the host is polling the
	 * endpoint to try and read data. */

	writel(S3C_DIEPMSK_TimeOUTMsk | S3C_DIEPMSK_AHBErrMsk |
	       S3C_DIEPMSK_INTknEPMisMsk |
	       S3C_DIEPMSK_EPDisbldMsk | S3C_DIEPMSK_XferComplMsk,
	       hsotg->regs + S3C_DIEPMSK);

	/* don't need XferCompl, we get that from RXFIFO in slave mode. In
	 * DMA mode we may need this. */
	writel(S3C_DOEPMSK_SetupMsk | S3C_DOEPMSK_AHBErrMsk |
	       S3C_DOEPMSK_EPDisbldMsk |
	       (using_dma(hsotg) ? (S3C_DIEPMSK_XferComplMsk |
				   S3C_DIEPMSK_TimeOUTMsk) : 0),
	       hsotg->regs + S3C_DOEPMSK);

	writel(0, hsotg->regs + S3C_DAINTMSK);

	dev_info(hsotg->dev, "EP0: DIEPCTL0=0x%08x, DOEPCTL0=0x%08x\n",
		 readl(hsotg->regs + S3C_DIEPCTL0),
		 readl(hsotg->regs + S3C_DOEPCTL0));

	/* enable in and out endpoint interrupts */
	s3c_hsotg_en_gsint(hsotg, S3C_GINTSTS_OEPInt | S3C_GINTSTS_IEPInt);

	/* Enable the RXFIFO when in slave mode, as this is how we collect
	 * the data. In DMA mode, we get events from the FIFO but also
	 * things we cannot process, so do not use it. */
	if (!using_dma(hsotg))
		s3c_hsotg_en_gsint(hsotg, S3C_GINTSTS_RxFLvl);

	/* Enable interrupts for EP0 in and out */
	s3c_hsotg_ctrl_epint(hsotg, 0, 0, 1);
	s3c_hsotg_ctrl_epint(hsotg, 0, 1, 1);

	__orr32(hsotg->regs + S3C_DCTL, S3C_DCTL_PWROnPrgDone);
	udelay(10);  /* see openiboot */
	__bic32(hsotg->regs + S3C_DCTL, S3C_DCTL_PWROnPrgDone);

	dev_info(hsotg->dev, "DCTL=0x%08x\n", readl(hsotg->regs + S3C_DCTL));

	/* S3C_DxEPCTL_USBActEp says RO in manual, but seems to be set by
	   writing to the EPCTL register.. */

	/* set to read 1 8byte packet */
	writel(S3C_DxEPTSIZ_MC(1) | S3C_DxEPTSIZ_PktCnt(1) |
	       S3C_DxEPTSIZ_XferSize(8), hsotg->regs + DOEPTSIZ0);

	writel(s3c_hsotg_ep0_mps(hsotg->eps[0].ep.maxpacket) |
	       S3C_DxEPCTL_CNAK | S3C_DxEPCTL_EPEna |
	       S3C_DxEPCTL_USBActEp,
	       hsotg->regs + S3C_DOEPCTL0);

	/* enable, but don't activate EP0in */
	writel(s3c_hsotg_ep0_mps(hsotg->eps[0].ep.maxpacket) |
	       S3C_DxEPCTL_USBActEp, hsotg->regs + S3C_DIEPCTL0);

	s3c_hsotg_enqueue_setup(hsotg);

	dev_info(hsotg->dev, "EP0: DIEPCTL0=0x%08x, DOEPCTL0=0x%08x\n",
		 readl(hsotg->regs + S3C_DIEPCTL0),
		 readl(hsotg->regs + S3C_DOEPCTL0));

	/* clear global NAKs */
	writel(S3C_DCTL_CGOUTNak | S3C_DCTL_CGNPInNAK,
	       hsotg->regs + S3C_DCTL);

	/* remove the soft-disconnect and let's go */
	__bic32(hsotg->regs + S3C_DCTL, S3C_DCTL_SftDiscon);

	/* report to the user, and return */

	dev_info(hsotg->dev, "bound driver %s\n", driver->driver.name);
	return 0;

err:
	hsotg->driver = NULL;
	hsotg->gadget.dev.driver = NULL;
	return ret;
}
EXPORT_SYMBOL(usb_gadget_register_driver);

int usb_gadget_unregister_driver(struct usb_gadget_driver *driver)
{
	struct s3c_hsotg *hsotg = our_hsotg;
	int ep;

	if (!hsotg)
		return -ENODEV;

	if (!driver || driver != hsotg->driver || !driver->unbind)
		return -EINVAL;

	/* all endpoints should be shutdown */
	for (ep = 0; ep < S3C_HSOTG_EPS; ep++)
		s3c_hsotg_ep_disable(&hsotg->eps[ep].ep);

	call_gadget(hsotg, disconnect);

	driver->unbind(&hsotg->gadget);
	hsotg->driver = NULL;
	hsotg->gadget.speed = USB_SPEED_UNKNOWN;

	device_del(&hsotg->gadget.dev);

	dev_info(hsotg->dev, "unregistered gadget driver '%s'\n",
		 driver->driver.name);

	return 0;
}
EXPORT_SYMBOL(usb_gadget_unregister_driver);

static int s3c_hsotg_gadget_getframe(struct usb_gadget *gadget)
{
	return s3c_hsotg_read_frameno(to_hsotg(gadget));
}

static struct usb_gadget_ops s3c_hsotg_gadget_ops = {
	.get_frame	= s3c_hsotg_gadget_getframe,
};

/**
 * s3c_hsotg_initep - initialise a single endpoint
 * @hsotg: The device state.
 * @hs_ep: The endpoint to be initialised.
 * @epnum: The endpoint number
 *
 * Initialise the given endpoint (as part of the probe and device state
 * creation) to give to the gadget driver. Setup the endpoint name, any
 * direction information and other state that may be required.
 */
static void __devinit s3c_hsotg_initep(struct s3c_hsotg *hsotg,
				       struct s3c_hsotg_ep *hs_ep,
				       int epnum)
{
	u32 ptxfifo;
	char *dir;

	if (epnum == 0)
		dir = "";
	else if ((epnum % 2) == 0) {
		dir = "out";
	} else {
		dir = "in";
		hs_ep->dir_in = 1;
	}

	hs_ep->index = epnum;

	snprintf(hs_ep->name, sizeof(hs_ep->name), "ep%d%s", epnum, dir);

	INIT_LIST_HEAD(&hs_ep->queue);
	INIT_LIST_HEAD(&hs_ep->ep.ep_list);

	spin_lock_init(&hs_ep->lock);

	/* add to the list of endpoints known by the gadget driver */
	if (epnum)
		list_add_tail(&hs_ep->ep.ep_list, &hsotg->gadget.ep_list);

	hs_ep->parent = hsotg;
	hs_ep->ep.name = hs_ep->name;
	hs_ep->ep.maxpacket = epnum ? 512 : EP0_MPS_LIMIT;
	hs_ep->ep.ops = &s3c_hsotg_ep_ops;

	/* Read the FIFO size for the Periodic TX FIFO, even if we're
	 * an OUT endpoint, we may as well do this if in future the
	 * code is changed to make each endpoint's direction changeable.
	 */

	ptxfifo = readl(hsotg->regs + S3C_DPTXFSIZn(epnum));
	hs_ep->fifo_size = S3C_DPTXFSIZn_DPTxFSize_GET(ptxfifo);

	/* if we're using dma, we need to set the next-endpoint pointer
	 * to be something valid.
	 */

	if (using_dma(hsotg)) {
		u32 next = S3C_DxEPCTL_NextEp((epnum + 1) % 15);
		writel(next, hsotg->regs + S3C_DIEPCTL(epnum));
		writel(next, hsotg->regs + S3C_DOEPCTL(epnum));
	}
}

/**
 * s3c_hsotg_otgreset - reset the OtG phy block
 * @hsotg: The host state.
 *
 * Power up the phy, set the basic configuration and start the PHY.
 */
static void s3c_hsotg_otgreset(struct s3c_hsotg *hsotg)
{
	u32 osc;

	writel(0, S3C_PHYPWR);
	mdelay(1);

	osc = hsotg->plat->is_osc ? S3C_PHYCLK_EXT_OSC : 0;

	writel(osc | 0x10, S3C_PHYCLK);

	/* issue a full set of resets to the otg and core */

	writel(S3C_RSTCON_PHY, S3C_RSTCON);
	udelay(20);	/* at-least 10uS */
	writel(0, S3C_RSTCON);
}


static void s3c_hsotg_init(struct s3c_hsotg *hsotg)
{
	/* unmask subset of endpoint interrupts */

	writel(S3C_DIEPMSK_TimeOUTMsk | S3C_DIEPMSK_AHBErrMsk |
	       S3C_DIEPMSK_EPDisbldMsk | S3C_DIEPMSK_XferComplMsk,
	       hsotg->regs + S3C_DIEPMSK);

	writel(S3C_DOEPMSK_SetupMsk | S3C_DOEPMSK_AHBErrMsk |
	       S3C_DOEPMSK_EPDisbldMsk | S3C_DOEPMSK_XferComplMsk,
	       hsotg->regs + S3C_DOEPMSK);

	writel(0, hsotg->regs + S3C_DAINTMSK);

	if (0) {
		/* post global nak until we're ready */
		writel(S3C_DCTL_SGNPInNAK | S3C_DCTL_SGOUTNak,
		       hsotg->regs + S3C_DCTL);
	}

	/* setup fifos */

	dev_info(hsotg->dev, "GRXFSIZ=0x%08x, GNPTXFSIZ=0x%08x\n",
		 readl(hsotg->regs + S3C_GRXFSIZ),
		 readl(hsotg->regs + S3C_GNPTXFSIZ));

	s3c_hsotg_init_fifo(hsotg);

	/* set the PLL on, remove the HNP/SRP and set the PHY */
	writel(S3C_GUSBCFG_PHYIf16 | S3C_GUSBCFG_TOutCal(7) | (0x5 << 10),
	       hsotg->regs + S3C_GUSBCFG);

	writel(using_dma(hsotg) ? S3C_GAHBCFG_DMAEn : 0x0,
	       hsotg->regs + S3C_GAHBCFG);
}

static void s3c_hsotg_dump(struct s3c_hsotg *hsotg)
{
	struct device *dev = hsotg->dev;
	void __iomem *regs = hsotg->regs;
	u32 val;
	int idx;

	dev_info(dev, "DCFG=0x%08x, DCTL=0x%08x, DIEPMSK=%08x\n",
		 readl(regs + S3C_DCFG), readl(regs + S3C_DCTL),
		 readl(regs + S3C_DIEPMSK));

	dev_info(dev, "GAHBCFG=0x%08x, 0x44=0x%08x\n",
		 readl(regs + S3C_GAHBCFG), readl(regs + 0x44));

	dev_info(dev, "GRXFSIZ=0x%08x, GNPTXFSIZ=0x%08x\n",
		 readl(regs + S3C_GRXFSIZ), readl(regs + S3C_GNPTXFSIZ));

	/* show periodic fifo settings */

	for (idx = 1; idx <= 15; idx++) {
		val = readl(regs + S3C_DPTXFSIZn(idx));
		dev_info(dev, "DPTx[%d] FSize=%d, StAddr=0x%08x\n", idx,
			 val >> S3C_DPTXFSIZn_DPTxFSize_SHIFT,
			 val & S3C_DPTXFSIZn_DPTxFStAddr_MASK);
	}

	for (idx = 0; idx < 15; idx++) {
		dev_info(dev,
			 "ep%d-in: EPCTL=0x%08x, SIZ=0x%08x, DMA=0x%08x\n", idx,
			 readl(regs + S3C_DIEPCTL(idx)),
			 readl(regs + S3C_DIEPTSIZ(idx)),
			 readl(regs + S3C_DIEPDMA(idx)));

		val = readl(regs + S3C_DOEPCTL(idx));
		dev_info(dev,
			 "ep%d-out: EPCTL=0x%08x, SIZ=0x%08x, DMA=0x%08x\n",
			 idx, readl(regs + S3C_DOEPCTL(idx)),
			 readl(regs + S3C_DOEPTSIZ(idx)),
			 readl(regs + S3C_DOEPDMA(idx)));

	}

	dev_info(dev, "DVBUSDIS=0x%08x, DVBUSPULSE=%08x\n",
		 readl(regs + S3C_DVBUSDIS), readl(regs + S3C_DVBUSPULSE));
}


/**
 * state_show - debugfs: show overall driver and device state.
 * @seq: The seq file to write to.
 * @v: Unused parameter.
 *
 * This debugfs entry shows the overall state of the hardware and
 * some general information about each of the endpoints available
 * to the system.
 */
static int state_show(struct seq_file *seq, void *v)
{
	struct s3c_hsotg *hsotg = seq->private;
	void __iomem *regs = hsotg->regs;
	int idx;

	seq_printf(seq, "DCFG=0x%08x, DCTL=0x%08x, DSTS=0x%08x\n",
		 readl(regs + S3C_DCFG),
		 readl(regs + S3C_DCTL),
		 readl(regs + S3C_DSTS));

	seq_printf(seq, "DIEPMSK=0x%08x, DOEPMASK=0x%08x\n",
		   readl(regs + S3C_DIEPMSK), readl(regs + S3C_DOEPMSK));

	seq_printf(seq, "GINTMSK=0x%08x, GINTSTS=0x%08x\n",
		   readl(regs + S3C_GINTMSK),
		   readl(regs + S3C_GINTSTS));

	seq_printf(seq, "DAINTMSK=0x%08x, DAINT=0x%08x\n",
		   readl(regs + S3C_DAINTMSK),
		   readl(regs + S3C_DAINT));

	seq_printf(seq, "GNPTXSTS=0x%08x, GRXSTSR=%08x\n",
		   readl(regs + S3C_GNPTXSTS),
		   readl(regs + S3C_GRXSTSR));

	seq_printf(seq, "\nEndpoint status:\n");

	for (idx = 0; idx < 15; idx++) {
		u32 in, out;

		in = readl(regs + S3C_DIEPCTL(idx));
		out = readl(regs + S3C_DOEPCTL(idx));

		seq_printf(seq, "ep%d: DIEPCTL=0x%08x, DOEPCTL=0x%08x",
			   idx, in, out);

		in = readl(regs + S3C_DIEPTSIZ(idx));
		out = readl(regs + S3C_DOEPTSIZ(idx));

		seq_printf(seq, ", DIEPTSIZ=0x%08x, DOEPTSIZ=0x%08x",
			   in, out);

		seq_printf(seq, "\n");
	}

	return 0;
}

static int state_open(struct inode *inode, struct file *file)
{
	return single_open(file, state_show, inode->i_private);
}

static const struct file_operations state_fops = {
	.owner		= THIS_MODULE,
	.open		= state_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/**
 * fifo_show - debugfs: show the fifo information
 * @seq: The seq_file to write data to.
 * @v: Unused parameter.
 *
 * Show the FIFO information for the overall fifo and all the
 * periodic transmission FIFOs.
*/
static int fifo_show(struct seq_file *seq, void *v)
{
	struct s3c_hsotg *hsotg = seq->private;
	void __iomem *regs = hsotg->regs;
	u32 val;
	int idx;

	seq_printf(seq, "Non-periodic FIFOs:\n");
	seq_printf(seq, "RXFIFO: Size %d\n", readl(regs + S3C_GRXFSIZ));

	val = readl(regs + S3C_GNPTXFSIZ);
	seq_printf(seq, "NPTXFIFO: Size %d, Start 0x%08x\n",
		   val >> S3C_GNPTXFSIZ_NPTxFDep_SHIFT,
		   val & S3C_GNPTXFSIZ_NPTxFStAddr_MASK);

	seq_printf(seq, "\nPeriodic TXFIFOs:\n");

	for (idx = 1; idx <= 15; idx++) {
		val = readl(regs + S3C_DPTXFSIZn(idx));

		seq_printf(seq, "\tDPTXFIFO%2d: Size %d, Start 0x%08x\n", idx,
			   val >> S3C_DPTXFSIZn_DPTxFSize_SHIFT,
			   val & S3C_DPTXFSIZn_DPTxFStAddr_MASK);
	}

	return 0;
}

static int fifo_open(struct inode *inode, struct file *file)
{
	return single_open(file, fifo_show, inode->i_private);
}

static const struct file_operations fifo_fops = {
	.owner		= THIS_MODULE,
	.open		= fifo_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};


static const char *decode_direction(int is_in)
{
	return is_in ? "in" : "out";
}

/**
 * ep_show - debugfs: show the state of an endpoint.
 * @seq: The seq_file to write data to.
 * @v: Unused parameter.
 *
 * This debugfs entry shows the state of the given endpoint (one is
 * registered for each available).
*/
static int ep_show(struct seq_file *seq, void *v)
{
	struct s3c_hsotg_ep *ep = seq->private;
	struct s3c_hsotg *hsotg = ep->parent;
	struct s3c_hsotg_req *req;
	void __iomem *regs = hsotg->regs;
	int index = ep->index;
	int show_limit = 15;
	unsigned long flags;

	seq_printf(seq, "Endpoint index %d, named %s,  dir %s:\n",
		   ep->index, ep->ep.name, decode_direction(ep->dir_in));

	/* first show the register state */

	seq_printf(seq, "\tDIEPCTL=0x%08x, DOEPCTL=0x%08x\n",
		   readl(regs + S3C_DIEPCTL(index)),
		   readl(regs + S3C_DOEPCTL(index)));

	seq_printf(seq, "\tDIEPDMA=0x%08x, DOEPDMA=0x%08x\n",
		   readl(regs + S3C_DIEPDMA(index)),
		   readl(regs + S3C_DOEPDMA(index)));

	seq_printf(seq, "\tDIEPINT=0x%08x, DOEPINT=0x%08x\n",
		   readl(regs + S3C_DIEPINT(index)),
		   readl(regs + S3C_DOEPINT(index)));

	seq_printf(seq, "\tDIEPTSIZ=0x%08x, DOEPTSIZ=0x%08x\n",
		   readl(regs + S3C_DIEPTSIZ(index)),
		   readl(regs + S3C_DOEPTSIZ(index)));

	seq_printf(seq, "\n");
	seq_printf(seq, "mps %d\n", ep->ep.maxpacket);
	seq_printf(seq, "total_data=%ld\n", ep->total_data);

	seq_printf(seq, "request list (%p,%p):\n",
		   ep->queue.next, ep->queue.prev);

	spin_lock_irqsave(&ep->lock, flags);

	list_for_each_entry(req, &ep->queue, queue) {
		if (--show_limit < 0) {
			seq_printf(seq, "not showing more requests...\n");
			break;
		}

		seq_printf(seq, "%c req %p: %d bytes @%p, ",
			   req == ep->req ? '*' : ' ',
			   req, req->req.length, req->req.buf);
		seq_printf(seq, "%d done, res %d\n",
			   req->req.actual, req->req.status);
	}

	spin_unlock_irqrestore(&ep->lock, flags);

	return 0;
}

static int ep_open(struct inode *inode, struct file *file)
{
	return single_open(file, ep_show, inode->i_private);
}

static const struct file_operations ep_fops = {
	.owner		= THIS_MODULE,
	.open		= ep_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/**
 * s3c_hsotg_create_debug - create debugfs directory and files
 * @hsotg: The driver state
 *
 * Create the debugfs files to allow the user to get information
 * about the state of the system. The directory name is created
 * with the same name as the device itself, in case we end up
 * with multiple blocks in future systems.
*/
static void __devinit s3c_hsotg_create_debug(struct s3c_hsotg *hsotg)
{
	struct dentry *root;
	unsigned epidx;

	root = debugfs_create_dir(dev_name(hsotg->dev), NULL);
	hsotg->debug_root = root;
	if (IS_ERR(root)) {
		dev_err(hsotg->dev, "cannot create debug root\n");
		return;
	}

	/* create general state file */

	hsotg->debug_file = debugfs_create_file("state", 0444, root,
						hsotg, &state_fops);

	if (IS_ERR(hsotg->debug_file))
		dev_err(hsotg->dev, "%s: failed to create state\n", __func__);

	hsotg->debug_fifo = debugfs_create_file("fifo", 0444, root,
						hsotg, &fifo_fops);

	if (IS_ERR(hsotg->debug_fifo))
		dev_err(hsotg->dev, "%s: failed to create fifo\n", __func__);

	/* create one file for each endpoint */

	for (epidx = 0; epidx < S3C_HSOTG_EPS; epidx++) {
		struct s3c_hsotg_ep *ep = &hsotg->eps[epidx];

		ep->debugfs = debugfs_create_file(ep->name, 0444,
						  root, ep, &ep_fops);

		if (IS_ERR(ep->debugfs))
			dev_err(hsotg->dev, "failed to create %s debug file\n",
				ep->name);
	}
}

/**
 * s3c_hsotg_delete_debug - cleanup debugfs entries
 * @hsotg: The driver state
 *
 * Cleanup (remove) the debugfs files for use on module exit.
*/
static void __devexit s3c_hsotg_delete_debug(struct s3c_hsotg *hsotg)
{
	unsigned epidx;

	for (epidx = 0; epidx < S3C_HSOTG_EPS; epidx++) {
		struct s3c_hsotg_ep *ep = &hsotg->eps[epidx];
		debugfs_remove(ep->debugfs);
	}

	debugfs_remove(hsotg->debug_file);
	debugfs_remove(hsotg->debug_fifo);
	debugfs_remove(hsotg->debug_root);
}

/**
 * s3c_hsotg_gate - set the hardware gate for the block
 * @pdev: The device we bound to
 * @on: On or off.
 *
 * Set the hardware gate setting into the block. If we end up on
 * something other than an S3C64XX, then we might need to change this
 * to using a platform data callback, or some other mechanism.
 */
static void s3c_hsotg_gate(struct platform_device *pdev, bool on)
{
	unsigned long flags;
	u32 others;

	local_irq_save(flags);

	others = __raw_readl(S3C64XX_OTHERS);
	if (on)
		others |= S3C64XX_OTHERS_USBMASK;
	else
		others &= ~S3C64XX_OTHERS_USBMASK;
	__raw_writel(others, S3C64XX_OTHERS);

	local_irq_restore(flags);
}

static struct s3c_hsotg_plat s3c_hsotg_default_pdata;

static int __devinit s3c_hsotg_probe(struct platform_device *pdev)
{
	struct s3c_hsotg_plat *plat = pdev->dev.platform_data;
	struct device *dev = &pdev->dev;
	struct s3c_hsotg *hsotg;
	struct resource *res;
	int epnum;
	int ret;

	if (!plat)
		plat = &s3c_hsotg_default_pdata;

	hsotg = kzalloc(sizeof(struct s3c_hsotg) +
			sizeof(struct s3c_hsotg_ep) * S3C_HSOTG_EPS,
			GFP_KERNEL);
	if (!hsotg) {
		dev_err(dev, "cannot get memory\n");
		return -ENOMEM;
	}

	hsotg->dev = dev;
	hsotg->plat = plat;

	platform_set_drvdata(pdev, hsotg);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "cannot find register resource 0\n");
		ret = -EINVAL;
		goto err_mem;
	}

	hsotg->regs_res = request_mem_region(res->start, resource_size(res),
					     dev_name(dev));
	if (!hsotg->regs_res) {
		dev_err(dev, "cannot reserve registers\n");
		ret = -ENOENT;
		goto err_mem;
	}

	hsotg->regs = ioremap(res->start, resource_size(res));
	if (!hsotg->regs) {
		dev_err(dev, "cannot map registers\n");
		ret = -ENXIO;
		goto err_regs_res;
	}

	ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		dev_err(dev, "cannot find IRQ\n");
		goto err_regs;
	}

	hsotg->irq = ret;

	ret = request_irq(ret, s3c_hsotg_irq, 0, dev_name(dev), hsotg);
	if (ret < 0) {
		dev_err(dev, "cannot claim IRQ\n");
		goto err_regs;
	}

	dev_info(dev, "regs %p, irq %d\n", hsotg->regs, hsotg->irq);

	device_initialize(&hsotg->gadget.dev);

	dev_set_name(&hsotg->gadget.dev, "gadget");

	hsotg->gadget.is_dualspeed = 1;
	hsotg->gadget.ops = &s3c_hsotg_gadget_ops;
	hsotg->gadget.name = dev_name(dev);

	hsotg->gadget.dev.parent = dev;
	hsotg->gadget.dev.dma_mask = dev->dma_mask;

	/* setup endpoint information */

	INIT_LIST_HEAD(&hsotg->gadget.ep_list);
	hsotg->gadget.ep0 = &hsotg->eps[0].ep;

	/* allocate EP0 request */

	hsotg->ctrl_req = s3c_hsotg_ep_alloc_request(&hsotg->eps[0].ep,
						     GFP_KERNEL);
	if (!hsotg->ctrl_req) {
		dev_err(dev, "failed to allocate ctrl req\n");
		goto err_regs;
	}

	/* reset the system */

	s3c_hsotg_gate(pdev, true);

	s3c_hsotg_otgreset(hsotg);
	s3c_hsotg_corereset(hsotg);
	s3c_hsotg_init(hsotg);

	/* initialise the endpoints now the core has been initialised */
	for (epnum = 0; epnum < S3C_HSOTG_EPS; epnum++)
		s3c_hsotg_initep(hsotg, &hsotg->eps[epnum], epnum);

	s3c_hsotg_create_debug(hsotg);

	s3c_hsotg_dump(hsotg);

	our_hsotg = hsotg;
	return 0;

err_regs:
	iounmap(hsotg->regs);

err_regs_res:
	release_resource(hsotg->regs_res);
	kfree(hsotg->regs_res);

err_mem:
	kfree(hsotg);
	return ret;
}

static int __devexit s3c_hsotg_remove(struct platform_device *pdev)
{
	struct s3c_hsotg *hsotg = platform_get_drvdata(pdev);

	s3c_hsotg_delete_debug(hsotg);

	usb_gadget_unregister_driver(hsotg->driver);

	free_irq(hsotg->irq, hsotg);
	iounmap(hsotg->regs);

	release_resource(hsotg->regs_res);
	kfree(hsotg->regs_res);

	s3c_hsotg_gate(pdev, false);

	kfree(hsotg);
	return 0;
}

#if 1
#define s3c_hsotg_suspend NULL
#define s3c_hsotg_resume NULL
#endif

static struct platform_driver s3c_hsotg_driver = {
	.driver		= {
		.name	= "s3c-hsotg",
		.owner	= THIS_MODULE,
	},
	.probe		= s3c_hsotg_probe,
	.remove		= __devexit_p(s3c_hsotg_remove),
	.suspend	= s3c_hsotg_suspend,
	.resume		= s3c_hsotg_resume,
};

static int __init s3c_hsotg_modinit(void)
{
	return platform_driver_register(&s3c_hsotg_driver);
}

static void __exit s3c_hsotg_modexit(void)
{
	platform_driver_unregister(&s3c_hsotg_driver);
}

module_init(s3c_hsotg_modinit);
module_exit(s3c_hsotg_modexit);

MODULE_DESCRIPTION("Samsung S3C USB High-speed/OtG device");
MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:s3c-hsotg");
