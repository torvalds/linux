// SPDX-License-Identifier: GPL-2.0-only
/*
 * vivid-kthread-out.h - video/vbi output thread support functions.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/font.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/random.h>
#include <linux/v4l2-dv-timings.h>
#include <asm/div64.h>
#include <media/videobuf2-vmalloc.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-event.h>

#include "vivid-core.h"
#include "vivid-vid-common.h"
#include "vivid-vid-cap.h"
#include "vivid-vid-out.h"
#include "vivid-radio-common.h"
#include "vivid-radio-rx.h"
#include "vivid-radio-tx.h"
#include "vivid-sdr-cap.h"
#include "vivid-vbi-cap.h"
#include "vivid-vbi-out.h"
#include "vivid-osd.h"
#include "vivid-ctrls.h"
#include "vivid-kthread-out.h"
#include "vivid-meta-out.h"

static void vivid_thread_vid_out_tick(struct vivid_dev *dev)
{
	struct vivid_buffer *vid_out_buf = NULL;
	struct vivid_buffer *vbi_out_buf = NULL;
	struct vivid_buffer *meta_out_buf = NULL;

	dprintk(dev, 1, "Video Output Thread Tick\n");

	/* Drop a certain percentage of buffers. */
	if (dev->perc_dropped_buffers &&
	    prandom_u32_max(100) < dev->perc_dropped_buffers)
		return;

	spin_lock(&dev->slock);
	/*
	 * Only dequeue buffer if there is at least one more pending.
	 * This makes video loopback possible.
	 */
	if (!list_empty(&dev->vid_out_active) &&
	    !list_is_singular(&dev->vid_out_active)) {
		vid_out_buf = list_entry(dev->vid_out_active.next,
					 struct vivid_buffer, list);
		list_del(&vid_out_buf->list);
	}
	if (!list_empty(&dev->vbi_out_active) &&
	    (dev->field_out != V4L2_FIELD_ALTERNATE ||
	     (dev->vbi_out_seq_count & 1))) {
		vbi_out_buf = list_entry(dev->vbi_out_active.next,
					 struct vivid_buffer, list);
		list_del(&vbi_out_buf->list);
	}
	if (!list_empty(&dev->meta_out_active)) {
		meta_out_buf = list_entry(dev->meta_out_active.next,
					  struct vivid_buffer, list);
		list_del(&meta_out_buf->list);
	}
	spin_unlock(&dev->slock);

	if (!vid_out_buf && !vbi_out_buf && !meta_out_buf)
		return;

	if (vid_out_buf) {
		v4l2_ctrl_request_setup(vid_out_buf->vb.vb2_buf.req_obj.req,
					&dev->ctrl_hdl_vid_out);
		v4l2_ctrl_request_complete(vid_out_buf->vb.vb2_buf.req_obj.req,
					   &dev->ctrl_hdl_vid_out);
		vid_out_buf->vb.sequence = dev->vid_out_seq_count;
		if (dev->field_out == V4L2_FIELD_ALTERNATE) {
			/*
			 * The sequence counter counts frames, not fields.
			 * So divide by two.
			 */
			vid_out_buf->vb.sequence /= 2;
		}
		vid_out_buf->vb.vb2_buf.timestamp =
			ktime_get_ns() + dev->time_wrap_offset;
		vb2_buffer_done(&vid_out_buf->vb.vb2_buf, dev->dqbuf_error ?
				VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE);
		dprintk(dev, 2, "vid_out buffer %d done\n",
			vid_out_buf->vb.vb2_buf.index);
	}

	if (vbi_out_buf) {
		v4l2_ctrl_request_setup(vbi_out_buf->vb.vb2_buf.req_obj.req,
					&dev->ctrl_hdl_vbi_out);
		v4l2_ctrl_request_complete(vbi_out_buf->vb.vb2_buf.req_obj.req,
					   &dev->ctrl_hdl_vbi_out);
		if (dev->stream_sliced_vbi_out)
			vivid_sliced_vbi_out_process(dev, vbi_out_buf);

		vbi_out_buf->vb.sequence = dev->vbi_out_seq_count;
		vbi_out_buf->vb.vb2_buf.timestamp =
			ktime_get_ns() + dev->time_wrap_offset;
		vb2_buffer_done(&vbi_out_buf->vb.vb2_buf, dev->dqbuf_error ?
				VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE);
		dprintk(dev, 2, "vbi_out buffer %d done\n",
			vbi_out_buf->vb.vb2_buf.index);
	}
	if (meta_out_buf) {
		v4l2_ctrl_request_setup(meta_out_buf->vb.vb2_buf.req_obj.req,
					&dev->ctrl_hdl_meta_out);
		v4l2_ctrl_request_complete(meta_out_buf->vb.vb2_buf.req_obj.req,
					   &dev->ctrl_hdl_meta_out);
		vivid_meta_out_process(dev, meta_out_buf);
		meta_out_buf->vb.sequence = dev->meta_out_seq_count;
		meta_out_buf->vb.vb2_buf.timestamp =
			ktime_get_ns() + dev->time_wrap_offset;
		vb2_buffer_done(&meta_out_buf->vb.vb2_buf, dev->dqbuf_error ?
				VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE);
		dprintk(dev, 2, "meta_out buffer %d done\n",
			meta_out_buf->vb.vb2_buf.index);
	}

	dev->dqbuf_error = false;
}

static int vivid_thread_vid_out(void *data)
{
	struct vivid_dev *dev = data;
	u64 numerators_since_start;
	u64 buffers_since_start;
	u64 next_jiffies_since_start;
	unsigned long jiffies_since_start;
	unsigned long cur_jiffies;
	unsigned wait_jiffies;
	unsigned numerator;
	unsigned denominator;

	dprintk(dev, 1, "Video Output Thread Start\n");

	set_freezable();

	/* Resets frame counters */
	dev->out_seq_offset = 0;
	if (dev->seq_wrap)
		dev->out_seq_count = 0xffffff80U;
	dev->jiffies_vid_out = jiffies;
	dev->vid_out_seq_start = dev->vbi_out_seq_start = 0;
	dev->meta_out_seq_start = 0;
	dev->out_seq_resync = false;

	for (;;) {
		try_to_freeze();
		if (kthread_should_stop())
			break;

		if (!mutex_trylock(&dev->mutex)) {
			schedule_timeout_uninterruptible(1);
			continue;
		}

		cur_jiffies = jiffies;
		if (dev->out_seq_resync) {
			dev->jiffies_vid_out = cur_jiffies;
			dev->out_seq_offset = dev->out_seq_count + 1;
			dev->out_seq_count = 0;
			dev->out_seq_resync = false;
		}
		numerator = dev->timeperframe_vid_out.numerator;
		denominator = dev->timeperframe_vid_out.denominator;

		if (dev->field_out == V4L2_FIELD_ALTERNATE)
			denominator *= 2;

		/* Calculate the number of jiffies since we started streaming */
		jiffies_since_start = cur_jiffies - dev->jiffies_vid_out;
		/* Get the number of buffers streamed since the start */
		buffers_since_start = (u64)jiffies_since_start * denominator +
				      (HZ * numerator) / 2;
		do_div(buffers_since_start, HZ * numerator);

		/*
		 * After more than 0xf0000000 (rounded down to a multiple of
		 * 'jiffies-per-day' to ease jiffies_to_msecs calculation)
		 * jiffies have passed since we started streaming reset the
		 * counters and keep track of the sequence offset.
		 */
		if (jiffies_since_start > JIFFIES_RESYNC) {
			dev->jiffies_vid_out = cur_jiffies;
			dev->out_seq_offset = buffers_since_start;
			buffers_since_start = 0;
		}
		dev->out_seq_count = buffers_since_start + dev->out_seq_offset;
		dev->vid_out_seq_count = dev->out_seq_count - dev->vid_out_seq_start;
		dev->vbi_out_seq_count = dev->out_seq_count - dev->vbi_out_seq_start;
		dev->meta_out_seq_count = dev->out_seq_count - dev->meta_out_seq_start;

		vivid_thread_vid_out_tick(dev);
		mutex_unlock(&dev->mutex);

		/*
		 * Calculate the number of 'numerators' streamed since we started,
		 * not including the current buffer.
		 */
		numerators_since_start = buffers_since_start * numerator;

		/* And the number of jiffies since we started */
		jiffies_since_start = jiffies - dev->jiffies_vid_out;

		/* Increase by the 'numerator' of one buffer */
		numerators_since_start += numerator;
		/*
		 * Calculate when that next buffer is supposed to start
		 * in jiffies since we started streaming.
		 */
		next_jiffies_since_start = numerators_since_start * HZ +
					   denominator / 2;
		do_div(next_jiffies_since_start, denominator);
		/* If it is in the past, then just schedule asap */
		if (next_jiffies_since_start < jiffies_since_start)
			next_jiffies_since_start = jiffies_since_start;

		wait_jiffies = next_jiffies_since_start - jiffies_since_start;
		schedule_timeout_interruptible(wait_jiffies ? wait_jiffies : 1);
	}
	dprintk(dev, 1, "Video Output Thread End\n");
	return 0;
}

static void vivid_grab_controls(struct vivid_dev *dev, bool grab)
{
	v4l2_ctrl_grab(dev->ctrl_has_crop_out, grab);
	v4l2_ctrl_grab(dev->ctrl_has_compose_out, grab);
	v4l2_ctrl_grab(dev->ctrl_has_scaler_out, grab);
	v4l2_ctrl_grab(dev->ctrl_tx_mode, grab);
	v4l2_ctrl_grab(dev->ctrl_tx_rgb_range, grab);
}

int vivid_start_generating_vid_out(struct vivid_dev *dev, bool *pstreaming)
{
	dprintk(dev, 1, "%s\n", __func__);

	if (dev->kthread_vid_out) {
		u32 seq_count = dev->out_seq_count + dev->seq_wrap * 128;

		if (pstreaming == &dev->vid_out_streaming)
			dev->vid_out_seq_start = seq_count;
		else if (pstreaming == &dev->vbi_out_streaming)
			dev->vbi_out_seq_start = seq_count;
		else
			dev->meta_out_seq_start = seq_count;
		*pstreaming = true;
		return 0;
	}

	/* Resets frame counters */
	dev->jiffies_vid_out = jiffies;
	dev->vid_out_seq_start = dev->seq_wrap * 128;
	dev->vbi_out_seq_start = dev->seq_wrap * 128;
	dev->meta_out_seq_start = dev->seq_wrap * 128;

	dev->kthread_vid_out = kthread_run(vivid_thread_vid_out, dev,
			"%s-vid-out", dev->v4l2_dev.name);

	if (IS_ERR(dev->kthread_vid_out)) {
		int err = PTR_ERR(dev->kthread_vid_out);

		dev->kthread_vid_out = NULL;
		v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
		return err;
	}
	*pstreaming = true;
	vivid_grab_controls(dev, true);

	dprintk(dev, 1, "returning from %s\n", __func__);
	return 0;
}

void vivid_stop_generating_vid_out(struct vivid_dev *dev, bool *pstreaming)
{
	dprintk(dev, 1, "%s\n", __func__);

	if (dev->kthread_vid_out == NULL)
		return;

	*pstreaming = false;
	if (pstreaming == &dev->vid_out_streaming) {
		/* Release all active buffers */
		while (!list_empty(&dev->vid_out_active)) {
			struct vivid_buffer *buf;

			buf = list_entry(dev->vid_out_active.next,
					 struct vivid_buffer, list);
			list_del(&buf->list);
			v4l2_ctrl_request_complete(buf->vb.vb2_buf.req_obj.req,
						   &dev->ctrl_hdl_vid_out);
			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
			dprintk(dev, 2, "vid_out buffer %d done\n",
				buf->vb.vb2_buf.index);
		}
	}

	if (pstreaming == &dev->vbi_out_streaming) {
		while (!list_empty(&dev->vbi_out_active)) {
			struct vivid_buffer *buf;

			buf = list_entry(dev->vbi_out_active.next,
					 struct vivid_buffer, list);
			list_del(&buf->list);
			v4l2_ctrl_request_complete(buf->vb.vb2_buf.req_obj.req,
						   &dev->ctrl_hdl_vbi_out);
			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
			dprintk(dev, 2, "vbi_out buffer %d done\n",
				buf->vb.vb2_buf.index);
		}
	}

	if (pstreaming == &dev->meta_out_streaming) {
		while (!list_empty(&dev->meta_out_active)) {
			struct vivid_buffer *buf;

			buf = list_entry(dev->meta_out_active.next,
					 struct vivid_buffer, list);
			list_del(&buf->list);
			v4l2_ctrl_request_complete(buf->vb.vb2_buf.req_obj.req,
						   &dev->ctrl_hdl_meta_out);
			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
			dprintk(dev, 2, "meta_out buffer %d done\n",
				buf->vb.vb2_buf.index);
		}
	}

	if (dev->vid_out_streaming || dev->vbi_out_streaming ||
	    dev->meta_out_streaming)
		return;

	/* shutdown control thread */
	vivid_grab_controls(dev, false);
	kthread_stop(dev->kthread_vid_out);
	dev->kthread_vid_out = NULL;
}
