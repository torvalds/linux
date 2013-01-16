/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_SESSION_H__
#define __MALI_SESSION_H__

#include "mali_mmu_page_directory.h"
#include "mali_kernel_descriptor_mapping.h"
#include "mali_osk.h"
#include "mali_osk_list.h"

struct mali_session_data
{
	_mali_osk_notification_queue_t * ioctl_queue;

#ifdef CONFIG_SYNC
	_mali_osk_list_t pending_jobs;
	_mali_osk_lock_t *pending_jobs_lock;
#endif

	_mali_osk_lock_t *memory_lock; /**< Lock protecting the vm manipulation */
	mali_descriptor_mapping * descriptor_mapping; /**< Mapping between userspace descriptors and our pointers */
	_mali_osk_list_t memory_head; /**< Track all the memory allocated in this session, for freeing on abnormal termination */

	struct mali_page_directory *page_directory; /**< MMU page directory for this session */

	_MALI_OSK_LIST_HEAD(link); /**< Link for list of all sessions */

	_MALI_OSK_LIST_HEAD(job_list); /**< List of all jobs on this session */
};

_mali_osk_errcode_t mali_session_initialize(void);
void mali_session_terminate(void);

/* List of all sessions. Actual list head in mali_kernel_core.c */
extern _mali_osk_list_t mali_sessions;
/* Lock to protect modification and access to the mali_sessions list */
extern _mali_osk_lock_t *mali_sessions_lock;

MALI_STATIC_INLINE void mali_session_lock(void)
{
	_mali_osk_lock_wait(mali_sessions_lock, _MALI_OSK_LOCKMODE_RW);
}

MALI_STATIC_INLINE void mali_session_unlock(void)
{
	_mali_osk_lock_signal(mali_sessions_lock, _MALI_OSK_LOCKMODE_RW);
}

void mali_session_add(struct mali_session_data *session);
void mali_session_remove(struct mali_session_data *session);
#define MALI_SESSION_FOREACH(session, tmp, link) \
	_MALI_OSK_LIST_FOREACHENTRY(session, tmp, &mali_sessions, struct mali_session_data, link)

MALI_STATIC_INLINE struct mali_page_directory *mali_session_get_page_directory(struct mali_session_data *session)
{
	return session->page_directory;
}

MALI_STATIC_INLINE void mali_session_send_notification(struct mali_session_data *session, _mali_osk_notification_t *object)
{
	_mali_osk_notification_queue_send(session->ioctl_queue, object);
}

#endif /* __MALI_SESSION_H__ */
