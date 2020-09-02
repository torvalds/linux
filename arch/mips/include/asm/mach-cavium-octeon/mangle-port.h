/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003, 2004 Ralf Baechle
 */
#ifndef __ASM_MACH_GENERIC_MANGLE_PORT_H
#define __ASM_MACH_GENERIC_MANGLE_PORT_H

#include <asm/byteorder.h>

#ifdef __BIG_ENDIAN

static inline bool __should_swizzle_bits(volatile void *a)
{
	extern const bool octeon_should_swizzle_table[];
	u64 did = ((u64)(uintptr_t)a >> 40) & 0xff;

	return octeon_should_swizzle_table[did];
}

# define __swizzle_addr_b(port)	(port)
# define __swizzle_addr_w(port)	(port)
# define __swizzle_addr_l(port)	(port)
# define __swizzle_addr_q(port)	(port)

#else /* __LITTLE_ENDIAN */

#define __should_swizzle_bits(a)	false

static inline bool __should_swizzle_addr(u64 p)
{
	/* boot bus? */
	return ((p >> 40) & 0xff) == 0;
}

# define __swizzle_addr_b(port)	\
	(__should_swizzle_addr(port) ? (port) ^ 7 : (port))
# define __swizzle_addr_w(port)	\
	(__should_swizzle_addr(port) ? (port) ^ 6 : (port))
# define __swizzle_addr_l(port)	\
	(__should_swizzle_addr(port) ? (port) ^ 4 : (port))
# define __swizzle_addr_q(port)	(port)

#endif /* __BIG_ENDIAN */


# define ioswabb(a, x)		(x)
# define __mem_ioswabb(a, x)	(x)
# define ioswabw(a, x)		(__should_swizzle_bits(a) ?		\
				 le16_to_cpu((__force __le16)(x)) :	\
				 (x))
# define __mem_ioswabw(a, x)	(x)
# define ioswabl(a, x)		(__should_swizzle_bits(a) ?		\
				 le32_to_cpu((__force __le32)(x)) :	\
				 (x))
# define __mem_ioswabl(a, x)	(x)
# define ioswabq(a, x)		(__should_swizzle_bits(a) ?		\
				 le64_to_cpu((__force __le64)(x)) :	\
				 (x))
# define __mem_ioswabq(a, x)	(x)

#endif /* __ASM_MACH_GENERIC_MANGLE_PORT_H */
