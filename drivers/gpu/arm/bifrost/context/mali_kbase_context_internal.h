/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2019-2021 ARM Limited. All rights reserved.
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

#include <mali_kbase.h>

typedef int kbase_context_init_method(struct kbase_context *kctx);
typedef void kbase_context_term_method(struct kbase_context *kctx);

/**
 * struct kbase_context_init - Device init/term methods.
 * @init: Function pointer to a initialise method.
 * @term: Function pointer to a terminate method.
 * @err_mes: Error message to be printed when init method fails.
 */
struct kbase_context_init {
	kbase_context_init_method *init;
	kbase_context_term_method *term;
	char *err_mes;
};

int kbase_context_common_init(struct kbase_context *kctx);
void kbase_context_common_term(struct kbase_context *kctx);

int kbase_context_mem_pool_group_init(struct kbase_context *kctx);
void kbase_context_mem_pool_group_term(struct kbase_context *kctx);

int kbase_context_mmu_init(struct kbase_context *kctx);
void kbase_context_mmu_term(struct kbase_context *kctx);

int kbase_context_mem_alloc_page(struct kbase_context *kctx);
void kbase_context_mem_pool_free(struct kbase_context *kctx);

void kbase_context_sticky_resource_term(struct kbase_context *kctx);

int kbase_context_add_to_dev_list(struct kbase_context *kctx);
void kbase_context_remove_from_dev_list(struct kbase_context *kctx);
