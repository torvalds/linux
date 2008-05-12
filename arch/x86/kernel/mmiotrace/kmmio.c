/* Support for MMIO probes.
 * Benfit many code from kprobes
 * (C) 2002 Louis Zhuang <louis.zhuang@intel.com>.
 *     2007 Alexander Eichner
 *     2008 Pekka Paalanen <pq@iki.fi>
 */

#include <linux/version.h>
#include <linux/list.h>
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
#include <linux/percpu.h>
#include <linux/kdebug.h>
#include <asm/io.h>
#include <asm/cacheflush.h>
#include <asm/errno.h>
#include <asm/tlbflush.h>
#include <asm/pgtable.h>

#include <linux/mmiotrace.h>

#define KMMIO_PAGE_HASH_BITS 4
#define KMMIO_PAGE_TABLE_SIZE (1 << KMMIO_PAGE_HASH_BITS)

struct kmmio_fault_page {
	struct list_head list;
	struct kmmio_fault_page *release_next;
	unsigned long page; /* location of the fault page */

	/*
	 * Number of times this page has been registered as a part
	 * of a probe. If zero, page is disarmed and this may be freed.
	 * Used only by writers (RCU).
	 */
	int count;
};

struct kmmio_delayed_release {
	struct rcu_head rcu;
	struct kmmio_fault_page *release_list;
};

struct kmmio_context {
	struct kmmio_fault_page *fpage;
	struct kmmio_probe *probe;
	unsigned long saved_flags;
	unsigned long addr;
	int active;
};

static int kmmio_die_notifier(struct notifier_block *nb, unsigned long val,
								void *args);

static DECLARE_MUTEX(kmmio_init_mutex);
static DEFINE_SPINLOCK(kmmio_lock);

/* These are protected by kmmio_lock */
static int kmmio_initialized;
unsigned int kmmio_count;

/* Read-protected by RCU, write-protected by kmmio_lock. */
static struct list_head kmmio_page_table[KMMIO_PAGE_TABLE_SIZE];
static LIST_HEAD(kmmio_probes);

static struct list_head *kmmio_page_list(unsigned long page)
{
	return &kmmio_page_table[hash_long(page, KMMIO_PAGE_HASH_BITS)];
}

/* Accessed per-cpu */
static DEFINE_PER_CPU(struct kmmio_context, kmmio_ctx);

/* protected by kmmio_init_mutex */
static struct notifier_block nb_die = {
	.notifier_call = kmmio_die_notifier
};

/**
 * Makes sure kmmio is initialized and usable.
 * This must be called before any other kmmio function defined here.
 * May sleep.
 */
void reference_kmmio(void)
{
	down(&kmmio_init_mutex);
	spin_lock_irq(&kmmio_lock);
	if (!kmmio_initialized) {
		int i;
		for (i = 0; i < KMMIO_PAGE_TABLE_SIZE; i++)
			INIT_LIST_HEAD(&kmmio_page_table[i]);
		if (register_die_notifier(&nb_die))
			BUG();
	}
	kmmio_initialized++;
	spin_unlock_irq(&kmmio_lock);
	up(&kmmio_init_mutex);
}
EXPORT_SYMBOL_GPL(reference_kmmio);

/**
 * Clean up kmmio after use. This must be called for every call to
 * reference_kmmio(). All probes registered after the corresponding
 * reference_kmmio() must have been unregistered when calling this.
 * May sleep.
 */
void unreference_kmmio(void)
{
	bool unreg = false;

	down(&kmmio_init_mutex);
	spin_lock_irq(&kmmio_lock);

	if (kmmio_initialized == 1) {
		BUG_ON(is_kmmio_active());
		unreg = true;
	}
	kmmio_initialized--;
	BUG_ON(kmmio_initialized < 0);
	spin_unlock_irq(&kmmio_lock);

	if (unreg)
		unregister_die_notifier(&nb_die); /* calls sync_rcu() */
	up(&kmmio_init_mutex);
}
EXPORT_SYMBOL(unreference_kmmio);

/*
 * this is basically a dynamic stabbing problem:
 * Could use the existing prio tree code or
 * Possible better implementations:
 * The Interval Skip List: A Data Structure for Finding All Intervals That
 * Overlap a Point (might be simple)
 * Space Efficient Dynamic Stabbing with Fast Queries - Mikkel Thorup
 */
/* Get the kmmio at this addr (if any). You must be holding RCU read lock. */
static struct kmmio_probe *get_kmmio_probe(unsigned long addr)
{
	struct kmmio_probe *p;
	list_for_each_entry_rcu(p, &kmmio_probes, list) {
		if (addr >= p->addr && addr <= (p->addr + p->len))
			return p;
	}
	return NULL;
}

/* You must be holding RCU read lock. */
static struct kmmio_fault_page *get_kmmio_fault_page(unsigned long page)
{
	struct list_head *head;
	struct kmmio_fault_page *p;

	page &= PAGE_MASK;
	head = kmmio_page_list(page);
	list_for_each_entry_rcu(p, head, list) {
		if (p->page == page)
			return p;
	}
	return NULL;
}

/** Mark the given page as not present. Access to it will trigger a fault. */
static void arm_kmmio_fault_page(unsigned long page, int *page_level)
{
	unsigned long address = page & PAGE_MASK;
	int level;
	pte_t *pte = lookup_address(address, &level);

	if (!pte) {
		pr_err("kmmio: Error in %s: no pte for page 0x%08lx\n",
							__func__, page);
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

/** Mark the given page as present. */
static void disarm_kmmio_fault_page(unsigned long page, int *page_level)
{
	unsigned long address = page & PAGE_MASK;
	int level;
	pte_t *pte = lookup_address(address, &level);

	if (!pte) {
		pr_err("kmmio: Error in %s: no pte for page 0x%08lx\n",
							__func__, page);
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
 * This is being called from do_page_fault().
 *
 * We may be in an interrupt or a critical section. Also prefecthing may
 * trigger a page fault. We may be in the middle of process switch.
 * We cannot take any locks, because we could be executing especially
 * within a kmmio critical section.
 *
 * Local interrupts are disabled, so preemption cannot happen.
 * Do not enable interrupts, do not sleep, and watch out for other CPUs.
 */
/*
 * Interrupts are disabled on entry as trap3 is an interrupt gate
 * and they remain disabled thorough out this function.
 */
int kmmio_handler(struct pt_regs *regs, unsigned long addr)
{
	struct kmmio_context *ctx;
	struct kmmio_fault_page *faultpage;

	/*
	 * Preemption is now disabled to prevent process switch during
	 * single stepping. We can only handle one active kmmio trace
	 * per cpu, so ensure that we finish it before something else
	 * gets to run.
	 *
	 * XXX what if an interrupt occurs between returning from
	 * do_page_fault() and entering the single-step exception handler?
	 * And that interrupt triggers a kmmio trap?
	 * XXX If we tracing an interrupt service routine or whatever, is
	 * this enough to keep it on the current cpu?
	 */
	preempt_disable();

	rcu_read_lock();
	faultpage = get_kmmio_fault_page(addr);
	if (!faultpage) {
		/*
		 * Either this page fault is not caused by kmmio, or
		 * another CPU just pulled the kmmio probe from under
		 * our feet. In the latter case all hell breaks loose.
		 */
		goto no_kmmio;
	}

	ctx = &get_cpu_var(kmmio_ctx);
	if (ctx->active) {
		/*
		 * Prevent overwriting already in-flight context.
		 * If this page fault really was due to kmmio trap,
		 * all hell breaks loose.
		 */
		pr_emerg("kmmio: recursive probe hit on CPU %d, "
					"for address 0x%08lx. Ignoring.\n",
					smp_processor_id(), addr);
		goto no_kmmio_ctx;
	}
	ctx->active++;

	ctx->fpage = faultpage;
	ctx->probe = get_kmmio_probe(addr);
	ctx->saved_flags = (regs->flags & (TF_MASK|IF_MASK));
	ctx->addr = addr;

	if (ctx->probe && ctx->probe->pre_handler)
		ctx->probe->pre_handler(ctx->probe, regs, addr);

	regs->flags |= TF_MASK;
	regs->flags &= ~IF_MASK;

	/* Now we set present bit in PTE and single step. */
	disarm_kmmio_fault_page(ctx->fpage->page, NULL);

	put_cpu_var(kmmio_ctx);
	rcu_read_unlock();
	return 1;

no_kmmio_ctx:
	put_cpu_var(kmmio_ctx);
no_kmmio:
	rcu_read_unlock();
	preempt_enable_no_resched();
	return 0; /* page fault not handled by kmmio */
}

/*
 * Interrupts are disabled on entry as trap1 is an interrupt gate
 * and they remain disabled thorough out this function.
 * This must always get called as the pair to kmmio_handler().
 */
static int post_kmmio_handler(unsigned long condition, struct pt_regs *regs)
{
	int ret = 0;
	struct kmmio_probe *probe;
	struct kmmio_fault_page *faultpage;
	struct kmmio_context *ctx = &get_cpu_var(kmmio_ctx);

	if (!ctx->active)
		goto out;

	rcu_read_lock();

	faultpage = get_kmmio_fault_page(ctx->addr);
	probe = get_kmmio_probe(ctx->addr);
	if (faultpage != ctx->fpage || probe != ctx->probe) {
		/*
		 * The trace setup changed after kmmio_handler() and before
		 * running this respective post handler. User does not want
		 * the result anymore.
		 */
		ctx->probe = NULL;
		ctx->fpage = NULL;
	}

	if (ctx->probe && ctx->probe->post_handler)
		ctx->probe->post_handler(ctx->probe, condition, regs);

	if (ctx->fpage)
		arm_kmmio_fault_page(ctx->fpage->page, NULL);

	regs->flags &= ~TF_MASK;
	regs->flags |= ctx->saved_flags;

	/* These were acquired in kmmio_handler(). */
	ctx->active--;
	BUG_ON(ctx->active);
	preempt_enable_no_resched();

	/*
	 * if somebody else is singlestepping across a probe point, flags
	 * will have TF set, in which case, continue the remaining processing
	 * of do_debug, as if this is not a probe hit.
	 */
	if (!(regs->flags & TF_MASK))
		ret = 1;

	rcu_read_unlock();
out:
	put_cpu_var(kmmio_ctx);
	return ret;
}

/* You must be holding kmmio_lock. */
static int add_kmmio_fault_page(unsigned long page)
{
	struct kmmio_fault_page *f;

	page &= PAGE_MASK;
	f = get_kmmio_fault_page(page);
	if (f) {
		if (!f->count)
			arm_kmmio_fault_page(f->page, NULL);
		f->count++;
		return 0;
	}

	f = kmalloc(sizeof(*f), GFP_ATOMIC);
	if (!f)
		return -1;

	f->count = 1;
	f->page = page;
	list_add_rcu(&f->list, kmmio_page_list(f->page));

	arm_kmmio_fault_page(f->page, NULL);

	return 0;
}

/* You must be holding kmmio_lock. */
static void release_kmmio_fault_page(unsigned long page,
				struct kmmio_fault_page **release_list)
{
	struct kmmio_fault_page *f;

	page &= PAGE_MASK;
	f = get_kmmio_fault_page(page);
	if (!f)
		return;

	f->count--;
	BUG_ON(f->count < 0);
	if (!f->count) {
		disarm_kmmio_fault_page(f->page, NULL);
		f->release_next = *release_list;
		*release_list = f;
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
	list_add_rcu(&p->list, &kmmio_probes);
	while (size < p->len) {
		if (add_kmmio_fault_page(p->addr + size))
			pr_err("kmmio: Unable to set page fault.\n");
		size += PAGE_SIZE;
	}
out:
	spin_unlock_irq(&kmmio_lock);
	/*
	 * XXX: What should I do here?
	 * Here was a call to global_flush_tlb(), but it does not exist
	 * anymore. It seems it's not needed after all.
	 */
	return ret;
}
EXPORT_SYMBOL(register_kmmio_probe);

static void rcu_free_kmmio_fault_pages(struct rcu_head *head)
{
	struct kmmio_delayed_release *dr = container_of(
						head,
						struct kmmio_delayed_release,
						rcu);
	struct kmmio_fault_page *p = dr->release_list;
	while (p) {
		struct kmmio_fault_page *next = p->release_next;
		BUG_ON(p->count);
		kfree(p);
		p = next;
	}
	kfree(dr);
}

static void remove_kmmio_fault_pages(struct rcu_head *head)
{
	struct kmmio_delayed_release *dr = container_of(
						head,
						struct kmmio_delayed_release,
						rcu);
	struct kmmio_fault_page *p = dr->release_list;
	struct kmmio_fault_page **prevp = &dr->release_list;
	unsigned long flags;
	spin_lock_irqsave(&kmmio_lock, flags);
	while (p) {
		if (!p->count)
			list_del_rcu(&p->list);
		else
			*prevp = p->release_next;
		prevp = &p->release_next;
		p = p->release_next;
	}
	spin_unlock_irqrestore(&kmmio_lock, flags);
	/* This is the real RCU destroy call. */
	call_rcu(&dr->rcu, rcu_free_kmmio_fault_pages);
}

/*
 * Remove a kmmio probe. You have to synchronize_rcu() before you can be
 * sure that the callbacks will not be called anymore.
 *
 * Unregistering a kmmio fault page has three steps:
 * 1. release_kmmio_fault_page()
 *    Disarm the page, wait a grace period to let all faults finish.
 * 2. remove_kmmio_fault_pages()
 *    Remove the pages from kmmio_page_table.
 * 3. rcu_free_kmmio_fault_pages()
 *    Actally free the kmmio_fault_page structs as with RCU.
 */
void unregister_kmmio_probe(struct kmmio_probe *p)
{
	unsigned long size = 0;
	struct kmmio_fault_page *release_list = NULL;
	struct kmmio_delayed_release *drelease;

	spin_lock_irq(&kmmio_lock);
	while (size < p->len) {
		release_kmmio_fault_page(p->addr + size, &release_list);
		size += PAGE_SIZE;
	}
	list_del_rcu(&p->list);
	kmmio_count--;
	spin_unlock_irq(&kmmio_lock);

	drelease = kmalloc(sizeof(*drelease), GFP_ATOMIC);
	if (!drelease) {
		pr_crit("kmmio: leaking kmmio_fault_page objects.\n");
		return;
	}
	drelease->release_list = release_list;

	/*
	 * This is not really RCU here. We have just disarmed a set of
	 * pages so that they cannot trigger page faults anymore. However,
	 * we cannot remove the pages from kmmio_page_table,
	 * because a probe hit might be in flight on another CPU. The
	 * pages are collected into a list, and they will be removed from
	 * kmmio_page_table when it is certain that no probe hit related to
	 * these pages can be in flight. RCU grace period sounds like a
	 * good choice.
	 *
	 * If we removed the pages too early, kmmio page fault handler might
	 * not find the respective kmmio_fault_page and determine it's not
	 * a kmmio fault, when it actually is. This would lead to madness.
	 */
	call_rcu(&drelease->rcu, remove_kmmio_fault_pages);
}
EXPORT_SYMBOL(unregister_kmmio_probe);

static int kmmio_die_notifier(struct notifier_block *nb, unsigned long val,
								void *args)
{
	struct die_args *arg = args;

	if (val == DIE_DEBUG)
		if (post_kmmio_handler(arg->err, arg->regs) == 1)
			return NOTIFY_STOP;

	return NOTIFY_DONE;
}
