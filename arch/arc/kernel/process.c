/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Amit Bhor, Kanika Nema: Codito Technologies 2004
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/elf.h>
#include <linux/tick.h>

SYSCALL_DEFINE1(arc_settls, void *, user_tls_data_ptr)
{
	task_thread_info(current)->thr_ptr = (unsigned int)user_tls_data_ptr;
	return 0;
}

/*
 * We return the user space TLS data ptr as sys-call return code
 * Ideally it should be copy to user.
 * However we can cheat by the fact that some sys-calls do return
 * absurdly high values
 * Since the tls dat aptr is not going to be in range of 0xFFFF_xxxx
 * it won't be considered a sys-call error
 * and it will be loads better than copy-to-user, which is a definite
 * D-TLB Miss
 */
SYSCALL_DEFINE0(arc_gettls)
{
	return task_thread_info(current)->thr_ptr;
}
