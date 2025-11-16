/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_GUC_PAGEFAULT_H_
#define _XE_GUC_PAGEFAULT_H_

#include <linux/types.h>

struct xe_guc;

int xe_guc_pagefault_handler(struct xe_guc *guc, u32 *msg, u32 len);

#endif
