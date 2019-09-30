// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018-2019 HiSilicon Limited. */
#include <linux/acpi.h>
#include <linux/aer.h>
#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/topology.h>
#include "hpre.h"

#define HPRE_VF_NUM			63
#define HPRE_QUEUE_NUM_V2		1024
#define HPRE_QM_ABNML_INT_MASK		0x100004
#define HPRE_CTRL_CNT_CLR_CE_BIT	BIT(0)
#define HPRE_COMM_CNT_CLR_CE		0x0
#define HPRE_CTRL_CNT_CLR_CE		0x301000
#define HPRE_FSM_MAX_CNT		0x301008
#define HPRE_VFG_AXQOS			0x30100c
#define HPRE_VFG_AXCACHE		0x301010
#define HPRE_RDCHN_INI_CFG		0x301014
#define HPRE_AWUSR_FP_CFG		0x301018
#define HPRE_BD_ENDIAN			0x301020
#define HPRE_ECC_BYPASS			0x301024
#define HPRE_RAS_WIDTH_CFG		0x301028
#define HPRE_POISON_BYPASS		0x30102c
#define HPRE_BD_ARUSR_CFG		0x301030
#define HPRE_BD_AWUSR_CFG		0x301034
#define HPRE_TYPES_ENB			0x301038
#define HPRE_DATA_RUSER_CFG		0x30103c
#define HPRE_DATA_WUSER_CFG		0x301040
#define HPRE_INT_MASK			0x301400
#define HPRE_INT_STATUS			0x301800
#define HPRE_CORE_INT_ENABLE		0
#define HPRE_CORE_INT_DISABLE		0x003fffff
#define HPRE_RAS_ECC_1BIT_TH		0x30140c
#define HPRE_RDCHN_INI_ST		0x301a00
#define HPRE_CLSTR_BASE			0x302000
#define HPRE_CORE_EN_OFFSET		0x04
#define HPRE_CORE_INI_CFG_OFFSET	0x20
#define HPRE_CORE_INI_STATUS_OFFSET	0x80
#define HPRE_CORE_HTBT_WARN_OFFSET	0x8c
#define HPRE_CORE_IS_SCHD_OFFSET	0x90

#define HPRE_RAS_CE_ENB			0x301410
#define HPRE_HAC_RAS_CE_ENABLE		0x3f
#define HPRE_RAS_NFE_ENB		0x301414
#define HPRE_HAC_RAS_NFE_ENABLE		0x3fffc0
#define HPRE_RAS_FE_ENB			0x301418
#define HPRE_HAC_RAS_FE_ENABLE		0

#define HPRE_CORE_ENB		(HPRE_CLSTR_BASE + HPRE_CORE_EN_OFFSET)
#define HPRE_CORE_INI_CFG	(HPRE_CLSTR_BASE + HPRE_CORE_INI_CFG_OFFSET)
#define HPRE_CORE_INI_STATUS (HPRE_CLSTR_BASE + HPRE_CORE_INI_STATUS_OFFSET)
#define HPRE_HAC_ECC1_CNT		0x301a04
#define HPRE_HAC_ECC2_CNT		0x301a08
#define HPRE_HAC_INT_STATUS		0x301800
#define HPRE_HAC_SOURCE_INT		0x301600
#define MASTER_GLOBAL_CTRL_SHUTDOWN	1
#define MASTER_TRANS_RETURN_RW		3
#define HPRE_MASTER_TRANS_RETURN	0x300150
#define HPRE_MASTER_GLOBAL_CTRL		0x300000
#define HPRE_CLSTR_ADDR_INTRVL		0x1000
#define HPRE_CLUSTER_INQURY		0x100
#define HPRE_CLSTR_ADDR_INQRY_RSLT	0x104
#define HPRE_TIMEOUT_ABNML_BIT		6
#define HPRE_PASID_EN_BIT		9
#define HPRE_REG_RD_INTVRL_US		10
#define HPRE_REG_RD_TMOUT_US		1000
#define HPRE_DBGFS_VAL_MAX_LEN		20
#define HPRE_PCI_DEVICE_ID		0xa258
#define HPRE_ADDR(qm, offset)		(qm->io_base + (offset))
#define HPRE_QM_USR_CFG_MASK		0xfffffffe
#define HPRE_QM_AXI_CFG_MASK		0xffff
#define HPRE_QM_VFG_AX_MASK		0xff
#define HPRE_BD_USR_MASK		0x3
#define HPRE_CLUSTER_CORE_MASK		0xf

#define HPRE_VIA_MSI_DSM		1

static LIST_HEAD(hpre_list);
static DEFINE_MUTEX(hpre_list_lock);
static const char hpre_name[] = "hisi_hpre";
static const struct pci_device_id hpre_dev_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_HUAWEI, HPRE_PCI_DEVICE_ID) },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, hpre_dev_ids);

struct hpre_hw_error {
	u32 int_msk;
	const char *msg;
};

static const struct hpre_hw_error hpre_hw_errors[] = {
	{ .int_msk = BIT(0), .msg = "hpre_ecc_1bitt_err" },
	{ .int_msk = BIT(1), .msg = "hpre_ecc_2bit_err" },
	{ .int_msk = BIT(2), .msg = "hpre_data_wr_err" },
	{ .int_msk = BIT(3), .msg = "hpre_data_rd_err" },
	{ .int_msk = BIT(4), .msg = "hpre_bd_rd_err" },
	{ .int_msk = BIT(5), .msg = "hpre_ooo_2bit_ecc_err" },
	{ .int_msk = BIT(6), .msg = "hpre_cltr1_htbt_tm_out_err" },
	{ .int_msk = BIT(7), .msg = "hpre_cltr2_htbt_tm_out_err" },
	{ .int_msk = BIT(8), .msg = "hpre_cltr3_htbt_tm_out_err" },
	{ .int_msk = BIT(9), .msg = "hpre_cltr4_htbt_tm_out_err" },
	{ .int_msk = GENMASK(10, 15), .msg = "hpre_ooo_rdrsp_err" },
	{ .int_msk = GENMASK(16, 21), .msg = "hpre_ooo_wrrsp_err" },
	{ /* sentinel */ }
};

static int hpre_pf_q_num_set(const char *val, const struct kernel_param *kp)
{
	struct pci_dev *pdev;
	u32 n, q_num;
	u8 rev_id;
	int ret;

	if (!val)
		return -EINVAL;

	pdev = pci_get_device(PCI_VENDOR_ID_HUAWEI, HPRE_PCI_DEVICE_ID, NULL);
	if (!pdev) {
		q_num = HPRE_QUEUE_NUM_V2;
		pr_info("No device found currently, suppose queue number is %d\n",
			q_num);
	} else {
		rev_id = pdev->revision;
		if (rev_id != QM_HW_V2)
			return -EINVAL;

		q_num = HPRE_QUEUE_NUM_V2;
	}

	ret = kstrtou32(val, 10, &n);
	if (ret != 0 || n == 0 || n > q_num)
		return -EINVAL;

	return param_set_int(val, kp);
}

static const struct kernel_param_ops hpre_pf_q_num_ops = {
	.set = hpre_pf_q_num_set,
	.get = param_get_int,
};

static u32 hpre_pf_q_num = HPRE_PF_DEF_Q_NUM;
module_param_cb(hpre_pf_q_num, &hpre_pf_q_num_ops, &hpre_pf_q_num, 0444);
MODULE_PARM_DESC(hpre_pf_q_num, "Number of queues in PF of CS(1-1024)");

static inline void hpre_add_to_list(struct hpre *hpre)
{
	mutex_lock(&hpre_list_lock);
	list_add_tail(&hpre->list, &hpre_list);
	mutex_unlock(&hpre_list_lock);
}

static inline void hpre_remove_from_list(struct hpre *hpre)
{
	mutex_lock(&hpre_list_lock);
	list_del(&hpre->list);
	mutex_unlock(&hpre_list_lock);
}

struct hpre *hpre_find_device(int node)
{
	struct hpre *hpre, *ret = NULL;
	int min_distance = INT_MAX;
	struct device *dev;
	int dev_node = 0;

	mutex_lock(&hpre_list_lock);
	list_for_each_entry(hpre, &hpre_list, list) {
		dev = &hpre->qm.pdev->dev;
#ifdef CONFIG_NUMA
		dev_node = dev->numa_node;
		if (dev_node < 0)
			dev_node = 0;
#endif
		if (node_distance(dev_node, node) < min_distance) {
			ret = hpre;
			min_distance = node_distance(dev_node, node);
		}
	}
	mutex_unlock(&hpre_list_lock);

	return ret;
}

static int hpre_cfg_by_dsm(struct hisi_qm *qm)
{
	struct device *dev = &qm->pdev->dev;
	union acpi_object *obj;
	guid_t guid;

	if (guid_parse("b06b81ab-0134-4a45-9b0c-483447b95fa7", &guid)) {
		dev_err(dev, "Hpre GUID failed\n");
		return -EINVAL;
	}

	/* Switch over to MSI handling due to non-standard PCI implementation */
	obj = acpi_evaluate_dsm(ACPI_HANDLE(dev), &guid,
				0, HPRE_VIA_MSI_DSM, NULL);
	if (!obj) {
		dev_err(dev, "ACPI handle failed!\n");
		return -EIO;
	}

	ACPI_FREE(obj);

	return 0;
}

static int hpre_set_user_domain_and_cache(struct hpre *hpre)
{
	struct hisi_qm *qm = &hpre->qm;
	struct device *dev = &qm->pdev->dev;
	unsigned long offset;
	int ret, i;
	u32 val;

	writel(HPRE_QM_USR_CFG_MASK, HPRE_ADDR(qm, QM_ARUSER_M_CFG_ENABLE));
	writel(HPRE_QM_USR_CFG_MASK, HPRE_ADDR(qm, QM_AWUSER_M_CFG_ENABLE));
	writel_relaxed(HPRE_QM_AXI_CFG_MASK, HPRE_ADDR(qm, QM_AXI_M_CFG));

	/* disable FLR triggered by BME(bus master enable) */
	writel(PEH_AXUSER_CFG, HPRE_ADDR(qm, QM_PEH_AXUSER_CFG));
	writel(PEH_AXUSER_CFG_ENABLE, HPRE_ADDR(qm, QM_PEH_AXUSER_CFG_ENABLE));

	/* HPRE need more time, we close this interrupt */
	val = readl_relaxed(HPRE_ADDR(qm, HPRE_QM_ABNML_INT_MASK));
	val |= BIT(HPRE_TIMEOUT_ABNML_BIT);
	writel_relaxed(val, HPRE_ADDR(qm, HPRE_QM_ABNML_INT_MASK));

	writel(0x1, HPRE_ADDR(qm, HPRE_TYPES_ENB));
	writel(HPRE_QM_VFG_AX_MASK, HPRE_ADDR(qm, HPRE_VFG_AXCACHE));
	writel(0x0, HPRE_ADDR(qm, HPRE_BD_ENDIAN));
	writel(0x0, HPRE_ADDR(qm, HPRE_INT_MASK));
	writel(0x0, HPRE_ADDR(qm, HPRE_RAS_ECC_1BIT_TH));
	writel(0x0, HPRE_ADDR(qm, HPRE_POISON_BYPASS));
	writel(0x0, HPRE_ADDR(qm, HPRE_COMM_CNT_CLR_CE));
	writel(0x0, HPRE_ADDR(qm, HPRE_ECC_BYPASS));

	writel(HPRE_BD_USR_MASK, HPRE_ADDR(qm, HPRE_BD_ARUSR_CFG));
	writel(HPRE_BD_USR_MASK, HPRE_ADDR(qm, HPRE_BD_AWUSR_CFG));
	writel(0x1, HPRE_ADDR(qm, HPRE_RDCHN_INI_CFG));
	ret = readl_relaxed_poll_timeout(HPRE_ADDR(qm, HPRE_RDCHN_INI_ST), val,
			val & BIT(0),
			HPRE_REG_RD_INTVRL_US,
			HPRE_REG_RD_TMOUT_US);
	if (ret) {
		dev_err(dev, "read rd channel timeout fail!\n");
		return -ETIMEDOUT;
	}

	for (i = 0; i < HPRE_CLUSTERS_NUM; i++) {
		offset = i * HPRE_CLSTR_ADDR_INTRVL;

		/* clusters initiating */
		writel(HPRE_CLUSTER_CORE_MASK,
		       HPRE_ADDR(qm, offset + HPRE_CORE_ENB));
		writel(0x1, HPRE_ADDR(qm, offset + HPRE_CORE_INI_CFG));
		ret = readl_relaxed_poll_timeout(HPRE_ADDR(qm, offset +
					HPRE_CORE_INI_STATUS), val,
					((val & HPRE_CLUSTER_CORE_MASK) ==
					HPRE_CLUSTER_CORE_MASK),
					HPRE_REG_RD_INTVRL_US,
					HPRE_REG_RD_TMOUT_US);
		if (ret) {
			dev_err(dev,
				"cluster %d int st status timeout!\n", i);
			return -ETIMEDOUT;
		}
	}

	ret = hpre_cfg_by_dsm(qm);
	if (ret)
		dev_err(dev, "acpi_evaluate_dsm err.\n");

	return ret;
}

static void hpre_hw_error_disable(struct hpre *hpre)
{
	struct hisi_qm *qm = &hpre->qm;

	/* disable hpre hw error interrupts */
	writel(HPRE_CORE_INT_DISABLE, qm->io_base + HPRE_INT_MASK);
}

static void hpre_hw_error_enable(struct hpre *hpre)
{
	struct hisi_qm *qm = &hpre->qm;

	/* enable hpre hw error interrupts */
	writel(HPRE_CORE_INT_ENABLE, qm->io_base + HPRE_INT_MASK);
	writel(HPRE_HAC_RAS_CE_ENABLE, qm->io_base + HPRE_RAS_CE_ENB);
	writel(HPRE_HAC_RAS_NFE_ENABLE, qm->io_base + HPRE_RAS_NFE_ENB);
	writel(HPRE_HAC_RAS_FE_ENABLE, qm->io_base + HPRE_RAS_FE_ENB);
}

static int hpre_qm_pre_init(struct hisi_qm *qm, struct pci_dev *pdev)
{
	enum qm_hw_ver rev_id;

	rev_id = hisi_qm_get_hw_version(pdev);
	if (rev_id < 0)
		return -ENODEV;

	if (rev_id == QM_HW_V1) {
		pci_warn(pdev, "HPRE version 1 is not supported!\n");
		return -EINVAL;
	}

	qm->pdev = pdev;
	qm->ver = rev_id;
	qm->sqe_size = HPRE_SQE_SIZE;
	qm->dev_name = hpre_name;
	qm->qp_base = HPRE_PF_DEF_Q_BASE;
	qm->qp_num = hpre_pf_q_num;
	qm->use_dma_api = true;

	return 0;
}

static void hpre_hw_err_init(struct hpre *hpre)
{
	hisi_qm_hw_error_init(&hpre->qm, QM_BASE_CE, QM_BASE_NFE,
			      0, QM_DB_RANDOM_INVALID);
	hpre_hw_error_enable(hpre);
}

static int hpre_pf_probe_init(struct hpre *hpre)
{
	struct hisi_qm *qm = &hpre->qm;
	int ret;

	qm->ctrl_qp_num = HPRE_QUEUE_NUM_V2;

	ret = hpre_set_user_domain_and_cache(hpre);
	if (ret)
		return ret;

	hpre_hw_err_init(hpre);

	return 0;
}

static int hpre_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct hisi_qm *qm;
	struct hpre *hpre;
	int ret;

	hpre = devm_kzalloc(&pdev->dev, sizeof(*hpre), GFP_KERNEL);
	if (!hpre)
		return -ENOMEM;

	pci_set_drvdata(pdev, hpre);

	qm = &hpre->qm;
	ret = hpre_qm_pre_init(qm, pdev);
	if (ret)
		return ret;

	ret = hisi_qm_init(qm);
	if (ret)
		return ret;

	ret = hpre_pf_probe_init(hpre);
	if (ret)
		goto err_with_qm_init;

	ret = hisi_qm_start(qm);
	if (ret)
		goto err_with_err_init;

	hpre_add_to_list(hpre);

	ret = hpre_algs_register();
	if (ret < 0) {
		hpre_remove_from_list(hpre);
		pci_err(pdev, "fail to register algs to crypto!\n");
		goto err_with_qm_start;
	}
	return 0;

err_with_qm_start:
	hisi_qm_stop(qm);

err_with_err_init:
	hpre_hw_error_disable(hpre);

err_with_qm_init:
	hisi_qm_uninit(qm);

	return ret;
}

static void hpre_remove(struct pci_dev *pdev)
{
	struct hpre *hpre = pci_get_drvdata(pdev);
	struct hisi_qm *qm = &hpre->qm;

	hpre_algs_unregister();
	hpre_remove_from_list(hpre);
	hisi_qm_stop(qm);
	hpre_hw_error_disable(hpre);
	hisi_qm_uninit(qm);
}

static void hpre_log_hw_error(struct hpre *hpre, u32 err_sts)
{
	const struct hpre_hw_error *err = hpre_hw_errors;
	struct device *dev = &hpre->qm.pdev->dev;

	while (err->msg) {
		if (err->int_msk & err_sts)
			dev_warn(dev, "%s [error status=0x%x] found\n",
				 err->msg, err->int_msk);
		err++;
	}
}

static pci_ers_result_t hpre_hw_error_handle(struct hpre *hpre)
{
	u32 err_sts;

	/* read err sts */
	err_sts = readl(hpre->qm.io_base + HPRE_HAC_INT_STATUS);
	if (err_sts) {
		hpre_log_hw_error(hpre, err_sts);

		/* clear error interrupts */
		writel(err_sts, hpre->qm.io_base + HPRE_HAC_SOURCE_INT);
		return PCI_ERS_RESULT_NEED_RESET;
	}

	return PCI_ERS_RESULT_RECOVERED;
}

static pci_ers_result_t hpre_process_hw_error(struct pci_dev *pdev)
{
	struct hpre *hpre = pci_get_drvdata(pdev);
	pci_ers_result_t qm_ret, hpre_ret;

	/* log qm error */
	qm_ret = hisi_qm_hw_error_handle(&hpre->qm);

	/* log hpre error */
	hpre_ret = hpre_hw_error_handle(hpre);

	return (qm_ret == PCI_ERS_RESULT_NEED_RESET ||
		hpre_ret == PCI_ERS_RESULT_NEED_RESET) ?
		PCI_ERS_RESULT_NEED_RESET : PCI_ERS_RESULT_RECOVERED;
}

static pci_ers_result_t hpre_error_detected(struct pci_dev *pdev,
					    pci_channel_state_t state)
{
	pci_info(pdev, "PCI error detected, state(=%d)!!\n", state);
	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;

	return hpre_process_hw_error(pdev);
}

static const struct pci_error_handlers hpre_err_handler = {
	.error_detected		= hpre_error_detected,
};

static struct pci_driver hpre_pci_driver = {
	.name			= hpre_name,
	.id_table		= hpre_dev_ids,
	.probe			= hpre_probe,
	.remove			= hpre_remove,
	.err_handler		= &hpre_err_handler,
};

static int __init hpre_init(void)
{
	int ret;

	ret = pci_register_driver(&hpre_pci_driver);
	if (ret)
		pr_err("hpre: can't register hisi hpre driver.\n");

	return ret;
}

static void __exit hpre_exit(void)
{
	pci_unregister_driver(&hpre_pci_driver);
}

module_init(hpre_init);
module_exit(hpre_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Zaibo Xu <xuzaibo@huawei.com>");
MODULE_DESCRIPTION("Driver for HiSilicon HPRE accelerator");
