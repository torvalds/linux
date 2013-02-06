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
 * @brief ASSERT() and related macros.
 */

#ifndef _MVP_ASSERT_H
#define _MVP_ASSERT_H

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

#define ASSERT(_x) ASSERT_BUG((_x),0)

#ifndef NDEBUG
#define ASSERT_BUG(_x,_tkt) do {  \
   if (UNLIKELY(!(_x))) {                                        \
      FatalError(__FILE__, __LINE__, FECodeAssert, _tkt, NULL);  \
   }                                                             \
} while (0)

#define ASSERTF(_x, ...) do {  \
   if (UNLIKELY(!(_x))) {       \
      FatalError(__FILE__,      \
                 __LINE__,      \
                 FECodeAssert,  \
                 0,             \
                 __VA_ARGS__);  \
   }                            \
} while (0)
#else

#define ASSERT_BUG(_x,_tkt)  (void)sizeof((int)(_x))
#define ASSERTF(_x, ...)    ASSERT_BUG(_x, 0)

#endif

/*
 * Compile-time assertions.
 *
 * ASSERT_ON_COMPILE does not use the common
 * switch (0) { case 0: case (e): ; } trick because some compilers (e.g. MSVC)
 * generate code for it.
 *
 * The implementation uses both enum and typedef because the typedef alone is
 * insufficient; gcc allows arrays to be declared with non-constant expressions
 * (even in typedefs, where it makes no sense).
 */
#ifdef __COVERITY__
#define ASSERT_ON_COMPILE(e) ASSERT(e)
#else
#define ASSERT_ON_COMPILE(e) \
   do { \
      enum { AssertOnCompileMisused = ((e) ? 1 : -1) }; \
      typedef char AssertOnCompileFailed[AssertOnCompileMisused]; \
   } while (0)
#endif

/*
 * To put an ASSERT_ON_COMPILE() outside a function, wrap it
 * in MY_ASSERTS().  The first parameter must be unique in
 * each .c file where it appears.  For example,
 *
 * MY_ASSERTS(FS3_INT,
 *    ASSERT_ON_COMPILE(sizeof(FS3_DiskLock) == 128);
 *    ASSERT_ON_COMPILE(sizeof(FS3_DiskLockReserved) == DISK_BLOCK_SIZE);
 *    ASSERT_ON_COMPILE(sizeof(FS3_DiskBlock) == DISK_BLOCK_SIZE);
 *    ASSERT_ON_COMPILE(sizeof(Hardware_DMIUUID) == 16);
 * )
 *
 * Caution: ASSERT() within MY_ASSERTS() is silently ignored.
 * The same goes for anything else not evaluated at compile time.
 */

#define MY_ASSERTS(name, assertions) \
   static inline void name(void) { \
      assertions \
   }

#define KNOWN_BUG(_tkt)

#define NOT_IMPLEMENTED() NOT_IMPLEMENTED_JIRA(0)
#define NOT_IMPLEMENTED_JIRA(_tkt,...) FatalError(__FILE__, __LINE__, FECodeNI, _tkt, NULL)

#define NOT_IMPLEMENTED_IF(_x) NOT_IMPLEMENTED_IF_JIRA((_x),0)
#define NOT_IMPLEMENTED_IF_JIRA(_x,_tkt,...) do { if (UNLIKELY(_x)) NOT_IMPLEMENTED_JIRA(_tkt); } while (0)
/*
 * All sites tagged with this are @knownjira{MVP-1855}.
 */
#define NOT_IMPLEMENTEDF(...) FatalError(__FILE__, __LINE__, FECodeNI, 0, __VA_ARGS__)

#define NOT_REACHED() FatalError(__FILE__, __LINE__, FECodeNR, 0, NULL)

#include "fatalerror.h"
#include "nottested.h"

#endif
