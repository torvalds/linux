/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_HUC_H_
#define _XE_HUC_H_

#include <linux/types.h>

struct drm_printer;
struct xe_huc;

enum xe_huc_auth_types {
	XE_HUC_AUTH_VIA_GUC = 0,
	XE_HUC_AUTH_VIA_GSC,
	XE_HUC_AUTH_TYPES_COUNT
};

int xe_huc_init(struct xe_huc *huc);
int xe_huc_init_post_hwconfig(struct xe_huc *huc);
int xe_huc_upload(struct xe_huc *huc);
int xe_huc_auth(struct xe_huc *huc, enum xe_huc_auth_types type);
bool xe_huc_is_authenticated(struct xe_huc *huc, enum xe_huc_auth_types type);
void xe_huc_sanitize(struct xe_huc *huc);
void xe_huc_print_info(struct xe_huc *huc, struct drm_printer *p);

#endif
