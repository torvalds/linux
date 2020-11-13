// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Red Hat
 *
 * based in parts on udlfb.c:
 * Copyright (C) 2009 Roberto De Ioris <roberto@unbit.it>
 * Copyright (C) 2009 Jaya Kumar <jayakumar.lkml@gmail.com>
 * Copyright (C) 2009 Bernie Thompson <bernie@plugable.com>
 */

#include <drm/drm.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include "udl_drv.h"

/* -BULK_SIZE as per usb-skeleton. Can we get full page and avoid overhead? */
#define BULK_SIZE 512

#define NR_USB_REQUEST_CHANNEL 0x12

#define MAX_TRANSFER (PAGE_SIZE*16 - BULK_SIZE)
#define WRITES_IN_FLIGHT (4)
#define MAX_VENDOR_DESCRIPTOR_SIZE 256

#define GET_URB_TIMEOUT	HZ
#define FREE_URB_TIMEOUT (HZ*2)

static int udl_parse_vendor_descriptor(struct udl_device *udl)
{
	struct usb_device *udev = udl_to_usb_device(udl);
	char *desc;
	char *buf;
	char *desc_end;

	u8 total_len = 0;

	buf = kzalloc(MAX_VENDOR_DESCRIPTOR_SIZE, GFP_KERNEL);
	if (!buf)
		return false;
	desc = buf;

	total_len = usb_get_descriptor(udev, 0x5f, /* vendor specific */
				    0, desc, MAX_VENDOR_DESCRIPTOR_SIZE);
	if (total_len > 5) {
		DRM_INFO("vendor descriptor length:%x data:%11ph\n",
			total_len, desc);

		if ((desc[0] != total_len) || /* descriptor length */
		    (desc[1] != 0x5f) ||   /* vendor descriptor type */
		    (desc[2] != 0x01) ||   /* version (2 bytes) */
		    (desc[3] != 0x00) ||
		    (desc[4] != total_len - 2)) /* length after type */
			goto unrecognized;

		desc_end = desc + total_len;
		desc += 5; /* the fixed header we've already parsed */

		while (desc < desc_end) {
			u8 length;
			u16 key;

			key = le16_to_cpu(*((u16 *) desc));
			desc += sizeof(u16);
			length = *desc;
			desc++;

			switch (key) {
			case 0x0200: { /* max_area */
				u32 max_area;
				max_area = le32_to_cpu(*((u32 *)desc));
				DRM_DEBUG("DL chip limited to %d pixel modes\n",
					max_area);
				udl->sku_pixel_limit = max_area;
				break;
			}
			default:
				break;
			}
			desc += length;
		}
	}

	goto success;

unrecognized:
	/* allow udlfb to load for now even if firmware unrecognized */
	DRM_ERROR("Unrecognized vendor firmware descriptor\n");

success:
	kfree(buf);
	return true;
}

/*
 * Need to ensure a channel is selected before submitting URBs
 */
static int udl_select_std_channel(struct udl_device *udl)
{
	static const u8 set_def_chn[] = {0x57, 0xCD, 0xDC, 0xA7,
					 0x1C, 0x88, 0x5E, 0x15,
					 0x60, 0xFE, 0xC6, 0x97,
					 0x16, 0x3D, 0x47, 0xF2};

	void *sendbuf;
	int ret;
	struct usb_device *udev = udl_to_usb_device(udl);

	sendbuf = kmemdup(set_def_chn, sizeof(set_def_chn), GFP_KERNEL);
	if (!sendbuf)
		return -ENOMEM;

	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			      NR_USB_REQUEST_CHANNEL,
			      (USB_DIR_OUT | USB_TYPE_VENDOR), 0, 0,
			      sendbuf, sizeof(set_def_chn),
			      USB_CTRL_SET_TIMEOUT);
	kfree(sendbuf);
	return ret < 0 ? ret : 0;
}

static void udl_release_urb_work(struct work_struct *work)
{
	struct urb_node *unode = container_of(work, struct urb_node,
					      release_urb_work.work);

	up(&unode->dev->urbs.limit_sem);
}

void udl_urb_completion(struct urb *urb)
{
	struct urb_node *unode = urb->context;
	struct udl_device *udl = unode->dev;
	unsigned long flags;

	/* sync/async unlink faults aren't errors */
	if (urb->status) {
		if (!(urb->status == -ENOENT ||
		    urb->status == -ECONNRESET ||
		    urb->status == -ESHUTDOWN)) {
			DRM_ERROR("%s - nonzero write bulk status received: %d\n",
				__func__, urb->status);
		}
	}

	urb->transfer_buffer_length = udl->urbs.size; /* reset to actual */

	spin_lock_irqsave(&udl->urbs.lock, flags);
	list_add_tail(&unode->entry, &udl->urbs.list);
	udl->urbs.available++;
	spin_unlock_irqrestore(&udl->urbs.lock, flags);

#if 0
	/*
	 * When using fb_defio, we deadlock if up() is called
	 * while another is waiting. So queue to another process.
	 */
	if (fb_defio)
		schedule_delayed_work(&unode->release_urb_work, 0);
	else
#endif
		up(&udl->urbs.limit_sem);
}

static void udl_free_urb_list(struct drm_device *dev)
{
	struct udl_device *udl = to_udl(dev);
	int count = udl->urbs.count;
	struct list_head *node;
	struct urb_node *unode;
	struct urb *urb;

	DRM_DEBUG("Waiting for completes and freeing all render urbs\n");

	/* keep waiting and freeing, until we've got 'em all */
	while (count--) {
		down(&udl->urbs.limit_sem);

		spin_lock_irq(&udl->urbs.lock);

		node = udl->urbs.list.next; /* have reserved one with sem */
		list_del_init(node);

		spin_unlock_irq(&udl->urbs.lock);

		unode = list_entry(node, struct urb_node, entry);
		urb = unode->urb;

		/* Free each separately allocated piece */
		usb_free_coherent(urb->dev, udl->urbs.size,
				  urb->transfer_buffer, urb->transfer_dma);
		usb_free_urb(urb);
		kfree(node);
	}
	udl->urbs.count = 0;
}

static int udl_alloc_urb_list(struct drm_device *dev, int count, size_t size)
{
	struct udl_device *udl = to_udl(dev);
	struct urb *urb;
	struct urb_node *unode;
	char *buf;
	size_t wanted_size = count * size;
	struct usb_device *udev = udl_to_usb_device(udl);

	spin_lock_init(&udl->urbs.lock);

retry:
	udl->urbs.size = size;
	INIT_LIST_HEAD(&udl->urbs.list);

	sema_init(&udl->urbs.limit_sem, 0);
	udl->urbs.count = 0;
	udl->urbs.available = 0;

	while (udl->urbs.count * size < wanted_size) {
		unode = kzalloc(sizeof(struct urb_node), GFP_KERNEL);
		if (!unode)
			break;
		unode->dev = udl;

		INIT_DELAYED_WORK(&unode->release_urb_work,
			  udl_release_urb_work);

		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			kfree(unode);
			break;
		}
		unode->urb = urb;

		buf = usb_alloc_coherent(udev, size, GFP_KERNEL,
					 &urb->transfer_dma);
		if (!buf) {
			kfree(unode);
			usb_free_urb(urb);
			if (size > PAGE_SIZE) {
				size /= 2;
				udl_free_urb_list(dev);
				goto retry;
			}
			break;
		}

		/* urb->transfer_buffer_length set to actual before submit */
		usb_fill_bulk_urb(urb, udev, usb_sndbulkpipe(udev, 1),
				  buf, size, udl_urb_completion, unode);
		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

		list_add_tail(&unode->entry, &udl->urbs.list);

		up(&udl->urbs.limit_sem);
		udl->urbs.count++;
		udl->urbs.available++;
	}

	DRM_DEBUG("allocated %d %d byte urbs\n", udl->urbs.count, (int) size);

	return udl->urbs.count;
}

struct urb *udl_get_urb(struct drm_device *dev)
{
	struct udl_device *udl = to_udl(dev);
	int ret = 0;
	struct list_head *entry;
	struct urb_node *unode;
	struct urb *urb = NULL;

	/* Wait for an in-flight buffer to complete and get re-queued */
	ret = down_timeout(&udl->urbs.limit_sem, GET_URB_TIMEOUT);
	if (ret) {
		DRM_INFO("wait for urb interrupted: %x available: %d\n",
		       ret, udl->urbs.available);
		goto error;
	}

	spin_lock_irq(&udl->urbs.lock);

	BUG_ON(list_empty(&udl->urbs.list)); /* reserved one with limit_sem */
	entry = udl->urbs.list.next;
	list_del_init(entry);
	udl->urbs.available--;

	spin_unlock_irq(&udl->urbs.lock);

	unode = list_entry(entry, struct urb_node, entry);
	urb = unode->urb;

error:
	return urb;
}

int udl_submit_urb(struct drm_device *dev, struct urb *urb, size_t len)
{
	struct udl_device *udl = to_udl(dev);
	int ret;

	BUG_ON(len > udl->urbs.size);

	urb->transfer_buffer_length = len; /* set to actual payload len */
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret) {
		udl_urb_completion(urb); /* because no one else will */
		DRM_ERROR("usb_submit_urb error %x\n", ret);
	}
	return ret;
}

int udl_init(struct udl_device *udl)
{
	struct drm_device *dev = &udl->drm;
	int ret = -ENOMEM;

	DRM_DEBUG("\n");

	mutex_init(&udl->gem_lock);

	if (!udl_parse_vendor_descriptor(udl)) {
		ret = -ENODEV;
		DRM_ERROR("firmware not recognized. Assume incompatible device\n");
		goto err;
	}

	if (udl_select_std_channel(udl))
		DRM_ERROR("Selecting channel failed\n");

	if (!udl_alloc_urb_list(dev, WRITES_IN_FLIGHT, MAX_TRANSFER)) {
		DRM_ERROR("udl_alloc_urb_list failed\n");
		goto err;
	}

	DRM_DEBUG("\n");
	ret = udl_modeset_init(dev);
	if (ret)
		goto err;

	drm_kms_helper_poll_init(dev);

	return 0;

err:
	if (udl->urbs.count)
		udl_free_urb_list(dev);
	DRM_ERROR("%d\n", ret);
	return ret;
}

int udl_drop_usb(struct drm_device *dev)
{
	udl_free_urb_list(dev);
	return 0;
}
