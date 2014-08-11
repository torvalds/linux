/*
 * TI EDMA DMA engine driver
 *
 * Copyright 2012 Texas Instruments
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/of.h>

#include <linux/platform_data/edma.h>

#include "dmaengine.h"
#include "virt-dma.h"

/*
 * This will go away when the private EDMA API is folded
 * into this driver and the platform device(s) are
 * instantiated in the arch code. We can only get away
 * with this simplification because DA8XX may not be built
 * in the same kernel image with other DaVinci parts. This
 * avoids having to sprinkle dmaengine driver platform devices
 * and data throughout all the existing board files.
 */
#ifdef CONFIG_ARCH_DAVINCI_DA8XX
#define EDMA_CTLRS	2
#define EDMA_CHANS	32
#else
#define EDMA_CTLRS	1
#define EDMA_CHANS	64
#endif /* CONFIG_ARCH_DAVINCI_DA8XX */

/*
 * Max of 20 segments per channel to conserve PaRAM slots
 * Also note that MAX_NR_SG should be atleast the no.of periods
 * that are required for ASoC, otherwise DMA prep calls will
 * fail. Today davinci-pcm is the only user of this driver and
 * requires atleast 17 slots, so we setup the default to 20.
 */
#define MAX_NR_SG		20
#define EDMA_MAX_SLOTS		MAX_NR_SG
#define EDMA_DESCRIPTORS	16

struct edma_pset {
	u32				len;
	dma_addr_t			addr;
	struct edmacc_param		param;
};

struct edma_desc {
	struct virt_dma_desc		vdesc;
	struct list_head		node;
	enum dma_transfer_direction	direction;
	int				cyclic;
	int				absync;
	int				pset_nr;
	struct edma_chan		*echan;
	int				processed;

	/*
	 * The following 4 elements are used for residue accounting.
	 *
	 * - processed_stat: the number of SG elements we have traversed
	 * so far to cover accounting. This is updated directly to processed
	 * during edma_callback and is always <= processed, because processed
	 * refers to the number of pending transfer (programmed to EDMA
	 * controller), where as processed_stat tracks number of transfers
	 * accounted for so far.
	 *
	 * - residue: The amount of bytes we have left to transfer for this desc
	 *
	 * - residue_stat: The residue in bytes of data we have covered
	 * so far for accounting. This is updated directly to residue
	 * during callbacks to keep it current.
	 *
	 * - sg_len: Tracks the length of the current intermediate transfer,
	 * this is required to update the residue during intermediate transfer
	 * completion callback.
	 */
	int				processed_stat;
	u32				sg_len;
	u32				residue;
	u32				residue_stat;

	struct edma_pset		pset[0];
};

struct edma_cc;

struct edma_chan {
	struct virt_dma_chan		vchan;
	struct list_head		node;
	struct edma_desc		*edesc;
	struct edma_cc			*ecc;
	int				ch_num;
	bool				alloced;
	int				slot[EDMA_MAX_SLOTS];
	int				missed;
	struct dma_slave_config		cfg;
};

struct edma_cc {
	int				ctlr;
	struct dma_device		dma_slave;
	struct edma_chan		slave_chans[EDMA_CHANS];
	int				num_slave_chans;
	int				dummy_slot;
};

static inline struct edma_cc *to_edma_cc(struct dma_device *d)
{
	return container_of(d, struct edma_cc, dma_slave);
}

static inline struct edma_chan *to_edma_chan(struct dma_chan *c)
{
	return container_of(c, struct edma_chan, vchan.chan);
}

static inline struct edma_desc
*to_edma_desc(struct dma_async_tx_descriptor *tx)
{
	return container_of(tx, struct edma_desc, vdesc.tx);
}

static void edma_desc_free(struct virt_dma_desc *vdesc)
{
	kfree(container_of(vdesc, struct edma_desc, vdesc));
}

/* Dispatch a queued descriptor to the controller (caller holds lock) */
static void edma_execute(struct edma_chan *echan)
{
	struct virt_dma_desc *vdesc;
	struct edma_desc *edesc;
	struct device *dev = echan->vchan.chan.device->dev;
	int i, j, left, nslots;

	/* If either we processed all psets or we're still not started */
	if (!echan->edesc ||
	    echan->edesc->pset_nr == echan->edesc->processed) {
		/* Get next vdesc */
		vdesc = vchan_next_desc(&echan->vchan);
		if (!vdesc) {
			echan->edesc = NULL;
			return;
		}
		list_del(&vdesc->node);
		echan->edesc = to_edma_desc(&vdesc->tx);
	}

	edesc = echan->edesc;

	/* Find out how many left */
	left = edesc->pset_nr - edesc->processed;
	nslots = min(MAX_NR_SG, left);
	edesc->sg_len = 0;

	/* Write descriptor PaRAM set(s) */
	for (i = 0; i < nslots; i++) {
		j = i + edesc->processed;
		edma_write_slot(echan->slot[i], &edesc->pset[j].param);
		edesc->sg_len += edesc->pset[j].len;
		dev_vdbg(echan->vchan.chan.device->dev,
			"\n pset[%d]:\n"
			"  chnum\t%d\n"
			"  slot\t%d\n"
			"  opt\t%08x\n"
			"  src\t%08x\n"
			"  dst\t%08x\n"
			"  abcnt\t%08x\n"
			"  ccnt\t%08x\n"
			"  bidx\t%08x\n"
			"  cidx\t%08x\n"
			"  lkrld\t%08x\n",
			j, echan->ch_num, echan->slot[i],
			edesc->pset[j].param.opt,
			edesc->pset[j].param.src,
			edesc->pset[j].param.dst,
			edesc->pset[j].param.a_b_cnt,
			edesc->pset[j].param.ccnt,
			edesc->pset[j].param.src_dst_bidx,
			edesc->pset[j].param.src_dst_cidx,
			edesc->pset[j].param.link_bcntrld);
		/* Link to the previous slot if not the last set */
		if (i != (nslots - 1))
			edma_link(echan->slot[i], echan->slot[i+1]);
	}

	edesc->processed += nslots;

	/*
	 * If this is either the last set in a set of SG-list transactions
	 * then setup a link to the dummy slot, this results in all future
	 * events being absorbed and that's OK because we're done
	 */
	if (edesc->processed == edesc->pset_nr) {
		if (edesc->cyclic)
			edma_link(echan->slot[nslots-1], echan->slot[1]);
		else
			edma_link(echan->slot[nslots-1],
				  echan->ecc->dummy_slot);
	}

	if (edesc->processed <= MAX_NR_SG) {
		dev_dbg(dev, "first transfer starting on channel %d\n",
			echan->ch_num);
		edma_start(echan->ch_num);
	} else {
		dev_dbg(dev, "chan: %d: completed %d elements, resuming\n",
			echan->ch_num, edesc->processed);
		edma_resume(echan->ch_num);
	}

	/*
	 * This happens due to setup times between intermediate transfers
	 * in long SG lists which have to be broken up into transfers of
	 * MAX_NR_SG
	 */
	if (echan->missed) {
		dev_dbg(dev, "missed event on channel %d\n", echan->ch_num);
		edma_clean_channel(echan->ch_num);
		edma_stop(echan->ch_num);
		edma_start(echan->ch_num);
		edma_trigger_channel(echan->ch_num);
		echan->missed = 0;
	}
}

static int edma_terminate_all(struct edma_chan *echan)
{
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&echan->vchan.lock, flags);

	/*
	 * Stop DMA activity: we assume the callback will not be called
	 * after edma_dma() returns (even if it does, it will see
	 * echan->edesc is NULL and exit.)
	 */
	if (echan->edesc) {
		int cyclic = echan->edesc->cyclic;
		echan->edesc = NULL;
		edma_stop(echan->ch_num);
		/* Move the cyclic channel back to default queue */
		if (cyclic)
			edma_assign_channel_eventq(echan->ch_num,
						   EVENTQ_DEFAULT);
	}

	vchan_get_all_descriptors(&echan->vchan, &head);
	spin_unlock_irqrestore(&echan->vchan.lock, flags);
	vchan_dma_desc_free_list(&echan->vchan, &head);

	return 0;
}

static int edma_slave_config(struct edma_chan *echan,
	struct dma_slave_config *cfg)
{
	if (cfg->src_addr_width == DMA_SLAVE_BUSWIDTH_8_BYTES ||
	    cfg->dst_addr_width == DMA_SLAVE_BUSWIDTH_8_BYTES)
		return -EINVAL;

	memcpy(&echan->cfg, cfg, sizeof(echan->cfg));

	return 0;
}

static int edma_dma_pause(struct edma_chan *echan)
{
	/* Pause/Resume only allowed with cyclic mode */
	if (!echan->edesc->cyclic)
		return -EINVAL;

	edma_pause(echan->ch_num);
	return 0;
}

static int edma_dma_resume(struct edma_chan *echan)
{
	/* Pause/Resume only allowed with cyclic mode */
	if (!echan->edesc->cyclic)
		return -EINVAL;

	edma_resume(echan->ch_num);
	return 0;
}

static int edma_control(struct dma_chan *chan, enum dma_ctrl_cmd cmd,
			unsigned long arg)
{
	int ret = 0;
	struct dma_slave_config *config;
	struct edma_chan *echan = to_edma_chan(chan);

	switch (cmd) {
	case DMA_TERMINATE_ALL:
		edma_terminate_all(echan);
		break;
	case DMA_SLAVE_CONFIG:
		config = (struct dma_slave_config *)arg;
		ret = edma_slave_config(echan, config);
		break;
	case DMA_PAUSE:
		ret = edma_dma_pause(echan);
		break;

	case DMA_RESUME:
		ret = edma_dma_resume(echan);
		break;

	default:
		ret = -ENOSYS;
	}

	return ret;
}

/*
 * A PaRAM set configuration abstraction used by other modes
 * @chan: Channel who's PaRAM set we're configuring
 * @pset: PaRAM set to initialize and setup.
 * @src_addr: Source address of the DMA
 * @dst_addr: Destination address of the DMA
 * @burst: In units of dev_width, how much to send
 * @dev_width: How much is the dev_width
 * @dma_length: Total length of the DMA transfer
 * @direction: Direction of the transfer
 */
static int edma_config_pset(struct dma_chan *chan, struct edma_pset *epset,
	dma_addr_t src_addr, dma_addr_t dst_addr, u32 burst,
	enum dma_slave_buswidth dev_width, unsigned int dma_length,
	enum dma_transfer_direction direction)
{
	struct edma_chan *echan = to_edma_chan(chan);
	struct device *dev = chan->device->dev;
	struct edmacc_param *param = &epset->param;
	int acnt, bcnt, ccnt, cidx;
	int src_bidx, dst_bidx, src_cidx, dst_cidx;
	int absync;

	acnt = dev_width;

	/* src/dst_maxburst == 0 is the same case as src/dst_maxburst == 1 */
	if (!burst)
		burst = 1;
	/*
	 * If the maxburst is equal to the fifo width, use
	 * A-synced transfers. This allows for large contiguous
	 * buffer transfers using only one PaRAM set.
	 */
	if (burst == 1) {
		/*
		 * For the A-sync case, bcnt and ccnt are the remainder
		 * and quotient respectively of the division of:
		 * (dma_length / acnt) by (SZ_64K -1). This is so
		 * that in case bcnt over flows, we have ccnt to use.
		 * Note: In A-sync tranfer only, bcntrld is used, but it
		 * only applies for sg_dma_len(sg) >= SZ_64K.
		 * In this case, the best way adopted is- bccnt for the
		 * first frame will be the remainder below. Then for
		 * every successive frame, bcnt will be SZ_64K-1. This
		 * is assured as bcntrld = 0xffff in end of function.
		 */
		absync = false;
		ccnt = dma_length / acnt / (SZ_64K - 1);
		bcnt = dma_length / acnt - ccnt * (SZ_64K - 1);
		/*
		 * If bcnt is non-zero, we have a remainder and hence an
		 * extra frame to transfer, so increment ccnt.
		 */
		if (bcnt)
			ccnt++;
		else
			bcnt = SZ_64K - 1;
		cidx = acnt;
	} else {
		/*
		 * If maxburst is greater than the fifo address_width,
		 * use AB-synced transfers where A count is the fifo
		 * address_width and B count is the maxburst. In this
		 * case, we are limited to transfers of C count frames
		 * of (address_width * maxburst) where C count is limited
		 * to SZ_64K-1. This places an upper bound on the length
		 * of an SG segment that can be handled.
		 */
		absync = true;
		bcnt = burst;
		ccnt = dma_length / (acnt * bcnt);
		if (ccnt > (SZ_64K - 1)) {
			dev_err(dev, "Exceeded max SG segment size\n");
			return -EINVAL;
		}
		cidx = acnt * bcnt;
	}

	epset->len = dma_length;

	if (direction == DMA_MEM_TO_DEV) {
		src_bidx = acnt;
		src_cidx = cidx;
		dst_bidx = 0;
		dst_cidx = 0;
		epset->addr = src_addr;
	} else if (direction == DMA_DEV_TO_MEM)  {
		src_bidx = 0;
		src_cidx = 0;
		dst_bidx = acnt;
		dst_cidx = cidx;
		epset->addr = dst_addr;
	} else if (direction == DMA_MEM_TO_MEM)  {
		src_bidx = acnt;
		src_cidx = cidx;
		dst_bidx = acnt;
		dst_cidx = cidx;
	} else {
		dev_err(dev, "%s: direction not implemented yet\n", __func__);
		return -EINVAL;
	}

	param->opt = EDMA_TCC(EDMA_CHAN_SLOT(echan->ch_num));
	/* Configure A or AB synchronized transfers */
	if (absync)
		param->opt |= SYNCDIM;

	param->src = src_addr;
	param->dst = dst_addr;

	param->src_dst_bidx = (dst_bidx << 16) | src_bidx;
	param->src_dst_cidx = (dst_cidx << 16) | src_cidx;

	param->a_b_cnt = bcnt << 16 | acnt;
	param->ccnt = ccnt;
	/*
	 * Only time when (bcntrld) auto reload is required is for
	 * A-sync case, and in this case, a requirement of reload value
	 * of SZ_64K-1 only is assured. 'link' is initially set to NULL
	 * and then later will be populated by edma_execute.
	 */
	param->link_bcntrld = 0xffffffff;
	return absync;
}

static struct dma_async_tx_descriptor *edma_prep_slave_sg(
	struct dma_chan *chan, struct scatterlist *sgl,
	unsigned int sg_len, enum dma_transfer_direction direction,
	unsigned long tx_flags, void *context)
{
	struct edma_chan *echan = to_edma_chan(chan);
	struct device *dev = chan->device->dev;
	struct edma_desc *edesc;
	dma_addr_t src_addr = 0, dst_addr = 0;
	enum dma_slave_buswidth dev_width;
	u32 burst;
	struct scatterlist *sg;
	int i, nslots, ret;

	if (unlikely(!echan || !sgl || !sg_len))
		return NULL;

	if (direction == DMA_DEV_TO_MEM) {
		src_addr = echan->cfg.src_addr;
		dev_width = echan->cfg.src_addr_width;
		burst = echan->cfg.src_maxburst;
	} else if (direction == DMA_MEM_TO_DEV) {
		dst_addr = echan->cfg.dst_addr;
		dev_width = echan->cfg.dst_addr_width;
		burst = echan->cfg.dst_maxburst;
	} else {
		dev_err(dev, "%s: bad direction: %d\n", __func__, direction);
		return NULL;
	}

	if (dev_width == DMA_SLAVE_BUSWIDTH_UNDEFINED) {
		dev_err(dev, "%s: Undefined slave buswidth\n", __func__);
		return NULL;
	}

	edesc = kzalloc(sizeof(*edesc) + sg_len *
		sizeof(edesc->pset[0]), GFP_ATOMIC);
	if (!edesc) {
		dev_err(dev, "%s: Failed to allocate a descriptor\n", __func__);
		return NULL;
	}

	edesc->pset_nr = sg_len;
	edesc->residue = 0;
	edesc->direction = direction;
	edesc->echan = echan;

	/* Allocate a PaRAM slot, if needed */
	nslots = min_t(unsigned, MAX_NR_SG, sg_len);

	for (i = 0; i < nslots; i++) {
		if (echan->slot[i] < 0) {
			echan->slot[i] =
				edma_alloc_slot(EDMA_CTLR(echan->ch_num),
						EDMA_SLOT_ANY);
			if (echan->slot[i] < 0) {
				kfree(edesc);
				dev_err(dev, "%s: Failed to allocate slot\n",
					__func__);
				return NULL;
			}
		}
	}

	/* Configure PaRAM sets for each SG */
	for_each_sg(sgl, sg, sg_len, i) {
		/* Get address for each SG */
		if (direction == DMA_DEV_TO_MEM)
			dst_addr = sg_dma_address(sg);
		else
			src_addr = sg_dma_address(sg);

		ret = edma_config_pset(chan, &edesc->pset[i], src_addr,
				       dst_addr, burst, dev_width,
				       sg_dma_len(sg), direction);
		if (ret < 0) {
			kfree(edesc);
			return NULL;
		}

		edesc->absync = ret;
		edesc->residue += sg_dma_len(sg);

		/* If this is the last in a current SG set of transactions,
		   enable interrupts so that next set is processed */
		if (!((i+1) % MAX_NR_SG))
			edesc->pset[i].param.opt |= TCINTEN;

		/* If this is the last set, enable completion interrupt flag */
		if (i == sg_len - 1)
			edesc->pset[i].param.opt |= TCINTEN;
	}
	edesc->residue_stat = edesc->residue;

	return vchan_tx_prep(&echan->vchan, &edesc->vdesc, tx_flags);
}

struct dma_async_tx_descriptor *edma_prep_dma_memcpy(
	struct dma_chan *chan, dma_addr_t dest, dma_addr_t src,
	size_t len, unsigned long tx_flags)
{
	int ret;
	struct edma_desc *edesc;
	struct device *dev = chan->device->dev;
	struct edma_chan *echan = to_edma_chan(chan);

	if (unlikely(!echan || !len))
		return NULL;

	edesc = kzalloc(sizeof(*edesc) + sizeof(edesc->pset[0]), GFP_ATOMIC);
	if (!edesc) {
		dev_dbg(dev, "Failed to allocate a descriptor\n");
		return NULL;
	}

	edesc->pset_nr = 1;

	ret = edma_config_pset(chan, &edesc->pset[0], src, dest, 1,
			       DMA_SLAVE_BUSWIDTH_4_BYTES, len, DMA_MEM_TO_MEM);
	if (ret < 0)
		return NULL;

	edesc->absync = ret;

	/*
	 * Enable intermediate transfer chaining to re-trigger channel
	 * on completion of every TR, and enable transfer-completion
	 * interrupt on completion of the whole transfer.
	 */
	edesc->pset[0].param.opt |= ITCCHEN;
	edesc->pset[0].param.opt |= TCINTEN;

	return vchan_tx_prep(&echan->vchan, &edesc->vdesc, tx_flags);
}

static struct dma_async_tx_descriptor *edma_prep_dma_cyclic(
	struct dma_chan *chan, dma_addr_t buf_addr, size_t buf_len,
	size_t period_len, enum dma_transfer_direction direction,
	unsigned long tx_flags)
{
	struct edma_chan *echan = to_edma_chan(chan);
	struct device *dev = chan->device->dev;
	struct edma_desc *edesc;
	dma_addr_t src_addr, dst_addr;
	enum dma_slave_buswidth dev_width;
	u32 burst;
	int i, ret, nslots;

	if (unlikely(!echan || !buf_len || !period_len))
		return NULL;

	if (direction == DMA_DEV_TO_MEM) {
		src_addr = echan->cfg.src_addr;
		dst_addr = buf_addr;
		dev_width = echan->cfg.src_addr_width;
		burst = echan->cfg.src_maxburst;
	} else if (direction == DMA_MEM_TO_DEV) {
		src_addr = buf_addr;
		dst_addr = echan->cfg.dst_addr;
		dev_width = echan->cfg.dst_addr_width;
		burst = echan->cfg.dst_maxburst;
	} else {
		dev_err(dev, "%s: bad direction: %d\n", __func__, direction);
		return NULL;
	}

	if (dev_width == DMA_SLAVE_BUSWIDTH_UNDEFINED) {
		dev_err(dev, "%s: Undefined slave buswidth\n", __func__);
		return NULL;
	}

	if (unlikely(buf_len % period_len)) {
		dev_err(dev, "Period should be multiple of Buffer length\n");
		return NULL;
	}

	nslots = (buf_len / period_len) + 1;

	/*
	 * Cyclic DMA users such as audio cannot tolerate delays introduced
	 * by cases where the number of periods is more than the maximum
	 * number of SGs the EDMA driver can handle at a time. For DMA types
	 * such as Slave SGs, such delays are tolerable and synchronized,
	 * but the synchronization is difficult to achieve with Cyclic and
	 * cannot be guaranteed, so we error out early.
	 */
	if (nslots > MAX_NR_SG)
		return NULL;

	edesc = kzalloc(sizeof(*edesc) + nslots *
		sizeof(edesc->pset[0]), GFP_ATOMIC);
	if (!edesc) {
		dev_err(dev, "%s: Failed to allocate a descriptor\n", __func__);
		return NULL;
	}

	edesc->cyclic = 1;
	edesc->pset_nr = nslots;
	edesc->residue = edesc->residue_stat = buf_len;
	edesc->direction = direction;
	edesc->echan = echan;

	dev_dbg(dev, "%s: channel=%d nslots=%d period_len=%zu buf_len=%zu\n",
		__func__, echan->ch_num, nslots, period_len, buf_len);

	for (i = 0; i < nslots; i++) {
		/* Allocate a PaRAM slot, if needed */
		if (echan->slot[i] < 0) {
			echan->slot[i] =
				edma_alloc_slot(EDMA_CTLR(echan->ch_num),
						EDMA_SLOT_ANY);
			if (echan->slot[i] < 0) {
				kfree(edesc);
				dev_err(dev, "%s: Failed to allocate slot\n",
					__func__);
				return NULL;
			}
		}

		if (i == nslots - 1) {
			memcpy(&edesc->pset[i], &edesc->pset[0],
			       sizeof(edesc->pset[0]));
			break;
		}

		ret = edma_config_pset(chan, &edesc->pset[i], src_addr,
				       dst_addr, burst, dev_width, period_len,
				       direction);
		if (ret < 0) {
			kfree(edesc);
			return NULL;
		}

		if (direction == DMA_DEV_TO_MEM)
			dst_addr += period_len;
		else
			src_addr += period_len;

		dev_vdbg(dev, "%s: Configure period %d of buf:\n", __func__, i);
		dev_vdbg(dev,
			"\n pset[%d]:\n"
			"  chnum\t%d\n"
			"  slot\t%d\n"
			"  opt\t%08x\n"
			"  src\t%08x\n"
			"  dst\t%08x\n"
			"  abcnt\t%08x\n"
			"  ccnt\t%08x\n"
			"  bidx\t%08x\n"
			"  cidx\t%08x\n"
			"  lkrld\t%08x\n",
			i, echan->ch_num, echan->slot[i],
			edesc->pset[i].param.opt,
			edesc->pset[i].param.src,
			edesc->pset[i].param.dst,
			edesc->pset[i].param.a_b_cnt,
			edesc->pset[i].param.ccnt,
			edesc->pset[i].param.src_dst_bidx,
			edesc->pset[i].param.src_dst_cidx,
			edesc->pset[i].param.link_bcntrld);

		edesc->absync = ret;

		/*
		 * Enable period interrupt only if it is requested
		 */
		if (tx_flags & DMA_PREP_INTERRUPT)
			edesc->pset[i].param.opt |= TCINTEN;
	}

	/* Place the cyclic channel to highest priority queue */
	edma_assign_channel_eventq(echan->ch_num, EVENTQ_0);

	return vchan_tx_prep(&echan->vchan, &edesc->vdesc, tx_flags);
}

static void edma_callback(unsigned ch_num, u16 ch_status, void *data)
{
	struct edma_chan *echan = data;
	struct device *dev = echan->vchan.chan.device->dev;
	struct edma_desc *edesc;
	struct edmacc_param p;

	edesc = echan->edesc;

	/* Pause the channel for non-cyclic */
	if (!edesc || (edesc && !edesc->cyclic))
		edma_pause(echan->ch_num);

	switch (ch_status) {
	case EDMA_DMA_COMPLETE:
		spin_lock(&echan->vchan.lock);

		if (edesc) {
			if (edesc->cyclic) {
				vchan_cyclic_callback(&edesc->vdesc);
			} else if (edesc->processed == edesc->pset_nr) {
				dev_dbg(dev, "Transfer complete, stopping channel %d\n", ch_num);
				edesc->residue = 0;
				edma_stop(echan->ch_num);
				vchan_cookie_complete(&edesc->vdesc);
				edma_execute(echan);
			} else {
				dev_dbg(dev, "Intermediate transfer complete on channel %d\n", ch_num);

				/* Update statistics for tx_status */
				edesc->residue -= edesc->sg_len;
				edesc->residue_stat = edesc->residue;
				edesc->processed_stat = edesc->processed;

				edma_execute(echan);
			}
		}

		spin_unlock(&echan->vchan.lock);

		break;
	case EDMA_DMA_CC_ERROR:
		spin_lock(&echan->vchan.lock);

		edma_read_slot(EDMA_CHAN_SLOT(echan->slot[0]), &p);

		/*
		 * Issue later based on missed flag which will be sure
		 * to happen as:
		 * (1) we finished transmitting an intermediate slot and
		 *     edma_execute is coming up.
		 * (2) or we finished current transfer and issue will
		 *     call edma_execute.
		 *
		 * Important note: issuing can be dangerous here and
		 * lead to some nasty recursion when we are in a NULL
		 * slot. So we avoid doing so and set the missed flag.
		 */
		if (p.a_b_cnt == 0 && p.ccnt == 0) {
			dev_dbg(dev, "Error occurred, looks like slot is null, just setting miss\n");
			echan->missed = 1;
		} else {
			/*
			 * The slot is already programmed but the event got
			 * missed, so its safe to issue it here.
			 */
			dev_dbg(dev, "Error occurred but slot is non-null, TRIGGERING\n");
			edma_clean_channel(echan->ch_num);
			edma_stop(echan->ch_num);
			edma_start(echan->ch_num);
			edma_trigger_channel(echan->ch_num);
		}

		spin_unlock(&echan->vchan.lock);

		break;
	default:
		break;
	}
}

/* Alloc channel resources */
static int edma_alloc_chan_resources(struct dma_chan *chan)
{
	struct edma_chan *echan = to_edma_chan(chan);
	struct device *dev = chan->device->dev;
	int ret;
	int a_ch_num;
	LIST_HEAD(descs);

	a_ch_num = edma_alloc_channel(echan->ch_num, edma_callback,
					chan, EVENTQ_DEFAULT);

	if (a_ch_num < 0) {
		ret = -ENODEV;
		goto err_no_chan;
	}

	if (a_ch_num != echan->ch_num) {
		dev_err(dev, "failed to allocate requested channel %u:%u\n",
			EDMA_CTLR(echan->ch_num),
			EDMA_CHAN_SLOT(echan->ch_num));
		ret = -ENODEV;
		goto err_wrong_chan;
	}

	echan->alloced = true;
	echan->slot[0] = echan->ch_num;

	dev_dbg(dev, "allocated channel %d for %u:%u\n", echan->ch_num,
		EDMA_CTLR(echan->ch_num), EDMA_CHAN_SLOT(echan->ch_num));

	return 0;

err_wrong_chan:
	edma_free_channel(a_ch_num);
err_no_chan:
	return ret;
}

/* Free channel resources */
static void edma_free_chan_resources(struct dma_chan *chan)
{
	struct edma_chan *echan = to_edma_chan(chan);
	struct device *dev = chan->device->dev;
	int i;

	/* Terminate transfers */
	edma_stop(echan->ch_num);

	vchan_free_chan_resources(&echan->vchan);

	/* Free EDMA PaRAM slots */
	for (i = 1; i < EDMA_MAX_SLOTS; i++) {
		if (echan->slot[i] >= 0) {
			edma_free_slot(echan->slot[i]);
			echan->slot[i] = -1;
		}
	}

	/* Free EDMA channel */
	if (echan->alloced) {
		edma_free_channel(echan->ch_num);
		echan->alloced = false;
	}

	dev_dbg(dev, "freeing channel for %u\n", echan->ch_num);
}

/* Send pending descriptor to hardware */
static void edma_issue_pending(struct dma_chan *chan)
{
	struct edma_chan *echan = to_edma_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&echan->vchan.lock, flags);
	if (vchan_issue_pending(&echan->vchan) && !echan->edesc)
		edma_execute(echan);
	spin_unlock_irqrestore(&echan->vchan.lock, flags);
}

static u32 edma_residue(struct edma_desc *edesc)
{
	bool dst = edesc->direction == DMA_DEV_TO_MEM;
	struct edma_pset *pset = edesc->pset;
	dma_addr_t done, pos;
	int i;

	/*
	 * We always read the dst/src position from the first RamPar
	 * pset. That's the one which is active now.
	 */
	pos = edma_get_position(edesc->echan->slot[0], dst);

	/*
	 * Cyclic is simple. Just subtract pset[0].addr from pos.
	 *
	 * We never update edesc->residue in the cyclic case, so we
	 * can tell the remaining room to the end of the circular
	 * buffer.
	 */
	if (edesc->cyclic) {
		done = pos - pset->addr;
		edesc->residue_stat = edesc->residue - done;
		return edesc->residue_stat;
	}

	/*
	 * For SG operation we catch up with the last processed
	 * status.
	 */
	pset += edesc->processed_stat;

	for (i = edesc->processed_stat; i < edesc->processed; i++, pset++) {
		/*
		 * If we are inside this pset address range, we know
		 * this is the active one. Get the current delta and
		 * stop walking the psets.
		 */
		if (pos >= pset->addr && pos < pset->addr + pset->len)
			return edesc->residue_stat - (pos - pset->addr);

		/* Otherwise mark it done and update residue_stat. */
		edesc->processed_stat++;
		edesc->residue_stat -= pset->len;
	}
	return edesc->residue_stat;
}

/* Check request completion status */
static enum dma_status edma_tx_status(struct dma_chan *chan,
				      dma_cookie_t cookie,
				      struct dma_tx_state *txstate)
{
	struct edma_chan *echan = to_edma_chan(chan);
	struct virt_dma_desc *vdesc;
	enum dma_status ret;
	unsigned long flags;

	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret == DMA_COMPLETE || !txstate)
		return ret;

	spin_lock_irqsave(&echan->vchan.lock, flags);
	if (echan->edesc && echan->edesc->vdesc.tx.cookie == cookie)
		txstate->residue = edma_residue(echan->edesc);
	else if ((vdesc = vchan_find_desc(&echan->vchan, cookie)))
		txstate->residue = to_edma_desc(&vdesc->tx)->residue;
	spin_unlock_irqrestore(&echan->vchan.lock, flags);

	return ret;
}

static void __init edma_chan_init(struct edma_cc *ecc,
				  struct dma_device *dma,
				  struct edma_chan *echans)
{
	int i, j;

	for (i = 0; i < EDMA_CHANS; i++) {
		struct edma_chan *echan = &echans[i];
		echan->ch_num = EDMA_CTLR_CHAN(ecc->ctlr, i);
		echan->ecc = ecc;
		echan->vchan.desc_free = edma_desc_free;

		vchan_init(&echan->vchan, dma);

		INIT_LIST_HEAD(&echan->node);
		for (j = 0; j < EDMA_MAX_SLOTS; j++)
			echan->slot[j] = -1;
	}
}

#define EDMA_DMA_BUSWIDTHS	(BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) | \
				 BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) | \
				 BIT(DMA_SLAVE_BUSWIDTH_3_BYTES) | \
				 BIT(DMA_SLAVE_BUSWIDTH_4_BYTES))

static int edma_dma_device_slave_caps(struct dma_chan *dchan,
				      struct dma_slave_caps *caps)
{
	caps->src_addr_widths = EDMA_DMA_BUSWIDTHS;
	caps->dstn_addr_widths = EDMA_DMA_BUSWIDTHS;
	caps->directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	caps->cmd_pause = true;
	caps->cmd_terminate = true;
	caps->residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;

	return 0;
}

static void edma_dma_init(struct edma_cc *ecc, struct dma_device *dma,
			  struct device *dev)
{
	dma->device_prep_slave_sg = edma_prep_slave_sg;
	dma->device_prep_dma_cyclic = edma_prep_dma_cyclic;
	dma->device_prep_dma_memcpy = edma_prep_dma_memcpy;
	dma->device_alloc_chan_resources = edma_alloc_chan_resources;
	dma->device_free_chan_resources = edma_free_chan_resources;
	dma->device_issue_pending = edma_issue_pending;
	dma->device_tx_status = edma_tx_status;
	dma->device_control = edma_control;
	dma->device_slave_caps = edma_dma_device_slave_caps;
	dma->dev = dev;

	/*
	 * code using dma memcpy must make sure alignment of
	 * length is at dma->copy_align boundary.
	 */
	dma->copy_align = DMA_SLAVE_BUSWIDTH_4_BYTES;

	INIT_LIST_HEAD(&dma->channels);
}

static int edma_probe(struct platform_device *pdev)
{
	struct edma_cc *ecc;
	int ret;

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	ecc = devm_kzalloc(&pdev->dev, sizeof(*ecc), GFP_KERNEL);
	if (!ecc) {
		dev_err(&pdev->dev, "Can't allocate controller\n");
		return -ENOMEM;
	}

	ecc->ctlr = pdev->id;
	ecc->dummy_slot = edma_alloc_slot(ecc->ctlr, EDMA_SLOT_ANY);
	if (ecc->dummy_slot < 0) {
		dev_err(&pdev->dev, "Can't allocate PaRAM dummy slot\n");
		return ecc->dummy_slot;
	}

	dma_cap_zero(ecc->dma_slave.cap_mask);
	dma_cap_set(DMA_SLAVE, ecc->dma_slave.cap_mask);
	dma_cap_set(DMA_CYCLIC, ecc->dma_slave.cap_mask);
	dma_cap_set(DMA_MEMCPY, ecc->dma_slave.cap_mask);

	edma_dma_init(ecc, &ecc->dma_slave, &pdev->dev);

	edma_chan_init(ecc, &ecc->dma_slave, ecc->slave_chans);

	ret = dma_async_device_register(&ecc->dma_slave);
	if (ret)
		goto err_reg1;

	platform_set_drvdata(pdev, ecc);

	dev_info(&pdev->dev, "TI EDMA DMA engine driver\n");

	return 0;

err_reg1:
	edma_free_slot(ecc->dummy_slot);
	return ret;
}

static int edma_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct edma_cc *ecc = dev_get_drvdata(dev);

	dma_async_device_unregister(&ecc->dma_slave);
	edma_free_slot(ecc->dummy_slot);

	return 0;
}

static struct platform_driver edma_driver = {
	.probe		= edma_probe,
	.remove		= edma_remove,
	.driver = {
		.name = "edma-dma-engine",
		.owner = THIS_MODULE,
	},
};

bool edma_filter_fn(struct dma_chan *chan, void *param)
{
	if (chan->device->dev->driver == &edma_driver.driver) {
		struct edma_chan *echan = to_edma_chan(chan);
		unsigned ch_req = *(unsigned *)param;
		return ch_req == echan->ch_num;
	}
	return false;
}
EXPORT_SYMBOL(edma_filter_fn);

static struct platform_device *pdev0, *pdev1;

static const struct platform_device_info edma_dev_info0 = {
	.name = "edma-dma-engine",
	.id = 0,
	.dma_mask = DMA_BIT_MASK(32),
};

static const struct platform_device_info edma_dev_info1 = {
	.name = "edma-dma-engine",
	.id = 1,
	.dma_mask = DMA_BIT_MASK(32),
};

static int edma_init(void)
{
	int ret = platform_driver_register(&edma_driver);

	if (ret == 0) {
		pdev0 = platform_device_register_full(&edma_dev_info0);
		if (IS_ERR(pdev0)) {
			platform_driver_unregister(&edma_driver);
			ret = PTR_ERR(pdev0);
			goto out;
		}
	}

	if (!of_have_populated_dt() && EDMA_CTLRS == 2) {
		pdev1 = platform_device_register_full(&edma_dev_info1);
		if (IS_ERR(pdev1)) {
			platform_driver_unregister(&edma_driver);
			platform_device_unregister(pdev0);
			ret = PTR_ERR(pdev1);
		}
	}

out:
	return ret;
}
subsys_initcall(edma_init);

static void __exit edma_exit(void)
{
	platform_device_unregister(pdev0);
	if (pdev1)
		platform_device_unregister(pdev1);
	platform_driver_unregister(&edma_driver);
}
module_exit(edma_exit);

MODULE_AUTHOR("Matt Porter <matt.porter@linaro.org>");
MODULE_DESCRIPTION("TI EDMA DMA engine driver");
MODULE_LICENSE("GPL v2");
