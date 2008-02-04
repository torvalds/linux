/*
 *  pvrusb2-dvb.c - linux-dvb api interface to the pvrusb2 driver.
 *
 *  Copyright (C) 2007, 2008 Michael Krufky <mkrufky@linuxtv.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "dvbdev.h"
#include "pvrusb2-hdw-internal.h"
#include "pvrusb2-hdw.h"
#include "pvrusb2-dvb.h"

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static int pvr2_dvb_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	printk(KERN_DEBUG "start pid: 0x%04x, feedtype: %d\n",
	       dvbdmxfeed->pid, dvbdmxfeed->type);
	return 0; /* FIXME: pvr2_dvb_ctrl_feed(dvbdmxfeed, 1); */
}

static int pvr2_dvb_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	printk(KERN_DEBUG "stop pid: 0x%04x, feedtype: %d\n",
	       dvbdmxfeed->pid, dvbdmxfeed->type);
	return 0; /* FIXME: pvr2_dvb_ctrl_feed(dvbdmxfeed, 0); */
}

static int pvr2_dvb_bus_ctrl(struct dvb_frontend *fe, int acquire)
{
	/* TO DO: This function will call into the core and request for
	 * input to be set to 'dtv' if (acquire) and if it isn't set already.
	 *
	 * If (!acquire) then we should do nothing -- don't switch inputs
	 * again unless the analog side of the driver requests the bus.
	 */
	return 0;
}

static int pvr2_dvb_adapter_init(struct pvr2_dvb_adapter *adap)
{
	int ret;

	ret = dvb_register_adapter(&adap->dvb_adap, "pvrusb2-dvb",
				   THIS_MODULE/*&hdw->usb_dev->owner*/,
				   &adap->pvr->hdw->usb_dev->dev,
				   adapter_nr);
	if (ret < 0) {
		err("dvb_register_adapter failed: error %d", ret);
		goto err;
	}
	adap->dvb_adap.priv = adap;

	adap->demux.dmx.capabilities = DMX_TS_FILTERING |
				       DMX_SECTION_FILTERING |
				       DMX_MEMORY_BASED_FILTERING;
	adap->demux.priv             = adap;
	adap->demux.filternum        = 256;
	adap->demux.feednum          = 256;
	adap->demux.start_feed       = pvr2_dvb_start_feed;
	adap->demux.stop_feed        = pvr2_dvb_stop_feed;
	adap->demux.write_to_decoder = NULL;

	ret = dvb_dmx_init(&adap->demux);
	if (ret < 0) {
		err("dvb_dmx_init failed: error %d", ret);
		goto err_dmx;
	}

	adap->dmxdev.filternum       = adap->demux.filternum;
	adap->dmxdev.demux           = &adap->demux.dmx;
	adap->dmxdev.capabilities    = 0;

	ret = dvb_dmxdev_init(&adap->dmxdev, &adap->dvb_adap);
	if (ret < 0) {
		err("dvb_dmxdev_init failed: error %d", ret);
		goto err_dmx_dev;
	}

	dvb_net_init(&adap->dvb_adap, &adap->dvb_net, &adap->demux.dmx);

	adap->digital_up = 1;

	return 0;

err_dmx_dev:
	dvb_dmx_release(&adap->demux);
err_dmx:
	dvb_unregister_adapter(&adap->dvb_adap);
err:
	return ret;
}

static int pvr2_dvb_adapter_exit(struct pvr2_dvb_adapter *adap)
{
	if (adap->digital_up) {
		printk(KERN_DEBUG "unregistering DVB devices\n");
		dvb_net_release(&adap->dvb_net);
		adap->demux.dmx.close(&adap->demux.dmx);
		dvb_dmxdev_release(&adap->dmxdev);
		dvb_dmx_release(&adap->demux);
		dvb_unregister_adapter(&adap->dvb_adap);
		adap->digital_up = 0;
	}
	return 0;
}

static int pvr2_dvb_frontend_init(struct pvr2_dvb_adapter *adap)
{
	struct pvr2_dvb_props *dvb_props = adap->pvr->hdw->hdw_desc->dvb_props;

	if (dvb_props == NULL) {
		err("fe_props not defined!");
		return -EINVAL;
	}

	if (dvb_props->frontend_attach == NULL) {
		err("frontend_attach not defined!");
		return -EINVAL;
	}

	if ((dvb_props->frontend_attach(adap) == 0) && (adap->fe)) {

		if (dvb_register_frontend(&adap->dvb_adap, adap->fe)) {
			err("frontend registration failed!");
			dvb_frontend_detach(adap->fe);
			adap->fe = NULL;
			return -ENODEV;
		}

		if (dvb_props->tuner_attach)
			dvb_props->tuner_attach(adap);

		if (adap->fe->ops.analog_ops.standby)
			adap->fe->ops.analog_ops.standby(adap->fe);

		/* Ensure all frontends negotiate bus access */
		adap->fe->ops.ts_bus_ctrl = pvr2_dvb_bus_ctrl;

	} else {
		err("no frontend was attached!");
		return -ENODEV;
	}

	return 0;
}

static int pvr2_dvb_frontend_exit(struct pvr2_dvb_adapter *adap)
{
	if (adap->fe != NULL) {
		dvb_unregister_frontend(adap->fe);
		dvb_frontend_detach(adap->fe);
	}
	return 0;
}

int pvr2_dvb_init(struct pvr2_context *pvr)
{
	int ret = 0;

	pvr->hdw->dvb.pvr = pvr;

	ret = pvr2_dvb_adapter_init(&pvr->hdw->dvb);
	if (ret < 0)
		goto fail;

	ret = pvr2_dvb_frontend_init(&pvr->hdw->dvb);
fail:
	return ret;
}

int pvr2_dvb_exit(struct pvr2_context *pvr)
{
	pvr2_dvb_frontend_exit(&pvr->hdw->dvb);
	pvr2_dvb_adapter_exit(&pvr->hdw->dvb);

	pvr->hdw->dvb.pvr = NULL;

	return 0;
}
