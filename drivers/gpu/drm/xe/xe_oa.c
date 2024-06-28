// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#include <linux/anon_inodes.h>
#include <linux/delay.h>
#include <linux/nospec.h>
#include <linux/poll.h>

#include <drm/drm_drv.h>
#include <drm/drm_managed.h>
#include <drm/xe_drm.h>

#include "abi/guc_actions_slpc_abi.h"
#include "instructions/xe_mi_commands.h"
#include "regs/xe_engine_regs.h"
#include "regs/xe_gt_regs.h"
#include "regs/xe_lrc_layout.h"
#include "regs/xe_oa_regs.h"
#include "xe_assert.h"
#include "xe_bb.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_exec_queue.h"
#include "xe_force_wake.h"
#include "xe_gt.h"
#include "xe_gt_mcr.h"
#include "xe_gt_printk.h"
#include "xe_guc_pc.h"
#include "xe_lrc.h"
#include "xe_macros.h"
#include "xe_mmio.h"
#include "xe_oa.h"
#include "xe_perf.h"
#include "xe_pm.h"
#include "xe_sched_job.h"
#include "xe_sriov.h"

#define DEFAULT_POLL_FREQUENCY_HZ 200
#define DEFAULT_POLL_PERIOD_NS (NSEC_PER_SEC / DEFAULT_POLL_FREQUENCY_HZ)
#define XE_OA_UNIT_INVALID U32_MAX

struct xe_oa_reg {
	struct xe_reg addr;
	u32 value;
};

struct xe_oa_config {
	struct xe_oa *oa;

	char uuid[UUID_STRING_LEN + 1];
	int id;

	const struct xe_oa_reg *regs;
	u32 regs_len;

	struct attribute_group sysfs_metric;
	struct attribute *attrs[2];
	struct kobj_attribute sysfs_metric_id;

	struct kref ref;
	struct rcu_head rcu;
};

struct flex {
	struct xe_reg reg;
	u32 offset;
	u32 value;
};

struct xe_oa_open_param {
	u32 oa_unit_id;
	bool sample;
	u32 metric_set;
	enum xe_oa_format_name oa_format;
	int period_exponent;
	bool disabled;
	int exec_queue_id;
	int engine_instance;
	struct xe_exec_queue *exec_q;
	struct xe_hw_engine *hwe;
	bool no_preempt;
};

struct xe_oa_config_bo {
	struct llist_node node;

	struct xe_oa_config *oa_config;
	struct xe_bb *bb;
};

#define DRM_FMT(x) DRM_XE_OA_FMT_TYPE_##x

static const struct xe_oa_format oa_formats[] = {
	[XE_OA_FORMAT_C4_B8]			= { 7, 64,  DRM_FMT(OAG) },
	[XE_OA_FORMAT_A12]			= { 0, 64,  DRM_FMT(OAG) },
	[XE_OA_FORMAT_A12_B8_C8]		= { 2, 128, DRM_FMT(OAG) },
	[XE_OA_FORMAT_A32u40_A4u32_B8_C8]	= { 5, 256, DRM_FMT(OAG) },
	[XE_OAR_FORMAT_A32u40_A4u32_B8_C8]	= { 5, 256, DRM_FMT(OAR) },
	[XE_OA_FORMAT_A24u40_A14u32_B8_C8]	= { 5, 256, DRM_FMT(OAG) },
	[XE_OAC_FORMAT_A24u64_B8_C8]		= { 1, 320, DRM_FMT(OAC), HDR_64_BIT },
	[XE_OAC_FORMAT_A22u32_R2u32_B8_C8]	= { 2, 192, DRM_FMT(OAC), HDR_64_BIT },
	[XE_OAM_FORMAT_MPEC8u64_B8_C8]		= { 1, 192, DRM_FMT(OAM_MPEC), HDR_64_BIT },
	[XE_OAM_FORMAT_MPEC8u32_B8_C8]		= { 2, 128, DRM_FMT(OAM_MPEC), HDR_64_BIT },
	[XE_OA_FORMAT_PEC64u64]			= { 1, 576, DRM_FMT(PEC), HDR_64_BIT, 1, 0 },
	[XE_OA_FORMAT_PEC64u64_B8_C8]		= { 1, 640, DRM_FMT(PEC), HDR_64_BIT, 1, 1 },
	[XE_OA_FORMAT_PEC64u32]			= { 1, 320, DRM_FMT(PEC), HDR_64_BIT },
	[XE_OA_FORMAT_PEC32u64_G1]		= { 5, 320, DRM_FMT(PEC), HDR_64_BIT, 1, 0 },
	[XE_OA_FORMAT_PEC32u32_G1]		= { 5, 192, DRM_FMT(PEC), HDR_64_BIT },
	[XE_OA_FORMAT_PEC32u64_G2]		= { 6, 320, DRM_FMT(PEC), HDR_64_BIT, 1, 0 },
	[XE_OA_FORMAT_PEC32u32_G2]		= { 6, 192, DRM_FMT(PEC), HDR_64_BIT },
	[XE_OA_FORMAT_PEC36u64_G1_32_G2_4]	= { 3, 320, DRM_FMT(PEC), HDR_64_BIT, 1, 0 },
	[XE_OA_FORMAT_PEC36u64_G1_4_G2_32]	= { 4, 320, DRM_FMT(PEC), HDR_64_BIT, 1, 0 },
};

static u32 xe_oa_circ_diff(struct xe_oa_stream *stream, u32 tail, u32 head)
{
	return tail >= head ? tail - head :
		tail + stream->oa_buffer.circ_size - head;
}

static u32 xe_oa_circ_incr(struct xe_oa_stream *stream, u32 ptr, u32 n)
{
	return ptr + n >= stream->oa_buffer.circ_size ?
		ptr + n - stream->oa_buffer.circ_size : ptr + n;
}

static void xe_oa_config_release(struct kref *ref)
{
	struct xe_oa_config *oa_config =
		container_of(ref, typeof(*oa_config), ref);

	kfree(oa_config->regs);

	kfree_rcu(oa_config, rcu);
}

static void xe_oa_config_put(struct xe_oa_config *oa_config)
{
	if (!oa_config)
		return;

	kref_put(&oa_config->ref, xe_oa_config_release);
}

static struct xe_oa_config *xe_oa_config_get(struct xe_oa_config *oa_config)
{
	return kref_get_unless_zero(&oa_config->ref) ? oa_config : NULL;
}

static struct xe_oa_config *xe_oa_get_oa_config(struct xe_oa *oa, int metrics_set)
{
	struct xe_oa_config *oa_config;

	rcu_read_lock();
	oa_config = idr_find(&oa->metrics_idr, metrics_set);
	if (oa_config)
		oa_config = xe_oa_config_get(oa_config);
	rcu_read_unlock();

	return oa_config;
}

static void free_oa_config_bo(struct xe_oa_config_bo *oa_bo)
{
	xe_oa_config_put(oa_bo->oa_config);
	xe_bb_free(oa_bo->bb, NULL);
	kfree(oa_bo);
}

static const struct xe_oa_regs *__oa_regs(struct xe_oa_stream *stream)
{
	return &stream->hwe->oa_unit->regs;
}

static u32 xe_oa_hw_tail_read(struct xe_oa_stream *stream)
{
	return xe_mmio_read32(stream->gt, __oa_regs(stream)->oa_tail_ptr) &
		OAG_OATAILPTR_MASK;
}

#define oa_report_header_64bit(__s) \
	((__s)->oa_buffer.format->header == HDR_64_BIT)

static u64 oa_report_id(struct xe_oa_stream *stream, void *report)
{
	return oa_report_header_64bit(stream) ? *(u64 *)report : *(u32 *)report;
}

static void oa_report_id_clear(struct xe_oa_stream *stream, u32 *report)
{
	if (oa_report_header_64bit(stream))
		*(u64 *)report = 0;
	else
		*report = 0;
}

static u64 oa_timestamp(struct xe_oa_stream *stream, void *report)
{
	return oa_report_header_64bit(stream) ?
		*((u64 *)report + 1) :
		*((u32 *)report + 1);
}

static void oa_timestamp_clear(struct xe_oa_stream *stream, u32 *report)
{
	if (oa_report_header_64bit(stream))
		*(u64 *)&report[2] = 0;
	else
		report[1] = 0;
}

static bool xe_oa_buffer_check_unlocked(struct xe_oa_stream *stream)
{
	u32 gtt_offset = xe_bo_ggtt_addr(stream->oa_buffer.bo);
	int report_size = stream->oa_buffer.format->size;
	u32 tail, hw_tail;
	unsigned long flags;
	bool pollin;
	u32 partial_report_size;

	spin_lock_irqsave(&stream->oa_buffer.ptr_lock, flags);

	hw_tail = xe_oa_hw_tail_read(stream);
	hw_tail -= gtt_offset;

	/*
	 * The tail pointer increases in 64 byte (cacheline size), not in report_size
	 * increments. Also report size may not be a power of 2. Compute potential
	 * partially landed report in OA buffer.
	 */
	partial_report_size = xe_oa_circ_diff(stream, hw_tail, stream->oa_buffer.tail);
	partial_report_size %= report_size;

	/* Subtract partial amount off the tail */
	hw_tail = xe_oa_circ_diff(stream, hw_tail, partial_report_size);

	tail = hw_tail;

	/*
	 * Walk the stream backward until we find a report with report id and timestamp
	 * not 0. We can't tell whether a report has fully landed in memory before the
	 * report id and timestamp of the following report have landed.
	 *
	 * This is assuming that the writes of the OA unit land in memory in the order
	 * they were written.  If not : (╯°□°）╯︵ ┻━┻
	 */
	while (xe_oa_circ_diff(stream, tail, stream->oa_buffer.tail) >= report_size) {
		void *report = stream->oa_buffer.vaddr + tail;

		if (oa_report_id(stream, report) || oa_timestamp(stream, report))
			break;

		tail = xe_oa_circ_diff(stream, tail, report_size);
	}

	if (xe_oa_circ_diff(stream, hw_tail, tail) > report_size)
		drm_dbg(&stream->oa->xe->drm,
			"unlanded report(s) head=0x%x tail=0x%x hw_tail=0x%x\n",
			stream->oa_buffer.head, tail, hw_tail);

	stream->oa_buffer.tail = tail;

	pollin = xe_oa_circ_diff(stream, stream->oa_buffer.tail,
				 stream->oa_buffer.head) >= report_size;

	spin_unlock_irqrestore(&stream->oa_buffer.ptr_lock, flags);

	return pollin;
}

static enum hrtimer_restart xe_oa_poll_check_timer_cb(struct hrtimer *hrtimer)
{
	struct xe_oa_stream *stream =
		container_of(hrtimer, typeof(*stream), poll_check_timer);

	if (xe_oa_buffer_check_unlocked(stream)) {
		stream->pollin = true;
		wake_up(&stream->poll_wq);
	}

	hrtimer_forward_now(hrtimer, ns_to_ktime(stream->poll_period_ns));

	return HRTIMER_RESTART;
}

static int xe_oa_append_report(struct xe_oa_stream *stream, char __user *buf,
			       size_t count, size_t *offset, const u8 *report)
{
	int report_size = stream->oa_buffer.format->size;
	int report_size_partial;
	u8 *oa_buf_end;

	if ((count - *offset) < report_size)
		return -ENOSPC;

	buf += *offset;

	oa_buf_end = stream->oa_buffer.vaddr + stream->oa_buffer.circ_size;
	report_size_partial = oa_buf_end - report;

	if (report_size_partial < report_size) {
		if (copy_to_user(buf, report, report_size_partial))
			return -EFAULT;
		buf += report_size_partial;

		if (copy_to_user(buf, stream->oa_buffer.vaddr,
				 report_size - report_size_partial))
			return -EFAULT;
	} else if (copy_to_user(buf, report, report_size)) {
		return -EFAULT;
	}

	*offset += report_size;

	return 0;
}

static int xe_oa_append_reports(struct xe_oa_stream *stream, char __user *buf,
				size_t count, size_t *offset)
{
	int report_size = stream->oa_buffer.format->size;
	u8 *oa_buf_base = stream->oa_buffer.vaddr;
	u32 gtt_offset = xe_bo_ggtt_addr(stream->oa_buffer.bo);
	size_t start_offset = *offset;
	unsigned long flags;
	u32 head, tail;
	int ret = 0;

	spin_lock_irqsave(&stream->oa_buffer.ptr_lock, flags);
	head = stream->oa_buffer.head;
	tail = stream->oa_buffer.tail;
	spin_unlock_irqrestore(&stream->oa_buffer.ptr_lock, flags);

	xe_assert(stream->oa->xe,
		  head < stream->oa_buffer.circ_size && tail < stream->oa_buffer.circ_size);

	for (; xe_oa_circ_diff(stream, tail, head);
	     head = xe_oa_circ_incr(stream, head, report_size)) {
		u8 *report = oa_buf_base + head;

		ret = xe_oa_append_report(stream, buf, count, offset, report);
		if (ret)
			break;

		if (!(stream->oa_buffer.circ_size % report_size)) {
			/* Clear out report id and timestamp to detect unlanded reports */
			oa_report_id_clear(stream, (void *)report);
			oa_timestamp_clear(stream, (void *)report);
		} else {
			u8 *oa_buf_end = stream->oa_buffer.vaddr + stream->oa_buffer.circ_size;
			u32 part = oa_buf_end - report;

			/* Zero out the entire report */
			if (report_size <= part) {
				memset(report, 0, report_size);
			} else {
				memset(report, 0, part);
				memset(oa_buf_base, 0, report_size - part);
			}
		}
	}

	if (start_offset != *offset) {
		struct xe_reg oaheadptr = __oa_regs(stream)->oa_head_ptr;

		spin_lock_irqsave(&stream->oa_buffer.ptr_lock, flags);
		xe_mmio_write32(stream->gt, oaheadptr,
				(head + gtt_offset) & OAG_OAHEADPTR_MASK);
		stream->oa_buffer.head = head;
		spin_unlock_irqrestore(&stream->oa_buffer.ptr_lock, flags);
	}

	return ret;
}

static void xe_oa_init_oa_buffer(struct xe_oa_stream *stream)
{
	u32 gtt_offset = xe_bo_ggtt_addr(stream->oa_buffer.bo);
	u32 oa_buf = gtt_offset | OABUFFER_SIZE_16M | OAG_OABUFFER_MEMORY_SELECT;
	unsigned long flags;

	spin_lock_irqsave(&stream->oa_buffer.ptr_lock, flags);

	xe_mmio_write32(stream->gt, __oa_regs(stream)->oa_status, 0);
	xe_mmio_write32(stream->gt, __oa_regs(stream)->oa_head_ptr,
			gtt_offset & OAG_OAHEADPTR_MASK);
	stream->oa_buffer.head = 0;
	/*
	 * PRM says: "This MMIO must be set before the OATAILPTR register and after the
	 * OAHEADPTR register. This is to enable proper functionality of the overflow bit".
	 */
	xe_mmio_write32(stream->gt, __oa_regs(stream)->oa_buffer, oa_buf);
	xe_mmio_write32(stream->gt, __oa_regs(stream)->oa_tail_ptr,
			gtt_offset & OAG_OATAILPTR_MASK);

	/* Mark that we need updated tail pointer to read from */
	stream->oa_buffer.tail = 0;

	spin_unlock_irqrestore(&stream->oa_buffer.ptr_lock, flags);

	/* Zero out the OA buffer since we rely on zero report id and timestamp fields */
	memset(stream->oa_buffer.vaddr, 0, stream->oa_buffer.bo->size);
}

static u32 __format_to_oactrl(const struct xe_oa_format *format, int counter_sel_mask)
{
	return ((format->counter_select << (ffs(counter_sel_mask) - 1)) & counter_sel_mask) |
		REG_FIELD_PREP(OA_OACONTROL_REPORT_BC_MASK, format->bc_report) |
		REG_FIELD_PREP(OA_OACONTROL_COUNTER_SIZE_MASK, format->counter_size);
}

static u32 __oa_ccs_select(struct xe_oa_stream *stream)
{
	u32 val;

	if (stream->hwe->class != XE_ENGINE_CLASS_COMPUTE)
		return 0;

	val = REG_FIELD_PREP(OAG_OACONTROL_OA_CCS_SELECT_MASK, stream->hwe->instance);
	xe_assert(stream->oa->xe,
		  REG_FIELD_GET(OAG_OACONTROL_OA_CCS_SELECT_MASK, val) == stream->hwe->instance);
	return val;
}

static void xe_oa_enable(struct xe_oa_stream *stream)
{
	const struct xe_oa_format *format = stream->oa_buffer.format;
	const struct xe_oa_regs *regs;
	u32 val;

	/*
	 * BSpec: 46822: Bit 0. Even if stream->sample is 0, for OAR to function, the OA
	 * buffer must be correctly initialized
	 */
	xe_oa_init_oa_buffer(stream);

	regs = __oa_regs(stream);
	val = __format_to_oactrl(format, regs->oa_ctrl_counter_select_mask) |
		__oa_ccs_select(stream) | OAG_OACONTROL_OA_COUNTER_ENABLE;

	xe_mmio_write32(stream->gt, regs->oa_ctrl, val);
}

static void xe_oa_disable(struct xe_oa_stream *stream)
{
	xe_mmio_write32(stream->gt, __oa_regs(stream)->oa_ctrl, 0);
	if (xe_mmio_wait32(stream->gt, __oa_regs(stream)->oa_ctrl,
			   OAG_OACONTROL_OA_COUNTER_ENABLE, 0, 50000, NULL, false))
		drm_err(&stream->oa->xe->drm,
			"wait for OA to be disabled timed out\n");

	if (GRAPHICS_VERx100(stream->oa->xe) <= 1270 && GRAPHICS_VERx100(stream->oa->xe) != 1260) {
		/* <= XE_METEORLAKE except XE_PVC */
		xe_mmio_write32(stream->gt, OA_TLB_INV_CR, 1);
		if (xe_mmio_wait32(stream->gt, OA_TLB_INV_CR, 1, 0, 50000, NULL, false))
			drm_err(&stream->oa->xe->drm,
				"wait for OA tlb invalidate timed out\n");
	}
}

static int xe_oa_wait_unlocked(struct xe_oa_stream *stream)
{
	/* We might wait indefinitely if periodic sampling is not enabled */
	if (!stream->periodic)
		return -EINVAL;

	return wait_event_interruptible(stream->poll_wq,
					xe_oa_buffer_check_unlocked(stream));
}

#define OASTATUS_RELEVANT_BITS (OASTATUS_MMIO_TRG_Q_FULL | OASTATUS_COUNTER_OVERFLOW | \
				OASTATUS_BUFFER_OVERFLOW | OASTATUS_REPORT_LOST)

static int __xe_oa_read(struct xe_oa_stream *stream, char __user *buf,
			size_t count, size_t *offset)
{
	/* Only clear our bits to avoid side-effects */
	stream->oa_status = xe_mmio_rmw32(stream->gt, __oa_regs(stream)->oa_status,
					  OASTATUS_RELEVANT_BITS, 0);
	/*
	 * Signal to userspace that there is non-zero OA status to read via
	 * @DRM_XE_PERF_IOCTL_STATUS perf fd ioctl
	 */
	if (stream->oa_status & OASTATUS_RELEVANT_BITS)
		return -EIO;

	return xe_oa_append_reports(stream, buf, count, offset);
}

static ssize_t xe_oa_read(struct file *file, char __user *buf,
			  size_t count, loff_t *ppos)
{
	struct xe_oa_stream *stream = file->private_data;
	size_t offset = 0;
	int ret;

	/* Can't read from disabled streams */
	if (!stream->enabled || !stream->sample)
		return -EINVAL;

	if (!(file->f_flags & O_NONBLOCK)) {
		do {
			ret = xe_oa_wait_unlocked(stream);
			if (ret)
				return ret;

			mutex_lock(&stream->stream_lock);
			ret = __xe_oa_read(stream, buf, count, &offset);
			mutex_unlock(&stream->stream_lock);
		} while (!offset && !ret);
	} else {
		mutex_lock(&stream->stream_lock);
		ret = __xe_oa_read(stream, buf, count, &offset);
		mutex_unlock(&stream->stream_lock);
	}

	/*
	 * Typically we clear pollin here in order to wait for the new hrtimer callback
	 * before unblocking. The exception to this is if __xe_oa_read returns -ENOSPC,
	 * which means that more OA data is available than could fit in the user provided
	 * buffer. In this case we want the next poll() call to not block.
	 *
	 * Also in case of -EIO, we have already waited for data before returning
	 * -EIO, so need to wait again
	 */
	if (ret != -ENOSPC && ret != -EIO)
		stream->pollin = false;

	/* Possible values for ret are 0, -EFAULT, -ENOSPC, -EIO, -EINVAL, ... */
	return offset ?: (ret ?: -EAGAIN);
}

static __poll_t xe_oa_poll_locked(struct xe_oa_stream *stream,
				  struct file *file, poll_table *wait)
{
	__poll_t events = 0;

	poll_wait(file, &stream->poll_wq, wait);

	/*
	 * We don't explicitly check whether there's something to read here since this
	 * path may be hot depending on what else userspace is polling, or on the timeout
	 * in use. We rely on hrtimer xe_oa_poll_check_timer_cb to notify us when there
	 * are samples to read
	 */
	if (stream->pollin)
		events |= EPOLLIN;

	return events;
}

static __poll_t xe_oa_poll(struct file *file, poll_table *wait)
{
	struct xe_oa_stream *stream = file->private_data;
	__poll_t ret;

	mutex_lock(&stream->stream_lock);
	ret = xe_oa_poll_locked(stream, file, wait);
	mutex_unlock(&stream->stream_lock);

	return ret;
}

static int xe_oa_submit_bb(struct xe_oa_stream *stream, struct xe_bb *bb)
{
	struct xe_sched_job *job;
	struct dma_fence *fence;
	long timeout;
	int err = 0;

	/* Kernel configuration is issued on stream->k_exec_q, not stream->exec_q */
	job = xe_bb_create_job(stream->k_exec_q, bb);
	if (IS_ERR(job)) {
		err = PTR_ERR(job);
		goto exit;
	}

	xe_sched_job_arm(job);
	fence = dma_fence_get(&job->drm.s_fence->finished);
	xe_sched_job_push(job);

	timeout = dma_fence_wait_timeout(fence, false, HZ);
	dma_fence_put(fence);
	if (timeout < 0)
		err = timeout;
	else if (!timeout)
		err = -ETIME;
exit:
	return err;
}

static void write_cs_mi_lri(struct xe_bb *bb, const struct xe_oa_reg *reg_data, u32 n_regs)
{
	u32 i;

#define MI_LOAD_REGISTER_IMM_MAX_REGS (126)

	for (i = 0; i < n_regs; i++) {
		if ((i % MI_LOAD_REGISTER_IMM_MAX_REGS) == 0) {
			u32 n_lri = min_t(u32, n_regs - i,
					  MI_LOAD_REGISTER_IMM_MAX_REGS);

			bb->cs[bb->len++] = MI_LOAD_REGISTER_IMM | MI_LRI_NUM_REGS(n_lri);
		}
		bb->cs[bb->len++] = reg_data[i].addr.addr;
		bb->cs[bb->len++] = reg_data[i].value;
	}
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

static void xe_oa_free_oa_buffer(struct xe_oa_stream *stream)
{
	xe_bo_unpin_map_no_vm(stream->oa_buffer.bo);
}

static void xe_oa_free_configs(struct xe_oa_stream *stream)
{
	struct xe_oa_config_bo *oa_bo, *tmp;

	xe_oa_config_put(stream->oa_config);
	llist_for_each_entry_safe(oa_bo, tmp, stream->oa_config_bos.first, node)
		free_oa_config_bo(oa_bo);
}

static void xe_oa_store_flex(struct xe_oa_stream *stream, struct xe_lrc *lrc,
			     struct xe_bb *bb, const struct flex *flex, u32 count)
{
	u32 offset = xe_bo_ggtt_addr(lrc->bo);

	do {
		bb->cs[bb->len++] = MI_STORE_DATA_IMM | BIT(22) /* GGTT */ | 2;
		bb->cs[bb->len++] = offset + flex->offset * sizeof(u32);
		bb->cs[bb->len++] = 0;
		bb->cs[bb->len++] = flex->value;

	} while (flex++, --count);
}

static int xe_oa_modify_ctx_image(struct xe_oa_stream *stream, struct xe_lrc *lrc,
				  const struct flex *flex, u32 count)
{
	struct xe_bb *bb;
	int err;

	bb = xe_bb_new(stream->gt, 4 * count, false);
	if (IS_ERR(bb)) {
		err = PTR_ERR(bb);
		goto exit;
	}

	xe_oa_store_flex(stream, lrc, bb, flex, count);

	err = xe_oa_submit_bb(stream, bb);
	xe_bb_free(bb, NULL);
exit:
	return err;
}

static int xe_oa_load_with_lri(struct xe_oa_stream *stream, struct xe_oa_reg *reg_lri)
{
	struct xe_bb *bb;
	int err;

	bb = xe_bb_new(stream->gt, 3, false);
	if (IS_ERR(bb)) {
		err = PTR_ERR(bb);
		goto exit;
	}

	write_cs_mi_lri(bb, reg_lri, 1);

	err = xe_oa_submit_bb(stream, bb);
	xe_bb_free(bb, NULL);
exit:
	return err;
}

static int xe_oa_configure_oar_context(struct xe_oa_stream *stream, bool enable)
{
	const struct xe_oa_format *format = stream->oa_buffer.format;
	struct xe_lrc *lrc = stream->exec_q->lrc[0];
	u32 regs_offset = xe_lrc_regs_offset(lrc) / sizeof(u32);
	u32 oacontrol = __format_to_oactrl(format, OAR_OACONTROL_COUNTER_SEL_MASK) |
		(enable ? OAR_OACONTROL_COUNTER_ENABLE : 0);

	struct flex regs_context[] = {
		{
			OACTXCONTROL(stream->hwe->mmio_base),
			stream->oa->ctx_oactxctrl_offset[stream->hwe->class] + 1,
			enable ? OA_COUNTER_RESUME : 0,
		},
		{
			RING_CONTEXT_CONTROL(stream->hwe->mmio_base),
			regs_offset + CTX_CONTEXT_CONTROL,
			_MASKED_FIELD(CTX_CTRL_OAC_CONTEXT_ENABLE,
				      enable ? CTX_CTRL_OAC_CONTEXT_ENABLE : 0)
		},
	};
	struct xe_oa_reg reg_lri = { OAR_OACONTROL, oacontrol };
	int err;

	/* Modify stream hwe context image with regs_context */
	err = xe_oa_modify_ctx_image(stream, stream->exec_q->lrc[0],
				     regs_context, ARRAY_SIZE(regs_context));
	if (err)
		return err;

	/* Apply reg_lri using LRI */
	return xe_oa_load_with_lri(stream, &reg_lri);
}

static int xe_oa_configure_oac_context(struct xe_oa_stream *stream, bool enable)
{
	const struct xe_oa_format *format = stream->oa_buffer.format;
	struct xe_lrc *lrc = stream->exec_q->lrc[0];
	u32 regs_offset = xe_lrc_regs_offset(lrc) / sizeof(u32);
	u32 oacontrol = __format_to_oactrl(format, OAR_OACONTROL_COUNTER_SEL_MASK) |
		(enable ? OAR_OACONTROL_COUNTER_ENABLE : 0);
	struct flex regs_context[] = {
		{
			OACTXCONTROL(stream->hwe->mmio_base),
			stream->oa->ctx_oactxctrl_offset[stream->hwe->class] + 1,
			enable ? OA_COUNTER_RESUME : 0,
		},
		{
			RING_CONTEXT_CONTROL(stream->hwe->mmio_base),
			regs_offset + CTX_CONTEXT_CONTROL,
			_MASKED_FIELD(CTX_CTRL_OAC_CONTEXT_ENABLE,
				      enable ? CTX_CTRL_OAC_CONTEXT_ENABLE : 0) |
			_MASKED_FIELD(CTX_CTRL_RUN_ALONE,
				      enable ? CTX_CTRL_RUN_ALONE : 0),
		},
	};
	struct xe_oa_reg reg_lri = { OAC_OACONTROL, oacontrol };
	int err;

	/* Set ccs select to enable programming of OAC_OACONTROL */
	xe_mmio_write32(stream->gt, __oa_regs(stream)->oa_ctrl, __oa_ccs_select(stream));

	/* Modify stream hwe context image with regs_context */
	err = xe_oa_modify_ctx_image(stream, stream->exec_q->lrc[0],
				     regs_context, ARRAY_SIZE(regs_context));
	if (err)
		return err;

	/* Apply reg_lri using LRI */
	return xe_oa_load_with_lri(stream, &reg_lri);
}

static int xe_oa_configure_oa_context(struct xe_oa_stream *stream, bool enable)
{
	switch (stream->hwe->class) {
	case XE_ENGINE_CLASS_RENDER:
		return xe_oa_configure_oar_context(stream, enable);
	case XE_ENGINE_CLASS_COMPUTE:
		return xe_oa_configure_oac_context(stream, enable);
	default:
		/* Video engines do not support MI_REPORT_PERF_COUNT */
		return 0;
	}
}

#define HAS_OA_BPC_REPORTING(xe) (GRAPHICS_VERx100(xe) >= 1255)

static u32 oag_configure_mmio_trigger(const struct xe_oa_stream *stream, bool enable)
{
	return _MASKED_FIELD(OAG_OA_DEBUG_DISABLE_MMIO_TRG,
			     enable && stream && stream->sample ?
			     0 : OAG_OA_DEBUG_DISABLE_MMIO_TRG);
}

static void xe_oa_disable_metric_set(struct xe_oa_stream *stream)
{
	u32 sqcnt1;

	/*
	 * Wa_1508761755:xehpsdv, dg2
	 * Enable thread stall DOP gating and EU DOP gating.
	 */
	if (stream->oa->xe->info.platform == XE_DG2) {
		xe_gt_mcr_multicast_write(stream->gt, ROW_CHICKEN,
					  _MASKED_BIT_DISABLE(STALL_DOP_GATING_DISABLE));
		xe_gt_mcr_multicast_write(stream->gt, ROW_CHICKEN2,
					  _MASKED_BIT_DISABLE(DISABLE_DOP_GATING));
	}

	xe_mmio_write32(stream->gt, __oa_regs(stream)->oa_debug,
			oag_configure_mmio_trigger(stream, false));

	/* disable the context save/restore or OAR counters */
	if (stream->exec_q)
		xe_oa_configure_oa_context(stream, false);

	/* Make sure we disable noa to save power. */
	xe_mmio_rmw32(stream->gt, RPM_CONFIG1, GT_NOA_ENABLE, 0);

	sqcnt1 = SQCNT1_PMON_ENABLE |
		 (HAS_OA_BPC_REPORTING(stream->oa->xe) ? SQCNT1_OABPC : 0);

	/* Reset PMON Enable to save power. */
	xe_mmio_rmw32(stream->gt, XELPMP_SQCNT1, sqcnt1, 0);
}

static void xe_oa_stream_destroy(struct xe_oa_stream *stream)
{
	struct xe_oa_unit *u = stream->hwe->oa_unit;
	struct xe_gt *gt = stream->hwe->gt;

	if (WARN_ON(stream != u->exclusive_stream))
		return;

	WRITE_ONCE(u->exclusive_stream, NULL);

	mutex_destroy(&stream->stream_lock);

	xe_oa_disable_metric_set(stream);
	xe_exec_queue_put(stream->k_exec_q);

	xe_oa_free_oa_buffer(stream);

	XE_WARN_ON(xe_force_wake_put(gt_to_fw(gt), XE_FORCEWAKE_ALL));
	xe_pm_runtime_put(stream->oa->xe);

	/* Wa_1509372804:pvc: Unset the override of GUCRC mode to enable rc6 */
	if (stream->override_gucrc)
		xe_gt_WARN_ON(gt, xe_guc_pc_unset_gucrc_mode(&gt->uc.guc.pc));

	xe_oa_free_configs(stream);
}

static int xe_oa_alloc_oa_buffer(struct xe_oa_stream *stream)
{
	struct xe_bo *bo;

	BUILD_BUG_ON_NOT_POWER_OF_2(XE_OA_BUFFER_SIZE);
	BUILD_BUG_ON(XE_OA_BUFFER_SIZE < SZ_128K || XE_OA_BUFFER_SIZE > SZ_16M);

	bo = xe_bo_create_pin_map(stream->oa->xe, stream->gt->tile, NULL,
				  XE_OA_BUFFER_SIZE, ttm_bo_type_kernel,
				  XE_BO_FLAG_SYSTEM | XE_BO_FLAG_GGTT);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	stream->oa_buffer.bo = bo;
	/* mmap implementation requires OA buffer to be in system memory */
	xe_assert(stream->oa->xe, bo->vmap.is_iomem == 0);
	stream->oa_buffer.vaddr = bo->vmap.vaddr;
	return 0;
}

static struct xe_oa_config_bo *
__xe_oa_alloc_config_buffer(struct xe_oa_stream *stream, struct xe_oa_config *oa_config)
{
	struct xe_oa_config_bo *oa_bo;
	size_t config_length;
	struct xe_bb *bb;

	oa_bo = kzalloc(sizeof(*oa_bo), GFP_KERNEL);
	if (!oa_bo)
		return ERR_PTR(-ENOMEM);

	config_length = num_lri_dwords(oa_config->regs_len);
	config_length = ALIGN(sizeof(u32) * config_length, XE_PAGE_SIZE) / sizeof(u32);

	bb = xe_bb_new(stream->gt, config_length, false);
	if (IS_ERR(bb))
		goto err_free;

	write_cs_mi_lri(bb, oa_config->regs, oa_config->regs_len);

	oa_bo->bb = bb;
	oa_bo->oa_config = xe_oa_config_get(oa_config);
	llist_add(&oa_bo->node, &stream->oa_config_bos);

	return oa_bo;
err_free:
	kfree(oa_bo);
	return ERR_CAST(bb);
}

static struct xe_oa_config_bo *
xe_oa_alloc_config_buffer(struct xe_oa_stream *stream, struct xe_oa_config *oa_config)
{
	struct xe_oa_config_bo *oa_bo;

	/* Look for the buffer in the already allocated BOs attached to the stream */
	llist_for_each_entry(oa_bo, stream->oa_config_bos.first, node) {
		if (oa_bo->oa_config == oa_config &&
		    memcmp(oa_bo->oa_config->uuid, oa_config->uuid,
			   sizeof(oa_config->uuid)) == 0)
			goto out;
	}

	oa_bo = __xe_oa_alloc_config_buffer(stream, oa_config);
out:
	return oa_bo;
}

static int xe_oa_emit_oa_config(struct xe_oa_stream *stream, struct xe_oa_config *config)
{
#define NOA_PROGRAM_ADDITIONAL_DELAY_US 500
	struct xe_oa_config_bo *oa_bo;
	int err, us = NOA_PROGRAM_ADDITIONAL_DELAY_US;

	oa_bo = xe_oa_alloc_config_buffer(stream, config);
	if (IS_ERR(oa_bo)) {
		err = PTR_ERR(oa_bo);
		goto exit;
	}

	err = xe_oa_submit_bb(stream, oa_bo->bb);

	/* Additional empirical delay needed for NOA programming after registers are written */
	usleep_range(us, 2 * us);
exit:
	return err;
}

static u32 oag_report_ctx_switches(const struct xe_oa_stream *stream)
{
	/* If user didn't require OA reports, ask HW not to emit ctx switch reports */
	return _MASKED_FIELD(OAG_OA_DEBUG_DISABLE_CTX_SWITCH_REPORTS,
			     stream->sample ?
			     0 : OAG_OA_DEBUG_DISABLE_CTX_SWITCH_REPORTS);
}

static int xe_oa_enable_metric_set(struct xe_oa_stream *stream)
{
	u32 oa_debug, sqcnt1;
	int ret;

	/*
	 * Wa_1508761755:xehpsdv, dg2
	 * EU NOA signals behave incorrectly if EU clock gating is enabled.
	 * Disable thread stall DOP gating and EU DOP gating.
	 */
	if (stream->oa->xe->info.platform == XE_DG2) {
		xe_gt_mcr_multicast_write(stream->gt, ROW_CHICKEN,
					  _MASKED_BIT_ENABLE(STALL_DOP_GATING_DISABLE));
		xe_gt_mcr_multicast_write(stream->gt, ROW_CHICKEN2,
					  _MASKED_BIT_ENABLE(DISABLE_DOP_GATING));
	}

	/* Disable clk ratio reports */
	oa_debug = OAG_OA_DEBUG_DISABLE_CLK_RATIO_REPORTS |
		OAG_OA_DEBUG_INCLUDE_CLK_RATIO;

	if (GRAPHICS_VER(stream->oa->xe) >= 20)
		oa_debug |=
			/* The three bits below are needed to get PEC counters running */
			OAG_OA_DEBUG_START_TRIGGER_SCOPE_CONTROL |
			OAG_OA_DEBUG_DISABLE_START_TRG_2_COUNT_QUAL |
			OAG_OA_DEBUG_DISABLE_START_TRG_1_COUNT_QUAL;

	xe_mmio_write32(stream->gt, __oa_regs(stream)->oa_debug,
			_MASKED_BIT_ENABLE(oa_debug) |
			oag_report_ctx_switches(stream) |
			oag_configure_mmio_trigger(stream, true));

	xe_mmio_write32(stream->gt, __oa_regs(stream)->oa_ctx_ctrl, stream->periodic ?
			(OAG_OAGLBCTXCTRL_COUNTER_RESUME |
			 OAG_OAGLBCTXCTRL_TIMER_ENABLE |
			 REG_FIELD_PREP(OAG_OAGLBCTXCTRL_TIMER_PERIOD_MASK,
					stream->period_exponent)) : 0);

	/*
	 * Initialize Super Queue Internal Cnt Register
	 * Set PMON Enable in order to collect valid metrics
	 * Enable bytes per clock reporting
	 */
	sqcnt1 = SQCNT1_PMON_ENABLE |
		 (HAS_OA_BPC_REPORTING(stream->oa->xe) ? SQCNT1_OABPC : 0);

	xe_mmio_rmw32(stream->gt, XELPMP_SQCNT1, 0, sqcnt1);

	/* Configure OAR/OAC */
	if (stream->exec_q) {
		ret = xe_oa_configure_oa_context(stream, true);
		if (ret)
			return ret;
	}

	return xe_oa_emit_oa_config(stream, stream->oa_config);
}

static void xe_oa_stream_enable(struct xe_oa_stream *stream)
{
	stream->pollin = false;

	xe_oa_enable(stream);

	if (stream->sample)
		hrtimer_start(&stream->poll_check_timer,
			      ns_to_ktime(stream->poll_period_ns),
			      HRTIMER_MODE_REL_PINNED);
}

static void xe_oa_stream_disable(struct xe_oa_stream *stream)
{
	xe_oa_disable(stream);

	if (stream->sample)
		hrtimer_cancel(&stream->poll_check_timer);
}

static int xe_oa_enable_preempt_timeslice(struct xe_oa_stream *stream)
{
	struct xe_exec_queue *q = stream->exec_q;
	int ret1, ret2;

	/* Best effort recovery: try to revert both to original, irrespective of error */
	ret1 = q->ops->set_timeslice(q, stream->hwe->eclass->sched_props.timeslice_us);
	ret2 = q->ops->set_preempt_timeout(q, stream->hwe->eclass->sched_props.preempt_timeout_us);
	if (ret1 || ret2)
		goto err;
	return 0;
err:
	drm_dbg(&stream->oa->xe->drm, "%s failed ret1 %d ret2 %d\n", __func__, ret1, ret2);
	return ret1 ?: ret2;
}

static int xe_oa_disable_preempt_timeslice(struct xe_oa_stream *stream)
{
	struct xe_exec_queue *q = stream->exec_q;
	int ret;

	/* Setting values to 0 will disable timeslice and preempt_timeout */
	ret = q->ops->set_timeslice(q, 0);
	if (ret)
		goto err;

	ret = q->ops->set_preempt_timeout(q, 0);
	if (ret)
		goto err;

	return 0;
err:
	xe_oa_enable_preempt_timeslice(stream);
	drm_dbg(&stream->oa->xe->drm, "%s failed %d\n", __func__, ret);
	return ret;
}

static int xe_oa_enable_locked(struct xe_oa_stream *stream)
{
	if (stream->enabled)
		return 0;

	if (stream->no_preempt) {
		int ret = xe_oa_disable_preempt_timeslice(stream);

		if (ret)
			return ret;
	}

	xe_oa_stream_enable(stream);

	stream->enabled = true;
	return 0;
}

static int xe_oa_disable_locked(struct xe_oa_stream *stream)
{
	int ret = 0;

	if (!stream->enabled)
		return 0;

	xe_oa_stream_disable(stream);

	if (stream->no_preempt)
		ret = xe_oa_enable_preempt_timeslice(stream);

	stream->enabled = false;
	return ret;
}

static long xe_oa_config_locked(struct xe_oa_stream *stream, u64 arg)
{
	struct drm_xe_ext_set_property ext;
	long ret = stream->oa_config->id;
	struct xe_oa_config *config;
	int err;

	err = __copy_from_user(&ext, u64_to_user_ptr(arg), sizeof(ext));
	if (XE_IOCTL_DBG(stream->oa->xe, err))
		return -EFAULT;

	if (XE_IOCTL_DBG(stream->oa->xe, ext.pad) ||
	    XE_IOCTL_DBG(stream->oa->xe, ext.base.name != DRM_XE_OA_EXTENSION_SET_PROPERTY) ||
	    XE_IOCTL_DBG(stream->oa->xe, ext.base.next_extension) ||
	    XE_IOCTL_DBG(stream->oa->xe, ext.property != DRM_XE_OA_PROPERTY_OA_METRIC_SET))
		return -EINVAL;

	config = xe_oa_get_oa_config(stream->oa, ext.value);
	if (!config)
		return -ENODEV;

	if (config != stream->oa_config) {
		err = xe_oa_emit_oa_config(stream, config);
		if (!err)
			config = xchg(&stream->oa_config, config);
		else
			ret = err;
	}

	xe_oa_config_put(config);

	return ret;
}

static long xe_oa_status_locked(struct xe_oa_stream *stream, unsigned long arg)
{
	struct drm_xe_oa_stream_status status = {};
	void __user *uaddr = (void __user *)arg;

	/* Map from register to uapi bits */
	if (stream->oa_status & OASTATUS_REPORT_LOST)
		status.oa_status |= DRM_XE_OASTATUS_REPORT_LOST;
	if (stream->oa_status & OASTATUS_BUFFER_OVERFLOW)
		status.oa_status |= DRM_XE_OASTATUS_BUFFER_OVERFLOW;
	if (stream->oa_status & OASTATUS_COUNTER_OVERFLOW)
		status.oa_status |= DRM_XE_OASTATUS_COUNTER_OVERFLOW;
	if (stream->oa_status & OASTATUS_MMIO_TRG_Q_FULL)
		status.oa_status |= DRM_XE_OASTATUS_MMIO_TRG_Q_FULL;

	if (copy_to_user(uaddr, &status, sizeof(status)))
		return -EFAULT;

	return 0;
}

static long xe_oa_info_locked(struct xe_oa_stream *stream, unsigned long arg)
{
	struct drm_xe_oa_stream_info info = { .oa_buf_size = XE_OA_BUFFER_SIZE, };
	void __user *uaddr = (void __user *)arg;

	if (copy_to_user(uaddr, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

static long xe_oa_ioctl_locked(struct xe_oa_stream *stream,
			       unsigned int cmd,
			       unsigned long arg)
{
	switch (cmd) {
	case DRM_XE_PERF_IOCTL_ENABLE:
		return xe_oa_enable_locked(stream);
	case DRM_XE_PERF_IOCTL_DISABLE:
		return xe_oa_disable_locked(stream);
	case DRM_XE_PERF_IOCTL_CONFIG:
		return xe_oa_config_locked(stream, arg);
	case DRM_XE_PERF_IOCTL_STATUS:
		return xe_oa_status_locked(stream, arg);
	case DRM_XE_PERF_IOCTL_INFO:
		return xe_oa_info_locked(stream, arg);
	}

	return -EINVAL;
}

static long xe_oa_ioctl(struct file *file,
			unsigned int cmd,
			unsigned long arg)
{
	struct xe_oa_stream *stream = file->private_data;
	long ret;

	mutex_lock(&stream->stream_lock);
	ret = xe_oa_ioctl_locked(stream, cmd, arg);
	mutex_unlock(&stream->stream_lock);

	return ret;
}

static void xe_oa_destroy_locked(struct xe_oa_stream *stream)
{
	if (stream->enabled)
		xe_oa_disable_locked(stream);

	xe_oa_stream_destroy(stream);

	if (stream->exec_q)
		xe_exec_queue_put(stream->exec_q);

	kfree(stream);
}

static int xe_oa_release(struct inode *inode, struct file *file)
{
	struct xe_oa_stream *stream = file->private_data;
	struct xe_gt *gt = stream->gt;

	mutex_lock(&gt->oa.gt_lock);
	xe_oa_destroy_locked(stream);
	mutex_unlock(&gt->oa.gt_lock);

	/* Release the reference the perf stream kept on the driver */
	drm_dev_put(&gt_to_xe(gt)->drm);

	return 0;
}

static int xe_oa_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct xe_oa_stream *stream = file->private_data;
	struct xe_bo *bo = stream->oa_buffer.bo;
	unsigned long start = vma->vm_start;
	int i, ret;

	if (xe_perf_stream_paranoid && !perfmon_capable()) {
		drm_dbg(&stream->oa->xe->drm, "Insufficient privilege to map OA buffer\n");
		return -EACCES;
	}

	/* Can mmap the entire OA buffer or nothing (no partial OA buffer mmaps) */
	if (vma->vm_end - vma->vm_start != XE_OA_BUFFER_SIZE) {
		drm_dbg(&stream->oa->xe->drm, "Wrong mmap size, must be OA buffer size\n");
		return -EINVAL;
	}

	/*
	 * Only support VM_READ, enforce MAP_PRIVATE by checking for
	 * VM_MAYSHARE, don't copy the vma on fork
	 */
	if (vma->vm_flags & (VM_WRITE | VM_EXEC | VM_SHARED | VM_MAYSHARE)) {
		drm_dbg(&stream->oa->xe->drm, "mmap must be read only\n");
		return -EINVAL;
	}
	vm_flags_mod(vma, VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP | VM_DONTCOPY,
		     VM_MAYWRITE | VM_MAYEXEC);

	xe_assert(stream->oa->xe, bo->ttm.ttm->num_pages ==
		  (vma->vm_end - vma->vm_start) >> PAGE_SHIFT);
	for (i = 0; i < bo->ttm.ttm->num_pages; i++) {
		ret = remap_pfn_range(vma, start, page_to_pfn(bo->ttm.ttm->pages[i]),
				      PAGE_SIZE, vma->vm_page_prot);
		if (ret)
			break;

		start += PAGE_SIZE;
	}

	return ret;
}

static const struct file_operations xe_oa_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.release	= xe_oa_release,
	.poll		= xe_oa_poll,
	.read		= xe_oa_read,
	.unlocked_ioctl	= xe_oa_ioctl,
	.mmap		= xe_oa_mmap,
};

static bool engine_supports_mi_query(struct xe_hw_engine *hwe)
{
	return hwe->class == XE_ENGINE_CLASS_RENDER ||
		hwe->class == XE_ENGINE_CLASS_COMPUTE;
}

static bool xe_oa_find_reg_in_lri(u32 *state, u32 reg, u32 *offset, u32 end)
{
	u32 idx = *offset;
	u32 len = min(MI_LRI_LEN(state[idx]) + idx, end);
	bool found = false;

	idx++;
	for (; idx < len; idx += 2) {
		if (state[idx] == reg) {
			found = true;
			break;
		}
	}

	*offset = idx;
	return found;
}

#define IS_MI_LRI_CMD(x) (REG_FIELD_GET(MI_OPCODE, (x)) == \
			  REG_FIELD_GET(MI_OPCODE, MI_LOAD_REGISTER_IMM))

static u32 xe_oa_context_image_offset(struct xe_oa_stream *stream, u32 reg)
{
	struct xe_lrc *lrc = stream->exec_q->lrc[0];
	u32 len = (xe_gt_lrc_size(stream->gt, stream->hwe->class) +
		   lrc->ring.size) / sizeof(u32);
	u32 offset = xe_lrc_regs_offset(lrc) / sizeof(u32);
	u32 *state = (u32 *)lrc->bo->vmap.vaddr;

	if (drm_WARN_ON(&stream->oa->xe->drm, !state))
		return U32_MAX;

	for (; offset < len; ) {
		if (IS_MI_LRI_CMD(state[offset])) {
			/*
			 * We expect reg-value pairs in MI_LRI command, so
			 * MI_LRI_LEN() should be even
			 */
			drm_WARN_ON(&stream->oa->xe->drm,
				    MI_LRI_LEN(state[offset]) & 0x1);

			if (xe_oa_find_reg_in_lri(state, reg, &offset, len))
				break;
		} else {
			offset++;
		}
	}

	return offset < len ? offset : U32_MAX;
}

static int xe_oa_set_ctx_ctrl_offset(struct xe_oa_stream *stream)
{
	struct xe_reg reg = OACTXCONTROL(stream->hwe->mmio_base);
	u32 offset = stream->oa->ctx_oactxctrl_offset[stream->hwe->class];

	/* Do this only once. Failure is stored as offset of U32_MAX */
	if (offset)
		goto exit;

	offset = xe_oa_context_image_offset(stream, reg.addr);
	stream->oa->ctx_oactxctrl_offset[stream->hwe->class] = offset;

	drm_dbg(&stream->oa->xe->drm, "%s oa ctx control at 0x%08x dword offset\n",
		stream->hwe->name, offset);
exit:
	return offset && offset != U32_MAX ? 0 : -ENODEV;
}

static int xe_oa_stream_init(struct xe_oa_stream *stream,
			     struct xe_oa_open_param *param)
{
	struct xe_oa_unit *u = param->hwe->oa_unit;
	struct xe_gt *gt = param->hwe->gt;
	int ret;

	stream->exec_q = param->exec_q;
	stream->poll_period_ns = DEFAULT_POLL_PERIOD_NS;
	stream->hwe = param->hwe;
	stream->gt = stream->hwe->gt;
	stream->oa_buffer.format = &stream->oa->oa_formats[param->oa_format];

	stream->sample = param->sample;
	stream->periodic = param->period_exponent > 0;
	stream->period_exponent = param->period_exponent;
	stream->no_preempt = param->no_preempt;

	/*
	 * For Xe2+, when overrun mode is enabled, there are no partial reports at the end
	 * of buffer, making the OA buffer effectively a non-power-of-2 size circular
	 * buffer whose size, circ_size, is a multiple of the report size
	 */
	if (GRAPHICS_VER(stream->oa->xe) >= 20 &&
	    stream->hwe->oa_unit->type == DRM_XE_OA_UNIT_TYPE_OAG && stream->sample)
		stream->oa_buffer.circ_size =
			XE_OA_BUFFER_SIZE - XE_OA_BUFFER_SIZE % stream->oa_buffer.format->size;
	else
		stream->oa_buffer.circ_size = XE_OA_BUFFER_SIZE;

	if (stream->exec_q && engine_supports_mi_query(stream->hwe)) {
		/* If we don't find the context offset, just return error */
		ret = xe_oa_set_ctx_ctrl_offset(stream);
		if (ret) {
			drm_err(&stream->oa->xe->drm,
				"xe_oa_set_ctx_ctrl_offset failed for %s\n",
				stream->hwe->name);
			goto exit;
		}
	}

	stream->oa_config = xe_oa_get_oa_config(stream->oa, param->metric_set);
	if (!stream->oa_config) {
		drm_dbg(&stream->oa->xe->drm, "Invalid OA config id=%i\n", param->metric_set);
		ret = -EINVAL;
		goto exit;
	}

	/*
	 * Wa_1509372804:pvc
	 *
	 * GuC reset of engines causes OA to lose configuration
	 * state. Prevent this by overriding GUCRC mode.
	 */
	if (stream->oa->xe->info.platform == XE_PVC) {
		ret = xe_guc_pc_override_gucrc_mode(&gt->uc.guc.pc,
						    SLPC_GUCRC_MODE_GUCRC_NO_RC6);
		if (ret)
			goto err_free_configs;

		stream->override_gucrc = true;
	}

	/* Take runtime pm ref and forcewake to disable RC6 */
	xe_pm_runtime_get(stream->oa->xe);
	XE_WARN_ON(xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL));

	ret = xe_oa_alloc_oa_buffer(stream);
	if (ret)
		goto err_fw_put;

	stream->k_exec_q = xe_exec_queue_create(stream->oa->xe, NULL,
						BIT(stream->hwe->logical_instance), 1,
						stream->hwe, EXEC_QUEUE_FLAG_KERNEL, 0);
	if (IS_ERR(stream->k_exec_q)) {
		ret = PTR_ERR(stream->k_exec_q);
		drm_err(&stream->oa->xe->drm, "gt%d, hwe %s, xe_exec_queue_create failed=%d",
			stream->gt->info.id, stream->hwe->name, ret);
		goto err_free_oa_buf;
	}

	ret = xe_oa_enable_metric_set(stream);
	if (ret) {
		drm_dbg(&stream->oa->xe->drm, "Unable to enable metric set\n");
		goto err_put_k_exec_q;
	}

	drm_dbg(&stream->oa->xe->drm, "opening stream oa config uuid=%s\n",
		stream->oa_config->uuid);

	WRITE_ONCE(u->exclusive_stream, stream);

	hrtimer_init(&stream->poll_check_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	stream->poll_check_timer.function = xe_oa_poll_check_timer_cb;
	init_waitqueue_head(&stream->poll_wq);

	spin_lock_init(&stream->oa_buffer.ptr_lock);
	mutex_init(&stream->stream_lock);

	return 0;

err_put_k_exec_q:
	xe_oa_disable_metric_set(stream);
	xe_exec_queue_put(stream->k_exec_q);
err_free_oa_buf:
	xe_oa_free_oa_buffer(stream);
err_fw_put:
	XE_WARN_ON(xe_force_wake_put(gt_to_fw(gt), XE_FORCEWAKE_ALL));
	xe_pm_runtime_put(stream->oa->xe);
	if (stream->override_gucrc)
		xe_gt_WARN_ON(gt, xe_guc_pc_unset_gucrc_mode(&gt->uc.guc.pc));
err_free_configs:
	xe_oa_free_configs(stream);
exit:
	return ret;
}

static int xe_oa_stream_open_ioctl_locked(struct xe_oa *oa,
					  struct xe_oa_open_param *param)
{
	struct xe_oa_stream *stream;
	int stream_fd;
	int ret;

	/* We currently only allow exclusive access */
	if (param->hwe->oa_unit->exclusive_stream) {
		drm_dbg(&oa->xe->drm, "OA unit already in use\n");
		ret = -EBUSY;
		goto exit;
	}

	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (!stream) {
		ret = -ENOMEM;
		goto exit;
	}

	stream->oa = oa;
	ret = xe_oa_stream_init(stream, param);
	if (ret)
		goto err_free;

	if (!param->disabled) {
		ret = xe_oa_enable_locked(stream);
		if (ret)
			goto err_destroy;
	}

	stream_fd = anon_inode_getfd("[xe_oa]", &xe_oa_fops, stream, 0);
	if (stream_fd < 0) {
		ret = stream_fd;
		goto err_disable;
	}

	/* Hold a reference on the drm device till stream_fd is released */
	drm_dev_get(&stream->oa->xe->drm);

	return stream_fd;
err_disable:
	if (!param->disabled)
		xe_oa_disable_locked(stream);
err_destroy:
	xe_oa_stream_destroy(stream);
err_free:
	kfree(stream);
exit:
	return ret;
}

/**
 * xe_oa_timestamp_frequency - Return OA timestamp frequency
 * @gt: @xe_gt
 *
 * OA timestamp frequency = CS timestamp frequency in most platforms. On some
 * platforms OA unit ignores the CTC_SHIFT and the 2 timestamps differ. In such
 * cases, return the adjusted CS timestamp frequency to the user.
 */
u32 xe_oa_timestamp_frequency(struct xe_gt *gt)
{
	u32 reg, shift;

	/*
	 * Wa_18013179988:dg2
	 * Wa_14015568240:pvc
	 * Wa_14015846243:mtl
	 */
	switch (gt_to_xe(gt)->info.platform) {
	case XE_DG2:
	case XE_PVC:
	case XE_METEORLAKE:
		xe_pm_runtime_get(gt_to_xe(gt));
		reg = xe_mmio_read32(gt, RPM_CONFIG0);
		xe_pm_runtime_put(gt_to_xe(gt));

		shift = REG_FIELD_GET(RPM_CONFIG0_CTC_SHIFT_PARAMETER_MASK, reg);
		return gt->info.reference_clock << (3 - shift);

	default:
		return gt->info.reference_clock;
	}
}

static u64 oa_exponent_to_ns(struct xe_gt *gt, int exponent)
{
	u64 nom = (2ULL << exponent) * NSEC_PER_SEC;
	u32 den = xe_oa_timestamp_frequency(gt);

	return div_u64(nom + den - 1, den);
}

static bool engine_supports_oa_format(const struct xe_hw_engine *hwe, int type)
{
	switch (hwe->oa_unit->type) {
	case DRM_XE_OA_UNIT_TYPE_OAG:
		return type == DRM_XE_OA_FMT_TYPE_OAG || type == DRM_XE_OA_FMT_TYPE_OAR ||
			type == DRM_XE_OA_FMT_TYPE_OAC || type == DRM_XE_OA_FMT_TYPE_PEC;
	case DRM_XE_OA_UNIT_TYPE_OAM:
		return type == DRM_XE_OA_FMT_TYPE_OAM || type == DRM_XE_OA_FMT_TYPE_OAM_MPEC;
	default:
		return false;
	}
}

static int decode_oa_format(struct xe_oa *oa, u64 fmt, enum xe_oa_format_name *name)
{
	u32 counter_size = FIELD_GET(DRM_XE_OA_FORMAT_MASK_COUNTER_SIZE, fmt);
	u32 counter_sel = FIELD_GET(DRM_XE_OA_FORMAT_MASK_COUNTER_SEL, fmt);
	u32 bc_report = FIELD_GET(DRM_XE_OA_FORMAT_MASK_BC_REPORT, fmt);
	u32 type = FIELD_GET(DRM_XE_OA_FORMAT_MASK_FMT_TYPE, fmt);
	int idx;

	for_each_set_bit(idx, oa->format_mask, __XE_OA_FORMAT_MAX) {
		const struct xe_oa_format *f = &oa->oa_formats[idx];

		if (counter_size == f->counter_size && bc_report == f->bc_report &&
		    type == f->type && counter_sel == f->counter_select) {
			*name = idx;
			return 0;
		}
	}

	return -EINVAL;
}

/**
 * xe_oa_unit_id - Return OA unit ID for a hardware engine
 * @hwe: @xe_hw_engine
 *
 * Return OA unit ID for a hardware engine when available
 */
u16 xe_oa_unit_id(struct xe_hw_engine *hwe)
{
	return hwe->oa_unit && hwe->oa_unit->num_engines ?
		hwe->oa_unit->oa_unit_id : U16_MAX;
}

static int xe_oa_assign_hwe(struct xe_oa *oa, struct xe_oa_open_param *param)
{
	struct xe_gt *gt;
	int i, ret = 0;

	if (param->exec_q) {
		/* When we have an exec_q, get hwe from the exec_q */
		param->hwe = xe_gt_hw_engine(param->exec_q->gt, param->exec_q->class,
					     param->engine_instance, true);
	} else {
		struct xe_hw_engine *hwe;
		enum xe_hw_engine_id id;

		/* Else just get the first hwe attached to the oa unit */
		for_each_gt(gt, oa->xe, i) {
			for_each_hw_engine(hwe, gt, id) {
				if (xe_oa_unit_id(hwe) == param->oa_unit_id) {
					param->hwe = hwe;
					goto out;
				}
			}
		}
	}
out:
	if (!param->hwe || xe_oa_unit_id(param->hwe) != param->oa_unit_id) {
		drm_dbg(&oa->xe->drm, "Unable to find hwe (%d, %d) for OA unit ID %d\n",
			param->exec_q ? param->exec_q->class : -1,
			param->engine_instance, param->oa_unit_id);
		ret = -EINVAL;
	}

	return ret;
}

static int xe_oa_set_prop_oa_unit_id(struct xe_oa *oa, u64 value,
				     struct xe_oa_open_param *param)
{
	if (value >= oa->oa_unit_ids) {
		drm_dbg(&oa->xe->drm, "OA unit ID out of range %lld\n", value);
		return -EINVAL;
	}
	param->oa_unit_id = value;
	return 0;
}

static int xe_oa_set_prop_sample_oa(struct xe_oa *oa, u64 value,
				    struct xe_oa_open_param *param)
{
	param->sample = value;
	return 0;
}

static int xe_oa_set_prop_metric_set(struct xe_oa *oa, u64 value,
				     struct xe_oa_open_param *param)
{
	param->metric_set = value;
	return 0;
}

static int xe_oa_set_prop_oa_format(struct xe_oa *oa, u64 value,
				    struct xe_oa_open_param *param)
{
	int ret = decode_oa_format(oa, value, &param->oa_format);

	if (ret) {
		drm_dbg(&oa->xe->drm, "Unsupported OA report format %#llx\n", value);
		return ret;
	}
	return 0;
}

static int xe_oa_set_prop_oa_exponent(struct xe_oa *oa, u64 value,
				      struct xe_oa_open_param *param)
{
#define OA_EXPONENT_MAX 31

	if (value > OA_EXPONENT_MAX) {
		drm_dbg(&oa->xe->drm, "OA timer exponent too high (> %u)\n", OA_EXPONENT_MAX);
		return -EINVAL;
	}
	param->period_exponent = value;
	return 0;
}

static int xe_oa_set_prop_disabled(struct xe_oa *oa, u64 value,
				   struct xe_oa_open_param *param)
{
	param->disabled = value;
	return 0;
}

static int xe_oa_set_prop_exec_queue_id(struct xe_oa *oa, u64 value,
					struct xe_oa_open_param *param)
{
	param->exec_queue_id = value;
	return 0;
}

static int xe_oa_set_prop_engine_instance(struct xe_oa *oa, u64 value,
					  struct xe_oa_open_param *param)
{
	param->engine_instance = value;
	return 0;
}

static int xe_oa_set_no_preempt(struct xe_oa *oa, u64 value,
				struct xe_oa_open_param *param)
{
	param->no_preempt = value;
	return 0;
}

typedef int (*xe_oa_set_property_fn)(struct xe_oa *oa, u64 value,
				     struct xe_oa_open_param *param);
static const xe_oa_set_property_fn xe_oa_set_property_funcs[] = {
	[DRM_XE_OA_PROPERTY_OA_UNIT_ID] = xe_oa_set_prop_oa_unit_id,
	[DRM_XE_OA_PROPERTY_SAMPLE_OA] = xe_oa_set_prop_sample_oa,
	[DRM_XE_OA_PROPERTY_OA_METRIC_SET] = xe_oa_set_prop_metric_set,
	[DRM_XE_OA_PROPERTY_OA_FORMAT] = xe_oa_set_prop_oa_format,
	[DRM_XE_OA_PROPERTY_OA_PERIOD_EXPONENT] = xe_oa_set_prop_oa_exponent,
	[DRM_XE_OA_PROPERTY_OA_DISABLED] = xe_oa_set_prop_disabled,
	[DRM_XE_OA_PROPERTY_EXEC_QUEUE_ID] = xe_oa_set_prop_exec_queue_id,
	[DRM_XE_OA_PROPERTY_OA_ENGINE_INSTANCE] = xe_oa_set_prop_engine_instance,
	[DRM_XE_OA_PROPERTY_NO_PREEMPT] = xe_oa_set_no_preempt,
};

static int xe_oa_user_ext_set_property(struct xe_oa *oa, u64 extension,
				       struct xe_oa_open_param *param)
{
	u64 __user *address = u64_to_user_ptr(extension);
	struct drm_xe_ext_set_property ext;
	int err;
	u32 idx;

	err = __copy_from_user(&ext, address, sizeof(ext));
	if (XE_IOCTL_DBG(oa->xe, err))
		return -EFAULT;

	if (XE_IOCTL_DBG(oa->xe, ext.property >= ARRAY_SIZE(xe_oa_set_property_funcs)) ||
	    XE_IOCTL_DBG(oa->xe, ext.pad))
		return -EINVAL;

	idx = array_index_nospec(ext.property, ARRAY_SIZE(xe_oa_set_property_funcs));
	return xe_oa_set_property_funcs[idx](oa, ext.value, param);
}

typedef int (*xe_oa_user_extension_fn)(struct xe_oa *oa, u64 extension,
				       struct xe_oa_open_param *param);
static const xe_oa_user_extension_fn xe_oa_user_extension_funcs[] = {
	[DRM_XE_OA_EXTENSION_SET_PROPERTY] = xe_oa_user_ext_set_property,
};

#define MAX_USER_EXTENSIONS	16
static int xe_oa_user_extensions(struct xe_oa *oa, u64 extension, int ext_number,
				 struct xe_oa_open_param *param)
{
	u64 __user *address = u64_to_user_ptr(extension);
	struct drm_xe_user_extension ext;
	int err;
	u32 idx;

	if (XE_IOCTL_DBG(oa->xe, ext_number >= MAX_USER_EXTENSIONS))
		return -E2BIG;

	err = __copy_from_user(&ext, address, sizeof(ext));
	if (XE_IOCTL_DBG(oa->xe, err))
		return -EFAULT;

	if (XE_IOCTL_DBG(oa->xe, ext.pad) ||
	    XE_IOCTL_DBG(oa->xe, ext.name >= ARRAY_SIZE(xe_oa_user_extension_funcs)))
		return -EINVAL;

	idx = array_index_nospec(ext.name, ARRAY_SIZE(xe_oa_user_extension_funcs));
	err = xe_oa_user_extension_funcs[idx](oa, extension, param);
	if (XE_IOCTL_DBG(oa->xe, err))
		return err;

	if (ext.next_extension)
		return xe_oa_user_extensions(oa, ext.next_extension, ++ext_number, param);

	return 0;
}

/**
 * xe_oa_stream_open_ioctl - Opens an OA stream
 * @dev: @drm_device
 * @data: pointer to struct @drm_xe_oa_config
 * @file: @drm_file
 *
 * The functions opens an OA stream. An OA stream, opened with specified
 * properties, enables perf counter samples to be collected, either
 * periodically (time based sampling), or on request (using perf queries)
 */
int xe_oa_stream_open_ioctl(struct drm_device *dev, u64 data, struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_oa *oa = &xe->oa;
	struct xe_file *xef = to_xe_file(file);
	struct xe_oa_open_param param = {};
	const struct xe_oa_format *f;
	bool privileged_op = true;
	int ret;

	if (!oa->xe) {
		drm_dbg(&xe->drm, "xe oa interface not available for this system\n");
		return -ENODEV;
	}

	ret = xe_oa_user_extensions(oa, data, 0, &param);
	if (ret)
		return ret;

	if (param.exec_queue_id > 0) {
		param.exec_q = xe_exec_queue_lookup(xef, param.exec_queue_id);
		if (XE_IOCTL_DBG(oa->xe, !param.exec_q))
			return -ENOENT;

		if (param.exec_q->width > 1)
			drm_dbg(&oa->xe->drm, "exec_q->width > 1, programming only exec_q->lrc[0]\n");
	}

	/*
	 * Query based sampling (using MI_REPORT_PERF_COUNT) with OAR/OAC,
	 * without global stream access, can be an unprivileged operation
	 */
	if (param.exec_q && !param.sample)
		privileged_op = false;

	if (param.no_preempt) {
		if (!param.exec_q) {
			drm_dbg(&oa->xe->drm, "Preemption disable without exec_q!\n");
			ret = -EINVAL;
			goto err_exec_q;
		}
		privileged_op = true;
	}

	if (privileged_op && xe_perf_stream_paranoid && !perfmon_capable()) {
		drm_dbg(&oa->xe->drm, "Insufficient privileges to open xe perf stream\n");
		ret = -EACCES;
		goto err_exec_q;
	}

	if (!param.exec_q && !param.sample) {
		drm_dbg(&oa->xe->drm, "Only OA report sampling supported\n");
		ret = -EINVAL;
		goto err_exec_q;
	}

	ret = xe_oa_assign_hwe(oa, &param);
	if (ret)
		goto err_exec_q;

	f = &oa->oa_formats[param.oa_format];
	if (!param.oa_format || !f->size ||
	    !engine_supports_oa_format(param.hwe, f->type)) {
		drm_dbg(&oa->xe->drm, "Invalid OA format %d type %d size %d for class %d\n",
			param.oa_format, f->type, f->size, param.hwe->class);
		ret = -EINVAL;
		goto err_exec_q;
	}

	if (param.period_exponent > 0) {
		u64 oa_period, oa_freq_hz;

		/* Requesting samples from OAG buffer is a privileged operation */
		if (!param.sample) {
			drm_dbg(&oa->xe->drm, "OA_EXPONENT specified without SAMPLE_OA\n");
			ret = -EINVAL;
			goto err_exec_q;
		}
		oa_period = oa_exponent_to_ns(param.hwe->gt, param.period_exponent);
		oa_freq_hz = div64_u64(NSEC_PER_SEC, oa_period);
		drm_dbg(&oa->xe->drm, "Using periodic sampling freq %lld Hz\n", oa_freq_hz);
	}

	mutex_lock(&param.hwe->gt->oa.gt_lock);
	ret = xe_oa_stream_open_ioctl_locked(oa, &param);
	mutex_unlock(&param.hwe->gt->oa.gt_lock);
err_exec_q:
	if (ret < 0 && param.exec_q)
		xe_exec_queue_put(param.exec_q);
	return ret;
}

static bool xe_oa_is_valid_flex_addr(struct xe_oa *oa, u32 addr)
{
	static const struct xe_reg flex_eu_regs[] = {
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
		if (flex_eu_regs[i].addr == addr)
			return true;
	}
	return false;
}

static bool xe_oa_reg_in_range_table(u32 addr, const struct xe_mmio_range *table)
{
	while (table->start && table->end) {
		if (addr >= table->start && addr <= table->end)
			return true;

		table++;
	}

	return false;
}

static const struct xe_mmio_range xehp_oa_b_counters[] = {
	{ .start = 0xdc48, .end = 0xdc48 },	/* OAA_ENABLE_REG */
	{ .start = 0xdd00, .end = 0xdd48 },	/* OAG_LCE0_0 - OAA_LENABLE_REG */
	{}
};

static const struct xe_mmio_range gen12_oa_b_counters[] = {
	{ .start = 0x2b2c, .end = 0x2b2c },	/* OAG_OA_PESS */
	{ .start = 0xd900, .end = 0xd91c },	/* OAG_OASTARTTRIG[1-8] */
	{ .start = 0xd920, .end = 0xd93c },	/* OAG_OAREPORTTRIG1[1-8] */
	{ .start = 0xd940, .end = 0xd97c },	/* OAG_CEC[0-7][0-1] */
	{ .start = 0xdc00, .end = 0xdc3c },	/* OAG_SCEC[0-7][0-1] */
	{ .start = 0xdc40, .end = 0xdc40 },	/* OAG_SPCTR_CNF */
	{ .start = 0xdc44, .end = 0xdc44 },	/* OAA_DBG_REG */
	{}
};

static const struct xe_mmio_range mtl_oam_b_counters[] = {
	{ .start = 0x393000, .end = 0x39301c },	/* OAM_STARTTRIG1[1-8] */
	{ .start = 0x393020, .end = 0x39303c },	/* OAM_REPORTTRIG1[1-8] */
	{ .start = 0x393040, .end = 0x39307c },	/* OAM_CEC[0-7][0-1] */
	{ .start = 0x393200, .end = 0x39323C },	/* MPES[0-7] */
	{}
};

static const struct xe_mmio_range xe2_oa_b_counters[] = {
	{ .start = 0x393200, .end = 0x39323C },	/* MPES_0_MPES_SAG - MPES_7_UPPER_MPES_SAG */
	{ .start = 0x394200, .end = 0x39423C },	/* MPES_0_MPES_SCMI0 - MPES_7_UPPER_MPES_SCMI0 */
	{ .start = 0x394A00, .end = 0x394A3C },	/* MPES_0_MPES_SCMI1 - MPES_7_UPPER_MPES_SCMI1 */
	{},
};

static bool xe_oa_is_valid_b_counter_addr(struct xe_oa *oa, u32 addr)
{
	return xe_oa_reg_in_range_table(addr, xehp_oa_b_counters) ||
		xe_oa_reg_in_range_table(addr, gen12_oa_b_counters) ||
		xe_oa_reg_in_range_table(addr, mtl_oam_b_counters) ||
		(GRAPHICS_VER(oa->xe) >= 20 &&
		 xe_oa_reg_in_range_table(addr, xe2_oa_b_counters));
}

static const struct xe_mmio_range mtl_oa_mux_regs[] = {
	{ .start = 0x0d00, .end = 0x0d04 },	/* RPM_CONFIG[0-1] */
	{ .start = 0x0d0c, .end = 0x0d2c },	/* NOA_CONFIG[0-8] */
	{ .start = 0x9840, .end = 0x9840 },	/* GDT_CHICKEN_BITS */
	{ .start = 0x9884, .end = 0x9888 },	/* NOA_WRITE */
	{ .start = 0x38d100, .end = 0x38d114},	/* VISACTL */
	{}
};

static const struct xe_mmio_range gen12_oa_mux_regs[] = {
	{ .start = 0x0d00, .end = 0x0d04 },     /* RPM_CONFIG[0-1] */
	{ .start = 0x0d0c, .end = 0x0d2c },     /* NOA_CONFIG[0-8] */
	{ .start = 0x9840, .end = 0x9840 },	/* GDT_CHICKEN_BITS */
	{ .start = 0x9884, .end = 0x9888 },	/* NOA_WRITE */
	{ .start = 0x20cc, .end = 0x20cc },	/* WAIT_FOR_RC6_EXIT */
	{}
};

static const struct xe_mmio_range xe2_oa_mux_regs[] = {
	{ .start = 0x5194, .end = 0x5194 },	/* SYS_MEM_LAT_MEASURE_MERTF_GRP_3D */
	{ .start = 0x8704, .end = 0x8704 },	/* LMEM_LAT_MEASURE_MCFG_GRP */
	{ .start = 0xB1BC, .end = 0xB1BC },	/* L3_BANK_LAT_MEASURE_LBCF_GFX */
	{ .start = 0xE18C, .end = 0xE18C },	/* SAMPLER_MODE */
	{ .start = 0xE590, .end = 0xE590 },	/* TDL_LSC_LAT_MEASURE_TDL_GFX */
	{ .start = 0x13000, .end = 0x137FC },	/* PES_0_PESL0 - PES_63_UPPER_PESL3 */
	{},
};

static bool xe_oa_is_valid_mux_addr(struct xe_oa *oa, u32 addr)
{
	if (GRAPHICS_VER(oa->xe) >= 20)
		return xe_oa_reg_in_range_table(addr, xe2_oa_mux_regs);
	else if (GRAPHICS_VERx100(oa->xe) >= 1270)
		return xe_oa_reg_in_range_table(addr, mtl_oa_mux_regs);
	else
		return xe_oa_reg_in_range_table(addr, gen12_oa_mux_regs);
}

static bool xe_oa_is_valid_config_reg_addr(struct xe_oa *oa, u32 addr)
{
	return xe_oa_is_valid_flex_addr(oa, addr) ||
		xe_oa_is_valid_b_counter_addr(oa, addr) ||
		xe_oa_is_valid_mux_addr(oa, addr);
}

static struct xe_oa_reg *
xe_oa_alloc_regs(struct xe_oa *oa, bool (*is_valid)(struct xe_oa *oa, u32 addr),
		 u32 __user *regs, u32 n_regs)
{
	struct xe_oa_reg *oa_regs;
	int err;
	u32 i;

	oa_regs = kmalloc_array(n_regs, sizeof(*oa_regs), GFP_KERNEL);
	if (!oa_regs)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < n_regs; i++) {
		u32 addr, value;

		err = get_user(addr, regs);
		if (err)
			goto addr_err;

		if (!is_valid(oa, addr)) {
			drm_dbg(&oa->xe->drm, "Invalid oa_reg address: %X\n", addr);
			err = -EINVAL;
			goto addr_err;
		}

		err = get_user(value, regs + 1);
		if (err)
			goto addr_err;

		oa_regs[i].addr = XE_REG(addr);
		oa_regs[i].value = value;

		regs += 2;
	}

	return oa_regs;

addr_err:
	kfree(oa_regs);
	return ERR_PTR(err);
}

static ssize_t show_dynamic_id(struct kobject *kobj,
			       struct kobj_attribute *attr,
			       char *buf)
{
	struct xe_oa_config *oa_config =
		container_of(attr, typeof(*oa_config), sysfs_metric_id);

	return sysfs_emit(buf, "%d\n", oa_config->id);
}

static int create_dynamic_oa_sysfs_entry(struct xe_oa *oa,
					 struct xe_oa_config *oa_config)
{
	sysfs_attr_init(&oa_config->sysfs_metric_id.attr);
	oa_config->sysfs_metric_id.attr.name = "id";
	oa_config->sysfs_metric_id.attr.mode = 0444;
	oa_config->sysfs_metric_id.show = show_dynamic_id;
	oa_config->sysfs_metric_id.store = NULL;

	oa_config->attrs[0] = &oa_config->sysfs_metric_id.attr;
	oa_config->attrs[1] = NULL;

	oa_config->sysfs_metric.name = oa_config->uuid;
	oa_config->sysfs_metric.attrs = oa_config->attrs;

	return sysfs_create_group(oa->metrics_kobj, &oa_config->sysfs_metric);
}

/**
 * xe_oa_add_config_ioctl - Adds one OA config
 * @dev: @drm_device
 * @data: pointer to struct @drm_xe_oa_config
 * @file: @drm_file
 *
 * The functions adds an OA config to the set of OA configs maintained in
 * the kernel. The config determines which OA metrics are collected for an
 * OA stream.
 */
int xe_oa_add_config_ioctl(struct drm_device *dev, u64 data, struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_oa *oa = &xe->oa;
	struct drm_xe_oa_config param;
	struct drm_xe_oa_config *arg = &param;
	struct xe_oa_config *oa_config, *tmp;
	struct xe_oa_reg *regs;
	int err, id;

	if (!oa->xe) {
		drm_dbg(&xe->drm, "xe oa interface not available for this system\n");
		return -ENODEV;
	}

	if (xe_perf_stream_paranoid && !perfmon_capable()) {
		drm_dbg(&oa->xe->drm, "Insufficient privileges to add xe OA config\n");
		return -EACCES;
	}

	err = __copy_from_user(&param, u64_to_user_ptr(data), sizeof(param));
	if (XE_IOCTL_DBG(oa->xe, err))
		return -EFAULT;

	if (XE_IOCTL_DBG(oa->xe, arg->extensions) ||
	    XE_IOCTL_DBG(oa->xe, !arg->regs_ptr) ||
	    XE_IOCTL_DBG(oa->xe, !arg->n_regs))
		return -EINVAL;

	oa_config = kzalloc(sizeof(*oa_config), GFP_KERNEL);
	if (!oa_config)
		return -ENOMEM;

	oa_config->oa = oa;
	kref_init(&oa_config->ref);

	if (!uuid_is_valid(arg->uuid)) {
		drm_dbg(&oa->xe->drm, "Invalid uuid format for OA config\n");
		err = -EINVAL;
		goto reg_err;
	}

	/* Last character in oa_config->uuid will be 0 because oa_config is kzalloc */
	memcpy(oa_config->uuid, arg->uuid, sizeof(arg->uuid));

	oa_config->regs_len = arg->n_regs;
	regs = xe_oa_alloc_regs(oa, xe_oa_is_valid_config_reg_addr,
				u64_to_user_ptr(arg->regs_ptr),
				arg->n_regs);
	if (IS_ERR(regs)) {
		drm_dbg(&oa->xe->drm, "Failed to create OA config for mux_regs\n");
		err = PTR_ERR(regs);
		goto reg_err;
	}
	oa_config->regs = regs;

	err = mutex_lock_interruptible(&oa->metrics_lock);
	if (err)
		goto reg_err;

	/* We shouldn't have too many configs, so this iteration shouldn't be too costly */
	idr_for_each_entry(&oa->metrics_idr, tmp, id) {
		if (!strcmp(tmp->uuid, oa_config->uuid)) {
			drm_dbg(&oa->xe->drm, "OA config already exists with this uuid\n");
			err = -EADDRINUSE;
			goto sysfs_err;
		}
	}

	err = create_dynamic_oa_sysfs_entry(oa, oa_config);
	if (err) {
		drm_dbg(&oa->xe->drm, "Failed to create sysfs entry for OA config\n");
		goto sysfs_err;
	}

	oa_config->id = idr_alloc(&oa->metrics_idr, oa_config, 1, 0, GFP_KERNEL);
	if (oa_config->id < 0) {
		drm_dbg(&oa->xe->drm, "Failed to create sysfs entry for OA config\n");
		err = oa_config->id;
		goto sysfs_err;
	}

	mutex_unlock(&oa->metrics_lock);

	drm_dbg(&oa->xe->drm, "Added config %s id=%i\n", oa_config->uuid, oa_config->id);

	return oa_config->id;

sysfs_err:
	mutex_unlock(&oa->metrics_lock);
reg_err:
	xe_oa_config_put(oa_config);
	drm_dbg(&oa->xe->drm, "Failed to add new OA config\n");
	return err;
}

/**
 * xe_oa_remove_config_ioctl - Removes one OA config
 * @dev: @drm_device
 * @data: pointer to struct @drm_xe_perf_param
 * @file: @drm_file
 */
int xe_oa_remove_config_ioctl(struct drm_device *dev, u64 data, struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_oa *oa = &xe->oa;
	struct xe_oa_config *oa_config;
	u64 arg, *ptr = u64_to_user_ptr(data);
	int ret;

	if (!oa->xe) {
		drm_dbg(&xe->drm, "xe oa interface not available for this system\n");
		return -ENODEV;
	}

	if (xe_perf_stream_paranoid && !perfmon_capable()) {
		drm_dbg(&oa->xe->drm, "Insufficient privileges to remove xe OA config\n");
		return -EACCES;
	}

	ret = get_user(arg, ptr);
	if (XE_IOCTL_DBG(oa->xe, ret))
		return ret;

	ret = mutex_lock_interruptible(&oa->metrics_lock);
	if (ret)
		return ret;

	oa_config = idr_find(&oa->metrics_idr, arg);
	if (!oa_config) {
		drm_dbg(&oa->xe->drm, "Failed to remove unknown OA config\n");
		ret = -ENOENT;
		goto err_unlock;
	}

	WARN_ON(arg != oa_config->id);

	sysfs_remove_group(oa->metrics_kobj, &oa_config->sysfs_metric);
	idr_remove(&oa->metrics_idr, arg);

	mutex_unlock(&oa->metrics_lock);

	drm_dbg(&oa->xe->drm, "Removed config %s id=%i\n", oa_config->uuid, oa_config->id);

	xe_oa_config_put(oa_config);

	return 0;

err_unlock:
	mutex_unlock(&oa->metrics_lock);
	return ret;
}

/**
 * xe_oa_register - Xe OA registration
 * @xe: @xe_device
 *
 * Exposes the metrics sysfs directory upon completion of module initialization
 */
void xe_oa_register(struct xe_device *xe)
{
	struct xe_oa *oa = &xe->oa;

	if (!oa->xe)
		return;

	oa->metrics_kobj = kobject_create_and_add("metrics",
						  &xe->drm.primary->kdev->kobj);
}

/**
 * xe_oa_unregister - Xe OA de-registration
 * @xe: @xe_device
 */
void xe_oa_unregister(struct xe_device *xe)
{
	struct xe_oa *oa = &xe->oa;

	if (!oa->metrics_kobj)
		return;

	kobject_put(oa->metrics_kobj);
	oa->metrics_kobj = NULL;
}

static u32 num_oa_units_per_gt(struct xe_gt *gt)
{
	return 1;
}

static u32 __hwe_oam_unit(struct xe_hw_engine *hwe)
{
	if (GRAPHICS_VERx100(gt_to_xe(hwe->gt)) >= 1270) {
		/*
		 * There's 1 SAMEDIA gt and 1 OAM per SAMEDIA gt. All media slices
		 * within the gt use the same OAM. All MTL/LNL SKUs list 1 SA MEDIA
		 */
		xe_gt_WARN_ON(hwe->gt, hwe->gt->info.type != XE_GT_TYPE_MEDIA);

		return 0;
	}

	return XE_OA_UNIT_INVALID;
}

static u32 __hwe_oa_unit(struct xe_hw_engine *hwe)
{
	switch (hwe->class) {
	case XE_ENGINE_CLASS_RENDER:
	case XE_ENGINE_CLASS_COMPUTE:
		return 0;

	case XE_ENGINE_CLASS_VIDEO_DECODE:
	case XE_ENGINE_CLASS_VIDEO_ENHANCE:
		return __hwe_oam_unit(hwe);

	default:
		return XE_OA_UNIT_INVALID;
	}
}

static struct xe_oa_regs __oam_regs(u32 base)
{
	return (struct xe_oa_regs) {
		base,
		OAM_HEAD_POINTER(base),
		OAM_TAIL_POINTER(base),
		OAM_BUFFER(base),
		OAM_CONTEXT_CONTROL(base),
		OAM_CONTROL(base),
		OAM_DEBUG(base),
		OAM_STATUS(base),
		OAM_CONTROL_COUNTER_SEL_MASK,
	};
}

static struct xe_oa_regs __oag_regs(void)
{
	return (struct xe_oa_regs) {
		0,
		OAG_OAHEADPTR,
		OAG_OATAILPTR,
		OAG_OABUFFER,
		OAG_OAGLBCTXCTRL,
		OAG_OACONTROL,
		OAG_OA_DEBUG,
		OAG_OASTATUS,
		OAG_OACONTROL_OA_COUNTER_SEL_MASK,
	};
}

static void __xe_oa_init_oa_units(struct xe_gt *gt)
{
	const u32 mtl_oa_base[] = { 0x13000 };
	int i, num_units = gt->oa.num_oa_units;

	for (i = 0; i < num_units; i++) {
		struct xe_oa_unit *u = &gt->oa.oa_unit[i];

		if (gt->info.type != XE_GT_TYPE_MEDIA) {
			u->regs = __oag_regs();
			u->type = DRM_XE_OA_UNIT_TYPE_OAG;
		} else if (GRAPHICS_VERx100(gt_to_xe(gt)) >= 1270) {
			u->regs = __oam_regs(mtl_oa_base[i]);
			u->type = DRM_XE_OA_UNIT_TYPE_OAM;
		}

		/* Ensure MMIO trigger remains disabled till there is a stream */
		xe_mmio_write32(gt, u->regs.oa_debug,
				oag_configure_mmio_trigger(NULL, false));

		/* Set oa_unit_ids now to ensure ids remain contiguous */
		u->oa_unit_id = gt_to_xe(gt)->oa.oa_unit_ids++;
	}
}

static int xe_oa_init_gt(struct xe_gt *gt)
{
	u32 num_oa_units = num_oa_units_per_gt(gt);
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;
	struct xe_oa_unit *u;

	u = drmm_kcalloc(&gt_to_xe(gt)->drm, num_oa_units, sizeof(*u), GFP_KERNEL);
	if (!u)
		return -ENOMEM;

	for_each_hw_engine(hwe, gt, id) {
		u32 index = __hwe_oa_unit(hwe);

		hwe->oa_unit = NULL;
		if (index < num_oa_units) {
			u[index].num_engines++;
			hwe->oa_unit = &u[index];
		}
	}

	/*
	 * Fused off engines can result in oa_unit's with num_engines == 0. These units
	 * will appear in OA unit query, but no perf streams can be opened on them.
	 */
	gt->oa.num_oa_units = num_oa_units;
	gt->oa.oa_unit = u;

	__xe_oa_init_oa_units(gt);

	drmm_mutex_init(&gt_to_xe(gt)->drm, &gt->oa.gt_lock);

	return 0;
}

static int xe_oa_init_oa_units(struct xe_oa *oa)
{
	struct xe_gt *gt;
	int i, ret;

	for_each_gt(gt, oa->xe, i) {
		ret = xe_oa_init_gt(gt);
		if (ret)
			return ret;
	}

	return 0;
}

static void oa_format_add(struct xe_oa *oa, enum xe_oa_format_name format)
{
	__set_bit(format, oa->format_mask);
}

static void xe_oa_init_supported_formats(struct xe_oa *oa)
{
	if (GRAPHICS_VER(oa->xe) >= 20) {
		/* Xe2+ */
		oa_format_add(oa, XE_OAM_FORMAT_MPEC8u64_B8_C8);
		oa_format_add(oa, XE_OAM_FORMAT_MPEC8u32_B8_C8);
		oa_format_add(oa, XE_OA_FORMAT_PEC64u64);
		oa_format_add(oa, XE_OA_FORMAT_PEC64u64_B8_C8);
		oa_format_add(oa, XE_OA_FORMAT_PEC64u32);
		oa_format_add(oa, XE_OA_FORMAT_PEC32u64_G1);
		oa_format_add(oa, XE_OA_FORMAT_PEC32u32_G1);
		oa_format_add(oa, XE_OA_FORMAT_PEC32u64_G2);
		oa_format_add(oa, XE_OA_FORMAT_PEC32u32_G2);
		oa_format_add(oa, XE_OA_FORMAT_PEC36u64_G1_32_G2_4);
		oa_format_add(oa, XE_OA_FORMAT_PEC36u64_G1_4_G2_32);
	} else if (GRAPHICS_VERx100(oa->xe) >= 1270) {
		/* XE_METEORLAKE */
		oa_format_add(oa, XE_OAR_FORMAT_A32u40_A4u32_B8_C8);
		oa_format_add(oa, XE_OA_FORMAT_A24u40_A14u32_B8_C8);
		oa_format_add(oa, XE_OAC_FORMAT_A24u64_B8_C8);
		oa_format_add(oa, XE_OAC_FORMAT_A22u32_R2u32_B8_C8);
		oa_format_add(oa, XE_OAM_FORMAT_MPEC8u64_B8_C8);
		oa_format_add(oa, XE_OAM_FORMAT_MPEC8u32_B8_C8);
	} else if (GRAPHICS_VERx100(oa->xe) >= 1255) {
		/* XE_DG2, XE_PVC */
		oa_format_add(oa, XE_OAR_FORMAT_A32u40_A4u32_B8_C8);
		oa_format_add(oa, XE_OA_FORMAT_A24u40_A14u32_B8_C8);
		oa_format_add(oa, XE_OAC_FORMAT_A24u64_B8_C8);
		oa_format_add(oa, XE_OAC_FORMAT_A22u32_R2u32_B8_C8);
	} else {
		/* Gen12+ */
		xe_assert(oa->xe, GRAPHICS_VER(oa->xe) >= 12);
		oa_format_add(oa, XE_OA_FORMAT_A12);
		oa_format_add(oa, XE_OA_FORMAT_A12_B8_C8);
		oa_format_add(oa, XE_OA_FORMAT_A32u40_A4u32_B8_C8);
		oa_format_add(oa, XE_OA_FORMAT_C4_B8);
	}
}

/**
 * xe_oa_init - OA initialization during device probe
 * @xe: @xe_device
 *
 * Return: 0 on success or a negative error code on failure
 */
int xe_oa_init(struct xe_device *xe)
{
	struct xe_oa *oa = &xe->oa;
	int ret;

	/* Support OA only with GuC submission and Gen12+ */
	if (!xe_device_uc_enabled(xe) || GRAPHICS_VER(xe) < 12)
		return 0;

	if (IS_SRIOV_VF(xe))
		return 0;

	oa->xe = xe;
	oa->oa_formats = oa_formats;

	drmm_mutex_init(&oa->xe->drm, &oa->metrics_lock);
	idr_init_base(&oa->metrics_idr, 1);

	ret = xe_oa_init_oa_units(oa);
	if (ret) {
		drm_err(&xe->drm, "OA initialization failed (%pe)\n", ERR_PTR(ret));
		goto exit;
	}

	xe_oa_init_supported_formats(oa);
	return 0;
exit:
	oa->xe = NULL;
	return ret;
}

static int destroy_config(int id, void *p, void *data)
{
	xe_oa_config_put(p);
	return 0;
}

/**
 * xe_oa_fini - OA de-initialization during device remove
 * @xe: @xe_device
 */
void xe_oa_fini(struct xe_device *xe)
{
	struct xe_oa *oa = &xe->oa;

	if (!oa->xe)
		return;

	idr_for_each(&oa->metrics_idr, destroy_config, oa);
	idr_destroy(&oa->metrics_idr);

	oa->xe = NULL;
}
