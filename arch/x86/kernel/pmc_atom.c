/*
 * Intel Atom SOC Power Management Controller Driver
 * Copyright (c) 2014, Intel Corporation.
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
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/io.h>

#include <asm/pmc_atom.h>

struct pmc_dev {
	u32 base_addr;
	void __iomem *regmap;
};

static struct pmc_dev pmc_device;
static u32 acpi_base_addr;

static inline u32 pmc_reg_read(struct pmc_dev *pmc, int reg_offset)
{
	return readl(pmc->regmap + reg_offset);
}

static inline void pmc_reg_write(struct pmc_dev *pmc, int reg_offset, u32 val)
{
	writel(val, pmc->regmap + reg_offset);
}

static void pmc_power_off(void)
{
	u16	pm1_cnt_port;
	u32	pm1_cnt_value;

	pr_info("Preparing to enter system sleep state S5\n");

	pm1_cnt_port = acpi_base_addr + PM1_CNT;

	pm1_cnt_value = inl(pm1_cnt_port);
	pm1_cnt_value &= SLEEP_TYPE_MASK;
	pm1_cnt_value |= SLEEP_TYPE_S5;
	pm1_cnt_value |= SLEEP_ENABLE;

	outl(pm1_cnt_value, pm1_cnt_port);
}

static void pmc_hw_reg_setup(struct pmc_dev *pmc)
{
	/*
	 * Disable PMC S0IX_WAKE_EN events coming from:
	 * - LPC clock run
	 * - GPIO_SUS ored dedicated IRQs
	 * - GPIO_SCORE ored dedicated IRQs
	 * - GPIO_SUS shared IRQ
	 * - GPIO_SCORE shared IRQ
	 */
	pmc_reg_write(pmc, PMC_S0IX_WAKE_EN, (u32)PMC_WAKE_EN_SETTING);
}

static int pmc_setup_dev(struct pci_dev *pdev)
{
	struct pmc_dev *pmc = &pmc_device;

	/* Obtain ACPI base address */
	pci_read_config_dword(pdev, ACPI_BASE_ADDR_OFFSET, &acpi_base_addr);
	acpi_base_addr &= ACPI_BASE_ADDR_MASK;

	/* Install power off function */
	if (acpi_base_addr != 0 && pm_power_off == NULL)
		pm_power_off = pmc_power_off;

	pci_read_config_dword(pdev, PMC_BASE_ADDR_OFFSET, &pmc->base_addr);
	pmc->base_addr &= PMC_BASE_ADDR_MASK;

	pmc->regmap = ioremap_nocache(pmc->base_addr, PMC_MMIO_REG_LEN);
	if (!pmc->regmap) {
		dev_err(&pdev->dev, "error: ioremap failed\n");
		return -ENOMEM;
	}

	/* PMC hardware registers setup */
	pmc_hw_reg_setup(pmc);
	return 0;
}

/*
 * Data for PCI driver interface
 *
 * This data only exists for exporting the supported
 * PCI ids via MODULE_DEVICE_TABLE.  We do not actually
 * register a pci_driver, because lpc_ich will register
 * a driver on the same PCI id.
 */
static const struct pci_device_id pmc_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_VLV_PMC) },
	{ 0, },
};

MODULE_DEVICE_TABLE(pci, pmc_pci_ids);

static int __init pmc_atom_init(void)
{
	int err = -ENODEV;
	struct pci_dev *pdev = NULL;
	const struct pci_device_id *ent;

	/* We look for our device - PCU PMC
	 * we assume that there is max. one device.
	 *
	 * We can't use plain pci_driver mechanism,
	 * as the device is really a multiple function device,
	 * main driver that binds to the pci_device is lpc_ich
	 * and have to find & bind to the device this way.
	 */
	for_each_pci_dev(pdev) {
		ent = pci_match_id(pmc_pci_ids, pdev);
		if (ent) {
			err = pmc_setup_dev(pdev);
			goto out;
		}
	}
	/* Device not found. */
out:
	return err;
}

module_init(pmc_atom_init);
/* no module_exit, this driver shouldn't be unloaded */

MODULE_AUTHOR("Aubrey Li <aubrey.li@linux.intel.com>");
MODULE_DESCRIPTION("Intel Atom SOC Power Management Controller Interface");
MODULE_LICENSE("GPL v2");
