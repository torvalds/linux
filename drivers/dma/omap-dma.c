/*
 * OMAP DMAengine support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/omap-dma.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "virt-dma.h"

#include <plat-omap/dma-omap.h>

#ifdef CONFIG_ARCH_OMAP2PLUS
#define dma_omap2plus()	1
#else
#define dma_omap2plus()	0
#endif

struct omap_dmadev {
	struct dma_device ddev;
	spinlock_t lock;
	struct tasklet_struct task;
	struct list_head pending;
};

struct omap_chan {
	struct virt_dma_chan vc;
	struct list_head node;

	struct dma_slave_config	cfg;
	unsigned dma_sig;
	bool cyclic;
	bool paused;

	int dma_ch;
	struct omap_desc *desc;
	unsigned sgidx;
};

struct omap_sg {
	dma_addr_t addr;
	uint32_t en;		/* number of elements (24-bit) */
	uint32_t fn;		/* number of frames (16-bit) */
};

struct omap_desc {
	struct virt_dma_desc vd;
	enum dma_transfer_direction dir;
	dma_addr_t dev_addr;

	int16_t fi;		/* for OMAP_DMA_SYNC_PACKET */
	uint8_t es;		/* OMAP_DMA_DATA_TYPE_xxx */
	uint8_t sync_mode;	/* OMAP_DMA_SYNC_xxx */
	uint8_t sync_type;	/* OMAP_DMA_xxx_SYNC* */
	uint8_t periph_port;	/* Peripheral port */

	unsigned sglen;
	struct omap_sg sg[0];
};

static const unsigned es_bytes[] = {
	[OMAP_DMA_DATA_TYPE_S8] = 1,
	[OMAP_DMA_DATA_TYPE_S16] = 2,
	[OMAP_DMA_DATA_TYPE_S32] = 4,
};

static inline struct omap_dmadev *to_omap_dma_dev(struct dma_device *d)
{
	return container_of(d, struct omap_dmadev, ddev);
}

static inline struct omap_chan *to_omap_dma_chan(struct dma_chan *c)
{
	return container_of(c, struct omap_chan, vc.chan);
}

static inline struct omap_desc *to_omap_dma_desc(struct dma_async_tx_descriptor *t)
{
	return container_of(t, struct omap_desc, vd.tx);
}

static void omap_dma_desc_free(struct virt_dma_desc *vd)
{
	kfree(container_of(vd, struct omap_desc, vd));
}

static void omap_dma_start_sg(struct omap_chan *c, struct omap_desc *d,
	unsigned idx)
{
	struct omap_sg *sg = d->sg + idx;

	if (d->dir == DMA_DEV_TO_MEM)
		omap_set_dma_dest_params(c->dma_ch, OMAP_DMA_PORT_EMIFF,
			OMAP_DMA_AMODE_POST_INC, sg->addr, 0, 0);
	else
		omap_set_dma_src_params(c->dma_ch, OMAP_DMA_PORT_EMIFF,
			OMAP_DMA_AMODE_POST_INC, sg->addr, 0, 0);

	omap_set_dma_transfer_params(c->dma_ch, d->es, sg->en, sg->fn,
		d->sync_mode, c->dma_sig, d->sync_type);

	omap_start_dma(c->dma_ch);
}

static void omap_dma_start_desc(struct omap_chan *c)
{
	struct virt_dma_desc *vd = vchan_next_desc(&c->vc);
	struct omap_desc *d;

	if (!vd) {
		c->desc = NULL;
		return;
	}

	list_del(&vd->node);

	c->desc = d = to_omap_dma_desc(&vd->tx);
	c->sgidx = 0;

	if (d->dir == DMA_DEV_TO_MEM)
		omap_set_dma_src_params(c->dma_ch, d->periph_port,
			OMAP_DMA_AMODE_CONSTANT, d->dev_addr, 0, d->fi);
	else
		omap_set_dma_dest_params(c->dma_ch, d->periph_port,
			OMAP_DMA_AMODE_CONSTANT, d->dev_addr, 0, d->fi);

	omap_dma_start_sg(c, d, 0);
}

static void omap_dma_callback(int ch, u16 status, void *data)
{
	struct omap_chan *c = data;
	struct omap_desc *d;
	unsigned long flags;

	spin_lock_irqsave(&c->vc.lock, flags);
	d = c->desc;
	if (d) {
		if (!c->cyclic) {
			if (++c->sgidx < d->sglen) {
				omap_dma_start_sg(c, d, c->sgidx);
			} else {
				omap_dma_start_desc(c);
				vchan_cookie_complete(&d->vd);
			}
		} else {
			vchan_cyclic_callback(&d->vd);
		}
	}
	spin_unlock_irqrestore(&c->vc.lock, flags);
}

/*
 * This callback schedules all pending channels.  We could be more
 * clever here by postponing allocation of the real DMA channels to
 * this point, and freeing them when our virtual channel becomes idle.
 *
 * We would then need to deal with 'all channels in-use'
 */
static void omap_dma_sched(unsigned long data)
{
	struct omap_dmadev *d = (struct omap_dmadev *)data;
	LIST_HEAD(head);

	spin_lock_irq(&d->lock);
	list_splice_tail_init(&d->pending, &head);
	spin_unlock_irq(&d->lock);

	while (!list_empty(&head)) {
		struct omap_chan *c = list_first_entry(&head,
			struct omap_chan, node);

		spin_lock_irq(&c->vc.lock);
		list_del_init(&c->node);
		omap_dma_start_desc(c);
		spin_unlock_irq(&c->vc.lock);
	}
}

static int omap_dma_alloc_chan_resources(struct dma_chan *chan)
{
	struct omap_chan *c = to_omap_dma_chan(chan);

	dev_info(c->vc.chan.device->dev, "allocating channel for %u\n", c->dma_sig);

	return omap_request_dma(c->dma_sig, "DMA engine",
		omap_dma_callback, c, &c->dma_ch);
}

static void omap_dma_free_chan_resources(struct dma_chan *chan)
{
	struct omap_chan *c = to_omap_dma_chan(chan);

	vchan_free_chan_resources(&c->vc);
	omap_free_dma(c->dma_ch);

	dev_info(c->vc.chan.device->dev, "freeing channel for %u\n", c->dma_sig);
}

static size_t omap_dma_sg_size(struct omap_sg *sg)
{
	return sg->en * sg->fn;
}

static size_t omap_dma_desc_size(struct omap_desc *d)
{
	unsigned i;
	size_t size;

	for (size = i = 0; i < d->sglen; i++)
		size += omap_dma_sg_size(&d->sg[i]);

	return size * es_bytes[d->es];
}

static size_t omap_dma_desc_size_pos(struct omap_desc *d, dma_addr_t addr)
{
	unsigned i;
	size_t size, es_size = es_bytes[d->es];

	for (size = i = 0; i < d->sglen; i++) {
		size_t this_size = omap_dma_sg_size(&d->sg[i]) * es_size;

		if (size)
			size += this_size;
		else if (addr >= d->sg[i].addr &&
			 addr < d->sg[i].addr + this_size)
			size += d->sg[i].addr + this_size - addr;
	}
	return size;
}

static enum dma_status omap_dma_tx_status(struct dma_chan *chan,
	dma_cookie_t cookie, struct dma_tx_state *txstate)
{
	struct omap_chan *c = to_omap_dma_chan(chan);
	struct virt_dma_desc *vd;
	enum dma_status ret;
	unsigned long flags;

	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret == DMA_SUCCESS || !txstate)
		return ret;

	spin_lock_irqsave(&c->vc.lock, flags);
	vd = vchan_find_desc(&c->vc, cookie);
	if (vd) {
		txstate->residue = omap_dma_desc_size(to_omap_dma_desc(&vd->tx));
	} else if (c->desc && c->desc->vd.tx.cookie == cookie) {
		struct omap_desc *d = c->desc;
		dma_addr_t pos;

		if (d->dir == DMA_MEM_TO_DEV)
			pos = omap_get_dma_src_pos(c->dma_ch);
		else if (d->dir == DMA_DEV_TO_MEM)
			pos = omap_get_dma_dst_pos(c->dma_ch);
		else
			pos = 0;

		txstate->residue = omap_dma_desc_size_pos(d, pos);
	} else {
		txstate->residue = 0;
	}
	spin_unlock_irqrestore(&c->vc.lock, flags);

	return ret;
}

static void omap_dma_issue_pending(struct dma_chan *chan)
{
	struct omap_chan *c = to_omap_dma_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&c->vc.lock, flags);
	if (vchan_issue_pending(&c->vc) && !c->desc) {
		struct omap_dmadev *d = to_omap_dma_dev(chan->device);
		spin_lock(&d->lock);
		if (list_empty(&c->node))
			list_add_tail(&c->node, &d->pending);
		spin_unlock(&d->lock);
		tasklet_schedule(&d->task);
	}
	spin_unlock_irqrestore(&c->vc.lock, flags);
}

static struct dma_async_tx_descriptor *omap_dma_prep_slave_sg(
	struct dma_chan *chan, struct scatterlist *sgl, unsigned sglen,
	enum dma_transfer_direction dir, unsigned long tx_flags, void *context)
{
	struct omap_chan *c = to_omap_dma_chan(chan);
	enum dma_slave_buswidth dev_width;
	struct scatterlist *sgent;
	struct omap_desc *d;
	dma_addr_t dev_addr;
	unsigned i, j = 0, es, en, frame_bytes, sync_type;
	u32 burst;

	if (dir == DMA_DEV_TO_MEM) {
		dev_addr = c->cfg.src_addr;
		dev_width = c->cfg.src_addr_width;
		burst = c->cfg.src_maxburst;
		sync_type = OMAP_DMA_SRC_SYNC;
	} else if (dir == DMA_MEM_TO_DEV) {
		dev_addr = c->cfg.dst_addr;
		dev_width = c->cfg.dst_addr_width;
		burst = c->cfg.dst_maxburst;
		sync_type = OMAP_DMA_DST_SYNC;
	} else {
		dev_err(chan->device->dev, "%s: bad direction?\n", __func__);
		return NULL;
	}

	/* Bus width translates to the element size (ES) */
	switch (dev_width) {
	case DMA_SLAVE_BUSWIDTH_1_BYTE:
		es = OMAP_DMA_DATA_TYPE_S8;
		break;
	case DMA_SLAVE_BUSWIDTH_2_BYTES:
		es = OMAP_DMA_DATA_TYPE_S16;
		break;
	case DMA_SLAVE_BUSWIDTH_4_BYTES:
		es = OMAP_DMA_DATA_TYPE_S32;
		break;
	default: /* not reached */
		return NULL;
	}

	/* Now allocate and setup the descriptor. */
	d = kzalloc(sizeof(*d) + sglen * sizeof(d->sg[0]), GFP_ATOMIC);
	if (!d)
		return NULL;

	d->dir = dir;
	d->dev_addr = dev_addr;
	d->es = es;
	d->sync_mode = OMAP_DMA_SYNC_FRAME;
	d->sync_type = sync_type;
	d->periph_port = OMAP_DMA_PORT_TIPB;

	/*
	 * Build our scatterlist entries: each contains the address,
	 * the number of elements (EN) in each frame, and the number of
	 * frames (FN).  Number of bytes for this entry = ES * EN * FN.
	 *
	 * Burst size translates to number of elements with frame sync.
	 * Note: DMA engine defines burst to be the number of dev-width
	 * transfers.
	 */
	en = burst;
	frame_bytes = es_bytes[es] * en;
	for_each_sg(sgl, sgent, sglen, i) {
		d->sg[j].addr = sg_dma_address(sgent);
		d->sg[j].en = en;
		d->sg[j].fn = sg_dma_len(sgent) / frame_bytes;
		j++;
	}

	d->sglen = j;

	return vchan_tx_prep(&c->vc, &d->vd, tx_flags);
}

static struct dma_async_tx_descriptor *omap_dma_prep_dma_cyclic(
	struct dma_chan *chan, dma_addr_t buf_addr, size_t buf_len,
	size_t period_len, enum dma_transfer_direction dir, unsigned long flags,
	void *context)
{
	struct omap_chan *c = to_omap_dma_chan(chan);
	enum dma_slave_buswidth dev_width;
	struct omap_desc *d;
	dma_addr_t dev_addr;
	unsigned es, sync_type;
	u32 burst;

	if (dir == DMA_DEV_TO_MEM) {
		dev_addr = c->cfg.src_addr;
		dev_width = c->cfg.src_addr_width;
		burst = c->cfg.src_maxburst;
		sync_type = OMAP_DMA_SRC_SYNC;
	} else if (dir == DMA_MEM_TO_DEV) {
		dev_addr = c->cfg.dst_addr;
		dev_width = c->cfg.dst_addr_width;
		burst = c->cfg.dst_maxburst;
		sync_type = OMAP_DMA_DST_SYNC;
	} else {
		dev_err(chan->device->dev, "%s: bad direction?\n", __func__);
		return NULL;
	}

	/* Bus width translates to the element size (ES) */
	switch (dev_width) {
	case DMA_SLAVE_BUSWIDTH_1_BYTE:
		es = OMAP_DMA_DATA_TYPE_S8;
		break;
	case DMA_SLAVE_BUSWIDTH_2_BYTES:
		es = OMAP_DMA_DATA_TYPE_S16;
		break;
	case DMA_SLAVE_BUSWIDTH_4_BYTES:
		es = OMAP_DMA_DATA_TYPE_S32;
		break;
	default: /* not reached */
		return NULL;
	}

	/* Now allocate and setup the descriptor. */
	d = kzalloc(sizeof(*d) + sizeof(d->sg[0]), GFP_ATOMIC);
	if (!d)
		return NULL;

	d->dir = dir;
	d->dev_addr = dev_addr;
	d->fi = burst;
	d->es = es;
	if (burst)
		d->sync_mode = OMAP_DMA_SYNC_PACKET;
	else
		d->sync_mode = OMAP_DMA_SYNC_ELEMENT;
	d->sync_type = sync_type;
	d->periph_port = OMAP_DMA_PORT_MPUI;
	d->sg[0].addr = buf_addr;
	d->sg[0].en = period_len / es_bytes[es];
	d->sg[0].fn = buf_len / period_len;
	d->sglen = 1;

	if (!c->cyclic) {
		c->cyclic = true;
		omap_dma_link_lch(c->dma_ch, c->dma_ch);

		if (flags & DMA_PREP_INTERRUPT)
			omap_enable_dma_irq(c->dma_ch, OMAP_DMA_FRAME_IRQ);

		omap_disable_dma_irq(c->dma_ch, OMAP_DMA_BLOCK_IRQ);
	}

	if (dma_omap2plus()) {
		omap_set_dma_src_burst_mode(c->dma_ch, OMAP_DMA_DATA_BURST_16);
		omap_set_dma_dest_burst_mode(c->dma_ch, OMAP_DMA_DATA_BURST_16);
	}

	return vchan_tx_prep(&c->vc, &d->vd, flags);
}

static int omap_dma_slave_config(struct omap_chan *c, struct dma_slave_config *cfg)
{
	if (cfg->src_addr_width == DMA_SLAVE_BUSWIDTH_8_BYTES ||
	    cfg->dst_addr_width == DMA_SLAVE_BUSWIDTH_8_BYTES)
		return -EINVAL;

	memcpy(&c->cfg, cfg, sizeof(c->cfg));

	return 0;
}

static int omap_dma_terminate_all(struct omap_chan *c)
{
	struct omap_dmadev *d = to_omap_dma_dev(c->vc.chan.device);
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&c->vc.lock, flags);

	/* Prevent this channel being scheduled */
	spin_lock(&d->lock);
	list_del_init(&c->node);
	spin_unlock(&d->lock);

	/*
	 * Stop DMA activity: we assume the callback will not be called
	 * after omap_stop_dma() returns (even if it does, it will see
	 * c->desc is NULL and exit.)
	 */
	if (c->desc) {
		c->desc = NULL;
		/* Avoid stopping the dma twice */
		if (!c->paused)
			omap_stop_dma(c->dma_ch);
	}

	if (c->cyclic) {
		c->cyclic = false;
		c->paused = false;
		omap_dma_unlink_lch(c->dma_ch, c->dma_ch);
	}

	vchan_get_all_descriptors(&c->vc, &head);
	spin_unlock_irqrestore(&c->vc.lock, flags);
	vchan_dma_desc_free_list(&c->vc, &head);

	return 0;
}

static int omap_dma_pause(struct omap_chan *c)
{
	/* Pause/Resume only allowed with cyclic mode */
	if (!c->cyclic)
		return -EINVAL;

	if (!c->paused) {
		omap_stop_dma(c->dma_ch);
		c->paused = true;
	}

	return 0;
}

static int omap_dma_resume(struct omap_chan *c)
{
	/* Pause/Resume only allowed with cyclic mode */
	if (!c->cyclic)
		return -EINVAL;

	if (c->paused) {
		omap_start_dma(c->dma_ch);
		c->paused = false;
	}

	return 0;
}

static int omap_dma_control(struct dma_chan *chan, enum dma_ctrl_cmd cmd,
	unsigned long arg)
{
	struct omap_chan *c = to_omap_dma_chan(chan);
	int ret;

	switch (cmd) {
	case DMA_SLAVE_CONFIG:
		ret = omap_dma_slave_config(c, (struct dma_slave_config *)arg);
		break;

	case DMA_TERMINATE_ALL:
		ret = omap_dma_terminate_all(c);
		break;

	case DMA_PAUSE:
		ret = omap_dma_pause(c);
		break;

	case DMA_RESUME:
		ret = omap_dma_resume(c);
		break;

	default:
		ret = -ENXIO;
		break;
	}

	return ret;
}

static int omap_dma_chan_init(struct omap_dmadev *od, int dma_sig)
{
	struct omap_chan *c;

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c)
		return -ENOMEM;

	c->dma_sig = dma_sig;
	c->vc.desc_free = omap_dma_desc_free;
	vchan_init(&c->vc, &od->ddev);
	INIT_LIST_HEAD(&c->node);

	od->ddev.chancnt++;

	return 0;
}

static void omap_dma_free(struct omap_dmadev *od)
{
	tasklet_kill(&od->task);
	while (!list_empty(&od->ddev.channels)) {
		struct omap_chan *c = list_first_entry(&od->ddev.channels,
			struct omap_chan, vc.chan.device_node);

		list_del(&c->vc.chan.device_node);
		tasklet_kill(&c->vc.task);
		kfree(c);
	}
	kfree(od);
}

static int omap_dma_probe(struct platform_device *pdev)
{
	struct omap_dmadev *od;
	int rc, i;

	od = kzalloc(sizeof(*od), GFP_KERNEL);
	if (!od)
		return -ENOMEM;

	dma_cap_set(DMA_SLAVE, od->ddev.cap_mask);
	dma_cap_set(DMA_CYCLIC, od->ddev.cap_mask);
	od->ddev.device_alloc_chan_resources = omap_dma_alloc_chan_resources;
	od->ddev.device_free_chan_resources = omap_dma_free_chan_resources;
	od->ddev.device_tx_status = omap_dma_tx_status;
	od->ddev.device_issue_pending = omap_dma_issue_pending;
	od->ddev.device_prep_slave_sg = omap_dma_prep_slave_sg;
	od->ddev.device_prep_dma_cyclic = omap_dma_prep_dma_cyclic;
	od->ddev.device_control = omap_dma_control;
	od->ddev.dev = &pdev->dev;
	INIT_LIST_HEAD(&od->ddev.channels);
	INIT_LIST_HEAD(&od->pending);
	spin_lock_init(&od->lock);

	tasklet_init(&od->task, omap_dma_sched, (unsigned long)od);

	for (i = 0; i < 127; i++) {
		rc = omap_dma_chan_init(od, i);
		if (rc) {
			omap_dma_free(od);
			return rc;
		}
	}

	rc = dma_async_device_register(&od->ddev);
	if (rc) {
		pr_warn("OMAP-DMA: failed to register slave DMA engine device: %d\n",
			rc);
		omap_dma_free(od);
	} else {
		platform_set_drvdata(pdev, od);
	}

	dev_info(&pdev->dev, "OMAP DMA engine driver\n");

	return rc;
}

static int omap_dma_remove(struct platform_device *pdev)
{
	struct omap_dmadev *od = platform_get_drvdata(pdev);

	dma_async_device_unregister(&od->ddev);
	omap_dma_free(od);

	return 0;
}

static struct platform_driver omap_dma_driver = {
	.probe	= omap_dma_probe,
	.remove	= omap_dma_remove,
	.driver = {
		.name = "omap-dma-engine",
		.owner = THIS_MODULE,
	},
};

bool omap_dma_filter_fn(struct dma_chan *chan, void *param)
{
	if (chan->device->dev->driver == &omap_dma_driver.driver) {
		struct omap_chan *c = to_omap_dma_chan(chan);
		unsigned req = *(unsigned *)param;

		return req == c->dma_sig;
	}
	return false;
}
EXPORT_SYMBOL_GPL(omap_dma_filter_fn);

static struct platform_device *pdev;

static const struct platform_device_info omap_dma_dev_info = {
	.name = "omap-dma-engine",
	.id = -1,
	.dma_mask = DMA_BIT_MASK(32),
};

static int omap_dma_init(void)
{
	int rc = platform_driver_register(&omap_dma_driver);

	if (rc == 0) {
		pdev = platform_device_register_full(&omap_dma_dev_info);
		if (IS_ERR(pdev)) {
			platform_driver_unregister(&omap_dma_driver);
			rc = PTR_ERR(pdev);
		}
	}
	return rc;
}
subsys_initcall(omap_dma_init);

static void __exit omap_dma_exit(void)
{
	platform_device_unregister(pdev);
	platform_driver_unregister(&omap_dma_driver);
}
module_exit(omap_dma_exit);

MODULE_AUTHOR("Russell King");
MODULE_LICENSE("GPL");
