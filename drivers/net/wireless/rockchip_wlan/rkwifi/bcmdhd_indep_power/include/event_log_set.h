/* SPDX-License-Identifier: GPL-2.0 */
/*
 * EVENT_LOG system definitions
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: event_log_set.h 700076 2017-05-17 14:42:22Z $
 */

#ifndef _EVENT_LOG_SET_H_
#define _EVENT_LOG_SET_H_

/* Set a maximum number of sets here.  It is not dynamic for
 *  efficiency of the EVENT_LOG calls.
 */
#define NUM_EVENT_LOG_SETS 8

/* Define new event log sets here */
#define EVENT_LOG_SET_BUS	0
#define EVENT_LOG_SET_WL	1
#define EVENT_LOG_SET_PSM	2
#define EVENT_LOG_SET_ERROR	3
#define EVENT_LOG_SET_MEM_API	4
/* Share the set with MEM_API for now to limit ROM invalidation.
 * The above set is used in dingo only
 * On trunk, MSCH should move to a different set.
 */
#define EVENT_LOG_SET_MSCH_PROFILER	4
#define EVENT_LOG_SET_ECOUNTERS 5	/* Host to instantiate this for ecounters. */
#define EVENT_LOG_SET_6	6	/* Instantiated by host for channel switch logs */
#define EVENT_LOG_SET_7	7	/* Instantiated by host for AMPDU stats */

#endif /* _EVENT_LOG_SET_H_ */
