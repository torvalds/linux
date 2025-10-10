/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 ARM Ltd.
 */
#ifndef __ASM_VDSO_PROCESSOR_H
#define __ASM_VDSO_PROCESSOR_H

#ifndef __ASSEMBLER__

static inline void cpu_relax(void)
{
	asm volatile("yield" ::: "memory");
}

#endif /* __ASSEMBLER__ */

#endif /* __ASM_VDSO_PROCESSOR_H */
