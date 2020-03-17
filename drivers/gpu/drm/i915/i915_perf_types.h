/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef _I915_PERF_TYPES_H_
#define _I915_PERF_TYPES_H_

#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/hrtimer.h>
#include <linux/llist.h>
#include <linux/poll.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/uuid.h>
#include <linux/wait.h>

#include "i915_reg.h"
#include "intel_wakeref.h"

struct drm_i915_private;
struct file;
struct i915_gem_context;
struct i915_perf;
struct i915_vma;
struct intel_context;
struct intel_engine_cs;

struct i915_oa_format {
	u32 format;
	int size;
};

struct i915_oa_reg {
	i915_reg_t addr;
	u32 value;
};

struct i915_oa_config {
	struct i915_perf *perf;

	char uuid[UUID_STRING_LEN + 1];
	int id;

	const struct i915_oa_reg *mux_regs;
	u32 mux_regs_len;
	const struct i915_oa_reg *b_counter_regs;
	u32 b_counter_regs_len;
	const struct i915_oa_reg *flex_regs;
	u32 flex_regs_len;

	struct attribute_group sysfs_metric;
	struct attribute *attrs[2];
	struct device_attribute sysfs_metric_id;

	struct kref ref;
	struct rcu_head rcu;
};

struct i915_perf_stream;

/**
 * struct i915_perf_stream_ops - the OPs to support a specific stream type
 */
struct i915_perf_stream_ops {
	/**
	 * @enable: Enables the collection of HW samples, either in response to
	 * `I915_PERF_IOCTL_ENABLE` or implicitly called when stream is opened
	 * without `I915_PERF_FLAG_DISABLED`.
	 */
	void (*enable)(struct i915_perf_stream *stream);

	/**
	 * @disable: Disables the collection of HW samples, either in response
	 * to `I915_PERF_IOCTL_DISABLE` or implicitly called before destroying
	 * the stream.
	 */
	void (*disable)(struct i915_perf_stream *stream);

	/**
	 * @poll_wait: Call poll_wait, passing a wait queue that will be woken
	 * once there is something ready to read() for the stream
	 */
	void (*poll_wait)(struct i915_perf_stream *stream,
			  struct file *file,
			  poll_table *wait);

	/**
	 * @wait_unlocked: For handling a blocking read, wait until there is
	 * something to ready to read() for the stream. E.g. wait on the same
	 * wait queue that would be passed to poll_wait().
	 */
	int (*wait_unlocked)(struct i915_perf_stream *stream);

	/**
	 * @read: Copy buffered metrics as records to userspace
	 * **buf**: the userspace, destination buffer
	 * **count**: the number of bytes to copy, requested by userspace
	 * **offset**: zero at the start of the read, updated as the read
	 * proceeds, it represents how many bytes have been copied so far and
	 * the buffer offset for copying the next record.
	 *
	 * Copy as many buffered i915 perf samples and records for this stream
	 * to userspace as will fit in the given buffer.
	 *
	 * Only write complete records; returning -%ENOSPC if there isn't room
	 * for a complete record.
	 *
	 * Return any error condition that results in a short read such as
	 * -%ENOSPC or -%EFAULT, even though these may be squashed before
	 * returning to userspace.
	 */
	int (*read)(struct i915_perf_stream *stream,
		    char __user *buf,
		    size_t count,
		    size_t *offset);

	/**
	 * @destroy: Cleanup any stream specific resources.
	 *
	 * The stream will always be disabled before this is called.
	 */
	void (*destroy)(struct i915_perf_stream *stream);
};

/**
 * struct i915_perf_stream - state for a single open stream FD
 */
struct i915_perf_stream {
	/**
	 * @perf: i915_perf backpointer
	 */
	struct i915_perf *perf;

	/**
	 * @uncore: mmio access path
	 */
	struct intel_uncore *uncore;

	/**
	 * @engine: Engine associated with this performance stream.
	 */
	struct intel_engine_cs *engine;

	/**
	 * @sample_flags: Flags representing the `DRM_I915_PERF_PROP_SAMPLE_*`
	 * properties given when opening a stream, representing the contents
	 * of a single sample as read() by userspace.
	 */
	u32 sample_flags;

	/**
	 * @sample_size: Considering the configured contents of a sample
	 * combined with the required header size, this is the total size
	 * of a single sample record.
	 */
	int sample_size;

	/**
	 * @ctx: %NULL if measuring system-wide across all contexts or a
	 * specific context that is being monitored.
	 */
	struct i915_gem_context *ctx;

	/**
	 * @enabled: Whether the stream is currently enabled, considering
	 * whether the stream was opened in a disabled state and based
	 * on `I915_PERF_IOCTL_ENABLE` and `I915_PERF_IOCTL_DISABLE` calls.
	 */
	bool enabled;

	/**
	 * @hold_preemption: Whether preemption is put on hold for command
	 * submissions done on the @ctx. This is useful for some drivers that
	 * cannot easily post process the OA buffer context to subtract delta
	 * of performance counters not associated with @ctx.
	 */
	bool hold_preemption;

	/**
	 * @ops: The callbacks providing the implementation of this specific
	 * type of configured stream.
	 */
	const struct i915_perf_stream_ops *ops;

	/**
	 * @oa_config: The OA configuration used by the stream.
	 */
	struct i915_oa_config *oa_config;

	/**
	 * @oa_config_bos: A list of struct i915_oa_config_bo allocated lazily
	 * each time @oa_config changes.
	 */
	struct llist_head oa_config_bos;

	/**
	 * @pinned_ctx: The OA context specific information.
	 */
	struct intel_context *pinned_ctx;

	/**
	 * @specific_ctx_id: The id of the specific context.
	 */
	u32 specific_ctx_id;

	/**
	 * @specific_ctx_id_mask: The mask used to masking specific_ctx_id bits.
	 */
	u32 specific_ctx_id_mask;

	/**
	 * @poll_check_timer: High resolution timer that will periodically
	 * check for data in the circular OA buffer for notifying userspace
	 * (e.g. during a read() or poll()).
	 */
	struct hrtimer poll_check_timer;

	/**
	 * @poll_wq: The wait queue that hrtimer callback wakes when it
	 * sees data ready to read in the circular OA buffer.
	 */
	wait_queue_head_t poll_wq;

	/**
	 * @pollin: Whether there is data available to read.
	 */
	bool pollin;

	/**
	 * @periodic: Whether periodic sampling is currently enabled.
	 */
	bool periodic;

	/**
	 * @period_exponent: The OA unit sampling frequency is derived from this.
	 */
	int period_exponent;

	/**
	 * @oa_buffer: State of the OA buffer.
	 */
	struct {
		struct i915_vma *vma;
		u8 *vaddr;
		u32 last_ctx_id;
		int format;
		int format_size;
		int size_exponent;

		/**
		 * @ptr_lock: Locks reads and writes to all head/tail state
		 *
		 * Consider: the head and tail pointer state needs to be read
		 * consistently from a hrtimer callback (atomic context) and
		 * read() fop (user context) with tail pointer updates happening
		 * in atomic context and head updates in user context and the
		 * (unlikely) possibility of read() errors needing to reset all
		 * head/tail state.
		 *
		 * Note: Contention/performance aren't currently a significant
		 * concern here considering the relatively low frequency of
		 * hrtimer callbacks (5ms period) and that reads typically only
		 * happen in response to a hrtimer event and likely complete
		 * before the next callback.
		 *
		 * Note: This lock is not held *while* reading and copying data
		 * to userspace so the value of head observed in htrimer
		 * callbacks won't represent any partial consumption of data.
		 */
		spinlock_t ptr_lock;

		/**
		 * @tails: One 'aging' tail pointer and one 'aged' tail pointer ready to
		 * used for reading.
		 *
		 * Initial values of 0xffffffff are invalid and imply that an
		 * update is required (and should be ignored by an attempted
		 * read)
		 */
		struct {
			u32 offset;
		} tails[2];

		/**
		 * @aged_tail_idx: Index for the aged tail ready to read() data up to.
		 */
		unsigned int aged_tail_idx;

		/**
		 * @aging_timestamp: A monotonic timestamp for when the current aging tail pointer
		 * was read; used to determine when it is old enough to trust.
		 */
		u64 aging_timestamp;

		/**
		 * @head: Although we can always read back the head pointer register,
		 * we prefer to avoid trusting the HW state, just to avoid any
		 * risk that some hardware condition could * somehow bump the
		 * head pointer unpredictably and cause us to forward the wrong
		 * OA buffer data to userspace.
		 */
		u32 head;
	} oa_buffer;

	/**
	 * @noa_wait: A batch buffer doing a wait on the GPU for the NOA logic to be
	 * reprogrammed.
	 */
	struct i915_vma *noa_wait;
};

/**
 * struct i915_oa_ops - Gen specific implementation of an OA unit stream
 */
struct i915_oa_ops {
	/**
	 * @is_valid_b_counter_reg: Validates register's address for
	 * programming boolean counters for a particular platform.
	 */
	bool (*is_valid_b_counter_reg)(struct i915_perf *perf, u32 addr);

	/**
	 * @is_valid_mux_reg: Validates register's address for programming mux
	 * for a particular platform.
	 */
	bool (*is_valid_mux_reg)(struct i915_perf *perf, u32 addr);

	/**
	 * @is_valid_flex_reg: Validates register's address for programming
	 * flex EU filtering for a particular platform.
	 */
	bool (*is_valid_flex_reg)(struct i915_perf *perf, u32 addr);

	/**
	 * @enable_metric_set: Selects and applies any MUX configuration to set
	 * up the Boolean and Custom (B/C) counters that are part of the
	 * counter reports being sampled. May apply system constraints such as
	 * disabling EU clock gating as required.
	 */
	struct i915_request *
		(*enable_metric_set)(struct i915_perf_stream *stream);

	/**
	 * @disable_metric_set: Remove system constraints associated with using
	 * the OA unit.
	 */
	void (*disable_metric_set)(struct i915_perf_stream *stream);

	/**
	 * @oa_enable: Enable periodic sampling
	 */
	void (*oa_enable)(struct i915_perf_stream *stream);

	/**
	 * @oa_disable: Disable periodic sampling
	 */
	void (*oa_disable)(struct i915_perf_stream *stream);

	/**
	 * @read: Copy data from the circular OA buffer into a given userspace
	 * buffer.
	 */
	int (*read)(struct i915_perf_stream *stream,
		    char __user *buf,
		    size_t count,
		    size_t *offset);

	/**
	 * @oa_hw_tail_read: read the OA tail pointer register
	 *
	 * In particular this enables us to share all the fiddly code for
	 * handling the OA unit tail pointer race that affects multiple
	 * generations.
	 */
	u32 (*oa_hw_tail_read)(struct i915_perf_stream *stream);
};

struct i915_perf {
	struct drm_i915_private *i915;

	struct kobject *metrics_kobj;

	/*
	 * Lock associated with adding/modifying/removing OA configs
	 * in perf->metrics_idr.
	 */
	struct mutex metrics_lock;

	/*
	 * List of dynamic configurations (struct i915_oa_config), you
	 * need to hold perf->metrics_lock to access it.
	 */
	struct idr metrics_idr;

	/*
	 * Lock associated with anything below within this structure
	 * except exclusive_stream.
	 */
	struct mutex lock;

	/*
	 * The stream currently using the OA unit. If accessed
	 * outside a syscall associated to its file
	 * descriptor.
	 */
	struct i915_perf_stream *exclusive_stream;

	/**
	 * For rate limiting any notifications of spurious
	 * invalid OA reports
	 */
	struct ratelimit_state spurious_report_rs;

	struct i915_oa_config test_config;

	u32 gen7_latched_oastatus1;
	u32 ctx_oactxctrl_offset;
	u32 ctx_flexeu0_offset;

	/**
	 * The RPT_ID/reason field for Gen8+ includes a bit
	 * to determine if the CTX ID in the report is valid
	 * but the specific bit differs between Gen 8 and 9
	 */
	u32 gen8_valid_ctx_bit;

	struct i915_oa_ops ops;
	const struct i915_oa_format *oa_formats;

	atomic64_t noa_programming_delay;
};

#endif /* _I915_PERF_TYPES_H_ */
