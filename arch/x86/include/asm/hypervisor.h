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

/* x86 hypervisor types  */
enum x86_hypervisor_type {
	X86_HYPER_NATIVE = 0,
	X86_HYPER_VMWARE,
	X86_HYPER_MS_HYPERV,
	X86_HYPER_XEN_PV,
	X86_HYPER_XEN_HVM,
	X86_HYPER_KVM,
	X86_HYPER_JAILHOUSE,
	X86_HYPER_ACRN,
};

#ifdef CONFIG_HYPERVISOR_GUEST

#include <asm/kvm_para.h>
#include <asm/x86_init.h>
#include <asm/xen/hypervisor.h>

struct hypervisor_x86 {
	/* Hypervisor name */
	const char	*name;

	/* Detection routine */
	uint32_t	(*detect)(void);

	/* Hypervisor type */
	enum x86_hypervisor_type type;

	/* init time callbacks */
	struct x86_hyper_init init;

	/* runtime callbacks */
	struct x86_hyper_runtime runtime;
};

extern enum x86_hypervisor_type x86_hyper_type;
extern void init_hypervisor_platform(void);
static inline bool hypervisor_is_type(enum x86_hypervisor_type type)
{
	return x86_hyper_type == type;
}
#else
static inline void init_hypervisor_platform(void) { }
static inline bool hypervisor_is_type(enum x86_hypervisor_type type)
{
	return type == X86_HYPER_NATIVE;
}
#endif /* CONFIG_HYPERVISOR_GUEST */
#endif /* _ASM_X86_HYPERVISOR_H */
