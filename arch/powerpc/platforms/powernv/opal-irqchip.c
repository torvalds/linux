/*
 * This file implements an irqchip for OPAL events. Whenever there is
 * an interrupt that is handled by OPAL we get passed a list of events
 * that Linux needs to do something about. These basically look like
 * interrupts to Linux so we implement an irqchip to handle them.
 *
 * Copyright Alistair Popple, IBM Corporation 2014.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/bitops.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/irq_work.h>

#include <asm/machdep.h>
#include <asm/opal.h>

#include "powernv.h"

/* Maximum number of events supported by OPAL firmware */
#define MAX_NUM_EVENTS 64

struct opal_event_irqchip {
	struct irq_chip irqchip;
	struct irq_domain *domain;
	unsigned long mask;
};
static struct opal_event_irqchip opal_event_irqchip;

static unsigned int opal_irq_count;
static unsigned int *opal_irqs;

static void opal_handle_irq_work(struct irq_work *work);
static u64 last_outstanding_events;
static struct irq_work opal_event_irq_work = {
	.func = opal_handle_irq_work,
};

void opal_handle_events(uint64_t events)
{
	int virq, hwirq = 0;
	u64 mask = opal_event_irqchip.mask;

	if (!in_irq() && (events & mask)) {
		last_outstanding_events = events;
		irq_work_queue(&opal_event_irq_work);
		return;
	}

	while (events & mask) {
		hwirq = fls64(events) - 1;
		if (BIT_ULL(hwirq) & mask) {
			virq = irq_find_mapping(opal_event_irqchip.domain,
						hwirq);
			if (virq)
				generic_handle_irq(virq);
		}
		events &= ~BIT_ULL(hwirq);
	}
}

static void opal_event_mask(struct irq_data *d)
{
	clear_bit(d->hwirq, &opal_event_irqchip.mask);
}

static void opal_event_unmask(struct irq_data *d)
{
	__be64 events;

	set_bit(d->hwirq, &opal_event_irqchip.mask);

	opal_poll_events(&events);
	last_outstanding_events = be64_to_cpu(events);

	/*
	 * We can't just handle the events now with opal_handle_events().
	 * If we did we would deadlock when opal_event_unmask() is called from
	 * handle_level_irq() with the irq descriptor lock held, because
	 * calling opal_handle_events() would call generic_handle_irq() and
	 * then handle_level_irq() which would try to take the descriptor lock
	 * again. Instead queue the events for later.
	 */
	if (last_outstanding_events & opal_event_irqchip.mask)
		/* Need to retrigger the interrupt */
		irq_work_queue(&opal_event_irq_work);
}

static int opal_event_set_type(struct irq_data *d, unsigned int flow_type)
{
	/*
	 * For now we only support level triggered events. The irq
	 * handler will be called continuously until the event has
	 * been cleared in OPAL.
	 */
	if (flow_type != IRQ_TYPE_LEVEL_HIGH)
		return -EINVAL;

	return 0;
}

static struct opal_event_irqchip opal_event_irqchip = {
	.irqchip = {
		.name = "OPAL EVT",
		.irq_mask = opal_event_mask,
		.irq_unmask = opal_event_unmask,
		.irq_set_type = opal_event_set_type,
	},
	.mask = 0,
};

static int opal_event_map(struct irq_domain *d, unsigned int irq,
			irq_hw_number_t hwirq)
{
	irq_set_chip_data(irq, &opal_event_irqchip);
	irq_set_chip_and_handler(irq, &opal_event_irqchip.irqchip,
				handle_level_irq);

	return 0;
}

static irqreturn_t opal_interrupt(int irq, void *data)
{
	__be64 events;

	opal_handle_interrupt(virq_to_hw(irq), &events);
	opal_handle_events(be64_to_cpu(events));

	return IRQ_HANDLED;
}

static void opal_handle_irq_work(struct irq_work *work)
{
	opal_handle_events(last_outstanding_events);
}

static int opal_event_match(struct irq_domain *h, struct device_node *node,
			    enum irq_domain_bus_token bus_token)
{
	return irq_domain_get_of_node(h) == node;
}

static int opal_event_xlate(struct irq_domain *h, struct device_node *np,
			   const u32 *intspec, unsigned int intsize,
			   irq_hw_number_t *out_hwirq, unsigned int *out_flags)
{
	*out_hwirq = intspec[0];
	*out_flags = IRQ_TYPE_LEVEL_HIGH;

	return 0;
}

static const struct irq_domain_ops opal_event_domain_ops = {
	.match	= opal_event_match,
	.map	= opal_event_map,
	.xlate	= opal_event_xlate,
};

void opal_event_shutdown(void)
{
	unsigned int i;

	/* First free interrupts, which will also mask them */
	for (i = 0; i < opal_irq_count; i++) {
		if (opal_irqs[i])
			free_irq(opal_irqs[i], NULL);
		opal_irqs[i] = 0;
	}
}

int __init opal_event_init(void)
{
	struct device_node *dn, *opal_node;
	const __be32 *irqs;
	int i, irqlen, rc = 0;

	opal_node = of_find_node_by_path("/ibm,opal");
	if (!opal_node) {
		pr_warn("opal: Node not found\n");
		return -ENODEV;
	}

	/* If dn is NULL it means the domain won't be linked to a DT
	 * node so therefore irq_of_parse_and_map(...) wont work. But
	 * that shouldn't be problem because if we're running a
	 * version of skiboot that doesn't have the dn then the
	 * devices won't have the correct properties and will have to
	 * fall back to the legacy method (opal_event_request(...))
	 * anyway. */
	dn = of_find_compatible_node(NULL, NULL, "ibm,opal-event");
	opal_event_irqchip.domain = irq_domain_add_linear(dn, MAX_NUM_EVENTS,
				&opal_event_domain_ops, &opal_event_irqchip);
	of_node_put(dn);
	if (!opal_event_irqchip.domain) {
		pr_warn("opal: Unable to create irq domain\n");
		rc = -ENOMEM;
		goto out;
	}

	/* Get interrupt property */
	irqs = of_get_property(opal_node, "opal-interrupts", &irqlen);
	opal_irq_count = irqs ? (irqlen / 4) : 0;
	pr_debug("Found %d interrupts reserved for OPAL\n", opal_irq_count);

	/* Install interrupt handlers */
	opal_irqs = kcalloc(opal_irq_count, sizeof(*opal_irqs), GFP_KERNEL);
	for (i = 0; irqs && i < opal_irq_count; i++, irqs++) {
		unsigned int irq, virq;

		/* Get hardware and virtual IRQ */
		irq = be32_to_cpup(irqs);
		virq = irq_create_mapping(NULL, irq);
		if (virq == NO_IRQ) {
			pr_warn("Failed to map irq 0x%x\n", irq);
			continue;
		}

		/* Install interrupt handler */
		rc = request_irq(virq, opal_interrupt, IRQF_TRIGGER_LOW,
				 "opal", NULL);
		if (rc) {
			irq_dispose_mapping(virq);
			pr_warn("Error %d requesting irq %d (0x%x)\n",
				 rc, virq, irq);
			continue;
		}

		/* Cache IRQ */
		opal_irqs[i] = virq;
	}

out:
	of_node_put(opal_node);
	return rc;
}
machine_arch_initcall(powernv, opal_event_init);

/**
 * opal_event_request(unsigned int opal_event_nr) - Request an event
 * @opal_event_nr: the opal event number to request
 *
 * This routine can be used to find the linux virq number which can
 * then be passed to request_irq to assign a handler for a particular
 * opal event. This should only be used by legacy devices which don't
 * have proper device tree bindings. Most devices should use
 * irq_of_parse_and_map() instead.
 */
int opal_event_request(unsigned int opal_event_nr)
{
	if (WARN_ON_ONCE(!opal_event_irqchip.domain))
		return NO_IRQ;

	return irq_create_mapping(opal_event_irqchip.domain, opal_event_nr);
}
EXPORT_SYMBOL(opal_event_request);
