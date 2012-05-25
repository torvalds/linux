/*
 * DVB USB library - provides a generic interface for a DVB USB device driver.
 *
 * dvb-usb-init.c
 *
 * Copyright (C) 2004-6 Patrick Boettcher (patrick.boettcher@desy.de)
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the
 *	Free Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */
#include "dvb_usb_common.h"

/* debug */
int dvb_usb_debug;
module_param_named(debug, dvb_usb_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info,xfer=2,pll=4,ts=8"\
		",err=16,rc=32,fw=64,mem=128,uxfer=256  (or-able))."
		DVB_USB_DEBUG_STATUS);

int dvb_usb_disable_rc_polling;
module_param_named(disable_rc_polling, dvb_usb_disable_rc_polling, int, 0644);
MODULE_PARM_DESC(disable_rc_polling,
		"disable remote control polling (default: 0).");

static int dvb_usb_force_pid_filter_usage;
module_param_named(force_pid_filter_usage, dvb_usb_force_pid_filter_usage,
		int, 0444);
MODULE_PARM_DESC(force_pid_filter_usage, "force all dvb-usb-devices to use a" \
		" PID filter, if any (default: 0).");

static int dvb_usb_adapter_init(struct dvb_usb_device *d)
{
	struct dvb_usb_adapter *adap;
	int ret, n, o, adapter_count;

	/* resolve adapter count */
	adapter_count = d->props.num_adapters;
	if (d->props.get_adapter_count) {
		ret = d->props.get_adapter_count(d);
		if (ret < 0)
			goto err;

		adapter_count = ret;
	}

	for (n = 0; n < adapter_count; n++) {
		adap = &d->adapter[n];
		adap->dev = d;
		adap->id  = n;

		memcpy(&adap->props, &d->props.adapter[n],
				sizeof(struct dvb_usb_adapter_properties));

		for (o = 0; o < adap->props.num_frontends; o++) {
			struct dvb_usb_adapter_fe_properties *props =
					&adap->props.fe[o];
			/* speed - when running at FULL speed we need a HW
			 * PID filter */
			if (d->udev->speed == USB_SPEED_FULL &&
					!(props->caps & DVB_USB_ADAP_HAS_PID_FILTER)) {
				err("This USB2.0 device cannot be run on a" \
					" USB1.1 port. (it lacks a" \
					" hardware PID filter)");
				return -ENODEV;
			}

			if ((d->udev->speed == USB_SPEED_FULL &&
					props->caps & DVB_USB_ADAP_HAS_PID_FILTER) ||
					(props->caps & DVB_USB_ADAP_NEED_PID_FILTERING)) {
				info("will use the device's hardware PID" \
					" filter (table count: %d).",
					props->pid_filter_count);
				adap->fe_adap[o].pid_filtering  = 1;
				adap->fe_adap[o].max_feed_count =
						props->pid_filter_count;
			} else {
				info("will pass the complete MPEG2 transport" \
					" stream to the software demuxer.");
				adap->fe_adap[o].pid_filtering  = 0;
				adap->fe_adap[o].max_feed_count = 255;
			}

			if (!adap->fe_adap[o].pid_filtering &&
					dvb_usb_force_pid_filter_usage &&
					props->caps & DVB_USB_ADAP_HAS_PID_FILTER) {
				info("pid filter enabled by module option.");
				adap->fe_adap[o].pid_filtering  = 1;
				adap->fe_adap[o].max_feed_count =
						props->pid_filter_count;
			}

			if (props->size_of_priv > 0) {
				adap->fe_adap[o].priv = kzalloc(props->size_of_priv, GFP_KERNEL);
				if (adap->fe_adap[o].priv == NULL) {
					err("no memory for priv for adapter" \
						" %d fe %d.", n, o);
					return -ENOMEM;
				}
			}
		}

		if (adap->props.size_of_priv > 0) {
			adap->priv = kzalloc(adap->props.size_of_priv,
					GFP_KERNEL);
			if (adap->priv == NULL) {
				err("no memory for priv for adapter %d.", n);
				return -ENOMEM;
			}
		}

		ret = dvb_usb_adapter_stream_init(adap);
		if (ret)
			return ret;

		ret = dvb_usb_adapter_dvb_init(adap);
		if (ret)
			return ret;

		ret = dvb_usb_adapter_frontend_init(adap);
		if (ret)
			return ret;

		/* use exclusive FE lock if there is multiple shared FEs */
		if (adap->fe_adap[1].fe)
			adap->dvb_adap.mfe_shared = 1;

		d->num_adapters_initialized++;
		d->state |= DVB_USB_STATE_DVB;
	}

	/*
	 * when reloading the driver w/o replugging the device
	 * sometimes a timeout occures, this helps
	 */
	if (d->props.generic_bulk_ctrl_endpoint != 0) {
		usb_clear_halt(d->udev, usb_sndbulkpipe(d->udev,
			d->props.generic_bulk_ctrl_endpoint));
		usb_clear_halt(d->udev, usb_rcvbulkpipe(d->udev,
			d->props.generic_bulk_ctrl_endpoint));
	}

	return 0;
err:
	pr_debug("%s: failed=%d\n", __func__, ret);
	return ret;
}

static int dvb_usb_adapter_exit(struct dvb_usb_device *d)
{
	int n;

	for (n = 0; n < d->num_adapters_initialized; n++) {
		dvb_usb_adapter_frontend_exit(&d->adapter[n]);
		dvb_usb_adapter_dvb_exit(&d->adapter[n]);
		dvb_usb_adapter_stream_exit(&d->adapter[n]);
		kfree(d->adapter[n].priv);
	}
	d->num_adapters_initialized = 0;
	d->state &= ~DVB_USB_STATE_DVB;
	return 0;
}


/* general initialization functions */
static int dvb_usb_exit(struct dvb_usb_device *d)
{
	deb_info("state before exiting everything: %x\n", d->state);
	dvb_usb_remote_exit(d);
	dvb_usb_adapter_exit(d);
	dvb_usb_i2c_exit(d);
	deb_info("state should be zero now: %x\n", d->state);
	d->state = DVB_USB_STATE_INIT;
	kfree(d->priv);
	kfree(d);
	return 0;
}

static int dvb_usb_init(struct dvb_usb_device *d)
{
	int ret = 0;

	mutex_init(&d->usb_mutex);
	mutex_init(&d->i2c_mutex);

	d->state = DVB_USB_STATE_INIT;

	/* check the capabilities and set appropriate variables */
	dvb_usb_device_power_ctrl(d, 1);

	/* read config */
	if (d->props.read_config) {
		ret = d->props.read_config(d);
		if (ret < 0)
			goto err;
	}

	ret = dvb_usb_i2c_init(d);
	if (ret == 0)
		ret = dvb_usb_adapter_init(d);

	if (ret) {
		dvb_usb_exit(d);
		return ret;
	}

	if (d->props.init)
		d->props.init(d);

	ret = dvb_usb_remote_init(d);
	if (ret)
		err("could not initialize remote control.");

	dvb_usb_device_power_ctrl(d, 0);

	return 0;
err:
	pr_debug("%s: failed=%d\n", __func__, ret);
	return ret;
}

int dvb_usb_device_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	if (onoff)
		d->powered++;
	else
		d->powered--;

	if (d->powered == 0 || (onoff && d->powered == 1)) {
		/* when switching from 1 to 0 or from 0 to 1 */
		deb_info("power control: %d\n", onoff);
		if (d->props.power_ctrl)
			return d->props.power_ctrl(d, onoff);
	}
	return 0;
}

/*
 * USB
 */
int dvb_usbv2_device_init(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct dvb_usb_device *d = NULL;
	struct dvb_usb_driver_info *driver_info =
			(struct dvb_usb_driver_info *) id->driver_info;
	const struct dvb_usb_device_properties *props = driver_info->props;
	int ret = -ENOMEM;
	bool cold = false;

	d = kzalloc(sizeof(struct dvb_usb_device), GFP_KERNEL);
	if (d == NULL) {
		err("no memory for 'struct dvb_usb_device'");
		return -ENOMEM;
	}

	d->udev = udev;
	d->name = driver_info->name;
	d->rc_map = driver_info->rc_map;
	memcpy(&d->props, props, sizeof(struct dvb_usb_device_properties));

	if (d->props.size_of_priv > 0) {
		d->priv = kzalloc(d->props.size_of_priv, GFP_KERNEL);
		if (d->priv == NULL) {
			err("no memory for priv in 'struct dvb_usb_device'");
			ret = -ENOMEM;
			goto err_kfree;
		}
	}

	if (d->props.identify_state) {
		ret = d->props.identify_state(d);
		if (ret == 0) {
			;
		} else if (ret == COLD) {
			cold = true;
			ret = 0;
		} else {
			goto err_kfree;
		}
	}

	if (cold) {
		info("found a '%s' in cold state, will try to load a firmware",
				d->name);
		ret = dvb_usb_download_firmware(d);
		if (ret == 0) {
			;
		} else if (ret == RECONNECTS_USB) {
			ret = 0;
			goto err_kfree;
		} else {
			goto err_kfree;
		}
	}

	info("found a '%s' in warm state.", d->name);

	usb_set_intfdata(intf, d);

	ret = dvb_usb_init(d);

	if (ret == 0)
		info("%s successfully initialized and connected.", d->name);
	else
		info("%s error while loading driver (%d)", d->name, ret);

	return 0;

err_kfree:
	kfree(d->priv);
	kfree(d);

	return ret;
}
EXPORT_SYMBOL(dvb_usbv2_device_init);

void dvb_usbv2_device_exit(struct usb_interface *intf)
{
	struct dvb_usb_device *d = usb_get_intfdata(intf);
	const char *name = NULL;

	usb_set_intfdata(intf, NULL);
	if (d) {
		name = d->name;
		dvb_usb_exit(d);
	}
	info("%s successfully deinitialized and disconnected.", name);
}
EXPORT_SYMBOL(dvb_usbv2_device_exit);

MODULE_VERSION("1.0");
MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@desy.de>");
MODULE_DESCRIPTION("A library module containing commonly used USB and DVB function USB DVB devices");
MODULE_LICENSE("GPL");
