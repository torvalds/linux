/*
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Brian Swetland <swetland@google.com>
 *	Iliyan Malchev <malchev@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/slab.h>

#include <asm/hardware/gic.h>
#include <asm/fiq.h>
#include <mach/io.h>
#include <mach/fiq.h>


// GIC ICDISR
#define GIC_DIST_SECURITY	0x080

#define FIRST_SGI_PPI_IRQ	32

static void rk30_fiq_mask(struct irq_data *d)
{
	u32 i = 0;
	void __iomem *base = RK30_GICD_BASE;

	if (d->irq < FIRST_SGI_PPI_IRQ)
		return;
	// ICDISR each bit   0 -- Secure   1--Non-Secure
	// FIQ output from secure , so set bit Non-Secure to mask the fiq
	base += GIC_DIST_SECURITY + ((d->irq / 32) << 2);
	i = readl_relaxed(base);
	i |= 1 << (d->irq % 32);
	writel_relaxed(i, base);
	dsb();
}

static void rk30_fiq_unmask(struct irq_data *d)
{
	u32 i = 0;
	void __iomem *base = RK30_GICD_BASE;

	if (d->irq < FIRST_SGI_PPI_IRQ)
		return;
	// ICDISR each bit   0 -- Secure   1--Non-Secure
	// FIQ output from secure , so set bit Secure to unmask the fiq
	base += GIC_DIST_SECURITY + ((d->irq / 32) << 2);
	i = readl_relaxed(base);
	i &= ~(1 << (d->irq % 32));
	writel_relaxed(i, base);
	dsb();
}

void rk_fiq_enable(int irq)
{
	rk30_fiq_unmask(irq_get_irq_data(irq));
	enable_fiq(irq);
}

void rk_fiq_disable(int irq)
{
	rk30_fiq_mask(irq_get_irq_data(irq));
	disable_fiq(irq);
}

void rk_irq_setpending(int irq)
{
	writel_relaxed(1<<(irq%32), RK30_GICD_BASE + GIC_DIST_PENDING_SET + (irq/32)*4);
	dsb();
}

void rk_irq_clearpending(int irq)
{
	writel_relaxed(1<<(irq%32), RK30_GICD_BASE + GIC_DIST_PENDING_CLEAR + (irq/32)*4);
	dsb();
}

void rk30_fiq_init(void)
{
	unsigned int gic_irqs, i;

	// read gic info to know how many irqs in our chip
	gic_irqs = readl_relaxed(RK30_GICD_BASE + GIC_DIST_CTR) & 0x1f;
	// set all the interrupt to non-secure state
	for (i = 0; i < (gic_irqs + 1); i++) {
		writel_relaxed(0xffffffff, RK30_GICD_BASE + GIC_DIST_SECURITY + (i<<2));
	}
	dsb();
	writel_relaxed(0x3, RK30_GICD_BASE + GIC_DIST_CTRL);
	writel_relaxed(0x0f, RK30_GICC_BASE + GIC_CPU_CTRL);
	//set the uart 2(the debug port) priority a little higher than other interrupts
	writel_relaxed(0xa0a0a090, RK30_GICD_BASE + GIC_DIST_PRI + (IRQ_UART2/4)*4);
	dsb();
}
