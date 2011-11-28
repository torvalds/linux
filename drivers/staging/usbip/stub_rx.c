/*
 * Copyright (C) 2003-2008 Takahiro Hirofuchi
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
 * USA.
 */

#include <asm/byteorder.h>
#include <linux/kthread.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include "usbip_common.h"
#include "stub.h"

static int is_clear_halt_cmd(struct urb *urb)
{
	struct usb_ctrlrequest *req;

	req = (struct usb_ctrlrequest *) urb->setup_packet;

	 return (req->bRequest == USB_REQ_CLEAR_FEATURE) &&
		 (req->bRequestType == USB_RECIP_ENDPOINT) &&
		 (req->wValue == USB_ENDPOINT_HALT);
}

static int is_set_interface_cmd(struct urb *urb)
{
	struct usb_ctrlrequest *req;

	req = (struct usb_ctrlrequest *) urb->setup_packet;

	return (req->bRequest == USB_REQ_SET_INTERFACE) &&
		(req->bRequestType == USB_RECIP_INTERFACE);
}

static int is_set_configuration_cmd(struct urb *urb)
{
	struct usb_ctrlrequest *req;

	req = (struct usb_ctrlrequest *) urb->setup_packet;

	return (req->bRequest == USB_REQ_SET_CONFIGURATION) &&
		(req->bRequestType == USB_RECIP_DEVICE);
}

static int is_reset_device_cmd(struct urb *urb)
{
	struct usb_ctrlrequest *req;
	__u16 value;
	__u16 index;

	req = (struct usb_ctrlrequest *) urb->setup_packet;
	value = le16_to_cpu(req->wValue);
	index = le16_to_cpu(req->wIndex);

	if ((req->bRequest == USB_REQ_SET_FEATURE) &&
	    (req->bRequestType == USB_RT_PORT) &&
	    (value == USB_PORT_FEAT_RESET)) {
		usbip_dbg_stub_rx("reset_device_cmd, port %u\n", index);
		return 1;
	} else
		return 0;
}

static int tweak_clear_halt_cmd(struct urb *urb)
{
	struct usb_ctrlrequest *req;
	int target_endp;
	int target_dir;
	int target_pipe;
	int ret;

	req = (struct usb_ctrlrequest *) urb->setup_packet;

	/*
	 * The stalled endpoint is specified in the wIndex value. The endpoint
	 * of the urb is the target of this clear_halt request (i.e., control
	 * endpoint).
	 */
	target_endp = le16_to_cpu(req->wIndex) & 0x000f;

	/* the stalled endpoint direction is IN or OUT?. USB_DIR_IN is 0x80.  */
	target_dir = le16_to_cpu(req->wIndex) & 0x0080;

	if (target_dir)
		target_pipe = usb_rcvctrlpipe(urb->dev, target_endp);
	else
		target_pipe = usb_sndctrlpipe(urb->dev, target_endp);

	ret = usb_clear_halt(urb->dev, target_pipe);
	if (ret < 0)
		dev_err(&urb->dev->dev, "usb_clear_halt error: devnum %d endp "
			"%d ret %d\n", urb->dev->devnum, target_endp, ret);
	else
		dev_info(&urb->dev->dev, "usb_clear_halt done: devnum %d endp "
			 "%d\n", urb->dev->devnum, target_endp);

	return ret;
}

static int tweak_set_interface_cmd(struct urb *urb)
{
	struct usb_ctrlrequest *req;
	__u16 alternate;
	__u16 interface;
	int ret;

	req = (struct usb_ctrlrequest *) urb->setup_packet;
	alternate = le16_to_cpu(req->wValue);
	interface = le16_to_cpu(req->wIndex);

	usbip_dbg_stub_rx("set_interface: inf %u alt %u\n",
			  interface, alternate);

	ret = usb_set_interface(urb->dev, interface, alternate);
	if (ret < 0)
		dev_err(&urb->dev->dev, "usb_set_interface error: inf %u alt "
			"%u ret %d\n", interface, alternate, ret);
	else
		dev_info(&urb->dev->dev, "usb_set_interface done: inf %u alt "
			 "%u\n", interface, alternate);

	return ret;
}

static int tweak_set_configuration_cmd(struct urb *urb)
{
	struct usb_ctrlrequest *req;
	__u16 config;

	req = (struct usb_ctrlrequest *) urb->setup_packet;
	config = le16_to_cpu(req->wValue);

	/*
	 * I have never seen a multi-config device. Very rare.
	 * For most devices, this will be called to choose a default
	 * configuration only once in an initialization phase.
	 *
	 * set_configuration may change a device configuration and its device
	 * drivers will be unbound and assigned for a new device configuration.
	 * This means this usbip driver will be also unbound when called, then
	 * eventually reassigned to the device as far as driver matching
	 * condition is kept.
	 *
	 * Unfortunatelly, an existing usbip connection will be dropped
	 * due to this driver unbinding. So, skip here.
	 * A user may need to set a special configuration value before
	 * exporting the device.
	 */
	dev_info(&urb->dev->dev, "usb_set_configuration %d to %s... skip!\n",
		 config, dev_name(&urb->dev->dev));

	return 0;
	/* return usb_driver_set_configuration(urb->dev, config); */
}

static int tweak_reset_device_cmd(struct urb *urb)
{
	struct stub_priv *priv = (struct stub_priv *) urb->context;
	struct stub_device *sdev = priv->sdev;

	dev_info(&urb->dev->dev, "usb_queue_reset_device\n");

	/*
	 * With the implementation of pre_reset and post_reset the driver no
	 * longer unbinds. This allows the use of synchronous reset.
	 */

	if (usb_lock_device_for_reset(sdev->udev, sdev->interface) < 0) {
		dev_err(&urb->dev->dev, "could not obtain lock to reset device\n");
		return 0;
	}
	usb_reset_device(sdev->udev);
	usb_unlock_device(sdev->udev);

	return 0;
}

/*
 * clear_halt, set_interface, and set_configuration require special tricks.
 */
static void tweak_special_requests(struct urb *urb)
{
	if (!urb || !urb->setup_packet)
		return;

	if (usb_pipetype(urb->pipe) != PIPE_CONTROL)
		return;

	if (is_clear_halt_cmd(urb))
		/* tweak clear_halt */
		 tweak_clear_halt_cmd(urb);

	else if (is_set_interface_cmd(urb))
		/* tweak set_interface */
		tweak_set_interface_cmd(urb);

	else if (is_set_configuration_cmd(urb))
		/* tweak set_configuration */
		tweak_set_configuration_cmd(urb);

	else if (is_reset_device_cmd(urb))
		tweak_reset_device_cmd(urb);
	else
		usbip_dbg_stub_rx("no need to tweak\n");
}

/*
 * stub_recv_unlink() unlinks the URB by a call to usb_unlink_urb().
 * By unlinking the urb asynchronously, stub_rx can continuously
 * process coming urbs.  Even if the urb is unlinked, its completion
 * handler will be called and stub_tx will send a return pdu.
 *
 * See also comments about unlinking strategy in vhci_hcd.c.
 */
static int stub_recv_cmd_unlink(struct stub_device *sdev,
				struct usbip_header *pdu)
{
	unsigned long flags;

	struct stub_priv *priv;

	spin_lock_irqsave(&sdev->priv_lock, flags);

	list_for_each_entry(priv, &sdev->priv_init, list) {
		if (priv->seqnum == pdu->u.cmd_unlink.seqnum) {
			int ret;

			dev_info(&priv->urb->dev->dev, "unlink urb %p\n",
				 priv->urb);

			/*
			 * This matched urb is not completed yet (i.e., be in
			 * flight in usb hcd hardware/driver). Now we are
			 * cancelling it. The unlinking flag means that we are
			 * now not going to return the normal result pdu of a
			 * submission request, but going to return a result pdu
			 * of the unlink request.
			 */
			priv->unlinking = 1;

			/*
			 * In the case that unlinking flag is on, prev->seqnum
			 * is changed from the seqnum of the cancelling urb to
			 * the seqnum of the unlink request. This will be used
			 * to make the result pdu of the unlink request.
			 */
			priv->seqnum = pdu->base.seqnum;

			spin_unlock_irqrestore(&sdev->priv_lock, flags);

			/*
			 * usb_unlink_urb() is now out of spinlocking to avoid
			 * spinlock recursion since stub_complete() is
			 * sometimes called in this context but not in the
			 * interrupt context.  If stub_complete() is executed
			 * before we call usb_unlink_urb(), usb_unlink_urb()
			 * will return an error value. In this case, stub_tx
			 * will return the result pdu of this unlink request
			 * though submission is completed and actual unlinking
			 * is not executed. OK?
			 */
			/* In the above case, urb->status is not -ECONNRESET,
			 * so a driver in a client host will know the failure
			 * of the unlink request ?
			 */
			ret = usb_unlink_urb(priv->urb);
			if (ret != -EINPROGRESS)
				dev_err(&priv->urb->dev->dev,
					"failed to unlink a urb %p, ret %d\n",
					priv->urb, ret);
			return 0;
		}
	}

	usbip_dbg_stub_rx("seqnum %d is not pending\n",
			  pdu->u.cmd_unlink.seqnum);

	/*
	 * The urb of the unlink target is not found in priv_init queue. It was
	 * already completed and its results is/was going to be sent by a
	 * CMD_RET pdu. In this case, usb_unlink_urb() is not needed. We only
	 * return the completeness of this unlink request to vhci_hcd.
	 */
	stub_enqueue_ret_unlink(sdev, pdu->base.seqnum, 0);

	spin_unlock_irqrestore(&sdev->priv_lock, flags);

	return 0;
}

static int valid_request(struct stub_device *sdev, struct usbip_header *pdu)
{
	struct usbip_device *ud = &sdev->ud;
	int valid = 0;

	if (pdu->base.devid == sdev->devid) {
		spin_lock(&ud->lock);
		if (ud->status == SDEV_ST_USED) {
			/* A request is valid. */
			valid = 1;
		}
		spin_unlock(&ud->lock);
	}

	return valid;
}

static struct stub_priv *stub_priv_alloc(struct stub_device *sdev,
					 struct usbip_header *pdu)
{
	struct stub_priv *priv;
	struct usbip_device *ud = &sdev->ud;
	unsigned long flags;

	spin_lock_irqsave(&sdev->priv_lock, flags);

	priv = kmem_cache_zalloc(stub_priv_cache, GFP_ATOMIC);
	if (!priv) {
		dev_err(&sdev->interface->dev, "alloc stub_priv\n");
		spin_unlock_irqrestore(&sdev->priv_lock, flags);
		usbip_event_add(ud, SDEV_EVENT_ERROR_MALLOC);
		return NULL;
	}

	priv->seqnum = pdu->base.seqnum;
	priv->sdev = sdev;

	/*
	 * After a stub_priv is linked to a list_head,
	 * our error handler can free allocated data.
	 */
	list_add_tail(&priv->list, &sdev->priv_init);

	spin_unlock_irqrestore(&sdev->priv_lock, flags);

	return priv;
}

static int get_pipe(struct stub_device *sdev, int epnum, int dir)
{
	struct usb_device *udev = sdev->udev;
	struct usb_host_endpoint *ep;
	struct usb_endpoint_descriptor *epd = NULL;

	if (dir == USBIP_DIR_IN)
		ep = udev->ep_in[epnum & 0x7f];
	else
		ep = udev->ep_out[epnum & 0x7f];
	if (!ep) {
		dev_err(&sdev->interface->dev, "no such endpoint?, %d\n",
			epnum);
		BUG();
	}

	epd = &ep->desc;
#if 0
	/* epnum 0 is always control */
	if (epnum == 0) {
		if (dir == USBIP_DIR_OUT)
			return usb_sndctrlpipe(udev, 0);
		else
			return usb_rcvctrlpipe(udev, 0);
	}
#endif
	if (usb_endpoint_xfer_control(epd)) {
		if (dir == USBIP_DIR_OUT)
			return usb_sndctrlpipe(udev, epnum);
		else
			return usb_rcvctrlpipe(udev, epnum);
	}

	if (usb_endpoint_xfer_bulk(epd)) {
		if (dir == USBIP_DIR_OUT)
			return usb_sndbulkpipe(udev, epnum);
		else
			return usb_rcvbulkpipe(udev, epnum);
	}

	if (usb_endpoint_xfer_int(epd)) {
		if (dir == USBIP_DIR_OUT)
			return usb_sndintpipe(udev, epnum);
		else
			return usb_rcvintpipe(udev, epnum);
	}

	if (usb_endpoint_xfer_isoc(epd)) {
		if (dir == USBIP_DIR_OUT)
			return usb_sndisocpipe(udev, epnum);
		else
			return usb_rcvisocpipe(udev, epnum);
	}

	/* NOT REACHED */
	dev_err(&sdev->interface->dev, "get pipe, epnum %d\n", epnum);
	return 0;
}

static void masking_bogus_flags(struct urb *urb)
{
	int				xfertype;
	struct usb_device		*dev;
	struct usb_host_endpoint	*ep;
	int				is_out;
	unsigned int	allowed;

	if (!urb || urb->hcpriv || !urb->complete)
		return;
	dev = urb->dev;
	if ((!dev) || (dev->state < USB_STATE_UNAUTHENTICATED))
		return;

	ep = (usb_pipein(urb->pipe) ? dev->ep_in : dev->ep_out)
		[usb_pipeendpoint(urb->pipe)];
	if (!ep)
		return;

	xfertype = usb_endpoint_type(&ep->desc);
	if (xfertype == USB_ENDPOINT_XFER_CONTROL) {
		struct usb_ctrlrequest *setup =
			(struct usb_ctrlrequest *) urb->setup_packet;

		if (!setup)
			return;
		is_out = !(setup->bRequestType & USB_DIR_IN) ||
			!setup->wLength;
	} else {
		is_out = usb_endpoint_dir_out(&ep->desc);
	}

	/* enforce simple/standard policy */
	allowed = (URB_NO_TRANSFER_DMA_MAP | URB_NO_INTERRUPT |
		   URB_DIR_MASK | URB_FREE_BUFFER);
	switch (xfertype) {
	case USB_ENDPOINT_XFER_BULK:
		if (is_out)
			allowed |= URB_ZERO_PACKET;
		/* FALLTHROUGH */
	case USB_ENDPOINT_XFER_CONTROL:
		allowed |= URB_NO_FSBR;	/* only affects UHCI */
		/* FALLTHROUGH */
	default:			/* all non-iso endpoints */
		if (!is_out)
			allowed |= URB_SHORT_NOT_OK;
		break;
	case USB_ENDPOINT_XFER_ISOC:
		allowed |= URB_ISO_ASAP;
		break;
	}
	urb->transfer_flags &= allowed;
}

static void stub_recv_cmd_submit(struct stub_device *sdev,
				 struct usbip_header *pdu)
{
	int ret;
	struct stub_priv *priv;
	struct usbip_device *ud = &sdev->ud;
	struct usb_device *udev = sdev->udev;
	int pipe = get_pipe(sdev, pdu->base.ep, pdu->base.direction);

	priv = stub_priv_alloc(sdev, pdu);
	if (!priv)
		return;

	/* setup a urb */
	if (usb_pipeisoc(pipe))
		priv->urb = usb_alloc_urb(pdu->u.cmd_submit.number_of_packets,
					  GFP_KERNEL);
	else
		priv->urb = usb_alloc_urb(0, GFP_KERNEL);

	if (!priv->urb) {
		dev_err(&sdev->interface->dev, "malloc urb\n");
		usbip_event_add(ud, SDEV_EVENT_ERROR_MALLOC);
		return;
	}

	/* set priv->urb->transfer_buffer */
	if (pdu->u.cmd_submit.transfer_buffer_length > 0) {
		priv->urb->transfer_buffer =
			kzalloc(pdu->u.cmd_submit.transfer_buffer_length,
				GFP_KERNEL);
		if (!priv->urb->transfer_buffer) {
			dev_err(&sdev->interface->dev, "malloc x_buff\n");
			usbip_event_add(ud, SDEV_EVENT_ERROR_MALLOC);
			return;
		}
	}

	/* set priv->urb->setup_packet */
	priv->urb->setup_packet = kmemdup(&pdu->u.cmd_submit.setup, 8,
					  GFP_KERNEL);
	if (!priv->urb->setup_packet) {
		dev_err(&sdev->interface->dev, "allocate setup_packet\n");
		usbip_event_add(ud, SDEV_EVENT_ERROR_MALLOC);
		return;
	}

	/* set other members from the base header of pdu */
	priv->urb->context                = (void *) priv;
	priv->urb->dev                    = udev;
	priv->urb->pipe                   = pipe;
	priv->urb->complete               = stub_complete;

	usbip_pack_pdu(pdu, priv->urb, USBIP_CMD_SUBMIT, 0);


	if (usbip_recv_xbuff(ud, priv->urb) < 0)
		return;

	if (usbip_recv_iso(ud, priv->urb) < 0)
		return;

	/* no need to submit an intercepted request, but harmless? */
	tweak_special_requests(priv->urb);

	masking_bogus_flags(priv->urb);
	/* urb is now ready to submit */
	ret = usb_submit_urb(priv->urb, GFP_KERNEL);

	if (ret == 0)
		usbip_dbg_stub_rx("submit urb ok, seqnum %u\n",
				  pdu->base.seqnum);
	else {
		dev_err(&sdev->interface->dev, "submit_urb error, %d\n", ret);
		usbip_dump_header(pdu);
		usbip_dump_urb(priv->urb);

		/*
		 * Pessimistic.
		 * This connection will be discarded.
		 */
		usbip_event_add(ud, SDEV_EVENT_ERROR_SUBMIT);
	}

	usbip_dbg_stub_rx("Leave\n");
	return;
}

/* recv a pdu */
static void stub_rx_pdu(struct usbip_device *ud)
{
	int ret;
	struct usbip_header pdu;
	struct stub_device *sdev = container_of(ud, struct stub_device, ud);
	struct device *dev = &sdev->interface->dev;

	usbip_dbg_stub_rx("Enter\n");

	memset(&pdu, 0, sizeof(pdu));

	/* 1. receive a pdu header */
	ret = usbip_xmit(0, ud->tcp_socket, (char *) &pdu, sizeof(pdu), 0);
	if (ret != sizeof(pdu)) {
		dev_err(dev, "recv a header, %d\n", ret);
		usbip_event_add(ud, SDEV_EVENT_ERROR_TCP);
		return;
	}

	usbip_header_correct_endian(&pdu, 0);

	if (usbip_dbg_flag_stub_rx)
		usbip_dump_header(&pdu);

	if (!valid_request(sdev, &pdu)) {
		dev_err(dev, "recv invalid request\n");
		usbip_event_add(ud, SDEV_EVENT_ERROR_TCP);
		return;
	}

	switch (pdu.base.command) {
	case USBIP_CMD_UNLINK:
		stub_recv_cmd_unlink(sdev, &pdu);
		break;

	case USBIP_CMD_SUBMIT:
		stub_recv_cmd_submit(sdev, &pdu);
		break;

	default:
		/* NOTREACHED */
		dev_err(dev, "unknown pdu\n");
		usbip_event_add(ud, SDEV_EVENT_ERROR_TCP);
		break;
	}
}

int stub_rx_loop(void *data)
{
	struct usbip_device *ud = data;

	while (!kthread_should_stop()) {
		if (usbip_event_happened(ud))
			break;

		stub_rx_pdu(ud);
	}

	return 0;
}
