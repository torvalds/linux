/*
 *  Copyright (C) 2013 Altera Corporation
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <asm/mach/map.h>
#include <asm/cti.h>
#include <asm/pmu.h>

#include "core.h"
#include "socfpga_cti.h"

#define SOCFPGA_NUM_CTI	2

struct cti socfpga_cti_data[SOCFPGA_NUM_CTI];

irqreturn_t socfpga_pmu_handler(int irq, void *dev, irq_handler_t handler)
{
	unsigned int handled = 0;
	int i;

	for (i = 0; i < SOCFPGA_NUM_CTI; i++)
		if (irq == socfpga_cti_data[i].irq) {
			cti_irq_ack(&socfpga_cti_data[i]);
			handled = handler(irq, dev);
		}

	return IRQ_RETVAL(handled);
}

int socfpga_init_cti(struct platform_device *pdev)
{
	struct device_node *np, *np2;
	void __iomem *cti0_addr;
	void __iomem *cti1_addr;
	u32 irq0, irq1;

	np = pdev->dev.of_node;
	np2 = of_find_compatible_node(np, NULL, "arm,coresight-cti");
	if (!np2) {
		dev_err(&pdev->dev, "PMU: Unable to find coresight-cti\n");
		return -1;
	}
	cti0_addr = of_iomap(np2, 0);
	if (!cti0_addr) {
		dev_err(&pdev->dev, "PMU: ioremap for CTI failed\n");
		return -1;
	}

	irq0 = platform_get_irq(pdev, 0);
	if (irq0 < 0)
		goto free_irq0;

	np2 = of_find_compatible_node(np2, NULL, "arm,coresight-cti");
	if (!np2) {
		dev_err(&pdev->dev, "PMU: Unable to find coresight-cti\n");
		goto err_iounmap;
	}
	cti1_addr = of_iomap(np2, 0);
	if (!cti1_addr)
		goto err_iounmap;

	irq1 = platform_get_irq(pdev, 1);
	if (irq1 < 0)
		goto free_irq1;

	/*configure CTI0 for pmu irq routing*/
	cti_init(&socfpga_cti_data[0], cti0_addr,
		irq0, CTI_MPU_IRQ_TRIG_OUT);
	cti_unlock(&socfpga_cti_data[0]);
	cti_map_trigger(&socfpga_cti_data[0],
		CTI_MPU_IRQ_TRIG_IN, CTI_MPU_IRQ_TRIG_OUT, PMU_CHANNEL_0);

	/*configure CTI1 for pmu irq routing*/
	cti_init(&socfpga_cti_data[1], cti1_addr,
		irq1, CTI_MPU_IRQ_TRIG_OUT);
	cti_unlock(&socfpga_cti_data[1]);
	cti_map_trigger(&socfpga_cti_data[1],
		CTI_MPU_IRQ_TRIG_IN, CTI_MPU_IRQ_TRIG_OUT, PMU_CHANNEL_1);

	dev_info(&pdev->dev, "PMU:CTI successfully enabled\n");
	return 0;

free_irq1:
	iounmap(cti1_addr);
err_iounmap:
	free_irq(irq0, pdev);
free_irq0:
	iounmap(cti0_addr);
	return -1;
}

int socfpga_start_cti(struct platform_device *pdev)
{
	int i;

	for (i = 0; i < SOCFPGA_NUM_CTI; i++)
		cti_enable(&socfpga_cti_data[i]);

	return 0;
}

int socfpga_stop_cti(struct platform_device *pdev)
{
	int i;

	for (i = 0; i < SOCFPGA_NUM_CTI; i++)
		cti_disable(&socfpga_cti_data[i]);

	return 0;
}

