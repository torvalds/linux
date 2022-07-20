/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Author: Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#ifndef __ASSEMBLY__

#include <asm/asm.h>
#include <asm/page.h>

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
	return (const struct vdso_data *)(get_vdso_base() - PAGE_SIZE);
}

#endif /* __ASSEMBLY__ */
