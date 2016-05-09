/*
 * Driver for the Intel P-Unit Mailbox IPC mechanism
 *
 * (C) Copyright 2015 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * The heart of the P-Unit is the Foxton microcontroller and its firmware,
 * which provide mailbox interface for power management usage.
 */

#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <asm/intel_punit_ipc.h>

/* IPC Mailbox registers */
#define OFFSET_DATA_LOW		0x0
#define OFFSET_DATA_HIGH	0x4
/* bit field of interface register */
#define	CMD_RUN			BIT(31)
#define	CMD_ERRCODE_MASK	GENMASK(7, 0)
#define	CMD_PARA1_SHIFT		8
#define	CMD_PARA2_SHIFT		16

#define CMD_TIMEOUT_SECONDS	1

enum {
	BASE_DATA = 0,
	BASE_IFACE,
	BASE_MAX,
};

typedef struct {
	struct device *dev;
	struct mutex lock;
	int irq;
	struct completion cmd_complete;
	/* base of interface and data registers */
	void __iomem *base[RESERVED_IPC][BASE_MAX];
	IPC_TYPE type;
} IPC_DEV;

static IPC_DEV *punit_ipcdev;

static inline u32 ipc_read_status(IPC_DEV *ipcdev, IPC_TYPE type)
{
	return readl(ipcdev->base[type][BASE_IFACE]);
}

static inline void ipc_write_cmd(IPC_DEV *ipcdev, IPC_TYPE type, u32 cmd)
{
	writel(cmd, ipcdev->base[type][BASE_IFACE]);
}

static inline u32 ipc_read_data_low(IPC_DEV *ipcdev, IPC_TYPE type)
{
	return readl(ipcdev->base[type][BASE_DATA] + OFFSET_DATA_LOW);
}

static inline u32 ipc_read_data_high(IPC_DEV *ipcdev, IPC_TYPE type)
{
	return readl(ipcdev->base[type][BASE_DATA] + OFFSET_DATA_HIGH);
}

static inline void ipc_write_data_low(IPC_DEV *ipcdev, IPC_TYPE type, u32 data)
{
	writel(data, ipcdev->base[type][BASE_DATA] + OFFSET_DATA_LOW);
}

static inline void ipc_write_data_high(IPC_DEV *ipcdev, IPC_TYPE type, u32 data)
{
	writel(data, ipcdev->base[type][BASE_DATA] + OFFSET_DATA_HIGH);
}

static const char *ipc_err_string(int error)
{
	if (error == IPC_PUNIT_ERR_SUCCESS)
		return "no error";
	else if (error == IPC_PUNIT_ERR_INVALID_CMD)
		return "invalid command";
	else if (error == IPC_PUNIT_ERR_INVALID_PARAMETER)
		return "invalid parameter";
	else if (error == IPC_PUNIT_ERR_CMD_TIMEOUT)
		return "command timeout";
	else if (error == IPC_PUNIT_ERR_CMD_LOCKED)
		return "command locked";
	else if (error == IPC_PUNIT_ERR_INVALID_VR_ID)
		return "invalid vr id";
	else if (error == IPC_PUNIT_ERR_VR_ERR)
		return "vr error";
	else
		return "unknown error";
}

static int intel_punit_ipc_check_status(IPC_DEV *ipcdev, IPC_TYPE type)
{
	int loops = CMD_TIMEOUT_SECONDS * USEC_PER_SEC;
	int errcode;
	int status;

	if (ipcdev->irq) {
		if (!wait_for_completion_timeout(&ipcdev->cmd_complete,
						 CMD_TIMEOUT_SECONDS * HZ)) {
			dev_err(ipcdev->dev, "IPC timed out\n");
			return -ETIMEDOUT;
		}
	} else {
		while ((ipc_read_status(ipcdev, type) & CMD_RUN) && --loops)
			udelay(1);
		if (!loops) {
			dev_err(ipcdev->dev, "IPC timed out\n");
			return -ETIMEDOUT;
		}
	}

	status = ipc_read_status(ipcdev, type);
	errcode = status & CMD_ERRCODE_MASK;
	if (errcode) {
		dev_err(ipcdev->dev, "IPC failed: %s, IPC_STS=0x%x\n",
			ipc_err_string(errcode), status);
		return -EIO;
	}

	return 0;
}

/**
 * intel_punit_ipc_simple_command() - Simple IPC command
 * @cmd:	IPC command code.
 * @para1:	First 8bit parameter, set 0 if not used.
 * @para2:	Second 8bit parameter, set 0 if not used.
 *
 * Send a IPC command to P-Unit when there is no data transaction
 *
 * Return:	IPC error code or 0 on success.
 */
int intel_punit_ipc_simple_command(int cmd, int para1, int para2)
{
	IPC_DEV *ipcdev = punit_ipcdev;
	IPC_TYPE type;
	u32 val;
	int ret;

	mutex_lock(&ipcdev->lock);

	reinit_completion(&ipcdev->cmd_complete);
	type = (cmd & IPC_PUNIT_CMD_TYPE_MASK) >> IPC_TYPE_OFFSET;

	val = cmd & ~IPC_PUNIT_CMD_TYPE_MASK;
	val |= CMD_RUN | para2 << CMD_PARA2_SHIFT | para1 << CMD_PARA1_SHIFT;
	ipc_write_cmd(ipcdev, type, val);
	ret = intel_punit_ipc_check_status(ipcdev, type);

	mutex_unlock(&ipcdev->lock);

	return ret;
}
EXPORT_SYMBOL(intel_punit_ipc_simple_command);

/**
 * intel_punit_ipc_command() - IPC command with data and pointers
 * @cmd:	IPC command code.
 * @para1:	First 8bit parameter, set 0 if not used.
 * @para2:	Second 8bit parameter, set 0 if not used.
 * @in:		Input data, 32bit for BIOS cmd, two 32bit for GTD and ISPD.
 * @out:	Output data.
 *
 * Send a IPC command to P-Unit with data transaction
 *
 * Return:	IPC error code or 0 on success.
 */
int intel_punit_ipc_command(u32 cmd, u32 para1, u32 para2, u32 *in, u32 *out)
{
	IPC_DEV *ipcdev = punit_ipcdev;
	IPC_TYPE type;
	u32 val;
	int ret;

	mutex_lock(&ipcdev->lock);

	reinit_completion(&ipcdev->cmd_complete);
	type = (cmd & IPC_PUNIT_CMD_TYPE_MASK) >> IPC_TYPE_OFFSET;

	if (in) {
		ipc_write_data_low(ipcdev, type, *in);
		if (type == GTDRIVER_IPC || type == ISPDRIVER_IPC)
			ipc_write_data_high(ipcdev, type, *++in);
	}

	val = cmd & ~IPC_PUNIT_CMD_TYPE_MASK;
	val |= CMD_RUN | para2 << CMD_PARA2_SHIFT | para1 << CMD_PARA1_SHIFT;
	ipc_write_cmd(ipcdev, type, val);

	ret = intel_punit_ipc_check_status(ipcdev, type);
	if (ret)
		goto out;

	if (out) {
		*out = ipc_read_data_low(ipcdev, type);
		if (type == GTDRIVER_IPC || type == ISPDRIVER_IPC)
			*++out = ipc_read_data_high(ipcdev, type);
	}

out:
	mutex_unlock(&ipcdev->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(intel_punit_ipc_command);

static irqreturn_t intel_punit_ioc(int irq, void *dev_id)
{
	IPC_DEV *ipcdev = dev_id;

	complete(&ipcdev->cmd_complete);
	return IRQ_HANDLED;
}

static int intel_punit_get_bars(struct platform_device *pdev)
{
	struct resource *res;
	void __iomem *addr;

	/*
	 * The following resources are required
	 * - BIOS_IPC BASE_DATA
	 * - BIOS_IPC BASE_IFACE
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(addr))
		return PTR_ERR(addr);
	punit_ipcdev->base[BIOS_IPC][BASE_DATA] = addr;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(addr))
		return PTR_ERR(addr);
	punit_ipcdev->base[BIOS_IPC][BASE_IFACE] = addr;

	/*
	 * The following resources are optional
	 * - ISPDRIVER_IPC BASE_DATA
	 * - ISPDRIVER_IPC BASE_IFACE
	 * - GTDRIVER_IPC BASE_DATA
	 * - GTDRIVER_IPC BASE_IFACE
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	if (res) {
		addr = devm_ioremap_resource(&pdev->dev, res);
		if (!IS_ERR(addr))
			punit_ipcdev->base[ISPDRIVER_IPC][BASE_DATA] = addr;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 3);
	if (res) {
		addr = devm_ioremap_resource(&pdev->dev, res);
		if (!IS_ERR(addr))
			punit_ipcdev->base[ISPDRIVER_IPC][BASE_IFACE] = addr;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 4);
	if (res) {
		addr = devm_ioremap_resource(&pdev->dev, res);
		if (!IS_ERR(addr))
			punit_ipcdev->base[GTDRIVER_IPC][BASE_DATA] = addr;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 5);
	if (res) {
		addr = devm_ioremap_resource(&pdev->dev, res);
		if (!IS_ERR(addr))
			punit_ipcdev->base[GTDRIVER_IPC][BASE_IFACE] = addr;
	}

	return 0;
}

static int intel_punit_ipc_probe(struct platform_device *pdev)
{
	int irq, ret;

	punit_ipcdev = devm_kzalloc(&pdev->dev,
				    sizeof(*punit_ipcdev), GFP_KERNEL);
	if (!punit_ipcdev)
		return -ENOMEM;

	platform_set_drvdata(pdev, punit_ipcdev);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		punit_ipcdev->irq = 0;
		dev_warn(&pdev->dev, "Invalid IRQ, using polling mode\n");
	} else {
		ret = devm_request_irq(&pdev->dev, irq, intel_punit_ioc,
				       IRQF_NO_SUSPEND, "intel_punit_ipc",
				       &punit_ipcdev);
		if (ret) {
			dev_err(&pdev->dev, "Failed to request irq: %d\n", irq);
			return ret;
		}
		punit_ipcdev->irq = irq;
	}

	ret = intel_punit_get_bars(pdev);
	if (ret)
		goto out;

	punit_ipcdev->dev = &pdev->dev;
	mutex_init(&punit_ipcdev->lock);
	init_completion(&punit_ipcdev->cmd_complete);

out:
	return ret;
}

static int intel_punit_ipc_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct acpi_device_id punit_ipc_acpi_ids[] = {
	{ "INT34D4", 0 },
	{ }
};

static struct platform_driver intel_punit_ipc_driver = {
	.probe = intel_punit_ipc_probe,
	.remove = intel_punit_ipc_remove,
	.driver = {
		.name = "intel_punit_ipc",
		.acpi_match_table = ACPI_PTR(punit_ipc_acpi_ids),
	},
};

static int __init intel_punit_ipc_init(void)
{
	return platform_driver_register(&intel_punit_ipc_driver);
}

static void __exit intel_punit_ipc_exit(void)
{
	platform_driver_unregister(&intel_punit_ipc_driver);
}

MODULE_AUTHOR("Zha Qipeng <qipeng.zha@intel.com>");
MODULE_DESCRIPTION("Intel P-Unit IPC driver");
MODULE_LICENSE("GPL v2");

/* Some modules are dependent on this, so init earlier */
fs_initcall(intel_punit_ipc_init);
module_exit(intel_punit_ipc_exit);
