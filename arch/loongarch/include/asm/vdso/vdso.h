/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Author: Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#ifndef _ASM_VDSO_VDSO_H
#define _ASM_VDSO_VDSO_H

#ifndef __ASSEMBLY__

#include <asm/asm.h>
#include <asm/page.h>
#include <asm/vdso.h>

struct vdso_pcpu_data {
	u32 node;
} ____cacheline_aligned_in_smp;

struct loongarch_vdso_data {
	struct vdso_pcpu_data pdata[NR_CPUS];
	struct vdso_rng_data rng_data;
};

/*
 * The layout of vvar:
 *
 *                      high
 * +---------------------+--------------------------+
 * | loongarch vdso data | LOONGARCH_VDSO_DATA_SIZE |
 * +---------------------+--------------------------+
 * |  time-ns vdso data  |        PAGE_SIZE         |
 * +---------------------+--------------------------+
 * |  generic vdso data  |        PAGE_SIZE         |
 * +---------------------+--------------------------+
 *                      low
 */
#define LOONGARCH_VDSO_DATA_SIZE PAGE_ALIGN(sizeof(struct loongarch_vdso_data))
#define LOONGARCH_VDSO_DATA_PAGES (LOONGARCH_VDSO_DATA_SIZE >> PAGE_SHIFT)

enum vvar_pages {
	VVAR_GENERIC_PAGE_OFFSET,
	VVAR_TIMENS_PAGE_OFFSET,
	VVAR_LOONGARCH_PAGES_START,
	VVAR_LOONGARCH_PAGES_END = VVAR_LOONGARCH_PAGES_START + LOONGARCH_VDSO_DATA_PAGES - 1,
	VVAR_NR_PAGES,
};

#define VVAR_SIZE (VVAR_NR_PAGES << PAGE_SHIFT)

extern struct loongarch_vdso_data _loongarch_data __attribute__((visibility("hidden")));

#endif /* __ASSEMBLY__ */

#endif
