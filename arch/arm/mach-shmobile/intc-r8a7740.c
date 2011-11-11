/*
 * R8A7740 processor support
 *
 * Copyright (C) 2011  Renesas Solutions Corp.
 * Copyright (C) 2011  Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/irq.h>
#include <linux/io.h>
#include <asm/hardware/gic.h>

#define INTA_CTRL	0xe605807c

static int r8a7740_set_wake(struct irq_data *data, unsigned int on)
{
	return 0; /* always allow wakeup */
}

void __init r8a7740_init_irq(void)
{
	void __iomem *gic_dist_base = __io(0xf0001000);
	void __iomem *gic_cpu_base = __io(0xf0000000);

	/*
	 * Change INT_SEL INTCA->GIC
	 * (on GPIO)
	 */
	__raw_writel(__raw_readl(INTA_CTRL) & ~(1 << 1), INTA_CTRL);

	gic_init(0, 29, gic_dist_base, gic_cpu_base);
	gic_arch_extn.irq_set_wake = r8a7740_set_wake;
}
