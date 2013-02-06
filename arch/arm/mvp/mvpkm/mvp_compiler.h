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
 * @brief Compiler-related definitions and directives.
 */

#ifndef _MVP_COMPILER_H_
#define _MVP_COMPILER_H_

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

#ifdef __GNUC__
#include "mvp_compiler_gcc.h"
#else /* __GNUC__ */
#include "mvp_compiler_other.h"
#endif /* __GNUC__ */

/**
 * @brief Find last set bit.
 *
 * @param n unsigned 32-bit integer.
 *
 * @return 0 if n == 0 otherwise 32 - the number of leading zeroes in n.
 */
#define FLS(n) (32 - CLZ(n))

#endif /// ifndef _MVP_COMPILER_H_
