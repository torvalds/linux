/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __ARCH_X86_KVM_VMX_ONHYPERV_H__
#define __ARCH_X86_KVM_VMX_ONHYPERV_H__

#include <asm/hyperv-tlfs.h>
#include <asm/mshyperv.h>

#include <linux/jump_label.h>

#include "capabilities.h"
#include "hyperv_evmcs.h"
#include "vmcs12.h"

#define current_evmcs ((struct hv_enlightened_vmcs *)this_cpu_read(current_vmcs))

#if IS_ENABLED(CONFIG_HYPERV)

DECLARE_STATIC_KEY_FALSE(__kvm_is_using_evmcs);

static __always_inline bool kvm_is_using_evmcs(void)
{
	return static_branch_unlikely(&__kvm_is_using_evmcs);
}

static __always_inline int get_evmcs_offset(unsigned long field,
					    u16 *clean_field)
{
	int offset = evmcs_field_offset(field, clean_field);

	WARN_ONCE(offset < 0, "accessing unsupported EVMCS field %lx\n", field);
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

static __always_inline void evmcs_write32(unsigned long field, u32 value)
{
	u16 clean_field;
	int offset = get_evmcs_offset(field, &clean_field);

	if (offset < 0)
		return;

	*(u32 *)((char *)current_evmcs + offset) = value;
	current_evmcs->hv_clean_fields &= ~clean_field;
}

static __always_inline void evmcs_write16(unsigned long field, u16 value)
{
	u16 clean_field;
	int offset = get_evmcs_offset(field, &clean_field);

	if (offset < 0)
		return;

	*(u16 *)((char *)current_evmcs + offset) = value;
	current_evmcs->hv_clean_fields &= ~clean_field;
}

static __always_inline u64 evmcs_read64(unsigned long field)
{
	int offset = get_evmcs_offset(field, NULL);

	if (offset < 0)
		return 0;

	return *(u64 *)((char *)current_evmcs + offset);
}

static __always_inline u32 evmcs_read32(unsigned long field)
{
	int offset = get_evmcs_offset(field, NULL);

	if (offset < 0)
		return 0;

	return *(u32 *)((char *)current_evmcs + offset);
}

static __always_inline u16 evmcs_read16(unsigned long field)
{
	int offset = get_evmcs_offset(field, NULL);

	if (offset < 0)
		return 0;

	return *(u16 *)((char *)current_evmcs + offset);
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

void evmcs_sanitize_exec_ctrls(struct vmcs_config *vmcs_conf);
#else /* !IS_ENABLED(CONFIG_HYPERV) */
static __always_inline bool kvm_is_using_evmcs(void) { return false; }
static __always_inline void evmcs_write64(unsigned long field, u64 value) {}
static __always_inline void evmcs_write32(unsigned long field, u32 value) {}
static __always_inline void evmcs_write16(unsigned long field, u16 value) {}
static __always_inline u64 evmcs_read64(unsigned long field) { return 0; }
static __always_inline u32 evmcs_read32(unsigned long field) { return 0; }
static __always_inline u16 evmcs_read16(unsigned long field) { return 0; }
static inline void evmcs_load(u64 phys_addr) {}
#endif /* IS_ENABLED(CONFIG_HYPERV) */

#endif /* __ARCH_X86_KVM_VMX_ONHYPERV_H__ */
