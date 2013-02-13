/*
 *  Copyright (C) 2009 Imagination Technologies
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * The Meta KICK interrupt mechanism is generally a useful feature, so
 * we provide an interface for registering multiple interrupt
 * handlers. All the registered interrupt handlers are "chained". When
 * a KICK interrupt is received the first function in the list is
 * called. If that interrupt handler cannot handle the KICK the next
 * one is called, then the next until someone handles it (or we run
 * out of functions). As soon as one function handles the interrupt no
 * other handlers are called.
 *
 * The only downside of chaining interrupt handlers is that each
 * handler must be able to detect whether the KICK was intended for it
 * or not.  For example, when the IPI handler runs and it sees that
 * there are no IPI messages it must not signal that the KICK was
 * handled, thereby giving the other handlers a chance to run.
 *
 * The reason that we provide our own interface for calling KICK
 * handlers instead of using the generic kernel infrastructure is that
 * the KICK handlers require access to a CPU's pTBI structure. So we
 * pass it as an argument.
 */
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/types.h>

#include <asm/traps.h>

/*
 * All accesses/manipulations of kick_handlers_list should be
 * performed while holding kick_handlers_lock.
 */
static DEFINE_SPINLOCK(kick_handlers_lock);
static LIST_HEAD(kick_handlers_list);

void kick_register_func(struct kick_irq_handler *kh)
{
	unsigned long flags;

	spin_lock_irqsave(&kick_handlers_lock, flags);

	list_add_tail(&kh->list, &kick_handlers_list);

	spin_unlock_irqrestore(&kick_handlers_lock, flags);
}
EXPORT_SYMBOL(kick_register_func);

void kick_unregister_func(struct kick_irq_handler *kh)
{
	unsigned long flags;

	spin_lock_irqsave(&kick_handlers_lock, flags);

	list_del(&kh->list);

	spin_unlock_irqrestore(&kick_handlers_lock, flags);
}
EXPORT_SYMBOL(kick_unregister_func);

TBIRES
kick_handler(TBIRES State, int SigNum, int Triggers, int Inst, PTBI pTBI)
{
	struct kick_irq_handler *kh;
	struct list_head *lh;
	int handled = 0;
	TBIRES ret;

	head_end(State, ~INTS_OFF_MASK);

	/* If we interrupted user code handle any critical sections. */
	if (State.Sig.SaveMask & TBICTX_PRIV_BIT)
		restart_critical_section(State);

	trace_hardirqs_off();

	/*
	 * There is no need to disable interrupts here because we
	 * can't nest KICK interrupts in a KICK interrupt handler.
	 */
	spin_lock(&kick_handlers_lock);

	list_for_each(lh, &kick_handlers_list) {
		kh = list_entry(lh, struct kick_irq_handler, list);

		ret = kh->func(State, SigNum, Triggers, Inst, pTBI, &handled);
		if (handled)
			break;
	}

	spin_unlock(&kick_handlers_lock);

	WARN_ON(!handled);

	return tail_end(ret);
}
