// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 HiSilicon Limited. */
#include <linux/acpi.h>
#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/seq_file.h>
#include <linux/topology.h>
#include <linux/uacce.h>
#include "zip.h"

#define PCI_DEVICE_ID_HUAWEI_ZIP_PF	0xa250

#define HZIP_QUEUE_NUM_V1		4096

#define HZIP_CLOCK_GATE_CTRL		0x301004
#define HZIP_DECOMP_CHECK_ENABLE	BIT(16)
#define HZIP_FSM_MAX_CNT		0x301008

#define HZIP_PORT_ARCA_CHE_0		0x301040
#define HZIP_PORT_ARCA_CHE_1		0x301044
#define HZIP_PORT_AWCA_CHE_0		0x301060
#define HZIP_PORT_AWCA_CHE_1		0x301064
#define HZIP_CACHE_ALL_EN		0xffffffff

#define HZIP_BD_RUSER_32_63		0x301110
#define HZIP_SGL_RUSER_32_63		0x30111c
#define HZIP_DATA_RUSER_32_63		0x301128
#define HZIP_DATA_WUSER_32_63		0x301134
#define HZIP_BD_WUSER_32_63		0x301140

#define HZIP_QM_IDEL_STATUS		0x3040e4

#define HZIP_CORE_DFX_BASE		0x301000
#define HZIP_CLOCK_GATED_CONTL		0X301004
#define HZIP_CORE_DFX_COMP_0		0x302000
#define HZIP_CORE_DFX_COMP_1		0x303000
#define HZIP_CORE_DFX_DECOMP_0		0x304000
#define HZIP_CORE_DFX_DECOMP_1		0x305000
#define HZIP_CORE_DFX_DECOMP_2		0x306000
#define HZIP_CORE_DFX_DECOMP_3		0x307000
#define HZIP_CORE_DFX_DECOMP_4		0x308000
#define HZIP_CORE_DFX_DECOMP_5		0x309000
#define HZIP_CORE_REGS_BASE_LEN		0xB0
#define HZIP_CORE_REGS_DFX_LEN		0x28

#define HZIP_CORE_INT_SOURCE		0x3010A0
#define HZIP_CORE_INT_MASK_REG		0x3010A4
#define HZIP_CORE_INT_SET		0x3010A8
#define HZIP_CORE_INT_STATUS		0x3010AC
#define HZIP_CORE_INT_STATUS_M_ECC	BIT(1)
#define HZIP_CORE_SRAM_ECC_ERR_INFO	0x301148
#define HZIP_CORE_INT_RAS_CE_ENB	0x301160
#define HZIP_CORE_INT_RAS_NFE_ENB	0x301164
#define HZIP_CORE_INT_RAS_FE_ENB        0x301168
#define HZIP_CORE_INT_RAS_FE_ENB_MASK	0x0
#define HZIP_OOO_SHUTDOWN_SEL		0x30120C
#define HZIP_SRAM_ECC_ERR_NUM_SHIFT	16
#define HZIP_SRAM_ECC_ERR_ADDR_SHIFT	24
#define HZIP_CORE_INT_MASK_ALL		GENMASK(12, 0)
#define HZIP_SQE_SIZE			128
#define HZIP_PF_DEF_Q_NUM		64
#define HZIP_PF_DEF_Q_BASE		0
#define HZIP_CTX_Q_NUM_DEF		2

#define HZIP_SOFT_CTRL_CNT_CLR_CE	0x301000
#define HZIP_SOFT_CTRL_CNT_CLR_CE_BIT	BIT(0)
#define HZIP_SOFT_CTRL_ZIP_CONTROL	0x30100C
#define HZIP_AXI_SHUTDOWN_ENABLE	BIT(14)
#define HZIP_WR_PORT			BIT(11)

#define HZIP_DEV_ALG_MAX_LEN		256
#define HZIP_ALG_ZLIB_BIT		GENMASK(1, 0)
#define HZIP_ALG_GZIP_BIT		GENMASK(3, 2)
#define HZIP_ALG_DEFLATE_BIT		GENMASK(5, 4)
#define HZIP_ALG_LZ77_BIT		GENMASK(7, 6)

#define HZIP_BUF_SIZE			22
#define HZIP_SQE_MASK_OFFSET		64
#define HZIP_SQE_MASK_LEN		48

#define HZIP_CNT_CLR_CE_EN		BIT(0)
#define HZIP_RO_CNT_CLR_CE_EN		BIT(2)
#define HZIP_RD_CNT_CLR_CE_EN		(HZIP_CNT_CLR_CE_EN | \
					 HZIP_RO_CNT_CLR_CE_EN)

#define HZIP_PREFETCH_CFG		0x3011B0
#define HZIP_SVA_TRANS			0x3011C4
#define HZIP_PREFETCH_ENABLE		(~(BIT(26) | BIT(17) | BIT(0)))
#define HZIP_SVA_PREFETCH_DISABLE	BIT(26)
#define HZIP_SVA_DISABLE_READY		(BIT(26) | BIT(30))
#define HZIP_SHAPER_RATE_COMPRESS	750
#define HZIP_SHAPER_RATE_DECOMPRESS	140
#define HZIP_DELAY_1_US		1
#define HZIP_POLL_TIMEOUT_US	1000

/* clock gating */
#define HZIP_PEH_CFG_AUTO_GATE		0x3011A8
#define HZIP_PEH_CFG_AUTO_GATE_EN	BIT(0)
#define HZIP_CORE_GATED_EN		GENMASK(15, 8)
#define HZIP_CORE_GATED_OOO_EN		BIT(29)
#define HZIP_CLOCK_GATED_EN		(HZIP_CORE_GATED_EN | \
					 HZIP_CORE_GATED_OOO_EN)

static const char hisi_zip_name[] = "hisi_zip";
static struct dentry *hzip_debugfs_root;

struct hisi_zip_hw_error {
	u32 int_msk;
	const char *msg;
};

struct zip_dfx_item {
	const char *name;
	u32 offset;
};

struct zip_dev_alg {
	u32 alg_msk;
	const char *algs;
};

static const struct zip_dev_alg zip_dev_algs[] = { {
		.alg_msk = HZIP_ALG_ZLIB_BIT,
		.algs = "zlib\n",
	}, {
		.alg_msk = HZIP_ALG_GZIP_BIT,
		.algs = "gzip\n",
	}, {
		.alg_msk = HZIP_ALG_DEFLATE_BIT,
		.algs = "deflate\n",
	}, {
		.alg_msk = HZIP_ALG_LZ77_BIT,
		.algs = "lz77_zstd\n",
	},
};

static struct hisi_qm_list zip_devices = {
	.register_to_crypto	= hisi_zip_register_to_crypto,
	.unregister_from_crypto	= hisi_zip_unregister_from_crypto,
};

static struct zip_dfx_item zip_dfx_files[] = {
	{"send_cnt", offsetof(struct hisi_zip_dfx, send_cnt)},
	{"recv_cnt", offsetof(struct hisi_zip_dfx, recv_cnt)},
	{"send_busy_cnt", offsetof(struct hisi_zip_dfx, send_busy_cnt)},
	{"err_bd_cnt", offsetof(struct hisi_zip_dfx, err_bd_cnt)},
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
	{ .int_msk = BIT(11), .msg = "zip_axi_poison_err" },
	{ .int_msk = BIT(12), .msg = "zip_sva_err" },
	{ /* sentinel */ }
};

enum ctrl_debug_file_index {
	HZIP_CLEAR_ENABLE,
	HZIP_DEBUG_FILE_NUM,
};

static const char * const ctrl_debug_file_name[] = {
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
	struct hisi_zip *hisi_zip;
	struct ctrl_debug_file files[HZIP_DEBUG_FILE_NUM];
};

enum zip_cap_type {
	ZIP_QM_NFE_MASK_CAP = 0x0,
	ZIP_QM_RESET_MASK_CAP,
	ZIP_QM_OOO_SHUTDOWN_MASK_CAP,
	ZIP_QM_CE_MASK_CAP,
	ZIP_NFE_MASK_CAP,
	ZIP_RESET_MASK_CAP,
	ZIP_OOO_SHUTDOWN_MASK_CAP,
	ZIP_CE_MASK_CAP,
	ZIP_CLUSTER_NUM_CAP,
	ZIP_CORE_TYPE_NUM_CAP,
	ZIP_CORE_NUM_CAP,
	ZIP_CLUSTER_COMP_NUM_CAP,
	ZIP_CLUSTER_DECOMP_NUM_CAP,
	ZIP_DECOMP_ENABLE_BITMAP,
	ZIP_COMP_ENABLE_BITMAP,
	ZIP_DRV_ALG_BITMAP,
	ZIP_DEV_ALG_BITMAP,
	ZIP_CORE1_ALG_BITMAP,
	ZIP_CORE2_ALG_BITMAP,
	ZIP_CORE3_ALG_BITMAP,
	ZIP_CORE4_ALG_BITMAP,
	ZIP_CORE5_ALG_BITMAP,
	ZIP_CAP_MAX
};

static struct hisi_qm_cap_info zip_basic_cap_info[] = {
	{ZIP_QM_NFE_MASK_CAP, 0x3124, 0, GENMASK(31, 0), 0x0, 0x1C57, 0x7C77},
	{ZIP_QM_RESET_MASK_CAP, 0x3128, 0, GENMASK(31, 0), 0x0, 0xC57, 0x6C77},
	{ZIP_QM_OOO_SHUTDOWN_MASK_CAP, 0x3128, 0, GENMASK(31, 0), 0x0, 0x4, 0x6C77},
	{ZIP_QM_CE_MASK_CAP, 0x312C, 0, GENMASK(31, 0), 0x0, 0x8, 0x8},
	{ZIP_NFE_MASK_CAP, 0x3130, 0, GENMASK(31, 0), 0x0, 0x7FE, 0x1FFE},
	{ZIP_RESET_MASK_CAP, 0x3134, 0, GENMASK(31, 0), 0x0, 0x7FE, 0x7FE},
	{ZIP_OOO_SHUTDOWN_MASK_CAP, 0x3134, 0, GENMASK(31, 0), 0x0, 0x2, 0x7FE},
	{ZIP_CE_MASK_CAP, 0x3138, 0, GENMASK(31, 0), 0x0, 0x1, 0x1},
	{ZIP_CLUSTER_NUM_CAP, 0x313C, 28, GENMASK(3, 0), 0x1, 0x1, 0x1},
	{ZIP_CORE_TYPE_NUM_CAP, 0x313C, 24, GENMASK(3, 0), 0x2, 0x2, 0x2},
	{ZIP_CORE_NUM_CAP, 0x313C, 16, GENMASK(7, 0), 0x8, 0x8, 0x5},
	{ZIP_CLUSTER_COMP_NUM_CAP, 0x313C, 8, GENMASK(7, 0), 0x2, 0x2, 0x2},
	{ZIP_CLUSTER_DECOMP_NUM_CAP, 0x313C, 0, GENMASK(7, 0), 0x6, 0x6, 0x3},
	{ZIP_DECOMP_ENABLE_BITMAP, 0x3140, 16, GENMASK(15, 0), 0xFC, 0xFC, 0x1C},
	{ZIP_COMP_ENABLE_BITMAP, 0x3140, 0, GENMASK(15, 0), 0x3, 0x3, 0x3},
	{ZIP_DRV_ALG_BITMAP, 0x3144, 0, GENMASK(31, 0), 0x0, 0x0, 0x30},
	{ZIP_DEV_ALG_BITMAP, 0x3148, 0, GENMASK(31, 0), 0xF, 0xF, 0x3F},
	{ZIP_CORE1_ALG_BITMAP, 0x314C, 0, GENMASK(31, 0), 0x5, 0x5, 0xD5},
	{ZIP_CORE2_ALG_BITMAP, 0x3150, 0, GENMASK(31, 0), 0x5, 0x5, 0xD5},
	{ZIP_CORE3_ALG_BITMAP, 0x3154, 0, GENMASK(31, 0), 0xA, 0xA, 0x2A},
	{ZIP_CORE4_ALG_BITMAP, 0x3158, 0, GENMASK(31, 0), 0xA, 0xA, 0x2A},
	{ZIP_CORE5_ALG_BITMAP, 0x315C, 0, GENMASK(31, 0), 0xA, 0xA, 0x2A},
	{ZIP_CAP_MAX, 0x317c, 0, GENMASK(0, 0), 0x0, 0x0, 0x0}
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

static const struct debugfs_reg32 hzip_dfx_regs[] = {
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
	{"HZIP_CONSUMED_BYTE             ",  0x38ull},
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

static const struct debugfs_reg32 hzip_com_dfx_regs[] = {
	{"HZIP_CLOCK_GATE_CTRL           ",  0x301004},
	{"HZIP_CORE_INT_RAS_CE_ENB       ",  0x301160},
	{"HZIP_CORE_INT_RAS_NFE_ENB      ",  0x301164},
	{"HZIP_CORE_INT_RAS_FE_ENB       ",  0x301168},
	{"HZIP_UNCOM_ERR_RAS_CTRL        ",  0x30116C},
};

static const struct debugfs_reg32 hzip_dump_dfx_regs[] = {
	{"HZIP_GET_BD_NUM                ",  0x00ull},
	{"HZIP_GET_RIGHT_BD              ",  0x04ull},
	{"HZIP_GET_ERROR_BD              ",  0x08ull},
	{"HZIP_DONE_BD_NUM               ",  0x0cull},
	{"HZIP_MAX_DELAY                 ",  0x20ull},
};

/* define the ZIP's dfx regs region and region length */
static struct dfx_diff_registers hzip_diff_regs[] = {
	{
		.reg_offset = HZIP_CORE_DFX_BASE,
		.reg_len = HZIP_CORE_REGS_BASE_LEN,
	}, {
		.reg_offset = HZIP_CORE_DFX_COMP_0,
		.reg_len = HZIP_CORE_REGS_DFX_LEN,
	}, {
		.reg_offset = HZIP_CORE_DFX_COMP_1,
		.reg_len = HZIP_CORE_REGS_DFX_LEN,
	}, {
		.reg_offset = HZIP_CORE_DFX_DECOMP_0,
		.reg_len = HZIP_CORE_REGS_DFX_LEN,
	}, {
		.reg_offset = HZIP_CORE_DFX_DECOMP_1,
		.reg_len = HZIP_CORE_REGS_DFX_LEN,
	}, {
		.reg_offset = HZIP_CORE_DFX_DECOMP_2,
		.reg_len = HZIP_CORE_REGS_DFX_LEN,
	}, {
		.reg_offset = HZIP_CORE_DFX_DECOMP_3,
		.reg_len = HZIP_CORE_REGS_DFX_LEN,
	}, {
		.reg_offset = HZIP_CORE_DFX_DECOMP_4,
		.reg_len = HZIP_CORE_REGS_DFX_LEN,
	}, {
		.reg_offset = HZIP_CORE_DFX_DECOMP_5,
		.reg_len = HZIP_CORE_REGS_DFX_LEN,
	},
};

static int hzip_diff_regs_show(struct seq_file *s, void *unused)
{
	struct hisi_qm *qm = s->private;

	hisi_qm_acc_diff_regs_dump(qm, s, qm->debug.acc_diff_regs,
					ARRAY_SIZE(hzip_diff_regs));

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(hzip_diff_regs);
static const struct kernel_param_ops zip_uacce_mode_ops = {
	.set = uacce_mode_set,
	.get = param_get_int,
};

/*
 * uacce_mode = 0 means zip only register to crypto,
 * uacce_mode = 1 means zip both register to crypto and uacce.
 */
static u32 uacce_mode = UACCE_MODE_NOUACCE;
module_param_cb(uacce_mode, &zip_uacce_mode_ops, &uacce_mode, 0444);
MODULE_PARM_DESC(uacce_mode, UACCE_MODE_DESC);

static bool pf_q_num_flag;
static int pf_q_num_set(const char *val, const struct kernel_param *kp)
{
	pf_q_num_flag = true;

	return q_num_set(val, kp, PCI_DEVICE_ID_HUAWEI_ZIP_PF);
}

static const struct kernel_param_ops pf_q_num_ops = {
	.set = pf_q_num_set,
	.get = param_get_int,
};

static u32 pf_q_num = HZIP_PF_DEF_Q_NUM;
module_param_cb(pf_q_num, &pf_q_num_ops, &pf_q_num, 0444);
MODULE_PARM_DESC(pf_q_num, "Number of queues in PF(v1 2-4096, v2 2-1024)");

static const struct kernel_param_ops vfs_num_ops = {
	.set = vfs_num_set,
	.get = param_get_int,
};

static u32 vfs_num;
module_param_cb(vfs_num, &vfs_num_ops, &vfs_num, 0444);
MODULE_PARM_DESC(vfs_num, "Number of VFs to enable(1-63), 0(default)");

static const struct pci_device_id hisi_zip_dev_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_HUAWEI, PCI_DEVICE_ID_HUAWEI_ZIP_PF) },
	{ PCI_DEVICE(PCI_VENDOR_ID_HUAWEI, PCI_DEVICE_ID_HUAWEI_ZIP_VF) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, hisi_zip_dev_ids);

int zip_create_qps(struct hisi_qp **qps, int qp_num, int node)
{
	if (node == NUMA_NO_NODE)
		node = cpu_to_node(smp_processor_id());

	return hisi_qm_alloc_qps_node(&zip_devices, qp_num, 0, node, qps);
}

bool hisi_zip_alg_support(struct hisi_qm *qm, u32 alg)
{
	u32 cap_val;

	cap_val = hisi_qm_get_hw_info(qm, zip_basic_cap_info, ZIP_DRV_ALG_BITMAP, qm->cap_ver);
	if ((alg & cap_val) == alg)
		return true;

	return false;
}

static int hisi_zip_set_qm_algs(struct hisi_qm *qm)
{
	struct device *dev = &qm->pdev->dev;
	char *algs, *ptr;
	u32 alg_mask;
	int i;

	if (!qm->use_sva)
		return 0;

	algs = devm_kzalloc(dev, HZIP_DEV_ALG_MAX_LEN * sizeof(char), GFP_KERNEL);
	if (!algs)
		return -ENOMEM;

	alg_mask = hisi_qm_get_hw_info(qm, zip_basic_cap_info, ZIP_DEV_ALG_BITMAP, qm->cap_ver);

	for (i = 0; i < ARRAY_SIZE(zip_dev_algs); i++)
		if (alg_mask & zip_dev_algs[i].alg_msk)
			strcat(algs, zip_dev_algs[i].algs);

	ptr = strrchr(algs, '\n');
	if (ptr)
		*ptr = '\0';

	qm->uacce->algs = algs;

	return 0;
}

static void hisi_zip_open_sva_prefetch(struct hisi_qm *qm)
{
	u32 val;
	int ret;

	if (!test_bit(QM_SUPPORT_SVA_PREFETCH, &qm->caps))
		return;

	/* Enable prefetch */
	val = readl_relaxed(qm->io_base + HZIP_PREFETCH_CFG);
	val &= HZIP_PREFETCH_ENABLE;
	writel(val, qm->io_base + HZIP_PREFETCH_CFG);

	ret = readl_relaxed_poll_timeout(qm->io_base + HZIP_PREFETCH_CFG,
					 val, !(val & HZIP_SVA_PREFETCH_DISABLE),
					 HZIP_DELAY_1_US, HZIP_POLL_TIMEOUT_US);
	if (ret)
		pci_err(qm->pdev, "failed to open sva prefetch\n");
}

static void hisi_zip_close_sva_prefetch(struct hisi_qm *qm)
{
	u32 val;
	int ret;

	if (!test_bit(QM_SUPPORT_SVA_PREFETCH, &qm->caps))
		return;

	val = readl_relaxed(qm->io_base + HZIP_PREFETCH_CFG);
	val |= HZIP_SVA_PREFETCH_DISABLE;
	writel(val, qm->io_base + HZIP_PREFETCH_CFG);

	ret = readl_relaxed_poll_timeout(qm->io_base + HZIP_SVA_TRANS,
					 val, !(val & HZIP_SVA_DISABLE_READY),
					 HZIP_DELAY_1_US, HZIP_POLL_TIMEOUT_US);
	if (ret)
		pci_err(qm->pdev, "failed to close sva prefetch\n");
}

static void hisi_zip_enable_clock_gate(struct hisi_qm *qm)
{
	u32 val;

	if (qm->ver < QM_HW_V3)
		return;

	val = readl(qm->io_base + HZIP_CLOCK_GATE_CTRL);
	val |= HZIP_CLOCK_GATED_EN;
	writel(val, qm->io_base + HZIP_CLOCK_GATE_CTRL);

	val = readl(qm->io_base + HZIP_PEH_CFG_AUTO_GATE);
	val |= HZIP_PEH_CFG_AUTO_GATE_EN;
	writel(val, qm->io_base + HZIP_PEH_CFG_AUTO_GATE);
}

static int hisi_zip_set_user_domain_and_cache(struct hisi_qm *qm)
{
	void __iomem *base = qm->io_base;
	u32 dcomp_bm, comp_bm;

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
	writel(HZIP_CACHE_ALL_EN, base + HZIP_PORT_ARCA_CHE_0);
	writel(HZIP_CACHE_ALL_EN, base + HZIP_PORT_ARCA_CHE_1);
	writel(HZIP_CACHE_ALL_EN, base + HZIP_PORT_AWCA_CHE_0);
	writel(HZIP_CACHE_ALL_EN, base + HZIP_PORT_AWCA_CHE_1);

	/* user domain configurations */
	writel(AXUSER_BASE, base + HZIP_BD_RUSER_32_63);
	writel(AXUSER_BASE, base + HZIP_BD_WUSER_32_63);

	if (qm->use_sva && qm->ver == QM_HW_V2) {
		writel(AXUSER_BASE | AXUSER_SSV, base + HZIP_DATA_RUSER_32_63);
		writel(AXUSER_BASE | AXUSER_SSV, base + HZIP_DATA_WUSER_32_63);
		writel(AXUSER_BASE | AXUSER_SSV, base + HZIP_SGL_RUSER_32_63);
	} else {
		writel(AXUSER_BASE, base + HZIP_DATA_RUSER_32_63);
		writel(AXUSER_BASE, base + HZIP_DATA_WUSER_32_63);
		writel(AXUSER_BASE, base + HZIP_SGL_RUSER_32_63);
	}

	/* let's open all compression/decompression cores */
	dcomp_bm = hisi_qm_get_hw_info(qm, zip_basic_cap_info,
				       ZIP_DECOMP_ENABLE_BITMAP, qm->cap_ver);
	comp_bm = hisi_qm_get_hw_info(qm, zip_basic_cap_info,
				      ZIP_COMP_ENABLE_BITMAP, qm->cap_ver);
	writel(HZIP_DECOMP_CHECK_ENABLE | dcomp_bm | comp_bm, base + HZIP_CLOCK_GATE_CTRL);

	/* enable sqc,cqc writeback */
	writel(SQC_CACHE_ENABLE | CQC_CACHE_ENABLE | SQC_CACHE_WB_ENABLE |
	       CQC_CACHE_WB_ENABLE | FIELD_PREP(SQC_CACHE_WB_THRD, 1) |
	       FIELD_PREP(CQC_CACHE_WB_THRD, 1), base + QM_CACHE_CTL);

	hisi_zip_enable_clock_gate(qm);

	return 0;
}

static void hisi_zip_master_ooo_ctrl(struct hisi_qm *qm, bool enable)
{
	u32 val1, val2;

	val1 = readl(qm->io_base + HZIP_SOFT_CTRL_ZIP_CONTROL);
	if (enable) {
		val1 |= HZIP_AXI_SHUTDOWN_ENABLE;
		val2 = hisi_qm_get_hw_info(qm, zip_basic_cap_info,
				ZIP_OOO_SHUTDOWN_MASK_CAP, qm->cap_ver);
	} else {
		val1 &= ~HZIP_AXI_SHUTDOWN_ENABLE;
		val2 = 0x0;
	}

	if (qm->ver > QM_HW_V2)
		writel(val2, qm->io_base + HZIP_OOO_SHUTDOWN_SEL);

	writel(val1, qm->io_base + HZIP_SOFT_CTRL_ZIP_CONTROL);
}

static void hisi_zip_hw_error_enable(struct hisi_qm *qm)
{
	u32 nfe, ce;

	if (qm->ver == QM_HW_V1) {
		writel(HZIP_CORE_INT_MASK_ALL,
		       qm->io_base + HZIP_CORE_INT_MASK_REG);
		dev_info(&qm->pdev->dev, "Does not support hw error handle\n");
		return;
	}

	nfe = hisi_qm_get_hw_info(qm, zip_basic_cap_info, ZIP_NFE_MASK_CAP, qm->cap_ver);
	ce = hisi_qm_get_hw_info(qm, zip_basic_cap_info, ZIP_CE_MASK_CAP, qm->cap_ver);

	/* clear ZIP hw error source if having */
	writel(ce | nfe | HZIP_CORE_INT_RAS_FE_ENB_MASK, qm->io_base + HZIP_CORE_INT_SOURCE);

	/* configure error type */
	writel(ce, qm->io_base + HZIP_CORE_INT_RAS_CE_ENB);
	writel(HZIP_CORE_INT_RAS_FE_ENB_MASK, qm->io_base + HZIP_CORE_INT_RAS_FE_ENB);
	writel(nfe, qm->io_base + HZIP_CORE_INT_RAS_NFE_ENB);

	hisi_zip_master_ooo_ctrl(qm, true);

	/* enable ZIP hw error interrupts */
	writel(0, qm->io_base + HZIP_CORE_INT_MASK_REG);
}

static void hisi_zip_hw_error_disable(struct hisi_qm *qm)
{
	u32 nfe, ce;

	/* disable ZIP hw error interrupts */
	nfe = hisi_qm_get_hw_info(qm, zip_basic_cap_info, ZIP_NFE_MASK_CAP, qm->cap_ver);
	ce = hisi_qm_get_hw_info(qm, zip_basic_cap_info, ZIP_CE_MASK_CAP, qm->cap_ver);
	writel(ce | nfe | HZIP_CORE_INT_RAS_FE_ENB_MASK, qm->io_base + HZIP_CORE_INT_MASK_REG);

	hisi_zip_master_ooo_ctrl(qm, false);
}

static inline struct hisi_qm *file_to_qm(struct ctrl_debug_file *file)
{
	struct hisi_zip *hisi_zip = file->ctrl->hisi_zip;

	return &hisi_zip->qm;
}

static u32 clear_enable_read(struct hisi_qm *qm)
{
	return readl(qm->io_base + HZIP_SOFT_CTRL_CNT_CLR_CE) &
		     HZIP_SOFT_CTRL_CNT_CLR_CE_BIT;
}

static int clear_enable_write(struct hisi_qm *qm, u32 val)
{
	u32 tmp;

	if (val != 1 && val != 0)
		return -EINVAL;

	tmp = (readl(qm->io_base + HZIP_SOFT_CTRL_CNT_CLR_CE) &
	       ~HZIP_SOFT_CTRL_CNT_CLR_CE_BIT) | val;
	writel(tmp, qm->io_base + HZIP_SOFT_CTRL_CNT_CLR_CE);

	return  0;
}

static ssize_t hisi_zip_ctrl_debug_read(struct file *filp, char __user *buf,
					size_t count, loff_t *pos)
{
	struct ctrl_debug_file *file = filp->private_data;
	struct hisi_qm *qm = file_to_qm(file);
	char tbuf[HZIP_BUF_SIZE];
	u32 val;
	int ret;

	ret = hisi_qm_get_dfx_access(qm);
	if (ret)
		return ret;

	spin_lock_irq(&file->lock);
	switch (file->index) {
	case HZIP_CLEAR_ENABLE:
		val = clear_enable_read(qm);
		break;
	default:
		goto err_input;
	}
	spin_unlock_irq(&file->lock);

	hisi_qm_put_dfx_access(qm);
	ret = scnprintf(tbuf, sizeof(tbuf), "%u\n", val);
	return simple_read_from_buffer(buf, count, pos, tbuf, ret);

err_input:
	spin_unlock_irq(&file->lock);
	hisi_qm_put_dfx_access(qm);
	return -EINVAL;
}

static ssize_t hisi_zip_ctrl_debug_write(struct file *filp,
					 const char __user *buf,
					 size_t count, loff_t *pos)
{
	struct ctrl_debug_file *file = filp->private_data;
	struct hisi_qm *qm = file_to_qm(file);
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
	ret = kstrtoul(tbuf, 0, &val);
	if (ret)
		return ret;

	ret = hisi_qm_get_dfx_access(qm);
	if (ret)
		return ret;

	spin_lock_irq(&file->lock);
	switch (file->index) {
	case HZIP_CLEAR_ENABLE:
		ret = clear_enable_write(qm, val);
		if (ret)
			goto err_input;
		break;
	default:
		ret = -EINVAL;
		goto err_input;
	}

	ret = count;

err_input:
	spin_unlock_irq(&file->lock);
	hisi_qm_put_dfx_access(qm);
	return ret;
}

static const struct file_operations ctrl_debug_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = hisi_zip_ctrl_debug_read,
	.write = hisi_zip_ctrl_debug_write,
};

static int zip_debugfs_atomic64_set(void *data, u64 val)
{
	if (val)
		return -EINVAL;

	atomic64_set((atomic64_t *)data, 0);

	return 0;
}

static int zip_debugfs_atomic64_get(void *data, u64 *val)
{
	*val = atomic64_read((atomic64_t *)data);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(zip_atomic64_ops, zip_debugfs_atomic64_get,
			 zip_debugfs_atomic64_set, "%llu\n");

static int hisi_zip_regs_show(struct seq_file *s, void *unused)
{
	hisi_qm_regs_dump(s, s->private);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(hisi_zip_regs);

static int hisi_zip_core_debug_init(struct hisi_qm *qm)
{
	u32 zip_core_num, zip_comp_core_num;
	struct device *dev = &qm->pdev->dev;
	struct debugfs_regset32 *regset;
	struct dentry *tmp_d;
	char buf[HZIP_BUF_SIZE];
	int i;

	zip_core_num = hisi_qm_get_hw_info(qm, zip_basic_cap_info, ZIP_CORE_NUM_CAP, qm->cap_ver);
	zip_comp_core_num = hisi_qm_get_hw_info(qm, zip_basic_cap_info, ZIP_CLUSTER_COMP_NUM_CAP,
						qm->cap_ver);

	for (i = 0; i < zip_core_num; i++) {
		if (i < zip_comp_core_num)
			scnprintf(buf, sizeof(buf), "comp_core%d", i);
		else
			scnprintf(buf, sizeof(buf), "decomp_core%d",
				  i - zip_comp_core_num);

		regset = devm_kzalloc(dev, sizeof(*regset), GFP_KERNEL);
		if (!regset)
			return -ENOENT;

		regset->regs = hzip_dfx_regs;
		regset->nregs = ARRAY_SIZE(hzip_dfx_regs);
		regset->base = qm->io_base + core_offsets[i];
		regset->dev = dev;

		tmp_d = debugfs_create_dir(buf, qm->debug.debug_root);
		debugfs_create_file("regs", 0444, tmp_d, regset,
				    &hisi_zip_regs_fops);
	}

	return 0;
}

static void hisi_zip_dfx_debug_init(struct hisi_qm *qm)
{
	struct dfx_diff_registers *hzip_regs = qm->debug.acc_diff_regs;
	struct hisi_zip *zip = container_of(qm, struct hisi_zip, qm);
	struct hisi_zip_dfx *dfx = &zip->dfx;
	struct dentry *tmp_dir;
	void *data;
	int i;

	tmp_dir = debugfs_create_dir("zip_dfx", qm->debug.debug_root);
	for (i = 0; i < ARRAY_SIZE(zip_dfx_files); i++) {
		data = (atomic64_t *)((uintptr_t)dfx + zip_dfx_files[i].offset);
		debugfs_create_file(zip_dfx_files[i].name,
				    0644, tmp_dir, data,
				    &zip_atomic64_ops);
	}

	if (qm->fun_type == QM_HW_PF && hzip_regs)
		debugfs_create_file("diff_regs", 0444, tmp_dir,
				      qm, &hzip_diff_regs_fops);
}

static int hisi_zip_ctrl_debug_init(struct hisi_qm *qm)
{
	struct hisi_zip *zip = container_of(qm, struct hisi_zip, qm);
	int i;

	for (i = HZIP_CLEAR_ENABLE; i < HZIP_DEBUG_FILE_NUM; i++) {
		spin_lock_init(&zip->ctrl->files[i].lock);
		zip->ctrl->files[i].ctrl = zip->ctrl;
		zip->ctrl->files[i].index = i;

		debugfs_create_file(ctrl_debug_file_name[i], 0600,
				    qm->debug.debug_root,
				    zip->ctrl->files + i,
				    &ctrl_debug_fops);
	}

	return hisi_zip_core_debug_init(qm);
}

static int hisi_zip_debugfs_init(struct hisi_qm *qm)
{
	struct device *dev = &qm->pdev->dev;
	struct dentry *dev_d;
	int ret;

	dev_d = debugfs_create_dir(dev_name(dev), hzip_debugfs_root);

	qm->debug.sqe_mask_offset = HZIP_SQE_MASK_OFFSET;
	qm->debug.sqe_mask_len = HZIP_SQE_MASK_LEN;
	qm->debug.debug_root = dev_d;
	ret = hisi_qm_regs_debugfs_init(qm, hzip_diff_regs, ARRAY_SIZE(hzip_diff_regs));
	if (ret) {
		dev_warn(dev, "Failed to init ZIP diff regs!\n");
		goto debugfs_remove;
	}

	hisi_qm_debug_init(qm);

	if (qm->fun_type == QM_HW_PF) {
		ret = hisi_zip_ctrl_debug_init(qm);
		if (ret)
			goto failed_to_create;
	}

	hisi_zip_dfx_debug_init(qm);

	return 0;

failed_to_create:
	hisi_qm_regs_debugfs_uninit(qm, ARRAY_SIZE(hzip_diff_regs));
debugfs_remove:
	debugfs_remove_recursive(hzip_debugfs_root);
	return ret;
}

/* hisi_zip_debug_regs_clear() - clear the zip debug regs */
static void hisi_zip_debug_regs_clear(struct hisi_qm *qm)
{
	int i, j;

	/* enable register read_clear bit */
	writel(HZIP_RD_CNT_CLR_CE_EN, qm->io_base + HZIP_SOFT_CTRL_CNT_CLR_CE);
	for (i = 0; i < ARRAY_SIZE(core_offsets); i++)
		for (j = 0; j < ARRAY_SIZE(hzip_dfx_regs); j++)
			readl(qm->io_base + core_offsets[i] +
			      hzip_dfx_regs[j].offset);

	/* disable register read_clear bit */
	writel(0x0, qm->io_base + HZIP_SOFT_CTRL_CNT_CLR_CE);

	hisi_qm_debug_regs_clear(qm);
}

static void hisi_zip_debugfs_exit(struct hisi_qm *qm)
{
	hisi_qm_regs_debugfs_uninit(qm, ARRAY_SIZE(hzip_diff_regs));

	debugfs_remove_recursive(qm->debug.debug_root);

	if (qm->fun_type == QM_HW_PF) {
		hisi_zip_debug_regs_clear(qm);
		qm->debug.curr_qm_qp_num = 0;
	}
}

static int hisi_zip_show_last_regs_init(struct hisi_qm *qm)
{
	int core_dfx_regs_num =  ARRAY_SIZE(hzip_dump_dfx_regs);
	int com_dfx_regs_num = ARRAY_SIZE(hzip_com_dfx_regs);
	struct qm_debug *debug = &qm->debug;
	void __iomem *io_base;
	u32 zip_core_num;
	int i, j, idx;

	zip_core_num = hisi_qm_get_hw_info(qm, zip_basic_cap_info, ZIP_CORE_NUM_CAP, qm->cap_ver);

	debug->last_words = kcalloc(core_dfx_regs_num * zip_core_num + com_dfx_regs_num,
				    sizeof(unsigned int), GFP_KERNEL);
	if (!debug->last_words)
		return -ENOMEM;

	for (i = 0; i < com_dfx_regs_num; i++) {
		io_base = qm->io_base + hzip_com_dfx_regs[i].offset;
		debug->last_words[i] = readl_relaxed(io_base);
	}

	for (i = 0; i < zip_core_num; i++) {
		io_base = qm->io_base + core_offsets[i];
		for (j = 0; j < core_dfx_regs_num; j++) {
			idx = com_dfx_regs_num + i * core_dfx_regs_num + j;
			debug->last_words[idx] = readl_relaxed(
				io_base + hzip_dump_dfx_regs[j].offset);
		}
	}

	return 0;
}

static void hisi_zip_show_last_regs_uninit(struct hisi_qm *qm)
{
	struct qm_debug *debug = &qm->debug;

	if (qm->fun_type == QM_HW_VF || !debug->last_words)
		return;

	kfree(debug->last_words);
	debug->last_words = NULL;
}

static void hisi_zip_show_last_dfx_regs(struct hisi_qm *qm)
{
	int core_dfx_regs_num =  ARRAY_SIZE(hzip_dump_dfx_regs);
	int com_dfx_regs_num = ARRAY_SIZE(hzip_com_dfx_regs);
	u32 zip_core_num, zip_comp_core_num;
	struct qm_debug *debug = &qm->debug;
	char buf[HZIP_BUF_SIZE];
	void __iomem *base;
	int i, j, idx;
	u32 val;

	if (qm->fun_type == QM_HW_VF || !debug->last_words)
		return;

	for (i = 0; i < com_dfx_regs_num; i++) {
		val = readl_relaxed(qm->io_base + hzip_com_dfx_regs[i].offset);
		if (debug->last_words[i] != val)
			pci_info(qm->pdev, "com_dfx: %s \t= 0x%08x => 0x%08x\n",
				 hzip_com_dfx_regs[i].name, debug->last_words[i], val);
	}

	zip_core_num = hisi_qm_get_hw_info(qm, zip_basic_cap_info, ZIP_CORE_NUM_CAP, qm->cap_ver);
	zip_comp_core_num = hisi_qm_get_hw_info(qm, zip_basic_cap_info, ZIP_CLUSTER_COMP_NUM_CAP,
						qm->cap_ver);
	for (i = 0; i < zip_core_num; i++) {
		if (i < zip_comp_core_num)
			scnprintf(buf, sizeof(buf), "Comp_core-%d", i);
		else
			scnprintf(buf, sizeof(buf), "Decomp_core-%d",
				  i - zip_comp_core_num);
		base = qm->io_base + core_offsets[i];

		pci_info(qm->pdev, "==>%s:\n", buf);
		/* dump last word for dfx regs during control resetting */
		for (j = 0; j < core_dfx_regs_num; j++) {
			idx = com_dfx_regs_num + i * core_dfx_regs_num + j;
			val = readl_relaxed(base + hzip_dump_dfx_regs[j].offset);
			if (debug->last_words[idx] != val)
				pci_info(qm->pdev, "%s \t= 0x%08x => 0x%08x\n",
					 hzip_dump_dfx_regs[j].name,
					 debug->last_words[idx], val);
		}
	}
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
			}
		}
		err++;
	}
}

static u32 hisi_zip_get_hw_err_status(struct hisi_qm *qm)
{
	return readl(qm->io_base + HZIP_CORE_INT_STATUS);
}

static void hisi_zip_clear_hw_err_status(struct hisi_qm *qm, u32 err_sts)
{
	u32 nfe;

	writel(err_sts, qm->io_base + HZIP_CORE_INT_SOURCE);
	nfe = hisi_qm_get_hw_info(qm, zip_basic_cap_info, ZIP_NFE_MASK_CAP, qm->cap_ver);
	writel(nfe, qm->io_base + HZIP_CORE_INT_RAS_NFE_ENB);
}

static void hisi_zip_open_axi_master_ooo(struct hisi_qm *qm)
{
	u32 val;

	val = readl(qm->io_base + HZIP_SOFT_CTRL_ZIP_CONTROL);

	writel(val & ~HZIP_AXI_SHUTDOWN_ENABLE,
	       qm->io_base + HZIP_SOFT_CTRL_ZIP_CONTROL);

	writel(val | HZIP_AXI_SHUTDOWN_ENABLE,
	       qm->io_base + HZIP_SOFT_CTRL_ZIP_CONTROL);
}

static void hisi_zip_close_axi_master_ooo(struct hisi_qm *qm)
{
	u32 nfe_enb;

	/* Disable ECC Mbit error report. */
	nfe_enb = readl(qm->io_base + HZIP_CORE_INT_RAS_NFE_ENB);
	writel(nfe_enb & ~HZIP_CORE_INT_STATUS_M_ECC,
	       qm->io_base + HZIP_CORE_INT_RAS_NFE_ENB);

	/* Inject zip ECC Mbit error to block master ooo. */
	writel(HZIP_CORE_INT_STATUS_M_ECC,
	       qm->io_base + HZIP_CORE_INT_SET);
}

static void hisi_zip_err_info_init(struct hisi_qm *qm)
{
	struct hisi_qm_err_info *err_info = &qm->err_info;

	err_info->fe = HZIP_CORE_INT_RAS_FE_ENB_MASK;
	err_info->ce = hisi_qm_get_hw_info(qm, zip_basic_cap_info, ZIP_QM_CE_MASK_CAP, qm->cap_ver);
	err_info->nfe = hisi_qm_get_hw_info(qm, zip_basic_cap_info,
					    ZIP_QM_NFE_MASK_CAP, qm->cap_ver);
	err_info->ecc_2bits_mask = HZIP_CORE_INT_STATUS_M_ECC;
	err_info->qm_shutdown_mask = hisi_qm_get_hw_info(qm, zip_basic_cap_info,
							 ZIP_QM_OOO_SHUTDOWN_MASK_CAP, qm->cap_ver);
	err_info->dev_shutdown_mask = hisi_qm_get_hw_info(qm, zip_basic_cap_info,
							  ZIP_OOO_SHUTDOWN_MASK_CAP, qm->cap_ver);
	err_info->qm_reset_mask = hisi_qm_get_hw_info(qm, zip_basic_cap_info,
						      ZIP_QM_RESET_MASK_CAP, qm->cap_ver);
	err_info->dev_reset_mask = hisi_qm_get_hw_info(qm, zip_basic_cap_info,
						       ZIP_RESET_MASK_CAP, qm->cap_ver);
	err_info->msi_wr_port = HZIP_WR_PORT;
	err_info->acpi_rst = "ZRST";
}

static const struct hisi_qm_err_ini hisi_zip_err_ini = {
	.hw_init		= hisi_zip_set_user_domain_and_cache,
	.hw_err_enable		= hisi_zip_hw_error_enable,
	.hw_err_disable		= hisi_zip_hw_error_disable,
	.get_dev_hw_err_status	= hisi_zip_get_hw_err_status,
	.clear_dev_hw_err_status = hisi_zip_clear_hw_err_status,
	.log_dev_hw_err		= hisi_zip_log_hw_error,
	.open_axi_master_ooo	= hisi_zip_open_axi_master_ooo,
	.close_axi_master_ooo	= hisi_zip_close_axi_master_ooo,
	.open_sva_prefetch	= hisi_zip_open_sva_prefetch,
	.close_sva_prefetch	= hisi_zip_close_sva_prefetch,
	.show_last_dfx_regs	= hisi_zip_show_last_dfx_regs,
	.err_info_init		= hisi_zip_err_info_init,
};

static int hisi_zip_pf_probe_init(struct hisi_zip *hisi_zip)
{
	struct hisi_qm *qm = &hisi_zip->qm;
	struct hisi_zip_ctrl *ctrl;
	int ret;

	ctrl = devm_kzalloc(&qm->pdev->dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	hisi_zip->ctrl = ctrl;
	ctrl->hisi_zip = hisi_zip;
	qm->err_ini = &hisi_zip_err_ini;
	qm->err_ini->err_info_init(qm);

	ret = hisi_zip_set_user_domain_and_cache(qm);
	if (ret)
		return ret;

	hisi_zip_open_sva_prefetch(qm);
	hisi_qm_dev_err_init(qm);
	hisi_zip_debug_regs_clear(qm);

	ret = hisi_zip_show_last_regs_init(qm);
	if (ret)
		pci_err(qm->pdev, "Failed to init last word regs!\n");

	return ret;
}

static int hisi_zip_qm_init(struct hisi_qm *qm, struct pci_dev *pdev)
{
	int ret;

	qm->pdev = pdev;
	qm->ver = pdev->revision;
	qm->mode = uacce_mode;
	qm->sqe_size = HZIP_SQE_SIZE;
	qm->dev_name = hisi_zip_name;

	qm->fun_type = (pdev->device == PCI_DEVICE_ID_HUAWEI_ZIP_PF) ?
			QM_HW_PF : QM_HW_VF;
	if (qm->fun_type == QM_HW_PF) {
		qm->qp_base = HZIP_PF_DEF_Q_BASE;
		qm->qp_num = pf_q_num;
		qm->debug.curr_qm_qp_num = pf_q_num;
		qm->qm_list = &zip_devices;
		if (pf_q_num_flag)
			set_bit(QM_MODULE_PARAM, &qm->misc_ctl);
	} else if (qm->fun_type == QM_HW_VF && qm->ver == QM_HW_V1) {
		/*
		 * have no way to get qm configure in VM in v1 hardware,
		 * so currently force PF to uses HZIP_PF_DEF_Q_NUM, and force
		 * to trigger only one VF in v1 hardware.
		 *
		 * v2 hardware has no such problem.
		 */
		qm->qp_base = HZIP_PF_DEF_Q_NUM;
		qm->qp_num = HZIP_QUEUE_NUM_V1 - HZIP_PF_DEF_Q_NUM;
	}

	ret = hisi_qm_init(qm);
	if (ret) {
		pci_err(qm->pdev, "Failed to init zip qm configures!\n");
		return ret;
	}

	ret = hisi_zip_set_qm_algs(qm);
	if (ret) {
		pci_err(qm->pdev, "Failed to set zip algs!\n");
		hisi_qm_uninit(qm);
	}

	return ret;
}

static void hisi_zip_qm_uninit(struct hisi_qm *qm)
{
	hisi_qm_uninit(qm);
}

static int hisi_zip_probe_init(struct hisi_zip *hisi_zip)
{
	u32 type_rate = HZIP_SHAPER_RATE_COMPRESS;
	struct hisi_qm *qm = &hisi_zip->qm;
	int ret;

	if (qm->fun_type == QM_HW_PF) {
		ret = hisi_zip_pf_probe_init(hisi_zip);
		if (ret)
			return ret;
		/* enable shaper type 0 */
		if (qm->ver >= QM_HW_V3) {
			type_rate |= QM_SHAPER_ENABLE;

			/* ZIP need to enable shaper type 1 */
			type_rate |= HZIP_SHAPER_RATE_DECOMPRESS << QM_SHAPER_TYPE1_OFFSET;
			qm->type_rate = type_rate;
		}
	}

	return 0;
}

static int hisi_zip_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct hisi_zip *hisi_zip;
	struct hisi_qm *qm;
	int ret;

	hisi_zip = devm_kzalloc(&pdev->dev, sizeof(*hisi_zip), GFP_KERNEL);
	if (!hisi_zip)
		return -ENOMEM;

	qm = &hisi_zip->qm;

	ret = hisi_zip_qm_init(qm, pdev);
	if (ret) {
		pci_err(pdev, "Failed to init ZIP QM (%d)!\n", ret);
		return ret;
	}

	ret = hisi_zip_probe_init(hisi_zip);
	if (ret) {
		pci_err(pdev, "Failed to probe (%d)!\n", ret);
		goto err_qm_uninit;
	}

	ret = hisi_qm_start(qm);
	if (ret)
		goto err_dev_err_uninit;

	ret = hisi_zip_debugfs_init(qm);
	if (ret)
		pci_err(pdev, "failed to init debugfs (%d)!\n", ret);

	hisi_qm_add_list(qm, &zip_devices);
	ret = hisi_qm_alg_register(qm, &zip_devices, HZIP_CTX_Q_NUM_DEF);
	if (ret < 0) {
		pci_err(pdev, "failed to register driver to crypto!\n");
		goto err_qm_del_list;
	}

	if (qm->uacce) {
		ret = uacce_register(qm->uacce);
		if (ret) {
			pci_err(pdev, "failed to register uacce (%d)!\n", ret);
			goto err_qm_alg_unregister;
		}
	}

	if (qm->fun_type == QM_HW_PF && vfs_num > 0) {
		ret = hisi_qm_sriov_enable(pdev, vfs_num);
		if (ret < 0)
			goto err_qm_alg_unregister;
	}

	hisi_qm_pm_init(qm);

	return 0;

err_qm_alg_unregister:
	hisi_qm_alg_unregister(qm, &zip_devices, HZIP_CTX_Q_NUM_DEF);

err_qm_del_list:
	hisi_qm_del_list(qm, &zip_devices);
	hisi_zip_debugfs_exit(qm);
	hisi_qm_stop(qm, QM_NORMAL);

err_dev_err_uninit:
	hisi_zip_show_last_regs_uninit(qm);
	hisi_qm_dev_err_uninit(qm);

err_qm_uninit:
	hisi_zip_qm_uninit(qm);

	return ret;
}

static void hisi_zip_remove(struct pci_dev *pdev)
{
	struct hisi_qm *qm = pci_get_drvdata(pdev);

	hisi_qm_pm_uninit(qm);
	hisi_qm_wait_task_finish(qm, &zip_devices);
	hisi_qm_alg_unregister(qm, &zip_devices, HZIP_CTX_Q_NUM_DEF);
	hisi_qm_del_list(qm, &zip_devices);

	if (qm->fun_type == QM_HW_PF && qm->vfs_num)
		hisi_qm_sriov_disable(pdev, true);

	hisi_zip_debugfs_exit(qm);
	hisi_qm_stop(qm, QM_NORMAL);
	hisi_zip_show_last_regs_uninit(qm);
	hisi_qm_dev_err_uninit(qm);
	hisi_zip_qm_uninit(qm);
}

static const struct dev_pm_ops hisi_zip_pm_ops = {
	SET_RUNTIME_PM_OPS(hisi_qm_suspend, hisi_qm_resume, NULL)
};

static const struct pci_error_handlers hisi_zip_err_handler = {
	.error_detected	= hisi_qm_dev_err_detected,
	.slot_reset	= hisi_qm_dev_slot_reset,
	.reset_prepare	= hisi_qm_reset_prepare,
	.reset_done	= hisi_qm_reset_done,
};

static struct pci_driver hisi_zip_pci_driver = {
	.name			= "hisi_zip",
	.id_table		= hisi_zip_dev_ids,
	.probe			= hisi_zip_probe,
	.remove			= hisi_zip_remove,
	.sriov_configure	= IS_ENABLED(CONFIG_PCI_IOV) ?
					hisi_qm_sriov_configure : NULL,
	.err_handler		= &hisi_zip_err_handler,
	.shutdown		= hisi_qm_dev_shutdown,
	.driver.pm		= &hisi_zip_pm_ops,
};

struct pci_driver *hisi_zip_get_pf_driver(void)
{
	return &hisi_zip_pci_driver;
}
EXPORT_SYMBOL_GPL(hisi_zip_get_pf_driver);

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
		hisi_zip_unregister_debugfs();
		pr_err("Failed to register pci driver.\n");
	}

	return ret;
}

static void __exit hisi_zip_exit(void)
{
	pci_unregister_driver(&hisi_zip_pci_driver);
	hisi_zip_unregister_debugfs();
}

module_init(hisi_zip_init);
module_exit(hisi_zip_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Zhou Wang <wangzhou1@hisilicon.com>");
MODULE_DESCRIPTION("Driver for HiSilicon ZIP accelerator");
