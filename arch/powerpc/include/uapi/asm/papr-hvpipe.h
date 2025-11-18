/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_PAPR_HVPIPE_H_
#define _UAPI_PAPR_HVPIPE_H_

#include <linux/types.h>
#include <asm/ioctl.h>
#include <asm/papr-miscdev.h>

/*
 * This header is included in payload between OS and the user
 * space.
 * flags: OS notifies the user space whether the hvpipe is
 *        closed or the buffer has the payload.
 */
struct papr_hvpipe_hdr {
	__u8 version;
	__u8 reserved[3];
	__u32 flags;
	__u8 reserved2[40];
};

/*
 * ioctl for /dev/papr-hvpipe
 */
#define PAPR_HVPIPE_IOC_CREATE_HANDLE	_IOW(PAPR_MISCDEV_IOC_ID, 9, __u32)

/*
 * hvpipe_hdr flags used for read()
 */
#define HVPIPE_MSG_AVAILABLE	0x01 /* Payload is available */
#define HVPIPE_LOST_CONNECTION	0x02 /* Pipe connection is closed/unavailable */

#endif /* _UAPI_PAPR_HVPIPE_H_ */
