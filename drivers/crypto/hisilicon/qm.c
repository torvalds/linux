// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 HiSilicon Limited. */
#include <asm/page.h>
#include <linux/acpi.h>
#include <linux/aer.h>
#include <linux/bitmap.h>
#include <linux/dma-mapping.h>
#include <linux/idr.h>
#include <linux/io.h>
#include <linux/irqreturn.h>
#include <linux/log2.h>
#include <linux/pm_runtime.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uacce.h>
#include <linux/uaccess.h>
#include <uapi/misc/uacce/hisi_qm.h>
#include "qm.h"

/* eq/aeq irq enable */
#define QM_VF_AEQ_INT_SOURCE		0x0
#define QM_VF_AEQ_INT_MASK		0x4
#define QM_VF_EQ_INT_SOURCE		0x8
#define QM_VF_EQ_INT_MASK		0xc
#define QM_IRQ_NUM_V1			1
#define QM_IRQ_NUM_PF_V2		4
#define QM_IRQ_NUM_VF_V2		2
#define QM_IRQ_NUM_VF_V3		3

#define QM_EQ_EVENT_IRQ_VECTOR		0
#define QM_AEQ_EVENT_IRQ_VECTOR		1
#define QM_CMD_EVENT_IRQ_VECTOR		2
#define QM_ABNORMAL_EVENT_IRQ_VECTOR	3

/* mailbox */
#define QM_MB_CMD_SQC			0x0
#define QM_MB_CMD_CQC			0x1
#define QM_MB_CMD_EQC			0x2
#define QM_MB_CMD_AEQC			0x3
#define QM_MB_CMD_SQC_BT		0x4
#define QM_MB_CMD_CQC_BT		0x5
#define QM_MB_CMD_SQC_VFT_V2		0x6
#define QM_MB_CMD_STOP_QP		0x8
#define QM_MB_CMD_SRC			0xc
#define QM_MB_CMD_DST			0xd

#define QM_MB_CMD_SEND_BASE		0x300
#define QM_MB_EVENT_SHIFT		8
#define QM_MB_BUSY_SHIFT		13
#define QM_MB_OP_SHIFT			14
#define QM_MB_CMD_DATA_ADDR_L		0x304
#define QM_MB_CMD_DATA_ADDR_H		0x308
#define QM_MB_PING_ALL_VFS		0xffff
#define QM_MB_CMD_DATA_SHIFT		32
#define QM_MB_CMD_DATA_MASK		GENMASK(31, 0)

/* sqc shift */
#define QM_SQ_HOP_NUM_SHIFT		0
#define QM_SQ_PAGE_SIZE_SHIFT		4
#define QM_SQ_BUF_SIZE_SHIFT		8
#define QM_SQ_SQE_SIZE_SHIFT		12
#define QM_SQ_PRIORITY_SHIFT		0
#define QM_SQ_ORDERS_SHIFT		4
#define QM_SQ_TYPE_SHIFT		8
#define QM_QC_PASID_ENABLE		0x1
#define QM_QC_PASID_ENABLE_SHIFT	7

#define QM_SQ_TYPE_MASK			GENMASK(3, 0)
#define QM_SQ_TAIL_IDX(sqc)		((le16_to_cpu((sqc)->w11) >> 6) & 0x1)

/* cqc shift */
#define QM_CQ_HOP_NUM_SHIFT		0
#define QM_CQ_PAGE_SIZE_SHIFT		4
#define QM_CQ_BUF_SIZE_SHIFT		8
#define QM_CQ_CQE_SIZE_SHIFT		12
#define QM_CQ_PHASE_SHIFT		0
#define QM_CQ_FLAG_SHIFT		1

#define QM_CQE_PHASE(cqe)		(le16_to_cpu((cqe)->w7) & 0x1)
#define QM_QC_CQE_SIZE			4
#define QM_CQ_TAIL_IDX(cqc)		((le16_to_cpu((cqc)->w11) >> 6) & 0x1)

/* eqc shift */
#define QM_EQE_AEQE_SIZE		(2UL << 12)
#define QM_EQC_PHASE_SHIFT		16

#define QM_EQE_PHASE(eqe)		((le32_to_cpu((eqe)->dw0) >> 16) & 0x1)
#define QM_EQE_CQN_MASK			GENMASK(15, 0)

#define QM_AEQE_PHASE(aeqe)		((le32_to_cpu((aeqe)->dw0) >> 16) & 0x1)
#define QM_AEQE_TYPE_SHIFT		17
#define QM_AEQE_CQN_MASK		GENMASK(15, 0)
#define QM_CQ_OVERFLOW			0
#define QM_EQ_OVERFLOW			1
#define QM_CQE_ERROR			2

#define QM_DOORBELL_CMD_SQ		0
#define QM_DOORBELL_CMD_CQ		1
#define QM_DOORBELL_CMD_EQ		2
#define QM_DOORBELL_CMD_AEQ		3

#define QM_DOORBELL_BASE_V1		0x340
#define QM_DB_CMD_SHIFT_V1		16
#define QM_DB_INDEX_SHIFT_V1		32
#define QM_DB_PRIORITY_SHIFT_V1		48
#define QM_DOORBELL_SQ_CQ_BASE_V2	0x1000
#define QM_DOORBELL_EQ_AEQ_BASE_V2	0x2000
#define QM_QUE_ISO_CFG_V		0x0030
#define QM_PAGE_SIZE			0x0034
#define QM_QUE_ISO_EN			0x100154
#define QM_CAPBILITY			0x100158
#define QM_QP_NUN_MASK			GENMASK(10, 0)
#define QM_QP_DB_INTERVAL		0x10000
#define QM_QP_MAX_NUM_SHIFT		11
#define QM_DB_CMD_SHIFT_V2		12
#define QM_DB_RAND_SHIFT_V2		16
#define QM_DB_INDEX_SHIFT_V2		32
#define QM_DB_PRIORITY_SHIFT_V2		48

#define QM_MEM_START_INIT		0x100040
#define QM_MEM_INIT_DONE		0x100044
#define QM_VFT_CFG_RDY			0x10006c
#define QM_VFT_CFG_OP_WR		0x100058
#define QM_VFT_CFG_TYPE			0x10005c
#define QM_SQC_VFT			0x0
#define QM_CQC_VFT			0x1
#define QM_VFT_CFG			0x100060
#define QM_VFT_CFG_OP_ENABLE		0x100054
#define QM_PM_CTRL			0x100148
#define QM_IDLE_DISABLE			BIT(9)

#define QM_VFT_CFG_DATA_L		0x100064
#define QM_VFT_CFG_DATA_H		0x100068
#define QM_SQC_VFT_BUF_SIZE		(7ULL << 8)
#define QM_SQC_VFT_SQC_SIZE		(5ULL << 12)
#define QM_SQC_VFT_INDEX_NUMBER		(1ULL << 16)
#define QM_SQC_VFT_START_SQN_SHIFT	28
#define QM_SQC_VFT_VALID		(1ULL << 44)
#define QM_SQC_VFT_SQN_SHIFT		45
#define QM_CQC_VFT_BUF_SIZE		(7ULL << 8)
#define QM_CQC_VFT_SQC_SIZE		(5ULL << 12)
#define QM_CQC_VFT_INDEX_NUMBER		(1ULL << 16)
#define QM_CQC_VFT_VALID		(1ULL << 28)

#define QM_SQC_VFT_BASE_SHIFT_V2	28
#define QM_SQC_VFT_BASE_MASK_V2		GENMASK(15, 0)
#define QM_SQC_VFT_NUM_SHIFT_V2		45
#define QM_SQC_VFT_NUM_MASK_v2		GENMASK(9, 0)

#define QM_DFX_CNT_CLR_CE		0x100118

#define QM_ABNORMAL_INT_SOURCE		0x100000
#define QM_ABNORMAL_INT_SOURCE_CLR	GENMASK(14, 0)
#define QM_ABNORMAL_INT_MASK		0x100004
#define QM_ABNORMAL_INT_MASK_VALUE	0x7fff
#define QM_ABNORMAL_INT_STATUS		0x100008
#define QM_ABNORMAL_INT_SET		0x10000c
#define QM_ABNORMAL_INF00		0x100010
#define QM_FIFO_OVERFLOW_TYPE		0xc0
#define QM_FIFO_OVERFLOW_TYPE_SHIFT	6
#define QM_FIFO_OVERFLOW_VF		0x3f
#define QM_ABNORMAL_INF01		0x100014
#define QM_DB_TIMEOUT_TYPE		0xc0
#define QM_DB_TIMEOUT_TYPE_SHIFT	6
#define QM_DB_TIMEOUT_VF		0x3f
#define QM_RAS_CE_ENABLE		0x1000ec
#define QM_RAS_FE_ENABLE		0x1000f0
#define QM_RAS_NFE_ENABLE		0x1000f4
#define QM_RAS_CE_THRESHOLD		0x1000f8
#define QM_RAS_CE_TIMES_PER_IRQ		1
#define QM_RAS_MSI_INT_SEL		0x1040f4
#define QM_OOO_SHUTDOWN_SEL		0x1040f8

#define QM_RESET_WAIT_TIMEOUT		400
#define QM_PEH_VENDOR_ID		0x1000d8
#define ACC_VENDOR_ID_VALUE		0x5a5a
#define QM_PEH_DFX_INFO0		0x1000fc
#define QM_PEH_DFX_INFO1		0x100100
#define QM_PEH_DFX_MASK			(BIT(0) | BIT(2))
#define QM_PEH_MSI_FINISH_MASK		GENMASK(19, 16)
#define ACC_PEH_SRIOV_CTRL_VF_MSE_SHIFT	3
#define ACC_PEH_MSI_DISABLE		GENMASK(31, 0)
#define ACC_MASTER_GLOBAL_CTRL_SHUTDOWN	0x1
#define ACC_MASTER_TRANS_RETURN_RW	3
#define ACC_MASTER_TRANS_RETURN		0x300150
#define ACC_MASTER_GLOBAL_CTRL		0x300000
#define ACC_AM_CFG_PORT_WR_EN		0x30001c
#define QM_RAS_NFE_MBIT_DISABLE		~QM_ECC_MBIT
#define ACC_AM_ROB_ECC_INT_STS		0x300104
#define ACC_ROB_ECC_ERR_MULTPL		BIT(1)
#define QM_MSI_CAP_ENABLE		BIT(16)

/* interfunction communication */
#define QM_IFC_READY_STATUS		0x100128
#define QM_IFC_C_STS_M			0x10012C
#define QM_IFC_INT_SET_P		0x100130
#define QM_IFC_INT_CFG			0x100134
#define QM_IFC_INT_SOURCE_P		0x100138
#define QM_IFC_INT_SOURCE_V		0x0020
#define QM_IFC_INT_MASK			0x0024
#define QM_IFC_INT_STATUS		0x0028
#define QM_IFC_INT_SET_V		0x002C
#define QM_IFC_SEND_ALL_VFS		GENMASK(6, 0)
#define QM_IFC_INT_SOURCE_CLR		GENMASK(63, 0)
#define QM_IFC_INT_SOURCE_MASK		BIT(0)
#define QM_IFC_INT_DISABLE		BIT(0)
#define QM_IFC_INT_STATUS_MASK		BIT(0)
#define QM_IFC_INT_SET_MASK		BIT(0)
#define QM_WAIT_DST_ACK			10
#define QM_MAX_PF_WAIT_COUNT		10
#define QM_MAX_VF_WAIT_COUNT		40
#define QM_VF_RESET_WAIT_US            20000
#define QM_VF_RESET_WAIT_CNT           3000
#define QM_VF_RESET_WAIT_TIMEOUT_US    \
	(QM_VF_RESET_WAIT_US * QM_VF_RESET_WAIT_CNT)

#define QM_DFX_MB_CNT_VF		0x104010
#define QM_DFX_DB_CNT_VF		0x104020
#define QM_DFX_SQE_CNT_VF_SQN		0x104030
#define QM_DFX_CQE_CNT_VF_CQN		0x104040
#define QM_DFX_QN_SHIFT			16
#define CURRENT_FUN_MASK		GENMASK(5, 0)
#define CURRENT_Q_MASK			GENMASK(31, 16)

#define POLL_PERIOD			10
#define POLL_TIMEOUT			1000
#define WAIT_PERIOD_US_MAX		200
#define WAIT_PERIOD_US_MIN		100
#define MAX_WAIT_COUNTS			1000
#define QM_CACHE_WB_START		0x204
#define QM_CACHE_WB_DONE		0x208

#define PCI_BAR_2			2
#define PCI_BAR_4			4
#define QM_SQE_DATA_ALIGN_MASK		GENMASK(6, 0)
#define QMC_ALIGN(sz)			ALIGN(sz, 32)

#define QM_DBG_READ_LEN		256
#define QM_DBG_WRITE_LEN		1024
#define QM_DBG_TMP_BUF_LEN		22
#define QM_PCI_COMMAND_INVALID		~0
#define QM_RESET_STOP_TX_OFFSET		1
#define QM_RESET_STOP_RX_OFFSET		2

#define WAIT_PERIOD			20
#define REMOVE_WAIT_DELAY		10
#define QM_SQE_ADDR_MASK		GENMASK(7, 0)
#define QM_EQ_DEPTH			(1024 * 2)

#define QM_DRIVER_REMOVING		0
#define QM_RST_SCHED			1
#define QM_RESETTING			2
#define QM_QOS_PARAM_NUM		2
#define QM_QOS_VAL_NUM			1
#define QM_QOS_BDF_PARAM_NUM		4
#define QM_QOS_MAX_VAL			1000
#define QM_QOS_RATE			100
#define QM_QOS_EXPAND_RATE		1000
#define QM_SHAPER_CIR_B_MASK		GENMASK(7, 0)
#define QM_SHAPER_CIR_U_MASK		GENMASK(10, 8)
#define QM_SHAPER_CIR_S_MASK		GENMASK(14, 11)
#define QM_SHAPER_FACTOR_CIR_U_SHIFT	8
#define QM_SHAPER_FACTOR_CIR_S_SHIFT	11
#define QM_SHAPER_FACTOR_CBS_B_SHIFT	15
#define QM_SHAPER_FACTOR_CBS_S_SHIFT	19
#define QM_SHAPER_CBS_B			1
#define QM_SHAPER_CBS_S			16
#define QM_SHAPER_VFT_OFFSET		6
#define WAIT_FOR_QOS_VF			100
#define QM_QOS_MIN_ERROR_RATE		5
#define QM_QOS_TYPICAL_NUM		8
#define QM_SHAPER_MIN_CBS_S		8
#define QM_QOS_TICK			0x300U
#define QM_QOS_DIVISOR_CLK		0x1f40U
#define QM_QOS_MAX_CIR_B		200
#define QM_QOS_MIN_CIR_B		100
#define QM_QOS_MAX_CIR_U		6
#define QM_QOS_MAX_CIR_S		11
#define QM_QOS_VAL_MAX_LEN		32

#define QM_AUTOSUSPEND_DELAY		3000

#define QM_MK_CQC_DW3_V1(hop_num, pg_sz, buf_sz, cqe_sz) \
	(((hop_num) << QM_CQ_HOP_NUM_SHIFT)	| \
	((pg_sz) << QM_CQ_PAGE_SIZE_SHIFT)	| \
	((buf_sz) << QM_CQ_BUF_SIZE_SHIFT)	| \
	((cqe_sz) << QM_CQ_CQE_SIZE_SHIFT))

#define QM_MK_CQC_DW3_V2(cqe_sz) \
	((QM_Q_DEPTH - 1) | ((cqe_sz) << QM_CQ_CQE_SIZE_SHIFT))

#define QM_MK_SQC_W13(priority, orders, alg_type) \
	(((priority) << QM_SQ_PRIORITY_SHIFT)	| \
	((orders) << QM_SQ_ORDERS_SHIFT)	| \
	(((alg_type) & QM_SQ_TYPE_MASK) << QM_SQ_TYPE_SHIFT))

#define QM_MK_SQC_DW3_V1(hop_num, pg_sz, buf_sz, sqe_sz) \
	(((hop_num) << QM_SQ_HOP_NUM_SHIFT)	| \
	((pg_sz) << QM_SQ_PAGE_SIZE_SHIFT)	| \
	((buf_sz) << QM_SQ_BUF_SIZE_SHIFT)	| \
	((u32)ilog2(sqe_sz) << QM_SQ_SQE_SIZE_SHIFT))

#define QM_MK_SQC_DW3_V2(sqe_sz) \
	((QM_Q_DEPTH - 1) | ((u32)ilog2(sqe_sz) << QM_SQ_SQE_SIZE_SHIFT))

#define INIT_QC_COMMON(qc, base, pasid) do {			\
	(qc)->head = 0;						\
	(qc)->tail = 0;						\
	(qc)->base_l = cpu_to_le32(lower_32_bits(base));	\
	(qc)->base_h = cpu_to_le32(upper_32_bits(base));	\
	(qc)->dw3 = 0;						\
	(qc)->w8 = 0;						\
	(qc)->rsvd0 = 0;					\
	(qc)->pasid = cpu_to_le16(pasid);			\
	(qc)->w11 = 0;						\
	(qc)->rsvd1 = 0;					\
} while (0)

enum vft_type {
	SQC_VFT = 0,
	CQC_VFT,
	SHAPER_VFT,
};

enum acc_err_result {
	ACC_ERR_NONE,
	ACC_ERR_NEED_RESET,
	ACC_ERR_RECOVERED,
};

enum qm_alg_type {
	ALG_TYPE_0,
	ALG_TYPE_1,
};

enum qm_mb_cmd {
	QM_PF_FLR_PREPARE = 0x01,
	QM_PF_SRST_PREPARE,
	QM_PF_RESET_DONE,
	QM_VF_PREPARE_DONE,
	QM_VF_PREPARE_FAIL,
	QM_VF_START_DONE,
	QM_VF_START_FAIL,
	QM_PF_SET_QOS,
	QM_VF_GET_QOS,
};

struct qm_cqe {
	__le32 rsvd0;
	__le16 cmd_id;
	__le16 rsvd1;
	__le16 sq_head;
	__le16 sq_num;
	__le16 rsvd2;
	__le16 w7;
};

struct qm_eqe {
	__le32 dw0;
};

struct qm_aeqe {
	__le32 dw0;
};

struct qm_sqc {
	__le16 head;
	__le16 tail;
	__le32 base_l;
	__le32 base_h;
	__le32 dw3;
	__le16 w8;
	__le16 rsvd0;
	__le16 pasid;
	__le16 w11;
	__le16 cq_num;
	__le16 w13;
	__le32 rsvd1;
};

struct qm_cqc {
	__le16 head;
	__le16 tail;
	__le32 base_l;
	__le32 base_h;
	__le32 dw3;
	__le16 w8;
	__le16 rsvd0;
	__le16 pasid;
	__le16 w11;
	__le32 dw6;
	__le32 rsvd1;
};

struct qm_eqc {
	__le16 head;
	__le16 tail;
	__le32 base_l;
	__le32 base_h;
	__le32 dw3;
	__le32 rsvd[2];
	__le32 dw6;
};

struct qm_aeqc {
	__le16 head;
	__le16 tail;
	__le32 base_l;
	__le32 base_h;
	__le32 dw3;
	__le32 rsvd[2];
	__le32 dw6;
};

struct qm_mailbox {
	__le16 w0;
	__le16 queue_num;
	__le32 base_l;
	__le32 base_h;
	__le32 rsvd;
};

struct qm_doorbell {
	__le16 queue_num;
	__le16 cmd;
	__le16 index;
	__le16 priority;
};

struct hisi_qm_resource {
	struct hisi_qm *qm;
	int distance;
	struct list_head list;
};

struct hisi_qm_hw_ops {
	int (*get_vft)(struct hisi_qm *qm, u32 *base, u32 *number);
	void (*qm_db)(struct hisi_qm *qm, u16 qn,
		      u8 cmd, u16 index, u8 priority);
	u32 (*get_irq_num)(struct hisi_qm *qm);
	int (*debug_init)(struct hisi_qm *qm);
	void (*hw_error_init)(struct hisi_qm *qm, u32 ce, u32 nfe, u32 fe);
	void (*hw_error_uninit)(struct hisi_qm *qm);
	enum acc_err_result (*hw_error_handle)(struct hisi_qm *qm);
	int (*stop_qp)(struct hisi_qp *qp);
	int (*set_msi)(struct hisi_qm *qm, bool set);
	int (*ping_all_vfs)(struct hisi_qm *qm, u64 cmd);
	int (*ping_pf)(struct hisi_qm *qm, u64 cmd);
};

struct qm_dfx_item {
	const char *name;
	u32 offset;
};

static struct qm_dfx_item qm_dfx_files[] = {
	{"err_irq", offsetof(struct qm_dfx, err_irq_cnt)},
	{"aeq_irq", offsetof(struct qm_dfx, aeq_irq_cnt)},
	{"abnormal_irq", offsetof(struct qm_dfx, abnormal_irq_cnt)},
	{"create_qp_err", offsetof(struct qm_dfx, create_qp_err_cnt)},
	{"mb_err", offsetof(struct qm_dfx, mb_err_cnt)},
};

static const char * const qm_debug_file_name[] = {
	[CURRENT_QM]   = "current_qm",
	[CURRENT_Q]    = "current_q",
	[CLEAR_ENABLE] = "clear_enable",
};

struct hisi_qm_hw_error {
	u32 int_msk;
	const char *msg;
};

static const struct hisi_qm_hw_error qm_hw_error[] = {
	{ .int_msk = BIT(0), .msg = "qm_axi_rresp" },
	{ .int_msk = BIT(1), .msg = "qm_axi_bresp" },
	{ .int_msk = BIT(2), .msg = "qm_ecc_mbit" },
	{ .int_msk = BIT(3), .msg = "qm_ecc_1bit" },
	{ .int_msk = BIT(4), .msg = "qm_acc_get_task_timeout" },
	{ .int_msk = BIT(5), .msg = "qm_acc_do_task_timeout" },
	{ .int_msk = BIT(6), .msg = "qm_acc_wb_not_ready_timeout" },
	{ .int_msk = BIT(7), .msg = "qm_sq_cq_vf_invalid" },
	{ .int_msk = BIT(8), .msg = "qm_cq_vf_invalid" },
	{ .int_msk = BIT(9), .msg = "qm_sq_vf_invalid" },
	{ .int_msk = BIT(10), .msg = "qm_db_timeout" },
	{ .int_msk = BIT(11), .msg = "qm_of_fifo_of" },
	{ .int_msk = BIT(12), .msg = "qm_db_random_invalid" },
	{ .int_msk = BIT(13), .msg = "qm_mailbox_timeout" },
	{ .int_msk = BIT(14), .msg = "qm_flr_timeout" },
	{ /* sentinel */ }
};

static const char * const qm_db_timeout[] = {
	"sq", "cq", "eq", "aeq",
};

static const char * const qm_fifo_overflow[] = {
	"cq", "eq", "aeq",
};

static const char * const qm_s[] = {
	"init", "start", "close", "stop",
};

static const char * const qp_s[] = {
	"none", "init", "start", "stop", "close",
};

struct qm_typical_qos_table {
	u32 start;
	u32 end;
	u32 val;
};

/* the qos step is 100 */
static struct qm_typical_qos_table shaper_cir_s[] = {
	{100, 100, 4},
	{200, 200, 3},
	{300, 500, 2},
	{600, 1000, 1},
	{1100, 100000, 0},
};

static struct qm_typical_qos_table shaper_cbs_s[] = {
	{100, 200, 9},
	{300, 500, 11},
	{600, 1000, 12},
	{1100, 10000, 16},
	{10100, 25000, 17},
	{25100, 50000, 18},
	{50100, 100000, 19}
};

static bool qm_avail_state(struct hisi_qm *qm, enum qm_state new)
{
	enum qm_state curr = atomic_read(&qm->status.flags);
	bool avail = false;

	switch (curr) {
	case QM_INIT:
		if (new == QM_START || new == QM_CLOSE)
			avail = true;
		break;
	case QM_START:
		if (new == QM_STOP)
			avail = true;
		break;
	case QM_STOP:
		if (new == QM_CLOSE || new == QM_START)
			avail = true;
		break;
	default:
		break;
	}

	dev_dbg(&qm->pdev->dev, "change qm state from %s to %s\n",
		qm_s[curr], qm_s[new]);

	if (!avail)
		dev_warn(&qm->pdev->dev, "Can not change qm state from %s to %s\n",
			 qm_s[curr], qm_s[new]);

	return avail;
}

static bool qm_qp_avail_state(struct hisi_qm *qm, struct hisi_qp *qp,
			      enum qp_state new)
{
	enum qm_state qm_curr = atomic_read(&qm->status.flags);
	enum qp_state qp_curr = 0;
	bool avail = false;

	if (qp)
		qp_curr = atomic_read(&qp->qp_status.flags);

	switch (new) {
	case QP_INIT:
		if (qm_curr == QM_START || qm_curr == QM_INIT)
			avail = true;
		break;
	case QP_START:
		if ((qm_curr == QM_START && qp_curr == QP_INIT) ||
		    (qm_curr == QM_START && qp_curr == QP_STOP))
			avail = true;
		break;
	case QP_STOP:
		if ((qm_curr == QM_START && qp_curr == QP_START) ||
		    (qp_curr == QP_INIT))
			avail = true;
		break;
	case QP_CLOSE:
		if ((qm_curr == QM_START && qp_curr == QP_INIT) ||
		    (qm_curr == QM_START && qp_curr == QP_STOP) ||
		    (qm_curr == QM_STOP && qp_curr == QP_STOP)  ||
		    (qm_curr == QM_STOP && qp_curr == QP_INIT))
			avail = true;
		break;
	default:
		break;
	}

	dev_dbg(&qm->pdev->dev, "change qp state from %s to %s in QM %s\n",
		qp_s[qp_curr], qp_s[new], qm_s[qm_curr]);

	if (!avail)
		dev_warn(&qm->pdev->dev,
			 "Can not change qp state from %s to %s in QM %s\n",
			 qp_s[qp_curr], qp_s[new], qm_s[qm_curr]);

	return avail;
}

static u32 qm_get_hw_error_status(struct hisi_qm *qm)
{
	return readl(qm->io_base + QM_ABNORMAL_INT_STATUS);
}

static u32 qm_get_dev_err_status(struct hisi_qm *qm)
{
	return qm->err_ini->get_dev_hw_err_status(qm);
}

/* Check if the error causes the master ooo block */
static int qm_check_dev_error(struct hisi_qm *qm)
{
	u32 val, dev_val;

	if (qm->fun_type == QM_HW_VF)
		return 0;

	val = qm_get_hw_error_status(qm);
	dev_val = qm_get_dev_err_status(qm);

	if (qm->ver < QM_HW_V3)
		return (val & QM_ECC_MBIT) ||
		       (dev_val & qm->err_info.ecc_2bits_mask);

	return (val & readl(qm->io_base + QM_OOO_SHUTDOWN_SEL)) ||
	       (dev_val & (~qm->err_info.dev_ce_mask));
}

static int qm_wait_reset_finish(struct hisi_qm *qm)
{
	int delay = 0;

	/* All reset requests need to be queued for processing */
	while (test_and_set_bit(QM_RESETTING, &qm->misc_ctl)) {
		msleep(++delay);
		if (delay > QM_RESET_WAIT_TIMEOUT)
			return -EBUSY;
	}

	return 0;
}

static int qm_reset_prepare_ready(struct hisi_qm *qm)
{
	struct pci_dev *pdev = qm->pdev;
	struct hisi_qm *pf_qm = pci_get_drvdata(pci_physfn(pdev));

	/*
	 * PF and VF on host doesnot support resetting at the
	 * same time on Kunpeng920.
	 */
	if (qm->ver < QM_HW_V3)
		return qm_wait_reset_finish(pf_qm);

	return qm_wait_reset_finish(qm);
}

static void qm_reset_bit_clear(struct hisi_qm *qm)
{
	struct pci_dev *pdev = qm->pdev;
	struct hisi_qm *pf_qm = pci_get_drvdata(pci_physfn(pdev));

	if (qm->ver < QM_HW_V3)
		clear_bit(QM_RESETTING, &pf_qm->misc_ctl);

	clear_bit(QM_RESETTING, &qm->misc_ctl);
}

static void qm_mb_pre_init(struct qm_mailbox *mailbox, u8 cmd,
			   u64 base, u16 queue, bool op)
{
	mailbox->w0 = cpu_to_le16((cmd) |
		((op) ? 0x1 << QM_MB_OP_SHIFT : 0) |
		(0x1 << QM_MB_BUSY_SHIFT));
	mailbox->queue_num = cpu_to_le16(queue);
	mailbox->base_l = cpu_to_le32(lower_32_bits(base));
	mailbox->base_h = cpu_to_le32(upper_32_bits(base));
	mailbox->rsvd = 0;
}

/* return 0 mailbox ready, -ETIMEDOUT hardware timeout */
static int qm_wait_mb_ready(struct hisi_qm *qm)
{
	u32 val;

	return readl_relaxed_poll_timeout(qm->io_base + QM_MB_CMD_SEND_BASE,
					  val, !((val >> QM_MB_BUSY_SHIFT) &
					  0x1), POLL_PERIOD, POLL_TIMEOUT);
}

/* 128 bit should be written to hardware at one time to trigger a mailbox */
static void qm_mb_write(struct hisi_qm *qm, const void *src)
{
	void __iomem *fun_base = qm->io_base + QM_MB_CMD_SEND_BASE;
	unsigned long tmp0 = 0, tmp1 = 0;

	if (!IS_ENABLED(CONFIG_ARM64)) {
		memcpy_toio(fun_base, src, 16);
		wmb();
		return;
	}

	asm volatile("ldp %0, %1, %3\n"
		     "stp %0, %1, %2\n"
		     "dsb sy\n"
		     : "=&r" (tmp0),
		       "=&r" (tmp1),
		       "+Q" (*((char __iomem *)fun_base))
		     : "Q" (*((char *)src))
		     : "memory");
}

static int qm_mb_nolock(struct hisi_qm *qm, struct qm_mailbox *mailbox)
{
	if (unlikely(qm_wait_mb_ready(qm))) {
		dev_err(&qm->pdev->dev, "QM mailbox is busy to start!\n");
		goto mb_busy;
	}

	qm_mb_write(qm, mailbox);

	if (unlikely(qm_wait_mb_ready(qm))) {
		dev_err(&qm->pdev->dev, "QM mailbox operation timeout!\n");
		goto mb_busy;
	}

	return 0;

mb_busy:
	atomic64_inc(&qm->debug.dfx.mb_err_cnt);
	return -EBUSY;
}

static int qm_mb(struct hisi_qm *qm, u8 cmd, dma_addr_t dma_addr, u16 queue,
		 bool op)
{
	struct qm_mailbox mailbox;
	int ret;

	dev_dbg(&qm->pdev->dev, "QM mailbox request to q%u: %u-%llx\n",
		queue, cmd, (unsigned long long)dma_addr);

	qm_mb_pre_init(&mailbox, cmd, dma_addr, queue, op);

	mutex_lock(&qm->mailbox_lock);
	ret = qm_mb_nolock(qm, &mailbox);
	mutex_unlock(&qm->mailbox_lock);

	return ret;
}

static void qm_db_v1(struct hisi_qm *qm, u16 qn, u8 cmd, u16 index, u8 priority)
{
	u64 doorbell;

	doorbell = qn | ((u64)cmd << QM_DB_CMD_SHIFT_V1) |
		   ((u64)index << QM_DB_INDEX_SHIFT_V1)  |
		   ((u64)priority << QM_DB_PRIORITY_SHIFT_V1);

	writeq(doorbell, qm->io_base + QM_DOORBELL_BASE_V1);
}

static void qm_db_v2(struct hisi_qm *qm, u16 qn, u8 cmd, u16 index, u8 priority)
{
	void __iomem *io_base = qm->io_base;
	u16 randata = 0;
	u64 doorbell;

	if (cmd == QM_DOORBELL_CMD_SQ || cmd == QM_DOORBELL_CMD_CQ)
		io_base = qm->db_io_base + (u64)qn * qm->db_interval +
			  QM_DOORBELL_SQ_CQ_BASE_V2;
	else
		io_base += QM_DOORBELL_EQ_AEQ_BASE_V2;

	doorbell = qn | ((u64)cmd << QM_DB_CMD_SHIFT_V2) |
		   ((u64)randata << QM_DB_RAND_SHIFT_V2) |
		   ((u64)index << QM_DB_INDEX_SHIFT_V2)	 |
		   ((u64)priority << QM_DB_PRIORITY_SHIFT_V2);

	writeq(doorbell, io_base);
}

static void qm_db(struct hisi_qm *qm, u16 qn, u8 cmd, u16 index, u8 priority)
{
	dev_dbg(&qm->pdev->dev, "QM doorbell request: qn=%u, cmd=%u, index=%u\n",
		qn, cmd, index);

	qm->ops->qm_db(qm, qn, cmd, index, priority);
}

static void qm_disable_clock_gate(struct hisi_qm *qm)
{
	u32 val;

	/* if qm enables clock gating in Kunpeng930, qos will be inaccurate. */
	if (qm->ver < QM_HW_V3)
		return;

	val = readl(qm->io_base + QM_PM_CTRL);
	val |= QM_IDLE_DISABLE;
	writel(val, qm->io_base +  QM_PM_CTRL);
}

static int qm_dev_mem_reset(struct hisi_qm *qm)
{
	u32 val;

	writel(0x1, qm->io_base + QM_MEM_START_INIT);
	return readl_relaxed_poll_timeout(qm->io_base + QM_MEM_INIT_DONE, val,
					  val & BIT(0), POLL_PERIOD,
					  POLL_TIMEOUT);
}

static u32 qm_get_irq_num_v1(struct hisi_qm *qm)
{
	return QM_IRQ_NUM_V1;
}

static u32 qm_get_irq_num_v2(struct hisi_qm *qm)
{
	if (qm->fun_type == QM_HW_PF)
		return QM_IRQ_NUM_PF_V2;
	else
		return QM_IRQ_NUM_VF_V2;
}

static u32 qm_get_irq_num_v3(struct hisi_qm *qm)
{
	if (qm->fun_type == QM_HW_PF)
		return QM_IRQ_NUM_PF_V2;

	return QM_IRQ_NUM_VF_V3;
}

static int qm_pm_get_sync(struct hisi_qm *qm)
{
	struct device *dev = &qm->pdev->dev;
	int ret;

	if (qm->fun_type == QM_HW_VF || qm->ver < QM_HW_V3)
		return 0;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0) {
		dev_err(dev, "failed to get_sync(%d).\n", ret);
		return ret;
	}

	return 0;
}

static void qm_pm_put_sync(struct hisi_qm *qm)
{
	struct device *dev = &qm->pdev->dev;

	if (qm->fun_type == QM_HW_VF || qm->ver < QM_HW_V3)
		return;

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
}

static struct hisi_qp *qm_to_hisi_qp(struct hisi_qm *qm, struct qm_eqe *eqe)
{
	u16 cqn = le32_to_cpu(eqe->dw0) & QM_EQE_CQN_MASK;

	return &qm->qp_array[cqn];
}

static void qm_cq_head_update(struct hisi_qp *qp)
{
	if (qp->qp_status.cq_head == QM_Q_DEPTH - 1) {
		qp->qp_status.cqc_phase = !qp->qp_status.cqc_phase;
		qp->qp_status.cq_head = 0;
	} else {
		qp->qp_status.cq_head++;
	}
}

static void qm_poll_qp(struct hisi_qp *qp, struct hisi_qm *qm)
{
	if (unlikely(atomic_read(&qp->qp_status.flags) == QP_STOP))
		return;

	if (qp->event_cb) {
		qp->event_cb(qp);
		return;
	}

	if (qp->req_cb) {
		struct qm_cqe *cqe = qp->cqe + qp->qp_status.cq_head;

		while (QM_CQE_PHASE(cqe) == qp->qp_status.cqc_phase) {
			dma_rmb();
			qp->req_cb(qp, qp->sqe + qm->sqe_size *
				   le16_to_cpu(cqe->sq_head));
			qm_cq_head_update(qp);
			cqe = qp->cqe + qp->qp_status.cq_head;
			qm_db(qm, qp->qp_id, QM_DOORBELL_CMD_CQ,
			      qp->qp_status.cq_head, 0);
			atomic_dec(&qp->qp_status.used);
		}

		/* set c_flag */
		qm_db(qm, qp->qp_id, QM_DOORBELL_CMD_CQ,
		      qp->qp_status.cq_head, 1);
	}
}

static void qm_work_process(struct work_struct *work)
{
	struct hisi_qm *qm = container_of(work, struct hisi_qm, work);
	struct qm_eqe *eqe = qm->eqe + qm->status.eq_head;
	struct hisi_qp *qp;
	int eqe_num = 0;

	while (QM_EQE_PHASE(eqe) == qm->status.eqc_phase) {
		eqe_num++;
		qp = qm_to_hisi_qp(qm, eqe);
		qm_poll_qp(qp, qm);

		if (qm->status.eq_head == QM_EQ_DEPTH - 1) {
			qm->status.eqc_phase = !qm->status.eqc_phase;
			eqe = qm->eqe;
			qm->status.eq_head = 0;
		} else {
			eqe++;
			qm->status.eq_head++;
		}

		if (eqe_num == QM_EQ_DEPTH / 2 - 1) {
			eqe_num = 0;
			qm_db(qm, 0, QM_DOORBELL_CMD_EQ, qm->status.eq_head, 0);
		}
	}

	qm_db(qm, 0, QM_DOORBELL_CMD_EQ, qm->status.eq_head, 0);
}

static irqreturn_t do_qm_irq(int irq, void *data)
{
	struct hisi_qm *qm = (struct hisi_qm *)data;

	/* the workqueue created by device driver of QM */
	if (qm->wq)
		queue_work(qm->wq, &qm->work);
	else
		schedule_work(&qm->work);

	return IRQ_HANDLED;
}

static irqreturn_t qm_irq(int irq, void *data)
{
	struct hisi_qm *qm = data;

	if (readl(qm->io_base + QM_VF_EQ_INT_SOURCE))
		return do_qm_irq(irq, data);

	atomic64_inc(&qm->debug.dfx.err_irq_cnt);
	dev_err(&qm->pdev->dev, "invalid int source\n");
	qm_db(qm, 0, QM_DOORBELL_CMD_EQ, qm->status.eq_head, 0);

	return IRQ_NONE;
}

static irqreturn_t qm_mb_cmd_irq(int irq, void *data)
{
	struct hisi_qm *qm = data;
	u32 val;

	val = readl(qm->io_base + QM_IFC_INT_STATUS);
	val &= QM_IFC_INT_STATUS_MASK;
	if (!val)
		return IRQ_NONE;

	schedule_work(&qm->cmd_process);

	return IRQ_HANDLED;
}

static void qm_set_qp_disable(struct hisi_qp *qp, int offset)
{
	u32 *addr;

	if (qp->is_in_kernel)
		return;

	addr = (u32 *)(qp->qdma.va + qp->qdma.size) - offset;
	*addr = 1;

	/* make sure setup is completed */
	mb();
}

static void qm_disable_qp(struct hisi_qm *qm, u32 qp_id)
{
	struct hisi_qp *qp = &qm->qp_array[qp_id];

	qm_set_qp_disable(qp, QM_RESET_STOP_TX_OFFSET);
	hisi_qm_stop_qp(qp);
	qm_set_qp_disable(qp, QM_RESET_STOP_RX_OFFSET);
}

static void qm_reset_function(struct hisi_qm *qm)
{
	struct hisi_qm *pf_qm = pci_get_drvdata(pci_physfn(qm->pdev));
	struct device *dev = &qm->pdev->dev;
	int ret;

	if (qm_check_dev_error(pf_qm))
		return;

	ret = qm_reset_prepare_ready(qm);
	if (ret) {
		dev_err(dev, "reset function not ready\n");
		return;
	}

	ret = hisi_qm_stop(qm, QM_FLR);
	if (ret) {
		dev_err(dev, "failed to stop qm when reset function\n");
		goto clear_bit;
	}

	ret = hisi_qm_start(qm);
	if (ret)
		dev_err(dev, "failed to start qm when reset function\n");

clear_bit:
	qm_reset_bit_clear(qm);
}

static irqreturn_t qm_aeq_thread(int irq, void *data)
{
	struct hisi_qm *qm = data;
	struct qm_aeqe *aeqe = qm->aeqe + qm->status.aeq_head;
	u32 type, qp_id;

	while (QM_AEQE_PHASE(aeqe) == qm->status.aeqc_phase) {
		type = le32_to_cpu(aeqe->dw0) >> QM_AEQE_TYPE_SHIFT;
		qp_id = le32_to_cpu(aeqe->dw0) & QM_AEQE_CQN_MASK;

		switch (type) {
		case QM_EQ_OVERFLOW:
			dev_err(&qm->pdev->dev, "eq overflow, reset function\n");
			qm_reset_function(qm);
			return IRQ_HANDLED;
		case QM_CQ_OVERFLOW:
			dev_err(&qm->pdev->dev, "cq overflow, stop qp(%u)\n",
				qp_id);
			fallthrough;
		case QM_CQE_ERROR:
			qm_disable_qp(qm, qp_id);
			break;
		default:
			dev_err(&qm->pdev->dev, "unknown error type %u\n",
				type);
			break;
		}

		if (qm->status.aeq_head == QM_Q_DEPTH - 1) {
			qm->status.aeqc_phase = !qm->status.aeqc_phase;
			aeqe = qm->aeqe;
			qm->status.aeq_head = 0;
		} else {
			aeqe++;
			qm->status.aeq_head++;
		}
	}

	qm_db(qm, 0, QM_DOORBELL_CMD_AEQ, qm->status.aeq_head, 0);

	return IRQ_HANDLED;
}

static irqreturn_t qm_aeq_irq(int irq, void *data)
{
	struct hisi_qm *qm = data;

	atomic64_inc(&qm->debug.dfx.aeq_irq_cnt);
	if (!readl(qm->io_base + QM_VF_AEQ_INT_SOURCE))
		return IRQ_NONE;

	return IRQ_WAKE_THREAD;
}

static void qm_irq_unregister(struct hisi_qm *qm)
{
	struct pci_dev *pdev = qm->pdev;

	free_irq(pci_irq_vector(pdev, QM_EQ_EVENT_IRQ_VECTOR), qm);

	if (qm->ver > QM_HW_V1) {
		free_irq(pci_irq_vector(pdev, QM_AEQ_EVENT_IRQ_VECTOR), qm);

		if (qm->fun_type == QM_HW_PF)
			free_irq(pci_irq_vector(pdev,
				 QM_ABNORMAL_EVENT_IRQ_VECTOR), qm);
	}

	if (qm->ver > QM_HW_V2)
		free_irq(pci_irq_vector(pdev, QM_CMD_EVENT_IRQ_VECTOR), qm);
}

static void qm_init_qp_status(struct hisi_qp *qp)
{
	struct hisi_qp_status *qp_status = &qp->qp_status;

	qp_status->sq_tail = 0;
	qp_status->cq_head = 0;
	qp_status->cqc_phase = true;
	atomic_set(&qp_status->used, 0);
}

static void qm_init_prefetch(struct hisi_qm *qm)
{
	struct device *dev = &qm->pdev->dev;
	u32 page_type = 0x0;

	if (qm->ver < QM_HW_V3)
		return;

	switch (PAGE_SIZE) {
	case SZ_4K:
		page_type = 0x0;
		break;
	case SZ_16K:
		page_type = 0x1;
		break;
	case SZ_64K:
		page_type = 0x2;
		break;
	default:
		dev_err(dev, "system page size is not support: %lu, default set to 4KB",
			PAGE_SIZE);
	}

	writel(page_type, qm->io_base + QM_PAGE_SIZE);
}

/*
 * acc_shaper_para_calc() Get the IR value by the qos formula, the return value
 * is the expected qos calculated.
 * the formula:
 * IR = X Mbps if ir = 1 means IR = 100 Mbps, if ir = 10000 means = 10Gbps
 *
 *		IR_b * (2 ^ IR_u) * 8000
 * IR(Mbps) = -------------------------
 *		  Tick * (2 ^ IR_s)
 */
static u32 acc_shaper_para_calc(u64 cir_b, u64 cir_u, u64 cir_s)
{
	return ((cir_b * QM_QOS_DIVISOR_CLK) * (1 << cir_u)) /
					(QM_QOS_TICK * (1 << cir_s));
}

static u32 acc_shaper_calc_cbs_s(u32 ir)
{
	int table_size = ARRAY_SIZE(shaper_cbs_s);
	int i;

	for (i = 0; i < table_size; i++) {
		if (ir >= shaper_cbs_s[i].start && ir <= shaper_cbs_s[i].end)
			return shaper_cbs_s[i].val;
	}

	return QM_SHAPER_MIN_CBS_S;
}

static u32 acc_shaper_calc_cir_s(u32 ir)
{
	int table_size = ARRAY_SIZE(shaper_cir_s);
	int i;

	for (i = 0; i < table_size; i++) {
		if (ir >= shaper_cir_s[i].start && ir <= shaper_cir_s[i].end)
			return shaper_cir_s[i].val;
	}

	return 0;
}

static int qm_get_shaper_para(u32 ir, struct qm_shaper_factor *factor)
{
	u32 cir_b, cir_u, cir_s, ir_calc;
	u32 error_rate;

	factor->cbs_s = acc_shaper_calc_cbs_s(ir);
	cir_s = acc_shaper_calc_cir_s(ir);

	for (cir_b = QM_QOS_MIN_CIR_B; cir_b <= QM_QOS_MAX_CIR_B; cir_b++) {
		for (cir_u = 0; cir_u <= QM_QOS_MAX_CIR_U; cir_u++) {
			ir_calc = acc_shaper_para_calc(cir_b, cir_u, cir_s);

			error_rate = QM_QOS_EXPAND_RATE * (u32)abs(ir_calc - ir) / ir;
			if (error_rate <= QM_QOS_MIN_ERROR_RATE) {
				factor->cir_b = cir_b;
				factor->cir_u = cir_u;
				factor->cir_s = cir_s;
				return 0;
			}
		}
	}

	return -EINVAL;
}

static void qm_vft_data_cfg(struct hisi_qm *qm, enum vft_type type, u32 base,
			    u32 number, struct qm_shaper_factor *factor)
{
	u64 tmp = 0;

	if (number > 0) {
		switch (type) {
		case SQC_VFT:
			if (qm->ver == QM_HW_V1) {
				tmp = QM_SQC_VFT_BUF_SIZE	|
				      QM_SQC_VFT_SQC_SIZE	|
				      QM_SQC_VFT_INDEX_NUMBER	|
				      QM_SQC_VFT_VALID		|
				      (u64)base << QM_SQC_VFT_START_SQN_SHIFT;
			} else {
				tmp = (u64)base << QM_SQC_VFT_START_SQN_SHIFT |
				      QM_SQC_VFT_VALID |
				      (u64)(number - 1) << QM_SQC_VFT_SQN_SHIFT;
			}
			break;
		case CQC_VFT:
			if (qm->ver == QM_HW_V1) {
				tmp = QM_CQC_VFT_BUF_SIZE	|
				      QM_CQC_VFT_SQC_SIZE	|
				      QM_CQC_VFT_INDEX_NUMBER	|
				      QM_CQC_VFT_VALID;
			} else {
				tmp = QM_CQC_VFT_VALID;
			}
			break;
		case SHAPER_VFT:
			if (qm->ver >= QM_HW_V3) {
				tmp = factor->cir_b |
				(factor->cir_u << QM_SHAPER_FACTOR_CIR_U_SHIFT) |
				(factor->cir_s << QM_SHAPER_FACTOR_CIR_S_SHIFT) |
				(QM_SHAPER_CBS_B << QM_SHAPER_FACTOR_CBS_B_SHIFT) |
				(factor->cbs_s << QM_SHAPER_FACTOR_CBS_S_SHIFT);
			}
			break;
		}
	}

	writel(lower_32_bits(tmp), qm->io_base + QM_VFT_CFG_DATA_L);
	writel(upper_32_bits(tmp), qm->io_base + QM_VFT_CFG_DATA_H);
}

static int qm_set_vft_common(struct hisi_qm *qm, enum vft_type type,
			     u32 fun_num, u32 base, u32 number)
{
	struct qm_shaper_factor *factor = &qm->factor[fun_num];
	unsigned int val;
	int ret;

	ret = readl_relaxed_poll_timeout(qm->io_base + QM_VFT_CFG_RDY, val,
					 val & BIT(0), POLL_PERIOD,
					 POLL_TIMEOUT);
	if (ret)
		return ret;

	writel(0x0, qm->io_base + QM_VFT_CFG_OP_WR);
	writel(type, qm->io_base + QM_VFT_CFG_TYPE);
	if (type == SHAPER_VFT)
		fun_num |= base << QM_SHAPER_VFT_OFFSET;

	writel(fun_num, qm->io_base + QM_VFT_CFG);

	qm_vft_data_cfg(qm, type, base, number, factor);

	writel(0x0, qm->io_base + QM_VFT_CFG_RDY);
	writel(0x1, qm->io_base + QM_VFT_CFG_OP_ENABLE);

	return readl_relaxed_poll_timeout(qm->io_base + QM_VFT_CFG_RDY, val,
					  val & BIT(0), POLL_PERIOD,
					  POLL_TIMEOUT);
}

static int qm_shaper_init_vft(struct hisi_qm *qm, u32 fun_num)
{
	u32 qos = qm->factor[fun_num].func_qos;
	int ret, i;

	ret = qm_get_shaper_para(qos * QM_QOS_RATE, &qm->factor[fun_num]);
	if (ret) {
		dev_err(&qm->pdev->dev, "failed to calculate shaper parameter!\n");
		return ret;
	}
	writel(qm->type_rate, qm->io_base + QM_SHAPER_CFG);
	for (i = ALG_TYPE_0; i <= ALG_TYPE_1; i++) {
		/* The base number of queue reuse for different alg type */
		ret = qm_set_vft_common(qm, SHAPER_VFT, fun_num, i, 1);
		if (ret)
			return ret;
	}

	return 0;
}

/* The config should be conducted after qm_dev_mem_reset() */
static int qm_set_sqc_cqc_vft(struct hisi_qm *qm, u32 fun_num, u32 base,
			      u32 number)
{
	int ret, i;

	for (i = SQC_VFT; i <= CQC_VFT; i++) {
		ret = qm_set_vft_common(qm, i, fun_num, base, number);
		if (ret)
			return ret;
	}

	/* init default shaper qos val */
	if (qm->ver >= QM_HW_V3) {
		ret = qm_shaper_init_vft(qm, fun_num);
		if (ret)
			goto back_sqc_cqc;
	}

	return 0;
back_sqc_cqc:
	for (i = SQC_VFT; i <= CQC_VFT; i++) {
		ret = qm_set_vft_common(qm, i, fun_num, 0, 0);
		if (ret)
			return ret;
	}
	return ret;
}

static int qm_get_vft_v2(struct hisi_qm *qm, u32 *base, u32 *number)
{
	u64 sqc_vft;
	int ret;

	ret = qm_mb(qm, QM_MB_CMD_SQC_VFT_V2, 0, 0, 1);
	if (ret)
		return ret;

	sqc_vft = readl(qm->io_base + QM_MB_CMD_DATA_ADDR_L) |
		  ((u64)readl(qm->io_base + QM_MB_CMD_DATA_ADDR_H) << 32);
	*base = QM_SQC_VFT_BASE_MASK_V2 & (sqc_vft >> QM_SQC_VFT_BASE_SHIFT_V2);
	*number = (QM_SQC_VFT_NUM_MASK_v2 &
		   (sqc_vft >> QM_SQC_VFT_NUM_SHIFT_V2)) + 1;

	return 0;
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

static struct hisi_qm *file_to_qm(struct debugfs_file *file)
{
	struct qm_debug *debug = file->debug;

	return container_of(debug, struct hisi_qm, debug);
}

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

#define CNT_CYC_REGS_NUM		10
static const struct debugfs_reg32 qm_dfx_regs[] = {
	/* XXX_CNT are reading clear register */
	{"QM_ECC_1BIT_CNT               ",  0x104000ull},
	{"QM_ECC_MBIT_CNT               ",  0x104008ull},
	{"QM_DFX_MB_CNT                 ",  0x104018ull},
	{"QM_DFX_DB_CNT                 ",  0x104028ull},
	{"QM_DFX_SQE_CNT                ",  0x104038ull},
	{"QM_DFX_CQE_CNT                ",  0x104048ull},
	{"QM_DFX_SEND_SQE_TO_ACC_CNT    ",  0x104050ull},
	{"QM_DFX_WB_SQE_FROM_ACC_CNT    ",  0x104058ull},
	{"QM_DFX_ACC_FINISH_CNT         ",  0x104060ull},
	{"QM_DFX_CQE_ERR_CNT            ",  0x1040b4ull},
	{"QM_DFX_FUNS_ACTIVE_ST         ",  0x200ull},
	{"QM_ECC_1BIT_INF               ",  0x104004ull},
	{"QM_ECC_MBIT_INF               ",  0x10400cull},
	{"QM_DFX_ACC_RDY_VLD0           ",  0x1040a0ull},
	{"QM_DFX_ACC_RDY_VLD1           ",  0x1040a4ull},
	{"QM_DFX_AXI_RDY_VLD            ",  0x1040a8ull},
	{"QM_DFX_FF_ST0                 ",  0x1040c8ull},
	{"QM_DFX_FF_ST1                 ",  0x1040ccull},
	{"QM_DFX_FF_ST2                 ",  0x1040d0ull},
	{"QM_DFX_FF_ST3                 ",  0x1040d4ull},
	{"QM_DFX_FF_ST4                 ",  0x1040d8ull},
	{"QM_DFX_FF_ST5                 ",  0x1040dcull},
	{"QM_DFX_FF_ST6                 ",  0x1040e0ull},
	{"QM_IN_IDLE_ST                 ",  0x1040e4ull},
};

static const struct debugfs_reg32 qm_vf_dfx_regs[] = {
	{"QM_DFX_FUNS_ACTIVE_ST         ",  0x200ull},
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

static ssize_t qm_cmd_read(struct file *filp, char __user *buffer,
			   size_t count, loff_t *pos)
{
	char buf[QM_DBG_READ_LEN];
	int len;

	len = scnprintf(buf, QM_DBG_READ_LEN, "%s\n",
			"Please echo help to cmd to get help information");

	return simple_read_from_buffer(buffer, count, pos, buf, len);
}

static void *qm_ctx_alloc(struct hisi_qm *qm, size_t ctx_size,
			  dma_addr_t *dma_addr)
{
	struct device *dev = &qm->pdev->dev;
	void *ctx_addr;

	ctx_addr = kzalloc(ctx_size, GFP_KERNEL);
	if (!ctx_addr)
		return ERR_PTR(-ENOMEM);

	*dma_addr = dma_map_single(dev, ctx_addr, ctx_size, DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, *dma_addr)) {
		dev_err(dev, "DMA mapping error!\n");
		kfree(ctx_addr);
		return ERR_PTR(-ENOMEM);
	}

	return ctx_addr;
}

static void qm_ctx_free(struct hisi_qm *qm, size_t ctx_size,
			const void *ctx_addr, dma_addr_t *dma_addr)
{
	struct device *dev = &qm->pdev->dev;

	dma_unmap_single(dev, *dma_addr, ctx_size, DMA_FROM_DEVICE);
	kfree(ctx_addr);
}

static int dump_show(struct hisi_qm *qm, void *info,
		     unsigned int info_size, char *info_name)
{
	struct device *dev = &qm->pdev->dev;
	u8 *info_buf, *info_curr = info;
	u32 i;
#define BYTE_PER_DW	4

	info_buf = kzalloc(info_size, GFP_KERNEL);
	if (!info_buf)
		return -ENOMEM;

	for (i = 0; i < info_size; i++, info_curr++) {
		if (i % BYTE_PER_DW == 0)
			info_buf[i + 3UL] = *info_curr;
		else if (i % BYTE_PER_DW == 1)
			info_buf[i + 1UL] = *info_curr;
		else if (i % BYTE_PER_DW == 2)
			info_buf[i - 1] = *info_curr;
		else if (i % BYTE_PER_DW == 3)
			info_buf[i - 3] = *info_curr;
	}

	dev_info(dev, "%s DUMP\n", info_name);
	for (i = 0; i < info_size; i += BYTE_PER_DW) {
		pr_info("DW%u: %02X%02X %02X%02X\n", i / BYTE_PER_DW,
			info_buf[i], info_buf[i + 1UL],
			info_buf[i + 2UL], info_buf[i + 3UL]);
	}

	kfree(info_buf);

	return 0;
}

static int qm_dump_sqc_raw(struct hisi_qm *qm, dma_addr_t dma_addr, u16 qp_id)
{
	return qm_mb(qm, QM_MB_CMD_SQC, dma_addr, qp_id, 1);
}

static int qm_dump_cqc_raw(struct hisi_qm *qm, dma_addr_t dma_addr, u16 qp_id)
{
	return qm_mb(qm, QM_MB_CMD_CQC, dma_addr, qp_id, 1);
}

static int qm_sqc_dump(struct hisi_qm *qm, const char *s)
{
	struct device *dev = &qm->pdev->dev;
	struct qm_sqc *sqc, *sqc_curr;
	dma_addr_t sqc_dma;
	u32 qp_id;
	int ret;

	if (!s)
		return -EINVAL;

	ret = kstrtou32(s, 0, &qp_id);
	if (ret || qp_id >= qm->qp_num) {
		dev_err(dev, "Please input qp num (0-%u)", qm->qp_num - 1);
		return -EINVAL;
	}

	sqc = qm_ctx_alloc(qm, sizeof(*sqc), &sqc_dma);
	if (IS_ERR(sqc))
		return PTR_ERR(sqc);

	ret = qm_dump_sqc_raw(qm, sqc_dma, qp_id);
	if (ret) {
		down_read(&qm->qps_lock);
		if (qm->sqc) {
			sqc_curr = qm->sqc + qp_id;

			ret = dump_show(qm, sqc_curr, sizeof(*sqc),
					"SOFT SQC");
			if (ret)
				dev_info(dev, "Show soft sqc failed!\n");
		}
		up_read(&qm->qps_lock);

		goto err_free_ctx;
	}

	ret = dump_show(qm, sqc, sizeof(*sqc), "SQC");
	if (ret)
		dev_info(dev, "Show hw sqc failed!\n");

err_free_ctx:
	qm_ctx_free(qm, sizeof(*sqc), sqc, &sqc_dma);
	return ret;
}

static int qm_cqc_dump(struct hisi_qm *qm, const char *s)
{
	struct device *dev = &qm->pdev->dev;
	struct qm_cqc *cqc, *cqc_curr;
	dma_addr_t cqc_dma;
	u32 qp_id;
	int ret;

	if (!s)
		return -EINVAL;

	ret = kstrtou32(s, 0, &qp_id);
	if (ret || qp_id >= qm->qp_num) {
		dev_err(dev, "Please input qp num (0-%u)", qm->qp_num - 1);
		return -EINVAL;
	}

	cqc = qm_ctx_alloc(qm, sizeof(*cqc), &cqc_dma);
	if (IS_ERR(cqc))
		return PTR_ERR(cqc);

	ret = qm_dump_cqc_raw(qm, cqc_dma, qp_id);
	if (ret) {
		down_read(&qm->qps_lock);
		if (qm->cqc) {
			cqc_curr = qm->cqc + qp_id;

			ret = dump_show(qm, cqc_curr, sizeof(*cqc),
					"SOFT CQC");
			if (ret)
				dev_info(dev, "Show soft cqc failed!\n");
		}
		up_read(&qm->qps_lock);

		goto err_free_ctx;
	}

	ret = dump_show(qm, cqc, sizeof(*cqc), "CQC");
	if (ret)
		dev_info(dev, "Show hw cqc failed!\n");

err_free_ctx:
	qm_ctx_free(qm, sizeof(*cqc), cqc, &cqc_dma);
	return ret;
}

static int qm_eqc_aeqc_dump(struct hisi_qm *qm, char *s, size_t size,
			    int cmd, char *name)
{
	struct device *dev = &qm->pdev->dev;
	dma_addr_t xeqc_dma;
	void *xeqc;
	int ret;

	if (strsep(&s, " ")) {
		dev_err(dev, "Please do not input extra characters!\n");
		return -EINVAL;
	}

	xeqc = qm_ctx_alloc(qm, size, &xeqc_dma);
	if (IS_ERR(xeqc))
		return PTR_ERR(xeqc);

	ret = qm_mb(qm, cmd, xeqc_dma, 0, 1);
	if (ret)
		goto err_free_ctx;

	ret = dump_show(qm, xeqc, size, name);
	if (ret)
		dev_info(dev, "Show hw %s failed!\n", name);

err_free_ctx:
	qm_ctx_free(qm, size, xeqc, &xeqc_dma);
	return ret;
}

static int q_dump_param_parse(struct hisi_qm *qm, char *s,
			      u32 *e_id, u32 *q_id)
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
	if (ret || *e_id >= QM_Q_DEPTH) {
		dev_err(dev, "Please input sqe num (0-%d)", QM_Q_DEPTH - 1);
		return -EINVAL;
	}

	if (strsep(&s, " ")) {
		dev_err(dev, "Please do not input extra characters!\n");
		return -EINVAL;
	}

	return 0;
}

static int qm_sq_dump(struct hisi_qm *qm, char *s)
{
	struct device *dev = &qm->pdev->dev;
	void *sqe, *sqe_curr;
	struct hisi_qp *qp;
	u32 qp_id, sqe_id;
	int ret;

	ret = q_dump_param_parse(qm, s, &sqe_id, &qp_id);
	if (ret)
		return ret;

	sqe = kzalloc(qm->sqe_size * QM_Q_DEPTH, GFP_KERNEL);
	if (!sqe)
		return -ENOMEM;

	qp = &qm->qp_array[qp_id];
	memcpy(sqe, qp->sqe, qm->sqe_size * QM_Q_DEPTH);
	sqe_curr = sqe + (u32)(sqe_id * qm->sqe_size);
	memset(sqe_curr + qm->debug.sqe_mask_offset, QM_SQE_ADDR_MASK,
	       qm->debug.sqe_mask_len);

	ret = dump_show(qm, sqe_curr, qm->sqe_size, "SQE");
	if (ret)
		dev_info(dev, "Show sqe failed!\n");

	kfree(sqe);

	return ret;
}

static int qm_cq_dump(struct hisi_qm *qm, char *s)
{
	struct device *dev = &qm->pdev->dev;
	struct qm_cqe *cqe_curr;
	struct hisi_qp *qp;
	u32 qp_id, cqe_id;
	int ret;

	ret = q_dump_param_parse(qm, s, &cqe_id, &qp_id);
	if (ret)
		return ret;

	qp = &qm->qp_array[qp_id];
	cqe_curr = qp->cqe + cqe_id;
	ret = dump_show(qm, cqe_curr, sizeof(struct qm_cqe), "CQE");
	if (ret)
		dev_info(dev, "Show cqe failed!\n");

	return ret;
}

static int qm_eq_aeq_dump(struct hisi_qm *qm, const char *s,
			  size_t size, char *name)
{
	struct device *dev = &qm->pdev->dev;
	void *xeqe;
	u32 xeqe_id;
	int ret;

	if (!s)
		return -EINVAL;

	ret = kstrtou32(s, 0, &xeqe_id);
	if (ret)
		return -EINVAL;

	if (!strcmp(name, "EQE") && xeqe_id >= QM_EQ_DEPTH) {
		dev_err(dev, "Please input eqe num (0-%d)", QM_EQ_DEPTH - 1);
		return -EINVAL;
	} else if (!strcmp(name, "AEQE") && xeqe_id >= QM_Q_DEPTH) {
		dev_err(dev, "Please input aeqe num (0-%d)", QM_Q_DEPTH - 1);
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

	ret = dump_show(qm, xeqe, size, name);
	if (ret)
		dev_info(dev, "Show %s failed!\n", name);

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

static int qm_cmd_write_dump(struct hisi_qm *qm, const char *cmd_buf)
{
	struct device *dev = &qm->pdev->dev;
	char *presult, *s, *s_tmp;
	int ret;

	s = kstrdup(cmd_buf, GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	s_tmp = s;
	presult = strsep(&s, " ");
	if (!presult) {
		ret = -EINVAL;
		goto err_buffer_free;
	}

	if (!strcmp(presult, "sqc"))
		ret = qm_sqc_dump(qm, s);
	else if (!strcmp(presult, "cqc"))
		ret = qm_cqc_dump(qm, s);
	else if (!strcmp(presult, "eqc"))
		ret = qm_eqc_aeqc_dump(qm, s, sizeof(struct qm_eqc),
				       QM_MB_CMD_EQC, "EQC");
	else if (!strcmp(presult, "aeqc"))
		ret = qm_eqc_aeqc_dump(qm, s, sizeof(struct qm_aeqc),
				       QM_MB_CMD_AEQC, "AEQC");
	else if (!strcmp(presult, "sq"))
		ret = qm_sq_dump(qm, s);
	else if (!strcmp(presult, "cq"))
		ret = qm_cq_dump(qm, s);
	else if (!strcmp(presult, "eq"))
		ret = qm_eq_aeq_dump(qm, s, sizeof(struct qm_eqe), "EQE");
	else if (!strcmp(presult, "aeq"))
		ret = qm_eq_aeq_dump(qm, s, sizeof(struct qm_aeqe), "AEQE");
	else if (!strcmp(presult, "help"))
		ret = qm_dbg_help(qm, s);
	else
		ret = -EINVAL;

	if (ret)
		dev_info(dev, "Please echo help\n");

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
	if (unlikely(atomic_read(&qm->status.flags) == QM_STOP))
		return 0;

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

static void qm_create_debugfs_file(struct hisi_qm *qm, struct dentry *dir,
				   enum qm_debug_file index)
{
	struct debugfs_file *file = qm->debug.files + index;

	debugfs_create_file(qm_debug_file_name[index], 0600, dir, file,
			    &qm_debug_fops);

	file->index = index;
	mutex_init(&file->lock);
	file->debug = &qm->debug;
}

static void qm_hw_error_init_v1(struct hisi_qm *qm, u32 ce, u32 nfe, u32 fe)
{
	writel(QM_ABNORMAL_INT_MASK_VALUE, qm->io_base + QM_ABNORMAL_INT_MASK);
}

static void qm_hw_error_cfg(struct hisi_qm *qm, u32 ce, u32 nfe, u32 fe)
{
	qm->error_mask = ce | nfe | fe;
	/* clear QM hw residual error source */
	writel(QM_ABNORMAL_INT_SOURCE_CLR,
	       qm->io_base + QM_ABNORMAL_INT_SOURCE);

	/* configure error type */
	writel(ce, qm->io_base + QM_RAS_CE_ENABLE);
	writel(QM_RAS_CE_TIMES_PER_IRQ, qm->io_base + QM_RAS_CE_THRESHOLD);
	writel(nfe, qm->io_base + QM_RAS_NFE_ENABLE);
	writel(fe, qm->io_base + QM_RAS_FE_ENABLE);
}

static void qm_hw_error_init_v2(struct hisi_qm *qm, u32 ce, u32 nfe, u32 fe)
{
	u32 irq_enable = ce | nfe | fe;
	u32 irq_unmask = ~irq_enable;

	qm_hw_error_cfg(qm, ce, nfe, fe);

	irq_unmask &= readl(qm->io_base + QM_ABNORMAL_INT_MASK);
	writel(irq_unmask, qm->io_base + QM_ABNORMAL_INT_MASK);
}

static void qm_hw_error_uninit_v2(struct hisi_qm *qm)
{
	writel(QM_ABNORMAL_INT_MASK_VALUE, qm->io_base + QM_ABNORMAL_INT_MASK);
}

static void qm_hw_error_init_v3(struct hisi_qm *qm, u32 ce, u32 nfe, u32 fe)
{
	u32 irq_enable = ce | nfe | fe;
	u32 irq_unmask = ~irq_enable;

	qm_hw_error_cfg(qm, ce, nfe, fe);

	/* enable close master ooo when hardware error happened */
	writel(nfe & (~QM_DB_RANDOM_INVALID), qm->io_base + QM_OOO_SHUTDOWN_SEL);

	irq_unmask &= readl(qm->io_base + QM_ABNORMAL_INT_MASK);
	writel(irq_unmask, qm->io_base + QM_ABNORMAL_INT_MASK);
}

static void qm_hw_error_uninit_v3(struct hisi_qm *qm)
{
	writel(QM_ABNORMAL_INT_MASK_VALUE, qm->io_base + QM_ABNORMAL_INT_MASK);

	/* disable close master ooo when hardware error happened */
	writel(0x0, qm->io_base + QM_OOO_SHUTDOWN_SEL);
}

static void qm_log_hw_error(struct hisi_qm *qm, u32 error_status)
{
	const struct hisi_qm_hw_error *err;
	struct device *dev = &qm->pdev->dev;
	u32 reg_val, type, vf_num;
	int i;

	for (i = 0; i < ARRAY_SIZE(qm_hw_error); i++) {
		err = &qm_hw_error[i];
		if (!(err->int_msk & error_status))
			continue;

		dev_err(dev, "%s [error status=0x%x] found\n",
			err->msg, err->int_msk);

		if (err->int_msk & QM_DB_TIMEOUT) {
			reg_val = readl(qm->io_base + QM_ABNORMAL_INF01);
			type = (reg_val & QM_DB_TIMEOUT_TYPE) >>
			       QM_DB_TIMEOUT_TYPE_SHIFT;
			vf_num = reg_val & QM_DB_TIMEOUT_VF;
			dev_err(dev, "qm %s doorbell timeout in function %u\n",
				qm_db_timeout[type], vf_num);
		} else if (err->int_msk & QM_OF_FIFO_OF) {
			reg_val = readl(qm->io_base + QM_ABNORMAL_INF00);
			type = (reg_val & QM_FIFO_OVERFLOW_TYPE) >>
			       QM_FIFO_OVERFLOW_TYPE_SHIFT;
			vf_num = reg_val & QM_FIFO_OVERFLOW_VF;

			if (type < ARRAY_SIZE(qm_fifo_overflow))
				dev_err(dev, "qm %s fifo overflow in function %u\n",
					qm_fifo_overflow[type], vf_num);
			else
				dev_err(dev, "unknown error type\n");
		}
	}
}

static enum acc_err_result qm_hw_error_handle_v2(struct hisi_qm *qm)
{
	u32 error_status, tmp, val;

	/* read err sts */
	tmp = readl(qm->io_base + QM_ABNORMAL_INT_STATUS);
	error_status = qm->error_mask & tmp;

	if (error_status) {
		if (error_status & QM_ECC_MBIT)
			qm->err_status.is_qm_ecc_mbit = true;

		qm_log_hw_error(qm, error_status);
		val = error_status | QM_DB_RANDOM_INVALID | QM_BASE_CE;
		/* ce error does not need to be reset */
		if (val == (QM_DB_RANDOM_INVALID | QM_BASE_CE)) {
			writel(error_status, qm->io_base +
			       QM_ABNORMAL_INT_SOURCE);
			writel(qm->err_info.nfe,
			       qm->io_base + QM_RAS_NFE_ENABLE);
			return ACC_ERR_RECOVERED;
		}

		return ACC_ERR_NEED_RESET;
	}

	return ACC_ERR_RECOVERED;
}

static int qm_get_mb_cmd(struct hisi_qm *qm, u64 *msg, u16 fun_num)
{
	struct qm_mailbox mailbox;
	int ret;

	qm_mb_pre_init(&mailbox, QM_MB_CMD_DST, 0, fun_num, 0);
	mutex_lock(&qm->mailbox_lock);
	ret = qm_mb_nolock(qm, &mailbox);
	if (ret)
		goto err_unlock;

	*msg = readl(qm->io_base + QM_MB_CMD_DATA_ADDR_L) |
		  ((u64)readl(qm->io_base + QM_MB_CMD_DATA_ADDR_H) << 32);

err_unlock:
	mutex_unlock(&qm->mailbox_lock);
	return ret;
}

static void qm_clear_cmd_interrupt(struct hisi_qm *qm, u64 vf_mask)
{
	u32 val;

	if (qm->fun_type == QM_HW_PF)
		writeq(vf_mask, qm->io_base + QM_IFC_INT_SOURCE_P);

	val = readl(qm->io_base + QM_IFC_INT_SOURCE_V);
	val |= QM_IFC_INT_SOURCE_MASK;
	writel(val, qm->io_base + QM_IFC_INT_SOURCE_V);
}

static void qm_handle_vf_msg(struct hisi_qm *qm, u32 vf_id)
{
	struct device *dev = &qm->pdev->dev;
	u32 cmd;
	u64 msg;
	int ret;

	ret = qm_get_mb_cmd(qm, &msg, vf_id);
	if (ret) {
		dev_err(dev, "failed to get msg from VF(%u)!\n", vf_id);
		return;
	}

	cmd = msg & QM_MB_CMD_DATA_MASK;
	switch (cmd) {
	case QM_VF_PREPARE_FAIL:
		dev_err(dev, "failed to stop VF(%u)!\n", vf_id);
		break;
	case QM_VF_START_FAIL:
		dev_err(dev, "failed to start VF(%u)!\n", vf_id);
		break;
	case QM_VF_PREPARE_DONE:
	case QM_VF_START_DONE:
		break;
	default:
		dev_err(dev, "unsupported cmd %u sent by VF(%u)!\n", cmd, vf_id);
		break;
	}
}

static int qm_wait_vf_prepare_finish(struct hisi_qm *qm)
{
	struct device *dev = &qm->pdev->dev;
	u32 vfs_num = qm->vfs_num;
	int cnt = 0;
	int ret = 0;
	u64 val;
	u32 i;

	if (!qm->vfs_num || qm->ver < QM_HW_V3)
		return 0;

	while (true) {
		val = readq(qm->io_base + QM_IFC_INT_SOURCE_P);
		/* All VFs send command to PF, break */
		if ((val & GENMASK(vfs_num, 1)) == GENMASK(vfs_num, 1))
			break;

		if (++cnt > QM_MAX_PF_WAIT_COUNT) {
			ret = -EBUSY;
			break;
		}

		msleep(QM_WAIT_DST_ACK);
	}

	/* PF check VFs msg */
	for (i = 1; i <= vfs_num; i++) {
		if (val & BIT(i))
			qm_handle_vf_msg(qm, i);
		else
			dev_err(dev, "VF(%u) not ping PF!\n", i);
	}

	/* PF clear interrupt to ack VFs */
	qm_clear_cmd_interrupt(qm, val);

	return ret;
}

static void qm_trigger_vf_interrupt(struct hisi_qm *qm, u32 fun_num)
{
	u32 val;

	val = readl(qm->io_base + QM_IFC_INT_CFG);
	val &= ~QM_IFC_SEND_ALL_VFS;
	val |= fun_num;
	writel(val, qm->io_base + QM_IFC_INT_CFG);

	val = readl(qm->io_base + QM_IFC_INT_SET_P);
	val |= QM_IFC_INT_SET_MASK;
	writel(val, qm->io_base + QM_IFC_INT_SET_P);
}

static void qm_trigger_pf_interrupt(struct hisi_qm *qm)
{
	u32 val;

	val = readl(qm->io_base + QM_IFC_INT_SET_V);
	val |= QM_IFC_INT_SET_MASK;
	writel(val, qm->io_base + QM_IFC_INT_SET_V);
}

static int qm_ping_single_vf(struct hisi_qm *qm, u64 cmd, u32 fun_num)
{
	struct device *dev = &qm->pdev->dev;
	struct qm_mailbox mailbox;
	int cnt = 0;
	u64 val;
	int ret;

	qm_mb_pre_init(&mailbox, QM_MB_CMD_SRC, cmd, fun_num, 0);
	mutex_lock(&qm->mailbox_lock);
	ret = qm_mb_nolock(qm, &mailbox);
	if (ret) {
		dev_err(dev, "failed to send command to vf(%u)!\n", fun_num);
		goto err_unlock;
	}

	qm_trigger_vf_interrupt(qm, fun_num);
	while (true) {
		msleep(QM_WAIT_DST_ACK);
		val = readq(qm->io_base + QM_IFC_READY_STATUS);
		/* if VF respond, PF notifies VF successfully. */
		if (!(val & BIT(fun_num)))
			goto err_unlock;

		if (++cnt > QM_MAX_PF_WAIT_COUNT) {
			dev_err(dev, "failed to get response from VF(%u)!\n", fun_num);
			ret = -ETIMEDOUT;
			break;
		}
	}

err_unlock:
	mutex_unlock(&qm->mailbox_lock);
	return ret;
}

static int qm_ping_all_vfs(struct hisi_qm *qm, u64 cmd)
{
	struct device *dev = &qm->pdev->dev;
	u32 vfs_num = qm->vfs_num;
	struct qm_mailbox mailbox;
	u64 val = 0;
	int cnt = 0;
	int ret;
	u32 i;

	qm_mb_pre_init(&mailbox, QM_MB_CMD_SRC, cmd, QM_MB_PING_ALL_VFS, 0);
	mutex_lock(&qm->mailbox_lock);
	/* PF sends command to all VFs by mailbox */
	ret = qm_mb_nolock(qm, &mailbox);
	if (ret) {
		dev_err(dev, "failed to send command to VFs!\n");
		mutex_unlock(&qm->mailbox_lock);
		return ret;
	}

	qm_trigger_vf_interrupt(qm, QM_IFC_SEND_ALL_VFS);
	while (true) {
		msleep(QM_WAIT_DST_ACK);
		val = readq(qm->io_base + QM_IFC_READY_STATUS);
		/* If all VFs acked, PF notifies VFs successfully. */
		if (!(val & GENMASK(vfs_num, 1))) {
			mutex_unlock(&qm->mailbox_lock);
			return 0;
		}

		if (++cnt > QM_MAX_PF_WAIT_COUNT)
			break;
	}

	mutex_unlock(&qm->mailbox_lock);

	/* Check which vf respond timeout. */
	for (i = 1; i <= vfs_num; i++) {
		if (val & BIT(i))
			dev_err(dev, "failed to get response from VF(%u)!\n", i);
	}

	return -ETIMEDOUT;
}

static int qm_ping_pf(struct hisi_qm *qm, u64 cmd)
{
	struct qm_mailbox mailbox;
	int cnt = 0;
	u32 val;
	int ret;

	qm_mb_pre_init(&mailbox, QM_MB_CMD_SRC, cmd, 0, 0);
	mutex_lock(&qm->mailbox_lock);
	ret = qm_mb_nolock(qm, &mailbox);
	if (ret) {
		dev_err(&qm->pdev->dev, "failed to send command to PF!\n");
		goto unlock;
	}

	qm_trigger_pf_interrupt(qm);
	/* Waiting for PF response */
	while (true) {
		msleep(QM_WAIT_DST_ACK);
		val = readl(qm->io_base + QM_IFC_INT_SET_V);
		if (!(val & QM_IFC_INT_STATUS_MASK))
			break;

		if (++cnt > QM_MAX_VF_WAIT_COUNT) {
			ret = -ETIMEDOUT;
			break;
		}
	}

unlock:
	mutex_unlock(&qm->mailbox_lock);
	return ret;
}

static int qm_stop_qp(struct hisi_qp *qp)
{
	return qm_mb(qp->qm, QM_MB_CMD_STOP_QP, 0, qp->qp_id, 0);
}

static int qm_set_msi(struct hisi_qm *qm, bool set)
{
	struct pci_dev *pdev = qm->pdev;

	if (set) {
		pci_write_config_dword(pdev, pdev->msi_cap + PCI_MSI_MASK_64,
				       0);
	} else {
		pci_write_config_dword(pdev, pdev->msi_cap + PCI_MSI_MASK_64,
				       ACC_PEH_MSI_DISABLE);
		if (qm->err_status.is_qm_ecc_mbit ||
		    qm->err_status.is_dev_ecc_mbit)
			return 0;

		mdelay(1);
		if (readl(qm->io_base + QM_PEH_DFX_INFO0))
			return -EFAULT;
	}

	return 0;
}

static void qm_wait_msi_finish(struct hisi_qm *qm)
{
	struct pci_dev *pdev = qm->pdev;
	u32 cmd = ~0;
	int cnt = 0;
	u32 val;
	int ret;

	while (true) {
		pci_read_config_dword(pdev, pdev->msi_cap +
				      PCI_MSI_PENDING_64, &cmd);
		if (!cmd)
			break;

		if (++cnt > MAX_WAIT_COUNTS) {
			pci_warn(pdev, "failed to empty MSI PENDING!\n");
			break;
		}

		udelay(1);
	}

	ret = readl_relaxed_poll_timeout(qm->io_base + QM_PEH_DFX_INFO0,
					 val, !(val & QM_PEH_DFX_MASK),
					 POLL_PERIOD, POLL_TIMEOUT);
	if (ret)
		pci_warn(pdev, "failed to empty PEH MSI!\n");

	ret = readl_relaxed_poll_timeout(qm->io_base + QM_PEH_DFX_INFO1,
					 val, !(val & QM_PEH_MSI_FINISH_MASK),
					 POLL_PERIOD, POLL_TIMEOUT);
	if (ret)
		pci_warn(pdev, "failed to finish MSI operation!\n");
}

static int qm_set_msi_v3(struct hisi_qm *qm, bool set)
{
	struct pci_dev *pdev = qm->pdev;
	int ret = -ETIMEDOUT;
	u32 cmd, i;

	pci_read_config_dword(pdev, pdev->msi_cap, &cmd);
	if (set)
		cmd |= QM_MSI_CAP_ENABLE;
	else
		cmd &= ~QM_MSI_CAP_ENABLE;

	pci_write_config_dword(pdev, pdev->msi_cap, cmd);
	if (set) {
		for (i = 0; i < MAX_WAIT_COUNTS; i++) {
			pci_read_config_dword(pdev, pdev->msi_cap, &cmd);
			if (cmd & QM_MSI_CAP_ENABLE)
				return 0;

			udelay(1);
		}
	} else {
		udelay(WAIT_PERIOD_US_MIN);
		qm_wait_msi_finish(qm);
		ret = 0;
	}

	return ret;
}

static const struct hisi_qm_hw_ops qm_hw_ops_v1 = {
	.qm_db = qm_db_v1,
	.get_irq_num = qm_get_irq_num_v1,
	.hw_error_init = qm_hw_error_init_v1,
	.set_msi = qm_set_msi,
};

static const struct hisi_qm_hw_ops qm_hw_ops_v2 = {
	.get_vft = qm_get_vft_v2,
	.qm_db = qm_db_v2,
	.get_irq_num = qm_get_irq_num_v2,
	.hw_error_init = qm_hw_error_init_v2,
	.hw_error_uninit = qm_hw_error_uninit_v2,
	.hw_error_handle = qm_hw_error_handle_v2,
	.set_msi = qm_set_msi,
};

static const struct hisi_qm_hw_ops qm_hw_ops_v3 = {
	.get_vft = qm_get_vft_v2,
	.qm_db = qm_db_v2,
	.get_irq_num = qm_get_irq_num_v3,
	.hw_error_init = qm_hw_error_init_v3,
	.hw_error_uninit = qm_hw_error_uninit_v3,
	.hw_error_handle = qm_hw_error_handle_v2,
	.stop_qp = qm_stop_qp,
	.set_msi = qm_set_msi_v3,
	.ping_all_vfs = qm_ping_all_vfs,
	.ping_pf = qm_ping_pf,
};

static void *qm_get_avail_sqe(struct hisi_qp *qp)
{
	struct hisi_qp_status *qp_status = &qp->qp_status;
	u16 sq_tail = qp_status->sq_tail;

	if (unlikely(atomic_read(&qp->qp_status.used) == QM_Q_DEPTH - 1))
		return NULL;

	return qp->sqe + sq_tail * qp->qm->sqe_size;
}

static void hisi_qm_unset_hw_reset(struct hisi_qp *qp)
{
	u64 *addr;

	/* Use last 64 bits of DUS to reset status. */
	addr = (u64 *)(qp->qdma.va + qp->qdma.size) - QM_RESET_STOP_TX_OFFSET;
	*addr = 0;
}

static struct hisi_qp *qm_create_qp_nolock(struct hisi_qm *qm, u8 alg_type)
{
	struct device *dev = &qm->pdev->dev;
	struct hisi_qp *qp;
	int qp_id;

	if (!qm_qp_avail_state(qm, NULL, QP_INIT))
		return ERR_PTR(-EPERM);

	if (qm->qp_in_used == qm->qp_num) {
		dev_info_ratelimited(dev, "All %u queues of QM are busy!\n",
				     qm->qp_num);
		atomic64_inc(&qm->debug.dfx.create_qp_err_cnt);
		return ERR_PTR(-EBUSY);
	}

	qp_id = idr_alloc_cyclic(&qm->qp_idr, NULL, 0, qm->qp_num, GFP_ATOMIC);
	if (qp_id < 0) {
		dev_info_ratelimited(dev, "All %u queues of QM are busy!\n",
				    qm->qp_num);
		atomic64_inc(&qm->debug.dfx.create_qp_err_cnt);
		return ERR_PTR(-EBUSY);
	}

	qp = &qm->qp_array[qp_id];
	hisi_qm_unset_hw_reset(qp);
	memset(qp->cqe, 0, sizeof(struct qm_cqe) * QM_Q_DEPTH);

	qp->event_cb = NULL;
	qp->req_cb = NULL;
	qp->qp_id = qp_id;
	qp->alg_type = alg_type;
	qp->is_in_kernel = true;
	qm->qp_in_used++;
	atomic_set(&qp->qp_status.flags, QP_INIT);

	return qp;
}

/**
 * hisi_qm_create_qp() - Create a queue pair from qm.
 * @qm: The qm we create a qp from.
 * @alg_type: Accelerator specific algorithm type in sqc.
 *
 * return created qp, -EBUSY if all qps in qm allocated, -ENOMEM if allocating
 * qp memory fails.
 */
struct hisi_qp *hisi_qm_create_qp(struct hisi_qm *qm, u8 alg_type)
{
	struct hisi_qp *qp;
	int ret;

	ret = qm_pm_get_sync(qm);
	if (ret)
		return ERR_PTR(ret);

	down_write(&qm->qps_lock);
	qp = qm_create_qp_nolock(qm, alg_type);
	up_write(&qm->qps_lock);

	if (IS_ERR(qp))
		qm_pm_put_sync(qm);

	return qp;
}
EXPORT_SYMBOL_GPL(hisi_qm_create_qp);

/**
 * hisi_qm_release_qp() - Release a qp back to its qm.
 * @qp: The qp we want to release.
 *
 * This function releases the resource of a qp.
 */
void hisi_qm_release_qp(struct hisi_qp *qp)
{
	struct hisi_qm *qm = qp->qm;

	down_write(&qm->qps_lock);

	if (!qm_qp_avail_state(qm, qp, QP_CLOSE)) {
		up_write(&qm->qps_lock);
		return;
	}

	qm->qp_in_used--;
	idr_remove(&qm->qp_idr, qp->qp_id);

	up_write(&qm->qps_lock);

	qm_pm_put_sync(qm);
}
EXPORT_SYMBOL_GPL(hisi_qm_release_qp);

static int qm_sq_ctx_cfg(struct hisi_qp *qp, int qp_id, u32 pasid)
{
	struct hisi_qm *qm = qp->qm;
	struct device *dev = &qm->pdev->dev;
	enum qm_hw_ver ver = qm->ver;
	struct qm_sqc *sqc;
	dma_addr_t sqc_dma;
	int ret;

	sqc = kzalloc(sizeof(struct qm_sqc), GFP_KERNEL);
	if (!sqc)
		return -ENOMEM;

	INIT_QC_COMMON(sqc, qp->sqe_dma, pasid);
	if (ver == QM_HW_V1) {
		sqc->dw3 = cpu_to_le32(QM_MK_SQC_DW3_V1(0, 0, 0, qm->sqe_size));
		sqc->w8 = cpu_to_le16(QM_Q_DEPTH - 1);
	} else {
		sqc->dw3 = cpu_to_le32(QM_MK_SQC_DW3_V2(qm->sqe_size));
		sqc->w8 = 0; /* rand_qc */
	}
	sqc->cq_num = cpu_to_le16(qp_id);
	sqc->w13 = cpu_to_le16(QM_MK_SQC_W13(0, 1, qp->alg_type));

	if (ver >= QM_HW_V3 && qm->use_sva && !qp->is_in_kernel)
		sqc->w11 = cpu_to_le16(QM_QC_PASID_ENABLE <<
				       QM_QC_PASID_ENABLE_SHIFT);

	sqc_dma = dma_map_single(dev, sqc, sizeof(struct qm_sqc),
				 DMA_TO_DEVICE);
	if (dma_mapping_error(dev, sqc_dma)) {
		kfree(sqc);
		return -ENOMEM;
	}

	ret = qm_mb(qm, QM_MB_CMD_SQC, sqc_dma, qp_id, 0);
	dma_unmap_single(dev, sqc_dma, sizeof(struct qm_sqc), DMA_TO_DEVICE);
	kfree(sqc);

	return ret;
}

static int qm_cq_ctx_cfg(struct hisi_qp *qp, int qp_id, u32 pasid)
{
	struct hisi_qm *qm = qp->qm;
	struct device *dev = &qm->pdev->dev;
	enum qm_hw_ver ver = qm->ver;
	struct qm_cqc *cqc;
	dma_addr_t cqc_dma;
	int ret;

	cqc = kzalloc(sizeof(struct qm_cqc), GFP_KERNEL);
	if (!cqc)
		return -ENOMEM;

	INIT_QC_COMMON(cqc, qp->cqe_dma, pasid);
	if (ver == QM_HW_V1) {
		cqc->dw3 = cpu_to_le32(QM_MK_CQC_DW3_V1(0, 0, 0,
							QM_QC_CQE_SIZE));
		cqc->w8 = cpu_to_le16(QM_Q_DEPTH - 1);
	} else {
		cqc->dw3 = cpu_to_le32(QM_MK_CQC_DW3_V2(QM_QC_CQE_SIZE));
		cqc->w8 = 0; /* rand_qc */
	}
	cqc->dw6 = cpu_to_le32(1 << QM_CQ_PHASE_SHIFT | 1 << QM_CQ_FLAG_SHIFT);

	if (ver >= QM_HW_V3 && qm->use_sva && !qp->is_in_kernel)
		cqc->w11 = cpu_to_le16(QM_QC_PASID_ENABLE);

	cqc_dma = dma_map_single(dev, cqc, sizeof(struct qm_cqc),
				 DMA_TO_DEVICE);
	if (dma_mapping_error(dev, cqc_dma)) {
		kfree(cqc);
		return -ENOMEM;
	}

	ret = qm_mb(qm, QM_MB_CMD_CQC, cqc_dma, qp_id, 0);
	dma_unmap_single(dev, cqc_dma, sizeof(struct qm_cqc), DMA_TO_DEVICE);
	kfree(cqc);

	return ret;
}

static int qm_qp_ctx_cfg(struct hisi_qp *qp, int qp_id, u32 pasid)
{
	int ret;

	qm_init_qp_status(qp);

	ret = qm_sq_ctx_cfg(qp, qp_id, pasid);
	if (ret)
		return ret;

	return qm_cq_ctx_cfg(qp, qp_id, pasid);
}

static int qm_start_qp_nolock(struct hisi_qp *qp, unsigned long arg)
{
	struct hisi_qm *qm = qp->qm;
	struct device *dev = &qm->pdev->dev;
	int qp_id = qp->qp_id;
	u32 pasid = arg;
	int ret;

	if (!qm_qp_avail_state(qm, qp, QP_START))
		return -EPERM;

	ret = qm_qp_ctx_cfg(qp, qp_id, pasid);
	if (ret)
		return ret;

	atomic_set(&qp->qp_status.flags, QP_START);
	dev_dbg(dev, "queue %d started\n", qp_id);

	return 0;
}

/**
 * hisi_qm_start_qp() - Start a qp into running.
 * @qp: The qp we want to start to run.
 * @arg: Accelerator specific argument.
 *
 * After this function, qp can receive request from user. Return 0 if
 * successful, Return -EBUSY if failed.
 */
int hisi_qm_start_qp(struct hisi_qp *qp, unsigned long arg)
{
	struct hisi_qm *qm = qp->qm;
	int ret;

	down_write(&qm->qps_lock);
	ret = qm_start_qp_nolock(qp, arg);
	up_write(&qm->qps_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(hisi_qm_start_qp);

/**
 * qp_stop_fail_cb() - call request cb.
 * @qp: stopped failed qp.
 *
 * Callback function should be called whether task completed or not.
 */
static void qp_stop_fail_cb(struct hisi_qp *qp)
{
	int qp_used = atomic_read(&qp->qp_status.used);
	u16 cur_tail = qp->qp_status.sq_tail;
	u16 cur_head = (cur_tail + QM_Q_DEPTH - qp_used) % QM_Q_DEPTH;
	struct hisi_qm *qm = qp->qm;
	u16 pos;
	int i;

	for (i = 0; i < qp_used; i++) {
		pos = (i + cur_head) % QM_Q_DEPTH;
		qp->req_cb(qp, qp->sqe + (u32)(qm->sqe_size * pos));
		atomic_dec(&qp->qp_status.used);
	}
}

/**
 * qm_drain_qp() - Drain a qp.
 * @qp: The qp we want to drain.
 *
 * Determine whether the queue is cleared by judging the tail pointers of
 * sq and cq.
 */
static int qm_drain_qp(struct hisi_qp *qp)
{
	size_t size = sizeof(struct qm_sqc) + sizeof(struct qm_cqc);
	struct hisi_qm *qm = qp->qm;
	struct device *dev = &qm->pdev->dev;
	struct qm_sqc *sqc;
	struct qm_cqc *cqc;
	dma_addr_t dma_addr;
	int ret = 0, i = 0;
	void *addr;

	/* No need to judge if master OOO is blocked. */
	if (qm_check_dev_error(qm))
		return 0;

	/* Kunpeng930 supports drain qp by device */
	if (qm->ops->stop_qp) {
		ret = qm->ops->stop_qp(qp);
		if (ret)
			dev_err(dev, "Failed to stop qp(%u)!\n", qp->qp_id);
		return ret;
	}

	addr = qm_ctx_alloc(qm, size, &dma_addr);
	if (IS_ERR(addr)) {
		dev_err(dev, "Failed to alloc ctx for sqc and cqc!\n");
		return -ENOMEM;
	}

	while (++i) {
		ret = qm_dump_sqc_raw(qm, dma_addr, qp->qp_id);
		if (ret) {
			dev_err_ratelimited(dev, "Failed to dump sqc!\n");
			break;
		}
		sqc = addr;

		ret = qm_dump_cqc_raw(qm, (dma_addr + sizeof(struct qm_sqc)),
				      qp->qp_id);
		if (ret) {
			dev_err_ratelimited(dev, "Failed to dump cqc!\n");
			break;
		}
		cqc = addr + sizeof(struct qm_sqc);

		if ((sqc->tail == cqc->tail) &&
		    (QM_SQ_TAIL_IDX(sqc) == QM_CQ_TAIL_IDX(cqc)))
			break;

		if (i == MAX_WAIT_COUNTS) {
			dev_err(dev, "Fail to empty queue %u!\n", qp->qp_id);
			ret = -EBUSY;
			break;
		}

		usleep_range(WAIT_PERIOD_US_MIN, WAIT_PERIOD_US_MAX);
	}

	qm_ctx_free(qm, size, addr, &dma_addr);

	return ret;
}

static int qm_stop_qp_nolock(struct hisi_qp *qp)
{
	struct device *dev = &qp->qm->pdev->dev;
	int ret;

	/*
	 * It is allowed to stop and release qp when reset, If the qp is
	 * stopped when reset but still want to be released then, the
	 * is_resetting flag should be set negative so that this qp will not
	 * be restarted after reset.
	 */
	if (atomic_read(&qp->qp_status.flags) == QP_STOP) {
		qp->is_resetting = false;
		return 0;
	}

	if (!qm_qp_avail_state(qp->qm, qp, QP_STOP))
		return -EPERM;

	atomic_set(&qp->qp_status.flags, QP_STOP);

	ret = qm_drain_qp(qp);
	if (ret)
		dev_err(dev, "Failed to drain out data for stopping!\n");

	if (qp->qm->wq)
		flush_workqueue(qp->qm->wq);
	else
		flush_work(&qp->qm->work);

	if (unlikely(qp->is_resetting && atomic_read(&qp->qp_status.used)))
		qp_stop_fail_cb(qp);

	dev_dbg(dev, "stop queue %u!", qp->qp_id);

	return 0;
}

/**
 * hisi_qm_stop_qp() - Stop a qp in qm.
 * @qp: The qp we want to stop.
 *
 * This function is reverse of hisi_qm_start_qp. Return 0 if successful.
 */
int hisi_qm_stop_qp(struct hisi_qp *qp)
{
	int ret;

	down_write(&qp->qm->qps_lock);
	ret = qm_stop_qp_nolock(qp);
	up_write(&qp->qm->qps_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(hisi_qm_stop_qp);

/**
 * hisi_qp_send() - Queue up a task in the hardware queue.
 * @qp: The qp in which to put the message.
 * @msg: The message.
 *
 * This function will return -EBUSY if qp is currently full, and -EAGAIN
 * if qp related qm is resetting.
 *
 * Note: This function may run with qm_irq_thread and ACC reset at same time.
 *       It has no race with qm_irq_thread. However, during hisi_qp_send, ACC
 *       reset may happen, we have no lock here considering performance. This
 *       causes current qm_db sending fail or can not receive sended sqe. QM
 *       sync/async receive function should handle the error sqe. ACC reset
 *       done function should clear used sqe to 0.
 */
int hisi_qp_send(struct hisi_qp *qp, const void *msg)
{
	struct hisi_qp_status *qp_status = &qp->qp_status;
	u16 sq_tail = qp_status->sq_tail;
	u16 sq_tail_next = (sq_tail + 1) % QM_Q_DEPTH;
	void *sqe = qm_get_avail_sqe(qp);

	if (unlikely(atomic_read(&qp->qp_status.flags) == QP_STOP ||
		     atomic_read(&qp->qm->status.flags) == QM_STOP ||
		     qp->is_resetting)) {
		dev_info_ratelimited(&qp->qm->pdev->dev, "QP is stopped or resetting\n");
		return -EAGAIN;
	}

	if (!sqe)
		return -EBUSY;

	memcpy(sqe, msg, qp->qm->sqe_size);

	qm_db(qp->qm, qp->qp_id, QM_DOORBELL_CMD_SQ, sq_tail_next, 0);
	atomic_inc(&qp->qp_status.used);
	qp_status->sq_tail = sq_tail_next;

	return 0;
}
EXPORT_SYMBOL_GPL(hisi_qp_send);

static void hisi_qm_cache_wb(struct hisi_qm *qm)
{
	unsigned int val;

	if (qm->ver == QM_HW_V1)
		return;

	writel(0x1, qm->io_base + QM_CACHE_WB_START);
	if (readl_relaxed_poll_timeout(qm->io_base + QM_CACHE_WB_DONE,
				       val, val & BIT(0), POLL_PERIOD,
				       POLL_TIMEOUT))
		dev_err(&qm->pdev->dev, "QM writeback sqc cache fail!\n");
}

static void qm_qp_event_notifier(struct hisi_qp *qp)
{
	wake_up_interruptible(&qp->uacce_q->wait);
}

static int hisi_qm_get_available_instances(struct uacce_device *uacce)
{
	return hisi_qm_get_free_qp_num(uacce->priv);
}

static void hisi_qm_set_hw_reset(struct hisi_qm *qm, int offset)
{
	int i;

	for (i = 0; i < qm->qp_num; i++)
		qm_set_qp_disable(&qm->qp_array[i], offset);
}

static int hisi_qm_uacce_get_queue(struct uacce_device *uacce,
				   unsigned long arg,
				   struct uacce_queue *q)
{
	struct hisi_qm *qm = uacce->priv;
	struct hisi_qp *qp;
	u8 alg_type = 0;

	qp = hisi_qm_create_qp(qm, alg_type);
	if (IS_ERR(qp))
		return PTR_ERR(qp);

	q->priv = qp;
	q->uacce = uacce;
	qp->uacce_q = q;
	qp->event_cb = qm_qp_event_notifier;
	qp->pasid = arg;
	qp->is_in_kernel = false;

	return 0;
}

static void hisi_qm_uacce_put_queue(struct uacce_queue *q)
{
	struct hisi_qp *qp = q->priv;

	hisi_qm_cache_wb(qp->qm);
	hisi_qm_release_qp(qp);
}

/* map sq/cq/doorbell to user space */
static int hisi_qm_uacce_mmap(struct uacce_queue *q,
			      struct vm_area_struct *vma,
			      struct uacce_qfile_region *qfr)
{
	struct hisi_qp *qp = q->priv;
	struct hisi_qm *qm = qp->qm;
	resource_size_t phys_base = qm->db_phys_base +
				    qp->qp_id * qm->db_interval;
	size_t sz = vma->vm_end - vma->vm_start;
	struct pci_dev *pdev = qm->pdev;
	struct device *dev = &pdev->dev;
	unsigned long vm_pgoff;
	int ret;

	switch (qfr->type) {
	case UACCE_QFRT_MMIO:
		if (qm->ver == QM_HW_V1) {
			if (sz > PAGE_SIZE * QM_DOORBELL_PAGE_NR)
				return -EINVAL;
		} else if (qm->ver == QM_HW_V2 || !qm->use_db_isolation) {
			if (sz > PAGE_SIZE * (QM_DOORBELL_PAGE_NR +
			    QM_DOORBELL_SQ_CQ_BASE_V2 / PAGE_SIZE))
				return -EINVAL;
		} else {
			if (sz > qm->db_interval)
				return -EINVAL;
		}

		vma->vm_flags |= VM_IO;

		return remap_pfn_range(vma, vma->vm_start,
				       phys_base >> PAGE_SHIFT,
				       sz, pgprot_noncached(vma->vm_page_prot));
	case UACCE_QFRT_DUS:
		if (sz != qp->qdma.size)
			return -EINVAL;

		/*
		 * dma_mmap_coherent() requires vm_pgoff as 0
		 * restore vm_pfoff to initial value for mmap()
		 */
		vm_pgoff = vma->vm_pgoff;
		vma->vm_pgoff = 0;
		ret = dma_mmap_coherent(dev, vma, qp->qdma.va,
					qp->qdma.dma, sz);
		vma->vm_pgoff = vm_pgoff;
		return ret;

	default:
		return -EINVAL;
	}
}

static int hisi_qm_uacce_start_queue(struct uacce_queue *q)
{
	struct hisi_qp *qp = q->priv;

	return hisi_qm_start_qp(qp, qp->pasid);
}

static void hisi_qm_uacce_stop_queue(struct uacce_queue *q)
{
	hisi_qm_stop_qp(q->priv);
}

static int hisi_qm_is_q_updated(struct uacce_queue *q)
{
	struct hisi_qp *qp = q->priv;
	struct qm_cqe *cqe = qp->cqe + qp->qp_status.cq_head;
	int updated = 0;

	while (QM_CQE_PHASE(cqe) == qp->qp_status.cqc_phase) {
		/* make sure to read data from memory */
		dma_rmb();
		qm_cq_head_update(qp);
		cqe = qp->cqe + qp->qp_status.cq_head;
		updated = 1;
	}

	return updated;
}

static void qm_set_sqctype(struct uacce_queue *q, u16 type)
{
	struct hisi_qm *qm = q->uacce->priv;
	struct hisi_qp *qp = q->priv;

	down_write(&qm->qps_lock);
	qp->alg_type = type;
	up_write(&qm->qps_lock);
}

static long hisi_qm_uacce_ioctl(struct uacce_queue *q, unsigned int cmd,
				unsigned long arg)
{
	struct hisi_qp *qp = q->priv;
	struct hisi_qp_ctx qp_ctx;

	if (cmd == UACCE_CMD_QM_SET_QP_CTX) {
		if (copy_from_user(&qp_ctx, (void __user *)arg,
				   sizeof(struct hisi_qp_ctx)))
			return -EFAULT;

		if (qp_ctx.qc_type != 0 && qp_ctx.qc_type != 1)
			return -EINVAL;

		qm_set_sqctype(q, qp_ctx.qc_type);
		qp_ctx.id = qp->qp_id;

		if (copy_to_user((void __user *)arg, &qp_ctx,
				 sizeof(struct hisi_qp_ctx)))
			return -EFAULT;
	} else {
		return -EINVAL;
	}

	return 0;
}

static const struct uacce_ops uacce_qm_ops = {
	.get_available_instances = hisi_qm_get_available_instances,
	.get_queue = hisi_qm_uacce_get_queue,
	.put_queue = hisi_qm_uacce_put_queue,
	.start_queue = hisi_qm_uacce_start_queue,
	.stop_queue = hisi_qm_uacce_stop_queue,
	.mmap = hisi_qm_uacce_mmap,
	.ioctl = hisi_qm_uacce_ioctl,
	.is_q_updated = hisi_qm_is_q_updated,
};

static int qm_alloc_uacce(struct hisi_qm *qm)
{
	struct pci_dev *pdev = qm->pdev;
	struct uacce_device *uacce;
	unsigned long mmio_page_nr;
	unsigned long dus_page_nr;
	struct uacce_interface interface = {
		.flags = UACCE_DEV_SVA,
		.ops = &uacce_qm_ops,
	};
	int ret;

	ret = strscpy(interface.name, dev_driver_string(&pdev->dev),
		      sizeof(interface.name));
	if (ret < 0)
		return -ENAMETOOLONG;

	uacce = uacce_alloc(&pdev->dev, &interface);
	if (IS_ERR(uacce))
		return PTR_ERR(uacce);

	if (uacce->flags & UACCE_DEV_SVA) {
		qm->use_sva = true;
	} else {
		/* only consider sva case */
		uacce_remove(uacce);
		qm->uacce = NULL;
		return -EINVAL;
	}

	uacce->is_vf = pdev->is_virtfn;
	uacce->priv = qm;
	uacce->algs = qm->algs;

	if (qm->ver == QM_HW_V1)
		uacce->api_ver = HISI_QM_API_VER_BASE;
	else if (qm->ver == QM_HW_V2)
		uacce->api_ver = HISI_QM_API_VER2_BASE;
	else
		uacce->api_ver = HISI_QM_API_VER3_BASE;

	if (qm->ver == QM_HW_V1)
		mmio_page_nr = QM_DOORBELL_PAGE_NR;
	else if (qm->ver == QM_HW_V2 || !qm->use_db_isolation)
		mmio_page_nr = QM_DOORBELL_PAGE_NR +
			QM_DOORBELL_SQ_CQ_BASE_V2 / PAGE_SIZE;
	else
		mmio_page_nr = qm->db_interval / PAGE_SIZE;

	/* Add one more page for device or qp status */
	dus_page_nr = (PAGE_SIZE - 1 + qm->sqe_size * QM_Q_DEPTH +
		       sizeof(struct qm_cqe) * QM_Q_DEPTH  + PAGE_SIZE) >>
					 PAGE_SHIFT;

	uacce->qf_pg_num[UACCE_QFRT_MMIO] = mmio_page_nr;
	uacce->qf_pg_num[UACCE_QFRT_DUS]  = dus_page_nr;

	qm->uacce = uacce;

	return 0;
}

/**
 * qm_frozen() - Try to froze QM to cut continuous queue request. If
 * there is user on the QM, return failure without doing anything.
 * @qm: The qm needed to be fronzen.
 *
 * This function frozes QM, then we can do SRIOV disabling.
 */
static int qm_frozen(struct hisi_qm *qm)
{
	if (test_bit(QM_DRIVER_REMOVING, &qm->misc_ctl))
		return 0;

	down_write(&qm->qps_lock);

	if (!qm->qp_in_used) {
		qm->qp_in_used = qm->qp_num;
		up_write(&qm->qps_lock);
		set_bit(QM_DRIVER_REMOVING, &qm->misc_ctl);
		return 0;
	}

	up_write(&qm->qps_lock);

	return -EBUSY;
}

static int qm_try_frozen_vfs(struct pci_dev *pdev,
			     struct hisi_qm_list *qm_list)
{
	struct hisi_qm *qm, *vf_qm;
	struct pci_dev *dev;
	int ret = 0;

	if (!qm_list || !pdev)
		return -EINVAL;

	/* Try to frozen all the VFs as disable SRIOV */
	mutex_lock(&qm_list->lock);
	list_for_each_entry(qm, &qm_list->list, list) {
		dev = qm->pdev;
		if (dev == pdev)
			continue;
		if (pci_physfn(dev) == pdev) {
			vf_qm = pci_get_drvdata(dev);
			ret = qm_frozen(vf_qm);
			if (ret)
				goto frozen_fail;
		}
	}

frozen_fail:
	mutex_unlock(&qm_list->lock);

	return ret;
}

/**
 * hisi_qm_wait_task_finish() - Wait until the task is finished
 * when removing the driver.
 * @qm: The qm needed to wait for the task to finish.
 * @qm_list: The list of all available devices.
 */
void hisi_qm_wait_task_finish(struct hisi_qm *qm, struct hisi_qm_list *qm_list)
{
	while (qm_frozen(qm) ||
	       ((qm->fun_type == QM_HW_PF) &&
	       qm_try_frozen_vfs(qm->pdev, qm_list))) {
		msleep(WAIT_PERIOD);
	}

	while (test_bit(QM_RST_SCHED, &qm->misc_ctl) ||
	       test_bit(QM_RESETTING, &qm->misc_ctl))
		msleep(WAIT_PERIOD);

	udelay(REMOVE_WAIT_DELAY);
}
EXPORT_SYMBOL_GPL(hisi_qm_wait_task_finish);

/**
 * hisi_qm_get_free_qp_num() - Get free number of qp in qm.
 * @qm: The qm which want to get free qp.
 *
 * This function return free number of qp in qm.
 */
int hisi_qm_get_free_qp_num(struct hisi_qm *qm)
{
	int ret;

	down_read(&qm->qps_lock);
	ret = qm->qp_num - qm->qp_in_used;
	up_read(&qm->qps_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(hisi_qm_get_free_qp_num);

static void hisi_qp_memory_uninit(struct hisi_qm *qm, int num)
{
	struct device *dev = &qm->pdev->dev;
	struct qm_dma *qdma;
	int i;

	for (i = num - 1; i >= 0; i--) {
		qdma = &qm->qp_array[i].qdma;
		dma_free_coherent(dev, qdma->size, qdma->va, qdma->dma);
	}

	kfree(qm->qp_array);
}

static int hisi_qp_memory_init(struct hisi_qm *qm, size_t dma_size, int id)
{
	struct device *dev = &qm->pdev->dev;
	size_t off = qm->sqe_size * QM_Q_DEPTH;
	struct hisi_qp *qp;

	qp = &qm->qp_array[id];
	qp->qdma.va = dma_alloc_coherent(dev, dma_size, &qp->qdma.dma,
					 GFP_KERNEL);
	if (!qp->qdma.va)
		return -ENOMEM;

	qp->sqe = qp->qdma.va;
	qp->sqe_dma = qp->qdma.dma;
	qp->cqe = qp->qdma.va + off;
	qp->cqe_dma = qp->qdma.dma + off;
	qp->qdma.size = dma_size;
	qp->qm = qm;
	qp->qp_id = id;

	return 0;
}

static void hisi_qm_pre_init(struct hisi_qm *qm)
{
	struct pci_dev *pdev = qm->pdev;

	if (qm->ver == QM_HW_V1)
		qm->ops = &qm_hw_ops_v1;
	else if (qm->ver == QM_HW_V2)
		qm->ops = &qm_hw_ops_v2;
	else
		qm->ops = &qm_hw_ops_v3;

	pci_set_drvdata(pdev, qm);
	mutex_init(&qm->mailbox_lock);
	init_rwsem(&qm->qps_lock);
	qm->qp_in_used = 0;
	qm->misc_ctl = false;
	if (qm->fun_type == QM_HW_PF && qm->ver > QM_HW_V2) {
		if (!acpi_device_power_manageable(ACPI_COMPANION(&pdev->dev)))
			dev_info(&pdev->dev, "_PS0 and _PR0 are not defined");
	}
}

static void qm_cmd_uninit(struct hisi_qm *qm)
{
	u32 val;

	if (qm->ver < QM_HW_V3)
		return;

	val = readl(qm->io_base + QM_IFC_INT_MASK);
	val |= QM_IFC_INT_DISABLE;
	writel(val, qm->io_base + QM_IFC_INT_MASK);
}

static void qm_cmd_init(struct hisi_qm *qm)
{
	u32 val;

	if (qm->ver < QM_HW_V3)
		return;

	/* Clear communication interrupt source */
	qm_clear_cmd_interrupt(qm, QM_IFC_INT_SOURCE_CLR);

	/* Enable pf to vf communication reg. */
	val = readl(qm->io_base + QM_IFC_INT_MASK);
	val &= ~QM_IFC_INT_DISABLE;
	writel(val, qm->io_base + QM_IFC_INT_MASK);
}

static void qm_put_pci_res(struct hisi_qm *qm)
{
	struct pci_dev *pdev = qm->pdev;

	if (qm->use_db_isolation)
		iounmap(qm->db_io_base);

	iounmap(qm->io_base);
	pci_release_mem_regions(pdev);
}

static void hisi_qm_pci_uninit(struct hisi_qm *qm)
{
	struct pci_dev *pdev = qm->pdev;

	pci_free_irq_vectors(pdev);
	qm_put_pci_res(qm);
	pci_disable_device(pdev);
}

/**
 * hisi_qm_uninit() - Uninitialize qm.
 * @qm: The qm needed uninit.
 *
 * This function uninits qm related device resources.
 */
void hisi_qm_uninit(struct hisi_qm *qm)
{
	struct pci_dev *pdev = qm->pdev;
	struct device *dev = &pdev->dev;

	qm_cmd_uninit(qm);
	kfree(qm->factor);
	down_write(&qm->qps_lock);

	if (!qm_avail_state(qm, QM_CLOSE)) {
		up_write(&qm->qps_lock);
		return;
	}

	hisi_qp_memory_uninit(qm, qm->qp_num);
	idr_destroy(&qm->qp_idr);

	if (qm->qdma.va) {
		hisi_qm_cache_wb(qm);
		dma_free_coherent(dev, qm->qdma.size,
				  qm->qdma.va, qm->qdma.dma);
	}
	up_write(&qm->qps_lock);

	qm_irq_unregister(qm);
	hisi_qm_pci_uninit(qm);
	if (qm->use_sva) {
		uacce_remove(qm->uacce);
		qm->uacce = NULL;
	}
}
EXPORT_SYMBOL_GPL(hisi_qm_uninit);

/**
 * hisi_qm_get_vft() - Get vft from a qm.
 * @qm: The qm we want to get its vft.
 * @base: The base number of queue in vft.
 * @number: The number of queues in vft.
 *
 * We can allocate multiple queues to a qm by configuring virtual function
 * table. We get related configures by this function. Normally, we call this
 * function in VF driver to get the queue information.
 *
 * qm hw v1 does not support this interface.
 */
int hisi_qm_get_vft(struct hisi_qm *qm, u32 *base, u32 *number)
{
	if (!base || !number)
		return -EINVAL;

	if (!qm->ops->get_vft) {
		dev_err(&qm->pdev->dev, "Don't support vft read!\n");
		return -EINVAL;
	}

	return qm->ops->get_vft(qm, base, number);
}
EXPORT_SYMBOL_GPL(hisi_qm_get_vft);

/**
 * hisi_qm_set_vft() - Set vft to a qm.
 * @qm: The qm we want to set its vft.
 * @fun_num: The function number.
 * @base: The base number of queue in vft.
 * @number: The number of queues in vft.
 *
 * This function is alway called in PF driver, it is used to assign queues
 * among PF and VFs.
 *
 * Assign queues A~B to PF: hisi_qm_set_vft(qm, 0, A, B - A + 1)
 * Assign queues A~B to VF: hisi_qm_set_vft(qm, 2, A, B - A + 1)
 * (VF function number 0x2)
 */
static int hisi_qm_set_vft(struct hisi_qm *qm, u32 fun_num, u32 base,
		    u32 number)
{
	u32 max_q_num = qm->ctrl_qp_num;

	if (base >= max_q_num || number > max_q_num ||
	    (base + number) > max_q_num)
		return -EINVAL;

	return qm_set_sqc_cqc_vft(qm, fun_num, base, number);
}

static void qm_init_eq_aeq_status(struct hisi_qm *qm)
{
	struct hisi_qm_status *status = &qm->status;

	status->eq_head = 0;
	status->aeq_head = 0;
	status->eqc_phase = true;
	status->aeqc_phase = true;
}

static void qm_enable_eq_aeq_interrupts(struct hisi_qm *qm)
{
	/* Clear eq/aeq interrupt source */
	qm_db(qm, 0, QM_DOORBELL_CMD_AEQ, qm->status.aeq_head, 0);
	qm_db(qm, 0, QM_DOORBELL_CMD_EQ, qm->status.eq_head, 0);

	writel(0x0, qm->io_base + QM_VF_EQ_INT_MASK);
	writel(0x0, qm->io_base + QM_VF_AEQ_INT_MASK);
}

static void qm_disable_eq_aeq_interrupts(struct hisi_qm *qm)
{
	writel(0x1, qm->io_base + QM_VF_EQ_INT_MASK);
	writel(0x1, qm->io_base + QM_VF_AEQ_INT_MASK);
}

static int qm_eq_ctx_cfg(struct hisi_qm *qm)
{
	struct device *dev = &qm->pdev->dev;
	struct qm_eqc *eqc;
	dma_addr_t eqc_dma;
	int ret;

	eqc = kzalloc(sizeof(struct qm_eqc), GFP_KERNEL);
	if (!eqc)
		return -ENOMEM;

	eqc->base_l = cpu_to_le32(lower_32_bits(qm->eqe_dma));
	eqc->base_h = cpu_to_le32(upper_32_bits(qm->eqe_dma));
	if (qm->ver == QM_HW_V1)
		eqc->dw3 = cpu_to_le32(QM_EQE_AEQE_SIZE);
	eqc->dw6 = cpu_to_le32((QM_EQ_DEPTH - 1) | (1 << QM_EQC_PHASE_SHIFT));

	eqc_dma = dma_map_single(dev, eqc, sizeof(struct qm_eqc),
				 DMA_TO_DEVICE);
	if (dma_mapping_error(dev, eqc_dma)) {
		kfree(eqc);
		return -ENOMEM;
	}

	ret = qm_mb(qm, QM_MB_CMD_EQC, eqc_dma, 0, 0);
	dma_unmap_single(dev, eqc_dma, sizeof(struct qm_eqc), DMA_TO_DEVICE);
	kfree(eqc);

	return ret;
}

static int qm_aeq_ctx_cfg(struct hisi_qm *qm)
{
	struct device *dev = &qm->pdev->dev;
	struct qm_aeqc *aeqc;
	dma_addr_t aeqc_dma;
	int ret;

	aeqc = kzalloc(sizeof(struct qm_aeqc), GFP_KERNEL);
	if (!aeqc)
		return -ENOMEM;

	aeqc->base_l = cpu_to_le32(lower_32_bits(qm->aeqe_dma));
	aeqc->base_h = cpu_to_le32(upper_32_bits(qm->aeqe_dma));
	aeqc->dw6 = cpu_to_le32((QM_Q_DEPTH - 1) | (1 << QM_EQC_PHASE_SHIFT));

	aeqc_dma = dma_map_single(dev, aeqc, sizeof(struct qm_aeqc),
				  DMA_TO_DEVICE);
	if (dma_mapping_error(dev, aeqc_dma)) {
		kfree(aeqc);
		return -ENOMEM;
	}

	ret = qm_mb(qm, QM_MB_CMD_AEQC, aeqc_dma, 0, 0);
	dma_unmap_single(dev, aeqc_dma, sizeof(struct qm_aeqc), DMA_TO_DEVICE);
	kfree(aeqc);

	return ret;
}

static int qm_eq_aeq_ctx_cfg(struct hisi_qm *qm)
{
	struct device *dev = &qm->pdev->dev;
	int ret;

	qm_init_eq_aeq_status(qm);

	ret = qm_eq_ctx_cfg(qm);
	if (ret) {
		dev_err(dev, "Set eqc failed!\n");
		return ret;
	}

	return qm_aeq_ctx_cfg(qm);
}

static int __hisi_qm_start(struct hisi_qm *qm)
{
	int ret;

	WARN_ON(!qm->qdma.va);

	if (qm->fun_type == QM_HW_PF) {
		ret = hisi_qm_set_vft(qm, 0, qm->qp_base, qm->qp_num);
		if (ret)
			return ret;
	}

	ret = qm_eq_aeq_ctx_cfg(qm);
	if (ret)
		return ret;

	ret = qm_mb(qm, QM_MB_CMD_SQC_BT, qm->sqc_dma, 0, 0);
	if (ret)
		return ret;

	ret = qm_mb(qm, QM_MB_CMD_CQC_BT, qm->cqc_dma, 0, 0);
	if (ret)
		return ret;

	qm_init_prefetch(qm);
	qm_enable_eq_aeq_interrupts(qm);

	return 0;
}

/**
 * hisi_qm_start() - start qm
 * @qm: The qm to be started.
 *
 * This function starts a qm, then we can allocate qp from this qm.
 */
int hisi_qm_start(struct hisi_qm *qm)
{
	struct device *dev = &qm->pdev->dev;
	int ret = 0;

	down_write(&qm->qps_lock);

	if (!qm_avail_state(qm, QM_START)) {
		up_write(&qm->qps_lock);
		return -EPERM;
	}

	dev_dbg(dev, "qm start with %u queue pairs\n", qm->qp_num);

	if (!qm->qp_num) {
		dev_err(dev, "qp_num should not be 0\n");
		ret = -EINVAL;
		goto err_unlock;
	}

	ret = __hisi_qm_start(qm);
	if (!ret)
		atomic_set(&qm->status.flags, QM_START);

err_unlock:
	up_write(&qm->qps_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(hisi_qm_start);

static int qm_restart(struct hisi_qm *qm)
{
	struct device *dev = &qm->pdev->dev;
	struct hisi_qp *qp;
	int ret, i;

	ret = hisi_qm_start(qm);
	if (ret < 0)
		return ret;

	down_write(&qm->qps_lock);
	for (i = 0; i < qm->qp_num; i++) {
		qp = &qm->qp_array[i];
		if (atomic_read(&qp->qp_status.flags) == QP_STOP &&
		    qp->is_resetting == true) {
			ret = qm_start_qp_nolock(qp, 0);
			if (ret < 0) {
				dev_err(dev, "Failed to start qp%d!\n", i);

				up_write(&qm->qps_lock);
				return ret;
			}
			qp->is_resetting = false;
		}
	}
	up_write(&qm->qps_lock);

	return 0;
}

/* Stop started qps in reset flow */
static int qm_stop_started_qp(struct hisi_qm *qm)
{
	struct device *dev = &qm->pdev->dev;
	struct hisi_qp *qp;
	int i, ret;

	for (i = 0; i < qm->qp_num; i++) {
		qp = &qm->qp_array[i];
		if (qp && atomic_read(&qp->qp_status.flags) == QP_START) {
			qp->is_resetting = true;
			ret = qm_stop_qp_nolock(qp);
			if (ret < 0) {
				dev_err(dev, "Failed to stop qp%d!\n", i);
				return ret;
			}
		}
	}

	return 0;
}


/**
 * qm_clear_queues() - Clear all queues memory in a qm.
 * @qm: The qm in which the queues will be cleared.
 *
 * This function clears all queues memory in a qm. Reset of accelerator can
 * use this to clear queues.
 */
static void qm_clear_queues(struct hisi_qm *qm)
{
	struct hisi_qp *qp;
	int i;

	for (i = 0; i < qm->qp_num; i++) {
		qp = &qm->qp_array[i];
		if (qp->is_resetting)
			memset(qp->qdma.va, 0, qp->qdma.size);
	}

	memset(qm->qdma.va, 0, qm->qdma.size);
}

/**
 * hisi_qm_stop() - Stop a qm.
 * @qm: The qm which will be stopped.
 * @r: The reason to stop qm.
 *
 * This function stops qm and its qps, then qm can not accept request.
 * Related resources are not released at this state, we can use hisi_qm_start
 * to let qm start again.
 */
int hisi_qm_stop(struct hisi_qm *qm, enum qm_stop_reason r)
{
	struct device *dev = &qm->pdev->dev;
	int ret = 0;

	down_write(&qm->qps_lock);

	qm->status.stop_reason = r;
	if (!qm_avail_state(qm, QM_STOP)) {
		ret = -EPERM;
		goto err_unlock;
	}

	if (qm->status.stop_reason == QM_SOFT_RESET ||
	    qm->status.stop_reason == QM_FLR) {
		hisi_qm_set_hw_reset(qm, QM_RESET_STOP_TX_OFFSET);
		ret = qm_stop_started_qp(qm);
		if (ret < 0) {
			dev_err(dev, "Failed to stop started qp!\n");
			goto err_unlock;
		}
		hisi_qm_set_hw_reset(qm, QM_RESET_STOP_RX_OFFSET);
	}

	qm_disable_eq_aeq_interrupts(qm);
	if (qm->fun_type == QM_HW_PF) {
		ret = hisi_qm_set_vft(qm, 0, 0, 0);
		if (ret < 0) {
			dev_err(dev, "Failed to set vft!\n");
			ret = -EBUSY;
			goto err_unlock;
		}
	}

	qm_clear_queues(qm);
	atomic_set(&qm->status.flags, QM_STOP);

err_unlock:
	up_write(&qm->qps_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(hisi_qm_stop);

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

static void qm_hw_error_init(struct hisi_qm *qm)
{
	struct hisi_qm_err_info *err_info = &qm->err_info;

	if (!qm->ops->hw_error_init) {
		dev_err(&qm->pdev->dev, "QM doesn't support hw error handling!\n");
		return;
	}

	qm->ops->hw_error_init(qm, err_info->ce, err_info->nfe, err_info->fe);
}

static void qm_hw_error_uninit(struct hisi_qm *qm)
{
	if (!qm->ops->hw_error_uninit) {
		dev_err(&qm->pdev->dev, "Unexpected QM hw error uninit!\n");
		return;
	}

	qm->ops->hw_error_uninit(qm);
}

static enum acc_err_result qm_hw_error_handle(struct hisi_qm *qm)
{
	if (!qm->ops->hw_error_handle) {
		dev_err(&qm->pdev->dev, "QM doesn't support hw error report!\n");
		return ACC_ERR_NONE;
	}

	return qm->ops->hw_error_handle(qm);
}

/**
 * hisi_qm_dev_err_init() - Initialize device error configuration.
 * @qm: The qm for which we want to do error initialization.
 *
 * Initialize QM and device error related configuration.
 */
void hisi_qm_dev_err_init(struct hisi_qm *qm)
{
	if (qm->fun_type == QM_HW_VF)
		return;

	qm_hw_error_init(qm);

	if (!qm->err_ini->hw_err_enable) {
		dev_err(&qm->pdev->dev, "Device doesn't support hw error init!\n");
		return;
	}
	qm->err_ini->hw_err_enable(qm);
}
EXPORT_SYMBOL_GPL(hisi_qm_dev_err_init);

/**
 * hisi_qm_dev_err_uninit() - Uninitialize device error configuration.
 * @qm: The qm for which we want to do error uninitialization.
 *
 * Uninitialize QM and device error related configuration.
 */
void hisi_qm_dev_err_uninit(struct hisi_qm *qm)
{
	if (qm->fun_type == QM_HW_VF)
		return;

	qm_hw_error_uninit(qm);

	if (!qm->err_ini->hw_err_disable) {
		dev_err(&qm->pdev->dev, "Unexpected device hw error uninit!\n");
		return;
	}
	qm->err_ini->hw_err_disable(qm);
}
EXPORT_SYMBOL_GPL(hisi_qm_dev_err_uninit);

/**
 * hisi_qm_free_qps() - free multiple queue pairs.
 * @qps: The queue pairs need to be freed.
 * @qp_num: The num of queue pairs.
 */
void hisi_qm_free_qps(struct hisi_qp **qps, int qp_num)
{
	int i;

	if (!qps || qp_num <= 0)
		return;

	for (i = qp_num - 1; i >= 0; i--)
		hisi_qm_release_qp(qps[i]);
}
EXPORT_SYMBOL_GPL(hisi_qm_free_qps);

static void free_list(struct list_head *head)
{
	struct hisi_qm_resource *res, *tmp;

	list_for_each_entry_safe(res, tmp, head, list) {
		list_del(&res->list);
		kfree(res);
	}
}

static int hisi_qm_sort_devices(int node, struct list_head *head,
				struct hisi_qm_list *qm_list)
{
	struct hisi_qm_resource *res, *tmp;
	struct hisi_qm *qm;
	struct list_head *n;
	struct device *dev;
	int dev_node = 0;

	list_for_each_entry(qm, &qm_list->list, list) {
		dev = &qm->pdev->dev;

		if (IS_ENABLED(CONFIG_NUMA)) {
			dev_node = dev_to_node(dev);
			if (dev_node < 0)
				dev_node = 0;
		}

		res = kzalloc(sizeof(*res), GFP_KERNEL);
		if (!res)
			return -ENOMEM;

		res->qm = qm;
		res->distance = node_distance(dev_node, node);
		n = head;
		list_for_each_entry(tmp, head, list) {
			if (res->distance < tmp->distance) {
				n = &tmp->list;
				break;
			}
		}
		list_add_tail(&res->list, n);
	}

	return 0;
}

/**
 * hisi_qm_alloc_qps_node() - Create multiple queue pairs.
 * @qm_list: The list of all available devices.
 * @qp_num: The number of queue pairs need created.
 * @alg_type: The algorithm type.
 * @node: The numa node.
 * @qps: The queue pairs need created.
 *
 * This function will sort all available device according to numa distance.
 * Then try to create all queue pairs from one device, if all devices do
 * not meet the requirements will return error.
 */
int hisi_qm_alloc_qps_node(struct hisi_qm_list *qm_list, int qp_num,
			   u8 alg_type, int node, struct hisi_qp **qps)
{
	struct hisi_qm_resource *tmp;
	int ret = -ENODEV;
	LIST_HEAD(head);
	int i;

	if (!qps || !qm_list || qp_num <= 0)
		return -EINVAL;

	mutex_lock(&qm_list->lock);
	if (hisi_qm_sort_devices(node, &head, qm_list)) {
		mutex_unlock(&qm_list->lock);
		goto err;
	}

	list_for_each_entry(tmp, &head, list) {
		for (i = 0; i < qp_num; i++) {
			qps[i] = hisi_qm_create_qp(tmp->qm, alg_type);
			if (IS_ERR(qps[i])) {
				hisi_qm_free_qps(qps, i);
				break;
			}
		}

		if (i == qp_num) {
			ret = 0;
			break;
		}
	}

	mutex_unlock(&qm_list->lock);
	if (ret)
		pr_info("Failed to create qps, node[%d], alg[%u], qp[%d]!\n",
			node, alg_type, qp_num);

err:
	free_list(&head);
	return ret;
}
EXPORT_SYMBOL_GPL(hisi_qm_alloc_qps_node);

static int qm_vf_q_assign(struct hisi_qm *qm, u32 num_vfs)
{
	u32 remain_q_num, vfs_q_num, act_q_num, q_num, i, j;
	u32 max_qp_num = qm->max_qp_num;
	u32 q_base = qm->qp_num;
	int ret;

	if (!num_vfs)
		return -EINVAL;

	vfs_q_num = qm->ctrl_qp_num - qm->qp_num;

	/* If vfs_q_num is less than num_vfs, return error. */
	if (vfs_q_num < num_vfs)
		return -EINVAL;

	q_num = vfs_q_num / num_vfs;
	remain_q_num = vfs_q_num % num_vfs;

	for (i = num_vfs; i > 0; i--) {
		/*
		 * if q_num + remain_q_num > max_qp_num in last vf, divide the
		 * remaining queues equally.
		 */
		if (i == num_vfs && q_num + remain_q_num <= max_qp_num) {
			act_q_num = q_num + remain_q_num;
			remain_q_num = 0;
		} else if (remain_q_num > 0) {
			act_q_num = q_num + 1;
			remain_q_num--;
		} else {
			act_q_num = q_num;
		}

		act_q_num = min_t(int, act_q_num, max_qp_num);
		ret = hisi_qm_set_vft(qm, i, q_base, act_q_num);
		if (ret) {
			for (j = num_vfs; j > i; j--)
				hisi_qm_set_vft(qm, j, 0, 0);
			return ret;
		}
		q_base += act_q_num;
	}

	return 0;
}

static int qm_clear_vft_config(struct hisi_qm *qm)
{
	int ret;
	u32 i;

	for (i = 1; i <= qm->vfs_num; i++) {
		ret = hisi_qm_set_vft(qm, i, 0, 0);
		if (ret)
			return ret;
	}
	qm->vfs_num = 0;

	return 0;
}

static int qm_func_shaper_enable(struct hisi_qm *qm, u32 fun_index, u32 qos)
{
	struct device *dev = &qm->pdev->dev;
	u32 ir = qos * QM_QOS_RATE;
	int ret, total_vfs, i;

	total_vfs = pci_sriov_get_totalvfs(qm->pdev);
	if (fun_index > total_vfs)
		return -EINVAL;

	qm->factor[fun_index].func_qos = qos;

	ret = qm_get_shaper_para(ir, &qm->factor[fun_index]);
	if (ret) {
		dev_err(dev, "failed to calculate shaper parameter!\n");
		return -EINVAL;
	}

	for (i = ALG_TYPE_0; i <= ALG_TYPE_1; i++) {
		/* The base number of queue reuse for different alg type */
		ret = qm_set_vft_common(qm, SHAPER_VFT, fun_index, i, 1);
		if (ret) {
			dev_err(dev, "type: %d, failed to set shaper vft!\n", i);
			return -EINVAL;
		}
	}

	return 0;
}

static u32 qm_get_shaper_vft_qos(struct hisi_qm *qm, u32 fun_index)
{
	u64 cir_u = 0, cir_b = 0, cir_s = 0;
	u64 shaper_vft, ir_calc, ir;
	unsigned int val;
	u32 error_rate;
	int ret;

	ret = readl_relaxed_poll_timeout(qm->io_base + QM_VFT_CFG_RDY, val,
					 val & BIT(0), POLL_PERIOD,
					 POLL_TIMEOUT);
	if (ret)
		return 0;

	writel(0x1, qm->io_base + QM_VFT_CFG_OP_WR);
	writel(SHAPER_VFT, qm->io_base + QM_VFT_CFG_TYPE);
	writel(fun_index, qm->io_base + QM_VFT_CFG);

	writel(0x0, qm->io_base + QM_VFT_CFG_RDY);
	writel(0x1, qm->io_base + QM_VFT_CFG_OP_ENABLE);

	ret = readl_relaxed_poll_timeout(qm->io_base + QM_VFT_CFG_RDY, val,
					 val & BIT(0), POLL_PERIOD,
					 POLL_TIMEOUT);
	if (ret)
		return 0;

	shaper_vft = readl(qm->io_base + QM_VFT_CFG_DATA_L) |
		  ((u64)readl(qm->io_base + QM_VFT_CFG_DATA_H) << 32);

	cir_b = shaper_vft & QM_SHAPER_CIR_B_MASK;
	cir_u = shaper_vft & QM_SHAPER_CIR_U_MASK;
	cir_u = cir_u >> QM_SHAPER_FACTOR_CIR_U_SHIFT;

	cir_s = shaper_vft & QM_SHAPER_CIR_S_MASK;
	cir_s = cir_s >> QM_SHAPER_FACTOR_CIR_S_SHIFT;

	ir_calc = acc_shaper_para_calc(cir_b, cir_u, cir_s);

	ir = qm->factor[fun_index].func_qos * QM_QOS_RATE;

	error_rate = QM_QOS_EXPAND_RATE * (u32)abs(ir_calc - ir) / ir;
	if (error_rate > QM_QOS_MIN_ERROR_RATE) {
		pci_err(qm->pdev, "error_rate: %u, get function qos is error!\n", error_rate);
		return 0;
	}

	return ir;
}

static void qm_vf_get_qos(struct hisi_qm *qm, u32 fun_num)
{
	struct device *dev = &qm->pdev->dev;
	u64 mb_cmd;
	u32 qos;
	int ret;

	qos = qm_get_shaper_vft_qos(qm, fun_num);
	if (!qos) {
		dev_err(dev, "function(%u) failed to get qos by PF!\n", fun_num);
		return;
	}

	mb_cmd = QM_PF_SET_QOS | (u64)qos << QM_MB_CMD_DATA_SHIFT;
	ret = qm_ping_single_vf(qm, mb_cmd, fun_num);
	if (ret)
		dev_err(dev, "failed to send cmd to VF(%u)!\n", fun_num);
}

static int qm_vf_read_qos(struct hisi_qm *qm)
{
	int cnt = 0;
	int ret;

	/* reset mailbox qos val */
	qm->mb_qos = 0;

	/* vf ping pf to get function qos */
	if (qm->ops->ping_pf) {
		ret = qm->ops->ping_pf(qm, QM_VF_GET_QOS);
		if (ret) {
			pci_err(qm->pdev, "failed to send cmd to PF to get qos!\n");
			return ret;
		}
	}

	while (true) {
		msleep(QM_WAIT_DST_ACK);
		if (qm->mb_qos)
			break;

		if (++cnt > QM_MAX_VF_WAIT_COUNT) {
			pci_err(qm->pdev, "PF ping VF timeout!\n");
			return  -ETIMEDOUT;
		}
	}

	return ret;
}

static ssize_t qm_algqos_read(struct file *filp, char __user *buf,
			       size_t count, loff_t *pos)
{
	struct hisi_qm *qm = filp->private_data;
	char tbuf[QM_DBG_READ_LEN];
	u32 qos_val, ir;
	int ret;

	ret = hisi_qm_get_dfx_access(qm);
	if (ret)
		return ret;

	/* Mailbox and reset cannot be operated at the same time */
	if (test_and_set_bit(QM_RESETTING, &qm->misc_ctl)) {
		pci_err(qm->pdev, "dev resetting, read alg qos failed!\n");
		ret = -EAGAIN;
		goto err_put_dfx_access;
	}

	if (qm->fun_type == QM_HW_PF) {
		ir = qm_get_shaper_vft_qos(qm, 0);
	} else {
		ret = qm_vf_read_qos(qm);
		if (ret)
			goto err_get_status;
		ir = qm->mb_qos;
	}

	qos_val = ir / QM_QOS_RATE;
	ret = scnprintf(tbuf, QM_DBG_READ_LEN, "%u\n", qos_val);

	ret =  simple_read_from_buffer(buf, count, pos, tbuf, ret);

err_get_status:
	clear_bit(QM_RESETTING, &qm->misc_ctl);
err_put_dfx_access:
	hisi_qm_put_dfx_access(qm);
	return ret;
}

static ssize_t qm_qos_value_init(const char *buf, unsigned long *val)
{
	int buflen = strlen(buf);
	int ret, i;

	for (i = 0; i < buflen; i++) {
		if (!isdigit(buf[i]))
			return -EINVAL;
	}

	ret = sscanf(buf, "%lu", val);
	if (ret != QM_QOS_VAL_NUM)
		return -EINVAL;

	return 0;
}

static ssize_t qm_get_qos_value(struct hisi_qm *qm, const char *buf,
			       unsigned long *val,
			       unsigned int *fun_index)
{
	char tbuf_bdf[QM_DBG_READ_LEN] = {0};
	char val_buf[QM_QOS_VAL_MAX_LEN] = {0};
	u32 tmp1, device, function;
	int ret, bus;

	ret = sscanf(buf, "%s %s", tbuf_bdf, val_buf);
	if (ret != QM_QOS_PARAM_NUM)
		return -EINVAL;

	ret = qm_qos_value_init(val_buf, val);
	if (ret || *val == 0 || *val > QM_QOS_MAX_VAL) {
		pci_err(qm->pdev, "input qos value is error, please set 1~1000!\n");
		return -EINVAL;
	}

	ret = sscanf(tbuf_bdf, "%u:%x:%u.%u", &tmp1, &bus, &device, &function);
	if (ret != QM_QOS_BDF_PARAM_NUM) {
		pci_err(qm->pdev, "input pci bdf value is error!\n");
		return -EINVAL;
	}

	*fun_index = PCI_DEVFN(device, function);

	return 0;
}

static ssize_t qm_algqos_write(struct file *filp, const char __user *buf,
			       size_t count, loff_t *pos)
{
	struct hisi_qm *qm = filp->private_data;
	char tbuf[QM_DBG_READ_LEN];
	unsigned int fun_index;
	unsigned long val;
	int len, ret;

	if (qm->fun_type == QM_HW_VF)
		return -EINVAL;

	if (*pos != 0)
		return 0;

	if (count >= QM_DBG_READ_LEN)
		return -ENOSPC;

	len = simple_write_to_buffer(tbuf, QM_DBG_READ_LEN - 1, pos, buf, count);
	if (len < 0)
		return len;

	tbuf[len] = '\0';
	ret = qm_get_qos_value(qm, tbuf, &val, &fun_index);
	if (ret)
		return ret;

	/* Mailbox and reset cannot be operated at the same time */
	if (test_and_set_bit(QM_RESETTING, &qm->misc_ctl)) {
		pci_err(qm->pdev, "dev resetting, write alg qos failed!\n");
		return -EAGAIN;
	}

	ret = qm_pm_get_sync(qm);
	if (ret) {
		ret = -EINVAL;
		goto err_get_status;
	}

	ret = qm_func_shaper_enable(qm, fun_index, val);
	if (ret) {
		pci_err(qm->pdev, "failed to enable function shaper!\n");
		ret = -EINVAL;
		goto err_put_sync;
	}

	pci_info(qm->pdev, "the qos value of function%u is set to %lu.\n",
		 fun_index, val);
	ret = count;

err_put_sync:
	qm_pm_put_sync(qm);
err_get_status:
	clear_bit(QM_RESETTING, &qm->misc_ctl);
	return ret;
}

static const struct file_operations qm_algqos_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = qm_algqos_read,
	.write = qm_algqos_write,
};

/**
 * hisi_qm_set_algqos_init() - Initialize function qos debugfs files.
 * @qm: The qm for which we want to add debugfs files.
 *
 * Create function qos debugfs files.
 */
static void hisi_qm_set_algqos_init(struct hisi_qm *qm)
{
	if (qm->fun_type == QM_HW_PF)
		debugfs_create_file("alg_qos", 0644, qm->debug.debug_root,
				    qm, &qm_algqos_fops);
	else
		debugfs_create_file("alg_qos", 0444, qm->debug.debug_root,
				    qm, &qm_algqos_fops);
}

/**
 * hisi_qm_debug_init() - Initialize qm related debugfs files.
 * @qm: The qm for which we want to add debugfs files.
 *
 * Create qm related debugfs files.
 */
void hisi_qm_debug_init(struct hisi_qm *qm)
{
	struct qm_dfx *dfx = &qm->debug.dfx;
	struct dentry *qm_d;
	void *data;
	int i;

	qm_d = debugfs_create_dir("qm", qm->debug.debug_root);
	qm->debug.qm_d = qm_d;

	/* only show this in PF */
	if (qm->fun_type == QM_HW_PF) {
		qm_create_debugfs_file(qm, qm->debug.debug_root, CURRENT_QM);
		for (i = CURRENT_Q; i < DEBUG_FILE_NUM; i++)
			qm_create_debugfs_file(qm, qm->debug.qm_d, i);
	}

	debugfs_create_file("regs", 0444, qm->debug.qm_d, qm, &qm_regs_fops);

	debugfs_create_file("cmd", 0600, qm->debug.qm_d, qm, &qm_cmd_fops);

	debugfs_create_file("status", 0444, qm->debug.qm_d, qm,
			&qm_status_fops);
	for (i = 0; i < ARRAY_SIZE(qm_dfx_files); i++) {
		data = (atomic64_t *)((uintptr_t)dfx + qm_dfx_files[i].offset);
		debugfs_create_file(qm_dfx_files[i].name,
			0644,
			qm_d,
			data,
			&qm_atomic64_ops);
	}

	if (qm->ver >= QM_HW_V3)
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

/**
 * hisi_qm_sriov_enable() - enable virtual functions
 * @pdev: the PCIe device
 * @max_vfs: the number of virtual functions to enable
 *
 * Returns the number of enabled VFs. If there are VFs enabled already or
 * max_vfs is more than the total number of device can be enabled, returns
 * failure.
 */
int hisi_qm_sriov_enable(struct pci_dev *pdev, int max_vfs)
{
	struct hisi_qm *qm = pci_get_drvdata(pdev);
	int pre_existing_vfs, num_vfs, total_vfs, ret;

	ret = qm_pm_get_sync(qm);
	if (ret)
		return ret;

	total_vfs = pci_sriov_get_totalvfs(pdev);
	pre_existing_vfs = pci_num_vf(pdev);
	if (pre_existing_vfs) {
		pci_err(pdev, "%d VFs already enabled. Please disable pre-enabled VFs!\n",
			pre_existing_vfs);
		goto err_put_sync;
	}

	num_vfs = min_t(int, max_vfs, total_vfs);
	ret = qm_vf_q_assign(qm, num_vfs);
	if (ret) {
		pci_err(pdev, "Can't assign queues for VF!\n");
		goto err_put_sync;
	}

	qm->vfs_num = num_vfs;

	ret = pci_enable_sriov(pdev, num_vfs);
	if (ret) {
		pci_err(pdev, "Can't enable VF!\n");
		qm_clear_vft_config(qm);
		goto err_put_sync;
	}

	pci_info(pdev, "VF enabled, vfs_num(=%d)!\n", num_vfs);

	return num_vfs;

err_put_sync:
	qm_pm_put_sync(qm);
	return ret;
}
EXPORT_SYMBOL_GPL(hisi_qm_sriov_enable);

/**
 * hisi_qm_sriov_disable - disable virtual functions
 * @pdev: the PCI device.
 * @is_frozen: true when all the VFs are frozen.
 *
 * Return failure if there are VFs assigned already or VF is in used.
 */
int hisi_qm_sriov_disable(struct pci_dev *pdev, bool is_frozen)
{
	struct hisi_qm *qm = pci_get_drvdata(pdev);
	int total_vfs = pci_sriov_get_totalvfs(qm->pdev);
	int ret;

	if (pci_vfs_assigned(pdev)) {
		pci_err(pdev, "Failed to disable VFs as VFs are assigned!\n");
		return -EPERM;
	}

	/* While VF is in used, SRIOV cannot be disabled. */
	if (!is_frozen && qm_try_frozen_vfs(pdev, qm->qm_list)) {
		pci_err(pdev, "Task is using its VF!\n");
		return -EBUSY;
	}

	pci_disable_sriov(pdev);
	/* clear vf function shaper configure array */
	memset(qm->factor + 1, 0, sizeof(struct qm_shaper_factor) * total_vfs);
	ret = qm_clear_vft_config(qm);
	if (ret)
		return ret;

	qm_pm_put_sync(qm);

	return 0;
}
EXPORT_SYMBOL_GPL(hisi_qm_sriov_disable);

/**
 * hisi_qm_sriov_configure - configure the number of VFs
 * @pdev: The PCI device
 * @num_vfs: The number of VFs need enabled
 *
 * Enable SR-IOV according to num_vfs, 0 means disable.
 */
int hisi_qm_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	if (num_vfs == 0)
		return hisi_qm_sriov_disable(pdev, false);
	else
		return hisi_qm_sriov_enable(pdev, num_vfs);
}
EXPORT_SYMBOL_GPL(hisi_qm_sriov_configure);

static enum acc_err_result qm_dev_err_handle(struct hisi_qm *qm)
{
	u32 err_sts;

	if (!qm->err_ini->get_dev_hw_err_status) {
		dev_err(&qm->pdev->dev, "Device doesn't support get hw error status!\n");
		return ACC_ERR_NONE;
	}

	/* get device hardware error status */
	err_sts = qm->err_ini->get_dev_hw_err_status(qm);
	if (err_sts) {
		if (err_sts & qm->err_info.ecc_2bits_mask)
			qm->err_status.is_dev_ecc_mbit = true;

		if (qm->err_ini->log_dev_hw_err)
			qm->err_ini->log_dev_hw_err(qm, err_sts);

		/* ce error does not need to be reset */
		if ((err_sts | qm->err_info.dev_ce_mask) ==
		     qm->err_info.dev_ce_mask) {
			if (qm->err_ini->clear_dev_hw_err_status)
				qm->err_ini->clear_dev_hw_err_status(qm,
								err_sts);

			return ACC_ERR_RECOVERED;
		}

		return ACC_ERR_NEED_RESET;
	}

	return ACC_ERR_RECOVERED;
}

static enum acc_err_result qm_process_dev_error(struct hisi_qm *qm)
{
	enum acc_err_result qm_ret, dev_ret;

	/* log qm error */
	qm_ret = qm_hw_error_handle(qm);

	/* log device error */
	dev_ret = qm_dev_err_handle(qm);

	return (qm_ret == ACC_ERR_NEED_RESET ||
		dev_ret == ACC_ERR_NEED_RESET) ?
		ACC_ERR_NEED_RESET : ACC_ERR_RECOVERED;
}

/**
 * hisi_qm_dev_err_detected() - Get device and qm error status then log it.
 * @pdev: The PCI device which need report error.
 * @state: The connectivity between CPU and device.
 *
 * We register this function into PCIe AER handlers, It will report device or
 * qm hardware error status when error occur.
 */
pci_ers_result_t hisi_qm_dev_err_detected(struct pci_dev *pdev,
					  pci_channel_state_t state)
{
	struct hisi_qm *qm = pci_get_drvdata(pdev);
	enum acc_err_result ret;

	if (pdev->is_virtfn)
		return PCI_ERS_RESULT_NONE;

	pci_info(pdev, "PCI error detected, state(=%u)!!\n", state);
	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;

	ret = qm_process_dev_error(qm);
	if (ret == ACC_ERR_NEED_RESET)
		return PCI_ERS_RESULT_NEED_RESET;

	return PCI_ERS_RESULT_RECOVERED;
}
EXPORT_SYMBOL_GPL(hisi_qm_dev_err_detected);

static int qm_check_req_recv(struct hisi_qm *qm)
{
	struct pci_dev *pdev = qm->pdev;
	int ret;
	u32 val;

	if (qm->ver >= QM_HW_V3)
		return 0;

	writel(ACC_VENDOR_ID_VALUE, qm->io_base + QM_PEH_VENDOR_ID);
	ret = readl_relaxed_poll_timeout(qm->io_base + QM_PEH_VENDOR_ID, val,
					 (val == ACC_VENDOR_ID_VALUE),
					 POLL_PERIOD, POLL_TIMEOUT);
	if (ret) {
		dev_err(&pdev->dev, "Fails to read QM reg!\n");
		return ret;
	}

	writel(PCI_VENDOR_ID_HUAWEI, qm->io_base + QM_PEH_VENDOR_ID);
	ret = readl_relaxed_poll_timeout(qm->io_base + QM_PEH_VENDOR_ID, val,
					 (val == PCI_VENDOR_ID_HUAWEI),
					 POLL_PERIOD, POLL_TIMEOUT);
	if (ret)
		dev_err(&pdev->dev, "Fails to read QM reg in the second time!\n");

	return ret;
}

static int qm_set_pf_mse(struct hisi_qm *qm, bool set)
{
	struct pci_dev *pdev = qm->pdev;
	u16 cmd;
	int i;

	pci_read_config_word(pdev, PCI_COMMAND, &cmd);
	if (set)
		cmd |= PCI_COMMAND_MEMORY;
	else
		cmd &= ~PCI_COMMAND_MEMORY;

	pci_write_config_word(pdev, PCI_COMMAND, cmd);
	for (i = 0; i < MAX_WAIT_COUNTS; i++) {
		pci_read_config_word(pdev, PCI_COMMAND, &cmd);
		if (set == ((cmd & PCI_COMMAND_MEMORY) >> 1))
			return 0;

		udelay(1);
	}

	return -ETIMEDOUT;
}

static int qm_set_vf_mse(struct hisi_qm *qm, bool set)
{
	struct pci_dev *pdev = qm->pdev;
	u16 sriov_ctrl;
	int pos;
	int i;

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_SRIOV);
	pci_read_config_word(pdev, pos + PCI_SRIOV_CTRL, &sriov_ctrl);
	if (set)
		sriov_ctrl |= PCI_SRIOV_CTRL_MSE;
	else
		sriov_ctrl &= ~PCI_SRIOV_CTRL_MSE;
	pci_write_config_word(pdev, pos + PCI_SRIOV_CTRL, sriov_ctrl);

	for (i = 0; i < MAX_WAIT_COUNTS; i++) {
		pci_read_config_word(pdev, pos + PCI_SRIOV_CTRL, &sriov_ctrl);
		if (set == (sriov_ctrl & PCI_SRIOV_CTRL_MSE) >>
		    ACC_PEH_SRIOV_CTRL_VF_MSE_SHIFT)
			return 0;

		udelay(1);
	}

	return -ETIMEDOUT;
}

static int qm_vf_reset_prepare(struct hisi_qm *qm,
			       enum qm_stop_reason stop_reason)
{
	struct hisi_qm_list *qm_list = qm->qm_list;
	struct pci_dev *pdev = qm->pdev;
	struct pci_dev *virtfn;
	struct hisi_qm *vf_qm;
	int ret = 0;

	mutex_lock(&qm_list->lock);
	list_for_each_entry(vf_qm, &qm_list->list, list) {
		virtfn = vf_qm->pdev;
		if (virtfn == pdev)
			continue;

		if (pci_physfn(virtfn) == pdev) {
			/* save VFs PCIE BAR configuration */
			pci_save_state(virtfn);

			ret = hisi_qm_stop(vf_qm, stop_reason);
			if (ret)
				goto stop_fail;
		}
	}

stop_fail:
	mutex_unlock(&qm_list->lock);
	return ret;
}

static int qm_try_stop_vfs(struct hisi_qm *qm, u64 cmd,
			   enum qm_stop_reason stop_reason)
{
	struct pci_dev *pdev = qm->pdev;
	int ret;

	if (!qm->vfs_num)
		return 0;

	/* Kunpeng930 supports to notify VFs to stop before PF reset */
	if (qm->ops->ping_all_vfs) {
		ret = qm->ops->ping_all_vfs(qm, cmd);
		if (ret)
			pci_err(pdev, "failed to send cmd to all VFs before PF reset!\n");
	} else {
		ret = qm_vf_reset_prepare(qm, stop_reason);
		if (ret)
			pci_err(pdev, "failed to prepare reset, ret = %d.\n", ret);
	}

	return ret;
}

static int qm_controller_reset_prepare(struct hisi_qm *qm)
{
	struct pci_dev *pdev = qm->pdev;
	int ret;

	ret = qm_reset_prepare_ready(qm);
	if (ret) {
		pci_err(pdev, "Controller reset not ready!\n");
		return ret;
	}

	/* PF obtains the information of VF by querying the register. */
	qm_cmd_uninit(qm);

	/* Whether VFs stop successfully, soft reset will continue. */
	ret = qm_try_stop_vfs(qm, QM_PF_SRST_PREPARE, QM_SOFT_RESET);
	if (ret)
		pci_err(pdev, "failed to stop vfs by pf in soft reset.\n");

	ret = hisi_qm_stop(qm, QM_SOFT_RESET);
	if (ret) {
		pci_err(pdev, "Fails to stop QM!\n");
		qm_reset_bit_clear(qm);
		return ret;
	}

	ret = qm_wait_vf_prepare_finish(qm);
	if (ret)
		pci_err(pdev, "failed to stop by vfs in soft reset!\n");

	clear_bit(QM_RST_SCHED, &qm->misc_ctl);

	return 0;
}

static void qm_dev_ecc_mbit_handle(struct hisi_qm *qm)
{
	u32 nfe_enb = 0;

	/* Kunpeng930 hardware automatically close master ooo when NFE occurs */
	if (qm->ver >= QM_HW_V3)
		return;

	if (!qm->err_status.is_dev_ecc_mbit &&
	    qm->err_status.is_qm_ecc_mbit &&
	    qm->err_ini->close_axi_master_ooo) {

		qm->err_ini->close_axi_master_ooo(qm);

	} else if (qm->err_status.is_dev_ecc_mbit &&
		   !qm->err_status.is_qm_ecc_mbit &&
		   !qm->err_ini->close_axi_master_ooo) {

		nfe_enb = readl(qm->io_base + QM_RAS_NFE_ENABLE);
		writel(nfe_enb & QM_RAS_NFE_MBIT_DISABLE,
		       qm->io_base + QM_RAS_NFE_ENABLE);
		writel(QM_ECC_MBIT, qm->io_base + QM_ABNORMAL_INT_SET);
	}
}

static int qm_soft_reset(struct hisi_qm *qm)
{
	struct pci_dev *pdev = qm->pdev;
	int ret;
	u32 val;

	/* Ensure all doorbells and mailboxes received by QM */
	ret = qm_check_req_recv(qm);
	if (ret)
		return ret;

	if (qm->vfs_num) {
		ret = qm_set_vf_mse(qm, false);
		if (ret) {
			pci_err(pdev, "Fails to disable vf MSE bit.\n");
			return ret;
		}
	}

	ret = qm->ops->set_msi(qm, false);
	if (ret) {
		pci_err(pdev, "Fails to disable PEH MSI bit.\n");
		return ret;
	}

	qm_dev_ecc_mbit_handle(qm);

	/* OOO register set and check */
	writel(ACC_MASTER_GLOBAL_CTRL_SHUTDOWN,
	       qm->io_base + ACC_MASTER_GLOBAL_CTRL);

	/* If bus lock, reset chip */
	ret = readl_relaxed_poll_timeout(qm->io_base + ACC_MASTER_TRANS_RETURN,
					 val,
					 (val == ACC_MASTER_TRANS_RETURN_RW),
					 POLL_PERIOD, POLL_TIMEOUT);
	if (ret) {
		pci_emerg(pdev, "Bus lock! Please reset system.\n");
		return ret;
	}

	if (qm->err_ini->close_sva_prefetch)
		qm->err_ini->close_sva_prefetch(qm);

	ret = qm_set_pf_mse(qm, false);
	if (ret) {
		pci_err(pdev, "Fails to disable pf MSE bit.\n");
		return ret;
	}

	/* The reset related sub-control registers are not in PCI BAR */
	if (ACPI_HANDLE(&pdev->dev)) {
		unsigned long long value = 0;
		acpi_status s;

		s = acpi_evaluate_integer(ACPI_HANDLE(&pdev->dev),
					  qm->err_info.acpi_rst,
					  NULL, &value);
		if (ACPI_FAILURE(s)) {
			pci_err(pdev, "NO controller reset method!\n");
			return -EIO;
		}

		if (value) {
			pci_err(pdev, "Reset step %llu failed!\n", value);
			return -EIO;
		}
	} else {
		pci_err(pdev, "No reset method!\n");
		return -EINVAL;
	}

	return 0;
}

static int qm_vf_reset_done(struct hisi_qm *qm)
{
	struct hisi_qm_list *qm_list = qm->qm_list;
	struct pci_dev *pdev = qm->pdev;
	struct pci_dev *virtfn;
	struct hisi_qm *vf_qm;
	int ret = 0;

	mutex_lock(&qm_list->lock);
	list_for_each_entry(vf_qm, &qm_list->list, list) {
		virtfn = vf_qm->pdev;
		if (virtfn == pdev)
			continue;

		if (pci_physfn(virtfn) == pdev) {
			/* enable VFs PCIE BAR configuration */
			pci_restore_state(virtfn);

			ret = qm_restart(vf_qm);
			if (ret)
				goto restart_fail;
		}
	}

restart_fail:
	mutex_unlock(&qm_list->lock);
	return ret;
}

static int qm_try_start_vfs(struct hisi_qm *qm, enum qm_mb_cmd cmd)
{
	struct pci_dev *pdev = qm->pdev;
	int ret;

	if (!qm->vfs_num)
		return 0;

	ret = qm_vf_q_assign(qm, qm->vfs_num);
	if (ret) {
		pci_err(pdev, "failed to assign VFs, ret = %d.\n", ret);
		return ret;
	}

	/* Kunpeng930 supports to notify VFs to start after PF reset. */
	if (qm->ops->ping_all_vfs) {
		ret = qm->ops->ping_all_vfs(qm, cmd);
		if (ret)
			pci_warn(pdev, "failed to send cmd to all VFs after PF reset!\n");
	} else {
		ret = qm_vf_reset_done(qm);
		if (ret)
			pci_warn(pdev, "failed to start vfs, ret = %d.\n", ret);
	}

	return ret;
}

static int qm_dev_hw_init(struct hisi_qm *qm)
{
	return qm->err_ini->hw_init(qm);
}

static void qm_restart_prepare(struct hisi_qm *qm)
{
	u32 value;

	if (qm->err_ini->open_sva_prefetch)
		qm->err_ini->open_sva_prefetch(qm);

	if (qm->ver >= QM_HW_V3)
		return;

	if (!qm->err_status.is_qm_ecc_mbit &&
	    !qm->err_status.is_dev_ecc_mbit)
		return;

	/* temporarily close the OOO port used for PEH to write out MSI */
	value = readl(qm->io_base + ACC_AM_CFG_PORT_WR_EN);
	writel(value & ~qm->err_info.msi_wr_port,
	       qm->io_base + ACC_AM_CFG_PORT_WR_EN);

	/* clear dev ecc 2bit error source if having */
	value = qm_get_dev_err_status(qm) & qm->err_info.ecc_2bits_mask;
	if (value && qm->err_ini->clear_dev_hw_err_status)
		qm->err_ini->clear_dev_hw_err_status(qm, value);

	/* clear QM ecc mbit error source */
	writel(QM_ECC_MBIT, qm->io_base + QM_ABNORMAL_INT_SOURCE);

	/* clear AM Reorder Buffer ecc mbit source */
	writel(ACC_ROB_ECC_ERR_MULTPL, qm->io_base + ACC_AM_ROB_ECC_INT_STS);
}

static void qm_restart_done(struct hisi_qm *qm)
{
	u32 value;

	if (qm->ver >= QM_HW_V3)
		goto clear_flags;

	if (!qm->err_status.is_qm_ecc_mbit &&
	    !qm->err_status.is_dev_ecc_mbit)
		return;

	/* open the OOO port for PEH to write out MSI */
	value = readl(qm->io_base + ACC_AM_CFG_PORT_WR_EN);
	value |= qm->err_info.msi_wr_port;
	writel(value, qm->io_base + ACC_AM_CFG_PORT_WR_EN);

clear_flags:
	qm->err_status.is_qm_ecc_mbit = false;
	qm->err_status.is_dev_ecc_mbit = false;
}

static int qm_controller_reset_done(struct hisi_qm *qm)
{
	struct pci_dev *pdev = qm->pdev;
	int ret;

	ret = qm->ops->set_msi(qm, true);
	if (ret) {
		pci_err(pdev, "Fails to enable PEH MSI bit!\n");
		return ret;
	}

	ret = qm_set_pf_mse(qm, true);
	if (ret) {
		pci_err(pdev, "Fails to enable pf MSE bit!\n");
		return ret;
	}

	if (qm->vfs_num) {
		ret = qm_set_vf_mse(qm, true);
		if (ret) {
			pci_err(pdev, "Fails to enable vf MSE bit!\n");
			return ret;
		}
	}

	ret = qm_dev_hw_init(qm);
	if (ret) {
		pci_err(pdev, "Failed to init device\n");
		return ret;
	}

	qm_restart_prepare(qm);
	hisi_qm_dev_err_init(qm);
	if (qm->err_ini->open_axi_master_ooo)
		qm->err_ini->open_axi_master_ooo(qm);

	ret = qm_dev_mem_reset(qm);
	if (ret) {
		pci_err(pdev, "failed to reset device memory\n");
		return ret;
	}

	ret = qm_restart(qm);
	if (ret) {
		pci_err(pdev, "Failed to start QM!\n");
		return ret;
	}

	ret = qm_try_start_vfs(qm, QM_PF_RESET_DONE);
	if (ret)
		pci_err(pdev, "failed to start vfs by pf in soft reset.\n");

	ret = qm_wait_vf_prepare_finish(qm);
	if (ret)
		pci_err(pdev, "failed to start by vfs in soft reset!\n");

	qm_cmd_init(qm);
	qm_restart_done(qm);

	qm_reset_bit_clear(qm);

	return 0;
}

static int qm_controller_reset(struct hisi_qm *qm)
{
	struct pci_dev *pdev = qm->pdev;
	int ret;

	pci_info(pdev, "Controller resetting...\n");

	ret = qm_controller_reset_prepare(qm);
	if (ret) {
		hisi_qm_set_hw_reset(qm, QM_RESET_STOP_TX_OFFSET);
		hisi_qm_set_hw_reset(qm, QM_RESET_STOP_RX_OFFSET);
		clear_bit(QM_RST_SCHED, &qm->misc_ctl);
		return ret;
	}

	ret = qm_soft_reset(qm);
	if (ret) {
		pci_err(pdev, "Controller reset failed (%d)\n", ret);
		qm_reset_bit_clear(qm);
		return ret;
	}

	ret = qm_controller_reset_done(qm);
	if (ret) {
		qm_reset_bit_clear(qm);
		return ret;
	}

	pci_info(pdev, "Controller reset complete\n");

	return 0;
}

/**
 * hisi_qm_dev_slot_reset() - slot reset
 * @pdev: the PCIe device
 *
 * This function offers QM relate PCIe device reset interface. Drivers which
 * use QM can use this function as slot_reset in its struct pci_error_handlers.
 */
pci_ers_result_t hisi_qm_dev_slot_reset(struct pci_dev *pdev)
{
	struct hisi_qm *qm = pci_get_drvdata(pdev);
	int ret;

	if (pdev->is_virtfn)
		return PCI_ERS_RESULT_RECOVERED;

	pci_aer_clear_nonfatal_status(pdev);

	/* reset pcie device controller */
	ret = qm_controller_reset(qm);
	if (ret) {
		pci_err(pdev, "Controller reset failed (%d)\n", ret);
		return PCI_ERS_RESULT_DISCONNECT;
	}

	return PCI_ERS_RESULT_RECOVERED;
}
EXPORT_SYMBOL_GPL(hisi_qm_dev_slot_reset);

void hisi_qm_reset_prepare(struct pci_dev *pdev)
{
	struct hisi_qm *pf_qm = pci_get_drvdata(pci_physfn(pdev));
	struct hisi_qm *qm = pci_get_drvdata(pdev);
	u32 delay = 0;
	int ret;

	hisi_qm_dev_err_uninit(pf_qm);

	/*
	 * Check whether there is an ECC mbit error, If it occurs, need to
	 * wait for soft reset to fix it.
	 */
	while (qm_check_dev_error(pf_qm)) {
		msleep(++delay);
		if (delay > QM_RESET_WAIT_TIMEOUT)
			return;
	}

	ret = qm_reset_prepare_ready(qm);
	if (ret) {
		pci_err(pdev, "FLR not ready!\n");
		return;
	}

	/* PF obtains the information of VF by querying the register. */
	if (qm->fun_type == QM_HW_PF)
		qm_cmd_uninit(qm);

	ret = qm_try_stop_vfs(qm, QM_PF_FLR_PREPARE, QM_FLR);
	if (ret)
		pci_err(pdev, "failed to stop vfs by pf in FLR.\n");

	ret = hisi_qm_stop(qm, QM_FLR);
	if (ret) {
		pci_err(pdev, "Failed to stop QM, ret = %d.\n", ret);
		hisi_qm_set_hw_reset(qm, QM_RESET_STOP_TX_OFFSET);
		hisi_qm_set_hw_reset(qm, QM_RESET_STOP_RX_OFFSET);
		return;
	}

	ret = qm_wait_vf_prepare_finish(qm);
	if (ret)
		pci_err(pdev, "failed to stop by vfs in FLR!\n");

	pci_info(pdev, "FLR resetting...\n");
}
EXPORT_SYMBOL_GPL(hisi_qm_reset_prepare);

static bool qm_flr_reset_complete(struct pci_dev *pdev)
{
	struct pci_dev *pf_pdev = pci_physfn(pdev);
	struct hisi_qm *qm = pci_get_drvdata(pf_pdev);
	u32 id;

	pci_read_config_dword(qm->pdev, PCI_COMMAND, &id);
	if (id == QM_PCI_COMMAND_INVALID) {
		pci_err(pdev, "Device can not be used!\n");
		return false;
	}

	return true;
}

void hisi_qm_reset_done(struct pci_dev *pdev)
{
	struct hisi_qm *pf_qm = pci_get_drvdata(pci_physfn(pdev));
	struct hisi_qm *qm = pci_get_drvdata(pdev);
	int ret;

	if (qm->fun_type == QM_HW_PF) {
		ret = qm_dev_hw_init(qm);
		if (ret) {
			pci_err(pdev, "Failed to init PF, ret = %d.\n", ret);
			goto flr_done;
		}
	}

	hisi_qm_dev_err_init(pf_qm);

	ret = qm_restart(qm);
	if (ret) {
		pci_err(pdev, "Failed to start QM, ret = %d.\n", ret);
		goto flr_done;
	}

	ret = qm_try_start_vfs(qm, QM_PF_RESET_DONE);
	if (ret)
		pci_err(pdev, "failed to start vfs by pf in FLR.\n");

	ret = qm_wait_vf_prepare_finish(qm);
	if (ret)
		pci_err(pdev, "failed to start by vfs in FLR!\n");

flr_done:
	if (qm->fun_type == QM_HW_PF)
		qm_cmd_init(qm);

	if (qm_flr_reset_complete(pdev))
		pci_info(pdev, "FLR reset complete\n");

	qm_reset_bit_clear(qm);
}
EXPORT_SYMBOL_GPL(hisi_qm_reset_done);

static irqreturn_t qm_abnormal_irq(int irq, void *data)
{
	struct hisi_qm *qm = data;
	enum acc_err_result ret;

	atomic64_inc(&qm->debug.dfx.abnormal_irq_cnt);
	ret = qm_process_dev_error(qm);
	if (ret == ACC_ERR_NEED_RESET &&
	    !test_bit(QM_DRIVER_REMOVING, &qm->misc_ctl) &&
	    !test_and_set_bit(QM_RST_SCHED, &qm->misc_ctl))
		schedule_work(&qm->rst_work);

	return IRQ_HANDLED;
}

static int qm_irq_register(struct hisi_qm *qm)
{
	struct pci_dev *pdev = qm->pdev;
	int ret;

	ret = request_irq(pci_irq_vector(pdev, QM_EQ_EVENT_IRQ_VECTOR),
			  qm_irq, 0, qm->dev_name, qm);
	if (ret)
		return ret;

	if (qm->ver > QM_HW_V1) {
		ret = request_threaded_irq(pci_irq_vector(pdev,
					   QM_AEQ_EVENT_IRQ_VECTOR),
					   qm_aeq_irq, qm_aeq_thread,
					   0, qm->dev_name, qm);
		if (ret)
			goto err_aeq_irq;

		if (qm->fun_type == QM_HW_PF) {
			ret = request_irq(pci_irq_vector(pdev,
					  QM_ABNORMAL_EVENT_IRQ_VECTOR),
					  qm_abnormal_irq, 0, qm->dev_name, qm);
			if (ret)
				goto err_abonormal_irq;
		}
	}

	if (qm->ver > QM_HW_V2) {
		ret = request_irq(pci_irq_vector(pdev, QM_CMD_EVENT_IRQ_VECTOR),
				qm_mb_cmd_irq, 0, qm->dev_name, qm);
		if (ret)
			goto err_mb_cmd_irq;
	}

	return 0;

err_mb_cmd_irq:
	if (qm->fun_type == QM_HW_PF)
		free_irq(pci_irq_vector(pdev, QM_ABNORMAL_EVENT_IRQ_VECTOR), qm);
err_abonormal_irq:
	free_irq(pci_irq_vector(pdev, QM_AEQ_EVENT_IRQ_VECTOR), qm);
err_aeq_irq:
	free_irq(pci_irq_vector(pdev, QM_EQ_EVENT_IRQ_VECTOR), qm);
	return ret;
}

/**
 * hisi_qm_dev_shutdown() - Shutdown device.
 * @pdev: The device will be shutdown.
 *
 * This function will stop qm when OS shutdown or rebooting.
 */
void hisi_qm_dev_shutdown(struct pci_dev *pdev)
{
	struct hisi_qm *qm = pci_get_drvdata(pdev);
	int ret;

	ret = hisi_qm_stop(qm, QM_NORMAL);
	if (ret)
		dev_err(&pdev->dev, "Fail to stop qm in shutdown!\n");
}
EXPORT_SYMBOL_GPL(hisi_qm_dev_shutdown);

static void hisi_qm_controller_reset(struct work_struct *rst_work)
{
	struct hisi_qm *qm = container_of(rst_work, struct hisi_qm, rst_work);
	int ret;

	ret = qm_pm_get_sync(qm);
	if (ret) {
		clear_bit(QM_RST_SCHED, &qm->misc_ctl);
		return;
	}

	/* reset pcie device controller */
	ret = qm_controller_reset(qm);
	if (ret)
		dev_err(&qm->pdev->dev, "controller reset failed (%d)\n", ret);

	qm_pm_put_sync(qm);
}

static void qm_pf_reset_vf_prepare(struct hisi_qm *qm,
				   enum qm_stop_reason stop_reason)
{
	enum qm_mb_cmd cmd = QM_VF_PREPARE_DONE;
	struct pci_dev *pdev = qm->pdev;
	int ret;

	ret = qm_reset_prepare_ready(qm);
	if (ret) {
		dev_err(&pdev->dev, "reset prepare not ready!\n");
		atomic_set(&qm->status.flags, QM_STOP);
		cmd = QM_VF_PREPARE_FAIL;
		goto err_prepare;
	}

	ret = hisi_qm_stop(qm, stop_reason);
	if (ret) {
		dev_err(&pdev->dev, "failed to stop QM, ret = %d.\n", ret);
		atomic_set(&qm->status.flags, QM_STOP);
		cmd = QM_VF_PREPARE_FAIL;
		goto err_prepare;
	} else {
		goto out;
	}

err_prepare:
	hisi_qm_set_hw_reset(qm, QM_RESET_STOP_TX_OFFSET);
	hisi_qm_set_hw_reset(qm, QM_RESET_STOP_RX_OFFSET);
out:
	pci_save_state(pdev);
	ret = qm->ops->ping_pf(qm, cmd);
	if (ret)
		dev_warn(&pdev->dev, "PF responds timeout in reset prepare!\n");
}

static void qm_pf_reset_vf_done(struct hisi_qm *qm)
{
	enum qm_mb_cmd cmd = QM_VF_START_DONE;
	struct pci_dev *pdev = qm->pdev;
	int ret;

	pci_restore_state(pdev);
	ret = hisi_qm_start(qm);
	if (ret) {
		dev_err(&pdev->dev, "failed to start QM, ret = %d.\n", ret);
		cmd = QM_VF_START_FAIL;
	}

	ret = qm->ops->ping_pf(qm, cmd);
	if (ret)
		dev_warn(&pdev->dev, "PF responds timeout in reset done!\n");

	qm_reset_bit_clear(qm);
}

static int qm_wait_pf_reset_finish(struct hisi_qm *qm)
{
	struct device *dev = &qm->pdev->dev;
	u32 val, cmd;
	u64 msg;
	int ret;

	/* Wait for reset to finish */
	ret = readl_relaxed_poll_timeout(qm->io_base + QM_IFC_INT_SOURCE_V, val,
					 val == BIT(0), QM_VF_RESET_WAIT_US,
					 QM_VF_RESET_WAIT_TIMEOUT_US);
	/* hardware completion status should be available by this time */
	if (ret) {
		dev_err(dev, "couldn't get reset done status from PF, timeout!\n");
		return -ETIMEDOUT;
	}

	/*
	 * Whether message is got successfully,
	 * VF needs to ack PF by clearing the interrupt.
	 */
	ret = qm_get_mb_cmd(qm, &msg, 0);
	qm_clear_cmd_interrupt(qm, 0);
	if (ret) {
		dev_err(dev, "failed to get msg from PF in reset done!\n");
		return ret;
	}

	cmd = msg & QM_MB_CMD_DATA_MASK;
	if (cmd != QM_PF_RESET_DONE) {
		dev_err(dev, "the cmd(%u) is not reset done!\n", cmd);
		ret = -EINVAL;
	}

	return ret;
}

static void qm_pf_reset_vf_process(struct hisi_qm *qm,
				   enum qm_stop_reason stop_reason)
{
	struct device *dev = &qm->pdev->dev;
	int ret;

	dev_info(dev, "device reset start...\n");

	/* The message is obtained by querying the register during resetting */
	qm_cmd_uninit(qm);
	qm_pf_reset_vf_prepare(qm, stop_reason);

	ret = qm_wait_pf_reset_finish(qm);
	if (ret)
		goto err_get_status;

	qm_pf_reset_vf_done(qm);
	qm_cmd_init(qm);

	dev_info(dev, "device reset done.\n");

	return;

err_get_status:
	qm_cmd_init(qm);
	qm_reset_bit_clear(qm);
}

static void qm_handle_cmd_msg(struct hisi_qm *qm, u32 fun_num)
{
	struct device *dev = &qm->pdev->dev;
	u64 msg;
	u32 cmd;
	int ret;

	/*
	 * Get the msg from source by sending mailbox. Whether message is got
	 * successfully, destination needs to ack source by clearing the interrupt.
	 */
	ret = qm_get_mb_cmd(qm, &msg, fun_num);
	qm_clear_cmd_interrupt(qm, BIT(fun_num));
	if (ret) {
		dev_err(dev, "failed to get msg from source!\n");
		return;
	}

	cmd = msg & QM_MB_CMD_DATA_MASK;
	switch (cmd) {
	case QM_PF_FLR_PREPARE:
		qm_pf_reset_vf_process(qm, QM_FLR);
		break;
	case QM_PF_SRST_PREPARE:
		qm_pf_reset_vf_process(qm, QM_SOFT_RESET);
		break;
	case QM_VF_GET_QOS:
		qm_vf_get_qos(qm, fun_num);
		break;
	case QM_PF_SET_QOS:
		qm->mb_qos = msg >> QM_MB_CMD_DATA_SHIFT;
		break;
	default:
		dev_err(dev, "unsupported cmd %u sent by function(%u)!\n", cmd, fun_num);
		break;
	}
}

static void qm_cmd_process(struct work_struct *cmd_process)
{
	struct hisi_qm *qm = container_of(cmd_process,
					struct hisi_qm, cmd_process);
	u32 vfs_num = qm->vfs_num;
	u64 val;
	u32 i;

	if (qm->fun_type == QM_HW_PF) {
		val = readq(qm->io_base + QM_IFC_INT_SOURCE_P);
		if (!val)
			return;

		for (i = 1; i <= vfs_num; i++) {
			if (val & BIT(i))
				qm_handle_cmd_msg(qm, i);
		}

		return;
	}

	qm_handle_cmd_msg(qm, 0);
}

/**
 * hisi_qm_alg_register() - Register alg to crypto and add qm to qm_list.
 * @qm: The qm needs add.
 * @qm_list: The qm list.
 *
 * This function adds qm to qm list, and will register algorithm to
 * crypto when the qm list is empty.
 */
int hisi_qm_alg_register(struct hisi_qm *qm, struct hisi_qm_list *qm_list)
{
	struct device *dev = &qm->pdev->dev;
	int flag = 0;
	int ret = 0;

	mutex_lock(&qm_list->lock);
	if (list_empty(&qm_list->list))
		flag = 1;
	list_add_tail(&qm->list, &qm_list->list);
	mutex_unlock(&qm_list->lock);

	if (qm->ver <= QM_HW_V2 && qm->use_sva) {
		dev_info(dev, "HW V2 not both use uacce sva mode and hardware crypto algs.\n");
		return 0;
	}

	if (flag) {
		ret = qm_list->register_to_crypto(qm);
		if (ret) {
			mutex_lock(&qm_list->lock);
			list_del(&qm->list);
			mutex_unlock(&qm_list->lock);
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(hisi_qm_alg_register);

/**
 * hisi_qm_alg_unregister() - Unregister alg from crypto and delete qm from
 * qm list.
 * @qm: The qm needs delete.
 * @qm_list: The qm list.
 *
 * This function deletes qm from qm list, and will unregister algorithm
 * from crypto when the qm list is empty.
 */
void hisi_qm_alg_unregister(struct hisi_qm *qm, struct hisi_qm_list *qm_list)
{
	mutex_lock(&qm_list->lock);
	list_del(&qm->list);
	mutex_unlock(&qm_list->lock);

	if (qm->ver <= QM_HW_V2 && qm->use_sva)
		return;

	if (list_empty(&qm_list->list))
		qm_list->unregister_from_crypto(qm);
}
EXPORT_SYMBOL_GPL(hisi_qm_alg_unregister);

static int qm_get_qp_num(struct hisi_qm *qm)
{
	if (qm->ver == QM_HW_V1)
		qm->ctrl_qp_num = QM_QNUM_V1;
	else if (qm->ver == QM_HW_V2)
		qm->ctrl_qp_num = QM_QNUM_V2;
	else
		qm->ctrl_qp_num = readl(qm->io_base + QM_CAPBILITY) &
					QM_QP_NUN_MASK;

	if (qm->use_db_isolation)
		qm->max_qp_num = (readl(qm->io_base + QM_CAPBILITY) >>
				  QM_QP_MAX_NUM_SHIFT) & QM_QP_NUN_MASK;
	else
		qm->max_qp_num = qm->ctrl_qp_num;

	/* check if qp number is valid */
	if (qm->qp_num > qm->max_qp_num) {
		dev_err(&qm->pdev->dev, "qp num(%u) is more than max qp num(%u)!\n",
			qm->qp_num, qm->max_qp_num);
		return -EINVAL;
	}

	return 0;
}

static int qm_get_pci_res(struct hisi_qm *qm)
{
	struct pci_dev *pdev = qm->pdev;
	struct device *dev = &pdev->dev;
	int ret;

	ret = pci_request_mem_regions(pdev, qm->dev_name);
	if (ret < 0) {
		dev_err(dev, "Failed to request mem regions!\n");
		return ret;
	}

	qm->phys_base = pci_resource_start(pdev, PCI_BAR_2);
	qm->io_base = ioremap(qm->phys_base, pci_resource_len(pdev, PCI_BAR_2));
	if (!qm->io_base) {
		ret = -EIO;
		goto err_request_mem_regions;
	}

	if (qm->ver > QM_HW_V2) {
		if (qm->fun_type == QM_HW_PF)
			qm->use_db_isolation = readl(qm->io_base +
						     QM_QUE_ISO_EN) & BIT(0);
		else
			qm->use_db_isolation = readl(qm->io_base +
						     QM_QUE_ISO_CFG_V) & BIT(0);
	}

	if (qm->use_db_isolation) {
		qm->db_interval = QM_QP_DB_INTERVAL;
		qm->db_phys_base = pci_resource_start(pdev, PCI_BAR_4);
		qm->db_io_base = ioremap(qm->db_phys_base,
					 pci_resource_len(pdev, PCI_BAR_4));
		if (!qm->db_io_base) {
			ret = -EIO;
			goto err_ioremap;
		}
	} else {
		qm->db_phys_base = qm->phys_base;
		qm->db_io_base = qm->io_base;
		qm->db_interval = 0;
	}

	if (qm->fun_type == QM_HW_PF) {
		ret = qm_get_qp_num(qm);
		if (ret)
			goto err_db_ioremap;
	}

	return 0;

err_db_ioremap:
	if (qm->use_db_isolation)
		iounmap(qm->db_io_base);
err_ioremap:
	iounmap(qm->io_base);
err_request_mem_regions:
	pci_release_mem_regions(pdev);
	return ret;
}

static int hisi_qm_pci_init(struct hisi_qm *qm)
{
	struct pci_dev *pdev = qm->pdev;
	struct device *dev = &pdev->dev;
	unsigned int num_vec;
	int ret;

	ret = pci_enable_device_mem(pdev);
	if (ret < 0) {
		dev_err(dev, "Failed to enable device mem!\n");
		return ret;
	}

	ret = qm_get_pci_res(qm);
	if (ret)
		goto err_disable_pcidev;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (ret < 0)
		goto err_get_pci_res;
	pci_set_master(pdev);

	if (!qm->ops->get_irq_num) {
		ret = -EOPNOTSUPP;
		goto err_get_pci_res;
	}
	num_vec = qm->ops->get_irq_num(qm);
	ret = pci_alloc_irq_vectors(pdev, num_vec, num_vec, PCI_IRQ_MSI);
	if (ret < 0) {
		dev_err(dev, "Failed to enable MSI vectors!\n");
		goto err_get_pci_res;
	}

	return 0;

err_get_pci_res:
	qm_put_pci_res(qm);
err_disable_pcidev:
	pci_disable_device(pdev);
	return ret;
}

static void hisi_qm_init_work(struct hisi_qm *qm)
{
	INIT_WORK(&qm->work, qm_work_process);
	if (qm->fun_type == QM_HW_PF)
		INIT_WORK(&qm->rst_work, hisi_qm_controller_reset);

	if (qm->ver > QM_HW_V2)
		INIT_WORK(&qm->cmd_process, qm_cmd_process);
}

static int hisi_qp_alloc_memory(struct hisi_qm *qm)
{
	struct device *dev = &qm->pdev->dev;
	size_t qp_dma_size;
	int i, ret;

	qm->qp_array = kcalloc(qm->qp_num, sizeof(struct hisi_qp), GFP_KERNEL);
	if (!qm->qp_array)
		return -ENOMEM;

	/* one more page for device or qp statuses */
	qp_dma_size = qm->sqe_size * QM_Q_DEPTH +
		      sizeof(struct qm_cqe) * QM_Q_DEPTH;
	qp_dma_size = PAGE_ALIGN(qp_dma_size) + PAGE_SIZE;
	for (i = 0; i < qm->qp_num; i++) {
		ret = hisi_qp_memory_init(qm, qp_dma_size, i);
		if (ret)
			goto err_init_qp_mem;

		dev_dbg(dev, "allocate qp dma buf size=%zx)\n", qp_dma_size);
	}

	return 0;
err_init_qp_mem:
	hisi_qp_memory_uninit(qm, i);

	return ret;
}

static int hisi_qm_memory_init(struct hisi_qm *qm)
{
	struct device *dev = &qm->pdev->dev;
	int ret, total_func, i;
	size_t off = 0;

	total_func = pci_sriov_get_totalvfs(qm->pdev) + 1;
	qm->factor = kcalloc(total_func, sizeof(struct qm_shaper_factor), GFP_KERNEL);
	if (!qm->factor)
		return -ENOMEM;
	for (i = 0; i < total_func; i++)
		qm->factor[i].func_qos = QM_QOS_MAX_VAL;

#define QM_INIT_BUF(qm, type, num) do { \
	(qm)->type = ((qm)->qdma.va + (off)); \
	(qm)->type##_dma = (qm)->qdma.dma + (off); \
	off += QMC_ALIGN(sizeof(struct qm_##type) * (num)); \
} while (0)

	idr_init(&qm->qp_idr);
	qm->qdma.size = QMC_ALIGN(sizeof(struct qm_eqe) * QM_EQ_DEPTH) +
			QMC_ALIGN(sizeof(struct qm_aeqe) * QM_Q_DEPTH) +
			QMC_ALIGN(sizeof(struct qm_sqc) * qm->qp_num) +
			QMC_ALIGN(sizeof(struct qm_cqc) * qm->qp_num);
	qm->qdma.va = dma_alloc_coherent(dev, qm->qdma.size, &qm->qdma.dma,
					 GFP_ATOMIC);
	dev_dbg(dev, "allocate qm dma buf size=%zx)\n", qm->qdma.size);
	if (!qm->qdma.va) {
		ret =  -ENOMEM;
		goto err_alloc_qdma;
	}

	QM_INIT_BUF(qm, eqe, QM_EQ_DEPTH);
	QM_INIT_BUF(qm, aeqe, QM_Q_DEPTH);
	QM_INIT_BUF(qm, sqc, qm->qp_num);
	QM_INIT_BUF(qm, cqc, qm->qp_num);

	ret = hisi_qp_alloc_memory(qm);
	if (ret)
		goto err_alloc_qp_array;

	return 0;

err_alloc_qp_array:
	dma_free_coherent(dev, qm->qdma.size, qm->qdma.va, qm->qdma.dma);
err_alloc_qdma:
	kfree(qm->factor);

	return ret;
}

/**
 * hisi_qm_init() - Initialize configures about qm.
 * @qm: The qm needing init.
 *
 * This function init qm, then we can call hisi_qm_start to put qm into work.
 */
int hisi_qm_init(struct hisi_qm *qm)
{
	struct pci_dev *pdev = qm->pdev;
	struct device *dev = &pdev->dev;
	int ret;

	hisi_qm_pre_init(qm);

	ret = hisi_qm_pci_init(qm);
	if (ret)
		return ret;

	ret = qm_irq_register(qm);
	if (ret)
		goto err_pci_init;

	if (qm->fun_type == QM_HW_VF && qm->ver != QM_HW_V1) {
		/* v2 starts to support get vft by mailbox */
		ret = hisi_qm_get_vft(qm, &qm->qp_base, &qm->qp_num);
		if (ret)
			goto err_irq_register;
	}

	if (qm->fun_type == QM_HW_PF) {
		qm_disable_clock_gate(qm);
		ret = qm_dev_mem_reset(qm);
		if (ret) {
			dev_err(dev, "failed to reset device memory\n");
			goto err_irq_register;
		}
	}

	if (qm->mode == UACCE_MODE_SVA) {
		ret = qm_alloc_uacce(qm);
		if (ret < 0)
			dev_warn(dev, "fail to alloc uacce (%d)\n", ret);
	}

	ret = hisi_qm_memory_init(qm);
	if (ret)
		goto err_alloc_uacce;

	hisi_qm_init_work(qm);
	qm_cmd_init(qm);
	atomic_set(&qm->status.flags, QM_INIT);

	return 0;

err_alloc_uacce:
	if (qm->use_sva) {
		uacce_remove(qm->uacce);
		qm->uacce = NULL;
	}
err_irq_register:
	qm_irq_unregister(qm);
err_pci_init:
	hisi_qm_pci_uninit(qm);
	return ret;
}
EXPORT_SYMBOL_GPL(hisi_qm_init);

/**
 * hisi_qm_get_dfx_access() - Try to get dfx access.
 * @qm: pointer to accelerator device.
 *
 * Try to get dfx access, then user can get message.
 *
 * If device is in suspended, return failure, otherwise
 * bump up the runtime PM usage counter.
 */
int hisi_qm_get_dfx_access(struct hisi_qm *qm)
{
	struct device *dev = &qm->pdev->dev;

	if (pm_runtime_suspended(dev)) {
		dev_info(dev, "can not read/write - device in suspended.\n");
		return -EAGAIN;
	}

	return qm_pm_get_sync(qm);
}
EXPORT_SYMBOL_GPL(hisi_qm_get_dfx_access);

/**
 * hisi_qm_put_dfx_access() - Put dfx access.
 * @qm: pointer to accelerator device.
 *
 * Put dfx access, drop runtime PM usage counter.
 */
void hisi_qm_put_dfx_access(struct hisi_qm *qm)
{
	qm_pm_put_sync(qm);
}
EXPORT_SYMBOL_GPL(hisi_qm_put_dfx_access);

/**
 * hisi_qm_pm_init() - Initialize qm runtime PM.
 * @qm: pointer to accelerator device.
 *
 * Function that initialize qm runtime PM.
 */
void hisi_qm_pm_init(struct hisi_qm *qm)
{
	struct device *dev = &qm->pdev->dev;

	if (qm->fun_type == QM_HW_VF || qm->ver < QM_HW_V3)
		return;

	pm_runtime_set_autosuspend_delay(dev, QM_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_put_noidle(dev);
}
EXPORT_SYMBOL_GPL(hisi_qm_pm_init);

/**
 * hisi_qm_pm_uninit() - Uninitialize qm runtime PM.
 * @qm: pointer to accelerator device.
 *
 * Function that uninitialize qm runtime PM.
 */
void hisi_qm_pm_uninit(struct hisi_qm *qm)
{
	struct device *dev = &qm->pdev->dev;

	if (qm->fun_type == QM_HW_VF || qm->ver < QM_HW_V3)
		return;

	pm_runtime_get_noresume(dev);
	pm_runtime_dont_use_autosuspend(dev);
}
EXPORT_SYMBOL_GPL(hisi_qm_pm_uninit);

static int qm_prepare_for_suspend(struct hisi_qm *qm)
{
	struct pci_dev *pdev = qm->pdev;
	int ret;
	u32 val;

	ret = qm->ops->set_msi(qm, false);
	if (ret) {
		pci_err(pdev, "failed to disable MSI before suspending!\n");
		return ret;
	}

	/* shutdown OOO register */
	writel(ACC_MASTER_GLOBAL_CTRL_SHUTDOWN,
	       qm->io_base + ACC_MASTER_GLOBAL_CTRL);

	ret = readl_relaxed_poll_timeout(qm->io_base + ACC_MASTER_TRANS_RETURN,
					 val,
					 (val == ACC_MASTER_TRANS_RETURN_RW),
					 POLL_PERIOD, POLL_TIMEOUT);
	if (ret) {
		pci_emerg(pdev, "Bus lock! Please reset system.\n");
		return ret;
	}

	ret = qm_set_pf_mse(qm, false);
	if (ret)
		pci_err(pdev, "failed to disable MSE before suspending!\n");

	return ret;
}

static int qm_rebuild_for_resume(struct hisi_qm *qm)
{
	struct pci_dev *pdev = qm->pdev;
	int ret;

	ret = qm_set_pf_mse(qm, true);
	if (ret) {
		pci_err(pdev, "failed to enable MSE after resuming!\n");
		return ret;
	}

	ret = qm->ops->set_msi(qm, true);
	if (ret) {
		pci_err(pdev, "failed to enable MSI after resuming!\n");
		return ret;
	}

	ret = qm_dev_hw_init(qm);
	if (ret) {
		pci_err(pdev, "failed to init device after resuming\n");
		return ret;
	}

	qm_cmd_init(qm);
	hisi_qm_dev_err_init(qm);
	qm_disable_clock_gate(qm);
	ret = qm_dev_mem_reset(qm);
	if (ret)
		pci_err(pdev, "failed to reset device memory\n");

	return ret;
}

/**
 * hisi_qm_suspend() - Runtime suspend of given device.
 * @dev: device to suspend.
 *
 * Function that suspend the device.
 */
int hisi_qm_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct hisi_qm *qm = pci_get_drvdata(pdev);
	int ret;

	pci_info(pdev, "entering suspended state\n");

	ret = hisi_qm_stop(qm, QM_NORMAL);
	if (ret) {
		pci_err(pdev, "failed to stop qm(%d)\n", ret);
		return ret;
	}

	ret = qm_prepare_for_suspend(qm);
	if (ret)
		pci_err(pdev, "failed to prepare suspended(%d)\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(hisi_qm_suspend);

/**
 * hisi_qm_resume() - Runtime resume of given device.
 * @dev: device to resume.
 *
 * Function that resume the device.
 */
int hisi_qm_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct hisi_qm *qm = pci_get_drvdata(pdev);
	int ret;

	pci_info(pdev, "resuming from suspend state\n");

	ret = qm_rebuild_for_resume(qm);
	if (ret) {
		pci_err(pdev, "failed to rebuild resume(%d)\n", ret);
		return ret;
	}

	ret = hisi_qm_start(qm);
	if (ret)
		pci_err(pdev, "failed to start qm(%d)\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(hisi_qm_resume);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Zhou Wang <wangzhou1@hisilicon.com>");
MODULE_DESCRIPTION("HiSilicon Accelerator queue manager driver");
