/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2024, Intel Corporation. All rights reserved.
 */

#ifndef __XE_PXP_TYPES_H__
#define __XE_PXP_TYPES_H__

#include <linux/iosys-map.h>
#include <linux/types.h>

struct xe_bo;
struct xe_exec_queue;
struct xe_device;
struct xe_gt;
struct xe_vm;

/**
 * struct xe_pxp_gsc_client_resources - resources for GSC submission by a PXP
 * client. The GSC FW supports multiple GSC client active at the same time.
 */
struct xe_pxp_gsc_client_resources {
	/**
	 * @host_session_handle: handle used to identify the client in messages
	 * sent to the GSC firmware.
	 */
	u64 host_session_handle;
	/** @vm: VM used for PXP submissions to the GSCCS */
	struct xe_vm *vm;
	/** @q: GSCCS exec queue for PXP submissions */
	struct xe_exec_queue *q;

	/**
	 * @bo: BO used for submissions to the GSCCS and GSC FW. It includes
	 * space for the GSCCS batch and the input/output buffers read/written
	 * by the FW
	 */
	struct xe_bo *bo;
	/** @inout_size: size of each of the msg_in/out sections individually */
	u32 inout_size;
	/** @batch: iosys_map to the batch memory within the BO */
	struct iosys_map batch;
	/** @msg_in: iosys_map to the input memory within the BO */
	struct iosys_map msg_in;
	/** @msg_out: iosys_map to the output memory within the BO */
	struct iosys_map msg_out;
};

/**
 * struct xe_pxp - pxp state
 */
struct xe_pxp {
	/** @xe: Backpoiner to the xe_device struct */
	struct xe_device *xe;

	/**
	 * @gt: pointer to the gt that owns the submission-side of PXP
	 * (VDBOX, KCR and GSC)
	 */
	struct xe_gt *gt;

	/** @vcs_exec: kernel-owned objects for PXP submissions to the VCS */
	struct {
		/** @vcs_exec.q: kernel-owned VCS exec queue used for PXP terminations */
		struct xe_exec_queue *q;
		/** @vcs_exec.bo: BO used for submissions to the VCS */
		struct xe_bo *bo;
	} vcs_exec;

	/** @gsc_res: kernel-owned objects for PXP submissions to the GSCCS */
	struct xe_pxp_gsc_client_resources gsc_res;
};

#endif /* __XE_PXP_TYPES_H__ */
