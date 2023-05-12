// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Linaro Ltd.
 *
 * This device driver implements MMIO TPM on SynQuacer Platform.
 */
#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/kernel.h>
#include "tpm.h"
#include "tpm_tis_core.h"

/*
 * irq > 0 means: use irq $irq;
 * irq = 0 means: autoprobe for an irq;
 * irq = -1 means: no irq support
 */
struct tpm_tis_synquacer_info {
	struct resource res;
	int irq;
};

struct tpm_tis_synquacer_phy {
	struct tpm_tis_data priv;
	void __iomem *iobase;
};

static inline struct tpm_tis_synquacer_phy *to_tpm_tis_tcg_phy(struct tpm_tis_data *data)
{
	return container_of(data, struct tpm_tis_synquacer_phy, priv);
}

static int tpm_tis_synquacer_read_bytes(struct tpm_tis_data *data, u32 addr,
					u16 len, u8 *result,
					enum tpm_tis_io_mode io_mode)
{
	struct tpm_tis_synquacer_phy *phy = to_tpm_tis_tcg_phy(data);
	switch (io_mode) {
	case TPM_TIS_PHYS_8:
		while (len--)
			*result++ = ioread8(phy->iobase + addr);
		break;
	case TPM_TIS_PHYS_16:
		result[1] = ioread8(phy->iobase + addr + 1);
		result[0] = ioread8(phy->iobase + addr);
		break;
	case TPM_TIS_PHYS_32:
		result[3] = ioread8(phy->iobase + addr + 3);
		result[2] = ioread8(phy->iobase + addr + 2);
		result[1] = ioread8(phy->iobase + addr + 1);
		result[0] = ioread8(phy->iobase + addr);
		break;
	}

	return 0;
}

static int tpm_tis_synquacer_write_bytes(struct tpm_tis_data *data, u32 addr,
					 u16 len, const u8 *value,
					 enum tpm_tis_io_mode io_mode)
{
	struct tpm_tis_synquacer_phy *phy = to_tpm_tis_tcg_phy(data);
	switch (io_mode) {
	case TPM_TIS_PHYS_8:
		while (len--)
			iowrite8(*value++, phy->iobase + addr);
		break;
	case TPM_TIS_PHYS_16:
		return -EINVAL;
	case TPM_TIS_PHYS_32:
		/*
		 * Due to the limitation of SPI controller on SynQuacer,
		 * 16/32 bits access must be done in byte-wise and descending order.
		 */
		iowrite8(value[3], phy->iobase + addr + 3);
		iowrite8(value[2], phy->iobase + addr + 2);
		iowrite8(value[1], phy->iobase + addr + 1);
		iowrite8(value[0], phy->iobase + addr);
		break;
	}

	return 0;
}

static const struct tpm_tis_phy_ops tpm_tcg_bw = {
	.read_bytes	= tpm_tis_synquacer_read_bytes,
	.write_bytes	= tpm_tis_synquacer_write_bytes,
};

static int tpm_tis_synquacer_init(struct device *dev,
				  struct tpm_tis_synquacer_info *tpm_info)
{
	struct tpm_tis_synquacer_phy *phy;

	phy = devm_kzalloc(dev, sizeof(struct tpm_tis_synquacer_phy), GFP_KERNEL);
	if (phy == NULL)
		return -ENOMEM;

	phy->iobase = devm_ioremap_resource(dev, &tpm_info->res);
	if (IS_ERR(phy->iobase))
		return PTR_ERR(phy->iobase);

	return tpm_tis_core_init(dev, &phy->priv, tpm_info->irq, &tpm_tcg_bw,
				 ACPI_HANDLE(dev));
}

static SIMPLE_DEV_PM_OPS(tpm_tis_synquacer_pm, tpm_pm_suspend, tpm_tis_resume);

static int tpm_tis_synquacer_probe(struct platform_device *pdev)
{
	struct tpm_tis_synquacer_info tpm_info = {};
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "no memory resource defined\n");
		return -ENODEV;
	}
	tpm_info.res = *res;

	tpm_info.irq = -1;

	return tpm_tis_synquacer_init(&pdev->dev, &tpm_info);
}

static void tpm_tis_synquacer_remove(struct platform_device *pdev)
{
	struct tpm_chip *chip = dev_get_drvdata(&pdev->dev);

	tpm_chip_unregister(chip);
	tpm_tis_remove(chip);
}

#ifdef CONFIG_OF
static const struct of_device_id tis_synquacer_of_platform_match[] = {
	{.compatible = "socionext,synquacer-tpm-mmio"},
	{},
};
MODULE_DEVICE_TABLE(of, tis_synquacer_of_platform_match);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id tpm_synquacer_acpi_tbl[] = {
	{ "SCX0009" },
	{},
};
MODULE_DEVICE_TABLE(acpi, tpm_synquacer_acpi_tbl);
#endif

static struct platform_driver tis_synquacer_drv = {
	.probe = tpm_tis_synquacer_probe,
	.remove_new = tpm_tis_synquacer_remove,
	.driver = {
		.name		= "tpm_tis_synquacer",
		.pm		= &tpm_tis_synquacer_pm,
		.of_match_table = of_match_ptr(tis_synquacer_of_platform_match),
		.acpi_match_table = ACPI_PTR(tpm_synquacer_acpi_tbl),
	},
};

static int __init tpm_tis_synquacer_module_init(void)
{
	int rc;

	rc = platform_driver_register(&tis_synquacer_drv);
	if (rc)
		return rc;

	return 0;
}

static void __exit tpm_tis_synquacer_module_exit(void)
{
	platform_driver_unregister(&tis_synquacer_drv);
}

module_init(tpm_tis_synquacer_module_init);
module_exit(tpm_tis_synquacer_module_exit);
MODULE_DESCRIPTION("TPM MMIO Driver for Socionext SynQuacer platform");
MODULE_LICENSE("GPL");
