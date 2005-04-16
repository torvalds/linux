/*
 * Copyright (C) 2000 - 2003 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include "linux/sched.h"
#include "asm/uaccess.h"

int copy_from_user_tt(void *to, const void __user *from, int n)
{
	if(!access_ok_tt(VERIFY_READ, from, n))
		return(n);

	return(__do_copy_from_user(to, from, n, &current->thread.fault_addr,
				   &current->thread.fault_catcher));
}

int copy_to_user_tt(void __user *to, const void *from, int n)
{
	if(!access_ok_tt(VERIFY_WRITE, to, n))
		return(n);

	return(__do_copy_to_user(to, from, n, &current->thread.fault_addr,
				 &current->thread.fault_catcher));
}

int strncpy_from_user_tt(char *dst, const char __user *src, int count)
{
	int n;

	if(!access_ok_tt(VERIFY_READ, src, 1))
		return(-EFAULT);

	n = __do_strncpy_from_user(dst, src, count,
				   &current->thread.fault_addr,
				   &current->thread.fault_catcher);
	if(n < 0) return(-EFAULT);
	return(n);
}

int __clear_user_tt(void __user *mem, int len)
{
	return(__do_clear_user(mem, len,
			       &current->thread.fault_addr,
			       &current->thread.fault_catcher));
}

int clear_user_tt(void __user *mem, int len)
{
	if(!access_ok_tt(VERIFY_WRITE, mem, len))
		return(len);

	return(__do_clear_user(mem, len, &current->thread.fault_addr,
			       &current->thread.fault_catcher));
}

int strnlen_user_tt(const void __user *str, int len)
{
	return(__do_strnlen_user(str, len,
				 &current->thread.fault_addr,
				 &current->thread.fault_catcher));
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
