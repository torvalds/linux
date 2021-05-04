// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD SoC Power Management Controller Driver
 *
 * Copyright (c) 2020, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/suspend.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

/* SMU communication registers */
#define AMD_PMC_REGISTER_MESSAGE	0x538
#define AMD_PMC_REGISTER_RESPONSE	0x980
#define AMD_PMC_REGISTER_ARGUMENT	0x9BC

/* Base address of SMU for mapping physical address to virtual address */
#define AMD_PMC_SMU_INDEX_ADDRESS	0xB8
#define AMD_PMC_SMU_INDEX_DATA		0xBC
#define AMD_PMC_MAPPING_SIZE		0x01000
#define AMD_PMC_BASE_ADDR_OFFSET	0x10000
#define AMD_PMC_BASE_ADDR_LO		0x13B102E8
#define AMD_PMC_BASE_ADDR_HI		0x13B102EC
#define AMD_PMC_BASE_ADDR_LO_MASK	GENMASK(15, 0)
#define AMD_PMC_BASE_ADDR_HI_MASK	GENMASK(31, 20)

/* SMU Response Codes */
#define AMD_PMC_RESULT_OK                    0x01
#define AMD_PMC_RESULT_CMD_REJECT_BUSY       0xFC
#define AMD_PMC_RESULT_CMD_REJECT_PREREQ     0xFD
#define AMD_PMC_RESULT_CMD_UNKNOWN           0xFE
#define AMD_PMC_RESULT_FAILED                0xFF

/* List of supported CPU ids */
#define AMD_CPU_ID_RV			0x15D0
#define AMD_CPU_ID_RN			0x1630
#define AMD_CPU_ID_PCO			AMD_CPU_ID_RV
#define AMD_CPU_ID_CZN			AMD_CPU_ID_RN

#define AMD_SMU_FW_VERSION		0x0
#define PMC_MSG_DELAY_MIN_US		100
#define RESPONSE_REGISTER_LOOP_MAX	200

enum amd_pmc_def {
	MSG_TEST = 0x01,
	MSG_OS_HINT_PCO,
	MSG_OS_HINT_RN,
};

struct amd_pmc_dev {
	void __iomem *regbase;
	void __iomem *smu_base;
	u32 base_addr;
	u32 cpu_id;
	struct device *dev;
#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct dentry *dbgfs_dir;
#endif /* CONFIG_DEBUG_FS */
};

static struct amd_pmc_dev pmc;

static inline u32 amd_pmc_reg_read(struct amd_pmc_dev *dev, int reg_offset)
{
	return ioread32(dev->regbase + reg_offset);
}

static inline void amd_pmc_reg_write(struct amd_pmc_dev *dev, int reg_offset, u32 val)
{
	iowrite32(val, dev->regbase + reg_offset);
}

#ifdef CONFIG_DEBUG_FS
static int smu_fw_info_show(struct seq_file *s, void *unused)
{
	struct amd_pmc_dev *dev = s->private;
	u32 value;

	value = ioread32(dev->smu_base + AMD_SMU_FW_VERSION);
	seq_printf(s, "SMU FW Info: %x\n", value);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(smu_fw_info);

static void amd_pmc_dbgfs_unregister(struct amd_pmc_dev *dev)
{
	debugfs_remove_recursive(dev->dbgfs_dir);
}

static void amd_pmc_dbgfs_register(struct amd_pmc_dev *dev)
{
	dev->dbgfs_dir = debugfs_create_dir("amd_pmc", NULL);
	debugfs_create_file("smu_fw_info", 0644, dev->dbgfs_dir, dev,
			    &smu_fw_info_fops);
}
#else
static inline void amd_pmc_dbgfs_register(struct amd_pmc_dev *dev)
{
}

static inline void amd_pmc_dbgfs_unregister(struct amd_pmc_dev *dev)
{
}
#endif /* CONFIG_DEBUG_FS */

static void amd_pmc_dump_registers(struct amd_pmc_dev *dev)
{
	u32 value;

	value = amd_pmc_reg_read(dev, AMD_PMC_REGISTER_RESPONSE);
	dev_dbg(dev->dev, "AMD_PMC_REGISTER_RESPONSE:%x\n", value);

	value = amd_pmc_reg_read(dev, AMD_PMC_REGISTER_ARGUMENT);
	dev_dbg(dev->dev, "AMD_PMC_REGISTER_ARGUMENT:%x\n", value);

	value = amd_pmc_reg_read(dev, AMD_PMC_REGISTER_MESSAGE);
	dev_dbg(dev->dev, "AMD_PMC_REGISTER_MESSAGE:%x\n", value);
}

static int amd_pmc_send_cmd(struct amd_pmc_dev *dev, bool set)
{
	int rc;
	u8 msg;
	u32 val;

	/* Wait until we get a valid response */
	rc = readx_poll_timeout(ioread32, dev->regbase + AMD_PMC_REGISTER_RESPONSE,
				val, val > 0, PMC_MSG_DELAY_MIN_US,
				PMC_MSG_DELAY_MIN_US * RESPONSE_REGISTER_LOOP_MAX);
	if (rc) {
		dev_err(dev->dev, "failed to talk to SMU\n");
		return rc;
	}

	/* Write zero to response register */
	amd_pmc_reg_write(dev, AMD_PMC_REGISTER_RESPONSE, 0);

	/* Write argument into response register */
	amd_pmc_reg_write(dev, AMD_PMC_REGISTER_ARGUMENT, set);

	/* Write message ID to message ID register */
	msg = (dev->cpu_id == AMD_CPU_ID_RN) ? MSG_OS_HINT_RN : MSG_OS_HINT_PCO;
	amd_pmc_reg_write(dev, AMD_PMC_REGISTER_MESSAGE, msg);
	return 0;
}

static int __maybe_unused amd_pmc_suspend(struct device *dev)
{
	struct amd_pmc_dev *pdev = dev_get_drvdata(dev);
	int rc;

	rc = amd_pmc_send_cmd(pdev, 1);
	if (rc)
		dev_err(pdev->dev, "suspend failed\n");

	amd_pmc_dump_registers(pdev);
	return 0;
}

static int __maybe_unused amd_pmc_resume(struct device *dev)
{
	struct amd_pmc_dev *pdev = dev_get_drvdata(dev);
	int rc;

	rc = amd_pmc_send_cmd(pdev, 0);
	if (rc)
		dev_err(pdev->dev, "resume failed\n");

	amd_pmc_dump_registers(pdev);
	return 0;
}

static const struct dev_pm_ops amd_pmc_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(amd_pmc_suspend, amd_pmc_resume)
};

static const struct pci_device_id pmc_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, AMD_CPU_ID_CZN) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, AMD_CPU_ID_RN) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, AMD_CPU_ID_PCO) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, AMD_CPU_ID_RV) },
	{ }
};

static int amd_pmc_probe(struct platform_device *pdev)
{
	struct amd_pmc_dev *dev = &pmc;
	struct pci_dev *rdev;
	u32 base_addr_lo;
	u32 base_addr_hi;
	u64 base_addr;
	int err;
	u32 val;

	dev->dev = &pdev->dev;

	rdev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(0, 0));
	if (!rdev || !pci_match_id(pmc_pci_ids, rdev)) {
		pci_dev_put(rdev);
		return -ENODEV;
	}

	dev->cpu_id = rdev->device;
	err = pci_write_config_dword(rdev, AMD_PMC_SMU_INDEX_ADDRESS, AMD_PMC_BASE_ADDR_LO);
	if (err) {
		dev_err(dev->dev, "error writing to 0x%x\n", AMD_PMC_SMU_INDEX_ADDRESS);
		pci_dev_put(rdev);
		return pcibios_err_to_errno(err);
	}

	err = pci_read_config_dword(rdev, AMD_PMC_SMU_INDEX_DATA, &val);
	if (err) {
		pci_dev_put(rdev);
		return pcibios_err_to_errno(err);
	}

	base_addr_lo = val & AMD_PMC_BASE_ADDR_HI_MASK;

	err = pci_write_config_dword(rdev, AMD_PMC_SMU_INDEX_ADDRESS, AMD_PMC_BASE_ADDR_HI);
	if (err) {
		dev_err(dev->dev, "error writing to 0x%x\n", AMD_PMC_SMU_INDEX_ADDRESS);
		pci_dev_put(rdev);
		return pcibios_err_to_errno(err);
	}

	err = pci_read_config_dword(rdev, AMD_PMC_SMU_INDEX_DATA, &val);
	if (err) {
		pci_dev_put(rdev);
		return pcibios_err_to_errno(err);
	}

	base_addr_hi = val & AMD_PMC_BASE_ADDR_LO_MASK;
	pci_dev_put(rdev);
	base_addr = ((u64)base_addr_hi << 32 | base_addr_lo);

	dev->smu_base = devm_ioremap(dev->dev, base_addr, AMD_PMC_MAPPING_SIZE);
	if (!dev->smu_base)
		return -ENOMEM;

	dev->regbase = devm_ioremap(dev->dev, base_addr + AMD_PMC_BASE_ADDR_OFFSET,
				    AMD_PMC_MAPPING_SIZE);
	if (!dev->regbase)
		return -ENOMEM;

	amd_pmc_dump_registers(dev);

	platform_set_drvdata(pdev, dev);
	amd_pmc_dbgfs_register(dev);
	return 0;
}

static int amd_pmc_remove(struct platform_device *pdev)
{
	struct amd_pmc_dev *dev = platform_get_drvdata(pdev);

	amd_pmc_dbgfs_unregister(dev);
	return 0;
}

static const struct acpi_device_id amd_pmc_acpi_ids[] = {
	{"AMDI0005", 0},
	{"AMD0004", 0},
	{ }
};
MODULE_DEVICE_TABLE(acpi, amd_pmc_acpi_ids);

static struct platform_driver amd_pmc_driver = {
	.driver = {
		.name = "amd_pmc",
		.acpi_match_table = amd_pmc_acpi_ids,
		.pm = &amd_pmc_pm_ops,
	},
	.probe = amd_pmc_probe,
	.remove = amd_pmc_remove,
};
module_platform_driver(amd_pmc_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("AMD PMC Driver");
