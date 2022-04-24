/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
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
 */

#ifndef _KBASE_DEBUG_MEM_ZONES_H
#define _KBASE_DEBUG_MEM_ZONES_H

#include <mali_kbase.h>

/**
 * kbase_debug_mem_zones_init() - Initialize the mem_zones sysfs file
 * @kctx: Pointer to kernel base context
 *
 * This function creates a "mem_zones" file which can be used to determine the
 * address ranges of GPU memory zones, in the GPU Virtual-Address space.
 *
 * The file is cleaned up by a call to debugfs_remove_recursive() deleting the
 * parent directory.
 */
void kbase_debug_mem_zones_init(struct kbase_context *kctx);

#endif
