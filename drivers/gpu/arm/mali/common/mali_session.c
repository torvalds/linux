/*
 * Copyright (C) 2012-2013 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_osk.h"
#include "mali_osk_list.h"
#include "mali_session.h"

_MALI_OSK_LIST_HEAD(mali_sessions);
static u32 mali_session_count = 0;

_mali_osk_spinlock_irq_t *mali_sessions_lock;

_mali_osk_errcode_t mali_session_initialize(void)
{
	_MALI_OSK_INIT_LIST_HEAD(&mali_sessions);

	mali_sessions_lock = _mali_osk_spinlock_irq_init(_MALI_OSK_LOCKFLAG_ORDERED, _MALI_OSK_LOCK_ORDER_SESSIONS);

	if (NULL == mali_sessions_lock) return _MALI_OSK_ERR_NOMEM;

	return _MALI_OSK_ERR_OK;
}

void mali_session_terminate(void)
{
	_mali_osk_spinlock_irq_term(mali_sessions_lock);
}

void mali_session_add(struct mali_session_data *session)
{
	mali_session_lock();
	_mali_osk_list_add(&session->link, &mali_sessions);
	mali_session_count++;
	mali_session_unlock();
}

void mali_session_remove(struct mali_session_data *session)
{
	mali_session_lock();
	_mali_osk_list_delinit(&session->link);
	mali_session_count--;
	mali_session_unlock();
}

u32 mali_session_get_count(void)
{
	return mali_session_count;
}

/*
 * Get the max completed window jobs from all active session,
 * which will be used in window render frame per sec calculate
 */
#if defined(CONFIG_MALI400_POWER_PERFORMANCE_POLICY)
u32 mali_session_max_window_num(void)
{
	struct mali_session_data *session, *tmp;
	u32 max_window_num = 0;
	u32 tmp_number = 0;

	mali_session_lock();

	MALI_SESSION_FOREACH(session, tmp, link) {
		tmp_number = _mali_osk_atomic_xchg(&session->number_of_window_jobs, 0);
		if (max_window_num < tmp_number) {
			max_window_num = tmp_number;
		}
	}

	mali_session_unlock();

	return max_window_num;
}
#endif
