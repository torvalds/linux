/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_osk_notification.c
 * Implementation of the OS abstraction layer for the kernel device driver
 */

#include "mali_osk.h"
#include "mali_kernel_common.h"

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

/**
 * Declaration of the notification queue object type
 * Contains a linked list of notification pending delivery to user space.
 * It also contains a wait queue of exclusive waiters blocked in the ioctl
 * When a new notification is posted a single thread is resumed.
 */
struct _mali_osk_notification_queue_t_struct
{
	spinlock_t mutex; /**< Mutex protecting the list */
	wait_queue_head_t receive_queue; /**< Threads waiting for new entries to the queue */
	struct list_head head; /**< List of notifications waiting to be picked up */
};

typedef struct _mali_osk_notification_wrapper_t_struct
{
	struct list_head list;           /**< Internal linked list variable */
	_mali_osk_notification_t data;   /**< Notification data */
} _mali_osk_notification_wrapper_t;

_mali_osk_notification_queue_t *_mali_osk_notification_queue_init( void )
{
	_mali_osk_notification_queue_t *	result;

	result = (_mali_osk_notification_queue_t *)kmalloc(sizeof(_mali_osk_notification_queue_t), GFP_KERNEL);
	if (NULL == result) return NULL;

	spin_lock_init(&result->mutex);
	init_waitqueue_head(&result->receive_queue);
	INIT_LIST_HEAD(&result->head);

	return result;
}

_mali_osk_notification_t *_mali_osk_notification_create( u32 type, u32 size )
{
	/* OPT Recycling of notification objects */
	_mali_osk_notification_wrapper_t *notification;

	notification = (_mali_osk_notification_wrapper_t *)kmalloc( sizeof(_mali_osk_notification_wrapper_t) + size,
	                                                            GFP_KERNEL | __GFP_HIGH | __GFP_REPEAT);
	if (NULL == notification)
	{
		MALI_DEBUG_PRINT(1, ("Failed to create a notification object\n"));
		return NULL;
	}

	/* Init the list */
	INIT_LIST_HEAD(&notification->list);

	if (0 != size)
	{
		notification->data.result_buffer = ((u8*)notification) + sizeof(_mali_osk_notification_wrapper_t);
	}
	else
	{
		notification->data.result_buffer = NULL;
	}

	/* set up the non-allocating fields */
	notification->data.notification_type = type;
	notification->data.result_buffer_size = size;

	/* all ok */
	return &(notification->data);
}

void _mali_osk_notification_delete( _mali_osk_notification_t *object )
{
	_mali_osk_notification_wrapper_t *notification;
	MALI_DEBUG_ASSERT_POINTER( object );

	notification = container_of( object, _mali_osk_notification_wrapper_t, data );

	/* Free the container */
	kfree(notification);
}

void _mali_osk_notification_queue_term( _mali_osk_notification_queue_t *queue )
{
	MALI_DEBUG_ASSERT_POINTER( queue );

	/* not much to do, just free the memory */
	kfree(queue);
}

void _mali_osk_notification_queue_send( _mali_osk_notification_queue_t *queue, _mali_osk_notification_t *object )
{
#if defined(MALI_UPPER_HALF_SCHEDULING)
	unsigned long irq_flags;
#endif

	_mali_osk_notification_wrapper_t *notification;
	MALI_DEBUG_ASSERT_POINTER( queue );
	MALI_DEBUG_ASSERT_POINTER( object );

	notification = container_of( object, _mali_osk_notification_wrapper_t, data );

#if defined(MALI_UPPER_HALF_SCHEDULING)
	spin_lock_irqsave(&queue->mutex, irq_flags);
#else
	spin_lock(&queue->mutex);
#endif

	list_add_tail(&notification->list, &queue->head);

#if defined(MALI_UPPER_HALF_SCHEDULING)
	spin_unlock_irqrestore(&queue->mutex, irq_flags);
#else
	spin_unlock(&queue->mutex);
#endif

	/* and wake up one possible exclusive waiter */
	wake_up(&queue->receive_queue);
}

_mali_osk_errcode_t _mali_osk_notification_queue_dequeue( _mali_osk_notification_queue_t *queue, _mali_osk_notification_t **result )
{
#if defined(MALI_UPPER_HALF_SCHEDULING)
	unsigned long irq_flags;
#endif

	_mali_osk_errcode_t ret = _MALI_OSK_ERR_ITEM_NOT_FOUND;
	_mali_osk_notification_wrapper_t *wrapper_object;

#if defined(MALI_UPPER_HALF_SCHEDULING)
	spin_lock_irqsave(&queue->mutex, irq_flags);
#else
	spin_lock(&queue->mutex);
#endif

	if (!list_empty(&queue->head))
	{
		wrapper_object = list_entry(queue->head.next, _mali_osk_notification_wrapper_t, list);
		*result = &(wrapper_object->data);
		list_del_init(&wrapper_object->list);
		ret = _MALI_OSK_ERR_OK;
	}

#if defined(MALI_UPPER_HALF_SCHEDULING)
	spin_unlock_irqrestore(&queue->mutex, irq_flags);
#else
	spin_unlock(&queue->mutex);
#endif

	return ret;
}

_mali_osk_errcode_t _mali_osk_notification_queue_receive( _mali_osk_notification_queue_t *queue, _mali_osk_notification_t **result )
{
    /* check input */
	MALI_DEBUG_ASSERT_POINTER( queue );
	MALI_DEBUG_ASSERT_POINTER( result );

	/* default result */
	*result = NULL;

	if (wait_event_interruptible(queue->receive_queue,
	                             _MALI_OSK_ERR_OK == _mali_osk_notification_queue_dequeue(queue, result)))
	{
		return _MALI_OSK_ERR_RESTARTSYSCALL;
	}

	return _MALI_OSK_ERR_OK; /* all ok */
}
