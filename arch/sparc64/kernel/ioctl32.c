/* $Id: ioctl32.c,v 1.136 2002/01/14 09:49:52 davem Exp $
 * ioctl32.c: Conversion between 32bit and 64bit native ioctls.
 *
 * Copyright (C) 1997-2000  Jakub Jelinek  (jakub@redhat.com)
 * Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
 * Copyright (C) 2003  Pavel Machek (pavel@suse.cz)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * ioctls.
 */

#define INCLUDES
#include "compat_ioctl.c"
#include <linux/syscalls.h>

#define CODE
#include "compat_ioctl.c"

#define COMPATIBLE_IOCTL(cmd)		HANDLE_IOCTL((cmd),sys_ioctl)
#define HANDLE_IOCTL(cmd,handler)	{ (cmd), (ioctl_trans_handler_t)(handler), NULL },
#define IOCTL_TABLE_START \
	struct ioctl_trans ioctl_start[] = {
#define IOCTL_TABLE_END \
	};

IOCTL_TABLE_START
#include <linux/compat_ioctl.h>
#define DECLARES
#include "compat_ioctl.c"
#if 0
HANDLE_IOCTL(RTC32_IRQP_READ, do_rtc_ioctl)
HANDLE_IOCTL(RTC32_IRQP_SET, do_rtc_ioctl)
HANDLE_IOCTL(RTC32_EPOCH_READ, do_rtc_ioctl)
HANDLE_IOCTL(RTC32_EPOCH_SET, do_rtc_ioctl)
#endif
/* take care of sizeof(sizeof()) breakage */
IOCTL_TABLE_END

int ioctl_table_size = ARRAY_SIZE(ioctl_start);
