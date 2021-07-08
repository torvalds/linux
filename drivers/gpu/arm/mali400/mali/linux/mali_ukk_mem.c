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
#include <linux/uaccess.h>  /* user space access */

#include "mali_ukk.h"
#include "mali_osk.h"
#include "mali_kernel_common.h"
#include "mali_session.h"
#include "mali_ukk_wrappers.h"

int mem_alloc_wrapper(struct mali_session_data *session_data, _mali_uk_alloc_mem_s __user *uargs)
{
	_mali_uk_alloc_mem_s kargs;
	_mali_osk_errcode_t err;

	MALI_CHECK_NON_NULL(uargs, -EINVAL);
	MALI_CHECK_NON_NULL(session_data, -EINVAL);

	if (0 != copy_from_user(&kargs, uargs, sizeof(_mali_uk_alloc_mem_s))) {
		return -EFAULT;
	}
	kargs.ctx = (uintptr_t)session_data;

	err = _mali_ukk_mem_allocate(&kargs);

	if (_MALI_OSK_ERR_OK != err) {
		return map_errcode(err);
	}

	if (0 != put_user(kargs.backend_handle, &uargs->backend_handle)) {
		return -EFAULT;
	}

	return 0;
}

int mem_free_wrapper(struct mali_session_data *session_data, _mali_uk_free_mem_s __user *uargs)
{
	_mali_uk_free_mem_s kargs;
	_mali_osk_errcode_t err;

	MALI_CHECK_NON_NULL(uargs, -EINVAL);
	MALI_CHECK_NON_NULL(session_data, -EINVAL);

	if (0 != copy_from_user(&kargs, uargs, sizeof(_mali_uk_free_mem_s))) {
		return -EFAULT;
	}
	kargs.ctx = (uintptr_t)session_data;

	err = _mali_ukk_mem_free(&kargs);

	if (_MALI_OSK_ERR_OK != err) {
		return map_errcode(err);
	}

	if (0 != put_user(kargs.free_pages_nr, &uargs->free_pages_nr)) {
		return -EFAULT;
	}

	return 0;
}

int mem_bind_wrapper(struct mali_session_data *session_data, _mali_uk_bind_mem_s __user *uargs)
{
	_mali_uk_bind_mem_s kargs;
	_mali_osk_errcode_t err;

	MALI_CHECK_NON_NULL(uargs, -EINVAL);
	MALI_CHECK_NON_NULL(session_data, -EINVAL);

	if (0 != copy_from_user(&kargs, uargs, sizeof(_mali_uk_bind_mem_s))) {
		return -EFAULT;
	}
	kargs.ctx = (uintptr_t)session_data;

	err = _mali_ukk_mem_bind(&kargs);

	if (_MALI_OSK_ERR_OK != err) {
		return map_errcode(err);
	}

	return 0;
}

int mem_unbind_wrapper(struct mali_session_data *session_data, _mali_uk_unbind_mem_s __user *uargs)
{
	_mali_uk_unbind_mem_s kargs;
	_mali_osk_errcode_t err;

	MALI_CHECK_NON_NULL(uargs, -EINVAL);
	MALI_CHECK_NON_NULL(session_data, -EINVAL);

	if (0 != copy_from_user(&kargs, uargs, sizeof(_mali_uk_unbind_mem_s))) {
		return -EFAULT;
	}
	kargs.ctx = (uintptr_t)session_data;

	err = _mali_ukk_mem_unbind(&kargs);

	if (_MALI_OSK_ERR_OK != err) {
		return map_errcode(err);
	}

	return 0;
}


int mem_cow_wrapper(struct mali_session_data *session_data, _mali_uk_cow_mem_s __user *uargs)
{
	_mali_uk_cow_mem_s kargs;
	_mali_osk_errcode_t err;

	MALI_CHECK_NON_NULL(uargs, -EINVAL);
	MALI_CHECK_NON_NULL(session_data, -EINVAL);

	if (0 != copy_from_user(&kargs, uargs, sizeof(_mali_uk_cow_mem_s))) {
		return -EFAULT;
	}
	kargs.ctx = (uintptr_t)session_data;

	err = _mali_ukk_mem_cow(&kargs);

	if (_MALI_OSK_ERR_OK != err) {
		return map_errcode(err);
	}

	if (0 != put_user(kargs.backend_handle, &uargs->backend_handle)) {
		return -EFAULT;
	}

	return 0;
}

int mem_cow_modify_range_wrapper(struct mali_session_data *session_data, _mali_uk_cow_modify_range_s __user *uargs)
{
	_mali_uk_cow_modify_range_s kargs;
	_mali_osk_errcode_t err;

	MALI_CHECK_NON_NULL(uargs, -EINVAL);
	MALI_CHECK_NON_NULL(session_data, -EINVAL);

	if (0 != copy_from_user(&kargs, uargs, sizeof(_mali_uk_cow_modify_range_s))) {
		return -EFAULT;
	}
	kargs.ctx = (uintptr_t)session_data;

	err = _mali_ukk_mem_cow_modify_range(&kargs);

	if (_MALI_OSK_ERR_OK != err) {
		return map_errcode(err);
	}

	if (0 != put_user(kargs.change_pages_nr, &uargs->change_pages_nr)) {
		return -EFAULT;
	}
	return 0;
}


int mem_resize_mem_wrapper(struct mali_session_data *session_data, _mali_uk_mem_resize_s __user *uargs)
{
	_mali_uk_mem_resize_s kargs;
	_mali_osk_errcode_t err;

	MALI_CHECK_NON_NULL(uargs, -EINVAL);
	MALI_CHECK_NON_NULL(session_data, -EINVAL);

	if (0 != copy_from_user(&kargs, uargs, sizeof(_mali_uk_mem_resize_s))) {
		return -EFAULT;
	}
	kargs.ctx = (uintptr_t)session_data;

	err = _mali_ukk_mem_resize(&kargs);

	if (_MALI_OSK_ERR_OK != err) {
		return map_errcode(err);
	}

	return 0;
}

int mem_write_safe_wrapper(struct mali_session_data *session_data, _mali_uk_mem_write_safe_s __user *uargs)
{
	_mali_uk_mem_write_safe_s kargs;
	_mali_osk_errcode_t err;

	MALI_CHECK_NON_NULL(uargs, -EINVAL);
	MALI_CHECK_NON_NULL(session_data, -EINVAL);

	if (0 != copy_from_user(&kargs, uargs, sizeof(_mali_uk_mem_write_safe_s))) {
		return -EFAULT;
	}

	kargs.ctx = (uintptr_t)session_data;

	/* Check if we can access the buffers */
	if (!access_ok((const void *)(uintptr_t)kargs.dest, kargs.size) ||
	    !access_ok((const void *)(uintptr_t)kargs.src, kargs.size)) {
		return -EINVAL;
	}

	/* Check if size wraps */
	if ((kargs.size + kargs.dest) <= kargs.dest
	    || (kargs.size + kargs.src) <= kargs.src) {
		return -EINVAL;
	}

	err = _mali_ukk_mem_write_safe(&kargs);
	if (_MALI_OSK_ERR_OK != err) {
		return map_errcode(err);
	}

	if (0 != put_user(kargs.size, &uargs->size)) {
		return -EFAULT;
	}

	return 0;
}



int mem_query_mmu_page_table_dump_size_wrapper(struct mali_session_data *session_data, _mali_uk_query_mmu_page_table_dump_size_s __user *uargs)
{
	_mali_uk_query_mmu_page_table_dump_size_s kargs;
	_mali_osk_errcode_t err;

	MALI_CHECK_NON_NULL(uargs, -EINVAL);
	MALI_CHECK_NON_NULL(session_data, -EINVAL);

	kargs.ctx = (uintptr_t)session_data;

	err = _mali_ukk_query_mmu_page_table_dump_size(&kargs);
	if (_MALI_OSK_ERR_OK != err) return map_errcode(err);

	if (0 != put_user(kargs.size, &uargs->size)) return -EFAULT;

	return 0;
}

int mem_dump_mmu_page_table_wrapper(struct mali_session_data *session_data, _mali_uk_dump_mmu_page_table_s __user *uargs)
{
	_mali_uk_dump_mmu_page_table_s kargs;
	_mali_osk_errcode_t err;
	void __user *user_buffer;
	void *buffer = NULL;
	int rc = -EFAULT;

	/* validate input */
	MALI_CHECK_NON_NULL(uargs, -EINVAL);
	/* the session_data pointer was validated by caller */

	if (0 != copy_from_user(&kargs, uargs, sizeof(_mali_uk_dump_mmu_page_table_s)))
		goto err_exit;

	user_buffer = (void __user *)(uintptr_t)kargs.buffer;
	if (!access_ok(user_buffer, kargs.size))
		goto err_exit;

	/* allocate temporary buffer (kernel side) to store mmu page table info */
	if (kargs.size <= 0)
		return -EINVAL;
	/* Allow at most 8MiB buffers, this is more than enough to dump a fully
	 * populated page table. */
	if (kargs.size > SZ_8M)
		return -EINVAL;

	buffer = (void *)(uintptr_t)_mali_osk_valloc(kargs.size);
	if (NULL == buffer) {
		rc = -ENOMEM;
		goto err_exit;
	}

	kargs.ctx = (uintptr_t)session_data;
	kargs.buffer = (uintptr_t)buffer;
	err = _mali_ukk_dump_mmu_page_table(&kargs);
	if (_MALI_OSK_ERR_OK != err) {
		rc = map_errcode(err);
		goto err_exit;
	}

	/* copy mmu page table info back to user space and update pointers */
	if (0 != copy_to_user(user_buffer, buffer, kargs.size))
		goto err_exit;

	kargs.register_writes = kargs.register_writes -
				(uintptr_t)buffer + (uintptr_t)user_buffer;
	kargs.page_table_dump = kargs.page_table_dump -
				(uintptr_t)buffer + (uintptr_t)user_buffer;

	if (0 != copy_to_user(uargs, &kargs, sizeof(kargs)))
		goto err_exit;

	rc = 0;

err_exit:
	if (buffer) _mali_osk_vfree(buffer);
	return rc;
}

int mem_usage_get_wrapper(struct mali_session_data *session_data, _mali_uk_profiling_memory_usage_get_s __user *uargs)
{
	_mali_osk_errcode_t err;
	_mali_uk_profiling_memory_usage_get_s kargs;

	MALI_CHECK_NON_NULL(uargs, -EINVAL);
	MALI_CHECK_NON_NULL(session_data, -EINVAL);

	if (0 != copy_from_user(&kargs, uargs, sizeof(_mali_uk_profiling_memory_usage_get_s))) {
		return -EFAULT;
	}

	kargs.ctx = (uintptr_t)session_data;
	err = _mali_ukk_mem_usage_get(&kargs);
	if (_MALI_OSK_ERR_OK != err) {
		return map_errcode(err);
	}

	kargs.ctx = (uintptr_t)NULL; /* prevent kernel address to be returned to user space */
	if (0 != copy_to_user(uargs, &kargs, sizeof(_mali_uk_profiling_memory_usage_get_s))) {
		return -EFAULT;
	}

	return 0;
}

