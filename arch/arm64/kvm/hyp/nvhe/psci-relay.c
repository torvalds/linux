// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 - Google LLC
 * Author: David Brazdil <dbrazdil@google.com>
 */

#include <asm/kvm_asm.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_hypevents.h>
#include <asm/kvm_mmu.h>
#include <linux/kvm_host.h>
#include <uapi/linux/psci.h>

#include <nvhe/arm-smccc.h>
#include <nvhe/mem_protect.h>
#include <nvhe/memory.h>
#include <nvhe/pkvm.h>
#include <nvhe/trap_handler.h>

void kvm_hyp_cpu_entry(unsigned long r0);
void kvm_hyp_cpu_resume(unsigned long r0);

void __noreturn __host_enter(struct kvm_cpu_context *host_ctxt);

/* Config options set by the host. */
struct kvm_host_psci_config __ro_after_init kvm_host_psci_config;

static void (*pkvm_psci_notifier)(enum pkvm_psci_notification, struct kvm_cpu_context *);
static void pkvm_psci_notify(enum pkvm_psci_notification notif, struct kvm_cpu_context *host_ctxt)
{
	if (READ_ONCE(pkvm_psci_notifier))
		pkvm_psci_notifier(notif, host_ctxt);
}

#ifdef CONFIG_MODULES
int __pkvm_register_psci_notifier(void (*cb)(enum pkvm_psci_notification, struct kvm_cpu_context *))
{
	return cmpxchg(&pkvm_psci_notifier, NULL, cb) ? -EBUSY : 0;
}
#endif

#define INVALID_CPU_ID	UINT_MAX

struct psci_boot_args {
	atomic_t lock;
	unsigned long pc;
	unsigned long r0;
};

#define PSCI_BOOT_ARGS_UNLOCKED		0
#define PSCI_BOOT_ARGS_LOCKED		1

#define PSCI_BOOT_ARGS_INIT					\
	((struct psci_boot_args){				\
		.lock = ATOMIC_INIT(PSCI_BOOT_ARGS_UNLOCKED),	\
	})

static DEFINE_PER_CPU(struct psci_boot_args, cpu_on_args) = PSCI_BOOT_ARGS_INIT;
static DEFINE_PER_CPU(struct psci_boot_args, suspend_args) = PSCI_BOOT_ARGS_INIT;

#define	is_psci_0_1(what, func_id)					\
	(kvm_host_psci_config.psci_0_1_ ## what ## _implemented &&	\
	 (func_id) == kvm_host_psci_config.function_ids_0_1.what)

static bool is_psci_0_1_call(u64 func_id)
{
	return (is_psci_0_1(cpu_suspend, func_id) ||
		is_psci_0_1(cpu_on, func_id) ||
		is_psci_0_1(cpu_off, func_id) ||
		is_psci_0_1(migrate, func_id));
}

static bool is_psci_0_2_call(u64 func_id)
{
	/* SMCCC reserves IDs 0x00-1F with the given 32/64-bit base for PSCI. */
	return (PSCI_0_2_FN(0) <= func_id && func_id <= PSCI_0_2_FN(31)) ||
	       (PSCI_0_2_FN64(0) <= func_id && func_id <= PSCI_0_2_FN64(31));
}

static unsigned long psci_call(unsigned long fn, unsigned long arg0,
			       unsigned long arg1, unsigned long arg2)
{
	struct arm_smccc_res res;

	arm_smccc_1_1_smc(fn, arg0, arg1, arg2, &res);
	return res.a0;
}

static unsigned long psci_forward(struct kvm_cpu_context *host_ctxt)
{
	return psci_call(cpu_reg(host_ctxt, 0), cpu_reg(host_ctxt, 1),
			 cpu_reg(host_ctxt, 2), cpu_reg(host_ctxt, 3));
}

static unsigned int find_cpu_id(u64 mpidr)
{
	unsigned int i;

	/* Reject invalid MPIDRs */
	if (mpidr & ~MPIDR_HWID_BITMASK)
		return INVALID_CPU_ID;

	for (i = 0; i < NR_CPUS; i++) {
		if (cpu_logical_map(i) == mpidr)
			return i;
	}

	return INVALID_CPU_ID;
}

static __always_inline bool try_acquire_boot_args(struct psci_boot_args *args)
{
	return atomic_cmpxchg_acquire(&args->lock,
				      PSCI_BOOT_ARGS_UNLOCKED,
				      PSCI_BOOT_ARGS_LOCKED) ==
		PSCI_BOOT_ARGS_UNLOCKED;
}

static __always_inline void release_boot_args(struct psci_boot_args *args)
{
	atomic_set_release(&args->lock, PSCI_BOOT_ARGS_UNLOCKED);
}

static int psci_cpu_on(u64 func_id, struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(u64, mpidr, host_ctxt, 1);
	DECLARE_REG(unsigned long, pc, host_ctxt, 2);
	DECLARE_REG(unsigned long, r0, host_ctxt, 3);

	unsigned int cpu_id;
	struct psci_boot_args *boot_args;
	struct kvm_nvhe_init_params *init_params;
	int ret;

	/*
	 * Find the logical CPU ID for the given MPIDR. The search set is
	 * the set of CPUs that were online at the point of KVM initialization.
	 * Booting other CPUs is rejected because their cpufeatures were not
	 * checked against the finalized capabilities. This could be relaxed
	 * by doing the feature checks in hyp.
	 */
	cpu_id = find_cpu_id(mpidr);
	if (cpu_id == INVALID_CPU_ID)
		return PSCI_RET_INVALID_PARAMS;

	boot_args = per_cpu_ptr(&cpu_on_args, cpu_id);
	init_params = per_cpu_ptr(&kvm_init_params, cpu_id);

	/* Check if the target CPU is already being booted. */
	if (!try_acquire_boot_args(boot_args))
		return PSCI_RET_ALREADY_ON;

	boot_args->pc = pc;
	boot_args->r0 = r0;
	wmb();

	ret = psci_call(func_id, mpidr,
			__hyp_pa(&kvm_hyp_cpu_entry),
			__hyp_pa(init_params));

	/* If successful, the lock will be released by the target CPU. */
	if (ret != PSCI_RET_SUCCESS)
		release_boot_args(boot_args);

	return ret;
}

static int psci_cpu_suspend(u64 func_id, struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(u64, power_state, host_ctxt, 1);
	DECLARE_REG(unsigned long, pc, host_ctxt, 2);
	DECLARE_REG(unsigned long, r0, host_ctxt, 3);
	int ret;

	struct psci_boot_args *boot_args;
	struct kvm_nvhe_init_params *init_params;

	boot_args = this_cpu_ptr(&suspend_args);
	init_params = this_cpu_ptr(&kvm_init_params);

	/*
	 * No need to acquire a lock before writing to boot_args because a core
	 * can only suspend itself. Racy CPU_ON calls use a separate struct.
	 */
	boot_args->pc = pc;
	boot_args->r0 = r0;

	pkvm_psci_notify(PKVM_PSCI_CPU_SUSPEND, host_ctxt);
	/*
	 * Will either return if shallow sleep state, or wake up into the entry
	 * point if it is a deep sleep state.
	 */
	ret = psci_call(func_id, power_state,
			__hyp_pa(&kvm_hyp_cpu_resume),
			__hyp_pa(init_params));

	return ret;
}

static int psci_system_suspend(u64 func_id, struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(unsigned long, pc, host_ctxt, 1);
	DECLARE_REG(unsigned long, r0, host_ctxt, 2);

	struct psci_boot_args *boot_args;
	struct kvm_nvhe_init_params *init_params;

	boot_args = this_cpu_ptr(&suspend_args);
	init_params = this_cpu_ptr(&kvm_init_params);

	/*
	 * No need to acquire a lock before writing to boot_args because a core
	 * can only suspend itself. Racy CPU_ON calls use a separate struct.
	 */
	boot_args->pc = pc;
	boot_args->r0 = r0;

	pkvm_psci_notify(PKVM_PSCI_SYSTEM_SUSPEND, host_ctxt);

	/* Will only return on error. */
	return psci_call(func_id,
			 __hyp_pa(&kvm_hyp_cpu_resume),
			 __hyp_pa(init_params), 0);
}

asmlinkage void __noreturn kvm_host_psci_cpu_entry(bool is_cpu_on)
{
	struct psci_boot_args *boot_args;
	struct kvm_cpu_context *host_ctxt;

	trace_hyp_enter();

	host_ctxt = &this_cpu_ptr(&kvm_host_data)->host_ctxt;

	if (is_cpu_on)
		boot_args = this_cpu_ptr(&cpu_on_args);
	else
		boot_args = this_cpu_ptr(&suspend_args);

	cpu_reg(host_ctxt, 0) = boot_args->r0;
	write_sysreg_el2(boot_args->pc, SYS_ELR);

	if (is_cpu_on)
		release_boot_args(boot_args);

	pkvm_psci_notify(PKVM_PSCI_CPU_ENTRY, host_ctxt);

	trace_hyp_exit();
	__host_enter(host_ctxt);
}

static DEFINE_HYP_SPINLOCK(mem_protect_lock);

static u64 psci_mem_protect(s64 offset)
{
	static u64 cnt;
	u64 new = cnt + offset;

	hyp_assert_lock_held(&mem_protect_lock);

	if (!offset || kvm_host_psci_config.version < PSCI_VERSION(1, 1))
		return cnt;

	if (!cnt || !new)
		psci_call(PSCI_1_1_FN_MEM_PROTECT, offset < 0 ? 0 : 1, 0, 0);

	cnt = new;
	return cnt;
}

static bool psci_mem_protect_active(void)
{
	return psci_mem_protect(0);
}

void psci_mem_protect_inc(u64 n)
{
	hyp_spin_lock(&mem_protect_lock);
	psci_mem_protect(n);
	hyp_spin_unlock(&mem_protect_lock);
}

void psci_mem_protect_dec(u64 n)
{
	hyp_spin_lock(&mem_protect_lock);
	psci_mem_protect(-n);
	hyp_spin_unlock(&mem_protect_lock);
}

static unsigned long psci_0_1_handler(u64 func_id, struct kvm_cpu_context *host_ctxt)
{
	if (is_psci_0_1(cpu_off, func_id) || is_psci_0_1(migrate, func_id))
		return psci_forward(host_ctxt);
	if (is_psci_0_1(cpu_on, func_id))
		return psci_cpu_on(func_id, host_ctxt);
	if (is_psci_0_1(cpu_suspend, func_id))
		return psci_cpu_suspend(func_id, host_ctxt);

	return PSCI_RET_NOT_SUPPORTED;
}

static unsigned long psci_0_2_handler(u64 func_id, struct kvm_cpu_context *host_ctxt)
{
	switch (func_id) {
	case PSCI_0_2_FN_PSCI_VERSION:
	case PSCI_0_2_FN_CPU_OFF:
	case PSCI_0_2_FN64_AFFINITY_INFO:
	case PSCI_0_2_FN64_MIGRATE:
	case PSCI_0_2_FN_MIGRATE_INFO_TYPE:
	case PSCI_0_2_FN64_MIGRATE_INFO_UP_CPU:
		return psci_forward(host_ctxt);
	/*
	 * SYSTEM_OFF/RESET should not return according to the spec.
	 * Allow it so as to stay robust to broken firmware.
	 */
	case PSCI_0_2_FN_SYSTEM_OFF:
	case PSCI_0_2_FN_SYSTEM_RESET:
		pkvm_poison_pvmfw_pages();
		/* Avoid racing with a MEM_PROTECT call. */
		hyp_spin_lock(&mem_protect_lock);
		return psci_forward(host_ctxt);
	case PSCI_0_2_FN64_CPU_SUSPEND:
		return psci_cpu_suspend(func_id, host_ctxt);
	case PSCI_0_2_FN64_CPU_ON:
		return psci_cpu_on(func_id, host_ctxt);
	default:
		return PSCI_RET_NOT_SUPPORTED;
	}
}

static unsigned long psci_1_0_handler(u64 func_id, struct kvm_cpu_context *host_ctxt)
{
	switch (func_id) {
	case PSCI_1_1_FN64_SYSTEM_RESET2:
		pkvm_poison_pvmfw_pages();
		hyp_spin_lock(&mem_protect_lock);
		if (psci_mem_protect_active())
			cpu_reg(host_ctxt, 0) = PSCI_0_2_FN_SYSTEM_RESET;
		fallthrough;
	case PSCI_1_0_FN_PSCI_FEATURES:
	case PSCI_1_0_FN_SET_SUSPEND_MODE:
		return psci_forward(host_ctxt);
	case PSCI_1_0_FN64_SYSTEM_SUSPEND:
		return psci_system_suspend(func_id, host_ctxt);
	default:
		return psci_0_2_handler(func_id, host_ctxt);
	}
}

bool kvm_host_psci_handler(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(u64, func_id, host_ctxt, 0);
	unsigned long ret;

	switch (kvm_host_psci_config.version) {
	case PSCI_VERSION(0, 1):
		if (!is_psci_0_1_call(func_id))
			return false;
		ret = psci_0_1_handler(func_id, host_ctxt);
		break;
	case PSCI_VERSION(0, 2):
		if (!is_psci_0_2_call(func_id))
			return false;
		ret = psci_0_2_handler(func_id, host_ctxt);
		break;
	default:
		if (!is_psci_0_2_call(func_id))
			return false;
		ret = psci_1_0_handler(func_id, host_ctxt);
		break;
	}

	cpu_reg(host_ctxt, 0) = ret;
	cpu_reg(host_ctxt, 1) = 0;
	cpu_reg(host_ctxt, 2) = 0;
	cpu_reg(host_ctxt, 3) = 0;
	return true;
}
