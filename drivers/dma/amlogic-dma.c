// SPDX-License-Identifier: (GPL-2.0-only OR MIT)
/*
 * Copyright (C) 2026 Amlogic, Inc. All rights reserved
 * Author: Xianwei Zhao <xianwei.zhao@amlogic.com>
 */

#include <dt-bindings/dma/amlogic,a9-dma.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "virt-dma.h"

#define RCH_REG_BASE		0x0
#define WCH_REG_BASE		0x2000
/*
 * Each rch (read from memory) REG offset  Rch_offset 0x0 each channel total 0x40
 * rch addr = DMA_base + Rch_offset+ chan_id * 0x40 + reg_offset
 */
#define RCH_READY		0x0
#define RCH_STATUS		0x4
#define RCH_CFG			0x8
#define CFG_CLEAR		BIT(25)
#define CFG_PAUSE		BIT(26)
#define CFG_ENABLE		BIT(27)
#define CFG_DONE		BIT(28)
#define RCH_ADDR		0xc
#define RCH_LEN			0x10
#define RCH_RD_LEN		0x14
#define RCH_PRT			0x18
#define RCH_SYCN_STAT		0x1c
#define RCH_ADDR_LOW		0x20
#define RCH_ADDR_HIGH		0x24
/* if work on 64, it work with RCH_PRT */
#define RCH_PTR_HIGH		0x28

/*
 * Each wch (write to memory) REG offset  Wch_offset 0x2000 each channel total 0x40
 * wch addr = DMA_base + Wch_offset+ chan_id * 0x40 + reg_offset
 */
#define WCH_READY		0x0
#define WCH_TOTAL_LEN		0x4
#define WCH_CFG			0x8
#define WCH_ADDR		0xc
#define WCH_LEN			0x10
#define WCH_RD_LEN		0x14
#define WCH_PRT			0x18
#define WCH_CMD_CNT		0x1c
#define WCH_ADDR_LOW		0x20
#define WCH_ADDR_HIGH		0x24
/* if work on 64, it work with RCH_PRT */
#define WCH_PTR_HIGH		0x28

/* DMA controller reg */
#define RCH_INT_MASK		0x1000
#define WCH_INT_MASK		0x1004
#define CLEAR_W_BATCH		0x1014
#define CLEAR_RCH		0x1024
#define CLEAR_WCH		0x1028
#define RCH_ACTIVE		0x1038
#define WCH_ACTIVE		0x103c
#define RCH_DONE		0x104c
#define WCH_DONE		0x1050
#define RCH_ERR			0x1060
#define RCH_LEN_ERR		0x1064
#define WCH_ERR			0x1068
#define DMA_BATCH_END		0x1078
#define WCH_EOC_DONE		0x1088
#define WDMA_RESP_ERR		0x1098
#define UPT_PKT_SYNC		0x10a8
#define RCHN_CFG		0x10ac
#define WCHN_CFG		0x10b0
#define MEM_PD_CFG		0x10b4
#define MEM_BUS_CFG		0x10b8
#define DMA_GMV_CFG		0x10bc
#define DMA_GMR_CFG		0x10c0

#define MAX_CHAN_ID		32
#define SG_MAX_LEN		GENMASK(26, 2)
#define INT_MASK		0xffffffff

struct aml_dma_sg_link {
#define LINK_LEN		GENMASK(26, 0)
#define LINK_IRQ		BIT(27)
#define LINK_EOC		BIT(28)
#define LINK_LOOP		BIT(29)
#define LINK_ERR		BIT(30)
#define LINK_OWNER		BIT(31)
	__le32 ctl;
	__le32 addr_low;
	__le32 addr_high;
	__le32 revered;
} __packed;

/* 1 page for link 256*16 */
#define DMA_MAX_LINK		256
/* sizeof(struct aml_dma_sg_link) */
#define DMA_LINK_SIZE		16
#define DMA_LINK_MAX_SIZE	(DMA_LINK_SIZE * DMA_MAX_LINK)

struct aml_dma_desc {
	struct virt_dma_desc		vd;
	struct aml_dma_sg_link		*sg_link;
	struct dma_device		*dma_device;
	dma_addr_t			sg_link_phys;
	size_t				sg_link_size;
	u32				data_len;
	struct work_struct		free_work;
};

struct aml_dma_chan {
	struct virt_dma_chan		vchan;
	struct aml_dma_dev		*aml_dma;
	struct aml_dma_desc		*cur_desc;
	enum dma_status			pre_status;
	enum dma_status			status;
	enum dma_transfer_direction	direction;
	int				chan_id;
	/* reg_base (direction + chan_id) */
	int				reg_offs;
	/* When there are multiple consecutive transmission errors, this channel halt */
	int				err_num;
};

struct aml_dma_dev {
	struct dma_device		dma_device;
	void __iomem			*base;
	struct regmap			*regmap;
	struct clk			*clk;
	int				irq;
	struct platform_device		*pdev;
	struct aml_dma_chan		*aml_rch[MAX_CHAN_ID];
	struct aml_dma_chan		*aml_wch[MAX_CHAN_ID];
	struct workqueue_struct		*free_wq;
	unsigned int			chan_nr;
	unsigned int			chan_used;
	struct aml_dma_chan		aml_chans[]__counted_by(chan_nr);
};

static inline struct aml_dma_chan *to_aml_dma_chan(struct dma_chan *chan)
{
	return container_of(chan, struct aml_dma_chan, vchan.chan);
}

static inline struct aml_dma_desc *to_aml_dma_desc(struct virt_dma_desc *vd)
{
	return container_of(vd, struct aml_dma_desc, vd);
}

static void aml_dma_free_desc_work(struct work_struct *work)
{
	struct aml_dma_desc *aml_desc =
			container_of(work, struct aml_dma_desc, free_work);

	dma_free_coherent(aml_desc->dma_device->dev,
			  aml_desc->sg_link_size,
			  aml_desc->sg_link,
			  aml_desc->sg_link_phys);
	kfree(aml_desc);
}

static void aml_dma_free_desc(struct virt_dma_desc *vd)
{
	struct aml_dma_desc *aml_desc = to_aml_dma_desc(vd);
	struct aml_dma_dev *aml_dev = container_of(aml_desc->dma_device,
						   struct aml_dma_dev, dma_device);

	queue_work(aml_dev->free_wq, &aml_desc->free_work);
}

static int aml_dma_alloc_chan_resources(struct dma_chan *chan)
{
	struct aml_dma_chan *aml_chan = to_aml_dma_chan(chan);
	struct aml_dma_dev *aml_dma = aml_chan->aml_dma;

	/* offset is the same RCH_CFG and WCH_CFG */
	regmap_set_bits(aml_dma->regmap, aml_chan->reg_offs + RCH_CFG, CFG_CLEAR);
	regmap_clear_bits(aml_dma->regmap, aml_chan->reg_offs + RCH_CFG, CFG_PAUSE);
	regmap_clear_bits(aml_dma->regmap, aml_chan->reg_offs + RCH_CFG, CFG_CLEAR);

	aml_chan->status = DMA_COMPLETE;
	aml_chan->cur_desc = NULL;
	aml_chan->err_num = 0;

	return 0;
}

static void aml_dma_free_chan_resources(struct dma_chan *chan)
{
	struct aml_dma_chan *aml_chan = to_aml_dma_chan(chan);
	struct aml_dma_dev *aml_dma = aml_chan->aml_dma;
	struct virt_dma_desc *cur_vd = NULL;
	unsigned long flags;

	spin_lock_irqsave(&aml_chan->vchan.lock, flags);
	regmap_set_bits(aml_dma->regmap, aml_chan->reg_offs + RCH_CFG, CFG_PAUSE);
	regmap_set_bits(aml_dma->regmap, aml_chan->reg_offs + RCH_CFG, CFG_CLEAR);
	if (aml_chan->cur_desc)
		cur_vd = &aml_chan->cur_desc->vd;
	aml_chan->cur_desc = NULL;
	spin_unlock_irqrestore(&aml_chan->vchan.lock, flags);
	if (cur_vd)
		aml_dma_free_desc(cur_vd);

	vchan_free_chan_resources(&aml_chan->vchan);
}

/* DMA transfer state  update how many data reside it */
static enum dma_status aml_dma_tx_status(struct dma_chan *chan,
					 dma_cookie_t cookie,
					 struct dma_tx_state *txstate)
{
	struct aml_dma_chan *aml_chan = to_aml_dma_chan(chan);
	struct aml_dma_dev *aml_dma = aml_chan->aml_dma;
	struct aml_dma_desc *aml_desc = NULL;
	struct virt_dma_desc *vd;
	u32 residue = 0, done = 0;
	unsigned long flags;
	enum dma_status ret;

	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret == DMA_COMPLETE)
		return ret;

	if (aml_chan->status == DMA_PAUSED)
		ret = DMA_PAUSED;
	if (!txstate)
		return ret;

	spin_lock_irqsave(&aml_chan->vchan.lock, flags);
	vd = vchan_find_desc(&aml_chan->vchan, cookie);
	if (vd) {
		aml_desc = to_aml_dma_desc(vd);
		residue = aml_desc->data_len;
	} else if (aml_chan->cur_desc && aml_chan->cur_desc->vd.tx.cookie == cookie) {
		aml_desc = aml_chan->cur_desc;
		regmap_read(aml_dma->regmap, aml_chan->reg_offs + RCH_RD_LEN, &done);
		residue = aml_desc->data_len - done;
	} else {
		dev_err(aml_dma->dma_device.dev, "cookie error\n");
	}
	spin_unlock_irqrestore(&aml_chan->vchan.lock, flags);
	dma_set_residue(txstate, residue);

	return ret;
}

static struct dma_async_tx_descriptor *aml_dma_prep_slave_sg
		(struct dma_chan *chan, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_transfer_direction direction,
		unsigned long flags, void *context)
{
	struct aml_dma_chan *aml_chan = to_aml_dma_chan(chan);
	struct aml_dma_dev *aml_dma = aml_chan->aml_dma;
	struct aml_dma_desc *aml_desc = NULL;
	struct aml_dma_sg_link *sg_link = NULL;
	struct scatterlist *sg = NULL;
	u64 paddr;
	u32 link_count, avail;
	u32 i;

	if (aml_chan->direction != direction) {
		dev_err(aml_dma->dma_device.dev, "direction not support\n");
		return NULL;
	}

	link_count = sg_nents_for_dma(sgl, sg_len, SG_MAX_LEN);
	if (link_count == 0)
		return NULL;

	aml_desc = kzalloc_obj(*aml_desc, GFP_NOWAIT);
	if (!aml_desc)
		return NULL;

	INIT_WORK(&aml_desc->free_work, aml_dma_free_desc_work);
	aml_desc->sg_link_size = link_count * sizeof(*sg_link);
	aml_desc->sg_link = dma_alloc_coherent(aml_dma->dma_device.dev, aml_desc->sg_link_size,
					       &aml_desc->sg_link_phys, GFP_NOWAIT);
	if (!aml_desc->sg_link) {
		kfree(aml_desc);
		return NULL;
	}
	aml_desc->dma_device = &aml_dma->dma_device;

	sg_link = aml_desc->sg_link;
	for_each_sg(sgl, sg, sg_len, i) {
		avail = sg_dma_len(sg);
		if (avail == 0)
			continue;
		paddr = sg->dma_address;
		while (avail > SG_MAX_LEN) {
			/* set dma address and len to sglink*/
			sg_link->addr_low = cpu_to_le32(lower_32_bits(paddr));
			sg_link->addr_high = cpu_to_le32(upper_32_bits(paddr));
			sg_link->ctl = cpu_to_le32(FIELD_PREP(LINK_LEN, SG_MAX_LEN));
			paddr = paddr + SG_MAX_LEN;
			avail = avail - SG_MAX_LEN;
			sg_link++;
		}
		/* set dma address and len to sglink*/
		sg_link->addr_low = cpu_to_le32(lower_32_bits(paddr));
		sg_link->addr_high = cpu_to_le32(upper_32_bits(paddr));
		sg_link->ctl = cpu_to_le32(FIELD_PREP(LINK_LEN, avail));

		aml_desc->data_len += sg_dma_len(sg);
		sg_link++;
	}

	/* the last sg set eoc flag */
	sg_link--;
	sg_link->ctl |= LINK_EOC;

	return vchan_tx_prep(&aml_chan->vchan, &aml_desc->vd, flags);
}

static int aml_dma_chan_pause(struct dma_chan *chan)
{
	struct aml_dma_chan *aml_chan = to_aml_dma_chan(chan);
	struct aml_dma_dev *aml_dma = aml_chan->aml_dma;
	unsigned long flags;

	spin_lock_irqsave(&aml_chan->vchan.lock, flags);
	regmap_set_bits(aml_dma->regmap, aml_chan->reg_offs + RCH_CFG, CFG_PAUSE);
	aml_chan->pre_status = aml_chan->status;
	aml_chan->status = DMA_PAUSED;
	spin_unlock_irqrestore(&aml_chan->vchan.lock, flags);

	return 0;
}

static int aml_dma_chan_resume(struct dma_chan *chan)
{
	struct aml_dma_chan *aml_chan = to_aml_dma_chan(chan);
	struct aml_dma_dev *aml_dma = aml_chan->aml_dma;
	unsigned long flags;

	spin_lock_irqsave(&aml_chan->vchan.lock, flags);
	regmap_clear_bits(aml_dma->regmap, aml_chan->reg_offs + RCH_CFG, CFG_PAUSE);
	aml_chan->status = aml_chan->pre_status;
	spin_unlock_irqrestore(&aml_chan->vchan.lock, flags);

	return 0;
}

static int aml_dma_terminate_all(struct dma_chan *chan)
{
	struct aml_dma_chan *aml_chan = to_aml_dma_chan(chan);
	struct aml_dma_dev *aml_dma = aml_chan->aml_dma;
	int chan_id = aml_chan->chan_id;
	struct virt_dma_desc *cur_vd;
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&aml_chan->vchan.lock, flags);
	regmap_set_bits(aml_dma->regmap, aml_chan->reg_offs + RCH_CFG, CFG_PAUSE);
	regmap_set_bits(aml_dma->regmap, aml_chan->reg_offs + RCH_CFG, CFG_CLEAR);

	if (aml_chan->direction == DMA_MEM_TO_DEV)
		regmap_set_bits(aml_dma->regmap, RCH_INT_MASK, BIT(chan_id));
	else if (aml_chan->direction == DMA_DEV_TO_MEM)
		regmap_set_bits(aml_dma->regmap, WCH_INT_MASK, BIT(chan_id));

	regmap_clear_bits(aml_dma->regmap, aml_chan->reg_offs + RCH_CFG, CFG_PAUSE);
	regmap_clear_bits(aml_dma->regmap, aml_chan->reg_offs + RCH_CFG, CFG_CLEAR);

	vchan_get_all_descriptors(&aml_chan->vchan, &head);
	cur_vd = &aml_chan->cur_desc->vd;
	aml_chan->cur_desc = NULL;
	spin_unlock_irqrestore(&aml_chan->vchan.lock, flags);
	if (cur_vd)
		aml_dma_free_desc(cur_vd);

	vchan_dma_desc_free_list(&aml_chan->vchan, &head);

	aml_chan->status = DMA_COMPLETE;
	aml_chan->err_num = 0;

	return 0;
}

static void aml_dma_start(struct aml_dma_chan *aml_chan)
{
	struct virt_dma_desc *vd = vchan_next_desc(&aml_chan->vchan);
	struct aml_dma_dev *aml_dma = aml_chan->aml_dma;
	struct aml_dma_desc *aml_desc = NULL;
	int chan_id = aml_chan->chan_id;

	if (aml_chan->status == DMA_ERROR) {
		if (aml_chan->err_num > 5) {
			dev_err(aml_dma->dma_device.dev, "hw error\n");
			return;
		}
		regmap_set_bits(aml_dma->regmap, aml_chan->reg_offs + RCH_CFG, CFG_PAUSE);
		regmap_set_bits(aml_dma->regmap, aml_chan->reg_offs + RCH_CFG, CFG_CLEAR);
		aml_chan->err_num++;
		regmap_clear_bits(aml_dma->regmap, aml_chan->reg_offs + RCH_CFG, CFG_PAUSE);
		regmap_clear_bits(aml_dma->regmap, aml_chan->reg_offs + RCH_CFG, CFG_CLEAR);
		aml_chan->status = DMA_COMPLETE;
	} else {
		aml_chan->err_num = 0;
	}

	if (!vd)
		return;

	if (aml_chan->status != DMA_COMPLETE)
		return;

	list_del(&vd->node);
	aml_desc = to_aml_dma_desc(vd);
	aml_chan->cur_desc = aml_desc;

	if (aml_chan->direction == DMA_MEM_TO_DEV) {
		regmap_write(aml_dma->regmap, aml_chan->reg_offs + RCH_ADDR,
			     lower_32_bits(aml_desc->sg_link_phys));
		regmap_write(aml_dma->regmap, aml_chan->reg_offs + RCH_ADDR_HIGH,
			     upper_32_bits(aml_desc->sg_link_phys));
		regmap_write(aml_dma->regmap, aml_chan->reg_offs + RCH_LEN, aml_desc->data_len);
		regmap_clear_bits(aml_dma->regmap, RCH_INT_MASK, BIT(chan_id));
		/* for rch (tx) need set cfg 0 to trigger start */
		regmap_write(aml_dma->regmap, aml_chan->reg_offs + RCH_CFG, 0);
	} else if (aml_chan->direction == DMA_DEV_TO_MEM) {
		regmap_write(aml_dma->regmap, aml_chan->reg_offs + WCH_ADDR,
			     lower_32_bits(aml_desc->sg_link_phys));
		regmap_write(aml_dma->regmap, aml_chan->reg_offs + WCH_ADDR_HIGH,
			     upper_32_bits(aml_desc->sg_link_phys));
		regmap_write(aml_dma->regmap, aml_chan->reg_offs + WCH_LEN, aml_desc->data_len);
		regmap_clear_bits(aml_dma->regmap, WCH_INT_MASK, BIT(chan_id));
	}
}

static void aml_dma_issue_pending(struct dma_chan *chan)
{
	struct aml_dma_chan *aml_chan = to_aml_dma_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&aml_chan->vchan.lock, flags);
	if (vchan_issue_pending(&aml_chan->vchan) && !aml_chan->cur_desc)
		aml_dma_start(aml_chan);
	spin_unlock_irqrestore(&aml_chan->vchan.lock, flags);
}

static irqreturn_t aml_dma_interrupt_handler(int irq, void *dev_id)
{
	struct aml_dma_dev *aml_dma = dev_id;
	struct aml_dma_chan *aml_chan;
	struct aml_dma_desc *aml_desc;
	u32 done, eoc_done, err, err_l, end;
	u32 cpl_data;
	int i = 0;

	/* deal with rch normal complete and error */
	regmap_read(aml_dma->regmap, RCH_DONE, &done);
	regmap_read(aml_dma->regmap, RCH_ERR, &err);
	regmap_read(aml_dma->regmap, RCH_LEN_ERR, &err_l);
	err = err | err_l;

	done = done | err;

	while (done) {
		i = ffs(done) - 1;
		regmap_write(aml_dma->regmap, CLEAR_RCH, BIT(i));
		done &= ~BIT(i);
		aml_chan = aml_dma->aml_rch[i];
		if (!aml_chan) {
			dev_err(aml_dma->dma_device.dev, "idx %d rch not initialized\n", i);
			continue;
		}
		spin_lock(&aml_chan->vchan.lock);
		aml_chan->status = (err & BIT(i)) ? DMA_ERROR : DMA_COMPLETE;
		aml_desc = aml_chan->cur_desc;
		if (!aml_desc) {
			spin_unlock(&aml_chan->vchan.lock);
			continue;
		}
		if (aml_chan->status == DMA_ERROR) {
			aml_desc->vd.tx_result.result = DMA_TRANS_READ_FAILED;
			regmap_read(aml_dma->regmap, aml_chan->reg_offs + RCH_RD_LEN, &cpl_data);
			aml_desc->vd.tx_result.residue = aml_desc->data_len - cpl_data;
		}
		vchan_cookie_complete(&aml_desc->vd);
		aml_chan->cur_desc = NULL;
		aml_dma_start(aml_chan);
		spin_unlock(&aml_chan->vchan.lock);
	}

	/* deal with wch normal complete and error */
	regmap_read(aml_dma->regmap, DMA_BATCH_END, &end);
	if (end)
		regmap_write(aml_dma->regmap, CLEAR_W_BATCH, end);

	regmap_read(aml_dma->regmap, WCH_DONE, &done);
	regmap_read(aml_dma->regmap, WCH_EOC_DONE, &eoc_done);
	done = done | eoc_done;

	regmap_read(aml_dma->regmap, WCH_ERR, &err);
	regmap_read(aml_dma->regmap, WDMA_RESP_ERR, &err_l);
	err = err | err_l;

	done = done | err;
	i = 0;
	while (done) {
		i = ffs(done) - 1;
		done &= ~BIT(i);
		regmap_write(aml_dma->regmap, CLEAR_WCH, BIT(i));
		aml_chan = aml_dma->aml_wch[i];
		if (!aml_chan) {
			dev_err(aml_dma->dma_device.dev, "idx %d wch not initialized\n", i);
			continue;
		}
		spin_lock(&aml_chan->vchan.lock);
		aml_chan->status = (err & BIT(i)) ? DMA_ERROR : DMA_COMPLETE;
		aml_desc = aml_chan->cur_desc;
		if (!aml_desc) {
			spin_unlock(&aml_chan->vchan.lock);
			continue;
		}
		if (aml_chan->status == DMA_ERROR) {
			aml_desc->vd.tx_result.result = DMA_TRANS_WRITE_FAILED;
			regmap_read(aml_dma->regmap, aml_chan->reg_offs + RCH_RD_LEN, &cpl_data);
			aml_desc->vd.tx_result.residue = aml_desc->data_len - cpl_data;
		}
		vchan_cookie_complete(&aml_desc->vd);
		aml_chan->cur_desc = NULL;
		aml_dma_start(aml_chan);
		spin_unlock(&aml_chan->vchan.lock);
	}

	return IRQ_HANDLED;
}

static struct dma_chan *aml_of_dma_xlate(struct of_phandle_args *dma_spec, struct of_dma *ofdma)
{
	struct aml_dma_dev *aml_dma = (struct aml_dma_dev *)ofdma->of_dma_data;
	struct aml_dma_chan *aml_chan = NULL;
	u32 type;
	u32 phy_chan_id;

	if (dma_spec->args_count != 2)
		return NULL;

	type = dma_spec->args[0];
	phy_chan_id = dma_spec->args[1];

	if (phy_chan_id >= MAX_CHAN_ID)
		return NULL;

	if (type == AMLOGIC_A9_DMA_TX) {
		aml_chan = aml_dma->aml_rch[phy_chan_id];
		if (!aml_chan) {
			if (aml_dma->chan_used >= aml_dma->chan_nr) {
				dev_err(aml_dma->dma_device.dev, "some dma clients err used\n");
				return NULL;
			}
			aml_chan = &aml_dma->aml_chans[aml_dma->chan_used];
			aml_dma->chan_used++;
			aml_chan->direction = DMA_MEM_TO_DEV;
			aml_chan->chan_id = phy_chan_id;
			aml_chan->reg_offs = RCH_REG_BASE + 0x40 * aml_chan->chan_id;
			aml_dma->aml_rch[phy_chan_id] = aml_chan;
		}
	} else if (type == AMLOGIC_A9_DMA_RX) {
		aml_chan = aml_dma->aml_wch[phy_chan_id];
		if (!aml_chan) {
			if (aml_dma->chan_used >= aml_dma->chan_nr) {
				dev_err(aml_dma->dma_device.dev, "some dma clients err used\n");
				return NULL;
			}
			aml_chan = &aml_dma->aml_chans[aml_dma->chan_used];
			aml_dma->chan_used++;
			aml_chan->direction = DMA_DEV_TO_MEM;
			aml_chan->chan_id = phy_chan_id;
			aml_chan->reg_offs = WCH_REG_BASE + 0x40 * aml_chan->chan_id;
			aml_dma->aml_wch[phy_chan_id] = aml_chan;
		}
	} else {
		dev_err(aml_dma->dma_device.dev, "type %d not supported\n", type);
		return NULL;
	}

	return dma_get_slave_channel(&aml_chan->vchan.chan);
}

static int aml_dma_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct dma_device *dma_dev;
	struct aml_dma_dev *aml_dma;
	int ret, i, len;
	u32 chan_nr;

	const struct regmap_config aml_regmap_config = {
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
		.max_register = 0x3000,
	};

	ret = of_property_read_u32(np, "dma-channels", &chan_nr);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to read dma-channels\n");
	if (chan_nr > (MAX_CHAN_ID * 2))
		return dev_err_probe(&pdev->dev, -EINVAL, "dma-channels unusual\n");

	len = sizeof(struct aml_dma_dev) + sizeof(struct aml_dma_chan) * chan_nr;
	aml_dma = devm_kzalloc(&pdev->dev, len, GFP_KERNEL);
	if (!aml_dma)
		return -ENOMEM;

	aml_dma->free_wq = alloc_ordered_workqueue("aml_dma_free", 0);
	if (!aml_dma->free_wq)
		return -ENOMEM;

	aml_dma->chan_nr = chan_nr;

	aml_dma->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(aml_dma->base))
		return PTR_ERR(aml_dma->base);

	aml_dma->regmap = devm_regmap_init_mmio(&pdev->dev, aml_dma->base,
						&aml_regmap_config);
	if (IS_ERR(aml_dma->regmap))
		return PTR_ERR(aml_dma->regmap);

	aml_dma->clk = devm_clk_get_enabled(&pdev->dev, NULL);
	if (IS_ERR(aml_dma->clk))
		return PTR_ERR(aml_dma->clk);

	aml_dma->irq = platform_get_irq(pdev, 0);

	aml_dma->pdev = pdev;
	aml_dma->dma_device.dev = &pdev->dev;

	dma_dev = &aml_dma->dma_device;
	INIT_LIST_HEAD(&dma_dev->channels);

	/* Initialize channel parameters */
	for (i = 0; i < chan_nr; i++) {
		struct aml_dma_chan *aml_chan = &aml_dma->aml_chans[i];

		aml_chan->aml_dma = aml_dma;
		aml_chan->vchan.desc_free = aml_dma_free_desc;
		vchan_init(&aml_chan->vchan, &aml_dma->dma_device);
	}
	aml_dma->chan_used = 0;

	dma_set_max_seg_size(dma_dev->dev, SG_MAX_LEN);
	dma_cap_set(DMA_SLAVE, dma_dev->cap_mask);
	dma_dev->device_alloc_chan_resources = aml_dma_alloc_chan_resources;
	dma_dev->device_free_chan_resources = aml_dma_free_chan_resources;
	dma_dev->device_tx_status = aml_dma_tx_status;
	dma_dev->device_prep_slave_sg = aml_dma_prep_slave_sg;
	dma_dev->device_pause = aml_dma_chan_pause;
	dma_dev->device_resume = aml_dma_chan_resume;
	dma_dev->device_terminate_all = aml_dma_terminate_all;
	dma_dev->device_issue_pending = aml_dma_issue_pending;
	/* PIO 4 bytes and I2C 1 byte */
	dma_dev->dst_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_4_BYTES) | BIT(DMA_SLAVE_BUSWIDTH_1_BYTE);
	dma_dev->directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	dma_dev->residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;

	regmap_write(aml_dma->regmap, RCH_INT_MASK, INT_MASK);
	regmap_write(aml_dma->regmap, WCH_INT_MASK, INT_MASK);

	ret = devm_request_irq(&pdev->dev, aml_dma->irq, aml_dma_interrupt_handler,
			       0, dev_name(&pdev->dev), aml_dma);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to register irq\n");

	ret = dmaenginem_async_device_register(dma_dev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to register dmaenginem\n");

	ret = of_dma_controller_register(np, aml_of_dma_xlate, aml_dma);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to register xlate\n");

	platform_set_drvdata(pdev, aml_dma);

	return 0;
}

static void aml_dma_remove(struct platform_device *pdev)
{
	struct aml_dma_dev *aml_dma = platform_get_drvdata(pdev);
	struct aml_dma_chan *aml_chan = NULL;
	int i;

	of_dma_controller_free((&pdev->dev)->of_node);

	regmap_write(aml_dma->regmap, RCH_INT_MASK, INT_MASK);
	regmap_write(aml_dma->regmap, WCH_INT_MASK, INT_MASK);

	for (i = 0; i < MAX_CHAN_ID; i++) {
		aml_chan = aml_dma->aml_rch[i];
		if (aml_chan)
			tasklet_kill(&aml_chan->vchan.task);
		aml_chan = aml_dma->aml_wch[i];
		if (aml_chan)
			tasklet_kill(&aml_chan->vchan.task);
	}
	destroy_workqueue(aml_dma->free_wq);
}

static const struct of_device_id aml_dma_ids[] = {
	{ .compatible = "amlogic,a9-dma", },
	{ }
};
MODULE_DEVICE_TABLE(of, aml_dma_ids);

static struct platform_driver aml_dma_driver = {
	.probe		= aml_dma_probe,
	.remove		= aml_dma_remove,
	.driver		= {
		.name	= "aml-dma",
		.of_match_table = aml_dma_ids,
	},
};

module_platform_driver(aml_dma_driver);

MODULE_DESCRIPTION("GENERAL DMA driver for Amlogic");
MODULE_AUTHOR("Xianwei Zhao <xianwei.zhao@amlogic.com>");
MODULE_LICENSE("GPL");
