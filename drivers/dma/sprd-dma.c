/*
 * Copyright (C) 2017 Spreadtrum Communications Inc.
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/dma/sprd-dma.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include "virt-dma.h"

#define SPRD_DMA_CHN_REG_OFFSET		0x1000
#define SPRD_DMA_CHN_REG_LENGTH		0x40
#define SPRD_DMA_MEMCPY_MIN_SIZE	64

/* DMA global registers definition */
#define SPRD_DMA_GLB_PAUSE		0x0
#define SPRD_DMA_GLB_FRAG_WAIT		0x4
#define SPRD_DMA_GLB_REQ_PEND0_EN	0x8
#define SPRD_DMA_GLB_REQ_PEND1_EN	0xc
#define SPRD_DMA_GLB_INT_RAW_STS	0x10
#define SPRD_DMA_GLB_INT_MSK_STS	0x14
#define SPRD_DMA_GLB_REQ_STS		0x18
#define SPRD_DMA_GLB_CHN_EN_STS		0x1c
#define SPRD_DMA_GLB_DEBUG_STS		0x20
#define SPRD_DMA_GLB_ARB_SEL_STS	0x24
#define SPRD_DMA_GLB_REQ_UID(uid)	(0x4 * ((uid) - 1))
#define SPRD_DMA_GLB_REQ_UID_OFFSET	0x2000

/* DMA channel registers definition */
#define SPRD_DMA_CHN_PAUSE		0x0
#define SPRD_DMA_CHN_REQ		0x4
#define SPRD_DMA_CHN_CFG		0x8
#define SPRD_DMA_CHN_INTC		0xc
#define SPRD_DMA_CHN_SRC_ADDR		0x10
#define SPRD_DMA_CHN_DES_ADDR		0x14
#define SPRD_DMA_CHN_FRG_LEN		0x18
#define SPRD_DMA_CHN_BLK_LEN		0x1c
#define SPRD_DMA_CHN_TRSC_LEN		0x20
#define SPRD_DMA_CHN_TRSF_STEP		0x24
#define SPRD_DMA_CHN_WARP_PTR		0x28
#define SPRD_DMA_CHN_WARP_TO		0x2c
#define SPRD_DMA_CHN_LLIST_PTR		0x30
#define SPRD_DMA_CHN_FRAG_STEP		0x34
#define SPRD_DMA_CHN_SRC_BLK_STEP	0x38
#define SPRD_DMA_CHN_DES_BLK_STEP	0x3c

/* SPRD_DMA_CHN_INTC register definition */
#define SPRD_DMA_INT_MASK		GENMASK(4, 0)
#define SPRD_DMA_INT_CLR_OFFSET		24
#define SPRD_DMA_FRAG_INT_EN		BIT(0)
#define SPRD_DMA_BLK_INT_EN		BIT(1)
#define SPRD_DMA_TRANS_INT_EN		BIT(2)
#define SPRD_DMA_LIST_INT_EN		BIT(3)
#define SPRD_DMA_CFG_ERR_INT_EN		BIT(4)

/* SPRD_DMA_CHN_CFG register definition */
#define SPRD_DMA_CHN_EN			BIT(0)
#define SPRD_DMA_LINKLIST_EN		BIT(4)
#define SPRD_DMA_WAIT_BDONE_OFFSET	24
#define SPRD_DMA_DONOT_WAIT_BDONE	1

/* SPRD_DMA_CHN_REQ register definition */
#define SPRD_DMA_REQ_EN			BIT(0)

/* SPRD_DMA_CHN_PAUSE register definition */
#define SPRD_DMA_PAUSE_EN		BIT(0)
#define SPRD_DMA_PAUSE_STS		BIT(2)
#define SPRD_DMA_PAUSE_CNT		0x2000

/* DMA_CHN_WARP_* register definition */
#define SPRD_DMA_HIGH_ADDR_MASK		GENMASK(31, 28)
#define SPRD_DMA_LOW_ADDR_MASK		GENMASK(31, 0)
#define SPRD_DMA_HIGH_ADDR_OFFSET	4

/* SPRD_DMA_CHN_INTC register definition */
#define SPRD_DMA_FRAG_INT_STS		BIT(16)
#define SPRD_DMA_BLK_INT_STS		BIT(17)
#define SPRD_DMA_TRSC_INT_STS		BIT(18)
#define SPRD_DMA_LIST_INT_STS		BIT(19)
#define SPRD_DMA_CFGERR_INT_STS		BIT(20)
#define SPRD_DMA_CHN_INT_STS					\
	(SPRD_DMA_FRAG_INT_STS | SPRD_DMA_BLK_INT_STS |		\
	 SPRD_DMA_TRSC_INT_STS | SPRD_DMA_LIST_INT_STS |	\
	 SPRD_DMA_CFGERR_INT_STS)

/* SPRD_DMA_CHN_FRG_LEN register definition */
#define SPRD_DMA_SRC_DATAWIDTH_OFFSET	30
#define SPRD_DMA_DES_DATAWIDTH_OFFSET	28
#define SPRD_DMA_SWT_MODE_OFFSET	26
#define SPRD_DMA_REQ_MODE_OFFSET	24
#define SPRD_DMA_REQ_MODE_MASK		GENMASK(1, 0)
#define SPRD_DMA_FIX_SEL_OFFSET		21
#define SPRD_DMA_FIX_EN_OFFSET		20
#define SPRD_DMA_LLIST_END		BIT(19)
#define SPRD_DMA_FRG_LEN_MASK		GENMASK(16, 0)

/* SPRD_DMA_CHN_BLK_LEN register definition */
#define SPRD_DMA_BLK_LEN_MASK		GENMASK(16, 0)

/* SPRD_DMA_CHN_TRSC_LEN register definition */
#define SPRD_DMA_TRSC_LEN_MASK		GENMASK(27, 0)

/* SPRD_DMA_CHN_TRSF_STEP register definition */
#define SPRD_DMA_DEST_TRSF_STEP_OFFSET	16
#define SPRD_DMA_SRC_TRSF_STEP_OFFSET	0
#define SPRD_DMA_TRSF_STEP_MASK		GENMASK(15, 0)

/* define the DMA transfer step type */
#define SPRD_DMA_NONE_STEP		0
#define SPRD_DMA_BYTE_STEP		1
#define SPRD_DMA_SHORT_STEP		2
#define SPRD_DMA_WORD_STEP		4
#define SPRD_DMA_DWORD_STEP		8

#define SPRD_DMA_SOFTWARE_UID		0

/* dma data width values */
enum sprd_dma_datawidth {
	SPRD_DMA_DATAWIDTH_1_BYTE,
	SPRD_DMA_DATAWIDTH_2_BYTES,
	SPRD_DMA_DATAWIDTH_4_BYTES,
	SPRD_DMA_DATAWIDTH_8_BYTES,
};

/* dma channel hardware configuration */
struct sprd_dma_chn_hw {
	u32 pause;
	u32 req;
	u32 cfg;
	u32 intc;
	u32 src_addr;
	u32 des_addr;
	u32 frg_len;
	u32 blk_len;
	u32 trsc_len;
	u32 trsf_step;
	u32 wrap_ptr;
	u32 wrap_to;
	u32 llist_ptr;
	u32 frg_step;
	u32 src_blk_step;
	u32 des_blk_step;
};

/* dma request description */
struct sprd_dma_desc {
	struct virt_dma_desc	vd;
	struct sprd_dma_chn_hw	chn_hw;
};

/* dma channel description */
struct sprd_dma_chn {
	struct virt_dma_chan	vc;
	void __iomem		*chn_base;
	struct sprd_dma_linklist	linklist;
	struct dma_slave_config	slave_cfg;
	u32			chn_num;
	u32			dev_id;
	struct sprd_dma_desc	*cur_desc;
};

/* SPRD dma device */
struct sprd_dma_dev {
	struct dma_device	dma_dev;
	void __iomem		*glb_base;
	struct clk		*clk;
	struct clk		*ashb_clk;
	int			irq;
	u32			total_chns;
	struct sprd_dma_chn	channels[0];
};

static bool sprd_dma_filter_fn(struct dma_chan *chan, void *param);
static struct of_dma_filter_info sprd_dma_info = {
	.filter_fn = sprd_dma_filter_fn,
};

static inline struct sprd_dma_chn *to_sprd_dma_chan(struct dma_chan *c)
{
	return container_of(c, struct sprd_dma_chn, vc.chan);
}

static inline struct sprd_dma_dev *to_sprd_dma_dev(struct dma_chan *c)
{
	struct sprd_dma_chn *schan = to_sprd_dma_chan(c);

	return container_of(schan, struct sprd_dma_dev, channels[c->chan_id]);
}

static inline struct sprd_dma_desc *to_sprd_dma_desc(struct virt_dma_desc *vd)
{
	return container_of(vd, struct sprd_dma_desc, vd);
}

static void sprd_dma_chn_update(struct sprd_dma_chn *schan, u32 reg,
				u32 mask, u32 val)
{
	u32 orig = readl(schan->chn_base + reg);
	u32 tmp;

	tmp = (orig & ~mask) | val;
	writel(tmp, schan->chn_base + reg);
}

static int sprd_dma_enable(struct sprd_dma_dev *sdev)
{
	int ret;

	ret = clk_prepare_enable(sdev->clk);
	if (ret)
		return ret;

	/*
	 * The ashb_clk is optional and only for AGCP DMA controller, so we
	 * need add one condition to check if the ashb_clk need enable.
	 */
	if (!IS_ERR(sdev->ashb_clk))
		ret = clk_prepare_enable(sdev->ashb_clk);

	return ret;
}

static void sprd_dma_disable(struct sprd_dma_dev *sdev)
{
	clk_disable_unprepare(sdev->clk);

	/*
	 * Need to check if we need disable the optional ashb_clk for AGCP DMA.
	 */
	if (!IS_ERR(sdev->ashb_clk))
		clk_disable_unprepare(sdev->ashb_clk);
}

static void sprd_dma_set_uid(struct sprd_dma_chn *schan)
{
	struct sprd_dma_dev *sdev = to_sprd_dma_dev(&schan->vc.chan);
	u32 dev_id = schan->dev_id;

	if (dev_id != SPRD_DMA_SOFTWARE_UID) {
		u32 uid_offset = SPRD_DMA_GLB_REQ_UID_OFFSET +
				 SPRD_DMA_GLB_REQ_UID(dev_id);

		writel(schan->chn_num + 1, sdev->glb_base + uid_offset);
	}
}

static void sprd_dma_unset_uid(struct sprd_dma_chn *schan)
{
	struct sprd_dma_dev *sdev = to_sprd_dma_dev(&schan->vc.chan);
	u32 dev_id = schan->dev_id;

	if (dev_id != SPRD_DMA_SOFTWARE_UID) {
		u32 uid_offset = SPRD_DMA_GLB_REQ_UID_OFFSET +
				 SPRD_DMA_GLB_REQ_UID(dev_id);

		writel(0, sdev->glb_base + uid_offset);
	}
}

static void sprd_dma_clear_int(struct sprd_dma_chn *schan)
{
	sprd_dma_chn_update(schan, SPRD_DMA_CHN_INTC,
			    SPRD_DMA_INT_MASK << SPRD_DMA_INT_CLR_OFFSET,
			    SPRD_DMA_INT_MASK << SPRD_DMA_INT_CLR_OFFSET);
}

static void sprd_dma_enable_chn(struct sprd_dma_chn *schan)
{
	sprd_dma_chn_update(schan, SPRD_DMA_CHN_CFG, SPRD_DMA_CHN_EN,
			    SPRD_DMA_CHN_EN);
}

static void sprd_dma_disable_chn(struct sprd_dma_chn *schan)
{
	sprd_dma_chn_update(schan, SPRD_DMA_CHN_CFG, SPRD_DMA_CHN_EN, 0);
}

static void sprd_dma_soft_request(struct sprd_dma_chn *schan)
{
	sprd_dma_chn_update(schan, SPRD_DMA_CHN_REQ, SPRD_DMA_REQ_EN,
			    SPRD_DMA_REQ_EN);
}

static void sprd_dma_pause_resume(struct sprd_dma_chn *schan, bool enable)
{
	struct sprd_dma_dev *sdev = to_sprd_dma_dev(&schan->vc.chan);
	u32 pause, timeout = SPRD_DMA_PAUSE_CNT;

	if (enable) {
		sprd_dma_chn_update(schan, SPRD_DMA_CHN_PAUSE,
				    SPRD_DMA_PAUSE_EN, SPRD_DMA_PAUSE_EN);

		do {
			pause = readl(schan->chn_base + SPRD_DMA_CHN_PAUSE);
			if (pause & SPRD_DMA_PAUSE_STS)
				break;

			cpu_relax();
		} while (--timeout > 0);

		if (!timeout)
			dev_warn(sdev->dma_dev.dev,
				 "pause dma controller timeout\n");
	} else {
		sprd_dma_chn_update(schan, SPRD_DMA_CHN_PAUSE,
				    SPRD_DMA_PAUSE_EN, 0);
	}
}

static void sprd_dma_stop_and_disable(struct sprd_dma_chn *schan)
{
	u32 cfg = readl(schan->chn_base + SPRD_DMA_CHN_CFG);

	if (!(cfg & SPRD_DMA_CHN_EN))
		return;

	sprd_dma_pause_resume(schan, true);
	sprd_dma_disable_chn(schan);
}

static unsigned long sprd_dma_get_dst_addr(struct sprd_dma_chn *schan)
{
	unsigned long addr, addr_high;

	addr = readl(schan->chn_base + SPRD_DMA_CHN_DES_ADDR);
	addr_high = readl(schan->chn_base + SPRD_DMA_CHN_WARP_TO) &
		    SPRD_DMA_HIGH_ADDR_MASK;

	return addr | (addr_high << SPRD_DMA_HIGH_ADDR_OFFSET);
}

static enum sprd_dma_int_type sprd_dma_get_int_type(struct sprd_dma_chn *schan)
{
	struct sprd_dma_dev *sdev = to_sprd_dma_dev(&schan->vc.chan);
	u32 intc_sts = readl(schan->chn_base + SPRD_DMA_CHN_INTC) &
		       SPRD_DMA_CHN_INT_STS;

	switch (intc_sts) {
	case SPRD_DMA_CFGERR_INT_STS:
		return SPRD_DMA_CFGERR_INT;

	case SPRD_DMA_LIST_INT_STS:
		return SPRD_DMA_LIST_INT;

	case SPRD_DMA_TRSC_INT_STS:
		return SPRD_DMA_TRANS_INT;

	case SPRD_DMA_BLK_INT_STS:
		return SPRD_DMA_BLK_INT;

	case SPRD_DMA_FRAG_INT_STS:
		return SPRD_DMA_FRAG_INT;

	default:
		dev_warn(sdev->dma_dev.dev, "incorrect dma interrupt type\n");
		return SPRD_DMA_NO_INT;
	}
}

static enum sprd_dma_req_mode sprd_dma_get_req_type(struct sprd_dma_chn *schan)
{
	u32 frag_reg = readl(schan->chn_base + SPRD_DMA_CHN_FRG_LEN);

	return (frag_reg >> SPRD_DMA_REQ_MODE_OFFSET) & SPRD_DMA_REQ_MODE_MASK;
}

static void sprd_dma_set_chn_config(struct sprd_dma_chn *schan,
				    struct sprd_dma_desc *sdesc)
{
	struct sprd_dma_chn_hw *cfg = &sdesc->chn_hw;

	writel(cfg->pause, schan->chn_base + SPRD_DMA_CHN_PAUSE);
	writel(cfg->cfg, schan->chn_base + SPRD_DMA_CHN_CFG);
	writel(cfg->intc, schan->chn_base + SPRD_DMA_CHN_INTC);
	writel(cfg->src_addr, schan->chn_base + SPRD_DMA_CHN_SRC_ADDR);
	writel(cfg->des_addr, schan->chn_base + SPRD_DMA_CHN_DES_ADDR);
	writel(cfg->frg_len, schan->chn_base + SPRD_DMA_CHN_FRG_LEN);
	writel(cfg->blk_len, schan->chn_base + SPRD_DMA_CHN_BLK_LEN);
	writel(cfg->trsc_len, schan->chn_base + SPRD_DMA_CHN_TRSC_LEN);
	writel(cfg->trsf_step, schan->chn_base + SPRD_DMA_CHN_TRSF_STEP);
	writel(cfg->wrap_ptr, schan->chn_base + SPRD_DMA_CHN_WARP_PTR);
	writel(cfg->wrap_to, schan->chn_base + SPRD_DMA_CHN_WARP_TO);
	writel(cfg->llist_ptr, schan->chn_base + SPRD_DMA_CHN_LLIST_PTR);
	writel(cfg->frg_step, schan->chn_base + SPRD_DMA_CHN_FRAG_STEP);
	writel(cfg->src_blk_step, schan->chn_base + SPRD_DMA_CHN_SRC_BLK_STEP);
	writel(cfg->des_blk_step, schan->chn_base + SPRD_DMA_CHN_DES_BLK_STEP);
	writel(cfg->req, schan->chn_base + SPRD_DMA_CHN_REQ);
}

static void sprd_dma_start(struct sprd_dma_chn *schan)
{
	struct virt_dma_desc *vd = vchan_next_desc(&schan->vc);

	if (!vd)
		return;

	list_del(&vd->node);
	schan->cur_desc = to_sprd_dma_desc(vd);

	/*
	 * Copy the DMA configuration from DMA descriptor to this hardware
	 * channel.
	 */
	sprd_dma_set_chn_config(schan, schan->cur_desc);
	sprd_dma_set_uid(schan);
	sprd_dma_enable_chn(schan);

	if (schan->dev_id == SPRD_DMA_SOFTWARE_UID)
		sprd_dma_soft_request(schan);
}

static void sprd_dma_stop(struct sprd_dma_chn *schan)
{
	sprd_dma_stop_and_disable(schan);
	sprd_dma_unset_uid(schan);
	sprd_dma_clear_int(schan);
}

static bool sprd_dma_check_trans_done(struct sprd_dma_desc *sdesc,
				      enum sprd_dma_int_type int_type,
				      enum sprd_dma_req_mode req_mode)
{
	if (int_type == SPRD_DMA_NO_INT)
		return false;

	if (int_type >= req_mode + 1)
		return true;
	else
		return false;
}

static irqreturn_t dma_irq_handle(int irq, void *dev_id)
{
	struct sprd_dma_dev *sdev = (struct sprd_dma_dev *)dev_id;
	u32 irq_status = readl(sdev->glb_base + SPRD_DMA_GLB_INT_MSK_STS);
	struct sprd_dma_chn *schan;
	struct sprd_dma_desc *sdesc;
	enum sprd_dma_req_mode req_type;
	enum sprd_dma_int_type int_type;
	bool trans_done = false;
	u32 i;

	while (irq_status) {
		i = __ffs(irq_status);
		irq_status &= (irq_status - 1);
		schan = &sdev->channels[i];

		spin_lock(&schan->vc.lock);
		int_type = sprd_dma_get_int_type(schan);
		req_type = sprd_dma_get_req_type(schan);
		sprd_dma_clear_int(schan);

		sdesc = schan->cur_desc;

		/* Check if the dma request descriptor is done. */
		trans_done = sprd_dma_check_trans_done(sdesc, int_type,
						       req_type);
		if (trans_done == true) {
			vchan_cookie_complete(&sdesc->vd);
			schan->cur_desc = NULL;
			sprd_dma_start(schan);
		}
		spin_unlock(&schan->vc.lock);
	}

	return IRQ_HANDLED;
}

static int sprd_dma_alloc_chan_resources(struct dma_chan *chan)
{
	struct sprd_dma_chn *schan = to_sprd_dma_chan(chan);
	int ret;

	ret = pm_runtime_get_sync(chan->device->dev);
	if (ret < 0)
		return ret;

	schan->dev_id = SPRD_DMA_SOFTWARE_UID;
	return 0;
}

static void sprd_dma_free_chan_resources(struct dma_chan *chan)
{
	struct sprd_dma_chn *schan = to_sprd_dma_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&schan->vc.lock, flags);
	sprd_dma_stop(schan);
	spin_unlock_irqrestore(&schan->vc.lock, flags);

	vchan_free_chan_resources(&schan->vc);
	pm_runtime_put(chan->device->dev);
}

static enum dma_status sprd_dma_tx_status(struct dma_chan *chan,
					  dma_cookie_t cookie,
					  struct dma_tx_state *txstate)
{
	struct sprd_dma_chn *schan = to_sprd_dma_chan(chan);
	struct virt_dma_desc *vd;
	unsigned long flags;
	enum dma_status ret;
	u32 pos;

	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret == DMA_COMPLETE || !txstate)
		return ret;

	spin_lock_irqsave(&schan->vc.lock, flags);
	vd = vchan_find_desc(&schan->vc, cookie);
	if (vd) {
		struct sprd_dma_desc *sdesc = to_sprd_dma_desc(vd);
		struct sprd_dma_chn_hw *hw = &sdesc->chn_hw;

		if (hw->trsc_len > 0)
			pos = hw->trsc_len;
		else if (hw->blk_len > 0)
			pos = hw->blk_len;
		else if (hw->frg_len > 0)
			pos = hw->frg_len;
		else
			pos = 0;
	} else if (schan->cur_desc && schan->cur_desc->vd.tx.cookie == cookie) {
		pos = sprd_dma_get_dst_addr(schan);
	} else {
		pos = 0;
	}
	spin_unlock_irqrestore(&schan->vc.lock, flags);

	dma_set_residue(txstate, pos);
	return ret;
}

static void sprd_dma_issue_pending(struct dma_chan *chan)
{
	struct sprd_dma_chn *schan = to_sprd_dma_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&schan->vc.lock, flags);
	if (vchan_issue_pending(&schan->vc) && !schan->cur_desc)
		sprd_dma_start(schan);
	spin_unlock_irqrestore(&schan->vc.lock, flags);
}

static int sprd_dma_get_datawidth(enum dma_slave_buswidth buswidth)
{
	switch (buswidth) {
	case DMA_SLAVE_BUSWIDTH_1_BYTE:
	case DMA_SLAVE_BUSWIDTH_2_BYTES:
	case DMA_SLAVE_BUSWIDTH_4_BYTES:
	case DMA_SLAVE_BUSWIDTH_8_BYTES:
		return ffs(buswidth) - 1;

	default:
		return -EINVAL;
	}
}

static int sprd_dma_get_step(enum dma_slave_buswidth buswidth)
{
	switch (buswidth) {
	case DMA_SLAVE_BUSWIDTH_1_BYTE:
	case DMA_SLAVE_BUSWIDTH_2_BYTES:
	case DMA_SLAVE_BUSWIDTH_4_BYTES:
	case DMA_SLAVE_BUSWIDTH_8_BYTES:
		return buswidth;

	default:
		return -EINVAL;
	}
}

static int sprd_dma_fill_desc(struct dma_chan *chan,
			      struct sprd_dma_chn_hw *hw,
			      unsigned int sglen, int sg_index,
			      dma_addr_t src, dma_addr_t dst, u32 len,
			      enum dma_transfer_direction dir,
			      unsigned long flags,
			      struct dma_slave_config *slave_cfg)
{
	struct sprd_dma_dev *sdev = to_sprd_dma_dev(chan);
	struct sprd_dma_chn *schan = to_sprd_dma_chan(chan);
	u32 req_mode = (flags >> SPRD_DMA_REQ_SHIFT) & SPRD_DMA_REQ_MODE_MASK;
	u32 int_mode = flags & SPRD_DMA_INT_MASK;
	int src_datawidth, dst_datawidth, src_step, dst_step;
	u32 temp, fix_mode = 0, fix_en = 0;

	if (dir == DMA_MEM_TO_DEV) {
		src_step = sprd_dma_get_step(slave_cfg->src_addr_width);
		if (src_step < 0) {
			dev_err(sdev->dma_dev.dev, "invalid source step\n");
			return src_step;
		}
		dst_step = SPRD_DMA_NONE_STEP;
	} else {
		dst_step = sprd_dma_get_step(slave_cfg->dst_addr_width);
		if (dst_step < 0) {
			dev_err(sdev->dma_dev.dev, "invalid destination step\n");
			return dst_step;
		}
		src_step = SPRD_DMA_NONE_STEP;
	}

	src_datawidth = sprd_dma_get_datawidth(slave_cfg->src_addr_width);
	if (src_datawidth < 0) {
		dev_err(sdev->dma_dev.dev, "invalid source datawidth\n");
		return src_datawidth;
	}

	dst_datawidth = sprd_dma_get_datawidth(slave_cfg->dst_addr_width);
	if (dst_datawidth < 0) {
		dev_err(sdev->dma_dev.dev, "invalid destination datawidth\n");
		return dst_datawidth;
	}

	if (slave_cfg->slave_id)
		schan->dev_id = slave_cfg->slave_id;

	hw->cfg = SPRD_DMA_DONOT_WAIT_BDONE << SPRD_DMA_WAIT_BDONE_OFFSET;

	/*
	 * wrap_ptr and wrap_to will save the high 4 bits source address and
	 * destination address.
	 */
	hw->wrap_ptr = (src >> SPRD_DMA_HIGH_ADDR_OFFSET) & SPRD_DMA_HIGH_ADDR_MASK;
	hw->wrap_to = (dst >> SPRD_DMA_HIGH_ADDR_OFFSET) & SPRD_DMA_HIGH_ADDR_MASK;
	hw->src_addr = src & SPRD_DMA_LOW_ADDR_MASK;
	hw->des_addr = dst & SPRD_DMA_LOW_ADDR_MASK;

	/*
	 * If the src step and dst step both are 0 or both are not 0, that means
	 * we can not enable the fix mode. If one is 0 and another one is not,
	 * we can enable the fix mode.
	 */
	if ((src_step != 0 && dst_step != 0) || (src_step | dst_step) == 0) {
		fix_en = 0;
	} else {
		fix_en = 1;
		if (src_step)
			fix_mode = 1;
		else
			fix_mode = 0;
	}

	hw->intc = int_mode | SPRD_DMA_CFG_ERR_INT_EN;

	temp = src_datawidth << SPRD_DMA_SRC_DATAWIDTH_OFFSET;
	temp |= dst_datawidth << SPRD_DMA_DES_DATAWIDTH_OFFSET;
	temp |= req_mode << SPRD_DMA_REQ_MODE_OFFSET;
	temp |= fix_mode << SPRD_DMA_FIX_SEL_OFFSET;
	temp |= fix_en << SPRD_DMA_FIX_EN_OFFSET;
	temp |= slave_cfg->src_maxburst & SPRD_DMA_FRG_LEN_MASK;
	hw->frg_len = temp;

	hw->blk_len = len & SPRD_DMA_BLK_LEN_MASK;
	hw->trsc_len = len & SPRD_DMA_TRSC_LEN_MASK;

	temp = (dst_step & SPRD_DMA_TRSF_STEP_MASK) << SPRD_DMA_DEST_TRSF_STEP_OFFSET;
	temp |= (src_step & SPRD_DMA_TRSF_STEP_MASK) << SPRD_DMA_SRC_TRSF_STEP_OFFSET;
	hw->trsf_step = temp;

	/* link-list configuration */
	if (schan->linklist.phy_addr) {
		if (sg_index == sglen - 1)
			hw->frg_len |= SPRD_DMA_LLIST_END;

		hw->cfg |= SPRD_DMA_LINKLIST_EN;

		/* link-list index */
		temp = (sg_index + 1) % sglen;
		/* Next link-list configuration's physical address offset */
		temp = temp * sizeof(*hw) + SPRD_DMA_CHN_SRC_ADDR;
		/*
		 * Set the link-list pointer point to next link-list
		 * configuration's physical address.
		 */
		hw->llist_ptr = schan->linklist.phy_addr + temp;
	} else {
		hw->llist_ptr = 0;
	}

	hw->frg_step = 0;
	hw->src_blk_step = 0;
	hw->des_blk_step = 0;
	return 0;
}

static int sprd_dma_fill_linklist_desc(struct dma_chan *chan,
				       unsigned int sglen, int sg_index,
				       dma_addr_t src, dma_addr_t dst, u32 len,
				       enum dma_transfer_direction dir,
				       unsigned long flags,
				       struct dma_slave_config *slave_cfg)
{
	struct sprd_dma_chn *schan = to_sprd_dma_chan(chan);
	struct sprd_dma_chn_hw *hw;

	if (!schan->linklist.virt_addr)
		return -EINVAL;

	hw = (struct sprd_dma_chn_hw *)(schan->linklist.virt_addr +
					sg_index * sizeof(*hw));

	return sprd_dma_fill_desc(chan, hw, sglen, sg_index, src, dst, len,
				  dir, flags, slave_cfg);
}

static struct dma_async_tx_descriptor *
sprd_dma_prep_dma_memcpy(struct dma_chan *chan, dma_addr_t dest, dma_addr_t src,
			 size_t len, unsigned long flags)
{
	struct sprd_dma_chn *schan = to_sprd_dma_chan(chan);
	struct sprd_dma_desc *sdesc;
	struct sprd_dma_chn_hw *hw;
	enum sprd_dma_datawidth datawidth;
	u32 step, temp;

	sdesc = kzalloc(sizeof(*sdesc), GFP_NOWAIT);
	if (!sdesc)
		return NULL;

	hw = &sdesc->chn_hw;

	hw->cfg = SPRD_DMA_DONOT_WAIT_BDONE << SPRD_DMA_WAIT_BDONE_OFFSET;
	hw->intc = SPRD_DMA_TRANS_INT | SPRD_DMA_CFG_ERR_INT_EN;
	hw->src_addr = src & SPRD_DMA_LOW_ADDR_MASK;
	hw->des_addr = dest & SPRD_DMA_LOW_ADDR_MASK;
	hw->wrap_ptr = (src >> SPRD_DMA_HIGH_ADDR_OFFSET) &
		SPRD_DMA_HIGH_ADDR_MASK;
	hw->wrap_to = (dest >> SPRD_DMA_HIGH_ADDR_OFFSET) &
		SPRD_DMA_HIGH_ADDR_MASK;

	if (IS_ALIGNED(len, 8)) {
		datawidth = SPRD_DMA_DATAWIDTH_8_BYTES;
		step = SPRD_DMA_DWORD_STEP;
	} else if (IS_ALIGNED(len, 4)) {
		datawidth = SPRD_DMA_DATAWIDTH_4_BYTES;
		step = SPRD_DMA_WORD_STEP;
	} else if (IS_ALIGNED(len, 2)) {
		datawidth = SPRD_DMA_DATAWIDTH_2_BYTES;
		step = SPRD_DMA_SHORT_STEP;
	} else {
		datawidth = SPRD_DMA_DATAWIDTH_1_BYTE;
		step = SPRD_DMA_BYTE_STEP;
	}

	temp = datawidth << SPRD_DMA_SRC_DATAWIDTH_OFFSET;
	temp |= datawidth << SPRD_DMA_DES_DATAWIDTH_OFFSET;
	temp |= SPRD_DMA_TRANS_REQ << SPRD_DMA_REQ_MODE_OFFSET;
	temp |= len & SPRD_DMA_FRG_LEN_MASK;
	hw->frg_len = temp;

	hw->blk_len = len & SPRD_DMA_BLK_LEN_MASK;
	hw->trsc_len = len & SPRD_DMA_TRSC_LEN_MASK;

	temp = (step & SPRD_DMA_TRSF_STEP_MASK) << SPRD_DMA_DEST_TRSF_STEP_OFFSET;
	temp |= (step & SPRD_DMA_TRSF_STEP_MASK) << SPRD_DMA_SRC_TRSF_STEP_OFFSET;
	hw->trsf_step = temp;

	return vchan_tx_prep(&schan->vc, &sdesc->vd, flags);
}

static struct dma_async_tx_descriptor *
sprd_dma_prep_slave_sg(struct dma_chan *chan, struct scatterlist *sgl,
		       unsigned int sglen, enum dma_transfer_direction dir,
		       unsigned long flags, void *context)
{
	struct sprd_dma_chn *schan = to_sprd_dma_chan(chan);
	struct dma_slave_config *slave_cfg = &schan->slave_cfg;
	dma_addr_t src = 0, dst = 0;
	struct sprd_dma_desc *sdesc;
	struct scatterlist *sg;
	u32 len = 0;
	int ret, i;

	if (!is_slave_direction(dir))
		return NULL;

	if (context) {
		struct sprd_dma_linklist *ll_cfg =
			(struct sprd_dma_linklist *)context;

		schan->linklist.phy_addr = ll_cfg->phy_addr;
		schan->linklist.virt_addr = ll_cfg->virt_addr;
	} else {
		schan->linklist.phy_addr = 0;
		schan->linklist.virt_addr = 0;
	}

	sdesc = kzalloc(sizeof(*sdesc), GFP_NOWAIT);
	if (!sdesc)
		return NULL;

	for_each_sg(sgl, sg, sglen, i) {
		len = sg_dma_len(sg);

		if (dir == DMA_MEM_TO_DEV) {
			src = sg_dma_address(sg);
			dst = slave_cfg->dst_addr;
		} else {
			src = slave_cfg->src_addr;
			dst = sg_dma_address(sg);
		}

		/*
		 * The link-list mode needs at least 2 link-list
		 * configurations. If there is only one sg, it doesn't
		 * need to fill the link-list configuration.
		 */
		if (sglen < 2)
			break;

		ret = sprd_dma_fill_linklist_desc(chan, sglen, i, src, dst, len,
						  dir, flags, slave_cfg);
		if (ret) {
			kfree(sdesc);
			return NULL;
		}
	}

	ret = sprd_dma_fill_desc(chan, &sdesc->chn_hw, 0, 0, src, dst, len,
				 dir, flags, slave_cfg);
	if (ret) {
		kfree(sdesc);
		return NULL;
	}

	return vchan_tx_prep(&schan->vc, &sdesc->vd, flags);
}

static int sprd_dma_slave_config(struct dma_chan *chan,
				 struct dma_slave_config *config)
{
	struct sprd_dma_chn *schan = to_sprd_dma_chan(chan);
	struct dma_slave_config *slave_cfg = &schan->slave_cfg;

	if (!is_slave_direction(config->direction))
		return -EINVAL;

	memcpy(slave_cfg, config, sizeof(*config));
	return 0;
}

static int sprd_dma_pause(struct dma_chan *chan)
{
	struct sprd_dma_chn *schan = to_sprd_dma_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&schan->vc.lock, flags);
	sprd_dma_pause_resume(schan, true);
	spin_unlock_irqrestore(&schan->vc.lock, flags);

	return 0;
}

static int sprd_dma_resume(struct dma_chan *chan)
{
	struct sprd_dma_chn *schan = to_sprd_dma_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&schan->vc.lock, flags);
	sprd_dma_pause_resume(schan, false);
	spin_unlock_irqrestore(&schan->vc.lock, flags);

	return 0;
}

static int sprd_dma_terminate_all(struct dma_chan *chan)
{
	struct sprd_dma_chn *schan = to_sprd_dma_chan(chan);
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&schan->vc.lock, flags);
	sprd_dma_stop(schan);

	vchan_get_all_descriptors(&schan->vc, &head);
	spin_unlock_irqrestore(&schan->vc.lock, flags);

	vchan_dma_desc_free_list(&schan->vc, &head);
	return 0;
}

static void sprd_dma_free_desc(struct virt_dma_desc *vd)
{
	struct sprd_dma_desc *sdesc = to_sprd_dma_desc(vd);

	kfree(sdesc);
}

static bool sprd_dma_filter_fn(struct dma_chan *chan, void *param)
{
	struct sprd_dma_chn *schan = to_sprd_dma_chan(chan);
	struct sprd_dma_dev *sdev = to_sprd_dma_dev(&schan->vc.chan);
	u32 req = *(u32 *)param;

	if (req < sdev->total_chns)
		return req == schan->chn_num + 1;
	else
		return false;
}

static int sprd_dma_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct sprd_dma_dev *sdev;
	struct sprd_dma_chn *dma_chn;
	struct resource *res;
	u32 chn_count;
	int ret, i;

	ret = device_property_read_u32(&pdev->dev, "#dma-channels", &chn_count);
	if (ret) {
		dev_err(&pdev->dev, "get dma channels count failed\n");
		return ret;
	}

	sdev = devm_kzalloc(&pdev->dev,
			    struct_size(sdev, channels, chn_count),
			    GFP_KERNEL);
	if (!sdev)
		return -ENOMEM;

	sdev->clk = devm_clk_get(&pdev->dev, "enable");
	if (IS_ERR(sdev->clk)) {
		dev_err(&pdev->dev, "get enable clock failed\n");
		return PTR_ERR(sdev->clk);
	}

	/* ashb clock is optional for AGCP DMA */
	sdev->ashb_clk = devm_clk_get(&pdev->dev, "ashb_eb");
	if (IS_ERR(sdev->ashb_clk))
		dev_warn(&pdev->dev, "no optional ashb eb clock\n");

	/*
	 * We have three DMA controllers: AP DMA, AON DMA and AGCP DMA. For AGCP
	 * DMA controller, it can or do not request the irq, which will save
	 * system power without resuming system by DMA interrupts if AGCP DMA
	 * does not request the irq. Thus the DMA interrupts property should
	 * be optional.
	 */
	sdev->irq = platform_get_irq(pdev, 0);
	if (sdev->irq > 0) {
		ret = devm_request_irq(&pdev->dev, sdev->irq, dma_irq_handle,
				       0, "sprd_dma", (void *)sdev);
		if (ret < 0) {
			dev_err(&pdev->dev, "request dma irq failed\n");
			return ret;
		}
	} else {
		dev_warn(&pdev->dev, "no interrupts for the dma controller\n");
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sdev->glb_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(sdev->glb_base))
		return PTR_ERR(sdev->glb_base);

	dma_cap_set(DMA_MEMCPY, sdev->dma_dev.cap_mask);
	sdev->total_chns = chn_count;
	sdev->dma_dev.chancnt = chn_count;
	INIT_LIST_HEAD(&sdev->dma_dev.channels);
	INIT_LIST_HEAD(&sdev->dma_dev.global_node);
	sdev->dma_dev.dev = &pdev->dev;
	sdev->dma_dev.device_alloc_chan_resources = sprd_dma_alloc_chan_resources;
	sdev->dma_dev.device_free_chan_resources = sprd_dma_free_chan_resources;
	sdev->dma_dev.device_tx_status = sprd_dma_tx_status;
	sdev->dma_dev.device_issue_pending = sprd_dma_issue_pending;
	sdev->dma_dev.device_prep_dma_memcpy = sprd_dma_prep_dma_memcpy;
	sdev->dma_dev.device_prep_slave_sg = sprd_dma_prep_slave_sg;
	sdev->dma_dev.device_config = sprd_dma_slave_config;
	sdev->dma_dev.device_pause = sprd_dma_pause;
	sdev->dma_dev.device_resume = sprd_dma_resume;
	sdev->dma_dev.device_terminate_all = sprd_dma_terminate_all;

	for (i = 0; i < chn_count; i++) {
		dma_chn = &sdev->channels[i];
		dma_chn->chn_num = i;
		dma_chn->cur_desc = NULL;
		/* get each channel's registers base address. */
		dma_chn->chn_base = sdev->glb_base + SPRD_DMA_CHN_REG_OFFSET +
				    SPRD_DMA_CHN_REG_LENGTH * i;

		dma_chn->vc.desc_free = sprd_dma_free_desc;
		vchan_init(&dma_chn->vc, &sdev->dma_dev);
	}

	platform_set_drvdata(pdev, sdev);
	ret = sprd_dma_enable(sdev);
	if (ret)
		return ret;

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0)
		goto err_rpm;

	ret = dma_async_device_register(&sdev->dma_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "register dma device failed:%d\n", ret);
		goto err_register;
	}

	sprd_dma_info.dma_cap = sdev->dma_dev.cap_mask;
	ret = of_dma_controller_register(np, of_dma_simple_xlate,
					 &sprd_dma_info);
	if (ret)
		goto err_of_register;

	pm_runtime_put(&pdev->dev);
	return 0;

err_of_register:
	dma_async_device_unregister(&sdev->dma_dev);
err_register:
	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
err_rpm:
	sprd_dma_disable(sdev);
	return ret;
}

static int sprd_dma_remove(struct platform_device *pdev)
{
	struct sprd_dma_dev *sdev = platform_get_drvdata(pdev);
	struct sprd_dma_chn *c, *cn;
	int ret;

	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0)
		return ret;

	/* explicitly free the irq */
	if (sdev->irq > 0)
		devm_free_irq(&pdev->dev, sdev->irq, sdev);

	list_for_each_entry_safe(c, cn, &sdev->dma_dev.channels,
				 vc.chan.device_node) {
		list_del(&c->vc.chan.device_node);
		tasklet_kill(&c->vc.task);
	}

	of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&sdev->dma_dev);
	sprd_dma_disable(sdev);

	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	return 0;
}

static const struct of_device_id sprd_dma_match[] = {
	{ .compatible = "sprd,sc9860-dma", },
	{},
};

static int __maybe_unused sprd_dma_runtime_suspend(struct device *dev)
{
	struct sprd_dma_dev *sdev = dev_get_drvdata(dev);

	sprd_dma_disable(sdev);
	return 0;
}

static int __maybe_unused sprd_dma_runtime_resume(struct device *dev)
{
	struct sprd_dma_dev *sdev = dev_get_drvdata(dev);
	int ret;

	ret = sprd_dma_enable(sdev);
	if (ret)
		dev_err(sdev->dma_dev.dev, "enable dma failed\n");

	return ret;
}

static const struct dev_pm_ops sprd_dma_pm_ops = {
	SET_RUNTIME_PM_OPS(sprd_dma_runtime_suspend,
			   sprd_dma_runtime_resume,
			   NULL)
};

static struct platform_driver sprd_dma_driver = {
	.probe = sprd_dma_probe,
	.remove = sprd_dma_remove,
	.driver = {
		.name = "sprd-dma",
		.of_match_table = sprd_dma_match,
		.pm = &sprd_dma_pm_ops,
	},
};
module_platform_driver(sprd_dma_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DMA driver for Spreadtrum");
MODULE_AUTHOR("Baolin Wang <baolin.wang@spreadtrum.com>");
MODULE_ALIAS("platform:sprd-dma");
