/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef __NDS32_ASM_BARRIER_H
#define __NDS32_ASM_BARRIER_H

#ifndef __ASSEMBLY__
#define mb()		asm volatile("msync all":::"memory")
#define rmb()		asm volatile("msync all":::"memory")
#define wmb()		asm volatile("msync store":::"memory")
#include <asm-generic/barrier.h>

#endif	/* __ASSEMBLY__ */

#endif	/* __NDS32_ASM_BARRIER_H */
