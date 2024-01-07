// SPDX-License-Identifier: GPL-2.0
// Copyright 2014-2015 Freescale
// Copyright 2018 NXP

/*
 * Driver for NXP Layerscape Queue Direct Memory Access Controller
 *
 * Author:
 *  Wen He <wen.he_1@nxp.com>
 *  Jiaheng Fan <jiaheng.fan@nxp.com>
 *
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>

#include "virt-dma.h"
#include "fsldma.h"

/* Register related definition */
#define FSL_QDMA_DMR			0x0
#define FSL_QDMA_DSR			0x4
#define FSL_QDMA_DEIER			0xe00
#define FSL_QDMA_DEDR			0xe04
#define FSL_QDMA_DECFDW0R		0xe10
#define FSL_QDMA_DECFDW1R		0xe14
#define FSL_QDMA_DECFDW2R		0xe18
#define FSL_QDMA_DECFDW3R		0xe1c
#define FSL_QDMA_DECFQIDR		0xe30
#define FSL_QDMA_DECBR			0xe34

#define FSL_QDMA_BCQMR(x)		(0xc0 + 0x100 * (x))
#define FSL_QDMA_BCQSR(x)		(0xc4 + 0x100 * (x))
#define FSL_QDMA_BCQEDPA_SADDR(x)	(0xc8 + 0x100 * (x))
#define FSL_QDMA_BCQDPA_SADDR(x)	(0xcc + 0x100 * (x))
#define FSL_QDMA_BCQEEPA_SADDR(x)	(0xd0 + 0x100 * (x))
#define FSL_QDMA_BCQEPA_SADDR(x)	(0xd4 + 0x100 * (x))
#define FSL_QDMA_BCQIER(x)		(0xe0 + 0x100 * (x))
#define FSL_QDMA_BCQIDR(x)		(0xe4 + 0x100 * (x))

#define FSL_QDMA_SQDPAR			0x80c
#define FSL_QDMA_SQEPAR			0x814
#define FSL_QDMA_BSQMR			0x800
#define FSL_QDMA_BSQSR			0x804
#define FSL_QDMA_BSQICR			0x828
#define FSL_QDMA_CQMR			0xa00
#define FSL_QDMA_CQDSCR1		0xa08
#define FSL_QDMA_CQDSCR2                0xa0c
#define FSL_QDMA_CQIER			0xa10
#define FSL_QDMA_CQEDR			0xa14
#define FSL_QDMA_SQCCMR			0xa20

/* Registers for bit and genmask */
#define FSL_QDMA_CQIDR_SQT		BIT(15)
#define QDMA_CCDF_FORMAT		BIT(29)
#define QDMA_CCDF_SER			BIT(30)
#define QDMA_SG_FIN			BIT(30)
#define QDMA_SG_LEN_MASK		GENMASK(29, 0)
#define QDMA_CCDF_MASK			GENMASK(28, 20)

#define FSL_QDMA_DEDR_CLEAR		GENMASK(31, 0)
#define FSL_QDMA_BCQIDR_CLEAR		GENMASK(31, 0)
#define FSL_QDMA_DEIER_CLEAR		GENMASK(31, 0)

#define FSL_QDMA_BCQIER_CQTIE		BIT(15)
#define FSL_QDMA_BCQIER_CQPEIE		BIT(23)
#define FSL_QDMA_BSQICR_ICEN		BIT(31)

#define FSL_QDMA_BSQICR_ICST(x)		((x) << 16)
#define FSL_QDMA_CQIER_MEIE		BIT(31)
#define FSL_QDMA_CQIER_TEIE		BIT(0)
#define FSL_QDMA_SQCCMR_ENTER_WM	BIT(21)

#define FSL_QDMA_BCQMR_EN		BIT(31)
#define FSL_QDMA_BCQMR_EI		BIT(30)
#define FSL_QDMA_BCQMR_CD_THLD(x)	((x) << 20)
#define FSL_QDMA_BCQMR_CQ_SIZE(x)	((x) << 16)

#define FSL_QDMA_BCQSR_QF		BIT(16)
#define FSL_QDMA_BCQSR_XOFF		BIT(0)

#define FSL_QDMA_BSQMR_EN		BIT(31)
#define FSL_QDMA_BSQMR_DI		BIT(30)
#define FSL_QDMA_BSQMR_CQ_SIZE(x)	((x) << 16)

#define FSL_QDMA_BSQSR_QE		BIT(17)

#define FSL_QDMA_DMR_DQD		BIT(30)
#define FSL_QDMA_DSR_DB		BIT(31)

/* Size related definition */
#define FSL_QDMA_QUEUE_MAX		8
#define FSL_QDMA_COMMAND_BUFFER_SIZE	64
#define FSL_QDMA_DESCRIPTOR_BUFFER_SIZE 32
#define FSL_QDMA_CIRCULAR_DESC_SIZE_MIN	64
#define FSL_QDMA_CIRCULAR_DESC_SIZE_MAX	16384
#define FSL_QDMA_QUEUE_NUM_MAX		8

/* Field definition for CMD */
#define FSL_QDMA_CMD_RWTTYPE		0x4
#define FSL_QDMA_CMD_LWC                0x2
#define FSL_QDMA_CMD_RWTTYPE_OFFSET	28
#define FSL_QDMA_CMD_NS_OFFSET		27
#define FSL_QDMA_CMD_DQOS_OFFSET	24
#define FSL_QDMA_CMD_WTHROTL_OFFSET	20
#define FSL_QDMA_CMD_DSEN_OFFSET	19
#define FSL_QDMA_CMD_LWC_OFFSET		16

/* Field definition for Descriptor status */
#define QDMA_CCDF_STATUS_RTE		BIT(5)
#define QDMA_CCDF_STATUS_WTE		BIT(4)
#define QDMA_CCDF_STATUS_CDE		BIT(2)
#define QDMA_CCDF_STATUS_SDE		BIT(1)
#define QDMA_CCDF_STATUS_DDE		BIT(0)
#define QDMA_CCDF_STATUS_MASK		(QDMA_CCDF_STATUS_RTE | \
					QDMA_CCDF_STATUS_WTE | \
					QDMA_CCDF_STATUS_CDE | \
					QDMA_CCDF_STATUS_SDE | \
					QDMA_CCDF_STATUS_DDE)

/* Field definition for Descriptor offset */
#define QDMA_CCDF_OFFSET		20
#define QDMA_SDDF_CMD(x)		(((u64)(x)) << 32)

/* Field definition for safe loop count*/
#define FSL_QDMA_HALT_COUNT		1500
#define FSL_QDMA_MAX_SIZE		16385
#define	FSL_QDMA_COMP_TIMEOUT		1000
#define FSL_COMMAND_QUEUE_OVERFLLOW	10

#define FSL_QDMA_BLOCK_BASE_OFFSET(fsl_qdma_engine, x)			\
	(((fsl_qdma_engine)->block_offset) * (x))

/**
 * struct fsl_qdma_format - This is the struct holding describing compound
 *			    descriptor format with qDMA.
 * @status:		    Command status and enqueue status notification.
 * @cfg:		    Frame offset and frame format.
 * @addr_lo:		    Holding the compound descriptor of the lower
 *			    32-bits address in memory 40-bit address.
 * @addr_hi:		    Same as above member, but point high 8-bits in
 *			    memory 40-bit address.
 * @__reserved1:	    Reserved field.
 * @cfg8b_w1:		    Compound descriptor command queue origin produced
 *			    by qDMA and dynamic debug field.
 * @data:		    Pointer to the memory 40-bit address, describes DMA
 *			    source information and DMA destination information.
 */
struct fsl_qdma_format {
	__le32 status;
	__le32 cfg;
	union {
		struct {
			__le32 addr_lo;
			u8 addr_hi;
			u8 __reserved1[2];
			u8 cfg8b_w1;
		} __packed;
		__le64 data;
	};
} __packed;

/* qDMA status notification pre information */
struct fsl_pre_status {
	u64 addr;
	u8 queue;
};

static DEFINE_PER_CPU(struct fsl_pre_status, pre);

struct fsl_qdma_chan {
	struct virt_dma_chan		vchan;
	struct virt_dma_desc		vdesc;
	enum dma_status			status;
	struct fsl_qdma_engine		*qdma;
	struct fsl_qdma_queue		*queue;
};

struct fsl_qdma_queue {
	struct fsl_qdma_format	*virt_head;
	struct fsl_qdma_format	*virt_tail;
	struct list_head	comp_used;
	struct list_head	comp_free;
	struct dma_pool		*comp_pool;
	struct dma_pool		*desc_pool;
	spinlock_t		queue_lock;
	dma_addr_t		bus_addr;
	u32                     n_cq;
	u32			id;
	struct fsl_qdma_format	*cq;
	void __iomem		*block_base;
};

struct fsl_qdma_comp {
	dma_addr_t              bus_addr;
	dma_addr_t              desc_bus_addr;
	struct fsl_qdma_format	*virt_addr;
	struct fsl_qdma_format	*desc_virt_addr;
	struct fsl_qdma_chan	*qchan;
	struct virt_dma_desc    vdesc;
	struct list_head	list;
};

struct fsl_qdma_engine {
	struct dma_device	dma_dev;
	void __iomem		*ctrl_base;
	void __iomem            *status_base;
	void __iomem		*block_base;
	u32			n_chans;
	u32			n_queues;
	struct mutex            fsl_qdma_mutex;
	int			error_irq;
	int			*queue_irq;
	u32			feature;
	struct fsl_qdma_queue	*queue;
	struct fsl_qdma_queue	**status;
	struct fsl_qdma_chan	*chans;
	int			block_number;
	int			block_offset;
	int			irq_base;
	int			desc_allocated;

};

static inline u64
qdma_ccdf_addr_get64(const struct fsl_qdma_format *ccdf)
{
	return le64_to_cpu(ccdf->data) & (U64_MAX >> 24);
}

static inline void
qdma_desc_addr_set64(struct fsl_qdma_format *ccdf, u64 addr)
{
	ccdf->addr_hi = upper_32_bits(addr);
	ccdf->addr_lo = cpu_to_le32(lower_32_bits(addr));
}

static inline u8
qdma_ccdf_get_queue(const struct fsl_qdma_format *ccdf)
{
	return ccdf->cfg8b_w1 & U8_MAX;
}

static inline int
qdma_ccdf_get_offset(const struct fsl_qdma_format *ccdf)
{
	return (le32_to_cpu(ccdf->cfg) & QDMA_CCDF_MASK) >> QDMA_CCDF_OFFSET;
}

static inline void
qdma_ccdf_set_format(struct fsl_qdma_format *ccdf, int offset)
{
	ccdf->cfg = cpu_to_le32(QDMA_CCDF_FORMAT |
				(offset << QDMA_CCDF_OFFSET));
}

static inline int
qdma_ccdf_get_status(const struct fsl_qdma_format *ccdf)
{
	return (le32_to_cpu(ccdf->status) & QDMA_CCDF_STATUS_MASK);
}

static inline void
qdma_ccdf_set_ser(struct fsl_qdma_format *ccdf, int status)
{
	ccdf->status = cpu_to_le32(QDMA_CCDF_SER | status);
}

static inline void qdma_csgf_set_len(struct fsl_qdma_format *csgf, int len)
{
	csgf->cfg = cpu_to_le32(len & QDMA_SG_LEN_MASK);
}

static inline void qdma_csgf_set_f(struct fsl_qdma_format *csgf, int len)
{
	csgf->cfg = cpu_to_le32(QDMA_SG_FIN | (len & QDMA_SG_LEN_MASK));
}

static u32 qdma_readl(struct fsl_qdma_engine *qdma, void __iomem *addr)
{
	return FSL_DMA_IN(qdma, addr, 32);
}

static void qdma_writel(struct fsl_qdma_engine *qdma, u32 val,
			void __iomem *addr)
{
	FSL_DMA_OUT(qdma, addr, val, 32);
}

static struct fsl_qdma_chan *to_fsl_qdma_chan(struct dma_chan *chan)
{
	return container_of(chan, struct fsl_qdma_chan, vchan.chan);
}

static struct fsl_qdma_comp *to_fsl_qdma_comp(struct virt_dma_desc *vd)
{
	return container_of(vd, struct fsl_qdma_comp, vdesc);
}

static void fsl_qdma_free_chan_resources(struct dma_chan *chan)
{
	struct fsl_qdma_chan *fsl_chan = to_fsl_qdma_chan(chan);
	struct fsl_qdma_queue *fsl_queue = fsl_chan->queue;
	struct fsl_qdma_engine *fsl_qdma = fsl_chan->qdma;
	struct fsl_qdma_comp *comp_temp, *_comp_temp;
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&fsl_chan->vchan.lock, flags);
	vchan_get_all_descriptors(&fsl_chan->vchan, &head);
	spin_unlock_irqrestore(&fsl_chan->vchan.lock, flags);

	vchan_dma_desc_free_list(&fsl_chan->vchan, &head);

	if (!fsl_queue->comp_pool && !fsl_queue->desc_pool)
		return;

	list_for_each_entry_safe(comp_temp, _comp_temp,
				 &fsl_queue->comp_used,	list) {
		dma_pool_free(fsl_queue->comp_pool,
			      comp_temp->virt_addr,
			      comp_temp->bus_addr);
		dma_pool_free(fsl_queue->desc_pool,
			      comp_temp->desc_virt_addr,
			      comp_temp->desc_bus_addr);
		list_del(&comp_temp->list);
		kfree(comp_temp);
	}

	list_for_each_entry_safe(comp_temp, _comp_temp,
				 &fsl_queue->comp_free, list) {
		dma_pool_free(fsl_queue->comp_pool,
			      comp_temp->virt_addr,
			      comp_temp->bus_addr);
		dma_pool_free(fsl_queue->desc_pool,
			      comp_temp->desc_virt_addr,
			      comp_temp->desc_bus_addr);
		list_del(&comp_temp->list);
		kfree(comp_temp);
	}

	dma_pool_destroy(fsl_queue->comp_pool);
	dma_pool_destroy(fsl_queue->desc_pool);

	fsl_qdma->desc_allocated--;
	fsl_queue->comp_pool = NULL;
	fsl_queue->desc_pool = NULL;
}

static void fsl_qdma_comp_fill_memcpy(struct fsl_qdma_comp *fsl_comp,
				      dma_addr_t dst, dma_addr_t src, u32 len)
{
	u32 cmd;
	struct fsl_qdma_format *sdf, *ddf;
	struct fsl_qdma_format *ccdf, *csgf_desc, *csgf_src, *csgf_dest;

	ccdf = fsl_comp->virt_addr;
	csgf_desc = fsl_comp->virt_addr + 1;
	csgf_src = fsl_comp->virt_addr + 2;
	csgf_dest = fsl_comp->virt_addr + 3;
	sdf = fsl_comp->desc_virt_addr;
	ddf = fsl_comp->desc_virt_addr + 1;

	memset(fsl_comp->virt_addr, 0, FSL_QDMA_COMMAND_BUFFER_SIZE);
	memset(fsl_comp->desc_virt_addr, 0, FSL_QDMA_DESCRIPTOR_BUFFER_SIZE);
	/* Head Command Descriptor(Frame Descriptor) */
	qdma_desc_addr_set64(ccdf, fsl_comp->bus_addr + 16);
	qdma_ccdf_set_format(ccdf, qdma_ccdf_get_offset(ccdf));
	qdma_ccdf_set_ser(ccdf, qdma_ccdf_get_status(ccdf));
	/* Status notification is enqueued to status queue. */
	/* Compound Command Descriptor(Frame List Table) */
	qdma_desc_addr_set64(csgf_desc, fsl_comp->desc_bus_addr);
	/* It must be 32 as Compound S/G Descriptor */
	qdma_csgf_set_len(csgf_desc, 32);
	qdma_desc_addr_set64(csgf_src, src);
	qdma_csgf_set_len(csgf_src, len);
	qdma_desc_addr_set64(csgf_dest, dst);
	qdma_csgf_set_len(csgf_dest, len);
	/* This entry is the last entry. */
	qdma_csgf_set_f(csgf_dest, len);
	/* Descriptor Buffer */
	cmd = cpu_to_le32(FSL_QDMA_CMD_RWTTYPE <<
			  FSL_QDMA_CMD_RWTTYPE_OFFSET);
	sdf->data = QDMA_SDDF_CMD(cmd);

	cmd = cpu_to_le32(FSL_QDMA_CMD_RWTTYPE <<
			  FSL_QDMA_CMD_RWTTYPE_OFFSET);
	cmd |= cpu_to_le32(FSL_QDMA_CMD_LWC << FSL_QDMA_CMD_LWC_OFFSET);
	ddf->data = QDMA_SDDF_CMD(cmd);
}

/*
 * Pre-request full command descriptor for enqueue.
 */
static int fsl_qdma_pre_request_enqueue_desc(struct fsl_qdma_queue *queue)
{
	int i;
	struct fsl_qdma_comp *comp_temp, *_comp_temp;

	for (i = 0; i < queue->n_cq + FSL_COMMAND_QUEUE_OVERFLLOW; i++) {
		comp_temp = kzalloc(sizeof(*comp_temp), GFP_KERNEL);
		if (!comp_temp)
			goto err_alloc;
		comp_temp->virt_addr =
			dma_pool_alloc(queue->comp_pool, GFP_KERNEL,
				       &comp_temp->bus_addr);
		if (!comp_temp->virt_addr)
			goto err_dma_alloc;

		comp_temp->desc_virt_addr =
			dma_pool_alloc(queue->desc_pool, GFP_KERNEL,
				       &comp_temp->desc_bus_addr);
		if (!comp_temp->desc_virt_addr)
			goto err_desc_dma_alloc;

		list_add_tail(&comp_temp->list, &queue->comp_free);
	}

	return 0;

err_desc_dma_alloc:
	dma_pool_free(queue->comp_pool, comp_temp->virt_addr,
		      comp_temp->bus_addr);

err_dma_alloc:
	kfree(comp_temp);

err_alloc:
	list_for_each_entry_safe(comp_temp, _comp_temp,
				 &queue->comp_free, list) {
		if (comp_temp->virt_addr)
			dma_pool_free(queue->comp_pool,
				      comp_temp->virt_addr,
				      comp_temp->bus_addr);
		if (comp_temp->desc_virt_addr)
			dma_pool_free(queue->desc_pool,
				      comp_temp->desc_virt_addr,
				      comp_temp->desc_bus_addr);

		list_del(&comp_temp->list);
		kfree(comp_temp);
	}

	return -ENOMEM;
}

/*
 * Request a command descriptor for enqueue.
 */
static struct fsl_qdma_comp
*fsl_qdma_request_enqueue_desc(struct fsl_qdma_chan *fsl_chan)
{
	unsigned long flags;
	struct fsl_qdma_comp *comp_temp;
	int timeout = FSL_QDMA_COMP_TIMEOUT;
	struct fsl_qdma_queue *queue = fsl_chan->queue;

	while (timeout--) {
		spin_lock_irqsave(&queue->queue_lock, flags);
		if (!list_empty(&queue->comp_free)) {
			comp_temp = list_first_entry(&queue->comp_free,
						     struct fsl_qdma_comp,
						     list);
			list_del(&comp_temp->list);

			spin_unlock_irqrestore(&queue->queue_lock, flags);
			comp_temp->qchan = fsl_chan;
			return comp_temp;
		}
		spin_unlock_irqrestore(&queue->queue_lock, flags);
		udelay(1);
	}

	return NULL;
}

static struct fsl_qdma_queue
*fsl_qdma_alloc_queue_resources(struct platform_device *pdev,
				struct fsl_qdma_engine *fsl_qdma)
{
	int ret, len, i, j;
	int queue_num, block_number;
	unsigned int queue_size[FSL_QDMA_QUEUE_MAX];
	struct fsl_qdma_queue *queue_head, *queue_temp;

	queue_num = fsl_qdma->n_queues;
	block_number = fsl_qdma->block_number;

	if (queue_num > FSL_QDMA_QUEUE_MAX)
		queue_num = FSL_QDMA_QUEUE_MAX;
	len = sizeof(*queue_head) * queue_num * block_number;
	queue_head = devm_kzalloc(&pdev->dev, len, GFP_KERNEL);
	if (!queue_head)
		return NULL;

	ret = device_property_read_u32_array(&pdev->dev, "queue-sizes",
					     queue_size, queue_num);
	if (ret) {
		dev_err(&pdev->dev, "Can't get queue-sizes.\n");
		return NULL;
	}
	for (j = 0; j < block_number; j++) {
		for (i = 0; i < queue_num; i++) {
			if (queue_size[i] > FSL_QDMA_CIRCULAR_DESC_SIZE_MAX ||
			    queue_size[i] < FSL_QDMA_CIRCULAR_DESC_SIZE_MIN) {
				dev_err(&pdev->dev,
					"Get wrong queue-sizes.\n");
				return NULL;
			}
			queue_temp = queue_head + i + (j * queue_num);

			queue_temp->cq =
			dmam_alloc_coherent(&pdev->dev,
					    sizeof(struct fsl_qdma_format) *
					    queue_size[i],
					    &queue_temp->bus_addr,
					    GFP_KERNEL);
			if (!queue_temp->cq)
				return NULL;
			queue_temp->block_base = fsl_qdma->block_base +
				FSL_QDMA_BLOCK_BASE_OFFSET(fsl_qdma, j);
			queue_temp->n_cq = queue_size[i];
			queue_temp->id = i;
			queue_temp->virt_head = queue_temp->cq;
			queue_temp->virt_tail = queue_temp->cq;
			/*
			 * List for queue command buffer
			 */
			INIT_LIST_HEAD(&queue_temp->comp_used);
			spin_lock_init(&queue_temp->queue_lock);
		}
	}
	return queue_head;
}

static struct fsl_qdma_queue
*fsl_qdma_prep_status_queue(struct platform_device *pdev)
{
	int ret;
	unsigned int status_size;
	struct fsl_qdma_queue *status_head;
	struct device_node *np = pdev->dev.of_node;

	ret = of_property_read_u32(np, "status-sizes", &status_size);
	if (ret) {
		dev_err(&pdev->dev, "Can't get status-sizes.\n");
		return NULL;
	}
	if (status_size > FSL_QDMA_CIRCULAR_DESC_SIZE_MAX ||
	    status_size < FSL_QDMA_CIRCULAR_DESC_SIZE_MIN) {
		dev_err(&pdev->dev, "Get wrong status_size.\n");
		return NULL;
	}
	status_head = devm_kzalloc(&pdev->dev,
				   sizeof(*status_head), GFP_KERNEL);
	if (!status_head)
		return NULL;

	/*
	 * Buffer for queue command
	 */
	status_head->cq = dmam_alloc_coherent(&pdev->dev,
					      sizeof(struct fsl_qdma_format) *
					      status_size,
					      &status_head->bus_addr,
					      GFP_KERNEL);
	if (!status_head->cq) {
		devm_kfree(&pdev->dev, status_head);
		return NULL;
	}
	status_head->n_cq = status_size;
	status_head->virt_head = status_head->cq;
	status_head->virt_tail = status_head->cq;
	status_head->comp_pool = NULL;

	return status_head;
}

static int fsl_qdma_halt(struct fsl_qdma_engine *fsl_qdma)
{
	u32 reg;
	int i, j, count = FSL_QDMA_HALT_COUNT;
	void __iomem *block, *ctrl = fsl_qdma->ctrl_base;

	/* Disable the command queue and wait for idle state. */
	reg = qdma_readl(fsl_qdma, ctrl + FSL_QDMA_DMR);
	reg |= FSL_QDMA_DMR_DQD;
	qdma_writel(fsl_qdma, reg, ctrl + FSL_QDMA_DMR);
	for (j = 0; j < fsl_qdma->block_number; j++) {
		block = fsl_qdma->block_base +
			FSL_QDMA_BLOCK_BASE_OFFSET(fsl_qdma, j);
		for (i = 0; i < FSL_QDMA_QUEUE_NUM_MAX; i++)
			qdma_writel(fsl_qdma, 0, block + FSL_QDMA_BCQMR(i));
	}
	while (1) {
		reg = qdma_readl(fsl_qdma, ctrl + FSL_QDMA_DSR);
		if (!(reg & FSL_QDMA_DSR_DB))
			break;
		if (count-- < 0)
			return -EBUSY;
		udelay(100);
	}

	for (j = 0; j < fsl_qdma->block_number; j++) {
		block = fsl_qdma->block_base +
			FSL_QDMA_BLOCK_BASE_OFFSET(fsl_qdma, j);

		/* Disable status queue. */
		qdma_writel(fsl_qdma, 0, block + FSL_QDMA_BSQMR);

		/*
		 * clear the command queue interrupt detect register for
		 * all queues.
		 */
		qdma_writel(fsl_qdma, FSL_QDMA_BCQIDR_CLEAR,
			    block + FSL_QDMA_BCQIDR(0));
	}

	return 0;
}

static int
fsl_qdma_queue_transfer_complete(struct fsl_qdma_engine *fsl_qdma,
				 void *block,
				 int id)
{
	bool duplicate;
	u32 reg, i, count;
	u8 completion_status;
	struct fsl_qdma_queue *temp_queue;
	struct fsl_qdma_format *status_addr;
	struct fsl_qdma_comp *fsl_comp = NULL;
	struct fsl_qdma_queue *fsl_queue = fsl_qdma->queue;
	struct fsl_qdma_queue *fsl_status = fsl_qdma->status[id];

	count = FSL_QDMA_MAX_SIZE;

	while (count--) {
		duplicate = 0;
		reg = qdma_readl(fsl_qdma, block + FSL_QDMA_BSQSR);
		if (reg & FSL_QDMA_BSQSR_QE)
			return 0;

		status_addr = fsl_status->virt_head;

		if (qdma_ccdf_get_queue(status_addr) ==
		   __this_cpu_read(pre.queue) &&
			qdma_ccdf_addr_get64(status_addr) ==
			__this_cpu_read(pre.addr))
			duplicate = 1;
		i = qdma_ccdf_get_queue(status_addr) +
			id * fsl_qdma->n_queues;
		__this_cpu_write(pre.addr, qdma_ccdf_addr_get64(status_addr));
		__this_cpu_write(pre.queue, qdma_ccdf_get_queue(status_addr));
		temp_queue = fsl_queue + i;

		spin_lock(&temp_queue->queue_lock);
		if (list_empty(&temp_queue->comp_used)) {
			if (!duplicate) {
				spin_unlock(&temp_queue->queue_lock);
				return -EAGAIN;
			}
		} else {
			fsl_comp = list_first_entry(&temp_queue->comp_used,
						    struct fsl_qdma_comp, list);
			if (fsl_comp->bus_addr + 16 !=
				__this_cpu_read(pre.addr)) {
				if (!duplicate) {
					spin_unlock(&temp_queue->queue_lock);
					return -EAGAIN;
				}
			}
		}

		if (duplicate) {
			reg = qdma_readl(fsl_qdma, block + FSL_QDMA_BSQMR);
			reg |= FSL_QDMA_BSQMR_DI;
			qdma_desc_addr_set64(status_addr, 0x0);
			fsl_status->virt_head++;
			if (fsl_status->virt_head == fsl_status->cq
						   + fsl_status->n_cq)
				fsl_status->virt_head = fsl_status->cq;
			qdma_writel(fsl_qdma, reg, block + FSL_QDMA_BSQMR);
			spin_unlock(&temp_queue->queue_lock);
			continue;
		}
		list_del(&fsl_comp->list);

		completion_status = qdma_ccdf_get_status(status_addr);

		reg = qdma_readl(fsl_qdma, block + FSL_QDMA_BSQMR);
		reg |= FSL_QDMA_BSQMR_DI;
		qdma_desc_addr_set64(status_addr, 0x0);
		fsl_status->virt_head++;
		if (fsl_status->virt_head == fsl_status->cq + fsl_status->n_cq)
			fsl_status->virt_head = fsl_status->cq;
		qdma_writel(fsl_qdma, reg, block + FSL_QDMA_BSQMR);
		spin_unlock(&temp_queue->queue_lock);

		/* The completion_status is evaluated here
		 * (outside of spin lock)
		 */
		if (completion_status) {
			/* A completion error occurred! */
			if (completion_status & QDMA_CCDF_STATUS_WTE) {
				/* Write transaction error */
				fsl_comp->vdesc.tx_result.result =
					DMA_TRANS_WRITE_FAILED;
			} else if (completion_status & QDMA_CCDF_STATUS_RTE) {
				/* Read transaction error */
				fsl_comp->vdesc.tx_result.result =
					DMA_TRANS_READ_FAILED;
			} else {
				/* Command/source/destination
				 * description error
				 */
				fsl_comp->vdesc.tx_result.result =
					DMA_TRANS_ABORTED;
				dev_err(fsl_qdma->dma_dev.dev,
					"DMA status descriptor error %x\n",
					completion_status);
			}
		}

		spin_lock(&fsl_comp->qchan->vchan.lock);
		vchan_cookie_complete(&fsl_comp->vdesc);
		fsl_comp->qchan->status = DMA_COMPLETE;
		spin_unlock(&fsl_comp->qchan->vchan.lock);
	}

	return 0;
}

static irqreturn_t fsl_qdma_error_handler(int irq, void *dev_id)
{
	unsigned int intr;
	struct fsl_qdma_engine *fsl_qdma = dev_id;
	void __iomem *status = fsl_qdma->status_base;
	unsigned int decfdw0r;
	unsigned int decfdw1r;
	unsigned int decfdw2r;
	unsigned int decfdw3r;

	intr = qdma_readl(fsl_qdma, status + FSL_QDMA_DEDR);

	if (intr) {
		decfdw0r = qdma_readl(fsl_qdma, status + FSL_QDMA_DECFDW0R);
		decfdw1r = qdma_readl(fsl_qdma, status + FSL_QDMA_DECFDW1R);
		decfdw2r = qdma_readl(fsl_qdma, status + FSL_QDMA_DECFDW2R);
		decfdw3r = qdma_readl(fsl_qdma, status + FSL_QDMA_DECFDW3R);
		dev_err(fsl_qdma->dma_dev.dev,
			"DMA transaction error! (%x: %x-%x-%x-%x)\n",
			intr, decfdw0r, decfdw1r, decfdw2r, decfdw3r);
	}

	qdma_writel(fsl_qdma, FSL_QDMA_DEDR_CLEAR, status + FSL_QDMA_DEDR);
	return IRQ_HANDLED;
}

static irqreturn_t fsl_qdma_queue_handler(int irq, void *dev_id)
{
	int id;
	unsigned int intr, reg;
	struct fsl_qdma_engine *fsl_qdma = dev_id;
	void __iomem *block, *ctrl = fsl_qdma->ctrl_base;

	id = irq - fsl_qdma->irq_base;
	if (id < 0 && id > fsl_qdma->block_number) {
		dev_err(fsl_qdma->dma_dev.dev,
			"irq %d is wrong irq_base is %d\n",
			irq, fsl_qdma->irq_base);
	}

	block = fsl_qdma->block_base +
		FSL_QDMA_BLOCK_BASE_OFFSET(fsl_qdma, id);

	intr = qdma_readl(fsl_qdma, block + FSL_QDMA_BCQIDR(0));

	if ((intr & FSL_QDMA_CQIDR_SQT) != 0)
		intr = fsl_qdma_queue_transfer_complete(fsl_qdma, block, id);

	if (intr != 0) {
		reg = qdma_readl(fsl_qdma, ctrl + FSL_QDMA_DMR);
		reg |= FSL_QDMA_DMR_DQD;
		qdma_writel(fsl_qdma, reg, ctrl + FSL_QDMA_DMR);
		qdma_writel(fsl_qdma, 0, block + FSL_QDMA_BCQIER(0));
		dev_err(fsl_qdma->dma_dev.dev, "QDMA: status err!\n");
	}

	/* Clear all detected events and interrupts. */
	qdma_writel(fsl_qdma, FSL_QDMA_BCQIDR_CLEAR,
		    block + FSL_QDMA_BCQIDR(0));

	return IRQ_HANDLED;
}

static int
fsl_qdma_irq_init(struct platform_device *pdev,
		  struct fsl_qdma_engine *fsl_qdma)
{
	int i;
	int cpu;
	int ret;
	char irq_name[32];

	fsl_qdma->error_irq =
		platform_get_irq_byname(pdev, "qdma-error");
	if (fsl_qdma->error_irq < 0)
		return fsl_qdma->error_irq;

	ret = devm_request_irq(&pdev->dev, fsl_qdma->error_irq,
			       fsl_qdma_error_handler, 0,
			       "qDMA error", fsl_qdma);
	if (ret) {
		dev_err(&pdev->dev, "Can't register qDMA controller IRQ.\n");
		return  ret;
	}

	for (i = 0; i < fsl_qdma->block_number; i++) {
		sprintf(irq_name, "qdma-queue%d", i);
		fsl_qdma->queue_irq[i] =
				platform_get_irq_byname(pdev, irq_name);

		if (fsl_qdma->queue_irq[i] < 0)
			return fsl_qdma->queue_irq[i];

		ret = devm_request_irq(&pdev->dev,
				       fsl_qdma->queue_irq[i],
				       fsl_qdma_queue_handler,
				       0,
				       "qDMA queue",
				       fsl_qdma);
		if (ret) {
			dev_err(&pdev->dev,
				"Can't register qDMA queue IRQ.\n");
			return  ret;
		}

		cpu = i % num_online_cpus();
		ret = irq_set_affinity_hint(fsl_qdma->queue_irq[i],
					    get_cpu_mask(cpu));
		if (ret) {
			dev_err(&pdev->dev,
				"Can't set cpu %d affinity to IRQ %d.\n",
				cpu,
				fsl_qdma->queue_irq[i]);
			return  ret;
		}
	}

	return 0;
}

static void fsl_qdma_irq_exit(struct platform_device *pdev,
			      struct fsl_qdma_engine *fsl_qdma)
{
	int i;

	devm_free_irq(&pdev->dev, fsl_qdma->error_irq, fsl_qdma);
	for (i = 0; i < fsl_qdma->block_number; i++)
		devm_free_irq(&pdev->dev, fsl_qdma->queue_irq[i], fsl_qdma);
}

static int fsl_qdma_reg_init(struct fsl_qdma_engine *fsl_qdma)
{
	u32 reg;
	int i, j, ret;
	struct fsl_qdma_queue *temp;
	void __iomem *status = fsl_qdma->status_base;
	void __iomem *block, *ctrl = fsl_qdma->ctrl_base;
	struct fsl_qdma_queue *fsl_queue = fsl_qdma->queue;

	/* Try to halt the qDMA engine first. */
	ret = fsl_qdma_halt(fsl_qdma);
	if (ret) {
		dev_err(fsl_qdma->dma_dev.dev, "DMA halt failed!");
		return ret;
	}

	for (i = 0; i < fsl_qdma->block_number; i++) {
		/*
		 * Clear the command queue interrupt detect register for
		 * all queues.
		 */

		block = fsl_qdma->block_base +
			FSL_QDMA_BLOCK_BASE_OFFSET(fsl_qdma, i);
		qdma_writel(fsl_qdma, FSL_QDMA_BCQIDR_CLEAR,
			    block + FSL_QDMA_BCQIDR(0));
	}

	for (j = 0; j < fsl_qdma->block_number; j++) {
		block = fsl_qdma->block_base +
			FSL_QDMA_BLOCK_BASE_OFFSET(fsl_qdma, j);
		for (i = 0; i < fsl_qdma->n_queues; i++) {
			temp = fsl_queue + i + (j * fsl_qdma->n_queues);
			/*
			 * Initialize Command Queue registers to
			 * point to the first
			 * command descriptor in memory.
			 * Dequeue Pointer Address Registers
			 * Enqueue Pointer Address Registers
			 */

			qdma_writel(fsl_qdma, temp->bus_addr,
				    block + FSL_QDMA_BCQDPA_SADDR(i));
			qdma_writel(fsl_qdma, temp->bus_addr,
				    block + FSL_QDMA_BCQEPA_SADDR(i));

			/* Initialize the queue mode. */
			reg = FSL_QDMA_BCQMR_EN;
			reg |= FSL_QDMA_BCQMR_CD_THLD(ilog2(temp->n_cq) - 4);
			reg |= FSL_QDMA_BCQMR_CQ_SIZE(ilog2(temp->n_cq) - 6);
			qdma_writel(fsl_qdma, reg, block + FSL_QDMA_BCQMR(i));
		}

		/*
		 * Workaround for erratum: ERR010812.
		 * We must enable XOFF to avoid the enqueue rejection occurs.
		 * Setting SQCCMR ENTER_WM to 0x20.
		 */

		qdma_writel(fsl_qdma, FSL_QDMA_SQCCMR_ENTER_WM,
			    block + FSL_QDMA_SQCCMR);

		/*
		 * Initialize status queue registers to point to the first
		 * command descriptor in memory.
		 * Dequeue Pointer Address Registers
		 * Enqueue Pointer Address Registers
		 */

		qdma_writel(fsl_qdma, fsl_qdma->status[j]->bus_addr,
			    block + FSL_QDMA_SQEPAR);
		qdma_writel(fsl_qdma, fsl_qdma->status[j]->bus_addr,
			    block + FSL_QDMA_SQDPAR);
		/* Initialize status queue interrupt. */
		qdma_writel(fsl_qdma, FSL_QDMA_BCQIER_CQTIE,
			    block + FSL_QDMA_BCQIER(0));
		qdma_writel(fsl_qdma, FSL_QDMA_BSQICR_ICEN |
				   FSL_QDMA_BSQICR_ICST(5) | 0x8000,
				   block + FSL_QDMA_BSQICR);
		qdma_writel(fsl_qdma, FSL_QDMA_CQIER_MEIE |
				   FSL_QDMA_CQIER_TEIE,
				   block + FSL_QDMA_CQIER);

		/* Initialize the status queue mode. */
		reg = FSL_QDMA_BSQMR_EN;
		reg |= FSL_QDMA_BSQMR_CQ_SIZE(ilog2
			(fsl_qdma->status[j]->n_cq) - 6);

		qdma_writel(fsl_qdma, reg, block + FSL_QDMA_BSQMR);
		reg = qdma_readl(fsl_qdma, block + FSL_QDMA_BSQMR);
	}

	/* Initialize controller interrupt register. */
	qdma_writel(fsl_qdma, FSL_QDMA_DEDR_CLEAR, status + FSL_QDMA_DEDR);
	qdma_writel(fsl_qdma, FSL_QDMA_DEIER_CLEAR, status + FSL_QDMA_DEIER);

	reg = qdma_readl(fsl_qdma, ctrl + FSL_QDMA_DMR);
	reg &= ~FSL_QDMA_DMR_DQD;
	qdma_writel(fsl_qdma, reg, ctrl + FSL_QDMA_DMR);

	return 0;
}

static struct dma_async_tx_descriptor *
fsl_qdma_prep_memcpy(struct dma_chan *chan, dma_addr_t dst,
		     dma_addr_t src, size_t len, unsigned long flags)
{
	struct fsl_qdma_comp *fsl_comp;
	struct fsl_qdma_chan *fsl_chan = to_fsl_qdma_chan(chan);

	fsl_comp = fsl_qdma_request_enqueue_desc(fsl_chan);

	if (!fsl_comp)
		return NULL;

	fsl_qdma_comp_fill_memcpy(fsl_comp, dst, src, len);

	return vchan_tx_prep(&fsl_chan->vchan, &fsl_comp->vdesc, flags);
}

static void fsl_qdma_enqueue_desc(struct fsl_qdma_chan *fsl_chan)
{
	u32 reg;
	struct virt_dma_desc *vdesc;
	struct fsl_qdma_comp *fsl_comp;
	struct fsl_qdma_queue *fsl_queue = fsl_chan->queue;
	void __iomem *block = fsl_queue->block_base;

	reg = qdma_readl(fsl_chan->qdma, block + FSL_QDMA_BCQSR(fsl_queue->id));
	if (reg & (FSL_QDMA_BCQSR_QF | FSL_QDMA_BCQSR_XOFF))
		return;
	vdesc = vchan_next_desc(&fsl_chan->vchan);
	if (!vdesc)
		return;
	list_del(&vdesc->node);
	fsl_comp = to_fsl_qdma_comp(vdesc);

	memcpy(fsl_queue->virt_head++,
	       fsl_comp->virt_addr, sizeof(struct fsl_qdma_format));
	if (fsl_queue->virt_head == fsl_queue->cq + fsl_queue->n_cq)
		fsl_queue->virt_head = fsl_queue->cq;

	list_add_tail(&fsl_comp->list, &fsl_queue->comp_used);
	barrier();
	reg = qdma_readl(fsl_chan->qdma, block + FSL_QDMA_BCQMR(fsl_queue->id));
	reg |= FSL_QDMA_BCQMR_EI;
	qdma_writel(fsl_chan->qdma, reg, block + FSL_QDMA_BCQMR(fsl_queue->id));
	fsl_chan->status = DMA_IN_PROGRESS;
}

static void fsl_qdma_free_desc(struct virt_dma_desc *vdesc)
{
	unsigned long flags;
	struct fsl_qdma_comp *fsl_comp;
	struct fsl_qdma_queue *fsl_queue;

	fsl_comp = to_fsl_qdma_comp(vdesc);
	fsl_queue = fsl_comp->qchan->queue;

	spin_lock_irqsave(&fsl_queue->queue_lock, flags);
	list_add_tail(&fsl_comp->list, &fsl_queue->comp_free);
	spin_unlock_irqrestore(&fsl_queue->queue_lock, flags);
}

static void fsl_qdma_issue_pending(struct dma_chan *chan)
{
	unsigned long flags;
	struct fsl_qdma_chan *fsl_chan = to_fsl_qdma_chan(chan);
	struct fsl_qdma_queue *fsl_queue = fsl_chan->queue;

	spin_lock_irqsave(&fsl_queue->queue_lock, flags);
	spin_lock(&fsl_chan->vchan.lock);
	if (vchan_issue_pending(&fsl_chan->vchan))
		fsl_qdma_enqueue_desc(fsl_chan);
	spin_unlock(&fsl_chan->vchan.lock);
	spin_unlock_irqrestore(&fsl_queue->queue_lock, flags);
}

static void fsl_qdma_synchronize(struct dma_chan *chan)
{
	struct fsl_qdma_chan *fsl_chan = to_fsl_qdma_chan(chan);

	vchan_synchronize(&fsl_chan->vchan);
}

static int fsl_qdma_terminate_all(struct dma_chan *chan)
{
	LIST_HEAD(head);
	unsigned long flags;
	struct fsl_qdma_chan *fsl_chan = to_fsl_qdma_chan(chan);

	spin_lock_irqsave(&fsl_chan->vchan.lock, flags);
	vchan_get_all_descriptors(&fsl_chan->vchan, &head);
	spin_unlock_irqrestore(&fsl_chan->vchan.lock, flags);
	vchan_dma_desc_free_list(&fsl_chan->vchan, &head);
	return 0;
}

static int fsl_qdma_alloc_chan_resources(struct dma_chan *chan)
{
	int ret;
	struct fsl_qdma_chan *fsl_chan = to_fsl_qdma_chan(chan);
	struct fsl_qdma_engine *fsl_qdma = fsl_chan->qdma;
	struct fsl_qdma_queue *fsl_queue = fsl_chan->queue;

	if (fsl_queue->comp_pool && fsl_queue->desc_pool)
		return fsl_qdma->desc_allocated;

	INIT_LIST_HEAD(&fsl_queue->comp_free);

	/*
	 * The dma pool for queue command buffer
	 */
	fsl_queue->comp_pool =
	dma_pool_create("comp_pool",
			chan->device->dev,
			FSL_QDMA_COMMAND_BUFFER_SIZE,
			64, 0);
	if (!fsl_queue->comp_pool)
		return -ENOMEM;

	/*
	 * The dma pool for Descriptor(SD/DD) buffer
	 */
	fsl_queue->desc_pool =
	dma_pool_create("desc_pool",
			chan->device->dev,
			FSL_QDMA_DESCRIPTOR_BUFFER_SIZE,
			32, 0);
	if (!fsl_queue->desc_pool)
		goto err_desc_pool;

	ret = fsl_qdma_pre_request_enqueue_desc(fsl_queue);
	if (ret) {
		dev_err(chan->device->dev,
			"failed to alloc dma buffer for S/G descriptor\n");
		goto err_mem;
	}

	fsl_qdma->desc_allocated++;
	return fsl_qdma->desc_allocated;

err_mem:
	dma_pool_destroy(fsl_queue->desc_pool);
err_desc_pool:
	dma_pool_destroy(fsl_queue->comp_pool);
	return -ENOMEM;
}

static int fsl_qdma_probe(struct platform_device *pdev)
{
	int ret, i;
	int blk_num, blk_off;
	u32 len, chans, queues;
	struct fsl_qdma_chan *fsl_chan;
	struct fsl_qdma_engine *fsl_qdma;
	struct device_node *np = pdev->dev.of_node;

	ret = of_property_read_u32(np, "dma-channels", &chans);
	if (ret) {
		dev_err(&pdev->dev, "Can't get dma-channels.\n");
		return ret;
	}

	ret = of_property_read_u32(np, "block-offset", &blk_off);
	if (ret) {
		dev_err(&pdev->dev, "Can't get block-offset.\n");
		return ret;
	}

	ret = of_property_read_u32(np, "block-number", &blk_num);
	if (ret) {
		dev_err(&pdev->dev, "Can't get block-number.\n");
		return ret;
	}

	blk_num = min_t(int, blk_num, num_online_cpus());

	len = sizeof(*fsl_qdma);
	fsl_qdma = devm_kzalloc(&pdev->dev, len, GFP_KERNEL);
	if (!fsl_qdma)
		return -ENOMEM;

	len = sizeof(*fsl_chan) * chans;
	fsl_qdma->chans = devm_kzalloc(&pdev->dev, len, GFP_KERNEL);
	if (!fsl_qdma->chans)
		return -ENOMEM;

	len = sizeof(struct fsl_qdma_queue *) * blk_num;
	fsl_qdma->status = devm_kzalloc(&pdev->dev, len, GFP_KERNEL);
	if (!fsl_qdma->status)
		return -ENOMEM;

	len = sizeof(int) * blk_num;
	fsl_qdma->queue_irq = devm_kzalloc(&pdev->dev, len, GFP_KERNEL);
	if (!fsl_qdma->queue_irq)
		return -ENOMEM;

	ret = of_property_read_u32(np, "fsl,dma-queues", &queues);
	if (ret) {
		dev_err(&pdev->dev, "Can't get queues.\n");
		return ret;
	}

	fsl_qdma->desc_allocated = 0;
	fsl_qdma->n_chans = chans;
	fsl_qdma->n_queues = queues;
	fsl_qdma->block_number = blk_num;
	fsl_qdma->block_offset = blk_off;

	mutex_init(&fsl_qdma->fsl_qdma_mutex);

	for (i = 0; i < fsl_qdma->block_number; i++) {
		fsl_qdma->status[i] = fsl_qdma_prep_status_queue(pdev);
		if (!fsl_qdma->status[i])
			return -ENOMEM;
	}
	fsl_qdma->ctrl_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(fsl_qdma->ctrl_base))
		return PTR_ERR(fsl_qdma->ctrl_base);

	fsl_qdma->status_base = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(fsl_qdma->status_base))
		return PTR_ERR(fsl_qdma->status_base);

	fsl_qdma->block_base = devm_platform_ioremap_resource(pdev, 2);
	if (IS_ERR(fsl_qdma->block_base))
		return PTR_ERR(fsl_qdma->block_base);
	fsl_qdma->queue = fsl_qdma_alloc_queue_resources(pdev, fsl_qdma);
	if (!fsl_qdma->queue)
		return -ENOMEM;

	ret = fsl_qdma_irq_init(pdev, fsl_qdma);
	if (ret)
		return ret;

	fsl_qdma->irq_base = platform_get_irq_byname(pdev, "qdma-queue0");
	if (fsl_qdma->irq_base < 0)
		return fsl_qdma->irq_base;

	fsl_qdma->feature = of_property_read_bool(np, "big-endian");
	INIT_LIST_HEAD(&fsl_qdma->dma_dev.channels);

	for (i = 0; i < fsl_qdma->n_chans; i++) {
		struct fsl_qdma_chan *fsl_chan = &fsl_qdma->chans[i];

		fsl_chan->qdma = fsl_qdma;
		fsl_chan->queue = fsl_qdma->queue + i % (fsl_qdma->n_queues *
							fsl_qdma->block_number);
		fsl_chan->vchan.desc_free = fsl_qdma_free_desc;
		vchan_init(&fsl_chan->vchan, &fsl_qdma->dma_dev);
	}

	dma_cap_set(DMA_MEMCPY, fsl_qdma->dma_dev.cap_mask);

	fsl_qdma->dma_dev.dev = &pdev->dev;
	fsl_qdma->dma_dev.device_free_chan_resources =
		fsl_qdma_free_chan_resources;
	fsl_qdma->dma_dev.device_alloc_chan_resources =
		fsl_qdma_alloc_chan_resources;
	fsl_qdma->dma_dev.device_tx_status = dma_cookie_status;
	fsl_qdma->dma_dev.device_prep_dma_memcpy = fsl_qdma_prep_memcpy;
	fsl_qdma->dma_dev.device_issue_pending = fsl_qdma_issue_pending;
	fsl_qdma->dma_dev.device_synchronize = fsl_qdma_synchronize;
	fsl_qdma->dma_dev.device_terminate_all = fsl_qdma_terminate_all;

	ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(40));
	if (ret) {
		dev_err(&pdev->dev, "dma_set_mask failure.\n");
		return ret;
	}

	platform_set_drvdata(pdev, fsl_qdma);

	ret = dma_async_device_register(&fsl_qdma->dma_dev);
	if (ret) {
		dev_err(&pdev->dev,
			"Can't register NXP Layerscape qDMA engine.\n");
		return ret;
	}

	ret = fsl_qdma_reg_init(fsl_qdma);
	if (ret) {
		dev_err(&pdev->dev, "Can't Initialize the qDMA engine.\n");
		return ret;
	}

	return 0;
}

static void fsl_qdma_cleanup_vchan(struct dma_device *dmadev)
{
	struct fsl_qdma_chan *chan, *_chan;

	list_for_each_entry_safe(chan, _chan,
				 &dmadev->channels, vchan.chan.device_node) {
		list_del(&chan->vchan.chan.device_node);
		tasklet_kill(&chan->vchan.task);
	}
}

static void fsl_qdma_remove(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct fsl_qdma_engine *fsl_qdma = platform_get_drvdata(pdev);

	fsl_qdma_irq_exit(pdev, fsl_qdma);
	fsl_qdma_cleanup_vchan(&fsl_qdma->dma_dev);
	of_dma_controller_free(np);
	dma_async_device_unregister(&fsl_qdma->dma_dev);
}

static const struct of_device_id fsl_qdma_dt_ids[] = {
	{ .compatible = "fsl,ls1021a-qdma", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, fsl_qdma_dt_ids);

static struct platform_driver fsl_qdma_driver = {
	.driver		= {
		.name	= "fsl-qdma",
		.of_match_table = fsl_qdma_dt_ids,
	},
	.probe          = fsl_qdma_probe,
	.remove_new	= fsl_qdma_remove,
};

module_platform_driver(fsl_qdma_driver);

MODULE_ALIAS("platform:fsl-qdma");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("NXP Layerscape qDMA engine driver");
