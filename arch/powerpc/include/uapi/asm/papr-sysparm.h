/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_PAPR_SYSPARM_H_
#define _UAPI_PAPR_SYSPARM_H_

#include <linux/types.h>
#include <asm/ioctl.h>
#include <asm/papr-miscdev.h>

enum {
	PAPR_SYSPARM_MAX_INPUT  = 1024,
	PAPR_SYSPARM_MAX_OUTPUT = 4000,
};

struct papr_sysparm_io_block {
	__u32 parameter;
	__u16 length;
	__u8 data[PAPR_SYSPARM_MAX_OUTPUT];
};

/**
 * PAPR_SYSPARM_IOC_GET - Retrieve the value of a PAPR system parameter.
 *
 * Uses _IOWR because of one corner case: Retrieving the value of the
 * "OS Service Entitlement Status" parameter (60) requires the caller
 * to supply input data (a date string) in the buffer passed to
 * firmware. So the @length and @data of the incoming
 * papr_sysparm_io_block are always used to initialize the work area
 * supplied to ibm,get-system-parameter. No other parameters are known
 * to parameterize the result this way, and callers are encouraged
 * (but not required) to zero-initialize @length and @data in the
 * common case.
 *
 * On error the contents of the ioblock are indeterminate.
 *
 * Return:
 * 0: Success; @length is the length of valid data in @data, not to exceed @PAPR_SYSPARM_MAX_OUTPUT.
 * -EIO: Platform error. (-1)
 * -EINVAL: Incorrect data length or format. (-9999)
 * -EPERM: The calling partition is not allowed to access this parameter. (-9002)
 * -EOPNOTSUPP: Parameter not supported on this platform (-3)
 */
#define PAPR_SYSPARM_IOC_GET _IOWR(PAPR_MISCDEV_IOC_ID, 1, struct papr_sysparm_io_block)

/**
 * PAPR_SYSPARM_IOC_SET - Update the value of a PAPR system parameter.
 *
 * The contents of the ioblock are unchanged regardless of success.
 *
 * Return:
 * 0: Success; the parameter has been updated.
 * -EIO: Platform error. (-1)
 * -EINVAL: Incorrect data length or format. (-9999)
 * -EPERM: The calling partition is not allowed to access this parameter. (-9002)
 * -EOPNOTSUPP: Parameter not supported on this platform (-3)
 */
#define PAPR_SYSPARM_IOC_SET _IOW(PAPR_MISCDEV_IOC_ID, 2, struct papr_sysparm_io_block)

#endif /* _UAPI_PAPR_SYSPARM_H_ */
