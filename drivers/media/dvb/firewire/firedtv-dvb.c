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

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include <dmxdev.h>
#include <dvb_demux.h>
#include <dvbdev.h>
#include <dvb_frontend.h>

#include "firedtv.h"

static int alloc_channel(struct firedtv *fdtv)
{
	int i;

	for (i = 0; i < 16; i++)
		if (!__test_and_set_bit(i, &fdtv->channel_active))
			break;
	return i;
}

static void collect_channels(struct firedtv *fdtv, int *pidc, u16 pid[])
{
	int i, n;

	for (i = 0, n = 0; i < 16; i++)
		if (test_bit(i, &fdtv->channel_active))
			pid[n++] = fdtv->channel_pid[i];
	*pidc = n;
}

static inline void dealloc_channel(struct firedtv *fdtv, int i)
{
	__clear_bit(i, &fdtv->channel_active);
}

int fdtv_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct firedtv *fdtv = dvbdmxfeed->demux->priv;
	int pidc, c, ret;
	u16 pids[16];

	switch (dvbdmxfeed->type) {
	case DMX_TYPE_TS:
	case DMX_TYPE_SEC:
		break;
	default:
		dev_err(fdtv->device, "can't start dmx feed: invalid type %u\n",
			dvbdmxfeed->type);
		return -EINVAL;
	}

	if (mutex_lock_interruptible(&fdtv->demux_mutex))
		return -EINTR;

	if (dvbdmxfeed->type == DMX_TYPE_TS) {
		switch (dvbdmxfeed->pes_type) {
		case DMX_TS_PES_VIDEO:
		case DMX_TS_PES_AUDIO:
		case DMX_TS_PES_TELETEXT:
		case DMX_TS_PES_PCR:
		case DMX_TS_PES_OTHER:
			c = alloc_channel(fdtv);
			break;
		default:
			dev_err(fdtv->device,
				"can't start dmx feed: invalid pes type %u\n",
				dvbdmxfeed->pes_type);
			ret = -EINVAL;
			goto out;
		}
	} else {
		c = alloc_channel(fdtv);
	}

	if (c > 15) {
		dev_err(fdtv->device, "can't start dmx feed: busy\n");
		ret = -EBUSY;
		goto out;
	}

	dvbdmxfeed->priv = (typeof(dvbdmxfeed->priv))(unsigned long)c;
	fdtv->channel_pid[c] = dvbdmxfeed->pid;
	collect_channels(fdtv, &pidc, pids);

	if (dvbdmxfeed->pid == 8192) {
		ret = avc_tuner_get_ts(fdtv);
		if (ret) {
			dealloc_channel(fdtv, c);
			dev_err(fdtv->device, "can't get TS\n");
			goto out;
		}
	} else {
		ret = avc_tuner_set_pids(fdtv, pidc, pids);
		if (ret) {
			dealloc_channel(fdtv, c);
			dev_err(fdtv->device, "can't set PIDs\n");
			goto out;
		}
	}
out:
	mutex_unlock(&fdtv->demux_mutex);

	return ret;
}

int fdtv_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *demux = dvbdmxfeed->demux;
	struct firedtv *fdtv = demux->priv;
	int pidc, c, ret;
	u16 pids[16];

	if (dvbdmxfeed->type == DMX_TYPE_TS &&
	    !((dvbdmxfeed->ts_type & TS_PACKET) &&
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

	c = (unsigned long)dvbdmxfeed->priv;
	dealloc_channel(fdtv, c);
	collect_channels(fdtv, &pidc, pids);

	ret = avc_tuner_set_pids(fdtv, pidc, pids);

	mutex_unlock(&fdtv->demux_mutex);

	return ret;
}

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

int fdtv_dvb_register(struct firedtv *fdtv, const char *name)
{
	int err;

	err = dvb_register_adapter(&fdtv->adapter, name,
				   THIS_MODULE, fdtv->device, adapter_nr);
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

	fdtv->dmxdev.filternum    = 16;
	fdtv->dmxdev.demux        = &fdtv->demux.dmx;
	fdtv->dmxdev.capabilities = 0;

	err = dvb_dmxdev_init(&fdtv->dmxdev, &fdtv->adapter);
	if (err)
		goto fail_dmx_release;

	fdtv->frontend.source = DMX_FRONTEND_0;

	err = fdtv->demux.dmx.add_frontend(&fdtv->demux.dmx, &fdtv->frontend);
	if (err)
		goto fail_dmxdev_release;

	err = fdtv->demux.dmx.connect_frontend(&fdtv->demux.dmx,
					       &fdtv->frontend);
	if (err)
		goto fail_rem_frontend;

	dvb_net_init(&fdtv->adapter, &fdtv->dvbnet, &fdtv->demux.dmx);

	fdtv_frontend_init(fdtv, name);
	err = dvb_register_frontend(&fdtv->adapter, &fdtv->fe);
	if (err)
		goto fail_net_release;

	err = fdtv_ca_register(fdtv);
	if (err)
		dev_info(fdtv->device,
			 "Conditional Access Module not enabled\n");
	return 0;

fail_net_release:
	dvb_net_release(&fdtv->dvbnet);
	fdtv->demux.dmx.close(&fdtv->demux.dmx);
fail_rem_frontend:
	fdtv->demux.dmx.remove_frontend(&fdtv->demux.dmx, &fdtv->frontend);
fail_dmxdev_release:
	dvb_dmxdev_release(&fdtv->dmxdev);
fail_dmx_release:
	dvb_dmx_release(&fdtv->demux);
fail_unreg_adapter:
	dvb_unregister_adapter(&fdtv->adapter);
fail_log:
	dev_err(fdtv->device, "DVB initialization failed\n");
	return err;
}

void fdtv_dvb_unregister(struct firedtv *fdtv)
{
	fdtv_ca_release(fdtv);
	dvb_unregister_frontend(&fdtv->fe);
	dvb_net_release(&fdtv->dvbnet);
	fdtv->demux.dmx.close(&fdtv->demux.dmx);
	fdtv->demux.dmx.remove_frontend(&fdtv->demux.dmx, &fdtv->frontend);
	dvb_dmxdev_release(&fdtv->dmxdev);
	dvb_dmx_release(&fdtv->demux);
	dvb_unregister_adapter(&fdtv->adapter);
}
