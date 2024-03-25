// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 * Copyright (c) 2019-2020 Marvell International Ltd.
 */

#include <linux/stddef.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <asm/byteorder.h>
#include <linux/dma-mapping.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/ethtool.h>
#include <linux/etherdevice.h>
#include <linux/vmalloc.h>
#include <linux/crash_dump.h>
#include <linux/crc32.h>
#include <linux/qed/qed_if.h>
#include <linux/qed/qed_ll2_if.h>
#include <net/devlink.h>
#include <linux/phylink.h>

#include "qed.h"
#include "qed_sriov.h"
#include "qed_sp.h"
#include "qed_dev_api.h"
#include "qed_ll2.h"
#include "qed_fcoe.h"
#include "qed_iscsi.h"

#include "qed_mcp.h"
#include "qed_reg_addr.h"
#include "qed_hw.h"
#include "qed_selftest.h"
#include "qed_debug.h"
#include "qed_devlink.h"

#define QED_ROCE_QPS			(8192)
#define QED_ROCE_DPIS			(8)
#define QED_RDMA_SRQS                   QED_ROCE_QPS
#define QED_NVM_CFG_GET_FLAGS		0xA
#define QED_NVM_CFG_GET_PF_FLAGS	0x1A
#define QED_NVM_CFG_MAX_ATTRS		50

static char version[] =
	"QLogic FastLinQ 4xxxx Core Module qed\n";

MODULE_DESCRIPTION("QLogic FastLinQ 4xxxx Core Module");
MODULE_LICENSE("GPL");

#define FW_FILE_VERSION				\
	__stringify(FW_MAJOR_VERSION) "."	\
	__stringify(FW_MINOR_VERSION) "."	\
	__stringify(FW_REVISION_VERSION) "."	\
	__stringify(FW_ENGINEERING_VERSION)

#define QED_FW_FILE_NAME	\
	"qed/qed_init_values_zipped-" FW_FILE_VERSION ".bin"

MODULE_FIRMWARE(QED_FW_FILE_NAME);

/* MFW speed capabilities maps */

struct qed_mfw_speed_map {
	u32		mfw_val;
	__ETHTOOL_DECLARE_LINK_MODE_MASK(caps);

	const u32	*cap_arr;
	u32		arr_size;
};

#define QED_MFW_SPEED_MAP(type, arr)		\
{						\
	.mfw_val	= (type),		\
	.cap_arr	= (arr),		\
	.arr_size	= ARRAY_SIZE(arr),	\
}

static const u32 qed_mfw_ext_1g[] __initconst = {
	ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
	ETHTOOL_LINK_MODE_1000baseKX_Full_BIT,
	ETHTOOL_LINK_MODE_1000baseX_Full_BIT,
};

static const u32 qed_mfw_ext_10g[] __initconst = {
	ETHTOOL_LINK_MODE_10000baseT_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseKR_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseKX4_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseR_FEC_BIT,
	ETHTOOL_LINK_MODE_10000baseCR_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseSR_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseLR_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseLRM_Full_BIT,
};

static const u32 qed_mfw_ext_25g[] __initconst = {
	ETHTOOL_LINK_MODE_25000baseKR_Full_BIT,
	ETHTOOL_LINK_MODE_25000baseCR_Full_BIT,
	ETHTOOL_LINK_MODE_25000baseSR_Full_BIT,
};

static const u32 qed_mfw_ext_40g[] __initconst = {
	ETHTOOL_LINK_MODE_40000baseLR4_Full_BIT,
	ETHTOOL_LINK_MODE_40000baseKR4_Full_BIT,
	ETHTOOL_LINK_MODE_40000baseCR4_Full_BIT,
	ETHTOOL_LINK_MODE_40000baseSR4_Full_BIT,
};

static const u32 qed_mfw_ext_50g_base_r[] __initconst = {
	ETHTOOL_LINK_MODE_50000baseKR_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseCR_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseSR_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseLR_ER_FR_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseDR_Full_BIT,
};

static const u32 qed_mfw_ext_50g_base_r2[] __initconst = {
	ETHTOOL_LINK_MODE_50000baseKR2_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseCR2_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseSR2_Full_BIT,
};

static const u32 qed_mfw_ext_100g_base_r2[] __initconst = {
	ETHTOOL_LINK_MODE_100000baseKR2_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseSR2_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseCR2_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseDR2_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseLR2_ER2_FR2_Full_BIT,
};

static const u32 qed_mfw_ext_100g_base_r4[] __initconst = {
	ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseSR4_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseCR4_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseLR4_ER4_Full_BIT,
};

static struct qed_mfw_speed_map qed_mfw_ext_maps[] __ro_after_init = {
	QED_MFW_SPEED_MAP(ETH_EXT_ADV_SPEED_1G, qed_mfw_ext_1g),
	QED_MFW_SPEED_MAP(ETH_EXT_ADV_SPEED_10G, qed_mfw_ext_10g),
	QED_MFW_SPEED_MAP(ETH_EXT_ADV_SPEED_25G, qed_mfw_ext_25g),
	QED_MFW_SPEED_MAP(ETH_EXT_ADV_SPEED_40G, qed_mfw_ext_40g),
	QED_MFW_SPEED_MAP(ETH_EXT_ADV_SPEED_50G_BASE_R,
			  qed_mfw_ext_50g_base_r),
	QED_MFW_SPEED_MAP(ETH_EXT_ADV_SPEED_50G_BASE_R2,
			  qed_mfw_ext_50g_base_r2),
	QED_MFW_SPEED_MAP(ETH_EXT_ADV_SPEED_100G_BASE_R2,
			  qed_mfw_ext_100g_base_r2),
	QED_MFW_SPEED_MAP(ETH_EXT_ADV_SPEED_100G_BASE_R4,
			  qed_mfw_ext_100g_base_r4),
};

static const u32 qed_mfw_legacy_1g[] __initconst = {
	ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
	ETHTOOL_LINK_MODE_1000baseKX_Full_BIT,
	ETHTOOL_LINK_MODE_1000baseX_Full_BIT,
};

static const u32 qed_mfw_legacy_10g[] __initconst = {
	ETHTOOL_LINK_MODE_10000baseT_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseKR_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseKX4_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseR_FEC_BIT,
	ETHTOOL_LINK_MODE_10000baseCR_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseSR_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseLR_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseLRM_Full_BIT,
};

static const u32 qed_mfw_legacy_20g[] __initconst = {
	ETHTOOL_LINK_MODE_20000baseKR2_Full_BIT,
};

static const u32 qed_mfw_legacy_25g[] __initconst = {
	ETHTOOL_LINK_MODE_25000baseKR_Full_BIT,
	ETHTOOL_LINK_MODE_25000baseCR_Full_BIT,
	ETHTOOL_LINK_MODE_25000baseSR_Full_BIT,
};

static const u32 qed_mfw_legacy_40g[] __initconst = {
	ETHTOOL_LINK_MODE_40000baseLR4_Full_BIT,
	ETHTOOL_LINK_MODE_40000baseKR4_Full_BIT,
	ETHTOOL_LINK_MODE_40000baseCR4_Full_BIT,
	ETHTOOL_LINK_MODE_40000baseSR4_Full_BIT,
};

static const u32 qed_mfw_legacy_50g[] __initconst = {
	ETHTOOL_LINK_MODE_50000baseKR2_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseCR2_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseSR2_Full_BIT,
};

static const u32 qed_mfw_legacy_bb_100g[] __initconst = {
	ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseSR4_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseCR4_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseLR4_ER4_Full_BIT,
};

static struct qed_mfw_speed_map qed_mfw_legacy_maps[] __ro_after_init = {
	QED_MFW_SPEED_MAP(NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_1G,
			  qed_mfw_legacy_1g),
	QED_MFW_SPEED_MAP(NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_10G,
			  qed_mfw_legacy_10g),
	QED_MFW_SPEED_MAP(NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_20G,
			  qed_mfw_legacy_20g),
	QED_MFW_SPEED_MAP(NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_25G,
			  qed_mfw_legacy_25g),
	QED_MFW_SPEED_MAP(NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_40G,
			  qed_mfw_legacy_40g),
	QED_MFW_SPEED_MAP(NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_50G,
			  qed_mfw_legacy_50g),
	QED_MFW_SPEED_MAP(NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_BB_100G,
			  qed_mfw_legacy_bb_100g),
};

static void __init qed_mfw_speed_map_populate(struct qed_mfw_speed_map *map)
{
	linkmode_set_bit_array(map->cap_arr, map->arr_size, map->caps);

	map->cap_arr = NULL;
	map->arr_size = 0;
}

static void __init qed_mfw_speed_maps_init(void)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(qed_mfw_ext_maps); i++)
		qed_mfw_speed_map_populate(qed_mfw_ext_maps + i);

	for (i = 0; i < ARRAY_SIZE(qed_mfw_legacy_maps); i++)
		qed_mfw_speed_map_populate(qed_mfw_legacy_maps + i);
}

static int __init qed_init(void)
{
	pr_info("%s", version);

	qed_mfw_speed_maps_init();

	return 0;
}
module_init(qed_init);

static void __exit qed_exit(void)
{
	/* To prevent marking this module as "permanent" */
}
module_exit(qed_exit);

static void qed_free_pci(struct qed_dev *cdev)
{
	struct pci_dev *pdev = cdev->pdev;

	if (cdev->doorbells && cdev->db_size)
		iounmap(cdev->doorbells);
	if (cdev->regview)
		iounmap(cdev->regview);
	if (atomic_read(&pdev->enable_cnt) == 1)
		pci_release_regions(pdev);

	pci_disable_device(pdev);
}

#define PCI_REVISION_ID_ERROR_VAL	0xff

/* Performs PCI initializations as well as initializing PCI-related parameters
 * in the device structrue. Returns 0 in case of success.
 */
static int qed_init_pci(struct qed_dev *cdev, struct pci_dev *pdev)
{
	u8 rev_id;
	int rc;

	cdev->pdev = pdev;

	rc = pci_enable_device(pdev);
	if (rc) {
		DP_NOTICE(cdev, "Cannot enable PCI device\n");
		goto err0;
	}

	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		DP_NOTICE(cdev, "No memory region found in bar #0\n");
		rc = -EIO;
		goto err1;
	}

	if (IS_PF(cdev) && !(pci_resource_flags(pdev, 2) & IORESOURCE_MEM)) {
		DP_NOTICE(cdev, "No memory region found in bar #2\n");
		rc = -EIO;
		goto err1;
	}

	if (atomic_read(&pdev->enable_cnt) == 1) {
		rc = pci_request_regions(pdev, "qed");
		if (rc) {
			DP_NOTICE(cdev,
				  "Failed to request PCI memory resources\n");
			goto err1;
		}
		pci_set_master(pdev);
		pci_save_state(pdev);
	}

	pci_read_config_byte(pdev, PCI_REVISION_ID, &rev_id);
	if (rev_id == PCI_REVISION_ID_ERROR_VAL) {
		DP_NOTICE(cdev,
			  "Detected PCI device error [rev_id 0x%x]. Probably due to prior indication. Aborting.\n",
			  rev_id);
		rc = -ENODEV;
		goto err2;
	}
	if (!pci_is_pcie(pdev)) {
		DP_NOTICE(cdev, "The bus is not PCI Express\n");
		rc = -EIO;
		goto err2;
	}

	if (IS_PF(cdev) && !pdev->pm_cap)
		DP_NOTICE(cdev, "Cannot find power management capability\n");

	rc = dma_set_mask_and_coherent(&cdev->pdev->dev, DMA_BIT_MASK(64));
	if (rc) {
		DP_NOTICE(cdev, "Can't request DMA addresses\n");
		rc = -EIO;
		goto err2;
	}

	cdev->pci_params.mem_start = pci_resource_start(pdev, 0);
	cdev->pci_params.mem_end = pci_resource_end(pdev, 0);
	cdev->pci_params.irq = pdev->irq;

	cdev->regview = pci_ioremap_bar(pdev, 0);
	if (!cdev->regview) {
		DP_NOTICE(cdev, "Cannot map register space, aborting\n");
		rc = -ENOMEM;
		goto err2;
	}

	cdev->db_phys_addr = pci_resource_start(cdev->pdev, 2);
	cdev->db_size = pci_resource_len(cdev->pdev, 2);
	if (!cdev->db_size) {
		if (IS_PF(cdev)) {
			DP_NOTICE(cdev, "No Doorbell bar available\n");
			return -EINVAL;
		} else {
			return 0;
		}
	}

	cdev->doorbells = ioremap_wc(cdev->db_phys_addr, cdev->db_size);

	if (!cdev->doorbells) {
		DP_NOTICE(cdev, "Cannot map doorbell space\n");
		return -ENOMEM;
	}

	return 0;

err2:
	pci_release_regions(pdev);
err1:
	pci_disable_device(pdev);
err0:
	return rc;
}

int qed_fill_dev_info(struct qed_dev *cdev,
		      struct qed_dev_info *dev_info)
{
	struct qed_hwfn *p_hwfn = QED_LEADING_HWFN(cdev);
	struct qed_hw_info *hw_info = &p_hwfn->hw_info;
	struct qed_tunnel_info *tun = &cdev->tunnel;
	struct qed_ptt  *ptt;

	memset(dev_info, 0, sizeof(struct qed_dev_info));

	if (tun->vxlan.tun_cls == QED_TUNN_CLSS_MAC_VLAN &&
	    tun->vxlan.b_mode_enabled)
		dev_info->vxlan_enable = true;

	if (tun->l2_gre.b_mode_enabled && tun->ip_gre.b_mode_enabled &&
	    tun->l2_gre.tun_cls == QED_TUNN_CLSS_MAC_VLAN &&
	    tun->ip_gre.tun_cls == QED_TUNN_CLSS_MAC_VLAN)
		dev_info->gre_enable = true;

	if (tun->l2_geneve.b_mode_enabled && tun->ip_geneve.b_mode_enabled &&
	    tun->l2_geneve.tun_cls == QED_TUNN_CLSS_MAC_VLAN &&
	    tun->ip_geneve.tun_cls == QED_TUNN_CLSS_MAC_VLAN)
		dev_info->geneve_enable = true;

	dev_info->num_hwfns = cdev->num_hwfns;
	dev_info->pci_mem_start = cdev->pci_params.mem_start;
	dev_info->pci_mem_end = cdev->pci_params.mem_end;
	dev_info->pci_irq = cdev->pci_params.irq;
	dev_info->rdma_supported = QED_IS_RDMA_PERSONALITY(p_hwfn);
	dev_info->dev_type = cdev->type;
	ether_addr_copy(dev_info->hw_mac, hw_info->hw_mac_addr);

	if (IS_PF(cdev)) {
		dev_info->fw_major = FW_MAJOR_VERSION;
		dev_info->fw_minor = FW_MINOR_VERSION;
		dev_info->fw_rev = FW_REVISION_VERSION;
		dev_info->fw_eng = FW_ENGINEERING_VERSION;
		dev_info->b_inter_pf_switch = test_bit(QED_MF_INTER_PF_SWITCH,
						       &cdev->mf_bits);
		if (!test_bit(QED_MF_DISABLE_ARFS, &cdev->mf_bits))
			dev_info->b_arfs_capable = true;
		dev_info->tx_switching = true;

		if (hw_info->b_wol_support == QED_WOL_SUPPORT_PME)
			dev_info->wol_support = true;

		dev_info->smart_an = qed_mcp_is_smart_an_supported(p_hwfn);
		dev_info->esl = qed_mcp_is_esl_supported(p_hwfn);
		dev_info->abs_pf_id = QED_LEADING_HWFN(cdev)->abs_pf_id;
	} else {
		qed_vf_get_fw_version(&cdev->hwfns[0], &dev_info->fw_major,
				      &dev_info->fw_minor, &dev_info->fw_rev,
				      &dev_info->fw_eng);
	}

	if (IS_PF(cdev)) {
		ptt = qed_ptt_acquire(QED_LEADING_HWFN(cdev));
		if (ptt) {
			qed_mcp_get_mfw_ver(QED_LEADING_HWFN(cdev), ptt,
					    &dev_info->mfw_rev, NULL);

			qed_mcp_get_mbi_ver(QED_LEADING_HWFN(cdev), ptt,
					    &dev_info->mbi_version);

			qed_mcp_get_flash_size(QED_LEADING_HWFN(cdev), ptt,
					       &dev_info->flash_size);

			qed_ptt_release(QED_LEADING_HWFN(cdev), ptt);
		}
	} else {
		qed_mcp_get_mfw_ver(QED_LEADING_HWFN(cdev), NULL,
				    &dev_info->mfw_rev, NULL);
	}

	dev_info->mtu = hw_info->mtu;
	cdev->common_dev_info = *dev_info;

	return 0;
}

static void qed_free_cdev(struct qed_dev *cdev)
{
	kfree((void *)cdev);
}

static struct qed_dev *qed_alloc_cdev(struct pci_dev *pdev)
{
	struct qed_dev *cdev;

	cdev = kzalloc(sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return cdev;

	qed_init_struct(cdev);

	return cdev;
}

/* Sets the requested power state */
static int qed_set_power_state(struct qed_dev *cdev, pci_power_t state)
{
	if (!cdev)
		return -ENODEV;

	DP_VERBOSE(cdev, NETIF_MSG_DRV, "Omitting Power state change\n");
	return 0;
}

/* probing */
static struct qed_dev *qed_probe(struct pci_dev *pdev,
				 struct qed_probe_params *params)
{
	struct qed_dev *cdev;
	int rc;

	cdev = qed_alloc_cdev(pdev);
	if (!cdev)
		goto err0;

	cdev->drv_type = DRV_ID_DRV_TYPE_LINUX;
	cdev->protocol = params->protocol;

	if (params->is_vf)
		cdev->b_is_vf = true;

	qed_init_dp(cdev, params->dp_module, params->dp_level);

	cdev->recov_in_prog = params->recov_in_prog;

	rc = qed_init_pci(cdev, pdev);
	if (rc) {
		DP_ERR(cdev, "init pci failed\n");
		goto err1;
	}
	DP_INFO(cdev, "PCI init completed successfully\n");

	rc = qed_hw_prepare(cdev, QED_PCI_DEFAULT);
	if (rc) {
		DP_ERR(cdev, "hw prepare failed\n");
		goto err2;
	}

	DP_INFO(cdev, "%s completed successfully\n", __func__);

	return cdev;

err2:
	qed_free_pci(cdev);
err1:
	qed_free_cdev(cdev);
err0:
	return NULL;
}

static void qed_remove(struct qed_dev *cdev)
{
	if (!cdev)
		return;

	qed_hw_remove(cdev);

	qed_free_pci(cdev);

	qed_set_power_state(cdev, PCI_D3hot);

	qed_free_cdev(cdev);
}

static void qed_disable_msix(struct qed_dev *cdev)
{
	if (cdev->int_params.out.int_mode == QED_INT_MODE_MSIX) {
		pci_disable_msix(cdev->pdev);
		kfree(cdev->int_params.msix_table);
	} else if (cdev->int_params.out.int_mode == QED_INT_MODE_MSI) {
		pci_disable_msi(cdev->pdev);
	}

	memset(&cdev->int_params.out, 0, sizeof(struct qed_int_param));
}

static int qed_enable_msix(struct qed_dev *cdev,
			   struct qed_int_params *int_params)
{
	int i, rc, cnt;

	cnt = int_params->in.num_vectors;

	for (i = 0; i < cnt; i++)
		int_params->msix_table[i].entry = i;

	rc = pci_enable_msix_range(cdev->pdev, int_params->msix_table,
				   int_params->in.min_msix_cnt, cnt);
	if (rc < cnt && rc >= int_params->in.min_msix_cnt &&
	    (rc % cdev->num_hwfns)) {
		pci_disable_msix(cdev->pdev);

		/* If fastpath is initialized, we need at least one interrupt
		 * per hwfn [and the slow path interrupts]. New requested number
		 * should be a multiple of the number of hwfns.
		 */
		cnt = (rc / cdev->num_hwfns) * cdev->num_hwfns;
		DP_NOTICE(cdev,
			  "Trying to enable MSI-X with less vectors (%d out of %d)\n",
			  cnt, int_params->in.num_vectors);
		rc = pci_enable_msix_exact(cdev->pdev, int_params->msix_table,
					   cnt);
		if (!rc)
			rc = cnt;
	}

	/* For VFs, we should return with an error in case we didn't get the
	 * exact number of msix vectors as we requested.
	 * Not doing that will lead to a crash when starting queues for
	 * this VF.
	 */
	if ((IS_PF(cdev) && rc > 0) || (IS_VF(cdev) && rc == cnt)) {
		/* MSI-x configuration was achieved */
		int_params->out.int_mode = QED_INT_MODE_MSIX;
		int_params->out.num_vectors = rc;
		rc = 0;
	} else {
		DP_NOTICE(cdev,
			  "Failed to enable MSI-X [Requested %d vectors][rc %d]\n",
			  cnt, rc);
	}

	return rc;
}

/* This function outputs the int mode and the number of enabled msix vector */
static int qed_set_int_mode(struct qed_dev *cdev, bool force_mode)
{
	struct qed_int_params *int_params = &cdev->int_params;
	struct msix_entry *tbl;
	int rc = 0, cnt;

	switch (int_params->in.int_mode) {
	case QED_INT_MODE_MSIX:
		/* Allocate MSIX table */
		cnt = int_params->in.num_vectors;
		int_params->msix_table = kcalloc(cnt, sizeof(*tbl), GFP_KERNEL);
		if (!int_params->msix_table) {
			rc = -ENOMEM;
			goto out;
		}

		/* Enable MSIX */
		rc = qed_enable_msix(cdev, int_params);
		if (!rc)
			goto out;

		DP_NOTICE(cdev, "Failed to enable MSI-X\n");
		kfree(int_params->msix_table);
		if (force_mode)
			goto out;
		fallthrough;

	case QED_INT_MODE_MSI:
		if (cdev->num_hwfns == 1) {
			rc = pci_enable_msi(cdev->pdev);
			if (!rc) {
				int_params->out.int_mode = QED_INT_MODE_MSI;
				goto out;
			}

			DP_NOTICE(cdev, "Failed to enable MSI\n");
			if (force_mode)
				goto out;
		}
		fallthrough;

	case QED_INT_MODE_INTA:
			int_params->out.int_mode = QED_INT_MODE_INTA;
			rc = 0;
			goto out;
	default:
		DP_NOTICE(cdev, "Unknown int_mode value %d\n",
			  int_params->in.int_mode);
		rc = -EINVAL;
	}

out:
	if (!rc)
		DP_INFO(cdev, "Using %s interrupts\n",
			int_params->out.int_mode == QED_INT_MODE_INTA ?
			"INTa" : int_params->out.int_mode == QED_INT_MODE_MSI ?
			"MSI" : "MSIX");
	cdev->int_coalescing_mode = QED_COAL_MODE_ENABLE;

	return rc;
}

static void qed_simd_handler_config(struct qed_dev *cdev, void *token,
				    int index, void(*handler)(void *))
{
	struct qed_hwfn *hwfn = &cdev->hwfns[index % cdev->num_hwfns];
	int relative_idx = index / cdev->num_hwfns;

	hwfn->simd_proto_handler[relative_idx].func = handler;
	hwfn->simd_proto_handler[relative_idx].token = token;
}

static void qed_simd_handler_clean(struct qed_dev *cdev, int index)
{
	struct qed_hwfn *hwfn = &cdev->hwfns[index % cdev->num_hwfns];
	int relative_idx = index / cdev->num_hwfns;

	memset(&hwfn->simd_proto_handler[relative_idx], 0,
	       sizeof(struct qed_simd_fp_handler));
}

static irqreturn_t qed_msix_sp_int(int irq, void *tasklet)
{
	tasklet_schedule((struct tasklet_struct *)tasklet);
	return IRQ_HANDLED;
}

static irqreturn_t qed_single_int(int irq, void *dev_instance)
{
	struct qed_dev *cdev = (struct qed_dev *)dev_instance;
	struct qed_hwfn *hwfn;
	irqreturn_t rc = IRQ_NONE;
	u64 status;
	int i, j;

	for (i = 0; i < cdev->num_hwfns; i++) {
		status = qed_int_igu_read_sisr_reg(&cdev->hwfns[i]);

		if (!status)
			continue;

		hwfn = &cdev->hwfns[i];

		/* Slowpath interrupt */
		if (unlikely(status & 0x1)) {
			tasklet_schedule(&hwfn->sp_dpc);
			status &= ~0x1;
			rc = IRQ_HANDLED;
		}

		/* Fastpath interrupts */
		for (j = 0; j < 64; j++) {
			if ((0x2ULL << j) & status) {
				struct qed_simd_fp_handler *p_handler =
					&hwfn->simd_proto_handler[j];

				if (p_handler->func)
					p_handler->func(p_handler->token);
				else
					DP_NOTICE(hwfn,
						  "Not calling fastpath handler as it is NULL [handler #%d, status 0x%llx]\n",
						  j, status);

				status &= ~(0x2ULL << j);
				rc = IRQ_HANDLED;
			}
		}

		if (unlikely(status))
			DP_VERBOSE(hwfn, NETIF_MSG_INTR,
				   "got an unknown interrupt status 0x%llx\n",
				   status);
	}

	return rc;
}

int qed_slowpath_irq_req(struct qed_hwfn *hwfn)
{
	struct qed_dev *cdev = hwfn->cdev;
	u32 int_mode;
	int rc = 0;
	u8 id;

	int_mode = cdev->int_params.out.int_mode;
	if (int_mode == QED_INT_MODE_MSIX) {
		id = hwfn->my_id;
		snprintf(hwfn->name, NAME_SIZE, "sp-%d-%02x:%02x.%02x",
			 id, cdev->pdev->bus->number,
			 PCI_SLOT(cdev->pdev->devfn), hwfn->abs_pf_id);
		rc = request_irq(cdev->int_params.msix_table[id].vector,
				 qed_msix_sp_int, 0, hwfn->name, &hwfn->sp_dpc);
	} else {
		unsigned long flags = 0;

		snprintf(cdev->name, NAME_SIZE, "%02x:%02x.%02x",
			 cdev->pdev->bus->number, PCI_SLOT(cdev->pdev->devfn),
			 PCI_FUNC(cdev->pdev->devfn));

		if (cdev->int_params.out.int_mode == QED_INT_MODE_INTA)
			flags |= IRQF_SHARED;

		rc = request_irq(cdev->pdev->irq, qed_single_int,
				 flags, cdev->name, cdev);
	}

	if (rc)
		DP_NOTICE(cdev, "request_irq failed, rc = %d\n", rc);
	else
		DP_VERBOSE(hwfn, (NETIF_MSG_INTR | QED_MSG_SP),
			   "Requested slowpath %s\n",
			   (int_mode == QED_INT_MODE_MSIX) ? "MSI-X" : "IRQ");

	return rc;
}

static void qed_slowpath_tasklet_flush(struct qed_hwfn *p_hwfn)
{
	/* Calling the disable function will make sure that any
	 * currently-running function is completed. The following call to the
	 * enable function makes this sequence a flush-like operation.
	 */
	if (p_hwfn->b_sp_dpc_enabled) {
		tasklet_disable(&p_hwfn->sp_dpc);
		tasklet_enable(&p_hwfn->sp_dpc);
	}
}

void qed_slowpath_irq_sync(struct qed_hwfn *p_hwfn)
{
	struct qed_dev *cdev = p_hwfn->cdev;
	u8 id = p_hwfn->my_id;
	u32 int_mode;

	int_mode = cdev->int_params.out.int_mode;
	if (int_mode == QED_INT_MODE_MSIX)
		synchronize_irq(cdev->int_params.msix_table[id].vector);
	else
		synchronize_irq(cdev->pdev->irq);

	qed_slowpath_tasklet_flush(p_hwfn);
}

static void qed_slowpath_irq_free(struct qed_dev *cdev)
{
	int i;

	if (cdev->int_params.out.int_mode == QED_INT_MODE_MSIX) {
		for_each_hwfn(cdev, i) {
			if (!cdev->hwfns[i].b_int_requested)
				break;
			free_irq(cdev->int_params.msix_table[i].vector,
				 &cdev->hwfns[i].sp_dpc);
		}
	} else {
		if (QED_LEADING_HWFN(cdev)->b_int_requested)
			free_irq(cdev->pdev->irq, cdev);
	}
	qed_int_disable_post_isr_release(cdev);
}

static int qed_nic_stop(struct qed_dev *cdev)
{
	int i, rc;

	rc = qed_hw_stop(cdev);

	for (i = 0; i < cdev->num_hwfns; i++) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		if (p_hwfn->b_sp_dpc_enabled) {
			tasklet_disable(&p_hwfn->sp_dpc);
			p_hwfn->b_sp_dpc_enabled = false;
			DP_VERBOSE(cdev, NETIF_MSG_IFDOWN,
				   "Disabled sp tasklet [hwfn %d] at %p\n",
				   i, &p_hwfn->sp_dpc);
		}
	}

	qed_dbg_pf_exit(cdev);

	return rc;
}

static int qed_nic_setup(struct qed_dev *cdev)
{
	int rc, i;

	/* Determine if interface is going to require LL2 */
	if (QED_LEADING_HWFN(cdev)->hw_info.personality != QED_PCI_ETH) {
		for (i = 0; i < cdev->num_hwfns; i++) {
			struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

			p_hwfn->using_ll2 = true;
		}
	}

	rc = qed_resc_alloc(cdev);
	if (rc)
		return rc;

	DP_INFO(cdev, "Allocated qed resources\n");

	qed_resc_setup(cdev);

	return rc;
}

static int qed_set_int_fp(struct qed_dev *cdev, u16 cnt)
{
	int limit = 0;

	/* Mark the fastpath as free/used */
	cdev->int_params.fp_initialized = cnt ? true : false;

	if (cdev->int_params.out.int_mode != QED_INT_MODE_MSIX)
		limit = cdev->num_hwfns * 63;
	else if (cdev->int_params.fp_msix_cnt)
		limit = cdev->int_params.fp_msix_cnt;

	if (!limit)
		return -ENOMEM;

	return min_t(int, cnt, limit);
}

static int qed_get_int_fp(struct qed_dev *cdev, struct qed_int_info *info)
{
	memset(info, 0, sizeof(struct qed_int_info));

	if (!cdev->int_params.fp_initialized) {
		DP_INFO(cdev,
			"Protocol driver requested interrupt information, but its support is not yet configured\n");
		return -EINVAL;
	}

	/* Need to expose only MSI-X information; Single IRQ is handled solely
	 * by qed.
	 */
	if (cdev->int_params.out.int_mode == QED_INT_MODE_MSIX) {
		int msix_base = cdev->int_params.fp_msix_base;

		info->msix_cnt = cdev->int_params.fp_msix_cnt;
		info->msix = &cdev->int_params.msix_table[msix_base];
	}

	return 0;
}

static int qed_slowpath_setup_int(struct qed_dev *cdev,
				  enum qed_int_mode int_mode)
{
	struct qed_sb_cnt_info sb_cnt_info;
	int num_l2_queues = 0;
	int rc;
	int i;

	if ((int_mode == QED_INT_MODE_MSI) && (cdev->num_hwfns > 1)) {
		DP_NOTICE(cdev, "MSI mode is not supported for CMT devices\n");
		return -EINVAL;
	}

	memset(&cdev->int_params, 0, sizeof(struct qed_int_params));
	cdev->int_params.in.int_mode = int_mode;
	for_each_hwfn(cdev, i) {
		memset(&sb_cnt_info, 0, sizeof(sb_cnt_info));
		qed_int_get_num_sbs(&cdev->hwfns[i], &sb_cnt_info);
		cdev->int_params.in.num_vectors += sb_cnt_info.cnt;
		cdev->int_params.in.num_vectors++; /* slowpath */
	}

	/* We want a minimum of one slowpath and one fastpath vector per hwfn */
	cdev->int_params.in.min_msix_cnt = cdev->num_hwfns * 2;

	if (is_kdump_kernel()) {
		DP_INFO(cdev,
			"Kdump kernel: Limit the max number of requested MSI-X vectors to %hd\n",
			cdev->int_params.in.min_msix_cnt);
		cdev->int_params.in.num_vectors =
			cdev->int_params.in.min_msix_cnt;
	}

	rc = qed_set_int_mode(cdev, false);
	if (rc)  {
		DP_ERR(cdev, "%s ERR\n", __func__);
		return rc;
	}

	cdev->int_params.fp_msix_base = cdev->num_hwfns;
	cdev->int_params.fp_msix_cnt = cdev->int_params.out.num_vectors -
				       cdev->num_hwfns;

	if (!IS_ENABLED(CONFIG_QED_RDMA) ||
	    !QED_IS_RDMA_PERSONALITY(QED_LEADING_HWFN(cdev)))
		return 0;

	for_each_hwfn(cdev, i)
		num_l2_queues += FEAT_NUM(&cdev->hwfns[i], QED_PF_L2_QUE);

	DP_VERBOSE(cdev, QED_MSG_RDMA,
		   "cdev->int_params.fp_msix_cnt=%d num_l2_queues=%d\n",
		   cdev->int_params.fp_msix_cnt, num_l2_queues);

	if (cdev->int_params.fp_msix_cnt > num_l2_queues) {
		cdev->int_params.rdma_msix_cnt =
			(cdev->int_params.fp_msix_cnt - num_l2_queues)
			/ cdev->num_hwfns;
		cdev->int_params.rdma_msix_base =
			cdev->int_params.fp_msix_base + num_l2_queues;
		cdev->int_params.fp_msix_cnt = num_l2_queues;
	} else {
		cdev->int_params.rdma_msix_cnt = 0;
	}

	DP_VERBOSE(cdev, QED_MSG_RDMA, "roce_msix_cnt=%d roce_msix_base=%d\n",
		   cdev->int_params.rdma_msix_cnt,
		   cdev->int_params.rdma_msix_base);

	return 0;
}

static int qed_slowpath_vf_setup_int(struct qed_dev *cdev)
{
	int rc;

	memset(&cdev->int_params, 0, sizeof(struct qed_int_params));
	cdev->int_params.in.int_mode = QED_INT_MODE_MSIX;

	qed_vf_get_num_rxqs(QED_LEADING_HWFN(cdev),
			    &cdev->int_params.in.num_vectors);
	if (cdev->num_hwfns > 1) {
		u8 vectors = 0;

		qed_vf_get_num_rxqs(&cdev->hwfns[1], &vectors);
		cdev->int_params.in.num_vectors += vectors;
	}

	/* We want a minimum of one fastpath vector per vf hwfn */
	cdev->int_params.in.min_msix_cnt = cdev->num_hwfns;

	rc = qed_set_int_mode(cdev, true);
	if (rc)
		return rc;

	cdev->int_params.fp_msix_base = 0;
	cdev->int_params.fp_msix_cnt = cdev->int_params.out.num_vectors;

	return 0;
}

u32 qed_unzip_data(struct qed_hwfn *p_hwfn, u32 input_len,
		   u8 *input_buf, u32 max_size, u8 *unzip_buf)
{
	int rc;

	p_hwfn->stream->next_in = input_buf;
	p_hwfn->stream->avail_in = input_len;
	p_hwfn->stream->next_out = unzip_buf;
	p_hwfn->stream->avail_out = max_size;

	rc = zlib_inflateInit2(p_hwfn->stream, MAX_WBITS);

	if (rc != Z_OK) {
		DP_VERBOSE(p_hwfn, NETIF_MSG_DRV, "zlib init failed, rc = %d\n",
			   rc);
		return 0;
	}

	rc = zlib_inflate(p_hwfn->stream, Z_FINISH);
	zlib_inflateEnd(p_hwfn->stream);

	if (rc != Z_OK && rc != Z_STREAM_END) {
		DP_VERBOSE(p_hwfn, NETIF_MSG_DRV, "FW unzip error: %s, rc=%d\n",
			   p_hwfn->stream->msg, rc);
		return 0;
	}

	return p_hwfn->stream->total_out / 4;
}

static int qed_alloc_stream_mem(struct qed_dev *cdev)
{
	int i;
	void *workspace;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		p_hwfn->stream = kzalloc(sizeof(*p_hwfn->stream), GFP_KERNEL);
		if (!p_hwfn->stream)
			return -ENOMEM;

		workspace = vzalloc(zlib_inflate_workspacesize());
		if (!workspace)
			return -ENOMEM;
		p_hwfn->stream->workspace = workspace;
	}

	return 0;
}

static void qed_free_stream_mem(struct qed_dev *cdev)
{
	int i;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		if (!p_hwfn->stream)
			return;

		vfree(p_hwfn->stream->workspace);
		kfree(p_hwfn->stream);
	}
}

static void qed_update_pf_params(struct qed_dev *cdev,
				 struct qed_pf_params *params)
{
	int i;

	if (IS_ENABLED(CONFIG_QED_RDMA)) {
		params->rdma_pf_params.num_qps = QED_ROCE_QPS;
		params->rdma_pf_params.min_dpis = QED_ROCE_DPIS;
		params->rdma_pf_params.num_srqs = QED_RDMA_SRQS;
		/* divide by 3 the MRs to avoid MF ILT overflow */
		params->rdma_pf_params.gl_pi = QED_ROCE_PROTOCOL_INDEX;
	}

	if (cdev->num_hwfns > 1 || IS_VF(cdev))
		params->eth_pf_params.num_arfs_filters = 0;

	/* In case we might support RDMA, don't allow qede to be greedy
	 * with the L2 contexts. Allow for 64 queues [rx, tx cos, xdp]
	 * per hwfn.
	 */
	if (QED_IS_RDMA_PERSONALITY(QED_LEADING_HWFN(cdev))) {
		u16 *num_cons;

		num_cons = &params->eth_pf_params.num_cons;
		*num_cons = min_t(u16, *num_cons, QED_MAX_L2_CONS);
	}

	for (i = 0; i < cdev->num_hwfns; i++) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		p_hwfn->pf_params = *params;
	}
}

#define QED_PERIODIC_DB_REC_COUNT		10
#define QED_PERIODIC_DB_REC_INTERVAL_MS		100
#define QED_PERIODIC_DB_REC_INTERVAL \
	msecs_to_jiffies(QED_PERIODIC_DB_REC_INTERVAL_MS)

static int qed_slowpath_delayed_work(struct qed_hwfn *hwfn,
				     enum qed_slowpath_wq_flag wq_flag,
				     unsigned long delay)
{
	if (!hwfn->slowpath_wq_active)
		return -EINVAL;

	/* Memory barrier for setting atomic bit */
	smp_mb__before_atomic();
	set_bit(wq_flag, &hwfn->slowpath_task_flags);
	/* Memory barrier after setting atomic bit */
	smp_mb__after_atomic();
	queue_delayed_work(hwfn->slowpath_wq, &hwfn->slowpath_task, delay);

	return 0;
}

void qed_periodic_db_rec_start(struct qed_hwfn *p_hwfn)
{
	/* Reset periodic Doorbell Recovery counter */
	p_hwfn->periodic_db_rec_count = QED_PERIODIC_DB_REC_COUNT;

	/* Don't schedule periodic Doorbell Recovery if already scheduled */
	if (test_bit(QED_SLOWPATH_PERIODIC_DB_REC,
		     &p_hwfn->slowpath_task_flags))
		return;

	qed_slowpath_delayed_work(p_hwfn, QED_SLOWPATH_PERIODIC_DB_REC,
				  QED_PERIODIC_DB_REC_INTERVAL);
}

static void qed_slowpath_wq_stop(struct qed_dev *cdev)
{
	int i;

	if (IS_VF(cdev))
		return;

	for_each_hwfn(cdev, i) {
		if (!cdev->hwfns[i].slowpath_wq)
			continue;

		/* Stop queuing new delayed works */
		cdev->hwfns[i].slowpath_wq_active = false;

		cancel_delayed_work(&cdev->hwfns[i].slowpath_task);
		destroy_workqueue(cdev->hwfns[i].slowpath_wq);
	}
}

static void qed_slowpath_task(struct work_struct *work)
{
	struct qed_hwfn *hwfn = container_of(work, struct qed_hwfn,
					     slowpath_task.work);
	struct qed_ptt *ptt = qed_ptt_acquire(hwfn);

	if (!ptt) {
		if (hwfn->slowpath_wq_active)
			queue_delayed_work(hwfn->slowpath_wq,
					   &hwfn->slowpath_task, 0);

		return;
	}

	if (test_and_clear_bit(QED_SLOWPATH_MFW_TLV_REQ,
			       &hwfn->slowpath_task_flags))
		qed_mfw_process_tlv_req(hwfn, ptt);

	if (test_and_clear_bit(QED_SLOWPATH_PERIODIC_DB_REC,
			       &hwfn->slowpath_task_flags)) {
		/* skip qed_db_rec_handler during recovery/unload */
		if (hwfn->cdev->recov_in_prog || !hwfn->slowpath_wq_active)
			goto out;

		qed_db_rec_handler(hwfn, ptt);
		if (hwfn->periodic_db_rec_count--)
			qed_slowpath_delayed_work(hwfn,
						  QED_SLOWPATH_PERIODIC_DB_REC,
						  QED_PERIODIC_DB_REC_INTERVAL);
	}

out:
	qed_ptt_release(hwfn, ptt);
}

static int qed_slowpath_wq_start(struct qed_dev *cdev)
{
	struct qed_hwfn *hwfn;
	char name[NAME_SIZE];
	int i;

	if (IS_VF(cdev))
		return 0;

	for_each_hwfn(cdev, i) {
		hwfn = &cdev->hwfns[i];

		snprintf(name, NAME_SIZE, "slowpath-%02x:%02x.%02x",
			 cdev->pdev->bus->number,
			 PCI_SLOT(cdev->pdev->devfn), hwfn->abs_pf_id);

		hwfn->slowpath_wq = alloc_workqueue(name, 0, 0);
		if (!hwfn->slowpath_wq) {
			DP_NOTICE(hwfn, "Cannot create slowpath workqueue\n");
			return -ENOMEM;
		}

		INIT_DELAYED_WORK(&hwfn->slowpath_task, qed_slowpath_task);
		hwfn->slowpath_wq_active = true;
	}

	return 0;
}

static int qed_slowpath_start(struct qed_dev *cdev,
			      struct qed_slowpath_params *params)
{
	struct qed_drv_load_params drv_load_params;
	struct qed_hw_init_params hw_init_params;
	struct qed_mcp_drv_version drv_version;
	struct qed_tunnel_info tunn_info;
	const u8 *data = NULL;
	struct qed_hwfn *hwfn;
	struct qed_ptt *p_ptt;
	int rc = -EINVAL;

	if (qed_iov_wq_start(cdev))
		goto err;

	if (qed_slowpath_wq_start(cdev))
		goto err;

	if (IS_PF(cdev)) {
		rc = request_firmware(&cdev->firmware, QED_FW_FILE_NAME,
				      &cdev->pdev->dev);
		if (rc) {
			DP_NOTICE(cdev,
				  "Failed to find fw file - /lib/firmware/%s\n",
				  QED_FW_FILE_NAME);
			goto err;
		}

		if (cdev->num_hwfns == 1) {
			p_ptt = qed_ptt_acquire(QED_LEADING_HWFN(cdev));
			if (p_ptt) {
				QED_LEADING_HWFN(cdev)->p_arfs_ptt = p_ptt;
			} else {
				DP_NOTICE(cdev,
					  "Failed to acquire PTT for aRFS\n");
				rc = -EINVAL;
				goto err;
			}
		}
	}

	cdev->rx_coalesce_usecs = QED_DEFAULT_RX_USECS;
	rc = qed_nic_setup(cdev);
	if (rc)
		goto err;

	if (IS_PF(cdev))
		rc = qed_slowpath_setup_int(cdev, params->int_mode);
	else
		rc = qed_slowpath_vf_setup_int(cdev);
	if (rc)
		goto err1;

	if (IS_PF(cdev)) {
		/* Allocate stream for unzipping */
		rc = qed_alloc_stream_mem(cdev);
		if (rc)
			goto err2;

		/* First Dword used to differentiate between various sources */
		data = cdev->firmware->data + sizeof(u32);

		qed_dbg_pf_init(cdev);
	}

	/* Start the slowpath */
	memset(&hw_init_params, 0, sizeof(hw_init_params));
	memset(&tunn_info, 0, sizeof(tunn_info));
	tunn_info.vxlan.b_mode_enabled = true;
	tunn_info.l2_gre.b_mode_enabled = true;
	tunn_info.ip_gre.b_mode_enabled = true;
	tunn_info.l2_geneve.b_mode_enabled = true;
	tunn_info.ip_geneve.b_mode_enabled = true;
	tunn_info.vxlan.tun_cls = QED_TUNN_CLSS_MAC_VLAN;
	tunn_info.l2_gre.tun_cls = QED_TUNN_CLSS_MAC_VLAN;
	tunn_info.ip_gre.tun_cls = QED_TUNN_CLSS_MAC_VLAN;
	tunn_info.l2_geneve.tun_cls = QED_TUNN_CLSS_MAC_VLAN;
	tunn_info.ip_geneve.tun_cls = QED_TUNN_CLSS_MAC_VLAN;
	hw_init_params.p_tunn = &tunn_info;
	hw_init_params.b_hw_start = true;
	hw_init_params.int_mode = cdev->int_params.out.int_mode;
	hw_init_params.allow_npar_tx_switch = true;
	hw_init_params.bin_fw_data = data;

	memset(&drv_load_params, 0, sizeof(drv_load_params));
	drv_load_params.is_crash_kernel = is_kdump_kernel();
	drv_load_params.mfw_timeout_val = QED_LOAD_REQ_LOCK_TO_DEFAULT;
	drv_load_params.avoid_eng_reset = false;
	drv_load_params.override_force_load = QED_OVERRIDE_FORCE_LOAD_NONE;
	hw_init_params.p_drv_load_params = &drv_load_params;

	rc = qed_hw_init(cdev, &hw_init_params);
	if (rc)
		goto err2;

	DP_INFO(cdev,
		"HW initialization and function start completed successfully\n");

	if (IS_PF(cdev)) {
		cdev->tunn_feature_mask = (BIT(QED_MODE_VXLAN_TUNN) |
					   BIT(QED_MODE_L2GENEVE_TUNN) |
					   BIT(QED_MODE_IPGENEVE_TUNN) |
					   BIT(QED_MODE_L2GRE_TUNN) |
					   BIT(QED_MODE_IPGRE_TUNN));
	}

	/* Allocate LL2 interface if needed */
	if (QED_LEADING_HWFN(cdev)->using_ll2) {
		rc = qed_ll2_alloc_if(cdev);
		if (rc)
			goto err3;
	}
	if (IS_PF(cdev)) {
		hwfn = QED_LEADING_HWFN(cdev);
		drv_version.version = (params->drv_major << 24) |
				      (params->drv_minor << 16) |
				      (params->drv_rev << 8) |
				      (params->drv_eng);
		strscpy(drv_version.name, params->name,
			MCP_DRV_VER_STR_SIZE - 4);
		rc = qed_mcp_send_drv_version(hwfn, hwfn->p_main_ptt,
					      &drv_version);
		if (rc) {
			DP_NOTICE(cdev, "Failed sending drv version command\n");
			goto err4;
		}
	}

	qed_reset_vport_stats(cdev);

	return 0;

err4:
	qed_ll2_dealloc_if(cdev);
err3:
	qed_hw_stop(cdev);
err2:
	qed_hw_timers_stop_all(cdev);
	if (IS_PF(cdev))
		qed_slowpath_irq_free(cdev);
	qed_free_stream_mem(cdev);
	qed_disable_msix(cdev);
err1:
	qed_resc_free(cdev);
err:
	if (IS_PF(cdev))
		release_firmware(cdev->firmware);

	if (IS_PF(cdev) && (cdev->num_hwfns == 1) &&
	    QED_LEADING_HWFN(cdev)->p_arfs_ptt)
		qed_ptt_release(QED_LEADING_HWFN(cdev),
				QED_LEADING_HWFN(cdev)->p_arfs_ptt);

	qed_iov_wq_stop(cdev, false);

	qed_slowpath_wq_stop(cdev);

	return rc;
}

static int qed_slowpath_stop(struct qed_dev *cdev)
{
	if (!cdev)
		return -ENODEV;

	qed_slowpath_wq_stop(cdev);

	qed_ll2_dealloc_if(cdev);

	if (IS_PF(cdev)) {
		if (cdev->num_hwfns == 1)
			qed_ptt_release(QED_LEADING_HWFN(cdev),
					QED_LEADING_HWFN(cdev)->p_arfs_ptt);
		qed_free_stream_mem(cdev);
		if (IS_QED_ETH_IF(cdev))
			qed_sriov_disable(cdev, true);
	}

	qed_nic_stop(cdev);

	if (IS_PF(cdev))
		qed_slowpath_irq_free(cdev);

	qed_disable_msix(cdev);

	qed_resc_free(cdev);

	qed_iov_wq_stop(cdev, true);

	if (IS_PF(cdev))
		release_firmware(cdev->firmware);

	return 0;
}

static void qed_set_name(struct qed_dev *cdev, char name[NAME_SIZE])
{
	int i;

	memcpy(cdev->name, name, NAME_SIZE);
	for_each_hwfn(cdev, i)
		snprintf(cdev->hwfns[i].name, NAME_SIZE, "%s-%d", name, i);
}

static u32 qed_sb_init(struct qed_dev *cdev,
		       struct qed_sb_info *sb_info,
		       void *sb_virt_addr,
		       dma_addr_t sb_phy_addr, u16 sb_id,
		       enum qed_sb_type type)
{
	struct qed_hwfn *p_hwfn;
	struct qed_ptt *p_ptt;
	u16 rel_sb_id;
	u32 rc;

	/* RoCE/Storage use a single engine in CMT mode while L2 uses both */
	if (type == QED_SB_TYPE_L2_QUEUE) {
		p_hwfn = &cdev->hwfns[sb_id % cdev->num_hwfns];
		rel_sb_id = sb_id / cdev->num_hwfns;
	} else {
		p_hwfn = QED_AFFIN_HWFN(cdev);
		rel_sb_id = sb_id;
	}

	DP_VERBOSE(cdev, NETIF_MSG_INTR,
		   "hwfn [%d] <--[init]-- SB %04x [0x%04x upper]\n",
		   IS_LEAD_HWFN(p_hwfn) ? 0 : 1, rel_sb_id, sb_id);

	if (IS_PF(p_hwfn->cdev)) {
		p_ptt = qed_ptt_acquire(p_hwfn);
		if (!p_ptt)
			return -EBUSY;

		rc = qed_int_sb_init(p_hwfn, p_ptt, sb_info, sb_virt_addr,
				     sb_phy_addr, rel_sb_id);
		qed_ptt_release(p_hwfn, p_ptt);
	} else {
		rc = qed_int_sb_init(p_hwfn, NULL, sb_info, sb_virt_addr,
				     sb_phy_addr, rel_sb_id);
	}

	return rc;
}

static u32 qed_sb_release(struct qed_dev *cdev,
			  struct qed_sb_info *sb_info,
			  u16 sb_id,
			  enum qed_sb_type type)
{
	struct qed_hwfn *p_hwfn;
	u16 rel_sb_id;
	u32 rc;

	/* RoCE/Storage use a single engine in CMT mode while L2 uses both */
	if (type == QED_SB_TYPE_L2_QUEUE) {
		p_hwfn = &cdev->hwfns[sb_id % cdev->num_hwfns];
		rel_sb_id = sb_id / cdev->num_hwfns;
	} else {
		p_hwfn = QED_AFFIN_HWFN(cdev);
		rel_sb_id = sb_id;
	}

	DP_VERBOSE(cdev, NETIF_MSG_INTR,
		   "hwfn [%d] <--[init]-- SB %04x [0x%04x upper]\n",
		   IS_LEAD_HWFN(p_hwfn) ? 0 : 1, rel_sb_id, sb_id);

	rc = qed_int_sb_release(p_hwfn, sb_info, rel_sb_id);

	return rc;
}

static bool qed_can_link_change(struct qed_dev *cdev)
{
	return true;
}

static void qed_set_ext_speed_params(struct qed_mcp_link_params *link_params,
				     const struct qed_link_params *params)
{
	struct qed_mcp_link_speed_params *ext_speed = &link_params->ext_speed;
	const struct qed_mfw_speed_map *map;
	u32 i;

	if (params->override_flags & QED_LINK_OVERRIDE_SPEED_AUTONEG)
		ext_speed->autoneg = !!params->autoneg;

	if (params->override_flags & QED_LINK_OVERRIDE_SPEED_ADV_SPEEDS) {
		ext_speed->advertised_speeds = 0;

		for (i = 0; i < ARRAY_SIZE(qed_mfw_ext_maps); i++) {
			map = qed_mfw_ext_maps + i;

			if (linkmode_intersects(params->adv_speeds, map->caps))
				ext_speed->advertised_speeds |= map->mfw_val;
		}
	}

	if (params->override_flags & QED_LINK_OVERRIDE_SPEED_FORCED_SPEED) {
		switch (params->forced_speed) {
		case SPEED_1000:
			ext_speed->forced_speed = QED_EXT_SPEED_1G;
			break;
		case SPEED_10000:
			ext_speed->forced_speed = QED_EXT_SPEED_10G;
			break;
		case SPEED_20000:
			ext_speed->forced_speed = QED_EXT_SPEED_20G;
			break;
		case SPEED_25000:
			ext_speed->forced_speed = QED_EXT_SPEED_25G;
			break;
		case SPEED_40000:
			ext_speed->forced_speed = QED_EXT_SPEED_40G;
			break;
		case SPEED_50000:
			ext_speed->forced_speed = QED_EXT_SPEED_50G_R |
						  QED_EXT_SPEED_50G_R2;
			break;
		case SPEED_100000:
			ext_speed->forced_speed = QED_EXT_SPEED_100G_R2 |
						  QED_EXT_SPEED_100G_R4 |
						  QED_EXT_SPEED_100G_P4;
			break;
		default:
			break;
		}
	}

	if (!(params->override_flags & QED_LINK_OVERRIDE_FEC_CONFIG))
		return;

	switch (params->forced_speed) {
	case SPEED_25000:
		switch (params->fec) {
		case FEC_FORCE_MODE_NONE:
			link_params->ext_fec_mode = ETH_EXT_FEC_25G_NONE;
			break;
		case FEC_FORCE_MODE_FIRECODE:
			link_params->ext_fec_mode = ETH_EXT_FEC_25G_BASE_R;
			break;
		case FEC_FORCE_MODE_RS:
			link_params->ext_fec_mode = ETH_EXT_FEC_25G_RS528;
			break;
		case FEC_FORCE_MODE_AUTO:
			link_params->ext_fec_mode = ETH_EXT_FEC_25G_RS528 |
						    ETH_EXT_FEC_25G_BASE_R |
						    ETH_EXT_FEC_25G_NONE;
			break;
		default:
			break;
		}

		break;
	case SPEED_40000:
		switch (params->fec) {
		case FEC_FORCE_MODE_NONE:
			link_params->ext_fec_mode = ETH_EXT_FEC_40G_NONE;
			break;
		case FEC_FORCE_MODE_FIRECODE:
			link_params->ext_fec_mode = ETH_EXT_FEC_40G_BASE_R;
			break;
		case FEC_FORCE_MODE_AUTO:
			link_params->ext_fec_mode = ETH_EXT_FEC_40G_BASE_R |
						    ETH_EXT_FEC_40G_NONE;
			break;
		default:
			break;
		}

		break;
	case SPEED_50000:
		switch (params->fec) {
		case FEC_FORCE_MODE_NONE:
			link_params->ext_fec_mode = ETH_EXT_FEC_50G_NONE;
			break;
		case FEC_FORCE_MODE_FIRECODE:
			link_params->ext_fec_mode = ETH_EXT_FEC_50G_BASE_R;
			break;
		case FEC_FORCE_MODE_RS:
			link_params->ext_fec_mode = ETH_EXT_FEC_50G_RS528;
			break;
		case FEC_FORCE_MODE_AUTO:
			link_params->ext_fec_mode = ETH_EXT_FEC_50G_RS528 |
						    ETH_EXT_FEC_50G_BASE_R |
						    ETH_EXT_FEC_50G_NONE;
			break;
		default:
			break;
		}

		break;
	case SPEED_100000:
		switch (params->fec) {
		case FEC_FORCE_MODE_NONE:
			link_params->ext_fec_mode = ETH_EXT_FEC_100G_NONE;
			break;
		case FEC_FORCE_MODE_FIRECODE:
			link_params->ext_fec_mode = ETH_EXT_FEC_100G_BASE_R;
			break;
		case FEC_FORCE_MODE_RS:
			link_params->ext_fec_mode = ETH_EXT_FEC_100G_RS528;
			break;
		case FEC_FORCE_MODE_AUTO:
			link_params->ext_fec_mode = ETH_EXT_FEC_100G_RS528 |
						    ETH_EXT_FEC_100G_BASE_R |
						    ETH_EXT_FEC_100G_NONE;
			break;
		default:
			break;
		}

		break;
	default:
		break;
	}
}

static int qed_set_link(struct qed_dev *cdev, struct qed_link_params *params)
{
	struct qed_mcp_link_params *link_params;
	struct qed_mcp_link_speed_params *speed;
	const struct qed_mfw_speed_map *map;
	struct qed_hwfn *hwfn;
	struct qed_ptt *ptt;
	int rc;
	u32 i;

	if (!cdev)
		return -ENODEV;

	/* The link should be set only once per PF */
	hwfn = &cdev->hwfns[0];

	/* When VF wants to set link, force it to read the bulletin instead.
	 * This mimics the PF behavior, where a noitification [both immediate
	 * and possible later] would be generated when changing properties.
	 */
	if (IS_VF(cdev)) {
		qed_schedule_iov(hwfn, QED_IOV_WQ_VF_FORCE_LINK_QUERY_FLAG);
		return 0;
	}

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return -EBUSY;

	link_params = qed_mcp_get_link_params(hwfn);
	if (!link_params)
		return -ENODATA;

	speed = &link_params->speed;

	if (params->override_flags & QED_LINK_OVERRIDE_SPEED_AUTONEG)
		speed->autoneg = !!params->autoneg;

	if (params->override_flags & QED_LINK_OVERRIDE_SPEED_ADV_SPEEDS) {
		speed->advertised_speeds = 0;

		for (i = 0; i < ARRAY_SIZE(qed_mfw_legacy_maps); i++) {
			map = qed_mfw_legacy_maps + i;

			if (linkmode_intersects(params->adv_speeds, map->caps))
				speed->advertised_speeds |= map->mfw_val;
		}
	}

	if (params->override_flags & QED_LINK_OVERRIDE_SPEED_FORCED_SPEED)
		speed->forced_speed = params->forced_speed;

	if (qed_mcp_is_ext_speed_supported(hwfn))
		qed_set_ext_speed_params(link_params, params);

	if (params->override_flags & QED_LINK_OVERRIDE_PAUSE_CONFIG) {
		if (params->pause_config & QED_LINK_PAUSE_AUTONEG_ENABLE)
			link_params->pause.autoneg = true;
		else
			link_params->pause.autoneg = false;
		if (params->pause_config & QED_LINK_PAUSE_RX_ENABLE)
			link_params->pause.forced_rx = true;
		else
			link_params->pause.forced_rx = false;
		if (params->pause_config & QED_LINK_PAUSE_TX_ENABLE)
			link_params->pause.forced_tx = true;
		else
			link_params->pause.forced_tx = false;
	}

	if (params->override_flags & QED_LINK_OVERRIDE_LOOPBACK_MODE) {
		switch (params->loopback_mode) {
		case QED_LINK_LOOPBACK_INT_PHY:
			link_params->loopback_mode = ETH_LOOPBACK_INT_PHY;
			break;
		case QED_LINK_LOOPBACK_EXT_PHY:
			link_params->loopback_mode = ETH_LOOPBACK_EXT_PHY;
			break;
		case QED_LINK_LOOPBACK_EXT:
			link_params->loopback_mode = ETH_LOOPBACK_EXT;
			break;
		case QED_LINK_LOOPBACK_MAC:
			link_params->loopback_mode = ETH_LOOPBACK_MAC;
			break;
		case QED_LINK_LOOPBACK_CNIG_AH_ONLY_0123:
			link_params->loopback_mode =
				ETH_LOOPBACK_CNIG_AH_ONLY_0123;
			break;
		case QED_LINK_LOOPBACK_CNIG_AH_ONLY_2301:
			link_params->loopback_mode =
				ETH_LOOPBACK_CNIG_AH_ONLY_2301;
			break;
		case QED_LINK_LOOPBACK_PCS_AH_ONLY:
			link_params->loopback_mode = ETH_LOOPBACK_PCS_AH_ONLY;
			break;
		case QED_LINK_LOOPBACK_REVERSE_MAC_AH_ONLY:
			link_params->loopback_mode =
				ETH_LOOPBACK_REVERSE_MAC_AH_ONLY;
			break;
		case QED_LINK_LOOPBACK_INT_PHY_FEA_AH_ONLY:
			link_params->loopback_mode =
				ETH_LOOPBACK_INT_PHY_FEA_AH_ONLY;
			break;
		default:
			link_params->loopback_mode = ETH_LOOPBACK_NONE;
			break;
		}
	}

	if (params->override_flags & QED_LINK_OVERRIDE_EEE_CONFIG)
		memcpy(&link_params->eee, &params->eee,
		       sizeof(link_params->eee));

	if (params->override_flags & QED_LINK_OVERRIDE_FEC_CONFIG)
		link_params->fec = params->fec;

	rc = qed_mcp_set_link(hwfn, ptt, params->link_up);

	qed_ptt_release(hwfn, ptt);

	return rc;
}

static int qed_get_port_type(u32 media_type)
{
	int port_type;

	switch (media_type) {
	case MEDIA_SFPP_10G_FIBER:
	case MEDIA_SFP_1G_FIBER:
	case MEDIA_XFP_FIBER:
	case MEDIA_MODULE_FIBER:
		port_type = PORT_FIBRE;
		break;
	case MEDIA_DA_TWINAX:
		port_type = PORT_DA;
		break;
	case MEDIA_BASE_T:
		port_type = PORT_TP;
		break;
	case MEDIA_KR:
	case MEDIA_NOT_PRESENT:
		port_type = PORT_NONE;
		break;
	case MEDIA_UNSPECIFIED:
	default:
		port_type = PORT_OTHER;
		break;
	}
	return port_type;
}

static int qed_get_link_data(struct qed_hwfn *hwfn,
			     struct qed_mcp_link_params *params,
			     struct qed_mcp_link_state *link,
			     struct qed_mcp_link_capabilities *link_caps)
{
	void *p;

	if (!IS_PF(hwfn->cdev)) {
		qed_vf_get_link_params(hwfn, params);
		qed_vf_get_link_state(hwfn, link);
		qed_vf_get_link_caps(hwfn, link_caps);

		return 0;
	}

	p = qed_mcp_get_link_params(hwfn);
	if (!p)
		return -ENXIO;
	memcpy(params, p, sizeof(*params));

	p = qed_mcp_get_link_state(hwfn);
	if (!p)
		return -ENXIO;
	memcpy(link, p, sizeof(*link));

	p = qed_mcp_get_link_capabilities(hwfn);
	if (!p)
		return -ENXIO;
	memcpy(link_caps, p, sizeof(*link_caps));

	return 0;
}

static void qed_fill_link_capability(struct qed_hwfn *hwfn,
				     struct qed_ptt *ptt, u32 capability,
				     unsigned long *if_caps)
{
	u32 media_type, tcvr_state, tcvr_type;
	u32 speed_mask, board_cfg;

	if (qed_mcp_get_media_type(hwfn, ptt, &media_type))
		media_type = MEDIA_UNSPECIFIED;

	if (qed_mcp_get_transceiver_data(hwfn, ptt, &tcvr_state, &tcvr_type))
		tcvr_type = ETH_TRANSCEIVER_STATE_UNPLUGGED;

	if (qed_mcp_trans_speed_mask(hwfn, ptt, &speed_mask))
		speed_mask = 0xFFFFFFFF;

	if (qed_mcp_get_board_config(hwfn, ptt, &board_cfg))
		board_cfg = NVM_CFG1_PORT_PORT_TYPE_UNDEFINED;

	DP_VERBOSE(hwfn->cdev, NETIF_MSG_DRV,
		   "Media_type = 0x%x tcvr_state = 0x%x tcvr_type = 0x%x speed_mask = 0x%x board_cfg = 0x%x\n",
		   media_type, tcvr_state, tcvr_type, speed_mask, board_cfg);

	switch (media_type) {
	case MEDIA_DA_TWINAX:
		phylink_set(if_caps, FIBRE);

		if (capability & NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_20G)
			phylink_set(if_caps, 20000baseKR2_Full);

		/* For DAC media multiple speed capabilities are supported */
		capability |= speed_mask;

		if (capability & NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_1G)
			phylink_set(if_caps, 1000baseKX_Full);
		if (capability & NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_10G)
			phylink_set(if_caps, 10000baseCR_Full);

		if (capability & NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_40G)
			switch (tcvr_type) {
			case ETH_TRANSCEIVER_TYPE_40G_CR4:
			case ETH_TRANSCEIVER_TYPE_MULTI_RATE_10G_40G_CR:
			case ETH_TRANSCEIVER_TYPE_MULTI_RATE_40G_100G_CR:
				phylink_set(if_caps, 40000baseCR4_Full);
				break;
			default:
				break;
			}

		if (capability & NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_25G)
			phylink_set(if_caps, 25000baseCR_Full);
		if (capability & NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_50G)
			phylink_set(if_caps, 50000baseCR2_Full);

		if (capability &
		    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_BB_100G)
			switch (tcvr_type) {
			case ETH_TRANSCEIVER_TYPE_100G_CR4:
			case ETH_TRANSCEIVER_TYPE_MULTI_RATE_40G_100G_CR:
				phylink_set(if_caps, 100000baseCR4_Full);
				break;
			default:
				break;
			}

		break;
	case MEDIA_BASE_T:
		phylink_set(if_caps, TP);

		if (board_cfg & NVM_CFG1_PORT_PORT_TYPE_EXT_PHY) {
			if (capability &
			    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_1G)
				phylink_set(if_caps, 1000baseT_Full);
			if (capability &
			    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_10G)
				phylink_set(if_caps, 10000baseT_Full);
		}

		if (board_cfg & NVM_CFG1_PORT_PORT_TYPE_MODULE) {
			phylink_set(if_caps, FIBRE);

			switch (tcvr_type) {
			case ETH_TRANSCEIVER_TYPE_1000BASET:
				phylink_set(if_caps, 1000baseT_Full);
				break;
			case ETH_TRANSCEIVER_TYPE_10G_BASET:
				phylink_set(if_caps, 10000baseT_Full);
				break;
			default:
				break;
			}
		}

		break;
	case MEDIA_SFP_1G_FIBER:
	case MEDIA_SFPP_10G_FIBER:
	case MEDIA_XFP_FIBER:
	case MEDIA_MODULE_FIBER:
		phylink_set(if_caps, FIBRE);
		capability |= speed_mask;

		if (capability & NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_1G)
			switch (tcvr_type) {
			case ETH_TRANSCEIVER_TYPE_1G_LX:
			case ETH_TRANSCEIVER_TYPE_1G_SX:
			case ETH_TRANSCEIVER_TYPE_MULTI_RATE_1G_10G_SR:
			case ETH_TRANSCEIVER_TYPE_MULTI_RATE_1G_10G_LR:
				phylink_set(if_caps, 1000baseKX_Full);
				break;
			default:
				break;
			}

		if (capability & NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_10G)
			switch (tcvr_type) {
			case ETH_TRANSCEIVER_TYPE_10G_SR:
			case ETH_TRANSCEIVER_TYPE_MULTI_RATE_10G_40G_SR:
			case ETH_TRANSCEIVER_TYPE_MULTI_RATE_10G_25G_SR:
			case ETH_TRANSCEIVER_TYPE_MULTI_RATE_1G_10G_SR:
				phylink_set(if_caps, 10000baseSR_Full);
				break;
			case ETH_TRANSCEIVER_TYPE_10G_LR:
			case ETH_TRANSCEIVER_TYPE_MULTI_RATE_10G_40G_LR:
			case ETH_TRANSCEIVER_TYPE_MULTI_RATE_10G_25G_LR:
			case ETH_TRANSCEIVER_TYPE_MULTI_RATE_1G_10G_LR:
				phylink_set(if_caps, 10000baseLR_Full);
				break;
			case ETH_TRANSCEIVER_TYPE_10G_LRM:
				phylink_set(if_caps, 10000baseLRM_Full);
				break;
			case ETH_TRANSCEIVER_TYPE_10G_ER:
				phylink_set(if_caps, 10000baseR_FEC);
				break;
			default:
				break;
			}

		if (capability & NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_20G)
			phylink_set(if_caps, 20000baseKR2_Full);

		if (capability & NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_25G)
			switch (tcvr_type) {
			case ETH_TRANSCEIVER_TYPE_25G_SR:
			case ETH_TRANSCEIVER_TYPE_MULTI_RATE_10G_25G_SR:
				phylink_set(if_caps, 25000baseSR_Full);
				break;
			default:
				break;
			}

		if (capability & NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_40G)
			switch (tcvr_type) {
			case ETH_TRANSCEIVER_TYPE_40G_LR4:
			case ETH_TRANSCEIVER_TYPE_MULTI_RATE_10G_40G_LR:
			case ETH_TRANSCEIVER_TYPE_MULTI_RATE_40G_100G_LR:
				phylink_set(if_caps, 40000baseLR4_Full);
				break;
			case ETH_TRANSCEIVER_TYPE_40G_SR4:
			case ETH_TRANSCEIVER_TYPE_MULTI_RATE_40G_100G_SR:
			case ETH_TRANSCEIVER_TYPE_MULTI_RATE_10G_40G_SR:
				phylink_set(if_caps, 40000baseSR4_Full);
				break;
			default:
				break;
			}

		if (capability & NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_50G)
			phylink_set(if_caps, 50000baseKR2_Full);

		if (capability &
		    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_BB_100G)
			switch (tcvr_type) {
			case ETH_TRANSCEIVER_TYPE_100G_SR4:
			case ETH_TRANSCEIVER_TYPE_MULTI_RATE_40G_100G_SR:
				phylink_set(if_caps, 100000baseSR4_Full);
				break;
			case ETH_TRANSCEIVER_TYPE_MULTI_RATE_40G_100G_LR:
				phylink_set(if_caps, 100000baseLR4_ER4_Full);
				break;
			default:
				break;
			}

		break;
	case MEDIA_KR:
		phylink_set(if_caps, Backplane);

		if (capability & NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_20G)
			phylink_set(if_caps, 20000baseKR2_Full);
		if (capability & NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_1G)
			phylink_set(if_caps, 1000baseKX_Full);
		if (capability & NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_10G)
			phylink_set(if_caps, 10000baseKR_Full);
		if (capability & NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_25G)
			phylink_set(if_caps, 25000baseKR_Full);
		if (capability & NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_40G)
			phylink_set(if_caps, 40000baseKR4_Full);
		if (capability & NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_50G)
			phylink_set(if_caps, 50000baseKR2_Full);
		if (capability &
		    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_BB_100G)
			phylink_set(if_caps, 100000baseKR4_Full);

		break;
	case MEDIA_UNSPECIFIED:
	case MEDIA_NOT_PRESENT:
	default:
		DP_VERBOSE(hwfn->cdev, QED_MSG_DEBUG,
			   "Unknown media and transceiver type;\n");
		break;
	}
}

static void qed_lp_caps_to_speed_mask(u32 caps, u32 *speed_mask)
{
	*speed_mask = 0;

	if (caps &
	    (QED_LINK_PARTNER_SPEED_1G_FD | QED_LINK_PARTNER_SPEED_1G_HD))
		*speed_mask |= NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_1G;
	if (caps & QED_LINK_PARTNER_SPEED_10G)
		*speed_mask |= NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_10G;
	if (caps & QED_LINK_PARTNER_SPEED_20G)
		*speed_mask |= NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_20G;
	if (caps & QED_LINK_PARTNER_SPEED_25G)
		*speed_mask |= NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_25G;
	if (caps & QED_LINK_PARTNER_SPEED_40G)
		*speed_mask |= NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_40G;
	if (caps & QED_LINK_PARTNER_SPEED_50G)
		*speed_mask |= NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_50G;
	if (caps & QED_LINK_PARTNER_SPEED_100G)
		*speed_mask |= NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_BB_100G;
}

static void qed_fill_link(struct qed_hwfn *hwfn,
			  struct qed_ptt *ptt,
			  struct qed_link_output *if_link)
{
	struct qed_mcp_link_capabilities link_caps;
	struct qed_mcp_link_params params;
	struct qed_mcp_link_state link;
	u32 media_type, speed_mask;

	memset(if_link, 0, sizeof(*if_link));

	/* Prepare source inputs */
	if (qed_get_link_data(hwfn, &params, &link, &link_caps)) {
		dev_warn(&hwfn->cdev->pdev->dev, "no link data available\n");
		return;
	}

	/* Set the link parameters to pass to protocol driver */
	if (link.link_up)
		if_link->link_up = true;

	if (IS_PF(hwfn->cdev) && qed_mcp_is_ext_speed_supported(hwfn)) {
		if (link_caps.default_ext_autoneg)
			phylink_set(if_link->supported_caps, Autoneg);

		linkmode_copy(if_link->advertised_caps, if_link->supported_caps);

		if (params.ext_speed.autoneg)
			phylink_set(if_link->advertised_caps, Autoneg);
		else
			phylink_clear(if_link->advertised_caps, Autoneg);

		qed_fill_link_capability(hwfn, ptt,
					 params.ext_speed.advertised_speeds,
					 if_link->advertised_caps);
	} else {
		if (link_caps.default_speed_autoneg)
			phylink_set(if_link->supported_caps, Autoneg);

		linkmode_copy(if_link->advertised_caps, if_link->supported_caps);

		if (params.speed.autoneg)
			phylink_set(if_link->advertised_caps, Autoneg);
		else
			phylink_clear(if_link->advertised_caps, Autoneg);
	}

	if (params.pause.autoneg ||
	    (params.pause.forced_rx && params.pause.forced_tx))
		phylink_set(if_link->supported_caps, Asym_Pause);
	if (params.pause.autoneg || params.pause.forced_rx ||
	    params.pause.forced_tx)
		phylink_set(if_link->supported_caps, Pause);

	if_link->sup_fec = link_caps.fec_default;
	if_link->active_fec = params.fec;

	/* Fill link advertised capability */
	qed_fill_link_capability(hwfn, ptt, params.speed.advertised_speeds,
				 if_link->advertised_caps);

	/* Fill link supported capability */
	qed_fill_link_capability(hwfn, ptt, link_caps.speed_capabilities,
				 if_link->supported_caps);

	/* Fill partner advertised capability */
	qed_lp_caps_to_speed_mask(link.partner_adv_speed, &speed_mask);
	qed_fill_link_capability(hwfn, ptt, speed_mask, if_link->lp_caps);

	if (link.link_up)
		if_link->speed = link.speed;

	/* TODO - fill duplex properly */
	if_link->duplex = DUPLEX_FULL;
	qed_mcp_get_media_type(hwfn, ptt, &media_type);
	if_link->port = qed_get_port_type(media_type);

	if_link->autoneg = params.speed.autoneg;

	if (params.pause.autoneg)
		if_link->pause_config |= QED_LINK_PAUSE_AUTONEG_ENABLE;
	if (params.pause.forced_rx)
		if_link->pause_config |= QED_LINK_PAUSE_RX_ENABLE;
	if (params.pause.forced_tx)
		if_link->pause_config |= QED_LINK_PAUSE_TX_ENABLE;

	if (link.an_complete)
		phylink_set(if_link->lp_caps, Autoneg);
	if (link.partner_adv_pause)
		phylink_set(if_link->lp_caps, Pause);
	if (link.partner_adv_pause == QED_LINK_PARTNER_ASYMMETRIC_PAUSE ||
	    link.partner_adv_pause == QED_LINK_PARTNER_BOTH_PAUSE)
		phylink_set(if_link->lp_caps, Asym_Pause);

	if (link_caps.default_eee == QED_MCP_EEE_UNSUPPORTED) {
		if_link->eee_supported = false;
	} else {
		if_link->eee_supported = true;
		if_link->eee_active = link.eee_active;
		if_link->sup_caps = link_caps.eee_speed_caps;
		/* MFW clears adv_caps on eee disable; use configured value */
		if_link->eee.adv_caps = link.eee_adv_caps ? link.eee_adv_caps :
					params.eee.adv_caps;
		if_link->eee.lp_adv_caps = link.eee_lp_adv_caps;
		if_link->eee.enable = params.eee.enable;
		if_link->eee.tx_lpi_enable = params.eee.tx_lpi_enable;
		if_link->eee.tx_lpi_timer = params.eee.tx_lpi_timer;
	}
}

static void qed_get_current_link(struct qed_dev *cdev,
				 struct qed_link_output *if_link)
{
	struct qed_hwfn *hwfn;
	struct qed_ptt *ptt;
	int i;

	hwfn = &cdev->hwfns[0];
	if (IS_PF(cdev)) {
		ptt = qed_ptt_acquire(hwfn);
		if (ptt) {
			qed_fill_link(hwfn, ptt, if_link);
			qed_ptt_release(hwfn, ptt);
		} else {
			DP_NOTICE(hwfn, "Failed to fill link; No PTT\n");
		}
	} else {
		qed_fill_link(hwfn, NULL, if_link);
	}

	for_each_hwfn(cdev, i)
		qed_inform_vf_link_state(&cdev->hwfns[i]);
}

void qed_link_update(struct qed_hwfn *hwfn, struct qed_ptt *ptt)
{
	void *cookie = hwfn->cdev->ops_cookie;
	struct qed_common_cb_ops *op = hwfn->cdev->protocol_ops.common;
	struct qed_link_output if_link;

	qed_fill_link(hwfn, ptt, &if_link);
	qed_inform_vf_link_state(hwfn);

	if (IS_LEAD_HWFN(hwfn) && cookie)
		op->link_update(cookie, &if_link);
}

void qed_bw_update(struct qed_hwfn *hwfn, struct qed_ptt *ptt)
{
	void *cookie = hwfn->cdev->ops_cookie;
	struct qed_common_cb_ops *op = hwfn->cdev->protocol_ops.common;

	if (IS_LEAD_HWFN(hwfn) && cookie && op && op->bw_update)
		op->bw_update(cookie);
}

static int qed_drain(struct qed_dev *cdev)
{
	struct qed_hwfn *hwfn;
	struct qed_ptt *ptt;
	int i, rc;

	if (IS_VF(cdev))
		return 0;

	for_each_hwfn(cdev, i) {
		hwfn = &cdev->hwfns[i];
		ptt = qed_ptt_acquire(hwfn);
		if (!ptt) {
			DP_NOTICE(hwfn, "Failed to drain NIG; No PTT\n");
			return -EBUSY;
		}
		rc = qed_mcp_drain(hwfn, ptt);
		qed_ptt_release(hwfn, ptt);
		if (rc)
			return rc;
	}

	return 0;
}

static u32 qed_nvm_flash_image_access_crc(struct qed_dev *cdev,
					  struct qed_nvm_image_att *nvm_image,
					  u32 *crc)
{
	u8 *buf = NULL;
	int rc;

	/* Allocate a buffer for holding the nvram image */
	buf = kzalloc(nvm_image->length, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* Read image into buffer */
	rc = qed_mcp_nvm_read(cdev, nvm_image->start_addr,
			      buf, nvm_image->length);
	if (rc) {
		DP_ERR(cdev, "Failed reading image from nvm\n");
		goto out;
	}

	/* Convert the buffer into big-endian format (excluding the
	 * closing 4 bytes of CRC).
	 */
	cpu_to_be32_array((__force __be32 *)buf, (const u32 *)buf,
			  DIV_ROUND_UP(nvm_image->length - 4, 4));

	/* Calc CRC for the "actual" image buffer, i.e. not including
	 * the last 4 CRC bytes.
	 */
	*crc = ~crc32(~0U, buf, nvm_image->length - 4);
	*crc = (__force u32)cpu_to_be32p(crc);

out:
	kfree(buf);

	return rc;
}

/* Binary file format -
 *     /----------------------------------------------------------------------\
 * 0B  |                       0x4 [command index]                            |
 * 4B  | image_type     | Options        |  Number of register settings       |
 * 8B  |                       Value                                          |
 * 12B |                       Mask                                           |
 * 16B |                       Offset                                         |
 *     \----------------------------------------------------------------------/
 * There can be several Value-Mask-Offset sets as specified by 'Number of...'.
 * Options - 0'b - Calculate & Update CRC for image
 */
static int qed_nvm_flash_image_access(struct qed_dev *cdev, const u8 **data,
				      bool *check_resp)
{
	struct qed_nvm_image_att nvm_image;
	struct qed_hwfn *p_hwfn;
	bool is_crc = false;
	u32 image_type;
	int rc = 0, i;
	u16 len;

	*data += 4;
	image_type = **data;
	p_hwfn = QED_LEADING_HWFN(cdev);
	for (i = 0; i < p_hwfn->nvm_info.num_images; i++)
		if (image_type == p_hwfn->nvm_info.image_att[i].image_type)
			break;
	if (i == p_hwfn->nvm_info.num_images) {
		DP_ERR(cdev, "Failed to find nvram image of type %08x\n",
		       image_type);
		return -ENOENT;
	}

	nvm_image.start_addr = p_hwfn->nvm_info.image_att[i].nvm_start_addr;
	nvm_image.length = p_hwfn->nvm_info.image_att[i].len;

	DP_VERBOSE(cdev, NETIF_MSG_DRV,
		   "Read image %02x; type = %08x; NVM [%08x,...,%08x]\n",
		   **data, image_type, nvm_image.start_addr,
		   nvm_image.start_addr + nvm_image.length - 1);
	(*data)++;
	is_crc = !!(**data & BIT(0));
	(*data)++;
	len = *((u16 *)*data);
	*data += 2;
	if (is_crc) {
		u32 crc = 0;

		rc = qed_nvm_flash_image_access_crc(cdev, &nvm_image, &crc);
		if (rc) {
			DP_ERR(cdev, "Failed calculating CRC, rc = %d\n", rc);
			goto exit;
		}

		rc = qed_mcp_nvm_write(cdev, QED_NVM_WRITE_NVRAM,
				       (nvm_image.start_addr +
					nvm_image.length - 4), (u8 *)&crc, 4);
		if (rc)
			DP_ERR(cdev, "Failed writing to %08x, rc = %d\n",
			       nvm_image.start_addr + nvm_image.length - 4, rc);
		goto exit;
	}

	/* Iterate over the values for setting */
	while (len) {
		u32 offset, mask, value, cur_value;
		u8 buf[4];

		value = *((u32 *)*data);
		*data += 4;
		mask = *((u32 *)*data);
		*data += 4;
		offset = *((u32 *)*data);
		*data += 4;

		rc = qed_mcp_nvm_read(cdev, nvm_image.start_addr + offset, buf,
				      4);
		if (rc) {
			DP_ERR(cdev, "Failed reading from %08x\n",
			       nvm_image.start_addr + offset);
			goto exit;
		}

		cur_value = le32_to_cpu(*((__le32 *)buf));
		DP_VERBOSE(cdev, NETIF_MSG_DRV,
			   "NVM %08x: %08x -> %08x [Value %08x Mask %08x]\n",
			   nvm_image.start_addr + offset, cur_value,
			   (cur_value & ~mask) | (value & mask), value, mask);
		value = (value & mask) | (cur_value & ~mask);
		rc = qed_mcp_nvm_write(cdev, QED_NVM_WRITE_NVRAM,
				       nvm_image.start_addr + offset,
				       (u8 *)&value, 4);
		if (rc) {
			DP_ERR(cdev, "Failed writing to %08x\n",
			       nvm_image.start_addr + offset);
			goto exit;
		}

		len--;
	}
exit:
	return rc;
}

/* Binary file format -
 *     /----------------------------------------------------------------------\
 * 0B  |                       0x3 [command index]                            |
 * 4B  | b'0: check_response?   | b'1-31  reserved                            |
 * 8B  | File-type |                   reserved                               |
 * 12B |                    Image length in bytes                             |
 *     \----------------------------------------------------------------------/
 *     Start a new file of the provided type
 */
static int qed_nvm_flash_image_file_start(struct qed_dev *cdev,
					  const u8 **data, bool *check_resp)
{
	u32 file_type, file_size = 0;
	int rc;

	*data += 4;
	*check_resp = !!(**data & BIT(0));
	*data += 4;
	file_type = **data;

	DP_VERBOSE(cdev, NETIF_MSG_DRV,
		   "About to start a new file of type %02x\n", file_type);
	if (file_type == DRV_MB_PARAM_NVM_PUT_FILE_BEGIN_MBI) {
		*data += 4;
		file_size = *((u32 *)(*data));
	}

	rc = qed_mcp_nvm_write(cdev, QED_PUT_FILE_BEGIN, file_type,
			       (u8 *)(&file_size), 4);
	*data += 4;

	return rc;
}

/* Binary file format -
 *     /----------------------------------------------------------------------\
 * 0B  |                       0x2 [command index]                            |
 * 4B  |                       Length in bytes                                |
 * 8B  | b'0: check_response?   | b'1-31  reserved                            |
 * 12B |                       Offset in bytes                                |
 * 16B |                       Data ...                                       |
 *     \----------------------------------------------------------------------/
 *     Write data as part of a file that was previously started. Data should be
 *     of length equal to that provided in the message
 */
static int qed_nvm_flash_image_file_data(struct qed_dev *cdev,
					 const u8 **data, bool *check_resp)
{
	u32 offset, len;
	int rc;

	*data += 4;
	len = *((u32 *)(*data));
	*data += 4;
	*check_resp = !!(**data & BIT(0));
	*data += 4;
	offset = *((u32 *)(*data));
	*data += 4;

	DP_VERBOSE(cdev, NETIF_MSG_DRV,
		   "About to write File-data: %08x bytes to offset %08x\n",
		   len, offset);

	rc = qed_mcp_nvm_write(cdev, QED_PUT_FILE_DATA, offset,
			       (char *)(*data), len);
	*data += len;

	return rc;
}

/* Binary file format [General header] -
 *     /----------------------------------------------------------------------\
 * 0B  |                       QED_NVM_SIGNATURE                              |
 * 4B  |                       Length in bytes                                |
 * 8B  | Highest command in this batchfile |          Reserved                |
 *     \----------------------------------------------------------------------/
 */
static int qed_nvm_flash_image_validate(struct qed_dev *cdev,
					const struct firmware *image,
					const u8 **data)
{
	u32 signature, len;

	/* Check minimum size */
	if (image->size < 12) {
		DP_ERR(cdev, "Image is too short [%08x]\n", (u32)image->size);
		return -EINVAL;
	}

	/* Check signature */
	signature = *((u32 *)(*data));
	if (signature != QED_NVM_SIGNATURE) {
		DP_ERR(cdev, "Wrong signature '%08x'\n", signature);
		return -EINVAL;
	}

	*data += 4;
	/* Validate internal size equals the image-size */
	len = *((u32 *)(*data));
	if (len != image->size) {
		DP_ERR(cdev, "Size mismatch: internal = %08x image = %08x\n",
		       len, (u32)image->size);
		return -EINVAL;
	}

	*data += 4;
	/* Make sure driver familiar with all commands necessary for this */
	if (*((u16 *)(*data)) >= QED_NVM_FLASH_CMD_NVM_MAX) {
		DP_ERR(cdev, "File contains unsupported commands [Need %04x]\n",
		       *((u16 *)(*data)));
		return -EINVAL;
	}

	*data += 4;

	return 0;
}

/* Binary file format -
 *     /----------------------------------------------------------------------\
 * 0B  |                       0x5 [command index]                            |
 * 4B  | Number of config attributes     |          Reserved                  |
 * 4B  | Config ID                       | Entity ID      | Length            |
 * 4B  | Value                                                                |
 *     |                                                                      |
 *     \----------------------------------------------------------------------/
 * There can be several cfg_id-entity_id-Length-Value sets as specified by
 * 'Number of config attributes'.
 *
 * The API parses config attributes from the user provided buffer and flashes
 * them to the respective NVM path using Management FW inerface.
 */
static int qed_nvm_flash_cfg_write(struct qed_dev *cdev, const u8 **data)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	u8 entity_id, len, buf[32];
	bool need_nvm_init = true;
	struct qed_ptt *ptt;
	u16 cfg_id, count;
	int rc = 0, i;
	u32 flags;

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return -EAGAIN;

	/* NVM CFG ID attribute header */
	*data += 4;
	count = *((u16 *)*data);
	*data += 4;

	DP_VERBOSE(cdev, NETIF_MSG_DRV,
		   "Read config ids: num_attrs = %0d\n", count);
	/* NVM CFG ID attributes. Start loop index from 1 to avoid additional
	 * arithmetic operations in the implementation.
	 */
	for (i = 1; i <= count; i++) {
		cfg_id = *((u16 *)*data);
		*data += 2;
		entity_id = **data;
		(*data)++;
		len = **data;
		(*data)++;
		memcpy(buf, *data, len);
		*data += len;

		flags = 0;
		if (need_nvm_init) {
			flags |= QED_NVM_CFG_OPTION_INIT;
			need_nvm_init = false;
		}

		/* Commit to flash and free the resources */
		if (!(i % QED_NVM_CFG_MAX_ATTRS) || i == count) {
			flags |= QED_NVM_CFG_OPTION_COMMIT |
				 QED_NVM_CFG_OPTION_FREE;
			need_nvm_init = true;
		}

		if (entity_id)
			flags |= QED_NVM_CFG_OPTION_ENTITY_SEL;

		DP_VERBOSE(cdev, NETIF_MSG_DRV,
			   "cfg_id = %d entity = %d len = %d\n", cfg_id,
			   entity_id, len);
		rc = qed_mcp_nvm_set_cfg(hwfn, ptt, cfg_id, entity_id, flags,
					 buf, len);
		if (rc) {
			DP_ERR(cdev, "Error %d configuring %d\n", rc, cfg_id);
			break;
		}
	}

	qed_ptt_release(hwfn, ptt);

	return rc;
}

#define QED_MAX_NVM_BUF_LEN	32
static int qed_nvm_flash_cfg_len(struct qed_dev *cdev, u32 cmd)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	u8 buf[QED_MAX_NVM_BUF_LEN];
	struct qed_ptt *ptt;
	u32 len;
	int rc;

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return QED_MAX_NVM_BUF_LEN;

	rc = qed_mcp_nvm_get_cfg(hwfn, ptt, cmd, 0, QED_NVM_CFG_GET_FLAGS, buf,
				 &len);
	if (rc || !len) {
		DP_ERR(cdev, "Error %d reading %d\n", rc, cmd);
		len = QED_MAX_NVM_BUF_LEN;
	}

	qed_ptt_release(hwfn, ptt);

	return len;
}

static int qed_nvm_flash_cfg_read(struct qed_dev *cdev, u8 **data,
				  u32 cmd, u32 entity_id)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_ptt *ptt;
	u32 flags, len;
	int rc = 0;

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return -EAGAIN;

	DP_VERBOSE(cdev, NETIF_MSG_DRV,
		   "Read config cmd = %d entity id %d\n", cmd, entity_id);
	flags = entity_id ? QED_NVM_CFG_GET_PF_FLAGS : QED_NVM_CFG_GET_FLAGS;
	rc = qed_mcp_nvm_get_cfg(hwfn, ptt, cmd, entity_id, flags, *data, &len);
	if (rc)
		DP_ERR(cdev, "Error %d reading %d\n", rc, cmd);

	qed_ptt_release(hwfn, ptt);

	return rc;
}

static int qed_nvm_flash(struct qed_dev *cdev, const char *name)
{
	const struct firmware *image;
	const u8 *data, *data_end;
	u32 cmd_type;
	int rc;

	rc = request_firmware(&image, name, &cdev->pdev->dev);
	if (rc) {
		DP_ERR(cdev, "Failed to find '%s'\n", name);
		return rc;
	}

	DP_VERBOSE(cdev, NETIF_MSG_DRV,
		   "Flashing '%s' - firmware's data at %p, size is %08x\n",
		   name, image->data, (u32)image->size);
	data = image->data;
	data_end = data + image->size;

	rc = qed_nvm_flash_image_validate(cdev, image, &data);
	if (rc)
		goto exit;

	while (data < data_end) {
		bool check_resp = false;

		/* Parse the actual command */
		cmd_type = *((u32 *)data);
		switch (cmd_type) {
		case QED_NVM_FLASH_CMD_FILE_DATA:
			rc = qed_nvm_flash_image_file_data(cdev, &data,
							   &check_resp);
			break;
		case QED_NVM_FLASH_CMD_FILE_START:
			rc = qed_nvm_flash_image_file_start(cdev, &data,
							    &check_resp);
			break;
		case QED_NVM_FLASH_CMD_NVM_CHANGE:
			rc = qed_nvm_flash_image_access(cdev, &data,
							&check_resp);
			break;
		case QED_NVM_FLASH_CMD_NVM_CFG_ID:
			rc = qed_nvm_flash_cfg_write(cdev, &data);
			break;
		default:
			DP_ERR(cdev, "Unknown command %08x\n", cmd_type);
			rc = -EINVAL;
			goto exit;
		}

		if (rc) {
			DP_ERR(cdev, "Command %08x failed\n", cmd_type);
			goto exit;
		}

		/* Check response if needed */
		if (check_resp) {
			u32 mcp_response = 0;

			if (qed_mcp_nvm_resp(cdev, (u8 *)&mcp_response)) {
				DP_ERR(cdev, "Failed getting MCP response\n");
				rc = -EINVAL;
				goto exit;
			}

			switch (mcp_response & FW_MSG_CODE_MASK) {
			case FW_MSG_CODE_OK:
			case FW_MSG_CODE_NVM_OK:
			case FW_MSG_CODE_NVM_PUT_FILE_FINISH_OK:
			case FW_MSG_CODE_PHY_OK:
				break;
			default:
				DP_ERR(cdev, "MFW returns error: %08x\n",
				       mcp_response);
				rc = -EINVAL;
				goto exit;
			}
		}
	}

exit:
	release_firmware(image);

	return rc;
}

static int qed_nvm_get_image(struct qed_dev *cdev, enum qed_nvm_images type,
			     u8 *buf, u16 len)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);

	return qed_mcp_get_nvm_image(hwfn, type, buf, len);
}

void qed_schedule_recovery_handler(struct qed_hwfn *p_hwfn)
{
	struct qed_common_cb_ops *ops = p_hwfn->cdev->protocol_ops.common;
	void *cookie = p_hwfn->cdev->ops_cookie;

	if (ops && ops->schedule_recovery_handler)
		ops->schedule_recovery_handler(cookie);
}

static const char * const qed_hw_err_type_descr[] = {
	[QED_HW_ERR_FAN_FAIL]		= "Fan Failure",
	[QED_HW_ERR_MFW_RESP_FAIL]	= "MFW Response Failure",
	[QED_HW_ERR_HW_ATTN]		= "HW Attention",
	[QED_HW_ERR_DMAE_FAIL]		= "DMAE Failure",
	[QED_HW_ERR_RAMROD_FAIL]	= "Ramrod Failure",
	[QED_HW_ERR_FW_ASSERT]		= "FW Assertion",
	[QED_HW_ERR_LAST]		= "Unknown",
};

void qed_hw_error_occurred(struct qed_hwfn *p_hwfn,
			   enum qed_hw_err_type err_type)
{
	struct qed_common_cb_ops *ops = p_hwfn->cdev->protocol_ops.common;
	void *cookie = p_hwfn->cdev->ops_cookie;
	const char *err_str;

	if (err_type > QED_HW_ERR_LAST)
		err_type = QED_HW_ERR_LAST;
	err_str = qed_hw_err_type_descr[err_type];

	DP_NOTICE(p_hwfn, "HW error occurred [%s]\n", err_str);

	/* Call the HW error handler of the protocol driver.
	 * If it is not available - perform a minimal handling of preventing
	 * HW attentions from being reasserted.
	 */
	if (ops && ops->schedule_hw_err_handler)
		ops->schedule_hw_err_handler(cookie, err_type);
	else
		qed_int_attn_clr_enable(p_hwfn->cdev, true);
}

static int qed_set_coalesce(struct qed_dev *cdev, u16 rx_coal, u16 tx_coal,
			    void *handle)
{
		return qed_set_queue_coalesce(rx_coal, tx_coal, handle);
}

static int qed_set_led(struct qed_dev *cdev, enum qed_led_mode mode)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_ptt *ptt;
	int status = 0;

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return -EAGAIN;

	status = qed_mcp_set_led(hwfn, ptt, mode);

	qed_ptt_release(hwfn, ptt);

	return status;
}

int qed_recovery_process(struct qed_dev *cdev)
{
	struct qed_hwfn *p_hwfn = QED_LEADING_HWFN(cdev);
	struct qed_ptt *p_ptt;
	int rc = 0;

	p_ptt = qed_ptt_acquire(p_hwfn);
	if (!p_ptt)
		return -EAGAIN;

	rc = qed_start_recovery_process(p_hwfn, p_ptt);

	qed_ptt_release(p_hwfn, p_ptt);

	return rc;
}

static int qed_update_wol(struct qed_dev *cdev, bool enabled)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_ptt *ptt;
	int rc = 0;

	if (IS_VF(cdev))
		return 0;

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return -EAGAIN;

	rc = qed_mcp_ov_update_wol(hwfn, ptt, enabled ? QED_OV_WOL_ENABLED
				   : QED_OV_WOL_DISABLED);
	if (rc)
		goto out;
	rc = qed_mcp_ov_update_current_config(hwfn, ptt, QED_OV_CLIENT_DRV);

out:
	qed_ptt_release(hwfn, ptt);
	return rc;
}

static int qed_update_drv_state(struct qed_dev *cdev, bool active)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_ptt *ptt;
	int status = 0;

	if (IS_VF(cdev))
		return 0;

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return -EAGAIN;

	status = qed_mcp_ov_update_driver_state(hwfn, ptt, active ?
						QED_OV_DRIVER_STATE_ACTIVE :
						QED_OV_DRIVER_STATE_DISABLED);

	qed_ptt_release(hwfn, ptt);

	return status;
}

static int qed_update_mac(struct qed_dev *cdev, const u8 *mac)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_ptt *ptt;
	int status = 0;

	if (IS_VF(cdev))
		return 0;

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return -EAGAIN;

	status = qed_mcp_ov_update_mac(hwfn, ptt, mac);
	if (status)
		goto out;

	status = qed_mcp_ov_update_current_config(hwfn, ptt, QED_OV_CLIENT_DRV);

out:
	qed_ptt_release(hwfn, ptt);
	return status;
}

static int qed_update_mtu(struct qed_dev *cdev, u16 mtu)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_ptt *ptt;
	int status = 0;

	if (IS_VF(cdev))
		return 0;

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return -EAGAIN;

	status = qed_mcp_ov_update_mtu(hwfn, ptt, mtu);
	if (status)
		goto out;

	status = qed_mcp_ov_update_current_config(hwfn, ptt, QED_OV_CLIENT_DRV);

out:
	qed_ptt_release(hwfn, ptt);
	return status;
}

static int
qed_get_sb_info(struct qed_dev *cdev, struct qed_sb_info *sb,
		u16 qid, struct qed_sb_info_dbg *sb_dbg)
{
	struct qed_hwfn *hwfn = &cdev->hwfns[qid % cdev->num_hwfns];
	struct qed_ptt *ptt;
	int rc;

	if (IS_VF(cdev))
		return -EINVAL;

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt) {
		DP_NOTICE(hwfn, "Can't acquire PTT\n");
		return -EAGAIN;
	}

	memset(sb_dbg, 0, sizeof(*sb_dbg));
	rc = qed_int_get_sb_dbg(hwfn, ptt, sb, sb_dbg);

	qed_ptt_release(hwfn, ptt);
	return rc;
}

static int qed_read_module_eeprom(struct qed_dev *cdev, char *buf,
				  u8 dev_addr, u32 offset, u32 len)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_ptt *ptt;
	int rc = 0;

	if (IS_VF(cdev))
		return 0;

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return -EAGAIN;

	rc = qed_mcp_phy_sfp_read(hwfn, ptt, MFW_PORT(hwfn), dev_addr,
				  offset, len, buf);

	qed_ptt_release(hwfn, ptt);

	return rc;
}

static int qed_set_grc_config(struct qed_dev *cdev, u32 cfg_id, u32 val)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_ptt *ptt;
	int rc = 0;

	if (IS_VF(cdev))
		return 0;

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return -EAGAIN;

	rc = qed_dbg_grc_config(hwfn, cfg_id, val);

	qed_ptt_release(hwfn, ptt);

	return rc;
}

static __printf(2, 3) void qed_mfw_report(struct qed_dev *cdev, char *fmt, ...)
{
	char buf[QED_MFW_REPORT_STR_SIZE];
	struct qed_hwfn *p_hwfn;
	struct qed_ptt *p_ptt;
	va_list vl;

	va_start(vl, fmt);
	vsnprintf(buf, QED_MFW_REPORT_STR_SIZE, fmt, vl);
	va_end(vl);

	if (IS_PF(cdev)) {
		p_hwfn = QED_LEADING_HWFN(cdev);
		p_ptt = qed_ptt_acquire(p_hwfn);
		if (p_ptt) {
			qed_mcp_send_raw_debug_data(p_hwfn, p_ptt, buf, strlen(buf));
			qed_ptt_release(p_hwfn, p_ptt);
		}
	}
}

static u8 qed_get_affin_hwfn_idx(struct qed_dev *cdev)
{
	return QED_AFFIN_HWFN_IDX(cdev);
}

static int qed_get_esl_status(struct qed_dev *cdev, bool *esl_active)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_ptt *ptt;
	int rc = 0;

	*esl_active = false;

	if (IS_VF(cdev))
		return 0;

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return -EAGAIN;

	rc = qed_mcp_get_esl_status(hwfn, ptt, esl_active);

	qed_ptt_release(hwfn, ptt);

	return rc;
}

static struct qed_selftest_ops qed_selftest_ops_pass = {
	.selftest_memory = &qed_selftest_memory,
	.selftest_interrupt = &qed_selftest_interrupt,
	.selftest_register = &qed_selftest_register,
	.selftest_clock = &qed_selftest_clock,
	.selftest_nvram = &qed_selftest_nvram,
};

const struct qed_common_ops qed_common_ops_pass = {
	.selftest = &qed_selftest_ops_pass,
	.probe = &qed_probe,
	.remove = &qed_remove,
	.set_power_state = &qed_set_power_state,
	.set_name = &qed_set_name,
	.update_pf_params = &qed_update_pf_params,
	.slowpath_start = &qed_slowpath_start,
	.slowpath_stop = &qed_slowpath_stop,
	.set_fp_int = &qed_set_int_fp,
	.get_fp_int = &qed_get_int_fp,
	.sb_init = &qed_sb_init,
	.sb_release = &qed_sb_release,
	.simd_handler_config = &qed_simd_handler_config,
	.simd_handler_clean = &qed_simd_handler_clean,
	.dbg_grc = &qed_dbg_grc,
	.dbg_grc_size = &qed_dbg_grc_size,
	.can_link_change = &qed_can_link_change,
	.set_link = &qed_set_link,
	.get_link = &qed_get_current_link,
	.drain = &qed_drain,
	.update_msglvl = &qed_init_dp,
	.devlink_register = qed_devlink_register,
	.devlink_unregister = qed_devlink_unregister,
	.report_fatal_error = qed_report_fatal_error,
	.dbg_all_data = &qed_dbg_all_data,
	.dbg_all_data_size = &qed_dbg_all_data_size,
	.chain_alloc = &qed_chain_alloc,
	.chain_free = &qed_chain_free,
	.nvm_flash = &qed_nvm_flash,
	.nvm_get_image = &qed_nvm_get_image,
	.set_coalesce = &qed_set_coalesce,
	.set_led = &qed_set_led,
	.recovery_process = &qed_recovery_process,
	.recovery_prolog = &qed_recovery_prolog,
	.attn_clr_enable = &qed_int_attn_clr_enable,
	.update_drv_state = &qed_update_drv_state,
	.update_mac = &qed_update_mac,
	.update_mtu = &qed_update_mtu,
	.update_wol = &qed_update_wol,
	.db_recovery_add = &qed_db_recovery_add,
	.db_recovery_del = &qed_db_recovery_del,
	.read_module_eeprom = &qed_read_module_eeprom,
	.get_affin_hwfn_idx = &qed_get_affin_hwfn_idx,
	.read_nvm_cfg = &qed_nvm_flash_cfg_read,
	.read_nvm_cfg_len = &qed_nvm_flash_cfg_len,
	.set_grc_config = &qed_set_grc_config,
	.mfw_report = &qed_mfw_report,
	.get_sb_info = &qed_get_sb_info,
	.get_esl_status = &qed_get_esl_status,
};

void qed_get_protocol_stats(struct qed_dev *cdev,
			    enum qed_mcp_protocol_type type,
			    union qed_mcp_protocol_stats *stats)
{
	struct qed_eth_stats eth_stats;

	memset(stats, 0, sizeof(*stats));

	switch (type) {
	case QED_MCP_LAN_STATS:
		qed_get_vport_stats_context(cdev, &eth_stats, true);
		stats->lan_stats.ucast_rx_pkts =
					eth_stats.common.rx_ucast_pkts;
		stats->lan_stats.ucast_tx_pkts =
					eth_stats.common.tx_ucast_pkts;
		stats->lan_stats.fcs_err = -1;
		break;
	case QED_MCP_FCOE_STATS:
		qed_get_protocol_stats_fcoe(cdev, &stats->fcoe_stats, true);
		break;
	case QED_MCP_ISCSI_STATS:
		qed_get_protocol_stats_iscsi(cdev, &stats->iscsi_stats, true);
		break;
	default:
		DP_VERBOSE(cdev, QED_MSG_SP,
			   "Invalid protocol type = %d\n", type);
		return;
	}
}

int qed_mfw_tlv_req(struct qed_hwfn *hwfn)
{
	DP_VERBOSE(hwfn->cdev, NETIF_MSG_DRV,
		   "Scheduling slowpath task [Flag: %d]\n",
		   QED_SLOWPATH_MFW_TLV_REQ);
	/* Memory barrier for setting atomic bit */
	smp_mb__before_atomic();
	set_bit(QED_SLOWPATH_MFW_TLV_REQ, &hwfn->slowpath_task_flags);
	/* Memory barrier after setting atomic bit */
	smp_mb__after_atomic();
	queue_delayed_work(hwfn->slowpath_wq, &hwfn->slowpath_task, 0);

	return 0;
}

static void
qed_fill_generic_tlv_data(struct qed_dev *cdev, struct qed_mfw_tlv_generic *tlv)
{
	struct qed_common_cb_ops *op = cdev->protocol_ops.common;
	struct qed_eth_stats_common *p_common;
	struct qed_generic_tlvs gen_tlvs;
	struct qed_eth_stats stats;
	int i;

	memset(&gen_tlvs, 0, sizeof(gen_tlvs));
	op->get_generic_tlv_data(cdev->ops_cookie, &gen_tlvs);

	if (gen_tlvs.feat_flags & QED_TLV_IP_CSUM)
		tlv->flags.ipv4_csum_offload = true;
	if (gen_tlvs.feat_flags & QED_TLV_LSO)
		tlv->flags.lso_supported = true;
	tlv->flags.b_set = true;

	for (i = 0; i < QED_TLV_MAC_COUNT; i++) {
		if (is_valid_ether_addr(gen_tlvs.mac[i])) {
			ether_addr_copy(tlv->mac[i], gen_tlvs.mac[i]);
			tlv->mac_set[i] = true;
		}
	}

	qed_get_vport_stats(cdev, &stats);
	p_common = &stats.common;
	tlv->rx_frames = p_common->rx_ucast_pkts + p_common->rx_mcast_pkts +
			 p_common->rx_bcast_pkts;
	tlv->rx_frames_set = true;
	tlv->rx_bytes = p_common->rx_ucast_bytes + p_common->rx_mcast_bytes +
			p_common->rx_bcast_bytes;
	tlv->rx_bytes_set = true;
	tlv->tx_frames = p_common->tx_ucast_pkts + p_common->tx_mcast_pkts +
			 p_common->tx_bcast_pkts;
	tlv->tx_frames_set = true;
	tlv->tx_bytes = p_common->tx_ucast_bytes + p_common->tx_mcast_bytes +
			p_common->tx_bcast_bytes;
	tlv->rx_bytes_set = true;
}

int qed_mfw_fill_tlv_data(struct qed_hwfn *hwfn, enum qed_mfw_tlv_type type,
			  union qed_mfw_tlv_data *tlv_buf)
{
	struct qed_dev *cdev = hwfn->cdev;
	struct qed_common_cb_ops *ops;

	ops = cdev->protocol_ops.common;
	if (!ops || !ops->get_protocol_tlv_data || !ops->get_generic_tlv_data) {
		DP_NOTICE(hwfn, "Can't collect TLV management info\n");
		return -EINVAL;
	}

	switch (type) {
	case QED_MFW_TLV_GENERIC:
		qed_fill_generic_tlv_data(hwfn->cdev, &tlv_buf->generic);
		break;
	case QED_MFW_TLV_ETH:
		ops->get_protocol_tlv_data(cdev->ops_cookie, &tlv_buf->eth);
		break;
	case QED_MFW_TLV_FCOE:
		ops->get_protocol_tlv_data(cdev->ops_cookie, &tlv_buf->fcoe);
		break;
	case QED_MFW_TLV_ISCSI:
		ops->get_protocol_tlv_data(cdev->ops_cookie, &tlv_buf->iscsi);
		break;
	default:
		break;
	}

	return 0;
}

unsigned long qed_get_epoch_time(void)
{
	return ktime_get_real_seconds();
}
