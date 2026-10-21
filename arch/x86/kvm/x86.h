/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARCH_X86_KVM_X86_H
#define ARCH_X86_KVM_X86_H

#include <linux/kvm_host.h>
#include <asm/fpu/xstate.h>
#include <asm/mce.h>
#include <asm/pvclock.h>
#include "msrs.h"
#include "mmu.h"
#include "regs.h"
#include "kvm_emulate.h"
#include "cpuid.h"

#define KVM_MAX_MCE_BANKS 32

int kvm_x86_vendor_init(struct kvm_x86_init_ops *ops);
void kvm_x86_vendor_exit(void);

void kvm_spurious_fault(void);

#define SIZE_OF_MEMSLOTS_HASHTABLE \
	(sizeof(((struct kvm_memslots *)0)->id_hash) * 2 * KVM_MAX_NR_ADDRESS_SPACES)

/* Sanity check the size of the memslot hash tables. */
static_assert(SIZE_OF_MEMSLOTS_HASHTABLE ==
	      (1024 * (1 + IS_ENABLED(CONFIG_X86_64)) * (1 + IS_ENABLED(CONFIG_KVM_SMM))));

/*
 * Assert that "struct kvm_{svm,vmx,tdx}" is an order-0 or order-1 allocation.
 * Spilling over to an order-2 allocation isn't fundamentally problematic, but
 * isn't expected to happen in the foreseeable future (O(years)).  Assert that
 * the size is an order-0 allocation when ignoring the memslot hash tables, to
 * help detect and debug unexpected size increases.
 */
#define KVM_SANITY_CHECK_VM_STRUCT_SIZE(x)						\
do {											\
	BUILD_BUG_ON(get_order(sizeof(struct x) - SIZE_OF_MEMSLOTS_HASHTABLE) &&	\
		     !IS_ENABLED(CONFIG_DEBUG_KERNEL) && !IS_ENABLED(CONFIG_KASAN));	\
	BUILD_BUG_ON(get_order(sizeof(struct x)) > 1 &&					\
		     !IS_ENABLED(CONFIG_DEBUG_KERNEL) && !IS_ENABLED(CONFIG_KASAN));	\
} while (0)

#define KVM_NESTED_VMENTER_CONSISTENCY_CHECK(consistency_check)		\
({									\
	bool failed = (consistency_check);				\
	if (failed)							\
		trace_kvm_nested_vmenter_failed(#consistency_check, 0);	\
	failed;								\
})

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

void kvm_service_local_tlb_flush_requests(struct kvm_vcpu *vcpu);
int kvm_check_nested_events(struct kvm_vcpu *vcpu);

/* Forcibly leave the nested mode in cases like a vCPU reset */
static inline void kvm_leave_nested(struct kvm_vcpu *vcpu)
{
	kvm_nested_call(leave_nested)(vcpu);
}

/*
 * If IBRS is advertised to the vCPU, KVM must flush the indirect branch
 * predictors when transitioning from L2 to L1, as L1 expects hardware (KVM in
 * this case) to provide separate predictor modes.  Bare metal isolates the host
 * from the guest, but doesn't isolate different guests from one another (in
 * this case L1 and L2). The exception is if bare metal supports same mode IBRS,
 * which offers protection within the same mode, and hence protects L1 from L2.
 */
static inline void kvm_nested_vmexit_handle_ibrs(struct kvm_vcpu *vcpu)
{
	if (cpu_feature_enabled(X86_FEATURE_AMD_IBRS_SAME_MODE))
		return;

	if (guest_cpu_cap_has(vcpu, X86_FEATURE_SPEC_CTRL) ||
	    guest_cpu_cap_has(vcpu, X86_FEATURE_AMD_IBRS))
		indirect_branch_prediction_barrier();
}

/*
 * Disallow modifying CPUID and feature MSRs, which affect the core virtual CPU
 * model exposed to the guest and virtualized by KVM, if the vCPU has already
 * run or is in guest mode (L2).  In both cases, KVM has already consumed the
 * current virtual CPU model, and doesn't support "unwinding" to react to the
 * new model.
 *
 * Note, the only way is_guest_mode() can be true with 'last_vmentry_cpu == -1'
 * is if userspace sets CPUID and feature MSRs (to enable VMX/SVM), then sets
 * nested state, and then attempts to set CPUID and/or feature MSRs *again*.
 */
static inline bool kvm_can_set_cpuid_and_feature_msrs(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.last_vmentry_cpu == -1 && !is_guest_mode(vcpu);
}

/*
 * WARN if a nested VM-Enter is pending completion, and userspace hasn't gained
 * control since the nested VM-Enter was initiated (in which case, userspace
 * may have modified vCPU state to induce an architecturally invalid VM-Exit).
 */
static inline void kvm_warn_on_nested_run_pending(struct kvm_vcpu *vcpu)
{
	WARN_ON_ONCE(vcpu->arch.nested_run_pending == KVM_NESTED_RUN_PENDING);
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

static inline bool x86_exception_has_error_code(unsigned int vector)
{
	static u32 exception_has_error_code = BIT(DF_VECTOR) | BIT(TS_VECTOR) |
			BIT(NP_VECTOR) | BIT(SS_VECTOR) | BIT(GP_VECTOR) |
			BIT(PF_VECTOR) | BIT(AC_VECTOR);

	return (1U << vector) & exception_has_error_code;
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

static inline bool kvm_check_has_quirk(struct kvm *kvm, u64 quirk)
{
	return !(READ_ONCE(kvm->arch.disabled_quirks) & quirk);
}

static __always_inline void kvm_request_l1tf_flush_l1d(void)
{
#if IS_ENABLED(CONFIG_CPU_MITIGATIONS) && IS_ENABLED(CONFIG_KVM_INTEL)
	/*
	 * Use a raw write to set the per-CPU flag, as KVM will ensure a flush
	 * even if preemption is currently enabled..  If the current vCPU task
	 * is migrated to a different CPU (or userspace runs the vCPU on a
	 * different task) before the next VM-Entry, then kvm_arch_vcpu_load()
	 * will request a flush on the new CPU.
	 */
	raw_cpu_write(irq_stat.kvm_cpu_l1tf_flush_l1d, 1);
#endif
}

void kvm_vcpu_reset(struct kvm_vcpu *vcpu, bool init_event);

void kvm_inject_realmode_interrupt(struct kvm_vcpu *vcpu, int irq, int inc_eip);

u64 get_kvmclock_ns(struct kvm *kvm);
uint64_t kvm_get_wall_clock_epoch(struct kvm *kvm);
bool kvm_get_monotonic_and_clockread(s64 *kernel_ns, u64 *tsc_timestamp);
int kvm_guest_time_update(struct kvm_vcpu *v);

void kvm_synchronize_tsc(struct kvm_vcpu *vcpu, u64 *user_value);
u64 kvm_scale_tsc(u64 tsc, u64 ratio);
u64 kvm_read_l1_tsc(struct kvm_vcpu *vcpu, u64 host_tsc);
u64 kvm_calc_nested_tsc_offset(u64 l1_offset, u64 l2_offset, u64 l2_multiplier);
u64 kvm_calc_nested_tsc_multiplier(u64 l1_multiplier, u64 l2_multiplier);
u64 kvm_compute_l1_tsc_offset(struct kvm_vcpu *vcpu, u64 target_tsc);
void kvm_vcpu_write_tsc_offset(struct kvm_vcpu *vcpu, u64 l1_offset);

static inline void adjust_tsc_offset_guest(struct kvm_vcpu *vcpu,
					   s64 adjustment)
{
	kvm_vcpu_write_tsc_offset(vcpu, vcpu->arch.l1_tsc_offset + adjustment);
}

static inline void adjust_tsc_offset_host(struct kvm_vcpu *vcpu, s64 adjustment)
{
	if (vcpu->arch.l1_tsc_scaling_ratio != kvm_caps.default_tsc_scaling_ratio)
		WARN_ON(adjustment < 0);
	adjustment = kvm_scale_tsc((u64) adjustment,
				   vcpu->arch.l1_tsc_scaling_ratio);
	adjust_tsc_offset_guest(vcpu, adjustment);
}

int kvm_read_guest_virt(struct kvm_vcpu *vcpu,
	gva_t addr, void *val, unsigned int bytes,
	struct x86_exception *exception);

int kvm_write_guest_virt_system(struct kvm_vcpu *vcpu,
	gva_t addr, void *val, unsigned int bytes,
	struct x86_exception *exception);

int handle_ud(struct kvm_vcpu *vcpu);

void kvm_deliver_exception_payload(struct kvm_vcpu *vcpu,
				   struct kvm_queued_exception *ex);
void kvm_handle_exception_payload_quirk(struct kvm_vcpu *vcpu);

void kvm_fixup_and_inject_pf_error(struct kvm_vcpu *vcpu, gva_t gva, u16 error_code);
int x86_decode_emulated_instruction(struct kvm_vcpu *vcpu, int emulation_type,
				    void *insn, int insn_len);
int x86_emulate_instruction(struct kvm_vcpu *vcpu, gpa_t cr2_or_gpa,
			    int emulation_type, void *insn, int insn_len);
/*
 * EMULTYPE_NO_DECODE - Set when re-emulating an instruction (after completing
 *			userspace I/O) to indicate that the emulation context
 *			should be reused as is, i.e. skip initialization of
 *			emulation context, instruction fetch and decode.
 *
 * EMULTYPE_TRAP_UD - Set when emulating an intercepted #UD from hardware.
 *		      Indicates that only select instructions (tagged with
 *		      EmulateOnUD) should be emulated (to minimize the emulator
 *		      attack surface).  See also EMULTYPE_TRAP_UD_FORCED.
 *
 * EMULTYPE_SKIP - Set when emulating solely to skip an instruction, i.e. to
 *		   decode the instruction length.  For use *only* by
 *		   kvm_x86_ops.skip_emulated_instruction() implementations if
 *		   EMULTYPE_COMPLETE_USER_EXIT is not set.
 *
 * EMULTYPE_ALLOW_RETRY_PF - Set when the emulator should resume the guest to
 *			     retry native execution under certain conditions,
 *			     Can only be set in conjunction with EMULTYPE_PF.
 *
 * EMULTYPE_TRAP_UD_FORCED - Set when emulating an intercepted #UD that was
 *			     triggered by KVM's magic "force emulation" prefix,
 *			     which is opt in via module param (off by default).
 *			     Bypasses EmulateOnUD restriction despite emulating
 *			     due to an intercepted #UD (see EMULTYPE_TRAP_UD).
 *			     Used to test the full emulator from userspace.
 *
 * EMULTYPE_VMWARE_GP - Set when emulating an intercepted #GP for VMware
 *			backdoor emulation, which is opt in via module param.
 *			VMware backdoor emulation handles select instructions
 *			and reinjects the #GP for all other cases.
 *
 * EMULTYPE_PF - Set when an intercepted #PF triggers the emulation, in which case
 *		 the CR2/GPA value pass on the stack is valid.
 *
 * EMULTYPE_COMPLETE_USER_EXIT - Set when the emulator should update interruptibility
 *				 state and inject single-step #DBs after skipping
 *				 an instruction (after completing userspace I/O).
 *
 * EMULTYPE_WRITE_PF_TO_SP - Set when emulating an intercepted page fault that
 *			     is attempting to write a gfn that contains one or
 *			     more of the PTEs used to translate the write itself,
 *			     and the owning page table is being shadowed by KVM.
 *			     If emulation of the faulting instruction fails and
 *			     this flag is set, KVM will exit to userspace instead
 *			     of retrying emulation as KVM cannot make forward
 *			     progress.
 *
 *			     If emulation fails for a write to guest page tables,
 *			     KVM unprotects (zaps) the shadow page for the target
 *			     gfn and resumes the guest to retry the non-emulatable
 *			     instruction (on hardware).  Unprotecting the gfn
 *			     doesn't allow forward progress for a self-changing
 *			     access because doing so also zaps the translation for
 *			     the gfn, i.e. retrying the instruction will hit a
 *			     !PRESENT fault, which results in a new shadow page
 *			     and sends KVM back to square one.
 *
 * EMULTYPE_SKIP_SOFT_INT - Set in combination with EMULTYPE_SKIP to only skip
 *                          an instruction if it could generate a given software
 *                          interrupt, which must be encoded via
 *                          EMULTYPE_SET_SOFT_INT_VECTOR().
 */
#define EMULTYPE_NO_DECODE	    (1 << 0)
#define EMULTYPE_TRAP_UD	    (1 << 1)
#define EMULTYPE_SKIP		    (1 << 2)
#define EMULTYPE_ALLOW_RETRY_PF	    (1 << 3)
#define EMULTYPE_TRAP_UD_FORCED	    (1 << 4)
#define EMULTYPE_VMWARE_GP	    (1 << 5)
#define EMULTYPE_PF		    (1 << 6)
#define EMULTYPE_COMPLETE_USER_EXIT (1 << 7)
#define EMULTYPE_WRITE_PF_TO_SP	    (1 << 8)
#define EMULTYPE_SKIP_SOFT_INT	    (1 << 9)

#define EMULTYPE_SET_SOFT_INT_VECTOR(v)	((u32)((v) & 0xff) << 16)
#define EMULTYPE_GET_SOFT_INT_VECTOR(e)	(((e) >> 16) & 0xff)

static inline bool kvm_can_emulate_event_vectoring(int emul_type)
{
	return !(emul_type & EMULTYPE_PF);
}

int kvm_emulate_instruction(struct kvm_vcpu *vcpu, int emulation_type);
int kvm_emulate_instruction_from_buffer(struct kvm_vcpu *vcpu,
					void *insn, int insn_len);
void __kvm_prepare_emulation_failure_exit(struct kvm_vcpu *vcpu,
					  u64 *data, u8 ndata);
void kvm_prepare_emulation_failure_exit(struct kvm_vcpu *vcpu);

void kvm_prepare_event_vectoring_exit(struct kvm_vcpu *vcpu, gpa_t gpa);
void kvm_prepare_unexpected_reason_exit(struct kvm_vcpu *vcpu, u64 exit_reason);

fastpath_t handle_fastpath_hlt(struct kvm_vcpu *vcpu);
fastpath_t handle_fastpath_invd(struct kvm_vcpu *vcpu);

int kvm_emulate_as_nop(struct kvm_vcpu *vcpu);
int kvm_emulate_invd(struct kvm_vcpu *vcpu);
int kvm_emulate_mwait(struct kvm_vcpu *vcpu);
int kvm_handle_invalid_op(struct kvm_vcpu *vcpu);
int kvm_emulate_monitor(struct kvm_vcpu *vcpu);

int kvm_fast_pio(struct kvm_vcpu *vcpu, int size, unsigned short port, int in);
int kvm_emulate_cpuid(struct kvm_vcpu *vcpu);
int kvm_emulate_halt(struct kvm_vcpu *vcpu);
int kvm_emulate_halt_noskip(struct kvm_vcpu *vcpu);
int kvm_emulate_ap_reset_hold(struct kvm_vcpu *vcpu);
int kvm_emulate_wbinvd(struct kvm_vcpu *vcpu);

void kvm_vcpu_deliver_sipi_vector(struct kvm_vcpu *vcpu, u8 vector);

enum kvm_task_switch_reason {
	TASK_SWITCH_CALL = 0,
	TASK_SWITCH_IRET = 1,
	TASK_SWITCH_JMP = 2,
	TASK_SWITCH_GATE = 3,
};
int kvm_task_switch(struct kvm_vcpu *vcpu, u16 tss_selector, int idt_index,
		    int reason, bool has_error_code, u32 error_code);

int __kvm_set_xcr(struct kvm_vcpu *vcpu, u32 index, u64 xcr);
int kvm_emulate_xsetbv(struct kvm_vcpu *vcpu);
int kvm_emulate_rdpmc(struct kvm_vcpu *vcpu);

int kvm_skip_emulated_instruction(struct kvm_vcpu *vcpu);
int kvm_complete_insn_gp(struct kvm_vcpu *vcpu, int err);

void kvm_queue_exception(struct kvm_vcpu *vcpu, unsigned nr);
void kvm_queue_exception_e(struct kvm_vcpu *vcpu, unsigned nr, u32 error_code);
void kvm_queue_exception_p(struct kvm_vcpu *vcpu, unsigned nr, unsigned long payload);
void kvm_requeue_exception(struct kvm_vcpu *vcpu, unsigned int nr,
			   bool has_error_code, u32 error_code);
void kvm_inject_page_fault(struct kvm_vcpu *vcpu, struct x86_exception *fault,
			   bool from_hardware);
void __kvm_inject_emulated_page_fault(struct kvm_vcpu *vcpu,
				      struct x86_exception *fault,
				      bool from_hardware);

static inline void kvm_inject_emulated_page_fault(struct kvm_vcpu *vcpu,
						  struct x86_exception *fault)
{
	__kvm_inject_emulated_page_fault(vcpu, fault, false);
}

bool kvm_require_dr(struct kvm_vcpu *vcpu, int dr);

static inline void kvm_inject_gp(struct kvm_vcpu *vcpu, u32 error_code)
{
	kvm_queue_exception_e(vcpu, GP_VECTOR, error_code);
}

void kvm_inject_nmi(struct kvm_vcpu *vcpu);
int kvm_get_nr_pending_nmis(struct kvm_vcpu *vcpu);

void __user *__x86_set_memory_region(struct kvm *kvm, int id, gpa_t gpa,
				     u32 size);
int memslot_rmap_alloc(struct kvm_memory_slot *slot, unsigned long npages);

bool kvm_vcpu_is_reset_bsp(struct kvm_vcpu *vcpu);
bool kvm_vcpu_is_bsp(struct kvm_vcpu *vcpu);

enum kvm_apicv_inhibit {

	/********************************************************************/
	/* INHIBITs that are relevant to both Intel's APICv and AMD's AVIC. */
	/********************************************************************/

	/*
	 * APIC acceleration is disabled by a module parameter
	 * and/or not supported in hardware.
	 */
	APICV_INHIBIT_REASON_DISABLED,

	/*
	 * APIC acceleration is inhibited because AutoEOI feature is
	 * being used by a HyperV guest.
	 */
	APICV_INHIBIT_REASON_HYPERV,

	/*
	 * APIC acceleration is inhibited because the userspace didn't yet
	 * enable the kernel/split irqchip.
	 */
	APICV_INHIBIT_REASON_ABSENT,

	/* APIC acceleration is inhibited because KVM_GUESTDBG_BLOCKIRQ
	 * (out of band, debug measure of blocking all interrupts on this vCPU)
	 * was enabled, to avoid AVIC/APICv bypassing it.
	 */
	APICV_INHIBIT_REASON_BLOCKIRQ,

	/*
	 * APICv is disabled because not all vCPUs have a 1:1 mapping between
	 * APIC ID and vCPU, _and_ KVM is not applying its x2APIC hotplug hack.
	 */
	APICV_INHIBIT_REASON_PHYSICAL_ID_ALIASED,

	/*
	 * For simplicity, the APIC acceleration is inhibited
	 * first time either APIC ID or APIC base are changed by the guest
	 * from their reset values.
	 */
	APICV_INHIBIT_REASON_APIC_ID_MODIFIED,
	APICV_INHIBIT_REASON_APIC_BASE_MODIFIED,

	/******************************************************/
	/* INHIBITs that are relevant only to the AMD's AVIC. */
	/******************************************************/

	/*
	 * AVIC is inhibited on a vCPU because it runs a nested guest.
	 *
	 * This is needed because unlike APICv, the peers of this vCPU
	 * cannot use the doorbell mechanism to signal interrupts via AVIC when
	 * a vCPU runs nested.
	 */
	APICV_INHIBIT_REASON_NESTED,

	/*
	 * On SVM, the wait for the IRQ window is implemented with pending vIRQ,
	 * which cannot be injected when the AVIC is enabled, thus AVIC
	 * is inhibited while KVM waits for IRQ window.
	 */
	APICV_INHIBIT_REASON_IRQWIN,

	/*
	 * PIT (i8254) 're-inject' mode, relies on EOI intercept,
	 * which AVIC doesn't support for edge triggered interrupts.
	 */
	APICV_INHIBIT_REASON_PIT_REINJ,

	/*
	 * AVIC is disabled because SEV doesn't support it.
	 */
	APICV_INHIBIT_REASON_SEV,

	/*
	 * AVIC is disabled because not all vCPUs with a valid LDR have a 1:1
	 * mapping between logical ID and vCPU.
	 */
	APICV_INHIBIT_REASON_LOGICAL_ID_ALIASED,

	/*
	 * AVIC is disabled because the vCPU's APIC ID is beyond the max
	 * supported by AVIC/x2AVIC, i.e. the vCPU is unaddressable.
	 */
	APICV_INHIBIT_REASON_PHYSICAL_ID_TOO_BIG,

	NR_APICV_INHIBIT_REASONS,
};

#define __APICV_INHIBIT_REASON(reason)			\
	{ BIT(APICV_INHIBIT_REASON_##reason), #reason }

#define APICV_INHIBIT_REASONS				\
	__APICV_INHIBIT_REASON(DISABLED),		\
	__APICV_INHIBIT_REASON(HYPERV),			\
	__APICV_INHIBIT_REASON(ABSENT),			\
	__APICV_INHIBIT_REASON(BLOCKIRQ),		\
	__APICV_INHIBIT_REASON(PHYSICAL_ID_ALIASED),	\
	__APICV_INHIBIT_REASON(APIC_ID_MODIFIED),	\
	__APICV_INHIBIT_REASON(APIC_BASE_MODIFIED),	\
	__APICV_INHIBIT_REASON(NESTED),			\
	__APICV_INHIBIT_REASON(IRQWIN),			\
	__APICV_INHIBIT_REASON(PIT_REINJ),		\
	__APICV_INHIBIT_REASON(SEV),			\
	__APICV_INHIBIT_REASON(LOGICAL_ID_ALIASED),	\
	__APICV_INHIBIT_REASON(PHYSICAL_ID_TOO_BIG)

bool kvm_apicv_activated(struct kvm *kvm);
bool kvm_vcpu_apicv_activated(struct kvm_vcpu *vcpu);
void __kvm_vcpu_update_apicv(struct kvm_vcpu *vcpu);
void __kvm_set_or_clear_apicv_inhibit(struct kvm *kvm,
				      enum kvm_apicv_inhibit reason, bool set);
void kvm_set_or_clear_apicv_inhibit(struct kvm *kvm,
				    enum kvm_apicv_inhibit reason, bool set);

static inline void kvm_set_apicv_inhibit(struct kvm *kvm,
					 enum kvm_apicv_inhibit reason)
{
	kvm_set_or_clear_apicv_inhibit(kvm, reason, true);
}

static inline void kvm_clear_apicv_inhibit(struct kvm *kvm,
					   enum kvm_apicv_inhibit reason)
{
	kvm_set_or_clear_apicv_inhibit(kvm, reason, false);
}

void kvm_inc_or_dec_irq_window_inhibit(struct kvm *kvm, bool inc);

static inline void kvm_inc_apicv_irq_window_req(struct kvm *kvm)
{
	kvm_inc_or_dec_irq_window_inhibit(kvm, true);
}

static inline void kvm_dec_apicv_irq_window_req(struct kvm *kvm)
{
	kvm_inc_or_dec_irq_window_inhibit(kvm, false);
}

void kvm_make_scan_ioapic_request(struct kvm *kvm);
void kvm_make_scan_ioapic_request_mask(struct kvm *kvm,
				       unsigned long *vcpu_bitmap);

void kvm_setup_xss_caps(void);

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

static inline void kvm_disable_exits(struct kvm *kvm, u64 mask)
{
	kvm->arch.disabled_exits |= mask;
}

static inline bool kvm_mwait_in_guest(struct kvm *kvm)
{
	return kvm->arch.disabled_exits & KVM_X86_DISABLE_EXITS_MWAIT;
}

static inline bool kvm_hlt_in_guest(struct kvm *kvm)
{
	return kvm->arch.disabled_exits & KVM_X86_DISABLE_EXITS_HLT;
}

static inline bool kvm_pause_in_guest(struct kvm *kvm)
{
	return kvm->arch.disabled_exits & KVM_X86_DISABLE_EXITS_PAUSE;
}

static inline bool kvm_cstate_in_guest(struct kvm *kvm)
{
	return kvm->arch.disabled_exits & KVM_X86_DISABLE_EXITS_CSTATE;
}

static inline bool kvm_aperfmperf_in_guest(struct kvm *kvm)
{
	return kvm->arch.disabled_exits & KVM_X86_DISABLE_EXITS_APERFMPERF;
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

static inline bool __kvm_pv_async_pf_enabled(u64 data)
{
	u64 mask = KVM_ASYNC_PF_ENABLED | KVM_ASYNC_PF_DELIVERY_AS_INT;

	return (data & mask) == mask;
}

static inline bool kvm_pv_async_pf_enabled(struct kvm_vcpu *vcpu)
{
	return __kvm_pv_async_pf_enabled(vcpu->arch.apf.msr_en_val);
}

static inline void kvm_async_pf_hash_reset(struct kvm_vcpu *vcpu)
{
	int i;
	for (i = 0; i < ASYNC_PF_PER_VCPU; i++)
		vcpu->arch.apf.gfns[i] = ~0;
}

bool kvm_find_async_pf_gfn(struct kvm_vcpu *vcpu, gfn_t gfn);

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

int kvm_handle_memory_failure(struct kvm_vcpu *vcpu, int r,
			      struct x86_exception *e);
void kvm_invalidate_pcid(struct kvm_vcpu *vcpu, unsigned long pcid);
int kvm_handle_invpcid(struct kvm_vcpu *vcpu, unsigned long type, gva_t gva);

int kvm_sev_es_mmio(struct kvm_vcpu *vcpu, bool is_write, gpa_t gpa,
		    unsigned int bytes, void *data);
int kvm_sev_es_string_io(struct kvm_vcpu *vcpu, unsigned int size,
			 unsigned int port, void *data,  unsigned int count,
			 int in);

static inline void __kvm_prepare_emulated_mmio_exit(struct kvm_vcpu *vcpu,
						    gpa_t gpa, unsigned int len,
						    const void *data,
						    bool is_write)
{
	struct kvm_run *run = vcpu->run;

	KVM_BUG_ON(len > 8, vcpu->kvm);

	run->mmio.len = len;
	run->mmio.is_write = is_write;
	run->exit_reason = KVM_EXIT_MMIO;
	run->mmio.phys_addr = gpa;
	if (is_write)
		memcpy(run->mmio.data, data, len);
}

static inline void kvm_prepare_emulated_mmio_exit(struct kvm_vcpu *vcpu,
						  struct kvm_mmio_fragment *frag)
{
	WARN_ON_ONCE(!vcpu->mmio_needed || !vcpu->mmio_nr_fragments);

	__kvm_prepare_emulated_mmio_exit(vcpu, frag->gpa, min(8u, frag->len),
					 frag->data, vcpu->mmio_is_write);
}

static inline bool kvm_is_valid_map_gpa_range_ret(u64 hypercall_ret)
{
	return !hypercall_ret || hypercall_ret == EINVAL ||
	       hypercall_ret == EAGAIN;
}

static inline bool user_exit_on_hypercall(struct kvm *kvm, unsigned long hc_nr)
{
	return kvm->arch.hypercall_exit_enabled & BIT(hc_nr);
}

int ____kvm_emulate_hypercall(struct kvm_vcpu *vcpu, int cpl,
			      int (*complete_hypercall)(struct kvm_vcpu *));

#define __kvm_emulate_hypercall(_vcpu, cpl, complete_hypercall)			\
({										\
	int __ret;								\
	__ret = ____kvm_emulate_hypercall(_vcpu, cpl, complete_hypercall);	\
										\
	if (__ret > 0)								\
		__ret = complete_hypercall(_vcpu);				\
	__ret;									\
})

int kvm_emulate_hypercall(struct kvm_vcpu *vcpu);

#endif
