// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/anon_inodes.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/types.h>

#include <uapi/drm/xe_drm.h>

#include "xe_device.h"
#include "xe_eu_stall.h"
#include "xe_gt_printk.h"
#include "xe_gt_topology.h"
#include "xe_macros.h"
#include "xe_observation.h"

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

/*
 * Userspace must enable the EU stall stream with DRM_XE_OBSERVATION_IOCTL_ENABLE
 * before calling read().
 */
static ssize_t xe_eu_stall_stream_read(struct file *file, char __user *buf,
				       size_t count, loff_t *ppos)
{
	ssize_t ret = 0;

	return ret;
}

static __poll_t xe_eu_stall_stream_poll(struct file *file, poll_table *wait)
{
	__poll_t ret = 0;

	return ret;
}

static long xe_eu_stall_stream_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return 0;
}

static int xe_eu_stall_stream_close(struct inode *inode, struct file *file)
{
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

static inline bool has_eu_stall_sampling_support(struct xe_device *xe)
{
	return false;
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
	int ret, stream_fd;

	if (!has_eu_stall_sampling_support(xe)) {
		drm_dbg(&xe->drm, "EU stall monitoring is not supported on this platform\n");
		return -ENODEV;
	}

	if (xe_observation_paranoid && !perfmon_capable()) {
		drm_dbg(&xe->drm,  "Insufficient privileges for EU stall monitoring\n");
		return -EACCES;
	}

	ret = xe_eu_stall_user_extensions(xe, data, 0, &props);
	if (ret)
		return ret;

	if (!props.gt) {
		drm_dbg(&xe->drm, "GT ID not provided for EU stall sampling\n");
		return -EINVAL;
	}

	stream_fd = anon_inode_getfd("[xe_eu_stall]", &fops_eu_stall, NULL, 0);
	if (stream_fd < 0)
		xe_gt_dbg(props.gt, "EU stall inode get fd failed : %d\n", stream_fd);

	return stream_fd;
}
