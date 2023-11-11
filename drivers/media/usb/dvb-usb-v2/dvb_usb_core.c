// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DVB USB framework
 *
 * Copyright (C) 2004-6 Patrick Boettcher <patrick.boettcher@posteo.de>
 * Copyright (C) 2012 Antti Palosaari <crope@iki.fi>
 */

#include "dvb_usb_common.h"
#include <media/media-device.h>

static int dvb_usbv2_disable_rc_polling;
module_param_named(disable_rc_polling, dvb_usbv2_disable_rc_polling, int, 0644);
MODULE_PARM_DESC(disable_rc_polling,
		"disable remote control polling (default: 0)");
static int dvb_usb_force_pid_filter_usage;
module_param_named(force_pid_filter_usage, dvb_usb_force_pid_filter_usage,
		int, 0444);
MODULE_PARM_DESC(force_pid_filter_usage,
		"force all DVB USB devices to use a PID filter, if any (default: 0)");

static int dvb_usbv2_download_firmware(struct dvb_usb_device *d,
		const char *name)
{
	int ret;
	const struct firmware *fw;
	dev_dbg(&d->udev->dev, "%s:\n", __func__);

	if (!d->props->download_firmware) {
		ret = -EINVAL;
		goto err;
	}

	ret = request_firmware(&fw, name, &d->udev->dev);
	if (ret < 0) {
		dev_err(&d->udev->dev,
				"%s: Did not find the firmware file '%s' (status %d). You can use <kernel_dir>/scripts/get_dvb_firmware to get the firmware\n",
				KBUILD_MODNAME, name, ret);
		goto err;
	}

	dev_info(&d->udev->dev, "%s: downloading firmware from file '%s'\n",
			KBUILD_MODNAME, name);

	ret = d->props->download_firmware(d, fw);
	release_firmware(fw);
	if (ret < 0)
		goto err;

	return ret;
err:
	dev_dbg(&d->udev->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int dvb_usbv2_i2c_init(struct dvb_usb_device *d)
{
	int ret;
	dev_dbg(&d->udev->dev, "%s:\n", __func__);

	if (!d->props->i2c_algo)
		return 0;

	strscpy(d->i2c_adap.name, d->name, sizeof(d->i2c_adap.name));
	d->i2c_adap.algo = d->props->i2c_algo;
	d->i2c_adap.dev.parent = &d->udev->dev;
	i2c_set_adapdata(&d->i2c_adap, d);

	ret = i2c_add_adapter(&d->i2c_adap);
	if (ret < 0) {
		d->i2c_adap.algo = NULL;
		goto err;
	}

	return 0;
err:
	dev_dbg(&d->udev->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int dvb_usbv2_i2c_exit(struct dvb_usb_device *d)
{
	dev_dbg(&d->udev->dev, "%s:\n", __func__);

	if (d->i2c_adap.algo)
		i2c_del_adapter(&d->i2c_adap);

	return 0;
}

#if IS_ENABLED(CONFIG_RC_CORE)
static void dvb_usb_read_remote_control(struct work_struct *work)
{
	struct dvb_usb_device *d = container_of(work,
			struct dvb_usb_device, rc_query_work.work);
	int ret;

	/*
	 * When the parameter has been set to 1 via sysfs while the
	 * driver was running, or when bulk mode is enabled after IR init.
	 */
	if (dvb_usbv2_disable_rc_polling || d->rc.bulk_mode) {
		d->rc_polling_active = false;
		return;
	}

	ret = d->rc.query(d);
	if (ret < 0) {
		dev_err(&d->udev->dev, "%s: rc.query() failed=%d\n",
				KBUILD_MODNAME, ret);
		d->rc_polling_active = false;
		return; /* stop polling */
	}

	schedule_delayed_work(&d->rc_query_work,
			msecs_to_jiffies(d->rc.interval));
}

static int dvb_usbv2_remote_init(struct dvb_usb_device *d)
{
	int ret;
	struct rc_dev *dev;
	dev_dbg(&d->udev->dev, "%s:\n", __func__);

	if (dvb_usbv2_disable_rc_polling || !d->props->get_rc_config)
		return 0;

	d->rc.map_name = d->rc_map;
	ret = d->props->get_rc_config(d, &d->rc);
	if (ret < 0)
		goto err;

	/* disable rc when there is no keymap defined */
	if (!d->rc.map_name)
		return 0;

	dev = rc_allocate_device(d->rc.driver_type);
	if (!dev) {
		ret = -ENOMEM;
		goto err;
	}

	dev->dev.parent = &d->udev->dev;
	dev->device_name = d->name;
	usb_make_path(d->udev, d->rc_phys, sizeof(d->rc_phys));
	strlcat(d->rc_phys, "/ir0", sizeof(d->rc_phys));
	dev->input_phys = d->rc_phys;
	usb_to_input_id(d->udev, &dev->input_id);
	dev->driver_name = d->props->driver_name;
	dev->map_name = d->rc.map_name;
	dev->allowed_protocols = d->rc.allowed_protos;
	dev->change_protocol = d->rc.change_protocol;
	dev->timeout = d->rc.timeout;
	dev->priv = d;

	ret = rc_register_device(dev);
	if (ret < 0) {
		rc_free_device(dev);
		goto err;
	}

	d->rc_dev = dev;

	/* start polling if needed */
	if (d->rc.query && !d->rc.bulk_mode) {
		/* initialize a work queue for handling polling */
		INIT_DELAYED_WORK(&d->rc_query_work,
				dvb_usb_read_remote_control);
		dev_info(&d->udev->dev,
				"%s: schedule remote query interval to %d msecs\n",
				KBUILD_MODNAME, d->rc.interval);
		schedule_delayed_work(&d->rc_query_work,
				msecs_to_jiffies(d->rc.interval));
		d->rc_polling_active = true;
	}

	return 0;
err:
	dev_dbg(&d->udev->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int dvb_usbv2_remote_exit(struct dvb_usb_device *d)
{
	dev_dbg(&d->udev->dev, "%s:\n", __func__);

	if (d->rc_dev) {
		cancel_delayed_work_sync(&d->rc_query_work);
		rc_unregister_device(d->rc_dev);
		d->rc_dev = NULL;
	}

	return 0;
}
#else
	#define dvb_usbv2_remote_init(args...) 0
	#define dvb_usbv2_remote_exit(args...)
#endif

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

static int dvb_usbv2_adapter_stream_init(struct dvb_usb_adapter *adap)
{
	dev_dbg(&adap_to_d(adap)->udev->dev, "%s: adap=%d\n", __func__,
			adap->id);

	adap->stream.udev = adap_to_d(adap)->udev;
	adap->stream.user_priv = adap;
	adap->stream.complete = dvb_usb_data_complete;

	return usb_urb_initv2(&adap->stream, &adap->props->stream);
}

static int dvb_usbv2_adapter_stream_exit(struct dvb_usb_adapter *adap)
{
	dev_dbg(&adap_to_d(adap)->udev->dev, "%s: adap=%d\n", __func__,
			adap->id);

	return usb_urb_exitv2(&adap->stream);
}

static int dvb_usb_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_usb_adapter *adap = dvbdmxfeed->demux->priv;
	struct dvb_usb_device *d = adap_to_d(adap);
	int ret = 0;
	struct usb_data_stream_properties stream_props;
	dev_dbg(&d->udev->dev,
			"%s: adap=%d active_fe=%d feed_type=%d setting pid [%s]: %04x (%04d) at index %d\n",
			__func__, adap->id, adap->active_fe, dvbdmxfeed->type,
			adap->pid_filtering ? "yes" : "no", dvbdmxfeed->pid,
			dvbdmxfeed->pid, dvbdmxfeed->index);

	/* wait init is done */
	wait_on_bit(&adap->state_bits, ADAP_INIT, TASK_UNINTERRUPTIBLE);

	if (adap->active_fe == -1)
		return -EINVAL;

	/* skip feed setup if we are already feeding */
	if (adap->feed_count++ > 0)
		goto skip_feed_start;

	/* set 'streaming' status bit */
	set_bit(ADAP_STREAMING, &adap->state_bits);

	/* resolve input and output streaming parameters */
	if (d->props->get_stream_config) {
		memcpy(&stream_props, &adap->props->stream,
				sizeof(struct usb_data_stream_properties));
		ret = d->props->get_stream_config(adap->fe[adap->active_fe],
				&adap->ts_type, &stream_props);
		if (ret)
			dev_err(&d->udev->dev,
					"%s: get_stream_config() failed=%d\n",
					KBUILD_MODNAME, ret);
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

	/* submit USB streaming packets */
	usb_urb_submitv2(&adap->stream, &stream_props);

	/* enable HW PID filter */
	if (adap->pid_filtering && adap->props->pid_filter_ctrl) {
		ret = adap->props->pid_filter_ctrl(adap, 1);
		if (ret)
			dev_err(&d->udev->dev,
					"%s: pid_filter_ctrl() failed=%d\n",
					KBUILD_MODNAME, ret);
	}

	/* ask device to start streaming */
	if (d->props->streaming_ctrl) {
		ret = d->props->streaming_ctrl(adap->fe[adap->active_fe], 1);
		if (ret)
			dev_err(&d->udev->dev,
					"%s: streaming_ctrl() failed=%d\n",
					KBUILD_MODNAME, ret);
	}
skip_feed_start:

	/* add PID to device HW PID filter */
	if (adap->pid_filtering && adap->props->pid_filter) {
		ret = adap->props->pid_filter(adap, dvbdmxfeed->index,
				dvbdmxfeed->pid, 1);
		if (ret)
			dev_err(&d->udev->dev, "%s: pid_filter() failed=%d\n",
					KBUILD_MODNAME, ret);
	}

	if (ret)
		dev_dbg(&d->udev->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int dvb_usb_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_usb_adapter *adap = dvbdmxfeed->demux->priv;
	struct dvb_usb_device *d = adap_to_d(adap);
	int ret = 0;
	dev_dbg(&d->udev->dev,
			"%s: adap=%d active_fe=%d feed_type=%d setting pid [%s]: %04x (%04d) at index %d\n",
			__func__, adap->id, adap->active_fe, dvbdmxfeed->type,
			adap->pid_filtering ? "yes" : "no", dvbdmxfeed->pid,
			dvbdmxfeed->pid, dvbdmxfeed->index);

	if (adap->active_fe == -1)
		return -EINVAL;

	/* remove PID from device HW PID filter */
	if (adap->pid_filtering && adap->props->pid_filter) {
		ret = adap->props->pid_filter(adap, dvbdmxfeed->index,
				dvbdmxfeed->pid, 0);
		if (ret)
			dev_err(&d->udev->dev, "%s: pid_filter() failed=%d\n",
					KBUILD_MODNAME, ret);
	}

	/* we cannot stop streaming until last PID is removed */
	if (--adap->feed_count > 0)
		goto skip_feed_stop;

	/* ask device to stop streaming */
	if (d->props->streaming_ctrl) {
		ret = d->props->streaming_ctrl(adap->fe[adap->active_fe], 0);
		if (ret)
			dev_err(&d->udev->dev,
					"%s: streaming_ctrl() failed=%d\n",
					KBUILD_MODNAME, ret);
	}

	/* disable HW PID filter */
	if (adap->pid_filtering && adap->props->pid_filter_ctrl) {
		ret = adap->props->pid_filter_ctrl(adap, 0);
		if (ret)
			dev_err(&d->udev->dev,
					"%s: pid_filter_ctrl() failed=%d\n",
					KBUILD_MODNAME, ret);
	}

	/* kill USB streaming packets */
	usb_urb_killv2(&adap->stream);

	/* clear 'streaming' status bit */
	clear_bit(ADAP_STREAMING, &adap->state_bits);
	smp_mb__after_atomic();
	wake_up_bit(&adap->state_bits, ADAP_STREAMING);
skip_feed_stop:

	if (ret)
		dev_dbg(&d->udev->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int dvb_usbv2_media_device_init(struct dvb_usb_adapter *adap)
{
#ifdef CONFIG_MEDIA_CONTROLLER_DVB
	struct media_device *mdev;
	struct dvb_usb_device *d = adap_to_d(adap);
	struct usb_device *udev = d->udev;

	mdev = kzalloc(sizeof(*mdev), GFP_KERNEL);
	if (!mdev)
		return -ENOMEM;

	media_device_usb_init(mdev, udev, d->name);

	dvb_register_media_controller(&adap->dvb_adap, mdev);

	dev_info(&d->udev->dev, "media controller created\n");
#endif
	return 0;
}

static int dvb_usbv2_media_device_register(struct dvb_usb_adapter *adap)
{
#ifdef CONFIG_MEDIA_CONTROLLER_DVB
	return media_device_register(adap->dvb_adap.mdev);
#else
	return 0;
#endif
}

static void dvb_usbv2_media_device_unregister(struct dvb_usb_adapter *adap)
{
#ifdef CONFIG_MEDIA_CONTROLLER_DVB

	if (!adap->dvb_adap.mdev)
		return;

	media_device_unregister(adap->dvb_adap.mdev);
	media_device_cleanup(adap->dvb_adap.mdev);
	kfree(adap->dvb_adap.mdev);
	adap->dvb_adap.mdev = NULL;

#endif
}

static int dvb_usbv2_adapter_dvb_init(struct dvb_usb_adapter *adap)
{
	int ret;
	struct dvb_usb_device *d = adap_to_d(adap);

	dev_dbg(&d->udev->dev, "%s: adap=%d\n", __func__, adap->id);

	ret = dvb_register_adapter(&adap->dvb_adap, d->name, d->props->owner,
			&d->udev->dev, d->props->adapter_nr);
	if (ret < 0) {
		dev_dbg(&d->udev->dev, "%s: dvb_register_adapter() failed=%d\n",
				__func__, ret);
		goto err_dvb_register_adapter;
	}

	adap->dvb_adap.priv = adap;

	ret = dvb_usbv2_media_device_init(adap);
	if (ret < 0) {
		dev_dbg(&d->udev->dev, "%s: dvb_usbv2_media_device_init() failed=%d\n",
				__func__, ret);
		goto err_dvb_register_mc;
	}

	if (d->props->read_mac_address) {
		ret = d->props->read_mac_address(adap,
				adap->dvb_adap.proposed_mac);
		if (ret < 0)
			goto err_dvb_dmx_init;

		dev_info(&d->udev->dev, "%s: MAC address: %pM\n",
				KBUILD_MODNAME, adap->dvb_adap.proposed_mac);
	}

	adap->demux.dmx.capabilities = DMX_TS_FILTERING | DMX_SECTION_FILTERING;
	adap->demux.priv             = adap;
	adap->demux.filternum        = 0;
	adap->demux.filternum        = adap->max_feed_count;
	adap->demux.feednum          = adap->demux.filternum;
	adap->demux.start_feed       = dvb_usb_start_feed;
	adap->demux.stop_feed        = dvb_usb_stop_feed;
	adap->demux.write_to_decoder = NULL;
	ret = dvb_dmx_init(&adap->demux);
	if (ret < 0) {
		dev_err(&d->udev->dev, "%s: dvb_dmx_init() failed=%d\n",
				KBUILD_MODNAME, ret);
		goto err_dvb_dmx_init;
	}

	adap->dmxdev.filternum       = adap->demux.filternum;
	adap->dmxdev.demux           = &adap->demux.dmx;
	adap->dmxdev.capabilities    = 0;
	ret = dvb_dmxdev_init(&adap->dmxdev, &adap->dvb_adap);
	if (ret < 0) {
		dev_err(&d->udev->dev, "%s: dvb_dmxdev_init() failed=%d\n",
				KBUILD_MODNAME, ret);
		goto err_dvb_dmxdev_init;
	}

	ret = dvb_net_init(&adap->dvb_adap, &adap->dvb_net, &adap->demux.dmx);
	if (ret < 0) {
		dev_err(&d->udev->dev, "%s: dvb_net_init() failed=%d\n",
				KBUILD_MODNAME, ret);
		goto err_dvb_net_init;
	}

	return 0;
err_dvb_net_init:
	dvb_dmxdev_release(&adap->dmxdev);
err_dvb_dmxdev_init:
	dvb_dmx_release(&adap->demux);
err_dvb_dmx_init:
	dvb_usbv2_media_device_unregister(adap);
err_dvb_register_mc:
	dvb_unregister_adapter(&adap->dvb_adap);
err_dvb_register_adapter:
	adap->dvb_adap.priv = NULL;
	return ret;
}

static int dvb_usbv2_adapter_dvb_exit(struct dvb_usb_adapter *adap)
{
	dev_dbg(&adap_to_d(adap)->udev->dev, "%s: adap=%d\n", __func__,
			adap->id);

	if (adap->dvb_adap.priv) {
		dvb_net_release(&adap->dvb_net);
		adap->demux.dmx.close(&adap->demux.dmx);
		dvb_dmxdev_release(&adap->dmxdev);
		dvb_dmx_release(&adap->demux);
		dvb_unregister_adapter(&adap->dvb_adap);
	}

	return 0;
}

static int dvb_usbv2_device_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	int ret;

	if (onoff)
		d->powered++;
	else
		d->powered--;

	if (d->powered == 0 || (onoff && d->powered == 1)) {
		/* when switching from 1 to 0 or from 0 to 1 */
		dev_dbg(&d->udev->dev, "%s: power=%d\n", __func__, onoff);
		if (d->props->power_ctrl) {
			ret = d->props->power_ctrl(d, onoff);
			if (ret < 0)
				goto err;
		}
	}

	return 0;
err:
	dev_dbg(&d->udev->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int dvb_usb_fe_init(struct dvb_frontend *fe)
{
	int ret;
	struct dvb_usb_adapter *adap = fe->dvb->priv;
	struct dvb_usb_device *d = adap_to_d(adap);
	dev_dbg(&d->udev->dev, "%s: adap=%d fe=%d\n", __func__, adap->id,
			fe->id);

	if (!adap->suspend_resume_active) {
		adap->active_fe = fe->id;
		set_bit(ADAP_INIT, &adap->state_bits);
	}

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
err:
	if (!adap->suspend_resume_active) {
		clear_bit(ADAP_INIT, &adap->state_bits);
		smp_mb__after_atomic();
		wake_up_bit(&adap->state_bits, ADAP_INIT);
	}

	dev_dbg(&d->udev->dev, "%s: ret=%d\n", __func__, ret);
	return ret;
}

static int dvb_usb_fe_sleep(struct dvb_frontend *fe)
{
	int ret;
	struct dvb_usb_adapter *adap = fe->dvb->priv;
	struct dvb_usb_device *d = adap_to_d(adap);
	dev_dbg(&d->udev->dev, "%s: adap=%d fe=%d\n", __func__, adap->id,
			fe->id);

	if (!adap->suspend_resume_active) {
		set_bit(ADAP_SLEEP, &adap->state_bits);
		wait_on_bit(&adap->state_bits, ADAP_STREAMING,
				TASK_UNINTERRUPTIBLE);
	}

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

err:
	if (!adap->suspend_resume_active) {
		adap->active_fe = -1;
		clear_bit(ADAP_SLEEP, &adap->state_bits);
		smp_mb__after_atomic();
		wake_up_bit(&adap->state_bits, ADAP_SLEEP);
	}

	dev_dbg(&d->udev->dev, "%s: ret=%d\n", __func__, ret);
	return ret;
}

static int dvb_usbv2_adapter_frontend_init(struct dvb_usb_adapter *adap)
{
	int ret, i, count_registered = 0;
	struct dvb_usb_device *d = adap_to_d(adap);
	dev_dbg(&d->udev->dev, "%s: adap=%d\n", __func__, adap->id);

	memset(adap->fe, 0, sizeof(adap->fe));
	adap->active_fe = -1;

	if (d->props->frontend_attach) {
		ret = d->props->frontend_attach(adap);
		if (ret < 0) {
			dev_dbg(&d->udev->dev,
					"%s: frontend_attach() failed=%d\n",
					__func__, ret);
			goto err_dvb_frontend_detach;
		}
	} else {
		dev_dbg(&d->udev->dev, "%s: frontend_attach() do not exists\n",
				__func__);
		ret = 0;
		goto err;
	}

	for (i = 0; i < MAX_NO_OF_FE_PER_ADAP && adap->fe[i]; i++) {
		adap->fe[i]->id = i;
		/* re-assign sleep and wakeup functions */
		adap->fe_init[i] = adap->fe[i]->ops.init;
		adap->fe[i]->ops.init = dvb_usb_fe_init;
		adap->fe_sleep[i] = adap->fe[i]->ops.sleep;
		adap->fe[i]->ops.sleep = dvb_usb_fe_sleep;

		ret = dvb_register_frontend(&adap->dvb_adap, adap->fe[i]);
		if (ret < 0) {
			dev_err(&d->udev->dev,
					"%s: frontend%d registration failed\n",
					KBUILD_MODNAME, i);
			goto err_dvb_unregister_frontend;
		}

		count_registered++;
	}

	if (d->props->tuner_attach) {
		ret = d->props->tuner_attach(adap);
		if (ret < 0) {
			dev_dbg(&d->udev->dev, "%s: tuner_attach() failed=%d\n",
					__func__, ret);
			goto err_dvb_unregister_frontend;
		}
	}

	ret = dvb_create_media_graph(&adap->dvb_adap, true);
	if (ret < 0)
		goto err_dvb_unregister_frontend;

	ret = dvb_usbv2_media_device_register(adap);

	return ret;

err_dvb_unregister_frontend:
	for (i = count_registered - 1; i >= 0; i--)
		dvb_unregister_frontend(adap->fe[i]);

err_dvb_frontend_detach:
	for (i = MAX_NO_OF_FE_PER_ADAP - 1; i >= 0; i--) {
		if (adap->fe[i]) {
			dvb_frontend_detach(adap->fe[i]);
			adap->fe[i] = NULL;
		}
	}

err:
	dev_dbg(&d->udev->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int dvb_usbv2_adapter_frontend_exit(struct dvb_usb_adapter *adap)
{
	int ret, i;
	struct dvb_usb_device *d = adap_to_d(adap);

	dev_dbg(&d->udev->dev, "%s: adap=%d\n", __func__, adap->id);

	for (i = MAX_NO_OF_FE_PER_ADAP - 1; i >= 0; i--) {
		if (adap->fe[i]) {
			dvb_unregister_frontend(adap->fe[i]);
			dvb_frontend_detach(adap->fe[i]);
		}
	}

	if (d->props->tuner_detach) {
		ret = d->props->tuner_detach(adap);
		if (ret < 0) {
			dev_dbg(&d->udev->dev, "%s: tuner_detach() failed=%d\n",
					__func__, ret);
		}
	}

	if (d->props->frontend_detach) {
		ret = d->props->frontend_detach(adap);
		if (ret < 0) {
			dev_dbg(&d->udev->dev,
					"%s: frontend_detach() failed=%d\n",
					__func__, ret);
		}
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
		adap->id = i;
		adap->props = &d->props->adapter[i];

		/* speed - when running at FULL speed we need a HW PID filter */
		if (d->udev->speed == USB_SPEED_FULL &&
				!(adap->props->caps & DVB_USB_ADAP_HAS_PID_FILTER)) {
			dev_err(&d->udev->dev,
					"%s: this USB2.0 device cannot be run on a USB1.1 port (it lacks a hardware PID filter)\n",
					KBUILD_MODNAME);
			ret = -ENODEV;
			goto err;
		} else if ((d->udev->speed == USB_SPEED_FULL &&
				adap->props->caps & DVB_USB_ADAP_HAS_PID_FILTER) ||
				(adap->props->caps & DVB_USB_ADAP_NEED_PID_FILTERING)) {
			dev_info(&d->udev->dev,
					"%s: will use the device's hardware PID filter (table count: %d)\n",
					KBUILD_MODNAME,
					adap->props->pid_filter_count);
			adap->pid_filtering  = 1;
			adap->max_feed_count = adap->props->pid_filter_count;
		} else {
			dev_info(&d->udev->dev,
					"%s: will pass the complete MPEG2 transport stream to the software demuxer\n",
					KBUILD_MODNAME);
			adap->pid_filtering  = 0;
			adap->max_feed_count = 255;
		}

		if (!adap->pid_filtering && dvb_usb_force_pid_filter_usage &&
				adap->props->caps & DVB_USB_ADAP_HAS_PID_FILTER) {
			dev_info(&d->udev->dev,
					"%s: PID filter enabled by module option\n",
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
	}

	return 0;
err:
	dev_dbg(&d->udev->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int dvb_usbv2_adapter_exit(struct dvb_usb_device *d)
{
	int i;
	dev_dbg(&d->udev->dev, "%s:\n", __func__);

	for (i = MAX_NO_OF_ADAPTER_PER_DEVICE - 1; i >= 0; i--) {
		if (d->adapter[i].props) {
			dvb_usbv2_adapter_dvb_exit(&d->adapter[i]);
			dvb_usbv2_adapter_stream_exit(&d->adapter[i]);
			dvb_usbv2_adapter_frontend_exit(&d->adapter[i]);
			dvb_usbv2_media_device_unregister(&d->adapter[i]);
		}
	}

	return 0;
}

/* general initialization functions */
static int dvb_usbv2_exit(struct dvb_usb_device *d)
{
	dev_dbg(&d->udev->dev, "%s:\n", __func__);

	dvb_usbv2_remote_exit(d);
	dvb_usbv2_adapter_exit(d);
	dvb_usbv2_i2c_exit(d);

	return 0;
}

static int dvb_usbv2_init(struct dvb_usb_device *d)
{
	int ret;
	dev_dbg(&d->udev->dev, "%s:\n", __func__);

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
	dev_dbg(&d->udev->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

int dvb_usbv2_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	int ret;
	struct dvb_usb_device *d;
	struct usb_device *udev = interface_to_usbdev(intf);
	struct dvb_usb_driver_info *driver_info =
			(struct dvb_usb_driver_info *) id->driver_info;

	dev_dbg(&udev->dev, "%s: bInterfaceNumber=%d\n", __func__,
			intf->cur_altsetting->desc.bInterfaceNumber);

	if (!id->driver_info) {
		dev_err(&udev->dev, "%s: driver_info failed\n", KBUILD_MODNAME);
		ret = -ENODEV;
		goto err;
	}

	d = kzalloc(sizeof(struct dvb_usb_device), GFP_KERNEL);
	if (!d) {
		dev_err(&udev->dev, "%s: kzalloc() failed\n", KBUILD_MODNAME);
		ret = -ENOMEM;
		goto err;
	}

	d->intf = intf;
	d->name = driver_info->name;
	d->rc_map = driver_info->rc_map;
	d->udev = udev;
	d->props = driver_info->props;

	if (intf->cur_altsetting->desc.bInterfaceNumber !=
			d->props->bInterfaceNumber) {
		ret = -ENODEV;
		goto err_kfree_d;
	}

	mutex_init(&d->usb_mutex);
	mutex_init(&d->i2c_mutex);

	if (d->props->size_of_priv) {
		d->priv = kzalloc(d->props->size_of_priv, GFP_KERNEL);
		if (!d->priv) {
			dev_err(&d->udev->dev, "%s: kzalloc() failed\n",
					KBUILD_MODNAME);
			ret = -ENOMEM;
			goto err_kfree_d;
		}
	}

	if (d->props->probe) {
		ret = d->props->probe(d);
		if (ret)
			goto err_kfree_priv;
	}

	if (d->props->identify_state) {
		const char *name = NULL;
		ret = d->props->identify_state(d, &name);
		if (ret == COLD) {
			dev_info(&d->udev->dev,
					"%s: found a '%s' in cold state\n",
					KBUILD_MODNAME, d->name);

			if (!name)
				name = d->props->firmware;

			ret = dvb_usbv2_download_firmware(d, name);
			if (ret == 0) {
				/* device is warm, continue initialization */
				;
			} else if (ret == RECONNECTS_USB) {
				/*
				 * USB core will call disconnect() and then
				 * probe() as device reconnects itself from the
				 * USB bus. disconnect() will release all driver
				 * resources and probe() is called for 'new'
				 * device. As 'new' device is warm we should
				 * never go here again.
				 */
				goto exit;
			} else {
				goto err_free_all;
			}
		} else if (ret != WARM) {
			goto err_free_all;
		}
	}

	dev_info(&d->udev->dev, "%s: found a '%s' in warm state\n",
			KBUILD_MODNAME, d->name);

	ret = dvb_usbv2_init(d);
	if (ret < 0)
		goto err_free_all;

	dev_info(&d->udev->dev,
			"%s: '%s' successfully initialized and connected\n",
			KBUILD_MODNAME, d->name);
exit:
	usb_set_intfdata(intf, d);

	return 0;
err_free_all:
	dvb_usbv2_exit(d);
	if (d->props->disconnect)
		d->props->disconnect(d);
err_kfree_priv:
	kfree(d->priv);
err_kfree_d:
	kfree(d);
err:
	dev_dbg(&udev->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}
EXPORT_SYMBOL(dvb_usbv2_probe);

void dvb_usbv2_disconnect(struct usb_interface *intf)
{
	struct dvb_usb_device *d = usb_get_intfdata(intf);
	const char *devname = kstrdup(dev_name(&d->udev->dev), GFP_KERNEL);
	const char *drvname = d->name;

	dev_dbg(&d->udev->dev, "%s: bInterfaceNumber=%d\n", __func__,
			intf->cur_altsetting->desc.bInterfaceNumber);

	if (d->props->exit)
		d->props->exit(d);

	dvb_usbv2_exit(d);

	if (d->props->disconnect)
		d->props->disconnect(d);

	kfree(d->priv);
	kfree(d);

	pr_info("%s: '%s:%s' successfully deinitialized and disconnected\n",
		KBUILD_MODNAME, drvname, devname);
	kfree(devname);
}
EXPORT_SYMBOL(dvb_usbv2_disconnect);

int dvb_usbv2_suspend(struct usb_interface *intf, pm_message_t msg)
{
	struct dvb_usb_device *d = usb_get_intfdata(intf);
	int ret = 0, i, active_fe;
	struct dvb_frontend *fe;
	dev_dbg(&d->udev->dev, "%s:\n", __func__);

	/* stop remote controller poll */
	if (d->rc_polling_active)
		cancel_delayed_work_sync(&d->rc_query_work);

	for (i = MAX_NO_OF_ADAPTER_PER_DEVICE - 1; i >= 0; i--) {
		active_fe = d->adapter[i].active_fe;
		if (d->adapter[i].dvb_adap.priv && active_fe != -1) {
			fe = d->adapter[i].fe[active_fe];
			d->adapter[i].suspend_resume_active = true;

			if (d->props->streaming_ctrl)
				d->props->streaming_ctrl(fe, 0);

			/* stop usb streaming */
			usb_urb_killv2(&d->adapter[i].stream);

			ret = dvb_frontend_suspend(fe);
		}
	}

	return ret;
}
EXPORT_SYMBOL(dvb_usbv2_suspend);

static int dvb_usbv2_resume_common(struct dvb_usb_device *d)
{
	int ret = 0, i, active_fe;
	struct dvb_frontend *fe;
	dev_dbg(&d->udev->dev, "%s:\n", __func__);

	for (i = 0; i < MAX_NO_OF_ADAPTER_PER_DEVICE; i++) {
		active_fe = d->adapter[i].active_fe;
		if (d->adapter[i].dvb_adap.priv && active_fe != -1) {
			fe = d->adapter[i].fe[active_fe];

			ret = dvb_frontend_resume(fe);

			/* resume usb streaming */
			usb_urb_submitv2(&d->adapter[i].stream, NULL);

			if (d->props->streaming_ctrl)
				d->props->streaming_ctrl(fe, 1);

			d->adapter[i].suspend_resume_active = false;
		}
	}

	/* start remote controller poll */
	if (d->rc_polling_active)
		schedule_delayed_work(&d->rc_query_work,
				msecs_to_jiffies(d->rc.interval));

	return ret;
}

int dvb_usbv2_resume(struct usb_interface *intf)
{
	struct dvb_usb_device *d = usb_get_intfdata(intf);
	dev_dbg(&d->udev->dev, "%s:\n", __func__);

	return dvb_usbv2_resume_common(d);
}
EXPORT_SYMBOL(dvb_usbv2_resume);

int dvb_usbv2_reset_resume(struct usb_interface *intf)
{
	struct dvb_usb_device *d = usb_get_intfdata(intf);
	int ret;
	dev_dbg(&d->udev->dev, "%s:\n", __func__);

	dvb_usbv2_device_power_ctrl(d, 1);

	if (d->props->init)
		d->props->init(d);

	ret = dvb_usbv2_resume_common(d);

	dvb_usbv2_device_power_ctrl(d, 0);

	return ret;
}
EXPORT_SYMBOL(dvb_usbv2_reset_resume);

MODULE_VERSION("2.0");
MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@posteo.de>");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("DVB USB common");
MODULE_LICENSE("GPL");
