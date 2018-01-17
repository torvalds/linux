/*
 *
 * (C) COPYRIGHT 2012-2017 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



/*
 * @file mali_kbase_sync_common.c
 *
 * Common code for our explicit fence functionality
 */

#include <linux/workqueue.h>
#include "mali_kbase.h"

void kbase_sync_fence_wait_worker(struct work_struct *data)
{
	struct kbase_jd_atom *katom;

	katom = container_of(data, struct kbase_jd_atom, work);
	kbase_soft_event_wait_callback(katom);
}

const char *kbase_sync_status_string(int status)
{
	if (status == 0)
		return "signaled";
	else if (status > 0)
		return "active";
	else
		return "error";
}
