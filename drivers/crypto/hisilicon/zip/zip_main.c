// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 HiSilicon Limited. */
#include <linux/acpi.h>
#include <linux/aer.h>
#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/topology.h>
#include "zip.h"

#define PCI_DEVICE_ID_ZIP_PF		0xa250
#define PCI_DEVICE_ID_ZIP_VF		0xa251

#define HZIP_VF_NUM			63
#define HZIP_QUEUE_NUM_V1		4096
#define HZIP_QUEUE_NUM_V2		1024

#define HZIP_CLOCK_GATE_CTRL		0x301004
#define COMP0_ENABLE			BIT(0)
#define COMP1_ENABLE			BIT(1)
#define DECOMP0_ENABLE			BIT(2)
#define DECOMP1_ENABLE			BIT(3)
#define DECOMP2_ENABLE			BIT(4)
#define DECOMP3_ENABLE			BIT(5)
#define DECOMP4_ENABLE			BIT(6)
#define DECOMP5_ENABLE			BIT(7)
#define ALL_COMP_DECOMP_EN		(COMP0_ENABLE | COMP1_ENABLE |	\
					 DECOMP0_ENABLE | DECOMP1_ENABLE | \
					 DECOMP2_ENABLE | DECOMP3_ENABLE | \
					 DECOMP4_ENABLE | DECOMP5_ENABLE)
#define DECOMP_CHECK_ENABLE		BIT(16)

#define HZIP_PORT_ARCA_CHE_0		0x301040
#define HZIP_PORT_ARCA_CHE_1		0x301044
#define HZIP_PORT_AWCA_CHE_0		0x301060
#define HZIP_PORT_AWCA_CHE_1		0x301064
#define CACHE_ALL_EN			0xffffffff

#define HZIP_BD_RUSER_32_63		0x301110
#define HZIP_SGL_RUSER_32_63		0x30111c
#define HZIP_DATA_RUSER_32_63		0x301128
#define HZIP_DATA_WUSER_32_63		0x301134
#define HZIP_BD_WUSER_32_63		0x301140



#define HZIP_CORE_INT_SOURCE		0x3010A0
#define HZIP_CORE_INT_MASK		0x3010A4
#define HZIP_CORE_INT_STATUS		0x3010AC
#define HZIP_CORE_INT_STATUS_M_ECC	BIT(1)
#define HZIP_CORE_SRAM_ECC_ERR_INFO	0x301148
#define SRAM_ECC_ERR_NUM_SHIFT		16
#define SRAM_ECC_ERR_ADDR_SHIFT		24
#define HZIP_CORE_INT_DISABLE		0x000007FF
#define HZIP_SQE_SIZE			128
#define HZIP_PF_DEF_Q_NUM		64
#define HZIP_PF_DEF_Q_BASE		0


#define HZIP_NUMA_DISTANCE		100

static const char hisi_zip_name[] = "hisi_zip";
LIST_HEAD(hisi_zip_list);
DEFINE_MUTEX(hisi_zip_list_lock);

#ifdef CONFIG_NUMA
static struct hisi_zip *find_zip_device_numa(int node)
{
	struct hisi_zip *zip = NULL;
	struct hisi_zip *hisi_zip;
	int min_distance = HZIP_NUMA_DISTANCE;
	struct device *dev;

	list_for_each_entry(hisi_zip, &hisi_zip_list, list) {
		dev = &hisi_zip->qm.pdev->dev;
		if (node_distance(dev->numa_node, node) < min_distance) {
			zip = hisi_zip;
			min_distance = node_distance(dev->numa_node, node);
		}
	}

	return zip;
}
#endif

struct hisi_zip *find_zip_device(int node)
{
	struct hisi_zip *zip = NULL;

	mutex_lock(&hisi_zip_list_lock);
#ifdef CONFIG_NUMA
	zip = find_zip_device_numa(node);
#else
	zip = list_first_entry(&hisi_zip_list, struct hisi_zip, list);
#endif
	mutex_unlock(&hisi_zip_list_lock);

	return zip;
}

struct hisi_zip_hw_error {
	u32 int_msk;
	const char *msg;
};

static const struct hisi_zip_hw_error zip_hw_error[] = {
	{ .int_msk = BIT(0), .msg = "zip_ecc_1bitt_err" },
	{ .int_msk = BIT(1), .msg = "zip_ecc_2bit_err" },
	{ .int_msk = BIT(2), .msg = "zip_axi_rresp_err" },
	{ .int_msk = BIT(3), .msg = "zip_axi_bresp_err" },
	{ .int_msk = BIT(4), .msg = "zip_src_addr_parse_err" },
	{ .int_msk = BIT(5), .msg = "zip_dst_addr_parse_err" },
	{ .int_msk = BIT(6), .msg = "zip_pre_in_addr_err" },
	{ .int_msk = BIT(7), .msg = "zip_pre_in_data_err" },
	{ .int_msk = BIT(8), .msg = "zip_com_inf_err" },
	{ .int_msk = BIT(9), .msg = "zip_enc_inf_err" },
	{ .int_msk = BIT(10), .msg = "zip_pre_out_err" },
	{ /* sentinel */ }
};

/*
 * One ZIP controller has one PF and multiple VFs, some global configurations
 * which PF has need this structure.
 *
 * Just relevant for PF.
 */
struct hisi_zip_ctrl {
	u32 num_vfs;
	struct hisi_zip *hisi_zip;
};

static int pf_q_num_set(const char *val, const struct kernel_param *kp)
{
	struct pci_dev *pdev = pci_get_device(PCI_VENDOR_ID_HUAWEI,
					      PCI_DEVICE_ID_ZIP_PF, NULL);
	u32 n, q_num;
	u8 rev_id;
	int ret;

	if (!val)
		return -EINVAL;

	if (!pdev) {
		q_num = min_t(u32, HZIP_QUEUE_NUM_V1, HZIP_QUEUE_NUM_V2);
		pr_info("No device found currently, suppose queue number is %d\n",
			q_num);
	} else {
		rev_id = pdev->revision;
		switch (rev_id) {
		case QM_HW_V1:
			q_num = HZIP_QUEUE_NUM_V1;
			break;
		case QM_HW_V2:
			q_num = HZIP_QUEUE_NUM_V2;
			break;
		default:
			return -EINVAL;
		}
	}

	ret = kstrtou32(val, 10, &n);
	if (ret != 0 || n > q_num || n == 0)
		return -EINVAL;

	return param_set_int(val, kp);
}

static const struct kernel_param_ops pf_q_num_ops = {
	.set = pf_q_num_set,
	.get = param_get_int,
};

static u32 pf_q_num = HZIP_PF_DEF_Q_NUM;
module_param_cb(pf_q_num, &pf_q_num_ops, &pf_q_num, 0444);
MODULE_PARM_DESC(pf_q_num, "Number of queues in PF(v1 1-4096, v2 1-1024)");

static int uacce_mode;
module_param(uacce_mode, int, 0);

static const struct pci_device_id hisi_zip_dev_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_HUAWEI, PCI_DEVICE_ID_ZIP_PF) },
	{ PCI_DEVICE(PCI_VENDOR_ID_HUAWEI, PCI_DEVICE_ID_ZIP_VF) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, hisi_zip_dev_ids);

static inline void hisi_zip_add_to_list(struct hisi_zip *hisi_zip)
{
	mutex_lock(&hisi_zip_list_lock);
	list_add_tail(&hisi_zip->list, &hisi_zip_list);
	mutex_unlock(&hisi_zip_list_lock);
}

static inline void hisi_zip_remove_from_list(struct hisi_zip *hisi_zip)
{
	mutex_lock(&hisi_zip_list_lock);
	list_del(&hisi_zip->list);
	mutex_unlock(&hisi_zip_list_lock);
}

static void hisi_zip_set_user_domain_and_cache(struct hisi_zip *hisi_zip)
{
	void __iomem *base = hisi_zip->qm.io_base;

	/* qm user domain */
	writel(AXUSER_BASE, base + QM_ARUSER_M_CFG_1);
	writel(ARUSER_M_CFG_ENABLE, base + QM_ARUSER_M_CFG_ENABLE);
	writel(AXUSER_BASE, base + QM_AWUSER_M_CFG_1);
	writel(AWUSER_M_CFG_ENABLE, base + QM_AWUSER_M_CFG_ENABLE);
	writel(WUSER_M_CFG_ENABLE, base + QM_WUSER_M_CFG_ENABLE);

	/* qm cache */
	writel(AXI_M_CFG, base + QM_AXI_M_CFG);
	writel(AXI_M_CFG_ENABLE, base + QM_AXI_M_CFG_ENABLE);
	/* disable FLR triggered by BME(bus master enable) */
	writel(PEH_AXUSER_CFG, base + QM_PEH_AXUSER_CFG);
	writel(PEH_AXUSER_CFG_ENABLE, base + QM_PEH_AXUSER_CFG_ENABLE);

	/* cache */
	writel(CACHE_ALL_EN, base + HZIP_PORT_ARCA_CHE_0);
	writel(CACHE_ALL_EN, base + HZIP_PORT_ARCA_CHE_1);
	writel(CACHE_ALL_EN, base + HZIP_PORT_AWCA_CHE_0);
	writel(CACHE_ALL_EN, base + HZIP_PORT_AWCA_CHE_1);

	/* user domain configurations */
	writel(AXUSER_BASE, base + HZIP_BD_RUSER_32_63);
	writel(AXUSER_BASE, base + HZIP_SGL_RUSER_32_63);
	writel(AXUSER_BASE, base + HZIP_BD_WUSER_32_63);
	writel(AXUSER_BASE, base + HZIP_DATA_RUSER_32_63);
	writel(AXUSER_BASE, base + HZIP_DATA_WUSER_32_63);

	/* let's open all compression/decompression cores */
	writel(DECOMP_CHECK_ENABLE | ALL_COMP_DECOMP_EN,
	       base + HZIP_CLOCK_GATE_CTRL);

	/* enable sqc writeback */
	writel(SQC_CACHE_ENABLE | CQC_CACHE_ENABLE | SQC_CACHE_WB_ENABLE |
	       CQC_CACHE_WB_ENABLE | FIELD_PREP(SQC_CACHE_WB_THRD, 1) |
	       FIELD_PREP(CQC_CACHE_WB_THRD, 1), base + QM_CACHE_CTL);
}

static void hisi_zip_hw_error_set_state(struct hisi_zip *hisi_zip, bool state)
{
	struct hisi_qm *qm = &hisi_zip->qm;

	if (qm->ver == QM_HW_V1) {
		writel(HZIP_CORE_INT_DISABLE, qm->io_base + HZIP_CORE_INT_MASK);
		dev_info(&qm->pdev->dev, "ZIP v%d does not support hw error handle\n",
			 qm->ver);
		return;
	}

	if (state) {
		/* clear ZIP hw error source if having */
		writel(HZIP_CORE_INT_DISABLE, hisi_zip->qm.io_base +
					      HZIP_CORE_INT_SOURCE);
		/* enable ZIP hw error interrupts */
		writel(0, hisi_zip->qm.io_base + HZIP_CORE_INT_MASK);
	} else {
		/* disable ZIP hw error interrupts */
		writel(HZIP_CORE_INT_DISABLE,
		       hisi_zip->qm.io_base + HZIP_CORE_INT_MASK);
	}
}

static void hisi_zip_hw_error_init(struct hisi_zip *hisi_zip)
{
	hisi_qm_hw_error_init(&hisi_zip->qm, QM_BASE_CE,
			      QM_BASE_NFE | QM_ACC_WB_NOT_READY_TIMEOUT, 0,
			      QM_DB_RANDOM_INVALID);
	hisi_zip_hw_error_set_state(hisi_zip, true);
}

static int hisi_zip_pf_probe_init(struct hisi_zip *hisi_zip)
{
	struct hisi_qm *qm = &hisi_zip->qm;
	struct hisi_zip_ctrl *ctrl;

	ctrl = devm_kzalloc(&qm->pdev->dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	hisi_zip->ctrl = ctrl;
	ctrl->hisi_zip = hisi_zip;

	switch (qm->ver) {
	case QM_HW_V1:
		qm->ctrl_qp_num = HZIP_QUEUE_NUM_V1;
		break;

	case QM_HW_V2:
		qm->ctrl_qp_num = HZIP_QUEUE_NUM_V2;
		break;

	default:
		return -EINVAL;
	}

	hisi_zip_set_user_domain_and_cache(hisi_zip);
	hisi_zip_hw_error_init(hisi_zip);

	return 0;
}

static int hisi_zip_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct hisi_zip *hisi_zip;
	enum qm_hw_ver rev_id;
	struct hisi_qm *qm;
	int ret;

	rev_id = hisi_qm_get_hw_version(pdev);
	if (rev_id == QM_HW_UNKNOWN)
		return -EINVAL;

	hisi_zip = devm_kzalloc(&pdev->dev, sizeof(*hisi_zip), GFP_KERNEL);
	if (!hisi_zip)
		return -ENOMEM;
	pci_set_drvdata(pdev, hisi_zip);

	qm = &hisi_zip->qm;
	qm->pdev = pdev;
	qm->ver = rev_id;

	qm->sqe_size = HZIP_SQE_SIZE;
	qm->dev_name = hisi_zip_name;
	qm->fun_type = (pdev->device == PCI_DEVICE_ID_ZIP_PF) ? QM_HW_PF :
								QM_HW_VF;
	switch (uacce_mode) {
	case 0:
		qm->use_dma_api = true;
		break;
	case 1:
		qm->use_dma_api = false;
		break;
	case 2:
		qm->use_dma_api = true;
		break;
	default:
		return -EINVAL;
	}

	ret = hisi_qm_init(qm);
	if (ret) {
		dev_err(&pdev->dev, "Failed to init qm!\n");
		return ret;
	}

	if (qm->fun_type == QM_HW_PF) {
		ret = hisi_zip_pf_probe_init(hisi_zip);
		if (ret)
			return ret;

		qm->qp_base = HZIP_PF_DEF_Q_BASE;
		qm->qp_num = pf_q_num;
	} else if (qm->fun_type == QM_HW_VF) {
		/*
		 * have no way to get qm configure in VM in v1 hardware,
		 * so currently force PF to uses HZIP_PF_DEF_Q_NUM, and force
		 * to trigger only one VF in v1 hardware.
		 *
		 * v2 hardware has no such problem.
		 */
		if (qm->ver == QM_HW_V1) {
			qm->qp_base = HZIP_PF_DEF_Q_NUM;
			qm->qp_num = HZIP_QUEUE_NUM_V1 - HZIP_PF_DEF_Q_NUM;
		} else if (qm->ver == QM_HW_V2)
			/* v2 starts to support get vft by mailbox */
			hisi_qm_get_vft(qm, &qm->qp_base, &qm->qp_num);
	}

	ret = hisi_qm_start(qm);
	if (ret)
		goto err_qm_uninit;

	hisi_zip_add_to_list(hisi_zip);

	return 0;

err_qm_uninit:
	hisi_qm_uninit(qm);
	return ret;
}

/* Currently we only support equal assignment */
static int hisi_zip_vf_q_assign(struct hisi_zip *hisi_zip, int num_vfs)
{
	struct hisi_qm *qm = &hisi_zip->qm;
	u32 qp_num = qm->qp_num;
	u32 q_base = qp_num;
	u32 q_num, remain_q_num, i;
	int ret;

	if (!num_vfs)
		return -EINVAL;

	remain_q_num = qm->ctrl_qp_num - qp_num;
	if (remain_q_num < num_vfs)
		return -EINVAL;

	q_num = remain_q_num / num_vfs;
	for (i = 1; i <= num_vfs; i++) {
		if (i == num_vfs)
			q_num += remain_q_num % num_vfs;
		ret = hisi_qm_set_vft(qm, i, q_base, q_num);
		if (ret)
			return ret;
		q_base += q_num;
	}

	return 0;
}

static int hisi_zip_clear_vft_config(struct hisi_zip *hisi_zip)
{
	struct hisi_zip_ctrl *ctrl = hisi_zip->ctrl;
	struct hisi_qm *qm = &hisi_zip->qm;
	u32 i, num_vfs = ctrl->num_vfs;
	int ret;

	for (i = 1; i <= num_vfs; i++) {
		ret = hisi_qm_set_vft(qm, i, 0, 0);
		if (ret)
			return ret;
	}

	ctrl->num_vfs = 0;

	return 0;
}

static int hisi_zip_sriov_enable(struct pci_dev *pdev, int max_vfs)
{
#ifdef CONFIG_PCI_IOV
	struct hisi_zip *hisi_zip = pci_get_drvdata(pdev);
	int pre_existing_vfs, num_vfs, ret;

	pre_existing_vfs = pci_num_vf(pdev);

	if (pre_existing_vfs) {
		dev_err(&pdev->dev,
			"Can't enable VF. Please disable pre-enabled VFs!\n");
		return 0;
	}

	num_vfs = min_t(int, max_vfs, HZIP_VF_NUM);

	ret = hisi_zip_vf_q_assign(hisi_zip, num_vfs);
	if (ret) {
		dev_err(&pdev->dev, "Can't assign queues for VF!\n");
		return ret;
	}

	hisi_zip->ctrl->num_vfs = num_vfs;

	ret = pci_enable_sriov(pdev, num_vfs);
	if (ret) {
		dev_err(&pdev->dev, "Can't enable VF!\n");
		hisi_zip_clear_vft_config(hisi_zip);
		return ret;
	}

	return num_vfs;
#else
	return 0;
#endif
}

static int hisi_zip_sriov_disable(struct pci_dev *pdev)
{
	struct hisi_zip *hisi_zip = pci_get_drvdata(pdev);

	if (pci_vfs_assigned(pdev)) {
		dev_err(&pdev->dev,
			"Can't disable VFs while VFs are assigned!\n");
		return -EPERM;
	}

	/* remove in hisi_zip_pci_driver will be called to free VF resources */
	pci_disable_sriov(pdev);

	return hisi_zip_clear_vft_config(hisi_zip);
}

static int hisi_zip_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	if (num_vfs == 0)
		return hisi_zip_sriov_disable(pdev);
	else
		return hisi_zip_sriov_enable(pdev, num_vfs);
}

static void hisi_zip_remove(struct pci_dev *pdev)
{
	struct hisi_zip *hisi_zip = pci_get_drvdata(pdev);
	struct hisi_qm *qm = &hisi_zip->qm;

	if (qm->fun_type == QM_HW_PF && hisi_zip->ctrl->num_vfs != 0)
		hisi_zip_sriov_disable(pdev);

	hisi_qm_stop(qm);

	if (qm->fun_type == QM_HW_PF)
		hisi_zip_hw_error_set_state(hisi_zip, false);

	hisi_qm_uninit(qm);
	hisi_zip_remove_from_list(hisi_zip);
}

static void hisi_zip_log_hw_error(struct hisi_zip *hisi_zip, u32 err_sts)
{
	const struct hisi_zip_hw_error *err = zip_hw_error;
	struct device *dev = &hisi_zip->qm.pdev->dev;
	u32 err_val;

	while (err->msg) {
		if (err->int_msk & err_sts) {
			dev_warn(dev, "%s [error status=0x%x] found\n",
				 err->msg, err->int_msk);

			if (HZIP_CORE_INT_STATUS_M_ECC & err->int_msk) {
				err_val = readl(hisi_zip->qm.io_base +
						HZIP_CORE_SRAM_ECC_ERR_INFO);
				dev_warn(dev, "hisi-zip multi ecc sram num=0x%x\n",
					 ((err_val >> SRAM_ECC_ERR_NUM_SHIFT) &
					  0xFF));
				dev_warn(dev, "hisi-zip multi ecc sram addr=0x%x\n",
					 (err_val >> SRAM_ECC_ERR_ADDR_SHIFT));
			}
		}
		err++;
	}
}

static pci_ers_result_t hisi_zip_hw_error_handle(struct hisi_zip *hisi_zip)
{
	u32 err_sts;

	/* read err sts */
	err_sts = readl(hisi_zip->qm.io_base + HZIP_CORE_INT_STATUS);

	if (err_sts) {
		hisi_zip_log_hw_error(hisi_zip, err_sts);
		/* clear error interrupts */
		writel(err_sts, hisi_zip->qm.io_base + HZIP_CORE_INT_SOURCE);

		return PCI_ERS_RESULT_NEED_RESET;
	}

	return PCI_ERS_RESULT_RECOVERED;
}

static pci_ers_result_t hisi_zip_process_hw_error(struct pci_dev *pdev)
{
	struct hisi_zip *hisi_zip = pci_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	pci_ers_result_t qm_ret, zip_ret;

	if (!hisi_zip) {
		dev_err(dev,
			"Can't recover ZIP-error occurred during device init\n");
		return PCI_ERS_RESULT_NONE;
	}

	qm_ret = hisi_qm_hw_error_handle(&hisi_zip->qm);

	zip_ret = hisi_zip_hw_error_handle(hisi_zip);

	return (qm_ret == PCI_ERS_RESULT_NEED_RESET ||
		zip_ret == PCI_ERS_RESULT_NEED_RESET) ?
	       PCI_ERS_RESULT_NEED_RESET : PCI_ERS_RESULT_RECOVERED;
}

static pci_ers_result_t hisi_zip_error_detected(struct pci_dev *pdev,
						pci_channel_state_t state)
{
	if (pdev->is_virtfn)
		return PCI_ERS_RESULT_NONE;

	dev_info(&pdev->dev, "PCI error detected, state(=%d)!!\n", state);
	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;

	return hisi_zip_process_hw_error(pdev);
}

static const struct pci_error_handlers hisi_zip_err_handler = {
	.error_detected	= hisi_zip_error_detected,
};

static struct pci_driver hisi_zip_pci_driver = {
	.name			= "hisi_zip",
	.id_table		= hisi_zip_dev_ids,
	.probe			= hisi_zip_probe,
	.remove			= hisi_zip_remove,
	.sriov_configure	= hisi_zip_sriov_configure,
	.err_handler		= &hisi_zip_err_handler,
};

static int __init hisi_zip_init(void)
{
	int ret;

	ret = pci_register_driver(&hisi_zip_pci_driver);
	if (ret < 0) {
		pr_err("Failed to register pci driver.\n");
		return ret;
	}

	if (uacce_mode == 0 || uacce_mode == 2) {
		ret = hisi_zip_register_to_crypto();
		if (ret < 0) {
			pr_err("Failed to register driver to crypto.\n");
			goto err_crypto;
		}
	}

	return 0;

err_crypto:
	pci_unregister_driver(&hisi_zip_pci_driver);
	return ret;
}

static void __exit hisi_zip_exit(void)
{
	if (uacce_mode == 0 || uacce_mode == 2)
		hisi_zip_unregister_from_crypto();
	pci_unregister_driver(&hisi_zip_pci_driver);
}

module_init(hisi_zip_init);
module_exit(hisi_zip_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Zhou Wang <wangzhou1@hisilicon.com>");
MODULE_DESCRIPTION("Driver for HiSilicon ZIP accelerator");
