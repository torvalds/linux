/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef __ASM_MMU_H
#define __ASM_MMU_H

#include <linux/atomic.h>
#include <linux/spinlock.h>

typedef struct {
	u64 asid[NR_CPUS];
	void *vdso;
} mm_context_t;

#endif /* __ASM_MMU_H */
