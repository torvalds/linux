/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_SRIOV_VF_CCS_TYPES_H_
#define _XE_SRIOV_VF_CCS_TYPES_H_

#include <linux/types.h>

#define for_each_ccs_rw_ctx(id__) \
	for ((id__) = 0; (id__) < XE_SRIOV_VF_CCS_CTX_COUNT; (id__)++)

enum xe_sriov_vf_ccs_rw_ctxs {
	XE_SRIOV_VF_CCS_READ_CTX,
	XE_SRIOV_VF_CCS_WRITE_CTX,
	XE_SRIOV_VF_CCS_CTX_COUNT
};

struct xe_migrate;
struct xe_sa_manager;

/**
 * struct xe_sriov_vf_ccs_ctx - VF CCS migration context data.
 */
struct xe_sriov_vf_ccs_ctx {
	/** @ctx_id: Id to which context it belongs to */
	enum xe_sriov_vf_ccs_rw_ctxs ctx_id;

	/** @mig_q: exec queues used for migration */
	struct xe_exec_queue *mig_q;

	/** @mem: memory data */
	struct {
		/** @mem.ccs_bb_pool: Pool from which batch buffers are allocated. */
		struct xe_sa_manager *ccs_bb_pool;
	} mem;
};

/**
 * struct xe_sriov_vf_ccs - The VF CCS migration support data.
 */
struct xe_sriov_vf_ccs {
	/** @contexts: CCS read and write contexts for VF. */
	struct xe_sriov_vf_ccs_ctx contexts[XE_SRIOV_VF_CCS_CTX_COUNT];

	/** @initialized: Initialization of VF CCS is completed or not. */
	bool initialized;
};

#endif
