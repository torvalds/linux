// SPDX-License-Identifier: GPL-2.0
/*
 *  S390 version
 *    Copyright IBM Corp. 1999, 2000
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Thomas Spatzier (tspat@de.ibm.com)
 *
 *  Derived from "arch/i386/kernel/sys_i386.c"
 *
 *  This file contains various random system calls that
 *  have a non-standard calling sequence on the Linux/s390
 *  platform.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/smp.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/syscalls.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/utsname.h>
#include <linux/personality.h>
#include <linux/unistd.h>
#include <linux/ipc.h>
#include <linux/uaccess.h>
#include "entry.h"

/*
 * Perform the mmap() system call. Linux for S/390 isn't able to handle more
 * than 5 system call parameters, so this system call uses a memory block
 * for parameter passing.
 */

struct s390_mmap_arg_struct {
	unsigned long addr;
	unsigned long len;
	unsigned long prot;
	unsigned long flags;
	unsigned long fd;
	unsigned long offset;
};

SYSCALL_DEFINE1(mmap2, struct s390_mmap_arg_struct __user *, arg)
{
	struct s390_mmap_arg_struct a;
	int error = -EFAULT;

	if (copy_from_user(&a, arg, sizeof(a)))
		goto out;
	error = sys_mmap_pgoff(a.addr, a.len, a.prot, a.flags, a.fd, a.offset);
out:
	return error;
}

/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls.
 */
SYSCALL_DEFINE5(s390_ipc, uint, call, int, first, unsigned long, second,
		unsigned long, third, void __user *, ptr)
{
	if (call >> 16)
		return -EINVAL;
	/* The s390 sys_ipc variant has only five parameters instead of six
	 * like the generic variant. The only difference is the handling of
	 * the SEMTIMEDOP subcall where on s390 the third parameter is used
	 * as a pointer to a struct timespec where the generic variant uses
	 * the fifth parameter.
	 * Therefore we can call the generic variant by simply passing the
	 * third parameter also as fifth parameter.
	 */
	return sys_ipc(call, first, second, third, ptr, third);
}

SYSCALL_DEFINE1(s390_personality, unsigned int, personality)
{
	unsigned int ret;

	if (personality(current->personality) == PER_LINUX32 &&
	    personality(personality) == PER_LINUX)
		personality |= PER_LINUX32;
	ret = sys_personality(personality);
	if (personality(ret) == PER_LINUX32)
		ret &= ~PER_LINUX32;

	return ret;
}
