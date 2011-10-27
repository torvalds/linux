/*
 *  arch/arm/mach-vt8500/irq.c
 *
 *  Copyright (C) 2010 Alexey Charkov <alchark@gmail.com>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/io.h>
#include <linux/irq.h>
#include <linux/interrupt.h>

#include <asm/irq.h>

#include "devices.h"

#define VT8500_IC_DCTR		0x40		/* Destination control
						register, 64*u8 */
#define VT8500_INT_ENABLE	(1 << 3)
#define VT8500_TRIGGER_HIGH	(0 << 4)
#define VT8500_TRIGGER_RISING	(1 << 4)
#define VT8500_TRIGGER_FALLING	(2 << 4)
#define VT8500_EDGE		( VT8500_TRIGGER_RISING \
				| VT8500_TRIGGER_FALLING)
#define VT8500_IC_STATUS	0x80		/* Interrupt status, 2*u32 */

static void __iomem *ic_regbase;
static void __iomem *sic_regbase;

static void vt8500_irq_mask(struct irq_data *d)
{
	void __iomem *base = ic_regbase;
	unsigned irq = d->irq;
	u8 edge;

	if (irq >= 64) {
		base = sic_regbase;
		irq -= 64;
	}
	edge = readb(base + VT8500_IC_DCTR + irq) & VT8500_EDGE;
	if (edge) {
		void __iomem *stat_reg = base + VT8500_IC_STATUS
						+ (irq < 32 ? 0 : 4);
		unsigned status = readl(stat_reg);

		status |= (1 << (irq & 0x1f));
		writel(status, stat_reg);
	} else {
		u8 dctr = readb(base + VT8500_IC_DCTR + irq);

		dctr &= ~VT8500_INT_ENABLE;
		writeb(dctr, base + VT8500_IC_DCTR + irq);
	}
}

static void vt8500_irq_unmask(struct irq_data *d)
{
	void __iomem *base = ic_regbase;
	unsigned irq = d->irq;
	u8 dctr;

	if (irq >= 64) {
		base = sic_regbase;
		irq -= 64;
	}
	dctr = readb(base + VT8500_IC_DCTR + irq);
	dctr |= VT8500_INT_ENABLE;
	writeb(dctr, base + VT8500_IC_DCTR + irq);
}

static int vt8500_irq_set_type(struct irq_data *d, unsigned int flow_type)
{
	void __iomem *base = ic_regbase;
	unsigned irq = d->irq;
	unsigned orig_irq = irq;
	u8 dctr;

	if (irq >= 64) {
		base = sic_regbase;
		irq -= 64;
	}

	dctr = readb(base + VT8500_IC_DCTR + irq);
	dctr &= ~VT8500_EDGE;

	switch (flow_type) {
	case IRQF_TRIGGER_LOW:
		return -EINVAL;
	case IRQF_TRIGGER_HIGH:
		dctr |= VT8500_TRIGGER_HIGH;
		__irq_set_handler_locked(orig_irq, handle_level_irq);
		break;
	case IRQF_TRIGGER_FALLING:
		dctr |= VT8500_TRIGGER_FALLING;
		__irq_set_handler_locked(orig_irq, handle_edge_irq);
		break;
	case IRQF_TRIGGER_RISING:
		dctr |= VT8500_TRIGGER_RISING;
		__irq_set_handler_locked(orig_irq, handle_edge_irq);
		break;
	}
	writeb(dctr, base + VT8500_IC_DCTR + irq);

	return 0;
}

static struct irq_chip vt8500_irq_chip = {
	.name = "vt8500",
	.irq_ack = vt8500_irq_mask,
	.irq_mask = vt8500_irq_mask,
	.irq_unmask = vt8500_irq_unmask,
	.irq_set_type = vt8500_irq_set_type,
};

void __init vt8500_init_irq(void)
{
	unsigned int i;

	ic_regbase = ioremap(wmt_ic_base, SZ_64K);

	if (ic_regbase) {
		/* Enable rotating priority for IRQ */
		writel((1 << 6), ic_regbase + 0x20);
		writel(0, ic_regbase + 0x24);

		for (i = 0; i < wmt_nr_irqs; i++) {
			/* Disable all interrupts and route them to IRQ */
			writeb(0x00, ic_regbase + VT8500_IC_DCTR + i);

			irq_set_chip_and_handler(i, &vt8500_irq_chip,
						 handle_level_irq);
			set_irq_flags(i, IRQF_VALID);
		}
	} else {
		printk(KERN_ERR "Unable to remap the Interrupt Controller registers, not enabling IRQs!\n");
	}
}

void __init wm8505_init_irq(void)
{
	unsigned int i;

	ic_regbase = ioremap(wmt_ic_base, SZ_64K);
	sic_regbase = ioremap(wmt_sic_base, SZ_64K);

	if (ic_regbase && sic_regbase) {
		/* Enable rotating priority for IRQ */
		writel((1 << 6), ic_regbase + 0x20);
		writel(0, ic_regbase + 0x24);
		writel((1 << 6), sic_regbase + 0x20);
		writel(0, sic_regbase + 0x24);

		for (i = 0; i < wmt_nr_irqs; i++) {
			/* Disable all interrupts and route them to IRQ */
			if (i < 64)
				writeb(0x00, ic_regbase + VT8500_IC_DCTR + i);
			else
				writeb(0x00, sic_regbase + VT8500_IC_DCTR
								+ i - 64);

			irq_set_chip_and_handler(i, &vt8500_irq_chip,
						 handle_level_irq);
			set_irq_flags(i, IRQF_VALID);
		}
	} else {
		printk(KERN_ERR "Unable to remap the Interrupt Controller registers, not enabling IRQs!\n");
	}
}
