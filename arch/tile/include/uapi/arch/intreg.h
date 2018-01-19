/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2017 Tilera Corporation. All Rights Reserved.
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
 */

/**
 * @file
 *
 * Provide types and defines for the type that can hold a register,
 * in the implementation namespace.
 */

#ifndef __ARCH_INTREG_H__
#define __ARCH_INTREG_H__

/*
 * Get number of bits in a register.  __INT_REG_BITS may be defined
 * prior to including this header to force a particular bit width.
 */

#ifndef __INT_REG_BITS
# if defined __tilegx__
#  define __INT_REG_BITS 64
# elif defined __tilepro__
#  define __INT_REG_BITS 32
# else
#  error Unrecognized architecture
# endif
#endif

#if __INT_REG_BITS == 64

# ifndef __ASSEMBLER__
/** Unsigned type that can hold a register. */
typedef unsigned long long __uint_reg_t;

/** Signed type that can hold a register. */
typedef long long __int_reg_t;
# endif

/** String prefix to use for printf(). */
# define __INT_REG_FMT "ll"

#elif __INT_REG_BITS == 32

# ifndef __ASSEMBLER__
/** Unsigned type that can hold a register. */
typedef unsigned long __uint_reg_t;

/** Signed type that can hold a register. */
typedef long __int_reg_t;
# endif

/** String prefix to use for printf(). */
# define __INT_REG_FMT "l"

#else
# error Unrecognized value of __INT_REG_BITS
#endif

#endif /* !__ARCH_INTREG_H__ */
