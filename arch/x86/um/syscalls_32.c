/* 
 * Copyright (C) 2000 - 2003 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include <linux/syscalls.h>
#include <sysdep/syscalls.h>

/*
 * The prototype on i386 is:
 *
 *     int clone(int flags, void * child_stack, int * parent_tidptr, struct user_desc * newtls
 *
 * and the "newtls" arg. on i386 is read by copy_thread directly from the
 * register saved on the stack.
 */
long i386_clone(unsigned long clone_flags, unsigned long newsp,
		int __user *parent_tid, void *newtls, int __user *child_tid)
{
	return sys_clone(clone_flags, newsp, parent_tid, child_tid);
}


long sys_sigaction(int sig, const struct old_sigaction __user *act,
			 struct old_sigaction __user *oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	if (act) {
		old_sigset_t mask;
		if (!access_ok(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user(new_ka.sa.sa_handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_restorer, &act->sa_restorer) ||
		    __get_user(new_ka.sa.sa_flags, &act->sa_flags) ||
		    __get_user(mask, &act->sa_mask))
			return -EFAULT;
		siginitset(&new_ka.sa.sa_mask, mask);
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (!access_ok(VERIFY_WRITE, oact, sizeof(*oact)) ||
		    __put_user(old_ka.sa.sa_handler, &oact->sa_handler) ||
		    __put_user(old_ka.sa.sa_restorer, &oact->sa_restorer) ||
		    __put_user(old_ka.sa.sa_flags, &oact->sa_flags) ||
		    __put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask))
			return -EFAULT;
	}

	return ret;
}
