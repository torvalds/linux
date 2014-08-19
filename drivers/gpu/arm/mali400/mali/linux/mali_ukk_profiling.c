/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2010-2014 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */
#include <linux/fs.h>       /* file system operations */
#include <asm/uaccess.h>    /* user space access */
#include <linux/slab.h>

#include "mali_ukk.h"
#include "mali_osk.h"
#include "mali_kernel_common.h"
#include "mali_session.h"
#include "mali_ukk_wrappers.h"

int profiling_add_event_wrapper(struct mali_session_data *session_data, _mali_uk_profiling_add_event_s __user *uargs)
{
	_mali_uk_profiling_add_event_s kargs;
	_mali_osk_errcode_t err;

	MALI_CHECK_NON_NULL(uargs, -EINVAL);

	if (0 != copy_from_user(&kargs, uargs, sizeof(_mali_uk_profiling_add_event_s))) {
		return -EFAULT;
	}

	kargs.ctx = (uintptr_t)session_data;
	err = _mali_ukk_profiling_add_event(&kargs);
	if (_MALI_OSK_ERR_OK != err) {
		return map_errcode(err);
	}

	return 0;
}

int profiling_memory_usage_get_wrapper(struct mali_session_data *session_data, _mali_uk_profiling_memory_usage_get_s __user *uargs)
{
	_mali_osk_errcode_t err;
	_mali_uk_profiling_memory_usage_get_s kargs;

	MALI_CHECK_NON_NULL(uargs, -EINVAL);
	MALI_CHECK_NON_NULL(session_data, -EINVAL);

	kargs.ctx = (uintptr_t)session_data;
	err = _mali_ukk_profiling_memory_usage_get(&kargs);
	if (_MALI_OSK_ERR_OK != err) {
		return map_errcode(err);
	}

	kargs.ctx = (uintptr_t)NULL; /* prevent kernel address to be returned to user space */
	if (0 != copy_to_user(uargs, &kargs, sizeof(_mali_uk_profiling_memory_usage_get_s))) {
		return -EFAULT;
	}

	return 0;
}

int profiling_report_sw_counters_wrapper(struct mali_session_data *session_data, _mali_uk_sw_counters_report_s __user *uargs)
{
	_mali_uk_sw_counters_report_s kargs;
	_mali_osk_errcode_t err;
	u32 *counter_buffer;
	u32 __user *counters;

	MALI_CHECK_NON_NULL(uargs, -EINVAL);

	if (0 != copy_from_user(&kargs, uargs, sizeof(_mali_uk_sw_counters_report_s))) {
		return -EFAULT;
	}

	/* make sure that kargs.num_counters is [at least somewhat] sane */
	if (kargs.num_counters > 10000) {
		MALI_DEBUG_PRINT(1, ("User space attempted to allocate too many counters.\n"));
		return -EINVAL;
	}

	counter_buffer = (u32 *)kmalloc(sizeof(u32) * kargs.num_counters, GFP_KERNEL);
	if (NULL == counter_buffer) {
		return -ENOMEM;
	}

	counters = (u32 *)(uintptr_t)kargs.counters;

	if (0 != copy_from_user(counter_buffer, counters, sizeof(u32) * kargs.num_counters)) {
		kfree(counter_buffer);
		return -EFAULT;
	}

	kargs.ctx = (uintptr_t)session_data;
	kargs.counters = (uintptr_t)counter_buffer;

	err = _mali_ukk_sw_counters_report(&kargs);

	kfree(counter_buffer);

	if (_MALI_OSK_ERR_OK != err) {
		return map_errcode(err);
	}

	return 0;
}
