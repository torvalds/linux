/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_GSC_H_
#define _XE_GSC_H_

#include "xe_gsc_types.h"

int xe_gsc_init(struct xe_gsc *gsc);
int xe_gsc_init_post_hwconfig(struct xe_gsc *gsc);
void xe_gsc_wait_for_worker_completion(struct xe_gsc *gsc);
void xe_gsc_load_start(struct xe_gsc *gsc);

#endif
