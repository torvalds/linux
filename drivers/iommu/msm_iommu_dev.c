/* Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/iommu.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/slab.h>

#include "msm_iommu_hw-8xxx.h"
#include "msm_iommu.h"

static void msm_iommu_reset(void __iomem *base, int ncb)
{
	int ctx;

	SET_RPUE(base, 0);
	SET_RPUEIE(base, 0);
	SET_ESRRESTORE(base, 0);
	SET_TBE(base, 0);
	SET_CR(base, 0);
	SET_SPDMBE(base, 0);
	SET_TESTBUSCR(base, 0);
	SET_TLBRSW(base, 0);
	SET_GLOBAL_TLBIALL(base, 0);
	SET_RPU_ACR(base, 0);
	SET_TLBLKCRWE(base, 1);

	for (ctx = 0; ctx < ncb; ctx++) {
		SET_BPRCOSH(base, ctx, 0);
		SET_BPRCISH(base, ctx, 0);
		SET_BPRCNSH(base, ctx, 0);
		SET_BPSHCFG(base, ctx, 0);
		SET_BPMTCFG(base, ctx, 0);
		SET_ACTLR(base, ctx, 0);
		SET_SCTLR(base, ctx, 0);
		SET_FSRRESTORE(base, ctx, 0);
		SET_TTBR0(base, ctx, 0);
		SET_TTBR1(base, ctx, 0);
		SET_TTBCR(base, ctx, 0);
		SET_BFBCR(base, ctx, 0);
		SET_PAR(base, ctx, 0);
		SET_FAR(base, ctx, 0);
		SET_CTX_TLBIALL(base, ctx, 0);
		SET_TLBFLPTER(base, ctx, 0);
		SET_TLBSLPTER(base, ctx, 0);
		SET_TLBLKCR(base, ctx, 0);
		SET_PRRR(base, ctx, 0);
		SET_NMRR(base, ctx, 0);
		SET_CONTEXTIDR(base, ctx, 0);
	}
}

static int msm_iommu_probe(struct platform_device *pdev)
{
	struct resource *r;
	struct msm_iommu_dev *iommu;
	int ret, par, val;

	iommu = devm_kzalloc(&pdev->dev, sizeof(*iommu), GFP_KERNEL);
	if (!iommu)
		return -ENODEV;

	iommu->dev = &pdev->dev;
	INIT_LIST_HEAD(&iommu->ctx_list);

	iommu->pclk = devm_clk_get(iommu->dev, "smmu_pclk");
	if (IS_ERR(iommu->pclk)) {
		dev_err(iommu->dev, "could not get smmu_pclk\n");
		return PTR_ERR(iommu->pclk);
	}

	ret = clk_prepare(iommu->pclk);
	if (ret) {
		dev_err(iommu->dev, "could not prepare smmu_pclk\n");
		return ret;
	}

	iommu->clk = devm_clk_get(iommu->dev, "iommu_clk");
	if (IS_ERR(iommu->clk)) {
		dev_err(iommu->dev, "could not get iommu_clk\n");
		clk_unprepare(iommu->pclk);
		return PTR_ERR(iommu->clk);
	}

	ret = clk_prepare(iommu->clk);
	if (ret) {
		dev_err(iommu->dev, "could not prepare iommu_clk\n");
		clk_unprepare(iommu->pclk);
		return ret;
	}

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	iommu->base = devm_ioremap_resource(iommu->dev, r);
	if (IS_ERR(iommu->base)) {
		dev_err(iommu->dev, "could not get iommu base\n");
		ret = PTR_ERR(iommu->base);
		goto fail;
	}

	iommu->irq = platform_get_irq(pdev, 0);
	if (iommu->irq < 0) {
		dev_err(iommu->dev, "could not get iommu irq\n");
		ret = -ENODEV;
		goto fail;
	}

	ret = of_property_read_u32(iommu->dev->of_node, "ncb", &val);
	if (ret) {
		dev_err(iommu->dev, "could not get ncb\n");
		goto fail;
	}
	iommu->ncb = val;

	msm_iommu_reset(iommu->base, iommu->ncb);
	SET_M(iommu->base, 0, 1);
	SET_PAR(iommu->base, 0, 0);
	SET_V2PCFG(iommu->base, 0, 1);
	SET_V2PPR(iommu->base, 0, 0);
	par = GET_PAR(iommu->base, 0);
	SET_V2PCFG(iommu->base, 0, 0);
	SET_M(iommu->base, 0, 0);

	if (!par) {
		pr_err("Invalid PAR value detected\n");
		ret = -ENODEV;
		goto fail;
	}

	ret = devm_request_threaded_irq(iommu->dev, iommu->irq, NULL,
					msm_iommu_fault_handler,
					IRQF_ONESHOT | IRQF_SHARED,
					"msm_iommu_secure_irpt_handler",
					iommu);
	if (ret) {
		pr_err("Request IRQ %d failed with ret=%d\n", iommu->irq, ret);
		goto fail;
	}

	list_add(&iommu->dev_node, &qcom_iommu_devices);

	pr_info("device mapped at %p, irq %d with %d ctx banks\n",
		iommu->base, iommu->irq, iommu->ncb);
fail:
	clk_unprepare(iommu->clk);
	clk_unprepare(iommu->pclk);
	return ret;
}

static const struct of_device_id msm_iommu_dt_match[] = {
	{ .compatible = "qcom,apq8064-iommu" },
	{}
};

static int msm_iommu_remove(struct platform_device *pdev)
{
	struct msm_iommu_dev *iommu = platform_get_drvdata(pdev);

	clk_unprepare(iommu->clk);
	clk_unprepare(iommu->pclk);
	return 0;
}

static struct platform_driver msm_iommu_driver = {
	.driver = {
		.name	= "msm_iommu",
		.of_match_table = msm_iommu_dt_match,
	},
	.probe		= msm_iommu_probe,
	.remove		= msm_iommu_remove,
};

static struct platform_driver * const drivers[] = {
	&msm_iommu_driver,
	&msm_iommu_ctx_driver,
};

static int __init msm_iommu_driver_init(void)
{
	return platform_register_drivers(drivers, ARRAY_SIZE(drivers));
}

static void __exit msm_iommu_driver_exit(void)
{
	platform_unregister_drivers(drivers, ARRAY_SIZE(drivers));
}

subsys_initcall(msm_iommu_driver_init);
module_exit(msm_iommu_driver_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Stepan Moskovchenko <stepanm@codeaurora.org>");
