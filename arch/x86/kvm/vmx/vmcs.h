/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_VMX_VMCS_H
#define __KVM_X86_VMX_VMCS_H

#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/nospec.h>

#include <asm/kvm.h>
#include <asm/vmx.h>

#include "capabilities.h"

struct vmcs_hdr {
	u32 revision_id:31;
	u32 shadow_vmcs:1;
};

struct vmcs {
	struct vmcs_hdr hdr;
	u32 abort;
	char data[];
};

DECLARE_PER_CPU(struct vmcs *, current_vmcs);

/*
 * vmcs_host_state tracks registers that are loaded from the VMCS on VMEXIT
 * and whose values change infrequently, but are not constant.  I.e. this is
 * used as a write-through cache of the corresponding VMCS fields.
 */
struct vmcs_host_state {
	unsigned long cr3;	/* May not match real cr3 */
	unsigned long cr4;	/* May not match real cr4 */
	unsigned long gs_base;
	unsigned long fs_base;
	unsigned long rsp;

	u16           fs_sel, gs_sel, ldt_sel;
#ifdef CONFIG_X86_64
	u16           ds_sel, es_sel;
#endif
};

struct vmcs_controls_shadow {
	u32 vm_entry;
	u32 vm_exit;
	u32 pin;
	u32 exec;
	u32 secondary_exec;
};

/*
 * Track a VMCS that may be loaded on a certain CPU. If it is (cpu!=-1), also
 * remember whether it was VMLAUNCHed, and maintain a linked list of all VMCSs
 * loaded on this CPU (so we can clear them if the CPU goes down).
 */
struct loaded_vmcs {
	struct vmcs *vmcs;
	struct vmcs *shadow_vmcs;
	int cpu;
	bool launched;
	bool nmi_known_unmasked;
	bool hv_timer_soft_disabled;
	/* Support for vnmi-less CPUs */
	int soft_vnmi_blocked;
	ktime_t entry_time;
	s64 vnmi_blocked_time;
	unsigned long *msr_bitmap;
	struct list_head loaded_vmcss_on_cpu_link;
	struct vmcs_host_state host_state;
	struct vmcs_controls_shadow controls_shadow;
};

static inline bool is_intr_type(u32 intr_info, u32 type)
{
	const u32 mask = INTR_INFO_VALID_MASK | INTR_INFO_INTR_TYPE_MASK;

	return (intr_info & mask) == (INTR_INFO_VALID_MASK | type);
}

static inline bool is_intr_type_n(u32 intr_info, u32 type, u8 vector)
{
	const u32 mask = INTR_INFO_VALID_MASK | INTR_INFO_INTR_TYPE_MASK |
			 INTR_INFO_VECTOR_MASK;

	return (intr_info & mask) == (INTR_INFO_VALID_MASK | type | vector);
}

static inline bool is_exception_n(u32 intr_info, u8 vector)
{
	return is_intr_type_n(intr_info, INTR_TYPE_HARD_EXCEPTION, vector);
}

static inline bool is_debug(u32 intr_info)
{
	return is_exception_n(intr_info, DB_VECTOR);
}

static inline bool is_breakpoint(u32 intr_info)
{
	return is_exception_n(intr_info, BP_VECTOR);
}

static inline bool is_page_fault(u32 intr_info)
{
	return is_exception_n(intr_info, PF_VECTOR);
}

static inline bool is_invalid_opcode(u32 intr_info)
{
	return is_exception_n(intr_info, UD_VECTOR);
}

static inline bool is_gp_fault(u32 intr_info)
{
	return is_exception_n(intr_info, GP_VECTOR);
}

static inline bool is_machine_check(u32 intr_info)
{
	return is_exception_n(intr_info, MC_VECTOR);
}

/* Undocumented: icebp/int1 */
static inline bool is_icebp(u32 intr_info)
{
	return is_intr_type(intr_info, INTR_TYPE_PRIV_SW_EXCEPTION);
}

static inline bool is_nmi(u32 intr_info)
{
	return is_intr_type(intr_info, INTR_TYPE_NMI_INTR);
}

static inline bool is_external_intr(u32 intr_info)
{
	return is_intr_type(intr_info, INTR_TYPE_EXT_INTR);
}

static inline bool is_exception_with_error_code(u32 intr_info)
{
	const u32 mask = INTR_INFO_VALID_MASK | INTR_INFO_DELIVER_CODE_MASK;

	return (intr_info & mask) == mask;
}

enum vmcs_field_width {
	VMCS_FIELD_WIDTH_U16 = 0,
	VMCS_FIELD_WIDTH_U64 = 1,
	VMCS_FIELD_WIDTH_U32 = 2,
	VMCS_FIELD_WIDTH_NATURAL_WIDTH = 3
};

static inline int vmcs_field_width(unsigned long field)
{
	if (0x1 & field)	/* the *_HIGH fields are all 32 bit */
		return VMCS_FIELD_WIDTH_U32;
	return (field >> 13) & 0x3;
}

static inline int vmcs_field_readonly(unsigned long field)
{
	return (((field >> 10) & 0x3) == 1);
}

#endif /* __KVM_X86_VMX_VMCS_H */
