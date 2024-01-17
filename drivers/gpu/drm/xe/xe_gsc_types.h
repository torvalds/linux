/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_GSC_TYPES_H_
#define _XE_GSC_TYPES_H_

#include <linux/iosys-map.h>
#include <linux/mutex.h>
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

	/** @proxy: sub-structure containing the SW proxy-related variables */
	struct {
		/** @component: struct for communication with mei component */
		struct i915_gsc_proxy_component *component;
		/** @mutex: protects the component binding and usage */
		struct mutex mutex;
		/** @component_added: whether the component has been added */
		bool component_added;
		/** @bo: object to store message to and from the GSC */
		struct xe_bo *bo;
		/** @to_gsc: map of the memory used to send messages to the GSC */
		struct iosys_map to_gsc;
		/** @from_gsc: map of the memory used to recv messages from the GSC */
		struct iosys_map from_gsc;
		/** @to_csme: pointer to the memory used to send messages to CSME */
		void *to_csme;
		/** @from_csme: pointer to the memory used to recv messages from CSME */
		void *from_csme;
	} proxy;
};

#endif
