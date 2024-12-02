/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2020 Linaro Ltd. */

#ifndef __VENUS_DBGFS_H__
#define __VENUS_DBGFS_H__

#include <linux/fault-inject.h>

struct venus_core;

#ifdef CONFIG_FAULT_INJECTION
extern struct fault_attr venus_ssr_attr;
static inline bool venus_fault_inject_ssr(void)
{
	return should_fail(&venus_ssr_attr, 1);
}
#else
static inline bool venus_fault_inject_ssr(void) { return false; }
#endif


void venus_dbgfs_init(struct venus_core *core);
void venus_dbgfs_deinit(struct venus_core *core);

#endif
