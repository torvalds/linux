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

int dvb_usb_disable_rc_polling;
module_param_named(disable_rc_polling, dvb_usb_disable_rc_polling, int, 0644);
MODULE_PARM_DESC(disable_rc_polling,
		"disable remote control polling (default: 0).");

static int dvb_usb_force_pid_filter_usage;
module_param_named(force_pid_filter_usage, dvb_usb_force_pid_filter_usage,
		int, 0444);
MODULE_PARM_DESC(force_pid_filter_usage, "force all dvb-usb-devices to use a" \
		" PID filter, if any (default: 0).");

int dvb_usb_download_firmware(struct dvb_usb_device *d)
{
	int ret;
	const struct firmware *fw = NULL;
	const char *name;

	/* resolve firmware name */
	name = d->props.firmware;
	if (d->props.get_firmware_name) {
		ret = d->props.get_firmware_name(d, &name);
		if (ret < 0)
			return ret;
	}

	if (!d->props.download_firmware) {
		ret = -EINVAL;
		goto err;
	}

	ret = request_firmware(&fw, name, &d->udev->dev);
	if (ret < 0) {
		pr_err("%s: did not find the firmware file. (%s) " \
				"Please see linux/Documentation/dvb/ for " \
				"more details on firmware-problems. (%d)\n",
				KBUILD_MODNAME, name, ret);
		goto err;
	}

	pr_info("%s: downloading firmware from file '%s'\n", KBUILD_MODNAME,
			name);

	ret = d->props.download_firmware(d, fw);

	release_firmware(fw);

	if (ret < 0)
		goto err;

	return 0;
err:
	pr_debug("%s: failed=%d\n", __func__, ret);
	return ret;
}

int dvb_usb_i2c_init(struct dvb_usb_device *d)
{
	int ret = 0;

	if (!d->props.i2c_algo)
		return 0;

	strlcpy(d->i2c_adap.name, d->name, sizeof(d->i2c_adap.name));
	d->i2c_adap.algo = d->props.i2c_algo;
	d->i2c_adap.algo_data = NULL;
	d->i2c_adap.dev.parent = &d->udev->dev;

	i2c_set_adapdata(&d->i2c_adap, d);

	ret = i2c_add_adapter(&d->i2c_adap);
	if (ret < 0)
		pr_err("%s: could not add i2c adapter\n", KBUILD_MODNAME);

	d->state |= DVB_USB_STATE_I2C;

	return ret;
}

int dvb_usb_i2c_exit(struct dvb_usb_device *d)
{
	if (d->state & DVB_USB_STATE_I2C)
		i2c_del_adapter(&d->i2c_adap);
	d->state &= ~DVB_USB_STATE_I2C;
	return 0;
}

static int dvb_usb_adapter_init(struct dvb_usb_device *d)
{
	struct dvb_usb_adapter *adap;
	int ret, n, adapter_count;

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

		/* speed - when running at FULL speed we need a HW PID filter */
		if (d->udev->speed == USB_SPEED_FULL &&
				!(adap->props.caps & DVB_USB_ADAP_HAS_PID_FILTER)) {
			pr_err("%s: this USB2.0 device cannot be run on a " \
					"USB1.1 port (it lacks a hardware " \
					"PID filter)\n", KBUILD_MODNAME);
			return -ENODEV;
		} else if ((d->udev->speed == USB_SPEED_FULL &&
				adap->props.caps & DVB_USB_ADAP_HAS_PID_FILTER) ||
				(adap->props.caps & DVB_USB_ADAP_NEED_PID_FILTERING)) {
			pr_info("%s: will use the device's hardware PID " \
					"filter (table count: %d)\n",
					KBUILD_MODNAME,
					adap->props.pid_filter_count);
			adap->pid_filtering  = 1;
			adap->max_feed_count = adap->props.pid_filter_count;
		} else {
			pr_info("%s: will pass the complete MPEG2 transport " \
					"stream to the software demuxer\n",
					KBUILD_MODNAME);
			adap->pid_filtering  = 0;
			adap->max_feed_count = 255;
		}

		if (!adap->pid_filtering && dvb_usb_force_pid_filter_usage &&
				adap->props.caps & DVB_USB_ADAP_HAS_PID_FILTER) {
			pr_info("%s: pid filter enabled by module option\n",
					KBUILD_MODNAME);
			adap->pid_filtering  = 1;
			adap->max_feed_count = adap->props.pid_filter_count;
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
		if (adap->fe[1])
			adap->dvb_adap.mfe_shared = 1;

		d->num_adapters_initialized++;
		d->state |= DVB_USB_STATE_DVB;
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

	}
	d->num_adapters_initialized = 0;
	d->state &= ~DVB_USB_STATE_DVB;
	return 0;
}

/* general initialization functions */
static int dvb_usb_exit(struct dvb_usb_device *d)
{
	pr_debug("%s: state before exiting everything: %x\n", __func__, d->state);
	dvb_usb_remote_exit(d);
	dvb_usb_adapter_exit(d);
	dvb_usb_i2c_exit(d);
	pr_debug("%s: state should be zero now: %x\n", __func__, d->state);
	d->state = DVB_USB_STATE_INIT;
	kfree(d->priv);
	kfree(d);
	return 0;
}

static int dvb_usb_init(struct dvb_usb_device *d)
{
	int ret = 0;

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
		pr_err("%s: could not initialize remote control\n",
				KBUILD_MODNAME);

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
		pr_debug("%s: power control: %d\n", __func__, onoff);
		if (d->props.power_ctrl)
			return d->props.power_ctrl(d, onoff);
	}
	return 0;
}

/*
 * USB
 */

/*
 * udev, which is used for the firmware downloading, requires we cannot
 * block during module_init(). module_init() calls USB probe() which
 * is this routine. Due to that we delay actual operation using workqueue
 * and return always success here.
 */

static void dvb_usbv2_init_work(struct work_struct *work)
{
	int ret;
	struct dvb_usb_device *d =
			container_of(work, struct dvb_usb_device, probe_work);
	bool cold = false;

	pr_debug("%s:\n", __func__);

	if (d->props.size_of_priv) {
		d->priv = kzalloc(d->props.size_of_priv, GFP_KERNEL);
		if (!d->priv) {
			pr_err("%s: kzalloc() failed\n", KBUILD_MODNAME);
			ret = -ENOMEM;
			goto err_usb_driver_release_interface;
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
			goto err_usb_driver_release_interface;
		}
	}

	if (cold) {
		pr_info("%s: found a '%s' in cold state\n",
				KBUILD_MODNAME, d->name);
		ret = dvb_usb_download_firmware(d);
		if (ret == 0) {
			;
		} else if (ret == RECONNECTS_USB) {
			ret = 0;
			goto exit_usb_driver_release_interface;
		} else {
			goto err_usb_driver_release_interface;
		}
	}

	pr_info("%s: found a '%s' in warm state\n", KBUILD_MODNAME, d->name);

	ret = dvb_usb_init(d);
	if (ret < 0)
		goto err_usb_driver_release_interface;

	pr_info("%s: '%s' successfully initialized and connected\n",
			KBUILD_MODNAME, d->name);

	return;
err_usb_driver_release_interface:
	pr_info("%s: '%s' error while loading driver (%d)\n", KBUILD_MODNAME,
			d->name, ret);
exit_usb_driver_release_interface:
	/* it finally calls .disconnect() which frees mem */
	usb_driver_release_interface(to_usb_driver(d->intf->dev.driver),
			d->intf);
	pr_debug("%s: failed=%d\n", __func__, ret);
	return;
}

int dvb_usbv2_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	int ret;
	struct dvb_usb_device *d;
	struct dvb_usb_driver_info *driver_info =
			(struct dvb_usb_driver_info *) id->driver_info;

	pr_debug("%s: bInterfaceNumber=%d\n", __func__,
			intf->cur_altsetting->desc.bInterfaceNumber);

	if (!id->driver_info) {
		pr_err("%s: driver_info failed\n", KBUILD_MODNAME);
		ret = -ENODEV;
		goto err;
	}

	d = kzalloc(sizeof(struct dvb_usb_device), GFP_KERNEL);
	if (!d) {
		pr_err("%s: kzalloc() failed\n", KBUILD_MODNAME);
		ret = -ENOMEM;
		goto err;
	}

	d->name = driver_info->name;
	d->rc_map = driver_info->rc_map;
	d->udev = interface_to_usbdev(intf);
	d->intf = intf;
	memcpy(&d->props, driver_info->props,
			sizeof(struct dvb_usb_device_properties));

	if (d->intf->cur_altsetting->desc.bInterfaceNumber !=
			d->props.bInterfaceNumber) {
		ret = 0;
		goto exit_kfree;
	}

	mutex_init(&d->usb_mutex);
	mutex_init(&d->i2c_mutex);
	INIT_WORK(&d->probe_work, dvb_usbv2_init_work);
	usb_set_intfdata(intf, d);
	ret = schedule_work(&d->probe_work);
	if (ret < 0) {
		pr_err("%s: schedule_work() failed\n", KBUILD_MODNAME);
		goto err_kfree;
	}

	return 0;
err_kfree:
	usb_set_intfdata(intf, NULL);
exit_kfree:
	kfree(d);
err:
	pr_debug("%s: failed=%d\n", __func__, ret);
	return ret;
}
EXPORT_SYMBOL(dvb_usbv2_probe);

void dvb_usbv2_disconnect(struct usb_interface *intf)
{
	struct dvb_usb_device *d = usb_get_intfdata(intf);
	const char *name = "generic DVB-USB module";

	pr_debug("%s:\n", __func__);

	/*
	 * FIXME: We should ensure initialization work is finished
	 * until exit from this routine (cancel_work_sync / flush_work).
	 * Unfortunately usb_driver_release_interface() call finally goes
	 * here too and in that case we endup deadlock. How to perform
	 * operation conditionally only on disconned / unload?
	 */

	usb_set_intfdata(intf, NULL);
	if (d) {
		name = d->name;
		dvb_usb_exit(d);
	}

	pr_info("%s: '%s' successfully deinitialized and disconnected\n",
			KBUILD_MODNAME, name);
}
EXPORT_SYMBOL(dvb_usbv2_disconnect);

MODULE_VERSION("1.0");
MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@desy.de>");
MODULE_DESCRIPTION("A library module containing commonly used USB and DVB function USB DVB devices");
MODULE_LICENSE("GPL");
