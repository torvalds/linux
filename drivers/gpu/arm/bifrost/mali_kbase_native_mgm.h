/*
 *
 * (C) COPYRIGHT 2019 ARM Limited. All rights reserved.
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

#ifndef _KBASE_NATIVE_MGM_H_
#define _KBASE_NATIVE_MGM_H_

#include <linux/memory_group_manager.h>

/**
 * kbase_native_mgm_dev - Native memory group manager device
 *
 * An implementation of the memory group manager interface that is intended for
 * internal use when no platform-specific memory group manager is available.
 *
 * It ignores the specified group ID and delegates to the kernel's physical
 * memory allocation and freeing functions.
 */
extern struct memory_group_manager_device kbase_native_mgm_dev;

#endif /* _KBASE_NATIVE_MGM_H_ */
