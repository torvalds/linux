/*
 * Copyright (C) 2012 Google, Inc.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _UAPI_LINUX_SYNC_H
#define _UAPI_LINUX_SYNC_H

#include <linux/ioctl.h>
#include <linux/types.h>

/**
 * struct sync_merge_data - data passed to merge ioctl
 * @fd2:	file descriptor of second fence
 * @name:	name of new fence
 * @fence:	returns the fd of the new fence to userspace
 */
struct sync_merge_data {
	__s32	fd2; /* fd of second fence */
	char	name[32]; /* name of new fence */
	__s32	fence; /* fd on newly created fence */
};

/**
 * struct sync_fence_info - detailed fence information
 * @obj_name:		name of parent sync_timeline
 * @driver_name:	name of driver implementing the parent
 * @status:		status of the fence 0:active 1:signaled <0:error
 * @timestamp_ns:	timestamp of status change in nanoseconds
 */
struct sync_fence_info {
	char	obj_name[32];
	char	driver_name[32];
	__s32	status;
	__u64	timestamp_ns;
};

/**
 * struct sync_file_info - data returned from fence info ioctl
 * @len:	ioctl caller writes the size of the buffer its passing in.
 *		ioctl returns length of sync_file_info returned to
 *		userspace including pt_info.
 * @name:	name of fence
 * @status:	status of fence. 1: signaled 0:active <0:error
 * @sync_fence_info: array of sync_fence_info for every fence in the sync_file
 */
struct sync_file_info {
	__u32	len;
	char	name[32];
	__s32	status;

	__u8	sync_fence_info[0];
};

#define SYNC_IOC_MAGIC		'>'

/**
 * DOC: SYNC_IOC_MERGE - merge two fences
 *
 * Takes a struct sync_merge_data.  Creates a new fence containing copies of
 * the sync_pts in both the calling fd and sync_merge_data.fd2.  Returns the
 * new fence's fd in sync_merge_data.fence
 */
#define SYNC_IOC_MERGE		_IOWR(SYNC_IOC_MAGIC, 1, struct sync_merge_data)

/**
 * DOC: SYNC_IOC_FENCE_INFO - get detailed information on a fence
 *
 * Takes a struct sync_file_info_data with extra space allocated for pt_info.
 * Caller should write the size of the buffer into len.  On return, len is
 * updated to reflect the total size of the sync_file_info_data including
 * pt_info.
 *
 * pt_info is a buffer containing sync_pt_infos for every sync_pt in the fence.
 * To iterate over the sync_pt_infos, use the sync_pt_info.len field.
 */
#define SYNC_IOC_FENCE_INFO	_IOWR(SYNC_IOC_MAGIC, 2, struct sync_file_info)

#endif /* _UAPI_LINUX_SYNC_H */
