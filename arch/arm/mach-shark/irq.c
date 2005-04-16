/*
 *  linux/arch/arm/mach-shark/irq.c
 *
 * by Alexander Schulz
 *
 * derived from linux/arch/ppc/kernel/i8259.c and:
 * include/asm-arm/arch-ebsa110/irq.h
 * Copyright (C) 1996-1998 Russell King
 */

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/ptrace.h>
#include <linux/interrupt.h>

#include <asm/irq.h>
#include <asm/io.h>
#include <asm/mach/irq.h>

/*
 * 8259A PIC functions to handle ISA devices:
 */

/*
 * This contains the irq mask for both 8259A irq controllers,
 * Let through the cascade-interrupt no. 2 (ff-(1<<2)==fb)
 */
static unsigned char cached_irq_mask[2] = { 0xfb, 0xff };

/*
 * These have to be protected by the irq controller spinlock
 * before being called.
 */
static void shark_disable_8259A_irq(unsigned int irq)
{
	unsigned int mask;
	if (irq<8) {
	  mask = 1 << irq;
	  cached_irq_mask[0] |= mask;
	  outb(cached_irq_mask[1],0xA1);
	} else {
	  mask = 1 << (irq-8);
	  cached_irq_mask[1] |= mask;
	  outb(cached_irq_mask[0],0x21);
	}
}

static void shark_enable_8259A_irq(unsigned int irq)
{
	unsigned int mask;
	if (irq<8) {
	  mask = ~(1 << irq);
	  cached_irq_mask[0] &= mask;
	  outb(cached_irq_mask[0],0x21);
	} else {
	  mask = ~(1 << (irq-8));
	  cached_irq_mask[1] &= mask;
	  outb(cached_irq_mask[1],0xA1);
	}
}

static void shark_ack_8259A_irq(unsigned int irq){}

static irqreturn_t bogus_int(int irq, void *dev_id, struct pt_regs *regs)
{
	printk("Got interrupt %i!\n",irq);
	return IRQ_NONE;
}

static struct irqaction cascade;

static struct irqchip fb_chip = {
	.ack	= shark_ack_8259A_irq,
	.mask	= shark_disable_8259A_irq,
	.unmask = shark_enable_8259A_irq,
};

void __init shark_init_irq(void)
{
	int irq;

	for (irq = 0; irq < NR_IRQS; irq++) {
		set_irq_chip(irq, &fb_chip);
		set_irq_handler(irq, do_edge_IRQ);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}

	/* init master interrupt controller */
	outb(0x11, 0x20); /* Start init sequence, edge triggered (level: 0x19)*/
	outb(0x00, 0x21); /* Vector base */
	outb(0x04, 0x21); /* Cascade (slave) on IRQ2 */
	outb(0x03, 0x21); /* Select 8086 mode , auto eoi*/
	outb(0x0A, 0x20);
	/* init slave interrupt controller */
	outb(0x11, 0xA0); /* Start init sequence, edge triggered */
	outb(0x08, 0xA1); /* Vector base */
	outb(0x02, 0xA1); /* Cascade (slave) on IRQ2 */
	outb(0x03, 0xA1); /* Select 8086 mode, auto eoi */
	outb(0x0A, 0xA0);
	outb(cached_irq_mask[1],0xA1);
	outb(cached_irq_mask[0],0x21);
	//request_region(0x20,0x2,"pic1");
	//request_region(0xA0,0x2,"pic2");

	cascade.handler = bogus_int;
	cascade.name = "cascade";
	setup_irq(2,&cascade);
}

