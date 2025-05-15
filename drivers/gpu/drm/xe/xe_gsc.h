/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_GSC_H_
#define _XE_GSC_H_

#include <linux/types.h>

struct drm_printer;
struct xe_gsc;
struct xe_gt;
struct xe_hw_engine;

int xe_gsc_init(struct xe_gsc *gsc);
int xe_gsc_init_post_hwconfig(struct xe_gsc *gsc);
void xe_gsc_wait_for_worker_completion(struct xe_gsc *gsc);
void xe_gsc_stop_prepare(struct xe_gsc *gsc);
void xe_gsc_load_start(struct xe_gsc *gsc);
void xe_gsc_hwe_irq_handler(struct xe_hw_engine *hwe, u16 intr_vec);

void xe_gsc_wa_14015076503(struct xe_gt *gt, bool prep);

void xe_gsc_print_info(struct xe_gsc *gsc, struct drm_printer *p);

#endif
