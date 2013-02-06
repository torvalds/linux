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
 * @brief timer definitions
 */

#ifndef _MVP_TIMER_H
#define _MVP_TIMER_H

#define INCLUDE_ALLOW_MVPD
#define INCLUDE_ALLOW_VMX
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_MONITOR
#define INCLUDE_ALLOW_GPL
#include "include_check.h"

/**
 * @brief timer tick rate as returned by MVPTimer_Now64 as a uint64 and used by
 * MVPTimer.when64.
 *
 * For example 1,000,000 means the counter is in microseconds.
 *
 * Current implementation requires MVP_TIMER_RATE64 <= 1,000,000,000 and that
 * it evenly divide 1,000,000,000.  Currently 1,000,000,000 to avoid a multiply
 * or divide in MVPTimer_Now64.
 */
#define MVP_TIMER_RATE64 1000000000

/*
 * Extract current UNIX-style time_t date/time from the 64-bit time as returned
 * by MVPTimer_Now64().
 */
#define MVP_TIMER_RATE64_TIME_T(time64) ((time_t)((time64) / MVP_TIMER_RATE64))

typedef struct MVPTimer MVPTimer;

/**
 * @brief timer entry struct
 */
struct MVPTimer {
   MVPTimer *next;                                ///< next in timers list
   uint64 when64;                                 ///< absolute expiration
   void (*entry)(uint64 now64, MVPTimer *timer);  ///< callback entrypoint
   void *param;                                   ///< callback parameter
};

void   MVPTimer_InitVMX(void);
uint64 MVPTimer_Now64(void);
void   MVPTimer_Start(MVPTimer *timer);
_Bool  MVPTimer_Cancel(MVPTimer *timer);

#endif
