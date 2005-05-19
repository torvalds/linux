/*
 * IA32 Architecture-specific ioctl shim code
 *
 * Copyright (C) 2000 VA Linux Co
 * Copyright (C) 2000 Don Dugger <n0ano@valinux.com>
 * Copyright (C) 2001-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/signal.h>	/* argh, msdos_fs.h isn't self-contained... */
#include <linux/syscalls.h>
#include "ia32priv.h"
  
#define	INCLUDES
#include "compat_ioctl.c"

#define IOCTL_NR(a)	((a) & ~(_IOC_SIZEMASK << _IOC_SIZESHIFT))

#define DO_IOCTL(fd, cmd, arg) ({			\
	int _ret;					\
	mm_segment_t _old_fs = get_fs();		\
							\
	set_fs(KERNEL_DS);				\
	_ret = sys_ioctl(fd, cmd, (unsigned long)arg);	\
	set_fs(_old_fs);				\
	_ret;						\
})

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
#define DECLARES
#include "compat_ioctl.c"
#include <linux/compat_ioctl.h>
IOCTL_TABLE_END

int ioctl_table_size = ARRAY_SIZE(ioctl_start);
