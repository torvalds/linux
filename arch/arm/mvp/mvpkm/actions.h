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
 * @brief Bit definitions for instrBActions.
 */

#ifndef _ACTIONS_H
#define _ACTIONS_H

#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_MONITOR
#define INCLUDE_ALLOW_GPL
#include "include_check.h"

#define L2_ACTION_GDB      0  ///< drop into guest debugger GDB
#define L2_ACTION_MKSCK    1  ///< scan the mksck pipes for incoming messages
#define L2_ACTION_ABORT    2  ///< abort the monitor cleanly
#define L2_ACTION_HALT     3  ///< halt the monitor
#define L2_ACTION_FIQ      6  ///< the VCPU's FIQ pin is active
#define L2_ACTION_IRQ      7  ///< the VCPU's IRQ pin is active
#define L2_ACTION_CKPT     8  ///< do a checkpoint
#define L2_ACTION_WFI      9  ///< wait for interrupt
#define L2_ACTION_TIMER   10  ///< timer event
#define L2_ACTION_BALLOON 11  ///< balloon trigger

#define ACTION_GDB      (1 << L2_ACTION_GDB)
#define ACTION_MKSCK    (1 << L2_ACTION_MKSCK)
#define ACTION_ABORT    (1 << L2_ACTION_ABORT)
#define ACTION_HALT     (1 << L2_ACTION_HALT)
#define ACTION_IRQ      (1 << L2_ACTION_IRQ)
#define ACTION_FIQ      (1 << L2_ACTION_FIQ)
#define ACTION_CKPT     (1 << L2_ACTION_CKPT)
#define ACTION_WFI      (1 << L2_ACTION_WFI)
#define ACTION_TIMER    (1 << L2_ACTION_TIMER)
#define ACTION_BALLOON  (1 << L2_ACTION_BALLOON)

#endif
