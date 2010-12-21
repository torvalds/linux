/*
 * Copyright (C) 2008, VMware, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#ifndef _ASM_X86_HYPERVISOR_H
#define _ASM_X86_HYPERVISOR_H

#include <asm/kvm_para.h>

extern void init_hypervisor(struct cpuinfo_x86 *c);
extern void init_hypervisor_platform(void);

/*
 * x86 hypervisor information
 */
struct hypervisor_x86 {
	/* Hypervisor name */
	const char	*name;

	/* Detection routine */
	bool		(*detect)(void);

	/* Adjust CPU feature bits (run once per CPU) */
	void		(*set_cpu_features)(struct cpuinfo_x86 *);

	/* Platform setup (run once per boot) */
	void		(*init_platform)(void);
};

extern const struct hypervisor_x86 *x86_hyper;

/* Recognized hypervisors */
extern const struct hypervisor_x86 x86_hyper_vmware;
extern const struct hypervisor_x86 x86_hyper_ms_hyperv;
extern const struct hypervisor_x86 x86_hyper_xen_hvm;

static inline bool hypervisor_x2apic_available(void)
{
	if (kvm_para_available())
		return true;
	return false;
}

#endif
