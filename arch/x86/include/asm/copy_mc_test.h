/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _COPY_MC_TEST_H_
#define _COPY_MC_TEST_H_

#ifndef __ASSEMBLY__
#ifdef CONFIG_COPY_MC_TEST
extern unsigned long copy_mc_test_src;
extern unsigned long copy_mc_test_dst;

static inline void copy_mc_inject_src(void *addr)
{
	if (addr)
		copy_mc_test_src = (unsigned long) addr;
	else
		copy_mc_test_src = ~0UL;
}

static inline void copy_mc_inject_dst(void *addr)
{
	if (addr)
		copy_mc_test_dst = (unsigned long) addr;
	else
		copy_mc_test_dst = ~0UL;
}
#else /* CONFIG_COPY_MC_TEST */
static inline void copy_mc_inject_src(void *addr)
{
}

static inline void copy_mc_inject_dst(void *addr)
{
}
#endif /* CONFIG_COPY_MC_TEST */

#else /* __ASSEMBLY__ */
#include <asm/export.h>

#ifdef CONFIG_COPY_MC_TEST
.macro COPY_MC_TEST_CTL
	.pushsection .data
	.align 8
	.globl copy_mc_test_src
	copy_mc_test_src:
		.quad 0
	EXPORT_SYMBOL_GPL(copy_mc_test_src)
	.globl copy_mc_test_dst
	copy_mc_test_dst:
		.quad 0
	EXPORT_SYMBOL_GPL(copy_mc_test_dst)
	.popsection
.endm

.macro COPY_MC_TEST_SRC reg count target
	leaq \count(\reg), %r9
	cmp copy_mc_test_src, %r9
	ja \target
.endm

.macro COPY_MC_TEST_DST reg count target
	leaq \count(\reg), %r9
	cmp copy_mc_test_dst, %r9
	ja \target
.endm
#else
.macro COPY_MC_TEST_CTL
.endm

.macro COPY_MC_TEST_SRC reg count target
.endm

.macro COPY_MC_TEST_DST reg count target
.endm
#endif /* CONFIG_COPY_MC_TEST */
#endif /* __ASSEMBLY__ */
#endif /* _COPY_MC_TEST_H_ */
