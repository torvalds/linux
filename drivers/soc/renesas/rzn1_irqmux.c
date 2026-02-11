// SPDX-License-Identifier: GPL-2.0-only
/*
 * RZ/N1 GPIO Interrupt Multiplexer
 *
 * Copyright 2025 Schneider Electric
 * Author: Herve Codina <herve.codina@bootlin.com>
 */

#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <dt-bindings/interrupt-controller/arm-gic.h>

/*
 * Up to 8 output lines are connected to GIC SPI interrupt controller
 * starting at IRQ 103.
 */
#define RZN1_IRQMUX_GIC_SPI_BASE	103
#define RZN1_IRQMUX_NUM_OUTPUTS		8

static int rzn1_irqmux_parent_args_to_line_index(struct device *dev,
						 const struct of_phandle_args *parent_args)
{
	/*
	 * The parent interrupt should be one of the GIC controller.
	 * Three arguments must be provided.
	 *  - args[0]: GIC_SPI
	 *  - args[1]: The GIC interrupt number
	 *  - args[2]: The interrupt flags
	 *
	 * We retrieve the line index based on the GIC interrupt number
	 * provided.
	 */

	if (parent_args->args_count != 3 || parent_args->args[0] != GIC_SPI) {
		dev_err(dev, "Invalid interrupt-map item\n");
		return -EINVAL;
	}

	if (parent_args->args[1] < RZN1_IRQMUX_GIC_SPI_BASE ||
	    parent_args->args[1] >= RZN1_IRQMUX_GIC_SPI_BASE + RZN1_IRQMUX_NUM_OUTPUTS) {
		dev_err(dev, "Invalid GIC interrupt %u\n", parent_args->args[1]);
		return -EINVAL;
	}

	return parent_args->args[1] - RZN1_IRQMUX_GIC_SPI_BASE;
}

static int rzn1_irqmux_probe(struct platform_device *pdev)
{
	DECLARE_BITMAP(index_done, RZN1_IRQMUX_NUM_OUTPUTS) = {};
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct of_imap_parser imap_parser;
	struct of_imap_item imap_item;
	u32 __iomem *regs;
	int index;
	int ret;
	u32 tmp;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	/* We support only #interrupt-cells = <1> and #address-cells = <0> */
	ret = of_property_read_u32(np, "#interrupt-cells", &tmp);
	if (ret)
		return ret;
	if (tmp != 1)
		return -EINVAL;

	ret = of_property_read_u32(np, "#address-cells", &tmp);
	if (ret)
		return ret;
	if (tmp != 0)
		return -EINVAL;

	ret = of_imap_parser_init(&imap_parser, np, &imap_item);
	if (ret)
		return ret;

	for_each_of_imap_item(&imap_parser, &imap_item) {
		index = rzn1_irqmux_parent_args_to_line_index(dev, &imap_item.parent_args);
		if (index < 0) {
			of_node_put(imap_item.parent_args.np);
			return index;
		}

		if (test_and_set_bit(index, index_done)) {
			of_node_put(imap_item.parent_args.np);
			dev_err(dev, "Mux output line %d already defined in interrupt-map\n",
				index);
			return -EINVAL;
		}

		/*
		 * The child #address-cells is 0 (already checked). The first
		 * value in imap item is the src hwirq.
		 */
		writel(imap_item.child_imap[0], regs + index);
	}

	return 0;
}

static const struct of_device_id rzn1_irqmux_of_match[] = {
	{ .compatible = "renesas,rzn1-gpioirqmux", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rzn1_irqmux_of_match);

static struct platform_driver rzn1_irqmux_driver = {
	.probe = rzn1_irqmux_probe,
	.driver = {
		.name = "rzn1_irqmux",
		.of_match_table = rzn1_irqmux_of_match,
	},
};
module_platform_driver(rzn1_irqmux_driver);

MODULE_AUTHOR("Herve Codina <herve.codina@bootlin.com>");
MODULE_DESCRIPTION("Renesas RZ/N1 GPIO IRQ Multiplexer Driver");
MODULE_LICENSE("GPL");
