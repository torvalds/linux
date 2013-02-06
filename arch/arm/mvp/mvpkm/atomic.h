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
 * @brief bus-atomic operators.
 *
 * The 'atm' argument is the atomic memory cell being operated on and the
 * remainder of the arguments are the values being applied to the atomic cell
 * which is assumed to be located in shared normal memory.  The operation is
 * both atomic and visible to the default share-ability domain upon completion.
 *
 * The design of each macro is such that the compiler should check types
 * correctly.  For those macros that return a value, the return type should be
 * the same as the 'atm' argument (with the exception of ATOMIC_SETIF which
 * returns an int value of 0 or 1).
 *
 * Those names ending in 'M' return the modified value of 'atm'.
 * Those names ending in 'O' return the original value of 'atm'.
 * Those names ending in 'V' return void (ie, nothing).
 */

#ifndef _ATOMIC_H
#define _ATOMIC_H

#define INCLUDE_ALLOW_MVPD
#define INCLUDE_ALLOW_VMX
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_MONITOR
#define INCLUDE_ALLOW_PV
#define INCLUDE_ALLOW_GPL
#define INCLUDE_ALLOW_HOSTUSER
#define INCLUDE_ALLOW_GUESTUSER
#include "include_check.h"

/*
 * Wrap type 't' in an atomic struct.
 * Eg, 'static ATOMIC(uint8) counter;'.
 *
 * The function macros use the atm_Normal member to clone the atom's type
 * when the volatile semantic is not required.  They use the atm_Volatl member
 * when the volatile semantic is required.
 */
#define ATOMIC(t) union { t atm_Normal; t volatile atm_Volatl; }

/*
 * Static atomic variable initialization.
 * Eg, 'static ATOMIC(uint8) counter = ATOMIC_INI(35);'.
 */
#define ATOMIC_INI(v) { .atm_Normal = v }

/*
 * Some commonly used atomic types.
 */
typedef ATOMIC(int32)  AtmSInt32 __attribute__ ((aligned (4)));
typedef ATOMIC(uint32) AtmUInt32 __attribute__ ((aligned (4)));
typedef ATOMIC(uint64) AtmUInt64 __attribute__ ((aligned (8)));

/*
 * Architecture-dependent implementations.
 */
#if defined(__COVERITY__)
#include "atomic_coverity.h"
#elif defined(__arm__)
#include "atomic_arm.h"
#elif defined(__i386) || defined(__x86_64)
#include "atomic_x86.h"
#endif

#endif
