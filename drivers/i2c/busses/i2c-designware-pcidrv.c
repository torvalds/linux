// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Synopsys DesignWare I2C adapter driver (master only).
 *
 * Based on the TI DAVINCI I2C adapter driver.
 *
 * Copyright (C) 2006 Texas Instruments.
 * Copyright (C) 2007 MontaVista Software Inc.
 * Copyright (C) 2009 Provigent Ltd.
 * Copyright (C) 2011, 2015, 2016 Intel Corporation.
 */
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/power_supply.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include "i2c-designware-core.h"
#include "i2c-ccgx-ucsi.h"

#define DRIVER_NAME "i2c-designware-pci"

enum dw_pci_ctl_id_t {
	medfield,
	merrifield,
	baytrail,
	cherrytrail,
	haswell,
	elkhartlake,
	navi_amd,
};

/*
 * This is a legacy structure to describe the hardware counters
 * to configure signal timings on the bus. For Device Tree platforms
 * one should use the respective properties and for ACPI there is
 * a set of ACPI methods that provide these counters. No new
 * platform should use this structure.
 */
struct dw_scl_sda_cfg {
	u16 ss_hcnt;
	u16 fs_hcnt;
	u16 ss_lcnt;
	u16 fs_lcnt;
	u32 sda_hold_time;
};

struct dw_pci_controller {
	u32 bus_num;
	u32 flags;
	struct dw_scl_sda_cfg *scl_sda_cfg;
	int (*setup)(struct pci_dev *pdev, struct dw_pci_controller *c);
	u32 (*get_clk_rate_khz)(struct dw_i2c_dev *dev);
};

/* Merrifield HCNT/LCNT/SDA hold time */
static struct dw_scl_sda_cfg mrfld_config = {
	.ss_hcnt = 0x2f8,
	.fs_hcnt = 0x87,
	.ss_lcnt = 0x37b,
	.fs_lcnt = 0x10a,
};

/* BayTrail HCNT/LCNT/SDA hold time */
static struct dw_scl_sda_cfg byt_config = {
	.ss_hcnt = 0x200,
	.fs_hcnt = 0x55,
	.ss_lcnt = 0x200,
	.fs_lcnt = 0x99,
	.sda_hold_time = 0x6,
};

/* Haswell HCNT/LCNT/SDA hold time */
static struct dw_scl_sda_cfg hsw_config = {
	.ss_hcnt = 0x01b0,
	.fs_hcnt = 0x48,
	.ss_lcnt = 0x01fb,
	.fs_lcnt = 0xa0,
	.sda_hold_time = 0x9,
};

/* NAVI-AMD HCNT/LCNT/SDA hold time */
static struct dw_scl_sda_cfg navi_amd_config = {
	.ss_hcnt = 0x1ae,
	.ss_lcnt = 0x23a,
	.sda_hold_time = 0x9,
};

static u32 mfld_get_clk_rate_khz(struct dw_i2c_dev *dev)
{
	return 25000;
}

static int mfld_setup(struct pci_dev *pdev, struct dw_pci_controller *c)
{
	struct dw_i2c_dev *dev = pci_get_drvdata(pdev);

	switch (pdev->device) {
	case 0x0817:
		dev->timings.bus_freq_hz = I2C_MAX_STANDARD_MODE_FREQ;
		fallthrough;
	case 0x0818:
	case 0x0819:
		c->bus_num = pdev->device - 0x817 + 3;
		return 0;
	case 0x082C:
	case 0x082D:
	case 0x082E:
		c->bus_num = pdev->device - 0x82C + 0;
		return 0;
	}
	return -ENODEV;
}

static int mrfld_setup(struct pci_dev *pdev, struct dw_pci_controller *c)
{
	/*
	 * On Intel Merrifield the user visible i2c buses are enumerated
	 * [1..7]. So, we add 1 to shift the default range. Besides that the
	 * first PCI slot provides 4 functions, that's why we have to add 0 to
	 * the first slot and 4 to the next one.
	 */
	switch (PCI_SLOT(pdev->devfn)) {
	case 8:
		c->bus_num = PCI_FUNC(pdev->devfn) + 0 + 1;
		return 0;
	case 9:
		c->bus_num = PCI_FUNC(pdev->devfn) + 4 + 1;
		return 0;
	}
	return -ENODEV;
}

static u32 ehl_get_clk_rate_khz(struct dw_i2c_dev *dev)
{
	return 100000;
}

static u32 navi_amd_get_clk_rate_khz(struct dw_i2c_dev *dev)
{
	return 100000;
}

static int navi_amd_setup(struct pci_dev *pdev, struct dw_pci_controller *c)
{
	struct dw_i2c_dev *dev = pci_get_drvdata(pdev);

	dev->flags |= MODEL_AMD_NAVI_GPU | ACCESS_POLLING;
	dev->timings.bus_freq_hz = I2C_MAX_STANDARD_MODE_FREQ;
	return 0;
}

static struct dw_pci_controller dw_pci_controllers[] = {
	[medfield] = {
		.bus_num = -1,
		.setup = mfld_setup,
		.get_clk_rate_khz = mfld_get_clk_rate_khz,
	},
	[merrifield] = {
		.bus_num = -1,
		.scl_sda_cfg = &mrfld_config,
		.setup = mrfld_setup,
	},
	[baytrail] = {
		.bus_num = -1,
		.scl_sda_cfg = &byt_config,
	},
	[haswell] = {
		.bus_num = -1,
		.scl_sda_cfg = &hsw_config,
	},
	[cherrytrail] = {
		.bus_num = -1,
		.scl_sda_cfg = &byt_config,
	},
	[elkhartlake] = {
		.bus_num = -1,
		.get_clk_rate_khz = ehl_get_clk_rate_khz,
	},
	[navi_amd] = {
		.bus_num = -1,
		.scl_sda_cfg = &navi_amd_config,
		.setup =  navi_amd_setup,
		.get_clk_rate_khz = navi_amd_get_clk_rate_khz,
	},
};

static const struct property_entry dgpu_properties[] = {
	/* USB-C doesn't power the system */
	PROPERTY_ENTRY_U8("scope", POWER_SUPPLY_SCOPE_DEVICE),
	{}
};

static const struct software_node dgpu_node = {
	.properties = dgpu_properties,
};

static int i2c_dw_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	struct device *device = &pdev->dev;
	struct dw_i2c_dev *dev;
	struct i2c_adapter *adap;
	int r;
	struct dw_pci_controller *controller;
	struct dw_scl_sda_cfg *cfg;

	if (id->driver_data >= ARRAY_SIZE(dw_pci_controllers))
		return dev_err_probe(device, -EINVAL, "Invalid driver data %ld\n",
				     id->driver_data);

	controller = &dw_pci_controllers[id->driver_data];

	r = pcim_enable_device(pdev);
	if (r)
		return dev_err_probe(device, r, "Failed to enable I2C PCI device\n");

	pci_set_master(pdev);

	r = pcim_iomap_regions(pdev, 1 << 0, pci_name(pdev));
	if (r)
		return dev_err_probe(device, r, "I/O memory remapping failed\n");

	dev = devm_kzalloc(device, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	r = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (r < 0)
		return r;

	dev->get_clk_rate_khz = controller->get_clk_rate_khz;
	dev->base = pcim_iomap_table(pdev)[0];
	dev->dev = device;
	dev->irq = pci_irq_vector(pdev, 0);
	dev->flags |= controller->flags;

	pci_set_drvdata(pdev, dev);

	if (controller->setup) {
		r = controller->setup(pdev, controller);
		if (r)
			return r;
	}

	r = i2c_dw_fw_parse_and_configure(dev);
	if (r)
		return r;

	i2c_dw_configure(dev);

	if (controller->scl_sda_cfg) {
		cfg = controller->scl_sda_cfg;
		dev->ss_hcnt = cfg->ss_hcnt;
		dev->fs_hcnt = cfg->fs_hcnt;
		dev->ss_lcnt = cfg->ss_lcnt;
		dev->fs_lcnt = cfg->fs_lcnt;
		dev->sda_hold_time = cfg->sda_hold_time;
	}

	adap = &dev->adapter;
	adap->owner = THIS_MODULE;
	adap->class = 0;
	adap->nr = controller->bus_num;

	r = i2c_dw_probe(dev);
	if (r)
		return r;

	if ((dev->flags & MODEL_MASK) == MODEL_AMD_NAVI_GPU) {
		dev->slave = i2c_new_ccgx_ucsi(&dev->adapter, dev->irq, &dgpu_node);
		if (IS_ERR(dev->slave))
			return dev_err_probe(device, PTR_ERR(dev->slave),
					     "register UCSI failed\n");
	}

	pm_runtime_set_autosuspend_delay(device, 1000);
	pm_runtime_use_autosuspend(device);
	pm_runtime_put_autosuspend(device);
	pm_runtime_allow(device);

	return 0;
}

static void i2c_dw_pci_remove(struct pci_dev *pdev)
{
	struct dw_i2c_dev *dev = pci_get_drvdata(pdev);
	struct device *device = &pdev->dev;

	i2c_dw_disable(dev);

	pm_runtime_forbid(device);
	pm_runtime_get_noresume(device);

	i2c_del_adapter(&dev->adapter);
}

static const struct pci_device_id i2c_designware_pci_ids[] = {
	/* Medfield */
	{ PCI_VDEVICE(INTEL, 0x0817), medfield },
	{ PCI_VDEVICE(INTEL, 0x0818), medfield },
	{ PCI_VDEVICE(INTEL, 0x0819), medfield },
	{ PCI_VDEVICE(INTEL, 0x082C), medfield },
	{ PCI_VDEVICE(INTEL, 0x082D), medfield },
	{ PCI_VDEVICE(INTEL, 0x082E), medfield },
	/* Merrifield */
	{ PCI_VDEVICE(INTEL, 0x1195), merrifield },
	{ PCI_VDEVICE(INTEL, 0x1196), merrifield },
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
	/* Braswell / Cherrytrail */
	{ PCI_VDEVICE(INTEL, 0x22C1), cherrytrail },
	{ PCI_VDEVICE(INTEL, 0x22C2), cherrytrail },
	{ PCI_VDEVICE(INTEL, 0x22C3), cherrytrail },
	{ PCI_VDEVICE(INTEL, 0x22C4), cherrytrail },
	{ PCI_VDEVICE(INTEL, 0x22C5), cherrytrail },
	{ PCI_VDEVICE(INTEL, 0x22C6), cherrytrail },
	{ PCI_VDEVICE(INTEL, 0x22C7), cherrytrail },
	/* Elkhart Lake (PSE I2C) */
	{ PCI_VDEVICE(INTEL, 0x4bb9), elkhartlake },
	{ PCI_VDEVICE(INTEL, 0x4bba), elkhartlake },
	{ PCI_VDEVICE(INTEL, 0x4bbb), elkhartlake },
	{ PCI_VDEVICE(INTEL, 0x4bbc), elkhartlake },
	{ PCI_VDEVICE(INTEL, 0x4bbd), elkhartlake },
	{ PCI_VDEVICE(INTEL, 0x4bbe), elkhartlake },
	{ PCI_VDEVICE(INTEL, 0x4bbf), elkhartlake },
	{ PCI_VDEVICE(INTEL, 0x4bc0), elkhartlake },
	/* AMD NAVI */
	{ PCI_VDEVICE(ATI,  0x7314), navi_amd },
	{ PCI_VDEVICE(ATI,  0x73a4), navi_amd },
	{ PCI_VDEVICE(ATI,  0x73e4), navi_amd },
	{ PCI_VDEVICE(ATI,  0x73c4), navi_amd },
	{ PCI_VDEVICE(ATI,  0x7444), navi_amd },
	{ PCI_VDEVICE(ATI,  0x7464), navi_amd },
	{}
};
MODULE_DEVICE_TABLE(pci, i2c_designware_pci_ids);

static struct pci_driver dw_i2c_driver = {
	.name		= DRIVER_NAME,
	.probe		= i2c_dw_pci_probe,
	.remove		= i2c_dw_pci_remove,
	.driver         = {
		.pm	= pm_ptr(&i2c_dw_dev_pm_ops),
	},
	.id_table	= i2c_designware_pci_ids,
};
module_pci_driver(dw_i2c_driver);

MODULE_AUTHOR("Baruch Siach <baruch@tkos.co.il>");
MODULE_DESCRIPTION("Synopsys DesignWare PCI I2C bus adapter");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(I2C_DW);
MODULE_IMPORT_NS(I2C_DW_COMMON);
