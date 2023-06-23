// SPDX-License-Identifier: GPL-2.0
/*
 * Nvidia GPU I2C controller Driver
 *
 * Copyright (C) 2018 NVIDIA Corporation. All rights reserved.
 * Author: Ajay Gupta <ajayg@nvidia.com>
 */
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>

#include <asm/unaligned.h>

#include "i2c-ccgx-ucsi.h"

/* I2C definitions */
#define I2C_MST_CNTL				0x00
#define I2C_MST_CNTL_GEN_START			BIT(0)
#define I2C_MST_CNTL_GEN_STOP			BIT(1)
#define I2C_MST_CNTL_CMD_READ			(1 << 2)
#define I2C_MST_CNTL_CMD_WRITE			(2 << 2)
#define I2C_MST_CNTL_BURST_SIZE_SHIFT		6
#define I2C_MST_CNTL_GEN_NACK			BIT(28)
#define I2C_MST_CNTL_STATUS			GENMASK(30, 29)
#define I2C_MST_CNTL_STATUS_OKAY		(0 << 29)
#define I2C_MST_CNTL_STATUS_NO_ACK		(1 << 29)
#define I2C_MST_CNTL_STATUS_TIMEOUT		(2 << 29)
#define I2C_MST_CNTL_STATUS_BUS_BUSY		(3 << 29)
#define I2C_MST_CNTL_CYCLE_TRIGGER		BIT(31)

#define I2C_MST_ADDR				0x04

#define I2C_MST_I2C0_TIMING				0x08
#define I2C_MST_I2C0_TIMING_SCL_PERIOD_100KHZ		0x10e
#define I2C_MST_I2C0_TIMING_TIMEOUT_CLK_CNT		16
#define I2C_MST_I2C0_TIMING_TIMEOUT_CLK_CNT_MAX		255
#define I2C_MST_I2C0_TIMING_TIMEOUT_CHECK		BIT(24)

#define I2C_MST_DATA					0x0c

#define I2C_MST_HYBRID_PADCTL				0x20
#define I2C_MST_HYBRID_PADCTL_MODE_I2C			BIT(0)
#define I2C_MST_HYBRID_PADCTL_I2C_SCL_INPUT_RCV		BIT(14)
#define I2C_MST_HYBRID_PADCTL_I2C_SDA_INPUT_RCV		BIT(15)

struct gpu_i2c_dev {
	struct device *dev;
	void __iomem *regs;
	struct i2c_adapter adapter;
	struct i2c_board_info *gpu_ccgx_ucsi;
	struct i2c_client *ccgx_client;
};

static void gpu_enable_i2c_bus(struct gpu_i2c_dev *i2cd)
{
	u32 val;

	/* enable I2C */
	val = readl(i2cd->regs + I2C_MST_HYBRID_PADCTL);
	val |= I2C_MST_HYBRID_PADCTL_MODE_I2C |
		I2C_MST_HYBRID_PADCTL_I2C_SCL_INPUT_RCV |
		I2C_MST_HYBRID_PADCTL_I2C_SDA_INPUT_RCV;
	writel(val, i2cd->regs + I2C_MST_HYBRID_PADCTL);

	/* enable 100KHZ mode */
	val = I2C_MST_I2C0_TIMING_SCL_PERIOD_100KHZ;
	val |= (I2C_MST_I2C0_TIMING_TIMEOUT_CLK_CNT_MAX
	    << I2C_MST_I2C0_TIMING_TIMEOUT_CLK_CNT);
	val |= I2C_MST_I2C0_TIMING_TIMEOUT_CHECK;
	writel(val, i2cd->regs + I2C_MST_I2C0_TIMING);
}

static int gpu_i2c_check_status(struct gpu_i2c_dev *i2cd)
{
	u32 val;
	int ret;

	ret = readl_poll_timeout(i2cd->regs + I2C_MST_CNTL, val,
				 !(val & I2C_MST_CNTL_CYCLE_TRIGGER) ||
				 (val & I2C_MST_CNTL_STATUS) != I2C_MST_CNTL_STATUS_BUS_BUSY,
				 500, 1000 * USEC_PER_MSEC);

	if (ret) {
		dev_err(i2cd->dev, "i2c timeout error %x\n", val);
		return -ETIMEDOUT;
	}

	val = readl(i2cd->regs + I2C_MST_CNTL);
	switch (val & I2C_MST_CNTL_STATUS) {
	case I2C_MST_CNTL_STATUS_OKAY:
		return 0;
	case I2C_MST_CNTL_STATUS_NO_ACK:
		return -ENXIO;
	case I2C_MST_CNTL_STATUS_TIMEOUT:
		return -ETIMEDOUT;
	default:
		return 0;
	}
}

static int gpu_i2c_read(struct gpu_i2c_dev *i2cd, u8 *data, u16 len)
{
	int status;
	u32 val;

	val = I2C_MST_CNTL_GEN_START | I2C_MST_CNTL_CMD_READ |
		(len << I2C_MST_CNTL_BURST_SIZE_SHIFT) |
		I2C_MST_CNTL_CYCLE_TRIGGER | I2C_MST_CNTL_GEN_NACK;
	writel(val, i2cd->regs + I2C_MST_CNTL);

	status = gpu_i2c_check_status(i2cd);
	if (status < 0)
		return status;

	val = readl(i2cd->regs + I2C_MST_DATA);
	switch (len) {
	case 1:
		data[0] = val;
		break;
	case 2:
		put_unaligned_be16(val, data);
		break;
	case 3:
		put_unaligned_be24(val, data);
		break;
	case 4:
		put_unaligned_be32(val, data);
		break;
	default:
		break;
	}
	return status;
}

static int gpu_i2c_start(struct gpu_i2c_dev *i2cd)
{
	writel(I2C_MST_CNTL_GEN_START, i2cd->regs + I2C_MST_CNTL);
	return gpu_i2c_check_status(i2cd);
}

static int gpu_i2c_stop(struct gpu_i2c_dev *i2cd)
{
	writel(I2C_MST_CNTL_GEN_STOP, i2cd->regs + I2C_MST_CNTL);
	return gpu_i2c_check_status(i2cd);
}

static int gpu_i2c_write(struct gpu_i2c_dev *i2cd, u8 data)
{
	u32 val;

	writel(data, i2cd->regs + I2C_MST_DATA);

	val = I2C_MST_CNTL_CMD_WRITE | (1 << I2C_MST_CNTL_BURST_SIZE_SHIFT);
	writel(val, i2cd->regs + I2C_MST_CNTL);

	return gpu_i2c_check_status(i2cd);
}

static int gpu_i2c_master_xfer(struct i2c_adapter *adap,
			       struct i2c_msg *msgs, int num)
{
	struct gpu_i2c_dev *i2cd = i2c_get_adapdata(adap);
	int status, status2;
	bool send_stop = true;
	int i, j;

	/*
	 * The controller supports maximum 4 byte read due to known
	 * limitation of sending STOP after every read.
	 */
	pm_runtime_get_sync(i2cd->dev);
	for (i = 0; i < num; i++) {
		if (msgs[i].flags & I2C_M_RD) {
			/* program client address before starting read */
			writel(msgs[i].addr, i2cd->regs + I2C_MST_ADDR);
			/* gpu_i2c_read has implicit start */
			status = gpu_i2c_read(i2cd, msgs[i].buf, msgs[i].len);
			if (status < 0)
				goto exit;
		} else {
			u8 addr = i2c_8bit_addr_from_msg(msgs + i);

			status = gpu_i2c_start(i2cd);
			if (status < 0) {
				if (i == 0)
					send_stop = false;
				goto exit;
			}

			status = gpu_i2c_write(i2cd, addr);
			if (status < 0)
				goto exit;

			for (j = 0; j < msgs[i].len; j++) {
				status = gpu_i2c_write(i2cd, msgs[i].buf[j]);
				if (status < 0)
					goto exit;
			}
		}
	}
	send_stop = false;
	status = gpu_i2c_stop(i2cd);
	if (status < 0)
		goto exit;

	status = i;
exit:
	if (send_stop) {
		status2 = gpu_i2c_stop(i2cd);
		if (status2 < 0)
			dev_err(i2cd->dev, "i2c stop failed %d\n", status2);
	}
	pm_runtime_mark_last_busy(i2cd->dev);
	pm_runtime_put_autosuspend(i2cd->dev);
	return status;
}

static const struct i2c_adapter_quirks gpu_i2c_quirks = {
	.max_read_len = 4,
	.max_comb_2nd_msg_len = 4,
	.flags = I2C_AQ_COMB_WRITE_THEN_READ,
};

static u32 gpu_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm gpu_i2c_algorithm = {
	.master_xfer	= gpu_i2c_master_xfer,
	.functionality	= gpu_i2c_functionality,
};

/*
 * This driver is for Nvidia GPU cards with USB Type-C interface.
 * We want to identify the cards using vendor ID and class code only
 * to avoid dependency of adding product id for any new card which
 * requires this driver.
 * Currently there is no class code defined for UCSI device over PCI
 * so using UNKNOWN class for now and it will be updated when UCSI
 * over PCI gets a class code.
 * There is no other NVIDIA cards with UNKNOWN class code. Even if the
 * driver gets loaded for an undesired card then eventually i2c_read()
 * (initiated from UCSI i2c_client) will timeout or UCSI commands will
 * timeout.
 */
#define PCI_CLASS_SERIAL_UNKNOWN	0x0c80
static const struct pci_device_id gpu_i2c_ids[] = {
	{ PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID, PCI_ANY_ID, PCI_ANY_ID,
		PCI_CLASS_SERIAL_UNKNOWN << 8, 0xffffff00},
	{ }
};
MODULE_DEVICE_TABLE(pci, gpu_i2c_ids);

static const struct property_entry ccgx_props[] = {
	/* Use FW built for NVIDIA (nv) only */
	PROPERTY_ENTRY_U16("ccgx,firmware-build", ('n' << 8) | 'v'),
	{ }
};

static const struct software_node ccgx_node = {
	.properties = ccgx_props,
};

static int gpu_i2c_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct gpu_i2c_dev *i2cd;
	int status;

	i2cd = devm_kzalloc(dev, sizeof(*i2cd), GFP_KERNEL);
	if (!i2cd)
		return -ENOMEM;

	i2cd->dev = dev;
	dev_set_drvdata(dev, i2cd);

	status = pcim_enable_device(pdev);
	if (status < 0)
		return dev_err_probe(dev, status, "pcim_enable_device failed\n");

	pci_set_master(pdev);

	i2cd->regs = pcim_iomap(pdev, 0, 0);
	if (!i2cd->regs)
		return dev_err_probe(dev, -ENOMEM, "pcim_iomap failed\n");

	status = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
	if (status < 0)
		return dev_err_probe(dev, status, "pci_alloc_irq_vectors err\n");

	gpu_enable_i2c_bus(i2cd);

	i2c_set_adapdata(&i2cd->adapter, i2cd);
	i2cd->adapter.owner = THIS_MODULE;
	strscpy(i2cd->adapter.name, "NVIDIA GPU I2C adapter",
		sizeof(i2cd->adapter.name));
	i2cd->adapter.algo = &gpu_i2c_algorithm;
	i2cd->adapter.quirks = &gpu_i2c_quirks;
	i2cd->adapter.dev.parent = dev;
	status = i2c_add_adapter(&i2cd->adapter);
	if (status < 0)
		goto free_irq_vectors;

	i2cd->ccgx_client = i2c_new_ccgx_ucsi(&i2cd->adapter, pdev->irq, &ccgx_node);
	if (IS_ERR(i2cd->ccgx_client)) {
		status = dev_err_probe(dev, PTR_ERR(i2cd->ccgx_client), "register UCSI failed\n");
		goto del_adapter;
	}

	pm_runtime_set_autosuspend_delay(dev, 3000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_put_autosuspend(dev);
	pm_runtime_allow(dev);

	return 0;

del_adapter:
	i2c_del_adapter(&i2cd->adapter);
free_irq_vectors:
	pci_free_irq_vectors(pdev);
	return status;
}

static void gpu_i2c_remove(struct pci_dev *pdev)
{
	struct gpu_i2c_dev *i2cd = pci_get_drvdata(pdev);

	pm_runtime_get_noresume(i2cd->dev);
	i2c_del_adapter(&i2cd->adapter);
	pci_free_irq_vectors(pdev);
}

#define gpu_i2c_suspend NULL

static __maybe_unused int gpu_i2c_resume(struct device *dev)
{
	struct gpu_i2c_dev *i2cd = dev_get_drvdata(dev);

	gpu_enable_i2c_bus(i2cd);
	/*
	 * Runtime resume ccgx client so that it can see for any
	 * connector change event. Old ccg firmware has known
	 * issue of not triggering interrupt when a device is
	 * connected to runtime resume the controller.
	 */
	pm_request_resume(&i2cd->ccgx_client->dev);
	return 0;
}

static UNIVERSAL_DEV_PM_OPS(gpu_i2c_driver_pm, gpu_i2c_suspend, gpu_i2c_resume,
			    NULL);

static struct pci_driver gpu_i2c_driver = {
	.name		= "nvidia-gpu",
	.id_table	= gpu_i2c_ids,
	.probe		= gpu_i2c_probe,
	.remove		= gpu_i2c_remove,
	.driver		= {
		.pm	= &gpu_i2c_driver_pm,
	},
};

module_pci_driver(gpu_i2c_driver);

MODULE_AUTHOR("Ajay Gupta <ajayg@nvidia.com>");
MODULE_DESCRIPTION("Nvidia GPU I2C controller Driver");
MODULE_LICENSE("GPL v2");
