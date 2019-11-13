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
#include <linux/seq_file.h>
#include <linux/topology.h>

#include "sec.h"

#define SEC_QUEUE_NUM_V1		4096
#define SEC_QUEUE_NUM_V2		1024
#define SEC_PF_PCI_DEVICE_ID		0xa255

#define SEC_XTS_MIV_ENABLE_REG		0x301384
#define SEC_XTS_MIV_ENABLE_MSK		0x7FFFFFFF
#define SEC_XTS_MIV_DISABLE_MSK		0xFFFFFFFF
#define SEC_BD_ERR_CHK_EN1		0xfffff7fd
#define SEC_BD_ERR_CHK_EN2		0xffffbfff

#define SEC_SQE_SIZE			128
#define SEC_SQ_SIZE			(SEC_SQE_SIZE * QM_Q_DEPTH)
#define SEC_PF_DEF_Q_NUM		64
#define SEC_PF_DEF_Q_BASE		0
#define SEC_CTX_Q_NUM_DEF		24

#define SEC_ENGINE_PF_CFG_OFF		0x300000
#define SEC_ACC_COMMON_REG_OFF		0x1000
#define SEC_CORE_INT_SOURCE		0x301010
#define SEC_CORE_INT_MASK		0x301000
#define SEC_CORE_INT_STATUS		0x301008
#define SEC_CORE_SRAM_ECC_ERR_INFO	0x301C14
#define SEC_ECC_NUM(err)			(((err) >> 16) & 0xFF)
#define SEC_ECC_ADDR(err)			((err) >> 0)
#define SEC_CORE_INT_DISABLE		0x0
#define SEC_CORE_INT_ENABLE		0x1ff

#define SEC_RAS_CE_REG			0x50
#define SEC_RAS_FE_REG			0x54
#define SEC_RAS_NFE_REG			0x58
#define SEC_RAS_CE_ENB_MSK		0x88
#define SEC_RAS_FE_ENB_MSK		0x0
#define SEC_RAS_NFE_ENB_MSK		0x177
#define SEC_RAS_DISABLE			0x0
#define SEC_MEM_START_INIT_REG		0x0100
#define SEC_MEM_INIT_DONE_REG		0x0104
#define SEC_QM_ABNORMAL_INT_MASK	0x100004

#define SEC_CONTROL_REG			0x0200
#define SEC_TRNG_EN_SHIFT		8
#define SEC_CLK_GATE_ENABLE		BIT(3)
#define SEC_CLK_GATE_DISABLE		(~BIT(3))
#define SEC_AXI_SHUTDOWN_ENABLE	BIT(12)
#define SEC_AXI_SHUTDOWN_DISABLE	0xFFFFEFFF

#define SEC_INTERFACE_USER_CTRL0_REG	0x0220
#define SEC_INTERFACE_USER_CTRL1_REG	0x0224
#define SEC_BD_ERR_CHK_EN_REG1		0x0384
#define SEC_BD_ERR_CHK_EN_REG2		0x038c

#define SEC_USER0_SMMU_NORMAL		(BIT(23) | BIT(15))
#define SEC_USER1_SMMU_NORMAL		(BIT(31) | BIT(23) | BIT(15) | BIT(7))
#define SEC_CORE_INT_STATUS_M_ECC	BIT(2)

#define SEC_DELAY_10_US			10
#define SEC_POLL_TIMEOUT_US		1000

#define SEC_ADDR(qm, offset) ((qm)->io_base + (offset) + \
			     SEC_ENGINE_PF_CFG_OFF + SEC_ACC_COMMON_REG_OFF)

struct sec_hw_error {
	u32 int_msk;
	const char *msg;
};

static const char sec_name[] = "hisi_sec2";
static LIST_HEAD(sec_list);
static DEFINE_MUTEX(sec_list_lock);

static const struct sec_hw_error sec_hw_errors[] = {
	{.int_msk = BIT(0), .msg = "sec_axi_rresp_err_rint"},
	{.int_msk = BIT(1), .msg = "sec_axi_bresp_err_rint"},
	{.int_msk = BIT(2), .msg = "sec_ecc_2bit_err_rint"},
	{.int_msk = BIT(3), .msg = "sec_ecc_1bit_err_rint"},
	{.int_msk = BIT(4), .msg = "sec_req_trng_timeout_rint"},
	{.int_msk = BIT(5), .msg = "sec_fsm_hbeat_rint"},
	{.int_msk = BIT(6), .msg = "sec_channel_req_rng_timeout_rint"},
	{.int_msk = BIT(7), .msg = "sec_bd_err_rint"},
	{.int_msk = BIT(8), .msg = "sec_chain_buff_err_rint"},
	{ /* sentinel */ }
};

struct sec_dev *sec_find_device(int node)
{
#define SEC_NUMA_MAX_DISTANCE	100
	int min_distance = SEC_NUMA_MAX_DISTANCE;
	int dev_node = 0, free_qp_num = 0;
	struct sec_dev *sec, *ret = NULL;
	struct hisi_qm *qm;
	struct device *dev;

	mutex_lock(&sec_list_lock);
	list_for_each_entry(sec, &sec_list, list) {
		qm = &sec->qm;
		dev = &qm->pdev->dev;
#ifdef CONFIG_NUMA
		dev_node = dev->numa_node;
		if (dev_node < 0)
			dev_node = 0;
#endif
		if (node_distance(dev_node, node) < min_distance) {
			free_qp_num = hisi_qm_get_free_qp_num(qm);
			if (free_qp_num >= sec->ctx_q_num) {
				ret = sec;
				min_distance = node_distance(dev_node, node);
			}
		}
	}
	mutex_unlock(&sec_list_lock);

	return ret;
}

static int sec_pf_q_num_set(const char *val, const struct kernel_param *kp)
{
	struct pci_dev *pdev;
	u32 n, q_num;
	u8 rev_id;
	int ret;

	if (!val)
		return -EINVAL;

	pdev = pci_get_device(PCI_VENDOR_ID_HUAWEI,
			      SEC_PF_PCI_DEVICE_ID, NULL);
	if (!pdev) {
		q_num = min_t(u32, SEC_QUEUE_NUM_V1, SEC_QUEUE_NUM_V2);
		pr_info("No device, suppose queue number is %d!\n", q_num);
	} else {
		rev_id = pdev->revision;

		switch (rev_id) {
		case QM_HW_V1:
			q_num = SEC_QUEUE_NUM_V1;
			break;
		case QM_HW_V2:
			q_num = SEC_QUEUE_NUM_V2;
			break;
		default:
			return -EINVAL;
		}
	}

	ret = kstrtou32(val, 10, &n);
	if (ret || !n || n > q_num)
		return -EINVAL;

	return param_set_int(val, kp);
}

static const struct kernel_param_ops sec_pf_q_num_ops = {
	.set = sec_pf_q_num_set,
	.get = param_get_int,
};
static u32 pf_q_num = SEC_PF_DEF_Q_NUM;
module_param_cb(pf_q_num, &sec_pf_q_num_ops, &pf_q_num, 0444);
MODULE_PARM_DESC(pf_q_num, "Number of queues in PF(v1 0-4096, v2 0-1024)");

static int sec_ctx_q_num_set(const char *val, const struct kernel_param *kp)
{
	u32 ctx_q_num;
	int ret;

	if (!val)
		return -EINVAL;

	ret = kstrtou32(val, 10, &ctx_q_num);
	if (ret)
		return -EINVAL;

	if (!ctx_q_num || ctx_q_num > QM_Q_DEPTH || ctx_q_num & 0x1) {
		pr_err("ctx queue num[%u] is invalid!\n", ctx_q_num);
		return -EINVAL;
	}

	return param_set_int(val, kp);
}

static const struct kernel_param_ops sec_ctx_q_num_ops = {
	.set = sec_ctx_q_num_set,
	.get = param_get_int,
};
static u32 ctx_q_num = SEC_CTX_Q_NUM_DEF;
module_param_cb(ctx_q_num, &sec_ctx_q_num_ops, &ctx_q_num, 0444);
MODULE_PARM_DESC(ctx_q_num, "Number of queue in ctx (2, 4, 6, ..., 1024)");

static const struct pci_device_id sec_dev_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_HUAWEI, SEC_PF_PCI_DEVICE_ID) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, sec_dev_ids);

static inline void sec_add_to_list(struct sec_dev *sec)
{
	mutex_lock(&sec_list_lock);
	list_add_tail(&sec->list, &sec_list);
	mutex_unlock(&sec_list_lock);
}

static inline void sec_remove_from_list(struct sec_dev *sec)
{
	mutex_lock(&sec_list_lock);
	list_del(&sec->list);
	mutex_unlock(&sec_list_lock);
}

static u8 sec_get_endian(struct sec_dev *sec)
{
	struct hisi_qm *qm = &sec->qm;
	u32 reg;

	reg = readl_relaxed(qm->io_base + SEC_ENGINE_PF_CFG_OFF +
			    SEC_ACC_COMMON_REG_OFF + SEC_CONTROL_REG);

	/* BD little endian mode */
	if (!(reg & BIT(0)))
		return SEC_LE;

	/* BD 32-bits big endian mode */
	else if (!(reg & BIT(1)))
		return SEC_32BE;

	/* BD 64-bits big endian mode */
	else
		return SEC_64BE;
}

static int sec_engine_init(struct sec_dev *sec)
{
	struct hisi_qm *qm = &sec->qm;
	int ret;
	u32 reg;

	/* disable clock gate control */
	reg = readl_relaxed(SEC_ADDR(qm, SEC_CONTROL_REG));
	reg &= SEC_CLK_GATE_DISABLE;
	writel_relaxed(reg, SEC_ADDR(qm, SEC_CONTROL_REG));

	writel_relaxed(0x1, SEC_ADDR(qm, SEC_MEM_START_INIT_REG));

	ret = readl_relaxed_poll_timeout(SEC_ADDR(qm, SEC_MEM_INIT_DONE_REG),
					 reg, reg & 0x1, SEC_DELAY_10_US,
					 SEC_POLL_TIMEOUT_US);
	if (ret) {
		dev_err(&qm->pdev->dev, "fail to init sec mem\n");
		return ret;
	}

	reg = readl_relaxed(SEC_ADDR(qm, SEC_CONTROL_REG));
	reg |= (0x1 << SEC_TRNG_EN_SHIFT);
	writel_relaxed(reg, SEC_ADDR(qm, SEC_CONTROL_REG));

	reg = readl_relaxed(SEC_ADDR(qm, SEC_INTERFACE_USER_CTRL0_REG));
	reg |= SEC_USER0_SMMU_NORMAL;
	writel_relaxed(reg, SEC_ADDR(qm, SEC_INTERFACE_USER_CTRL0_REG));

	reg = readl_relaxed(SEC_ADDR(qm, SEC_INTERFACE_USER_CTRL1_REG));
	reg |= SEC_USER1_SMMU_NORMAL;
	writel_relaxed(reg, SEC_ADDR(qm, SEC_INTERFACE_USER_CTRL1_REG));

	writel_relaxed(SEC_BD_ERR_CHK_EN1,
		       SEC_ADDR(qm, SEC_BD_ERR_CHK_EN_REG1));
	writel_relaxed(SEC_BD_ERR_CHK_EN2,
		       SEC_ADDR(qm, SEC_BD_ERR_CHK_EN_REG2));

	/* enable clock gate control */
	reg = readl_relaxed(SEC_ADDR(qm, SEC_CONTROL_REG));
	reg |= SEC_CLK_GATE_ENABLE;
	writel_relaxed(reg, SEC_ADDR(qm, SEC_CONTROL_REG));

	/* config endian */
	reg = readl_relaxed(SEC_ADDR(qm, SEC_CONTROL_REG));
	reg |= sec_get_endian(sec);
	writel_relaxed(reg, SEC_ADDR(qm, SEC_CONTROL_REG));

	/* Enable sm4 xts mode multiple iv */
	writel_relaxed(SEC_XTS_MIV_ENABLE_MSK,
		       qm->io_base + SEC_XTS_MIV_ENABLE_REG);

	return 0;
}

static int sec_set_user_domain_and_cache(struct sec_dev *sec)
{
	struct hisi_qm *qm = &sec->qm;

	/* qm user domain */
	writel(AXUSER_BASE, qm->io_base + QM_ARUSER_M_CFG_1);
	writel(ARUSER_M_CFG_ENABLE, qm->io_base + QM_ARUSER_M_CFG_ENABLE);
	writel(AXUSER_BASE, qm->io_base + QM_AWUSER_M_CFG_1);
	writel(AWUSER_M_CFG_ENABLE, qm->io_base + QM_AWUSER_M_CFG_ENABLE);
	writel(WUSER_M_CFG_ENABLE, qm->io_base + QM_WUSER_M_CFG_ENABLE);

	/* qm cache */
	writel(AXI_M_CFG, qm->io_base + QM_AXI_M_CFG);
	writel(AXI_M_CFG_ENABLE, qm->io_base + QM_AXI_M_CFG_ENABLE);

	/* disable FLR triggered by BME(bus master enable) */
	writel(PEH_AXUSER_CFG, qm->io_base + QM_PEH_AXUSER_CFG);
	writel(PEH_AXUSER_CFG_ENABLE, qm->io_base + QM_PEH_AXUSER_CFG_ENABLE);

	/* enable sqc,cqc writeback */
	writel(SQC_CACHE_ENABLE | CQC_CACHE_ENABLE | SQC_CACHE_WB_ENABLE |
	       CQC_CACHE_WB_ENABLE | FIELD_PREP(SQC_CACHE_WB_THRD, 1) |
	       FIELD_PREP(CQC_CACHE_WB_THRD, 1), qm->io_base + QM_CACHE_CTL);

	return sec_engine_init(sec);
}

static void sec_hw_error_enable(struct sec_dev *sec)
{
	struct hisi_qm *qm = &sec->qm;
	u32 val;

	if (qm->ver == QM_HW_V1) {
		writel(SEC_CORE_INT_DISABLE, qm->io_base + SEC_CORE_INT_MASK);
		dev_info(&qm->pdev->dev, "V1 not support hw error handle\n");
		return;
	}

	val = readl(qm->io_base + SEC_CONTROL_REG);

	/* clear SEC hw error source if having */
	writel(SEC_CORE_INT_DISABLE, qm->io_base + SEC_CORE_INT_SOURCE);

	/* enable SEC hw error interrupts */
	writel(SEC_CORE_INT_ENABLE, qm->io_base + SEC_CORE_INT_MASK);

	/* enable RAS int */
	writel(SEC_RAS_CE_ENB_MSK, qm->io_base + SEC_RAS_CE_REG);
	writel(SEC_RAS_FE_ENB_MSK, qm->io_base + SEC_RAS_FE_REG);
	writel(SEC_RAS_NFE_ENB_MSK, qm->io_base + SEC_RAS_NFE_REG);

	/* enable SEC block master OOO when m-bit error occur */
	val = val | SEC_AXI_SHUTDOWN_ENABLE;

	writel(val, qm->io_base + SEC_CONTROL_REG);
}

static void sec_hw_error_disable(struct sec_dev *sec)
{
	struct hisi_qm *qm = &sec->qm;
	u32 val;

	val = readl(qm->io_base + SEC_CONTROL_REG);

	/* disable RAS int */
	writel(SEC_RAS_DISABLE, qm->io_base + SEC_RAS_CE_REG);
	writel(SEC_RAS_DISABLE, qm->io_base + SEC_RAS_FE_REG);
	writel(SEC_RAS_DISABLE, qm->io_base + SEC_RAS_NFE_REG);

	/* disable SEC hw error interrupts */
	writel(SEC_CORE_INT_DISABLE, qm->io_base + SEC_CORE_INT_MASK);

	/* disable SEC block master OOO when m-bit error occur */
	val = val & SEC_AXI_SHUTDOWN_DISABLE;

	writel(val, qm->io_base + SEC_CONTROL_REG);
}

static void sec_hw_error_init(struct sec_dev *sec)
{
	hisi_qm_hw_error_init(&sec->qm, QM_BASE_CE,
			      QM_BASE_NFE | QM_ACC_DO_TASK_TIMEOUT
			      | QM_ACC_WB_NOT_READY_TIMEOUT, 0,
			      QM_DB_RANDOM_INVALID);
	sec_hw_error_enable(sec);
}

static void sec_hw_error_uninit(struct sec_dev *sec)
{
	sec_hw_error_disable(sec);
	writel(GENMASK(12, 0), sec->qm.io_base + SEC_QM_ABNORMAL_INT_MASK);
}

static int sec_pf_probe_init(struct sec_dev *sec)
{
	struct hisi_qm *qm = &sec->qm;
	int ret;

	switch (qm->ver) {
	case QM_HW_V1:
		qm->ctrl_qp_num = SEC_QUEUE_NUM_V1;
		break;

	case QM_HW_V2:
		qm->ctrl_qp_num = SEC_QUEUE_NUM_V2;
		break;

	default:
		return -EINVAL;
	}

	ret = sec_set_user_domain_and_cache(sec);
	if (ret)
		return ret;

	sec_hw_error_init(sec);

	return 0;
}

static int sec_qm_init(struct hisi_qm *qm, struct pci_dev *pdev)
{
	enum qm_hw_ver rev_id;

	rev_id = hisi_qm_get_hw_version(pdev);
	if (rev_id == QM_HW_UNKNOWN)
		return -ENODEV;

	qm->pdev = pdev;
	qm->ver = rev_id;

	qm->sqe_size = SEC_SQE_SIZE;
	qm->dev_name = sec_name;
	qm->fun_type = (pdev->device == SEC_PF_PCI_DEVICE_ID) ?
			QM_HW_PF : QM_HW_VF;
	qm->use_dma_api = true;

	return hisi_qm_init(qm);
}

static void sec_qm_uninit(struct hisi_qm *qm)
{
	hisi_qm_uninit(qm);
}

static int sec_probe_init(struct hisi_qm *qm, struct sec_dev *sec)
{
	qm->qp_base = SEC_PF_DEF_Q_BASE;
	qm->qp_num = pf_q_num;

	return sec_pf_probe_init(sec);
}

static void sec_probe_uninit(struct sec_dev *sec)
{
	sec_hw_error_uninit(sec);
}

static int sec_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct sec_dev *sec;
	struct hisi_qm *qm;
	int ret;

	sec = devm_kzalloc(&pdev->dev, sizeof(*sec), GFP_KERNEL);
	if (!sec)
		return -ENOMEM;

	pci_set_drvdata(pdev, sec);

	sec->ctx_q_num = ctx_q_num;

	qm = &sec->qm;

	ret = sec_qm_init(qm, pdev);
	if (ret) {
		pci_err(pdev, "Failed to pre init qm!\n");
		return ret;
	}

	ret = sec_probe_init(qm, sec);
	if (ret) {
		pci_err(pdev, "Failed to probe!\n");
		goto err_qm_uninit;
	}

	ret = hisi_qm_start(qm);
	if (ret) {
		pci_err(pdev, "Failed to start sec qm!\n");
		goto err_probe_uninit;
	}

	sec_add_to_list(sec);

	ret = sec_register_to_crypto();
	if (ret < 0) {
		pr_err("Failed to register driver to crypto.\n");
		goto err_remove_from_list;
	}

	return 0;

err_remove_from_list:
	sec_remove_from_list(sec);
	hisi_qm_stop(qm);

err_probe_uninit:
	sec_probe_uninit(sec);

err_qm_uninit:
	sec_qm_uninit(qm);

	return ret;
}

static void sec_remove(struct pci_dev *pdev)
{
	struct sec_dev *sec = pci_get_drvdata(pdev);
	struct hisi_qm *qm = &sec->qm;

	sec_unregister_from_crypto();

	sec_remove_from_list(sec);

	(void)hisi_qm_stop(qm);

	sec_probe_uninit(sec);

	sec_qm_uninit(qm);
}

static void sec_log_hw_error(struct sec_dev *sec, u32 err_sts)
{
	const struct sec_hw_error *errs = sec_hw_errors;
	struct device *dev = &sec->qm.pdev->dev;
	u32 err_val;

	while (errs->msg) {
		if (errs->int_msk & err_sts) {
			dev_err(dev, "%s [error status=0x%x] found\n",
				errs->msg, errs->int_msk);

			if (SEC_CORE_INT_STATUS_M_ECC & err_sts) {
				err_val = readl(sec->qm.io_base +
						SEC_CORE_SRAM_ECC_ERR_INFO);
				dev_err(dev, "multi ecc sram num=0x%x\n",
					SEC_ECC_NUM(err_val));
				dev_err(dev, "multi ecc sram addr=0x%x\n",
					SEC_ECC_ADDR(err_val));
			}
		}
		errs++;
	}
}

static pci_ers_result_t sec_hw_error_handle(struct sec_dev *sec)
{
	u32 err_sts;

	/* read err sts */
	err_sts = readl(sec->qm.io_base + SEC_CORE_INT_STATUS);
	if (err_sts) {
		sec_log_hw_error(sec, err_sts);

		/* clear error interrupts */
		writel(err_sts, sec->qm.io_base + SEC_CORE_INT_SOURCE);

		return PCI_ERS_RESULT_NEED_RESET;
	}

	return PCI_ERS_RESULT_RECOVERED;
}

static pci_ers_result_t sec_process_hw_error(struct pci_dev *pdev)
{
	struct sec_dev *sec = pci_get_drvdata(pdev);
	pci_ers_result_t qm_ret, sec_ret;

	if (!sec) {
		pci_err(pdev, "Can't recover error during device init\n");
		return PCI_ERS_RESULT_NONE;
	}

	/* log qm error */
	qm_ret = hisi_qm_hw_error_handle(&sec->qm);

	/* log sec error */
	sec_ret = sec_hw_error_handle(sec);

	return (qm_ret == PCI_ERS_RESULT_NEED_RESET ||
		sec_ret == PCI_ERS_RESULT_NEED_RESET) ?
		PCI_ERS_RESULT_NEED_RESET : PCI_ERS_RESULT_RECOVERED;
}

static pci_ers_result_t sec_error_detected(struct pci_dev *pdev,
					   pci_channel_state_t state)
{
	pci_info(pdev, "PCI error detected, state(=%d)!!\n", state);
	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;

	return sec_process_hw_error(pdev);
}

static const struct pci_error_handlers sec_err_handler = {
	.error_detected = sec_error_detected,
};

static struct pci_driver sec_pci_driver = {
	.name = "hisi_sec2",
	.id_table = sec_dev_ids,
	.probe = sec_probe,
	.remove = sec_remove,
	.err_handler = &sec_err_handler,
};

static int __init sec_init(void)
{
	int ret;

	ret = pci_register_driver(&sec_pci_driver);
	if (ret < 0) {
		pr_err("Failed to register pci driver.\n");
		return ret;
	}

	return 0;
}

static void __exit sec_exit(void)
{
	pci_unregister_driver(&sec_pci_driver);
}

module_init(sec_init);
module_exit(sec_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Zaibo Xu <xuzaibo@huawei.com>");
MODULE_AUTHOR("Longfang Liu <liulongfang@huawei.com>");
MODULE_AUTHOR("Wei Zhang <zhangwei375@huawei.com>");
MODULE_DESCRIPTION("Driver for HiSilicon SEC accelerator");
