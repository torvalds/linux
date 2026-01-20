/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_PAGEFAULT_H_
#define _XE_PAGEFAULT_H_

struct xe_device;
struct xe_gt;
struct xe_pagefault;

int xe_pagefault_init(struct xe_device *xe);

void xe_pagefault_reset(struct xe_device *xe, struct xe_gt *gt);

int xe_pagefault_handler(struct xe_device *xe, struct xe_pagefault *pf);

#endif
