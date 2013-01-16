/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_osk_misc.c
 * Implementation of the OS abstraction layer for the kernel device driver
 */
#include <linux/kernel.h>
#include <asm/uaccess.h>
#include <asm/cacheflush.h>
#include <linux/sched.h>
#include <linux/module.h>
#include "mali_osk.h"

void _mali_osk_dbgmsg( const char *fmt, ... )
{
    va_list args;
    va_start(args, fmt);
    vprintk(fmt, args);
	va_end(args);
}

u32 _mali_osk_snprintf( char *buf, u32 size, const char *fmt, ... )
{
	int res;
	va_list args;
	va_start(args, fmt);

	res = vscnprintf(buf, (size_t)size, fmt, args);

	va_end(args);
	return res;
}

void _mali_osk_abort(void)
{
	/* make a simple fault by dereferencing a NULL pointer */
	dump_stack();
	*(int *)0 = 0;
}

void _mali_osk_break(void)
{
	_mali_osk_abort();
}

u32 _mali_osk_get_pid(void)
{
	/* Thread group ID is the process ID on Linux */
	return (u32)current->tgid;
}

u32 _mali_osk_get_tid(void)
{
	/* pid is actually identifying the thread on Linux */
	return (u32)current->pid;
}
