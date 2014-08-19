/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2013-2014 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */
#include <linux/fs.h>       /* file system operations */
#include <asm/uaccess.h>    /* user space access */

#include "mali_ukk.h"
#include "mali_osk.h"
#include "mali_kernel_common.h"
#include "mali_session.h"
#include "mali_ukk_wrappers.h"

#include "mali_timeline.h"
#include "mali_timeline_fence_wait.h"
#include "mali_timeline_sync_fence.h"

int timeline_get_latest_point_wrapper(struct mali_session_data *session, _mali_uk_timeline_get_latest_point_s __user *uargs)
{
	u32 val;
	mali_timeline_id timeline;
	mali_timeline_point point;

	MALI_DEBUG_ASSERT_POINTER(session);

	if (0 != get_user(val, &uargs->timeline)) return -EFAULT;

	if (MALI_UK_TIMELINE_MAX <= val) {
		return -EINVAL;
	}

	timeline = (mali_timeline_id)val;

	point = mali_timeline_system_get_latest_point(session->timeline_system, timeline);

	if (0 != put_user(point, &uargs->point)) return -EFAULT;

	return 0;
}

int timeline_wait_wrapper(struct mali_session_data *session, _mali_uk_timeline_wait_s __user *uargs)
{
	u32 timeout, status;
	mali_bool ret;
	_mali_uk_fence_t uk_fence;
	struct mali_timeline_fence fence;

	MALI_DEBUG_ASSERT_POINTER(session);

	if (0 != copy_from_user(&uk_fence, &uargs->fence, sizeof(_mali_uk_fence_t))) return -EFAULT;
	if (0 != get_user(timeout, &uargs->timeout)) return -EFAULT;

	mali_timeline_fence_copy_uk_fence(&fence, &uk_fence);

	ret = mali_timeline_fence_wait(session->timeline_system, &fence, timeout);
	status = (MALI_TRUE == ret ? 1 : 0);

	if (0 != put_user(status, &uargs->status)) return -EFAULT;

	return 0;
}

int timeline_create_sync_fence_wrapper(struct mali_session_data *session, _mali_uk_timeline_create_sync_fence_s __user *uargs)
{
	s32 sync_fd = -1;
	_mali_uk_fence_t uk_fence;
	struct mali_timeline_fence fence;

	MALI_DEBUG_ASSERT_POINTER(session);

	if (0 != copy_from_user(&uk_fence, &uargs->fence, sizeof(_mali_uk_fence_t))) return -EFAULT;
	mali_timeline_fence_copy_uk_fence(&fence, &uk_fence);

#if defined(CONFIG_SYNC)
	sync_fd = mali_timeline_sync_fence_create(session->timeline_system, &fence);
#else
	sync_fd = -1;
#endif /* defined(CONFIG_SYNC) */

	if (0 != put_user(sync_fd, &uargs->sync_fd)) return -EFAULT;

	return 0;
}
