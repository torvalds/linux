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
 * @brief various comm event signaling types and signatures
 */

#ifndef _COMM_EV_H
#define _COMM_EV_H

#define INCLUDE_ALLOW_PV
#define INCLUDE_ALLOW_GPL
#define INCLUDE_ALLOW_MONITOR
#define INCLUDE_ALLOW_MODULE
#include "include_check.h"

/**
 * @name Identifiers of comm event signaling class methods
 * @{
 */
#define MVP_COMM_EV_SIGNATURE       0x4d4d4f43                   ///< 'COMM'
#define MVP_COMM_EV_SIGNAL          (MVP_OBJECT_CUSTOM_BASE + 0) ///< Signal host
#define MVP_COMM_EV_READ_EVENT_DATA (MVP_OBJECT_CUSTOM_BASE + 1) ///< read event data
#define MVP_COMM_EV_LAST            (MVP_OBJECT_CUSTOM_BASE + 2) ///< Number of methods
/**@}*/

typedef struct CommEvent {
   CommTranspID id;
   CommTranspIOEvent event;
} CommEvent;

#endif
