// SPDX-License-Identifier: GPL-2.0
/*
 * JZ47xx ECC common code
 *
 * Copyright (c) 2015 Imagination Technologies
 * Author: Alex Smith <alex.smith@imgtec.com>
 */

#include <linux/clk.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include "ingenic_ecc.h"

/**
 * ingenic_ecc_calculate() - calculate ECC for a data buffer
 * @ecc: ECC device.
 * @params: ECC parameters.
 * @buf: input buffer with raw data.
 * @ecc_code: output buffer with ECC.
 *
 * Return: 0 on success, -ETIMEDOUT if timed out while waiting for ECC
 * controller.
 */
int ingenic_ecc_calculate(struct ingenic_ecc *ecc,
			  struct ingenic_ecc_params *params,
			  const u8 *buf, u8 *ecc_code)
{
	return ecc->ops->calculate(ecc, params, buf, ecc_code);
}
EXPORT_SYMBOL(ingenic_ecc_calculate);

/**
 * ingenic_ecc_correct() - detect and correct bit errors
 * @ecc: ECC device.
 * @params: ECC parameters.
 * @buf: raw data read from the chip.
 * @ecc_code: ECC read from the chip.
 *
 * Given the raw data and the ECC read from the NAND device, detects and
 * corrects errors in the data.
 *
 * Return: the number of bit errors corrected, -EBADMSG if there are too many
 * errors to correct or -ETIMEDOUT if we timed out waiting for the controller.
 */
int ingenic_ecc_correct(struct ingenic_ecc *ecc,
			struct ingenic_ecc_params *params,
			u8 *buf, u8 *ecc_code)
{
	return ecc->ops->correct(ecc, params, buf, ecc_code);
}
EXPORT_SYMBOL(ingenic_ecc_correct);

/**
 * ingenic_ecc_get() - get the ECC controller device
 * @np: ECC device tree node.
 *
 * Gets the ECC controller device from the specified device tree node. The
 * device must be released with ingenic_ecc_release() when it is no longer being
 * used.
 *
 * Return: a pointer to ingenic_ecc, errors are encoded into the pointer.
 * PTR_ERR(-EPROBE_DEFER) if the device hasn't been initialised yet.
 */
static struct ingenic_ecc *ingenic_ecc_get(struct device_node *np)
{
	struct platform_device *pdev;
	struct ingenic_ecc *ecc;

	pdev = of_find_device_by_node(np);
	if (!pdev || !platform_get_drvdata(pdev))
		return ERR_PTR(-EPROBE_DEFER);

	get_device(&pdev->dev);

	ecc = platform_get_drvdata(pdev);
	clk_prepare_enable(ecc->clk);

	return ecc;
}

/**
 * of_ingenic_ecc_get() - get the ECC controller from a DT node
 * @of_node: the node that contains an ecc-engine property.
 *
 * Get the ecc-engine property from the given device tree
 * node and pass it to ingenic_ecc_get to do the work.
 *
 * Return: a pointer to ingenic_ecc, errors are encoded into the pointer.
 * PTR_ERR(-EPROBE_DEFER) if the device hasn't been initialised yet.
 */
struct ingenic_ecc *of_ingenic_ecc_get(struct device_node *of_node)
{
	struct ingenic_ecc *ecc = NULL;
	struct device_node *np;

	np = of_parse_phandle(of_node, "ecc-engine", 0);

	/*
	 * If the ecc-engine property is not found, check for the deprecated
	 * ingenic,bch-controller property
	 */
	if (!np)
		np = of_parse_phandle(of_node, "ingenic,bch-controller", 0);

	if (np) {
		ecc = ingenic_ecc_get(np);
		of_node_put(np);
	}
	return ecc;
}
EXPORT_SYMBOL(of_ingenic_ecc_get);

/**
 * ingenic_ecc_release() - release the ECC controller device
 * @ecc: ECC device.
 */
void ingenic_ecc_release(struct ingenic_ecc *ecc)
{
	clk_disable_unprepare(ecc->clk);
	put_device(ecc->dev);
}
EXPORT_SYMBOL(ingenic_ecc_release);

int ingenic_ecc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ingenic_ecc *ecc;
	struct resource *res;

	ecc = devm_kzalloc(dev, sizeof(*ecc), GFP_KERNEL);
	if (!ecc)
		return -ENOMEM;

	ecc->ops = device_get_match_data(dev);
	if (!ecc->ops)
		return -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ecc->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(ecc->base))
		return PTR_ERR(ecc->base);

	ecc->ops->disable(ecc);

	ecc->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(ecc->clk)) {
		dev_err(dev, "failed to get clock: %ld\n", PTR_ERR(ecc->clk));
		return PTR_ERR(ecc->clk);
	}

	mutex_init(&ecc->lock);

	ecc->dev = dev;
	platform_set_drvdata(pdev, ecc);

	return 0;
}
EXPORT_SYMBOL(ingenic_ecc_probe);

MODULE_AUTHOR("Alex Smith <alex@alex-smith.me.uk>");
MODULE_AUTHOR("Harvey Hunt <harveyhuntnexus@gmail.com>");
MODULE_DESCRIPTION("Ingenic ECC common driver");
MODULE_LICENSE("GPL v2");
