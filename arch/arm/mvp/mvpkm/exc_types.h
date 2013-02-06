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
 *  @file
 *
 *  @brief Exception-related types. See A2.6 ARM DDI 0100I.
 */

#ifndef _EXC_TYPES_H_
#define _EXC_TYPES_H_

#define INCLUDE_ALLOW_MVPD
#define INCLUDE_ALLOW_VMX
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_MONITOR
#define INCLUDE_ALLOW_PV
#define INCLUDE_ALLOW_GPL
#include "include_check.h"

/**
 * @brief ARM hardware exception enumeration. EXC_NONE is added to provide
 *        a distinguished value to flag non-exception states.
 */
typedef enum {
   EXC_NONE,
   EXC_RESET,
   EXC_UNDEFINED,
   EXC_SWI,
   EXC_PREFETCH_ABORT,
   EXC_DATA_ABORT,
   EXC_IRQ,
   EXC_FIQ
} ARM_Exception;

#endif /// _EXC_TYPES_H_
