/*
 * Linux 2.6.32 and later Kernel module for VMware MVP Guest Communications
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
 * @brief common definitions for GCC
 */

#ifndef _MVP_COMPILER_GCC_H
#define _MVP_COMPILER_GCC_H

#define INCLUDE_ALLOW_MVPD
#define INCLUDE_ALLOW_VMX
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_MONITOR
#define INCLUDE_ALLOW_PV
#define INCLUDE_ALLOW_HOSTUSER
#define INCLUDE_ALLOW_GUESTUSER
#define INCLUDE_ALLOW_WORKSTATION
#define INCLUDE_ALLOW_GPL
#include "include_check.h"

/**
 * @brief Count leading zeroes.
 *
 * @param n unsigned 32-bit integer.
 *
 * @return 32 if n == 0 otherwise 31 - the bit position of the most significant 1
 *         in n.
 */
#ifdef __COVERITY__
static inline int
CLZ(unsigned int n)
{
   unsigned int r = 0;

   while (n) {
      r++;
      n >>= 1;
   }

   return 32 - r;
}
#else
#define CLZ(n) __builtin_clz(n)
#endif

#define PACKED __attribute__ ((packed))
#define ALLOC  __attribute__ ((malloc, warn_unused_result))
#define UNUSED __attribute__ ((unused))
#define PURE   __attribute__ ((pure))
#define WARN_UNUSED_RESULT __attribute__ ((warn_unused_result))
#define FORMAT(x,y,z) __attribute__ ((format(x,y,z)))
#define LIKELY(x)       __builtin_expect(!!(x), 1)
#define UNLIKELY(x)     __builtin_expect((x), 0)

/*
 * For debug builds, we want to omit __attribute__((noreturn)) so that gcc will
 * keep stack linkages and then we will have useful core dumps.  For non-debug
 * builds, we don't care about the stack frames and want the little bit of
 * optimization that noreturn gives us.
 */
#if defined(__COVERITY__) || !defined(MVP_DEBUG)
#define NORETURN __attribute__((noreturn))
#else
#define NORETURN
#endif

#endif
