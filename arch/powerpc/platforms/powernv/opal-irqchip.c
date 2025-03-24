// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * This file implements an irqchip for OPAL events. Whenever there is
 * an interrupt that is handled by OPAL we get passed a list of events
 * that Linux needs to do something about. These basically look like
 * interrupts to Linux so we implement an irqchip to handle them.
 *
 * Copyright Alistair Popple, IBM Corporation 2014.
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
#include <linux/of_irq.h>

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
static u64 last_outstanding_events;
static int opal_irq_count;
static struct resource *opal_irqs;

void opal_handle_events(void)
{
	__be64 events = 0;
	u64 e;

	e = READ_ONCE(last_outstanding_events) & opal_event_irqchip.mask;
again:
	while (e) {
		int hwirq;

		hwirq = fls64(e) - 1;
		e &= ~BIT_ULL(hwirq);

		local_irq_disable();
		irq_enter();
		generic_handle_domain_irq(opal_event_irqchip.domain, hwirq);
		irq_exit();
		local_irq_enable();

		cond_resched();
	}
	WRITE_ONCE(last_outstanding_events, 0);
	if (opal_poll_events(&events) != OPAL_SUCCESS)
		return;
	e = be64_to_cpu(events) & opal_event_irqchip.mask;
	if (e)
		goto again;
}

bool opal_have_pending_events(void)
{
	if (READ_ONCE(last_outstanding_events) & opal_event_irqchip.mask)
		return true;
	return false;
}

static void opal_event_mask(struct irq_data *d)
{
	clear_bit(d->hwirq, &opal_event_irqchip.mask);
}

static void opal_event_unmask(struct irq_data *d)
{
	set_bit(d->hwirq, &opal_event_irqchip.mask);
	if (opal_have_pending_events())
		opal_wake_poller();
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
	WRITE_ONCE(last_outstanding_events, be64_to_cpu(events));
	if (opal_have_pending_events())
		opal_wake_poller();

	return IRQ_HANDLED;
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
		if (!opal_irqs || !opal_irqs[i].start)
			continue;

		if (in_interrupt() || irqs_disabled())
			disable_irq_nosync(opal_irqs[i].start);
		else
			free_irq(opal_irqs[i].start, NULL);

		opal_irqs[i].start = 0;
	}
}

int __init opal_event_init(void)
{
	struct device_node *dn, *opal_node;
	bool old_style = false;
	int i, rc = 0;

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

	/* Look for new-style (standard) "interrupts" property */
	opal_irq_count = of_irq_count(opal_node);

	/* Absent ? Look for the old one */
	if (opal_irq_count < 1) {
		/* Get opal-interrupts property and names if present */
		rc = of_property_count_u32_elems(opal_node, "opal-interrupts");
		if (rc > 0)
			opal_irq_count = rc;
		old_style = true;
	}

	/* No interrupts ? Bail out */
	if (!opal_irq_count)
		goto out;

	pr_debug("OPAL: Found %d interrupts reserved for OPAL using %s scheme\n",
		 opal_irq_count, old_style ? "old" : "new");

	/* Allocate an IRQ resources array */
	opal_irqs = kcalloc(opal_irq_count, sizeof(struct resource), GFP_KERNEL);
	if (WARN_ON(!opal_irqs)) {
		rc = -ENOMEM;
		goto out;
	}

	/* Build the resources array */
	if (old_style) {
		/* Old style "opal-interrupts" property */
		for (i = 0; i < opal_irq_count; i++) {
			struct resource *r = &opal_irqs[i];
			const char *name = NULL;
			u32 hw_irq;
			int virq;

			rc = of_property_read_u32_index(opal_node, "opal-interrupts",
							i, &hw_irq);
			if (WARN_ON(rc < 0)) {
				opal_irq_count = i;
				break;
			}
			of_property_read_string_index(opal_node, "opal-interrupts-names",
						      i, &name);
			virq = irq_create_mapping(NULL, hw_irq);
			if (!virq) {
				pr_warn("Failed to map OPAL irq 0x%x\n", hw_irq);
				continue;
			}
			r->start = r->end = virq;
			r->flags = IORESOURCE_IRQ | IRQ_TYPE_LEVEL_LOW;
			r->name = name;
		}
	} else {
		/* new style standard "interrupts" property */
		rc = of_irq_to_resource_table(opal_node, opal_irqs, opal_irq_count);
		if (WARN_ON(rc < 0)) {
			opal_irq_count = 0;
			kfree(opal_irqs);
			goto out;
		}
		if (WARN_ON(rc < opal_irq_count))
			opal_irq_count = rc;
	}

	/* Install interrupt handlers */
	for (i = 0; i < opal_irq_count; i++) {
		struct resource *r = &opal_irqs[i];
		const char *name;

		/* Prefix name */
		if (r->name && strlen(r->name))
			name = kasprintf(GFP_KERNEL, "opal-%s", r->name);
		else
			name = kasprintf(GFP_KERNEL, "opal");

		if (!name)
			continue;
		/* Install interrupt handler */
		rc = request_irq(r->start, opal_interrupt, r->flags & IRQD_TRIGGER_MASK,
				 name, NULL);
		if (rc) {
			pr_warn("Error %d requesting OPAL irq %d\n", rc, (int)r->start);
			kfree(name);
			continue;
		}
	}
	rc = 0;
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
		return 0;

	return irq_create_mapping(opal_event_irqchip.domain, opal_event_nr);
}
EXPORT_SYMBOL(opal_event_request);
