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
#define HPRE_PCI_VF_DEVICE_ID		0xa259
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

static const char * const hpre_debug_file_name[] = {
	[HPRE_CURRENT_QM]   = "current_qm",
	[HPRE_CLEAR_ENABLE] = "rdclr_en",
	[HPRE_CLUSTER_CTRL] = "cluster_ctrl",
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
	{ .int_msk = GENMASK(15, 10), .msg = "hpre_ooo_rdrsp_err" },
	{ .int_msk = GENMASK(21, 16), .msg = "hpre_ooo_wrrsp_err" },
	{ /* sentinel */ }
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

static struct debugfs_reg32 hpre_cluster_dfx_regs[] = {
	{"CORES_EN_STATUS          ",  HPRE_CORE_EN_OFFSET},
	{"CORES_INI_CFG              ",  HPRE_CORE_INI_CFG_OFFSET},
	{"CORES_INI_STATUS         ",  HPRE_CORE_INI_STATUS_OFFSET},
	{"CORES_HTBT_WARN         ",  HPRE_CORE_HTBT_WARN_OFFSET},
	{"CORES_IS_SCHD               ",  HPRE_CORE_IS_SCHD_OFFSET},
};

static struct debugfs_reg32 hpre_com_dfx_regs[] = {
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

static void hpre_cnt_regs_clear(struct hisi_qm *qm)
{
	unsigned long offset;
	int i;

	/* clear current_qm */
	writel(0x0, qm->io_base + QM_DFX_MB_CNT_VF);
	writel(0x0, qm->io_base + QM_DFX_DB_CNT_VF);

	/* clear clusterX/cluster_ctrl */
	for (i = 0; i < HPRE_CLUSTERS_NUM; i++) {
		offset = HPRE_CLSTR_BASE + i * HPRE_CLSTR_ADDR_INTRVL;
		writel(0x0, qm->io_base + offset + HPRE_CLUSTER_INQURY);
	}

	/* clear rdclr_en */
	writel(0x0, qm->io_base + HPRE_CTRL_CNT_CLR_CE);

	hisi_qm_debug_regs_clear(qm);
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
	struct hpre_debug *debug = file->debug;
	struct hpre *hpre = container_of(debug, struct hpre, debug);
	u32 num_vfs = hpre->num_vfs;
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
	ret = sprintf(tbuf, "%u\n", val);
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

static int hpre_create_debugfs_file(struct hpre_debug *dbg, struct dentry *dir,
				    enum hpre_ctrl_dbgfs_file type, int indx)
{
	struct dentry *tmp, *file_dir;

	if (dir)
		file_dir = dir;
	else
		file_dir = dbg->debug_root;

	if (type >= HPRE_DEBUG_FILE_NUM)
		return -EINVAL;

	spin_lock_init(&dbg->files[indx].lock);
	dbg->files[indx].debug = dbg;
	dbg->files[indx].type = type;
	dbg->files[indx].index = indx;
	tmp = debugfs_create_file(hpre_debug_file_name[type], 0600, file_dir,
				  dbg->files + indx, &hpre_ctrl_debug_fops);
	if (!tmp)
		return -ENOENT;

	return 0;
}

static int hpre_pf_comm_regs_debugfs_init(struct hpre_debug *debug)
{
	struct hpre *hpre = container_of(debug, struct hpre, debug);
	struct hisi_qm *qm = &hpre->qm;
	struct device *dev = &qm->pdev->dev;
	struct debugfs_regset32 *regset;
	struct dentry *tmp;

	regset = devm_kzalloc(dev, sizeof(*regset), GFP_KERNEL);
	if (!regset)
		return -ENOMEM;

	regset->regs = hpre_com_dfx_regs;
	regset->nregs = ARRAY_SIZE(hpre_com_dfx_regs);
	regset->base = qm->io_base;

	tmp = debugfs_create_regset32("regs", 0444,  debug->debug_root, regset);
	if (!tmp)
		return -ENOENT;

	return 0;
}

static int hpre_cluster_debugfs_init(struct hpre_debug *debug)
{
	struct hpre *hpre = container_of(debug, struct hpre, debug);
	struct hisi_qm *qm = &hpre->qm;
	struct device *dev = &qm->pdev->dev;
	char buf[HPRE_DBGFS_VAL_MAX_LEN];
	struct debugfs_regset32 *regset;
	struct dentry *tmp_d, *tmp;
	int i, ret;

	for (i = 0; i < HPRE_CLUSTERS_NUM; i++) {
		sprintf(buf, "cluster%d", i);

		tmp_d = debugfs_create_dir(buf, debug->debug_root);
		if (!tmp_d)
			return -ENOENT;

		regset = devm_kzalloc(dev, sizeof(*regset), GFP_KERNEL);
		if (!regset)
			return -ENOMEM;

		regset->regs = hpre_cluster_dfx_regs;
		regset->nregs = ARRAY_SIZE(hpre_cluster_dfx_regs);
		regset->base = qm->io_base + hpre_cluster_offsets[i];

		tmp = debugfs_create_regset32("regs", 0444, tmp_d, regset);
		if (!tmp)
			return -ENOENT;
		ret = hpre_create_debugfs_file(debug, tmp_d, HPRE_CLUSTER_CTRL,
					       i + HPRE_CLUSTER_CTRL);
		if (ret)
			return ret;
	}

	return 0;
}

static int hpre_ctrl_debug_init(struct hpre_debug *debug)
{
	int ret;

	ret = hpre_create_debugfs_file(debug, NULL, HPRE_CURRENT_QM,
				       HPRE_CURRENT_QM);
	if (ret)
		return ret;

	ret = hpre_create_debugfs_file(debug, NULL, HPRE_CLEAR_ENABLE,
				       HPRE_CLEAR_ENABLE);
	if (ret)
		return ret;

	ret = hpre_pf_comm_regs_debugfs_init(debug);
	if (ret)
		return ret;

	return hpre_cluster_debugfs_init(debug);
}

static int hpre_debugfs_init(struct hpre *hpre)
{
	struct hisi_qm *qm = &hpre->qm;
	struct device *dev = &qm->pdev->dev;
	struct dentry *dir;
	int ret;

	dir = debugfs_create_dir(dev_name(dev), hpre_debugfs_root);
	if (!dir)
		return -ENOENT;

	qm->debug.debug_root = dir;

	ret = hisi_qm_debug_init(qm);
	if (ret)
		goto failed_to_create;

	if (qm->pdev->device == HPRE_PCI_DEVICE_ID) {
		hpre->debug.debug_root = dir;
		ret = hpre_ctrl_debug_init(&hpre->debug);
		if (ret)
			goto failed_to_create;
	}
	return 0;

failed_to_create:
	debugfs_remove_recursive(qm->debug.debug_root);
	return ret;
}

static void hpre_debugfs_exit(struct hpre *hpre)
{
	struct hisi_qm *qm = &hpre->qm;

	debugfs_remove_recursive(qm->debug.debug_root);
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
	qm->fun_type = (pdev->device == HPRE_PCI_DEVICE_ID) ?
		       QM_HW_PF : QM_HW_VF;
	if (pdev->is_physfn) {
		qm->qp_base = HPRE_PF_DEF_Q_BASE;
		qm->qp_num = hpre_pf_q_num;
	}
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

	if (pdev->is_physfn) {
		ret = hpre_pf_probe_init(hpre);
		if (ret)
			goto err_with_qm_init;
	} else if (qm->fun_type == QM_HW_VF && qm->ver == QM_HW_V2) {
		/* v2 starts to support get vft by mailbox */
		ret = hisi_qm_get_vft(qm, &qm->qp_base, &qm->qp_num);
		if (ret)
			goto err_with_qm_init;
	}

	ret = hisi_qm_start(qm);
	if (ret)
		goto err_with_err_init;

	ret = hpre_debugfs_init(hpre);
	if (ret)
		dev_warn(&pdev->dev, "init debugfs fail!\n");

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
	if (pdev->is_physfn)
		hpre_hw_error_disable(hpre);

err_with_qm_init:
	hisi_qm_uninit(qm);

	return ret;
}

static int hpre_vf_q_assign(struct hpre *hpre, int num_vfs)
{
	struct hisi_qm *qm = &hpre->qm;
	u32 qp_num = qm->qp_num;
	int q_num, remain_q_num, i;
	u32 q_base = qp_num;
	int ret;

	if (!num_vfs)
		return -EINVAL;

	remain_q_num = qm->ctrl_qp_num - qp_num;

	/* If remaining queues are not enough, return error. */
	if (remain_q_num < num_vfs)
		return -EINVAL;

	q_num = remain_q_num / num_vfs;
	for (i = 1; i <= num_vfs; i++) {
		if (i == num_vfs)
			q_num += remain_q_num % num_vfs;
		ret = hisi_qm_set_vft(qm, i, q_base, (u32)q_num);
		if (ret)
			return ret;
		q_base += q_num;
	}

	return 0;
}

static int hpre_clear_vft_config(struct hpre *hpre)
{
	struct hisi_qm *qm = &hpre->qm;
	u32 num_vfs = hpre->num_vfs;
	int ret;
	u32 i;

	for (i = 1; i <= num_vfs; i++) {
		ret = hisi_qm_set_vft(qm, i, 0, 0);
		if (ret)
			return ret;
	}
	hpre->num_vfs = 0;

	return 0;
}

static int hpre_sriov_enable(struct pci_dev *pdev, int max_vfs)
{
	struct hpre *hpre = pci_get_drvdata(pdev);
	int pre_existing_vfs, num_vfs, ret;

	pre_existing_vfs = pci_num_vf(pdev);
	if (pre_existing_vfs) {
		pci_err(pdev,
			"Can't enable VF. Please disable pre-enabled VFs!\n");
		return 0;
	}

	num_vfs = min_t(int, max_vfs, HPRE_VF_NUM);
	ret = hpre_vf_q_assign(hpre, num_vfs);
	if (ret) {
		pci_err(pdev, "Can't assign queues for VF!\n");
		return ret;
	}

	hpre->num_vfs = num_vfs;

	ret = pci_enable_sriov(pdev, num_vfs);
	if (ret) {
		pci_err(pdev, "Can't enable VF!\n");
		hpre_clear_vft_config(hpre);
		return ret;
	}

	return num_vfs;
}

static int hpre_sriov_disable(struct pci_dev *pdev)
{
	struct hpre *hpre = pci_get_drvdata(pdev);

	if (pci_vfs_assigned(pdev)) {
		pci_err(pdev, "Failed to disable VFs while VFs are assigned!\n");
		return -EPERM;
	}

	/* remove in hpre_pci_driver will be called to free VF resources */
	pci_disable_sriov(pdev);

	return hpre_clear_vft_config(hpre);
}

static int hpre_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	if (num_vfs)
		return hpre_sriov_enable(pdev, num_vfs);
	else
		return hpre_sriov_disable(pdev);
}

static void hpre_remove(struct pci_dev *pdev)
{
	struct hpre *hpre = pci_get_drvdata(pdev);
	struct hisi_qm *qm = &hpre->qm;
	int ret;

	hpre_algs_unregister();
	hpre_remove_from_list(hpre);
	if (qm->fun_type == QM_HW_PF && hpre->num_vfs != 0) {
		ret = hpre_sriov_disable(pdev);
		if (ret) {
			pci_err(pdev, "Disable SRIOV fail!\n");
			return;
		}
	}
	if (qm->fun_type == QM_HW_PF) {
		hpre_cnt_regs_clear(qm);
		qm->debug.curr_qm_qp_num = 0;
	}

	hpre_debugfs_exit(hpre);
	hisi_qm_stop(qm);
	if (qm->fun_type == QM_HW_PF)
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
	.sriov_configure	= hpre_sriov_configure,
	.err_handler		= &hpre_err_handler,
};

static void hpre_register_debugfs(void)
{
	if (!debugfs_initialized())
		return;

	hpre_debugfs_root = debugfs_create_dir(hpre_name, NULL);
	if (IS_ERR_OR_NULL(hpre_debugfs_root))
		hpre_debugfs_root = NULL;
}

static void hpre_unregister_debugfs(void)
{
	debugfs_remove_recursive(hpre_debugfs_root);
}

static int __init hpre_init(void)
{
	int ret;

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
