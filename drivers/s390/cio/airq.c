/*
 *    Support for adapter interruptions
 *
 *    Copyright IBM Corp. 1999, 2007
 *    Author(s): Ingo Adlung <adlung@de.ibm.com>
 *		 Cornelia Huck <cornelia.huck@de.ibm.com>
 *		 Arnd Bergmann <arndb@de.ibm.com>
 *		 Peter Oberparleiter <peter.oberparleiter@de.ibm.com>
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rculist.h>
#include <linux/slab.h>

#include <asm/airq.h>
#include <asm/isc.h>

#include "cio.h"
#include "cio_debug.h"
#include "ioasm.h"

static DEFINE_SPINLOCK(airq_lists_lock);
static struct hlist_head airq_lists[MAX_ISC+1];

/**
 * register_adapter_interrupt() - register adapter interrupt handler
 * @airq: pointer to adapter interrupt descriptor
 *
 * Returns 0 on success, or -EINVAL.
 */
int register_adapter_interrupt(struct airq_struct *airq)
{
	char dbf_txt[32];

	if (!airq->handler || airq->isc > MAX_ISC)
		return -EINVAL;
	if (!airq->lsi_ptr) {
		airq->lsi_ptr = kzalloc(1, GFP_KERNEL);
		if (!airq->lsi_ptr)
			return -ENOMEM;
		airq->flags |= AIRQ_PTR_ALLOCATED;
	}
	if (!airq->lsi_mask)
		airq->lsi_mask = 0xff;
	snprintf(dbf_txt, sizeof(dbf_txt), "rairq:%p", airq);
	CIO_TRACE_EVENT(4, dbf_txt);
	isc_register(airq->isc);
	spin_lock(&airq_lists_lock);
	hlist_add_head_rcu(&airq->list, &airq_lists[airq->isc]);
	spin_unlock(&airq_lists_lock);
	return 0;
}
EXPORT_SYMBOL(register_adapter_interrupt);

/**
 * unregister_adapter_interrupt - unregister adapter interrupt handler
 * @airq: pointer to adapter interrupt descriptor
 */
void unregister_adapter_interrupt(struct airq_struct *airq)
{
	char dbf_txt[32];

	if (hlist_unhashed(&airq->list))
		return;
	snprintf(dbf_txt, sizeof(dbf_txt), "urairq:%p", airq);
	CIO_TRACE_EVENT(4, dbf_txt);
	spin_lock(&airq_lists_lock);
	hlist_del_rcu(&airq->list);
	spin_unlock(&airq_lists_lock);
	synchronize_rcu();
	isc_unregister(airq->isc);
	if (airq->flags & AIRQ_PTR_ALLOCATED) {
		kfree(airq->lsi_ptr);
		airq->lsi_ptr = NULL;
		airq->flags &= ~AIRQ_PTR_ALLOCATED;
	}
}
EXPORT_SYMBOL(unregister_adapter_interrupt);

void do_adapter_IO(u8 isc)
{
	struct airq_struct *airq;
	struct hlist_head *head;

	head = &airq_lists[isc];
	rcu_read_lock();
	hlist_for_each_entry_rcu(airq, head, list)
		if ((*airq->lsi_ptr & airq->lsi_mask) != 0)
			airq->handler(airq);
	rcu_read_unlock();
}
