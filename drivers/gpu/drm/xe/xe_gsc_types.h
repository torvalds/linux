/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_GSC_TYPES_H_
#define _XE_GSC_TYPES_H_

#include <linux/iosys-map.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "xe_uc_fw_types.h"

struct xe_bo;
struct xe_exec_queue;
struct i915_gsc_proxy_component;

/**
 * struct xe_gsc - GSC
 */
struct xe_gsc {
	/** @fw: Generic uC firmware management */
	struct xe_uc_fw fw;

	/** @security_version: SVN found in the fetched blob */
	u32 security_version;

	/** @private: Private data for use by the GSC FW */
	struct xe_bo *private;

	/** @q: Default queue used for submissions to GSC FW */
	struct xe_exec_queue *q;

	/** @wq: workqueue to handle jobs for delayed load and proxy handling */
	struct workqueue_struct *wq;

	/** @work: delayed load and proxy handling work */
	struct work_struct work;

	/** @lock: protects access to the work_actions mask */
	spinlock_t lock;

	/** @work_actions: mask of actions to be performed in the work */
	u32 work_actions;
#define GSC_ACTION_FW_LOAD BIT(0)
#define GSC_ACTION_SW_PROXY BIT(1)
#define GSC_ACTION_ER_COMPLETE BIT(2)

	/** @proxy: sub-structure containing the SW proxy-related variables */
	struct {
		/** @proxy.component: struct for communication with mei component */
		struct i915_gsc_proxy_component *component;
		/** @proxy.mutex: protects the component binding and usage */
		struct mutex mutex;
		/** @proxy.component_added: whether the component has been added */
		bool component_added;
		/** @proxy.bo: object to store message to and from the GSC */
		struct xe_bo *bo;
		/** @proxy.to_gsc: map of the memory used to send messages to the GSC */
		struct iosys_map to_gsc;
		/** @proxy.from_gsc: map of the memory used to recv messages from the GSC */
		struct iosys_map from_gsc;
		/** @proxy.to_csme: pointer to the memory used to send messages to CSME */
		void *to_csme;
		/** @proxy.from_csme: pointer to the memory used to recv messages from CSME */
		void *from_csme;
	} proxy;
};

#endif
