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
 * @brief Time stamp and event counters.
 */

#ifndef _TSC_H_
#define _TSC_H_

#define INCLUDE_ALLOW_MVPD
#define INCLUDE_ALLOW_VMX
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_MONITOR
#define INCLUDE_ALLOW_GPL
#include "include_check.h"

#include "arm_inline.h"

#define ARM_PMNC_E          (1 << 0)
#define ARM_PMNC_D          (1 << 3)

#define ARM_PMCNT_C          (1 << 31)

#define ARM_PMNC_INVALID_EVENT -1

#define TSC_READ(_reg)  ARM_MRC_CP15(CYCLE_COUNT, (_reg))
#define TSC_WRITE(_reg) ARM_MCR_CP15(CYCLE_COUNT, (_reg))

#endif // ifndef _TSC_H_
