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
#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_qos.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/suspend.h>

#include <asm/intel_pmc_ipc.h>

#include <linux/platform_data/itco_wdt.h>

/*
 * IPC registers
 * The IA write to IPC_CMD command register triggers an interrupt to the ARC,
 * The ARC handles the interrupt and services it, writing optional data to
 * the IPC1 registers, updates the IPC_STS response register with the status.
 */
#define IPC_CMD			0x00
#define		IPC_CMD_MSI		BIT(8)
#define		IPC_CMD_SIZE		16
#define		IPC_CMD_SUBCMD		12
#define IPC_STATUS		0x04
#define		IPC_STATUS_IRQ		BIT(2)
#define		IPC_STATUS_ERR		BIT(1)
#define		IPC_STATUS_BUSY		BIT(0)
#define IPC_SPTR		0x08
#define IPC_DPTR		0x0C
#define IPC_WRITE_BUFFER	0x80
#define IPC_READ_BUFFER		0x90

/* Residency with clock rate at 19.2MHz to usecs */
#define S0IX_RESIDENCY_IN_USECS(d, s)		\
({						\
	u64 result = 10ull * ((d) + (s));	\
	do_div(result, 192);			\
	result;					\
})

/*
 * 16-byte buffer for sending data associated with IPC command.
 */
#define IPC_DATA_BUFFER_SIZE	16

#define IPC_LOOP_CNT		3000000
#define IPC_MAX_SEC		3

#define IPC_TRIGGER_MODE_IRQ		true

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
	void __iomem *ipc_base;
	bool irq_mode;
	int irq;
	int cmd;
	struct completion cmd_complete;

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

static char *ipc_err_sources[] = {
	[IPC_ERR_NONE] =
		"no error",
	[IPC_ERR_CMD_NOT_SUPPORTED] =
		"command not supported",
	[IPC_ERR_CMD_NOT_SERVICED] =
		"command not serviced",
	[IPC_ERR_UNABLE_TO_SERVICE] =
		"unable to service",
	[IPC_ERR_CMD_INVALID] =
		"command invalid",
	[IPC_ERR_CMD_FAILED] =
		"command failed",
	[IPC_ERR_EMSECURITY] =
		"Invalid Battery",
	[IPC_ERR_UNSIGNEDKERNEL] =
		"Unsigned kernel",
};

/* Prevent concurrent calls to the PMC */
static DEFINE_MUTEX(ipclock);

static inline void ipc_send_command(u32 cmd)
{
	ipcdev.cmd = cmd;
	if (ipcdev.irq_mode) {
		reinit_completion(&ipcdev.cmd_complete);
		cmd |= IPC_CMD_MSI;
	}
	writel(cmd, ipcdev.ipc_base + IPC_CMD);
}

static inline u32 ipc_read_status(void)
{
	return readl(ipcdev.ipc_base + IPC_STATUS);
}

static inline void ipc_data_writel(u32 data, u32 offset)
{
	writel(data, ipcdev.ipc_base + IPC_WRITE_BUFFER + offset);
}

static inline u8 __maybe_unused ipc_data_readb(u32 offset)
{
	return readb(ipcdev.ipc_base + IPC_READ_BUFFER + offset);
}

static inline u32 ipc_data_readl(u32 offset)
{
	return readl(ipcdev.ipc_base + IPC_READ_BUFFER + offset);
}

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
 * intel_pmc_gcr_read() - Read a 32-bit PMC GCR register
 * @offset:	offset of GCR register from GCR address base
 * @data:	data pointer for storing the register output
 *
 * Reads the 32-bit PMC GCR register at given offset.
 *
 * Return:	negative value on error or 0 on success.
 */
int intel_pmc_gcr_read(u32 offset, u32 *data)
{
	int ret;

	spin_lock(&ipcdev.gcr_lock);

	ret = is_gcr_valid(offset);
	if (ret < 0) {
		spin_unlock(&ipcdev.gcr_lock);
		return ret;
	}

	*data = readl(ipcdev.gcr_mem_base + offset);

	spin_unlock(&ipcdev.gcr_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(intel_pmc_gcr_read);

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
 * intel_pmc_gcr_write() - Write PMC GCR register
 * @offset:	offset of GCR register from GCR address base
 * @data:	register update value
 *
 * Writes the PMC GCR register of given offset with given
 * value.
 *
 * Return:	negative value on error or 0 on success.
 */
int intel_pmc_gcr_write(u32 offset, u32 data)
{
	int ret;

	spin_lock(&ipcdev.gcr_lock);

	ret = is_gcr_valid(offset);
	if (ret < 0) {
		spin_unlock(&ipcdev.gcr_lock);
		return ret;
	}

	writel(data, ipcdev.gcr_mem_base + offset);

	spin_unlock(&ipcdev.gcr_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(intel_pmc_gcr_write);

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
int intel_pmc_gcr_update(u32 offset, u32 mask, u32 val)
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
EXPORT_SYMBOL_GPL(intel_pmc_gcr_update);

static int update_no_reboot_bit(void *priv, bool set)
{
	u32 value = set ? PMC_CFG_NO_REBOOT_EN : PMC_CFG_NO_REBOOT_DIS;

	return intel_pmc_gcr_update(PMC_GCR_PMC_CFG_REG,
				    PMC_CFG_NO_REBOOT_MASK, value);
}

static int intel_pmc_ipc_check_status(void)
{
	int status;
	int ret = 0;

	if (ipcdev.irq_mode) {
		if (0 == wait_for_completion_timeout(
				&ipcdev.cmd_complete, IPC_MAX_SEC * HZ))
			ret = -ETIMEDOUT;
	} else {
		int loop_count = IPC_LOOP_CNT;

		while ((ipc_read_status() & IPC_STATUS_BUSY) && --loop_count)
			udelay(1);
		if (loop_count == 0)
			ret = -ETIMEDOUT;
	}

	status = ipc_read_status();
	if (ret == -ETIMEDOUT) {
		dev_err(ipcdev.dev,
			"IPC timed out, TS=0x%x, CMD=0x%x\n",
			status, ipcdev.cmd);
		return ret;
	}

	if (status & IPC_STATUS_ERR) {
		int i;

		ret = -EIO;
		i = (status >> IPC_CMD_SIZE) & 0xFF;
		if (i < ARRAY_SIZE(ipc_err_sources))
			dev_err(ipcdev.dev,
				"IPC failed: %s, STS=0x%x, CMD=0x%x\n",
				ipc_err_sources[i], status, ipcdev.cmd);
		else
			dev_err(ipcdev.dev,
				"IPC failed: unknown, STS=0x%x, CMD=0x%x\n",
				status, ipcdev.cmd);
		if ((i == IPC_ERR_UNSIGNEDKERNEL) || (i == IPC_ERR_EMSECURITY))
			ret = -EACCES;
	}

	return ret;
}

/**
 * intel_pmc_ipc_simple_command() - Simple IPC command
 * @cmd:	IPC command code.
 * @sub:	IPC command sub type.
 *
 * Send a simple IPC command to PMC when don't need to specify
 * input/output data and source/dest pointers.
 *
 * Return:	an IPC error code or 0 on success.
 */
int intel_pmc_ipc_simple_command(int cmd, int sub)
{
	int ret;

	mutex_lock(&ipclock);
	if (ipcdev.dev == NULL) {
		mutex_unlock(&ipclock);
		return -ENODEV;
	}
	ipc_send_command(sub << IPC_CMD_SUBCMD | cmd);
	ret = intel_pmc_ipc_check_status();
	mutex_unlock(&ipclock);

	return ret;
}
EXPORT_SYMBOL_GPL(intel_pmc_ipc_simple_command);

/**
 * intel_pmc_ipc_raw_cmd() - IPC command with data and pointers
 * @cmd:	IPC command code.
 * @sub:	IPC command sub type.
 * @in:		input data of this IPC command.
 * @inlen:	input data length in bytes.
 * @out:	output data of this IPC command.
 * @outlen:	output data length in dwords.
 * @sptr:	data writing to SPTR register.
 * @dptr:	data writing to DPTR register.
 *
 * Send an IPC command to PMC with input/output data and source/dest pointers.
 *
 * Return:	an IPC error code or 0 on success.
 */
int intel_pmc_ipc_raw_cmd(u32 cmd, u32 sub, u8 *in, u32 inlen, u32 *out,
			  u32 outlen, u32 dptr, u32 sptr)
{
	u32 wbuf[4] = { 0 };
	int ret;
	int i;

	if (inlen > IPC_DATA_BUFFER_SIZE || outlen > IPC_DATA_BUFFER_SIZE / 4)
		return -EINVAL;

	mutex_lock(&ipclock);
	if (ipcdev.dev == NULL) {
		mutex_unlock(&ipclock);
		return -ENODEV;
	}
	memcpy(wbuf, in, inlen);
	writel(dptr, ipcdev.ipc_base + IPC_DPTR);
	writel(sptr, ipcdev.ipc_base + IPC_SPTR);
	/* The input data register is 32bit register and inlen is in Byte */
	for (i = 0; i < ((inlen + 3) / 4); i++)
		ipc_data_writel(wbuf[i], 4 * i);
	ipc_send_command((inlen << IPC_CMD_SIZE) |
			(sub << IPC_CMD_SUBCMD) | cmd);
	ret = intel_pmc_ipc_check_status();
	if (!ret) {
		/* out is read from 32bit register and outlen is in 32bit */
		for (i = 0; i < outlen; i++)
			*out++ = ipc_data_readl(4 * i);
	}
	mutex_unlock(&ipclock);

	return ret;
}
EXPORT_SYMBOL_GPL(intel_pmc_ipc_raw_cmd);

/**
 * intel_pmc_ipc_command() -  IPC command with input/output data
 * @cmd:	IPC command code.
 * @sub:	IPC command sub type.
 * @in:		input data of this IPC command.
 * @inlen:	input data length in bytes.
 * @out:	output data of this IPC command.
 * @outlen:	output data length in dwords.
 *
 * Send an IPC command to PMC with input/output data.
 *
 * Return:	an IPC error code or 0 on success.
 */
int intel_pmc_ipc_command(u32 cmd, u32 sub, u8 *in, u32 inlen,
			  u32 *out, u32 outlen)
{
	return intel_pmc_ipc_raw_cmd(cmd, sub, in, inlen, out, outlen, 0, 0);
}
EXPORT_SYMBOL_GPL(intel_pmc_ipc_command);

static irqreturn_t ioc(int irq, void *dev_id)
{
	int status;

	if (ipcdev.irq_mode) {
		status = ipc_read_status();
		writel(status | IPC_STATUS_IRQ, ipcdev.ipc_base + IPC_STATUS);
	}
	complete(&ipcdev.cmd_complete);

	return IRQ_HANDLED;
}

static int ipc_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct intel_pmc_ipc_dev *pmc = &ipcdev;
	int ret;

	/* Only one PMC is supported */
	if (pmc->dev)
		return -EBUSY;

	pmc->irq_mode = IPC_TRIGGER_MODE_IRQ;

	spin_lock_init(&ipcdev.gcr_lock);

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = pcim_iomap_regions(pdev, 1 << 0, pci_name(pdev));
	if (ret)
		return ret;

	init_completion(&pmc->cmd_complete);

	pmc->ipc_base = pcim_iomap_table(pdev)[0];

	ret = devm_request_irq(&pdev->dev, pdev->irq, ioc, 0, "intel_pmc_ipc",
				pmc);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request irq\n");
		return ret;
	}

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
	int subcmd;
	int cmd;
	int ret;

	ret = sscanf(buf, "%d %d", &cmd, &subcmd);
	if (ret != 2) {
		dev_err(dev, "Error args\n");
		return -EINVAL;
	}

	ret = intel_pmc_ipc_simple_command(cmd, subcmd);
	if (ret) {
		dev_err(dev, "command %d error with %d\n", cmd, ret);
		return ret;
	}
	return (ssize_t)count;
}

static ssize_t intel_pmc_ipc_northpeak_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t count)
{
	unsigned long val;
	int subcmd;
	int ret;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	if (val)
		subcmd = 1;
	else
		subcmd = 0;
	ret = intel_pmc_ipc_simple_command(PMC_IPC_NORTHPEAK_CTRL, subcmd);
	if (ret) {
		dev_err(dev, "command north %d error with %d\n", subcmd, ret);
		return ret;
	}
	return (ssize_t)count;
}

static DEVICE_ATTR(simplecmd, S_IWUSR,
		   NULL, intel_pmc_ipc_simple_cmd_store);
static DEVICE_ATTR(northpeak, S_IWUSR,
		   NULL, intel_pmc_ipc_northpeak_store);

static struct attribute *intel_ipc_attrs[] = {
	&dev_attr_northpeak.attr,
	&dev_attr_simplecmd.attr,
	NULL
};

static const struct attribute_group intel_ipc_group = {
	.attrs = intel_ipc_attrs,
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

static int ipc_plat_get_res(struct platform_device *pdev)
{
	struct resource *res, *punit_res = punit_res_array;
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

	res = platform_get_resource(pdev, IORESOURCE_MEM,
				    PLAT_RESOURCE_IPC_INDEX);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get ipc resource\n");
		return -ENXIO;
	}
	size = PLAT_RESOURCE_IPC_SIZE + PLAT_RESOURCE_GCR_SIZE;
	res->end = res->start + size - 1;

	addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(addr))
		return PTR_ERR(addr);

	ipcdev.ipc_base = addr;

	ipcdev.gcr_mem_base = addr + PLAT_RESOURCE_GCR_OFFSET;
	dev_info(&pdev->dev, "ipc res: %pR\n", res);

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
	int ret;

	ipcdev.dev = &pdev->dev;
	ipcdev.irq_mode = IPC_TRIGGER_MODE_IRQ;
	init_completion(&ipcdev.cmd_complete);
	spin_lock_init(&ipcdev.gcr_lock);

	ipcdev.irq = platform_get_irq(pdev, 0);
	if (ipcdev.irq < 0) {
		dev_err(&pdev->dev, "Failed to get irq\n");
		return -EINVAL;
	}

	ret = ipc_plat_get_res(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request resource\n");
		return ret;
	}

	ret = ipc_create_pmc_devices();
	if (ret) {
		dev_err(&pdev->dev, "Failed to create pmc devices\n");
		return ret;
	}

	if (devm_request_irq(&pdev->dev, ipcdev.irq, ioc, IRQF_NO_SUSPEND,
			     "intel_pmc_ipc", &ipcdev)) {
		dev_err(&pdev->dev, "Failed to request irq\n");
		ret = -EBUSY;
		goto err_irq;
	}

	ret = sysfs_create_group(&pdev->dev.kobj, &intel_ipc_group);
	if (ret) {
		dev_err(&pdev->dev, "Failed to create sysfs group %d\n",
			ret);
		goto err_sys;
	}

	ipcdev.has_gcr_regs = true;

	return 0;
err_sys:
	devm_free_irq(&pdev->dev, ipcdev.irq, &ipcdev);
err_irq:
	platform_device_unregister(ipcdev.tco_dev);
	platform_device_unregister(ipcdev.punit_dev);
	platform_device_unregister(ipcdev.telemetry_dev);

	return ret;
}

static int ipc_plat_remove(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &intel_ipc_group);
	devm_free_irq(&pdev->dev, ipcdev.irq, &ipcdev);
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
