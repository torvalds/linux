/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ASM_KVM_HOST_ARM64_H
#define ASM_KVM_HOST_ARM64_H

#include <linux/bug.h>

#include <asm/kvm_host_types.h>
#include <asm/debug.h>

#define vcpu_gp_regs(v)		((v)->arch.sae_block.gpr)

#include <arm64/kvm_host.h>
#include <arm64/ptrace.h>

#include <asm/sae.h>

#define KVM_HAVE_MMU_RWLOCK
#define KVM_MAX_VCPUS 1
#define __KVM_HAVE_VCPU_RUN_PID_CHANGE

#define KVM_S390_ARM64_IMPL_FEATURES 0
#define KVM_HALT_POLL_NS_DEFAULT 50000

#define KVM_S390_MANAGES_S390_GUEST 0

/* Minimal (=no) vgic definitions */
#define KVM_IRQCHIP_NUM_PINS 1
#define irqchip_in_kernel(_k) false

#define __ctxt_sys_reg(ctx, reg) NULL
struct kvm_cpu_context {
	/*
	 * These are just for 32 bit, which we don't have, making them RES0.
	 * They are exposed to user space.
	 */
	u64	spsr_abt;
	u64	spsr_und;
	u64	spsr_irq;
	u64	spsr_fiq;

	__vector128 __aligned(16) vregs[32];
};

struct kvm_vcpu_arch {
	struct kvm_sae_block sae_block;
	struct kvm_sae_save_area save_area;
	struct kvm_cpu_context ctxt;

	u32 host_acrs[NUM_ACRS];

	/* Hypervisor Configuration Register */
	u64 hcr_el2;

	/* Configuration flags, set once and for all before the vcpu can run */
	u8 cflags;

	/* Input flags to the hypervisor code, potentially cleared after use */
	u8 iflags;

	/* State flags for kernel bookkeeping, unused by the hypervisor code */
	u8 sflags;

	/*
	 * Don't run the guest (internal implementation need).
	 *
	 * Contrary to the flags above, this is set/cleared outside of
	 * a vcpu context, and thus cannot be mixed with the flags
	 * themselves (or the flag accesses need to be made atomic).
	 */
	bool pause;

	/* vcpu power state */
	struct kvm_mp_state mp_state;
	/* lock for mp_state & reset_state.reset */
	spinlock_t mp_state_lock;

	/* vcpu reset state */
	struct vcpu_reset_state reset_state;

	struct kvm_s390_mmu_cache *mc;

	void *debugfs_state_data;
};

struct kvm_vcpu_stat {
	struct kvm_vcpu_stat_generic generic;
	/* ARM64 stats */
	u64 hvc_exit_stat;
	u64 wfe_exit_stat;
	u64 wfi_exit_stat;
	u64 mmio_exit_user;
	u64 mmio_exit_kernel;
	u64 signal_exits;
	u64 exits;
	/* GMAP stats */
	u64 pfault_sync;
};

#define kvm_has_mte(_kvm) false
#define vcpu_has_sve(_vcpu) false
#define vcpu_has_ptrauth(_vcpu) false

struct kvm_arch_memory_slot {
};

struct kvm_arch {
	struct gmap *gmap;
	u64 guest_phys_size;

	/* VM-wide vCPU feature set */
	unsigned long flags;

	/* Protects VM-scoped configuration data */
	struct mutex config_lock;

	debug_info_t *dbf;

	DECLARE_BITMAP(vcpu_features, KVM_VCPU_MAX_FEATURES);

	unsigned long mem_limit;
};

static inline bool __vcpu_has_feature(const struct kvm_arch *ka, int feature)
{
	return test_bit(feature, ka->vcpu_features);
}

struct kvm_vm_stat {
	struct kvm_vm_stat_generic generic;
};

static inline bool system_has_full_ptr_auth(void)
{
	return true;
}

#define kvm_vm_is_protected(_kvm) false
#define vcpu_is_protected(_vcpu) false

#define vcpu_is_loaded(_vcpu) ((_vcpu)->cpu != -1)

#define KVM_HVA_ERR_BAD		(-1UL)
#define KVM_HVA_ERR_RO_BAD	(-2UL)

static inline bool kvm_is_error_hva(unsigned long addr)
{
	return IS_ERR_VALUE(addr);
}

u32 get_kvm_ipa_limit(void);

/* unused, but required functions */
static inline void kvm_arch_free_memslot(struct kvm *kvm, struct kvm_memory_slot *slot) {}
static inline void kvm_arch_memslots_updated(struct kvm *kvm, u64 gen) {}
static inline void kvm_arch_flush_shadow_all(struct kvm *kvm) {}
static inline void kvm_arch_flush_shadow_memslot(struct kvm *kvm, struct kvm_memory_slot *slot) {}
static inline void kvm_arch_vcpu_blocking(struct kvm_vcpu *vcpu) {}
static inline void kvm_arch_vcpu_unblocking(struct kvm_vcpu *vcpu) {}
static inline void kvm_arch_sync_events(struct kvm *kvm) {}
static inline void kvm_arch_sched_in(struct kvm_vcpu *vcpu, int cpu) {}
static inline void kvm_arch_mmu_enable_log_dirty_pt_masked(struct kvm *kvm,
							   struct kvm_memory_slot *slot,
							   gfn_t gfn_offset,
							   unsigned long mask) {}

int kvm_handle_guest_abort(struct kvm_vcpu *vcpu);
void kvm_reset_vcpu(struct kvm_vcpu *vcpu);

/* arm64 guests do not use async-pf. Defined because Kbuild requires it as s390 kvm turns it on. */
#define ASYNC_PF_PER_VCPU 0
struct kvm_arch_async_pf {
	unsigned long pfault_token;
};

#define __unsupp_async_call(fn) WARN_ONCE(true, "async not supported on kvm-arm64 %s", fn)

static inline bool kvm_arch_can_dequeue_async_page_present(struct kvm_vcpu *vcpu)
{
	__unsupp_async_call(__func__);
	return false;
};

static inline void kvm_arch_async_page_ready(struct kvm_vcpu *vcpu,
					     struct kvm_async_pf *work)
{
	__unsupp_async_call(__func__);
};

static inline bool kvm_arch_async_page_not_present(struct kvm_vcpu *vcpu,
						   struct kvm_async_pf *work)
{
	__unsupp_async_call(__func__);
	return false;
};

static inline void kvm_arch_async_page_present(struct kvm_vcpu *vcpu,
					       struct kvm_async_pf *work)
{
	__unsupp_async_call(__func__);
};

static inline void kvm_arch_async_page_present_queued(struct kvm_vcpu *vcpu)
{
	__unsupp_async_call(__func__);
};

#define kvm_supports_32bit_el0() false

#endif /* ASM_KVM_HOST_ARM64_H */
