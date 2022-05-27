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
	preempt_enable();
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
