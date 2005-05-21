/*
 * Copyright (C) 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/config.h"
#include "linux/slab.h"
#include "asm/uaccess.h"
#include "asm/ptrace.h"
#include "choose-mode.h"
#include "kern.h"

#ifdef CONFIG_MODE_TT
extern int modify_ldt(int func, void *ptr, unsigned long bytecount);

/* XXX this needs copy_to_user and copy_from_user */

int sys_modify_ldt_tt(int func, void __user *ptr, unsigned long bytecount)
{
	if (!access_ok(VERIFY_READ, ptr, bytecount))
		return -EFAULT;

	return modify_ldt(func, ptr, bytecount);
}
#endif

#ifdef CONFIG_MODE_SKAS
extern int userspace_pid[];

#include "skas_ptrace.h"

int sys_modify_ldt_skas(int func, void __user *ptr, unsigned long bytecount)
{
	struct ptrace_ldt ldt;
	void *buf;
	int res, n;

	buf = kmalloc(bytecount, GFP_KERNEL);
	if(buf == NULL)
		return(-ENOMEM);

	res = 0;

	switch(func){
	case 1:
	case 0x11:
		res = copy_from_user(buf, ptr, bytecount);
		break;
	}

	if(res != 0){
		res = -EFAULT;
		goto out;
	}

	ldt = ((struct ptrace_ldt) { .func	= func,
				     .ptr	= buf,
				     .bytecount = bytecount });
#warning Need to look up userspace_pid by cpu
	res = ptrace(PTRACE_LDT, userspace_pid[0], 0, (unsigned long) &ldt);
	if(res < 0)
		goto out;

	switch(func){
	case 0:
	case 2:
		n = res;
		res = copy_to_user(ptr, buf, n);
		if(res != 0)
			res = -EFAULT;
		else 
			res = n;
		break;
	}

 out:
	kfree(buf);
	return(res);
}
#endif

int sys_modify_ldt(int func, void __user *ptr, unsigned long bytecount)
{
	return(CHOOSE_MODE_PROC(sys_modify_ldt_tt, sys_modify_ldt_skas, func, 
				ptr, bytecount));
}



/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
