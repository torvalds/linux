/*
 *
 * (C) COPYRIGHT 2015-2020 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#include "mali_kbase_timeline.h"
#include "mali_kbase_timeline_priv.h"
#include "mali_kbase_tracepoints.h"

#include <mali_kbase.h>
#include <mali_kbase_jm.h>

#include <linux/anon_inodes.h>
#include <linux/atomic.h>
#include <linux/file.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/stringify.h>
#include <linux/timer.h>
#include <linux/wait.h>


/* The period of autoflush checker execution in milliseconds. */
#define AUTOFLUSH_INTERVAL 1000 /* ms */

/*****************************************************************************/

/* These values are used in mali_kbase_tracepoints.h
 * to retrieve the streams from a kbase_timeline instance.
 */
const size_t __obj_stream_offset =
	offsetof(struct kbase_timeline, streams)
	+ sizeof(struct kbase_tlstream) * TL_STREAM_TYPE_OBJ;

const size_t __aux_stream_offset =
	offsetof(struct kbase_timeline, streams)
	+ sizeof(struct kbase_tlstream) * TL_STREAM_TYPE_AUX;

/**
 * kbasep_timeline_autoflush_timer_callback - autoflush timer callback
 * @timer:  Timer list
 *
 * Timer is executed periodically to check if any of the stream contains
 * buffer ready to be submitted to user space.
 */
static void kbasep_timeline_autoflush_timer_callback(struct timer_list *timer)
{
	enum tl_stream_type stype;
	int                 rcode;
	struct kbase_timeline *timeline =
		container_of(timer, struct kbase_timeline, autoflush_timer);

	CSTD_UNUSED(timer);

	for (stype = (enum tl_stream_type)0; stype < TL_STREAM_TYPE_COUNT;
			stype++) {
		struct kbase_tlstream *stream = &timeline->streams[stype];

		int af_cnt = atomic_read(&stream->autoflush_counter);

		/* Check if stream contain unflushed data. */
		if (af_cnt < 0)
			continue;

		/* Check if stream should be flushed now. */
		if (af_cnt != atomic_cmpxchg(
					&stream->autoflush_counter,
					af_cnt,
					af_cnt + 1))
			continue;
		if (!af_cnt)
			continue;

		/* Autoflush this stream. */
		kbase_tlstream_flush_stream(stream);
	}

	if (atomic_read(&timeline->autoflush_timer_active))
		rcode = mod_timer(
				&timeline->autoflush_timer,
				jiffies + msecs_to_jiffies(AUTOFLUSH_INTERVAL));
	CSTD_UNUSED(rcode);
}



/*****************************************************************************/

int kbase_timeline_init(struct kbase_timeline **timeline,
		atomic_t *timeline_flags)
{
	enum tl_stream_type i;
	struct kbase_timeline *result;
#if MALI_USE_CSF
	struct kbase_tlstream *csffw_stream;
#endif

	if (!timeline || !timeline_flags)
		return -EINVAL;

	result = kzalloc(sizeof(*result), GFP_KERNEL);
	if (!result)
		return -ENOMEM;

	mutex_init(&result->reader_lock);
	init_waitqueue_head(&result->event_queue);

	/* Prepare stream structures. */
	for (i = 0; i < TL_STREAM_TYPE_COUNT; i++)
		kbase_tlstream_init(&result->streams[i], i,
			&result->event_queue);

	/* Initialize autoflush timer. */
	atomic_set(&result->autoflush_timer_active, 0);
	kbase_timer_setup(&result->autoflush_timer,
			  kbasep_timeline_autoflush_timer_callback);
	result->timeline_flags = timeline_flags;

#if MALI_USE_CSF
	csffw_stream = &result->streams[TL_STREAM_TYPE_CSFFW];
	kbase_csf_tl_reader_init(&result->csf_tl_reader, csffw_stream);
#endif

	*timeline = result;
	return 0;
}

void kbase_timeline_term(struct kbase_timeline *timeline)
{
	enum tl_stream_type i;

	if (!timeline)
		return;

#if MALI_USE_CSF
	kbase_csf_tl_reader_term(&timeline->csf_tl_reader);
#endif

	for (i = (enum tl_stream_type)0; i < TL_STREAM_TYPE_COUNT; i++)
		kbase_tlstream_term(&timeline->streams[i]);

	kfree(timeline);
}

#ifdef CONFIG_MALI_BIFROST_DEVFREQ
static void kbase_tlstream_current_devfreq_target(struct kbase_device *kbdev)
{
	struct devfreq *devfreq = kbdev->devfreq;

	/* Devfreq initialization failure isn't a fatal error, so devfreq might
	 * be null.
	 */
	if (devfreq) {
		unsigned long cur_freq = 0;

		mutex_lock(&devfreq->lock);
#if KERNEL_VERSION(4, 3, 0) > LINUX_VERSION_CODE
		cur_freq = kbdev->current_nominal_freq;
#else
		cur_freq = devfreq->last_status.current_frequency;
#endif
		KBASE_TLSTREAM_AUX_DEVFREQ_TARGET(kbdev, (u64)cur_freq);
		mutex_unlock(&devfreq->lock);
	}
}
#endif /* CONFIG_MALI_BIFROST_DEVFREQ */

int kbase_timeline_io_acquire(struct kbase_device *kbdev, u32 flags)
{
	int ret;
	u32 timeline_flags = TLSTREAM_ENABLED | flags;
	struct kbase_timeline *timeline = kbdev->timeline;

	if (!atomic_cmpxchg(timeline->timeline_flags, 0, timeline_flags)) {
		int rcode;

#if MALI_USE_CSF
		if (flags & BASE_TLSTREAM_ENABLE_CSFFW_TRACEPOINTS) {
			ret = kbase_csf_tl_reader_start(
				&timeline->csf_tl_reader, kbdev);
			if (ret)
			{
				atomic_set(timeline->timeline_flags, 0);
				return ret;
			}
		}
#endif
		ret = anon_inode_getfd(
				"[mali_tlstream]",
				&kbasep_tlstream_fops,
				timeline,
				O_RDONLY | O_CLOEXEC);
		if (ret < 0) {
			atomic_set(timeline->timeline_flags, 0);
#if MALI_USE_CSF
			kbase_csf_tl_reader_stop(&timeline->csf_tl_reader);
#endif
			return ret;
		}

		/* Reset and initialize header streams. */
		kbase_tlstream_reset(
			&timeline->streams[TL_STREAM_TYPE_OBJ_SUMMARY]);

		timeline->obj_header_btc = obj_desc_header_size;
		timeline->aux_header_btc = aux_desc_header_size;

		/* Start autoflush timer. */
		atomic_set(&timeline->autoflush_timer_active, 1);
		rcode = mod_timer(
				&timeline->autoflush_timer,
				jiffies + msecs_to_jiffies(AUTOFLUSH_INTERVAL));
		CSTD_UNUSED(rcode);

#if !MALI_USE_CSF
		/* If job dumping is enabled, readjust the software event's
		 * timeout as the default value of 3 seconds is often
		 * insufficient.
		 */
		if (flags & BASE_TLSTREAM_JOB_DUMPING_ENABLED) {
			dev_info(kbdev->dev,
					"Job dumping is enabled, readjusting the software event's timeout\n");
			atomic_set(&kbdev->js_data.soft_job_timeout_ms,
					1800000);
		}
#endif /* !MALI_USE_CSF */

		/* Summary stream was cleared during acquire.
		 * Create static timeline objects that will be
		 * read by client.
		 */
		kbase_create_timeline_objects(kbdev);

#ifdef CONFIG_MALI_BIFROST_DEVFREQ
		/* Devfreq target tracepoints are only fired when the target
		 * changes, so we won't know the current target unless we
		 * send it now.
		 */
		kbase_tlstream_current_devfreq_target(kbdev);
#endif /* CONFIG_MALI_BIFROST_DEVFREQ */

	} else {
		ret = -EBUSY;
	}

	return ret;
}

void kbase_timeline_streams_flush(struct kbase_timeline *timeline)
{
	enum tl_stream_type stype;

#if MALI_USE_CSF
	kbase_csf_tl_reader_flush_buffer(&timeline->csf_tl_reader);
#endif

	for (stype = 0; stype < TL_STREAM_TYPE_COUNT; stype++)
		kbase_tlstream_flush_stream(&timeline->streams[stype]);
}

void kbase_timeline_streams_body_reset(struct kbase_timeline *timeline)
{
	kbase_tlstream_reset(
			&timeline->streams[TL_STREAM_TYPE_OBJ]);
	kbase_tlstream_reset(
			&timeline->streams[TL_STREAM_TYPE_AUX]);
#if MALI_USE_CSF
	kbase_tlstream_reset(
			&timeline->streams[TL_STREAM_TYPE_CSFFW]);
#endif
}

#if MALI_UNIT_TEST
void kbase_timeline_stats(struct kbase_timeline *timeline,
		u32 *bytes_collected, u32 *bytes_generated)
{
	enum tl_stream_type stype;

	KBASE_DEBUG_ASSERT(bytes_collected);

	/* Accumulate bytes generated per stream  */
	*bytes_generated = 0;
	for (stype = (enum tl_stream_type)0; stype < TL_STREAM_TYPE_COUNT;
			stype++)
		*bytes_generated += atomic_read(
			&timeline->streams[stype].bytes_generated);

	*bytes_collected = atomic_read(&timeline->bytes_collected);
}
#endif /* MALI_UNIT_TEST */
