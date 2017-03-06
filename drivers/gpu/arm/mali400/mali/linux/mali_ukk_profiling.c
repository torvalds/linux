/*
 * Copyright (C) 2010-2017 ARM Limited. All rights reserved.
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

int profiling_get_stream_fd_wrapper(struct mali_session_data *session_data, _mali_uk_profiling_stream_fd_get_s __user *uargs)
{
	_mali_uk_profiling_stream_fd_get_s kargs;
	_mali_osk_errcode_t err;

	MALI_CHECK_NON_NULL(uargs, -EINVAL);

	if (0 != copy_from_user(&kargs, uargs, sizeof(_mali_uk_profiling_stream_fd_get_s))) {
		return -EFAULT;
	}

	kargs.ctx = (uintptr_t)session_data;
	err = _mali_ukk_profiling_stream_fd_get(&kargs);
	if (_MALI_OSK_ERR_OK != err) {
		return map_errcode(err);
	}

	if (0 != copy_to_user(uargs, &kargs, sizeof(_mali_uk_profiling_stream_fd_get_s))) {
		return -EFAULT;
	}

	return 0;
}

int profiling_control_set_wrapper(struct mali_session_data *session_data, _mali_uk_profiling_control_set_s __user *uargs)
{
	_mali_uk_profiling_control_set_s kargs;
	_mali_osk_errcode_t err;
	u8 *kernel_control_data = NULL;
	u8 *kernel_response_data = NULL;

	MALI_CHECK_NON_NULL(uargs, -EINVAL);

	if (0 != get_user(kargs.control_packet_size, &uargs->control_packet_size)) return -EFAULT;
	if (0 != get_user(kargs.response_packet_size, &uargs->response_packet_size)) return -EFAULT;

	kargs.ctx = (uintptr_t)session_data;


	/* Sanity check about the size */
	if (kargs.control_packet_size > PAGE_SIZE || kargs.response_packet_size > PAGE_SIZE)
		return -EINVAL;

	if (0 !=  kargs.control_packet_size) {

		if (0 == kargs.response_packet_size)
			return -EINVAL;

		kernel_control_data = _mali_osk_calloc(1, kargs.control_packet_size);
		if (NULL == kernel_control_data) {
			return -ENOMEM;
		}

		kernel_response_data = _mali_osk_calloc(1, kargs.response_packet_size);
		if (NULL == kernel_response_data) {
			_mali_osk_free(kernel_control_data);
			return -ENOMEM;
		}

		kargs.control_packet_data = (uintptr_t)kernel_control_data;
		kargs.response_packet_data = (uintptr_t)kernel_response_data;

		if (0 != copy_from_user((void *)(uintptr_t)kernel_control_data, (void *)(uintptr_t)uargs->control_packet_data, kargs.control_packet_size)) {
			_mali_osk_free(kernel_control_data);
			_mali_osk_free(kernel_response_data);
			return -EFAULT;
		}

		err = _mali_ukk_profiling_control_set(&kargs);
		if (_MALI_OSK_ERR_OK != err) {
			_mali_osk_free(kernel_control_data);
			_mali_osk_free(kernel_response_data);
			return map_errcode(err);
		}

		if (0 != kargs.response_packet_size && 0 != copy_to_user(((void *)(uintptr_t)uargs->response_packet_data), ((void *)(uintptr_t)kargs.response_packet_data), kargs.response_packet_size)) {
			_mali_osk_free(kernel_control_data);
			_mali_osk_free(kernel_response_data);
			return -EFAULT;
		}

		if (0 != put_user(kargs.response_packet_size, &uargs->response_packet_size)) {
			_mali_osk_free(kernel_control_data);
			_mali_osk_free(kernel_response_data);
			return -EFAULT;
		}

		_mali_osk_free(kernel_control_data);
		_mali_osk_free(kernel_response_data);
	} else {

		err = _mali_ukk_profiling_control_set(&kargs);
		if (_MALI_OSK_ERR_OK != err) {
			return map_errcode(err);
		}

	}
	return 0;
}
