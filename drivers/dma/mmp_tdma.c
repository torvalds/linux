// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver For Marvell Two-channel DMA Engine
 *
 * Copyright: Marvell International Ltd.
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/dmaengine.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/platform_data/dma-mmp_tdma.h>
#include <linux/of_device.h>
#include <linux/of_dma.h>

#include "dmaengine.h"

/*
 * Two-Channel DMA registers
 */
#define TDBCR		0x00	/* Byte Count */
#define TDSAR		0x10	/* Src Addr */
#define TDDAR		0x20	/* Dst Addr */
#define TDNDPR		0x30	/* Next Desc */
#define TDCR		0x40	/* Control */
#define TDCP		0x60	/* Priority*/
#define TDCDPR		0x70	/* Current Desc */
#define TDIMR		0x80	/* Int Mask */
#define TDISR		0xa0	/* Int Status */

/* Two-Channel DMA Control Register */
#define TDCR_SSZ_8_BITS		(0x0 << 22)	/* Sample Size */
#define TDCR_SSZ_12_BITS	(0x1 << 22)
#define TDCR_SSZ_16_BITS	(0x2 << 22)
#define TDCR_SSZ_20_BITS	(0x3 << 22)
#define TDCR_SSZ_24_BITS	(0x4 << 22)
#define TDCR_SSZ_32_BITS	(0x5 << 22)
#define TDCR_SSZ_SHIFT		(0x1 << 22)
#define TDCR_SSZ_MASK		(0x7 << 22)
#define TDCR_SSPMOD		(0x1 << 21)	/* SSP MOD */
#define TDCR_ABR		(0x1 << 20)	/* Channel Abort */
#define TDCR_CDE		(0x1 << 17)	/* Close Desc Enable */
#define TDCR_PACKMOD		(0x1 << 16)	/* Pack Mode (ADMA Only) */
#define TDCR_CHANACT		(0x1 << 14)	/* Channel Active */
#define TDCR_FETCHND		(0x1 << 13)	/* Fetch Next Desc */
#define TDCR_CHANEN		(0x1 << 12)	/* Channel Enable */
#define TDCR_INTMODE		(0x1 << 10)	/* Interrupt Mode */
#define TDCR_CHAINMOD		(0x1 << 9)	/* Chain Mode */
#define TDCR_BURSTSZ_MSK	(0x7 << 6)	/* Burst Size */
#define TDCR_BURSTSZ_4B		(0x0 << 6)
#define TDCR_BURSTSZ_8B		(0x1 << 6)
#define TDCR_BURSTSZ_16B	(0x3 << 6)
#define TDCR_BURSTSZ_32B	(0x6 << 6)
#define TDCR_BURSTSZ_64B	(0x7 << 6)
#define TDCR_BURSTSZ_SQU_1B		(0x5 << 6)
#define TDCR_BURSTSZ_SQU_2B		(0x6 << 6)
#define TDCR_BURSTSZ_SQU_4B		(0x0 << 6)
#define TDCR_BURSTSZ_SQU_8B		(0x1 << 6)
#define TDCR_BURSTSZ_SQU_16B	(0x3 << 6)
#define TDCR_BURSTSZ_SQU_32B	(0x7 << 6)
#define TDCR_BURSTSZ_128B	(0x5 << 6)
#define TDCR_DSTDIR_MSK		(0x3 << 4)	/* Dst Direction */
#define TDCR_DSTDIR_ADDR_HOLD	(0x2 << 4)	/* Dst Addr Hold */
#define TDCR_DSTDIR_ADDR_INC	(0x0 << 4)	/* Dst Addr Increment */
#define TDCR_SRCDIR_MSK		(0x3 << 2)	/* Src Direction */
#define TDCR_SRCDIR_ADDR_HOLD	(0x2 << 2)	/* Src Addr Hold */
#define TDCR_SRCDIR_ADDR_INC	(0x0 << 2)	/* Src Addr Increment */
#define TDCR_DSTDESCCONT	(0x1 << 1)
#define TDCR_SRCDESTCONT	(0x1 << 0)

/* Two-Channel DMA Int Mask Register */
#define TDIMR_COMP		(0x1 << 0)

/* Two-Channel DMA Int Status Register */
#define TDISR_COMP		(0x1 << 0)

/*
 * Two-Channel DMA Descriptor Struct
 * NOTE: desc's buf must be aligned to 16 bytes.
 */
struct mmp_tdma_desc {
	u32 byte_cnt;
	u32 src_addr;
	u32 dst_addr;
	u32 nxt_desc;
};

enum mmp_tdma_type {
	MMP_AUD_TDMA = 0,
	PXA910_SQU,
};

#define TDMA_MAX_XFER_BYTES    SZ_64K

struct mmp_tdma_chan {
	struct device			*dev;
	struct dma_chan			chan;
	struct dma_async_tx_descriptor	desc;
	struct tasklet_struct		tasklet;

	struct mmp_tdma_desc		*desc_arr;
	dma_addr_t			desc_arr_phys;
	int				desc_num;
	enum dma_transfer_direction	dir;
	dma_addr_t			dev_addr;
	u32				burst_sz;
	enum dma_slave_buswidth		buswidth;
	enum dma_status			status;
	struct dma_slave_config		slave_config;

	int				idx;
	enum mmp_tdma_type		type;
	int				irq;
	void __iomem			*reg_base;

	size_t				buf_len;
	size_t				period_len;
	size_t				pos;

	struct gen_pool			*pool;
};

#define TDMA_CHANNEL_NUM 2
struct mmp_tdma_device {
	struct device			*dev;
	void __iomem			*base;
	struct dma_device		device;
	struct mmp_tdma_chan		*tdmac[TDMA_CHANNEL_NUM];
};

#define to_mmp_tdma_chan(dchan) container_of(dchan, struct mmp_tdma_chan, chan)

static int mmp_tdma_config_write(struct dma_chan *chan,
				 enum dma_transfer_direction dir,
				 struct dma_slave_config *dmaengine_cfg);

static void mmp_tdma_chan_set_desc(struct mmp_tdma_chan *tdmac, dma_addr_t phys)
{
	writel(phys, tdmac->reg_base + TDNDPR);
	writel(readl(tdmac->reg_base + TDCR) | TDCR_FETCHND,
					tdmac->reg_base + TDCR);
}

static void mmp_tdma_enable_irq(struct mmp_tdma_chan *tdmac, bool enable)
{
	if (enable)
		writel(TDIMR_COMP, tdmac->reg_base + TDIMR);
	else
		writel(0, tdmac->reg_base + TDIMR);
}

static void mmp_tdma_enable_chan(struct mmp_tdma_chan *tdmac)
{
	/* enable dma chan */
	writel(readl(tdmac->reg_base + TDCR) | TDCR_CHANEN,
					tdmac->reg_base + TDCR);
	tdmac->status = DMA_IN_PROGRESS;
}

static int mmp_tdma_disable_chan(struct dma_chan *chan)
{
	struct mmp_tdma_chan *tdmac = to_mmp_tdma_chan(chan);
	u32 tdcr;

	tdcr = readl(tdmac->reg_base + TDCR);
	tdcr |= TDCR_ABR;
	tdcr &= ~TDCR_CHANEN;
	writel(tdcr, tdmac->reg_base + TDCR);

	tdmac->status = DMA_COMPLETE;

	return 0;
}

static int mmp_tdma_resume_chan(struct dma_chan *chan)
{
	struct mmp_tdma_chan *tdmac = to_mmp_tdma_chan(chan);

	writel(readl(tdmac->reg_base + TDCR) | TDCR_CHANEN,
					tdmac->reg_base + TDCR);
	tdmac->status = DMA_IN_PROGRESS;

	return 0;
}

static int mmp_tdma_pause_chan(struct dma_chan *chan)
{
	struct mmp_tdma_chan *tdmac = to_mmp_tdma_chan(chan);

	writel(readl(tdmac->reg_base + TDCR) & ~TDCR_CHANEN,
					tdmac->reg_base + TDCR);
	tdmac->status = DMA_PAUSED;

	return 0;
}

static int mmp_tdma_config_chan(struct dma_chan *chan)
{
	struct mmp_tdma_chan *tdmac = to_mmp_tdma_chan(chan);
	unsigned int tdcr = 0;

	mmp_tdma_disable_chan(chan);

	if (tdmac->dir == DMA_MEM_TO_DEV)
		tdcr = TDCR_DSTDIR_ADDR_HOLD | TDCR_SRCDIR_ADDR_INC;
	else if (tdmac->dir == DMA_DEV_TO_MEM)
		tdcr = TDCR_SRCDIR_ADDR_HOLD | TDCR_DSTDIR_ADDR_INC;

	if (tdmac->type == MMP_AUD_TDMA) {
		tdcr |= TDCR_PACKMOD;

		switch (tdmac->burst_sz) {
		case 4:
			tdcr |= TDCR_BURSTSZ_4B;
			break;
		case 8:
			tdcr |= TDCR_BURSTSZ_8B;
			break;
		case 16:
			tdcr |= TDCR_BURSTSZ_16B;
			break;
		case 32:
			tdcr |= TDCR_BURSTSZ_32B;
			break;
		case 64:
			tdcr |= TDCR_BURSTSZ_64B;
			break;
		case 128:
			tdcr |= TDCR_BURSTSZ_128B;
			break;
		default:
			dev_err(tdmac->dev, "unknown burst size.\n");
			return -EINVAL;
		}

		switch (tdmac->buswidth) {
		case DMA_SLAVE_BUSWIDTH_1_BYTE:
			tdcr |= TDCR_SSZ_8_BITS;
			break;
		case DMA_SLAVE_BUSWIDTH_2_BYTES:
			tdcr |= TDCR_SSZ_16_BITS;
			break;
		case DMA_SLAVE_BUSWIDTH_4_BYTES:
			tdcr |= TDCR_SSZ_32_BITS;
			break;
		default:
			dev_err(tdmac->dev, "unknown bus size.\n");
			return -EINVAL;
		}
	} else if (tdmac->type == PXA910_SQU) {
		tdcr |= TDCR_SSPMOD;

		switch (tdmac->burst_sz) {
		case 1:
			tdcr |= TDCR_BURSTSZ_SQU_1B;
			break;
		case 2:
			tdcr |= TDCR_BURSTSZ_SQU_2B;
			break;
		case 4:
			tdcr |= TDCR_BURSTSZ_SQU_4B;
			break;
		case 8:
			tdcr |= TDCR_BURSTSZ_SQU_8B;
			break;
		case 16:
			tdcr |= TDCR_BURSTSZ_SQU_16B;
			break;
		case 32:
			tdcr |= TDCR_BURSTSZ_SQU_32B;
			break;
		default:
			dev_err(tdmac->dev, "unknown burst size.\n");
			return -EINVAL;
		}
	}

	writel(tdcr, tdmac->reg_base + TDCR);
	return 0;
}

static int mmp_tdma_clear_chan_irq(struct mmp_tdma_chan *tdmac)
{
	u32 reg = readl(tdmac->reg_base + TDISR);

	if (reg & TDISR_COMP) {
		/* clear irq */
		reg &= ~TDISR_COMP;
		writel(reg, tdmac->reg_base + TDISR);

		return 0;
	}
	return -EAGAIN;
}

static size_t mmp_tdma_get_pos(struct mmp_tdma_chan *tdmac)
{
	size_t reg;

	if (tdmac->idx == 0) {
		reg = __raw_readl(tdmac->reg_base + TDSAR);
		reg -= tdmac->desc_arr[0].src_addr;
	} else if (tdmac->idx == 1) {
		reg = __raw_readl(tdmac->reg_base + TDDAR);
		reg -= tdmac->desc_arr[0].dst_addr;
	} else
		return -EINVAL;

	return reg;
}

static irqreturn_t mmp_tdma_chan_handler(int irq, void *dev_id)
{
	struct mmp_tdma_chan *tdmac = dev_id;

	if (mmp_tdma_clear_chan_irq(tdmac) == 0) {
		tasklet_schedule(&tdmac->tasklet);
		return IRQ_HANDLED;
	} else
		return IRQ_NONE;
}

static irqreturn_t mmp_tdma_int_handler(int irq, void *dev_id)
{
	struct mmp_tdma_device *tdev = dev_id;
	int i, ret;
	int irq_num = 0;

	for (i = 0; i < TDMA_CHANNEL_NUM; i++) {
		struct mmp_tdma_chan *tdmac = tdev->tdmac[i];

		ret = mmp_tdma_chan_handler(irq, tdmac);
		if (ret == IRQ_HANDLED)
			irq_num++;
	}

	if (irq_num)
		return IRQ_HANDLED;
	else
		return IRQ_NONE;
}

static void dma_do_tasklet(struct tasklet_struct *t)
{
	struct mmp_tdma_chan *tdmac = from_tasklet(tdmac, t, tasklet);

	dmaengine_desc_get_callback_invoke(&tdmac->desc, NULL);
}

static void mmp_tdma_free_descriptor(struct mmp_tdma_chan *tdmac)
{
	struct gen_pool *gpool;
	int size = tdmac->desc_num * sizeof(struct mmp_tdma_desc);

	gpool = tdmac->pool;
	if (gpool && tdmac->desc_arr)
		gen_pool_free(gpool, (unsigned long)tdmac->desc_arr,
				size);
	tdmac->desc_arr = NULL;
	if (tdmac->status == DMA_ERROR)
		tdmac->status = DMA_COMPLETE;

	return;
}

static dma_cookie_t mmp_tdma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct mmp_tdma_chan *tdmac = to_mmp_tdma_chan(tx->chan);

	mmp_tdma_chan_set_desc(tdmac, tdmac->desc_arr_phys);

	return 0;
}

static int mmp_tdma_alloc_chan_resources(struct dma_chan *chan)
{
	struct mmp_tdma_chan *tdmac = to_mmp_tdma_chan(chan);
	int ret;

	dma_async_tx_descriptor_init(&tdmac->desc, chan);
	tdmac->desc.tx_submit = mmp_tdma_tx_submit;

	if (tdmac->irq) {
		ret = devm_request_irq(tdmac->dev, tdmac->irq,
			mmp_tdma_chan_handler, 0, "tdma", tdmac);
		if (ret)
			return ret;
	}
	return 1;
}

static void mmp_tdma_free_chan_resources(struct dma_chan *chan)
{
	struct mmp_tdma_chan *tdmac = to_mmp_tdma_chan(chan);

	if (tdmac->irq)
		devm_free_irq(tdmac->dev, tdmac->irq, tdmac);
	mmp_tdma_free_descriptor(tdmac);
	return;
}

static struct mmp_tdma_desc *mmp_tdma_alloc_descriptor(struct mmp_tdma_chan *tdmac)
{
	struct gen_pool *gpool;
	int size = tdmac->desc_num * sizeof(struct mmp_tdma_desc);

	gpool = tdmac->pool;
	if (!gpool)
		return NULL;

	tdmac->desc_arr = gen_pool_dma_alloc(gpool, size, &tdmac->desc_arr_phys);

	return tdmac->desc_arr;
}

static struct dma_async_tx_descriptor *mmp_tdma_prep_dma_cyclic(
		struct dma_chan *chan, dma_addr_t dma_addr, size_t buf_len,
		size_t period_len, enum dma_transfer_direction direction,
		unsigned long flags)
{
	struct mmp_tdma_chan *tdmac = to_mmp_tdma_chan(chan);
	struct mmp_tdma_desc *desc;
	int num_periods = buf_len / period_len;
	int i = 0, buf = 0;

	if (!is_slave_direction(direction)) {
		dev_err(tdmac->dev, "unsupported transfer direction\n");
		return NULL;
	}

	if (tdmac->status != DMA_COMPLETE) {
		dev_err(tdmac->dev, "controller busy");
		return NULL;
	}

	if (period_len > TDMA_MAX_XFER_BYTES) {
		dev_err(tdmac->dev,
				"maximum period size exceeded: %zu > %d\n",
				period_len, TDMA_MAX_XFER_BYTES);
		goto err_out;
	}

	tdmac->status = DMA_IN_PROGRESS;
	tdmac->desc_num = num_periods;
	desc = mmp_tdma_alloc_descriptor(tdmac);
	if (!desc)
		goto err_out;

	if (mmp_tdma_config_write(chan, direction, &tdmac->slave_config))
		goto err_out;

	while (buf < buf_len) {
		desc = &tdmac->desc_arr[i];

		if (i + 1 == num_periods)
			desc->nxt_desc = tdmac->desc_arr_phys;
		else
			desc->nxt_desc = tdmac->desc_arr_phys +
				sizeof(*desc) * (i + 1);

		if (direction == DMA_MEM_TO_DEV) {
			desc->src_addr = dma_addr;
			desc->dst_addr = tdmac->dev_addr;
		} else {
			desc->src_addr = tdmac->dev_addr;
			desc->dst_addr = dma_addr;
		}
		desc->byte_cnt = period_len;
		dma_addr += period_len;
		buf += period_len;
		i++;
	}

	/* enable interrupt */
	if (flags & DMA_PREP_INTERRUPT)
		mmp_tdma_enable_irq(tdmac, true);

	tdmac->buf_len = buf_len;
	tdmac->period_len = period_len;
	tdmac->pos = 0;

	return &tdmac->desc;

err_out:
	tdmac->status = DMA_ERROR;
	return NULL;
}

static int mmp_tdma_terminate_all(struct dma_chan *chan)
{
	struct mmp_tdma_chan *tdmac = to_mmp_tdma_chan(chan);

	mmp_tdma_disable_chan(chan);
	/* disable interrupt */
	mmp_tdma_enable_irq(tdmac, false);

	return 0;
}

static int mmp_tdma_config(struct dma_chan *chan,
			   struct dma_slave_config *dmaengine_cfg)
{
	struct mmp_tdma_chan *tdmac = to_mmp_tdma_chan(chan);

	memcpy(&tdmac->slave_config, dmaengine_cfg, sizeof(*dmaengine_cfg));

	return 0;
}

static int mmp_tdma_config_write(struct dma_chan *chan,
				 enum dma_transfer_direction dir,
				 struct dma_slave_config *dmaengine_cfg)
{
	struct mmp_tdma_chan *tdmac = to_mmp_tdma_chan(chan);

	if (dir == DMA_DEV_TO_MEM) {
		tdmac->dev_addr = dmaengine_cfg->src_addr;
		tdmac->burst_sz = dmaengine_cfg->src_maxburst;
		tdmac->buswidth = dmaengine_cfg->src_addr_width;
	} else {
		tdmac->dev_addr = dmaengine_cfg->dst_addr;
		tdmac->burst_sz = dmaengine_cfg->dst_maxburst;
		tdmac->buswidth = dmaengine_cfg->dst_addr_width;
	}
	tdmac->dir = dir;

	return mmp_tdma_config_chan(chan);
}

static enum dma_status mmp_tdma_tx_status(struct dma_chan *chan,
			dma_cookie_t cookie, struct dma_tx_state *txstate)
{
	struct mmp_tdma_chan *tdmac = to_mmp_tdma_chan(chan);

	tdmac->pos = mmp_tdma_get_pos(tdmac);
	dma_set_tx_state(txstate, chan->completed_cookie, chan->cookie,
			 tdmac->buf_len - tdmac->pos);

	return tdmac->status;
}

static void mmp_tdma_issue_pending(struct dma_chan *chan)
{
	struct mmp_tdma_chan *tdmac = to_mmp_tdma_chan(chan);

	mmp_tdma_enable_chan(tdmac);
}

static int mmp_tdma_remove(struct platform_device *pdev)
{
	if (pdev->dev.of_node)
		of_dma_controller_free(pdev->dev.of_node);

	return 0;
}

static int mmp_tdma_chan_init(struct mmp_tdma_device *tdev,
					int idx, int irq,
					int type, struct gen_pool *pool)
{
	struct mmp_tdma_chan *tdmac;

	if (idx >= TDMA_CHANNEL_NUM) {
		dev_err(tdev->dev, "too many channels for device!\n");
		return -EINVAL;
	}

	/* alloc channel */
	tdmac = devm_kzalloc(tdev->dev, sizeof(*tdmac), GFP_KERNEL);
	if (!tdmac)
		return -ENOMEM;

	if (irq)
		tdmac->irq = irq;
	tdmac->dev	   = tdev->dev;
	tdmac->chan.device = &tdev->device;
	tdmac->idx	   = idx;
	tdmac->type	   = type;
	tdmac->reg_base	   = tdev->base + idx * 4;
	tdmac->pool	   = pool;
	tdmac->status = DMA_COMPLETE;
	tdev->tdmac[tdmac->idx] = tdmac;
	tasklet_setup(&tdmac->tasklet, dma_do_tasklet);

	/* add the channel to tdma_chan list */
	list_add_tail(&tdmac->chan.device_node,
			&tdev->device.channels);
	return 0;
}

struct mmp_tdma_filter_param {
	unsigned int chan_id;
};

static bool mmp_tdma_filter_fn(struct dma_chan *chan, void *fn_param)
{
	struct mmp_tdma_filter_param *param = fn_param;

	if (chan->chan_id != param->chan_id)
		return false;

	return true;
}

static struct dma_chan *mmp_tdma_xlate(struct of_phandle_args *dma_spec,
			       struct of_dma *ofdma)
{
	struct mmp_tdma_device *tdev = ofdma->of_dma_data;
	dma_cap_mask_t mask = tdev->device.cap_mask;
	struct mmp_tdma_filter_param param;

	if (dma_spec->args_count != 1)
		return NULL;

	param.chan_id = dma_spec->args[0];

	if (param.chan_id >= TDMA_CHANNEL_NUM)
		return NULL;

	return __dma_request_channel(&mask, mmp_tdma_filter_fn, &param,
				     ofdma->of_node);
}

static const struct of_device_id mmp_tdma_dt_ids[] = {
	{ .compatible = "marvell,adma-1.0", .data = (void *)MMP_AUD_TDMA},
	{ .compatible = "marvell,pxa910-squ", .data = (void *)PXA910_SQU},
	{}
};
MODULE_DEVICE_TABLE(of, mmp_tdma_dt_ids);

static int mmp_tdma_probe(struct platform_device *pdev)
{
	enum mmp_tdma_type type;
	const struct of_device_id *of_id;
	struct mmp_tdma_device *tdev;
	struct resource *iores;
	int i, ret;
	int irq = 0, irq_num = 0;
	int chan_num = TDMA_CHANNEL_NUM;
	struct gen_pool *pool = NULL;

	of_id = of_match_device(mmp_tdma_dt_ids, &pdev->dev);
	if (of_id)
		type = (enum mmp_tdma_type) of_id->data;
	else
		type = platform_get_device_id(pdev)->driver_data;

	/* always have couple channels */
	tdev = devm_kzalloc(&pdev->dev, sizeof(*tdev), GFP_KERNEL);
	if (!tdev)
		return -ENOMEM;

	tdev->dev = &pdev->dev;

	for (i = 0; i < chan_num; i++) {
		if (platform_get_irq(pdev, i) > 0)
			irq_num++;
	}

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	tdev->base = devm_ioremap_resource(&pdev->dev, iores);
	if (IS_ERR(tdev->base))
		return PTR_ERR(tdev->base);

	INIT_LIST_HEAD(&tdev->device.channels);

	if (pdev->dev.of_node)
		pool = of_gen_pool_get(pdev->dev.of_node, "asram", 0);
	else
		pool = sram_get_gpool("asram");
	if (!pool) {
		dev_err(&pdev->dev, "asram pool not available\n");
		return -ENOMEM;
	}

	if (irq_num != chan_num) {
		irq = platform_get_irq(pdev, 0);
		ret = devm_request_irq(&pdev->dev, irq,
			mmp_tdma_int_handler, IRQF_SHARED, "tdma", tdev);
		if (ret)
			return ret;
	}

	/* initialize channel parameters */
	for (i = 0; i < chan_num; i++) {
		irq = (irq_num != chan_num) ? 0 : platform_get_irq(pdev, i);
		ret = mmp_tdma_chan_init(tdev, i, irq, type, pool);
		if (ret)
			return ret;
	}

	dma_cap_set(DMA_SLAVE, tdev->device.cap_mask);
	dma_cap_set(DMA_CYCLIC, tdev->device.cap_mask);
	tdev->device.dev = &pdev->dev;
	tdev->device.device_alloc_chan_resources =
					mmp_tdma_alloc_chan_resources;
	tdev->device.device_free_chan_resources =
					mmp_tdma_free_chan_resources;
	tdev->device.device_prep_dma_cyclic = mmp_tdma_prep_dma_cyclic;
	tdev->device.device_tx_status = mmp_tdma_tx_status;
	tdev->device.device_issue_pending = mmp_tdma_issue_pending;
	tdev->device.device_config = mmp_tdma_config;
	tdev->device.device_pause = mmp_tdma_pause_chan;
	tdev->device.device_resume = mmp_tdma_resume_chan;
	tdev->device.device_terminate_all = mmp_tdma_terminate_all;
	tdev->device.copy_align = DMAENGINE_ALIGN_8_BYTES;

	tdev->device.directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	if (type == MMP_AUD_TDMA) {
		tdev->device.max_burst = SZ_128;
		tdev->device.src_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
		tdev->device.dst_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	} else if (type == PXA910_SQU) {
		tdev->device.max_burst = SZ_32;
	}
	tdev->device.residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;
	tdev->device.descriptor_reuse = true;

	dma_set_mask(&pdev->dev, DMA_BIT_MASK(64));
	platform_set_drvdata(pdev, tdev);

	ret = dmaenginem_async_device_register(&tdev->device);
	if (ret) {
		dev_err(tdev->device.dev, "unable to register\n");
		return ret;
	}

	if (pdev->dev.of_node) {
		ret = of_dma_controller_register(pdev->dev.of_node,
							mmp_tdma_xlate, tdev);
		if (ret) {
			dev_err(tdev->device.dev,
				"failed to register controller\n");
			return ret;
		}
	}

	dev_info(tdev->device.dev, "initialized\n");
	return 0;
}

static const struct platform_device_id mmp_tdma_id_table[] = {
	{ "mmp-adma",	MMP_AUD_TDMA },
	{ "pxa910-squ",	PXA910_SQU },
	{ },
};

static struct platform_driver mmp_tdma_driver = {
	.driver		= {
		.name	= "mmp-tdma",
		.of_match_table = mmp_tdma_dt_ids,
	},
	.id_table	= mmp_tdma_id_table,
	.probe		= mmp_tdma_probe,
	.remove		= mmp_tdma_remove,
};

module_platform_driver(mmp_tdma_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MMP Two-Channel DMA Driver");
MODULE_ALIAS("platform:mmp-tdma");
MODULE_AUTHOR("Leo Yan <leoy@marvell.com>");
MODULE_AUTHOR("Zhangfei Gao <zhangfei.gao@marvell.com>");
