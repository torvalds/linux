// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2011-2012 Calxeda, Inc.
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/edac.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "edac_module.h"

#define SR_CLR_SB_ECC_INTR	0x0
#define SR_CLR_DB_ECC_INTR	0x4

struct hb_l2_drvdata {
	void __iomem *base;
	int sb_irq;
	int db_irq;
};

static irqreturn_t highbank_l2_err_handler(int irq, void *dev_id)
{
	struct edac_device_ctl_info *dci = dev_id;
	struct hb_l2_drvdata *drvdata = dci->pvt_info;

	if (irq == drvdata->sb_irq) {
		writel(1, drvdata->base + SR_CLR_SB_ECC_INTR);
		edac_device_handle_ce(dci, 0, 0, dci->ctl_name);
	}
	if (irq == drvdata->db_irq) {
		writel(1, drvdata->base + SR_CLR_DB_ECC_INTR);
		edac_device_handle_ue(dci, 0, 0, dci->ctl_name);
	}

	return IRQ_HANDLED;
}

static const struct of_device_id hb_l2_err_of_match[] = {
	{ .compatible = "calxeda,hb-sregs-l2-ecc", },
	{},
};
MODULE_DEVICE_TABLE(of, hb_l2_err_of_match);

static int highbank_l2_err_probe(struct platform_device *pdev)
{
	const struct of_device_id *id;
	struct edac_device_ctl_info *dci;
	struct hb_l2_drvdata *drvdata;
	struct resource *r;
	int res = 0;

	dci = edac_device_alloc_ctl_info(sizeof(*drvdata), "cpu",
		1, "L", 1, 2, NULL, 0, 0);
	if (!dci)
		return -ENOMEM;

	drvdata = dci->pvt_info;
	dci->dev = &pdev->dev;
	platform_set_drvdata(pdev, dci);

	if (!devres_open_group(&pdev->dev, NULL, GFP_KERNEL))
		return -ENOMEM;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(&pdev->dev, "Unable to get mem resource\n");
		res = -ENODEV;
		goto err;
	}

	if (!devm_request_mem_region(&pdev->dev, r->start,
				     resource_size(r), dev_name(&pdev->dev))) {
		dev_err(&pdev->dev, "Error while requesting mem region\n");
		res = -EBUSY;
		goto err;
	}

	drvdata->base = devm_ioremap(&pdev->dev, r->start, resource_size(r));
	if (!drvdata->base) {
		dev_err(&pdev->dev, "Unable to map regs\n");
		res = -ENOMEM;
		goto err;
	}

	id = of_match_device(hb_l2_err_of_match, &pdev->dev);
	dci->mod_name = pdev->dev.driver->name;
	dci->ctl_name = id ? id->compatible : "unknown";
	dci->dev_name = dev_name(&pdev->dev);

	if (edac_device_add_device(dci))
		goto err;

	drvdata->db_irq = platform_get_irq(pdev, 0);
	res = devm_request_irq(&pdev->dev, drvdata->db_irq,
			       highbank_l2_err_handler,
			       0, dev_name(&pdev->dev), dci);
	if (res < 0)
		goto err2;

	drvdata->sb_irq = platform_get_irq(pdev, 1);
	res = devm_request_irq(&pdev->dev, drvdata->sb_irq,
			       highbank_l2_err_handler,
			       0, dev_name(&pdev->dev), dci);
	if (res < 0)
		goto err2;

	devres_close_group(&pdev->dev, NULL);
	return 0;
err2:
	edac_device_del_device(&pdev->dev);
err:
	devres_release_group(&pdev->dev, NULL);
	edac_device_free_ctl_info(dci);
	return res;
}

static void highbank_l2_err_remove(struct platform_device *pdev)
{
	struct edac_device_ctl_info *dci = platform_get_drvdata(pdev);

	edac_device_del_device(&pdev->dev);
	edac_device_free_ctl_info(dci);
}

static struct platform_driver highbank_l2_edac_driver = {
	.probe = highbank_l2_err_probe,
	.remove_new = highbank_l2_err_remove,
	.driver = {
		.name = "hb_l2_edac",
		.of_match_table = hb_l2_err_of_match,
	},
};

module_platform_driver(highbank_l2_edac_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Calxeda, Inc.");
MODULE_DESCRIPTION("EDAC Driver for Calxeda Highbank L2 Cache");
