/*
 *  arch/arm/mach-socfpga/dma.c
 *
 * Copyright Altera Corporation (C) 2013. All rights reserved.
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
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/dma-mapping.h>
#include <linux/amba/bus.h>
#include <linux/amba/pl330.h>
#include <linux/interrupt.h>
#include <linux/export.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <asm/irq.h>
#include <linux/of.h>

#include "dma.h"

static void combiner_func(struct irq_data *data) { }

static unsigned int combiner_ret(struct irq_data *data)
{
	return 0;
}

struct irq_chip combiner_irq_chip = {
	.name = "combiner",
	.irq_startup = combiner_ret,
	.irq_shutdown = combiner_func,
	.irq_enable = combiner_func,
	.irq_disable = combiner_func,
	.irq_ack = combiner_func,
	.irq_mask = combiner_func,
	.irq_unmask = combiner_func,
};

static irqreturn_t socfpga_dma_irq_handler(int irq, void *data)
{
	struct amba_device *adev = (struct amba_device *) data;
	int dev_irq = adev->irq[0];
	if (generic_handle_irq(dev_irq) == 0)
		return IRQ_HANDLED;
	return IRQ_NONE;
}

static int pl330_dma_platform_init(struct amba_device *adev)
{
	struct dma_pl330_platdata *pdat;
	int irq, irq_alloc;
	int ret;
	struct device_node *np = adev->dev.of_node;
	unsigned int prop;

	pdat = adev->dev.platform_data;

	dma_cap_set(DMA_MEMCPY, pdat->cap_mask);
	dma_cap_set(DMA_SLAVE, pdat->cap_mask);
	dma_cap_set(DMA_CYCLIC, pdat->cap_mask);

	pdat->irq_start = adev->irq[0];

	if (of_property_read_u32(np, "nr-irqs", &prop))
		return -1;
	pdat->irq_end = pdat->irq_start + prop - 1;

	if (of_property_read_u32(np, "nr-valid-peri", &prop))
		return -1;
	pdat->nr_valid_peri = prop;

	/* Hook all the dma interrupts into our irq combiner handler */
	for (irq = pdat->irq_start; irq <= pdat->irq_end; irq++) {
		ret = request_irq(irq, socfpga_dma_irq_handler, 0,
			dev_name(&adev->dev), adev);

		if (ret) {
			irq_alloc = irq;
			goto request_irq_err;
		}
	}

	/* now that we have the interrupts hooked, get a combiner
		irq (virtual irq) from the system and use that as
		the main irq */
	adev->irq[0] = irq_alloc_desc(0);
	irq_set_chip_and_handler(adev->irq[0], &combiner_irq_chip,
		handle_simple_irq);
	set_irq_flags(adev->irq[0], IRQF_VALID);

	return 0;

request_irq_err:
	for (irq = pdat->irq_start; irq < irq_alloc; irq++)
		free_irq(irq, adev);

	return ret;
}

static void pl330_dma_platform_exit(struct amba_device *adev)
{
	struct dma_pl330_platdata *pdat;
	int irq;

	pdat = adev->dev.platform_data;

	for (irq = pdat->irq_start; irq <= pdat->irq_end; irq++)
		free_irq(irq, adev);

	irq_set_chip_and_handler(adev->irq[0], NULL, NULL);
	irq_free_descs(adev->irq[0], 1);

}

struct dma_pl330_platdata dma_platform_data = {
	.init = pl330_dma_platform_init,
	.exit = pl330_dma_platform_exit,
};
