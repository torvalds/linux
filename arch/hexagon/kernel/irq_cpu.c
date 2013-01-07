/*
 * First-level interrupt controller model for Hexagon.
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/interrupt.h>
#include <asm/irq.h>
#include <asm/hexagon_vm.h>

static void mask_irq(struct irq_data *data)
{
	__vmintop_locdis((long) data->irq);
}

static void mask_irq_num(unsigned int irq)
{
	__vmintop_locdis((long) irq);
}

static void unmask_irq(struct irq_data *data)
{
	__vmintop_locen((long) data->irq);
}

/*  This is actually all we need for handle_fasteoi_irq  */
static void eoi_irq(struct irq_data *data)
{
	__vmintop_globen((long) data->irq);
}

/* Power mamangement wake call. We don't need this, however,
 * if this is absent, then an -ENXIO error is returned to the
 * msm_serial driver, and it fails to correctly initialize.
 * This is a bug in the msm_serial driver, but, for now, we
 * work around it here, by providing this bogus handler.
 * XXX FIXME!!! remove this when msm_serial is fixed.
 */
static int set_wake(struct irq_data *data, unsigned int on)
{
	return 0;
}

static struct irq_chip hexagon_irq_chip = {
	.name		= "HEXAGON",
	.irq_mask	= mask_irq,
	.irq_unmask	= unmask_irq,
	.irq_set_wake	= set_wake,
	.irq_eoi	= eoi_irq
};

/**
 * The hexagon core comes with a first-level interrupt controller
 * with 32 total possible interrupts.  When the core is embedded
 * into different systems/platforms, it is typically wrapped by
 * macro cells that provide one or more second-level interrupt
 * controllers that are cascaded into one or more of the first-level
 * interrupts handled here. The precise wiring of these other
 * irqs varies from platform to platform, and are set up & configured
 * in the platform-specific files.
 *
 * The first-level interrupt controller is wrapped by the VM, which
 * virtualizes the interrupt controller for us.  It provides a very
 * simple, fast & efficient API, and so the fasteoi handler is
 * appropriate for this case.
 */
void __init init_IRQ(void)
{
	int irq;

	for (irq = 0; irq < HEXAGON_CPUINTS; irq++) {
		mask_irq_num(irq);
		irq_set_chip_and_handler(irq, &hexagon_irq_chip,
						 handle_fasteoi_irq);
	}
}
