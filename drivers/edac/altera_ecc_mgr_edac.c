/*
 *  Copyright (C) 2013 Altera Corporation
 * Copyright 2011-2012 Calxeda, Inc.
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
 * Adapted from the highbank_l2_edac driver
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <asm/cacheflush.h>
#include <asm/system.h>

#include "edac_core.h"
#include "edac_module.h"

#include <linux/ctype.h>
#include <linux/edac.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/types.h>

/* MPU L2 Register Defines */
#define ALTR_MPUL2_CONTROL_OFFSET	0x100
#define ALTR_MPUL2_CTL_CACHE_EN_MASK	0x00000001

/* L2 ECC Management Group Defines */
#define ALTR_MAN_GRP_L2_ECC_OFFSET	0x00
#define ALTR_L2_ECC_EN_MASK		0x00000001
#define ALTR_L2_ECC_INJS_MASK		0x00000002
#define ALTR_L2_ECC_INJD_MASK		0x00000004

struct ecc_mgr_of_data {
	int (*setup)(struct platform_device *pdev, void __iomem *base);
};

struct altr_ecc_mgr_dev {
	void __iomem *base;
	int sb_irq;
	int db_irq;
	const struct ecc_mgr_of_data *data;
	char *edac_dev_name;
};

static irqreturn_t altr_ecc_mgr_handler(int irq, void *dev_id)
{
	struct edac_device_ctl_info *dci = dev_id;
	struct altr_ecc_mgr_dev *drvdata = dci->pvt_info;

	if (irq == drvdata->sb_irq)
		edac_device_handle_ce(dci, 0, 0, drvdata->edac_dev_name);
	if (irq == drvdata->db_irq) {
		edac_device_handle_ue(dci, 0, 0, drvdata->edac_dev_name);
		panic("\nEDAC:ECC_MGR[Uncorrectable errors]\n");
	}

	return IRQ_HANDLED;
}

#ifdef CONFIG_EDAC_DEBUG
static ssize_t altr_l2_ecc_trig(struct edac_device_ctl_info *edac_dci,
				const char *buffer, size_t count)
{
	u32 *ptemp, i, error_mask;
	int result = 0;
	unsigned long flags;
	struct altr_ecc_mgr_dev *drvdata = edac_dci->pvt_info;

	ptemp = kmalloc(5000, GFP_KERNEL);
	if (!ptemp) {
		dev_err(edac_dci->dev,
			"**EDAC L2 Inject: Buffer Allocation error\n");
		return -ENOMEM;
	}

	memset(ptemp, 0, 5000);
	wmb();
	flush_cache_all();

	if (count == 3)
		error_mask = ALTR_L2_ECC_EN_MASK | ALTR_L2_ECC_INJD_MASK;
	else
		error_mask = ALTR_L2_ECC_EN_MASK | ALTR_L2_ECC_INJS_MASK;

	dev_alert(edac_dci->dev, "%s: Trigger Error Mask (0x%X)\n",
			__func__, error_mask);

	local_irq_save(flags);
	/* write data out which should be corrupted. */
	for (i = 0; i < 16; i++) {
		/* Read data out so we're in the correct state */
		if (ptemp[i])
			result = -1;
		rmb();
		/* Toggle Error bit (it is latched) */
		writel(error_mask, drvdata->base);
		writel(ALTR_L2_ECC_EN_MASK, drvdata->base);
		ptemp[i] = i;
	}
	wmb();
	local_irq_restore(flags);

	if (result)
		dev_alert(edac_dci->dev, "%s: Mem Not Cleared (%d)\n",
				__func__, result);

	result = 0;
	/* Read out written data. ECC error caused here */
	for (i = 0; i < 16; i++)
		if (ptemp[i] != i)
			result = -1;
	rmb();

	kfree(ptemp);

	if (result)
		dev_alert(edac_dci->dev, "%s: Trigger Match Error (%d)\n",
			__func__, result);

	return count;
}

static struct edac_dev_sysfs_attribute altr_l2_sysfs_attributes[] = {
	{
		.attr = { .name = "altr_l2_trigger",
			  .mode = (S_IRUGO | S_IWUSR) },
		.show = NULL,
		.store = altr_l2_ecc_trig
	},
	{
		.attr = {.name = NULL }
	}
};

static void altr_set_sysfs_attr(struct edac_device_ctl_info *edac_dci)
{
	edac_dci->sysfs_attributes =  altr_l2_sysfs_attributes;
}
#else
static void altr_set_sysfs_attr(struct edac_device_ctl_info *edac_dci)
{}
#endif

/*
 * altr_l2_dependencies()
 *	Test for L2 cache ECC dependencies upon entry because
 *	the preloader/UBoot should have initialized the L2
 *	memory and enabled the ECC.
 *	Can't turn on ECC here because accessing un-initialized
 *	memory will cause CE/UE errors possibly causing an ABORT.
 *	Bail if ECC is not on.
 *	Test For 1) L2 ECC is enabled and 2) L2 Cache is enabled.
 */
static int altr_l2_dependencies(struct platform_device *pdev,
				void __iomem *base)
{
	u32 control;
	struct regmap *l2_vbase;

	control = readl(base) & ALTR_L2_ECC_EN_MASK;
	if (!control) {
		dev_err(&pdev->dev, "L2: No ECC present, or ECC disabled\n");
		return -ENODEV;
	}

	l2_vbase = syscon_regmap_lookup_by_compatible("arm,pl310-cache");
	if (IS_ERR(l2_vbase)) {
		dev_err(&pdev->dev,
			"L2 ECC:regmap for arm,pl310-cache lookup failed.\n");
		return -ENODEV;
	}

	regmap_read(l2_vbase, ALTR_MPUL2_CONTROL_OFFSET, &control);
	if (!(control & ALTR_MPUL2_CTL_CACHE_EN_MASK)) {
		dev_err(&pdev->dev, "L2: Cache disabled\n");
		return -ENODEV;
	}

	return 0;
}

static const struct ecc_mgr_of_data l2ecc_data = {
	.setup = altr_l2_dependencies,
};

static const struct of_device_id altr_ecc_mgr_of_match[] = {
	{ .compatible = "altr,l2-edac", .data = (void *)&l2ecc_data },
	{},
};

/*
 * altr_ecc_mgr_probe()
 *	This is a generic EDAC device driver that will support
 *	various Altera memory devices such as the L2 cache ECC and
 *	OCRAM ECC as well as the memories for other peripherals.
 *	Module specific initialization is done by passing the
 *	function index in the device tree.
 */
static int altr_ecc_mgr_probe(struct platform_device *pdev)
{
	struct edac_device_ctl_info *dci;
	struct altr_ecc_mgr_dev *drvdata;
	struct resource *r;
	int res = 0;
	struct device_node *np = pdev->dev.of_node;
	char *ecc_name = (char *)np->name;

	dci = edac_device_alloc_ctl_info(sizeof(*drvdata), "ecc",
			1, ecc_name, 1, 0, NULL, 0, 0);

	if (!dci)
		return -ENOMEM;

	drvdata = dci->pvt_info;
	dci->dev = &pdev->dev;
	platform_set_drvdata(pdev, dci);
	drvdata->edac_dev_name = ecc_name;

	if (!devres_open_group(&pdev->dev, NULL, GFP_KERNEL))
		return -ENOMEM;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(&pdev->dev, "%s:Unable to get mem resource\n",
			ecc_name);
		res = -ENODEV;
		goto err;
	}

	if (!devm_request_mem_region(&pdev->dev, r->start, resource_size(r),
					dev_name(&pdev->dev))) {
		dev_err(&pdev->dev, "%s:Error requesting mem region\n",
			ecc_name);
		res = -EBUSY;
		goto err;
	}

	drvdata->base = devm_ioremap(&pdev->dev, r->start, resource_size(r));
	if (!drvdata->base) {
		dev_err(&pdev->dev, "%s:Unable to map regs\n", ecc_name);
		res = -ENOMEM;
		goto err;
	}

	/* Check specific dependencies for the module */
	drvdata->data = of_match_node(altr_ecc_mgr_of_match, np)->data;
	if (drvdata->data->setup) {
		res = drvdata->data->setup(pdev, drvdata->base);
		if (res < 0)
			goto err;
	}

	drvdata->sb_irq = platform_get_irq(pdev, 0);
	res = devm_request_irq(&pdev->dev, drvdata->sb_irq,
			       altr_ecc_mgr_handler,
			       0, dev_name(&pdev->dev), dci);
	if (res < 0)
		goto err;

	drvdata->db_irq = platform_get_irq(pdev, 1);
	res = devm_request_irq(&pdev->dev, drvdata->db_irq,
			       altr_ecc_mgr_handler,
			       0, dev_name(&pdev->dev), dci);
	if (res < 0)
		goto err;

	dci->mod_name = "ECC_MGR";
	dci->dev_name = drvdata->edac_dev_name;

	altr_set_sysfs_attr(dci);

	if (edac_device_add_device(dci))
		goto err;

	devres_close_group(&pdev->dev, NULL);

	return 0;
err:
	devres_release_group(&pdev->dev, NULL);
	edac_device_free_ctl_info(dci);

	return res;
}

static int altr_ecc_mgr_remove(struct platform_device *pdev)
{
	struct edac_device_ctl_info *dci = platform_get_drvdata(pdev);

	edac_device_del_device(&pdev->dev);
	edac_device_free_ctl_info(dci);

	return 0;
}

static struct platform_driver altr_ecc_mgr_driver = {
	.probe =  altr_ecc_mgr_probe,
	.remove = altr_ecc_mgr_remove,
	.driver = {
		.name = "altr_ecc_mgr",
		.of_match_table = of_match_ptr(altr_ecc_mgr_of_match),
	},
};

MODULE_DEVICE_TABLE(of, altr_ecc_mgr_of_match);

module_platform_driver(altr_ecc_mgr_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Altera Corporation");
MODULE_DESCRIPTION("EDAC Driver for Altera SoC L2 Cache");
