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

#include <linux/module.h>
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
#include <asm/timer.h>
#include <asm/cpu.h>
#include <asm/traps.h>
#include <asm/desc.h>
#include <asm/tlbflush.h>
#include <asm/idle.h>
#include <asm/apic.h>
#include <asm/apicdef.h>
#include <asm/hypervisor.h>

static int kvmapf = 1;

static int parse_no_kvmapf(char *arg)
{
        kvmapf = 0;
        return 0;
}

early_param("no-kvmapf", parse_no_kvmapf);

static int steal_acc = 1;
static int parse_no_stealacc(char *arg)
{
        steal_acc = 0;
        return 0;
}

early_param("no-steal-acc", parse_no_stealacc);

static DEFINE_PER_CPU(struct kvm_vcpu_pv_apf_data, apf_reason) __aligned(64);
static DEFINE_PER_CPU(struct kvm_steal_time, steal_time) __aligned(64);
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
	wait_queue_head_t wq;
	u32 token;
	int cpu;
	bool halted;
};

static struct kvm_task_sleep_head {
	spinlock_t lock;
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

void kvm_async_pf_task_wait(u32 token)
{
	u32 key = hash_32(token, KVM_TASK_SLEEP_HASHBITS);
	struct kvm_task_sleep_head *b = &async_pf_sleepers[key];
	struct kvm_task_sleep_node n, *e;
	DEFINE_WAIT(wait);
	int cpu, idle;

	cpu = get_cpu();
	idle = idle_cpu(cpu);
	put_cpu();

	spin_lock(&b->lock);
	e = _find_apf_task(b, token);
	if (e) {
		/* dummy entry exist -> wake up was delivered ahead of PF */
		hlist_del(&e->link);
		kfree(e);
		spin_unlock(&b->lock);
		return;
	}

	n.token = token;
	n.cpu = smp_processor_id();
	n.halted = idle || preempt_count() > 1;
	init_waitqueue_head(&n.wq);
	hlist_add_head(&n.link, &b->list);
	spin_unlock(&b->lock);

	for (;;) {
		if (!n.halted)
			prepare_to_wait(&n.wq, &wait, TASK_UNINTERRUPTIBLE);
		if (hlist_unhashed(&n.link))
			break;

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
	}
	if (!n.halted)
		finish_wait(&n.wq, &wait);

	return;
}
EXPORT_SYMBOL_GPL(kvm_async_pf_task_wait);

static void apf_task_wake_one(struct kvm_task_sleep_node *n)
{
	hlist_del_init(&n->link);
	if (n->halted)
		smp_send_reschedule(n->cpu);
	else if (waitqueue_active(&n->wq))
		wake_up(&n->wq);
}

static void apf_task_wake_all(void)
{
	int i;

	for (i = 0; i < KVM_TASK_SLEEP_HASHSIZE; i++) {
		struct hlist_node *p, *next;
		struct kvm_task_sleep_head *b = &async_pf_sleepers[i];
		spin_lock(&b->lock);
		hlist_for_each_safe(p, next, &b->list) {
			struct kvm_task_sleep_node *n =
				hlist_entry(p, typeof(*n), link);
			if (n->cpu == smp_processor_id())
				apf_task_wake_one(n);
		}
		spin_unlock(&b->lock);
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
	spin_lock(&b->lock);
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
			spin_unlock(&b->lock);
			cpu_relax();
			goto again;
		}
		n->token = token;
		n->cpu = smp_processor_id();
		init_waitqueue_head(&n->wq);
		hlist_add_head(&n->link, &b->list);
	} else
		apf_task_wake_one(n);
	spin_unlock(&b->lock);
	return;
}
EXPORT_SYMBOL_GPL(kvm_async_pf_task_wake);

u32 kvm_read_and_reset_pf_reason(void)
{
	u32 reason = 0;

	if (__get_cpu_var(apf_reason).enabled) {
		reason = __get_cpu_var(apf_reason).reason;
		__get_cpu_var(apf_reason).reason = 0;
	}

	return reason;
}
EXPORT_SYMBOL_GPL(kvm_read_and_reset_pf_reason);

dotraplinkage void __kprobes
do_async_page_fault(struct pt_regs *regs, unsigned long error_code)
{
	switch (kvm_read_and_reset_pf_reason()) {
	default:
		do_page_fault(regs, error_code);
		break;
	case KVM_PV_REASON_PAGE_NOT_PRESENT:
		/* page is swapped out by the host. */
		rcu_irq_enter();
		exit_idle();
		kvm_async_pf_task_wait((u32)read_cr2());
		rcu_irq_exit();
		break;
	case KVM_PV_REASON_PAGE_READY:
		rcu_irq_enter();
		exit_idle();
		kvm_async_pf_task_wake((u32)read_cr2());
		rcu_irq_exit();
		break;
	}
}

static void __init paravirt_ops_setup(void)
{
	pv_info.name = "KVM";
	pv_info.paravirt_enabled = 1;

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

	memset(st, 0, sizeof(*st));

	wrmsrl(MSR_KVM_STEAL_TIME, (__pa(st) | KVM_MSR_ENABLED));
	printk(KERN_INFO "kvm-stealtime: cpu %d, msr %lx\n",
		cpu, __pa(st));
}

static DEFINE_PER_CPU(unsigned long, kvm_apic_eoi) = KVM_PV_EOI_DISABLED;

static void kvm_guest_apic_eoi_write(u32 reg, u32 val)
{
	/**
	 * This relies on __test_and_clear_bit to modify the memory
	 * in a way that is atomic with respect to the local CPU.
	 * The hypervisor only accesses this memory from the local CPU so
	 * there's no need for lock or memory barriers.
	 * An optimization barrier is implied in apic write.
	 */
	if (__test_and_clear_bit(KVM_PV_EOI_BIT, &__get_cpu_var(kvm_apic_eoi)))
		return;
	apic_write(APIC_EOI, APIC_EOI_ACK);
}

void __cpuinit kvm_guest_cpu_init(void)
{
	if (!kvm_para_available())
		return;

	if (kvm_para_has_feature(KVM_FEATURE_ASYNC_PF) && kvmapf) {
		u64 pa = __pa(&__get_cpu_var(apf_reason));

#ifdef CONFIG_PREEMPT
		pa |= KVM_ASYNC_PF_SEND_ALWAYS;
#endif
		wrmsrl(MSR_KVM_ASYNC_PF_EN, pa | KVM_ASYNC_PF_ENABLED);
		__get_cpu_var(apf_reason).enabled = 1;
		printk(KERN_INFO"KVM setup async PF for cpu %d\n",
		       smp_processor_id());
	}

	if (kvm_para_has_feature(KVM_FEATURE_PV_EOI)) {
		unsigned long pa;
		/* Size alignment is implied but just to make it explicit. */
		BUILD_BUG_ON(__alignof__(kvm_apic_eoi) < 4);
		__get_cpu_var(kvm_apic_eoi) = 0;
		pa = __pa(&__get_cpu_var(kvm_apic_eoi)) | KVM_MSR_ENABLED;
		wrmsrl(MSR_KVM_PV_EOI_EN, pa);
	}

	if (has_steal_clock)
		kvm_register_steal_time();
}

static void kvm_pv_disable_apf(void)
{
	if (!__get_cpu_var(apf_reason).enabled)
		return;

	wrmsrl(MSR_KVM_ASYNC_PF_EN, 0);
	__get_cpu_var(apf_reason).enabled = 0;

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
		rmb();
		steal = src->steal;
		rmb();
	} while ((version & 1) || (version != src->version));

	return steal;
}

void kvm_disable_steal_time(void)
{
	if (!has_steal_clock)
		return;

	wrmsr(MSR_KVM_STEAL_TIME, 0, 0);
}

#ifdef CONFIG_SMP
static void __init kvm_smp_prepare_boot_cpu(void)
{
	WARN_ON(kvm_register_clock("primary cpu clock"));
	kvm_guest_cpu_init();
	native_smp_prepare_boot_cpu();
}

static void __cpuinit kvm_guest_cpu_online(void *dummy)
{
	kvm_guest_cpu_init();
}

static void kvm_guest_cpu_offline(void *dummy)
{
	kvm_disable_steal_time();
	if (kvm_para_has_feature(KVM_FEATURE_PV_EOI))
		wrmsrl(MSR_KVM_PV_EOI_EN, 0);
	kvm_pv_disable_apf();
	apf_task_wake_all();
}

static int __cpuinit kvm_cpu_notify(struct notifier_block *self,
				    unsigned long action, void *hcpu)
{
	int cpu = (unsigned long)hcpu;
	switch (action) {
	case CPU_ONLINE:
	case CPU_DOWN_FAILED:
	case CPU_ONLINE_FROZEN:
		smp_call_function_single(cpu, kvm_guest_cpu_online, NULL, 0);
		break;
	case CPU_DOWN_PREPARE:
	case CPU_DOWN_PREPARE_FROZEN:
		smp_call_function_single(cpu, kvm_guest_cpu_offline, NULL, 1);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata kvm_cpu_notifier = {
        .notifier_call  = kvm_cpu_notify,
};
#endif

static void __init kvm_apf_trap_init(void)
{
	set_intr_gate(14, &async_page_fault);
}

void __init kvm_guest_init(void)
{
	int i;

	if (!kvm_para_available())
		return;

	paravirt_ops_setup();
	register_reboot_notifier(&kvm_pv_reboot_nb);
	for (i = 0; i < KVM_TASK_SLEEP_HASHSIZE; i++)
		spin_lock_init(&async_pf_sleepers[i].lock);
	if (kvm_para_has_feature(KVM_FEATURE_ASYNC_PF))
		x86_init.irqs.trap_init = kvm_apf_trap_init;

	if (kvm_para_has_feature(KVM_FEATURE_STEAL_TIME)) {
		has_steal_clock = 1;
		pv_time_ops.steal_clock = kvm_steal_clock;
	}

	if (kvm_para_has_feature(KVM_FEATURE_PV_EOI))
		apic_set_eoi_write(kvm_guest_apic_eoi_write);

#ifdef CONFIG_SMP
	smp_ops.smp_prepare_boot_cpu = kvm_smp_prepare_boot_cpu;
	register_cpu_notifier(&kvm_cpu_notifier);
#else
	kvm_guest_cpu_init();
#endif
}

static bool __init kvm_detect(void)
{
	if (!kvm_para_available())
		return false;
	return true;
}

const struct hypervisor_x86 x86_hyper_kvm __refconst = {
	.name			= "KVM",
	.detect			= kvm_detect,
};
EXPORT_SYMBOL_GPL(x86_hyper_kvm);

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
