/*
 * Copyright Altera Corporation (C) 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms and conditions of the GNU General Public License,
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

#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <asm/mach/map.h>
#include <asm/cti.h>
#include <asm/pmu.h>

#include "core.h"
#include "socfpga_cti.h"

#define SOCFPGA_MAX_NUM_CTI	8

struct socfpga_cti {
	struct cti socfpga_cti_data;
	void __iomem *cti_addr;
	int irq;
};

struct socfpga_cti_device {
	struct device *dev;
	struct socfpga_cti *socfpga_cti[SOCFPGA_MAX_NUM_CTI];
	int socfpga_ncores;
};

irqreturn_t socfpga_pmu_handler(int irq, void *dev, irq_handler_t handler)
{
	struct arm_pmu *armpmu = (struct arm_pmu *)dev;
	struct platform_device *pdev = armpmu->plat_device;
	struct socfpga_cti_device *socfpga_cti_device =
					platform_get_drvdata(pdev);
	int ncores = socfpga_cti_device->socfpga_ncores;
	struct cti *cti;
	int irq_local;
	unsigned int handled = 0;
	int i;

	for (i = 0; i < ncores; i++) {
		irq_local = socfpga_cti_device->socfpga_cti[i]->irq;
		cti = &socfpga_cti_device->socfpga_cti[i]->socfpga_cti_data;
		if (irq == irq_local)
			cti_irq_ack(cti);
	}

	handled = handler(irq, dev);

	return IRQ_RETVAL(handled);
}

int socfpga_init_cti(struct platform_device *pdev)
{
	struct device_node *np, *np2;
	struct socfpga_cti_device *socfpga_cti_device;
	struct socfpga_cti *socfpga_cti, *socfpga_cti_error;
	void __iomem *cti_addr;
	int ncores;
	int i;

	socfpga_cti_device = devm_kzalloc(&pdev->dev,
					sizeof(*socfpga_cti_device),
					GFP_KERNEL);
	if (!socfpga_cti_device)
		return -ENOMEM;

	ncores = num_online_cpus();
	socfpga_cti_device->socfpga_ncores = ncores;

	socfpga_cti_device->dev = &pdev->dev;

	np = pdev->dev.of_node;

	socfpga_cti = kzalloc(sizeof(socfpga_cti) * ncores, GFP_KERNEL);
	if (!socfpga_cti)
		return -ENOMEM;
	socfpga_cti_error = socfpga_cti;

	for (i = 0; i < socfpga_cti_device->socfpga_ncores; i++) {
		np2 = of_find_compatible_node(np, NULL, "arm,coresight-cti");

		cti_addr = of_iomap(np2, 0);
		if (!cti_addr) {
			dev_err(&pdev->dev, "PMU: ioremap for CTI failed\n");
			kfree(socfpga_cti_error);
			return -ENOMEM;
		}
		socfpga_cti->cti_addr = cti_addr;

		socfpga_cti->irq = platform_get_irq(pdev, i);

		/*configure CTI0 for pmu irq routing*/
		cti_init(&socfpga_cti->socfpga_cti_data, socfpga_cti->cti_addr,
				socfpga_cti->irq, CTI_MPU_IRQ_TRIG_OUT);
		cti_unlock(&socfpga_cti->socfpga_cti_data);
		cti_map_trigger(&socfpga_cti->socfpga_cti_data,
			CTI_MPU_IRQ_TRIG_IN, CTI_MPU_IRQ_TRIG_OUT,
			PMU_CHANNEL_0);

		socfpga_cti_device->socfpga_cti[i] = socfpga_cti;
		socfpga_cti += sizeof(socfpga_cti);
	}
	platform_set_drvdata(pdev, socfpga_cti_device);

	dev_info(&pdev->dev, "PMU:CTI successfully enabled for %d cores\n",
				socfpga_cti_device->socfpga_ncores);
	return 0;
}

int socfpga_start_cti(struct platform_device *pdev)
{
	struct socfpga_cti_device *cti_dev = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < cti_dev->socfpga_ncores; i++)
		 cti_enable(&cti_dev->socfpga_cti[i]->socfpga_cti_data);

	return 0;
}

int socfpga_stop_cti(struct platform_device *pdev)
{
	struct socfpga_cti_device *cti_dev = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < cti_dev->socfpga_ncores; i++)
		cti_disable(&cti_dev->socfpga_cti[i]->socfpga_cti_data);

	return 0;
}
