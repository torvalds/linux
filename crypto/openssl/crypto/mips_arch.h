/*
 * Copyright 2011-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef __MIPS_ARCH_H__
# define __MIPS_ARCH_H__

# if (defined(__mips_smartmips) || defined(_MIPS_ARCH_MIPS32R3) || \
      defined(_MIPS_ARCH_MIPS32R5) || defined(_MIPS_ARCH_MIPS32R6))
      && !defined(_MIPS_ARCH_MIPS32R2)
#  define _MIPS_ARCH_MIPS32R2
# endif

# if (defined(_MIPS_ARCH_MIPS64R3) || defined(_MIPS_ARCH_MIPS64R5) || \
      defined(_MIPS_ARCH_MIPS64R6)) \
      && !defined(_MIPS_ARCH_MIPS64R2)
#  define _MIPS_ARCH_MIPS64R2
# endif

# if defined(_MIPS_ARCH_MIPS64R6)
#  define dmultu(rs,rt)
#  define mflo(rd,rs,rt)	dmulu	rd,rs,rt
#  define mfhi(rd,rs,rt)	dmuhu	rd,rs,rt
# elif defined(_MIPS_ARCH_MIPS32R6)
#  define multu(rs,rt)
#  define mflo(rd,rs,rt)	mulu	rd,rs,rt
#  define mfhi(rd,rs,rt)	muhu	rd,rs,rt
# else
#  define dmultu(rs,rt)		dmultu	rs,rt
#  define multu(rs,rt)		multu	rs,rt
#  define mflo(rd,rs,rt)	mflo	rd
#  define mfhi(rd,rs,rt)	mfhi	rd
# endif

#endif
