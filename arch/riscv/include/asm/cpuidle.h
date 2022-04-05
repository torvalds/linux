/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Allwinner Ltd
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 */

#ifndef _ASM_RISCV_CPUIDLE_H
#define _ASM_RISCV_CPUIDLE_H

#include <asm/barrier.h>
#include <asm/processor.h>

static inline void cpu_do_idle(void)
{
	/*
	 * Add mb() here to ensure that all
	 * IO/MEM accesses are completed prior
	 * to entering WFI.
	 */
	mb();
	wait_for_interrupt();
}

#endif
