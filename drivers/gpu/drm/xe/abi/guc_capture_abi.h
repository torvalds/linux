/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _ABI_GUC_CAPTURE_ABI_H
#define _ABI_GUC_CAPTURE_ABI_H

#include <linux/types.h>

/* Capture List Index */
enum guc_capture_list_index_type {
	GUC_CAPTURE_LIST_INDEX_PF = 0,
	GUC_CAPTURE_LIST_INDEX_VF = 1,
};

#define GUC_CAPTURE_LIST_INDEX_MAX	(GUC_CAPTURE_LIST_INDEX_VF + 1)

/* Register-types of GuC capture register lists */
enum guc_state_capture_type {
	GUC_STATE_CAPTURE_TYPE_GLOBAL = 0,
	GUC_STATE_CAPTURE_TYPE_ENGINE_CLASS,
	GUC_STATE_CAPTURE_TYPE_ENGINE_INSTANCE
};

#define GUC_STATE_CAPTURE_TYPE_MAX	(GUC_STATE_CAPTURE_TYPE_ENGINE_INSTANCE + 1)

/* Class indices for capture_class and capture_instance arrays */
enum guc_capture_list_class_type {
	GUC_CAPTURE_LIST_CLASS_RENDER_COMPUTE = 0,
	GUC_CAPTURE_LIST_CLASS_VIDEO = 1,
	GUC_CAPTURE_LIST_CLASS_VIDEOENHANCE = 2,
	GUC_CAPTURE_LIST_CLASS_BLITTER = 3,
	GUC_CAPTURE_LIST_CLASS_GSC_OTHER = 4,
};

#define GUC_CAPTURE_LIST_CLASS_MAX	(GUC_CAPTURE_LIST_CLASS_GSC_OTHER + 1)

/**
 * struct guc_mmio_reg - GuC MMIO reg state struct
 *
 * GuC MMIO reg state struct
 */
struct guc_mmio_reg {
	/** @offset: MMIO Offset - filled in by Host */
	u32 offset;
	/** @value: MMIO Value - Used by Firmware to store value */
	u32 value;
	/** @flags: Flags for accessing the MMIO */
	u32 flags;
	/** @mask: Value of a mask to apply if mask with value is set */
	u32 mask;
#define GUC_REGSET_MASKED		BIT(0)
#define GUC_REGSET_STEERING_NEEDED	BIT(1)
#define GUC_REGSET_MASKED_WITH_VALUE	BIT(2)
#define GUC_REGSET_RESTORE_ONLY		BIT(3)
#define GUC_REGSET_STEERING_GROUP       GENMASK(16, 12)
#define GUC_REGSET_STEERING_INSTANCE    GENMASK(23, 20)
} __packed;

/**
 * struct guc_mmio_reg_set - GuC register sets
 *
 * GuC register sets
 */
struct guc_mmio_reg_set {
	/** @address: register address */
	u32 address;
	/** @count: register count */
	u16 count;
	/** @reserved: reserved */
	u16 reserved;
} __packed;

/**
 * struct guc_debug_capture_list_header - Debug capture list header.
 *
 * Debug capture list header.
 */
struct guc_debug_capture_list_header {
	/** @info: contains number of MMIO descriptors in the capture list. */
	u32 info;
#define GUC_CAPTURELISTHDR_NUMDESCR GENMASK(15, 0)
} __packed;

/**
 * struct guc_debug_capture_list - Debug capture list
 *
 * As part of ADS registration, these header structures (followed by
 * an array of 'struct guc_mmio_reg' entries) are used to register with
 * GuC microkernel the list of registers we want it to dump out prior
 * to a engine reset.
 */
struct guc_debug_capture_list {
	/** @header: Debug capture list header. */
	struct guc_debug_capture_list_header header;
	/** @regs: MMIO descriptors in the capture list. */
	struct guc_mmio_reg regs[];
} __packed;

/**
 * struct guc_state_capture_header_t - State capture header.
 *
 * Prior to resetting engines that have hung or faulted, GuC microkernel
 * reports the engine error-state (register values that was read) by
 * logging them into the shared GuC log buffer using these hierarchy
 * of structures.
 */
struct guc_state_capture_header_t {
	/**
	 * @owner: VFID
	 * BR[ 7: 0] MBZ when SRIOV is disabled. When SRIOV is enabled
	 * VFID is an integer in range [0, 63] where 0 means the state capture
	 * is corresponding to the PF and an integer N in range [1, 63] means
	 * the state capture is for VF N.
	 */
	u32 owner;
#define GUC_STATE_CAPTURE_HEADER_VFID GENMASK(7, 0)
	/** @info: Engine class/instance and capture type info */
	u32 info;
#define GUC_STATE_CAPTURE_HEADER_CAPTURE_TYPE GENMASK(3, 0) /* see guc_state_capture_type */
#define GUC_STATE_CAPTURE_HEADER_ENGINE_CLASS GENMASK(7, 4) /* see guc_capture_list_class_type */
#define GUC_STATE_CAPTURE_HEADER_ENGINE_INSTANCE GENMASK(11, 8)
	/**
	 * @lrca: logical ring context address.
	 * if type-instance, LRCA (address) that hung, else set to ~0
	 */
	u32 lrca;
	/**
	 * @guc_id: context_index.
	 * if type-instance, context index of hung context, else set to ~0
	 */
	u32 guc_id;
	/** @num_mmio_entries: Number of captured MMIO entries. */
	u32 num_mmio_entries;
#define GUC_STATE_CAPTURE_HEADER_NUM_MMIO_ENTRIES GENMASK(9, 0)
} __packed;

/**
 * struct guc_state_capture_t - State capture.
 *
 * State capture
 */
struct guc_state_capture_t {
	/** @header: State capture header. */
	struct guc_state_capture_header_t header;
	/** @mmio_entries: Array of captured guc_mmio_reg entries. */
	struct guc_mmio_reg mmio_entries[];
} __packed;

/* State Capture Group Type */
enum guc_state_capture_group_type {
	GUC_STATE_CAPTURE_GROUP_TYPE_FULL = 0,
	GUC_STATE_CAPTURE_GROUP_TYPE_PARTIAL
};

#define GUC_STATE_CAPTURE_GROUP_TYPE_MAX (GUC_STATE_CAPTURE_GROUP_TYPE_PARTIAL + 1)

/**
 * struct guc_state_capture_group_header_t - State capture group header
 *
 * State capture group header.
 */
struct guc_state_capture_group_header_t {
	/** @owner: VFID */
	u32 owner;
#define GUC_STATE_CAPTURE_GROUP_HEADER_VFID GENMASK(7, 0)
	/** @info: Engine class/instance and capture type info */
	u32 info;
#define GUC_STATE_CAPTURE_GROUP_HEADER_NUM_CAPTURES GENMASK(7, 0)
#define GUC_STATE_CAPTURE_GROUP_HEADER_CAPTURE_GROUP_TYPE GENMASK(15, 8)
} __packed;

/**
 * struct guc_state_capture_group_t - State capture group.
 *
 * this is the top level structure where an error-capture dump starts
 */
struct guc_state_capture_group_t {
	/** @grp_header: State capture group header. */
	struct guc_state_capture_group_header_t grp_header;
	/** @capture_entries: Array of state captures */
	struct guc_state_capture_t capture_entries[];
} __packed;

#endif
