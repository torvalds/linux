// SPDX-License-Identifier: GPL-2.0+
/*
 * USB driver for Nintendo Switch 2 controllers configuration interface
 *
 * Copyright (c) 2025 Valve Software
 *
 * This driver is based on the following work:
 *   https://gist.github.com/shinyquagsire23/66f006b46c56216acbaac6c1e2279b64
 *   https://github.com/ndeadly/switch2_controller_research
 */

#include "hid-ids.h"
#include "hid-nintendo.h"
#include <linux/module.h>
#include <linux/usb/input.h>

#define NS2_BULK_SIZE 64
#define NS2_IN_URBS 2
#define NS2_OUT_URBS 4

static struct usb_driver switch2_usb;

enum switch2_urb_state {
	NS2_URB_FREE,
	NS2_URB_OUT,
	NS2_URB_IN,
};

struct switch2_urb {
	struct urb *urb;
	uint8_t *data;
	enum switch2_urb_state state;
};

struct switch2_usb {
	struct switch2_cfg_intf cfg;
	struct usb_device *udev;

	struct switch2_urb bulk_in[NS2_IN_URBS];
	struct usb_anchor bulk_in_anchor;
	bool shutdown;
	spinlock_t bulk_in_lock;

	struct switch2_urb bulk_out[NS2_OUT_URBS];
	struct usb_anchor bulk_out_anchor;
	spinlock_t bulk_out_lock;

	struct work_struct message_in_work;
};

static void switch2_bulk_in(struct urb *urb)
{
	struct switch2_usb *ns2_usb = urb->context;
	int i;
	bool schedule = false;
	unsigned long flags;

	switch (urb->status) {
	case 0:
		schedule = true;
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
	case -EPIPE:
		dev_dbg(&ns2_usb->udev->dev, "shutting down input urb: %d\n", urb->status);
		return;
	default:
		dev_dbg(&ns2_usb->udev->dev, "unknown input urb status: %d\n", urb->status);
		break;
	}

	spin_lock_irqsave(&ns2_usb->bulk_in_lock, flags);
	if (ns2_usb->shutdown)
		schedule = false;

	for (i = 0; i < NS2_IN_URBS; i++) {
		int err;
		struct switch2_urb *ns2_urb;

		if (ns2_usb->bulk_in[i].urb == urb) {
			if (schedule) {
				ns2_usb->bulk_in[i].state = NS2_URB_IN;
				continue;
			} else {
				ns2_usb->bulk_in[i].state = NS2_URB_FREE;
			}
		}

		if (ns2_usb->bulk_in[i].state != NS2_URB_FREE)
			continue;

		if (ns2_usb->shutdown)
			continue;

		/*
		 * We want exactly one bulk in URB scheduled at a time, so only
		 * reschedule this immediately if nothing else is scheduled
		 * currently.
		 */
		if (!usb_anchor_empty(&ns2_usb->bulk_in_anchor))
			continue;

		ns2_urb = &ns2_usb->bulk_in[i];
		if (!ns2_urb)
			continue;

		usb_anchor_urb(ns2_urb->urb, &ns2_usb->bulk_in_anchor);
		err = usb_submit_urb(ns2_urb->urb, GFP_ATOMIC);
		if (err) {
			usb_unanchor_urb(ns2_urb->urb);
			dev_dbg(&ns2_usb->udev->dev, "failed to queue input urb: %d\n", err);
		} else {
			ns2_urb->state = NS2_URB_OUT;
		}
	}
	spin_unlock_irqrestore(&ns2_usb->bulk_in_lock, flags);

	if (schedule)
		schedule_work(&ns2_usb->message_in_work);
}

static void switch2_bulk_out(struct urb *urb)
{
	struct switch2_usb *ns2_usb = urb->context;
	int i;

	guard(spinlock_irqsave)(&ns2_usb->bulk_out_lock);

	switch (urb->status) {
	case 0:
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		dev_dbg(&ns2_usb->udev->dev, "shutting down output urb: %d\n", urb->status);
		return;
	case -EPIPE:
		break;
	default:
		dev_dbg(&ns2_usb->udev->dev, "unknown output urb status: %d\n", urb->status);
		break;
	}

	for (i = 0; i < NS2_OUT_URBS; i++) {
		if (ns2_usb->bulk_out[i].urb != urb)
			continue;

		ns2_usb->bulk_out[i].state = NS2_URB_FREE;
		break;
	}
}

static int switch2_usb_send_cmd(enum switch2_cmd command, uint8_t subcommand,
	const void *message, size_t size, struct switch2_cfg_intf *cfg)
{
	struct switch2_usb *ns2_usb = (struct switch2_usb *)cfg;
	struct switch2_urb *urb = NULL;
	int i;
	int ret;
	unsigned long flags;

	struct switch2_cmd_header header = {
		command, NS2_DIR_OUT | NS2_FLAG_OK, NS2_TRANS_USB, subcommand, 0, size
	};

	if (WARN_ON(size > 56))
		return -EINVAL;

	spin_lock_irqsave(&ns2_usb->bulk_out_lock, flags);
	for (i = 0; i < NS2_OUT_URBS; i++) {
		if (ns2_usb->bulk_out[i].state != NS2_URB_FREE)
			continue;

		urb = &ns2_usb->bulk_out[i];
		urb->state = NS2_URB_OUT;
		break;
	}
	spin_unlock_irqrestore(&ns2_usb->bulk_out_lock, flags);

	if (!urb) {
		dev_warn(&ns2_usb->udev->dev, "output queue full, dropping message\n");
		return -ENOBUFS;
	}

	memcpy(urb->data, &header, sizeof(header));
	if (message && size)
		memcpy(&urb->data[8], message, size);
	urb->urb->transfer_buffer_length = size + sizeof(header);

	print_hex_dump_debug("sending cmd: ", DUMP_PREFIX_OFFSET, 16, 1, urb->data,
		size + sizeof(header), false);

	usb_anchor_urb(urb->urb, &ns2_usb->bulk_out_anchor);
	ret = usb_submit_urb(urb->urb, GFP_KERNEL);
	if (ret) {
		if (ret != -ENODEV)
			dev_warn(&ns2_usb->udev->dev, "failed to submit output urb: %i", ret);
		spin_lock_irqsave(&ns2_usb->bulk_out_lock, flags);
		urb->state = NS2_URB_FREE;
		spin_unlock_irqrestore(&ns2_usb->bulk_out_lock, flags);
		usb_unanchor_urb(urb->urb);
		return ret;
	}

	return 0;
}

static void switch2_usb_free_in_urbs(struct switch2_usb *ns2_usb)
{
	struct urb *urb;
	uint8_t *data;
	int i;
	unsigned long flags;

	spin_lock_irqsave(&ns2_usb->bulk_in_lock, flags);
	ns2_usb->shutdown = true;
	spin_unlock_irqrestore(&ns2_usb->bulk_in_lock, flags);

	cancel_work_sync(&ns2_usb->message_in_work);

	usb_kill_anchored_urbs(&ns2_usb->bulk_in_anchor);
	for (i = 0; i < NS2_IN_URBS; i++) {
		spin_lock_irqsave(&ns2_usb->bulk_in_lock, flags);
		urb = ns2_usb->bulk_in[i].urb;
		data = ns2_usb->bulk_in[i].data;
		if (!urb) {
			spin_unlock_irqrestore(&ns2_usb->bulk_in_lock, flags);
			continue;
		}

		ns2_usb->bulk_in[i].urb = NULL;
		ns2_usb->bulk_in[i].data = NULL;
		spin_unlock_irqrestore(&ns2_usb->bulk_in_lock, flags);

		usb_kill_urb(urb);
		usb_free_coherent(ns2_usb->udev, NS2_BULK_SIZE, data, urb->transfer_dma);
		usb_free_urb(urb);
	}
}

static void switch2_usb_free_out_urbs(struct switch2_usb *ns2_usb)
{
	struct urb *urb;
	uint8_t *data;
	int i;
	unsigned long flags;

	usb_kill_anchored_urbs(&ns2_usb->bulk_out_anchor);
	for (i = 0; i < NS2_OUT_URBS; i++) {
		spin_lock_irqsave(&ns2_usb->bulk_out_lock, flags);
		urb = ns2_usb->bulk_out[i].urb;
		data = ns2_usb->bulk_out[i].data;
		if (!urb) {
			spin_unlock_irqrestore(&ns2_usb->bulk_out_lock, flags);
			continue;
		}

		ns2_usb->bulk_out[i].urb = NULL;
		ns2_usb->bulk_out[i].data = NULL;
		spin_unlock_irqrestore(&ns2_usb->bulk_out_lock, flags);

		usb_kill_urb(urb);
		usb_free_coherent(ns2_usb->udev, NS2_BULK_SIZE, data, urb->transfer_dma);
		usb_free_urb(urb);
	}
}

static void switch2_usb_shutdown(struct switch2_cfg_intf *intf)
{
	struct switch2_usb *ns2_usb = (struct switch2_usb *)intf;
	int i;
	unsigned long flags;

	spin_lock_irqsave(&ns2_usb->bulk_in_lock, flags);
	ns2_usb->shutdown = true;
	spin_unlock_irqrestore(&ns2_usb->bulk_in_lock, flags);

	cancel_work_sync(&ns2_usb->message_in_work);

	usb_kill_anchored_urbs(&ns2_usb->bulk_in_anchor);
	spin_lock_irqsave(&ns2_usb->bulk_in_lock, flags);
	for (i = 0; i < NS2_IN_URBS; i++)
		ns2_usb->bulk_in[i].state = NS2_URB_FREE;
	spin_unlock_irqrestore(&ns2_usb->bulk_in_lock, flags);
}

static void switch2_usb_message_in_work(struct work_struct *work)
{
	struct switch2_usb *ns2_usb = container_of(work, struct switch2_usb, message_in_work);
	struct switch2_urb *urb;
	int err;
	int i;
	unsigned long flags;

	spin_lock_irqsave(&ns2_usb->bulk_in_lock, flags);
	for (i = 0; i < NS2_IN_URBS; i++) {
		urb = &ns2_usb->bulk_in[i];
		if (urb->state != NS2_URB_IN)
			continue;
		spin_unlock_irqrestore(&ns2_usb->bulk_in_lock, flags);

		if (ns2_usb->cfg.parent) {
			err = switch2_receive_command(ns2_usb->cfg.parent,
				urb->urb->transfer_buffer, urb->urb->actual_length);
			if (err)
				dev_dbg(&ns2_usb->udev->dev, "receive command failed: %d\n", err);
		} else {
			dev_err(&ns2_usb->udev->dev,
				"Got message before controller is fully set up; discarding\n");
		}

		spin_lock_irqsave(&ns2_usb->bulk_in_lock, flags);
		if (!urb->urb)
			/* We're shutting down and this URB was freed */
			break;

		urb->state = NS2_URB_FREE;
		/*
		 * We want exactly one bulk in URB scheduled at a time, so only
		 * reschedule this immediately if nothing else is scheduled
		 * currently.
		 */
		if (!usb_anchor_empty(&ns2_usb->bulk_in_anchor))
			continue;

		usb_anchor_urb(urb->urb, &ns2_usb->bulk_in_anchor);
		err = usb_submit_urb(urb->urb, GFP_ATOMIC);
		if (err) {
			usb_unanchor_urb(urb->urb);
			dev_dbg(&ns2_usb->udev->dev,
				"failed to queue input urb: %d\n", err);
		} else {
			urb->state = NS2_URB_OUT;
		}
	}
	spin_unlock_irqrestore(&ns2_usb->bulk_in_lock, flags);
}

static int switch2_usb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct switch2_usb *ns2_usb;
	struct usb_device *udev;
	struct usb_endpoint_descriptor *bulk_in, *bulk_out;
	struct urb *urb;
	uint8_t *data;
	char phys[64];
	int ret;
	int i;
	unsigned long flags;

	udev = interface_to_usbdev(intf);
	if (usb_make_path(udev, phys, sizeof(phys)) < 0)
		return -EINVAL;

	ret = usb_find_common_endpoints(intf->cur_altsetting, &bulk_in, &bulk_out, NULL, NULL);
	if (ret) {
		dev_err(&intf->dev, "failed to find bulk EPs\n");
		return ret;
	}

	ns2_usb = devm_kzalloc(&intf->dev, sizeof(*ns2_usb), GFP_KERNEL);
	if (!ns2_usb)
		return -ENOMEM;

	init_usb_anchor(&ns2_usb->bulk_out_anchor);
	spin_lock_init(&ns2_usb->bulk_out_lock);
	init_usb_anchor(&ns2_usb->bulk_in_anchor);
	spin_lock_init(&ns2_usb->bulk_in_lock);
	INIT_WORK(&ns2_usb->message_in_work, switch2_usb_message_in_work);

	ns2_usb->udev = udev;
	for (i = 0; i < NS2_IN_URBS; i++) {
		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			ret = -ENOMEM;
			goto err_free_in;
		}

		data = usb_alloc_coherent(udev, NS2_BULK_SIZE, GFP_KERNEL,
			&urb->transfer_dma);
		if (!data) {
			usb_free_urb(urb);
			ret = -ENOMEM;
			goto err_free_in;
		}

		spin_lock_irqsave(&ns2_usb->bulk_in_lock, flags);
		usb_fill_bulk_urb(urb, udev,
			usb_rcvbulkpipe(udev, bulk_in->bEndpointAddress),
			data, NS2_BULK_SIZE, switch2_bulk_in, ns2_usb);
		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

		ns2_usb->bulk_in[i].urb = urb;
		ns2_usb->bulk_in[i].data = data;
		spin_unlock_irqrestore(&ns2_usb->bulk_in_lock, flags);
	}

	for (i = 0; i < NS2_OUT_URBS; i++) {
		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			ret = -ENOMEM;
			goto err_free_out;
		}

		data = usb_alloc_coherent(udev, NS2_BULK_SIZE, GFP_KERNEL,
			&urb->transfer_dma);
		if (!data) {
			usb_free_urb(urb);
			ret = -ENOMEM;
			goto err_free_out;
		}

		spin_lock_irqsave(&ns2_usb->bulk_out_lock, flags);
		usb_fill_bulk_urb(urb, udev,
			usb_sndbulkpipe(udev, bulk_out->bEndpointAddress),
			data, NS2_BULK_SIZE, switch2_bulk_out, ns2_usb);
		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

		ns2_usb->bulk_out[i].urb = urb;
		ns2_usb->bulk_out[i].data = data;
		spin_unlock_irqrestore(&ns2_usb->bulk_out_lock, flags);
	}

	usb_set_intfdata(intf, ns2_usb);

	ns2_usb->cfg.dev = &ns2_usb->udev->dev;
	ns2_usb->cfg.send_command = switch2_usb_send_cmd;

	spin_lock_irqsave(&ns2_usb->bulk_in_lock, flags);
	ns2_usb->bulk_in[0].state = NS2_URB_OUT;
	usb_anchor_urb(ns2_usb->bulk_in[0].urb, &ns2_usb->bulk_in_anchor);
	ret = usb_submit_urb(ns2_usb->bulk_in[0].urb, GFP_ATOMIC);
	spin_unlock_irqrestore(&ns2_usb->bulk_in_lock, flags);

	if (ret < 0)
		goto err_free_out;

	ns2_usb->cfg.shutdown = switch2_usb_shutdown;
	ret = switch2_controller_attach_cfg(phys, &ns2_usb->cfg);
	if (ret < 0)
		goto err_free_out;

	return 0;

err_free_out:
	switch2_usb_free_out_urbs(ns2_usb);
err_free_in:
	switch2_usb_free_in_urbs(ns2_usb);
	devm_kfree(&intf->dev, ns2_usb);

	return ret;
}

static void switch2_usb_disconnect(struct usb_interface *intf)
{
	struct switch2_usb *ns2_usb = usb_get_intfdata(intf);

	switch2_usb_free_in_urbs(ns2_usb);

	/*
	 * We need to detach *before* we kill the out URBs to make sure no
	 * further URBs get scheduled by the HID endpoint in the meantime.
	 */
	if (ns2_usb->cfg.parent)
		switch2_controller_detach_cfg(ns2_usb->cfg.parent);

	switch2_usb_free_out_urbs(ns2_usb);
}

#define SWITCH2_CONTROLLER(vend, prod) \
	USB_DEVICE_AND_INTERFACE_INFO(vend, prod, USB_CLASS_VENDOR_SPEC, 0, 0)

static const struct usb_device_id switch2_usb_devices[] = {
	{ SWITCH2_CONTROLLER(USB_VENDOR_ID_NINTENDO, USB_DEVICE_ID_NINTENDO_NS2_JOYCONL) },
	{ SWITCH2_CONTROLLER(USB_VENDOR_ID_NINTENDO, USB_DEVICE_ID_NINTENDO_NS2_JOYCONR) },
	{ SWITCH2_CONTROLLER(USB_VENDOR_ID_NINTENDO, USB_DEVICE_ID_NINTENDO_NS2_PROCON) },
	{ SWITCH2_CONTROLLER(USB_VENDOR_ID_NINTENDO, USB_DEVICE_ID_NINTENDO_NS2_GCCON) },
	{ }
};
MODULE_DEVICE_TABLE(usb, switch2_usb_devices);

static struct usb_driver switch2_usb = {
	.name		= "nintendo-switch2",
	.id_table	= switch2_usb_devices,
	.probe		= switch2_usb_probe,
	.disconnect	= switch2_usb_disconnect,
};
module_usb_driver(switch2_usb);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vicki Pfau <vi@endrift.com>");
MODULE_DESCRIPTION("Driver for Nintendo Switch 2 Controllers");
