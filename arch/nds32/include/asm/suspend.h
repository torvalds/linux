/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2008-2017 Andes Technology Corporation

#ifndef __ASM_NDS32_SUSPEND_H
#define __ASM_NDS32_SUSPEND_H

extern void suspend2ram(void);
extern void cpu_resume(void);
extern unsigned long wake_mask;

#endif
