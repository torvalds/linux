/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_S390_RWONCE_H
#define __ASM_S390_RWONCE_H

#include <linux/compiler_types.h>

/*
 * Use READ_ONCE_ALIGNED_128() for 128-bit block concurrent (atomic) read
 * accesses. Note that x must be 128-bit aligned, otherwise a specification
 * exception is generated.
 */
#define READ_ONCE_ALIGNED_128(x)			\
({							\
	union {						\
		typeof(x) __x;				\
		__uint128_t val;			\
	} __u;						\
							\
	BUILD_BUG_ON(sizeof(x) != 16);			\
	asm volatile(					\
		"	lpq	%[val],%[_x]\n"		\
		: [val] "=d" (__u.val)			\
		: [_x] "QS" (x)				\
		: "memory");				\
	__u.__x;					\
})

#include <asm-generic/rwonce.h>

#endif	/* __ASM_S390_RWONCE_H */
