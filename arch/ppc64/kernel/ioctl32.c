/* 
 * ioctl32.c: Conversion between 32bit and 64bit native ioctls.
 * 
 * Based on sparc64 ioctl32.c by:
 *
 * Copyright (C) 1997-2000  Jakub Jelinek  (jakub@redhat.com)
 * Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
 *
 * ppc64 changes:
 *
 * Copyright (C) 2000  Ken Aaker (kdaaker@rchland.vnet.ibm.com)
 * Copyright (C) 2001  Anton Blanchard (antonb@au.ibm.com)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * ioctls.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define INCLUDES
#include "compat_ioctl.c"
#include <linux/syscalls.h>

#define CODE
#include "compat_ioctl.c"

#define HANDLE_IOCTL(cmd,handler) { cmd, (ioctl_trans_handler_t)handler, NULL },
#define COMPATIBLE_IOCTL(cmd) HANDLE_IOCTL(cmd,sys_ioctl)

#define IOCTL_TABLE_START \
	struct ioctl_trans ioctl_start[] = {
#define IOCTL_TABLE_END \
	};

IOCTL_TABLE_START
#include <linux/compat_ioctl.h>
#define DECLARES
#include "compat_ioctl.c"

/* Little p (/dev/rtc, /dev/envctrl, etc.) */
COMPATIBLE_IOCTL(_IOR('p', 20, int[7])) /* RTCGET */
COMPATIBLE_IOCTL(_IOW('p', 21, int[7])) /* RTCSET */

IOCTL_TABLE_END

int ioctl_table_size = ARRAY_SIZE(ioctl_start);
