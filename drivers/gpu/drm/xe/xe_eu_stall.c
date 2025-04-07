// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/anon_inodes.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/types.h>

#include <drm/drm_drv.h>
#include <generated/xe_wa_oob.h>
#include <uapi/drm/xe_drm.h>

#include "xe_bo.h"
#include "xe_device.h"
#include "xe_eu_stall.h"
#include "xe_force_wake.h"
#include "xe_gt_mcr.h"
#include "xe_gt_printk.h"
#include "xe_gt_topology.h"
#include "xe_macros.h"
#include "xe_observation.h"
#include "xe_pm.h"
#include "xe_trace.h"
#include "xe_wa.h"

#include "regs/xe_eu_stall_regs.h"
#include "regs/xe_gt_regs.h"

#define POLL_PERIOD_MS	5

static size_t per_xecore_buf_size = SZ_512K;

struct per_xecore_buf {
	/* Buffer vaddr */
	u8 *vaddr;
	/* Write pointer */
	u32 write;
	/* Read pointer */
	u32 read;
};

struct xe_eu_stall_data_stream {
	bool pollin;
	bool enabled;
	int wait_num_reports;
	int sampling_rate_mult;
	wait_queue_head_t poll_wq;
	size_t data_record_size;
	size_t per_xecore_buf_size;

	struct xe_gt *gt;
	struct xe_bo *bo;
	struct per_xecore_buf *xecore_buf;
	struct {
		bool reported_to_user;
		xe_dss_mask_t mask;
	} data_drop;
	struct delayed_work buf_poll_work;
};

struct xe_eu_stall_gt {
	/* Lock to protect stream */
	struct mutex stream_lock;
	/* EU stall data stream */
	struct xe_eu_stall_data_stream *stream;
	/* Workqueue to schedule buffer pointers polling work */
	struct workqueue_struct *buf_ptr_poll_wq;
};

/**
 * struct eu_stall_open_properties - EU stall sampling properties received
 *				     from user space at open.
 * @sampling_rate_mult: EU stall sampling rate multiplier.
 *			HW will sample every (sampling_rate_mult x 251) cycles.
 * @wait_num_reports: Minimum number of EU stall data reports to unblock poll().
 * @gt: GT on which EU stall data will be captured.
 */
struct eu_stall_open_properties {
	int sampling_rate_mult;
	int wait_num_reports;
	struct xe_gt *gt;
};

/*
 * EU stall data format for PVC
 */
struct xe_eu_stall_data_pvc {
	__u64 ip_addr:29;	  /* Bits 0  to 28  */
	__u64 active_count:8;	  /* Bits 29 to 36  */
	__u64 other_count:8;	  /* Bits 37 to 44  */
	__u64 control_count:8;	  /* Bits 45 to 52  */
	__u64 pipestall_count:8;  /* Bits 53 to 60  */
	__u64 send_count:8;	  /* Bits 61 to 68  */
	__u64 dist_acc_count:8;	  /* Bits 69 to 76  */
	__u64 sbid_count:8;	  /* Bits 77 to 84  */
	__u64 sync_count:8;	  /* Bits 85 to 92  */
	__u64 inst_fetch_count:8; /* Bits 93 to 100 */
	__u64 unused_bits:27;
	__u64 unused[6];
} __packed;

/*
 * EU stall data format for Xe2 arch GPUs (LNL, BMG).
 */
struct xe_eu_stall_data_xe2 {
	__u64 ip_addr:29;	  /* Bits 0  to 28  */
	__u64 tdr_count:8;	  /* Bits 29 to 36  */
	__u64 other_count:8;	  /* Bits 37 to 44  */
	__u64 control_count:8;	  /* Bits 45 to 52  */
	__u64 pipestall_count:8;  /* Bits 53 to 60  */
	__u64 send_count:8;	  /* Bits 61 to 68  */
	__u64 dist_acc_count:8;   /* Bits 69 to 76  */
	__u64 sbid_count:8;	  /* Bits 77 to 84  */
	__u64 sync_count:8;	  /* Bits 85 to 92  */
	__u64 inst_fetch_count:8; /* Bits 93 to 100 */
	__u64 active_count:8;	  /* Bits 101 to 108 */
	__u64 ex_id:3;		  /* Bits 109 to 111 */
	__u64 end_flag:1;	  /* Bit  112 */
	__u64 unused_bits:15;
	__u64 unused[6];
} __packed;

const u64 eu_stall_sampling_rates[] = {251, 251 * 2, 251 * 3, 251 * 4, 251 * 5, 251 * 6, 251 * 7};

/**
 * xe_eu_stall_get_sampling_rates - get EU stall sampling rates information.
 *
 * @num_rates: Pointer to a u32 to return the number of sampling rates.
 * @rates: double u64 pointer to point to an array of sampling rates.
 *
 * Stores the number of sampling rates and pointer to the array of
 * sampling rates in the input pointers.
 *
 * Returns: Size of the EU stall sampling rates array.
 */
size_t xe_eu_stall_get_sampling_rates(u32 *num_rates, const u64 **rates)
{
	*num_rates = ARRAY_SIZE(eu_stall_sampling_rates);
	*rates = eu_stall_sampling_rates;

	return sizeof(eu_stall_sampling_rates);
}

/**
 * xe_eu_stall_get_per_xecore_buf_size - get per XeCore buffer size.
 *
 * Returns: The per XeCore buffer size used to allocate the per GT
 *	    EU stall data buffer.
 */
size_t xe_eu_stall_get_per_xecore_buf_size(void)
{
	return per_xecore_buf_size;
}

/**
 * xe_eu_stall_data_record_size - get EU stall data record size.
 *
 * @xe: Pointer to a Xe device.
 *
 * Returns: EU stall data record size.
 */
size_t xe_eu_stall_data_record_size(struct xe_device *xe)
{
	size_t record_size = 0;

	if (xe->info.platform == XE_PVC)
		record_size = sizeof(struct xe_eu_stall_data_pvc);
	else if (GRAPHICS_VER(xe) >= 20)
		record_size = sizeof(struct xe_eu_stall_data_xe2);

	xe_assert(xe, is_power_of_2(record_size));

	return record_size;
}

/**
 * num_data_rows - Return the number of EU stall data rows of 64B each
 *		   for a given data size.
 *
 * @data_size: EU stall data size
 */
static u32 num_data_rows(u32 data_size)
{
	return data_size >> 6;
}

static void xe_eu_stall_fini(void *arg)
{
	struct xe_gt *gt = arg;

	destroy_workqueue(gt->eu_stall->buf_ptr_poll_wq);
	mutex_destroy(&gt->eu_stall->stream_lock);
	kfree(gt->eu_stall);
}

/**
 * xe_eu_stall_init() - Allocate and initialize GT level EU stall data
 *			structure xe_eu_stall_gt within struct xe_gt.
 *
 * @gt: GT being initialized.
 *
 * Returns: zero on success or a negative error code.
 */
int xe_eu_stall_init(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	int ret;

	gt->eu_stall = kzalloc(sizeof(*gt->eu_stall), GFP_KERNEL);
	if (!gt->eu_stall) {
		ret = -ENOMEM;
		goto exit;
	}

	mutex_init(&gt->eu_stall->stream_lock);

	gt->eu_stall->buf_ptr_poll_wq = alloc_ordered_workqueue("xe_eu_stall", 0);
	if (!gt->eu_stall->buf_ptr_poll_wq) {
		ret = -ENOMEM;
		goto exit_free;
	}

	return devm_add_action_or_reset(xe->drm.dev, xe_eu_stall_fini, gt);
exit_free:
	mutex_destroy(&gt->eu_stall->stream_lock);
	kfree(gt->eu_stall);
exit:
	return ret;
}

static int set_prop_eu_stall_sampling_rate(struct xe_device *xe, u64 value,
					   struct eu_stall_open_properties *props)
{
	value = div_u64(value, 251);
	if (value == 0 || value > 7) {
		drm_dbg(&xe->drm, "Invalid EU stall sampling rate %llu\n", value);
		return -EINVAL;
	}
	props->sampling_rate_mult = value;
	return 0;
}

static int set_prop_eu_stall_wait_num_reports(struct xe_device *xe, u64 value,
					      struct eu_stall_open_properties *props)
{
	props->wait_num_reports = value;

	return 0;
}

static int set_prop_eu_stall_gt_id(struct xe_device *xe, u64 value,
				   struct eu_stall_open_properties *props)
{
	if (value >= xe->info.gt_count) {
		drm_dbg(&xe->drm, "Invalid GT ID %llu for EU stall sampling\n", value);
		return -EINVAL;
	}
	props->gt = xe_device_get_gt(xe, value);
	return 0;
}

typedef int (*set_eu_stall_property_fn)(struct xe_device *xe, u64 value,
					struct eu_stall_open_properties *props);

static const set_eu_stall_property_fn xe_set_eu_stall_property_funcs[] = {
	[DRM_XE_EU_STALL_PROP_SAMPLE_RATE] = set_prop_eu_stall_sampling_rate,
	[DRM_XE_EU_STALL_PROP_WAIT_NUM_REPORTS] = set_prop_eu_stall_wait_num_reports,
	[DRM_XE_EU_STALL_PROP_GT_ID] = set_prop_eu_stall_gt_id,
};

static int xe_eu_stall_user_ext_set_property(struct xe_device *xe, u64 extension,
					     struct eu_stall_open_properties *props)
{
	u64 __user *address = u64_to_user_ptr(extension);
	struct drm_xe_ext_set_property ext;
	int err;
	u32 idx;

	err = __copy_from_user(&ext, address, sizeof(ext));
	if (XE_IOCTL_DBG(xe, err))
		return -EFAULT;

	if (XE_IOCTL_DBG(xe, ext.property >= ARRAY_SIZE(xe_set_eu_stall_property_funcs)) ||
	    XE_IOCTL_DBG(xe, ext.pad))
		return -EINVAL;

	idx = array_index_nospec(ext.property, ARRAY_SIZE(xe_set_eu_stall_property_funcs));
	return xe_set_eu_stall_property_funcs[idx](xe, ext.value, props);
}

typedef int (*xe_eu_stall_user_extension_fn)(struct xe_device *xe, u64 extension,
					     struct eu_stall_open_properties *props);
static const xe_eu_stall_user_extension_fn xe_eu_stall_user_extension_funcs[] = {
	[DRM_XE_EU_STALL_EXTENSION_SET_PROPERTY] = xe_eu_stall_user_ext_set_property,
};

#define MAX_USER_EXTENSIONS	5
static int xe_eu_stall_user_extensions(struct xe_device *xe, u64 extension,
				       int ext_number, struct eu_stall_open_properties *props)
{
	u64 __user *address = u64_to_user_ptr(extension);
	struct drm_xe_user_extension ext;
	int err;
	u32 idx;

	if (XE_IOCTL_DBG(xe, ext_number >= MAX_USER_EXTENSIONS))
		return -E2BIG;

	err = __copy_from_user(&ext, address, sizeof(ext));
	if (XE_IOCTL_DBG(xe, err))
		return -EFAULT;

	if (XE_IOCTL_DBG(xe, ext.pad) ||
	    XE_IOCTL_DBG(xe, ext.name >= ARRAY_SIZE(xe_eu_stall_user_extension_funcs)))
		return -EINVAL;

	idx = array_index_nospec(ext.name, ARRAY_SIZE(xe_eu_stall_user_extension_funcs));
	err = xe_eu_stall_user_extension_funcs[idx](xe, extension, props);
	if (XE_IOCTL_DBG(xe, err))
		return err;

	if (ext.next_extension)
		return xe_eu_stall_user_extensions(xe, ext.next_extension, ++ext_number, props);

	return 0;
}

/**
 * buf_data_size - Calculate the number of bytes in a circular buffer
 *		   given the read and write pointers and the size of
 *		   the buffer.
 *
 * @buf_size: Size of the circular buffer
 * @read_ptr: Read pointer with an additional overflow bit
 * @write_ptr: Write pointer with an additional overflow bit
 *
 * Since the read and write pointers have an additional overflow bit,
 * this function calculates the offsets from the pointers and use the
 * offsets to calculate the data size in the buffer.
 *
 * Returns: number of bytes of data in the buffer
 */
static u32 buf_data_size(size_t buf_size, u32 read_ptr, u32 write_ptr)
{
	u32 read_offset, write_offset, size = 0;

	if (read_ptr == write_ptr)
		goto exit;

	read_offset = read_ptr & (buf_size - 1);
	write_offset = write_ptr & (buf_size - 1);

	if (write_offset > read_offset)
		size = write_offset - read_offset;
	else
		size = buf_size - read_offset + write_offset;
exit:
	return size;
}

/**
 * eu_stall_data_buf_poll - Poll for EU stall data in the buffer.
 *
 * @stream: xe EU stall data stream instance
 *
 * Returns: true if the EU stall buffer contains minimum stall data as
 *	    specified by the event report count, else false.
 */
static bool eu_stall_data_buf_poll(struct xe_eu_stall_data_stream *stream)
{
	u32 read_ptr, write_ptr_reg, write_ptr, total_data = 0;
	u32 buf_size = stream->per_xecore_buf_size;
	struct per_xecore_buf *xecore_buf;
	struct xe_gt *gt = stream->gt;
	bool min_data_present = false;
	u16 group, instance;
	unsigned int xecore;

	mutex_lock(&gt->eu_stall->stream_lock);
	for_each_dss_steering(xecore, gt, group, instance) {
		xecore_buf = &stream->xecore_buf[xecore];
		read_ptr = xecore_buf->read;
		write_ptr_reg = xe_gt_mcr_unicast_read(gt, XEHPC_EUSTALL_REPORT,
						       group, instance);
		write_ptr = REG_FIELD_GET(XEHPC_EUSTALL_REPORT_WRITE_PTR_MASK, write_ptr_reg);
		write_ptr <<= 6;
		write_ptr &= ((buf_size << 1) - 1);
		if (!min_data_present) {
			total_data += buf_data_size(buf_size, read_ptr, write_ptr);
			if (num_data_rows(total_data) >= stream->wait_num_reports)
				min_data_present = true;
		}
		if (write_ptr_reg & XEHPC_EUSTALL_REPORT_OVERFLOW_DROP)
			set_bit(xecore, stream->data_drop.mask);
		xecore_buf->write = write_ptr;
	}
	mutex_unlock(&gt->eu_stall->stream_lock);

	return min_data_present;
}

static void clear_dropped_eviction_line_bit(struct xe_gt *gt, u16 group, u16 instance)
{
	struct xe_device *xe = gt_to_xe(gt);
	u32 write_ptr_reg;

	/* On PVC, the overflow bit has to be cleared by writing 1 to it.
	 * On Xe2 and later GPUs, the bit has to be cleared by writing 0 to it.
	 */
	if (GRAPHICS_VER(xe) >= 20)
		write_ptr_reg = _MASKED_BIT_DISABLE(XEHPC_EUSTALL_REPORT_OVERFLOW_DROP);
	else
		write_ptr_reg = _MASKED_BIT_ENABLE(XEHPC_EUSTALL_REPORT_OVERFLOW_DROP);

	xe_gt_mcr_unicast_write(gt, XEHPC_EUSTALL_REPORT, write_ptr_reg, group, instance);
}

static int xe_eu_stall_data_buf_read(struct xe_eu_stall_data_stream *stream,
				     char __user *buf, size_t count,
				     size_t *total_data_size, struct xe_gt *gt,
				     u16 group, u16 instance, unsigned int xecore)
{
	size_t read_data_size, copy_size, buf_size;
	u32 read_ptr_reg, read_ptr, write_ptr;
	u8 *xecore_start_vaddr, *read_vaddr;
	struct per_xecore_buf *xecore_buf;
	u32 read_offset, write_offset;

	/* Hardware increments the read and write pointers such that they can
	 * overflow into one additional bit. For example, a 256KB size buffer
	 * offset pointer needs 18 bits. But HW uses 19 bits for the read and
	 * write pointers. This technique avoids wasting a slot in the buffer.
	 * Read and write offsets are calculated from the pointers in order to
	 * check if the write pointer has wrapped around the array.
	 */
	xecore_buf = &stream->xecore_buf[xecore];
	xecore_start_vaddr = xecore_buf->vaddr;
	read_ptr = xecore_buf->read;
	write_ptr = xecore_buf->write;
	buf_size = stream->per_xecore_buf_size;

	read_data_size = buf_data_size(buf_size, read_ptr, write_ptr);
	/* Read only the data that the user space buffer can accommodate */
	read_data_size = min_t(size_t, count - *total_data_size, read_data_size);
	if (read_data_size == 0)
		goto exit_drop;

	read_offset = read_ptr & (buf_size - 1);
	write_offset = write_ptr & (buf_size - 1);
	read_vaddr = xecore_start_vaddr + read_offset;

	if (write_offset > read_offset) {
		if (copy_to_user(buf + *total_data_size, read_vaddr, read_data_size))
			return -EFAULT;
	} else {
		if (read_data_size >= buf_size - read_offset)
			copy_size = buf_size - read_offset;
		else
			copy_size = read_data_size;
		if (copy_to_user(buf + *total_data_size, read_vaddr, copy_size))
			return -EFAULT;
		if (copy_to_user(buf + *total_data_size + copy_size,
				 xecore_start_vaddr, read_data_size - copy_size))
			return -EFAULT;
	}

	*total_data_size += read_data_size;
	read_ptr += read_data_size;

	/* Read pointer can overflow into one additional bit */
	read_ptr &= (buf_size << 1) - 1;
	read_ptr_reg = REG_FIELD_PREP(XEHPC_EUSTALL_REPORT1_READ_PTR_MASK, (read_ptr >> 6));
	read_ptr_reg = _MASKED_FIELD(XEHPC_EUSTALL_REPORT1_READ_PTR_MASK, read_ptr_reg);
	xe_gt_mcr_unicast_write(gt, XEHPC_EUSTALL_REPORT1, read_ptr_reg, group, instance);
	xecore_buf->read = read_ptr;
	trace_xe_eu_stall_data_read(group, instance, read_ptr, write_ptr,
				    read_data_size, *total_data_size);
exit_drop:
	/* Clear drop bit (if set) after any data was read or if the buffer was empty.
	 * Drop bit can be set even if the buffer is empty as the buffer may have been emptied
	 * in the previous read() and the data drop bit was set during the previous read().
	 */
	if (test_bit(xecore, stream->data_drop.mask)) {
		clear_dropped_eviction_line_bit(gt, group, instance);
		clear_bit(xecore, stream->data_drop.mask);
	}
	return 0;
}

/**
 * xe_eu_stall_stream_read_locked - copy EU stall counters data from the
 *				    per xecore buffers to the userspace buffer
 * @stream: A stream opened for EU stall count metrics
 * @file: An xe EU stall data stream file
 * @buf: destination buffer given by userspace
 * @count: the number of bytes userspace wants to read
 *
 * Returns: Number of bytes copied or a negative error code
 * If we've successfully copied any data then reporting that takes
 * precedence over any internal error status, so the data isn't lost.
 */
static ssize_t xe_eu_stall_stream_read_locked(struct xe_eu_stall_data_stream *stream,
					      struct file *file, char __user *buf,
					      size_t count)
{
	struct xe_gt *gt = stream->gt;
	size_t total_size = 0;
	u16 group, instance;
	unsigned int xecore;
	int ret = 0;

	if (bitmap_weight(stream->data_drop.mask, XE_MAX_DSS_FUSE_BITS)) {
		if (!stream->data_drop.reported_to_user) {
			stream->data_drop.reported_to_user = true;
			xe_gt_dbg(gt, "EU stall data dropped in XeCores: %*pb\n",
				  XE_MAX_DSS_FUSE_BITS, stream->data_drop.mask);
			return -EIO;
		}
		stream->data_drop.reported_to_user = false;
	}

	for_each_dss_steering(xecore, gt, group, instance) {
		ret = xe_eu_stall_data_buf_read(stream, buf, count, &total_size,
						gt, group, instance, xecore);
		if (ret || count == total_size)
			break;
	}
	return total_size ?: (ret ?: -EAGAIN);
}

/*
 * Userspace must enable the EU stall stream with DRM_XE_OBSERVATION_IOCTL_ENABLE
 * before calling read().
 *
 * Returns: The number of bytes copied or a negative error code on failure.
 *	    -EIO if HW drops any EU stall data when the buffer is full.
 */
static ssize_t xe_eu_stall_stream_read(struct file *file, char __user *buf,
				       size_t count, loff_t *ppos)
{
	struct xe_eu_stall_data_stream *stream = file->private_data;
	struct xe_gt *gt = stream->gt;
	ssize_t ret, aligned_count;

	aligned_count = ALIGN_DOWN(count, stream->data_record_size);
	if (aligned_count == 0)
		return -EINVAL;

	if (!stream->enabled) {
		xe_gt_dbg(gt, "EU stall data stream not enabled to read\n");
		return -EINVAL;
	}

	if (!(file->f_flags & O_NONBLOCK)) {
		do {
			ret = wait_event_interruptible(stream->poll_wq, stream->pollin);
			if (ret)
				return -EINTR;

			mutex_lock(&gt->eu_stall->stream_lock);
			ret = xe_eu_stall_stream_read_locked(stream, file, buf, aligned_count);
			mutex_unlock(&gt->eu_stall->stream_lock);
		} while (ret == -EAGAIN);
	} else {
		mutex_lock(&gt->eu_stall->stream_lock);
		ret = xe_eu_stall_stream_read_locked(stream, file, buf, aligned_count);
		mutex_unlock(&gt->eu_stall->stream_lock);
	}

	/*
	 * This may not work correctly if the user buffer is very small.
	 * We don't want to block the next read() when there is data in the buffer
	 * now, but couldn't be accommodated in the small user buffer.
	 */
	stream->pollin = false;

	return ret;
}

static void xe_eu_stall_stream_free(struct xe_eu_stall_data_stream *stream)
{
	struct xe_gt *gt = stream->gt;

	gt->eu_stall->stream = NULL;
	kfree(stream);
}

static void xe_eu_stall_data_buf_destroy(struct xe_eu_stall_data_stream *stream)
{
	xe_bo_unpin_map_no_vm(stream->bo);
	kfree(stream->xecore_buf);
}

static int xe_eu_stall_data_buf_alloc(struct xe_eu_stall_data_stream *stream,
				      u16 last_xecore)
{
	struct xe_tile *tile = stream->gt->tile;
	struct xe_bo *bo;
	u32 size;

	stream->xecore_buf = kcalloc(last_xecore, sizeof(*stream->xecore_buf), GFP_KERNEL);
	if (!stream->xecore_buf)
		return -ENOMEM;

	size = stream->per_xecore_buf_size * last_xecore;

	bo = xe_bo_create_pin_map_at_aligned(tile->xe, tile, NULL,
					     size, ~0ull, ttm_bo_type_kernel,
					     XE_BO_FLAG_SYSTEM | XE_BO_FLAG_GGTT, SZ_64);
	if (IS_ERR(bo)) {
		kfree(stream->xecore_buf);
		return PTR_ERR(bo);
	}

	XE_WARN_ON(!IS_ALIGNED(xe_bo_ggtt_addr(bo), SZ_64));
	stream->bo = bo;

	return 0;
}

static int xe_eu_stall_stream_enable(struct xe_eu_stall_data_stream *stream)
{
	u32 write_ptr_reg, write_ptr, read_ptr_reg, reg_value;
	struct per_xecore_buf *xecore_buf;
	struct xe_gt *gt = stream->gt;
	u16 group, instance;
	unsigned int fw_ref;
	int xecore;

	/* Take runtime pm ref and forcewake to disable RC6 */
	xe_pm_runtime_get(gt_to_xe(gt));
	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FW_RENDER);
	if (!xe_force_wake_ref_has_domain(fw_ref, XE_FW_RENDER)) {
		xe_gt_err(gt, "Failed to get RENDER forcewake\n");
		xe_pm_runtime_put(gt_to_xe(gt));
		return -ETIMEDOUT;
	}

	if (XE_WA(gt, 22016596838))
		xe_gt_mcr_multicast_write(gt, ROW_CHICKEN2,
					  _MASKED_BIT_ENABLE(DISABLE_DOP_GATING));

	for_each_dss_steering(xecore, gt, group, instance) {
		write_ptr_reg = xe_gt_mcr_unicast_read(gt, XEHPC_EUSTALL_REPORT, group, instance);
		/* Clear any drop bits set and not cleared in the previous session. */
		if (write_ptr_reg & XEHPC_EUSTALL_REPORT_OVERFLOW_DROP)
			clear_dropped_eviction_line_bit(gt, group, instance);
		write_ptr = REG_FIELD_GET(XEHPC_EUSTALL_REPORT_WRITE_PTR_MASK, write_ptr_reg);
		read_ptr_reg = REG_FIELD_PREP(XEHPC_EUSTALL_REPORT1_READ_PTR_MASK, write_ptr);
		read_ptr_reg = _MASKED_FIELD(XEHPC_EUSTALL_REPORT1_READ_PTR_MASK, read_ptr_reg);
		/* Initialize the read pointer to the write pointer */
		xe_gt_mcr_unicast_write(gt, XEHPC_EUSTALL_REPORT1, read_ptr_reg, group, instance);
		write_ptr <<= 6;
		write_ptr &= (stream->per_xecore_buf_size << 1) - 1;
		xecore_buf = &stream->xecore_buf[xecore];
		xecore_buf->write = write_ptr;
		xecore_buf->read = write_ptr;
	}
	stream->data_drop.reported_to_user = false;
	bitmap_zero(stream->data_drop.mask, XE_MAX_DSS_FUSE_BITS);

	reg_value = _MASKED_FIELD(EUSTALL_MOCS | EUSTALL_SAMPLE_RATE,
				  REG_FIELD_PREP(EUSTALL_MOCS, gt->mocs.uc_index << 1) |
				  REG_FIELD_PREP(EUSTALL_SAMPLE_RATE,
						 stream->sampling_rate_mult));
	xe_gt_mcr_multicast_write(gt, XEHPC_EUSTALL_CTRL, reg_value);
	/* GGTT addresses can never be > 32 bits */
	xe_gt_mcr_multicast_write(gt, XEHPC_EUSTALL_BASE_UPPER, 0);
	reg_value = xe_bo_ggtt_addr(stream->bo);
	reg_value |= REG_FIELD_PREP(XEHPC_EUSTALL_BASE_XECORE_BUF_SZ,
				    stream->per_xecore_buf_size / SZ_256K);
	reg_value |= XEHPC_EUSTALL_BASE_ENABLE_SAMPLING;
	xe_gt_mcr_multicast_write(gt, XEHPC_EUSTALL_BASE, reg_value);

	return 0;
}

static void eu_stall_data_buf_poll_work_fn(struct work_struct *work)
{
	struct xe_eu_stall_data_stream *stream =
		container_of(work, typeof(*stream), buf_poll_work.work);
	struct xe_gt *gt = stream->gt;

	if (eu_stall_data_buf_poll(stream)) {
		stream->pollin = true;
		wake_up(&stream->poll_wq);
	}
	queue_delayed_work(gt->eu_stall->buf_ptr_poll_wq,
			   &stream->buf_poll_work,
			   msecs_to_jiffies(POLL_PERIOD_MS));
}

static int xe_eu_stall_stream_init(struct xe_eu_stall_data_stream *stream,
				   struct eu_stall_open_properties *props)
{
	unsigned int max_wait_num_reports, xecore, last_xecore, num_xecores;
	struct per_xecore_buf *xecore_buf;
	struct xe_gt *gt = stream->gt;
	xe_dss_mask_t all_xecores;
	u16 group, instance;
	u32 vaddr_offset;
	int ret;

	bitmap_or(all_xecores, gt->fuse_topo.g_dss_mask, gt->fuse_topo.c_dss_mask,
		  XE_MAX_DSS_FUSE_BITS);
	num_xecores = bitmap_weight(all_xecores, XE_MAX_DSS_FUSE_BITS);
	last_xecore = xe_gt_topology_mask_last_dss(all_xecores) + 1;

	max_wait_num_reports = num_data_rows(per_xecore_buf_size * num_xecores);
	if (props->wait_num_reports == 0 || props->wait_num_reports > max_wait_num_reports) {
		xe_gt_dbg(gt, "Invalid EU stall event report count %u\n",
			  props->wait_num_reports);
		xe_gt_dbg(gt, "Minimum event report count is 1, maximum is %u\n",
			  max_wait_num_reports);
		return -EINVAL;
	}

	init_waitqueue_head(&stream->poll_wq);
	INIT_DELAYED_WORK(&stream->buf_poll_work, eu_stall_data_buf_poll_work_fn);
	stream->per_xecore_buf_size = per_xecore_buf_size;
	stream->sampling_rate_mult = props->sampling_rate_mult;
	stream->wait_num_reports = props->wait_num_reports;
	stream->data_record_size = xe_eu_stall_data_record_size(gt_to_xe(gt));

	ret = xe_eu_stall_data_buf_alloc(stream, last_xecore);
	if (ret)
		return ret;

	for_each_dss_steering(xecore, gt, group, instance) {
		xecore_buf = &stream->xecore_buf[xecore];
		vaddr_offset = xecore * stream->per_xecore_buf_size;
		xecore_buf->vaddr = stream->bo->vmap.vaddr + vaddr_offset;
	}
	return 0;
}

static __poll_t xe_eu_stall_stream_poll_locked(struct xe_eu_stall_data_stream *stream,
					       struct file *file, poll_table *wait)
{
	__poll_t events = 0;

	poll_wait(file, &stream->poll_wq, wait);

	if (stream->pollin)
		events |= EPOLLIN;

	return events;
}

static __poll_t xe_eu_stall_stream_poll(struct file *file, poll_table *wait)
{
	struct xe_eu_stall_data_stream *stream = file->private_data;
	struct xe_gt *gt = stream->gt;
	__poll_t ret;

	mutex_lock(&gt->eu_stall->stream_lock);
	ret = xe_eu_stall_stream_poll_locked(stream, file, wait);
	mutex_unlock(&gt->eu_stall->stream_lock);

	return ret;
}

static int xe_eu_stall_enable_locked(struct xe_eu_stall_data_stream *stream)
{
	struct xe_gt *gt = stream->gt;
	int ret = 0;

	if (stream->enabled)
		return ret;

	stream->enabled = true;

	ret = xe_eu_stall_stream_enable(stream);

	queue_delayed_work(gt->eu_stall->buf_ptr_poll_wq,
			   &stream->buf_poll_work,
			   msecs_to_jiffies(POLL_PERIOD_MS));
	return ret;
}

static int xe_eu_stall_disable_locked(struct xe_eu_stall_data_stream *stream)
{
	struct xe_gt *gt = stream->gt;

	if (!stream->enabled)
		return 0;

	stream->enabled = false;

	xe_gt_mcr_multicast_write(gt, XEHPC_EUSTALL_BASE, 0);

	cancel_delayed_work_sync(&stream->buf_poll_work);

	if (XE_WA(gt, 22016596838))
		xe_gt_mcr_multicast_write(gt, ROW_CHICKEN2,
					  _MASKED_BIT_DISABLE(DISABLE_DOP_GATING));

	xe_force_wake_put(gt_to_fw(gt), XE_FW_RENDER);
	xe_pm_runtime_put(gt_to_xe(gt));

	return 0;
}

static long xe_eu_stall_stream_ioctl_locked(struct xe_eu_stall_data_stream *stream,
					    unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case DRM_XE_OBSERVATION_IOCTL_ENABLE:
		return xe_eu_stall_enable_locked(stream);
	case DRM_XE_OBSERVATION_IOCTL_DISABLE:
		return xe_eu_stall_disable_locked(stream);
	}

	return -EINVAL;
}

static long xe_eu_stall_stream_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct xe_eu_stall_data_stream *stream = file->private_data;
	struct xe_gt *gt = stream->gt;
	long ret;

	mutex_lock(&gt->eu_stall->stream_lock);
	ret = xe_eu_stall_stream_ioctl_locked(stream, cmd, arg);
	mutex_unlock(&gt->eu_stall->stream_lock);

	return ret;
}

static int xe_eu_stall_stream_close(struct inode *inode, struct file *file)
{
	struct xe_eu_stall_data_stream *stream = file->private_data;
	struct xe_gt *gt = stream->gt;

	drm_dev_put(&gt->tile->xe->drm);

	mutex_lock(&gt->eu_stall->stream_lock);
	xe_eu_stall_disable_locked(stream);
	xe_eu_stall_data_buf_destroy(stream);
	xe_eu_stall_stream_free(stream);
	mutex_unlock(&gt->eu_stall->stream_lock);

	return 0;
}

static const struct file_operations fops_eu_stall = {
	.owner		= THIS_MODULE,
	.llseek		= noop_llseek,
	.release	= xe_eu_stall_stream_close,
	.poll		= xe_eu_stall_stream_poll,
	.read		= xe_eu_stall_stream_read,
	.unlocked_ioctl = xe_eu_stall_stream_ioctl,
	.compat_ioctl   = xe_eu_stall_stream_ioctl,
};

static int xe_eu_stall_stream_open_locked(struct drm_device *dev,
					  struct eu_stall_open_properties *props,
					  struct drm_file *file)
{
	struct xe_eu_stall_data_stream *stream;
	struct xe_gt *gt = props->gt;
	unsigned long f_flags = 0;
	int ret, stream_fd;

	/* Only one session can be active at any time */
	if (gt->eu_stall->stream) {
		xe_gt_dbg(gt, "EU stall sampling session already active\n");
		return -EBUSY;
	}

	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (!stream)
		return -ENOMEM;

	gt->eu_stall->stream = stream;
	stream->gt = gt;

	ret = xe_eu_stall_stream_init(stream, props);
	if (ret) {
		xe_gt_dbg(gt, "EU stall stream init failed : %d\n", ret);
		goto err_free;
	}

	stream_fd = anon_inode_getfd("[xe_eu_stall]", &fops_eu_stall, stream, f_flags);
	if (stream_fd < 0) {
		ret = stream_fd;
		xe_gt_dbg(gt, "EU stall inode get fd failed : %d\n", ret);
		goto err_destroy;
	}

	/* Take a reference on the driver that will be kept with stream_fd
	 * until its release.
	 */
	drm_dev_get(&gt->tile->xe->drm);

	return stream_fd;

err_destroy:
	xe_eu_stall_data_buf_destroy(stream);
err_free:
	xe_eu_stall_stream_free(stream);
	return ret;
}

/**
 * xe_eu_stall_stream_open - Open a xe EU stall data stream fd
 *
 * @dev: DRM device pointer
 * @data: pointer to first struct @drm_xe_ext_set_property in
 *	  the chain of input properties from the user space.
 * @file: DRM file pointer
 *
 * This function opens a EU stall data stream with input properties from
 * the user space.
 *
 * Returns: EU stall data stream fd on success or a negative error code.
 */
int xe_eu_stall_stream_open(struct drm_device *dev, u64 data, struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct eu_stall_open_properties props = {};
	int ret;

	if (!xe_eu_stall_supported_on_platform(xe)) {
		drm_dbg(&xe->drm, "EU stall monitoring is not supported on this platform\n");
		return -ENODEV;
	}

	if (xe_observation_paranoid && !perfmon_capable()) {
		drm_dbg(&xe->drm,  "Insufficient privileges for EU stall monitoring\n");
		return -EACCES;
	}

	/* Initialize and set default values */
	props.wait_num_reports = 1;
	props.sampling_rate_mult = 4;

	ret = xe_eu_stall_user_extensions(xe, data, 0, &props);
	if (ret)
		return ret;

	if (!props.gt) {
		drm_dbg(&xe->drm, "GT ID not provided for EU stall sampling\n");
		return -EINVAL;
	}

	mutex_lock(&props.gt->eu_stall->stream_lock);
	ret = xe_eu_stall_stream_open_locked(dev, &props, file);
	mutex_unlock(&props.gt->eu_stall->stream_lock);

	return ret;
}
