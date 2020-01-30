// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2019 Texas Instruments Incorporated - http://www.ti.com
 *  Author: Peter Ujfalusi <peter.ujfalusi@ti.com>
 */

#include <linux/kernel.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/workqueue.h>
#include <linux/completion.h>
#include <linux/soc/ti/k3-ringacc.h>
#include <linux/soc/ti/ti_sci_protocol.h>
#include <linux/soc/ti/ti_sci_inta_msi.h>
#include <linux/dma/ti-cppi5.h>

#include "../virt-dma.h"
#include "k3-udma.h"
#include "k3-psil-priv.h"

struct udma_static_tr {
	u8 elsize; /* RPSTR0 */
	u16 elcnt; /* RPSTR0 */
	u16 bstcnt; /* RPSTR1 */
};

#define K3_UDMA_MAX_RFLOWS		1024
#define K3_UDMA_DEFAULT_RING_SIZE	16

/* How SRC/DST tag should be updated by UDMA in the descriptor's Word 3 */
#define UDMA_RFLOW_SRCTAG_NONE		0
#define UDMA_RFLOW_SRCTAG_CFG_TAG	1
#define UDMA_RFLOW_SRCTAG_FLOW_ID	2
#define UDMA_RFLOW_SRCTAG_SRC_TAG	4

#define UDMA_RFLOW_DSTTAG_NONE		0
#define UDMA_RFLOW_DSTTAG_CFG_TAG	1
#define UDMA_RFLOW_DSTTAG_FLOW_ID	2
#define UDMA_RFLOW_DSTTAG_DST_TAG_LO	4
#define UDMA_RFLOW_DSTTAG_DST_TAG_HI	5

struct udma_chan;

enum udma_mmr {
	MMR_GCFG = 0,
	MMR_RCHANRT,
	MMR_TCHANRT,
	MMR_LAST,
};

static const char * const mmr_names[] = { "gcfg", "rchanrt", "tchanrt" };

struct udma_tchan {
	void __iomem *reg_rt;

	int id;
	struct k3_ring *t_ring; /* Transmit ring */
	struct k3_ring *tc_ring; /* Transmit Completion ring */
};

struct udma_rflow {
	int id;
	struct k3_ring *fd_ring; /* Free Descriptor ring */
	struct k3_ring *r_ring; /* Receive ring */
};

struct udma_rchan {
	void __iomem *reg_rt;

	int id;
};

#define UDMA_FLAG_PDMA_ACC32		BIT(0)
#define UDMA_FLAG_PDMA_BURST		BIT(1)

struct udma_match_data {
	u32 psil_base;
	bool enable_memcpy_support;
	u32 flags;
	u32 statictr_z_mask;
	u32 rchan_oes_offset;

	u8 tpl_levels;
	u32 level_start_idx[];
};

struct udma_dev {
	struct dma_device ddev;
	struct device *dev;
	void __iomem *mmrs[MMR_LAST];
	const struct udma_match_data *match_data;

	size_t desc_align; /* alignment to use for descriptors */

	struct udma_tisci_rm tisci_rm;

	struct k3_ringacc *ringacc;

	struct work_struct purge_work;
	struct list_head desc_to_purge;
	spinlock_t lock;

	int tchan_cnt;
	int echan_cnt;
	int rchan_cnt;
	int rflow_cnt;
	unsigned long *tchan_map;
	unsigned long *rchan_map;
	unsigned long *rflow_gp_map;
	unsigned long *rflow_gp_map_allocated;
	unsigned long *rflow_in_use;

	struct udma_tchan *tchans;
	struct udma_rchan *rchans;
	struct udma_rflow *rflows;

	struct udma_chan *channels;
	u32 psil_base;
};

struct udma_hwdesc {
	size_t cppi5_desc_size;
	void *cppi5_desc_vaddr;
	dma_addr_t cppi5_desc_paddr;

	/* TR descriptor internal pointers */
	void *tr_req_base;
	struct cppi5_tr_resp_t *tr_resp_base;
};

struct udma_desc {
	struct virt_dma_desc vd;

	bool terminated;

	enum dma_transfer_direction dir;

	struct udma_static_tr static_tr;
	u32 residue;

	unsigned int sglen;
	unsigned int desc_idx; /* Only used for cyclic in packet mode */
	unsigned int tr_idx;

	u32 metadata_size;
	void *metadata; /* pointer to provided metadata buffer (EPIP, PSdata) */

	unsigned int hwdesc_count;
	struct udma_hwdesc hwdesc[0];
};

enum udma_chan_state {
	UDMA_CHAN_IS_IDLE = 0, /* not active, no teardown is in progress */
	UDMA_CHAN_IS_ACTIVE, /* Normal operation */
	UDMA_CHAN_IS_TERMINATING, /* channel is being terminated */
};

struct udma_tx_drain {
	struct delayed_work work;
	unsigned long jiffie;
	u32 residue;
};

struct udma_chan_config {
	bool pkt_mode; /* TR or packet */
	bool needs_epib; /* EPIB is needed for the communication or not */
	u32 psd_size; /* size of Protocol Specific Data */
	u32 metadata_size; /* (needs_epib ? 16:0) + psd_size */
	u32 hdesc_size; /* Size of a packet descriptor in packet mode */
	bool notdpkt; /* Suppress sending TDC packet */
	int remote_thread_id;
	u32 src_thread;
	u32 dst_thread;
	enum psil_endpoint_type ep_type;
	bool enable_acc32;
	bool enable_burst;
	enum udma_tp_level channel_tpl; /* Channel Throughput Level */

	enum dma_transfer_direction dir;
};

struct udma_chan {
	struct virt_dma_chan vc;
	struct dma_slave_config	cfg;
	struct udma_dev *ud;
	struct udma_desc *desc;
	struct udma_desc *terminated_desc;
	struct udma_static_tr static_tr;
	char *name;

	struct udma_tchan *tchan;
	struct udma_rchan *rchan;
	struct udma_rflow *rflow;

	bool psil_paired;

	int irq_num_ring;
	int irq_num_udma;

	bool cyclic;
	bool paused;

	enum udma_chan_state state;
	struct completion teardown_completed;

	struct udma_tx_drain tx_drain;

	u32 bcnt; /* number of bytes completed since the start of the channel */
	u32 in_ring_cnt; /* number of descriptors in flight */

	/* Channel configuration parameters */
	struct udma_chan_config config;

	/* dmapool for packet mode descriptors */
	bool use_dma_pool;
	struct dma_pool *hdesc_pool;

	u32 id;
};

static inline struct udma_dev *to_udma_dev(struct dma_device *d)
{
	return container_of(d, struct udma_dev, ddev);
}

static inline struct udma_chan *to_udma_chan(struct dma_chan *c)
{
	return container_of(c, struct udma_chan, vc.chan);
}

static inline struct udma_desc *to_udma_desc(struct dma_async_tx_descriptor *t)
{
	return container_of(t, struct udma_desc, vd.tx);
}

/* Generic register access functions */
static inline u32 udma_read(void __iomem *base, int reg)
{
	return readl(base + reg);
}

static inline void udma_write(void __iomem *base, int reg, u32 val)
{
	writel(val, base + reg);
}

static inline void udma_update_bits(void __iomem *base, int reg,
				    u32 mask, u32 val)
{
	u32 tmp, orig;

	orig = readl(base + reg);
	tmp = orig & ~mask;
	tmp |= (val & mask);

	if (tmp != orig)
		writel(tmp, base + reg);
}

/* TCHANRT */
static inline u32 udma_tchanrt_read(struct udma_tchan *tchan, int reg)
{
	if (!tchan)
		return 0;
	return udma_read(tchan->reg_rt, reg);
}

static inline void udma_tchanrt_write(struct udma_tchan *tchan, int reg,
				      u32 val)
{
	if (!tchan)
		return;
	udma_write(tchan->reg_rt, reg, val);
}

static inline void udma_tchanrt_update_bits(struct udma_tchan *tchan, int reg,
					    u32 mask, u32 val)
{
	if (!tchan)
		return;
	udma_update_bits(tchan->reg_rt, reg, mask, val);
}

/* RCHANRT */
static inline u32 udma_rchanrt_read(struct udma_rchan *rchan, int reg)
{
	if (!rchan)
		return 0;
	return udma_read(rchan->reg_rt, reg);
}

static inline void udma_rchanrt_write(struct udma_rchan *rchan, int reg,
				      u32 val)
{
	if (!rchan)
		return;
	udma_write(rchan->reg_rt, reg, val);
}

static inline void udma_rchanrt_update_bits(struct udma_rchan *rchan, int reg,
					    u32 mask, u32 val)
{
	if (!rchan)
		return;
	udma_update_bits(rchan->reg_rt, reg, mask, val);
}

static int navss_psil_pair(struct udma_dev *ud, u32 src_thread, u32 dst_thread)
{
	struct udma_tisci_rm *tisci_rm = &ud->tisci_rm;

	dst_thread |= K3_PSIL_DST_THREAD_ID_OFFSET;
	return tisci_rm->tisci_psil_ops->pair(tisci_rm->tisci,
					      tisci_rm->tisci_navss_dev_id,
					      src_thread, dst_thread);
}

static int navss_psil_unpair(struct udma_dev *ud, u32 src_thread,
			     u32 dst_thread)
{
	struct udma_tisci_rm *tisci_rm = &ud->tisci_rm;

	dst_thread |= K3_PSIL_DST_THREAD_ID_OFFSET;
	return tisci_rm->tisci_psil_ops->unpair(tisci_rm->tisci,
						tisci_rm->tisci_navss_dev_id,
						src_thread, dst_thread);
}

static void udma_reset_uchan(struct udma_chan *uc)
{
	memset(&uc->config, 0, sizeof(uc->config));
	uc->config.remote_thread_id = -1;
	uc->state = UDMA_CHAN_IS_IDLE;
}

static void udma_dump_chan_stdata(struct udma_chan *uc)
{
	struct device *dev = uc->ud->dev;
	u32 offset;
	int i;

	if (uc->config.dir == DMA_MEM_TO_DEV || uc->config.dir == DMA_MEM_TO_MEM) {
		dev_dbg(dev, "TCHAN State data:\n");
		for (i = 0; i < 32; i++) {
			offset = UDMA_TCHAN_RT_STDATA_REG + i * 4;
			dev_dbg(dev, "TRT_STDATA[%02d]: 0x%08x\n", i,
				udma_tchanrt_read(uc->tchan, offset));
		}
	}

	if (uc->config.dir == DMA_DEV_TO_MEM || uc->config.dir == DMA_MEM_TO_MEM) {
		dev_dbg(dev, "RCHAN State data:\n");
		for (i = 0; i < 32; i++) {
			offset = UDMA_RCHAN_RT_STDATA_REG + i * 4;
			dev_dbg(dev, "RRT_STDATA[%02d]: 0x%08x\n", i,
				udma_rchanrt_read(uc->rchan, offset));
		}
	}
}

static inline dma_addr_t udma_curr_cppi5_desc_paddr(struct udma_desc *d,
						    int idx)
{
	return d->hwdesc[idx].cppi5_desc_paddr;
}

static inline void *udma_curr_cppi5_desc_vaddr(struct udma_desc *d, int idx)
{
	return d->hwdesc[idx].cppi5_desc_vaddr;
}

static struct udma_desc *udma_udma_desc_from_paddr(struct udma_chan *uc,
						   dma_addr_t paddr)
{
	struct udma_desc *d = uc->terminated_desc;

	if (d) {
		dma_addr_t desc_paddr = udma_curr_cppi5_desc_paddr(d,
								   d->desc_idx);

		if (desc_paddr != paddr)
			d = NULL;
	}

	if (!d) {
		d = uc->desc;
		if (d) {
			dma_addr_t desc_paddr = udma_curr_cppi5_desc_paddr(d,
								d->desc_idx);

			if (desc_paddr != paddr)
				d = NULL;
		}
	}

	return d;
}

static void udma_free_hwdesc(struct udma_chan *uc, struct udma_desc *d)
{
	if (uc->use_dma_pool) {
		int i;

		for (i = 0; i < d->hwdesc_count; i++) {
			if (!d->hwdesc[i].cppi5_desc_vaddr)
				continue;

			dma_pool_free(uc->hdesc_pool,
				      d->hwdesc[i].cppi5_desc_vaddr,
				      d->hwdesc[i].cppi5_desc_paddr);

			d->hwdesc[i].cppi5_desc_vaddr = NULL;
		}
	} else if (d->hwdesc[0].cppi5_desc_vaddr) {
		struct udma_dev *ud = uc->ud;

		dma_free_coherent(ud->dev, d->hwdesc[0].cppi5_desc_size,
				  d->hwdesc[0].cppi5_desc_vaddr,
				  d->hwdesc[0].cppi5_desc_paddr);

		d->hwdesc[0].cppi5_desc_vaddr = NULL;
	}
}

static void udma_purge_desc_work(struct work_struct *work)
{
	struct udma_dev *ud = container_of(work, typeof(*ud), purge_work);
	struct virt_dma_desc *vd, *_vd;
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&ud->lock, flags);
	list_splice_tail_init(&ud->desc_to_purge, &head);
	spin_unlock_irqrestore(&ud->lock, flags);

	list_for_each_entry_safe(vd, _vd, &head, node) {
		struct udma_chan *uc = to_udma_chan(vd->tx.chan);
		struct udma_desc *d = to_udma_desc(&vd->tx);

		udma_free_hwdesc(uc, d);
		list_del(&vd->node);
		kfree(d);
	}

	/* If more to purge, schedule the work again */
	if (!list_empty(&ud->desc_to_purge))
		schedule_work(&ud->purge_work);
}

static void udma_desc_free(struct virt_dma_desc *vd)
{
	struct udma_dev *ud = to_udma_dev(vd->tx.chan->device);
	struct udma_chan *uc = to_udma_chan(vd->tx.chan);
	struct udma_desc *d = to_udma_desc(&vd->tx);
	unsigned long flags;

	if (uc->terminated_desc == d)
		uc->terminated_desc = NULL;

	if (uc->use_dma_pool) {
		udma_free_hwdesc(uc, d);
		kfree(d);
		return;
	}

	spin_lock_irqsave(&ud->lock, flags);
	list_add_tail(&vd->node, &ud->desc_to_purge);
	spin_unlock_irqrestore(&ud->lock, flags);

	schedule_work(&ud->purge_work);
}

static bool udma_is_chan_running(struct udma_chan *uc)
{
	u32 trt_ctl = 0;
	u32 rrt_ctl = 0;

	if (uc->tchan)
		trt_ctl = udma_tchanrt_read(uc->tchan, UDMA_TCHAN_RT_CTL_REG);
	if (uc->rchan)
		rrt_ctl = udma_rchanrt_read(uc->rchan, UDMA_RCHAN_RT_CTL_REG);

	if (trt_ctl & UDMA_CHAN_RT_CTL_EN || rrt_ctl & UDMA_CHAN_RT_CTL_EN)
		return true;

	return false;
}

static bool udma_is_chan_paused(struct udma_chan *uc)
{
	u32 val, pause_mask;

	switch (uc->desc->dir) {
	case DMA_DEV_TO_MEM:
		val = udma_rchanrt_read(uc->rchan,
					UDMA_RCHAN_RT_PEER_RT_EN_REG);
		pause_mask = UDMA_PEER_RT_EN_PAUSE;
		break;
	case DMA_MEM_TO_DEV:
		val = udma_tchanrt_read(uc->tchan,
					UDMA_TCHAN_RT_PEER_RT_EN_REG);
		pause_mask = UDMA_PEER_RT_EN_PAUSE;
		break;
	case DMA_MEM_TO_MEM:
		val = udma_tchanrt_read(uc->tchan, UDMA_TCHAN_RT_CTL_REG);
		pause_mask = UDMA_CHAN_RT_CTL_PAUSE;
		break;
	default:
		return false;
	}

	if (val & pause_mask)
		return true;

	return false;
}

static void udma_sync_for_device(struct udma_chan *uc, int idx)
{
	struct udma_desc *d = uc->desc;

	if (uc->cyclic && uc->config.pkt_mode) {
		dma_sync_single_for_device(uc->ud->dev,
					   d->hwdesc[idx].cppi5_desc_paddr,
					   d->hwdesc[idx].cppi5_desc_size,
					   DMA_TO_DEVICE);
	} else {
		int i;

		for (i = 0; i < d->hwdesc_count; i++) {
			if (!d->hwdesc[i].cppi5_desc_vaddr)
				continue;

			dma_sync_single_for_device(uc->ud->dev,
						d->hwdesc[i].cppi5_desc_paddr,
						d->hwdesc[i].cppi5_desc_size,
						DMA_TO_DEVICE);
		}
	}
}

static int udma_push_to_ring(struct udma_chan *uc, int idx)
{
	struct udma_desc *d = uc->desc;

	struct k3_ring *ring = NULL;
	int ret = -EINVAL;

	switch (uc->config.dir) {
	case DMA_DEV_TO_MEM:
		ring = uc->rflow->fd_ring;
		break;
	case DMA_MEM_TO_DEV:
	case DMA_MEM_TO_MEM:
		ring = uc->tchan->t_ring;
		break;
	default:
		break;
	}

	if (ring) {
		dma_addr_t desc_addr = udma_curr_cppi5_desc_paddr(d, idx);

		wmb(); /* Ensure that writes are not moved over this point */
		udma_sync_for_device(uc, idx);
		ret = k3_ringacc_ring_push(ring, &desc_addr);
		uc->in_ring_cnt++;
	}

	return ret;
}

static int udma_pop_from_ring(struct udma_chan *uc, dma_addr_t *addr)
{
	struct k3_ring *ring = NULL;
	int ret = -ENOENT;

	switch (uc->config.dir) {
	case DMA_DEV_TO_MEM:
		ring = uc->rflow->r_ring;
		break;
	case DMA_MEM_TO_DEV:
	case DMA_MEM_TO_MEM:
		ring = uc->tchan->tc_ring;
		break;
	default:
		break;
	}

	if (ring && k3_ringacc_ring_get_occ(ring)) {
		struct udma_desc *d = NULL;

		ret = k3_ringacc_ring_pop(ring, addr);
		if (ret)
			return ret;

		/* Teardown completion */
		if (cppi5_desc_is_tdcm(*addr))
			return ret;

		d = udma_udma_desc_from_paddr(uc, *addr);

		if (d)
			dma_sync_single_for_cpu(uc->ud->dev, *addr,
						d->hwdesc[0].cppi5_desc_size,
						DMA_FROM_DEVICE);
		rmb(); /* Ensure that reads are not moved before this point */

		if (!ret)
			uc->in_ring_cnt--;
	}

	return ret;
}

static void udma_reset_rings(struct udma_chan *uc)
{
	struct k3_ring *ring1 = NULL;
	struct k3_ring *ring2 = NULL;

	switch (uc->config.dir) {
	case DMA_DEV_TO_MEM:
		if (uc->rchan) {
			ring1 = uc->rflow->fd_ring;
			ring2 = uc->rflow->r_ring;
		}
		break;
	case DMA_MEM_TO_DEV:
	case DMA_MEM_TO_MEM:
		if (uc->tchan) {
			ring1 = uc->tchan->t_ring;
			ring2 = uc->tchan->tc_ring;
		}
		break;
	default:
		break;
	}

	if (ring1)
		k3_ringacc_ring_reset_dma(ring1,
					  k3_ringacc_ring_get_occ(ring1));
	if (ring2)
		k3_ringacc_ring_reset(ring2);

	/* make sure we are not leaking memory by stalled descriptor */
	if (uc->terminated_desc) {
		udma_desc_free(&uc->terminated_desc->vd);
		uc->terminated_desc = NULL;
	}

	uc->in_ring_cnt = 0;
}

static void udma_reset_counters(struct udma_chan *uc)
{
	u32 val;

	if (uc->tchan) {
		val = udma_tchanrt_read(uc->tchan, UDMA_TCHAN_RT_BCNT_REG);
		udma_tchanrt_write(uc->tchan, UDMA_TCHAN_RT_BCNT_REG, val);

		val = udma_tchanrt_read(uc->tchan, UDMA_TCHAN_RT_SBCNT_REG);
		udma_tchanrt_write(uc->tchan, UDMA_TCHAN_RT_SBCNT_REG, val);

		val = udma_tchanrt_read(uc->tchan, UDMA_TCHAN_RT_PCNT_REG);
		udma_tchanrt_write(uc->tchan, UDMA_TCHAN_RT_PCNT_REG, val);

		val = udma_tchanrt_read(uc->tchan, UDMA_TCHAN_RT_PEER_BCNT_REG);
		udma_tchanrt_write(uc->tchan, UDMA_TCHAN_RT_PEER_BCNT_REG, val);
	}

	if (uc->rchan) {
		val = udma_rchanrt_read(uc->rchan, UDMA_RCHAN_RT_BCNT_REG);
		udma_rchanrt_write(uc->rchan, UDMA_RCHAN_RT_BCNT_REG, val);

		val = udma_rchanrt_read(uc->rchan, UDMA_RCHAN_RT_SBCNT_REG);
		udma_rchanrt_write(uc->rchan, UDMA_RCHAN_RT_SBCNT_REG, val);

		val = udma_rchanrt_read(uc->rchan, UDMA_RCHAN_RT_PCNT_REG);
		udma_rchanrt_write(uc->rchan, UDMA_RCHAN_RT_PCNT_REG, val);

		val = udma_rchanrt_read(uc->rchan, UDMA_RCHAN_RT_PEER_BCNT_REG);
		udma_rchanrt_write(uc->rchan, UDMA_RCHAN_RT_PEER_BCNT_REG, val);
	}

	uc->bcnt = 0;
}

static int udma_reset_chan(struct udma_chan *uc, bool hard)
{
	switch (uc->config.dir) {
	case DMA_DEV_TO_MEM:
		udma_rchanrt_write(uc->rchan, UDMA_RCHAN_RT_PEER_RT_EN_REG, 0);
		udma_rchanrt_write(uc->rchan, UDMA_RCHAN_RT_CTL_REG, 0);
		break;
	case DMA_MEM_TO_DEV:
		udma_tchanrt_write(uc->tchan, UDMA_TCHAN_RT_CTL_REG, 0);
		udma_tchanrt_write(uc->tchan, UDMA_TCHAN_RT_PEER_RT_EN_REG, 0);
		break;
	case DMA_MEM_TO_MEM:
		udma_rchanrt_write(uc->rchan, UDMA_RCHAN_RT_CTL_REG, 0);
		udma_tchanrt_write(uc->tchan, UDMA_TCHAN_RT_CTL_REG, 0);
		break;
	default:
		return -EINVAL;
	}

	/* Reset all counters */
	udma_reset_counters(uc);

	/* Hard reset: re-initialize the channel to reset */
	if (hard) {
		struct udma_chan_config ucc_backup;
		int ret;

		memcpy(&ucc_backup, &uc->config, sizeof(uc->config));
		uc->ud->ddev.device_free_chan_resources(&uc->vc.chan);

		/* restore the channel configuration */
		memcpy(&uc->config, &ucc_backup, sizeof(uc->config));
		ret = uc->ud->ddev.device_alloc_chan_resources(&uc->vc.chan);
		if (ret)
			return ret;

		/*
		 * Setting forced teardown after forced reset helps recovering
		 * the rchan.
		 */
		if (uc->config.dir == DMA_DEV_TO_MEM)
			udma_rchanrt_write(uc->rchan, UDMA_RCHAN_RT_CTL_REG,
					   UDMA_CHAN_RT_CTL_EN |
					   UDMA_CHAN_RT_CTL_TDOWN |
					   UDMA_CHAN_RT_CTL_FTDOWN);
	}
	uc->state = UDMA_CHAN_IS_IDLE;

	return 0;
}

static void udma_start_desc(struct udma_chan *uc)
{
	struct udma_chan_config *ucc = &uc->config;

	if (ucc->pkt_mode && (uc->cyclic || ucc->dir == DMA_DEV_TO_MEM)) {
		int i;

		/* Push all descriptors to ring for packet mode cyclic or RX */
		for (i = 0; i < uc->desc->sglen; i++)
			udma_push_to_ring(uc, i);
	} else {
		udma_push_to_ring(uc, 0);
	}
}

static bool udma_chan_needs_reconfiguration(struct udma_chan *uc)
{
	/* Only PDMAs have staticTR */
	if (uc->config.ep_type == PSIL_EP_NATIVE)
		return false;

	/* Check if the staticTR configuration has changed for TX */
	if (memcmp(&uc->static_tr, &uc->desc->static_tr, sizeof(uc->static_tr)))
		return true;

	return false;
}

static int udma_start(struct udma_chan *uc)
{
	struct virt_dma_desc *vd = vchan_next_desc(&uc->vc);

	if (!vd) {
		uc->desc = NULL;
		return -ENOENT;
	}

	list_del(&vd->node);

	uc->desc = to_udma_desc(&vd->tx);

	/* Channel is already running and does not need reconfiguration */
	if (udma_is_chan_running(uc) && !udma_chan_needs_reconfiguration(uc)) {
		udma_start_desc(uc);
		goto out;
	}

	/* Make sure that we clear the teardown bit, if it is set */
	udma_reset_chan(uc, false);

	/* Push descriptors before we start the channel */
	udma_start_desc(uc);

	switch (uc->desc->dir) {
	case DMA_DEV_TO_MEM:
		/* Config remote TR */
		if (uc->config.ep_type == PSIL_EP_PDMA_XY) {
			u32 val = PDMA_STATIC_TR_Y(uc->desc->static_tr.elcnt) |
				  PDMA_STATIC_TR_X(uc->desc->static_tr.elsize);
			const struct udma_match_data *match_data =
							uc->ud->match_data;

			if (uc->config.enable_acc32)
				val |= PDMA_STATIC_TR_XY_ACC32;
			if (uc->config.enable_burst)
				val |= PDMA_STATIC_TR_XY_BURST;

			udma_rchanrt_write(uc->rchan,
				UDMA_RCHAN_RT_PEER_STATIC_TR_XY_REG, val);

			udma_rchanrt_write(uc->rchan,
				UDMA_RCHAN_RT_PEER_STATIC_TR_Z_REG,
				PDMA_STATIC_TR_Z(uc->desc->static_tr.bstcnt,
						 match_data->statictr_z_mask));

			/* save the current staticTR configuration */
			memcpy(&uc->static_tr, &uc->desc->static_tr,
			       sizeof(uc->static_tr));
		}

		udma_rchanrt_write(uc->rchan, UDMA_RCHAN_RT_CTL_REG,
				   UDMA_CHAN_RT_CTL_EN);

		/* Enable remote */
		udma_rchanrt_write(uc->rchan, UDMA_RCHAN_RT_PEER_RT_EN_REG,
				   UDMA_PEER_RT_EN_ENABLE);

		break;
	case DMA_MEM_TO_DEV:
		/* Config remote TR */
		if (uc->config.ep_type == PSIL_EP_PDMA_XY) {
			u32 val = PDMA_STATIC_TR_Y(uc->desc->static_tr.elcnt) |
				  PDMA_STATIC_TR_X(uc->desc->static_tr.elsize);

			if (uc->config.enable_acc32)
				val |= PDMA_STATIC_TR_XY_ACC32;
			if (uc->config.enable_burst)
				val |= PDMA_STATIC_TR_XY_BURST;

			udma_tchanrt_write(uc->tchan,
				UDMA_TCHAN_RT_PEER_STATIC_TR_XY_REG, val);

			/* save the current staticTR configuration */
			memcpy(&uc->static_tr, &uc->desc->static_tr,
			       sizeof(uc->static_tr));
		}

		/* Enable remote */
		udma_tchanrt_write(uc->tchan, UDMA_TCHAN_RT_PEER_RT_EN_REG,
				   UDMA_PEER_RT_EN_ENABLE);

		udma_tchanrt_write(uc->tchan, UDMA_TCHAN_RT_CTL_REG,
				   UDMA_CHAN_RT_CTL_EN);

		break;
	case DMA_MEM_TO_MEM:
		udma_rchanrt_write(uc->rchan, UDMA_RCHAN_RT_CTL_REG,
				   UDMA_CHAN_RT_CTL_EN);
		udma_tchanrt_write(uc->tchan, UDMA_TCHAN_RT_CTL_REG,
				   UDMA_CHAN_RT_CTL_EN);

		break;
	default:
		return -EINVAL;
	}

	uc->state = UDMA_CHAN_IS_ACTIVE;
out:

	return 0;
}

static int udma_stop(struct udma_chan *uc)
{
	enum udma_chan_state old_state = uc->state;

	uc->state = UDMA_CHAN_IS_TERMINATING;
	reinit_completion(&uc->teardown_completed);

	switch (uc->config.dir) {
	case DMA_DEV_TO_MEM:
		udma_rchanrt_write(uc->rchan, UDMA_RCHAN_RT_PEER_RT_EN_REG,
				   UDMA_PEER_RT_EN_ENABLE |
				   UDMA_PEER_RT_EN_TEARDOWN);
		break;
	case DMA_MEM_TO_DEV:
		udma_tchanrt_write(uc->tchan, UDMA_TCHAN_RT_PEER_RT_EN_REG,
				   UDMA_PEER_RT_EN_ENABLE |
				   UDMA_PEER_RT_EN_FLUSH);
		udma_tchanrt_write(uc->tchan, UDMA_TCHAN_RT_CTL_REG,
				   UDMA_CHAN_RT_CTL_EN |
				   UDMA_CHAN_RT_CTL_TDOWN);
		break;
	case DMA_MEM_TO_MEM:
		udma_tchanrt_write(uc->tchan, UDMA_TCHAN_RT_CTL_REG,
				   UDMA_CHAN_RT_CTL_EN |
				   UDMA_CHAN_RT_CTL_TDOWN);
		break;
	default:
		uc->state = old_state;
		complete_all(&uc->teardown_completed);
		return -EINVAL;
	}

	return 0;
}

static void udma_cyclic_packet_elapsed(struct udma_chan *uc)
{
	struct udma_desc *d = uc->desc;
	struct cppi5_host_desc_t *h_desc;

	h_desc = d->hwdesc[d->desc_idx].cppi5_desc_vaddr;
	cppi5_hdesc_reset_to_original(h_desc);
	udma_push_to_ring(uc, d->desc_idx);
	d->desc_idx = (d->desc_idx + 1) % d->sglen;
}

static inline void udma_fetch_epib(struct udma_chan *uc, struct udma_desc *d)
{
	struct cppi5_host_desc_t *h_desc = d->hwdesc[0].cppi5_desc_vaddr;

	memcpy(d->metadata, h_desc->epib, d->metadata_size);
}

static bool udma_is_desc_really_done(struct udma_chan *uc, struct udma_desc *d)
{
	u32 peer_bcnt, bcnt;

	/* Only TX towards PDMA is affected */
	if (uc->config.ep_type == PSIL_EP_NATIVE ||
	    uc->config.dir != DMA_MEM_TO_DEV)
		return true;

	peer_bcnt = udma_tchanrt_read(uc->tchan, UDMA_TCHAN_RT_PEER_BCNT_REG);
	bcnt = udma_tchanrt_read(uc->tchan, UDMA_TCHAN_RT_BCNT_REG);

	if (peer_bcnt < bcnt) {
		uc->tx_drain.residue = bcnt - peer_bcnt;
		uc->tx_drain.jiffie = jiffies;
		return false;
	}

	return true;
}

static void udma_check_tx_completion(struct work_struct *work)
{
	struct udma_chan *uc = container_of(work, typeof(*uc),
					    tx_drain.work.work);
	bool desc_done = true;
	u32 residue_diff;
	unsigned long jiffie_diff, delay;

	if (uc->desc) {
		residue_diff = uc->tx_drain.residue;
		jiffie_diff = uc->tx_drain.jiffie;
		desc_done = udma_is_desc_really_done(uc, uc->desc);
	}

	if (!desc_done) {
		jiffie_diff = uc->tx_drain.jiffie - jiffie_diff;
		residue_diff -= uc->tx_drain.residue;
		if (residue_diff) {
			/* Try to guess when we should check next time */
			residue_diff /= jiffie_diff;
			delay = uc->tx_drain.residue / residue_diff / 3;
			if (jiffies_to_msecs(delay) < 5)
				delay = 0;
		} else {
			/* No progress, check again in 1 second  */
			delay = HZ;
		}

		schedule_delayed_work(&uc->tx_drain.work, delay);
	} else if (uc->desc) {
		struct udma_desc *d = uc->desc;

		uc->bcnt += d->residue;
		udma_start(uc);
		vchan_cookie_complete(&d->vd);
	}
}

static irqreturn_t udma_ring_irq_handler(int irq, void *data)
{
	struct udma_chan *uc = data;
	struct udma_desc *d;
	unsigned long flags;
	dma_addr_t paddr = 0;

	if (udma_pop_from_ring(uc, &paddr) || !paddr)
		return IRQ_HANDLED;

	spin_lock_irqsave(&uc->vc.lock, flags);

	/* Teardown completion message */
	if (cppi5_desc_is_tdcm(paddr)) {
		/* Compensate our internal pop/push counter */
		uc->in_ring_cnt++;

		complete_all(&uc->teardown_completed);

		if (uc->terminated_desc) {
			udma_desc_free(&uc->terminated_desc->vd);
			uc->terminated_desc = NULL;
		}

		if (!uc->desc)
			udma_start(uc);

		goto out;
	}

	d = udma_udma_desc_from_paddr(uc, paddr);

	if (d) {
		dma_addr_t desc_paddr = udma_curr_cppi5_desc_paddr(d,
								   d->desc_idx);
		if (desc_paddr != paddr) {
			dev_err(uc->ud->dev, "not matching descriptors!\n");
			goto out;
		}

		if (uc->cyclic) {
			/* push the descriptor back to the ring */
			if (d == uc->desc) {
				udma_cyclic_packet_elapsed(uc);
				vchan_cyclic_callback(&d->vd);
			}
		} else {
			bool desc_done = false;

			if (d == uc->desc) {
				desc_done = udma_is_desc_really_done(uc, d);

				if (desc_done) {
					uc->bcnt += d->residue;
					udma_start(uc);
				} else {
					schedule_delayed_work(&uc->tx_drain.work,
							      0);
				}
			}

			if (desc_done)
				vchan_cookie_complete(&d->vd);
		}
	}
out:
	spin_unlock_irqrestore(&uc->vc.lock, flags);

	return IRQ_HANDLED;
}

static irqreturn_t udma_udma_irq_handler(int irq, void *data)
{
	struct udma_chan *uc = data;
	struct udma_desc *d;
	unsigned long flags;

	spin_lock_irqsave(&uc->vc.lock, flags);
	d = uc->desc;
	if (d) {
		d->tr_idx = (d->tr_idx + 1) % d->sglen;

		if (uc->cyclic) {
			vchan_cyclic_callback(&d->vd);
		} else {
			/* TODO: figure out the real amount of data */
			uc->bcnt += d->residue;
			udma_start(uc);
			vchan_cookie_complete(&d->vd);
		}
	}

	spin_unlock_irqrestore(&uc->vc.lock, flags);

	return IRQ_HANDLED;
}

/**
 * __udma_alloc_gp_rflow_range - alloc range of GP RX flows
 * @ud: UDMA device
 * @from: Start the search from this flow id number
 * @cnt: Number of consecutive flow ids to allocate
 *
 * Allocate range of RX flow ids for future use, those flows can be requested
 * only using explicit flow id number. if @from is set to -1 it will try to find
 * first free range. if @from is positive value it will force allocation only
 * of the specified range of flows.
 *
 * Returns -ENOMEM if can't find free range.
 * -EEXIST if requested range is busy.
 * -EINVAL if wrong input values passed.
 * Returns flow id on success.
 */
static int __udma_alloc_gp_rflow_range(struct udma_dev *ud, int from, int cnt)
{
	int start, tmp_from;
	DECLARE_BITMAP(tmp, K3_UDMA_MAX_RFLOWS);

	tmp_from = from;
	if (tmp_from < 0)
		tmp_from = ud->rchan_cnt;
	/* default flows can't be allocated and accessible only by id */
	if (tmp_from < ud->rchan_cnt)
		return -EINVAL;

	if (tmp_from + cnt > ud->rflow_cnt)
		return -EINVAL;

	bitmap_or(tmp, ud->rflow_gp_map, ud->rflow_gp_map_allocated,
		  ud->rflow_cnt);

	start = bitmap_find_next_zero_area(tmp,
					   ud->rflow_cnt,
					   tmp_from, cnt, 0);
	if (start >= ud->rflow_cnt)
		return -ENOMEM;

	if (from >= 0 && start != from)
		return -EEXIST;

	bitmap_set(ud->rflow_gp_map_allocated, start, cnt);
	return start;
}

static int __udma_free_gp_rflow_range(struct udma_dev *ud, int from, int cnt)
{
	if (from < ud->rchan_cnt)
		return -EINVAL;
	if (from + cnt > ud->rflow_cnt)
		return -EINVAL;

	bitmap_clear(ud->rflow_gp_map_allocated, from, cnt);
	return 0;
}

static struct udma_rflow *__udma_get_rflow(struct udma_dev *ud, int id)
{
	/*
	 * Attempt to request rflow by ID can be made for any rflow
	 * if not in use with assumption that caller knows what's doing.
	 * TI-SCI FW will perform additional permission check ant way, it's
	 * safe
	 */

	if (id < 0 || id >= ud->rflow_cnt)
		return ERR_PTR(-ENOENT);

	if (test_bit(id, ud->rflow_in_use))
		return ERR_PTR(-ENOENT);

	/* GP rflow has to be allocated first */
	if (!test_bit(id, ud->rflow_gp_map) &&
	    !test_bit(id, ud->rflow_gp_map_allocated))
		return ERR_PTR(-EINVAL);

	dev_dbg(ud->dev, "get rflow%d\n", id);
	set_bit(id, ud->rflow_in_use);
	return &ud->rflows[id];
}

static void __udma_put_rflow(struct udma_dev *ud, struct udma_rflow *rflow)
{
	if (!test_bit(rflow->id, ud->rflow_in_use)) {
		dev_err(ud->dev, "attempt to put unused rflow%d\n", rflow->id);
		return;
	}

	dev_dbg(ud->dev, "put rflow%d\n", rflow->id);
	clear_bit(rflow->id, ud->rflow_in_use);
}

#define UDMA_RESERVE_RESOURCE(res)					\
static struct udma_##res *__udma_reserve_##res(struct udma_dev *ud,	\
					       enum udma_tp_level tpl,	\
					       int id)			\
{									\
	if (id >= 0) {							\
		if (test_bit(id, ud->res##_map)) {			\
			dev_err(ud->dev, "res##%d is in use\n", id);	\
			return ERR_PTR(-ENOENT);			\
		}							\
	} else {							\
		int start;						\
									\
		if (tpl >= ud->match_data->tpl_levels)			\
			tpl = ud->match_data->tpl_levels - 1;		\
									\
		start = ud->match_data->level_start_idx[tpl];		\
									\
		id = find_next_zero_bit(ud->res##_map, ud->res##_cnt,	\
					start);				\
		if (id == ud->res##_cnt) {				\
			return ERR_PTR(-ENOENT);			\
		}							\
	}								\
									\
	set_bit(id, ud->res##_map);					\
	return &ud->res##s[id];						\
}

UDMA_RESERVE_RESOURCE(tchan);
UDMA_RESERVE_RESOURCE(rchan);

static int udma_get_tchan(struct udma_chan *uc)
{
	struct udma_dev *ud = uc->ud;

	if (uc->tchan) {
		dev_dbg(ud->dev, "chan%d: already have tchan%d allocated\n",
			uc->id, uc->tchan->id);
		return 0;
	}

	uc->tchan = __udma_reserve_tchan(ud, uc->config.channel_tpl, -1);
	if (IS_ERR(uc->tchan))
		return PTR_ERR(uc->tchan);

	return 0;
}

static int udma_get_rchan(struct udma_chan *uc)
{
	struct udma_dev *ud = uc->ud;

	if (uc->rchan) {
		dev_dbg(ud->dev, "chan%d: already have rchan%d allocated\n",
			uc->id, uc->rchan->id);
		return 0;
	}

	uc->rchan = __udma_reserve_rchan(ud, uc->config.channel_tpl, -1);
	if (IS_ERR(uc->rchan))
		return PTR_ERR(uc->rchan);

	return 0;
}

static int udma_get_chan_pair(struct udma_chan *uc)
{
	struct udma_dev *ud = uc->ud;
	const struct udma_match_data *match_data = ud->match_data;
	int chan_id, end;

	if ((uc->tchan && uc->rchan) && uc->tchan->id == uc->rchan->id) {
		dev_info(ud->dev, "chan%d: already have %d pair allocated\n",
			 uc->id, uc->tchan->id);
		return 0;
	}

	if (uc->tchan) {
		dev_err(ud->dev, "chan%d: already have tchan%d allocated\n",
			uc->id, uc->tchan->id);
		return -EBUSY;
	} else if (uc->rchan) {
		dev_err(ud->dev, "chan%d: already have rchan%d allocated\n",
			uc->id, uc->rchan->id);
		return -EBUSY;
	}

	/* Can be optimized, but let's have it like this for now */
	end = min(ud->tchan_cnt, ud->rchan_cnt);
	/* Try to use the highest TPL channel pair for MEM_TO_MEM channels */
	chan_id = match_data->level_start_idx[match_data->tpl_levels - 1];
	for (; chan_id < end; chan_id++) {
		if (!test_bit(chan_id, ud->tchan_map) &&
		    !test_bit(chan_id, ud->rchan_map))
			break;
	}

	if (chan_id == end)
		return -ENOENT;

	set_bit(chan_id, ud->tchan_map);
	set_bit(chan_id, ud->rchan_map);
	uc->tchan = &ud->tchans[chan_id];
	uc->rchan = &ud->rchans[chan_id];

	return 0;
}

static int udma_get_rflow(struct udma_chan *uc, int flow_id)
{
	struct udma_dev *ud = uc->ud;

	if (!uc->rchan) {
		dev_err(ud->dev, "chan%d: does not have rchan??\n", uc->id);
		return -EINVAL;
	}

	if (uc->rflow) {
		dev_dbg(ud->dev, "chan%d: already have rflow%d allocated\n",
			uc->id, uc->rflow->id);
		return 0;
	}

	uc->rflow = __udma_get_rflow(ud, flow_id);
	if (IS_ERR(uc->rflow))
		return PTR_ERR(uc->rflow);

	return 0;
}

static void udma_put_rchan(struct udma_chan *uc)
{
	struct udma_dev *ud = uc->ud;

	if (uc->rchan) {
		dev_dbg(ud->dev, "chan%d: put rchan%d\n", uc->id,
			uc->rchan->id);
		clear_bit(uc->rchan->id, ud->rchan_map);
		uc->rchan = NULL;
	}
}

static void udma_put_tchan(struct udma_chan *uc)
{
	struct udma_dev *ud = uc->ud;

	if (uc->tchan) {
		dev_dbg(ud->dev, "chan%d: put tchan%d\n", uc->id,
			uc->tchan->id);
		clear_bit(uc->tchan->id, ud->tchan_map);
		uc->tchan = NULL;
	}
}

static void udma_put_rflow(struct udma_chan *uc)
{
	struct udma_dev *ud = uc->ud;

	if (uc->rflow) {
		dev_dbg(ud->dev, "chan%d: put rflow%d\n", uc->id,
			uc->rflow->id);
		__udma_put_rflow(ud, uc->rflow);
		uc->rflow = NULL;
	}
}

static void udma_free_tx_resources(struct udma_chan *uc)
{
	if (!uc->tchan)
		return;

	k3_ringacc_ring_free(uc->tchan->t_ring);
	k3_ringacc_ring_free(uc->tchan->tc_ring);
	uc->tchan->t_ring = NULL;
	uc->tchan->tc_ring = NULL;

	udma_put_tchan(uc);
}

static int udma_alloc_tx_resources(struct udma_chan *uc)
{
	struct k3_ring_cfg ring_cfg;
	struct udma_dev *ud = uc->ud;
	int ret;

	ret = udma_get_tchan(uc);
	if (ret)
		return ret;

	uc->tchan->t_ring = k3_ringacc_request_ring(ud->ringacc,
						    uc->tchan->id, 0);
	if (!uc->tchan->t_ring) {
		ret = -EBUSY;
		goto err_tx_ring;
	}

	uc->tchan->tc_ring = k3_ringacc_request_ring(ud->ringacc, -1, 0);
	if (!uc->tchan->tc_ring) {
		ret = -EBUSY;
		goto err_txc_ring;
	}

	memset(&ring_cfg, 0, sizeof(ring_cfg));
	ring_cfg.size = K3_UDMA_DEFAULT_RING_SIZE;
	ring_cfg.elm_size = K3_RINGACC_RING_ELSIZE_8;
	ring_cfg.mode = K3_RINGACC_RING_MODE_MESSAGE;

	ret = k3_ringacc_ring_cfg(uc->tchan->t_ring, &ring_cfg);
	ret |= k3_ringacc_ring_cfg(uc->tchan->tc_ring, &ring_cfg);

	if (ret)
		goto err_ringcfg;

	return 0;

err_ringcfg:
	k3_ringacc_ring_free(uc->tchan->tc_ring);
	uc->tchan->tc_ring = NULL;
err_txc_ring:
	k3_ringacc_ring_free(uc->tchan->t_ring);
	uc->tchan->t_ring = NULL;
err_tx_ring:
	udma_put_tchan(uc);

	return ret;
}

static void udma_free_rx_resources(struct udma_chan *uc)
{
	if (!uc->rchan)
		return;

	if (uc->rflow) {
		struct udma_rflow *rflow = uc->rflow;

		k3_ringacc_ring_free(rflow->fd_ring);
		k3_ringacc_ring_free(rflow->r_ring);
		rflow->fd_ring = NULL;
		rflow->r_ring = NULL;

		udma_put_rflow(uc);
	}

	udma_put_rchan(uc);
}

static int udma_alloc_rx_resources(struct udma_chan *uc)
{
	struct udma_dev *ud = uc->ud;
	struct k3_ring_cfg ring_cfg;
	struct udma_rflow *rflow;
	int fd_ring_id;
	int ret;

	ret = udma_get_rchan(uc);
	if (ret)
		return ret;

	/* For MEM_TO_MEM we don't need rflow or rings */
	if (uc->config.dir == DMA_MEM_TO_MEM)
		return 0;

	ret = udma_get_rflow(uc, uc->rchan->id);
	if (ret) {
		ret = -EBUSY;
		goto err_rflow;
	}

	rflow = uc->rflow;
	fd_ring_id = ud->tchan_cnt + ud->echan_cnt + uc->rchan->id;
	rflow->fd_ring = k3_ringacc_request_ring(ud->ringacc, fd_ring_id, 0);
	if (!rflow->fd_ring) {
		ret = -EBUSY;
		goto err_rx_ring;
	}

	rflow->r_ring = k3_ringacc_request_ring(ud->ringacc, -1, 0);
	if (!rflow->r_ring) {
		ret = -EBUSY;
		goto err_rxc_ring;
	}

	memset(&ring_cfg, 0, sizeof(ring_cfg));

	if (uc->config.pkt_mode)
		ring_cfg.size = SG_MAX_SEGMENTS;
	else
		ring_cfg.size = K3_UDMA_DEFAULT_RING_SIZE;

	ring_cfg.elm_size = K3_RINGACC_RING_ELSIZE_8;
	ring_cfg.mode = K3_RINGACC_RING_MODE_MESSAGE;

	ret = k3_ringacc_ring_cfg(rflow->fd_ring, &ring_cfg);
	ring_cfg.size = K3_UDMA_DEFAULT_RING_SIZE;
	ret |= k3_ringacc_ring_cfg(rflow->r_ring, &ring_cfg);

	if (ret)
		goto err_ringcfg;

	return 0;

err_ringcfg:
	k3_ringacc_ring_free(rflow->r_ring);
	rflow->r_ring = NULL;
err_rxc_ring:
	k3_ringacc_ring_free(rflow->fd_ring);
	rflow->fd_ring = NULL;
err_rx_ring:
	udma_put_rflow(uc);
err_rflow:
	udma_put_rchan(uc);

	return ret;
}

#define TISCI_TCHAN_VALID_PARAMS (				\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_PAUSE_ON_ERR_VALID |	\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_TX_FILT_EINFO_VALID |	\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_TX_FILT_PSWORDS_VALID |	\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_CHAN_TYPE_VALID |		\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_TX_SUPR_TDPKT_VALID |	\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_FETCH_SIZE_VALID |		\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_CQ_QNUM_VALID)

#define TISCI_RCHAN_VALID_PARAMS (				\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_PAUSE_ON_ERR_VALID |	\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_FETCH_SIZE_VALID |		\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_CQ_QNUM_VALID |		\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_CHAN_TYPE_VALID |		\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_RX_IGNORE_SHORT_VALID |	\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_RX_IGNORE_LONG_VALID |	\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_RX_FLOWID_START_VALID |	\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_RX_FLOWID_CNT_VALID)

static int udma_tisci_m2m_channel_config(struct udma_chan *uc)
{
	struct udma_dev *ud = uc->ud;
	struct udma_tisci_rm *tisci_rm = &ud->tisci_rm;
	const struct ti_sci_rm_udmap_ops *tisci_ops = tisci_rm->tisci_udmap_ops;
	struct udma_tchan *tchan = uc->tchan;
	struct udma_rchan *rchan = uc->rchan;
	int ret = 0;

	/* Non synchronized - mem to mem type of transfer */
	int tc_ring = k3_ringacc_get_ring_id(tchan->tc_ring);
	struct ti_sci_msg_rm_udmap_tx_ch_cfg req_tx = { 0 };
	struct ti_sci_msg_rm_udmap_rx_ch_cfg req_rx = { 0 };

	req_tx.valid_params = TISCI_TCHAN_VALID_PARAMS;
	req_tx.nav_id = tisci_rm->tisci_dev_id;
	req_tx.index = tchan->id;
	req_tx.tx_chan_type = TI_SCI_RM_UDMAP_CHAN_TYPE_3RDP_BCOPY_PBRR;
	req_tx.tx_fetch_size = sizeof(struct cppi5_desc_hdr_t) >> 2;
	req_tx.txcq_qnum = tc_ring;

	ret = tisci_ops->tx_ch_cfg(tisci_rm->tisci, &req_tx);
	if (ret) {
		dev_err(ud->dev, "tchan%d cfg failed %d\n", tchan->id, ret);
		return ret;
	}

	req_rx.valid_params = TISCI_RCHAN_VALID_PARAMS;
	req_rx.nav_id = tisci_rm->tisci_dev_id;
	req_rx.index = rchan->id;
	req_rx.rx_fetch_size = sizeof(struct cppi5_desc_hdr_t) >> 2;
	req_rx.rxcq_qnum = tc_ring;
	req_rx.rx_chan_type = TI_SCI_RM_UDMAP_CHAN_TYPE_3RDP_BCOPY_PBRR;

	ret = tisci_ops->rx_ch_cfg(tisci_rm->tisci, &req_rx);
	if (ret)
		dev_err(ud->dev, "rchan%d alloc failed %d\n", rchan->id, ret);

	return ret;
}

static int udma_tisci_tx_channel_config(struct udma_chan *uc)
{
	struct udma_dev *ud = uc->ud;
	struct udma_tisci_rm *tisci_rm = &ud->tisci_rm;
	const struct ti_sci_rm_udmap_ops *tisci_ops = tisci_rm->tisci_udmap_ops;
	struct udma_tchan *tchan = uc->tchan;
	int tc_ring = k3_ringacc_get_ring_id(tchan->tc_ring);
	struct ti_sci_msg_rm_udmap_tx_ch_cfg req_tx = { 0 };
	u32 mode, fetch_size;
	int ret = 0;

	if (uc->config.pkt_mode) {
		mode = TI_SCI_RM_UDMAP_CHAN_TYPE_PKT_PBRR;
		fetch_size = cppi5_hdesc_calc_size(uc->config.needs_epib,
						   uc->config.psd_size, 0);
	} else {
		mode = TI_SCI_RM_UDMAP_CHAN_TYPE_3RDP_PBRR;
		fetch_size = sizeof(struct cppi5_desc_hdr_t);
	}

	req_tx.valid_params = TISCI_TCHAN_VALID_PARAMS;
	req_tx.nav_id = tisci_rm->tisci_dev_id;
	req_tx.index = tchan->id;
	req_tx.tx_chan_type = mode;
	req_tx.tx_supr_tdpkt = uc->config.notdpkt;
	req_tx.tx_fetch_size = fetch_size >> 2;
	req_tx.txcq_qnum = tc_ring;

	ret = tisci_ops->tx_ch_cfg(tisci_rm->tisci, &req_tx);
	if (ret)
		dev_err(ud->dev, "tchan%d cfg failed %d\n", tchan->id, ret);

	return ret;
}

static int udma_tisci_rx_channel_config(struct udma_chan *uc)
{
	struct udma_dev *ud = uc->ud;
	struct udma_tisci_rm *tisci_rm = &ud->tisci_rm;
	const struct ti_sci_rm_udmap_ops *tisci_ops = tisci_rm->tisci_udmap_ops;
	struct udma_rchan *rchan = uc->rchan;
	int fd_ring = k3_ringacc_get_ring_id(uc->rflow->fd_ring);
	int rx_ring = k3_ringacc_get_ring_id(uc->rflow->r_ring);
	struct ti_sci_msg_rm_udmap_rx_ch_cfg req_rx = { 0 };
	struct ti_sci_msg_rm_udmap_flow_cfg flow_req = { 0 };
	u32 mode, fetch_size;
	int ret = 0;

	if (uc->config.pkt_mode) {
		mode = TI_SCI_RM_UDMAP_CHAN_TYPE_PKT_PBRR;
		fetch_size = cppi5_hdesc_calc_size(uc->config.needs_epib,
						   uc->config.psd_size, 0);
	} else {
		mode = TI_SCI_RM_UDMAP_CHAN_TYPE_3RDP_PBRR;
		fetch_size = sizeof(struct cppi5_desc_hdr_t);
	}

	req_rx.valid_params = TISCI_RCHAN_VALID_PARAMS;
	req_rx.nav_id = tisci_rm->tisci_dev_id;
	req_rx.index = rchan->id;
	req_rx.rx_fetch_size =  fetch_size >> 2;
	req_rx.rxcq_qnum = rx_ring;
	req_rx.rx_chan_type = mode;

	ret = tisci_ops->rx_ch_cfg(tisci_rm->tisci, &req_rx);
	if (ret) {
		dev_err(ud->dev, "rchan%d cfg failed %d\n", rchan->id, ret);
		return ret;
	}

	flow_req.valid_params =
		TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_EINFO_PRESENT_VALID |
		TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_PSINFO_PRESENT_VALID |
		TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_ERROR_HANDLING_VALID |
		TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_DESC_TYPE_VALID |
		TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_DEST_QNUM_VALID |
		TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_SRC_TAG_HI_SEL_VALID |
		TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_SRC_TAG_LO_SEL_VALID |
		TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_DEST_TAG_HI_SEL_VALID |
		TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_DEST_TAG_LO_SEL_VALID |
		TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_FDQ0_SZ0_QNUM_VALID |
		TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_FDQ1_QNUM_VALID |
		TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_FDQ2_QNUM_VALID |
		TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_FDQ3_QNUM_VALID;

	flow_req.nav_id = tisci_rm->tisci_dev_id;
	flow_req.flow_index = rchan->id;

	if (uc->config.needs_epib)
		flow_req.rx_einfo_present = 1;
	else
		flow_req.rx_einfo_present = 0;
	if (uc->config.psd_size)
		flow_req.rx_psinfo_present = 1;
	else
		flow_req.rx_psinfo_present = 0;
	flow_req.rx_error_handling = 1;
	flow_req.rx_dest_qnum = rx_ring;
	flow_req.rx_src_tag_hi_sel = UDMA_RFLOW_SRCTAG_NONE;
	flow_req.rx_src_tag_lo_sel = UDMA_RFLOW_SRCTAG_SRC_TAG;
	flow_req.rx_dest_tag_hi_sel = UDMA_RFLOW_DSTTAG_DST_TAG_HI;
	flow_req.rx_dest_tag_lo_sel = UDMA_RFLOW_DSTTAG_DST_TAG_LO;
	flow_req.rx_fdq0_sz0_qnum = fd_ring;
	flow_req.rx_fdq1_qnum = fd_ring;
	flow_req.rx_fdq2_qnum = fd_ring;
	flow_req.rx_fdq3_qnum = fd_ring;

	ret = tisci_ops->rx_flow_cfg(tisci_rm->tisci, &flow_req);

	if (ret)
		dev_err(ud->dev, "flow%d config failed: %d\n", rchan->id, ret);

	return 0;
}

static int udma_alloc_chan_resources(struct dma_chan *chan)
{
	struct udma_chan *uc = to_udma_chan(chan);
	struct udma_dev *ud = to_udma_dev(chan->device);
	const struct udma_match_data *match_data = ud->match_data;
	struct k3_ring *irq_ring;
	u32 irq_udma_idx;
	int ret;

	if (uc->config.pkt_mode || uc->config.dir == DMA_MEM_TO_MEM) {
		uc->use_dma_pool = true;
		/* in case of MEM_TO_MEM we have maximum of two TRs */
		if (uc->config.dir == DMA_MEM_TO_MEM) {
			uc->config.hdesc_size = cppi5_trdesc_calc_size(
					sizeof(struct cppi5_tr_type15_t), 2);
			uc->config.pkt_mode = false;
		}
	}

	if (uc->use_dma_pool) {
		uc->hdesc_pool = dma_pool_create(uc->name, ud->ddev.dev,
						 uc->config.hdesc_size,
						 ud->desc_align,
						 0);
		if (!uc->hdesc_pool) {
			dev_err(ud->ddev.dev,
				"Descriptor pool allocation failed\n");
			uc->use_dma_pool = false;
			return -ENOMEM;
		}
	}

	/*
	 * Make sure that the completion is in a known state:
	 * No teardown, the channel is idle
	 */
	reinit_completion(&uc->teardown_completed);
	complete_all(&uc->teardown_completed);
	uc->state = UDMA_CHAN_IS_IDLE;

	switch (uc->config.dir) {
	case DMA_MEM_TO_MEM:
		/* Non synchronized - mem to mem type of transfer */
		dev_dbg(uc->ud->dev, "%s: chan%d as MEM-to-MEM\n", __func__,
			uc->id);

		ret = udma_get_chan_pair(uc);
		if (ret)
			return ret;

		ret = udma_alloc_tx_resources(uc);
		if (ret)
			return ret;

		ret = udma_alloc_rx_resources(uc);
		if (ret) {
			udma_free_tx_resources(uc);
			return ret;
		}

		uc->config.src_thread = ud->psil_base + uc->tchan->id;
		uc->config.dst_thread = (ud->psil_base + uc->rchan->id) |
					K3_PSIL_DST_THREAD_ID_OFFSET;

		irq_ring = uc->tchan->tc_ring;
		irq_udma_idx = uc->tchan->id;

		ret = udma_tisci_m2m_channel_config(uc);
		break;
	case DMA_MEM_TO_DEV:
		/* Slave transfer synchronized - mem to dev (TX) trasnfer */
		dev_dbg(uc->ud->dev, "%s: chan%d as MEM-to-DEV\n", __func__,
			uc->id);

		ret = udma_alloc_tx_resources(uc);
		if (ret) {
			uc->config.remote_thread_id = -1;
			return ret;
		}

		uc->config.src_thread = ud->psil_base + uc->tchan->id;
		uc->config.dst_thread = uc->config.remote_thread_id;
		uc->config.dst_thread |= K3_PSIL_DST_THREAD_ID_OFFSET;

		irq_ring = uc->tchan->tc_ring;
		irq_udma_idx = uc->tchan->id;

		ret = udma_tisci_tx_channel_config(uc);
		break;
	case DMA_DEV_TO_MEM:
		/* Slave transfer synchronized - dev to mem (RX) trasnfer */
		dev_dbg(uc->ud->dev, "%s: chan%d as DEV-to-MEM\n", __func__,
			uc->id);

		ret = udma_alloc_rx_resources(uc);
		if (ret) {
			uc->config.remote_thread_id = -1;
			return ret;
		}

		uc->config.src_thread = uc->config.remote_thread_id;
		uc->config.dst_thread = (ud->psil_base + uc->rchan->id) |
					K3_PSIL_DST_THREAD_ID_OFFSET;

		irq_ring = uc->rflow->r_ring;
		irq_udma_idx = match_data->rchan_oes_offset + uc->rchan->id;

		ret = udma_tisci_rx_channel_config(uc);
		break;
	default:
		/* Can not happen */
		dev_err(uc->ud->dev, "%s: chan%d invalid direction (%u)\n",
			__func__, uc->id, uc->config.dir);
		return -EINVAL;
	}

	/* check if the channel configuration was successful */
	if (ret)
		goto err_res_free;

	if (udma_is_chan_running(uc)) {
		dev_warn(ud->dev, "chan%d: is running!\n", uc->id);
		udma_stop(uc);
		if (udma_is_chan_running(uc)) {
			dev_err(ud->dev, "chan%d: won't stop!\n", uc->id);
			goto err_res_free;
		}
	}

	/* PSI-L pairing */
	ret = navss_psil_pair(ud, uc->config.src_thread, uc->config.dst_thread);
	if (ret) {
		dev_err(ud->dev, "PSI-L pairing failed: 0x%04x -> 0x%04x\n",
			uc->config.src_thread, uc->config.dst_thread);
		goto err_res_free;
	}

	uc->psil_paired = true;

	uc->irq_num_ring = k3_ringacc_get_ring_irq_num(irq_ring);
	if (uc->irq_num_ring <= 0) {
		dev_err(ud->dev, "Failed to get ring irq (index: %u)\n",
			k3_ringacc_get_ring_id(irq_ring));
		ret = -EINVAL;
		goto err_psi_free;
	}

	ret = request_irq(uc->irq_num_ring, udma_ring_irq_handler,
			  IRQF_TRIGGER_HIGH, uc->name, uc);
	if (ret) {
		dev_err(ud->dev, "chan%d: ring irq request failed\n", uc->id);
		goto err_irq_free;
	}

	/* Event from UDMA (TR events) only needed for slave TR mode channels */
	if (is_slave_direction(uc->config.dir) && !uc->config.pkt_mode) {
		uc->irq_num_udma = ti_sci_inta_msi_get_virq(ud->dev,
							    irq_udma_idx);
		if (uc->irq_num_udma <= 0) {
			dev_err(ud->dev, "Failed to get udma irq (index: %u)\n",
				irq_udma_idx);
			free_irq(uc->irq_num_ring, uc);
			ret = -EINVAL;
			goto err_irq_free;
		}

		ret = request_irq(uc->irq_num_udma, udma_udma_irq_handler, 0,
				  uc->name, uc);
		if (ret) {
			dev_err(ud->dev, "chan%d: UDMA irq request failed\n",
				uc->id);
			free_irq(uc->irq_num_ring, uc);
			goto err_irq_free;
		}
	} else {
		uc->irq_num_udma = 0;
	}

	udma_reset_rings(uc);

	INIT_DELAYED_WORK_ONSTACK(&uc->tx_drain.work,
				  udma_check_tx_completion);
	return 0;

err_irq_free:
	uc->irq_num_ring = 0;
	uc->irq_num_udma = 0;
err_psi_free:
	navss_psil_unpair(ud, uc->config.src_thread, uc->config.dst_thread);
	uc->psil_paired = false;
err_res_free:
	udma_free_tx_resources(uc);
	udma_free_rx_resources(uc);

	udma_reset_uchan(uc);

	if (uc->use_dma_pool) {
		dma_pool_destroy(uc->hdesc_pool);
		uc->use_dma_pool = false;
	}

	return ret;
}

static int udma_slave_config(struct dma_chan *chan,
			     struct dma_slave_config *cfg)
{
	struct udma_chan *uc = to_udma_chan(chan);

	memcpy(&uc->cfg, cfg, sizeof(uc->cfg));

	return 0;
}

static struct udma_desc *udma_alloc_tr_desc(struct udma_chan *uc,
					    size_t tr_size, int tr_count,
					    enum dma_transfer_direction dir)
{
	struct udma_hwdesc *hwdesc;
	struct cppi5_desc_hdr_t *tr_desc;
	struct udma_desc *d;
	u32 reload_count = 0;
	u32 ring_id;

	switch (tr_size) {
	case 16:
	case 32:
	case 64:
	case 128:
		break;
	default:
		dev_err(uc->ud->dev, "Unsupported TR size of %zu\n", tr_size);
		return NULL;
	}

	/* We have only one descriptor containing multiple TRs */
	d = kzalloc(sizeof(*d) + sizeof(d->hwdesc[0]), GFP_NOWAIT);
	if (!d)
		return NULL;

	d->sglen = tr_count;

	d->hwdesc_count = 1;
	hwdesc = &d->hwdesc[0];

	/* Allocate memory for DMA ring descriptor */
	if (uc->use_dma_pool) {
		hwdesc->cppi5_desc_size = uc->config.hdesc_size;
		hwdesc->cppi5_desc_vaddr = dma_pool_zalloc(uc->hdesc_pool,
						GFP_NOWAIT,
						&hwdesc->cppi5_desc_paddr);
	} else {
		hwdesc->cppi5_desc_size = cppi5_trdesc_calc_size(tr_size,
								 tr_count);
		hwdesc->cppi5_desc_size = ALIGN(hwdesc->cppi5_desc_size,
						uc->ud->desc_align);
		hwdesc->cppi5_desc_vaddr = dma_alloc_coherent(uc->ud->dev,
						hwdesc->cppi5_desc_size,
						&hwdesc->cppi5_desc_paddr,
						GFP_NOWAIT);
	}

	if (!hwdesc->cppi5_desc_vaddr) {
		kfree(d);
		return NULL;
	}

	/* Start of the TR req records */
	hwdesc->tr_req_base = hwdesc->cppi5_desc_vaddr + tr_size;
	/* Start address of the TR response array */
	hwdesc->tr_resp_base = hwdesc->tr_req_base + tr_size * tr_count;

	tr_desc = hwdesc->cppi5_desc_vaddr;

	if (uc->cyclic)
		reload_count = CPPI5_INFO0_TRDESC_RLDCNT_INFINITE;

	if (dir == DMA_DEV_TO_MEM)
		ring_id = k3_ringacc_get_ring_id(uc->rflow->r_ring);
	else
		ring_id = k3_ringacc_get_ring_id(uc->tchan->tc_ring);

	cppi5_trdesc_init(tr_desc, tr_count, tr_size, 0, reload_count);
	cppi5_desc_set_pktids(tr_desc, uc->id,
			      CPPI5_INFO1_DESC_FLOWID_DEFAULT);
	cppi5_desc_set_retpolicy(tr_desc, 0, ring_id);

	return d;
}

static struct udma_desc *
udma_prep_slave_sg_tr(struct udma_chan *uc, struct scatterlist *sgl,
		      unsigned int sglen, enum dma_transfer_direction dir,
		      unsigned long tx_flags, void *context)
{
	enum dma_slave_buswidth dev_width;
	struct scatterlist *sgent;
	struct udma_desc *d;
	size_t tr_size;
	struct cppi5_tr_type1_t *tr_req = NULL;
	unsigned int i;
	u32 burst;

	if (dir == DMA_DEV_TO_MEM) {
		dev_width = uc->cfg.src_addr_width;
		burst = uc->cfg.src_maxburst;
	} else if (dir == DMA_MEM_TO_DEV) {
		dev_width = uc->cfg.dst_addr_width;
		burst = uc->cfg.dst_maxburst;
	} else {
		dev_err(uc->ud->dev, "%s: bad direction?\n", __func__);
		return NULL;
	}

	if (!burst)
		burst = 1;

	/* Now allocate and setup the descriptor. */
	tr_size = sizeof(struct cppi5_tr_type1_t);
	d = udma_alloc_tr_desc(uc, tr_size, sglen, dir);
	if (!d)
		return NULL;

	d->sglen = sglen;

	tr_req = d->hwdesc[0].tr_req_base;
	for_each_sg(sgl, sgent, sglen, i) {
		d->residue += sg_dma_len(sgent);

		cppi5_tr_init(&tr_req[i].flags, CPPI5_TR_TYPE1, false, false,
			      CPPI5_TR_EVENT_SIZE_COMPLETION, 0);
		cppi5_tr_csf_set(&tr_req[i].flags, CPPI5_TR_CSF_SUPR_EVT);

		tr_req[i].addr = sg_dma_address(sgent);
		tr_req[i].icnt0 = burst * dev_width;
		tr_req[i].dim1 = burst * dev_width;
		tr_req[i].icnt1 = sg_dma_len(sgent) / tr_req[i].icnt0;
	}

	cppi5_tr_csf_set(&tr_req[i - 1].flags, CPPI5_TR_CSF_EOP);

	return d;
}

static int udma_configure_statictr(struct udma_chan *uc, struct udma_desc *d,
				   enum dma_slave_buswidth dev_width,
				   u16 elcnt)
{
	if (uc->config.ep_type != PSIL_EP_PDMA_XY)
		return 0;

	/* Bus width translates to the element size (ES) */
	switch (dev_width) {
	case DMA_SLAVE_BUSWIDTH_1_BYTE:
		d->static_tr.elsize = 0;
		break;
	case DMA_SLAVE_BUSWIDTH_2_BYTES:
		d->static_tr.elsize = 1;
		break;
	case DMA_SLAVE_BUSWIDTH_3_BYTES:
		d->static_tr.elsize = 2;
		break;
	case DMA_SLAVE_BUSWIDTH_4_BYTES:
		d->static_tr.elsize = 3;
		break;
	case DMA_SLAVE_BUSWIDTH_8_BYTES:
		d->static_tr.elsize = 4;
		break;
	default: /* not reached */
		return -EINVAL;
	}

	d->static_tr.elcnt = elcnt;

	/*
	 * PDMA must to close the packet when the channel is in packet mode.
	 * For TR mode when the channel is not cyclic we also need PDMA to close
	 * the packet otherwise the transfer will stall because PDMA holds on
	 * the data it has received from the peripheral.
	 */
	if (uc->config.pkt_mode || !uc->cyclic) {
		unsigned int div = dev_width * elcnt;

		if (uc->cyclic)
			d->static_tr.bstcnt = d->residue / d->sglen / div;
		else
			d->static_tr.bstcnt = d->residue / div;

		if (uc->config.dir == DMA_DEV_TO_MEM &&
		    d->static_tr.bstcnt > uc->ud->match_data->statictr_z_mask)
			return -EINVAL;
	} else {
		d->static_tr.bstcnt = 0;
	}

	return 0;
}

static struct udma_desc *
udma_prep_slave_sg_pkt(struct udma_chan *uc, struct scatterlist *sgl,
		       unsigned int sglen, enum dma_transfer_direction dir,
		       unsigned long tx_flags, void *context)
{
	struct scatterlist *sgent;
	struct cppi5_host_desc_t *h_desc = NULL;
	struct udma_desc *d;
	u32 ring_id;
	unsigned int i;

	d = kzalloc(sizeof(*d) + sglen * sizeof(d->hwdesc[0]), GFP_NOWAIT);
	if (!d)
		return NULL;

	d->sglen = sglen;
	d->hwdesc_count = sglen;

	if (dir == DMA_DEV_TO_MEM)
		ring_id = k3_ringacc_get_ring_id(uc->rflow->r_ring);
	else
		ring_id = k3_ringacc_get_ring_id(uc->tchan->tc_ring);

	for_each_sg(sgl, sgent, sglen, i) {
		struct udma_hwdesc *hwdesc = &d->hwdesc[i];
		dma_addr_t sg_addr = sg_dma_address(sgent);
		struct cppi5_host_desc_t *desc;
		size_t sg_len = sg_dma_len(sgent);

		hwdesc->cppi5_desc_vaddr = dma_pool_zalloc(uc->hdesc_pool,
						GFP_NOWAIT,
						&hwdesc->cppi5_desc_paddr);
		if (!hwdesc->cppi5_desc_vaddr) {
			dev_err(uc->ud->dev,
				"descriptor%d allocation failed\n", i);

			udma_free_hwdesc(uc, d);
			kfree(d);
			return NULL;
		}

		d->residue += sg_len;
		hwdesc->cppi5_desc_size = uc->config.hdesc_size;
		desc = hwdesc->cppi5_desc_vaddr;

		if (i == 0) {
			cppi5_hdesc_init(desc, 0, 0);
			/* Flow and Packed ID */
			cppi5_desc_set_pktids(&desc->hdr, uc->id,
					      CPPI5_INFO1_DESC_FLOWID_DEFAULT);
			cppi5_desc_set_retpolicy(&desc->hdr, 0, ring_id);
		} else {
			cppi5_hdesc_reset_hbdesc(desc);
			cppi5_desc_set_retpolicy(&desc->hdr, 0, 0xffff);
		}

		/* attach the sg buffer to the descriptor */
		cppi5_hdesc_attach_buf(desc, sg_addr, sg_len, sg_addr, sg_len);

		/* Attach link as host buffer descriptor */
		if (h_desc)
			cppi5_hdesc_link_hbdesc(h_desc,
						hwdesc->cppi5_desc_paddr);

		if (dir == DMA_MEM_TO_DEV)
			h_desc = desc;
	}

	if (d->residue >= SZ_4M) {
		dev_err(uc->ud->dev,
			"%s: Transfer size %u is over the supported 4M range\n",
			__func__, d->residue);
		udma_free_hwdesc(uc, d);
		kfree(d);
		return NULL;
	}

	h_desc = d->hwdesc[0].cppi5_desc_vaddr;
	cppi5_hdesc_set_pktlen(h_desc, d->residue);

	return d;
}

static int udma_attach_metadata(struct dma_async_tx_descriptor *desc,
				void *data, size_t len)
{
	struct udma_desc *d = to_udma_desc(desc);
	struct udma_chan *uc = to_udma_chan(desc->chan);
	struct cppi5_host_desc_t *h_desc;
	u32 psd_size = len;
	u32 flags = 0;

	if (!uc->config.pkt_mode || !uc->config.metadata_size)
		return -ENOTSUPP;

	if (!data || len > uc->config.metadata_size)
		return -EINVAL;

	if (uc->config.needs_epib && len < CPPI5_INFO0_HDESC_EPIB_SIZE)
		return -EINVAL;

	h_desc = d->hwdesc[0].cppi5_desc_vaddr;
	if (d->dir == DMA_MEM_TO_DEV)
		memcpy(h_desc->epib, data, len);

	if (uc->config.needs_epib)
		psd_size -= CPPI5_INFO0_HDESC_EPIB_SIZE;

	d->metadata = data;
	d->metadata_size = len;
	if (uc->config.needs_epib)
		flags |= CPPI5_INFO0_HDESC_EPIB_PRESENT;

	cppi5_hdesc_update_flags(h_desc, flags);
	cppi5_hdesc_update_psdata_size(h_desc, psd_size);

	return 0;
}

static void *udma_get_metadata_ptr(struct dma_async_tx_descriptor *desc,
				   size_t *payload_len, size_t *max_len)
{
	struct udma_desc *d = to_udma_desc(desc);
	struct udma_chan *uc = to_udma_chan(desc->chan);
	struct cppi5_host_desc_t *h_desc;

	if (!uc->config.pkt_mode || !uc->config.metadata_size)
		return ERR_PTR(-ENOTSUPP);

	h_desc = d->hwdesc[0].cppi5_desc_vaddr;

	*max_len = uc->config.metadata_size;

	*payload_len = cppi5_hdesc_epib_present(&h_desc->hdr) ?
		       CPPI5_INFO0_HDESC_EPIB_SIZE : 0;
	*payload_len += cppi5_hdesc_get_psdata_size(h_desc);

	return h_desc->epib;
}

static int udma_set_metadata_len(struct dma_async_tx_descriptor *desc,
				 size_t payload_len)
{
	struct udma_desc *d = to_udma_desc(desc);
	struct udma_chan *uc = to_udma_chan(desc->chan);
	struct cppi5_host_desc_t *h_desc;
	u32 psd_size = payload_len;
	u32 flags = 0;

	if (!uc->config.pkt_mode || !uc->config.metadata_size)
		return -ENOTSUPP;

	if (payload_len > uc->config.metadata_size)
		return -EINVAL;

	if (uc->config.needs_epib && payload_len < CPPI5_INFO0_HDESC_EPIB_SIZE)
		return -EINVAL;

	h_desc = d->hwdesc[0].cppi5_desc_vaddr;

	if (uc->config.needs_epib) {
		psd_size -= CPPI5_INFO0_HDESC_EPIB_SIZE;
		flags |= CPPI5_INFO0_HDESC_EPIB_PRESENT;
	}

	cppi5_hdesc_update_flags(h_desc, flags);
	cppi5_hdesc_update_psdata_size(h_desc, psd_size);

	return 0;
}

static struct dma_descriptor_metadata_ops metadata_ops = {
	.attach = udma_attach_metadata,
	.get_ptr = udma_get_metadata_ptr,
	.set_len = udma_set_metadata_len,
};

static struct dma_async_tx_descriptor *
udma_prep_slave_sg(struct dma_chan *chan, struct scatterlist *sgl,
		   unsigned int sglen, enum dma_transfer_direction dir,
		   unsigned long tx_flags, void *context)
{
	struct udma_chan *uc = to_udma_chan(chan);
	enum dma_slave_buswidth dev_width;
	struct udma_desc *d;
	u32 burst;

	if (dir != uc->config.dir) {
		dev_err(chan->device->dev,
			"%s: chan%d is for %s, not supporting %s\n",
			__func__, uc->id,
			dmaengine_get_direction_text(uc->config.dir),
			dmaengine_get_direction_text(dir));
		return NULL;
	}

	if (dir == DMA_DEV_TO_MEM) {
		dev_width = uc->cfg.src_addr_width;
		burst = uc->cfg.src_maxburst;
	} else if (dir == DMA_MEM_TO_DEV) {
		dev_width = uc->cfg.dst_addr_width;
		burst = uc->cfg.dst_maxburst;
	} else {
		dev_err(chan->device->dev, "%s: bad direction?\n", __func__);
		return NULL;
	}

	if (!burst)
		burst = 1;

	if (uc->config.pkt_mode)
		d = udma_prep_slave_sg_pkt(uc, sgl, sglen, dir, tx_flags,
					   context);
	else
		d = udma_prep_slave_sg_tr(uc, sgl, sglen, dir, tx_flags,
					  context);

	if (!d)
		return NULL;

	d->dir = dir;
	d->desc_idx = 0;
	d->tr_idx = 0;

	/* static TR for remote PDMA */
	if (udma_configure_statictr(uc, d, dev_width, burst)) {
		dev_err(uc->ud->dev,
			"%s: StaticTR Z is limited to maximum 4095 (%u)\n",
			__func__, d->static_tr.bstcnt);

		udma_free_hwdesc(uc, d);
		kfree(d);
		return NULL;
	}

	if (uc->config.metadata_size)
		d->vd.tx.metadata_ops = &metadata_ops;

	return vchan_tx_prep(&uc->vc, &d->vd, tx_flags);
}

static struct udma_desc *
udma_prep_dma_cyclic_tr(struct udma_chan *uc, dma_addr_t buf_addr,
			size_t buf_len, size_t period_len,
			enum dma_transfer_direction dir, unsigned long flags)
{
	enum dma_slave_buswidth dev_width;
	struct udma_desc *d;
	size_t tr_size;
	struct cppi5_tr_type1_t *tr_req;
	unsigned int i;
	unsigned int periods = buf_len / period_len;
	u32 burst;

	if (dir == DMA_DEV_TO_MEM) {
		dev_width = uc->cfg.src_addr_width;
		burst = uc->cfg.src_maxburst;
	} else if (dir == DMA_MEM_TO_DEV) {
		dev_width = uc->cfg.dst_addr_width;
		burst = uc->cfg.dst_maxburst;
	} else {
		dev_err(uc->ud->dev, "%s: bad direction?\n", __func__);
		return NULL;
	}

	if (!burst)
		burst = 1;

	/* Now allocate and setup the descriptor. */
	tr_size = sizeof(struct cppi5_tr_type1_t);
	d = udma_alloc_tr_desc(uc, tr_size, periods, dir);
	if (!d)
		return NULL;

	tr_req = d->hwdesc[0].tr_req_base;
	for (i = 0; i < periods; i++) {
		cppi5_tr_init(&tr_req[i].flags, CPPI5_TR_TYPE1, false, false,
			      CPPI5_TR_EVENT_SIZE_COMPLETION, 0);

		tr_req[i].addr = buf_addr + period_len * i;
		tr_req[i].icnt0 = dev_width;
		tr_req[i].icnt1 = period_len / dev_width;
		tr_req[i].dim1 = dev_width;

		if (!(flags & DMA_PREP_INTERRUPT))
			cppi5_tr_csf_set(&tr_req[i].flags,
					 CPPI5_TR_CSF_SUPR_EVT);
	}

	return d;
}

static struct udma_desc *
udma_prep_dma_cyclic_pkt(struct udma_chan *uc, dma_addr_t buf_addr,
			 size_t buf_len, size_t period_len,
			 enum dma_transfer_direction dir, unsigned long flags)
{
	struct udma_desc *d;
	u32 ring_id;
	int i;
	int periods = buf_len / period_len;

	if (periods > (K3_UDMA_DEFAULT_RING_SIZE - 1))
		return NULL;

	if (period_len >= SZ_4M)
		return NULL;

	d = kzalloc(sizeof(*d) + periods * sizeof(d->hwdesc[0]), GFP_NOWAIT);
	if (!d)
		return NULL;

	d->hwdesc_count = periods;

	/* TODO: re-check this... */
	if (dir == DMA_DEV_TO_MEM)
		ring_id = k3_ringacc_get_ring_id(uc->rflow->r_ring);
	else
		ring_id = k3_ringacc_get_ring_id(uc->tchan->tc_ring);

	for (i = 0; i < periods; i++) {
		struct udma_hwdesc *hwdesc = &d->hwdesc[i];
		dma_addr_t period_addr = buf_addr + (period_len * i);
		struct cppi5_host_desc_t *h_desc;

		hwdesc->cppi5_desc_vaddr = dma_pool_zalloc(uc->hdesc_pool,
						GFP_NOWAIT,
						&hwdesc->cppi5_desc_paddr);
		if (!hwdesc->cppi5_desc_vaddr) {
			dev_err(uc->ud->dev,
				"descriptor%d allocation failed\n", i);

			udma_free_hwdesc(uc, d);
			kfree(d);
			return NULL;
		}

		hwdesc->cppi5_desc_size = uc->config.hdesc_size;
		h_desc = hwdesc->cppi5_desc_vaddr;

		cppi5_hdesc_init(h_desc, 0, 0);
		cppi5_hdesc_set_pktlen(h_desc, period_len);

		/* Flow and Packed ID */
		cppi5_desc_set_pktids(&h_desc->hdr, uc->id,
				      CPPI5_INFO1_DESC_FLOWID_DEFAULT);
		cppi5_desc_set_retpolicy(&h_desc->hdr, 0, ring_id);

		/* attach each period to a new descriptor */
		cppi5_hdesc_attach_buf(h_desc,
				       period_addr, period_len,
				       period_addr, period_len);
	}

	return d;
}

static struct dma_async_tx_descriptor *
udma_prep_dma_cyclic(struct dma_chan *chan, dma_addr_t buf_addr, size_t buf_len,
		     size_t period_len, enum dma_transfer_direction dir,
		     unsigned long flags)
{
	struct udma_chan *uc = to_udma_chan(chan);
	enum dma_slave_buswidth dev_width;
	struct udma_desc *d;
	u32 burst;

	if (dir != uc->config.dir) {
		dev_err(chan->device->dev,
			"%s: chan%d is for %s, not supporting %s\n",
			__func__, uc->id,
			dmaengine_get_direction_text(uc->config.dir),
			dmaengine_get_direction_text(dir));
		return NULL;
	}

	uc->cyclic = true;

	if (dir == DMA_DEV_TO_MEM) {
		dev_width = uc->cfg.src_addr_width;
		burst = uc->cfg.src_maxburst;
	} else if (dir == DMA_MEM_TO_DEV) {
		dev_width = uc->cfg.dst_addr_width;
		burst = uc->cfg.dst_maxburst;
	} else {
		dev_err(uc->ud->dev, "%s: bad direction?\n", __func__);
		return NULL;
	}

	if (!burst)
		burst = 1;

	if (uc->config.pkt_mode)
		d = udma_prep_dma_cyclic_pkt(uc, buf_addr, buf_len, period_len,
					     dir, flags);
	else
		d = udma_prep_dma_cyclic_tr(uc, buf_addr, buf_len, period_len,
					    dir, flags);

	if (!d)
		return NULL;

	d->sglen = buf_len / period_len;

	d->dir = dir;
	d->residue = buf_len;

	/* static TR for remote PDMA */
	if (udma_configure_statictr(uc, d, dev_width, burst)) {
		dev_err(uc->ud->dev,
			"%s: StaticTR Z is limited to maximum 4095 (%u)\n",
			__func__, d->static_tr.bstcnt);

		udma_free_hwdesc(uc, d);
		kfree(d);
		return NULL;
	}

	if (uc->config.metadata_size)
		d->vd.tx.metadata_ops = &metadata_ops;

	return vchan_tx_prep(&uc->vc, &d->vd, flags);
}

static struct dma_async_tx_descriptor *
udma_prep_dma_memcpy(struct dma_chan *chan, dma_addr_t dest, dma_addr_t src,
		     size_t len, unsigned long tx_flags)
{
	struct udma_chan *uc = to_udma_chan(chan);
	struct udma_desc *d;
	struct cppi5_tr_type15_t *tr_req;
	int num_tr;
	size_t tr_size = sizeof(struct cppi5_tr_type15_t);
	u16 tr0_cnt0, tr0_cnt1, tr1_cnt0;

	if (uc->config.dir != DMA_MEM_TO_MEM) {
		dev_err(chan->device->dev,
			"%s: chan%d is for %s, not supporting %s\n",
			__func__, uc->id,
			dmaengine_get_direction_text(uc->config.dir),
			dmaengine_get_direction_text(DMA_MEM_TO_MEM));
		return NULL;
	}

	if (len < SZ_64K) {
		num_tr = 1;
		tr0_cnt0 = len;
		tr0_cnt1 = 1;
	} else {
		unsigned long align_to = __ffs(src | dest);

		if (align_to > 3)
			align_to = 3;
		/*
		 * Keep simple: tr0: SZ_64K-alignment blocks,
		 *		tr1: the remaining
		 */
		num_tr = 2;
		tr0_cnt0 = (SZ_64K - BIT(align_to));
		if (len / tr0_cnt0 >= SZ_64K) {
			dev_err(uc->ud->dev, "size %zu is not supported\n",
				len);
			return NULL;
		}

		tr0_cnt1 = len / tr0_cnt0;
		tr1_cnt0 = len % tr0_cnt0;
	}

	d = udma_alloc_tr_desc(uc, tr_size, num_tr, DMA_MEM_TO_MEM);
	if (!d)
		return NULL;

	d->dir = DMA_MEM_TO_MEM;
	d->desc_idx = 0;
	d->tr_idx = 0;
	d->residue = len;

	tr_req = d->hwdesc[0].tr_req_base;

	cppi5_tr_init(&tr_req[0].flags, CPPI5_TR_TYPE15, false, true,
		      CPPI5_TR_EVENT_SIZE_COMPLETION, 0);
	cppi5_tr_csf_set(&tr_req[0].flags, CPPI5_TR_CSF_SUPR_EVT);

	tr_req[0].addr = src;
	tr_req[0].icnt0 = tr0_cnt0;
	tr_req[0].icnt1 = tr0_cnt1;
	tr_req[0].icnt2 = 1;
	tr_req[0].icnt3 = 1;
	tr_req[0].dim1 = tr0_cnt0;

	tr_req[0].daddr = dest;
	tr_req[0].dicnt0 = tr0_cnt0;
	tr_req[0].dicnt1 = tr0_cnt1;
	tr_req[0].dicnt2 = 1;
	tr_req[0].dicnt3 = 1;
	tr_req[0].ddim1 = tr0_cnt0;

	if (num_tr == 2) {
		cppi5_tr_init(&tr_req[1].flags, CPPI5_TR_TYPE15, false, true,
			      CPPI5_TR_EVENT_SIZE_COMPLETION, 0);
		cppi5_tr_csf_set(&tr_req[1].flags, CPPI5_TR_CSF_SUPR_EVT);

		tr_req[1].addr = src + tr0_cnt1 * tr0_cnt0;
		tr_req[1].icnt0 = tr1_cnt0;
		tr_req[1].icnt1 = 1;
		tr_req[1].icnt2 = 1;
		tr_req[1].icnt3 = 1;

		tr_req[1].daddr = dest + tr0_cnt1 * tr0_cnt0;
		tr_req[1].dicnt0 = tr1_cnt0;
		tr_req[1].dicnt1 = 1;
		tr_req[1].dicnt2 = 1;
		tr_req[1].dicnt3 = 1;
	}

	cppi5_tr_csf_set(&tr_req[num_tr - 1].flags, CPPI5_TR_CSF_EOP);

	if (uc->config.metadata_size)
		d->vd.tx.metadata_ops = &metadata_ops;

	return vchan_tx_prep(&uc->vc, &d->vd, tx_flags);
}

static void udma_issue_pending(struct dma_chan *chan)
{
	struct udma_chan *uc = to_udma_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&uc->vc.lock, flags);

	/* If we have something pending and no active descriptor, then */
	if (vchan_issue_pending(&uc->vc) && !uc->desc) {
		/*
		 * start a descriptor if the channel is NOT [marked as
		 * terminating _and_ it is still running (teardown has not
		 * completed yet)].
		 */
		if (!(uc->state == UDMA_CHAN_IS_TERMINATING &&
		      udma_is_chan_running(uc)))
			udma_start(uc);
	}

	spin_unlock_irqrestore(&uc->vc.lock, flags);
}

static enum dma_status udma_tx_status(struct dma_chan *chan,
				      dma_cookie_t cookie,
				      struct dma_tx_state *txstate)
{
	struct udma_chan *uc = to_udma_chan(chan);
	enum dma_status ret;
	unsigned long flags;

	spin_lock_irqsave(&uc->vc.lock, flags);

	ret = dma_cookie_status(chan, cookie, txstate);

	if (ret == DMA_IN_PROGRESS && udma_is_chan_paused(uc))
		ret = DMA_PAUSED;

	if (ret == DMA_COMPLETE || !txstate)
		goto out;

	if (uc->desc && uc->desc->vd.tx.cookie == cookie) {
		u32 peer_bcnt = 0;
		u32 bcnt = 0;
		u32 residue = uc->desc->residue;
		u32 delay = 0;

		if (uc->desc->dir == DMA_MEM_TO_DEV) {
			bcnt = udma_tchanrt_read(uc->tchan,
						 UDMA_TCHAN_RT_SBCNT_REG);

			if (uc->config.ep_type != PSIL_EP_NATIVE) {
				peer_bcnt = udma_tchanrt_read(uc->tchan,
						UDMA_TCHAN_RT_PEER_BCNT_REG);

				if (bcnt > peer_bcnt)
					delay = bcnt - peer_bcnt;
			}
		} else if (uc->desc->dir == DMA_DEV_TO_MEM) {
			bcnt = udma_rchanrt_read(uc->rchan,
						 UDMA_RCHAN_RT_BCNT_REG);

			if (uc->config.ep_type != PSIL_EP_NATIVE) {
				peer_bcnt = udma_rchanrt_read(uc->rchan,
						UDMA_RCHAN_RT_PEER_BCNT_REG);

				if (peer_bcnt > bcnt)
					delay = peer_bcnt - bcnt;
			}
		} else {
			bcnt = udma_tchanrt_read(uc->tchan,
						 UDMA_TCHAN_RT_BCNT_REG);
		}

		bcnt -= uc->bcnt;
		if (bcnt && !(bcnt % uc->desc->residue))
			residue = 0;
		else
			residue -= bcnt % uc->desc->residue;

		if (!residue && (uc->config.dir == DMA_DEV_TO_MEM || !delay)) {
			ret = DMA_COMPLETE;
			delay = 0;
		}

		dma_set_residue(txstate, residue);
		dma_set_in_flight_bytes(txstate, delay);

	} else {
		ret = DMA_COMPLETE;
	}

out:
	spin_unlock_irqrestore(&uc->vc.lock, flags);
	return ret;
}

static int udma_pause(struct dma_chan *chan)
{
	struct udma_chan *uc = to_udma_chan(chan);

	if (!uc->desc)
		return -EINVAL;

	/* pause the channel */
	switch (uc->desc->dir) {
	case DMA_DEV_TO_MEM:
		udma_rchanrt_update_bits(uc->rchan,
					 UDMA_RCHAN_RT_PEER_RT_EN_REG,
					 UDMA_PEER_RT_EN_PAUSE,
					 UDMA_PEER_RT_EN_PAUSE);
		break;
	case DMA_MEM_TO_DEV:
		udma_tchanrt_update_bits(uc->tchan,
					 UDMA_TCHAN_RT_PEER_RT_EN_REG,
					 UDMA_PEER_RT_EN_PAUSE,
					 UDMA_PEER_RT_EN_PAUSE);
		break;
	case DMA_MEM_TO_MEM:
		udma_tchanrt_update_bits(uc->tchan, UDMA_TCHAN_RT_CTL_REG,
					 UDMA_CHAN_RT_CTL_PAUSE,
					 UDMA_CHAN_RT_CTL_PAUSE);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int udma_resume(struct dma_chan *chan)
{
	struct udma_chan *uc = to_udma_chan(chan);

	if (!uc->desc)
		return -EINVAL;

	/* resume the channel */
	switch (uc->desc->dir) {
	case DMA_DEV_TO_MEM:
		udma_rchanrt_update_bits(uc->rchan,
					 UDMA_RCHAN_RT_PEER_RT_EN_REG,
					 UDMA_PEER_RT_EN_PAUSE, 0);

		break;
	case DMA_MEM_TO_DEV:
		udma_tchanrt_update_bits(uc->tchan,
					 UDMA_TCHAN_RT_PEER_RT_EN_REG,
					 UDMA_PEER_RT_EN_PAUSE, 0);
		break;
	case DMA_MEM_TO_MEM:
		udma_tchanrt_update_bits(uc->tchan, UDMA_TCHAN_RT_CTL_REG,
					 UDMA_CHAN_RT_CTL_PAUSE, 0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int udma_terminate_all(struct dma_chan *chan)
{
	struct udma_chan *uc = to_udma_chan(chan);
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&uc->vc.lock, flags);

	if (udma_is_chan_running(uc))
		udma_stop(uc);

	if (uc->desc) {
		uc->terminated_desc = uc->desc;
		uc->desc = NULL;
		uc->terminated_desc->terminated = true;
		cancel_delayed_work(&uc->tx_drain.work);
	}

	uc->paused = false;

	vchan_get_all_descriptors(&uc->vc, &head);
	spin_unlock_irqrestore(&uc->vc.lock, flags);
	vchan_dma_desc_free_list(&uc->vc, &head);

	return 0;
}

static void udma_synchronize(struct dma_chan *chan)
{
	struct udma_chan *uc = to_udma_chan(chan);
	unsigned long timeout = msecs_to_jiffies(1000);

	vchan_synchronize(&uc->vc);

	if (uc->state == UDMA_CHAN_IS_TERMINATING) {
		timeout = wait_for_completion_timeout(&uc->teardown_completed,
						      timeout);
		if (!timeout) {
			dev_warn(uc->ud->dev, "chan%d teardown timeout!\n",
				 uc->id);
			udma_dump_chan_stdata(uc);
			udma_reset_chan(uc, true);
		}
	}

	udma_reset_chan(uc, false);
	if (udma_is_chan_running(uc))
		dev_warn(uc->ud->dev, "chan%d refused to stop!\n", uc->id);

	cancel_delayed_work_sync(&uc->tx_drain.work);
	udma_reset_rings(uc);
}

static void udma_desc_pre_callback(struct virt_dma_chan *vc,
				   struct virt_dma_desc *vd,
				   struct dmaengine_result *result)
{
	struct udma_chan *uc = to_udma_chan(&vc->chan);
	struct udma_desc *d;

	if (!vd)
		return;

	d = to_udma_desc(&vd->tx);

	if (d->metadata_size)
		udma_fetch_epib(uc, d);

	/* Provide residue information for the client */
	if (result) {
		void *desc_vaddr = udma_curr_cppi5_desc_vaddr(d, d->desc_idx);

		if (cppi5_desc_get_type(desc_vaddr) ==
		    CPPI5_INFO0_DESC_TYPE_VAL_HOST) {
			result->residue = d->residue -
					  cppi5_hdesc_get_pktlen(desc_vaddr);
			if (result->residue)
				result->result = DMA_TRANS_ABORTED;
			else
				result->result = DMA_TRANS_NOERROR;
		} else {
			result->residue = 0;
			result->result = DMA_TRANS_NOERROR;
		}
	}
}

/*
 * This tasklet handles the completion of a DMA descriptor by
 * calling its callback and freeing it.
 */
static void udma_vchan_complete(unsigned long arg)
{
	struct virt_dma_chan *vc = (struct virt_dma_chan *)arg;
	struct virt_dma_desc *vd, *_vd;
	struct dmaengine_desc_callback cb;
	LIST_HEAD(head);

	spin_lock_irq(&vc->lock);
	list_splice_tail_init(&vc->desc_completed, &head);
	vd = vc->cyclic;
	if (vd) {
		vc->cyclic = NULL;
		dmaengine_desc_get_callback(&vd->tx, &cb);
	} else {
		memset(&cb, 0, sizeof(cb));
	}
	spin_unlock_irq(&vc->lock);

	udma_desc_pre_callback(vc, vd, NULL);
	dmaengine_desc_callback_invoke(&cb, NULL);

	list_for_each_entry_safe(vd, _vd, &head, node) {
		struct dmaengine_result result;

		dmaengine_desc_get_callback(&vd->tx, &cb);

		list_del(&vd->node);

		udma_desc_pre_callback(vc, vd, &result);
		dmaengine_desc_callback_invoke(&cb, &result);

		vchan_vdesc_fini(vd);
	}
}

static void udma_free_chan_resources(struct dma_chan *chan)
{
	struct udma_chan *uc = to_udma_chan(chan);
	struct udma_dev *ud = to_udma_dev(chan->device);

	udma_terminate_all(chan);
	if (uc->terminated_desc) {
		udma_reset_chan(uc, false);
		udma_reset_rings(uc);
	}

	cancel_delayed_work_sync(&uc->tx_drain.work);
	destroy_delayed_work_on_stack(&uc->tx_drain.work);

	if (uc->irq_num_ring > 0) {
		free_irq(uc->irq_num_ring, uc);

		uc->irq_num_ring = 0;
	}
	if (uc->irq_num_udma > 0) {
		free_irq(uc->irq_num_udma, uc);

		uc->irq_num_udma = 0;
	}

	/* Release PSI-L pairing */
	if (uc->psil_paired) {
		navss_psil_unpair(ud, uc->config.src_thread,
				  uc->config.dst_thread);
		uc->psil_paired = false;
	}

	vchan_free_chan_resources(&uc->vc);
	tasklet_kill(&uc->vc.task);

	udma_free_tx_resources(uc);
	udma_free_rx_resources(uc);
	udma_reset_uchan(uc);

	if (uc->use_dma_pool) {
		dma_pool_destroy(uc->hdesc_pool);
		uc->use_dma_pool = false;
	}
}

static struct platform_driver udma_driver;

static bool udma_dma_filter_fn(struct dma_chan *chan, void *param)
{
	struct udma_chan_config *ucc;
	struct psil_endpoint_config *ep_config;
	struct udma_chan *uc;
	struct udma_dev *ud;
	u32 *args;

	if (chan->device->dev->driver != &udma_driver.driver)
		return false;

	uc = to_udma_chan(chan);
	ucc = &uc->config;
	ud = uc->ud;
	args = param;

	ucc->remote_thread_id = args[0];

	if (ucc->remote_thread_id & K3_PSIL_DST_THREAD_ID_OFFSET)
		ucc->dir = DMA_MEM_TO_DEV;
	else
		ucc->dir = DMA_DEV_TO_MEM;

	ep_config = psil_get_ep_config(ucc->remote_thread_id);
	if (IS_ERR(ep_config)) {
		dev_err(ud->dev, "No configuration for psi-l thread 0x%04x\n",
			ucc->remote_thread_id);
		ucc->dir = DMA_MEM_TO_MEM;
		ucc->remote_thread_id = -1;
		return false;
	}

	ucc->pkt_mode = ep_config->pkt_mode;
	ucc->channel_tpl = ep_config->channel_tpl;
	ucc->notdpkt = ep_config->notdpkt;
	ucc->ep_type = ep_config->ep_type;

	if (ucc->ep_type != PSIL_EP_NATIVE) {
		const struct udma_match_data *match_data = ud->match_data;

		if (match_data->flags & UDMA_FLAG_PDMA_ACC32)
			ucc->enable_acc32 = ep_config->pdma_acc32;
		if (match_data->flags & UDMA_FLAG_PDMA_BURST)
			ucc->enable_burst = ep_config->pdma_burst;
	}

	ucc->needs_epib = ep_config->needs_epib;
	ucc->psd_size = ep_config->psd_size;
	ucc->metadata_size =
			(ucc->needs_epib ? CPPI5_INFO0_HDESC_EPIB_SIZE : 0) +
			ucc->psd_size;

	if (ucc->pkt_mode)
		ucc->hdesc_size = ALIGN(sizeof(struct cppi5_host_desc_t) +
				 ucc->metadata_size, ud->desc_align);

	dev_dbg(ud->dev, "chan%d: Remote thread: 0x%04x (%s)\n", uc->id,
		ucc->remote_thread_id, dmaengine_get_direction_text(ucc->dir));

	return true;
}

static struct dma_chan *udma_of_xlate(struct of_phandle_args *dma_spec,
				      struct of_dma *ofdma)
{
	struct udma_dev *ud = ofdma->of_dma_data;
	dma_cap_mask_t mask = ud->ddev.cap_mask;
	struct dma_chan *chan;

	if (dma_spec->args_count != 1)
		return NULL;

	chan = __dma_request_channel(&mask, udma_dma_filter_fn,
				     &dma_spec->args[0], ofdma->of_node);
	if (!chan) {
		dev_err(ud->dev, "get channel fail in %s.\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	return chan;
}

static struct udma_match_data am654_main_data = {
	.psil_base = 0x1000,
	.enable_memcpy_support = true,
	.statictr_z_mask = GENMASK(11, 0),
	.rchan_oes_offset = 0x2000,
	.tpl_levels = 2,
	.level_start_idx = {
		[0] = 8, /* Normal channels */
		[1] = 0, /* High Throughput channels */
	},
};

static struct udma_match_data am654_mcu_data = {
	.psil_base = 0x6000,
	.enable_memcpy_support = true, /* TEST: DMA domains */
	.statictr_z_mask = GENMASK(11, 0),
	.rchan_oes_offset = 0x2000,
	.tpl_levels = 2,
	.level_start_idx = {
		[0] = 2, /* Normal channels */
		[1] = 0, /* High Throughput channels */
	},
};

static struct udma_match_data j721e_main_data = {
	.psil_base = 0x1000,
	.enable_memcpy_support = true,
	.flags = UDMA_FLAG_PDMA_ACC32 | UDMA_FLAG_PDMA_BURST,
	.statictr_z_mask = GENMASK(23, 0),
	.rchan_oes_offset = 0x400,
	.tpl_levels = 3,
	.level_start_idx = {
		[0] = 16, /* Normal channels */
		[1] = 4, /* High Throughput channels */
		[2] = 0, /* Ultra High Throughput channels */
	},
};

static struct udma_match_data j721e_mcu_data = {
	.psil_base = 0x6000,
	.enable_memcpy_support = false, /* MEM_TO_MEM is slow via MCU UDMA */
	.flags = UDMA_FLAG_PDMA_ACC32 | UDMA_FLAG_PDMA_BURST,
	.statictr_z_mask = GENMASK(23, 0),
	.rchan_oes_offset = 0x400,
	.tpl_levels = 2,
	.level_start_idx = {
		[0] = 2, /* Normal channels */
		[1] = 0, /* High Throughput channels */
	},
};

static const struct of_device_id udma_of_match[] = {
	{
		.compatible = "ti,am654-navss-main-udmap",
		.data = &am654_main_data,
	},
	{
		.compatible = "ti,am654-navss-mcu-udmap",
		.data = &am654_mcu_data,
	}, {
		.compatible = "ti,j721e-navss-main-udmap",
		.data = &j721e_main_data,
	}, {
		.compatible = "ti,j721e-navss-mcu-udmap",
		.data = &j721e_mcu_data,
	},
	{ /* Sentinel */ },
};

static int udma_get_mmrs(struct platform_device *pdev, struct udma_dev *ud)
{
	struct resource *res;
	int i;

	for (i = 0; i < MMR_LAST; i++) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   mmr_names[i]);
		ud->mmrs[i] = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(ud->mmrs[i]))
			return PTR_ERR(ud->mmrs[i]);
	}

	return 0;
}

static int udma_setup_resources(struct udma_dev *ud)
{
	struct device *dev = ud->dev;
	int ch_count, ret, i, j;
	u32 cap2, cap3;
	struct ti_sci_resource_desc *rm_desc;
	struct ti_sci_resource *rm_res, irq_res;
	struct udma_tisci_rm *tisci_rm = &ud->tisci_rm;
	static const char * const range_names[] = { "ti,sci-rm-range-tchan",
						    "ti,sci-rm-range-rchan",
						    "ti,sci-rm-range-rflow" };

	cap2 = udma_read(ud->mmrs[MMR_GCFG], 0x28);
	cap3 = udma_read(ud->mmrs[MMR_GCFG], 0x2c);

	ud->rflow_cnt = cap3 & 0x3fff;
	ud->tchan_cnt = cap2 & 0x1ff;
	ud->echan_cnt = (cap2 >> 9) & 0x1ff;
	ud->rchan_cnt = (cap2 >> 18) & 0x1ff;
	ch_count  = ud->tchan_cnt + ud->rchan_cnt;

	ud->tchan_map = devm_kmalloc_array(dev, BITS_TO_LONGS(ud->tchan_cnt),
					   sizeof(unsigned long), GFP_KERNEL);
	ud->tchans = devm_kcalloc(dev, ud->tchan_cnt, sizeof(*ud->tchans),
				  GFP_KERNEL);
	ud->rchan_map = devm_kmalloc_array(dev, BITS_TO_LONGS(ud->rchan_cnt),
					   sizeof(unsigned long), GFP_KERNEL);
	ud->rchans = devm_kcalloc(dev, ud->rchan_cnt, sizeof(*ud->rchans),
				  GFP_KERNEL);
	ud->rflow_gp_map = devm_kmalloc_array(dev, BITS_TO_LONGS(ud->rflow_cnt),
					      sizeof(unsigned long),
					      GFP_KERNEL);
	ud->rflow_gp_map_allocated = devm_kcalloc(dev,
						  BITS_TO_LONGS(ud->rflow_cnt),
						  sizeof(unsigned long),
						  GFP_KERNEL);
	ud->rflow_in_use = devm_kcalloc(dev, BITS_TO_LONGS(ud->rflow_cnt),
					sizeof(unsigned long),
					GFP_KERNEL);
	ud->rflows = devm_kcalloc(dev, ud->rflow_cnt, sizeof(*ud->rflows),
				  GFP_KERNEL);

	if (!ud->tchan_map || !ud->rchan_map || !ud->rflow_gp_map ||
	    !ud->rflow_gp_map_allocated || !ud->tchans || !ud->rchans ||
	    !ud->rflows || !ud->rflow_in_use)
		return -ENOMEM;

	/*
	 * RX flows with the same Ids as RX channels are reserved to be used
	 * as default flows if remote HW can't generate flow_ids. Those
	 * RX flows can be requested only explicitly by id.
	 */
	bitmap_set(ud->rflow_gp_map_allocated, 0, ud->rchan_cnt);

	/* by default no GP rflows are assigned to Linux */
	bitmap_set(ud->rflow_gp_map, 0, ud->rflow_cnt);

	/* Get resource ranges from tisci */
	for (i = 0; i < RM_RANGE_LAST; i++)
		tisci_rm->rm_ranges[i] =
			devm_ti_sci_get_of_resource(tisci_rm->tisci, dev,
						    tisci_rm->tisci_dev_id,
						    (char *)range_names[i]);

	/* tchan ranges */
	rm_res = tisci_rm->rm_ranges[RM_RANGE_TCHAN];
	if (IS_ERR(rm_res)) {
		bitmap_zero(ud->tchan_map, ud->tchan_cnt);
	} else {
		bitmap_fill(ud->tchan_map, ud->tchan_cnt);
		for (i = 0; i < rm_res->sets; i++) {
			rm_desc = &rm_res->desc[i];
			bitmap_clear(ud->tchan_map, rm_desc->start,
				     rm_desc->num);
			dev_dbg(dev, "ti-sci-res: tchan: %d:%d\n",
				rm_desc->start, rm_desc->num);
		}
	}
	irq_res.sets = rm_res->sets;

	/* rchan and matching default flow ranges */
	rm_res = tisci_rm->rm_ranges[RM_RANGE_RCHAN];
	if (IS_ERR(rm_res)) {
		bitmap_zero(ud->rchan_map, ud->rchan_cnt);
	} else {
		bitmap_fill(ud->rchan_map, ud->rchan_cnt);
		for (i = 0; i < rm_res->sets; i++) {
			rm_desc = &rm_res->desc[i];
			bitmap_clear(ud->rchan_map, rm_desc->start,
				     rm_desc->num);
			dev_dbg(dev, "ti-sci-res: rchan: %d:%d\n",
				rm_desc->start, rm_desc->num);
		}
	}

	irq_res.sets += rm_res->sets;
	irq_res.desc = kcalloc(irq_res.sets, sizeof(*irq_res.desc), GFP_KERNEL);
	rm_res = tisci_rm->rm_ranges[RM_RANGE_TCHAN];
	for (i = 0; i < rm_res->sets; i++) {
		irq_res.desc[i].start = rm_res->desc[i].start;
		irq_res.desc[i].num = rm_res->desc[i].num;
	}
	rm_res = tisci_rm->rm_ranges[RM_RANGE_RCHAN];
	for (j = 0; j < rm_res->sets; j++, i++) {
		irq_res.desc[i].start = rm_res->desc[j].start +
					ud->match_data->rchan_oes_offset;
		irq_res.desc[i].num = rm_res->desc[j].num;
	}
	ret = ti_sci_inta_msi_domain_alloc_irqs(ud->dev, &irq_res);
	kfree(irq_res.desc);
	if (ret) {
		dev_err(ud->dev, "Failed to allocate MSI interrupts\n");
		return ret;
	}

	/* GP rflow ranges */
	rm_res = tisci_rm->rm_ranges[RM_RANGE_RFLOW];
	if (IS_ERR(rm_res)) {
		/* all gp flows are assigned exclusively to Linux */
		bitmap_clear(ud->rflow_gp_map, ud->rchan_cnt,
			     ud->rflow_cnt - ud->rchan_cnt);
	} else {
		for (i = 0; i < rm_res->sets; i++) {
			rm_desc = &rm_res->desc[i];
			bitmap_clear(ud->rflow_gp_map, rm_desc->start,
				     rm_desc->num);
			dev_dbg(dev, "ti-sci-res: rflow: %d:%d\n",
				rm_desc->start, rm_desc->num);
		}
	}

	ch_count -= bitmap_weight(ud->tchan_map, ud->tchan_cnt);
	ch_count -= bitmap_weight(ud->rchan_map, ud->rchan_cnt);
	if (!ch_count)
		return -ENODEV;

	ud->channels = devm_kcalloc(dev, ch_count, sizeof(*ud->channels),
				    GFP_KERNEL);
	if (!ud->channels)
		return -ENOMEM;

	dev_info(dev, "Channels: %d (tchan: %u, rchan: %u, gp-rflow: %u)\n",
		 ch_count,
		 ud->tchan_cnt - bitmap_weight(ud->tchan_map, ud->tchan_cnt),
		 ud->rchan_cnt - bitmap_weight(ud->rchan_map, ud->rchan_cnt),
		 ud->rflow_cnt - bitmap_weight(ud->rflow_gp_map,
					       ud->rflow_cnt));

	return ch_count;
}

#define TI_UDMAC_BUSWIDTHS	(BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) | \
				 BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) | \
				 BIT(DMA_SLAVE_BUSWIDTH_3_BYTES) | \
				 BIT(DMA_SLAVE_BUSWIDTH_4_BYTES) | \
				 BIT(DMA_SLAVE_BUSWIDTH_8_BYTES))

static int udma_probe(struct platform_device *pdev)
{
	struct device_node *navss_node = pdev->dev.parent->of_node;
	struct device *dev = &pdev->dev;
	struct udma_dev *ud;
	const struct of_device_id *match;
	int i, ret;
	int ch_count;

	ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(48));
	if (ret)
		dev_err(dev, "failed to set dma mask stuff\n");

	ud = devm_kzalloc(dev, sizeof(*ud), GFP_KERNEL);
	if (!ud)
		return -ENOMEM;

	ret = udma_get_mmrs(pdev, ud);
	if (ret)
		return ret;

	ud->tisci_rm.tisci = ti_sci_get_by_phandle(dev->of_node, "ti,sci");
	if (IS_ERR(ud->tisci_rm.tisci))
		return PTR_ERR(ud->tisci_rm.tisci);

	ret = of_property_read_u32(dev->of_node, "ti,sci-dev-id",
				   &ud->tisci_rm.tisci_dev_id);
	if (ret) {
		dev_err(dev, "ti,sci-dev-id read failure %d\n", ret);
		return ret;
	}
	pdev->id = ud->tisci_rm.tisci_dev_id;

	ret = of_property_read_u32(navss_node, "ti,sci-dev-id",
				   &ud->tisci_rm.tisci_navss_dev_id);
	if (ret) {
		dev_err(dev, "NAVSS ti,sci-dev-id read failure %d\n", ret);
		return ret;
	}

	ud->tisci_rm.tisci_udmap_ops = &ud->tisci_rm.tisci->ops.rm_udmap_ops;
	ud->tisci_rm.tisci_psil_ops = &ud->tisci_rm.tisci->ops.rm_psil_ops;

	ud->ringacc = of_k3_ringacc_get_by_phandle(dev->of_node, "ti,ringacc");
	if (IS_ERR(ud->ringacc))
		return PTR_ERR(ud->ringacc);

	dev->msi_domain = of_msi_get_domain(dev, dev->of_node,
					    DOMAIN_BUS_TI_SCI_INTA_MSI);
	if (!dev->msi_domain) {
		dev_err(dev, "Failed to get MSI domain\n");
		return -EPROBE_DEFER;
	}

	match = of_match_node(udma_of_match, dev->of_node);
	if (!match) {
		dev_err(dev, "No compatible match found\n");
		return -ENODEV;
	}
	ud->match_data = match->data;

	dma_cap_set(DMA_SLAVE, ud->ddev.cap_mask);
	dma_cap_set(DMA_CYCLIC, ud->ddev.cap_mask);

	ud->ddev.device_alloc_chan_resources = udma_alloc_chan_resources;
	ud->ddev.device_config = udma_slave_config;
	ud->ddev.device_prep_slave_sg = udma_prep_slave_sg;
	ud->ddev.device_prep_dma_cyclic = udma_prep_dma_cyclic;
	ud->ddev.device_issue_pending = udma_issue_pending;
	ud->ddev.device_tx_status = udma_tx_status;
	ud->ddev.device_pause = udma_pause;
	ud->ddev.device_resume = udma_resume;
	ud->ddev.device_terminate_all = udma_terminate_all;
	ud->ddev.device_synchronize = udma_synchronize;

	ud->ddev.device_free_chan_resources = udma_free_chan_resources;
	ud->ddev.src_addr_widths = TI_UDMAC_BUSWIDTHS;
	ud->ddev.dst_addr_widths = TI_UDMAC_BUSWIDTHS;
	ud->ddev.directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	ud->ddev.residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;
	ud->ddev.copy_align = DMAENGINE_ALIGN_8_BYTES;
	ud->ddev.desc_metadata_modes = DESC_METADATA_CLIENT |
				       DESC_METADATA_ENGINE;
	if (ud->match_data->enable_memcpy_support) {
		dma_cap_set(DMA_MEMCPY, ud->ddev.cap_mask);
		ud->ddev.device_prep_dma_memcpy = udma_prep_dma_memcpy;
		ud->ddev.directions |= BIT(DMA_MEM_TO_MEM);
	}

	ud->ddev.dev = dev;
	ud->dev = dev;
	ud->psil_base = ud->match_data->psil_base;

	INIT_LIST_HEAD(&ud->ddev.channels);
	INIT_LIST_HEAD(&ud->desc_to_purge);

	ch_count = udma_setup_resources(ud);
	if (ch_count <= 0)
		return ch_count;

	spin_lock_init(&ud->lock);
	INIT_WORK(&ud->purge_work, udma_purge_desc_work);

	ud->desc_align = 64;
	if (ud->desc_align < dma_get_cache_alignment())
		ud->desc_align = dma_get_cache_alignment();

	for (i = 0; i < ud->tchan_cnt; i++) {
		struct udma_tchan *tchan = &ud->tchans[i];

		tchan->id = i;
		tchan->reg_rt = ud->mmrs[MMR_TCHANRT] + i * 0x1000;
	}

	for (i = 0; i < ud->rchan_cnt; i++) {
		struct udma_rchan *rchan = &ud->rchans[i];

		rchan->id = i;
		rchan->reg_rt = ud->mmrs[MMR_RCHANRT] + i * 0x1000;
	}

	for (i = 0; i < ud->rflow_cnt; i++) {
		struct udma_rflow *rflow = &ud->rflows[i];

		rflow->id = i;
	}

	for (i = 0; i < ch_count; i++) {
		struct udma_chan *uc = &ud->channels[i];

		uc->ud = ud;
		uc->vc.desc_free = udma_desc_free;
		uc->id = i;
		uc->tchan = NULL;
		uc->rchan = NULL;
		uc->config.remote_thread_id = -1;
		uc->config.dir = DMA_MEM_TO_MEM;
		uc->name = devm_kasprintf(dev, GFP_KERNEL, "%s chan%d",
					  dev_name(dev), i);

		vchan_init(&uc->vc, &ud->ddev);
		/* Use custom vchan completion handling */
		tasklet_init(&uc->vc.task, udma_vchan_complete,
			     (unsigned long)&uc->vc);
		init_completion(&uc->teardown_completed);
	}

	ret = dma_async_device_register(&ud->ddev);
	if (ret) {
		dev_err(dev, "failed to register slave DMA engine: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, ud);

	ret = of_dma_controller_register(dev->of_node, udma_of_xlate, ud);
	if (ret) {
		dev_err(dev, "failed to register of_dma controller\n");
		dma_async_device_unregister(&ud->ddev);
	}

	return ret;
}

static struct platform_driver udma_driver = {
	.driver = {
		.name	= "ti-udma",
		.of_match_table = udma_of_match,
		.suppress_bind_attrs = true,
	},
	.probe		= udma_probe,
};
builtin_platform_driver(udma_driver);

/* Private interfaces to UDMA */
#include "k3-udma-private.c"
