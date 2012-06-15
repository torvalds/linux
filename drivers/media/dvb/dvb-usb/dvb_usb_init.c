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
#include <linux/usb/input.h>

int dvb_usbv2_disable_rc_polling;
module_param_named(disable_rc_polling, dvb_usbv2_disable_rc_polling, int, 0644);
MODULE_PARM_DESC(disable_rc_polling,
		"disable remote control polling (default: 0).");
static int dvb_usb_force_pid_filter_usage;
module_param_named(force_pid_filter_usage, dvb_usb_force_pid_filter_usage,
		int, 0444);
MODULE_PARM_DESC(force_pid_filter_usage, "force all dvb-usb-devices to use a" \
		" PID filter, if any (default: 0).");

static int dvb_usbv2_download_firmware(struct dvb_usb_device *d)
{
	int ret;
	const struct firmware *fw = NULL;
	const char *name;

	/* resolve firmware name */
	name = d->props->firmware;
	if (d->props->get_firmware_name) {
		ret = d->props->get_firmware_name(d, &name);
		if (ret < 0)
			goto err;
	}

	if (!d->props->download_firmware) {
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

	ret = d->props->download_firmware(d, fw);

	release_firmware(fw);

	if (ret < 0)
		goto err;

	return ret;
err:
	pr_debug("%s: failed=%d\n", __func__, ret);
	return ret;
}

static int dvb_usbv2_i2c_init(struct dvb_usb_device *d)
{
	int ret;

	pr_debug("%s:\n", __func__);

	if (!d->props->i2c_algo)
		return 0;

	strlcpy(d->i2c_adap.name, d->name, sizeof(d->i2c_adap.name));
	d->i2c_adap.algo = d->props->i2c_algo;
	d->i2c_adap.dev.parent = &d->udev->dev;
	i2c_set_adapdata(&d->i2c_adap, d);

	ret = i2c_add_adapter(&d->i2c_adap);
	if (ret < 0) {
		d->i2c_adap.algo = NULL;
		pr_err("%s: i2c_add_adapter() failed=%d\n", KBUILD_MODNAME,
				ret);
		goto err;
	}

	return 0;
err:
	pr_debug("%s: failed=%d\n", __func__, ret);
	return ret;
}

static int dvb_usbv2_i2c_exit(struct dvb_usb_device *d)
{
	pr_debug("%s:\n", __func__);

	if (d->i2c_adap.algo)
		i2c_del_adapter(&d->i2c_adap);

	return 0;
}

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

static int dvb_usbv2_remote_init(struct dvb_usb_device *d)
{
	int ret;
	struct rc_dev *dev;

	pr_debug("%s:\n", __func__);

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

	return 0;
err:
	pr_debug("%s: failed=%d\n", __func__, ret);
	return ret;
}

static int dvb_usbv2_remote_exit(struct dvb_usb_device *d)
{
	pr_debug("%s:\n", __func__);

	if (d->rc_dev) {
		cancel_delayed_work_sync(&d->rc_query_work);
		rc_unregister_device(d->rc_dev);
		d->rc_dev = NULL;
	}

	return 0;
}

static int dvb_usbv2_adapter_init(struct dvb_usb_device *d)
{
	struct dvb_usb_adapter *adap;
	int ret, i, adapter_count;

	/* resolve adapter count */
	adapter_count = d->props->num_adapters;
	if (d->props->get_adapter_count) {
		ret = d->props->get_adapter_count(d);
		if (ret < 0)
			goto err;

		adapter_count = ret;
	}

	for (i = 0; i < adapter_count; i++) {
		adap = &d->adapter[i];
		adap->dev = d;
		adap->id = i;
		adap->props = &d->props->adapter[i];

		/* speed - when running at FULL speed we need a HW PID filter */
		if (d->udev->speed == USB_SPEED_FULL &&
				!(adap->props->caps & DVB_USB_ADAP_HAS_PID_FILTER)) {
			pr_err("%s: this USB2.0 device cannot be run on a " \
					"USB1.1 port (it lacks a hardware " \
					"PID filter)\n", KBUILD_MODNAME);
			ret = -ENODEV;
			goto err;
		} else if ((d->udev->speed == USB_SPEED_FULL &&
				adap->props->caps & DVB_USB_ADAP_HAS_PID_FILTER) ||
				(adap->props->caps & DVB_USB_ADAP_NEED_PID_FILTERING)) {
			pr_info("%s: will use the device's hardware PID " \
					"filter (table count: %d)\n",
					KBUILD_MODNAME,
					adap->props->pid_filter_count);
			adap->pid_filtering  = 1;
			adap->max_feed_count = adap->props->pid_filter_count;
		} else {
			pr_info("%s: will pass the complete MPEG2 transport " \
					"stream to the software demuxer\n",
					KBUILD_MODNAME);
			adap->pid_filtering  = 0;
			adap->max_feed_count = 255;
		}

		if (!adap->pid_filtering && dvb_usb_force_pid_filter_usage &&
				adap->props->caps & DVB_USB_ADAP_HAS_PID_FILTER) {
			pr_info("%s: pid filter enabled by module option\n",
					KBUILD_MODNAME);
			adap->pid_filtering  = 1;
			adap->max_feed_count = adap->props->pid_filter_count;
		}

		ret = dvb_usbv2_adapter_stream_init(adap);
		if (ret)
			goto err;

		ret = dvb_usbv2_adapter_dvb_init(adap);
		if (ret)
			goto err;

		ret = dvb_usbv2_adapter_frontend_init(adap);
		if (ret)
			goto err;

		/* use exclusive FE lock if there is multiple shared FEs */
		if (adap->fe[1])
			adap->dvb_adap.mfe_shared = 1;

		adap->dvb_adap.fe_ioctl_override = d->props->fe_ioctl_override;

		d->num_adapters_initialized++;
	}

	return 0;
err:
	pr_debug("%s: failed=%d\n", __func__, ret);
	return ret;
}

static int dvb_usbv2_adapter_exit(struct dvb_usb_device *d)
{
	int i;

	pr_debug("%s:\n", __func__);

	for (i = MAX_NO_OF_ADAPTER_PER_DEVICE - 1; i >= 0; i--) {
		dvb_usbv2_adapter_frontend_exit(&d->adapter[i]);
		dvb_usbv2_adapter_dvb_exit(&d->adapter[i]);
		dvb_usbv2_adapter_stream_exit(&d->adapter[i]);
	}

	d->num_adapters_initialized = 0;

	return 0;
}

/* general initialization functions */
static int dvb_usbv2_exit(struct dvb_usb_device *d)
{
	pr_debug("%s:\n", __func__);

	dvb_usbv2_remote_exit(d);
	dvb_usbv2_adapter_exit(d);
	dvb_usbv2_i2c_exit(d);
	kfree(d->priv);
	kfree(d);

	return 0;
}

static int dvb_usbv2_init(struct dvb_usb_device *d)
{
	int ret = 0;

	dvb_usbv2_device_power_ctrl(d, 1);

	if (d->props->read_config) {
		ret = d->props->read_config(d);
		if (ret < 0)
			goto err;
	}

	ret = dvb_usbv2_i2c_init(d);
	if (ret < 0)
		goto err;

	ret = dvb_usbv2_adapter_init(d);
	if (ret < 0)
		goto err;

	if (d->props->init) {
		ret = d->props->init(d);
		if (ret < 0)
			goto err;
	}

	ret = dvb_usbv2_remote_init(d);
	if (ret < 0)
		goto err;

	dvb_usbv2_device_power_ctrl(d, 0);

	return 0;
err:
	dvb_usbv2_device_power_ctrl(d, 0);
	pr_debug("%s: failed=%d\n", __func__, ret);
	return ret;
}

int dvb_usbv2_device_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	int ret;

	if (onoff)
		d->powered++;
	else
		d->powered--;

	if (d->powered == 0 || (onoff && d->powered == 1)) {
		/* when switching from 1 to 0 or from 0 to 1 */
		pr_debug("%s: power control=%d\n", __func__, onoff);
		if (d->props->power_ctrl) {
			ret = d->props->power_ctrl(d, onoff);
			goto err;
		}
	}

	return 0;
err:
	pr_debug("%s: failed=%d\n", __func__, ret);
	return ret;
}

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

	d->work_pid = current->pid;

	pr_debug("%s: work_pid=%d\n", __func__, d->work_pid);

	if (d->props->size_of_priv) {
		d->priv = kzalloc(d->props->size_of_priv, GFP_KERNEL);
		if (!d->priv) {
			pr_err("%s: kzalloc() failed\n", KBUILD_MODNAME);
			ret = -ENOMEM;
			goto err_usb_driver_release_interface;
		}
	}

	if (d->props->identify_state) {
		ret = d->props->identify_state(d);
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
		ret = dvb_usbv2_download_firmware(d);
		if (ret == 0) {
			/* device is warm, continue initialization */
			;
		} else if (ret == RECONNECTS_USB) {
			/*
			 * USB core will call disconnect() and then probe()
			 * as device reconnects itself from the USB bus.
			 * disconnect() will release all driver resources
			 * and probe() is called for 'new' device. As 'new'
			 * device is warm we should never go here again.
			 */
			return;
		} else {
			/* Unexpected fatal error. We must unregister driver
			 * manually from the device, because device is already
			 * register by returning from probe() with success.
			 * usb_driver_release_interface() finally calls
			 * disconnect() in order to free resources.
			 */
			goto err_usb_driver_release_interface;
		}
	}

	pr_info("%s: found a '%s' in warm state\n", KBUILD_MODNAME, d->name);

	ret = dvb_usbv2_init(d);
	if (ret < 0)
		goto err_usb_driver_release_interface;

	pr_info("%s: '%s' successfully initialized and connected\n",
			KBUILD_MODNAME, d->name);

	return;
err_usb_driver_release_interface:
	pr_info("%s: '%s' error while loading driver (%d)\n", KBUILD_MODNAME,
			d->name, ret);
	/* it finally calls disconnect() which frees mem */
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
	d->props = driver_info->props;

	if (d->intf->cur_altsetting->desc.bInterfaceNumber !=
			d->props->bInterfaceNumber) {
		ret = -ENODEV;
		goto err_kfree;
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
	kfree(d);
err:
	pr_debug("%s: failed=%d\n", __func__, ret);
	return ret;
}
EXPORT_SYMBOL(dvb_usbv2_probe);

void dvb_usbv2_disconnect(struct usb_interface *intf)
{
	struct dvb_usb_device *d = usb_get_intfdata(intf);
	const char *name;

	pr_debug("%s: pid=%d work_pid=%d\n", __func__, current->pid,
			d->work_pid);

	/* ensure initialization work is finished until release resources */
	if (d->work_pid != current->pid)
		cancel_work_sync(&d->probe_work);

	if (d->props->disconnect)
		d->props->disconnect(d);

	name = d->name;
	dvb_usbv2_exit(d);

	pr_info("%s: '%s' successfully deinitialized and disconnected\n",
			KBUILD_MODNAME, name);
}
EXPORT_SYMBOL(dvb_usbv2_disconnect);

int dvb_usbv2_suspend(struct usb_interface *intf, pm_message_t msg)
{
	struct dvb_usb_device *d = usb_get_intfdata(intf);
	int i;

	pr_debug("%s:\n", __func__);

	/* stop remote controller poll */
	if (d->rc.query && !d->rc.bulk_mode)
		cancel_delayed_work_sync(&d->rc_query_work);

	/* stop streaming */
	for (i = d->num_adapters_initialized - 1; i >= 0; i--) {
		if (d->adapter[i].active_fe != -1)
			usb_urb_killv2(&d->adapter[i].stream);
	}

	return 0;
}
EXPORT_SYMBOL(dvb_usbv2_suspend);

int dvb_usbv2_resume(struct usb_interface *intf)
{
	struct dvb_usb_device *d = usb_get_intfdata(intf);
	int i;

	pr_debug("%s:\n", __func__);

	/* start streaming */
	for (i = 0; i < d->num_adapters_initialized; i++) {
		if (d->adapter[i].active_fe != -1)
			usb_urb_submitv2(&d->adapter[i].stream, NULL);
	}

	/* start remote controller poll */
	if (d->rc.query && !d->rc.bulk_mode)
		schedule_delayed_work(&d->rc_query_work,
				msecs_to_jiffies(d->rc.interval));

	return 0;
}
EXPORT_SYMBOL(dvb_usbv2_resume);

MODULE_VERSION("1.0");
MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@desy.de>");
MODULE_DESCRIPTION("A library module containing commonly used USB and DVB function USB DVB devices");
MODULE_LICENSE("GPL");
