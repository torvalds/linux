/* dvb-usb-dvb.c is part of the DVB USB library.
 *
 * Copyright (C) 2004-6 Patrick Boettcher (patrick.boettcher@desy.de)
 * see dvb-usb-init.c for copyright information.
 *
 * This file contains functions for initializing and handling the
 * linux-dvb API.
 */
#include "dvb_usb_common.h"

static void dvb_usb_data_complete(struct usb_data_stream *stream, u8 *buf,
		size_t len)
{
	struct dvb_usb_adapter *adap = stream->user_priv;
	dvb_dmx_swfilter(&adap->demux, buf, len);
}

static void dvb_usb_data_complete_204(struct usb_data_stream *stream, u8 *buf,
		size_t len)
{
	struct dvb_usb_adapter *adap = stream->user_priv;
	dvb_dmx_swfilter_204(&adap->demux, buf, len);
}

static void dvb_usb_data_complete_raw(struct usb_data_stream *stream, u8 *buf,
		size_t len)
{
	struct dvb_usb_adapter *adap = stream->user_priv;
	dvb_dmx_swfilter_raw(&adap->demux, buf, len);
}

int dvb_usbv2_adapter_stream_init(struct dvb_usb_adapter *adap)
{
	pr_debug("%s: adap=%d\n", __func__, adap->id);

	adap->stream.udev = adap_to_d(adap)->udev;
	adap->stream.user_priv = adap;
	adap->stream.complete = dvb_usb_data_complete;

	return usb_urb_initv2(&adap->stream, &adap->props->stream);
}

int dvb_usbv2_adapter_stream_exit(struct dvb_usb_adapter *adap)
{
	pr_debug("%s: adap=%d\n", __func__, adap->id);
	usb_urb_exitv2(&adap->stream);

	return 0;
}

/* does the complete input transfer handling */
static inline int dvb_usb_ctrl_feed(struct dvb_demux_feed *dvbdmxfeed, int count)
{
	struct dvb_usb_adapter *adap = dvbdmxfeed->demux->priv;
	struct dvb_usb_device *d = adap_to_d(adap);
	int ret;
	pr_debug("%s: adap=%d active_fe=%d feed_type=%d setting pid [%s]: " \
			"%04x (%04d) at index %d '%s'\n", __func__, adap->id,
			adap->active_fe, dvbdmxfeed->type,
			adap->pid_filtering ? "yes" : "no", dvbdmxfeed->pid,
			dvbdmxfeed->pid, dvbdmxfeed->index,
			(count == 1) ? "on" : "off");

	if (adap->active_fe == -1)
		return -EINVAL;

	adap->feed_count += count;

	/* stop feeding if it is last pid */
	if (adap->feed_count == 0) {
		pr_debug("%s: stop feeding\n", __func__);
		usb_urb_killv2(&adap->stream);

		if (d->props->streaming_ctrl) {
			ret = d->props->streaming_ctrl(adap, 0);
			if (ret < 0) {
				pr_err("%s: streaming_ctrl() failed=%d\n",
						KBUILD_MODNAME, ret);
				goto err_mutex_unlock;
			}
		}
		mutex_unlock(&adap->sync_mutex);
	}

	/* activate the pid on the device pid filter */
	if (adap->props->caps & DVB_USB_ADAP_HAS_PID_FILTER &&
			adap->pid_filtering &&
			adap->props->pid_filter)
		ret = adap->props->pid_filter(adap, dvbdmxfeed->index,
				dvbdmxfeed->pid, (count == 1) ? 1 : 0);
			if (ret < 0)
				pr_err("%s: pid_filter() failed=%d\n",
						KBUILD_MODNAME, ret);

	/* start feeding if it is first pid */
	if (adap->feed_count == 1 && count == 1) {
		struct usb_data_stream_properties stream_props;
		mutex_lock(&adap->sync_mutex);
		pr_debug("%s: start feeding\n", __func__);

		/* resolve input and output streaming paramters */
		if (d->props->get_stream_config) {
			memcpy(&stream_props, &adap->props->stream,
				sizeof(struct usb_data_stream_properties));
			ret = d->props->get_stream_config(
					adap->fe[adap->active_fe],
					&adap->ts_type, &stream_props);
			if (ret < 0)
				goto err_mutex_unlock;
		} else {
			stream_props = adap->props->stream;
		}

		switch (adap->ts_type) {
		case DVB_USB_FE_TS_TYPE_204:
			adap->stream.complete = dvb_usb_data_complete_204;
			break;
		case DVB_USB_FE_TS_TYPE_RAW:
			adap->stream.complete = dvb_usb_data_complete_raw;
			break;
		case DVB_USB_FE_TS_TYPE_188:
		default:
			adap->stream.complete = dvb_usb_data_complete;
			break;
		}

		usb_urb_submitv2(&adap->stream, &stream_props);

		if (adap->props->caps & DVB_USB_ADAP_HAS_PID_FILTER &&
				adap->props->caps &
				DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF &&
				adap->props->pid_filter_ctrl) {
			ret = adap->props->pid_filter_ctrl(adap,
					adap->pid_filtering);
			if (ret < 0) {
				pr_err("%s: pid_filter_ctrl() failed=%d\n",
						KBUILD_MODNAME, ret);
				goto err_mutex_unlock;
			}
		}

		if (d->props->streaming_ctrl) {
			ret = d->props->streaming_ctrl(adap, 1);
			if (ret < 0) {
				pr_err("%s: streaming_ctrl() failed=%d\n",
						KBUILD_MODNAME, ret);
				goto err_mutex_unlock;
			}
		}
	}

	return 0;
err_mutex_unlock:
	mutex_unlock(&adap->sync_mutex);
	pr_debug("%s: failed=%d\n", __func__, ret);
	return ret;
}

static int dvb_usb_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	return dvb_usb_ctrl_feed(dvbdmxfeed, 1);
}

static int dvb_usb_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	return dvb_usb_ctrl_feed(dvbdmxfeed, -1);
}

int dvb_usbv2_adapter_dvb_init(struct dvb_usb_adapter *adap)
{
	int ret;
	struct dvb_usb_device *d = adap_to_d(adap);
	pr_debug("%s: adap=%d\n", __func__, adap->id);

	ret = dvb_register_adapter(&adap->dvb_adap, d->name, d->props->owner,
			&d->udev->dev, d->props->adapter_nr);
	if (ret < 0) {
		pr_debug("%s: dvb_register_adapter() failed=%d\n", __func__,
				ret);
		goto err;
	}

	adap->dvb_adap.priv = adap;

	if (d->props->read_mac_address) {
		ret = d->props->read_mac_address(adap,
				adap->dvb_adap.proposed_mac);
		if (ret < 0)
			goto err_dmx;

		pr_info("%s: MAC address: %pM\n", KBUILD_MODNAME,
				adap->dvb_adap.proposed_mac);
	}

	adap->demux.dmx.capabilities = DMX_TS_FILTERING | DMX_SECTION_FILTERING;
	adap->demux.priv             = adap;
	adap->demux.filternum        = 0;
	if (adap->demux.filternum < adap->max_feed_count)
		adap->demux.filternum = adap->max_feed_count;
	adap->demux.feednum          = adap->demux.filternum;
	adap->demux.start_feed       = dvb_usb_start_feed;
	adap->demux.stop_feed        = dvb_usb_stop_feed;
	adap->demux.write_to_decoder = NULL;
	ret = dvb_dmx_init(&adap->demux);
	if (ret < 0) {
		pr_err("%s: dvb_dmx_init() failed=%d\n", KBUILD_MODNAME, ret);
		goto err_dmx;
	}

	adap->dmxdev.filternum       = adap->demux.filternum;
	adap->dmxdev.demux           = &adap->demux.dmx;
	adap->dmxdev.capabilities    = 0;
	ret = dvb_dmxdev_init(&adap->dmxdev, &adap->dvb_adap);
	if (ret < 0) {
		pr_err("%s: dvb_dmxdev_init() failed=%d\n", KBUILD_MODNAME,
				ret);
		goto err_dmx_dev;
	}

	ret = dvb_net_init(&adap->dvb_adap, &adap->dvb_net, &adap->demux.dmx);
	if (ret < 0) {
		pr_err("%s: dvb_net_init() failed=%d\n", KBUILD_MODNAME, ret);
		goto err_net_init;
	}

	mutex_init(&adap->sync_mutex);

	return 0;
err_net_init:
	dvb_dmxdev_release(&adap->dmxdev);
err_dmx_dev:
	dvb_dmx_release(&adap->demux);
err_dmx:
	dvb_unregister_adapter(&adap->dvb_adap);
err:
	adap->dvb_adap.priv = NULL;
	return ret;
}

int dvb_usbv2_adapter_dvb_exit(struct dvb_usb_adapter *adap)
{
	pr_debug("%s: adap=%d\n", __func__, adap->id);

	if (adap->dvb_adap.priv) {
		dvb_net_release(&adap->dvb_net);
		adap->demux.dmx.close(&adap->demux.dmx);
		dvb_dmxdev_release(&adap->dmxdev);
		dvb_dmx_release(&adap->demux);
		dvb_unregister_adapter(&adap->dvb_adap);
	}

	return 0;
}

static int dvb_usb_fe_wakeup(struct dvb_frontend *fe)
{
	int ret;
	struct dvb_usb_adapter *adap = fe->dvb->priv;
	struct dvb_usb_device *d = adap_to_d(adap);
	mutex_lock(&adap->sync_mutex);
	pr_debug("%s: adap=%d fe=%d\n", __func__, adap->id, fe->id);

	ret = dvb_usbv2_device_power_ctrl(d, 1);
	if (ret < 0)
		goto err;

	if (d->props->frontend_ctrl) {
		ret = d->props->frontend_ctrl(fe, 1);
		if (ret < 0)
			goto err;
	}

	if (adap->fe_init[fe->id]) {
		ret = adap->fe_init[fe->id](fe);
		if (ret < 0)
			goto err;
	}

	adap->active_fe = fe->id;
	mutex_unlock(&adap->sync_mutex);

	return 0;
err:
	mutex_unlock(&adap->sync_mutex);
	pr_debug("%s: failed=%d\n", __func__, ret);
	return ret;
}

static int dvb_usb_fe_sleep(struct dvb_frontend *fe)
{
	int ret;
	struct dvb_usb_adapter *adap = fe->dvb->priv;
	struct dvb_usb_device *d = adap_to_d(adap);
	mutex_lock(&adap->sync_mutex);
	pr_debug("%s: adap=%d fe=%d\n", __func__, adap->id, fe->id);

	if (adap->fe_sleep[fe->id]) {
		ret = adap->fe_sleep[fe->id](fe);
		if (ret < 0)
			goto err;
	}

	if (d->props->frontend_ctrl) {
		ret = d->props->frontend_ctrl(fe, 0);
		if (ret < 0)
			goto err;
	}

	ret = dvb_usbv2_device_power_ctrl(d, 0);
	if (ret < 0)
		goto err;

	adap->active_fe = -1;
	mutex_unlock(&adap->sync_mutex);

	return 0;
err:
	mutex_unlock(&adap->sync_mutex);
	pr_debug("%s: failed=%d\n", __func__, ret);
	return ret;
}

int dvb_usbv2_adapter_frontend_init(struct dvb_usb_adapter *adap)
{
	int ret, i, count_registered = 0;
	struct dvb_usb_device *d = adap_to_d(adap);
	pr_debug("%s: adap=%d\n", __func__, adap->id);

	memset(adap->fe, 0, sizeof(adap->fe));
	adap->active_fe = -1;

	if (d->props->frontend_attach) {
		ret = d->props->frontend_attach(adap);
		if (ret < 0) {
			pr_debug("%s: frontend_attach() failed=%d\n", __func__,
					ret);
			goto err_dvb_frontend_detach;
		}
	} else {
		pr_debug("%s: frontend_attach() do not exists\n", __func__);
		ret = 0;
		goto err;
	}

	for (i = 0; i < MAX_NO_OF_FE_PER_ADAP && adap->fe[i]; i++) {
		adap->fe[i]->id = i;

		/* re-assign sleep and wakeup functions */
		adap->fe_init[i] = adap->fe[i]->ops.init;
		adap->fe[i]->ops.init  = dvb_usb_fe_wakeup;
		adap->fe_sleep[i] = adap->fe[i]->ops.sleep;
		adap->fe[i]->ops.sleep = dvb_usb_fe_sleep;

		ret = dvb_register_frontend(&adap->dvb_adap, adap->fe[i]);
		if (ret < 0) {
			pr_err("%s: frontend%d registration failed\n",
					KBUILD_MODNAME, i);
			goto err_dvb_unregister_frontend;
		}

		count_registered++;
	}

	if (d->props->tuner_attach) {
		ret = d->props->tuner_attach(adap);
		if (ret < 0) {
			pr_debug("%s: tuner_attach() failed=%d\n", __func__,
					ret);
			goto err_dvb_unregister_frontend;
		}
	}

	return 0;

err_dvb_unregister_frontend:
	for (i = count_registered - 1; i >= 0; i--)
		dvb_unregister_frontend(adap->fe[i]);

err_dvb_frontend_detach:
	for (i = MAX_NO_OF_FE_PER_ADAP - 1; i >= 0; i--) {
		if (adap->fe[i])
			dvb_frontend_detach(adap->fe[i]);
	}

err:
	pr_debug("%s: failed=%d\n", __func__, ret);
	return ret;
}

int dvb_usbv2_adapter_frontend_exit(struct dvb_usb_adapter *adap)
{
	int i;
	pr_debug("%s: adap=%d\n", __func__, adap->id);

	for (i = MAX_NO_OF_FE_PER_ADAP - 1; i >= 0; i--) {
		if (adap->fe[i]) {
			dvb_unregister_frontend(adap->fe[i]);
			dvb_frontend_detach(adap->fe[i]);
		}
	}

	return 0;
}
