/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 Linus Torvalds
 * Copyright (C) 1994 - 2000 Ralf Baechle
 */
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>

#include <asm/i8259.h>
#include <asm/io.h>
#include <asm/sni.h>

DEFINE_SPINLOCK(pciasic_lock);

extern asmlinkage void sni_rm200_pci_handle_int(void);

static void enable_pciasic_irq(unsigned int irq)
{
	unsigned int mask = 1 << (irq - PCIMT_IRQ_INT2);
	unsigned long flags;

	spin_lock_irqsave(&pciasic_lock, flags);
	*(volatile u8 *) PCIMT_IRQSEL |= mask;
	spin_unlock_irqrestore(&pciasic_lock, flags);
}

static unsigned int startup_pciasic_irq(unsigned int irq)
{
	enable_pciasic_irq(irq);
	return 0; /* never anything pending */
}

#define shutdown_pciasic_irq	disable_pciasic_irq

void disable_pciasic_irq(unsigned int irq)
{
	unsigned int mask = ~(1 << (irq - PCIMT_IRQ_INT2));
	unsigned long flags;

	spin_lock_irqsave(&pciasic_lock, flags);
	*(volatile u8 *) PCIMT_IRQSEL &= mask;
	spin_unlock_irqrestore(&pciasic_lock, flags);
}

#define mask_and_ack_pciasic_irq disable_pciasic_irq

static void end_pciasic_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_pciasic_irq(irq);
}

static struct hw_interrupt_type pciasic_irq_type = {
	.typename = "ASIC-PCI",
	.startup = startup_pciasic_irq,
	.shutdown = shutdown_pciasic_irq,
	.enable = enable_pciasic_irq,
	.disable = disable_pciasic_irq,
	.ack = mask_and_ack_pciasic_irq,
	.end = end_pciasic_irq,
};

/*
 * hwint0 should deal with MP agent, ASIC PCI, EISA NMI and debug
 * button interrupts.  Later ...
 */
void pciasic_hwint0(struct pt_regs *regs)
{
	panic("Received int0 but no handler yet ...");
}

/* This interrupt was used for the com1 console on the first prototypes.  */
void pciasic_hwint2(struct pt_regs *regs)
{
	/* I think this shouldn't happen on production machines.  */
	panic("hwint2 and no handler yet");
}

/* hwint5 is the r4k count / compare interrupt  */
void pciasic_hwint5(struct pt_regs *regs)
{
	panic("hwint5 and no handler yet");
}

static unsigned int ls1bit8(unsigned int x)
{
	int b = 7, s;

	s = 4; if ((x & 0x0f) == 0) s = 0; b -= s; x <<= s;
	s = 2; if ((x & 0x30) == 0) s = 0; b -= s; x <<= s;
	s = 1; if ((x & 0x40) == 0) s = 0; b -= s;

	return b;
}

/*
 * hwint 1 deals with EISA and SCSI interrupts,
 *
 * The EISA_INT bit in CSITPEND is high active, all others are low active.
 */
void pciasic_hwint1(struct pt_regs *regs)
{
	u8 pend = *(volatile char *)PCIMT_CSITPEND;
	unsigned long flags;

	if (pend & IT_EISA) {
		int irq;
		/*
		 * Note: ASIC PCI's builtin interrupt achknowledge feature is
		 * broken.  Using it may result in loss of some or all i8259
		 * interupts, so don't use PCIMT_INT_ACKNOWLEDGE ...
		 */
		irq = i8259_irq();
		if (unlikely(irq < 0))
			return;

		do_IRQ(irq, regs);
	}

	if (!(pend & IT_SCSI)) {
		flags = read_c0_status();
		clear_c0_status(ST0_IM);
		do_IRQ(PCIMT_IRQ_SCSI, regs);
		write_c0_status(flags);
	}
}

/*
 * hwint 3 should deal with the PCI A - D interrupts,
 */
void pciasic_hwint3(struct pt_regs *regs)
{
	u8 pend = *(volatile char *)PCIMT_CSITPEND;
	int irq;

	pend &= (IT_INTA | IT_INTB | IT_INTC | IT_INTD);
	clear_c0_status(IE_IRQ3);
	irq = PCIMT_IRQ_INT2 + ls1bit8(pend);
	do_IRQ(irq, regs);
	set_c0_status(IE_IRQ3);
}

/*
 * hwint 4 is used for only the onboard PCnet 32.
 */
void pciasic_hwint4(struct pt_regs *regs)
{
	clear_c0_status(IE_IRQ4);
	do_IRQ(PCIMT_IRQ_ETHERNET, regs);
	set_c0_status(IE_IRQ4);
}

void __init init_pciasic(void)
{
	unsigned long flags;

	spin_lock_irqsave(&pciasic_lock, flags);
	* (volatile u8 *) PCIMT_IRQSEL =
		IT_EISA | IT_INTA | IT_INTB | IT_INTC | IT_INTD;
	spin_unlock_irqrestore(&pciasic_lock, flags);
}

/*
 * On systems with i8259-style interrupt controllers we assume for
 * driver compatibility reasons interrupts 0 - 15 to be the i8295
 * interrupts even if the hardware uses a different interrupt numbering.
 */
void __init arch_init_irq(void)
{
	int i;

	set_except_vector(0, sni_rm200_pci_handle_int);

	init_i8259_irqs();			/* Integrated i8259  */
	init_pciasic();

	/* Actually we've got more interrupts to handle ...  */
	for (i = PCIMT_IRQ_INT2; i <= PCIMT_IRQ_ETHERNET; i++) {
		irq_desc[i].status     = IRQ_DISABLED;
		irq_desc[i].action     = 0;
		irq_desc[i].depth      = 1;
		irq_desc[i].handler    = &pciasic_irq_type;
	}

	change_c0_status(ST0_IM, IE_IRQ1|IE_IRQ2|IE_IRQ3|IE_IRQ4);
}
