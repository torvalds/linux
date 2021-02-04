// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018-2019 HiSilicon Limited. */
#include <linux/acpi.h>
#include <linux/aer.h>
#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/topology.h>
#include <linux/uacce.h>
#include "hpre.h"

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
#define HPRE_RDCHN_INI_ST		0x301a00
#define HPRE_CLSTR_BASE			0x302000
#define HPRE_CORE_EN_OFFSET		0x04
#define HPRE_CORE_INI_CFG_OFFSET	0x20
#define HPRE_CORE_INI_STATUS_OFFSET	0x80
#define HPRE_CORE_HTBT_WARN_OFFSET	0x8c
#define HPRE_CORE_IS_SCHD_OFFSET	0x90

#define HPRE_RAS_CE_ENB			0x301410
#define HPRE_HAC_RAS_CE_ENABLE		(BIT(0) | BIT(22) | BIT(23))
#define HPRE_RAS_NFE_ENB		0x301414
#define HPRE_HAC_RAS_NFE_ENABLE		0x3ffffe
#define HPRE_RAS_FE_ENB			0x301418
#define HPRE_HAC_RAS_FE_ENABLE		0

#define HPRE_CORE_ENB		(HPRE_CLSTR_BASE + HPRE_CORE_EN_OFFSET)
#define HPRE_CORE_INI_CFG	(HPRE_CLSTR_BASE + HPRE_CORE_INI_CFG_OFFSET)
#define HPRE_CORE_INI_STATUS (HPRE_CLSTR_BASE + HPRE_CORE_INI_STATUS_OFFSET)
#define HPRE_HAC_ECC1_CNT		0x301a04
#define HPRE_HAC_ECC2_CNT		0x301a08
#define HPRE_HAC_INT_STATUS		0x301800
#define HPRE_HAC_SOURCE_INT		0x301600
#define HPRE_CLSTR_ADDR_INTRVL		0x1000
#define HPRE_CLUSTER_INQURY		0x100
#define HPRE_CLSTR_ADDR_INQRY_RSLT	0x104
#define HPRE_TIMEOUT_ABNML_BIT		6
#define HPRE_PASID_EN_BIT		9
#define HPRE_REG_RD_INTVRL_US		10
#define HPRE_REG_RD_TMOUT_US		1000
#define HPRE_DBGFS_VAL_MAX_LEN		20
#define HPRE_PCI_DEVICE_ID		0xa258
#define HPRE_PCI_VF_DEVICE_ID		0xa259
#define HPRE_ADDR(qm, offset)		((qm)->io_base + (offset))
#define HPRE_QM_USR_CFG_MASK		0xfffffffe
#define HPRE_QM_AXI_CFG_MASK		0xffff
#define HPRE_QM_VFG_AX_MASK		0xff
#define HPRE_BD_USR_MASK		0x3
#define HPRE_CLUSTER_CORE_MASK_V2	0xf
#define HPRE_CLUSTER_CORE_MASK_V3	0xff

#define HPRE_AM_OOO_SHUTDOWN_ENB	0x301044
#define HPRE_AM_OOO_SHUTDOWN_ENABLE	BIT(0)
#define HPRE_WR_MSI_PORT		BIT(2)

#define HPRE_CORE_ECC_2BIT_ERR		BIT(1)
#define HPRE_OOO_ECC_2BIT_ERR		BIT(5)

#define HPRE_QM_BME_FLR			BIT(7)
#define HPRE_QM_PM_FLR			BIT(11)
#define HPRE_QM_SRIOV_FLR		BIT(12)

#define HPRE_CLUSTERS_NUM(qm)		\
	(((qm)->ver >= QM_HW_V3) ? HPRE_CLUSTERS_NUM_V3 : HPRE_CLUSTERS_NUM_V2)
#define HPRE_CLUSTER_CORE_MASK(qm)	\
	(((qm)->ver >= QM_HW_V3) ? HPRE_CLUSTER_CORE_MASK_V3 :\
		HPRE_CLUSTER_CORE_MASK_V2)
#define HPRE_VIA_MSI_DSM		1
#define HPRE_SQE_MASK_OFFSET		8
#define HPRE_SQE_MASK_LEN		24

static const char hpre_name[] = "hisi_hpre";
static struct dentry *hpre_debugfs_root;
static const struct pci_device_id hpre_dev_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_HUAWEI, HPRE_PCI_DEVICE_ID) },
	{ PCI_DEVICE(PCI_VENDOR_ID_HUAWEI, HPRE_PCI_VF_DEVICE_ID) },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, hpre_dev_ids);

struct hpre_hw_error {
	u32 int_msk;
	const char *msg;
};

static struct hisi_qm_list hpre_devices = {
	.register_to_crypto	= hpre_algs_register,
	.unregister_from_crypto	= hpre_algs_unregister,
};

static const char * const hpre_debug_file_name[] = {
	[HPRE_CURRENT_QM]   = "current_qm",
	[HPRE_CLEAR_ENABLE] = "rdclr_en",
	[HPRE_CLUSTER_CTRL] = "cluster_ctrl",
};

static const struct hpre_hw_error hpre_hw_errors[] = {
	{ .int_msk = BIT(0), .msg = "core_ecc_1bit_err_int_set" },
	{ .int_msk = BIT(1), .msg = "core_ecc_2bit_err_int_set" },
	{ .int_msk = BIT(2), .msg = "dat_wb_poison_int_set" },
	{ .int_msk = BIT(3), .msg = "dat_rd_poison_int_set" },
	{ .int_msk = BIT(4), .msg = "bd_rd_poison_int_set" },
	{ .int_msk = BIT(5), .msg = "ooo_ecc_2bit_err_int_set" },
	{ .int_msk = BIT(6), .msg = "cluster1_shb_timeout_int_set" },
	{ .int_msk = BIT(7), .msg = "cluster2_shb_timeout_int_set" },
	{ .int_msk = BIT(8), .msg = "cluster3_shb_timeout_int_set" },
	{ .int_msk = BIT(9), .msg = "cluster4_shb_timeout_int_set" },
	{ .int_msk = GENMASK(15, 10), .msg = "ooo_rdrsp_err_int_set" },
	{ .int_msk = GENMASK(21, 16), .msg = "ooo_wrrsp_err_int_set" },
	{ .int_msk = BIT(22), .msg = "pt_rng_timeout_int_set"},
	{ .int_msk = BIT(23), .msg = "sva_fsm_timeout_int_set"},
	{
		/* sentinel */
	}
};

static const u64 hpre_cluster_offsets[] = {
	[HPRE_CLUSTER0] =
		HPRE_CLSTR_BASE + HPRE_CLUSTER0 * HPRE_CLSTR_ADDR_INTRVL,
	[HPRE_CLUSTER1] =
		HPRE_CLSTR_BASE + HPRE_CLUSTER1 * HPRE_CLSTR_ADDR_INTRVL,
	[HPRE_CLUSTER2] =
		HPRE_CLSTR_BASE + HPRE_CLUSTER2 * HPRE_CLSTR_ADDR_INTRVL,
	[HPRE_CLUSTER3] =
		HPRE_CLSTR_BASE + HPRE_CLUSTER3 * HPRE_CLSTR_ADDR_INTRVL,
};

static const struct debugfs_reg32 hpre_cluster_dfx_regs[] = {
	{"CORES_EN_STATUS          ",  HPRE_CORE_EN_OFFSET},
	{"CORES_INI_CFG              ",  HPRE_CORE_INI_CFG_OFFSET},
	{"CORES_INI_STATUS         ",  HPRE_CORE_INI_STATUS_OFFSET},
	{"CORES_HTBT_WARN         ",  HPRE_CORE_HTBT_WARN_OFFSET},
	{"CORES_IS_SCHD               ",  HPRE_CORE_IS_SCHD_OFFSET},
};

static const struct debugfs_reg32 hpre_com_dfx_regs[] = {
	{"READ_CLR_EN          ",  HPRE_CTRL_CNT_CLR_CE},
	{"AXQOS                   ",  HPRE_VFG_AXQOS},
	{"AWUSR_CFG              ",  HPRE_AWUSR_FP_CFG},
	{"QM_ARUSR_MCFG1           ",  QM_ARUSER_M_CFG_1},
	{"QM_AWUSR_MCFG1           ",  QM_AWUSER_M_CFG_1},
	{"BD_ENDIAN               ",  HPRE_BD_ENDIAN},
	{"ECC_CHECK_CTRL       ",  HPRE_ECC_BYPASS},
	{"RAS_INT_WIDTH       ",  HPRE_RAS_WIDTH_CFG},
	{"POISON_BYPASS       ",  HPRE_POISON_BYPASS},
	{"BD_ARUSER               ",  HPRE_BD_ARUSR_CFG},
	{"BD_AWUSER               ",  HPRE_BD_AWUSR_CFG},
	{"DATA_ARUSER            ",  HPRE_DATA_RUSER_CFG},
	{"DATA_AWUSER           ",  HPRE_DATA_WUSER_CFG},
	{"INT_STATUS               ",  HPRE_INT_STATUS},
};

static const char *hpre_dfx_files[HPRE_DFX_FILE_NUM] = {
	"send_cnt",
	"recv_cnt",
	"send_fail_cnt",
	"send_busy_cnt",
	"over_thrhld_cnt",
	"overtime_thrhld",
	"invalid_req_cnt"
};

static const struct kernel_param_ops hpre_uacce_mode_ops = {
	.set = uacce_mode_set,
	.get = param_get_int,
};

/*
 * uacce_mode = 0 means hpre only register to crypto,
 * uacce_mode = 1 means hpre both register to crypto and uacce.
 */
static u32 uacce_mode = UACCE_MODE_NOUACCE;
module_param_cb(uacce_mode, &hpre_uacce_mode_ops, &uacce_mode, 0444);
MODULE_PARM_DESC(uacce_mode, UACCE_MODE_DESC);

static int pf_q_num_set(const char *val, const struct kernel_param *kp)
{
	return q_num_set(val, kp, HPRE_PCI_DEVICE_ID);
}

static const struct kernel_param_ops hpre_pf_q_num_ops = {
	.set = pf_q_num_set,
	.get = param_get_int,
};

static u32 pf_q_num = HPRE_PF_DEF_Q_NUM;
module_param_cb(pf_q_num, &hpre_pf_q_num_ops, &pf_q_num, 0444);
MODULE_PARM_DESC(pf_q_num, "Number of queues in PF of CS(2-1024)");

static const struct kernel_param_ops vfs_num_ops = {
	.set = vfs_num_set,
	.get = param_get_int,
};

static u32 vfs_num;
module_param_cb(vfs_num, &vfs_num_ops, &vfs_num, 0444);
MODULE_PARM_DESC(vfs_num, "Number of VFs to enable(1-63), 0(default)");

struct hisi_qp *hpre_create_qp(void)
{
	int node = cpu_to_node(smp_processor_id());
	struct hisi_qp *qp = NULL;
	int ret;

	ret = hisi_qm_alloc_qps_node(&hpre_devices, 1, 0, node, &qp);
	if (!ret)
		return qp;

	return NULL;
}

static void hpre_pasid_enable(struct hisi_qm *qm)
{
	u32 val;

	val = readl_relaxed(qm->io_base + HPRE_DATA_RUSER_CFG);
	val |= BIT(HPRE_PASID_EN_BIT);
	writel_relaxed(val, qm->io_base + HPRE_DATA_RUSER_CFG);
	val = readl_relaxed(qm->io_base + HPRE_DATA_WUSER_CFG);
	val |= BIT(HPRE_PASID_EN_BIT);
	writel_relaxed(val, qm->io_base + HPRE_DATA_WUSER_CFG);
}

static void hpre_pasid_disable(struct hisi_qm *qm)
{
	u32 val;

	val = readl_relaxed(qm->io_base +  HPRE_DATA_RUSER_CFG);
	val &= ~BIT(HPRE_PASID_EN_BIT);
	writel_relaxed(val, qm->io_base + HPRE_DATA_RUSER_CFG);
	val = readl_relaxed(qm->io_base + HPRE_DATA_WUSER_CFG);
	val &= ~BIT(HPRE_PASID_EN_BIT);
	writel_relaxed(val, qm->io_base + HPRE_DATA_WUSER_CFG);
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

static int hpre_set_cluster(struct hisi_qm *qm)
{
	u32 cluster_core_mask = HPRE_CLUSTER_CORE_MASK(qm);
	u8 clusters_num = HPRE_CLUSTERS_NUM(qm);
	struct device *dev = &qm->pdev->dev;
	unsigned long offset;
	u32 val = 0;
	int ret, i;

	for (i = 0; i < clusters_num; i++) {
		offset = i * HPRE_CLSTR_ADDR_INTRVL;

		/* clusters initiating */
		writel(cluster_core_mask,
		       HPRE_ADDR(qm, offset + HPRE_CORE_ENB));
		writel(0x1, HPRE_ADDR(qm, offset + HPRE_CORE_INI_CFG));
		ret = readl_relaxed_poll_timeout(HPRE_ADDR(qm, offset +
					HPRE_CORE_INI_STATUS), val,
					((val & cluster_core_mask) ==
					cluster_core_mask),
					HPRE_REG_RD_INTVRL_US,
					HPRE_REG_RD_TMOUT_US);
		if (ret) {
			dev_err(dev,
				"cluster %d int st status timeout!\n", i);
			return -ETIMEDOUT;
		}
	}

	return 0;
}

/*
 * For Kunpeng 920, we shoul disable FLR triggered by hardware (BME/PM/SRIOV).
 * Or it may stay in D3 state when we bind and unbind hpre quickly,
 * as it does FLR triggered by hardware.
 */
static void disable_flr_of_bme(struct hisi_qm *qm)
{
	u32 val;

	val = readl(HPRE_ADDR(qm, QM_PEH_AXUSER_CFG));
	val &= ~(HPRE_QM_BME_FLR | HPRE_QM_SRIOV_FLR);
	val |= HPRE_QM_PM_FLR;
	writel(val, HPRE_ADDR(qm, QM_PEH_AXUSER_CFG));
	writel(PEH_AXUSER_CFG_ENABLE, HPRE_ADDR(qm, QM_PEH_AXUSER_CFG_ENABLE));
}

static int hpre_set_user_domain_and_cache(struct hisi_qm *qm)
{
	struct device *dev = &qm->pdev->dev;
	u32 val;
	int ret;

	writel(HPRE_QM_USR_CFG_MASK, HPRE_ADDR(qm, QM_ARUSER_M_CFG_ENABLE));
	writel(HPRE_QM_USR_CFG_MASK, HPRE_ADDR(qm, QM_AWUSER_M_CFG_ENABLE));
	writel_relaxed(HPRE_QM_AXI_CFG_MASK, HPRE_ADDR(qm, QM_AXI_M_CFG));

	/* HPRE need more time, we close this interrupt */
	val = readl_relaxed(HPRE_ADDR(qm, HPRE_QM_ABNML_INT_MASK));
	val |= BIT(HPRE_TIMEOUT_ABNML_BIT);
	writel_relaxed(val, HPRE_ADDR(qm, HPRE_QM_ABNML_INT_MASK));

	writel(0x1, HPRE_ADDR(qm, HPRE_TYPES_ENB));
	writel(HPRE_QM_VFG_AX_MASK, HPRE_ADDR(qm, HPRE_VFG_AXCACHE));
	writel(0x0, HPRE_ADDR(qm, HPRE_BD_ENDIAN));
	writel(0x0, HPRE_ADDR(qm, HPRE_INT_MASK));
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

	ret = hpre_set_cluster(qm);
	if (ret)
		return -ETIMEDOUT;

	/* This setting is only needed by Kunpeng 920. */
	if (qm->ver == QM_HW_V2) {
		ret = hpre_cfg_by_dsm(qm);
		if (ret)
			dev_err(dev, "acpi_evaluate_dsm err.\n");

		disable_flr_of_bme(qm);

		/* Enable data buffer pasid */
		if (qm->use_sva)
			hpre_pasid_enable(qm);
	}

	return ret;
}

static void hpre_cnt_regs_clear(struct hisi_qm *qm)
{
	u8 clusters_num = HPRE_CLUSTERS_NUM(qm);
	unsigned long offset;
	int i;

	/* clear current_qm */
	writel(0x0, qm->io_base + QM_DFX_MB_CNT_VF);
	writel(0x0, qm->io_base + QM_DFX_DB_CNT_VF);

	/* clear clusterX/cluster_ctrl */
	for (i = 0; i < clusters_num; i++) {
		offset = HPRE_CLSTR_BASE + i * HPRE_CLSTR_ADDR_INTRVL;
		writel(0x0, qm->io_base + offset + HPRE_CLUSTER_INQURY);
	}

	/* clear rdclr_en */
	writel(0x0, qm->io_base + HPRE_CTRL_CNT_CLR_CE);

	hisi_qm_debug_regs_clear(qm);
}

static void hpre_hw_error_disable(struct hisi_qm *qm)
{
	u32 val;

	/* disable hpre hw error interrupts */
	writel(HPRE_CORE_INT_DISABLE, qm->io_base + HPRE_INT_MASK);

	/* disable HPRE block master OOO when m-bit error occur */
	val = readl(qm->io_base + HPRE_AM_OOO_SHUTDOWN_ENB);
	val &= ~HPRE_AM_OOO_SHUTDOWN_ENABLE;
	writel(val, qm->io_base + HPRE_AM_OOO_SHUTDOWN_ENB);
}

static void hpre_hw_error_enable(struct hisi_qm *qm)
{
	u32 val;

	/* clear HPRE hw error source if having */
	writel(HPRE_CORE_INT_DISABLE, qm->io_base + HPRE_HAC_SOURCE_INT);

	/* enable hpre hw error interrupts */
	writel(HPRE_CORE_INT_ENABLE, qm->io_base + HPRE_INT_MASK);
	writel(HPRE_HAC_RAS_CE_ENABLE, qm->io_base + HPRE_RAS_CE_ENB);
	writel(HPRE_HAC_RAS_NFE_ENABLE, qm->io_base + HPRE_RAS_NFE_ENB);
	writel(HPRE_HAC_RAS_FE_ENABLE, qm->io_base + HPRE_RAS_FE_ENB);

	/* enable HPRE block master OOO when m-bit error occur */
	val = readl(qm->io_base + HPRE_AM_OOO_SHUTDOWN_ENB);
	val |= HPRE_AM_OOO_SHUTDOWN_ENABLE;
	writel(val, qm->io_base + HPRE_AM_OOO_SHUTDOWN_ENB);
}

static inline struct hisi_qm *hpre_file_to_qm(struct hpre_debugfs_file *file)
{
	struct hpre *hpre = container_of(file->debug, struct hpre, debug);

	return &hpre->qm;
}

static u32 hpre_current_qm_read(struct hpre_debugfs_file *file)
{
	struct hisi_qm *qm = hpre_file_to_qm(file);

	return readl(qm->io_base + QM_DFX_MB_CNT_VF);
}

static int hpre_current_qm_write(struct hpre_debugfs_file *file, u32 val)
{
	struct hisi_qm *qm = hpre_file_to_qm(file);
	u32 num_vfs = qm->vfs_num;
	u32 vfq_num, tmp;

	if (val > num_vfs)
		return -EINVAL;

	/* According PF or VF Dev ID to calculation curr_qm_qp_num and store */
	if (val == 0) {
		qm->debug.curr_qm_qp_num = qm->qp_num;
	} else {
		vfq_num = (qm->ctrl_qp_num - qm->qp_num) / num_vfs;
		if (val == num_vfs) {
			qm->debug.curr_qm_qp_num =
			qm->ctrl_qp_num - qm->qp_num - (num_vfs - 1) * vfq_num;
		} else {
			qm->debug.curr_qm_qp_num = vfq_num;
		}
	}

	writel(val, qm->io_base + QM_DFX_MB_CNT_VF);
	writel(val, qm->io_base + QM_DFX_DB_CNT_VF);

	tmp = val |
	      (readl(qm->io_base + QM_DFX_SQE_CNT_VF_SQN) & CURRENT_Q_MASK);
	writel(tmp, qm->io_base + QM_DFX_SQE_CNT_VF_SQN);

	tmp = val |
	      (readl(qm->io_base + QM_DFX_CQE_CNT_VF_CQN) & CURRENT_Q_MASK);
	writel(tmp, qm->io_base + QM_DFX_CQE_CNT_VF_CQN);

	return  0;
}

static u32 hpre_clear_enable_read(struct hpre_debugfs_file *file)
{
	struct hisi_qm *qm = hpre_file_to_qm(file);

	return readl(qm->io_base + HPRE_CTRL_CNT_CLR_CE) &
	       HPRE_CTRL_CNT_CLR_CE_BIT;
}

static int hpre_clear_enable_write(struct hpre_debugfs_file *file, u32 val)
{
	struct hisi_qm *qm = hpre_file_to_qm(file);
	u32 tmp;

	if (val != 1 && val != 0)
		return -EINVAL;

	tmp = (readl(qm->io_base + HPRE_CTRL_CNT_CLR_CE) &
	       ~HPRE_CTRL_CNT_CLR_CE_BIT) | val;
	writel(tmp, qm->io_base + HPRE_CTRL_CNT_CLR_CE);

	return  0;
}

static u32 hpre_cluster_inqry_read(struct hpre_debugfs_file *file)
{
	struct hisi_qm *qm = hpre_file_to_qm(file);
	int cluster_index = file->index - HPRE_CLUSTER_CTRL;
	unsigned long offset = HPRE_CLSTR_BASE +
			       cluster_index * HPRE_CLSTR_ADDR_INTRVL;

	return readl(qm->io_base + offset + HPRE_CLSTR_ADDR_INQRY_RSLT);
}

static int hpre_cluster_inqry_write(struct hpre_debugfs_file *file, u32 val)
{
	struct hisi_qm *qm = hpre_file_to_qm(file);
	int cluster_index = file->index - HPRE_CLUSTER_CTRL;
	unsigned long offset = HPRE_CLSTR_BASE + cluster_index *
			       HPRE_CLSTR_ADDR_INTRVL;

	writel(val, qm->io_base + offset + HPRE_CLUSTER_INQURY);

	return  0;
}

static ssize_t hpre_ctrl_debug_read(struct file *filp, char __user *buf,
				    size_t count, loff_t *pos)
{
	struct hpre_debugfs_file *file = filp->private_data;
	char tbuf[HPRE_DBGFS_VAL_MAX_LEN];
	u32 val;
	int ret;

	spin_lock_irq(&file->lock);
	switch (file->type) {
	case HPRE_CURRENT_QM:
		val = hpre_current_qm_read(file);
		break;
	case HPRE_CLEAR_ENABLE:
		val = hpre_clear_enable_read(file);
		break;
	case HPRE_CLUSTER_CTRL:
		val = hpre_cluster_inqry_read(file);
		break;
	default:
		spin_unlock_irq(&file->lock);
		return -EINVAL;
	}
	spin_unlock_irq(&file->lock);
	ret = snprintf(tbuf, HPRE_DBGFS_VAL_MAX_LEN, "%u\n", val);
	return simple_read_from_buffer(buf, count, pos, tbuf, ret);
}

static ssize_t hpre_ctrl_debug_write(struct file *filp, const char __user *buf,
				     size_t count, loff_t *pos)
{
	struct hpre_debugfs_file *file = filp->private_data;
	char tbuf[HPRE_DBGFS_VAL_MAX_LEN];
	unsigned long val;
	int len, ret;

	if (*pos != 0)
		return 0;

	if (count >= HPRE_DBGFS_VAL_MAX_LEN)
		return -ENOSPC;

	len = simple_write_to_buffer(tbuf, HPRE_DBGFS_VAL_MAX_LEN - 1,
				     pos, buf, count);
	if (len < 0)
		return len;

	tbuf[len] = '\0';
	if (kstrtoul(tbuf, 0, &val))
		return -EFAULT;

	spin_lock_irq(&file->lock);
	switch (file->type) {
	case HPRE_CURRENT_QM:
		ret = hpre_current_qm_write(file, val);
		if (ret)
			goto err_input;
		break;
	case HPRE_CLEAR_ENABLE:
		ret = hpre_clear_enable_write(file, val);
		if (ret)
			goto err_input;
		break;
	case HPRE_CLUSTER_CTRL:
		ret = hpre_cluster_inqry_write(file, val);
		if (ret)
			goto err_input;
		break;
	default:
		ret = -EINVAL;
		goto err_input;
	}
	spin_unlock_irq(&file->lock);

	return count;

err_input:
	spin_unlock_irq(&file->lock);
	return ret;
}

static const struct file_operations hpre_ctrl_debug_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = hpre_ctrl_debug_read,
	.write = hpre_ctrl_debug_write,
};

static int hpre_debugfs_atomic64_get(void *data, u64 *val)
{
	struct hpre_dfx *dfx_item = data;

	*val = atomic64_read(&dfx_item->value);

	return 0;
}

static int hpre_debugfs_atomic64_set(void *data, u64 val)
{
	struct hpre_dfx *dfx_item = data;
	struct hpre_dfx *hpre_dfx = NULL;

	if (dfx_item->type == HPRE_OVERTIME_THRHLD) {
		hpre_dfx = dfx_item - HPRE_OVERTIME_THRHLD;
		atomic64_set(&hpre_dfx[HPRE_OVER_THRHLD_CNT].value, 0);
	} else if (val) {
		return -EINVAL;
	}

	atomic64_set(&dfx_item->value, val);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(hpre_atomic64_ops, hpre_debugfs_atomic64_get,
			 hpre_debugfs_atomic64_set, "%llu\n");

static int hpre_create_debugfs_file(struct hisi_qm *qm, struct dentry *dir,
				    enum hpre_ctrl_dbgfs_file type, int indx)
{
	struct hpre *hpre = container_of(qm, struct hpre, qm);
	struct hpre_debug *dbg = &hpre->debug;
	struct dentry *file_dir;

	if (dir)
		file_dir = dir;
	else
		file_dir = qm->debug.debug_root;

	if (type >= HPRE_DEBUG_FILE_NUM)
		return -EINVAL;

	spin_lock_init(&dbg->files[indx].lock);
	dbg->files[indx].debug = dbg;
	dbg->files[indx].type = type;
	dbg->files[indx].index = indx;
	debugfs_create_file(hpre_debug_file_name[type], 0600, file_dir,
			    dbg->files + indx, &hpre_ctrl_debug_fops);

	return 0;
}

static int hpre_pf_comm_regs_debugfs_init(struct hisi_qm *qm)
{
	struct device *dev = &qm->pdev->dev;
	struct debugfs_regset32 *regset;

	regset = devm_kzalloc(dev, sizeof(*regset), GFP_KERNEL);
	if (!regset)
		return -ENOMEM;

	regset->regs = hpre_com_dfx_regs;
	regset->nregs = ARRAY_SIZE(hpre_com_dfx_regs);
	regset->base = qm->io_base;

	debugfs_create_regset32("regs", 0444,  qm->debug.debug_root, regset);
	return 0;
}

static int hpre_cluster_debugfs_init(struct hisi_qm *qm)
{
	u8 clusters_num = HPRE_CLUSTERS_NUM(qm);
	struct device *dev = &qm->pdev->dev;
	char buf[HPRE_DBGFS_VAL_MAX_LEN];
	struct debugfs_regset32 *regset;
	struct dentry *tmp_d;
	int i, ret;

	for (i = 0; i < clusters_num; i++) {
		ret = snprintf(buf, HPRE_DBGFS_VAL_MAX_LEN, "cluster%d", i);
		if (ret < 0)
			return -EINVAL;
		tmp_d = debugfs_create_dir(buf, qm->debug.debug_root);

		regset = devm_kzalloc(dev, sizeof(*regset), GFP_KERNEL);
		if (!regset)
			return -ENOMEM;

		regset->regs = hpre_cluster_dfx_regs;
		regset->nregs = ARRAY_SIZE(hpre_cluster_dfx_regs);
		regset->base = qm->io_base + hpre_cluster_offsets[i];

		debugfs_create_regset32("regs", 0444, tmp_d, regset);
		ret = hpre_create_debugfs_file(qm, tmp_d, HPRE_CLUSTER_CTRL,
					       i + HPRE_CLUSTER_CTRL);
		if (ret)
			return ret;
	}

	return 0;
}

static int hpre_ctrl_debug_init(struct hisi_qm *qm)
{
	int ret;

	ret = hpre_create_debugfs_file(qm, NULL, HPRE_CURRENT_QM,
				       HPRE_CURRENT_QM);
	if (ret)
		return ret;

	ret = hpre_create_debugfs_file(qm, NULL, HPRE_CLEAR_ENABLE,
				       HPRE_CLEAR_ENABLE);
	if (ret)
		return ret;

	ret = hpre_pf_comm_regs_debugfs_init(qm);
	if (ret)
		return ret;

	return hpre_cluster_debugfs_init(qm);
}

static void hpre_dfx_debug_init(struct hisi_qm *qm)
{
	struct hpre *hpre = container_of(qm, struct hpre, qm);
	struct hpre_dfx *dfx = hpre->debug.dfx;
	struct dentry *parent;
	int i;

	parent = debugfs_create_dir("hpre_dfx", qm->debug.debug_root);
	for (i = 0; i < HPRE_DFX_FILE_NUM; i++) {
		dfx[i].type = i;
		debugfs_create_file(hpre_dfx_files[i], 0644, parent, &dfx[i],
				    &hpre_atomic64_ops);
	}
}

static int hpre_debugfs_init(struct hisi_qm *qm)
{
	struct device *dev = &qm->pdev->dev;
	int ret;

	qm->debug.debug_root = debugfs_create_dir(dev_name(dev),
						  hpre_debugfs_root);

	qm->debug.sqe_mask_offset = HPRE_SQE_MASK_OFFSET;
	qm->debug.sqe_mask_len = HPRE_SQE_MASK_LEN;
	hisi_qm_debug_init(qm);

	if (qm->pdev->device == HPRE_PCI_DEVICE_ID) {
		ret = hpre_ctrl_debug_init(qm);
		if (ret)
			goto failed_to_create;
	}

	hpre_dfx_debug_init(qm);

	return 0;

failed_to_create:
	debugfs_remove_recursive(qm->debug.debug_root);
	return ret;
}

static void hpre_debugfs_exit(struct hisi_qm *qm)
{
	debugfs_remove_recursive(qm->debug.debug_root);
}

static int hpre_qm_init(struct hisi_qm *qm, struct pci_dev *pdev)
{
	if (pdev->revision == QM_HW_V1) {
		pci_warn(pdev, "HPRE version 1 is not supported!\n");
		return -EINVAL;
	}

	if (pdev->revision >= QM_HW_V3)
		qm->algs = "rsa\ndh\necdh\nx25519\nx448\necdsa\nsm2\n";
	else
		qm->algs = "rsa\ndh\n";
	qm->mode = uacce_mode;
	qm->pdev = pdev;
	qm->ver = pdev->revision;
	qm->sqe_size = HPRE_SQE_SIZE;
	qm->dev_name = hpre_name;

	qm->fun_type = (pdev->device == HPRE_PCI_DEVICE_ID) ?
			QM_HW_PF : QM_HW_VF;
	if (qm->fun_type == QM_HW_PF) {
		qm->qp_base = HPRE_PF_DEF_Q_BASE;
		qm->qp_num = pf_q_num;
		qm->debug.curr_qm_qp_num = pf_q_num;
		qm->qm_list = &hpre_devices;
	}

	return hisi_qm_init(qm);
}

static void hpre_log_hw_error(struct hisi_qm *qm, u32 err_sts)
{
	const struct hpre_hw_error *err = hpre_hw_errors;
	struct device *dev = &qm->pdev->dev;

	while (err->msg) {
		if (err->int_msk & err_sts)
			dev_warn(dev, "%s [error status=0x%x] found\n",
				 err->msg, err->int_msk);
		err++;
	}
}

static u32 hpre_get_hw_err_status(struct hisi_qm *qm)
{
	return readl(qm->io_base + HPRE_HAC_INT_STATUS);
}

static void hpre_clear_hw_err_status(struct hisi_qm *qm, u32 err_sts)
{
	writel(err_sts, qm->io_base + HPRE_HAC_SOURCE_INT);
}

static void hpre_open_axi_master_ooo(struct hisi_qm *qm)
{
	u32 value;

	value = readl(qm->io_base + HPRE_AM_OOO_SHUTDOWN_ENB);
	writel(value & ~HPRE_AM_OOO_SHUTDOWN_ENABLE,
	       HPRE_ADDR(qm, HPRE_AM_OOO_SHUTDOWN_ENB));
	writel(value | HPRE_AM_OOO_SHUTDOWN_ENABLE,
	       HPRE_ADDR(qm, HPRE_AM_OOO_SHUTDOWN_ENB));
}

static const struct hisi_qm_err_ini hpre_err_ini = {
	.hw_init		= hpre_set_user_domain_and_cache,
	.hw_err_enable		= hpre_hw_error_enable,
	.hw_err_disable		= hpre_hw_error_disable,
	.get_dev_hw_err_status	= hpre_get_hw_err_status,
	.clear_dev_hw_err_status = hpre_clear_hw_err_status,
	.log_dev_hw_err		= hpre_log_hw_error,
	.open_axi_master_ooo	= hpre_open_axi_master_ooo,
	.err_info		= {
		.ce			= QM_BASE_CE,
		.nfe			= QM_BASE_NFE | QM_ACC_DO_TASK_TIMEOUT,
		.fe			= 0,
		.ecc_2bits_mask		= HPRE_CORE_ECC_2BIT_ERR |
					  HPRE_OOO_ECC_2BIT_ERR,
		.msi_wr_port		= HPRE_WR_MSI_PORT,
		.acpi_rst		= "HRST",
	}
};

static int hpre_pf_probe_init(struct hpre *hpre)
{
	struct hisi_qm *qm = &hpre->qm;
	int ret;

	qm->ctrl_qp_num = HPRE_QUEUE_NUM_V2;

	ret = hpre_set_user_domain_and_cache(qm);
	if (ret)
		return ret;

	qm->err_ini = &hpre_err_ini;
	hisi_qm_dev_err_init(qm);

	return 0;
}

static int hpre_probe_init(struct hpre *hpre)
{
	struct hisi_qm *qm = &hpre->qm;
	int ret;

	if (qm->fun_type == QM_HW_PF) {
		ret = hpre_pf_probe_init(hpre);
		if (ret)
			return ret;
	}

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

	qm = &hpre->qm;
	ret = hpre_qm_init(qm, pdev);
	if (ret) {
		pci_err(pdev, "Failed to init HPRE QM (%d)!\n", ret);
		return ret;
	}

	ret = hpre_probe_init(hpre);
	if (ret) {
		pci_err(pdev, "Failed to probe (%d)!\n", ret);
		goto err_with_qm_init;
	}

	ret = hisi_qm_start(qm);
	if (ret)
		goto err_with_err_init;

	ret = hpre_debugfs_init(qm);
	if (ret)
		dev_warn(&pdev->dev, "init debugfs fail!\n");

	ret = hisi_qm_alg_register(qm, &hpre_devices);
	if (ret < 0) {
		pci_err(pdev, "fail to register algs to crypto!\n");
		goto err_with_qm_start;
	}

	if (qm->uacce) {
		ret = uacce_register(qm->uacce);
		if (ret) {
			pci_err(pdev, "failed to register uacce (%d)!\n", ret);
			goto err_with_alg_register;
		}
	}

	if (qm->fun_type == QM_HW_PF && vfs_num) {
		ret = hisi_qm_sriov_enable(pdev, vfs_num);
		if (ret < 0)
			goto err_with_alg_register;
	}

	return 0;

err_with_alg_register:
	hisi_qm_alg_unregister(qm, &hpre_devices);

err_with_qm_start:
	hpre_debugfs_exit(qm);
	hisi_qm_stop(qm, QM_NORMAL);

err_with_err_init:
	hisi_qm_dev_err_uninit(qm);

err_with_qm_init:
	hisi_qm_uninit(qm);

	return ret;
}

static void hpre_remove(struct pci_dev *pdev)
{
	struct hisi_qm *qm = pci_get_drvdata(pdev);
	int ret;

	hisi_qm_wait_task_finish(qm, &hpre_devices);
	hisi_qm_alg_unregister(qm, &hpre_devices);
	if (qm->fun_type == QM_HW_PF && qm->vfs_num) {
		ret = hisi_qm_sriov_disable(pdev, qm->is_frozen);
		if (ret) {
			pci_err(pdev, "Disable SRIOV fail!\n");
			return;
		}
	}

	hpre_debugfs_exit(qm);
	hisi_qm_stop(qm, QM_NORMAL);

	if (qm->fun_type == QM_HW_PF) {
		if (qm->use_sva && qm->ver == QM_HW_V2)
			hpre_pasid_disable(qm);
		hpre_cnt_regs_clear(qm);
		qm->debug.curr_qm_qp_num = 0;
		hisi_qm_dev_err_uninit(qm);
	}

	hisi_qm_uninit(qm);
}


static const struct pci_error_handlers hpre_err_handler = {
	.error_detected		= hisi_qm_dev_err_detected,
	.slot_reset		= hisi_qm_dev_slot_reset,
	.reset_prepare		= hisi_qm_reset_prepare,
	.reset_done		= hisi_qm_reset_done,
};

static struct pci_driver hpre_pci_driver = {
	.name			= hpre_name,
	.id_table		= hpre_dev_ids,
	.probe			= hpre_probe,
	.remove			= hpre_remove,
	.sriov_configure	= IS_ENABLED(CONFIG_PCI_IOV) ?
				  hisi_qm_sriov_configure : NULL,
	.err_handler		= &hpre_err_handler,
	.shutdown		= hisi_qm_dev_shutdown,
};

static void hpre_register_debugfs(void)
{
	if (!debugfs_initialized())
		return;

	hpre_debugfs_root = debugfs_create_dir(hpre_name, NULL);
}

static void hpre_unregister_debugfs(void)
{
	debugfs_remove_recursive(hpre_debugfs_root);
}

static int __init hpre_init(void)
{
	int ret;

	hisi_qm_init_list(&hpre_devices);
	hpre_register_debugfs();

	ret = pci_register_driver(&hpre_pci_driver);
	if (ret) {
		hpre_unregister_debugfs();
		pr_err("hpre: can't register hisi hpre driver.\n");
	}

	return ret;
}

static void __exit hpre_exit(void)
{
	pci_unregister_driver(&hpre_pci_driver);
	hpre_unregister_debugfs();
}

module_init(hpre_init);
module_exit(hpre_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Zaibo Xu <xuzaibo@huawei.com>");
MODULE_DESCRIPTION("Driver for HiSilicon HPRE accelerator");
