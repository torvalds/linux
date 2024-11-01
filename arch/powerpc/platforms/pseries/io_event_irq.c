// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2010 2011 Mark Nelson and Tseng-Hui (Frank) Lin, IBM Corporation
 */

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/list.h>
#include <linux/notifier.h>

#include <asm/machdep.h>
#include <asm/rtas.h>
#include <asm/irq.h>
#include <asm/io_event_irq.h>

#include "pseries.h"

/*
 * IO event interrupt is a mechanism provided by RTAS to return
 * information about hardware error and non-error events. Device
 * drivers can register their event handlers to receive events.
 * Device drivers are expected to use atomic_notifier_chain_register()
 * and atomic_notifier_chain_unregister() to register and unregister
 * their event handlers. Since multiple IO event types and scopes
 * share an IO event interrupt, the event handlers are called one
 * by one until the IO event is claimed by one of the handlers.
 * The event handlers are expected to return NOTIFY_OK if the
 * event is handled by the event handler or NOTIFY_DONE if the
 * event does not belong to the handler.
 *
 * Usage:
 *
 * Notifier function:
 * #include <asm/io_event_irq.h>
 * int event_handler(struct notifier_block *nb, unsigned long val, void *data) {
 * 	p = (struct pseries_io_event_sect_data *) data;
 * 	if (! is_my_event(p->scope, p->event_type)) return NOTIFY_DONE;
 * 		:
 * 		:
 * 	return NOTIFY_OK;
 * }
 * struct notifier_block event_nb = {
 * 	.notifier_call = event_handler,
 * }
 *
 * Registration:
 * atomic_notifier_chain_register(&pseries_ioei_notifier_list, &event_nb);
 *
 * Unregistration:
 * atomic_notifier_chain_unregister(&pseries_ioei_notifier_list, &event_nb);
 */

ATOMIC_NOTIFIER_HEAD(pseries_ioei_notifier_list);
EXPORT_SYMBOL_GPL(pseries_ioei_notifier_list);

static int ioei_check_exception_token;

static char ioei_rtas_buf[RTAS_DATA_BUF_SIZE] __cacheline_aligned;

/**
 * Find the data portion of an IO Event section from event log.
 * @elog: RTAS error/event log.
 *
 * Return:
 * 	pointer to a valid IO event section data. NULL if not found.
 */
static struct pseries_io_event * ioei_find_event(struct rtas_error_log *elog)
{
	struct pseries_errorlog *sect;

	/* We should only ever get called for io-event interrupts, but if
	 * we do get called for another type then something went wrong so
	 * make some noise about it.
	 * RTAS_TYPE_IO only exists in extended event log version 6 or later.
	 * No need to check event log version.
	 */
	if (unlikely(rtas_error_type(elog) != RTAS_TYPE_IO)) {
		printk_once(KERN_WARNING"io_event_irq: Unexpected event type %d",
			    rtas_error_type(elog));
		return NULL;
	}

	sect = get_pseries_errorlog(elog, PSERIES_ELOG_SECT_ID_IO_EVENT);
	if (unlikely(!sect)) {
		printk_once(KERN_WARNING "io_event_irq: RTAS extended event "
			    "log does not contain an IO Event section. "
			    "Could be a bug in system firmware!\n");
		return NULL;
	}
	return (struct pseries_io_event *) &sect->data;
}

/*
 * PAPR:
 * - check-exception returns the first found error or event and clear that
 *   error or event so it is reported once.
 * - Each interrupt returns one event. If a plateform chooses to report
 *   multiple events through a single interrupt, it must ensure that the
 *   interrupt remains asserted until check-exception has been used to
 *   process all out-standing events for that interrupt.
 *
 * Implementation notes:
 * - Events must be processed in the order they are returned. Hence,
 *   sequential in nature.
 * - The owner of an event is determined by combinations of scope,
 *   event type, and sub-type. There is no easy way to pre-sort clients
 *   by scope or event type alone. For example, Torrent ISR route change
 *   event is reported with scope 0x00 (Not Applicable) rather than
 *   0x3B (Torrent-hub). It is better to let the clients to identify
 *   who owns the event.
 */

static irqreturn_t ioei_interrupt(int irq, void *dev_id)
{
	struct pseries_io_event *event;
	int rtas_rc;

	for (;;) {
		rtas_rc = rtas_call(ioei_check_exception_token, 6, 1, NULL,
				    RTAS_VECTOR_EXTERNAL_INTERRUPT,
				    virq_to_hw(irq),
				    RTAS_IO_EVENTS, 1 /* Time Critical */,
				    __pa(ioei_rtas_buf),
				    RTAS_DATA_BUF_SIZE);
		if (rtas_rc != 0)
			break;

		event = ioei_find_event((struct rtas_error_log *)ioei_rtas_buf);
		if (!event)
			continue;

		atomic_notifier_call_chain(&pseries_ioei_notifier_list,
					   0, event);
	}
	return IRQ_HANDLED;
}

static int __init ioei_init(void)
{
	struct device_node *np;

	ioei_check_exception_token = rtas_token("check-exception");
	if (ioei_check_exception_token == RTAS_UNKNOWN_SERVICE)
		return -ENODEV;

	np = of_find_node_by_path("/event-sources/ibm,io-events");
	if (np) {
		request_event_sources_irqs(np, ioei_interrupt, "IO_EVENT");
		pr_info("IBM I/O event interrupts enabled\n");
		of_node_put(np);
	} else {
		return -ENODEV;
	}
	return 0;
}
machine_subsys_initcall(pseries, ioei_init);

