/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2012-2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#include "mali_scheduler.h"

#include "mali_kernel_common.h"
#include "mali_osk.h"

mali_bool mali_scheduler_hints[MALI_SCHEDULER_HINT_MAX];

static _mali_osk_atomic_t mali_job_id_autonumber;
static _mali_osk_atomic_t mali_job_cache_order_autonumber;

static _mali_osk_wq_work_t *pp_scheduler_wq_high_pri = NULL;
static _mali_osk_wq_work_t *gp_scheduler_wq_high_pri = NULL;

static void mali_scheduler_wq_schedule_pp(void *arg)
{
	MALI_IGNORE(arg);

	mali_pp_scheduler_schedule();
}

static void mali_scheduler_wq_schedule_gp(void *arg)
{
	MALI_IGNORE(arg);

	mali_gp_scheduler_schedule();
}

_mali_osk_errcode_t mali_scheduler_initialize(void)
{
	if ( _MALI_OSK_ERR_OK != _mali_osk_atomic_init(&mali_job_id_autonumber, 0)) {
		MALI_DEBUG_PRINT(1,  ("Initialization of atomic job id counter failed.\n"));
		return _MALI_OSK_ERR_FAULT;
	}

	if ( _MALI_OSK_ERR_OK != _mali_osk_atomic_init(&mali_job_cache_order_autonumber, 0)) {
		MALI_DEBUG_PRINT(1,  ("Initialization of atomic job cache order counter failed.\n"));
		_mali_osk_atomic_term(&mali_job_id_autonumber);
		return _MALI_OSK_ERR_FAULT;
	}

	pp_scheduler_wq_high_pri = _mali_osk_wq_create_work_high_pri(mali_scheduler_wq_schedule_pp, NULL);
	if (NULL == pp_scheduler_wq_high_pri) {
		_mali_osk_atomic_term(&mali_job_cache_order_autonumber);
		_mali_osk_atomic_term(&mali_job_id_autonumber);
		return _MALI_OSK_ERR_NOMEM;
	}

	gp_scheduler_wq_high_pri = _mali_osk_wq_create_work_high_pri(mali_scheduler_wq_schedule_gp, NULL);
	if (NULL == gp_scheduler_wq_high_pri) {
		_mali_osk_wq_delete_work(pp_scheduler_wq_high_pri);
		_mali_osk_atomic_term(&mali_job_cache_order_autonumber);
		_mali_osk_atomic_term(&mali_job_id_autonumber);
		return _MALI_OSK_ERR_NOMEM;
	}

	return _MALI_OSK_ERR_OK;
}

void mali_scheduler_terminate(void)
{
	_mali_osk_wq_delete_work(gp_scheduler_wq_high_pri);
	_mali_osk_wq_delete_work(pp_scheduler_wq_high_pri);
	_mali_osk_atomic_term(&mali_job_cache_order_autonumber);
	_mali_osk_atomic_term(&mali_job_id_autonumber);
}

u32 mali_scheduler_get_new_id(void)
{
	u32 job_id = _mali_osk_atomic_inc_return(&mali_job_id_autonumber);
	return job_id;
}

u32 mali_scheduler_get_new_cache_order(void)
{
	u32 job_cache_order = _mali_osk_atomic_inc_return(&mali_job_cache_order_autonumber);
	return job_cache_order;
}

void mali_scheduler_schedule_from_mask(mali_scheduler_mask mask, mali_bool deferred_schedule)
{
	if (MALI_SCHEDULER_MASK_GP & mask) {
		/* GP needs scheduling. */
		if (deferred_schedule) {
			/* Schedule GP deferred. */
			_mali_osk_wq_schedule_work_high_pri(gp_scheduler_wq_high_pri);
		} else {
			/* Schedule GP now. */
			mali_gp_scheduler_schedule();
		}
	}

	if (MALI_SCHEDULER_MASK_PP & mask) {
		/* PP needs scheduling. */
		if (deferred_schedule) {
			/* Schedule PP deferred. */
			_mali_osk_wq_schedule_work_high_pri(pp_scheduler_wq_high_pri);
		} else {
			/* Schedule PP now. */
			mali_pp_scheduler_schedule();
		}
	}
}
