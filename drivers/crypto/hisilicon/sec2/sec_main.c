// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 HiSilicon Limited. */

#include <linux/acpi.h>
#include <linux/aer.h>
#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/seq_file.h>
#include <linux/topology.h>
#include <linux/uacce.h>

#include "sec.h"

#define SEC_VF_NUM			63
#define SEC_QUEUE_NUM_V1		4096
#define SEC_PF_PCI_DEVICE_ID		0xa255
#define SEC_VF_PCI_DEVICE_ID		0xa256

#define SEC_BD_ERR_CHK_EN0		0xEFFFFFFF
#define SEC_BD_ERR_CHK_EN1		0x7ffff7fd
#define SEC_BD_ERR_CHK_EN3		0xffffbfff

#define SEC_SQE_SIZE			128
#define SEC_SQ_SIZE			(SEC_SQE_SIZE * QM_Q_DEPTH)
#define SEC_PF_DEF_Q_NUM		256
#define SEC_PF_DEF_Q_BASE		0
#define SEC_CTX_Q_NUM_DEF		2
#define SEC_CTX_Q_NUM_MAX		32

#define SEC_CTRL_CNT_CLR_CE		0x301120
#define SEC_CTRL_CNT_CLR_CE_BIT	BIT(0)
#define SEC_CORE_INT_SOURCE		0x301010
#define SEC_CORE_INT_MASK		0x301000
#define SEC_CORE_INT_STATUS		0x301008
#define SEC_CORE_SRAM_ECC_ERR_INFO	0x301C14
#define SEC_ECC_NUM			16
#define SEC_ECC_MASH			0xFF
#define SEC_CORE_INT_DISABLE		0x0
#define SEC_CORE_INT_ENABLE		0x1ff
#define SEC_CORE_INT_CLEAR		0x1ff
#define SEC_SAA_ENABLE			0x17f

#define SEC_RAS_CE_REG			0x301050
#define SEC_RAS_FE_REG			0x301054
#define SEC_RAS_NFE_REG			0x301058
#define SEC_RAS_CE_ENB_MSK		0x88
#define SEC_RAS_FE_ENB_MSK		0x0
#define SEC_RAS_NFE_ENB_MSK		0x177
#define SEC_RAS_DISABLE		0x0
#define SEC_MEM_START_INIT_REG	0x301100
#define SEC_MEM_INIT_DONE_REG		0x301104

#define SEC_CONTROL_REG		0x301200
#define SEC_TRNG_EN_SHIFT		8
#define SEC_CLK_GATE_ENABLE		BIT(3)
#define SEC_CLK_GATE_DISABLE		(~BIT(3))
#define SEC_AXI_SHUTDOWN_ENABLE	BIT(12)
#define SEC_AXI_SHUTDOWN_DISABLE	0xFFFFEFFF

#define SEC_INTERFACE_USER_CTRL0_REG	0x301220
#define SEC_INTERFACE_USER_CTRL1_REG	0x301224
#define SEC_SAA_EN_REG			0x301270
#define SEC_BD_ERR_CHK_EN_REG0		0x301380
#define SEC_BD_ERR_CHK_EN_REG1		0x301384
#define SEC_BD_ERR_CHK_EN_REG3		0x30138c

#define SEC_USER0_SMMU_NORMAL		(BIT(23) | BIT(15))
#define SEC_USER1_SMMU_NORMAL		(BIT(31) | BIT(23) | BIT(15) | BIT(7))
#define SEC_USER1_ENABLE_CONTEXT_SSV	BIT(24)
#define SEC_USER1_ENABLE_DATA_SSV	BIT(16)
#define SEC_USER1_WB_CONTEXT_SSV	BIT(8)
#define SEC_USER1_WB_DATA_SSV		BIT(0)
#define SEC_USER1_SVA_SET		(SEC_USER1_ENABLE_CONTEXT_SSV | \
					SEC_USER1_ENABLE_DATA_SSV | \
					SEC_USER1_WB_CONTEXT_SSV |  \
					SEC_USER1_WB_DATA_SSV)
#define SEC_USER1_SMMU_SVA		(SEC_USER1_SMMU_NORMAL | SEC_USER1_SVA_SET)
#define SEC_USER1_SMMU_MASK		(~SEC_USER1_SVA_SET)
#define SEC_CORE_INT_STATUS_M_ECC	BIT(2)

#define SEC_DELAY_10_US			10
#define SEC_POLL_TIMEOUT_US		1000
#define SEC_DBGFS_VAL_MAX_LEN		20
#define SEC_SINGLE_PORT_MAX_TRANS	0x2060

#define SEC_SQE_MASK_OFFSET		64
#define SEC_SQE_MASK_LEN		48

struct sec_hw_error {
	u32 int_msk;
	const char *msg;
};

struct sec_dfx_item {
	const char *name;
	u32 offset;
};

static const char sec_name[] = "hisi_sec2";
static struct dentry *sec_debugfs_root;

static struct hisi_qm_list sec_devices = {
	.register_to_crypto	= sec_register_to_crypto,
	.unregister_from_crypto	= sec_unregister_from_crypto,
};

static const struct sec_hw_error sec_hw_errors[] = {
	{
		.int_msk = BIT(0),
		.msg = "sec_axi_rresp_err_rint"
	},
	{
		.int_msk = BIT(1),
		.msg = "sec_axi_bresp_err_rint"
	},
	{
		.int_msk = BIT(2),
		.msg = "sec_ecc_2bit_err_rint"
	},
	{
		.int_msk = BIT(3),
		.msg = "sec_ecc_1bit_err_rint"
	},
	{
		.int_msk = BIT(4),
		.msg = "sec_req_trng_timeout_rint"
	},
	{
		.int_msk = BIT(5),
		.msg = "sec_fsm_hbeat_rint"
	},
	{
		.int_msk = BIT(6),
		.msg = "sec_channel_req_rng_timeout_rint"
	},
	{
		.int_msk = BIT(7),
		.msg = "sec_bd_err_rint"
	},
	{
		.int_msk = BIT(8),
		.msg = "sec_chain_buff_err_rint"
	},
	{}
};

static const char * const sec_dbg_file_name[] = {
	[SEC_CLEAR_ENABLE] = "clear_enable",
};

static struct sec_dfx_item sec_dfx_labels[] = {
	{"send_cnt", offsetof(struct sec_dfx, send_cnt)},
	{"recv_cnt", offsetof(struct sec_dfx, recv_cnt)},
	{"send_busy_cnt", offsetof(struct sec_dfx, send_busy_cnt)},
	{"recv_busy_cnt", offsetof(struct sec_dfx, recv_busy_cnt)},
	{"err_bd_cnt", offsetof(struct sec_dfx, err_bd_cnt)},
	{"invalid_req_cnt", offsetof(struct sec_dfx, invalid_req_cnt)},
	{"done_flag_cnt", offsetof(struct sec_dfx, done_flag_cnt)},
};

static const struct debugfs_reg32 sec_dfx_regs[] = {
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
	return q_num_set(val, kp, SEC_PF_PCI_DEVICE_ID);
}

static const struct kernel_param_ops sec_pf_q_num_ops = {
	.set = sec_pf_q_num_set,
	.get = param_get_int,
};

static u32 pf_q_num = SEC_PF_DEF_Q_NUM;
module_param_cb(pf_q_num, &sec_pf_q_num_ops, &pf_q_num, 0444);
MODULE_PARM_DESC(pf_q_num, "Number of queues in PF(v1 2-4096, v2 2-1024)");

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
MODULE_PARM_DESC(ctx_q_num, "Queue num in ctx (2 default, 2, 4, ..., 32)");

static const struct kernel_param_ops vfs_num_ops = {
	.set = vfs_num_set,
	.get = param_get_int,
};

static u32 vfs_num;
module_param_cb(vfs_num, &vfs_num_ops, &vfs_num, 0444);
MODULE_PARM_DESC(vfs_num, "Number of VFs to enable(1-63), 0(default)");

void sec_destroy_qps(struct hisi_qp **qps, int qp_num)
{
	hisi_qm_free_qps(qps, qp_num);
	kfree(qps);
}

struct hisi_qp **sec_create_qps(void)
{
	int node = cpu_to_node(smp_processor_id());
	u32 ctx_num = ctx_q_num;
	struct hisi_qp **qps;
	int ret;

	qps = kcalloc(ctx_num, sizeof(struct hisi_qp *), GFP_KERNEL);
	if (!qps)
		return NULL;

	ret = hisi_qm_alloc_qps_node(&sec_devices, ctx_num, 0, node, qps);
	if (!ret)
		return qps;

	kfree(qps);
	return NULL;
}

static const struct kernel_param_ops sec_uacce_mode_ops = {
	.set = uacce_mode_set,
	.get = param_get_int,
};

/*
 * uacce_mode = 0 means sec only register to crypto,
 * uacce_mode = 1 means sec both register to crypto and uacce.
 */
static u32 uacce_mode = UACCE_MODE_NOUACCE;
module_param_cb(uacce_mode, &sec_uacce_mode_ops, &uacce_mode, 0444);
MODULE_PARM_DESC(uacce_mode, UACCE_MODE_DESC);

static const struct pci_device_id sec_dev_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_HUAWEI, SEC_PF_PCI_DEVICE_ID) },
	{ PCI_DEVICE(PCI_VENDOR_ID_HUAWEI, SEC_VF_PCI_DEVICE_ID) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, sec_dev_ids);

static u8 sec_get_endian(struct hisi_qm *qm)
{
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
	reg = readl_relaxed(qm->io_base + SEC_CONTROL_REG);
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

static int sec_engine_init(struct hisi_qm *qm)
{
	int ret;
	u32 reg;

	/* disable clock gate control */
	reg = readl_relaxed(qm->io_base + SEC_CONTROL_REG);
	reg &= SEC_CLK_GATE_DISABLE;
	writel_relaxed(reg, qm->io_base + SEC_CONTROL_REG);

	writel_relaxed(0x1, qm->io_base + SEC_MEM_START_INIT_REG);

	ret = readl_relaxed_poll_timeout(qm->io_base + SEC_MEM_INIT_DONE_REG,
					 reg, reg & 0x1, SEC_DELAY_10_US,
					 SEC_POLL_TIMEOUT_US);
	if (ret) {
		pci_err(qm->pdev, "fail to init sec mem\n");
		return ret;
	}

	reg = readl_relaxed(qm->io_base + SEC_CONTROL_REG);
	reg |= (0x1 << SEC_TRNG_EN_SHIFT);
	writel_relaxed(reg, qm->io_base + SEC_CONTROL_REG);

	reg = readl_relaxed(qm->io_base + SEC_INTERFACE_USER_CTRL0_REG);
	reg |= SEC_USER0_SMMU_NORMAL;
	writel_relaxed(reg, qm->io_base + SEC_INTERFACE_USER_CTRL0_REG);

	reg = readl_relaxed(qm->io_base + SEC_INTERFACE_USER_CTRL1_REG);
	reg &= SEC_USER1_SMMU_MASK;
	if (qm->use_sva && qm->ver == QM_HW_V2)
		reg |= SEC_USER1_SMMU_SVA;
	else
		reg |= SEC_USER1_SMMU_NORMAL;
	writel_relaxed(reg, qm->io_base + SEC_INTERFACE_USER_CTRL1_REG);

	writel(SEC_SINGLE_PORT_MAX_TRANS,
	       qm->io_base + AM_CFG_SINGLE_PORT_MAX_TRANS);

	writel(SEC_SAA_ENABLE, qm->io_base + SEC_SAA_EN_REG);

	/* Enable sm4 extra mode, as ctr/ecb */
	writel_relaxed(SEC_BD_ERR_CHK_EN0,
		       qm->io_base + SEC_BD_ERR_CHK_EN_REG0);
	/* Enable sm4 xts mode multiple iv */
	writel_relaxed(SEC_BD_ERR_CHK_EN1,
		       qm->io_base + SEC_BD_ERR_CHK_EN_REG1);
	writel_relaxed(SEC_BD_ERR_CHK_EN3,
		       qm->io_base + SEC_BD_ERR_CHK_EN_REG3);

	/* config endian */
	reg = readl_relaxed(qm->io_base + SEC_CONTROL_REG);
	reg |= sec_get_endian(qm);
	writel_relaxed(reg, qm->io_base + SEC_CONTROL_REG);

	return 0;
}

static int sec_set_user_domain_and_cache(struct hisi_qm *qm)
{
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

	return sec_engine_init(qm);
}

/* sec_debug_regs_clear() - clear the sec debug regs */
static void sec_debug_regs_clear(struct hisi_qm *qm)
{
	int i;

	/* clear sec dfx regs */
	writel(0x1, qm->io_base + SEC_CTRL_CNT_CLR_CE);
	for (i = 0; i < ARRAY_SIZE(sec_dfx_regs); i++)
		readl(qm->io_base + sec_dfx_regs[i].offset);

	/* clear rdclr_en */
	writel(0x0, qm->io_base + SEC_CTRL_CNT_CLR_CE);

	hisi_qm_debug_regs_clear(qm);
}

static void sec_hw_error_enable(struct hisi_qm *qm)
{
	u32 val;

	if (qm->ver == QM_HW_V1) {
		writel(SEC_CORE_INT_DISABLE, qm->io_base + SEC_CORE_INT_MASK);
		pci_info(qm->pdev, "V1 not support hw error handle\n");
		return;
	}

	val = readl(qm->io_base + SEC_CONTROL_REG);

	/* clear SEC hw error source if having */
	writel(SEC_CORE_INT_CLEAR, qm->io_base + SEC_CORE_INT_SOURCE);

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

static int sec_debugfs_atomic64_set(void *data, u64 val)
{
	if (val)
		return -EINVAL;

	atomic64_set((atomic64_t *)data, 0);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(sec_atomic64_ops, sec_debugfs_atomic64_get,
			 sec_debugfs_atomic64_set, "%lld\n");

static int sec_core_debug_init(struct hisi_qm *qm)
{
	struct sec_dev *sec = container_of(qm, struct sec_dev, qm);
	struct device *dev = &qm->pdev->dev;
	struct sec_dfx *dfx = &sec->debug.dfx;
	struct debugfs_regset32 *regset;
	struct dentry *tmp_d;
	int i;

	tmp_d = debugfs_create_dir("sec_dfx", qm->debug.debug_root);

	regset = devm_kzalloc(dev, sizeof(*regset), GFP_KERNEL);
	if (!regset)
		return -ENOMEM;

	regset->regs = sec_dfx_regs;
	regset->nregs = ARRAY_SIZE(sec_dfx_regs);
	regset->base = qm->io_base;

	if (qm->pdev->device == SEC_PF_PCI_DEVICE_ID)
		debugfs_create_regset32("regs", 0444, tmp_d, regset);

	for (i = 0; i < ARRAY_SIZE(sec_dfx_labels); i++) {
		atomic64_t *data = (atomic64_t *)((uintptr_t)dfx +
					sec_dfx_labels[i].offset);
		debugfs_create_file(sec_dfx_labels[i].name, 0644,
				   tmp_d, data, &sec_atomic64_ops);
	}

	return 0;
}

static int sec_debug_init(struct hisi_qm *qm)
{
	struct sec_dev *sec = container_of(qm, struct sec_dev, qm);
	int i;

	if (qm->pdev->device == SEC_PF_PCI_DEVICE_ID) {
		for (i = SEC_CLEAR_ENABLE; i < SEC_DEBUG_FILE_NUM; i++) {
			spin_lock_init(&sec->debug.files[i].lock);
			sec->debug.files[i].index = i;
			sec->debug.files[i].qm = qm;

			debugfs_create_file(sec_dbg_file_name[i], 0600,
						  qm->debug.debug_root,
						  sec->debug.files + i,
						  &sec_dbg_fops);
		}
	}

	return sec_core_debug_init(qm);
}

static int sec_debugfs_init(struct hisi_qm *qm)
{
	struct device *dev = &qm->pdev->dev;
	int ret;

	qm->debug.debug_root = debugfs_create_dir(dev_name(dev),
						  sec_debugfs_root);
	qm->debug.sqe_mask_offset = SEC_SQE_MASK_OFFSET;
	qm->debug.sqe_mask_len = SEC_SQE_MASK_LEN;
	hisi_qm_debug_init(qm);

	ret = sec_debug_init(qm);
	if (ret)
		goto failed_to_create;

	return 0;

failed_to_create:
	debugfs_remove_recursive(sec_debugfs_root);
	return ret;
}

static void sec_debugfs_exit(struct hisi_qm *qm)
{
	debugfs_remove_recursive(qm->debug.debug_root);
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
						((err_val) >> SEC_ECC_NUM) &
						SEC_ECC_MASH);
			}
		}
		errs++;
	}
}

static u32 sec_get_hw_err_status(struct hisi_qm *qm)
{
	return readl(qm->io_base + SEC_CORE_INT_STATUS);
}

static void sec_clear_hw_err_status(struct hisi_qm *qm, u32 err_sts)
{
	writel(err_sts, qm->io_base + SEC_CORE_INT_SOURCE);
}

static void sec_open_axi_master_ooo(struct hisi_qm *qm)
{
	u32 val;

	val = readl(qm->io_base + SEC_CONTROL_REG);
	writel(val & SEC_AXI_SHUTDOWN_DISABLE, qm->io_base + SEC_CONTROL_REG);
	writel(val | SEC_AXI_SHUTDOWN_ENABLE, qm->io_base + SEC_CONTROL_REG);
}

static const struct hisi_qm_err_ini sec_err_ini = {
	.hw_init		= sec_set_user_domain_and_cache,
	.hw_err_enable		= sec_hw_error_enable,
	.hw_err_disable		= sec_hw_error_disable,
	.get_dev_hw_err_status	= sec_get_hw_err_status,
	.clear_dev_hw_err_status = sec_clear_hw_err_status,
	.log_dev_hw_err		= sec_log_hw_error,
	.open_axi_master_ooo	= sec_open_axi_master_ooo,
	.err_info		= {
		.ce		= QM_BASE_CE,
		.nfe		= QM_BASE_NFE | QM_ACC_DO_TASK_TIMEOUT |
				  QM_ACC_WB_NOT_READY_TIMEOUT,
		.fe		= 0,
		.ecc_2bits_mask	= SEC_CORE_INT_STATUS_M_ECC,
		.dev_ce_mask	= SEC_RAS_CE_ENB_MSK,
		.msi_wr_port	= BIT(0),
		.acpi_rst	= "SRST",
	}
};

static int sec_pf_probe_init(struct sec_dev *sec)
{
	struct hisi_qm *qm = &sec->qm;
	int ret;

	qm->err_ini = &sec_err_ini;

	ret = sec_set_user_domain_and_cache(qm);
	if (ret)
		return ret;

	hisi_qm_dev_err_init(qm);
	sec_debug_regs_clear(qm);

	return 0;
}

static int sec_qm_init(struct hisi_qm *qm, struct pci_dev *pdev)
{
	int ret;

	qm->pdev = pdev;
	qm->ver = pdev->revision;
	qm->algs = "cipher\ndigest\naead";
	qm->mode = uacce_mode;
	qm->sqe_size = SEC_SQE_SIZE;
	qm->dev_name = sec_name;

	qm->fun_type = (pdev->device == SEC_PF_PCI_DEVICE_ID) ?
			QM_HW_PF : QM_HW_VF;
	if (qm->fun_type == QM_HW_PF) {
		qm->qp_base = SEC_PF_DEF_Q_BASE;
		qm->qp_num = pf_q_num;
		qm->debug.curr_qm_qp_num = pf_q_num;
		qm->qm_list = &sec_devices;
	} else if (qm->fun_type == QM_HW_VF && qm->ver == QM_HW_V1) {
		/*
		 * have no way to get qm configure in VM in v1 hardware,
		 * so currently force PF to uses SEC_PF_DEF_Q_NUM, and force
		 * to trigger only one VF in v1 hardware.
		 * v2 hardware has no such problem.
		 */
		qm->qp_base = SEC_PF_DEF_Q_NUM;
		qm->qp_num = SEC_QUEUE_NUM_V1 - SEC_PF_DEF_Q_NUM;
	}

	/*
	 * WQ_HIGHPRI: SEC request must be low delayed,
	 * so need a high priority workqueue.
	 * WQ_UNBOUND: SEC task is likely with long
	 * running CPU intensive workloads.
	 */
	qm->wq = alloc_workqueue("%s", WQ_HIGHPRI | WQ_MEM_RECLAIM |
				 WQ_UNBOUND, num_online_cpus(),
				 pci_name(qm->pdev));
	if (!qm->wq) {
		pci_err(qm->pdev, "fail to alloc workqueue\n");
		return -ENOMEM;
	}

	ret = hisi_qm_init(qm);
	if (ret)
		destroy_workqueue(qm->wq);

	return ret;
}

static void sec_qm_uninit(struct hisi_qm *qm)
{
	hisi_qm_uninit(qm);
}

static int sec_probe_init(struct sec_dev *sec)
{
	struct hisi_qm *qm = &sec->qm;
	int ret;

	if (qm->fun_type == QM_HW_PF) {
		ret = sec_pf_probe_init(sec);
		if (ret)
			return ret;
	}

	return 0;
}

static void sec_probe_uninit(struct hisi_qm *qm)
{
	hisi_qm_dev_err_uninit(qm);

	destroy_workqueue(qm->wq);
}

static void sec_iommu_used_check(struct sec_dev *sec)
{
	struct iommu_domain *domain;
	struct device *dev = &sec->qm.pdev->dev;

	domain = iommu_get_domain_for_dev(dev);

	/* Check if iommu is used */
	sec->iommu_used = false;
	if (domain) {
		if (domain->type & __IOMMU_DOMAIN_PAGING)
			sec->iommu_used = true;
		dev_info(dev, "SMMU Opened, the iommu type = %u\n",
			domain->type);
	}
}

static int sec_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct sec_dev *sec;
	struct hisi_qm *qm;
	int ret;

	sec = devm_kzalloc(&pdev->dev, sizeof(*sec), GFP_KERNEL);
	if (!sec)
		return -ENOMEM;

	qm = &sec->qm;
	ret = sec_qm_init(qm, pdev);
	if (ret) {
		pci_err(pdev, "Failed to init SEC QM (%d)!\n", ret);
		return ret;
	}

	sec->ctx_q_num = ctx_q_num;
	sec_iommu_used_check(sec);

	ret = sec_probe_init(sec);
	if (ret) {
		pci_err(pdev, "Failed to probe!\n");
		goto err_qm_uninit;
	}

	ret = hisi_qm_start(qm);
	if (ret) {
		pci_err(pdev, "Failed to start sec qm!\n");
		goto err_probe_uninit;
	}

	ret = sec_debugfs_init(qm);
	if (ret)
		pci_warn(pdev, "Failed to init debugfs!\n");

	ret = hisi_qm_alg_register(qm, &sec_devices);
	if (ret < 0) {
		pr_err("Failed to register driver to crypto.\n");
		goto err_qm_stop;
	}

	if (qm->uacce) {
		ret = uacce_register(qm->uacce);
		if (ret) {
			pci_err(pdev, "failed to register uacce (%d)!\n", ret);
			goto err_alg_unregister;
		}
	}

	if (qm->fun_type == QM_HW_PF && vfs_num) {
		ret = hisi_qm_sriov_enable(pdev, vfs_num);
		if (ret < 0)
			goto err_alg_unregister;
	}

	return 0;

err_alg_unregister:
	hisi_qm_alg_unregister(qm, &sec_devices);
err_qm_stop:
	sec_debugfs_exit(qm);
	hisi_qm_stop(qm, QM_NORMAL);
err_probe_uninit:
	sec_probe_uninit(qm);
err_qm_uninit:
	sec_qm_uninit(qm);
	return ret;
}

static void sec_remove(struct pci_dev *pdev)
{
	struct hisi_qm *qm = pci_get_drvdata(pdev);

	hisi_qm_wait_task_finish(qm, &sec_devices);
	hisi_qm_alg_unregister(qm, &sec_devices);
	if (qm->fun_type == QM_HW_PF && qm->vfs_num)
		hisi_qm_sriov_disable(pdev, true);

	sec_debugfs_exit(qm);

	(void)hisi_qm_stop(qm, QM_NORMAL);

	if (qm->fun_type == QM_HW_PF)
		sec_debug_regs_clear(qm);

	sec_probe_uninit(qm);

	sec_qm_uninit(qm);
}

static const struct pci_error_handlers sec_err_handler = {
	.error_detected = hisi_qm_dev_err_detected,
	.slot_reset	= hisi_qm_dev_slot_reset,
	.reset_prepare	= hisi_qm_reset_prepare,
	.reset_done	= hisi_qm_reset_done,
};

static struct pci_driver sec_pci_driver = {
	.name = "hisi_sec2",
	.id_table = sec_dev_ids,
	.probe = sec_probe,
	.remove = sec_remove,
	.err_handler = &sec_err_handler,
	.sriov_configure = hisi_qm_sriov_configure,
	.shutdown = hisi_qm_dev_shutdown,
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

	hisi_qm_init_list(&sec_devices);
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
MODULE_AUTHOR("Kai Ye <yekai13@huawei.com>");
MODULE_AUTHOR("Wei Zhang <zhangwei375@huawei.com>");
MODULE_DESCRIPTION("Driver for HiSilicon SEC accelerator");
