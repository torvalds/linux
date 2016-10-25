/*
 * Copyright (C) 2015 Karol Kosik <karo9@interia.eu>
 * Copyright (C) 2015-2016 Samsung Electronics
 *               Igor Kotrasinski <i.kotrasinsk@samsung.com>
 *               Krzysztof Opasiak <k.opasiak@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/usb.h>
#include <linux/usb/gadget.h>
#include <linux/usb/hcd.h>
#include <linux/kthread.h>
#include <linux/file.h>
#include <linux/byteorder/generic.h>

#include "usbip_common.h"
#include "vudc.h"

#define VIRTUAL_ENDPOINTS (1 /* ep0 */ + 15 /* in eps */ + 15 /* out eps */)

/* urb-related structures alloc / free */


static void free_urb(struct urb *urb)
{
	if (!urb)
		return;

	kfree(urb->setup_packet);
	urb->setup_packet = NULL;

	kfree(urb->transfer_buffer);
	urb->transfer_buffer = NULL;

	usb_free_urb(urb);
}

struct urbp *alloc_urbp(void)
{
	struct urbp *urb_p;

	urb_p = kzalloc(sizeof(*urb_p), GFP_KERNEL);
	if (!urb_p)
		return urb_p;

	urb_p->urb = NULL;
	urb_p->ep = NULL;
	INIT_LIST_HEAD(&urb_p->urb_entry);
	return urb_p;
}

static void free_urbp(struct urbp *urb_p)
{
	kfree(urb_p);
}

void free_urbp_and_urb(struct urbp *urb_p)
{
	if (!urb_p)
		return;
	free_urb(urb_p->urb);
	free_urbp(urb_p);
}


/* utilities ; almost verbatim from dummy_hcd.c */

/* called with spinlock held */
static void nuke(struct vudc *udc, struct vep *ep)
{
	struct vrequest	*req;

	while (!list_empty(&ep->req_queue)) {
		req = list_first_entry(&ep->req_queue, struct vrequest,
				       req_entry);
		list_del_init(&req->req_entry);
		req->req.status = -ESHUTDOWN;

		spin_unlock(&udc->lock);
		usb_gadget_giveback_request(&ep->ep, &req->req);
		spin_lock(&udc->lock);
	}
}

/* caller must hold lock */
static void stop_activity(struct vudc *udc)
{
	int i;
	struct urbp *urb_p, *tmp;

	udc->address = 0;

	for (i = 0; i < VIRTUAL_ENDPOINTS; i++)
		nuke(udc, &udc->ep[i]);

	list_for_each_entry_safe(urb_p, tmp, &udc->urb_queue, urb_entry) {
		list_del(&urb_p->urb_entry);
		free_urbp_and_urb(urb_p);
	}
}

struct vep *vudc_find_endpoint(struct vudc *udc, u8 address)
{
	int i;

	if ((address & ~USB_DIR_IN) == 0)
		return &udc->ep[0];

	for (i = 1; i < VIRTUAL_ENDPOINTS; i++) {
		struct vep *ep = &udc->ep[i];

		if (!ep->desc)
			continue;
		if (ep->desc->bEndpointAddress == address)
			return ep;
	}
	return NULL;
}

/* gadget ops */

/* FIXME - this will probably misbehave when suspend/resume is added */
static int vgadget_get_frame(struct usb_gadget *_gadget)
{
	struct timeval now;
	struct vudc *udc = usb_gadget_to_vudc(_gadget);

	do_gettimeofday(&now);
	return ((now.tv_sec - udc->start_time.tv_sec) * 1000 +
			(now.tv_usec - udc->start_time.tv_usec) / 1000)
			% 0x7FF;
}

static int vgadget_set_selfpowered(struct usb_gadget *_gadget, int value)
{
	struct vudc *udc = usb_gadget_to_vudc(_gadget);

	if (value)
		udc->devstatus |= (1 << USB_DEVICE_SELF_POWERED);
	else
		udc->devstatus &= ~(1 << USB_DEVICE_SELF_POWERED);
	return 0;
}

static int vgadget_pullup(struct usb_gadget *_gadget, int value)
{
	struct vudc *udc = usb_gadget_to_vudc(_gadget);
	unsigned long flags;
	int ret;


	spin_lock_irqsave(&udc->lock, flags);
	value = !!value;
	if (value == udc->pullup)
		goto unlock;

	udc->pullup = value;
	if (value) {
		udc->gadget.speed = min_t(u8, USB_SPEED_HIGH,
					   udc->driver->max_speed);
		udc->ep[0].ep.maxpacket = 64;
		/*
		 * This is the first place where we can ask our
		 * gadget driver for descriptors.
		 */
		ret = get_gadget_descs(udc);
		if (ret) {
			dev_err(&udc->gadget.dev, "Unable go get desc: %d", ret);
			goto unlock;
		}

		spin_unlock_irqrestore(&udc->lock, flags);
		usbip_start_eh(&udc->ud);
	} else {
		/* Invalidate descriptors */
		udc->desc_cached = 0;

		spin_unlock_irqrestore(&udc->lock, flags);
		usbip_event_add(&udc->ud, VUDC_EVENT_REMOVED);
		usbip_stop_eh(&udc->ud); /* Wait for eh completion */
	}

	return 0;

unlock:
	spin_unlock_irqrestore(&udc->lock, flags);
	return 0;
}

static int vgadget_udc_start(struct usb_gadget *g,
		struct usb_gadget_driver *driver)
{
	struct vudc *udc = usb_gadget_to_vudc(g);
	unsigned long flags;

	spin_lock_irqsave(&udc->lock, flags);
	udc->driver = driver;
	udc->pullup = udc->connected = udc->desc_cached = 0;
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static int vgadget_udc_stop(struct usb_gadget *g)
{
	struct vudc *udc = usb_gadget_to_vudc(g);
	unsigned long flags;

	spin_lock_irqsave(&udc->lock, flags);
	udc->driver = NULL;
	spin_unlock_irqrestore(&udc->lock, flags);
	return 0;
}

static const struct usb_gadget_ops vgadget_ops = {
	.get_frame	= vgadget_get_frame,
	.set_selfpowered = vgadget_set_selfpowered,
	.pullup		= vgadget_pullup,
	.udc_start	= vgadget_udc_start,
	.udc_stop	= vgadget_udc_stop,
};


/* endpoint ops */

static int vep_enable(struct usb_ep *_ep,
		const struct usb_endpoint_descriptor *desc)
{
	struct vep	*ep;
	struct vudc	*udc;
	unsigned int	maxp;
	unsigned long	flags;

	ep = to_vep(_ep);
	udc = ep_to_vudc(ep);

	if (!_ep || !desc || ep->desc || _ep->caps.type_control
			|| desc->bDescriptorType != USB_DT_ENDPOINT)
		return -EINVAL;

	if (!udc->driver)
		return -ESHUTDOWN;

	spin_lock_irqsave(&udc->lock, flags);

	maxp = usb_endpoint_maxp(desc) & 0x7ff;
	_ep->maxpacket = maxp;
	ep->desc = desc;
	ep->type = usb_endpoint_type(desc);
	ep->halted = ep->wedged = 0;

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static int vep_disable(struct usb_ep *_ep)
{
	struct vep *ep;
	struct vudc *udc;
	unsigned long flags;

	ep = to_vep(_ep);
	udc = ep_to_vudc(ep);
	if (!_ep || !ep->desc || _ep->caps.type_control)
		return -EINVAL;

	spin_lock_irqsave(&udc->lock, flags);
	ep->desc = NULL;
	nuke(udc, ep);
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static struct usb_request *vep_alloc_request(struct usb_ep *_ep,
		gfp_t mem_flags)
{
	struct vep *ep;
	struct vrequest *req;

	if (!_ep)
		return NULL;
	ep = to_vep(_ep);

	req = kzalloc(sizeof(*req), mem_flags);
	if (!req)
		return NULL;

	INIT_LIST_HEAD(&req->req_entry);

	return &req->req;
}

static void vep_free_request(struct usb_ep *_ep, struct usb_request *_req)
{
	struct vrequest *req;

	if (WARN_ON(!_ep || !_req))
		return;

	req = to_vrequest(_req);
	kfree(req);
}

static int vep_queue(struct usb_ep *_ep, struct usb_request *_req,
		gfp_t mem_flags)
{
	struct vep *ep;
	struct vrequest *req;
	struct vudc *udc;
	unsigned long flags;

	if (!_ep || !_req)
		return -EINVAL;

	ep = to_vep(_ep);
	req = to_vrequest(_req);
	udc = ep_to_vudc(ep);

	spin_lock_irqsave(&udc->lock, flags);
	_req->actual = 0;
	_req->status = -EINPROGRESS;

	list_add_tail(&req->req_entry, &ep->req_queue);
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static int vep_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct vep *ep;
	struct vrequest *req;
	struct vudc *udc;
	struct vrequest *lst;
	unsigned long flags;
	int ret = -EINVAL;

	if (!_ep || !_req)
		return ret;

	ep = to_vep(_ep);
	req = to_vrequest(_req);
	udc = req->udc;

	if (!udc->driver)
		return -ESHUTDOWN;

	spin_lock_irqsave(&udc->lock, flags);
	list_for_each_entry(lst, &ep->req_queue, req_entry) {
		if (&lst->req == _req) {
			list_del_init(&lst->req_entry);
			_req->status = -ECONNRESET;
			ret = 0;
			break;
		}
	}
	spin_unlock_irqrestore(&udc->lock, flags);

	if (ret == 0)
		usb_gadget_giveback_request(_ep, _req);

	return ret;
}

static int
vep_set_halt_and_wedge(struct usb_ep *_ep, int value, int wedged)
{
	struct vep *ep;
	struct vudc *udc;
	unsigned long flags;
	int ret = 0;

	ep = to_vep(_ep);
	if (!_ep)
		return -EINVAL;

	udc = ep_to_vudc(ep);
	if (!udc->driver)
		return -ESHUTDOWN;

	spin_lock_irqsave(&udc->lock, flags);
	if (!value)
		ep->halted = ep->wedged = 0;
	else if (ep->desc && (ep->desc->bEndpointAddress & USB_DIR_IN) &&
			!list_empty(&ep->req_queue))
		ret = -EAGAIN;
	else {
		ep->halted = 1;
		if (wedged)
			ep->wedged = 1;
	}

	spin_unlock_irqrestore(&udc->lock, flags);
	return ret;
}

static int
vep_set_halt(struct usb_ep *_ep, int value)
{
	return vep_set_halt_and_wedge(_ep, value, 0);
}

static int vep_set_wedge(struct usb_ep *_ep)
{
	return vep_set_halt_and_wedge(_ep, 1, 1);
}

static const struct usb_ep_ops vep_ops = {
	.enable		= vep_enable,
	.disable	= vep_disable,

	.alloc_request	= vep_alloc_request,
	.free_request	= vep_free_request,

	.queue		= vep_queue,
	.dequeue	= vep_dequeue,

	.set_halt	= vep_set_halt,
	.set_wedge	= vep_set_wedge,
};


/* shutdown / reset / error handlers */

static void vudc_shutdown(struct usbip_device *ud)
{
	struct vudc *udc = container_of(ud, struct vudc, ud);
	int call_disconnect = 0;
	unsigned long flags;

	dev_dbg(&udc->pdev->dev, "device shutdown");
	if (ud->tcp_socket)
		kernel_sock_shutdown(ud->tcp_socket, SHUT_RDWR);

	if (ud->tcp_rx) {
		kthread_stop_put(ud->tcp_rx);
		ud->tcp_rx = NULL;
	}
	if (ud->tcp_tx) {
		kthread_stop_put(ud->tcp_tx);
		ud->tcp_tx = NULL;
	}

	if (ud->tcp_socket) {
		sockfd_put(ud->tcp_socket);
		ud->tcp_socket = NULL;
	}

	spin_lock_irqsave(&udc->lock, flags);
	stop_activity(udc);
	if (udc->connected && udc->driver->disconnect)
		call_disconnect = 1;
	udc->connected = 0;
	spin_unlock_irqrestore(&udc->lock, flags);
	if (call_disconnect)
		udc->driver->disconnect(&udc->gadget);
}

static void vudc_device_reset(struct usbip_device *ud)
{
	struct vudc *udc = container_of(ud, struct vudc, ud);
	unsigned long flags;

	dev_dbg(&udc->pdev->dev, "device reset");
	spin_lock_irqsave(&udc->lock, flags);
	stop_activity(udc);
	spin_unlock_irqrestore(&udc->lock, flags);
	if (udc->driver)
		usb_gadget_udc_reset(&udc->gadget, udc->driver);
	spin_lock_irqsave(&ud->lock, flags);
	ud->status = SDEV_ST_AVAILABLE;
	spin_unlock_irqrestore(&ud->lock, flags);
}

static void vudc_device_unusable(struct usbip_device *ud)
{
	unsigned long flags;

	spin_lock_irqsave(&ud->lock, flags);
	ud->status = SDEV_ST_ERROR;
	spin_unlock_irqrestore(&ud->lock, flags);
}

/* device setup / cleanup */

struct vudc_device *alloc_vudc_device(int devid)
{
	struct vudc_device *udc_dev = NULL;

	udc_dev = kzalloc(sizeof(*udc_dev), GFP_KERNEL);
	if (!udc_dev)
		goto out;

	INIT_LIST_HEAD(&udc_dev->dev_entry);

	udc_dev->pdev = platform_device_alloc(GADGET_NAME, devid);
	if (!udc_dev->pdev) {
		kfree(udc_dev);
		udc_dev = NULL;
	}

out:
	return udc_dev;
}

void put_vudc_device(struct vudc_device *udc_dev)
{
	platform_device_put(udc_dev->pdev);
	kfree(udc_dev);
}

static int init_vudc_hw(struct vudc *udc)
{
	int i;
	struct usbip_device *ud = &udc->ud;
	struct vep *ep;

	udc->ep = kcalloc(VIRTUAL_ENDPOINTS, sizeof(*udc->ep), GFP_KERNEL);
	if (!udc->ep)
		goto nomem_ep;

	INIT_LIST_HEAD(&udc->gadget.ep_list);

	/* create ep0 and 15 in, 15 out general purpose eps */
	for (i = 0; i < VIRTUAL_ENDPOINTS; ++i) {
		int is_out = i % 2;
		int num = (i + 1) / 2;

		ep = &udc->ep[i];

		sprintf(ep->name, "ep%d%s", num,
			i ? (is_out ? "out" : "in") : "");
		ep->ep.name = ep->name;
		if (i == 0) {
			ep->ep.caps.type_control = true;
			ep->ep.caps.dir_out = true;
			ep->ep.caps.dir_in = true;
		} else {
			ep->ep.caps.type_iso = true;
			ep->ep.caps.type_int = true;
			ep->ep.caps.type_bulk = true;
		}

		if (is_out)
			ep->ep.caps.dir_out = true;
		else
			ep->ep.caps.dir_in = true;

		ep->ep.ops = &vep_ops;
		list_add_tail(&ep->ep.ep_list, &udc->gadget.ep_list);
		ep->halted = ep->wedged = ep->already_seen =
			ep->setup_stage = 0;
		usb_ep_set_maxpacket_limit(&ep->ep, ~0);
		ep->ep.max_streams = 16;
		ep->gadget = &udc->gadget;
		ep->desc = NULL;
		INIT_LIST_HEAD(&ep->req_queue);
	}

	spin_lock_init(&udc->lock);
	spin_lock_init(&udc->lock_tx);
	INIT_LIST_HEAD(&udc->urb_queue);
	INIT_LIST_HEAD(&udc->tx_queue);
	init_waitqueue_head(&udc->tx_waitq);

	spin_lock_init(&ud->lock);
	ud->status = SDEV_ST_AVAILABLE;
	ud->side = USBIP_VUDC;

	ud->eh_ops.shutdown = vudc_shutdown;
	ud->eh_ops.reset    = vudc_device_reset;
	ud->eh_ops.unusable = vudc_device_unusable;

	udc->gadget.ep0 = &udc->ep[0].ep;
	list_del_init(&udc->ep[0].ep.ep_list);

	v_init_timer(udc);
	return 0;

nomem_ep:
		return -ENOMEM;
}

static void cleanup_vudc_hw(struct vudc *udc)
{
	kfree(udc->ep);
}

/* platform driver ops */

int vudc_probe(struct platform_device *pdev)
{
	struct vudc *udc;
	int ret = -ENOMEM;

	udc = kzalloc(sizeof(*udc), GFP_KERNEL);
	if (!udc)
		goto out;

	udc->gadget.name = GADGET_NAME;
	udc->gadget.ops = &vgadget_ops;
	udc->gadget.max_speed = USB_SPEED_HIGH;
	udc->gadget.dev.parent = &pdev->dev;
	udc->pdev = pdev;

	ret = init_vudc_hw(udc);
	if (ret)
		goto err_init_vudc_hw;

	ret = usb_add_gadget_udc(&pdev->dev, &udc->gadget);
	if (ret < 0)
		goto err_add_udc;

	ret = sysfs_create_group(&pdev->dev.kobj, &vudc_attr_group);
	if (ret) {
		dev_err(&udc->pdev->dev, "create sysfs files\n");
		goto err_sysfs;
	}

	platform_set_drvdata(pdev, udc);

	return ret;

err_sysfs:
	usb_del_gadget_udc(&udc->gadget);
err_add_udc:
	cleanup_vudc_hw(udc);
err_init_vudc_hw:
	kfree(udc);
out:
	return ret;
}

int vudc_remove(struct platform_device *pdev)
{
	struct vudc *udc = platform_get_drvdata(pdev);

	sysfs_remove_group(&pdev->dev.kobj, &vudc_attr_group);
	usb_del_gadget_udc(&udc->gadget);
	cleanup_vudc_hw(udc);
	kfree(udc);
	return 0;
}
