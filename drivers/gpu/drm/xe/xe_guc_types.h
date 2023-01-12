/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_GUC_TYPES_H_
#define _XE_GUC_TYPES_H_

#include <linux/idr.h>
#include <linux/xarray.h>

#include "xe_guc_ads_types.h"
#include "xe_guc_ct_types.h"
#include "xe_guc_fwif.h"
#include "xe_guc_log_types.h"
#include "xe_guc_pc_types.h"
#include "xe_uc_fw_types.h"

/**
 * struct xe_guc - Graphic micro controller
 */
struct xe_guc {
	/** @fw: Generic uC firmware management */
	struct xe_uc_fw fw;
	/** @log: GuC log */
	struct xe_guc_log log;
	/** @ads: GuC ads */
	struct xe_guc_ads ads;
	/** @ct: GuC ct */
	struct xe_guc_ct ct;
	/** @pc: GuC Power Conservation */
	struct xe_guc_pc pc;
	/** @submission_state: GuC submission state */
	struct {
		/** @engine_lookup: Lookup an xe_engine from guc_id */
		struct xarray engine_lookup;
		/** @guc_ids: used to allocate new guc_ids, single-lrc */
		struct ida guc_ids;
		/** @guc_ids_bitmap: used to allocate new guc_ids, multi-lrc */
		unsigned long *guc_ids_bitmap;
		/** @stopped: submissions are stopped */
		atomic_t stopped;
		/** @lock: protects submission state */
		struct mutex lock;
		/** @suspend: suspend fence state */
		struct {
			/** @lock: suspend fences lock */
			spinlock_t lock;
			/** @context: suspend fences context */
			u64 context;
			/** @seqno: suspend fences seqno */
			u32 seqno;
		} suspend;
		/** @version: submission version */
		struct {
			/** @major: major version of GuC submission */
			u32 major;
			/** @minor: minor version of GuC submission */
			u32 minor;
			/** @patch: patch version of GuC submission */
			u32 patch;
		} version;
	} submission_state;
	/** @hwconfig: Hardware config state */
	struct {
		/** @bo: buffer object of the hardware config */
		struct xe_bo *bo;
		/** @size: size of the hardware config */
		u32 size;
	} hwconfig;

	/**
	 * @notify_reg: Register which is written to notify GuC of H2G messages
	 */
	u32 notify_reg;
	/** @params: Control params for fw initialization */
	u32 params[GUC_CTL_MAX_DWORDS];
};

#endif
