/* $Id: ioctl32.c,v 1.5 2002/10/18 00:21:43 varenet Exp $
 * ioctl32.c: Conversion between 32bit and 64bit native ioctls.
 *
 * Copyright (C) 1997-2000  Jakub Jelinek  (jakub@redhat.com)
 * Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * ioctls.
 */

#include <linux/syscalls.h>

#define INCLUDES
#include "compat_ioctl.c"

#include <asm/perf.h>
#include <asm/ioctls.h>

#define CODE
#include "compat_ioctl.c"

#define HANDLE_IOCTL(cmd, handler) { cmd, (ioctl_trans_handler_t)handler, NULL },
#define COMPATIBLE_IOCTL(cmd) HANDLE_IOCTL(cmd, sys_ioctl) 

#define IOCTL_TABLE_START  struct ioctl_trans ioctl_start[] = {
#define IOCTL_TABLE_END    };

IOCTL_TABLE_START
#include <linux/compat_ioctl.h>

#define DECLARES
#include "compat_ioctl.c"

/* And these ioctls need translation */
HANDLE_IOCTL(SIOCGPPPSTATS, dev_ifsioc)
HANDLE_IOCTL(SIOCGPPPCSTATS, dev_ifsioc)
HANDLE_IOCTL(SIOCGPPPVER, dev_ifsioc)

#if defined(CONFIG_GEN_RTC)
COMPATIBLE_IOCTL(RTC_AIE_ON)
COMPATIBLE_IOCTL(RTC_AIE_OFF)
COMPATIBLE_IOCTL(RTC_UIE_ON)
COMPATIBLE_IOCTL(RTC_UIE_OFF)
COMPATIBLE_IOCTL(RTC_PIE_ON)
COMPATIBLE_IOCTL(RTC_PIE_OFF)
COMPATIBLE_IOCTL(RTC_WIE_ON)
COMPATIBLE_IOCTL(RTC_WIE_OFF)
COMPATIBLE_IOCTL(RTC_ALM_SET)   /* struct rtc_time only has ints */
COMPATIBLE_IOCTL(RTC_ALM_READ)  /* struct rtc_time only has ints */
COMPATIBLE_IOCTL(RTC_RD_TIME)   /* struct rtc_time only has ints */
COMPATIBLE_IOCTL(RTC_SET_TIME)  /* struct rtc_time only has ints */
HANDLE_IOCTL(RTC_IRQP_READ, w_long)
COMPATIBLE_IOCTL(RTC_IRQP_SET)
HANDLE_IOCTL(RTC_EPOCH_READ, w_long)
COMPATIBLE_IOCTL(RTC_EPOCH_SET)
#endif

IOCTL_TABLE_END

int ioctl_table_size = ARRAY_SIZE(ioctl_start);
