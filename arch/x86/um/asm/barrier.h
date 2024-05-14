/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_UM_BARRIER_H_
#define _ASM_UM_BARRIER_H_

#include <asm/cpufeatures.h>
#include <asm/alternative.h>

/*
 * Force strict CPU ordering.
 * And yes, this is required on UP too when we're talking
 * to devices.
 */
#ifdef CONFIG_X86_32

#define mb()	alternative("lock; addl $0,0(%%esp)", "mfence", X86_FEATURE_XMM2)
#define rmb()	alternative("lock; addl $0,0(%%esp)", "lfence", X86_FEATURE_XMM2)
#define wmb()	alternative("lock; addl $0,0(%%esp)", "sfence", X86_FEATURE_XMM)

#else /* CONFIG_X86_32 */

#define mb()	asm volatile("mfence" : : : "memory")
#define rmb()	asm volatile("lfence" : : : "memory")
#define wmb()	asm volatile("sfence" : : : "memory")

#endif /* CONFIG_X86_32 */

#include <asm-generic/barrier.h>

#endif
