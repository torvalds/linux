// SPDX-License-Identifier: GPL-2.0+
/*
 * Xilinx USB peripheral controller driver
 *
 * Copyright (C) 2004 by Thomas Rathbone
 * Copyright (C) 2005 by HP Labs
 * Copyright (C) 2005 by David Brownell
 * Copyright (C) 2010 - 2014 Xilinx, Inc.
 *
 * Some parts of this driver code is based on the driver for at91-series
 * USB peripheral controller (at91_udc.c).
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/prefetch.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>

/* Register offsets for the USB device.*/
#define XUSB_EP0_CONFIG_OFFSET		0x0000  /* EP0 Config Reg Offset */
#define XUSB_SETUP_PKT_ADDR_OFFSET	0x0080  /* Setup Packet Address */
#define XUSB_ADDRESS_OFFSET		0x0100  /* Address Register */
#define XUSB_CONTROL_OFFSET		0x0104  /* Control Register */
#define XUSB_STATUS_OFFSET		0x0108  /* Status Register */
#define XUSB_FRAMENUM_OFFSET		0x010C	/* Frame Number Register */
#define XUSB_IER_OFFSET			0x0110	/* Interrupt Enable Register */
#define XUSB_BUFFREADY_OFFSET		0x0114	/* Buffer Ready Register */
#define XUSB_TESTMODE_OFFSET		0x0118	/* Test Mode Register */
#define XUSB_DMA_RESET_OFFSET		0x0200  /* DMA Soft Reset Register */
#define XUSB_DMA_CONTROL_OFFSET		0x0204	/* DMA Control Register */
#define XUSB_DMA_DSAR_ADDR_OFFSET	0x0208	/* DMA source Address Reg */
#define XUSB_DMA_DDAR_ADDR_OFFSET	0x020C	/* DMA destination Addr Reg */
#define XUSB_DMA_LENGTH_OFFSET		0x0210	/* DMA Length Register */
#define XUSB_DMA_STATUS_OFFSET		0x0214	/* DMA Status Register */

/* Endpoint Configuration Space offsets */
#define XUSB_EP_CFGSTATUS_OFFSET	0x00	/* Endpoint Config Status  */
#define XUSB_EP_BUF0COUNT_OFFSET	0x08	/* Buffer 0 Count */
#define XUSB_EP_BUF1COUNT_OFFSET	0x0C	/* Buffer 1 Count */

#define XUSB_CONTROL_USB_READY_MASK	0x80000000 /* USB ready Mask */
#define XUSB_CONTROL_USB_RMTWAKE_MASK	0x40000000 /* Remote wake up mask */

/* Interrupt register related masks.*/
#define XUSB_STATUS_GLOBAL_INTR_MASK	0x80000000 /* Global Intr Enable */
#define XUSB_STATUS_DMADONE_MASK	0x04000000 /* DMA done Mask */
#define XUSB_STATUS_DMAERR_MASK		0x02000000 /* DMA Error Mask */
#define XUSB_STATUS_DMABUSY_MASK	0x80000000 /* DMA Error Mask */
#define XUSB_STATUS_RESUME_MASK		0x01000000 /* USB Resume Mask */
#define XUSB_STATUS_RESET_MASK		0x00800000 /* USB Reset Mask */
#define XUSB_STATUS_SUSPEND_MASK	0x00400000 /* USB Suspend Mask */
#define XUSB_STATUS_DISCONNECT_MASK	0x00200000 /* USB Disconnect Mask */
#define XUSB_STATUS_FIFO_BUFF_RDY_MASK	0x00100000 /* FIFO Buff Ready Mask */
#define XUSB_STATUS_FIFO_BUFF_FREE_MASK	0x00080000 /* FIFO Buff Free Mask */
#define XUSB_STATUS_SETUP_PACKET_MASK	0x00040000 /* Setup packet received */
#define XUSB_STATUS_EP1_BUFF2_COMP_MASK	0x00000200 /* EP 1 Buff 2 Processed */
#define XUSB_STATUS_EP1_BUFF1_COMP_MASK	0x00000002 /* EP 1 Buff 1 Processed */
#define XUSB_STATUS_EP0_BUFF2_COMP_MASK	0x00000100 /* EP 0 Buff 2 Processed */
#define XUSB_STATUS_EP0_BUFF1_COMP_MASK	0x00000001 /* EP 0 Buff 1 Processed */
#define XUSB_STATUS_HIGH_SPEED_MASK	0x00010000 /* USB Speed Mask */
/* Suspend,Reset,Suspend and Disconnect Mask */
#define XUSB_STATUS_INTR_EVENT_MASK	0x01E00000
/* Buffers  completion Mask */
#define XUSB_STATUS_INTR_BUFF_COMP_ALL_MASK	0x0000FEFF
/* Mask for buffer 0 and buffer 1 completion for all Endpoints */
#define XUSB_STATUS_INTR_BUFF_COMP_SHIFT_MASK	0x00000101
#define XUSB_STATUS_EP_BUFF2_SHIFT	8	   /* EP buffer offset */

/* Endpoint Configuration Status Register */
#define XUSB_EP_CFG_VALID_MASK		0x80000000 /* Endpoint Valid bit */
#define XUSB_EP_CFG_STALL_MASK		0x40000000 /* Endpoint Stall bit */
#define XUSB_EP_CFG_DATA_TOGGLE_MASK	0x08000000 /* Endpoint Data toggle */

/* USB device specific global configuration constants.*/
#define XUSB_MAX_ENDPOINTS		8	/* Maximum End Points */
#define XUSB_EP_NUMBER_ZERO		0	/* End point Zero */
/* DPRAM is the source address for DMA transfer */
#define XUSB_DMA_READ_FROM_DPRAM	0x80000000
#define XUSB_DMA_DMASR_BUSY		0x80000000 /* DMA busy */
#define XUSB_DMA_DMASR_ERROR		0x40000000 /* DMA Error */
/*
 * When this bit is set, the DMA buffer ready bit is set by hardware upon
 * DMA transfer completion.
 */
#define XUSB_DMA_BRR_CTRL		0x40000000 /* DMA bufready ctrl bit */
/* Phase States */
#define SETUP_PHASE			0x0000	/* Setup Phase */
#define DATA_PHASE			0x0001  /* Data Phase */
#define STATUS_PHASE			0x0002  /* Status Phase */

#define EP0_MAX_PACKET		64 /* Endpoint 0 maximum packet length */
#define STATUSBUFF_SIZE		2  /* Buffer size for GET_STATUS command */
#define EPNAME_SIZE		4  /* Buffer size for endpoint name */

/* container_of helper macros */
#define to_udc(g)	 container_of((g), struct xusb_udc, gadget)
#define to_xusb_ep(ep)	 container_of((ep), struct xusb_ep, ep_usb)
#define to_xusb_req(req) container_of((req), struct xusb_req, usb_req)

/**
 * struct xusb_req - Xilinx USB device request structure
 * @usb_req: Linux usb request structure
 * @queue: usb device request queue
 * @ep: pointer to xusb_endpoint structure
 */
struct xusb_req {
	struct usb_request usb_req;
	struct list_head queue;
	struct xusb_ep *ep;
};

/**
 * struct xusb_ep - USB end point structure.
 * @ep_usb: usb endpoint instance
 * @queue: endpoint message queue
 * @udc: xilinx usb peripheral driver instance pointer
 * @desc: pointer to the usb endpoint descriptor
 * @rambase: the endpoint buffer address
 * @offset: the endpoint register offset value
 * @name: name of the endpoint
 * @epnumber: endpoint number
 * @maxpacket: maximum packet size the endpoint can store
 * @buffer0count: the size of the packet recieved in the first buffer
 * @buffer1count: the size of the packet received in the second buffer
 * @curbufnum: current buffer of endpoint that will be processed next
 * @buffer0ready: the busy state of first buffer
 * @buffer1ready: the busy state of second buffer
 * @is_in: endpoint direction (IN or OUT)
 * @is_iso: endpoint type(isochronous or non isochronous)
 */
struct xusb_ep {
	struct usb_ep ep_usb;
	struct list_head queue;
	struct xusb_udc *udc;
	const struct usb_endpoint_descriptor *desc;
	u32  rambase;
	u32  offset;
	char name[4];
	u16  epnumber;
	u16  maxpacket;
	u16  buffer0count;
	u16  buffer1count;
	u8   curbufnum;
	bool buffer0ready;
	bool buffer1ready;
	bool is_in;
	bool is_iso;
};

/**
 * struct xusb_udc -  USB peripheral driver structure
 * @gadget: USB gadget driver instance
 * @ep: an array of endpoint structures
 * @driver: pointer to the usb gadget driver instance
 * @setup: usb_ctrlrequest structure for control requests
 * @req: pointer to dummy request for get status command
 * @dev: pointer to device structure in gadget
 * @usb_state: device in suspended state or not
 * @remote_wkp: remote wakeup enabled by host
 * @setupseqtx: tx status
 * @setupseqrx: rx status
 * @addr: the usb device base address
 * @lock: instance of spinlock
 * @dma_enabled: flag indicating whether the dma is included in the system
 * @clk: pointer to struct clk
 * @read_fn: function pointer to read device registers
 * @write_fn: function pointer to write to device registers
 */
struct xusb_udc {
	struct usb_gadget gadget;
	struct xusb_ep ep[8];
	struct usb_gadget_driver *driver;
	struct usb_ctrlrequest setup;
	struct xusb_req *req;
	struct device *dev;
	u32 usb_state;
	u32 remote_wkp;
	u32 setupseqtx;
	u32 setupseqrx;
	void __iomem *addr;
	spinlock_t lock;
	bool dma_enabled;
	struct clk *clk;

	unsigned int (*read_fn)(void __iomem *);
	void (*write_fn)(void __iomem *, u32, u32);
};

/* Endpoint buffer start addresses in the core */
static u32 rambase[8] = { 0x22, 0x1000, 0x1100, 0x1200, 0x1300, 0x1400, 0x1500,
			  0x1600 };

static const char driver_name[] = "xilinx-udc";
static const char ep0name[] = "ep0";

/* Control endpoint configuration.*/
static const struct usb_endpoint_descriptor config_bulk_out_desc = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,
	.bEndpointAddress	= USB_DIR_OUT,
	.bmAttributes		= USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize		= cpu_to_le16(EP0_MAX_PACKET),
};

/**
 * xudc_write32 - little endian write to device registers
 * @addr: base addr of device registers
 * @offset: register offset
 * @val: data to be written
 */
static void xudc_write32(void __iomem *addr, u32 offset, u32 val)
{
	iowrite32(val, addr + offset);
}

/**
 * xudc_read32 - little endian read from device registers
 * @addr: addr of device register
 * Return: value at addr
 */
static unsigned int xudc_read32(void __iomem *addr)
{
	return ioread32(addr);
}

/**
 * xudc_write32_be - big endian write to device registers
 * @addr: base addr of device registers
 * @offset: register offset
 * @val: data to be written
 */
static void xudc_write32_be(void __iomem *addr, u32 offset, u32 val)
{
	iowrite32be(val, addr + offset);
}

/**
 * xudc_read32_be - big endian read from device registers
 * @addr: addr of device register
 * Return: value at addr
 */
static unsigned int xudc_read32_be(void __iomem *addr)
{
	return ioread32be(addr);
}

/**
 * xudc_wrstatus - Sets up the usb device status stages.
 * @udc: pointer to the usb device controller structure.
 */
static void xudc_wrstatus(struct xusb_udc *udc)
{
	struct xusb_ep *ep0 = &udc->ep[XUSB_EP_NUMBER_ZERO];
	u32 epcfgreg;

	epcfgreg = udc->read_fn(udc->addr + ep0->offset)|
				XUSB_EP_CFG_DATA_TOGGLE_MASK;
	udc->write_fn(udc->addr, ep0->offset, epcfgreg);
	udc->write_fn(udc->addr, ep0->offset + XUSB_EP_BUF0COUNT_OFFSET, 0);
	udc->write_fn(udc->addr, XUSB_BUFFREADY_OFFSET, 1);
}

/**
 * xudc_epconfig - Configures the given endpoint.
 * @ep: pointer to the usb device endpoint structure.
 * @udc: pointer to the usb peripheral controller structure.
 *
 * This function configures a specific endpoint with the given configuration
 * data.
 */
static void xudc_epconfig(struct xusb_ep *ep, struct xusb_udc *udc)
{
	u32 epcfgreg;

	/*
	 * Configure the end point direction, type, Max Packet Size and the
	 * EP buffer location.
	 */
	epcfgreg = ((ep->is_in << 29) | (ep->is_iso << 28) |
		   (ep->ep_usb.maxpacket << 15) | (ep->rambase));
	udc->write_fn(udc->addr, ep->offset, epcfgreg);

	/* Set the Buffer count and the Buffer ready bits.*/
	udc->write_fn(udc->addr, ep->offset + XUSB_EP_BUF0COUNT_OFFSET,
		      ep->buffer0count);
	udc->write_fn(udc->addr, ep->offset + XUSB_EP_BUF1COUNT_OFFSET,
		      ep->buffer1count);
	if (ep->buffer0ready)
		udc->write_fn(udc->addr, XUSB_BUFFREADY_OFFSET,
			      1 << ep->epnumber);
	if (ep->buffer1ready)
		udc->write_fn(udc->addr, XUSB_BUFFREADY_OFFSET,
			      1 << (ep->epnumber + XUSB_STATUS_EP_BUFF2_SHIFT));
}

/**
 * xudc_start_dma - Starts DMA transfer.
 * @ep: pointer to the usb device endpoint structure.
 * @src: DMA source address.
 * @dst: DMA destination address.
 * @length: number of bytes to transfer.
 *
 * Return: 0 on success, error code on failure
 *
 * This function starts DMA transfer by writing to DMA source,
 * destination and lenth registers.
 */
static int xudc_start_dma(struct xusb_ep *ep, dma_addr_t src,
			  dma_addr_t dst, u32 length)
{
	struct xusb_udc *udc = ep->udc;
	int rc = 0;
	u32 timeout = 500;
	u32 reg;

	/*
	 * Set the addresses in the DMA source and
	 * destination registers and then set the length
	 * into the DMA length register.
	 */
	udc->write_fn(udc->addr, XUSB_DMA_DSAR_ADDR_OFFSET, src);
	udc->write_fn(udc->addr, XUSB_DMA_DDAR_ADDR_OFFSET, dst);
	udc->write_fn(udc->addr, XUSB_DMA_LENGTH_OFFSET, length);

	/*
	 * Wait till DMA transaction is complete and
	 * check whether the DMA transaction was
	 * successful.
	 */
	do {
		reg = udc->read_fn(udc->addr + XUSB_DMA_STATUS_OFFSET);
		if (!(reg &  XUSB_DMA_DMASR_BUSY))
			break;

		/*
		 * We can't sleep here, because it's also called from
		 * interrupt context.
		 */
		timeout--;
		if (!timeout) {
			dev_err(udc->dev, "DMA timeout\n");
			return -ETIMEDOUT;
		}
		udelay(1);
	} while (1);

	if ((udc->read_fn(udc->addr + XUSB_DMA_STATUS_OFFSET) &
			  XUSB_DMA_DMASR_ERROR) == XUSB_DMA_DMASR_ERROR){
		dev_err(udc->dev, "DMA Error\n");
		rc = -EINVAL;
	}

	return rc;
}

/**
 * xudc_dma_send - Sends IN data using DMA.
 * @ep: pointer to the usb device endpoint structure.
 * @req: pointer to the usb request structure.
 * @buffer: pointer to data to be sent.
 * @length: number of bytes to send.
 *
 * Return: 0 on success, -EAGAIN if no buffer is free and error
 *	   code on failure.
 *
 * This function sends data using DMA.
 */
static int xudc_dma_send(struct xusb_ep *ep, struct xusb_req *req,
			 u8 *buffer, u32 length)
{
	u32 *eprambase;
	dma_addr_t src;
	dma_addr_t dst;
	struct xusb_udc *udc = ep->udc;

	src = req->usb_req.dma + req->usb_req.actual;
	if (req->usb_req.length)
		dma_sync_single_for_device(udc->dev, src,
					   length, DMA_TO_DEVICE);
	if (!ep->curbufnum && !ep->buffer0ready) {
		/* Get the Buffer address and copy the transmit data.*/
		eprambase = (u32 __force *)(udc->addr + ep->rambase);
		dst = virt_to_phys(eprambase);
		udc->write_fn(udc->addr, ep->offset +
			      XUSB_EP_BUF0COUNT_OFFSET, length);
		udc->write_fn(udc->addr, XUSB_DMA_CONTROL_OFFSET,
			      XUSB_DMA_BRR_CTRL | (1 << ep->epnumber));
		ep->buffer0ready = 1;
		ep->curbufnum = 1;
	} else if (ep->curbufnum && !ep->buffer1ready) {
		/* Get the Buffer address and copy the transmit data.*/
		eprambase = (u32 __force *)(udc->addr + ep->rambase +
			     ep->ep_usb.maxpacket);
		dst = virt_to_phys(eprambase);
		udc->write_fn(udc->addr, ep->offset +
			      XUSB_EP_BUF1COUNT_OFFSET, length);
		udc->write_fn(udc->addr, XUSB_DMA_CONTROL_OFFSET,
			      XUSB_DMA_BRR_CTRL | (1 << (ep->epnumber +
			      XUSB_STATUS_EP_BUFF2_SHIFT)));
		ep->buffer1ready = 1;
		ep->curbufnum = 0;
	} else {
		/* None of ping pong buffers are ready currently .*/
		return -EAGAIN;
	}

	return xudc_start_dma(ep, src, dst, length);
}

/**
 * xudc_dma_receive - Receives OUT data using DMA.
 * @ep: pointer to the usb device endpoint structure.
 * @req: pointer to the usb request structure.
 * @buffer: pointer to storage buffer of received data.
 * @length: number of bytes to receive.
 *
 * Return: 0 on success, -EAGAIN if no buffer is free and error
 *	   code on failure.
 *
 * This function receives data using DMA.
 */
static int xudc_dma_receive(struct xusb_ep *ep, struct xusb_req *req,
			    u8 *buffer, u32 length)
{
	u32 *eprambase;
	dma_addr_t src;
	dma_addr_t dst;
	struct xusb_udc *udc = ep->udc;

	dst = req->usb_req.dma + req->usb_req.actual;
	if (!ep->curbufnum && !ep->buffer0ready) {
		/* Get the Buffer address and copy the transmit data */
		eprambase = (u32 __force *)(udc->addr + ep->rambase);
		src = virt_to_phys(eprambase);
		udc->write_fn(udc->addr, XUSB_DMA_CONTROL_OFFSET,
			      XUSB_DMA_BRR_CTRL | XUSB_DMA_READ_FROM_DPRAM |
			      (1 << ep->epnumber));
		ep->buffer0ready = 1;
		ep->curbufnum = 1;
	} else if (ep->curbufnum && !ep->buffer1ready) {
		/* Get the Buffer address and copy the transmit data */
		eprambase = (u32 __force *)(udc->addr +
			     ep->rambase + ep->ep_usb.maxpacket);
		src = virt_to_phys(eprambase);
		udc->write_fn(udc->addr, XUSB_DMA_CONTROL_OFFSET,
			      XUSB_DMA_BRR_CTRL | XUSB_DMA_READ_FROM_DPRAM |
			      (1 << (ep->epnumber +
			      XUSB_STATUS_EP_BUFF2_SHIFT)));
		ep->buffer1ready = 1;
		ep->curbufnum = 0;
	} else {
		/* None of the ping-pong buffers are ready currently */
		return -EAGAIN;
	}

	return xudc_start_dma(ep, src, dst, length);
}

/**
 * xudc_eptxrx - Transmits or receives data to or from an endpoint.
 * @ep: pointer to the usb endpoint configuration structure.
 * @req: pointer to the usb request structure.
 * @bufferptr: pointer to buffer containing the data to be sent.
 * @bufferlen: The number of data bytes to be sent.
 *
 * Return: 0 on success, -EAGAIN if no buffer is free.
 *
 * This function copies the transmit/receive data to/from the end point buffer
 * and enables the buffer for transmission/reception.
 */
static int xudc_eptxrx(struct xusb_ep *ep, struct xusb_req *req,
		       u8 *bufferptr, u32 bufferlen)
{
	u32 *eprambase;
	u32 bytestosend;
	int rc = 0;
	struct xusb_udc *udc = ep->udc;

	bytestosend = bufferlen;
	if (udc->dma_enabled) {
		if (ep->is_in)
			rc = xudc_dma_send(ep, req, bufferptr, bufferlen);
		else
			rc = xudc_dma_receive(ep, req, bufferptr, bufferlen);
		return rc;
	}
	/* Put the transmit buffer into the correct ping-pong buffer.*/
	if (!ep->curbufnum && !ep->buffer0ready) {
		/* Get the Buffer address and copy the transmit data.*/
		eprambase = (u32 __force *)(udc->addr + ep->rambase);
		if (ep->is_in) {
			memcpy(eprambase, bufferptr, bytestosend);
			udc->write_fn(udc->addr, ep->offset +
				      XUSB_EP_BUF0COUNT_OFFSET, bufferlen);
		} else {
			memcpy(bufferptr, eprambase, bytestosend);
		}
		/*
		 * Enable the buffer for transmission.
		 */
		udc->write_fn(udc->addr, XUSB_BUFFREADY_OFFSET,
			      1 << ep->epnumber);
		ep->buffer0ready = 1;
		ep->curbufnum = 1;
	} else if (ep->curbufnum && !ep->buffer1ready) {
		/* Get the Buffer address and copy the transmit data.*/
		eprambase = (u32 __force *)(udc->addr + ep->rambase +
			     ep->ep_usb.maxpacket);
		if (ep->is_in) {
			memcpy(eprambase, bufferptr, bytestosend);
			udc->write_fn(udc->addr, ep->offset +
				      XUSB_EP_BUF1COUNT_OFFSET, bufferlen);
		} else {
			memcpy(bufferptr, eprambase, bytestosend);
		}
		/*
		 * Enable the buffer for transmission.
		 */
		udc->write_fn(udc->addr, XUSB_BUFFREADY_OFFSET,
			      1 << (ep->epnumber + XUSB_STATUS_EP_BUFF2_SHIFT));
		ep->buffer1ready = 1;
		ep->curbufnum = 0;
	} else {
		/* None of the ping-pong buffers are ready currently */
		return -EAGAIN;
	}
	return rc;
}

/**
 * xudc_done - Exeutes the endpoint data transfer completion tasks.
 * @ep: pointer to the usb device endpoint structure.
 * @req: pointer to the usb request structure.
 * @status: Status of the data transfer.
 *
 * Deletes the message from the queue and updates data transfer completion
 * status.
 */
static void xudc_done(struct xusb_ep *ep, struct xusb_req *req, int status)
{
	struct xusb_udc *udc = ep->udc;

	list_del_init(&req->queue);

	if (req->usb_req.status == -EINPROGRESS)
		req->usb_req.status = status;
	else
		status = req->usb_req.status;

	if (status && status != -ESHUTDOWN)
		dev_dbg(udc->dev, "%s done %p, status %d\n",
			ep->ep_usb.name, req, status);
	/* unmap request if DMA is present*/
	if (udc->dma_enabled && ep->epnumber && req->usb_req.length)
		usb_gadget_unmap_request(&udc->gadget, &req->usb_req,
					 ep->is_in);

	if (req->usb_req.complete) {
		spin_unlock(&udc->lock);
		req->usb_req.complete(&ep->ep_usb, &req->usb_req);
		spin_lock(&udc->lock);
	}
}

/**
 * xudc_read_fifo - Reads the data from the given endpoint buffer.
 * @ep: pointer to the usb device endpoint structure.
 * @req: pointer to the usb request structure.
 *
 * Return: 0 if request is completed and -EAGAIN if not completed.
 *
 * Pulls OUT packet data from the endpoint buffer.
 */
static int xudc_read_fifo(struct xusb_ep *ep, struct xusb_req *req)
{
	u8 *buf;
	u32 is_short, count, bufferspace;
	u8 bufoffset;
	u8 two_pkts = 0;
	int ret;
	int retval = -EAGAIN;
	struct xusb_udc *udc = ep->udc;

	if (ep->buffer0ready && ep->buffer1ready) {
		dev_dbg(udc->dev, "Packet NOT ready!\n");
		return retval;
	}
top:
	if (ep->curbufnum)
		bufoffset = XUSB_EP_BUF1COUNT_OFFSET;
	else
		bufoffset = XUSB_EP_BUF0COUNT_OFFSET;

	count = udc->read_fn(udc->addr + ep->offset + bufoffset);

	if (!ep->buffer0ready && !ep->buffer1ready)
		two_pkts = 1;

	buf = req->usb_req.buf + req->usb_req.actual;
	prefetchw(buf);
	bufferspace = req->usb_req.length - req->usb_req.actual;
	is_short = count < ep->ep_usb.maxpacket;

	if (unlikely(!bufferspace)) {
		/*
		 * This happens when the driver's buffer
		 * is smaller than what the host sent.
		 * discard the extra data.
		 */
		if (req->usb_req.status != -EOVERFLOW)
			dev_dbg(udc->dev, "%s overflow %d\n",
				ep->ep_usb.name, count);
		req->usb_req.status = -EOVERFLOW;
		xudc_done(ep, req, -EOVERFLOW);
		return 0;
	}

	ret = xudc_eptxrx(ep, req, buf, count);
	switch (ret) {
	case 0:
		req->usb_req.actual += min(count, bufferspace);
		dev_dbg(udc->dev, "read %s, %d bytes%s req %p %d/%d\n",
			ep->ep_usb.name, count, is_short ? "/S" : "", req,
			req->usb_req.actual, req->usb_req.length);
		bufferspace -= count;
		/* Completion */
		if ((req->usb_req.actual == req->usb_req.length) || is_short) {
			if (udc->dma_enabled && req->usb_req.length)
				dma_sync_single_for_cpu(udc->dev,
							req->usb_req.dma,
							req->usb_req.actual,
							DMA_FROM_DEVICE);
			xudc_done(ep, req, 0);
			return 0;
		}
		if (two_pkts) {
			two_pkts = 0;
			goto top;
		}
		break;
	case -EAGAIN:
		dev_dbg(udc->dev, "receive busy\n");
		break;
	case -EINVAL:
	case -ETIMEDOUT:
		/* DMA error, dequeue the request */
		xudc_done(ep, req, -ECONNRESET);
		retval = 0;
		break;
	}

	return retval;
}

/**
 * xudc_write_fifo - Writes data into the given endpoint buffer.
 * @ep: pointer to the usb device endpoint structure.
 * @req: pointer to the usb request structure.
 *
 * Return: 0 if request is completed and -EAGAIN if not completed.
 *
 * Loads endpoint buffer for an IN packet.
 */
static int xudc_write_fifo(struct xusb_ep *ep, struct xusb_req *req)
{
	u32 max;
	u32 length;
	int ret;
	int retval = -EAGAIN;
	struct xusb_udc *udc = ep->udc;
	int is_last, is_short = 0;
	u8 *buf;

	max = le16_to_cpu(ep->desc->wMaxPacketSize);
	buf = req->usb_req.buf + req->usb_req.actual;
	prefetch(buf);
	length = req->usb_req.length - req->usb_req.actual;
	length = min(length, max);

	ret = xudc_eptxrx(ep, req, buf, length);
	switch (ret) {
	case 0:
		req->usb_req.actual += length;
		if (unlikely(length != max)) {
			is_last = is_short = 1;
		} else {
			if (likely(req->usb_req.length !=
				   req->usb_req.actual) || req->usb_req.zero)
				is_last = 0;
			else
				is_last = 1;
		}
		dev_dbg(udc->dev, "%s: wrote %s %d bytes%s%s %d left %p\n",
			__func__, ep->ep_usb.name, length, is_last ? "/L" : "",
			is_short ? "/S" : "",
			req->usb_req.length - req->usb_req.actual, req);
		/* completion */
		if (is_last) {
			xudc_done(ep, req, 0);
			retval = 0;
		}
		break;
	case -EAGAIN:
		dev_dbg(udc->dev, "Send busy\n");
		break;
	case -EINVAL:
	case -ETIMEDOUT:
		/* DMA error, dequeue the request */
		xudc_done(ep, req, -ECONNRESET);
		retval = 0;
		break;
	}

	return retval;
}

/**
 * xudc_nuke - Cleans up the data transfer message list.
 * @ep: pointer to the usb device endpoint structure.
 * @status: Status of the data transfer.
 */
static void xudc_nuke(struct xusb_ep *ep, int status)
{
	struct xusb_req *req;

	while (!list_empty(&ep->queue)) {
		req = list_first_entry(&ep->queue, struct xusb_req, queue);
		xudc_done(ep, req, status);
	}
}

/**
 * xudc_ep_set_halt - Stalls/unstalls the given endpoint.
 * @_ep: pointer to the usb device endpoint structure.
 * @value: value to indicate stall/unstall.
 *
 * Return: 0 for success and error value on failure
 */
static int xudc_ep_set_halt(struct usb_ep *_ep, int value)
{
	struct xusb_ep *ep = to_xusb_ep(_ep);
	struct xusb_udc *udc;
	unsigned long flags;
	u32 epcfgreg;

	if (!_ep || (!ep->desc && ep->epnumber)) {
		pr_debug("%s: bad ep or descriptor\n", __func__);
		return -EINVAL;
	}
	udc = ep->udc;

	if (ep->is_in && (!list_empty(&ep->queue)) && value) {
		dev_dbg(udc->dev, "requests pending can't halt\n");
		return -EAGAIN;
	}

	if (ep->buffer0ready || ep->buffer1ready) {
		dev_dbg(udc->dev, "HW buffers busy can't halt\n");
		return -EAGAIN;
	}

	spin_lock_irqsave(&udc->lock, flags);

	if (value) {
		/* Stall the device.*/
		epcfgreg = udc->read_fn(udc->addr + ep->offset);
		epcfgreg |= XUSB_EP_CFG_STALL_MASK;
		udc->write_fn(udc->addr, ep->offset, epcfgreg);
	} else {
		/* Unstall the device.*/
		epcfgreg = udc->read_fn(udc->addr + ep->offset);
		epcfgreg &= ~XUSB_EP_CFG_STALL_MASK;
		udc->write_fn(udc->addr, ep->offset, epcfgreg);
		if (ep->epnumber) {
			/* Reset the toggle bit.*/
			epcfgreg = udc->read_fn(ep->udc->addr + ep->offset);
			epcfgreg &= ~XUSB_EP_CFG_DATA_TOGGLE_MASK;
			udc->write_fn(udc->addr, ep->offset, epcfgreg);
		}
	}

	spin_unlock_irqrestore(&udc->lock, flags);
	return 0;
}

/**
 * __xudc_ep_enable - Enables the given endpoint.
 * @ep: pointer to the xusb endpoint structure.
 * @desc: pointer to usb endpoint descriptor.
 *
 * Return: 0 for success and error value on failure
 */
static int __xudc_ep_enable(struct xusb_ep *ep,
			    const struct usb_endpoint_descriptor *desc)
{
	struct xusb_udc *udc = ep->udc;
	u32 tmp;
	u32 epcfg;
	u32 ier;
	u16 maxpacket;

	ep->is_in = ((desc->bEndpointAddress & USB_DIR_IN) != 0);
	/* Bit 3...0:endpoint number */
	ep->epnumber = (desc->bEndpointAddress & 0x0f);
	ep->desc = desc;
	ep->ep_usb.desc = desc;
	tmp = desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK;
	ep->ep_usb.maxpacket = maxpacket = le16_to_cpu(desc->wMaxPacketSize);

	switch (tmp) {
	case USB_ENDPOINT_XFER_CONTROL:
		dev_dbg(udc->dev, "only one control endpoint\n");
		/* NON- ISO */
		ep->is_iso = 0;
		return -EINVAL;
	case USB_ENDPOINT_XFER_INT:
		/* NON- ISO */
		ep->is_iso = 0;
		if (maxpacket > 64) {
			dev_dbg(udc->dev, "bogus maxpacket %d\n", maxpacket);
			return -EINVAL;
		}
		break;
	case USB_ENDPOINT_XFER_BULK:
		/* NON- ISO */
		ep->is_iso = 0;
		if (!(is_power_of_2(maxpacket) && maxpacket >= 8 &&
				maxpacket <= 512)) {
			dev_dbg(udc->dev, "bogus maxpacket %d\n", maxpacket);
			return -EINVAL;
		}
		break;
	case USB_ENDPOINT_XFER_ISOC:
		/* ISO */
		ep->is_iso = 1;
		break;
	}

	ep->buffer0ready = false;
	ep->buffer1ready = false;
	ep->curbufnum = 0;
	ep->rambase = rambase[ep->epnumber];
	xudc_epconfig(ep, udc);

	dev_dbg(udc->dev, "Enable Endpoint %d max pkt is %d\n",
		ep->epnumber, maxpacket);

	/* Enable the End point.*/
	epcfg = udc->read_fn(udc->addr + ep->offset);
	epcfg |= XUSB_EP_CFG_VALID_MASK;
	udc->write_fn(udc->addr, ep->offset, epcfg);
	if (ep->epnumber)
		ep->rambase <<= 2;

	/* Enable buffer completion interrupts for endpoint */
	ier = udc->read_fn(udc->addr + XUSB_IER_OFFSET);
	ier |= (XUSB_STATUS_INTR_BUFF_COMP_SHIFT_MASK << ep->epnumber);
	udc->write_fn(udc->addr, XUSB_IER_OFFSET, ier);

	/* for OUT endpoint set buffers ready to receive */
	if (ep->epnumber && !ep->is_in) {
		udc->write_fn(udc->addr, XUSB_BUFFREADY_OFFSET,
			      1 << ep->epnumber);
		ep->buffer0ready = true;
		udc->write_fn(udc->addr, XUSB_BUFFREADY_OFFSET,
			     (1 << (ep->epnumber +
			      XUSB_STATUS_EP_BUFF2_SHIFT)));
		ep->buffer1ready = true;
	}

	return 0;
}

/**
 * xudc_ep_enable - Enables the given endpoint.
 * @_ep: pointer to the usb endpoint structure.
 * @desc: pointer to usb endpoint descriptor.
 *
 * Return: 0 for success and error value on failure
 */
static int xudc_ep_enable(struct usb_ep *_ep,
			  const struct usb_endpoint_descriptor *desc)
{
	struct xusb_ep *ep;
	struct xusb_udc *udc;
	unsigned long flags;
	int ret;

	if (!_ep || !desc || desc->bDescriptorType != USB_DT_ENDPOINT) {
		pr_debug("%s: bad ep or descriptor\n", __func__);
		return -EINVAL;
	}

	ep = to_xusb_ep(_ep);
	udc = ep->udc;

	if (!udc->driver || udc->gadget.speed == USB_SPEED_UNKNOWN) {
		dev_dbg(udc->dev, "bogus device state\n");
		return -ESHUTDOWN;
	}

	spin_lock_irqsave(&udc->lock, flags);
	ret = __xudc_ep_enable(ep, desc);
	spin_unlock_irqrestore(&udc->lock, flags);

	return ret;
}

/**
 * xudc_ep_disable - Disables the given endpoint.
 * @_ep: pointer to the usb endpoint structure.
 *
 * Return: 0 for success and error value on failure
 */
static int xudc_ep_disable(struct usb_ep *_ep)
{
	struct xusb_ep *ep;
	unsigned long flags;
	u32 epcfg;
	struct xusb_udc *udc;

	if (!_ep) {
		pr_debug("%s: invalid ep\n", __func__);
		return -EINVAL;
	}

	ep = to_xusb_ep(_ep);
	udc = ep->udc;

	spin_lock_irqsave(&udc->lock, flags);

	xudc_nuke(ep, -ESHUTDOWN);

	/* Restore the endpoint's pristine config */
	ep->desc = NULL;
	ep->ep_usb.desc = NULL;

	dev_dbg(udc->dev, "USB Ep %d disable\n ", ep->epnumber);
	/* Disable the endpoint.*/
	epcfg = udc->read_fn(udc->addr + ep->offset);
	epcfg &= ~XUSB_EP_CFG_VALID_MASK;
	udc->write_fn(udc->addr, ep->offset, epcfg);

	spin_unlock_irqrestore(&udc->lock, flags);
	return 0;
}

/**
 * xudc_ep_alloc_request - Initializes the request queue.
 * @_ep: pointer to the usb endpoint structure.
 * @gfp_flags: Flags related to the request call.
 *
 * Return: pointer to request structure on success and a NULL on failure.
 */
static struct usb_request *xudc_ep_alloc_request(struct usb_ep *_ep,
						 gfp_t gfp_flags)
{
	struct xusb_ep *ep = to_xusb_ep(_ep);
	struct xusb_req *req;

	req = kzalloc(sizeof(*req), gfp_flags);
	if (!req)
		return NULL;

	req->ep = ep;
	INIT_LIST_HEAD(&req->queue);
	return &req->usb_req;
}

/**
 * xudc_free_request - Releases the request from queue.
 * @_ep: pointer to the usb device endpoint structure.
 * @_req: pointer to the usb request structure.
 */
static void xudc_free_request(struct usb_ep *_ep, struct usb_request *_req)
{
	struct xusb_req *req = to_xusb_req(_req);

	kfree(req);
}

/**
 * __xudc_ep0_queue - Adds the request to endpoint 0 queue.
 * @ep0: pointer to the xusb endpoint 0 structure.
 * @req: pointer to the xusb request structure.
 *
 * Return: 0 for success and error value on failure
 */
static int __xudc_ep0_queue(struct xusb_ep *ep0, struct xusb_req *req)
{
	struct xusb_udc *udc = ep0->udc;
	u32 length;
	u8 *corebuf;

	if (!udc->driver || udc->gadget.speed == USB_SPEED_UNKNOWN) {
		dev_dbg(udc->dev, "%s, bogus device state\n", __func__);
		return -EINVAL;
	}
	if (!list_empty(&ep0->queue)) {
		dev_dbg(udc->dev, "%s:ep0 busy\n", __func__);
		return -EBUSY;
	}

	req->usb_req.status = -EINPROGRESS;
	req->usb_req.actual = 0;

	list_add_tail(&req->queue, &ep0->queue);

	if (udc->setup.bRequestType & USB_DIR_IN) {
		prefetch(req->usb_req.buf);
		length = req->usb_req.length;
		corebuf = (void __force *) ((ep0->rambase << 2) +
			   udc->addr);
		length = req->usb_req.actual = min_t(u32, length,
						     EP0_MAX_PACKET);
		memcpy(corebuf, req->usb_req.buf, length);
		udc->write_fn(udc->addr, XUSB_EP_BUF0COUNT_OFFSET, length);
		udc->write_fn(udc->addr, XUSB_BUFFREADY_OFFSET, 1);
	} else {
		if (udc->setup.wLength) {
			/* Enable EP0 buffer to receive data */
			udc->write_fn(udc->addr, XUSB_EP_BUF0COUNT_OFFSET, 0);
			udc->write_fn(udc->addr, XUSB_BUFFREADY_OFFSET, 1);
		} else {
			xudc_wrstatus(udc);
		}
	}

	return 0;
}

/**
 * xudc_ep0_queue - Adds the request to endpoint 0 queue.
 * @_ep: pointer to the usb endpoint 0 structure.
 * @_req: pointer to the usb request structure.
 * @gfp_flags: Flags related to the request call.
 *
 * Return: 0 for success and error value on failure
 */
static int xudc_ep0_queue(struct usb_ep *_ep, struct usb_request *_req,
			  gfp_t gfp_flags)
{
	struct xusb_req *req	= to_xusb_req(_req);
	struct xusb_ep	*ep0	= to_xusb_ep(_ep);
	struct xusb_udc *udc	= ep0->udc;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&udc->lock, flags);
	ret = __xudc_ep0_queue(ep0, req);
	spin_unlock_irqrestore(&udc->lock, flags);

	return ret;
}

/**
 * xudc_ep_queue - Adds the request to endpoint queue.
 * @_ep: pointer to the usb endpoint structure.
 * @_req: pointer to the usb request structure.
 * @gfp_flags: Flags related to the request call.
 *
 * Return: 0 for success and error value on failure
 */
static int xudc_ep_queue(struct usb_ep *_ep, struct usb_request *_req,
			 gfp_t gfp_flags)
{
	struct xusb_req *req = to_xusb_req(_req);
	struct xusb_ep	*ep  = to_xusb_ep(_ep);
	struct xusb_udc *udc = ep->udc;
	int  ret;
	unsigned long flags;

	if (!ep->desc) {
		dev_dbg(udc->dev, "%s: queuing request to disabled %s\n",
			__func__, ep->name);
		return -ESHUTDOWN;
	}

	if (!udc->driver || udc->gadget.speed == USB_SPEED_UNKNOWN) {
		dev_dbg(udc->dev, "%s, bogus device state\n", __func__);
		return -EINVAL;
	}

	spin_lock_irqsave(&udc->lock, flags);

	_req->status = -EINPROGRESS;
	_req->actual = 0;

	if (udc->dma_enabled) {
		ret = usb_gadget_map_request(&udc->gadget, &req->usb_req,
					     ep->is_in);
		if (ret) {
			dev_dbg(udc->dev, "gadget_map failed ep%d\n",
				ep->epnumber);
			spin_unlock_irqrestore(&udc->lock, flags);
			return -EAGAIN;
		}
	}

	if (list_empty(&ep->queue)) {
		if (ep->is_in) {
			dev_dbg(udc->dev, "xudc_write_fifo from ep_queue\n");
			if (!xudc_write_fifo(ep, req))
				req = NULL;
		} else {
			dev_dbg(udc->dev, "xudc_read_fifo from ep_queue\n");
			if (!xudc_read_fifo(ep, req))
				req = NULL;
		}
	}

	if (req != NULL)
		list_add_tail(&req->queue, &ep->queue);

	spin_unlock_irqrestore(&udc->lock, flags);
	return 0;
}

/**
 * xudc_ep_dequeue - Removes the request from the queue.
 * @_ep: pointer to the usb device endpoint structure.
 * @_req: pointer to the usb request structure.
 *
 * Return: 0 for success and error value on failure
 */
static int xudc_ep_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct xusb_ep *ep	= to_xusb_ep(_ep);
	struct xusb_req *req	= to_xusb_req(_req);
	struct xusb_udc *udc	= ep->udc;
	unsigned long flags;

	spin_lock_irqsave(&udc->lock, flags);
	/* Make sure it's actually queued on this endpoint */
	list_for_each_entry(req, &ep->queue, queue) {
		if (&req->usb_req == _req)
			break;
	}
	if (&req->usb_req != _req) {
		spin_unlock_irqrestore(&udc->lock, flags);
		return -EINVAL;
	}
	xudc_done(ep, req, -ECONNRESET);
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

/**
 * xudc_ep0_enable - Enables the given endpoint.
 * @ep: pointer to the usb endpoint structure.
 * @desc: pointer to usb endpoint descriptor.
 *
 * Return: error always.
 *
 * endpoint 0 enable should not be called by gadget layer.
 */
static int xudc_ep0_enable(struct usb_ep *ep,
			   const struct usb_endpoint_descriptor *desc)
{
	return -EINVAL;
}

/**
 * xudc_ep0_disable - Disables the given endpoint.
 * @ep: pointer to the usb endpoint structure.
 *
 * Return: error always.
 *
 * endpoint 0 disable should not be called by gadget layer.
 */
static int xudc_ep0_disable(struct usb_ep *ep)
{
	return -EINVAL;
}

static const struct usb_ep_ops xusb_ep0_ops = {
	.enable		= xudc_ep0_enable,
	.disable	= xudc_ep0_disable,
	.alloc_request	= xudc_ep_alloc_request,
	.free_request	= xudc_free_request,
	.queue		= xudc_ep0_queue,
	.dequeue	= xudc_ep_dequeue,
	.set_halt	= xudc_ep_set_halt,
};

static const struct usb_ep_ops xusb_ep_ops = {
	.enable		= xudc_ep_enable,
	.disable	= xudc_ep_disable,
	.alloc_request	= xudc_ep_alloc_request,
	.free_request	= xudc_free_request,
	.queue		= xudc_ep_queue,
	.dequeue	= xudc_ep_dequeue,
	.set_halt	= xudc_ep_set_halt,
};

/**
 * xudc_get_frame - Reads the current usb frame number.
 * @gadget: pointer to the usb gadget structure.
 *
 * Return: current frame number for success and error value on failure.
 */
static int xudc_get_frame(struct usb_gadget *gadget)
{
	struct xusb_udc *udc;
	int frame;

	if (!gadget)
		return -ENODEV;

	udc = to_udc(gadget);
	frame = udc->read_fn(udc->addr + XUSB_FRAMENUM_OFFSET);
	return frame;
}

/**
 * xudc_wakeup - Send remote wakeup signal to host
 * @gadget: pointer to the usb gadget structure.
 *
 * Return: 0 on success and error on failure
 */
static int xudc_wakeup(struct usb_gadget *gadget)
{
	struct xusb_udc *udc = to_udc(gadget);
	u32 crtlreg;
	int status = -EINVAL;
	unsigned long flags;

	spin_lock_irqsave(&udc->lock, flags);

	/* Remote wake up not enabled by host */
	if (!udc->remote_wkp)
		goto done;

	crtlreg = udc->read_fn(udc->addr + XUSB_CONTROL_OFFSET);
	crtlreg |= XUSB_CONTROL_USB_RMTWAKE_MASK;
	/* set remote wake up bit */
	udc->write_fn(udc->addr, XUSB_CONTROL_OFFSET, crtlreg);
	/*
	 * wait for a while and reset remote wake up bit since this bit
	 * is not cleared by HW after sending remote wakeup to host.
	 */
	mdelay(2);

	crtlreg &= ~XUSB_CONTROL_USB_RMTWAKE_MASK;
	udc->write_fn(udc->addr, XUSB_CONTROL_OFFSET, crtlreg);
	status = 0;
done:
	spin_unlock_irqrestore(&udc->lock, flags);
	return status;
}

/**
 * xudc_pullup - start/stop USB traffic
 * @gadget: pointer to the usb gadget structure.
 * @is_on: flag to start or stop
 *
 * Return: 0 always
 *
 * This function starts/stops SIE engine of IP based on is_on.
 */
static int xudc_pullup(struct usb_gadget *gadget, int is_on)
{
	struct xusb_udc *udc = to_udc(gadget);
	unsigned long flags;
	u32 crtlreg;

	spin_lock_irqsave(&udc->lock, flags);

	crtlreg = udc->read_fn(udc->addr + XUSB_CONTROL_OFFSET);
	if (is_on)
		crtlreg |= XUSB_CONTROL_USB_READY_MASK;
	else
		crtlreg &= ~XUSB_CONTROL_USB_READY_MASK;

	udc->write_fn(udc->addr, XUSB_CONTROL_OFFSET, crtlreg);

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

/**
 * xudc_eps_init - initialize endpoints.
 * @udc: pointer to the usb device controller structure.
 */
static void xudc_eps_init(struct xusb_udc *udc)
{
	u32 ep_number;

	INIT_LIST_HEAD(&udc->gadget.ep_list);

	for (ep_number = 0; ep_number < XUSB_MAX_ENDPOINTS; ep_number++) {
		struct xusb_ep *ep = &udc->ep[ep_number];

		if (ep_number) {
			list_add_tail(&ep->ep_usb.ep_list,
				      &udc->gadget.ep_list);
			usb_ep_set_maxpacket_limit(&ep->ep_usb,
						  (unsigned short) ~0);
			snprintf(ep->name, EPNAME_SIZE, "ep%d", ep_number);
			ep->ep_usb.name = ep->name;
			ep->ep_usb.ops = &xusb_ep_ops;

			ep->ep_usb.caps.type_iso = true;
			ep->ep_usb.caps.type_bulk = true;
			ep->ep_usb.caps.type_int = true;
		} else {
			ep->ep_usb.name = ep0name;
			usb_ep_set_maxpacket_limit(&ep->ep_usb, EP0_MAX_PACKET);
			ep->ep_usb.ops = &xusb_ep0_ops;

			ep->ep_usb.caps.type_control = true;
		}

		ep->ep_usb.caps.dir_in = true;
		ep->ep_usb.caps.dir_out = true;

		ep->udc = udc;
		ep->epnumber = ep_number;
		ep->desc = NULL;
		/*
		 * The configuration register address offset between
		 * each endpoint is 0x10.
		 */
		ep->offset = XUSB_EP0_CONFIG_OFFSET + (ep_number * 0x10);
		ep->is_in = 0;
		ep->is_iso = 0;
		ep->maxpacket = 0;
		xudc_epconfig(ep, udc);

		/* Initialize one queue per endpoint */
		INIT_LIST_HEAD(&ep->queue);
	}
}

/**
 * xudc_stop_activity - Stops any further activity on the device.
 * @udc: pointer to the usb device controller structure.
 */
static void xudc_stop_activity(struct xusb_udc *udc)
{
	int i;
	struct xusb_ep *ep;

	for (i = 0; i < XUSB_MAX_ENDPOINTS; i++) {
		ep = &udc->ep[i];
		xudc_nuke(ep, -ESHUTDOWN);
	}
}

/**
 * xudc_start - Starts the device.
 * @gadget: pointer to the usb gadget structure
 * @driver: pointer to gadget driver structure
 *
 * Return: zero on success and error on failure
 */
static int xudc_start(struct usb_gadget *gadget,
		      struct usb_gadget_driver *driver)
{
	struct xusb_udc *udc	= to_udc(gadget);
	struct xusb_ep *ep0	= &udc->ep[XUSB_EP_NUMBER_ZERO];
	const struct usb_endpoint_descriptor *desc = &config_bulk_out_desc;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&udc->lock, flags);

	if (udc->driver) {
		dev_err(udc->dev, "%s is already bound to %s\n",
			udc->gadget.name, udc->driver->driver.name);
		ret = -EBUSY;
		goto err;
	}

	/* hook up the driver */
	udc->driver = driver;
	udc->gadget.speed = driver->max_speed;

	/* Enable the control endpoint. */
	ret = __xudc_ep_enable(ep0, desc);

	/* Set device address and remote wakeup to 0 */
	udc->write_fn(udc->addr, XUSB_ADDRESS_OFFSET, 0);
	udc->remote_wkp = 0;
err:
	spin_unlock_irqrestore(&udc->lock, flags);
	return ret;
}

/**
 * xudc_stop - stops the device.
 * @gadget: pointer to the usb gadget structure
 *
 * Return: zero always
 */
static int xudc_stop(struct usb_gadget *gadget)
{
	struct xusb_udc *udc = to_udc(gadget);
	unsigned long flags;

	spin_lock_irqsave(&udc->lock, flags);

	udc->gadget.speed = USB_SPEED_UNKNOWN;
	udc->driver = NULL;

	/* Set device address and remote wakeup to 0 */
	udc->write_fn(udc->addr, XUSB_ADDRESS_OFFSET, 0);
	udc->remote_wkp = 0;

	xudc_stop_activity(udc);

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static const struct usb_gadget_ops xusb_udc_ops = {
	.get_frame	= xudc_get_frame,
	.wakeup		= xudc_wakeup,
	.pullup		= xudc_pullup,
	.udc_start	= xudc_start,
	.udc_stop	= xudc_stop,
};

/**
 * xudc_clear_stall_all_ep - clears stall of every endpoint.
 * @udc: pointer to the udc structure.
 */
static void xudc_clear_stall_all_ep(struct xusb_udc *udc)
{
	struct xusb_ep *ep;
	u32 epcfgreg;
	int i;

	for (i = 0; i < XUSB_MAX_ENDPOINTS; i++) {
		ep = &udc->ep[i];
		epcfgreg = udc->read_fn(udc->addr + ep->offset);
		epcfgreg &= ~XUSB_EP_CFG_STALL_MASK;
		udc->write_fn(udc->addr, ep->offset, epcfgreg);
		if (ep->epnumber) {
			/* Reset the toggle bit.*/
			epcfgreg = udc->read_fn(udc->addr + ep->offset);
			epcfgreg &= ~XUSB_EP_CFG_DATA_TOGGLE_MASK;
			udc->write_fn(udc->addr, ep->offset, epcfgreg);
		}
	}
}

/**
 * xudc_startup_handler - The usb device controller interrupt handler.
 * @udc: pointer to the udc structure.
 * @intrstatus: The mask value containing the interrupt sources.
 *
 * This function handles the RESET,SUSPEND,RESUME and DISCONNECT interrupts.
 */
static void xudc_startup_handler(struct xusb_udc *udc, u32 intrstatus)
{
	u32 intrreg;

	if (intrstatus & XUSB_STATUS_RESET_MASK) {

		dev_dbg(udc->dev, "Reset\n");

		if (intrstatus & XUSB_STATUS_HIGH_SPEED_MASK)
			udc->gadget.speed = USB_SPEED_HIGH;
		else
			udc->gadget.speed = USB_SPEED_FULL;

		xudc_stop_activity(udc);
		xudc_clear_stall_all_ep(udc);
		udc->write_fn(udc->addr, XUSB_TESTMODE_OFFSET, 0);

		/* Set device address and remote wakeup to 0 */
		udc->write_fn(udc->addr, XUSB_ADDRESS_OFFSET, 0);
		udc->remote_wkp = 0;

		/* Enable the suspend, resume and disconnect */
		intrreg = udc->read_fn(udc->addr + XUSB_IER_OFFSET);
		intrreg |= XUSB_STATUS_SUSPEND_MASK | XUSB_STATUS_RESUME_MASK |
			   XUSB_STATUS_DISCONNECT_MASK;
		udc->write_fn(udc->addr, XUSB_IER_OFFSET, intrreg);
	}
	if (intrstatus & XUSB_STATUS_SUSPEND_MASK) {

		dev_dbg(udc->dev, "Suspend\n");

		/* Enable the reset, resume and disconnect */
		intrreg = udc->read_fn(udc->addr + XUSB_IER_OFFSET);
		intrreg |= XUSB_STATUS_RESET_MASK | XUSB_STATUS_RESUME_MASK |
			   XUSB_STATUS_DISCONNECT_MASK;
		udc->write_fn(udc->addr, XUSB_IER_OFFSET, intrreg);

		udc->usb_state = USB_STATE_SUSPENDED;

		if (udc->driver->suspend) {
			spin_unlock(&udc->lock);
			udc->driver->suspend(&udc->gadget);
			spin_lock(&udc->lock);
		}
	}
	if (intrstatus & XUSB_STATUS_RESUME_MASK) {
		bool condition = (udc->usb_state != USB_STATE_SUSPENDED);

		dev_WARN_ONCE(udc->dev, condition,
				"Resume IRQ while not suspended\n");

		dev_dbg(udc->dev, "Resume\n");

		/* Enable the reset, suspend and disconnect */
		intrreg = udc->read_fn(udc->addr + XUSB_IER_OFFSET);
		intrreg |= XUSB_STATUS_RESET_MASK | XUSB_STATUS_SUSPEND_MASK |
			   XUSB_STATUS_DISCONNECT_MASK;
		udc->write_fn(udc->addr, XUSB_IER_OFFSET, intrreg);

		udc->usb_state = 0;

		if (udc->driver->resume) {
			spin_unlock(&udc->lock);
			udc->driver->resume(&udc->gadget);
			spin_lock(&udc->lock);
		}
	}
	if (intrstatus & XUSB_STATUS_DISCONNECT_MASK) {

		dev_dbg(udc->dev, "Disconnect\n");

		/* Enable the reset, resume and suspend */
		intrreg = udc->read_fn(udc->addr + XUSB_IER_OFFSET);
		intrreg |= XUSB_STATUS_RESET_MASK | XUSB_STATUS_RESUME_MASK |
			   XUSB_STATUS_SUSPEND_MASK;
		udc->write_fn(udc->addr, XUSB_IER_OFFSET, intrreg);

		if (udc->driver && udc->driver->disconnect) {
			spin_unlock(&udc->lock);
			udc->driver->disconnect(&udc->gadget);
			spin_lock(&udc->lock);
		}
	}
}

/**
 * xudc_ep0_stall - Stall endpoint zero.
 * @udc: pointer to the udc structure.
 *
 * This function stalls endpoint zero.
 */
static void xudc_ep0_stall(struct xusb_udc *udc)
{
	u32 epcfgreg;
	struct xusb_ep *ep0 = &udc->ep[XUSB_EP_NUMBER_ZERO];

	epcfgreg = udc->read_fn(udc->addr + ep0->offset);
	epcfgreg |= XUSB_EP_CFG_STALL_MASK;
	udc->write_fn(udc->addr, ep0->offset, epcfgreg);
}

/**
 * xudc_setaddress - executes SET_ADDRESS command
 * @udc: pointer to the udc structure.
 *
 * This function executes USB SET_ADDRESS command
 */
static void xudc_setaddress(struct xusb_udc *udc)
{
	struct xusb_ep *ep0	= &udc->ep[0];
	struct xusb_req *req	= udc->req;
	int ret;

	req->usb_req.length = 0;
	ret = __xudc_ep0_queue(ep0, req);
	if (ret == 0)
		return;

	dev_err(udc->dev, "Can't respond to SET ADDRESS request\n");
	xudc_ep0_stall(udc);
}

/**
 * xudc_getstatus - executes GET_STATUS command
 * @udc: pointer to the udc structure.
 *
 * This function executes USB GET_STATUS command
 */
static void xudc_getstatus(struct xusb_udc *udc)
{
	struct xusb_ep *ep0	= &udc->ep[0];
	struct xusb_req *req	= udc->req;
	struct xusb_ep *target_ep;
	u16 status = 0;
	u32 epcfgreg;
	int epnum;
	u32 halt;
	int ret;

	switch (udc->setup.bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		/* Get device status */
		status = 1 << USB_DEVICE_SELF_POWERED;
		if (udc->remote_wkp)
			status |= (1 << USB_DEVICE_REMOTE_WAKEUP);
		break;
	case USB_RECIP_INTERFACE:
		break;
	case USB_RECIP_ENDPOINT:
		epnum = udc->setup.wIndex & USB_ENDPOINT_NUMBER_MASK;
		target_ep = &udc->ep[epnum];
		epcfgreg = udc->read_fn(udc->addr + target_ep->offset);
		halt = epcfgreg & XUSB_EP_CFG_STALL_MASK;
		if (udc->setup.wIndex & USB_DIR_IN) {
			if (!target_ep->is_in)
				goto stall;
		} else {
			if (target_ep->is_in)
				goto stall;
		}
		if (halt)
			status = 1 << USB_ENDPOINT_HALT;
		break;
	default:
		goto stall;
	}

	req->usb_req.length = 2;
	*(u16 *)req->usb_req.buf = cpu_to_le16(status);
	ret = __xudc_ep0_queue(ep0, req);
	if (ret == 0)
		return;
stall:
	dev_err(udc->dev, "Can't respond to getstatus request\n");
	xudc_ep0_stall(udc);
}

/**
 * xudc_set_clear_feature - Executes the set feature and clear feature commands.
 * @udc: pointer to the usb device controller structure.
 *
 * Processes the SET_FEATURE and CLEAR_FEATURE commands.
 */
static void xudc_set_clear_feature(struct xusb_udc *udc)
{
	struct xusb_ep *ep0	= &udc->ep[0];
	struct xusb_req *req	= udc->req;
	struct xusb_ep *target_ep;
	u8 endpoint;
	u8 outinbit;
	u32 epcfgreg;
	int flag = (udc->setup.bRequest == USB_REQ_SET_FEATURE ? 1 : 0);
	int ret;

	switch (udc->setup.bRequestType) {
	case USB_RECIP_DEVICE:
		switch (udc->setup.wValue) {
		case USB_DEVICE_TEST_MODE:
			/*
			 * The Test Mode will be executed
			 * after the status phase.
			 */
			break;
		case USB_DEVICE_REMOTE_WAKEUP:
			if (flag)
				udc->remote_wkp = 1;
			else
				udc->remote_wkp = 0;
			break;
		default:
			xudc_ep0_stall(udc);
			break;
		}
		break;
	case USB_RECIP_ENDPOINT:
		if (!udc->setup.wValue) {
			endpoint = udc->setup.wIndex & USB_ENDPOINT_NUMBER_MASK;
			target_ep = &udc->ep[endpoint];
			outinbit = udc->setup.wIndex & USB_ENDPOINT_DIR_MASK;
			outinbit = outinbit >> 7;

			/* Make sure direction matches.*/
			if (outinbit != target_ep->is_in) {
				xudc_ep0_stall(udc);
				return;
			}
			epcfgreg = udc->read_fn(udc->addr + target_ep->offset);
			if (!endpoint) {
				/* Clear the stall.*/
				epcfgreg &= ~XUSB_EP_CFG_STALL_MASK;
				udc->write_fn(udc->addr,
					      target_ep->offset, epcfgreg);
			} else {
				if (flag) {
					epcfgreg |= XUSB_EP_CFG_STALL_MASK;
					udc->write_fn(udc->addr,
						      target_ep->offset,
						      epcfgreg);
				} else {
					/* Unstall the endpoint.*/
					epcfgreg &= ~(XUSB_EP_CFG_STALL_MASK |
						XUSB_EP_CFG_DATA_TOGGLE_MASK);
					udc->write_fn(udc->addr,
						      target_ep->offset,
						      epcfgreg);
				}
			}
		}
		break;
	default:
		xudc_ep0_stall(udc);
		return;
	}

	req->usb_req.length = 0;
	ret = __xudc_ep0_queue(ep0, req);
	if (ret == 0)
		return;

	dev_err(udc->dev, "Can't respond to SET/CLEAR FEATURE\n");
	xudc_ep0_stall(udc);
}

/**
 * xudc_handle_setup - Processes the setup packet.
 * @udc: pointer to the usb device controller structure.
 *
 * Process setup packet and delegate to gadget layer.
 */
static void xudc_handle_setup(struct xusb_udc *udc)
	__must_hold(&udc->lock)
{
	struct xusb_ep *ep0 = &udc->ep[0];
	struct usb_ctrlrequest setup;
	u32 *ep0rambase;

	/* Load up the chapter 9 command buffer.*/
	ep0rambase = (u32 __force *) (udc->addr + XUSB_SETUP_PKT_ADDR_OFFSET);
	memcpy(&setup, ep0rambase, 8);

	udc->setup = setup;
	udc->setup.wValue = cpu_to_le16(setup.wValue);
	udc->setup.wIndex = cpu_to_le16(setup.wIndex);
	udc->setup.wLength = cpu_to_le16(setup.wLength);

	/* Clear previous requests */
	xudc_nuke(ep0, -ECONNRESET);

	if (udc->setup.bRequestType & USB_DIR_IN) {
		/* Execute the get command.*/
		udc->setupseqrx = STATUS_PHASE;
		udc->setupseqtx = DATA_PHASE;
	} else {
		/* Execute the put command.*/
		udc->setupseqrx = DATA_PHASE;
		udc->setupseqtx = STATUS_PHASE;
	}

	switch (udc->setup.bRequest) {
	case USB_REQ_GET_STATUS:
		/* Data+Status phase form udc */
		if ((udc->setup.bRequestType &
				(USB_DIR_IN | USB_TYPE_MASK)) !=
				(USB_DIR_IN | USB_TYPE_STANDARD))
			break;
		xudc_getstatus(udc);
		return;
	case USB_REQ_SET_ADDRESS:
		/* Status phase from udc */
		if (udc->setup.bRequestType != (USB_DIR_OUT |
				USB_TYPE_STANDARD | USB_RECIP_DEVICE))
			break;
		xudc_setaddress(udc);
		return;
	case USB_REQ_CLEAR_FEATURE:
	case USB_REQ_SET_FEATURE:
		/* Requests with no data phase, status phase from udc */
		if ((udc->setup.bRequestType & USB_TYPE_MASK)
				!= USB_TYPE_STANDARD)
			break;
		xudc_set_clear_feature(udc);
		return;
	default:
		break;
	}

	spin_unlock(&udc->lock);
	if (udc->driver->setup(&udc->gadget, &setup) < 0)
		xudc_ep0_stall(udc);
	spin_lock(&udc->lock);
}

/**
 * xudc_ep0_out - Processes the endpoint 0 OUT token.
 * @udc: pointer to the usb device controller structure.
 */
static void xudc_ep0_out(struct xusb_udc *udc)
{
	struct xusb_ep *ep0 = &udc->ep[0];
	struct xusb_req *req;
	u8 *ep0rambase;
	unsigned int bytes_to_rx;
	void *buffer;

	req = list_first_entry(&ep0->queue, struct xusb_req, queue);

	switch (udc->setupseqrx) {
	case STATUS_PHASE:
		/*
		 * This resets both state machines for the next
		 * Setup packet.
		 */
		udc->setupseqrx = SETUP_PHASE;
		udc->setupseqtx = SETUP_PHASE;
		req->usb_req.actual = req->usb_req.length;
		xudc_done(ep0, req, 0);
		break;
	case DATA_PHASE:
		bytes_to_rx = udc->read_fn(udc->addr +
					   XUSB_EP_BUF0COUNT_OFFSET);
		/* Copy the data to be received from the DPRAM. */
		ep0rambase = (u8 __force *) (udc->addr +
			     (ep0->rambase << 2));
		buffer = req->usb_req.buf + req->usb_req.actual;
		req->usb_req.actual = req->usb_req.actual + bytes_to_rx;
		memcpy(buffer, ep0rambase, bytes_to_rx);

		if (req->usb_req.length == req->usb_req.actual) {
			/* Data transfer completed get ready for Status stage */
			xudc_wrstatus(udc);
		} else {
			/* Enable EP0 buffer to receive data */
			udc->write_fn(udc->addr, XUSB_EP_BUF0COUNT_OFFSET, 0);
			udc->write_fn(udc->addr, XUSB_BUFFREADY_OFFSET, 1);
		}
		break;
	default:
		break;
	}
}

/**
 * xudc_ep0_in - Processes the endpoint 0 IN token.
 * @udc: pointer to the usb device controller structure.
 */
static void xudc_ep0_in(struct xusb_udc *udc)
{
	struct xusb_ep *ep0 = &udc->ep[0];
	struct xusb_req *req;
	unsigned int bytes_to_tx;
	void *buffer;
	u32 epcfgreg;
	u16 count = 0;
	u16 length;
	u8 *ep0rambase;
	u8 test_mode = udc->setup.wIndex >> 8;

	req = list_first_entry(&ep0->queue, struct xusb_req, queue);
	bytes_to_tx = req->usb_req.length - req->usb_req.actual;

	switch (udc->setupseqtx) {
	case STATUS_PHASE:
		switch (udc->setup.bRequest) {
		case USB_REQ_SET_ADDRESS:
			/* Set the address of the device.*/
			udc->write_fn(udc->addr, XUSB_ADDRESS_OFFSET,
				      udc->setup.wValue);
			break;
		case USB_REQ_SET_FEATURE:
			if (udc->setup.bRequestType ==
					USB_RECIP_DEVICE) {
				if (udc->setup.wValue ==
						USB_DEVICE_TEST_MODE)
					udc->write_fn(udc->addr,
						      XUSB_TESTMODE_OFFSET,
						      test_mode);
			}
			break;
		}
		req->usb_req.actual = req->usb_req.length;
		xudc_done(ep0, req, 0);
		break;
	case DATA_PHASE:
		if (!bytes_to_tx) {
			/*
			 * We're done with data transfer, next
			 * will be zero length OUT with data toggle of
			 * 1. Setup data_toggle.
			 */
			epcfgreg = udc->read_fn(udc->addr + ep0->offset);
			epcfgreg |= XUSB_EP_CFG_DATA_TOGGLE_MASK;
			udc->write_fn(udc->addr, ep0->offset, epcfgreg);
			udc->setupseqtx = STATUS_PHASE;
		} else {
			length = count = min_t(u32, bytes_to_tx,
					       EP0_MAX_PACKET);
			/* Copy the data to be transmitted into the DPRAM. */
			ep0rambase = (u8 __force *) (udc->addr +
				     (ep0->rambase << 2));
			buffer = req->usb_req.buf + req->usb_req.actual;
			req->usb_req.actual = req->usb_req.actual + length;
			memcpy(ep0rambase, buffer, length);
		}
		udc->write_fn(udc->addr, XUSB_EP_BUF0COUNT_OFFSET, count);
		udc->write_fn(udc->addr, XUSB_BUFFREADY_OFFSET, 1);
		break;
	default:
		break;
	}
}

/**
 * xudc_ctrl_ep_handler - Endpoint 0 interrupt handler.
 * @udc: pointer to the udc structure.
 * @intrstatus:	It's the mask value for the interrupt sources on endpoint 0.
 *
 * Processes the commands received during enumeration phase.
 */
static void xudc_ctrl_ep_handler(struct xusb_udc *udc, u32 intrstatus)
{

	if (intrstatus & XUSB_STATUS_SETUP_PACKET_MASK) {
		xudc_handle_setup(udc);
	} else {
		if (intrstatus & XUSB_STATUS_FIFO_BUFF_RDY_MASK)
			xudc_ep0_out(udc);
		else if (intrstatus & XUSB_STATUS_FIFO_BUFF_FREE_MASK)
			xudc_ep0_in(udc);
	}
}

/**
 * xudc_nonctrl_ep_handler - Non control endpoint interrupt handler.
 * @udc: pointer to the udc structure.
 * @epnum: End point number for which the interrupt is to be processed
 * @intrstatus:	mask value for interrupt sources of endpoints other
 *		than endpoint 0.
 *
 * Processes the buffer completion interrupts.
 */
static void xudc_nonctrl_ep_handler(struct xusb_udc *udc, u8 epnum,
				    u32 intrstatus)
{

	struct xusb_req *req;
	struct xusb_ep *ep;

	ep = &udc->ep[epnum];
	/* Process the End point interrupts.*/
	if (intrstatus & (XUSB_STATUS_EP0_BUFF1_COMP_MASK << epnum))
		ep->buffer0ready = 0;
	if (intrstatus & (XUSB_STATUS_EP0_BUFF2_COMP_MASK << epnum))
		ep->buffer1ready = false;

	if (list_empty(&ep->queue))
		return;

	req = list_first_entry(&ep->queue, struct xusb_req, queue);

	if (ep->is_in)
		xudc_write_fifo(ep, req);
	else
		xudc_read_fifo(ep, req);
}

/**
 * xudc_irq - The main interrupt handler.
 * @irq: The interrupt number.
 * @_udc: pointer to the usb device controller structure.
 *
 * Return: IRQ_HANDLED after the interrupt is handled.
 */
static irqreturn_t xudc_irq(int irq, void *_udc)
{
	struct xusb_udc *udc = _udc;
	u32 intrstatus;
	u32 ier;
	u8 index;
	u32 bufintr;
	unsigned long flags;

	spin_lock_irqsave(&udc->lock, flags);

	/*
	 * Event interrupts are level sensitive hence first disable
	 * IER, read ISR and figure out active interrupts.
	 */
	ier = udc->read_fn(udc->addr + XUSB_IER_OFFSET);
	ier &= ~XUSB_STATUS_INTR_EVENT_MASK;
	udc->write_fn(udc->addr, XUSB_IER_OFFSET, ier);

	/* Read the Interrupt Status Register.*/
	intrstatus = udc->read_fn(udc->addr + XUSB_STATUS_OFFSET);

	/* Call the handler for the event interrupt.*/
	if (intrstatus & XUSB_STATUS_INTR_EVENT_MASK) {
		/*
		 * Check if there is any action to be done for :
		 * - USB Reset received {XUSB_STATUS_RESET_MASK}
		 * - USB Suspend received {XUSB_STATUS_SUSPEND_MASK}
		 * - USB Resume received {XUSB_STATUS_RESUME_MASK}
		 * - USB Disconnect received {XUSB_STATUS_DISCONNECT_MASK}
		 */
		xudc_startup_handler(udc, intrstatus);
	}

	/* Check the buffer completion interrupts */
	if (intrstatus & XUSB_STATUS_INTR_BUFF_COMP_ALL_MASK) {
		/* Enable Reset, Suspend, Resume and Disconnect  */
		ier = udc->read_fn(udc->addr + XUSB_IER_OFFSET);
		ier |= XUSB_STATUS_INTR_EVENT_MASK;
		udc->write_fn(udc->addr, XUSB_IER_OFFSET, ier);

		if (intrstatus & XUSB_STATUS_EP0_BUFF1_COMP_MASK)
			xudc_ctrl_ep_handler(udc, intrstatus);

		for (index = 1; index < 8; index++) {
			bufintr = ((intrstatus &
				  (XUSB_STATUS_EP1_BUFF1_COMP_MASK <<
				  (index - 1))) || (intrstatus &
				  (XUSB_STATUS_EP1_BUFF2_COMP_MASK <<
				  (index - 1))));
			if (bufintr) {
				xudc_nonctrl_ep_handler(udc, index,
							intrstatus);
			}
		}
	}

	spin_unlock_irqrestore(&udc->lock, flags);
	return IRQ_HANDLED;
}

/**
 * xudc_probe - The device probe function for driver initialization.
 * @pdev: pointer to the platform device structure.
 *
 * Return: 0 for success and error value on failure
 */
static int xudc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	struct xusb_udc *udc;
	int irq;
	int ret;
	u32 ier;
	u8 *buff;

	udc = devm_kzalloc(&pdev->dev, sizeof(*udc), GFP_KERNEL);
	if (!udc)
		return -ENOMEM;

	/* Create a dummy request for GET_STATUS, SET_ADDRESS */
	udc->req = devm_kzalloc(&pdev->dev, sizeof(struct xusb_req),
				GFP_KERNEL);
	if (!udc->req)
		return -ENOMEM;

	buff = devm_kzalloc(&pdev->dev, STATUSBUFF_SIZE, GFP_KERNEL);
	if (!buff)
		return -ENOMEM;

	udc->req->usb_req.buf = buff;

	/* Map the registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	udc->addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(udc->addr))
		return PTR_ERR(udc->addr);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;
	ret = devm_request_irq(&pdev->dev, irq, xudc_irq, 0,
			       dev_name(&pdev->dev), udc);
	if (ret < 0) {
		dev_dbg(&pdev->dev, "unable to request irq %d", irq);
		goto fail;
	}

	udc->dma_enabled = of_property_read_bool(np, "xlnx,has-builtin-dma");

	/* Setup gadget structure */
	udc->gadget.ops = &xusb_udc_ops;
	udc->gadget.max_speed = USB_SPEED_HIGH;
	udc->gadget.speed = USB_SPEED_UNKNOWN;
	udc->gadget.ep0 = &udc->ep[XUSB_EP_NUMBER_ZERO].ep_usb;
	udc->gadget.name = driver_name;

	udc->clk = devm_clk_get(&pdev->dev, "s_axi_aclk");
	if (IS_ERR(udc->clk)) {
		if (PTR_ERR(udc->clk) != -ENOENT) {
			ret = PTR_ERR(udc->clk);
			goto fail;
		}

		/*
		 * Clock framework support is optional, continue on,
		 * anyways if we don't find a matching clock
		 */
		dev_warn(&pdev->dev, "s_axi_aclk clock property is not found\n");
		udc->clk = NULL;
	}

	ret = clk_prepare_enable(udc->clk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable clock.\n");
		return ret;
	}

	spin_lock_init(&udc->lock);

	/* Check for IP endianness */
	udc->write_fn = xudc_write32_be;
	udc->read_fn = xudc_read32_be;
	udc->write_fn(udc->addr, XUSB_TESTMODE_OFFSET, USB_TEST_J);
	if ((udc->read_fn(udc->addr + XUSB_TESTMODE_OFFSET))
			!= USB_TEST_J) {
		udc->write_fn = xudc_write32;
		udc->read_fn = xudc_read32;
	}
	udc->write_fn(udc->addr, XUSB_TESTMODE_OFFSET, 0);

	xudc_eps_init(udc);

	/* Set device address to 0.*/
	udc->write_fn(udc->addr, XUSB_ADDRESS_OFFSET, 0);

	ret = usb_add_gadget_udc(&pdev->dev, &udc->gadget);
	if (ret)
		goto err_disable_unprepare_clk;

	udc->dev = &udc->gadget.dev;

	/* Enable the interrupts.*/
	ier = XUSB_STATUS_GLOBAL_INTR_MASK | XUSB_STATUS_INTR_EVENT_MASK |
	      XUSB_STATUS_FIFO_BUFF_RDY_MASK | XUSB_STATUS_FIFO_BUFF_FREE_MASK |
	      XUSB_STATUS_SETUP_PACKET_MASK |
	      XUSB_STATUS_INTR_BUFF_COMP_ALL_MASK;

	udc->write_fn(udc->addr, XUSB_IER_OFFSET, ier);

	platform_set_drvdata(pdev, udc);

	dev_vdbg(&pdev->dev, "%s at 0x%08X mapped to %p %s\n",
		 driver_name, (u32)res->start, udc->addr,
		 udc->dma_enabled ? "with DMA" : "without DMA");

	return 0;

err_disable_unprepare_clk:
	clk_disable_unprepare(udc->clk);
fail:
	dev_err(&pdev->dev, "probe failed, %d\n", ret);
	return ret;
}

/**
 * xudc_remove - Releases the resources allocated during the initialization.
 * @pdev: pointer to the platform device structure.
 *
 * Return: 0 always
 */
static int xudc_remove(struct platform_device *pdev)
{
	struct xusb_udc *udc = platform_get_drvdata(pdev);

	usb_del_gadget_udc(&udc->gadget);
	clk_disable_unprepare(udc->clk);

	return 0;
}

/* Match table for of_platform binding */
static const struct of_device_id usb_of_match[] = {
	{ .compatible = "xlnx,usb2-device-4.00.a", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, usb_of_match);

static struct platform_driver xudc_driver = {
	.driver = {
		.name = driver_name,
		.of_match_table = usb_of_match,
	},
	.probe = xudc_probe,
	.remove = xudc_remove,
};

module_platform_driver(xudc_driver);

MODULE_DESCRIPTION("Xilinx udc driver");
MODULE_AUTHOR("Xilinx, Inc");
MODULE_LICENSE("GPL");
