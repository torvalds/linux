/*
 * arch/arm/mach-mv78xx0/irq.c
 *
 * MV78xx0 IRQ handling.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <mach/mv78xx0.h>
#include <plat/irq.h>
#include "common.h"

void __init mv78xx0_init_irq(void)
{
	orion_irq_init(0, (void __iomem *)(IRQ_VIRT_BASE + IRQ_MASK_LOW_OFF));
	orion_irq_init(32, (void __iomem *)(IRQ_VIRT_BASE + IRQ_MASK_HIGH_OFF));
}
