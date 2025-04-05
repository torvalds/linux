// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 * Copyright (C) IBM Corporation, 2011
 *
 * Authors: Sukadev Bhattiprolu <sukadev@linux.vnet.ibm.com>
 *          Anton Blanchard <anton@au.ibm.com>
 */
#include <linux/uaccess.h>
#include <linux/hardirq.h>
#include <asm/switch_to.h>

int enter_vmx_usercopy(void)
{
	if (in_interrupt())
		return 0;

	preempt_disable();
	/*
	 * We need to disable page faults as they can call schedule and
	 * thus make us lose the VMX context. So on page faults, we just
	 * fail which will cause a fallback to the normal non-vmx copy.
	 */
	pagefault_disable();

	enable_kernel_altivec();

	return 1;
}

/*
 * This function must return 0 because we tail call optimise when calling
 * from __copy_tofrom_user_power7 which returns 0 on success.
 */
int exit_vmx_usercopy(void)
{
	disable_kernel_altivec();
	pagefault_enable();
	preempt_enable_no_resched();
	/*
	 * Must never explicitly call schedule (including preempt_enable())
	 * while in a kuap-unlocked user copy, because the AMR register will
	 * not be saved and restored across context switch. However preempt
	 * kernels need to be preempted as soon as possible if need_resched is
	 * set and we are preemptible. The hack here is to schedule a
	 * decrementer to fire here and reschedule for us if necessary.
	 */
	if (IS_ENABLED(CONFIG_PREEMPTION) && need_resched())
		set_dec(1);
	return 0;
}

int enter_vmx_ops(void)
{
	if (in_interrupt())
		return 0;

	preempt_disable();

	enable_kernel_altivec();

	return 1;
}

/*
 * All calls to this function will be optimised into tail calls. We are
 * passed a pointer to the destination which we return as required by a
 * memcpy implementation.
 */
void *exit_vmx_ops(void *dest)
{
	disable_kernel_altivec();
	preempt_enable();
	return dest;
}
