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
	struct dvb_usb_device *d = container_of(work,
			struct dvb_usb_device, rc_query_work.work);
	int ret;

	/* TODO: need a lock here.  We can simply skip checking for the remote
	   control if we're busy. */

	/* when the parameter has been set to 1 via sysfs while the
	 * driver was running, or when bulk mode is enabled after IR init
	 */
	if (dvb_usbv2_disable_rc_polling || d->rc.bulk_mode)
		return;

	ret = d->rc.query(d);
	if (ret < 0)
		pr_err("%s: error %d while querying for an remote control " \
				"event\n", KBUILD_MODNAME, ret);

	schedule_delayed_work(&d->rc_query_work,
			      msecs_to_jiffies(d->rc.interval));
}

int dvb_usbv2_remote_init(struct dvb_usb_device *d)
{
	int ret;
	struct rc_dev *dev;

	if (dvb_usbv2_disable_rc_polling || !d->props->get_rc_config)
		return 0;

	ret = d->props->get_rc_config(d, &d->rc);
	if (ret < 0)
		goto err;

	dev = rc_allocate_device();
	if (!dev) {
		ret = -ENOMEM;
		goto err;
	}

	dev->dev.parent = &d->udev->dev;
	dev->input_name = "IR-receiver inside an USB DVB receiver";
	usb_make_path(d->udev, d->rc_phys, sizeof(d->rc_phys));
	strlcat(d->rc_phys, "/ir0", sizeof(d->rc_phys));
	dev->input_phys = d->rc_phys;
	usb_to_input_id(d->udev, &dev->input_id);
	/* TODO: likely RC-core should took const char * */
	dev->driver_name = (char *) d->props->driver_name;
	dev->driver_type = d->rc.driver_type;
	dev->allowed_protos = d->rc.allowed_protos;
	dev->change_protocol = d->rc.change_protocol;
	dev->priv = d;
	/* select used keymap */
	if (d->rc.map_name)
		dev->map_name = d->rc.map_name;
	else if (d->rc_map)
		dev->map_name = d->rc_map;
	else
		dev->map_name = RC_MAP_EMPTY; /* keep rc enabled */

	ret = rc_register_device(dev);
	if (ret < 0) {
		rc_free_device(dev);
		goto err;
	}

	d->input_dev = NULL;
	d->rc_dev = dev;

	/* start polling if needed */
	if (d->rc.query && !d->rc.bulk_mode) {
		/* initialize a work queue for handling polling */
		INIT_DELAYED_WORK(&d->rc_query_work,
				dvb_usb_read_remote_control);
		pr_info("%s: schedule remote query interval to %d msecs\n",
				KBUILD_MODNAME, d->rc.interval);
		schedule_delayed_work(&d->rc_query_work,
				msecs_to_jiffies(d->rc.interval));
	}

	d->state |= DVB_USB_STATE_REMOTE;

	return 0;
err:
	pr_debug("%s: failed=%d\n", __func__, ret);
	return ret;
}

int dvb_usbv2_remote_exit(struct dvb_usb_device *d)
{
	if (d->state & DVB_USB_STATE_REMOTE) {
		cancel_delayed_work_sync(&d->rc_query_work);
		rc_unregister_device(d->rc_dev);
	}

	d->state &= ~DVB_USB_STATE_REMOTE;

	return 0;
}
