/*
 * DMA controller driver for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/module.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/sirfsoc_dma.h>

#define SIRFSOC_DMA_DESCRIPTORS                 16
#define SIRFSOC_DMA_CHANNELS                    16

#define SIRFSOC_DMA_CH_ADDR                     0x00
#define SIRFSOC_DMA_CH_XLEN                     0x04
#define SIRFSOC_DMA_CH_YLEN                     0x08
#define SIRFSOC_DMA_CH_CTRL                     0x0C

#define SIRFSOC_DMA_WIDTH_0                     0x100
#define SIRFSOC_DMA_CH_VALID                    0x140
#define SIRFSOC_DMA_CH_INT                      0x144
#define SIRFSOC_DMA_INT_EN                      0x148
#define SIRFSOC_DMA_CH_LOOP_CTRL                0x150

#define SIRFSOC_DMA_MODE_CTRL_BIT               4
#define SIRFSOC_DMA_DIR_CTRL_BIT                5

/* xlen and dma_width register is in 4 bytes boundary */
#define SIRFSOC_DMA_WORD_LEN			4

struct sirfsoc_dma_desc {
	struct dma_async_tx_descriptor	desc;
	struct list_head		node;

	/* SiRFprimaII 2D-DMA parameters */

	int             xlen;           /* DMA xlen */
	int             ylen;           /* DMA ylen */
	int             width;          /* DMA width */
	int             dir;
	bool            cyclic;         /* is loop DMA? */
	u32             addr;		/* DMA buffer address */
};

struct sirfsoc_dma_chan {
	struct dma_chan			chan;
	struct list_head		free;
	struct list_head		prepared;
	struct list_head		queued;
	struct list_head		active;
	struct list_head		completed;
	dma_cookie_t			completed_cookie;
	unsigned long			happened_cyclic;
	unsigned long			completed_cyclic;

	/* Lock for this structure */
	spinlock_t			lock;

	int				mode;
};

struct sirfsoc_dma {
	struct dma_device		dma;
	struct tasklet_struct		tasklet;
	struct sirfsoc_dma_chan		channels[SIRFSOC_DMA_CHANNELS];
	void __iomem			*base;
	int				irq;
};

#define DRV_NAME	"sirfsoc_dma"

/* Convert struct dma_chan to struct sirfsoc_dma_chan */
static inline
struct sirfsoc_dma_chan *dma_chan_to_sirfsoc_dma_chan(struct dma_chan *c)
{
	return container_of(c, struct sirfsoc_dma_chan, chan);
}

/* Convert struct dma_chan to struct sirfsoc_dma */
static inline struct sirfsoc_dma *dma_chan_to_sirfsoc_dma(struct dma_chan *c)
{
	struct sirfsoc_dma_chan *schan = dma_chan_to_sirfsoc_dma_chan(c);
	return container_of(schan, struct sirfsoc_dma, channels[c->chan_id]);
}

/* Execute all queued DMA descriptors */
static void sirfsoc_dma_execute(struct sirfsoc_dma_chan *schan)
{
	struct sirfsoc_dma *sdma = dma_chan_to_sirfsoc_dma(&schan->chan);
	int cid = schan->chan.chan_id;
	struct sirfsoc_dma_desc *sdesc = NULL;

	/*
	 * lock has been held by functions calling this, so we don't hold
	 * lock again
	 */

	sdesc = list_first_entry(&schan->queued, struct sirfsoc_dma_desc,
		node);
	/* Move the first queued descriptor to active list */
	list_move_tail(&schan->queued, &schan->active);

	/* Start the DMA transfer */
	writel_relaxed(sdesc->width, sdma->base + SIRFSOC_DMA_WIDTH_0 +
		cid * 4);
	writel_relaxed(cid | (schan->mode << SIRFSOC_DMA_MODE_CTRL_BIT) |
		(sdesc->dir << SIRFSOC_DMA_DIR_CTRL_BIT),
		sdma->base + cid * 0x10 + SIRFSOC_DMA_CH_CTRL);
	writel_relaxed(sdesc->xlen, sdma->base + cid * 0x10 +
		SIRFSOC_DMA_CH_XLEN);
	writel_relaxed(sdesc->ylen, sdma->base + cid * 0x10 +
		SIRFSOC_DMA_CH_YLEN);
	writel_relaxed(readl_relaxed(sdma->base + SIRFSOC_DMA_INT_EN) |
		(1 << cid), sdma->base + SIRFSOC_DMA_INT_EN);

	/*
	 * writel has an implict memory write barrier to make sure data is
	 * flushed into memory before starting DMA
	 */
	writel(sdesc->addr >> 2, sdma->base + cid * 0x10 + SIRFSOC_DMA_CH_ADDR);

	if (sdesc->cyclic) {
		writel((1 << cid) | 1 << (cid + 16) |
			readl_relaxed(sdma->base + SIRFSOC_DMA_CH_LOOP_CTRL),
			sdma->base + SIRFSOC_DMA_CH_LOOP_CTRL);
		schan->happened_cyclic = schan->completed_cyclic = 0;
	}
}

/* Interrupt handler */
static irqreturn_t sirfsoc_dma_irq(int irq, void *data)
{
	struct sirfsoc_dma *sdma = data;
	struct sirfsoc_dma_chan *schan;
	struct sirfsoc_dma_desc *sdesc = NULL;
	u32 is;
	int ch;

	is = readl(sdma->base + SIRFSOC_DMA_CH_INT);
	while ((ch = fls(is) - 1) >= 0) {
		is &= ~(1 << ch);
		writel_relaxed(1 << ch, sdma->base + SIRFSOC_DMA_CH_INT);
		schan = &sdma->channels[ch];

		spin_lock(&schan->lock);

		sdesc = list_first_entry(&schan->active, struct sirfsoc_dma_desc,
			node);
		if (!sdesc->cyclic) {
			/* Execute queued descriptors */
			list_splice_tail_init(&schan->active, &schan->completed);
			if (!list_empty(&schan->queued))
				sirfsoc_dma_execute(schan);
		} else
			schan->happened_cyclic++;

		spin_unlock(&schan->lock);
	}

	/* Schedule tasklet */
	tasklet_schedule(&sdma->tasklet);

	return IRQ_HANDLED;
}

/* process completed descriptors */
static void sirfsoc_dma_process_completed(struct sirfsoc_dma *sdma)
{
	dma_cookie_t last_cookie = 0;
	struct sirfsoc_dma_chan *schan;
	struct sirfsoc_dma_desc *sdesc;
	struct dma_async_tx_descriptor *desc;
	unsigned long flags;
	unsigned long happened_cyclic;
	LIST_HEAD(list);
	int i;

	for (i = 0; i < sdma->dma.chancnt; i++) {
		schan = &sdma->channels[i];

		/* Get all completed descriptors */
		spin_lock_irqsave(&schan->lock, flags);
		if (!list_empty(&schan->completed)) {
			list_splice_tail_init(&schan->completed, &list);
			spin_unlock_irqrestore(&schan->lock, flags);

			/* Execute callbacks and run dependencies */
			list_for_each_entry(sdesc, &list, node) {
				desc = &sdesc->desc;

				if (desc->callback)
					desc->callback(desc->callback_param);

				last_cookie = desc->cookie;
				dma_run_dependencies(desc);
			}

			/* Free descriptors */
			spin_lock_irqsave(&schan->lock, flags);
			list_splice_tail_init(&list, &schan->free);
			schan->completed_cookie = last_cookie;
			spin_unlock_irqrestore(&schan->lock, flags);
		} else {
			/* for cyclic channel, desc is always in active list */
			sdesc = list_first_entry(&schan->active, struct sirfsoc_dma_desc,
				node);

			if (!sdesc || (sdesc && !sdesc->cyclic)) {
				/* without active cyclic DMA */
				spin_unlock_irqrestore(&schan->lock, flags);
				continue;
			}

			/* cyclic DMA */
			happened_cyclic = schan->happened_cyclic;
			spin_unlock_irqrestore(&schan->lock, flags);

			desc = &sdesc->desc;
			while (happened_cyclic != schan->completed_cyclic) {
				if (desc->callback)
					desc->callback(desc->callback_param);
				schan->completed_cyclic++;
			}
		}
	}
}

/* DMA Tasklet */
static void sirfsoc_dma_tasklet(unsigned long data)
{
	struct sirfsoc_dma *sdma = (void *)data;

	sirfsoc_dma_process_completed(sdma);
}

/* Submit descriptor to hardware */
static dma_cookie_t sirfsoc_dma_tx_submit(struct dma_async_tx_descriptor *txd)
{
	struct sirfsoc_dma_chan *schan = dma_chan_to_sirfsoc_dma_chan(txd->chan);
	struct sirfsoc_dma_desc *sdesc;
	unsigned long flags;
	dma_cookie_t cookie;

	sdesc = container_of(txd, struct sirfsoc_dma_desc, desc);

	spin_lock_irqsave(&schan->lock, flags);

	/* Move descriptor to queue */
	list_move_tail(&sdesc->node, &schan->queued);

	/* Update cookie */
	cookie = schan->chan.cookie + 1;
	if (cookie <= 0)
		cookie = 1;

	schan->chan.cookie = cookie;
	sdesc->desc.cookie = cookie;

	spin_unlock_irqrestore(&schan->lock, flags);

	return cookie;
}

static int sirfsoc_dma_slave_config(struct sirfsoc_dma_chan *schan,
	struct dma_slave_config *config)
{
	unsigned long flags;

	if ((config->src_addr_width != DMA_SLAVE_BUSWIDTH_4_BYTES) ||
		(config->dst_addr_width != DMA_SLAVE_BUSWIDTH_4_BYTES))
		return -EINVAL;

	spin_lock_irqsave(&schan->lock, flags);
	schan->mode = (config->src_maxburst == 4 ? 1 : 0);
	spin_unlock_irqrestore(&schan->lock, flags);

	return 0;
}

static int sirfsoc_dma_terminate_all(struct sirfsoc_dma_chan *schan)
{
	struct sirfsoc_dma *sdma = dma_chan_to_sirfsoc_dma(&schan->chan);
	int cid = schan->chan.chan_id;
	unsigned long flags;

	writel_relaxed(readl_relaxed(sdma->base + SIRFSOC_DMA_INT_EN) &
		~(1 << cid), sdma->base + SIRFSOC_DMA_INT_EN);
	writel_relaxed(1 << cid, sdma->base + SIRFSOC_DMA_CH_VALID);

	writel_relaxed(readl_relaxed(sdma->base + SIRFSOC_DMA_CH_LOOP_CTRL)
		& ~((1 << cid) | 1 << (cid + 16)),
			sdma->base + SIRFSOC_DMA_CH_LOOP_CTRL);

	spin_lock_irqsave(&schan->lock, flags);
	list_splice_tail_init(&schan->active, &schan->free);
	list_splice_tail_init(&schan->queued, &schan->free);
	spin_unlock_irqrestore(&schan->lock, flags);

	return 0;
}

static int sirfsoc_dma_control(struct dma_chan *chan, enum dma_ctrl_cmd cmd,
	unsigned long arg)
{
	struct dma_slave_config *config;
	struct sirfsoc_dma_chan *schan = dma_chan_to_sirfsoc_dma_chan(chan);

	switch (cmd) {
	case DMA_TERMINATE_ALL:
		return sirfsoc_dma_terminate_all(schan);
	case DMA_SLAVE_CONFIG:
		config = (struct dma_slave_config *)arg;
		return sirfsoc_dma_slave_config(schan, config);

	default:
		break;
	}

	return -ENOSYS;
}

/* Alloc channel resources */
static int sirfsoc_dma_alloc_chan_resources(struct dma_chan *chan)
{
	struct sirfsoc_dma *sdma = dma_chan_to_sirfsoc_dma(chan);
	struct sirfsoc_dma_chan *schan = dma_chan_to_sirfsoc_dma_chan(chan);
	struct sirfsoc_dma_desc *sdesc;
	unsigned long flags;
	LIST_HEAD(descs);
	int i;

	/* Alloc descriptors for this channel */
	for (i = 0; i < SIRFSOC_DMA_DESCRIPTORS; i++) {
		sdesc = kzalloc(sizeof(*sdesc), GFP_KERNEL);
		if (!sdesc) {
			dev_notice(sdma->dma.dev, "Memory allocation error. "
				"Allocated only %u descriptors\n", i);
			break;
		}

		dma_async_tx_descriptor_init(&sdesc->desc, chan);
		sdesc->desc.flags = DMA_CTRL_ACK;
		sdesc->desc.tx_submit = sirfsoc_dma_tx_submit;

		list_add_tail(&sdesc->node, &descs);
	}

	/* Return error only if no descriptors were allocated */
	if (i == 0)
		return -ENOMEM;

	spin_lock_irqsave(&schan->lock, flags);

	list_splice_tail_init(&descs, &schan->free);
	spin_unlock_irqrestore(&schan->lock, flags);

	return i;
}

/* Free channel resources */
static void sirfsoc_dma_free_chan_resources(struct dma_chan *chan)
{
	struct sirfsoc_dma_chan *schan = dma_chan_to_sirfsoc_dma_chan(chan);
	struct sirfsoc_dma_desc *sdesc, *tmp;
	unsigned long flags;
	LIST_HEAD(descs);

	spin_lock_irqsave(&schan->lock, flags);

	/* Channel must be idle */
	BUG_ON(!list_empty(&schan->prepared));
	BUG_ON(!list_empty(&schan->queued));
	BUG_ON(!list_empty(&schan->active));
	BUG_ON(!list_empty(&schan->completed));

	/* Move data */
	list_splice_tail_init(&schan->free, &descs);

	spin_unlock_irqrestore(&schan->lock, flags);

	/* Free descriptors */
	list_for_each_entry_safe(sdesc, tmp, &descs, node)
		kfree(sdesc);
}

/* Send pending descriptor to hardware */
static void sirfsoc_dma_issue_pending(struct dma_chan *chan)
{
	struct sirfsoc_dma_chan *schan = dma_chan_to_sirfsoc_dma_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&schan->lock, flags);

	if (list_empty(&schan->active) && !list_empty(&schan->queued))
		sirfsoc_dma_execute(schan);

	spin_unlock_irqrestore(&schan->lock, flags);
}

/* Check request completion status */
static enum dma_status
sirfsoc_dma_tx_status(struct dma_chan *chan, dma_cookie_t cookie,
	struct dma_tx_state *txstate)
{
	struct sirfsoc_dma_chan *schan = dma_chan_to_sirfsoc_dma_chan(chan);
	unsigned long flags;
	dma_cookie_t last_used;
	dma_cookie_t last_complete;

	spin_lock_irqsave(&schan->lock, flags);
	last_used = schan->chan.cookie;
	last_complete = schan->completed_cookie;
	spin_unlock_irqrestore(&schan->lock, flags);

	dma_set_tx_state(txstate, last_complete, last_used, 0);
	return dma_async_is_complete(cookie, last_complete, last_used);
}

static struct dma_async_tx_descriptor *sirfsoc_dma_prep_interleaved(
	struct dma_chan *chan, struct dma_interleaved_template *xt,
	unsigned long flags)
{
	struct sirfsoc_dma *sdma = dma_chan_to_sirfsoc_dma(chan);
	struct sirfsoc_dma_chan *schan = dma_chan_to_sirfsoc_dma_chan(chan);
	struct sirfsoc_dma_desc *sdesc = NULL;
	unsigned long iflags;
	int ret;

	if ((xt->dir != DMA_MEM_TO_DEV) || (xt->dir != DMA_DEV_TO_MEM)) {
		ret = -EINVAL;
		goto err_dir;
	}

	/* Get free descriptor */
	spin_lock_irqsave(&schan->lock, iflags);
	if (!list_empty(&schan->free)) {
		sdesc = list_first_entry(&schan->free, struct sirfsoc_dma_desc,
			node);
		list_del(&sdesc->node);
	}
	spin_unlock_irqrestore(&schan->lock, iflags);

	if (!sdesc) {
		/* try to free completed descriptors */
		sirfsoc_dma_process_completed(sdma);
		ret = 0;
		goto no_desc;
	}

	/* Place descriptor in prepared list */
	spin_lock_irqsave(&schan->lock, iflags);

	/*
	 * Number of chunks in a frame can only be 1 for prima2
	 * and ylen (number of frame - 1) must be at least 0
	 */
	if ((xt->frame_size == 1) && (xt->numf > 0)) {
		sdesc->cyclic = 0;
		sdesc->xlen = xt->sgl[0].size / SIRFSOC_DMA_WORD_LEN;
		sdesc->width = (xt->sgl[0].size + xt->sgl[0].icg) /
				SIRFSOC_DMA_WORD_LEN;
		sdesc->ylen = xt->numf - 1;
		if (xt->dir == DMA_MEM_TO_DEV) {
			sdesc->addr = xt->src_start;
			sdesc->dir = 1;
		} else {
			sdesc->addr = xt->dst_start;
			sdesc->dir = 0;
		}

		list_add_tail(&sdesc->node, &schan->prepared);
	} else {
		pr_err("sirfsoc DMA Invalid xfer\n");
		ret = -EINVAL;
		goto err_xfer;
	}
	spin_unlock_irqrestore(&schan->lock, iflags);

	return &sdesc->desc;
err_xfer:
	spin_unlock_irqrestore(&schan->lock, iflags);
no_desc:
err_dir:
	return ERR_PTR(ret);
}

static struct dma_async_tx_descriptor *
sirfsoc_dma_prep_cyclic(struct dma_chan *chan, dma_addr_t addr,
	size_t buf_len, size_t period_len,
	enum dma_transfer_direction direction)
{
	struct sirfsoc_dma_chan *schan = dma_chan_to_sirfsoc_dma_chan(chan);
	struct sirfsoc_dma_desc *sdesc = NULL;
	unsigned long iflags;

	/*
	 * we only support cycle transfer with 2 period
	 * If the X-length is set to 0, it would be the loop mode.
	 * The DMA address keeps increasing until reaching the end of a loop
	 * area whose size is defined by (DMA_WIDTH x (Y_LENGTH + 1)). Then
	 * the DMA address goes back to the beginning of this area.
	 * In loop mode, the DMA data region is divided into two parts, BUFA
	 * and BUFB. DMA controller generates interrupts twice in each loop:
	 * when the DMA address reaches the end of BUFA or the end of the
	 * BUFB
	 */
	if (buf_len !=  2 * period_len)
		return ERR_PTR(-EINVAL);

	/* Get free descriptor */
	spin_lock_irqsave(&schan->lock, iflags);
	if (!list_empty(&schan->free)) {
		sdesc = list_first_entry(&schan->free, struct sirfsoc_dma_desc,
			node);
		list_del(&sdesc->node);
	}
	spin_unlock_irqrestore(&schan->lock, iflags);

	if (!sdesc)
		return 0;

	/* Place descriptor in prepared list */
	spin_lock_irqsave(&schan->lock, iflags);
	sdesc->addr = addr;
	sdesc->cyclic = 1;
	sdesc->xlen = 0;
	sdesc->ylen = buf_len / SIRFSOC_DMA_WORD_LEN - 1;
	sdesc->width = 1;
	list_add_tail(&sdesc->node, &schan->prepared);
	spin_unlock_irqrestore(&schan->lock, iflags);

	return &sdesc->desc;
}

/*
 * The DMA controller consists of 16 independent DMA channels.
 * Each channel is allocated to a different function
 */
bool sirfsoc_dma_filter_id(struct dma_chan *chan, void *chan_id)
{
	unsigned int ch_nr = (unsigned int) chan_id;

	if (ch_nr == chan->chan_id +
		chan->device->dev_id * SIRFSOC_DMA_CHANNELS)
		return true;

	return false;
}
EXPORT_SYMBOL(sirfsoc_dma_filter_id);

static int __devinit sirfsoc_dma_probe(struct platform_device *op)
{
	struct device_node *dn = op->dev.of_node;
	struct device *dev = &op->dev;
	struct dma_device *dma;
	struct sirfsoc_dma *sdma;
	struct sirfsoc_dma_chan *schan;
	struct resource res;
	ulong regs_start, regs_size;
	u32 id;
	int ret, i;

	sdma = devm_kzalloc(dev, sizeof(*sdma), GFP_KERNEL);
	if (!sdma) {
		dev_err(dev, "Memory exhausted!\n");
		return -ENOMEM;
	}

	if (of_property_read_u32(dn, "cell-index", &id)) {
		dev_err(dev, "Fail to get DMAC index\n");
		ret = -ENODEV;
		goto free_mem;
	}

	sdma->irq = irq_of_parse_and_map(dn, 0);
	if (sdma->irq == NO_IRQ) {
		dev_err(dev, "Error mapping IRQ!\n");
		ret = -EINVAL;
		goto free_mem;
	}

	ret = of_address_to_resource(dn, 0, &res);
	if (ret) {
		dev_err(dev, "Error parsing memory region!\n");
		goto free_mem;
	}

	regs_start = res.start;
	regs_size = resource_size(&res);

	sdma->base = devm_ioremap(dev, regs_start, regs_size);
	if (!sdma->base) {
		dev_err(dev, "Error mapping memory region!\n");
		ret = -ENOMEM;
		goto irq_dispose;
	}

	ret = devm_request_irq(dev, sdma->irq, &sirfsoc_dma_irq, 0, DRV_NAME,
		sdma);
	if (ret) {
		dev_err(dev, "Error requesting IRQ!\n");
		ret = -EINVAL;
		goto unmap_mem;
	}

	dma = &sdma->dma;
	dma->dev = dev;
	dma->chancnt = SIRFSOC_DMA_CHANNELS;

	dma->device_alloc_chan_resources = sirfsoc_dma_alloc_chan_resources;
	dma->device_free_chan_resources = sirfsoc_dma_free_chan_resources;
	dma->device_issue_pending = sirfsoc_dma_issue_pending;
	dma->device_control = sirfsoc_dma_control;
	dma->device_tx_status = sirfsoc_dma_tx_status;
	dma->device_prep_interleaved_dma = sirfsoc_dma_prep_interleaved;
	dma->device_prep_dma_cyclic = sirfsoc_dma_prep_cyclic;

	INIT_LIST_HEAD(&dma->channels);
	dma_cap_set(DMA_SLAVE, dma->cap_mask);
	dma_cap_set(DMA_CYCLIC, dma->cap_mask);
	dma_cap_set(DMA_INTERLEAVE, dma->cap_mask);
	dma_cap_set(DMA_PRIVATE, dma->cap_mask);

	for (i = 0; i < dma->chancnt; i++) {
		schan = &sdma->channels[i];

		schan->chan.device = dma;
		schan->chan.cookie = 1;
		schan->completed_cookie = schan->chan.cookie;

		INIT_LIST_HEAD(&schan->free);
		INIT_LIST_HEAD(&schan->prepared);
		INIT_LIST_HEAD(&schan->queued);
		INIT_LIST_HEAD(&schan->active);
		INIT_LIST_HEAD(&schan->completed);

		spin_lock_init(&schan->lock);
		list_add_tail(&schan->chan.device_node, &dma->channels);
	}

	tasklet_init(&sdma->tasklet, sirfsoc_dma_tasklet, (unsigned long)sdma);

	/* Register DMA engine */
	dev_set_drvdata(dev, sdma);
	ret = dma_async_device_register(dma);
	if (ret)
		goto free_irq;

	dev_info(dev, "initialized SIRFSOC DMAC driver\n");

	return 0;

free_irq:
	devm_free_irq(dev, sdma->irq, sdma);
irq_dispose:
	irq_dispose_mapping(sdma->irq);
unmap_mem:
	iounmap(sdma->base);
free_mem:
	devm_kfree(dev, sdma);
	return ret;
}

static int __devexit sirfsoc_dma_remove(struct platform_device *op)
{
	struct device *dev = &op->dev;
	struct sirfsoc_dma *sdma = dev_get_drvdata(dev);

	dma_async_device_unregister(&sdma->dma);
	devm_free_irq(dev, sdma->irq, sdma);
	irq_dispose_mapping(sdma->irq);
	iounmap(sdma->base);
	devm_kfree(dev, sdma);
	return 0;
}

static struct of_device_id sirfsoc_dma_match[] = {
	{ .compatible = "sirf,prima2-dmac", },
	{},
};

static struct platform_driver sirfsoc_dma_driver = {
	.probe		= sirfsoc_dma_probe,
	.remove		= __devexit_p(sirfsoc_dma_remove),
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table	= sirfsoc_dma_match,
	},
};

module_platform_driver(sirfsoc_dma_driver);

MODULE_AUTHOR("Rongjun Ying <rongjun.ying@csr.com>, "
	"Barry Song <baohua.song@csr.com>");
MODULE_DESCRIPTION("SIRFSOC DMA control driver");
MODULE_LICENSE("GPL v2");
