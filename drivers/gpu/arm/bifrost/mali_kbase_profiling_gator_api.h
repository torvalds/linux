/*
 *
 * (C) COPYRIGHT 2010, 2013 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */

/**
 * @file mali_kbase_profiling_gator_api.h
 * Model interface
 */

#ifndef _KBASE_PROFILING_GATOR_API_H_
#define _KBASE_PROFILING_GATOR_API_H_

/*
 * List of possible actions to be controlled by Streamline.
 * The following numbers are used by gator to control
 * the frame buffer dumping and s/w counter reporting.
 */
#define FBDUMP_CONTROL_ENABLE (1)
#define FBDUMP_CONTROL_RATE (2)
#define SW_COUNTER_ENABLE (3)
#define FBDUMP_CONTROL_RESIZE_FACTOR (4)
#define FBDUMP_CONTROL_MAX (5)
#define FBDUMP_CONTROL_MIN FBDUMP_CONTROL_ENABLE

void _mali_profiling_control(u32 action, u32 value);

#endif				/* _KBASE_PROFILING_GATOR_API */
