/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#include <linux/init.h>

/* Module modprobe variables */
extern bool enable_guc;
extern bool enable_display;
extern u32 xe_force_lmem_bar_size;
extern int xe_guc_log_level;
extern char *xe_param_force_probe;
