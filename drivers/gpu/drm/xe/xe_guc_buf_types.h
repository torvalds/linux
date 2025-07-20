/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _XE_GUC_BUF_TYPES_H_
#define _XE_GUC_BUF_TYPES_H_

struct drm_suballoc;
struct xe_sa_manager;

/**
 * struct xe_guc_buf_cache - GuC Data Buffer Cache.
 */
struct xe_guc_buf_cache {
	/* private: internal sub-allocation manager */
	struct xe_sa_manager *sam;
};

/**
 * struct xe_guc_buf - GuC Data Buffer Reference.
 */
struct xe_guc_buf {
	/* private: internal sub-allocation reference */
	struct drm_suballoc *sa;
};

#endif
