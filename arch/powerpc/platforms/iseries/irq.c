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
#include <linux/config.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/threads.h>
#include <linux/smp.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/bootmem.h>
#include <linux/ide.h>
#include <linux/irq.h>
#include <linux/spinlock.h>

#include <asm/ppcdebug.h>
#include <asm/iseries/hv_types.h>
#include <asm/iseries/hv_lp_event.h>
#include <asm/iseries/hv_call_xm.h>

#include "irq.h"
#include "call_pci.h"

/* This maps virtual irq numbers to real irqs */
unsigned int virt_irq_to_real_map[NR_IRQS];

/* The next available virtual irq number */
/* Note: the pcnet32 driver assumes irq numbers < 2 aren't valid. :( */
static int next_virtual_irq = 2;

static long Pci_Interrupt_Count;
static long Pci_Event_Count;

enum XmPciLpEvent_Subtype {
	XmPciLpEvent_BusCreated		= 0,	// PHB has been created
	XmPciLpEvent_BusError		= 1,	// PHB has failed
	XmPciLpEvent_BusFailed		= 2,	// Msg to Secondary, Primary failed bus
	XmPciLpEvent_NodeFailed		= 4,	// Multi-adapter bridge has failed
	XmPciLpEvent_NodeRecovered	= 5,	// Multi-adapter bridge has recovered
	XmPciLpEvent_BusRecovered	= 12,	// PHB has been recovered
	XmPciLpEvent_UnQuiesceBus	= 18,	// Secondary bus unqiescing
	XmPciLpEvent_BridgeError	= 21,	// Bridge Error
	XmPciLpEvent_SlotInterrupt	= 22	// Slot interrupt
};

struct XmPciLpEvent_BusInterrupt {
	HvBusNumber	busNumber;
	HvSubBusNumber	subBusNumber;
};

struct XmPciLpEvent_NodeInterrupt {
	HvBusNumber	busNumber;
	HvSubBusNumber	subBusNumber;
	HvAgentId	deviceId;
};

struct XmPciLpEvent {
	struct HvLpEvent hvLpEvent;

	union {
		u64 alignData;			// Align on an 8-byte boundary

		struct {
			u32		fisr;
			HvBusNumber	busNumber;
			HvSubBusNumber	subBusNumber;
			HvAgentId	deviceId;
		} slotInterrupt;

		struct XmPciLpEvent_BusInterrupt busFailed;
		struct XmPciLpEvent_BusInterrupt busRecovered;
		struct XmPciLpEvent_BusInterrupt busCreated;

		struct XmPciLpEvent_NodeInterrupt nodeFailed;
		struct XmPciLpEvent_NodeInterrupt nodeRecovered;

	} eventData;

};

static void intReceived(struct XmPciLpEvent *eventParm,
		struct pt_regs *regsParm)
{
	int irq;

	++Pci_Interrupt_Count;

	switch (eventParm->hvLpEvent.xSubtype) {
	case XmPciLpEvent_SlotInterrupt:
		irq = eventParm->hvLpEvent.xCorrelationToken;
		/* Dispatch the interrupt handlers for this irq */
		ppc_irq_dispatch_handler(regsParm, irq);
		HvCallPci_eoi(eventParm->eventData.slotInterrupt.busNumber,
			eventParm->eventData.slotInterrupt.subBusNumber,
			eventParm->eventData.slotInterrupt.deviceId);
		break;
		/* Ignore error recovery events for now */
	case XmPciLpEvent_BusCreated:
		printk(KERN_INFO "intReceived: system bus %d created\n",
			eventParm->eventData.busCreated.busNumber);
		break;
	case XmPciLpEvent_BusError:
	case XmPciLpEvent_BusFailed:
		printk(KERN_INFO "intReceived: system bus %d failed\n",
			eventParm->eventData.busFailed.busNumber);
		break;
	case XmPciLpEvent_BusRecovered:
	case XmPciLpEvent_UnQuiesceBus:
		printk(KERN_INFO "intReceived: system bus %d recovered\n",
			eventParm->eventData.busRecovered.busNumber);
		break;
	case XmPciLpEvent_NodeFailed:
	case XmPciLpEvent_BridgeError:
		printk(KERN_INFO
			"intReceived: multi-adapter bridge %d/%d/%d failed\n",
			eventParm->eventData.nodeFailed.busNumber,
			eventParm->eventData.nodeFailed.subBusNumber,
			eventParm->eventData.nodeFailed.deviceId);
		break;
	case XmPciLpEvent_NodeRecovered:
		printk(KERN_INFO
			"intReceived: multi-adapter bridge %d/%d/%d recovered\n",
			eventParm->eventData.nodeRecovered.busNumber,
			eventParm->eventData.nodeRecovered.subBusNumber,
			eventParm->eventData.nodeRecovered.deviceId);
		break;
	default:
		printk(KERN_ERR
			"intReceived: unrecognized event subtype 0x%x\n",
			eventParm->hvLpEvent.xSubtype);
		break;
	}
}

static void XmPciLpEvent_handler(struct HvLpEvent *eventParm,
		struct pt_regs *regsParm)
{
#ifdef CONFIG_PCI
	++Pci_Event_Count;

	if (eventParm && (eventParm->xType == HvLpEvent_Type_PciIo)) {
		switch (eventParm->xFlags.xFunction) {
		case HvLpEvent_Function_Int:
			intReceived((struct XmPciLpEvent *)eventParm, regsParm);
			break;
		case HvLpEvent_Function_Ack:
			printk(KERN_ERR
				"XmPciLpEvent_handler: unexpected ack received\n");
			break;
		default:
			printk(KERN_ERR
				"XmPciLpEvent_handler: unexpected event function %d\n",
				(int)eventParm->xFlags.xFunction);
			break;
		}
	} else if (eventParm)
		printk(KERN_ERR
			"XmPciLpEvent_handler: Unrecognized PCI event type 0x%x\n",
			(int)eventParm->xType);
	else
		printk(KERN_ERR "XmPciLpEvent_handler: NULL event received\n");
#endif
}

/*
 * This is called by init_IRQ.  set in ppc_md.init_IRQ by iSeries_setup.c
 * It must be called before the bus walk.
 */
void __init iSeries_init_IRQ(void)
{
	/* Register PCI event handler and open an event path */
	int xRc;

	xRc = HvLpEvent_registerHandler(HvLpEvent_Type_PciIo,
			&XmPciLpEvent_handler);
	if (xRc == 0) {
		xRc = HvLpEvent_openPath(HvLpEvent_Type_PciIo, 0);
		if (xRc != 0)
			printk(KERN_ERR "iSeries_init_IRQ: open event path "
					"failed with rc 0x%x\n", xRc);
	} else
		printk(KERN_ERR "iSeries_init_IRQ: register handler "
				"failed with rc 0x%x\n", xRc);
}

#define REAL_IRQ_TO_BUS(irq)	((((irq) >> 6) & 0xff) + 1)
#define REAL_IRQ_TO_IDSEL(irq)	((((irq) >> 3) & 7) + 1)
#define REAL_IRQ_TO_FUNC(irq)	((irq) & 7)

/*
 * This will be called by device drivers (via enable_IRQ)
 * to enable INTA in the bridge interrupt status register.
 */
static void iSeries_enable_IRQ(unsigned int irq)
{
	u32 bus, deviceId, function, mask;
	const u32 subBus = 0;
	unsigned int rirq = virt_irq_to_real_map[irq];

	/* The IRQ has already been locked by the caller */
	bus = REAL_IRQ_TO_BUS(rirq);
	function = REAL_IRQ_TO_FUNC(rirq);
	deviceId = (REAL_IRQ_TO_IDSEL(rirq) << 4) + function;

	/* Unmask secondary INTA */
	mask = 0x80000000;
	HvCallPci_unmaskInterrupts(bus, subBus, deviceId, mask);
	PPCDBG(PPCDBG_BUSWALK, "iSeries_enable_IRQ 0x%02X.%02X.%02X 0x%04X\n",
			bus, subBus, deviceId, irq);
}

/* This is called by iSeries_activate_IRQs */
static unsigned int iSeries_startup_IRQ(unsigned int irq)
{
	u32 bus, deviceId, function, mask;
	const u32 subBus = 0;
	unsigned int rirq = virt_irq_to_real_map[irq];

	bus = REAL_IRQ_TO_BUS(rirq);
	function = REAL_IRQ_TO_FUNC(rirq);
	deviceId = (REAL_IRQ_TO_IDSEL(rirq) << 4) + function;

	/* Link the IRQ number to the bridge */
	HvCallXm_connectBusUnit(bus, subBus, deviceId, irq);

	/* Unmask bridge interrupts in the FISR */
	mask = 0x01010000 << function;
	HvCallPci_unmaskFisr(bus, subBus, deviceId, mask);
	iSeries_enable_IRQ(irq);
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

		if (desc && desc->handler && desc->handler->startup) {
			spin_lock_irqsave(&desc->lock, flags);
			desc->handler->startup(irq);
			spin_unlock_irqrestore(&desc->lock, flags);
		}
	}
}

/*  this is not called anywhere currently */
static void iSeries_shutdown_IRQ(unsigned int irq)
{
	u32 bus, deviceId, function, mask;
	const u32 subBus = 0;
	unsigned int rirq = virt_irq_to_real_map[irq];

	/* irq should be locked by the caller */
	bus = REAL_IRQ_TO_BUS(rirq);
	function = REAL_IRQ_TO_FUNC(rirq);
	deviceId = (REAL_IRQ_TO_IDSEL(rirq) << 4) + function;

	/* Invalidate the IRQ number in the bridge */
	HvCallXm_connectBusUnit(bus, subBus, deviceId, 0);

	/* Mask bridge interrupts in the FISR */
	mask = 0x01010000 << function;
	HvCallPci_maskFisr(bus, subBus, deviceId, mask);
}

/*
 * This will be called by device drivers (via disable_IRQ)
 * to disable INTA in the bridge interrupt status register.
 */
static void iSeries_disable_IRQ(unsigned int irq)
{
	u32 bus, deviceId, function, mask;
	const u32 subBus = 0;
	unsigned int rirq = virt_irq_to_real_map[irq];

	/* The IRQ has already been locked by the caller */
	bus = REAL_IRQ_TO_BUS(rirq);
	function = REAL_IRQ_TO_FUNC(rirq);
	deviceId = (REAL_IRQ_TO_IDSEL(rirq) << 4) + function;

	/* Mask secondary INTA   */
	mask = 0x80000000;
	HvCallPci_maskInterrupts(bus, subBus, deviceId, mask);
	PPCDBG(PPCDBG_BUSWALK, "iSeries_disable_IRQ 0x%02X.%02X.%02X 0x%04X\n",
			bus, subBus, deviceId, irq);
}

/*
 * Need to define this so ppc_irq_dispatch_handler will NOT call
 * enable_IRQ at the end of interrupt handling.  However, this does
 * nothing because there is not enough information provided to do
 * the EOI HvCall.  This is done by XmPciLpEvent.c
 */
static void iSeries_end_IRQ(unsigned int irq)
{
}

static hw_irq_controller iSeries_IRQ_handler = {
	.typename = "iSeries irq controller",
	.startup = iSeries_startup_IRQ,
	.shutdown = iSeries_shutdown_IRQ,
	.enable = iSeries_enable_IRQ,
	.disable = iSeries_disable_IRQ,
	.end = iSeries_end_IRQ
};

/*
 * This is called out of iSeries_scan_slot to allocate an IRQ for an EADS slot
 * It calculates the irq value for the slot.
 * Note that subBusNumber is always 0 (at the moment at least).
 */
int __init iSeries_allocate_IRQ(HvBusNumber busNumber,
		HvSubBusNumber subBusNumber, HvAgentId deviceId)
{
	unsigned int realirq, virtirq;
	u8 idsel = (deviceId >> 4);
	u8 function = deviceId & 7;

	virtirq = next_virtual_irq++;
	realirq = ((busNumber - 1) << 6) + ((idsel - 1) << 3) + function;
	virt_irq_to_real_map[virtirq] = realirq;

	irq_desc[virtirq].handler = &iSeries_IRQ_handler;
	return virtirq;
}

int virt_irq_create_mapping(unsigned int real_irq)
{
	BUG(); /* Don't call this on iSeries, yet */

	return 0;
}

void virt_irq_init(void)
{
	return;
}
