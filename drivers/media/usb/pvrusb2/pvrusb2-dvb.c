// SPDX-License-Identifier: GPL-2.0-only
/*
 *  pvrusb2-dvb.c - linux-dvb api interface to the pvrusb2 driver.
 *
 *  Copyright (C) 2007, 2008 Michael Krufky <mkrufky@linuxtv.org>
 */

#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <media/dvbdev.h>
#include "pvrusb2-debug.h"
#include "pvrusb2-hdw-internal.h"
#include "pvrusb2-hdw.h"
#include "pvrusb2-io.h"
#include "pvrusb2-dvb.h"

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static int pvr2_dvb_feed_func(struct pvr2_dvb_adapter *adap)
{
	int ret;
	unsigned int count;
	struct pvr2_buffer *bp;
	struct pvr2_stream *stream;

	pvr2_trace(PVR2_TRACE_DVB_FEED, "dvb feed thread started");
	set_freezable();

	stream = adap->channel.stream->stream;

	for (;;) {
		if (kthread_should_stop()) break;

		bp = pvr2_stream_get_ready_buffer(stream);
		if (bp != NULL) {
			count = pvr2_buffer_get_count(bp);
			if (count) {
				dvb_dmx_swfilter(
					&adap->demux,
					adap->buffer_storage[
					    pvr2_buffer_get_id(bp)],
					count);
			} else {
				ret = pvr2_buffer_get_status(bp);
				if (ret < 0) break;
			}
			ret = pvr2_buffer_queue(bp);
			if (ret < 0) break;

			/* Since we know we did something to a buffer,
			   just go back and try again.  No point in
			   blocking unless we really ran out of
			   buffers to process. */
			continue;
		}


		/* Wait until more buffers become available or we're
		   told not to wait any longer. */
		ret = wait_event_freezable(adap->buffer_wait_data,
		    (pvr2_stream_get_ready_count(stream) > 0) ||
		    kthread_should_stop());
		if (ret < 0) break;
	}

	/* If we get here and ret is < 0, then an error has occurred.
	   Probably would be a good idea to communicate that to DVB core... */

	pvr2_trace(PVR2_TRACE_DVB_FEED, "dvb feed thread stopped");

	return 0;
}

static int pvr2_dvb_feed_thread(void *data)
{
	int stat = pvr2_dvb_feed_func(data);

	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
	}
	return stat;
}

static void pvr2_dvb_notify(void *ptr)
{
	struct pvr2_dvb_adapter *adap = ptr;

	wake_up(&adap->buffer_wait_data);
}

static void pvr2_dvb_stream_end(struct pvr2_dvb_adapter *adap)
{
	unsigned int idx;
	struct pvr2_stream *stream;

	if (adap->thread) {
		kthread_stop(adap->thread);
		adap->thread = NULL;
	}

	if (adap->channel.stream) {
		stream = adap->channel.stream->stream;
	} else {
		stream = NULL;
	}
	if (stream) {
		pvr2_hdw_set_streaming(adap->channel.hdw, 0);
		pvr2_stream_set_callback(stream, NULL, NULL);
		pvr2_stream_kill(stream);
		pvr2_stream_set_buffer_count(stream, 0);
		pvr2_channel_claim_stream(&adap->channel, NULL);
	}

	if (adap->stream_run) {
		for (idx = 0; idx < PVR2_DVB_BUFFER_COUNT; idx++) {
			if (!(adap->buffer_storage[idx])) continue;
			kfree(adap->buffer_storage[idx]);
			adap->buffer_storage[idx] = NULL;
		}
		adap->stream_run = 0;
	}
}

static int pvr2_dvb_stream_do_start(struct pvr2_dvb_adapter *adap)
{
	struct pvr2_context *pvr = adap->channel.mc_head;
	unsigned int idx;
	int ret;
	struct pvr2_buffer *bp;
	struct pvr2_stream *stream = NULL;

	if (adap->stream_run) return -EIO;

	ret = pvr2_channel_claim_stream(&adap->channel, &pvr->video_stream);
	/* somebody else already has the stream */
	if (ret < 0) return ret;

	stream = adap->channel.stream->stream;

	for (idx = 0; idx < PVR2_DVB_BUFFER_COUNT; idx++) {
		adap->buffer_storage[idx] = kmalloc(PVR2_DVB_BUFFER_SIZE,
						    GFP_KERNEL);
		if (!(adap->buffer_storage[idx])) return -ENOMEM;
	}

	pvr2_stream_set_callback(pvr->video_stream.stream,
				 pvr2_dvb_notify, adap);

	ret = pvr2_stream_set_buffer_count(stream, PVR2_DVB_BUFFER_COUNT);
	if (ret < 0) return ret;

	for (idx = 0; idx < PVR2_DVB_BUFFER_COUNT; idx++) {
		bp = pvr2_stream_get_buffer(stream, idx);
		pvr2_buffer_set_buffer(bp,
				       adap->buffer_storage[idx],
				       PVR2_DVB_BUFFER_SIZE);
	}

	ret = pvr2_hdw_set_streaming(adap->channel.hdw, 1);
	if (ret < 0) return ret;

	while ((bp = pvr2_stream_get_idle_buffer(stream)) != NULL) {
		ret = pvr2_buffer_queue(bp);
		if (ret < 0) return ret;
	}

	adap->thread = kthread_run(pvr2_dvb_feed_thread, adap, "pvrusb2-dvb");

	if (IS_ERR(adap->thread)) {
		ret = PTR_ERR(adap->thread);
		adap->thread = NULL;
		return ret;
	}

	adap->stream_run = !0;

	return 0;
}

static int pvr2_dvb_stream_start(struct pvr2_dvb_adapter *adap)
{
	int ret = pvr2_dvb_stream_do_start(adap);
	if (ret < 0) pvr2_dvb_stream_end(adap);
	return ret;
}

static int pvr2_dvb_ctrl_feed(struct dvb_demux_feed *dvbdmxfeed, int onoff)
{
	struct pvr2_dvb_adapter *adap = dvbdmxfeed->demux->priv;
	int ret = 0;

	if (adap == NULL) return -ENODEV;

	mutex_lock(&adap->lock);
	do {
		if (onoff) {
			if (!adap->feedcount) {
				pvr2_trace(PVR2_TRACE_DVB_FEED,
					   "start feeding demux");
				ret = pvr2_dvb_stream_start(adap);
				if (ret < 0) break;
			}
			(adap->feedcount)++;
		} else if (adap->feedcount > 0) {
			(adap->feedcount)--;
			if (!adap->feedcount) {
				pvr2_trace(PVR2_TRACE_DVB_FEED,
					   "stop feeding demux");
				pvr2_dvb_stream_end(adap);
			}
		}
	} while (0);
	mutex_unlock(&adap->lock);

	return ret;
}

static int pvr2_dvb_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	pvr2_trace(PVR2_TRACE_DVB_FEED, "start pid: 0x%04x", dvbdmxfeed->pid);
	return pvr2_dvb_ctrl_feed(dvbdmxfeed, 1);
}

static int pvr2_dvb_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	pvr2_trace(PVR2_TRACE_DVB_FEED, "stop pid: 0x%04x", dvbdmxfeed->pid);
	return pvr2_dvb_ctrl_feed(dvbdmxfeed, 0);
}

static int pvr2_dvb_bus_ctrl(struct dvb_frontend *fe, int acquire)
{
	struct pvr2_dvb_adapter *adap = fe->dvb->priv;
	return pvr2_channel_limit_inputs(
	    &adap->channel,
	    (acquire ? (1 << PVR2_CVAL_INPUT_DTV) : 0));
}

static int pvr2_dvb_adapter_init(struct pvr2_dvb_adapter *adap)
{
	int ret;

	ret = dvb_register_adapter(&adap->dvb_adap, "pvrusb2-dvb",
				   THIS_MODULE/*&hdw->usb_dev->owner*/,
				   &adap->channel.hdw->usb_dev->dev,
				   adapter_nr);
	if (ret < 0) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "dvb_register_adapter failed: error %d", ret);
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
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "dvb_dmx_init failed: error %d", ret);
		goto err_dmx;
	}

	adap->dmxdev.filternum       = adap->demux.filternum;
	adap->dmxdev.demux           = &adap->demux.dmx;
	adap->dmxdev.capabilities    = 0;

	ret = dvb_dmxdev_init(&adap->dmxdev, &adap->dvb_adap);
	if (ret < 0) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "dvb_dmxdev_init failed: error %d", ret);
		goto err_dmx_dev;
	}

	dvb_net_init(&adap->dvb_adap, &adap->dvb_net, &adap->demux.dmx);

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
	pvr2_trace(PVR2_TRACE_INFO, "unregistering DVB devices");
	dvb_net_release(&adap->dvb_net);
	adap->demux.dmx.close(&adap->demux.dmx);
	dvb_dmxdev_release(&adap->dmxdev);
	dvb_dmx_release(&adap->demux);
	dvb_unregister_adapter(&adap->dvb_adap);
	return 0;
}

static int pvr2_dvb_frontend_init(struct pvr2_dvb_adapter *adap)
{
	struct pvr2_hdw *hdw = adap->channel.hdw;
	const struct pvr2_dvb_props *dvb_props = hdw->hdw_desc->dvb_props;
	int ret = 0;

	if (dvb_props == NULL) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS, "fe_props not defined!");
		return -EINVAL;
	}

	ret = pvr2_channel_limit_inputs(
	    &adap->channel,
	    (1 << PVR2_CVAL_INPUT_DTV));
	if (ret) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "failed to grab control of dtv input (code=%d)",
		    ret);
		return ret;
	}

	if (dvb_props->frontend_attach == NULL) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "frontend_attach not defined!");
		ret = -EINVAL;
		goto done;
	}

	if (dvb_props->frontend_attach(adap) == 0 && adap->fe[0]) {
		if (dvb_register_frontend(&adap->dvb_adap, adap->fe[0])) {
			pvr2_trace(PVR2_TRACE_ERROR_LEGS,
				   "frontend registration failed!");
			ret = -ENODEV;
			goto fail_frontend0;
		}
		if (adap->fe[0]->ops.analog_ops.standby)
			adap->fe[0]->ops.analog_ops.standby(adap->fe[0]);

		pvr2_trace(PVR2_TRACE_INFO, "transferring fe[%d] ts_bus_ctrl() to pvr2_dvb_bus_ctrl()",
			   adap->fe[0]->id);
		adap->fe[0]->ops.ts_bus_ctrl = pvr2_dvb_bus_ctrl;
	} else {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS,
			   "no frontend was attached!");
		ret = -ENODEV;
		return ret;
	}

	if (dvb_props->tuner_attach && dvb_props->tuner_attach(adap)) {
		pvr2_trace(PVR2_TRACE_ERROR_LEGS, "tuner attach failed");
		ret = -ENODEV;
		goto fail_tuner;
	}

	if (adap->fe[1]) {
		adap->fe[1]->id = 1;
		adap->fe[1]->tuner_priv = adap->fe[0]->tuner_priv;
		memcpy(&adap->fe[1]->ops.tuner_ops,
		       &adap->fe[0]->ops.tuner_ops,
		       sizeof(struct dvb_tuner_ops));

		if (dvb_register_frontend(&adap->dvb_adap, adap->fe[1])) {
			pvr2_trace(PVR2_TRACE_ERROR_LEGS,
				   "frontend registration failed!");
			ret = -ENODEV;
			goto fail_frontend1;
		}
		/* MFE lock */
		adap->dvb_adap.mfe_shared = 1;

		if (adap->fe[1]->ops.analog_ops.standby)
			adap->fe[1]->ops.analog_ops.standby(adap->fe[1]);

		pvr2_trace(PVR2_TRACE_INFO, "transferring fe[%d] ts_bus_ctrl() to pvr2_dvb_bus_ctrl()",
			   adap->fe[1]->id);
		adap->fe[1]->ops.ts_bus_ctrl = pvr2_dvb_bus_ctrl;
	}
done:
	pvr2_channel_limit_inputs(&adap->channel, 0);
	return ret;

fail_frontend1:
	dvb_frontend_detach(adap->fe[1]);
	adap->fe[1] = NULL;
fail_tuner:
	dvb_unregister_frontend(adap->fe[0]);
fail_frontend0:
	dvb_frontend_detach(adap->fe[0]);
	adap->fe[0] = NULL;
	dvb_module_release(adap->i2c_client_tuner);
	dvb_module_release(adap->i2c_client_demod[1]);
	dvb_module_release(adap->i2c_client_demod[0]);

	return ret;
}

static int pvr2_dvb_frontend_exit(struct pvr2_dvb_adapter *adap)
{
	if (adap->fe[1]) {
		dvb_unregister_frontend(adap->fe[1]);
		dvb_frontend_detach(adap->fe[1]);
		adap->fe[1] = NULL;
	}
	if (adap->fe[0]) {
		dvb_unregister_frontend(adap->fe[0]);
		dvb_frontend_detach(adap->fe[0]);
		adap->fe[0] = NULL;
	}

	dvb_module_release(adap->i2c_client_tuner);
	adap->i2c_client_tuner = NULL;
	dvb_module_release(adap->i2c_client_demod[1]);
	adap->i2c_client_demod[1] = NULL;
	dvb_module_release(adap->i2c_client_demod[0]);
	adap->i2c_client_demod[0] = NULL;

	return 0;
}

static void pvr2_dvb_destroy(struct pvr2_dvb_adapter *adap)
{
	pvr2_dvb_stream_end(adap);
	pvr2_dvb_frontend_exit(adap);
	pvr2_dvb_adapter_exit(adap);
	pvr2_channel_done(&adap->channel);
	kfree(adap);
}

static void pvr2_dvb_internal_check(struct pvr2_channel *chp)
{
	struct pvr2_dvb_adapter *adap;
	adap = container_of(chp, struct pvr2_dvb_adapter, channel);
	if (!adap->channel.mc_head->disconnect_flag) return;
	pvr2_dvb_destroy(adap);
}

struct pvr2_dvb_adapter *pvr2_dvb_create(struct pvr2_context *pvr)
{
	int ret = 0;
	struct pvr2_dvb_adapter *adap;
	if (!pvr->hdw->hdw_desc->dvb_props) {
		/* Device lacks a digital interface so don't set up
		   the DVB side of the driver either.  For now. */
		return NULL;
	}
	adap = kzalloc(sizeof(*adap), GFP_KERNEL);
	if (!adap) return adap;
	pvr2_channel_init(&adap->channel, pvr);
	adap->channel.check_func = pvr2_dvb_internal_check;
	init_waitqueue_head(&adap->buffer_wait_data);
	mutex_init(&adap->lock);
	ret = pvr2_dvb_adapter_init(adap);
	if (ret < 0) goto fail1;
	ret = pvr2_dvb_frontend_init(adap);
	if (ret < 0) goto fail2;
	return adap;

fail2:
	pvr2_dvb_adapter_exit(adap);
fail1:
	pvr2_channel_done(&adap->channel);
	return NULL;
}

