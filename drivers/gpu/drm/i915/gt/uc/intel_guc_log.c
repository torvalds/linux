// SPDX-License-Identifier: MIT
/*
 * Copyright © 2014-2019 Intel Corporation
 */

#include <linux/debugfs.h>
#include <linux/string_helpers.h>

#include "gt/intel_gt.h"
#include "i915_drv.h"
#include "i915_irq.h"
#include "i915_memcpy.h"
#include "intel_guc_capture.h"
#include "intel_guc_log.h"

#if defined(CONFIG_DRM_I915_DEBUG_GUC)
#define GUC_LOG_DEFAULT_CRASH_BUFFER_SIZE	SZ_2M
#define GUC_LOG_DEFAULT_DEBUG_BUFFER_SIZE	SZ_16M
#define GUC_LOG_DEFAULT_CAPTURE_BUFFER_SIZE	SZ_4M
#elif defined(CONFIG_DRM_I915_DEBUG_GEM)
#define GUC_LOG_DEFAULT_CRASH_BUFFER_SIZE	SZ_1M
#define GUC_LOG_DEFAULT_DEBUG_BUFFER_SIZE	SZ_2M
#define GUC_LOG_DEFAULT_CAPTURE_BUFFER_SIZE	SZ_4M
#else
#define GUC_LOG_DEFAULT_CRASH_BUFFER_SIZE	SZ_8K
#define GUC_LOG_DEFAULT_DEBUG_BUFFER_SIZE	SZ_64K
#define GUC_LOG_DEFAULT_CAPTURE_BUFFER_SIZE	SZ_2M
#endif

static void guc_log_copy_debuglogs_for_relay(struct intel_guc_log *log);

struct guc_log_section {
	u32 max;
	u32 flag;
	u32 default_val;
	const char *name;
};

static void _guc_log_init_sizes(struct intel_guc_log *log)
{
	struct intel_guc *guc = log_to_guc(log);
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	static const struct guc_log_section sections[GUC_LOG_SECTIONS_LIMIT] = {
		{
			GUC_LOG_CRASH_MASK >> GUC_LOG_CRASH_SHIFT,
			GUC_LOG_LOG_ALLOC_UNITS,
			GUC_LOG_DEFAULT_CRASH_BUFFER_SIZE,
			"crash dump"
		},
		{
			GUC_LOG_DEBUG_MASK >> GUC_LOG_DEBUG_SHIFT,
			GUC_LOG_LOG_ALLOC_UNITS,
			GUC_LOG_DEFAULT_DEBUG_BUFFER_SIZE,
			"debug",
		},
		{
			GUC_LOG_CAPTURE_MASK >> GUC_LOG_CAPTURE_SHIFT,
			GUC_LOG_CAPTURE_ALLOC_UNITS,
			GUC_LOG_DEFAULT_CAPTURE_BUFFER_SIZE,
			"capture",
		}
	};
	int i;

	for (i = 0; i < GUC_LOG_SECTIONS_LIMIT; i++)
		log->sizes[i].bytes = sections[i].default_val;

	/* If debug size > 1MB then bump default crash size to keep the same units */
	if (log->sizes[GUC_LOG_SECTIONS_DEBUG].bytes >= SZ_1M &&
	    GUC_LOG_DEFAULT_CRASH_BUFFER_SIZE < SZ_1M)
		log->sizes[GUC_LOG_SECTIONS_CRASH].bytes = SZ_1M;

	/* Prepare the GuC API structure fields: */
	for (i = 0; i < GUC_LOG_SECTIONS_LIMIT; i++) {
		/* Convert to correct units */
		if ((log->sizes[i].bytes % SZ_1M) == 0) {
			log->sizes[i].units = SZ_1M;
			log->sizes[i].flag = sections[i].flag;
		} else {
			log->sizes[i].units = SZ_4K;
			log->sizes[i].flag = 0;
		}

		if (!IS_ALIGNED(log->sizes[i].bytes, log->sizes[i].units))
			drm_err(&i915->drm, "Mis-aligned GuC log %s size: 0x%X vs 0x%X!",
				sections[i].name, log->sizes[i].bytes, log->sizes[i].units);
		log->sizes[i].count = log->sizes[i].bytes / log->sizes[i].units;

		if (!log->sizes[i].count) {
			drm_err(&i915->drm, "Zero GuC log %s size!", sections[i].name);
		} else {
			/* Size is +1 unit */
			log->sizes[i].count--;
		}

		/* Clip to field size */
		if (log->sizes[i].count > sections[i].max) {
			drm_err(&i915->drm, "GuC log %s size too large: %d vs %d!",
				sections[i].name, log->sizes[i].count + 1, sections[i].max + 1);
			log->sizes[i].count = sections[i].max;
		}
	}

	if (log->sizes[GUC_LOG_SECTIONS_CRASH].units != log->sizes[GUC_LOG_SECTIONS_DEBUG].units) {
		drm_err(&i915->drm, "Unit mis-match for GuC log crash and debug sections: %d vs %d!",
			log->sizes[GUC_LOG_SECTIONS_CRASH].units,
			log->sizes[GUC_LOG_SECTIONS_DEBUG].units);
		log->sizes[GUC_LOG_SECTIONS_CRASH].units = log->sizes[GUC_LOG_SECTIONS_DEBUG].units;
		log->sizes[GUC_LOG_SECTIONS_CRASH].count = 0;
	}

	log->sizes_initialised = true;
}

static void guc_log_init_sizes(struct intel_guc_log *log)
{
	if (log->sizes_initialised)
		return;

	_guc_log_init_sizes(log);
}

static u32 intel_guc_log_section_size_crash(struct intel_guc_log *log)
{
	guc_log_init_sizes(log);

	return log->sizes[GUC_LOG_SECTIONS_CRASH].bytes;
}

static u32 intel_guc_log_section_size_debug(struct intel_guc_log *log)
{
	guc_log_init_sizes(log);

	return log->sizes[GUC_LOG_SECTIONS_DEBUG].bytes;
}

u32 intel_guc_log_section_size_capture(struct intel_guc_log *log)
{
	guc_log_init_sizes(log);

	return log->sizes[GUC_LOG_SECTIONS_CAPTURE].bytes;
}

static u32 intel_guc_log_size(struct intel_guc_log *log)
{
	/*
	 *  GuC Log buffer Layout:
	 *
	 *  NB: Ordering must follow "enum guc_log_buffer_type".
	 *
	 *  +===============================+ 00B
	 *  |      Debug state header       |
	 *  +-------------------------------+ 32B
	 *  |    Crash dump state header    |
	 *  +-------------------------------+ 64B
	 *  |     Capture state header      |
	 *  +-------------------------------+ 96B
	 *  |                               |
	 *  +===============================+ PAGE_SIZE (4KB)
	 *  |          Debug logs           |
	 *  +===============================+ + DEBUG_SIZE
	 *  |        Crash Dump logs        |
	 *  +===============================+ + CRASH_SIZE
	 *  |         Capture logs          |
	 *  +===============================+ + CAPTURE_SIZE
	 */
	return PAGE_SIZE +
		intel_guc_log_section_size_crash(log) +
		intel_guc_log_section_size_debug(log) +
		intel_guc_log_section_size_capture(log);
}

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
		INTEL_GUC_ACTION_LOG_BUFFER_FILE_FLUSH_COMPLETE,
		GUC_DEBUG_LOG_BUFFER
	};

	return intel_guc_send_nb(guc, action, ARRAY_SIZE(action), 0);
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
static const struct rchan_callbacks relay_callbacks = {
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
	relay_reserve(log->relay.channel, log->vma->obj->base.size -
					  intel_guc_log_section_size_capture(log));

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

bool intel_guc_check_log_buf_overflow(struct intel_guc_log *log,
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

		dev_notice_ratelimited(guc_to_gt(log_to_guc(log))->i915->drm.dev,
				       "GuC log buffer overflow\n");
	}

	return overflow;
}

unsigned int intel_guc_get_log_buffer_size(struct intel_guc_log *log,
					   enum guc_log_buffer_type type)
{
	switch (type) {
	case GUC_DEBUG_LOG_BUFFER:
		return intel_guc_log_section_size_debug(log);
	case GUC_CRASH_DUMP_LOG_BUFFER:
		return intel_guc_log_section_size_crash(log);
	case GUC_CAPTURE_LOG_BUFFER:
		return intel_guc_log_section_size_capture(log);
	default:
		MISSING_CASE(type);
	}

	return 0;
}

size_t intel_guc_get_log_buffer_offset(struct intel_guc_log *log,
				       enum guc_log_buffer_type type)
{
	enum guc_log_buffer_type i;
	size_t offset = PAGE_SIZE;/* for the log_buffer_states */

	for (i = GUC_DEBUG_LOG_BUFFER; i < GUC_MAX_LOG_BUFFER; ++i) {
		if (i == type)
			break;
		offset += intel_guc_get_log_buffer_size(log, i);
	}

	return offset;
}

static void _guc_log_copy_debuglogs_for_relay(struct intel_guc_log *log)
{
	unsigned int buffer_size, read_offset, write_offset, bytes_to_copy, full_cnt;
	struct guc_log_buffer_state *log_buf_state, *log_buf_snapshot_state;
	struct guc_log_buffer_state log_buf_state_local;
	enum guc_log_buffer_type type;
	void *src_data, *dst_data;
	bool new_overflow;

	mutex_lock(&log->relay.lock);

	if (WARN_ON(!intel_guc_log_relay_created(log)))
		goto out_unlock;

	/* Get the pointer to shared GuC log buffer */
	src_data = log->buf_addr;
	log_buf_state = src_data;

	/* Get the pointer to local buffer to store the logs */
	log_buf_snapshot_state = dst_data = guc_get_write_buffer(log);

	if (unlikely(!log_buf_snapshot_state)) {
		/*
		 * Used rate limited to avoid deluge of messages, logs might be
		 * getting consumed by User at a slow rate.
		 */
		DRM_ERROR_RATELIMITED("no sub-buffer to copy general logs\n");
		log->relay.full_count++;

		goto out_unlock;
	}

	/* Actual logs are present from the 2nd page */
	src_data += PAGE_SIZE;
	dst_data += PAGE_SIZE;

	/* For relay logging, we exclude error state capture */
	for (type = GUC_DEBUG_LOG_BUFFER; type <= GUC_CRASH_DUMP_LOG_BUFFER; type++) {
		/*
		 * Make a copy of the state structure, inside GuC log buffer
		 * (which is uncached mapped), on the stack to avoid reading
		 * from it multiple times.
		 */
		memcpy(&log_buf_state_local, log_buf_state,
		       sizeof(struct guc_log_buffer_state));
		buffer_size = intel_guc_get_log_buffer_size(log, type);
		read_offset = log_buf_state_local.read_ptr;
		write_offset = log_buf_state_local.sampled_write_ptr;
		full_cnt = log_buf_state_local.buffer_full_cnt;

		/* Bookkeeping stuff */
		log->stats[type].flush += log_buf_state_local.flush_to_file;
		new_overflow = intel_guc_check_log_buf_overflow(log, type, full_cnt);

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

static void copy_debug_logs_work(struct work_struct *work)
{
	struct intel_guc_log *log =
		container_of(work, struct intel_guc_log, relay.flush_work);

	guc_log_copy_debuglogs_for_relay(log);
}

static int guc_log_relay_map(struct intel_guc_log *log)
{
	lockdep_assert_held(&log->relay.lock);

	if (!log->vma || !log->buf_addr)
		return -ENODEV;

	/*
	 * WC vmalloc mapping of log buffer pages was done at
	 * GuC Log Init time, but lets keep a ref for book-keeping
	 */
	i915_gem_object_get(log->vma->obj);
	log->relay.buf_in_use = true;

	return 0;
}

static void guc_log_relay_unmap(struct intel_guc_log *log)
{
	lockdep_assert_held(&log->relay.lock);

	i915_gem_object_put(log->vma->obj);
	log->relay.buf_in_use = false;
}

void intel_guc_log_init_early(struct intel_guc_log *log)
{
	mutex_init(&log->relay.lock);
	INIT_WORK(&log->relay.flush_work, copy_debug_logs_work);
	log->relay.started = false;
}

static int guc_log_relay_create(struct intel_guc_log *log)
{
	struct intel_guc *guc = log_to_guc(log);
	struct drm_i915_private *dev_priv = guc_to_gt(guc)->i915;
	struct rchan *guc_log_relay_chan;
	size_t n_subbufs, subbuf_size;
	int ret;

	lockdep_assert_held(&log->relay.lock);
	GEM_BUG_ON(!log->vma);

	 /*
	  * Keep the size of sub buffers same as shared log buffer
	  * but GuC log-events excludes the error-state-capture logs
	  */
	subbuf_size = log->vma->size - intel_guc_log_section_size_capture(log);

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

static void guc_log_copy_debuglogs_for_relay(struct intel_guc_log *log)
{
	struct intel_guc *guc = log_to_guc(log);
	struct drm_i915_private *dev_priv = guc_to_gt(guc)->i915;
	intel_wakeref_t wakeref;

	_guc_log_copy_debuglogs_for_relay(log);

	/*
	 * Generally device is expected to be active only at this
	 * time, so get/put should be really quick.
	 */
	with_intel_runtime_pm(&dev_priv->runtime_pm, wakeref)
		guc_action_flush_log_complete(guc);
}

static u32 __get_default_log_level(struct intel_guc_log *log)
{
	struct intel_guc *guc = log_to_guc(log);
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;

	/* A negative value means "use platform/config default" */
	if (i915->params.guc_log_level < 0) {
		return (IS_ENABLED(CONFIG_DRM_I915_DEBUG) ||
			IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM)) ?
			GUC_LOG_LEVEL_MAX : GUC_LOG_LEVEL_NON_VERBOSE;
	}

	if (i915->params.guc_log_level > GUC_LOG_LEVEL_MAX) {
		DRM_WARN("Incompatible option detected: %s=%d, %s!\n",
			 "guc_log_level", i915->params.guc_log_level,
			 "verbosity too high");
		return (IS_ENABLED(CONFIG_DRM_I915_DEBUG) ||
			IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM)) ?
			GUC_LOG_LEVEL_MAX : GUC_LOG_LEVEL_DISABLED;
	}

	GEM_BUG_ON(i915->params.guc_log_level < GUC_LOG_LEVEL_DISABLED);
	GEM_BUG_ON(i915->params.guc_log_level > GUC_LOG_LEVEL_MAX);
	return i915->params.guc_log_level;
}

int intel_guc_log_create(struct intel_guc_log *log)
{
	struct intel_guc *guc = log_to_guc(log);
	struct i915_vma *vma;
	void *vaddr;
	u32 guc_log_size;
	int ret;

	GEM_BUG_ON(log->vma);

	guc_log_size = intel_guc_log_size(log);

	vma = intel_guc_allocate_vma(guc, guc_log_size);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto err;
	}

	log->vma = vma;
	/*
	 * Create a WC (Uncached for read) vmalloc mapping up front immediate access to
	 * data from memory during  critical events such as error capture
	 */
	vaddr = i915_gem_object_pin_map_unlocked(log->vma->obj, I915_MAP_WC);
	if (IS_ERR(vaddr)) {
		ret = PTR_ERR(vaddr);
		i915_vma_unpin_and_release(&log->vma, 0);
		goto err;
	}
	log->buf_addr = vaddr;

	log->level = __get_default_log_level(log);
	DRM_DEBUG_DRIVER("guc_log_level=%d (%s, verbose:%s, verbosity:%d)\n",
			 log->level, str_enabled_disabled(log->level),
			 str_yes_no(GUC_LOG_LEVEL_IS_VERBOSE(log->level)),
			 GUC_LOG_LEVEL_TO_VERBOSITY(log->level));

	return 0;

err:
	DRM_ERROR("Failed to allocate or map GuC log buffer. %d\n", ret);
	return ret;
}

void intel_guc_log_destroy(struct intel_guc_log *log)
{
	log->buf_addr = NULL;
	i915_vma_unpin_and_release(&log->vma, I915_VMA_RELEASE_MAP);
}

int intel_guc_log_set_level(struct intel_guc_log *log, u32 level)
{
	struct intel_guc *guc = log_to_guc(log);
	struct drm_i915_private *dev_priv = guc_to_gt(guc)->i915;
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

bool intel_guc_log_relay_created(const struct intel_guc_log *log)
{
	return log->buf_addr;
}

int intel_guc_log_relay_open(struct intel_guc_log *log)
{
	int ret;

	if (!log->vma)
		return -ENODEV;

	mutex_lock(&log->relay.lock);

	if (intel_guc_log_relay_created(log)) {
		ret = -EEXIST;
		goto out_unlock;
	}

	/*
	 * We require SSE 4.1 for fast reads from the GuC log buffer and
	 * it should be present on the chipsets supporting GuC based
	 * submissions.
	 */
	if (!i915_has_memcpy_from_wc()) {
		ret = -ENXIO;
		goto out_unlock;
	}

	ret = guc_log_relay_create(log);
	if (ret)
		goto out_unlock;

	ret = guc_log_relay_map(log);
	if (ret)
		goto out_relay;

	mutex_unlock(&log->relay.lock);

	return 0;

out_relay:
	guc_log_relay_destroy(log);
out_unlock:
	mutex_unlock(&log->relay.lock);

	return ret;
}

int intel_guc_log_relay_start(struct intel_guc_log *log)
{
	if (log->relay.started)
		return -EEXIST;

	/*
	 * When GuC is logging without us relaying to userspace, we're ignoring
	 * the flush notification. This means that we need to unconditionally
	 * flush on relay enabling, since GuC only notifies us once.
	 */
	queue_work(system_highpri_wq, &log->relay.flush_work);

	log->relay.started = true;

	return 0;
}

void intel_guc_log_relay_flush(struct intel_guc_log *log)
{
	struct intel_guc *guc = log_to_guc(log);
	intel_wakeref_t wakeref;

	if (!log->relay.started)
		return;

	/*
	 * Before initiating the forceful flush, wait for any pending/ongoing
	 * flush to complete otherwise forceful flush may not actually happen.
	 */
	flush_work(&log->relay.flush_work);

	with_intel_runtime_pm(guc_to_gt(guc)->uncore->rpm, wakeref)
		guc_action_flush_log(guc);

	/* GuC would have updated log buffer by now, so copy it */
	guc_log_copy_debuglogs_for_relay(log);
}

/*
 * Stops the relay log. Called from intel_guc_log_relay_close(), so no
 * possibility of race with start/flush since relay_write cannot race
 * relay_close.
 */
static void guc_log_relay_stop(struct intel_guc_log *log)
{
	struct intel_guc *guc = log_to_guc(log);
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;

	if (!log->relay.started)
		return;

	intel_synchronize_irq(i915);

	flush_work(&log->relay.flush_work);

	log->relay.started = false;
}

void intel_guc_log_relay_close(struct intel_guc_log *log)
{
	guc_log_relay_stop(log);

	mutex_lock(&log->relay.lock);
	GEM_BUG_ON(!intel_guc_log_relay_created(log));
	guc_log_relay_unmap(log);
	guc_log_relay_destroy(log);
	mutex_unlock(&log->relay.lock);
}

void intel_guc_log_handle_flush_event(struct intel_guc_log *log)
{
	if (log->relay.started)
		queue_work(system_highpri_wq, &log->relay.flush_work);
}

static const char *
stringify_guc_log_type(enum guc_log_buffer_type type)
{
	switch (type) {
	case GUC_DEBUG_LOG_BUFFER:
		return "DEBUG";
	case GUC_CRASH_DUMP_LOG_BUFFER:
		return "CRASH";
	case GUC_CAPTURE_LOG_BUFFER:
		return "CAPTURE";
	default:
		MISSING_CASE(type);
	}

	return "";
}

/**
 * intel_guc_log_info - dump information about GuC log relay
 * @log: the GuC log
 * @p: the &drm_printer
 *
 * Pretty printer for GuC log info
 */
void intel_guc_log_info(struct intel_guc_log *log, struct drm_printer *p)
{
	enum guc_log_buffer_type type;

	if (!intel_guc_log_relay_created(log)) {
		drm_puts(p, "GuC log relay not created\n");
		return;
	}

	drm_puts(p, "GuC logging stats:\n");

	drm_printf(p, "\tRelay full count: %u\n", log->relay.full_count);

	for (type = GUC_DEBUG_LOG_BUFFER; type < GUC_MAX_LOG_BUFFER; type++) {
		drm_printf(p, "\t%s:\tflush count %10u, overflow count %10u\n",
			   stringify_guc_log_type(type),
			   log->stats[type].flush,
			   log->stats[type].sampled_overflow);
	}
}

/**
 * intel_guc_log_dump - dump the contents of the GuC log
 * @log: the GuC log
 * @p: the &drm_printer
 * @dump_load_err: dump the log saved on GuC load error
 *
 * Pretty printer for the GuC log
 */
int intel_guc_log_dump(struct intel_guc_log *log, struct drm_printer *p,
		       bool dump_load_err)
{
	struct intel_guc *guc = log_to_guc(log);
	struct intel_uc *uc = container_of(guc, struct intel_uc, guc);
	struct drm_i915_gem_object *obj = NULL;
	void *map;
	u32 *page;
	int i, j;

	if (!intel_guc_is_supported(guc))
		return -ENODEV;

	if (dump_load_err)
		obj = uc->load_err_log;
	else if (guc->log.vma)
		obj = guc->log.vma->obj;

	if (!obj)
		return 0;

	page = (u32 *)__get_free_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	intel_guc_dump_time_info(guc, p);

	map = i915_gem_object_pin_map_unlocked(obj, I915_MAP_WC);
	if (IS_ERR(map)) {
		DRM_DEBUG("Failed to pin object\n");
		drm_puts(p, "(log data unaccessible)\n");
		free_page((unsigned long)page);
		return PTR_ERR(map);
	}

	for (i = 0; i < obj->base.size; i += PAGE_SIZE) {
		if (!i915_memcpy_from_wc(page, map + i, PAGE_SIZE))
			memcpy(page, map + i, PAGE_SIZE);

		for (j = 0; j < PAGE_SIZE / sizeof(u32); j += 4)
			drm_printf(p, "0x%08x 0x%08x 0x%08x 0x%08x\n",
				   *(page + j + 0), *(page + j + 1),
				   *(page + j + 2), *(page + j + 3));
	}

	drm_puts(p, "\n");

	i915_gem_object_unpin_map(obj);
	free_page((unsigned long)page);

	return 0;
}
