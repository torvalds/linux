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
#include <linux/uacce.h>
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
#define HZIP_FSM_MAX_CNT		0x301008

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

#define HZIP_QM_IDEL_STATUS		0x3040e4

#define HZIP_CORE_DEBUG_COMP_0		0x302000
#define HZIP_CORE_DEBUG_COMP_1		0x303000
#define HZIP_CORE_DEBUG_DECOMP_0	0x304000
#define HZIP_CORE_DEBUG_DECOMP_1	0x305000
#define HZIP_CORE_DEBUG_DECOMP_2	0x306000
#define HZIP_CORE_DEBUG_DECOMP_3	0x307000
#define HZIP_CORE_DEBUG_DECOMP_4	0x308000
#define HZIP_CORE_DEBUG_DECOMP_5	0x309000

#define HZIP_CORE_INT_SOURCE		0x3010A0
#define HZIP_CORE_INT_MASK_REG		0x3010A4
#define HZIP_CORE_INT_STATUS		0x3010AC
#define HZIP_CORE_INT_STATUS_M_ECC	BIT(1)
#define HZIP_CORE_SRAM_ECC_ERR_INFO	0x301148
#define HZIP_CORE_INT_RAS_CE_ENB	0x301160
#define HZIP_CORE_INT_RAS_NFE_ENB	0x301164
#define HZIP_CORE_INT_RAS_FE_ENB        0x301168
#define HZIP_CORE_INT_RAS_NFE_ENABLE	0x7FE
#define HZIP_SRAM_ECC_ERR_NUM_SHIFT	16
#define HZIP_SRAM_ECC_ERR_ADDR_SHIFT	24
#define HZIP_CORE_INT_MASK_ALL		GENMASK(10, 0)
#define HZIP_COMP_CORE_NUM		2
#define HZIP_DECOMP_CORE_NUM		6
#define HZIP_CORE_NUM			(HZIP_COMP_CORE_NUM + \
					 HZIP_DECOMP_CORE_NUM)
#define HZIP_SQE_SIZE			128
#define HZIP_SQ_SIZE			(HZIP_SQE_SIZE * QM_Q_DEPTH)
#define HZIP_PF_DEF_Q_NUM		64
#define HZIP_PF_DEF_Q_BASE		0

#define HZIP_SOFT_CTRL_CNT_CLR_CE	0x301000
#define SOFT_CTRL_CNT_CLR_CE_BIT	BIT(0)

#define HZIP_BUF_SIZE			22

static const char hisi_zip_name[] = "hisi_zip";
static struct dentry *hzip_debugfs_root;
static struct hisi_qm_list zip_devices;

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

enum ctrl_debug_file_index {
	HZIP_CURRENT_QM,
	HZIP_CLEAR_ENABLE,
	HZIP_DEBUG_FILE_NUM,
};

static const char * const ctrl_debug_file_name[] = {
	[HZIP_CURRENT_QM]   = "current_qm",
	[HZIP_CLEAR_ENABLE] = "clear_enable",
};

struct ctrl_debug_file {
	enum ctrl_debug_file_index index;
	spinlock_t lock;
	struct hisi_zip_ctrl *ctrl;
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
	struct dentry *debug_root;
	struct ctrl_debug_file files[HZIP_DEBUG_FILE_NUM];
};

enum {
	HZIP_COMP_CORE0,
	HZIP_COMP_CORE1,
	HZIP_DECOMP_CORE0,
	HZIP_DECOMP_CORE1,
	HZIP_DECOMP_CORE2,
	HZIP_DECOMP_CORE3,
	HZIP_DECOMP_CORE4,
	HZIP_DECOMP_CORE5,
};

static const u64 core_offsets[] = {
	[HZIP_COMP_CORE0]   = 0x302000,
	[HZIP_COMP_CORE1]   = 0x303000,
	[HZIP_DECOMP_CORE0] = 0x304000,
	[HZIP_DECOMP_CORE1] = 0x305000,
	[HZIP_DECOMP_CORE2] = 0x306000,
	[HZIP_DECOMP_CORE3] = 0x307000,
	[HZIP_DECOMP_CORE4] = 0x308000,
	[HZIP_DECOMP_CORE5] = 0x309000,
};

static struct debugfs_reg32 hzip_dfx_regs[] = {
	{"HZIP_GET_BD_NUM                ",  0x00ull},
	{"HZIP_GET_RIGHT_BD              ",  0x04ull},
	{"HZIP_GET_ERROR_BD              ",  0x08ull},
	{"HZIP_DONE_BD_NUM               ",  0x0cull},
	{"HZIP_WORK_CYCLE                ",  0x10ull},
	{"HZIP_IDLE_CYCLE                ",  0x18ull},
	{"HZIP_MAX_DELAY                 ",  0x20ull},
	{"HZIP_MIN_DELAY                 ",  0x24ull},
	{"HZIP_AVG_DELAY                 ",  0x28ull},
	{"HZIP_MEM_VISIBLE_DATA          ",  0x30ull},
	{"HZIP_MEM_VISIBLE_ADDR          ",  0x34ull},
	{"HZIP_COMSUMED_BYTE             ",  0x38ull},
	{"HZIP_PRODUCED_BYTE             ",  0x40ull},
	{"HZIP_COMP_INF                  ",  0x70ull},
	{"HZIP_PRE_OUT                   ",  0x78ull},
	{"HZIP_BD_RD                     ",  0x7cull},
	{"HZIP_BD_WR                     ",  0x80ull},
	{"HZIP_GET_BD_AXI_ERR_NUM        ",  0x84ull},
	{"HZIP_GET_BD_PARSE_ERR_NUM      ",  0x88ull},
	{"HZIP_ADD_BD_AXI_ERR_NUM        ",  0x8cull},
	{"HZIP_DECOMP_STF_RELOAD_CURR_ST ",  0x94ull},
	{"HZIP_DECOMP_LZ77_CURR_ST       ",  0x9cull},
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

static u32 vfs_num;
module_param(vfs_num, uint, 0444);
MODULE_PARM_DESC(vfs_num, "Number of VFs to enable(1-63)");

static const struct pci_device_id hisi_zip_dev_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_HUAWEI, PCI_DEVICE_ID_ZIP_PF) },
	{ PCI_DEVICE(PCI_VENDOR_ID_HUAWEI, PCI_DEVICE_ID_ZIP_VF) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, hisi_zip_dev_ids);

int zip_create_qps(struct hisi_qp **qps, int qp_num)
{
	int node = cpu_to_node(smp_processor_id());

	return hisi_qm_alloc_qps_node(&zip_devices, qp_num, 0, node, qps);
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

	if (hisi_zip->qm.use_sva) {
		writel(AXUSER_BASE | AXUSER_SSV, base + HZIP_DATA_RUSER_32_63);
		writel(AXUSER_BASE | AXUSER_SSV, base + HZIP_DATA_WUSER_32_63);
	} else {
		writel(AXUSER_BASE, base + HZIP_DATA_RUSER_32_63);
		writel(AXUSER_BASE, base + HZIP_DATA_WUSER_32_63);
	}

	/* let's open all compression/decompression cores */
	writel(DECOMP_CHECK_ENABLE | ALL_COMP_DECOMP_EN,
	       base + HZIP_CLOCK_GATE_CTRL);

	/* enable sqc writeback */
	writel(SQC_CACHE_ENABLE | CQC_CACHE_ENABLE | SQC_CACHE_WB_ENABLE |
	       CQC_CACHE_WB_ENABLE | FIELD_PREP(SQC_CACHE_WB_THRD, 1) |
	       FIELD_PREP(CQC_CACHE_WB_THRD, 1), base + QM_CACHE_CTL);
}

static void hisi_zip_hw_error_enable(struct hisi_qm *qm)
{
	if (qm->ver == QM_HW_V1) {
		writel(HZIP_CORE_INT_MASK_ALL,
		       qm->io_base + HZIP_CORE_INT_MASK_REG);
		dev_info(&qm->pdev->dev, "Does not support hw error handle\n");
		return;
	}

	/* clear ZIP hw error source if having */
	writel(HZIP_CORE_INT_MASK_ALL, qm->io_base + HZIP_CORE_INT_SOURCE);

	/* configure error type */
	writel(0x1, qm->io_base + HZIP_CORE_INT_RAS_CE_ENB);
	writel(0x0, qm->io_base + HZIP_CORE_INT_RAS_FE_ENB);
	writel(HZIP_CORE_INT_RAS_NFE_ENABLE,
		qm->io_base + HZIP_CORE_INT_RAS_NFE_ENB);

	/* enable ZIP hw error interrupts */
	writel(0, qm->io_base + HZIP_CORE_INT_MASK_REG);
}

static void hisi_zip_hw_error_disable(struct hisi_qm *qm)
{
	/* disable ZIP hw error interrupts */
	writel(HZIP_CORE_INT_MASK_ALL, qm->io_base + HZIP_CORE_INT_MASK_REG);
}

static inline struct hisi_qm *file_to_qm(struct ctrl_debug_file *file)
{
	struct hisi_zip *hisi_zip = file->ctrl->hisi_zip;

	return &hisi_zip->qm;
}

static u32 current_qm_read(struct ctrl_debug_file *file)
{
	struct hisi_qm *qm = file_to_qm(file);

	return readl(qm->io_base + QM_DFX_MB_CNT_VF);
}

static int current_qm_write(struct ctrl_debug_file *file, u32 val)
{
	struct hisi_qm *qm = file_to_qm(file);
	struct hisi_zip_ctrl *ctrl = file->ctrl;
	u32 vfq_num;
	u32 tmp;

	if (val > ctrl->num_vfs)
		return -EINVAL;

	/* Calculate curr_qm_qp_num and store */
	if (val == 0) {
		qm->debug.curr_qm_qp_num = qm->qp_num;
	} else {
		vfq_num = (qm->ctrl_qp_num - qm->qp_num) / ctrl->num_vfs;
		if (val == ctrl->num_vfs)
			qm->debug.curr_qm_qp_num = qm->ctrl_qp_num -
				qm->qp_num - (ctrl->num_vfs - 1) * vfq_num;
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

	return  0;
}

static u32 clear_enable_read(struct ctrl_debug_file *file)
{
	struct hisi_qm *qm = file_to_qm(file);

	return readl(qm->io_base + HZIP_SOFT_CTRL_CNT_CLR_CE) &
	       SOFT_CTRL_CNT_CLR_CE_BIT;
}

static int clear_enable_write(struct ctrl_debug_file *file, u32 val)
{
	struct hisi_qm *qm = file_to_qm(file);
	u32 tmp;

	if (val != 1 && val != 0)
		return -EINVAL;

	tmp = (readl(qm->io_base + HZIP_SOFT_CTRL_CNT_CLR_CE) &
	       ~SOFT_CTRL_CNT_CLR_CE_BIT) | val;
	writel(tmp, qm->io_base + HZIP_SOFT_CTRL_CNT_CLR_CE);

	return  0;
}

static ssize_t ctrl_debug_read(struct file *filp, char __user *buf,
			       size_t count, loff_t *pos)
{
	struct ctrl_debug_file *file = filp->private_data;
	char tbuf[HZIP_BUF_SIZE];
	u32 val;
	int ret;

	spin_lock_irq(&file->lock);
	switch (file->index) {
	case HZIP_CURRENT_QM:
		val = current_qm_read(file);
		break;
	case HZIP_CLEAR_ENABLE:
		val = clear_enable_read(file);
		break;
	default:
		spin_unlock_irq(&file->lock);
		return -EINVAL;
	}
	spin_unlock_irq(&file->lock);
	ret = sprintf(tbuf, "%u\n", val);
	return simple_read_from_buffer(buf, count, pos, tbuf, ret);
}

static ssize_t ctrl_debug_write(struct file *filp, const char __user *buf,
				size_t count, loff_t *pos)
{
	struct ctrl_debug_file *file = filp->private_data;
	char tbuf[HZIP_BUF_SIZE];
	unsigned long val;
	int len, ret;

	if (*pos != 0)
		return 0;

	if (count >= HZIP_BUF_SIZE)
		return -ENOSPC;

	len = simple_write_to_buffer(tbuf, HZIP_BUF_SIZE - 1, pos, buf, count);
	if (len < 0)
		return len;

	tbuf[len] = '\0';
	if (kstrtoul(tbuf, 0, &val))
		return -EFAULT;

	spin_lock_irq(&file->lock);
	switch (file->index) {
	case HZIP_CURRENT_QM:
		ret = current_qm_write(file, val);
		if (ret)
			goto err_input;
		break;
	case HZIP_CLEAR_ENABLE:
		ret = clear_enable_write(file, val);
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

static const struct file_operations ctrl_debug_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = ctrl_debug_read,
	.write = ctrl_debug_write,
};

static int hisi_zip_core_debug_init(struct hisi_zip_ctrl *ctrl)
{
	struct hisi_zip *hisi_zip = ctrl->hisi_zip;
	struct hisi_qm *qm = &hisi_zip->qm;
	struct device *dev = &qm->pdev->dev;
	struct debugfs_regset32 *regset;
	struct dentry *tmp_d;
	char buf[HZIP_BUF_SIZE];
	int i;

	for (i = 0; i < HZIP_CORE_NUM; i++) {
		if (i < HZIP_COMP_CORE_NUM)
			sprintf(buf, "comp_core%d", i);
		else
			sprintf(buf, "decomp_core%d", i - HZIP_COMP_CORE_NUM);

		regset = devm_kzalloc(dev, sizeof(*regset), GFP_KERNEL);
		if (!regset)
			return -ENOENT;

		regset->regs = hzip_dfx_regs;
		regset->nregs = ARRAY_SIZE(hzip_dfx_regs);
		regset->base = qm->io_base + core_offsets[i];

		tmp_d = debugfs_create_dir(buf, ctrl->debug_root);
		debugfs_create_regset32("regs", 0444, tmp_d, regset);
	}

	return 0;
}

static int hisi_zip_ctrl_debug_init(struct hisi_zip_ctrl *ctrl)
{
	int i;

	for (i = HZIP_CURRENT_QM; i < HZIP_DEBUG_FILE_NUM; i++) {
		spin_lock_init(&ctrl->files[i].lock);
		ctrl->files[i].ctrl = ctrl;
		ctrl->files[i].index = i;

		debugfs_create_file(ctrl_debug_file_name[i], 0600,
				    ctrl->debug_root, ctrl->files + i,
				    &ctrl_debug_fops);
	}

	return hisi_zip_core_debug_init(ctrl);
}

static int hisi_zip_debugfs_init(struct hisi_zip *hisi_zip)
{
	struct hisi_qm *qm = &hisi_zip->qm;
	struct device *dev = &qm->pdev->dev;
	struct dentry *dev_d;
	int ret;

	dev_d = debugfs_create_dir(dev_name(dev), hzip_debugfs_root);

	qm->debug.debug_root = dev_d;
	ret = hisi_qm_debug_init(qm);
	if (ret)
		goto failed_to_create;

	if (qm->fun_type == QM_HW_PF) {
		hisi_zip->ctrl->debug_root = dev_d;
		ret = hisi_zip_ctrl_debug_init(hisi_zip->ctrl);
		if (ret)
			goto failed_to_create;
	}

	return 0;

failed_to_create:
	debugfs_remove_recursive(hzip_debugfs_root);
	return ret;
}

static void hisi_zip_debug_regs_clear(struct hisi_zip *hisi_zip)
{
	struct hisi_qm *qm = &hisi_zip->qm;

	writel(0x0, qm->io_base + QM_DFX_MB_CNT_VF);
	writel(0x0, qm->io_base + QM_DFX_DB_CNT_VF);
	writel(0x0, qm->io_base + HZIP_SOFT_CTRL_CNT_CLR_CE);

	hisi_qm_debug_regs_clear(qm);
}

static void hisi_zip_debugfs_exit(struct hisi_zip *hisi_zip)
{
	struct hisi_qm *qm = &hisi_zip->qm;

	debugfs_remove_recursive(qm->debug.debug_root);

	if (qm->fun_type == QM_HW_PF)
		hisi_zip_debug_regs_clear(hisi_zip);
}

static void hisi_zip_log_hw_error(struct hisi_qm *qm, u32 err_sts)
{
	const struct hisi_zip_hw_error *err = zip_hw_error;
	struct device *dev = &qm->pdev->dev;
	u32 err_val;

	while (err->msg) {
		if (err->int_msk & err_sts) {
			dev_err(dev, "%s [error status=0x%x] found\n",
				 err->msg, err->int_msk);

			if (err->int_msk & HZIP_CORE_INT_STATUS_M_ECC) {
				err_val = readl(qm->io_base +
						HZIP_CORE_SRAM_ECC_ERR_INFO);
				dev_err(dev, "hisi-zip multi ecc sram num=0x%x\n",
					((err_val >>
					HZIP_SRAM_ECC_ERR_NUM_SHIFT) & 0xFF));
				dev_err(dev, "hisi-zip multi ecc sram addr=0x%x\n",
					(err_val >>
					HZIP_SRAM_ECC_ERR_ADDR_SHIFT));
			}
		}
		err++;
	}

	writel(err_sts, qm->io_base + HZIP_CORE_INT_SOURCE);
}

static u32 hisi_zip_get_hw_err_status(struct hisi_qm *qm)
{
	return readl(qm->io_base + HZIP_CORE_INT_STATUS);
}

static const struct hisi_qm_err_ini hisi_zip_err_ini = {
	.hw_err_enable		= hisi_zip_hw_error_enable,
	.hw_err_disable		= hisi_zip_hw_error_disable,
	.get_dev_hw_err_status	= hisi_zip_get_hw_err_status,
	.log_dev_hw_err		= hisi_zip_log_hw_error,
	.err_info		= {
		.ce			= QM_BASE_CE,
		.nfe			= QM_BASE_NFE |
					  QM_ACC_WB_NOT_READY_TIMEOUT,
		.fe			= 0,
		.msi			= QM_DB_RANDOM_INVALID,
	}
};

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

	qm->err_ini = &hisi_zip_err_ini;

	hisi_zip_set_user_domain_and_cache(hisi_zip);
	hisi_qm_dev_err_init(qm);
	hisi_zip_debug_regs_clear(hisi_zip);

	return 0;
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
	qm->use_dma_api = true;
	qm->pdev = pdev;
	qm->ver = rev_id;

	qm->algs = "zlib\ngzip";
	qm->sqe_size = HZIP_SQE_SIZE;
	qm->dev_name = hisi_zip_name;
	qm->fun_type = (pdev->device == PCI_DEVICE_ID_ZIP_PF) ? QM_HW_PF :
								QM_HW_VF;
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

	ret = hisi_zip_debugfs_init(hisi_zip);
	if (ret)
		dev_err(&pdev->dev, "Failed to init debugfs (%d)!\n", ret);

	hisi_qm_add_to_list(qm, &zip_devices);

	if (qm->uacce) {
		ret = uacce_register(qm->uacce);
		if (ret)
			goto err_qm_uninit;
	}

	if (qm->fun_type == QM_HW_PF && vfs_num > 0) {
		ret = hisi_zip_sriov_enable(pdev, vfs_num);
		if (ret < 0)
			goto err_remove_from_list;
	}

	return 0;

err_remove_from_list:
	hisi_qm_del_from_list(qm, &zip_devices);
	hisi_zip_debugfs_exit(hisi_zip);
	hisi_qm_stop(qm);
err_qm_uninit:
	hisi_qm_uninit(qm);
	return ret;
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

	hisi_zip_debugfs_exit(hisi_zip);
	hisi_qm_stop(qm);

	hisi_qm_dev_err_uninit(qm);
	hisi_qm_uninit(qm);
	hisi_qm_del_from_list(qm, &zip_devices);
}

static const struct pci_error_handlers hisi_zip_err_handler = {
	.error_detected	= hisi_qm_dev_err_detected,
};

static struct pci_driver hisi_zip_pci_driver = {
	.name			= "hisi_zip",
	.id_table		= hisi_zip_dev_ids,
	.probe			= hisi_zip_probe,
	.remove			= hisi_zip_remove,
	.sriov_configure	= IS_ENABLED(CONFIG_PCI_IOV) ?
					hisi_zip_sriov_configure : NULL,
	.err_handler		= &hisi_zip_err_handler,
};

static void hisi_zip_register_debugfs(void)
{
	if (!debugfs_initialized())
		return;

	hzip_debugfs_root = debugfs_create_dir("hisi_zip", NULL);
}

static void hisi_zip_unregister_debugfs(void)
{
	debugfs_remove_recursive(hzip_debugfs_root);
}

static int __init hisi_zip_init(void)
{
	int ret;

	hisi_qm_init_list(&zip_devices);
	hisi_zip_register_debugfs();

	ret = pci_register_driver(&hisi_zip_pci_driver);
	if (ret < 0) {
		pr_err("Failed to register pci driver.\n");
		goto err_pci;
	}

	ret = hisi_zip_register_to_crypto();
	if (ret < 0) {
		pr_err("Failed to register driver to crypto.\n");
		goto err_crypto;
	}

	return 0;

err_crypto:
	pci_unregister_driver(&hisi_zip_pci_driver);
err_pci:
	hisi_zip_unregister_debugfs();

	return ret;
}

static void __exit hisi_zip_exit(void)
{
	hisi_zip_unregister_from_crypto();
	pci_unregister_driver(&hisi_zip_pci_driver);
	hisi_zip_unregister_debugfs();
}

module_init(hisi_zip_init);
module_exit(hisi_zip_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Zhou Wang <wangzhou1@hisilicon.com>");
MODULE_DESCRIPTION("Driver for HiSilicon ZIP accelerator");
