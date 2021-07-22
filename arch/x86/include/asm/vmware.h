/* SPDX-License-Identifier: GPL-2.0 or MIT */
#ifndef _ASM_X86_VMWARE_H
#define _ASM_X86_VMWARE_H

#include <asm/cpufeatures.h>
#include <asm/alternative.h>
#include <linux/stringify.h>

/*
 * The hypercall definitions differ in the low word of the %edx argument
 * in the following way: the old port base interface uses the port
 * number to distinguish between high- and low bandwidth versions.
 *
 * The new vmcall interface instead uses a set of flags to select
 * bandwidth mode and transfer direction. The flags should be loaded
 * into %dx by any user and are automatically replaced by the port
 * number if the VMWARE_HYPERVISOR_PORT method is used.
 *
 * In short, new driver code should strictly use the new definition of
 * %dx content.
 */

/* Old port-based version */
#define VMWARE_HYPERVISOR_PORT    0x5658
#define VMWARE_HYPERVISOR_PORT_HB 0x5659

/* Current vmcall / vmmcall version */
#define VMWARE_HYPERVISOR_HB   BIT(0)
#define VMWARE_HYPERVISOR_OUT  BIT(1)

/* The low bandwidth call. The low word of edx is presumed clear. */
#define VMWARE_HYPERCALL						\
	ALTERNATIVE_2("movw $" __stringify(VMWARE_HYPERVISOR_PORT) ", %%dx; " \
		      "inl (%%dx), %%eax",				\
		      "vmcall", X86_FEATURE_VMCALL,			\
		      "vmmcall", X86_FEATURE_VMW_VMMCALL)

/*
 * The high bandwidth out call. The low word of edx is presumed to have the
 * HB and OUT bits set.
 */
#define VMWARE_HYPERCALL_HB_OUT						\
	ALTERNATIVE_2("movw $" __stringify(VMWARE_HYPERVISOR_PORT_HB) ", %%dx; " \
		      "rep outsb",					\
		      "vmcall", X86_FEATURE_VMCALL,			\
		      "vmmcall", X86_FEATURE_VMW_VMMCALL)

/*
 * The high bandwidth in call. The low word of edx is presumed to have the
 * HB bit set.
 */
#define VMWARE_HYPERCALL_HB_IN						\
	ALTERNATIVE_2("movw $" __stringify(VMWARE_HYPERVISOR_PORT_HB) ", %%dx; " \
		      "rep insb",					\
		      "vmcall", X86_FEATURE_VMCALL,			\
		      "vmmcall", X86_FEATURE_VMW_VMMCALL)
#endif
