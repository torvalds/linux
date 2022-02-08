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
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "amd_sfh_pcie.h"

#define DRIVER_NAME	"pcie_mp2_amd"
#define DRIVER_DESC	"AMD(R) PCIe MP2 Communication Driver"

#define ACEL_EN		BIT(0)
#define GYRO_EN		BIT(1)
#define MAGNO_EN	BIT(2)
#define HPD_EN		BIT(16)
#define ALS_EN		BIT(19)

static int sensor_mask_override = -1;
module_param_named(sensor_mask, sensor_mask_override, int, 0444);
MODULE_PARM_DESC(sensor_mask, "override the detected sensors mask");

static int amd_sfh_wait_response_v2(struct amd_mp2_dev *mp2, u8 sid, u32 sensor_sts)
{
	union cmd_response cmd_resp;

	/* Get response with status within a max of 1600 ms timeout */
	if (!readl_poll_timeout(mp2->mmio + AMD_P2C_MSG(0), cmd_resp.resp,
				(cmd_resp.response_v2.response == sensor_sts &&
				cmd_resp.response_v2.status == 0 && (sid == 0xff ||
				cmd_resp.response_v2.sensor_id == sid)), 500, 1600000))
		return cmd_resp.response_v2.response;

	return SENSOR_DISABLED;
}

static void amd_start_sensor_v2(struct amd_mp2_dev *privdata, struct amd_mp2_sensor_info info)
{
	union sfh_cmd_base cmd_base;

	cmd_base.ul = 0;
	cmd_base.cmd_v2.cmd_id = ENABLE_SENSOR;
	cmd_base.cmd_v2.period = info.period;
	cmd_base.cmd_v2.sensor_id = info.sensor_idx;
	cmd_base.cmd_v2.length = 16;

	if (info.sensor_idx == als_idx)
		cmd_base.cmd_v2.mem_type = USE_C2P_REG;

	writeq(info.dma_address, privdata->mmio + AMD_C2P_MSG1);
	writel(cmd_base.ul, privdata->mmio + AMD_C2P_MSG0);
}

static void amd_stop_sensor_v2(struct amd_mp2_dev *privdata, u16 sensor_idx)
{
	union sfh_cmd_base cmd_base;

	cmd_base.ul = 0;
	cmd_base.cmd_v2.cmd_id = DISABLE_SENSOR;
	cmd_base.cmd_v2.period = 0;
	cmd_base.cmd_v2.sensor_id = sensor_idx;
	cmd_base.cmd_v2.length  = 16;

	writeq(0x0, privdata->mmio + AMD_C2P_MSG1);
	writel(cmd_base.ul, privdata->mmio + AMD_C2P_MSG0);
}

static void amd_stop_all_sensor_v2(struct amd_mp2_dev *privdata)
{
	union sfh_cmd_base cmd_base;

	cmd_base.cmd_v2.cmd_id = STOP_ALL_SENSORS;
	cmd_base.cmd_v2.period = 0;
	cmd_base.cmd_v2.sensor_id = 0;

	writel(cmd_base.ul, privdata->mmio + AMD_C2P_MSG0);
}

static void amd_sfh_clear_intr_v2(struct amd_mp2_dev *privdata)
{
	if (readl(privdata->mmio + AMD_P2C_MSG(4))) {
		writel(0, privdata->mmio + AMD_P2C_MSG(4));
		writel(0xf, privdata->mmio + AMD_P2C_MSG(5));
	}
}

static void amd_sfh_clear_intr(struct amd_mp2_dev *privdata)
{
	if (privdata->mp2_ops->clear_intr)
		privdata->mp2_ops->clear_intr(privdata);
}

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

	if (sensor_mask_override == -1) {
		dmi_id = dmi_first_match(dmi_sensor_mask_overrides);
		if (dmi_id)
			sensor_mask_override = (long)dmi_id->driver_data;
	}

	if (sensor_mask_override >= 0) {
		activestatus = sensor_mask_override;
	} else {
		activestatus = privdata->mp2_acs >> 4;
	}

	if (ACEL_EN  & activestatus)
		sensor_id[num_of_sensors++] = accel_idx;

	if (GYRO_EN & activestatus)
		sensor_id[num_of_sensors++] = gyro_idx;

	if (MAGNO_EN & activestatus)
		sensor_id[num_of_sensors++] = mag_idx;

	if (ALS_EN & activestatus)
		sensor_id[num_of_sensors++] = als_idx;

	if (HPD_EN & activestatus)
		sensor_id[num_of_sensors++] = HPD_IDX;

	return num_of_sensors;
}

static void amd_mp2_pci_remove(void *privdata)
{
	struct amd_mp2_dev *mp2 = privdata;
	amd_sfh_hid_client_deinit(privdata);
	mp2->mp2_ops->stop_all(mp2);
	amd_sfh_clear_intr(mp2);
}

static const struct amd_mp2_ops amd_sfh_ops_v2 = {
	.start = amd_start_sensor_v2,
	.stop = amd_stop_sensor_v2,
	.stop_all = amd_stop_all_sensor_v2,
	.response = amd_sfh_wait_response_v2,
	.clear_intr = amd_sfh_clear_intr_v2,
};

static const struct amd_mp2_ops amd_sfh_ops = {
	.start = amd_start_sensor,
	.stop = amd_stop_sensor,
	.stop_all = amd_stop_all_sensors,
};

static void mp2_select_ops(struct amd_mp2_dev *privdata)
{
	u8 acs;

	privdata->mp2_acs = readl(privdata->mmio + AMD_P2C_MSG3);
	acs = privdata->mp2_acs & GENMASK(3, 0);

	switch (acs) {
	case V2_STATUS:
		privdata->mp2_ops = &amd_sfh_ops_v2;
		break;
	default:
		privdata->mp2_ops = &amd_sfh_ops;
		break;
	}
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

	privdata->cl_data = devm_kzalloc(&pdev->dev, sizeof(struct amdtp_cl_data), GFP_KERNEL);
	if (!privdata->cl_data)
		return -ENOMEM;

	mp2_select_ops(privdata);

	rc = amd_sfh_hid_client_init(privdata);
	if (rc) {
		amd_sfh_clear_intr(privdata);
		dev_err(&pdev->dev, "amd_sfh_hid_client_init failed\n");
		return rc;
	}

	amd_sfh_clear_intr(privdata);

	return devm_add_action_or_reset(&pdev->dev, amd_mp2_pci_remove, privdata);
}

static int __maybe_unused amd_mp2_pci_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct amd_mp2_dev *mp2 = pci_get_drvdata(pdev);
	struct amdtp_cl_data *cl_data = mp2->cl_data;
	struct amd_mp2_sensor_info info;
	int i, status;

	for (i = 0; i < cl_data->num_hid_devices; i++) {
		if (cl_data->sensor_sts[i] == SENSOR_DISABLED) {
			info.period = AMD_SFH_IDLE_LOOP;
			info.sensor_idx = cl_data->sensor_idx[i];
			info.dma_address = cl_data->sensor_dma_addr[i];
			mp2->mp2_ops->start(mp2, info);
			status = amd_sfh_wait_for_response
					(mp2, cl_data->sensor_idx[i], SENSOR_ENABLED);
			if (status == SENSOR_ENABLED)
				cl_data->sensor_sts[i] = SENSOR_ENABLED;
			dev_dbg(dev, "resume sid 0x%x status 0x%x\n",
				cl_data->sensor_idx[i], cl_data->sensor_sts[i]);
		}
	}

	schedule_delayed_work(&cl_data->work_buffer, msecs_to_jiffies(AMD_SFH_IDLE_LOOP));
	amd_sfh_clear_intr(mp2);

	return 0;
}

static int __maybe_unused amd_mp2_pci_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct amd_mp2_dev *mp2 = pci_get_drvdata(pdev);
	struct amdtp_cl_data *cl_data = mp2->cl_data;
	int i, status;

	for (i = 0; i < cl_data->num_hid_devices; i++) {
		if (cl_data->sensor_idx[i] != HPD_IDX &&
		    cl_data->sensor_sts[i] == SENSOR_ENABLED) {
			mp2->mp2_ops->stop(mp2, cl_data->sensor_idx[i]);
			status = amd_sfh_wait_for_response
					(mp2, cl_data->sensor_idx[i], SENSOR_DISABLED);
			if (status != SENSOR_ENABLED)
				cl_data->sensor_sts[i] = SENSOR_DISABLED;
			dev_dbg(dev, "suspend sid 0x%x status 0x%x\n",
				cl_data->sensor_idx[i], cl_data->sensor_sts[i]);
		}
	}

	cancel_delayed_work_sync(&cl_data->work_buffer);
	amd_sfh_clear_intr(mp2);

	return 0;
}

static SIMPLE_DEV_PM_OPS(amd_mp2_pm_ops, amd_mp2_pci_suspend,
		amd_mp2_pci_resume);

static const struct pci_device_id amd_mp2_pci_tbl[] = {
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_MP2) },
	{ }
};
MODULE_DEVICE_TABLE(pci, amd_mp2_pci_tbl);

static struct pci_driver amd_mp2_pci_driver = {
	.name		= DRIVER_NAME,
	.id_table	= amd_mp2_pci_tbl,
	.probe		= amd_mp2_pci_probe,
	.driver.pm	= &amd_mp2_pm_ops,
};
module_pci_driver(amd_mp2_pci_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Shyam Sundar S K <Shyam-sundar.S-k@amd.com>");
MODULE_AUTHOR("Sandeep Singh <Sandeep.singh@amd.com>");
