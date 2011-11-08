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
#include <linux/delay.h>
#include <linux/hardirq.h>
#include <linux/slab.h>

#if defined(CONFIG_EDAC)
#include <linux/edac.h>
#endif

#include <linux/atomic.h>
#include <asm/traps.h>
#include <asm/mach_traps.h>
#include <asm/nmi.h>

#define NMI_MAX_NAMELEN	16
struct nmiaction {
	struct list_head list;
	nmi_handler_t handler;
	unsigned int flags;
	char *name;
};

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

};

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

static int notrace __kprobes nmi_handle(unsigned int type, struct pt_regs *regs)
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

		handled += a->handler(type, regs);

	}

	rcu_read_unlock();

	/* return total number of NMI events handled */
	return handled;
}

static int __setup_nmi(unsigned int type, struct nmiaction *action)
{
	struct nmi_desc *desc = nmi_to_desc(type);
	unsigned long flags;

	spin_lock_irqsave(&desc->lock, flags);

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

static struct nmiaction *__free_nmi(unsigned int type, const char *name)
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
	return (n);
}

int register_nmi_handler(unsigned int type, nmi_handler_t handler,
			unsigned long nmiflags, const char *devname)
{
	struct nmiaction *action;
	int retval = -ENOMEM;

	if (!handler)
		return -EINVAL;

	action = kzalloc(sizeof(struct nmiaction), GFP_KERNEL);
	if (!action)
		goto fail_action;

	action->handler = handler;
	action->flags = nmiflags;
	action->name = kstrndup(devname, NMI_MAX_NAMELEN, GFP_KERNEL);
	if (!action->name)
		goto fail_action_name;

	retval = __setup_nmi(type, action);

	if (retval)
		goto fail_setup_nmi;

	return retval;

fail_setup_nmi:
	kfree(action->name);
fail_action_name:
	kfree(action);
fail_action:	

	return retval;
}
EXPORT_SYMBOL_GPL(register_nmi_handler);

void unregister_nmi_handler(unsigned int type, const char *name)
{
	struct nmiaction *a;

	a = __free_nmi(type, name);
	if (a) {
		kfree(a->name);
		kfree(a);
	}
}

EXPORT_SYMBOL_GPL(unregister_nmi_handler);

static notrace __kprobes void
pci_serr_error(unsigned char reason, struct pt_regs *regs)
{
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
		panic("NMI: Not continuing");

	pr_emerg("Dazed and confused, but trying to continue\n");

	/* Clear and disable the PCI SERR error line. */
	reason = (reason & NMI_REASON_CLEAR_MASK) | NMI_REASON_CLEAR_SERR;
	outb(reason, NMI_REASON_PORT);
}

static notrace __kprobes void
io_check_error(unsigned char reason, struct pt_regs *regs)
{
	unsigned long i;

	pr_emerg(
	"NMI: IOCK error (debug interrupt?) for reason %02x on CPU %d.\n",
		 reason, smp_processor_id());
	show_registers(regs);

	if (panic_on_io_nmi)
		panic("NMI IOCK error: Not continuing");

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

static notrace __kprobes void
unknown_nmi_error(unsigned char reason, struct pt_regs *regs)
{
	int handled;

	handled = nmi_handle(NMI_UNKNOWN, regs);
	if (handled)
		return;
#ifdef CONFIG_MCA
	/*
	 * Might actually be able to figure out what the guilty party
	 * is:
	 */
	if (MCA_bus) {
		mca_handle_nmi();
		return;
	}
#endif
	pr_emerg("Uhhuh. NMI received for unknown reason %02x on CPU %d.\n",
		 reason, smp_processor_id());

	pr_emerg("Do you have a strange power saving mode enabled?\n");
	if (unknown_nmi_panic || panic_on_unrecovered_nmi)
		panic("NMI: Not continuing");

	pr_emerg("Dazed and confused, but trying to continue\n");
}

static notrace __kprobes void default_do_nmi(struct pt_regs *regs)
{
	unsigned char reason = 0;
	int handled;

	/*
	 * CPU-specific NMI must be processed before non-CPU-specific
	 * NMI, otherwise we may lose it, because the CPU-specific
	 * NMI can not be detected/processed on other CPUs.
	 */
	handled = nmi_handle(NMI_LOCAL, regs);
	if (handled)
		return;

	/* Non-CPU-specific NMI: NMI sources can be processed on any CPU */
	raw_spin_lock(&nmi_reason_lock);
	reason = get_nmi_reason();

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
		raw_spin_unlock(&nmi_reason_lock);
		return;
	}
	raw_spin_unlock(&nmi_reason_lock);

	unknown_nmi_error(reason, regs);
}

dotraplinkage notrace __kprobes void
do_nmi(struct pt_regs *regs, long error_code)
{
	nmi_enter();

	inc_irq_stat(__nmi_count);

	if (!ignore_nmis)
		default_do_nmi(regs);

	nmi_exit();
}

void stop_nmi(void)
{
	ignore_nmis++;
}

void restart_nmi(void)
{
	ignore_nmis--;
}
