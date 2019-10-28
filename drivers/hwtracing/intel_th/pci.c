// SPDX-License-Identifier: GPL-2.0
/*
 * Intel(R) Trace Hub pci driver
 *
 * Copyright (C) 2014-2015 Intel Corporation.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/pci.h>

#include "intel_th.h"

#define DRIVER_NAME "intel_th_pci"

enum {
	TH_PCI_CONFIG_BAR	= 0,
	TH_PCI_STH_SW_BAR	= 2,
	TH_PCI_RTIT_BAR		= 4,
};

#define BAR_MASK (BIT(TH_PCI_CONFIG_BAR) | BIT(TH_PCI_STH_SW_BAR))

#define PCI_REG_NPKDSC	0x80
#define NPKDSC_TSACT	BIT(5)

static int intel_th_pci_activate(struct intel_th *th)
{
	struct pci_dev *pdev = to_pci_dev(th->dev);
	u32 npkdsc;
	int err;

	if (!INTEL_TH_CAP(th, tscu_enable))
		return 0;

	err = pci_read_config_dword(pdev, PCI_REG_NPKDSC, &npkdsc);
	if (!err) {
		npkdsc |= NPKDSC_TSACT;
		err = pci_write_config_dword(pdev, PCI_REG_NPKDSC, npkdsc);
	}

	if (err)
		dev_err(&pdev->dev, "failed to read NPKDSC register\n");

	return err;
}

static void intel_th_pci_deactivate(struct intel_th *th)
{
	struct pci_dev *pdev = to_pci_dev(th->dev);
	u32 npkdsc;
	int err;

	if (!INTEL_TH_CAP(th, tscu_enable))
		return;

	err = pci_read_config_dword(pdev, PCI_REG_NPKDSC, &npkdsc);
	if (!err) {
		npkdsc |= NPKDSC_TSACT;
		err = pci_write_config_dword(pdev, PCI_REG_NPKDSC, npkdsc);
	}

	if (err)
		dev_err(&pdev->dev, "failed to read NPKDSC register\n");
}

static int intel_th_pci_probe(struct pci_dev *pdev,
			      const struct pci_device_id *id)
{
	struct intel_th_drvdata *drvdata = (void *)id->driver_data;
	struct resource resource[TH_MMIO_END + TH_NVEC_MAX] = {
		[TH_MMIO_CONFIG]	= pdev->resource[TH_PCI_CONFIG_BAR],
		[TH_MMIO_SW]		= pdev->resource[TH_PCI_STH_SW_BAR],
	};
	int err, r = TH_MMIO_SW + 1, i;
	struct intel_th *th;

	err = pcim_enable_device(pdev);
	if (err)
		return err;

	err = pcim_iomap_regions_request_all(pdev, BAR_MASK, DRIVER_NAME);
	if (err)
		return err;

	if (pdev->resource[TH_PCI_RTIT_BAR].start) {
		resource[TH_MMIO_RTIT] = pdev->resource[TH_PCI_RTIT_BAR];
		r++;
	}

	err = pci_alloc_irq_vectors(pdev, 1, 8, PCI_IRQ_ALL_TYPES);
	if (err > 0)
		for (i = 0; i < err; i++, r++) {
			resource[r].flags = IORESOURCE_IRQ;
			resource[r].start = pci_irq_vector(pdev, i);
		}

	th = intel_th_alloc(&pdev->dev, drvdata, resource, r);
	if (IS_ERR(th))
		return PTR_ERR(th);

	th->activate   = intel_th_pci_activate;
	th->deactivate = intel_th_pci_deactivate;

	pci_set_master(pdev);

	return 0;
}

static void intel_th_pci_remove(struct pci_dev *pdev)
{
	struct intel_th *th = pci_get_drvdata(pdev);

	intel_th_free(th);

	pci_free_irq_vectors(pdev);
}

static const struct intel_th_drvdata intel_th_2x = {
	.tscu_enable	= 1,
	.has_mintctl	= 1,
};

static const struct pci_device_id intel_th_pci_id_table[] = {
	{
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x9d26),
		.driver_data = (kernel_ulong_t)0,
	},
	{
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xa126),
		.driver_data = (kernel_ulong_t)0,
	},
	{
		/* Apollo Lake */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x5a8e),
		.driver_data = (kernel_ulong_t)0,
	},
	{
		/* Broxton */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x0a80),
		.driver_data = (kernel_ulong_t)0,
	},
	{
		/* Broxton B-step */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x1a8e),
		.driver_data = (kernel_ulong_t)0,
	},
	{
		/* Kaby Lake PCH-H */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xa2a6),
		.driver_data = (kernel_ulong_t)0,
	},
	{
		/* Denverton */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x19e1),
		.driver_data = (kernel_ulong_t)0,
	},
	{
		/* Lewisburg PCH */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xa1a6),
		.driver_data = (kernel_ulong_t)0,
	},
	{
		/* Lewisburg PCH */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xa226),
		.driver_data = (kernel_ulong_t)0,
	},
	{
		/* Gemini Lake */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x318e),
		.driver_data = (kernel_ulong_t)&intel_th_2x,
	},
	{
		/* Cannon Lake H */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xa326),
		.driver_data = (kernel_ulong_t)&intel_th_2x,
	},
	{
		/* Cannon Lake LP */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x9da6),
		.driver_data = (kernel_ulong_t)&intel_th_2x,
	},
	{
		/* Cedar Fork PCH */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x18e1),
		.driver_data = (kernel_ulong_t)&intel_th_2x,
	},
	{
		/* Ice Lake PCH */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x34a6),
		.driver_data = (kernel_ulong_t)&intel_th_2x,
	},
	{
		/* Comet Lake */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x02a6),
		.driver_data = (kernel_ulong_t)&intel_th_2x,
	},
	{
		/* Comet Lake PCH */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x06a6),
		.driver_data = (kernel_ulong_t)&intel_th_2x,
	},
	{
		/* Ice Lake NNPI */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x45c5),
		.driver_data = (kernel_ulong_t)&intel_th_2x,
	},
	{
		/* Tiger Lake PCH */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xa0a6),
		.driver_data = (kernel_ulong_t)&intel_th_2x,
	},
	{ 0 },
};

MODULE_DEVICE_TABLE(pci, intel_th_pci_id_table);

static struct pci_driver intel_th_pci_driver = {
	.name		= DRIVER_NAME,
	.id_table	= intel_th_pci_id_table,
	.probe		= intel_th_pci_probe,
	.remove		= intel_th_pci_remove,
};

module_pci_driver(intel_th_pci_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel(R) Trace Hub PCI controller driver");
MODULE_AUTHOR("Alexander Shishkin <alexander.shishkin@intel.com>");
