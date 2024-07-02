// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2019-2022 HiSilicon Limited. */

#include <linux/bitfield.h>
#include <linux/dmaengine.h>
#include <linux/init.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include "virt-dma.h"

/* HiSilicon DMA register common field define */
#define HISI_DMA_Q_SQ_BASE_L			0x0
#define HISI_DMA_Q_SQ_BASE_H			0x4
#define HISI_DMA_Q_SQ_DEPTH			0x8
#define HISI_DMA_Q_SQ_TAIL_PTR			0xc
#define HISI_DMA_Q_CQ_BASE_L			0x10
#define HISI_DMA_Q_CQ_BASE_H			0x14
#define HISI_DMA_Q_CQ_DEPTH			0x18
#define HISI_DMA_Q_CQ_HEAD_PTR			0x1c
#define HISI_DMA_Q_CTRL0			0x20
#define HISI_DMA_Q_CTRL0_QUEUE_EN		BIT(0)
#define HISI_DMA_Q_CTRL0_QUEUE_PAUSE		BIT(4)
#define HISI_DMA_Q_CTRL1			0x24
#define HISI_DMA_Q_CTRL1_QUEUE_RESET		BIT(0)
#define HISI_DMA_Q_FSM_STS			0x30
#define HISI_DMA_Q_FSM_STS_MASK			GENMASK(3, 0)
#define HISI_DMA_Q_ERR_INT_NUM0			0x84
#define HISI_DMA_Q_ERR_INT_NUM1			0x88
#define HISI_DMA_Q_ERR_INT_NUM2			0x8c

/* HiSilicon IP08 DMA register and field define */
#define HISI_DMA_HIP08_MODE			0x217C
#define HISI_DMA_HIP08_Q_BASE			0x0
#define HISI_DMA_HIP08_Q_CTRL0_ERR_ABORT_EN	BIT(2)
#define HISI_DMA_HIP08_Q_INT_STS		0x40
#define HISI_DMA_HIP08_Q_INT_MSK		0x44
#define HISI_DMA_HIP08_Q_INT_STS_MASK		GENMASK(14, 0)
#define HISI_DMA_HIP08_Q_ERR_INT_NUM3		0x90
#define HISI_DMA_HIP08_Q_ERR_INT_NUM4		0x94
#define HISI_DMA_HIP08_Q_ERR_INT_NUM5		0x98
#define HISI_DMA_HIP08_Q_ERR_INT_NUM6		0x48
#define HISI_DMA_HIP08_Q_CTRL0_SQCQ_DRCT	BIT(24)

/* HiSilicon IP09 DMA register and field define */
#define HISI_DMA_HIP09_DMA_FLR_DISABLE		0xA00
#define HISI_DMA_HIP09_DMA_FLR_DISABLE_B	BIT(0)
#define HISI_DMA_HIP09_Q_BASE			0x2000
#define HISI_DMA_HIP09_Q_CTRL0_ERR_ABORT_EN	GENMASK(31, 28)
#define HISI_DMA_HIP09_Q_CTRL0_SQ_DRCT		BIT(26)
#define HISI_DMA_HIP09_Q_CTRL0_CQ_DRCT		BIT(27)
#define HISI_DMA_HIP09_Q_CTRL1_VA_ENABLE	BIT(2)
#define HISI_DMA_HIP09_Q_INT_STS		0x40
#define HISI_DMA_HIP09_Q_INT_MSK		0x44
#define HISI_DMA_HIP09_Q_INT_STS_MASK		0x1
#define HISI_DMA_HIP09_Q_ERR_INT_STS		0x48
#define HISI_DMA_HIP09_Q_ERR_INT_MSK		0x4C
#define HISI_DMA_HIP09_Q_ERR_INT_STS_MASK	GENMASK(18, 1)
#define HISI_DMA_HIP09_PORT_CFG_REG(port_id)	(0x800 + \
						(port_id) * 0x20)
#define HISI_DMA_HIP09_PORT_CFG_LINK_DOWN_MASK_B	BIT(16)

#define HISI_DMA_HIP09_MAX_PORT_NUM		16

#define HISI_DMA_HIP08_MSI_NUM			32
#define HISI_DMA_HIP08_CHAN_NUM			30
#define HISI_DMA_HIP09_MSI_NUM			4
#define HISI_DMA_HIP09_CHAN_NUM			4
#define HISI_DMA_REVISION_HIP08B		0x21
#define HISI_DMA_REVISION_HIP09A		0x30

#define HISI_DMA_Q_OFFSET			0x100
#define HISI_DMA_Q_DEPTH_VAL			1024

#define PCI_BAR_2				2

#define HISI_DMA_POLL_Q_STS_DELAY_US		10
#define HISI_DMA_POLL_Q_STS_TIME_OUT_US		1000

#define HISI_DMA_MAX_DIR_NAME_LEN		128

/*
 * The HIP08B(HiSilicon IP08) and HIP09A(HiSilicon IP09) are DMA iEPs, they
 * have the same pci device id but different pci revision.
 * Unfortunately, they have different register layouts, so two layout
 * enumerations are defined.
 */
enum hisi_dma_reg_layout {
	HISI_DMA_REG_LAYOUT_INVALID = 0,
	HISI_DMA_REG_LAYOUT_HIP08,
	HISI_DMA_REG_LAYOUT_HIP09
};

enum hisi_dma_mode {
	EP = 0,
	RC,
};

enum hisi_dma_chan_status {
	DISABLE = -1,
	IDLE = 0,
	RUN,
	CPL,
	PAUSE,
	HALT,
	ABORT,
	WAIT,
	BUFFCLR,
};

struct hisi_dma_sqe {
	__le32 dw0;
#define OPCODE_MASK			GENMASK(3, 0)
#define OPCODE_SMALL_PACKAGE		0x1
#define OPCODE_M2M			0x4
#define LOCAL_IRQ_EN			BIT(8)
#define ATTR_SRC_MASK			GENMASK(14, 12)
	__le32 dw1;
	__le32 dw2;
#define ATTR_DST_MASK			GENMASK(26, 24)
	__le32 length;
	__le64 src_addr;
	__le64 dst_addr;
};

struct hisi_dma_cqe {
	__le32 rsv0;
	__le32 rsv1;
	__le16 sq_head;
	__le16 rsv2;
	__le16 rsv3;
	__le16 w0;
#define STATUS_MASK			GENMASK(15, 1)
#define STATUS_SUCC			0x0
#define VALID_BIT			BIT(0)
};

struct hisi_dma_desc {
	struct virt_dma_desc vd;
	struct hisi_dma_sqe sqe;
};

struct hisi_dma_chan {
	struct virt_dma_chan vc;
	struct hisi_dma_dev *hdma_dev;
	struct hisi_dma_sqe *sq;
	struct hisi_dma_cqe *cq;
	dma_addr_t sq_dma;
	dma_addr_t cq_dma;
	u32 sq_tail;
	u32 cq_head;
	u32 qp_num;
	enum hisi_dma_chan_status status;
	struct hisi_dma_desc *desc;
};

struct hisi_dma_dev {
	struct pci_dev *pdev;
	void __iomem *base;
	struct dma_device dma_dev;
	u32 chan_num;
	u32 chan_depth;
	enum hisi_dma_reg_layout reg_layout;
	void __iomem *queue_base; /* queue region start of register */
	struct hisi_dma_chan chan[] __counted_by(chan_num);
};

#ifdef CONFIG_DEBUG_FS

static const struct debugfs_reg32 hisi_dma_comm_chan_regs[] = {
	{"DMA_QUEUE_SQ_DEPTH                ", 0x0008ull},
	{"DMA_QUEUE_SQ_TAIL_PTR             ", 0x000Cull},
	{"DMA_QUEUE_CQ_DEPTH                ", 0x0018ull},
	{"DMA_QUEUE_CQ_HEAD_PTR             ", 0x001Cull},
	{"DMA_QUEUE_CTRL0                   ", 0x0020ull},
	{"DMA_QUEUE_CTRL1                   ", 0x0024ull},
	{"DMA_QUEUE_FSM_STS                 ", 0x0030ull},
	{"DMA_QUEUE_SQ_STS                  ", 0x0034ull},
	{"DMA_QUEUE_CQ_TAIL_PTR             ", 0x003Cull},
	{"DMA_QUEUE_INT_STS                 ", 0x0040ull},
	{"DMA_QUEUE_INT_MSK                 ", 0x0044ull},
	{"DMA_QUEUE_INT_RO                  ", 0x006Cull},
};

static const struct debugfs_reg32 hisi_dma_hip08_chan_regs[] = {
	{"DMA_QUEUE_BYTE_CNT                ", 0x0038ull},
	{"DMA_ERR_INT_NUM6                  ", 0x0048ull},
	{"DMA_QUEUE_DESP0                   ", 0x0050ull},
	{"DMA_QUEUE_DESP1                   ", 0x0054ull},
	{"DMA_QUEUE_DESP2                   ", 0x0058ull},
	{"DMA_QUEUE_DESP3                   ", 0x005Cull},
	{"DMA_QUEUE_DESP4                   ", 0x0074ull},
	{"DMA_QUEUE_DESP5                   ", 0x0078ull},
	{"DMA_QUEUE_DESP6                   ", 0x007Cull},
	{"DMA_QUEUE_DESP7                   ", 0x0080ull},
	{"DMA_ERR_INT_NUM0                  ", 0x0084ull},
	{"DMA_ERR_INT_NUM1                  ", 0x0088ull},
	{"DMA_ERR_INT_NUM2                  ", 0x008Cull},
	{"DMA_ERR_INT_NUM3                  ", 0x0090ull},
	{"DMA_ERR_INT_NUM4                  ", 0x0094ull},
	{"DMA_ERR_INT_NUM5                  ", 0x0098ull},
	{"DMA_QUEUE_SQ_STS2                 ", 0x00A4ull},
};

static const struct debugfs_reg32 hisi_dma_hip09_chan_regs[] = {
	{"DMA_QUEUE_ERR_INT_STS             ", 0x0048ull},
	{"DMA_QUEUE_ERR_INT_MSK             ", 0x004Cull},
	{"DFX_SQ_READ_ERR_PTR               ", 0x0068ull},
	{"DFX_DMA_ERR_INT_NUM0              ", 0x0084ull},
	{"DFX_DMA_ERR_INT_NUM1              ", 0x0088ull},
	{"DFX_DMA_ERR_INT_NUM2              ", 0x008Cull},
	{"DFX_DMA_QUEUE_SQ_STS2             ", 0x00A4ull},
};

static const struct debugfs_reg32 hisi_dma_hip08_comm_regs[] = {
	{"DMA_ECC_ERR_ADDR                  ", 0x2004ull},
	{"DMA_ECC_ECC_CNT                   ", 0x2014ull},
	{"COMMON_AND_CH_ERR_STS             ", 0x2030ull},
	{"LOCAL_CPL_ID_STS_0                ", 0x20E0ull},
	{"LOCAL_CPL_ID_STS_1                ", 0x20E4ull},
	{"LOCAL_CPL_ID_STS_2                ", 0x20E8ull},
	{"LOCAL_CPL_ID_STS_3                ", 0x20ECull},
	{"LOCAL_TLP_NUM                     ", 0x2158ull},
	{"SQCQ_TLP_NUM                      ", 0x2164ull},
	{"CPL_NUM                           ", 0x2168ull},
	{"INF_BACK_PRESS_STS                ", 0x2170ull},
	{"DMA_CH_RAS_LEVEL                  ", 0x2184ull},
	{"DMA_CM_RAS_LEVEL                  ", 0x2188ull},
	{"DMA_CH_ERR_STS                    ", 0x2190ull},
	{"DMA_CH_DONE_STS                   ", 0x2194ull},
	{"DMA_SQ_TAG_STS_0                  ", 0x21A0ull},
	{"DMA_SQ_TAG_STS_1                  ", 0x21A4ull},
	{"DMA_SQ_TAG_STS_2                  ", 0x21A8ull},
	{"DMA_SQ_TAG_STS_3                  ", 0x21ACull},
	{"LOCAL_P_ID_STS_0                  ", 0x21B0ull},
	{"LOCAL_P_ID_STS_1                  ", 0x21B4ull},
	{"LOCAL_P_ID_STS_2                  ", 0x21B8ull},
	{"LOCAL_P_ID_STS_3                  ", 0x21BCull},
	{"DMA_PREBUFF_INFO_0                ", 0x2200ull},
	{"DMA_CM_TABLE_INFO_0               ", 0x2220ull},
	{"DMA_CM_CE_RO                      ", 0x2244ull},
	{"DMA_CM_NFE_RO                     ", 0x2248ull},
	{"DMA_CM_FE_RO                      ", 0x224Cull},
};

static const struct debugfs_reg32 hisi_dma_hip09_comm_regs[] = {
	{"COMMON_AND_CH_ERR_STS             ", 0x0030ull},
	{"DMA_PORT_IDLE_STS                 ", 0x0150ull},
	{"DMA_CH_RAS_LEVEL                  ", 0x0184ull},
	{"DMA_CM_RAS_LEVEL                  ", 0x0188ull},
	{"DMA_CM_CE_RO                      ", 0x0244ull},
	{"DMA_CM_NFE_RO                     ", 0x0248ull},
	{"DMA_CM_FE_RO                      ", 0x024Cull},
	{"DFX_INF_BACK_PRESS_STS0           ", 0x1A40ull},
	{"DFX_INF_BACK_PRESS_STS1           ", 0x1A44ull},
	{"DFX_INF_BACK_PRESS_STS2           ", 0x1A48ull},
	{"DFX_DMA_WRR_DISABLE               ", 0x1A4Cull},
	{"DFX_PA_REQ_TLP_NUM                ", 0x1C00ull},
	{"DFX_PA_BACK_TLP_NUM               ", 0x1C04ull},
	{"DFX_PA_RETRY_TLP_NUM              ", 0x1C08ull},
	{"DFX_LOCAL_NP_TLP_NUM              ", 0x1C0Cull},
	{"DFX_LOCAL_CPL_HEAD_TLP_NUM        ", 0x1C10ull},
	{"DFX_LOCAL_CPL_DATA_TLP_NUM        ", 0x1C14ull},
	{"DFX_LOCAL_CPL_EXT_DATA_TLP_NUM    ", 0x1C18ull},
	{"DFX_LOCAL_P_HEAD_TLP_NUM          ", 0x1C1Cull},
	{"DFX_LOCAL_P_ACK_TLP_NUM           ", 0x1C20ull},
	{"DFX_BUF_ALOC_PORT_REQ_NUM         ", 0x1C24ull},
	{"DFX_BUF_ALOC_PORT_RESULT_NUM      ", 0x1C28ull},
	{"DFX_BUF_FAIL_SIZE_NUM             ", 0x1C2Cull},
	{"DFX_BUF_ALOC_SIZE_NUM             ", 0x1C30ull},
	{"DFX_BUF_NP_RELEASE_SIZE_NUM       ", 0x1C34ull},
	{"DFX_BUF_P_RELEASE_SIZE_NUM        ", 0x1C38ull},
	{"DFX_BUF_PORT_RELEASE_SIZE_NUM     ", 0x1C3Cull},
	{"DFX_DMA_PREBUF_MEM0_ECC_ERR_ADDR  ", 0x1CA8ull},
	{"DFX_DMA_PREBUF_MEM0_ECC_CNT       ", 0x1CACull},
	{"DFX_DMA_LOC_NP_OSTB_ECC_ERR_ADDR  ", 0x1CB0ull},
	{"DFX_DMA_LOC_NP_OSTB_ECC_CNT       ", 0x1CB4ull},
	{"DFX_DMA_PREBUF_MEM1_ECC_ERR_ADDR  ", 0x1CC0ull},
	{"DFX_DMA_PREBUF_MEM1_ECC_CNT       ", 0x1CC4ull},
	{"DMA_CH_DONE_STS                   ", 0x02E0ull},
	{"DMA_CH_ERR_STS                    ", 0x0320ull},
};
#endif /* CONFIG_DEBUG_FS*/

static enum hisi_dma_reg_layout hisi_dma_get_reg_layout(struct pci_dev *pdev)
{
	if (pdev->revision == HISI_DMA_REVISION_HIP08B)
		return HISI_DMA_REG_LAYOUT_HIP08;
	else if (pdev->revision >= HISI_DMA_REVISION_HIP09A)
		return HISI_DMA_REG_LAYOUT_HIP09;

	return HISI_DMA_REG_LAYOUT_INVALID;
}

static u32 hisi_dma_get_chan_num(struct pci_dev *pdev)
{
	if (pdev->revision == HISI_DMA_REVISION_HIP08B)
		return HISI_DMA_HIP08_CHAN_NUM;

	return HISI_DMA_HIP09_CHAN_NUM;
}

static u32 hisi_dma_get_msi_num(struct pci_dev *pdev)
{
	if (pdev->revision == HISI_DMA_REVISION_HIP08B)
		return HISI_DMA_HIP08_MSI_NUM;

	return HISI_DMA_HIP09_MSI_NUM;
}

static u32 hisi_dma_get_queue_base(struct pci_dev *pdev)
{
	if (pdev->revision == HISI_DMA_REVISION_HIP08B)
		return HISI_DMA_HIP08_Q_BASE;

	return HISI_DMA_HIP09_Q_BASE;
}

static inline struct hisi_dma_chan *to_hisi_dma_chan(struct dma_chan *c)
{
	return container_of(c, struct hisi_dma_chan, vc.chan);
}

static inline struct hisi_dma_desc *to_hisi_dma_desc(struct virt_dma_desc *vd)
{
	return container_of(vd, struct hisi_dma_desc, vd);
}

static inline void hisi_dma_chan_write(void __iomem *base, u32 reg, u32 index,
				       u32 val)
{
	writel_relaxed(val, base + reg + index * HISI_DMA_Q_OFFSET);
}

static inline void hisi_dma_update_bit(void __iomem *addr, u32 pos, bool val)
{
	u32 tmp;

	tmp = readl_relaxed(addr);
	tmp = val ? tmp | pos : tmp & ~pos;
	writel_relaxed(tmp, addr);
}

static void hisi_dma_pause_dma(struct hisi_dma_dev *hdma_dev, u32 index,
			       bool pause)
{
	void __iomem *addr;

	addr = hdma_dev->queue_base + HISI_DMA_Q_CTRL0 +
	       index * HISI_DMA_Q_OFFSET;
	hisi_dma_update_bit(addr, HISI_DMA_Q_CTRL0_QUEUE_PAUSE, pause);
}

static void hisi_dma_enable_dma(struct hisi_dma_dev *hdma_dev, u32 index,
				bool enable)
{
	void __iomem *addr;

	addr = hdma_dev->queue_base + HISI_DMA_Q_CTRL0 +
	       index * HISI_DMA_Q_OFFSET;
	hisi_dma_update_bit(addr, HISI_DMA_Q_CTRL0_QUEUE_EN, enable);
}

static void hisi_dma_mask_irq(struct hisi_dma_dev *hdma_dev, u32 qp_index)
{
	void __iomem *q_base = hdma_dev->queue_base;

	if (hdma_dev->reg_layout == HISI_DMA_REG_LAYOUT_HIP08)
		hisi_dma_chan_write(q_base, HISI_DMA_HIP08_Q_INT_MSK,
				    qp_index, HISI_DMA_HIP08_Q_INT_STS_MASK);
	else {
		hisi_dma_chan_write(q_base, HISI_DMA_HIP09_Q_INT_MSK,
				    qp_index, HISI_DMA_HIP09_Q_INT_STS_MASK);
		hisi_dma_chan_write(q_base, HISI_DMA_HIP09_Q_ERR_INT_MSK,
				    qp_index,
				    HISI_DMA_HIP09_Q_ERR_INT_STS_MASK);
	}
}

static void hisi_dma_unmask_irq(struct hisi_dma_dev *hdma_dev, u32 qp_index)
{
	void __iomem *q_base = hdma_dev->queue_base;

	if (hdma_dev->reg_layout == HISI_DMA_REG_LAYOUT_HIP08) {
		hisi_dma_chan_write(q_base, HISI_DMA_HIP08_Q_INT_STS,
				    qp_index, HISI_DMA_HIP08_Q_INT_STS_MASK);
		hisi_dma_chan_write(q_base, HISI_DMA_HIP08_Q_INT_MSK,
				    qp_index, 0);
	} else {
		hisi_dma_chan_write(q_base, HISI_DMA_HIP09_Q_INT_STS,
				    qp_index, HISI_DMA_HIP09_Q_INT_STS_MASK);
		hisi_dma_chan_write(q_base, HISI_DMA_HIP09_Q_ERR_INT_STS,
				    qp_index,
				    HISI_DMA_HIP09_Q_ERR_INT_STS_MASK);
		hisi_dma_chan_write(q_base, HISI_DMA_HIP09_Q_INT_MSK,
				    qp_index, 0);
		hisi_dma_chan_write(q_base, HISI_DMA_HIP09_Q_ERR_INT_MSK,
				    qp_index, 0);
	}
}

static void hisi_dma_do_reset(struct hisi_dma_dev *hdma_dev, u32 index)
{
	void __iomem *addr;

	addr = hdma_dev->queue_base +
	       HISI_DMA_Q_CTRL1 + index * HISI_DMA_Q_OFFSET;
	hisi_dma_update_bit(addr, HISI_DMA_Q_CTRL1_QUEUE_RESET, 1);
}

static void hisi_dma_reset_qp_point(struct hisi_dma_dev *hdma_dev, u32 index)
{
	void __iomem *q_base = hdma_dev->queue_base;

	hisi_dma_chan_write(q_base, HISI_DMA_Q_SQ_TAIL_PTR, index, 0);
	hisi_dma_chan_write(q_base, HISI_DMA_Q_CQ_HEAD_PTR, index, 0);
}

static void hisi_dma_reset_or_disable_hw_chan(struct hisi_dma_chan *chan,
					      bool disable)
{
	struct hisi_dma_dev *hdma_dev = chan->hdma_dev;
	u32 index = chan->qp_num, tmp;
	void __iomem *addr;
	int ret;

	hisi_dma_pause_dma(hdma_dev, index, true);
	hisi_dma_enable_dma(hdma_dev, index, false);
	hisi_dma_mask_irq(hdma_dev, index);

	addr = hdma_dev->queue_base +
	       HISI_DMA_Q_FSM_STS + index * HISI_DMA_Q_OFFSET;

	ret = readl_relaxed_poll_timeout(addr, tmp,
		FIELD_GET(HISI_DMA_Q_FSM_STS_MASK, tmp) != RUN,
		HISI_DMA_POLL_Q_STS_DELAY_US, HISI_DMA_POLL_Q_STS_TIME_OUT_US);
	if (ret) {
		dev_err(&hdma_dev->pdev->dev, "disable channel timeout!\n");
		WARN_ON(1);
	}

	hisi_dma_do_reset(hdma_dev, index);
	hisi_dma_reset_qp_point(hdma_dev, index);
	hisi_dma_pause_dma(hdma_dev, index, false);

	if (!disable) {
		hisi_dma_enable_dma(hdma_dev, index, true);
		hisi_dma_unmask_irq(hdma_dev, index);
	}

	ret = readl_relaxed_poll_timeout(addr, tmp,
		FIELD_GET(HISI_DMA_Q_FSM_STS_MASK, tmp) == IDLE,
		HISI_DMA_POLL_Q_STS_DELAY_US, HISI_DMA_POLL_Q_STS_TIME_OUT_US);
	if (ret) {
		dev_err(&hdma_dev->pdev->dev, "reset channel timeout!\n");
		WARN_ON(1);
	}
}

static void hisi_dma_free_chan_resources(struct dma_chan *c)
{
	struct hisi_dma_chan *chan = to_hisi_dma_chan(c);
	struct hisi_dma_dev *hdma_dev = chan->hdma_dev;

	hisi_dma_reset_or_disable_hw_chan(chan, false);
	vchan_free_chan_resources(&chan->vc);

	memset(chan->sq, 0, sizeof(struct hisi_dma_sqe) * hdma_dev->chan_depth);
	memset(chan->cq, 0, sizeof(struct hisi_dma_cqe) * hdma_dev->chan_depth);
	chan->sq_tail = 0;
	chan->cq_head = 0;
	chan->status = DISABLE;
}

static void hisi_dma_desc_free(struct virt_dma_desc *vd)
{
	kfree(to_hisi_dma_desc(vd));
}

static struct dma_async_tx_descriptor *
hisi_dma_prep_dma_memcpy(struct dma_chan *c, dma_addr_t dst, dma_addr_t src,
			 size_t len, unsigned long flags)
{
	struct hisi_dma_chan *chan = to_hisi_dma_chan(c);
	struct hisi_dma_desc *desc;

	desc = kzalloc(sizeof(*desc), GFP_NOWAIT);
	if (!desc)
		return NULL;

	desc->sqe.length = cpu_to_le32(len);
	desc->sqe.src_addr = cpu_to_le64(src);
	desc->sqe.dst_addr = cpu_to_le64(dst);

	return vchan_tx_prep(&chan->vc, &desc->vd, flags);
}

static enum dma_status
hisi_dma_tx_status(struct dma_chan *c, dma_cookie_t cookie,
		   struct dma_tx_state *txstate)
{
	return dma_cookie_status(c, cookie, txstate);
}

static void hisi_dma_start_transfer(struct hisi_dma_chan *chan)
{
	struct hisi_dma_sqe *sqe = chan->sq + chan->sq_tail;
	struct hisi_dma_dev *hdma_dev = chan->hdma_dev;
	struct hisi_dma_desc *desc;
	struct virt_dma_desc *vd;

	vd = vchan_next_desc(&chan->vc);
	if (!vd) {
		chan->desc = NULL;
		return;
	}
	list_del(&vd->node);
	desc = to_hisi_dma_desc(vd);
	chan->desc = desc;

	memcpy(sqe, &desc->sqe, sizeof(struct hisi_dma_sqe));

	/* update other field in sqe */
	sqe->dw0 = cpu_to_le32(FIELD_PREP(OPCODE_MASK, OPCODE_M2M));
	sqe->dw0 |= cpu_to_le32(LOCAL_IRQ_EN);

	/* make sure data has been updated in sqe */
	wmb();

	/* update sq tail, point to new sqe position */
	chan->sq_tail = (chan->sq_tail + 1) % hdma_dev->chan_depth;

	/* update sq_tail to trigger a new task */
	hisi_dma_chan_write(hdma_dev->queue_base, HISI_DMA_Q_SQ_TAIL_PTR,
			    chan->qp_num, chan->sq_tail);
}

static void hisi_dma_issue_pending(struct dma_chan *c)
{
	struct hisi_dma_chan *chan = to_hisi_dma_chan(c);
	unsigned long flags;

	spin_lock_irqsave(&chan->vc.lock, flags);

	if (vchan_issue_pending(&chan->vc) && !chan->desc)
		hisi_dma_start_transfer(chan);

	spin_unlock_irqrestore(&chan->vc.lock, flags);
}

static int hisi_dma_terminate_all(struct dma_chan *c)
{
	struct hisi_dma_chan *chan = to_hisi_dma_chan(c);
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&chan->vc.lock, flags);

	hisi_dma_pause_dma(chan->hdma_dev, chan->qp_num, true);
	if (chan->desc) {
		vchan_terminate_vdesc(&chan->desc->vd);
		chan->desc = NULL;
	}

	vchan_get_all_descriptors(&chan->vc, &head);

	spin_unlock_irqrestore(&chan->vc.lock, flags);

	vchan_dma_desc_free_list(&chan->vc, &head);
	hisi_dma_pause_dma(chan->hdma_dev, chan->qp_num, false);

	return 0;
}

static void hisi_dma_synchronize(struct dma_chan *c)
{
	struct hisi_dma_chan *chan = to_hisi_dma_chan(c);

	vchan_synchronize(&chan->vc);
}

static int hisi_dma_alloc_qps_mem(struct hisi_dma_dev *hdma_dev)
{
	size_t sq_size = sizeof(struct hisi_dma_sqe) * hdma_dev->chan_depth;
	size_t cq_size = sizeof(struct hisi_dma_cqe) * hdma_dev->chan_depth;
	struct device *dev = &hdma_dev->pdev->dev;
	struct hisi_dma_chan *chan;
	int i;

	for (i = 0; i < hdma_dev->chan_num; i++) {
		chan = &hdma_dev->chan[i];
		chan->sq = dmam_alloc_coherent(dev, sq_size, &chan->sq_dma,
					       GFP_KERNEL);
		if (!chan->sq)
			return -ENOMEM;

		chan->cq = dmam_alloc_coherent(dev, cq_size, &chan->cq_dma,
					       GFP_KERNEL);
		if (!chan->cq)
			return -ENOMEM;
	}

	return 0;
}

static void hisi_dma_init_hw_qp(struct hisi_dma_dev *hdma_dev, u32 index)
{
	struct hisi_dma_chan *chan = &hdma_dev->chan[index];
	void __iomem *q_base = hdma_dev->queue_base;
	u32 hw_depth = hdma_dev->chan_depth - 1;
	void __iomem *addr;
	u32 tmp;

	/* set sq, cq base */
	hisi_dma_chan_write(q_base, HISI_DMA_Q_SQ_BASE_L, index,
			    lower_32_bits(chan->sq_dma));
	hisi_dma_chan_write(q_base, HISI_DMA_Q_SQ_BASE_H, index,
			    upper_32_bits(chan->sq_dma));
	hisi_dma_chan_write(q_base, HISI_DMA_Q_CQ_BASE_L, index,
			    lower_32_bits(chan->cq_dma));
	hisi_dma_chan_write(q_base, HISI_DMA_Q_CQ_BASE_H, index,
			    upper_32_bits(chan->cq_dma));

	/* set sq, cq depth */
	hisi_dma_chan_write(q_base, HISI_DMA_Q_SQ_DEPTH, index, hw_depth);
	hisi_dma_chan_write(q_base, HISI_DMA_Q_CQ_DEPTH, index, hw_depth);

	/* init sq tail and cq head */
	hisi_dma_chan_write(q_base, HISI_DMA_Q_SQ_TAIL_PTR, index, 0);
	hisi_dma_chan_write(q_base, HISI_DMA_Q_CQ_HEAD_PTR, index, 0);

	/* init error interrupt stats */
	hisi_dma_chan_write(q_base, HISI_DMA_Q_ERR_INT_NUM0, index, 0);
	hisi_dma_chan_write(q_base, HISI_DMA_Q_ERR_INT_NUM1, index, 0);
	hisi_dma_chan_write(q_base, HISI_DMA_Q_ERR_INT_NUM2, index, 0);

	if (hdma_dev->reg_layout == HISI_DMA_REG_LAYOUT_HIP08) {
		hisi_dma_chan_write(q_base, HISI_DMA_HIP08_Q_ERR_INT_NUM3,
				    index, 0);
		hisi_dma_chan_write(q_base, HISI_DMA_HIP08_Q_ERR_INT_NUM4,
				    index, 0);
		hisi_dma_chan_write(q_base, HISI_DMA_HIP08_Q_ERR_INT_NUM5,
				    index, 0);
		hisi_dma_chan_write(q_base, HISI_DMA_HIP08_Q_ERR_INT_NUM6,
				    index, 0);
		/*
		 * init SQ/CQ direction selecting register.
		 * "0" is to local side and "1" is to remote side.
		 */
		addr = q_base + HISI_DMA_Q_CTRL0 + index * HISI_DMA_Q_OFFSET;
		hisi_dma_update_bit(addr, HISI_DMA_HIP08_Q_CTRL0_SQCQ_DRCT, 0);

		/*
		 * 0 - Continue to next descriptor if error occurs.
		 * 1 - Abort the DMA queue if error occurs.
		 */
		hisi_dma_update_bit(addr,
				    HISI_DMA_HIP08_Q_CTRL0_ERR_ABORT_EN, 0);
	} else {
		addr = q_base + HISI_DMA_Q_CTRL0 + index * HISI_DMA_Q_OFFSET;

		/*
		 * init SQ/CQ direction selecting register.
		 * "0" is to local side and "1" is to remote side.
		 */
		hisi_dma_update_bit(addr, HISI_DMA_HIP09_Q_CTRL0_SQ_DRCT, 0);
		hisi_dma_update_bit(addr, HISI_DMA_HIP09_Q_CTRL0_CQ_DRCT, 0);

		/*
		 * 0 - Continue to next descriptor if error occurs.
		 * 1 - Abort the DMA queue if error occurs.
		 */

		tmp = readl_relaxed(addr);
		tmp &= ~HISI_DMA_HIP09_Q_CTRL0_ERR_ABORT_EN;
		writel_relaxed(tmp, addr);

		/*
		 * 0 - dma should process FLR whith CPU.
		 * 1 - dma not process FLR, only cpu process FLR.
		 */
		addr = q_base + HISI_DMA_HIP09_DMA_FLR_DISABLE +
		       index * HISI_DMA_Q_OFFSET;
		hisi_dma_update_bit(addr, HISI_DMA_HIP09_DMA_FLR_DISABLE_B, 0);

		addr = q_base + HISI_DMA_Q_CTRL1 + index * HISI_DMA_Q_OFFSET;
		hisi_dma_update_bit(addr, HISI_DMA_HIP09_Q_CTRL1_VA_ENABLE, 1);
	}
}

static void hisi_dma_enable_qp(struct hisi_dma_dev *hdma_dev, u32 qp_index)
{
	hisi_dma_init_hw_qp(hdma_dev, qp_index);
	hisi_dma_unmask_irq(hdma_dev, qp_index);
	hisi_dma_enable_dma(hdma_dev, qp_index, true);
}

static void hisi_dma_disable_qp(struct hisi_dma_dev *hdma_dev, u32 qp_index)
{
	hisi_dma_reset_or_disable_hw_chan(&hdma_dev->chan[qp_index], true);
}

static void hisi_dma_enable_qps(struct hisi_dma_dev *hdma_dev)
{
	int i;

	for (i = 0; i < hdma_dev->chan_num; i++) {
		hdma_dev->chan[i].qp_num = i;
		hdma_dev->chan[i].hdma_dev = hdma_dev;
		hdma_dev->chan[i].vc.desc_free = hisi_dma_desc_free;
		vchan_init(&hdma_dev->chan[i].vc, &hdma_dev->dma_dev);
		hisi_dma_enable_qp(hdma_dev, i);
	}
}

static void hisi_dma_disable_qps(struct hisi_dma_dev *hdma_dev)
{
	int i;

	for (i = 0; i < hdma_dev->chan_num; i++) {
		hisi_dma_disable_qp(hdma_dev, i);
		tasklet_kill(&hdma_dev->chan[i].vc.task);
	}
}

static irqreturn_t hisi_dma_irq(int irq, void *data)
{
	struct hisi_dma_chan *chan = data;
	struct hisi_dma_dev *hdma_dev = chan->hdma_dev;
	struct hisi_dma_desc *desc;
	struct hisi_dma_cqe *cqe;
	void __iomem *q_base;

	spin_lock(&chan->vc.lock);

	desc = chan->desc;
	cqe = chan->cq + chan->cq_head;
	q_base = hdma_dev->queue_base;
	if (desc) {
		chan->cq_head = (chan->cq_head + 1) % hdma_dev->chan_depth;
		hisi_dma_chan_write(q_base, HISI_DMA_Q_CQ_HEAD_PTR,
				    chan->qp_num, chan->cq_head);
		if (FIELD_GET(STATUS_MASK, cqe->w0) == STATUS_SUCC) {
			vchan_cookie_complete(&desc->vd);
			hisi_dma_start_transfer(chan);
		} else {
			dev_err(&hdma_dev->pdev->dev, "task error!\n");
		}
	}

	spin_unlock(&chan->vc.lock);

	return IRQ_HANDLED;
}

static int hisi_dma_request_qps_irq(struct hisi_dma_dev *hdma_dev)
{
	struct pci_dev *pdev = hdma_dev->pdev;
	int i, ret;

	for (i = 0; i < hdma_dev->chan_num; i++) {
		ret = devm_request_irq(&pdev->dev, pci_irq_vector(pdev, i),
				       hisi_dma_irq, IRQF_SHARED, "hisi_dma",
				       &hdma_dev->chan[i]);
		if (ret)
			return ret;
	}

	return 0;
}

/* This function enables all hw channels in a device */
static int hisi_dma_enable_hw_channels(struct hisi_dma_dev *hdma_dev)
{
	int ret;

	ret = hisi_dma_alloc_qps_mem(hdma_dev);
	if (ret) {
		dev_err(&hdma_dev->pdev->dev, "fail to allocate qp memory!\n");
		return ret;
	}

	ret = hisi_dma_request_qps_irq(hdma_dev);
	if (ret) {
		dev_err(&hdma_dev->pdev->dev, "fail to request qp irq!\n");
		return ret;
	}

	hisi_dma_enable_qps(hdma_dev);

	return 0;
}

static void hisi_dma_disable_hw_channels(void *data)
{
	hisi_dma_disable_qps(data);
}

static void hisi_dma_set_mode(struct hisi_dma_dev *hdma_dev,
			      enum hisi_dma_mode mode)
{
	if (hdma_dev->reg_layout == HISI_DMA_REG_LAYOUT_HIP08)
		writel_relaxed(mode == RC ? 1 : 0,
			       hdma_dev->base + HISI_DMA_HIP08_MODE);
}

static void hisi_dma_init_hw(struct hisi_dma_dev *hdma_dev)
{
	void __iomem *addr;
	int i;

	if (hdma_dev->reg_layout == HISI_DMA_REG_LAYOUT_HIP09) {
		for (i = 0; i < HISI_DMA_HIP09_MAX_PORT_NUM; i++) {
			addr = hdma_dev->base + HISI_DMA_HIP09_PORT_CFG_REG(i);
			hisi_dma_update_bit(addr,
				HISI_DMA_HIP09_PORT_CFG_LINK_DOWN_MASK_B, 1);
		}
	}
}

static void hisi_dma_init_dma_dev(struct hisi_dma_dev *hdma_dev)
{
	struct dma_device *dma_dev;

	dma_dev = &hdma_dev->dma_dev;
	dma_cap_set(DMA_MEMCPY, dma_dev->cap_mask);
	dma_dev->device_free_chan_resources = hisi_dma_free_chan_resources;
	dma_dev->device_prep_dma_memcpy = hisi_dma_prep_dma_memcpy;
	dma_dev->device_tx_status = hisi_dma_tx_status;
	dma_dev->device_issue_pending = hisi_dma_issue_pending;
	dma_dev->device_terminate_all = hisi_dma_terminate_all;
	dma_dev->device_synchronize = hisi_dma_synchronize;
	dma_dev->directions = BIT(DMA_MEM_TO_MEM);
	dma_dev->dev = &hdma_dev->pdev->dev;
	INIT_LIST_HEAD(&dma_dev->channels);
}

/* --- debugfs implementation --- */
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
static struct debugfs_reg32 *hisi_dma_get_ch_regs(struct hisi_dma_dev *hdma_dev,
						  u32 *regs_sz)
{
	struct device *dev = &hdma_dev->pdev->dev;
	struct debugfs_reg32 *regs;
	u32 regs_sz_comm;

	regs_sz_comm = ARRAY_SIZE(hisi_dma_comm_chan_regs);

	if (hdma_dev->reg_layout == HISI_DMA_REG_LAYOUT_HIP08)
		*regs_sz = regs_sz_comm + ARRAY_SIZE(hisi_dma_hip08_chan_regs);
	else
		*regs_sz = regs_sz_comm + ARRAY_SIZE(hisi_dma_hip09_chan_regs);

	regs = devm_kcalloc(dev, *regs_sz, sizeof(struct debugfs_reg32),
			    GFP_KERNEL);
	if (!regs)
		return NULL;
	memcpy(regs, hisi_dma_comm_chan_regs, sizeof(hisi_dma_comm_chan_regs));

	if (hdma_dev->reg_layout == HISI_DMA_REG_LAYOUT_HIP08)
		memcpy(regs + regs_sz_comm, hisi_dma_hip08_chan_regs,
		       sizeof(hisi_dma_hip08_chan_regs));
	else
		memcpy(regs + regs_sz_comm, hisi_dma_hip09_chan_regs,
		       sizeof(hisi_dma_hip09_chan_regs));

	return regs;
}

static int hisi_dma_create_chan_dir(struct hisi_dma_dev *hdma_dev)
{
	char dir_name[HISI_DMA_MAX_DIR_NAME_LEN];
	struct debugfs_regset32 *regsets;
	struct debugfs_reg32 *regs;
	struct dentry *chan_dir;
	struct device *dev;
	u32 regs_sz;
	int ret;
	int i;

	dev = &hdma_dev->pdev->dev;

	regsets = devm_kcalloc(dev, hdma_dev->chan_num,
			       sizeof(*regsets), GFP_KERNEL);
	if (!regsets)
		return -ENOMEM;

	regs = hisi_dma_get_ch_regs(hdma_dev, &regs_sz);
	if (!regs)
		return -ENOMEM;

	for (i = 0; i < hdma_dev->chan_num; i++) {
		regsets[i].regs = regs;
		regsets[i].nregs = regs_sz;
		regsets[i].base = hdma_dev->queue_base + i * HISI_DMA_Q_OFFSET;
		regsets[i].dev = dev;

		memset(dir_name, 0, HISI_DMA_MAX_DIR_NAME_LEN);
		ret = sprintf(dir_name, "channel%d", i);
		if (ret < 0)
			return ret;

		chan_dir = debugfs_create_dir(dir_name,
					      hdma_dev->dma_dev.dbg_dev_root);
		debugfs_create_regset32("regs", 0444, chan_dir, &regsets[i]);
	}

	return 0;
}

static void hisi_dma_create_debugfs(struct hisi_dma_dev *hdma_dev)
{
	struct debugfs_regset32 *regset;
	struct device *dev;
	int ret;

	dev = &hdma_dev->pdev->dev;

	if (hdma_dev->dma_dev.dbg_dev_root == NULL)
		return;

	regset = devm_kzalloc(dev, sizeof(*regset), GFP_KERNEL);
	if (!regset)
		return;

	if (hdma_dev->reg_layout == HISI_DMA_REG_LAYOUT_HIP08) {
		regset->regs = hisi_dma_hip08_comm_regs;
		regset->nregs = ARRAY_SIZE(hisi_dma_hip08_comm_regs);
	} else {
		regset->regs = hisi_dma_hip09_comm_regs;
		regset->nregs = ARRAY_SIZE(hisi_dma_hip09_comm_regs);
	}
	regset->base = hdma_dev->base;
	regset->dev = dev;

	debugfs_create_regset32("regs", 0444,
				hdma_dev->dma_dev.dbg_dev_root, regset);

	ret = hisi_dma_create_chan_dir(hdma_dev);
	if (ret < 0)
		dev_info(&hdma_dev->pdev->dev, "fail to create debugfs for channels!\n");
}
#else
static void hisi_dma_create_debugfs(struct hisi_dma_dev *hdma_dev) { }
#endif /* CONFIG_DEBUG_FS*/
/* --- debugfs implementation --- */

static int hisi_dma_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	enum hisi_dma_reg_layout reg_layout;
	struct device *dev = &pdev->dev;
	struct hisi_dma_dev *hdma_dev;
	struct dma_device *dma_dev;
	u32 chan_num;
	u32 msi_num;
	int ret;

	reg_layout = hisi_dma_get_reg_layout(pdev);
	if (reg_layout == HISI_DMA_REG_LAYOUT_INVALID) {
		dev_err(dev, "unsupported device!\n");
		return -EINVAL;
	}

	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err(dev, "failed to enable device mem!\n");
		return ret;
	}

	ret = pcim_iomap_regions(pdev, 1 << PCI_BAR_2, pci_name(pdev));
	if (ret) {
		dev_err(dev, "failed to remap I/O region!\n");
		return ret;
	}

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret)
		return ret;

	chan_num = hisi_dma_get_chan_num(pdev);
	hdma_dev = devm_kzalloc(dev, struct_size(hdma_dev, chan, chan_num),
				GFP_KERNEL);
	if (!hdma_dev)
		return -EINVAL;

	hdma_dev->base = pcim_iomap_table(pdev)[PCI_BAR_2];
	hdma_dev->pdev = pdev;
	hdma_dev->chan_depth = HISI_DMA_Q_DEPTH_VAL;
	hdma_dev->chan_num = chan_num;
	hdma_dev->reg_layout = reg_layout;
	hdma_dev->queue_base = hdma_dev->base + hisi_dma_get_queue_base(pdev);

	pci_set_drvdata(pdev, hdma_dev);
	pci_set_master(pdev);

	msi_num = hisi_dma_get_msi_num(pdev);

	/* This will be freed by 'pcim_release()'. See 'pcim_enable_device()' */
	ret = pci_alloc_irq_vectors(pdev, msi_num, msi_num, PCI_IRQ_MSI);
	if (ret < 0) {
		dev_err(dev, "Failed to allocate MSI vectors!\n");
		return ret;
	}

	hisi_dma_init_dma_dev(hdma_dev);

	hisi_dma_set_mode(hdma_dev, RC);

	hisi_dma_init_hw(hdma_dev);

	ret = hisi_dma_enable_hw_channels(hdma_dev);
	if (ret < 0) {
		dev_err(dev, "failed to enable hw channel!\n");
		return ret;
	}

	ret = devm_add_action_or_reset(dev, hisi_dma_disable_hw_channels,
				       hdma_dev);
	if (ret)
		return ret;

	dma_dev = &hdma_dev->dma_dev;
	ret = dmaenginem_async_device_register(dma_dev);
	if (ret < 0) {
		dev_err(dev, "failed to register device!\n");
		return ret;
	}

	hisi_dma_create_debugfs(hdma_dev);

	return 0;
}

static const struct pci_device_id hisi_dma_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_HUAWEI, 0xa122) },
	{ 0, }
};

static struct pci_driver hisi_dma_pci_driver = {
	.name		= "hisi_dma",
	.id_table	= hisi_dma_pci_tbl,
	.probe		= hisi_dma_probe,
};

module_pci_driver(hisi_dma_pci_driver);

MODULE_AUTHOR("Zhou Wang <wangzhou1@hisilicon.com>");
MODULE_AUTHOR("Zhenfa Qiu <qiuzhenfa@hisilicon.com>");
MODULE_DESCRIPTION("HiSilicon Kunpeng DMA controller driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(pci, hisi_dma_pci_tbl);
