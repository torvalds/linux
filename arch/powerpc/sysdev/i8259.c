/*
 * i8259 interrupt controller driver.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <asm/i8259.h>

static volatile void __iomem *pci_intack; /* RO, gives us the irq vector */

static unsigned char cached_8259[2] = { 0xff, 0xff };
#define cached_A1 (cached_8259[0])
#define cached_21 (cached_8259[1])

static DEFINE_SPINLOCK(i8259_lock);

static int i8259_pic_irq_offset;

/*
 * Acknowledge the IRQ using either the PCI host bridge's interrupt
 * acknowledge feature or poll.  How i8259_init() is called determines
 * which is called.  It should be noted that polling is broken on some
 * IBM and Motorola PReP boxes so we must use the int-ack feature on them.
 */
int i8259_irq(struct pt_regs *regs)
{
	int irq;

	spin_lock(&i8259_lock);

	/* Either int-ack or poll for the IRQ */
	if (pci_intack)
		irq = readb(pci_intack);
	else {
		/* Perform an interrupt acknowledge cycle on controller 1. */
		outb(0x0C, 0x20);		/* prepare for poll */
		irq = inb(0x20) & 7;
		if (irq == 2 ) {
			/*
			 * Interrupt is cascaded so perform interrupt
			 * acknowledge on controller 2.
			 */
			outb(0x0C, 0xA0);	/* prepare for poll */
			irq = (inb(0xA0) & 7) + 8;
		}
	}

	if (irq == 7) {
		/*
		 * This may be a spurious interrupt.
		 *
		 * Read the interrupt status register (ISR). If the most
		 * significant bit is not set then there is no valid
		 * interrupt.
		 */
		if (!pci_intack)
			outb(0x0B, 0x20);	/* ISR register */
		if(~inb(0x20) & 0x80)
			irq = -1;
	}

	spin_unlock(&i8259_lock);
	return irq + i8259_pic_irq_offset;
}

int i8259_irq_cascade(struct pt_regs *regs, void *unused)
{
	return i8259_irq(regs);
}

static void i8259_mask_and_ack_irq(unsigned int irq_nr)
{
	unsigned long flags;

	spin_lock_irqsave(&i8259_lock, flags);
	irq_nr -= i8259_pic_irq_offset;
	if (irq_nr > 7) {
		cached_A1 |= 1 << (irq_nr-8);
		inb(0xA1); 	/* DUMMY */
		outb(cached_A1, 0xA1);
		outb(0x20, 0xA0);	/* Non-specific EOI */
		outb(0x20, 0x20);	/* Non-specific EOI to cascade */
	} else {
		cached_21 |= 1 << irq_nr;
		inb(0x21); 	/* DUMMY */
		outb(cached_21, 0x21);
		outb(0x20, 0x20);	/* Non-specific EOI */
	}
	spin_unlock_irqrestore(&i8259_lock, flags);
}

static void i8259_set_irq_mask(int irq_nr)
{
	outb(cached_A1,0xA1);
	outb(cached_21,0x21);
}

static void i8259_mask_irq(unsigned int irq_nr)
{
	unsigned long flags;

	spin_lock_irqsave(&i8259_lock, flags);
	irq_nr -= i8259_pic_irq_offset;
	if (irq_nr < 8)
		cached_21 |= 1 << irq_nr;
	else
		cached_A1 |= 1 << (irq_nr-8);
	i8259_set_irq_mask(irq_nr);
	spin_unlock_irqrestore(&i8259_lock, flags);
}

static void i8259_unmask_irq(unsigned int irq_nr)
{
	unsigned long flags;

	spin_lock_irqsave(&i8259_lock, flags);
	irq_nr -= i8259_pic_irq_offset;
	if (irq_nr < 8)
		cached_21 &= ~(1 << irq_nr);
	else
		cached_A1 &= ~(1 << (irq_nr-8));
	i8259_set_irq_mask(irq_nr);
	spin_unlock_irqrestore(&i8259_lock, flags);
}

static void i8259_end_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS))
	    && irq_desc[irq].action)
		i8259_unmask_irq(irq);
}

struct hw_interrupt_type i8259_pic = {
	.typename = " i8259    ",
	.enable = i8259_unmask_irq,
	.disable = i8259_mask_irq,
	.ack = i8259_mask_and_ack_irq,
	.end = i8259_end_irq,
};

static struct resource pic1_iores = {
	.name = "8259 (master)",
	.start = 0x20,
	.end = 0x21,
	.flags = IORESOURCE_BUSY,
};

static struct resource pic2_iores = {
	.name = "8259 (slave)",
	.start = 0xa0,
	.end = 0xa1,
	.flags = IORESOURCE_BUSY,
};

static struct resource pic_edgectrl_iores = {
	.name = "8259 edge control",
	.start = 0x4d0,
	.end = 0x4d1,
	.flags = IORESOURCE_BUSY,
};

static struct irqaction i8259_irqaction = {
	.handler = no_action,
	.flags = SA_INTERRUPT,
	.mask = CPU_MASK_NONE,
	.name = "82c59 secondary cascade",
};

/*
 * i8259_init()
 * intack_addr - PCI interrupt acknowledge (real) address which will return
 *               the active irq from the 8259
 */
void __init i8259_init(unsigned long intack_addr, int offset)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&i8259_lock, flags);
	i8259_pic_irq_offset = offset;

	/* init master interrupt controller */
	outb(0x11, 0x20); /* Start init sequence */
	outb(0x00, 0x21); /* Vector base */
	outb(0x04, 0x21); /* edge tiggered, Cascade (slave) on IRQ2 */
	outb(0x01, 0x21); /* Select 8086 mode */

	/* init slave interrupt controller */
	outb(0x11, 0xA0); /* Start init sequence */
	outb(0x08, 0xA1); /* Vector base */
	outb(0x02, 0xA1); /* edge triggered, Cascade (slave) on IRQ2 */
	outb(0x01, 0xA1); /* Select 8086 mode */

	/* always read ISR */
	outb(0x0B, 0x20);
	outb(0x0B, 0xA0);

	/* Mask all interrupts */
	outb(cached_A1, 0xA1);
	outb(cached_21, 0x21);

	spin_unlock_irqrestore(&i8259_lock, flags);

	/* reserve our resources */
	setup_irq(offset + 2, &i8259_irqaction);
	request_resource(&ioport_resource, &pic1_iores);
	request_resource(&ioport_resource, &pic2_iores);
	request_resource(&ioport_resource, &pic_edgectrl_iores);

	if (intack_addr != 0)
		pci_intack = ioremap(intack_addr, 1);

	for (i = 0; i < NUM_ISA_INTERRUPTS; ++i)
		irq_desc[offset + i].handler = &i8259_pic;
}
