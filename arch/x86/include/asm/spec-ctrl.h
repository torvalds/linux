/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_SPECCTRL_H_
#define _ASM_X86_SPECCTRL_H_

#include <linux/thread_info.h>
#include <asm/nospec-branch.h>
#include <asm/msr.h>

/*
 * On VMENTER we must preserve whatever view of the SPEC_CTRL MSR
 * the guest has, while on VMEXIT we restore the host view. This
 * would be easier if SPEC_CTRL were architecturally maskable or
 * shadowable for guests but this is not (currently) the case.
 * Takes the guest view of SPEC_CTRL MSR as a parameter and also
 * the guest's version of VIRT_SPEC_CTRL, if emulated.
 */
extern void x86_virt_spec_ctrl(u64 guest_virt_spec_ctrl, bool guest);

/**
 * x86_spec_ctrl_set_guest - Set speculation control registers for the guest
 * @guest_spec_ctrl:		The guest content of MSR_SPEC_CTRL
 * @guest_virt_spec_ctrl:	The guest controlled bits of MSR_VIRT_SPEC_CTRL
 *				(may get translated to MSR_AMD64_LS_CFG bits)
 *
 * Avoids writing to the MSR if the content/bits are the same
 */
static inline
void x86_spec_ctrl_set_guest(u64 guest_virt_spec_ctrl)
{
	x86_virt_spec_ctrl(guest_virt_spec_ctrl, true);
}

/**
 * x86_spec_ctrl_restore_host - Restore host speculation control registers
 * @guest_spec_ctrl:		The guest content of MSR_SPEC_CTRL
 * @guest_virt_spec_ctrl:	The guest controlled bits of MSR_VIRT_SPEC_CTRL
 *				(may get translated to MSR_AMD64_LS_CFG bits)
 *
 * Avoids writing to the MSR if the content/bits are the same
 */
static inline
void x86_spec_ctrl_restore_host(u64 guest_virt_spec_ctrl)
{
	x86_virt_spec_ctrl(guest_virt_spec_ctrl, false);
}

/* AMD specific Speculative Store Bypass MSR data */
extern u64 x86_amd_ls_cfg_base;
extern u64 x86_amd_ls_cfg_ssbd_mask;

static inline u64 ssbd_tif_to_spec_ctrl(u64 tifn)
{
	BUILD_BUG_ON(TIF_SSBD < SPEC_CTRL_SSBD_SHIFT);
	return (tifn & _TIF_SSBD) >> (TIF_SSBD - SPEC_CTRL_SSBD_SHIFT);
}

static inline u64 stibp_tif_to_spec_ctrl(u64 tifn)
{
	BUILD_BUG_ON(TIF_SPEC_IB < SPEC_CTRL_STIBP_SHIFT);
	return (tifn & _TIF_SPEC_IB) >> (TIF_SPEC_IB - SPEC_CTRL_STIBP_SHIFT);
}

static inline unsigned long ssbd_spec_ctrl_to_tif(u64 spec_ctrl)
{
	BUILD_BUG_ON(TIF_SSBD < SPEC_CTRL_SSBD_SHIFT);
	return (spec_ctrl & SPEC_CTRL_SSBD) << (TIF_SSBD - SPEC_CTRL_SSBD_SHIFT);
}

static inline unsigned long stibp_spec_ctrl_to_tif(u64 spec_ctrl)
{
	BUILD_BUG_ON(TIF_SPEC_IB < SPEC_CTRL_STIBP_SHIFT);
	return (spec_ctrl & SPEC_CTRL_STIBP) << (TIF_SPEC_IB - SPEC_CTRL_STIBP_SHIFT);
}

static inline u64 ssbd_tif_to_amd_ls_cfg(u64 tifn)
{
	return (tifn & _TIF_SSBD) ? x86_amd_ls_cfg_ssbd_mask : 0ULL;
}

/*
 * This can be used in noinstr functions & should only be called in bare
 * metal context.
 */
static __always_inline void __update_spec_ctrl(u64 val)
{
	__this_cpu_write(x86_spec_ctrl_current, val);
	native_wrmsrl(MSR_IA32_SPEC_CTRL, val);
}

#ifdef CONFIG_SMP
extern void speculative_store_bypass_ht_init(void);
#else
static inline void speculative_store_bypass_ht_init(void) { }
#endif

extern void speculation_ctrl_update(unsigned long tif);
extern void speculation_ctrl_update_current(void);

extern bool itlb_multihit_kvm_mitigation;

#endif
