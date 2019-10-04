/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * CPU reset routines
 *
 * Copyright (C) 2015 Huawei Futurewei Technologies.
 */

#ifndef _ARM64_CPU_RESET_H
#define _ARM64_CPU_RESET_H

#include <asm/virt.h>

void __cpu_soft_restart(unsigned long el2_switch, unsigned long entry,
	unsigned long arg0, unsigned long arg1, unsigned long arg2);

static inline void __noreturn __nocfi cpu_soft_restart(unsigned long entry,
						       unsigned long arg0,
						       unsigned long arg1,
						       unsigned long arg2)
{
	typeof(__cpu_soft_restart) *restart;

	unsigned long el2_switch = !is_kernel_in_hyp_mode() &&
		is_hyp_mode_available();
	restart = (void *)__pa_function(__cpu_soft_restart);

	cpu_install_idmap();
	restart(el2_switch, entry, arg0, arg1, arg2);
	unreachable();
}

#endif
