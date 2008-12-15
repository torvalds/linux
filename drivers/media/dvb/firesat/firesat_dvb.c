/*
 * FireDTV driver (formerly known as FireSAT)
 *
 * Copyright (C) 2004 Andreas Monitzer <andy@monitzer.com>
 * Copyright (C) 2008 Henrik Kurelid <henrik@kurelid.se>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include <dvb_demux.h>
#include <dvb_frontend.h>
#include <dvbdev.h>

#include "avc_api.h"
#include "firesat.h"
#include "firesat-ci.h"

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static struct firesat_channel *firesat_channel_allocate(struct firesat *firesat)
{
	struct firesat_channel *c = NULL;
	int k;

	if (mutex_lock_interruptible(&firesat->demux_mutex))
		return NULL;

	for (k = 0; k < 16; k++)
		if (!firesat->channel[k].active) {
			firesat->channel[k].active = true;
			c = &firesat->channel[k];
			break;
		}

	mutex_unlock(&firesat->demux_mutex);
	return c;
}

static int firesat_channel_collect(struct firesat *firesat, int *pidc, u16 pid[])
{
	int k, l = 0;

	if (mutex_lock_interruptible(&firesat->demux_mutex))
		return -EINTR;

	for (k = 0; k < 16; k++)
		if (firesat->channel[k].active)
			pid[l++] = firesat->channel[k].pid;

	mutex_unlock(&firesat->demux_mutex);

	*pidc = l;

	return 0;
}

static int firesat_channel_release(struct firesat *firesat,
				   struct firesat_channel *channel)
{
	if (mutex_lock_interruptible(&firesat->demux_mutex))
		return -EINTR;

	channel->active = false;

	mutex_unlock(&firesat->demux_mutex);
	return 0;
}

int firesat_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct firesat *firesat = (struct firesat*)dvbdmxfeed->demux->priv;
	struct firesat_channel *channel;
	int pidc,k;
	u16 pids[16];

	switch (dvbdmxfeed->type) {
	case DMX_TYPE_TS:
	case DMX_TYPE_SEC:
		break;
	default:
		printk(KERN_ERR "%s: invalid type %u\n",
		       __func__, dvbdmxfeed->type);
		return -EINVAL;
	}

	if (dvbdmxfeed->type == DMX_TYPE_TS) {
		switch (dvbdmxfeed->pes_type) {
		case DMX_TS_PES_VIDEO:
		case DMX_TS_PES_AUDIO:
		case DMX_TS_PES_TELETEXT:
		case DMX_TS_PES_PCR:
		case DMX_TS_PES_OTHER:
			//Dirty fix to keep firesat->channel pid-list up to date
			for(k=0;k<16;k++){
				if (!firesat->channel[k].active)
					firesat->channel[k].pid =
						dvbdmxfeed->pid;
					break;
			}
			channel = firesat_channel_allocate(firesat);
			break;
		default:
			printk(KERN_ERR "%s: invalid pes type %u\n",
			       __func__, dvbdmxfeed->pes_type);
			return -EINVAL;
		}
	} else {
		channel = firesat_channel_allocate(firesat);
	}

	if (!channel) {
		printk(KERN_ERR "%s: busy!\n", __func__);
		return -EBUSY;
	}

	dvbdmxfeed->priv = channel;
	channel->pid = dvbdmxfeed->pid;

	if (firesat_channel_collect(firesat, &pidc, pids)) {
		firesat_channel_release(firesat, channel);
		printk(KERN_ERR "%s: could not collect pids!\n", __func__);
		return -EINTR;
	}

	if (dvbdmxfeed->pid == 8192) {
		k = avc_tuner_get_ts(firesat);
		if (k) {
			firesat_channel_release(firesat, channel);
			printk("%s: AVCTuner_GetTS failed with error %d\n",
			       __func__, k);
			return k;
		}
	} else {
		k = avc_tuner_set_pids(firesat, pidc, pids);
		if (k) {
			firesat_channel_release(firesat, channel);
			printk("%s: AVCTuner_SetPIDs failed with error %d\n",
			       __func__, k);
			return k;
		}
	}

	return 0;
}

int firesat_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *demux = dvbdmxfeed->demux;
	struct firesat *firesat = (struct firesat*)demux->priv;
	struct firesat_channel *c = dvbdmxfeed->priv;
	int k, l;
	u16 pids[16];

	if (dvbdmxfeed->type == DMX_TYPE_TS && !((dvbdmxfeed->ts_type & TS_PACKET) &&
				(demux->dmx.frontend->source != DMX_MEMORY_FE))) {

		if (dvbdmxfeed->ts_type & TS_DECODER) {

			if (dvbdmxfeed->pes_type >= DMX_TS_PES_OTHER ||
				!demux->pesfilter[dvbdmxfeed->pes_type])

				return -EINVAL;

			demux->pids[dvbdmxfeed->pes_type] |= 0x8000;
			demux->pesfilter[dvbdmxfeed->pes_type] = NULL;
		}

		if (!(dvbdmxfeed->ts_type & TS_DECODER &&
			dvbdmxfeed->pes_type < DMX_TS_PES_OTHER))

			return 0;
	}

	if (mutex_lock_interruptible(&firesat->demux_mutex))
		return -EINTR;

	/* list except channel to be removed */
	for (k = 0, l = 0; k < 16; k++)
		if (firesat->channel[k].active) {
			if (&firesat->channel[k] != c)
				pids[l++] = firesat->channel[k].pid;
			else
				firesat->channel[k].active = false;
		}

	k = avc_tuner_set_pids(firesat, l, pids);
	if (!k)
		c->active = false;

	mutex_unlock(&firesat->demux_mutex);
	return k;
}

int firesat_dvbdev_init(struct firesat *firesat, struct device *dev)
{
	int err;

	err = DVB_REGISTER_ADAPTER(&firesat->adapter,
				   firedtv_model_names[firesat->type],
				   THIS_MODULE, dev, adapter_nr);
	if (err < 0)
		goto fail_log;

	/*DMX_TS_FILTERING | DMX_SECTION_FILTERING*/
	firesat->demux.dmx.capabilities = 0;

	firesat->demux.priv		= (void *)firesat;
	firesat->demux.filternum	= 16;
	firesat->demux.feednum		= 16;
	firesat->demux.start_feed	= firesat_start_feed;
	firesat->demux.stop_feed	= firesat_stop_feed;
	firesat->demux.write_to_decoder	= NULL;

	err = dvb_dmx_init(&firesat->demux);
	if (err)
		goto fail_unreg_adapter;

	firesat->dmxdev.filternum	= 16;
	firesat->dmxdev.demux		= &firesat->demux.dmx;
	firesat->dmxdev.capabilities	= 0;

	err = dvb_dmxdev_init(&firesat->dmxdev, &firesat->adapter);
	if (err)
		goto fail_dmx_release;

	firesat->frontend.source = DMX_FRONTEND_0;

	err = firesat->demux.dmx.add_frontend(&firesat->demux.dmx,
					      &firesat->frontend);
	if (err)
		goto fail_dmxdev_release;

	err = firesat->demux.dmx.connect_frontend(&firesat->demux.dmx,
						  &firesat->frontend);
	if (err)
		goto fail_rem_frontend;

	dvb_net_init(&firesat->adapter, &firesat->dvbnet, &firesat->demux.dmx);

	firesat_frontend_init(firesat);
	err = dvb_register_frontend(&firesat->adapter, &firesat->fe);
	if (err)
		goto fail_net_release;

	err = firesat_ca_register(firesat);
	if (err)
		dev_info(dev, "Conditional Access Module not enabled\n");

	return 0;

fail_net_release:
	dvb_net_release(&firesat->dvbnet);
	firesat->demux.dmx.close(&firesat->demux.dmx);
fail_rem_frontend:
	firesat->demux.dmx.remove_frontend(&firesat->demux.dmx,
					   &firesat->frontend);
fail_dmxdev_release:
	dvb_dmxdev_release(&firesat->dmxdev);
fail_dmx_release:
	dvb_dmx_release(&firesat->demux);
fail_unreg_adapter:
	dvb_unregister_adapter(&firesat->adapter);
fail_log:
	dev_err(dev, "DVB initialization failed\n");
	return err;
}


