/******************************************************************************
 * arch/ia64/xen/suspend.c
 *
 * Copyright (c) 2008 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * suspend/resume
 */

#include <xen/xen-ops.h>
#include <asm/xen/hypervisor.h>
#include "time.h"

void
xen_mm_pin_all(void)
{
	/* nothing */
}

void
xen_mm_unpin_all(void)
{
	/* nothing */
}

void
xen_arch_pre_suspend()
{
	/* nothing */
}

void
xen_arch_post_suspend(int suspend_cancelled)
{
	if (suspend_cancelled)
		return;

	xen_ia64_enable_opt_feature();
	/* add more if necessary */
}

void xen_arch_resume(void)
{
	xen_timer_resume_on_aps();
}
