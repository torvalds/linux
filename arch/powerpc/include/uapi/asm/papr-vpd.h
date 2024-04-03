/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_PAPR_VPD_H_
#define _UAPI_PAPR_VPD_H_

#include <asm/ioctl.h>
#include <asm/papr-miscdev.h>

struct papr_location_code {
	/*
	 * PAPR+ v2.13 12.3.2.4 Converged Location Code Rules - Length
	 * Restrictions. 79 characters plus nul.
	 */
	char str[80];
};

/*
 * ioctl for /dev/papr-vpd. Returns a VPD handle fd corresponding to
 * the location code.
 */
#define PAPR_VPD_IOC_CREATE_HANDLE _IOW(PAPR_MISCDEV_IOC_ID, 0, struct papr_location_code)

#endif /* _UAPI_PAPR_VPD_H_ */
