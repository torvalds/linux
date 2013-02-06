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
 * @brief General architecture-independent definitions, typedefs, and macros.
 */

#ifndef _UTILS_H
#define _UTILS_H

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

#define MAX_FILENAME 128

// Round address up to given size boundary
// Note: ALIGN() conflicts with Linux

#define MVP_ALIGN(_v, _n) (((_v) + (_n) - 1) & -(_n))

#define ALIGNVA(_addr, _size) MVP_ALIGN(_addr, _size)

#define alignof(t) offsetof(struct { char c; typeof(t) x; }, x)

#define MIN(x,y) ((x) < (y) ? (x) : (y))
#define MAX(x,y) ((x) > (y) ? (x) : (y))

#ifndef NULL
#define NULL ((void *)0)
#endif

#define KB(_X_) ((_X_)*1024U)
#define MB(_X_) (KB(_X_)*1024)
#define GB(_X_) (MB(_X_)*1024)

#define NELEM(x) (sizeof(x)/sizeof((x)[0]))

/*
 *  x in [low,high)
 * args evaluated once
 */
#define RANGE(x,low,high)                       \
   ({                                           \
      typeof(x) _x = (x);                       \
      typeof(x) _low = (typeof(x))(low);        \
      typeof(x) _high =(typeof(x))(high);       \
      (_Bool)( (_low <= _x) && (_x < _high));   \
   })

#define OBJECTS_PER_PAGE(_type) (PAGE_SIZE / sizeof(_type))

#define MA_2_MPN(_ma)   ((MPN)((_ma) / PAGE_SIZE))
#define MPN_2_MA(_mpn)  ((MA)((_mpn) * PAGE_SIZE))

#define VA_2_VPN(_va)   ((_va) / PAGE_SIZE)
#define VPN_2_vA(_vpn)  ((_vpn) * PAGE_SIZE)

/*
 * The following convenience macro can be used in a following situation
 *
 *     send(..., &foo, sizeof(foo))  --> send(..., PTR_N_SIZE(foo))
 */

#define PTR_N_SIZE(_var) &(_var), sizeof(_var)


/*
 *
 * BIT-PULLING macros
 *
 */
#define MVP_BIT(val,n) ( ((val)>>(n))&1)
#define MVP_BITS(val,m,n) (((val)<<(31-(n))) >> ((31-(n))+(m)) )
#define MVP_EXTRACT_FIELD(w, m, n) MVP_BITS((w), (m), ((m) + (n) - 1))
#define MVP_MASK(m, n) (MVP_EXTRACT_FIELD(~(uint32)0U, (m), (n)) << (m))
#define MVP_UPDATE_FIELD(old_val, field_val, m, n) \
   (((old_val) & ~MVP_MASK((m), (n))) | (MVP_EXTRACT_FIELD((field_val), 0, (n)) << (m)))

/*
 *
 * 64BIT-PULLING macros
 *
 */
#define MVP_BITS64(val,m,n) (((val)<<(63-(n))) >> ((63-(n))+(m)) )
#define MVP_EXTRACT_FIELD64(w, m, n) MVP_BITS64((w), (m), ((m) + (n) - 1))
#define MVP_MASK64(m, n) (MVP_EXTRACT_FIELD64(~(uint64)0ULL, (m), (n)) << (m))
#define MVP_UPDATE_FIELD64(old_val, field_val, m, n) \
   (((old_val) & ~MVP_MASK64((m), (n))) | (MVP_EXTRACT_FIELD64(((uint64)(field_val)), 0ULL, (n)) << (m)))

/*
 *
 * BIT-CHANGING macros
 *
 */
#define MVP_SETBIT(val,n)     ((val)|=(1<<(n)))
#define MVP_CLRBIT(val,n)     ((val)&=(~(1<<(n))))

/*
 * Fixed bit-width sign extension.
 */
#define MVP_SIGN_EXTEND(val,width) \
   (((val) ^ (1 << ((width) - 1))) - (1 << ((width) - 1)))


/*
 * Assembler helpers.
 */
#define _MVP_HASH #
#define MVP_HASH() _MVP_HASH

#define _MVP_STRINGIFY(...)     #__VA_ARGS__
#define MVP_STRINGIFY(...)      _MVP_STRINGIFY(__VA_ARGS__)

#ifndef __ASSEMBLER__

#include <stddef.h>
#include <stdbool.h>

/*
 * Constant equivalents of build-flags.
 *
 * Test these when possible instead of using #ifdef so that your code
 * gets parsed.
 */
#ifdef MVP_DEBUG
static const _Bool mvpDebug = true;
#else
static const _Bool mvpDebug = false;
#endif

#ifdef MVP_STATS
static const _Bool mvpStats = true;
#else
static const _Bool mvpStats = false;
#endif

#ifdef MVP_DEVEL
static const _Bool mvpDevel = true;
#else
static const _Bool mvpDevel = false;
#endif

#endif

#endif
