/*
 * Copyright 2010 2011 Mark Nelson and Tseng-Hui (Frank) Lin, IBM Corporation
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
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

/* pSeries event log format */

/* Two bytes ASCII section IDs */
#define PSERIES_ELOG_SECT_ID_PRIV_HDR		(('P' << 8) | 'H')
#define PSERIES_ELOG_SECT_ID_USER_HDR		(('U' << 8) | 'H')
#define PSERIES_ELOG_SECT_ID_PRIMARY_SRC	(('P' << 8) | 'S')
#define PSERIES_ELOG_SECT_ID_EXTENDED_UH	(('E' << 8) | 'H')
#define PSERIES_ELOG_SECT_ID_FAILING_MTMS	(('M' << 8) | 'T')
#define PSERIES_ELOG_SECT_ID_SECONDARY_SRC	(('S' << 8) | 'S')
#define PSERIES_ELOG_SECT_ID_DUMP_LOCATOR	(('D' << 8) | 'H')
#define PSERIES_ELOG_SECT_ID_FW_ERROR		(('S' << 8) | 'W')
#define PSERIES_ELOG_SECT_ID_IMPACT_PART_ID	(('L' << 8) | 'P')
#define PSERIES_ELOG_SECT_ID_LOGIC_RESOURCE_ID	(('L' << 8) | 'R')
#define PSERIES_ELOG_SECT_ID_HMC_ID		(('H' << 8) | 'M')
#define PSERIES_ELOG_SECT_ID_EPOW		(('E' << 8) | 'P')
#define PSERIES_ELOG_SECT_ID_IO_EVENT		(('I' << 8) | 'E')
#define PSERIES_ELOG_SECT_ID_MANUFACT_INFO	(('M' << 8) | 'I')
#define PSERIES_ELOG_SECT_ID_CALL_HOME		(('C' << 8) | 'H')
#define PSERIES_ELOG_SECT_ID_USER_DEF		(('U' << 8) | 'D')

/* Vendor specific Platform Event Log Format, Version 6, section header */
struct pseries_elog_section {
	uint16_t id;			/* 0x00 2-byte ASCII section ID	*/
	uint16_t length;		/* 0x02 Section length in bytes	*/
	uint8_t version;		/* 0x04 Section version		*/
	uint8_t subtype;		/* 0x05 Section subtype		*/
	uint16_t creator_component;	/* 0x06 Creator component ID	*/
	uint8_t data[];			/* 0x08 Start of section data	*/
};

static char ioei_rtas_buf[RTAS_DATA_BUF_SIZE] __cacheline_aligned;

/**
 * Find data portion of a specific section in RTAS extended event log.
 * @elog: RTAS error/event log.
 * @sect_id: secsion ID.
 *
 * Return:
 *	pointer to the section data of the specified section
 *	NULL if not found
 */
static struct pseries_elog_section *find_xelog_section(struct rtas_error_log *elog,
						       uint16_t sect_id)
{
	struct rtas_ext_event_log_v6 *xelog =
		(struct rtas_ext_event_log_v6 *) elog->buffer;
	struct pseries_elog_section *sect;
	unsigned char *p, *log_end;

	/* Check that we understand the format */
	if (elog->extended_log_length < sizeof(struct rtas_ext_event_log_v6) ||
	    xelog->log_format != RTAS_V6EXT_LOG_FORMAT_EVENT_LOG ||
	    xelog->company_id != RTAS_V6EXT_COMPANY_ID_IBM)
		return NULL;

	log_end = elog->buffer + elog->extended_log_length;
	p = xelog->vendor_log;
	while (p < log_end) {
		sect = (struct pseries_elog_section *)p;
		if (sect->id == sect_id)
			return sect;
		p += sect->length;
	}
	return NULL;
}

/**
 * Find the data portion of an IO Event section from event log.
 * @elog: RTAS error/event log.
 *
 * Return:
 * 	pointer to a valid IO event section data. NULL if not found.
 */
static struct pseries_io_event * ioei_find_event(struct rtas_error_log *elog)
{
	struct pseries_elog_section *sect;

	/* We should only ever get called for io-event interrupts, but if
	 * we do get called for another type then something went wrong so
	 * make some noise about it.
	 * RTAS_TYPE_IO only exists in extended event log version 6 or later.
	 * No need to check event log version.
	 */
	if (unlikely(elog->type != RTAS_TYPE_IO)) {
		printk_once(KERN_WARNING "io_event_irq: Unexpected event type %d",
			    elog->type);
		return NULL;
	}

	sect = find_xelog_section(elog, PSERIES_ELOG_SECT_ID_IO_EVENT);
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
 *   event is reported with scope 0x00 (Not Applicatable) rather than
 *   0x3B (Torrent-hub). It is better to let the clients to identify
 *   who owns the the event.
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

