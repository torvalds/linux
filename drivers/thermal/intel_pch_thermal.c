/* intel_pch_thermal.c - Intel PCH Thermal driver
 *
 * Copyright (c) 2015, Intel Corporation.
 *
 * Authors:
 *     Tushar Dave <tushar.n.dave@intel.com>
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

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/thermal.h>
#include <linux/pm.h>

/* Intel PCH thermal Device IDs */
#define PCH_THERMAL_DID_HSW_1	0x9C24 /* Haswell PCH */
#define PCH_THERMAL_DID_HSW_2	0x8C24 /* Haswell PCH */
#define PCH_THERMAL_DID_WPT	0x9CA4 /* Wildcat Point */
#define PCH_THERMAL_DID_SKL	0x9D31 /* Skylake PCH */

/* Wildcat Point-LP  PCH Thermal registers */
#define WPT_TEMP	0x0000	/* Temperature */
#define WPT_TSC	0x04	/* Thermal Sensor Control */
#define WPT_TSS	0x06	/* Thermal Sensor Status */
#define WPT_TSEL	0x08	/* Thermal Sensor Enable and Lock */
#define WPT_TSREL	0x0A	/* Thermal Sensor Report Enable and Lock */
#define WPT_TSMIC	0x0C	/* Thermal Sensor SMI Control */
#define WPT_CTT	0x0010	/* Catastrophic Trip Point */
#define WPT_TAHV	0x0014	/* Thermal Alert High Value */
#define WPT_TALV	0x0018	/* Thermal Alert Low Value */
#define WPT_TL		0x00000040	/* Throttle Value */
#define WPT_PHL	0x0060	/* PCH Hot Level */
#define WPT_PHLC	0x62	/* PHL Control */
#define WPT_TAS	0x80	/* Thermal Alert Status */
#define WPT_TSPIEN	0x82	/* PCI Interrupt Event Enables */
#define WPT_TSGPEN	0x84	/* General Purpose Event Enables */

/*  Wildcat Point-LP  PCH Thermal Register bit definitions */
#define WPT_TEMP_TSR	0x00ff	/* Temp TS Reading */
#define WPT_TSC_CPDE	0x01	/* Catastrophic Power-Down Enable */
#define WPT_TSS_TSDSS	0x10	/* Thermal Sensor Dynamic Shutdown Status */
#define WPT_TSS_GPES	0x08	/* GPE status */
#define WPT_TSEL_ETS	0x01    /* Enable TS */
#define WPT_TSEL_PLDB	0x80	/* TSEL Policy Lock-Down Bit */
#define WPT_TL_TOL	0x000001FF	/* T0 Level */
#define WPT_TL_T1L	0x1ff00000	/* T1 Level */
#define WPT_TL_TTEN	0x20000000	/* TT Enable */

static char driver_name[] = "Intel PCH thermal driver";

struct pch_thermal_device {
	void __iomem *hw_base;
	const struct pch_dev_ops *ops;
	struct pci_dev *pdev;
	struct thermal_zone_device *tzd;
	int crt_trip_id;
	unsigned long crt_temp;
	int hot_trip_id;
	unsigned long hot_temp;
	int psv_trip_id;
	unsigned long psv_temp;
	bool bios_enabled;
};

#ifdef CONFIG_ACPI

/*
 * On some platforms, there is a companion ACPI device, which adds
 * passive trip temperature using _PSV method. There is no specific
 * passive temperature setting in MMIO interface of this PCI device.
 */
static void pch_wpt_add_acpi_psv_trip(struct pch_thermal_device *ptd,
				      int *nr_trips)
{
	struct acpi_device *adev;

	ptd->psv_trip_id = -1;

	adev = ACPI_COMPANION(&ptd->pdev->dev);
	if (adev) {
		unsigned long long r;
		acpi_status status;

		status = acpi_evaluate_integer(adev->handle, "_PSV", NULL,
					       &r);
		if (ACPI_SUCCESS(status)) {
			unsigned long trip_temp;

			trip_temp = DECI_KELVIN_TO_MILLICELSIUS(r);
			if (trip_temp) {
				ptd->psv_temp = trip_temp;
				ptd->psv_trip_id = *nr_trips;
				++(*nr_trips);
			}
		}
	}
}
#else
static void pch_wpt_add_acpi_psv_trip(struct pch_thermal_device *ptd,
				      int *nr_trips)
{
	ptd->psv_trip_id = -1;

}
#endif

static int pch_wpt_init(struct pch_thermal_device *ptd, int *nr_trips)
{
	u8 tsel;
	u16 trip_temp;

	*nr_trips = 0;

	/* Check if BIOS has already enabled thermal sensor */
	if (WPT_TSS_TSDSS & readb(ptd->hw_base + WPT_TSS)) {
		ptd->bios_enabled = true;
		goto read_trips;
	}

	tsel = readb(ptd->hw_base + WPT_TSEL);
	/*
	 * When TSEL's Policy Lock-Down bit is 1, TSEL become RO.
	 * If so, thermal sensor cannot enable. Bail out.
	 */
	if (tsel & WPT_TSEL_PLDB) {
		dev_err(&ptd->pdev->dev, "Sensor can't be enabled\n");
		return -ENODEV;
	}

	writeb(tsel|WPT_TSEL_ETS, ptd->hw_base + WPT_TSEL);
	if (!(WPT_TSS_TSDSS & readb(ptd->hw_base + WPT_TSS))) {
		dev_err(&ptd->pdev->dev, "Sensor can't be enabled\n");
		return -ENODEV;
	}

read_trips:
	ptd->crt_trip_id = -1;
	trip_temp = readw(ptd->hw_base + WPT_CTT);
	trip_temp &= 0x1FF;
	if (trip_temp) {
		/* Resolution of 1/2 degree C and an offset of -50C */
		ptd->crt_temp = trip_temp * 1000 / 2 - 50000;
		ptd->crt_trip_id = 0;
		++(*nr_trips);
	}

	ptd->hot_trip_id = -1;
	trip_temp = readw(ptd->hw_base + WPT_PHL);
	trip_temp &= 0x1FF;
	if (trip_temp) {
		/* Resolution of 1/2 degree C and an offset of -50C */
		ptd->hot_temp = trip_temp * 1000 / 2 - 50000;
		ptd->hot_trip_id = *nr_trips;
		++(*nr_trips);
	}

	pch_wpt_add_acpi_psv_trip(ptd, nr_trips);

	return 0;
}

static int pch_wpt_get_temp(struct pch_thermal_device *ptd, int *temp)
{
	u8 wpt_temp;

	wpt_temp = WPT_TEMP_TSR & readl(ptd->hw_base + WPT_TEMP);

	/* Resolution of 1/2 degree C and an offset of -50C */
	*temp = (wpt_temp * 1000 / 2 - 50000);

	return 0;
}

static int pch_wpt_suspend(struct pch_thermal_device *ptd)
{
	u8 tsel;

	if (ptd->bios_enabled)
		return 0;

	tsel = readb(ptd->hw_base + WPT_TSEL);

	writeb(tsel & 0xFE, ptd->hw_base + WPT_TSEL);

	return 0;
}

static int pch_wpt_resume(struct pch_thermal_device *ptd)
{
	u8 tsel;

	if (ptd->bios_enabled)
		return 0;

	tsel = readb(ptd->hw_base + WPT_TSEL);

	writeb(tsel | WPT_TSEL_ETS, ptd->hw_base + WPT_TSEL);

	return 0;
}

struct pch_dev_ops {
	int (*hw_init)(struct pch_thermal_device *ptd, int *nr_trips);
	int (*get_temp)(struct pch_thermal_device *ptd, int *temp);
	int (*suspend)(struct pch_thermal_device *ptd);
	int (*resume)(struct pch_thermal_device *ptd);
};


/* dev ops for Wildcat Point */
static const struct pch_dev_ops pch_dev_ops_wpt = {
	.hw_init = pch_wpt_init,
	.get_temp = pch_wpt_get_temp,
	.suspend = pch_wpt_suspend,
	.resume = pch_wpt_resume,
};

static int pch_thermal_get_temp(struct thermal_zone_device *tzd, int *temp)
{
	struct pch_thermal_device *ptd = tzd->devdata;

	return	ptd->ops->get_temp(ptd, temp);
}

static int pch_get_trip_type(struct thermal_zone_device *tzd, int trip,
			     enum thermal_trip_type *type)
{
	struct pch_thermal_device *ptd = tzd->devdata;

	if (ptd->crt_trip_id == trip)
		*type = THERMAL_TRIP_CRITICAL;
	else if (ptd->hot_trip_id == trip)
		*type = THERMAL_TRIP_HOT;
	else if (ptd->psv_trip_id == trip)
		*type = THERMAL_TRIP_PASSIVE;
	else
		return -EINVAL;

	return 0;
}

static int pch_get_trip_temp(struct thermal_zone_device *tzd, int trip, int *temp)
{
	struct pch_thermal_device *ptd = tzd->devdata;

	if (ptd->crt_trip_id == trip)
		*temp = ptd->crt_temp;
	else if (ptd->hot_trip_id == trip)
		*temp = ptd->hot_temp;
	else if (ptd->psv_trip_id == trip)
		*temp = ptd->psv_temp;
	else
		return -EINVAL;

	return 0;
}

static struct thermal_zone_device_ops tzd_ops = {
	.get_temp = pch_thermal_get_temp,
	.get_trip_type = pch_get_trip_type,
	.get_trip_temp = pch_get_trip_temp,
};


static int intel_pch_thermal_probe(struct pci_dev *pdev,
				   const struct pci_device_id *id)
{
	struct pch_thermal_device *ptd;
	int err;
	int nr_trips;
	char *dev_name;

	ptd = devm_kzalloc(&pdev->dev, sizeof(*ptd), GFP_KERNEL);
	if (!ptd)
		return -ENOMEM;

	switch (pdev->device) {
	case PCH_THERMAL_DID_WPT:
		ptd->ops = &pch_dev_ops_wpt;
		dev_name = "pch_wildcat_point";
		break;
	case PCH_THERMAL_DID_SKL:
		ptd->ops = &pch_dev_ops_wpt;
		dev_name = "pch_skylake";
		break;
	case PCH_THERMAL_DID_HSW_1:
	case PCH_THERMAL_DID_HSW_2:
		ptd->ops = &pch_dev_ops_wpt;
		dev_name = "pch_haswell";
		break;
	default:
		dev_err(&pdev->dev, "unknown pch thermal device\n");
		return -ENODEV;
	}

	pci_set_drvdata(pdev, ptd);
	ptd->pdev = pdev;

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "failed to enable pci device\n");
		return err;
	}

	err = pci_request_regions(pdev, driver_name);
	if (err) {
		dev_err(&pdev->dev, "failed to request pci region\n");
		goto error_disable;
	}

	ptd->hw_base = pci_ioremap_bar(pdev, 0);
	if (!ptd->hw_base) {
		err = -ENOMEM;
		dev_err(&pdev->dev, "failed to map mem base\n");
		goto error_release;
	}

	err = ptd->ops->hw_init(ptd, &nr_trips);
	if (err)
		goto error_cleanup;

	ptd->tzd = thermal_zone_device_register(dev_name, nr_trips, 0, ptd,
						&tzd_ops, NULL, 0, 0);
	if (IS_ERR(ptd->tzd)) {
		dev_err(&pdev->dev, "Failed to register thermal zone %s\n",
			dev_name);
		err = PTR_ERR(ptd->tzd);
		goto error_cleanup;
	}

	return 0;

error_cleanup:
	iounmap(ptd->hw_base);
error_release:
	pci_release_regions(pdev);
error_disable:
	pci_disable_device(pdev);
	dev_err(&pdev->dev, "pci device failed to probe\n");
	return err;
}

static void intel_pch_thermal_remove(struct pci_dev *pdev)
{
	struct pch_thermal_device *ptd = pci_get_drvdata(pdev);

	thermal_zone_device_unregister(ptd->tzd);
	iounmap(ptd->hw_base);
	pci_set_drvdata(pdev, NULL);
	pci_release_region(pdev, 0);
	pci_disable_device(pdev);
}

static int intel_pch_thermal_suspend(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct pch_thermal_device *ptd = pci_get_drvdata(pdev);

	return ptd->ops->suspend(ptd);
}

static int intel_pch_thermal_resume(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct pch_thermal_device *ptd = pci_get_drvdata(pdev);

	return ptd->ops->resume(ptd);
}

static struct pci_device_id intel_pch_thermal_id[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCH_THERMAL_DID_WPT) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCH_THERMAL_DID_SKL) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCH_THERMAL_DID_HSW_1) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCH_THERMAL_DID_HSW_2) },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, intel_pch_thermal_id);

static const struct dev_pm_ops intel_pch_pm_ops = {
	.suspend = intel_pch_thermal_suspend,
	.resume = intel_pch_thermal_resume,
};

static struct pci_driver intel_pch_thermal_driver = {
	.name		= "intel_pch_thermal",
	.id_table	= intel_pch_thermal_id,
	.probe		= intel_pch_thermal_probe,
	.remove		= intel_pch_thermal_remove,
	.driver.pm	= &intel_pch_pm_ops,
};

module_pci_driver(intel_pch_thermal_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel PCH Thermal driver");
