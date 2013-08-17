/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_osk_wq.c
 * Implementation of the OS abstraction layer for the kernel device driver
 */

#include <linux/slab.h>	/* For memory allocation */
#include <linux/workqueue.h>
#include <linux/version.h>

#include "mali_osk.h"
#include "mali_kernel_common.h"
#include "mali_kernel_license.h"
#include "mali_kernel_linux.h"

typedef struct _mali_osk_wq_work_t_struct
{
	_mali_osk_wq_work_handler_t handler;
	void *data;
	struct work_struct work_handle;
} mali_osk_wq_work_object_t;

#if MALI_LICENSE_IS_GPL
struct workqueue_struct *mali_wq = NULL;
#endif

static void _mali_osk_wq_work_func ( struct work_struct *work );

_mali_osk_errcode_t _mali_osk_wq_init(void)
{
#if MALI_LICENSE_IS_GPL
	MALI_DEBUG_ASSERT(NULL == mali_wq);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36)
	mali_wq = alloc_workqueue("mali", WQ_UNBOUND, 0);
#else
	mali_wq = create_workqueue("mali");
#endif
	if(NULL == mali_wq)
	{
		MALI_PRINT_ERROR(("Unable to create Mali workqueue\n"));
		return _MALI_OSK_ERR_FAULT;
	}
#endif

	return _MALI_OSK_ERR_OK;
}

void _mali_osk_wq_flush(void)
{
#if MALI_LICENSE_IS_GPL
       flush_workqueue(mali_wq);
#else
       flush_scheduled_work();
#endif
}

void _mali_osk_wq_term(void)
{
#if MALI_LICENSE_IS_GPL
	MALI_DEBUG_ASSERT(NULL != mali_wq);

	flush_workqueue(mali_wq);
	destroy_workqueue(mali_wq);
	mali_wq = NULL;
#else
	flush_scheduled_work();
#endif
}

_mali_osk_wq_work_t *_mali_osk_wq_create_work( _mali_osk_wq_work_handler_t handler, void *data )
{
	mali_osk_wq_work_object_t *work = kmalloc(sizeof(mali_osk_wq_work_object_t), GFP_KERNEL);

	if (NULL == work) return NULL;

	work->handler = handler;
	work->data = data;

	INIT_WORK( &work->work_handle, _mali_osk_wq_work_func );

	return work;
}

void _mali_osk_wq_delete_work( _mali_osk_wq_work_t *work )
{
	mali_osk_wq_work_object_t *work_object = (mali_osk_wq_work_object_t *)work;
	_mali_osk_wq_flush();
	kfree(work_object);
}

void _mali_osk_wq_schedule_work( _mali_osk_wq_work_t *work )
{
	mali_osk_wq_work_object_t *work_object = (mali_osk_wq_work_object_t *)work;
#if MALI_LICENSE_IS_GPL
	queue_work(mali_wq, &work_object->work_handle);
#else
	schedule_work(&work_object->work_handle);
#endif
}

static void _mali_osk_wq_work_func ( struct work_struct *work )
{
	mali_osk_wq_work_object_t *work_object;

	work_object = _MALI_OSK_CONTAINER_OF(work, mali_osk_wq_work_object_t, work_handle);
	work_object->handler(work_object->data);
}

