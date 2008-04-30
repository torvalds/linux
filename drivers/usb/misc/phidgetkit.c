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

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb.h>

#include "phidget.h"

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
	int amnesiac;
};

#define ifkit(_sensors, _inputs, _outputs, _lcd, _amnesiac)		\
{									\
	.sensors	= _sensors,					\
	.inputs		= _inputs,					\
	.outputs	= _outputs,					\
	.has_lcd	= _lcd,						\
	.amnesiac	= _amnesiac					\
};

static const struct driver_interfacekit ph_004 = ifkit(0, 0, 4, 0, 0);
static const struct driver_interfacekit ph_888n = ifkit(8, 8, 8, 0, 1);
static const struct driver_interfacekit ph_888o = ifkit(8, 8, 8, 0, 0);
static const struct driver_interfacekit ph_047 = ifkit(0, 4, 7, 1, 0);
static const struct driver_interfacekit ph_884 = ifkit(8, 8, 4, 0, 0);
static const struct driver_interfacekit ph_088 = ifkit(0, 8, 8, 1, 0);
static const struct driver_interfacekit ph_01616 = ifkit(0, 16, 16, 0, 0);

static unsigned long device_no;

struct interfacekit {
	struct usb_device *udev;
	struct usb_interface *intf;
	struct driver_interfacekit *ifkit;
	struct device *dev;
	unsigned long outputs;
	int dev_no;
	u8 inputs[MAX_INTERFACES];
	u16 sensors[MAX_INTERFACES];
	u8 lcd_files_on;

	struct urb *irq;
	unsigned char *data;
	dma_addr_t data_dma;

	struct delayed_work do_notify;
	struct delayed_work do_resubmit;
	unsigned long input_events;
	unsigned long sensor_events;
};

static struct usb_device_id id_table[] = {
	{USB_DEVICE(USB_VENDOR_ID_GLAB, USB_DEVICE_ID_INTERFACEKIT004),
		.driver_info = (kernel_ulong_t)&ph_004},
	{USB_DEVICE_VER(USB_VENDOR_ID_GLAB, USB_DEVICE_ID_INTERFACEKIT888, 0, 0x814),
		.driver_info = (kernel_ulong_t)&ph_888o},
	{USB_DEVICE_VER(USB_VENDOR_ID_GLAB, USB_DEVICE_ID_INTERFACEKIT888, 0x0815, 0xffff),
		.driver_info = (kernel_ulong_t)&ph_888n},
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

static int set_outputs(struct interfacekit *kit)
{
	u8 *buffer;
	int retval;

	buffer = kzalloc(4, GFP_KERNEL);
	if (!buffer) {
		dev_err(&kit->udev->dev, "%s - out of memory\n", __func__);
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

	if (kit->ifkit->amnesiac)
		schedule_delayed_work(&kit->do_resubmit, HZ / 2);

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
		dev_err(&kit->udev->dev, "%s - out of memory\n", __func__);
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
static ssize_t lcd_line_##number(struct device *dev,			\
					struct device_attribute *attr,	\
					const char *buf, size_t count)	\
{									\
	struct interfacekit *kit = dev_get_drvdata(dev);		\
	change_string(kit, buf, number - 1);				\
	return count;							\
}

#define lcd_line_attr(number)						\
	__ATTR(lcd_line_##number, S_IWUGO, NULL, lcd_line_##number)

set_lcd_line(1);
set_lcd_line(2);

static ssize_t set_backlight(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct interfacekit *kit = dev_get_drvdata(dev);
	int enabled;
	unsigned char *buffer;
	int retval = -ENOMEM;
	
	buffer = kzalloc(8, GFP_KERNEL);
	if (!buffer) {
		dev_err(&kit->udev->dev, "%s - out of memory\n", __func__);
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

static struct device_attribute dev_lcd_line_attrs[] = {
	lcd_line_attr(1),
	lcd_line_attr(2),
	__ATTR(backlight, S_IWUGO, NULL, set_backlight)
};

static void remove_lcd_files(struct interfacekit *kit)
{
	int i;

	if (kit->lcd_files_on) {
		dev_dbg(&kit->udev->dev, "Removing lcd files\n");

		for (i=0; i<ARRAY_SIZE(dev_lcd_line_attrs); i++)
			device_remove_file(kit->dev, &dev_lcd_line_attrs[i]);
	}
}

static ssize_t enable_lcd_files(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct interfacekit *kit = dev_get_drvdata(dev);
	int enable;
	int i, rc;
	
	if (kit->ifkit->has_lcd == 0)
		return -ENODEV;

	if (sscanf(buf, "%d", &enable) < 1)
		return -EINVAL;

	if (enable) {
		if (!kit->lcd_files_on) {
			dev_dbg(&kit->udev->dev, "Adding lcd files\n");
			for (i=0; i<ARRAY_SIZE(dev_lcd_line_attrs); i++) {
				rc = device_create_file(kit->dev,
					&dev_lcd_line_attrs[i]);
				if (rc)
					goto out;
			}
			kit->lcd_files_on = 1;
		}
	} else {
		if (kit->lcd_files_on) {
			remove_lcd_files(kit);
			kit->lcd_files_on = 0;
		}
	}
	
	return count;
out:
	while (i-- > 0)
		device_remove_file(kit->dev, &dev_lcd_line_attrs[i]);

	return rc;
}

static DEVICE_ATTR(lcd, S_IWUGO, NULL, enable_lcd_files);

static void interfacekit_irq(struct urb *urb)
{
	struct interfacekit *kit = urb->context;
	unsigned char *buffer = kit->data;
	int i, level, sensor;
	int retval;
	int status = urb->status;

	switch (status) {
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
		schedule_delayed_work(&kit->do_notify, 0);

resubmit:
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		err("can't resubmit intr, %s-%s/interfacekit0, retval %d",
			kit->udev->bus->bus_name,
			kit->udev->devpath, retval);
}

static void do_notify(struct work_struct *work)
{
	struct interfacekit *kit =
		container_of(work, struct interfacekit, do_notify.work);
	int i;
	char sysfs_file[8];

	for (i=0; i<kit->ifkit->inputs; i++) {
		if (test_and_clear_bit(i, &kit->input_events)) {
			sprintf(sysfs_file, "input%d", i + 1);
			sysfs_notify(&kit->dev->kobj, NULL, sysfs_file);
		}
	}

	for (i=0; i<kit->ifkit->sensors; i++) {
		if (test_and_clear_bit(i, &kit->sensor_events)) {
			sprintf(sysfs_file, "sensor%d", i + 1);
			sysfs_notify(&kit->dev->kobj, NULL, sysfs_file);
		}
	}
}

static void do_resubmit(struct work_struct *work)
{
	struct interfacekit *kit =
		container_of(work, struct interfacekit, do_resubmit.work);
	set_outputs(kit);
}

#define show_set_output(value)		\
static ssize_t set_output##value(struct device *dev,			\
					struct device_attribute *attr,	\
					const char *buf, size_t count)	\
{									\
	struct interfacekit *kit = dev_get_drvdata(dev);		\
	int enable;							\
	int retval;							\
									\
	if (sscanf(buf, "%d", &enable) < 1)				\
		return -EINVAL;						\
									\
	if (enable)							\
		set_bit(value - 1, &kit->outputs);			\
	else								\
		clear_bit(value - 1, &kit->outputs); 			\
									\
	retval = set_outputs(kit);					\
									\
	return retval ? retval : count;					\
}									\
									\
static ssize_t show_output##value(struct device *dev, 			\
					struct device_attribute *attr,	\
					char *buf)			\
{									\
	struct interfacekit *kit = dev_get_drvdata(dev);		\
									\
	return sprintf(buf, "%d\n", !!test_bit(value - 1, &kit->outputs));\
}

#define output_attr(value)						\
	__ATTR(output##value, S_IWUGO | S_IRUGO,			\
		show_output##value, set_output##value)

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

static struct device_attribute dev_output_attrs[] = {
	output_attr(1), output_attr(2), output_attr(3), output_attr(4),
	output_attr(5), output_attr(6), output_attr(7), output_attr(8),
	output_attr(9), output_attr(10), output_attr(11), output_attr(12),
	output_attr(13), output_attr(14), output_attr(15), output_attr(16)
};

#define show_input(value)	\
static ssize_t show_input##value(struct device *dev, 			\
			struct device_attribute *attr, char *buf)	\
{									\
	struct interfacekit *kit = dev_get_drvdata(dev);		\
									\
	return sprintf(buf, "%d\n", (int)kit->inputs[value - 1]);	\
}

#define input_attr(value)						\
	__ATTR(input##value, S_IRUGO, show_input##value, NULL)

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

static struct device_attribute dev_input_attrs[] = {
	input_attr(1), input_attr(2), input_attr(3), input_attr(4),
	input_attr(5), input_attr(6), input_attr(7), input_attr(8),
	input_attr(9), input_attr(10), input_attr(11), input_attr(12),
	input_attr(13), input_attr(14), input_attr(15), input_attr(16)
};

#define show_sensor(value)	\
static ssize_t show_sensor##value(struct device *dev,			\
					struct device_attribute *attr,	\
					char *buf)			\
{									\
	struct interfacekit *kit = dev_get_drvdata(dev);		\
									\
	return sprintf(buf, "%d\n", (int)kit->sensors[value - 1]);	\
}

#define sensor_attr(value)						\
	__ATTR(sensor##value, S_IRUGO, show_sensor##value, NULL)

show_sensor(1);
show_sensor(2);
show_sensor(3);
show_sensor(4);
show_sensor(5);
show_sensor(6);
show_sensor(7);
show_sensor(8);

static struct device_attribute dev_sensor_attrs[] = {
	sensor_attr(1), sensor_attr(2), sensor_attr(3), sensor_attr(4),
	sensor_attr(5), sensor_attr(6), sensor_attr(7), sensor_attr(8)
};

static int interfacekit_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_host_interface *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct interfacekit *kit;
	struct driver_interfacekit *ifkit;
	int pipe, maxp, rc = -ENOMEM;
	int bit, value, i;

	ifkit = (struct driver_interfacekit *)id->driver_info;
	if (!ifkit)
		return -ENODEV;

	interface = intf->cur_altsetting;
	if (interface->desc.bNumEndpoints != 1)
		return -ENODEV;

	endpoint = &interface->endpoint[0].desc;
	if (!usb_endpoint_dir_in(endpoint))
		return -ENODEV;
	/*
	 * bmAttributes
	 */
	pipe = usb_rcvintpipe(dev, endpoint->bEndpointAddress);
	maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));
	
	kit = kzalloc(sizeof(*kit), GFP_KERNEL);
	if (!kit)
		goto out;

	kit->dev_no = -1;
	kit->ifkit = ifkit;
	kit->data = usb_buffer_alloc(dev, URB_INT_SIZE, GFP_ATOMIC, &kit->data_dma);
	if (!kit->data)
		goto out;

	kit->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!kit->irq)
		goto out;

	kit->udev = usb_get_dev(dev);
	kit->intf = intf;
	INIT_DELAYED_WORK(&kit->do_notify, do_notify);
	INIT_DELAYED_WORK(&kit->do_resubmit, do_resubmit);
	usb_fill_int_urb(kit->irq, kit->udev, pipe, kit->data,
			maxp > URB_INT_SIZE ? URB_INT_SIZE : maxp,
			interfacekit_irq, kit, endpoint->bInterval);
	kit->irq->transfer_dma = kit->data_dma;
	kit->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	usb_set_intfdata(intf, kit);

        do {
                bit = find_first_zero_bit(&device_no, sizeof(device_no));
                value = test_and_set_bit(bit, &device_no);
        } while(value);
        kit->dev_no = bit;

        kit->dev = device_create(phidget_class, &kit->udev->dev, 0,
               		"interfacekit%d", kit->dev_no);
        if (IS_ERR(kit->dev)) {
                rc = PTR_ERR(kit->dev);
                kit->dev = NULL;
                goto out;
        }
	dev_set_drvdata(kit->dev, kit);

	if (usb_submit_urb(kit->irq, GFP_KERNEL)) {
		rc = -EIO;
		goto out;
	}

	for (i=0; i<ifkit->outputs; i++ ) {
		rc = device_create_file(kit->dev, &dev_output_attrs[i]);
		if (rc)
			goto out2;
	}

	for (i=0; i<ifkit->inputs; i++ ) {
		rc = device_create_file(kit->dev, &dev_input_attrs[i]);
		if (rc)
			goto out3;
	}

	for (i=0; i<ifkit->sensors; i++ ) {
		rc = device_create_file(kit->dev, &dev_sensor_attrs[i]);
		if (rc)
			goto out4;
	}

	if (ifkit->has_lcd) {
		rc = device_create_file(kit->dev, &dev_attr_lcd);
		if (rc)
			goto out4;

	}

	dev_info(&intf->dev, "USB PhidgetInterfaceKit %d/%d/%d attached\n",
			ifkit->sensors, ifkit->inputs, ifkit->outputs);

	return 0;

out4:
	while (i-- > 0)
		device_remove_file(kit->dev, &dev_sensor_attrs[i]);

	i = ifkit->inputs;
out3:
	while (i-- > 0)
		device_remove_file(kit->dev, &dev_input_attrs[i]);

	i = ifkit->outputs;
out2:
	while (i-- > 0)
		device_remove_file(kit->dev, &dev_output_attrs[i]);
out:
	if (kit) {
		usb_free_urb(kit->irq);
		if (kit->data)
			usb_buffer_free(dev, URB_INT_SIZE, kit->data, kit->data_dma);
		if (kit->dev)
			device_unregister(kit->dev);
		if (kit->dev_no >= 0)
			clear_bit(kit->dev_no, &device_no);

		kfree(kit);
	}

	return rc;
}

static void interfacekit_disconnect(struct usb_interface *interface)
{
	struct interfacekit *kit;
	int i;

	kit = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);
	if (!kit)
		return;

	usb_kill_urb(kit->irq);
	usb_free_urb(kit->irq);
	usb_buffer_free(kit->udev, URB_INT_SIZE, kit->data, kit->data_dma);

	cancel_delayed_work(&kit->do_notify);
	cancel_delayed_work(&kit->do_resubmit);

	for (i=0; i<kit->ifkit->outputs; i++)
		device_remove_file(kit->dev, &dev_output_attrs[i]);

	for (i=0; i<kit->ifkit->inputs; i++)
		device_remove_file(kit->dev, &dev_input_attrs[i]);

	for (i=0; i<kit->ifkit->sensors; i++)
		device_remove_file(kit->dev, &dev_sensor_attrs[i]);

	if (kit->ifkit->has_lcd) {
		device_remove_file(kit->dev, &dev_attr_lcd);
		remove_lcd_files(kit);
	}

	device_unregister(kit->dev);

	dev_info(&interface->dev, "USB PhidgetInterfaceKit %d/%d/%d detached\n",
		kit->ifkit->sensors, kit->ifkit->inputs, kit->ifkit->outputs);

	usb_put_dev(kit->udev);
	clear_bit(kit->dev_no, &device_no);

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
