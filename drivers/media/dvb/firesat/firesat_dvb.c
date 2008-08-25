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
		if (firesat->channel[k].active == 0) {
			firesat->channel[k].active = 1;
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
		if (firesat->channel[k].active == 1)
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

	channel->active = 0;

	mutex_unlock(&firesat->demux_mutex);
	return 0;
}

int firesat_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct firesat *firesat = (struct firesat*)dvbdmxfeed->demux->priv;
	struct firesat_channel *channel;
	int pidc,k;
	u16 pids[16];

//	printk(KERN_INFO "%s (pid %u)\n", __func__, dvbdmxfeed->pid);

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
				if(firesat->channel[k].active == 0)
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

	channel->dvbdmxfeed = dvbdmxfeed;
	channel->pid = dvbdmxfeed->pid;
	channel->type = dvbdmxfeed->type;
	channel->firesat = firesat;

	if (firesat_channel_collect(firesat, &pidc, pids)) {
		firesat_channel_release(firesat, channel);
		printk(KERN_ERR "%s: could not collect pids!\n", __func__);
		return -EINTR;
	}

	if(dvbdmxfeed->pid == 8192) {
		if((k = AVCTuner_GetTS(firesat))) {
			firesat_channel_release(firesat, channel);
			printk("%s: AVCTuner_GetTS failed with error %d\n",
			       __func__, k);
			return k;
		}
	}
	else {
		if((k = AVCTuner_SetPIDs(firesat, pidc, pids))) {
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

	//printk(KERN_INFO "%s (pid %u)\n", __func__, dvbdmxfeed->pid);

	if (dvbdmxfeed->type == DMX_TYPE_TS && !((dvbdmxfeed->ts_type & TS_PACKET) &&
				(demux->dmx.frontend->source != DMX_MEMORY_FE))) {

		if (dvbdmxfeed->ts_type & TS_DECODER) {

			if (dvbdmxfeed->pes_type >= DMX_TS_PES_OTHER ||
				!demux->pesfilter[dvbdmxfeed->pes_type])

				return -EINVAL;

			demux->pids[dvbdmxfeed->pes_type] |= 0x8000;
			demux->pesfilter[dvbdmxfeed->pes_type] = 0;
		}

		if (!(dvbdmxfeed->ts_type & TS_DECODER &&
			dvbdmxfeed->pes_type < DMX_TS_PES_OTHER))

			return 0;
	}

	if (mutex_lock_interruptible(&firesat->demux_mutex))
		return -EINTR;

	/* list except channel to be removed */
	for (k = 0, l = 0; k < 16; k++)
		if (firesat->channel[k].active == 1) {
			if (&firesat->channel[k] != c)
				pids[l++] = firesat->channel[k].pid;
			else
				firesat->channel[k].active = 0;
		}

	k = AVCTuner_SetPIDs(firesat, l, pids);
	if (!k)
		c->active = 0;

	mutex_unlock(&firesat->demux_mutex);
	return k;
}

int firesat_dvbdev_init(struct firesat *firesat,
			struct device *dev,
			struct dvb_frontend *fe)
{
	int result;

	firesat->adapter = kmalloc(sizeof(*firesat->adapter), GFP_KERNEL);
	if (!firesat->adapter) {
		printk(KERN_ERR "firedtv: couldn't allocate memory\n");
		return -ENOMEM;
	}

	result = DVB_REGISTER_ADAPTER(firesat->adapter,
				      firedtv_model_names[firesat->type],
				      THIS_MODULE, dev, adapter_nr);
	if (result < 0) {
		printk(KERN_ERR "firedtv: dvb_register_adapter failed\n");
		kfree(firesat->adapter);
		return result;
	}

		memset(&firesat->demux, 0, sizeof(struct dvb_demux));
		firesat->demux.dmx.capabilities = 0/*DMX_TS_FILTERING | DMX_SECTION_FILTERING*/;

		firesat->demux.priv		= (void *)firesat;
		firesat->demux.filternum	= 16;
		firesat->demux.feednum		= 16;
		firesat->demux.start_feed	= firesat_start_feed;
		firesat->demux.stop_feed	= firesat_stop_feed;
		firesat->demux.write_to_decoder	= NULL;

		if ((result = dvb_dmx_init(&firesat->demux)) < 0) {
			printk("%s: dvb_dmx_init failed: error %d\n", __func__,
				   result);

			dvb_unregister_adapter(firesat->adapter);

			return result;
		}

		firesat->dmxdev.filternum	= 16;
		firesat->dmxdev.demux		= &firesat->demux.dmx;
		firesat->dmxdev.capabilities	= 0;

		if ((result = dvb_dmxdev_init(&firesat->dmxdev, firesat->adapter)) < 0) {
			printk("%s: dvb_dmxdev_init failed: error %d\n",
				   __func__, result);

			dvb_dmx_release(&firesat->demux);
			dvb_unregister_adapter(firesat->adapter);

			return result;
		}

		firesat->frontend.source = DMX_FRONTEND_0;

		if ((result = firesat->demux.dmx.add_frontend(&firesat->demux.dmx,
							  &firesat->frontend)) < 0) {
			printk("%s: dvb_dmx_init failed: error %d\n", __func__,
				   result);

			dvb_dmxdev_release(&firesat->dmxdev);
			dvb_dmx_release(&firesat->demux);
			dvb_unregister_adapter(firesat->adapter);

			return result;
		}

		if ((result = firesat->demux.dmx.connect_frontend(&firesat->demux.dmx,
								  &firesat->frontend)) < 0) {
			printk("%s: dvb_dmx_init failed: error %d\n", __func__,
				   result);

			firesat->demux.dmx.remove_frontend(&firesat->demux.dmx, &firesat->frontend);
			dvb_dmxdev_release(&firesat->dmxdev);
			dvb_dmx_release(&firesat->demux);
			dvb_unregister_adapter(firesat->adapter);

			return result;
		}

		dvb_net_init(firesat->adapter, &firesat->dvbnet, &firesat->demux.dmx);

//		fe->ops = firesat_ops;
//		fe->dvb = firesat->adapter;
		firesat_frontend_attach(firesat, fe);

		fe->sec_priv = firesat; //IMPORTANT, functions depend on this!!!
		if ((result= dvb_register_frontend(firesat->adapter, fe)) < 0) {
			printk("%s: dvb_register_frontend_new failed: error %d\n", __func__, result);
			/* ### cleanup */
			return result;
		}

			firesat_ca_init(firesat);

		return 0;
}


