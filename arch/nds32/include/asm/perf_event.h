/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2008-2018 Andes Technology Corporation */

#ifndef __ASM_PERF_EVENT_H
#define __ASM_PERF_EVENT_H

/*
 * This file is request by Perf,
 * please refer to tools/perf/design.txt for more details
 */
struct pt_regs;
unsigned long perf_instruction_pointer(struct pt_regs *regs);
unsigned long perf_misc_flags(struct pt_regs *regs);
#define perf_misc_flags(regs)   perf_misc_flags(regs)

#endif
