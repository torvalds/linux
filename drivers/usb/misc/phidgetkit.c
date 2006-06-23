/*
 * USB PhidgetInterfaceKit driver 1.0
 *
 * Copyright (C) 2004, 2006 Sean Young <sean@mess.org>
 * Copyright (C) 2005 Daniel Saakes <daniel@saakes.net>
 * Copyright (C) 2004 Greg Kroah-Hartman <greg@kroah.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This is a driver for the USB PhidgetInterfaceKit.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb.h>

#define DRIVER_AUTHOR "Sean Young <sean@mess.org>"
#define DRIVER_DESC "USB PhidgetInterfaceKit Driver"

#define USB_VENDOR_ID_GLAB		0x06c2
#define USB_DEVICE_ID_INTERFACEKIT004	0x0040
#define USB_DEVICE_ID_INTERFACEKIT01616	0x0044
#define USB_DEVICE_ID_INTERFACEKIT888	0x0045
#define USB_DEVICE_ID_INTERFACEKIT047	0x0051
#define USB_DEVICE_ID_INTERFACEKIT088	0x0053

#define USB_VENDOR_ID_WISEGROUP		0x0925
#define USB_DEVICE_ID_INTERFACEKIT884	0x8201

#define MAX_INTERFACES			16

#define URB_INT_SIZE			8

struct driver_interfacekit {
	int sensors;
	int inputs;
	int outputs;
	int has_lcd;
};
#define ifkit(_sensors, _inputs, _outputs, _lcd)			\
static struct driver_interfacekit ph_##_sensors##_inputs##_outputs = {	\
	.sensors	= _sensors,					\
	.inputs		= _inputs,					\
	.outputs	= _outputs,					\
	.has_lcd	= _lcd,						\
};
ifkit(0, 0, 4, 0);
ifkit(8, 8, 8, 0);
ifkit(0, 4, 7, 1);
ifkit(8, 8, 4, 0);
ifkit(0, 8, 8, 1);
ifkit(0, 16, 16, 0);

struct interfacekit {
	struct usb_device *udev;
	struct usb_interface *intf;
	struct driver_interfacekit *ifkit;
	unsigned long outputs;
	u8 inputs[MAX_INTERFACES];
	u16 sensors[MAX_INTERFACES];
	u8 lcd_files_on;

	struct urb *irq;
	unsigned char *data;
	dma_addr_t data_dma;

	struct work_struct do_notify;
	unsigned long input_events;
	unsigned long sensor_events;
};

static struct usb_device_id id_table[] = {
	{USB_DEVICE(USB_VENDOR_ID_GLAB, USB_DEVICE_ID_INTERFACEKIT004),
		.driver_info = (kernel_ulong_t)&ph_004},
	{USB_DEVICE(USB_VENDOR_ID_GLAB, USB_DEVICE_ID_INTERFACEKIT888),
		.driver_info = (kernel_ulong_t)&ph_888},
	{USB_DEVICE(USB_VENDOR_ID_GLAB, USB_DEVICE_ID_INTERFACEKIT047),
		.driver_info = (kernel_ulong_t)&ph_047},
	{USB_DEVICE(USB_VENDOR_ID_GLAB, USB_DEVICE_ID_INTERFACEKIT088),
		.driver_info = (kernel_ulong_t)&ph_088},
	{USB_DEVICE(USB_VENDOR_ID_GLAB, USB_DEVICE_ID_INTERFACEKIT01616),
		.driver_info = (kernel_ulong_t)&ph_01616},
	{USB_DEVICE(USB_VENDOR_ID_WISEGROUP, USB_DEVICE_ID_INTERFACEKIT884),
		.driver_info = (kernel_ulong_t)&ph_884},
	{}
};
MODULE_DEVICE_TABLE(usb, id_table);

static int change_outputs(struct interfacekit *kit, int output_num, int enable)
{
	u8 *buffer;
	int retval;

	if (enable)
		set_bit(output_num, &kit->outputs);
	else
		clear_bit(output_num, &kit->outputs);

	buffer = kzalloc(4, GFP_KERNEL);
	if (!buffer) {
		dev_err(&kit->udev->dev, "%s - out of memory\n", __FUNCTION__);
		return -ENOMEM;
	}
	buffer[0] = (u8)kit->outputs;
	buffer[1] = (u8)(kit->outputs >> 8);

	dev_dbg(&kit->udev->dev, "sending data: 0x%04x\n", (u16)kit->outputs);

	retval = usb_control_msg(kit->udev,
			 usb_sndctrlpipe(kit->udev, 0),
			 0x09, 0x21, 0x0200, 0x0000, buffer, 4, 2000);

	if (retval != 4)
		dev_err(&kit->udev->dev, "usb_control_msg returned %d\n", 
				retval);
	kfree(buffer);

	return retval < 0 ? retval : 0;
}

static int change_string(struct interfacekit *kit, const char *display, unsigned char row)
{
	unsigned char *buffer;
	unsigned char *form_buffer;
	int retval = -ENOMEM;
	int i,j, len, buf_ptr;
	
	buffer = kmalloc(8, GFP_KERNEL);
	form_buffer = kmalloc(30, GFP_KERNEL);
	if ((!buffer) || (!form_buffer)) {
		dev_err(&kit->udev->dev, "%s - out of memory\n", __FUNCTION__);
		goto exit;
	}

	len = strlen(display);
	if (len > 20)
		len = 20;

	dev_dbg(&kit->udev->dev, "Setting LCD line %d to %s\n", row, display);

	form_buffer[0] = row * 0x40 + 0x80;
	form_buffer[1] = 0x02;
	buf_ptr = 2;
	for (i = 0; i<len; i++)
		form_buffer[buf_ptr++] = display[i];

	for (i = 0; i < (20 - len); i++)
		form_buffer[buf_ptr++] = 0x20;
	form_buffer[buf_ptr++] = 0x01;
	form_buffer[buf_ptr++] = row * 0x40 + 0x80 + strlen(display);

	for (i = 0; i < buf_ptr; i += 7) {
		if ((buf_ptr - i) > 7)
			len = 7;
		else
			len = (buf_ptr - i);
		for (j = 0; j < len; j++)
			buffer[j] = form_buffer[i + j];
		buffer[7] = len;

		retval = usb_control_msg(kit->udev,
				 usb_sndctrlpipe(kit->udev, 0),
				 0x09, 0x21, 0x0200, 0x0000, buffer, 8, 2000);
		if (retval < 0)
			goto exit;
	}

	retval = 0;
exit:
	kfree(buffer);
	kfree(form_buffer);

	return retval;
}

#define set_lcd_line(number)	\
static ssize_t lcd_line_##number(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)	\
{											\
	struct usb_interface *intf = to_usb_interface(dev);				\
	struct interfacekit *kit = usb_get_intfdata(intf);				\
	change_string(kit, buf, number - 1);						\
	return count;									\
}											\
static DEVICE_ATTR(lcd_line_##number, S_IWUGO, NULL, lcd_line_##number);
set_lcd_line(1);
set_lcd_line(2);

static ssize_t set_backlight(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct interfacekit *kit = usb_get_intfdata(intf);
	int enabled;
	unsigned char *buffer;
	int retval = -ENOMEM;
	
	buffer = kzalloc(8, GFP_KERNEL);
	if (!buffer) {
		dev_err(&kit->udev->dev, "%s - out of memory\n", __FUNCTION__);
		goto exit;
	}

	if (sscanf(buf, "%d", &enabled) < 1) {
		retval = -EINVAL;
		goto exit;
	}
	if (enabled)
		buffer[0] = 0x01;
	buffer[7] = 0x11;

	dev_dbg(&kit->udev->dev, "Setting backlight to %s\n", enabled ? "on" : "off");
	
	retval = usb_control_msg(kit->udev,
			 usb_sndctrlpipe(kit->udev, 0),
			 0x09, 0x21, 0x0200, 0x0000, buffer, 8, 2000);
	if (retval < 0)
		goto exit;

	retval = count;
exit:
	kfree(buffer);
	return retval;
}
static DEVICE_ATTR(backlight, S_IWUGO, NULL, set_backlight);

static void remove_lcd_files(struct interfacekit *kit)
{
	if (kit->lcd_files_on) {
		dev_dbg(&kit->udev->dev, "Removing lcd files\n");
		device_remove_file(&kit->intf->dev, &dev_attr_lcd_line_1);
		device_remove_file(&kit->intf->dev, &dev_attr_lcd_line_2);
		device_remove_file(&kit->intf->dev, &dev_attr_backlight);
	}
}

static ssize_t enable_lcd_files(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct interfacekit *kit = usb_get_intfdata(intf);
	int enable;
	
	if (kit->ifkit->has_lcd == 0)
		return -ENODEV;

	if (sscanf(buf, "%d", &enable) < 1)
		return -EINVAL;

	if (enable) {
		if (!kit->lcd_files_on) {
			dev_dbg(&kit->udev->dev, "Adding lcd files\n");
			device_create_file(&kit->intf->dev, &dev_attr_lcd_line_1);
			device_create_file(&kit->intf->dev, &dev_attr_lcd_line_2);
			device_create_file(&kit->intf->dev, &dev_attr_backlight);
			kit->lcd_files_on = 1;
		}
	} else {
		if (kit->lcd_files_on) {
			remove_lcd_files(kit);
			kit->lcd_files_on = 0;
		}
	}
	
	return count;
}
static DEVICE_ATTR(lcd, S_IWUGO, NULL, enable_lcd_files);

static void interfacekit_irq(struct urb *urb, struct pt_regs *regs)
{
	struct interfacekit *kit = urb->context;
	unsigned char *buffer = kit->data;
	int i, level, sensor;
	int status;

	switch (urb->status) {
	case 0:			/* success */
		break;
	case -ECONNRESET:	/* unlink */
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	/* -EPIPE:  should clear the halt */
	default:		/* error */
		goto resubmit;
	}

	/* digital inputs */
	if (kit->ifkit->inputs == 16) {
		for (i=0; i < 8; i++) {
			level = (buffer[0] >> i) & 1;
			if (kit->inputs[i] != level) {
				kit->inputs[i] = level;
				set_bit(i, &kit->input_events);
			}
			level = (buffer[1] >> i) & 1;
			if (kit->inputs[8 + i] != level) {
				kit->inputs[8 + i] = level;
				set_bit(8 + i, &kit->input_events);
			}
		}
	}
	else if (kit->ifkit->inputs == 8) {
		for (i=0; i < 8; i++) {
			level = (buffer[1] >> i) & 1;
			if (kit->inputs[i] != level) {
				kit->inputs[i] = level;
				set_bit(i, &kit->input_events);
			}
		}
	}

	/* analog inputs */
	if (kit->ifkit->sensors) {
		sensor = (buffer[0] & 1) ? 4 : 0;

		level = buffer[2] + (buffer[3] & 0x0f) * 256;
		if (level != kit->sensors[sensor]) {
			kit->sensors[sensor] = level;
			set_bit(sensor, &kit->sensor_events);
		}
		sensor++;
		level = buffer[4] + (buffer[3] & 0xf0) * 16;
		if (level != kit->sensors[sensor]) {
			kit->sensors[sensor] = level;
			set_bit(sensor, &kit->sensor_events);
		}
		sensor++;
		level = buffer[5] + (buffer[6] & 0x0f) * 256;
		if (level != kit->sensors[sensor]) {
			kit->sensors[sensor] = level;
			set_bit(sensor, &kit->sensor_events);
		}
		sensor++;
		level = buffer[7] + (buffer[6] & 0xf0) * 16;
		if (level != kit->sensors[sensor]) {
			kit->sensors[sensor] = level;
			set_bit(sensor, &kit->sensor_events);
		}
	}

	if (kit->input_events || kit->sensor_events)
		schedule_work(&kit->do_notify);

resubmit:
	status = usb_submit_urb(urb, SLAB_ATOMIC);
	if (status)
		err("can't resubmit intr, %s-%s/interfacekit0, status %d",
			kit->udev->bus->bus_name,
			kit->udev->devpath, status);
}

static void do_notify(void *data)
{
	struct interfacekit *kit = data;
	int i;
	char sysfs_file[8];

	for (i=0; i<kit->ifkit->inputs; i++) {
		if (test_and_clear_bit(i, &kit->input_events)) {
			sprintf(sysfs_file, "input%d", i + 1);
			sysfs_notify(&kit->intf->dev.kobj, NULL, sysfs_file);
		}
	}

	for (i=0; i<kit->ifkit->sensors; i++) {
		if (test_and_clear_bit(i, &kit->sensor_events)) {
			sprintf(sysfs_file, "sensor%d", i + 1);
			sysfs_notify(&kit->intf->dev.kobj, NULL, sysfs_file);
		}
	}
}

#define show_set_output(value)		\
static ssize_t set_output##value(struct device *dev, struct device_attribute *attr, const char *buf,	\
							size_t count)	\
{									\
	struct usb_interface *intf = to_usb_interface(dev);		\
	struct interfacekit *kit = usb_get_intfdata(intf);		\
	int enabled;							\
	int retval;							\
									\
	if (sscanf(buf, "%d", &enabled) < 1)				\
		return -EINVAL;						\
									\
	retval = change_outputs(kit, value - 1, enabled);		\
									\
	return retval ? retval : count;					\
}									\
									\
static ssize_t show_output##value(struct device *dev, struct device_attribute *attr, char *buf)	\
{									\
	struct usb_interface *intf = to_usb_interface(dev);		\
	struct interfacekit *kit = usb_get_intfdata(intf);		\
									\
	return sprintf(buf, "%d\n", !!test_bit(value - 1, &kit->outputs));\
}									\
static DEVICE_ATTR(output##value, S_IWUGO | S_IRUGO,			\
		show_output##value, set_output##value);
show_set_output(1);
show_set_output(2);
show_set_output(3);
show_set_output(4);
show_set_output(5);
show_set_output(6);
show_set_output(7);
show_set_output(8);
show_set_output(9);
show_set_output(10);
show_set_output(11);
show_set_output(12);
show_set_output(13);
show_set_output(14);
show_set_output(15);
show_set_output(16);

#define show_input(value)	\
static ssize_t show_input##value(struct device *dev, struct device_attribute *attr, char *buf)	\
{									\
	struct usb_interface *intf = to_usb_interface(dev);		\
	struct interfacekit *kit = usb_get_intfdata(intf);		\
									\
	return sprintf(buf, "%d\n", (int)kit->inputs[value - 1]);	\
}									\
static DEVICE_ATTR(input##value, S_IRUGO, show_input##value, NULL);

show_input(1);
show_input(2);
show_input(3);
show_input(4);
show_input(5);
show_input(6);
show_input(7);
show_input(8);
show_input(9);
show_input(10);
show_input(11);
show_input(12);
show_input(13);
show_input(14);
show_input(15);
show_input(16);

#define show_sensor(value)	\
static ssize_t show_sensor##value(struct device *dev, struct device_attribute *attr, char *buf)	\
{									\
	struct usb_interface *intf = to_usb_interface(dev);		\
	struct interfacekit *kit = usb_get_intfdata(intf);		\
									\
	return sprintf(buf, "%d\n", (int)kit->sensors[value - 1]);	\
}									\
static DEVICE_ATTR(sensor##value, S_IRUGO, show_sensor##value, NULL);

show_sensor(1);
show_sensor(2);
show_sensor(3);
show_sensor(4);
show_sensor(5);
show_sensor(6);
show_sensor(7);
show_sensor(8);

static int interfacekit_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_host_interface *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct interfacekit *kit;
	struct driver_interfacekit *ifkit;
	int pipe, maxp, rc = -ENOMEM;

	ifkit = (struct driver_interfacekit *)id->driver_info;
	if (!ifkit)
		return -ENODEV;

	interface = intf->cur_altsetting;
	if (interface->desc.bNumEndpoints != 1)
		return -ENODEV;

	endpoint = &interface->endpoint[0].desc;
	if (!(endpoint->bEndpointAddress & 0x80)) 
		return -ENODEV;
	/*
	 * bmAttributes
	 */
	pipe = usb_rcvintpipe(dev, endpoint->bEndpointAddress);
	maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));
	
	kit = kzalloc(sizeof(*kit), GFP_KERNEL);
	if (!kit)
		goto out;

	kit->ifkit = ifkit;
	kit->data = usb_buffer_alloc(dev, URB_INT_SIZE, SLAB_ATOMIC, &kit->data_dma);
	if (!kit->data)
		goto out;

	kit->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!kit->irq)
		goto out;

	kit->udev = usb_get_dev(dev);
	kit->intf = intf;
	INIT_WORK(&kit->do_notify, do_notify, kit);
	usb_fill_int_urb(kit->irq, kit->udev, pipe, kit->data,
			maxp > URB_INT_SIZE ? URB_INT_SIZE : maxp,
			interfacekit_irq, kit, endpoint->bInterval);
	kit->irq->transfer_dma = kit->data_dma;
	kit->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	usb_set_intfdata(intf, kit);

	if (usb_submit_urb(kit->irq, GFP_KERNEL)) {
		rc = -EIO;
		goto out;
	}

	if (ifkit->outputs >= 4) {
		device_create_file(&intf->dev, &dev_attr_output1);
		device_create_file(&intf->dev, &dev_attr_output2);
		device_create_file(&intf->dev, &dev_attr_output3);
		device_create_file(&intf->dev, &dev_attr_output4);
	}
	if (ifkit->outputs >= 8) {
		device_create_file(&intf->dev, &dev_attr_output5);
		device_create_file(&intf->dev, &dev_attr_output6);
		device_create_file(&intf->dev, &dev_attr_output7);
		device_create_file(&intf->dev, &dev_attr_output8);
	} 
	if (ifkit->outputs == 16) {
		device_create_file(&intf->dev, &dev_attr_output9);
		device_create_file(&intf->dev, &dev_attr_output10);
		device_create_file(&intf->dev, &dev_attr_output11);
		device_create_file(&intf->dev, &dev_attr_output12);
		device_create_file(&intf->dev, &dev_attr_output13);
		device_create_file(&intf->dev, &dev_attr_output14);
		device_create_file(&intf->dev, &dev_attr_output15);
		device_create_file(&intf->dev, &dev_attr_output16);
	}

	if (ifkit->inputs >= 4) {
		device_create_file(&intf->dev, &dev_attr_input1);
		device_create_file(&intf->dev, &dev_attr_input2);
		device_create_file(&intf->dev, &dev_attr_input3);
		device_create_file(&intf->dev, &dev_attr_input4);
	}
	if (ifkit->inputs >= 8) {
		device_create_file(&intf->dev, &dev_attr_input5);
		device_create_file(&intf->dev, &dev_attr_input6);
		device_create_file(&intf->dev, &dev_attr_input7);
		device_create_file(&intf->dev, &dev_attr_input8);
	}
	if (ifkit->inputs == 16) {
		device_create_file(&intf->dev, &dev_attr_input9);
		device_create_file(&intf->dev, &dev_attr_input10);
		device_create_file(&intf->dev, &dev_attr_input11);
		device_create_file(&intf->dev, &dev_attr_input12);
		device_create_file(&intf->dev, &dev_attr_input13);
		device_create_file(&intf->dev, &dev_attr_input14);
		device_create_file(&intf->dev, &dev_attr_input15);
		device_create_file(&intf->dev, &dev_attr_input16);
	}

	if (ifkit->sensors >= 4) {
		device_create_file(&intf->dev, &dev_attr_sensor1);
		device_create_file(&intf->dev, &dev_attr_sensor2);
		device_create_file(&intf->dev, &dev_attr_sensor3);
		device_create_file(&intf->dev, &dev_attr_sensor4);
	}
	if (ifkit->sensors >= 7) {
		device_create_file(&intf->dev, &dev_attr_sensor5);
		device_create_file(&intf->dev, &dev_attr_sensor6);
		device_create_file(&intf->dev, &dev_attr_sensor7);
	}
	if (ifkit->sensors == 8)
		device_create_file(&intf->dev, &dev_attr_sensor8);

	if (ifkit->has_lcd)
		device_create_file(&intf->dev, &dev_attr_lcd);

	dev_info(&intf->dev, "USB PhidgetInterfaceKit %d/%d/%d attached\n",
			ifkit->sensors, ifkit->inputs, ifkit->outputs);

	return 0;

out:
	if (kit) {
		if (kit->irq)
			usb_free_urb(kit->irq);
		if (kit->data)
			usb_buffer_free(dev, URB_INT_SIZE, kit->data, kit->data_dma);
		kfree(kit);
	}

	return rc;
}

static void interfacekit_disconnect(struct usb_interface *interface)
{
	struct interfacekit *kit;

	kit = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);
	if (!kit)
		return;

	usb_kill_urb(kit->irq);
	usb_free_urb(kit->irq);
	usb_buffer_free(kit->udev, URB_INT_SIZE, kit->data, kit->data_dma);

	cancel_delayed_work(&kit->do_notify);

	if (kit->ifkit->outputs >= 4) {
		device_remove_file(&interface->dev, &dev_attr_output1);
		device_remove_file(&interface->dev, &dev_attr_output2);
		device_remove_file(&interface->dev, &dev_attr_output3);
		device_remove_file(&interface->dev, &dev_attr_output4);
	}
	if (kit->ifkit->outputs >= 8) {
		device_remove_file(&interface->dev, &dev_attr_output5);
		device_remove_file(&interface->dev, &dev_attr_output6);
		device_remove_file(&interface->dev, &dev_attr_output7);
		device_remove_file(&interface->dev, &dev_attr_output8);
	}
	if (kit->ifkit->outputs == 16) {
		device_remove_file(&interface->dev, &dev_attr_output9);
		device_remove_file(&interface->dev, &dev_attr_output10);
		device_remove_file(&interface->dev, &dev_attr_output11);
		device_remove_file(&interface->dev, &dev_attr_output12);
		device_remove_file(&interface->dev, &dev_attr_output13);
		device_remove_file(&interface->dev, &dev_attr_output14);
		device_remove_file(&interface->dev, &dev_attr_output15);
		device_remove_file(&interface->dev, &dev_attr_output16);
	}

	if (kit->ifkit->inputs >= 4) {
		device_remove_file(&interface->dev, &dev_attr_input1);
		device_remove_file(&interface->dev, &dev_attr_input2);
		device_remove_file(&interface->dev, &dev_attr_input3);
		device_remove_file(&interface->dev, &dev_attr_input4);
	}
	if (kit->ifkit->inputs >= 8) {
		device_remove_file(&interface->dev, &dev_attr_input5);
		device_remove_file(&interface->dev, &dev_attr_input6);
		device_remove_file(&interface->dev, &dev_attr_input7);
		device_remove_file(&interface->dev, &dev_attr_input8);
	}
	if (kit->ifkit->inputs == 16) {
		device_remove_file(&interface->dev, &dev_attr_input9);
		device_remove_file(&interface->dev, &dev_attr_input10);
		device_remove_file(&interface->dev, &dev_attr_input11);
		device_remove_file(&interface->dev, &dev_attr_input12);
		device_remove_file(&interface->dev, &dev_attr_input13);
		device_remove_file(&interface->dev, &dev_attr_input14);
		device_remove_file(&interface->dev, &dev_attr_input15);
		device_remove_file(&interface->dev, &dev_attr_input16);
	}

	if (kit->ifkit->sensors >= 4) {
		device_remove_file(&interface->dev, &dev_attr_sensor1);
		device_remove_file(&interface->dev, &dev_attr_sensor2);
		device_remove_file(&interface->dev, &dev_attr_sensor3);
		device_remove_file(&interface->dev, &dev_attr_sensor4);
	}
	if (kit->ifkit->sensors >= 7) {
		device_remove_file(&interface->dev, &dev_attr_sensor5);
		device_remove_file(&interface->dev, &dev_attr_sensor6);
		device_remove_file(&interface->dev, &dev_attr_sensor7);
	}
	if (kit->ifkit->sensors == 8)
		device_remove_file(&interface->dev, &dev_attr_sensor8);

	if (kit->ifkit->has_lcd)
		device_remove_file(&interface->dev, &dev_attr_lcd);

	dev_info(&interface->dev, "USB PhidgetInterfaceKit %d/%d/%d detached\n",
		kit->ifkit->sensors, kit->ifkit->inputs, kit->ifkit->outputs);

	usb_put_dev(kit->udev);
	kfree(kit);
}

static struct usb_driver interfacekit_driver = {
	.name = "phidgetkit",
	.probe = interfacekit_probe,
	.disconnect = interfacekit_disconnect,
	.id_table = id_table
};

static int __init interfacekit_init(void)
{
	int retval = 0;

	retval = usb_register(&interfacekit_driver);
	if (retval)
		err("usb_register failed. Error number %d", retval);

	return retval;
}

static void __exit interfacekit_exit(void)
{
	usb_deregister(&interfacekit_driver);
}

module_init(interfacekit_init);
module_exit(interfacekit_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
