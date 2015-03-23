/*
 * CoreNet Coherency Fabric error reporting
 *
 * Copyright 2014 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>

enum ccf_version {
	CCF1,
	CCF2,
};

struct ccf_info {
	enum ccf_version version;
	int err_reg_offs;
	bool has_brr;
};

static const struct ccf_info ccf1_info = {
	.version = CCF1,
	.err_reg_offs = 0xa00,
	.has_brr = false,
};

static const struct ccf_info ccf2_info = {
	.version = CCF2,
	.err_reg_offs = 0xe40,
	.has_brr = true,
};

/*
 * This register is present but not documented, with different values for
 * IP_ID, on other chips with fsl,corenet2-cf such as t4240 and b4860.
 */
#define CCF_BRR			0xbf8
#define CCF_BRR_IPID		0xffff0000
#define CCF_BRR_IPID_T1040	0x09310000

static const struct of_device_id ccf_matches[] = {
	{
		.compatible = "fsl,corenet1-cf",
		.data = &ccf1_info,
	},
	{
		.compatible = "fsl,corenet2-cf",
		.data = &ccf2_info,
	},
	{}
};

struct ccf_err_regs {
	u32 errdet;		/* 0x00 Error Detect Register */
	/* 0x04 Error Enable (ccf1)/Disable (ccf2) Register */
	u32 errdis;
	/* 0x08 Error Interrupt Enable Register (ccf2 only) */
	u32 errinten;
	u32 cecar;		/* 0x0c Error Capture Attribute Register */
	u32 cecaddrh;		/* 0x10 Error Capture Address High */
	u32 cecaddrl;		/* 0x14 Error Capture Address Low */
	u32 cecar2;		/* 0x18 Error Capture Attribute Register 2 */
};

/* LAE/CV also valid for errdis and errinten */
#define ERRDET_LAE		(1 << 0)  /* Local Access Error */
#define ERRDET_CV		(1 << 1)  /* Coherency Violation */
#define ERRDET_UTID		(1 << 2)  /* Unavailable Target ID (t1040) */
#define ERRDET_MCST		(1 << 3)  /* Multicast Stash (t1040) */
#define ERRDET_CTYPE_SHIFT	26	  /* Capture Type (ccf2 only) */
#define ERRDET_CTYPE_MASK	(0x1f << ERRDET_CTYPE_SHIFT)
#define ERRDET_CAP		(1 << 31) /* Capture Valid (ccf2 only) */

#define CECAR_VAL		(1 << 0)  /* Valid (ccf1 only) */
#define CECAR_UVT		(1 << 15) /* Unavailable target ID (ccf1) */
#define CECAR_SRCID_SHIFT_CCF1	24
#define CECAR_SRCID_MASK_CCF1	(0xff << CECAR_SRCID_SHIFT_CCF1)
#define CECAR_SRCID_SHIFT_CCF2	18
#define CECAR_SRCID_MASK_CCF2	(0xff << CECAR_SRCID_SHIFT_CCF2)

#define CECADDRH_ADDRH		0xff

struct ccf_private {
	const struct ccf_info *info;
	struct device *dev;
	void __iomem *regs;
	struct ccf_err_regs __iomem *err_regs;
	bool t1040;
};

static irqreturn_t ccf_irq(int irq, void *dev_id)
{
	struct ccf_private *ccf = dev_id;
	static DEFINE_RATELIMIT_STATE(ratelimit, DEFAULT_RATELIMIT_INTERVAL,
				      DEFAULT_RATELIMIT_BURST);
	u32 errdet, cecar, cecar2;
	u64 addr;
	u32 src_id;
	bool uvt = false;
	bool cap_valid = false;

	errdet = ioread32be(&ccf->err_regs->errdet);
	cecar = ioread32be(&ccf->err_regs->cecar);
	cecar2 = ioread32be(&ccf->err_regs->cecar2);
	addr = ioread32be(&ccf->err_regs->cecaddrl);
	addr |= ((u64)(ioread32be(&ccf->err_regs->cecaddrh) &
		       CECADDRH_ADDRH)) << 32;

	if (!__ratelimit(&ratelimit))
		goto out;

	switch (ccf->info->version) {
	case CCF1:
		if (cecar & CECAR_VAL) {
			if (cecar & CECAR_UVT)
				uvt = true;

			src_id = (cecar & CECAR_SRCID_MASK_CCF1) >>
				 CECAR_SRCID_SHIFT_CCF1;
			cap_valid = true;
		}

		break;
	case CCF2:
		if (errdet & ERRDET_CAP) {
			src_id = (cecar & CECAR_SRCID_MASK_CCF2) >>
				 CECAR_SRCID_SHIFT_CCF2;
			cap_valid = true;
		}

		break;
	}

	dev_crit(ccf->dev, "errdet 0x%08x cecar 0x%08x cecar2 0x%08x\n",
		 errdet, cecar, cecar2);

	if (errdet & ERRDET_LAE) {
		if (uvt)
			dev_crit(ccf->dev, "LAW Unavailable Target ID\n");
		else
			dev_crit(ccf->dev, "Local Access Window Error\n");
	}

	if (errdet & ERRDET_CV)
		dev_crit(ccf->dev, "Coherency Violation\n");

	if (errdet & ERRDET_UTID)
		dev_crit(ccf->dev, "Unavailable Target ID\n");

	if (errdet & ERRDET_MCST)
		dev_crit(ccf->dev, "Multicast Stash\n");

	if (cap_valid) {
		dev_crit(ccf->dev, "address 0x%09llx, src id 0x%x\n",
			 addr, src_id);
	}

out:
	iowrite32be(errdet, &ccf->err_regs->errdet);
	return errdet ? IRQ_HANDLED : IRQ_NONE;
}

static int ccf_probe(struct platform_device *pdev)
{
	struct ccf_private *ccf;
	struct resource *r;
	const struct of_device_id *match;
	u32 errinten;
	int ret, irq;

	match = of_match_device(ccf_matches, &pdev->dev);
	if (WARN_ON(!match))
		return -ENODEV;

	ccf = devm_kzalloc(&pdev->dev, sizeof(*ccf), GFP_KERNEL);
	if (!ccf)
		return -ENOMEM;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(&pdev->dev, "%s: no mem resource\n", __func__);
		return -ENXIO;
	}

	ccf->regs = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(ccf->regs)) {
		dev_err(&pdev->dev, "%s: can't map mem resource\n", __func__);
		return PTR_ERR(ccf->regs);
	}

	ccf->dev = &pdev->dev;
	ccf->info = match->data;
	ccf->err_regs = ccf->regs + ccf->info->err_reg_offs;

	if (ccf->info->has_brr) {
		u32 brr = ioread32be(ccf->regs + CCF_BRR);

		if ((brr & CCF_BRR_IPID) == CCF_BRR_IPID_T1040)
			ccf->t1040 = true;
	}

	dev_set_drvdata(&pdev->dev, ccf);

	irq = platform_get_irq(pdev, 0);
	if (!irq) {
		dev_err(&pdev->dev, "%s: no irq\n", __func__);
		return -ENXIO;
	}

	ret = devm_request_irq(&pdev->dev, irq, ccf_irq, 0, pdev->name, ccf);
	if (ret) {
		dev_err(&pdev->dev, "%s: can't request irq\n", __func__);
		return ret;
	}

	errinten = ERRDET_LAE | ERRDET_CV;
	if (ccf->t1040)
		errinten |= ERRDET_UTID | ERRDET_MCST;

	switch (ccf->info->version) {
	case CCF1:
		/* On CCF1 this register enables rather than disables. */
		iowrite32be(errinten, &ccf->err_regs->errdis);
		break;

	case CCF2:
		iowrite32be(0, &ccf->err_regs->errdis);
		iowrite32be(errinten, &ccf->err_regs->errinten);
		break;
	}

	return 0;
}

static int ccf_remove(struct platform_device *pdev)
{
	struct ccf_private *ccf = dev_get_drvdata(&pdev->dev);

	switch (ccf->info->version) {
	case CCF1:
		iowrite32be(0, &ccf->err_regs->errdis);
		break;

	case CCF2:
		/*
		 * We clear errdis on ccf1 because that's the only way to
		 * disable interrupts, but on ccf2 there's no need to disable
		 * detection.
		 */
		iowrite32be(0, &ccf->err_regs->errinten);
		break;
	}

	return 0;
}

static struct platform_driver ccf_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = ccf_matches,
	},
	.probe = ccf_probe,
	.remove = ccf_remove,
};

module_platform_driver(ccf_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Freescale Semiconductor");
MODULE_DESCRIPTION("Freescale CoreNet Coherency Fabric error reporting");
