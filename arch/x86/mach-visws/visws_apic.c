/*
 *	Copyright (C) 1999 Bent Hagemark, Ingo Molnar
 *
 *  SGI Visual Workstation interrupt controller
 *
 *  The Cobalt system ASIC in the Visual Workstation contains a "Cobalt" APIC
 *  which serves as the main interrupt controller in the system.  Non-legacy
 *  hardware in the system uses this controller directly.  Legacy devices
 *  are connected to the PIIX4 which in turn has its 8259(s) connected to
 *  a of the Cobalt APIC entry.
 *
 *  09/02/2000 - Updated for 2.4 by jbarnes@sgi.com
 *
 *  25/11/2002 - Updated for 2.5 by Andrey Panin <pazke@orbita1.ru>
 */

#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/apic.h>
#include <asm/i8259.h>

#include "cobalt.h"
#include "irq_vectors.h"


static DEFINE_SPINLOCK(cobalt_lock);

/*
 * Set the given Cobalt APIC Redirection Table entry to point
 * to the given IDT vector/index.
 */
static inline void co_apic_set(int entry, int irq)
{
	co_apic_write(CO_APIC_LO(entry), CO_APIC_LEVEL | (irq + FIRST_EXTERNAL_VECTOR));
	co_apic_write(CO_APIC_HI(entry), 0);
}

/*
 * Cobalt (IO)-APIC functions to handle PCI devices.
 */
static inline int co_apic_ide0_hack(void)
{
	extern char visws_board_type;
	extern char visws_board_rev;

	if (visws_board_type == VISWS_320 && visws_board_rev == 5)
		return 5;
	return CO_APIC_IDE0;
}

static int is_co_apic(unsigned int irq)
{
	if (IS_CO_APIC(irq))
		return CO_APIC(irq);

	switch (irq) {
		case 0: return CO_APIC_CPU;
		case CO_IRQ_IDE0: return co_apic_ide0_hack();
		case CO_IRQ_IDE1: return CO_APIC_IDE1;
		default: return -1;
	}
}


/*
 * This is the SGI Cobalt (IO-)APIC:
 */

static void enable_cobalt_irq(unsigned int irq)
{
	co_apic_set(is_co_apic(irq), irq);
}

static void disable_cobalt_irq(unsigned int irq)
{
	int entry = is_co_apic(irq);

	co_apic_write(CO_APIC_LO(entry), CO_APIC_MASK);
	co_apic_read(CO_APIC_LO(entry));
}

/*
 * "irq" really just serves to identify the device.  Here is where we
 * map this to the Cobalt APIC entry where it's physically wired.
 * This is called via request_irq -> setup_irq -> irq_desc->startup()
 */
static unsigned int startup_cobalt_irq(unsigned int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&cobalt_lock, flags);
	if ((irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS | IRQ_WAITING)))
		irq_desc[irq].status &= ~(IRQ_DISABLED | IRQ_INPROGRESS | IRQ_WAITING);
	enable_cobalt_irq(irq);
	spin_unlock_irqrestore(&cobalt_lock, flags);
	return 0;
}

static void ack_cobalt_irq(unsigned int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&cobalt_lock, flags);
	disable_cobalt_irq(irq);
	apic_write(APIC_EOI, APIC_EIO_ACK);
	spin_unlock_irqrestore(&cobalt_lock, flags);
}

static void end_cobalt_irq(unsigned int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&cobalt_lock, flags);
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		enable_cobalt_irq(irq);
	spin_unlock_irqrestore(&cobalt_lock, flags);
}

static struct irq_chip cobalt_irq_type = {
	.typename =	"Cobalt-APIC",
	.startup =	startup_cobalt_irq,
	.shutdown =	disable_cobalt_irq,
	.enable =	enable_cobalt_irq,
	.disable =	disable_cobalt_irq,
	.ack =		ack_cobalt_irq,
	.end =		end_cobalt_irq,
};


/*
 * This is the PIIX4-based 8259 that is wired up indirectly to Cobalt
 * -- not the manner expected by the code in i8259.c.
 *
 * there is a 'master' physical interrupt source that gets sent to
 * the CPU. But in the chipset there are various 'virtual' interrupts
 * waiting to be handled. We represent this to Linux through a 'master'
 * interrupt controller type, and through a special virtual interrupt-
 * controller. Device drivers only see the virtual interrupt sources.
 */
static unsigned int startup_piix4_master_irq(unsigned int irq)
{
	init_8259A(0);

	return startup_cobalt_irq(irq);
}

static void end_piix4_master_irq(unsigned int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&cobalt_lock, flags);
	enable_cobalt_irq(irq);
	spin_unlock_irqrestore(&cobalt_lock, flags);
}

static struct irq_chip piix4_master_irq_type = {
	.typename =	"PIIX4-master",
	.startup =	startup_piix4_master_irq,
	.ack =		ack_cobalt_irq,
	.end =		end_piix4_master_irq,
};


static struct irq_chip piix4_virtual_irq_type = {
	.typename =	"PIIX4-virtual",
	.shutdown =	disable_8259A_irq,
	.enable =	enable_8259A_irq,
	.disable =	disable_8259A_irq,
};


/*
 * PIIX4-8259 master/virtual functions to handle interrupt requests
 * from legacy devices: floppy, parallel, serial, rtc.
 *
 * None of these get Cobalt APIC entries, neither do they have IDT
 * entries. These interrupts are purely virtual and distributed from
 * the 'master' interrupt source: CO_IRQ_8259.
 *
 * When the 8259 interrupts its handler figures out which of these
 * devices is interrupting and dispatches to its handler.
 *
 * CAREFUL: devices see the 'virtual' interrupt only. Thus disable/
 * enable_irq gets the right irq. This 'master' irq is never directly
 * manipulated by any driver.
 */
static irqreturn_t piix4_master_intr(int irq, void *dev_id)
{
	int realirq;
	irq_desc_t *desc;
	unsigned long flags;

	spin_lock_irqsave(&i8259A_lock, flags);

	/* Find out what's interrupting in the PIIX4 master 8259 */
	outb(0x0c, 0x20);		/* OCW3 Poll command */
	realirq = inb(0x20);

	/*
	 * Bit 7 == 0 means invalid/spurious
	 */
	if (unlikely(!(realirq & 0x80)))
		goto out_unlock;

	realirq &= 7;

	if (unlikely(realirq == 2)) {
		outb(0x0c, 0xa0);
		realirq = inb(0xa0);

		if (unlikely(!(realirq & 0x80)))
			goto out_unlock;

		realirq = (realirq & 7) + 8;
	}

	/* mask and ack interrupt */
	cached_irq_mask |= 1 << realirq;
	if (unlikely(realirq > 7)) {
		inb(0xa1);
		outb(cached_slave_mask, 0xa1);
		outb(0x60 + (realirq & 7), 0xa0);
		outb(0x60 + 2, 0x20);
	} else {
		inb(0x21);
		outb(cached_master_mask, 0x21);
		outb(0x60 + realirq, 0x20);
	}

	spin_unlock_irqrestore(&i8259A_lock, flags);

	desc = irq_desc + realirq;

	/*
	 * handle this 'virtual interrupt' as a Cobalt one now.
	 */
	kstat_cpu(smp_processor_id()).irqs[realirq]++;

	if (likely(desc->action != NULL))
		handle_IRQ_event(realirq, desc->action);

	if (!(desc->status & IRQ_DISABLED))
		enable_8259A_irq(realirq);

	return IRQ_HANDLED;

out_unlock:
	spin_unlock_irqrestore(&i8259A_lock, flags);
	return IRQ_NONE;
}

static struct irqaction master_action = {
	.handler =	piix4_master_intr,
	.name =		"PIIX4-8259",
};

static struct irqaction cascade_action = {
	.handler = 	no_action,
	.name =		"cascade",
};


void init_VISWS_APIC_irqs(void)
{
	int i;

	for (i = 0; i < CO_IRQ_APIC0 + CO_APIC_LAST + 1; i++) {
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].action = 0;
		irq_desc[i].depth = 1;

		if (i == 0) {
			irq_desc[i].chip = &cobalt_irq_type;
		}
		else if (i == CO_IRQ_IDE0) {
			irq_desc[i].chip = &cobalt_irq_type;
		}
		else if (i == CO_IRQ_IDE1) {
			irq_desc[i].chip = &cobalt_irq_type;
		}
		else if (i == CO_IRQ_8259) {
			irq_desc[i].chip = &piix4_master_irq_type;
		}
		else if (i < CO_IRQ_APIC0) {
			irq_desc[i].chip = &piix4_virtual_irq_type;
		}
		else if (IS_CO_APIC(i)) {
			irq_desc[i].chip = &cobalt_irq_type;
		}
	}

	setup_irq(CO_IRQ_8259, &master_action);
	setup_irq(2, &cascade_action);
}
