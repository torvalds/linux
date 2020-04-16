// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the Intel PMC IPC mechanism
 *
 * (C) Copyright 2014-2015 Intel Corporation
 *
 * This driver is based on Intel SCU IPC driver(intel_scu_ipc.c) by
 *     Sreedhara DS <sreedhara.ds@intel.com>
 *
 * PMC running in ARC processor communicates with other entity running in IA
 * core through IPC mechanism which in turn messaging between IA core ad PMC.
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>

#include <asm/intel_pmc_ipc.h>
#include <asm/intel_scu_ipc.h>

#include <linux/platform_data/itco_wdt.h>

/* Residency with clock rate at 19.2MHz to usecs */
#define S0IX_RESIDENCY_IN_USECS(d, s)		\
({						\
	u64 result = 10ull * ((d) + (s));	\
	do_div(result, 192);			\
	result;					\
})

/* exported resources from IFWI */
#define PLAT_RESOURCE_IPC_INDEX		0
#define PLAT_RESOURCE_IPC_SIZE		0x1000
#define PLAT_RESOURCE_GCR_OFFSET	0x1000
#define PLAT_RESOURCE_GCR_SIZE		0x1000
#define PLAT_RESOURCE_BIOS_DATA_INDEX	1
#define PLAT_RESOURCE_BIOS_IFACE_INDEX	2
#define PLAT_RESOURCE_TELEM_SSRAM_INDEX	3
#define PLAT_RESOURCE_ISP_DATA_INDEX	4
#define PLAT_RESOURCE_ISP_IFACE_INDEX	5
#define PLAT_RESOURCE_GTD_DATA_INDEX	6
#define PLAT_RESOURCE_GTD_IFACE_INDEX	7
#define PLAT_RESOURCE_ACPI_IO_INDEX	0

/*
 * BIOS does not create an ACPI device for each PMC function,
 * but exports multiple resources from one ACPI device(IPC) for
 * multiple functions. This driver is responsible to create a
 * platform device and to export resources for those functions.
 */
#define TCO_DEVICE_NAME			"iTCO_wdt"
#define SMI_EN_OFFSET			0x40
#define SMI_EN_SIZE			4
#define TCO_BASE_OFFSET			0x60
#define TCO_REGS_SIZE			16
#define PUNIT_DEVICE_NAME		"intel_punit_ipc"
#define TELEMETRY_DEVICE_NAME		"intel_telemetry"
#define TELEM_SSRAM_SIZE		240
#define TELEM_PMC_SSRAM_OFFSET		0x1B00
#define TELEM_PUNIT_SSRAM_OFFSET	0x1A00
#define TCO_PMC_OFFSET			0x08
#define TCO_PMC_SIZE			0x04

/* PMC register bit definitions */

/* PMC_CFG_REG bit masks */
#define PMC_CFG_NO_REBOOT_MASK		BIT_MASK(4)
#define PMC_CFG_NO_REBOOT_EN		(1 << 4)
#define PMC_CFG_NO_REBOOT_DIS		(0 << 4)

static struct intel_pmc_ipc_dev {
	struct device *dev;

	/* The following PMC BARs share the same ACPI device with the IPC */
	resource_size_t acpi_io_base;
	int acpi_io_size;
	struct platform_device *tco_dev;

	/* gcr */
	void __iomem *gcr_mem_base;
	bool has_gcr_regs;
	spinlock_t gcr_lock;

	/* punit */
	struct platform_device *punit_dev;
	unsigned int punit_res_count;

	/* Telemetry */
	resource_size_t telem_pmc_ssram_base;
	resource_size_t telem_punit_ssram_base;
	int telem_pmc_ssram_size;
	int telem_punit_ssram_size;
	u8 telem_res_inval;
	struct platform_device *telemetry_dev;
} ipcdev;

static inline u64 gcr_data_readq(u32 offset)
{
	return readq(ipcdev.gcr_mem_base + offset);
}

static inline int is_gcr_valid(u32 offset)
{
	if (!ipcdev.has_gcr_regs)
		return -EACCES;

	if (offset > PLAT_RESOURCE_GCR_SIZE)
		return -EINVAL;

	return 0;
}

/**
 * intel_pmc_gcr_read64() - Read a 64-bit PMC GCR register
 * @offset:	offset of GCR register from GCR address base
 * @data:	data pointer for storing the register output
 *
 * Reads the 64-bit PMC GCR register at given offset.
 *
 * Return:	negative value on error or 0 on success.
 */
int intel_pmc_gcr_read64(u32 offset, u64 *data)
{
	int ret;

	spin_lock(&ipcdev.gcr_lock);

	ret = is_gcr_valid(offset);
	if (ret < 0) {
		spin_unlock(&ipcdev.gcr_lock);
		return ret;
	}

	*data = readq(ipcdev.gcr_mem_base + offset);

	spin_unlock(&ipcdev.gcr_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(intel_pmc_gcr_read64);

/**
 * intel_pmc_gcr_update() - Update PMC GCR register bits
 * @offset:	offset of GCR register from GCR address base
 * @mask:	bit mask for update operation
 * @val:	update value
 *
 * Updates the bits of given GCR register as specified by
 * @mask and @val.
 *
 * Return:	negative value on error or 0 on success.
 */
static int intel_pmc_gcr_update(u32 offset, u32 mask, u32 val)
{
	u32 new_val;
	int ret = 0;

	spin_lock(&ipcdev.gcr_lock);

	ret = is_gcr_valid(offset);
	if (ret < 0)
		goto gcr_ipc_unlock;

	new_val = readl(ipcdev.gcr_mem_base + offset);

	new_val &= ~mask;
	new_val |= val & mask;

	writel(new_val, ipcdev.gcr_mem_base + offset);

	new_val = readl(ipcdev.gcr_mem_base + offset);

	/* check whether the bit update is successful */
	if ((new_val & mask) != (val & mask)) {
		ret = -EIO;
		goto gcr_ipc_unlock;
	}

gcr_ipc_unlock:
	spin_unlock(&ipcdev.gcr_lock);
	return ret;
}

static int update_no_reboot_bit(void *priv, bool set)
{
	u32 value = set ? PMC_CFG_NO_REBOOT_EN : PMC_CFG_NO_REBOOT_DIS;

	return intel_pmc_gcr_update(PMC_GCR_PMC_CFG_REG,
				    PMC_CFG_NO_REBOOT_MASK, value);
}

static int ipc_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct intel_pmc_ipc_dev *pmc = &ipcdev;
	struct intel_scu_ipc_data scu_data = {};
	struct intel_scu_ipc_dev *scu;
	int ret;

	/* Only one PMC is supported */
	if (pmc->dev)
		return -EBUSY;

	spin_lock_init(&ipcdev.gcr_lock);

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	scu_data.mem = pdev->resource[0];

	scu = devm_intel_scu_ipc_register(&pdev->dev, &scu_data);
	if (IS_ERR(scu))
		return PTR_ERR(scu);

	pmc->dev = &pdev->dev;

	pci_set_drvdata(pdev, pmc);

	return 0;
}

static const struct pci_device_id ipc_pci_ids[] = {
	{PCI_VDEVICE(INTEL, 0x0a94), 0},
	{PCI_VDEVICE(INTEL, 0x1a94), 0},
	{PCI_VDEVICE(INTEL, 0x5a94), 0},
	{ 0,}
};
MODULE_DEVICE_TABLE(pci, ipc_pci_ids);

static struct pci_driver ipc_pci_driver = {
	.name = "intel_pmc_ipc",
	.id_table = ipc_pci_ids,
	.probe = ipc_pci_probe,
};

static ssize_t intel_pmc_ipc_simple_cmd_store(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t count)
{
	struct intel_scu_ipc_dev *scu = dev_get_drvdata(dev);
	int subcmd;
	int cmd;
	int ret;

	ret = sscanf(buf, "%d %d", &cmd, &subcmd);
	if (ret != 2) {
		dev_err(dev, "Error args\n");
		return -EINVAL;
	}

	ret = intel_scu_ipc_dev_simple_command(scu, cmd, subcmd);
	if (ret) {
		dev_err(dev, "command %d error with %d\n", cmd, ret);
		return ret;
	}
	return (ssize_t)count;
}
static DEVICE_ATTR(simplecmd, 0200, NULL, intel_pmc_ipc_simple_cmd_store);

static ssize_t intel_pmc_ipc_northpeak_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t count)
{
	struct intel_scu_ipc_dev *scu = dev_get_drvdata(dev);
	unsigned long val;
	int subcmd;
	int ret;

	ret = kstrtoul(buf, 0, &val);
	if (ret)
		return ret;

	if (val)
		subcmd = 1;
	else
		subcmd = 0;
	ret = intel_scu_ipc_dev_simple_command(scu, PMC_IPC_NORTHPEAK_CTRL, subcmd);
	if (ret) {
		dev_err(dev, "command north %d error with %d\n", subcmd, ret);
		return ret;
	}
	return (ssize_t)count;
}
static DEVICE_ATTR(northpeak, 0200, NULL, intel_pmc_ipc_northpeak_store);

static struct attribute *intel_ipc_attrs[] = {
	&dev_attr_northpeak.attr,
	&dev_attr_simplecmd.attr,
	NULL
};

static const struct attribute_group intel_ipc_group = {
	.attrs = intel_ipc_attrs,
};

static const struct attribute_group *intel_ipc_groups[] = {
	&intel_ipc_group,
	NULL
};

static struct resource punit_res_array[] = {
	/* Punit BIOS */
	{
		.flags = IORESOURCE_MEM,
	},
	{
		.flags = IORESOURCE_MEM,
	},
	/* Punit ISP */
	{
		.flags = IORESOURCE_MEM,
	},
	{
		.flags = IORESOURCE_MEM,
	},
	/* Punit GTD */
	{
		.flags = IORESOURCE_MEM,
	},
	{
		.flags = IORESOURCE_MEM,
	},
};

#define TCO_RESOURCE_ACPI_IO		0
#define TCO_RESOURCE_SMI_EN_IO		1
#define TCO_RESOURCE_GCR_MEM		2
static struct resource tco_res[] = {
	/* ACPI - TCO */
	{
		.flags = IORESOURCE_IO,
	},
	/* ACPI - SMI */
	{
		.flags = IORESOURCE_IO,
	},
};

static struct itco_wdt_platform_data tco_info = {
	.name = "Apollo Lake SoC",
	.version = 5,
	.no_reboot_priv = &ipcdev,
	.update_no_reboot_bit = update_no_reboot_bit,
};

#define TELEMETRY_RESOURCE_PUNIT_SSRAM	0
#define TELEMETRY_RESOURCE_PMC_SSRAM	1
static struct resource telemetry_res[] = {
	/*Telemetry*/
	{
		.flags = IORESOURCE_MEM,
	},
	{
		.flags = IORESOURCE_MEM,
	},
};

static int ipc_create_punit_device(void)
{
	struct platform_device *pdev;
	const struct platform_device_info pdevinfo = {
		.parent = ipcdev.dev,
		.name = PUNIT_DEVICE_NAME,
		.id = -1,
		.res = punit_res_array,
		.num_res = ipcdev.punit_res_count,
		};

	pdev = platform_device_register_full(&pdevinfo);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	ipcdev.punit_dev = pdev;

	return 0;
}

static int ipc_create_tco_device(void)
{
	struct platform_device *pdev;
	struct resource *res;
	const struct platform_device_info pdevinfo = {
		.parent = ipcdev.dev,
		.name = TCO_DEVICE_NAME,
		.id = -1,
		.res = tco_res,
		.num_res = ARRAY_SIZE(tco_res),
		.data = &tco_info,
		.size_data = sizeof(tco_info),
		};

	res = tco_res + TCO_RESOURCE_ACPI_IO;
	res->start = ipcdev.acpi_io_base + TCO_BASE_OFFSET;
	res->end = res->start + TCO_REGS_SIZE - 1;

	res = tco_res + TCO_RESOURCE_SMI_EN_IO;
	res->start = ipcdev.acpi_io_base + SMI_EN_OFFSET;
	res->end = res->start + SMI_EN_SIZE - 1;

	pdev = platform_device_register_full(&pdevinfo);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	ipcdev.tco_dev = pdev;

	return 0;
}

static int ipc_create_telemetry_device(void)
{
	struct platform_device *pdev;
	struct resource *res;
	const struct platform_device_info pdevinfo = {
		.parent = ipcdev.dev,
		.name = TELEMETRY_DEVICE_NAME,
		.id = -1,
		.res = telemetry_res,
		.num_res = ARRAY_SIZE(telemetry_res),
		};

	res = telemetry_res + TELEMETRY_RESOURCE_PUNIT_SSRAM;
	res->start = ipcdev.telem_punit_ssram_base;
	res->end = res->start + ipcdev.telem_punit_ssram_size - 1;

	res = telemetry_res + TELEMETRY_RESOURCE_PMC_SSRAM;
	res->start = ipcdev.telem_pmc_ssram_base;
	res->end = res->start + ipcdev.telem_pmc_ssram_size - 1;

	pdev = platform_device_register_full(&pdevinfo);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	ipcdev.telemetry_dev = pdev;

	return 0;
}

static int ipc_create_pmc_devices(void)
{
	int ret;

	/* If we have ACPI based watchdog use that instead */
	if (!acpi_has_watchdog()) {
		ret = ipc_create_tco_device();
		if (ret) {
			dev_err(ipcdev.dev, "Failed to add tco platform device\n");
			return ret;
		}
	}

	ret = ipc_create_punit_device();
	if (ret) {
		dev_err(ipcdev.dev, "Failed to add punit platform device\n");
		platform_device_unregister(ipcdev.tco_dev);
		return ret;
	}

	if (!ipcdev.telem_res_inval) {
		ret = ipc_create_telemetry_device();
		if (ret) {
			dev_warn(ipcdev.dev,
				"Failed to add telemetry platform device\n");
			platform_device_unregister(ipcdev.punit_dev);
			platform_device_unregister(ipcdev.tco_dev);
		}
	}

	return ret;
}

static int ipc_plat_get_res(struct platform_device *pdev,
			    struct intel_scu_ipc_data *scu_data)
{
	struct resource *res, *punit_res = punit_res_array;
	resource_size_t start;
	void __iomem *addr;
	int size;

	res = platform_get_resource(pdev, IORESOURCE_IO,
				    PLAT_RESOURCE_ACPI_IO_INDEX);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get io resource\n");
		return -ENXIO;
	}
	size = resource_size(res);
	ipcdev.acpi_io_base = res->start;
	ipcdev.acpi_io_size = size;
	dev_info(&pdev->dev, "io res: %pR\n", res);

	ipcdev.punit_res_count = 0;

	/* This is index 0 to cover BIOS data register */
	res = platform_get_resource(pdev, IORESOURCE_MEM,
				    PLAT_RESOURCE_BIOS_DATA_INDEX);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get res of punit BIOS data\n");
		return -ENXIO;
	}
	punit_res[ipcdev.punit_res_count++] = *res;
	dev_info(&pdev->dev, "punit BIOS data res: %pR\n", res);

	/* This is index 1 to cover BIOS interface register */
	res = platform_get_resource(pdev, IORESOURCE_MEM,
				    PLAT_RESOURCE_BIOS_IFACE_INDEX);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get res of punit BIOS iface\n");
		return -ENXIO;
	}
	punit_res[ipcdev.punit_res_count++] = *res;
	dev_info(&pdev->dev, "punit BIOS interface res: %pR\n", res);

	/* This is index 2 to cover ISP data register, optional */
	res = platform_get_resource(pdev, IORESOURCE_MEM,
				    PLAT_RESOURCE_ISP_DATA_INDEX);
	if (res) {
		punit_res[ipcdev.punit_res_count++] = *res;
		dev_info(&pdev->dev, "punit ISP data res: %pR\n", res);
	}

	/* This is index 3 to cover ISP interface register, optional */
	res = platform_get_resource(pdev, IORESOURCE_MEM,
				    PLAT_RESOURCE_ISP_IFACE_INDEX);
	if (res) {
		punit_res[ipcdev.punit_res_count++] = *res;
		dev_info(&pdev->dev, "punit ISP interface res: %pR\n", res);
	}

	/* This is index 4 to cover GTD data register, optional */
	res = platform_get_resource(pdev, IORESOURCE_MEM,
				    PLAT_RESOURCE_GTD_DATA_INDEX);
	if (res) {
		punit_res[ipcdev.punit_res_count++] = *res;
		dev_info(&pdev->dev, "punit GTD data res: %pR\n", res);
	}

	/* This is index 5 to cover GTD interface register, optional */
	res = platform_get_resource(pdev, IORESOURCE_MEM,
				    PLAT_RESOURCE_GTD_IFACE_INDEX);
	if (res) {
		punit_res[ipcdev.punit_res_count++] = *res;
		dev_info(&pdev->dev, "punit GTD interface res: %pR\n", res);
	}

	scu_data->irq = platform_get_irq(pdev, 0);

	res = platform_get_resource(pdev, IORESOURCE_MEM,
				    PLAT_RESOURCE_IPC_INDEX);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get ipc resource\n");
		return -ENXIO;
	}
	dev_info(&pdev->dev, "ipc res: %pR\n", res);

	scu_data->mem.flags = res->flags;
	scu_data->mem.start = res->start;
	scu_data->mem.end = res->start + PLAT_RESOURCE_IPC_SIZE - 1;

	start = res->start + PLAT_RESOURCE_GCR_OFFSET;
	if (!devm_request_mem_region(&pdev->dev, start, PLAT_RESOURCE_GCR_SIZE,
				     "pmc_ipc_plat"))
		return -EBUSY;

	addr = devm_ioremap(&pdev->dev, start, PLAT_RESOURCE_GCR_SIZE);
	if (!addr)
		return -ENOMEM;

	ipcdev.gcr_mem_base = addr;

	ipcdev.telem_res_inval = 0;
	res = platform_get_resource(pdev, IORESOURCE_MEM,
				    PLAT_RESOURCE_TELEM_SSRAM_INDEX);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get telemetry ssram resource\n");
		ipcdev.telem_res_inval = 1;
	} else {
		ipcdev.telem_punit_ssram_base = res->start +
						TELEM_PUNIT_SSRAM_OFFSET;
		ipcdev.telem_punit_ssram_size = TELEM_SSRAM_SIZE;
		ipcdev.telem_pmc_ssram_base = res->start +
						TELEM_PMC_SSRAM_OFFSET;
		ipcdev.telem_pmc_ssram_size = TELEM_SSRAM_SIZE;
		dev_info(&pdev->dev, "telemetry ssram res: %pR\n", res);
	}

	return 0;
}

/**
 * intel_pmc_s0ix_counter_read() - Read S0ix residency.
 * @data: Out param that contains current S0ix residency count.
 *
 * Return: an error code or 0 on success.
 */
int intel_pmc_s0ix_counter_read(u64 *data)
{
	u64 deep, shlw;

	if (!ipcdev.has_gcr_regs)
		return -EACCES;

	deep = gcr_data_readq(PMC_GCR_TELEM_DEEP_S0IX_REG);
	shlw = gcr_data_readq(PMC_GCR_TELEM_SHLW_S0IX_REG);

	*data = S0IX_RESIDENCY_IN_USECS(deep, shlw);

	return 0;
}
EXPORT_SYMBOL_GPL(intel_pmc_s0ix_counter_read);

#ifdef CONFIG_ACPI
static const struct acpi_device_id ipc_acpi_ids[] = {
	{ "INT34D2", 0},
	{ }
};
MODULE_DEVICE_TABLE(acpi, ipc_acpi_ids);
#endif

static int ipc_plat_probe(struct platform_device *pdev)
{
	struct intel_scu_ipc_data scu_data = {};
	struct intel_scu_ipc_dev *scu;
	int ret;

	ipcdev.dev = &pdev->dev;
	spin_lock_init(&ipcdev.gcr_lock);

	ret = ipc_plat_get_res(pdev, &scu_data);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request resource\n");
		return ret;
	}

	scu = devm_intel_scu_ipc_register(&pdev->dev, &scu_data);
	if (IS_ERR(scu))
		return PTR_ERR(scu);

	platform_set_drvdata(pdev, scu);

	ret = ipc_create_pmc_devices();
	if (ret) {
		dev_err(&pdev->dev, "Failed to create pmc devices\n");
		return ret;
	}

	ipcdev.has_gcr_regs = true;

	return 0;
}

static int ipc_plat_remove(struct platform_device *pdev)
{
	platform_device_unregister(ipcdev.tco_dev);
	platform_device_unregister(ipcdev.punit_dev);
	platform_device_unregister(ipcdev.telemetry_dev);
	ipcdev.dev = NULL;
	return 0;
}

static struct platform_driver ipc_plat_driver = {
	.remove = ipc_plat_remove,
	.probe = ipc_plat_probe,
	.driver = {
		.name = "pmc-ipc-plat",
		.acpi_match_table = ACPI_PTR(ipc_acpi_ids),
		.dev_groups = intel_ipc_groups,
	},
};

static int __init intel_pmc_ipc_init(void)
{
	int ret;

	ret = platform_driver_register(&ipc_plat_driver);
	if (ret) {
		pr_err("Failed to register PMC ipc platform driver\n");
		return ret;
	}
	ret = pci_register_driver(&ipc_pci_driver);
	if (ret) {
		pr_err("Failed to register PMC ipc pci driver\n");
		platform_driver_unregister(&ipc_plat_driver);
		return ret;
	}
	return ret;
}

static void __exit intel_pmc_ipc_exit(void)
{
	pci_unregister_driver(&ipc_pci_driver);
	platform_driver_unregister(&ipc_plat_driver);
}

MODULE_AUTHOR("Zha Qipeng <qipeng.zha@intel.com>");
MODULE_DESCRIPTION("Intel PMC IPC driver");
MODULE_LICENSE("GPL v2");

/* Some modules are dependent on this, so init earlier */
fs_initcall(intel_pmc_ipc_init);
module_exit(intel_pmc_ipc_exit);
