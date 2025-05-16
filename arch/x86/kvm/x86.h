/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARCH_X86_KVM_X86_H
#define ARCH_X86_KVM_X86_H

#include <linux/kvm_host.h>
#include <asm/fpu/xstate.h>
#include <asm/mce.h>
#include <asm/pvclock.h>
#include "kvm_cache_regs.h"
#include "kvm_emulate.h"
#include "cpuid.h"

struct kvm_caps {
	/* control of guest tsc rate supported? */
	bool has_tsc_control;
	/* maximum supported tsc_khz for guests */
	u32  max_guest_tsc_khz;
	/* number of bits of the fractional part of the TSC scaling ratio */
	u8   tsc_scaling_ratio_frac_bits;
	/* maximum allowed value of TSC scaling ratio */
	u64  max_tsc_scaling_ratio;
	/* 1ull << kvm_caps.tsc_scaling_ratio_frac_bits */
	u64  default_tsc_scaling_ratio;
	/* bus lock detection supported? */
	bool has_bus_lock_exit;
	/* notify VM exit supported? */
	bool has_notify_vmexit;
	/* bit mask of VM types */
	u32 supported_vm_types;

	u64 supported_mce_cap;
	u64 supported_xcr0;
	u64 supported_xss;
	u64 supported_perf_cap;
};

struct kvm_host_values {
	/*
	 * The host's raw MAXPHYADDR, i.e. the number of non-reserved physical
	 * address bits irrespective of features that repurpose legal bits,
	 * e.g. MKTME.
	 */
	u8 maxphyaddr;

	u64 efer;
	u64 xcr0;
	u64 xss;
	u64 arch_capabilities;
};

void kvm_spurious_fault(void);

#define KVM_NESTED_VMENTER_CONSISTENCY_CHECK(consistency_check)		\
({									\
	bool failed = (consistency_check);				\
	if (failed)							\
		trace_kvm_nested_vmenter_failed(#consistency_check, 0);	\
	failed;								\
})

/*
 * The first...last VMX feature MSRs that are emulated by KVM.  This may or may
 * not cover all known VMX MSRs, as KVM doesn't emulate an MSR until there's an
 * associated feature that KVM supports for nested virtualization.
 */
#define KVM_FIRST_EMULATED_VMX_MSR	MSR_IA32_VMX_BASIC
#define KVM_LAST_EMULATED_VMX_MSR	MSR_IA32_VMX_VMFUNC

#define KVM_DEFAULT_PLE_GAP		128
#define KVM_VMX_DEFAULT_PLE_WINDOW	4096
#define KVM_DEFAULT_PLE_WINDOW_GROW	2
#define KVM_DEFAULT_PLE_WINDOW_SHRINK	0
#define KVM_VMX_DEFAULT_PLE_WINDOW_MAX	UINT_MAX
#define KVM_SVM_DEFAULT_PLE_WINDOW_MAX	USHRT_MAX
#define KVM_SVM_DEFAULT_PLE_WINDOW	3000

static inline unsigned int __grow_ple_window(unsigned int val,
		unsigned int base, unsigned int modifier, unsigned int max)
{
	u64 ret = val;

	if (modifier < 1)
		return base;

	if (modifier < base)
		ret *= modifier;
	else
		ret += modifier;

	return min(ret, (u64)max);
}

static inline unsigned int __shrink_ple_window(unsigned int val,
		unsigned int base, unsigned int modifier, unsigned int min)
{
	if (modifier < 1)
		return base;

	if (modifier < base)
		val /= modifier;
	else
		val -= modifier;

	return max(val, min);
}

#define MSR_IA32_CR_PAT_DEFAULT	\
	PAT_VALUE(WB, WT, UC_MINUS, UC, WB, WT, UC_MINUS, UC)

void kvm_service_local_tlb_flush_requests(struct kvm_vcpu *vcpu);
int kvm_check_nested_events(struct kvm_vcpu *vcpu);

/* Forcibly leave the nested mode in cases like a vCPU reset */
static inline void kvm_leave_nested(struct kvm_vcpu *vcpu)
{
	kvm_x86_ops.nested_ops->leave_nested(vcpu);
}

static inline bool kvm_vcpu_has_run(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.last_vmentry_cpu != -1;
}

static inline void kvm_set_mp_state(struct kvm_vcpu *vcpu, int mp_state)
{
	vcpu->arch.mp_state = mp_state;
	if (mp_state == KVM_MP_STATE_RUNNABLE)
		vcpu->arch.pv.pv_unhalted = false;
}

static inline bool kvm_is_exception_pending(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.exception.pending ||
	       vcpu->arch.exception_vmexit.pending ||
	       kvm_test_request(KVM_REQ_TRIPLE_FAULT, vcpu);
}

static inline void kvm_clear_exception_queue(struct kvm_vcpu *vcpu)
{
	vcpu->arch.exception.pending = false;
	vcpu->arch.exception.injected = false;
	vcpu->arch.exception_vmexit.pending = false;
}

static inline void kvm_queue_interrupt(struct kvm_vcpu *vcpu, u8 vector,
	bool soft)
{
	vcpu->arch.interrupt.injected = true;
	vcpu->arch.interrupt.soft = soft;
	vcpu->arch.interrupt.nr = vector;
}

static inline void kvm_clear_interrupt_queue(struct kvm_vcpu *vcpu)
{
	vcpu->arch.interrupt.injected = false;
}

static inline bool kvm_event_needs_reinjection(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.exception.injected || vcpu->arch.interrupt.injected ||
		vcpu->arch.nmi_injected;
}

static inline bool kvm_exception_is_soft(unsigned int nr)
{
	return (nr == BP_VECTOR) || (nr == OF_VECTOR);
}

static inline bool is_protmode(struct kvm_vcpu *vcpu)
{
	return kvm_is_cr0_bit_set(vcpu, X86_CR0_PE);
}

static inline bool is_long_mode(struct kvm_vcpu *vcpu)
{
#ifdef CONFIG_X86_64
	return !!(vcpu->arch.efer & EFER_LMA);
#else
	return false;
#endif
}

static inline bool is_64_bit_mode(struct kvm_vcpu *vcpu)
{
	int cs_db, cs_l;

	WARN_ON_ONCE(vcpu->arch.guest_state_protected);

	if (!is_long_mode(vcpu))
		return false;
	kvm_x86_call(get_cs_db_l_bits)(vcpu, &cs_db, &cs_l);
	return cs_l;
}

static inline bool is_64_bit_hypercall(struct kvm_vcpu *vcpu)
{
	/*
	 * If running with protected guest state, the CS register is not
	 * accessible. The hypercall register values will have had to been
	 * provided in 64-bit mode, so assume the guest is in 64-bit.
	 */
	return vcpu->arch.guest_state_protected || is_64_bit_mode(vcpu);
}

static inline bool x86_exception_has_error_code(unsigned int vector)
{
	static u32 exception_has_error_code = BIT(DF_VECTOR) | BIT(TS_VECTOR) |
			BIT(NP_VECTOR) | BIT(SS_VECTOR) | BIT(GP_VECTOR) |
			BIT(PF_VECTOR) | BIT(AC_VECTOR);

	return (1U << vector) & exception_has_error_code;
}

static inline bool mmu_is_nested(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.walk_mmu == &vcpu->arch.nested_mmu;
}

static inline bool is_pae(struct kvm_vcpu *vcpu)
{
	return kvm_is_cr4_bit_set(vcpu, X86_CR4_PAE);
}

static inline bool is_pse(struct kvm_vcpu *vcpu)
{
	return kvm_is_cr4_bit_set(vcpu, X86_CR4_PSE);
}

static inline bool is_paging(struct kvm_vcpu *vcpu)
{
	return likely(kvm_is_cr0_bit_set(vcpu, X86_CR0_PG));
}

static inline bool is_pae_paging(struct kvm_vcpu *vcpu)
{
	return !is_long_mode(vcpu) && is_pae(vcpu) && is_paging(vcpu);
}

static inline u8 vcpu_virt_addr_bits(struct kvm_vcpu *vcpu)
{
	return kvm_is_cr4_bit_set(vcpu, X86_CR4_LA57) ? 57 : 48;
}

static inline u8 max_host_virt_addr_bits(void)
{
	return kvm_cpu_cap_has(X86_FEATURE_LA57) ? 57 : 48;
}

/*
 * x86 MSRs which contain linear addresses, x86 hidden segment bases, and
 * IDT/GDT bases have static canonicality checks, the size of which depends
 * only on the CPU's support for 5-level paging, rather than on the state of
 * CR4.LA57.  This applies to both WRMSR and to other instructions that set
 * their values, e.g. SGDT.
 *
 * KVM passes through most of these MSRS and also doesn't intercept the
 * instructions that set the hidden segment bases.
 *
 * Because of this, to be consistent with hardware, even if the guest doesn't
 * have LA57 enabled in its CPUID, perform canonicality checks based on *host*
 * support for 5 level paging.
 *
 * Finally, instructions which are related to MMU invalidation of a given
 * linear address, also have a similar static canonical check on address.
 * This allows for example to invalidate 5-level addresses of a guest from a
 * host which uses 4-level paging.
 */
static inline bool is_noncanonical_address(u64 la, struct kvm_vcpu *vcpu,
					   unsigned int flags)
{
	if (flags & (X86EMUL_F_INVLPG | X86EMUL_F_MSR | X86EMUL_F_DT_LOAD))
		return !__is_canonical_address(la, max_host_virt_addr_bits());
	else
		return !__is_canonical_address(la, vcpu_virt_addr_bits(vcpu));
}

static inline bool is_noncanonical_msr_address(u64 la, struct kvm_vcpu *vcpu)
{
	return is_noncanonical_address(la, vcpu, X86EMUL_F_MSR);
}

static inline bool is_noncanonical_base_address(u64 la, struct kvm_vcpu *vcpu)
{
	return is_noncanonical_address(la, vcpu, X86EMUL_F_DT_LOAD);
}

static inline bool is_noncanonical_invlpg_address(u64 la, struct kvm_vcpu *vcpu)
{
	return is_noncanonical_address(la, vcpu, X86EMUL_F_INVLPG);
}

static inline void vcpu_cache_mmio_info(struct kvm_vcpu *vcpu,
					gva_t gva, gfn_t gfn, unsigned access)
{
	u64 gen = kvm_memslots(vcpu->kvm)->generation;

	if (unlikely(gen & KVM_MEMSLOT_GEN_UPDATE_IN_PROGRESS))
		return;

	/*
	 * If this is a shadow nested page table, the "GVA" is
	 * actually a nGPA.
	 */
	vcpu->arch.mmio_gva = mmu_is_nested(vcpu) ? 0 : gva & PAGE_MASK;
	vcpu->arch.mmio_access = access;
	vcpu->arch.mmio_gfn = gfn;
	vcpu->arch.mmio_gen = gen;
}

static inline bool vcpu_match_mmio_gen(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.mmio_gen == kvm_memslots(vcpu->kvm)->generation;
}

/*
 * Clear the mmio cache info for the given gva. If gva is MMIO_GVA_ANY, we
 * clear all mmio cache info.
 */
#define MMIO_GVA_ANY (~(gva_t)0)

static inline void vcpu_clear_mmio_info(struct kvm_vcpu *vcpu, gva_t gva)
{
	if (gva != MMIO_GVA_ANY && vcpu->arch.mmio_gva != (gva & PAGE_MASK))
		return;

	vcpu->arch.mmio_gva = 0;
}

static inline bool vcpu_match_mmio_gva(struct kvm_vcpu *vcpu, unsigned long gva)
{
	if (vcpu_match_mmio_gen(vcpu) && vcpu->arch.mmio_gva &&
	      vcpu->arch.mmio_gva == (gva & PAGE_MASK))
		return true;

	return false;
}

static inline bool vcpu_match_mmio_gpa(struct kvm_vcpu *vcpu, gpa_t gpa)
{
	if (vcpu_match_mmio_gen(vcpu) && vcpu->arch.mmio_gfn &&
	      vcpu->arch.mmio_gfn == gpa >> PAGE_SHIFT)
		return true;

	return false;
}

static inline unsigned long kvm_register_read(struct kvm_vcpu *vcpu, int reg)
{
	unsigned long val = kvm_register_read_raw(vcpu, reg);

	return is_64_bit_mode(vcpu) ? val : (u32)val;
}

static inline void kvm_register_write(struct kvm_vcpu *vcpu,
				       int reg, unsigned long val)
{
	if (!is_64_bit_mode(vcpu))
		val = (u32)val;
	return kvm_register_write_raw(vcpu, reg, val);
}

static inline bool kvm_check_has_quirk(struct kvm *kvm, u64 quirk)
{
	return !(kvm->arch.disabled_quirks & quirk);
}

void kvm_inject_realmode_interrupt(struct kvm_vcpu *vcpu, int irq, int inc_eip);

u64 get_kvmclock_ns(struct kvm *kvm);
uint64_t kvm_get_wall_clock_epoch(struct kvm *kvm);
bool kvm_get_monotonic_and_clockread(s64 *kernel_ns, u64 *tsc_timestamp);
int kvm_guest_time_update(struct kvm_vcpu *v);

int kvm_read_guest_virt(struct kvm_vcpu *vcpu,
	gva_t addr, void *val, unsigned int bytes,
	struct x86_exception *exception);

int kvm_write_guest_virt_system(struct kvm_vcpu *vcpu,
	gva_t addr, void *val, unsigned int bytes,
	struct x86_exception *exception);

int handle_ud(struct kvm_vcpu *vcpu);

void kvm_deliver_exception_payload(struct kvm_vcpu *vcpu,
				   struct kvm_queued_exception *ex);

int kvm_mtrr_set_msr(struct kvm_vcpu *vcpu, u32 msr, u64 data);
int kvm_mtrr_get_msr(struct kvm_vcpu *vcpu, u32 msr, u64 *pdata);
bool kvm_vector_hashing_enabled(void);
void kvm_fixup_and_inject_pf_error(struct kvm_vcpu *vcpu, gva_t gva, u16 error_code);
int x86_decode_emulated_instruction(struct kvm_vcpu *vcpu, int emulation_type,
				    void *insn, int insn_len);
int x86_emulate_instruction(struct kvm_vcpu *vcpu, gpa_t cr2_or_gpa,
			    int emulation_type, void *insn, int insn_len);
fastpath_t handle_fastpath_set_msr_irqoff(struct kvm_vcpu *vcpu);
fastpath_t handle_fastpath_hlt(struct kvm_vcpu *vcpu);

extern struct kvm_caps kvm_caps;
extern struct kvm_host_values kvm_host;

extern bool enable_pmu;

/*
 * Get a filtered version of KVM's supported XCR0 that strips out dynamic
 * features for which the current process doesn't (yet) have permission to use.
 * This is intended to be used only when enumerating support to userspace,
 * e.g. in KVM_GET_SUPPORTED_CPUID and KVM_CAP_XSAVE2, it does NOT need to be
 * used to check/restrict guest behavior as KVM rejects KVM_SET_CPUID{2} if
 * userspace attempts to enable unpermitted features.
 */
static inline u64 kvm_get_filtered_xcr0(void)
{
	u64 permitted_xcr0 = kvm_caps.supported_xcr0;

	BUILD_BUG_ON(XFEATURE_MASK_USER_DYNAMIC != XFEATURE_MASK_XTILE_DATA);

	if (permitted_xcr0 & XFEATURE_MASK_USER_DYNAMIC) {
		permitted_xcr0 &= xstate_get_guest_group_perm();

		/*
		 * Treat XTILE_CFG as unsupported if the current process isn't
		 * allowed to use XTILE_DATA, as attempting to set XTILE_CFG in
		 * XCR0 without setting XTILE_DATA is architecturally illegal.
		 */
		if (!(permitted_xcr0 & XFEATURE_MASK_XTILE_DATA))
			permitted_xcr0 &= ~XFEATURE_MASK_XTILE_CFG;
	}
	return permitted_xcr0;
}

static inline bool kvm_mpx_supported(void)
{
	return (kvm_caps.supported_xcr0 & (XFEATURE_MASK_BNDREGS | XFEATURE_MASK_BNDCSR))
		== (XFEATURE_MASK_BNDREGS | XFEATURE_MASK_BNDCSR);
}

extern unsigned int min_timer_period_us;

extern bool enable_vmware_backdoor;

extern int pi_inject_timer;

extern bool report_ignored_msrs;

extern bool eager_page_split;

static inline void kvm_pr_unimpl_wrmsr(struct kvm_vcpu *vcpu, u32 msr, u64 data)
{
	if (report_ignored_msrs)
		vcpu_unimpl(vcpu, "Unhandled WRMSR(0x%x) = 0x%llx\n", msr, data);
}

static inline void kvm_pr_unimpl_rdmsr(struct kvm_vcpu *vcpu, u32 msr)
{
	if (report_ignored_msrs)
		vcpu_unimpl(vcpu, "Unhandled RDMSR(0x%x)\n", msr);
}

static inline u64 nsec_to_cycles(struct kvm_vcpu *vcpu, u64 nsec)
{
	return pvclock_scale_delta(nsec, vcpu->arch.virtual_tsc_mult,
				   vcpu->arch.virtual_tsc_shift);
}

/* Same "calling convention" as do_div:
 * - divide (n << 32) by base
 * - put result in n
 * - return remainder
 */
#define do_shl32_div32(n, base)					\
	({							\
	    u32 __quot, __rem;					\
	    asm("divl %2" : "=a" (__quot), "=d" (__rem)		\
			: "rm" (base), "0" (0), "1" ((u32) n));	\
	    n = __quot;						\
	    __rem;						\
	 })

static inline bool kvm_mwait_in_guest(struct kvm *kvm)
{
	return kvm->arch.mwait_in_guest;
}

static inline bool kvm_hlt_in_guest(struct kvm *kvm)
{
	return kvm->arch.hlt_in_guest;
}

static inline bool kvm_pause_in_guest(struct kvm *kvm)
{
	return kvm->arch.pause_in_guest;
}

static inline bool kvm_cstate_in_guest(struct kvm *kvm)
{
	return kvm->arch.cstate_in_guest;
}

static inline bool kvm_notify_vmexit_enabled(struct kvm *kvm)
{
	return kvm->arch.notify_vmexit_flags & KVM_X86_NOTIFY_VMEXIT_ENABLED;
}

static __always_inline void kvm_before_interrupt(struct kvm_vcpu *vcpu,
						 enum kvm_intr_type intr)
{
	WRITE_ONCE(vcpu->arch.handling_intr_from_guest, (u8)intr);
}

static __always_inline void kvm_after_interrupt(struct kvm_vcpu *vcpu)
{
	WRITE_ONCE(vcpu->arch.handling_intr_from_guest, 0);
}

static inline bool kvm_handling_nmi_from_guest(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.handling_intr_from_guest == KVM_HANDLING_NMI;
}

static inline bool kvm_pat_valid(u64 data)
{
	if (data & 0xF8F8F8F8F8F8F8F8ull)
		return false;
	/* 0, 1, 4, 5, 6, 7 are valid values.  */
	return (data | ((data & 0x0202020202020202ull) << 1)) == data;
}

static inline bool kvm_dr7_valid(u64 data)
{
	/* Bits [63:32] are reserved */
	return !(data >> 32);
}
static inline bool kvm_dr6_valid(u64 data)
{
	/* Bits [63:32] are reserved */
	return !(data >> 32);
}

/*
 * Trigger machine check on the host. We assume all the MSRs are already set up
 * by the CPU and that we still run on the same CPU as the MCE occurred on.
 * We pass a fake environment to the machine check handler because we want
 * the guest to be always treated like user space, no matter what context
 * it used internally.
 */
static inline void kvm_machine_check(void)
{
#if defined(CONFIG_X86_MCE)
	struct pt_regs regs = {
		.cs = 3, /* Fake ring 3 no matter what the guest ran on */
		.flags = X86_EFLAGS_IF,
	};

	do_machine_check(&regs);
#endif
}

void kvm_load_guest_xsave_state(struct kvm_vcpu *vcpu);
void kvm_load_host_xsave_state(struct kvm_vcpu *vcpu);
int kvm_spec_ctrl_test_value(u64 value);
int kvm_handle_memory_failure(struct kvm_vcpu *vcpu, int r,
			      struct x86_exception *e);
int kvm_handle_invpcid(struct kvm_vcpu *vcpu, unsigned long type, gva_t gva);
bool kvm_msr_allowed(struct kvm_vcpu *vcpu, u32 index, u32 type);

enum kvm_msr_access {
	MSR_TYPE_R	= BIT(0),
	MSR_TYPE_W	= BIT(1),
	MSR_TYPE_RW	= MSR_TYPE_R | MSR_TYPE_W,
};

/*
 * Internal error codes that are used to indicate that MSR emulation encountered
 * an error that should result in #GP in the guest, unless userspace handles it.
 * Note, '1', '0', and negative numbers are off limits, as they are used by KVM
 * as part of KVM's lightly documented internal KVM_RUN return codes.
 *
 * UNSUPPORTED	- The MSR isn't supported, either because it is completely
 *		  unknown to KVM, or because the MSR should not exist according
 *		  to the vCPU model.
 *
 * FILTERED	- Access to the MSR is denied by a userspace MSR filter.
 */
#define  KVM_MSR_RET_UNSUPPORTED	2
#define  KVM_MSR_RET_FILTERED		3

static inline bool __kvm_is_valid_cr4(struct kvm_vcpu *vcpu, unsigned long cr4)
{
	return !(cr4 & vcpu->arch.cr4_guest_rsvd_bits);
}

#define __cr4_reserved_bits(__cpu_has, __c)             \
({                                                      \
	u64 __reserved_bits = CR4_RESERVED_BITS;        \
                                                        \
	if (!__cpu_has(__c, X86_FEATURE_XSAVE))         \
		__reserved_bits |= X86_CR4_OSXSAVE;     \
	if (!__cpu_has(__c, X86_FEATURE_SMEP))          \
		__reserved_bits |= X86_CR4_SMEP;        \
	if (!__cpu_has(__c, X86_FEATURE_SMAP))          \
		__reserved_bits |= X86_CR4_SMAP;        \
	if (!__cpu_has(__c, X86_FEATURE_FSGSBASE))      \
		__reserved_bits |= X86_CR4_FSGSBASE;    \
	if (!__cpu_has(__c, X86_FEATURE_PKU))           \
		__reserved_bits |= X86_CR4_PKE;         \
	if (!__cpu_has(__c, X86_FEATURE_LA57))          \
		__reserved_bits |= X86_CR4_LA57;        \
	if (!__cpu_has(__c, X86_FEATURE_UMIP))          \
		__reserved_bits |= X86_CR4_UMIP;        \
	if (!__cpu_has(__c, X86_FEATURE_VMX))           \
		__reserved_bits |= X86_CR4_VMXE;        \
	if (!__cpu_has(__c, X86_FEATURE_PCID))          \
		__reserved_bits |= X86_CR4_PCIDE;       \
	if (!__cpu_has(__c, X86_FEATURE_LAM))           \
		__reserved_bits |= X86_CR4_LAM_SUP;     \
	__reserved_bits;                                \
})

int kvm_sev_es_mmio_write(struct kvm_vcpu *vcpu, gpa_t src, unsigned int bytes,
			  void *dst);
int kvm_sev_es_mmio_read(struct kvm_vcpu *vcpu, gpa_t src, unsigned int bytes,
			 void *dst);
int kvm_sev_es_string_io(struct kvm_vcpu *vcpu, unsigned int size,
			 unsigned int port, void *data,  unsigned int count,
			 int in);

static inline bool user_exit_on_hypercall(struct kvm *kvm, unsigned long hc_nr)
{
	return kvm->arch.hypercall_exit_enabled & BIT(hc_nr);
}

int ____kvm_emulate_hypercall(struct kvm_vcpu *vcpu, unsigned long nr,
			      unsigned long a0, unsigned long a1,
			      unsigned long a2, unsigned long a3,
			      int op_64_bit, int cpl,
			      int (*complete_hypercall)(struct kvm_vcpu *));

#define __kvm_emulate_hypercall(_vcpu, nr, a0, a1, a2, a3, op_64_bit, cpl, complete_hypercall)	\
({												\
	int __ret;										\
												\
	__ret = ____kvm_emulate_hypercall(_vcpu,						\
					  kvm_##nr##_read(_vcpu), kvm_##a0##_read(_vcpu),	\
					  kvm_##a1##_read(_vcpu), kvm_##a2##_read(_vcpu),	\
					  kvm_##a3##_read(_vcpu), op_64_bit, cpl,		\
					  complete_hypercall);					\
												\
	if (__ret > 0)										\
		__ret = complete_hypercall(_vcpu);						\
	__ret;											\
})

int kvm_emulate_hypercall(struct kvm_vcpu *vcpu);

#endif
