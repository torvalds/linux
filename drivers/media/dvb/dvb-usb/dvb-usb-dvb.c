/* dvb-usb-dvb.c is part of the DVB USB library.
 *
 * Copyright (C) 2004-6 Patrick Boettcher (patrick.boettcher@desy.de)
 * see dvb-usb-init.c for copyright information.
 *
 * This file contains functions for initializing and handling the
 * linux-dvb API.
 */
#include "dvb-usb-common.h"

/* does the complete input transfer handling */
static int dvb_usb_ctrl_feed(struct dvb_demux_feed *dvbdmxfeed, int onoff)
{
	struct dvb_usb_adapter *adap = dvbdmxfeed->demux->priv;
	int newfeedcount,ret;

	if (adap == NULL)
		return -ENODEV;

	newfeedcount = adap->feedcount + (onoff ? 1 : -1);

	/* stop feed before setting a new pid if there will be no pid anymore */
	if (newfeedcount == 0) {
		deb_ts("stop feeding\n");
		usb_urb_kill(&adap->stream);

		if (adap->props.streaming_ctrl != NULL)
			if ((ret = adap->props.streaming_ctrl(adap,0)))
				err("error while stopping stream.");
	}

	adap->feedcount = newfeedcount;

	/* activate the pid on the device specific pid_filter */
	deb_ts("setting pid (%s): %5d %04x at index %d '%s'\n",adap->pid_filtering ?
		"yes" : "no", dvbdmxfeed->pid,dvbdmxfeed->pid,dvbdmxfeed->index,onoff ?
		"on" : "off");
	if (adap->props.caps & DVB_USB_ADAP_HAS_PID_FILTER &&
		adap->pid_filtering &&
		adap->props.pid_filter != NULL)
		adap->props.pid_filter(adap, dvbdmxfeed->index, dvbdmxfeed->pid,onoff);

	/* start the feed if this was the first feed and there is still a feed
	 * for reception.
	 */
	if (adap->feedcount == onoff && adap->feedcount > 0) {
		deb_ts("submitting all URBs\n");
		usb_urb_submit(&adap->stream);

		deb_ts("controlling pid parser\n");
		if (adap->props.caps & DVB_USB_ADAP_HAS_PID_FILTER &&
			adap->props.caps & DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF &&
			adap->props.pid_filter_ctrl != NULL)
			if (adap->props.pid_filter_ctrl(adap,adap->pid_filtering) < 0)
				err("could not handle pid_parser");

		deb_ts("start feeding\n");
		if (adap->props.streaming_ctrl != NULL)
			if (adap->props.streaming_ctrl(adap,1)) {
				err("error while enabling fifo.");
				return -ENODEV;
			}

	}
	return 0;
}

static int dvb_usb_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	deb_ts("start pid: 0x%04x, feedtype: %d\n", dvbdmxfeed->pid,dvbdmxfeed->type);
	return dvb_usb_ctrl_feed(dvbdmxfeed,1);
}

static int dvb_usb_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	deb_ts("stop pid: 0x%04x, feedtype: %d\n", dvbdmxfeed->pid, dvbdmxfeed->type);
	return dvb_usb_ctrl_feed(dvbdmxfeed,0);
}

int dvb_usb_adapter_dvb_init(struct dvb_usb_adapter *adap, short *adapter_nums)
{
	int ret = dvb_register_adapter(&adap->dvb_adap, adap->dev->desc->name,
				       adap->dev->owner, &adap->dev->udev->dev,
				       adapter_nums);

	if (ret < 0) {
		deb_info("dvb_register_adapter failed: error %d", ret);
		goto err;
	}
	adap->dvb_adap.priv = adap;

	if (adap->dev->props.read_mac_address) {
		if (adap->dev->props.read_mac_address(adap->dev,adap->dvb_adap.proposed_mac) == 0)
			info("MAC address: %02x:%02x:%02x:%02x:%02x:%02x",adap->dvb_adap.proposed_mac[0],
					adap->dvb_adap.proposed_mac[1], adap->dvb_adap.proposed_mac[2],
					adap->dvb_adap.proposed_mac[3], adap->dvb_adap.proposed_mac[4],
					adap->dvb_adap.proposed_mac[5]);
		else
			err("MAC address reading failed.");
	}


	adap->demux.dmx.capabilities = DMX_TS_FILTERING | DMX_SECTION_FILTERING;
	adap->demux.priv             = adap;

	adap->demux.feednum          = adap->demux.filternum = adap->max_feed_count;
	adap->demux.start_feed       = dvb_usb_start_feed;
	adap->demux.stop_feed        = dvb_usb_stop_feed;
	adap->demux.write_to_decoder = NULL;
	if ((ret = dvb_dmx_init(&adap->demux)) < 0) {
		err("dvb_dmx_init failed: error %d",ret);
		goto err_dmx;
	}

	adap->dmxdev.filternum       = adap->demux.filternum;
	adap->dmxdev.demux           = &adap->demux.dmx;
	adap->dmxdev.capabilities    = 0;
	if ((ret = dvb_dmxdev_init(&adap->dmxdev, &adap->dvb_adap)) < 0) {
		err("dvb_dmxdev_init failed: error %d",ret);
		goto err_dmx_dev;
	}

	dvb_net_init(&adap->dvb_adap, &adap->dvb_net, &adap->demux.dmx);

	adap->state |= DVB_USB_ADAP_STATE_DVB;
	return 0;

err_dmx_dev:
	dvb_dmx_release(&adap->demux);
err_dmx:
	dvb_unregister_adapter(&adap->dvb_adap);
err:
	return ret;
}

int dvb_usb_adapter_dvb_exit(struct dvb_usb_adapter *adap)
{
	if (adap->state & DVB_USB_ADAP_STATE_DVB) {
		deb_info("unregistering DVB part\n");
		dvb_net_release(&adap->dvb_net);
		adap->demux.dmx.close(&adap->demux.dmx);
		dvb_dmxdev_release(&adap->dmxdev);
		dvb_dmx_release(&adap->demux);
		dvb_unregister_adapter(&adap->dvb_adap);
		adap->state &= ~DVB_USB_ADAP_STATE_DVB;
	}
	return 0;
}

static int dvb_usb_fe_wakeup(struct dvb_frontend *fe)
{
	struct dvb_usb_adapter *adap = fe->dvb->priv;

	dvb_usb_device_power_ctrl(adap->dev, 1);

	if (adap->fe_init)
		adap->fe_init(fe);

	return 0;
}

static int dvb_usb_fe_sleep(struct dvb_frontend *fe)
{
	struct dvb_usb_adapter *adap = fe->dvb->priv;

	if (adap->fe_sleep)
		adap->fe_sleep(fe);

	return dvb_usb_device_power_ctrl(adap->dev, 0);
}

int dvb_usb_adapter_frontend_init(struct dvb_usb_adapter *adap)
{
	if (adap->props.frontend_attach == NULL) {
		err("strange: '%s' #%d doesn't want to attach a frontend.",adap->dev->desc->name, adap->id);
		return 0;
	}

	/* re-assign sleep and wakeup functions */
	if (adap->props.frontend_attach(adap) == 0 && adap->fe != NULL) {
		adap->fe_init  = adap->fe->ops.init;  adap->fe->ops.init  = dvb_usb_fe_wakeup;
		adap->fe_sleep = adap->fe->ops.sleep; adap->fe->ops.sleep = dvb_usb_fe_sleep;

		if (dvb_register_frontend(&adap->dvb_adap, adap->fe)) {
			err("Frontend registration failed.");
			dvb_frontend_detach(adap->fe);
			adap->fe = NULL;
			return -ENODEV;
		}

		/* only attach the tuner if the demod is there */
		if (adap->props.tuner_attach != NULL)
			adap->props.tuner_attach(adap);
	} else
		err("no frontend was attached by '%s'",adap->dev->desc->name);

	return 0;
}

int dvb_usb_adapter_frontend_exit(struct dvb_usb_adapter *adap)
{
	if (adap->fe != NULL) {
		dvb_unregister_frontend(adap->fe);
		dvb_frontend_detach(adap->fe);
	}
	return 0;
}
