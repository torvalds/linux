/*
 * Copyright (C) 2012, 2014-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_EXECUTOR_H__
#define __MALI_EXECUTOR_H__

#include "mali_osk.h"
#include "mali_scheduler_types.h"
#include "mali_kernel_common.h"

typedef enum {
	MALI_EXECUTOR_HINT_GP_BOUND = 0
#define MALI_EXECUTOR_HINT_MAX        1
} mali_executor_hint;

extern mali_bool mali_executor_hints[MALI_EXECUTOR_HINT_MAX];

/* forward declare struct instead of using include */
struct mali_session_data;
struct mali_group;
struct mali_pp_core;

extern _mali_osk_spinlock_irq_t *mali_executor_lock_obj;

#define MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD() MALI_DEBUG_ASSERT_LOCK_HELD(mali_executor_lock_obj);

_mali_osk_errcode_t mali_executor_initialize(void);
void mali_executor_terminate(void);

void mali_executor_populate(void);
void mali_executor_depopulate(void);

void mali_executor_suspend(void);
void mali_executor_resume(void);

u32 mali_executor_get_num_cores_total(void);
u32 mali_executor_get_num_cores_enabled(void);
struct mali_pp_core *mali_executor_get_virtual_pp(void);
struct mali_group *mali_executor_get_virtual_group(void);

void mali_executor_zap_all_active(struct mali_session_data *session);

/**
 * Schedule GP and PP according to bitmask.
 *
 * @param mask A scheduling bitmask.
 * @param deferred_schedule MALI_TRUE if schedule should be deferred, MALI_FALSE if not.
 */
void mali_executor_schedule_from_mask(mali_scheduler_mask mask, mali_bool deferred_schedule);

_mali_osk_errcode_t mali_executor_interrupt_gp(struct mali_group *group, mali_bool in_upper_half);
_mali_osk_errcode_t mali_executor_interrupt_pp(struct mali_group *group, mali_bool in_upper_half);
_mali_osk_errcode_t mali_executor_interrupt_mmu(struct mali_group *group, mali_bool in_upper_half);
void mali_executor_group_power_up(struct mali_group *groups[], u32 num_groups);
void mali_executor_group_power_down(struct mali_group *groups[], u32 num_groups);

void mali_executor_abort_session(struct mali_session_data *session);

void mali_executor_core_scaling_enable(void);
void mali_executor_core_scaling_disable(void);
mali_bool mali_executor_core_scaling_is_enabled(void);

void mali_executor_group_enable(struct mali_group *group);
void mali_executor_group_disable(struct mali_group *group);
mali_bool mali_executor_group_is_disabled(struct mali_group *group);

int mali_executor_set_perf_level(unsigned int target_core_nr, mali_bool override);

#if MALI_STATE_TRACKING
u32 mali_executor_dump_state(char *buf, u32 size);
#endif

MALI_STATIC_INLINE void mali_executor_hint_enable(mali_executor_hint hint)
{
	MALI_DEBUG_ASSERT(hint < MALI_EXECUTOR_HINT_MAX);
	mali_executor_hints[hint] = MALI_TRUE;
}

MALI_STATIC_INLINE void mali_executor_hint_disable(mali_executor_hint hint)
{
	MALI_DEBUG_ASSERT(hint < MALI_EXECUTOR_HINT_MAX);
	mali_executor_hints[hint] = MALI_FALSE;
}

MALI_STATIC_INLINE mali_bool mali_executor_hint_is_enabled(mali_executor_hint hint)
{
	MALI_DEBUG_ASSERT(hint < MALI_EXECUTOR_HINT_MAX);
	return mali_executor_hints[hint];
}

void mali_executor_running_status_print(void);
void mali_executor_status_dump(void);
void mali_executor_lock(void);
void mali_executor_unlock(void);
#endif /* __MALI_EXECUTOR_H__ */
