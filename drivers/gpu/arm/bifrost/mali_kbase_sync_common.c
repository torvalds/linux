/*
 *
 * (C) COPYRIGHT 2012-2019 ARM Limited. All rights reserved.
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

/*
 * @file mali_kbase_sync_common.c
 *
 * Common code for our explicit fence functionality
 */

#include <linux/workqueue.h>
#include "mali_kbase.h"
#include "mali_kbase_sync.h"

void kbase_sync_fence_wait_worker(struct work_struct *data)
{
	struct kbase_jd_atom *katom;

	katom = container_of(data, struct kbase_jd_atom, work);
	kbase_soft_event_wait_callback(katom);
}

const char *kbase_sync_status_string(int status)
{
	if (status == 0)
		return "active";
	else if (status > 0)
		return "signaled";
	else
		return "error";
}
