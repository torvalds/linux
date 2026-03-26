/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Author: Huacai Chen <chenhuacai@loongson.cn>
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#include <asm/vdso/vdso.h>

#include <linux/linkage.h>
#include <uapi/asm/unistd.h>

#include <asm/regdef.h>
#include <asm/asm.h>
#include <asm/asm-offsets.h>

	.section	.text

SYM_SIGFUNC_START(__vdso_rt_sigreturn)

	li.w	a7, __NR_rt_sigreturn
	syscall	0

SYM_SIGFUNC_END(__vdso_rt_sigreturn)
