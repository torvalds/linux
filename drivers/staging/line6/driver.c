/*
 * Line6 Linux USB driver - 0.9.1beta
 *
 * Copyright (C) 2004-2010 Markus Grabner (grabner@icg.tugraz.at)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include "audio.h"
#include "capture.h"
#include "driver.h"
#include "midi.h"
#include "playback.h"
#include "pod.h"
#include "podhd.h"
#include "revision.h"
#include "toneport.h"
#include "usbdefs.h"
#include "variax.h"

#define DRIVER_AUTHOR  "Markus Grabner <grabner@icg.tugraz.at>"
#define DRIVER_DESC    "Line6 USB Driver"
#define DRIVER_VERSION "0.9.1beta" DRIVER_REVISION

/* table of devices that work with this driver */
static const struct usb_device_id line6_id_table[] = {
	{USB_DEVICE(LINE6_VENDOR_ID, LINE6_DEVID_BASSPODXT)},
	{USB_DEVICE(LINE6_VENDOR_ID, LINE6_DEVID_BASSPODXTLIVE)},
	{USB_DEVICE(LINE6_VENDOR_ID, LINE6_DEVID_BASSPODXTPRO)},
	{USB_DEVICE(LINE6_VENDOR_ID, LINE6_DEVID_GUITARPORT)},
	{USB_DEVICE(LINE6_VENDOR_ID, LINE6_DEVID_POCKETPOD)},
	{USB_DEVICE(LINE6_VENDOR_ID, LINE6_DEVID_PODHD300)},
	{USB_DEVICE(LINE6_VENDOR_ID, LINE6_DEVID_PODHD400)},
	{USB_DEVICE(LINE6_VENDOR_ID, LINE6_DEVID_PODHD500)},
	{USB_DEVICE(LINE6_VENDOR_ID, LINE6_DEVID_PODSTUDIO_GX)},
	{USB_DEVICE(LINE6_VENDOR_ID, LINE6_DEVID_PODSTUDIO_UX1)},
	{USB_DEVICE(LINE6_VENDOR_ID, LINE6_DEVID_PODSTUDIO_UX2)},
	{USB_DEVICE(LINE6_VENDOR_ID, LINE6_DEVID_PODX3)},
	{USB_DEVICE(LINE6_VENDOR_ID, LINE6_DEVID_PODX3LIVE)},
	{USB_DEVICE(LINE6_VENDOR_ID, LINE6_DEVID_PODXT)},
	{USB_DEVICE(LINE6_VENDOR_ID, LINE6_DEVID_PODXTLIVE)},
	{USB_DEVICE(LINE6_VENDOR_ID, LINE6_DEVID_PODXTPRO)},
	{USB_DEVICE(LINE6_VENDOR_ID, LINE6_DEVID_TONEPORT_GX)},
	{USB_DEVICE(LINE6_VENDOR_ID, LINE6_DEVID_TONEPORT_UX1)},
	{USB_DEVICE(LINE6_VENDOR_ID, LINE6_DEVID_TONEPORT_UX2)},
	{USB_DEVICE(LINE6_VENDOR_ID, LINE6_DEVID_VARIAX)},
	{},
};

MODULE_DEVICE_TABLE(usb, line6_id_table);

#define L6PROP(dev_bit, dev_id, dev_name, dev_cap)\
	{.device_bit = LINE6_BIT_##dev_bit, .id = dev_id,\
	 .name = dev_name, .capabilities = LINE6_BIT_##dev_cap}

/* *INDENT-OFF* */
static const struct line6_properties line6_properties_table[] = {
	L6PROP(BASSPODXT,     "BassPODxt",     "BassPODxt",        CTRL_PCM_HW),
	L6PROP(BASSPODXTLIVE, "BassPODxtLive", "BassPODxt Live",   CTRL_PCM_HW),
	L6PROP(BASSPODXTPRO,  "BassPODxtPro",  "BassPODxt Pro",    CTRL_PCM_HW),
	L6PROP(GUITARPORT,    "GuitarPort",    "GuitarPort",       PCM),
	L6PROP(POCKETPOD,     "PocketPOD",     "Pocket POD",       CONTROL),
	L6PROP(PODHD300,      "PODHD300",      "POD HD300",        CTRL_PCM_HW),
	L6PROP(PODHD400,      "PODHD400",      "POD HD400",        CTRL_PCM_HW),
	L6PROP(PODHD500,      "PODHD500",      "POD HD500",        CTRL_PCM_HW),
	L6PROP(PODSTUDIO_GX,  "PODStudioGX",   "POD Studio GX",    PCM),
	L6PROP(PODSTUDIO_UX1, "PODStudioUX1",  "POD Studio UX1",   PCM),
	L6PROP(PODSTUDIO_UX2, "PODStudioUX2",  "POD Studio UX2",   PCM),
	L6PROP(PODX3,         "PODX3",         "POD X3",           PCM),
	L6PROP(PODX3LIVE,     "PODX3Live",     "POD X3 Live",      PCM),
	L6PROP(PODXT,         "PODxt",         "PODxt",            CTRL_PCM_HW),
	L6PROP(PODXTLIVE,     "PODxtLive",     "PODxt Live",       CTRL_PCM_HW),
	L6PROP(PODXTPRO,      "PODxtPro",      "PODxt Pro",        CTRL_PCM_HW),
	L6PROP(TONEPORT_GX,   "TonePortGX",    "TonePort GX",      PCM),
	L6PROP(TONEPORT_UX1,  "TonePortUX1",   "TonePort UX1",     PCM),
	L6PROP(TONEPORT_UX2,  "TonePortUX2",   "TonePort UX2",     PCM),
	L6PROP(VARIAX,        "Variax",        "Variax Workbench", CONTROL),
};
/* *INDENT-ON* */

/*
	This is Line6's MIDI manufacturer ID.
*/
const unsigned char line6_midi_id[] = {
	0x00, 0x01, 0x0c
};

/*
	Code to request version of POD, Variax interface
	(and maybe other devices).
*/
static const char line6_request_version[] = {
	0xf0, 0x7e, 0x7f, 0x06, 0x01, 0xf7
};

/**
	 Class for asynchronous messages.
*/
struct message {
	struct usb_line6 *line6;
	const char *buffer;
	int size;
	int done;
};

/*
	Forward declarations.
*/
static void line6_data_received(struct urb *urb);
static int line6_send_raw_message_async_part(struct message *msg,
					     struct urb *urb);

/*
	Start to listen on endpoint.
*/
static int line6_start_listen(struct usb_line6 *line6)
{
	int err;

	usb_fill_int_urb(line6->urb_listen, line6->usbdev,
			 usb_rcvintpipe(line6->usbdev, line6->ep_control_read),
			 line6->buffer_listen, LINE6_BUFSIZE_LISTEN,
			 line6_data_received, line6, line6->interval);
	line6->urb_listen->actual_length = 0;
	err = usb_submit_urb(line6->urb_listen, GFP_ATOMIC);
	return err;
}

/*
	Stop listening on endpoint.
*/
static void line6_stop_listen(struct usb_line6 *line6)
{
	usb_kill_urb(line6->urb_listen);
}

/*
	Send raw message in pieces of wMaxPacketSize bytes.
*/
int line6_send_raw_message(struct usb_line6 *line6, const char *buffer,
			   int size)
{
	int i, done = 0;

	for (i = 0; i < size; i += line6->max_packet_size) {
		int partial;
		const char *frag_buf = buffer + i;
		int frag_size = min(line6->max_packet_size, size - i);
		int retval;

		retval = usb_interrupt_msg(line6->usbdev,
					usb_sndintpipe(line6->usbdev,
						line6->ep_control_write),
					(char *)frag_buf, frag_size,
					&partial, LINE6_TIMEOUT * HZ);

		if (retval) {
			dev_err(line6->ifcdev,
				"usb_interrupt_msg failed (%d)\n", retval);
			break;
		}

		done += frag_size;
	}

	return done;
}

/*
	Notification of completion of asynchronous request transmission.
*/
static void line6_async_request_sent(struct urb *urb)
{
	struct message *msg = (struct message *)urb->context;

	if (msg->done >= msg->size) {
		usb_free_urb(urb);
		kfree(msg);
	} else
		line6_send_raw_message_async_part(msg, urb);
}

/*
	Asynchronously send part of a raw message.
*/
static int line6_send_raw_message_async_part(struct message *msg,
					     struct urb *urb)
{
	int retval;
	struct usb_line6 *line6 = msg->line6;
	int done = msg->done;
	int bytes = min(msg->size - done, line6->max_packet_size);

	usb_fill_int_urb(urb, line6->usbdev,
			 usb_sndintpipe(line6->usbdev, line6->ep_control_write),
			 (char *)msg->buffer + done, bytes,
			 line6_async_request_sent, msg, line6->interval);

	msg->done += bytes;
	retval = usb_submit_urb(urb, GFP_ATOMIC);

	if (retval < 0) {
		dev_err(line6->ifcdev, "%s: usb_submit_urb failed (%d)\n",
			__func__, retval);
		usb_free_urb(urb);
		kfree(msg);
		return retval;
	}

	return 0;
}

/*
	Setup and start timer.
*/
void line6_start_timer(struct timer_list *timer, unsigned int msecs,
		       void (*function)(unsigned long), unsigned long data)
{
	setup_timer(timer, function, data);
	timer->expires = jiffies + msecs * HZ / 1000;
	add_timer(timer);
}

/*
	Asynchronously send raw message.
*/
int line6_send_raw_message_async(struct usb_line6 *line6, const char *buffer,
				 int size)
{
	struct message *msg;
	struct urb *urb;

	/* create message: */
	msg = kmalloc(sizeof(struct message), GFP_ATOMIC);
	if (msg == NULL)
		return -ENOMEM;

	/* create URB: */
	urb = usb_alloc_urb(0, GFP_ATOMIC);

	if (urb == NULL) {
		kfree(msg);
		dev_err(line6->ifcdev, "Out of memory\n");
		return -ENOMEM;
	}

	/* set message data: */
	msg->line6 = line6;
	msg->buffer = buffer;
	msg->size = size;
	msg->done = 0;

	/* start sending: */
	return line6_send_raw_message_async_part(msg, urb);
}

/*
	Send asynchronous device version request.
*/
int line6_version_request_async(struct usb_line6 *line6)
{
	char *buffer;
	int retval;

	buffer = kmemdup(line6_request_version,
			sizeof(line6_request_version), GFP_ATOMIC);
	if (buffer == NULL) {
		dev_err(line6->ifcdev, "Out of memory");
		return -ENOMEM;
	}

	retval = line6_send_raw_message_async(line6, buffer,
					      sizeof(line6_request_version));
	kfree(buffer);
	return retval;
}

/*
	Send sysex message in pieces of wMaxPacketSize bytes.
*/
int line6_send_sysex_message(struct usb_line6 *line6, const char *buffer,
			     int size)
{
	return line6_send_raw_message(line6, buffer,
				      size + SYSEX_EXTRA_SIZE) -
	    SYSEX_EXTRA_SIZE;
}

/*
	Allocate buffer for sysex message and prepare header.
	@param code sysex message code
	@param size number of bytes between code and sysex end
*/
char *line6_alloc_sysex_buffer(struct usb_line6 *line6, int code1, int code2,
			       int size)
{
	char *buffer = kmalloc(size + SYSEX_EXTRA_SIZE, GFP_ATOMIC);

	if (!buffer)
		return NULL;

	buffer[0] = LINE6_SYSEX_BEGIN;
	memcpy(buffer + 1, line6_midi_id, sizeof(line6_midi_id));
	buffer[sizeof(line6_midi_id) + 1] = code1;
	buffer[sizeof(line6_midi_id) + 2] = code2;
	buffer[sizeof(line6_midi_id) + 3 + size] = LINE6_SYSEX_END;
	return buffer;
}

/*
	Notification of data received from the Line6 device.
*/
static void line6_data_received(struct urb *urb)
{
	struct usb_line6 *line6 = (struct usb_line6 *)urb->context;
	struct midi_buffer *mb = &line6->line6midi->midibuf_in;
	int done;

	if (urb->status == -ESHUTDOWN)
		return;

	done =
	    line6_midibuf_write(mb, urb->transfer_buffer, urb->actual_length);

	if (done < urb->actual_length) {
		line6_midibuf_ignore(mb, done);
		dev_dbg(line6->ifcdev, "%d %d buffer overflow - message skipped\n",
			done, urb->actual_length);
	}

	for (;;) {
		done =
		    line6_midibuf_read(mb, line6->buffer_message,
				       LINE6_MESSAGE_MAXLEN);

		if (done == 0)
			break;

		line6->message_length = done;
		line6_midi_receive(line6, line6->buffer_message, done);

		switch (le16_to_cpu(line6->usbdev->descriptor.idProduct)) {
		case LINE6_DEVID_BASSPODXT:
		case LINE6_DEVID_BASSPODXTLIVE:
		case LINE6_DEVID_BASSPODXTPRO:
		case LINE6_DEVID_PODXT:
		case LINE6_DEVID_PODXTPRO:
		case LINE6_DEVID_POCKETPOD:
			line6_pod_process_message((struct usb_line6_pod *)
						  line6);
			break;

		case LINE6_DEVID_PODHD300:
		case LINE6_DEVID_PODHD400:
		case LINE6_DEVID_PODHD500:
			break; /* let userspace handle MIDI */

		case LINE6_DEVID_PODXTLIVE:
			switch (line6->interface_number) {
			case PODXTLIVE_INTERFACE_POD:
				line6_pod_process_message((struct usb_line6_pod
							   *)line6);
				break;

			case PODXTLIVE_INTERFACE_VARIAX:
				line6_variax_process_message((struct
							      usb_line6_variax
							      *)line6);
				break;

			default:
				dev_err(line6->ifcdev,
					"PODxt Live interface %d not supported\n",
					line6->interface_number);
			}
			break;

		case LINE6_DEVID_VARIAX:
			line6_variax_process_message((struct usb_line6_variax *)
						     line6);
			break;

		default:
			MISSING_CASE;
		}
	}

	line6_start_listen(line6);
}

/*
	Send channel number (i.e., switch to a different sound).
*/
int line6_send_program(struct usb_line6 *line6, u8 value)
{
	int retval;
	unsigned char *buffer;
	int partial;

	buffer = kmalloc(2, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	buffer[0] = LINE6_PROGRAM_CHANGE | LINE6_CHANNEL_HOST;
	buffer[1] = value;

	retval = usb_interrupt_msg(line6->usbdev,
				   usb_sndintpipe(line6->usbdev,
						  line6->ep_control_write),
				   buffer, 2, &partial, LINE6_TIMEOUT * HZ);

	if (retval)
		dev_err(line6->ifcdev, "usb_interrupt_msg failed (%d)\n",
			retval);

	kfree(buffer);
	return retval;
}

/*
	Transmit Line6 control parameter.
*/
int line6_transmit_parameter(struct usb_line6 *line6, int param, u8 value)
{
	int retval;
	unsigned char *buffer;
	int partial;

	buffer = kmalloc(3, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	buffer[0] = LINE6_PARAM_CHANGE | LINE6_CHANNEL_HOST;
	buffer[1] = param;
	buffer[2] = value;

	retval = usb_interrupt_msg(line6->usbdev,
				   usb_sndintpipe(line6->usbdev,
						  line6->ep_control_write),
				   buffer, 3, &partial, LINE6_TIMEOUT * HZ);

	if (retval)
		dev_err(line6->ifcdev, "usb_interrupt_msg failed (%d)\n",
			retval);

	kfree(buffer);
	return retval;
}

/*
	Read data from device.
*/
int line6_read_data(struct usb_line6 *line6, int address, void *data,
		    size_t datalen)
{
	struct usb_device *usbdev = line6->usbdev;
	int ret;
	unsigned char len;

	/* query the serial number: */
	ret = usb_control_msg(usbdev, usb_sndctrlpipe(usbdev, 0), 0x67,
			      USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
			      (datalen << 8) | 0x21, address,
			      NULL, 0, LINE6_TIMEOUT * HZ);

	if (ret < 0) {
		dev_err(line6->ifcdev, "read request failed (error %d)\n", ret);
		return ret;
	}

	/* Wait for data length. We'll get 0xff until length arrives. */
	do {
		ret = usb_control_msg(usbdev, usb_rcvctrlpipe(usbdev, 0), 0x67,
				      USB_TYPE_VENDOR | USB_RECIP_DEVICE |
				      USB_DIR_IN,
				      0x0012, 0x0000, &len, 1,
				      LINE6_TIMEOUT * HZ);
		if (ret < 0) {
			dev_err(line6->ifcdev,
				"receive length failed (error %d)\n", ret);
			return ret;
		}
	} while (len == 0xff);

	if (len != datalen) {
		/* should be equal or something went wrong */
		dev_err(line6->ifcdev,
			"length mismatch (expected %d, got %d)\n",
			(int)datalen, (int)len);
		return -EINVAL;
	}

	/* receive the result: */
	ret = usb_control_msg(usbdev, usb_rcvctrlpipe(usbdev, 0), 0x67,
			      USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
			      0x0013, 0x0000, data, datalen,
			      LINE6_TIMEOUT * HZ);

	if (ret < 0) {
		dev_err(line6->ifcdev, "read failed (error %d)\n", ret);
		return ret;
	}

	return 0;
}

/*
	Write data to device.
*/
int line6_write_data(struct usb_line6 *line6, int address, void *data,
		     size_t datalen)
{
	struct usb_device *usbdev = line6->usbdev;
	int ret;
	unsigned char status;

	ret = usb_control_msg(usbdev, usb_sndctrlpipe(usbdev, 0), 0x67,
			      USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
			      0x0022, address, data, datalen,
			      LINE6_TIMEOUT * HZ);

	if (ret < 0) {
		dev_err(line6->ifcdev,
			"write request failed (error %d)\n", ret);
		return ret;
	}

	do {
		ret = usb_control_msg(usbdev, usb_rcvctrlpipe(usbdev, 0),
				      0x67,
				      USB_TYPE_VENDOR | USB_RECIP_DEVICE |
				      USB_DIR_IN,
				      0x0012, 0x0000,
				      &status, 1, LINE6_TIMEOUT * HZ);

		if (ret < 0) {
			dev_err(line6->ifcdev,
				"receiving status failed (error %d)\n", ret);
			return ret;
		}
	} while (status == 0xff);

	if (status != 0) {
		dev_err(line6->ifcdev, "write failed (error %d)\n", ret);
		return -EINVAL;
	}

	return 0;
}

/*
	Read Line6 device serial number.
	(POD, TonePort, GuitarPort)
*/
int line6_read_serial_number(struct usb_line6 *line6, int *serial_number)
{
	return line6_read_data(line6, 0x80d0, serial_number,
			       sizeof(*serial_number));
}

/*
	No operation (i.e., unsupported).
*/
ssize_t line6_nop_read(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	return 0;
}

/*
	Generic destructor.
*/
static void line6_destruct(struct usb_interface *interface)
{
	struct usb_line6 *line6;

	if (interface == NULL)
		return;
	line6 = usb_get_intfdata(interface);
	if (line6 == NULL)
		return;

	/* free buffer memory first: */
	kfree(line6->buffer_message);
	kfree(line6->buffer_listen);

	/* then free URBs: */
	usb_free_urb(line6->urb_listen);

	/* make sure the device isn't destructed twice: */
	usb_set_intfdata(interface, NULL);

	/* free interface data: */
	kfree(line6);
}

/*
	Probe USB device.
*/
static int line6_probe(struct usb_interface *interface,
		       const struct usb_device_id *id)
{
	int devtype;
	struct usb_device *usbdev;
	struct usb_line6 *line6;
	const struct line6_properties *properties;
	int interface_number, alternate = 0;
	int product;
	int size = 0;
	int ep_read = 0, ep_write = 0;
	int ret;

	if (interface == NULL)
		return -ENODEV;
	usbdev = interface_to_usbdev(interface);
	if (usbdev == NULL)
		return -ENODEV;

	/* we don't handle multiple configurations */
	if (usbdev->descriptor.bNumConfigurations != 1) {
		ret = -ENODEV;
		goto err_put;
	}

	/* check vendor and product id */
	for (devtype = ARRAY_SIZE(line6_id_table) - 1; devtype--;) {
		u16 idVendor = le16_to_cpu(usbdev->descriptor.idVendor);
		u16 idProduct = le16_to_cpu(usbdev->descriptor.idProduct);

		if (idVendor == line6_id_table[devtype].idVendor &&
		    idProduct == line6_id_table[devtype].idProduct)
			break;
	}

	if (devtype < 0) {
		ret = -ENODEV;
		goto err_put;
	}

	/* initialize device info: */
	properties = &line6_properties_table[devtype];
	dev_info(&interface->dev, "Line6 %s found\n", properties->name);
	product = le16_to_cpu(usbdev->descriptor.idProduct);

	/* query interface number */
	interface_number = interface->cur_altsetting->desc.bInterfaceNumber;

	switch (product) {
	case LINE6_DEVID_BASSPODXTLIVE:
	case LINE6_DEVID_PODXTLIVE:
	case LINE6_DEVID_VARIAX:
		alternate = 1;
		break;

	case LINE6_DEVID_POCKETPOD:
		switch (interface_number) {
		case 0:
			return 0;	/* this interface has no endpoints */
		case 1:
			alternate = 0;
			break;
		default:
			MISSING_CASE;
		}
		break;

	case LINE6_DEVID_PODHD500:
	case LINE6_DEVID_PODX3:
	case LINE6_DEVID_PODX3LIVE:
		switch (interface_number) {
		case 0:
			alternate = 1;
			break;
		case 1:
			alternate = 0;
			break;
		default:
			MISSING_CASE;
		}
		break;

	case LINE6_DEVID_BASSPODXT:
	case LINE6_DEVID_BASSPODXTPRO:
	case LINE6_DEVID_PODXT:
	case LINE6_DEVID_PODXTPRO:
	case LINE6_DEVID_PODHD300:
	case LINE6_DEVID_PODHD400:
		alternate = 5;
		break;

	case LINE6_DEVID_GUITARPORT:
	case LINE6_DEVID_PODSTUDIO_GX:
	case LINE6_DEVID_PODSTUDIO_UX1:
	case LINE6_DEVID_TONEPORT_GX:
	case LINE6_DEVID_TONEPORT_UX1:
		alternate = 2;	/* 1..4 seem to be ok */
		break;

	case LINE6_DEVID_TONEPORT_UX2:
	case LINE6_DEVID_PODSTUDIO_UX2:
		switch (interface_number) {
		case 0:
			/* defaults to 44.1kHz, 16-bit */
			alternate = 2;
			break;
		case 1:
			/* don't know yet what this is ...
			   alternate = 1;
			   break;
			 */
			return -ENODEV;
		default:
			MISSING_CASE;
		}
		break;

	default:
		MISSING_CASE;
		ret = -ENODEV;
		goto err_put;
	}

	ret = usb_set_interface(usbdev, interface_number, alternate);
	if (ret < 0) {
		dev_err(&interface->dev, "set_interface failed\n");
		goto err_put;
	}

	/* initialize device data based on product id: */
	switch (product) {
	case LINE6_DEVID_BASSPODXT:
	case LINE6_DEVID_BASSPODXTLIVE:
	case LINE6_DEVID_BASSPODXTPRO:
	case LINE6_DEVID_PODXT:
	case LINE6_DEVID_PODXTPRO:
		size = sizeof(struct usb_line6_pod);
		ep_read = 0x84;
		ep_write = 0x03;
		break;

	case LINE6_DEVID_PODHD300:
	case LINE6_DEVID_PODHD400:
		size = sizeof(struct usb_line6_podhd);
		ep_read = 0x84;
		ep_write = 0x03;
		break;

	case LINE6_DEVID_PODHD500:
		size = sizeof(struct usb_line6_podhd);
		ep_read = 0x81;
		ep_write = 0x01;
		break;

	case LINE6_DEVID_POCKETPOD:
		size = sizeof(struct usb_line6_pod);
		ep_read = 0x82;
		ep_write = 0x02;
		break;

	case LINE6_DEVID_PODX3:
	case LINE6_DEVID_PODX3LIVE:
		/* currently unused! */
		size = sizeof(struct usb_line6_pod);
		ep_read = 0x81;
		ep_write = 0x01;
		break;

	case LINE6_DEVID_PODSTUDIO_GX:
	case LINE6_DEVID_PODSTUDIO_UX1:
	case LINE6_DEVID_PODSTUDIO_UX2:
	case LINE6_DEVID_TONEPORT_GX:
	case LINE6_DEVID_TONEPORT_UX1:
	case LINE6_DEVID_TONEPORT_UX2:
	case LINE6_DEVID_GUITARPORT:
		size = sizeof(struct usb_line6_toneport);
		/* these don't have a control channel */
		break;

	case LINE6_DEVID_PODXTLIVE:
		switch (interface_number) {
		case PODXTLIVE_INTERFACE_POD:
			size = sizeof(struct usb_line6_pod);
			ep_read = 0x84;
			ep_write = 0x03;
			break;

		case PODXTLIVE_INTERFACE_VARIAX:
			size = sizeof(struct usb_line6_variax);
			ep_read = 0x86;
			ep_write = 0x05;
			break;

		default:
			ret = -ENODEV;
			goto err_put;
		}
		break;

	case LINE6_DEVID_VARIAX:
		size = sizeof(struct usb_line6_variax);
		ep_read = 0x82;
		ep_write = 0x01;
		break;

	default:
		MISSING_CASE;
		ret = -ENODEV;
		goto err_put;
	}

	if (size == 0) {
		dev_err(&interface->dev,
			"driver bug: interface data size not set\n");
		ret = -ENODEV;
		goto err_put;
	}

	line6 = kzalloc(size, GFP_KERNEL);
	if (line6 == NULL) {
		ret = -ENODEV;
		goto err_put;
	}

	/* store basic data: */
	line6->interface_number = interface_number;
	line6->properties = properties;
	line6->usbdev = usbdev;
	line6->ifcdev = &interface->dev;
	line6->ep_control_read = ep_read;
	line6->ep_control_write = ep_write;
	line6->product = product;

	/* get data from endpoint descriptor (see usb_maxpacket): */
	{
		struct usb_host_endpoint *ep;
		unsigned epnum =
		    usb_pipeendpoint(usb_rcvintpipe(usbdev, ep_read));
		ep = usbdev->ep_in[epnum];

		if (ep != NULL) {
			line6->interval = ep->desc.bInterval;
			line6->max_packet_size =
			    le16_to_cpu(ep->desc.wMaxPacketSize);
		} else {
			line6->interval = LINE6_FALLBACK_INTERVAL;
			line6->max_packet_size = LINE6_FALLBACK_MAXPACKETSIZE;
			dev_err(line6->ifcdev,
				"endpoint not available, using fallback values");
		}
	}

	usb_set_intfdata(interface, line6);

	if (properties->capabilities & LINE6_BIT_CONTROL) {
		/* initialize USB buffers: */
		line6->buffer_listen =
		    kmalloc(LINE6_BUFSIZE_LISTEN, GFP_KERNEL);
		if (line6->buffer_listen == NULL) {
			ret = -ENOMEM;
			goto err_destruct;
		}

		line6->buffer_message =
		    kmalloc(LINE6_MESSAGE_MAXLEN, GFP_KERNEL);
		if (line6->buffer_message == NULL) {
			ret = -ENOMEM;
			goto err_destruct;
		}

		line6->urb_listen = usb_alloc_urb(0, GFP_KERNEL);

		if (line6->urb_listen == NULL) {
			dev_err(&interface->dev, "Out of memory\n");
			line6_destruct(interface);
			ret = -ENOMEM;
			goto err_destruct;
		}

		ret = line6_start_listen(line6);
		if (ret < 0) {
			dev_err(&interface->dev, "%s: usb_submit_urb failed\n",
				__func__);
			goto err_destruct;
		}
	}

	/* initialize device data based on product id: */
	switch (product) {
	case LINE6_DEVID_BASSPODXT:
	case LINE6_DEVID_BASSPODXTLIVE:
	case LINE6_DEVID_BASSPODXTPRO:
	case LINE6_DEVID_POCKETPOD:
	case LINE6_DEVID_PODX3:
	case LINE6_DEVID_PODX3LIVE:
	case LINE6_DEVID_PODXT:
	case LINE6_DEVID_PODXTPRO:
		ret = line6_pod_init(interface, (struct usb_line6_pod *)line6);
		break;

	case LINE6_DEVID_PODHD300:
	case LINE6_DEVID_PODHD400:
	case LINE6_DEVID_PODHD500:
		ret = line6_podhd_init(interface,
				       (struct usb_line6_podhd *)line6);
		break;

	case LINE6_DEVID_PODXTLIVE:
		switch (interface_number) {
		case PODXTLIVE_INTERFACE_POD:
			ret =
			    line6_pod_init(interface,
					   (struct usb_line6_pod *)line6);
			break;

		case PODXTLIVE_INTERFACE_VARIAX:
			ret =
			    line6_variax_init(interface,
					      (struct usb_line6_variax *)line6);
			break;

		default:
			dev_err(&interface->dev,
				"PODxt Live interface %d not supported\n",
				interface_number);
			ret = -ENODEV;
		}

		break;

	case LINE6_DEVID_VARIAX:
		ret =
		    line6_variax_init(interface,
				      (struct usb_line6_variax *)line6);
		break;

	case LINE6_DEVID_PODSTUDIO_GX:
	case LINE6_DEVID_PODSTUDIO_UX1:
	case LINE6_DEVID_PODSTUDIO_UX2:
	case LINE6_DEVID_TONEPORT_GX:
	case LINE6_DEVID_TONEPORT_UX1:
	case LINE6_DEVID_TONEPORT_UX2:
	case LINE6_DEVID_GUITARPORT:
		ret =
		    line6_toneport_init(interface,
					(struct usb_line6_toneport *)line6);
		break;

	default:
		MISSING_CASE;
		ret = -ENODEV;
	}

	if (ret < 0)
		goto err_destruct;

	ret = sysfs_create_link(&interface->dev.kobj, &usbdev->dev.kobj,
				"usb_device");
	if (ret < 0)
		goto err_destruct;

	/* creation of additional special files should go here */

	dev_info(&interface->dev, "Line6 %s now attached\n",
		 line6->properties->name);

	switch (product) {
	case LINE6_DEVID_PODX3:
	case LINE6_DEVID_PODX3LIVE:
		dev_info(&interface->dev,
			 "NOTE: the Line6 %s is detected, but not yet supported\n",
			 line6->properties->name);
	}

	/* increment reference counters: */
	usb_get_intf(interface);
	usb_get_dev(usbdev);

	return 0;

err_destruct:
	line6_destruct(interface);
err_put:
	return ret;
}

/*
	Line6 device disconnected.
*/
static void line6_disconnect(struct usb_interface *interface)
{
	struct usb_line6 *line6;
	struct usb_device *usbdev;
	int interface_number;

	if (interface == NULL)
		return;
	usbdev = interface_to_usbdev(interface);
	if (usbdev == NULL)
		return;

	/* removal of additional special files should go here */

	sysfs_remove_link(&interface->dev.kobj, "usb_device");

	interface_number = interface->cur_altsetting->desc.bInterfaceNumber;
	line6 = usb_get_intfdata(interface);

	if (line6 != NULL) {
		if (line6->urb_listen != NULL)
			line6_stop_listen(line6);

		if (usbdev != line6->usbdev)
			dev_err(line6->ifcdev,
				"driver bug: inconsistent usb device\n");

		switch (le16_to_cpu(line6->usbdev->descriptor.idProduct)) {
		case LINE6_DEVID_BASSPODXT:
		case LINE6_DEVID_BASSPODXTLIVE:
		case LINE6_DEVID_BASSPODXTPRO:
		case LINE6_DEVID_POCKETPOD:
		case LINE6_DEVID_PODX3:
		case LINE6_DEVID_PODX3LIVE:
		case LINE6_DEVID_PODXT:
		case LINE6_DEVID_PODXTPRO:
			line6_pod_disconnect(interface);
			break;

		case LINE6_DEVID_PODHD300:
		case LINE6_DEVID_PODHD400:
		case LINE6_DEVID_PODHD500:
			line6_podhd_disconnect(interface);
			break;

		case LINE6_DEVID_PODXTLIVE:
			switch (interface_number) {
			case PODXTLIVE_INTERFACE_POD:
				line6_pod_disconnect(interface);
				break;

			case PODXTLIVE_INTERFACE_VARIAX:
				line6_variax_disconnect(interface);
				break;
			}

			break;

		case LINE6_DEVID_VARIAX:
			line6_variax_disconnect(interface);
			break;

		case LINE6_DEVID_PODSTUDIO_GX:
		case LINE6_DEVID_PODSTUDIO_UX1:
		case LINE6_DEVID_PODSTUDIO_UX2:
		case LINE6_DEVID_TONEPORT_GX:
		case LINE6_DEVID_TONEPORT_UX1:
		case LINE6_DEVID_TONEPORT_UX2:
		case LINE6_DEVID_GUITARPORT:
			line6_toneport_disconnect(interface);
			break;

		default:
			MISSING_CASE;
		}

		dev_info(&interface->dev, "Line6 %s now disconnected\n",
			 line6->properties->name);
	}

	line6_destruct(interface);

	/* decrement reference counters: */
	usb_put_intf(interface);
	usb_put_dev(usbdev);
}

#ifdef CONFIG_PM

/*
	Suspend Line6 device.
*/
static int line6_suspend(struct usb_interface *interface, pm_message_t message)
{
	struct usb_line6 *line6 = usb_get_intfdata(interface);
	struct snd_line6_pcm *line6pcm = line6->line6pcm;

	snd_power_change_state(line6->card, SNDRV_CTL_POWER_D3hot);

	if (line6->properties->capabilities & LINE6_BIT_CONTROL)
		line6_stop_listen(line6);

	if (line6pcm != NULL) {
		snd_pcm_suspend_all(line6pcm->pcm);
		line6_pcm_disconnect(line6pcm);
		line6pcm->flags = 0;
	}

	return 0;
}

/*
	Resume Line6 device.
*/
static int line6_resume(struct usb_interface *interface)
{
	struct usb_line6 *line6 = usb_get_intfdata(interface);

	if (line6->properties->capabilities & LINE6_BIT_CONTROL)
		line6_start_listen(line6);

	snd_power_change_state(line6->card, SNDRV_CTL_POWER_D0);
	return 0;
}

/*
	Resume Line6 device after reset.
*/
static int line6_reset_resume(struct usb_interface *interface)
{
	struct usb_line6 *line6 = usb_get_intfdata(interface);

	switch (le16_to_cpu(line6->usbdev->descriptor.idProduct)) {
	case LINE6_DEVID_PODSTUDIO_GX:
	case LINE6_DEVID_PODSTUDIO_UX1:
	case LINE6_DEVID_PODSTUDIO_UX2:
	case LINE6_DEVID_TONEPORT_GX:
	case LINE6_DEVID_TONEPORT_UX1:
	case LINE6_DEVID_TONEPORT_UX2:
	case LINE6_DEVID_GUITARPORT:
		line6_toneport_reset_resume((struct usb_line6_toneport *)line6);
	}

	return line6_resume(interface);
}

#endif /* CONFIG_PM */

static struct usb_driver line6_driver = {
	.name = DRIVER_NAME,
	.probe = line6_probe,
	.disconnect = line6_disconnect,
#ifdef CONFIG_PM
	.suspend = line6_suspend,
	.resume = line6_resume,
	.reset_resume = line6_reset_resume,
#endif
	.id_table = line6_id_table,
};

module_usb_driver(line6_driver);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);
