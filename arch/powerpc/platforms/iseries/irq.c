/*
 * This module supports the iSeries PCI bus interrupt handling
 * Copyright (C) 20yy  <Robert L Holtorf> <IBM Corp>
 * Copyright (C) 2004-2005 IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 * Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330,
 * Boston, MA  02111-1307  USA
 *
 * Change Activity:
 *   Created, December 13, 2000 by Wayne Holm
 * End Change Activity
 */
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/threads.h>
#include <linux/smp.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/bootmem.h>
#include <linux/irq.h>
#include <linux/spinlock.h>

#include <asm/paca.h>
#include <asm/iseries/hv_types.h>
#include <asm/iseries/hv_lp_event.h>
#include <asm/iseries/hv_call_xm.h>
#include <asm/iseries/it_lp_queue.h>

#include "irq.h"
#include "pci.h"
#include "call_pci.h"
#include "smp.h"

#ifdef CONFIG_PCI

enum pci_event_type {
	pe_bus_created		= 0,	/* PHB has been created */
	pe_bus_error		= 1,	/* PHB has failed */
	pe_bus_failed		= 2,	/* Msg to Secondary, Primary failed bus */
	pe_node_failed		= 4,	/* Multi-adapter bridge has failed */
	pe_node_recovered	= 5,	/* Multi-adapter bridge has recovered */
	pe_bus_recovered	= 12,	/* PHB has been recovered */
	pe_unquiese_bus		= 18,	/* Secondary bus unqiescing */
	pe_bridge_error		= 21,	/* Bridge Error */
	pe_slot_interrupt	= 22	/* Slot interrupt */
};

struct pci_event {
	struct HvLpEvent event;
	union {
		u64 __align;		/* Align on an 8-byte boundary */
		struct {
			u32		fisr;
			HvBusNumber	bus_number;
			HvSubBusNumber	sub_bus_number;
			HvAgentId	dev_id;
		} slot;
		struct {
			HvBusNumber	bus_number;
			HvSubBusNumber	sub_bus_number;
		} bus;
		struct {
			HvBusNumber	bus_number;
			HvSubBusNumber	sub_bus_number;
			HvAgentId	dev_id;
		} node;
	} data;
};

static DEFINE_SPINLOCK(pending_irqs_lock);
static int num_pending_irqs;
static int pending_irqs[NR_IRQS];

static void int_received(struct pci_event *event)
{
	int irq;

	switch (event->event.xSubtype) {
	case pe_slot_interrupt:
		irq = event->event.xCorrelationToken;
		if (irq < NR_IRQS) {
			spin_lock(&pending_irqs_lock);
			pending_irqs[irq]++;
			num_pending_irqs++;
			spin_unlock(&pending_irqs_lock);
		} else {
			printk(KERN_WARNING "int_received: bad irq number %d\n",
					irq);
			HvCallPci_eoi(event->data.slot.bus_number,
					event->data.slot.sub_bus_number,
					event->data.slot.dev_id);
		}
		break;
		/* Ignore error recovery events for now */
	case pe_bus_created:
		printk(KERN_INFO "int_received: system bus %d created\n",
			event->data.bus.bus_number);
		break;
	case pe_bus_error:
	case pe_bus_failed:
		printk(KERN_INFO "int_received: system bus %d failed\n",
			event->data.bus.bus_number);
		break;
	case pe_bus_recovered:
	case pe_unquiese_bus:
		printk(KERN_INFO "int_received: system bus %d recovered\n",
			event->data.bus.bus_number);
		break;
	case pe_node_failed:
	case pe_bridge_error:
		printk(KERN_INFO
			"int_received: multi-adapter bridge %d/%d/%d failed\n",
			event->data.node.bus_number,
			event->data.node.sub_bus_number,
			event->data.node.dev_id);
		break;
	case pe_node_recovered:
		printk(KERN_INFO
			"int_received: multi-adapter bridge %d/%d/%d recovered\n",
			event->data.node.bus_number,
			event->data.node.sub_bus_number,
			event->data.node.dev_id);
		break;
	default:
		printk(KERN_ERR
			"int_received: unrecognized event subtype 0x%x\n",
			event->event.xSubtype);
		break;
	}
}

static void pci_event_handler(struct HvLpEvent *event)
{
	if (event && (event->xType == HvLpEvent_Type_PciIo)) {
		if (hvlpevent_is_int(event))
			int_received((struct pci_event *)event);
		else
			printk(KERN_ERR
				"pci_event_handler: unexpected ack received\n");
	} else if (event)
		printk(KERN_ERR
			"pci_event_handler: Unrecognized PCI event type 0x%x\n",
			(int)event->xType);
	else
		printk(KERN_ERR "pci_event_handler: NULL event received\n");
}

#define REAL_IRQ_TO_SUBBUS(irq)	(((irq) >> 14) & 0xff)
#define REAL_IRQ_TO_BUS(irq)	((((irq) >> 6) & 0xff) + 1)
#define REAL_IRQ_TO_IDSEL(irq)	((((irq) >> 3) & 7) + 1)
#define REAL_IRQ_TO_FUNC(irq)	((irq) & 7)

/*
 * This will be called by device drivers (via enable_IRQ)
 * to enable INTA in the bridge interrupt status register.
 */
static void iseries_enable_IRQ(unsigned int irq)
{
	u32 bus, dev_id, function, mask;
	const u32 sub_bus = 0;
	unsigned int rirq = (unsigned int)irq_map[irq].hwirq;

	/* The IRQ has already been locked by the caller */
	bus = REAL_IRQ_TO_BUS(rirq);
	function = REAL_IRQ_TO_FUNC(rirq);
	dev_id = (REAL_IRQ_TO_IDSEL(rirq) << 4) + function;

	/* Unmask secondary INTA */
	mask = 0x80000000;
	HvCallPci_unmaskInterrupts(bus, sub_bus, dev_id, mask);
}

/* This is called by iseries_activate_IRQs */
static unsigned int iseries_startup_IRQ(unsigned int irq)
{
	u32 bus, dev_id, function, mask;
	const u32 sub_bus = 0;
	unsigned int rirq = (unsigned int)irq_map[irq].hwirq;

	bus = REAL_IRQ_TO_BUS(rirq);
	function = REAL_IRQ_TO_FUNC(rirq);
	dev_id = (REAL_IRQ_TO_IDSEL(rirq) << 4) + function;

	/* Link the IRQ number to the bridge */
	HvCallXm_connectBusUnit(bus, sub_bus, dev_id, irq);

	/* Unmask bridge interrupts in the FISR */
	mask = 0x01010000 << function;
	HvCallPci_unmaskFisr(bus, sub_bus, dev_id, mask);
	iseries_enable_IRQ(irq);
	return 0;
}

/*
 * This is called out of iSeries_fixup to activate interrupt
 * generation for usable slots
 */
void __init iSeries_activate_IRQs()
{
	int irq;
	unsigned long flags;

	for_each_irq (irq) {
		irq_desc_t *desc = get_irq_desc(irq);

		if (desc && desc->chip && desc->chip->startup) {
			spin_lock_irqsave(&desc->lock, flags);
			desc->chip->startup(irq);
			spin_unlock_irqrestore(&desc->lock, flags);
		}
	}
}

/*  this is not called anywhere currently */
static void iseries_shutdown_IRQ(unsigned int irq)
{
	u32 bus, dev_id, function, mask;
	const u32 sub_bus = 0;
	unsigned int rirq = (unsigned int)irq_map[irq].hwirq;

	/* irq should be locked by the caller */
	bus = REAL_IRQ_TO_BUS(rirq);
	function = REAL_IRQ_TO_FUNC(rirq);
	dev_id = (REAL_IRQ_TO_IDSEL(rirq) << 4) + function;

	/* Invalidate the IRQ number in the bridge */
	HvCallXm_connectBusUnit(bus, sub_bus, dev_id, 0);

	/* Mask bridge interrupts in the FISR */
	mask = 0x01010000 << function;
	HvCallPci_maskFisr(bus, sub_bus, dev_id, mask);
}

/*
 * This will be called by device drivers (via disable_IRQ)
 * to disable INTA in the bridge interrupt status register.
 */
static void iseries_disable_IRQ(unsigned int irq)
{
	u32 bus, dev_id, function, mask;
	const u32 sub_bus = 0;
	unsigned int rirq = (unsigned int)irq_map[irq].hwirq;

	/* The IRQ has already been locked by the caller */
	bus = REAL_IRQ_TO_BUS(rirq);
	function = REAL_IRQ_TO_FUNC(rirq);
	dev_id = (REAL_IRQ_TO_IDSEL(rirq) << 4) + function;

	/* Mask secondary INTA   */
	mask = 0x80000000;
	HvCallPci_maskInterrupts(bus, sub_bus, dev_id, mask);
}

static void iseries_end_IRQ(unsigned int irq)
{
	unsigned int rirq = (unsigned int)irq_map[irq].hwirq;

	HvCallPci_eoi(REAL_IRQ_TO_BUS(rirq), REAL_IRQ_TO_SUBBUS(rirq),
		(REAL_IRQ_TO_IDSEL(rirq) << 4) + REAL_IRQ_TO_FUNC(rirq));
}

static struct irq_chip iseries_pic = {
	.typename	= "iSeries irq controller",
	.startup	= iseries_startup_IRQ,
	.shutdown	= iseries_shutdown_IRQ,
	.unmask		= iseries_enable_IRQ,
	.mask		= iseries_disable_IRQ,
	.eoi		= iseries_end_IRQ
};

/*
 * This is called out of iSeries_scan_slot to allocate an IRQ for an EADS slot
 * It calculates the irq value for the slot.
 * Note that sub_bus is always 0 (at the moment at least).
 */
int __init iSeries_allocate_IRQ(HvBusNumber bus,
		HvSubBusNumber sub_bus, u32 bsubbus)
{
	unsigned int realirq;
	u8 idsel = ISERIES_GET_DEVICE_FROM_SUBBUS(bsubbus);
	u8 function = ISERIES_GET_FUNCTION_FROM_SUBBUS(bsubbus);

	realirq = (((((sub_bus << 8) + (bus - 1)) << 3) + (idsel - 1)) << 3)
		+ function;

	return irq_create_mapping(NULL, realirq);
}

#endif /* CONFIG_PCI */

/*
 * Get the next pending IRQ.
 */
unsigned int iSeries_get_irq(void)
{
	int irq = NO_IRQ_IGNORE;

#ifdef CONFIG_SMP
	if (get_lppaca()->int_dword.fields.ipi_cnt) {
		get_lppaca()->int_dword.fields.ipi_cnt = 0;
		iSeries_smp_message_recv();
	}
#endif /* CONFIG_SMP */
	if (hvlpevent_is_pending())
		process_hvlpevents();

#ifdef CONFIG_PCI
	if (num_pending_irqs) {
		spin_lock(&pending_irqs_lock);
		for (irq = 0; irq < NR_IRQS; irq++) {
			if (pending_irqs[irq]) {
				pending_irqs[irq]--;
				num_pending_irqs--;
				break;
			}
		}
		spin_unlock(&pending_irqs_lock);
		if (irq >= NR_IRQS)
			irq = NO_IRQ_IGNORE;
	}
#endif

	return irq;
}

#ifdef CONFIG_PCI

static int iseries_irq_host_map(struct irq_host *h, unsigned int virq,
				irq_hw_number_t hw)
{
	set_irq_chip_and_handler(virq, &iseries_pic, handle_fasteoi_irq);

	return 0;
}

static int iseries_irq_host_match(struct irq_host *h, struct device_node *np)
{
	/* Match all */
	return 1;
}

static struct irq_host_ops iseries_irq_host_ops = {
	.map = iseries_irq_host_map,
	.match = iseries_irq_host_match,
};

/*
 * This is called by init_IRQ.  set in ppc_md.init_IRQ by iSeries_setup.c
 * It must be called before the bus walk.
 */
void __init iSeries_init_IRQ(void)
{
	/* Register PCI event handler and open an event path */
	struct irq_host *host;
	int ret;

	/*
	 * The Hypervisor only allows us up to 256 interrupt
	 * sources (the irq number is passed in a u8).
	 */
	irq_set_virq_count(256);

	/* Create irq host. No need for a revmap since HV will give us
	 * back our virtual irq number
	 */
	host = irq_alloc_host(NULL, IRQ_HOST_MAP_NOMAP, 0,
			      &iseries_irq_host_ops, 0);
	BUG_ON(host == NULL);
	irq_set_default_host(host);

	ret = HvLpEvent_registerHandler(HvLpEvent_Type_PciIo,
			&pci_event_handler);
	if (ret == 0) {
		ret = HvLpEvent_openPath(HvLpEvent_Type_PciIo, 0);
		if (ret != 0)
			printk(KERN_ERR "iseries_init_IRQ: open event path "
					"failed with rc 0x%x\n", ret);
	} else
		printk(KERN_ERR "iseries_init_IRQ: register handler "
				"failed with rc 0x%x\n", ret);
}

#endif	/* CONFIG_PCI */
