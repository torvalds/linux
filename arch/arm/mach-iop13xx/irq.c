/*
 * iop13xx IRQ handling / support functions
 * Copyright (c) 2005-2006, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/sysctl.h>
#include <linux/uaccess.h>
#include <asm/mach/irq.h>
#include <asm/irq.h>
#include <mach/hardware.h>
#include <mach/irqs.h>
#include "msi.h"

/* INTCTL0 CP6 R0 Page 4
 */
static u32 read_intctl_0(void)
{
	u32 val;
	asm volatile("mrc p6, 0, %0, c0, c4, 0":"=r" (val));
	return val;
}
static void write_intctl_0(u32 val)
{
	asm volatile("mcr p6, 0, %0, c0, c4, 0"::"r" (val));
}

/* INTCTL1 CP6 R1 Page 4
 */
static u32 read_intctl_1(void)
{
	u32 val;
	asm volatile("mrc p6, 0, %0, c1, c4, 0":"=r" (val));
	return val;
}
static void write_intctl_1(u32 val)
{
	asm volatile("mcr p6, 0, %0, c1, c4, 0"::"r" (val));
}

/* INTCTL2 CP6 R2 Page 4
 */
static u32 read_intctl_2(void)
{
	u32 val;
	asm volatile("mrc p6, 0, %0, c2, c4, 0":"=r" (val));
	return val;
}
static void write_intctl_2(u32 val)
{
	asm volatile("mcr p6, 0, %0, c2, c4, 0"::"r" (val));
}

/* INTCTL3 CP6 R3 Page 4
 */
static u32 read_intctl_3(void)
{
	u32 val;
	asm volatile("mrc p6, 0, %0, c3, c4, 0":"=r" (val));
	return val;
}
static void write_intctl_3(u32 val)
{
	asm volatile("mcr p6, 0, %0, c3, c4, 0"::"r" (val));
}

/* INTSTR0 CP6 R0 Page 5
 */
static void write_intstr_0(u32 val)
{
	asm volatile("mcr p6, 0, %0, c0, c5, 0"::"r" (val));
}

/* INTSTR1 CP6 R1 Page 5
 */
static void write_intstr_1(u32 val)
{
	asm volatile("mcr p6, 0, %0, c1, c5, 0"::"r" (val));
}

/* INTSTR2 CP6 R2 Page 5
 */
static void write_intstr_2(u32 val)
{
	asm volatile("mcr p6, 0, %0, c2, c5, 0"::"r" (val));
}

/* INTSTR3 CP6 R3 Page 5
 */
static void write_intstr_3(u32 val)
{
	asm volatile("mcr p6, 0, %0, c3, c5, 0"::"r" (val));
}

/* INTBASE CP6 R0 Page 2
 */
static void write_intbase(u32 val)
{
	asm volatile("mcr p6, 0, %0, c0, c2, 0"::"r" (val));
}

/* INTSIZE CP6 R2 Page 2
 */
static void write_intsize(u32 val)
{
	asm volatile("mcr p6, 0, %0, c2, c2, 0"::"r" (val));
}

/* 0 = Interrupt Masked and 1 = Interrupt not masked */
static void
iop13xx_irq_mask0 (struct irq_data *d)
{
	write_intctl_0(read_intctl_0() & ~(1 << (d->irq - 0)));
}

static void
iop13xx_irq_mask1 (struct irq_data *d)
{
	write_intctl_1(read_intctl_1() & ~(1 << (d->irq - 32)));
}

static void
iop13xx_irq_mask2 (struct irq_data *d)
{
	write_intctl_2(read_intctl_2() & ~(1 << (d->irq - 64)));
}

static void
iop13xx_irq_mask3 (struct irq_data *d)
{
	write_intctl_3(read_intctl_3() & ~(1 << (d->irq - 96)));
}

static void
iop13xx_irq_unmask0(struct irq_data *d)
{
	write_intctl_0(read_intctl_0() | (1 << (d->irq - 0)));
}

static void
iop13xx_irq_unmask1(struct irq_data *d)
{
	write_intctl_1(read_intctl_1() | (1 << (d->irq - 32)));
}

static void
iop13xx_irq_unmask2(struct irq_data *d)
{
	write_intctl_2(read_intctl_2() | (1 << (d->irq - 64)));
}

static void
iop13xx_irq_unmask3(struct irq_data *d)
{
	write_intctl_3(read_intctl_3() | (1 << (d->irq - 96)));
}

static struct irq_chip iop13xx_irqchip1 = {
	.name       = "IOP13xx-1",
	.irq_ack    = iop13xx_irq_mask0,
	.irq_mask   = iop13xx_irq_mask0,
	.irq_unmask = iop13xx_irq_unmask0,
};

static struct irq_chip iop13xx_irqchip2 = {
	.name       = "IOP13xx-2",
	.irq_ack    = iop13xx_irq_mask1,
	.irq_mask   = iop13xx_irq_mask1,
	.irq_unmask = iop13xx_irq_unmask1,
};

static struct irq_chip iop13xx_irqchip3 = {
	.name       = "IOP13xx-3",
	.irq_ack    = iop13xx_irq_mask2,
	.irq_mask   = iop13xx_irq_mask2,
	.irq_unmask = iop13xx_irq_unmask2,
};

static struct irq_chip iop13xx_irqchip4 = {
	.name       = "IOP13xx-4",
	.irq_ack    = iop13xx_irq_mask3,
	.irq_mask   = iop13xx_irq_mask3,
	.irq_unmask = iop13xx_irq_unmask3,
};

extern void iop_init_cp6_handler(void);

void __init iop13xx_init_irq(void)
{
	unsigned int i;

	iop_init_cp6_handler();

	/* disable all interrupts */
	write_intctl_0(0);
	write_intctl_1(0);
	write_intctl_2(0);
	write_intctl_3(0);

	/* treat all as IRQ */
	write_intstr_0(0);
	write_intstr_1(0);
	write_intstr_2(0);
	write_intstr_3(0);

	/* initialize the interrupt vector generator */
	write_intbase(INTBASE);
	write_intsize(INTSIZE_4);

	for(i = 0; i <= IRQ_IOP13XX_HPI; i++) {
		if (i < 32)
			irq_set_chip(i, &iop13xx_irqchip1);
		else if (i < 64)
			irq_set_chip(i, &iop13xx_irqchip2);
		else if (i < 96)
			irq_set_chip(i, &iop13xx_irqchip3);
		else
			irq_set_chip(i, &iop13xx_irqchip4);

		irq_set_handler(i, handle_level_irq);
		irq_clear_status_flags(i, IRQ_NOREQUEST | IRQ_NOPROBE);
	}

	iop13xx_msi_init();
}
