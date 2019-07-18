/*
 * KVM paravirt_ops implementation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright (C) 2007, Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 * Copyright IBM Corporation, 2007
 *   Authors: Anthony Liguori <aliguori@us.ibm.com>
 */

#include <linux/context_tracking.h>
#include <linux/init.h>
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
#include <linux/debugfs.h>
#include <linux/nmi.h>
#include <linux/swait.h>
#include <asm/timer.h>
#include <asm/cpu.h>
#include <asm/traps.h>
#include <asm/desc.h>
#include <asm/tlbflush.h>
#include <asm/apic.h>
#include <asm/apicdef.h>
#include <asm/hypervisor.h>
#include <asm/tlb.h>

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
static DEFINE_PER_CPU_DECRYPTED(struct kvm_steal_time, steal_time) __aligned(64);
static int has_steal_clock = 0;

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
	bool halted;
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

/*
 * @interrupt_kernel: Is this called from a routine which interrupts the kernel
 * 		      (other than user space)?
 */
void kvm_async_pf_task_wait(u32 token, int interrupt_kernel)
{
	u32 key = hash_32(token, KVM_TASK_SLEEP_HASHBITS);
	struct kvm_task_sleep_head *b = &async_pf_sleepers[key];
	struct kvm_task_sleep_node n, *e;
	DECLARE_SWAITQUEUE(wait);

	rcu_irq_enter();

	raw_spin_lock(&b->lock);
	e = _find_apf_task(b, token);
	if (e) {
		/* dummy entry exist -> wake up was delivered ahead of PF */
		hlist_del(&e->link);
		kfree(e);
		raw_spin_unlock(&b->lock);

		rcu_irq_exit();
		return;
	}

	n.token = token;
	n.cpu = smp_processor_id();
	n.halted = is_idle_task(current) ||
		   (IS_ENABLED(CONFIG_PREEMPT_COUNT)
		    ? preempt_count() > 1 || rcu_preempt_depth()
		    : interrupt_kernel);
	init_swait_queue_head(&n.wq);
	hlist_add_head(&n.link, &b->list);
	raw_spin_unlock(&b->lock);

	for (;;) {
		if (!n.halted)
			prepare_to_swait_exclusive(&n.wq, &wait, TASK_UNINTERRUPTIBLE);
		if (hlist_unhashed(&n.link))
			break;

		rcu_irq_exit();

		if (!n.halted) {
			local_irq_enable();
			schedule();
			local_irq_disable();
		} else {
			/*
			 * We cannot reschedule. So halt.
			 */
			native_safe_halt();
			local_irq_disable();
		}

		rcu_irq_enter();
	}
	if (!n.halted)
		finish_swait(&n.wq, &wait);

	rcu_irq_exit();
	return;
}
EXPORT_SYMBOL_GPL(kvm_async_pf_task_wait);

static void apf_task_wake_one(struct kvm_task_sleep_node *n)
{
	hlist_del_init(&n->link);
	if (n->halted)
		smp_send_reschedule(n->cpu);
	else if (swq_has_sleeper(&n->wq))
		swake_up_one(&n->wq);
}

static void apf_task_wake_all(void)
{
	int i;

	for (i = 0; i < KVM_TASK_SLEEP_HASHSIZE; i++) {
		struct hlist_node *p, *next;
		struct kvm_task_sleep_head *b = &async_pf_sleepers[i];
		raw_spin_lock(&b->lock);
		hlist_for_each_safe(p, next, &b->list) {
			struct kvm_task_sleep_node *n =
				hlist_entry(p, typeof(*n), link);
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
	struct kvm_task_sleep_node *n;

	if (token == ~0) {
		apf_task_wake_all();
		return;
	}

again:
	raw_spin_lock(&b->lock);
	n = _find_apf_task(b, token);
	if (!n) {
		/*
		 * async PF was not yet handled.
		 * Add dummy entry for the token.
		 */
		n = kzalloc(sizeof(*n), GFP_ATOMIC);
		if (!n) {
			/*
			 * Allocation failed! Busy wait while other cpu
			 * handles async PF.
			 */
			raw_spin_unlock(&b->lock);
			cpu_relax();
			goto again;
		}
		n->token = token;
		n->cpu = smp_processor_id();
		init_swait_queue_head(&n->wq);
		hlist_add_head(&n->link, &b->list);
	} else
		apf_task_wake_one(n);
	raw_spin_unlock(&b->lock);
	return;
}
EXPORT_SYMBOL_GPL(kvm_async_pf_task_wake);

u32 kvm_read_and_reset_pf_reason(void)
{
	u32 reason = 0;

	if (__this_cpu_read(apf_reason.enabled)) {
		reason = __this_cpu_read(apf_reason.reason);
		__this_cpu_write(apf_reason.reason, 0);
	}

	return reason;
}
EXPORT_SYMBOL_GPL(kvm_read_and_reset_pf_reason);
NOKPROBE_SYMBOL(kvm_read_and_reset_pf_reason);

dotraplinkage void
do_async_page_fault(struct pt_regs *regs, unsigned long error_code)
{
	enum ctx_state prev_state;

	switch (kvm_read_and_reset_pf_reason()) {
	default:
		do_page_fault(regs, error_code);
		break;
	case KVM_PV_REASON_PAGE_NOT_PRESENT:
		/* page is swapped out by the host. */
		prev_state = exception_enter();
		kvm_async_pf_task_wait((u32)read_cr2(), !user_mode(regs));
		exception_exit(prev_state);
		break;
	case KVM_PV_REASON_PAGE_READY:
		rcu_irq_enter();
		kvm_async_pf_task_wake((u32)read_cr2());
		rcu_irq_exit();
		break;
	}
}
NOKPROBE_SYMBOL(do_async_page_fault);

static void __init paravirt_ops_setup(void)
{
	pv_info.name = "KVM";

	if (kvm_para_has_feature(KVM_FEATURE_NOP_IO_DELAY))
		pv_cpu_ops.io_delay = kvm_io_delay;

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
	pr_info("kvm-stealtime: cpu %d, msr %llx\n",
		cpu, (unsigned long long) slow_virt_to_phys(st));
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
	if (!kvm_para_available())
		return;

	if (kvm_para_has_feature(KVM_FEATURE_ASYNC_PF) && kvmapf) {
		u64 pa = slow_virt_to_phys(this_cpu_ptr(&apf_reason));

#ifdef CONFIG_PREEMPT
		pa |= KVM_ASYNC_PF_SEND_ALWAYS;
#endif
		pa |= KVM_ASYNC_PF_ENABLED;

		if (kvm_para_has_feature(KVM_FEATURE_ASYNC_PF_VMEXIT))
			pa |= KVM_ASYNC_PF_DELIVERY_AS_PF_VMEXIT;

		wrmsrl(MSR_KVM_ASYNC_PF_EN, pa);
		__this_cpu_write(apf_reason.enabled, 1);
		printk(KERN_INFO"KVM setup async PF for cpu %d\n",
		       smp_processor_id());
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

	printk(KERN_INFO"Unregister pv shared memory for cpu %d\n",
	       smp_processor_id());
}

static void kvm_pv_guest_cpu_reboot(void *unused)
{
	/*
	 * We disable PV EOI before we load a new kernel by kexec,
	 * since MSR_KVM_PV_EOI_EN stores a pointer into old kernel's memory.
	 * New kernel can re-enable when it boots.
	 */
	if (kvm_para_has_feature(KVM_FEATURE_PV_EOI))
		wrmsrl(MSR_KVM_PV_EOI_EN, 0);
	kvm_pv_disable_apf();
	kvm_disable_steal_time();
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

void kvm_disable_steal_time(void)
{
	if (!has_steal_clock)
		return;

	wrmsr(MSR_KVM_STEAL_TIME, 0, 0);
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

	if (!sev_active())
		return;

	for_each_possible_cpu(cpu) {
		__set_percpu_decrypted(&per_cpu(apf_reason, cpu), sizeof(apf_reason));
		__set_percpu_decrypted(&per_cpu(steal_time, cpu), sizeof(steal_time));
		__set_percpu_decrypted(&per_cpu(kvm_apic_eoi, cpu), sizeof(kvm_apic_eoi));
	}
}

#ifdef CONFIG_SMP
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
		} else if (apic_id < min + KVM_IPI_CLUSTER_SIZE) {
			max = apic_id < max ? max : apic_id;
		} else {
			ret = kvm_hypercall4(KVM_HC_SEND_IPI, (unsigned long)ipi_bitmap,
				(unsigned long)(ipi_bitmap >> BITS_PER_LONG), min, icr);
			WARN_ONCE(ret < 0, "KVM: failed to send PV IPI: %ld", ret);
			min = max = apic_id;
			ipi_bitmap = 0;
		}
		__set_bit(apic_id - min, (unsigned long *)&ipi_bitmap);
	}

	if (ipi_bitmap) {
		ret = kvm_hypercall4(KVM_HC_SEND_IPI, (unsigned long)ipi_bitmap,
			(unsigned long)(ipi_bitmap >> BITS_PER_LONG), min, icr);
		WARN_ONCE(ret < 0, "KVM: failed to send PV IPI: %ld", ret);
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
	struct cpumask new_mask;
	const struct cpumask *local_mask;

	cpumask_copy(&new_mask, mask);
	cpumask_clear_cpu(this_cpu, &new_mask);
	local_mask = &new_mask;
	__send_ipi_mask(local_mask, vector);
}

static void kvm_send_ipi_allbutself(int vector)
{
	kvm_send_ipi_mask_allbutself(cpu_online_mask, vector);
}

static void kvm_send_ipi_all(int vector)
{
	__send_ipi_mask(cpu_online_mask, vector);
}

/*
 * Set the IPI entry points
 */
static void kvm_setup_pv_ipi(void)
{
	apic->send_IPI_mask = kvm_send_ipi_mask;
	apic->send_IPI_mask_allbutself = kvm_send_ipi_mask_allbutself;
	apic->send_IPI_allbutself = kvm_send_ipi_allbutself;
	apic->send_IPI_all = kvm_send_ipi_all;
	pr_info("KVM setup pv IPIs\n");
}

static void __init kvm_smp_prepare_cpus(unsigned int max_cpus)
{
	native_smp_prepare_cpus(max_cpus);
	if (kvm_para_has_hint(KVM_HINTS_REALTIME))
		static_branch_disable(&virt_spin_lock_key);
}

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

static void kvm_guest_cpu_offline(void)
{
	kvm_disable_steal_time();
	if (kvm_para_has_feature(KVM_FEATURE_PV_EOI))
		wrmsrl(MSR_KVM_PV_EOI_EN, 0);
	kvm_pv_disable_apf();
	apf_task_wake_all();
}

static int kvm_cpu_online(unsigned int cpu)
{
	local_irq_disable();
	kvm_guest_cpu_init();
	local_irq_enable();
	return 0;
}

static int kvm_cpu_down_prepare(unsigned int cpu)
{
	local_irq_disable();
	kvm_guest_cpu_offline();
	local_irq_enable();
	return 0;
}
#endif

static void __init kvm_apf_trap_init(void)
{
	update_intr_gate(X86_TRAP_PF, async_page_fault);
}

static DEFINE_PER_CPU(cpumask_var_t, __pv_tlb_mask);

static void kvm_flush_tlb_others(const struct cpumask *cpumask,
			const struct flush_tlb_info *info)
{
	u8 state;
	int cpu;
	struct kvm_steal_time *src;
	struct cpumask *flushmask = this_cpu_cpumask_var_ptr(__pv_tlb_mask);

	cpumask_copy(flushmask, cpumask);
	/*
	 * We have to call flush only on online vCPUs. And
	 * queue flush_on_enter for pre-empted vCPUs
	 */
	for_each_cpu(cpu, flushmask) {
		src = &per_cpu(steal_time, cpu);
		state = READ_ONCE(src->preempted);
		if ((state & KVM_VCPU_PREEMPTED)) {
			if (try_cmpxchg(&src->preempted, &state,
					state | KVM_VCPU_FLUSH_TLB))
				__cpumask_clear_cpu(cpu, flushmask);
		}
	}

	native_flush_tlb_others(flushmask, info);
}

static void __init kvm_guest_init(void)
{
	int i;

	if (!kvm_para_available())
		return;

	paravirt_ops_setup();
	register_reboot_notifier(&kvm_pv_reboot_nb);
	for (i = 0; i < KVM_TASK_SLEEP_HASHSIZE; i++)
		raw_spin_lock_init(&async_pf_sleepers[i].lock);
	if (kvm_para_has_feature(KVM_FEATURE_ASYNC_PF))
		x86_init.irqs.trap_init = kvm_apf_trap_init;

	if (kvm_para_has_feature(KVM_FEATURE_STEAL_TIME)) {
		has_steal_clock = 1;
		pv_time_ops.steal_clock = kvm_steal_clock;
	}

	if (kvm_para_has_feature(KVM_FEATURE_PV_TLB_FLUSH) &&
	    !kvm_para_has_hint(KVM_HINTS_REALTIME) &&
	    kvm_para_has_feature(KVM_FEATURE_STEAL_TIME)) {
		pv_mmu_ops.flush_tlb_others = kvm_flush_tlb_others;
		pv_mmu_ops.tlb_remove_table = tlb_remove_table;
	}

	if (kvm_para_has_feature(KVM_FEATURE_PV_EOI))
		apic_set_eoi_write(kvm_guest_apic_eoi_write);

#ifdef CONFIG_SMP
	smp_ops.smp_prepare_cpus = kvm_smp_prepare_cpus;
	smp_ops.smp_prepare_boot_cpu = kvm_smp_prepare_boot_cpu;
	if (cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN, "x86/kvm:online",
				      kvm_cpu_online, kvm_cpu_down_prepare) < 0)
		pr_err("kvm_guest: Failed to install cpu hotplug callbacks\n");
#else
	sev_map_percpu_data();
	kvm_guest_cpu_init();
#endif

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
		return hypervisor_cpuid_base("KVMKVMKVM\0\0\0", 0);

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

static uint32_t __init kvm_detect(void)
{
	return kvm_cpuid_base();
}

static void __init kvm_apic_init(void)
{
#if defined(CONFIG_SMP)
	if (kvm_para_has_feature(KVM_FEATURE_PV_SEND_IPI))
		kvm_setup_pv_ipi();
#endif
}

static void __init kvm_init_platform(void)
{
	kvmclock_init();
	x86_platform.apic_post_init = kvm_apic_init;
}

const __initconst struct hypervisor_x86 x86_hyper_kvm = {
	.name			= "KVM",
	.detect			= kvm_detect,
	.type			= X86_HYPER_KVM,
	.init.guest_late_init	= kvm_guest_init,
	.init.x2apic_available	= kvm_para_available,
	.init.init_platform	= kvm_init_platform,
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

static __init int kvm_setup_pv_tlb_flush(void)
{
	int cpu;

	if (kvm_para_has_feature(KVM_FEATURE_PV_TLB_FLUSH) &&
	    !kvm_para_has_hint(KVM_HINTS_REALTIME) &&
	    kvm_para_has_feature(KVM_FEATURE_STEAL_TIME)) {
		for_each_possible_cpu(cpu) {
			zalloc_cpumask_var_node(per_cpu_ptr(&__pv_tlb_mask, cpu),
				GFP_KERNEL, cpu_to_node(cpu));
		}
		pr_info("KVM setup pv remote TLB flush\n");
	}

	return 0;
}
arch_initcall(kvm_setup_pv_tlb_flush);

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
	unsigned long flags;

	if (in_nmi())
		return;

	local_irq_save(flags);

	if (READ_ONCE(*ptr) != val)
		goto out;

	/*
	 * halt until it's our turn and kicked. Note that we do safe halt
	 * for irq enabled case to avoid hang when lock info is overwritten
	 * in irq spinlock slowpath and no spurious interrupt occur to save us.
	 */
	if (arch_irqs_disabled_flags(flags))
		halt();
	else
		safe_halt();

out:
	local_irq_restore(flags);
}

#ifdef CONFIG_X86_32
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
"movq	__per_cpu_offset(,%rdi,8), %rax;"
"cmpb	$0, " __stringify(KVM_STEAL_TIME_preempted) "+steal_time(%rax);"
"setne	%al;"
"ret;"
".size __raw_callee_save___kvm_vcpu_is_preempted, .-__raw_callee_save___kvm_vcpu_is_preempted;"
".popsection");

#endif

/*
 * Setup pv_lock_ops to exploit KVM_FEATURE_PV_UNHALT if present.
 */
void __init kvm_spinlock_init(void)
{
	if (!kvm_para_available())
		return;
	/* Does host kernel support KVM_FEATURE_PV_UNHALT? */
	if (!kvm_para_has_feature(KVM_FEATURE_PV_UNHALT))
		return;

	if (kvm_para_has_hint(KVM_HINTS_REALTIME))
		return;

	/* Don't use the pvqspinlock code if there is only 1 vCPU. */
	if (num_possible_cpus() == 1)
		return;

	__pv_init_lock_hash();
	pv_lock_ops.queued_spin_lock_slowpath = __pv_queued_spin_lock_slowpath;
	pv_lock_ops.queued_spin_unlock = PV_CALLEE_SAVE(__pv_queued_spin_unlock);
	pv_lock_ops.wait = kvm_wait;
	pv_lock_ops.kick = kvm_kick_cpu;

	if (kvm_para_has_feature(KVM_FEATURE_STEAL_TIME)) {
		pv_lock_ops.vcpu_is_preempted =
			PV_CALLEE_SAVE(__kvm_vcpu_is_preempted);
	}
}

#endif	/* CONFIG_PARAVIRT_SPINLOCKS */
