/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_VMX_HYPERV_H
#define __KVM_X86_VMX_HYPERV_H

#include <linux/jump_label.h>

#include <asm/hyperv-tlfs.h>
#include <asm/mshyperv.h>
#include <asm/vmx.h>

#include "../hyperv.h"

#include "capabilities.h"
#include "vmcs.h"
#include "vmcs12.h"

#define KVM_EVMCS_VERSION 1

/*
 * Enlightened VMCSv1 doesn't support these:
 *
 *	POSTED_INTR_NV                  = 0x00000002,
 *	GUEST_INTR_STATUS               = 0x00000810,
 *	APIC_ACCESS_ADDR		= 0x00002014,
 *	POSTED_INTR_DESC_ADDR           = 0x00002016,
 *	EOI_EXIT_BITMAP0                = 0x0000201c,
 *	EOI_EXIT_BITMAP1                = 0x0000201e,
 *	EOI_EXIT_BITMAP2                = 0x00002020,
 *	EOI_EXIT_BITMAP3                = 0x00002022,
 *	GUEST_PML_INDEX			= 0x00000812,
 *	PML_ADDRESS			= 0x0000200e,
 *	VM_FUNCTION_CONTROL             = 0x00002018,
 *	EPTP_LIST_ADDRESS               = 0x00002024,
 *	VMREAD_BITMAP                   = 0x00002026,
 *	VMWRITE_BITMAP                  = 0x00002028,
 *
 *	TSC_MULTIPLIER                  = 0x00002032,
 *	PLE_GAP                         = 0x00004020,
 *	PLE_WINDOW                      = 0x00004022,
 *	VMX_PREEMPTION_TIMER_VALUE      = 0x0000482E,
 *
 * Currently unsupported in KVM:
 *	GUEST_IA32_RTIT_CTL		= 0x00002814,
 */
#define EVMCS1_SUPPORTED_PINCTRL					\
	(PIN_BASED_ALWAYSON_WITHOUT_TRUE_MSR |				\
	 PIN_BASED_EXT_INTR_MASK |					\
	 PIN_BASED_NMI_EXITING |					\
	 PIN_BASED_VIRTUAL_NMIS)

#define EVMCS1_SUPPORTED_EXEC_CTRL					\
	(CPU_BASED_ALWAYSON_WITHOUT_TRUE_MSR |				\
	 CPU_BASED_HLT_EXITING |					\
	 CPU_BASED_CR3_LOAD_EXITING |					\
	 CPU_BASED_CR3_STORE_EXITING |					\
	 CPU_BASED_UNCOND_IO_EXITING |					\
	 CPU_BASED_MOV_DR_EXITING |					\
	 CPU_BASED_USE_TSC_OFFSETTING |					\
	 CPU_BASED_MWAIT_EXITING |					\
	 CPU_BASED_MONITOR_EXITING |					\
	 CPU_BASED_INVLPG_EXITING |					\
	 CPU_BASED_RDPMC_EXITING |					\
	 CPU_BASED_INTR_WINDOW_EXITING |				\
	 CPU_BASED_CR8_LOAD_EXITING |					\
	 CPU_BASED_CR8_STORE_EXITING |					\
	 CPU_BASED_RDTSC_EXITING |					\
	 CPU_BASED_TPR_SHADOW |						\
	 CPU_BASED_USE_IO_BITMAPS |					\
	 CPU_BASED_MONITOR_TRAP_FLAG |					\
	 CPU_BASED_USE_MSR_BITMAPS |					\
	 CPU_BASED_NMI_WINDOW_EXITING |					\
	 CPU_BASED_PAUSE_EXITING |					\
	 CPU_BASED_ACTIVATE_SECONDARY_CONTROLS)

#define EVMCS1_SUPPORTED_2NDEXEC					\
	(SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |			\
	 SECONDARY_EXEC_WBINVD_EXITING |				\
	 SECONDARY_EXEC_ENABLE_VPID |					\
	 SECONDARY_EXEC_ENABLE_EPT |					\
	 SECONDARY_EXEC_UNRESTRICTED_GUEST |				\
	 SECONDARY_EXEC_DESC |						\
	 SECONDARY_EXEC_ENABLE_RDTSCP |					\
	 SECONDARY_EXEC_ENABLE_INVPCID |				\
	 SECONDARY_EXEC_ENABLE_XSAVES |					\
	 SECONDARY_EXEC_RDSEED_EXITING |				\
	 SECONDARY_EXEC_RDRAND_EXITING |				\
	 SECONDARY_EXEC_TSC_SCALING |					\
	 SECONDARY_EXEC_ENABLE_USR_WAIT_PAUSE |				\
	 SECONDARY_EXEC_PT_USE_GPA |					\
	 SECONDARY_EXEC_PT_CONCEAL_VMX |				\
	 SECONDARY_EXEC_BUS_LOCK_DETECTION |				\
	 SECONDARY_EXEC_NOTIFY_VM_EXITING |				\
	 SECONDARY_EXEC_ENCLS_EXITING)

#define EVMCS1_SUPPORTED_3RDEXEC (0ULL)

#define EVMCS1_SUPPORTED_VMEXIT_CTRL					\
	(VM_EXIT_ALWAYSON_WITHOUT_TRUE_MSR |				\
	 VM_EXIT_SAVE_DEBUG_CONTROLS |					\
	 VM_EXIT_ACK_INTR_ON_EXIT |					\
	 VM_EXIT_HOST_ADDR_SPACE_SIZE |					\
	 VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |				\
	 VM_EXIT_SAVE_IA32_PAT |					\
	 VM_EXIT_LOAD_IA32_PAT |					\
	 VM_EXIT_SAVE_IA32_EFER |					\
	 VM_EXIT_LOAD_IA32_EFER |					\
	 VM_EXIT_CLEAR_BNDCFGS |					\
	 VM_EXIT_PT_CONCEAL_PIP |					\
	 VM_EXIT_CLEAR_IA32_RTIT_CTL)

#define EVMCS1_SUPPORTED_VMENTRY_CTRL					\
	(VM_ENTRY_ALWAYSON_WITHOUT_TRUE_MSR |				\
	 VM_ENTRY_LOAD_DEBUG_CONTROLS |					\
	 VM_ENTRY_IA32E_MODE |						\
	 VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL |				\
	 VM_ENTRY_LOAD_IA32_PAT |					\
	 VM_ENTRY_LOAD_IA32_EFER |					\
	 VM_ENTRY_LOAD_BNDCFGS |					\
	 VM_ENTRY_PT_CONCEAL_PIP |					\
	 VM_ENTRY_LOAD_IA32_RTIT_CTL)

#define EVMCS1_SUPPORTED_VMFUNC (0)

struct evmcs_field {
	u16 offset;
	u16 clean_field;
};

extern const struct evmcs_field vmcs_field_to_evmcs_1[];
extern const unsigned int nr_evmcs_1_fields;

static __always_inline int evmcs_field_offset(unsigned long field,
					      u16 *clean_field)
{
	unsigned int index = ROL16(field, 6);
	const struct evmcs_field *evmcs_field;

	if (unlikely(index >= nr_evmcs_1_fields))
		return -ENOENT;

	evmcs_field = &vmcs_field_to_evmcs_1[index];

	/*
	 * Use offset=0 to detect holes in eVMCS. This offset belongs to
	 * 'revision_id' but this field has no encoding and is supposed to
	 * be accessed directly.
	 */
	if (unlikely(!evmcs_field->offset))
		return -ENOENT;

	if (clean_field)
		*clean_field = evmcs_field->clean_field;

	return evmcs_field->offset;
}

static inline u64 evmcs_read_any(struct hv_enlightened_vmcs *evmcs,
				 unsigned long field, u16 offset)
{
	/*
	 * vmcs12_read_any() doesn't care whether the supplied structure
	 * is 'struct vmcs12' or 'struct hv_enlightened_vmcs' as it takes
	 * the exact offset of the required field, use it for convenience
	 * here.
	 */
	return vmcs12_read_any((void *)evmcs, field, offset);
}

#define EVMPTR_INVALID (-1ULL)
#define EVMPTR_MAP_PENDING (-2ULL)

static inline bool evmptr_is_valid(u64 evmptr)
{
	return evmptr != EVMPTR_INVALID && evmptr != EVMPTR_MAP_PENDING;
}

enum nested_evmptrld_status {
	EVMPTRLD_DISABLED,
	EVMPTRLD_SUCCEEDED,
	EVMPTRLD_VMFAIL,
	EVMPTRLD_ERROR,
};

u64 nested_get_evmptr(struct kvm_vcpu *vcpu);
uint16_t nested_get_evmcs_version(struct kvm_vcpu *vcpu);
int nested_enable_evmcs(struct kvm_vcpu *vcpu,
			uint16_t *vmcs_version);
void nested_evmcs_filter_control_msr(struct kvm_vcpu *vcpu, u32 msr_index, u64 *pdata);
int nested_evmcs_check_controls(struct vmcs12 *vmcs12);
bool nested_evmcs_l2_tlb_flush_enabled(struct kvm_vcpu *vcpu);
void vmx_hv_inject_synthetic_vmexit_post_tlb_flush(struct kvm_vcpu *vcpu);

#endif /* __KVM_X86_VMX_HYPERV_H */
