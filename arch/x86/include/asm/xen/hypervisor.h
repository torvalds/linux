/******************************************************************************
 * hypervisor.h
 *
 * Linux-specific hypervisor handling.
 *
 * Copyright (c) 2002-2004, K A Fraser
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef _ASM_X86_XEN_HYPERVISOR_H
#define _ASM_X86_XEN_HYPERVISOR_H

extern struct shared_info *HYPERVISOR_shared_info;
extern struct start_info *xen_start_info;

#include <asm/processor.h>

static inline uint32_t xen_cpuid_base(void)
{
	return hypervisor_cpuid_base("XenVMMXenVMM", 2);
}

#ifdef CONFIG_XEN
extern bool __init xen_hvm_need_lapic(void);

static inline bool __init xen_x2apic_para_available(void)
{
	return xen_hvm_need_lapic();
}
#else
static inline bool __init xen_x2apic_para_available(void)
{
	return (xen_cpuid_base() != 0);
}
#endif

#ifdef CONFIG_HOTPLUG_CPU
void xen_arch_register_cpu(int num);
void xen_arch_unregister_cpu(int num);
#endif

#ifdef CONFIG_PVH
void __init xen_pvh_init(struct boot_params *boot_params);
#endif

#endif /* _ASM_X86_XEN_HYPERVISOR_H */
