/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef __ASM_PREFETCH_H
#define __ASM_PREFETCH_H

#define Pref_Load	0
#define Pref_Store	8

#ifdef __ASSEMBLER__

	.macro	__pref hint addr
#ifdef CONFIG_CPU_HAS_PREFETCH
	preld	\hint, \addr, 0
#endif
	.endm

	.macro	pref_load addr
	__pref	Pref_Load, \addr
	.endm

	.macro	pref_store addr
	__pref	Pref_Store, \addr
	.endm

#endif

#endif /* __ASM_PREFETCH_H */
