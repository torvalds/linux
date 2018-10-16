#ifndef __ASM_X86_REFCOUNT_H
#define __ASM_X86_REFCOUNT_H
/*
 * x86-specific implementation of refcount_t. Based on PAX_REFCOUNT from
 * PaX/grsecurity.
 */

#ifdef __ASSEMBLY__

#include <asm/asm.h>
#include <asm/bug.h>

.macro REFCOUNT_EXCEPTION counter:req
	.pushsection .text..refcount
111:	lea \counter, %_ASM_CX
112:	ud2
	ASM_UNREACHABLE
	.popsection
113:	_ASM_EXTABLE_REFCOUNT(112b, 113b)
.endm

/* Trigger refcount exception if refcount result is negative. */
.macro REFCOUNT_CHECK_LT_ZERO counter:req
	js 111f
	REFCOUNT_EXCEPTION counter="\counter"
.endm

/* Trigger refcount exception if refcount result is zero or negative. */
.macro REFCOUNT_CHECK_LE_ZERO counter:req
	jz 111f
	REFCOUNT_CHECK_LT_ZERO counter="\counter"
.endm

/* Trigger refcount exception unconditionally. */
.macro REFCOUNT_ERROR counter:req
	jmp 111f
	REFCOUNT_EXCEPTION counter="\counter"
.endm

#else /* __ASSEMBLY__ */

#include <linux/refcount.h>
#include <asm/bug.h>

/*
 * This is the first portion of the refcount error handling, which lives in
 * .text.unlikely, and is jumped to from the CPU flag check (in the
 * following macros). This saves the refcount value location into CX for
 * the exception handler to use (in mm/extable.c), and then triggers the
 * central refcount exception. The fixup address for the exception points
 * back to the regular execution flow in .text.
 */

static __always_inline void refcount_add(unsigned int i, refcount_t *r)
{
	asm volatile(LOCK_PREFIX "addl %1,%0\n\t"
		"REFCOUNT_CHECK_LT_ZERO counter=\"%[counter]\""
		: [counter] "+m" (r->refs.counter)
		: "ir" (i)
		: "cc", "cx");
}

static __always_inline void refcount_inc(refcount_t *r)
{
	asm volatile(LOCK_PREFIX "incl %0\n\t"
		"REFCOUNT_CHECK_LT_ZERO counter=\"%[counter]\""
		: [counter] "+m" (r->refs.counter)
		: : "cc", "cx");
}

static __always_inline void refcount_dec(refcount_t *r)
{
	asm volatile(LOCK_PREFIX "decl %0\n\t"
		"REFCOUNT_CHECK_LE_ZERO counter=\"%[counter]\""
		: [counter] "+m" (r->refs.counter)
		: : "cc", "cx");
}

static __always_inline __must_check
bool refcount_sub_and_test(unsigned int i, refcount_t *r)
{
	GEN_BINARY_SUFFIXED_RMWcc(LOCK_PREFIX "subl",
				  "REFCOUNT_CHECK_LT_ZERO counter=\"%0\"",
				  r->refs.counter, "er", i, "%0", e, "cx");
}

static __always_inline __must_check bool refcount_dec_and_test(refcount_t *r)
{
	GEN_UNARY_SUFFIXED_RMWcc(LOCK_PREFIX "decl",
				 "REFCOUNT_CHECK_LT_ZERO counter=\"%0\"",
				 r->refs.counter, "%0", e, "cx");
}

static __always_inline __must_check
bool refcount_add_not_zero(unsigned int i, refcount_t *r)
{
	int c, result;

	c = atomic_read(&(r->refs));
	do {
		if (unlikely(c == 0))
			return false;

		result = c + i;

		/* Did we try to increment from/to an undesirable state? */
		if (unlikely(c < 0 || c == INT_MAX || result < c)) {
			asm volatile("REFCOUNT_ERROR counter=\"%[counter]\""
				     : : [counter] "m" (r->refs.counter)
				     : "cc", "cx");
			break;
		}

	} while (!atomic_try_cmpxchg(&(r->refs), &c, result));

	return c != 0;
}

static __always_inline __must_check bool refcount_inc_not_zero(refcount_t *r)
{
	return refcount_add_not_zero(1, r);
}

#endif /* __ASSEMBLY__ */

#endif
