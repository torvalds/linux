/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MCSAFE_TEST_H_
#define _MCSAFE_TEST_H_

#ifndef __ASSEMBLY__
#ifdef CONFIG_MCSAFE_TEST
extern unsigned long mcsafe_test_src;
extern unsigned long mcsafe_test_dst;

static inline void mcsafe_inject_src(void *addr)
{
	if (addr)
		mcsafe_test_src = (unsigned long) addr;
	else
		mcsafe_test_src = ~0UL;
}

static inline void mcsafe_inject_dst(void *addr)
{
	if (addr)
		mcsafe_test_dst = (unsigned long) addr;
	else
		mcsafe_test_dst = ~0UL;
}
#else /* CONFIG_MCSAFE_TEST */
static inline void mcsafe_inject_src(void *addr)
{
}

static inline void mcsafe_inject_dst(void *addr)
{
}
#endif /* CONFIG_MCSAFE_TEST */

#else /* __ASSEMBLY__ */
#include <asm/export.h>

#ifdef CONFIG_MCSAFE_TEST
.macro MCSAFE_TEST_CTL
	.pushsection .data
	.align 8
	.globl mcsafe_test_src
	mcsafe_test_src:
		.quad 0
	EXPORT_SYMBOL_GPL(mcsafe_test_src)
	.globl mcsafe_test_dst
	mcsafe_test_dst:
		.quad 0
	EXPORT_SYMBOL_GPL(mcsafe_test_dst)
	.popsection
.endm

.macro MCSAFE_TEST_SRC reg count target
	leaq \count(\reg), %r9
	cmp mcsafe_test_src, %r9
	ja \target
.endm

.macro MCSAFE_TEST_DST reg count target
	leaq \count(\reg), %r9
	cmp mcsafe_test_dst, %r9
	ja \target
.endm
#else
.macro MCSAFE_TEST_CTL
.endm

.macro MCSAFE_TEST_SRC reg count target
.endm

.macro MCSAFE_TEST_DST reg count target
.endm
#endif /* CONFIG_MCSAFE_TEST */
#endif /* __ASSEMBLY__ */
#endif /* _MCSAFE_TEST_H_ */
