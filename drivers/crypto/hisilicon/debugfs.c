// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 HiSilicon Limited. */
#include <linux/hisi_acc_qm.h>
#include "qm_common.h"

#define QM_DFX_BASE			0x0100000
#define QM_DFX_STATE1			0x0104000
#define QM_DFX_STATE2			0x01040C8
#define QM_DFX_COMMON			0x0000
#define QM_DFX_BASE_LEN			0x5A
#define QM_DFX_STATE1_LEN		0x2E
#define QM_DFX_STATE2_LEN		0x11
#define QM_DFX_COMMON_LEN		0xC3
#define QM_DFX_REGS_LEN			4UL
#define QM_DBG_TMP_BUF_LEN		22
#define QM_XQC_ADDR_MASK		GENMASK(31, 0)
#define CURRENT_FUN_MASK		GENMASK(5, 0)
#define CURRENT_Q_MASK			GENMASK(31, 16)
#define QM_SQE_ADDR_MASK		GENMASK(7, 0)

#define QM_DFX_MB_CNT_VF		0x104010
#define QM_DFX_DB_CNT_VF		0x104020
#define QM_DFX_SQE_CNT_VF_SQN		0x104030
#define QM_DFX_CQE_CNT_VF_CQN		0x104040
#define QM_DFX_QN_SHIFT			16
#define QM_DFX_CNT_CLR_CE		0x100118
#define QM_DBG_WRITE_LEN		1024
#define QM_IN_IDLE_ST_REG		0x1040e4
#define QM_IN_IDLE_STATE		0x1

static const char * const qm_debug_file_name[] = {
	[CURRENT_QM]   = "current_qm",
	[CURRENT_Q]    = "current_q",
	[CLEAR_ENABLE] = "clear_enable",
};

static const char * const qm_s[] = {
	"work", "stop",
};

struct qm_dfx_item {
	const char *name;
	u32 offset;
};

struct qm_cmd_dump_item {
	const char *cmd;
	char *info_name;
	int (*dump_fn)(struct hisi_qm *qm, char *cmd, char *info_name);
};

static struct qm_dfx_item qm_dfx_files[] = {
	{"err_irq", offsetof(struct qm_dfx, err_irq_cnt)},
	{"aeq_irq", offsetof(struct qm_dfx, aeq_irq_cnt)},
	{"abnormal_irq", offsetof(struct qm_dfx, abnormal_irq_cnt)},
	{"create_qp_err", offsetof(struct qm_dfx, create_qp_err_cnt)},
	{"mb_err", offsetof(struct qm_dfx, mb_err_cnt)},
};

#define CNT_CYC_REGS_NUM		10
static const struct debugfs_reg32 qm_dfx_regs[] = {
	/* XXX_CNT are reading clear register */
	{"QM_ECC_1BIT_CNT               ",  0x104000},
	{"QM_ECC_MBIT_CNT               ",  0x104008},
	{"QM_DFX_MB_CNT                 ",  0x104018},
	{"QM_DFX_DB_CNT                 ",  0x104028},
	{"QM_DFX_SQE_CNT                ",  0x104038},
	{"QM_DFX_CQE_CNT                ",  0x104048},
	{"QM_DFX_SEND_SQE_TO_ACC_CNT    ",  0x104050},
	{"QM_DFX_WB_SQE_FROM_ACC_CNT    ",  0x104058},
	{"QM_DFX_ACC_FINISH_CNT         ",  0x104060},
	{"QM_DFX_CQE_ERR_CNT            ",  0x1040b4},
	{"QM_DFX_FUNS_ACTIVE_ST         ",  0x200},
	{"QM_ECC_1BIT_INF               ",  0x104004},
	{"QM_ECC_MBIT_INF               ",  0x10400c},
	{"QM_DFX_ACC_RDY_VLD0           ",  0x1040a0},
	{"QM_DFX_ACC_RDY_VLD1           ",  0x1040a4},
	{"QM_DFX_AXI_RDY_VLD            ",  0x1040a8},
	{"QM_DFX_FF_ST0                 ",  0x1040c8},
	{"QM_DFX_FF_ST1                 ",  0x1040cc},
	{"QM_DFX_FF_ST2                 ",  0x1040d0},
	{"QM_DFX_FF_ST3                 ",  0x1040d4},
	{"QM_DFX_FF_ST4                 ",  0x1040d8},
	{"QM_DFX_FF_ST5                 ",  0x1040dc},
	{"QM_DFX_FF_ST6                 ",  0x1040e0},
	{"QM_IN_IDLE_ST                 ",  0x1040e4},
	{"QM_CACHE_CTL                  ",  0x100050},
	{"QM_TIMEOUT_CFG                ",  0x100070},
	{"QM_DB_TIMEOUT_CFG             ",  0x100074},
	{"QM_FLR_PENDING_TIME_CFG       ",  0x100078},
	{"QM_ARUSR_MCFG1                ",  0x100088},
	{"QM_AWUSR_MCFG1                ",  0x100098},
	{"QM_AXI_M_CFG_ENABLE           ",  0x1000B0},
	{"QM_RAS_CE_THRESHOLD           ",  0x1000F8},
	{"QM_AXI_TIMEOUT_CTRL           ",  0x100120},
	{"QM_AXI_TIMEOUT_STATUS         ",  0x100124},
	{"QM_CQE_AGGR_TIMEOUT_CTRL      ",  0x100144},
	{"ACC_RAS_MSI_INT_SEL           ",  0x1040fc},
	{"QM_CQE_OUT                    ",  0x104100},
	{"QM_EQE_OUT                    ",  0x104104},
	{"QM_AEQE_OUT                   ",  0x104108},
	{"QM_DB_INFO0                   ",  0x104180},
	{"QM_DB_INFO1                   ",  0x104184},
	{"QM_AM_CTRL_GLOBAL             ",  0x300000},
	{"QM_AM_CURR_PORT_STS           ",  0x300100},
	{"QM_AM_CURR_TRANS_RETURN       ",  0x300150},
	{"QM_AM_CURR_RD_MAX_TXID        ",  0x300154},
	{"QM_AM_CURR_WR_MAX_TXID        ",  0x300158},
	{"QM_AM_ALARM_RRESP             ",  0x300180},
	{"QM_AM_ALARM_BRESP             ",  0x300184},
};

static const struct debugfs_reg32 qm_vf_dfx_regs[] = {
	{"QM_DFX_FUNS_ACTIVE_ST         ",  0x200},
};

/* define the QM's dfx regs region and region length */
static struct dfx_diff_registers qm_diff_regs[] = {
	{
		.reg_offset = QM_DFX_BASE,
		.reg_len = QM_DFX_BASE_LEN,
	}, {
		.reg_offset = QM_DFX_STATE1,
		.reg_len = QM_DFX_STATE1_LEN,
	}, {
		.reg_offset = QM_DFX_STATE2,
		.reg_len = QM_DFX_STATE2_LEN,
	}, {
		.reg_offset = QM_DFX_COMMON,
		.reg_len = QM_DFX_COMMON_LEN,
	},
};

static struct hisi_qm *file_to_qm(struct debugfs_file *file)
{
	struct qm_debug *debug = file->debug;

	return container_of(debug, struct hisi_qm, debug);
}

static ssize_t qm_cmd_read(struct file *filp, char __user *buffer,
			   size_t count, loff_t *pos)
{
	char buf[QM_DBG_READ_LEN];
	int len;

	len = scnprintf(buf, QM_DBG_READ_LEN, "%s\n",
			"Please echo help to cmd to get help information");

	return simple_read_from_buffer(buffer, count, pos, buf, len);
}

static void dump_show(struct hisi_qm *qm, void *info,
		     unsigned int info_size, char *info_name)
{
	struct device *dev = &qm->pdev->dev;
	u8 *info_curr = info;
	u32 i;
#define BYTE_PER_DW	4

	dev_info(dev, "%s DUMP\n", info_name);
	for (i = 0; i < info_size; i += BYTE_PER_DW, info_curr += BYTE_PER_DW) {
		pr_info("DW%u: %02X%02X %02X%02X\n", i / BYTE_PER_DW,
			*(info_curr + 3), *(info_curr + 2), *(info_curr + 1), *(info_curr));
	}
}

static int qm_sqc_dump(struct hisi_qm *qm, char *s, char *name)
{
	struct device *dev = &qm->pdev->dev;
	struct qm_sqc sqc;
	u32 qp_id;
	int ret;

	if (!s)
		return -EINVAL;

	ret = kstrtou32(s, 0, &qp_id);
	if (ret || qp_id >= qm->qp_num) {
		dev_err(dev, "Please input qp num (0-%u)", qm->qp_num - 1);
		return -EINVAL;
	}

	ret = qm_set_and_get_xqc(qm, QM_MB_CMD_SQC, &sqc, qp_id, 1);
	if (!ret) {
		sqc.base_h = cpu_to_le32(QM_XQC_ADDR_MASK);
		sqc.base_l = cpu_to_le32(QM_XQC_ADDR_MASK);
		dump_show(qm, &sqc, sizeof(struct qm_sqc), name);

		return 0;
	}

	down_read(&qm->qps_lock);
	if (qm->sqc) {
		memcpy(&sqc, qm->sqc + qp_id, sizeof(struct qm_sqc));
		sqc.base_h = cpu_to_le32(QM_XQC_ADDR_MASK);
		sqc.base_l = cpu_to_le32(QM_XQC_ADDR_MASK);
		dump_show(qm, &sqc, sizeof(struct qm_sqc), "SOFT SQC");
	}
	up_read(&qm->qps_lock);

	return 0;
}

static int qm_cqc_dump(struct hisi_qm *qm, char *s, char *name)
{
	struct device *dev = &qm->pdev->dev;
	struct qm_cqc cqc;
	u32 qp_id;
	int ret;

	if (!s)
		return -EINVAL;

	ret = kstrtou32(s, 0, &qp_id);
	if (ret || qp_id >= qm->qp_num) {
		dev_err(dev, "Please input qp num (0-%u)", qm->qp_num - 1);
		return -EINVAL;
	}

	ret = qm_set_and_get_xqc(qm, QM_MB_CMD_CQC, &cqc, qp_id, 1);
	if (!ret) {
		cqc.base_h = cpu_to_le32(QM_XQC_ADDR_MASK);
		cqc.base_l = cpu_to_le32(QM_XQC_ADDR_MASK);
		dump_show(qm, &cqc, sizeof(struct qm_cqc), name);

		return 0;
	}

	down_read(&qm->qps_lock);
	if (qm->cqc) {
		memcpy(&cqc, qm->cqc + qp_id, sizeof(struct qm_cqc));
		cqc.base_h = cpu_to_le32(QM_XQC_ADDR_MASK);
		cqc.base_l = cpu_to_le32(QM_XQC_ADDR_MASK);
		dump_show(qm, &cqc, sizeof(struct qm_cqc), "SOFT CQC");
	}
	up_read(&qm->qps_lock);

	return 0;
}

static int qm_eqc_aeqc_dump(struct hisi_qm *qm, char *s, char *name)
{
	struct device *dev = &qm->pdev->dev;
	struct qm_aeqc aeqc;
	struct qm_eqc eqc;
	size_t size;
	void *xeqc;
	int ret;
	u8 cmd;

	if (strsep(&s, " ")) {
		dev_err(dev, "Please do not input extra characters!\n");
		return -EINVAL;
	}

	if (!strcmp(name, "EQC")) {
		cmd = QM_MB_CMD_EQC;
		size = sizeof(struct qm_eqc);
		xeqc = &eqc;
	} else {
		cmd = QM_MB_CMD_AEQC;
		size = sizeof(struct qm_aeqc);
		xeqc = &aeqc;
	}

	ret = qm_set_and_get_xqc(qm, cmd, xeqc, 0, 1);
	if (ret)
		return ret;

	aeqc.base_h = cpu_to_le32(QM_XQC_ADDR_MASK);
	aeqc.base_l = cpu_to_le32(QM_XQC_ADDR_MASK);
	eqc.base_h = cpu_to_le32(QM_XQC_ADDR_MASK);
	eqc.base_l = cpu_to_le32(QM_XQC_ADDR_MASK);
	dump_show(qm, xeqc, size, name);

	return ret;
}

static int q_dump_param_parse(struct hisi_qm *qm, char *s,
			      u32 *e_id, u32 *q_id, u16 q_depth)
{
	struct device *dev = &qm->pdev->dev;
	unsigned int qp_num = qm->qp_num;
	char *presult;
	int ret;

	presult = strsep(&s, " ");
	if (!presult) {
		dev_err(dev, "Please input qp number!\n");
		return -EINVAL;
	}

	ret = kstrtou32(presult, 0, q_id);
	if (ret || *q_id >= qp_num) {
		dev_err(dev, "Please input qp num (0-%u)", qp_num - 1);
		return -EINVAL;
	}

	presult = strsep(&s, " ");
	if (!presult) {
		dev_err(dev, "Please input sqe number!\n");
		return -EINVAL;
	}

	ret = kstrtou32(presult, 0, e_id);
	if (ret || *e_id >= q_depth) {
		dev_err(dev, "Please input sqe num (0-%u)", q_depth - 1);
		return -EINVAL;
	}

	if (strsep(&s, " ")) {
		dev_err(dev, "Please do not input extra characters!\n");
		return -EINVAL;
	}

	return 0;
}

static int qm_sq_dump(struct hisi_qm *qm, char *s, char *name)
{
	u16 sq_depth = qm->qp_array->sq_depth;
	struct hisi_qp *qp;
	u32 qp_id, sqe_id;
	void *sqe;
	int ret;

	ret = q_dump_param_parse(qm, s, &sqe_id, &qp_id, sq_depth);
	if (ret)
		return ret;

	sqe = kzalloc(qm->sqe_size, GFP_KERNEL);
	if (!sqe)
		return -ENOMEM;

	qp = &qm->qp_array[qp_id];
	memcpy(sqe, qp->sqe + sqe_id * qm->sqe_size, qm->sqe_size);
	memset(sqe + qm->debug.sqe_mask_offset, QM_SQE_ADDR_MASK,
	       qm->debug.sqe_mask_len);

	dump_show(qm, sqe, qm->sqe_size, name);

	kfree(sqe);

	return 0;
}

static int qm_cq_dump(struct hisi_qm *qm, char *s, char *name)
{
	struct qm_cqe *cqe_curr;
	struct hisi_qp *qp;
	u32 qp_id, cqe_id;
	int ret;

	ret = q_dump_param_parse(qm, s, &cqe_id, &qp_id, qm->qp_array->cq_depth);
	if (ret)
		return ret;

	qp = &qm->qp_array[qp_id];
	cqe_curr = qp->cqe + cqe_id;
	dump_show(qm, cqe_curr, sizeof(struct qm_cqe), name);

	return 0;
}

static int qm_eq_aeq_dump(struct hisi_qm *qm, char *s, char *name)
{
	struct device *dev = &qm->pdev->dev;
	u16 xeq_depth;
	size_t size;
	void *xeqe;
	u32 xeqe_id;
	int ret;

	if (!s)
		return -EINVAL;

	ret = kstrtou32(s, 0, &xeqe_id);
	if (ret)
		return -EINVAL;

	if (!strcmp(name, "EQE")) {
		xeq_depth = qm->eq_depth;
		size = sizeof(struct qm_eqe);
	} else {
		xeq_depth = qm->aeq_depth;
		size = sizeof(struct qm_aeqe);
	}

	if (xeqe_id >= xeq_depth) {
		dev_err(dev, "Please input eqe or aeqe num (0-%u)", xeq_depth - 1);
		return -EINVAL;
	}

	down_read(&qm->qps_lock);

	if (qm->eqe && !strcmp(name, "EQE")) {
		xeqe = qm->eqe + xeqe_id;
	} else if (qm->aeqe && !strcmp(name, "AEQE")) {
		xeqe = qm->aeqe + xeqe_id;
	} else {
		ret = -EINVAL;
		goto err_unlock;
	}

	dump_show(qm, xeqe, size, name);

err_unlock:
	up_read(&qm->qps_lock);
	return ret;
}

static int qm_dbg_help(struct hisi_qm *qm, char *s)
{
	struct device *dev = &qm->pdev->dev;

	if (strsep(&s, " ")) {
		dev_err(dev, "Please do not input extra characters!\n");
		return -EINVAL;
	}

	dev_info(dev, "available commands:\n");
	dev_info(dev, "sqc <num>\n");
	dev_info(dev, "cqc <num>\n");
	dev_info(dev, "eqc\n");
	dev_info(dev, "aeqc\n");
	dev_info(dev, "sq <num> <e>\n");
	dev_info(dev, "cq <num> <e>\n");
	dev_info(dev, "eq <e>\n");
	dev_info(dev, "aeq <e>\n");

	return 0;
}

static const struct qm_cmd_dump_item qm_cmd_dump_table[] = {
	{
		.cmd = "sqc",
		.info_name = "SQC",
		.dump_fn = qm_sqc_dump,
	}, {
		.cmd = "cqc",
		.info_name = "CQC",
		.dump_fn = qm_cqc_dump,
	}, {
		.cmd = "eqc",
		.info_name = "EQC",
		.dump_fn = qm_eqc_aeqc_dump,
	}, {
		.cmd = "aeqc",
		.info_name = "AEQC",
		.dump_fn = qm_eqc_aeqc_dump,
	}, {
		.cmd = "sq",
		.info_name = "SQE",
		.dump_fn = qm_sq_dump,
	}, {
		.cmd = "cq",
		.info_name = "CQE",
		.dump_fn = qm_cq_dump,
	}, {
		.cmd = "eq",
		.info_name = "EQE",
		.dump_fn = qm_eq_aeq_dump,
	}, {
		.cmd = "aeq",
		.info_name = "AEQE",
		.dump_fn = qm_eq_aeq_dump,
	},
};

static int qm_cmd_write_dump(struct hisi_qm *qm, const char *cmd_buf)
{
	struct device *dev = &qm->pdev->dev;
	char *presult, *s, *s_tmp;
	int table_size, i, ret;

	s = kstrdup(cmd_buf, GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	s_tmp = s;
	presult = strsep(&s, " ");
	if (!presult) {
		ret = -EINVAL;
		goto err_buffer_free;
	}

	if (!strcmp(presult, "help")) {
		ret = qm_dbg_help(qm, s);
		goto err_buffer_free;
	}

	table_size = ARRAY_SIZE(qm_cmd_dump_table);
	for (i = 0; i < table_size; i++) {
		if (!strcmp(presult, qm_cmd_dump_table[i].cmd)) {
			ret = qm_cmd_dump_table[i].dump_fn(qm, s,
				qm_cmd_dump_table[i].info_name);
			break;
		}
	}

	if (i == table_size) {
		dev_info(dev, "Please echo help\n");
		ret = -EINVAL;
	}

err_buffer_free:
	kfree(s_tmp);

	return ret;
}

static ssize_t qm_cmd_write(struct file *filp, const char __user *buffer,
			    size_t count, loff_t *pos)
{
	struct hisi_qm *qm = filp->private_data;
	char *cmd_buf, *cmd_buf_tmp;
	int ret;

	if (*pos)
		return 0;

	ret = hisi_qm_get_dfx_access(qm);
	if (ret)
		return ret;

	/* Judge if the instance is being reset. */
	if (unlikely(atomic_read(&qm->status.flags) == QM_STOP)) {
		ret = 0;
		goto put_dfx_access;
	}

	if (count > QM_DBG_WRITE_LEN) {
		ret = -ENOSPC;
		goto put_dfx_access;
	}

	cmd_buf = memdup_user_nul(buffer, count);
	if (IS_ERR(cmd_buf)) {
		ret = PTR_ERR(cmd_buf);
		goto put_dfx_access;
	}

	cmd_buf_tmp = strchr(cmd_buf, '\n');
	if (cmd_buf_tmp) {
		*cmd_buf_tmp = '\0';
		count = cmd_buf_tmp - cmd_buf + 1;
	}

	ret = qm_cmd_write_dump(qm, cmd_buf);
	if (ret) {
		kfree(cmd_buf);
		goto put_dfx_access;
	}

	kfree(cmd_buf);

	ret = count;

put_dfx_access:
	hisi_qm_put_dfx_access(qm);
	return ret;
}

static const struct file_operations qm_cmd_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = qm_cmd_read,
	.write = qm_cmd_write,
};

/**
 * hisi_qm_regs_dump() - Dump registers's value.
 * @s: debugfs file handle.
 * @regset: accelerator registers information.
 *
 * Dump accelerator registers.
 */
void hisi_qm_regs_dump(struct seq_file *s, struct debugfs_regset32 *regset)
{
	struct pci_dev *pdev = to_pci_dev(regset->dev);
	struct hisi_qm *qm = pci_get_drvdata(pdev);
	const struct debugfs_reg32 *regs = regset->regs;
	int regs_len = regset->nregs;
	int i, ret;
	u32 val;

	ret = hisi_qm_get_dfx_access(qm);
	if (ret)
		return;

	for (i = 0; i < regs_len; i++) {
		val = readl(regset->base + regs[i].offset);
		seq_printf(s, "%s= 0x%08x\n", regs[i].name, val);
	}

	hisi_qm_put_dfx_access(qm);
}
EXPORT_SYMBOL_GPL(hisi_qm_regs_dump);

static int qm_regs_show(struct seq_file *s, void *unused)
{
	struct hisi_qm *qm = s->private;
	struct debugfs_regset32 regset;

	if (qm->fun_type == QM_HW_PF) {
		regset.regs = qm_dfx_regs;
		regset.nregs = ARRAY_SIZE(qm_dfx_regs);
	} else {
		regset.regs = qm_vf_dfx_regs;
		regset.nregs = ARRAY_SIZE(qm_vf_dfx_regs);
	}

	regset.base = qm->io_base;
	regset.dev = &qm->pdev->dev;

	hisi_qm_regs_dump(s, &regset);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(qm_regs);

static u32 current_q_read(struct hisi_qm *qm)
{
	return readl(qm->io_base + QM_DFX_SQE_CNT_VF_SQN) >> QM_DFX_QN_SHIFT;
}

static int current_q_write(struct hisi_qm *qm, u32 val)
{
	u32 tmp;

	if (val >= qm->debug.curr_qm_qp_num)
		return -EINVAL;

	tmp = val << QM_DFX_QN_SHIFT |
	      (readl(qm->io_base + QM_DFX_SQE_CNT_VF_SQN) & CURRENT_FUN_MASK);
	writel(tmp, qm->io_base + QM_DFX_SQE_CNT_VF_SQN);

	tmp = val << QM_DFX_QN_SHIFT |
	      (readl(qm->io_base + QM_DFX_CQE_CNT_VF_CQN) & CURRENT_FUN_MASK);
	writel(tmp, qm->io_base + QM_DFX_CQE_CNT_VF_CQN);

	return 0;
}

static u32 clear_enable_read(struct hisi_qm *qm)
{
	return readl(qm->io_base + QM_DFX_CNT_CLR_CE);
}

/* rd_clr_ctrl 1 enable read clear, otherwise 0 disable it */
static int clear_enable_write(struct hisi_qm *qm, u32 rd_clr_ctrl)
{
	if (rd_clr_ctrl > 1)
		return -EINVAL;

	writel(rd_clr_ctrl, qm->io_base + QM_DFX_CNT_CLR_CE);

	return 0;
}

static u32 current_qm_read(struct hisi_qm *qm)
{
	return readl(qm->io_base + QM_DFX_MB_CNT_VF);
}

static int qm_get_vf_qp_num(struct hisi_qm *qm, u32 fun_num)
{
	u32 remain_q_num, vfq_num;
	u32 num_vfs = qm->vfs_num;

	vfq_num = (qm->ctrl_qp_num - qm->qp_num) / num_vfs;
	if (vfq_num >= qm->max_qp_num)
		return qm->max_qp_num;

	remain_q_num = (qm->ctrl_qp_num - qm->qp_num) % num_vfs;
	if (vfq_num + remain_q_num <= qm->max_qp_num)
		return fun_num == num_vfs ? vfq_num + remain_q_num : vfq_num;

	/*
	 * if vfq_num + remain_q_num > max_qp_num, the last VFs,
	 * each with one more queue.
	 */
	return fun_num + remain_q_num > num_vfs ? vfq_num + 1 : vfq_num;
}

static int current_qm_write(struct hisi_qm *qm, u32 val)
{
	u32 tmp;

	if (val > qm->vfs_num)
		return -EINVAL;

	/* According PF or VF Dev ID to calculation curr_qm_qp_num and store */
	if (!val)
		qm->debug.curr_qm_qp_num = qm->qp_num;
	else
		qm->debug.curr_qm_qp_num = qm_get_vf_qp_num(qm, val);

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

static ssize_t qm_debug_read(struct file *filp, char __user *buf,
			     size_t count, loff_t *pos)
{
	struct debugfs_file *file = filp->private_data;
	enum qm_debug_file index = file->index;
	struct hisi_qm *qm = file_to_qm(file);
	char tbuf[QM_DBG_TMP_BUF_LEN];
	u32 val;
	int ret;

	ret = hisi_qm_get_dfx_access(qm);
	if (ret)
		return ret;

	mutex_lock(&file->lock);
	switch (index) {
	case CURRENT_QM:
		val = current_qm_read(qm);
		break;
	case CURRENT_Q:
		val = current_q_read(qm);
		break;
	case CLEAR_ENABLE:
		val = clear_enable_read(qm);
		break;
	default:
		goto err_input;
	}
	mutex_unlock(&file->lock);

	hisi_qm_put_dfx_access(qm);
	ret = scnprintf(tbuf, QM_DBG_TMP_BUF_LEN, "%u\n", val);
	return simple_read_from_buffer(buf, count, pos, tbuf, ret);

err_input:
	mutex_unlock(&file->lock);
	hisi_qm_put_dfx_access(qm);
	return -EINVAL;
}

static ssize_t qm_debug_write(struct file *filp, const char __user *buf,
			      size_t count, loff_t *pos)
{
	struct debugfs_file *file = filp->private_data;
	enum qm_debug_file index = file->index;
	struct hisi_qm *qm = file_to_qm(file);
	unsigned long val;
	char tbuf[QM_DBG_TMP_BUF_LEN];
	int len, ret;

	if (*pos != 0)
		return 0;

	if (count >= QM_DBG_TMP_BUF_LEN)
		return -ENOSPC;

	len = simple_write_to_buffer(tbuf, QM_DBG_TMP_BUF_LEN - 1, pos, buf,
				     count);
	if (len < 0)
		return len;

	tbuf[len] = '\0';
	if (kstrtoul(tbuf, 0, &val))
		return -EFAULT;

	ret = hisi_qm_get_dfx_access(qm);
	if (ret)
		return ret;

	mutex_lock(&file->lock);
	switch (index) {
	case CURRENT_QM:
		ret = current_qm_write(qm, val);
		break;
	case CURRENT_Q:
		ret = current_q_write(qm, val);
		break;
	case CLEAR_ENABLE:
		ret = clear_enable_write(qm, val);
		break;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&file->lock);

	hisi_qm_put_dfx_access(qm);

	if (ret)
		return ret;

	return count;
}

static const struct file_operations qm_debug_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = qm_debug_read,
	.write = qm_debug_write,
};

static void dfx_regs_uninit(struct hisi_qm *qm,
		struct dfx_diff_registers *dregs, int reg_len)
{
	int i;

	if (!dregs)
		return;

	/* Setting the pointer is NULL to prevent double free */
	for (i = 0; i < reg_len; i++) {
		if (!dregs[i].regs)
			continue;

		kfree(dregs[i].regs);
		dregs[i].regs = NULL;
	}
	kfree(dregs);
}

static struct dfx_diff_registers *dfx_regs_init(struct hisi_qm *qm,
	const struct dfx_diff_registers *cregs, u32 reg_len)
{
	struct dfx_diff_registers *diff_regs;
	u32 j, base_offset;
	int i;

	diff_regs = kcalloc(reg_len, sizeof(*diff_regs), GFP_KERNEL);
	if (!diff_regs)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < reg_len; i++) {
		if (!cregs[i].reg_len)
			continue;

		diff_regs[i].reg_offset = cregs[i].reg_offset;
		diff_regs[i].reg_len = cregs[i].reg_len;
		diff_regs[i].regs = kcalloc(QM_DFX_REGS_LEN, cregs[i].reg_len,
					 GFP_KERNEL);
		if (!diff_regs[i].regs)
			goto alloc_error;

		for (j = 0; j < diff_regs[i].reg_len; j++) {
			base_offset = diff_regs[i].reg_offset +
					j * QM_DFX_REGS_LEN;
			diff_regs[i].regs[j] = readl(qm->io_base + base_offset);
		}
	}

	return diff_regs;

alloc_error:
	while (i > 0) {
		i--;
		kfree(diff_regs[i].regs);
	}
	kfree(diff_regs);
	return ERR_PTR(-ENOMEM);
}

static int qm_diff_regs_init(struct hisi_qm *qm,
		struct dfx_diff_registers *dregs, u32 reg_len)
{
	int ret;

	qm->debug.qm_diff_regs = dfx_regs_init(qm, qm_diff_regs, ARRAY_SIZE(qm_diff_regs));
	if (IS_ERR(qm->debug.qm_diff_regs)) {
		ret = PTR_ERR(qm->debug.qm_diff_regs);
		qm->debug.qm_diff_regs = NULL;
		return ret;
	}

	qm->debug.acc_diff_regs = dfx_regs_init(qm, dregs, reg_len);
	if (IS_ERR(qm->debug.acc_diff_regs)) {
		dfx_regs_uninit(qm, qm->debug.qm_diff_regs, ARRAY_SIZE(qm_diff_regs));
		ret = PTR_ERR(qm->debug.acc_diff_regs);
		qm->debug.acc_diff_regs = NULL;
		return ret;
	}

	return 0;
}

static void qm_last_regs_uninit(struct hisi_qm *qm)
{
	struct qm_debug *debug = &qm->debug;

	if (qm->fun_type == QM_HW_VF || !debug->qm_last_words)
		return;

	kfree(debug->qm_last_words);
	debug->qm_last_words = NULL;
}

static int qm_last_regs_init(struct hisi_qm *qm)
{
	int dfx_regs_num = ARRAY_SIZE(qm_dfx_regs);
	struct qm_debug *debug = &qm->debug;
	int i;

	if (qm->fun_type == QM_HW_VF)
		return 0;

	debug->qm_last_words = kcalloc(dfx_regs_num, sizeof(unsigned int), GFP_KERNEL);
	if (!debug->qm_last_words)
		return -ENOMEM;

	for (i = 0; i < dfx_regs_num; i++) {
		debug->qm_last_words[i] = readl_relaxed(qm->io_base +
			qm_dfx_regs[i].offset);
	}

	return 0;
}

static void qm_diff_regs_uninit(struct hisi_qm *qm, u32 reg_len)
{
	dfx_regs_uninit(qm, qm->debug.acc_diff_regs, reg_len);
	qm->debug.acc_diff_regs = NULL;
	dfx_regs_uninit(qm, qm->debug.qm_diff_regs, ARRAY_SIZE(qm_diff_regs));
	qm->debug.qm_diff_regs = NULL;
}

/**
 * hisi_qm_regs_debugfs_init() - Allocate memory for registers.
 * @qm: device qm handle.
 * @dregs: diff registers handle.
 * @reg_len: diff registers region length.
 */
int hisi_qm_regs_debugfs_init(struct hisi_qm *qm,
		struct dfx_diff_registers *dregs, u32 reg_len)
{
	int ret;

	if (!qm || !dregs)
		return -EINVAL;

	if (qm->fun_type != QM_HW_PF)
		return 0;

	ret = qm_last_regs_init(qm);
	if (ret) {
		dev_info(&qm->pdev->dev, "failed to init qm words memory!\n");
		return ret;
	}

	ret = qm_diff_regs_init(qm, dregs, reg_len);
	if (ret) {
		qm_last_regs_uninit(qm);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(hisi_qm_regs_debugfs_init);

/**
 * hisi_qm_regs_debugfs_uninit() - Free memory for registers.
 * @qm: device qm handle.
 * @reg_len: diff registers region length.
 */
void hisi_qm_regs_debugfs_uninit(struct hisi_qm *qm, u32 reg_len)
{
	if (!qm || qm->fun_type != QM_HW_PF)
		return;

	qm_diff_regs_uninit(qm, reg_len);
	qm_last_regs_uninit(qm);
}
EXPORT_SYMBOL_GPL(hisi_qm_regs_debugfs_uninit);

/**
 * hisi_qm_acc_diff_regs_dump() - Dump registers's value.
 * @qm: device qm handle.
 * @s: Debugfs file handle.
 * @dregs: diff registers handle.
 * @regs_len: diff registers region length.
 */
void hisi_qm_acc_diff_regs_dump(struct hisi_qm *qm, struct seq_file *s,
	struct dfx_diff_registers *dregs, u32 regs_len)
{
	u32 j, val, base_offset;
	int i, ret;

	if (!qm || !s || !dregs)
		return;

	ret = hisi_qm_get_dfx_access(qm);
	if (ret)
		return;

	down_read(&qm->qps_lock);
	for (i = 0; i < regs_len; i++) {
		if (!dregs[i].reg_len)
			continue;

		for (j = 0; j < dregs[i].reg_len; j++) {
			base_offset = dregs[i].reg_offset + j * QM_DFX_REGS_LEN;
			val = readl(qm->io_base + base_offset);
			if (val != dregs[i].regs[j])
				seq_printf(s, "0x%08x = 0x%08x ---> 0x%08x\n",
					   base_offset, dregs[i].regs[j], val);
		}
	}
	up_read(&qm->qps_lock);

	hisi_qm_put_dfx_access(qm);
}
EXPORT_SYMBOL_GPL(hisi_qm_acc_diff_regs_dump);

void hisi_qm_show_last_dfx_regs(struct hisi_qm *qm)
{
	struct qm_debug *debug = &qm->debug;
	struct pci_dev *pdev = qm->pdev;
	u32 val;
	int i;

	if (qm->fun_type == QM_HW_VF || !debug->qm_last_words)
		return;

	for (i = 0; i < ARRAY_SIZE(qm_dfx_regs); i++) {
		val = readl_relaxed(qm->io_base + qm_dfx_regs[i].offset);
		if (debug->qm_last_words[i] != val)
			pci_info(pdev, "%s \t= 0x%08x => 0x%08x\n",
			qm_dfx_regs[i].name, debug->qm_last_words[i], val);
	}
}

static int qm_diff_regs_show(struct seq_file *s, void *unused)
{
	struct hisi_qm *qm = s->private;

	hisi_qm_acc_diff_regs_dump(qm, s, qm->debug.qm_diff_regs,
					ARRAY_SIZE(qm_diff_regs));

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(qm_diff_regs);

static int qm_state_show(struct seq_file *s, void *unused)
{
	struct hisi_qm *qm = s->private;
	u32 val;
	int ret;

	/* If device is in suspended, directly return the idle state. */
	ret = hisi_qm_get_dfx_access(qm);
	if (!ret) {
		val = readl(qm->io_base + QM_IN_IDLE_ST_REG);
		hisi_qm_put_dfx_access(qm);
	} else if (ret == -EAGAIN) {
		val = QM_IN_IDLE_STATE;
	} else {
		return ret;
	}

	seq_printf(s, "%u\n", val);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(qm_state);

static ssize_t qm_status_read(struct file *filp, char __user *buffer,
			      size_t count, loff_t *pos)
{
	struct hisi_qm *qm = filp->private_data;
	char buf[QM_DBG_READ_LEN];
	int val, len;

	val = atomic_read(&qm->status.flags);
	len = scnprintf(buf, QM_DBG_READ_LEN, "%s\n", qm_s[val]);

	return simple_read_from_buffer(buffer, count, pos, buf, len);
}

static const struct file_operations qm_status_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = qm_status_read,
};

static void qm_create_debugfs_file(struct hisi_qm *qm, struct dentry *dir,
				   enum qm_debug_file index)
{
	struct debugfs_file *file = qm->debug.files + index;

	file->index = index;
	mutex_init(&file->lock);
	file->debug = &qm->debug;

	debugfs_create_file(qm_debug_file_name[index], 0600, dir, file,
			    &qm_debug_fops);
}

static int qm_debugfs_atomic64_set(void *data, u64 val)
{
	if (val)
		return -EINVAL;

	atomic64_set((atomic64_t *)data, 0);

	return 0;
}

static int qm_debugfs_atomic64_get(void *data, u64 *val)
{
	*val = atomic64_read((atomic64_t *)data);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(qm_atomic64_ops, qm_debugfs_atomic64_get,
			 qm_debugfs_atomic64_set, "%llu\n");

/**
 * hisi_qm_debug_init() - Initialize qm related debugfs files.
 * @qm: The qm for which we want to add debugfs files.
 *
 * Create qm related debugfs files.
 */
void hisi_qm_debug_init(struct hisi_qm *qm)
{
	struct dfx_diff_registers *qm_regs = qm->debug.qm_diff_regs;
	struct qm_dev_dfx *dev_dfx = &qm->debug.dev_dfx;
	struct qm_dfx *dfx = &qm->debug.dfx;
	struct dentry *qm_d;
	void *data;
	int i;

	qm_d = debugfs_create_dir("qm", qm->debug.debug_root);
	qm->debug.qm_d = qm_d;

	/* only show this in PF */
	if (qm->fun_type == QM_HW_PF) {
		debugfs_create_file("qm_state", 0444, qm->debug.qm_d,
					qm, &qm_state_fops);

		qm_create_debugfs_file(qm, qm->debug.debug_root, CURRENT_QM);
		for (i = CURRENT_Q; i < DEBUG_FILE_NUM; i++)
			qm_create_debugfs_file(qm, qm->debug.qm_d, i);
	}

	if (qm_regs)
		debugfs_create_file("diff_regs", 0444, qm->debug.qm_d,
					qm, &qm_diff_regs_fops);

	debugfs_create_file("regs", 0444, qm->debug.qm_d, qm, &qm_regs_fops);

	debugfs_create_file("cmd", 0600, qm->debug.qm_d, qm, &qm_cmd_fops);

	debugfs_create_file("status", 0444, qm->debug.qm_d, qm,
			&qm_status_fops);

	debugfs_create_u32("dev_state", 0444, qm->debug.qm_d, &dev_dfx->dev_state);
	debugfs_create_u32("dev_timeout", 0644, qm->debug.qm_d, &dev_dfx->dev_timeout);

	for (i = 0; i < ARRAY_SIZE(qm_dfx_files); i++) {
		data = (atomic64_t *)((uintptr_t)dfx + qm_dfx_files[i].offset);
		debugfs_create_file(qm_dfx_files[i].name,
			0644,
			qm_d,
			data,
			&qm_atomic64_ops);
	}

	if (test_bit(QM_SUPPORT_FUNC_QOS, &qm->caps))
		hisi_qm_set_algqos_init(qm);
}
EXPORT_SYMBOL_GPL(hisi_qm_debug_init);

/**
 * hisi_qm_debug_regs_clear() - clear qm debug related registers.
 * @qm: The qm for which we want to clear its debug registers.
 */
void hisi_qm_debug_regs_clear(struct hisi_qm *qm)
{
	const struct debugfs_reg32 *regs;
	int i;

	/* clear current_qm */
	writel(0x0, qm->io_base + QM_DFX_MB_CNT_VF);
	writel(0x0, qm->io_base + QM_DFX_DB_CNT_VF);

	/* clear current_q */
	writel(0x0, qm->io_base + QM_DFX_SQE_CNT_VF_SQN);
	writel(0x0, qm->io_base + QM_DFX_CQE_CNT_VF_CQN);

	/*
	 * these registers are reading and clearing, so clear them after
	 * reading them.
	 */
	writel(0x1, qm->io_base + QM_DFX_CNT_CLR_CE);

	regs = qm_dfx_regs;
	for (i = 0; i < CNT_CYC_REGS_NUM; i++) {
		readl(qm->io_base + regs->offset);
		regs++;
	}

	/* clear clear_enable */
	writel(0x0, qm->io_base + QM_DFX_CNT_CLR_CE);
}
EXPORT_SYMBOL_GPL(hisi_qm_debug_regs_clear);
