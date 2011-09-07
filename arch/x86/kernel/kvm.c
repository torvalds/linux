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

#define MMU_QUEUE_SIZE 1024

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

struct kvm_para_state {
	u8 mmu_queue[MMU_QUEUE_SIZE];
	int mmu_queue_len;
};

static DEFINE_PER_CPU(struct kvm_para_state, para_state);
static DEFINE_PER_CPU(struct kvm_vcpu_pv_apf_data, apf_reason) __aligned(64);
static DEFINE_PER_CPU(struct kvm_steal_time, steal_time) __aligned(64);
static int has_steal_clock = 0;

static struct kvm_para_state *kvm_para_state(void)
{
	return &per_cpu(para_state, raw_smp_processor_id());
}

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
	struct mm_struct *mm;
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
	n.mm = current->active_mm;
	n.halted = idle || preempt_count() > 1;
	atomic_inc(&n.mm->mm_count);
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
	if (!n->mm)
		return;
	mmdrop(n->mm);
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
		n = kmalloc(sizeof(*n), GFP_ATOMIC);
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
		n->mm = NULL;
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
		kvm_async_pf_task_wait((u32)read_cr2());
		break;
	case KVM_PV_REASON_PAGE_READY:
		kvm_async_pf_task_wake((u32)read_cr2());
		break;
	}
}

static void kvm_mmu_op(void *buffer, unsigned len)
{
	int r;
	unsigned long a1, a2;

	do {
		a1 = __pa(buffer);
		a2 = 0;   /* on i386 __pa() always returns <4G */
		r = kvm_hypercall3(KVM_HC_MMU_OP, len, a1, a2);
		buffer += r;
		len -= r;
	} while (len);
}

static void mmu_queue_flush(struct kvm_para_state *state)
{
	if (state->mmu_queue_len) {
		kvm_mmu_op(state->mmu_queue, state->mmu_queue_len);
		state->mmu_queue_len = 0;
	}
}

static void kvm_deferred_mmu_op(void *buffer, int len)
{
	struct kvm_para_state *state = kvm_para_state();

	if (paravirt_get_lazy_mode() != PARAVIRT_LAZY_MMU) {
		kvm_mmu_op(buffer, len);
		return;
	}
	if (state->mmu_queue_len + len > sizeof state->mmu_queue)
		mmu_queue_flush(state);
	memcpy(state->mmu_queue + state->mmu_queue_len, buffer, len);
	state->mmu_queue_len += len;
}

static void kvm_mmu_write(void *dest, u64 val)
{
	__u64 pte_phys;
	struct kvm_mmu_op_write_pte wpte;

#ifdef CONFIG_HIGHPTE
	struct page *page;
	unsigned long dst = (unsigned long) dest;

	page = kmap_atomic_to_page(dest);
	pte_phys = page_to_pfn(page);
	pte_phys <<= PAGE_SHIFT;
	pte_phys += (dst & ~(PAGE_MASK));
#else
	pte_phys = (unsigned long)__pa(dest);
#endif
	wpte.header.op = KVM_MMU_OP_WRITE_PTE;
	wpte.pte_val = val;
	wpte.pte_phys = pte_phys;

	kvm_deferred_mmu_op(&wpte, sizeof wpte);
}

/*
 * We only need to hook operations that are MMU writes.  We hook these so that
 * we can use lazy MMU mode to batch these operations.  We could probably
 * improve the performance of the host code if we used some of the information
 * here to simplify processing of batched writes.
 */
static void kvm_set_pte(pte_t *ptep, pte_t pte)
{
	kvm_mmu_write(ptep, pte_val(pte));
}

static void kvm_set_pte_at(struct mm_struct *mm, unsigned long addr,
			   pte_t *ptep, pte_t pte)
{
	kvm_mmu_write(ptep, pte_val(pte));
}

static void kvm_set_pmd(pmd_t *pmdp, pmd_t pmd)
{
	kvm_mmu_write(pmdp, pmd_val(pmd));
}

#if PAGETABLE_LEVELS >= 3
#ifdef CONFIG_X86_PAE
static void kvm_set_pte_atomic(pte_t *ptep, pte_t pte)
{
	kvm_mmu_write(ptep, pte_val(pte));
}

static void kvm_pte_clear(struct mm_struct *mm,
			  unsigned long addr, pte_t *ptep)
{
	kvm_mmu_write(ptep, 0);
}

static void kvm_pmd_clear(pmd_t *pmdp)
{
	kvm_mmu_write(pmdp, 0);
}
#endif

static void kvm_set_pud(pud_t *pudp, pud_t pud)
{
	kvm_mmu_write(pudp, pud_val(pud));
}

#if PAGETABLE_LEVELS == 4
static void kvm_set_pgd(pgd_t *pgdp, pgd_t pgd)
{
	kvm_mmu_write(pgdp, pgd_val(pgd));
}
#endif
#endif /* PAGETABLE_LEVELS >= 3 */

static void kvm_flush_tlb(void)
{
	struct kvm_mmu_op_flush_tlb ftlb = {
		.header.op = KVM_MMU_OP_FLUSH_TLB,
	};

	kvm_deferred_mmu_op(&ftlb, sizeof ftlb);
}

static void kvm_release_pt(unsigned long pfn)
{
	struct kvm_mmu_op_release_pt rpt = {
		.header.op = KVM_MMU_OP_RELEASE_PT,
		.pt_phys = (u64)pfn << PAGE_SHIFT,
	};

	kvm_mmu_op(&rpt, sizeof rpt);
}

static void kvm_enter_lazy_mmu(void)
{
	paravirt_enter_lazy_mmu();
}

static void kvm_leave_lazy_mmu(void)
{
	struct kvm_para_state *state = kvm_para_state();

	mmu_queue_flush(state);
	paravirt_leave_lazy_mmu();
}

static void __init paravirt_ops_setup(void)
{
	pv_info.name = "KVM";
	pv_info.paravirt_enabled = 1;

	if (kvm_para_has_feature(KVM_FEATURE_NOP_IO_DELAY))
		pv_cpu_ops.io_delay = kvm_io_delay;

	if (kvm_para_has_feature(KVM_FEATURE_MMU_OP)) {
		pv_mmu_ops.set_pte = kvm_set_pte;
		pv_mmu_ops.set_pte_at = kvm_set_pte_at;
		pv_mmu_ops.set_pmd = kvm_set_pmd;
#if PAGETABLE_LEVELS >= 3
#ifdef CONFIG_X86_PAE
		pv_mmu_ops.set_pte_atomic = kvm_set_pte_atomic;
		pv_mmu_ops.pte_clear = kvm_pte_clear;
		pv_mmu_ops.pmd_clear = kvm_pmd_clear;
#endif
		pv_mmu_ops.set_pud = kvm_set_pud;
#if PAGETABLE_LEVELS == 4
		pv_mmu_ops.set_pgd = kvm_set_pgd;
#endif
#endif
		pv_mmu_ops.flush_tlb_user = kvm_flush_tlb;
		pv_mmu_ops.release_pte = kvm_release_pt;
		pv_mmu_ops.release_pmd = kvm_release_pt;
		pv_mmu_ops.release_pud = kvm_release_pt;

		pv_mmu_ops.lazy_mode.enter = kvm_enter_lazy_mmu;
		pv_mmu_ops.lazy_mode.leave = kvm_leave_lazy_mmu;
	}
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

	if (has_steal_clock)
		kvm_register_steal_time();
}

static void kvm_pv_disable_apf(void *unused)
{
	if (!__get_cpu_var(apf_reason).enabled)
		return;

	wrmsrl(MSR_KVM_ASYNC_PF_EN, 0);
	__get_cpu_var(apf_reason).enabled = 0;

	printk(KERN_INFO"Unregister pv shared memory for cpu %d\n",
	       smp_processor_id());
}

static int kvm_pv_reboot_notify(struct notifier_block *nb,
				unsigned long code, void *unused)
{
	if (code == SYS_RESTART)
		on_each_cpu(kvm_pv_disable_apf, NULL, 1);
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
#ifdef CONFIG_KVM_CLOCK
	WARN_ON(kvm_register_clock("primary cpu clock"));
#endif
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
	kvm_pv_disable_apf(NULL);
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

#ifdef CONFIG_SMP
	smp_ops.smp_prepare_boot_cpu = kvm_smp_prepare_boot_cpu;
	register_cpu_notifier(&kvm_cpu_notifier);
#else
	kvm_guest_cpu_init();
#endif
}

static __init int activate_jump_labels(void)
{
	if (has_steal_clock) {
		jump_label_inc(&paravirt_steal_enabled);
		if (steal_acc)
			jump_label_inc(&paravirt_steal_rq_enabled);
	}

	return 0;
}
arch_initcall(activate_jump_labels);
