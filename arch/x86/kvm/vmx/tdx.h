/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_VMX_TDX_H
#define __KVM_X86_VMX_TDX_H

#include "tdx_arch.h"
#include "tdx_errno.h"

#ifdef CONFIG_KVM_INTEL_TDX
#include "common.h"

void tdx_hardware_setup(void);
int tdx_bringup(void);
void tdx_cleanup(void);

extern bool enable_tdx;

/* TDX module hardware states. These follow the TDX module OP_STATEs. */
enum kvm_tdx_state {
	TD_STATE_UNINITIALIZED = 0,
	TD_STATE_INITIALIZED,
	TD_STATE_RUNNABLE,
};

struct kvm_tdx {
	struct kvm kvm;

	struct misc_cg *misc_cg;
	int hkid;
	enum kvm_tdx_state state;

	u64 attributes;
	u64 xfam;

	u64 tsc_offset;
	u64 tsc_multiplier;

	struct tdx_td td;

	/* For KVM_TDX_INIT_MEM_REGION. */
	atomic64_t nr_premapped;

	/*
	 * Prevent vCPUs from TD entry to ensure SEPT zap related SEAMCALLs do
	 * not contend with tdh_vp_enter() and TDCALLs.
	 * Set/unset is protected with kvm->mmu_lock.
	 */
	bool wait_for_sept_zap;
};

/* TDX module vCPU states */
enum vcpu_tdx_state {
	VCPU_TD_STATE_UNINITIALIZED = 0,
	VCPU_TD_STATE_INITIALIZED,
};

struct vcpu_tdx {
	struct kvm_vcpu	vcpu;
	struct vcpu_vt vt;
	u64 ext_exit_qualification;
	gpa_t exit_gpa;
	struct tdx_module_args vp_enter_args;

	struct tdx_vp vp;

	struct list_head cpu_list;

	u64 vp_enter_ret;

	enum vcpu_tdx_state state;
	bool guest_entered;

	u64 map_gpa_next;
	u64 map_gpa_end;
};

void tdh_vp_rd_failed(struct vcpu_tdx *tdx, char *uclass, u32 field, u64 err);
void tdh_vp_wr_failed(struct vcpu_tdx *tdx, char *uclass, char *op, u32 field,
		      u64 val, u64 err);

static __always_inline u64 td_tdcs_exec_read64(struct kvm_tdx *kvm_tdx, u32 field)
{
	u64 err, data;

	err = tdh_mng_rd(&kvm_tdx->td, TDCS_EXEC(field), &data);
	if (unlikely(err)) {
		pr_err("TDH_MNG_RD[EXEC.0x%x] failed: 0x%llx\n", field, err);
		return 0;
	}
	return data;
}

static __always_inline void tdvps_vmcs_check(u32 field, u8 bits)
{
#define VMCS_ENC_ACCESS_TYPE_MASK	0x1UL
#define VMCS_ENC_ACCESS_TYPE_FULL	0x0UL
#define VMCS_ENC_ACCESS_TYPE_HIGH	0x1UL
#define VMCS_ENC_ACCESS_TYPE(field)	((field) & VMCS_ENC_ACCESS_TYPE_MASK)

	/* TDX is 64bit only.  HIGH field isn't supported. */
	BUILD_BUG_ON_MSG(__builtin_constant_p(field) &&
			 VMCS_ENC_ACCESS_TYPE(field) == VMCS_ENC_ACCESS_TYPE_HIGH,
			 "Read/Write to TD VMCS *_HIGH fields not supported");

	BUILD_BUG_ON(bits != 16 && bits != 32 && bits != 64);

#define VMCS_ENC_WIDTH_MASK	GENMASK(14, 13)
#define VMCS_ENC_WIDTH_16BIT	(0UL << 13)
#define VMCS_ENC_WIDTH_64BIT	(1UL << 13)
#define VMCS_ENC_WIDTH_32BIT	(2UL << 13)
#define VMCS_ENC_WIDTH_NATURAL	(3UL << 13)
#define VMCS_ENC_WIDTH(field)	((field) & VMCS_ENC_WIDTH_MASK)

	/* TDX is 64bit only.  i.e. natural width = 64bit. */
	BUILD_BUG_ON_MSG(bits != 64 && __builtin_constant_p(field) &&
			 (VMCS_ENC_WIDTH(field) == VMCS_ENC_WIDTH_64BIT ||
			  VMCS_ENC_WIDTH(field) == VMCS_ENC_WIDTH_NATURAL),
			 "Invalid TD VMCS access for 64-bit field");
	BUILD_BUG_ON_MSG(bits != 32 && __builtin_constant_p(field) &&
			 VMCS_ENC_WIDTH(field) == VMCS_ENC_WIDTH_32BIT,
			 "Invalid TD VMCS access for 32-bit field");
	BUILD_BUG_ON_MSG(bits != 16 && __builtin_constant_p(field) &&
			 VMCS_ENC_WIDTH(field) == VMCS_ENC_WIDTH_16BIT,
			 "Invalid TD VMCS access for 16-bit field");
}

static __always_inline void tdvps_management_check(u64 field, u8 bits) {}
static __always_inline void tdvps_state_non_arch_check(u64 field, u8 bits) {}

#define TDX_BUILD_TDVPS_ACCESSORS(bits, uclass, lclass)				\
static __always_inline u##bits td_##lclass##_read##bits(struct vcpu_tdx *tdx,	\
							u32 field)		\
{										\
	u64 err, data;								\
										\
	tdvps_##lclass##_check(field, bits);					\
	err = tdh_vp_rd(&tdx->vp, TDVPS_##uclass(field), &data);		\
	if (unlikely(err)) {							\
		tdh_vp_rd_failed(tdx, #uclass, field, err);			\
		return 0;							\
	}									\
	return (u##bits)data;							\
}										\
static __always_inline void td_##lclass##_write##bits(struct vcpu_tdx *tdx,	\
						      u32 field, u##bits val)	\
{										\
	u64 err;								\
										\
	tdvps_##lclass##_check(field, bits);					\
	err = tdh_vp_wr(&tdx->vp, TDVPS_##uclass(field), val,			\
		      GENMASK_ULL(bits - 1, 0));				\
	if (unlikely(err))							\
		tdh_vp_wr_failed(tdx, #uclass, " = ", field, (u64)val, err);	\
}										\
static __always_inline void td_##lclass##_setbit##bits(struct vcpu_tdx *tdx,	\
						       u32 field, u64 bit)	\
{										\
	u64 err;								\
										\
	tdvps_##lclass##_check(field, bits);					\
	err = tdh_vp_wr(&tdx->vp, TDVPS_##uclass(field), bit, bit);		\
	if (unlikely(err))							\
		tdh_vp_wr_failed(tdx, #uclass, " |= ", field, bit, err);	\
}										\
static __always_inline void td_##lclass##_clearbit##bits(struct vcpu_tdx *tdx,	\
							 u32 field, u64 bit)	\
{										\
	u64 err;								\
										\
	tdvps_##lclass##_check(field, bits);					\
	err = tdh_vp_wr(&tdx->vp, TDVPS_##uclass(field), 0, bit);		\
	if (unlikely(err))							\
		tdh_vp_wr_failed(tdx, #uclass, " &= ~", field, bit, err);\
}


bool tdx_interrupt_allowed(struct kvm_vcpu *vcpu);
int tdx_complete_emulated_msr(struct kvm_vcpu *vcpu, int err);

TDX_BUILD_TDVPS_ACCESSORS(16, VMCS, vmcs);
TDX_BUILD_TDVPS_ACCESSORS(32, VMCS, vmcs);
TDX_BUILD_TDVPS_ACCESSORS(64, VMCS, vmcs);

TDX_BUILD_TDVPS_ACCESSORS(8, MANAGEMENT, management);
TDX_BUILD_TDVPS_ACCESSORS(64, STATE_NON_ARCH, state_non_arch);

#else
static inline int tdx_bringup(void) { return 0; }
static inline void tdx_cleanup(void) {}

#define enable_tdx	0

struct kvm_tdx {
	struct kvm kvm;
};

struct vcpu_tdx {
	struct kvm_vcpu	vcpu;
};

static inline bool tdx_interrupt_allowed(struct kvm_vcpu *vcpu) { return false; }
static inline int tdx_complete_emulated_msr(struct kvm_vcpu *vcpu, int err) { return 0; }

#endif

#endif
