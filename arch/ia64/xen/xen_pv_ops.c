/******************************************************************************
 * arch/ia64/xen/xen_pv_ops.c
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
 */

#include <linux/console.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/pm.h>

#include <asm/xen/hypervisor.h>
#include <asm/xen/xencomm.h>
#include <asm/xen/privop.h>

/***************************************************************************
 * general info
 */
static struct pv_info xen_info __initdata = {
	.kernel_rpl = 2,	/* or 1: determin at runtime */
	.paravirt_enabled = 1,
	.name = "Xen/ia64",
};

#define IA64_RSC_PL_SHIFT	2
#define IA64_RSC_PL_BIT_SIZE	2
#define IA64_RSC_PL_MASK	\
	(((1UL << IA64_RSC_PL_BIT_SIZE) - 1) << IA64_RSC_PL_SHIFT)

static void __init
xen_info_init(void)
{
	/* Xenified Linux/ia64 may run on pl = 1 or 2.
	 * determin at run time. */
	unsigned long rsc = ia64_getreg(_IA64_REG_AR_RSC);
	unsigned int rpl = (rsc & IA64_RSC_PL_MASK) >> IA64_RSC_PL_SHIFT;
	xen_info.kernel_rpl = rpl;
}

/***************************************************************************
 * pv_ops initialization
 */

void __init
xen_setup_pv_ops(void)
{
	xen_info_init();
	pv_info = xen_info;
}
