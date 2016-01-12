/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002 Andi Kleen, SuSE Labs
 *  Copyright (C) 2011	Don Zickus Red Hat, Inc.
 *
 *  Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 */

/*
 * Handle hardware traps and faults.
 */
#include <linux/spinlock.h>
#include <linux/kprobes.h>
#include <linux/kdebug.h>
#include <linux/nmi.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/hardirq.h>
#include <linux/slab.h>
#include <linux/export.h>

#if defined(CONFIG_EDAC)
#include <linux/edac.h>
#endif

#include <linux/atomic.h>
#include <asm/traps.h>
#include <asm/mach_traps.h>
#include <asm/nmi.h>
#include <asm/x86_init.h>
#include <asm/reboot.h>

#define CREATE_TRACE_POINTS
#include <trace/events/nmi.h>

struct nmi_desc {
	spinlock_t lock;
	struct list_head head;
};

static struct nmi_desc nmi_desc[NMI_MAX] = 
{
	{
		.lock = __SPIN_LOCK_UNLOCKED(&nmi_desc[0].lock),
		.head = LIST_HEAD_INIT(nmi_desc[0].head),
	},
	{
		.lock = __SPIN_LOCK_UNLOCKED(&nmi_desc[1].lock),
		.head = LIST_HEAD_INIT(nmi_desc[1].head),
	},
	{
		.lock = __SPIN_LOCK_UNLOCKED(&nmi_desc[2].lock),
		.head = LIST_HEAD_INIT(nmi_desc[2].head),
	},
	{
		.lock = __SPIN_LOCK_UNLOCKED(&nmi_desc[3].lock),
		.head = LIST_HEAD_INIT(nmi_desc[3].head),
	},

};

struct nmi_stats {
	unsigned int normal;
	unsigned int unknown;
	unsigned int external;
	unsigned int swallow;
};

static DEFINE_PER_CPU(struct nmi_stats, nmi_stats);

static int ignore_nmis;

int unknown_nmi_panic;
/*
 * Prevent NMI reason port (0x61) being accessed simultaneously, can
 * only be used in NMI handler.
 */
static DEFINE_RAW_SPINLOCK(nmi_reason_lock);

static int __init setup_unknown_nmi_panic(char *str)
{
	unknown_nmi_panic = 1;
	return 1;
}
__setup("unknown_nmi_panic", setup_unknown_nmi_panic);

#define nmi_to_desc(type) (&nmi_desc[type])

static u64 nmi_longest_ns = 1 * NSEC_PER_MSEC;

static int __init nmi_warning_debugfs(void)
{
	debugfs_create_u64("nmi_longest_ns", 0644,
			arch_debugfs_dir, &nmi_longest_ns);
	return 0;
}
fs_initcall(nmi_warning_debugfs);

static void nmi_max_handler(struct irq_work *w)
{
	struct nmiaction *a = container_of(w, struct nmiaction, irq_work);
	int remainder_ns, decimal_msecs;
	u64 whole_msecs = ACCESS_ONCE(a->max_duration);

	remainder_ns = do_div(whole_msecs, (1000 * 1000));
	decimal_msecs = remainder_ns / 1000;

	printk_ratelimited(KERN_INFO
		"INFO: NMI handler (%ps) took too long to run: %lld.%03d msecs\n",
		a->handler, whole_msecs, decimal_msecs);
}

static int nmi_handle(unsigned int type, struct pt_regs *regs)
{
	struct nmi_desc *desc = nmi_to_desc(type);
	struct nmiaction *a;
	int handled=0;

	rcu_read_lock();

	/*
	 * NMIs are edge-triggered, which means if you have enough
	 * of them concurrently, you can lose some because only one
	 * can be latched at any given time.  Walk the whole list
	 * to handle those situations.
	 */
	list_for_each_entry_rcu(a, &desc->head, list) {
		int thishandled;
		u64 delta;

		delta = sched_clock();
		thishandled = a->handler(type, regs);
		handled += thishandled;
		delta = sched_clock() - delta;
		trace_nmi_handler(a->handler, (int)delta, thishandled);

		if (delta < nmi_longest_ns || delta < a->max_duration)
			continue;

		a->max_duration = delta;
		irq_work_queue(&a->irq_work);
	}

	rcu_read_unlock();

	/* return total number of NMI events handled */
	return handled;
}
NOKPROBE_SYMBOL(nmi_handle);

int __register_nmi_handler(unsigned int type, struct nmiaction *action)
{
	struct nmi_desc *desc = nmi_to_desc(type);
	unsigned long flags;

	if (!action->handler)
		return -EINVAL;

	init_irq_work(&action->irq_work, nmi_max_handler);

	spin_lock_irqsave(&desc->lock, flags);

	/*
	 * most handlers of type NMI_UNKNOWN never return because
	 * they just assume the NMI is theirs.  Just a sanity check
	 * to manage expectations
	 */
	WARN_ON_ONCE(type == NMI_UNKNOWN && !list_empty(&desc->head));
	WARN_ON_ONCE(type == NMI_SERR && !list_empty(&desc->head));
	WARN_ON_ONCE(type == NMI_IO_CHECK && !list_empty(&desc->head));

	/*
	 * some handlers need to be executed first otherwise a fake
	 * event confuses some handlers (kdump uses this flag)
	 */
	if (action->flags & NMI_FLAG_FIRST)
		list_add_rcu(&action->list, &desc->head);
	else
		list_add_tail_rcu(&action->list, &desc->head);
	
	spin_unlock_irqrestore(&desc->lock, flags);
	return 0;
}
EXPORT_SYMBOL(__register_nmi_handler);

void unregister_nmi_handler(unsigned int type, const char *name)
{
	struct nmi_desc *desc = nmi_to_desc(type);
	struct nmiaction *n;
	unsigned long flags;

	spin_lock_irqsave(&desc->lock, flags);

	list_for_each_entry_rcu(n, &desc->head, list) {
		/*
		 * the name passed in to describe the nmi handler
		 * is used as the lookup key
		 */
		if (!strcmp(n->name, name)) {
			WARN(in_nmi(),
				"Trying to free NMI (%s) from NMI context!\n", n->name);
			list_del_rcu(&n->list);
			break;
		}
	}

	spin_unlock_irqrestore(&desc->lock, flags);
	synchronize_rcu();
}
EXPORT_SYMBOL_GPL(unregister_nmi_handler);

static void
pci_serr_error(unsigned char reason, struct pt_regs *regs)
{
	/* check to see if anyone registered against these types of errors */
	if (nmi_handle(NMI_SERR, regs))
		return;

	pr_emerg("NMI: PCI system error (SERR) for reason %02x on CPU %d.\n",
		 reason, smp_processor_id());

	/*
	 * On some machines, PCI SERR line is used to report memory
	 * errors. EDAC makes use of it.
	 */
#if defined(CONFIG_EDAC)
	if (edac_handler_set()) {
		edac_atomic_assert_error();
		return;
	}
#endif

	if (panic_on_unrecovered_nmi)
		nmi_panic(regs, "NMI: Not continuing");

	pr_emerg("Dazed and confused, but trying to continue\n");

	/* Clear and disable the PCI SERR error line. */
	reason = (reason & NMI_REASON_CLEAR_MASK) | NMI_REASON_CLEAR_SERR;
	outb(reason, NMI_REASON_PORT);
}
NOKPROBE_SYMBOL(pci_serr_error);

static void
io_check_error(unsigned char reason, struct pt_regs *regs)
{
	unsigned long i;

	/* check to see if anyone registered against these types of errors */
	if (nmi_handle(NMI_IO_CHECK, regs))
		return;

	pr_emerg(
	"NMI: IOCK error (debug interrupt?) for reason %02x on CPU %d.\n",
		 reason, smp_processor_id());
	show_regs(regs);

	if (panic_on_io_nmi) {
		nmi_panic(regs, "NMI IOCK error: Not continuing");

		/*
		 * If we end up here, it means we have received an NMI while
		 * processing panic(). Simply return without delaying and
		 * re-enabling NMIs.
		 */
		return;
	}

	/* Re-enable the IOCK line, wait for a few seconds */
	reason = (reason & NMI_REASON_CLEAR_MASK) | NMI_REASON_CLEAR_IOCHK;
	outb(reason, NMI_REASON_PORT);

	i = 20000;
	while (--i) {
		touch_nmi_watchdog();
		udelay(100);
	}

	reason &= ~NMI_REASON_CLEAR_IOCHK;
	outb(reason, NMI_REASON_PORT);
}
NOKPROBE_SYMBOL(io_check_error);

static void
unknown_nmi_error(unsigned char reason, struct pt_regs *regs)
{
	int handled;

	/*
	 * Use 'false' as back-to-back NMIs are dealt with one level up.
	 * Of course this makes having multiple 'unknown' handlers useless
	 * as only the first one is ever run (unless it can actually determine
	 * if it caused the NMI)
	 */
	handled = nmi_handle(NMI_UNKNOWN, regs);
	if (handled) {
		__this_cpu_add(nmi_stats.unknown, handled);
		return;
	}

	__this_cpu_add(nmi_stats.unknown, 1);

	pr_emerg("Uhhuh. NMI received for unknown reason %02x on CPU %d.\n",
		 reason, smp_processor_id());

	pr_emerg("Do you have a strange power saving mode enabled?\n");
	if (unknown_nmi_panic || panic_on_unrecovered_nmi)
		nmi_panic(regs, "NMI: Not continuing");

	pr_emerg("Dazed and confused, but trying to continue\n");
}
NOKPROBE_SYMBOL(unknown_nmi_error);

static DEFINE_PER_CPU(bool, swallow_nmi);
static DEFINE_PER_CPU(unsigned long, last_nmi_rip);

static void default_do_nmi(struct pt_regs *regs)
{
	unsigned char reason = 0;
	int handled;
	bool b2b = false;

	/*
	 * CPU-specific NMI must be processed before non-CPU-specific
	 * NMI, otherwise we may lose it, because the CPU-specific
	 * NMI can not be detected/processed on other CPUs.
	 */

	/*
	 * Back-to-back NMIs are interesting because they can either
	 * be two NMI or more than two NMIs (any thing over two is dropped
	 * due to NMI being edge-triggered).  If this is the second half
	 * of the back-to-back NMI, assume we dropped things and process
	 * more handlers.  Otherwise reset the 'swallow' NMI behaviour
	 */
	if (regs->ip == __this_cpu_read(last_nmi_rip))
		b2b = true;
	else
		__this_cpu_write(swallow_nmi, false);

	__this_cpu_write(last_nmi_rip, regs->ip);

	handled = nmi_handle(NMI_LOCAL, regs);
	__this_cpu_add(nmi_stats.normal, handled);
	if (handled) {
		/*
		 * There are cases when a NMI handler handles multiple
		 * events in the current NMI.  One of these events may
		 * be queued for in the next NMI.  Because the event is
		 * already handled, the next NMI will result in an unknown
		 * NMI.  Instead lets flag this for a potential NMI to
		 * swallow.
		 */
		if (handled > 1)
			__this_cpu_write(swallow_nmi, true);
		return;
	}

	/*
	 * Non-CPU-specific NMI: NMI sources can be processed on any CPU.
	 *
	 * Another CPU may be processing panic routines while holding
	 * nmi_reason_lock. Check if the CPU issued the IPI for crash dumping,
	 * and if so, call its callback directly.  If there is no CPU preparing
	 * crash dump, we simply loop here.
	 */
	while (!raw_spin_trylock(&nmi_reason_lock)) {
		run_crash_ipi_callback(regs);
		cpu_relax();
	}

	reason = x86_platform.get_nmi_reason();

	if (reason & NMI_REASON_MASK) {
		if (reason & NMI_REASON_SERR)
			pci_serr_error(reason, regs);
		else if (reason & NMI_REASON_IOCHK)
			io_check_error(reason, regs);
#ifdef CONFIG_X86_32
		/*
		 * Reassert NMI in case it became active
		 * meanwhile as it's edge-triggered:
		 */
		reassert_nmi();
#endif
		__this_cpu_add(nmi_stats.external, 1);
		raw_spin_unlock(&nmi_reason_lock);
		return;
	}
	raw_spin_unlock(&nmi_reason_lock);

	/*
	 * Only one NMI can be latched at a time.  To handle
	 * this we may process multiple nmi handlers at once to
	 * cover the case where an NMI is dropped.  The downside
	 * to this approach is we may process an NMI prematurely,
	 * while its real NMI is sitting latched.  This will cause
	 * an unknown NMI on the next run of the NMI processing.
	 *
	 * We tried to flag that condition above, by setting the
	 * swallow_nmi flag when we process more than one event.
	 * This condition is also only present on the second half
	 * of a back-to-back NMI, so we flag that condition too.
	 *
	 * If both are true, we assume we already processed this
	 * NMI previously and we swallow it.  Otherwise we reset
	 * the logic.
	 *
	 * There are scenarios where we may accidentally swallow
	 * a 'real' unknown NMI.  For example, while processing
	 * a perf NMI another perf NMI comes in along with a
	 * 'real' unknown NMI.  These two NMIs get combined into
	 * one (as descibed above).  When the next NMI gets
	 * processed, it will be flagged by perf as handled, but
	 * noone will know that there was a 'real' unknown NMI sent
	 * also.  As a result it gets swallowed.  Or if the first
	 * perf NMI returns two events handled then the second
	 * NMI will get eaten by the logic below, again losing a
	 * 'real' unknown NMI.  But this is the best we can do
	 * for now.
	 */
	if (b2b && __this_cpu_read(swallow_nmi))
		__this_cpu_add(nmi_stats.swallow, 1);
	else
		unknown_nmi_error(reason, regs);
}
NOKPROBE_SYMBOL(default_do_nmi);

/*
 * NMIs can page fault or hit breakpoints which will cause it to lose
 * its NMI context with the CPU when the breakpoint or page fault does an IRET.
 *
 * As a result, NMIs can nest if NMIs get unmasked due an IRET during
 * NMI processing.  On x86_64, the asm glue protects us from nested NMIs
 * if the outer NMI came from kernel mode, but we can still nest if the
 * outer NMI came from user mode.
 *
 * To handle these nested NMIs, we have three states:
 *
 *  1) not running
 *  2) executing
 *  3) latched
 *
 * When no NMI is in progress, it is in the "not running" state.
 * When an NMI comes in, it goes into the "executing" state.
 * Normally, if another NMI is triggered, it does not interrupt
 * the running NMI and the HW will simply latch it so that when
 * the first NMI finishes, it will restart the second NMI.
 * (Note, the latch is binary, thus multiple NMIs triggering,
 *  when one is running, are ignored. Only one NMI is restarted.)
 *
 * If an NMI executes an iret, another NMI can preempt it. We do not
 * want to allow this new NMI to run, but we want to execute it when the
 * first one finishes.  We set the state to "latched", and the exit of
 * the first NMI will perform a dec_return, if the result is zero
 * (NOT_RUNNING), then it will simply exit the NMI handler. If not, the
 * dec_return would have set the state to NMI_EXECUTING (what we want it
 * to be when we are running). In this case, we simply jump back to
 * rerun the NMI handler again, and restart the 'latched' NMI.
 *
 * No trap (breakpoint or page fault) should be hit before nmi_restart,
 * thus there is no race between the first check of state for NOT_RUNNING
 * and setting it to NMI_EXECUTING. The HW will prevent nested NMIs
 * at this point.
 *
 * In case the NMI takes a page fault, we need to save off the CR2
 * because the NMI could have preempted another page fault and corrupt
 * the CR2 that is about to be read. As nested NMIs must be restarted
 * and they can not take breakpoints or page faults, the update of the
 * CR2 must be done before converting the nmi state back to NOT_RUNNING.
 * Otherwise, there would be a race of another nested NMI coming in
 * after setting state to NOT_RUNNING but before updating the nmi_cr2.
 */
enum nmi_states {
	NMI_NOT_RUNNING = 0,
	NMI_EXECUTING,
	NMI_LATCHED,
};
static DEFINE_PER_CPU(enum nmi_states, nmi_state);
static DEFINE_PER_CPU(unsigned long, nmi_cr2);

#ifdef CONFIG_X86_64
/*
 * In x86_64, we need to handle breakpoint -> NMI -> breakpoint.  Without
 * some care, the inner breakpoint will clobber the outer breakpoint's
 * stack.
 *
 * If a breakpoint is being processed, and the debug stack is being
 * used, if an NMI comes in and also hits a breakpoint, the stack
 * pointer will be set to the same fixed address as the breakpoint that
 * was interrupted, causing that stack to be corrupted. To handle this
 * case, check if the stack that was interrupted is the debug stack, and
 * if so, change the IDT so that new breakpoints will use the current
 * stack and not switch to the fixed address. On return of the NMI,
 * switch back to the original IDT.
 */
static DEFINE_PER_CPU(int, update_debug_stack);
#endif

dotraplinkage notrace void
do_nmi(struct pt_regs *regs, long error_code)
{
	if (this_cpu_read(nmi_state) != NMI_NOT_RUNNING) {
		this_cpu_write(nmi_state, NMI_LATCHED);
		return;
	}
	this_cpu_write(nmi_state, NMI_EXECUTING);
	this_cpu_write(nmi_cr2, read_cr2());
nmi_restart:

#ifdef CONFIG_X86_64
	/*
	 * If we interrupted a breakpoint, it is possible that
	 * the nmi handler will have breakpoints too. We need to
	 * change the IDT such that breakpoints that happen here
	 * continue to use the NMI stack.
	 */
	if (unlikely(is_debug_stack(regs->sp))) {
		debug_stack_set_zero();
		this_cpu_write(update_debug_stack, 1);
	}
#endif

	nmi_enter();

	inc_irq_stat(__nmi_count);

	if (!ignore_nmis)
		default_do_nmi(regs);

	nmi_exit();

#ifdef CONFIG_X86_64
	if (unlikely(this_cpu_read(update_debug_stack))) {
		debug_stack_reset();
		this_cpu_write(update_debug_stack, 0);
	}
#endif

	if (unlikely(this_cpu_read(nmi_cr2) != read_cr2()))
		write_cr2(this_cpu_read(nmi_cr2));
	if (this_cpu_dec_return(nmi_state))
		goto nmi_restart;
}
NOKPROBE_SYMBOL(do_nmi);

void stop_nmi(void)
{
	ignore_nmis++;
}

void restart_nmi(void)
{
	ignore_nmis--;
}

/* reset the back-to-back NMI logic */
void local_touch_nmi(void)
{
	__this_cpu_write(last_nmi_rip, 0);
}
EXPORT_SYMBOL_GPL(local_touch_nmi);
