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
#include <linux/debugfs.h>
#include <asm/timer.h>
#include <asm/cpu.h>
#include <asm/traps.h>
#include <asm/desc.h>
#include <asm/tlbflush.h>
#include <asm/idle.h>
#include <asm/apic.h>
#include <asm/apicdef.h>
#include <asm/hypervisor.h>
#include <asm/kvm_guest.h>

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

static int kvmclock_vsyscall = 1;
static int parse_no_kvmclock_vsyscall(char *arg)
{
        kvmclock_vsyscall = 0;
        return 0;
}

early_param("no-kvmclock-vsyscall", parse_no_kvmclock_vsyscall);

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

	rcu_irq_enter();

	spin_lock(&b->lock);
	e = _find_apf_task(b, token);
	if (e) {
		/* dummy entry exist -> wake up was delivered ahead of PF */
		hlist_del(&e->link);
		kfree(e);
		spin_unlock(&b->lock);

		rcu_irq_exit();
		return;
	}

	n.token = token;
	n.cpu = smp_processor_id();
	n.halted = is_idle_task(current) || preempt_count() > 1;
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
			rcu_irq_exit();
			native_safe_halt();
			rcu_irq_enter();
			local_irq_disable();
		}
	}
	if (!n.halted)
		finish_wait(&n.wq, &wait);

	rcu_irq_exit();
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
	enum ctx_state prev_state;

	switch (kvm_read_and_reset_pf_reason()) {
	default:
		do_page_fault(regs, error_code);
		break;
	case KVM_PV_REASON_PAGE_NOT_PRESENT:
		/* page is swapped out by the host. */
		prev_state = exception_enter();
		exit_idle();
		kvm_async_pf_task_wait((u32)read_cr2());
		exception_exit(prev_state);
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

	wrmsrl(MSR_KVM_STEAL_TIME, (slow_virt_to_phys(st) | KVM_MSR_ENABLED));
	pr_info("kvm-stealtime: cpu %d, msr %llx\n",
		cpu, (unsigned long long) slow_virt_to_phys(st));
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

void kvm_guest_cpu_init(void)
{
	if (!kvm_para_available())
		return;

	if (kvm_para_has_feature(KVM_FEATURE_ASYNC_PF) && kvmapf) {
		u64 pa = slow_virt_to_phys(&__get_cpu_var(apf_reason));

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
		pa = slow_virt_to_phys(&__get_cpu_var(kvm_apic_eoi))
			| KVM_MSR_ENABLED;
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
	kvm_spinlock_init();
}

static void kvm_guest_cpu_online(void *dummy)
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

static int kvm_cpu_notify(struct notifier_block *self, unsigned long action,
			  void *hcpu)
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

static struct notifier_block kvm_cpu_notifier = {
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

	if (kvmclock_vsyscall)
		kvm_setup_vsyscall_timeinfo();

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
	.x2apic_available	= kvm_para_available,
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

#ifdef CONFIG_PARAVIRT_SPINLOCKS

/* Kick a cpu by its apicid. Used to wake up a halted vcpu */
static void kvm_kick_cpu(int cpu)
{
	int apicid;
	unsigned long flags = 0;

	apicid = per_cpu(x86_cpu_to_apicid, cpu);
	kvm_hypercall2(KVM_HC_KICK_CPU, flags, apicid);
}

enum kvm_contention_stat {
	TAKEN_SLOW,
	TAKEN_SLOW_PICKUP,
	RELEASED_SLOW,
	RELEASED_SLOW_KICKED,
	NR_CONTENTION_STATS
};

#ifdef CONFIG_KVM_DEBUG_FS
#define HISTO_BUCKETS	30

static struct kvm_spinlock_stats
{
	u32 contention_stats[NR_CONTENTION_STATS];
	u32 histo_spin_blocked[HISTO_BUCKETS+1];
	u64 time_blocked;
} spinlock_stats;

static u8 zero_stats;

static inline void check_zero(void)
{
	u8 ret;
	u8 old;

	old = ACCESS_ONCE(zero_stats);
	if (unlikely(old)) {
		ret = cmpxchg(&zero_stats, old, 0);
		/* This ensures only one fellow resets the stat */
		if (ret == old)
			memset(&spinlock_stats, 0, sizeof(spinlock_stats));
	}
}

static inline void add_stats(enum kvm_contention_stat var, u32 val)
{
	check_zero();
	spinlock_stats.contention_stats[var] += val;
}


static inline u64 spin_time_start(void)
{
	return sched_clock();
}

static void __spin_time_accum(u64 delta, u32 *array)
{
	unsigned index;

	index = ilog2(delta);
	check_zero();

	if (index < HISTO_BUCKETS)
		array[index]++;
	else
		array[HISTO_BUCKETS]++;
}

static inline void spin_time_accum_blocked(u64 start)
{
	u32 delta;

	delta = sched_clock() - start;
	__spin_time_accum(delta, spinlock_stats.histo_spin_blocked);
	spinlock_stats.time_blocked += delta;
}

static struct dentry *d_spin_debug;
static struct dentry *d_kvm_debug;

struct dentry *kvm_init_debugfs(void)
{
	d_kvm_debug = debugfs_create_dir("kvm", NULL);
	if (!d_kvm_debug)
		printk(KERN_WARNING "Could not create 'kvm' debugfs directory\n");

	return d_kvm_debug;
}

static int __init kvm_spinlock_debugfs(void)
{
	struct dentry *d_kvm;

	d_kvm = kvm_init_debugfs();
	if (d_kvm == NULL)
		return -ENOMEM;

	d_spin_debug = debugfs_create_dir("spinlocks", d_kvm);

	debugfs_create_u8("zero_stats", 0644, d_spin_debug, &zero_stats);

	debugfs_create_u32("taken_slow", 0444, d_spin_debug,
		   &spinlock_stats.contention_stats[TAKEN_SLOW]);
	debugfs_create_u32("taken_slow_pickup", 0444, d_spin_debug,
		   &spinlock_stats.contention_stats[TAKEN_SLOW_PICKUP]);

	debugfs_create_u32("released_slow", 0444, d_spin_debug,
		   &spinlock_stats.contention_stats[RELEASED_SLOW]);
	debugfs_create_u32("released_slow_kicked", 0444, d_spin_debug,
		   &spinlock_stats.contention_stats[RELEASED_SLOW_KICKED]);

	debugfs_create_u64("time_blocked", 0444, d_spin_debug,
			   &spinlock_stats.time_blocked);

	debugfs_create_u32_array("histo_blocked", 0444, d_spin_debug,
		     spinlock_stats.histo_spin_blocked, HISTO_BUCKETS + 1);

	return 0;
}
fs_initcall(kvm_spinlock_debugfs);
#else  /* !CONFIG_KVM_DEBUG_FS */
static inline void add_stats(enum kvm_contention_stat var, u32 val)
{
}

static inline u64 spin_time_start(void)
{
	return 0;
}

static inline void spin_time_accum_blocked(u64 start)
{
}
#endif  /* CONFIG_KVM_DEBUG_FS */

struct kvm_lock_waiting {
	struct arch_spinlock *lock;
	__ticket_t want;
};

/* cpus 'waiting' on a spinlock to become available */
static cpumask_t waiting_cpus;

/* Track spinlock on which a cpu is waiting */
static DEFINE_PER_CPU(struct kvm_lock_waiting, klock_waiting);

static void kvm_lock_spinning(struct arch_spinlock *lock, __ticket_t want)
{
	struct kvm_lock_waiting *w;
	int cpu;
	u64 start;
	unsigned long flags;

	if (in_nmi())
		return;

	w = &__get_cpu_var(klock_waiting);
	cpu = smp_processor_id();
	start = spin_time_start();

	/*
	 * Make sure an interrupt handler can't upset things in a
	 * partially setup state.
	 */
	local_irq_save(flags);

	/*
	 * The ordering protocol on this is that the "lock" pointer
	 * may only be set non-NULL if the "want" ticket is correct.
	 * If we're updating "want", we must first clear "lock".
	 */
	w->lock = NULL;
	smp_wmb();
	w->want = want;
	smp_wmb();
	w->lock = lock;

	add_stats(TAKEN_SLOW, 1);

	/*
	 * This uses set_bit, which is atomic but we should not rely on its
	 * reordering gurantees. So barrier is needed after this call.
	 */
	cpumask_set_cpu(cpu, &waiting_cpus);

	barrier();

	/*
	 * Mark entry to slowpath before doing the pickup test to make
	 * sure we don't deadlock with an unlocker.
	 */
	__ticket_enter_slowpath(lock);

	/*
	 * check again make sure it didn't become free while
	 * we weren't looking.
	 */
	if (ACCESS_ONCE(lock->tickets.head) == want) {
		add_stats(TAKEN_SLOW_PICKUP, 1);
		goto out;
	}

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
	cpumask_clear_cpu(cpu, &waiting_cpus);
	w->lock = NULL;
	local_irq_restore(flags);
	spin_time_accum_blocked(start);
}
PV_CALLEE_SAVE_REGS_THUNK(kvm_lock_spinning);

/* Kick vcpu waiting on @lock->head to reach value @ticket */
static void kvm_unlock_kick(struct arch_spinlock *lock, __ticket_t ticket)
{
	int cpu;

	add_stats(RELEASED_SLOW, 1);
	for_each_cpu(cpu, &waiting_cpus) {
		const struct kvm_lock_waiting *w = &per_cpu(klock_waiting, cpu);
		if (ACCESS_ONCE(w->lock) == lock &&
		    ACCESS_ONCE(w->want) == ticket) {
			add_stats(RELEASED_SLOW_KICKED, 1);
			kvm_kick_cpu(cpu);
			break;
		}
	}
}

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

	printk(KERN_INFO "KVM setup paravirtual spinlock\n");

	static_key_slow_inc(&paravirt_ticketlocks_enabled);

	pv_lock_ops.lock_spinning = PV_CALLEE_SAVE(kvm_lock_spinning);
	pv_lock_ops.unlock_kick = kvm_unlock_kick;
}
#endif	/* CONFIG_PARAVIRT_SPINLOCKS */
