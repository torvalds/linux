/*
 * Linux 2.6.32 and later Kernel module for VMware MVP Hypervisor Support
 *
 * Copyright (C) 2010-2012 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; see the file COPYING.  If not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#line 5

/**
 * @file
 *
 * @brief Math library.
 */

#ifndef _MVP_MATH_H_
#define _MVP_MATH_H_

#define INCLUDE_ALLOW_VMX
#define INCLUDE_ALLOW_PV
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_MONITOR
#define INCLUDE_ALLOW_GPL
#define INCLUDE_ALLOW_HOSTUSER
#include "include_check.h"

#include "mvp_compiler_gcc.h"

/**
 * @brief Compute floor log2 of a given 32-bit unsigned integer.
 *
 * @param n 32-bit unsigned integer, n > 0.
 *
 * @return floor(log2(n)).
 */
#define LOG2(n)                         \
(                                       \
          __builtin_constant_p(n) ? (   \
            (n) & (1UL << 31) ? 31 :    \
            (n) & (1UL << 30) ? 30 :    \
            (n) & (1UL << 29) ? 29 :    \
            (n) & (1UL << 28) ? 28 :    \
            (n) & (1UL << 27) ? 27 :    \
            (n) & (1UL << 26) ? 26 :    \
            (n) & (1UL << 25) ? 25 :    \
            (n) & (1UL << 24) ? 24 :    \
            (n) & (1UL << 23) ? 23 :    \
            (n) & (1UL << 22) ? 22 :    \
            (n) & (1UL << 21) ? 21 :    \
            (n) & (1UL << 20) ? 20 :    \
            (n) & (1UL << 19) ? 19 :    \
            (n) & (1UL << 18) ? 18 :    \
            (n) & (1UL << 17) ? 17 :    \
            (n) & (1UL << 16) ? 16 :    \
            (n) & (1UL << 15) ? 15 :    \
            (n) & (1UL << 14) ? 14 :    \
            (n) & (1UL << 13) ? 13 :    \
            (n) & (1UL << 12) ? 12 :    \
            (n) & (1UL << 11) ? 11 :    \
            (n) & (1UL << 10) ? 10 :    \
            (n) & (1UL <<  9) ?  9 :    \
            (n) & (1UL <<  8) ?  8 :    \
            (n) & (1UL <<  7) ?  7 :    \
            (n) & (1UL <<  6) ?  6 :    \
            (n) & (1UL <<  5) ?  5 :    \
            (n) & (1UL <<  4) ?  4 :    \
            (n) & (1UL <<  3) ?  3 :    \
            (n) & (1UL <<  2) ?  2 :    \
            (n) & (1UL <<  1) ?  1 :    \
            (n) & (1UL <<  0) ?  0 :    \
	    0xffffffff			\
	  ) : (uint32)(CLZ(1) - CLZ(n)) \
)

/**
 * @brief Multiplicative hash function for 32-bit key and p-bit range. See p229
 *        Introduction to Algorithms, Cormen, Leiserson and Rivest, 1996.
 *
 * @param key 32-bit key.
 * @param p range order, <= 32.
 *
 * @return hash value in range [0..2^p)
 */
static inline uint32
Math_MultiplicativeHash(uint32 key, uint32 p)
{
   return (key * 2654435769UL) >> (32 - p);
}

/**
 * @brief Compute ceiling log2 of a given 32-bit unsigned integer.
 *
 * @param n 32-bit unsigned integer, n > 0.
 *
 * @return ceiling(log2(n)).
 */
static inline uint32 CLOG2(uint32 n)
{
   return LOG2(n) + ((n & -n) != n);
}


/**
 * @brief djb2 String hashing function by Dan Bernstein, see
 *      http://www.cse.yorku.ca/~oz/hash.html
 * @param str String to hash
 * @return 32-bit hash value
 */
static inline
uint32 Math_Djb2Hash(uint8 *str)
{
     uint32 hash = 5381;
     int32 c;

     while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
     }

     return hash;
}

#endif // ifndef _MVP_MATH_H_
