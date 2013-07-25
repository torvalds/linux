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

#include <linux/init.h>
#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/irqchip/arm-gic.h>

static void __init r8a7740_init_irq_common(void)
{
	void __iomem *intc_prio_base = ioremap_nocache(0xe6900010, 0x10);
	void __iomem *intc_msk_base = ioremap_nocache(0xe6900040, 0x10);
	void __iomem *pfc_inta_ctrl = ioremap_nocache(0xe605807c, 0x4);

	/* route signals to GIC */
	iowrite32(0x0, pfc_inta_ctrl);

	/*
	 * To mask the shared interrupt to SPI 149 we must ensure to set
	 * PRIO *and* MASK. Else we run into IRQ floods when registering
	 * the intc_irqpin devices
	 */
	iowrite32(0x0, intc_prio_base + 0x0);
	iowrite32(0x0, intc_prio_base + 0x4);
	iowrite32(0x0, intc_prio_base + 0x8);
	iowrite32(0x0, intc_prio_base + 0xc);
	iowrite8(0xff, intc_msk_base + 0x0);
	iowrite8(0xff, intc_msk_base + 0x4);
	iowrite8(0xff, intc_msk_base + 0x8);
	iowrite8(0xff, intc_msk_base + 0xc);

	iounmap(intc_prio_base);
	iounmap(intc_msk_base);
	iounmap(pfc_inta_ctrl);
}

void __init r8a7740_init_irq_of(void)
{
	irqchip_init();
	r8a7740_init_irq_common();
}

void __init r8a7740_init_irq(void)
{
	void __iomem *gic_dist_base = ioremap_nocache(0xc2800000, 0x1000);
	void __iomem *gic_cpu_base = ioremap_nocache(0xc2000000, 0x1000);

	/* initialize the Generic Interrupt Controller PL390 r0p0 */
	gic_init(0, 29, gic_dist_base, gic_cpu_base);
	r8a7740_init_irq_common();
}
