/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021-2021 Intel Corporation
 */

#ifndef _INTEL_GUC_CAPTURE_H
#define _INTEL_GUC_CAPTURE_H

#include <linux/types.h>

struct guc_gt_system_info;
struct intel_guc;

int intel_guc_capture_output_min_size_est(struct intel_guc *guc);
int intel_guc_capture_getlist(struct intel_guc *guc, u32 owner, u32 type, u32 classid,
			      void **outptr);
int intel_guc_capture_getlistsize(struct intel_guc *guc, u32 owner, u32 type, u32 classid,
				  size_t *size);
int intel_guc_capture_getnullheader(struct intel_guc *guc, void **outptr, size_t *size);
void intel_guc_capture_destroy(struct intel_guc *guc);
int intel_guc_capture_init(struct intel_guc *guc);

#endif /* _INTEL_GUC_CAPTURE_H */
