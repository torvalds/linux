// SPDX-License-Identifier: GPL-2.0-only
/*
 * vivid-kthread-touch.c - touch capture thread support functions.
 *
 */

#include <linux/freezer.h>
#include "vivid-core.h"
#include "vivid-kthread-touch.h"
#include "vivid-touch-cap.h"

static noinline_for_stack void vivid_thread_tch_cap_tick(struct vivid_dev *dev,
							 int dropped_bufs)
{
	struct vivid_buffer *tch_cap_buf = NULL;

	spin_lock(&dev->slock);
	if (!list_empty(&dev->touch_cap_active)) {
		tch_cap_buf = list_entry(dev->touch_cap_active.next,
					 struct vivid_buffer, list);
		list_del(&tch_cap_buf->list);
	}

	spin_unlock(&dev->slock);

	if (tch_cap_buf) {
		v4l2_ctrl_request_setup(tch_cap_buf->vb.vb2_buf.req_obj.req,
					&dev->ctrl_hdl_touch_cap);

		vivid_fillbuff_tch(dev, tch_cap_buf);
		v4l2_ctrl_request_complete(tch_cap_buf->vb.vb2_buf.req_obj.req,
					   &dev->ctrl_hdl_touch_cap);
		vb2_buffer_done(&tch_cap_buf->vb.vb2_buf, dev->dqbuf_error ?
				VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE);
		dprintk(dev, 2, "touch_cap buffer %d done\n",
			tch_cap_buf->vb.vb2_buf.index);

		tch_cap_buf->vb.vb2_buf.timestamp = ktime_get_ns() + dev->time_wrap_offset;
	}
	dev->dqbuf_error = false;
}

static int vivid_thread_touch_cap(void *data)
{
	struct vivid_dev *dev = data;
	u64 numerators_since_start;
	u64 buffers_since_start;
	u64 next_jiffies_since_start;
	unsigned long jiffies_since_start;
	unsigned long cur_jiffies;
	unsigned int wait_jiffies;
	unsigned int numerator;
	unsigned int denominator;
	int dropped_bufs;

	dprintk(dev, 1, "Touch Capture Thread Start\n");

	set_freezable();

	/* Resets frame counters */
	dev->touch_cap_seq_offset = 0;
	dev->touch_cap_seq_count = 0;
	dev->touch_cap_seq_resync = false;
	dev->jiffies_touch_cap = jiffies;

	for (;;) {
		try_to_freeze();
		if (kthread_should_stop())
			break;

		if (!mutex_trylock(&dev->mutex)) {
			schedule();
			continue;
		}
		cur_jiffies = jiffies;
		if (dev->touch_cap_seq_resync) {
			dev->jiffies_touch_cap = cur_jiffies;
			dev->touch_cap_seq_offset = dev->touch_cap_seq_count + 1;
			dev->touch_cap_seq_count = 0;
			dev->cap_seq_resync = false;
		}
		denominator = dev->timeperframe_tch_cap.denominator;
		numerator = dev->timeperframe_tch_cap.numerator;

		/* Calculate the number of jiffies since we started streaming */
		jiffies_since_start = cur_jiffies - dev->jiffies_touch_cap;
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
			dev->jiffies_touch_cap = cur_jiffies;
			dev->cap_seq_offset = buffers_since_start;
			buffers_since_start = 0;
		}
		dropped_bufs = buffers_since_start + dev->touch_cap_seq_offset - dev->touch_cap_seq_count;
		dev->touch_cap_seq_count = buffers_since_start + dev->touch_cap_seq_offset;

		vivid_thread_tch_cap_tick(dev, dropped_bufs);

		/*
		 * Calculate the number of 'numerators' streamed
		 * since we started, including the current buffer.
		 */
		numerators_since_start = ++buffers_since_start * numerator;

		/* And the number of jiffies since we started */
		jiffies_since_start = jiffies - dev->jiffies_touch_cap;

		mutex_unlock(&dev->mutex);

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
		while (jiffies - cur_jiffies < wait_jiffies &&
		       !kthread_should_stop())
			schedule();
	}
	dprintk(dev, 1, "Touch Capture Thread End\n");
	return 0;
}

int vivid_start_generating_touch_cap(struct vivid_dev *dev)
{
	if (dev->kthread_touch_cap) {
		dev->touch_cap_streaming = true;
		return 0;
	}

	dev->kthread_touch_cap = kthread_run(vivid_thread_touch_cap, dev,
					     "%s-tch-cap", dev->v4l2_dev.name);

	if (IS_ERR(dev->kthread_touch_cap)) {
		int err = PTR_ERR(dev->kthread_touch_cap);

		dev->kthread_touch_cap = NULL;
		v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
		return err;
	}
	dev->touch_cap_streaming = true;
	dprintk(dev, 1, "returning from %s\n", __func__);
	return 0;
}

void vivid_stop_generating_touch_cap(struct vivid_dev *dev)
{
	if (!dev->kthread_touch_cap)
		return;

	dev->touch_cap_streaming = false;

	while (!list_empty(&dev->touch_cap_active)) {
		struct vivid_buffer *buf;

		buf = list_entry(dev->touch_cap_active.next,
				 struct vivid_buffer, list);
		list_del(&buf->list);
		v4l2_ctrl_request_complete(buf->vb.vb2_buf.req_obj.req,
					   &dev->ctrl_hdl_touch_cap);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		dprintk(dev, 2, "touch_cap buffer %d done\n",
			buf->vb.vb2_buf.index);
	}

	kthread_stop(dev->kthread_touch_cap);
	dev->kthread_touch_cap = NULL;
}
