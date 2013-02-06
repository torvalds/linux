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
 *  @brief MVP host kernel implementation of monitor timers
 *
 * The monitor sends requests that are simply a 64-bit absolute time that it
 * wants a reply.  If it changes its mind, it simply sends a different 64-bit
 * absolute time.  It is tolerant of us replying too soon, so if we miss the
 * update to a later time, it doesn't matter, the monitor will re-send the
 * request for the later time.  The only time we should miss an update to a
 * sooner time is when we are about to send the reply to the old time anyway,
 * in which case the monitor sees a reply as quickly as we can generate them,
 * so no harm there either.
 */

#include <linux/module.h>
#include <linux/hrtimer.h>

#include "mvp.h"
#include "mvp_timer.h"
#include "actions.h"
#include "mvpkm_kernel.h"

/**
 * @brief Linux timer callback
 * @param timer The linux timer raised
 * @return Status to not restart the timer
 */
static enum hrtimer_restart
MonitorTimerCB(struct hrtimer *timer)
{
   MvpkmVM *vm = container_of(timer, MvpkmVM, monTimer.timer);
   Mvpkm_WakeGuest(vm, ACTION_TIMER);
   return HRTIMER_NORESTART;
}

/**
 * @brief Initialize vm associated timer
 * @param vm  which virtual machine we're running
 */
void
MonitorTimer_Setup(MvpkmVM *vm)
{
   MonTimer *monTimer = &vm->monTimer;
   monTimer->vm = vm;

   hrtimer_init(&monTimer->timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
   monTimer->timer.function = MonitorTimerCB;
}

/**
 * @brief New timer request from monitor
 * @param monTimer Monitor timer
 * @param when64 Timer target value
 */
void
MonitorTimer_Request(MonTimer *monTimer, uint64 when64)
{
   if (when64) {
      ktime_t kt;

      /*
       * Simple conversion, assuming RATE64 is 1e+9
       */
      kt = ns_to_ktime(when64);
      ASSERT_ON_COMPILE(MVP_TIMER_RATE64 == 1000000000);

      /*
       * Start the timer.  If it was already active, it will remove
       * the previous expiration time.  Linux handles correctly timer
       * with deadline in the past, and forces a safety minimal delta
       * for closer timer deadlines.
       */
      hrtimer_start(&monTimer->timer, kt, HRTIMER_MODE_ABS);
   } else {
      /*
       * Cancel a pending request.  If there is none, this will do nothing.
       * If it's too late, monitor tolerance will forgive us.
       */
      hrtimer_cancel(&monTimer->timer);
   }
}
