/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_REG_WHITELIST_
#define _XE_REG_WHITELIST_

struct xe_hw_engine;

void xe_reg_whitelist_process_engine(struct xe_hw_engine *hwe);

#endif
