/*
 * Intel Wireless UWB Link 1480
 * USB SKU firmware upload implementation
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * This driver will prepare the i1480 device to behave as a real
 * Wireless USB HWA adaptor by uploading the firmware.
 *
 * When the device is connected or driver is loaded, i1480_usb_probe()
 * is called--this will allocate and initialize the device structure,
 * fill in the pointers to the common functions (read, write,
 * wait_init_done and cmd for HWA command execution) and once that is
 * done, call the common firmware uploading routine. Then clean up and
 * return -ENODEV, as we don't attach to the device.
 *
 * The rest are the basic ops we implement that the fw upload code
 * uses to do its job. All the ops in the common code are i1480->NAME,
 * the functions are i1480_usb_NAME().
 */
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/uwb.h>
#include <linux/usb/wusb.h>
#include <linux/usb/wusb-wa.h>
#include "i1480-dfu.h"

struct i1480_usb {
	struct i1480 i1480;
	struct usb_device *usb_dev;
	struct usb_interface *usb_iface;
	struct urb *neep_urb;	/* URB for reading from EP1 */
};


static
void i1480_usb_init(struct i1480_usb *i1480_usb)
{
	i1480_init(&i1480_usb->i1480);
}


static
int i1480_usb_create(struct i1480_usb *i1480_usb, struct usb_interface *iface)
{
	struct usb_device *usb_dev = interface_to_usbdev(iface);
	int result = -ENOMEM;

	i1480_usb->usb_dev = usb_get_dev(usb_dev);	/* bind the USB device */
	i1480_usb->usb_iface = usb_get_intf(iface);
	usb_set_intfdata(iface, i1480_usb);		/* Bind the driver to iface0 */
	i1480_usb->neep_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (i1480_usb->neep_urb == NULL)
		goto error;
	return 0;

error:
	usb_set_intfdata(iface, NULL);
	usb_put_intf(iface);
	usb_put_dev(usb_dev);
	return result;
}


static
void i1480_usb_destroy(struct i1480_usb *i1480_usb)
{
	usb_kill_urb(i1480_usb->neep_urb);
	usb_free_urb(i1480_usb->neep_urb);
	usb_set_intfdata(i1480_usb->usb_iface, NULL);
	usb_put_intf(i1480_usb->usb_iface);
	usb_put_dev(i1480_usb->usb_dev);
}


/**
 * Write a buffer to a memory address in the i1480 device
 *
 * @i1480:  i1480 instance
 * @memory_address:
 *          Address where to write the data buffer to.
 * @buffer: Buffer to the data
 * @size:   Size of the buffer [has to be < 512].
 * @returns: 0 if ok, < 0 errno code on error.
 *
 * Data buffers to USB cannot be on the stack or in vmalloc'ed areas,
 * so we copy it to the local i1480 buffer before proceeding. In any
 * case, we have a max size we can send.
 */
static
int i1480_usb_write(struct i1480 *i1480, u32 memory_address,
		    const void *buffer, size_t size)
{
	int result = 0;
	struct i1480_usb *i1480_usb = container_of(i1480, struct i1480_usb, i1480);
	size_t buffer_size, itr = 0;

	BUG_ON(size & 0x3); /* Needs to be a multiple of 4 */
	while (size > 0) {
		buffer_size = size < i1480->buf_size ? size : i1480->buf_size;
		memcpy(i1480->cmd_buf, buffer + itr, buffer_size);
		result = usb_control_msg(
			i1480_usb->usb_dev, usb_sndctrlpipe(i1480_usb->usb_dev, 0),
			0xf0, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			memory_address,	(memory_address >> 16),
			i1480->cmd_buf, buffer_size, 100 /* FIXME: arbitrary */);
		if (result < 0)
			break;
		itr += result;
		memory_address += result;
		size -= result;
	}
	return result;
}


/**
 * Read a block [max size 512] of the device's memory to @i1480's buffer.
 *
 * @i1480: i1480 instance
 * @memory_address:
 *         Address where to read from.
 * @size:  Size to read. Smaller than or equal to 512.
 * @returns: >= 0 number of bytes written if ok, < 0 errno code on error.
 *
 * NOTE: if the memory address or block is incorrect, you might get a
 *       stall or a different memory read. Caller has to verify the
 *       memory address and size passed back in the @neh structure.
 */
static
int i1480_usb_read(struct i1480 *i1480, u32 addr, size_t size)
{
	ssize_t result = 0, bytes = 0;
	size_t itr, read_size = i1480->buf_size;
	struct i1480_usb *i1480_usb = container_of(i1480, struct i1480_usb, i1480);

	BUG_ON(size > i1480->buf_size);
	BUG_ON(size & 0x3); /* Needs to be a multiple of 4 */
	BUG_ON(read_size > 512);

	if (addr >= 0x8000d200 && addr < 0x8000d400)	/* Yeah, HW quirk */
		read_size = 4;

	for (itr = 0; itr < size; itr += read_size) {
		size_t itr_addr = addr + itr;
		size_t itr_size = min(read_size, size - itr);
		result = usb_control_msg(
			i1480_usb->usb_dev, usb_rcvctrlpipe(i1480_usb->usb_dev, 0),
			0xf0, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			itr_addr, (itr_addr >> 16),
			i1480->cmd_buf + itr, itr_size,
			100 /* FIXME: arbitrary */);
		if (result < 0) {
			dev_err(i1480->dev, "%s: USB read error: %zd\n",
				__func__, result);
			goto out;
		}
		if (result != itr_size) {
			result = -EIO;
			dev_err(i1480->dev,
				"%s: partial read got only %zu bytes vs %zu expected\n",
				__func__, result, itr_size);
			goto out;
		}
		bytes += result;
	}
	result = bytes;
out:
	return result;
}


/**
 * Callback for reads on the notification/event endpoint
 *
 * Just enables the completion read handler.
 */
static
void i1480_usb_neep_cb(struct urb *urb)
{
	struct i1480 *i1480 = urb->context;
	struct device *dev = i1480->dev;

	switch (urb->status) {
	case 0:
		break;
	case -ECONNRESET:	/* Not an error, but a controlled situation; */
	case -ENOENT:		/* (we killed the URB)...so, no broadcast */
		dev_dbg(dev, "NEEP: reset/noent %d\n", urb->status);
		break;
	case -ESHUTDOWN:	/* going away! */
		dev_dbg(dev, "NEEP: down %d\n", urb->status);
		break;
	default:
		dev_err(dev, "NEEP: unknown status %d\n", urb->status);
		break;
	}
	i1480->evt_result = urb->actual_length;
	complete(&i1480->evt_complete);
	return;
}


/**
 * Wait for the MAC FW to initialize
 *
 * MAC FW sends a 0xfd/0101/00 notification to EP1 when done
 * initializing. Get that notification into i1480->evt_buf; upper layer
 * will verify it.
 *
 * Set i1480->evt_result with the result of getting the event or its
 * size (if successful).
 *
 * Delivers the data directly to i1480->evt_buf
 */
static
int i1480_usb_wait_init_done(struct i1480 *i1480)
{
	int result;
	struct device *dev = i1480->dev;
	struct i1480_usb *i1480_usb = container_of(i1480, struct i1480_usb, i1480);
	struct usb_endpoint_descriptor *epd;

	init_completion(&i1480->evt_complete);
	i1480->evt_result = -EINPROGRESS;
	epd = &i1480_usb->usb_iface->cur_altsetting->endpoint[0].desc;
	usb_fill_int_urb(i1480_usb->neep_urb, i1480_usb->usb_dev,
			 usb_rcvintpipe(i1480_usb->usb_dev, epd->bEndpointAddress),
			 i1480->evt_buf, i1480->buf_size,
			 i1480_usb_neep_cb, i1480, epd->bInterval);
	result = usb_submit_urb(i1480_usb->neep_urb, GFP_KERNEL);
	if (result < 0) {
		dev_err(dev, "init done: cannot submit NEEP read: %d\n",
			result);
		goto error_submit;
	}
	/* Wait for the USB callback to get the data */
	result = wait_for_completion_interruptible_timeout(
		&i1480->evt_complete, HZ);
	if (result <= 0) {
		result = result == 0 ? -ETIMEDOUT : result;
		goto error_wait;
	}
	usb_kill_urb(i1480_usb->neep_urb);
	return 0;

error_wait:
	usb_kill_urb(i1480_usb->neep_urb);
error_submit:
	i1480->evt_result = result;
	return result;
}


/**
 * Generic function for issuing commands to the i1480
 *
 * @i1480:      i1480 instance
 * @cmd_name:   Name of the command (for error messages)
 * @cmd:        Pointer to command buffer
 * @cmd_size:   Size of the command buffer
 * @reply:      Buffer for the reply event
 * @reply_size: Expected size back (including RCEB); the reply buffer
 *              is assumed to be as big as this.
 * @returns:    >= 0 size of the returned event data if ok,
 *              < 0 errno code on error.
 *
 * Arms the NE handle, issues the command to the device and checks the
 * basics of the reply event.
 */
static
int i1480_usb_cmd(struct i1480 *i1480, const char *cmd_name, size_t cmd_size)
{
	int result;
	struct device *dev = i1480->dev;
	struct i1480_usb *i1480_usb = container_of(i1480, struct i1480_usb, i1480);
	struct usb_endpoint_descriptor *epd;
	struct uwb_rccb *cmd = i1480->cmd_buf;
	u8 iface_no;

	/* Post a read on the notification & event endpoint */
	iface_no = i1480_usb->usb_iface->cur_altsetting->desc.bInterfaceNumber;
	epd = &i1480_usb->usb_iface->cur_altsetting->endpoint[0].desc;
	usb_fill_int_urb(
		i1480_usb->neep_urb, i1480_usb->usb_dev,
		usb_rcvintpipe(i1480_usb->usb_dev, epd->bEndpointAddress),
		i1480->evt_buf, i1480->buf_size,
		i1480_usb_neep_cb, i1480, epd->bInterval);
	result = usb_submit_urb(i1480_usb->neep_urb, GFP_KERNEL);
	if (result < 0) {
		dev_err(dev, "%s: cannot submit NEEP read: %d\n",
			cmd_name, result);
			goto error_submit_ep1;
	}
	/* Now post the command on EP0 */
	result = usb_control_msg(
		i1480_usb->usb_dev, usb_sndctrlpipe(i1480_usb->usb_dev, 0),
		WA_EXEC_RC_CMD,
		USB_DIR_OUT | USB_RECIP_INTERFACE | USB_TYPE_CLASS,
		0, iface_no,
		cmd, cmd_size,
		100 /* FIXME: this is totally arbitrary */);
	if (result < 0) {
		dev_err(dev, "%s: control request failed: %d\n",
			cmd_name, result);
		goto error_submit_ep0;
	}
	return result;

error_submit_ep0:
	usb_kill_urb(i1480_usb->neep_urb);
error_submit_ep1:
	return result;
}


/*
 * Probe a i1480 device for uploading firmware.
 *
 * We attach only to interface #0, which is the radio control interface.
 */
static
int i1480_usb_probe(struct usb_interface *iface, const struct usb_device_id *id)
{
	struct i1480_usb *i1480_usb;
	struct i1480 *i1480;
	struct device *dev = &iface->dev;
	int result;

	result = -ENODEV;
	if (iface->cur_altsetting->desc.bInterfaceNumber != 0) {
		dev_dbg(dev, "not attaching to iface %d\n",
			iface->cur_altsetting->desc.bInterfaceNumber);
		goto error;
	}
	if (iface->num_altsetting > 1
	    && interface_to_usbdev(iface)->descriptor.idProduct == 0xbabe) {
		/* Need altsetting #1 [HW QUIRK] or EP1 won't work */
		result = usb_set_interface(interface_to_usbdev(iface), 0, 1);
		if (result < 0)
			dev_warn(dev,
				 "can't set altsetting 1 on iface 0: %d\n",
				 result);
	}

	if (iface->cur_altsetting->desc.bNumEndpoints < 1)
		return -ENODEV;

	result = -ENOMEM;
	i1480_usb = kzalloc(sizeof(*i1480_usb), GFP_KERNEL);
	if (i1480_usb == NULL) {
		dev_err(dev, "Unable to allocate instance\n");
		goto error;
	}
	i1480_usb_init(i1480_usb);

	i1480 = &i1480_usb->i1480;
	i1480->buf_size = 512;
	i1480->cmd_buf = kmalloc(2 * i1480->buf_size, GFP_KERNEL);
	if (i1480->cmd_buf == NULL) {
		dev_err(dev, "Cannot allocate transfer buffers\n");
		result = -ENOMEM;
		goto error_buf_alloc;
	}
	i1480->evt_buf = i1480->cmd_buf + i1480->buf_size;

	result = i1480_usb_create(i1480_usb, iface);
	if (result < 0) {
		dev_err(dev, "Cannot create instance: %d\n", result);
		goto error_create;
	}

	/* setup the fops and upload the firmware */
	i1480->pre_fw_name = "i1480-pre-phy-0.0.bin";
	i1480->mac_fw_name = "i1480-usb-0.0.bin";
	i1480->mac_fw_name_deprecate = "ptc-0.0.bin";
	i1480->phy_fw_name = "i1480-phy-0.0.bin";
	i1480->dev = &iface->dev;
	i1480->write = i1480_usb_write;
	i1480->read = i1480_usb_read;
	i1480->rc_setup = NULL;
	i1480->wait_init_done = i1480_usb_wait_init_done;
	i1480->cmd = i1480_usb_cmd;

	result = i1480_fw_upload(&i1480_usb->i1480);	/* the real thing */
	if (result >= 0) {
		usb_reset_device(i1480_usb->usb_dev);
		result = -ENODEV;	/* we don't want to bind to the iface */
	}
	i1480_usb_destroy(i1480_usb);
error_create:
	kfree(i1480->cmd_buf);
error_buf_alloc:
	kfree(i1480_usb);
error:
	return result;
}

MODULE_FIRMWARE("i1480-pre-phy-0.0.bin");
MODULE_FIRMWARE("i1480-usb-0.0.bin");
MODULE_FIRMWARE("i1480-phy-0.0.bin");

#define i1480_USB_DEV(v, p)				\
{							\
	.match_flags = USB_DEVICE_ID_MATCH_DEVICE	\
		 | USB_DEVICE_ID_MATCH_DEV_INFO		\
		 | USB_DEVICE_ID_MATCH_INT_INFO,	\
	.idVendor = (v),				\
	.idProduct = (p),				\
	.bDeviceClass = 0xff,				\
	.bDeviceSubClass = 0xff,			\
	.bDeviceProtocol = 0xff,			\
	.bInterfaceClass = 0xff,			\
	.bInterfaceSubClass = 0xff,			\
	.bInterfaceProtocol = 0xff,			\
}


/** USB device ID's that we handle */
static const struct usb_device_id i1480_usb_id_table[] = {
	i1480_USB_DEV(0x8086, 0xdf3b),
	i1480_USB_DEV(0x15a9, 0x0005),
	i1480_USB_DEV(0x07d1, 0x3802),
	i1480_USB_DEV(0x050d, 0x305a),
	i1480_USB_DEV(0x3495, 0x3007),
	{},
};
MODULE_DEVICE_TABLE(usb, i1480_usb_id_table);


static struct usb_driver i1480_dfu_driver = {
	.name =		"i1480-dfu-usb",
	.id_table =	i1480_usb_id_table,
	.probe =	i1480_usb_probe,
	.disconnect =	NULL,
};

module_usb_driver(i1480_dfu_driver);

MODULE_AUTHOR("Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>");
MODULE_DESCRIPTION("Intel Wireless UWB Link 1480 firmware uploader for USB");
MODULE_LICENSE("GPL");
