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
 * @brief The monitor-kernel socket interface kernel-only definitions.
 */

#ifndef _CPUFREQ_KERNEL_H
#define _CPUFREQ_KERNEL_H

#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_GPL
#include "include_check.h"

/* Scaling factors to convert CPU clock cycles to Rate64 value */
struct TscToRate64Cb {
   uint32 mult;
   uint32 shift;
};

/* It is assumed that this is only accessed from the current CPU core and not
 * "across cores" */
DECLARE_PER_CPU(struct TscToRate64Cb, tscToRate64);

void CpuFreq_Init(void);
void CpuFreq_Exit(void);

#endif
