/* dvb-usb-dvb.c is part of the DVB USB library.
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@desy.de)
 * see dvb-usb-init.c for copyright information.
 *
 * This file contains functions for initializing and handling the
 * linux-dvb API.
 */
#include "dvb-usb-common.h"

static int dvb_usb_ctrl_feed(struct dvb_demux_feed *dvbdmxfeed, int onoff)
{
	struct dvb_usb_device *d = dvbdmxfeed->demux->priv;
	int newfeedcount,ret;

	if (d == NULL)
		return -ENODEV;

	newfeedcount = d->feedcount + (onoff ? 1 : -1);

	/*
	 * stop feed before setting a new pid if there will be no pid anymore
	 */
	if (newfeedcount == 0) {
		deb_ts("stop feeding\n");
		dvb_usb_urb_kill(d);

		if (d->props.streaming_ctrl != NULL)
			if ((ret = d->props.streaming_ctrl(d,0)))
				err("error while stopping stream.");

	}

	d->feedcount = newfeedcount;

	/* activate the pid on the device specific pid_filter */
	deb_ts("setting pid: %5d %04x at index %d '%s'\n",dvbdmxfeed->pid,dvbdmxfeed->pid,dvbdmxfeed->index,onoff ? "on" : "off");
	if (d->props.caps & DVB_USB_HAS_PID_FILTER &&
		d->pid_filtering &&
		d->props.pid_filter != NULL)
		d->props.pid_filter(d,dvbdmxfeed->index,dvbdmxfeed->pid,onoff);

	/* start the feed if this was the first feed and there is still a feed
	 * for reception.
	 */
	if (d->feedcount == onoff && d->feedcount > 0) {
		deb_ts("submitting all URBs\n");
		dvb_usb_urb_submit(d);

		deb_ts("controlling pid parser\n");
		if (d->props.caps & DVB_USB_HAS_PID_FILTER &&
			d->props.caps & DVB_USB_PID_FILTER_CAN_BE_TURNED_OFF &&
			d->props.pid_filter_ctrl != NULL)
			if (d->props.pid_filter_ctrl(d,d->pid_filtering) < 0)
				err("could not handle pid_parser");

		deb_ts("start feeding\n");
		if (d->props.streaming_ctrl != NULL)
			if (d->props.streaming_ctrl(d,1)) {
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

int dvb_usb_dvb_init(struct dvb_usb_device *d)
{
	int ret;

	if ((ret = dvb_register_adapter(&d->dvb_adap, d->desc->name,
			d->owner)) < 0) {
		deb_info("dvb_register_adapter failed: error %d", ret);
		goto err;
	}
	d->dvb_adap.priv = d;

	if (d->props.read_mac_address) {
		if (d->props.read_mac_address(d,d->dvb_adap.proposed_mac) == 0)
			info("MAC address: %02x:%02x:%02x:%02x:%02x:%02x",d->dvb_adap.proposed_mac[0],
					d->dvb_adap.proposed_mac[1],d->dvb_adap.proposed_mac[2],
					d->dvb_adap.proposed_mac[3],d->dvb_adap.proposed_mac[4],
					d->dvb_adap.proposed_mac[5]);
		else
			err("MAC address reading failed.");
	}


	d->demux.dmx.capabilities = DMX_TS_FILTERING | DMX_SECTION_FILTERING;
	d->demux.priv = d;

	d->demux.feednum = d->demux.filternum = d->max_feed_count;
	d->demux.start_feed = dvb_usb_start_feed;
	d->demux.stop_feed  = dvb_usb_stop_feed;
	d->demux.write_to_decoder = NULL;
	if ((ret = dvb_dmx_init(&d->demux)) < 0) {
		err("dvb_dmx_init failed: error %d",ret);
		goto err_dmx;
	}

	d->dmxdev.filternum = d->demux.filternum;
	d->dmxdev.demux = &d->demux.dmx;
	d->dmxdev.capabilities = 0;
	if ((ret = dvb_dmxdev_init(&d->dmxdev, &d->dvb_adap)) < 0) {
		err("dvb_dmxdev_init failed: error %d",ret);
		goto err_dmx_dev;
	}

	dvb_net_init(&d->dvb_adap, &d->dvb_net, &d->demux.dmx);

	goto success;
err_dmx_dev:
	dvb_dmx_release(&d->demux);
err_dmx:
	dvb_unregister_adapter(&d->dvb_adap);
err:
	return ret;
success:
	d->state |= DVB_USB_STATE_DVB;
	return 0;
}

int dvb_usb_dvb_exit(struct dvb_usb_device *d)
{
	if (d->state & DVB_USB_STATE_DVB) {
		deb_info("unregistering DVB part\n");
		dvb_net_release(&d->dvb_net);
		d->demux.dmx.close(&d->demux.dmx);
		dvb_dmxdev_release(&d->dmxdev);
		dvb_dmx_release(&d->demux);
		dvb_unregister_adapter(&d->dvb_adap);
		d->state &= ~DVB_USB_STATE_DVB;
	}
	return 0;
}

static int dvb_usb_fe_wakeup(struct dvb_frontend *fe)
{
	struct dvb_usb_device *d = fe->dvb->priv;

	if (d->props.power_ctrl)
		d->props.power_ctrl(d,1);

	if (d->fe_init)
		d->fe_init(fe);

	return 0;
}

static int dvb_usb_fe_sleep(struct dvb_frontend *fe)
{
	struct dvb_usb_device *d = fe->dvb->priv;

	if (d->fe_sleep)
		d->fe_sleep(fe);

	if (d->props.power_ctrl)
		d->props.power_ctrl(d,0);

	return 0;
}

int dvb_usb_fe_init(struct dvb_usb_device* d)
{
	if (d->props.frontend_attach == NULL) {
		err("strange '%s' doesn't want to attach a frontend.",d->desc->name);
		return 0;
	}

	d->props.frontend_attach(d);

	/* re-assign sleep and wakeup functions */
	if (d->fe != NULL) {
		d->fe_init = d->fe->ops->init;   d->fe->ops->init  = dvb_usb_fe_wakeup;
		d->fe_sleep = d->fe->ops->sleep; d->fe->ops->sleep = dvb_usb_fe_sleep;

		if (dvb_register_frontend(&d->dvb_adap, d->fe)) {
			err("Frontend registration failed.");
			if (d->fe->ops->release)
				d->fe->ops->release(d->fe);
			d->fe = NULL;
			return -ENODEV;
		}
	} else
		err("no frontend was attached by '%s'",d->desc->name);

	if (d->props.tuner_attach != NULL)
		d->props.tuner_attach(d);

	return 0;
}

int dvb_usb_fe_exit(struct dvb_usb_device *d)
{
	if (d->fe != NULL)
		dvb_unregister_frontend(d->fe);
	return 0;
}
