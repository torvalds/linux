/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2013-2015, 2019-2021 ARM Limited. All rights reserved.
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

#ifndef _KBASE_DEBUG_MEM_VIEW_H
#define _KBASE_DEBUG_MEM_VIEW_H

#include <mali_kbase.h>

/**
 * kbase_debug_mem_view_init - Initialize the mem_view sysfs file
 * @kctx: Pointer to kernel base context
 *
 * This function creates a "mem_view" file which can be used to get a view of
 * the context's memory as the GPU sees it (i.e. using the GPU's page tables).
 *
 * The file is cleaned up by a call to debugfs_remove_recursive() deleting the
 * parent directory.
 */
void kbase_debug_mem_view_init(struct kbase_context *kctx);

#endif
