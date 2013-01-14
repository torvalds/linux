/*
 * Copyright (C) 2010, 2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <linux/fs.h>       /* file system operations */
#include <asm/uaccess.h>    /* user space access */

#include "mali_ukk.h"
#include "mali_osk.h"
#include "mali_kernel_common.h"
#include "mali_session.h"
#include "mali_ukk_wrappers.h"

int pp_start_job_wrapper(struct mali_session_data *session_data, _mali_uk_pp_start_job_s __user *uargs)
{
	_mali_osk_errcode_t err;
	int fence = -1;

	MALI_CHECK_NON_NULL(uargs, -EINVAL);
	MALI_CHECK_NON_NULL(session_data, -EINVAL);

	err = _mali_ukk_pp_start_job(session_data, uargs, &fence);
	if (_MALI_OSK_ERR_OK != err) return map_errcode(err);

	if (-1 != fence)
	{
		if (0 != put_user(fence, &uargs->fence)) return -EFAULT;
	}

	return 0;
}

int pp_get_number_of_cores_wrapper(struct mali_session_data *session_data, _mali_uk_get_pp_number_of_cores_s __user *uargs)
{
    _mali_uk_get_pp_number_of_cores_s kargs;
    _mali_osk_errcode_t err;

    MALI_CHECK_NON_NULL(uargs, -EINVAL);
    MALI_CHECK_NON_NULL(session_data, -EINVAL);

    kargs.ctx = session_data;
    err = _mali_ukk_get_pp_number_of_cores(&kargs);
    if (_MALI_OSK_ERR_OK != err) return map_errcode(err);

    if (0 != put_user(kargs.number_of_cores, &uargs->number_of_cores)) return -EFAULT;

    return 0;
}

int pp_get_core_version_wrapper(struct mali_session_data *session_data, _mali_uk_get_pp_core_version_s __user *uargs)
{
    _mali_uk_get_pp_core_version_s kargs;
    _mali_osk_errcode_t err;

    MALI_CHECK_NON_NULL(uargs, -EINVAL);
    MALI_CHECK_NON_NULL(session_data, -EINVAL);

    kargs.ctx = session_data;
    err = _mali_ukk_get_pp_core_version(&kargs);
    if (_MALI_OSK_ERR_OK != err) return map_errcode(err);

    if (0 != put_user(kargs.version, &uargs->version)) return -EFAULT;

    return 0;
}

int pp_disable_wb_wrapper(struct mali_session_data *session_data, _mali_uk_pp_disable_wb_s __user *uargs)
{
	_mali_uk_pp_disable_wb_s kargs;

    MALI_CHECK_NON_NULL(uargs, -EINVAL);
    MALI_CHECK_NON_NULL(session_data, -EINVAL);

    if (0 != copy_from_user(&kargs, uargs, sizeof(_mali_uk_pp_disable_wb_s))) return -EFAULT;

    kargs.ctx = session_data;
    _mali_ukk_pp_job_disable_wb(&kargs);

    return 0;
}
