/* cyangadget.c - Linux USB Gadget driver file for the Cypress West Bridge
## ===========================
## Copyright (C) 2010  Cypress Semiconductor
##
## This program is free software; you can redistribute it and/or
## modify it under the terms of the GNU General Public License
## as published by the Free Software Foundation; either version 2
## of the License, or (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 51 Franklin Street, Fifth Floor
## Boston, MA  02110-1301, USA.
## ===========================
*/

/*
 * Cypress West Bridge high/full speed usb device controller code
 * Based on the Netchip 2280 device controller by David Brownell
 * in the linux 2.6.10 kernel
 *
 * linux/drivers/usb/gadget/net2280.c
 */

/*
 * Copyright (C) 2002 NetChip Technology, Inc. (http://www.netchip.com)
 * Copyright (C) 2003 David Brownell
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
 * Foundation, Inc., 59 Temple Place, Suite 330
 * Boston, MA  02111-1307  USA
 */

#include "cyasgadget.h"

#define	CY_AS_DRIVER_DESC		"cypress west bridge usb gadget"
#define	CY_AS_DRIVER_VERSION		"REV B"
#define	DMA_ADDR_INVALID			(~(dma_addr_t)0)

static const char cy_as_driver_name[] = "cy_astoria_gadget";
static const char cy_as_driver_desc[] = CY_AS_DRIVER_DESC;

static const char cy_as_ep0name[] = "EP0";
static const char *cy_as_ep_names[] = {
	cy_as_ep0name, "EP1",
	"EP2", "EP3", "EP4", "EP5", "EP6", "EP7", "EP8",
	"EP9", "EP10", "EP11", "EP12", "EP13", "EP14", "EP15"
};

/* forward declarations */
static void
cyas_ep_reset(
	struct cyasgadget_ep *an_ep) ;

static int
cyasgadget_fifo_status(
	struct usb_ep *_ep) ;

static void
cyasgadget_stallcallback(
	cy_as_device_handle h,
	cy_as_return_status_t status,
	uint32_t tag,
	cy_as_funct_c_b_type cbtype,
	void *cbdata);

/* variables */
static cyasgadget	*cy_as_gadget_controller;

static int append_mtp;
module_param(append_mtp, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(append_mtp,
	"west bridge to append descriptors for mtp 0=no 1=yes");

static int msc_enum_bus_0;
module_param(msc_enum_bus_0, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(msc_enum_bus_0,
	"west bridge to enumerate bus 0 as msc 0=no 1=yes");

static int msc_enum_bus_1;
module_param(msc_enum_bus_1, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(msc_enum_bus_1,
	"west bridge to enumerate bus 1 as msc 0=no 1=yes");

/* all Callbacks are placed in this subsection*/
static void cy_as_gadget_usb_event_callback(
					cy_as_device_handle h,
					cy_as_usb_event ev,
					void *evdata
					)
{
	cyasgadget  *cy_as_dev ;
	#ifndef WESTBRIDGE_NDEBUG
	struct usb_ctrlrequest *ctrlreq;
	#endif

	/* cy_as_dev = container_of(h, cyasgadget, dev_handle); */
	cy_as_dev = cy_as_gadget_controller ;
	switch (ev) {
	case cy_as_event_usb_suspend:
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message(
			"<1>_cy_as_event_usb_suspend received\n") ;
		#endif
		cy_as_dev->driver->suspend(&cy_as_dev->gadget) ;
		break;

	case cy_as_event_usb_resume:
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message(
			"<1>_cy_as_event_usb_resume received\n") ;
		#endif
		cy_as_dev->driver->resume(&cy_as_dev->gadget) ;
		break;

	case cy_as_event_usb_reset:
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message(
			"<1>_cy_as_event_usb_reset received\n") ;
		#endif
		break;

	case cy_as_event_usb_speed_change:
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message(
			"<1>_cy_as_event_usb_speed_change received\n") ;
		#endif
		break;

	case cy_as_event_usb_set_config:
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message(
			"<1>_cy_as_event_usb_set_config received\n") ;
		#endif
		break;

	case cy_as_event_usb_setup_packet:
		#ifndef WESTBRIDGE_NDEBUG
		ctrlreq = (struct usb_ctrlrequest *)evdata;

		cy_as_hal_print_message("<1>_cy_as_event_usb_setup_packet "
							"received"
							"bRequestType=0x%x,"
							"bRequest=0x%x,"
							"wValue=x%x,"
							"wIndex=0x%x,"
							"wLength=0x%x,",
							ctrlreq->bRequestType,
							ctrlreq->bRequest,
							ctrlreq->wValue,
							ctrlreq->wIndex,
							ctrlreq->wLength
							) ;
		#endif
		cy_as_dev->outsetupreq = 0;
		if ((((uint8_t *)evdata)[0] & USB_DIR_IN) == USB_DIR_OUT)
			cy_as_dev->outsetupreq = 1;
		cy_as_dev->driver->setup(&cy_as_dev->gadget,
			(struct usb_ctrlrequest *)evdata) ;
		break;

	case cy_as_event_usb_status_packet:
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message(
			"<1>_cy_as_event_usb_status_packet received\n") ;
		#endif
		break;

	case cy_as_event_usb_inquiry_before:
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message(
			"<1>_cy_as_event_usb_inquiry_before received\n") ;
		#endif
		break;

	case cy_as_event_usb_inquiry_after:
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message(
			"<1>_cy_as_event_usb_inquiry_after received\n") ;
		#endif
		break;

	case cy_as_event_usb_start_stop:
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message(
			"<1>_cy_as_event_usb_start_stop received\n") ;
		#endif
		break;

	default:
		break;
	}
}

static void cy_as_gadget_mtp_event_callback(
					cy_as_device_handle handle,
					cy_as_mtp_event evtype,
					void *evdata
					)
{

	cyasgadget *dev = cy_as_gadget_controller ;
	(void) handle;

	switch (evtype) {
	case cy_as_mtp_send_object_complete:
		{
			cy_as_mtp_send_object_complete_data *send_obj_data =
				(cy_as_mtp_send_object_complete_data *) evdata ;

			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message(
				"<6>MTP EVENT: send_object_complete\n");
			cy_as_hal_print_message(
				"<6>_bytes sent = %d\n_send status = %d",
					send_obj_data->byte_count,
					send_obj_data->status);
			#endif

			dev->tmtp_send_complete_data.byte_count =
				send_obj_data->byte_count;
			dev->tmtp_send_complete_data.status =
				send_obj_data->status;
			dev->tmtp_send_complete_data.transaction_id =
				send_obj_data->transaction_id ;
			dev->tmtp_send_complete = cy_true ;
			break;
		}
	case cy_as_mtp_get_object_complete:
		{
			cy_as_mtp_get_object_complete_data *get_obj_data =
				(cy_as_mtp_get_object_complete_data *) evdata ;

			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message(
				"<6>MTP EVENT: get_object_complete\n");
			cy_as_hal_print_message(
				"<6>_bytes got = %d\n_get status = %d",
				get_obj_data->byte_count, get_obj_data->status);
			#endif

			dev->tmtp_get_complete_data.byte_count =
				get_obj_data->byte_count;
			dev->tmtp_get_complete_data.status =
				get_obj_data->status ;
			dev->tmtp_get_complete = cy_true ;
			break;
		}
	case cy_as_mtp_block_table_needed:
		{
			dev->tmtp_need_new_blk_tbl = cy_true ;
			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message(
				"<6>MTP EVENT: cy_as_mtp_block_table_needed\n");
			#endif
			break;
		}
	default:
		break;
	}
}

static void
cyasgadget_setupreadcallback(
		cy_as_device_handle h,
		cy_as_end_point_number_t ep,
		uint32_t count,
		void *buf,
		cy_as_return_status_t status)
{
    cyasgadget_ep  *an_ep;
    cyasgadget_req *an_req;
    cyasgadget     *cy_as_dev ;
    unsigned	   stopped ;
    unsigned long	flags;
    (void)buf ;

    cy_as_dev = cy_as_gadget_controller ;
    if (cy_as_dev->driver == NULL)
		return;

    an_ep =  &cy_as_dev->an_gadget_ep[ep] ;
    spin_lock_irqsave(&cy_as_dev->lock, flags);
	stopped = an_ep->stopped ;

#ifndef WESTBRIDGE_NDEBUG
    cy_as_hal_print_message(
		"%s: ep=%d, count=%d, "
		"status=%d\n", __func__,  ep, count, status) ;
#endif

    an_req = list_entry(an_ep->queue.next,
		cyasgadget_req, queue) ;
    list_del_init(&an_req->queue) ;

    if (status == CY_AS_ERROR_SUCCESS)
		an_req->req.status = 0;
    else
		an_req->req.status = -status;
    an_req->req.actual = count ;
    an_ep->stopped = 1;

	spin_unlock_irqrestore(&cy_as_dev->lock, flags);

    an_req->req.complete(&an_ep->usb_ep_inst, &an_req->req);

    an_ep->stopped = stopped;

}
/*called when the write of a setup packet has been completed*/
static void cyasgadget_setupwritecallback(
					cy_as_device_handle h,
					cy_as_end_point_number_t ep,
					uint32_t count,
					void *buf,
					cy_as_return_status_t status
					)
{
	cyasgadget_ep  *an_ep;
	cyasgadget_req *an_req;
	cyasgadget	 *cy_as_dev ;
	unsigned	   stopped ;
	unsigned long	flags;

	(void)buf ;

	#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("<1>%s called status=0x%x\n",
			__func__, status);
	#endif

	cy_as_dev = cy_as_gadget_controller ;

	if (cy_as_dev->driver == NULL)
		return;

	an_ep =  &cy_as_dev->an_gadget_ep[ep] ;

	spin_lock_irqsave(&cy_as_dev->lock, flags);

	stopped = an_ep->stopped ;

#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message("setup_write_callback: ep=%d, "
		"count=%d, status=%d\n", ep, count, status) ;
#endif

	an_req = list_entry(an_ep->queue.next, cyasgadget_req, queue) ;
	list_del_init(&an_req->queue) ;

	an_req->req.actual = count ;
	an_req->req.status = 0 ;
	an_ep->stopped = 1;

	spin_unlock_irqrestore(&cy_as_dev->lock, flags);

	an_req->req.complete(&an_ep->usb_ep_inst, &an_req->req);

	an_ep->stopped = stopped;

}

/* called when a read operation has completed.*/
static void cyasgadget_readcallback(
					cy_as_device_handle h,
					cy_as_end_point_number_t ep,
					uint32_t count,
					void *buf,
					cy_as_return_status_t status
					)
{
	cyasgadget_ep  *an_ep;
	cyasgadget_req *an_req;
	cyasgadget	 *cy_as_dev ;
	unsigned	   stopped ;
	cy_as_return_status_t  ret ;
	unsigned long	flags;

	(void)h ;
	(void)buf ;

	cy_as_dev = cy_as_gadget_controller ;

	if (cy_as_dev->driver == NULL)
		return;

	an_ep =  &cy_as_dev->an_gadget_ep[ep] ;
	stopped = an_ep->stopped ;

	#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s: ep=%d, count=%d, status=%d\n",
			__func__, ep, count, status) ;
	#endif

	if (status == CY_AS_ERROR_CANCELED)
		return ;

	spin_lock_irqsave(&cy_as_dev->lock, flags);

	an_req = list_entry(an_ep->queue.next, cyasgadget_req, queue) ;
	list_del_init(&an_req->queue) ;

	if (status == CY_AS_ERROR_SUCCESS)
		an_req->req.status = 0 ;
	else
		an_req->req.status = -status ;

	an_req->complete = 1;
	an_req->req.actual = count ;
	an_ep->stopped = 1;

	spin_unlock_irqrestore(&cy_as_dev->lock, flags);
	an_req->req.complete(&an_ep->usb_ep_inst, &an_req->req);

	an_ep->stopped = stopped;

	/* We need to call ReadAsync on this end-point
	 * again, so as to not miss any data packets. */
	if (!an_ep->stopped) {
		spin_lock_irqsave(&cy_as_dev->lock, flags);
		an_req = 0 ;
		if (!list_empty(&an_ep->queue))
			an_req = list_entry(an_ep->queue.next,
				cyasgadget_req, queue) ;

		spin_unlock_irqrestore(&cy_as_dev->lock, flags);

		if ((an_req) && (an_req->req.status == -EINPROGRESS)) {
			ret = cy_as_usb_read_data_async(cy_as_dev->dev_handle,
				an_ep->num, cy_false, an_req->req.length,
				an_req->req.buf, cyasgadget_readcallback);

			if (ret != CY_AS_ERROR_SUCCESS)
				cy_as_hal_print_message("<1>_cy_as_gadget: "
					"cy_as_usb_read_data_async failed "
					"with error code %d\n", ret) ;
			else
				an_req->req.status = -EALREADY ;
		}
	}
}

/* function is called when a usb write operation has completed*/
static void cyasgadget_writecallback(
					cy_as_device_handle h,
					cy_as_end_point_number_t ep,
					uint32_t count,
					void *buf,
					cy_as_return_status_t status
					)
{
	cyasgadget_ep  *an_ep;
	cyasgadget_req *an_req;
	cyasgadget	 *cy_as_dev ;
	unsigned	   stopped = 0;
	cy_as_return_status_t  ret ;
	unsigned long	flags;

	(void)h ;
	(void)buf ;

	cy_as_dev = cy_as_gadget_controller ;
	if (cy_as_dev->driver == NULL)
		return;

	an_ep =  &cy_as_dev->an_gadget_ep[ep] ;

	if (status == CY_AS_ERROR_CANCELED)
		return ;

	#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s: ep=%d, count=%d, status=%d\n",
			__func__, ep, count, status) ;
	#endif

	spin_lock_irqsave(&cy_as_dev->lock, flags);

	an_req = list_entry(an_ep->queue.next, cyasgadget_req, queue) ;
	list_del_init(&an_req->queue) ;
	an_req->req.actual = count ;

	/* Verify the status value before setting req.status to zero */
	if (status == CY_AS_ERROR_SUCCESS)
		an_req->req.status = 0 ;
	else
		an_req->req.status = -status ;

	an_ep->stopped = 1;

	spin_unlock_irqrestore(&cy_as_dev->lock, flags);

	an_req->req.complete(&an_ep->usb_ep_inst, &an_req->req);
	an_ep->stopped = stopped;

	/* We need to call WriteAsync on this end-point again, so as to not
	   miss any data packets. */
	if (!an_ep->stopped) {
		spin_lock_irqsave(&cy_as_dev->lock, flags);
		an_req = 0 ;
		if (!list_empty(&an_ep->queue))
			an_req = list_entry(an_ep->queue.next,
				cyasgadget_req, queue) ;
		spin_unlock_irqrestore(&cy_as_dev->lock, flags);

		if ((an_req) && (an_req->req.status == -EINPROGRESS)) {
			ret = cy_as_usb_write_data_async(cy_as_dev->dev_handle,
				an_ep->num, an_req->req.length, an_req->req.buf,
				cy_false, cyasgadget_writecallback);

			if (ret != CY_AS_ERROR_SUCCESS)
				cy_as_hal_print_message("<1>_cy_as_gadget: "
					"cy_as_usb_write_data_async "
					"failed with error code %d\n", ret) ;
			else
				an_req->req.status = -EALREADY ;
		}
	}
}

static void cyasgadget_stallcallback(
						cy_as_device_handle h,
						cy_as_return_status_t status,
						uint32_t tag,
						cy_as_funct_c_b_type cbtype,
						void *cbdata
						)
{
	#ifndef WESTBRIDGE_NDEBUG
	if (status != CY_AS_ERROR_SUCCESS)
		cy_as_hal_print_message("<1>_set/_clear stall "
			"failed with status %d\n", status) ;
	#endif
}


/*******************************************************************/
/* All usb_ep_ops (cyasgadget_ep_ops) are placed in this subsection*/
/*******************************************************************/
static int cyasgadget_enable(
			struct usb_ep *_ep,
			const struct usb_endpoint_descriptor *desc
					)
{
	cyasgadget		*an_dev;
	cyasgadget_ep	*an_ep;
	u32			max, tmp;
	unsigned long	flags;

	an_ep = container_of(_ep, cyasgadget_ep, usb_ep_inst);
	if (!_ep || !desc || an_ep->desc || _ep->name == cy_as_ep0name
		|| desc->bDescriptorType != USB_DT_ENDPOINT)
		return -EINVAL;

	an_dev = an_ep->dev;
	if (!an_dev->driver || an_dev->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	max = le16_to_cpu(desc->wMaxPacketSize) & 0x1fff;

	spin_lock_irqsave(&an_dev->lock, flags);
	_ep->maxpacket = max & 0x7ff;
	an_ep->desc = desc;

	/* ep_reset() has already been called */
	an_ep->stopped = 0;
	an_ep->out_overflow = 0;

	if (an_ep->cyepconfig.enabled != cy_true) {
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("<1>_cy_as_gadget: "
			"cy_as_usb_end_point_config EP %s mismatch "
			"on enabled\n", an_ep->usb_ep_inst.name) ;
		#endif
		return -EINVAL;
	}

	tmp = (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK);
	an_ep->is_iso = (tmp == USB_ENDPOINT_XFER_ISOC) ? 1 : 0;

	spin_unlock_irqrestore(&an_dev->lock, flags);

	switch (tmp) {
	case USB_ENDPOINT_XFER_ISOC:
		if (an_ep->cyepconfig.type != cy_as_usb_iso) {
			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message("<1>_cy_as_gadget: "
				"cy_as_usb_end_point_config EP %s mismatch "
				"on type %d %d\n", an_ep->usb_ep_inst.name,
				an_ep->cyepconfig.type, cy_as_usb_iso) ;
			#endif
			return -EINVAL;
		}
		break;
	case USB_ENDPOINT_XFER_INT:
		if (an_ep->cyepconfig.type != cy_as_usb_int) {
			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message("<1>_cy_as_gadget: "
				"cy_as_usb_end_point_config EP %s mismatch "
				"on type %d %d\n", an_ep->usb_ep_inst.name,
				an_ep->cyepconfig.type, cy_as_usb_int) ;
			#endif
			return -EINVAL;
		}
		break;
	default:
		if (an_ep->cyepconfig.type != cy_as_usb_bulk) {
			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message("<1>_cy_as_gadget: "
				"cy_as_usb_end_point_config EP %s mismatch "
				"on type %d %d\n", an_ep->usb_ep_inst.name,
				an_ep->cyepconfig.type, cy_as_usb_bulk) ;
			#endif
			return -EINVAL;
		}
		break;
	}

	tmp = desc->bEndpointAddress;
	an_ep->is_in = (tmp & USB_DIR_IN) != 0;

	if ((an_ep->cyepconfig.dir == cy_as_usb_in) &&
	(!an_ep->is_in)) {
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("<1>_cy_as_gadget: "
			"cy_as_usb_end_point_config EP %s mismatch "
			"on dir %d %d\n", an_ep->usb_ep_inst.name,
			an_ep->cyepconfig.dir, cy_as_usb_in) ;
		#endif
		return -EINVAL;
	} else if ((an_ep->cyepconfig.dir == cy_as_usb_out) &&
	(an_ep->is_in)) {
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("<1>_cy_as_gadget: "
			"cy_as_usb_end_point_config EP %s mismatch "
			"on dir %d %d\n", an_ep->usb_ep_inst.name,
			an_ep->cyepconfig.dir, cy_as_usb_out) ;
		#endif
		return -EINVAL;
	}

	cy_as_usb_clear_stall(an_dev->dev_handle, an_ep->num,
		cyasgadget_stallcallback, 0);

	cy_as_hal_print_message("%s enabled %s (ep%d-%d) max %04x\n",
		__func__, _ep->name, an_ep->num, tmp, max);

	return 0;
}

static int cyasgadget_disable(
					struct usb_ep *_ep
					)
{
	cyasgadget_ep	*an_ep;
	unsigned long	flags;

	an_ep = container_of(_ep, cyasgadget_ep, usb_ep_inst);
	if (!_ep || !an_ep->desc || _ep->name == cy_as_ep0name)
		return -EINVAL;

	spin_lock_irqsave(&an_ep->dev->lock, flags);
	cyas_ep_reset(an_ep);

	spin_unlock_irqrestore(&an_ep->dev->lock, flags);
	return 0;
}

static struct usb_request *cyasgadget_alloc_request(
			struct usb_ep *_ep, gfp_t gfp_flags
			)
{
	cyasgadget_ep	*an_ep;
	cyasgadget_req	*an_req;

	if (!_ep)
		return NULL;

	an_ep = container_of(_ep, cyasgadget_ep, usb_ep_inst);

	an_req = kzalloc(sizeof(cyasgadget_req), gfp_flags);
	if (!an_req)
		return NULL;

	an_req->req.dma = DMA_ADDR_INVALID;
	INIT_LIST_HEAD(&an_req->queue);

	return &an_req->req;
}

static void cyasgadget_free_request(
					struct usb_ep *_ep,
					struct usb_request *_req
					)
{
	cyasgadget_req *an_req ;

	if (!_ep || !_req)
		return ;

	an_req = container_of(_req, cyasgadget_req, req) ;

	kfree(an_req);
}

/* Load a packet into the fifo we use for usb IN transfers.
 * works for all endpoints. */
static int cyasgadget_queue(
				struct usb_ep *_ep,
				struct usb_request *_req,
				gfp_t gfp_flags
				)
{
	cyasgadget_req	*as_req;
	cyasgadget_ep	*as_ep;
	cyasgadget		*cy_as_dev;
	unsigned long	flags;
	cy_as_return_status_t  ret = 0;

	as_req = container_of(_req, cyasgadget_req, req);
	if (!_req || !_req->complete || !_req->buf
		|| !list_empty(&as_req->queue))
		return -EINVAL;

	as_ep = container_of(_ep, cyasgadget_ep, usb_ep_inst);

	if (!_ep || (!as_ep->desc && (as_ep->num != 0)))
		return -EINVAL;

	cy_as_dev = as_ep->dev;
	if (!cy_as_dev->driver ||
		cy_as_dev->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	spin_lock_irqsave(&cy_as_dev->lock, flags);

	_req->status = -EINPROGRESS;
	_req->actual = 0;

	spin_unlock_irqrestore(&cy_as_dev->lock, flags);

	/* Call Async functions */
	if (as_ep->is_in) {
		#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message("<1>_cy_as_gadget: "
				"cy_as_usb_write_data_async being called "
				"on ep %d\n", as_ep->num) ;
		#endif

		ret = cy_as_usb_write_data_async(cy_as_dev->dev_handle,
			as_ep->num, _req->length, _req->buf,
			cy_false, cyasgadget_writecallback) ;
		if (ret != CY_AS_ERROR_SUCCESS)
			cy_as_hal_print_message("<1>_cy_as_gadget: "
				"cy_as_usb_write_data_async failed with "
				"error code %d\n", ret) ;
		else
			_req->status = -EALREADY ;
	} else if (as_ep->num == 0) {
		/*
		ret = cy_as_usb_write_data_async(cy_as_dev->dev_handle,
			as_ep->num, _req->length, _req->buf, cy_false,
			cyasgadget_setupwritecallback) ;

		if (ret != CY_AS_ERROR_SUCCESS)
			cy_as_hal_print_message("<1>_cy_as_gadget: "
				"cy_as_usb_write_data_async failed with error "
				"code %d\n", ret) ;
		*/
		if ((cy_as_dev->outsetupreq) && (_req->length)) {
			#ifndef WESTBRIDGE_NDEBUG
				cy_as_hal_print_message("<1>_cy_as_gadget: "
					"cy_as_usb_read_data_async "
					"being called on ep %d\n",
					as_ep->num) ;
			#endif

			ret = cy_as_usb_read_data_async (
				cy_as_dev->dev_handle, as_ep->num,
				cy_true, _req->length, _req->buf,
				cyasgadget_setupreadcallback);

			if (ret != CY_AS_ERROR_SUCCESS)
				cy_as_hal_print_message("<1>_cy_as_gadget: "
				"cy_as_usb_read_data_async failed with "
				"error code %d\n", ret) ;

		} else {
			#ifndef WESTBRIDGE_NDEBUG
				cy_as_hal_print_message("<1>_cy_as_gadget: "
					"cy_as_usb_write_data_async "
					"being called on ep %d\n",
					as_ep->num) ;
			#endif

			ret = cy_as_usb_write_data_async(cy_as_dev->dev_handle,
			as_ep->num, _req->length, _req->buf, cy_false,
			cyasgadget_setupwritecallback) ;

			if (ret != CY_AS_ERROR_SUCCESS)
				cy_as_hal_print_message("<1>_cy_as_gadget: "
				"cy_as_usb_write_data_async failed with "
				"error code %d\n", ret) ;
		}

	} else if (list_empty(&as_ep->queue)) {
		#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message("<1>_cy_as_gadget: "
				"cy_as_usb_read_data_async being called since "
				"ep queue empty%d\n", ret) ;
		#endif

		ret = cy_as_usb_read_data_async(cy_as_dev->dev_handle,
			as_ep->num, cy_false, _req->length, _req->buf,
			cyasgadget_readcallback) ;
		if (ret != CY_AS_ERROR_SUCCESS)
			cy_as_hal_print_message("<1>_cy_as_gadget: "
				"cy_as_usb_read_data_async failed with error "
				"code %d\n", ret) ;
		else
			_req->status = -EALREADY ;
	}

	spin_lock_irqsave(&cy_as_dev->lock, flags);

	if (as_req)
		list_add_tail(&as_req->queue, &as_ep->queue);

	spin_unlock_irqrestore(&cy_as_dev->lock, flags);

	return 0;
}

/* dequeue request */
static int cyasgadget_dequeue(
				struct usb_ep *_ep,
				struct usb_request *_req
				)
{
	cyasgadget_ep	*an_ep;
	cyasgadget		*dev;
	an_ep = container_of(_ep, cyasgadget_ep, usb_ep_inst);
	dev = an_ep->dev ;

	#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("<1>%s called\n", __func__);
	#endif

	cy_as_usb_cancel_async(dev->dev_handle, an_ep->num);

	return 0;
}

static int cyasgadget_set_halt(
				struct usb_ep *_ep,
				int value
				)
{
	cyasgadget_ep	*an_ep;
	int			retval = 0;

	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message("<1>%s called\n", __func__);
	#endif

	an_ep = container_of(_ep, cyasgadget_ep, usb_ep_inst);
	if (!_ep || (!an_ep->desc && an_ep->num != 0))
		return -EINVAL;

	if (!an_ep->dev->driver || an_ep->dev->gadget.speed ==
		USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	if (an_ep->desc /* not ep0 */ &&
	(an_ep->desc->bmAttributes & 0x03) == USB_ENDPOINT_XFER_ISOC)
		return -EINVAL;

	if (!list_empty(&an_ep->queue))
		retval = -EAGAIN;
	else if (an_ep->is_in && value &&
		cyasgadget_fifo_status(_ep) != 0)
			retval = -EAGAIN;
	else {
		if (value) {
			cy_as_usb_set_stall(an_ep->dev->dev_handle,
				an_ep->num, cyasgadget_stallcallback, 0) ;
		} else {
			cy_as_usb_clear_stall(an_ep->dev->dev_handle,
				an_ep->num, cyasgadget_stallcallback, 0) ;
		}
	}

	return retval;
}

static int cyasgadget_fifo_status(
				struct usb_ep *_ep
				)
{
	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message("<1>%s called\n", __func__);
	#endif

	return 0 ;
}

static void cyasgadget_fifo_flush(
				struct usb_ep *_ep
				)
{
	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message("<1>%s called\n", __func__);
	#endif
}

static struct usb_ep_ops cyasgadget_ep_ops = {
	.enable		= cyasgadget_enable,
	.disable	= cyasgadget_disable,
	.alloc_request	= cyasgadget_alloc_request,
	.free_request	= cyasgadget_free_request,
	.queue		= cyasgadget_queue,
	.dequeue	= cyasgadget_dequeue,
	.set_halt	= cyasgadget_set_halt,
	.fifo_status	= cyasgadget_fifo_status,
	.fifo_flush	= cyasgadget_fifo_flush,
};

/*************************************************************/
/*This subsection contains all usb_gadget_ops cyasgadget_ops */
/*************************************************************/
static int cyasgadget_get_frame(
				struct usb_gadget *_gadget
				)
{
	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message("<1>%s called\n", __func__);
	#endif
	return 0 ;
}

static int cyasgadget_wakeup(
					struct usb_gadget *_gadget
					)
{
	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message("<1>%s called\n", __func__);
	#endif
	return 0;
}

static int cyasgadget_set_selfpowered(
					struct usb_gadget *_gadget,
					int value
					)
{
	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message("<1>%s called\n", __func__);
	#endif
	return 0;
}

static int cyasgadget_pullup(
					struct usb_gadget *_gadget,
					int is_on
					)
{
	struct cyasgadget  *cy_as_dev ;
	unsigned long   flags;

	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message("<1>%s called\n", __func__);
	#endif

	if (!_gadget)
		return -ENODEV;

	cy_as_dev = container_of(_gadget, cyasgadget, gadget);

	spin_lock_irqsave(&cy_as_dev->lock, flags);
	cy_as_dev->softconnect = (is_on != 0);
	if (is_on)
		cy_as_usb_connect(cy_as_dev->dev_handle, 0, 0) ;
	else
		cy_as_usb_disconnect(cy_as_dev->dev_handle, 0, 0) ;

	spin_unlock_irqrestore(&cy_as_dev->lock, flags);

	return 0;
}

static int cyasgadget_ioctl(
					struct usb_gadget *_gadget,
					unsigned code,
					unsigned long param
					)
{
	int err = 0;
	int retval = 0;
	int ret_stat = 0;
	cyasgadget *dev = cy_as_gadget_controller ;

	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message("<1>%s called, code=%d, param=%ld\n",
		__func__, code, param);
	#endif
	/*
	 * extract the type and number bitfields, and don't decode
	 * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
	 */
	if (_IOC_TYPE(code) != CYASGADGET_IOC_MAGIC) {
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s, bad magic number = 0x%x\n",
			__func__, _IOC_TYPE(code));
		#endif
		return -ENOTTY;
	}

	if (_IOC_NR(code) > CYASGADGET_IOC_MAXNR) {
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s, bad ioctl code = 0x%x\n",
			__func__, _IOC_NR(code));
		#endif
		return -ENOTTY;
	}

	/*
	 * the direction is a bitmask, and VERIFY_WRITE catches R/W
	 * transfers. `Type' is user-oriented, while
	 * access_ok is kernel-oriented, so the concept of "read" and
	 * "write" is reversed
	 */
	if (_IOC_DIR(code) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE,
			(void __user *)param, _IOC_SIZE(code));
	else if (_IOC_DIR(code) & _IOC_WRITE)
		err =  !access_ok(VERIFY_READ,
			(void __user *)param, _IOC_SIZE(code));

	if (err) {
		cy_as_hal_print_message("%s, bad ioctl dir = 0x%x\n",
			__func__, _IOC_DIR(code));
		return -EFAULT;
	}

	switch (code) {
	case CYASGADGET_GETMTPSTATUS:
		{
		cy_as_gadget_ioctl_tmtp_status *usr_d =
			(cy_as_gadget_ioctl_tmtp_status *)param ;

		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s: got CYASGADGET_GETMTPSTATUS\n",
			__func__);
		#endif

		retval = __put_user(dev->tmtp_send_complete,
			(uint32_t __user *)(&(usr_d->tmtp_send_complete)));
		retval = __put_user(dev->tmtp_get_complete,
			(uint32_t __user *)(&(usr_d->tmtp_get_complete)));
		retval = __put_user(dev->tmtp_need_new_blk_tbl,
			(uint32_t __user *)(&(usr_d->tmtp_need_new_blk_tbl)));

		if (copy_to_user((&(usr_d->tmtp_send_complete_data)),
			(&(dev->tmtp_send_complete_data)),
			sizeof(cy_as_gadget_ioctl_send_object)))
			return -EFAULT;

		if (copy_to_user((&(usr_d->tmtp_get_complete_data)),
			(&(dev->tmtp_get_complete_data)),
			sizeof(cy_as_gadget_ioctl_get_object)))
			return -EFAULT;
		break;
		}
	case CYASGADGET_CLEARTMTPSTATUS:
		{
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s got CYASGADGET_CLEARTMTPSTATUS\n",
			__func__);
		#endif

		dev->tmtp_send_complete = 0 ;
		dev->tmtp_get_complete = 0 ;
		dev->tmtp_need_new_blk_tbl = 0 ;

		break;
		}
	case CYASGADGET_INITSOJ:
		{
		cy_as_gadget_ioctl_i_s_o_j_d k_d ;
		cy_as_gadget_ioctl_i_s_o_j_d *usr_d =
			(cy_as_gadget_ioctl_i_s_o_j_d *)param ;
		cy_as_mtp_block_table blk_table ;
		struct scatterlist sg ;
		char *alloc_filename;
		struct file *file_to_allocate;

		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s got CYASGADGET_INITSOJ\n",
			__func__);
		#endif

		memset(&blk_table, 0, sizeof(blk_table));

		/* Get user argument structure  */
		if (copy_from_user(&k_d, usr_d,
			sizeof(cy_as_gadget_ioctl_i_s_o_j_d)))
			return -EFAULT;

		/* better use fixed size buff*/
		alloc_filename = kmalloc(k_d.name_length + 1, GFP_KERNEL);
		if (alloc_filename == NULL)
			return -ENOMEM;

		/* get the filename */
		if (copy_from_user(alloc_filename, k_d.file_name,
			k_d.name_length + 1)) {
			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message("%s: CYASGADGET_INITSOJ, "
				"copy file name from user space failed\n",
				__func__);
			#endif
			kfree(alloc_filename);
			return -EFAULT;
		}

		file_to_allocate = filp_open(alloc_filename, O_RDWR, 0);

		if (!IS_ERR(file_to_allocate)) {

			struct address_space *mapping =
				file_to_allocate->f_mapping;
			const struct address_space_operations *a_ops =
				mapping->a_ops;
			struct inode *inode = mapping->host;
			struct inode *alloc_inode =
				file_to_allocate->f_path.dentry->d_inode;
			uint32_t num_clusters = 0;
			struct buffer_head bh;
			struct kstat stat;
			int nr_pages = 0;
			int ret_stat = 0;

			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message("%s: fhandle is OK, "
				"calling vfs_getattr\n", __func__);
			#endif

			ret_stat = vfs_getattr(file_to_allocate->f_path.mnt,
				file_to_allocate->f_path.dentry, &stat);

			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message("%s: returned from "
				"vfs_getattr() stat->blksize=0x%lx\n",
				__func__, stat.blksize);
			#endif

			/* TODO:  get this from disk properties
			 * (from blockdevice)*/
			#define SECTOR_SIZE 512
			if (stat.blksize != 0) {
				num_clusters = (k_d.num_bytes) / SECTOR_SIZE;

				if (((k_d.num_bytes) % SECTOR_SIZE) != 0)
						num_clusters++;
			} else {
				goto initsoj_safe_exit;
			}

			bh.b_state = 0;
			bh.b_blocknr = 0;
			/* block size is arbitrary , we'll use sector size*/
			bh.b_size = SECTOR_SIZE ;



			/* clear dirty pages in page cache
			 * (if were any allocated) */
			nr_pages = (k_d.num_bytes) / (PAGE_CACHE_SIZE);

			if (((k_d.num_bytes) % (PAGE_CACHE_SIZE)) != 0)
				nr_pages++;

			#ifndef WESTBRIDGE_NDEBUG
			/*check out how many pages where actually allocated */
			if (mapping->nrpages != nr_pages)
				cy_as_hal_print_message("%s mpage_cleardirty "
					"mapping->nrpages %d != num_pages %d\n",
					__func__, (int) mapping->nrpages,
					nr_pages);

				cy_as_hal_print_message("%s: calling "
					"mpage_cleardirty() "
					"for %d pages\n", __func__, nr_pages);
			#endif

			ret_stat = mpage_cleardirty(mapping, nr_pages);

			/*fill up the the block table from the addr mapping  */
			if (a_ops->bmap) {
				int8_t blk_table_idx = -1;
				uint32_t file_block_idx = 0;
				uint32_t last_blk_addr_map = 0,
					curr_blk_addr_map = 0;

				#ifndef WESTBRIDGE_NDEBUG
				if (alloc_inode->i_bytes == 0)
						cy_as_hal_print_message(
						"%s: alloc_inode->ibytes =0\n",
						__func__);
				#endif

				/* iterate through the list of
				 * blocks (not clusters)*/
				for (file_block_idx = 0;
					file_block_idx < num_clusters
					/*inode->i_bytes*/; file_block_idx++) {

					/* returns starting sector number */
					curr_blk_addr_map =
						a_ops->bmap(mapping,
							file_block_idx);

					/*no valid mapping*/
					if (curr_blk_addr_map == 0) {
						#ifndef WESTBRIDGE_NDEBUG
						cy_as_hal_print_message(
							"%s:hit invalid "
							"mapping\n", __func__);
						#endif
						break;
					} else if (curr_blk_addr_map !=
						(last_blk_addr_map + 1) ||
						(blk_table.num_blocks
						[blk_table_idx] == 65535)) {

						/* next table entry */
						blk_table_idx++;
						/* starting sector of a
						 * scattered cluster*/
						blk_table.start_blocks
							[blk_table_idx] =
							curr_blk_addr_map;
						/* ++ num of blocks in cur
						 * table entry*/
						blk_table.
						num_blocks[blk_table_idx]++;

						#ifndef WESTBRIDGE_NDEBUG
						if (file_block_idx != 0)
							cy_as_hal_print_message(
							 "<*> next table "
							 "entry:%d required\n",
							 blk_table_idx);
						#endif
					} else {
						/*add contiguous block*/
						blk_table.num_blocks
						[blk_table_idx]++;
					} /*if (curr_blk_addr_map == 0)*/

					last_blk_addr_map = curr_blk_addr_map;
				} /* end for (file_block_idx = 0; file_block_idx
				< inode->i_bytes;) */

				#ifndef WESTBRIDGE_NDEBUG
				/*print result for verification*/
				{
					int i;
					cy_as_hal_print_message(
						"%s: print block table "
						"mapping:\n",
						__func__);
					for (i = 0; i <= blk_table_idx; i++) {
						cy_as_hal_print_message(
						"<1> %d 0x%x 0x%x\n", i,
						blk_table.start_blocks[i],
						blk_table.num_blocks[i]);
					}
				}
				#endif

				/* copy the block table to user
				 * space (for debug purposes) */
				retval = __put_user(
					blk_table.start_blocks[blk_table_idx],
					(uint32_t __user *)
						(&(usr_d->blk_addr_p)));

				retval = __put_user(
					blk_table.num_blocks[blk_table_idx],
					(uint32_t __user *)
						(&(usr_d->blk_count_p)));

				blk_table_idx++;
				retval = __put_user(blk_table_idx,
					(uint32_t __user *)
						(&(usr_d->item_count)));

			} /*end if (a_ops->bmap)*/

			filp_close(file_to_allocate, NULL);

			dev->tmtp_send_complete = 0 ;
			dev->tmtp_need_new_blk_tbl = 0 ;

			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message(
				"%s: calling cy_as_mtp_init_send_object()\n",
				__func__);
			#endif
			sg_init_one(&sg, &blk_table, sizeof(blk_table));
			ret_stat = cy_as_mtp_init_send_object(dev->dev_handle,
				(cy_as_mtp_block_table *)&sg,
				k_d.num_bytes, 0, 0);
			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message("%s: returned from "
				"cy_as_mtp_init_send_object()\n", __func__);
			#endif

		}
		#ifndef WESTBRIDGE_NDEBUG
		else {
			cy_as_hal_print_message(
				"%s: failed to allocate the file %s\n",
				__func__, alloc_filename);
		} /* end if (file_to_allocate)*/
		#endif
		kfree(alloc_filename);
initsoj_safe_exit:
			ret_stat = 0;
			retval = __put_user(ret_stat,
				(uint32_t __user *)(&(usr_d->ret_val)));

			break;
		}
	case CYASGADGET_INITGOJ:
		{
		cy_as_gadget_ioctl_i_g_o_j_d k_d ;
		cy_as_gadget_ioctl_i_g_o_j_d *usr_d =
			(cy_as_gadget_ioctl_i_g_o_j_d *)param ;
		cy_as_mtp_block_table blk_table ;
		struct scatterlist sg ;
		char *map_filename;
		struct file *file_to_map;

		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message(
			"%s: got CYASGADGET_INITGOJ\n",
				__func__);
		#endif

		memset(&blk_table, 0, sizeof(blk_table));

		/* Get user argument sturcutre */
		if (copy_from_user(&k_d, usr_d,
			sizeof(cy_as_gadget_ioctl_i_g_o_j_d)))
				return -EFAULT;

		map_filename = kmalloc(k_d.name_length + 1, GFP_KERNEL);
		if (map_filename == NULL)
			return -ENOMEM;
		if (copy_from_user(map_filename, k_d.file_name,
			k_d.name_length + 1)) {
			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message("%s: copy file name from "
				"user space failed\n", __func__);
			#endif
			kfree(map_filename);
			return -EFAULT;
		}

		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("<*>%s: opening %s for kernel "
			"mode access map\n", __func__, map_filename);
		#endif
		file_to_map = filp_open(map_filename, O_RDWR, 0);
		if (file_to_map) {
			struct address_space *mapping = file_to_map->f_mapping;
			const struct address_space_operations
				*a_ops = mapping->a_ops;
			struct inode *inode = mapping->host;

			int8_t blk_table_idx = -1;
			uint32_t file_block_idx = 0;
			uint32_t last_blk_addr_map = 0, curr_blk_addr_map = 0;

			/*verify operation exists*/
			if (a_ops->bmap) {
				#ifndef WESTBRIDGE_NDEBUG
				cy_as_hal_print_message(
					"<*>%s: bmap found, i_bytes=0x%x, "
					"i_size=0x%x, i_blocks=0x%x\n",
					__func__, inode->i_bytes,
					(unsigned int) inode->i_size,
					(unsigned int) inode->i_blocks);
				#endif

				k_d.num_bytes = inode->i_size;

				#ifndef WESTBRIDGE_NDEBUG
				cy_as_hal_print_message(
					"<*>%s: k_d.num_bytes=0x%x\n",
					__func__, k_d.num_bytes);
				#endif

				for (file_block_idx = 0;
					file_block_idx < inode->i_size;
					file_block_idx++) {
					curr_blk_addr_map =
						a_ops->bmap(mapping,
							file_block_idx);

					if (curr_blk_addr_map == 0) {
						/*no valid mapping*/
						#ifndef WESTBRIDGE_NDEBUG
						cy_as_hal_print_message(
							"%s: no valid "
							"mapping\n", __func__);
						#endif
						break;
					} else if (curr_blk_addr_map !=
					(last_blk_addr_map + 1)) {
						/*non-contiguous break*/
						blk_table_idx++;
						blk_table.start_blocks
							[blk_table_idx] =
							curr_blk_addr_map;
						blk_table.num_blocks
							[blk_table_idx]++;
						#ifndef WESTBRIDGE_NDEBUG
						cy_as_hal_print_message(
							"%s: found non-"
							"contiguous break",
							__func__);
						#endif
					} else {
						/*add contiguous block*/
						blk_table.num_blocks
							[blk_table_idx]++;
					}
					last_blk_addr_map = curr_blk_addr_map;
				}

				/*print result for verification*/
				#ifndef WESTBRIDGE_NDEBUG
				{
					int i = 0;

					for (i = 0 ; i <= blk_table_idx; i++) {
						cy_as_hal_print_message(
						"%s %d 0x%x 0x%x\n",
						__func__, i,
						blk_table.start_blocks[i],
						blk_table.num_blocks[i]);
					}
				}
				#endif
			} else {
				#ifndef WESTBRIDGE_NDEBUG
				cy_as_hal_print_message(
					"%s: could not find "
					"a_ops->bmap\n", __func__);
				#endif
				return -EFAULT;
			}

			filp_close(file_to_map, NULL);

			dev->tmtp_get_complete = 0 ;
			dev->tmtp_need_new_blk_tbl = 0 ;

			ret_stat = __put_user(
				blk_table.start_blocks[blk_table_idx],
				(uint32_t __user *)(&(usr_d->blk_addr_p)));

			ret_stat = __put_user(
				blk_table.num_blocks[blk_table_idx],
				(uint32_t __user *)(&(usr_d->blk_count_p)));

			sg_init_one(&sg, &blk_table, sizeof(blk_table));

			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message(
				"%s: calling cy_as_mtp_init_get_object() "
				"start=0x%x, num =0x%x, tid=0x%x, "
				"num_bytes=0x%x\n",
				__func__,
				blk_table.start_blocks[0],
				blk_table.num_blocks[0],
				k_d.tid,
				k_d.num_bytes);
			#endif

			ret_stat = cy_as_mtp_init_get_object(
				dev->dev_handle,
				(cy_as_mtp_block_table *)&sg,
				k_d.num_bytes, k_d.tid, 0, 0);
			if (ret_stat != CY_AS_ERROR_SUCCESS) {
					#ifndef WESTBRIDGE_NDEBUG
					cy_as_hal_print_message(
						"%s: cy_as_mtp_init_get_object "
						"failed ret_stat=0x%x\n",
						__func__, ret_stat);
					#endif
			}
		}
		#ifndef WESTBRIDGE_NDEBUG
		else {
				cy_as_hal_print_message(
					"%s: failed to open file %s\n",
					__func__, map_filename);
		}
		#endif
		kfree(map_filename);

		ret_stat = 0;
		retval = __put_user(ret_stat, (uint32_t __user *)
			(&(usr_d->ret_val)));
		break;
		}
	case CYASGADGET_CANCELSOJ:
		{
		cy_as_gadget_ioctl_cancel *usr_d =
			(cy_as_gadget_ioctl_cancel *)param ;

		#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message(
				"%s: got CYASGADGET_CANCELSOJ\n",
				__func__);
		#endif

		ret_stat = cy_as_mtp_cancel_send_object(dev->dev_handle, 0, 0);

		retval = __put_user(ret_stat, (uint32_t __user *)
			(&(usr_d->ret_val)));
		break;
		}
	case CYASGADGET_CANCELGOJ:
		{
		cy_as_gadget_ioctl_cancel *usr_d =
			(cy_as_gadget_ioctl_cancel *)param ;

		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s: got CYASGADGET_CANCELGOJ\n",
			__func__);
		#endif

		ret_stat = cy_as_mtp_cancel_get_object(dev->dev_handle, 0, 0);

		retval = __put_user(ret_stat,
			(uint32_t __user *)(&(usr_d->ret_val)));
		break;
		}
	default:
		{
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s: unknown ioctl received: %d\n",
			__func__, code);

		cy_as_hal_print_message("%s: known codes:\n"
			"CYASGADGET_GETMTPSTATUS=%d\n"
			"CYASGADGET_CLEARTMTPSTATUS=%d\n"
			"CYASGADGET_INITSOJ=%d\n"
			"CYASGADGET_INITGOJ=%d\n"
			"CYASGADGET_CANCELSOJ=%d\n"
			"CYASGADGET_CANCELGOJ=%d\n",
			__func__,
			CYASGADGET_GETMTPSTATUS,
			CYASGADGET_CLEARTMTPSTATUS,
			CYASGADGET_INITSOJ,
			CYASGADGET_INITGOJ,
			CYASGADGET_CANCELSOJ,
			CYASGADGET_CANCELGOJ);
		#endif
		break;
		}
	}

	return 0;
}

static const struct usb_gadget_ops cyasgadget_ops = {
	.get_frame		 = cyasgadget_get_frame,
	.wakeup		 = cyasgadget_wakeup,
	.set_selfpowered = cyasgadget_set_selfpowered,
	.pullup		 = cyasgadget_pullup,
	.ioctl	   = cyasgadget_ioctl,
};


/* keeping it simple:
 * - one bus driver, initted first;
 * - one function driver, initted second
 *
 * most of the work to support multiple controllers would
 * be to associate this gadget driver with all of them, or
 * perhaps to bind specific drivers to specific devices.
 */

static void cyas_ep_reset(
				cyasgadget_ep *an_ep
				)
{
	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message("<1>%s called\n", __func__);
	#endif

	an_ep->desc = NULL;
	INIT_LIST_HEAD(&an_ep->queue);

	an_ep->stopped = 0 ;
	an_ep->is_in   = 0 ;
	an_ep->is_iso  = 0 ;
	an_ep->usb_ep_inst.maxpacket = ~0;
	an_ep->usb_ep_inst.ops = &cyasgadget_ep_ops;
}

static void cyas_usb_reset(
				cyasgadget *cy_as_dev
				)
{
	cy_as_return_status_t ret;
	cy_as_usb_enum_control config ;

	#ifndef WESTBRIDGE_NDEBUG
	cy_as_device *dev_p = (cy_as_device *)cy_as_dev->dev_handle ;

	cy_as_hal_print_message("<1>%s called mtp_firmware=0x%x\n",
		__func__, dev_p->is_mtp_firmware);
	#endif

	ret = cy_as_misc_release_resource(cy_as_dev->dev_handle,
		cy_as_bus_u_s_b) ;
	if (ret != CY_AS_ERROR_SUCCESS && ret !=
		CY_AS_ERROR_RESOURCE_NOT_OWNED) {
		cy_as_hal_print_message("<1>_cy_as_gadget: cannot "
			"release usb resource: failed with error code %d\n",
			ret) ;
		return ;
	}

	cy_as_dev->gadget.speed = USB_SPEED_HIGH ;

	ret = cy_as_usb_start(cy_as_dev->dev_handle, 0, 0) ;
	if (ret != CY_AS_ERROR_SUCCESS) {
		cy_as_hal_print_message("<1>_cy_as_gadget: "
			"cy_as_usb_start failed with error code %d\n",
			ret) ;
		return ;
	}
	/* P port will do enumeration, not West Bridge */
	config.antioch_enumeration = cy_false ;
	/*  1  2  : 1-BUS_NUM , 2:Storage_device number, SD - is bus 1*/

	/* TODO: add module param to enumerate mass storage */
	config.mass_storage_interface = 0 ;

	if (append_mtp) {
		ret = cy_as_mtp_start(cy_as_dev->dev_handle,
			cy_as_gadget_mtp_event_callback, 0, 0);
		if (ret == CY_AS_ERROR_SUCCESS)  {
			cy_as_hal_print_message("MTP start passed, enumerating "
				"MTP interface\n");
			config.mtp_interface = append_mtp ;
			/*Do not enumerate NAND storage*/
			config.devices_to_enumerate[0][0] = cy_false;

			/*enumerate SD storage as MTP*/
			config.devices_to_enumerate[1][0] = cy_true;
		}
	} else {
		cy_as_hal_print_message("MTP start not attempted, not "
			"enumerating MTP interface\n");
		config.mtp_interface = 0 ;
		/* enumerate mass storage based on module parameters */
		config.devices_to_enumerate[0][0] = msc_enum_bus_0;
		config.devices_to_enumerate[1][0] = msc_enum_bus_1;
	}

	ret = cy_as_usb_set_enum_config(cy_as_dev->dev_handle,
		&config, 0, 0) ;
	if (ret != CY_AS_ERROR_SUCCESS) {
		cy_as_hal_print_message("<1>_cy_as_gadget: "
			"cy_as_usb_set_enum_config failed with error "
			"code %d\n", ret) ;
		return ;
	}

	cy_as_usb_set_physical_configuration(cy_as_dev->dev_handle, 1);

}

static void cyas_usb_reinit(
				cyasgadget *cy_as_dev
				)
{
	int index = 0;
	cyasgadget_ep *an_ep_p;
	cy_as_return_status_t ret;
	cy_as_device *dev_p = (cy_as_device *)cy_as_dev->dev_handle ;

	INIT_LIST_HEAD(&cy_as_dev->gadget.ep_list);

	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message("<1>%s called, is_mtp_firmware = "
		"0x%x\n", __func__, dev_p->is_mtp_firmware);
	#endif

	/* Init the end points */
	for (index = 1; index <= 15; index++) {
		an_ep_p = &cy_as_dev->an_gadget_ep[index] ;
		cyas_ep_reset(an_ep_p) ;
		an_ep_p->usb_ep_inst.name = cy_as_ep_names[index] ;
		an_ep_p->dev = cy_as_dev ;
		an_ep_p->num = index ;
		memset(&an_ep_p->cyepconfig, 0, sizeof(an_ep_p->cyepconfig));

		/* EP0, EPs 2,4,6,8 need not be added */
		if ((index <= 8) && (index % 2 == 0) &&
			(!dev_p->is_mtp_firmware)) {
			/* EP0 is 64 and EPs 2,4,6,8 not allowed */
			cy_as_dev->an_gadget_ep[index].fifo_size = 0 ;
		} else {
			if (index == 1)
				an_ep_p->fifo_size = 64;
			else
				an_ep_p->fifo_size = 512 ;
			list_add_tail(&an_ep_p->usb_ep_inst.ep_list,
				&cy_as_dev->gadget.ep_list);
		}
	}
	/* need to setendpointconfig before usb connect, this is not
	 * quite compatible with gadget methodology (ep_enable called
	 * by gadget after connect), therefore need to set config in
	 * initialization and verify compatibility in ep_enable,
	 * kick up error otherwise*/
	an_ep_p = &cy_as_dev->an_gadget_ep[3] ;
	an_ep_p->cyepconfig.enabled = cy_true ;
	an_ep_p->cyepconfig.dir = cy_as_usb_out ;
	an_ep_p->cyepconfig.type = cy_as_usb_bulk ;
	an_ep_p->cyepconfig.size = 0 ;
	an_ep_p->cyepconfig.physical = 1 ;
	ret = cy_as_usb_set_end_point_config(an_ep_p->dev->dev_handle,
		3, &an_ep_p->cyepconfig) ;
	if (ret != CY_AS_ERROR_SUCCESS) {
		cy_as_hal_print_message("cy_as_usb_set_end_point_config "
			"failed with error code %d\n", ret) ;
	}

	cy_as_usb_set_stall(an_ep_p->dev->dev_handle, 3, 0, 0);

	an_ep_p = &cy_as_dev->an_gadget_ep[5] ;
	an_ep_p->cyepconfig.enabled = cy_true ;
	an_ep_p->cyepconfig.dir = cy_as_usb_in ;
	an_ep_p->cyepconfig.type = cy_as_usb_bulk ;
	an_ep_p->cyepconfig.size = 0 ;
	an_ep_p->cyepconfig.physical = 2 ;
	ret = cy_as_usb_set_end_point_config(an_ep_p->dev->dev_handle,
		5, &an_ep_p->cyepconfig) ;
	if (ret != CY_AS_ERROR_SUCCESS) {
		cy_as_hal_print_message("cy_as_usb_set_end_point_config "
			"failed with error code %d\n", ret) ;
	}

	cy_as_usb_set_stall(an_ep_p->dev->dev_handle, 5, 0, 0);

	an_ep_p = &cy_as_dev->an_gadget_ep[9] ;
	an_ep_p->cyepconfig.enabled = cy_true ;
	an_ep_p->cyepconfig.dir = cy_as_usb_in ;
	an_ep_p->cyepconfig.type = cy_as_usb_bulk ;
	an_ep_p->cyepconfig.size = 0 ;
	an_ep_p->cyepconfig.physical = 4 ;
	ret = cy_as_usb_set_end_point_config(an_ep_p->dev->dev_handle,
		9, &an_ep_p->cyepconfig) ;
	if (ret != CY_AS_ERROR_SUCCESS) {
		cy_as_hal_print_message("cy_as_usb_set_end_point_config "
			"failed with error code %d\n", ret) ;
	}

	cy_as_usb_set_stall(an_ep_p->dev->dev_handle, 9, 0, 0);

	if (dev_p->mtp_count != 0) {
		/* these need to be set for compatibility with
		 * the gadget_enable logic */
		an_ep_p = &cy_as_dev->an_gadget_ep[2] ;
		an_ep_p->cyepconfig.enabled = cy_true ;
		an_ep_p->cyepconfig.dir = cy_as_usb_out ;
		an_ep_p->cyepconfig.type = cy_as_usb_bulk ;
		an_ep_p->cyepconfig.size = 0 ;
		an_ep_p->cyepconfig.physical = 0 ;
		cy_as_usb_set_stall(an_ep_p->dev->dev_handle, 2, 0, 0);

		an_ep_p = &cy_as_dev->an_gadget_ep[6] ;
		an_ep_p->cyepconfig.enabled = cy_true ;
		an_ep_p->cyepconfig.dir = cy_as_usb_in ;
		an_ep_p->cyepconfig.type = cy_as_usb_bulk ;
		an_ep_p->cyepconfig.size = 0 ;
		an_ep_p->cyepconfig.physical = 0 ;
		cy_as_usb_set_stall(an_ep_p->dev->dev_handle, 6, 0, 0);
	}

	cyas_ep_reset(&cy_as_dev->an_gadget_ep[0]) ;
	cy_as_dev->an_gadget_ep[0].usb_ep_inst.name = cy_as_ep_names[0] ;
	cy_as_dev->an_gadget_ep[0].dev = cy_as_dev ;
	cy_as_dev->an_gadget_ep[0].num = 0 ;
	cy_as_dev->an_gadget_ep[0].fifo_size = 64 ;

	cy_as_dev->an_gadget_ep[0].usb_ep_inst.maxpacket = 64;
	cy_as_dev->gadget.ep0 = &cy_as_dev->an_gadget_ep[0].usb_ep_inst;
	cy_as_dev->an_gadget_ep[0].stopped = 0;
	INIT_LIST_HEAD(&cy_as_dev->gadget.ep0->ep_list);
}

static void cyas_ep0_start(
				cyasgadget *dev
				)
{
	cy_as_return_status_t ret ;

	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message("<1>%s called\n", __func__);
	#endif

	ret = cy_as_usb_register_callback(dev->dev_handle,
		cy_as_gadget_usb_event_callback) ;
	if (ret != CY_AS_ERROR_SUCCESS) {
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s: cy_as_usb_register_callback "
			"failed with error code %d\n", __func__, ret) ;
		#endif
		return ;
	}

	ret = cy_as_usb_commit_config(dev->dev_handle, 0, 0) ;
	if (ret != CY_AS_ERROR_SUCCESS) {
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s: cy_as_usb_commit_config "
			"failed with error code %d\n", __func__, ret) ;
		#endif
		return ;
	}

	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message("%s: cy_as_usb_commit_config "
		"message sent\n", __func__) ;
	#endif

	ret = cy_as_usb_connect(dev->dev_handle, 0, 0) ;
	if (ret != CY_AS_ERROR_SUCCESS) {
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s: cy_as_usb_connect failed "
			"with error code %d\n", __func__, ret) ;
		#endif
		return ;
	}

	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message("%s: cy_as_usb_connect message "
		"sent\n", __func__) ;
	#endif
}

/*
 * When a driver is successfully registered, it will receive
 * control requests including set_configuration(), which enables
 * non-control requests.  then usb traffic follows until a
 * disconnect is reported.  then a host may connect again, or
 * the driver might get unbound.
 */
int usb_gadget_probe_driver(struct usb_gadget_driver *driver,
		int (*bind)(struct usb_gadget *))
{
	cyasgadget *dev = cy_as_gadget_controller ;
	int		retval;

	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message("<1>%s called driver=0x%x\n",
		__func__, (unsigned int) driver);
	#endif

	/* insist on high speed support from the driver, since
	* "must not be used in normal operation"
	*/
	if (!driver
		|| !bind
		|| !driver->unbind
		|| !driver->setup)
		return -EINVAL;

	if (!dev)
		return -ENODEV;

	if (dev->driver)
		return -EBUSY;

	/* hook up the driver ... */
	dev->softconnect = 1;
	driver->driver.bus = NULL;
	dev->driver = driver;
	dev->gadget.dev.driver = &driver->driver;

	/* Do the needful */
	cyas_usb_reset(dev) ; /* External usb */
	cyas_usb_reinit(dev) ; /* Internal */

	retval = bind(&dev->gadget);
	if (retval) {
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s bind to driver %s --> %d\n",
			__func__, driver->driver.name, retval);
		#endif

		dev->driver = NULL;
		dev->gadget.dev.driver = NULL;
		return retval;
	}

	/* ... then enable host detection and ep0; and we're ready
	* for set_configuration as well as eventual disconnect.
	*/
	cyas_ep0_start(dev);

	return 0;
}
EXPORT_SYMBOL(usb_gadget_probe_driver);

static void cyasgadget_nuke(
							cyasgadget_ep *an_ep
							)
{
	cyasgadget	*dev = cy_as_gadget_controller ;

	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message("<1>%s called\n", __func__);
	#endif

	cy_as_usb_cancel_async(dev->dev_handle, an_ep->num);
	an_ep->stopped = 1 ;

	while (!list_empty(&an_ep->queue)) {
		cyasgadget_req *an_req = list_entry
			(an_ep->queue.next, cyasgadget_req, queue) ;
		list_del_init(&an_req->queue) ;
		an_req->req.status = -ESHUTDOWN ;
		an_req->req.complete(&an_ep->usb_ep_inst, &an_req->req) ;
	}
}

static void cyasgadget_stop_activity(
				cyasgadget *dev,
				struct usb_gadget_driver *driver
				)
{
	int index ;

	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message("<1>%s called\n", __func__);
	#endif

	/* don't disconnect if it's not connected */
	if (dev->gadget.speed == USB_SPEED_UNKNOWN)
		driver = NULL;

	if (spin_is_locked(&dev->lock))
		spin_unlock(&dev->lock);

	/* Stop hardware; prevent new request submissions;
	 * and kill any outstanding requests.
	 */
	cy_as_usb_disconnect(dev->dev_handle, 0, 0) ;

	for (index = 3; index <= 7; index += 2) {
		cyasgadget_ep *an_ep_p = &dev->an_gadget_ep[index] ;
		cyasgadget_nuke(an_ep_p) ;
	}

	for (index = 9; index <= 15; index++) {
		cyasgadget_ep *an_ep_p = &dev->an_gadget_ep[index] ;
		cyasgadget_nuke(an_ep_p) ;
	}

	/* report disconnect; the driver is already quiesced */
	if (driver)
		driver->disconnect(&dev->gadget);

	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message("cy_as_usb_disconnect returned success");
	#endif

	/* Stop Usb */
	cy_as_usb_stop(dev->dev_handle, 0, 0) ;

	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message("cy_as_usb_stop returned success");
	#endif
}

int usb_gadget_unregister_driver(
				struct usb_gadget_driver *driver
				)
{
	cyasgadget	*dev = cy_as_gadget_controller ;

	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message("<1>%s called\n", __func__);
	#endif

	if (!dev)
		return -ENODEV;

	if (!driver || driver != dev->driver)
		return -EINVAL;

	cyasgadget_stop_activity(dev, driver);

	driver->unbind(&dev->gadget);
	dev->gadget.dev.driver = NULL;
	dev->driver = NULL;

	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message("unregistered driver '%s'\n",
		driver->driver.name) ;
	#endif

	return 0;
}
EXPORT_SYMBOL(usb_gadget_unregister_driver);

static void cyas_gadget_release(
				struct device *_dev
				)
{
	cyasgadget *dev = dev_get_drvdata(_dev);

	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message("<1>%s called\n", __func__);
	#endif

	kfree(dev);
}

/* DeInitialize gadget driver  */
static void cyasgadget_deinit(
			cyasgadget *cy_as_dev
			)
{
	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message("<1>_cy_as_gadget deinitialize called\n") ;
	#endif

	if (!cy_as_dev) {
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("<1>_cy_as_gadget_deinit: "
			"invalid cyasgadget device\n") ;
		#endif
		return ;
	}

	if (cy_as_dev->driver) {
		/* should have been done already by driver model core */
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("<1> cy_as_gadget: '%s' "
			"is still registered\n",
			cy_as_dev->driver->driver.name);
		#endif
		usb_gadget_unregister_driver(cy_as_dev->driver);
	}

	kfree(cy_as_dev) ;
	cy_as_gadget_controller = NULL ;
}

/* Initialize gadget driver  */
static int cyasgadget_initialize(void)
{
	cyasgadget *cy_as_dev = 0 ;
	int		 retval = 0 ;

	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message("<1>_cy_as_gadget [V1.1] initialize called\n") ;
	#endif

	if (cy_as_gadget_controller != 0) {
		cy_as_hal_print_message("<1> cy_as_gadget: the device has "
			"already been initilaized. ignoring\n") ;
		return -EBUSY ;
	}

	cy_as_dev = kzalloc(sizeof(cyasgadget), GFP_ATOMIC);
	if (cy_as_dev == NULL) {
		cy_as_hal_print_message("<1> cy_as_gadget: memory "
			"allocation failed\n") ;
		return -ENOMEM;
	}

	spin_lock_init(&cy_as_dev->lock);
	cy_as_dev->gadget.ops = &cyasgadget_ops;
	cy_as_dev->gadget.is_dualspeed = 1;

	/* the "gadget" abstracts/virtualizes the controller */
	/*strcpy(cy_as_dev->gadget.dev.bus_id, "cyasgadget");*/
	cy_as_dev->gadget.dev.release = cyas_gadget_release;
	cy_as_dev->gadget.name = cy_as_driver_name;

	/* Get the device handle */
	cy_as_dev->dev_handle = cyasdevice_getdevhandle() ;
	if (0 == cy_as_dev->dev_handle) {
		#ifndef NDEBUG
		cy_as_hal_print_message("<1> cy_as_gadget: "
			"no west bridge device\n") ;
		#endif
		retval = -EFAULT ;
		goto done ;
	}

	/* We are done now */
	cy_as_gadget_controller = cy_as_dev ;
	return 0 ;

/*
 * in case of an error
 */
done:
	if (cy_as_dev)
		cyasgadget_deinit(cy_as_dev) ;

	return retval ;
}

static int __init cyas_init(void)
{
	int init_res = 0;

	init_res = cyasgadget_initialize();

	if (init_res != 0) {
		printk(KERN_WARNING "<1> gadget ctl instance "
			"init error:%d\n", init_res);
		if (init_res > 0) {
			/* force -E/0 linux convention */
			init_res = init_res * -1;
		}
	}

	return init_res;
}
module_init(cyas_init);

static void __exit cyas_cleanup(void)
{
	if (cy_as_gadget_controller != NULL)
		cyasgadget_deinit(cy_as_gadget_controller);
}
module_exit(cyas_cleanup);


MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(CY_AS_DRIVER_DESC);
MODULE_AUTHOR("cypress semiconductor");

/*[]*/
