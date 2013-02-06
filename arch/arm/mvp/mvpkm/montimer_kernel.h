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

#ifndef _MONITOR_TIMER_KERNEL_H
#define _MONITOR_TIMER_KERNEL_H

#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_GPL
#include "include_check.h"

#include <linux/hrtimer.h>

/**
 * @brief Monitor Timer structure
 */
typedef struct {
   struct MvpkmVM *vm;         ///< Associated vm
   struct hrtimer  timer;      ///< Linux timer
} MonTimer;

void MonitorTimer_Setup(struct MvpkmVM *vm);
void MonitorTimer_Request(MonTimer *monTimer, uint64 when64);

#endif
