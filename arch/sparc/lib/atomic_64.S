/* SPDX-License-Identifier: GPL-2.0 */
/* atomic.S: These things are too big to do inline.
 *
 * Copyright (C) 1999, 2007 2012 David S. Miller (davem@davemloft.net)
 */

#include <linux/linkage.h>
#include <asm/asi.h>
#include <asm/backoff.h>
#include <asm/export.h>

	.text

	/* Three versions of the atomic routines, one that
	 * does not return a value and does not perform
	 * memory barriers, and a two which return
	 * a value, the new and old value resp. and does the
	 * barriers.
	 */

#define ATOMIC_OP(op)							\
ENTRY(arch_atomic_##op) /* %o0 = increment, %o1 = atomic_ptr */		\
	BACKOFF_SETUP(%o2);						\
1:	lduw	[%o1], %g1;						\
	op	%g1, %o0, %g7;						\
	cas	[%o1], %g1, %g7;					\
	cmp	%g1, %g7;						\
	bne,pn	%icc, BACKOFF_LABEL(2f, 1b);				\
	 nop;								\
	retl;								\
	 nop;								\
2:	BACKOFF_SPIN(%o2, %o3, 1b);					\
ENDPROC(arch_atomic_##op);						\
EXPORT_SYMBOL(arch_atomic_##op);

#define ATOMIC_OP_RETURN(op)						\
ENTRY(arch_atomic_##op##_return) /* %o0 = increment, %o1 = atomic_ptr */\
	BACKOFF_SETUP(%o2);						\
1:	lduw	[%o1], %g1;						\
	op	%g1, %o0, %g7;						\
	cas	[%o1], %g1, %g7;					\
	cmp	%g1, %g7;						\
	bne,pn	%icc, BACKOFF_LABEL(2f, 1b);				\
	 op	%g1, %o0, %g1;						\
	retl;								\
	 sra	%g1, 0, %o0;						\
2:	BACKOFF_SPIN(%o2, %o3, 1b);					\
ENDPROC(arch_atomic_##op##_return);					\
EXPORT_SYMBOL(arch_atomic_##op##_return);

#define ATOMIC_FETCH_OP(op)						\
ENTRY(arch_atomic_fetch_##op) /* %o0 = increment, %o1 = atomic_ptr */	\
	BACKOFF_SETUP(%o2);						\
1:	lduw	[%o1], %g1;						\
	op	%g1, %o0, %g7;						\
	cas	[%o1], %g1, %g7;					\
	cmp	%g1, %g7;						\
	bne,pn	%icc, BACKOFF_LABEL(2f, 1b);				\
	 nop;								\
	retl;								\
	 sra	%g1, 0, %o0;						\
2:	BACKOFF_SPIN(%o2, %o3, 1b);					\
ENDPROC(arch_atomic_fetch_##op);					\
EXPORT_SYMBOL(arch_atomic_fetch_##op);

ATOMIC_OP(add)
ATOMIC_OP_RETURN(add)
ATOMIC_FETCH_OP(add)

ATOMIC_OP(sub)
ATOMIC_OP_RETURN(sub)
ATOMIC_FETCH_OP(sub)

ATOMIC_OP(and)
ATOMIC_FETCH_OP(and)

ATOMIC_OP(or)
ATOMIC_FETCH_OP(or)

ATOMIC_OP(xor)
ATOMIC_FETCH_OP(xor)

#undef ATOMIC_FETCH_OP
#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP

#define ATOMIC64_OP(op)							\
ENTRY(arch_atomic64_##op) /* %o0 = increment, %o1 = atomic_ptr */	\
	BACKOFF_SETUP(%o2);						\
1:	ldx	[%o1], %g1;						\
	op	%g1, %o0, %g7;						\
	casx	[%o1], %g1, %g7;					\
	cmp	%g1, %g7;						\
	bne,pn	%xcc, BACKOFF_LABEL(2f, 1b);				\
	 nop;								\
	retl;								\
	 nop;								\
2:	BACKOFF_SPIN(%o2, %o3, 1b);					\
ENDPROC(arch_atomic64_##op);						\
EXPORT_SYMBOL(arch_atomic64_##op);

#define ATOMIC64_OP_RETURN(op)						\
ENTRY(arch_atomic64_##op##_return) /* %o0 = increment, %o1 = atomic_ptr */	\
	BACKOFF_SETUP(%o2);						\
1:	ldx	[%o1], %g1;						\
	op	%g1, %o0, %g7;						\
	casx	[%o1], %g1, %g7;					\
	cmp	%g1, %g7;						\
	bne,pn	%xcc, BACKOFF_LABEL(2f, 1b);				\
	 nop;								\
	retl;								\
	 op	%g1, %o0, %o0;						\
2:	BACKOFF_SPIN(%o2, %o3, 1b);					\
ENDPROC(arch_atomic64_##op##_return);					\
EXPORT_SYMBOL(arch_atomic64_##op##_return);

#define ATOMIC64_FETCH_OP(op)						\
ENTRY(arch_atomic64_fetch_##op) /* %o0 = increment, %o1 = atomic_ptr */	\
	BACKOFF_SETUP(%o2);						\
1:	ldx	[%o1], %g1;						\
	op	%g1, %o0, %g7;						\
	casx	[%o1], %g1, %g7;					\
	cmp	%g1, %g7;						\
	bne,pn	%xcc, BACKOFF_LABEL(2f, 1b);				\
	 nop;								\
	retl;								\
	 mov	%g1, %o0;						\
2:	BACKOFF_SPIN(%o2, %o3, 1b);					\
ENDPROC(arch_atomic64_fetch_##op);					\
EXPORT_SYMBOL(arch_atomic64_fetch_##op);

ATOMIC64_OP(add)
ATOMIC64_OP_RETURN(add)
ATOMIC64_FETCH_OP(add)

ATOMIC64_OP(sub)
ATOMIC64_OP_RETURN(sub)
ATOMIC64_FETCH_OP(sub)

ATOMIC64_OP(and)
ATOMIC64_FETCH_OP(and)

ATOMIC64_OP(or)
ATOMIC64_FETCH_OP(or)

ATOMIC64_OP(xor)
ATOMIC64_FETCH_OP(xor)

#undef ATOMIC64_FETCH_OP
#undef ATOMIC64_OP_RETURN
#undef ATOMIC64_OP

ENTRY(arch_atomic64_dec_if_positive) /* %o0 = atomic_ptr */
	BACKOFF_SETUP(%o2)
1:	ldx	[%o0], %g1
	brlez,pn %g1, 3f
	 sub	%g1, 1, %g7
	casx	[%o0], %g1, %g7
	cmp	%g1, %g7
	bne,pn	%xcc, BACKOFF_LABEL(2f, 1b)
	 nop
3:	retl
	 sub	%g1, 1, %o0
2:	BACKOFF_SPIN(%o2, %o3, 1b)
ENDPROC(arch_atomic64_dec_if_positive)
EXPORT_SYMBOL(arch_atomic64_dec_if_positive)
