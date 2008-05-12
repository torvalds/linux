/* Support for MMIO probes.
 * Benfit many code from kprobes
 * (C) 2002 Louis Zhuang <louis.zhuang@intel.com>.
 *     2007 Alexander Eichner
 *     2008 Pekka Paalanen <pq@iki.fi>
 */

#include <linux/version.h>
#include <linux/spinlock.h>
#include <linux/hash.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/ptrace.h>
#include <linux/preempt.h>
#include <asm/io.h>
#include <asm/cacheflush.h>
#include <asm/errno.h>
#include <asm/tlbflush.h>
#include <asm/pgtable.h>

#include "kmmio.h"

#define KMMIO_HASH_BITS 6
#define KMMIO_TABLE_SIZE (1 << KMMIO_HASH_BITS)
#define KMMIO_PAGE_HASH_BITS 4
#define KMMIO_PAGE_TABLE_SIZE (1 << KMMIO_PAGE_HASH_BITS)

struct kmmio_context {
	struct kmmio_fault_page *fpage;
	struct kmmio_probe *probe;
	unsigned long saved_flags;
	int active;
};

static int kmmio_page_fault(struct pt_regs *regs, unsigned long error_code,
						unsigned long address);
static int kmmio_die_notifier(struct notifier_block *nb, unsigned long val,
								void *args);

static DEFINE_SPINLOCK(kmmio_lock);

/* These are protected by kmmio_lock */
unsigned int kmmio_count;
static unsigned int handler_registered;
static struct list_head kmmio_page_table[KMMIO_PAGE_TABLE_SIZE];
static LIST_HEAD(kmmio_probes);

static struct kmmio_context kmmio_ctx[NR_CPUS];

static struct pf_handler kmmio_pf_hook = {
	.handler = kmmio_page_fault
};

static struct notifier_block nb_die = {
	.notifier_call = kmmio_die_notifier
};

int init_kmmio(void)
{
	int i;
	for (i = 0; i < KMMIO_PAGE_TABLE_SIZE; i++)
		INIT_LIST_HEAD(&kmmio_page_table[i]);

	register_die_notifier(&nb_die);
	return 0;
}

void cleanup_kmmio(void)
{
	/*
	 * Assume the following have been already cleaned by calling
	 * unregister_kmmio_probe() appropriately:
	 * kmmio_page_table, kmmio_probes
	 */
	if (handler_registered) {
		unregister_page_fault_handler(&kmmio_pf_hook);
		synchronize_rcu();
	}
	unregister_die_notifier(&nb_die);
}

/*
 * this is basically a dynamic stabbing problem:
 * Could use the existing prio tree code or
 * Possible better implementations:
 * The Interval Skip List: A Data Structure for Finding All Intervals That
 * Overlap a Point (might be simple)
 * Space Efficient Dynamic Stabbing with Fast Queries - Mikkel Thorup
 */
/* Get the kmmio at this addr (if any). You must be holding kmmio_lock. */
static struct kmmio_probe *get_kmmio_probe(unsigned long addr)
{
	struct kmmio_probe *p;
	list_for_each_entry(p, &kmmio_probes, list) {
		if (addr >= p->addr && addr <= (p->addr + p->len))
			return p;
	}
	return NULL;
}

static struct kmmio_fault_page *get_kmmio_fault_page(unsigned long page)
{
	struct list_head *head, *tmp;

	page &= PAGE_MASK;
	head = &kmmio_page_table[hash_long(page, KMMIO_PAGE_HASH_BITS)];
	list_for_each(tmp, head) {
		struct kmmio_fault_page *p
			= list_entry(tmp, struct kmmio_fault_page, list);
		if (p->page == page)
			return p;
	}

	return NULL;
}

static void arm_kmmio_fault_page(unsigned long page, int *page_level)
{
	unsigned long address = page & PAGE_MASK;
	int level;
	pte_t *pte = lookup_address(address, &level);

	if (!pte) {
		printk(KERN_ERR "Error in %s: no pte for page 0x%08lx\n",
						__FUNCTION__, page);
		return;
	}

	if (level == PG_LEVEL_2M) {
		pmd_t *pmd = (pmd_t *)pte;
		set_pmd(pmd, __pmd(pmd_val(*pmd) & ~_PAGE_PRESENT));
	} else {
		/* PG_LEVEL_4K */
		set_pte(pte, __pte(pte_val(*pte) & ~_PAGE_PRESENT));
	}

	if (page_level)
		*page_level = level;

	__flush_tlb_one(page);
}

static void disarm_kmmio_fault_page(unsigned long page, int *page_level)
{
	unsigned long address = page & PAGE_MASK;
	int level;
	pte_t *pte = lookup_address(address, &level);

	if (!pte) {
		printk(KERN_ERR "Error in %s: no pte for page 0x%08lx\n",
						__FUNCTION__, page);
		return;
	}

	if (level == PG_LEVEL_2M) {
		pmd_t *pmd = (pmd_t *)pte;
		set_pmd(pmd, __pmd(pmd_val(*pmd) | _PAGE_PRESENT));
	} else {
		/* PG_LEVEL_4K */
		set_pte(pte, __pte(pte_val(*pte) | _PAGE_PRESENT));
	}

	if (page_level)
		*page_level = level;

	__flush_tlb_one(page);
}

/*
 * Interrupts are disabled on entry as trap3 is an interrupt gate
 * and they remain disabled thorough out this function.
 */
static int kmmio_handler(struct pt_regs *regs, unsigned long addr)
{
	struct kmmio_context *ctx;
	int cpu;

	/*
	 * Preemption is now disabled to prevent process switch during
	 * single stepping. We can only handle one active kmmio trace
	 * per cpu, so ensure that we finish it before something else
	 * gets to run.
	 *
	 * XXX what if an interrupt occurs between returning from
	 * do_page_fault() and entering the single-step exception handler?
	 * And that interrupt triggers a kmmio trap?
	 */
	preempt_disable();
	cpu = smp_processor_id();
	ctx = &kmmio_ctx[cpu];

	/* interrupts disabled and CPU-local data => atomicity guaranteed. */
	if (ctx->active) {
		/*
		 * This avoids a deadlock with kmmio_lock.
		 * If this page fault really was due to kmmio trap,
		 * all hell breaks loose.
		 */
		printk(KERN_EMERG "mmiotrace: recursive probe hit on CPU %d, "
					"for address %lu. Ignoring.\n",
					cpu, addr);
		goto no_kmmio;
	}
	ctx->active++;

	/*
	 * Acquire the kmmio lock to prevent changes affecting
	 * get_kmmio_fault_page() and get_kmmio_probe(), since we save their
	 * returned pointers.
	 * The lock is released in post_kmmio_handler().
	 * XXX: could/should get_kmmio_*() be using RCU instead of spinlock?
	 */
	spin_lock(&kmmio_lock);

	ctx->fpage = get_kmmio_fault_page(addr);
	if (!ctx->fpage) {
		/* this page fault is not caused by kmmio */
		goto no_kmmio_locked;
	}

	ctx->probe = get_kmmio_probe(addr);
	ctx->saved_flags = (regs->flags & (TF_MASK|IF_MASK));

	if (ctx->probe && ctx->probe->pre_handler)
		ctx->probe->pre_handler(ctx->probe, regs, addr);

	regs->flags |= TF_MASK;
	regs->flags &= ~IF_MASK;

	/* We hold lock, now we set present bit in PTE and single step. */
	disarm_kmmio_fault_page(ctx->fpage->page, NULL);

	return 1;

no_kmmio_locked:
	spin_unlock(&kmmio_lock);
	ctx->active--;
no_kmmio:
	preempt_enable_no_resched();
	/* page fault not handled by kmmio */
	return 0;
}

/*
 * Interrupts are disabled on entry as trap1 is an interrupt gate
 * and they remain disabled thorough out this function.
 * And we hold kmmio lock.
 */
static int post_kmmio_handler(unsigned long condition, struct pt_regs *regs)
{
	int cpu = smp_processor_id();
	struct kmmio_context *ctx = &kmmio_ctx[cpu];

	if (!ctx->active)
		return 0;

	if (ctx->probe && ctx->probe->post_handler)
		ctx->probe->post_handler(ctx->probe, condition, regs);

	arm_kmmio_fault_page(ctx->fpage->page, NULL);

	regs->flags &= ~TF_MASK;
	regs->flags |= ctx->saved_flags;

	/* These were acquired in kmmio_handler(). */
	ctx->active--;
	spin_unlock(&kmmio_lock);
	preempt_enable_no_resched();

	/*
	 * if somebody else is singlestepping across a probe point, flags
	 * will have TF set, in which case, continue the remaining processing
	 * of do_debug, as if this is not a probe hit.
	 */
	if (regs->flags & TF_MASK)
		return 0;

	return 1;
}

static int add_kmmio_fault_page(unsigned long page)
{
	struct kmmio_fault_page *f;

	page &= PAGE_MASK;
	f = get_kmmio_fault_page(page);
	if (f) {
		f->count++;
		return 0;
	}

	f = kmalloc(sizeof(*f), GFP_ATOMIC);
	if (!f)
		return -1;

	f->count = 1;
	f->page = page;
	list_add(&f->list,
		 &kmmio_page_table[hash_long(f->page, KMMIO_PAGE_HASH_BITS)]);

	arm_kmmio_fault_page(f->page, NULL);

	return 0;
}

static void release_kmmio_fault_page(unsigned long page)
{
	struct kmmio_fault_page *f;

	page &= PAGE_MASK;
	f = get_kmmio_fault_page(page);
	if (!f)
		return;

	f->count--;
	if (!f->count) {
		disarm_kmmio_fault_page(f->page, NULL);
		list_del(&f->list);
	}
}

int register_kmmio_probe(struct kmmio_probe *p)
{
	int ret = 0;
	unsigned long size = 0;

	spin_lock_irq(&kmmio_lock);
	kmmio_count++;
	if (get_kmmio_probe(p->addr)) {
		ret = -EEXIST;
		goto out;
	}
	list_add(&p->list, &kmmio_probes);
	/*printk("adding fault pages...\n");*/
	while (size < p->len) {
		if (add_kmmio_fault_page(p->addr + size))
			printk(KERN_ERR "mmio: Unable to set page fault.\n");
		size += PAGE_SIZE;
	}

	if (!handler_registered) {
		register_page_fault_handler(&kmmio_pf_hook);
		handler_registered++;
	}

out:
	spin_unlock_irq(&kmmio_lock);
	/*
	 * XXX: What should I do here?
	 * Here was a call to global_flush_tlb(), but it does not exist
	 * anymore.
	 */
	return ret;
}

void unregister_kmmio_probe(struct kmmio_probe *p)
{
	unsigned long size = 0;

	spin_lock_irq(&kmmio_lock);
	while (size < p->len) {
		release_kmmio_fault_page(p->addr + size);
		size += PAGE_SIZE;
	}
	list_del(&p->list);
	kmmio_count--;
	spin_unlock_irq(&kmmio_lock);
}

/*
 * According to 2.6.20, mainly x86_64 arch:
 * This is being called from do_page_fault(), via the page fault notifier
 * chain. The chain is called for both user space faults and kernel space
 * faults (address >= TASK_SIZE64), except not on faults serviced by
 * vmalloc_fault().
 *
 * We may be in an interrupt or a critical section. Also prefecthing may
 * trigger a page fault. We may be in the middle of process switch.
 * The page fault hook functionality has put us inside RCU read lock.
 *
 * Local interrupts are disabled, so preemption cannot happen.
 * Do not enable interrupts, do not sleep, and watch out for other CPUs.
 */
static int kmmio_page_fault(struct pt_regs *regs, unsigned long error_code,
						unsigned long address)
{
	if (is_kmmio_active())
		if (kmmio_handler(regs, address) == 1)
			return -1;
	return 0;
}

static int kmmio_die_notifier(struct notifier_block *nb, unsigned long val,
								void *args)
{
	struct die_args *arg = args;

	if (val == DIE_DEBUG)
		if (post_kmmio_handler(arg->err, arg->regs) == 1)
			return NOTIFY_STOP;

	return NOTIFY_DONE;
}
