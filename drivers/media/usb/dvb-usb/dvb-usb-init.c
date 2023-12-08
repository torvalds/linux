// SPDX-License-Identifier: GPL-2.0-only
/*
 * DVB USB library - provides a generic interface for a DVB USB device driver.
 *
 * dvb-usb-init.c
 *
 * Copyright (C) 2004-6 Patrick Boettcher (patrick.boettcher@posteo.de)
 *
 * see Documentation/driver-api/media/drivers/dvb-usb.rst for more information
 */
#include "dvb-usb-common.h"

/* debug */
int dvb_usb_debug;
module_param_named(debug, dvb_usb_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info,xfer=2,pll=4,ts=8,err=16,rc=32,fw=64,mem=128,uxfer=256  (or-able))." DVB_USB_DEBUG_STATUS);

int dvb_usb_disable_rc_polling;
module_param_named(disable_rc_polling, dvb_usb_disable_rc_polling, int, 0644);
MODULE_PARM_DESC(disable_rc_polling, "disable remote control polling (default: 0).");

static int dvb_usb_force_pid_filter_usage;
module_param_named(force_pid_filter_usage, dvb_usb_force_pid_filter_usage, int, 0444);
MODULE_PARM_DESC(force_pid_filter_usage, "force all dvb-usb-devices to use a PID filter, if any (default: 0).");

static int dvb_usb_adapter_init(struct dvb_usb_device *d, short *adapter_nrs)
{
	struct dvb_usb_adapter *adap;
	int ret, n, o;

	for (n = 0; n < d->props.num_adapters; n++) {
		adap = &d->adapter[n];
		adap->dev = d;
		adap->id  = n;

		memcpy(&adap->props, &d->props.adapter[n], sizeof(struct dvb_usb_adapter_properties));

		for (o = 0; o < adap->props.num_frontends; o++) {
			struct dvb_usb_adapter_fe_properties *props = &adap->props.fe[o];
			/* speed - when running at FULL speed we need a HW PID filter */
			if (d->udev->speed == USB_SPEED_FULL && !(props->caps & DVB_USB_ADAP_HAS_PID_FILTER)) {
				err("This USB2.0 device cannot be run on a USB1.1 port. (it lacks a hardware PID filter)");
				return -ENODEV;
			}

			if ((d->udev->speed == USB_SPEED_FULL && props->caps & DVB_USB_ADAP_HAS_PID_FILTER) ||
				(props->caps & DVB_USB_ADAP_NEED_PID_FILTERING)) {
				info("will use the device's hardware PID filter (table count: %d).", props->pid_filter_count);
				adap->fe_adap[o].pid_filtering  = 1;
				adap->fe_adap[o].max_feed_count = props->pid_filter_count;
			} else {
				info("will pass the complete MPEG2 transport stream to the software demuxer.");
				adap->fe_adap[o].pid_filtering  = 0;
				adap->fe_adap[o].max_feed_count = 255;
			}

			if (!adap->fe_adap[o].pid_filtering &&
				dvb_usb_force_pid_filter_usage &&
				props->caps & DVB_USB_ADAP_HAS_PID_FILTER) {
				info("pid filter enabled by module option.");
				adap->fe_adap[o].pid_filtering  = 1;
				adap->fe_adap[o].max_feed_count = props->pid_filter_count;
			}

			if (props->size_of_priv > 0) {
				adap->fe_adap[o].priv = kzalloc(props->size_of_priv, GFP_KERNEL);
				if (adap->fe_adap[o].priv == NULL) {
					err("no memory for priv for adapter %d fe %d.", n, o);
					return -ENOMEM;
				}
			}
		}

		if (adap->props.size_of_priv > 0) {
			adap->priv = kzalloc(adap->props.size_of_priv, GFP_KERNEL);
			if (adap->priv == NULL) {
				err("no memory for priv for adapter %d.", n);
				return -ENOMEM;
			}
		}

		ret = dvb_usb_adapter_stream_init(adap);
		if (ret)
			goto stream_init_err;

		ret = dvb_usb_adapter_dvb_init(adap, adapter_nrs);
		if (ret)
			goto dvb_init_err;

		ret = dvb_usb_adapter_frontend_init(adap);
		if (ret)
			goto frontend_init_err;

		/* use exclusive FE lock if there is multiple shared FEs */
		if (adap->fe_adap[1].fe)
			adap->dvb_adap.mfe_shared = 1;

		d->num_adapters_initialized++;
		d->state |= DVB_USB_STATE_DVB;
	}

	/*
	 * when reloading the driver w/o replugging the device
	 * sometimes a timeout occurs, this helps
	 */
	if (d->props.generic_bulk_ctrl_endpoint != 0) {
		usb_clear_halt(d->udev, usb_sndbulkpipe(d->udev, d->props.generic_bulk_ctrl_endpoint));
		usb_clear_halt(d->udev, usb_rcvbulkpipe(d->udev, d->props.generic_bulk_ctrl_endpoint));
	}

	return 0;

frontend_init_err:
	dvb_usb_adapter_dvb_exit(adap);
dvb_init_err:
	dvb_usb_adapter_stream_exit(adap);
stream_init_err:
	kfree(adap->priv);
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

	if (d->priv != NULL && d->props.priv_destroy != NULL)
		d->props.priv_destroy(d);

	kfree(d->priv);
	kfree(d);
	return 0;
}

static int dvb_usb_init(struct dvb_usb_device *d, short *adapter_nums)
{
	int ret = 0;

	mutex_init(&d->data_mutex);
	mutex_init(&d->usb_mutex);
	mutex_init(&d->i2c_mutex);

	d->state = DVB_USB_STATE_INIT;

	if (d->props.size_of_priv > 0) {
		d->priv = kzalloc(d->props.size_of_priv, GFP_KERNEL);
		if (d->priv == NULL) {
			err("no memory for priv in 'struct dvb_usb_device'");
			return -ENOMEM;
		}

		if (d->props.priv_init != NULL) {
			ret = d->props.priv_init(d);
			if (ret != 0)
				goto err_priv_init;
		}
	}

	/* check the capabilities and set appropriate variables */
	dvb_usb_device_power_ctrl(d, 1);

	ret = dvb_usb_i2c_init(d);
	if (ret)
		goto err_i2c_init;
	ret = dvb_usb_adapter_init(d, adapter_nums);
	if (ret)
		goto err_adapter_init;

	if ((ret = dvb_usb_remote_init(d)))
		err("could not initialize remote control.");

	dvb_usb_device_power_ctrl(d, 0);

	return 0;

err_adapter_init:
	dvb_usb_adapter_exit(d);
	dvb_usb_i2c_exit(d);
err_i2c_init:
	if (d->priv && d->props.priv_destroy)
		d->props.priv_destroy(d);
err_priv_init:
	kfree(d->priv);
	d->priv = NULL;
	return ret;
}

/* determine the name and the state of the just found USB device */
static const struct dvb_usb_device_description *dvb_usb_find_device(struct usb_device *udev, const struct dvb_usb_device_properties *props, int *cold)
{
	int i, j;
	const struct dvb_usb_device_description *desc = NULL;

	*cold = -1;

	for (i = 0; i < props->num_device_descs; i++) {

		for (j = 0; j < DVB_USB_ID_MAX_NUM && props->devices[i].cold_ids[j] != NULL; j++) {
			deb_info("check for cold %x %x\n", props->devices[i].cold_ids[j]->idVendor, props->devices[i].cold_ids[j]->idProduct);
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
			deb_info("check for warm %x %x\n", props->devices[i].warm_ids[j]->idVendor, props->devices[i].warm_ids[j]->idProduct);
			if (props->devices[i].warm_ids[j]->idVendor == le16_to_cpu(udev->descriptor.idVendor) &&
				props->devices[i].warm_ids[j]->idProduct == le16_to_cpu(udev->descriptor.idProduct)) {
				*cold = 0;
				desc = &props->devices[i];
				break;
			}
		}
	}

	if (desc != NULL && props->identify_state != NULL)
		props->identify_state(udev, props, &desc, cold);

	return desc;
}

int dvb_usb_device_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	if (onoff)
		d->powered++;
	else
		d->powered--;

	if (d->powered == 0 || (onoff && d->powered == 1)) { /* when switching from 1 to 0 or from 0 to 1 */
		deb_info("power control: %d\n", onoff);
		if (d->props.power_ctrl)
			return d->props.power_ctrl(d, onoff);
	}
	return 0;
}

/*
 * USB
 */
int dvb_usb_device_init(struct usb_interface *intf,
			const struct dvb_usb_device_properties *props,
			struct module *owner, struct dvb_usb_device **du,
			short *adapter_nums)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct dvb_usb_device *d = NULL;
	const struct dvb_usb_device_description *desc = NULL;

	int ret = -ENOMEM, cold = 0;

	if (du != NULL)
		*du = NULL;

	d = kzalloc(sizeof(*d), GFP_KERNEL);
	if (!d) {
		err("no memory for 'struct dvb_usb_device'");
		return -ENOMEM;
	}

	memcpy(&d->props, props, sizeof(struct dvb_usb_device_properties));

	desc = dvb_usb_find_device(udev, &d->props, &cold);
	if (!desc) {
		deb_err("something went very wrong, device was not found in current device list - let's see what comes next.\n");
		ret = -ENODEV;
		goto error;
	}

	if (cold) {
		info("found a '%s' in cold state, will try to load a firmware", desc->name);
		ret = dvb_usb_download_firmware(udev, props);
		if (!props->no_reconnect || ret != 0)
			goto error;
	}

	info("found a '%s' in warm state.", desc->name);
	d->udev = udev;
	d->desc = desc;
	d->owner = owner;

	usb_set_intfdata(intf, d);

	ret = dvb_usb_init(d, adapter_nums);
	if (ret) {
		info("%s error while loading driver (%d)", desc->name, ret);
		goto error;
	}

	if (du)
		*du = d;

	info("%s successfully initialized and connected.", desc->name);
	return 0;

 error:
	usb_set_intfdata(intf, NULL);
	kfree(d);
	return ret;
}
EXPORT_SYMBOL(dvb_usb_device_init);

void dvb_usb_device_exit(struct usb_interface *intf)
{
	struct dvb_usb_device *d = usb_get_intfdata(intf);
	const char *default_name = "generic DVB-USB module";
	char name[40];

	usb_set_intfdata(intf, NULL);
	if (d != NULL && d->desc != NULL) {
		strscpy(name, d->desc->name, sizeof(name));
		dvb_usb_exit(d);
	} else {
		strscpy(name, default_name, sizeof(name));
	}
	info("%s successfully deinitialized and disconnected.", name);

}
EXPORT_SYMBOL(dvb_usb_device_exit);

MODULE_VERSION("1.0");
MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@posteo.de>");
MODULE_DESCRIPTION("A library module containing commonly used USB and DVB function USB DVB devices");
MODULE_LICENSE("GPL");
