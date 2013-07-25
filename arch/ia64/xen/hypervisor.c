/******************************************************************************
 * arch/ia64/xen/hypervisor.c
 *
 * Copyright (c) 2006 Isaku Yamahata <yamahata at valinux co jp>
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

#include <linux/efi.h>
#include <linux/export.h>
#include <asm/xen/hypervisor.h>
#include <asm/xen/privop.h>

#include "irq_xen.h"

struct shared_info *HYPERVISOR_shared_info __read_mostly =
	(struct shared_info *)XSI_BASE;
EXPORT_SYMBOL(HYPERVISOR_shared_info);

DEFINE_PER_CPU(struct vcpu_info *, xen_vcpu);

struct start_info *xen_start_info;
EXPORT_SYMBOL(xen_start_info);

EXPORT_SYMBOL(xen_domain_type);

EXPORT_SYMBOL(__hypercall);

/* Stolen from arch/x86/xen/enlighten.c */
/*
 * Flag to determine whether vcpu info placement is available on all
 * VCPUs.  We assume it is to start with, and then set it to zero on
 * the first failure.  This is because it can succeed on some VCPUs
 * and not others, since it can involve hypervisor memory allocation,
 * or because the guest failed to guarantee all the appropriate
 * constraints on all VCPUs (ie buffer can't cross a page boundary).
 *
 * Note that any particular CPU may be using a placed vcpu structure,
 * but we can only optimise if the all are.
 *
 * 0: not available, 1: available
 */

static void __init xen_vcpu_setup(int cpu)
{
	/*
	 * WARNING:
	 * before changing MAX_VIRT_CPUS,
	 * check that shared_info fits on a page
	 */
	BUILD_BUG_ON(sizeof(struct shared_info) > PAGE_SIZE);
	per_cpu(xen_vcpu, cpu) = &HYPERVISOR_shared_info->vcpu_info[cpu];
}

void __init xen_setup_vcpu_info_placement(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		xen_vcpu_setup(cpu);
}

void
xen_cpu_init(void)
{
	xen_smp_intr_init();
}

/**************************************************************************
 * opt feature
 */
void
xen_ia64_enable_opt_feature(void)
{
	/* Enable region 7 identity map optimizations in Xen */
	struct xen_ia64_opt_feature optf;

	optf.cmd = XEN_IA64_OPTF_IDENT_MAP_REG7;
	optf.on = XEN_IA64_OPTF_ON;
	optf.pgprot = pgprot_val(PAGE_KERNEL);
	optf.key = 0;	/* No key on linux. */
	HYPERVISOR_opt_feature(&optf);
}
