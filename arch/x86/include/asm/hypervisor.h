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

#ifdef CONFIG_HYPERVISOR_GUEST

#include <asm/kvm_para.h>
#include <asm/xen/hypervisor.h>

/*
 * x86 hypervisor information
 */
struct hypervisor_x86 {
	/* Hypervisor name */
	const char	*name;

	/* Detection routine */
	uint32_t	(*detect)(void);

	/* Platform setup (run once per boot) */
	void		(*init_platform)(void);

	/* X2APIC detection (run once per boot) */
	bool		(*x2apic_available)(void);

	/* pin current vcpu to specified physical cpu (run rarely) */
	void		(*pin_vcpu)(int);

	/* called during init_mem_mapping() to setup early mappings. */
	void		(*init_mem_mapping)(void);
};

extern const struct hypervisor_x86 *x86_hyper;

/* Recognized hypervisors */
extern const struct hypervisor_x86 x86_hyper_vmware;
extern const struct hypervisor_x86 x86_hyper_ms_hyperv;
extern const struct hypervisor_x86 x86_hyper_xen_pv;
extern const struct hypervisor_x86 x86_hyper_xen_hvm;
extern const struct hypervisor_x86 x86_hyper_kvm;

extern void init_hypervisor_platform(void);
extern bool hypervisor_x2apic_available(void);
extern void hypervisor_pin_vcpu(int cpu);

static inline void hypervisor_init_mem_mapping(void)
{
	if (x86_hyper && x86_hyper->init_mem_mapping)
		x86_hyper->init_mem_mapping();
}
#else
static inline void init_hypervisor_platform(void) { }
static inline bool hypervisor_x2apic_available(void) { return false; }
static inline void hypervisor_init_mem_mapping(void) { }
#endif /* CONFIG_HYPERVISOR_GUEST */
#endif /* _ASM_X86_HYPERVISOR_H */
