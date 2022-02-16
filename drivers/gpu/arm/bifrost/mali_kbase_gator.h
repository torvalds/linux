/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2011-2021 ARM Limited. All rights reserved.
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

/* NB taken from gator  */
/*
 * List of possible actions to be controlled by DS-5 Streamline.
 * The following numbers are used by gator to control the frame buffer dumping
 * and s/w counter reporting. We cannot use the enums in mali_uk_types.h because
 * they are unknown inside gator.
 */

#ifndef _KBASE_GATOR_H_
#define _KBASE_GATOR_H_

#include <linux/types.h>

#define GATOR_JOB_SLOT_START 1
#define GATOR_JOB_SLOT_STOP  2
#define GATOR_JOB_SLOT_SOFT_STOPPED  3

#ifdef CONFIG_MALI_BIFROST_GATOR_SUPPORT

#define GATOR_MAKE_EVENT(type, number) (((type) << 24) | ((number) << 16))

struct kbase_context;

void kbase_trace_mali_job_slots_event(u32 dev_id, u32 event, const struct kbase_context *kctx, u8 atom_id);
void kbase_trace_mali_pm_status(u32 dev_id, u32 event, u64 value);
void kbase_trace_mali_page_fault_insert_pages(u32 dev_id, int event, u32 value);
void kbase_trace_mali_total_alloc_pages_change(u32 dev_id, long long event);

#endif /* CONFIG_MALI_BIFROST_GATOR_SUPPORT */

#endif  /* _KBASE_GATOR_H_ */
