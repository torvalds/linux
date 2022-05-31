/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef __ASM_PERCPU_H
#define __ASM_PERCPU_H

/* Use r21 for fast access */
register unsigned long __my_cpu_offset __asm__("$r21");

static inline void set_my_cpu_offset(unsigned long off)
{
	__my_cpu_offset = off;
	csr_write64(off, PERCPU_BASE_KS);
}
#define __my_cpu_offset __my_cpu_offset

#include <asm-generic/percpu.h>

#endif /* __ASM_PERCPU_H */
