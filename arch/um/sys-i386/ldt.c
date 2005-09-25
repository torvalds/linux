/*
 * Copyright (C) 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/config.h"
#include "linux/sched.h"
#include "linux/slab.h"
#include "linux/types.h"
#include "asm/uaccess.h"
#include "asm/ptrace.h"
#include "asm/smp.h"
#include "asm/ldt.h"
#include "choose-mode.h"
#include "kern.h"
#include "mode_kern.h"

#ifdef CONFIG_MODE_TT

extern int modify_ldt(int func, void *ptr, unsigned long bytecount);

static int do_modify_ldt_tt(int func, void *ptr, unsigned long bytecount)
{
	return modify_ldt(func, ptr, bytecount);
}

#endif

#ifdef CONFIG_MODE_SKAS

#include "skas.h"
#include "skas_ptrace.h"

static int do_modify_ldt_skas(int func, void *ptr, unsigned long bytecount)
{
	struct ptrace_ldt ldt;
	u32 cpu;
	int res;

	ldt = ((struct ptrace_ldt) { .func	= func,
				     .ptr	= ptr,
				     .bytecount = bytecount });

	cpu = get_cpu();
	res = ptrace(PTRACE_LDT, userspace_pid[cpu], 0, (unsigned long) &ldt);
	put_cpu();

	return res;
}
#endif

int sys_modify_ldt(int func, void __user *ptr, unsigned long bytecount)
{
	struct user_desc info;
	int res = 0;
	void *buf = NULL;
	void *p = NULL; /* What we pass to host. */

	switch(func){
	case 1:
	case 0x11: /* write_ldt */
		/* Do this check now to avoid overflows. */
		if (bytecount != sizeof(struct user_desc)) {
			res = -EINVAL;
			goto out;
		}

		if(copy_from_user(&info, ptr, sizeof(info))) {
			res = -EFAULT;
			goto out;
		}

		p = &info;
		break;
	case 0:
	case 2: /* read_ldt */

		/* The use of info avoids kmalloc on the write case, not on the
		 * read one. */
		buf = kmalloc(bytecount, GFP_KERNEL);
		if (!buf) {
			res = -ENOMEM;
			goto out;
		}
		p = buf;
		break;
	default:
		res = -ENOSYS;
		goto out;
	}

	res = CHOOSE_MODE_PROC(do_modify_ldt_tt, do_modify_ldt_skas, func,
				p, bytecount);
	if(res < 0)
		goto out;

	switch(func){
	case 0:
	case 2:
		/* Modify_ldt was for reading and returned the number of read
		 * bytes.*/
		if(copy_to_user(ptr, p, res))
			res = -EFAULT;
		break;
	}

out:
	kfree(buf);
	return res;
}
