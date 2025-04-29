/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_PAPR_PLATFORM_DUMP_H_
#define _UAPI_PAPR_PLATFORM_DUMP_H_

#include <linux/types.h>
#include <asm/ioctl.h>
#include <asm/papr-miscdev.h>

/*
 * ioctl for /dev/papr-platform-dump. Returns a platform-dump handle fd
 * corresponding to dump tag.
 */
#define PAPR_PLATFORM_DUMP_IOC_CREATE_HANDLE _IOW(PAPR_MISCDEV_IOC_ID, 6, __u64)
#define PAPR_PLATFORM_DUMP_IOC_INVALIDATE    _IOW(PAPR_MISCDEV_IOC_ID, 7, __u64)

#endif /* _UAPI_PAPR_PLATFORM_DUMP_H_ */
