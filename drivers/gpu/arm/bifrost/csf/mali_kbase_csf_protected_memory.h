/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2019-2022 ARM Limited. All rights reserved.
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

#ifndef _KBASE_CSF_PROTECTED_MEMORY_H_
#define _KBASE_CSF_PROTECTED_MEMORY_H_

#include "mali_kbase.h"
/**
 * kbase_csf_protected_memory_init - Initilaise protected memory allocator.
 *
 * @kbdev:	Device pointer.
 *
 * Return: 0 if success, or an error code on failure.
 */
int kbase_csf_protected_memory_init(struct kbase_device *const kbdev);

/**
 * kbase_csf_protected_memory_term - Terminate prtotected memory allocator.
 *
 * @kbdev:	Device pointer.
 */
void kbase_csf_protected_memory_term(struct kbase_device *const kbdev);

/**
 * kbase_csf_protected_memory_alloc - Allocate protected memory pages.
 *
 * @kbdev:	Device pointer.
 * @phys:	Array of physical addresses to be filled in by the protected
 *		memory allocator.
 * @num_pages:	Number of pages requested to be allocated.
 * @is_small_page: Flag used to select the order of protected memory page.
 *
 * Return: Pointer to an array of protected memory allocations on success,
 *		or NULL on failure.
 */
struct protected_memory_allocation **
	kbase_csf_protected_memory_alloc(
		struct kbase_device *const kbdev,
		struct tagged_addr *phys,
		size_t num_pages,
		bool is_small_page);

/**
 * kbase_csf_protected_memory_free - Free the allocated
 *					protected memory pages
 *
 * @kbdev:	Device pointer.
 * @pma:	Array of pointer to protected memory allocations.
 * @num_pages:	Number of pages to be freed.
 * @is_small_page: Flag used to select the order of protected memory page.
 */
void kbase_csf_protected_memory_free(
		struct kbase_device *const kbdev,
		struct protected_memory_allocation **pma,
		size_t num_pages,
		bool is_small_page);
#endif
