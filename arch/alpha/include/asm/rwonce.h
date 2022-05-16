/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019 Google LLC.
 */
#ifndef __ASM_RWONCE_H
#define __ASM_RWONCE_H

#ifdef CONFIG_SMP

#include <asm/barrier.h>

/*
 * Alpha is apparently daft enough to reorder address-dependent loads
 * on some CPU implementations. Knock some common sense into it with
 * a memory barrier in READ_ONCE().
 *
 * For the curious, more information about this unusual reordering is
 * available in chapter 15 of the "perfbook":
 *
 *  https://kernel.org/pub/linux/kernel/people/paulmck/perfbook/perfbook.html
 *
 */
#define __READ_ONCE(x)							\
({									\
	__unqual_scalar_typeof(x) __x =					\
		(*(volatile typeof(__x) *)(&(x)));			\
	mb();								\
	(typeof(x))__x;							\
})

#endif /* CONFIG_SMP */

#include <asm-generic/rwonce.h>

#endif /* __ASM_RWONCE_H */
