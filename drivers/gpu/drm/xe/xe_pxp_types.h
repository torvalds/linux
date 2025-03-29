/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2024, Intel Corporation. All rights reserved.
 */

#ifndef __XE_PXP_TYPES_H__
#define __XE_PXP_TYPES_H__

#include <linux/completion.h>
#include <linux/iosys-map.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/workqueue.h>

struct xe_bo;
struct xe_exec_queue;
struct xe_device;
struct xe_gt;
struct xe_vm;

enum xe_pxp_status {
	XE_PXP_ERROR = -1,
	XE_PXP_NEEDS_TERMINATION = 0, /* starting status */
	XE_PXP_NEEDS_ADDITIONAL_TERMINATION,
	XE_PXP_TERMINATION_IN_PROGRESS,
	XE_PXP_READY_TO_START,
	XE_PXP_START_IN_PROGRESS,
	XE_PXP_ACTIVE,
	XE_PXP_SUSPENDED,
};

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

	/** @irq: wrapper for the worker and queue used for PXP irq support */
	struct {
		/** @irq.work: worker that manages irq events. */
		struct work_struct work;
		/** @irq.wq: workqueue on which to queue the irq work. */
		struct workqueue_struct *wq;
		/** @irq.events: pending events, protected with xe->irq.lock. */
		u32 events;
#define PXP_TERMINATION_REQUEST  BIT(0)
#define PXP_TERMINATION_COMPLETE BIT(1)
	} irq;

	/** @mutex: protects the pxp status and the queue list */
	struct mutex mutex;
	/** @status: the current pxp status */
	enum xe_pxp_status status;
	/** @activation: completion struct that tracks pxp start */
	struct completion activation;
	/** @termination: completion struct that tracks terminations */
	struct completion termination;

	/** @queues: management of exec_queues that use PXP */
	struct {
		/** @queues.lock: spinlock protecting the queue management */
		spinlock_t lock;
		/** @queues.list: list of exec_queues that use PXP */
		struct list_head list;
	} queues;

	/**
	 * @key_instance: keep track of the current iteration of the PXP key.
	 * Note that, due to the time needed for PXP termination and re-start
	 * to complete, the minimum time between 2 subsequent increases of this
	 * variable is 50ms, and even that only if there is a continuous attack;
	 * normal behavior is for this to increase much much slower than that.
	 * This means that we don't expect this to ever wrap and don't implement
	 * that case in the code.
	 */
	u32 key_instance;
	/**
	 * @last_suspend_key_instance: value of key_instance at the last
	 * suspend. Used to check if any PXP session has been created between
	 * suspend cycles.
	 */
	u32 last_suspend_key_instance;
};

#endif /* __XE_PXP_TYPES_H__ */
