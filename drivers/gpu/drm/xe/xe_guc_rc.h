/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2026 Intel Corporation
 */

#ifndef _XE_GUC_RC_H_
#define _XE_GUC_RC_H_

struct xe_guc;
enum slpc_gucrc_mode;

int xe_guc_rc_init(struct xe_guc *guc);
int xe_guc_rc_enable(struct xe_guc *guc);
void xe_guc_rc_disable(struct xe_guc *guc);

#endif
