/*
 * Copyright © 2014-2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include <linux/debugfs.h>

#include "intel_guc_log.h"
#include "i915_drv.h"

static void guc_log_capture_logs(struct intel_guc_log *log);

/**
 * DOC: GuC firmware log
 *
 * Firmware log is enabled by setting i915.guc_log_level to the positive level.
 * Log data is printed out via reading debugfs i915_guc_log_dump. Reading from
 * i915_guc_load_status will print out firmware loading status and scratch
 * registers value.
 */

static int guc_action_flush_log_complete(struct intel_guc *guc)
{
	u32 action[] = {
		INTEL_GUC_ACTION_LOG_BUFFER_FILE_FLUSH_COMPLETE
	};

	return intel_guc_send(guc, action, ARRAY_SIZE(action));
}

static int guc_action_flush_log(struct intel_guc *guc)
{
	u32 action[] = {
		INTEL_GUC_ACTION_FORCE_LOG_BUFFER_FLUSH,
		0
	};

	return intel_guc_send(guc, action, ARRAY_SIZE(action));
}

static int guc_action_control_log(struct intel_guc *guc, bool enable,
				  bool default_logging, u32 verbosity)
{
	u32 action[] = {
		INTEL_GUC_ACTION_UK_LOG_ENABLE_LOGGING,
		(enable ? GUC_LOG_CONTROL_LOGGING_ENABLED : 0) |
		(verbosity << GUC_LOG_CONTROL_VERBOSITY_SHIFT) |
		(default_logging ? GUC_LOG_CONTROL_DEFAULT_LOGGING : 0)
	};

	GEM_BUG_ON(verbosity > GUC_LOG_VERBOSITY_MAX);

	return intel_guc_send(guc, action, ARRAY_SIZE(action));
}

static inline struct intel_guc *log_to_guc(struct intel_guc_log *log)
{
	return container_of(log, struct intel_guc, log);
}

static void guc_log_enable_flush_events(struct intel_guc_log *log)
{
	intel_guc_enable_msg(log_to_guc(log),
			     INTEL_GUC_RECV_MSG_FLUSH_LOG_BUFFER |
			     INTEL_GUC_RECV_MSG_CRASH_DUMP_POSTED);
}

static void guc_log_disable_flush_events(struct intel_guc_log *log)
{
	intel_guc_disable_msg(log_to_guc(log),
			      INTEL_GUC_RECV_MSG_FLUSH_LOG_BUFFER |
			      INTEL_GUC_RECV_MSG_CRASH_DUMP_POSTED);
}

/*
 * Sub buffer switch callback. Called whenever relay has to switch to a new
 * sub buffer, relay stays on the same sub buffer if 0 is returned.
 */
static int subbuf_start_callback(struct rchan_buf *buf,
				 void *subbuf,
				 void *prev_subbuf,
				 size_t prev_padding)
{
	/*
	 * Use no-overwrite mode by default, where relay will stop accepting
	 * new data if there are no empty sub buffers left.
	 * There is no strict synchronization enforced by relay between Consumer
	 * and Producer. In overwrite mode, there is a possibility of getting
	 * inconsistent/garbled data, the producer could be writing on to the
	 * same sub buffer from which Consumer is reading. This can't be avoided
	 * unless Consumer is fast enough and can always run in tandem with
	 * Producer.
	 */
	if (relay_buf_full(buf))
		return 0;

	return 1;
}

/*
 * file_create() callback. Creates relay file in debugfs.
 */
static struct dentry *create_buf_file_callback(const char *filename,
					       struct dentry *parent,
					       umode_t mode,
					       struct rchan_buf *buf,
					       int *is_global)
{
	struct dentry *buf_file;

	/*
	 * This to enable the use of a single buffer for the relay channel and
	 * correspondingly have a single file exposed to User, through which
	 * it can collect the logs in order without any post-processing.
	 * Need to set 'is_global' even if parent is NULL for early logging.
	 */
	*is_global = 1;

	if (!parent)
		return NULL;

	buf_file = debugfs_create_file(filename, mode,
				       parent, buf, &relay_file_operations);
	if (IS_ERR(buf_file))
		return NULL;

	return buf_file;
}

/*
 * file_remove() default callback. Removes relay file in debugfs.
 */
static int remove_buf_file_callback(struct dentry *dentry)
{
	debugfs_remove(dentry);
	return 0;
}

/* relay channel callbacks */
static struct rchan_callbacks relay_callbacks = {
	.subbuf_start = subbuf_start_callback,
	.create_buf_file = create_buf_file_callback,
	.remove_buf_file = remove_buf_file_callback,
};

static void guc_move_to_next_buf(struct intel_guc_log *log)
{
	/*
	 * Make sure the updates made in the sub buffer are visible when
	 * Consumer sees the following update to offset inside the sub buffer.
	 */
	smp_wmb();

	/* All data has been written, so now move the offset of sub buffer. */
	relay_reserve(log->relay.channel, log->vma->obj->base.size);

	/* Switch to the next sub buffer */
	relay_flush(log->relay.channel);
}

static void *guc_get_write_buffer(struct intel_guc_log *log)
{
	/*
	 * Just get the base address of a new sub buffer and copy data into it
	 * ourselves. NULL will be returned in no-overwrite mode, if all sub
	 * buffers are full. Could have used the relay_write() to indirectly
	 * copy the data, but that would have been bit convoluted, as we need to
	 * write to only certain locations inside a sub buffer which cannot be
	 * done without using relay_reserve() along with relay_write(). So its
	 * better to use relay_reserve() alone.
	 */
	return relay_reserve(log->relay.channel, 0);
}

static bool guc_check_log_buf_overflow(struct intel_guc_log *log,
				       enum guc_log_buffer_type type,
				       unsigned int full_cnt)
{
	unsigned int prev_full_cnt = log->stats[type].sampled_overflow;
	bool overflow = false;

	if (full_cnt != prev_full_cnt) {
		overflow = true;

		log->stats[type].overflow = full_cnt;
		log->stats[type].sampled_overflow += full_cnt - prev_full_cnt;

		if (full_cnt < prev_full_cnt) {
			/* buffer_full_cnt is a 4 bit counter */
			log->stats[type].sampled_overflow += 16;
		}

		dev_notice_ratelimited(guc_to_i915(log_to_guc(log))->drm.dev,
				       "GuC log buffer overflow\n");
	}

	return overflow;
}

static unsigned int guc_get_log_buffer_size(enum guc_log_buffer_type type)
{
	switch (type) {
	case GUC_ISR_LOG_BUFFER:
		return ISR_BUFFER_SIZE;
	case GUC_DPC_LOG_BUFFER:
		return DPC_BUFFER_SIZE;
	case GUC_CRASH_DUMP_LOG_BUFFER:
		return CRASH_BUFFER_SIZE;
	default:
		MISSING_CASE(type);
	}

	return 0;
}

static void guc_read_update_log_buffer(struct intel_guc_log *log)
{
	unsigned int buffer_size, read_offset, write_offset, bytes_to_copy, full_cnt;
	struct guc_log_buffer_state *log_buf_state, *log_buf_snapshot_state;
	struct guc_log_buffer_state log_buf_state_local;
	enum guc_log_buffer_type type;
	void *src_data, *dst_data;
	bool new_overflow;

	mutex_lock(&log->relay.lock);

	if (WARN_ON(!intel_guc_log_relay_enabled(log)))
		goto out_unlock;

	/* Get the pointer to shared GuC log buffer */
	log_buf_state = src_data = log->relay.buf_addr;

	/* Get the pointer to local buffer to store the logs */
	log_buf_snapshot_state = dst_data = guc_get_write_buffer(log);

	if (unlikely(!log_buf_snapshot_state)) {
		/*
		 * Used rate limited to avoid deluge of messages, logs might be
		 * getting consumed by User at a slow rate.
		 */
		DRM_ERROR_RATELIMITED("no sub-buffer to capture logs\n");
		log->relay.full_count++;

		goto out_unlock;
	}

	/* Actual logs are present from the 2nd page */
	src_data += PAGE_SIZE;
	dst_data += PAGE_SIZE;

	for (type = GUC_ISR_LOG_BUFFER; type < GUC_MAX_LOG_BUFFER; type++) {
		/*
		 * Make a copy of the state structure, inside GuC log buffer
		 * (which is uncached mapped), on the stack to avoid reading
		 * from it multiple times.
		 */
		memcpy(&log_buf_state_local, log_buf_state,
		       sizeof(struct guc_log_buffer_state));
		buffer_size = guc_get_log_buffer_size(type);
		read_offset = log_buf_state_local.read_ptr;
		write_offset = log_buf_state_local.sampled_write_ptr;
		full_cnt = log_buf_state_local.buffer_full_cnt;

		/* Bookkeeping stuff */
		log->stats[type].flush += log_buf_state_local.flush_to_file;
		new_overflow = guc_check_log_buf_overflow(log, type, full_cnt);

		/* Update the state of shared log buffer */
		log_buf_state->read_ptr = write_offset;
		log_buf_state->flush_to_file = 0;
		log_buf_state++;

		/* First copy the state structure in snapshot buffer */
		memcpy(log_buf_snapshot_state, &log_buf_state_local,
		       sizeof(struct guc_log_buffer_state));

		/*
		 * The write pointer could have been updated by GuC firmware,
		 * after sending the flush interrupt to Host, for consistency
		 * set write pointer value to same value of sampled_write_ptr
		 * in the snapshot buffer.
		 */
		log_buf_snapshot_state->write_ptr = write_offset;
		log_buf_snapshot_state++;

		/* Now copy the actual logs. */
		if (unlikely(new_overflow)) {
			/* copy the whole buffer in case of overflow */
			read_offset = 0;
			write_offset = buffer_size;
		} else if (unlikely((read_offset > buffer_size) ||
				    (write_offset > buffer_size))) {
			DRM_ERROR("invalid log buffer state\n");
			/* copy whole buffer as offsets are unreliable */
			read_offset = 0;
			write_offset = buffer_size;
		}

		/* Just copy the newly written data */
		if (read_offset > write_offset) {
			i915_memcpy_from_wc(dst_data, src_data, write_offset);
			bytes_to_copy = buffer_size - read_offset;
		} else {
			bytes_to_copy = write_offset - read_offset;
		}
		i915_memcpy_from_wc(dst_data + read_offset,
				    src_data + read_offset, bytes_to_copy);

		src_data += buffer_size;
		dst_data += buffer_size;
	}

	guc_move_to_next_buf(log);

out_unlock:
	mutex_unlock(&log->relay.lock);
}

static void capture_logs_work(struct work_struct *work)
{
	struct intel_guc_log *log =
		container_of(work, struct intel_guc_log, relay.flush_work);

	guc_log_capture_logs(log);
}

static int guc_log_map(struct intel_guc_log *log)
{
	void *vaddr;

	lockdep_assert_held(&log->relay.lock);

	if (!log->vma)
		return -ENODEV;

	/*
	 * Create a WC (Uncached for read) vmalloc mapping of log
	 * buffer pages, so that we can directly get the data
	 * (up-to-date) from memory.
	 */
	vaddr = i915_gem_object_pin_map(log->vma->obj, I915_MAP_WC);
	if (IS_ERR(vaddr))
		return PTR_ERR(vaddr);

	log->relay.buf_addr = vaddr;

	return 0;
}

static void guc_log_unmap(struct intel_guc_log *log)
{
	lockdep_assert_held(&log->relay.lock);

	i915_gem_object_unpin_map(log->vma->obj);
	log->relay.buf_addr = NULL;
}

void intel_guc_log_init_early(struct intel_guc_log *log)
{
	mutex_init(&log->relay.lock);
	INIT_WORK(&log->relay.flush_work, capture_logs_work);
}

static int guc_log_relay_create(struct intel_guc_log *log)
{
	struct intel_guc *guc = log_to_guc(log);
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	struct rchan *guc_log_relay_chan;
	size_t n_subbufs, subbuf_size;
	int ret;

	lockdep_assert_held(&log->relay.lock);

	 /* Keep the size of sub buffers same as shared log buffer */
	subbuf_size = log->vma->size;

	/*
	 * Store up to 8 snapshots, which is large enough to buffer sufficient
	 * boot time logs and provides enough leeway to User, in terms of
	 * latency, for consuming the logs from relay. Also doesn't take
	 * up too much memory.
	 */
	n_subbufs = 8;

	guc_log_relay_chan = relay_open("guc_log",
					dev_priv->drm.primary->debugfs_root,
					subbuf_size, n_subbufs,
					&relay_callbacks, dev_priv);
	if (!guc_log_relay_chan) {
		DRM_ERROR("Couldn't create relay chan for GuC logging\n");

		ret = -ENOMEM;
		return ret;
	}

	GEM_BUG_ON(guc_log_relay_chan->subbuf_size < subbuf_size);
	log->relay.channel = guc_log_relay_chan;

	return 0;
}

static void guc_log_relay_destroy(struct intel_guc_log *log)
{
	lockdep_assert_held(&log->relay.lock);

	relay_close(log->relay.channel);
	log->relay.channel = NULL;
}

static void guc_log_capture_logs(struct intel_guc_log *log)
{
	struct intel_guc *guc = log_to_guc(log);
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	intel_wakeref_t wakeref;

	guc_read_update_log_buffer(log);

	/*
	 * Generally device is expected to be active only at this
	 * time, so get/put should be really quick.
	 */
	with_intel_runtime_pm(&dev_priv->runtime_pm, wakeref)
		guc_action_flush_log_complete(guc);
}

int intel_guc_log_create(struct intel_guc_log *log)
{
	struct intel_guc *guc = log_to_guc(log);
	struct i915_vma *vma;
	u32 guc_log_size;
	int ret;

	GEM_BUG_ON(log->vma);

	/*
	 *  GuC Log buffer Layout
	 *
	 *  +===============================+ 00B
	 *  |    Crash dump state header    |
	 *  +-------------------------------+ 32B
	 *  |       DPC state header        |
	 *  +-------------------------------+ 64B
	 *  |       ISR state header        |
	 *  +-------------------------------+ 96B
	 *  |                               |
	 *  +===============================+ PAGE_SIZE (4KB)
	 *  |        Crash Dump logs        |
	 *  +===============================+ + CRASH_SIZE
	 *  |           DPC logs            |
	 *  +===============================+ + DPC_SIZE
	 *  |           ISR logs            |
	 *  +===============================+ + ISR_SIZE
	 */
	guc_log_size = PAGE_SIZE + CRASH_BUFFER_SIZE + DPC_BUFFER_SIZE +
			ISR_BUFFER_SIZE;

	vma = intel_guc_allocate_vma(guc, guc_log_size);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto err;
	}

	log->vma = vma;

	log->level = i915_modparams.guc_log_level;

	return 0;

err:
	DRM_ERROR("Failed to allocate GuC log buffer. %d\n", ret);
	return ret;
}

void intel_guc_log_destroy(struct intel_guc_log *log)
{
	i915_vma_unpin_and_release(&log->vma, 0);
}

int intel_guc_log_set_level(struct intel_guc_log *log, u32 level)
{
	struct intel_guc *guc = log_to_guc(log);
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	intel_wakeref_t wakeref;
	int ret = 0;

	BUILD_BUG_ON(GUC_LOG_VERBOSITY_MIN != 0);
	GEM_BUG_ON(!log->vma);

	/*
	 * GuC is recognizing log levels starting from 0 to max, we're using 0
	 * as indication that logging should be disabled.
	 */
	if (level < GUC_LOG_LEVEL_DISABLED || level > GUC_LOG_LEVEL_MAX)
		return -EINVAL;

	mutex_lock(&dev_priv->drm.struct_mutex);

	if (log->level == level)
		goto out_unlock;

	with_intel_runtime_pm(&dev_priv->runtime_pm, wakeref)
		ret = guc_action_control_log(guc,
					     GUC_LOG_LEVEL_IS_VERBOSE(level),
					     GUC_LOG_LEVEL_IS_ENABLED(level),
					     GUC_LOG_LEVEL_TO_VERBOSITY(level));
	if (ret) {
		DRM_DEBUG_DRIVER("guc_log_control action failed %d\n", ret);
		goto out_unlock;
	}

	log->level = level;

out_unlock:
	mutex_unlock(&dev_priv->drm.struct_mutex);

	return ret;
}

bool intel_guc_log_relay_enabled(const struct intel_guc_log *log)
{
	return log->relay.buf_addr;
}

int intel_guc_log_relay_open(struct intel_guc_log *log)
{
	int ret;

	mutex_lock(&log->relay.lock);

	if (intel_guc_log_relay_enabled(log)) {
		ret = -EEXIST;
		goto out_unlock;
	}

	/*
	 * We require SSE 4.1 for fast reads from the GuC log buffer and
	 * it should be present on the chipsets supporting GuC based
	 * submisssions.
	 */
	if (!i915_has_memcpy_from_wc()) {
		ret = -ENXIO;
		goto out_unlock;
	}

	ret = guc_log_relay_create(log);
	if (ret)
		goto out_unlock;

	ret = guc_log_map(log);
	if (ret)
		goto out_relay;

	mutex_unlock(&log->relay.lock);

	guc_log_enable_flush_events(log);

	/*
	 * When GuC is logging without us relaying to userspace, we're ignoring
	 * the flush notification. This means that we need to unconditionally
	 * flush on relay enabling, since GuC only notifies us once.
	 */
	queue_work(log->relay.flush_wq, &log->relay.flush_work);

	return 0;

out_relay:
	guc_log_relay_destroy(log);
out_unlock:
	mutex_unlock(&log->relay.lock);

	return ret;
}

void intel_guc_log_relay_flush(struct intel_guc_log *log)
{
	struct intel_guc *guc = log_to_guc(log);
	struct drm_i915_private *i915 = guc_to_i915(guc);
	intel_wakeref_t wakeref;

	/*
	 * Before initiating the forceful flush, wait for any pending/ongoing
	 * flush to complete otherwise forceful flush may not actually happen.
	 */
	flush_work(&log->relay.flush_work);

	with_intel_runtime_pm(&i915->runtime_pm, wakeref)
		guc_action_flush_log(guc);

	/* GuC would have updated log buffer by now, so capture it */
	guc_log_capture_logs(log);
}

void intel_guc_log_relay_close(struct intel_guc_log *log)
{
	struct intel_guc *guc = log_to_guc(log);
	struct drm_i915_private *i915 = guc_to_i915(guc);

	guc_log_disable_flush_events(log);
	intel_synchronize_irq(i915);

	flush_work(&log->relay.flush_work);

	mutex_lock(&log->relay.lock);
	GEM_BUG_ON(!intel_guc_log_relay_enabled(log));
	guc_log_unmap(log);
	guc_log_relay_destroy(log);
	mutex_unlock(&log->relay.lock);
}

void intel_guc_log_handle_flush_event(struct intel_guc_log *log)
{
	queue_work(log->relay.flush_wq, &log->relay.flush_work);
}
