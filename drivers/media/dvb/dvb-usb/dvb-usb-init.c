/*
 * DVB USB library - provides a generic interface for a DVB USB device driver.
 *
 * dvb-usb-init.c
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@desy.de)
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the Free
 *	Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */
#include "dvb-usb-common.h"

/* debug */
int dvb_usb_debug;
module_param_named(debug,dvb_usb_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info,xfer=2,pll=4,ts=8,err=16,rc=32,fw=64 (or-able))." DVB_USB_DEBUG_STATUS);

int dvb_usb_disable_rc_polling;
module_param_named(disable_rc_polling, dvb_usb_disable_rc_polling, int, 0644);
MODULE_PARM_DESC(disable_rc_polling, "disable remote control polling (default: 0).");

/* general initialization functions */
int dvb_usb_exit(struct dvb_usb_device *d)
{
	deb_info("state before exiting everything: %x\n",d->state);
	dvb_usb_remote_exit(d);
	dvb_usb_fe_exit(d);
	dvb_usb_i2c_exit(d);
	dvb_usb_dvb_exit(d);
	dvb_usb_urb_exit(d);
	deb_info("state should be zero now: %x\n",d->state);
	d->state = DVB_USB_STATE_INIT;
	kfree(d->priv);
	kfree(d);
	return 0;
}

static int dvb_usb_init(struct dvb_usb_device *d)
{
	int ret = 0;

	sema_init(&d->usb_sem, 1);
	sema_init(&d->i2c_sem, 1);

	d->state = DVB_USB_STATE_INIT;

/* check the capabilites and set appropriate variables */

/* speed - when running at FULL speed we need a HW PID filter */
	if (d->udev->speed == USB_SPEED_FULL && !(d->props.caps & DVB_USB_HAS_PID_FILTER)) {
		err("This USB2.0 device cannot be run on a USB1.1 port. (it lacks a hardware PID filter)");
		return -ENODEV;
	}

	if ((d->udev->speed == USB_SPEED_FULL && d->props.caps & DVB_USB_HAS_PID_FILTER) ||
		(d->props.caps & DVB_USB_NEED_PID_FILTERING)) {
		info("will use the device's hardware PID filter (table count: %d).",d->props.pid_filter_count);
		d->pid_filtering = 1;
		d->max_feed_count = d->props.pid_filter_count;
	} else {
		info("will pass the complete MPEG2 transport stream to the software demuxer.");
		d->pid_filtering = 0;
		d->max_feed_count = 255;
	}

	if (d->props.power_ctrl)
		d->props.power_ctrl(d,1);

	if ((ret = dvb_usb_urb_init(d)) ||
		(ret = dvb_usb_dvb_init(d)) ||
		(ret = dvb_usb_i2c_init(d)) ||
		(ret = dvb_usb_fe_init(d))) {
		dvb_usb_exit(d);
		return ret;
	}

	if ((ret = dvb_usb_remote_init(d)))
		err("could not initialize remote control.");

	if (d->props.power_ctrl)
		d->props.power_ctrl(d,0);

	return 0;
}

/* determine the name and the state of the just found USB device */
static struct dvb_usb_device_description * dvb_usb_find_device(struct usb_device *udev,struct dvb_usb_properties *props, int *cold)
{
	int i,j;
	struct dvb_usb_device_description *desc = NULL;
	*cold = -1;

	for (i = 0; i < props->num_device_descs; i++) {

		for (j = 0; j < DVB_USB_ID_MAX_NUM && props->devices[i].cold_ids[j] != NULL; j++) {
			deb_info("check for cold %x %x\n",props->devices[i].cold_ids[j]->idVendor, props->devices[i].cold_ids[j]->idProduct);
			if (props->devices[i].cold_ids[j]->idVendor  == le16_to_cpu(udev->descriptor.idVendor) &&
				props->devices[i].cold_ids[j]->idProduct == le16_to_cpu(udev->descriptor.idProduct)) {
				*cold = 1;
				desc = &props->devices[i];
				break;
			}
		}

		if (desc != NULL)
			break;

		for (j = 0; j < DVB_USB_ID_MAX_NUM && props->devices[i].warm_ids[j] != NULL; j++) {
			deb_info("check for warm %x %x\n",props->devices[i].warm_ids[j]->idVendor, props->devices[i].warm_ids[j]->idProduct);
			if (props->devices[i].warm_ids[j]->idVendor == le16_to_cpu(udev->descriptor.idVendor) &&
				props->devices[i].warm_ids[j]->idProduct == le16_to_cpu(udev->descriptor.idProduct)) {
				*cold = 0;
				desc = &props->devices[i];
				break;
			}
		}
	}

	if (desc != NULL && props->identify_state != NULL)
		props->identify_state(udev,props,&desc,cold);

	return desc;
}

/*
 * USB
 */

int dvb_usb_device_init(struct usb_interface *intf, struct dvb_usb_properties
		*props, struct module *owner,struct dvb_usb_device **du)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct dvb_usb_device *d = NULL;
	struct dvb_usb_device_description *desc = NULL;

	int ret = -ENOMEM,cold=0;

	if ((desc = dvb_usb_find_device(udev,props,&cold)) == NULL) {
		deb_err("something went very wrong, device was not found in current device list - let's see what comes next.\n");
		return -ENODEV;
	}

	if (cold) {
		info("found a '%s' in cold state, will try to load a firmware",desc->name);
		ret = usb_cypress_load_firmware(udev,props->firmware,props->usb_ctrl);
	} else {
		info("found a '%s' in warm state.",desc->name);
		d = kmalloc(sizeof(struct dvb_usb_device),GFP_KERNEL);
		if (d == NULL) {
			err("no memory for 'struct dvb_usb_device'");
			return ret;
		}
		memset(d,0,sizeof(struct dvb_usb_device));

		d->udev = udev;
		memcpy(&d->props,props,sizeof(struct dvb_usb_properties));
		d->desc = desc;
		d->owner = owner;

		if (d->props.size_of_priv > 0) {
			d->priv = kmalloc(d->props.size_of_priv,GFP_KERNEL);
			if (d->priv == NULL) {
				err("no memory for priv in 'struct dvb_usb_device'");
				kfree(d);
				return -ENOMEM;
			}
			memset(d->priv,0,d->props.size_of_priv);
		}

		usb_set_intfdata(intf, d);

		if (du != NULL)
			*du = d;

		ret = dvb_usb_init(d);
	}

	if (ret == 0)
		info("%s successfully initialized and connected.",desc->name);
	else
		info("%s error while loading driver (%d)",desc->name,ret);
	return ret;
}
EXPORT_SYMBOL(dvb_usb_device_init);

void dvb_usb_device_exit(struct usb_interface *intf)
{
	struct dvb_usb_device *d = usb_get_intfdata(intf);
	const char *name = "generic DVB-USB module";

	usb_set_intfdata(intf,NULL);
	if (d != NULL && d->desc != NULL) {
		name = d->desc->name;
		dvb_usb_exit(d);
	}
	info("%s successfully deinitialized and disconnected.",name);

}
EXPORT_SYMBOL(dvb_usb_device_exit);

MODULE_VERSION("0.3");
MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@desy.de>");
MODULE_DESCRIPTION("A library module containing commonly used USB and DVB function USB DVB devices");
MODULE_LICENSE("GPL");
