// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Ingenic JZ4780 DMA controller
 *
 * Copyright (c) 2015 Imagination Technologies
 * Author: Alex Smith <alex@alex-smith.me.uk>
 */

#include <linux/clk.h>
#include <linux/dmapool.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "dmaengine.h"
#include "virt-dma.h"

/* Global registers. */
#define JZ_DMA_REG_DMAC		0x00
#define JZ_DMA_REG_DIRQP	0x04
#define JZ_DMA_REG_DDR		0x08
#define JZ_DMA_REG_DDRS		0x0c
#define JZ_DMA_REG_DCKE		0x10
#define JZ_DMA_REG_DCKES	0x14
#define JZ_DMA_REG_DCKEC	0x18
#define JZ_DMA_REG_DMACP	0x1c
#define JZ_DMA_REG_DSIRQP	0x20
#define JZ_DMA_REG_DSIRQM	0x24
#define JZ_DMA_REG_DCIRQP	0x28
#define JZ_DMA_REG_DCIRQM	0x2c

/* Per-channel registers. */
#define JZ_DMA_REG_CHAN(n)	(n * 0x20)
#define JZ_DMA_REG_DSA		0x00
#define JZ_DMA_REG_DTA		0x04
#define JZ_DMA_REG_DTC		0x08
#define JZ_DMA_REG_DRT		0x0c
#define JZ_DMA_REG_DCS		0x10
#define JZ_DMA_REG_DCM		0x14
#define JZ_DMA_REG_DDA		0x18
#define JZ_DMA_REG_DSD		0x1c

#define JZ_DMA_DMAC_DMAE	BIT(0)
#define JZ_DMA_DMAC_AR		BIT(2)
#define JZ_DMA_DMAC_HLT		BIT(3)
#define JZ_DMA_DMAC_FAIC	BIT(27)
#define JZ_DMA_DMAC_FMSC	BIT(31)

#define JZ_DMA_DRT_AUTO		0x8

#define JZ_DMA_DCS_CTE		BIT(0)
#define JZ_DMA_DCS_HLT		BIT(2)
#define JZ_DMA_DCS_TT		BIT(3)
#define JZ_DMA_DCS_AR		BIT(4)
#define JZ_DMA_DCS_DES8		BIT(30)

#define JZ_DMA_DCM_LINK		BIT(0)
#define JZ_DMA_DCM_TIE		BIT(1)
#define JZ_DMA_DCM_STDE		BIT(2)
#define JZ_DMA_DCM_TSZ_SHIFT	8
#define JZ_DMA_DCM_TSZ_MASK	(0x7 << JZ_DMA_DCM_TSZ_SHIFT)
#define JZ_DMA_DCM_DP_SHIFT	12
#define JZ_DMA_DCM_SP_SHIFT	14
#define JZ_DMA_DCM_DAI		BIT(22)
#define JZ_DMA_DCM_SAI		BIT(23)

#define JZ_DMA_SIZE_4_BYTE	0x0
#define JZ_DMA_SIZE_1_BYTE	0x1
#define JZ_DMA_SIZE_2_BYTE	0x2
#define JZ_DMA_SIZE_16_BYTE	0x3
#define JZ_DMA_SIZE_32_BYTE	0x4
#define JZ_DMA_SIZE_64_BYTE	0x5
#define JZ_DMA_SIZE_128_BYTE	0x6

#define JZ_DMA_WIDTH_32_BIT	0x0
#define JZ_DMA_WIDTH_8_BIT	0x1
#define JZ_DMA_WIDTH_16_BIT	0x2

#define JZ_DMA_BUSWIDTHS	(BIT(DMA_SLAVE_BUSWIDTH_1_BYTE)	 | \
				 BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) | \
				 BIT(DMA_SLAVE_BUSWIDTH_4_BYTES))

#define JZ4780_DMA_CTRL_OFFSET	0x1000

/* macros for use with jz4780_dma_soc_data.flags */
#define JZ_SOC_DATA_ALLOW_LEGACY_DT	BIT(0)
#define JZ_SOC_DATA_PROGRAMMABLE_DMA	BIT(1)
#define JZ_SOC_DATA_PER_CHAN_PM		BIT(2)
#define JZ_SOC_DATA_NO_DCKES_DCKEC	BIT(3)
#define JZ_SOC_DATA_BREAK_LINKS		BIT(4)

/**
 * struct jz4780_dma_hwdesc - descriptor structure read by the DMA controller.
 * @dcm: value for the DCM (channel command) register
 * @dsa: source address
 * @dta: target address
 * @dtc: transfer count (number of blocks of the transfer size specified in DCM
 * to transfer) in the low 24 bits, offset of the next descriptor from the
 * descriptor base address in the upper 8 bits.
 */
struct jz4780_dma_hwdesc {
	uint32_t dcm;
	uint32_t dsa;
	uint32_t dta;
	uint32_t dtc;
};

/* Size of allocations for hardware descriptor blocks. */
#define JZ_DMA_DESC_BLOCK_SIZE	PAGE_SIZE
#define JZ_DMA_MAX_DESC		\
	(JZ_DMA_DESC_BLOCK_SIZE / sizeof(struct jz4780_dma_hwdesc))

struct jz4780_dma_desc {
	struct virt_dma_desc vdesc;

	struct jz4780_dma_hwdesc *desc;
	dma_addr_t desc_phys;
	unsigned int count;
	enum dma_transaction_type type;
	uint32_t status;
};

struct jz4780_dma_chan {
	struct virt_dma_chan vchan;
	unsigned int id;
	struct dma_pool *desc_pool;

	uint32_t transfer_type;
	uint32_t transfer_shift;
	struct dma_slave_config	config;

	struct jz4780_dma_desc *desc;
	unsigned int curr_hwdesc;
};

struct jz4780_dma_soc_data {
	unsigned int nb_channels;
	unsigned int transfer_ord_max;
	unsigned long flags;
};

struct jz4780_dma_dev {
	struct dma_device dma_device;
	void __iomem *chn_base;
	void __iomem *ctrl_base;
	struct clk *clk;
	unsigned int irq;
	const struct jz4780_dma_soc_data *soc_data;

	uint32_t chan_reserved;
	struct jz4780_dma_chan chan[];
};

struct jz4780_dma_filter_data {
	uint32_t transfer_type;
	int channel;
};

static inline struct jz4780_dma_chan *to_jz4780_dma_chan(struct dma_chan *chan)
{
	return container_of(chan, struct jz4780_dma_chan, vchan.chan);
}

static inline struct jz4780_dma_desc *to_jz4780_dma_desc(
	struct virt_dma_desc *vdesc)
{
	return container_of(vdesc, struct jz4780_dma_desc, vdesc);
}

static inline struct jz4780_dma_dev *jz4780_dma_chan_parent(
	struct jz4780_dma_chan *jzchan)
{
	return container_of(jzchan->vchan.chan.device, struct jz4780_dma_dev,
			    dma_device);
}

static inline uint32_t jz4780_dma_chn_readl(struct jz4780_dma_dev *jzdma,
	unsigned int chn, unsigned int reg)
{
	return readl(jzdma->chn_base + reg + JZ_DMA_REG_CHAN(chn));
}

static inline void jz4780_dma_chn_writel(struct jz4780_dma_dev *jzdma,
	unsigned int chn, unsigned int reg, uint32_t val)
{
	writel(val, jzdma->chn_base + reg + JZ_DMA_REG_CHAN(chn));
}

static inline uint32_t jz4780_dma_ctrl_readl(struct jz4780_dma_dev *jzdma,
	unsigned int reg)
{
	return readl(jzdma->ctrl_base + reg);
}

static inline void jz4780_dma_ctrl_writel(struct jz4780_dma_dev *jzdma,
	unsigned int reg, uint32_t val)
{
	writel(val, jzdma->ctrl_base + reg);
}

static inline void jz4780_dma_chan_enable(struct jz4780_dma_dev *jzdma,
	unsigned int chn)
{
	if (jzdma->soc_data->flags & JZ_SOC_DATA_PER_CHAN_PM) {
		unsigned int reg;

		if (jzdma->soc_data->flags & JZ_SOC_DATA_NO_DCKES_DCKEC)
			reg = JZ_DMA_REG_DCKE;
		else
			reg = JZ_DMA_REG_DCKES;

		jz4780_dma_ctrl_writel(jzdma, reg, BIT(chn));
	}
}

static inline void jz4780_dma_chan_disable(struct jz4780_dma_dev *jzdma,
	unsigned int chn)
{
	if ((jzdma->soc_data->flags & JZ_SOC_DATA_PER_CHAN_PM) &&
			!(jzdma->soc_data->flags & JZ_SOC_DATA_NO_DCKES_DCKEC))
		jz4780_dma_ctrl_writel(jzdma, JZ_DMA_REG_DCKEC, BIT(chn));
}

static struct jz4780_dma_desc *jz4780_dma_desc_alloc(
	struct jz4780_dma_chan *jzchan, unsigned int count,
	enum dma_transaction_type type)
{
	struct jz4780_dma_desc *desc;

	if (count > JZ_DMA_MAX_DESC)
		return NULL;

	desc = kzalloc(sizeof(*desc), GFP_NOWAIT);
	if (!desc)
		return NULL;

	desc->desc = dma_pool_alloc(jzchan->desc_pool, GFP_NOWAIT,
				    &desc->desc_phys);
	if (!desc->desc) {
		kfree(desc);
		return NULL;
	}

	desc->count = count;
	desc->type = type;
	return desc;
}

static void jz4780_dma_desc_free(struct virt_dma_desc *vdesc)
{
	struct jz4780_dma_desc *desc = to_jz4780_dma_desc(vdesc);
	struct jz4780_dma_chan *jzchan = to_jz4780_dma_chan(vdesc->tx.chan);

	dma_pool_free(jzchan->desc_pool, desc->desc, desc->desc_phys);
	kfree(desc);
}

static uint32_t jz4780_dma_transfer_size(struct jz4780_dma_chan *jzchan,
	unsigned long val, uint32_t *shift)
{
	struct jz4780_dma_dev *jzdma = jz4780_dma_chan_parent(jzchan);
	int ord = ffs(val) - 1;

	/*
	 * 8 byte transfer sizes unsupported so fall back on 4. If it's larger
	 * than the maximum, just limit it. It is perfectly safe to fall back
	 * in this way since we won't exceed the maximum burst size supported
	 * by the device, the only effect is reduced efficiency. This is better
	 * than refusing to perform the request at all.
	 */
	if (ord == 3)
		ord = 2;
	else if (ord > jzdma->soc_data->transfer_ord_max)
		ord = jzdma->soc_data->transfer_ord_max;

	*shift = ord;

	switch (ord) {
	case 0:
		return JZ_DMA_SIZE_1_BYTE;
	case 1:
		return JZ_DMA_SIZE_2_BYTE;
	case 2:
		return JZ_DMA_SIZE_4_BYTE;
	case 4:
		return JZ_DMA_SIZE_16_BYTE;
	case 5:
		return JZ_DMA_SIZE_32_BYTE;
	case 6:
		return JZ_DMA_SIZE_64_BYTE;
	default:
		return JZ_DMA_SIZE_128_BYTE;
	}
}

static int jz4780_dma_setup_hwdesc(struct jz4780_dma_chan *jzchan,
	struct jz4780_dma_hwdesc *desc, dma_addr_t addr, size_t len,
	enum dma_transfer_direction direction)
{
	struct dma_slave_config *config = &jzchan->config;
	uint32_t width, maxburst, tsz;

	if (direction == DMA_MEM_TO_DEV) {
		desc->dcm = JZ_DMA_DCM_SAI;
		desc->dsa = addr;
		desc->dta = config->dst_addr;

		width = config->dst_addr_width;
		maxburst = config->dst_maxburst;
	} else {
		desc->dcm = JZ_DMA_DCM_DAI;
		desc->dsa = config->src_addr;
		desc->dta = addr;

		width = config->src_addr_width;
		maxburst = config->src_maxburst;
	}

	/*
	 * This calculates the maximum transfer size that can be used with the
	 * given address, length, width and maximum burst size. The address
	 * must be aligned to the transfer size, the total length must be
	 * divisible by the transfer size, and we must not use more than the
	 * maximum burst specified by the user.
	 */
	tsz = jz4780_dma_transfer_size(jzchan, addr | len | (width * maxburst),
				       &jzchan->transfer_shift);

	switch (width) {
	case DMA_SLAVE_BUSWIDTH_1_BYTE:
	case DMA_SLAVE_BUSWIDTH_2_BYTES:
		break;
	case DMA_SLAVE_BUSWIDTH_4_BYTES:
		width = JZ_DMA_WIDTH_32_BIT;
		break;
	default:
		return -EINVAL;
	}

	desc->dcm |= tsz << JZ_DMA_DCM_TSZ_SHIFT;
	desc->dcm |= width << JZ_DMA_DCM_SP_SHIFT;
	desc->dcm |= width << JZ_DMA_DCM_DP_SHIFT;

	desc->dtc = len >> jzchan->transfer_shift;
	return 0;
}

static struct dma_async_tx_descriptor *jz4780_dma_prep_slave_sg(
	struct dma_chan *chan, struct scatterlist *sgl, unsigned int sg_len,
	enum dma_transfer_direction direction, unsigned long flags,
	void *context)
{
	struct jz4780_dma_chan *jzchan = to_jz4780_dma_chan(chan);
	struct jz4780_dma_dev *jzdma = jz4780_dma_chan_parent(jzchan);
	struct jz4780_dma_desc *desc;
	unsigned int i;
	int err;

	desc = jz4780_dma_desc_alloc(jzchan, sg_len, DMA_SLAVE);
	if (!desc)
		return NULL;

	for (i = 0; i < sg_len; i++) {
		err = jz4780_dma_setup_hwdesc(jzchan, &desc->desc[i],
					      sg_dma_address(&sgl[i]),
					      sg_dma_len(&sgl[i]),
					      direction);
		if (err < 0) {
			jz4780_dma_desc_free(&jzchan->desc->vdesc);
			return NULL;
		}

		desc->desc[i].dcm |= JZ_DMA_DCM_TIE;

		if (i != (sg_len - 1) &&
		    !(jzdma->soc_data->flags & JZ_SOC_DATA_BREAK_LINKS)) {
			/* Automatically proceeed to the next descriptor. */
			desc->desc[i].dcm |= JZ_DMA_DCM_LINK;

			/*
			 * The upper 8 bits of the DTC field in the descriptor
			 * must be set to (offset from descriptor base of next
			 * descriptor >> 4).
			 */
			desc->desc[i].dtc |=
				(((i + 1) * sizeof(*desc->desc)) >> 4) << 24;
		}
	}

	return vchan_tx_prep(&jzchan->vchan, &desc->vdesc, flags);
}

static struct dma_async_tx_descriptor *jz4780_dma_prep_dma_cyclic(
	struct dma_chan *chan, dma_addr_t buf_addr, size_t buf_len,
	size_t period_len, enum dma_transfer_direction direction,
	unsigned long flags)
{
	struct jz4780_dma_chan *jzchan = to_jz4780_dma_chan(chan);
	struct jz4780_dma_desc *desc;
	unsigned int periods, i;
	int err;

	if (buf_len % period_len)
		return NULL;

	periods = buf_len / period_len;

	desc = jz4780_dma_desc_alloc(jzchan, periods, DMA_CYCLIC);
	if (!desc)
		return NULL;

	for (i = 0; i < periods; i++) {
		err = jz4780_dma_setup_hwdesc(jzchan, &desc->desc[i], buf_addr,
					      period_len, direction);
		if (err < 0) {
			jz4780_dma_desc_free(&jzchan->desc->vdesc);
			return NULL;
		}

		buf_addr += period_len;

		/*
		 * Set the link bit to indicate that the controller should
		 * automatically proceed to the next descriptor. In
		 * jz4780_dma_begin(), this will be cleared if we need to issue
		 * an interrupt after each period.
		 */
		desc->desc[i].dcm |= JZ_DMA_DCM_TIE | JZ_DMA_DCM_LINK;

		/*
		 * The upper 8 bits of the DTC field in the descriptor must be
		 * set to (offset from descriptor base of next descriptor >> 4).
		 * If this is the last descriptor, link it back to the first,
		 * i.e. leave offset set to 0, otherwise point to the next one.
		 */
		if (i != (periods - 1)) {
			desc->desc[i].dtc |=
				(((i + 1) * sizeof(*desc->desc)) >> 4) << 24;
		}
	}

	return vchan_tx_prep(&jzchan->vchan, &desc->vdesc, flags);
}

static struct dma_async_tx_descriptor *jz4780_dma_prep_dma_memcpy(
	struct dma_chan *chan, dma_addr_t dest, dma_addr_t src,
	size_t len, unsigned long flags)
{
	struct jz4780_dma_chan *jzchan = to_jz4780_dma_chan(chan);
	struct jz4780_dma_desc *desc;
	uint32_t tsz;

	desc = jz4780_dma_desc_alloc(jzchan, 1, DMA_MEMCPY);
	if (!desc)
		return NULL;

	tsz = jz4780_dma_transfer_size(jzchan, dest | src | len,
				       &jzchan->transfer_shift);

	jzchan->transfer_type = JZ_DMA_DRT_AUTO;

	desc->desc[0].dsa = src;
	desc->desc[0].dta = dest;
	desc->desc[0].dcm = JZ_DMA_DCM_TIE | JZ_DMA_DCM_SAI | JZ_DMA_DCM_DAI |
			    tsz << JZ_DMA_DCM_TSZ_SHIFT |
			    JZ_DMA_WIDTH_32_BIT << JZ_DMA_DCM_SP_SHIFT |
			    JZ_DMA_WIDTH_32_BIT << JZ_DMA_DCM_DP_SHIFT;
	desc->desc[0].dtc = len >> jzchan->transfer_shift;

	return vchan_tx_prep(&jzchan->vchan, &desc->vdesc, flags);
}

static void jz4780_dma_begin(struct jz4780_dma_chan *jzchan)
{
	struct jz4780_dma_dev *jzdma = jz4780_dma_chan_parent(jzchan);
	struct virt_dma_desc *vdesc;
	unsigned int i;
	dma_addr_t desc_phys;

	if (!jzchan->desc) {
		vdesc = vchan_next_desc(&jzchan->vchan);
		if (!vdesc)
			return;

		list_del(&vdesc->node);

		jzchan->desc = to_jz4780_dma_desc(vdesc);
		jzchan->curr_hwdesc = 0;

		if (jzchan->desc->type == DMA_CYCLIC && vdesc->tx.callback) {
			/*
			 * The DMA controller doesn't support triggering an
			 * interrupt after processing each descriptor, only
			 * after processing an entire terminated list of
			 * descriptors. For a cyclic DMA setup the list of
			 * descriptors is not terminated so we can never get an
			 * interrupt.
			 *
			 * If the user requested a callback for a cyclic DMA
			 * setup then we workaround this hardware limitation
			 * here by degrading to a set of unlinked descriptors
			 * which we will submit in sequence in response to the
			 * completion of processing the previous descriptor.
			 */
			for (i = 0; i < jzchan->desc->count; i++)
				jzchan->desc->desc[i].dcm &= ~JZ_DMA_DCM_LINK;
		}
	} else {
		/*
		 * There is an existing transfer, therefore this must be one
		 * for which we unlinked the descriptors above. Advance to the
		 * next one in the list.
		 */
		jzchan->curr_hwdesc =
			(jzchan->curr_hwdesc + 1) % jzchan->desc->count;
	}

	/* Enable the channel's clock. */
	jz4780_dma_chan_enable(jzdma, jzchan->id);

	/* Use 4-word descriptors. */
	jz4780_dma_chn_writel(jzdma, jzchan->id, JZ_DMA_REG_DCS, 0);

	/* Set transfer type. */
	jz4780_dma_chn_writel(jzdma, jzchan->id, JZ_DMA_REG_DRT,
			      jzchan->transfer_type);

	/*
	 * Set the transfer count. This is redundant for a descriptor-driven
	 * transfer. However, there can be a delay between the transfer start
	 * time and when DTCn reg contains the new transfer count. Setting
	 * it explicitly ensures residue is computed correctly at all times.
	 */
	jz4780_dma_chn_writel(jzdma, jzchan->id, JZ_DMA_REG_DTC,
				jzchan->desc->desc[jzchan->curr_hwdesc].dtc);

	/* Write descriptor address and initiate descriptor fetch. */
	desc_phys = jzchan->desc->desc_phys +
		    (jzchan->curr_hwdesc * sizeof(*jzchan->desc->desc));
	jz4780_dma_chn_writel(jzdma, jzchan->id, JZ_DMA_REG_DDA, desc_phys);
	jz4780_dma_ctrl_writel(jzdma, JZ_DMA_REG_DDRS, BIT(jzchan->id));

	/* Enable the channel. */
	jz4780_dma_chn_writel(jzdma, jzchan->id, JZ_DMA_REG_DCS,
			      JZ_DMA_DCS_CTE);
}

static void jz4780_dma_issue_pending(struct dma_chan *chan)
{
	struct jz4780_dma_chan *jzchan = to_jz4780_dma_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&jzchan->vchan.lock, flags);

	if (vchan_issue_pending(&jzchan->vchan) && !jzchan->desc)
		jz4780_dma_begin(jzchan);

	spin_unlock_irqrestore(&jzchan->vchan.lock, flags);
}

static int jz4780_dma_terminate_all(struct dma_chan *chan)
{
	struct jz4780_dma_chan *jzchan = to_jz4780_dma_chan(chan);
	struct jz4780_dma_dev *jzdma = jz4780_dma_chan_parent(jzchan);
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&jzchan->vchan.lock, flags);

	/* Clear the DMA status and stop the transfer. */
	jz4780_dma_chn_writel(jzdma, jzchan->id, JZ_DMA_REG_DCS, 0);
	if (jzchan->desc) {
		vchan_terminate_vdesc(&jzchan->desc->vdesc);
		jzchan->desc = NULL;
	}

	jz4780_dma_chan_disable(jzdma, jzchan->id);

	vchan_get_all_descriptors(&jzchan->vchan, &head);

	spin_unlock_irqrestore(&jzchan->vchan.lock, flags);

	vchan_dma_desc_free_list(&jzchan->vchan, &head);
	return 0;
}

static void jz4780_dma_synchronize(struct dma_chan *chan)
{
	struct jz4780_dma_chan *jzchan = to_jz4780_dma_chan(chan);
	struct jz4780_dma_dev *jzdma = jz4780_dma_chan_parent(jzchan);

	vchan_synchronize(&jzchan->vchan);
	jz4780_dma_chan_disable(jzdma, jzchan->id);
}

static int jz4780_dma_config(struct dma_chan *chan,
	struct dma_slave_config *config)
{
	struct jz4780_dma_chan *jzchan = to_jz4780_dma_chan(chan);

	if ((config->src_addr_width == DMA_SLAVE_BUSWIDTH_8_BYTES)
	   || (config->dst_addr_width == DMA_SLAVE_BUSWIDTH_8_BYTES))
		return -EINVAL;

	/* Copy the reset of the slave configuration, it is used later. */
	memcpy(&jzchan->config, config, sizeof(jzchan->config));

	return 0;
}

static size_t jz4780_dma_desc_residue(struct jz4780_dma_chan *jzchan,
	struct jz4780_dma_desc *desc, unsigned int next_sg)
{
	struct jz4780_dma_dev *jzdma = jz4780_dma_chan_parent(jzchan);
	unsigned int count = 0;
	unsigned int i;

	for (i = next_sg; i < desc->count; i++)
		count += desc->desc[i].dtc & GENMASK(23, 0);

	if (next_sg != 0)
		count += jz4780_dma_chn_readl(jzdma, jzchan->id,
					 JZ_DMA_REG_DTC);

	return count << jzchan->transfer_shift;
}

static enum dma_status jz4780_dma_tx_status(struct dma_chan *chan,
	dma_cookie_t cookie, struct dma_tx_state *txstate)
{
	struct jz4780_dma_chan *jzchan = to_jz4780_dma_chan(chan);
	struct virt_dma_desc *vdesc;
	enum dma_status status;
	unsigned long flags;
	unsigned long residue = 0;

	status = dma_cookie_status(chan, cookie, txstate);
	if ((status == DMA_COMPLETE) || (txstate == NULL))
		return status;

	spin_lock_irqsave(&jzchan->vchan.lock, flags);

	vdesc = vchan_find_desc(&jzchan->vchan, cookie);
	if (vdesc) {
		/* On the issued list, so hasn't been processed yet */
		residue = jz4780_dma_desc_residue(jzchan,
					to_jz4780_dma_desc(vdesc), 0);
	} else if (cookie == jzchan->desc->vdesc.tx.cookie) {
		residue = jz4780_dma_desc_residue(jzchan, jzchan->desc,
					jzchan->curr_hwdesc + 1);
	}
	dma_set_residue(txstate, residue);

	if (vdesc && jzchan->desc && vdesc == &jzchan->desc->vdesc
	    && jzchan->desc->status & (JZ_DMA_DCS_AR | JZ_DMA_DCS_HLT))
		status = DMA_ERROR;

	spin_unlock_irqrestore(&jzchan->vchan.lock, flags);
	return status;
}

static bool jz4780_dma_chan_irq(struct jz4780_dma_dev *jzdma,
				struct jz4780_dma_chan *jzchan)
{
	const unsigned int soc_flags = jzdma->soc_data->flags;
	struct jz4780_dma_desc *desc = jzchan->desc;
	uint32_t dcs;
	bool ack = true;

	spin_lock(&jzchan->vchan.lock);

	dcs = jz4780_dma_chn_readl(jzdma, jzchan->id, JZ_DMA_REG_DCS);
	jz4780_dma_chn_writel(jzdma, jzchan->id, JZ_DMA_REG_DCS, 0);

	if (dcs & JZ_DMA_DCS_AR) {
		dev_warn(&jzchan->vchan.chan.dev->device,
			 "address error (DCS=0x%x)\n", dcs);
	}

	if (dcs & JZ_DMA_DCS_HLT) {
		dev_warn(&jzchan->vchan.chan.dev->device,
			 "channel halt (DCS=0x%x)\n", dcs);
	}

	if (jzchan->desc) {
		jzchan->desc->status = dcs;

		if ((dcs & (JZ_DMA_DCS_AR | JZ_DMA_DCS_HLT)) == 0) {
			if (jzchan->desc->type == DMA_CYCLIC) {
				vchan_cyclic_callback(&jzchan->desc->vdesc);

				jz4780_dma_begin(jzchan);
			} else if (dcs & JZ_DMA_DCS_TT) {
				if (!(soc_flags & JZ_SOC_DATA_BREAK_LINKS) ||
				    (jzchan->curr_hwdesc + 1 == desc->count)) {
					vchan_cookie_complete(&desc->vdesc);
					jzchan->desc = NULL;
				}

				jz4780_dma_begin(jzchan);
			} else {
				/* False positive - continue the transfer */
				ack = false;
				jz4780_dma_chn_writel(jzdma, jzchan->id,
						      JZ_DMA_REG_DCS,
						      JZ_DMA_DCS_CTE);
			}
		}
	} else {
		dev_err(&jzchan->vchan.chan.dev->device,
			"channel IRQ with no active transfer\n");
	}

	spin_unlock(&jzchan->vchan.lock);

	return ack;
}

static irqreturn_t jz4780_dma_irq_handler(int irq, void *data)
{
	struct jz4780_dma_dev *jzdma = data;
	unsigned int nb_channels = jzdma->soc_data->nb_channels;
	unsigned long pending;
	uint32_t dmac;
	int i;

	pending = jz4780_dma_ctrl_readl(jzdma, JZ_DMA_REG_DIRQP);

	for_each_set_bit(i, &pending, nb_channels) {
		if (jz4780_dma_chan_irq(jzdma, &jzdma->chan[i]))
			pending &= ~BIT(i);
	}

	/* Clear halt and address error status of all channels. */
	dmac = jz4780_dma_ctrl_readl(jzdma, JZ_DMA_REG_DMAC);
	dmac &= ~(JZ_DMA_DMAC_HLT | JZ_DMA_DMAC_AR);
	jz4780_dma_ctrl_writel(jzdma, JZ_DMA_REG_DMAC, dmac);

	/* Clear interrupt pending status. */
	jz4780_dma_ctrl_writel(jzdma, JZ_DMA_REG_DIRQP, pending);

	return IRQ_HANDLED;
}

static int jz4780_dma_alloc_chan_resources(struct dma_chan *chan)
{
	struct jz4780_dma_chan *jzchan = to_jz4780_dma_chan(chan);

	jzchan->desc_pool = dma_pool_create(dev_name(&chan->dev->device),
					    chan->device->dev,
					    JZ_DMA_DESC_BLOCK_SIZE,
					    PAGE_SIZE, 0);
	if (!jzchan->desc_pool) {
		dev_err(&chan->dev->device,
			"failed to allocate descriptor pool\n");
		return -ENOMEM;
	}

	return 0;
}

static void jz4780_dma_free_chan_resources(struct dma_chan *chan)
{
	struct jz4780_dma_chan *jzchan = to_jz4780_dma_chan(chan);

	vchan_free_chan_resources(&jzchan->vchan);
	dma_pool_destroy(jzchan->desc_pool);
	jzchan->desc_pool = NULL;
}

static bool jz4780_dma_filter_fn(struct dma_chan *chan, void *param)
{
	struct jz4780_dma_chan *jzchan = to_jz4780_dma_chan(chan);
	struct jz4780_dma_dev *jzdma = jz4780_dma_chan_parent(jzchan);
	struct jz4780_dma_filter_data *data = param;


	if (data->channel > -1) {
		if (data->channel != jzchan->id)
			return false;
	} else if (jzdma->chan_reserved & BIT(jzchan->id)) {
		return false;
	}

	jzchan->transfer_type = data->transfer_type;

	return true;
}

static struct dma_chan *jz4780_of_dma_xlate(struct of_phandle_args *dma_spec,
	struct of_dma *ofdma)
{
	struct jz4780_dma_dev *jzdma = ofdma->of_dma_data;
	dma_cap_mask_t mask = jzdma->dma_device.cap_mask;
	struct jz4780_dma_filter_data data;

	if (dma_spec->args_count != 2)
		return NULL;

	data.transfer_type = dma_spec->args[0];
	data.channel = dma_spec->args[1];

	if (data.channel > -1) {
		if (data.channel >= jzdma->soc_data->nb_channels) {
			dev_err(jzdma->dma_device.dev,
				"device requested non-existent channel %u\n",
				data.channel);
			return NULL;
		}

		/* Can only select a channel marked as reserved. */
		if (!(jzdma->chan_reserved & BIT(data.channel))) {
			dev_err(jzdma->dma_device.dev,
				"device requested unreserved channel %u\n",
				data.channel);
			return NULL;
		}

		jzdma->chan[data.channel].transfer_type = data.transfer_type;

		return dma_get_slave_channel(
			&jzdma->chan[data.channel].vchan.chan);
	} else {
		return __dma_request_channel(&mask, jz4780_dma_filter_fn, &data,
					     ofdma->of_node);
	}
}

static int jz4780_dma_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct jz4780_dma_soc_data *soc_data;
	struct jz4780_dma_dev *jzdma;
	struct jz4780_dma_chan *jzchan;
	struct dma_device *dd;
	struct resource *res;
	int i, ret;

	if (!dev->of_node) {
		dev_err(dev, "This driver must be probed from devicetree\n");
		return -EINVAL;
	}

	soc_data = device_get_match_data(dev);
	if (!soc_data)
		return -EINVAL;

	jzdma = devm_kzalloc(dev, struct_size(jzdma, chan,
			     soc_data->nb_channels), GFP_KERNEL);
	if (!jzdma)
		return -ENOMEM;

	jzdma->soc_data = soc_data;
	platform_set_drvdata(pdev, jzdma);

	jzdma->chn_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(jzdma->chn_base))
		return PTR_ERR(jzdma->chn_base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res) {
		jzdma->ctrl_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(jzdma->ctrl_base))
			return PTR_ERR(jzdma->ctrl_base);
	} else if (soc_data->flags & JZ_SOC_DATA_ALLOW_LEGACY_DT) {
		/*
		 * On JZ4780, if the second memory resource was not supplied,
		 * assume we're using an old devicetree, and calculate the
		 * offset to the control registers.
		 */
		jzdma->ctrl_base = jzdma->chn_base + JZ4780_DMA_CTRL_OFFSET;
	} else {
		dev_err(dev, "failed to get I/O memory\n");
		return -EINVAL;
	}

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;

	jzdma->irq = ret;

	ret = request_irq(jzdma->irq, jz4780_dma_irq_handler, 0, dev_name(dev),
			  jzdma);
	if (ret) {
		dev_err(dev, "failed to request IRQ %u!\n", jzdma->irq);
		return ret;
	}

	jzdma->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(jzdma->clk)) {
		dev_err(dev, "failed to get clock\n");
		ret = PTR_ERR(jzdma->clk);
		goto err_free_irq;
	}

	clk_prepare_enable(jzdma->clk);

	/* Property is optional, if it doesn't exist the value will remain 0. */
	of_property_read_u32_index(dev->of_node, "ingenic,reserved-channels",
				   0, &jzdma->chan_reserved);

	dd = &jzdma->dma_device;

	dma_cap_set(DMA_MEMCPY, dd->cap_mask);
	dma_cap_set(DMA_SLAVE, dd->cap_mask);
	dma_cap_set(DMA_CYCLIC, dd->cap_mask);

	dd->dev = dev;
	dd->copy_align = DMAENGINE_ALIGN_4_BYTES;
	dd->device_alloc_chan_resources = jz4780_dma_alloc_chan_resources;
	dd->device_free_chan_resources = jz4780_dma_free_chan_resources;
	dd->device_prep_slave_sg = jz4780_dma_prep_slave_sg;
	dd->device_prep_dma_cyclic = jz4780_dma_prep_dma_cyclic;
	dd->device_prep_dma_memcpy = jz4780_dma_prep_dma_memcpy;
	dd->device_config = jz4780_dma_config;
	dd->device_terminate_all = jz4780_dma_terminate_all;
	dd->device_synchronize = jz4780_dma_synchronize;
	dd->device_tx_status = jz4780_dma_tx_status;
	dd->device_issue_pending = jz4780_dma_issue_pending;
	dd->src_addr_widths = JZ_DMA_BUSWIDTHS;
	dd->dst_addr_widths = JZ_DMA_BUSWIDTHS;
	dd->directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	dd->residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;

	/*
	 * Enable DMA controller, mark all channels as not programmable.
	 * Also set the FMSC bit - it increases MSC performance, so it makes
	 * little sense not to enable it.
	 */
	jz4780_dma_ctrl_writel(jzdma, JZ_DMA_REG_DMAC, JZ_DMA_DMAC_DMAE |
			       JZ_DMA_DMAC_FAIC | JZ_DMA_DMAC_FMSC);

	if (soc_data->flags & JZ_SOC_DATA_PROGRAMMABLE_DMA)
		jz4780_dma_ctrl_writel(jzdma, JZ_DMA_REG_DMACP, 0);

	INIT_LIST_HEAD(&dd->channels);

	for (i = 0; i < soc_data->nb_channels; i++) {
		jzchan = &jzdma->chan[i];
		jzchan->id = i;

		vchan_init(&jzchan->vchan, dd);
		jzchan->vchan.desc_free = jz4780_dma_desc_free;
	}

	ret = dmaenginem_async_device_register(dd);
	if (ret) {
		dev_err(dev, "failed to register device\n");
		goto err_disable_clk;
	}

	/* Register with OF DMA helpers. */
	ret = of_dma_controller_register(dev->of_node, jz4780_of_dma_xlate,
					 jzdma);
	if (ret) {
		dev_err(dev, "failed to register OF DMA controller\n");
		goto err_disable_clk;
	}

	dev_info(dev, "JZ4780 DMA controller initialised\n");
	return 0;

err_disable_clk:
	clk_disable_unprepare(jzdma->clk);

err_free_irq:
	free_irq(jzdma->irq, jzdma);
	return ret;
}

static int jz4780_dma_remove(struct platform_device *pdev)
{
	struct jz4780_dma_dev *jzdma = platform_get_drvdata(pdev);
	int i;

	of_dma_controller_free(pdev->dev.of_node);

	clk_disable_unprepare(jzdma->clk);
	free_irq(jzdma->irq, jzdma);

	for (i = 0; i < jzdma->soc_data->nb_channels; i++)
		tasklet_kill(&jzdma->chan[i].vchan.task);

	return 0;
}

static const struct jz4780_dma_soc_data jz4740_dma_soc_data = {
	.nb_channels = 6,
	.transfer_ord_max = 5,
	.flags = JZ_SOC_DATA_BREAK_LINKS,
};

static const struct jz4780_dma_soc_data jz4725b_dma_soc_data = {
	.nb_channels = 6,
	.transfer_ord_max = 5,
	.flags = JZ_SOC_DATA_PER_CHAN_PM | JZ_SOC_DATA_NO_DCKES_DCKEC |
		 JZ_SOC_DATA_BREAK_LINKS,
};

static const struct jz4780_dma_soc_data jz4770_dma_soc_data = {
	.nb_channels = 6,
	.transfer_ord_max = 6,
	.flags = JZ_SOC_DATA_PER_CHAN_PM,
};

static const struct jz4780_dma_soc_data jz4780_dma_soc_data = {
	.nb_channels = 32,
	.transfer_ord_max = 7,
	.flags = JZ_SOC_DATA_ALLOW_LEGACY_DT | JZ_SOC_DATA_PROGRAMMABLE_DMA,
};

static const struct jz4780_dma_soc_data x1000_dma_soc_data = {
	.nb_channels = 8,
	.transfer_ord_max = 7,
	.flags = JZ_SOC_DATA_PROGRAMMABLE_DMA,
};

static const struct of_device_id jz4780_dma_dt_match[] = {
	{ .compatible = "ingenic,jz4740-dma", .data = &jz4740_dma_soc_data },
	{ .compatible = "ingenic,jz4725b-dma", .data = &jz4725b_dma_soc_data },
	{ .compatible = "ingenic,jz4770-dma", .data = &jz4770_dma_soc_data },
	{ .compatible = "ingenic,jz4780-dma", .data = &jz4780_dma_soc_data },
	{ .compatible = "ingenic,x1000-dma", .data = &x1000_dma_soc_data },
	{},
};
MODULE_DEVICE_TABLE(of, jz4780_dma_dt_match);

static struct platform_driver jz4780_dma_driver = {
	.probe		= jz4780_dma_probe,
	.remove		= jz4780_dma_remove,
	.driver	= {
		.name	= "jz4780-dma",
		.of_match_table = of_match_ptr(jz4780_dma_dt_match),
	},
};

static int __init jz4780_dma_init(void)
{
	return platform_driver_register(&jz4780_dma_driver);
}
subsys_initcall(jz4780_dma_init);

static void __exit jz4780_dma_exit(void)
{
	platform_driver_unregister(&jz4780_dma_driver);
}
module_exit(jz4780_dma_exit);

MODULE_AUTHOR("Alex Smith <alex@alex-smith.me.uk>");
MODULE_DESCRIPTION("Ingenic JZ4780 DMA controller driver");
MODULE_LICENSE("GPL");
