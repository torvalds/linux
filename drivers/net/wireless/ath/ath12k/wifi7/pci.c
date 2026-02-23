// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2019-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/pci.h>

#include "../pci.h"
#include "pci.h"
#include "../core.h"
#include "../hif.h"
#include "../mhi.h"
#include "hw.h"
#include "../hal.h"
#include "dp.h"
#include "core.h"
#include "hal.h"

#define QCN9274_DEVICE_ID		0x1109
#define WCN7850_DEVICE_ID		0x1107
#define QCC2072_DEVICE_ID		0x1112

#define ATH12K_PCI_W7_SOC_HW_VERSION_1	1
#define ATH12K_PCI_W7_SOC_HW_VERSION_2	2

#define TCSR_SOC_HW_VERSION		0x1B00000
#define TCSR_SOC_HW_VERSION_MAJOR_MASK	GENMASK(11, 8)
#define TCSR_SOC_HW_VERSION_MINOR_MASK	GENMASK(7, 4)

#define WINDOW_REG_ADDRESS		0x310c
#define WINDOW_REG_ADDRESS_QCC2072	0x3278

static const struct pci_device_id ath12k_wifi7_pci_id_table[] = {
	{ PCI_VDEVICE(QCOM, QCN9274_DEVICE_ID) },
	{ PCI_VDEVICE(QCOM, WCN7850_DEVICE_ID) },
	{ PCI_VDEVICE(QCOM, QCC2072_DEVICE_ID) },
	{}
};

MODULE_DEVICE_TABLE(pci, ath12k_wifi7_pci_id_table);

/* TODO: revisit IRQ mapping for new SRNG's */
static const struct ath12k_msi_config ath12k_wifi7_msi_config[] = {
	{
		.total_vectors = 16,
		.total_users = 3,
		.users = (struct ath12k_msi_user[]) {
			{ .name = "MHI", .num_vectors = 3, .base_vector = 0 },
			{ .name = "CE", .num_vectors = 5, .base_vector = 3 },
			{ .name = "DP", .num_vectors = 8, .base_vector = 8 },
		},
	},
};

static const struct ath12k_pci_ops ath12k_wifi7_pci_ops_qcn9274 = {
	.wakeup = NULL,
	.release = NULL,
};

static int ath12k_wifi7_pci_bus_wake_up(struct ath12k_base *ab)
{
	struct ath12k_pci *ab_pci = ath12k_pci_priv(ab);

	return mhi_device_get_sync(ab_pci->mhi_ctrl->mhi_dev);
}

static void ath12k_wifi7_pci_bus_release(struct ath12k_base *ab)
{
	struct ath12k_pci *ab_pci = ath12k_pci_priv(ab);

	mhi_device_put(ab_pci->mhi_ctrl->mhi_dev);
}

static const struct ath12k_pci_ops ath12k_wifi7_pci_ops_wcn7850 = {
	.wakeup = ath12k_wifi7_pci_bus_wake_up,
	.release = ath12k_wifi7_pci_bus_release,
};

static
void ath12k_wifi7_pci_read_hw_version(struct ath12k_base *ab,
				      u32 *major, u32 *minor)
{
	u32 soc_hw_version;

	soc_hw_version = ath12k_pci_read32(ab, TCSR_SOC_HW_VERSION);
	*major = u32_get_bits(soc_hw_version, TCSR_SOC_HW_VERSION_MAJOR_MASK);
	*minor = u32_get_bits(soc_hw_version, TCSR_SOC_HW_VERSION_MINOR_MASK);
}

static int ath12k_wifi7_pci_probe(struct pci_dev *pdev,
				  const struct pci_device_id *pci_dev)
{
	u32 soc_hw_version_major, soc_hw_version_minor;
	struct ath12k_pci *ab_pci;
	struct ath12k_base *ab;
	int ret;

	ab = pci_get_drvdata(pdev);
	if (!ab)
		return -EINVAL;

	ab_pci = ath12k_pci_priv(ab);
	if (!ab_pci)
		return -EINVAL;

	switch (pci_dev->device) {
	case QCN9274_DEVICE_ID:
		ab_pci->msi_config = &ath12k_wifi7_msi_config[0];
		ab->static_window_map = true;
		ab_pci->pci_ops = &ath12k_wifi7_pci_ops_qcn9274;
		/*
		 * init window reg addr before reading hardware version
		 * as it will be used there
		 */
		ab_pci->window_reg_addr = WINDOW_REG_ADDRESS;
		ath12k_wifi7_pci_read_hw_version(ab, &soc_hw_version_major,
						 &soc_hw_version_minor);
		ab->target_mem_mode = ath12k_core_get_memory_mode(ab);
		switch (soc_hw_version_major) {
		case ATH12K_PCI_W7_SOC_HW_VERSION_2:
			ab->hw_rev = ATH12K_HW_QCN9274_HW20;
			break;
		case ATH12K_PCI_W7_SOC_HW_VERSION_1:
			ab->hw_rev = ATH12K_HW_QCN9274_HW10;
			break;
		default:
			dev_err(&pdev->dev,
				"Unknown hardware version found for QCN9274: 0x%x\n",
				soc_hw_version_major);
			return -EOPNOTSUPP;
		}
		break;
	case WCN7850_DEVICE_ID:
		ab->id.bdf_search = ATH12K_BDF_SEARCH_BUS_AND_BOARD;
		ab_pci->msi_config = &ath12k_wifi7_msi_config[0];
		ab->static_window_map = false;
		ab_pci->pci_ops = &ath12k_wifi7_pci_ops_wcn7850;
		/*
		 * init window reg addr before reading hardware version
		 * as it will be used there
		 */
		ab_pci->window_reg_addr = WINDOW_REG_ADDRESS;
		ath12k_wifi7_pci_read_hw_version(ab, &soc_hw_version_major,
						 &soc_hw_version_minor);
		ab->target_mem_mode = ATH12K_QMI_MEMORY_MODE_DEFAULT;
		switch (soc_hw_version_major) {
		case ATH12K_PCI_W7_SOC_HW_VERSION_2:
			ab->hw_rev = ATH12K_HW_WCN7850_HW20;
			break;
		default:
			dev_err(&pdev->dev,
				"Unknown hardware version found for WCN7850: 0x%x\n",
				soc_hw_version_major);
			return -EOPNOTSUPP;
		}
		break;
	case QCC2072_DEVICE_ID:
		ab->id.bdf_search = ATH12K_BDF_SEARCH_BUS_AND_BOARD;
		ab_pci->msi_config = &ath12k_wifi7_msi_config[0];
		ab->static_window_map = false;
		ab_pci->pci_ops = &ath12k_wifi7_pci_ops_wcn7850;
		ab_pci->window_reg_addr = WINDOW_REG_ADDRESS_QCC2072;
		ab->target_mem_mode = ATH12K_QMI_MEMORY_MODE_DEFAULT;
		/* there is only one version till now */
		ab->hw_rev = ATH12K_HW_QCC2072_HW10;
		break;
	default:
		dev_err(&pdev->dev, "Unknown Wi-Fi 7 PCI device found: 0x%x\n",
			pci_dev->device);
		return -EOPNOTSUPP;
	}

	ret = ath12k_wifi7_hw_init(ab);
	if (ret) {
		dev_err(&pdev->dev, "WiFi-7 hw_init for PCI failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct ath12k_pci_reg_base ath12k_wifi7_reg_base = {
	.umac_base = HAL_SEQ_WCSS_UMAC_OFFSET,
	.ce_reg_base = HAL_CE_WFSS_CE_REG_BASE,
};

static struct ath12k_pci_driver ath12k_wifi7_pci_driver = {
	.name = "ath12k_wifi7_pci",
	.id_table = ath12k_wifi7_pci_id_table,
	.ops.probe = ath12k_wifi7_pci_probe,
	.reg_base = &ath12k_wifi7_reg_base,
	.ops.arch_init = ath12k_wifi7_arch_init,
	.ops.arch_deinit = ath12k_wifi7_arch_deinit,
};

int ath12k_wifi7_pci_init(void)
{
	int ret;

	ret = ath12k_pci_register_driver(ATH12K_DEVICE_FAMILY_WIFI7,
					 &ath12k_wifi7_pci_driver);
	if (ret) {
		pr_err("Failed to register ath12k Wi-Fi 7 driver: %d\n",
		       ret);
		return ret;
	}

	return 0;
}

void ath12k_wifi7_pci_exit(void)
{
	ath12k_pci_unregister_driver(ATH12K_DEVICE_FAMILY_WIFI7);
}
