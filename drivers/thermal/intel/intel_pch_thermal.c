// SPDX-License-Identifier: GPL-2.0-only
/* intel_pch_thermal.c - Intel PCH Thermal driver
 *
 * Copyright (c) 2015, Intel Corporation.
 *
 * Authors:
 *     Tushar Dave <tushar.n.dave@intel.com>
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/suspend.h>
#include <linux/thermal.h>
#include <linux/types.h>
#include <linux/units.h>

/* Intel PCH thermal Device IDs */
#define PCH_THERMAL_DID_HSW_1	0x9C24 /* Haswell PCH */
#define PCH_THERMAL_DID_HSW_2	0x8C24 /* Haswell PCH */
#define PCH_THERMAL_DID_WPT	0x9CA4 /* Wildcat Point */
#define PCH_THERMAL_DID_SKL	0x9D31 /* Skylake PCH */
#define PCH_THERMAL_DID_SKL_H	0xA131 /* Skylake PCH 100 series */
#define PCH_THERMAL_DID_CNL	0x9Df9 /* CNL PCH */
#define PCH_THERMAL_DID_CNL_H	0xA379 /* CNL-H PCH */
#define PCH_THERMAL_DID_CNL_LP	0x02F9 /* CNL-LP PCH */
#define PCH_THERMAL_DID_CML_H	0X06F9 /* CML-H PCH */
#define PCH_THERMAL_DID_LWB	0xA1B1 /* Lewisburg PCH */

/* Wildcat Point-LP  PCH Thermal registers */
#define WPT_TEMP	0x0000	/* Temperature */
#define WPT_TSC	0x04	/* Thermal Sensor Control */
#define WPT_TSS	0x06	/* Thermal Sensor Status */
#define WPT_TSEL	0x08	/* Thermal Sensor Enable and Lock */
#define WPT_TSREL	0x0A	/* Thermal Sensor Report Enable and Lock */
#define WPT_TSMIC	0x0C	/* Thermal Sensor SMI Control */
#define WPT_CTT	0x0010	/* Catastrophic Trip Point */
#define WPT_TSPM	0x001C	/* Thermal Sensor Power Management */
#define WPT_TAHV	0x0014	/* Thermal Alert High Value */
#define WPT_TALV	0x0018	/* Thermal Alert Low Value */
#define WPT_TL		0x00000040	/* Throttle Value */
#define WPT_PHL	0x0060	/* PCH Hot Level */
#define WPT_PHLC	0x62	/* PHL Control */
#define WPT_TAS	0x80	/* Thermal Alert Status */
#define WPT_TSPIEN	0x82	/* PCI Interrupt Event Enables */
#define WPT_TSGPEN	0x84	/* General Purpose Event Enables */

/*  Wildcat Point-LP  PCH Thermal Register bit definitions */
#define WPT_TEMP_TSR	0x01ff	/* Temp TS Reading */
#define WPT_TSC_CPDE	0x01	/* Catastrophic Power-Down Enable */
#define WPT_TSS_TSDSS	0x10	/* Thermal Sensor Dynamic Shutdown Status */
#define WPT_TSS_GPES	0x08	/* GPE status */
#define WPT_TSEL_ETS	0x01    /* Enable TS */
#define WPT_TSEL_PLDB	0x80	/* TSEL Policy Lock-Down Bit */
#define WPT_TL_TOL	0x000001FF	/* T0 Level */
#define WPT_TL_T1L	0x1ff00000	/* T1 Level */
#define WPT_TL_TTEN	0x20000000	/* TT Enable */

/* Resolution of 1/2 degree C and an offset of -50C */
#define PCH_TEMP_OFFSET	(-50)
#define GET_WPT_TEMP(x)	((x) * MILLIDEGREE_PER_DEGREE / 2 + WPT_TEMP_OFFSET)
#define WPT_TEMP_OFFSET	(PCH_TEMP_OFFSET * MILLIDEGREE_PER_DEGREE)
#define GET_PCH_TEMP(x)	(((x) / 2) + PCH_TEMP_OFFSET)

/* Amount of time for each cooling delay, 100ms by default for now */
static unsigned int delay_timeout = 100;
module_param(delay_timeout, int, 0644);
MODULE_PARM_DESC(delay_timeout, "amount of time delay for each iteration.");

/* Number of iterations for cooling delay, 10 counts by default for now */
static unsigned int delay_cnt = 10;
module_param(delay_cnt, int, 0644);
MODULE_PARM_DESC(delay_cnt, "total number of iterations for time delay.");

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

			trip_temp = deci_kelvin_to_millicelsius(r);
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
	if (WPT_TSEL_ETS & readb(ptd->hw_base + WPT_TSEL)) {
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
	if (!(WPT_TSEL_ETS & readb(ptd->hw_base + WPT_TSEL))) {
		dev_err(&ptd->pdev->dev, "Sensor can't be enabled\n");
		return -ENODEV;
	}

read_trips:
	ptd->crt_trip_id = -1;
	trip_temp = readw(ptd->hw_base + WPT_CTT);
	trip_temp &= 0x1FF;
	if (trip_temp) {
		ptd->crt_temp = GET_WPT_TEMP(trip_temp);
		ptd->crt_trip_id = 0;
		++(*nr_trips);
	}

	ptd->hot_trip_id = -1;
	trip_temp = readw(ptd->hw_base + WPT_PHL);
	trip_temp &= 0x1FF;
	if (trip_temp) {
		ptd->hot_temp = GET_WPT_TEMP(trip_temp);
		ptd->hot_trip_id = *nr_trips;
		++(*nr_trips);
	}

	pch_wpt_add_acpi_psv_trip(ptd, nr_trips);

	return 0;
}

static int pch_wpt_get_temp(struct pch_thermal_device *ptd, int *temp)
{
	*temp = GET_WPT_TEMP(WPT_TEMP_TSR & readw(ptd->hw_base + WPT_TEMP));

	return 0;
}

static int pch_wpt_suspend(struct pch_thermal_device *ptd)
{
	u8 tsel;
	u8 pch_delay_cnt = 1;
	u16 pch_thr_temp, pch_cur_temp;

	/* Shutdown the thermal sensor if it is not enabled by BIOS */
	if (!ptd->bios_enabled) {
		tsel = readb(ptd->hw_base + WPT_TSEL);
		writeb(tsel & 0xFE, ptd->hw_base + WPT_TSEL);
		return 0;
	}

	/* Do not check temperature if it is not a S0ix capable platform */
#ifdef CONFIG_ACPI
	if (!(acpi_gbl_FADT.flags & ACPI_FADT_LOW_POWER_S0))
		return 0;
#else
	return 0;
#endif

	/* Do not check temperature if it is not s2idle */
	if (pm_suspend_via_firmware())
		return 0;

	/* Get the PCH temperature threshold value */
	pch_thr_temp = GET_PCH_TEMP(WPT_TEMP_TSR & readw(ptd->hw_base + WPT_TSPM));

	/* Get the PCH current temperature value */
	pch_cur_temp = GET_PCH_TEMP(WPT_TEMP_TSR & readw(ptd->hw_base + WPT_TEMP));

	/*
	 * If current PCH temperature is higher than configured PCH threshold
	 * value, run some delay loop with sleep to let the current temperature
	 * go down below the threshold value which helps to allow system enter
	 * lower power S0ix suspend state. Even after delay loop if PCH current
	 * temperature stays above threshold, notify the warning message
	 * which helps to indentify the reason why S0ix entry was rejected.
	 */
	while (pch_delay_cnt <= delay_cnt) {
		if (pch_cur_temp <= pch_thr_temp)
			break;

		dev_warn(&ptd->pdev->dev,
			"CPU-PCH current temp [%dC] higher than the threshold temp [%dC], sleep %d times for %d ms duration\n",
			pch_cur_temp, pch_thr_temp, pch_delay_cnt, delay_timeout);
		msleep(delay_timeout);
		/* Read the PCH current temperature for next cycle. */
		pch_cur_temp = GET_PCH_TEMP(WPT_TEMP_TSR & readw(ptd->hw_base + WPT_TEMP));
		pch_delay_cnt++;
	}

	if (pch_cur_temp > pch_thr_temp)
		dev_warn(&ptd->pdev->dev,
			"CPU-PCH is hot [%dC] even after delay, continue to suspend. S0ix might fail\n",
			pch_cur_temp);
	else
		dev_info(&ptd->pdev->dev,
			"CPU-PCH is cool [%dC], continue to suspend\n", pch_cur_temp);

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

enum board_ids {
	board_hsw,
	board_wpt,
	board_skl,
	board_cnl,
	board_cml,
	board_lwb,
};

static const struct board_info {
	const char *name;
	const struct pch_dev_ops *ops;
} board_info[] = {
	[board_hsw] = {
		.name = "pch_haswell",
		.ops = &pch_dev_ops_wpt,
	},
	[board_wpt] = {
		.name = "pch_wildcat_point",
		.ops = &pch_dev_ops_wpt,
	},
	[board_skl] = {
		.name = "pch_skylake",
		.ops = &pch_dev_ops_wpt,
	},
	[board_cnl] = {
		.name = "pch_cannonlake",
		.ops = &pch_dev_ops_wpt,
	},
	[board_cml] = {
		.name = "pch_cometlake",
		.ops = &pch_dev_ops_wpt,
	},
	[board_lwb] = {
		.name = "pch_lewisburg",
		.ops = &pch_dev_ops_wpt,
	},
};

static int intel_pch_thermal_probe(struct pci_dev *pdev,
				   const struct pci_device_id *id)
{
	enum board_ids board_id = id->driver_data;
	const struct board_info *bi = &board_info[board_id];
	struct pch_thermal_device *ptd;
	int err;
	int nr_trips;

	ptd = devm_kzalloc(&pdev->dev, sizeof(*ptd), GFP_KERNEL);
	if (!ptd)
		return -ENOMEM;

	ptd->ops = bi->ops;

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

	ptd->tzd = thermal_zone_device_register(bi->name, nr_trips, 0, ptd,
						&tzd_ops, NULL, 0, 0);
	if (IS_ERR(ptd->tzd)) {
		dev_err(&pdev->dev, "Failed to register thermal zone %s\n",
			bi->name);
		err = PTR_ERR(ptd->tzd);
		goto error_cleanup;
	}
	err = thermal_zone_device_enable(ptd->tzd);
	if (err)
		goto err_unregister;

	return 0;

err_unregister:
	thermal_zone_device_unregister(ptd->tzd);
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
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static int intel_pch_thermal_suspend(struct device *device)
{
	struct pch_thermal_device *ptd = dev_get_drvdata(device);

	return ptd->ops->suspend(ptd);
}

static int intel_pch_thermal_resume(struct device *device)
{
	struct pch_thermal_device *ptd = dev_get_drvdata(device);

	return ptd->ops->resume(ptd);
}

static const struct pci_device_id intel_pch_thermal_id[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCH_THERMAL_DID_HSW_1),
		.driver_data = board_hsw, },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCH_THERMAL_DID_HSW_2),
		.driver_data = board_hsw, },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCH_THERMAL_DID_WPT),
		.driver_data = board_wpt, },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCH_THERMAL_DID_SKL),
		.driver_data = board_skl, },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCH_THERMAL_DID_SKL_H),
		.driver_data = board_skl, },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCH_THERMAL_DID_CNL),
		.driver_data = board_cnl, },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCH_THERMAL_DID_CNL_H),
		.driver_data = board_cnl, },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCH_THERMAL_DID_CNL_LP),
		.driver_data = board_cnl, },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCH_THERMAL_DID_CML_H),
		.driver_data = board_cml, },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCH_THERMAL_DID_LWB),
		.driver_data = board_lwb, },
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
