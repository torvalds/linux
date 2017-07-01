/*
 * TechnoTrend USB IR Receiver
 *
 * Copyright (C) 2012 Sean Young <sean@mess.org>
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
 */

#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/input.h>
#include <linux/slab.h>
#include <linux/leds.h>
#include <media/rc-core.h>

#define DRIVER_NAME	"ttusbir"
#define DRIVER_DESC	"TechnoTrend USB IR Receiver"
/*
 * The Windows driver uses 8 URBS, the original lirc drivers has a
 * configurable amount (2 default, 4 max). This device generates about 125
 * messages per second (!), whether IR is idle or not.
 */
#define NUM_URBS	4
#define NS_PER_BYTE	62500
#define NS_PER_BIT	(NS_PER_BYTE/8)

struct ttusbir {
	struct rc_dev *rc;
	struct device *dev;
	struct usb_device *udev;

	struct urb *urb[NUM_URBS];

	struct led_classdev led;
	struct urb *bulk_urb;
	uint8_t bulk_buffer[5];
	int bulk_out_endp, iso_in_endp;
	bool led_on, is_led_on;
	atomic_t led_complete;

	char phys[64];
};

static enum led_brightness ttusbir_brightness_get(struct led_classdev *led_dev)
{
	struct ttusbir *tt = container_of(led_dev, struct ttusbir, led);

	return tt->led_on ? LED_FULL : LED_OFF;
}

static void ttusbir_set_led(struct ttusbir *tt)
{
	int ret;

	smp_mb();

	if (tt->led_on != tt->is_led_on && tt->udev &&
				atomic_add_unless(&tt->led_complete, 1, 1)) {
		tt->bulk_buffer[4] = tt->is_led_on = tt->led_on;
		ret = usb_submit_urb(tt->bulk_urb, GFP_ATOMIC);
		if (ret) {
			dev_warn(tt->dev, "failed to submit bulk urb: %d\n",
									ret);
			atomic_dec(&tt->led_complete);
		}
	}
}

static void ttusbir_brightness_set(struct led_classdev *led_dev, enum
						led_brightness brightness)
{
	struct ttusbir *tt = container_of(led_dev, struct ttusbir, led);

	tt->led_on = brightness != LED_OFF;

	ttusbir_set_led(tt);
}

/*
 * The urb cannot be reused until the urb completes
 */
static void ttusbir_bulk_complete(struct urb *urb)
{
	struct ttusbir *tt = urb->context;

	atomic_dec(&tt->led_complete);

	switch (urb->status) {
	case 0:
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		usb_unlink_urb(urb);
		return;
	case -EPIPE:
	default:
		dev_dbg(tt->dev, "Error: urb status = %d\n", urb->status);
		break;
	}

	ttusbir_set_led(tt);
}

/*
 * The data is one bit per sample, a set bit signifying silence and samples
 * being MSB first. Bit 0 can contain garbage so take it to be whatever
 * bit 1 is, so we don't have unexpected edges.
 */
static void ttusbir_process_ir_data(struct ttusbir *tt, uint8_t *buf)
{
	struct ir_raw_event rawir;
	unsigned i, v, b;
	bool event = false;

	init_ir_raw_event(&rawir);

	for (i = 0; i < 128; i++) {
		v = buf[i] & 0xfe;
		switch (v) {
		case 0xfe:
			rawir.pulse = false;
			rawir.duration = NS_PER_BYTE;
			if (ir_raw_event_store_with_filter(tt->rc, &rawir))
				event = true;
			break;
		case 0:
			rawir.pulse = true;
			rawir.duration = NS_PER_BYTE;
			if (ir_raw_event_store_with_filter(tt->rc, &rawir))
				event = true;
			break;
		default:
			/* one edge per byte */
			if (v & 2) {
				b = ffz(v | 1);
				rawir.pulse = true;
			} else {
				b = ffs(v) - 1;
				rawir.pulse = false;
			}

			rawir.duration = NS_PER_BIT * (8 - b);
			if (ir_raw_event_store_with_filter(tt->rc, &rawir))
				event = true;

			rawir.pulse = !rawir.pulse;
			rawir.duration = NS_PER_BIT * b;
			if (ir_raw_event_store_with_filter(tt->rc, &rawir))
				event = true;
			break;
		}
	}

	/* don't wakeup when there's nothing to do */
	if (event)
		ir_raw_event_handle(tt->rc);
}

static void ttusbir_urb_complete(struct urb *urb)
{
	struct ttusbir *tt = urb->context;
	int rc;

	switch (urb->status) {
	case 0:
		ttusbir_process_ir_data(tt, urb->transfer_buffer);
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		usb_unlink_urb(urb);
		return;
	case -EPIPE:
	default:
		dev_dbg(tt->dev, "Error: urb status = %d\n", urb->status);
		break;
	}

	rc = usb_submit_urb(urb, GFP_ATOMIC);
	if (rc && rc != -ENODEV)
		dev_warn(tt->dev, "failed to resubmit urb: %d\n", rc);
}

static int ttusbir_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct ttusbir *tt;
	struct usb_interface_descriptor *idesc;
	struct usb_endpoint_descriptor *desc;
	struct rc_dev *rc;
	int i, j, ret;
	int altsetting = -1;

	tt = kzalloc(sizeof(*tt), GFP_KERNEL);
	rc = rc_allocate_device(RC_DRIVER_IR_RAW);
	if (!tt || !rc) {
		ret = -ENOMEM;
		goto out;
	}

	/* find the correct alt setting */
	for (i = 0; i < intf->num_altsetting && altsetting == -1; i++) {
		int max_packet, bulk_out_endp = -1, iso_in_endp = -1;

		idesc = &intf->altsetting[i].desc;

		for (j = 0; j < idesc->bNumEndpoints; j++) {
			desc = &intf->altsetting[i].endpoint[j].desc;
			max_packet = le16_to_cpu(desc->wMaxPacketSize);
			if (usb_endpoint_dir_in(desc) &&
					usb_endpoint_xfer_isoc(desc) &&
					max_packet == 0x10)
				iso_in_endp = j;
			else if (usb_endpoint_dir_out(desc) &&
					usb_endpoint_xfer_bulk(desc) &&
					max_packet == 0x20)
				bulk_out_endp = j;

			if (bulk_out_endp != -1 && iso_in_endp != -1) {
				tt->bulk_out_endp = bulk_out_endp;
				tt->iso_in_endp = iso_in_endp;
				altsetting = i;
				break;
			}
		}
	}

	if (altsetting == -1) {
		dev_err(&intf->dev, "cannot find expected altsetting\n");
		ret = -ENODEV;
		goto out;
	}

	tt->dev = &intf->dev;
	tt->udev = interface_to_usbdev(intf);
	tt->rc = rc;

	ret = usb_set_interface(tt->udev, 0, altsetting);
	if (ret)
		goto out;

	for (i = 0; i < NUM_URBS; i++) {
		struct urb *urb = usb_alloc_urb(8, GFP_KERNEL);
		void *buffer;

		if (!urb) {
			ret = -ENOMEM;
			goto out;
		}

		urb->dev = tt->udev;
		urb->context = tt;
		urb->pipe = usb_rcvisocpipe(tt->udev, tt->iso_in_endp);
		urb->interval = 1;
		buffer = usb_alloc_coherent(tt->udev, 128, GFP_KERNEL,
						&urb->transfer_dma);
		if (!buffer) {
			usb_free_urb(urb);
			ret = -ENOMEM;
			goto out;
		}
		urb->transfer_flags = URB_NO_TRANSFER_DMA_MAP | URB_ISO_ASAP;
		urb->transfer_buffer = buffer;
		urb->complete = ttusbir_urb_complete;
		urb->number_of_packets = 8;
		urb->transfer_buffer_length = 128;

		for (j = 0; j < 8; j++) {
			urb->iso_frame_desc[j].offset = j * 16;
			urb->iso_frame_desc[j].length = 16;
		}

		tt->urb[i] = urb;
	}

	tt->bulk_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!tt->bulk_urb) {
		ret = -ENOMEM;
		goto out;
	}

	tt->bulk_buffer[0] = 0xaa;
	tt->bulk_buffer[1] = 0x01;
	tt->bulk_buffer[2] = 0x05;
	tt->bulk_buffer[3] = 0x01;

	usb_fill_bulk_urb(tt->bulk_urb, tt->udev, usb_sndbulkpipe(tt->udev,
		tt->bulk_out_endp), tt->bulk_buffer, sizeof(tt->bulk_buffer),
						ttusbir_bulk_complete, tt);

	tt->led.name = "ttusbir:green:power";
	tt->led.default_trigger = "rc-feedback";
	tt->led.brightness_set = ttusbir_brightness_set;
	tt->led.brightness_get = ttusbir_brightness_get;
	tt->is_led_on = tt->led_on = true;
	atomic_set(&tt->led_complete, 0);
	ret = led_classdev_register(&intf->dev, &tt->led);
	if (ret)
		goto out;

	usb_make_path(tt->udev, tt->phys, sizeof(tt->phys));

	rc->device_name = DRIVER_DESC;
	rc->input_phys = tt->phys;
	usb_to_input_id(tt->udev, &rc->input_id);
	rc->dev.parent = &intf->dev;
	rc->allowed_protocols = RC_BIT_ALL_IR_DECODER;
	rc->priv = tt;
	rc->driver_name = DRIVER_NAME;
	rc->map_name = RC_MAP_TT_1500;
	rc->min_timeout = 1;
	rc->timeout = IR_DEFAULT_TIMEOUT;
	rc->max_timeout = 10 * IR_DEFAULT_TIMEOUT;

	/*
	 * The precision is NS_PER_BIT, but since every 8th bit can be
	 * overwritten with garbage the accuracy is at best 2 * NS_PER_BIT.
	 */
	rc->rx_resolution = NS_PER_BIT;

	ret = rc_register_device(rc);
	if (ret) {
		dev_err(&intf->dev, "failed to register rc device %d\n", ret);
		goto out2;
	}

	usb_set_intfdata(intf, tt);

	for (i = 0; i < NUM_URBS; i++) {
		ret = usb_submit_urb(tt->urb[i], GFP_KERNEL);
		if (ret) {
			dev_err(tt->dev, "failed to submit urb %d\n", ret);
			goto out3;
		}
	}

	return 0;
out3:
	rc_unregister_device(rc);
	rc = NULL;
out2:
	led_classdev_unregister(&tt->led);
out:
	if (tt) {
		for (i = 0; i < NUM_URBS && tt->urb[i]; i++) {
			struct urb *urb = tt->urb[i];

			usb_kill_urb(urb);
			usb_free_coherent(tt->udev, 128, urb->transfer_buffer,
							urb->transfer_dma);
			usb_free_urb(urb);
		}
		usb_kill_urb(tt->bulk_urb);
		usb_free_urb(tt->bulk_urb);
		kfree(tt);
	}
	rc_free_device(rc);

	return ret;
}

static void ttusbir_disconnect(struct usb_interface *intf)
{
	struct ttusbir *tt = usb_get_intfdata(intf);
	struct usb_device *udev = tt->udev;
	int i;

	tt->udev = NULL;

	rc_unregister_device(tt->rc);
	led_classdev_unregister(&tt->led);
	for (i = 0; i < NUM_URBS; i++) {
		usb_kill_urb(tt->urb[i]);
		usb_free_coherent(udev, 128, tt->urb[i]->transfer_buffer,
						tt->urb[i]->transfer_dma);
		usb_free_urb(tt->urb[i]);
	}
	usb_kill_urb(tt->bulk_urb);
	usb_free_urb(tt->bulk_urb);
	usb_set_intfdata(intf, NULL);
	kfree(tt);
}

static int ttusbir_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct ttusbir *tt = usb_get_intfdata(intf);
	int i;

	for (i = 0; i < NUM_URBS; i++)
		usb_kill_urb(tt->urb[i]);

	led_classdev_suspend(&tt->led);
	usb_kill_urb(tt->bulk_urb);

	return 0;
}

static int ttusbir_resume(struct usb_interface *intf)
{
	struct ttusbir *tt = usb_get_intfdata(intf);
	int i, rc;

	tt->is_led_on = true;
	led_classdev_resume(&tt->led);

	for (i = 0; i < NUM_URBS; i++) {
		rc = usb_submit_urb(tt->urb[i], GFP_KERNEL);
		if (rc) {
			dev_warn(tt->dev, "failed to submit urb: %d\n", rc);
			break;
		}
	}

	return rc;
}

static const struct usb_device_id ttusbir_table[] = {
	{ USB_DEVICE(0x0b48, 0x2003) },
	{ }
};

static struct usb_driver ttusbir_driver = {
	.name = DRIVER_NAME,
	.id_table = ttusbir_table,
	.probe = ttusbir_probe,
	.suspend = ttusbir_suspend,
	.resume = ttusbir_resume,
	.reset_resume = ttusbir_resume,
	.disconnect = ttusbir_disconnect,
};

module_usb_driver(ttusbir_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Sean Young <sean@mess.org>");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(usb, ttusbir_table);

