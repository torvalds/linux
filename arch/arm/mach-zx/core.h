/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2014 Linaro Ltd.
 * Copyright (C) 2014 ZTE Corporation.
 */

#ifndef __MACH_ZX_CORE_H
#define __MACH_ZX_CORE_H

extern void zx_resume_jump(void);
extern size_t zx_suspend_iram_sz;
extern unsigned long zx_secondary_startup_pa;

void zx_secondary_startup(void);

#endif /* __MACH_ZX_CORE_H */
