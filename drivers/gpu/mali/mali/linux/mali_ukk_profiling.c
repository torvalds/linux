/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <linux/fs.h>       /* file system operations */
#include <asm/uaccess.h>    /* user space access */
#include <linux/slab.h>

#include "mali_ukk.h"
#include "mali_osk.h"
#include "mali_kernel_common.h"
#include "mali_session.h"
#include "mali_ukk_wrappers.h"

int profiling_start_wrapper(struct mali_session_data *session_data, _mali_uk_profiling_start_s __user *uargs)
{
	_mali_uk_profiling_start_s kargs;
	_mali_osk_errcode_t err;

	MALI_CHECK_NON_NULL(uargs, -EINVAL);

	if (0 != copy_from_user(&kargs, uargs, sizeof(_mali_uk_profiling_start_s)))
	{
		return -EFAULT;
	}

	kargs.ctx = session_data;
	err = _mali_ukk_profiling_start(&kargs);
	if (_MALI_OSK_ERR_OK != err)
	{
		return map_errcode(err);
	}

	if (0 != put_user(kargs.limit, &uargs->limit))
	{
		return -EFAULT;
	}

	return 0;
}

int profiling_add_event_wrapper(struct mali_session_data *session_data, _mali_uk_profiling_add_event_s __user *uargs)
{
	_mali_uk_profiling_add_event_s kargs;
	_mali_osk_errcode_t err;

	MALI_CHECK_NON_NULL(uargs, -EINVAL);

	if (0 != copy_from_user(&kargs, uargs, sizeof(_mali_uk_profiling_add_event_s)))
	{
		return -EFAULT;
	}

	kargs.ctx = session_data;
	err = _mali_ukk_profiling_add_event(&kargs);
	if (_MALI_OSK_ERR_OK != err)
	{
		return map_errcode(err);
	}

	return 0;
}

int profiling_stop_wrapper(struct mali_session_data *session_data, _mali_uk_profiling_stop_s __user *uargs)
{
	_mali_uk_profiling_stop_s kargs;
	_mali_osk_errcode_t err;

	MALI_CHECK_NON_NULL(uargs, -EINVAL);

	kargs.ctx = session_data;
	err = _mali_ukk_profiling_stop(&kargs);
	if (_MALI_OSK_ERR_OK != err)
	{
		return map_errcode(err);
	}

	if (0 != put_user(kargs.count, &uargs->count))
	{
		return -EFAULT;
	}

	return 0;
}

int profiling_get_event_wrapper(struct mali_session_data *session_data, _mali_uk_profiling_get_event_s __user *uargs)
{
	_mali_uk_profiling_get_event_s kargs;
	_mali_osk_errcode_t err;

	MALI_CHECK_NON_NULL(uargs, -EINVAL);

	if (0 != get_user(kargs.index, &uargs->index))
	{
		return -EFAULT;
	}

	kargs.ctx = session_data;

	err = _mali_ukk_profiling_get_event(&kargs);
	if (_MALI_OSK_ERR_OK != err)
	{
		return map_errcode(err);
	}

	kargs.ctx = NULL; /* prevent kernel address to be returned to user space */
	if (0 != copy_to_user(uargs, &kargs, sizeof(_mali_uk_profiling_get_event_s)))
	{
		return -EFAULT;
	}

	return 0;
}

int profiling_clear_wrapper(struct mali_session_data *session_data, _mali_uk_profiling_clear_s __user *uargs)
{
	_mali_uk_profiling_clear_s kargs;
	_mali_osk_errcode_t err;

	MALI_CHECK_NON_NULL(uargs, -EINVAL);

	kargs.ctx = session_data;
	err = _mali_ukk_profiling_clear(&kargs);
	if (_MALI_OSK_ERR_OK != err)
	{
		return map_errcode(err);
	}

	return 0;
}

int profiling_report_sw_counters_wrapper(struct mali_session_data *session_data, _mali_uk_sw_counters_report_s __user *uargs)
{
	_mali_uk_sw_counters_report_s kargs;
	_mali_osk_errcode_t err;
	u32 *counter_buffer;

	MALI_CHECK_NON_NULL(uargs, -EINVAL);

	if (0 != copy_from_user(&kargs, uargs, sizeof(_mali_uk_sw_counters_report_s)))
	{
		return -EFAULT;
	}

	/* make sure that kargs.num_counters is [at least somewhat] sane */
	if (kargs.num_counters > 10000) {
		MALI_DEBUG_PRINT(1, ("User space attempted to allocate too many counters.\n"));
		return -EINVAL;
	}

	counter_buffer = (u32*)kmalloc(sizeof(u32) * kargs.num_counters, GFP_KERNEL);
	if (NULL == counter_buffer)
	{
		return -ENOMEM;
	}

	if (0 != copy_from_user(counter_buffer, kargs.counters, sizeof(u32) * kargs.num_counters))
	{
		kfree(counter_buffer);
		return -EFAULT;
	}

	kargs.ctx = session_data;
	kargs.counters = counter_buffer;

	err = _mali_ukk_sw_counters_report(&kargs);

	kfree(counter_buffer);

	if (_MALI_OSK_ERR_OK != err)
	{
		return map_errcode(err);
	}

	return 0;
}


