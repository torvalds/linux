/*
 * Copyright (C) 2002 ARM Limited, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip/arm-gic.h>

#include "irq-gic-common.h"

int gic_configure_irq(unsigned int irq, unsigned int type,
		       void __iomem *base, void (*sync_access)(void))
{
	u32 confmask = 0x2 << ((irq % 16) * 2);
	u32 confoff = (irq / 16) * 4;
	u32 val, oldval;
	int ret = 0;

	/*
	 * Read current configuration register, and insert the config
	 * for "irq", depending on "type".
	 */
	val = oldval = readl_relaxed(base + GIC_DIST_CONFIG + confoff);
	if (type & IRQ_TYPE_LEVEL_MASK)
		val &= ~confmask;
	else if (type & IRQ_TYPE_EDGE_BOTH)
		val |= confmask;

	/*
	 * Write back the new configuration, and possibly re-enable
	 * the interrupt. If we tried to write a new configuration and failed,
	 * return an error.
	 */
	writel_relaxed(val, base + GIC_DIST_CONFIG + confoff);
	if (readl_relaxed(base + GIC_DIST_CONFIG + confoff) != val && val != oldval)
		ret = -EINVAL;

	if (sync_access)
		sync_access();

	return ret;
}

void __init gic_dist_config(void __iomem *base, int gic_irqs,
			    void (*sync_access)(void))
{
	unsigned int i;

	/*
	 * Set all global interrupts to be level triggered, active low.
	 */
	for (i = 32; i < gic_irqs; i += 16)
		writel_relaxed(GICD_INT_ACTLOW_LVLTRIG,
					base + GIC_DIST_CONFIG + i / 4);

	/*
	 * Set priority on all global interrupts.
	 */
	for (i = 32; i < gic_irqs; i += 4)
		writel_relaxed(GICD_INT_DEF_PRI_X4, base + GIC_DIST_PRI + i);

	/*
	 * Disable all interrupts.  Leave the PPI and SGIs alone
	 * as they are enabled by redistributor registers.
	 */
	for (i = 32; i < gic_irqs; i += 32)
		writel_relaxed(GICD_INT_EN_CLR_X32,
					base + GIC_DIST_ENABLE_CLEAR + i / 8);

	if (sync_access)
		sync_access();
}

void gic_cpu_config(void __iomem *base, void (*sync_access)(void))
{
	int i;

	/*
	 * Deal with the banked PPI and SGI interrupts - disable all
	 * PPI interrupts, ensure all SGI interrupts are enabled.
	 */
	writel_relaxed(GICD_INT_EN_CLR_PPI, base + GIC_DIST_ENABLE_CLEAR);
	writel_relaxed(GICD_INT_EN_SET_SGI, base + GIC_DIST_ENABLE_SET);

	/*
	 * Set priority on PPI and SGI interrupts
	 */
	for (i = 0; i < 32; i += 4)
		writel_relaxed(GICD_INT_DEF_PRI_X4,
					base + GIC_DIST_PRI + i * 4 / 4);

	if (sync_access)
		sync_access();
}
