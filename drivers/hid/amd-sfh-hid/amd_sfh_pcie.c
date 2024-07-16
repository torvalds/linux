// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD MP2 PCIe communication driver
 * Copyright 2020-2021 Advanced Micro Devices, Inc.
 *
 * Authors: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 *	    Sandeep Singh <Sandeep.singh@amd.com>
 *	    Basavaraj Natikar <Basavaraj.Natikar@amd.com>
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
#include "sfh1_1/amd_sfh_init.h"

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

static bool intr_disable = true;

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
	cmd_base.cmd_v2.intr_disable = intr_disable;
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
	cmd_base.cmd_v2.intr_disable = intr_disable;
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
	cmd_base.cmd_v2.intr_disable = intr_disable;
	cmd_base.cmd_v2.period = 0;
	cmd_base.cmd_v2.sensor_id = 0;

	writel(cmd_base.ul, privdata->mmio + AMD_C2P_MSG0);
}

void amd_sfh_clear_intr_v2(struct amd_mp2_dev *privdata)
{
	if (readl(privdata->mmio + AMD_P2C_MSG(4))) {
		writel(0, privdata->mmio + AMD_P2C_MSG(4));
		writel(0xf, privdata->mmio + AMD_P2C_MSG(5));
	}
}

void amd_sfh_clear_intr(struct amd_mp2_dev *privdata)
{
	if (privdata->mp2_ops->clear_intr)
		privdata->mp2_ops->clear_intr(privdata);
}

static irqreturn_t amd_sfh_irq_handler(int irq, void *data)
{
	amd_sfh_clear_intr(data);

	return IRQ_HANDLED;
}

int amd_sfh_irq_init_v2(struct amd_mp2_dev *privdata)
{
	int rc;

	pci_intx(privdata->pdev, true);

	rc = devm_request_irq(&privdata->pdev->dev, privdata->pdev->irq,
			      amd_sfh_irq_handler, 0, DRIVER_NAME, privdata);
	if (rc) {
		dev_err(&privdata->pdev->dev, "failed to request irq %d err=%d\n",
			privdata->pdev->irq, rc);
		return rc;
	}

	return 0;
}

static int amd_sfh_dis_sts_v2(struct amd_mp2_dev *privdata)
{
	return (readl(privdata->mmio + AMD_P2C_MSG(1)) &
		      SENSOR_DISCOVERY_STATUS_MASK) >> SENSOR_DISCOVERY_STATUS_SHIFT;
}

static void amd_start_sensor(struct amd_mp2_dev *privdata, struct amd_mp2_sensor_info info)
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

static void amd_stop_sensor(struct amd_mp2_dev *privdata, u16 sensor_idx)
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

static void amd_stop_all_sensors(struct amd_mp2_dev *privdata)
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
	pci_intx(mp2->pdev, false);
	amd_sfh_clear_intr(mp2);
}

static struct amd_mp2_ops amd_sfh_ops_v2 = {
	.start = amd_start_sensor_v2,
	.stop = amd_stop_sensor_v2,
	.stop_all = amd_stop_all_sensor_v2,
	.response = amd_sfh_wait_response_v2,
	.clear_intr = amd_sfh_clear_intr_v2,
	.init_intr = amd_sfh_irq_init_v2,
	.discovery_status = amd_sfh_dis_sts_v2,
	.remove = amd_mp2_pci_remove,
};

static struct amd_mp2_ops amd_sfh_ops = {
	.start = amd_start_sensor,
	.stop = amd_stop_sensor,
	.stop_all = amd_stop_all_sensors,
	.remove = amd_mp2_pci_remove,
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

int amd_sfh_irq_init(struct amd_mp2_dev *privdata)
{
	if (privdata->mp2_ops->init_intr)
		return privdata->mp2_ops->init_intr(privdata);

	return 0;
}

static int mp2_disable_intr(const struct dmi_system_id *id)
{
	intr_disable = false;
	return 0;
}

static const struct dmi_system_id dmi_sfh_table[] = {
	{
		/*
		 * https://bugzilla.kernel.org/show_bug.cgi?id=218104
		 */
		.callback = mp2_disable_intr,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP ProBook x360 435 G7"),
		},
	},
	{}
};

static const struct dmi_system_id dmi_nodevs[] = {
	{
		/*
		 * Google Chromebooks use Chrome OS Embedded Controller Sensor
		 * Hub instead of Sensor Hub Fusion and leaves MP2
		 * uninitialized, which disables all functionalities, even
		 * including the registers necessary for feature detections.
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Google"),
		},
	},
	{ }
};

static int amd_mp2_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct amd_mp2_dev *privdata;
	int rc;

	if (dmi_first_match(dmi_nodevs))
		return -ENODEV;

	dmi_check_system(dmi_sfh_table);

	privdata = devm_kzalloc(&pdev->dev, sizeof(*privdata), GFP_KERNEL);
	if (!privdata)
		return -ENOMEM;

	privdata->pdev = pdev;
	dev_set_drvdata(&pdev->dev, privdata);
	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	rc = pcim_iomap_regions(pdev, BIT(2), DRIVER_NAME);
	if (rc)
		return rc;

	privdata->mmio = pcim_iomap_table(pdev)[2];
	pci_set_master(pdev);
	rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (rc) {
		dev_err(&pdev->dev, "failed to set DMA mask\n");
		return rc;
	}

	privdata->cl_data = devm_kzalloc(&pdev->dev, sizeof(struct amdtp_cl_data), GFP_KERNEL);
	if (!privdata->cl_data)
		return -ENOMEM;

	privdata->sfh1_1_ops = (const struct amd_sfh1_1_ops *)id->driver_data;
	if (privdata->sfh1_1_ops) {
		rc = privdata->sfh1_1_ops->init(privdata);
		if (rc)
			return rc;
		goto init_done;
	}

	mp2_select_ops(privdata);

	rc = amd_sfh_irq_init(privdata);
	if (rc) {
		dev_err(&pdev->dev, "amd_sfh_irq_init failed\n");
		return rc;
	}

	rc = amd_sfh_hid_client_init(privdata);
	if (rc) {
		amd_sfh_clear_intr(privdata);
		if (rc != -EOPNOTSUPP)
			dev_err(&pdev->dev, "amd_sfh_hid_client_init failed\n");
		return rc;
	}

init_done:
	amd_sfh_clear_intr(privdata);

	return devm_add_action_or_reset(&pdev->dev, privdata->mp2_ops->remove, privdata);
}

static void amd_sfh_shutdown(struct pci_dev *pdev)
{
	struct amd_mp2_dev *mp2 = pci_get_drvdata(pdev);

	if (mp2 && mp2->mp2_ops)
		mp2->mp2_ops->stop_all(mp2);
}

static int __maybe_unused amd_mp2_pci_resume(struct device *dev)
{
	struct amd_mp2_dev *mp2 = dev_get_drvdata(dev);

	mp2->mp2_ops->resume(mp2);

	return 0;
}

static int __maybe_unused amd_mp2_pci_suspend(struct device *dev)
{
	struct amd_mp2_dev *mp2 = dev_get_drvdata(dev);

	mp2->mp2_ops->suspend(mp2);

	return 0;
}

static SIMPLE_DEV_PM_OPS(amd_mp2_pm_ops, amd_mp2_pci_suspend,
		amd_mp2_pci_resume);

static const struct pci_device_id amd_mp2_pci_tbl[] = {
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_MP2) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_MP2_1_1),
	  .driver_data = (kernel_ulong_t)&sfh1_1_ops },
	{ }
};
MODULE_DEVICE_TABLE(pci, amd_mp2_pci_tbl);

static struct pci_driver amd_mp2_pci_driver = {
	.name		= DRIVER_NAME,
	.id_table	= amd_mp2_pci_tbl,
	.probe		= amd_mp2_pci_probe,
	.driver.pm	= &amd_mp2_pm_ops,
	.shutdown	= amd_sfh_shutdown,
};
module_pci_driver(amd_mp2_pci_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Shyam Sundar S K <Shyam-sundar.S-k@amd.com>");
MODULE_AUTHOR("Sandeep Singh <Sandeep.singh@amd.com>");
MODULE_AUTHOR("Basavaraj Natikar <Basavaraj.Natikar@amd.com>");
