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

#include <linux/kthread.h>
#include <linux/freezer.h>
#include "dvbdev.h"
#include "pvrusb2-hdw-internal.h"
#include "pvrusb2-hdw.h"
#include "pvrusb2-io.h"
#include "pvrusb2-dvb.h"

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

#define BUFFER_COUNT 32
#define BUFFER_SIZE PAGE_ALIGN(0x4000)

struct pvr2_dvb_fh {
	struct pvr2_channel	channel;
	struct pvr2_stream	*stream;
	struct pvr2_dvb_adapter	*adap;
	wait_queue_head_t	wait_data;
	char			*buffer_storage[BUFFER_COUNT];
};

static void pvr2_dvb_notify(struct pvr2_dvb_fh *fhp)
{
	wake_up(&fhp->wait_data);
}

static int pvr2_dvb_fh_init(struct pvr2_dvb_fh *fh,
			    struct pvr2_dvb_adapter *adap)
{
	struct pvr2_context *pvr = adap->pvr;
	unsigned int idx;
	int ret;
	struct pvr2_buffer *bp;

	init_waitqueue_head(&fh->wait_data);

	fh->adap = adap;

	pvr2_channel_init(&fh->channel, adap->pvr);

	ret = pvr2_channel_claim_stream(&fh->channel, &pvr->video_stream);
	/* somebody else already has the stream */
	if (ret != 0)
		return ret;

	fh->stream = pvr->video_stream.stream;

	for (idx = 0; idx < BUFFER_COUNT; idx++) {
		fh->buffer_storage[idx] = kmalloc(BUFFER_SIZE, GFP_KERNEL);
		if (!(fh->buffer_storage[idx]))
			break;
	}

	if (idx < BUFFER_COUNT) {
		/* An allocation appears to have failed */
		ret = -ENOMEM;
		goto cleanup;
	}

	pvr2_stream_set_callback(pvr->video_stream.stream,
				 (pvr2_stream_callback) pvr2_dvb_notify, fh);

	ret = pvr2_stream_set_buffer_count(fh->stream, BUFFER_COUNT);
	if (ret < 0)
		return ret;

	for (idx = 0; idx < BUFFER_COUNT; idx++) {
		bp = pvr2_stream_get_buffer(fh->stream, idx);
		pvr2_buffer_set_buffer(bp,
				       fh->buffer_storage[idx],
				       BUFFER_SIZE);
	}

	ret = pvr2_hdw_set_streaming(fh->channel.hdw, 1);
	if (ret < 0)
		goto cleanup;

	while ((bp = pvr2_stream_get_idle_buffer(fh->stream)) != 0) {
		ret = pvr2_buffer_queue(bp);
		if (ret < 0)
			goto cleanup;
	}

	return ret;

cleanup:
	if (fh->stream)
		pvr2_stream_kill(fh->stream);

	for (idx = 0; idx < BUFFER_COUNT; idx++) {
		if (!(fh->buffer_storage[idx]))
			continue;

		kfree(fh->buffer_storage[idx]);
	}
	pvr2_channel_done(&fh->channel);

	return ret;
}

static void pvr2_dvb_fh_done(struct pvr2_dvb_fh *fh)
{
	unsigned int idx;

	pvr2_hdw_set_streaming(fh->channel.hdw, 0);

	pvr2_stream_kill(fh->stream);

//	pvr2_channel_claim_stream(&fh->channel, NULL);

	for (idx = 0; idx < BUFFER_COUNT; idx++) {
		if (!(fh->buffer_storage[idx]))
			continue;

		kfree(fh->buffer_storage[idx]);
	}

	pvr2_channel_done(&fh->channel);
}

static int pvr2_dvb_feed_func(struct pvr2_dvb_adapter *adap)
{
	struct pvr2_dvb_fh fh;
	int ret;
	unsigned int count;
	struct pvr2_buffer *bp;

	printk(KERN_DEBUG "dvb thread started\n");
	set_freezable();

	memset(&fh, 0, sizeof(fh));

	ret = pvr2_dvb_fh_init(&fh, adap);
	if (ret != 0)
		return ret;

	for (;;) {
		if ((0 == adap->feedcount) || (kthread_should_stop()))
			break;

		/* Not sure about this... */
		try_to_freeze();

		bp = pvr2_stream_get_ready_buffer(fh.stream);
		if (bp != NULL) {
			count = pvr2_buffer_get_count(bp);
			if (count) {
				dvb_dmx_swfilter(
					&adap->demux,
					fh.buffer_storage[
						pvr2_buffer_get_id(bp)],
					count);
			} else {
				ret = pvr2_buffer_get_status(bp);
				if (ret < 0)
					break;
			}
			ret = pvr2_buffer_queue(bp);
			if (ret < 0)
				break;

			/* Since we know we did something to a buffer,
			   just go back and try again.  No point in
			   blocking unless we really ran out of
			   buffers to process. */
			continue;
		}


		/* Wait until more buffers become available. */
		ret = wait_event_interruptible(
			fh.wait_data,
			pvr2_stream_get_ready_count(fh.stream) > 0);
		if (ret < 0)
			break;
	}

	pvr2_dvb_fh_done(&fh);

	/* If we get here and ret is < 0, then an error has occurred.
	   Probably would be a good idea to communicate that to DVB core... */

	printk(KERN_DEBUG "dvb thread stopped\n");

	return 0;
}

static int pvr2_dvb_feed_thread(void *data)
{
	int stat = pvr2_dvb_feed_func(data);
	/* from videobuf-dvb.c: */
	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
	}
	return stat;
}

static int pvr2_dvb_ctrl_feed(struct dvb_demux_feed *dvbdmxfeed, int onoff)
{
	struct pvr2_dvb_adapter *adap = dvbdmxfeed->demux->priv;
	int newfeedcount, ret = 0;

	if (adap == NULL)
		return -ENODEV;

	mutex_lock(&adap->lock);
	newfeedcount = adap->feedcount + (onoff ? 1 : -1);

	if (newfeedcount == 0) {
		printk(KERN_DEBUG "stop feeding\n");

		ret = kthread_stop(adap->thread);
		adap->thread = NULL;
	}

	adap->feedcount = newfeedcount;

	if (adap->feedcount == onoff && adap->feedcount > 0) {
		if (NULL != adap->thread)
			goto fail;

		printk(KERN_DEBUG "start feeding\n");

		adap->thread = kthread_run(pvr2_dvb_feed_thread,
					   adap, "pvrusb2-dvb");
		if (IS_ERR(adap->thread)) {
			ret = PTR_ERR(adap->thread);
			adap->thread = NULL;
		}
		//ret = newfeedcount;
	}
fail:
	mutex_unlock(&adap->lock);

	return ret;
}

static int pvr2_dvb_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	printk(KERN_DEBUG "start pid: 0x%04x, feedtype: %d\n",
	       dvbdmxfeed->pid, dvbdmxfeed->type);
	return pvr2_dvb_ctrl_feed(dvbdmxfeed, 1);
}

static int pvr2_dvb_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	printk(KERN_DEBUG "stop pid: 0x%04x, feedtype: %d\n",
	       dvbdmxfeed->pid, dvbdmxfeed->type);
	return pvr2_dvb_ctrl_feed(dvbdmxfeed, 0);
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
	struct pvr2_hdw *hdw = adap->pvr->hdw;
	struct pvr2_dvb_props *dvb_props = hdw->hdw_desc->dvb_props;
	int ret;

	if (dvb_props == NULL) {
		err("fe_props not defined!");
		return -EINVAL;
	}

	/* FIXME: This code should be moved into the core,
	 * and should only be called if we don't already have
	 * control of the bus.
	 *
	 * We can't call "pvr2_dvb_bus_ctrl(adap->fe, 1)" from here,
	 * because adap->fe isn't defined yet.
	 */
	ret = pvr2_ctrl_set_value(pvr2_hdw_get_ctrl_by_id(hdw,
							  PVR2_CID_INPUT),
				  PVR2_CVAL_INPUT_DTV);
	if (ret != 0)
		return ret;

	pvr2_hdw_commit_ctl(hdw);


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
	mutex_init(&pvr->hdw->dvb.lock);

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
