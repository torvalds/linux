// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2019 HiSilicon Limited. */
#include <linux/bitfield.h>
#include <linux/dmaengine.h>
#include <linux/init.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include "virt-dma.h"

#define HISI_DMA_SQ_BASE_L		0x0
#define HISI_DMA_SQ_BASE_H		0x4
#define HISI_DMA_SQ_DEPTH		0x8
#define HISI_DMA_SQ_TAIL_PTR		0xc
#define HISI_DMA_CQ_BASE_L		0x10
#define HISI_DMA_CQ_BASE_H		0x14
#define HISI_DMA_CQ_DEPTH		0x18
#define HISI_DMA_CQ_HEAD_PTR		0x1c
#define HISI_DMA_CTRL0			0x20
#define HISI_DMA_CTRL0_QUEUE_EN_S	0
#define HISI_DMA_CTRL0_QUEUE_PAUSE_S	4
#define HISI_DMA_CTRL1			0x24
#define HISI_DMA_CTRL1_QUEUE_RESET_S	0
#define HISI_DMA_Q_FSM_STS		0x30
#define HISI_DMA_FSM_STS_MASK		GENMASK(3, 0)
#define HISI_DMA_INT_STS		0x40
#define HISI_DMA_INT_STS_MASK		GENMASK(12, 0)
#define HISI_DMA_INT_MSK		0x44
#define HISI_DMA_MODE			0x217c
#define HISI_DMA_OFFSET			0x100

#define HISI_DMA_MSI_NUM		32
#define HISI_DMA_CHAN_NUM		30
#define HISI_DMA_Q_DEPTH_VAL		1024

#define PCI_BAR_2			2

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
	struct hisi_dma_chan chan[];
};

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
	writel_relaxed(val, base + reg + index * HISI_DMA_OFFSET);
}

static inline void hisi_dma_update_bit(void __iomem *addr, u32 pos, bool val)
{
	u32 tmp;

	tmp = readl_relaxed(addr);
	tmp = val ? tmp | BIT(pos) : tmp & ~BIT(pos);
	writel_relaxed(tmp, addr);
}

static void hisi_dma_pause_dma(struct hisi_dma_dev *hdma_dev, u32 index,
			       bool pause)
{
	void __iomem *addr = hdma_dev->base + HISI_DMA_CTRL0 + index *
			     HISI_DMA_OFFSET;

	hisi_dma_update_bit(addr, HISI_DMA_CTRL0_QUEUE_PAUSE_S, pause);
}

static void hisi_dma_enable_dma(struct hisi_dma_dev *hdma_dev, u32 index,
				bool enable)
{
	void __iomem *addr = hdma_dev->base + HISI_DMA_CTRL0 + index *
			     HISI_DMA_OFFSET;

	hisi_dma_update_bit(addr, HISI_DMA_CTRL0_QUEUE_EN_S, enable);
}

static void hisi_dma_mask_irq(struct hisi_dma_dev *hdma_dev, u32 qp_index)
{
	hisi_dma_chan_write(hdma_dev->base, HISI_DMA_INT_MSK, qp_index,
			    HISI_DMA_INT_STS_MASK);
}

static void hisi_dma_unmask_irq(struct hisi_dma_dev *hdma_dev, u32 qp_index)
{
	void __iomem *base = hdma_dev->base;

	hisi_dma_chan_write(base, HISI_DMA_INT_STS, qp_index,
			    HISI_DMA_INT_STS_MASK);
	hisi_dma_chan_write(base, HISI_DMA_INT_MSK, qp_index, 0);
}

static void hisi_dma_do_reset(struct hisi_dma_dev *hdma_dev, u32 index)
{
	void __iomem *addr = hdma_dev->base + HISI_DMA_CTRL1 + index *
			     HISI_DMA_OFFSET;

	hisi_dma_update_bit(addr, HISI_DMA_CTRL1_QUEUE_RESET_S, 1);
}

static void hisi_dma_reset_qp_point(struct hisi_dma_dev *hdma_dev, u32 index)
{
	hisi_dma_chan_write(hdma_dev->base, HISI_DMA_SQ_TAIL_PTR, index, 0);
	hisi_dma_chan_write(hdma_dev->base, HISI_DMA_CQ_HEAD_PTR, index, 0);
}

static void hisi_dma_reset_or_disable_hw_chan(struct hisi_dma_chan *chan,
					      bool disable)
{
	struct hisi_dma_dev *hdma_dev = chan->hdma_dev;
	u32 index = chan->qp_num, tmp;
	int ret;

	hisi_dma_pause_dma(hdma_dev, index, true);
	hisi_dma_enable_dma(hdma_dev, index, false);
	hisi_dma_mask_irq(hdma_dev, index);

	ret = readl_relaxed_poll_timeout(hdma_dev->base +
		HISI_DMA_Q_FSM_STS + index * HISI_DMA_OFFSET, tmp,
		FIELD_GET(HISI_DMA_FSM_STS_MASK, tmp) != RUN, 10, 1000);
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

	ret = readl_relaxed_poll_timeout(hdma_dev->base +
		HISI_DMA_Q_FSM_STS + index * HISI_DMA_OFFSET, tmp,
		FIELD_GET(HISI_DMA_FSM_STS_MASK, tmp) == IDLE, 10, 1000);
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
		dev_err(&hdma_dev->pdev->dev, "no issued task!\n");
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
	hisi_dma_chan_write(hdma_dev->base, HISI_DMA_SQ_TAIL_PTR, chan->qp_num,
			    chan->sq_tail);
}

static void hisi_dma_issue_pending(struct dma_chan *c)
{
	struct hisi_dma_chan *chan = to_hisi_dma_chan(c);
	unsigned long flags;

	spin_lock_irqsave(&chan->vc.lock, flags);

	if (vchan_issue_pending(&chan->vc))
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
	u32 hw_depth = hdma_dev->chan_depth - 1;
	void __iomem *base = hdma_dev->base;

	/* set sq, cq base */
	hisi_dma_chan_write(base, HISI_DMA_SQ_BASE_L, index,
			    lower_32_bits(chan->sq_dma));
	hisi_dma_chan_write(base, HISI_DMA_SQ_BASE_H, index,
			    upper_32_bits(chan->sq_dma));
	hisi_dma_chan_write(base, HISI_DMA_CQ_BASE_L, index,
			    lower_32_bits(chan->cq_dma));
	hisi_dma_chan_write(base, HISI_DMA_CQ_BASE_H, index,
			    upper_32_bits(chan->cq_dma));

	/* set sq, cq depth */
	hisi_dma_chan_write(base, HISI_DMA_SQ_DEPTH, index, hw_depth);
	hisi_dma_chan_write(base, HISI_DMA_CQ_DEPTH, index, hw_depth);

	/* init sq tail and cq head */
	hisi_dma_chan_write(base, HISI_DMA_SQ_TAIL_PTR, index, 0);
	hisi_dma_chan_write(base, HISI_DMA_CQ_HEAD_PTR, index, 0);
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

	spin_lock(&chan->vc.lock);

	desc = chan->desc;
	cqe = chan->cq + chan->cq_head;
	if (desc) {
		chan->cq_head = (chan->cq_head + 1) % hdma_dev->chan_depth;
		hisi_dma_chan_write(hdma_dev->base, HISI_DMA_CQ_HEAD_PTR,
				    chan->qp_num, chan->cq_head);
		if (FIELD_GET(STATUS_MASK, cqe->w0) == STATUS_SUCC) {
			vchan_cookie_complete(&desc->vd);
		} else {
			dev_err(&hdma_dev->pdev->dev, "task error!\n");
		}

		chan->desc = NULL;
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
	writel_relaxed(mode == RC ? 1 : 0, hdma_dev->base + HISI_DMA_MODE);
}

static int hisi_dma_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct hisi_dma_dev *hdma_dev;
	struct dma_device *dma_dev;
	int ret;

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

	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (ret)
		return ret;

	ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
	if (ret)
		return ret;

	hdma_dev = devm_kzalloc(dev, struct_size(hdma_dev, chan, HISI_DMA_CHAN_NUM), GFP_KERNEL);
	if (!hdma_dev)
		return -EINVAL;

	hdma_dev->base = pcim_iomap_table(pdev)[PCI_BAR_2];
	hdma_dev->pdev = pdev;
	hdma_dev->chan_num = HISI_DMA_CHAN_NUM;
	hdma_dev->chan_depth = HISI_DMA_Q_DEPTH_VAL;

	pci_set_drvdata(pdev, hdma_dev);
	pci_set_master(pdev);

	/* This will be freed by 'pcim_release()'. See 'pcim_enable_device()' */
	ret = pci_alloc_irq_vectors(pdev, HISI_DMA_MSI_NUM, HISI_DMA_MSI_NUM,
				    PCI_IRQ_MSI);
	if (ret < 0) {
		dev_err(dev, "Failed to allocate MSI vectors!\n");
		return ret;
	}

	dma_dev = &hdma_dev->dma_dev;
	dma_cap_set(DMA_MEMCPY, dma_dev->cap_mask);
	dma_dev->device_free_chan_resources = hisi_dma_free_chan_resources;
	dma_dev->device_prep_dma_memcpy = hisi_dma_prep_dma_memcpy;
	dma_dev->device_tx_status = hisi_dma_tx_status;
	dma_dev->device_issue_pending = hisi_dma_issue_pending;
	dma_dev->device_terminate_all = hisi_dma_terminate_all;
	dma_dev->device_synchronize = hisi_dma_synchronize;
	dma_dev->directions = BIT(DMA_MEM_TO_MEM);
	dma_dev->dev = dev;
	INIT_LIST_HEAD(&dma_dev->channels);

	hisi_dma_set_mode(hdma_dev, RC);

	ret = hisi_dma_enable_hw_channels(hdma_dev);
	if (ret < 0) {
		dev_err(dev, "failed to enable hw channel!\n");
		return ret;
	}

	ret = devm_add_action_or_reset(dev, hisi_dma_disable_hw_channels,
				       hdma_dev);
	if (ret)
		return ret;

	ret = dmaenginem_async_device_register(dma_dev);
	if (ret < 0)
		dev_err(dev, "failed to register device!\n");

	return ret;
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
