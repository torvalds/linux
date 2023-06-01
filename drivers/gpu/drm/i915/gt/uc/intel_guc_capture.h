/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021-2021 Intel Corporation
 */

#ifndef _INTEL_GUC_CAPTURE_H
#define _INTEL_GUC_CAPTURE_H

#include <linux/types.h>

struct drm_i915_error_state_buf;
struct guc_gt_system_info;
struct intel_engine_coredump;
struct intel_engine_cs;
struct intel_context;
struct intel_gt;
struct intel_guc;

void intel_guc_capture_free_node(struct intel_engine_coredump *ee);
int intel_guc_capture_print_engine_node(struct drm_i915_error_state_buf *m,
					const struct intel_engine_coredump *ee);
void intel_guc_capture_get_matching_node(struct intel_gt *gt, struct intel_engine_coredump *ee,
					 struct intel_context *ce);
bool intel_guc_capture_is_matching_engine(struct intel_gt *gt, struct intel_context *ce,
					  struct intel_engine_cs *engine);
void intel_guc_capture_process(struct intel_guc *guc);
int intel_guc_capture_getlist(struct intel_guc *guc, u32 owner, u32 type, u32 classid,
			      void **outptr);
int intel_guc_capture_getlistsize(struct intel_guc *guc, u32 owner, u32 type, u32 classid,
				  size_t *size);
int intel_guc_capture_getnullheader(struct intel_guc *guc, void **outptr, size_t *size);
void intel_guc_capture_destroy(struct intel_guc *guc);
int intel_guc_capture_init(struct intel_guc *guc);

#endif /* _INTEL_GUC_CAPTURE_H */
