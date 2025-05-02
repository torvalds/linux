/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_GUC_TYPES_H_
#define _XE_GUC_TYPES_H_

#include <linux/idr.h>
#include <linux/xarray.h>

#include "regs/xe_reg_defs.h"
#include "xe_guc_ads_types.h"
#include "xe_guc_buf_types.h"
#include "xe_guc_ct_types.h"
#include "xe_guc_engine_activity_types.h"
#include "xe_guc_fwif.h"
#include "xe_guc_log_types.h"
#include "xe_guc_pc_types.h"
#include "xe_guc_relay_types.h"
#include "xe_uc_fw_types.h"

/**
 * struct xe_guc_db_mgr - GuC Doorbells Manager.
 *
 * Note: GuC Doorbells Manager is relying on &xe_guc::submission_state.lock
 * to protect its members.
 */
struct xe_guc_db_mgr {
	/** @count: number of doorbells to manage */
	unsigned int count;
	/** @bitmap: bitmap to track allocated doorbells */
	unsigned long *bitmap;
};

/**
 * struct xe_guc_id_mgr - GuC context ID Manager.
 *
 * Note: GuC context ID Manager is relying on &xe_guc::submission_state.lock
 * to protect its members.
 */
struct xe_guc_id_mgr {
	/** @bitmap: bitmap to track allocated IDs */
	unsigned long *bitmap;
	/** @total: total number of IDs being managed */
	unsigned int total;
	/** @used: number of IDs currently in use */
	unsigned int used;
};

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
	/** @buf: GuC Buffer Cache manager */
	struct xe_guc_buf_cache buf;
	/** @capture: the error-state-capture module's data and objects */
	struct xe_guc_state_capture *capture;
	/** @pc: GuC Power Conservation */
	struct xe_guc_pc pc;
	/** @dbm: GuC Doorbell Manager */
	struct xe_guc_db_mgr dbm;

	/** @g2g: GuC to GuC communication state */
	struct {
		/** @g2g.bo: Storage for GuC to GuC communication channels */
		struct xe_bo *bo;
		/** @g2g.owned: Is the BO owned by this GT or just mapped in */
		bool owned;
	} g2g;

	/** @submission_state: GuC submission state */
	struct {
		/** @submission_state.idm: GuC context ID Manager */
		struct xe_guc_id_mgr idm;
		/** @submission_state.exec_queue_lookup: Lookup an xe_engine from guc_id */
		struct xarray exec_queue_lookup;
		/** @submission_state.stopped: submissions are stopped */
		atomic_t stopped;
		/** @submission_state.lock: protects submission state */
		struct mutex lock;
		/** @submission_state.enabled: submission is enabled */
		bool enabled;
		/** @submission_state.fini_wq: submit fini wait queue */
		wait_queue_head_t fini_wq;
	} submission_state;

	/** @hwconfig: Hardware config state */
	struct {
		/** @hwconfig.bo: buffer object of the hardware config */
		struct xe_bo *bo;
		/** @hwconfig.size: size of the hardware config */
		u32 size;
	} hwconfig;

	/** @relay: GuC Relay Communication used in SR-IOV */
	struct xe_guc_relay relay;

	/** @engine_activity: Device specific engine activity */
	struct xe_guc_engine_activity engine_activity;

	/**
	 * @notify_reg: Register which is written to notify GuC of H2G messages
	 */
	struct xe_reg notify_reg;
	/** @params: Control params for fw initialization */
	u32 params[GUC_CTL_MAX_DWORDS];
};

#endif
