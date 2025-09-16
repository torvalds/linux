/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Author: Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#ifndef _ASM_VDSO_VDSO_H
#define _ASM_VDSO_VDSO_H

#ifndef __ASSEMBLER__

#include <asm/asm.h>
#include <asm/page.h>
#include <asm/vdso.h>
#include <vdso/datapage.h>

#define VVAR_SIZE (VDSO_NR_PAGES << PAGE_SHIFT)

#endif /* __ASSEMBLER__ */

#endif
