/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_VERMAGIC_H
#define _ASM_VERMAGIC_H

#define MODULE_PROC_FAMILY "LOONGARCH "

#ifdef CONFIG_32BIT
#define MODULE_KERNEL_TYPE "32BIT "
#elif defined CONFIG_64BIT
#define MODULE_KERNEL_TYPE "64BIT "
#endif

#define MODULE_ARCH_VERMAGIC \
	MODULE_PROC_FAMILY MODULE_KERNEL_TYPE

#endif /* _ASM_VERMAGIC_H */
