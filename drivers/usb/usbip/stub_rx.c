// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2003-2008 Takahiro Hirofuchi
 */

#include <asm/byteorder.h>
#include <linux/kthread.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/scatterlist.h>

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
		dev_err(&urb->dev->dev,
			"usb_clear_halt error: devnum %d endp %d ret %d\n",
			urb->dev->devnum, target_endp, ret);
	else
		dev_info(&urb->dev->dev,
			 "usb_clear_halt done: devnum %d endp %d\n",
			 urb->dev->devnum, target_endp);

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
		dev_err(&urb->dev->dev,
			"usb_set_interface error: inf %u alt %u ret %d\n",
			interface, alternate, ret);
	else
		dev_info(&urb->dev->dev,
			"usb_set_interface done: inf %u alt %u\n",
			interface, alternate);

	return ret;
}

static int tweak_set_configuration_cmd(struct urb *urb)
{
	struct stub_priv *priv = (struct stub_priv *) urb->context;
	struct stub_device *sdev = priv->sdev;
	struct usb_ctrlrequest *req;
	__u16 config;
	int err;

	req = (struct usb_ctrlrequest *) urb->setup_packet;
	config = le16_to_cpu(req->wValue);

	err = usb_set_configuration(sdev->udev, config);
	if (err && err != -ENODEV)
		dev_err(&sdev->udev->dev, "can't set config #%d, error %d\n",
			config, err);
	return 0;
}

static int tweak_reset_device_cmd(struct urb *urb)
{
	struct stub_priv *priv = (struct stub_priv *) urb->context;
	struct stub_device *sdev = priv->sdev;

	dev_info(&urb->dev->dev, "usb_queue_reset_device\n");

	if (usb_lock_device_for_reset(sdev->udev, NULL) < 0) {
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
	int ret, i;
	unsigned long flags;
	struct stub_priv *priv;

	spin_lock_irqsave(&sdev->priv_lock, flags);

	list_for_each_entry(priv, &sdev->priv_init, list) {
		if (priv->seqnum != pdu->u.cmd_unlink.seqnum)
			continue;

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
		for (i = priv->completed_urbs; i < priv->num_urbs; i++) {
			ret = usb_unlink_urb(priv->urbs[i]);
			if (ret != -EINPROGRESS)
				dev_err(&priv->urbs[i]->dev->dev,
					"failed to unlink %d/%d urb of seqnum %lu, ret %d\n",
					i + 1, priv->num_urbs,
					priv->seqnum, ret);
		}
		return 0;
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
		spin_lock_irq(&ud->lock);
		if (ud->status == SDEV_ST_USED) {
			/* A request is valid. */
			valid = 1;
		}
		spin_unlock_irq(&ud->lock);
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
		dev_err(&sdev->udev->dev, "alloc stub_priv\n");
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

static int get_pipe(struct stub_device *sdev, struct usbip_header *pdu)
{
	struct usb_device *udev = sdev->udev;
	struct usb_host_endpoint *ep;
	struct usb_endpoint_descriptor *epd = NULL;
	int epnum = pdu->base.ep;
	int dir = pdu->base.direction;

	if (epnum < 0 || epnum > 15)
		goto err_ret;

	if (dir == USBIP_DIR_IN)
		ep = udev->ep_in[epnum & 0x7f];
	else
		ep = udev->ep_out[epnum & 0x7f];
	if (!ep)
		goto err_ret;

	epd = &ep->desc;

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
		/* validate number of packets */
		if (pdu->u.cmd_submit.number_of_packets < 0 ||
		    pdu->u.cmd_submit.number_of_packets >
		    USBIP_MAX_ISO_PACKETS) {
			dev_err(&sdev->udev->dev,
				"CMD_SUBMIT: isoc invalid num packets %d\n",
				pdu->u.cmd_submit.number_of_packets);
			return -1;
		}
		if (dir == USBIP_DIR_OUT)
			return usb_sndisocpipe(udev, epnum);
		else
			return usb_rcvisocpipe(udev, epnum);
	}

err_ret:
	/* NOT REACHED */
	dev_err(&sdev->udev->dev, "CMD_SUBMIT: invalid epnum %d\n", epnum);
	return -1;
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

static int stub_recv_xbuff(struct usbip_device *ud, struct stub_priv *priv)
{
	int ret;
	int i;

	for (i = 0; i < priv->num_urbs; i++) {
		ret = usbip_recv_xbuff(ud, priv->urbs[i]);
		if (ret < 0)
			break;
	}

	return ret;
}

static void stub_recv_cmd_submit(struct stub_device *sdev,
				 struct usbip_header *pdu)
{
	struct stub_priv *priv;
	struct usbip_device *ud = &sdev->ud;
	struct usb_device *udev = sdev->udev;
	struct scatterlist *sgl = NULL, *sg;
	void *buffer = NULL;
	unsigned long long buf_len;
	int nents;
	int num_urbs = 1;
	int pipe = get_pipe(sdev, pdu);
	int use_sg = pdu->u.cmd_submit.transfer_flags & URB_DMA_MAP_SG;
	int support_sg = 1;
	int np = 0;
	int ret, i;

	if (pipe == -1)
		return;

	priv = stub_priv_alloc(sdev, pdu);
	if (!priv)
		return;

	buf_len = (unsigned long long)pdu->u.cmd_submit.transfer_buffer_length;

	/* allocate urb transfer buffer, if needed */
	if (buf_len) {
		if (use_sg) {
			sgl = sgl_alloc(buf_len, GFP_KERNEL, &nents);
			if (!sgl)
				goto err_malloc;
		} else {
			buffer = kzalloc(buf_len, GFP_KERNEL);
			if (!buffer)
				goto err_malloc;
		}
	}

	/* Check if the server's HCD supports SG */
	if (use_sg && !udev->bus->sg_tablesize) {
		/*
		 * If the server's HCD doesn't support SG, break a single SG
		 * request into several URBs and map each SG list entry to
		 * corresponding URB buffer. The previously allocated SG
		 * list is stored in priv->sgl (If the server's HCD support SG,
		 * SG list is stored only in urb->sg) and it is used as an
		 * indicator that the server split single SG request into
		 * several URBs. Later, priv->sgl is used by stub_complete() and
		 * stub_send_ret_submit() to reassemble the divied URBs.
		 */
		support_sg = 0;
		num_urbs = nents;
		priv->completed_urbs = 0;
		pdu->u.cmd_submit.transfer_flags &= ~URB_DMA_MAP_SG;
	}

	/* allocate urb array */
	priv->num_urbs = num_urbs;
	priv->urbs = kmalloc_array(num_urbs, sizeof(*priv->urbs), GFP_KERNEL);
	if (!priv->urbs)
		goto err_urbs;

	/* setup a urb */
	if (support_sg) {
		if (usb_pipeisoc(pipe))
			np = pdu->u.cmd_submit.number_of_packets;

		priv->urbs[0] = usb_alloc_urb(np, GFP_KERNEL);
		if (!priv->urbs[0])
			goto err_urb;

		if (buf_len) {
			if (use_sg) {
				priv->urbs[0]->sg = sgl;
				priv->urbs[0]->num_sgs = nents;
				priv->urbs[0]->transfer_buffer = NULL;
			} else {
				priv->urbs[0]->transfer_buffer = buffer;
			}
		}

		/* copy urb setup packet */
		priv->urbs[0]->setup_packet = kmemdup(&pdu->u.cmd_submit.setup,
					8, GFP_KERNEL);
		if (!priv->urbs[0]->setup_packet) {
			usbip_event_add(ud, SDEV_EVENT_ERROR_MALLOC);
			return;
		}

		usbip_pack_pdu(pdu, priv->urbs[0], USBIP_CMD_SUBMIT, 0);
	} else {
		for_each_sg(sgl, sg, nents, i) {
			priv->urbs[i] = usb_alloc_urb(0, GFP_KERNEL);
			/* The URBs which is previously allocated will be freed
			 * in stub_device_cleanup_urbs() if error occurs.
			 */
			if (!priv->urbs[i])
				goto err_urb;

			usbip_pack_pdu(pdu, priv->urbs[i], USBIP_CMD_SUBMIT, 0);
			priv->urbs[i]->transfer_buffer = sg_virt(sg);
			priv->urbs[i]->transfer_buffer_length = sg->length;
		}
		priv->sgl = sgl;
	}

	for (i = 0; i < num_urbs; i++) {
		/* set other members from the base header of pdu */
		priv->urbs[i]->context = (void *) priv;
		priv->urbs[i]->dev = udev;
		priv->urbs[i]->pipe = pipe;
		priv->urbs[i]->complete = stub_complete;

		/* no need to submit an intercepted request, but harmless? */
		tweak_special_requests(priv->urbs[i]);

		masking_bogus_flags(priv->urbs[i]);
	}

	if (stub_recv_xbuff(ud, priv) < 0)
		return;

	if (usbip_recv_iso(ud, priv->urbs[0]) < 0)
		return;

	/* urb is now ready to submit */
	for (i = 0; i < priv->num_urbs; i++) {
		ret = usb_submit_urb(priv->urbs[i], GFP_KERNEL);

		if (ret == 0)
			usbip_dbg_stub_rx("submit urb ok, seqnum %u\n",
					pdu->base.seqnum);
		else {
			dev_err(&udev->dev, "submit_urb error, %d\n", ret);
			usbip_dump_header(pdu);
			usbip_dump_urb(priv->urbs[i]);

			/*
			 * Pessimistic.
			 * This connection will be discarded.
			 */
			usbip_event_add(ud, SDEV_EVENT_ERROR_SUBMIT);
			break;
		}
	}

	usbip_dbg_stub_rx("Leave\n");
	return;

err_urb:
	kfree(priv->urbs);
err_urbs:
	kfree(buffer);
	sgl_free(sgl);
err_malloc:
	usbip_event_add(ud, SDEV_EVENT_ERROR_MALLOC);
}

/* recv a pdu */
static void stub_rx_pdu(struct usbip_device *ud)
{
	int ret;
	struct usbip_header pdu;
	struct stub_device *sdev = container_of(ud, struct stub_device, ud);
	struct device *dev = &sdev->udev->dev;

	usbip_dbg_stub_rx("Enter\n");

	memset(&pdu, 0, sizeof(pdu));

	/* receive a pdu header */
	ret = usbip_recv(ud->tcp_socket, &pdu, sizeof(pdu));
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
