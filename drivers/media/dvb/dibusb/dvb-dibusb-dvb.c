/*
 * dvb-dibusb-dvb.c is part of the driver for mobile USB Budget DVB-T devices 
 * based on reference design made by DiBcom (http://www.dibcom.fr/)
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@desy.de)
 *
 * see dvb-dibusb-core.c for more copyright details.
 *
 * This file contains functions for initializing and handling the 
 * linux-dvb API.
 */
#include "dvb-dibusb.h"

#include <linux/usb.h>
#include <linux/version.h>

static u32 urb_compl_count;

/*
 * MPEG2 TS DVB stuff 
 */
void dibusb_urb_complete(struct urb *urb, struct pt_regs *ptregs)
{
	struct usb_dibusb *dib = urb->context;

	deb_ts("urb complete feedcount: %d, status: %d, length: %d\n",dib->feedcount,urb->status,
			urb->actual_length);

	urb_compl_count++;
	if (urb_compl_count % 1000 == 0)
		deb_info("%d urbs completed so far.\n",urb_compl_count);

	switch (urb->status) {
		case 0:         /* success */
		case -ETIMEDOUT:    /* NAK */
			break;
		case -ECONNRESET:   /* kill */
		case -ENOENT:
		case -ESHUTDOWN:
			return;
		default:        /* error */
			deb_ts("urb completition error %d.", urb->status);
			break;
	}

	if (dib->feedcount > 0 && urb->actual_length > 0) {
		if (dib->init_state & DIBUSB_STATE_DVB)
			dvb_dmx_swfilter(&dib->demux, (u8*) urb->transfer_buffer,urb->actual_length);
	} else 
		deb_ts("URB dropped because of feedcount.\n");

	usb_submit_urb(urb,GFP_ATOMIC);
}

static int dibusb_ctrl_feed(struct dvb_demux_feed *dvbdmxfeed, int onoff) 
{
	struct usb_dibusb *dib = dvbdmxfeed->demux->priv;
	int newfeedcount;
	
	if (dib == NULL)
		return -ENODEV;

	newfeedcount = dib->feedcount + (onoff ? 1 : -1);

	/* 
	 * stop feed before setting a new pid if there will be no pid anymore 
	 */
	if (newfeedcount == 0) {
		deb_ts("stop feeding\n");
		if (dib->xfer_ops.fifo_ctrl != NULL) {
			if (dib->xfer_ops.fifo_ctrl(dib->fe,0)) {
				err("error while inhibiting fifo.");
				return -ENODEV;
			}
		}
		dibusb_streaming(dib,0);
	}
	
	dib->feedcount = newfeedcount;

	/* activate the pid on the device specific pid_filter */
	deb_ts("setting pid: %5d %04x at index %d '%s'\n",dvbdmxfeed->pid,dvbdmxfeed->pid,dvbdmxfeed->index,onoff ? "on" : "off");
	if (dib->pid_parse && dib->xfer_ops.pid_ctrl != NULL)
		dib->xfer_ops.pid_ctrl(dib->fe,dvbdmxfeed->index,dvbdmxfeed->pid,onoff);

	/* 
	 * start the feed if this was the first pid to set and there is still a pid
	 * for reception.
	 */
	if (dib->feedcount == onoff && dib->feedcount > 0) {

		deb_ts("controlling pid parser\n");
		if (dib->xfer_ops.pid_parse != NULL) {
			if (dib->xfer_ops.pid_parse(dib->fe,dib->pid_parse) < 0) {
				err("could not handle pid_parser");
			}
		}
		
		deb_ts("start feeding\n");
		if (dib->xfer_ops.fifo_ctrl != NULL) {
			if (dib->xfer_ops.fifo_ctrl(dib->fe,1)) {
				err("error while enabling fifo.");
				return -ENODEV;
			}
		}
		dibusb_streaming(dib,1);
	}
	return 0;
}

static int dibusb_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	deb_ts("start pid: 0x%04x, feedtype: %d\n", dvbdmxfeed->pid,dvbdmxfeed->type);
	return dibusb_ctrl_feed(dvbdmxfeed,1);
}

static int dibusb_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	deb_ts("stop pid: 0x%04x, feedtype: %d\n", dvbdmxfeed->pid, dvbdmxfeed->type);
	return dibusb_ctrl_feed(dvbdmxfeed,0);
}

int dibusb_dvb_init(struct usb_dibusb *dib)
{
	int ret;

	urb_compl_count = 0;

	if ((ret = dvb_register_adapter(&dib->adapter, DRIVER_DESC,
			THIS_MODULE)) < 0) {
		deb_info("dvb_register_adapter failed: error %d", ret);
		goto err;
	}
	dib->adapter.priv = dib;
	
/* i2c is done in dibusb_i2c_init */
	
	dib->demux.dmx.capabilities = DMX_TS_FILTERING | DMX_SECTION_FILTERING;

	dib->demux.priv = (void *)dib;
	/* get pidcount from demod */
	dib->demux.feednum = dib->demux.filternum = 255;
	dib->demux.start_feed = dibusb_start_feed;
	dib->demux.stop_feed = dibusb_stop_feed;
	dib->demux.write_to_decoder = NULL;
	if ((ret = dvb_dmx_init(&dib->demux)) < 0) {
		err("dvb_dmx_init failed: error %d",ret);
		goto err_dmx;
	}

	dib->dmxdev.filternum = dib->demux.filternum;
	dib->dmxdev.demux = &dib->demux.dmx;
	dib->dmxdev.capabilities = 0;
	if ((ret = dvb_dmxdev_init(&dib->dmxdev, &dib->adapter)) < 0) {
		err("dvb_dmxdev_init failed: error %d",ret);
		goto err_dmx_dev;
	}

	dvb_net_init(&dib->adapter, &dib->dvb_net, &dib->demux.dmx);

	goto success;
err_dmx_dev:
	dvb_dmx_release(&dib->demux);
err_dmx:
	dvb_unregister_adapter(&dib->adapter);
err:
	return ret;
success:
	dib->init_state |= DIBUSB_STATE_DVB;
	return 0;
}

int dibusb_dvb_exit(struct usb_dibusb *dib)
{
	if (dib->init_state & DIBUSB_STATE_DVB) {
		dib->init_state &= ~DIBUSB_STATE_DVB;
		deb_info("unregistering DVB part\n");
		dvb_net_release(&dib->dvb_net);
		dib->demux.dmx.close(&dib->demux.dmx);
		dvb_dmxdev_release(&dib->dmxdev);
		dvb_dmx_release(&dib->demux);
		dvb_unregister_adapter(&dib->adapter);
	}
	return 0;
}
