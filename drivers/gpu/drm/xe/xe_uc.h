/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_UC_H_
#define _XE_UC_H_

struct xe_uc;

int xe_uc_init(struct xe_uc *uc);
int xe_uc_init_hwconfig(struct xe_uc *uc);
int xe_uc_init_post_hwconfig(struct xe_uc *uc);
int xe_uc_init_hw(struct xe_uc *uc);
int xe_uc_fini_hw(struct xe_uc *uc);
void xe_uc_gucrc_disable(struct xe_uc *uc);
int xe_uc_reset_prepare(struct xe_uc *uc);
void xe_uc_stop_prepare(struct xe_uc *uc);
void xe_uc_stop(struct xe_uc *uc);
int xe_uc_start(struct xe_uc *uc);
int xe_uc_suspend(struct xe_uc *uc);
int xe_uc_sanitize_reset(struct xe_uc *uc);
void xe_uc_declare_wedged(struct xe_uc *uc);

#endif
