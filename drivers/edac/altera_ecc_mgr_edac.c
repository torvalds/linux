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

#include <linux/ctype.h>
#include <linux/edac.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/types.h>
#include <asm/cacheflush.h>
#include <asm/system.h>
#include "altera_edac.h"
#include "edac_core.h"
#include "edac_module.h"

static irqreturn_t altr_ecc_mgr_handler(int irq, void *dev_id)
{
	struct edac_device_ctl_info *dci = dev_id;
	struct altr_ecc_mgr_dev *drvdata = dci->pvt_info;
	const struct ecc_mgr_prv_data *priv = drvdata->data;

	if (irq == drvdata->sb_irq) {
		if (priv->ce_clear_mask)
			writel(priv->ce_clear_mask, drvdata->base);
		edac_device_handle_ce(dci, 0, 0, drvdata->edac_dev_name);
	}
	if (irq == drvdata->db_irq) {
		if (priv->ue_clear_mask)
			writel(priv->ue_clear_mask, drvdata->base);
		edac_device_handle_ue(dci, 0, 0, drvdata->edac_dev_name);
		panic("\nEDAC:ECC_MGR[Uncorrectable errors]\n");
	}

	return IRQ_HANDLED;
}

#ifdef CONFIG_EDAC_DEBUG
ssize_t altr_ecc_mgr_trig(struct edac_device_ctl_info *edac_dci,
			  const char *buffer, size_t count)
{
	u32 *ptemp, i, error_mask;
	int result = 0;
	unsigned long flags;
	struct altr_ecc_mgr_dev *drvdata = edac_dci->pvt_info;
	const struct ecc_mgr_prv_data *priv = drvdata->data;
	void *generic_ptr = edac_dci->dev;

	if (!priv->init_mem)
		return -ENOMEM;

	/* Note that generic_ptr is initialized to the device * but in
	 * some init_functions, this is overridden and returns data    */
	ptemp = priv->init_mem(priv->trig_alloc_sz, &generic_ptr);
	if (!ptemp) {
		dev_err(edac_dci->dev,
			"**EDAC Error Inject: Buffer Allocation error\n");
		return -ENOMEM;
	}

	if (count == 3)
		error_mask = priv->ue_set_mask;
	else
		error_mask = priv->ce_set_mask;

	dev_alert(edac_dci->dev, "%s: Trigger Error Mask (0x%X)\n",
			__func__, error_mask);

	local_irq_save(flags);
	/* write data out which should be corrupted. */
	for (i = 0; i < 16; i++) {
		/* Read data out so we're in the correct state */
		if (ptemp[i])
			result = -1;
		rmb();
		/* Toggle Error bit (it is latched), leave ECC enabled */
		writel(error_mask, drvdata->base);
		writel(priv->ecc_enable_mask, drvdata->base);
		ptemp[i] = i;
	}
	wmb();
	local_irq_restore(flags);

	if (result)
		dev_alert(edac_dci->dev, "%s: Mem Not Cleared (%d)\n",
				__func__, result);

	/* Read out written data. ECC error caused here */
	for (i = 0; i < 16; i++)
		if (ptemp[i] != i)
			result = -1;
	rmb();

	if (priv->free_mem)
		priv->free_mem(ptemp, priv->trig_alloc_sz, generic_ptr);

	if (result)
		dev_alert(edac_dci->dev, "%s: Trigger Match Error (%d)\n",
			__func__, result);

	return count;
}

static void altr_set_sysfs_attr(struct edac_device_ctl_info *edac_dci,
				const struct ecc_mgr_prv_data *priv)
{
	struct edac_dev_sysfs_attribute *ecc_attr = priv->eccmgr_sysfs_attr;
	if (ecc_attr)
		edac_dci->sysfs_attributes =  ecc_attr;
}
#else
static void altr_set_sysfs_attr(struct edac_device_ctl_info *edac_dci,
				const struct ecc_mgr_prv_data *priv)
{}
#endif	/* #ifdef CONFIG_EDAC_DEBUG */

static const struct of_device_id altr_ecc_mgr_of_match[] = {
#ifdef CONFIG_EDAC_ALTERA_L2_ECC
	{ .compatible = "altr,l2-edac", .data = (void *)&l2ecc_data },
#endif
#ifdef CONFIG_EDAC_ALTERA_OCRAM_ECC
	{ .compatible = "altr,ocram-edac", .data = (void *)&ocramecc_data },
#endif
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
	const struct ecc_mgr_prv_data *priv;
	struct resource *r;
	int res = 0;
	struct device_node *np = pdev->dev.of_node;
	char *ecc_name = (char *)np->name;
	static int dev_instance;

	dci = edac_device_alloc_ctl_info(sizeof(*drvdata), ecc_name,
			1, ecc_name, 1, 0, NULL, 0, dev_instance++);

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
	priv = drvdata->data = of_match_node(altr_ecc_mgr_of_match, np)->data;
	if (priv->setup) {
		res = priv->setup(pdev, drvdata->base);
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

	altr_set_sysfs_attr(dci, priv);

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
MODULE_DESCRIPTION("EDAC Driver for Altera SoC ECC Manager");
