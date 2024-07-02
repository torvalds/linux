// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#include "pvr_params.h"

#include <linux/cache.h>
#include <linux/moduleparam.h>

static struct pvr_device_params pvr_device_param_defaults __read_mostly = {
#define X(type_, name_, value_, desc_, ...) .name_ = (value_),
	PVR_DEVICE_PARAMS
#undef X
};

#define PVR_DEVICE_PARAM_NAMED(name_, type_, desc_) \
	module_param_named(name_, pvr_device_param_defaults.name_, type_, \
			   0400);                                         \
	MODULE_PARM_DESC(name_, desc_);

/*
 * This list of defines must contain every type specified in "pvr_params.h" as
 * ``PVR_PARAM_TYPE_*_C``.
 */
#define PVR_PARAM_TYPE_X32_MODPARAM uint

#define X(type_, name_, value_, desc_, ...) \
	PVR_DEVICE_PARAM_NAMED(name_, PVR_PARAM_TYPE_##type_##_MODPARAM, desc_);
PVR_DEVICE_PARAMS
#undef X

int
pvr_device_params_init(struct pvr_device_params *params)
{
	/*
	 * If heap-allocated parameters are added in the future (e.g.
	 * modparam's charp type), they must be handled specially here (via
	 * kstrdup() in the case of charp). Since that's not necessary yet,
	 * a straight copy will do for now. This change will also require a
	 * pvr_device_params_fini() function to free any heap-allocated copies.
	 */

	*params = pvr_device_param_defaults;

	return 0;
}

#if defined(CONFIG_DEBUG_FS)
#include "pvr_device.h"

#include <linux/dcache.h>
#include <linux/debugfs.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/stddef.h>

/*
 * This list of defines must contain every type specified in "pvr_params.h" as
 * ``PVR_PARAM_TYPE_*_C``.
 */
#define PVR_PARAM_TYPE_X32_FMT "0x%08llx"

#define X_SET(name_, mode_) X_SET_##mode_(name_)
#define X_SET_DEF(name_, update_, mode_) X_SET_DEF_##mode_(name_, update_)

#define X_SET_RO(name_) NULL
#define X_SET_RW(name_) __pvr_device_param_##name_##set

#define X_SET_DEF_RO(name_, update_)
#define X_SET_DEF_RW(name_, update_)                                    \
	static int                                                      \
	X_SET_RW(name_)(void *data, u64 val)                            \
	{                                                               \
		struct pvr_device *pvr_dev = data;                      \
		/* This is not just (update_) to suppress -Waddress. */ \
		if ((void *)(update_) != NULL)                          \
			(update_)(pvr_dev, pvr_dev->params.name_, val); \
		pvr_dev->params.name_ = val;                            \
		return 0;                                               \
	}

#define X(type_, name_, value_, desc_, mode_, update_)                     \
	static int                                                         \
	__pvr_device_param_##name_##_get(void *data, u64 *val)             \
	{                                                                  \
		struct pvr_device *pvr_dev = data;                         \
		*val = pvr_dev->params.name_;                              \
		return 0;                                                  \
	}                                                                  \
	X_SET_DEF(name_, update_, mode_)                                   \
	static int                                                         \
	__pvr_device_param_##name_##_open(struct inode *inode,             \
					  struct file *file)               \
	{                                                                  \
		__simple_attr_check_format(PVR_PARAM_TYPE_##type_##_FMT,   \
					   0ull);                          \
		return simple_attr_open(inode, file,                       \
					__pvr_device_param_##name_##_get,  \
					X_SET(name_, mode_),               \
					PVR_PARAM_TYPE_##type_##_FMT);     \
	}
PVR_DEVICE_PARAMS
#undef X

#undef X_SET
#undef X_SET_RO
#undef X_SET_RW
#undef X_SET_DEF
#undef X_SET_DEF_RO
#undef X_SET_DEF_RW

static struct {
#define X(type_, name_, value_, desc_, mode_, update_) \
	const struct file_operations name_;
	PVR_DEVICE_PARAMS
#undef X
} pvr_device_param_debugfs_fops = {
#define X(type_, name_, value_, desc_, mode_, update_)     \
	.name_ = {                                         \
		.owner = THIS_MODULE,                      \
		.open = __pvr_device_param_##name_##_open, \
		.release = simple_attr_release,            \
		.read = simple_attr_read,                  \
		.write = simple_attr_write,                \
		.llseek = generic_file_llseek,             \
	},
	PVR_DEVICE_PARAMS
#undef X
};

void
pvr_params_debugfs_init(struct pvr_device *pvr_dev, struct dentry *dir)
{
#define X_MODE(mode_) X_MODE_##mode_
#define X_MODE_RO 0400
#define X_MODE_RW 0600

#define X(type_, name_, value_, desc_, mode_, update_)             \
	debugfs_create_file(#name_, X_MODE(mode_), dir, pvr_dev,   \
			    &pvr_device_param_debugfs_fops.name_);
	PVR_DEVICE_PARAMS
#undef X

#undef X_MODE
#undef X_MODE_RO
#undef X_MODE_RW
}
#endif
