/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ALPHA_CMPXCHG_H
#define _ALPHA_CMPXCHG_H

/*
 * Atomic exchange routines.
 */

#define __ASM__MB
#define ____xchg(type, args...)		__xchg ## type ## _local(args)
#define ____cmpxchg(type, args...)	__cmpxchg ## type ## _local(args)
#include <asm/xchg.h>

#define xchg_local(ptr, x)						\
({									\
	__typeof__(*(ptr)) _x_ = (x);					\
	(__typeof__(*(ptr))) __xchg_local((ptr), (unsigned long)_x_,	\
				       sizeof(*(ptr)));			\
})

#define cmpxchg_local(ptr, o, n)					\
({									\
	__typeof__(*(ptr)) _o_ = (o);					\
	__typeof__(*(ptr)) _n_ = (n);					\
	(__typeof__(*(ptr))) __cmpxchg_local((ptr), (unsigned long)_o_,	\
					  (unsigned long)_n_,		\
					  sizeof(*(ptr)));		\
})

#define cmpxchg64_local(ptr, o, n)					\
({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);				\
	cmpxchg_local((ptr), (o), (n));					\
})

#ifdef CONFIG_SMP
#undef __ASM__MB
#define __ASM__MB	"\tmb\n"
#endif
#undef ____xchg
#undef ____cmpxchg
#define ____xchg(type, args...)		__xchg ##type(args)
#define ____cmpxchg(type, args...)	__cmpxchg ##type(args)
#include <asm/xchg.h>

#define xchg(ptr, x)							\
({									\
	__typeof__(*(ptr)) _x_ = (x);					\
	(__typeof__(*(ptr))) __xchg((ptr), (unsigned long)_x_,		\
				 sizeof(*(ptr)));			\
})

#define cmpxchg(ptr, o, n)						\
({									\
	__typeof__(*(ptr)) _o_ = (o);					\
	__typeof__(*(ptr)) _n_ = (n);					\
	(__typeof__(*(ptr))) __cmpxchg((ptr), (unsigned long)_o_,	\
				    (unsigned long)_n_,	sizeof(*(ptr)));\
})

#define cmpxchg64(ptr, o, n)						\
({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);				\
	cmpxchg((ptr), (o), (n));					\
})

#undef __ASM__MB
#undef ____cmpxchg

#endif /* _ALPHA_CMPXCHG_H */
