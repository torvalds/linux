/* ==========================================================================
 * $File: //dwh/usb_iip/dev/software/otg/linux/drivers/dwc_otg_hcd_linux.c $
 * $Revision: #22 $
 * $Date: 2012/12/21 $
 * $Change: 2131568 $
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
#ifndef DWC_DEVICE_ONLY

/**
 * @file
 *
 * This file contains the implementation of the HCD. In Linux, the HCD
 * implements the hc_driver API.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/dma-mapping.h>
#include <linux/version.h>
#include <asm/io.h>
#include <linux/usb.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35)
#include <../drivers/usb/core/hcd.h>
#else
#include <linux/usb/hcd.h>
#endif

#include "dwc_otg_hcd_if.h"
#include "dwc_otg_dbg.h"
#include "dwc_otg_driver.h"
#include "dwc_otg_hcd.h"
/**
 * Gets the endpoint number from a _bEndpointAddress argument. The endpoint is
 * qualified with its direction (possible 32 endpoints per device).
 */
#define dwc_ep_addr_to_endpoint(_bEndpointAddress_) ((_bEndpointAddress_ & USB_ENDPOINT_NUMBER_MASK) | \
						     ((_bEndpointAddress_ & USB_DIR_IN) != 0) << 4)

static const char dwc_otg_hcd_name[] = "dwc_otg_hcd";

/** @name Linux HC Driver API Functions */
/** @{ */
static int urb_enqueue(struct usb_hcd *hcd,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
		       struct usb_host_endpoint *ep,
#endif
		       struct urb *urb, gfp_t mem_flags);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
static int urb_dequeue(struct usb_hcd *hcd, struct urb *urb);
#else
static int urb_dequeue(struct usb_hcd *hcd, struct urb *urb, int status);
#endif

static void endpoint_disable(struct usb_hcd *hcd, struct usb_host_endpoint *ep);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30)
static void endpoint_reset(struct usb_hcd *hcd, struct usb_host_endpoint *ep);
#endif
static irqreturn_t dwc_otg_hcd_irq(struct usb_hcd *hcd);
extern int hcd_start(struct usb_hcd *hcd);
extern void hcd_stop(struct usb_hcd *hcd);
extern int hcd_suspend(struct usb_hcd *hcd);
extern int hcd_resume(struct usb_hcd *hcd);
static int get_frame_number(struct usb_hcd *hcd);
extern int hub_status_data(struct usb_hcd *hcd, char *buf);
extern int hub_control(struct usb_hcd *hcd,
		       u16 typeReq,
		       u16 wValue, u16 wIndex, char *buf, u16 wLength);

struct wrapper_priv_data {
	dwc_otg_hcd_t *dwc_otg_hcd;
};

/** @} */

static struct hc_driver dwc_otg_hc_driver = {

	.description = dwc_otg_hcd_name,
	.product_desc = "DWC OTG Controller",
	.hcd_priv_size = sizeof(struct wrapper_priv_data),

	.irq = dwc_otg_hcd_irq,

	.flags = HCD_MEMORY | HCD_USB2,

	//.reset =              
	.start = hcd_start,
	.stop = hcd_stop,

	.bus_suspend = hcd_suspend,
	.bus_resume =  hcd_resume, 

	.urb_enqueue = urb_enqueue,
	.urb_dequeue = urb_dequeue,
	.endpoint_disable = endpoint_disable,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30)
	.endpoint_reset = endpoint_reset,
#endif
	.get_frame_number = get_frame_number,

	.hub_status_data = hub_status_data,
	.hub_control = hub_control,
};

/** Gets the dwc_otg_hcd from a struct usb_hcd */
static inline dwc_otg_hcd_t *hcd_to_dwc_otg_hcd(struct usb_hcd *hcd)
{
	struct wrapper_priv_data *p;
	p = (struct wrapper_priv_data *)(hcd->hcd_priv);
	return p->dwc_otg_hcd;
}

/** Gets the struct usb_hcd that contains a dwc_otg_hcd_t. */
static inline struct usb_hcd *dwc_otg_hcd_to_hcd(dwc_otg_hcd_t * dwc_otg_hcd)
{
	return dwc_otg_hcd_get_priv_data(dwc_otg_hcd);
}

/** Gets the usb_host_endpoint associated with an URB. */
inline struct usb_host_endpoint *dwc_urb_to_endpoint(struct urb *urb)
{
	struct usb_device *dev = urb->dev;
	int ep_num = usb_pipeendpoint(urb->pipe);

	if (usb_pipein(urb->pipe))
		return dev->ep_in[ep_num];
	else
		return dev->ep_out[ep_num];
}

static int _disconnect(dwc_otg_hcd_t * hcd)
{
	struct usb_hcd *usb_hcd = dwc_otg_hcd_to_hcd(hcd);

	usb_hcd->self.is_b_host = 0;
	return 0;
}

static int _start(dwc_otg_hcd_t * hcd)
{
	struct usb_hcd *usb_hcd = dwc_otg_hcd_to_hcd(hcd);

	usb_hcd->self.is_b_host = dwc_otg_hcd_is_b_host(hcd);
	hcd_start(usb_hcd);

	return 0;
}

static int _hub_info(dwc_otg_hcd_t * hcd, void *urb_handle, uint32_t * hub_addr,
		     uint32_t * port_addr)
{
	struct urb *urb = (struct urb *)urb_handle;
	if (urb->dev->tt) {
		*hub_addr = urb->dev->tt->hub->devnum;
	} else {
		*hub_addr = 0;
	}
	*port_addr = urb->dev->ttport;
	return 0;
}

static int _speed(dwc_otg_hcd_t * hcd, void *urb_handle)
{
	struct urb *urb = (struct urb *)urb_handle;
	return urb->dev->speed;
}

static int _get_b_hnp_enable(dwc_otg_hcd_t * hcd)
{
	struct usb_hcd *usb_hcd = dwc_otg_hcd_to_hcd(hcd);
	return usb_hcd->self.b_hnp_enable;
}

static void allocate_bus_bandwidth(struct usb_hcd *hcd, uint32_t bw,
				   struct urb *urb)
{
	hcd_to_bus(hcd)->bandwidth_allocated += bw / urb->interval;
	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {
		hcd_to_bus(hcd)->bandwidth_isoc_reqs++;
	} else {
		hcd_to_bus(hcd)->bandwidth_int_reqs++;
	}
}

static void free_bus_bandwidth(struct usb_hcd *hcd, uint32_t bw,
			       struct urb *urb)
{
	hcd_to_bus(hcd)->bandwidth_allocated -= bw / urb->interval;
	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {
		hcd_to_bus(hcd)->bandwidth_isoc_reqs--;
	} else {
		hcd_to_bus(hcd)->bandwidth_int_reqs--;
	}
}
int _hcd_isoc_complete(dwc_otg_hcd_t * hcd,void *urb_handle,
		     dwc_otg_hcd_urb_t * dwc_otg_urb, int32_t status)
{
	struct urb *urb = (struct urb *)urb_handle;

	if(dwc_otg_urb->qh_state == URB_STATE_DQUEUE)
		status = -ENOENT;

	urb->actual_length = dwc_otg_urb->actual_length;
	/* Convert status value. */
	switch (status) {
	case -DWC_E_PROTOCOL:
		status = -EPROTO;
		break;
	case -DWC_E_IN_PROGRESS:
		status = -EINPROGRESS;
		break;
	case -DWC_E_PIPE:
		status = -EPIPE;
		break;
	case -DWC_E_IO:
		status = -EIO;
		break;
	case -DWC_E_TIMEOUT:
		status = -ETIMEDOUT;
		break;
	case -DWC_E_OVERFLOW:
		status = -EOVERFLOW;
		break;
	default:
		if (status) {
			printk(KERN_DEBUG "%s:Uknown urb status %d\n",__func__, status);

		}
	}

	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {
		int i;

		urb->error_count = dwc_otg_hcd_urb_get_error_count(dwc_otg_urb);
		for (i = 0; i < urb->number_of_packets; ++i) {
			urb->iso_frame_desc[i].actual_length = dwc_otg_urb->iso_descs[i].actual_length;
			urb->iso_frame_desc[i].status = dwc_otg_urb->iso_descs[i].status;
		}
	}

	urb->status = status;

	if (!status) {
		if ((urb->transfer_flags & URB_SHORT_NOT_OK) &&
		    (urb->actual_length < urb->transfer_buffer_length)) {
			urb->status = -EREMOTEIO;
		}
	}

	if ((usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) ||
	    (usb_pipetype(urb->pipe) == PIPE_INTERRUPT)) {
		struct usb_host_endpoint *ep = dwc_urb_to_endpoint(urb);
		if (ep) {
			free_bus_bandwidth(dwc_otg_hcd_to_hcd(hcd),
					   dwc_otg_hcd_get_ep_bandwidth(hcd,
									ep->hcpriv),
					   urb);
		}
	}
	
	
	DWC_FREE(dwc_otg_urb);

	return 0;
}
/**
 * Sets the final status of an URB and returns it to the device driver. Any
 * required cleanup of the URB is performed.
 */
static int _complete(dwc_otg_hcd_t * hcd, void *urb_handle,
		     dwc_otg_hcd_urb_t * dwc_otg_urb, int32_t status)
{
	struct urb *urb = (struct urb *)urb_handle;
#ifdef DEBUG
	if (CHK_DEBUG_LEVEL(DBG_HCDV | DBG_HCD_URB)) {
		DWC_PRINTF("%s: urb %p, device %d, ep %d %s, status=%d\n",
			   __func__, urb, usb_pipedevice(urb->pipe),
			   usb_pipeendpoint(urb->pipe),
			   usb_pipein(urb->pipe) ? "IN" : "OUT", status);
		if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {
			int i;
			for (i = 0; i < urb->number_of_packets; i++) {
				DWC_PRINTF("  ISO Desc %d status: %d\n",
					   i, urb->iso_frame_desc[i].status);
			}
		}
	}
#endif

	urb->actual_length = dwc_otg_hcd_urb_get_actual_length(dwc_otg_urb);
	/* Convert status value. */
	switch (status) {
	case -DWC_E_PROTOCOL:
		status = -EPROTO;
		break;
	case -DWC_E_IN_PROGRESS:
		status = -EINPROGRESS;
		break;
	case -DWC_E_PIPE:
		status = -EPIPE;
		break;
	case -DWC_E_IO:
		status = -EIO;
		break;
	case -DWC_E_TIMEOUT:
		status = -ETIMEDOUT;
		break;
	case -DWC_E_OVERFLOW:
		status = -EOVERFLOW;
		break;
	case -DWC_E_SHUTDOWN:
 		status = -ESHUTDOWN;
 		break;	
	default:
		if (status) {
			DWC_PRINTF("Uknown urb status %d\n", status);

		}
	}

	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {
		int i;

		urb->error_count = dwc_otg_hcd_urb_get_error_count(dwc_otg_urb);
		for (i = 0; i < urb->number_of_packets; ++i) {
			urb->iso_frame_desc[i].actual_length =
			    dwc_otg_hcd_urb_get_iso_desc_actual_length
			    (dwc_otg_urb, i);
			urb->iso_frame_desc[i].status =
			    dwc_otg_hcd_urb_get_iso_desc_status(dwc_otg_urb, i);
		}
	}

	urb->status = status;
	if (!status) {
		if ((urb->transfer_flags & URB_SHORT_NOT_OK) &&
		    (urb->actual_length < urb->transfer_buffer_length)) {
			urb->status = -EREMOTEIO;
		}
	}

	if ((usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) ||
	    (usb_pipetype(urb->pipe) == PIPE_INTERRUPT)) {
		struct usb_host_endpoint *ep = dwc_urb_to_endpoint(urb);
		if (ep) {
			free_bus_bandwidth(dwc_otg_hcd_to_hcd(hcd),
					   dwc_otg_hcd_get_ep_bandwidth(hcd,
									ep->hcpriv),
					   urb);
		}
	}
#if 0
	usb_hcd_unlink_urb_from_ep(dwc_otg_hcd_to_hcd(hcd), urb);
#endif
	DWC_FREE(dwc_otg_urb);
	dwc_otg_urb = NULL;

	DWC_SPINUNLOCK(hcd->lock);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
	usb_hcd_giveback_urb(dwc_otg_hcd_to_hcd(hcd), urb);
#else
	usb_hcd_giveback_urb(dwc_otg_hcd_to_hcd(hcd), urb, status);
#endif
	DWC_SPINLOCK(hcd->lock);

	return 0;
}

/*copy from _complete,not use spinlock*/
/**
 * Sets the final status of an URB and returns it to the device driver. Any
 * required cleanup of the URB is performed.
 */
int _complete_in_tasklet(dwc_otg_hcd_t * hcd, void *urb_handle,
		     dwc_otg_hcd_urb_t * dwc_otg_urb, int32_t status)
{
	struct urb *urb = (struct urb *)urb_handle;
#if 0
#ifdef DEBUG
	if (CHK_DEBUG_LEVEL(DBG_HCDV | DBG_HCD_URB)) {
		DWC_PRINTF("%s: urb %p, device %d, ep %d %s, status=%d\n",
			   __func__, urb, usb_pipedevice(urb->pipe),
			   usb_pipeendpoint(urb->pipe),
			   usb_pipein(urb->pipe) ? "IN" : "OUT", status);
		if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {
			int i;
			for (i = 0; i < urb->number_of_packets; i++) {
				DWC_PRINTF("  ISO Desc %d status: %d\n",
					   i, urb->iso_frame_desc[i].status);
			}
		}
	}
#endif

	urb->actual_length = dwc_otg_hcd_urb_get_actual_length(dwc_otg_urb);
	/* Convert status value. */
	switch (status) {
	case -DWC_E_PROTOCOL:
		status = -EPROTO;
		break;
	case -DWC_E_IN_PROGRESS:
		status = -EINPROGRESS;
		break;
	case -DWC_E_PIPE:
		status = -EPIPE;
		break;
	case -DWC_E_IO:
		status = -EIO;
		break;
	case -DWC_E_TIMEOUT:
		status = -ETIMEDOUT;
		break;
	case -DWC_E_OVERFLOW:
		status = -EOVERFLOW;
		break;
	default:
		if (status) {
			DWC_PRINTF("Uknown urb status %d\n", status);

		}
	}

	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {
		int i;

		urb->error_count = dwc_otg_hcd_urb_get_error_count(dwc_otg_urb);
		for (i = 0; i < urb->number_of_packets; ++i) {
			urb->iso_frame_desc[i].actual_length =
			    dwc_otg_hcd_urb_get_iso_desc_actual_length
			    (dwc_otg_urb, i);
			urb->iso_frame_desc[i].status =
			    dwc_otg_hcd_urb_get_iso_desc_status(dwc_otg_urb, i);
		}
	}

	urb->status = status;
	urb->hcpriv = NULL;
	if (!status) {
		if ((urb->transfer_flags & URB_SHORT_NOT_OK) &&
		    (urb->actual_length < urb->transfer_buffer_length)) {
			urb->status = -EREMOTEIO;
		}
	}

	if ((usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) ||
	    (usb_pipetype(urb->pipe) == PIPE_INTERRUPT)) {
		struct usb_host_endpoint *ep = dwc_urb_to_endpoint(urb);
		if (ep) {
			free_bus_bandwidth(dwc_otg_hcd_to_hcd(hcd),
					   dwc_otg_hcd_get_ep_bandwidth(hcd,
									ep->hcpriv),
					   urb);
		}
	}
#if 0
	usb_hcd_unlink_urb_from_ep(dwc_otg_hcd_to_hcd(hcd), urb);
#endif
	DWC_FREE(dwc_otg_urb);
#endif
	//DWC_SPINUNLOCK(hcd->lock);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
	usb_hcd_giveback_urb(dwc_otg_hcd_to_hcd(hcd), urb);
#else
		usb_hcd_giveback_urb(dwc_otg_hcd_to_hcd(hcd), urb, urb->status); 
#endif
	//DWC_SPINLOCK(hcd->lock);

	return 0;
}

static struct dwc_otg_hcd_function_ops hcd_fops = {
	.start = _start,
	.disconnect = _disconnect,
	.hub_info = _hub_info,
	.speed = _speed,
	.complete = _complete,
	.complete_in_tasklet = _complete_in_tasklet,
	.hcd_isoc_complete = _hcd_isoc_complete,
	.get_b_hnp_enable = _get_b_hnp_enable,
};

/**
 * Initializes the HCD. This function allocates memory for and initializes the
 * static parts of the usb_hcd and dwc_otg_hcd structures. It also registers the
 * USB bus with the core and calls the hc_driver->start() function. It returns
 * a negative error on failure.
 */
int hcd_init(
#ifdef LM_INTERFACE
		    struct lm_device *_dev
#elif  defined(PCI_INTERFACE)
		    struct pci_dev *_dev
#endif
    )
{
	struct usb_hcd *hcd = NULL;
	dwc_otg_hcd_t *dwc_otg_hcd = NULL;
#ifdef LM_INTERFACE
	dwc_otg_device_t *otg_dev = lm_get_drvdata(_dev);
#elif  defined(PCI_INTERFACE)
	dwc_otg_device_t *otg_dev = pci_get_drvdata(_dev);
#endif

	int retval = 0;

	DWC_DEBUGPL(DBG_HCD, "DWC OTG HCD INIT\n");
	
#if 0 /* move to dwc_otg_driver_probe*/
	/* Set device flags indicating whether the HCD supports DMA. */
	if (dwc_otg_is_dma_enable(otg_dev->core_if)) {
#ifdef LM_INTERFACE
		_dev->dev.dma_mask = (void *)~0;
		_dev->dev.coherent_dma_mask = ~0;
#elif  defined(PCI_INTERFACE)
		pci_set_dma_mask(_dev, DMA_32BIT_MASK);
		pci_set_consistent_dma_mask(_dev, DMA_32BIT_MASK);
#endif

	} else {
#ifdef LM_INTERFACE
		_dev->dev.dma_mask = (void *)0;
		_dev->dev.coherent_dma_mask = 0;
#elif  defined(PCI_INTERFACE)
		pci_set_dma_mask(_dev, 0);
		pci_set_consistent_dma_mask(_dev, 0);
#endif
	}
#endif
	/*
	 * Allocate memory for the base HCD plus the DWC OTG HCD.
	 * Initialize the base HCD.
	 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,30)
	hcd = usb_create_hcd(&dwc_otg_hc_driver, &_dev->dev, _dev->dev.bus_id);
#else
	hcd = usb_create_hcd(&dwc_otg_hc_driver, &_dev->dev, dev_name(&_dev->dev));
	hcd->has_tt = 1;
//      hcd->uses_new_polling = 1;
//      hcd->poll_rh = 0;
#endif
	if (!hcd) {
		retval = -ENOMEM;
		goto error1;
	}

	hcd->regs = otg_dev->os_dep.base;

	/* Initialize the DWC OTG HCD. */
	dwc_otg_hcd = dwc_otg_hcd_alloc_hcd();
	if (!dwc_otg_hcd) {
		goto error2;
	}
	((struct wrapper_priv_data *)(hcd->hcd_priv))->dwc_otg_hcd =
	    dwc_otg_hcd;
	otg_dev->hcd = dwc_otg_hcd;

	if (dwc_otg_hcd_init(dwc_otg_hcd, otg_dev->core_if)) {
		goto error2;
	}

	otg_dev->hcd->otg_dev = otg_dev;
	hcd->self.otg_port = dwc_otg_hcd_otg_port(dwc_otg_hcd);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33) //don't support for LM(with 2.6.20.1 kernel)
//	hcd->self.otg_version = dwc_otg_get_otg_version(otg_dev->core_if);
	/* Don't support SG list at this point */
	hcd->self.sg_tablesize = 0;
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)
	/* Do not to do HNP polling if not capable */
//	if (otg_dev->core_if->otg_ver)
//		hcd->self.is_hnp_cap = dwc_otg_get_hnpcapable(otg_dev->core_if);
#endif
	/*
	 * Finish generic HCD initialization and start the HCD. This function
	 * allocates the DMA buffer pool, registers the USB bus, requests the
	 * IRQ line, and calls hcd_start method.
	 */
	retval = usb_add_hcd(hcd, _dev->irq, IRQF_SHARED | IRQF_DISABLED);
	if (retval < 0) {
		goto error2;
	}

	dwc_otg_hcd_set_priv_data(dwc_otg_hcd, hcd);
	return 0;

error2:
	usb_put_hcd(hcd);
error1:
	return retval;
}

/**
 * Removes the HCD.
 * Frees memory and resources associated with the HCD and deregisters the bus.
 */
void hcd_remove(
#ifdef LM_INTERFACE
		       struct lm_device *_dev
#elif  defined(PCI_INTERFACE)
		       struct pci_dev *_dev
#endif
    )
{
#ifdef LM_INTERFACE
	dwc_otg_device_t *otg_dev = lm_get_drvdata(_dev);
#elif  defined(PCI_INTERFACE)
	dwc_otg_device_t *otg_dev = pci_get_drvdata(_dev);
#endif

	dwc_otg_hcd_t *dwc_otg_hcd;
	struct usb_hcd *hcd;

	DWC_DEBUGPL(DBG_HCD, "DWC OTG HCD REMOVE\n");

	if (!otg_dev) {
		DWC_DEBUGPL(DBG_ANY, "%s: otg_dev NULL!\n", __func__);
		return;
	}

	dwc_otg_hcd = otg_dev->hcd;

	if (!dwc_otg_hcd) {
		DWC_DEBUGPL(DBG_ANY, "%s: otg_dev->hcd NULL!\n", __func__);
		return;
	}

	hcd = dwc_otg_hcd_to_hcd(dwc_otg_hcd);

	if (!hcd) {
		DWC_DEBUGPL(DBG_ANY,
			    "%s: dwc_otg_hcd_to_hcd(dwc_otg_hcd) NULL!\n",
			    __func__);
		return;
	}
	usb_remove_hcd(hcd);
	dwc_otg_hcd_set_priv_data(dwc_otg_hcd, NULL);
	dwc_otg_hcd_remove(dwc_otg_hcd);
	usb_put_hcd(hcd);
}

/* =========================================================================
 *  Linux HC Driver Functions
 * ========================================================================= */

/** Initializes the DWC_otg controller and its root hub and prepares it for host
 * mode operation. Activates the root port. Returns 0 on success and a negative
 * error code on failure. */
int hcd_start(struct usb_hcd *hcd)
{
	dwc_otg_hcd_t *dwc_otg_hcd = hcd_to_dwc_otg_hcd(hcd);
	struct usb_bus *bus;

	DWC_DEBUGPL(DBG_HCD, "DWC OTG HCD START\n");
	bus = hcd_to_bus(hcd);

	hcd->state = HC_STATE_RUNNING;
	if (dwc_otg_hcd_start(dwc_otg_hcd, &hcd_fops)) {
		if (dwc_otg_hcd->core_if->otg_ver)
			dwc_otg_hcd->core_if->op_state = B_PERIPHERAL;
		return 0;
	}

	/* Initialize and connect root hub if one is not already attached */
	if (bus->root_hub) {
		DWC_DEBUGPL(DBG_HCD, "DWC OTG HCD Has Root Hub\n");
		/* Inform the HUB driver to resume. */
		usb_hcd_resume_root_hub(hcd);
	}

	return 0;
}

/**
 * Halts the DWC_otg host mode operations in a clean manner. USB transfers are
 * stopped.
 */
void hcd_stop(struct usb_hcd *hcd)
{
	dwc_otg_hcd_t *dwc_otg_hcd = hcd_to_dwc_otg_hcd(hcd);

	dwc_otg_hcd_stop(dwc_otg_hcd);
}

/** HCD Suspend */
int hcd_suspend(struct usb_hcd *hcd)
{
	dwc_otg_hcd_t *dwc_otg_hcd = hcd_to_dwc_otg_hcd(hcd);
	dwc_otg_hcd->auto_pm_suspend_flag = (hcd->flags>>31)&1;

	DWC_DEBUGPL(DBG_HCD, "HCD SUSPEND\n");

	dwc_otg_hcd_suspend(dwc_otg_hcd);
	hcd->flags &= (~(1<<31));

	return 0;
}

/** HCD resume */
int hcd_resume(struct usb_hcd *hcd)
{
	dwc_otg_hcd_t *dwc_otg_hcd = hcd_to_dwc_otg_hcd(hcd);
	dwc_otg_hcd->auto_pm_suspend_flag = (hcd->flags>>31)&1;
	
	DWC_DEBUGPL(DBG_HCD, "HCD RESUME\n");
	
	dwc_otg_hcd_resume(dwc_otg_hcd);
	hcd->flags &= (~(1<<31));
	return 0;
}

/** Returns the current frame number. */
static int get_frame_number(struct usb_hcd *hcd)
{
	dwc_otg_hcd_t *dwc_otg_hcd = hcd_to_dwc_otg_hcd(hcd);

	return dwc_otg_hcd_get_frame_number(dwc_otg_hcd);
}

#ifdef DEBUG
static void dump_urb_info(struct urb *urb, char *fn_name)
{
	DWC_PRINTF("%s, urb %p\n", fn_name, urb);
	DWC_PRINTF("  Device address: %d\n", usb_pipedevice(urb->pipe));
	DWC_PRINTF("  Endpoint: %d, %s\n", usb_pipeendpoint(urb->pipe),
		   (usb_pipein(urb->pipe) ? "IN" : "OUT"));
	DWC_PRINTF("  Endpoint type: %s\n", ( {
					     char *pipetype;
					     switch (usb_pipetype(urb->pipe)) {
case PIPE_CONTROL:
pipetype = "CONTROL"; break; case PIPE_BULK:
pipetype = "BULK"; break; case PIPE_INTERRUPT:
pipetype = "INTERRUPT"; break; case PIPE_ISOCHRONOUS:
pipetype = "ISOCHRONOUS"; break; default:
					     pipetype = "UNKNOWN"; break;};
					     pipetype;}
		   )) ;
	DWC_PRINTF("  Speed: %s\n", ( {
				     char *speed; switch (urb->dev->speed) {
case USB_SPEED_HIGH:
speed = "HIGH"; break; case USB_SPEED_FULL:
speed = "FULL"; break; case USB_SPEED_LOW:
speed = "LOW"; break; default:
				     speed = "UNKNOWN"; break;};
				     speed;}
		   )) ;
	DWC_PRINTF("  Max packet size: %d\n",
		   usb_maxpacket(urb->dev, urb->pipe, usb_pipeout(urb->pipe)));
	DWC_PRINTF("  Data buffer length: %d\n", urb->transfer_buffer_length);
	DWC_PRINTF("  Transfer buffer: %p, Transfer DMA: %p\n",
		   urb->transfer_buffer, (void *)urb->transfer_dma);
	DWC_PRINTF("  Setup buffer: %p, Setup DMA: %p\n",
		   urb->setup_packet, (void *)urb->setup_dma);
	DWC_PRINTF("  Interval: %d\n", urb->interval);
	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {
		int i;
		for (i = 0; i < urb->number_of_packets; i++) {
			DWC_PRINTF("  ISO Desc %d:\n", i);
			DWC_PRINTF("    offset: %d, length %d\n",
				   urb->iso_frame_desc[i].offset,
				   urb->iso_frame_desc[i].length);
		}
	}
}

#endif

/** Starts processing a USB transfer request specified by a USB Request Block
 * (URB). mem_flags indicates the type of memory allocation to use while
 * processing this URB. */
static int urb_enqueue(struct usb_hcd *hcd,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
		       struct usb_host_endpoint *ep,
#endif
		       struct urb *urb, gfp_t mem_flags)
{
	int retval = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)
	struct usb_host_endpoint *ep = urb->ep;
#endif
	dwc_otg_hcd_t *dwc_otg_hcd = hcd_to_dwc_otg_hcd(hcd);
	dwc_otg_hcd_urb_t *dwc_otg_urb;
	int i;
	int alloc_bandwidth = 0;
	uint8_t ep_type = 0;
	uint32_t flags = 0;
	void *buf;
	dwc_irqflags_t flags_lock;
#ifdef DEBUG
	if (CHK_DEBUG_LEVEL(DBG_HCDV | DBG_HCD_URB)) {
		dump_urb_info(urb, "urb_enqueue");
	}
#endif
	DWC_SPINLOCK_IRQSAVE(dwc_otg_hcd->lock, &flags_lock);
	DWC_SPINUNLOCK_IRQRESTORE(dwc_otg_hcd->lock, flags_lock);	
	if (unlikely(atomic_read(&urb->reject))) {
		printk("%s:urb(%p) had been killed\n",__func__,urb);
		return -EPERM;
	}

#if 0
	retval = usb_hcd_link_urb_to_ep(hcd,urb);
	if (unlikely(retval)) {
		DWC_ERROR("DWC OTG HCD URB Enqueue failed linking urb. "
			  "Error status %d\n", retval);
		return retval;
	}
#endif
	if ((usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS)
	    || (usb_pipetype(urb->pipe) == PIPE_INTERRUPT)) {
		if (!dwc_otg_hcd_is_bandwidth_allocated
		    (dwc_otg_hcd, &ep->hcpriv)) {
			alloc_bandwidth = 1;
		}
	}

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_CONTROL:
		ep_type = USB_ENDPOINT_XFER_CONTROL;
		break;
	case PIPE_ISOCHRONOUS:
		ep_type = USB_ENDPOINT_XFER_ISOC;
		break;
	case PIPE_BULK:
		ep_type = USB_ENDPOINT_XFER_BULK;
		break;
	case PIPE_INTERRUPT:
		ep_type = USB_ENDPOINT_XFER_INT;
		break;
	default:
		DWC_WARN("Wrong ep type\n");
	}

	dwc_otg_urb = dwc_otg_hcd_urb_alloc(dwc_otg_hcd,
					    urb->number_of_packets,
					    mem_flags == GFP_ATOMIC ? 1 : 0);

	dwc_otg_hcd_urb_set_pipeinfo(dwc_otg_urb, usb_pipedevice(urb->pipe),
				     usb_pipeendpoint(urb->pipe), ep_type,
				     usb_pipein(urb->pipe),
				     usb_maxpacket(urb->dev, urb->pipe,
						   !(usb_pipein(urb->pipe))));

	buf = urb->transfer_buffer;
	if (hcd->self.uses_dma) {
		/*
		 * Calculate virtual address from physical address,
		 * because some class driver may not fill transfer_buffer.
		 * In Buffer DMA mode virual address is used,
		 * when handling non DWORD aligned buffers.
		 */
		buf = phys_to_virt(urb->transfer_dma);
	}

	if (!(urb->transfer_flags & URB_NO_INTERRUPT))
		flags |= URB_GIVEBACK_ASAP;
	if (urb->transfer_flags & URB_ZERO_PACKET)
		flags |= URB_SEND_ZERO_PACKET;

	dwc_otg_hcd_urb_set_params(dwc_otg_urb, urb, buf,
				   urb->transfer_dma,
				   urb->transfer_buffer_length,
				   urb->setup_packet,
				   urb->setup_dma, flags, urb->interval);

	for (i = 0; i < urb->number_of_packets; ++i) {
		dwc_otg_hcd_urb_set_iso_desc_params(dwc_otg_urb, i,
						    urb->
						    iso_frame_desc[i].offset,
						    urb->
						    iso_frame_desc[i].length);
	}
	if (unlikely(atomic_read(&urb->reject))) {
		printk("%s:urb(%p) had been killed2\n",__func__,urb);
		DWC_FREE(dwc_otg_urb);
		return -EPERM;
	}
	urb->hcpriv = dwc_otg_urb;
	retval = dwc_otg_hcd_urb_enqueue(dwc_otg_hcd, dwc_otg_urb, &ep->hcpriv,
					/* mem_flags == GFP_ATOMIC ? 1 : 0*/1);
	if (!retval) {
		if (alloc_bandwidth) {
			allocate_bus_bandwidth(hcd,
					       dwc_otg_hcd_get_ep_bandwidth
					       (dwc_otg_hcd, ep->hcpriv), urb);
		}
	} else {
		DWC_FREE(dwc_otg_urb);
		urb->hcpriv = NULL;
#if 0
		usb_hcd_unlink_urb_from_ep(hcd,urb);
#endif
		if (retval == -DWC_E_NO_DEVICE) {
			retval = -ENODEV;
		}
	}

	return retval;
}

/** Aborts/cancels a USB transfer request. Always returns 0 to indicate
 * success.  */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
static int urb_dequeue(struct usb_hcd *hcd, struct urb *urb)
#else
static int urb_dequeue(struct usb_hcd *hcd, struct urb *urb, int status)
#endif
{
	int retval = 0;
	dwc_irqflags_t flags;
	dwc_otg_hcd_t *dwc_otg_hcd;
	DWC_DEBUGPL(DBG_HCD, "DWC OTG HCD URB Dequeue\n");

	dwc_otg_hcd = hcd_to_dwc_otg_hcd(hcd);

#ifdef DEBUG
	if (CHK_DEBUG_LEVEL(DBG_HCDV | DBG_HCD_URB)) {
		dump_urb_info(urb, "urb_dequeue");
	}
#endif

	DWC_SPINLOCK_IRQSAVE(dwc_otg_hcd->lock, &flags);
#if 0
	retval = usb_hcd_check_unlink_urb(hcd, urb, status);
	if(unlikely(retval))
		goto EXIT;
#endif
	if(usb_pipeint(urb->pipe) && (dwc_otg_hcd->ssplit_lock == usb_pipedevice(urb->pipe))){
		DWC_DEBUGPL(DBG_HCD, "addr=%d(%p)\n",usb_pipedevice(urb->pipe),urb->hcpriv);
		dwc_otg_hcd->ssplit_lock = 0;	
	}
		
	if(urb->hcpriv == NULL){
		DWC_WARN("urb->hcpriv == NULL! urb = %p status=%d\n",urb,status);
		goto EXIT;
	}
	retval = dwc_otg_hcd_urb_dequeue(dwc_otg_hcd, urb->hcpriv);

	if(!retval) 
	DWC_FREE(urb->hcpriv);
	
EXIT:
	DWC_SPINUNLOCK_IRQRESTORE(dwc_otg_hcd->lock, flags);

	if(retval)
		return retval;
	/* Higher layer software sets URB status. */
	if(urb->hcpriv)
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
	usb_hcd_giveback_urb(hcd, urb);
#else
	usb_hcd_giveback_urb(hcd, urb, status);
#endif
	if (CHK_DEBUG_LEVEL(DBG_HCDV | DBG_HCD_URB)) {
		DWC_PRINTF("Called usb_hcd_giveback_urb()\n");
		DWC_PRINTF("  urb->status = %d\n", urb->status);
	}

	return 0;
}

/* Frees resources in the DWC_otg controller related to a given endpoint. Also
 * clears state in the HCD related to the endpoint. Any URBs for the endpoint
 * must already be dequeued. */
static void endpoint_disable(struct usb_hcd *hcd, struct usb_host_endpoint *ep)
{
	dwc_otg_hcd_t *dwc_otg_hcd = hcd_to_dwc_otg_hcd(hcd);

	DWC_DEBUGPL(DBG_HCD,
		    "DWC OTG HCD EP DISABLE: _bEndpointAddress=0x%02x, "
		    "endpoint=%d,is_intr=%d\n", ep->desc.bEndpointAddress,
		    dwc_ep_addr_to_endpoint(ep->desc.bEndpointAddress),
			usb_endpoint_xfer_int(&ep->desc));
	if(usb_endpoint_xfer_int(&ep->desc))
		dwc_otg_hcd->ssplit_lock = 0;
	dwc_otg_hcd_endpoint_disable(dwc_otg_hcd, ep->hcpriv, 250);
	ep->hcpriv = NULL;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30)
/* Resets endpoint specific parameter values, in current version used to reset 
 * the data toggle(as a WA). This function can be called from usb_clear_halt routine */
static void endpoint_reset(struct usb_hcd *hcd, struct usb_host_endpoint *ep)
{
	dwc_irqflags_t flags;
	dwc_otg_hcd_t *dwc_otg_hcd = hcd_to_dwc_otg_hcd(hcd);

	DWC_SPINLOCK_IRQSAVE(dwc_otg_hcd->lock, &flags);
	
	if(usb_endpoint_xfer_int(&ep->desc))
		dwc_otg_hcd->ssplit_lock = 0;

	if (ep->hcpriv) {
		dwc_otg_hcd_endpoint_reset(dwc_otg_hcd, ep->hcpriv);
	}
	DWC_SPINUNLOCK_IRQRESTORE(dwc_otg_hcd->lock, flags);
}
#endif

/** Handles host mode interrupts for the DWC_otg controller. Returns IRQ_NONE if
 * there was no interrupt to handle. Returns IRQ_HANDLED if there was a valid
 * interrupt.
 *
 * This function is called by the USB core when an interrupt occurs */
static irqreturn_t dwc_otg_hcd_irq(struct usb_hcd *hcd)
{
	dwc_otg_hcd_t *dwc_otg_hcd = hcd_to_dwc_otg_hcd(hcd);
	int32_t retval = dwc_otg_hcd_handle_intr(dwc_otg_hcd);
	if (retval != 0) {
		S3C2410X_CLEAR_EINTPEND();
	}
	return IRQ_RETVAL(retval);
}

/** Creates Status Change bitmap for the root hub and root port. The bitmap is
 * returned in buf. Bit 0 is the status change indicator for the root hub. Bit 1
 * is the status change indicator for the single root port. Returns 1 if either
 * change indicator is 1, otherwise returns 0. */
int hub_status_data(struct usb_hcd *hcd, char *buf)
{
	dwc_otg_hcd_t *dwc_otg_hcd = hcd_to_dwc_otg_hcd(hcd);

	buf[0] = 0;
	buf[0] |= (dwc_otg_hcd_is_status_changed(dwc_otg_hcd, 1)) << 1;

	return (buf[0] != 0);
}

/** Handles hub class-specific requests. */
int hub_control(struct usb_hcd *hcd,
		u16 typeReq, u16 wValue, u16 wIndex, char *buf, u16 wLength)
{
	int retval;

	retval = dwc_otg_hcd_hub_control(hcd_to_dwc_otg_hcd(hcd),
					 typeReq, wValue, wIndex, buf, wLength);

	switch (retval) {
	case -DWC_E_INVALID:
		retval = -EINVAL;
		break;
	}

	return retval;
}

#endif /* DWC_DEVICE_ONLY */
