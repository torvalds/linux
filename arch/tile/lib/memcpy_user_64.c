/*
 * Copyright 2011 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * Do memcpy(), but trap and return "n" when a load or store faults.
 *
 * Note: this idiom only works when memcpy() compiles to a leaf function.
 * Here leaf function not only means it does not have calls, but also
 * requires no stack operations (sp, stack frame pointer) and no
 * use of callee-saved registers, else "jrp lr" will be incorrect since
 * unwinding stack frame is bypassed. Since memcpy() is not complex so
 * these conditions are satisfied here, but we need to be careful when
 * modifying this file. This is not a clean solution but is the best
 * one so far.
 *
 * Also note that we are capturing "n" from the containing scope here.
 */

#define _ST(p, inst, v)						\
	({							\
		asm("1: " #inst " %0, %1;"			\
		    ".pushsection .coldtext.memcpy,\"ax\";"	\
		    "2: { move r0, %2; jrp lr };"		\
		    ".section __ex_table,\"a\";"		\
		    ".quad 1b, 2b;"				\
		    ".popsection"				\
		    : "=m" (*(p)) : "r" (v), "r" (n));		\
	})

#define _LD(p, inst)						\
	({							\
		unsigned long __v;				\
		asm("1: " #inst " %0, %1;"			\
		    ".pushsection .coldtext.memcpy,\"ax\";"	\
		    "2: { move r0, %2; jrp lr };"		\
		    ".section __ex_table,\"a\";"		\
		    ".quad 1b, 2b;"				\
		    ".popsection"				\
		    : "=r" (__v) : "m" (*(p)), "r" (n));	\
		__v;						\
	})

#define USERCOPY_FUNC __copy_to_user_inatomic
#define ST1(p, v) _ST((p), st1, (v))
#define ST2(p, v) _ST((p), st2, (v))
#define ST4(p, v) _ST((p), st4, (v))
#define ST8(p, v) _ST((p), st, (v))
#define LD1 LD
#define LD2 LD
#define LD4 LD
#define LD8 LD
#include "memcpy_64.c"

#define USERCOPY_FUNC __copy_from_user_inatomic
#define ST1 ST
#define ST2 ST
#define ST4 ST
#define ST8 ST
#define LD1(p) _LD((p), ld1u)
#define LD2(p) _LD((p), ld2u)
#define LD4(p) _LD((p), ld4u)
#define LD8(p) _LD((p), ld)
#include "memcpy_64.c"

#define USERCOPY_FUNC __copy_in_user_inatomic
#define ST1(p, v) _ST((p), st1, (v))
#define ST2(p, v) _ST((p), st2, (v))
#define ST4(p, v) _ST((p), st4, (v))
#define ST8(p, v) _ST((p), st, (v))
#define LD1(p) _LD((p), ld1u)
#define LD2(p) _LD((p), ld2u)
#define LD4(p) _LD((p), ld4u)
#define LD8(p) _LD((p), ld)
#include "memcpy_64.c"

unsigned long __copy_from_user_zeroing(void *to, const void __user *from,
				       unsigned long n)
{
	unsigned long rc = __copy_from_user_inatomic(to, from, n);
	if (unlikely(rc))
		memset(to + n - rc, 0, rc);
	return rc;
}
