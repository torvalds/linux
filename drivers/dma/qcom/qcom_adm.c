// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2015, The Linux Foundation. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dma/qcom_adm.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

#include "../dmaengine.h"
#include "../virt-dma.h"

/* ADM registers - calculated from channel number and security domain */
#define ADM_CHAN_MULTI			0x4
#define ADM_CI_MULTI			0x4
#define ADM_CRCI_MULTI			0x4
#define ADM_EE_MULTI			0x800
#define ADM_CHAN_OFFS(chan)		(ADM_CHAN_MULTI * (chan))
#define ADM_EE_OFFS(ee)			(ADM_EE_MULTI * (ee))
#define ADM_CHAN_EE_OFFS(chan, ee)	(ADM_CHAN_OFFS(chan) + ADM_EE_OFFS(ee))
#define ADM_CHAN_OFFS(chan)		(ADM_CHAN_MULTI * (chan))
#define ADM_CI_OFFS(ci)			(ADM_CHAN_OFF(ci))
#define ADM_CH_CMD_PTR(chan, ee)	(ADM_CHAN_EE_OFFS(chan, ee))
#define ADM_CH_RSLT(chan, ee)		(0x40 + ADM_CHAN_EE_OFFS(chan, ee))
#define ADM_CH_FLUSH_STATE0(chan, ee)	(0x80 + ADM_CHAN_EE_OFFS(chan, ee))
#define ADM_CH_STATUS_SD(chan, ee)	(0x200 + ADM_CHAN_EE_OFFS(chan, ee))
#define ADM_CH_CONF(chan)		(0x240 + ADM_CHAN_OFFS(chan))
#define ADM_CH_RSLT_CONF(chan, ee)	(0x300 + ADM_CHAN_EE_OFFS(chan, ee))
#define ADM_SEC_DOMAIN_IRQ_STATUS(ee)	(0x380 + ADM_EE_OFFS(ee))
#define ADM_CI_CONF(ci)			(0x390 + (ci) * ADM_CI_MULTI)
#define ADM_GP_CTL			0x3d8
#define ADM_CRCI_CTL(crci, ee)		(0x400 + (crci) * ADM_CRCI_MULTI + \
						ADM_EE_OFFS(ee))

/* channel status */
#define ADM_CH_STATUS_VALID		BIT(1)

/* channel result */
#define ADM_CH_RSLT_VALID		BIT(31)
#define ADM_CH_RSLT_ERR			BIT(3)
#define ADM_CH_RSLT_FLUSH		BIT(2)
#define ADM_CH_RSLT_TPD			BIT(1)

/* channel conf */
#define ADM_CH_CONF_SHADOW_EN		BIT(12)
#define ADM_CH_CONF_MPU_DISABLE		BIT(11)
#define ADM_CH_CONF_PERM_MPU_CONF	BIT(9)
#define ADM_CH_CONF_FORCE_RSLT_EN	BIT(7)
#define ADM_CH_CONF_SEC_DOMAIN(ee)	((((ee) & 0x3) << 4) | (((ee) & 0x4) << 11))

/* channel result conf */
#define ADM_CH_RSLT_CONF_FLUSH_EN	BIT(1)
#define ADM_CH_RSLT_CONF_IRQ_EN		BIT(0)

/* CRCI CTL */
#define ADM_CRCI_CTL_MUX_SEL		BIT(18)
#define ADM_CRCI_CTL_RST		BIT(17)

/* CI configuration */
#define ADM_CI_RANGE_END(x)		((x) << 24)
#define ADM_CI_RANGE_START(x)		((x) << 16)
#define ADM_CI_BURST_4_WORDS		BIT(2)
#define ADM_CI_BURST_8_WORDS		BIT(3)

/* GP CTL */
#define ADM_GP_CTL_LP_EN		BIT(12)
#define ADM_GP_CTL_LP_CNT(x)		((x) << 8)

/* Command pointer list entry */
#define ADM_CPLE_LP			BIT(31)
#define ADM_CPLE_CMD_PTR_LIST		BIT(29)

/* Command list entry */
#define ADM_CMD_LC			BIT(31)
#define ADM_CMD_DST_CRCI(n)		(((n) & 0xf) << 7)
#define ADM_CMD_SRC_CRCI(n)		(((n) & 0xf) << 3)

#define ADM_CMD_TYPE_SINGLE		0x0
#define ADM_CMD_TYPE_BOX		0x3

#define ADM_CRCI_MUX_SEL		BIT(4)
#define ADM_DESC_ALIGN			8
#define ADM_MAX_XFER			(SZ_64K - 1)
#define ADM_MAX_ROWS			(SZ_64K - 1)
#define ADM_MAX_CHANNELS		16

struct adm_desc_hw_box {
	u32 cmd;
	u32 src_addr;
	u32 dst_addr;
	u32 row_len;
	u32 num_rows;
	u32 row_offset;
};

struct adm_desc_hw_single {
	u32 cmd;
	u32 src_addr;
	u32 dst_addr;
	u32 len;
};

struct adm_async_desc {
	struct virt_dma_desc vd;
	struct adm_device *adev;

	size_t length;
	enum dma_transfer_direction dir;
	dma_addr_t dma_addr;
	size_t dma_len;

	void *cpl;
	dma_addr_t cp_addr;
	u32 crci;
	u32 mux;
	u32 blk_size;
};

struct adm_chan {
	struct virt_dma_chan vc;
	struct adm_device *adev;

	/* parsed from DT */
	u32 id;			/* channel id */

	struct adm_async_desc *curr_txd;
	struct dma_slave_config slave;
	u32 crci;
	u32 mux;
	struct list_head node;

	int error;
	int initialized;
};

static inline struct adm_chan *to_adm_chan(struct dma_chan *common)
{
	return container_of(common, struct adm_chan, vc.chan);
}

struct adm_device {
	void __iomem *regs;
	struct device *dev;
	struct dma_device common;
	struct device_dma_parameters dma_parms;
	struct adm_chan *channels;

	u32 ee;

	struct clk *core_clk;
	struct clk *iface_clk;

	struct reset_control *clk_reset;
	struct reset_control *c0_reset;
	struct reset_control *c1_reset;
	struct reset_control *c2_reset;
	int irq;
};

/**
 * adm_free_chan - Frees dma resources associated with the specific channel
 *
 * @chan: dma channel
 *
 * Free all allocated descriptors associated with this channel
 */
static void adm_free_chan(struct dma_chan *chan)
{
	/* free all queued descriptors */
	vchan_free_chan_resources(to_virt_chan(chan));
}

/**
 * adm_get_blksize - Get block size from burst value
 *
 * @burst: Burst size of transaction
 */
static int adm_get_blksize(unsigned int burst)
{
	int ret;

	switch (burst) {
	case 16:
	case 32:
	case 64:
	case 128:
		ret = ffs(burst >> 4) - 1;
		break;
	case 192:
		ret = 4;
		break;
	case 256:
		ret = 5;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

/**
 * adm_process_fc_descriptors - Process descriptors for flow controlled xfers
 *
 * @achan: ADM channel
 * @desc: Descriptor memory pointer
 * @sg: Scatterlist entry
 * @crci: CRCI value
 * @burst: Burst size of transaction
 * @direction: DMA transfer direction
 */
static void *adm_process_fc_descriptors(struct adm_chan *achan, void *desc,
					struct scatterlist *sg, u32 crci,
					u32 burst,
					enum dma_transfer_direction direction)
{
	struct adm_desc_hw_box *box_desc = NULL;
	struct adm_desc_hw_single *single_desc;
	u32 remainder = sg_dma_len(sg);
	u32 rows, row_offset, crci_cmd;
	u32 mem_addr = sg_dma_address(sg);
	u32 *incr_addr = &mem_addr;
	u32 *src, *dst;

	if (direction == DMA_DEV_TO_MEM) {
		crci_cmd = ADM_CMD_SRC_CRCI(crci);
		row_offset = burst;
		src = &achan->slave.src_addr;
		dst = &mem_addr;
	} else {
		crci_cmd = ADM_CMD_DST_CRCI(crci);
		row_offset = burst << 16;
		src = &mem_addr;
		dst = &achan->slave.dst_addr;
	}

	while (remainder >= burst) {
		box_desc = desc;
		box_desc->cmd = ADM_CMD_TYPE_BOX | crci_cmd;
		box_desc->row_offset = row_offset;
		box_desc->src_addr = *src;
		box_desc->dst_addr = *dst;

		rows = remainder / burst;
		rows = min_t(u32, rows, ADM_MAX_ROWS);
		box_desc->num_rows = rows << 16 | rows;
		box_desc->row_len = burst << 16 | burst;

		*incr_addr += burst * rows;
		remainder -= burst * rows;
		desc += sizeof(*box_desc);
	}

	/* if leftover bytes, do one single descriptor */
	if (remainder) {
		single_desc = desc;
		single_desc->cmd = ADM_CMD_TYPE_SINGLE | crci_cmd;
		single_desc->len = remainder;
		single_desc->src_addr = *src;
		single_desc->dst_addr = *dst;
		desc += sizeof(*single_desc);

		if (sg_is_last(sg))
			single_desc->cmd |= ADM_CMD_LC;
	} else {
		if (box_desc && sg_is_last(sg))
			box_desc->cmd |= ADM_CMD_LC;
	}

	return desc;
}

/**
 * adm_process_non_fc_descriptors - Process descriptors for non-fc xfers
 *
 * @achan: ADM channel
 * @desc: Descriptor memory pointer
 * @sg: Scatterlist entry
 * @direction: DMA transfer direction
 */
static void *adm_process_non_fc_descriptors(struct adm_chan *achan, void *desc,
					    struct scatterlist *sg,
					    enum dma_transfer_direction direction)
{
	struct adm_desc_hw_single *single_desc;
	u32 remainder = sg_dma_len(sg);
	u32 mem_addr = sg_dma_address(sg);
	u32 *incr_addr = &mem_addr;
	u32 *src, *dst;

	if (direction == DMA_DEV_TO_MEM) {
		src = &achan->slave.src_addr;
		dst = &mem_addr;
	} else {
		src = &mem_addr;
		dst = &achan->slave.dst_addr;
	}

	do {
		single_desc = desc;
		single_desc->cmd = ADM_CMD_TYPE_SINGLE;
		single_desc->src_addr = *src;
		single_desc->dst_addr = *dst;
		single_desc->len = (remainder > ADM_MAX_XFER) ?
				ADM_MAX_XFER : remainder;

		remainder -= single_desc->len;
		*incr_addr += single_desc->len;
		desc += sizeof(*single_desc);
	} while (remainder);

	/* set last command if this is the end of the whole transaction */
	if (sg_is_last(sg))
		single_desc->cmd |= ADM_CMD_LC;

	return desc;
}

/**
 * adm_prep_slave_sg - Prep slave sg transaction
 *
 * @chan: dma channel
 * @sgl: scatter gather list
 * @sg_len: length of sg
 * @direction: DMA transfer direction
 * @flags: DMA flags
 * @context: transfer context (unused)
 */
static struct dma_async_tx_descriptor *adm_prep_slave_sg(struct dma_chan *chan,
							 struct scatterlist *sgl,
							 unsigned int sg_len,
							 enum dma_transfer_direction direction,
							 unsigned long flags,
							 void *context)
{
	struct adm_chan *achan = to_adm_chan(chan);
	struct adm_device *adev = achan->adev;
	struct adm_async_desc *async_desc;
	struct scatterlist *sg;
	dma_addr_t cple_addr;
	u32 i, burst;
	u32 single_count = 0, box_count = 0, crci = 0;
	void *desc;
	u32 *cple;
	int blk_size = 0;

	if (!is_slave_direction(direction)) {
		dev_err(adev->dev, "invalid dma direction\n");
		return NULL;
	}

	/*
	 * get burst value from slave configuration
	 */
	burst = (direction == DMA_MEM_TO_DEV) ?
		achan->slave.dst_maxburst :
		achan->slave.src_maxburst;

	/* if using flow control, validate burst and crci values */
	if (achan->slave.device_fc) {
		blk_size = adm_get_blksize(burst);
		if (blk_size < 0) {
			dev_err(adev->dev, "invalid burst value: %d\n",
				burst);
			return ERR_PTR(-EINVAL);
		}

		crci = achan->crci & 0xf;
		if (!crci || achan->crci > 0x1f) {
			dev_err(adev->dev, "invalid crci value\n");
			return ERR_PTR(-EINVAL);
		}
	}

	/* iterate through sgs and compute allocation size of structures */
	for_each_sg(sgl, sg, sg_len, i) {
		if (achan->slave.device_fc) {
			box_count += DIV_ROUND_UP(sg_dma_len(sg) / burst,
						  ADM_MAX_ROWS);
			if (sg_dma_len(sg) % burst)
				single_count++;
		} else {
			single_count += DIV_ROUND_UP(sg_dma_len(sg),
						     ADM_MAX_XFER);
		}
	}

	async_desc = kzalloc(sizeof(*async_desc), GFP_NOWAIT);
	if (!async_desc)
		return ERR_PTR(-ENOMEM);

	async_desc->mux = achan->mux ? ADM_CRCI_CTL_MUX_SEL : 0;
	async_desc->crci = crci;
	async_desc->blk_size = blk_size;
	async_desc->dma_len = single_count * sizeof(struct adm_desc_hw_single) +
				box_count * sizeof(struct adm_desc_hw_box) +
				sizeof(*cple) + 2 * ADM_DESC_ALIGN;

	async_desc->cpl = kzalloc(async_desc->dma_len, GFP_NOWAIT);
	if (!async_desc->cpl)
		goto free;

	async_desc->adev = adev;

	/* both command list entry and descriptors must be 8 byte aligned */
	cple = PTR_ALIGN(async_desc->cpl, ADM_DESC_ALIGN);
	desc = PTR_ALIGN(cple + 1, ADM_DESC_ALIGN);

	for_each_sg(sgl, sg, sg_len, i) {
		async_desc->length += sg_dma_len(sg);

		if (achan->slave.device_fc)
			desc = adm_process_fc_descriptors(achan, desc, sg, crci,
							  burst, direction);
		else
			desc = adm_process_non_fc_descriptors(achan, desc, sg,
							      direction);
	}

	async_desc->dma_addr = dma_map_single(adev->dev, async_desc->cpl,
					      async_desc->dma_len,
					      DMA_TO_DEVICE);
	if (dma_mapping_error(adev->dev, async_desc->dma_addr))
		goto free;

	cple_addr = async_desc->dma_addr + ((void *)cple - async_desc->cpl);

	/* init cmd list */
	dma_sync_single_for_cpu(adev->dev, cple_addr, sizeof(*cple),
				DMA_TO_DEVICE);
	*cple = ADM_CPLE_LP;
	*cple |= (async_desc->dma_addr + ADM_DESC_ALIGN) >> 3;
	dma_sync_single_for_device(adev->dev, cple_addr, sizeof(*cple),
				   DMA_TO_DEVICE);

	return vchan_tx_prep(&achan->vc, &async_desc->vd, flags);

free:
	kfree(async_desc);
	return ERR_PTR(-ENOMEM);
}

/**
 * adm_terminate_all - terminate all transactions on a channel
 * @chan: dma channel
 *
 * Dequeues and frees all transactions, aborts current transaction
 * No callbacks are done
 *
 */
static int adm_terminate_all(struct dma_chan *chan)
{
	struct adm_chan *achan = to_adm_chan(chan);
	struct adm_device *adev = achan->adev;
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&achan->vc.lock, flags);
	vchan_get_all_descriptors(&achan->vc, &head);

	/* send flush command to terminate current transaction */
	writel_relaxed(0x0,
		       adev->regs + ADM_CH_FLUSH_STATE0(achan->id, adev->ee));

	spin_unlock_irqrestore(&achan->vc.lock, flags);

	vchan_dma_desc_free_list(&achan->vc, &head);

	return 0;
}

static int adm_slave_config(struct dma_chan *chan, struct dma_slave_config *cfg)
{
	struct adm_chan *achan = to_adm_chan(chan);
	struct qcom_adm_peripheral_config *config = cfg->peripheral_config;
	unsigned long flag;

	spin_lock_irqsave(&achan->vc.lock, flag);
	memcpy(&achan->slave, cfg, sizeof(struct dma_slave_config));
	if (cfg->peripheral_size == sizeof(config))
		achan->crci = config->crci;
	spin_unlock_irqrestore(&achan->vc.lock, flag);

	return 0;
}

/**
 * adm_start_dma - start next transaction
 * @achan: ADM dma channel
 */
static void adm_start_dma(struct adm_chan *achan)
{
	struct virt_dma_desc *vd = vchan_next_desc(&achan->vc);
	struct adm_device *adev = achan->adev;
	struct adm_async_desc *async_desc;

	lockdep_assert_held(&achan->vc.lock);

	if (!vd)
		return;

	list_del(&vd->node);

	/* write next command list out to the CMD FIFO */
	async_desc = container_of(vd, struct adm_async_desc, vd);
	achan->curr_txd = async_desc;

	/* reset channel error */
	achan->error = 0;

	if (!achan->initialized) {
		/* enable interrupts */
		writel(ADM_CH_CONF_SHADOW_EN |
		       ADM_CH_CONF_PERM_MPU_CONF |
		       ADM_CH_CONF_MPU_DISABLE |
		       ADM_CH_CONF_SEC_DOMAIN(adev->ee),
		       adev->regs + ADM_CH_CONF(achan->id));

		writel(ADM_CH_RSLT_CONF_IRQ_EN | ADM_CH_RSLT_CONF_FLUSH_EN,
		       adev->regs + ADM_CH_RSLT_CONF(achan->id, adev->ee));

		achan->initialized = 1;
	}

	/* set the crci block size if this transaction requires CRCI */
	if (async_desc->crci) {
		writel(async_desc->mux | async_desc->blk_size,
		       adev->regs + ADM_CRCI_CTL(async_desc->crci, adev->ee));
	}

	/* make sure IRQ enable doesn't get reordered */
	wmb();

	/* write next command list out to the CMD FIFO */
	writel(ALIGN(async_desc->dma_addr, ADM_DESC_ALIGN) >> 3,
	       adev->regs + ADM_CH_CMD_PTR(achan->id, adev->ee));
}

/**
 * adm_dma_irq - irq handler for ADM controller
 * @irq: IRQ of interrupt
 * @data: callback data
 *
 * IRQ handler for the bam controller
 */
static irqreturn_t adm_dma_irq(int irq, void *data)
{
	struct adm_device *adev = data;
	u32 srcs, i;
	struct adm_async_desc *async_desc;
	unsigned long flags;

	srcs = readl_relaxed(adev->regs +
			ADM_SEC_DOMAIN_IRQ_STATUS(adev->ee));

	for (i = 0; i < ADM_MAX_CHANNELS; i++) {
		struct adm_chan *achan = &adev->channels[i];
		u32 status, result;

		if (srcs & BIT(i)) {
			status = readl_relaxed(adev->regs +
					       ADM_CH_STATUS_SD(i, adev->ee));

			/* if no result present, skip */
			if (!(status & ADM_CH_STATUS_VALID))
				continue;

			result = readl_relaxed(adev->regs +
				ADM_CH_RSLT(i, adev->ee));

			/* no valid results, skip */
			if (!(result & ADM_CH_RSLT_VALID))
				continue;

			/* flag error if transaction was flushed or failed */
			if (result & (ADM_CH_RSLT_ERR | ADM_CH_RSLT_FLUSH))
				achan->error = 1;

			spin_lock_irqsave(&achan->vc.lock, flags);
			async_desc = achan->curr_txd;

			achan->curr_txd = NULL;

			if (async_desc) {
				vchan_cookie_complete(&async_desc->vd);

				/* kick off next DMA */
				adm_start_dma(achan);
			}

			spin_unlock_irqrestore(&achan->vc.lock, flags);
		}
	}

	return IRQ_HANDLED;
}

/**
 * adm_tx_status - returns status of transaction
 * @chan: dma channel
 * @cookie: transaction cookie
 * @txstate: DMA transaction state
 *
 * Return status of dma transaction
 */
static enum dma_status adm_tx_status(struct dma_chan *chan, dma_cookie_t cookie,
				     struct dma_tx_state *txstate)
{
	struct adm_chan *achan = to_adm_chan(chan);
	struct virt_dma_desc *vd;
	enum dma_status ret;
	unsigned long flags;
	size_t residue = 0;

	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret == DMA_COMPLETE || !txstate)
		return ret;

	spin_lock_irqsave(&achan->vc.lock, flags);

	vd = vchan_find_desc(&achan->vc, cookie);
	if (vd)
		residue = container_of(vd, struct adm_async_desc, vd)->length;

	spin_unlock_irqrestore(&achan->vc.lock, flags);

	/*
	 * residue is either the full length if it is in the issued list, or 0
	 * if it is in progress.  We have no reliable way of determining
	 * anything inbetween
	 */
	dma_set_residue(txstate, residue);

	if (achan->error)
		return DMA_ERROR;

	return ret;
}

/**
 * adm_issue_pending - starts pending transactions
 * @chan: dma channel
 *
 * Issues all pending transactions and starts DMA
 */
static void adm_issue_pending(struct dma_chan *chan)
{
	struct adm_chan *achan = to_adm_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&achan->vc.lock, flags);

	if (vchan_issue_pending(&achan->vc) && !achan->curr_txd)
		adm_start_dma(achan);
	spin_unlock_irqrestore(&achan->vc.lock, flags);
}

/**
 * adm_dma_free_desc - free descriptor memory
 * @vd: virtual descriptor
 *
 */
static void adm_dma_free_desc(struct virt_dma_desc *vd)
{
	struct adm_async_desc *async_desc = container_of(vd,
			struct adm_async_desc, vd);

	dma_unmap_single(async_desc->adev->dev, async_desc->dma_addr,
			 async_desc->dma_len, DMA_TO_DEVICE);
	kfree(async_desc->cpl);
	kfree(async_desc);
}

static void adm_channel_init(struct adm_device *adev, struct adm_chan *achan,
			     u32 index)
{
	achan->id = index;
	achan->adev = adev;

	vchan_init(&achan->vc, &adev->common);
	achan->vc.desc_free = adm_dma_free_desc;
}

/**
 * adm_dma_xlate
 * @dma_spec:	pointer to DMA specifier as found in the device tree
 * @ofdma:	pointer to DMA controller data
 *
 * This can use either 1-cell or 2-cell formats, the first cell
 * identifies the slave device, while the optional second cell
 * contains the crci value.
 *
 * Returns pointer to appropriate dma channel on success or NULL on error.
 */
static struct dma_chan *adm_dma_xlate(struct of_phandle_args *dma_spec,
			       struct of_dma *ofdma)
{
	struct dma_device *dev = ofdma->of_dma_data;
	struct dma_chan *chan, *candidate = NULL;
	struct adm_chan *achan;

	if (!dev || dma_spec->args_count > 2)
		return NULL;

	list_for_each_entry(chan, &dev->channels, device_node)
		if (chan->chan_id == dma_spec->args[0]) {
			candidate = chan;
			break;
		}

	if (!candidate)
		return NULL;

	achan = to_adm_chan(candidate);
	if (dma_spec->args_count == 2)
		achan->crci = dma_spec->args[1];
	else
		achan->crci = 0;

	return dma_get_slave_channel(candidate);
}

static int adm_dma_probe(struct platform_device *pdev)
{
	struct adm_device *adev;
	int ret;
	u32 i;

	adev = devm_kzalloc(&pdev->dev, sizeof(*adev), GFP_KERNEL);
	if (!adev)
		return -ENOMEM;

	adev->dev = &pdev->dev;

	adev->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(adev->regs))
		return PTR_ERR(adev->regs);

	adev->irq = platform_get_irq(pdev, 0);
	if (adev->irq < 0)
		return adev->irq;

	ret = of_property_read_u32(pdev->dev.of_node, "qcom,ee", &adev->ee);
	if (ret) {
		dev_err(adev->dev, "Execution environment unspecified\n");
		return ret;
	}

	adev->core_clk = devm_clk_get(adev->dev, "core");
	if (IS_ERR(adev->core_clk))
		return PTR_ERR(adev->core_clk);

	adev->iface_clk = devm_clk_get(adev->dev, "iface");
	if (IS_ERR(adev->iface_clk))
		return PTR_ERR(adev->iface_clk);

	adev->clk_reset = devm_reset_control_get_exclusive(&pdev->dev, "clk");
	if (IS_ERR(adev->clk_reset)) {
		dev_err(adev->dev, "failed to get ADM0 reset\n");
		return PTR_ERR(adev->clk_reset);
	}

	adev->c0_reset = devm_reset_control_get_exclusive(&pdev->dev, "c0");
	if (IS_ERR(adev->c0_reset)) {
		dev_err(adev->dev, "failed to get ADM0 C0 reset\n");
		return PTR_ERR(adev->c0_reset);
	}

	adev->c1_reset = devm_reset_control_get_exclusive(&pdev->dev, "c1");
	if (IS_ERR(adev->c1_reset)) {
		dev_err(adev->dev, "failed to get ADM0 C1 reset\n");
		return PTR_ERR(adev->c1_reset);
	}

	adev->c2_reset = devm_reset_control_get_exclusive(&pdev->dev, "c2");
	if (IS_ERR(adev->c2_reset)) {
		dev_err(adev->dev, "failed to get ADM0 C2 reset\n");
		return PTR_ERR(adev->c2_reset);
	}

	ret = clk_prepare_enable(adev->core_clk);
	if (ret) {
		dev_err(adev->dev, "failed to prepare/enable core clock\n");
		return ret;
	}

	ret = clk_prepare_enable(adev->iface_clk);
	if (ret) {
		dev_err(adev->dev, "failed to prepare/enable iface clock\n");
		goto err_disable_core_clk;
	}

	reset_control_assert(adev->clk_reset);
	reset_control_assert(adev->c0_reset);
	reset_control_assert(adev->c1_reset);
	reset_control_assert(adev->c2_reset);

	udelay(2);

	reset_control_deassert(adev->clk_reset);
	reset_control_deassert(adev->c0_reset);
	reset_control_deassert(adev->c1_reset);
	reset_control_deassert(adev->c2_reset);

	adev->channels = devm_kcalloc(adev->dev, ADM_MAX_CHANNELS,
				      sizeof(*adev->channels), GFP_KERNEL);

	if (!adev->channels) {
		ret = -ENOMEM;
		goto err_disable_clks;
	}

	/* allocate and initialize channels */
	INIT_LIST_HEAD(&adev->common.channels);

	for (i = 0; i < ADM_MAX_CHANNELS; i++)
		adm_channel_init(adev, &adev->channels[i], i);

	/* reset CRCIs */
	for (i = 0; i < 16; i++)
		writel(ADM_CRCI_CTL_RST, adev->regs +
			ADM_CRCI_CTL(i, adev->ee));

	/* configure client interfaces */
	writel(ADM_CI_RANGE_START(0x40) | ADM_CI_RANGE_END(0xb0) |
	       ADM_CI_BURST_8_WORDS, adev->regs + ADM_CI_CONF(0));
	writel(ADM_CI_RANGE_START(0x2a) | ADM_CI_RANGE_END(0x2c) |
	       ADM_CI_BURST_8_WORDS, adev->regs + ADM_CI_CONF(1));
	writel(ADM_CI_RANGE_START(0x12) | ADM_CI_RANGE_END(0x28) |
	       ADM_CI_BURST_8_WORDS, adev->regs + ADM_CI_CONF(2));
	writel(ADM_GP_CTL_LP_EN | ADM_GP_CTL_LP_CNT(0xf),
	       adev->regs + ADM_GP_CTL);

	ret = devm_request_irq(adev->dev, adev->irq, adm_dma_irq,
			       0, "adm_dma", adev);
	if (ret)
		goto err_disable_clks;

	platform_set_drvdata(pdev, adev);

	adev->common.dev = adev->dev;
	adev->common.dev->dma_parms = &adev->dma_parms;

	/* set capabilities */
	dma_cap_zero(adev->common.cap_mask);
	dma_cap_set(DMA_SLAVE, adev->common.cap_mask);
	dma_cap_set(DMA_PRIVATE, adev->common.cap_mask);

	/* initialize dmaengine apis */
	adev->common.directions = BIT(DMA_DEV_TO_MEM | DMA_MEM_TO_DEV);
	adev->common.residue_granularity = DMA_RESIDUE_GRANULARITY_DESCRIPTOR;
	adev->common.src_addr_widths = DMA_SLAVE_BUSWIDTH_4_BYTES;
	adev->common.dst_addr_widths = DMA_SLAVE_BUSWIDTH_4_BYTES;
	adev->common.device_free_chan_resources = adm_free_chan;
	adev->common.device_prep_slave_sg = adm_prep_slave_sg;
	adev->common.device_issue_pending = adm_issue_pending;
	adev->common.device_tx_status = adm_tx_status;
	adev->common.device_terminate_all = adm_terminate_all;
	adev->common.device_config = adm_slave_config;

	ret = dma_async_device_register(&adev->common);
	if (ret) {
		dev_err(adev->dev, "failed to register dma async device\n");
		goto err_disable_clks;
	}

	ret = of_dma_controller_register(pdev->dev.of_node, adm_dma_xlate,
					 &adev->common);
	if (ret)
		goto err_unregister_dma;

	return 0;

err_unregister_dma:
	dma_async_device_unregister(&adev->common);
err_disable_clks:
	clk_disable_unprepare(adev->iface_clk);
err_disable_core_clk:
	clk_disable_unprepare(adev->core_clk);

	return ret;
}

static int adm_dma_remove(struct platform_device *pdev)
{
	struct adm_device *adev = platform_get_drvdata(pdev);
	struct adm_chan *achan;
	u32 i;

	of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&adev->common);

	for (i = 0; i < ADM_MAX_CHANNELS; i++) {
		achan = &adev->channels[i];

		/* mask IRQs for this channel/EE pair */
		writel(0, adev->regs + ADM_CH_RSLT_CONF(achan->id, adev->ee));

		tasklet_kill(&adev->channels[i].vc.task);
		adm_terminate_all(&adev->channels[i].vc.chan);
	}

	devm_free_irq(adev->dev, adev->irq, adev);

	clk_disable_unprepare(adev->core_clk);
	clk_disable_unprepare(adev->iface_clk);

	return 0;
}

static const struct of_device_id adm_of_match[] = {
	{ .compatible = "qcom,adm", },
	{}
};
MODULE_DEVICE_TABLE(of, adm_of_match);

static struct platform_driver adm_dma_driver = {
	.probe = adm_dma_probe,
	.remove = adm_dma_remove,
	.driver = {
		.name = "adm-dma-engine",
		.of_match_table = adm_of_match,
	},
};

module_platform_driver(adm_dma_driver);

MODULE_AUTHOR("Andy Gross <agross@codeaurora.org>");
MODULE_DESCRIPTION("QCOM ADM DMA engine driver");
MODULE_LICENSE("GPL v2");
