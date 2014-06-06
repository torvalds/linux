/*
 * Synopsys DesignWare I2C adapter driver (master only).
 *
 * Based on the TI DAVINCI I2C adapter driver.
 *
 * Copyright (C) 2006 Texas Instruments.
 * Copyright (C) 2007 MontaVista Software Inc.
 * Copyright (C) 2009 Provigent Ltd.
 * Copyright (C) 2011 Intel corporation.
 *
 * ----------------------------------------------------------------------------
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * ----------------------------------------------------------------------------
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include "i2c-designware-core.h"

#define DRIVER_NAME "i2c-designware-pci"

enum dw_pci_ctl_id_t {
	moorestown_0,
	moorestown_1,
	moorestown_2,

	medfield_0,
	medfield_1,
	medfield_2,
	medfield_3,
	medfield_4,
	medfield_5,

	baytrail,
	haswell,
};

struct dw_scl_sda_cfg {
	u32 ss_hcnt;
	u32 fs_hcnt;
	u32 ss_lcnt;
	u32 fs_lcnt;
	u32 sda_hold;
};

struct dw_pci_controller {
	u32 bus_num;
	u32 bus_cfg;
	u32 tx_fifo_depth;
	u32 rx_fifo_depth;
	u32 clk_khz;
	u32 functionality;
	struct dw_scl_sda_cfg *scl_sda_cfg;
};

#define INTEL_MID_STD_CFG  (DW_IC_CON_MASTER |			\
				DW_IC_CON_SLAVE_DISABLE |	\
				DW_IC_CON_RESTART_EN)

#define DW_DEFAULT_FUNCTIONALITY (I2C_FUNC_I2C |			\
					I2C_FUNC_SMBUS_BYTE |		\
					I2C_FUNC_SMBUS_BYTE_DATA |	\
					I2C_FUNC_SMBUS_WORD_DATA |	\
					I2C_FUNC_SMBUS_I2C_BLOCK)

/* BayTrail HCNT/LCNT/SDA hold time */
static struct dw_scl_sda_cfg byt_config = {
	.ss_hcnt = 0x200,
	.fs_hcnt = 0x55,
	.ss_lcnt = 0x200,
	.fs_lcnt = 0x99,
	.sda_hold = 0x6,
};

/* Haswell HCNT/LCNT/SDA hold time */
static struct dw_scl_sda_cfg hsw_config = {
	.ss_hcnt = 0x01b0,
	.fs_hcnt = 0x48,
	.ss_lcnt = 0x01fb,
	.fs_lcnt = 0xa0,
	.sda_hold = 0x9,
};

static struct  dw_pci_controller  dw_pci_controllers[] = {
	[moorestown_0] = {
		.bus_num     = 0,
		.bus_cfg   = INTEL_MID_STD_CFG | DW_IC_CON_SPEED_FAST,
		.tx_fifo_depth = 32,
		.rx_fifo_depth = 32,
		.clk_khz      = 25000,
	},
	[moorestown_1] = {
		.bus_num     = 1,
		.bus_cfg   = INTEL_MID_STD_CFG | DW_IC_CON_SPEED_FAST,
		.tx_fifo_depth = 32,
		.rx_fifo_depth = 32,
		.clk_khz      = 25000,
	},
	[moorestown_2] = {
		.bus_num     = 2,
		.bus_cfg   = INTEL_MID_STD_CFG | DW_IC_CON_SPEED_FAST,
		.tx_fifo_depth = 32,
		.rx_fifo_depth = 32,
		.clk_khz      = 25000,
	},
	[medfield_0] = {
		.bus_num     = 0,
		.bus_cfg   = INTEL_MID_STD_CFG | DW_IC_CON_SPEED_FAST,
		.tx_fifo_depth = 32,
		.rx_fifo_depth = 32,
		.clk_khz      = 25000,
	},
	[medfield_1] = {
		.bus_num     = 1,
		.bus_cfg   = INTEL_MID_STD_CFG | DW_IC_CON_SPEED_FAST,
		.tx_fifo_depth = 32,
		.rx_fifo_depth = 32,
		.clk_khz      = 25000,
	},
	[medfield_2] = {
		.bus_num     = 2,
		.bus_cfg   = INTEL_MID_STD_CFG | DW_IC_CON_SPEED_FAST,
		.tx_fifo_depth = 32,
		.rx_fifo_depth = 32,
		.clk_khz      = 25000,
	},
	[medfield_3] = {
		.bus_num     = 3,
		.bus_cfg   = INTEL_MID_STD_CFG | DW_IC_CON_SPEED_STD,
		.tx_fifo_depth = 32,
		.rx_fifo_depth = 32,
		.clk_khz      = 25000,
	},
	[medfield_4] = {
		.bus_num     = 4,
		.bus_cfg   = INTEL_MID_STD_CFG | DW_IC_CON_SPEED_FAST,
		.tx_fifo_depth = 32,
		.rx_fifo_depth = 32,
		.clk_khz      = 25000,
	},
	[medfield_5] = {
		.bus_num     = 5,
		.bus_cfg   = INTEL_MID_STD_CFG | DW_IC_CON_SPEED_FAST,
		.tx_fifo_depth = 32,
		.rx_fifo_depth = 32,
		.clk_khz      = 25000,
	},
	[baytrail] = {
		.bus_num = -1,
		.bus_cfg = INTEL_MID_STD_CFG | DW_IC_CON_SPEED_FAST,
		.tx_fifo_depth = 32,
		.rx_fifo_depth = 32,
		.clk_khz = 100000,
		.functionality = I2C_FUNC_10BIT_ADDR,
		.scl_sda_cfg = &byt_config,
	},
	[haswell] = {
		.bus_num = -1,
		.bus_cfg = INTEL_MID_STD_CFG | DW_IC_CON_SPEED_FAST,
		.tx_fifo_depth = 32,
		.rx_fifo_depth = 32,
		.clk_khz = 100000,
		.functionality = I2C_FUNC_10BIT_ADDR,
		.scl_sda_cfg = &hsw_config,
	},
};
static struct i2c_algorithm i2c_dw_algo = {
	.master_xfer	= i2c_dw_xfer,
	.functionality	= i2c_dw_func,
};

#ifdef CONFIG_PM
static int i2c_dw_pci_suspend(struct device *dev)
{
	struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);

	i2c_dw_disable(pci_get_drvdata(pdev));
	return 0;
}

static int i2c_dw_pci_resume(struct device *dev)
{
	struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);

	return i2c_dw_init(pci_get_drvdata(pdev));
}
#endif

static UNIVERSAL_DEV_PM_OPS(i2c_dw_pm_ops, i2c_dw_pci_suspend,
			    i2c_dw_pci_resume, NULL);

static u32 i2c_dw_get_clk_rate_khz(struct dw_i2c_dev *dev)
{
	return dev->controller->clk_khz;
}

static int i2c_dw_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	struct dw_i2c_dev *dev;
	struct i2c_adapter *adap;
	int r;
	struct  dw_pci_controller *controller;
	struct dw_scl_sda_cfg *cfg;

	if (id->driver_data >= ARRAY_SIZE(dw_pci_controllers)) {
		dev_err(&pdev->dev, "%s: invalid driver data %ld\n", __func__,
			id->driver_data);
		return -EINVAL;
	}

	controller = &dw_pci_controllers[id->driver_data];

	r = pcim_enable_device(pdev);
	if (r) {
		dev_err(&pdev->dev, "Failed to enable I2C PCI device (%d)\n",
			r);
		return r;
	}

	r = pcim_iomap_regions(pdev, 1 << 0, pci_name(pdev));
	if (r) {
		dev_err(&pdev->dev, "I/O memory remapping failed\n");
		return r;
	}

	dev = devm_kzalloc(&pdev->dev, sizeof(struct dw_i2c_dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	init_completion(&dev->cmd_complete);
	mutex_init(&dev->lock);
	dev->clk = NULL;
	dev->controller = controller;
	dev->get_clk_rate_khz = i2c_dw_get_clk_rate_khz;
	dev->base = pcim_iomap_table(pdev)[0];
	dev->dev = &pdev->dev;
	dev->functionality = controller->functionality |
				DW_DEFAULT_FUNCTIONALITY;

	dev->master_cfg =  controller->bus_cfg;
	if (controller->scl_sda_cfg) {
		cfg = controller->scl_sda_cfg;
		dev->ss_hcnt = cfg->ss_hcnt;
		dev->fs_hcnt = cfg->fs_hcnt;
		dev->ss_lcnt = cfg->ss_lcnt;
		dev->fs_lcnt = cfg->fs_lcnt;
		dev->sda_hold_time = cfg->sda_hold;
	}

	pci_set_drvdata(pdev, dev);

	dev->tx_fifo_depth = controller->tx_fifo_depth;
	dev->rx_fifo_depth = controller->rx_fifo_depth;
	r = i2c_dw_init(dev);
	if (r)
		return r;

	adap = &dev->adapter;
	i2c_set_adapdata(adap, dev);
	adap->owner = THIS_MODULE;
	adap->class = 0;
	adap->algo = &i2c_dw_algo;
	adap->dev.parent = &pdev->dev;
	adap->nr = controller->bus_num;

	snprintf(adap->name, sizeof(adap->name), "i2c-designware-pci");

	r = devm_request_irq(&pdev->dev, pdev->irq, i2c_dw_isr, IRQF_SHARED,
			adap->name, dev);
	if (r) {
		dev_err(&pdev->dev, "failure requesting irq %i\n", dev->irq);
		return r;
	}

	i2c_dw_disable_int(dev);
	i2c_dw_clear_int(dev);
	r = i2c_add_numbered_adapter(adap);
	if (r) {
		dev_err(&pdev->dev, "failure adding adapter\n");
		return r;
	}

	pm_runtime_set_autosuspend_delay(&pdev->dev, 1000);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_put_autosuspend(&pdev->dev);
	pm_runtime_allow(&pdev->dev);

	return 0;
}

static void i2c_dw_pci_remove(struct pci_dev *pdev)
{
	struct dw_i2c_dev *dev = pci_get_drvdata(pdev);

	i2c_dw_disable(dev);
	pm_runtime_forbid(&pdev->dev);
	pm_runtime_get_noresume(&pdev->dev);

	i2c_del_adapter(&dev->adapter);
}

/* work with hotplug and coldplug */
MODULE_ALIAS("i2c_designware-pci");

static const struct pci_device_id i2_designware_pci_ids[] = {
	/* Moorestown */
	{ PCI_VDEVICE(INTEL, 0x0802), moorestown_0 },
	{ PCI_VDEVICE(INTEL, 0x0803), moorestown_1 },
	{ PCI_VDEVICE(INTEL, 0x0804), moorestown_2 },
	/* Medfield */
	{ PCI_VDEVICE(INTEL, 0x0817), medfield_3,},
	{ PCI_VDEVICE(INTEL, 0x0818), medfield_4 },
	{ PCI_VDEVICE(INTEL, 0x0819), medfield_5 },
	{ PCI_VDEVICE(INTEL, 0x082C), medfield_0 },
	{ PCI_VDEVICE(INTEL, 0x082D), medfield_1 },
	{ PCI_VDEVICE(INTEL, 0x082E), medfield_2 },
	/* Baytrail */
	{ PCI_VDEVICE(INTEL, 0x0F41), baytrail },
	{ PCI_VDEVICE(INTEL, 0x0F42), baytrail },
	{ PCI_VDEVICE(INTEL, 0x0F43), baytrail },
	{ PCI_VDEVICE(INTEL, 0x0F44), baytrail },
	{ PCI_VDEVICE(INTEL, 0x0F45), baytrail },
	{ PCI_VDEVICE(INTEL, 0x0F46), baytrail },
	{ PCI_VDEVICE(INTEL, 0x0F47), baytrail },
	/* Haswell */
	{ PCI_VDEVICE(INTEL, 0x9c61), haswell },
	{ PCI_VDEVICE(INTEL, 0x9c62), haswell },
	{ 0,}
};
MODULE_DEVICE_TABLE(pci, i2_designware_pci_ids);

static struct pci_driver dw_i2c_driver = {
	.name		= DRIVER_NAME,
	.id_table	= i2_designware_pci_ids,
	.probe		= i2c_dw_pci_probe,
	.remove		= i2c_dw_pci_remove,
	.driver         = {
		.pm     = &i2c_dw_pm_ops,
	},
};

module_pci_driver(dw_i2c_driver);

MODULE_AUTHOR("Baruch Siach <baruch@tkos.co.il>");
MODULE_DESCRIPTION("Synopsys DesignWare PCI I2C bus adapter");
MODULE_LICENSE("GPL");
