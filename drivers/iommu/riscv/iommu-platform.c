// SPDX-License-Identifier: GPL-2.0-only
/*
 * RISC-V IOMMU as a platform device
 *
 * Copyright © 2023 FORTH-ICS/CARV
 * Copyright © 2023-2024 Rivos Inc.
 *
 * Authors
 *	Nick Kossifidis <mick@ics.forth.gr>
 *	Tomasz Jeznach <tjeznach@rivosinc.com>
 */

#include <linux/kernel.h>
#include <linux/msi.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include "iommu-bits.h"
#include "iommu.h"

static void riscv_iommu_write_msi_msg(struct msi_desc *desc, struct msi_msg *msg)
{
	struct device *dev = msi_desc_to_dev(desc);
	struct riscv_iommu_device *iommu = dev_get_drvdata(dev);
	u16 idx = desc->msi_index;
	u64 addr;

	addr = ((u64)msg->address_hi << 32) | msg->address_lo;

	if (addr != (addr & RISCV_IOMMU_MSI_CFG_TBL_ADDR)) {
		dev_err_once(dev,
			     "uh oh, the IOMMU can't send MSIs to 0x%llx, sending to 0x%llx instead\n",
			     addr, addr & RISCV_IOMMU_MSI_CFG_TBL_ADDR);
	}

	addr &= RISCV_IOMMU_MSI_CFG_TBL_ADDR;

	riscv_iommu_writeq(iommu, RISCV_IOMMU_REG_MSI_CFG_TBL_ADDR(idx), addr);
	riscv_iommu_writel(iommu, RISCV_IOMMU_REG_MSI_CFG_TBL_DATA(idx), msg->data);
	riscv_iommu_writel(iommu, RISCV_IOMMU_REG_MSI_CFG_TBL_CTRL(idx), 0);
}

static int riscv_iommu_platform_probe(struct platform_device *pdev)
{
	enum riscv_iommu_igs_settings igs;
	struct device *dev = &pdev->dev;
	struct riscv_iommu_device *iommu = NULL;
	struct resource *res = NULL;
	int vec, ret;

	iommu = devm_kzalloc(dev, sizeof(*iommu), GFP_KERNEL);
	if (!iommu)
		return -ENOMEM;

	iommu->dev = dev;
	iommu->reg = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(iommu->reg))
		return dev_err_probe(dev, PTR_ERR(iommu->reg),
				     "could not map register region\n");

	dev_set_drvdata(dev, iommu);

	/* Check device reported capabilities / features. */
	iommu->caps = riscv_iommu_readq(iommu, RISCV_IOMMU_REG_CAPABILITIES);
	iommu->fctl = riscv_iommu_readl(iommu, RISCV_IOMMU_REG_FCTL);

	iommu->irqs_count = platform_irq_count(pdev);
	if (iommu->irqs_count <= 0)
		return dev_err_probe(dev, -ENODEV,
				     "no IRQ resources provided\n");
	if (iommu->irqs_count > RISCV_IOMMU_INTR_COUNT)
		iommu->irqs_count = RISCV_IOMMU_INTR_COUNT;

	igs = FIELD_GET(RISCV_IOMMU_CAPABILITIES_IGS, iommu->caps);
	switch (igs) {
	case RISCV_IOMMU_CAPABILITIES_IGS_BOTH:
	case RISCV_IOMMU_CAPABILITIES_IGS_MSI:
		if (is_of_node(dev->fwnode))
			of_msi_configure(dev, to_of_node(dev->fwnode));

		if (!dev_get_msi_domain(dev)) {
			dev_warn(dev, "failed to find an MSI domain\n");
			goto msi_fail;
		}

		ret = platform_device_msi_init_and_alloc_irqs(dev, iommu->irqs_count,
							      riscv_iommu_write_msi_msg);
		if (ret) {
			dev_warn(dev, "failed to allocate MSIs\n");
			goto msi_fail;
		}

		for (vec = 0; vec < iommu->irqs_count; vec++)
			iommu->irqs[vec] = msi_get_virq(dev, vec);

		/* Enable message-signaled interrupts, fctl.WSI */
		if (iommu->fctl & RISCV_IOMMU_FCTL_WSI) {
			iommu->fctl ^= RISCV_IOMMU_FCTL_WSI;
			riscv_iommu_writel(iommu, RISCV_IOMMU_REG_FCTL, iommu->fctl);
		}

		dev_info(dev, "using MSIs\n");
		break;

msi_fail:
		if (igs != RISCV_IOMMU_CAPABILITIES_IGS_BOTH) {
			return dev_err_probe(dev, -ENODEV,
					     "unable to use wire-signaled interrupts\n");
		}

		fallthrough;

	case RISCV_IOMMU_CAPABILITIES_IGS_WSI:
		for (vec = 0; vec < iommu->irqs_count; vec++)
			iommu->irqs[vec] = platform_get_irq(pdev, vec);

		/* Enable wire-signaled interrupts, fctl.WSI */
		if (!(iommu->fctl & RISCV_IOMMU_FCTL_WSI)) {
			iommu->fctl |= RISCV_IOMMU_FCTL_WSI;
			riscv_iommu_writel(iommu, RISCV_IOMMU_REG_FCTL, iommu->fctl);
		}
		dev_info(dev, "using wire-signaled interrupts\n");
		break;
	default:
		return dev_err_probe(dev, -ENODEV, "invalid IGS\n");
	}

	return riscv_iommu_init(iommu);
};

static void riscv_iommu_platform_remove(struct platform_device *pdev)
{
	struct riscv_iommu_device *iommu = dev_get_drvdata(&pdev->dev);
	bool msi = !(iommu->fctl & RISCV_IOMMU_FCTL_WSI);

	riscv_iommu_remove(iommu);

	if (msi)
		platform_device_msi_free_irqs_all(&pdev->dev);
};

static void riscv_iommu_platform_shutdown(struct platform_device *pdev)
{
	riscv_iommu_disable(dev_get_drvdata(&pdev->dev));
};

static const struct of_device_id riscv_iommu_of_match[] = {
	{.compatible = "riscv,iommu",},
	{},
};

static struct platform_driver riscv_iommu_platform_driver = {
	.probe = riscv_iommu_platform_probe,
	.remove = riscv_iommu_platform_remove,
	.shutdown = riscv_iommu_platform_shutdown,
	.driver = {
		.name = "riscv,iommu",
		.of_match_table = riscv_iommu_of_match,
		.suppress_bind_attrs = true,
	},
};

builtin_platform_driver(riscv_iommu_platform_driver);
