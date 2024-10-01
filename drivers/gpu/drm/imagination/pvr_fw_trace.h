/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_FW_TRACE_H
#define PVR_FW_TRACE_H

#include <drm/drm_file.h>
#include <linux/types.h>

#include "pvr_rogue_fwif.h"

/* Forward declaration from pvr_device.h. */
struct pvr_device;

/* Forward declaration from pvr_gem.h. */
struct pvr_fw_object;

/* Forward declarations from pvr_rogue_fwif.h */
struct rogue_fwif_tracebuf;
struct rogue_fwif_tracebuf_space;

/**
 * struct pvr_fw_trace_buffer - Structure representing a trace buffer
 */
struct pvr_fw_trace_buffer {
	/** @buf_obj: FW buffer object representing trace buffer. */
	struct pvr_fw_object *buf_obj;

	/** @buf: Pointer to CPU mapping of trace buffer. */
	u32 *buf;

	/**
	 * @tracebuf_space: Pointer to FW tracebuf_space structure for this
	 *                  trace buffer.
	 */
	struct rogue_fwif_tracebuf_space *tracebuf_space;
};

/**
 * struct pvr_fw_trace - Device firmware trace data
 */
struct pvr_fw_trace {
	/**
	 * @tracebuf_ctrl_obj: Object representing FW trace buffer control
	 *                     structure.
	 */
	struct pvr_fw_object *tracebuf_ctrl_obj;

	/**
	 * @tracebuf_ctrl: Pointer to CPU mapping of FW trace buffer control
	 *                 structure.
	 */
	struct rogue_fwif_tracebuf *tracebuf_ctrl;

	/**
	 * @buffers: Array representing the actual trace buffers owned by this
	 *           device.
	 */
	struct pvr_fw_trace_buffer buffers[ROGUE_FW_THREAD_MAX];

	/** @group_mask: Mask of enabled trace groups. */
	u32 group_mask;
};

int pvr_fw_trace_init(struct pvr_device *pvr_dev);
void pvr_fw_trace_fini(struct pvr_device *pvr_dev);

#if defined(CONFIG_DEBUG_FS)
/* Forward declaration from <linux/dcache.h>. */
struct dentry;

void pvr_fw_trace_mask_update(struct pvr_device *pvr_dev, u32 old_mask,
			      u32 new_mask);

void pvr_fw_trace_debugfs_init(struct pvr_device *pvr_dev, struct dentry *dir);
#endif /* defined(CONFIG_DEBUG_FS) */

#endif /* PVR_FW_TRACE_H */
