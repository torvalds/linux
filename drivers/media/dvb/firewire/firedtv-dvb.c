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

#include "avc.h"
#include "firedtv.h"
#include "firedtv-ci.h"

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static struct firedtv_channel *fdtv_channel_allocate(struct firedtv *fdtv)
{
	struct firedtv_channel *c = NULL;
	int k;

	if (mutex_lock_interruptible(&fdtv->demux_mutex))
		return NULL;

	for (k = 0; k < 16; k++)
		if (!fdtv->channel[k].active) {
			fdtv->channel[k].active = true;
			c = &fdtv->channel[k];
			break;
		}

	mutex_unlock(&fdtv->demux_mutex);
	return c;
}

static int fdtv_channel_collect(struct firedtv *fdtv, int *pidc, u16 pid[])
{
	int k, l = 0;

	if (mutex_lock_interruptible(&fdtv->demux_mutex))
		return -EINTR;

	for (k = 0; k < 16; k++)
		if (fdtv->channel[k].active)
			pid[l++] = fdtv->channel[k].pid;

	mutex_unlock(&fdtv->demux_mutex);

	*pidc = l;

	return 0;
}

static int fdtv_channel_release(struct firedtv *fdtv,
				   struct firedtv_channel *channel)
{
	if (mutex_lock_interruptible(&fdtv->demux_mutex))
		return -EINTR;

	channel->active = false;

	mutex_unlock(&fdtv->demux_mutex);
	return 0;
}

int fdtv_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct firedtv *fdtv = (struct firedtv*)dvbdmxfeed->demux->priv;
	struct firedtv_channel *channel;
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
			//Dirty fix to keep fdtv->channel pid-list up to date
			for(k=0;k<16;k++){
				if (!fdtv->channel[k].active)
					fdtv->channel[k].pid =
						dvbdmxfeed->pid;
					break;
			}
			channel = fdtv_channel_allocate(fdtv);
			break;
		default:
			printk(KERN_ERR "%s: invalid pes type %u\n",
			       __func__, dvbdmxfeed->pes_type);
			return -EINVAL;
		}
	} else {
		channel = fdtv_channel_allocate(fdtv);
	}

	if (!channel) {
		printk(KERN_ERR "%s: busy!\n", __func__);
		return -EBUSY;
	}

	dvbdmxfeed->priv = channel;
	channel->pid = dvbdmxfeed->pid;

	if (fdtv_channel_collect(fdtv, &pidc, pids)) {
		fdtv_channel_release(fdtv, channel);
		printk(KERN_ERR "%s: could not collect pids!\n", __func__);
		return -EINTR;
	}

	if (dvbdmxfeed->pid == 8192) {
		k = avc_tuner_get_ts(fdtv);
		if (k) {
			fdtv_channel_release(fdtv, channel);
			printk("%s: AVCTuner_GetTS failed with error %d\n",
			       __func__, k);
			return k;
		}
	} else {
		k = avc_tuner_set_pids(fdtv, pidc, pids);
		if (k) {
			fdtv_channel_release(fdtv, channel);
			printk("%s: AVCTuner_SetPIDs failed with error %d\n",
			       __func__, k);
			return k;
		}
	}

	return 0;
}

int fdtv_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *demux = dvbdmxfeed->demux;
	struct firedtv *fdtv = (struct firedtv*)demux->priv;
	struct firedtv_channel *c = dvbdmxfeed->priv;
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

	if (mutex_lock_interruptible(&fdtv->demux_mutex))
		return -EINTR;

	/* list except channel to be removed */
	for (k = 0, l = 0; k < 16; k++)
		if (fdtv->channel[k].active) {
			if (&fdtv->channel[k] != c)
				pids[l++] = fdtv->channel[k].pid;
			else
				fdtv->channel[k].active = false;
		}

	k = avc_tuner_set_pids(fdtv, l, pids);
	if (!k)
		c->active = false;

	mutex_unlock(&fdtv->demux_mutex);
	return k;
}

int fdtv_dvbdev_init(struct firedtv *fdtv, struct device *dev)
{
	int err;

	err = DVB_REGISTER_ADAPTER(&fdtv->adapter,
				   fdtv_model_names[fdtv->type],
				   THIS_MODULE, dev, adapter_nr);
	if (err < 0)
		goto fail_log;

	/*DMX_TS_FILTERING | DMX_SECTION_FILTERING*/
	fdtv->demux.dmx.capabilities = 0;

	fdtv->demux.priv	= fdtv;
	fdtv->demux.filternum	= 16;
	fdtv->demux.feednum	= 16;
	fdtv->demux.start_feed	= fdtv_start_feed;
	fdtv->demux.stop_feed	= fdtv_stop_feed;
	fdtv->demux.write_to_decoder = NULL;

	err = dvb_dmx_init(&fdtv->demux);
	if (err)
		goto fail_unreg_adapter;

	fdtv->dmxdev.filternum	= 16;
	fdtv->dmxdev.demux		= &fdtv->demux.dmx;
	fdtv->dmxdev.capabilities	= 0;

	err = dvb_dmxdev_init(&fdtv->dmxdev, &fdtv->adapter);
	if (err)
		goto fail_dmx_release;

	fdtv->frontend.source = DMX_FRONTEND_0;

	err = fdtv->demux.dmx.add_frontend(&fdtv->demux.dmx,
					      &fdtv->frontend);
	if (err)
		goto fail_dmxdev_release;

	err = fdtv->demux.dmx.connect_frontend(&fdtv->demux.dmx,
						  &fdtv->frontend);
	if (err)
		goto fail_rem_frontend;

	dvb_net_init(&fdtv->adapter, &fdtv->dvbnet, &fdtv->demux.dmx);

	fdtv_frontend_init(fdtv);
	err = dvb_register_frontend(&fdtv->adapter, &fdtv->fe);
	if (err)
		goto fail_net_release;

	err = fdtv_ca_register(fdtv);
	if (err)
		dev_info(dev, "Conditional Access Module not enabled\n");

	return 0;

fail_net_release:
	dvb_net_release(&fdtv->dvbnet);
	fdtv->demux.dmx.close(&fdtv->demux.dmx);
fail_rem_frontend:
	fdtv->demux.dmx.remove_frontend(&fdtv->demux.dmx,
					   &fdtv->frontend);
fail_dmxdev_release:
	dvb_dmxdev_release(&fdtv->dmxdev);
fail_dmx_release:
	dvb_dmx_release(&fdtv->demux);
fail_unreg_adapter:
	dvb_unregister_adapter(&fdtv->adapter);
fail_log:
	dev_err(dev, "DVB initialization failed\n");
	return err;
}


