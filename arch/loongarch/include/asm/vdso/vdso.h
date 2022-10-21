/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Author: Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#ifndef __ASSEMBLY__

#include <asm/asm.h>
#include <asm/page.h>
#include <asm/vdso.h>

struct vdso_pcpu_data {
	u32 node;
} ____cacheline_aligned_in_smp;

struct loongarch_vdso_data {
	struct vdso_pcpu_data pdata[NR_CPUS];
	struct vdso_data data[CS_BASES]; /* Arch-independent data */
};

#define VDSO_DATA_SIZE PAGE_ALIGN(sizeof(struct loongarch_vdso_data))

static inline unsigned long get_vdso_base(void)
{
	unsigned long addr;

	__asm__(
	" la.pcrel %0, _start\n"
	: "=r" (addr)
	:
	:);

	return addr;
}

static inline const struct vdso_data *get_vdso_data(void)
{
	return (const struct vdso_data *)(get_vdso_base()
			- VDSO_DATA_SIZE + SMP_CACHE_BYTES * NR_CPUS);
}

#endif /* __ASSEMBLY__ */
