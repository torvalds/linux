/************************************************************************/
/* This module supports the iSeries PCI bus interrupt handling          */
/* Copyright (C) 20yy  <Robert L Holtorf> <IBM Corp>                    */
/*                                                                      */
/* This program is free software; you can redistribute it and/or modify */
/* it under the terms of the GNU General Public License as published by */
/* the Free Software Foundation; either version 2 of the License, or    */
/* (at your option) any later version.                                  */
/*                                                                      */
/* This program is distributed in the hope that it will be useful,      */ 
/* but WITHOUT ANY WARRANTY; without even the implied warranty of       */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the        */
/* GNU General Public License for more details.                         */
/*                                                                      */
/* You should have received a copy of the GNU General Public License    */ 
/* along with this program; if not, write to the:                       */
/* Free Software Foundation, Inc.,                                      */ 
/* 59 Temple Place, Suite 330,                                          */ 
/* Boston, MA  02111-1307  USA                                          */
/************************************************************************/
/* Change Activity:                                                     */
/*   Created, December 13, 2000 by Wayne Holm                           */ 
/* End Change Activity                                                  */
/************************************************************************/
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

#include <asm/iSeries/HvCallPci.h>
#include <asm/iSeries/HvCallXm.h>
#include <asm/iSeries/iSeries_irq.h>
#include <asm/iSeries/XmPciLpEvent.h>

static unsigned int iSeries_startup_IRQ(unsigned int irq);
static void iSeries_shutdown_IRQ(unsigned int irq);
static void iSeries_enable_IRQ(unsigned int irq);
static void iSeries_disable_IRQ(unsigned int irq);
static void iSeries_end_IRQ(unsigned int irq);

static hw_irq_controller iSeries_IRQ_handler = {
	.typename = "iSeries irq controller",
	.startup = iSeries_startup_IRQ,
	.shutdown = iSeries_shutdown_IRQ,
	.enable = iSeries_enable_IRQ,
	.disable = iSeries_disable_IRQ,
	.end = iSeries_end_IRQ
};

/* This maps virtual irq numbers to real irqs */
unsigned int virt_irq_to_real_map[NR_IRQS];

/* The next available virtual irq number */
/* Note: the pcnet32 driver assumes irq numbers < 2 aren't valid. :( */
static int next_virtual_irq = 2;

/* This is called by init_IRQ.  set in ppc_md.init_IRQ by iSeries_setup.c */
void __init iSeries_init_IRQ(void)
{
	/* Register PCI event handler and open an event path */
	XmPciLpEvent_init();
}

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

#define REAL_IRQ_TO_BUS(irq)	((((irq) >> 6) & 0xff) + 1)
#define REAL_IRQ_TO_IDSEL(irq)	((((irq) >> 3) & 7) + 1)
#define REAL_IRQ_TO_FUNC(irq)	((irq) & 7)

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

/*
 * Need to define this so ppc_irq_dispatch_handler will NOT call
 * enable_IRQ at the end of interrupt handling.  However, this does
 * nothing because there is not enough information provided to do
 * the EOI HvCall.  This is done by XmPciLpEvent.c
 */
static void iSeries_end_IRQ(unsigned int irq)
{
}
