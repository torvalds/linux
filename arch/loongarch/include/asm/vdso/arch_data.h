/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Author: Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#ifndef _VDSO_ARCH_DATA_H
#define _VDSO_ARCH_DATA_H

#ifndef __ASSEMBLY__

#include <asm/asm.h>
#include <asm/vdso.h>

struct vdso_pcpu_data {
	u32 node;
} ____cacheline_aligned_in_smp;

struct vdso_arch_data {
	struct vdso_pcpu_data pdata[NR_CPUS];
};

#endif /* __ASSEMBLY__ */

#endif
