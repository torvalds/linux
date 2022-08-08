// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD MP2 PCIe communication driver
 * Copyright 2020 Advanced Micro Devices, Inc.
 *
 * Authors: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 *	    Sandeep Singh <Sandeep.singh@amd.com>
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmi.h>
#include <linux/interrupt.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "amd_sfh_pcie.h"

#define DRIVER_NAME	"pcie_mp2_amd"
#define DRIVER_DESC	"AMD(R) PCIe MP2 Communication Driver"

#define ACEL_EN		BIT(0)
#define GYRO_EN		BIT(1)
#define MAGNO_EN	BIT(2)
#define ALS_EN		BIT(19)

static int sensor_mask_override = -1;
module_param_named(sensor_mask, sensor_mask_override, int, 0444);
MODULE_PARM_DESC(sensor_mask, "override the detected sensors mask");

void amd_start_sensor(struct amd_mp2_dev *privdata, struct amd_mp2_sensor_info info)
{
	union sfh_cmd_param cmd_param;
	union sfh_cmd_base cmd_base;

	/* fill up command register */
	memset(&cmd_base, 0, sizeof(cmd_base));
	cmd_base.s.cmd_id = ENABLE_SENSOR;
	cmd_base.s.period = info.period;
	cmd_base.s.sensor_id = info.sensor_idx;

	/* fill up command param register */
	memset(&cmd_param, 0, sizeof(cmd_param));
	cmd_param.s.buf_layout = 1;
	cmd_param.s.buf_length = 16;

	writeq(info.dma_address, privdata->mmio + AMD_C2P_MSG2);
	writel(cmd_param.ul, privdata->mmio + AMD_C2P_MSG1);
	writel(cmd_base.ul, privdata->mmio + AMD_C2P_MSG0);
}

void amd_stop_sensor(struct amd_mp2_dev *privdata, u16 sensor_idx)
{
	union sfh_cmd_base cmd_base;

	/* fill up command register */
	memset(&cmd_base, 0, sizeof(cmd_base));
	cmd_base.s.cmd_id = DISABLE_SENSOR;
	cmd_base.s.period = 0;
	cmd_base.s.sensor_id = sensor_idx;

	writeq(0x0, privdata->mmio + AMD_C2P_MSG2);
	writel(cmd_base.ul, privdata->mmio + AMD_C2P_MSG0);
}

void amd_stop_all_sensors(struct amd_mp2_dev *privdata)
{
	union sfh_cmd_base cmd_base;

	/* fill up command register */
	memset(&cmd_base, 0, sizeof(cmd_base));
	cmd_base.s.cmd_id = STOP_ALL_SENSORS;
	cmd_base.s.period = 0;
	cmd_base.s.sensor_id = 0;

	writel(cmd_base.ul, privdata->mmio + AMD_C2P_MSG0);
}

static const struct dmi_system_id dmi_sensor_mask_overrides[] = {
	{
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP ENVY x360 Convertible 13-ag0xxx"),
		},
		.driver_data = (void *)(ACEL_EN | MAGNO_EN),
	},
	{
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP ENVY x360 Convertible 15-cp0xxx"),
		},
		.driver_data = (void *)(ACEL_EN | MAGNO_EN),
	},
	{ }
};

int amd_mp2_get_sensor_num(struct amd_mp2_dev *privdata, u8 *sensor_id)
{
	int activestatus, num_of_sensors = 0;
	const struct dmi_system_id *dmi_id;
	u32 activecontrolstatus;

	if (sensor_mask_override == -1) {
		dmi_id = dmi_first_match(dmi_sensor_mask_overrides);
		if (dmi_id)
			sensor_mask_override = (long)dmi_id->driver_data;
	}

	if (sensor_mask_override >= 0) {
		activestatus = sensor_mask_override;
	} else {
		activecontrolstatus = readl(privdata->mmio + AMD_P2C_MSG3);
		activestatus = activecontrolstatus >> 4;
	}

	if (ACEL_EN  & activestatus)
		sensor_id[num_of_sensors++] = accel_idx;

	if (GYRO_EN & activestatus)
		sensor_id[num_of_sensors++] = gyro_idx;

	if (MAGNO_EN & activestatus)
		sensor_id[num_of_sensors++] = mag_idx;

	if (ALS_EN & activestatus)
		sensor_id[num_of_sensors++] = als_idx;

	return num_of_sensors;
}

static void amd_mp2_pci_remove(void *privdata)
{
	amd_sfh_hid_client_deinit(privdata);
	amd_stop_all_sensors(privdata);
}

static int amd_mp2_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct amd_mp2_dev *privdata;
	int rc;

	privdata = devm_kzalloc(&pdev->dev, sizeof(*privdata), GFP_KERNEL);
	if (!privdata)
		return -ENOMEM;

	privdata->pdev = pdev;
	pci_set_drvdata(pdev, privdata);
	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	rc = pcim_iomap_regions(pdev, BIT(2), DRIVER_NAME);
	if (rc)
		return rc;

	privdata->mmio = pcim_iomap_table(pdev)[2];
	pci_set_master(pdev);
	rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (rc) {
		rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		return rc;
	}
	rc = devm_add_action_or_reset(&pdev->dev, amd_mp2_pci_remove, privdata);
	if (rc)
		return rc;

	return amd_sfh_hid_client_init(privdata);
}

static const struct pci_device_id amd_mp2_pci_tbl[] = {
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_MP2) },
	{ }
};
MODULE_DEVICE_TABLE(pci, amd_mp2_pci_tbl);

static struct pci_driver amd_mp2_pci_driver = {
	.name		= DRIVER_NAME,
	.id_table	= amd_mp2_pci_tbl,
	.probe		= amd_mp2_pci_probe,
};
module_pci_driver(amd_mp2_pci_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Shyam Sundar S K <Shyam-sundar.S-k@amd.com>");
MODULE_AUTHOR("Sandeep Singh <Sandeep.singh@amd.com>");
