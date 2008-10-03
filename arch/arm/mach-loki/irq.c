/*
 * arch/arm/mach-loki/irq.c
 *
 * Marvell Loki (88RC8480) IRQ handling.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <asm/io.h>
#include <plat/irq.h>
#include "common.h"

void __init loki_init_irq(void)
{
	orion_irq_init(0, (void __iomem *)(IRQ_VIRT_BASE + IRQ_MASK_OFF));
}
