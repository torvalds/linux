// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Red Hat
 *
 * based in parts on udlfb.c:
 * Copyright (C) 2009 Roberto De Ioris <roberto@unbit.it>
 * Copyright (C) 2009 Jaya Kumar <jayakumar.lkml@gmail.com>
 * Copyright (C) 2009 Bernie Thompson <bernie@plugable.com>
 */

#include <linux/unaligned.h>

#include <drm/drm.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include "udl_drv.h"

/* -BULK_SIZE as per usb-skeleton. Can we get full page and avoid overhead? */
#define BULK_SIZE 512

#define NR_USB_REQUEST_CHANNEL 0x12

#define MAX_TRANSFER (PAGE_SIZE*16 - BULK_SIZE)
#define WRITES_IN_FLIGHT (20)
#define MAX_VENDOR_DESCRIPTOR_SIZE 256

#define UDL_SKU_PIXEL_LIMIT_DEFAULT	2080000

static struct urb *udl_get_urb_locked(struct udl_device *udl, long timeout);

/*
 * Try to make sense of whatever we parse. Therefore return @end on
 * errors, but don't fail hard.
 */
static const u8 *udl_parse_key_value_pair(struct udl_device *udl, const u8 *pos, const u8 *end)
{
	u16 key;
	u8 len;

	/* read key */
	if (pos >= end - 2)
		return end;
	key = get_unaligned_le16(pos);
	pos += 2;

	/* read value length */
	if (pos >= end - 1)
		return end;
	len = *pos++;

	/* read value */
	if (pos >= end - len)
		return end;
	switch (key) {
	case 0x0200: { /* maximum number of pixels */
		unsigned int sku_pixel_limit;

		if (len < sizeof(__le32))
			break;
		sku_pixel_limit = get_unaligned_le32(pos);
		if (sku_pixel_limit >= 16 * UDL_SKU_PIXEL_LIMIT_DEFAULT)
			break; /* almost 100 MiB, so probably bogus */
		udl->sku_pixel_limit = sku_pixel_limit;
		break;
	}
	default:
		break;
	}
	pos += len;

	return pos;
}

static int udl_parse_vendor_descriptor(struct udl_device *udl)
{
	struct drm_device *dev = &udl->drm;
	struct usb_device *udev = udl_to_usb_device(udl);
	bool detected = false;
	void *buf;
	int ret;
	unsigned int len;
	const u8 *desc;
	const u8 *desc_end;

	buf = kzalloc(MAX_VENDOR_DESCRIPTOR_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = usb_get_descriptor(udev, 0x5f, /* vendor specific */
				 0, buf, MAX_VENDOR_DESCRIPTOR_SIZE);
	if (ret < 0)
		goto out;
	len = ret;

	if (len < 5)
		goto out;

	desc = buf;
	desc_end = desc + len;

	if ((desc[0] != len) ||    /* descriptor length */
	    (desc[1] != 0x5f) ||   /* vendor descriptor type */
	    (desc[2] != 0x01) ||   /* version (2 bytes) */
	    (desc[3] != 0x00) ||
	    (desc[4] != len - 2))  /* length after type */
		goto out;
	desc += 5;

	detected = true;

	while (desc < desc_end)
		desc = udl_parse_key_value_pair(udl, desc, desc_end);

out:
	if (!detected)
		drm_warn(dev, "Unrecognized vendor firmware descriptor\n");
	kfree(buf);

	return 0;
}

/*
 * Need to ensure a channel is selected before submitting URBs
 */
int udl_select_std_channel(struct udl_device *udl)
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

void udl_urb_completion(struct urb *urb)
{
	struct urb_node *unode = urb->context;
	struct udl_device *udl = unode->dev;
	unsigned long flags;

	/* sync/async unlink faults aren't errors */
	if (urb->status) {
		if (!(urb->status == -ENOENT ||
		    urb->status == -ECONNRESET ||
		    urb->status == -EPROTO ||
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

	wake_up(&udl->urbs.sleep);
}

static void udl_free_urb_list(struct udl_device *udl)
{
	struct urb_node *unode;
	struct urb *urb;

	DRM_DEBUG("Waiting for completes and freeing all render urbs\n");

	/* keep waiting and freeing, until we've got 'em all */
	while (udl->urbs.count) {
		spin_lock_irq(&udl->urbs.lock);
		urb = udl_get_urb_locked(udl, MAX_SCHEDULE_TIMEOUT);
		udl->urbs.count--;
		spin_unlock_irq(&udl->urbs.lock);
		if (WARN_ON(!urb))
			break;
		unode = urb->context;
		/* Free each separately allocated piece */
		usb_free_coherent(urb->dev, udl->urbs.size,
				  urb->transfer_buffer, urb->transfer_dma);
		usb_free_urb(urb);
		kfree(unode);
	}

	wake_up_all(&udl->urbs.sleep);
}

static int udl_alloc_urb_list(struct udl_device *udl, int count, size_t size)
{
	struct urb *urb;
	struct urb_node *unode;
	char *buf;
	size_t wanted_size = count * size;
	struct usb_device *udev = udl_to_usb_device(udl);

	spin_lock_init(&udl->urbs.lock);
	INIT_LIST_HEAD(&udl->urbs.list);
	init_waitqueue_head(&udl->urbs.sleep);
	udl->urbs.count = 0;
	udl->urbs.available = 0;

retry:
	udl->urbs.size = size;

	while (udl->urbs.count * size < wanted_size) {
		unode = kzalloc(sizeof(struct urb_node), GFP_KERNEL);
		if (!unode)
			break;
		unode->dev = udl;

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
				udl_free_urb_list(udl);
				goto retry;
			}
			break;
		}

		/* urb->transfer_buffer_length set to actual before submit */
		usb_fill_bulk_urb(urb, udev, usb_sndbulkpipe(udev, 1),
				  buf, size, udl_urb_completion, unode);
		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

		list_add_tail(&unode->entry, &udl->urbs.list);

		udl->urbs.count++;
		udl->urbs.available++;
	}

	DRM_DEBUG("allocated %d %d byte urbs\n", udl->urbs.count, (int) size);

	return udl->urbs.count;
}

static struct urb *udl_get_urb_locked(struct udl_device *udl, long timeout)
{
	struct urb_node *unode;

	assert_spin_locked(&udl->urbs.lock);

	/* Wait for an in-flight buffer to complete and get re-queued */
	if (!wait_event_lock_irq_timeout(udl->urbs.sleep,
					 !udl->urbs.count ||
					 !list_empty(&udl->urbs.list),
					 udl->urbs.lock, timeout)) {
		DRM_INFO("wait for urb interrupted: available: %d\n",
			 udl->urbs.available);
		return NULL;
	}

	if (!udl->urbs.count)
		return NULL;

	unode = list_first_entry(&udl->urbs.list, struct urb_node, entry);
	list_del_init(&unode->entry);
	udl->urbs.available--;

	return unode->urb;
}

#define GET_URB_TIMEOUT	HZ
struct urb *udl_get_urb(struct udl_device *udl)
{
	struct urb *urb;

	spin_lock_irq(&udl->urbs.lock);
	urb = udl_get_urb_locked(udl, GET_URB_TIMEOUT);
	spin_unlock_irq(&udl->urbs.lock);
	return urb;
}

int udl_submit_urb(struct udl_device *udl, struct urb *urb, size_t len)
{
	int ret;

	if (WARN_ON(len > udl->urbs.size)) {
		ret = -EINVAL;
		goto error;
	}
	urb->transfer_buffer_length = len; /* set to actual payload len */
	ret = usb_submit_urb(urb, GFP_ATOMIC);
 error:
	if (ret) {
		udl_urb_completion(urb); /* because no one else will */
		DRM_ERROR("usb_submit_urb error %x\n", ret);
	}
	return ret;
}

/* wait until all pending URBs have been processed */
void udl_sync_pending_urbs(struct udl_device *udl)
{
	struct drm_device *dev = &udl->drm;

	spin_lock_irq(&udl->urbs.lock);
	/* 2 seconds as a sane timeout */
	if (!wait_event_lock_irq_timeout(udl->urbs.sleep,
					 udl->urbs.available == udl->urbs.count,
					 udl->urbs.lock,
					 msecs_to_jiffies(2000)))
		drm_err(dev, "Timeout for syncing pending URBs\n");
	spin_unlock_irq(&udl->urbs.lock);
}

int udl_init(struct udl_device *udl)
{
	struct drm_device *dev = &udl->drm;
	int ret = -ENOMEM;
	struct device *dma_dev;

	DRM_DEBUG("\n");

	dma_dev = usb_intf_get_dma_device(to_usb_interface(dev->dev));
	if (dma_dev) {
		drm_dev_set_dma_dev(dev, dma_dev);
		put_device(dma_dev);
	} else {
		drm_warn(dev, "buffer sharing not supported"); /* not an error */
	}

	/*
	 * Not all devices provide vendor descriptors with device
	 * information. Initialize to default values of real-world
	 * devices. It is just enough memory for FullHD.
	 */
	udl->sku_pixel_limit = UDL_SKU_PIXEL_LIMIT_DEFAULT;

	ret = udl_parse_vendor_descriptor(udl);
	if (ret)
		goto err;

	if (udl_select_std_channel(udl))
		DRM_ERROR("Selecting channel failed\n");

	if (!udl_alloc_urb_list(udl, WRITES_IN_FLIGHT, MAX_TRANSFER)) {
		DRM_ERROR("udl_alloc_urb_list failed\n");
		ret = -ENOMEM;
		goto err;
	}

	DRM_DEBUG("\n");
	ret = udl_modeset_init(udl);
	if (ret)
		goto err;

	return 0;

err:
	if (udl->urbs.count)
		udl_free_urb_list(udl);
	DRM_ERROR("%d\n", ret);
	return ret;
}

int udl_drop_usb(struct udl_device *udl)
{
	udl_free_urb_list(udl);

	return 0;
}
