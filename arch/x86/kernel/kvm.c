// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * KVM paravirt_ops implementation
 *
 * Copyright (C) 2007, Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 * Copyright IBM Corporation, 2007
 *   Authors: Anthony Liguori <aliguori@us.ibm.com>
 */

#define pr_fmt(fmt) "kvm-guest: " fmt

#include <linux/context_tracking.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/kvm_para.h>
#include <linux/cpu.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/hardirq.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/hash.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/kprobes.h>
#include <linux/nmi.h>
#include <linux/swait.h>
#include <linux/syscore_ops.h>
#include <linux/cc_platform.h>
#include <linux/efi.h>
#include <asm/timer.h>
#include <asm/cpu.h>
#include <asm/traps.h>
#include <asm/desc.h>
#include <asm/tlbflush.h>
#include <asm/apic.h>
#include <asm/apicdef.h>
#include <asm/hypervisor.h>
#include <asm/tlb.h>
#include <asm/cpuidle_haltpoll.h>
#include <asm/ptrace.h>
#include <asm/reboot.h>
#include <asm/svm.h>
#include <asm/e820/api.h>

DEFINE_STATIC_KEY_FALSE(kvm_async_pf_enabled);

static int kvmapf = 1;

static int __init parse_no_kvmapf(char *arg)
{
        kvmapf = 0;
        return 0;
}

early_param("no-kvmapf", parse_no_kvmapf);

static int steal_acc = 1;
static int __init parse_no_stealacc(char *arg)
{
        steal_acc = 0;
        return 0;
}

early_param("no-steal-acc", parse_no_stealacc);

static DEFINE_PER_CPU_DECRYPTED(struct kvm_vcpu_pv_apf_data, apf_reason) __aligned(64);
DEFINE_PER_CPU_DECRYPTED(struct kvm_steal_time, steal_time) __aligned(64) __visible;
static int has_steal_clock = 0;

static int has_guest_poll = 0;
/*
 * No need for any "IO delay" on KVM
 */
static void kvm_io_delay(void)
{
}

#define KVM_TASK_SLEEP_HASHBITS 8
#define KVM_TASK_SLEEP_HASHSIZE (1<<KVM_TASK_SLEEP_HASHBITS)

struct kvm_task_sleep_node {
	struct hlist_node link;
	struct swait_queue_head wq;
	u32 token;
	int cpu;
};

static struct kvm_task_sleep_head {
	raw_spinlock_t lock;
	struct hlist_head list;
} async_pf_sleepers[KVM_TASK_SLEEP_HASHSIZE];

static struct kvm_task_sleep_node *_find_apf_task(struct kvm_task_sleep_head *b,
						  u32 token)
{
	struct hlist_node *p;

	hlist_for_each(p, &b->list) {
		struct kvm_task_sleep_node *n =
			hlist_entry(p, typeof(*n), link);
		if (n->token == token)
			return n;
	}

	return NULL;
}

static bool kvm_async_pf_queue_task(u32 token, struct kvm_task_sleep_node *n)
{
	u32 key = hash_32(token, KVM_TASK_SLEEP_HASHBITS);
	struct kvm_task_sleep_head *b = &async_pf_sleepers[key];
	struct kvm_task_sleep_node *e;

	raw_spin_lock(&b->lock);
	e = _find_apf_task(b, token);
	if (e) {
		/* dummy entry exist -> wake up was delivered ahead of PF */
		hlist_del(&e->link);
		raw_spin_unlock(&b->lock);
		kfree(e);
		return false;
	}

	n->token = token;
	n->cpu = smp_processor_id();
	init_swait_queue_head(&n->wq);
	hlist_add_head(&n->link, &b->list);
	raw_spin_unlock(&b->lock);
	return true;
}

/*
 * kvm_async_pf_task_wait_schedule - Wait for pagefault to be handled
 * @token:	Token to identify the sleep node entry
 *
 * Invoked from the async pagefault handling code or from the VM exit page
 * fault handler. In both cases RCU is watching.
 */
void kvm_async_pf_task_wait_schedule(u32 token)
{
	struct kvm_task_sleep_node n;
	DECLARE_SWAITQUEUE(wait);

	lockdep_assert_irqs_disabled();

	if (!kvm_async_pf_queue_task(token, &n))
		return;

	for (;;) {
		prepare_to_swait_exclusive(&n.wq, &wait, TASK_UNINTERRUPTIBLE);
		if (hlist_unhashed(&n.link))
			break;

		local_irq_enable();
		schedule();
		local_irq_disable();
	}
	finish_swait(&n.wq, &wait);
}
EXPORT_SYMBOL_GPL(kvm_async_pf_task_wait_schedule);

static void apf_task_wake_one(struct kvm_task_sleep_node *n)
{
	hlist_del_init(&n->link);
	if (swq_has_sleeper(&n->wq))
		swake_up_one(&n->wq);
}

static void apf_task_wake_all(void)
{
	int i;

	for (i = 0; i < KVM_TASK_SLEEP_HASHSIZE; i++) {
		struct kvm_task_sleep_head *b = &async_pf_sleepers[i];
		struct kvm_task_sleep_node *n;
		struct hlist_node *p, *next;

		raw_spin_lock(&b->lock);
		hlist_for_each_safe(p, next, &b->list) {
			n = hlist_entry(p, typeof(*n), link);
			if (n->cpu == smp_processor_id())
				apf_task_wake_one(n);
		}
		raw_spin_unlock(&b->lock);
	}
}

void kvm_async_pf_task_wake(u32 token)
{
	u32 key = hash_32(token, KVM_TASK_SLEEP_HASHBITS);
	struct kvm_task_sleep_head *b = &async_pf_sleepers[key];
	struct kvm_task_sleep_node *n, *dummy = NULL;

	if (token == ~0) {
		apf_task_wake_all();
		return;
	}

again:
	raw_spin_lock(&b->lock);
	n = _find_apf_task(b, token);
	if (!n) {
		/*
		 * Async #PF not yet handled, add a dummy entry for the token.
		 * Allocating the token must be down outside of the raw lock
		 * as the allocator is preemptible on PREEMPT_RT kernels.
		 */
		if (!dummy) {
			raw_spin_unlock(&b->lock);
			dummy = kzalloc(sizeof(*dummy), GFP_ATOMIC);

			/*
			 * Continue looping on allocation failure, eventually
			 * the async #PF will be handled and allocating a new
			 * node will be unnecessary.
			 */
			if (!dummy)
				cpu_relax();

			/*
			 * Recheck for async #PF completion before enqueueing
			 * the dummy token to avoid duplicate list entries.
			 */
			goto again;
		}
		dummy->token = token;
		dummy->cpu = smp_processor_id();
		init_swait_queue_head(&dummy->wq);
		hlist_add_head(&dummy->link, &b->list);
		dummy = NULL;
	} else {
		apf_task_wake_one(n);
	}
	raw_spin_unlock(&b->lock);

	/* A dummy token might be allocated and ultimately not used.  */
	kfree(dummy);
}
EXPORT_SYMBOL_GPL(kvm_async_pf_task_wake);

noinstr u32 kvm_read_and_reset_apf_flags(void)
{
	u32 flags = 0;

	if (__this_cpu_read(apf_reason.enabled)) {
		flags = __this_cpu_read(apf_reason.flags);
		__this_cpu_write(apf_reason.flags, 0);
	}

	return flags;
}
EXPORT_SYMBOL_GPL(kvm_read_and_reset_apf_flags);

noinstr bool __kvm_handle_async_pf(struct pt_regs *regs, u32 token)
{
	u32 flags = kvm_read_and_reset_apf_flags();
	irqentry_state_t state;

	if (!flags)
		return false;

	state = irqentry_enter(regs);
	instrumentation_begin();

	/*
	 * If the host managed to inject an async #PF into an interrupt
	 * disabled region, then die hard as this is not going to end well
	 * and the host side is seriously broken.
	 */
	if (unlikely(!(regs->flags & X86_EFLAGS_IF)))
		panic("Host injected async #PF in interrupt disabled region\n");

	if (flags & KVM_PV_REASON_PAGE_NOT_PRESENT) {
		if (unlikely(!(user_mode(regs))))
			panic("Host injected async #PF in kernel mode\n");
		/* Page is swapped out by the host. */
		kvm_async_pf_task_wait_schedule(token);
	} else {
		WARN_ONCE(1, "Unexpected async PF flags: %x\n", flags);
	}

	instrumentation_end();
	irqentry_exit(regs, state);
	return true;
}

DEFINE_IDTENTRY_SYSVEC(sysvec_kvm_asyncpf_interrupt)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	u32 token;

	ack_APIC_irq();

	inc_irq_stat(irq_hv_callback_count);

	if (__this_cpu_read(apf_reason.enabled)) {
		token = __this_cpu_read(apf_reason.token);
		kvm_async_pf_task_wake(token);
		__this_cpu_write(apf_reason.token, 0);
		wrmsrl(MSR_KVM_ASYNC_PF_ACK, 1);
	}

	set_irq_regs(old_regs);
}

static void __init paravirt_ops_setup(void)
{
	pv_info.name = "KVM";

	if (kvm_para_has_feature(KVM_FEATURE_NOP_IO_DELAY))
		pv_ops.cpu.io_delay = kvm_io_delay;

#ifdef CONFIG_X86_IO_APIC
	no_timer_check = 1;
#endif
}

static void kvm_register_steal_time(void)
{
	int cpu = smp_processor_id();
	struct kvm_steal_time *st = &per_cpu(steal_time, cpu);

	if (!has_steal_clock)
		return;

	wrmsrl(MSR_KVM_STEAL_TIME, (slow_virt_to_phys(st) | KVM_MSR_ENABLED));
	pr_debug("stealtime: cpu %d, msr %llx\n", cpu,
		(unsigned long long) slow_virt_to_phys(st));
}

static DEFINE_PER_CPU_DECRYPTED(unsigned long, kvm_apic_eoi) = KVM_PV_EOI_DISABLED;

static notrace void kvm_guest_apic_eoi_write(u32 reg, u32 val)
{
	/**
	 * This relies on __test_and_clear_bit to modify the memory
	 * in a way that is atomic with respect to the local CPU.
	 * The hypervisor only accesses this memory from the local CPU so
	 * there's no need for lock or memory barriers.
	 * An optimization barrier is implied in apic write.
	 */
	if (__test_and_clear_bit(KVM_PV_EOI_BIT, this_cpu_ptr(&kvm_apic_eoi)))
		return;
	apic->native_eoi_write(APIC_EOI, APIC_EOI_ACK);
}

static void kvm_guest_cpu_init(void)
{
	if (kvm_para_has_feature(KVM_FEATURE_ASYNC_PF_INT) && kvmapf) {
		u64 pa = slow_virt_to_phys(this_cpu_ptr(&apf_reason));

		WARN_ON_ONCE(!static_branch_likely(&kvm_async_pf_enabled));

		pa = slow_virt_to_phys(this_cpu_ptr(&apf_reason));
		pa |= KVM_ASYNC_PF_ENABLED | KVM_ASYNC_PF_DELIVERY_AS_INT;

		if (kvm_para_has_feature(KVM_FEATURE_ASYNC_PF_VMEXIT))
			pa |= KVM_ASYNC_PF_DELIVERY_AS_PF_VMEXIT;

		wrmsrl(MSR_KVM_ASYNC_PF_INT, HYPERVISOR_CALLBACK_VECTOR);

		wrmsrl(MSR_KVM_ASYNC_PF_EN, pa);
		__this_cpu_write(apf_reason.enabled, 1);
		pr_debug("setup async PF for cpu %d\n", smp_processor_id());
	}

	if (kvm_para_has_feature(KVM_FEATURE_PV_EOI)) {
		unsigned long pa;

		/* Size alignment is implied but just to make it explicit. */
		BUILD_BUG_ON(__alignof__(kvm_apic_eoi) < 4);
		__this_cpu_write(kvm_apic_eoi, 0);
		pa = slow_virt_to_phys(this_cpu_ptr(&kvm_apic_eoi))
			| KVM_MSR_ENABLED;
		wrmsrl(MSR_KVM_PV_EOI_EN, pa);
	}

	if (has_steal_clock)
		kvm_register_steal_time();
}

static void kvm_pv_disable_apf(void)
{
	if (!__this_cpu_read(apf_reason.enabled))
		return;

	wrmsrl(MSR_KVM_ASYNC_PF_EN, 0);
	__this_cpu_write(apf_reason.enabled, 0);

	pr_debug("disable async PF for cpu %d\n", smp_processor_id());
}

static void kvm_disable_steal_time(void)
{
	if (!has_steal_clock)
		return;

	wrmsr(MSR_KVM_STEAL_TIME, 0, 0);
}

static u64 kvm_steal_clock(int cpu)
{
	u64 steal;
	struct kvm_steal_time *src;
	int version;

	src = &per_cpu(steal_time, cpu);
	do {
		version = src->version;
		virt_rmb();
		steal = src->steal;
		virt_rmb();
	} while ((version & 1) || (version != src->version));

	return steal;
}

static inline void __set_percpu_decrypted(void *ptr, unsigned long size)
{
	early_set_memory_decrypted((unsigned long) ptr, size);
}

/*
 * Iterate through all possible CPUs and map the memory region pointed
 * by apf_reason, steal_time and kvm_apic_eoi as decrypted at once.
 *
 * Note: we iterate through all possible CPUs to ensure that CPUs
 * hotplugged will have their per-cpu variable already mapped as
 * decrypted.
 */
static void __init sev_map_percpu_data(void)
{
	int cpu;

	if (!cc_platform_has(CC_ATTR_GUEST_MEM_ENCRYPT))
		return;

	for_each_possible_cpu(cpu) {
		__set_percpu_decrypted(&per_cpu(apf_reason, cpu), sizeof(apf_reason));
		__set_percpu_decrypted(&per_cpu(steal_time, cpu), sizeof(steal_time));
		__set_percpu_decrypted(&per_cpu(kvm_apic_eoi, cpu), sizeof(kvm_apic_eoi));
	}
}

static void kvm_guest_cpu_offline(bool shutdown)
{
	kvm_disable_steal_time();
	if (kvm_para_has_feature(KVM_FEATURE_PV_EOI))
		wrmsrl(MSR_KVM_PV_EOI_EN, 0);
	if (kvm_para_has_feature(KVM_FEATURE_MIGRATION_CONTROL))
		wrmsrl(MSR_KVM_MIGRATION_CONTROL, 0);
	kvm_pv_disable_apf();
	if (!shutdown)
		apf_task_wake_all();
	kvmclock_disable();
}

static int kvm_cpu_online(unsigned int cpu)
{
	unsigned long flags;

	local_irq_save(flags);
	kvm_guest_cpu_init();
	local_irq_restore(flags);
	return 0;
}

#ifdef CONFIG_SMP

static DEFINE_PER_CPU(cpumask_var_t, __pv_cpu_mask);

static bool pv_tlb_flush_supported(void)
{
	return (kvm_para_has_feature(KVM_FEATURE_PV_TLB_FLUSH) &&
		!kvm_para_has_hint(KVM_HINTS_REALTIME) &&
		kvm_para_has_feature(KVM_FEATURE_STEAL_TIME) &&
		!boot_cpu_has(X86_FEATURE_MWAIT) &&
		(num_possible_cpus() != 1));
}

static bool pv_ipi_supported(void)
{
	return (kvm_para_has_feature(KVM_FEATURE_PV_SEND_IPI) &&
	       (num_possible_cpus() != 1));
}

static bool pv_sched_yield_supported(void)
{
	return (kvm_para_has_feature(KVM_FEATURE_PV_SCHED_YIELD) &&
		!kvm_para_has_hint(KVM_HINTS_REALTIME) &&
	    kvm_para_has_feature(KVM_FEATURE_STEAL_TIME) &&
	    !boot_cpu_has(X86_FEATURE_MWAIT) &&
	    (num_possible_cpus() != 1));
}

#define KVM_IPI_CLUSTER_SIZE	(2 * BITS_PER_LONG)

static void __send_ipi_mask(const struct cpumask *mask, int vector)
{
	unsigned long flags;
	int cpu, apic_id, icr;
	int min = 0, max = 0;
#ifdef CONFIG_X86_64
	__uint128_t ipi_bitmap = 0;
#else
	u64 ipi_bitmap = 0;
#endif
	long ret;

	if (cpumask_empty(mask))
		return;

	local_irq_save(flags);

	switch (vector) {
	default:
		icr = APIC_DM_FIXED | vector;
		break;
	case NMI_VECTOR:
		icr = APIC_DM_NMI;
		break;
	}

	for_each_cpu(cpu, mask) {
		apic_id = per_cpu(x86_cpu_to_apicid, cpu);
		if (!ipi_bitmap) {
			min = max = apic_id;
		} else if (apic_id < min && max - apic_id < KVM_IPI_CLUSTER_SIZE) {
			ipi_bitmap <<= min - apic_id;
			min = apic_id;
		} else if (apic_id > min && apic_id < min + KVM_IPI_CLUSTER_SIZE) {
			max = apic_id < max ? max : apic_id;
		} else {
			ret = kvm_hypercall4(KVM_HC_SEND_IPI, (unsigned long)ipi_bitmap,
				(unsigned long)(ipi_bitmap >> BITS_PER_LONG), min, icr);
			WARN_ONCE(ret < 0, "kvm-guest: failed to send PV IPI: %ld",
				  ret);
			min = max = apic_id;
			ipi_bitmap = 0;
		}
		__set_bit(apic_id - min, (unsigned long *)&ipi_bitmap);
	}

	if (ipi_bitmap) {
		ret = kvm_hypercall4(KVM_HC_SEND_IPI, (unsigned long)ipi_bitmap,
			(unsigned long)(ipi_bitmap >> BITS_PER_LONG), min, icr);
		WARN_ONCE(ret < 0, "kvm-guest: failed to send PV IPI: %ld",
			  ret);
	}

	local_irq_restore(flags);
}

static void kvm_send_ipi_mask(const struct cpumask *mask, int vector)
{
	__send_ipi_mask(mask, vector);
}

static void kvm_send_ipi_mask_allbutself(const struct cpumask *mask, int vector)
{
	unsigned int this_cpu = smp_processor_id();
	struct cpumask *new_mask = this_cpu_cpumask_var_ptr(__pv_cpu_mask);
	const struct cpumask *local_mask;

	cpumask_copy(new_mask, mask);
	cpumask_clear_cpu(this_cpu, new_mask);
	local_mask = new_mask;
	__send_ipi_mask(local_mask, vector);
}

static int __init setup_efi_kvm_sev_migration(void)
{
	efi_char16_t efi_sev_live_migration_enabled[] = L"SevLiveMigrationEnabled";
	efi_guid_t efi_variable_guid = AMD_SEV_MEM_ENCRYPT_GUID;
	efi_status_t status;
	unsigned long size;
	bool enabled;

	if (!cc_platform_has(CC_ATTR_GUEST_MEM_ENCRYPT) ||
	    !kvm_para_has_feature(KVM_FEATURE_MIGRATION_CONTROL))
		return 0;

	if (!efi_enabled(EFI_BOOT))
		return 0;

	if (!efi_enabled(EFI_RUNTIME_SERVICES)) {
		pr_info("%s : EFI runtime services are not enabled\n", __func__);
		return 0;
	}

	size = sizeof(enabled);

	/* Get variable contents into buffer */
	status = efi.get_variable(efi_sev_live_migration_enabled,
				  &efi_variable_guid, NULL, &size, &enabled);

	if (status == EFI_NOT_FOUND) {
		pr_info("%s : EFI live migration variable not found\n", __func__);
		return 0;
	}

	if (status != EFI_SUCCESS) {
		pr_info("%s : EFI variable retrieval failed\n", __func__);
		return 0;
	}

	if (enabled == 0) {
		pr_info("%s: live migration disabled in EFI\n", __func__);
		return 0;
	}

	pr_info("%s : live migration enabled in EFI\n", __func__);
	wrmsrl(MSR_KVM_MIGRATION_CONTROL, KVM_MIGRATION_READY);

	return 1;
}

late_initcall(setup_efi_kvm_sev_migration);

/*
 * Set the IPI entry points
 */
static void kvm_setup_pv_ipi(void)
{
	apic->send_IPI_mask = kvm_send_ipi_mask;
	apic->send_IPI_mask_allbutself = kvm_send_ipi_mask_allbutself;
	pr_info("setup PV IPIs\n");
}

static void kvm_smp_send_call_func_ipi(const struct cpumask *mask)
{
	int cpu;

	native_send_call_func_ipi(mask);

	/* Make sure other vCPUs get a chance to run if they need to. */
	for_each_cpu(cpu, mask) {
		if (!idle_cpu(cpu) && vcpu_is_preempted(cpu)) {
			kvm_hypercall1(KVM_HC_SCHED_YIELD, per_cpu(x86_cpu_to_apicid, cpu));
			break;
		}
	}
}

static void kvm_flush_tlb_multi(const struct cpumask *cpumask,
			const struct flush_tlb_info *info)
{
	u8 state;
	int cpu;
	struct kvm_steal_time *src;
	struct cpumask *flushmask = this_cpu_cpumask_var_ptr(__pv_cpu_mask);

	cpumask_copy(flushmask, cpumask);
	/*
	 * We have to call flush only on online vCPUs. And
	 * queue flush_on_enter for pre-empted vCPUs
	 */
	for_each_cpu(cpu, flushmask) {
		/*
		 * The local vCPU is never preempted, so we do not explicitly
		 * skip check for local vCPU - it will never be cleared from
		 * flushmask.
		 */
		src = &per_cpu(steal_time, cpu);
		state = READ_ONCE(src->preempted);
		if ((state & KVM_VCPU_PREEMPTED)) {
			if (try_cmpxchg(&src->preempted, &state,
					state | KVM_VCPU_FLUSH_TLB))
				__cpumask_clear_cpu(cpu, flushmask);
		}
	}

	native_flush_tlb_multi(flushmask, info);
}

static __init int kvm_alloc_cpumask(void)
{
	int cpu;

	if (!kvm_para_available() || nopv)
		return 0;

	if (pv_tlb_flush_supported() || pv_ipi_supported())
		for_each_possible_cpu(cpu) {
			zalloc_cpumask_var_node(per_cpu_ptr(&__pv_cpu_mask, cpu),
				GFP_KERNEL, cpu_to_node(cpu));
		}

	return 0;
}
arch_initcall(kvm_alloc_cpumask);

static void __init kvm_smp_prepare_boot_cpu(void)
{
	/*
	 * Map the per-cpu variables as decrypted before kvm_guest_cpu_init()
	 * shares the guest physical address with the hypervisor.
	 */
	sev_map_percpu_data();

	kvm_guest_cpu_init();
	native_smp_prepare_boot_cpu();
	kvm_spinlock_init();
}

static int kvm_cpu_down_prepare(unsigned int cpu)
{
	unsigned long flags;

	local_irq_save(flags);
	kvm_guest_cpu_offline(false);
	local_irq_restore(flags);
	return 0;
}

#endif

static int kvm_suspend(void)
{
	u64 val = 0;

	kvm_guest_cpu_offline(false);

#ifdef CONFIG_ARCH_CPUIDLE_HALTPOLL
	if (kvm_para_has_feature(KVM_FEATURE_POLL_CONTROL))
		rdmsrl(MSR_KVM_POLL_CONTROL, val);
	has_guest_poll = !(val & 1);
#endif
	return 0;
}

static void kvm_resume(void)
{
	kvm_cpu_online(raw_smp_processor_id());

#ifdef CONFIG_ARCH_CPUIDLE_HALTPOLL
	if (kvm_para_has_feature(KVM_FEATURE_POLL_CONTROL) && has_guest_poll)
		wrmsrl(MSR_KVM_POLL_CONTROL, 0);
#endif
}

static struct syscore_ops kvm_syscore_ops = {
	.suspend	= kvm_suspend,
	.resume		= kvm_resume,
};

static void kvm_pv_guest_cpu_reboot(void *unused)
{
	kvm_guest_cpu_offline(true);
}

static int kvm_pv_reboot_notify(struct notifier_block *nb,
				unsigned long code, void *unused)
{
	if (code == SYS_RESTART)
		on_each_cpu(kvm_pv_guest_cpu_reboot, NULL, 1);
	return NOTIFY_DONE;
}

static struct notifier_block kvm_pv_reboot_nb = {
	.notifier_call = kvm_pv_reboot_notify,
};

/*
 * After a PV feature is registered, the host will keep writing to the
 * registered memory location. If the guest happens to shutdown, this memory
 * won't be valid. In cases like kexec, in which you install a new kernel, this
 * means a random memory location will be kept being written.
 */
#ifdef CONFIG_KEXEC_CORE
static void kvm_crash_shutdown(struct pt_regs *regs)
{
	kvm_guest_cpu_offline(true);
	native_machine_crash_shutdown(regs);
}
#endif

#if defined(CONFIG_X86_32) || !defined(CONFIG_SMP)
bool __kvm_vcpu_is_preempted(long cpu);

__visible bool __kvm_vcpu_is_preempted(long cpu)
{
	struct kvm_steal_time *src = &per_cpu(steal_time, cpu);

	return !!(src->preempted & KVM_VCPU_PREEMPTED);
}
PV_CALLEE_SAVE_REGS_THUNK(__kvm_vcpu_is_preempted);

#else

#include <asm/asm-offsets.h>

extern bool __raw_callee_save___kvm_vcpu_is_preempted(long);

/*
 * Hand-optimize version for x86-64 to avoid 8 64-bit register saving and
 * restoring to/from the stack.
 */
asm(
".pushsection .text;"
".global __raw_callee_save___kvm_vcpu_is_preempted;"
".type __raw_callee_save___kvm_vcpu_is_preempted, @function;"
"__raw_callee_save___kvm_vcpu_is_preempted:"
ASM_ENDBR
"movq	__per_cpu_offset(,%rdi,8), %rax;"
"cmpb	$0, " __stringify(KVM_STEAL_TIME_preempted) "+steal_time(%rax);"
"setne	%al;"
ASM_RET
".size __raw_callee_save___kvm_vcpu_is_preempted, .-__raw_callee_save___kvm_vcpu_is_preempted;"
".popsection");

#endif

static void __init kvm_guest_init(void)
{
	int i;

	paravirt_ops_setup();
	register_reboot_notifier(&kvm_pv_reboot_nb);
	for (i = 0; i < KVM_TASK_SLEEP_HASHSIZE; i++)
		raw_spin_lock_init(&async_pf_sleepers[i].lock);

	if (kvm_para_has_feature(KVM_FEATURE_STEAL_TIME)) {
		has_steal_clock = 1;
		static_call_update(pv_steal_clock, kvm_steal_clock);

		pv_ops.lock.vcpu_is_preempted =
			PV_CALLEE_SAVE(__kvm_vcpu_is_preempted);
	}

	if (kvm_para_has_feature(KVM_FEATURE_PV_EOI))
		apic_set_eoi_write(kvm_guest_apic_eoi_write);

	if (kvm_para_has_feature(KVM_FEATURE_ASYNC_PF_INT) && kvmapf) {
		static_branch_enable(&kvm_async_pf_enabled);
		alloc_intr_gate(HYPERVISOR_CALLBACK_VECTOR, asm_sysvec_kvm_asyncpf_interrupt);
	}

#ifdef CONFIG_SMP
	if (pv_tlb_flush_supported()) {
		pv_ops.mmu.flush_tlb_multi = kvm_flush_tlb_multi;
		pv_ops.mmu.tlb_remove_table = tlb_remove_table;
		pr_info("KVM setup pv remote TLB flush\n");
	}

	smp_ops.smp_prepare_boot_cpu = kvm_smp_prepare_boot_cpu;
	if (pv_sched_yield_supported()) {
		smp_ops.send_call_func_ipi = kvm_smp_send_call_func_ipi;
		pr_info("setup PV sched yield\n");
	}
	if (cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN, "x86/kvm:online",
				      kvm_cpu_online, kvm_cpu_down_prepare) < 0)
		pr_err("failed to install cpu hotplug callbacks\n");
#else
	sev_map_percpu_data();
	kvm_guest_cpu_init();
#endif

#ifdef CONFIG_KEXEC_CORE
	machine_ops.crash_shutdown = kvm_crash_shutdown;
#endif

	register_syscore_ops(&kvm_syscore_ops);

	/*
	 * Hard lockup detection is enabled by default. Disable it, as guests
	 * can get false positives too easily, for example if the host is
	 * overcommitted.
	 */
	hardlockup_detector_disable();
}

static noinline uint32_t __kvm_cpuid_base(void)
{
	if (boot_cpu_data.cpuid_level < 0)
		return 0;	/* So we don't blow up on old processors */

	if (boot_cpu_has(X86_FEATURE_HYPERVISOR))
		return hypervisor_cpuid_base(KVM_SIGNATURE, 0);

	return 0;
}

static inline uint32_t kvm_cpuid_base(void)
{
	static int kvm_cpuid_base = -1;

	if (kvm_cpuid_base == -1)
		kvm_cpuid_base = __kvm_cpuid_base();

	return kvm_cpuid_base;
}

bool kvm_para_available(void)
{
	return kvm_cpuid_base() != 0;
}
EXPORT_SYMBOL_GPL(kvm_para_available);

unsigned int kvm_arch_para_features(void)
{
	return cpuid_eax(kvm_cpuid_base() | KVM_CPUID_FEATURES);
}

unsigned int kvm_arch_para_hints(void)
{
	return cpuid_edx(kvm_cpuid_base() | KVM_CPUID_FEATURES);
}
EXPORT_SYMBOL_GPL(kvm_arch_para_hints);

static uint32_t __init kvm_detect(void)
{
	return kvm_cpuid_base();
}

static void __init kvm_apic_init(void)
{
#ifdef CONFIG_SMP
	if (pv_ipi_supported())
		kvm_setup_pv_ipi();
#endif
}

static bool __init kvm_msi_ext_dest_id(void)
{
	return kvm_para_has_feature(KVM_FEATURE_MSI_EXT_DEST_ID);
}

static void kvm_sev_hc_page_enc_status(unsigned long pfn, int npages, bool enc)
{
	kvm_sev_hypercall3(KVM_HC_MAP_GPA_RANGE, pfn << PAGE_SHIFT, npages,
			   KVM_MAP_GPA_RANGE_ENC_STAT(enc) | KVM_MAP_GPA_RANGE_PAGE_SZ_4K);
}

static void __init kvm_init_platform(void)
{
	if (cc_platform_has(CC_ATTR_GUEST_MEM_ENCRYPT) &&
	    kvm_para_has_feature(KVM_FEATURE_MIGRATION_CONTROL)) {
		unsigned long nr_pages;
		int i;

		pv_ops.mmu.notify_page_enc_status_changed =
			kvm_sev_hc_page_enc_status;

		/*
		 * Reset the host's shared pages list related to kernel
		 * specific page encryption status settings before we load a
		 * new kernel by kexec. Reset the page encryption status
		 * during early boot intead of just before kexec to avoid SMP
		 * races during kvm_pv_guest_cpu_reboot().
		 * NOTE: We cannot reset the complete shared pages list
		 * here as we need to retain the UEFI/OVMF firmware
		 * specific settings.
		 */

		for (i = 0; i < e820_table->nr_entries; i++) {
			struct e820_entry *entry = &e820_table->entries[i];

			if (entry->type != E820_TYPE_RAM)
				continue;

			nr_pages = DIV_ROUND_UP(entry->size, PAGE_SIZE);

			kvm_sev_hypercall3(KVM_HC_MAP_GPA_RANGE, entry->addr,
				       nr_pages,
				       KVM_MAP_GPA_RANGE_ENCRYPTED | KVM_MAP_GPA_RANGE_PAGE_SZ_4K);
		}

		/*
		 * Ensure that _bss_decrypted section is marked as decrypted in the
		 * shared pages list.
		 */
		early_set_mem_enc_dec_hypercall((unsigned long)__start_bss_decrypted,
						__end_bss_decrypted - __start_bss_decrypted, 0);

		/*
		 * If not booted using EFI, enable Live migration support.
		 */
		if (!efi_enabled(EFI_BOOT))
			wrmsrl(MSR_KVM_MIGRATION_CONTROL,
			       KVM_MIGRATION_READY);
	}
	kvmclock_init();
	x86_platform.apic_post_init = kvm_apic_init;
}

#if defined(CONFIG_AMD_MEM_ENCRYPT)
static void kvm_sev_es_hcall_prepare(struct ghcb *ghcb, struct pt_regs *regs)
{
	/* RAX and CPL are already in the GHCB */
	ghcb_set_rbx(ghcb, regs->bx);
	ghcb_set_rcx(ghcb, regs->cx);
	ghcb_set_rdx(ghcb, regs->dx);
	ghcb_set_rsi(ghcb, regs->si);
}

static bool kvm_sev_es_hcall_finish(struct ghcb *ghcb, struct pt_regs *regs)
{
	/* No checking of the return state needed */
	return true;
}
#endif

const __initconst struct hypervisor_x86 x86_hyper_kvm = {
	.name				= "KVM",
	.detect				= kvm_detect,
	.type				= X86_HYPER_KVM,
	.init.guest_late_init		= kvm_guest_init,
	.init.x2apic_available		= kvm_para_available,
	.init.msi_ext_dest_id		= kvm_msi_ext_dest_id,
	.init.init_platform		= kvm_init_platform,
#if defined(CONFIG_AMD_MEM_ENCRYPT)
	.runtime.sev_es_hcall_prepare	= kvm_sev_es_hcall_prepare,
	.runtime.sev_es_hcall_finish	= kvm_sev_es_hcall_finish,
#endif
};

static __init int activate_jump_labels(void)
{
	if (has_steal_clock) {
		static_key_slow_inc(&paravirt_steal_enabled);
		if (steal_acc)
			static_key_slow_inc(&paravirt_steal_rq_enabled);
	}

	return 0;
}
arch_initcall(activate_jump_labels);

#ifdef CONFIG_PARAVIRT_SPINLOCKS

/* Kick a cpu by its apicid. Used to wake up a halted vcpu */
static void kvm_kick_cpu(int cpu)
{
	int apicid;
	unsigned long flags = 0;

	apicid = per_cpu(x86_cpu_to_apicid, cpu);
	kvm_hypercall2(KVM_HC_KICK_CPU, flags, apicid);
}

#include <asm/qspinlock.h>

static void kvm_wait(u8 *ptr, u8 val)
{
	if (in_nmi())
		return;

	/*
	 * halt until it's our turn and kicked. Note that we do safe halt
	 * for irq enabled case to avoid hang when lock info is overwritten
	 * in irq spinlock slowpath and no spurious interrupt occur to save us.
	 */
	if (irqs_disabled()) {
		if (READ_ONCE(*ptr) == val)
			halt();
	} else {
		local_irq_disable();

		/* safe_halt() will enable IRQ */
		if (READ_ONCE(*ptr) == val)
			safe_halt();
		else
			local_irq_enable();
	}
}

/*
 * Setup pv_lock_ops to exploit KVM_FEATURE_PV_UNHALT if present.
 */
void __init kvm_spinlock_init(void)
{
	/*
	 * In case host doesn't support KVM_FEATURE_PV_UNHALT there is still an
	 * advantage of keeping virt_spin_lock_key enabled: virt_spin_lock() is
	 * preferred over native qspinlock when vCPU is preempted.
	 */
	if (!kvm_para_has_feature(KVM_FEATURE_PV_UNHALT)) {
		pr_info("PV spinlocks disabled, no host support\n");
		return;
	}

	/*
	 * Disable PV spinlocks and use native qspinlock when dedicated pCPUs
	 * are available.
	 */
	if (kvm_para_has_hint(KVM_HINTS_REALTIME)) {
		pr_info("PV spinlocks disabled with KVM_HINTS_REALTIME hints\n");
		goto out;
	}

	if (num_possible_cpus() == 1) {
		pr_info("PV spinlocks disabled, single CPU\n");
		goto out;
	}

	if (nopvspin) {
		pr_info("PV spinlocks disabled, forced by \"nopvspin\" parameter\n");
		goto out;
	}

	pr_info("PV spinlocks enabled\n");

	__pv_init_lock_hash();
	pv_ops.lock.queued_spin_lock_slowpath = __pv_queued_spin_lock_slowpath;
	pv_ops.lock.queued_spin_unlock =
		PV_CALLEE_SAVE(__pv_queued_spin_unlock);
	pv_ops.lock.wait = kvm_wait;
	pv_ops.lock.kick = kvm_kick_cpu;

	/*
	 * When PV spinlock is enabled which is preferred over
	 * virt_spin_lock(), virt_spin_lock_key's value is meaningless.
	 * Just disable it anyway.
	 */
out:
	static_branch_disable(&virt_spin_lock_key);
}

#endif	/* CONFIG_PARAVIRT_SPINLOCKS */

#ifdef CONFIG_ARCH_CPUIDLE_HALTPOLL

static void kvm_disable_host_haltpoll(void *i)
{
	wrmsrl(MSR_KVM_POLL_CONTROL, 0);
}

static void kvm_enable_host_haltpoll(void *i)
{
	wrmsrl(MSR_KVM_POLL_CONTROL, 1);
}

void arch_haltpoll_enable(unsigned int cpu)
{
	if (!kvm_para_has_feature(KVM_FEATURE_POLL_CONTROL)) {
		pr_err_once("host does not support poll control\n");
		pr_err_once("host upgrade recommended\n");
		return;
	}

	/* Enable guest halt poll disables host halt poll */
	smp_call_function_single(cpu, kvm_disable_host_haltpoll, NULL, 1);
}
EXPORT_SYMBOL_GPL(arch_haltpoll_enable);

void arch_haltpoll_disable(unsigned int cpu)
{
	if (!kvm_para_has_feature(KVM_FEATURE_POLL_CONTROL))
		return;

	/* Disable guest halt poll enables host halt poll */
	smp_call_function_single(cpu, kvm_enable_host_haltpoll, NULL, 1);
}
EXPORT_SYMBOL_GPL(arch_haltpoll_disable);
#endif
