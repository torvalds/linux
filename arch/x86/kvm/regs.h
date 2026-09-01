/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARCH_X86_KVM_REGS_H
#define ARCH_X86_KVM_REGS_H

#include <linux/kvm_host.h>

#define KVM_POSSIBLE_CR0_GUEST_BITS	(X86_CR0_TS | X86_CR0_WP)
#define KVM_POSSIBLE_CR4_GUEST_BITS				  \
	(X86_CR4_PVI | X86_CR4_DE | X86_CR4_PCE | X86_CR4_OSFXSR  \
	 | X86_CR4_OSXMMEXCPT | X86_CR4_PGE | X86_CR4_TSD | X86_CR4_FSGSBASE \
	 | X86_CR4_CET)

#define X86_CR0_PDPTR_BITS    (X86_CR0_CD | X86_CR0_NW | X86_CR0_PG)
#define X86_CR4_TLBFLUSH_BITS (X86_CR4_PGE | X86_CR4_PCIDE | X86_CR4_PAE | X86_CR4_SMEP)
#define X86_CR4_PDPTR_BITS    (X86_CR4_PGE | X86_CR4_PSE | X86_CR4_PAE | X86_CR4_SMEP)

static_assert(!(KVM_POSSIBLE_CR0_GUEST_BITS & X86_CR0_PDPTR_BITS));

#define CR0_RESERVED_BITS                                               \
	(~(unsigned long)(X86_CR0_PE | X86_CR0_MP | X86_CR0_EM | X86_CR0_TS \
			  | X86_CR0_ET | X86_CR0_NE | X86_CR0_WP | X86_CR0_AM \
			  | X86_CR0_NW | X86_CR0_CD | X86_CR0_PG))

#define CR4_RESERVED_BITS                                               \
	(~(unsigned long)(X86_CR4_VME | X86_CR4_PVI | X86_CR4_TSD | X86_CR4_DE\
			  | X86_CR4_PSE | X86_CR4_PAE | X86_CR4_MCE     \
			  | X86_CR4_PGE | X86_CR4_PCE | X86_CR4_OSFXSR | X86_CR4_PCIDE \
			  | X86_CR4_OSXSAVE | X86_CR4_SMEP | X86_CR4_FSGSBASE \
			  | X86_CR4_OSXMMEXCPT | X86_CR4_LA57 | X86_CR4_VMXE \
			  | X86_CR4_SMAP | X86_CR4_PKE | X86_CR4_UMIP \
			  | X86_CR4_LAM_SUP | X86_CR4_CET))

#define CR8_RESERVED_BITS (~(unsigned long)X86_CR8_TPR)

#define DR6_BUS_LOCK   (1 << 11)
#define DR6_BD		(1 << 13)
#define DR6_BS		(1 << 14)
#define DR6_BT		(1 << 15)
#define DR6_RTM		(1 << 16)
/*
 * DR6_ACTIVE_LOW combines fixed-1 and active-low bits.
 * We can regard all the bits in DR6_FIXED_1 as active_low bits;
 * they will never be 0 for now, but when they are defined
 * in the future it will require no code change.
 *
 * DR6_ACTIVE_LOW is also used as the init/reset value for DR6.
 */
#define DR6_ACTIVE_LOW	0xffff0ff0
#define DR6_VOLATILE	0x0001e80f
#define DR6_FIXED_1	(DR6_ACTIVE_LOW & ~DR6_VOLATILE)

#define DR7_BP_EN_MASK	0x000000ff
#define DR7_GE		(1 << 9)
#define DR7_GD		(1 << 13)
#define DR7_VOLATILE	0xffff2bff

void kvm_post_set_cr0(struct kvm_vcpu *vcpu, unsigned long old_cr0, unsigned long cr0);
void kvm_post_set_cr4(struct kvm_vcpu *vcpu, unsigned long old_cr4, unsigned long cr4);
int kvm_set_cr0(struct kvm_vcpu *vcpu, unsigned long cr0);
int kvm_set_cr3(struct kvm_vcpu *vcpu, unsigned long cr3);
int kvm_set_cr4(struct kvm_vcpu *vcpu, unsigned long cr4);
int kvm_set_cr8(struct kvm_vcpu *vcpu, unsigned long cr8);
int kvm_set_dr(struct kvm_vcpu *vcpu, int dr, unsigned long val);
unsigned long kvm_get_dr(struct kvm_vcpu *vcpu, int dr);
unsigned long kvm_get_cr8(struct kvm_vcpu *vcpu);
void kvm_lmsw(struct kvm_vcpu *vcpu, unsigned long msw);
int load_pdptrs(struct kvm_vcpu *vcpu, unsigned long cr3);

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
#ifdef CONFIG_X86_64
	/*
	 * If running with protected guest state, the CS register is not
	 * accessible. The hypercall register values will have had to been
	 * provided in 64-bit mode, so assume the guest is in 64-bit.
	 */
	return vcpu->arch.guest_state_protected || is_64_bit_mode(vcpu);
#else
	return false;
#endif
}

static __always_inline unsigned long kvm_reg_mode_mask(struct kvm_vcpu *vcpu)
{
#ifdef CONFIG_X86_64
	return is_64_bit_mode(vcpu) ? GENMASK(63, 0) : GENMASK(31, 0);
#else
	return GENMASK(31, 0);
#endif
}

#define __BUILD_KVM_GPR_ACCESSORS(lname, uname)						\
static __always_inline unsigned long kvm_##lname##_read(struct kvm_vcpu *vcpu)		\
{											\
	return vcpu->arch.regs[VCPU_REGS_##uname] & kvm_reg_mode_mask(vcpu);		\
}											\
static __always_inline unsigned long kvm_##lname##_read_raw(struct kvm_vcpu *vcpu)	\
{											\
	return vcpu->arch.regs[VCPU_REGS_##uname];					\
}											\
static __always_inline void kvm_##lname##_write_raw(struct kvm_vcpu *vcpu,		\
						    unsigned long val)			\
{											\
	vcpu->arch.regs[VCPU_REGS_##uname] = val;					\
}
#define BUILD_KVM_GPR_ACCESSORS(lname, uname)						\
static __always_inline u32 kvm_e##lname##_read(struct kvm_vcpu *vcpu)			\
{											\
	return vcpu->arch.regs[VCPU_REGS_##uname];					\
}											\
static __always_inline void kvm_e##lname##_write(struct kvm_vcpu *vcpu, u32 val)	\
{											\
	vcpu->arch.regs[VCPU_REGS_##uname] = val;					\
}											\
__BUILD_KVM_GPR_ACCESSORS(r##lname, uname)

BUILD_KVM_GPR_ACCESSORS(ax, RAX)
BUILD_KVM_GPR_ACCESSORS(bx, RBX)
BUILD_KVM_GPR_ACCESSORS(cx, RCX)
BUILD_KVM_GPR_ACCESSORS(dx, RDX)
BUILD_KVM_GPR_ACCESSORS(bp, RBP)
BUILD_KVM_GPR_ACCESSORS(si, RSI)
BUILD_KVM_GPR_ACCESSORS(di, RDI)
#ifdef CONFIG_X86_64
__BUILD_KVM_GPR_ACCESSORS(r8,  R8)
__BUILD_KVM_GPR_ACCESSORS(r9,  R9)
__BUILD_KVM_GPR_ACCESSORS(r10, R10)
__BUILD_KVM_GPR_ACCESSORS(r11, R11)
__BUILD_KVM_GPR_ACCESSORS(r12, R12)
__BUILD_KVM_GPR_ACCESSORS(r13, R13)
__BUILD_KVM_GPR_ACCESSORS(r14, R14)
__BUILD_KVM_GPR_ACCESSORS(r15, R15)
#endif

/*
 * Using the register cache from interrupt context is generally not allowed, as
 * caching a register and marking it available/dirty can't be done atomically,
 * i.e. accesses from interrupt context may clobber state or read stale data if
 * the vCPU task is in the process of updating the cache.  The exception is if
 * KVM is handling a PMI IRQ/NMI VM-Exit, as that bound code sequence doesn't
 * touch the cache, it runs after the cache is reset (post VM-Exit), and PMIs
 * need to access several registers that are cacheable.
 */
#define kvm_assert_register_caching_allowed(vcpu)		\
	lockdep_assert_once(in_task() || kvm_arch_pmi_in_guest(vcpu))

/*
 * avail  dirty
 * 0	  0	  register in VMCS/VMCB
 * 0	  1	  *INVALID*
 * 1	  0	  register in vcpu->arch
 * 1	  1	  register in vcpu->arch, needs to be stored back
 */
static inline bool kvm_register_is_available(struct kvm_vcpu *vcpu,
					     enum kvm_reg reg)
{
	kvm_assert_register_caching_allowed(vcpu);
	return test_bit(reg, vcpu->arch.regs_avail);
}

static inline bool kvm_register_is_dirty(struct kvm_vcpu *vcpu,
					 enum kvm_reg reg)
{
	kvm_assert_register_caching_allowed(vcpu);
	return test_bit(reg, vcpu->arch.regs_dirty);
}

static inline void kvm_register_mark_for_reload(struct kvm_vcpu *vcpu,
					       enum kvm_reg reg)
{
	kvm_assert_register_caching_allowed(vcpu);
	__clear_bit(reg, vcpu->arch.regs_avail);
	__clear_bit(reg, vcpu->arch.regs_dirty);
}

static inline void kvm_register_mark_available(struct kvm_vcpu *vcpu,
					       enum kvm_reg reg)
{
	kvm_assert_register_caching_allowed(vcpu);
	__set_bit(reg, vcpu->arch.regs_avail);
}

static inline void kvm_register_mark_dirty(struct kvm_vcpu *vcpu,
					   enum kvm_reg reg)
{
	kvm_assert_register_caching_allowed(vcpu);
	__set_bit(reg, vcpu->arch.regs_avail);
	__set_bit(reg, vcpu->arch.regs_dirty);
}

/*
 * kvm_register_test_and_mark_available() is a special snowflake that uses an
 * arch bitop directly to avoid the explicit instrumentation that comes with
 * the generic bitops.  This allows code that cannot be instrumented (noinstr
 * functions), e.g. the low level VM-Enter/VM-Exit paths, to cache registers.
 */
static __always_inline bool kvm_register_test_and_mark_available(struct kvm_vcpu *vcpu,
								 enum kvm_reg reg)
{
	kvm_assert_register_caching_allowed(vcpu);
	return arch___test_and_set_bit(reg, vcpu->arch.regs_avail);
}

static __always_inline void kvm_clear_available_registers(struct kvm_vcpu *vcpu,
							  unsigned long clear_mask)
{
	BUILD_BUG_ON(sizeof(clear_mask) != sizeof(vcpu->arch.regs_avail[0]));
	BUILD_BUG_ON(ARRAY_SIZE(vcpu->arch.regs_avail) != 1);

	/*
	 * Note the bitwise-AND!  In practice, a straight write would also work
	 * as KVM initializes the mask to all ones and never clears registers
	 * that are eagerly synchronized.  Using a bitwise-AND adds a bit of
	 * sanity checking as incorrectly marking an eagerly sync'd register
	 * unavailable will generate a WARN due to an unexpected cache request.
	 */
	vcpu->arch.regs_avail[0] &= ~clear_mask;
}

static __always_inline void kvm_reset_dirty_registers(struct kvm_vcpu *vcpu)
{
	BUILD_BUG_ON(ARRAY_SIZE(vcpu->arch.regs_dirty) != 1);
	vcpu->arch.regs_dirty[0] = 0;
}

/*
 * The "raw" register helpers are only for cases where the full 64 bits of a
 * register are read/written irrespective of current vCPU mode.  In other words,
 * odds are good you shouldn't be using the raw variants.
 */
static inline unsigned long kvm_register_read_raw(struct kvm_vcpu *vcpu, int reg)
{
	if (WARN_ON_ONCE((unsigned int)reg >= NR_VCPU_GENERAL_PURPOSE_REGS))
		return 0;

	if (!kvm_register_is_available(vcpu, reg))
		kvm_x86_call(cache_reg)(vcpu, reg);

	return vcpu->arch.regs[reg];
}

static inline unsigned long kvm_register_read(struct kvm_vcpu *vcpu, int reg)
{
	return kvm_register_read_raw(vcpu, reg) & kvm_reg_mode_mask(vcpu);
}

static inline void kvm_register_write_raw(struct kvm_vcpu *vcpu, int reg,
					  unsigned long val)
{
	if (WARN_ON_ONCE((unsigned int)reg >= NR_VCPU_GENERAL_PURPOSE_REGS))
		return;

	vcpu->arch.regs[reg] = val;
	kvm_register_mark_dirty(vcpu, reg);
}

static inline void kvm_register_write(struct kvm_vcpu *vcpu,
				       int reg, unsigned long val)
{
	return kvm_register_write_raw(vcpu, reg, val & kvm_reg_mode_mask(vcpu));
}

static inline unsigned long kvm_rip_read(struct kvm_vcpu *vcpu)
{
	if (!kvm_register_is_available(vcpu, VCPU_REG_RIP))
		kvm_x86_call(cache_reg)(vcpu, VCPU_REG_RIP);

	return vcpu->arch.rip;
}

static inline void kvm_rip_write(struct kvm_vcpu *vcpu, unsigned long val)
{
	vcpu->arch.rip = val;
	kvm_register_mark_dirty(vcpu, VCPU_REG_RIP);
}

static inline unsigned long kvm_rsp_read(struct kvm_vcpu *vcpu)
{
	return kvm_register_read_raw(vcpu, VCPU_REGS_RSP);
}

static inline void kvm_rsp_write(struct kvm_vcpu *vcpu, unsigned long val)
{
	kvm_register_write_raw(vcpu, VCPU_REGS_RSP, val);
}

static inline u64 kvm_read_edx_eax(struct kvm_vcpu *vcpu)
{
	return kvm_eax_read(vcpu) | (u64)(kvm_edx_read(vcpu)) << 32;
}

static inline u64 kvm_pdptr_read(struct kvm_vcpu *vcpu, int index)
{
	might_sleep();  /* on svm */

	if (!kvm_register_is_available(vcpu, VCPU_REG_PDPTR))
		kvm_x86_call(cache_reg)(vcpu, VCPU_REG_PDPTR);

	return vcpu->arch.pdptrs[index];
}

static inline void kvm_pdptr_write(struct kvm_vcpu *vcpu, int index, u64 value)
{
	vcpu->arch.pdptrs[index] = value;
}

static inline ulong kvm_read_cr0_bits(struct kvm_vcpu *vcpu, ulong mask)
{
	ulong tmask = mask & KVM_POSSIBLE_CR0_GUEST_BITS;
	if ((tmask & vcpu->arch.cr0_guest_owned_bits) &&
	    !kvm_register_is_available(vcpu, VCPU_REG_CR0))
		kvm_x86_call(cache_reg)(vcpu, VCPU_REG_CR0);
	return vcpu->arch.cr0 & mask;
}

static __always_inline bool kvm_is_cr0_bit_set(struct kvm_vcpu *vcpu,
					       unsigned long cr0_bit)
{
	BUILD_BUG_ON(!is_power_of_2(cr0_bit));

	return !!kvm_read_cr0_bits(vcpu, cr0_bit);
}

static inline ulong kvm_read_cr0(struct kvm_vcpu *vcpu)
{
	return kvm_read_cr0_bits(vcpu, ~0UL);
}

static inline ulong kvm_read_cr4_bits(struct kvm_vcpu *vcpu, ulong mask)
{
	ulong tmask = mask & KVM_POSSIBLE_CR4_GUEST_BITS;
	if ((tmask & vcpu->arch.cr4_guest_owned_bits) &&
	    !kvm_register_is_available(vcpu, VCPU_REG_CR4))
		kvm_x86_call(cache_reg)(vcpu, VCPU_REG_CR4);
	return vcpu->arch.cr4 & mask;
}

static __always_inline bool kvm_is_cr4_bit_set(struct kvm_vcpu *vcpu,
					       unsigned long cr4_bit)
{
	BUILD_BUG_ON(!is_power_of_2(cr4_bit));

	return !!kvm_read_cr4_bits(vcpu, cr4_bit);
}

static inline ulong kvm_read_cr3(struct kvm_vcpu *vcpu)
{
	if (!kvm_register_is_available(vcpu, VCPU_REG_CR3))
		kvm_x86_call(cache_reg)(vcpu, VCPU_REG_CR3);
	return vcpu->arch.cr3;
}

static inline ulong kvm_read_cr4(struct kvm_vcpu *vcpu)
{
	return kvm_read_cr4_bits(vcpu, ~0UL);
}

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
	if (!__cpu_has(__c, X86_FEATURE_SHSTK) &&       \
	    !__cpu_has(__c, X86_FEATURE_IBT))           \
		__reserved_bits |= X86_CR4_CET;         \
	__reserved_bits;                                \
})

static inline bool is_protmode(struct kvm_vcpu *vcpu)
{
	return kvm_is_cr0_bit_set(vcpu, X86_CR0_PE);
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

static inline unsigned long kvm_get_effective_dr7(struct kvm_vcpu *vcpu)
{
	if (vcpu->guest_debug & KVM_GUESTDBG_USE_HW_BP)
		return vcpu->arch.guest_debug_dr7;

	return vcpu->arch.dr7;
}

static inline void enter_guest_mode(struct kvm_vcpu *vcpu)
{
	vcpu->arch.hflags |= HF_GUEST_MASK;
	vcpu->stat.guest_mode = 1;
}

static inline void leave_guest_mode(struct kvm_vcpu *vcpu)
{
	vcpu->arch.hflags &= ~HF_GUEST_MASK;

	if (vcpu->arch.load_eoi_exitmap_pending) {
		vcpu->arch.load_eoi_exitmap_pending = false;
		kvm_make_request(KVM_REQ_LOAD_EOI_EXITMAP, vcpu);
	}

	vcpu->stat.guest_mode = 0;
}

static inline bool is_guest_mode(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.hflags & HF_GUEST_MASK;
}

static inline unsigned long kvm_get_segment_base(struct kvm_vcpu *vcpu, int seg)
{
	return kvm_x86_call(get_segment_base)(vcpu, seg);
}

static inline void kvm_set_segment(struct kvm_vcpu *vcpu,
				   struct kvm_segment *var, int seg)
{
	kvm_x86_call(set_segment)(vcpu, var, seg);
}

static inline void kvm_get_segment(struct kvm_vcpu *vcpu,
				   struct kvm_segment *var, int seg)
{
	kvm_x86_call(get_segment)(vcpu, var, seg);
}

unsigned long kvm_get_linear_rip(struct kvm_vcpu *vcpu);
bool kvm_is_linear_rip(struct kvm_vcpu *vcpu, unsigned long linear_rip);

unsigned long kvm_get_rflags(struct kvm_vcpu *vcpu);
void __kvm_set_rflags(struct kvm_vcpu *vcpu, unsigned long rflags);
void kvm_set_rflags(struct kvm_vcpu *vcpu, unsigned long rflags);

void kvm_vcpu_ioctl_x86_get_sregs2(struct kvm_vcpu *vcpu,
				   struct kvm_sregs2 *sregs2);
int kvm_vcpu_ioctl_x86_set_sregs2(struct kvm_vcpu *vcpu,
				  struct kvm_sregs2 *sregs2);

void kvm_run_sync_regs_to_user(struct kvm_vcpu *vcpu);
int kvm_run_sync_regs_from_user(struct kvm_vcpu *vcpu);

void kvm_update_dr0123(struct kvm_vcpu *vcpu);
void kvm_update_dr7(struct kvm_vcpu *vcpu);
int kvm_vcpu_ioctl_x86_get_debugregs(struct kvm_vcpu *vcpu,
				     struct kvm_debugregs *dbgregs);
int kvm_vcpu_ioctl_x86_set_debugregs(struct kvm_vcpu *vcpu,
				     struct kvm_debugregs *dbgregs);


#endif
