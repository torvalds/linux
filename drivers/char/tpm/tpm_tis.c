/*
 * Copyright (C) 2005, 2006 IBM Corporation
 * Copyright (C) 2014, 2015 Intel Corporation
 *
 * Authors:
 * Leendert van Doorn <leendert@watson.ibm.com>
 * Kylene Hall <kjhall@us.ibm.com>
 *
 * Maintained by: <tpmdd-devel@lists.sourceforge.net>
 *
 * Device driver for TCG/TCPA TPM (trusted platform module).
 * Specifications at www.trustedcomputinggroup.org
 *
 * This device driver implements the TPM interface as defined in
 * the TCG TPM Interface Spec version 1.2, revision 1.0.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pnp.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/acpi.h>
#include <linux/freezer.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/kernel.h>
#include "tpm.h"
#include "tpm_tis_core.h"

struct tpm_info {
	struct resource res;
	/* irq > 0 means: use irq $irq;
	 * irq = 0 means: autoprobe for an irq;
	 * irq = -1 means: no irq support
	 */
	int irq;
};

struct tpm_tis_tcg_phy {
	struct tpm_tis_data priv;
	void __iomem *iobase;
};

static inline struct tpm_tis_tcg_phy *to_tpm_tis_tcg_phy(struct tpm_tis_data *data)
{
	return container_of(data, struct tpm_tis_tcg_phy, priv);
}

static bool interrupts = true;
module_param(interrupts, bool, 0444);
MODULE_PARM_DESC(interrupts, "Enable interrupts");

static bool itpm;
module_param(itpm, bool, 0444);
MODULE_PARM_DESC(itpm, "Force iTPM workarounds (found on some Lenovo laptops)");

static bool force;
#ifdef CONFIG_X86
module_param(force, bool, 0444);
MODULE_PARM_DESC(force, "Force device probe rather than using ACPI entry");
#endif

#if defined(CONFIG_PNP) && defined(CONFIG_ACPI)
static int has_hid(struct acpi_device *dev, const char *hid)
{
	struct acpi_hardware_id *id;

	list_for_each_entry(id, &dev->pnp.ids, list)
		if (!strcmp(hid, id->id))
			return 1;

	return 0;
}

static inline int is_itpm(struct acpi_device *dev)
{
	if (!dev)
		return 0;
	return has_hid(dev, "INTC0102");
}
#else
static inline int is_itpm(struct acpi_device *dev)
{
	return 0;
}
#endif

#if defined(CONFIG_ACPI)
#define DEVICE_IS_TPM2 1

static const struct acpi_device_id tpm_acpi_tbl[] = {
	{"MSFT0101", DEVICE_IS_TPM2},
	{},
};
MODULE_DEVICE_TABLE(acpi, tpm_acpi_tbl);

static int check_acpi_tpm2(struct device *dev)
{
	const struct acpi_device_id *aid = acpi_match_device(tpm_acpi_tbl, dev);
	struct acpi_table_tpm2 *tbl;
	acpi_status st;

	if (!aid || aid->driver_data != DEVICE_IS_TPM2)
		return 0;

	/* If the ACPI TPM2 signature is matched then a global ACPI_SIG_TPM2
	 * table is mandatory
	 */
	st =
	    acpi_get_table(ACPI_SIG_TPM2, 1, (struct acpi_table_header **)&tbl);
	if (ACPI_FAILURE(st) || tbl->header.length < sizeof(*tbl)) {
		dev_err(dev, FW_BUG "failed to get TPM2 ACPI table\n");
		return -EINVAL;
	}

	/* The tpm2_crb driver handles this device */
	if (tbl->start_method != ACPI_TPM2_MEMORY_MAPPED)
		return -ENODEV;

	return 0;
}
#else
static int check_acpi_tpm2(struct device *dev)
{
	return 0;
}
#endif

static int tpm_tcg_read_bytes(struct tpm_tis_data *data, u32 addr, u16 len,
			      u8 *result)
{
	struct tpm_tis_tcg_phy *phy = to_tpm_tis_tcg_phy(data);

	if (is_bsw() && !(data->flags & TPM_TIS_CLK_ENABLE))
		WARN(1, "CLKRUN not enabled!\n");

	while (len--)
		*result++ = ioread8(phy->iobase + addr);

	return 0;
}

static int tpm_tcg_write_bytes(struct tpm_tis_data *data, u32 addr, u16 len,
			       const u8 *value)
{
	struct tpm_tis_tcg_phy *phy = to_tpm_tis_tcg_phy(data);

	if (is_bsw() && !(data->flags & TPM_TIS_CLK_ENABLE))
		WARN(1, "CLKRUN not enabled!\n");

	while (len--)
		iowrite8(*value++, phy->iobase + addr);

	return 0;
}

static int tpm_tcg_read16(struct tpm_tis_data *data, u32 addr, u16 *result)
{
	struct tpm_tis_tcg_phy *phy = to_tpm_tis_tcg_phy(data);

	if (is_bsw() && !(data->flags & TPM_TIS_CLK_ENABLE))
		WARN(1, "CLKRUN not enabled!\n");

	*result = ioread16(phy->iobase + addr);

	return 0;
}

static int tpm_tcg_read32(struct tpm_tis_data *data, u32 addr, u32 *result)
{
	struct tpm_tis_tcg_phy *phy = to_tpm_tis_tcg_phy(data);

	if (is_bsw() && !(data->flags & TPM_TIS_CLK_ENABLE))
		WARN(1, "CLKRUN not enabled!\n");

	*result = ioread32(phy->iobase + addr);

	return 0;
}

static int tpm_tcg_write32(struct tpm_tis_data *data, u32 addr, u32 value)
{
	struct tpm_tis_tcg_phy *phy = to_tpm_tis_tcg_phy(data);

	if (is_bsw() && !(data->flags & TPM_TIS_CLK_ENABLE))
		WARN(1, "CLKRUN not enabled!\n");

	iowrite32(value, phy->iobase + addr);

	return 0;
}

static const struct tpm_tis_phy_ops tpm_tcg = {
	.read_bytes = tpm_tcg_read_bytes,
	.write_bytes = tpm_tcg_write_bytes,
	.read16 = tpm_tcg_read16,
	.read32 = tpm_tcg_read32,
	.write32 = tpm_tcg_write32,
};

static int tpm_tis_init(struct device *dev, struct tpm_info *tpm_info)
{
	struct tpm_tis_tcg_phy *phy;
	int irq = -1;
	int rc;

	rc = check_acpi_tpm2(dev);
	if (rc)
		return rc;

	phy = devm_kzalloc(dev, sizeof(struct tpm_tis_tcg_phy), GFP_KERNEL);
	if (phy == NULL)
		return -ENOMEM;

	phy->iobase = devm_ioremap_resource(dev, &tpm_info->res);
	if (IS_ERR(phy->iobase))
		return PTR_ERR(phy->iobase);

	if (interrupts)
		irq = tpm_info->irq;

	if (itpm || is_itpm(ACPI_COMPANION(dev)))
		phy->priv.flags |= TPM_TIS_ITPM_WORKAROUND;

	return tpm_tis_core_init(dev, &phy->priv, irq, &tpm_tcg,
				 ACPI_HANDLE(dev));
}

static SIMPLE_DEV_PM_OPS(tpm_tis_pm, tpm_pm_suspend, tpm_tis_resume);

static int tpm_tis_pnp_init(struct pnp_dev *pnp_dev,
			    const struct pnp_device_id *pnp_id)
{
	struct tpm_info tpm_info = {};
	struct resource *res;

	res = pnp_get_resource(pnp_dev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;
	tpm_info.res = *res;

	if (pnp_irq_valid(pnp_dev, 0))
		tpm_info.irq = pnp_irq(pnp_dev, 0);
	else
		tpm_info.irq = -1;

	return tpm_tis_init(&pnp_dev->dev, &tpm_info);
}

static struct pnp_device_id tpm_pnp_tbl[] = {
	{"PNP0C31", 0},		/* TPM */
	{"ATM1200", 0},		/* Atmel */
	{"IFX0102", 0},		/* Infineon */
	{"BCM0101", 0},		/* Broadcom */
	{"BCM0102", 0},		/* Broadcom */
	{"NSC1200", 0},		/* National */
	{"ICO0102", 0},		/* Intel */
	/* Add new here */
	{"", 0},		/* User Specified */
	{"", 0}			/* Terminator */
};
MODULE_DEVICE_TABLE(pnp, tpm_pnp_tbl);

static void tpm_tis_pnp_remove(struct pnp_dev *dev)
{
	struct tpm_chip *chip = pnp_get_drvdata(dev);
	struct tpm_tis_data *priv = dev_get_drvdata(&chip->dev);

	tpm_chip_unregister(chip);
	tpm_tis_remove(chip);
}

static struct pnp_driver tis_pnp_driver = {
	.name = "tpm_tis",
	.id_table = tpm_pnp_tbl,
	.probe = tpm_tis_pnp_init,
	.remove = tpm_tis_pnp_remove,
	.driver	= {
		.pm = &tpm_tis_pm,
	},
};

#define TIS_HID_USR_IDX (ARRAY_SIZE(tpm_pnp_tbl) - 2)
module_param_string(hid, tpm_pnp_tbl[TIS_HID_USR_IDX].id,
		    sizeof(tpm_pnp_tbl[TIS_HID_USR_IDX].id), 0444);
MODULE_PARM_DESC(hid, "Set additional specific HID for this driver to probe");

static struct platform_device *force_pdev;

static int tpm_tis_plat_probe(struct platform_device *pdev)
{
	struct tpm_info tpm_info = {};
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "no memory resource defined\n");
		return -ENODEV;
	}
	tpm_info.res = *res;

	tpm_info.irq = platform_get_irq(pdev, 0);
	if (tpm_info.irq <= 0) {
		if (pdev != force_pdev)
			tpm_info.irq = -1;
		else
			/* When forcing auto probe the IRQ */
			tpm_info.irq = 0;
	}

	return tpm_tis_init(&pdev->dev, &tpm_info);
}

static int tpm_tis_plat_remove(struct platform_device *pdev)
{
	struct tpm_chip *chip = dev_get_drvdata(&pdev->dev);
	struct tpm_tis_data *priv = dev_get_drvdata(&chip->dev);

	tpm_chip_unregister(chip);
	tpm_tis_remove(chip);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id tis_of_platform_match[] = {
	{.compatible = "tcg,tpm-tis-mmio"},
	{},
};
MODULE_DEVICE_TABLE(of, tis_of_platform_match);
#endif

static struct platform_driver tis_drv = {
	.probe = tpm_tis_plat_probe,
	.remove = tpm_tis_plat_remove,
	.driver = {
		.name		= "tpm_tis",
		.pm		= &tpm_tis_pm,
		.of_match_table = of_match_ptr(tis_of_platform_match),
		.acpi_match_table = ACPI_PTR(tpm_acpi_tbl),
	},
};

static int tpm_tis_force_device(void)
{
	struct platform_device *pdev;
	static const struct resource x86_resources[] = {
		{
			.start = 0xFED40000,
			.end = 0xFED40000 + TIS_MEM_LEN - 1,
			.flags = IORESOURCE_MEM,
		},
	};

	if (!force)
		return 0;

	/* The driver core will match the name tpm_tis of the device to
	 * the tpm_tis platform driver and complete the setup via
	 * tpm_tis_plat_probe
	 */
	pdev = platform_device_register_simple("tpm_tis", -1, x86_resources,
					       ARRAY_SIZE(x86_resources));
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);
	force_pdev = pdev;

	return 0;
}

static int __init init_tis(void)
{
	int rc;

	rc = tpm_tis_force_device();
	if (rc)
		goto err_force;

	rc = platform_driver_register(&tis_drv);
	if (rc)
		goto err_platform;


	if (IS_ENABLED(CONFIG_PNP)) {
		rc = pnp_register_driver(&tis_pnp_driver);
		if (rc)
			goto err_pnp;
	}

	return 0;

err_pnp:
	platform_driver_unregister(&tis_drv);
err_platform:
	if (force_pdev)
		platform_device_unregister(force_pdev);
err_force:
	return rc;
}

static void __exit cleanup_tis(void)
{
	pnp_unregister_driver(&tis_pnp_driver);
	platform_driver_unregister(&tis_drv);

	if (force_pdev)
		platform_device_unregister(force_pdev);
}

module_init(init_tis);
module_exit(cleanup_tis);
MODULE_AUTHOR("Leendert van Doorn (leendert@watson.ibm.com)");
MODULE_DESCRIPTION("TPM Driver");
MODULE_VERSION("2.0");
MODULE_LICENSE("GPL");
