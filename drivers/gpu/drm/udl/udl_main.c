/*
 * Copyright (C) 2012 Red Hat
 *
 * based in parts on udlfb.c:
 * Copyright (C) 2009 Roberto De Ioris <roberto@unbit.it>
 * Copyright (C) 2009 Jaya Kumar <jayakumar.lkml@gmail.com>
 * Copyright (C) 2009 Bernie Thompson <bernie@plugable.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License v2. See the file COPYING in the main directory of this archive for
 * more details.
 */
#include "drmP.h"
#include "udl_drv.h"

/* -BULK_SIZE as per usb-skeleton. Can we get full page and avoid overhead? */
#define BULK_SIZE 512

#define MAX_TRANSFER (PAGE_SIZE*16 - BULK_SIZE)
#define WRITES_IN_FLIGHT (4)
#define MAX_VENDOR_DESCRIPTOR_SIZE 256

#define GET_URB_TIMEOUT	HZ
#define FREE_URB_TIMEOUT (HZ*2)

static int udl_parse_vendor_descriptor(struct drm_device *dev,
				       struct usb_device *usbdev)
{
	struct udl_device *udl = dev->dev_private;
	char *desc;
	char *buf;
	char *desc_end;

	u8 total_len = 0;

	buf = kzalloc(MAX_VENDOR_DESCRIPTOR_SIZE, GFP_KERNEL);
	if (!buf)
		return false;
	desc = buf;

	total_len = usb_get_descriptor(usbdev, 0x5f, /* vendor specific */
				    0, desc, MAX_VENDOR_DESCRIPTOR_SIZE);
	if (total_len > 5) {
		DRM_INFO("vendor descriptor length:%x data:%02x %02x %02x %02x" \
			"%02x %02x %02x %02x %02x %02x %02x\n",
			total_len, desc[0],
			desc[1], desc[2], desc[3], desc[4], desc[5], desc[6],
			desc[7], desc[8], desc[9], desc[10]);

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
			atomic_set(&udl->lost_pixels, 1);
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
	struct udl_device *udl = dev->dev_private;
	int count = udl->urbs.count;
	struct list_head *node;
	struct urb_node *unode;
	struct urb *urb;
	int ret;
	unsigned long flags;

	DRM_DEBUG("Waiting for completes and freeing all render urbs\n");

	/* keep waiting and freeing, until we've got 'em all */
	while (count--) {

		/* Getting interrupted means a leak, but ok at shutdown*/
		ret = down_interruptible(&udl->urbs.limit_sem);
		if (ret)
			break;

		spin_lock_irqsave(&udl->urbs.lock, flags);

		node = udl->urbs.list.next; /* have reserved one with sem */
		list_del_init(node);

		spin_unlock_irqrestore(&udl->urbs.lock, flags);

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
	struct udl_device *udl = dev->dev_private;
	int i = 0;
	struct urb *urb;
	struct urb_node *unode;
	char *buf;

	spin_lock_init(&udl->urbs.lock);

	udl->urbs.size = size;
	INIT_LIST_HEAD(&udl->urbs.list);

	while (i < count) {
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

		buf = usb_alloc_coherent(udl->ddev->usbdev, MAX_TRANSFER, GFP_KERNEL,
					 &urb->transfer_dma);
		if (!buf) {
			kfree(unode);
			usb_free_urb(urb);
			break;
		}

		/* urb->transfer_buffer_length set to actual before submit */
		usb_fill_bulk_urb(urb, udl->ddev->usbdev, usb_sndbulkpipe(udl->ddev->usbdev, 1),
			buf, size, udl_urb_completion, unode);
		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

		list_add_tail(&unode->entry, &udl->urbs.list);

		i++;
	}

	sema_init(&udl->urbs.limit_sem, i);
	udl->urbs.count = i;
	udl->urbs.available = i;

	DRM_DEBUG("allocated %d %d byte urbs\n", i, (int) size);

	return i;
}

struct urb *udl_get_urb(struct drm_device *dev)
{
	struct udl_device *udl = dev->dev_private;
	int ret = 0;
	struct list_head *entry;
	struct urb_node *unode;
	struct urb *urb = NULL;
	unsigned long flags;

	/* Wait for an in-flight buffer to complete and get re-queued */
	ret = down_timeout(&udl->urbs.limit_sem, GET_URB_TIMEOUT);
	if (ret) {
		atomic_set(&udl->lost_pixels, 1);
		DRM_INFO("wait for urb interrupted: %x available: %d\n",
		       ret, udl->urbs.available);
		goto error;
	}

	spin_lock_irqsave(&udl->urbs.lock, flags);

	BUG_ON(list_empty(&udl->urbs.list)); /* reserved one with limit_sem */
	entry = udl->urbs.list.next;
	list_del_init(entry);
	udl->urbs.available--;

	spin_unlock_irqrestore(&udl->urbs.lock, flags);

	unode = list_entry(entry, struct urb_node, entry);
	urb = unode->urb;

error:
	return urb;
}

int udl_submit_urb(struct drm_device *dev, struct urb *urb, size_t len)
{
	struct udl_device *udl = dev->dev_private;
	int ret;

	BUG_ON(len > udl->urbs.size);

	urb->transfer_buffer_length = len; /* set to actual payload len */
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret) {
		udl_urb_completion(urb); /* because no one else will */
		atomic_set(&udl->lost_pixels, 1);
		DRM_ERROR("usb_submit_urb error %x\n", ret);
	}
	return ret;
}

int udl_driver_load(struct drm_device *dev, unsigned long flags)
{
	struct udl_device *udl;
	int ret;

	DRM_DEBUG("\n");
	udl = kzalloc(sizeof(struct udl_device), GFP_KERNEL);
	if (!udl)
		return -ENOMEM;

	udl->ddev = dev;
	dev->dev_private = udl;

	if (!udl_parse_vendor_descriptor(dev, dev->usbdev)) {
		DRM_ERROR("firmware not recognized. Assume incompatible device\n");
		goto err;
	}

	if (!udl_alloc_urb_list(dev, WRITES_IN_FLIGHT, MAX_TRANSFER)) {
		ret = -ENOMEM;
		DRM_ERROR("udl_alloc_urb_list failed\n");
		goto err;
	}

	DRM_DEBUG("\n");
	ret = udl_modeset_init(dev);

	ret = udl_fbdev_init(dev);
	return 0;
err:
	kfree(udl);
	DRM_ERROR("%d\n", ret);
	return ret;
}

int udl_drop_usb(struct drm_device *dev)
{
	udl_free_urb_list(dev);
	return 0;
}

int udl_driver_unload(struct drm_device *dev)
{
	struct udl_device *udl = dev->dev_private;

	if (udl->urbs.count)
		udl_free_urb_list(dev);

	udl_fbdev_cleanup(dev);
	udl_modeset_cleanup(dev);
	kfree(udl);
	return 0;
}
