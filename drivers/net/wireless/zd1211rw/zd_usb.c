/* zd_usb.c
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <asm/unaligned.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/usb.h>
#include <linux/workqueue.h>
#include <net/ieee80211.h>

#include "zd_def.h"
#include "zd_netdev.h"
#include "zd_mac.h"
#include "zd_usb.h"
#include "zd_util.h"

static struct usb_device_id usb_ids[] = {
	/* ZD1211 */
	{ USB_DEVICE(0x0ace, 0x1211), .driver_info = DEVICE_ZD1211 },
	{ USB_DEVICE(0x07b8, 0x6001), .driver_info = DEVICE_ZD1211 },
	{ USB_DEVICE(0x126f, 0xa006), .driver_info = DEVICE_ZD1211 },
	{ USB_DEVICE(0x6891, 0xa727), .driver_info = DEVICE_ZD1211 },
	{ USB_DEVICE(0x0df6, 0x9071), .driver_info = DEVICE_ZD1211 },
	{ USB_DEVICE(0x157e, 0x300b), .driver_info = DEVICE_ZD1211 },
	{ USB_DEVICE(0x079b, 0x004a), .driver_info = DEVICE_ZD1211 },
	{ USB_DEVICE(0x1740, 0x2000), .driver_info = DEVICE_ZD1211 },
	{ USB_DEVICE(0x157e, 0x3204), .driver_info = DEVICE_ZD1211 },
	{ USB_DEVICE(0x0586, 0x3402), .driver_info = DEVICE_ZD1211 },
	{ USB_DEVICE(0x0b3b, 0x5630), .driver_info = DEVICE_ZD1211 },
	{ USB_DEVICE(0x0b05, 0x170c), .driver_info = DEVICE_ZD1211 },
	{ USB_DEVICE(0x1435, 0x0711), .driver_info = DEVICE_ZD1211 },
	{ USB_DEVICE(0x0586, 0x3409), .driver_info = DEVICE_ZD1211 },
	{ USB_DEVICE(0x0b3b, 0x1630), .driver_info = DEVICE_ZD1211 },
	/* ZD1211B */
	{ USB_DEVICE(0x0ace, 0x1215), .driver_info = DEVICE_ZD1211B },
	{ USB_DEVICE(0x157e, 0x300d), .driver_info = DEVICE_ZD1211B },
	{ USB_DEVICE(0x079b, 0x0062), .driver_info = DEVICE_ZD1211B },
	{ USB_DEVICE(0x1582, 0x6003), .driver_info = DEVICE_ZD1211B },
	/* "Driverless" devices that need ejecting */
	{ USB_DEVICE(0x0ace, 0x2011), .driver_info = DEVICE_INSTALLER },
	{}
};

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("USB driver for devices with the ZD1211 chip.");
MODULE_AUTHOR("Ulrich Kunitz");
MODULE_AUTHOR("Daniel Drake");
MODULE_VERSION("1.0");
MODULE_DEVICE_TABLE(usb, usb_ids);

#define FW_ZD1211_PREFIX	"zd1211/zd1211_"
#define FW_ZD1211B_PREFIX	"zd1211/zd1211b_"

/* register address handling */

#ifdef DEBUG
static int check_addr(struct zd_usb *usb, zd_addr_t addr)
{
	u32 base = ZD_ADDR_BASE(addr);
	u32 offset = ZD_OFFSET(addr);

	if ((u32)addr & ADDR_ZERO_MASK)
		goto invalid_address;
	switch (base) {
	case USB_BASE:
		break;
	case CR_BASE:
		if (offset > CR_MAX_OFFSET) {
			dev_dbg(zd_usb_dev(usb),
				"CR offset %#010x larger than"
				" CR_MAX_OFFSET %#10x\n",
				offset, CR_MAX_OFFSET);
			goto invalid_address;
		}
		if (offset & 1) {
			dev_dbg(zd_usb_dev(usb),
				"CR offset %#010x is not a multiple of 2\n",
				offset);
			goto invalid_address;
		}
		break;
	case E2P_BASE:
		if (offset > E2P_MAX_OFFSET) {
			dev_dbg(zd_usb_dev(usb),
				"E2P offset %#010x larger than"
				" E2P_MAX_OFFSET %#010x\n",
				offset, E2P_MAX_OFFSET);
			goto invalid_address;
		}
		break;
	case FW_BASE:
		if (!usb->fw_base_offset) {
			dev_dbg(zd_usb_dev(usb),
			       "ERROR: fw base offset has not been set\n");
			return -EAGAIN;
		}
		if (offset > FW_MAX_OFFSET) {
			dev_dbg(zd_usb_dev(usb),
				"FW offset %#10x is larger than"
				" FW_MAX_OFFSET %#010x\n",
				offset, FW_MAX_OFFSET);
			goto invalid_address;
		}
		break;
	default:
		dev_dbg(zd_usb_dev(usb),
			"address has unsupported base %#010x\n", addr);
		goto invalid_address;
	}

	return 0;
invalid_address:
	dev_dbg(zd_usb_dev(usb),
		"ERROR: invalid address: %#010x\n", addr);
	return -EINVAL;
}
#endif /* DEBUG */

static u16 usb_addr(struct zd_usb *usb, zd_addr_t addr)
{
	u32 base;
	u16 offset;

	base = ZD_ADDR_BASE(addr);
	offset = ZD_OFFSET(addr);

	ZD_ASSERT(check_addr(usb, addr) == 0);

	switch (base) {
	case CR_BASE:
		offset += CR_BASE_OFFSET;
		break;
	case E2P_BASE:
		offset += E2P_BASE_OFFSET;
		break;
	case FW_BASE:
		offset += usb->fw_base_offset;
		break;
	}

	return offset;
}

/* USB device initialization */

static int request_fw_file(
	const struct firmware **fw, const char *name, struct device *device)
{
	int r;

	dev_dbg_f(device, "fw name %s\n", name);

	r = request_firmware(fw, name, device);
	if (r)
		dev_err(device,
		       "Could not load firmware file %s. Error number %d\n",
		       name, r);
	return r;
}

static inline u16 get_bcdDevice(const struct usb_device *udev)
{
	return le16_to_cpu(udev->descriptor.bcdDevice);
}

enum upload_code_flags {
	REBOOT = 1,
};

/* Ensures that MAX_TRANSFER_SIZE is even. */
#define MAX_TRANSFER_SIZE (USB_MAX_TRANSFER_SIZE & ~1)

static int upload_code(struct usb_device *udev,
	const u8 *data, size_t size, u16 code_offset, int flags)
{
	u8 *p;
	int r;

	/* USB request blocks need "kmalloced" buffers.
	 */
	p = kmalloc(MAX_TRANSFER_SIZE, GFP_KERNEL);
	if (!p) {
		dev_err(&udev->dev, "out of memory\n");
		r = -ENOMEM;
		goto error;
	}

	size &= ~1;
	while (size > 0) {
		size_t transfer_size = size <= MAX_TRANSFER_SIZE ?
			size : MAX_TRANSFER_SIZE;

		dev_dbg_f(&udev->dev, "transfer size %zu\n", transfer_size);

		memcpy(p, data, transfer_size);
		r = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			USB_REQ_FIRMWARE_DOWNLOAD,
			USB_DIR_OUT | USB_TYPE_VENDOR,
			code_offset, 0, p, transfer_size, 1000 /* ms */);
		if (r < 0) {
			dev_err(&udev->dev,
			       "USB control request for firmware upload"
			       " failed. Error number %d\n", r);
			goto error;
		}
		transfer_size = r & ~1;

		size -= transfer_size;
		data += transfer_size;
		code_offset += transfer_size/sizeof(u16);
	}

	if (flags & REBOOT) {
		u8 ret;

		r = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
			USB_REQ_FIRMWARE_CONFIRM,
			USB_DIR_IN | USB_TYPE_VENDOR,
			0, 0, &ret, sizeof(ret), 5000 /* ms */);
		if (r != sizeof(ret)) {
			dev_err(&udev->dev,
				"control request firmeware confirmation failed."
				" Return value %d\n", r);
			if (r >= 0)
				r = -ENODEV;
			goto error;
		}
		if (ret & 0x80) {
			dev_err(&udev->dev,
				"Internal error while downloading."
				" Firmware confirm return value %#04x\n",
				(unsigned int)ret);
			r = -ENODEV;
			goto error;
		}
		dev_dbg_f(&udev->dev, "firmware confirm return value %#04x\n",
			(unsigned int)ret);
	}

	r = 0;
error:
	kfree(p);
	return r;
}

static u16 get_word(const void *data, u16 offset)
{
	const __le16 *p = data;
	return le16_to_cpu(p[offset]);
}

static char *get_fw_name(char *buffer, size_t size, u8 device_type,
	               const char* postfix)
{
	scnprintf(buffer, size, "%s%s",
		device_type == DEVICE_ZD1211B ?
			FW_ZD1211B_PREFIX : FW_ZD1211_PREFIX,
		postfix);
	return buffer;
}

static int handle_version_mismatch(struct usb_device *udev, u8 device_type,
	const struct firmware *ub_fw)
{
	const struct firmware *ur_fw = NULL;
	int offset;
	int r = 0;
	char fw_name[128];

	r = request_fw_file(&ur_fw,
		get_fw_name(fw_name, sizeof(fw_name), device_type, "ur"),
		&udev->dev);
	if (r)
		goto error;

	r = upload_code(udev, ur_fw->data, ur_fw->size, FW_START_OFFSET,
		REBOOT);
	if (r)
		goto error;

	offset = ((EEPROM_REGS_OFFSET + EEPROM_REGS_SIZE) * sizeof(u16));
	r = upload_code(udev, ub_fw->data + offset, ub_fw->size - offset,
		E2P_BASE_OFFSET + EEPROM_REGS_SIZE, REBOOT);

	/* At this point, the vendor driver downloads the whole firmware
	 * image, hacks around with version IDs, and uploads it again,
	 * completely overwriting the boot code. We do not do this here as
	 * it is not required on any tested devices, and it is suspected to
	 * cause problems. */
error:
	release_firmware(ur_fw);
	return r;
}

static int upload_firmware(struct usb_device *udev, u8 device_type)
{
	int r;
	u16 fw_bcdDevice;
	u16 bcdDevice;
	const struct firmware *ub_fw = NULL;
	const struct firmware *uph_fw = NULL;
	char fw_name[128];

	bcdDevice = get_bcdDevice(udev);

	r = request_fw_file(&ub_fw,
		get_fw_name(fw_name, sizeof(fw_name), device_type,  "ub"),
		&udev->dev);
	if (r)
		goto error;

	fw_bcdDevice = get_word(ub_fw->data, EEPROM_REGS_OFFSET);

	if (fw_bcdDevice != bcdDevice) {
		dev_info(&udev->dev,
			"firmware version %#06x and device bootcode version "
			"%#06x differ\n", fw_bcdDevice, bcdDevice);
		if (bcdDevice <= 0x4313)
			dev_warn(&udev->dev, "device has old bootcode, please "
				"report success or failure\n");

		r = handle_version_mismatch(udev, device_type, ub_fw);
		if (r)
			goto error;
	} else {
		dev_dbg_f(&udev->dev,
			"firmware device id %#06x is equal to the "
			"actual device id\n", fw_bcdDevice);
	}


	r = request_fw_file(&uph_fw,
		get_fw_name(fw_name, sizeof(fw_name), device_type, "uphr"),
		&udev->dev);
	if (r)
		goto error;

	r = upload_code(udev, uph_fw->data, uph_fw->size, FW_START_OFFSET,
		        REBOOT);
	if (r) {
		dev_err(&udev->dev,
			"Could not upload firmware code uph. Error number %d\n",
			r);
	}

	/* FALL-THROUGH */
error:
	release_firmware(ub_fw);
	release_firmware(uph_fw);
	return r;
}

#define urb_dev(urb) (&(urb)->dev->dev)

static inline void handle_regs_int(struct urb *urb)
{
	struct zd_usb *usb = urb->context;
	struct zd_usb_interrupt *intr = &usb->intr;
	int len;

	ZD_ASSERT(in_interrupt());
	spin_lock(&intr->lock);

	if (intr->read_regs_enabled) {
		intr->read_regs.length = len = urb->actual_length;

		if (len > sizeof(intr->read_regs.buffer))
			len = sizeof(intr->read_regs.buffer);
		memcpy(intr->read_regs.buffer, urb->transfer_buffer, len);
		intr->read_regs_enabled = 0;
		complete(&intr->read_regs.completion);
		goto out;
	}

	dev_dbg_f(urb_dev(urb), "regs interrupt ignored\n");
out:
	spin_unlock(&intr->lock);
}

static inline void handle_retry_failed_int(struct urb *urb)
{
	dev_dbg_f(urb_dev(urb), "retry failed interrupt\n");
}


static void int_urb_complete(struct urb *urb)
{
	int r;
	struct usb_int_header *hdr;

	switch (urb->status) {
	case 0:
		break;
	case -ESHUTDOWN:
	case -EINVAL:
	case -ENODEV:
	case -ENOENT:
	case -ECONNRESET:
	case -EPIPE:
		goto kfree;
	default:
		goto resubmit;
	}

	if (urb->actual_length < sizeof(hdr)) {
		dev_dbg_f(urb_dev(urb), "error: urb %p to small\n", urb);
		goto resubmit;
	}

	hdr = urb->transfer_buffer;
	if (hdr->type != USB_INT_TYPE) {
		dev_dbg_f(urb_dev(urb), "error: urb %p wrong type\n", urb);
		goto resubmit;
	}

	switch (hdr->id) {
	case USB_INT_ID_REGS:
		handle_regs_int(urb);
		break;
	case USB_INT_ID_RETRY_FAILED:
		handle_retry_failed_int(urb);
		break;
	default:
		dev_dbg_f(urb_dev(urb), "error: urb %p unknown id %x\n", urb,
			(unsigned int)hdr->id);
		goto resubmit;
	}

resubmit:
	r = usb_submit_urb(urb, GFP_ATOMIC);
	if (r) {
		dev_dbg_f(urb_dev(urb), "resubmit urb %p\n", urb);
		goto kfree;
	}
	return;
kfree:
	kfree(urb->transfer_buffer);
}

static inline int int_urb_interval(struct usb_device *udev)
{
	switch (udev->speed) {
	case USB_SPEED_HIGH:
		return 4;
	case USB_SPEED_LOW:
		return 10;
	case USB_SPEED_FULL:
	default:
		return 1;
	}
}

static inline int usb_int_enabled(struct zd_usb *usb)
{
	unsigned long flags;
	struct zd_usb_interrupt *intr = &usb->intr;
	struct urb *urb;

	spin_lock_irqsave(&intr->lock, flags);
	urb = intr->urb;
	spin_unlock_irqrestore(&intr->lock, flags);
	return urb != NULL;
}

int zd_usb_enable_int(struct zd_usb *usb)
{
	int r;
	struct usb_device *udev;
	struct zd_usb_interrupt *intr = &usb->intr;
	void *transfer_buffer = NULL;
	struct urb *urb;

	dev_dbg_f(zd_usb_dev(usb), "\n");

	urb = usb_alloc_urb(0, GFP_NOFS);
	if (!urb) {
		r = -ENOMEM;
		goto out;
	}

	ZD_ASSERT(!irqs_disabled());
	spin_lock_irq(&intr->lock);
	if (intr->urb) {
		spin_unlock_irq(&intr->lock);
		r = 0;
		goto error_free_urb;
	}
	intr->urb = urb;
	spin_unlock_irq(&intr->lock);

	/* TODO: make it a DMA buffer */
	r = -ENOMEM;
	transfer_buffer = kmalloc(USB_MAX_EP_INT_BUFFER, GFP_NOFS);
	if (!transfer_buffer) {
		dev_dbg_f(zd_usb_dev(usb),
			"couldn't allocate transfer_buffer\n");
		goto error_set_urb_null;
	}

	udev = zd_usb_to_usbdev(usb);
	usb_fill_int_urb(urb, udev, usb_rcvintpipe(udev, EP_INT_IN),
			 transfer_buffer, USB_MAX_EP_INT_BUFFER,
			 int_urb_complete, usb,
			 intr->interval);

	dev_dbg_f(zd_usb_dev(usb), "submit urb %p\n", intr->urb);
	r = usb_submit_urb(urb, GFP_NOFS);
	if (r) {
		dev_dbg_f(zd_usb_dev(usb),
			 "Couldn't submit urb. Error number %d\n", r);
		goto error;
	}

	return 0;
error:
	kfree(transfer_buffer);
error_set_urb_null:
	spin_lock_irq(&intr->lock);
	intr->urb = NULL;
	spin_unlock_irq(&intr->lock);
error_free_urb:
	usb_free_urb(urb);
out:
	return r;
}

void zd_usb_disable_int(struct zd_usb *usb)
{
	unsigned long flags;
	struct zd_usb_interrupt *intr = &usb->intr;
	struct urb *urb;

	spin_lock_irqsave(&intr->lock, flags);
	urb = intr->urb;
	if (!urb) {
		spin_unlock_irqrestore(&intr->lock, flags);
		return;
	}
	intr->urb = NULL;
	spin_unlock_irqrestore(&intr->lock, flags);

	usb_kill_urb(urb);
	dev_dbg_f(zd_usb_dev(usb), "urb %p killed\n", urb);
	usb_free_urb(urb);
}

static void handle_rx_packet(struct zd_usb *usb, const u8 *buffer,
			     unsigned int length)
{
	int i;
	struct zd_mac *mac = zd_usb_to_mac(usb);
	const struct rx_length_info *length_info;

	if (length < sizeof(struct rx_length_info)) {
		/* It's not a complete packet anyhow. */
		return;
	}
	length_info = (struct rx_length_info *)
		(buffer + length - sizeof(struct rx_length_info));

	/* It might be that three frames are merged into a single URB
	 * transaction. We have to check for the length info tag.
	 *
	 * While testing we discovered that length_info might be unaligned,
	 * because if USB transactions are merged, the last packet will not
	 * be padded. Unaligned access might also happen if the length_info
	 * structure is not present.
	 */
	if (get_unaligned(&length_info->tag) == cpu_to_le16(RX_LENGTH_INFO_TAG))
	{
		unsigned int l, k, n;
		for (i = 0, l = 0;; i++) {
			k = le16_to_cpu(get_unaligned(&length_info->length[i]));
			n = l+k;
			if (n > length)
				return;
			zd_mac_rx(mac, buffer+l, k);
			if (i >= 2)
				return;
			l = (n+3) & ~3;
		}
	} else {
		zd_mac_rx(mac, buffer, length);
	}
}

static void rx_urb_complete(struct urb *urb)
{
	struct zd_usb *usb;
	struct zd_usb_rx *rx;
	const u8 *buffer;
	unsigned int length;

	switch (urb->status) {
	case 0:
		break;
	case -ESHUTDOWN:
	case -EINVAL:
	case -ENODEV:
	case -ENOENT:
	case -ECONNRESET:
	case -EPIPE:
		return;
	default:
		dev_dbg_f(urb_dev(urb), "urb %p error %d\n", urb, urb->status);
		goto resubmit;
	}

	buffer = urb->transfer_buffer;
	length = urb->actual_length;
	usb = urb->context;
	rx = &usb->rx;

	if (length%rx->usb_packet_size > rx->usb_packet_size-4) {
		/* If there is an old first fragment, we don't care. */
		dev_dbg_f(urb_dev(urb), "*** first fragment ***\n");
		ZD_ASSERT(length <= ARRAY_SIZE(rx->fragment));
		spin_lock(&rx->lock);
		memcpy(rx->fragment, buffer, length);
		rx->fragment_length = length;
		spin_unlock(&rx->lock);
		goto resubmit;
	}

	spin_lock(&rx->lock);
	if (rx->fragment_length > 0) {
		/* We are on a second fragment, we believe */
		ZD_ASSERT(length + rx->fragment_length <=
			  ARRAY_SIZE(rx->fragment));
		dev_dbg_f(urb_dev(urb), "*** second fragment ***\n");
		memcpy(rx->fragment+rx->fragment_length, buffer, length);
		handle_rx_packet(usb, rx->fragment,
			         rx->fragment_length + length);
		rx->fragment_length = 0;
		spin_unlock(&rx->lock);
	} else {
		spin_unlock(&rx->lock);
		handle_rx_packet(usb, buffer, length);
	}

resubmit:
	usb_submit_urb(urb, GFP_ATOMIC);
}

static struct urb *alloc_urb(struct zd_usb *usb)
{
	struct usb_device *udev = zd_usb_to_usbdev(usb);
	struct urb *urb;
	void *buffer;

	urb = usb_alloc_urb(0, GFP_NOFS);
	if (!urb)
		return NULL;
	buffer = usb_buffer_alloc(udev, USB_MAX_RX_SIZE, GFP_NOFS,
		                  &urb->transfer_dma);
	if (!buffer) {
		usb_free_urb(urb);
		return NULL;
	}

	usb_fill_bulk_urb(urb, udev, usb_rcvbulkpipe(udev, EP_DATA_IN),
		          buffer, USB_MAX_RX_SIZE,
			  rx_urb_complete, usb);
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	return urb;
}

static void free_urb(struct urb *urb)
{
	if (!urb)
		return;
	usb_buffer_free(urb->dev, urb->transfer_buffer_length,
		        urb->transfer_buffer, urb->transfer_dma);
	usb_free_urb(urb);
}

int zd_usb_enable_rx(struct zd_usb *usb)
{
	int i, r;
	struct zd_usb_rx *rx = &usb->rx;
	struct urb **urbs;

	dev_dbg_f(zd_usb_dev(usb), "\n");

	r = -ENOMEM;
	urbs = kcalloc(URBS_COUNT, sizeof(struct urb *), GFP_NOFS);
	if (!urbs)
		goto error;
	for (i = 0; i < URBS_COUNT; i++) {
		urbs[i] = alloc_urb(usb);
		if (!urbs[i])
			goto error;
	}

	ZD_ASSERT(!irqs_disabled());
	spin_lock_irq(&rx->lock);
	if (rx->urbs) {
		spin_unlock_irq(&rx->lock);
		r = 0;
		goto error;
	}
	rx->urbs = urbs;
	rx->urbs_count = URBS_COUNT;
	spin_unlock_irq(&rx->lock);

	for (i = 0; i < URBS_COUNT; i++) {
		r = usb_submit_urb(urbs[i], GFP_NOFS);
		if (r)
			goto error_submit;
	}

	return 0;
error_submit:
	for (i = 0; i < URBS_COUNT; i++) {
		usb_kill_urb(urbs[i]);
	}
	spin_lock_irq(&rx->lock);
	rx->urbs = NULL;
	rx->urbs_count = 0;
	spin_unlock_irq(&rx->lock);
error:
	if (urbs) {
		for (i = 0; i < URBS_COUNT; i++)
			free_urb(urbs[i]);
	}
	return r;
}

void zd_usb_disable_rx(struct zd_usb *usb)
{
	int i;
	unsigned long flags;
	struct urb **urbs;
	unsigned int count;
	struct zd_usb_rx *rx = &usb->rx;

	spin_lock_irqsave(&rx->lock, flags);
	urbs = rx->urbs;
	count = rx->urbs_count;
	spin_unlock_irqrestore(&rx->lock, flags);
	if (!urbs)
		return;

	for (i = 0; i < count; i++) {
		usb_kill_urb(urbs[i]);
		free_urb(urbs[i]);
	}
	kfree(urbs);

	spin_lock_irqsave(&rx->lock, flags);
	rx->urbs = NULL;
	rx->urbs_count = 0;
	spin_unlock_irqrestore(&rx->lock, flags);
}

static void tx_urb_complete(struct urb *urb)
{
	int r;

	switch (urb->status) {
	case 0:
		break;
	case -ESHUTDOWN:
	case -EINVAL:
	case -ENODEV:
	case -ENOENT:
	case -ECONNRESET:
	case -EPIPE:
		dev_dbg_f(urb_dev(urb), "urb %p error %d\n", urb, urb->status);
		break;
	default:
		dev_dbg_f(urb_dev(urb), "urb %p error %d\n", urb, urb->status);
		goto resubmit;
	}
free_urb:
	usb_buffer_free(urb->dev, urb->transfer_buffer_length,
		        urb->transfer_buffer, urb->transfer_dma);
	usb_free_urb(urb);
	return;
resubmit:
	r = usb_submit_urb(urb, GFP_ATOMIC);
	if (r) {
		dev_dbg_f(urb_dev(urb), "error resubmit urb %p %d\n", urb, r);
		goto free_urb;
	}
}

/* Puts the frame on the USB endpoint. It doesn't wait for
 * completion. The frame must contain the control set.
 */
int zd_usb_tx(struct zd_usb *usb, const u8 *frame, unsigned int length)
{
	int r;
	struct usb_device *udev = zd_usb_to_usbdev(usb);
	struct urb *urb;
	void *buffer;

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb) {
		r = -ENOMEM;
		goto out;
	}

	buffer = usb_buffer_alloc(zd_usb_to_usbdev(usb), length, GFP_ATOMIC,
		                  &urb->transfer_dma);
	if (!buffer) {
		r = -ENOMEM;
		goto error_free_urb;
	}
	memcpy(buffer, frame, length);

	usb_fill_bulk_urb(urb, udev, usb_sndbulkpipe(udev, EP_DATA_OUT),
		          buffer, length, tx_urb_complete, NULL);
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	r = usb_submit_urb(urb, GFP_ATOMIC);
	if (r)
		goto error;
	return 0;
error:
	usb_buffer_free(zd_usb_to_usbdev(usb), length, buffer,
		        urb->transfer_dma);
error_free_urb:
	usb_free_urb(urb);
out:
	return r;
}

static inline void init_usb_interrupt(struct zd_usb *usb)
{
	struct zd_usb_interrupt *intr = &usb->intr;

	spin_lock_init(&intr->lock);
	intr->interval = int_urb_interval(zd_usb_to_usbdev(usb));
	init_completion(&intr->read_regs.completion);
	intr->read_regs.cr_int_addr = cpu_to_le16(usb_addr(usb, CR_INTERRUPT));
}

static inline void init_usb_rx(struct zd_usb *usb)
{
	struct zd_usb_rx *rx = &usb->rx;
	spin_lock_init(&rx->lock);
	if (interface_to_usbdev(usb->intf)->speed == USB_SPEED_HIGH) {
		rx->usb_packet_size = 512;
	} else {
		rx->usb_packet_size = 64;
	}
	ZD_ASSERT(rx->fragment_length == 0);
}

static inline void init_usb_tx(struct zd_usb *usb)
{
	/* FIXME: at this point we will allocate a fixed number of urb's for
	 * use in a cyclic scheme */
}

void zd_usb_init(struct zd_usb *usb, struct net_device *netdev,
	         struct usb_interface *intf)
{
	memset(usb, 0, sizeof(*usb));
	usb->intf = usb_get_intf(intf);
	usb_set_intfdata(usb->intf, netdev);
	init_usb_interrupt(usb);
	init_usb_tx(usb);
	init_usb_rx(usb);
}

int zd_usb_init_hw(struct zd_usb *usb)
{
	int r;
	struct zd_chip *chip = zd_usb_to_chip(usb);

	ZD_ASSERT(mutex_is_locked(&chip->mutex));
	r = zd_ioread16_locked(chip, &usb->fw_base_offset,
		        USB_REG((u16)FW_BASE_ADDR_OFFSET));
	if (r)
		return r;
	dev_dbg_f(zd_usb_dev(usb), "fw_base_offset: %#06hx\n",
		 usb->fw_base_offset);

	return 0;
}

void zd_usb_clear(struct zd_usb *usb)
{
	usb_set_intfdata(usb->intf, NULL);
	usb_put_intf(usb->intf);
	ZD_MEMCLEAR(usb, sizeof(*usb));
	/* FIXME: usb_interrupt, usb_tx, usb_rx? */
}

static const char *speed(enum usb_device_speed speed)
{
	switch (speed) {
	case USB_SPEED_LOW:
		return "low";
	case USB_SPEED_FULL:
		return "full";
	case USB_SPEED_HIGH:
		return "high";
	default:
		return "unknown speed";
	}
}

static int scnprint_id(struct usb_device *udev, char *buffer, size_t size)
{
	return scnprintf(buffer, size, "%04hx:%04hx v%04hx %s",
		le16_to_cpu(udev->descriptor.idVendor),
		le16_to_cpu(udev->descriptor.idProduct),
		get_bcdDevice(udev),
		speed(udev->speed));
}

int zd_usb_scnprint_id(struct zd_usb *usb, char *buffer, size_t size)
{
	struct usb_device *udev = interface_to_usbdev(usb->intf);
	return scnprint_id(udev, buffer, size);
}

#ifdef DEBUG
static void print_id(struct usb_device *udev)
{
	char buffer[40];

	scnprint_id(udev, buffer, sizeof(buffer));
	buffer[sizeof(buffer)-1] = 0;
	dev_dbg_f(&udev->dev, "%s\n", buffer);
}
#else
#define print_id(udev) do { } while (0)
#endif

static int eject_installer(struct usb_interface *intf)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_host_interface *iface_desc = &intf->altsetting[0];
	struct usb_endpoint_descriptor *endpoint;
	unsigned char *cmd;
	u8 bulk_out_ep;
	int r;

	/* Find bulk out endpoint */
	endpoint = &iface_desc->endpoint[1].desc;
	if ((endpoint->bEndpointAddress & USB_TYPE_MASK) == USB_DIR_OUT &&
	    (endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
	    USB_ENDPOINT_XFER_BULK) {
		bulk_out_ep = endpoint->bEndpointAddress;
	} else {
		dev_err(&udev->dev,
			"zd1211rw: Could not find bulk out endpoint\n");
		return -ENODEV;
	}

	cmd = kzalloc(31, GFP_KERNEL);
	if (cmd == NULL)
		return -ENODEV;

	/* USB bulk command block */
	cmd[0] = 0x55;	/* bulk command signature */
	cmd[1] = 0x53;	/* bulk command signature */
	cmd[2] = 0x42;	/* bulk command signature */
	cmd[3] = 0x43;	/* bulk command signature */
	cmd[14] = 6;	/* command length */

	cmd[15] = 0x1b;	/* SCSI command: START STOP UNIT */
	cmd[19] = 0x2;	/* eject disc */

	dev_info(&udev->dev, "Ejecting virtual installer media...\n");
	r = usb_bulk_msg(udev, usb_sndbulkpipe(udev, bulk_out_ep),
		cmd, 31, NULL, 2000);
	kfree(cmd);
	if (r)
		return r;

	/* At this point, the device disconnects and reconnects with the real
	 * ID numbers. */

	usb_set_intfdata(intf, NULL);
	return 0;
}

static int probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	int r;
	struct usb_device *udev = interface_to_usbdev(intf);
	struct net_device *netdev = NULL;

	print_id(udev);

	if (id->driver_info & DEVICE_INSTALLER)
		return eject_installer(intf);

	switch (udev->speed) {
	case USB_SPEED_LOW:
	case USB_SPEED_FULL:
	case USB_SPEED_HIGH:
		break;
	default:
		dev_dbg_f(&intf->dev, "Unknown USB speed\n");
		r = -ENODEV;
		goto error;
	}

	netdev = zd_netdev_alloc(intf);
	if (netdev == NULL) {
		r = -ENOMEM;
		goto error;
	}

	r = upload_firmware(udev, id->driver_info);
	if (r) {
		dev_err(&intf->dev,
		       "couldn't load firmware. Error number %d\n", r);
		goto error;
	}

	r = usb_reset_configuration(udev);
	if (r) {
		dev_dbg_f(&intf->dev,
			"couldn't reset configuration. Error number %d\n", r);
		goto error;
	}

	/* At this point the interrupt endpoint is not generally enabled. We
	 * save the USB bandwidth until the network device is opened. But
	 * notify that the initialization of the MAC will require the
	 * interrupts to be temporary enabled.
	 */
	r = zd_mac_init_hw(zd_netdev_mac(netdev), id->driver_info);
	if (r) {
		dev_dbg_f(&intf->dev,
		         "couldn't initialize mac. Error number %d\n", r);
		goto error;
	}

	r = register_netdev(netdev);
	if (r) {
		dev_dbg_f(&intf->dev,
			 "couldn't register netdev. Error number %d\n", r);
		goto error;
	}

	dev_dbg_f(&intf->dev, "successful\n");
	dev_info(&intf->dev,"%s\n", netdev->name);
	return 0;
error:
	usb_reset_device(interface_to_usbdev(intf));
	zd_netdev_free(netdev);
	return r;
}

static void disconnect(struct usb_interface *intf)
{
	struct net_device *netdev = zd_intf_to_netdev(intf);
	struct zd_mac *mac = zd_netdev_mac(netdev);
	struct zd_usb *usb = &mac->chip.usb;

	/* Either something really bad happened, or we're just dealing with
	 * a DEVICE_INSTALLER. */
	if (netdev == NULL)
		return;

	dev_dbg_f(zd_usb_dev(usb), "\n");

	zd_netdev_disconnect(netdev);

	/* Just in case something has gone wrong! */
	zd_usb_disable_rx(usb);
	zd_usb_disable_int(usb);

	/* If the disconnect has been caused by a removal of the
	 * driver module, the reset allows reloading of the driver. If the
	 * reset will not be executed here, the upload of the firmware in the
	 * probe function caused by the reloading of the driver will fail.
	 */
	usb_reset_device(interface_to_usbdev(intf));

	zd_netdev_free(netdev);
	dev_dbg(&intf->dev, "disconnected\n");
}

static struct usb_driver driver = {
	.name		= "zd1211rw",
	.id_table	= usb_ids,
	.probe		= probe,
	.disconnect	= disconnect,
};

struct workqueue_struct *zd_workqueue;

static int __init usb_init(void)
{
	int r;

	pr_debug("usb_init()\n");

	zd_workqueue = create_singlethread_workqueue(driver.name);
	if (zd_workqueue == NULL) {
		printk(KERN_ERR "%s: couldn't create workqueue\n", driver.name);
		return -ENOMEM;
	}

	r = usb_register(&driver);
	if (r) {
		printk(KERN_ERR "usb_register() failed. Error number %d\n", r);
		return r;
	}

	pr_debug("zd1211rw initialized\n");
	return 0;
}

static void __exit usb_exit(void)
{
	pr_debug("usb_exit()\n");
	usb_deregister(&driver);
	destroy_workqueue(zd_workqueue);
}

module_init(usb_init);
module_exit(usb_exit);

static int usb_int_regs_length(unsigned int count)
{
	return sizeof(struct usb_int_regs) + count * sizeof(struct reg_data);
}

static void prepare_read_regs_int(struct zd_usb *usb)
{
	struct zd_usb_interrupt *intr = &usb->intr;

	spin_lock_irq(&intr->lock);
	intr->read_regs_enabled = 1;
	INIT_COMPLETION(intr->read_regs.completion);
	spin_unlock_irq(&intr->lock);
}

static void disable_read_regs_int(struct zd_usb *usb)
{
	struct zd_usb_interrupt *intr = &usb->intr;

	spin_lock_irq(&intr->lock);
	intr->read_regs_enabled = 0;
	spin_unlock_irq(&intr->lock);
}

static int get_results(struct zd_usb *usb, u16 *values,
	               struct usb_req_read_regs *req, unsigned int count)
{
	int r;
	int i;
	struct zd_usb_interrupt *intr = &usb->intr;
	struct read_regs_int *rr = &intr->read_regs;
	struct usb_int_regs *regs = (struct usb_int_regs *)rr->buffer;

	spin_lock_irq(&intr->lock);

	r = -EIO;
	/* The created block size seems to be larger than expected.
	 * However results appear to be correct.
	 */
	if (rr->length < usb_int_regs_length(count)) {
		dev_dbg_f(zd_usb_dev(usb),
			 "error: actual length %d less than expected %d\n",
			 rr->length, usb_int_regs_length(count));
		goto error_unlock;
	}
	if (rr->length > sizeof(rr->buffer)) {
		dev_dbg_f(zd_usb_dev(usb),
			 "error: actual length %d exceeds buffer size %zu\n",
			 rr->length, sizeof(rr->buffer));
		goto error_unlock;
	}

	for (i = 0; i < count; i++) {
		struct reg_data *rd = &regs->regs[i];
		if (rd->addr != req->addr[i]) {
			dev_dbg_f(zd_usb_dev(usb),
				 "rd[%d] addr %#06hx expected %#06hx\n", i,
				 le16_to_cpu(rd->addr),
				 le16_to_cpu(req->addr[i]));
			goto error_unlock;
		}
		values[i] = le16_to_cpu(rd->value);
	}

	r = 0;
error_unlock:
	spin_unlock_irq(&intr->lock);
	return r;
}

int zd_usb_ioread16v(struct zd_usb *usb, u16 *values,
	             const zd_addr_t *addresses, unsigned int count)
{
	int r;
	int i, req_len, actual_req_len;
	struct usb_device *udev;
	struct usb_req_read_regs *req = NULL;
	unsigned long timeout;

	if (count < 1) {
		dev_dbg_f(zd_usb_dev(usb), "error: count is zero\n");
		return -EINVAL;
	}
	if (count > USB_MAX_IOREAD16_COUNT) {
		dev_dbg_f(zd_usb_dev(usb),
			 "error: count %u exceeds possible max %u\n",
			 count, USB_MAX_IOREAD16_COUNT);
		return -EINVAL;
	}
	if (in_atomic()) {
		dev_dbg_f(zd_usb_dev(usb),
			 "error: io in atomic context not supported\n");
		return -EWOULDBLOCK;
	}
	if (!usb_int_enabled(usb)) {
		 dev_dbg_f(zd_usb_dev(usb),
			  "error: usb interrupt not enabled\n");
		return -EWOULDBLOCK;
	}

	req_len = sizeof(struct usb_req_read_regs) + count * sizeof(__le16);
	req = kmalloc(req_len, GFP_NOFS);
	if (!req)
		return -ENOMEM;
	req->id = cpu_to_le16(USB_REQ_READ_REGS);
	for (i = 0; i < count; i++)
		req->addr[i] = cpu_to_le16(usb_addr(usb, addresses[i]));

	udev = zd_usb_to_usbdev(usb);
	prepare_read_regs_int(usb);
	r = usb_bulk_msg(udev, usb_sndbulkpipe(udev, EP_REGS_OUT),
		         req, req_len, &actual_req_len, 1000 /* ms */);
	if (r) {
		dev_dbg_f(zd_usb_dev(usb),
			"error in usb_bulk_msg(). Error number %d\n", r);
		goto error;
	}
	if (req_len != actual_req_len) {
		dev_dbg_f(zd_usb_dev(usb), "error in usb_bulk_msg()\n"
			" req_len %d != actual_req_len %d\n",
			req_len, actual_req_len);
		r = -EIO;
		goto error;
	}

	timeout = wait_for_completion_timeout(&usb->intr.read_regs.completion,
	                                      msecs_to_jiffies(1000));
	if (!timeout) {
		disable_read_regs_int(usb);
		dev_dbg_f(zd_usb_dev(usb), "read timed out\n");
		r = -ETIMEDOUT;
		goto error;
	}

	r = get_results(usb, values, req, count);
error:
	kfree(req);
	return r;
}

int zd_usb_iowrite16v(struct zd_usb *usb, const struct zd_ioreq16 *ioreqs,
	              unsigned int count)
{
	int r;
	struct usb_device *udev;
	struct usb_req_write_regs *req = NULL;
	int i, req_len, actual_req_len;

	if (count == 0)
		return 0;
	if (count > USB_MAX_IOWRITE16_COUNT) {
		dev_dbg_f(zd_usb_dev(usb),
			"error: count %u exceeds possible max %u\n",
			count, USB_MAX_IOWRITE16_COUNT);
		return -EINVAL;
	}
	if (in_atomic()) {
		dev_dbg_f(zd_usb_dev(usb),
			"error: io in atomic context not supported\n");
		return -EWOULDBLOCK;
	}

	req_len = sizeof(struct usb_req_write_regs) +
		  count * sizeof(struct reg_data);
	req = kmalloc(req_len, GFP_NOFS);
	if (!req)
		return -ENOMEM;

	req->id = cpu_to_le16(USB_REQ_WRITE_REGS);
	for (i = 0; i < count; i++) {
		struct reg_data *rw  = &req->reg_writes[i];
		rw->addr = cpu_to_le16(usb_addr(usb, ioreqs[i].addr));
		rw->value = cpu_to_le16(ioreqs[i].value);
	}

	udev = zd_usb_to_usbdev(usb);
	r = usb_bulk_msg(udev, usb_sndbulkpipe(udev, EP_REGS_OUT),
		         req, req_len, &actual_req_len, 1000 /* ms */);
	if (r) {
		dev_dbg_f(zd_usb_dev(usb),
			"error in usb_bulk_msg(). Error number %d\n", r);
		goto error;
	}
	if (req_len != actual_req_len) {
		dev_dbg_f(zd_usb_dev(usb),
			"error in usb_bulk_msg()"
			" req_len %d != actual_req_len %d\n",
			req_len, actual_req_len);
		r = -EIO;
		goto error;
	}

	/* FALL-THROUGH with r == 0 */
error:
	kfree(req);
	return r;
}

int zd_usb_rfwrite(struct zd_usb *usb, u32 value, u8 bits)
{
	int r;
	struct usb_device *udev;
	struct usb_req_rfwrite *req = NULL;
	int i, req_len, actual_req_len;
	u16 bit_value_template;

	if (in_atomic()) {
		dev_dbg_f(zd_usb_dev(usb),
			"error: io in atomic context not supported\n");
		return -EWOULDBLOCK;
	}
	if (bits < USB_MIN_RFWRITE_BIT_COUNT) {
		dev_dbg_f(zd_usb_dev(usb),
			"error: bits %d are smaller than"
			" USB_MIN_RFWRITE_BIT_COUNT %d\n",
			bits, USB_MIN_RFWRITE_BIT_COUNT);
		return -EINVAL;
	}
	if (bits > USB_MAX_RFWRITE_BIT_COUNT) {
		dev_dbg_f(zd_usb_dev(usb),
			"error: bits %d exceed USB_MAX_RFWRITE_BIT_COUNT %d\n",
			bits, USB_MAX_RFWRITE_BIT_COUNT);
		return -EINVAL;
	}
#ifdef DEBUG
	if (value & (~0UL << bits)) {
		dev_dbg_f(zd_usb_dev(usb),
			"error: value %#09x has bits >= %d set\n",
			value, bits);
		return -EINVAL;
	}
#endif /* DEBUG */

	dev_dbg_f(zd_usb_dev(usb), "value %#09x bits %d\n", value, bits);

	r = zd_usb_ioread16(usb, &bit_value_template, CR203);
	if (r) {
		dev_dbg_f(zd_usb_dev(usb),
			"error %d: Couldn't read CR203\n", r);
		goto out;
	}
	bit_value_template &= ~(RF_IF_LE|RF_CLK|RF_DATA);

	req_len = sizeof(struct usb_req_rfwrite) + bits * sizeof(__le16);
	req = kmalloc(req_len, GFP_NOFS);
	if (!req)
		return -ENOMEM;

	req->id = cpu_to_le16(USB_REQ_WRITE_RF);
	/* 1: 3683a, but not used in ZYDAS driver */
	req->value = cpu_to_le16(2);
	req->bits = cpu_to_le16(bits);

	for (i = 0; i < bits; i++) {
		u16 bv = bit_value_template;
		if (value & (1 << (bits-1-i)))
			bv |= RF_DATA;
		req->bit_values[i] = cpu_to_le16(bv);
	}

	udev = zd_usb_to_usbdev(usb);
	r = usb_bulk_msg(udev, usb_sndbulkpipe(udev, EP_REGS_OUT),
		         req, req_len, &actual_req_len, 1000 /* ms */);
	if (r) {
		dev_dbg_f(zd_usb_dev(usb),
			"error in usb_bulk_msg(). Error number %d\n", r);
		goto out;
	}
	if (req_len != actual_req_len) {
		dev_dbg_f(zd_usb_dev(usb), "error in usb_bulk_msg()"
			" req_len %d != actual_req_len %d\n",
			req_len, actual_req_len);
		r = -EIO;
		goto out;
	}

	/* FALL-THROUGH with r == 0 */
out:
	kfree(req);
	return r;
}
