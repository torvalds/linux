/* dvb-usb-remote.c is part of the DVB USB library.
 *
 * Copyright (C) 2004-6 Patrick Boettcher (patrick.boettcher@desy.de)
 * see dvb-usb-init.c for copyright information.
 *
 * This file contains functions for initializing the input-device and for
 * handling remote-control-queries.
 */
#include "dvb_usb_common.h"
#include <linux/usb/input.h>

/* Remote-control poll function - called every dib->rc_query_interval ms to see
 * whether the remote control has received anything.
 *
 * TODO: Fix the repeat rate of the input device.
 */
static void dvb_usb_read_remote_control(struct work_struct *work)
{
	struct dvb_usb_device *d =
		container_of(work, struct dvb_usb_device, rc_query_work.work);
	int err;

	/* TODO: need a lock here.  We can simply skip checking for the remote
	   control if we're busy. */

	/* when the parameter has been set to 1 via sysfs while the
	 * driver was running, or when bulk mode is enabled after IR init
	 */
	if (dvb_usb_disable_rc_polling || d->props.rc.bulk_mode)
		return;

	err = d->props.rc.rc_query(d);
	if (err)
		err("error %d while querying for an remote control event.",
			err);

	schedule_delayed_work(&d->rc_query_work,
			      msecs_to_jiffies(d->props.rc.rc_interval));
}

static int rc_core_dvb_usb_remote_init(struct dvb_usb_device *d)
{
	int err, rc_interval;
	struct rc_dev *dev;

	dev = rc_allocate_device();
	if (!dev)
		return -ENOMEM;

	dev->driver_name = d->props.rc.module_name;
	dev->map_name = d->rc_map;
	dev->change_protocol = d->props.rc.change_protocol;
	dev->allowed_protos = d->props.rc.allowed_protos;
	dev->driver_type = d->props.rc.driver_type;
	usb_to_input_id(d->udev, &dev->input_id);
	dev->input_name = "IR-receiver inside an USB DVB receiver";
	dev->input_phys = d->rc_phys;
	dev->dev.parent = &d->udev->dev;
	dev->priv = d;

	/* leave remote controller enabled even there is no default map */
	if (dev->map_name == NULL)
		dev->map_name = RC_MAP_EMPTY;

	err = rc_register_device(dev);
	if (err < 0) {
		rc_free_device(dev);
		return err;
	}

	d->input_dev = NULL;
	d->rc_dev = dev;

	if (!d->props.rc.rc_query || d->props.rc.bulk_mode)
		return 0;

	/* Polling mode - initialize a work queue for handling it */
	INIT_DELAYED_WORK(&d->rc_query_work, dvb_usb_read_remote_control);

	rc_interval = d->props.rc.rc_interval;

	info("schedule remote query interval to %d msecs.", rc_interval);
	schedule_delayed_work(&d->rc_query_work,
			      msecs_to_jiffies(rc_interval));

	return 0;
}

int dvb_usb_remote_init(struct dvb_usb_device *d)
{
	int err;

	if (dvb_usb_disable_rc_polling)
		return 0;

	if (d->props.rc.module_name == NULL)
		return 0;

	usb_make_path(d->udev, d->rc_phys, sizeof(d->rc_phys));
	strlcat(d->rc_phys, "/ir0", sizeof(d->rc_phys));

	/* Start the remote-control polling. */
	err = rc_core_dvb_usb_remote_init(d);
	if (err)
		return err;

	d->state |= DVB_USB_STATE_REMOTE;

	return 0;
}

int dvb_usb_remote_exit(struct dvb_usb_device *d)
{
	if (d->state & DVB_USB_STATE_REMOTE) {
		cancel_delayed_work_sync(&d->rc_query_work);
		rc_unregister_device(d->rc_dev);
	}
	d->state &= ~DVB_USB_STATE_REMOTE;
	return 0;
}
