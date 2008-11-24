/*
 * USB Phidget MotorControl driver
 *
 * Copyright (C) 2006  Sean Young <sean@mess.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/usb.h>

#include "phidget.h"

#define DRIVER_AUTHOR "Sean Young <sean@mess.org>"
#define DRIVER_DESC "USB PhidgetMotorControl Driver"

#define USB_VENDOR_ID_GLAB		0x06c2
#define USB_DEVICE_ID_MOTORCONTROL	0x0058

#define URB_INT_SIZE			8

static unsigned long device_no;

struct motorcontrol {
	struct usb_device *udev;
	struct usb_interface *intf;
	struct device *dev;
	int dev_no;
	u8 inputs[4];
	s8 desired_speed[2];
	s8 speed[2];
	s16 _current[2];
	s8 acceleration[2];
	struct urb *irq;
	unsigned char *data;
	dma_addr_t data_dma;

	struct delayed_work do_notify;
	unsigned long input_events;
	unsigned long speed_events;
	unsigned long exceed_events;
};

static struct usb_device_id id_table[] = {
	{ USB_DEVICE(USB_VENDOR_ID_GLAB, USB_DEVICE_ID_MOTORCONTROL) },
	{}
};
MODULE_DEVICE_TABLE(usb, id_table);

static int set_motor(struct motorcontrol *mc, int motor)
{
	u8 *buffer;
	int speed, speed2, acceleration;
	int retval;

	buffer = kzalloc(8, GFP_KERNEL);
	if (!buffer) {
		dev_err(&mc->intf->dev, "%s - out of memory\n", __func__);
		return -ENOMEM;
	}

	acceleration = mc->acceleration[motor] * 10;
	/* -127 <= speed <= 127 */
	speed = (mc->desired_speed[motor] * 127) / 100;
	/* -0x7300 <= speed2 <= 0x7300 */
	speed2 = (mc->desired_speed[motor] * 230 * 128) / 100;

	buffer[0] = motor;
	buffer[1] = speed;
	buffer[2] = acceleration >> 8;
	buffer[3] = acceleration;
	buffer[4] = speed2 >> 8;
	buffer[5] = speed2;

	retval = usb_control_msg(mc->udev,
			 usb_sndctrlpipe(mc->udev, 0),
			 0x09, 0x21, 0x0200, 0x0000, buffer, 8, 2000);

	if (retval != 8)
		dev_err(&mc->intf->dev, "usb_control_msg returned %d\n",
				retval);
	kfree(buffer);

	return retval < 0 ? retval : 0;
}

static void motorcontrol_irq(struct urb *urb)
{
	struct motorcontrol *mc = urb->context;
	unsigned char *buffer = mc->data;
	int i, level;
	int retval;
	int status = urb->status;;

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
	for (i=0; i<4; i++) {
		level = (buffer[0] >> i) & 1;
		if (mc->inputs[i] != level) {
			mc->inputs[i] = level;
			set_bit(i, &mc->input_events);
		}
	}

	/* motor speed */
	if (buffer[2] == 0) {
		for (i=0; i<2; i++) {
		level = ((s8)buffer[4+i]) * 100 / 127;
			if (mc->speed[i] != level) {
				mc->speed[i] = level;
				set_bit(i, &mc->speed_events);
			}
		}
	} else {
		int index = buffer[3] & 1;

		level = ((s8)buffer[4] << 8) | buffer[5];
		level = level * 100 / 29440;
		if (mc->speed[index] != level) {
			mc->speed[index] = level;
			set_bit(index, &mc->speed_events);
		}

		level = ((s8)buffer[6] << 8) | buffer[7];
		mc->_current[index] = level * 100 / 1572;
	}

	if (buffer[1] & 1)
		set_bit(0, &mc->exceed_events);

	if (buffer[1] & 2)
		set_bit(1, &mc->exceed_events);

	if (mc->input_events || mc->exceed_events || mc->speed_events)
		schedule_delayed_work(&mc->do_notify, 0);

resubmit:
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		dev_err(&mc->intf->dev,
			"can't resubmit intr, %s-%s/motorcontrol0, retval %d\n",
			mc->udev->bus->bus_name,
			mc->udev->devpath, retval);
}

static void do_notify(struct work_struct *work)
{
	struct motorcontrol *mc =
		container_of(work, struct motorcontrol, do_notify.work);
	int i;
	char sysfs_file[8];

	for (i=0; i<4; i++) {
		if (test_and_clear_bit(i, &mc->input_events)) {
			sprintf(sysfs_file, "input%d", i);
			sysfs_notify(&mc->dev->kobj, NULL, sysfs_file);
		}
	}

	for (i=0; i<2; i++) {
		if (test_and_clear_bit(i, &mc->speed_events)) {
			sprintf(sysfs_file, "speed%d", i);
			sysfs_notify(&mc->dev->kobj, NULL, sysfs_file);
		}
	}

	for (i=0; i<2; i++) {
		if (test_and_clear_bit(i, &mc->exceed_events))
			dev_warn(&mc->intf->dev,
				"motor #%d exceeds 1.5 Amp current limit\n", i);
	}
}

#define show_set_speed(value)		\
static ssize_t set_speed##value(struct device *dev,			\
					struct device_attribute *attr,	\
					const char *buf, size_t count)	\
{									\
	struct motorcontrol *mc = dev_get_drvdata(dev);			\
	int speed;							\
	int retval;							\
									\
	if (sscanf(buf, "%d", &speed) < 1)				\
		return -EINVAL;						\
									\
	if (speed < -100 || speed > 100)				\
		return -EINVAL;						\
									\
	mc->desired_speed[value] = speed;				\
									\
	retval = set_motor(mc, value);					\
									\
	return retval ? retval : count;					\
}									\
									\
static ssize_t show_speed##value(struct device *dev,			\
					struct device_attribute *attr,	\
					char *buf)			\
{									\
	struct motorcontrol *mc = dev_get_drvdata(dev);			\
									\
	return sprintf(buf, "%d\n", mc->speed[value]);			\
}

#define speed_attr(value) 						\
	__ATTR(speed##value, S_IWUGO | S_IRUGO, 			\
		show_speed##value, set_speed##value)

show_set_speed(0);
show_set_speed(1);

#define show_set_acceleration(value)		\
static ssize_t set_acceleration##value(struct device *dev, 		\
					struct device_attribute *attr,	\
					const char *buf, size_t count)	\
{									\
	struct motorcontrol *mc = dev_get_drvdata(dev);			\
	int acceleration;						\
	int retval;							\
									\
	if (sscanf(buf, "%d", &acceleration) < 1)			\
		return -EINVAL;						\
									\
	if (acceleration < 0 || acceleration > 100)			\
		return -EINVAL;						\
									\
	mc->acceleration[value] = acceleration;				\
									\
	retval = set_motor(mc, value);					\
									\
	return retval ? retval : count;					\
}									\
									\
static ssize_t show_acceleration##value(struct device *dev,	 	\
					struct device_attribute *attr,	\
							char *buf)	\
{									\
	struct motorcontrol *mc = dev_get_drvdata(dev);			\
									\
	return sprintf(buf, "%d\n", mc->acceleration[value]);		\
}

#define acceleration_attr(value)	\
	__ATTR(acceleration##value, S_IWUGO | S_IRUGO,			\
		show_acceleration##value, set_acceleration##value)

show_set_acceleration(0);
show_set_acceleration(1);

#define show_current(value)	\
static ssize_t show_current##value(struct device *dev,			\
					struct device_attribute *attr,	\
					char *buf)			\
{									\
	struct motorcontrol *mc = dev_get_drvdata(dev);			\
									\
	return sprintf(buf, "%dmA\n", (int)mc->_current[value]);	\
}

#define current_attr(value)	\
	__ATTR(current##value, S_IRUGO, show_current##value, NULL)

show_current(0);
show_current(1);

#define show_input(value)	\
static ssize_t show_input##value(struct device *dev,			\
					struct device_attribute *attr,	\
					char *buf)			\
{									\
	struct motorcontrol *mc = dev_get_drvdata(dev);			\
									\
	return sprintf(buf, "%d\n", (int)mc->inputs[value]);		\
}

#define input_attr(value)	\
	__ATTR(input##value, S_IRUGO, show_input##value, NULL)

show_input(0);
show_input(1);
show_input(2);
show_input(3);

static struct device_attribute dev_attrs[] = {
	input_attr(0),
	input_attr(1),
	input_attr(2),
	input_attr(3),
	speed_attr(0),
	speed_attr(1),
	acceleration_attr(0),
	acceleration_attr(1),
	current_attr(0),
	current_attr(1)
};

static int motorcontrol_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_host_interface *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct motorcontrol *mc;
	int pipe, maxp, rc = -ENOMEM;
	int bit, value, i;

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

	mc = kzalloc(sizeof(*mc), GFP_KERNEL);
	if (!mc)
		goto out;

	mc->dev_no = -1;
	mc->data = usb_buffer_alloc(dev, URB_INT_SIZE, GFP_ATOMIC, &mc->data_dma);
	if (!mc->data)
		goto out;

	mc->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!mc->irq)
		goto out;

	mc->udev = usb_get_dev(dev);
	mc->intf = intf;
	mc->acceleration[0] = mc->acceleration[1] = 10;
	INIT_DELAYED_WORK(&mc->do_notify, do_notify);
	usb_fill_int_urb(mc->irq, mc->udev, pipe, mc->data,
			maxp > URB_INT_SIZE ? URB_INT_SIZE : maxp,
			motorcontrol_irq, mc, endpoint->bInterval);
	mc->irq->transfer_dma = mc->data_dma;
	mc->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	usb_set_intfdata(intf, mc);

	do {
		bit = find_first_zero_bit(&device_no, sizeof(device_no));
		value = test_and_set_bit(bit, &device_no);
	} while(value);
	mc->dev_no = bit;

	mc->dev = device_create(phidget_class, &mc->udev->dev, MKDEV(0, 0), mc,
				"motorcontrol%d", mc->dev_no);
	if (IS_ERR(mc->dev)) {
		rc = PTR_ERR(mc->dev);
		mc->dev = NULL;
		goto out;
	}

	if (usb_submit_urb(mc->irq, GFP_KERNEL)) {
		rc = -EIO;
		goto out;
	}

	for (i=0; i<ARRAY_SIZE(dev_attrs); i++) {
		rc = device_create_file(mc->dev, &dev_attrs[i]);
		if (rc)
			goto out2;
	}

	dev_info(&intf->dev, "USB PhidgetMotorControl attached\n");

	return 0;
out2:
	while (i-- > 0)
		device_remove_file(mc->dev, &dev_attrs[i]);
out:
	if (mc) {
		usb_free_urb(mc->irq);
		if (mc->data)
			usb_buffer_free(dev, URB_INT_SIZE, mc->data, mc->data_dma);
		if (mc->dev)
			device_unregister(mc->dev);
		if (mc->dev_no >= 0)
			clear_bit(mc->dev_no, &device_no);

		kfree(mc);
	}

	return rc;
}

static void motorcontrol_disconnect(struct usb_interface *interface)
{
	struct motorcontrol *mc;
	int i;

	mc = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);
	if (!mc)
		return;

	usb_kill_urb(mc->irq);
	usb_free_urb(mc->irq);
	usb_buffer_free(mc->udev, URB_INT_SIZE, mc->data, mc->data_dma);

	cancel_delayed_work(&mc->do_notify);

	for (i=0; i<ARRAY_SIZE(dev_attrs); i++)
		device_remove_file(mc->dev, &dev_attrs[i]);

	device_unregister(mc->dev);

	usb_put_dev(mc->udev);
	clear_bit(mc->dev_no, &device_no);
	kfree(mc);

	dev_info(&interface->dev, "USB PhidgetMotorControl detached\n");
}

static struct usb_driver motorcontrol_driver = {
	.name = "phidgetmotorcontrol",
	.probe = motorcontrol_probe,
	.disconnect = motorcontrol_disconnect,
	.id_table = id_table
};

static int __init motorcontrol_init(void)
{
	int retval = 0;

	retval = usb_register(&motorcontrol_driver);
	if (retval)
		err("usb_register failed. Error number %d", retval);

	return retval;
}

static void __exit motorcontrol_exit(void)
{
	usb_deregister(&motorcontrol_driver);
}

module_init(motorcontrol_init);
module_exit(motorcontrol_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
