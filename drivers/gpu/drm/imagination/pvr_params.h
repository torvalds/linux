/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_PARAMS_H
#define PVR_PARAMS_H

#include "pvr_rogue_fwif.h"

#include <linux/cache.h>
#include <linux/compiler_attributes.h>

/*
 * This is the definitive list of types allowed in the definition of
 * %PVR_DEVICE_PARAMS.
 */
#define PVR_PARAM_TYPE_X32_C u32

/*
 * This macro defines all device-specific parameters; that is parameters which
 * are set independently per device.
 *
 * The X-macro accepts the following arguments. Arguments marked with [debugfs]
 * are ignored when debugfs is disabled; values used for these arguments may
 * safely be gated behind CONFIG_DEBUG_FS.
 *
 * @type_: The definitive list of allowed values is PVR_PARAM_TYPE_*_C.
 * @name_: Name of the parameter. This is used both as the field name in C and
 *         stringified as the parameter name.
 * @value_: Initial/default value.
 * @desc_: String literal used as help text to describe the usage of this
 *         parameter.
 * @mode_: [debugfs] One of {RO,RW}. The access mode of the debugfs entry for
 *         this parameter.
 * @update_: [debugfs] When debugfs support is enabled, parameters may be
 *           updated at runtime. When this happens, this function will be
 *           called to allow changes to propagate. The signature of this
 *           function is:
 *
 *              void (*)(struct pvr_device *pvr_dev, T old_val, T new_val)
 *
 *           Where T is the C type associated with @type_.
 *
 *           If @mode_ does not allow write access, this function will never be
 *           called. In this case, or if no update callback is required, you
 *           should specify NULL for this argument.
 */
#define PVR_DEVICE_PARAMS                                                    \
	X(X32, fw_trace_mask, ROGUE_FWIF_LOG_TYPE_NONE,                      \
	  "Enable FW trace for the specified groups. Specifying 0 disables " \
	  "all FW tracing.",                                                 \
	  RW, pvr_fw_trace_mask_update)

struct pvr_device_params {
#define X(type_, name_, value_, desc_, ...) \
	PVR_PARAM_TYPE_##type_##_C name_;
	PVR_DEVICE_PARAMS
#undef X
};

int pvr_device_params_init(struct pvr_device_params *params);

#if defined(CONFIG_DEBUG_FS)
/* Forward declaration from "pvr_device.h". */
struct pvr_device;

/* Forward declaration from <linux/dcache.h>. */
struct dentry;

void pvr_params_debugfs_init(struct pvr_device *pvr_dev, struct dentry *dir);
#endif /* defined(CONFIG_DEBUG_FS) */

#endif /* PVR_PARAMS_H */
