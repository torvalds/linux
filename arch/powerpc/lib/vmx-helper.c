/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
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
	pagefault_enable();
	preempt_enable();
	return 0;
}

int enter_vmx_copy(void)
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
void *exit_vmx_copy(void *dest)
{
	preempt_enable();
	return dest;
}
