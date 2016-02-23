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

#ifndef _IRQ_GIC_COMMON_H
#define _IRQ_GIC_COMMON_H

#include <linux/of.h>
#include <linux/irqdomain.h>

struct gic_quirk {
	const char *desc;
	void (*init)(void *data);
	u32 iidr;
	u32 mask;
};

int gic_configure_irq(unsigned int irq, unsigned int type,
                       void __iomem *base, void (*sync_access)(void));
void gic_dist_config(void __iomem *base, int gic_irqs,
		     void (*sync_access)(void));
void gic_cpu_config(void __iomem *base, void (*sync_access)(void));
void gic_enable_quirks(u32 iidr, const struct gic_quirk *quirks,
		void *data);

#endif /* _IRQ_GIC_COMMON_H */
