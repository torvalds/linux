// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 HiSilicon Limited. */

#include <linux/acpi.h>
#include <linux/aer.h>
#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/seq_file.h>
#include <linux/topology.h>

#include "sec.h"

#define SEC_VF_NUM			63
#define SEC_QUEUE_NUM_V1		4096
#define SEC_QUEUE_NUM_V2		1024
#define SEC_PF_PCI_DEVICE_ID		0xa255
#define SEC_VF_PCI_DEVICE_ID		0xa256

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
#define SEC_CTX_Q_NUM_MAX		32

#define SEC_CTRL_CNT_CLR_CE		0x301120
#define SEC_CTRL_CNT_CLR_CE_BIT		BIT(0)
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
#define SEC_VF_CNT_MASK			0xffffffc0
#define SEC_DBGFS_VAL_MAX_LEN		20

#define SEC_ADDR(qm, offset) ((qm)->io_base + (offset) + \
			     SEC_ENGINE_PF_CFG_OFF + SEC_ACC_COMMON_REG_OFF)

struct sec_hw_error {
	u32 int_msk;
	const char *msg;
};

static const char sec_name[] = "hisi_sec2";
static struct dentry *sec_debugfs_root;
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

static const char * const sec_dbg_file_name[] = {
	[SEC_CURRENT_QM] = "current_qm",
	[SEC_CLEAR_ENABLE] = "clear_enable",
};

static struct debugfs_reg32 sec_dfx_regs[] = {
	{"SEC_PF_ABNORMAL_INT_SOURCE    ",  0x301010},
	{"SEC_SAA_EN                    ",  0x301270},
	{"SEC_BD_LATENCY_MIN            ",  0x301600},
	{"SEC_BD_LATENCY_MAX            ",  0x301608},
	{"SEC_BD_LATENCY_AVG            ",  0x30160C},
	{"SEC_BD_NUM_IN_SAA0            ",  0x301670},
	{"SEC_BD_NUM_IN_SAA1            ",  0x301674},
	{"SEC_BD_NUM_IN_SEC             ",  0x301680},
	{"SEC_ECC_1BIT_CNT              ",  0x301C00},
	{"SEC_ECC_1BIT_INFO             ",  0x301C04},
	{"SEC_ECC_2BIT_CNT              ",  0x301C10},
	{"SEC_ECC_2BIT_INFO             ",  0x301C14},
	{"SEC_BD_SAA0                   ",  0x301C20},
	{"SEC_BD_SAA1                   ",  0x301C24},
	{"SEC_BD_SAA2                   ",  0x301C28},
	{"SEC_BD_SAA3                   ",  0x301C2C},
	{"SEC_BD_SAA4                   ",  0x301C30},
	{"SEC_BD_SAA5                   ",  0x301C34},
	{"SEC_BD_SAA6                   ",  0x301C38},
	{"SEC_BD_SAA7                   ",  0x301C3C},
	{"SEC_BD_SAA8                   ",  0x301C40},
};

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

	if (!ctx_q_num || ctx_q_num > SEC_CTX_Q_NUM_MAX || ctx_q_num & 0x1) {
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
MODULE_PARM_DESC(ctx_q_num, "Queue num in ctx (24 default, 2, 4, ..., 32)");

static const struct pci_device_id sec_dev_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_HUAWEI, SEC_PF_PCI_DEVICE_ID) },
	{ PCI_DEVICE(PCI_VENDOR_ID_HUAWEI, SEC_VF_PCI_DEVICE_ID) },
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

	/*
	 * As for VF, it is a wrong way to get endian setting by
	 * reading a register of the engine
	 */
	if (qm->pdev->is_virtfn) {
		dev_err_ratelimited(&qm->pdev->dev,
				    "cannot access a register in VF!\n");
		return SEC_LE;
	}
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

/* sec_debug_regs_clear() - clear the sec debug regs */
static void sec_debug_regs_clear(struct hisi_qm *qm)
{
	/* clear current_qm */
	writel(0x0, qm->io_base + QM_DFX_MB_CNT_VF);
	writel(0x0, qm->io_base + QM_DFX_DB_CNT_VF);

	/* clear rdclr_en */
	writel(0x0, qm->io_base + SEC_CTRL_CNT_CLR_CE);

	hisi_qm_debug_regs_clear(qm);
}

static void sec_hw_error_enable(struct hisi_qm *qm)
{
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

static void sec_hw_error_disable(struct hisi_qm *qm)
{
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

static u32 sec_current_qm_read(struct sec_debug_file *file)
{
	struct hisi_qm *qm = file->qm;

	return readl(qm->io_base + QM_DFX_MB_CNT_VF);
}

static int sec_current_qm_write(struct sec_debug_file *file, u32 val)
{
	struct hisi_qm *qm = file->qm;
	struct sec_dev *sec = container_of(qm, struct sec_dev, qm);
	u32 vfq_num;
	u32 tmp;

	if (val > sec->num_vfs)
		return -EINVAL;

	/* According PF or VF Dev ID to calculation curr_qm_qp_num and store */
	if (!val) {
		qm->debug.curr_qm_qp_num = qm->qp_num;
	} else {
		vfq_num = (qm->ctrl_qp_num - qm->qp_num) / sec->num_vfs;

		if (val == sec->num_vfs)
			qm->debug.curr_qm_qp_num =
				qm->ctrl_qp_num - qm->qp_num -
				(sec->num_vfs - 1) * vfq_num;
		else
			qm->debug.curr_qm_qp_num = vfq_num;
	}

	writel(val, qm->io_base + QM_DFX_MB_CNT_VF);
	writel(val, qm->io_base + QM_DFX_DB_CNT_VF);

	tmp = val |
	      (readl(qm->io_base + QM_DFX_SQE_CNT_VF_SQN) & CURRENT_Q_MASK);
	writel(tmp, qm->io_base + QM_DFX_SQE_CNT_VF_SQN);

	tmp = val |
	      (readl(qm->io_base + QM_DFX_CQE_CNT_VF_CQN) & CURRENT_Q_MASK);
	writel(tmp, qm->io_base + QM_DFX_CQE_CNT_VF_CQN);

	return 0;
}

static u32 sec_clear_enable_read(struct sec_debug_file *file)
{
	struct hisi_qm *qm = file->qm;

	return readl(qm->io_base + SEC_CTRL_CNT_CLR_CE) &
			SEC_CTRL_CNT_CLR_CE_BIT;
}

static int sec_clear_enable_write(struct sec_debug_file *file, u32 val)
{
	struct hisi_qm *qm = file->qm;
	u32 tmp;

	if (val != 1 && val)
		return -EINVAL;

	tmp = (readl(qm->io_base + SEC_CTRL_CNT_CLR_CE) &
	       ~SEC_CTRL_CNT_CLR_CE_BIT) | val;
	writel(tmp, qm->io_base + SEC_CTRL_CNT_CLR_CE);

	return 0;
}

static ssize_t sec_debug_read(struct file *filp, char __user *buf,
			       size_t count, loff_t *pos)
{
	struct sec_debug_file *file = filp->private_data;
	char tbuf[SEC_DBGFS_VAL_MAX_LEN];
	u32 val;
	int ret;

	spin_lock_irq(&file->lock);

	switch (file->index) {
	case SEC_CURRENT_QM:
		val = sec_current_qm_read(file);
		break;
	case SEC_CLEAR_ENABLE:
		val = sec_clear_enable_read(file);
		break;
	default:
		spin_unlock_irq(&file->lock);
		return -EINVAL;
	}

	spin_unlock_irq(&file->lock);
	ret = snprintf(tbuf, SEC_DBGFS_VAL_MAX_LEN, "%u\n", val);

	return simple_read_from_buffer(buf, count, pos, tbuf, ret);
}

static ssize_t sec_debug_write(struct file *filp, const char __user *buf,
			       size_t count, loff_t *pos)
{
	struct sec_debug_file *file = filp->private_data;
	char tbuf[SEC_DBGFS_VAL_MAX_LEN];
	unsigned long val;
	int len, ret;

	if (*pos != 0)
		return 0;

	if (count >= SEC_DBGFS_VAL_MAX_LEN)
		return -ENOSPC;

	len = simple_write_to_buffer(tbuf, SEC_DBGFS_VAL_MAX_LEN - 1,
				     pos, buf, count);
	if (len < 0)
		return len;

	tbuf[len] = '\0';
	if (kstrtoul(tbuf, 0, &val))
		return -EFAULT;

	spin_lock_irq(&file->lock);

	switch (file->index) {
	case SEC_CURRENT_QM:
		ret = sec_current_qm_write(file, val);
		if (ret)
			goto err_input;
		break;
	case SEC_CLEAR_ENABLE:
		ret = sec_clear_enable_write(file, val);
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

static const struct file_operations sec_dbg_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = sec_debug_read,
	.write = sec_debug_write,
};

static int sec_debugfs_atomic64_get(void *data, u64 *val)
{
	*val = atomic64_read((atomic64_t *)data);
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(sec_atomic64_ops, sec_debugfs_atomic64_get,
			 NULL, "%lld\n");

static int sec_core_debug_init(struct sec_dev *sec)
{
	struct hisi_qm *qm = &sec->qm;
	struct device *dev = &qm->pdev->dev;
	struct sec_dfx *dfx = &sec->debug.dfx;
	struct debugfs_regset32 *regset;
	struct dentry *tmp_d;

	tmp_d = debugfs_create_dir("sec_dfx", sec->qm.debug.debug_root);

	regset = devm_kzalloc(dev, sizeof(*regset), GFP_KERNEL);
	if (!regset)
		return -ENOENT;

	regset->regs = sec_dfx_regs;
	regset->nregs = ARRAY_SIZE(sec_dfx_regs);
	regset->base = qm->io_base;

	debugfs_create_regset32("regs", 0444, tmp_d, regset);

	debugfs_create_file("send_cnt", 0444, tmp_d,
			    &dfx->send_cnt, &sec_atomic64_ops);

	debugfs_create_file("recv_cnt", 0444, tmp_d,
			    &dfx->recv_cnt, &sec_atomic64_ops);

	return 0;
}

static int sec_debug_init(struct sec_dev *sec)
{
	int i;

	for (i = SEC_CURRENT_QM; i < SEC_DEBUG_FILE_NUM; i++) {
		spin_lock_init(&sec->debug.files[i].lock);
		sec->debug.files[i].index = i;
		sec->debug.files[i].qm = &sec->qm;

		debugfs_create_file(sec_dbg_file_name[i], 0600,
				    sec->qm.debug.debug_root,
				    sec->debug.files + i,
				    &sec_dbg_fops);
	}

	return sec_core_debug_init(sec);
}

static int sec_debugfs_init(struct sec_dev *sec)
{
	struct hisi_qm *qm = &sec->qm;
	struct device *dev = &qm->pdev->dev;
	int ret;

	qm->debug.debug_root = debugfs_create_dir(dev_name(dev),
						  sec_debugfs_root);
	ret = hisi_qm_debug_init(qm);
	if (ret)
		goto failed_to_create;

	if (qm->pdev->device == SEC_PF_PCI_DEVICE_ID) {
		ret = sec_debug_init(sec);
		if (ret)
			goto failed_to_create;
	}

	return 0;

failed_to_create:
	debugfs_remove_recursive(sec_debugfs_root);

	return ret;
}

static void sec_debugfs_exit(struct sec_dev *sec)
{
	debugfs_remove_recursive(sec->qm.debug.debug_root);
}

static void sec_log_hw_error(struct hisi_qm *qm, u32 err_sts)
{
	const struct sec_hw_error *errs = sec_hw_errors;
	struct device *dev = &qm->pdev->dev;
	u32 err_val;

	while (errs->msg) {
		if (errs->int_msk & err_sts) {
			dev_err(dev, "%s [error status=0x%x] found\n",
				errs->msg, errs->int_msk);

			if (SEC_CORE_INT_STATUS_M_ECC & errs->int_msk) {
				err_val = readl(qm->io_base +
						SEC_CORE_SRAM_ECC_ERR_INFO);
				dev_err(dev, "multi ecc sram num=0x%x\n",
					SEC_ECC_NUM(err_val));
				dev_err(dev, "multi ecc sram addr=0x%x\n",
					SEC_ECC_ADDR(err_val));
			}
		}
		errs++;
	}

	writel(err_sts, qm->io_base + SEC_CORE_INT_SOURCE);
}

static u32 sec_get_hw_err_status(struct hisi_qm *qm)
{
	return readl(qm->io_base + SEC_CORE_INT_STATUS);
}

static const struct hisi_qm_err_ini sec_err_ini = {
	.hw_err_enable		= sec_hw_error_enable,
	.hw_err_disable		= sec_hw_error_disable,
	.get_dev_hw_err_status	= sec_get_hw_err_status,
	.log_dev_hw_err		= sec_log_hw_error,
	.err_info		= {
		.ce			= QM_BASE_CE,
		.nfe			= QM_BASE_NFE | QM_ACC_DO_TASK_TIMEOUT |
					  QM_ACC_WB_NOT_READY_TIMEOUT,
		.fe			= 0,
		.msi			= QM_DB_RANDOM_INVALID,
	}
};

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

	qm->err_ini = &sec_err_ini;

	ret = sec_set_user_domain_and_cache(sec);
	if (ret)
		return ret;

	hisi_qm_dev_err_init(qm);
	sec_debug_regs_clear(qm);

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
	if (qm->fun_type == QM_HW_PF) {
		qm->qp_base = SEC_PF_DEF_Q_BASE;
		qm->qp_num = pf_q_num;
		qm->debug.curr_qm_qp_num = pf_q_num;

		return sec_pf_probe_init(sec);
	} else if (qm->fun_type == QM_HW_VF) {
		/*
		 * have no way to get qm configure in VM in v1 hardware,
		 * so currently force PF to uses SEC_PF_DEF_Q_NUM, and force
		 * to trigger only one VF in v1 hardware.
		 * v2 hardware has no such problem.
		 */
		if (qm->ver == QM_HW_V1) {
			qm->qp_base = SEC_PF_DEF_Q_NUM;
			qm->qp_num = SEC_QUEUE_NUM_V1 - SEC_PF_DEF_Q_NUM;
		} else if (qm->ver == QM_HW_V2) {
			/* v2 starts to support get vft by mailbox */
			return hisi_qm_get_vft(qm, &qm->qp_base, &qm->qp_num);
		}
	} else {
		return -ENODEV;
	}

	return 0;
}

static void sec_probe_uninit(struct hisi_qm *qm)
{
	hisi_qm_dev_err_uninit(qm);
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

	ret = sec_debugfs_init(sec);
	if (ret)
		pci_warn(pdev, "Failed to init debugfs!\n");

	sec_add_to_list(sec);

	ret = sec_register_to_crypto();
	if (ret < 0) {
		pr_err("Failed to register driver to crypto.\n");
		goto err_remove_from_list;
	}

	return 0;

err_remove_from_list:
	sec_remove_from_list(sec);
	sec_debugfs_exit(sec);
	hisi_qm_stop(qm);

err_probe_uninit:
	sec_probe_uninit(qm);

err_qm_uninit:
	sec_qm_uninit(qm);

	return ret;
}

/* now we only support equal assignment */
static int sec_vf_q_assign(struct sec_dev *sec, u32 num_vfs)
{
	struct hisi_qm *qm = &sec->qm;
	u32 qp_num = qm->qp_num;
	u32 q_base = qp_num;
	u32 q_num, remain_q_num;
	int i, j, ret;

	if (!num_vfs)
		return -EINVAL;

	remain_q_num = qm->ctrl_qp_num - qp_num;
	q_num = remain_q_num / num_vfs;

	for (i = 1; i <= num_vfs; i++) {
		if (i == num_vfs)
			q_num += remain_q_num % num_vfs;
		ret = hisi_qm_set_vft(qm, i, q_base, q_num);
		if (ret) {
			for (j = i; j > 0; j--)
				hisi_qm_set_vft(qm, j, 0, 0);
			return ret;
		}
		q_base += q_num;
	}

	return 0;
}

static int sec_clear_vft_config(struct sec_dev *sec)
{
	struct hisi_qm *qm = &sec->qm;
	u32 num_vfs = sec->num_vfs;
	int ret;
	u32 i;

	for (i = 1; i <= num_vfs; i++) {
		ret = hisi_qm_set_vft(qm, i, 0, 0);
		if (ret)
			return ret;
	}

	sec->num_vfs = 0;

	return 0;
}

static int sec_sriov_enable(struct pci_dev *pdev, int max_vfs)
{
	struct sec_dev *sec = pci_get_drvdata(pdev);
	int pre_existing_vfs, ret;
	u32 num_vfs;

	pre_existing_vfs = pci_num_vf(pdev);

	if (pre_existing_vfs) {
		pci_err(pdev, "Can't enable VF. Please disable at first!\n");
		return 0;
	}

	num_vfs = min_t(u32, max_vfs, SEC_VF_NUM);

	ret = sec_vf_q_assign(sec, num_vfs);
	if (ret) {
		pci_err(pdev, "Can't assign queues for VF!\n");
		return ret;
	}

	sec->num_vfs = num_vfs;

	ret = pci_enable_sriov(pdev, num_vfs);
	if (ret) {
		pci_err(pdev, "Can't enable VF!\n");
		sec_clear_vft_config(sec);
		return ret;
	}

	return num_vfs;
}

static int sec_sriov_disable(struct pci_dev *pdev)
{
	struct sec_dev *sec = pci_get_drvdata(pdev);

	if (pci_vfs_assigned(pdev)) {
		pci_err(pdev, "Can't disable VFs while VFs are assigned!\n");
		return -EPERM;
	}

	/* remove in sec_pci_driver will be called to free VF resources */
	pci_disable_sriov(pdev);

	return sec_clear_vft_config(sec);
}

static int sec_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	if (num_vfs)
		return sec_sriov_enable(pdev, num_vfs);
	else
		return sec_sriov_disable(pdev);
}

static void sec_remove(struct pci_dev *pdev)
{
	struct sec_dev *sec = pci_get_drvdata(pdev);
	struct hisi_qm *qm = &sec->qm;

	sec_unregister_from_crypto();

	sec_remove_from_list(sec);

	if (qm->fun_type == QM_HW_PF && sec->num_vfs)
		(void)sec_sriov_disable(pdev);

	sec_debugfs_exit(sec);

	(void)hisi_qm_stop(qm);

	if (qm->fun_type == QM_HW_PF)
		sec_debug_regs_clear(qm);

	sec_probe_uninit(qm);

	sec_qm_uninit(qm);
}

static const struct pci_error_handlers sec_err_handler = {
	.error_detected = hisi_qm_dev_err_detected,
};

static struct pci_driver sec_pci_driver = {
	.name = "hisi_sec2",
	.id_table = sec_dev_ids,
	.probe = sec_probe,
	.remove = sec_remove,
	.err_handler = &sec_err_handler,
	.sriov_configure = sec_sriov_configure,
};

static void sec_register_debugfs(void)
{
	if (!debugfs_initialized())
		return;

	sec_debugfs_root = debugfs_create_dir("hisi_sec2", NULL);
}

static void sec_unregister_debugfs(void)
{
	debugfs_remove_recursive(sec_debugfs_root);
}

static int __init sec_init(void)
{
	int ret;

	sec_register_debugfs();

	ret = pci_register_driver(&sec_pci_driver);
	if (ret < 0) {
		sec_unregister_debugfs();
		pr_err("Failed to register pci driver.\n");
		return ret;
	}

	return 0;
}

static void __exit sec_exit(void)
{
	pci_unregister_driver(&sec_pci_driver);
	sec_unregister_debugfs();
}

module_init(sec_init);
module_exit(sec_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Zaibo Xu <xuzaibo@huawei.com>");
MODULE_AUTHOR("Longfang Liu <liulongfang@huawei.com>");
MODULE_AUTHOR("Wei Zhang <zhangwei375@huawei.com>");
MODULE_DESCRIPTION("Driver for HiSilicon SEC accelerator");
