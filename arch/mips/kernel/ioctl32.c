/*
 * ioctl32.c: Conversion between 32bit and 64bit native ioctls.
 *
 * Copyright (C) 2000 Silicon Graphics, Inc.
 * Written by Ulf Carlsson (ulfc@engr.sgi.com)
 * Copyright (C) 2000, 2004 Ralf Baechle
 * Copyright (C) 2002, 2003  Maciej W. Rozycki
 */
#define INCLUDES
#include "compat_ioctl.c"

#include <linux/config.h>
#include <linux/types.h>
#include <linux/compat.h>
#include <linux/ioctl32.h>
#include <linux/syscalls.h>

#ifdef CONFIG_SIBYTE_TBPROF
#include <asm/sibyte/trace_prof.h>
#endif

#define A(__x) ((unsigned long)(__x))

long sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg);

#define CODE
#include "compat_ioctl.c"

typedef int (* ioctl32_handler_t)(unsigned int, unsigned int, unsigned long, struct file *);

#define COMPATIBLE_IOCTL(cmd)		HANDLE_IOCTL((cmd),sys_ioctl)
#define HANDLE_IOCTL(cmd,handler)	{ (cmd), (ioctl32_handler_t)(handler), NULL },
#define IOCTL_TABLE_START \
	struct ioctl_trans ioctl_start[] = {
#define IOCTL_TABLE_END \
	};

IOCTL_TABLE_START

#include <linux/compat_ioctl.h>
#define DECLARES
#include "compat_ioctl.c"

#ifdef CONFIG_SIBYTE_TBPROF
COMPATIBLE_IOCTL(SBPROF_ZBSTART)
COMPATIBLE_IOCTL(SBPROF_ZBSTOP)
COMPATIBLE_IOCTL(SBPROF_ZBWAITFULL)
#endif /* CONFIG_SIBYTE_TBPROF */

/*HANDLE_IOCTL(RTC_IRQP_READ, w_long)
COMPATIBLE_IOCTL(RTC_IRQP_SET)
HANDLE_IOCTL(RTC_EPOCH_READ, w_long)
COMPATIBLE_IOCTL(RTC_EPOCH_SET)
*/

IOCTL_TABLE_END

int ioctl_table_size = ARRAY_SIZE(ioctl_start);
