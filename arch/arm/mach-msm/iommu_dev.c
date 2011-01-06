/* Copyright (c) 2010, Code Aurora Forum. All rights reserved.
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

#include <mach/iommu_hw-8xxx.h>
#include <mach/iommu.h>

struct iommu_ctx_iter_data {
	/* input */
	const char *name;

	/* output */
	struct device *dev;
};

static struct platform_device *msm_iommu_root_dev;

static int each_iommu_ctx(struct device *dev, void *data)
{
	struct iommu_ctx_iter_data *res = data;
	struct msm_iommu_ctx_dev *c = dev->platform_data;

	if (!res || !c || !c->name || !res->name)
		return -EINVAL;

	if (!strcmp(res->name, c->name)) {
		res->dev = dev;
		return 1;
	}
	return 0;
}

static int each_iommu(struct device *dev, void *data)
{
	return device_for_each_child(dev, data, each_iommu_ctx);
}

struct device *msm_iommu_get_ctx(const char *ctx_name)
{
	struct iommu_ctx_iter_data r;
	int found;

	if (!msm_iommu_root_dev) {
		pr_err("No root IOMMU device.\n");
		goto fail;
	}

	r.name = ctx_name;
	found = device_for_each_child(&msm_iommu_root_dev->dev, &r, each_iommu);

	if (!found) {
		pr_err("Could not find context <%s>\n", ctx_name);
		goto fail;
	}

	return r.dev;
fail:
	return NULL;
}
EXPORT_SYMBOL(msm_iommu_get_ctx);

static void msm_iommu_reset(void __iomem *base)
{
	int ctx, ncb;

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
	ncb = GET_NCB(base)+1;

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
	struct resource *r, *r2;
	struct clk *iommu_clk;
	struct msm_iommu_drvdata *drvdata;
	struct msm_iommu_dev *iommu_dev = pdev->dev.platform_data;
	void __iomem *regs_base;
	resource_size_t	len;
	int ret = 0, ncb, nm2v, irq;

	if (pdev->id != -1) {
		drvdata = kzalloc(sizeof(*drvdata), GFP_KERNEL);

		if (!drvdata) {
			ret = -ENOMEM;
			goto fail;
		}

		if (!iommu_dev) {
			ret = -ENODEV;
			goto fail;
		}

		if (iommu_dev->clk_rate != 0) {
			iommu_clk = clk_get(&pdev->dev, "iommu_clk");

			if (IS_ERR(iommu_clk)) {
				ret = -ENODEV;
				goto fail;
			}

			if (iommu_dev->clk_rate > 0) {
				ret = clk_set_rate(iommu_clk,
							iommu_dev->clk_rate);
				if (ret) {
					clk_put(iommu_clk);
					goto fail;
				}
			}

			ret = clk_enable(iommu_clk);
			if (ret) {
				clk_put(iommu_clk);
				goto fail;
			}
			clk_put(iommu_clk);
		}

		r = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						 "physbase");
		if (!r) {
			ret = -ENODEV;
			goto fail;
		}

		len = r->end - r->start + 1;

		r2 = request_mem_region(r->start, len, r->name);
		if (!r2) {
			pr_err("Could not request memory region: "
			"start=%p, len=%d\n", (void *) r->start, len);
			ret = -EBUSY;
			goto fail;
		}

		regs_base = ioremap(r2->start, len);

		if (!regs_base) {
			pr_err("Could not ioremap: start=%p, len=%d\n",
				 (void *) r2->start, len);
			ret = -EBUSY;
			goto fail_mem;
		}

		irq = platform_get_irq_byname(pdev, "secure_irq");
		if (irq < 0) {
			ret = -ENODEV;
			goto fail_io;
		}

		mb();

		if (GET_IDR(regs_base) == 0) {
			pr_err("Invalid IDR value detected\n");
			ret = -ENODEV;
			goto fail_io;
		}

		ret = request_irq(irq, msm_iommu_fault_handler, 0,
				"msm_iommu_secure_irpt_handler", drvdata);
		if (ret) {
			pr_err("Request IRQ %d failed with ret=%d\n", irq, ret);
			goto fail_io;
		}

		msm_iommu_reset(regs_base);
		drvdata->base = regs_base;
		drvdata->irq = irq;

		nm2v = GET_NM2VCBMT((unsigned long) regs_base);
		ncb = GET_NCB((unsigned long) regs_base);

		pr_info("device %s mapped at %p, irq %d with %d ctx banks\n",
			iommu_dev->name, regs_base, irq, ncb+1);

		platform_set_drvdata(pdev, drvdata);
	} else
		msm_iommu_root_dev = pdev;

	return 0;

fail_io:
	iounmap(regs_base);
fail_mem:
	release_mem_region(r->start, len);
fail:
	kfree(drvdata);
	return ret;
}

static int msm_iommu_remove(struct platform_device *pdev)
{
	struct msm_iommu_drvdata *drv = NULL;

	drv = platform_get_drvdata(pdev);
	if (drv) {
		memset(drv, 0, sizeof(struct msm_iommu_drvdata));
		kfree(drv);
		platform_set_drvdata(pdev, NULL);
	}
	return 0;
}

static int msm_iommu_ctx_probe(struct platform_device *pdev)
{
	struct msm_iommu_ctx_dev *c = pdev->dev.platform_data;
	struct msm_iommu_drvdata *drvdata;
	struct msm_iommu_ctx_drvdata *ctx_drvdata = NULL;
	int i, ret = 0;
	if (!c || !pdev->dev.parent) {
		ret = -EINVAL;
		goto fail;
	}

	drvdata = dev_get_drvdata(pdev->dev.parent);

	if (!drvdata) {
		ret = -ENODEV;
		goto fail;
	}

	ctx_drvdata = kzalloc(sizeof(*ctx_drvdata), GFP_KERNEL);
	if (!ctx_drvdata) {
		ret = -ENOMEM;
		goto fail;
	}
	ctx_drvdata->num = c->num;
	ctx_drvdata->pdev = pdev;

	INIT_LIST_HEAD(&ctx_drvdata->attached_elm);
	platform_set_drvdata(pdev, ctx_drvdata);

	/* Program the M2V tables for this context */
	for (i = 0; i < MAX_NUM_MIDS; i++) {
		int mid = c->mids[i];
		if (mid == -1)
			break;

		SET_M2VCBR_N(drvdata->base, mid, 0);
		SET_CBACR_N(drvdata->base, c->num, 0);

		/* Set VMID = MID */
		SET_VMID(drvdata->base, mid, mid);

		/* Set the context number for that MID to this context */
		SET_CBNDX(drvdata->base, mid, c->num);

		/* Set MID associated with this context bank */
		SET_CBVMID(drvdata->base, c->num, mid);

		/* Set security bit override to be Non-secure */
		SET_NSCFG(drvdata->base, mid, 3);
	}

	pr_info("context device %s with bank index %d\n", c->name, c->num);

	return 0;
fail:
	kfree(ctx_drvdata);
	return ret;
}

static int msm_iommu_ctx_remove(struct platform_device *pdev)
{
	struct msm_iommu_ctx_drvdata *drv = NULL;
	drv = platform_get_drvdata(pdev);
	if (drv) {
		memset(drv, 0, sizeof(struct msm_iommu_ctx_drvdata));
		kfree(drv);
		platform_set_drvdata(pdev, NULL);
	}
	return 0;
}

static struct platform_driver msm_iommu_driver = {
	.driver = {
		.name	= "msm_iommu",
	},
	.probe		= msm_iommu_probe,
	.remove		= msm_iommu_remove,
};

static struct platform_driver msm_iommu_ctx_driver = {
	.driver = {
		.name	= "msm_iommu_ctx",
	},
	.probe		= msm_iommu_ctx_probe,
	.remove		= msm_iommu_ctx_remove,
};

static int __init msm_iommu_driver_init(void)
{
	int ret;
	ret = platform_driver_register(&msm_iommu_driver);
	if (ret != 0) {
		pr_err("Failed to register IOMMU driver\n");
		goto error;
	}

	ret = platform_driver_register(&msm_iommu_ctx_driver);
	if (ret != 0) {
		pr_err("Failed to register IOMMU context driver\n");
		goto error;
	}

error:
	return ret;
}

static void __exit msm_iommu_driver_exit(void)
{
	platform_driver_unregister(&msm_iommu_ctx_driver);
	platform_driver_unregister(&msm_iommu_driver);
}

subsys_initcall(msm_iommu_driver_init);
module_exit(msm_iommu_driver_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Stepan Moskovchenko <stepanm@codeaurora.org>");
