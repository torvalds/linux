// SPDX-License-Identifier: GPL-2.0-only
/*
 * Routines common to most mpc85xx-based boards.
 */

#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

#include <asm/fsl_pm.h>
#include <soc/fsl/qe/qe.h>
#include <sysdev/cpm2_pic.h>

#include "mpc85xx.h"

const struct fsl_pm_ops *qoriq_pm_ops;

static const struct of_device_id mpc85xx_common_ids[] __initconst = {
	{ .type = "soc", },
	{ .compatible = "soc", },
	{ .compatible = "simple-bus", },
	{ .name = "cpm", },
	{ .name = "localbus", },
	{ .compatible = "gianfar", },
	{ .compatible = "fsl,qe", },
	{ .compatible = "fsl,cpm2", },
	{ .compatible = "fsl,srio", },
	/* So that the DMA channel nodes can be probed individually: */
	{ .compatible = "fsl,eloplus-dma", },
	/* For the PMC driver */
	{ .compatible = "fsl,mpc8548-guts", },
	/* Probably unnecessary? */
	{ .compatible = "gpio-leds", },
	/* For all PCI controllers */
	{ .compatible = "fsl,mpc8540-pci", },
	{ .compatible = "fsl,mpc8548-pcie", },
	{ .compatible = "fsl,p1022-pcie", },
	{ .compatible = "fsl,p1010-pcie", },
	{ .compatible = "fsl,p1023-pcie", },
	{ .compatible = "fsl,p4080-pcie", },
	{ .compatible = "fsl,qoriq-pcie-v2.4", },
	{ .compatible = "fsl,qoriq-pcie-v2.3", },
	{ .compatible = "fsl,qoriq-pcie-v2.2", },
	{ .compatible = "fsl,fman", },
	{},
};

int __init mpc85xx_common_publish_devices(void)
{
	return of_platform_bus_probe(NULL, mpc85xx_common_ids, NULL);
}
#ifdef CONFIG_CPM2
static void cpm2_cascade(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	int cascade_irq;

	while ((cascade_irq = cpm2_get_irq()) >= 0)
		generic_handle_irq(cascade_irq);

	chip->irq_eoi(&desc->irq_data);
}


void __init mpc85xx_cpm2_pic_init(void)
{
	struct device_node *np;
	int irq;

	/* Setup CPM2 PIC */
	np = of_find_compatible_node(NULL, NULL, "fsl,cpm2-pic");
	if (np == NULL) {
		printk(KERN_ERR "PIC init: can not find fsl,cpm2-pic node\n");
		return;
	}
	irq = irq_of_parse_and_map(np, 0);
	if (!irq) {
		of_node_put(np);
		printk(KERN_ERR "PIC init: got no IRQ for cpm cascade\n");
		return;
	}

	cpm2_pic_init(np);
	of_node_put(np);
	irq_set_chained_handler(irq, cpm2_cascade);
}
#endif

#ifdef CONFIG_QUICC_ENGINE
void __init mpc85xx_qe_par_io_init(void)
{
	struct device_node *np;

	np = of_find_node_by_name(NULL, "par_io");
	if (np) {
		struct device_node *ucc;

		par_io_init(np);
		of_node_put(np);

		for_each_node_by_name(ucc, "ucc")
			par_io_of_config(ucc);

	}
}
#endif
