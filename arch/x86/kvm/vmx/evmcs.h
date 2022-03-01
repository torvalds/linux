/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_VMX_EVMCS_H
#define __KVM_X86_VMX_EVMCS_H

#include <linux/jump_label.h>

#include <asm/hyperv-tlfs.h>
#include <asm/mshyperv.h>
#include <asm/vmx.h>

#include "capabilities.h"
#include "vmcs.h"
#include "vmcs12.h"

struct vmcs_config;

DECLARE_STATIC_KEY_FALSE(enable_evmcs);

#define current_evmcs ((struct hv_enlightened_vmcs *)this_cpu_read(current_vmcs))

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
 *      GUEST_IA32_PERF_GLOBAL_CTRL     = 0x00002808,
 *      HOST_IA32_PERF_GLOBAL_CTRL      = 0x00002c04,
 *
 * Currently unsupported in KVM:
 *	GUEST_IA32_RTIT_CTL		= 0x00002814,
 */
#define EVMCS1_UNSUPPORTED_PINCTRL (PIN_BASED_POSTED_INTR | \
				    PIN_BASED_VMX_PREEMPTION_TIMER)
#define EVMCS1_UNSUPPORTED_2NDEXEC					\
	(SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY |				\
	 SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |			\
	 SECONDARY_EXEC_APIC_REGISTER_VIRT |				\
	 SECONDARY_EXEC_ENABLE_PML |					\
	 SECONDARY_EXEC_ENABLE_VMFUNC |					\
	 SECONDARY_EXEC_SHADOW_VMCS |					\
	 SECONDARY_EXEC_TSC_SCALING |					\
	 SECONDARY_EXEC_PAUSE_LOOP_EXITING)
#define EVMCS1_UNSUPPORTED_VMEXIT_CTRL					\
	(VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |				\
	 VM_EXIT_SAVE_VMX_PREEMPTION_TIMER)
#define EVMCS1_UNSUPPORTED_VMENTRY_CTRL (VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL)
#define EVMCS1_UNSUPPORTED_VMFUNC (VMX_VMFUNC_EPTP_SWITCHING)

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

#if IS_ENABLED(CONFIG_HYPERV)

static __always_inline int get_evmcs_offset(unsigned long field,
					    u16 *clean_field)
{
	int offset = evmcs_field_offset(field, clean_field);

	WARN_ONCE(offset < 0, "KVM: accessing unsupported EVMCS field %lx\n",
		  field);

	return offset;
}

static __always_inline void evmcs_write64(unsigned long field, u64 value)
{
	u16 clean_field;
	int offset = get_evmcs_offset(field, &clean_field);

	if (offset < 0)
		return;

	*(u64 *)((char *)current_evmcs + offset) = value;

	current_evmcs->hv_clean_fields &= ~clean_field;
}

static inline void evmcs_write32(unsigned long field, u32 value)
{
	u16 clean_field;
	int offset = get_evmcs_offset(field, &clean_field);

	if (offset < 0)
		return;

	*(u32 *)((char *)current_evmcs + offset) = value;
	current_evmcs->hv_clean_fields &= ~clean_field;
}

static inline void evmcs_write16(unsigned long field, u16 value)
{
	u16 clean_field;
	int offset = get_evmcs_offset(field, &clean_field);

	if (offset < 0)
		return;

	*(u16 *)((char *)current_evmcs + offset) = value;
	current_evmcs->hv_clean_fields &= ~clean_field;
}

static inline u64 evmcs_read64(unsigned long field)
{
	int offset = get_evmcs_offset(field, NULL);

	if (offset < 0)
		return 0;

	return *(u64 *)((char *)current_evmcs + offset);
}

static inline u32 evmcs_read32(unsigned long field)
{
	int offset = get_evmcs_offset(field, NULL);

	if (offset < 0)
		return 0;

	return *(u32 *)((char *)current_evmcs + offset);
}

static inline u16 evmcs_read16(unsigned long field)
{
	int offset = get_evmcs_offset(field, NULL);

	if (offset < 0)
		return 0;

	return *(u16 *)((char *)current_evmcs + offset);
}

static inline void evmcs_touch_msr_bitmap(void)
{
	if (unlikely(!current_evmcs))
		return;

	if (current_evmcs->hv_enlightenments_control.msr_bitmap)
		current_evmcs->hv_clean_fields &=
			~HV_VMX_ENLIGHTENED_CLEAN_FIELD_MSR_BITMAP;
}

static inline void evmcs_load(u64 phys_addr)
{
	struct hv_vp_assist_page *vp_ap =
		hv_get_vp_assist_page(smp_processor_id());

	if (current_evmcs->hv_enlightenments_control.nested_flush_hypercall)
		vp_ap->nested_control.features.directhypercall = 1;
	vp_ap->current_nested_vmcs = phys_addr;
	vp_ap->enlighten_vmentry = 1;
}

__init void evmcs_sanitize_exec_ctrls(struct vmcs_config *vmcs_conf);
#else /* !IS_ENABLED(CONFIG_HYPERV) */
static __always_inline void evmcs_write64(unsigned long field, u64 value) {}
static inline void evmcs_write32(unsigned long field, u32 value) {}
static inline void evmcs_write16(unsigned long field, u16 value) {}
static inline u64 evmcs_read64(unsigned long field) { return 0; }
static inline u32 evmcs_read32(unsigned long field) { return 0; }
static inline u16 evmcs_read16(unsigned long field) { return 0; }
static inline void evmcs_load(u64 phys_addr) {}
static inline void evmcs_touch_msr_bitmap(void) {}
#endif /* IS_ENABLED(CONFIG_HYPERV) */

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

bool nested_enlightened_vmentry(struct kvm_vcpu *vcpu, u64 *evmcs_gpa);
uint16_t nested_get_evmcs_version(struct kvm_vcpu *vcpu);
int nested_enable_evmcs(struct kvm_vcpu *vcpu,
			uint16_t *vmcs_version);
void nested_evmcs_filter_control_msr(u32 msr_index, u64 *pdata);
int nested_evmcs_check_controls(struct vmcs12 *vmcs12);

#endif /* __KVM_X86_VMX_EVMCS_H */
