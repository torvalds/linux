 /* ==========================================================================
  * $File: //dwh/usb_iip/dev/software/otg/linux/drivers/dwc_otg_pcd_linux.c $
  * $Revision: #26 $
  * $Date: 2012/12/13 $
  * $Change: 2125485 $
  *
  * Synopsys HS OTG Linux Software Driver and documentation (hereinafter,
  * "Software") is an Unsupported proprietary work of Synopsys, Inc. unless
  * otherwise expressly agreed to in writing between Synopsys and you.
  *
  * The Software IS NOT an item of Licensed Software or Licensed Product under
  * any End User Software License Agreement or Agreement for Licensed Product
  * with Synopsys or any supplement thereto. You are permitted to use and
  * redistribute this Software in source and binary forms, with or without
  * modification, provided that redistributions of source code must retain this
  * notice. You may not view, use, disclose, copy or distribute this file or
  * any information contained herein except pursuant to this license grant from
  * Synopsys. If you do not agree with this notice, including the disclaimer
  * below, then you are not authorized to use the Software.
  *
  * THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS" BASIS
  * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  * ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS BE LIABLE FOR ANY DIRECT,
  * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
  * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
  * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
  * DAMAGE.
  * ========================================================================== */
#ifndef DWC_HOST_ONLY

/** @file
 * This file implements the Peripheral Controller Driver.
 *
 * The Peripheral Controller Driver (PCD) is responsible for
 * translating requests from the Function Driver into the appropriate
 * actions on the DWC_otg controller. It isolates the Function Driver
 * from the specifics of the controller by providing an API to the
 * Function Driver.
 *
 * The Peripheral Controller Driver for Linux will implement the
 * Gadget API, so that the existing Gadget drivers can be used.
 * (Gadget Driver is the Linux terminology for a Function Driver.)
 *
 * The Linux Gadget API is defined in the header file
 * <code><linux/usb_gadget.h></code>.  The USB EP operations API is
 * defined in the structure <code>usb_ep_ops</code> and the USB
 * Controller API is defined in the structure
 * <code>usb_gadget_ops</code>.
 *
 */

#include "dwc_otg_os_dep.h"
#include "dwc_otg_pcd_if.h"
#include "dwc_otg_pcd.h"
#include "dwc_otg_driver.h"
#include "dwc_otg_dbg.h"
#include "dwc_otg_attr.h"

#include "usbdev_rk.h"
static struct gadget_wrapper {
	dwc_otg_pcd_t *pcd;

	struct usb_gadget gadget;
	struct usb_gadget_driver *driver;

	struct usb_ep ep0;
	struct usb_ep in_ep[16];
	struct usb_ep out_ep[16];

} *gadget_wrapper;

/* Display the contents of the buffer */
extern void dump_msg(const u8 *buf, unsigned int length);

/**
 * Get the dwc_otg_pcd_ep_t* from usb_ep* pointer - NULL in case
 * if the endpoint is not found
 */
static struct dwc_otg_pcd_ep *ep_from_handle(dwc_otg_pcd_t *pcd, void *handle)
{
	int i;
	if (pcd->ep0.priv == handle) {
		return &pcd->ep0;
	}

	for (i = 0; i < MAX_EPS_CHANNELS - 1; i++) {
		if (pcd->in_ep[i].priv == handle)
			return &pcd->in_ep[i];
		if (pcd->out_ep[i].priv == handle)
			return &pcd->out_ep[i];
	}

	return NULL;
}

/* USB Endpoint Operations */
/*
 * The following sections briefly describe the behavior of the Gadget
 * API endpoint operations implemented in the DWC_otg driver
 * software. Detailed descriptions of the generic behavior of each of
 * these functions can be found in the Linux header file
 * include/linux/usb_gadget.h.
 *
 * The Gadget API provides wrapper functions for each of the function
 * pointers defined in usb_ep_ops. The Gadget Driver calls the wrapper
 * function, which then calls the underlying PCD function. The
 * following sections are named according to the wrapper
 * functions. Within each section, the corresponding DWC_otg PCD
 * function name is specified.
 *
 */

/**
 * This function is called by the Gadget Driver for each EP to be
 * configured for the current configuration (SET_CONFIGURATION).
 *
 * This function initializes the dwc_otg_ep_t data structure, and then
 * calls dwc_otg_ep_activate.
 */
static int ep_enable(struct usb_ep *usb_ep,
		     const struct usb_endpoint_descriptor *ep_desc)
{
	int retval;

	DWC_DEBUGPL(DBG_PCDV, "%s(%p,%p)\n", __func__, usb_ep, ep_desc);

	if (!usb_ep || !ep_desc || ep_desc->bDescriptorType != USB_DT_ENDPOINT) {
		DWC_WARN("%s, bad ep or descriptor\n", __func__);
		return -EINVAL;
	}
	if (usb_ep == &gadget_wrapper->ep0) {
		DWC_WARN("%s, bad ep(0)\n", __func__);
		return -EINVAL;
	}

	/* Check FIFO size? */
	if (!ep_desc->wMaxPacketSize) {
		DWC_WARN("%s, bad %s maxpacket\n", __func__, usb_ep->name);
		return -ERANGE;
	}

	if (!gadget_wrapper->driver ||
	    gadget_wrapper->gadget.speed == USB_SPEED_UNKNOWN) {
		DWC_WARN("%s, bogus device state\n", __func__);
		return -ESHUTDOWN;
	}

	/* Delete after check - MAS */
#if 0
	nat = (uint32_t) ep_desc->wMaxPacketSize;
	printk(KERN_ALERT "%s: nat (before) =%d\n", __func__, nat);
	nat = (nat >> 11) & 0x03;
	printk(KERN_ALERT "%s: nat (after) =%d\n", __func__, nat);
#endif
	retval = dwc_otg_pcd_ep_enable(gadget_wrapper->pcd,
				       (const uint8_t *)ep_desc,
				       (void *)usb_ep);
	if (retval) {
		DWC_WARN("dwc_otg_pcd_ep_enable failed\n");
		return -EINVAL;
	}

	usb_ep->maxpacket = le16_to_cpu(ep_desc->wMaxPacketSize);

	return 0;
}

/**
 * This function is called when an EP is disabled due to disconnect or
 * change in configuration. Any pending requests will terminate with a
 * status of -ESHUTDOWN.
 *
 * This function modifies the dwc_otg_ep_t data structure for this EP,
 * and then calls dwc_otg_ep_deactivate.
 */
static int ep_disable(struct usb_ep *usb_ep)
{
	int retval;

	DWC_DEBUGPL(DBG_PCDV, "%s(%p)\n", __func__, usb_ep);
	if (!usb_ep) {
		DWC_DEBUGPL(DBG_PCD, "%s, %s not enabled\n", __func__,
			    usb_ep ? usb_ep->name : NULL);
		return -EINVAL;
	}

	retval = dwc_otg_pcd_ep_disable(gadget_wrapper->pcd, usb_ep);
	if (retval) {
		retval = -EINVAL;
	}

	return retval;
}

/**
 * This function allocates a request object to use with the specified
 * endpoint.
 *
 * @param ep The endpoint to be used with with the request
 * @param gfp_flags the GFP_* flags to use.
 */
static struct usb_request *dwc_otg_pcd_alloc_request(struct usb_ep *ep,
						     gfp_t gfp_flags)
{
	struct usb_request *usb_req;

	DWC_DEBUGPL(DBG_PCDV, "%s(%p,%d)\n", __func__, ep, gfp_flags);
	if (0 == ep) {
		DWC_WARN("%s() %s\n", __func__, "Invalid EP!\n");
		return 0;
	}
	usb_req = kmalloc(sizeof(*usb_req), gfp_flags);
	if (0 == usb_req) {
		DWC_WARN("%s() %s\n", __func__, "request allocation failed!\n");
		return 0;
	}
	memset(usb_req, 0, sizeof(*usb_req));
	usb_req->dma = DWC_DMA_ADDR_INVALID;

	return usb_req;
}

/**
 * This function frees a request object.
 *
 * @param ep The endpoint associated with the request
 * @param req The request being freed
 */
static void dwc_otg_pcd_free_request(struct usb_ep *ep, struct usb_request *req)
{
	DWC_DEBUGPL(DBG_PCDV, "%s(%p,%p)\n", __func__, ep, req);

	if (0 == ep || 0 == req) {
		DWC_WARN("%s() %s\n", __func__,
			 "Invalid ep or req argument!\n");
		return;
	}

	kfree(req);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28)
/**
 * This function allocates an I/O buffer to be used for a transfer
 * to/from the specified endpoint.
 *
 * @param usb_ep The endpoint to be used with with the request
 * @param bytes The desired number of bytes for the buffer
 * @param dma Pointer to the buffer's DMA address; must be valid
 * @param gfp_flags the GFP_* flags to use.
 * @return address of a new buffer or null is buffer could not be allocated.
 */
static void *dwc_otg_pcd_alloc_buffer(struct usb_ep *usb_ep, unsigned bytes,
				      dma_addr_t *dma, gfp_t gfp_flags)
{
	void *buf;
	dwc_otg_pcd_t *pcd = 0;

	pcd = gadget_wrapper->pcd;

	DWC_DEBUGPL(DBG_PCDV, "%s(%p,%d,%p,%0x)\n", __func__, usb_ep, bytes,
		    dma, gfp_flags);

	/* Check dword alignment */
	if ((bytes & 0x3UL) != 0) {
		DWC_WARN("%s() Buffer size is not a multiple of"
			 "DWORD size (%d)", __func__, bytes);
	}

	buf = dma_alloc_coherent(NULL, bytes, dma, gfp_flags);

	/* Check dword alignment */
	if (((int)buf & 0x3UL) != 0) {
		DWC_WARN("%s() Buffer is not DWORD aligned (%p)",
			 __func__, buf);
	}

	return buf;
}

/**
 * This function frees an I/O buffer that was allocated by alloc_buffer.
 *
 * @param usb_ep the endpoint associated with the buffer
 * @param buf address of the buffer
 * @param dma The buffer's DMA address
 * @param bytes The number of bytes of the buffer
 */
static void dwc_otg_pcd_free_buffer(struct usb_ep *usb_ep, void *buf,
				    dma_addr_t dma, unsigned bytes)
{
	dwc_otg_pcd_t *pcd = 0;

	pcd = gadget_wrapper->pcd;

	DWC_DEBUGPL(DBG_PCDV, "%s(%p,%0x,%d)\n", __func__, buf, dma, bytes);

	dma_free_coherent(NULL, bytes, buf, dma);
}
#endif

/**
 * This function is used to submit an I/O Request to an EP.
 *
 *	- When the request completes the request's completion callback
 *	  is called to return the request to the driver.
 *	- An EP, except control EPs, may have multiple requests
 *	  pending.
 *	- Once submitted the request cannot be examined or modified.
 *	- Each request is turned into one or more packets.
 *	- A BULK EP can queue any amount of data; the transfer is
 *	  packetized.
 *	- Zero length Packets are specified with the request 'zero'
 *	  flag.
 */
static int ep_queue(struct usb_ep *usb_ep, struct usb_request *usb_req,
		    gfp_t gfp_flags)
{
	dwc_otg_pcd_t *pcd;
	struct dwc_otg_pcd_ep *ep = NULL;
	int retval = 0, is_isoc_ep = 0;
	dma_addr_t dma_addr = DWC_DMA_ADDR_INVALID;

	DWC_DEBUGPL(DBG_PCDV, "%s(%p,%p,%d)\n",
		    __func__, usb_ep, usb_req, gfp_flags);

	if (!usb_req || !usb_req->complete || !usb_req->buf) {
		DWC_WARN("bad params\n");
		return -EINVAL;
	}

	if (!usb_ep) {
		DWC_WARN("bad ep\n");
		return -EINVAL;
	}

	pcd = gadget_wrapper->pcd;
	if (!gadget_wrapper->driver ||
	    gadget_wrapper->gadget.speed == USB_SPEED_UNKNOWN) {
		DWC_DEBUGPL(DBG_PCDV, "gadget.speed=%d\n",
			    gadget_wrapper->gadget.speed);
		DWC_WARN("bogus device state\n");
		return -ESHUTDOWN;
	}

	DWC_DEBUGPL(DBG_PCD, "%s queue req %p, len %d buf %p\n",
		    usb_ep->name, usb_req, usb_req->length, usb_req->buf);

	usb_req->status = -EINPROGRESS;
	usb_req->actual = 0;

	ep = ep_from_handle(pcd, usb_ep);
	if (ep == NULL)
		is_isoc_ep = 0;
	else
		is_isoc_ep = (ep->dwc_ep.type == DWC_OTG_EP_TYPE_ISOC) ? 1 : 0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28)
	dma_addr = usb_req->dma;
#else
	if (GET_CORE_IF(pcd)->dma_enable) {
		/* if (usb_req->length != 0) {*/
		/* In device DMA mode when gadget perform ep_queue request
		 * with buffer length 0, Kernel stack dump occurred. For 0
		 * length buffers perform dma_map_single() with length 4.*/
		if (usb_req->dma == DWC_DMA_ADDR_INVALID) {
			dma_addr =
			    dma_map_single(gadget_wrapper->gadget.dev.parent,
					   usb_req->buf,
					   usb_req->length !=
					   0 ? usb_req->length : 4,
					   ep->dwc_ep.
					   is_in ? DMA_TO_DEVICE :
					   DMA_FROM_DEVICE);
			usb_req->dma = dma_addr;
		} else {
			dma_addr = usb_req->dma;
		}

	}
#endif

#ifdef DWC_UTE_PER_IO
	if (is_isoc_ep == 1) {
		retval =
		    dwc_otg_pcd_xiso_ep_queue(pcd, usb_ep, usb_req->buf,
					      dma_addr, usb_req->length,
					      usb_req->zero, usb_req,
					      gfp_flags == GFP_ATOMIC ? 1 : 0,
					      &usb_req->ext_req);
		if (retval)
			return -EINVAL;

		return 0;
	}
#endif
	retval = dwc_otg_pcd_ep_queue(pcd, usb_ep, usb_req->buf, dma_addr,
				      usb_req->length, usb_req->zero, usb_req,
				      gfp_flags == GFP_ATOMIC ? 1 : 0);
	if (retval) {
		return -EINVAL;
	}

	return 0;
}

/**
 * This function cancels an I/O request from an EP.
 */
static int ep_dequeue(struct usb_ep *usb_ep, struct usb_request *usb_req)
{
	DWC_DEBUGPL(DBG_PCDV, "%s(%p,%p)\n", __func__, usb_ep, usb_req);

	if (!usb_ep || !usb_req) {
		DWC_WARN("bad argument\n");
		return -EINVAL;
	}
	if (!gadget_wrapper->driver ||
	    gadget_wrapper->gadget.speed == USB_SPEED_UNKNOWN) {
		DWC_WARN("bogus device state\n");
		return -ESHUTDOWN;
	}
	if (dwc_otg_pcd_ep_dequeue(gadget_wrapper->pcd, usb_ep, usb_req)) {
		return -EINVAL;
	}

	return 0;
}

/**
 * usb_ep_set_halt stalls an endpoint.
 *
 * usb_ep_clear_halt clears an endpoint halt and resets its data
 * toggle.
 *
 * Both of these functions are implemented with the same underlying
 * function. The behavior depends on the value argument.
 *
 * @param[in] usb_ep the Endpoint to halt or clear halt.
 * @param[in] value
 *	- 0 means clear_halt.
 *	- 1 means set_halt,
 *	- 2 means clear stall lock flag.
 *	- 3 means set  stall lock flag.
 */
static int ep_halt(struct usb_ep *usb_ep, int value)
{
	int retval = 0;

	DWC_DEBUGPL(DBG_PCD, "HALT %s %d\n", usb_ep->name, value);

	if (!usb_ep) {
		DWC_WARN("bad ep\n");
		return -EINVAL;
	}

	retval = dwc_otg_pcd_ep_halt(gadget_wrapper->pcd, usb_ep, value);
	if (retval == -DWC_E_AGAIN) {
		return -EAGAIN;
	} else if (retval) {
		retval = -EINVAL;
	}

	return retval;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 26)
static int ep_wedge(struct usb_ep *usb_ep)
{
	DWC_DEBUGPL(DBG_PCD, "WEDGE %s\n", usb_ep->name);

	return ep_halt(usb_ep, 3);
}
#endif

#ifdef DWC_EN_ISOC
/**
 * This function is used to submit an ISOC Transfer Request to an EP.
 *
 *	- Every time a sync period completes the request's completion callback
 *	  is called to provide data to the gadget driver.
 *	- Once submitted the request cannot be modified.
 *	- Each request is turned into periodic data packets untill ISO
 *	  Transfer is stopped..
 */
static int iso_ep_start(struct usb_ep *usb_ep, struct usb_iso_request *req,
			gfp_t gfp_flags)
{
	int retval = 0;

	if (!req || !req->process_buffer || !req->buf0 || !req->buf1) {
		DWC_WARN("bad params\n");
		return -EINVAL;
	}

	if (!usb_ep) {
		DWC_PRINTF("bad params\n");
		return -EINVAL;
	}

	req->status = -EINPROGRESS;

	retval =
	    dwc_otg_pcd_iso_ep_start(gadget_wrapper->pcd, usb_ep, req->buf0,
				     req->buf1, req->dma0, req->dma1,
				     req->sync_frame, req->data_pattern_frame,
				     req->data_per_frame,
				     req->flags & USB_REQ_ISO_ASAP ? -1 :
				     req->start_frame, req->buf_proc_intrvl,
				     req, gfp_flags == GFP_ATOMIC ? 1 : 0);

	if (retval) {
		return -EINVAL;
	}

	return retval;
}

/**
 * This function stops ISO EP Periodic Data Transfer.
 */
static int iso_ep_stop(struct usb_ep *usb_ep, struct usb_iso_request *req)
{
	int retval = 0;
	if (!usb_ep) {
		DWC_WARN("bad ep\n");
	}

	if (!gadget_wrapper->driver ||
	    gadget_wrapper->gadget.speed == USB_SPEED_UNKNOWN) {
		DWC_DEBUGPL(DBG_PCDV, "gadget.speed=%d\n",
			    gadget_wrapper->gadget.speed);
		DWC_WARN("bogus device state\n");
	}

	dwc_otg_pcd_iso_ep_stop(gadget_wrapper->pcd, usb_ep, req);
	if (retval) {
		retval = -EINVAL;
	}

	return retval;
}

static struct usb_iso_request *alloc_iso_request(struct usb_ep *ep,
						 int packets, gfp_t gfp_flags)
{
	struct usb_iso_request *pReq = NULL;
	uint32_t req_size;

	req_size = sizeof(struct usb_iso_request);
	req_size +=
	    (2 * packets * (sizeof(struct usb_gadget_iso_packet_descriptor)));

	pReq = kmalloc(req_size, gfp_flags);
	if (!pReq) {
		DWC_WARN("Can't allocate Iso Request\n");
		return 0;
	}
	pReq->iso_packet_desc0 = (void *)(pReq + 1);

	pReq->iso_packet_desc1 = pReq->iso_packet_desc0 + packets;

	return pReq;
}

static void free_iso_request(struct usb_ep *ep, struct usb_iso_request *req)
{
	kfree(req);
}

static struct usb_isoc_ep_ops dwc_otg_pcd_ep_ops = {
	.ep_ops = {
		   .enable = ep_enable,
		   .disable = ep_disable,

		   .alloc_request = dwc_otg_pcd_alloc_request,
		   .free_request = dwc_otg_pcd_free_request,

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28)
		   .alloc_buffer = dwc_otg_pcd_alloc_buffer,
		   .free_buffer = dwc_otg_pcd_free_buffer,
#endif

		   .queue = ep_queue,
		   .dequeue = ep_dequeue,

		   .set_halt = ep_halt,

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 26)
		   .set_wedge = ep_wedge,
#endif
		   .fifo_status = 0,
		   .fifo_flush = 0,
		   },

	.iso_ep_start = iso_ep_start,
	.iso_ep_stop = iso_ep_stop,
	.alloc_iso_request = alloc_iso_request,
	.free_iso_request = free_iso_request,
};

#else

static struct usb_ep_ops dwc_otg_pcd_ep_ops = {
	.enable = ep_enable,
	.disable = ep_disable,

	.alloc_request = dwc_otg_pcd_alloc_request,
	.free_request = dwc_otg_pcd_free_request,

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28)
	.alloc_buffer = dwc_otg_pcd_alloc_buffer,
	.free_buffer = dwc_otg_pcd_free_buffer,
#endif

	.queue = ep_queue,
	.dequeue = ep_dequeue,

	.set_halt = ep_halt,

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 26)
	.set_wedge = ep_wedge,
#endif

	.fifo_status = 0,
	.fifo_flush = 0,

};

#endif /* _EN_ISOC_ */
/*	Gadget Operations */
/**
 * The following gadget operations will be implemented in the DWC_otg
 * PCD. Functions in the API that are not described below are not
 * implemented.
 *
 * The Gadget API provides wrapper functions for each of the function
 * pointers defined in usb_gadget_ops. The Gadget Driver calls the
 * wrapper function, which then calls the underlying PCD function. The
 * following sections are named according to the wrapper functions
 * (except for ioctl, which doesn't have a wrapper function). Within
 * each section, the corresponding DWC_otg PCD function name is
 * specified.
 *
 */

/**
 *Gets the USB Frame number of the last SOF.
 */
static int get_frame_number(struct usb_gadget *gadget)
{
	struct gadget_wrapper *d;

	DWC_DEBUGPL(DBG_PCDV, "%s(%p)\n", __func__, gadget);

	if (gadget == 0) {
		return -ENODEV;
	}

	d = container_of(gadget, struct gadget_wrapper, gadget);
	return dwc_otg_pcd_get_frame_number(d->pcd);
}

#ifdef CONFIG_USB_DWC_OTG_LPM
static int test_lpm_enabled(struct usb_gadget *gadget)
{
	struct gadget_wrapper *d;

	d = container_of(gadget, struct gadget_wrapper, gadget);

	return dwc_otg_pcd_is_lpm_enabled(d->pcd);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
static int test_besl_enabled(struct usb_gadget *gadget)
{
	struct gadget_wrapper *d;

	d = container_of(gadget, struct gadget_wrapper, gadget);

	return dwc_otg_pcd_is_besl_enabled(d->pcd);
}

static int get_param_baseline_besl(struct usb_gadget *gadget)
{
	struct gadget_wrapper *d;

	d = container_of(gadget, struct gadget_wrapper, gadget);

	return dwc_otg_pcd_get_param_baseline_besl(d->pcd);
}

static int get_param_deep_besl(struct usb_gadget *gadget)
{
	struct gadget_wrapper *d;

	d = container_of(gadget, struct gadget_wrapper, gadget);

	return dwc_otg_pcd_get_param_deep_besl(d->pcd);
}
#endif
#endif

/**
 * Initiates Session Request Protocol (SRP) to wakeup the host if no
 * session is in progress. If a session is already in progress, but
 * the device is suspended, remote wakeup signaling is started.
 *
 */
static int wakeup(struct usb_gadget *gadget)
{
	struct gadget_wrapper *d;

	DWC_DEBUGPL(DBG_PCDV, "%s(%p)\n", __func__, gadget);

	if (gadget == 0) {
		return -ENODEV;
	} else {
		d = container_of(gadget, struct gadget_wrapper, gadget);
	}
	dwc_otg_pcd_wakeup(d->pcd);
	return 0;
}

static int dwc_otg_pcd_pullup(struct usb_gadget *_gadget, int is_on)
{
	struct gadget_wrapper *d;
	dwc_otg_pcd_t *pcd;
	dwc_otg_core_if_t *core_if;

	printk("pcd_pullup, is_on %d\n", is_on);
	if (_gadget == NULL)
		return -ENODEV;
	else {
		d = container_of(_gadget, struct gadget_wrapper, gadget);
		pcd = d->pcd;
		core_if = GET_CORE_IF(d->pcd);
	}

	if (is_on) {
		/* dwc_otg_pcd_pullup_enable(pcd); */
		pcd->conn_en = 1;
		pcd->conn_status = 0;
	} else {
		dwc_otg_pcd_pullup_disable(pcd);
		pcd->conn_status = 2;
	}

	return 0;

}

static int dwc_otg_gadget_start(struct usb_gadget *g,
				struct usb_gadget_driver *driver)
{
	DWC_DEBUGPL(DBG_PCD, "registering gadget driver '%s'\n",
		    driver->driver.name);
	if (gadget_wrapper == 0) {
		DWC_ERROR("ENODEV\n");
		return -ENODEV;
	}
	if (gadget_wrapper->driver != 0) {
		DWC_ERROR("EBUSY (%p)\n", gadget_wrapper->driver);
		return -EBUSY;
	}
	/* hook up the driver */
	gadget_wrapper->driver = driver;
	gadget_wrapper->gadget.dev.driver = &driver->driver;

	DWC_DEBUGPL(DBG_PCD, "bind to driver %s\n", driver->driver.name);

	return 0;
}

static int dwc_otg_gadget_stop(struct usb_gadget *g,
			       struct usb_gadget_driver *driver)
{
	return 0;
}

static const struct usb_gadget_ops dwc_otg_pcd_ops = {
	.get_frame = get_frame_number,
	.wakeup = wakeup,
	.pullup = dwc_otg_pcd_pullup,
#ifdef CONFIG_USB_DWC_OTG_LPM
	.lpm_support = test_lpm_enabled,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
	.besl_support = test_besl_enabled,
	.get_baseline_besl = get_param_baseline_besl,
	.get_deep_besl = get_param_deep_besl,
#endif
#endif
	.udc_start = dwc_otg_gadget_start,
	.udc_stop = dwc_otg_gadget_stop,

	/* current versions must always be self-powered */
};

static int _setup(dwc_otg_pcd_t *pcd, uint8_t *bytes)
{
	int retval = -DWC_E_NOT_SUPPORTED;
	if (gadget_wrapper->driver && gadget_wrapper->driver->setup) {
		retval = gadget_wrapper->driver->setup(&gadget_wrapper->gadget,
						       (struct usb_ctrlrequest
							*)bytes);
	}

	if (retval == -ENOTSUPP) {
		retval = -DWC_E_NOT_SUPPORTED;
	} else if (retval < 0) {
		retval = -DWC_E_INVALID;
	}

	return retval;
}

#ifdef DWC_EN_ISOC
static int _isoc_complete(dwc_otg_pcd_t *pcd, void *ep_handle,
			  void *req_handle, int proc_buf_num)
{
	int i, packet_count;
	struct usb_gadget_iso_packet_descriptor *iso_packet = 0;
	struct usb_iso_request *iso_req = req_handle;

	if (proc_buf_num) {
		iso_packet = iso_req->iso_packet_desc1;
	} else {
		iso_packet = iso_req->iso_packet_desc0;
	}
	packet_count =
	    dwc_otg_pcd_get_iso_packet_count(pcd, ep_handle, req_handle);
	for (i = 0; i < packet_count; ++i) {
		int status;
		int actual;
		int offset;
		dwc_otg_pcd_get_iso_packet_params(pcd, ep_handle, req_handle,
						  i, &status, &actual, &offset);
		switch (status) {
		case -DWC_E_NO_DATA:
			status = -ENODATA;
			break;
		default:
			if (status) {
				DWC_PRINTF("unknown status in isoc packet\n");
			}

		}
		iso_packet[i].status = status;
		iso_packet[i].offset = offset;
		iso_packet[i].actual_length = actual;
	}

	iso_req->status = 0;
	iso_req->process_buffer(ep_handle, iso_req);

	return 0;
}
#endif /* DWC_EN_ISOC */

#ifdef DWC_UTE_PER_IO
/**
 * Copy the contents of the extended request to the Linux usb_request's
 * extended part and call the gadget's completion.
 *
 * @param pcd			Pointer to the pcd structure
 * @param ep_handle		Void pointer to the usb_ep structure
 * @param req_handle	Void pointer to the usb_request structure
 * @param status		Request status returned from the portable logic
 * @param ereq_port		Void pointer to the extended request structure
 *						created in the the portable part that contains the
 *						results of the processed iso packets.
 */
static int _xisoc_complete(dwc_otg_pcd_t *pcd, void *ep_handle,
			   void *req_handle, int32_t status, void *ereq_port)
{
	struct dwc_ute_iso_req_ext *ereqorg = NULL;
	struct dwc_iso_xreq_port *ereqport = NULL;
	struct dwc_ute_iso_packet_descriptor *desc_org = NULL;
	int i;
	struct usb_request *req;
	/* struct dwc_ute_iso_packet_descriptor * */
	/* int status = 0; */

	req = (struct usb_request *)req_handle;
	ereqorg = &req->ext_req;
	ereqport = (struct dwc_iso_xreq_port *)ereq_port;
	desc_org = ereqorg->per_io_frame_descs;

	if (req && req->complete) {
		/* Copy the request data from the portable logic to our request */
		for (i = 0; i < ereqport->pio_pkt_count; i++) {
			desc_org[i].actual_length =
			    ereqport->per_io_frame_descs[i].actual_length;
			desc_org[i].status =
			    ereqport->per_io_frame_descs[i].status;
		}

		switch (status) {
		case -DWC_E_SHUTDOWN:
			req->status = -ESHUTDOWN;
			break;
		case -DWC_E_RESTART:
			req->status = -ECONNRESET;
			break;
		case -DWC_E_INVALID:
			req->status = -EINVAL;
			break;
		case -DWC_E_TIMEOUT:
			req->status = -ETIMEDOUT;
			break;
		default:
			req->status = status;
		}

		/* And call the gadget's completion */
		req->complete(ep_handle, req);
	}

	return 0;
}
#endif /* DWC_UTE_PER_IO */

static int _complete(dwc_otg_pcd_t *pcd, void *ep_handle,
		     void *req_handle, int32_t status, uint32_t actual)
{
	struct usb_request *req = (struct usb_request *)req_handle;
	struct dwc_otg_pcd_ep *ep = NULL;

	ep = ep_from_handle(pcd, ep_handle);

	if (GET_CORE_IF(pcd)->dma_enable) {
		/* if (req->length != 0) */
		if (req->dma != DWC_DMA_ADDR_INVALID) {
			dma_unmap_single(gadget_wrapper->gadget.dev.parent,
					 req->dma,
					 req->length !=
					 0 ? req->length : 4,
					 ep->dwc_ep.is_in
					 ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
			req->dma = DWC_DMA_ADDR_INVALID;
		}
	}

	if (req && req->complete) {
		switch (status) {
		case -DWC_E_SHUTDOWN:
			req->status = -ESHUTDOWN;
			break;
		case -DWC_E_RESTART:
			req->status = -ECONNRESET;
			break;
		case -DWC_E_INVALID:
			req->status = -EINVAL;
			break;
		case -DWC_E_TIMEOUT:
			req->status = -ETIMEDOUT;
			break;
		default:
			req->status = status;

		}

		req->actual = actual;
		DWC_SPINUNLOCK(pcd->lock);
		req->complete(ep_handle, req);
		DWC_SPINLOCK(pcd->lock);
	}


	return 0;
}

static int _connect(dwc_otg_pcd_t *pcd, int speed)
{
	gadget_wrapper->gadget.speed = speed;
	return 0;
}

static int _disconnect(dwc_otg_pcd_t *pcd)
{
	if (gadget_wrapper->driver && gadget_wrapper->driver->disconnect) {
		gadget_wrapper->driver->disconnect(&gadget_wrapper->gadget);
	}
	return 0;
}

static int _resume(dwc_otg_pcd_t *pcd)
{
	if (gadget_wrapper->driver && gadget_wrapper->driver->resume) {
		gadget_wrapper->driver->resume(&gadget_wrapper->gadget);
	}

	return 0;
}

static int _suspend(dwc_otg_pcd_t *pcd)
{
	if (gadget_wrapper->driver && gadget_wrapper->driver->suspend) {
		gadget_wrapper->driver->suspend(&gadget_wrapper->gadget);
	}
	return 0;
}

/**
 * This function updates the otg values in the gadget structure.
 */
static int _hnp_changed(dwc_otg_pcd_t *pcd)
{

	if (!gadget_wrapper->gadget.is_otg)
		return 0;

	gadget_wrapper->gadget.b_hnp_enable = get_b_hnp_enable(pcd);
	gadget_wrapper->gadget.a_hnp_support = get_a_hnp_support(pcd);
	gadget_wrapper->gadget.a_alt_hnp_support = get_a_alt_hnp_support(pcd);
	return 0;
}

static int _reset(dwc_otg_pcd_t *pcd)
{
	return 0;
}

#ifdef DWC_UTE_CFI
static int _cfi_setup(dwc_otg_pcd_t *pcd, void *cfi_req)
{
	int retval = -DWC_E_INVALID;
	if (gadget_wrapper->driver->cfi_feature_setup) {
		retval =
		    gadget_wrapper->driver->cfi_feature_setup(&gadget_wrapper->
							      gadget,
							      (struct
							       cfi_usb_ctrlrequest
							       *)cfi_req);
	}

	return retval;
}
#endif

static const struct dwc_otg_pcd_function_ops fops = {
	.complete = _complete,
#ifdef DWC_EN_ISOC
	.isoc_complete = _isoc_complete,
#endif
	.setup = _setup,
	.disconnect = _disconnect,
	.connect = _connect,
	.resume = _resume,
	.suspend = _suspend,
	.hnp_changed = _hnp_changed,
	.reset = _reset,
#ifdef DWC_UTE_CFI
	.cfi_setup = _cfi_setup,
#endif
#ifdef DWC_UTE_PER_IO
	.xisoc_complete = _xisoc_complete,
#endif
};

/**
 * This function is the top level PCD interrupt handler.
 */
static irqreturn_t dwc_otg_pcd_irq(int irq, void *dev)
{
	dwc_otg_pcd_t *pcd = dev;
	int32_t retval = IRQ_NONE;

	retval = dwc_otg_pcd_handle_intr(pcd);
	if (retval != 0) {
		/* S3C2410X_CLEAR_EINTPEND();*/
	}
	return IRQ_RETVAL(retval);
}

/**
 * This function initialized the usb_ep structures to there default
 * state.
 *
 * @param d Pointer on gadget_wrapper.
 */
void gadget_add_eps(struct gadget_wrapper *d)
{
	static const char *names[] = {

		"ep0",
		"ep1in",
		"ep2in",
		"ep3in",
		"ep4in",
		"ep5in",
		"ep6in",
		"ep7in",
		"ep8in",
		"ep9in",
		"ep10in",
		"ep11in",
		"ep12in",
		"ep13in",
		"ep14in",
		"ep15in",
		"ep1out",
		"ep2out",
		"ep3out",
		"ep4out",
		"ep5out",
		"ep6out",
		"ep7out",
		"ep8out",
		"ep9out",
		"ep10out",
		"ep11out",
		"ep12out",
		"ep13out",
		"ep14out",
		"ep15out"
	};

	int i;
	struct usb_ep *ep;
	int8_t dev_endpoints;

	DWC_DEBUGPL(DBG_PCDV, "%s\n", __func__);

	INIT_LIST_HEAD(&d->gadget.ep_list);
	d->gadget.ep0 = &d->ep0;
	d->gadget.speed = USB_SPEED_UNKNOWN;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)
	d->gadget.max_speed = USB_SPEED_HIGH;
#endif

	INIT_LIST_HEAD(&d->gadget.ep0->ep_list);

	/**
	 * Initialize the EP0 structure.
	 */
	ep = &d->ep0;

	/* Init the usb_ep structure. */
	ep->name = names[0];
	ep->ops = (struct usb_ep_ops *)&dwc_otg_pcd_ep_ops;

	/**
	 * @todo NGS: What should the max packet size be set to
	 * here?  Before EP type is set?
	 */
	ep->maxpacket = MAX_PACKET_SIZE;
	dwc_otg_pcd_ep_enable(d->pcd, NULL, ep);

	list_add_tail(&ep->ep_list, &d->gadget.ep_list);

	/**
	 * Initialize the EP structures.
	 */
	dev_endpoints = d->pcd->core_if->dev_if->num_in_eps;

	for (i = 0; i < dev_endpoints; i++) {
		ep = &d->in_ep[i];

		/* Init the usb_ep structure. */
		ep->name = names[d->pcd->in_ep[i].dwc_ep.num];
		ep->ops = (struct usb_ep_ops *)&dwc_otg_pcd_ep_ops;

		/**
		 * @todo NGS: What should the max packet size be set to
		 * here?  Before EP type is set?
		 */
		ep->maxpacket = MAX_PACKET_SIZE;
		list_add_tail(&ep->ep_list, &d->gadget.ep_list);
	}

	dev_endpoints = d->pcd->core_if->dev_if->num_out_eps;

	for (i = 0; i < dev_endpoints; i++) {
		ep = &d->out_ep[i];

		/* Init the usb_ep structure. */
		ep->name = names[15 + d->pcd->out_ep[i].dwc_ep.num];
		ep->ops = (struct usb_ep_ops *)&dwc_otg_pcd_ep_ops;

		/**
		 * @todo NGS: What should the max packet size be set to
		 * here?  Before EP type is set?
		 */
		ep->maxpacket = MAX_PACKET_SIZE;

		list_add_tail(&ep->ep_list, &d->gadget.ep_list);
	}

	/* remove ep0 from the list.  There is a ep0 pointer. */
	list_del_init(&d->ep0.ep_list);

	d->ep0.maxpacket = MAX_EP0_SIZE;
}

/**
 * This function releases the Gadget device.
 * required by device_unregister().
 *
 * @todo Should this do something?	Should it free the PCD?
 */
static void dwc_otg_pcd_gadget_release(struct device *dev)
{
	DWC_DEBUGPL(DBG_PCDV, "%s(%p)\n", __func__, dev);
}

static struct gadget_wrapper *alloc_wrapper(struct platform_device *_dev)
{
	static char pcd_name[] = "dwc_otg_pcd";

	dwc_otg_device_t *otg_dev = dwc_get_device_platform_data(_dev);
	struct gadget_wrapper *d;
	int retval;

	d = DWC_ALLOC(sizeof(*d));
	if (d == NULL) {
		return NULL;
	}

	memset(d, 0, sizeof(*d));

	d->gadget.name = pcd_name;
	d->pcd = otg_dev->pcd;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 30)
	strcpy(d->gadget.dev.bus_id, "gadget");
#else
	dev_set_name(&d->gadget.dev, "%s", "gadget");
#endif

	d->gadget.dev.parent = &_dev->dev;
	d->gadget.dev.release = dwc_otg_pcd_gadget_release;
	d->gadget.ops = &dwc_otg_pcd_ops;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 0)
	d->gadget.is_dualspeed = dwc_otg_pcd_is_dualspeed(otg_dev->pcd);
#endif
	d->gadget.is_otg = dwc_otg_pcd_is_otg(otg_dev->pcd);

	d->gadget.max_speed = USB_SPEED_HIGH;
	d->driver = 0;
	d->pcd->conn_en = 0;
	/* Register the gadget device */
	retval = usb_add_gadget_udc(&_dev->dev, &d->gadget);
	if (retval != 0) {
		DWC_ERROR("device_register failed\n");
		DWC_FREE(d);
		return NULL;
	}

	return d;
}

static void free_wrapper(struct gadget_wrapper *d)
{
	if (d->driver) {
		/* should have been done already by driver model core */
		DWC_WARN("driver '%s' is still registered\n",
			 d->driver->driver.name);
		usb_gadget_unregister_driver(d->driver);
	}

	usb_del_gadget_udc(&d->gadget);
	DWC_FREE(d);
}

static void dwc_otg_pcd_work_init(dwc_otg_pcd_t *pcd,
				  struct platform_device *dev);

/**
 * This function initialized the PCD portion of the driver.
 *
 */
int pcd_init(struct platform_device *_dev)
{

	dwc_otg_device_t *otg_dev = dwc_get_device_platform_data(_dev);

	int retval = 0;
	int irq;
	DWC_DEBUGPL(DBG_PCDV, "%s(%p)\n", __func__, _dev);

	otg_dev->pcd = dwc_otg_pcd_init(otg_dev->core_if);
	printk("pcd_init otg_dev = %p\n", otg_dev);

	if (!otg_dev->pcd) {
		DWC_ERROR("dwc_otg_pcd_init failed\n");
		return -ENOMEM;
	}

	otg_dev->pcd->otg_dev = otg_dev;
	gadget_wrapper = alloc_wrapper(_dev);

	/*
	 * Initialize EP structures
	 */
	gadget_add_eps(gadget_wrapper);
	/*
	 * Setup interupt handler
	 */
	irq = platform_get_irq(_dev, 0);
	DWC_DEBUGPL(DBG_ANY, "registering handler for irq%d\n", irq);
	retval = request_irq(irq, dwc_otg_pcd_irq,
			     IRQF_SHARED,
			     gadget_wrapper->gadget.name, otg_dev->pcd);
	if (retval != 0) {
		DWC_ERROR("request of irq%d failed\n", irq);
		free_wrapper(gadget_wrapper);
		return -EBUSY;
	}

	dwc_otg_pcd_start(gadget_wrapper->pcd, &fops);

	dwc_otg_pcd_work_init(otg_dev->pcd, _dev);

	return retval;
}

/**
 * Cleanup the PCD.
 */
void pcd_remove(struct platform_device *_dev)
{

	dwc_otg_device_t *otg_dev = dwc_get_device_platform_data(_dev);
	dwc_otg_pcd_t *pcd = otg_dev->pcd;
	int irq;

	DWC_DEBUGPL(DBG_PCDV, "%s(%p)\n", __func__, _dev);

	/*
	 * Free the IRQ
	 */
	irq = platform_get_irq(_dev, 0);
	free_irq(irq, pcd);
	free_wrapper(gadget_wrapper);
	dwc_otg_pcd_remove(otg_dev->pcd);
	otg_dev->pcd = 0;
}

/**
 * This function registers a gadget driver with the PCD.
 *
 * When a driver is successfully registered, it will receive control
 * requests including set_configuration(), which enables non-control
 * requests.  then usb traffic follows until a disconnect is reported.
 * then a host may connect again, or the driver might get unbound.
 *
 * @param driver The driver being registered
 * @param bind The bind function of gadget driver
 */
#if 0
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37)
int usb_gadget_register_driver(struct usb_gadget_driver *driver)
#else
int usb_gadget_probe_driver(struct usb_gadget_driver *driver,
			    int (*bind) (struct usb_gadget *))
#endif
{
	int retval;

	DWC_DEBUGPL(DBG_PCD, "registering gadget driver '%s'\n",
		    driver->driver.name);

	if (!driver ||
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0)
	    driver->speed == USB_SPEED_UNKNOWN ||
#else
	    driver->max_speed == USB_SPEED_UNKNOWN ||
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37)
	    !driver->bind ||
#else
	    !bind ||
#endif
	    !driver->unbind || !driver->disconnect || !driver->setup) {
		DWC_DEBUGPL(DBG_PCDV, "EINVAL\n");
		return -EINVAL;
	}
	if (gadget_wrapper == 0) {
		DWC_DEBUGPL(DBG_PCDV, "ENODEV\n");
		return -ENODEV;
	}
	if (gadget_wrapper->driver != 0) {
		DWC_DEBUGPL(DBG_PCDV, "EBUSY (%p)\n", gadget_wrapper->driver);
		return -EBUSY;
	}

	/* hook up the driver */
	gadget_wrapper->driver = driver;
	gadget_wrapper->gadget.dev.driver = &driver->driver;

	DWC_DEBUGPL(DBG_PCD, "bind to driver %s\n", driver->driver.name);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37)
	retval = driver->bind(&gadget_wrapper->gadget);
#else
	retval = bind(&gadget_wrapper->gadget);
#endif
	if (retval) {
		DWC_ERROR("bind to driver %s --> error %d\n",
			  driver->driver.name, retval);
		gadget_wrapper->driver = 0;
		gadget_wrapper->gadget.dev.driver = 0;
		return retval;
	}
	DWC_DEBUGPL(DBG_ANY, "registered gadget driver '%s'\n",
		    driver->driver.name);
	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37)
EXPORT_SYMBOL(usb_gadget_register_driver);
#else
EXPORT_SYMBOL(usb_gadget_probe_driver);
#endif

/**
 * This function unregisters a gadget driver
 *
 * @param driver The driver being unregistered
 */
int usb_gadget_unregister_driver(struct usb_gadget_driver *driver)
{
	/* DWC_DEBUGPL(DBG_PCDV,"%s(%p)\n", __func__, _driver); */

	if (gadget_wrapper == 0) {
		DWC_DEBUGPL(DBG_ANY, "%s Return(%d): s_pcd==0\n", __func__,
			    -ENODEV);
		return -ENODEV;
	}
	if (driver == 0 || driver != gadget_wrapper->driver) {
		DWC_DEBUGPL(DBG_ANY, "%s Return(%d): driver?\n", __func__,
			    -EINVAL);
		return -EINVAL;
	}

	driver->unbind(&gadget_wrapper->gadget);
	gadget_wrapper->driver = 0;

	DWC_DEBUGPL(DBG_ANY, "unregistered driver '%s'\n", driver->driver.name);
	return 0;
}

EXPORT_SYMBOL(usb_gadget_unregister_driver);

#endif

/************************ for RK platform ************************/

void dwc_otg_msc_lock(dwc_otg_pcd_t *pcd)
{
	unsigned long flags;
	local_irq_save(flags);
	wake_lock(&pcd->wake_lock);
	local_irq_restore(flags);
}

void dwc_otg_msc_unlock(dwc_otg_pcd_t *pcd)
{
	unsigned long flags;
	local_irq_save(flags);
	wake_unlock(&pcd->wake_lock);
	local_irq_restore(flags);
}

unsigned int dwc_otg_battery_detect(bool det_type)
{
	printk("%s\n", __func__);
	return 0;
}

static void dwc_phy_reconnect(struct work_struct *work)
{
	dwc_otg_pcd_t *pcd;
	dwc_otg_core_if_t *core_if;
	gotgctl_data_t gctrl;
	dctl_data_t dctl = {.d32 = 0 };
	struct dwc_otg_platform_data *pldata;

	pcd = container_of(work, dwc_otg_pcd_t, reconnect.work);
	pldata = pcd->otg_dev->pldata;
	core_if = GET_CORE_IF(pcd);
	gctrl.d32 = DWC_READ_REG32(&core_if->core_global_regs->gotgctl);

	if (gctrl.b.bsesvld) {
		pcd->conn_status++;
		pldata->soft_reset(pldata, RST_RECNT);
		dwc_pcd_reset(pcd);
		/*
		 * Enable the global interrupt after all the interrupt
		 * handlers are installed.
		 */
		dctl.d32 =
		    DWC_READ_REG32(&core_if->dev_if->dev_global_regs->dctl);
		dctl.b.sftdiscon = 0;
		DWC_WRITE_REG32(&core_if->dev_if->dev_global_regs->dctl,
				dctl.d32);
		printk
		    ("*******************soft connect!!!*******************\n");
	}
}

static void dwc_otg_pcd_check_vbus_work(struct work_struct *work)
{
	dwc_otg_pcd_t *_pcd =
	    container_of(work, dwc_otg_pcd_t, check_vbus_work.work);
	struct dwc_otg_device *otg_dev = _pcd->otg_dev;
	struct dwc_otg_platform_data *pldata = otg_dev->pldata;
	unsigned long flags;

	local_irq_save(flags);

	if (!pldata->get_status(USB_STATUS_ID)) {
		/* id low, host mode */
		if (pldata->dwc_otg_uart_mode != NULL) {
			/* exit phy bypass to uart & enable usb phy */
			pldata->dwc_otg_uart_mode(pldata, PHY_USB_MODE);
		}
		if (pldata->phy_status) {
			pldata->clock_enable(pldata, 1);
			pldata->phy_suspend(pldata, USB_PHY_ENABLED);
		}
		if (pldata->bc_detect_cb != NULL)
			pldata->bc_detect_cb(_pcd->vbus_status =
					     USB_BC_TYPE_DISCNT);
		else
			_pcd->vbus_status = USB_BC_TYPE_DISCNT;
		dwc_otg_enable_global_interrupts(otg_dev->core_if);

	} else if (pldata->get_status(USB_STATUS_BVABLID)) {
		/* if usb not connect before ,then start connect */
		if (_pcd->vbus_status == USB_BC_TYPE_DISCNT) {
			printk("*****************vbus detect*******************\n");
			/* if( pldata->bc_detect_cb != NULL ) */
			/* 	pldata->bc_detect_cb(_pcd->vbus_status */
			/*			     = usb_battery_charger_detect(1)); */
			/* else */
			_pcd->vbus_status = USB_BC_TYPE_SDP;
			if (_pcd->conn_en) {
				goto connect;
			} else if (pldata->phy_status == USB_PHY_ENABLED) {
				/* do not allow to connect, suspend phy */
				pldata->phy_suspend(pldata, USB_PHY_SUSPEND);
				udelay(3);
				pldata->clock_enable(pldata, 0);
			}
		} else if ((_pcd->conn_en) && (_pcd->conn_status >= 0)
			   && (_pcd->conn_status < 2)) {
			printk("****************soft reconnect******************\n");
			goto connect;
		} else if (_pcd->conn_status == 2) {
			/* release pcd->wake_lock if fail to connect,
			 * allow system to enter second sleep.
			 */
			dwc_otg_msc_unlock(_pcd);
			_pcd->conn_status++;
			if (pldata->bc_detect_cb != NULL) {
				rk_usb_charger_status = USB_BC_TYPE_DCP;
				pldata->bc_detect_cb(_pcd->vbus_status =
						     USB_BC_TYPE_DCP);
			} else {
				_pcd->vbus_status = USB_BC_TYPE_DCP;
				rk_usb_charger_status = USB_BC_TYPE_DCP;
			}
			/* fail to connect, suspend usb phy and disable clk */
			if (pldata->phy_status == USB_PHY_ENABLED) {
				pldata->phy_suspend(pldata, USB_PHY_SUSPEND);
				udelay(3);
				pldata->clock_enable(pldata, 0);
			}
		}
	} else {
		if (pldata->bc_detect_cb != NULL)
			pldata->bc_detect_cb(_pcd->vbus_status =
					     USB_BC_TYPE_DISCNT);
		else
			_pcd->vbus_status = USB_BC_TYPE_DISCNT;

		if (_pcd->conn_status) {
			_pcd->conn_status = 0;
		}

		if (pldata->phy_status == USB_PHY_ENABLED) {
			/* release wake lock */
			dwc_otg_msc_unlock(_pcd);
			/* no vbus detect here , close usb phy  */
			pldata->phy_suspend(pldata, USB_PHY_SUSPEND);
			udelay(3);
			pldata->clock_enable(pldata, 0);
		}

		/* usb phy bypass to uart mode  */
		if (pldata->dwc_otg_uart_mode != NULL)
			pldata->dwc_otg_uart_mode(pldata, PHY_UART_MODE);
	}

	schedule_delayed_work(&_pcd->check_vbus_work, HZ);
	local_irq_restore(flags);

	return;

connect:
	if (pldata->phy_status) {
		pldata->clock_enable(pldata, 1);
		pldata->phy_suspend(pldata, USB_PHY_ENABLED);
	}

	if (_pcd->conn_status == 0)
		dwc_otg_msc_lock(_pcd);

	schedule_delayed_work(&_pcd->reconnect, 8);	/* delay 8 jiffies */
	schedule_delayed_work(&_pcd->check_vbus_work, (HZ << 2));
	local_irq_restore(flags);

	return;
}

void dwc_otg_pcd_start_check_vbus_work(dwc_otg_pcd_t *pcd)
{
	/*
	 * when receive reset int,the vbus state may not be update,so
	 * always start vbus work here.
	 */
	schedule_delayed_work(&pcd->check_vbus_work, (HZ * 2));
}

/*
* 20091228,HSL@RK,to get the current vbus status.
*/
int dwc_vbus_status(void)
{
#ifdef CONFIG_USB20_OTG
	dwc_otg_pcd_t *pcd = 0;
	if (gadget_wrapper) {
		pcd = gadget_wrapper->pcd;
	}

	if (!pcd)
		return 0;
	else
		return pcd->vbus_status;
#else
	return 0;
#endif
}

EXPORT_SYMBOL(dwc_vbus_status);

static void dwc_otg_pcd_work_init(dwc_otg_pcd_t *pcd,
				  struct platform_device *dev)
{

	struct dwc_otg_device *otg_dev = pcd->otg_dev;
#ifdef CONFIG_RK_USB_UART
	struct dwc_otg_platform_data *pldata = otg_dev->pldata;
#endif

	pcd->vbus_status = USB_BC_TYPE_DISCNT;
	pcd->phy_suspend = USB_PHY_ENABLED;

	INIT_DELAYED_WORK(&pcd->reconnect, dwc_phy_reconnect);
	INIT_DELAYED_WORK(&pcd->check_vbus_work, dwc_otg_pcd_check_vbus_work);

	wake_lock_init(&pcd->wake_lock, WAKE_LOCK_SUSPEND, "usb_pcd");

	if (dwc_otg_is_device_mode(pcd->core_if) &&
	    (otg_dev->core_if->usb_mode != USB_MODE_FORCE_HOST)) {
#ifdef CONFIG_RK_USB_UART
		if (pldata->get_status(USB_STATUS_BVABLID)) {
			/* enter usb phy mode */
			pldata->dwc_otg_uart_mode(pldata, PHY_USB_MODE);
		} else {
			/* usb phy bypass to uart mode */
			pldata->dwc_otg_uart_mode(pldata, PHY_UART_MODE);
		}
#endif
		schedule_delayed_work(&pcd->check_vbus_work, (HZ << 4));
	}
#ifdef CONFIG_RK_USB_UART
	else if (pldata->dwc_otg_uart_mode != NULL)
		/* host mode,enter usb phy mode */
		pldata->dwc_otg_uart_mode(pldata, PHY_USB_MODE);
#endif

}

#endif /* DWC_HOST_ONLY */
