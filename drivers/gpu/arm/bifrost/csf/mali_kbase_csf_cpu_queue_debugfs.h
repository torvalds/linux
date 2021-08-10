/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2020-2021 ARM Limited. All rights reserved.
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

#ifndef _KBASE_CSF_CPU_QUEUE_DEBUGFS_H_
#define _KBASE_CSF_CPU_QUEUE_DEBUGFS_H_

#include <asm/atomic.h>
#include <linux/types.h>

#include "mali_kbase.h"

/* Forward declaration */
struct base_csf_notification;

#define MALI_CSF_CPU_QUEUE_DEBUGFS_VERSION 0

/* CPU queue dump status */
/* Dumping is done or no dumping is in progress. */
#define BASE_CSF_CPU_QUEUE_DUMP_COMPLETE	0
/* Dumping request is pending. */
#define BASE_CSF_CPU_QUEUE_DUMP_PENDING		1
/* Dumping request is issued to Userspace */
#define BASE_CSF_CPU_QUEUE_DUMP_ISSUED		2


/**
 * kbase_csf_cpu_queue_debugfs_init() - Create a debugfs entry for per context cpu queue(s)
 *
 * @kctx: The kbase_context for which to create the debugfs entry
 */
void kbase_csf_cpu_queue_debugfs_init(struct kbase_context *kctx);

/**
 * kbase_csf_cpu_queue_read_dump_req - Read cpu queue dump request event
 *
 * @kctx: The kbase_context which cpu queue dumpped belongs to
 * @req:  Notification with cpu queue dump request.
 *
 * Return: true if needs CPU queue dump, or false otherwise.
 */
bool kbase_csf_cpu_queue_read_dump_req(struct kbase_context *kctx,
					struct base_csf_notification *req);

/**
 * kbase_csf_cpu_queue_dump_needed - Check the requirement for cpu queue dump
 *
 * @kctx: The kbase_context which cpu queue dumpped belongs to
 *
 * Return: true if it needs cpu queue dump, or false otherwise.
 */
static inline bool kbase_csf_cpu_queue_dump_needed(struct kbase_context *kctx)
{
#if IS_ENABLED(CONFIG_DEBUG_FS)
	return (atomic_read(&kctx->csf.cpu_queue.dump_req_status) ==
		BASE_CSF_CPU_QUEUE_DUMP_ISSUED);
#else
	return false;
#endif
}

/**
 * kbase_csf_cpu_queue_dump - dump buffer containing cpu queue information to debugfs
 *
 * @kctx: The kbase_context which cpu queue dumpped belongs to
 * @buffer: Buffer containing the cpu queue information.
 * @buf_size: Buffer size.
 *
 * Return: Return 0 for dump successfully, or error code.
 */
int kbase_csf_cpu_queue_dump(struct kbase_context *kctx,
		u64 buffer, size_t buf_size);
#endif /* _KBASE_CSF_CPU_QUEUE_DEBUGFS_H_ */
