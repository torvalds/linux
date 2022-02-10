/*
 * Copyright © 2015-2016 Intel Corporation
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
 * Authors:
 *   Robert Bragg <robert@sixbynine.org>
 */


/**
 * DOC: i915 Perf Overview
 *
 * Gen graphics supports a large number of performance counters that can help
 * driver and application developers understand and optimize their use of the
 * GPU.
 *
 * This i915 perf interface enables userspace to configure and open a file
 * descriptor representing a stream of GPU metrics which can then be read() as
 * a stream of sample records.
 *
 * The interface is particularly suited to exposing buffered metrics that are
 * captured by DMA from the GPU, unsynchronized with and unrelated to the CPU.
 *
 * Streams representing a single context are accessible to applications with a
 * corresponding drm file descriptor, such that OpenGL can use the interface
 * without special privileges. Access to system-wide metrics requires root
 * privileges by default, unless changed via the dev.i915.perf_event_paranoid
 * sysctl option.
 *
 */

/**
 * DOC: i915 Perf History and Comparison with Core Perf
 *
 * The interface was initially inspired by the core Perf infrastructure but
 * some notable differences are:
 *
 * i915 perf file descriptors represent a "stream" instead of an "event"; where
 * a perf event primarily corresponds to a single 64bit value, while a stream
 * might sample sets of tightly-coupled counters, depending on the
 * configuration.  For example the Gen OA unit isn't designed to support
 * orthogonal configurations of individual counters; it's configured for a set
 * of related counters. Samples for an i915 perf stream capturing OA metrics
 * will include a set of counter values packed in a compact HW specific format.
 * The OA unit supports a number of different packing formats which can be
 * selected by the user opening the stream. Perf has support for grouping
 * events, but each event in the group is configured, validated and
 * authenticated individually with separate system calls.
 *
 * i915 perf stream configurations are provided as an array of u64 (key,value)
 * pairs, instead of a fixed struct with multiple miscellaneous config members,
 * interleaved with event-type specific members.
 *
 * i915 perf doesn't support exposing metrics via an mmap'd circular buffer.
 * The supported metrics are being written to memory by the GPU unsynchronized
 * with the CPU, using HW specific packing formats for counter sets. Sometimes
 * the constraints on HW configuration require reports to be filtered before it
 * would be acceptable to expose them to unprivileged applications - to hide
 * the metrics of other processes/contexts. For these use cases a read() based
 * interface is a good fit, and provides an opportunity to filter data as it
 * gets copied from the GPU mapped buffers to userspace buffers.
 *
 *
 * Issues hit with first prototype based on Core Perf
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * The first prototype of this driver was based on the core perf
 * infrastructure, and while we did make that mostly work, with some changes to
 * perf, we found we were breaking or working around too many assumptions baked
 * into perf's currently cpu centric design.
 *
 * In the end we didn't see a clear benefit to making perf's implementation and
 * interface more complex by changing design assumptions while we knew we still
 * wouldn't be able to use any existing perf based userspace tools.
 *
 * Also considering the Gen specific nature of the Observability hardware and
 * how userspace will sometimes need to combine i915 perf OA metrics with
 * side-band OA data captured via MI_REPORT_PERF_COUNT commands; we're
 * expecting the interface to be used by a platform specific userspace such as
 * OpenGL or tools. This is to say; we aren't inherently missing out on having
 * a standard vendor/architecture agnostic interface by not using perf.
 *
 *
 * For posterity, in case we might re-visit trying to adapt core perf to be
 * better suited to exposing i915 metrics these were the main pain points we
 * hit:
 *
 * - The perf based OA PMU driver broke some significant design assumptions:
 *
 *   Existing perf pmus are used for profiling work on a cpu and we were
 *   introducing the idea of _IS_DEVICE pmus with different security
 *   implications, the need to fake cpu-related data (such as user/kernel
 *   registers) to fit with perf's current design, and adding _DEVICE records
 *   as a way to forward device-specific status records.
 *
 *   The OA unit writes reports of counters into a circular buffer, without
 *   involvement from the CPU, making our PMU driver the first of a kind.
 *
 *   Given the way we were periodically forward data from the GPU-mapped, OA
 *   buffer to perf's buffer, those bursts of sample writes looked to perf like
 *   we were sampling too fast and so we had to subvert its throttling checks.
 *
 *   Perf supports groups of counters and allows those to be read via
 *   transactions internally but transactions currently seem designed to be
 *   explicitly initiated from the cpu (say in response to a userspace read())
 *   and while we could pull a report out of the OA buffer we can't
 *   trigger a report from the cpu on demand.
 *
 *   Related to being report based; the OA counters are configured in HW as a
 *   set while perf generally expects counter configurations to be orthogonal.
 *   Although counters can be associated with a group leader as they are
 *   opened, there's no clear precedent for being able to provide group-wide
 *   configuration attributes (for example we want to let userspace choose the
 *   OA unit report format used to capture all counters in a set, or specify a
 *   GPU context to filter metrics on). We avoided using perf's grouping
 *   feature and forwarded OA reports to userspace via perf's 'raw' sample
 *   field. This suited our userspace well considering how coupled the counters
 *   are when dealing with normalizing. It would be inconvenient to split
 *   counters up into separate events, only to require userspace to recombine
 *   them. For Mesa it's also convenient to be forwarded raw, periodic reports
 *   for combining with the side-band raw reports it captures using
 *   MI_REPORT_PERF_COUNT commands.
 *
 *   - As a side note on perf's grouping feature; there was also some concern
 *     that using PERF_FORMAT_GROUP as a way to pack together counter values
 *     would quite drastically inflate our sample sizes, which would likely
 *     lower the effective sampling resolutions we could use when the available
 *     memory bandwidth is limited.
 *
 *     With the OA unit's report formats, counters are packed together as 32
 *     or 40bit values, with the largest report size being 256 bytes.
 *
 *     PERF_FORMAT_GROUP values are 64bit, but there doesn't appear to be a
 *     documented ordering to the values, implying PERF_FORMAT_ID must also be
 *     used to add a 64bit ID before each value; giving 16 bytes per counter.
 *
 *   Related to counter orthogonality; we can't time share the OA unit, while
 *   event scheduling is a central design idea within perf for allowing
 *   userspace to open + enable more events than can be configured in HW at any
 *   one time.  The OA unit is not designed to allow re-configuration while in
 *   use. We can't reconfigure the OA unit without losing internal OA unit
 *   state which we can't access explicitly to save and restore. Reconfiguring
 *   the OA unit is also relatively slow, involving ~100 register writes. From
 *   userspace Mesa also depends on a stable OA configuration when emitting
 *   MI_REPORT_PERF_COUNT commands and importantly the OA unit can't be
 *   disabled while there are outstanding MI_RPC commands lest we hang the
 *   command streamer.
 *
 *   The contents of sample records aren't extensible by device drivers (i.e.
 *   the sample_type bits). As an example; Sourab Gupta had been looking to
 *   attach GPU timestamps to our OA samples. We were shoehorning OA reports
 *   into sample records by using the 'raw' field, but it's tricky to pack more
 *   than one thing into this field because events/core.c currently only lets a
 *   pmu give a single raw data pointer plus len which will be copied into the
 *   ring buffer. To include more than the OA report we'd have to copy the
 *   report into an intermediate larger buffer. I'd been considering allowing a
 *   vector of data+len values to be specified for copying the raw data, but
 *   it felt like a kludge to being using the raw field for this purpose.
 *
 * - It felt like our perf based PMU was making some technical compromises
 *   just for the sake of using perf:
 *
 *   perf_event_open() requires events to either relate to a pid or a specific
 *   cpu core, while our device pmu related to neither.  Events opened with a
 *   pid will be automatically enabled/disabled according to the scheduling of
 *   that process - so not appropriate for us. When an event is related to a
 *   cpu id, perf ensures pmu methods will be invoked via an inter process
 *   interrupt on that core. To avoid invasive changes our userspace opened OA
 *   perf events for a specific cpu. This was workable but it meant the
 *   majority of the OA driver ran in atomic context, including all OA report
 *   forwarding, which wasn't really necessary in our case and seems to make
 *   our locking requirements somewhat complex as we handled the interaction
 *   with the rest of the i915 driver.
 */

#include <linux/anon_inodes.h>
#include <linux/sizes.h>
#include <linux/uuid.h>

#include "gem/i915_gem_context.h"
#include "gem/i915_gem_internal.h"
#include "gt/intel_engine_pm.h"
#include "gt/intel_engine_regs.h"
#include "gt/intel_engine_user.h"
#include "gt/intel_execlists_submission.h"
#include "gt/intel_gpu_commands.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_clock_utils.h"
#include "gt/intel_gt_regs.h"
#include "gt/intel_lrc.h"
#include "gt/intel_ring.h"

#include "i915_drv.h"
#include "i915_file_private.h"
#include "i915_perf.h"
#include "i915_perf_oa_regs.h"

/* HW requires this to be a power of two, between 128k and 16M, though driver
 * is currently generally designed assuming the largest 16M size is used such
 * that the overflow cases are unlikely in normal operation.
 */
#define OA_BUFFER_SIZE		SZ_16M

#define OA_TAKEN(tail, head)	((tail - head) & (OA_BUFFER_SIZE - 1))

/**
 * DOC: OA Tail Pointer Race
 *
 * There's a HW race condition between OA unit tail pointer register updates and
 * writes to memory whereby the tail pointer can sometimes get ahead of what's
 * been written out to the OA buffer so far (in terms of what's visible to the
 * CPU).
 *
 * Although this can be observed explicitly while copying reports to userspace
 * by checking for a zeroed report-id field in tail reports, we want to account
 * for this earlier, as part of the oa_buffer_check_unlocked to avoid lots of
 * redundant read() attempts.
 *
 * We workaround this issue in oa_buffer_check_unlocked() by reading the reports
 * in the OA buffer, starting from the tail reported by the HW until we find a
 * report with its first 2 dwords not 0 meaning its previous report is
 * completely in memory and ready to be read. Those dwords are also set to 0
 * once read and the whole buffer is cleared upon OA buffer initialization. The
 * first dword is the reason for this report while the second is the timestamp,
 * making the chances of having those 2 fields at 0 fairly unlikely. A more
 * detailed explanation is available in oa_buffer_check_unlocked().
 *
 * Most of the implementation details for this workaround are in
 * oa_buffer_check_unlocked() and _append_oa_reports()
 *
 * Note for posterity: previously the driver used to define an effective tail
 * pointer that lagged the real pointer by a 'tail margin' measured in bytes
 * derived from %OA_TAIL_MARGIN_NSEC and the configured sampling frequency.
 * This was flawed considering that the OA unit may also automatically generate
 * non-periodic reports (such as on context switch) or the OA unit may be
 * enabled without any periodic sampling.
 */
#define OA_TAIL_MARGIN_NSEC	100000ULL
#define INVALID_TAIL_PTR	0xffffffff

/* The default frequency for checking whether the OA unit has written new
 * reports to the circular OA buffer...
 */
#define DEFAULT_POLL_FREQUENCY_HZ 200
#define DEFAULT_POLL_PERIOD_NS (NSEC_PER_SEC / DEFAULT_POLL_FREQUENCY_HZ)

/* for sysctl proc_dointvec_minmax of dev.i915.perf_stream_paranoid */
static u32 i915_perf_stream_paranoid = true;

/* The maximum exponent the hardware accepts is 63 (essentially it selects one
 * of the 64bit timestamp bits to trigger reports from) but there's currently
 * no known use case for sampling as infrequently as once per 47 thousand years.
 *
 * Since the timestamps included in OA reports are only 32bits it seems
 * reasonable to limit the OA exponent where it's still possible to account for
 * overflow in OA report timestamps.
 */
#define OA_EXPONENT_MAX 31

#define INVALID_CTX_ID 0xffffffff

/* On Gen8+ automatically triggered OA reports include a 'reason' field... */
#define OAREPORT_REASON_MASK           0x3f
#define OAREPORT_REASON_MASK_EXTENDED  0x7f
#define OAREPORT_REASON_SHIFT          19
#define OAREPORT_REASON_TIMER          (1<<0)
#define OAREPORT_REASON_CTX_SWITCH     (1<<3)
#define OAREPORT_REASON_CLK_RATIO      (1<<5)


/* For sysctl proc_dointvec_minmax of i915_oa_max_sample_rate
 *
 * The highest sampling frequency we can theoretically program the OA unit
 * with is always half the timestamp frequency: E.g. 6.25Mhz for Haswell.
 *
 * Initialized just before we register the sysctl parameter.
 */
static int oa_sample_rate_hard_limit;

/* Theoretically we can program the OA unit to sample every 160ns but don't
 * allow that by default unless root...
 *
 * The default threshold of 100000Hz is based on perf's similar
 * kernel.perf_event_max_sample_rate sysctl parameter.
 */
static u32 i915_oa_max_sample_rate = 100000;

/* XXX: beware if future OA HW adds new report formats that the current
 * code assumes all reports have a power-of-two size and ~(size - 1) can
 * be used as a mask to align the OA tail pointer.
 */
static const struct i915_oa_format oa_formats[I915_OA_FORMAT_MAX] = {
	[I915_OA_FORMAT_A13]	    = { 0, 64 },
	[I915_OA_FORMAT_A29]	    = { 1, 128 },
	[I915_OA_FORMAT_A13_B8_C8]  = { 2, 128 },
	/* A29_B8_C8 Disallowed as 192 bytes doesn't factor into buffer size */
	[I915_OA_FORMAT_B4_C8]	    = { 4, 64 },
	[I915_OA_FORMAT_A45_B8_C8]  = { 5, 256 },
	[I915_OA_FORMAT_B4_C8_A16]  = { 6, 128 },
	[I915_OA_FORMAT_C4_B8]	    = { 7, 64 },
	[I915_OA_FORMAT_A12]		    = { 0, 64 },
	[I915_OA_FORMAT_A12_B8_C8]	    = { 2, 128 },
	[I915_OA_FORMAT_A32u40_A4u32_B8_C8] = { 5, 256 },
};

#define SAMPLE_OA_REPORT      (1<<0)

/**
 * struct perf_open_properties - for validated properties given to open a stream
 * @sample_flags: `DRM_I915_PERF_PROP_SAMPLE_*` properties are tracked as flags
 * @single_context: Whether a single or all gpu contexts should be monitored
 * @hold_preemption: Whether the preemption is disabled for the filtered
 *                   context
 * @ctx_handle: A gem ctx handle for use with @single_context
 * @metrics_set: An ID for an OA unit metric set advertised via sysfs
 * @oa_format: An OA unit HW report format
 * @oa_periodic: Whether to enable periodic OA unit sampling
 * @oa_period_exponent: The OA unit sampling period is derived from this
 * @engine: The engine (typically rcs0) being monitored by the OA unit
 * @has_sseu: Whether @sseu was specified by userspace
 * @sseu: internal SSEU configuration computed either from the userspace
 *        specified configuration in the opening parameters or a default value
 *        (see get_default_sseu_config())
 * @poll_oa_period: The period in nanoseconds at which the CPU will check for OA
 * data availability
 *
 * As read_properties_unlocked() enumerates and validates the properties given
 * to open a stream of metrics the configuration is built up in the structure
 * which starts out zero initialized.
 */
struct perf_open_properties {
	u32 sample_flags;

	u64 single_context:1;
	u64 hold_preemption:1;
	u64 ctx_handle;

	/* OA sampling state */
	int metrics_set;
	int oa_format;
	bool oa_periodic;
	int oa_period_exponent;

	struct intel_engine_cs *engine;

	bool has_sseu;
	struct intel_sseu sseu;

	u64 poll_oa_period;
};

struct i915_oa_config_bo {
	struct llist_node node;

	struct i915_oa_config *oa_config;
	struct i915_vma *vma;
};

static struct ctl_table_header *sysctl_header;

static enum hrtimer_restart oa_poll_check_timer_cb(struct hrtimer *hrtimer);

void i915_oa_config_release(struct kref *ref)
{
	struct i915_oa_config *oa_config =
		container_of(ref, typeof(*oa_config), ref);

	kfree(oa_config->flex_regs);
	kfree(oa_config->b_counter_regs);
	kfree(oa_config->mux_regs);

	kfree_rcu(oa_config, rcu);
}

struct i915_oa_config *
i915_perf_get_oa_config(struct i915_perf *perf, int metrics_set)
{
	struct i915_oa_config *oa_config;

	rcu_read_lock();
	oa_config = idr_find(&perf->metrics_idr, metrics_set);
	if (oa_config)
		oa_config = i915_oa_config_get(oa_config);
	rcu_read_unlock();

	return oa_config;
}

static void free_oa_config_bo(struct i915_oa_config_bo *oa_bo)
{
	i915_oa_config_put(oa_bo->oa_config);
	i915_vma_put(oa_bo->vma);
	kfree(oa_bo);
}

static u32 gen12_oa_hw_tail_read(struct i915_perf_stream *stream)
{
	struct intel_uncore *uncore = stream->uncore;

	return intel_uncore_read(uncore, GEN12_OAG_OATAILPTR) &
	       GEN12_OAG_OATAILPTR_MASK;
}

static u32 gen8_oa_hw_tail_read(struct i915_perf_stream *stream)
{
	struct intel_uncore *uncore = stream->uncore;

	return intel_uncore_read(uncore, GEN8_OATAILPTR) & GEN8_OATAILPTR_MASK;
}

static u32 gen7_oa_hw_tail_read(struct i915_perf_stream *stream)
{
	struct intel_uncore *uncore = stream->uncore;
	u32 oastatus1 = intel_uncore_read(uncore, GEN7_OASTATUS1);

	return oastatus1 & GEN7_OASTATUS1_TAIL_MASK;
}

/**
 * oa_buffer_check_unlocked - check for data and update tail ptr state
 * @stream: i915 stream instance
 *
 * This is either called via fops (for blocking reads in user ctx) or the poll
 * check hrtimer (atomic ctx) to check the OA buffer tail pointer and check
 * if there is data available for userspace to read.
 *
 * This function is central to providing a workaround for the OA unit tail
 * pointer having a race with respect to what data is visible to the CPU.
 * It is responsible for reading tail pointers from the hardware and giving
 * the pointers time to 'age' before they are made available for reading.
 * (See description of OA_TAIL_MARGIN_NSEC above for further details.)
 *
 * Besides returning true when there is data available to read() this function
 * also updates the tail, aging_tail and aging_timestamp in the oa_buffer
 * object.
 *
 * Note: It's safe to read OA config state here unlocked, assuming that this is
 * only called while the stream is enabled, while the global OA configuration
 * can't be modified.
 *
 * Returns: %true if the OA buffer contains data, else %false
 */
static bool oa_buffer_check_unlocked(struct i915_perf_stream *stream)
{
	u32 gtt_offset = i915_ggtt_offset(stream->oa_buffer.vma);
	int report_size = stream->oa_buffer.format_size;
	unsigned long flags;
	bool pollin;
	u32 hw_tail;
	u64 now;

	/* We have to consider the (unlikely) possibility that read() errors
	 * could result in an OA buffer reset which might reset the head and
	 * tail state.
	 */
	spin_lock_irqsave(&stream->oa_buffer.ptr_lock, flags);

	hw_tail = stream->perf->ops.oa_hw_tail_read(stream);

	/* The tail pointer increases in 64 byte increments,
	 * not in report_size steps...
	 */
	hw_tail &= ~(report_size - 1);

	now = ktime_get_mono_fast_ns();

	if (hw_tail == stream->oa_buffer.aging_tail &&
	    (now - stream->oa_buffer.aging_timestamp) > OA_TAIL_MARGIN_NSEC) {
		/* If the HW tail hasn't move since the last check and the HW
		 * tail has been aging for long enough, declare it the new
		 * tail.
		 */
		stream->oa_buffer.tail = stream->oa_buffer.aging_tail;
	} else {
		u32 head, tail, aged_tail;

		/* NB: The head we observe here might effectively be a little
		 * out of date. If a read() is in progress, the head could be
		 * anywhere between this head and stream->oa_buffer.tail.
		 */
		head = stream->oa_buffer.head - gtt_offset;
		aged_tail = stream->oa_buffer.tail - gtt_offset;

		hw_tail -= gtt_offset;
		tail = hw_tail;

		/* Walk the stream backward until we find a report with dword 0
		 * & 1 not at 0. Since the circular buffer pointers progress by
		 * increments of 64 bytes and that reports can be up to 256
		 * bytes long, we can't tell whether a report has fully landed
		 * in memory before the first 2 dwords of the following report
		 * have effectively landed.
		 *
		 * This is assuming that the writes of the OA unit land in
		 * memory in the order they were written to.
		 * If not : (╯°□°）╯︵ ┻━┻
		 */
		while (OA_TAKEN(tail, aged_tail) >= report_size) {
			u32 *report32 = (void *)(stream->oa_buffer.vaddr + tail);

			if (report32[0] != 0 || report32[1] != 0)
				break;

			tail = (tail - report_size) & (OA_BUFFER_SIZE - 1);
		}

		if (OA_TAKEN(hw_tail, tail) > report_size &&
		    __ratelimit(&stream->perf->tail_pointer_race))
			DRM_NOTE("unlanded report(s) head=0x%x "
				 "tail=0x%x hw_tail=0x%x\n",
				 head, tail, hw_tail);

		stream->oa_buffer.tail = gtt_offset + tail;
		stream->oa_buffer.aging_tail = gtt_offset + hw_tail;
		stream->oa_buffer.aging_timestamp = now;
	}

	pollin = OA_TAKEN(stream->oa_buffer.tail - gtt_offset,
			  stream->oa_buffer.head - gtt_offset) >= report_size;

	spin_unlock_irqrestore(&stream->oa_buffer.ptr_lock, flags);

	return pollin;
}

/**
 * append_oa_status - Appends a status record to a userspace read() buffer.
 * @stream: An i915-perf stream opened for OA metrics
 * @buf: destination buffer given by userspace
 * @count: the number of bytes userspace wants to read
 * @offset: (inout): the current position for writing into @buf
 * @type: The kind of status to report to userspace
 *
 * Writes a status record (such as `DRM_I915_PERF_RECORD_OA_REPORT_LOST`)
 * into the userspace read() buffer.
 *
 * The @buf @offset will only be updated on success.
 *
 * Returns: 0 on success, negative error code on failure.
 */
static int append_oa_status(struct i915_perf_stream *stream,
			    char __user *buf,
			    size_t count,
			    size_t *offset,
			    enum drm_i915_perf_record_type type)
{
	struct drm_i915_perf_record_header header = { type, 0, sizeof(header) };

	if ((count - *offset) < header.size)
		return -ENOSPC;

	if (copy_to_user(buf + *offset, &header, sizeof(header)))
		return -EFAULT;

	(*offset) += header.size;

	return 0;
}

/**
 * append_oa_sample - Copies single OA report into userspace read() buffer.
 * @stream: An i915-perf stream opened for OA metrics
 * @buf: destination buffer given by userspace
 * @count: the number of bytes userspace wants to read
 * @offset: (inout): the current position for writing into @buf
 * @report: A single OA report to (optionally) include as part of the sample
 *
 * The contents of a sample are configured through `DRM_I915_PERF_PROP_SAMPLE_*`
 * properties when opening a stream, tracked as `stream->sample_flags`. This
 * function copies the requested components of a single sample to the given
 * read() @buf.
 *
 * The @buf @offset will only be updated on success.
 *
 * Returns: 0 on success, negative error code on failure.
 */
static int append_oa_sample(struct i915_perf_stream *stream,
			    char __user *buf,
			    size_t count,
			    size_t *offset,
			    const u8 *report)
{
	int report_size = stream->oa_buffer.format_size;
	struct drm_i915_perf_record_header header;

	header.type = DRM_I915_PERF_RECORD_SAMPLE;
	header.pad = 0;
	header.size = stream->sample_size;

	if ((count - *offset) < header.size)
		return -ENOSPC;

	buf += *offset;
	if (copy_to_user(buf, &header, sizeof(header)))
		return -EFAULT;
	buf += sizeof(header);

	if (copy_to_user(buf, report, report_size))
		return -EFAULT;

	(*offset) += header.size;

	return 0;
}

/**
 * gen8_append_oa_reports - Copies all buffered OA reports into
 *			    userspace read() buffer.
 * @stream: An i915-perf stream opened for OA metrics
 * @buf: destination buffer given by userspace
 * @count: the number of bytes userspace wants to read
 * @offset: (inout): the current position for writing into @buf
 *
 * Notably any error condition resulting in a short read (-%ENOSPC or
 * -%EFAULT) will be returned even though one or more records may
 * have been successfully copied. In this case it's up to the caller
 * to decide if the error should be squashed before returning to
 * userspace.
 *
 * Note: reports are consumed from the head, and appended to the
 * tail, so the tail chases the head?... If you think that's mad
 * and back-to-front you're not alone, but this follows the
 * Gen PRM naming convention.
 *
 * Returns: 0 on success, negative error code on failure.
 */
static int gen8_append_oa_reports(struct i915_perf_stream *stream,
				  char __user *buf,
				  size_t count,
				  size_t *offset)
{
	struct intel_uncore *uncore = stream->uncore;
	int report_size = stream->oa_buffer.format_size;
	u8 *oa_buf_base = stream->oa_buffer.vaddr;
	u32 gtt_offset = i915_ggtt_offset(stream->oa_buffer.vma);
	u32 mask = (OA_BUFFER_SIZE - 1);
	size_t start_offset = *offset;
	unsigned long flags;
	u32 head, tail;
	u32 taken;
	int ret = 0;

	if (drm_WARN_ON(&uncore->i915->drm, !stream->enabled))
		return -EIO;

	spin_lock_irqsave(&stream->oa_buffer.ptr_lock, flags);

	head = stream->oa_buffer.head;
	tail = stream->oa_buffer.tail;

	spin_unlock_irqrestore(&stream->oa_buffer.ptr_lock, flags);

	/*
	 * NB: oa_buffer.head/tail include the gtt_offset which we don't want
	 * while indexing relative to oa_buf_base.
	 */
	head -= gtt_offset;
	tail -= gtt_offset;

	/*
	 * An out of bounds or misaligned head or tail pointer implies a driver
	 * bug since we validate + align the tail pointers we read from the
	 * hardware and we are in full control of the head pointer which should
	 * only be incremented by multiples of the report size (notably also
	 * all a power of two).
	 */
	if (drm_WARN_ONCE(&uncore->i915->drm,
			  head > OA_BUFFER_SIZE || head % report_size ||
			  tail > OA_BUFFER_SIZE || tail % report_size,
			  "Inconsistent OA buffer pointers: head = %u, tail = %u\n",
			  head, tail))
		return -EIO;


	for (/* none */;
	     (taken = OA_TAKEN(tail, head));
	     head = (head + report_size) & mask) {
		u8 *report = oa_buf_base + head;
		u32 *report32 = (void *)report;
		u32 ctx_id;
		u32 reason;

		/*
		 * All the report sizes factor neatly into the buffer
		 * size so we never expect to see a report split
		 * between the beginning and end of the buffer.
		 *
		 * Given the initial alignment check a misalignment
		 * here would imply a driver bug that would result
		 * in an overrun.
		 */
		if (drm_WARN_ON(&uncore->i915->drm,
				(OA_BUFFER_SIZE - head) < report_size)) {
			drm_err(&uncore->i915->drm,
				"Spurious OA head ptr: non-integral report offset\n");
			break;
		}

		/*
		 * The reason field includes flags identifying what
		 * triggered this specific report (mostly timer
		 * triggered or e.g. due to a context switch).
		 *
		 * This field is never expected to be zero so we can
		 * check that the report isn't invalid before copying
		 * it to userspace...
		 */
		reason = ((report32[0] >> OAREPORT_REASON_SHIFT) &
			  (GRAPHICS_VER(stream->perf->i915) == 12 ?
			   OAREPORT_REASON_MASK_EXTENDED :
			   OAREPORT_REASON_MASK));

		ctx_id = report32[2] & stream->specific_ctx_id_mask;

		/*
		 * Squash whatever is in the CTX_ID field if it's marked as
		 * invalid to be sure we avoid false-positive, single-context
		 * filtering below...
		 *
		 * Note: that we don't clear the valid_ctx_bit so userspace can
		 * understand that the ID has been squashed by the kernel.
		 */
		if (!(report32[0] & stream->perf->gen8_valid_ctx_bit) &&
		    GRAPHICS_VER(stream->perf->i915) <= 11)
			ctx_id = report32[2] = INVALID_CTX_ID;

		/*
		 * NB: For Gen 8 the OA unit no longer supports clock gating
		 * off for a specific context and the kernel can't securely
		 * stop the counters from updating as system-wide / global
		 * values.
		 *
		 * Automatic reports now include a context ID so reports can be
		 * filtered on the cpu but it's not worth trying to
		 * automatically subtract/hide counter progress for other
		 * contexts while filtering since we can't stop userspace
		 * issuing MI_REPORT_PERF_COUNT commands which would still
		 * provide a side-band view of the real values.
		 *
		 * To allow userspace (such as Mesa/GL_INTEL_performance_query)
		 * to normalize counters for a single filtered context then it
		 * needs be forwarded bookend context-switch reports so that it
		 * can track switches in between MI_REPORT_PERF_COUNT commands
		 * and can itself subtract/ignore the progress of counters
		 * associated with other contexts. Note that the hardware
		 * automatically triggers reports when switching to a new
		 * context which are tagged with the ID of the newly active
		 * context. To avoid the complexity (and likely fragility) of
		 * reading ahead while parsing reports to try and minimize
		 * forwarding redundant context switch reports (i.e. between
		 * other, unrelated contexts) we simply elect to forward them
		 * all.
		 *
		 * We don't rely solely on the reason field to identify context
		 * switches since it's not-uncommon for periodic samples to
		 * identify a switch before any 'context switch' report.
		 */
		if (!stream->perf->exclusive_stream->ctx ||
		    stream->specific_ctx_id == ctx_id ||
		    stream->oa_buffer.last_ctx_id == stream->specific_ctx_id ||
		    reason & OAREPORT_REASON_CTX_SWITCH) {

			/*
			 * While filtering for a single context we avoid
			 * leaking the IDs of other contexts.
			 */
			if (stream->perf->exclusive_stream->ctx &&
			    stream->specific_ctx_id != ctx_id) {
				report32[2] = INVALID_CTX_ID;
			}

			ret = append_oa_sample(stream, buf, count, offset,
					       report);
			if (ret)
				break;

			stream->oa_buffer.last_ctx_id = ctx_id;
		}

		/*
		 * Clear out the first 2 dword as a mean to detect unlanded
		 * reports.
		 */
		report32[0] = 0;
		report32[1] = 0;
	}

	if (start_offset != *offset) {
		i915_reg_t oaheadptr;

		oaheadptr = GRAPHICS_VER(stream->perf->i915) == 12 ?
			    GEN12_OAG_OAHEADPTR : GEN8_OAHEADPTR;

		spin_lock_irqsave(&stream->oa_buffer.ptr_lock, flags);

		/*
		 * We removed the gtt_offset for the copy loop above, indexing
		 * relative to oa_buf_base so put back here...
		 */
		head += gtt_offset;
		intel_uncore_write(uncore, oaheadptr,
				   head & GEN12_OAG_OAHEADPTR_MASK);
		stream->oa_buffer.head = head;

		spin_unlock_irqrestore(&stream->oa_buffer.ptr_lock, flags);
	}

	return ret;
}

/**
 * gen8_oa_read - copy status records then buffered OA reports
 * @stream: An i915-perf stream opened for OA metrics
 * @buf: destination buffer given by userspace
 * @count: the number of bytes userspace wants to read
 * @offset: (inout): the current position for writing into @buf
 *
 * Checks OA unit status registers and if necessary appends corresponding
 * status records for userspace (such as for a buffer full condition) and then
 * initiate appending any buffered OA reports.
 *
 * Updates @offset according to the number of bytes successfully copied into
 * the userspace buffer.
 *
 * NB: some data may be successfully copied to the userspace buffer
 * even if an error is returned, and this is reflected in the
 * updated @offset.
 *
 * Returns: zero on success or a negative error code
 */
static int gen8_oa_read(struct i915_perf_stream *stream,
			char __user *buf,
			size_t count,
			size_t *offset)
{
	struct intel_uncore *uncore = stream->uncore;
	u32 oastatus;
	i915_reg_t oastatus_reg;
	int ret;

	if (drm_WARN_ON(&uncore->i915->drm, !stream->oa_buffer.vaddr))
		return -EIO;

	oastatus_reg = GRAPHICS_VER(stream->perf->i915) == 12 ?
		       GEN12_OAG_OASTATUS : GEN8_OASTATUS;

	oastatus = intel_uncore_read(uncore, oastatus_reg);

	/*
	 * We treat OABUFFER_OVERFLOW as a significant error:
	 *
	 * Although theoretically we could handle this more gracefully
	 * sometimes, some Gens don't correctly suppress certain
	 * automatically triggered reports in this condition and so we
	 * have to assume that old reports are now being trampled
	 * over.
	 *
	 * Considering how we don't currently give userspace control
	 * over the OA buffer size and always configure a large 16MB
	 * buffer, then a buffer overflow does anyway likely indicate
	 * that something has gone quite badly wrong.
	 */
	if (oastatus & GEN8_OASTATUS_OABUFFER_OVERFLOW) {
		ret = append_oa_status(stream, buf, count, offset,
				       DRM_I915_PERF_RECORD_OA_BUFFER_LOST);
		if (ret)
			return ret;

		DRM_DEBUG("OA buffer overflow (exponent = %d): force restart\n",
			  stream->period_exponent);

		stream->perf->ops.oa_disable(stream);
		stream->perf->ops.oa_enable(stream);

		/*
		 * Note: .oa_enable() is expected to re-init the oabuffer and
		 * reset GEN8_OASTATUS for us
		 */
		oastatus = intel_uncore_read(uncore, oastatus_reg);
	}

	if (oastatus & GEN8_OASTATUS_REPORT_LOST) {
		ret = append_oa_status(stream, buf, count, offset,
				       DRM_I915_PERF_RECORD_OA_REPORT_LOST);
		if (ret)
			return ret;

		intel_uncore_rmw(uncore, oastatus_reg,
				 GEN8_OASTATUS_COUNTER_OVERFLOW |
				 GEN8_OASTATUS_REPORT_LOST,
				 IS_GRAPHICS_VER(uncore->i915, 8, 11) ?
				 (GEN8_OASTATUS_HEAD_POINTER_WRAP |
				  GEN8_OASTATUS_TAIL_POINTER_WRAP) : 0);
	}

	return gen8_append_oa_reports(stream, buf, count, offset);
}

/**
 * gen7_append_oa_reports - Copies all buffered OA reports into
 *			    userspace read() buffer.
 * @stream: An i915-perf stream opened for OA metrics
 * @buf: destination buffer given by userspace
 * @count: the number of bytes userspace wants to read
 * @offset: (inout): the current position for writing into @buf
 *
 * Notably any error condition resulting in a short read (-%ENOSPC or
 * -%EFAULT) will be returned even though one or more records may
 * have been successfully copied. In this case it's up to the caller
 * to decide if the error should be squashed before returning to
 * userspace.
 *
 * Note: reports are consumed from the head, and appended to the
 * tail, so the tail chases the head?... If you think that's mad
 * and back-to-front you're not alone, but this follows the
 * Gen PRM naming convention.
 *
 * Returns: 0 on success, negative error code on failure.
 */
static int gen7_append_oa_reports(struct i915_perf_stream *stream,
				  char __user *buf,
				  size_t count,
				  size_t *offset)
{
	struct intel_uncore *uncore = stream->uncore;
	int report_size = stream->oa_buffer.format_size;
	u8 *oa_buf_base = stream->oa_buffer.vaddr;
	u32 gtt_offset = i915_ggtt_offset(stream->oa_buffer.vma);
	u32 mask = (OA_BUFFER_SIZE - 1);
	size_t start_offset = *offset;
	unsigned long flags;
	u32 head, tail;
	u32 taken;
	int ret = 0;

	if (drm_WARN_ON(&uncore->i915->drm, !stream->enabled))
		return -EIO;

	spin_lock_irqsave(&stream->oa_buffer.ptr_lock, flags);

	head = stream->oa_buffer.head;
	tail = stream->oa_buffer.tail;

	spin_unlock_irqrestore(&stream->oa_buffer.ptr_lock, flags);

	/* NB: oa_buffer.head/tail include the gtt_offset which we don't want
	 * while indexing relative to oa_buf_base.
	 */
	head -= gtt_offset;
	tail -= gtt_offset;

	/* An out of bounds or misaligned head or tail pointer implies a driver
	 * bug since we validate + align the tail pointers we read from the
	 * hardware and we are in full control of the head pointer which should
	 * only be incremented by multiples of the report size (notably also
	 * all a power of two).
	 */
	if (drm_WARN_ONCE(&uncore->i915->drm,
			  head > OA_BUFFER_SIZE || head % report_size ||
			  tail > OA_BUFFER_SIZE || tail % report_size,
			  "Inconsistent OA buffer pointers: head = %u, tail = %u\n",
			  head, tail))
		return -EIO;


	for (/* none */;
	     (taken = OA_TAKEN(tail, head));
	     head = (head + report_size) & mask) {
		u8 *report = oa_buf_base + head;
		u32 *report32 = (void *)report;

		/* All the report sizes factor neatly into the buffer
		 * size so we never expect to see a report split
		 * between the beginning and end of the buffer.
		 *
		 * Given the initial alignment check a misalignment
		 * here would imply a driver bug that would result
		 * in an overrun.
		 */
		if (drm_WARN_ON(&uncore->i915->drm,
				(OA_BUFFER_SIZE - head) < report_size)) {
			drm_err(&uncore->i915->drm,
				"Spurious OA head ptr: non-integral report offset\n");
			break;
		}

		/* The report-ID field for periodic samples includes
		 * some undocumented flags related to what triggered
		 * the report and is never expected to be zero so we
		 * can check that the report isn't invalid before
		 * copying it to userspace...
		 */
		if (report32[0] == 0) {
			if (__ratelimit(&stream->perf->spurious_report_rs))
				DRM_NOTE("Skipping spurious, invalid OA report\n");
			continue;
		}

		ret = append_oa_sample(stream, buf, count, offset, report);
		if (ret)
			break;

		/* Clear out the first 2 dwords as a mean to detect unlanded
		 * reports.
		 */
		report32[0] = 0;
		report32[1] = 0;
	}

	if (start_offset != *offset) {
		spin_lock_irqsave(&stream->oa_buffer.ptr_lock, flags);

		/* We removed the gtt_offset for the copy loop above, indexing
		 * relative to oa_buf_base so put back here...
		 */
		head += gtt_offset;

		intel_uncore_write(uncore, GEN7_OASTATUS2,
				   (head & GEN7_OASTATUS2_HEAD_MASK) |
				   GEN7_OASTATUS2_MEM_SELECT_GGTT);
		stream->oa_buffer.head = head;

		spin_unlock_irqrestore(&stream->oa_buffer.ptr_lock, flags);
	}

	return ret;
}

/**
 * gen7_oa_read - copy status records then buffered OA reports
 * @stream: An i915-perf stream opened for OA metrics
 * @buf: destination buffer given by userspace
 * @count: the number of bytes userspace wants to read
 * @offset: (inout): the current position for writing into @buf
 *
 * Checks Gen 7 specific OA unit status registers and if necessary appends
 * corresponding status records for userspace (such as for a buffer full
 * condition) and then initiate appending any buffered OA reports.
 *
 * Updates @offset according to the number of bytes successfully copied into
 * the userspace buffer.
 *
 * Returns: zero on success or a negative error code
 */
static int gen7_oa_read(struct i915_perf_stream *stream,
			char __user *buf,
			size_t count,
			size_t *offset)
{
	struct intel_uncore *uncore = stream->uncore;
	u32 oastatus1;
	int ret;

	if (drm_WARN_ON(&uncore->i915->drm, !stream->oa_buffer.vaddr))
		return -EIO;

	oastatus1 = intel_uncore_read(uncore, GEN7_OASTATUS1);

	/* XXX: On Haswell we don't have a safe way to clear oastatus1
	 * bits while the OA unit is enabled (while the tail pointer
	 * may be updated asynchronously) so we ignore status bits
	 * that have already been reported to userspace.
	 */
	oastatus1 &= ~stream->perf->gen7_latched_oastatus1;

	/* We treat OABUFFER_OVERFLOW as a significant error:
	 *
	 * - The status can be interpreted to mean that the buffer is
	 *   currently full (with a higher precedence than OA_TAKEN()
	 *   which will start to report a near-empty buffer after an
	 *   overflow) but it's awkward that we can't clear the status
	 *   on Haswell, so without a reset we won't be able to catch
	 *   the state again.
	 *
	 * - Since it also implies the HW has started overwriting old
	 *   reports it may also affect our sanity checks for invalid
	 *   reports when copying to userspace that assume new reports
	 *   are being written to cleared memory.
	 *
	 * - In the future we may want to introduce a flight recorder
	 *   mode where the driver will automatically maintain a safe
	 *   guard band between head/tail, avoiding this overflow
	 *   condition, but we avoid the added driver complexity for
	 *   now.
	 */
	if (unlikely(oastatus1 & GEN7_OASTATUS1_OABUFFER_OVERFLOW)) {
		ret = append_oa_status(stream, buf, count, offset,
				       DRM_I915_PERF_RECORD_OA_BUFFER_LOST);
		if (ret)
			return ret;

		DRM_DEBUG("OA buffer overflow (exponent = %d): force restart\n",
			  stream->period_exponent);

		stream->perf->ops.oa_disable(stream);
		stream->perf->ops.oa_enable(stream);

		oastatus1 = intel_uncore_read(uncore, GEN7_OASTATUS1);
	}

	if (unlikely(oastatus1 & GEN7_OASTATUS1_REPORT_LOST)) {
		ret = append_oa_status(stream, buf, count, offset,
				       DRM_I915_PERF_RECORD_OA_REPORT_LOST);
		if (ret)
			return ret;
		stream->perf->gen7_latched_oastatus1 |=
			GEN7_OASTATUS1_REPORT_LOST;
	}

	return gen7_append_oa_reports(stream, buf, count, offset);
}

/**
 * i915_oa_wait_unlocked - handles blocking IO until OA data available
 * @stream: An i915-perf stream opened for OA metrics
 *
 * Called when userspace tries to read() from a blocking stream FD opened
 * for OA metrics. It waits until the hrtimer callback finds a non-empty
 * OA buffer and wakes us.
 *
 * Note: it's acceptable to have this return with some false positives
 * since any subsequent read handling will return -EAGAIN if there isn't
 * really data ready for userspace yet.
 *
 * Returns: zero on success or a negative error code
 */
static int i915_oa_wait_unlocked(struct i915_perf_stream *stream)
{
	/* We would wait indefinitely if periodic sampling is not enabled */
	if (!stream->periodic)
		return -EIO;

	return wait_event_interruptible(stream->poll_wq,
					oa_buffer_check_unlocked(stream));
}

/**
 * i915_oa_poll_wait - call poll_wait() for an OA stream poll()
 * @stream: An i915-perf stream opened for OA metrics
 * @file: An i915 perf stream file
 * @wait: poll() state table
 *
 * For handling userspace polling on an i915 perf stream opened for OA metrics,
 * this starts a poll_wait with the wait queue that our hrtimer callback wakes
 * when it sees data ready to read in the circular OA buffer.
 */
static void i915_oa_poll_wait(struct i915_perf_stream *stream,
			      struct file *file,
			      poll_table *wait)
{
	poll_wait(file, &stream->poll_wq, wait);
}

/**
 * i915_oa_read - just calls through to &i915_oa_ops->read
 * @stream: An i915-perf stream opened for OA metrics
 * @buf: destination buffer given by userspace
 * @count: the number of bytes userspace wants to read
 * @offset: (inout): the current position for writing into @buf
 *
 * Updates @offset according to the number of bytes successfully copied into
 * the userspace buffer.
 *
 * Returns: zero on success or a negative error code
 */
static int i915_oa_read(struct i915_perf_stream *stream,
			char __user *buf,
			size_t count,
			size_t *offset)
{
	return stream->perf->ops.read(stream, buf, count, offset);
}

static struct intel_context *oa_pin_context(struct i915_perf_stream *stream)
{
	struct i915_gem_engines_iter it;
	struct i915_gem_context *ctx = stream->ctx;
	struct intel_context *ce;
	struct i915_gem_ww_ctx ww;
	int err = -ENODEV;

	for_each_gem_engine(ce, i915_gem_context_lock_engines(ctx), it) {
		if (ce->engine != stream->engine) /* first match! */
			continue;

		err = 0;
		break;
	}
	i915_gem_context_unlock_engines(ctx);

	if (err)
		return ERR_PTR(err);

	i915_gem_ww_ctx_init(&ww, true);
retry:
	/*
	 * As the ID is the gtt offset of the context's vma we
	 * pin the vma to ensure the ID remains fixed.
	 */
	err = intel_context_pin_ww(ce, &ww);
	if (err == -EDEADLK) {
		err = i915_gem_ww_ctx_backoff(&ww);
		if (!err)
			goto retry;
	}
	i915_gem_ww_ctx_fini(&ww);

	if (err)
		return ERR_PTR(err);

	stream->pinned_ctx = ce;
	return stream->pinned_ctx;
}

/**
 * oa_get_render_ctx_id - determine and hold ctx hw id
 * @stream: An i915-perf stream opened for OA metrics
 *
 * Determine the render context hw id, and ensure it remains fixed for the
 * lifetime of the stream. This ensures that we don't have to worry about
 * updating the context ID in OACONTROL on the fly.
 *
 * Returns: zero on success or a negative error code
 */
static int oa_get_render_ctx_id(struct i915_perf_stream *stream)
{
	struct intel_context *ce;

	ce = oa_pin_context(stream);
	if (IS_ERR(ce))
		return PTR_ERR(ce);

	switch (GRAPHICS_VER(ce->engine->i915)) {
	case 7: {
		/*
		 * On Haswell we don't do any post processing of the reports
		 * and don't need to use the mask.
		 */
		stream->specific_ctx_id = i915_ggtt_offset(ce->state);
		stream->specific_ctx_id_mask = 0;
		break;
	}

	case 8:
	case 9:
		if (intel_engine_uses_guc(ce->engine)) {
			/*
			 * When using GuC, the context descriptor we write in
			 * i915 is read by GuC and rewritten before it's
			 * actually written into the hardware. The LRCA is
			 * what is put into the context id field of the
			 * context descriptor by GuC. Because it's aligned to
			 * a page, the lower 12bits are always at 0 and
			 * dropped by GuC. They won't be part of the context
			 * ID in the OA reports, so squash those lower bits.
			 */
			stream->specific_ctx_id = ce->lrc.lrca >> 12;

			/*
			 * GuC uses the top bit to signal proxy submission, so
			 * ignore that bit.
			 */
			stream->specific_ctx_id_mask =
				(1U << (GEN8_CTX_ID_WIDTH - 1)) - 1;
		} else {
			stream->specific_ctx_id_mask =
				(1U << GEN8_CTX_ID_WIDTH) - 1;
			stream->specific_ctx_id = stream->specific_ctx_id_mask;
		}
		break;

	case 11:
	case 12:
		if (GRAPHICS_VER_FULL(ce->engine->i915) >= IP_VER(12, 50)) {
			stream->specific_ctx_id_mask =
				((1U << XEHP_SW_CTX_ID_WIDTH) - 1) <<
				(XEHP_SW_CTX_ID_SHIFT - 32);
			stream->specific_ctx_id =
				(XEHP_MAX_CONTEXT_HW_ID - 1) <<
				(XEHP_SW_CTX_ID_SHIFT - 32);
		} else {
			stream->specific_ctx_id_mask =
				((1U << GEN11_SW_CTX_ID_WIDTH) - 1) << (GEN11_SW_CTX_ID_SHIFT - 32);
			/*
			 * Pick an unused context id
			 * 0 - BITS_PER_LONG are used by other contexts
			 * GEN12_MAX_CONTEXT_HW_ID (0x7ff) is used by idle context
			 */
			stream->specific_ctx_id =
				(GEN12_MAX_CONTEXT_HW_ID - 1) << (GEN11_SW_CTX_ID_SHIFT - 32);
		}
		break;

	default:
		MISSING_CASE(GRAPHICS_VER(ce->engine->i915));
	}

	ce->tag = stream->specific_ctx_id;

	drm_dbg(&stream->perf->i915->drm,
		"filtering on ctx_id=0x%x ctx_id_mask=0x%x\n",
		stream->specific_ctx_id,
		stream->specific_ctx_id_mask);

	return 0;
}

/**
 * oa_put_render_ctx_id - counterpart to oa_get_render_ctx_id releases hold
 * @stream: An i915-perf stream opened for OA metrics
 *
 * In case anything needed doing to ensure the context HW ID would remain valid
 * for the lifetime of the stream, then that can be undone here.
 */
static void oa_put_render_ctx_id(struct i915_perf_stream *stream)
{
	struct intel_context *ce;

	ce = fetch_and_zero(&stream->pinned_ctx);
	if (ce) {
		ce->tag = 0; /* recomputed on next submission after parking */
		intel_context_unpin(ce);
	}

	stream->specific_ctx_id = INVALID_CTX_ID;
	stream->specific_ctx_id_mask = 0;
}

static void
free_oa_buffer(struct i915_perf_stream *stream)
{
	i915_vma_unpin_and_release(&stream->oa_buffer.vma,
				   I915_VMA_RELEASE_MAP);

	stream->oa_buffer.vaddr = NULL;
}

static void
free_oa_configs(struct i915_perf_stream *stream)
{
	struct i915_oa_config_bo *oa_bo, *tmp;

	i915_oa_config_put(stream->oa_config);
	llist_for_each_entry_safe(oa_bo, tmp, stream->oa_config_bos.first, node)
		free_oa_config_bo(oa_bo);
}

static void
free_noa_wait(struct i915_perf_stream *stream)
{
	i915_vma_unpin_and_release(&stream->noa_wait, 0);
}

static void i915_oa_stream_destroy(struct i915_perf_stream *stream)
{
	struct i915_perf *perf = stream->perf;

	BUG_ON(stream != perf->exclusive_stream);

	/*
	 * Unset exclusive_stream first, it will be checked while disabling
	 * the metric set on gen8+.
	 *
	 * See i915_oa_init_reg_state() and lrc_configure_all_contexts()
	 */
	WRITE_ONCE(perf->exclusive_stream, NULL);
	perf->ops.disable_metric_set(stream);

	free_oa_buffer(stream);

	intel_uncore_forcewake_put(stream->uncore, FORCEWAKE_ALL);
	intel_engine_pm_put(stream->engine);

	if (stream->ctx)
		oa_put_render_ctx_id(stream);

	free_oa_configs(stream);
	free_noa_wait(stream);

	if (perf->spurious_report_rs.missed) {
		DRM_NOTE("%d spurious OA report notices suppressed due to ratelimiting\n",
			 perf->spurious_report_rs.missed);
	}
}

static void gen7_init_oa_buffer(struct i915_perf_stream *stream)
{
	struct intel_uncore *uncore = stream->uncore;
	u32 gtt_offset = i915_ggtt_offset(stream->oa_buffer.vma);
	unsigned long flags;

	spin_lock_irqsave(&stream->oa_buffer.ptr_lock, flags);

	/* Pre-DevBDW: OABUFFER must be set with counters off,
	 * before OASTATUS1, but after OASTATUS2
	 */
	intel_uncore_write(uncore, GEN7_OASTATUS2, /* head */
			   gtt_offset | GEN7_OASTATUS2_MEM_SELECT_GGTT);
	stream->oa_buffer.head = gtt_offset;

	intel_uncore_write(uncore, GEN7_OABUFFER, gtt_offset);

	intel_uncore_write(uncore, GEN7_OASTATUS1, /* tail */
			   gtt_offset | OABUFFER_SIZE_16M);

	/* Mark that we need updated tail pointers to read from... */
	stream->oa_buffer.aging_tail = INVALID_TAIL_PTR;
	stream->oa_buffer.tail = gtt_offset;

	spin_unlock_irqrestore(&stream->oa_buffer.ptr_lock, flags);

	/* On Haswell we have to track which OASTATUS1 flags we've
	 * already seen since they can't be cleared while periodic
	 * sampling is enabled.
	 */
	stream->perf->gen7_latched_oastatus1 = 0;

	/* NB: although the OA buffer will initially be allocated
	 * zeroed via shmfs (and so this memset is redundant when
	 * first allocating), we may re-init the OA buffer, either
	 * when re-enabling a stream or in error/reset paths.
	 *
	 * The reason we clear the buffer for each re-init is for the
	 * sanity check in gen7_append_oa_reports() that looks at the
	 * report-id field to make sure it's non-zero which relies on
	 * the assumption that new reports are being written to zeroed
	 * memory...
	 */
	memset(stream->oa_buffer.vaddr, 0, OA_BUFFER_SIZE);
}

static void gen8_init_oa_buffer(struct i915_perf_stream *stream)
{
	struct intel_uncore *uncore = stream->uncore;
	u32 gtt_offset = i915_ggtt_offset(stream->oa_buffer.vma);
	unsigned long flags;

	spin_lock_irqsave(&stream->oa_buffer.ptr_lock, flags);

	intel_uncore_write(uncore, GEN8_OASTATUS, 0);
	intel_uncore_write(uncore, GEN8_OAHEADPTR, gtt_offset);
	stream->oa_buffer.head = gtt_offset;

	intel_uncore_write(uncore, GEN8_OABUFFER_UDW, 0);

	/*
	 * PRM says:
	 *
	 *  "This MMIO must be set before the OATAILPTR
	 *  register and after the OAHEADPTR register. This is
	 *  to enable proper functionality of the overflow
	 *  bit."
	 */
	intel_uncore_write(uncore, GEN8_OABUFFER, gtt_offset |
		   OABUFFER_SIZE_16M | GEN8_OABUFFER_MEM_SELECT_GGTT);
	intel_uncore_write(uncore, GEN8_OATAILPTR, gtt_offset & GEN8_OATAILPTR_MASK);

	/* Mark that we need updated tail pointers to read from... */
	stream->oa_buffer.aging_tail = INVALID_TAIL_PTR;
	stream->oa_buffer.tail = gtt_offset;

	/*
	 * Reset state used to recognise context switches, affecting which
	 * reports we will forward to userspace while filtering for a single
	 * context.
	 */
	stream->oa_buffer.last_ctx_id = INVALID_CTX_ID;

	spin_unlock_irqrestore(&stream->oa_buffer.ptr_lock, flags);

	/*
	 * NB: although the OA buffer will initially be allocated
	 * zeroed via shmfs (and so this memset is redundant when
	 * first allocating), we may re-init the OA buffer, either
	 * when re-enabling a stream or in error/reset paths.
	 *
	 * The reason we clear the buffer for each re-init is for the
	 * sanity check in gen8_append_oa_reports() that looks at the
	 * reason field to make sure it's non-zero which relies on
	 * the assumption that new reports are being written to zeroed
	 * memory...
	 */
	memset(stream->oa_buffer.vaddr, 0, OA_BUFFER_SIZE);
}

static void gen12_init_oa_buffer(struct i915_perf_stream *stream)
{
	struct intel_uncore *uncore = stream->uncore;
	u32 gtt_offset = i915_ggtt_offset(stream->oa_buffer.vma);
	unsigned long flags;

	spin_lock_irqsave(&stream->oa_buffer.ptr_lock, flags);

	intel_uncore_write(uncore, GEN12_OAG_OASTATUS, 0);
	intel_uncore_write(uncore, GEN12_OAG_OAHEADPTR,
			   gtt_offset & GEN12_OAG_OAHEADPTR_MASK);
	stream->oa_buffer.head = gtt_offset;

	/*
	 * PRM says:
	 *
	 *  "This MMIO must be set before the OATAILPTR
	 *  register and after the OAHEADPTR register. This is
	 *  to enable proper functionality of the overflow
	 *  bit."
	 */
	intel_uncore_write(uncore, GEN12_OAG_OABUFFER, gtt_offset |
			   OABUFFER_SIZE_16M | GEN8_OABUFFER_MEM_SELECT_GGTT);
	intel_uncore_write(uncore, GEN12_OAG_OATAILPTR,
			   gtt_offset & GEN12_OAG_OATAILPTR_MASK);

	/* Mark that we need updated tail pointers to read from... */
	stream->oa_buffer.aging_tail = INVALID_TAIL_PTR;
	stream->oa_buffer.tail = gtt_offset;

	/*
	 * Reset state used to recognise context switches, affecting which
	 * reports we will forward to userspace while filtering for a single
	 * context.
	 */
	stream->oa_buffer.last_ctx_id = INVALID_CTX_ID;

	spin_unlock_irqrestore(&stream->oa_buffer.ptr_lock, flags);

	/*
	 * NB: although the OA buffer will initially be allocated
	 * zeroed via shmfs (and so this memset is redundant when
	 * first allocating), we may re-init the OA buffer, either
	 * when re-enabling a stream or in error/reset paths.
	 *
	 * The reason we clear the buffer for each re-init is for the
	 * sanity check in gen8_append_oa_reports() that looks at the
	 * reason field to make sure it's non-zero which relies on
	 * the assumption that new reports are being written to zeroed
	 * memory...
	 */
	memset(stream->oa_buffer.vaddr, 0,
	       stream->oa_buffer.vma->size);
}

static int alloc_oa_buffer(struct i915_perf_stream *stream)
{
	struct drm_i915_private *i915 = stream->perf->i915;
	struct drm_i915_gem_object *bo;
	struct i915_vma *vma;
	int ret;

	if (drm_WARN_ON(&i915->drm, stream->oa_buffer.vma))
		return -ENODEV;

	BUILD_BUG_ON_NOT_POWER_OF_2(OA_BUFFER_SIZE);
	BUILD_BUG_ON(OA_BUFFER_SIZE < SZ_128K || OA_BUFFER_SIZE > SZ_16M);

	bo = i915_gem_object_create_shmem(stream->perf->i915, OA_BUFFER_SIZE);
	if (IS_ERR(bo)) {
		drm_err(&i915->drm, "Failed to allocate OA buffer\n");
		return PTR_ERR(bo);
	}

	i915_gem_object_set_cache_coherency(bo, I915_CACHE_LLC);

	/* PreHSW required 512K alignment, HSW requires 16M */
	vma = i915_gem_object_ggtt_pin(bo, NULL, 0, SZ_16M, 0);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto err_unref;
	}
	stream->oa_buffer.vma = vma;

	stream->oa_buffer.vaddr =
		i915_gem_object_pin_map_unlocked(bo, I915_MAP_WB);
	if (IS_ERR(stream->oa_buffer.vaddr)) {
		ret = PTR_ERR(stream->oa_buffer.vaddr);
		goto err_unpin;
	}

	return 0;

err_unpin:
	__i915_vma_unpin(vma);

err_unref:
	i915_gem_object_put(bo);

	stream->oa_buffer.vaddr = NULL;
	stream->oa_buffer.vma = NULL;

	return ret;
}

static u32 *save_restore_register(struct i915_perf_stream *stream, u32 *cs,
				  bool save, i915_reg_t reg, u32 offset,
				  u32 dword_count)
{
	u32 cmd;
	u32 d;

	cmd = save ? MI_STORE_REGISTER_MEM : MI_LOAD_REGISTER_MEM;
	cmd |= MI_SRM_LRM_GLOBAL_GTT;
	if (GRAPHICS_VER(stream->perf->i915) >= 8)
		cmd++;

	for (d = 0; d < dword_count; d++) {
		*cs++ = cmd;
		*cs++ = i915_mmio_reg_offset(reg) + 4 * d;
		*cs++ = intel_gt_scratch_offset(stream->engine->gt,
						offset) + 4 * d;
		*cs++ = 0;
	}

	return cs;
}

static int alloc_noa_wait(struct i915_perf_stream *stream)
{
	struct drm_i915_private *i915 = stream->perf->i915;
	struct drm_i915_gem_object *bo;
	struct i915_vma *vma;
	const u64 delay_ticks = 0xffffffffffffffff -
		intel_gt_ns_to_clock_interval(stream->perf->i915->ggtt.vm.gt,
					      atomic64_read(&stream->perf->noa_programming_delay));
	const u32 base = stream->engine->mmio_base;
#define CS_GPR(x) GEN8_RING_CS_GPR(base, x)
	u32 *batch, *ts0, *cs, *jump;
	struct i915_gem_ww_ctx ww;
	int ret, i;
	enum {
		START_TS,
		NOW_TS,
		DELTA_TS,
		JUMP_PREDICATE,
		DELTA_TARGET,
		N_CS_GPR
	};

	bo = i915_gem_object_create_internal(i915, 4096);
	if (IS_ERR(bo)) {
		drm_err(&i915->drm,
			"Failed to allocate NOA wait batchbuffer\n");
		return PTR_ERR(bo);
	}

	i915_gem_ww_ctx_init(&ww, true);
retry:
	ret = i915_gem_object_lock(bo, &ww);
	if (ret)
		goto out_ww;

	/*
	 * We pin in GGTT because we jump into this buffer now because
	 * multiple OA config BOs will have a jump to this address and it
	 * needs to be fixed during the lifetime of the i915/perf stream.
	 */
	vma = i915_gem_object_ggtt_pin_ww(bo, &ww, NULL, 0, 0, PIN_HIGH);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto out_ww;
	}

	batch = cs = i915_gem_object_pin_map(bo, I915_MAP_WB);
	if (IS_ERR(batch)) {
		ret = PTR_ERR(batch);
		goto err_unpin;
	}

	/* Save registers. */
	for (i = 0; i < N_CS_GPR; i++)
		cs = save_restore_register(
			stream, cs, true /* save */, CS_GPR(i),
			INTEL_GT_SCRATCH_FIELD_PERF_CS_GPR + 8 * i, 2);
	cs = save_restore_register(
		stream, cs, true /* save */, MI_PREDICATE_RESULT_1(RENDER_RING_BASE),
		INTEL_GT_SCRATCH_FIELD_PERF_PREDICATE_RESULT_1, 1);

	/* First timestamp snapshot location. */
	ts0 = cs;

	/*
	 * Initial snapshot of the timestamp register to implement the wait.
	 * We work with 32b values, so clear out the top 32b bits of the
	 * register because the ALU works 64bits.
	 */
	*cs++ = MI_LOAD_REGISTER_IMM(1);
	*cs++ = i915_mmio_reg_offset(CS_GPR(START_TS)) + 4;
	*cs++ = 0;
	*cs++ = MI_LOAD_REGISTER_REG | (3 - 2);
	*cs++ = i915_mmio_reg_offset(RING_TIMESTAMP(base));
	*cs++ = i915_mmio_reg_offset(CS_GPR(START_TS));

	/*
	 * This is the location we're going to jump back into until the
	 * required amount of time has passed.
	 */
	jump = cs;

	/*
	 * Take another snapshot of the timestamp register. Take care to clear
	 * up the top 32bits of CS_GPR(1) as we're using it for other
	 * operations below.
	 */
	*cs++ = MI_LOAD_REGISTER_IMM(1);
	*cs++ = i915_mmio_reg_offset(CS_GPR(NOW_TS)) + 4;
	*cs++ = 0;
	*cs++ = MI_LOAD_REGISTER_REG | (3 - 2);
	*cs++ = i915_mmio_reg_offset(RING_TIMESTAMP(base));
	*cs++ = i915_mmio_reg_offset(CS_GPR(NOW_TS));

	/*
	 * Do a diff between the 2 timestamps and store the result back into
	 * CS_GPR(1).
	 */
	*cs++ = MI_MATH(5);
	*cs++ = MI_MATH_LOAD(MI_MATH_REG_SRCA, MI_MATH_REG(NOW_TS));
	*cs++ = MI_MATH_LOAD(MI_MATH_REG_SRCB, MI_MATH_REG(START_TS));
	*cs++ = MI_MATH_SUB;
	*cs++ = MI_MATH_STORE(MI_MATH_REG(DELTA_TS), MI_MATH_REG_ACCU);
	*cs++ = MI_MATH_STORE(MI_MATH_REG(JUMP_PREDICATE), MI_MATH_REG_CF);

	/*
	 * Transfer the carry flag (set to 1 if ts1 < ts0, meaning the
	 * timestamp have rolled over the 32bits) into the predicate register
	 * to be used for the predicated jump.
	 */
	*cs++ = MI_LOAD_REGISTER_REG | (3 - 2);
	*cs++ = i915_mmio_reg_offset(CS_GPR(JUMP_PREDICATE));
	*cs++ = i915_mmio_reg_offset(MI_PREDICATE_RESULT_1(RENDER_RING_BASE));

	/* Restart from the beginning if we had timestamps roll over. */
	*cs++ = (GRAPHICS_VER(i915) < 8 ?
		 MI_BATCH_BUFFER_START :
		 MI_BATCH_BUFFER_START_GEN8) |
		MI_BATCH_PREDICATE;
	*cs++ = i915_ggtt_offset(vma) + (ts0 - batch) * 4;
	*cs++ = 0;

	/*
	 * Now add the diff between to previous timestamps and add it to :
	 *      (((1 * << 64) - 1) - delay_ns)
	 *
	 * When the Carry Flag contains 1 this means the elapsed time is
	 * longer than the expected delay, and we can exit the wait loop.
	 */
	*cs++ = MI_LOAD_REGISTER_IMM(2);
	*cs++ = i915_mmio_reg_offset(CS_GPR(DELTA_TARGET));
	*cs++ = lower_32_bits(delay_ticks);
	*cs++ = i915_mmio_reg_offset(CS_GPR(DELTA_TARGET)) + 4;
	*cs++ = upper_32_bits(delay_ticks);

	*cs++ = MI_MATH(4);
	*cs++ = MI_MATH_LOAD(MI_MATH_REG_SRCA, MI_MATH_REG(DELTA_TS));
	*cs++ = MI_MATH_LOAD(MI_MATH_REG_SRCB, MI_MATH_REG(DELTA_TARGET));
	*cs++ = MI_MATH_ADD;
	*cs++ = MI_MATH_STOREINV(MI_MATH_REG(JUMP_PREDICATE), MI_MATH_REG_CF);

	*cs++ = MI_ARB_CHECK;

	/*
	 * Transfer the result into the predicate register to be used for the
	 * predicated jump.
	 */
	*cs++ = MI_LOAD_REGISTER_REG | (3 - 2);
	*cs++ = i915_mmio_reg_offset(CS_GPR(JUMP_PREDICATE));
	*cs++ = i915_mmio_reg_offset(MI_PREDICATE_RESULT_1(RENDER_RING_BASE));

	/* Predicate the jump.  */
	*cs++ = (GRAPHICS_VER(i915) < 8 ?
		 MI_BATCH_BUFFER_START :
		 MI_BATCH_BUFFER_START_GEN8) |
		MI_BATCH_PREDICATE;
	*cs++ = i915_ggtt_offset(vma) + (jump - batch) * 4;
	*cs++ = 0;

	/* Restore registers. */
	for (i = 0; i < N_CS_GPR; i++)
		cs = save_restore_register(
			stream, cs, false /* restore */, CS_GPR(i),
			INTEL_GT_SCRATCH_FIELD_PERF_CS_GPR + 8 * i, 2);
	cs = save_restore_register(
		stream, cs, false /* restore */, MI_PREDICATE_RESULT_1(RENDER_RING_BASE),
		INTEL_GT_SCRATCH_FIELD_PERF_PREDICATE_RESULT_1, 1);

	/* And return to the ring. */
	*cs++ = MI_BATCH_BUFFER_END;

	GEM_BUG_ON(cs - batch > PAGE_SIZE / sizeof(*batch));

	i915_gem_object_flush_map(bo);
	__i915_gem_object_release_map(bo);

	stream->noa_wait = vma;
	goto out_ww;

err_unpin:
	i915_vma_unpin_and_release(&vma, 0);
out_ww:
	if (ret == -EDEADLK) {
		ret = i915_gem_ww_ctx_backoff(&ww);
		if (!ret)
			goto retry;
	}
	i915_gem_ww_ctx_fini(&ww);
	if (ret)
		i915_gem_object_put(bo);
	return ret;
}

static u32 *write_cs_mi_lri(u32 *cs,
			    const struct i915_oa_reg *reg_data,
			    u32 n_regs)
{
	u32 i;

	for (i = 0; i < n_regs; i++) {
		if ((i % MI_LOAD_REGISTER_IMM_MAX_REGS) == 0) {
			u32 n_lri = min_t(u32,
					  n_regs - i,
					  MI_LOAD_REGISTER_IMM_MAX_REGS);

			*cs++ = MI_LOAD_REGISTER_IMM(n_lri);
		}
		*cs++ = i915_mmio_reg_offset(reg_data[i].addr);
		*cs++ = reg_data[i].value;
	}

	return cs;
}

static int num_lri_dwords(int num_regs)
{
	int count = 0;

	if (num_regs > 0) {
		count += DIV_ROUND_UP(num_regs, MI_LOAD_REGISTER_IMM_MAX_REGS);
		count += num_regs * 2;
	}

	return count;
}

static struct i915_oa_config_bo *
alloc_oa_config_buffer(struct i915_perf_stream *stream,
		       struct i915_oa_config *oa_config)
{
	struct drm_i915_gem_object *obj;
	struct i915_oa_config_bo *oa_bo;
	struct i915_gem_ww_ctx ww;
	size_t config_length = 0;
	u32 *cs;
	int err;

	oa_bo = kzalloc(sizeof(*oa_bo), GFP_KERNEL);
	if (!oa_bo)
		return ERR_PTR(-ENOMEM);

	config_length += num_lri_dwords(oa_config->mux_regs_len);
	config_length += num_lri_dwords(oa_config->b_counter_regs_len);
	config_length += num_lri_dwords(oa_config->flex_regs_len);
	config_length += 3; /* MI_BATCH_BUFFER_START */
	config_length = ALIGN(sizeof(u32) * config_length, I915_GTT_PAGE_SIZE);

	obj = i915_gem_object_create_shmem(stream->perf->i915, config_length);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		goto err_free;
	}

	i915_gem_ww_ctx_init(&ww, true);
retry:
	err = i915_gem_object_lock(obj, &ww);
	if (err)
		goto out_ww;

	cs = i915_gem_object_pin_map(obj, I915_MAP_WB);
	if (IS_ERR(cs)) {
		err = PTR_ERR(cs);
		goto out_ww;
	}

	cs = write_cs_mi_lri(cs,
			     oa_config->mux_regs,
			     oa_config->mux_regs_len);
	cs = write_cs_mi_lri(cs,
			     oa_config->b_counter_regs,
			     oa_config->b_counter_regs_len);
	cs = write_cs_mi_lri(cs,
			     oa_config->flex_regs,
			     oa_config->flex_regs_len);

	/* Jump into the active wait. */
	*cs++ = (GRAPHICS_VER(stream->perf->i915) < 8 ?
		 MI_BATCH_BUFFER_START :
		 MI_BATCH_BUFFER_START_GEN8);
	*cs++ = i915_ggtt_offset(stream->noa_wait);
	*cs++ = 0;

	i915_gem_object_flush_map(obj);
	__i915_gem_object_release_map(obj);

	oa_bo->vma = i915_vma_instance(obj,
				       &stream->engine->gt->ggtt->vm,
				       NULL);
	if (IS_ERR(oa_bo->vma)) {
		err = PTR_ERR(oa_bo->vma);
		goto out_ww;
	}

	oa_bo->oa_config = i915_oa_config_get(oa_config);
	llist_add(&oa_bo->node, &stream->oa_config_bos);

out_ww:
	if (err == -EDEADLK) {
		err = i915_gem_ww_ctx_backoff(&ww);
		if (!err)
			goto retry;
	}
	i915_gem_ww_ctx_fini(&ww);

	if (err)
		i915_gem_object_put(obj);
err_free:
	if (err) {
		kfree(oa_bo);
		return ERR_PTR(err);
	}
	return oa_bo;
}

static struct i915_vma *
get_oa_vma(struct i915_perf_stream *stream, struct i915_oa_config *oa_config)
{
	struct i915_oa_config_bo *oa_bo;

	/*
	 * Look for the buffer in the already allocated BOs attached
	 * to the stream.
	 */
	llist_for_each_entry(oa_bo, stream->oa_config_bos.first, node) {
		if (oa_bo->oa_config == oa_config &&
		    memcmp(oa_bo->oa_config->uuid,
			   oa_config->uuid,
			   sizeof(oa_config->uuid)) == 0)
			goto out;
	}

	oa_bo = alloc_oa_config_buffer(stream, oa_config);
	if (IS_ERR(oa_bo))
		return ERR_CAST(oa_bo);

out:
	return i915_vma_get(oa_bo->vma);
}

static int
emit_oa_config(struct i915_perf_stream *stream,
	       struct i915_oa_config *oa_config,
	       struct intel_context *ce,
	       struct i915_active *active)
{
	struct i915_request *rq;
	struct i915_vma *vma;
	struct i915_gem_ww_ctx ww;
	int err;

	vma = get_oa_vma(stream, oa_config);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	i915_gem_ww_ctx_init(&ww, true);
retry:
	err = i915_gem_object_lock(vma->obj, &ww);
	if (err)
		goto err;

	err = i915_vma_pin_ww(vma, &ww, 0, 0, PIN_GLOBAL | PIN_HIGH);
	if (err)
		goto err;

	intel_engine_pm_get(ce->engine);
	rq = i915_request_create(ce);
	intel_engine_pm_put(ce->engine);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_vma_unpin;
	}

	if (!IS_ERR_OR_NULL(active)) {
		/* After all individual context modifications */
		err = i915_request_await_active(rq, active,
						I915_ACTIVE_AWAIT_ACTIVE);
		if (err)
			goto err_add_request;

		err = i915_active_add_request(active, rq);
		if (err)
			goto err_add_request;
	}

	err = i915_request_await_object(rq, vma->obj, 0);
	if (!err)
		err = i915_vma_move_to_active(vma, rq, 0);
	if (err)
		goto err_add_request;

	err = rq->engine->emit_bb_start(rq,
					vma->node.start, 0,
					I915_DISPATCH_SECURE);
	if (err)
		goto err_add_request;

err_add_request:
	i915_request_add(rq);
err_vma_unpin:
	i915_vma_unpin(vma);
err:
	if (err == -EDEADLK) {
		err = i915_gem_ww_ctx_backoff(&ww);
		if (!err)
			goto retry;
	}

	i915_gem_ww_ctx_fini(&ww);
	i915_vma_put(vma);
	return err;
}

static struct intel_context *oa_context(struct i915_perf_stream *stream)
{
	return stream->pinned_ctx ?: stream->engine->kernel_context;
}

static int
hsw_enable_metric_set(struct i915_perf_stream *stream,
		      struct i915_active *active)
{
	struct intel_uncore *uncore = stream->uncore;

	/*
	 * PRM:
	 *
	 * OA unit is using “crclk” for its functionality. When trunk
	 * level clock gating takes place, OA clock would be gated,
	 * unable to count the events from non-render clock domain.
	 * Render clock gating must be disabled when OA is enabled to
	 * count the events from non-render domain. Unit level clock
	 * gating for RCS should also be disabled.
	 */
	intel_uncore_rmw(uncore, GEN7_MISCCPCTL,
			 GEN7_DOP_CLOCK_GATE_ENABLE, 0);
	intel_uncore_rmw(uncore, GEN6_UCGCTL1,
			 0, GEN6_CSUNIT_CLOCK_GATE_DISABLE);

	return emit_oa_config(stream,
			      stream->oa_config, oa_context(stream),
			      active);
}

static void hsw_disable_metric_set(struct i915_perf_stream *stream)
{
	struct intel_uncore *uncore = stream->uncore;

	intel_uncore_rmw(uncore, GEN6_UCGCTL1,
			 GEN6_CSUNIT_CLOCK_GATE_DISABLE, 0);
	intel_uncore_rmw(uncore, GEN7_MISCCPCTL,
			 0, GEN7_DOP_CLOCK_GATE_ENABLE);

	intel_uncore_rmw(uncore, GDT_CHICKEN_BITS, GT_NOA_ENABLE, 0);
}

static u32 oa_config_flex_reg(const struct i915_oa_config *oa_config,
			      i915_reg_t reg)
{
	u32 mmio = i915_mmio_reg_offset(reg);
	int i;

	/*
	 * This arbitrary default will select the 'EU FPU0 Pipeline
	 * Active' event. In the future it's anticipated that there
	 * will be an explicit 'No Event' we can select, but not yet...
	 */
	if (!oa_config)
		return 0;

	for (i = 0; i < oa_config->flex_regs_len; i++) {
		if (i915_mmio_reg_offset(oa_config->flex_regs[i].addr) == mmio)
			return oa_config->flex_regs[i].value;
	}

	return 0;
}
/*
 * NB: It must always remain pointer safe to run this even if the OA unit
 * has been disabled.
 *
 * It's fine to put out-of-date values into these per-context registers
 * in the case that the OA unit has been disabled.
 */
static void
gen8_update_reg_state_unlocked(const struct intel_context *ce,
			       const struct i915_perf_stream *stream)
{
	u32 ctx_oactxctrl = stream->perf->ctx_oactxctrl_offset;
	u32 ctx_flexeu0 = stream->perf->ctx_flexeu0_offset;
	/* The MMIO offsets for Flex EU registers aren't contiguous */
	i915_reg_t flex_regs[] = {
		EU_PERF_CNTL0,
		EU_PERF_CNTL1,
		EU_PERF_CNTL2,
		EU_PERF_CNTL3,
		EU_PERF_CNTL4,
		EU_PERF_CNTL5,
		EU_PERF_CNTL6,
	};
	u32 *reg_state = ce->lrc_reg_state;
	int i;

	reg_state[ctx_oactxctrl + 1] =
		(stream->period_exponent << GEN8_OA_TIMER_PERIOD_SHIFT) |
		(stream->periodic ? GEN8_OA_TIMER_ENABLE : 0) |
		GEN8_OA_COUNTER_RESUME;

	for (i = 0; i < ARRAY_SIZE(flex_regs); i++)
		reg_state[ctx_flexeu0 + i * 2 + 1] =
			oa_config_flex_reg(stream->oa_config, flex_regs[i]);
}

struct flex {
	i915_reg_t reg;
	u32 offset;
	u32 value;
};

static int
gen8_store_flex(struct i915_request *rq,
		struct intel_context *ce,
		const struct flex *flex, unsigned int count)
{
	u32 offset;
	u32 *cs;

	cs = intel_ring_begin(rq, 4 * count);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	offset = i915_ggtt_offset(ce->state) + LRC_STATE_OFFSET;
	do {
		*cs++ = MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT;
		*cs++ = offset + flex->offset * sizeof(u32);
		*cs++ = 0;
		*cs++ = flex->value;
	} while (flex++, --count);

	intel_ring_advance(rq, cs);

	return 0;
}

static int
gen8_load_flex(struct i915_request *rq,
	       struct intel_context *ce,
	       const struct flex *flex, unsigned int count)
{
	u32 *cs;

	GEM_BUG_ON(!count || count > 63);

	cs = intel_ring_begin(rq, 2 * count + 2);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = MI_LOAD_REGISTER_IMM(count);
	do {
		*cs++ = i915_mmio_reg_offset(flex->reg);
		*cs++ = flex->value;
	} while (flex++, --count);
	*cs++ = MI_NOOP;

	intel_ring_advance(rq, cs);

	return 0;
}

static int gen8_modify_context(struct intel_context *ce,
			       const struct flex *flex, unsigned int count)
{
	struct i915_request *rq;
	int err;

	rq = intel_engine_create_kernel_request(ce->engine);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	/* Serialise with the remote context */
	err = intel_context_prepare_remote_request(ce, rq);
	if (err == 0)
		err = gen8_store_flex(rq, ce, flex, count);

	i915_request_add(rq);
	return err;
}

static int
gen8_modify_self(struct intel_context *ce,
		 const struct flex *flex, unsigned int count,
		 struct i915_active *active)
{
	struct i915_request *rq;
	int err;

	intel_engine_pm_get(ce->engine);
	rq = i915_request_create(ce);
	intel_engine_pm_put(ce->engine);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	if (!IS_ERR_OR_NULL(active)) {
		err = i915_active_add_request(active, rq);
		if (err)
			goto err_add_request;
	}

	err = gen8_load_flex(rq, ce, flex, count);
	if (err)
		goto err_add_request;

err_add_request:
	i915_request_add(rq);
	return err;
}

static int gen8_configure_context(struct i915_gem_context *ctx,
				  struct flex *flex, unsigned int count)
{
	struct i915_gem_engines_iter it;
	struct intel_context *ce;
	int err = 0;

	for_each_gem_engine(ce, i915_gem_context_lock_engines(ctx), it) {
		GEM_BUG_ON(ce == ce->engine->kernel_context);

		if (ce->engine->class != RENDER_CLASS)
			continue;

		/* Otherwise OA settings will be set upon first use */
		if (!intel_context_pin_if_active(ce))
			continue;

		flex->value = intel_sseu_make_rpcs(ce->engine->gt, &ce->sseu);
		err = gen8_modify_context(ce, flex, count);

		intel_context_unpin(ce);
		if (err)
			break;
	}
	i915_gem_context_unlock_engines(ctx);

	return err;
}

static int gen12_configure_oar_context(struct i915_perf_stream *stream,
				       struct i915_active *active)
{
	int err;
	struct intel_context *ce = stream->pinned_ctx;
	u32 format = stream->oa_buffer.format;
	struct flex regs_context[] = {
		{
			GEN8_OACTXCONTROL,
			stream->perf->ctx_oactxctrl_offset + 1,
			active ? GEN8_OA_COUNTER_RESUME : 0,
		},
	};
	/* Offsets in regs_lri are not used since this configuration is only
	 * applied using LRI. Initialize the correct offsets for posterity.
	 */
#define GEN12_OAR_OACONTROL_OFFSET 0x5B0
	struct flex regs_lri[] = {
		{
			GEN12_OAR_OACONTROL,
			GEN12_OAR_OACONTROL_OFFSET + 1,
			(format << GEN12_OAR_OACONTROL_COUNTER_FORMAT_SHIFT) |
			(active ? GEN12_OAR_OACONTROL_COUNTER_ENABLE : 0)
		},
		{
			RING_CONTEXT_CONTROL(ce->engine->mmio_base),
			CTX_CONTEXT_CONTROL,
			_MASKED_FIELD(GEN12_CTX_CTRL_OAR_CONTEXT_ENABLE,
				      active ?
				      GEN12_CTX_CTRL_OAR_CONTEXT_ENABLE :
				      0)
		},
	};

	/* Modify the context image of pinned context with regs_context*/
	err = intel_context_lock_pinned(ce);
	if (err)
		return err;

	err = gen8_modify_context(ce, regs_context, ARRAY_SIZE(regs_context));
	intel_context_unlock_pinned(ce);
	if (err)
		return err;

	/* Apply regs_lri using LRI with pinned context */
	return gen8_modify_self(ce, regs_lri, ARRAY_SIZE(regs_lri), active);
}

/*
 * Manages updating the per-context aspects of the OA stream
 * configuration across all contexts.
 *
 * The awkward consideration here is that OACTXCONTROL controls the
 * exponent for periodic sampling which is primarily used for system
 * wide profiling where we'd like a consistent sampling period even in
 * the face of context switches.
 *
 * Our approach of updating the register state context (as opposed to
 * say using a workaround batch buffer) ensures that the hardware
 * won't automatically reload an out-of-date timer exponent even
 * transiently before a WA BB could be parsed.
 *
 * This function needs to:
 * - Ensure the currently running context's per-context OA state is
 *   updated
 * - Ensure that all existing contexts will have the correct per-context
 *   OA state if they are scheduled for use.
 * - Ensure any new contexts will be initialized with the correct
 *   per-context OA state.
 *
 * Note: it's only the RCS/Render context that has any OA state.
 * Note: the first flex register passed must always be R_PWR_CLK_STATE
 */
static int
oa_configure_all_contexts(struct i915_perf_stream *stream,
			  struct flex *regs,
			  size_t num_regs,
			  struct i915_active *active)
{
	struct drm_i915_private *i915 = stream->perf->i915;
	struct intel_engine_cs *engine;
	struct i915_gem_context *ctx, *cn;
	int err;

	lockdep_assert_held(&stream->perf->lock);

	/*
	 * The OA register config is setup through the context image. This image
	 * might be written to by the GPU on context switch (in particular on
	 * lite-restore). This means we can't safely update a context's image,
	 * if this context is scheduled/submitted to run on the GPU.
	 *
	 * We could emit the OA register config through the batch buffer but
	 * this might leave small interval of time where the OA unit is
	 * configured at an invalid sampling period.
	 *
	 * Note that since we emit all requests from a single ring, there
	 * is still an implicit global barrier here that may cause a high
	 * priority context to wait for an otherwise independent low priority
	 * context. Contexts idle at the time of reconfiguration are not
	 * trapped behind the barrier.
	 */
	spin_lock(&i915->gem.contexts.lock);
	list_for_each_entry_safe(ctx, cn, &i915->gem.contexts.list, link) {
		if (!kref_get_unless_zero(&ctx->ref))
			continue;

		spin_unlock(&i915->gem.contexts.lock);

		err = gen8_configure_context(ctx, regs, num_regs);
		if (err) {
			i915_gem_context_put(ctx);
			return err;
		}

		spin_lock(&i915->gem.contexts.lock);
		list_safe_reset_next(ctx, cn, link);
		i915_gem_context_put(ctx);
	}
	spin_unlock(&i915->gem.contexts.lock);

	/*
	 * After updating all other contexts, we need to modify ourselves.
	 * If we don't modify the kernel_context, we do not get events while
	 * idle.
	 */
	for_each_uabi_engine(engine, i915) {
		struct intel_context *ce = engine->kernel_context;

		if (engine->class != RENDER_CLASS)
			continue;

		regs[0].value = intel_sseu_make_rpcs(engine->gt, &ce->sseu);

		err = gen8_modify_self(ce, regs, num_regs, active);
		if (err)
			return err;
	}

	return 0;
}

static int
gen12_configure_all_contexts(struct i915_perf_stream *stream,
			     const struct i915_oa_config *oa_config,
			     struct i915_active *active)
{
	struct flex regs[] = {
		{
			GEN8_R_PWR_CLK_STATE(RENDER_RING_BASE),
			CTX_R_PWR_CLK_STATE,
		},
	};

	return oa_configure_all_contexts(stream,
					 regs, ARRAY_SIZE(regs),
					 active);
}

static int
lrc_configure_all_contexts(struct i915_perf_stream *stream,
			   const struct i915_oa_config *oa_config,
			   struct i915_active *active)
{
	/* The MMIO offsets for Flex EU registers aren't contiguous */
	const u32 ctx_flexeu0 = stream->perf->ctx_flexeu0_offset;
#define ctx_flexeuN(N) (ctx_flexeu0 + 2 * (N) + 1)
	struct flex regs[] = {
		{
			GEN8_R_PWR_CLK_STATE(RENDER_RING_BASE),
			CTX_R_PWR_CLK_STATE,
		},
		{
			GEN8_OACTXCONTROL,
			stream->perf->ctx_oactxctrl_offset + 1,
		},
		{ EU_PERF_CNTL0, ctx_flexeuN(0) },
		{ EU_PERF_CNTL1, ctx_flexeuN(1) },
		{ EU_PERF_CNTL2, ctx_flexeuN(2) },
		{ EU_PERF_CNTL3, ctx_flexeuN(3) },
		{ EU_PERF_CNTL4, ctx_flexeuN(4) },
		{ EU_PERF_CNTL5, ctx_flexeuN(5) },
		{ EU_PERF_CNTL6, ctx_flexeuN(6) },
	};
#undef ctx_flexeuN
	int i;

	regs[1].value =
		(stream->period_exponent << GEN8_OA_TIMER_PERIOD_SHIFT) |
		(stream->periodic ? GEN8_OA_TIMER_ENABLE : 0) |
		GEN8_OA_COUNTER_RESUME;

	for (i = 2; i < ARRAY_SIZE(regs); i++)
		regs[i].value = oa_config_flex_reg(oa_config, regs[i].reg);

	return oa_configure_all_contexts(stream,
					 regs, ARRAY_SIZE(regs),
					 active);
}

static int
gen8_enable_metric_set(struct i915_perf_stream *stream,
		       struct i915_active *active)
{
	struct intel_uncore *uncore = stream->uncore;
	struct i915_oa_config *oa_config = stream->oa_config;
	int ret;

	/*
	 * We disable slice/unslice clock ratio change reports on SKL since
	 * they are too noisy. The HW generates a lot of redundant reports
	 * where the ratio hasn't really changed causing a lot of redundant
	 * work to processes and increasing the chances we'll hit buffer
	 * overruns.
	 *
	 * Although we don't currently use the 'disable overrun' OABUFFER
	 * feature it's worth noting that clock ratio reports have to be
	 * disabled before considering to use that feature since the HW doesn't
	 * correctly block these reports.
	 *
	 * Currently none of the high-level metrics we have depend on knowing
	 * this ratio to normalize.
	 *
	 * Note: This register is not power context saved and restored, but
	 * that's OK considering that we disable RC6 while the OA unit is
	 * enabled.
	 *
	 * The _INCLUDE_CLK_RATIO bit allows the slice/unslice frequency to
	 * be read back from automatically triggered reports, as part of the
	 * RPT_ID field.
	 */
	if (IS_GRAPHICS_VER(stream->perf->i915, 9, 11)) {
		intel_uncore_write(uncore, GEN8_OA_DEBUG,
				   _MASKED_BIT_ENABLE(GEN9_OA_DEBUG_DISABLE_CLK_RATIO_REPORTS |
						      GEN9_OA_DEBUG_INCLUDE_CLK_RATIO));
	}

	/*
	 * Update all contexts prior writing the mux configurations as we need
	 * to make sure all slices/subslices are ON before writing to NOA
	 * registers.
	 */
	ret = lrc_configure_all_contexts(stream, oa_config, active);
	if (ret)
		return ret;

	return emit_oa_config(stream,
			      stream->oa_config, oa_context(stream),
			      active);
}

static u32 oag_report_ctx_switches(const struct i915_perf_stream *stream)
{
	return _MASKED_FIELD(GEN12_OAG_OA_DEBUG_DISABLE_CTX_SWITCH_REPORTS,
			     (stream->sample_flags & SAMPLE_OA_REPORT) ?
			     0 : GEN12_OAG_OA_DEBUG_DISABLE_CTX_SWITCH_REPORTS);
}

static int
gen12_enable_metric_set(struct i915_perf_stream *stream,
			struct i915_active *active)
{
	struct intel_uncore *uncore = stream->uncore;
	struct i915_oa_config *oa_config = stream->oa_config;
	bool periodic = stream->periodic;
	u32 period_exponent = stream->period_exponent;
	int ret;

	intel_uncore_write(uncore, GEN12_OAG_OA_DEBUG,
			   /* Disable clk ratio reports, like previous Gens. */
			   _MASKED_BIT_ENABLE(GEN12_OAG_OA_DEBUG_DISABLE_CLK_RATIO_REPORTS |
					      GEN12_OAG_OA_DEBUG_INCLUDE_CLK_RATIO) |
			   /*
			    * If the user didn't require OA reports, instruct
			    * the hardware not to emit ctx switch reports.
			    */
			   oag_report_ctx_switches(stream));

	intel_uncore_write(uncore, GEN12_OAG_OAGLBCTXCTRL, periodic ?
			   (GEN12_OAG_OAGLBCTXCTRL_COUNTER_RESUME |
			    GEN12_OAG_OAGLBCTXCTRL_TIMER_ENABLE |
			    (period_exponent << GEN12_OAG_OAGLBCTXCTRL_TIMER_PERIOD_SHIFT))
			    : 0);

	/*
	 * Update all contexts prior writing the mux configurations as we need
	 * to make sure all slices/subslices are ON before writing to NOA
	 * registers.
	 */
	ret = gen12_configure_all_contexts(stream, oa_config, active);
	if (ret)
		return ret;

	/*
	 * For Gen12, performance counters are context
	 * saved/restored. Only enable it for the context that
	 * requested this.
	 */
	if (stream->ctx) {
		ret = gen12_configure_oar_context(stream, active);
		if (ret)
			return ret;
	}

	return emit_oa_config(stream,
			      stream->oa_config, oa_context(stream),
			      active);
}

static void gen8_disable_metric_set(struct i915_perf_stream *stream)
{
	struct intel_uncore *uncore = stream->uncore;

	/* Reset all contexts' slices/subslices configurations. */
	lrc_configure_all_contexts(stream, NULL, NULL);

	intel_uncore_rmw(uncore, GDT_CHICKEN_BITS, GT_NOA_ENABLE, 0);
}

static void gen11_disable_metric_set(struct i915_perf_stream *stream)
{
	struct intel_uncore *uncore = stream->uncore;

	/* Reset all contexts' slices/subslices configurations. */
	lrc_configure_all_contexts(stream, NULL, NULL);

	/* Make sure we disable noa to save power. */
	intel_uncore_rmw(uncore, RPM_CONFIG1, GEN10_GT_NOA_ENABLE, 0);
}

static void gen12_disable_metric_set(struct i915_perf_stream *stream)
{
	struct intel_uncore *uncore = stream->uncore;

	/* Reset all contexts' slices/subslices configurations. */
	gen12_configure_all_contexts(stream, NULL, NULL);

	/* disable the context save/restore or OAR counters */
	if (stream->ctx)
		gen12_configure_oar_context(stream, NULL);

	/* Make sure we disable noa to save power. */
	intel_uncore_rmw(uncore, RPM_CONFIG1, GEN10_GT_NOA_ENABLE, 0);
}

static void gen7_oa_enable(struct i915_perf_stream *stream)
{
	struct intel_uncore *uncore = stream->uncore;
	struct i915_gem_context *ctx = stream->ctx;
	u32 ctx_id = stream->specific_ctx_id;
	bool periodic = stream->periodic;
	u32 period_exponent = stream->period_exponent;
	u32 report_format = stream->oa_buffer.format;

	/*
	 * Reset buf pointers so we don't forward reports from before now.
	 *
	 * Think carefully if considering trying to avoid this, since it
	 * also ensures status flags and the buffer itself are cleared
	 * in error paths, and we have checks for invalid reports based
	 * on the assumption that certain fields are written to zeroed
	 * memory which this helps maintains.
	 */
	gen7_init_oa_buffer(stream);

	intel_uncore_write(uncore, GEN7_OACONTROL,
			   (ctx_id & GEN7_OACONTROL_CTX_MASK) |
			   (period_exponent <<
			    GEN7_OACONTROL_TIMER_PERIOD_SHIFT) |
			   (periodic ? GEN7_OACONTROL_TIMER_ENABLE : 0) |
			   (report_format << GEN7_OACONTROL_FORMAT_SHIFT) |
			   (ctx ? GEN7_OACONTROL_PER_CTX_ENABLE : 0) |
			   GEN7_OACONTROL_ENABLE);
}

static void gen8_oa_enable(struct i915_perf_stream *stream)
{
	struct intel_uncore *uncore = stream->uncore;
	u32 report_format = stream->oa_buffer.format;

	/*
	 * Reset buf pointers so we don't forward reports from before now.
	 *
	 * Think carefully if considering trying to avoid this, since it
	 * also ensures status flags and the buffer itself are cleared
	 * in error paths, and we have checks for invalid reports based
	 * on the assumption that certain fields are written to zeroed
	 * memory which this helps maintains.
	 */
	gen8_init_oa_buffer(stream);

	/*
	 * Note: we don't rely on the hardware to perform single context
	 * filtering and instead filter on the cpu based on the context-id
	 * field of reports
	 */
	intel_uncore_write(uncore, GEN8_OACONTROL,
			   (report_format << GEN8_OA_REPORT_FORMAT_SHIFT) |
			   GEN8_OA_COUNTER_ENABLE);
}

static void gen12_oa_enable(struct i915_perf_stream *stream)
{
	struct intel_uncore *uncore = stream->uncore;
	u32 report_format = stream->oa_buffer.format;

	/*
	 * If we don't want OA reports from the OA buffer, then we don't even
	 * need to program the OAG unit.
	 */
	if (!(stream->sample_flags & SAMPLE_OA_REPORT))
		return;

	gen12_init_oa_buffer(stream);

	intel_uncore_write(uncore, GEN12_OAG_OACONTROL,
			   (report_format << GEN12_OAG_OACONTROL_OA_COUNTER_FORMAT_SHIFT) |
			   GEN12_OAG_OACONTROL_OA_COUNTER_ENABLE);
}

/**
 * i915_oa_stream_enable - handle `I915_PERF_IOCTL_ENABLE` for OA stream
 * @stream: An i915 perf stream opened for OA metrics
 *
 * [Re]enables hardware periodic sampling according to the period configured
 * when opening the stream. This also starts a hrtimer that will periodically
 * check for data in the circular OA buffer for notifying userspace (e.g.
 * during a read() or poll()).
 */
static void i915_oa_stream_enable(struct i915_perf_stream *stream)
{
	stream->pollin = false;

	stream->perf->ops.oa_enable(stream);

	if (stream->sample_flags & SAMPLE_OA_REPORT)
		hrtimer_start(&stream->poll_check_timer,
			      ns_to_ktime(stream->poll_oa_period),
			      HRTIMER_MODE_REL_PINNED);
}

static void gen7_oa_disable(struct i915_perf_stream *stream)
{
	struct intel_uncore *uncore = stream->uncore;

	intel_uncore_write(uncore, GEN7_OACONTROL, 0);
	if (intel_wait_for_register(uncore,
				    GEN7_OACONTROL, GEN7_OACONTROL_ENABLE, 0,
				    50))
		drm_err(&stream->perf->i915->drm,
			"wait for OA to be disabled timed out\n");
}

static void gen8_oa_disable(struct i915_perf_stream *stream)
{
	struct intel_uncore *uncore = stream->uncore;

	intel_uncore_write(uncore, GEN8_OACONTROL, 0);
	if (intel_wait_for_register(uncore,
				    GEN8_OACONTROL, GEN8_OA_COUNTER_ENABLE, 0,
				    50))
		drm_err(&stream->perf->i915->drm,
			"wait for OA to be disabled timed out\n");
}

static void gen12_oa_disable(struct i915_perf_stream *stream)
{
	struct intel_uncore *uncore = stream->uncore;

	intel_uncore_write(uncore, GEN12_OAG_OACONTROL, 0);
	if (intel_wait_for_register(uncore,
				    GEN12_OAG_OACONTROL,
				    GEN12_OAG_OACONTROL_OA_COUNTER_ENABLE, 0,
				    50))
		drm_err(&stream->perf->i915->drm,
			"wait for OA to be disabled timed out\n");

	intel_uncore_write(uncore, GEN12_OA_TLB_INV_CR, 1);
	if (intel_wait_for_register(uncore,
				    GEN12_OA_TLB_INV_CR,
				    1, 0,
				    50))
		drm_err(&stream->perf->i915->drm,
			"wait for OA tlb invalidate timed out\n");
}

/**
 * i915_oa_stream_disable - handle `I915_PERF_IOCTL_DISABLE` for OA stream
 * @stream: An i915 perf stream opened for OA metrics
 *
 * Stops the OA unit from periodically writing counter reports into the
 * circular OA buffer. This also stops the hrtimer that periodically checks for
 * data in the circular OA buffer, for notifying userspace.
 */
static void i915_oa_stream_disable(struct i915_perf_stream *stream)
{
	stream->perf->ops.oa_disable(stream);

	if (stream->sample_flags & SAMPLE_OA_REPORT)
		hrtimer_cancel(&stream->poll_check_timer);
}

static const struct i915_perf_stream_ops i915_oa_stream_ops = {
	.destroy = i915_oa_stream_destroy,
	.enable = i915_oa_stream_enable,
	.disable = i915_oa_stream_disable,
	.wait_unlocked = i915_oa_wait_unlocked,
	.poll_wait = i915_oa_poll_wait,
	.read = i915_oa_read,
};

static int i915_perf_stream_enable_sync(struct i915_perf_stream *stream)
{
	struct i915_active *active;
	int err;

	active = i915_active_create();
	if (!active)
		return -ENOMEM;

	err = stream->perf->ops.enable_metric_set(stream, active);
	if (err == 0)
		__i915_active_wait(active, TASK_UNINTERRUPTIBLE);

	i915_active_put(active);
	return err;
}

static void
get_default_sseu_config(struct intel_sseu *out_sseu,
			struct intel_engine_cs *engine)
{
	const struct sseu_dev_info *devinfo_sseu = &engine->gt->info.sseu;

	*out_sseu = intel_sseu_from_device_info(devinfo_sseu);

	if (GRAPHICS_VER(engine->i915) == 11) {
		/*
		 * We only need subslice count so it doesn't matter which ones
		 * we select - just turn off low bits in the amount of half of
		 * all available subslices per slice.
		 */
		out_sseu->subslice_mask =
			~(~0 << (hweight8(out_sseu->subslice_mask) / 2));
		out_sseu->slice_mask = 0x1;
	}
}

static int
get_sseu_config(struct intel_sseu *out_sseu,
		struct intel_engine_cs *engine,
		const struct drm_i915_gem_context_param_sseu *drm_sseu)
{
	if (drm_sseu->engine.engine_class != engine->uabi_class ||
	    drm_sseu->engine.engine_instance != engine->uabi_instance)
		return -EINVAL;

	return i915_gem_user_to_context_sseu(engine->gt, drm_sseu, out_sseu);
}

/**
 * i915_oa_stream_init - validate combined props for OA stream and init
 * @stream: An i915 perf stream
 * @param: The open parameters passed to `DRM_I915_PERF_OPEN`
 * @props: The property state that configures stream (individually validated)
 *
 * While read_properties_unlocked() validates properties in isolation it
 * doesn't ensure that the combination necessarily makes sense.
 *
 * At this point it has been determined that userspace wants a stream of
 * OA metrics, but still we need to further validate the combined
 * properties are OK.
 *
 * If the configuration makes sense then we can allocate memory for
 * a circular OA buffer and apply the requested metric set configuration.
 *
 * Returns: zero on success or a negative error code.
 */
static int i915_oa_stream_init(struct i915_perf_stream *stream,
			       struct drm_i915_perf_open_param *param,
			       struct perf_open_properties *props)
{
	struct drm_i915_private *i915 = stream->perf->i915;
	struct i915_perf *perf = stream->perf;
	int format_size;
	int ret;

	if (!props->engine) {
		DRM_DEBUG("OA engine not specified\n");
		return -EINVAL;
	}

	/*
	 * If the sysfs metrics/ directory wasn't registered for some
	 * reason then don't let userspace try their luck with config
	 * IDs
	 */
	if (!perf->metrics_kobj) {
		DRM_DEBUG("OA metrics weren't advertised via sysfs\n");
		return -EINVAL;
	}

	if (!(props->sample_flags & SAMPLE_OA_REPORT) &&
	    (GRAPHICS_VER(perf->i915) < 12 || !stream->ctx)) {
		DRM_DEBUG("Only OA report sampling supported\n");
		return -EINVAL;
	}

	if (!perf->ops.enable_metric_set) {
		DRM_DEBUG("OA unit not supported\n");
		return -ENODEV;
	}

	/*
	 * To avoid the complexity of having to accurately filter
	 * counter reports and marshal to the appropriate client
	 * we currently only allow exclusive access
	 */
	if (perf->exclusive_stream) {
		DRM_DEBUG("OA unit already in use\n");
		return -EBUSY;
	}

	if (!props->oa_format) {
		DRM_DEBUG("OA report format not specified\n");
		return -EINVAL;
	}

	stream->engine = props->engine;
	stream->uncore = stream->engine->gt->uncore;

	stream->sample_size = sizeof(struct drm_i915_perf_record_header);

	format_size = perf->oa_formats[props->oa_format].size;

	stream->sample_flags = props->sample_flags;
	stream->sample_size += format_size;

	stream->oa_buffer.format_size = format_size;
	if (drm_WARN_ON(&i915->drm, stream->oa_buffer.format_size == 0))
		return -EINVAL;

	stream->hold_preemption = props->hold_preemption;

	stream->oa_buffer.format =
		perf->oa_formats[props->oa_format].format;

	stream->periodic = props->oa_periodic;
	if (stream->periodic)
		stream->period_exponent = props->oa_period_exponent;

	if (stream->ctx) {
		ret = oa_get_render_ctx_id(stream);
		if (ret) {
			DRM_DEBUG("Invalid context id to filter with\n");
			return ret;
		}
	}

	ret = alloc_noa_wait(stream);
	if (ret) {
		DRM_DEBUG("Unable to allocate NOA wait batch buffer\n");
		goto err_noa_wait_alloc;
	}

	stream->oa_config = i915_perf_get_oa_config(perf, props->metrics_set);
	if (!stream->oa_config) {
		DRM_DEBUG("Invalid OA config id=%i\n", props->metrics_set);
		ret = -EINVAL;
		goto err_config;
	}

	/* PRM - observability performance counters:
	 *
	 *   OACONTROL, performance counter enable, note:
	 *
	 *   "When this bit is set, in order to have coherent counts,
	 *   RC6 power state and trunk clock gating must be disabled.
	 *   This can be achieved by programming MMIO registers as
	 *   0xA094=0 and 0xA090[31]=1"
	 *
	 *   In our case we are expecting that taking pm + FORCEWAKE
	 *   references will effectively disable RC6.
	 */
	intel_engine_pm_get(stream->engine);
	intel_uncore_forcewake_get(stream->uncore, FORCEWAKE_ALL);

	ret = alloc_oa_buffer(stream);
	if (ret)
		goto err_oa_buf_alloc;

	stream->ops = &i915_oa_stream_ops;

	perf->sseu = props->sseu;
	WRITE_ONCE(perf->exclusive_stream, stream);

	ret = i915_perf_stream_enable_sync(stream);
	if (ret) {
		DRM_DEBUG("Unable to enable metric set\n");
		goto err_enable;
	}

	DRM_DEBUG("opening stream oa config uuid=%s\n",
		  stream->oa_config->uuid);

	hrtimer_init(&stream->poll_check_timer,
		     CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	stream->poll_check_timer.function = oa_poll_check_timer_cb;
	init_waitqueue_head(&stream->poll_wq);
	spin_lock_init(&stream->oa_buffer.ptr_lock);

	return 0;

err_enable:
	WRITE_ONCE(perf->exclusive_stream, NULL);
	perf->ops.disable_metric_set(stream);

	free_oa_buffer(stream);

err_oa_buf_alloc:
	free_oa_configs(stream);

	intel_uncore_forcewake_put(stream->uncore, FORCEWAKE_ALL);
	intel_engine_pm_put(stream->engine);

err_config:
	free_noa_wait(stream);

err_noa_wait_alloc:
	if (stream->ctx)
		oa_put_render_ctx_id(stream);

	return ret;
}

void i915_oa_init_reg_state(const struct intel_context *ce,
			    const struct intel_engine_cs *engine)
{
	struct i915_perf_stream *stream;

	if (engine->class != RENDER_CLASS)
		return;

	/* perf.exclusive_stream serialised by lrc_configure_all_contexts() */
	stream = READ_ONCE(engine->i915->perf.exclusive_stream);
	if (stream && GRAPHICS_VER(stream->perf->i915) < 12)
		gen8_update_reg_state_unlocked(ce, stream);
}

/**
 * i915_perf_read - handles read() FOP for i915 perf stream FDs
 * @file: An i915 perf stream file
 * @buf: destination buffer given by userspace
 * @count: the number of bytes userspace wants to read
 * @ppos: (inout) file seek position (unused)
 *
 * The entry point for handling a read() on a stream file descriptor from
 * userspace. Most of the work is left to the i915_perf_read_locked() and
 * &i915_perf_stream_ops->read but to save having stream implementations (of
 * which we might have multiple later) we handle blocking read here.
 *
 * We can also consistently treat trying to read from a disabled stream
 * as an IO error so implementations can assume the stream is enabled
 * while reading.
 *
 * Returns: The number of bytes copied or a negative error code on failure.
 */
static ssize_t i915_perf_read(struct file *file,
			      char __user *buf,
			      size_t count,
			      loff_t *ppos)
{
	struct i915_perf_stream *stream = file->private_data;
	struct i915_perf *perf = stream->perf;
	size_t offset = 0;
	int ret;

	/* To ensure it's handled consistently we simply treat all reads of a
	 * disabled stream as an error. In particular it might otherwise lead
	 * to a deadlock for blocking file descriptors...
	 */
	if (!stream->enabled || !(stream->sample_flags & SAMPLE_OA_REPORT))
		return -EIO;

	if (!(file->f_flags & O_NONBLOCK)) {
		/* There's the small chance of false positives from
		 * stream->ops->wait_unlocked.
		 *
		 * E.g. with single context filtering since we only wait until
		 * oabuffer has >= 1 report we don't immediately know whether
		 * any reports really belong to the current context
		 */
		do {
			ret = stream->ops->wait_unlocked(stream);
			if (ret)
				return ret;

			mutex_lock(&perf->lock);
			ret = stream->ops->read(stream, buf, count, &offset);
			mutex_unlock(&perf->lock);
		} while (!offset && !ret);
	} else {
		mutex_lock(&perf->lock);
		ret = stream->ops->read(stream, buf, count, &offset);
		mutex_unlock(&perf->lock);
	}

	/* We allow the poll checking to sometimes report false positive EPOLLIN
	 * events where we might actually report EAGAIN on read() if there's
	 * not really any data available. In this situation though we don't
	 * want to enter a busy loop between poll() reporting a EPOLLIN event
	 * and read() returning -EAGAIN. Clearing the oa.pollin state here
	 * effectively ensures we back off until the next hrtimer callback
	 * before reporting another EPOLLIN event.
	 * The exception to this is if ops->read() returned -ENOSPC which means
	 * that more OA data is available than could fit in the user provided
	 * buffer. In this case we want the next poll() call to not block.
	 */
	if (ret != -ENOSPC)
		stream->pollin = false;

	/* Possible values for ret are 0, -EFAULT, -ENOSPC, -EIO, ... */
	return offset ?: (ret ?: -EAGAIN);
}

static enum hrtimer_restart oa_poll_check_timer_cb(struct hrtimer *hrtimer)
{
	struct i915_perf_stream *stream =
		container_of(hrtimer, typeof(*stream), poll_check_timer);

	if (oa_buffer_check_unlocked(stream)) {
		stream->pollin = true;
		wake_up(&stream->poll_wq);
	}

	hrtimer_forward_now(hrtimer,
			    ns_to_ktime(stream->poll_oa_period));

	return HRTIMER_RESTART;
}

/**
 * i915_perf_poll_locked - poll_wait() with a suitable wait queue for stream
 * @stream: An i915 perf stream
 * @file: An i915 perf stream file
 * @wait: poll() state table
 *
 * For handling userspace polling on an i915 perf stream, this calls through to
 * &i915_perf_stream_ops->poll_wait to call poll_wait() with a wait queue that
 * will be woken for new stream data.
 *
 * Note: The &perf->lock mutex has been taken to serialize
 * with any non-file-operation driver hooks.
 *
 * Returns: any poll events that are ready without sleeping
 */
static __poll_t i915_perf_poll_locked(struct i915_perf_stream *stream,
				      struct file *file,
				      poll_table *wait)
{
	__poll_t events = 0;

	stream->ops->poll_wait(stream, file, wait);

	/* Note: we don't explicitly check whether there's something to read
	 * here since this path may be very hot depending on what else
	 * userspace is polling, or on the timeout in use. We rely solely on
	 * the hrtimer/oa_poll_check_timer_cb to notify us when there are
	 * samples to read.
	 */
	if (stream->pollin)
		events |= EPOLLIN;

	return events;
}

/**
 * i915_perf_poll - call poll_wait() with a suitable wait queue for stream
 * @file: An i915 perf stream file
 * @wait: poll() state table
 *
 * For handling userspace polling on an i915 perf stream, this ensures
 * poll_wait() gets called with a wait queue that will be woken for new stream
 * data.
 *
 * Note: Implementation deferred to i915_perf_poll_locked()
 *
 * Returns: any poll events that are ready without sleeping
 */
static __poll_t i915_perf_poll(struct file *file, poll_table *wait)
{
	struct i915_perf_stream *stream = file->private_data;
	struct i915_perf *perf = stream->perf;
	__poll_t ret;

	mutex_lock(&perf->lock);
	ret = i915_perf_poll_locked(stream, file, wait);
	mutex_unlock(&perf->lock);

	return ret;
}

/**
 * i915_perf_enable_locked - handle `I915_PERF_IOCTL_ENABLE` ioctl
 * @stream: A disabled i915 perf stream
 *
 * [Re]enables the associated capture of data for this stream.
 *
 * If a stream was previously enabled then there's currently no intention
 * to provide userspace any guarantee about the preservation of previously
 * buffered data.
 */
static void i915_perf_enable_locked(struct i915_perf_stream *stream)
{
	if (stream->enabled)
		return;

	/* Allow stream->ops->enable() to refer to this */
	stream->enabled = true;

	if (stream->ops->enable)
		stream->ops->enable(stream);

	if (stream->hold_preemption)
		intel_context_set_nopreempt(stream->pinned_ctx);
}

/**
 * i915_perf_disable_locked - handle `I915_PERF_IOCTL_DISABLE` ioctl
 * @stream: An enabled i915 perf stream
 *
 * Disables the associated capture of data for this stream.
 *
 * The intention is that disabling an re-enabling a stream will ideally be
 * cheaper than destroying and re-opening a stream with the same configuration,
 * though there are no formal guarantees about what state or buffered data
 * must be retained between disabling and re-enabling a stream.
 *
 * Note: while a stream is disabled it's considered an error for userspace
 * to attempt to read from the stream (-EIO).
 */
static void i915_perf_disable_locked(struct i915_perf_stream *stream)
{
	if (!stream->enabled)
		return;

	/* Allow stream->ops->disable() to refer to this */
	stream->enabled = false;

	if (stream->hold_preemption)
		intel_context_clear_nopreempt(stream->pinned_ctx);

	if (stream->ops->disable)
		stream->ops->disable(stream);
}

static long i915_perf_config_locked(struct i915_perf_stream *stream,
				    unsigned long metrics_set)
{
	struct i915_oa_config *config;
	long ret = stream->oa_config->id;

	config = i915_perf_get_oa_config(stream->perf, metrics_set);
	if (!config)
		return -EINVAL;

	if (config != stream->oa_config) {
		int err;

		/*
		 * If OA is bound to a specific context, emit the
		 * reconfiguration inline from that context. The update
		 * will then be ordered with respect to submission on that
		 * context.
		 *
		 * When set globally, we use a low priority kernel context,
		 * so it will effectively take effect when idle.
		 */
		err = emit_oa_config(stream, config, oa_context(stream), NULL);
		if (!err)
			config = xchg(&stream->oa_config, config);
		else
			ret = err;
	}

	i915_oa_config_put(config);

	return ret;
}

/**
 * i915_perf_ioctl_locked - support ioctl() usage with i915 perf stream FDs
 * @stream: An i915 perf stream
 * @cmd: the ioctl request
 * @arg: the ioctl data
 *
 * Note: The &perf->lock mutex has been taken to serialize
 * with any non-file-operation driver hooks.
 *
 * Returns: zero on success or a negative error code. Returns -EINVAL for
 * an unknown ioctl request.
 */
static long i915_perf_ioctl_locked(struct i915_perf_stream *stream,
				   unsigned int cmd,
				   unsigned long arg)
{
	switch (cmd) {
	case I915_PERF_IOCTL_ENABLE:
		i915_perf_enable_locked(stream);
		return 0;
	case I915_PERF_IOCTL_DISABLE:
		i915_perf_disable_locked(stream);
		return 0;
	case I915_PERF_IOCTL_CONFIG:
		return i915_perf_config_locked(stream, arg);
	}

	return -EINVAL;
}

/**
 * i915_perf_ioctl - support ioctl() usage with i915 perf stream FDs
 * @file: An i915 perf stream file
 * @cmd: the ioctl request
 * @arg: the ioctl data
 *
 * Implementation deferred to i915_perf_ioctl_locked().
 *
 * Returns: zero on success or a negative error code. Returns -EINVAL for
 * an unknown ioctl request.
 */
static long i915_perf_ioctl(struct file *file,
			    unsigned int cmd,
			    unsigned long arg)
{
	struct i915_perf_stream *stream = file->private_data;
	struct i915_perf *perf = stream->perf;
	long ret;

	mutex_lock(&perf->lock);
	ret = i915_perf_ioctl_locked(stream, cmd, arg);
	mutex_unlock(&perf->lock);

	return ret;
}

/**
 * i915_perf_destroy_locked - destroy an i915 perf stream
 * @stream: An i915 perf stream
 *
 * Frees all resources associated with the given i915 perf @stream, disabling
 * any associated data capture in the process.
 *
 * Note: The &perf->lock mutex has been taken to serialize
 * with any non-file-operation driver hooks.
 */
static void i915_perf_destroy_locked(struct i915_perf_stream *stream)
{
	if (stream->enabled)
		i915_perf_disable_locked(stream);

	if (stream->ops->destroy)
		stream->ops->destroy(stream);

	if (stream->ctx)
		i915_gem_context_put(stream->ctx);

	kfree(stream);
}

/**
 * i915_perf_release - handles userspace close() of a stream file
 * @inode: anonymous inode associated with file
 * @file: An i915 perf stream file
 *
 * Cleans up any resources associated with an open i915 perf stream file.
 *
 * NB: close() can't really fail from the userspace point of view.
 *
 * Returns: zero on success or a negative error code.
 */
static int i915_perf_release(struct inode *inode, struct file *file)
{
	struct i915_perf_stream *stream = file->private_data;
	struct i915_perf *perf = stream->perf;

	mutex_lock(&perf->lock);
	i915_perf_destroy_locked(stream);
	mutex_unlock(&perf->lock);

	/* Release the reference the perf stream kept on the driver. */
	drm_dev_put(&perf->i915->drm);

	return 0;
}


static const struct file_operations fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.release	= i915_perf_release,
	.poll		= i915_perf_poll,
	.read		= i915_perf_read,
	.unlocked_ioctl	= i915_perf_ioctl,
	/* Our ioctl have no arguments, so it's safe to use the same function
	 * to handle 32bits compatibility.
	 */
	.compat_ioctl   = i915_perf_ioctl,
};


/**
 * i915_perf_open_ioctl_locked - DRM ioctl() for userspace to open a stream FD
 * @perf: i915 perf instance
 * @param: The open parameters passed to 'DRM_I915_PERF_OPEN`
 * @props: individually validated u64 property value pairs
 * @file: drm file
 *
 * See i915_perf_ioctl_open() for interface details.
 *
 * Implements further stream config validation and stream initialization on
 * behalf of i915_perf_open_ioctl() with the &perf->lock mutex
 * taken to serialize with any non-file-operation driver hooks.
 *
 * Note: at this point the @props have only been validated in isolation and
 * it's still necessary to validate that the combination of properties makes
 * sense.
 *
 * In the case where userspace is interested in OA unit metrics then further
 * config validation and stream initialization details will be handled by
 * i915_oa_stream_init(). The code here should only validate config state that
 * will be relevant to all stream types / backends.
 *
 * Returns: zero on success or a negative error code.
 */
static int
i915_perf_open_ioctl_locked(struct i915_perf *perf,
			    struct drm_i915_perf_open_param *param,
			    struct perf_open_properties *props,
			    struct drm_file *file)
{
	struct i915_gem_context *specific_ctx = NULL;
	struct i915_perf_stream *stream = NULL;
	unsigned long f_flags = 0;
	bool privileged_op = true;
	int stream_fd;
	int ret;

	if (props->single_context) {
		u32 ctx_handle = props->ctx_handle;
		struct drm_i915_file_private *file_priv = file->driver_priv;

		specific_ctx = i915_gem_context_lookup(file_priv, ctx_handle);
		if (IS_ERR(specific_ctx)) {
			DRM_DEBUG("Failed to look up context with ID %u for opening perf stream\n",
				  ctx_handle);
			ret = PTR_ERR(specific_ctx);
			goto err;
		}
	}

	/*
	 * On Haswell the OA unit supports clock gating off for a specific
	 * context and in this mode there's no visibility of metrics for the
	 * rest of the system, which we consider acceptable for a
	 * non-privileged client.
	 *
	 * For Gen8->11 the OA unit no longer supports clock gating off for a
	 * specific context and the kernel can't securely stop the counters
	 * from updating as system-wide / global values. Even though we can
	 * filter reports based on the included context ID we can't block
	 * clients from seeing the raw / global counter values via
	 * MI_REPORT_PERF_COUNT commands and so consider it a privileged op to
	 * enable the OA unit by default.
	 *
	 * For Gen12+ we gain a new OAR unit that only monitors the RCS on a
	 * per context basis. So we can relax requirements there if the user
	 * doesn't request global stream access (i.e. query based sampling
	 * using MI_RECORD_PERF_COUNT.
	 */
	if (IS_HASWELL(perf->i915) && specific_ctx)
		privileged_op = false;
	else if (GRAPHICS_VER(perf->i915) == 12 && specific_ctx &&
		 (props->sample_flags & SAMPLE_OA_REPORT) == 0)
		privileged_op = false;

	if (props->hold_preemption) {
		if (!props->single_context) {
			DRM_DEBUG("preemption disable with no context\n");
			ret = -EINVAL;
			goto err;
		}
		privileged_op = true;
	}

	/*
	 * Asking for SSEU configuration is a priviliged operation.
	 */
	if (props->has_sseu)
		privileged_op = true;
	else
		get_default_sseu_config(&props->sseu, props->engine);

	/* Similar to perf's kernel.perf_paranoid_cpu sysctl option
	 * we check a dev.i915.perf_stream_paranoid sysctl option
	 * to determine if it's ok to access system wide OA counters
	 * without CAP_PERFMON or CAP_SYS_ADMIN privileges.
	 */
	if (privileged_op &&
	    i915_perf_stream_paranoid && !perfmon_capable()) {
		DRM_DEBUG("Insufficient privileges to open i915 perf stream\n");
		ret = -EACCES;
		goto err_ctx;
	}

	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (!stream) {
		ret = -ENOMEM;
		goto err_ctx;
	}

	stream->perf = perf;
	stream->ctx = specific_ctx;
	stream->poll_oa_period = props->poll_oa_period;

	ret = i915_oa_stream_init(stream, param, props);
	if (ret)
		goto err_alloc;

	/* we avoid simply assigning stream->sample_flags = props->sample_flags
	 * to have _stream_init check the combination of sample flags more
	 * thoroughly, but still this is the expected result at this point.
	 */
	if (WARN_ON(stream->sample_flags != props->sample_flags)) {
		ret = -ENODEV;
		goto err_flags;
	}

	if (param->flags & I915_PERF_FLAG_FD_CLOEXEC)
		f_flags |= O_CLOEXEC;
	if (param->flags & I915_PERF_FLAG_FD_NONBLOCK)
		f_flags |= O_NONBLOCK;

	stream_fd = anon_inode_getfd("[i915_perf]", &fops, stream, f_flags);
	if (stream_fd < 0) {
		ret = stream_fd;
		goto err_flags;
	}

	if (!(param->flags & I915_PERF_FLAG_DISABLED))
		i915_perf_enable_locked(stream);

	/* Take a reference on the driver that will be kept with stream_fd
	 * until its release.
	 */
	drm_dev_get(&perf->i915->drm);

	return stream_fd;

err_flags:
	if (stream->ops->destroy)
		stream->ops->destroy(stream);
err_alloc:
	kfree(stream);
err_ctx:
	if (specific_ctx)
		i915_gem_context_put(specific_ctx);
err:
	return ret;
}

static u64 oa_exponent_to_ns(struct i915_perf *perf, int exponent)
{
	return intel_gt_clock_interval_to_ns(perf->i915->ggtt.vm.gt,
					     2ULL << exponent);
}

static __always_inline bool
oa_format_valid(struct i915_perf *perf, enum drm_i915_oa_format format)
{
	return test_bit(format, perf->format_mask);
}

static __always_inline void
oa_format_add(struct i915_perf *perf, enum drm_i915_oa_format format)
{
	__set_bit(format, perf->format_mask);
}

/**
 * read_properties_unlocked - validate + copy userspace stream open properties
 * @perf: i915 perf instance
 * @uprops: The array of u64 key value pairs given by userspace
 * @n_props: The number of key value pairs expected in @uprops
 * @props: The stream configuration built up while validating properties
 *
 * Note this function only validates properties in isolation it doesn't
 * validate that the combination of properties makes sense or that all
 * properties necessary for a particular kind of stream have been set.
 *
 * Note that there currently aren't any ordering requirements for properties so
 * we shouldn't validate or assume anything about ordering here. This doesn't
 * rule out defining new properties with ordering requirements in the future.
 */
static int read_properties_unlocked(struct i915_perf *perf,
				    u64 __user *uprops,
				    u32 n_props,
				    struct perf_open_properties *props)
{
	u64 __user *uprop = uprops;
	u32 i;
	int ret;

	memset(props, 0, sizeof(struct perf_open_properties));
	props->poll_oa_period = DEFAULT_POLL_PERIOD_NS;

	if (!n_props) {
		DRM_DEBUG("No i915 perf properties given\n");
		return -EINVAL;
	}

	/* At the moment we only support using i915-perf on the RCS. */
	props->engine = intel_engine_lookup_user(perf->i915,
						 I915_ENGINE_CLASS_RENDER,
						 0);
	if (!props->engine) {
		DRM_DEBUG("No RENDER-capable engines\n");
		return -EINVAL;
	}

	/* Considering that ID = 0 is reserved and assuming that we don't
	 * (currently) expect any configurations to ever specify duplicate
	 * values for a particular property ID then the last _PROP_MAX value is
	 * one greater than the maximum number of properties we expect to get
	 * from userspace.
	 */
	if (n_props >= DRM_I915_PERF_PROP_MAX) {
		DRM_DEBUG("More i915 perf properties specified than exist\n");
		return -EINVAL;
	}

	for (i = 0; i < n_props; i++) {
		u64 oa_period, oa_freq_hz;
		u64 id, value;

		ret = get_user(id, uprop);
		if (ret)
			return ret;

		ret = get_user(value, uprop + 1);
		if (ret)
			return ret;

		if (id == 0 || id >= DRM_I915_PERF_PROP_MAX) {
			DRM_DEBUG("Unknown i915 perf property ID\n");
			return -EINVAL;
		}

		switch ((enum drm_i915_perf_property_id)id) {
		case DRM_I915_PERF_PROP_CTX_HANDLE:
			props->single_context = 1;
			props->ctx_handle = value;
			break;
		case DRM_I915_PERF_PROP_SAMPLE_OA:
			if (value)
				props->sample_flags |= SAMPLE_OA_REPORT;
			break;
		case DRM_I915_PERF_PROP_OA_METRICS_SET:
			if (value == 0) {
				DRM_DEBUG("Unknown OA metric set ID\n");
				return -EINVAL;
			}
			props->metrics_set = value;
			break;
		case DRM_I915_PERF_PROP_OA_FORMAT:
			if (value == 0 || value >= I915_OA_FORMAT_MAX) {
				DRM_DEBUG("Out-of-range OA report format %llu\n",
					  value);
				return -EINVAL;
			}
			if (!oa_format_valid(perf, value)) {
				DRM_DEBUG("Unsupported OA report format %llu\n",
					  value);
				return -EINVAL;
			}
			props->oa_format = value;
			break;
		case DRM_I915_PERF_PROP_OA_EXPONENT:
			if (value > OA_EXPONENT_MAX) {
				DRM_DEBUG("OA timer exponent too high (> %u)\n",
					 OA_EXPONENT_MAX);
				return -EINVAL;
			}

			/* Theoretically we can program the OA unit to sample
			 * e.g. every 160ns for HSW, 167ns for BDW/SKL or 104ns
			 * for BXT. We don't allow such high sampling
			 * frequencies by default unless root.
			 */

			BUILD_BUG_ON(sizeof(oa_period) != 8);
			oa_period = oa_exponent_to_ns(perf, value);

			/* This check is primarily to ensure that oa_period <=
			 * UINT32_MAX (before passing to do_div which only
			 * accepts a u32 denominator), but we can also skip
			 * checking anything < 1Hz which implicitly can't be
			 * limited via an integer oa_max_sample_rate.
			 */
			if (oa_period <= NSEC_PER_SEC) {
				u64 tmp = NSEC_PER_SEC;
				do_div(tmp, oa_period);
				oa_freq_hz = tmp;
			} else
				oa_freq_hz = 0;

			if (oa_freq_hz > i915_oa_max_sample_rate && !perfmon_capable()) {
				DRM_DEBUG("OA exponent would exceed the max sampling frequency (sysctl dev.i915.oa_max_sample_rate) %uHz without CAP_PERFMON or CAP_SYS_ADMIN privileges\n",
					  i915_oa_max_sample_rate);
				return -EACCES;
			}

			props->oa_periodic = true;
			props->oa_period_exponent = value;
			break;
		case DRM_I915_PERF_PROP_HOLD_PREEMPTION:
			props->hold_preemption = !!value;
			break;
		case DRM_I915_PERF_PROP_GLOBAL_SSEU: {
			struct drm_i915_gem_context_param_sseu user_sseu;

			if (copy_from_user(&user_sseu,
					   u64_to_user_ptr(value),
					   sizeof(user_sseu))) {
				DRM_DEBUG("Unable to copy global sseu parameter\n");
				return -EFAULT;
			}

			ret = get_sseu_config(&props->sseu, props->engine, &user_sseu);
			if (ret) {
				DRM_DEBUG("Invalid SSEU configuration\n");
				return ret;
			}
			props->has_sseu = true;
			break;
		}
		case DRM_I915_PERF_PROP_POLL_OA_PERIOD:
			if (value < 100000 /* 100us */) {
				DRM_DEBUG("OA availability timer too small (%lluns < 100us)\n",
					  value);
				return -EINVAL;
			}
			props->poll_oa_period = value;
			break;
		case DRM_I915_PERF_PROP_MAX:
			MISSING_CASE(id);
			return -EINVAL;
		}

		uprop += 2;
	}

	return 0;
}

/**
 * i915_perf_open_ioctl - DRM ioctl() for userspace to open a stream FD
 * @dev: drm device
 * @data: ioctl data copied from userspace (unvalidated)
 * @file: drm file
 *
 * Validates the stream open parameters given by userspace including flags
 * and an array of u64 key, value pair properties.
 *
 * Very little is assumed up front about the nature of the stream being
 * opened (for instance we don't assume it's for periodic OA unit metrics). An
 * i915-perf stream is expected to be a suitable interface for other forms of
 * buffered data written by the GPU besides periodic OA metrics.
 *
 * Note we copy the properties from userspace outside of the i915 perf
 * mutex to avoid an awkward lockdep with mmap_lock.
 *
 * Most of the implementation details are handled by
 * i915_perf_open_ioctl_locked() after taking the &perf->lock
 * mutex for serializing with any non-file-operation driver hooks.
 *
 * Return: A newly opened i915 Perf stream file descriptor or negative
 * error code on failure.
 */
int i915_perf_open_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file)
{
	struct i915_perf *perf = &to_i915(dev)->perf;
	struct drm_i915_perf_open_param *param = data;
	struct perf_open_properties props;
	u32 known_open_flags;
	int ret;

	if (!perf->i915) {
		DRM_DEBUG("i915 perf interface not available for this system\n");
		return -ENOTSUPP;
	}

	known_open_flags = I915_PERF_FLAG_FD_CLOEXEC |
			   I915_PERF_FLAG_FD_NONBLOCK |
			   I915_PERF_FLAG_DISABLED;
	if (param->flags & ~known_open_flags) {
		DRM_DEBUG("Unknown drm_i915_perf_open_param flag\n");
		return -EINVAL;
	}

	ret = read_properties_unlocked(perf,
				       u64_to_user_ptr(param->properties_ptr),
				       param->num_properties,
				       &props);
	if (ret)
		return ret;

	mutex_lock(&perf->lock);
	ret = i915_perf_open_ioctl_locked(perf, param, &props, file);
	mutex_unlock(&perf->lock);

	return ret;
}

/**
 * i915_perf_register - exposes i915-perf to userspace
 * @i915: i915 device instance
 *
 * In particular OA metric sets are advertised under a sysfs metrics/
 * directory allowing userspace to enumerate valid IDs that can be
 * used to open an i915-perf stream.
 */
void i915_perf_register(struct drm_i915_private *i915)
{
	struct i915_perf *perf = &i915->perf;

	if (!perf->i915)
		return;

	/* To be sure we're synchronized with an attempted
	 * i915_perf_open_ioctl(); considering that we register after
	 * being exposed to userspace.
	 */
	mutex_lock(&perf->lock);

	perf->metrics_kobj =
		kobject_create_and_add("metrics",
				       &i915->drm.primary->kdev->kobj);

	mutex_unlock(&perf->lock);
}

/**
 * i915_perf_unregister - hide i915-perf from userspace
 * @i915: i915 device instance
 *
 * i915-perf state cleanup is split up into an 'unregister' and
 * 'deinit' phase where the interface is first hidden from
 * userspace by i915_perf_unregister() before cleaning up
 * remaining state in i915_perf_fini().
 */
void i915_perf_unregister(struct drm_i915_private *i915)
{
	struct i915_perf *perf = &i915->perf;

	if (!perf->metrics_kobj)
		return;

	kobject_put(perf->metrics_kobj);
	perf->metrics_kobj = NULL;
}

static bool gen8_is_valid_flex_addr(struct i915_perf *perf, u32 addr)
{
	static const i915_reg_t flex_eu_regs[] = {
		EU_PERF_CNTL0,
		EU_PERF_CNTL1,
		EU_PERF_CNTL2,
		EU_PERF_CNTL3,
		EU_PERF_CNTL4,
		EU_PERF_CNTL5,
		EU_PERF_CNTL6,
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(flex_eu_regs); i++) {
		if (i915_mmio_reg_offset(flex_eu_regs[i]) == addr)
			return true;
	}
	return false;
}

static bool reg_in_range_table(u32 addr, const struct i915_range *table)
{
	while (table->start || table->end) {
		if (addr >= table->start && addr <= table->end)
			return true;

		table++;
	}

	return false;
}

#define REG_EQUAL(addr, mmio) \
	((addr) == i915_mmio_reg_offset(mmio))

static const struct i915_range gen7_oa_b_counters[] = {
	{ .start = 0x2710, .end = 0x272c },	/* OASTARTTRIG[1-8] */
	{ .start = 0x2740, .end = 0x275c },	/* OAREPORTTRIG[1-8] */
	{ .start = 0x2770, .end = 0x27ac },	/* OACEC[0-7][0-1] */
	{}
};

static const struct i915_range gen12_oa_b_counters[] = {
	{ .start = 0x2b2c, .end = 0x2b2c },	/* GEN12_OAG_OA_PESS */
	{ .start = 0xd900, .end = 0xd91c },	/* GEN12_OAG_OASTARTTRIG[1-8] */
	{ .start = 0xd920, .end = 0xd93c },	/* GEN12_OAG_OAREPORTTRIG1[1-8] */
	{ .start = 0xd940, .end = 0xd97c },	/* GEN12_OAG_CEC[0-7][0-1] */
	{ .start = 0xdc00, .end = 0xdc3c },	/* GEN12_OAG_SCEC[0-7][0-1] */
	{ .start = 0xdc40, .end = 0xdc40 },	/* GEN12_OAG_SPCTR_CNF */
	{ .start = 0xdc44, .end = 0xdc44 },	/* GEN12_OAA_DBG_REG */
	{}
};

static const struct i915_range gen7_oa_mux_regs[] = {
	{ .start = 0x91b8, .end = 0x91cc },	/* OA_PERFCNT[1-2], OA_PERFMATRIX */
	{ .start = 0x9800, .end = 0x9888 },	/* MICRO_BP0_0 - NOA_WRITE */
	{ .start = 0xe180, .end = 0xe180 },	/* HALF_SLICE_CHICKEN2 */
	{}
};

static const struct i915_range hsw_oa_mux_regs[] = {
	{ .start = 0x09e80, .end = 0x09ea4 }, /* HSW_MBVID2_NOA[0-9] */
	{ .start = 0x09ec0, .end = 0x09ec0 }, /* HSW_MBVID2_MISR0 */
	{ .start = 0x25100, .end = 0x2ff90 },
	{}
};

static const struct i915_range chv_oa_mux_regs[] = {
	{ .start = 0x182300, .end = 0x1823a4 },
	{}
};

static const struct i915_range gen8_oa_mux_regs[] = {
	{ .start = 0x0d00, .end = 0x0d2c },	/* RPM_CONFIG[0-1], NOA_CONFIG[0-8] */
	{ .start = 0x20cc, .end = 0x20cc },	/* WAIT_FOR_RC6_EXIT */
	{}
};

static const struct i915_range gen11_oa_mux_regs[] = {
	{ .start = 0x91c8, .end = 0x91dc },	/* OA_PERFCNT[3-4] */
	{}
};

static const struct i915_range gen12_oa_mux_regs[] = {
	{ .start = 0x0d00, .end = 0x0d04 },     /* RPM_CONFIG[0-1] */
	{ .start = 0x0d0c, .end = 0x0d2c },     /* NOA_CONFIG[0-8] */
	{ .start = 0x9840, .end = 0x9840 },	/* GDT_CHICKEN_BITS */
	{ .start = 0x9884, .end = 0x9888 },	/* NOA_WRITE */
	{ .start = 0x20cc, .end = 0x20cc },	/* WAIT_FOR_RC6_EXIT */
	{}
};

static bool gen7_is_valid_b_counter_addr(struct i915_perf *perf, u32 addr)
{
	return reg_in_range_table(addr, gen7_oa_b_counters);
}

static bool gen8_is_valid_mux_addr(struct i915_perf *perf, u32 addr)
{
	return reg_in_range_table(addr, gen7_oa_mux_regs) ||
		reg_in_range_table(addr, gen8_oa_mux_regs);
}

static bool gen11_is_valid_mux_addr(struct i915_perf *perf, u32 addr)
{
	return reg_in_range_table(addr, gen7_oa_mux_regs) ||
		reg_in_range_table(addr, gen8_oa_mux_regs) ||
		reg_in_range_table(addr, gen11_oa_mux_regs);
}

static bool hsw_is_valid_mux_addr(struct i915_perf *perf, u32 addr)
{
	return reg_in_range_table(addr, gen7_oa_mux_regs) ||
		reg_in_range_table(addr, hsw_oa_mux_regs);
}

static bool chv_is_valid_mux_addr(struct i915_perf *perf, u32 addr)
{
	return reg_in_range_table(addr, gen7_oa_mux_regs) ||
		reg_in_range_table(addr, chv_oa_mux_regs);
}

static bool gen12_is_valid_b_counter_addr(struct i915_perf *perf, u32 addr)
{
	return reg_in_range_table(addr, gen12_oa_b_counters);
}

static bool gen12_is_valid_mux_addr(struct i915_perf *perf, u32 addr)
{
	return reg_in_range_table(addr, gen12_oa_mux_regs);
}

static u32 mask_reg_value(u32 reg, u32 val)
{
	/* HALF_SLICE_CHICKEN2 is programmed with a the
	 * WaDisableSTUnitPowerOptimization workaround. Make sure the value
	 * programmed by userspace doesn't change this.
	 */
	if (REG_EQUAL(reg, HALF_SLICE_CHICKEN2))
		val = val & ~_MASKED_BIT_ENABLE(GEN8_ST_PO_DISABLE);

	/* WAIT_FOR_RC6_EXIT has only one bit fullfilling the function
	 * indicated by its name and a bunch of selection fields used by OA
	 * configs.
	 */
	if (REG_EQUAL(reg, WAIT_FOR_RC6_EXIT))
		val = val & ~_MASKED_BIT_ENABLE(HSW_WAIT_FOR_RC6_EXIT_ENABLE);

	return val;
}

static struct i915_oa_reg *alloc_oa_regs(struct i915_perf *perf,
					 bool (*is_valid)(struct i915_perf *perf, u32 addr),
					 u32 __user *regs,
					 u32 n_regs)
{
	struct i915_oa_reg *oa_regs;
	int err;
	u32 i;

	if (!n_regs)
		return NULL;

	/* No is_valid function means we're not allowing any register to be programmed. */
	GEM_BUG_ON(!is_valid);
	if (!is_valid)
		return ERR_PTR(-EINVAL);

	oa_regs = kmalloc_array(n_regs, sizeof(*oa_regs), GFP_KERNEL);
	if (!oa_regs)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < n_regs; i++) {
		u32 addr, value;

		err = get_user(addr, regs);
		if (err)
			goto addr_err;

		if (!is_valid(perf, addr)) {
			DRM_DEBUG("Invalid oa_reg address: %X\n", addr);
			err = -EINVAL;
			goto addr_err;
		}

		err = get_user(value, regs + 1);
		if (err)
			goto addr_err;

		oa_regs[i].addr = _MMIO(addr);
		oa_regs[i].value = mask_reg_value(addr, value);

		regs += 2;
	}

	return oa_regs;

addr_err:
	kfree(oa_regs);
	return ERR_PTR(err);
}

static ssize_t show_dynamic_id(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct i915_oa_config *oa_config =
		container_of(attr, typeof(*oa_config), sysfs_metric_id);

	return sprintf(buf, "%d\n", oa_config->id);
}

static int create_dynamic_oa_sysfs_entry(struct i915_perf *perf,
					 struct i915_oa_config *oa_config)
{
	sysfs_attr_init(&oa_config->sysfs_metric_id.attr);
	oa_config->sysfs_metric_id.attr.name = "id";
	oa_config->sysfs_metric_id.attr.mode = S_IRUGO;
	oa_config->sysfs_metric_id.show = show_dynamic_id;
	oa_config->sysfs_metric_id.store = NULL;

	oa_config->attrs[0] = &oa_config->sysfs_metric_id.attr;
	oa_config->attrs[1] = NULL;

	oa_config->sysfs_metric.name = oa_config->uuid;
	oa_config->sysfs_metric.attrs = oa_config->attrs;

	return sysfs_create_group(perf->metrics_kobj,
				  &oa_config->sysfs_metric);
}

/**
 * i915_perf_add_config_ioctl - DRM ioctl() for userspace to add a new OA config
 * @dev: drm device
 * @data: ioctl data (pointer to struct drm_i915_perf_oa_config) copied from
 *        userspace (unvalidated)
 * @file: drm file
 *
 * Validates the submitted OA register to be saved into a new OA config that
 * can then be used for programming the OA unit and its NOA network.
 *
 * Returns: A new allocated config number to be used with the perf open ioctl
 * or a negative error code on failure.
 */
int i915_perf_add_config_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file)
{
	struct i915_perf *perf = &to_i915(dev)->perf;
	struct drm_i915_perf_oa_config *args = data;
	struct i915_oa_config *oa_config, *tmp;
	struct i915_oa_reg *regs;
	int err, id;

	if (!perf->i915) {
		DRM_DEBUG("i915 perf interface not available for this system\n");
		return -ENOTSUPP;
	}

	if (!perf->metrics_kobj) {
		DRM_DEBUG("OA metrics weren't advertised via sysfs\n");
		return -EINVAL;
	}

	if (i915_perf_stream_paranoid && !perfmon_capable()) {
		DRM_DEBUG("Insufficient privileges to add i915 OA config\n");
		return -EACCES;
	}

	if ((!args->mux_regs_ptr || !args->n_mux_regs) &&
	    (!args->boolean_regs_ptr || !args->n_boolean_regs) &&
	    (!args->flex_regs_ptr || !args->n_flex_regs)) {
		DRM_DEBUG("No OA registers given\n");
		return -EINVAL;
	}

	oa_config = kzalloc(sizeof(*oa_config), GFP_KERNEL);
	if (!oa_config) {
		DRM_DEBUG("Failed to allocate memory for the OA config\n");
		return -ENOMEM;
	}

	oa_config->perf = perf;
	kref_init(&oa_config->ref);

	if (!uuid_is_valid(args->uuid)) {
		DRM_DEBUG("Invalid uuid format for OA config\n");
		err = -EINVAL;
		goto reg_err;
	}

	/* Last character in oa_config->uuid will be 0 because oa_config is
	 * kzalloc.
	 */
	memcpy(oa_config->uuid, args->uuid, sizeof(args->uuid));

	oa_config->mux_regs_len = args->n_mux_regs;
	regs = alloc_oa_regs(perf,
			     perf->ops.is_valid_mux_reg,
			     u64_to_user_ptr(args->mux_regs_ptr),
			     args->n_mux_regs);

	if (IS_ERR(regs)) {
		DRM_DEBUG("Failed to create OA config for mux_regs\n");
		err = PTR_ERR(regs);
		goto reg_err;
	}
	oa_config->mux_regs = regs;

	oa_config->b_counter_regs_len = args->n_boolean_regs;
	regs = alloc_oa_regs(perf,
			     perf->ops.is_valid_b_counter_reg,
			     u64_to_user_ptr(args->boolean_regs_ptr),
			     args->n_boolean_regs);

	if (IS_ERR(regs)) {
		DRM_DEBUG("Failed to create OA config for b_counter_regs\n");
		err = PTR_ERR(regs);
		goto reg_err;
	}
	oa_config->b_counter_regs = regs;

	if (GRAPHICS_VER(perf->i915) < 8) {
		if (args->n_flex_regs != 0) {
			err = -EINVAL;
			goto reg_err;
		}
	} else {
		oa_config->flex_regs_len = args->n_flex_regs;
		regs = alloc_oa_regs(perf,
				     perf->ops.is_valid_flex_reg,
				     u64_to_user_ptr(args->flex_regs_ptr),
				     args->n_flex_regs);

		if (IS_ERR(regs)) {
			DRM_DEBUG("Failed to create OA config for flex_regs\n");
			err = PTR_ERR(regs);
			goto reg_err;
		}
		oa_config->flex_regs = regs;
	}

	err = mutex_lock_interruptible(&perf->metrics_lock);
	if (err)
		goto reg_err;

	/* We shouldn't have too many configs, so this iteration shouldn't be
	 * too costly.
	 */
	idr_for_each_entry(&perf->metrics_idr, tmp, id) {
		if (!strcmp(tmp->uuid, oa_config->uuid)) {
			DRM_DEBUG("OA config already exists with this uuid\n");
			err = -EADDRINUSE;
			goto sysfs_err;
		}
	}

	err = create_dynamic_oa_sysfs_entry(perf, oa_config);
	if (err) {
		DRM_DEBUG("Failed to create sysfs entry for OA config\n");
		goto sysfs_err;
	}

	/* Config id 0 is invalid, id 1 for kernel stored test config. */
	oa_config->id = idr_alloc(&perf->metrics_idr,
				  oa_config, 2,
				  0, GFP_KERNEL);
	if (oa_config->id < 0) {
		DRM_DEBUG("Failed to create sysfs entry for OA config\n");
		err = oa_config->id;
		goto sysfs_err;
	}

	mutex_unlock(&perf->metrics_lock);

	DRM_DEBUG("Added config %s id=%i\n", oa_config->uuid, oa_config->id);

	return oa_config->id;

sysfs_err:
	mutex_unlock(&perf->metrics_lock);
reg_err:
	i915_oa_config_put(oa_config);
	DRM_DEBUG("Failed to add new OA config\n");
	return err;
}

/**
 * i915_perf_remove_config_ioctl - DRM ioctl() for userspace to remove an OA config
 * @dev: drm device
 * @data: ioctl data (pointer to u64 integer) copied from userspace
 * @file: drm file
 *
 * Configs can be removed while being used, the will stop appearing in sysfs
 * and their content will be freed when the stream using the config is closed.
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int i915_perf_remove_config_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file)
{
	struct i915_perf *perf = &to_i915(dev)->perf;
	u64 *arg = data;
	struct i915_oa_config *oa_config;
	int ret;

	if (!perf->i915) {
		DRM_DEBUG("i915 perf interface not available for this system\n");
		return -ENOTSUPP;
	}

	if (i915_perf_stream_paranoid && !perfmon_capable()) {
		DRM_DEBUG("Insufficient privileges to remove i915 OA config\n");
		return -EACCES;
	}

	ret = mutex_lock_interruptible(&perf->metrics_lock);
	if (ret)
		return ret;

	oa_config = idr_find(&perf->metrics_idr, *arg);
	if (!oa_config) {
		DRM_DEBUG("Failed to remove unknown OA config\n");
		ret = -ENOENT;
		goto err_unlock;
	}

	GEM_BUG_ON(*arg != oa_config->id);

	sysfs_remove_group(perf->metrics_kobj, &oa_config->sysfs_metric);

	idr_remove(&perf->metrics_idr, *arg);

	mutex_unlock(&perf->metrics_lock);

	DRM_DEBUG("Removed config %s id=%i\n", oa_config->uuid, oa_config->id);

	i915_oa_config_put(oa_config);

	return 0;

err_unlock:
	mutex_unlock(&perf->metrics_lock);
	return ret;
}

static struct ctl_table oa_table[] = {
	{
	 .procname = "perf_stream_paranoid",
	 .data = &i915_perf_stream_paranoid,
	 .maxlen = sizeof(i915_perf_stream_paranoid),
	 .mode = 0644,
	 .proc_handler = proc_dointvec_minmax,
	 .extra1 = SYSCTL_ZERO,
	 .extra2 = SYSCTL_ONE,
	 },
	{
	 .procname = "oa_max_sample_rate",
	 .data = &i915_oa_max_sample_rate,
	 .maxlen = sizeof(i915_oa_max_sample_rate),
	 .mode = 0644,
	 .proc_handler = proc_dointvec_minmax,
	 .extra1 = SYSCTL_ZERO,
	 .extra2 = &oa_sample_rate_hard_limit,
	 },
	{}
};

static void oa_init_supported_formats(struct i915_perf *perf)
{
	struct drm_i915_private *i915 = perf->i915;
	enum intel_platform platform = INTEL_INFO(i915)->platform;

	switch (platform) {
	case INTEL_HASWELL:
		oa_format_add(perf, I915_OA_FORMAT_A13);
		oa_format_add(perf, I915_OA_FORMAT_A13);
		oa_format_add(perf, I915_OA_FORMAT_A29);
		oa_format_add(perf, I915_OA_FORMAT_A13_B8_C8);
		oa_format_add(perf, I915_OA_FORMAT_B4_C8);
		oa_format_add(perf, I915_OA_FORMAT_A45_B8_C8);
		oa_format_add(perf, I915_OA_FORMAT_B4_C8_A16);
		oa_format_add(perf, I915_OA_FORMAT_C4_B8);
		break;

	case INTEL_BROADWELL:
	case INTEL_CHERRYVIEW:
	case INTEL_SKYLAKE:
	case INTEL_BROXTON:
	case INTEL_KABYLAKE:
	case INTEL_GEMINILAKE:
	case INTEL_COFFEELAKE:
	case INTEL_COMETLAKE:
	case INTEL_ICELAKE:
	case INTEL_ELKHARTLAKE:
	case INTEL_JASPERLAKE:
	case INTEL_TIGERLAKE:
	case INTEL_ROCKETLAKE:
	case INTEL_DG1:
	case INTEL_ALDERLAKE_S:
	case INTEL_ALDERLAKE_P:
		oa_format_add(perf, I915_OA_FORMAT_A12);
		oa_format_add(perf, I915_OA_FORMAT_A12_B8_C8);
		oa_format_add(perf, I915_OA_FORMAT_A32u40_A4u32_B8_C8);
		oa_format_add(perf, I915_OA_FORMAT_C4_B8);
		break;

	default:
		MISSING_CASE(platform);
	}
}

/**
 * i915_perf_init - initialize i915-perf state on module bind
 * @i915: i915 device instance
 *
 * Initializes i915-perf state without exposing anything to userspace.
 *
 * Note: i915-perf initialization is split into an 'init' and 'register'
 * phase with the i915_perf_register() exposing state to userspace.
 */
void i915_perf_init(struct drm_i915_private *i915)
{
	struct i915_perf *perf = &i915->perf;

	/* XXX const struct i915_perf_ops! */

	perf->oa_formats = oa_formats;
	if (IS_HASWELL(i915)) {
		perf->ops.is_valid_b_counter_reg = gen7_is_valid_b_counter_addr;
		perf->ops.is_valid_mux_reg = hsw_is_valid_mux_addr;
		perf->ops.is_valid_flex_reg = NULL;
		perf->ops.enable_metric_set = hsw_enable_metric_set;
		perf->ops.disable_metric_set = hsw_disable_metric_set;
		perf->ops.oa_enable = gen7_oa_enable;
		perf->ops.oa_disable = gen7_oa_disable;
		perf->ops.read = gen7_oa_read;
		perf->ops.oa_hw_tail_read = gen7_oa_hw_tail_read;
	} else if (HAS_LOGICAL_RING_CONTEXTS(i915)) {
		/* Note: that although we could theoretically also support the
		 * legacy ringbuffer mode on BDW (and earlier iterations of
		 * this driver, before upstreaming did this) it didn't seem
		 * worth the complexity to maintain now that BDW+ enable
		 * execlist mode by default.
		 */
		perf->ops.read = gen8_oa_read;

		if (IS_GRAPHICS_VER(i915, 8, 9)) {
			perf->ops.is_valid_b_counter_reg =
				gen7_is_valid_b_counter_addr;
			perf->ops.is_valid_mux_reg =
				gen8_is_valid_mux_addr;
			perf->ops.is_valid_flex_reg =
				gen8_is_valid_flex_addr;

			if (IS_CHERRYVIEW(i915)) {
				perf->ops.is_valid_mux_reg =
					chv_is_valid_mux_addr;
			}

			perf->ops.oa_enable = gen8_oa_enable;
			perf->ops.oa_disable = gen8_oa_disable;
			perf->ops.enable_metric_set = gen8_enable_metric_set;
			perf->ops.disable_metric_set = gen8_disable_metric_set;
			perf->ops.oa_hw_tail_read = gen8_oa_hw_tail_read;

			if (GRAPHICS_VER(i915) == 8) {
				perf->ctx_oactxctrl_offset = 0x120;
				perf->ctx_flexeu0_offset = 0x2ce;

				perf->gen8_valid_ctx_bit = BIT(25);
			} else {
				perf->ctx_oactxctrl_offset = 0x128;
				perf->ctx_flexeu0_offset = 0x3de;

				perf->gen8_valid_ctx_bit = BIT(16);
			}
		} else if (GRAPHICS_VER(i915) == 11) {
			perf->ops.is_valid_b_counter_reg =
				gen7_is_valid_b_counter_addr;
			perf->ops.is_valid_mux_reg =
				gen11_is_valid_mux_addr;
			perf->ops.is_valid_flex_reg =
				gen8_is_valid_flex_addr;

			perf->ops.oa_enable = gen8_oa_enable;
			perf->ops.oa_disable = gen8_oa_disable;
			perf->ops.enable_metric_set = gen8_enable_metric_set;
			perf->ops.disable_metric_set = gen11_disable_metric_set;
			perf->ops.oa_hw_tail_read = gen8_oa_hw_tail_read;

			perf->ctx_oactxctrl_offset = 0x124;
			perf->ctx_flexeu0_offset = 0x78e;

			perf->gen8_valid_ctx_bit = BIT(16);
		} else if (GRAPHICS_VER(i915) == 12) {
			perf->ops.is_valid_b_counter_reg =
				gen12_is_valid_b_counter_addr;
			perf->ops.is_valid_mux_reg =
				gen12_is_valid_mux_addr;
			perf->ops.is_valid_flex_reg =
				gen8_is_valid_flex_addr;

			perf->ops.oa_enable = gen12_oa_enable;
			perf->ops.oa_disable = gen12_oa_disable;
			perf->ops.enable_metric_set = gen12_enable_metric_set;
			perf->ops.disable_metric_set = gen12_disable_metric_set;
			perf->ops.oa_hw_tail_read = gen12_oa_hw_tail_read;

			perf->ctx_flexeu0_offset = 0;
			perf->ctx_oactxctrl_offset = 0x144;
		}
	}

	if (perf->ops.enable_metric_set) {
		mutex_init(&perf->lock);

		/* Choose a representative limit */
		oa_sample_rate_hard_limit = to_gt(i915)->clock_frequency / 2;

		mutex_init(&perf->metrics_lock);
		idr_init_base(&perf->metrics_idr, 1);

		/* We set up some ratelimit state to potentially throttle any
		 * _NOTES about spurious, invalid OA reports which we don't
		 * forward to userspace.
		 *
		 * We print a _NOTE about any throttling when closing the
		 * stream instead of waiting until driver _fini which no one
		 * would ever see.
		 *
		 * Using the same limiting factors as printk_ratelimit()
		 */
		ratelimit_state_init(&perf->spurious_report_rs, 5 * HZ, 10);
		/* Since we use a DRM_NOTE for spurious reports it would be
		 * inconsistent to let __ratelimit() automatically print a
		 * warning for throttling.
		 */
		ratelimit_set_flags(&perf->spurious_report_rs,
				    RATELIMIT_MSG_ON_RELEASE);

		ratelimit_state_init(&perf->tail_pointer_race,
				     5 * HZ, 10);
		ratelimit_set_flags(&perf->tail_pointer_race,
				    RATELIMIT_MSG_ON_RELEASE);

		atomic64_set(&perf->noa_programming_delay,
			     500 * 1000 /* 500us */);

		perf->i915 = i915;

		oa_init_supported_formats(perf);
	}
}

static int destroy_config(int id, void *p, void *data)
{
	i915_oa_config_put(p);
	return 0;
}

int i915_perf_sysctl_register(void)
{
	sysctl_header = register_sysctl("dev/i915", oa_table);
	return 0;
}

void i915_perf_sysctl_unregister(void)
{
	unregister_sysctl_table(sysctl_header);
}

/**
 * i915_perf_fini - Counter part to i915_perf_init()
 * @i915: i915 device instance
 */
void i915_perf_fini(struct drm_i915_private *i915)
{
	struct i915_perf *perf = &i915->perf;

	if (!perf->i915)
		return;

	idr_for_each(&perf->metrics_idr, destroy_config, perf);
	idr_destroy(&perf->metrics_idr);

	memset(&perf->ops, 0, sizeof(perf->ops));
	perf->i915 = NULL;
}

/**
 * i915_perf_ioctl_version - Version of the i915-perf subsystem
 *
 * This version number is used by userspace to detect available features.
 */
int i915_perf_ioctl_version(void)
{
	/*
	 * 1: Initial version
	 *   I915_PERF_IOCTL_ENABLE
	 *   I915_PERF_IOCTL_DISABLE
	 *
	 * 2: Added runtime modification of OA config.
	 *   I915_PERF_IOCTL_CONFIG
	 *
	 * 3: Add DRM_I915_PERF_PROP_HOLD_PREEMPTION parameter to hold
	 *    preemption on a particular context so that performance data is
	 *    accessible from a delta of MI_RPC reports without looking at the
	 *    OA buffer.
	 *
	 * 4: Add DRM_I915_PERF_PROP_ALLOWED_SSEU to limit what contexts can
	 *    be run for the duration of the performance recording based on
	 *    their SSEU configuration.
	 *
	 * 5: Add DRM_I915_PERF_PROP_POLL_OA_PERIOD parameter that controls the
	 *    interval for the hrtimer used to check for OA data.
	 */
	return 5;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/i915_perf.c"
#endif
