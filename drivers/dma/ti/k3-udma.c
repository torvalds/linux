// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2019 Texas Instruments Incorporated - http://www.ti.com
 *  Author: Peter Ujfalusi <peter.ujfalusi@ti.com>
 */

#include <linux/kernel.h>
#include <linux/delay.h>
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
#include <linux/sys_soc.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/workqueue.h>
#include <linux/completion.h>
#include <linux/soc/ti/k3-ringacc.h>
#include <linux/soc/ti/ti_sci_protocol.h>
#include <linux/soc/ti/ti_sci_inta_msi.h>
#include <linux/dma/k3-event-router.h>
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

enum k3_dma_type {
	DMA_TYPE_UDMA = 0,
	DMA_TYPE_BCDMA,
	DMA_TYPE_PKTDMA,
};

enum udma_mmr {
	MMR_GCFG = 0,
	MMR_BCHANRT,
	MMR_RCHANRT,
	MMR_TCHANRT,
	MMR_LAST,
};

static const char * const mmr_names[] = {
	[MMR_GCFG] = "gcfg",
	[MMR_BCHANRT] = "bchanrt",
	[MMR_RCHANRT] = "rchanrt",
	[MMR_TCHANRT] = "tchanrt",
};

struct udma_tchan {
	void __iomem *reg_rt;

	int id;
	struct k3_ring *t_ring; /* Transmit ring */
	struct k3_ring *tc_ring; /* Transmit Completion ring */
	int tflow_id; /* applicable only for PKTDMA */

};

#define udma_bchan udma_tchan

struct udma_rflow {
	int id;
	struct k3_ring *fd_ring; /* Free Descriptor ring */
	struct k3_ring *r_ring; /* Receive ring */
};

struct udma_rchan {
	void __iomem *reg_rt;

	int id;
};

struct udma_oes_offsets {
	/* K3 UDMA Output Event Offset */
	u32 udma_rchan;

	/* BCDMA Output Event Offsets */
	u32 bcdma_bchan_data;
	u32 bcdma_bchan_ring;
	u32 bcdma_tchan_data;
	u32 bcdma_tchan_ring;
	u32 bcdma_rchan_data;
	u32 bcdma_rchan_ring;

	/* PKTDMA Output Event Offsets */
	u32 pktdma_tchan_flow;
	u32 pktdma_rchan_flow;
};

#define UDMA_FLAG_PDMA_ACC32		BIT(0)
#define UDMA_FLAG_PDMA_BURST		BIT(1)
#define UDMA_FLAG_TDTYPE		BIT(2)
#define UDMA_FLAG_BURST_SIZE		BIT(3)
#define UDMA_FLAGS_J7_CLASS		(UDMA_FLAG_PDMA_ACC32 | \
					 UDMA_FLAG_PDMA_BURST | \
					 UDMA_FLAG_TDTYPE | \
					 UDMA_FLAG_BURST_SIZE)

struct udma_match_data {
	enum k3_dma_type type;
	u32 psil_base;
	bool enable_memcpy_support;
	u32 flags;
	u32 statictr_z_mask;
	u8 burst_size[3];
};

struct udma_soc_data {
	struct udma_oes_offsets oes;
	u32 bcdma_trigger_event_offset;
};

struct udma_hwdesc {
	size_t cppi5_desc_size;
	void *cppi5_desc_vaddr;
	dma_addr_t cppi5_desc_paddr;

	/* TR descriptor internal pointers */
	void *tr_req_base;
	struct cppi5_tr_resp_t *tr_resp_base;
};

struct udma_rx_flush {
	struct udma_hwdesc hwdescs[2];

	size_t buffer_size;
	void *buffer_vaddr;
	dma_addr_t buffer_paddr;
};

struct udma_tpl {
	u8 levels;
	u32 start_idx[3];
};

struct udma_dev {
	struct dma_device ddev;
	struct device *dev;
	void __iomem *mmrs[MMR_LAST];
	const struct udma_match_data *match_data;
	const struct udma_soc_data *soc_data;

	struct udma_tpl bchan_tpl;
	struct udma_tpl tchan_tpl;
	struct udma_tpl rchan_tpl;

	size_t desc_align; /* alignment to use for descriptors */

	struct udma_tisci_rm tisci_rm;

	struct k3_ringacc *ringacc;

	struct work_struct purge_work;
	struct list_head desc_to_purge;
	spinlock_t lock;

	struct udma_rx_flush rx_flush;

	int bchan_cnt;
	int tchan_cnt;
	int echan_cnt;
	int rchan_cnt;
	int rflow_cnt;
	int tflow_cnt;
	unsigned long *bchan_map;
	unsigned long *tchan_map;
	unsigned long *rchan_map;
	unsigned long *rflow_gp_map;
	unsigned long *rflow_gp_map_allocated;
	unsigned long *rflow_in_use;
	unsigned long *tflow_map;

	struct udma_bchan *bchans;
	struct udma_tchan *tchans;
	struct udma_rchan *rchans;
	struct udma_rflow *rflows;

	struct udma_chan *channels;
	u32 psil_base;
	u32 atype;
	u32 asel;
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
	struct udma_hwdesc hwdesc[];
};

enum udma_chan_state {
	UDMA_CHAN_IS_IDLE = 0, /* not active, no teardown is in progress */
	UDMA_CHAN_IS_ACTIVE, /* Normal operation */
	UDMA_CHAN_IS_TERMINATING, /* channel is being terminated */
};

struct udma_tx_drain {
	struct delayed_work work;
	ktime_t tstamp;
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
	u32 atype;
	u32 asel;
	u32 src_thread;
	u32 dst_thread;
	enum psil_endpoint_type ep_type;
	bool enable_acc32;
	bool enable_burst;
	enum udma_tp_level channel_tpl; /* Channel Throughput Level */

	u32 tr_trigger_type;

	/* PKDMA mapped channel */
	int mapped_channel_id;
	/* PKTDMA default tflow or rflow for mapped channel */
	int default_flow_id;

	enum dma_transfer_direction dir;
};

struct udma_chan {
	struct virt_dma_chan vc;
	struct dma_slave_config	cfg;
	struct udma_dev *ud;
	struct device *dma_dev;
	struct udma_desc *desc;
	struct udma_desc *terminated_desc;
	struct udma_static_tr static_tr;
	char *name;

	struct udma_bchan *bchan;
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
static inline u32 udma_tchanrt_read(struct udma_chan *uc, int reg)
{
	if (!uc->tchan)
		return 0;
	return udma_read(uc->tchan->reg_rt, reg);
}

static inline void udma_tchanrt_write(struct udma_chan *uc, int reg, u32 val)
{
	if (!uc->tchan)
		return;
	udma_write(uc->tchan->reg_rt, reg, val);
}

static inline void udma_tchanrt_update_bits(struct udma_chan *uc, int reg,
					    u32 mask, u32 val)
{
	if (!uc->tchan)
		return;
	udma_update_bits(uc->tchan->reg_rt, reg, mask, val);
}

/* RCHANRT */
static inline u32 udma_rchanrt_read(struct udma_chan *uc, int reg)
{
	if (!uc->rchan)
		return 0;
	return udma_read(uc->rchan->reg_rt, reg);
}

static inline void udma_rchanrt_write(struct udma_chan *uc, int reg, u32 val)
{
	if (!uc->rchan)
		return;
	udma_write(uc->rchan->reg_rt, reg, val);
}

static inline void udma_rchanrt_update_bits(struct udma_chan *uc, int reg,
					    u32 mask, u32 val)
{
	if (!uc->rchan)
		return;
	udma_update_bits(uc->rchan->reg_rt, reg, mask, val);
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

static void k3_configure_chan_coherency(struct dma_chan *chan, u32 asel)
{
	struct device *chan_dev = &chan->dev->device;

	if (asel == 0) {
		/* No special handling for the channel */
		chan->dev->chan_dma_dev = false;

		chan_dev->dma_coherent = false;
		chan_dev->dma_parms = NULL;
	} else if (asel == 14 || asel == 15) {
		chan->dev->chan_dma_dev = true;

		chan_dev->dma_coherent = true;
		dma_coerce_mask_and_coherent(chan_dev, DMA_BIT_MASK(48));
		chan_dev->dma_parms = chan_dev->parent->dma_parms;
	} else {
		dev_warn(chan->device->dev, "Invalid ASEL value: %u\n", asel);

		chan_dev->dma_coherent = false;
		chan_dev->dma_parms = NULL;
	}
}

static u8 udma_get_chan_tpl_index(struct udma_tpl *tpl_map, int chan_id)
{
	int i;

	for (i = 0; i < tpl_map->levels; i++) {
		if (chan_id >= tpl_map->start_idx[i])
			return i;
	}

	return 0;
}

static void udma_reset_uchan(struct udma_chan *uc)
{
	memset(&uc->config, 0, sizeof(uc->config));
	uc->config.remote_thread_id = -1;
	uc->config.mapped_channel_id = -1;
	uc->config.default_flow_id = -1;
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
			offset = UDMA_CHAN_RT_STDATA_REG + i * 4;
			dev_dbg(dev, "TRT_STDATA[%02d]: 0x%08x\n", i,
				udma_tchanrt_read(uc, offset));
		}
	}

	if (uc->config.dir == DMA_DEV_TO_MEM || uc->config.dir == DMA_MEM_TO_MEM) {
		dev_dbg(dev, "RCHAN State data:\n");
		for (i = 0; i < 32; i++) {
			offset = UDMA_CHAN_RT_STDATA_REG + i * 4;
			dev_dbg(dev, "RRT_STDATA[%02d]: 0x%08x\n", i,
				udma_rchanrt_read(uc, offset));
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
		dma_free_coherent(uc->dma_dev, d->hwdesc[0].cppi5_desc_size,
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
		trt_ctl = udma_tchanrt_read(uc, UDMA_CHAN_RT_CTL_REG);
	if (uc->rchan)
		rrt_ctl = udma_rchanrt_read(uc, UDMA_CHAN_RT_CTL_REG);

	if (trt_ctl & UDMA_CHAN_RT_CTL_EN || rrt_ctl & UDMA_CHAN_RT_CTL_EN)
		return true;

	return false;
}

static bool udma_is_chan_paused(struct udma_chan *uc)
{
	u32 val, pause_mask;

	switch (uc->config.dir) {
	case DMA_DEV_TO_MEM:
		val = udma_rchanrt_read(uc, UDMA_CHAN_RT_PEER_RT_EN_REG);
		pause_mask = UDMA_PEER_RT_EN_PAUSE;
		break;
	case DMA_MEM_TO_DEV:
		val = udma_tchanrt_read(uc, UDMA_CHAN_RT_PEER_RT_EN_REG);
		pause_mask = UDMA_PEER_RT_EN_PAUSE;
		break;
	case DMA_MEM_TO_MEM:
		val = udma_tchanrt_read(uc, UDMA_CHAN_RT_CTL_REG);
		pause_mask = UDMA_CHAN_RT_CTL_PAUSE;
		break;
	default:
		return false;
	}

	if (val & pause_mask)
		return true;

	return false;
}

static inline dma_addr_t udma_get_rx_flush_hwdesc_paddr(struct udma_chan *uc)
{
	return uc->ud->rx_flush.hwdescs[uc->config.pkt_mode].cppi5_desc_paddr;
}

static int udma_push_to_ring(struct udma_chan *uc, int idx)
{
	struct udma_desc *d = uc->desc;
	struct k3_ring *ring = NULL;
	dma_addr_t paddr;

	switch (uc->config.dir) {
	case DMA_DEV_TO_MEM:
		ring = uc->rflow->fd_ring;
		break;
	case DMA_MEM_TO_DEV:
	case DMA_MEM_TO_MEM:
		ring = uc->tchan->t_ring;
		break;
	default:
		return -EINVAL;
	}

	/* RX flush packet: idx == -1 is only passed in case of DEV_TO_MEM */
	if (idx == -1) {
		paddr = udma_get_rx_flush_hwdesc_paddr(uc);
	} else {
		paddr = udma_curr_cppi5_desc_paddr(d, idx);

		wmb(); /* Ensure that writes are not moved over this point */
	}

	return k3_ringacc_ring_push(ring, &paddr);
}

static bool udma_desc_is_rx_flush(struct udma_chan *uc, dma_addr_t addr)
{
	if (uc->config.dir != DMA_DEV_TO_MEM)
		return false;

	if (addr == udma_get_rx_flush_hwdesc_paddr(uc))
		return true;

	return false;
}

static int udma_pop_from_ring(struct udma_chan *uc, dma_addr_t *addr)
{
	struct k3_ring *ring = NULL;
	int ret;

	switch (uc->config.dir) {
	case DMA_DEV_TO_MEM:
		ring = uc->rflow->r_ring;
		break;
	case DMA_MEM_TO_DEV:
	case DMA_MEM_TO_MEM:
		ring = uc->tchan->tc_ring;
		break;
	default:
		return -ENOENT;
	}

	ret = k3_ringacc_ring_pop(ring, addr);
	if (ret)
		return ret;

	rmb(); /* Ensure that reads are not moved before this point */

	/* Teardown completion */
	if (cppi5_desc_is_tdcm(*addr))
		return 0;

	/* Check for flush descriptor */
	if (udma_desc_is_rx_flush(uc, *addr))
		return -ENOENT;

	return 0;
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
}

static void udma_decrement_byte_counters(struct udma_chan *uc, u32 val)
{
	if (uc->desc->dir == DMA_DEV_TO_MEM) {
		udma_rchanrt_write(uc, UDMA_CHAN_RT_BCNT_REG, val);
		udma_rchanrt_write(uc, UDMA_CHAN_RT_SBCNT_REG, val);
		udma_rchanrt_write(uc, UDMA_CHAN_RT_PEER_BCNT_REG, val);
	} else {
		udma_tchanrt_write(uc, UDMA_CHAN_RT_BCNT_REG, val);
		udma_tchanrt_write(uc, UDMA_CHAN_RT_SBCNT_REG, val);
		if (!uc->bchan)
			udma_tchanrt_write(uc, UDMA_CHAN_RT_PEER_BCNT_REG, val);
	}
}

static void udma_reset_counters(struct udma_chan *uc)
{
	u32 val;

	if (uc->tchan) {
		val = udma_tchanrt_read(uc, UDMA_CHAN_RT_BCNT_REG);
		udma_tchanrt_write(uc, UDMA_CHAN_RT_BCNT_REG, val);

		val = udma_tchanrt_read(uc, UDMA_CHAN_RT_SBCNT_REG);
		udma_tchanrt_write(uc, UDMA_CHAN_RT_SBCNT_REG, val);

		val = udma_tchanrt_read(uc, UDMA_CHAN_RT_PCNT_REG);
		udma_tchanrt_write(uc, UDMA_CHAN_RT_PCNT_REG, val);

		if (!uc->bchan) {
			val = udma_tchanrt_read(uc, UDMA_CHAN_RT_PEER_BCNT_REG);
			udma_tchanrt_write(uc, UDMA_CHAN_RT_PEER_BCNT_REG, val);
		}
	}

	if (uc->rchan) {
		val = udma_rchanrt_read(uc, UDMA_CHAN_RT_BCNT_REG);
		udma_rchanrt_write(uc, UDMA_CHAN_RT_BCNT_REG, val);

		val = udma_rchanrt_read(uc, UDMA_CHAN_RT_SBCNT_REG);
		udma_rchanrt_write(uc, UDMA_CHAN_RT_SBCNT_REG, val);

		val = udma_rchanrt_read(uc, UDMA_CHAN_RT_PCNT_REG);
		udma_rchanrt_write(uc, UDMA_CHAN_RT_PCNT_REG, val);

		val = udma_rchanrt_read(uc, UDMA_CHAN_RT_PEER_BCNT_REG);
		udma_rchanrt_write(uc, UDMA_CHAN_RT_PEER_BCNT_REG, val);
	}
}

static int udma_reset_chan(struct udma_chan *uc, bool hard)
{
	switch (uc->config.dir) {
	case DMA_DEV_TO_MEM:
		udma_rchanrt_write(uc, UDMA_CHAN_RT_PEER_RT_EN_REG, 0);
		udma_rchanrt_write(uc, UDMA_CHAN_RT_CTL_REG, 0);
		break;
	case DMA_MEM_TO_DEV:
		udma_tchanrt_write(uc, UDMA_CHAN_RT_CTL_REG, 0);
		udma_tchanrt_write(uc, UDMA_CHAN_RT_PEER_RT_EN_REG, 0);
		break;
	case DMA_MEM_TO_MEM:
		udma_rchanrt_write(uc, UDMA_CHAN_RT_CTL_REG, 0);
		udma_tchanrt_write(uc, UDMA_CHAN_RT_CTL_REG, 0);
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
			udma_rchanrt_write(uc, UDMA_CHAN_RT_CTL_REG,
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

	if (uc->ud->match_data->type == DMA_TYPE_UDMA && ucc->pkt_mode &&
	    (uc->cyclic || ucc->dir == DMA_DEV_TO_MEM)) {
		int i;

		/*
		 * UDMA only: Push all descriptors to ring for packet mode
		 * cyclic or RX
		 * PKTDMA supports pre-linked descriptor and cyclic is not
		 * supported
		 */
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

			udma_rchanrt_write(uc,
					   UDMA_CHAN_RT_PEER_STATIC_TR_XY_REG,
					   val);

			udma_rchanrt_write(uc,
				UDMA_CHAN_RT_PEER_STATIC_TR_Z_REG,
				PDMA_STATIC_TR_Z(uc->desc->static_tr.bstcnt,
						 match_data->statictr_z_mask));

			/* save the current staticTR configuration */
			memcpy(&uc->static_tr, &uc->desc->static_tr,
			       sizeof(uc->static_tr));
		}

		udma_rchanrt_write(uc, UDMA_CHAN_RT_CTL_REG,
				   UDMA_CHAN_RT_CTL_EN);

		/* Enable remote */
		udma_rchanrt_write(uc, UDMA_CHAN_RT_PEER_RT_EN_REG,
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

			udma_tchanrt_write(uc,
					   UDMA_CHAN_RT_PEER_STATIC_TR_XY_REG,
					   val);

			/* save the current staticTR configuration */
			memcpy(&uc->static_tr, &uc->desc->static_tr,
			       sizeof(uc->static_tr));
		}

		/* Enable remote */
		udma_tchanrt_write(uc, UDMA_CHAN_RT_PEER_RT_EN_REG,
				   UDMA_PEER_RT_EN_ENABLE);

		udma_tchanrt_write(uc, UDMA_CHAN_RT_CTL_REG,
				   UDMA_CHAN_RT_CTL_EN);

		break;
	case DMA_MEM_TO_MEM:
		udma_rchanrt_write(uc, UDMA_CHAN_RT_CTL_REG,
				   UDMA_CHAN_RT_CTL_EN);
		udma_tchanrt_write(uc, UDMA_CHAN_RT_CTL_REG,
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
		if (!uc->cyclic && !uc->desc)
			udma_push_to_ring(uc, -1);

		udma_rchanrt_write(uc, UDMA_CHAN_RT_PEER_RT_EN_REG,
				   UDMA_PEER_RT_EN_ENABLE |
				   UDMA_PEER_RT_EN_TEARDOWN);
		break;
	case DMA_MEM_TO_DEV:
		udma_tchanrt_write(uc, UDMA_CHAN_RT_PEER_RT_EN_REG,
				   UDMA_PEER_RT_EN_ENABLE |
				   UDMA_PEER_RT_EN_FLUSH);
		udma_tchanrt_write(uc, UDMA_CHAN_RT_CTL_REG,
				   UDMA_CHAN_RT_CTL_EN |
				   UDMA_CHAN_RT_CTL_TDOWN);
		break;
	case DMA_MEM_TO_MEM:
		udma_tchanrt_write(uc, UDMA_CHAN_RT_CTL_REG,
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

	peer_bcnt = udma_tchanrt_read(uc, UDMA_CHAN_RT_PEER_BCNT_REG);
	bcnt = udma_tchanrt_read(uc, UDMA_CHAN_RT_BCNT_REG);

	/* Transfer is incomplete, store current residue and time stamp */
	if (peer_bcnt < bcnt) {
		uc->tx_drain.residue = bcnt - peer_bcnt;
		uc->tx_drain.tstamp = ktime_get();
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
	ktime_t time_diff;
	unsigned long delay;

	while (1) {
		if (uc->desc) {
			/* Get previous residue and time stamp */
			residue_diff = uc->tx_drain.residue;
			time_diff = uc->tx_drain.tstamp;
			/*
			 * Get current residue and time stamp or see if
			 * transfer is complete
			 */
			desc_done = udma_is_desc_really_done(uc, uc->desc);
		}

		if (!desc_done) {
			/*
			 * Find the time delta and residue delta w.r.t
			 * previous poll
			 */
			time_diff = ktime_sub(uc->tx_drain.tstamp,
					      time_diff) + 1;
			residue_diff -= uc->tx_drain.residue;
			if (residue_diff) {
				/*
				 * Try to guess when we should check
				 * next time by calculating rate at
				 * which data is being drained at the
				 * peer device
				 */
				delay = (time_diff / residue_diff) *
					uc->tx_drain.residue;
			} else {
				/* No progress, check again in 1 second  */
				schedule_delayed_work(&uc->tx_drain.work, HZ);
				break;
			}

			usleep_range(ktime_to_us(delay),
				     ktime_to_us(delay) + 10);
			continue;
		}

		if (uc->desc) {
			struct udma_desc *d = uc->desc;

			udma_decrement_byte_counters(uc, d->residue);
			udma_start(uc);
			vchan_cookie_complete(&d->vd);
			break;
		}

		break;
	}
}

static irqreturn_t udma_ring_irq_handler(int irq, void *data)
{
	struct udma_chan *uc = data;
	struct udma_desc *d;
	dma_addr_t paddr = 0;

	if (udma_pop_from_ring(uc, &paddr) || !paddr)
		return IRQ_HANDLED;

	spin_lock(&uc->vc.lock);

	/* Teardown completion message */
	if (cppi5_desc_is_tdcm(paddr)) {
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

		if (d == uc->desc) {
			/* active descriptor */
			if (uc->cyclic) {
				udma_cyclic_packet_elapsed(uc);
				vchan_cyclic_callback(&d->vd);
			} else {
				if (udma_is_desc_really_done(uc, d)) {
					udma_decrement_byte_counters(uc, d->residue);
					udma_start(uc);
					vchan_cookie_complete(&d->vd);
				} else {
					schedule_delayed_work(&uc->tx_drain.work,
							      0);
				}
			}
		} else {
			/*
			 * terminated descriptor, mark the descriptor as
			 * completed to update the channel's cookie marker
			 */
			dma_cookie_complete(&d->vd.tx);
		}
	}
out:
	spin_unlock(&uc->vc.lock);

	return IRQ_HANDLED;
}

static irqreturn_t udma_udma_irq_handler(int irq, void *data)
{
	struct udma_chan *uc = data;
	struct udma_desc *d;

	spin_lock(&uc->vc.lock);
	d = uc->desc;
	if (d) {
		d->tr_idx = (d->tr_idx + 1) % d->sglen;

		if (uc->cyclic) {
			vchan_cyclic_callback(&d->vd);
		} else {
			/* TODO: figure out the real amount of data */
			udma_decrement_byte_counters(uc, d->residue);
			udma_start(uc);
			vchan_cookie_complete(&d->vd);
		}
	}

	spin_unlock(&uc->vc.lock);

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

	if (ud->rflow_gp_map) {
		/* GP rflow has to be allocated first */
		if (!test_bit(id, ud->rflow_gp_map) &&
		    !test_bit(id, ud->rflow_gp_map_allocated))
			return ERR_PTR(-EINVAL);
	}

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
		if (tpl >= ud->res##_tpl.levels)			\
			tpl = ud->res##_tpl.levels - 1;			\
									\
		start = ud->res##_tpl.start_idx[tpl];			\
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

UDMA_RESERVE_RESOURCE(bchan);
UDMA_RESERVE_RESOURCE(tchan);
UDMA_RESERVE_RESOURCE(rchan);

static int bcdma_get_bchan(struct udma_chan *uc)
{
	struct udma_dev *ud = uc->ud;
	enum udma_tp_level tpl;
	int ret;

	if (uc->bchan) {
		dev_dbg(ud->dev, "chan%d: already have bchan%d allocated\n",
			uc->id, uc->bchan->id);
		return 0;
	}

	/*
	 * Use normal channels for peripherals, and highest TPL channel for
	 * mem2mem
	 */
	if (uc->config.tr_trigger_type)
		tpl = 0;
	else
		tpl = ud->bchan_tpl.levels - 1;

	uc->bchan = __udma_reserve_bchan(ud, tpl, -1);
	if (IS_ERR(uc->bchan)) {
		ret = PTR_ERR(uc->bchan);
		uc->bchan = NULL;
		return ret;
	}

	uc->tchan = uc->bchan;

	return 0;
}

static int udma_get_tchan(struct udma_chan *uc)
{
	struct udma_dev *ud = uc->ud;
	int ret;

	if (uc->tchan) {
		dev_dbg(ud->dev, "chan%d: already have tchan%d allocated\n",
			uc->id, uc->tchan->id);
		return 0;
	}

	/*
	 * mapped_channel_id is -1 for UDMA, BCDMA and PKTDMA unmapped channels.
	 * For PKTDMA mapped channels it is configured to a channel which must
	 * be used to service the peripheral.
	 */
	uc->tchan = __udma_reserve_tchan(ud, uc->config.channel_tpl,
					 uc->config.mapped_channel_id);
	if (IS_ERR(uc->tchan)) {
		ret = PTR_ERR(uc->tchan);
		uc->tchan = NULL;
		return ret;
	}

	if (ud->tflow_cnt) {
		int tflow_id;

		/* Only PKTDMA have support for tx flows */
		if (uc->config.default_flow_id >= 0)
			tflow_id = uc->config.default_flow_id;
		else
			tflow_id = uc->tchan->id;

		if (test_bit(tflow_id, ud->tflow_map)) {
			dev_err(ud->dev, "tflow%d is in use\n", tflow_id);
			clear_bit(uc->tchan->id, ud->tchan_map);
			uc->tchan = NULL;
			return -ENOENT;
		}

		uc->tchan->tflow_id = tflow_id;
		set_bit(tflow_id, ud->tflow_map);
	} else {
		uc->tchan->tflow_id = -1;
	}

	return 0;
}

static int udma_get_rchan(struct udma_chan *uc)
{
	struct udma_dev *ud = uc->ud;
	int ret;

	if (uc->rchan) {
		dev_dbg(ud->dev, "chan%d: already have rchan%d allocated\n",
			uc->id, uc->rchan->id);
		return 0;
	}

	/*
	 * mapped_channel_id is -1 for UDMA, BCDMA and PKTDMA unmapped channels.
	 * For PKTDMA mapped channels it is configured to a channel which must
	 * be used to service the peripheral.
	 */
	uc->rchan = __udma_reserve_rchan(ud, uc->config.channel_tpl,
					 uc->config.mapped_channel_id);
	if (IS_ERR(uc->rchan)) {
		ret = PTR_ERR(uc->rchan);
		uc->rchan = NULL;
		return ret;
	}

	return 0;
}

static int udma_get_chan_pair(struct udma_chan *uc)
{
	struct udma_dev *ud = uc->ud;
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
	/*
	 * Try to use the highest TPL channel pair for MEM_TO_MEM channels
	 * Note: in UDMAP the channel TPL is symmetric between tchan and rchan
	 */
	chan_id = ud->tchan_tpl.start_idx[ud->tchan_tpl.levels - 1];
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

	/* UDMA does not use tx flows */
	uc->tchan->tflow_id = -1;

	return 0;
}

static int udma_get_rflow(struct udma_chan *uc, int flow_id)
{
	struct udma_dev *ud = uc->ud;
	int ret;

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
	if (IS_ERR(uc->rflow)) {
		ret = PTR_ERR(uc->rflow);
		uc->rflow = NULL;
		return ret;
	}

	return 0;
}

static void bcdma_put_bchan(struct udma_chan *uc)
{
	struct udma_dev *ud = uc->ud;

	if (uc->bchan) {
		dev_dbg(ud->dev, "chan%d: put bchan%d\n", uc->id,
			uc->bchan->id);
		clear_bit(uc->bchan->id, ud->bchan_map);
		uc->bchan = NULL;
		uc->tchan = NULL;
	}
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

		if (uc->tchan->tflow_id >= 0)
			clear_bit(uc->tchan->tflow_id, ud->tflow_map);

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

static void bcdma_free_bchan_resources(struct udma_chan *uc)
{
	if (!uc->bchan)
		return;

	k3_ringacc_ring_free(uc->bchan->tc_ring);
	k3_ringacc_ring_free(uc->bchan->t_ring);
	uc->bchan->tc_ring = NULL;
	uc->bchan->t_ring = NULL;
	k3_configure_chan_coherency(&uc->vc.chan, 0);

	bcdma_put_bchan(uc);
}

static int bcdma_alloc_bchan_resources(struct udma_chan *uc)
{
	struct k3_ring_cfg ring_cfg;
	struct udma_dev *ud = uc->ud;
	int ret;

	ret = bcdma_get_bchan(uc);
	if (ret)
		return ret;

	ret = k3_ringacc_request_rings_pair(ud->ringacc, uc->bchan->id, -1,
					    &uc->bchan->t_ring,
					    &uc->bchan->tc_ring);
	if (ret) {
		ret = -EBUSY;
		goto err_ring;
	}

	memset(&ring_cfg, 0, sizeof(ring_cfg));
	ring_cfg.size = K3_UDMA_DEFAULT_RING_SIZE;
	ring_cfg.elm_size = K3_RINGACC_RING_ELSIZE_8;
	ring_cfg.mode = K3_RINGACC_RING_MODE_RING;

	k3_configure_chan_coherency(&uc->vc.chan, ud->asel);
	ring_cfg.asel = ud->asel;
	ring_cfg.dma_dev = dmaengine_get_dma_device(&uc->vc.chan);

	ret = k3_ringacc_ring_cfg(uc->bchan->t_ring, &ring_cfg);
	if (ret)
		goto err_ringcfg;

	return 0;

err_ringcfg:
	k3_ringacc_ring_free(uc->bchan->tc_ring);
	uc->bchan->tc_ring = NULL;
	k3_ringacc_ring_free(uc->bchan->t_ring);
	uc->bchan->t_ring = NULL;
	k3_configure_chan_coherency(&uc->vc.chan, 0);
err_ring:
	bcdma_put_bchan(uc);

	return ret;
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
	struct udma_tchan *tchan;
	int ring_idx, ret;

	ret = udma_get_tchan(uc);
	if (ret)
		return ret;

	tchan = uc->tchan;
	if (tchan->tflow_id >= 0)
		ring_idx = tchan->tflow_id;
	else
		ring_idx = ud->bchan_cnt + tchan->id;

	ret = k3_ringacc_request_rings_pair(ud->ringacc, ring_idx, -1,
					    &tchan->t_ring,
					    &tchan->tc_ring);
	if (ret) {
		ret = -EBUSY;
		goto err_ring;
	}

	memset(&ring_cfg, 0, sizeof(ring_cfg));
	ring_cfg.size = K3_UDMA_DEFAULT_RING_SIZE;
	ring_cfg.elm_size = K3_RINGACC_RING_ELSIZE_8;
	if (ud->match_data->type == DMA_TYPE_UDMA) {
		ring_cfg.mode = K3_RINGACC_RING_MODE_MESSAGE;
	} else {
		ring_cfg.mode = K3_RINGACC_RING_MODE_RING;

		k3_configure_chan_coherency(&uc->vc.chan, uc->config.asel);
		ring_cfg.asel = uc->config.asel;
		ring_cfg.dma_dev = dmaengine_get_dma_device(&uc->vc.chan);
	}

	ret = k3_ringacc_ring_cfg(tchan->t_ring, &ring_cfg);
	ret |= k3_ringacc_ring_cfg(tchan->tc_ring, &ring_cfg);

	if (ret)
		goto err_ringcfg;

	return 0;

err_ringcfg:
	k3_ringacc_ring_free(uc->tchan->tc_ring);
	uc->tchan->tc_ring = NULL;
	k3_ringacc_ring_free(uc->tchan->t_ring);
	uc->tchan->t_ring = NULL;
err_ring:
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

	if (uc->config.default_flow_id >= 0)
		ret = udma_get_rflow(uc, uc->config.default_flow_id);
	else
		ret = udma_get_rflow(uc, uc->rchan->id);

	if (ret) {
		ret = -EBUSY;
		goto err_rflow;
	}

	rflow = uc->rflow;
	if (ud->tflow_cnt)
		fd_ring_id = ud->tflow_cnt + rflow->id;
	else
		fd_ring_id = ud->bchan_cnt + ud->tchan_cnt + ud->echan_cnt +
			     uc->rchan->id;

	ret = k3_ringacc_request_rings_pair(ud->ringacc, fd_ring_id, -1,
					    &rflow->fd_ring, &rflow->r_ring);
	if (ret) {
		ret = -EBUSY;
		goto err_ring;
	}

	memset(&ring_cfg, 0, sizeof(ring_cfg));

	ring_cfg.elm_size = K3_RINGACC_RING_ELSIZE_8;
	if (ud->match_data->type == DMA_TYPE_UDMA) {
		if (uc->config.pkt_mode)
			ring_cfg.size = SG_MAX_SEGMENTS;
		else
			ring_cfg.size = K3_UDMA_DEFAULT_RING_SIZE;

		ring_cfg.mode = K3_RINGACC_RING_MODE_MESSAGE;
	} else {
		ring_cfg.size = K3_UDMA_DEFAULT_RING_SIZE;
		ring_cfg.mode = K3_RINGACC_RING_MODE_RING;

		k3_configure_chan_coherency(&uc->vc.chan, uc->config.asel);
		ring_cfg.asel = uc->config.asel;
		ring_cfg.dma_dev = dmaengine_get_dma_device(&uc->vc.chan);
	}

	ret = k3_ringacc_ring_cfg(rflow->fd_ring, &ring_cfg);

	ring_cfg.size = K3_UDMA_DEFAULT_RING_SIZE;
	ret |= k3_ringacc_ring_cfg(rflow->r_ring, &ring_cfg);

	if (ret)
		goto err_ringcfg;

	return 0;

err_ringcfg:
	k3_ringacc_ring_free(rflow->r_ring);
	rflow->r_ring = NULL;
	k3_ringacc_ring_free(rflow->fd_ring);
	rflow->fd_ring = NULL;
err_ring:
	udma_put_rflow(uc);
err_rflow:
	udma_put_rchan(uc);

	return ret;
}

#define TISCI_BCDMA_BCHAN_VALID_PARAMS (			\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_PAUSE_ON_ERR_VALID |	\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_EXTENDED_CH_TYPE_VALID)

#define TISCI_BCDMA_TCHAN_VALID_PARAMS (			\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_PAUSE_ON_ERR_VALID |	\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_TX_SUPR_TDPKT_VALID)

#define TISCI_BCDMA_RCHAN_VALID_PARAMS (			\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_PAUSE_ON_ERR_VALID)

#define TISCI_UDMA_TCHAN_VALID_PARAMS (				\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_PAUSE_ON_ERR_VALID |	\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_TX_FILT_EINFO_VALID |	\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_TX_FILT_PSWORDS_VALID |	\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_CHAN_TYPE_VALID |		\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_TX_SUPR_TDPKT_VALID |	\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_FETCH_SIZE_VALID |		\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_CQ_QNUM_VALID |		\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_ATYPE_VALID)

#define TISCI_UDMA_RCHAN_VALID_PARAMS (				\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_PAUSE_ON_ERR_VALID |	\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_FETCH_SIZE_VALID |		\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_CQ_QNUM_VALID |		\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_CHAN_TYPE_VALID |		\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_RX_IGNORE_SHORT_VALID |	\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_RX_IGNORE_LONG_VALID |	\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_RX_FLOWID_START_VALID |	\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_RX_FLOWID_CNT_VALID |	\
	TI_SCI_MSG_VALUE_RM_UDMAP_CH_ATYPE_VALID)

static int udma_tisci_m2m_channel_config(struct udma_chan *uc)
{
	struct udma_dev *ud = uc->ud;
	struct udma_tisci_rm *tisci_rm = &ud->tisci_rm;
	const struct ti_sci_rm_udmap_ops *tisci_ops = tisci_rm->tisci_udmap_ops;
	struct udma_tchan *tchan = uc->tchan;
	struct udma_rchan *rchan = uc->rchan;
	u8 burst_size = 0;
	int ret;
	u8 tpl;

	/* Non synchronized - mem to mem type of transfer */
	int tc_ring = k3_ringacc_get_ring_id(tchan->tc_ring);
	struct ti_sci_msg_rm_udmap_tx_ch_cfg req_tx = { 0 };
	struct ti_sci_msg_rm_udmap_rx_ch_cfg req_rx = { 0 };

	if (ud->match_data->flags & UDMA_FLAG_BURST_SIZE) {
		tpl = udma_get_chan_tpl_index(&ud->tchan_tpl, tchan->id);

		burst_size = ud->match_data->burst_size[tpl];
	}

	req_tx.valid_params = TISCI_UDMA_TCHAN_VALID_PARAMS;
	req_tx.nav_id = tisci_rm->tisci_dev_id;
	req_tx.index = tchan->id;
	req_tx.tx_chan_type = TI_SCI_RM_UDMAP_CHAN_TYPE_3RDP_BCOPY_PBRR;
	req_tx.tx_fetch_size = sizeof(struct cppi5_desc_hdr_t) >> 2;
	req_tx.txcq_qnum = tc_ring;
	req_tx.tx_atype = ud->atype;
	if (burst_size) {
		req_tx.valid_params |= TI_SCI_MSG_VALUE_RM_UDMAP_CH_BURST_SIZE_VALID;
		req_tx.tx_burst_size = burst_size;
	}

	ret = tisci_ops->tx_ch_cfg(tisci_rm->tisci, &req_tx);
	if (ret) {
		dev_err(ud->dev, "tchan%d cfg failed %d\n", tchan->id, ret);
		return ret;
	}

	req_rx.valid_params = TISCI_UDMA_RCHAN_VALID_PARAMS;
	req_rx.nav_id = tisci_rm->tisci_dev_id;
	req_rx.index = rchan->id;
	req_rx.rx_fetch_size = sizeof(struct cppi5_desc_hdr_t) >> 2;
	req_rx.rxcq_qnum = tc_ring;
	req_rx.rx_chan_type = TI_SCI_RM_UDMAP_CHAN_TYPE_3RDP_BCOPY_PBRR;
	req_rx.rx_atype = ud->atype;
	if (burst_size) {
		req_rx.valid_params |= TI_SCI_MSG_VALUE_RM_UDMAP_CH_BURST_SIZE_VALID;
		req_rx.rx_burst_size = burst_size;
	}

	ret = tisci_ops->rx_ch_cfg(tisci_rm->tisci, &req_rx);
	if (ret)
		dev_err(ud->dev, "rchan%d alloc failed %d\n", rchan->id, ret);

	return ret;
}

static int bcdma_tisci_m2m_channel_config(struct udma_chan *uc)
{
	struct udma_dev *ud = uc->ud;
	struct udma_tisci_rm *tisci_rm = &ud->tisci_rm;
	const struct ti_sci_rm_udmap_ops *tisci_ops = tisci_rm->tisci_udmap_ops;
	struct ti_sci_msg_rm_udmap_tx_ch_cfg req_tx = { 0 };
	struct udma_bchan *bchan = uc->bchan;
	u8 burst_size = 0;
	int ret;
	u8 tpl;

	if (ud->match_data->flags & UDMA_FLAG_BURST_SIZE) {
		tpl = udma_get_chan_tpl_index(&ud->bchan_tpl, bchan->id);

		burst_size = ud->match_data->burst_size[tpl];
	}

	req_tx.valid_params = TISCI_BCDMA_BCHAN_VALID_PARAMS;
	req_tx.nav_id = tisci_rm->tisci_dev_id;
	req_tx.extended_ch_type = TI_SCI_RM_BCDMA_EXTENDED_CH_TYPE_BCHAN;
	req_tx.index = bchan->id;
	if (burst_size) {
		req_tx.valid_params |= TI_SCI_MSG_VALUE_RM_UDMAP_CH_BURST_SIZE_VALID;
		req_tx.tx_burst_size = burst_size;
	}

	ret = tisci_ops->tx_ch_cfg(tisci_rm->tisci, &req_tx);
	if (ret)
		dev_err(ud->dev, "bchan%d cfg failed %d\n", bchan->id, ret);

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
	int ret;

	if (uc->config.pkt_mode) {
		mode = TI_SCI_RM_UDMAP_CHAN_TYPE_PKT_PBRR;
		fetch_size = cppi5_hdesc_calc_size(uc->config.needs_epib,
						   uc->config.psd_size, 0);
	} else {
		mode = TI_SCI_RM_UDMAP_CHAN_TYPE_3RDP_PBRR;
		fetch_size = sizeof(struct cppi5_desc_hdr_t);
	}

	req_tx.valid_params = TISCI_UDMA_TCHAN_VALID_PARAMS;
	req_tx.nav_id = tisci_rm->tisci_dev_id;
	req_tx.index = tchan->id;
	req_tx.tx_chan_type = mode;
	req_tx.tx_supr_tdpkt = uc->config.notdpkt;
	req_tx.tx_fetch_size = fetch_size >> 2;
	req_tx.txcq_qnum = tc_ring;
	req_tx.tx_atype = uc->config.atype;
	if (uc->config.ep_type == PSIL_EP_PDMA_XY &&
	    ud->match_data->flags & UDMA_FLAG_TDTYPE) {
		/* wait for peer to complete the teardown for PDMAs */
		req_tx.valid_params |=
				TI_SCI_MSG_VALUE_RM_UDMAP_CH_TX_TDTYPE_VALID;
		req_tx.tx_tdtype = 1;
	}

	ret = tisci_ops->tx_ch_cfg(tisci_rm->tisci, &req_tx);
	if (ret)
		dev_err(ud->dev, "tchan%d cfg failed %d\n", tchan->id, ret);

	return ret;
}

static int bcdma_tisci_tx_channel_config(struct udma_chan *uc)
{
	struct udma_dev *ud = uc->ud;
	struct udma_tisci_rm *tisci_rm = &ud->tisci_rm;
	const struct ti_sci_rm_udmap_ops *tisci_ops = tisci_rm->tisci_udmap_ops;
	struct udma_tchan *tchan = uc->tchan;
	struct ti_sci_msg_rm_udmap_tx_ch_cfg req_tx = { 0 };
	int ret;

	req_tx.valid_params = TISCI_BCDMA_TCHAN_VALID_PARAMS;
	req_tx.nav_id = tisci_rm->tisci_dev_id;
	req_tx.index = tchan->id;
	req_tx.tx_supr_tdpkt = uc->config.notdpkt;
	if (ud->match_data->flags & UDMA_FLAG_TDTYPE) {
		/* wait for peer to complete the teardown for PDMAs */
		req_tx.valid_params |=
				TI_SCI_MSG_VALUE_RM_UDMAP_CH_TX_TDTYPE_VALID;
		req_tx.tx_tdtype = 1;
	}

	ret = tisci_ops->tx_ch_cfg(tisci_rm->tisci, &req_tx);
	if (ret)
		dev_err(ud->dev, "tchan%d cfg failed %d\n", tchan->id, ret);

	return ret;
}

#define pktdma_tisci_tx_channel_config bcdma_tisci_tx_channel_config

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
	int ret;

	if (uc->config.pkt_mode) {
		mode = TI_SCI_RM_UDMAP_CHAN_TYPE_PKT_PBRR;
		fetch_size = cppi5_hdesc_calc_size(uc->config.needs_epib,
						   uc->config.psd_size, 0);
	} else {
		mode = TI_SCI_RM_UDMAP_CHAN_TYPE_3RDP_PBRR;
		fetch_size = sizeof(struct cppi5_desc_hdr_t);
	}

	req_rx.valid_params = TISCI_UDMA_RCHAN_VALID_PARAMS;
	req_rx.nav_id = tisci_rm->tisci_dev_id;
	req_rx.index = rchan->id;
	req_rx.rx_fetch_size =  fetch_size >> 2;
	req_rx.rxcq_qnum = rx_ring;
	req_rx.rx_chan_type = mode;
	req_rx.rx_atype = uc->config.atype;

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

static int bcdma_tisci_rx_channel_config(struct udma_chan *uc)
{
	struct udma_dev *ud = uc->ud;
	struct udma_tisci_rm *tisci_rm = &ud->tisci_rm;
	const struct ti_sci_rm_udmap_ops *tisci_ops = tisci_rm->tisci_udmap_ops;
	struct udma_rchan *rchan = uc->rchan;
	struct ti_sci_msg_rm_udmap_rx_ch_cfg req_rx = { 0 };
	int ret;

	req_rx.valid_params = TISCI_BCDMA_RCHAN_VALID_PARAMS;
	req_rx.nav_id = tisci_rm->tisci_dev_id;
	req_rx.index = rchan->id;

	ret = tisci_ops->rx_ch_cfg(tisci_rm->tisci, &req_rx);
	if (ret)
		dev_err(ud->dev, "rchan%d cfg failed %d\n", rchan->id, ret);

	return ret;
}

static int pktdma_tisci_rx_channel_config(struct udma_chan *uc)
{
	struct udma_dev *ud = uc->ud;
	struct udma_tisci_rm *tisci_rm = &ud->tisci_rm;
	const struct ti_sci_rm_udmap_ops *tisci_ops = tisci_rm->tisci_udmap_ops;
	struct ti_sci_msg_rm_udmap_rx_ch_cfg req_rx = { 0 };
	struct ti_sci_msg_rm_udmap_flow_cfg flow_req = { 0 };
	int ret;

	req_rx.valid_params = TISCI_BCDMA_RCHAN_VALID_PARAMS;
	req_rx.nav_id = tisci_rm->tisci_dev_id;
	req_rx.index = uc->rchan->id;

	ret = tisci_ops->rx_ch_cfg(tisci_rm->tisci, &req_rx);
	if (ret) {
		dev_err(ud->dev, "rchan%d cfg failed %d\n", uc->rchan->id, ret);
		return ret;
	}

	flow_req.valid_params =
		TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_EINFO_PRESENT_VALID |
		TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_PSINFO_PRESENT_VALID |
		TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_ERROR_HANDLING_VALID;

	flow_req.nav_id = tisci_rm->tisci_dev_id;
	flow_req.flow_index = uc->rflow->id;

	if (uc->config.needs_epib)
		flow_req.rx_einfo_present = 1;
	else
		flow_req.rx_einfo_present = 0;
	if (uc->config.psd_size)
		flow_req.rx_psinfo_present = 1;
	else
		flow_req.rx_psinfo_present = 0;
	flow_req.rx_error_handling = 1;

	ret = tisci_ops->rx_flow_cfg(tisci_rm->tisci, &flow_req);

	if (ret)
		dev_err(ud->dev, "flow%d config failed: %d\n", uc->rflow->id,
			ret);

	return ret;
}

static int udma_alloc_chan_resources(struct dma_chan *chan)
{
	struct udma_chan *uc = to_udma_chan(chan);
	struct udma_dev *ud = to_udma_dev(chan->device);
	const struct udma_soc_data *soc_data = ud->soc_data;
	struct k3_ring *irq_ring;
	u32 irq_udma_idx;
	int ret;

	uc->dma_dev = ud->dev;

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
			ret = -ENOMEM;
			goto err_cleanup;
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
			goto err_cleanup;

		ret = udma_alloc_tx_resources(uc);
		if (ret) {
			udma_put_rchan(uc);
			goto err_cleanup;
		}

		ret = udma_alloc_rx_resources(uc);
		if (ret) {
			udma_free_tx_resources(uc);
			goto err_cleanup;
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
		if (ret)
			goto err_cleanup;

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
		if (ret)
			goto err_cleanup;

		uc->config.src_thread = uc->config.remote_thread_id;
		uc->config.dst_thread = (ud->psil_base + uc->rchan->id) |
					K3_PSIL_DST_THREAD_ID_OFFSET;

		irq_ring = uc->rflow->r_ring;
		irq_udma_idx = soc_data->oes.udma_rchan + uc->rchan->id;

		ret = udma_tisci_rx_channel_config(uc);
		break;
	default:
		/* Can not happen */
		dev_err(uc->ud->dev, "%s: chan%d invalid direction (%u)\n",
			__func__, uc->id, uc->config.dir);
		ret = -EINVAL;
		goto err_cleanup;

	}

	/* check if the channel configuration was successful */
	if (ret)
		goto err_res_free;

	if (udma_is_chan_running(uc)) {
		dev_warn(ud->dev, "chan%d: is running!\n", uc->id);
		udma_reset_chan(uc, false);
		if (udma_is_chan_running(uc)) {
			dev_err(ud->dev, "chan%d: won't stop!\n", uc->id);
			ret = -EBUSY;
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
		uc->irq_num_udma = msi_get_virq(ud->dev, irq_udma_idx);
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
err_cleanup:
	udma_reset_uchan(uc);

	if (uc->use_dma_pool) {
		dma_pool_destroy(uc->hdesc_pool);
		uc->use_dma_pool = false;
	}

	return ret;
}

static int bcdma_alloc_chan_resources(struct dma_chan *chan)
{
	struct udma_chan *uc = to_udma_chan(chan);
	struct udma_dev *ud = to_udma_dev(chan->device);
	const struct udma_oes_offsets *oes = &ud->soc_data->oes;
	u32 irq_udma_idx, irq_ring_idx;
	int ret;

	/* Only TR mode is supported */
	uc->config.pkt_mode = false;

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

		ret = bcdma_alloc_bchan_resources(uc);
		if (ret)
			return ret;

		irq_ring_idx = uc->bchan->id + oes->bcdma_bchan_ring;
		irq_udma_idx = uc->bchan->id + oes->bcdma_bchan_data;

		ret = bcdma_tisci_m2m_channel_config(uc);
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

		irq_ring_idx = uc->tchan->id + oes->bcdma_tchan_ring;
		irq_udma_idx = uc->tchan->id + oes->bcdma_tchan_data;

		ret = bcdma_tisci_tx_channel_config(uc);
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

		irq_ring_idx = uc->rchan->id + oes->bcdma_rchan_ring;
		irq_udma_idx = uc->rchan->id + oes->bcdma_rchan_data;

		ret = bcdma_tisci_rx_channel_config(uc);
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
		udma_reset_chan(uc, false);
		if (udma_is_chan_running(uc)) {
			dev_err(ud->dev, "chan%d: won't stop!\n", uc->id);
			ret = -EBUSY;
			goto err_res_free;
		}
	}

	uc->dma_dev = dmaengine_get_dma_device(chan);
	if (uc->config.dir == DMA_MEM_TO_MEM  && !uc->config.tr_trigger_type) {
		uc->config.hdesc_size = cppi5_trdesc_calc_size(
					sizeof(struct cppi5_tr_type15_t), 2);

		uc->hdesc_pool = dma_pool_create(uc->name, ud->ddev.dev,
						 uc->config.hdesc_size,
						 ud->desc_align,
						 0);
		if (!uc->hdesc_pool) {
			dev_err(ud->ddev.dev,
				"Descriptor pool allocation failed\n");
			uc->use_dma_pool = false;
			ret = -ENOMEM;
			goto err_res_free;
		}

		uc->use_dma_pool = true;
	} else if (uc->config.dir != DMA_MEM_TO_MEM) {
		/* PSI-L pairing */
		ret = navss_psil_pair(ud, uc->config.src_thread,
				      uc->config.dst_thread);
		if (ret) {
			dev_err(ud->dev,
				"PSI-L pairing failed: 0x%04x -> 0x%04x\n",
				uc->config.src_thread, uc->config.dst_thread);
			goto err_res_free;
		}

		uc->psil_paired = true;
	}

	uc->irq_num_ring = msi_get_virq(ud->dev, irq_ring_idx);
	if (uc->irq_num_ring <= 0) {
		dev_err(ud->dev, "Failed to get ring irq (index: %u)\n",
			irq_ring_idx);
		ret = -EINVAL;
		goto err_psi_free;
	}

	ret = request_irq(uc->irq_num_ring, udma_ring_irq_handler,
			  IRQF_TRIGGER_HIGH, uc->name, uc);
	if (ret) {
		dev_err(ud->dev, "chan%d: ring irq request failed\n", uc->id);
		goto err_irq_free;
	}

	/* Event from BCDMA (TR events) only needed for slave channels */
	if (is_slave_direction(uc->config.dir)) {
		uc->irq_num_udma = msi_get_virq(ud->dev, irq_udma_idx);
		if (uc->irq_num_udma <= 0) {
			dev_err(ud->dev, "Failed to get bcdma irq (index: %u)\n",
				irq_udma_idx);
			free_irq(uc->irq_num_ring, uc);
			ret = -EINVAL;
			goto err_irq_free;
		}

		ret = request_irq(uc->irq_num_udma, udma_udma_irq_handler, 0,
				  uc->name, uc);
		if (ret) {
			dev_err(ud->dev, "chan%d: BCDMA irq request failed\n",
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
	if (uc->psil_paired)
		navss_psil_unpair(ud, uc->config.src_thread,
				  uc->config.dst_thread);
	uc->psil_paired = false;
err_res_free:
	bcdma_free_bchan_resources(uc);
	udma_free_tx_resources(uc);
	udma_free_rx_resources(uc);

	udma_reset_uchan(uc);

	if (uc->use_dma_pool) {
		dma_pool_destroy(uc->hdesc_pool);
		uc->use_dma_pool = false;
	}

	return ret;
}

static int bcdma_router_config(struct dma_chan *chan)
{
	struct k3_event_route_data *router_data = chan->route_data;
	struct udma_chan *uc = to_udma_chan(chan);
	u32 trigger_event;

	if (!uc->bchan)
		return -EINVAL;

	if (uc->config.tr_trigger_type != 1 && uc->config.tr_trigger_type != 2)
		return -EINVAL;

	trigger_event = uc->ud->soc_data->bcdma_trigger_event_offset;
	trigger_event += (uc->bchan->id * 2) + uc->config.tr_trigger_type - 1;

	return router_data->set_event(router_data->priv, trigger_event);
}

static int pktdma_alloc_chan_resources(struct dma_chan *chan)
{
	struct udma_chan *uc = to_udma_chan(chan);
	struct udma_dev *ud = to_udma_dev(chan->device);
	const struct udma_oes_offsets *oes = &ud->soc_data->oes;
	u32 irq_ring_idx;
	int ret;

	/*
	 * Make sure that the completion is in a known state:
	 * No teardown, the channel is idle
	 */
	reinit_completion(&uc->teardown_completed);
	complete_all(&uc->teardown_completed);
	uc->state = UDMA_CHAN_IS_IDLE;

	switch (uc->config.dir) {
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

		irq_ring_idx = uc->tchan->tflow_id + oes->pktdma_tchan_flow;

		ret = pktdma_tisci_tx_channel_config(uc);
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

		irq_ring_idx = uc->rflow->id + oes->pktdma_rchan_flow;

		ret = pktdma_tisci_rx_channel_config(uc);
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
		udma_reset_chan(uc, false);
		if (udma_is_chan_running(uc)) {
			dev_err(ud->dev, "chan%d: won't stop!\n", uc->id);
			ret = -EBUSY;
			goto err_res_free;
		}
	}

	uc->dma_dev = dmaengine_get_dma_device(chan);
	uc->hdesc_pool = dma_pool_create(uc->name, uc->dma_dev,
					 uc->config.hdesc_size, ud->desc_align,
					 0);
	if (!uc->hdesc_pool) {
		dev_err(ud->ddev.dev,
			"Descriptor pool allocation failed\n");
		uc->use_dma_pool = false;
		ret = -ENOMEM;
		goto err_res_free;
	}

	uc->use_dma_pool = true;

	/* PSI-L pairing */
	ret = navss_psil_pair(ud, uc->config.src_thread, uc->config.dst_thread);
	if (ret) {
		dev_err(ud->dev, "PSI-L pairing failed: 0x%04x -> 0x%04x\n",
			uc->config.src_thread, uc->config.dst_thread);
		goto err_res_free;
	}

	uc->psil_paired = true;

	uc->irq_num_ring = msi_get_virq(ud->dev, irq_ring_idx);
	if (uc->irq_num_ring <= 0) {
		dev_err(ud->dev, "Failed to get ring irq (index: %u)\n",
			irq_ring_idx);
		ret = -EINVAL;
		goto err_psi_free;
	}

	ret = request_irq(uc->irq_num_ring, udma_ring_irq_handler,
			  IRQF_TRIGGER_HIGH, uc->name, uc);
	if (ret) {
		dev_err(ud->dev, "chan%d: ring irq request failed\n", uc->id);
		goto err_irq_free;
	}

	uc->irq_num_udma = 0;

	udma_reset_rings(uc);

	INIT_DELAYED_WORK_ONSTACK(&uc->tx_drain.work,
				  udma_check_tx_completion);

	if (uc->tchan)
		dev_dbg(ud->dev,
			"chan%d: tchan%d, tflow%d, Remote thread: 0x%04x\n",
			uc->id, uc->tchan->id, uc->tchan->tflow_id,
			uc->config.remote_thread_id);
	else if (uc->rchan)
		dev_dbg(ud->dev,
			"chan%d: rchan%d, rflow%d, Remote thread: 0x%04x\n",
			uc->id, uc->rchan->id, uc->rflow->id,
			uc->config.remote_thread_id);
	return 0;

err_irq_free:
	uc->irq_num_ring = 0;
err_psi_free:
	navss_psil_unpair(ud, uc->config.src_thread, uc->config.dst_thread);
	uc->psil_paired = false;
err_res_free:
	udma_free_tx_resources(uc);
	udma_free_rx_resources(uc);

	udma_reset_uchan(uc);

	dma_pool_destroy(uc->hdesc_pool);
	uc->use_dma_pool = false;

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

/**
 * udma_get_tr_counters - calculate TR counters for a given length
 * @len: Length of the trasnfer
 * @align_to: Preferred alignment
 * @tr0_cnt0: First TR icnt0
 * @tr0_cnt1: First TR icnt1
 * @tr1_cnt0: Second (if used) TR icnt0
 *
 * For len < SZ_64K only one TR is enough, tr1_cnt0 is not updated
 * For len >= SZ_64K two TRs are used in a simple way:
 * First TR: SZ_64K-alignment blocks (tr0_cnt0, tr0_cnt1)
 * Second TR: the remaining length (tr1_cnt0)
 *
 * Returns the number of TRs the length needs (1 or 2)
 * -EINVAL if the length can not be supported
 */
static int udma_get_tr_counters(size_t len, unsigned long align_to,
				u16 *tr0_cnt0, u16 *tr0_cnt1, u16 *tr1_cnt0)
{
	if (len < SZ_64K) {
		*tr0_cnt0 = len;
		*tr0_cnt1 = 1;

		return 1;
	}

	if (align_to > 3)
		align_to = 3;

realign:
	*tr0_cnt0 = SZ_64K - BIT(align_to);
	if (len / *tr0_cnt0 >= SZ_64K) {
		if (align_to) {
			align_to--;
			goto realign;
		}
		return -EINVAL;
	}

	*tr0_cnt1 = len / *tr0_cnt0;
	*tr1_cnt0 = len % *tr0_cnt0;

	return 2;
}

static struct udma_desc *
udma_prep_slave_sg_tr(struct udma_chan *uc, struct scatterlist *sgl,
		      unsigned int sglen, enum dma_transfer_direction dir,
		      unsigned long tx_flags, void *context)
{
	struct scatterlist *sgent;
	struct udma_desc *d;
	struct cppi5_tr_type1_t *tr_req = NULL;
	u16 tr0_cnt0, tr0_cnt1, tr1_cnt0;
	unsigned int i;
	size_t tr_size;
	int num_tr = 0;
	int tr_idx = 0;
	u64 asel;

	/* estimate the number of TRs we will need */
	for_each_sg(sgl, sgent, sglen, i) {
		if (sg_dma_len(sgent) < SZ_64K)
			num_tr++;
		else
			num_tr += 2;
	}

	/* Now allocate and setup the descriptor. */
	tr_size = sizeof(struct cppi5_tr_type1_t);
	d = udma_alloc_tr_desc(uc, tr_size, num_tr, dir);
	if (!d)
		return NULL;

	d->sglen = sglen;

	if (uc->ud->match_data->type == DMA_TYPE_UDMA)
		asel = 0;
	else
		asel = (u64)uc->config.asel << K3_ADDRESS_ASEL_SHIFT;

	tr_req = d->hwdesc[0].tr_req_base;
	for_each_sg(sgl, sgent, sglen, i) {
		dma_addr_t sg_addr = sg_dma_address(sgent);

		num_tr = udma_get_tr_counters(sg_dma_len(sgent), __ffs(sg_addr),
					      &tr0_cnt0, &tr0_cnt1, &tr1_cnt0);
		if (num_tr < 0) {
			dev_err(uc->ud->dev, "size %u is not supported\n",
				sg_dma_len(sgent));
			udma_free_hwdesc(uc, d);
			kfree(d);
			return NULL;
		}

		cppi5_tr_init(&tr_req[tr_idx].flags, CPPI5_TR_TYPE1, false,
			      false, CPPI5_TR_EVENT_SIZE_COMPLETION, 0);
		cppi5_tr_csf_set(&tr_req[tr_idx].flags, CPPI5_TR_CSF_SUPR_EVT);

		sg_addr |= asel;
		tr_req[tr_idx].addr = sg_addr;
		tr_req[tr_idx].icnt0 = tr0_cnt0;
		tr_req[tr_idx].icnt1 = tr0_cnt1;
		tr_req[tr_idx].dim1 = tr0_cnt0;
		tr_idx++;

		if (num_tr == 2) {
			cppi5_tr_init(&tr_req[tr_idx].flags, CPPI5_TR_TYPE1,
				      false, false,
				      CPPI5_TR_EVENT_SIZE_COMPLETION, 0);
			cppi5_tr_csf_set(&tr_req[tr_idx].flags,
					 CPPI5_TR_CSF_SUPR_EVT);

			tr_req[tr_idx].addr = sg_addr + tr0_cnt1 * tr0_cnt0;
			tr_req[tr_idx].icnt0 = tr1_cnt0;
			tr_req[tr_idx].icnt1 = 1;
			tr_req[tr_idx].dim1 = tr1_cnt0;
			tr_idx++;
		}

		d->residue += sg_dma_len(sgent);
	}

	cppi5_tr_csf_set(&tr_req[tr_idx - 1].flags,
			 CPPI5_TR_CSF_SUPR_EVT | CPPI5_TR_CSF_EOP);

	return d;
}

static struct udma_desc *
udma_prep_slave_sg_triggered_tr(struct udma_chan *uc, struct scatterlist *sgl,
				unsigned int sglen,
				enum dma_transfer_direction dir,
				unsigned long tx_flags, void *context)
{
	struct scatterlist *sgent;
	struct cppi5_tr_type15_t *tr_req = NULL;
	enum dma_slave_buswidth dev_width;
	u16 tr_cnt0, tr_cnt1;
	dma_addr_t dev_addr;
	struct udma_desc *d;
	unsigned int i;
	size_t tr_size, sg_len;
	int num_tr = 0;
	int tr_idx = 0;
	u32 burst, trigger_size, port_window;
	u64 asel;

	if (dir == DMA_DEV_TO_MEM) {
		dev_addr = uc->cfg.src_addr;
		dev_width = uc->cfg.src_addr_width;
		burst = uc->cfg.src_maxburst;
		port_window = uc->cfg.src_port_window_size;
	} else if (dir == DMA_MEM_TO_DEV) {
		dev_addr = uc->cfg.dst_addr;
		dev_width = uc->cfg.dst_addr_width;
		burst = uc->cfg.dst_maxburst;
		port_window = uc->cfg.dst_port_window_size;
	} else {
		dev_err(uc->ud->dev, "%s: bad direction?\n", __func__);
		return NULL;
	}

	if (!burst)
		burst = 1;

	if (port_window) {
		if (port_window != burst) {
			dev_err(uc->ud->dev,
				"The burst must be equal to port_window\n");
			return NULL;
		}

		tr_cnt0 = dev_width * port_window;
		tr_cnt1 = 1;
	} else {
		tr_cnt0 = dev_width;
		tr_cnt1 = burst;
	}
	trigger_size = tr_cnt0 * tr_cnt1;

	/* estimate the number of TRs we will need */
	for_each_sg(sgl, sgent, sglen, i) {
		sg_len = sg_dma_len(sgent);

		if (sg_len % trigger_size) {
			dev_err(uc->ud->dev,
				"Not aligned SG entry (%zu for %u)\n", sg_len,
				trigger_size);
			return NULL;
		}

		if (sg_len / trigger_size < SZ_64K)
			num_tr++;
		else
			num_tr += 2;
	}

	/* Now allocate and setup the descriptor. */
	tr_size = sizeof(struct cppi5_tr_type15_t);
	d = udma_alloc_tr_desc(uc, tr_size, num_tr, dir);
	if (!d)
		return NULL;

	d->sglen = sglen;

	if (uc->ud->match_data->type == DMA_TYPE_UDMA) {
		asel = 0;
	} else {
		asel = (u64)uc->config.asel << K3_ADDRESS_ASEL_SHIFT;
		dev_addr |= asel;
	}

	tr_req = d->hwdesc[0].tr_req_base;
	for_each_sg(sgl, sgent, sglen, i) {
		u16 tr0_cnt2, tr0_cnt3, tr1_cnt2;
		dma_addr_t sg_addr = sg_dma_address(sgent);

		sg_len = sg_dma_len(sgent);
		num_tr = udma_get_tr_counters(sg_len / trigger_size, 0,
					      &tr0_cnt2, &tr0_cnt3, &tr1_cnt2);
		if (num_tr < 0) {
			dev_err(uc->ud->dev, "size %zu is not supported\n",
				sg_len);
			udma_free_hwdesc(uc, d);
			kfree(d);
			return NULL;
		}

		cppi5_tr_init(&tr_req[tr_idx].flags, CPPI5_TR_TYPE15, false,
			      true, CPPI5_TR_EVENT_SIZE_COMPLETION, 0);
		cppi5_tr_csf_set(&tr_req[tr_idx].flags, CPPI5_TR_CSF_SUPR_EVT);
		cppi5_tr_set_trigger(&tr_req[tr_idx].flags,
				     uc->config.tr_trigger_type,
				     CPPI5_TR_TRIGGER_TYPE_ICNT2_DEC, 0, 0);

		sg_addr |= asel;
		if (dir == DMA_DEV_TO_MEM) {
			tr_req[tr_idx].addr = dev_addr;
			tr_req[tr_idx].icnt0 = tr_cnt0;
			tr_req[tr_idx].icnt1 = tr_cnt1;
			tr_req[tr_idx].icnt2 = tr0_cnt2;
			tr_req[tr_idx].icnt3 = tr0_cnt3;
			tr_req[tr_idx].dim1 = (-1) * tr_cnt0;

			tr_req[tr_idx].daddr = sg_addr;
			tr_req[tr_idx].dicnt0 = tr_cnt0;
			tr_req[tr_idx].dicnt1 = tr_cnt1;
			tr_req[tr_idx].dicnt2 = tr0_cnt2;
			tr_req[tr_idx].dicnt3 = tr0_cnt3;
			tr_req[tr_idx].ddim1 = tr_cnt0;
			tr_req[tr_idx].ddim2 = trigger_size;
			tr_req[tr_idx].ddim3 = trigger_size * tr0_cnt2;
		} else {
			tr_req[tr_idx].addr = sg_addr;
			tr_req[tr_idx].icnt0 = tr_cnt0;
			tr_req[tr_idx].icnt1 = tr_cnt1;
			tr_req[tr_idx].icnt2 = tr0_cnt2;
			tr_req[tr_idx].icnt3 = tr0_cnt3;
			tr_req[tr_idx].dim1 = tr_cnt0;
			tr_req[tr_idx].dim2 = trigger_size;
			tr_req[tr_idx].dim3 = trigger_size * tr0_cnt2;

			tr_req[tr_idx].daddr = dev_addr;
			tr_req[tr_idx].dicnt0 = tr_cnt0;
			tr_req[tr_idx].dicnt1 = tr_cnt1;
			tr_req[tr_idx].dicnt2 = tr0_cnt2;
			tr_req[tr_idx].dicnt3 = tr0_cnt3;
			tr_req[tr_idx].ddim1 = (-1) * tr_cnt0;
		}

		tr_idx++;

		if (num_tr == 2) {
			cppi5_tr_init(&tr_req[tr_idx].flags, CPPI5_TR_TYPE15,
				      false, true,
				      CPPI5_TR_EVENT_SIZE_COMPLETION, 0);
			cppi5_tr_csf_set(&tr_req[tr_idx].flags,
					 CPPI5_TR_CSF_SUPR_EVT);
			cppi5_tr_set_trigger(&tr_req[tr_idx].flags,
					     uc->config.tr_trigger_type,
					     CPPI5_TR_TRIGGER_TYPE_ICNT2_DEC,
					     0, 0);

			sg_addr += trigger_size * tr0_cnt2 * tr0_cnt3;
			if (dir == DMA_DEV_TO_MEM) {
				tr_req[tr_idx].addr = dev_addr;
				tr_req[tr_idx].icnt0 = tr_cnt0;
				tr_req[tr_idx].icnt1 = tr_cnt1;
				tr_req[tr_idx].icnt2 = tr1_cnt2;
				tr_req[tr_idx].icnt3 = 1;
				tr_req[tr_idx].dim1 = (-1) * tr_cnt0;

				tr_req[tr_idx].daddr = sg_addr;
				tr_req[tr_idx].dicnt0 = tr_cnt0;
				tr_req[tr_idx].dicnt1 = tr_cnt1;
				tr_req[tr_idx].dicnt2 = tr1_cnt2;
				tr_req[tr_idx].dicnt3 = 1;
				tr_req[tr_idx].ddim1 = tr_cnt0;
				tr_req[tr_idx].ddim2 = trigger_size;
			} else {
				tr_req[tr_idx].addr = sg_addr;
				tr_req[tr_idx].icnt0 = tr_cnt0;
				tr_req[tr_idx].icnt1 = tr_cnt1;
				tr_req[tr_idx].icnt2 = tr1_cnt2;
				tr_req[tr_idx].icnt3 = 1;
				tr_req[tr_idx].dim1 = tr_cnt0;
				tr_req[tr_idx].dim2 = trigger_size;

				tr_req[tr_idx].daddr = dev_addr;
				tr_req[tr_idx].dicnt0 = tr_cnt0;
				tr_req[tr_idx].dicnt1 = tr_cnt1;
				tr_req[tr_idx].dicnt2 = tr1_cnt2;
				tr_req[tr_idx].dicnt3 = 1;
				tr_req[tr_idx].ddim1 = (-1) * tr_cnt0;
			}
			tr_idx++;
		}

		d->residue += sg_len;
	}

	cppi5_tr_csf_set(&tr_req[tr_idx - 1].flags,
			 CPPI5_TR_CSF_SUPR_EVT | CPPI5_TR_CSF_EOP);

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
	u64 asel;

	d = kzalloc(struct_size(d, hwdesc, sglen), GFP_NOWAIT);
	if (!d)
		return NULL;

	d->sglen = sglen;
	d->hwdesc_count = sglen;

	if (dir == DMA_DEV_TO_MEM)
		ring_id = k3_ringacc_get_ring_id(uc->rflow->r_ring);
	else
		ring_id = k3_ringacc_get_ring_id(uc->tchan->tc_ring);

	if (uc->ud->match_data->type == DMA_TYPE_UDMA)
		asel = 0;
	else
		asel = (u64)uc->config.asel << K3_ADDRESS_ASEL_SHIFT;

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
		sg_addr |= asel;
		cppi5_hdesc_attach_buf(desc, sg_addr, sg_len, sg_addr, sg_len);

		/* Attach link as host buffer descriptor */
		if (h_desc)
			cppi5_hdesc_link_hbdesc(h_desc,
						hwdesc->cppi5_desc_paddr | asel);

		if (uc->ud->match_data->type == DMA_TYPE_PKTDMA ||
		    dir == DMA_MEM_TO_DEV)
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

	if (dir != uc->config.dir &&
	    (uc->config.dir == DMA_MEM_TO_MEM && !uc->config.tr_trigger_type)) {
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
	else if (is_slave_direction(uc->config.dir))
		d = udma_prep_slave_sg_tr(uc, sgl, sglen, dir, tx_flags,
					  context);
	else
		d = udma_prep_slave_sg_triggered_tr(uc, sgl, sglen, dir,
						    tx_flags, context);

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
	struct udma_desc *d;
	size_t tr_size, period_addr;
	struct cppi5_tr_type1_t *tr_req;
	unsigned int periods = buf_len / period_len;
	u16 tr0_cnt0, tr0_cnt1, tr1_cnt0;
	unsigned int i;
	int num_tr;

	num_tr = udma_get_tr_counters(period_len, __ffs(buf_addr), &tr0_cnt0,
				      &tr0_cnt1, &tr1_cnt0);
	if (num_tr < 0) {
		dev_err(uc->ud->dev, "size %zu is not supported\n",
			period_len);
		return NULL;
	}

	/* Now allocate and setup the descriptor. */
	tr_size = sizeof(struct cppi5_tr_type1_t);
	d = udma_alloc_tr_desc(uc, tr_size, periods * num_tr, dir);
	if (!d)
		return NULL;

	tr_req = d->hwdesc[0].tr_req_base;
	if (uc->ud->match_data->type == DMA_TYPE_UDMA)
		period_addr = buf_addr;
	else
		period_addr = buf_addr |
			((u64)uc->config.asel << K3_ADDRESS_ASEL_SHIFT);

	for (i = 0; i < periods; i++) {
		int tr_idx = i * num_tr;

		cppi5_tr_init(&tr_req[tr_idx].flags, CPPI5_TR_TYPE1, false,
			      false, CPPI5_TR_EVENT_SIZE_COMPLETION, 0);

		tr_req[tr_idx].addr = period_addr;
		tr_req[tr_idx].icnt0 = tr0_cnt0;
		tr_req[tr_idx].icnt1 = tr0_cnt1;
		tr_req[tr_idx].dim1 = tr0_cnt0;

		if (num_tr == 2) {
			cppi5_tr_csf_set(&tr_req[tr_idx].flags,
					 CPPI5_TR_CSF_SUPR_EVT);
			tr_idx++;

			cppi5_tr_init(&tr_req[tr_idx].flags, CPPI5_TR_TYPE1,
				      false, false,
				      CPPI5_TR_EVENT_SIZE_COMPLETION, 0);

			tr_req[tr_idx].addr = period_addr + tr0_cnt1 * tr0_cnt0;
			tr_req[tr_idx].icnt0 = tr1_cnt0;
			tr_req[tr_idx].icnt1 = 1;
			tr_req[tr_idx].dim1 = tr1_cnt0;
		}

		if (!(flags & DMA_PREP_INTERRUPT))
			cppi5_tr_csf_set(&tr_req[tr_idx].flags,
					 CPPI5_TR_CSF_SUPR_EVT);

		period_addr += period_len;
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

	d = kzalloc(struct_size(d, hwdesc, periods), GFP_NOWAIT);
	if (!d)
		return NULL;

	d->hwdesc_count = periods;

	/* TODO: re-check this... */
	if (dir == DMA_DEV_TO_MEM)
		ring_id = k3_ringacc_get_ring_id(uc->rflow->r_ring);
	else
		ring_id = k3_ringacc_get_ring_id(uc->tchan->tc_ring);

	if (uc->ud->match_data->type != DMA_TYPE_UDMA)
		buf_addr |= (u64)uc->config.asel << K3_ADDRESS_ASEL_SHIFT;

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

	num_tr = udma_get_tr_counters(len, __ffs(src | dest), &tr0_cnt0,
				      &tr0_cnt1, &tr1_cnt0);
	if (num_tr < 0) {
		dev_err(uc->ud->dev, "size %zu is not supported\n",
			len);
		return NULL;
	}

	d = udma_alloc_tr_desc(uc, tr_size, num_tr, DMA_MEM_TO_MEM);
	if (!d)
		return NULL;

	d->dir = DMA_MEM_TO_MEM;
	d->desc_idx = 0;
	d->tr_idx = 0;
	d->residue = len;

	if (uc->ud->match_data->type != DMA_TYPE_UDMA) {
		src |= (u64)uc->ud->asel << K3_ADDRESS_ASEL_SHIFT;
		dest |= (u64)uc->ud->asel << K3_ADDRESS_ASEL_SHIFT;
	}

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

	cppi5_tr_csf_set(&tr_req[num_tr - 1].flags,
			 CPPI5_TR_CSF_SUPR_EVT | CPPI5_TR_CSF_EOP);

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

	if (!udma_is_chan_running(uc))
		ret = DMA_COMPLETE;

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
			bcnt = udma_tchanrt_read(uc, UDMA_CHAN_RT_SBCNT_REG);

			if (uc->config.ep_type != PSIL_EP_NATIVE) {
				peer_bcnt = udma_tchanrt_read(uc,
						UDMA_CHAN_RT_PEER_BCNT_REG);

				if (bcnt > peer_bcnt)
					delay = bcnt - peer_bcnt;
			}
		} else if (uc->desc->dir == DMA_DEV_TO_MEM) {
			bcnt = udma_rchanrt_read(uc, UDMA_CHAN_RT_BCNT_REG);

			if (uc->config.ep_type != PSIL_EP_NATIVE) {
				peer_bcnt = udma_rchanrt_read(uc,
						UDMA_CHAN_RT_PEER_BCNT_REG);

				if (peer_bcnt > bcnt)
					delay = peer_bcnt - bcnt;
			}
		} else {
			bcnt = udma_tchanrt_read(uc, UDMA_CHAN_RT_BCNT_REG);
		}

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

	/* pause the channel */
	switch (uc->config.dir) {
	case DMA_DEV_TO_MEM:
		udma_rchanrt_update_bits(uc, UDMA_CHAN_RT_PEER_RT_EN_REG,
					 UDMA_PEER_RT_EN_PAUSE,
					 UDMA_PEER_RT_EN_PAUSE);
		break;
	case DMA_MEM_TO_DEV:
		udma_tchanrt_update_bits(uc, UDMA_CHAN_RT_PEER_RT_EN_REG,
					 UDMA_PEER_RT_EN_PAUSE,
					 UDMA_PEER_RT_EN_PAUSE);
		break;
	case DMA_MEM_TO_MEM:
		udma_tchanrt_update_bits(uc, UDMA_CHAN_RT_CTL_REG,
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

	/* resume the channel */
	switch (uc->config.dir) {
	case DMA_DEV_TO_MEM:
		udma_rchanrt_update_bits(uc, UDMA_CHAN_RT_PEER_RT_EN_REG,
					 UDMA_PEER_RT_EN_PAUSE, 0);

		break;
	case DMA_MEM_TO_DEV:
		udma_tchanrt_update_bits(uc, UDMA_CHAN_RT_PEER_RT_EN_REG,
					 UDMA_PEER_RT_EN_PAUSE, 0);
		break;
	case DMA_MEM_TO_MEM:
		udma_tchanrt_update_bits(uc, UDMA_CHAN_RT_CTL_REG,
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
static void udma_vchan_complete(struct tasklet_struct *t)
{
	struct virt_dma_chan *vc = from_tasklet(vc, t, task);
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

	bcdma_free_bchan_resources(uc);
	udma_free_tx_resources(uc);
	udma_free_rx_resources(uc);
	udma_reset_uchan(uc);

	if (uc->use_dma_pool) {
		dma_pool_destroy(uc->hdesc_pool);
		uc->use_dma_pool = false;
	}
}

static struct platform_driver udma_driver;
static struct platform_driver bcdma_driver;
static struct platform_driver pktdma_driver;

struct udma_filter_param {
	int remote_thread_id;
	u32 atype;
	u32 asel;
	u32 tr_trigger_type;
};

static bool udma_dma_filter_fn(struct dma_chan *chan, void *param)
{
	struct udma_chan_config *ucc;
	struct psil_endpoint_config *ep_config;
	struct udma_filter_param *filter_param;
	struct udma_chan *uc;
	struct udma_dev *ud;

	if (chan->device->dev->driver != &udma_driver.driver &&
	    chan->device->dev->driver != &bcdma_driver.driver &&
	    chan->device->dev->driver != &pktdma_driver.driver)
		return false;

	uc = to_udma_chan(chan);
	ucc = &uc->config;
	ud = uc->ud;
	filter_param = param;

	if (filter_param->atype > 2) {
		dev_err(ud->dev, "Invalid channel atype: %u\n",
			filter_param->atype);
		return false;
	}

	if (filter_param->asel > 15) {
		dev_err(ud->dev, "Invalid channel asel: %u\n",
			filter_param->asel);
		return false;
	}

	ucc->remote_thread_id = filter_param->remote_thread_id;
	ucc->atype = filter_param->atype;
	ucc->asel = filter_param->asel;
	ucc->tr_trigger_type = filter_param->tr_trigger_type;

	if (ucc->tr_trigger_type) {
		ucc->dir = DMA_MEM_TO_MEM;
		goto triggered_bchan;
	} else if (ucc->remote_thread_id & K3_PSIL_DST_THREAD_ID_OFFSET) {
		ucc->dir = DMA_MEM_TO_DEV;
	} else {
		ucc->dir = DMA_DEV_TO_MEM;
	}

	ep_config = psil_get_ep_config(ucc->remote_thread_id);
	if (IS_ERR(ep_config)) {
		dev_err(ud->dev, "No configuration for psi-l thread 0x%04x\n",
			ucc->remote_thread_id);
		ucc->dir = DMA_MEM_TO_MEM;
		ucc->remote_thread_id = -1;
		ucc->atype = 0;
		ucc->asel = 0;
		return false;
	}

	if (ud->match_data->type == DMA_TYPE_BCDMA &&
	    ep_config->pkt_mode) {
		dev_err(ud->dev,
			"Only TR mode is supported (psi-l thread 0x%04x)\n",
			ucc->remote_thread_id);
		ucc->dir = DMA_MEM_TO_MEM;
		ucc->remote_thread_id = -1;
		ucc->atype = 0;
		ucc->asel = 0;
		return false;
	}

	ucc->pkt_mode = ep_config->pkt_mode;
	ucc->channel_tpl = ep_config->channel_tpl;
	ucc->notdpkt = ep_config->notdpkt;
	ucc->ep_type = ep_config->ep_type;

	if (ud->match_data->type == DMA_TYPE_PKTDMA &&
	    ep_config->mapped_channel_id >= 0) {
		ucc->mapped_channel_id = ep_config->mapped_channel_id;
		ucc->default_flow_id = ep_config->default_flow_id;
	} else {
		ucc->mapped_channel_id = -1;
		ucc->default_flow_id = -1;
	}

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

triggered_bchan:
	dev_dbg(ud->dev, "chan%d: triggered channel (type: %u)\n", uc->id,
		ucc->tr_trigger_type);

	return true;

}

static struct dma_chan *udma_of_xlate(struct of_phandle_args *dma_spec,
				      struct of_dma *ofdma)
{
	struct udma_dev *ud = ofdma->of_dma_data;
	dma_cap_mask_t mask = ud->ddev.cap_mask;
	struct udma_filter_param filter_param;
	struct dma_chan *chan;

	if (ud->match_data->type == DMA_TYPE_BCDMA) {
		if (dma_spec->args_count != 3)
			return NULL;

		filter_param.tr_trigger_type = dma_spec->args[0];
		filter_param.remote_thread_id = dma_spec->args[1];
		filter_param.asel = dma_spec->args[2];
		filter_param.atype = 0;
	} else {
		if (dma_spec->args_count != 1 && dma_spec->args_count != 2)
			return NULL;

		filter_param.remote_thread_id = dma_spec->args[0];
		filter_param.tr_trigger_type = 0;
		if (dma_spec->args_count == 2) {
			if (ud->match_data->type == DMA_TYPE_UDMA) {
				filter_param.atype = dma_spec->args[1];
				filter_param.asel = 0;
			} else {
				filter_param.atype = 0;
				filter_param.asel = dma_spec->args[1];
			}
		} else {
			filter_param.atype = 0;
			filter_param.asel = 0;
		}
	}

	chan = __dma_request_channel(&mask, udma_dma_filter_fn, &filter_param,
				     ofdma->of_node);
	if (!chan) {
		dev_err(ud->dev, "get channel fail in %s.\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	return chan;
}

static struct udma_match_data am654_main_data = {
	.type = DMA_TYPE_UDMA,
	.psil_base = 0x1000,
	.enable_memcpy_support = true,
	.statictr_z_mask = GENMASK(11, 0),
	.burst_size = {
		TI_SCI_RM_UDMAP_CHAN_BURST_SIZE_64_BYTES, /* Normal Channels */
		TI_SCI_RM_UDMAP_CHAN_BURST_SIZE_64_BYTES, /* H Channels */
		0, /* No UH Channels */
	},
};

static struct udma_match_data am654_mcu_data = {
	.type = DMA_TYPE_UDMA,
	.psil_base = 0x6000,
	.enable_memcpy_support = false,
	.statictr_z_mask = GENMASK(11, 0),
	.burst_size = {
		TI_SCI_RM_UDMAP_CHAN_BURST_SIZE_64_BYTES, /* Normal Channels */
		TI_SCI_RM_UDMAP_CHAN_BURST_SIZE_64_BYTES, /* H Channels */
		0, /* No UH Channels */
	},
};

static struct udma_match_data j721e_main_data = {
	.type = DMA_TYPE_UDMA,
	.psil_base = 0x1000,
	.enable_memcpy_support = true,
	.flags = UDMA_FLAGS_J7_CLASS,
	.statictr_z_mask = GENMASK(23, 0),
	.burst_size = {
		TI_SCI_RM_UDMAP_CHAN_BURST_SIZE_64_BYTES, /* Normal Channels */
		TI_SCI_RM_UDMAP_CHAN_BURST_SIZE_256_BYTES, /* H Channels */
		TI_SCI_RM_UDMAP_CHAN_BURST_SIZE_256_BYTES, /* UH Channels */
	},
};

static struct udma_match_data j721e_mcu_data = {
	.type = DMA_TYPE_UDMA,
	.psil_base = 0x6000,
	.enable_memcpy_support = false, /* MEM_TO_MEM is slow via MCU UDMA */
	.flags = UDMA_FLAGS_J7_CLASS,
	.statictr_z_mask = GENMASK(23, 0),
	.burst_size = {
		TI_SCI_RM_UDMAP_CHAN_BURST_SIZE_64_BYTES, /* Normal Channels */
		TI_SCI_RM_UDMAP_CHAN_BURST_SIZE_128_BYTES, /* H Channels */
		0, /* No UH Channels */
	},
};

static struct udma_match_data am64_bcdma_data = {
	.type = DMA_TYPE_BCDMA,
	.psil_base = 0x2000, /* for tchan and rchan, not applicable to bchan */
	.enable_memcpy_support = true, /* Supported via bchan */
	.flags = UDMA_FLAGS_J7_CLASS,
	.statictr_z_mask = GENMASK(23, 0),
	.burst_size = {
		TI_SCI_RM_UDMAP_CHAN_BURST_SIZE_64_BYTES, /* Normal Channels */
		0, /* No H Channels */
		0, /* No UH Channels */
	},
};

static struct udma_match_data am64_pktdma_data = {
	.type = DMA_TYPE_PKTDMA,
	.psil_base = 0x1000,
	.enable_memcpy_support = false, /* PKTDMA does not support MEM_TO_MEM */
	.flags = UDMA_FLAGS_J7_CLASS,
	.statictr_z_mask = GENMASK(23, 0),
	.burst_size = {
		TI_SCI_RM_UDMAP_CHAN_BURST_SIZE_64_BYTES, /* Normal Channels */
		0, /* No H Channels */
		0, /* No UH Channels */
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

static const struct of_device_id bcdma_of_match[] = {
	{
		.compatible = "ti,am64-dmss-bcdma",
		.data = &am64_bcdma_data,
	},
	{ /* Sentinel */ },
};

static const struct of_device_id pktdma_of_match[] = {
	{
		.compatible = "ti,am64-dmss-pktdma",
		.data = &am64_pktdma_data,
	},
	{ /* Sentinel */ },
};

static struct udma_soc_data am654_soc_data = {
	.oes = {
		.udma_rchan = 0x200,
	},
};

static struct udma_soc_data j721e_soc_data = {
	.oes = {
		.udma_rchan = 0x400,
	},
};

static struct udma_soc_data j7200_soc_data = {
	.oes = {
		.udma_rchan = 0x80,
	},
};

static struct udma_soc_data am64_soc_data = {
	.oes = {
		.bcdma_bchan_data = 0x2200,
		.bcdma_bchan_ring = 0x2400,
		.bcdma_tchan_data = 0x2800,
		.bcdma_tchan_ring = 0x2a00,
		.bcdma_rchan_data = 0x2e00,
		.bcdma_rchan_ring = 0x3000,
		.pktdma_tchan_flow = 0x1200,
		.pktdma_rchan_flow = 0x1600,
	},
	.bcdma_trigger_event_offset = 0xc400,
};

static const struct soc_device_attribute k3_soc_devices[] = {
	{ .family = "AM65X", .data = &am654_soc_data },
	{ .family = "J721E", .data = &j721e_soc_data },
	{ .family = "J7200", .data = &j7200_soc_data },
	{ .family = "AM64X", .data = &am64_soc_data },
	{ .family = "J721S2", .data = &j721e_soc_data},
	{ .family = "AM62X", .data = &am64_soc_data },
	{ /* sentinel */ }
};

static int udma_get_mmrs(struct platform_device *pdev, struct udma_dev *ud)
{
	u32 cap2, cap3, cap4;
	int i;

	ud->mmrs[MMR_GCFG] = devm_platform_ioremap_resource_byname(pdev, mmr_names[MMR_GCFG]);
	if (IS_ERR(ud->mmrs[MMR_GCFG]))
		return PTR_ERR(ud->mmrs[MMR_GCFG]);

	cap2 = udma_read(ud->mmrs[MMR_GCFG], 0x28);
	cap3 = udma_read(ud->mmrs[MMR_GCFG], 0x2c);

	switch (ud->match_data->type) {
	case DMA_TYPE_UDMA:
		ud->rflow_cnt = UDMA_CAP3_RFLOW_CNT(cap3);
		ud->tchan_cnt = UDMA_CAP2_TCHAN_CNT(cap2);
		ud->echan_cnt = UDMA_CAP2_ECHAN_CNT(cap2);
		ud->rchan_cnt = UDMA_CAP2_RCHAN_CNT(cap2);
		break;
	case DMA_TYPE_BCDMA:
		ud->bchan_cnt = BCDMA_CAP2_BCHAN_CNT(cap2);
		ud->tchan_cnt = BCDMA_CAP2_TCHAN_CNT(cap2);
		ud->rchan_cnt = BCDMA_CAP2_RCHAN_CNT(cap2);
		ud->rflow_cnt = ud->rchan_cnt;
		break;
	case DMA_TYPE_PKTDMA:
		cap4 = udma_read(ud->mmrs[MMR_GCFG], 0x30);
		ud->tchan_cnt = UDMA_CAP2_TCHAN_CNT(cap2);
		ud->rchan_cnt = UDMA_CAP2_RCHAN_CNT(cap2);
		ud->rflow_cnt = UDMA_CAP3_RFLOW_CNT(cap3);
		ud->tflow_cnt = PKTDMA_CAP4_TFLOW_CNT(cap4);
		break;
	default:
		return -EINVAL;
	}

	for (i = 1; i < MMR_LAST; i++) {
		if (i == MMR_BCHANRT && ud->bchan_cnt == 0)
			continue;
		if (i == MMR_TCHANRT && ud->tchan_cnt == 0)
			continue;
		if (i == MMR_RCHANRT && ud->rchan_cnt == 0)
			continue;

		ud->mmrs[i] = devm_platform_ioremap_resource_byname(pdev, mmr_names[i]);
		if (IS_ERR(ud->mmrs[i]))
			return PTR_ERR(ud->mmrs[i]);
	}

	return 0;
}

static void udma_mark_resource_ranges(struct udma_dev *ud, unsigned long *map,
				      struct ti_sci_resource_desc *rm_desc,
				      char *name)
{
	bitmap_clear(map, rm_desc->start, rm_desc->num);
	bitmap_clear(map, rm_desc->start_sec, rm_desc->num_sec);
	dev_dbg(ud->dev, "ti_sci resource range for %s: %d:%d | %d:%d\n", name,
		rm_desc->start, rm_desc->num, rm_desc->start_sec,
		rm_desc->num_sec);
}

static const char * const range_names[] = {
	[RM_RANGE_BCHAN] = "ti,sci-rm-range-bchan",
	[RM_RANGE_TCHAN] = "ti,sci-rm-range-tchan",
	[RM_RANGE_RCHAN] = "ti,sci-rm-range-rchan",
	[RM_RANGE_RFLOW] = "ti,sci-rm-range-rflow",
	[RM_RANGE_TFLOW] = "ti,sci-rm-range-tflow",
};

static int udma_setup_resources(struct udma_dev *ud)
{
	int ret, i, j;
	struct device *dev = ud->dev;
	struct ti_sci_resource *rm_res, irq_res;
	struct udma_tisci_rm *tisci_rm = &ud->tisci_rm;
	u32 cap3;

	/* Set up the throughput level start indexes */
	cap3 = udma_read(ud->mmrs[MMR_GCFG], 0x2c);
	if (of_device_is_compatible(dev->of_node,
				    "ti,am654-navss-main-udmap")) {
		ud->tchan_tpl.levels = 2;
		ud->tchan_tpl.start_idx[0] = 8;
	} else if (of_device_is_compatible(dev->of_node,
					   "ti,am654-navss-mcu-udmap")) {
		ud->tchan_tpl.levels = 2;
		ud->tchan_tpl.start_idx[0] = 2;
	} else if (UDMA_CAP3_UCHAN_CNT(cap3)) {
		ud->tchan_tpl.levels = 3;
		ud->tchan_tpl.start_idx[1] = UDMA_CAP3_UCHAN_CNT(cap3);
		ud->tchan_tpl.start_idx[0] = UDMA_CAP3_HCHAN_CNT(cap3);
	} else if (UDMA_CAP3_HCHAN_CNT(cap3)) {
		ud->tchan_tpl.levels = 2;
		ud->tchan_tpl.start_idx[0] = UDMA_CAP3_HCHAN_CNT(cap3);
	} else {
		ud->tchan_tpl.levels = 1;
	}

	ud->rchan_tpl.levels = ud->tchan_tpl.levels;
	ud->rchan_tpl.start_idx[0] = ud->tchan_tpl.start_idx[0];
	ud->rchan_tpl.start_idx[1] = ud->tchan_tpl.start_idx[1];

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
	for (i = 0; i < RM_RANGE_LAST; i++) {
		if (i == RM_RANGE_BCHAN || i == RM_RANGE_TFLOW)
			continue;

		tisci_rm->rm_ranges[i] =
			devm_ti_sci_get_of_resource(tisci_rm->tisci, dev,
						    tisci_rm->tisci_dev_id,
						    (char *)range_names[i]);
	}

	/* tchan ranges */
	rm_res = tisci_rm->rm_ranges[RM_RANGE_TCHAN];
	if (IS_ERR(rm_res)) {
		bitmap_zero(ud->tchan_map, ud->tchan_cnt);
		irq_res.sets = 1;
	} else {
		bitmap_fill(ud->tchan_map, ud->tchan_cnt);
		for (i = 0; i < rm_res->sets; i++)
			udma_mark_resource_ranges(ud, ud->tchan_map,
						  &rm_res->desc[i], "tchan");
		irq_res.sets = rm_res->sets;
	}

	/* rchan and matching default flow ranges */
	rm_res = tisci_rm->rm_ranges[RM_RANGE_RCHAN];
	if (IS_ERR(rm_res)) {
		bitmap_zero(ud->rchan_map, ud->rchan_cnt);
		irq_res.sets++;
	} else {
		bitmap_fill(ud->rchan_map, ud->rchan_cnt);
		for (i = 0; i < rm_res->sets; i++)
			udma_mark_resource_ranges(ud, ud->rchan_map,
						  &rm_res->desc[i], "rchan");
		irq_res.sets += rm_res->sets;
	}

	irq_res.desc = kcalloc(irq_res.sets, sizeof(*irq_res.desc), GFP_KERNEL);
	if (!irq_res.desc)
		return -ENOMEM;
	rm_res = tisci_rm->rm_ranges[RM_RANGE_TCHAN];
	if (IS_ERR(rm_res)) {
		irq_res.desc[0].start = 0;
		irq_res.desc[0].num = ud->tchan_cnt;
		i = 1;
	} else {
		for (i = 0; i < rm_res->sets; i++) {
			irq_res.desc[i].start = rm_res->desc[i].start;
			irq_res.desc[i].num = rm_res->desc[i].num;
			irq_res.desc[i].start_sec = rm_res->desc[i].start_sec;
			irq_res.desc[i].num_sec = rm_res->desc[i].num_sec;
		}
	}
	rm_res = tisci_rm->rm_ranges[RM_RANGE_RCHAN];
	if (IS_ERR(rm_res)) {
		irq_res.desc[i].start = 0;
		irq_res.desc[i].num = ud->rchan_cnt;
	} else {
		for (j = 0; j < rm_res->sets; j++, i++) {
			if (rm_res->desc[j].num) {
				irq_res.desc[i].start = rm_res->desc[j].start +
						ud->soc_data->oes.udma_rchan;
				irq_res.desc[i].num = rm_res->desc[j].num;
			}
			if (rm_res->desc[j].num_sec) {
				irq_res.desc[i].start_sec = rm_res->desc[j].start_sec +
						ud->soc_data->oes.udma_rchan;
				irq_res.desc[i].num_sec = rm_res->desc[j].num_sec;
			}
		}
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
		for (i = 0; i < rm_res->sets; i++)
			udma_mark_resource_ranges(ud, ud->rflow_gp_map,
						  &rm_res->desc[i], "gp-rflow");
	}

	return 0;
}

static int bcdma_setup_resources(struct udma_dev *ud)
{
	int ret, i, j;
	struct device *dev = ud->dev;
	struct ti_sci_resource *rm_res, irq_res;
	struct udma_tisci_rm *tisci_rm = &ud->tisci_rm;
	const struct udma_oes_offsets *oes = &ud->soc_data->oes;
	u32 cap;

	/* Set up the throughput level start indexes */
	cap = udma_read(ud->mmrs[MMR_GCFG], 0x2c);
	if (BCDMA_CAP3_UBCHAN_CNT(cap)) {
		ud->bchan_tpl.levels = 3;
		ud->bchan_tpl.start_idx[1] = BCDMA_CAP3_UBCHAN_CNT(cap);
		ud->bchan_tpl.start_idx[0] = BCDMA_CAP3_HBCHAN_CNT(cap);
	} else if (BCDMA_CAP3_HBCHAN_CNT(cap)) {
		ud->bchan_tpl.levels = 2;
		ud->bchan_tpl.start_idx[0] = BCDMA_CAP3_HBCHAN_CNT(cap);
	} else {
		ud->bchan_tpl.levels = 1;
	}

	cap = udma_read(ud->mmrs[MMR_GCFG], 0x30);
	if (BCDMA_CAP4_URCHAN_CNT(cap)) {
		ud->rchan_tpl.levels = 3;
		ud->rchan_tpl.start_idx[1] = BCDMA_CAP4_URCHAN_CNT(cap);
		ud->rchan_tpl.start_idx[0] = BCDMA_CAP4_HRCHAN_CNT(cap);
	} else if (BCDMA_CAP4_HRCHAN_CNT(cap)) {
		ud->rchan_tpl.levels = 2;
		ud->rchan_tpl.start_idx[0] = BCDMA_CAP4_HRCHAN_CNT(cap);
	} else {
		ud->rchan_tpl.levels = 1;
	}

	if (BCDMA_CAP4_UTCHAN_CNT(cap)) {
		ud->tchan_tpl.levels = 3;
		ud->tchan_tpl.start_idx[1] = BCDMA_CAP4_UTCHAN_CNT(cap);
		ud->tchan_tpl.start_idx[0] = BCDMA_CAP4_HTCHAN_CNT(cap);
	} else if (BCDMA_CAP4_HTCHAN_CNT(cap)) {
		ud->tchan_tpl.levels = 2;
		ud->tchan_tpl.start_idx[0] = BCDMA_CAP4_HTCHAN_CNT(cap);
	} else {
		ud->tchan_tpl.levels = 1;
	}

	ud->bchan_map = devm_kmalloc_array(dev, BITS_TO_LONGS(ud->bchan_cnt),
					   sizeof(unsigned long), GFP_KERNEL);
	ud->bchans = devm_kcalloc(dev, ud->bchan_cnt, sizeof(*ud->bchans),
				  GFP_KERNEL);
	ud->tchan_map = devm_kmalloc_array(dev, BITS_TO_LONGS(ud->tchan_cnt),
					   sizeof(unsigned long), GFP_KERNEL);
	ud->tchans = devm_kcalloc(dev, ud->tchan_cnt, sizeof(*ud->tchans),
				  GFP_KERNEL);
	ud->rchan_map = devm_kmalloc_array(dev, BITS_TO_LONGS(ud->rchan_cnt),
					   sizeof(unsigned long), GFP_KERNEL);
	ud->rchans = devm_kcalloc(dev, ud->rchan_cnt, sizeof(*ud->rchans),
				  GFP_KERNEL);
	/* BCDMA do not really have flows, but the driver expect it */
	ud->rflow_in_use = devm_kcalloc(dev, BITS_TO_LONGS(ud->rchan_cnt),
					sizeof(unsigned long),
					GFP_KERNEL);
	ud->rflows = devm_kcalloc(dev, ud->rchan_cnt, sizeof(*ud->rflows),
				  GFP_KERNEL);

	if (!ud->bchan_map || !ud->tchan_map || !ud->rchan_map ||
	    !ud->rflow_in_use || !ud->bchans || !ud->tchans || !ud->rchans ||
	    !ud->rflows)
		return -ENOMEM;

	/* Get resource ranges from tisci */
	for (i = 0; i < RM_RANGE_LAST; i++) {
		if (i == RM_RANGE_RFLOW || i == RM_RANGE_TFLOW)
			continue;
		if (i == RM_RANGE_BCHAN && ud->bchan_cnt == 0)
			continue;
		if (i == RM_RANGE_TCHAN && ud->tchan_cnt == 0)
			continue;
		if (i == RM_RANGE_RCHAN && ud->rchan_cnt == 0)
			continue;

		tisci_rm->rm_ranges[i] =
			devm_ti_sci_get_of_resource(tisci_rm->tisci, dev,
						    tisci_rm->tisci_dev_id,
						    (char *)range_names[i]);
	}

	irq_res.sets = 0;

	/* bchan ranges */
	if (ud->bchan_cnt) {
		rm_res = tisci_rm->rm_ranges[RM_RANGE_BCHAN];
		if (IS_ERR(rm_res)) {
			bitmap_zero(ud->bchan_map, ud->bchan_cnt);
			irq_res.sets++;
		} else {
			bitmap_fill(ud->bchan_map, ud->bchan_cnt);
			for (i = 0; i < rm_res->sets; i++)
				udma_mark_resource_ranges(ud, ud->bchan_map,
							  &rm_res->desc[i],
							  "bchan");
			irq_res.sets += rm_res->sets;
		}
	}

	/* tchan ranges */
	if (ud->tchan_cnt) {
		rm_res = tisci_rm->rm_ranges[RM_RANGE_TCHAN];
		if (IS_ERR(rm_res)) {
			bitmap_zero(ud->tchan_map, ud->tchan_cnt);
			irq_res.sets += 2;
		} else {
			bitmap_fill(ud->tchan_map, ud->tchan_cnt);
			for (i = 0; i < rm_res->sets; i++)
				udma_mark_resource_ranges(ud, ud->tchan_map,
							  &rm_res->desc[i],
							  "tchan");
			irq_res.sets += rm_res->sets * 2;
		}
	}

	/* rchan ranges */
	if (ud->rchan_cnt) {
		rm_res = tisci_rm->rm_ranges[RM_RANGE_RCHAN];
		if (IS_ERR(rm_res)) {
			bitmap_zero(ud->rchan_map, ud->rchan_cnt);
			irq_res.sets += 2;
		} else {
			bitmap_fill(ud->rchan_map, ud->rchan_cnt);
			for (i = 0; i < rm_res->sets; i++)
				udma_mark_resource_ranges(ud, ud->rchan_map,
							  &rm_res->desc[i],
							  "rchan");
			irq_res.sets += rm_res->sets * 2;
		}
	}

	irq_res.desc = kcalloc(irq_res.sets, sizeof(*irq_res.desc), GFP_KERNEL);
	if (!irq_res.desc)
		return -ENOMEM;
	if (ud->bchan_cnt) {
		rm_res = tisci_rm->rm_ranges[RM_RANGE_BCHAN];
		if (IS_ERR(rm_res)) {
			irq_res.desc[0].start = oes->bcdma_bchan_ring;
			irq_res.desc[0].num = ud->bchan_cnt;
			i = 1;
		} else {
			for (i = 0; i < rm_res->sets; i++) {
				irq_res.desc[i].start = rm_res->desc[i].start +
							oes->bcdma_bchan_ring;
				irq_res.desc[i].num = rm_res->desc[i].num;
			}
		}
	}
	if (ud->tchan_cnt) {
		rm_res = tisci_rm->rm_ranges[RM_RANGE_TCHAN];
		if (IS_ERR(rm_res)) {
			irq_res.desc[i].start = oes->bcdma_tchan_data;
			irq_res.desc[i].num = ud->tchan_cnt;
			irq_res.desc[i + 1].start = oes->bcdma_tchan_ring;
			irq_res.desc[i + 1].num = ud->tchan_cnt;
			i += 2;
		} else {
			for (j = 0; j < rm_res->sets; j++, i += 2) {
				irq_res.desc[i].start = rm_res->desc[j].start +
							oes->bcdma_tchan_data;
				irq_res.desc[i].num = rm_res->desc[j].num;

				irq_res.desc[i + 1].start = rm_res->desc[j].start +
							oes->bcdma_tchan_ring;
				irq_res.desc[i + 1].num = rm_res->desc[j].num;
			}
		}
	}
	if (ud->rchan_cnt) {
		rm_res = tisci_rm->rm_ranges[RM_RANGE_RCHAN];
		if (IS_ERR(rm_res)) {
			irq_res.desc[i].start = oes->bcdma_rchan_data;
			irq_res.desc[i].num = ud->rchan_cnt;
			irq_res.desc[i + 1].start = oes->bcdma_rchan_ring;
			irq_res.desc[i + 1].num = ud->rchan_cnt;
			i += 2;
		} else {
			for (j = 0; j < rm_res->sets; j++, i += 2) {
				irq_res.desc[i].start = rm_res->desc[j].start +
							oes->bcdma_rchan_data;
				irq_res.desc[i].num = rm_res->desc[j].num;

				irq_res.desc[i + 1].start = rm_res->desc[j].start +
							oes->bcdma_rchan_ring;
				irq_res.desc[i + 1].num = rm_res->desc[j].num;
			}
		}
	}

	ret = ti_sci_inta_msi_domain_alloc_irqs(ud->dev, &irq_res);
	kfree(irq_res.desc);
	if (ret) {
		dev_err(ud->dev, "Failed to allocate MSI interrupts\n");
		return ret;
	}

	return 0;
}

static int pktdma_setup_resources(struct udma_dev *ud)
{
	int ret, i, j;
	struct device *dev = ud->dev;
	struct ti_sci_resource *rm_res, irq_res;
	struct udma_tisci_rm *tisci_rm = &ud->tisci_rm;
	const struct udma_oes_offsets *oes = &ud->soc_data->oes;
	u32 cap3;

	/* Set up the throughput level start indexes */
	cap3 = udma_read(ud->mmrs[MMR_GCFG], 0x2c);
	if (UDMA_CAP3_UCHAN_CNT(cap3)) {
		ud->tchan_tpl.levels = 3;
		ud->tchan_tpl.start_idx[1] = UDMA_CAP3_UCHAN_CNT(cap3);
		ud->tchan_tpl.start_idx[0] = UDMA_CAP3_HCHAN_CNT(cap3);
	} else if (UDMA_CAP3_HCHAN_CNT(cap3)) {
		ud->tchan_tpl.levels = 2;
		ud->tchan_tpl.start_idx[0] = UDMA_CAP3_HCHAN_CNT(cap3);
	} else {
		ud->tchan_tpl.levels = 1;
	}

	ud->rchan_tpl.levels = ud->tchan_tpl.levels;
	ud->rchan_tpl.start_idx[0] = ud->tchan_tpl.start_idx[0];
	ud->rchan_tpl.start_idx[1] = ud->tchan_tpl.start_idx[1];

	ud->tchan_map = devm_kmalloc_array(dev, BITS_TO_LONGS(ud->tchan_cnt),
					   sizeof(unsigned long), GFP_KERNEL);
	ud->tchans = devm_kcalloc(dev, ud->tchan_cnt, sizeof(*ud->tchans),
				  GFP_KERNEL);
	ud->rchan_map = devm_kmalloc_array(dev, BITS_TO_LONGS(ud->rchan_cnt),
					   sizeof(unsigned long), GFP_KERNEL);
	ud->rchans = devm_kcalloc(dev, ud->rchan_cnt, sizeof(*ud->rchans),
				  GFP_KERNEL);
	ud->rflow_in_use = devm_kcalloc(dev, BITS_TO_LONGS(ud->rflow_cnt),
					sizeof(unsigned long),
					GFP_KERNEL);
	ud->rflows = devm_kcalloc(dev, ud->rflow_cnt, sizeof(*ud->rflows),
				  GFP_KERNEL);
	ud->tflow_map = devm_kmalloc_array(dev, BITS_TO_LONGS(ud->tflow_cnt),
					   sizeof(unsigned long), GFP_KERNEL);

	if (!ud->tchan_map || !ud->rchan_map || !ud->tflow_map || !ud->tchans ||
	    !ud->rchans || !ud->rflows || !ud->rflow_in_use)
		return -ENOMEM;

	/* Get resource ranges from tisci */
	for (i = 0; i < RM_RANGE_LAST; i++) {
		if (i == RM_RANGE_BCHAN)
			continue;

		tisci_rm->rm_ranges[i] =
			devm_ti_sci_get_of_resource(tisci_rm->tisci, dev,
						    tisci_rm->tisci_dev_id,
						    (char *)range_names[i]);
	}

	/* tchan ranges */
	rm_res = tisci_rm->rm_ranges[RM_RANGE_TCHAN];
	if (IS_ERR(rm_res)) {
		bitmap_zero(ud->tchan_map, ud->tchan_cnt);
	} else {
		bitmap_fill(ud->tchan_map, ud->tchan_cnt);
		for (i = 0; i < rm_res->sets; i++)
			udma_mark_resource_ranges(ud, ud->tchan_map,
						  &rm_res->desc[i], "tchan");
	}

	/* rchan ranges */
	rm_res = tisci_rm->rm_ranges[RM_RANGE_RCHAN];
	if (IS_ERR(rm_res)) {
		bitmap_zero(ud->rchan_map, ud->rchan_cnt);
	} else {
		bitmap_fill(ud->rchan_map, ud->rchan_cnt);
		for (i = 0; i < rm_res->sets; i++)
			udma_mark_resource_ranges(ud, ud->rchan_map,
						  &rm_res->desc[i], "rchan");
	}

	/* rflow ranges */
	rm_res = tisci_rm->rm_ranges[RM_RANGE_RFLOW];
	if (IS_ERR(rm_res)) {
		/* all rflows are assigned exclusively to Linux */
		bitmap_zero(ud->rflow_in_use, ud->rflow_cnt);
		irq_res.sets = 1;
	} else {
		bitmap_fill(ud->rflow_in_use, ud->rflow_cnt);
		for (i = 0; i < rm_res->sets; i++)
			udma_mark_resource_ranges(ud, ud->rflow_in_use,
						  &rm_res->desc[i], "rflow");
		irq_res.sets = rm_res->sets;
	}

	/* tflow ranges */
	rm_res = tisci_rm->rm_ranges[RM_RANGE_TFLOW];
	if (IS_ERR(rm_res)) {
		/* all tflows are assigned exclusively to Linux */
		bitmap_zero(ud->tflow_map, ud->tflow_cnt);
		irq_res.sets++;
	} else {
		bitmap_fill(ud->tflow_map, ud->tflow_cnt);
		for (i = 0; i < rm_res->sets; i++)
			udma_mark_resource_ranges(ud, ud->tflow_map,
						  &rm_res->desc[i], "tflow");
		irq_res.sets += rm_res->sets;
	}

	irq_res.desc = kcalloc(irq_res.sets, sizeof(*irq_res.desc), GFP_KERNEL);
	if (!irq_res.desc)
		return -ENOMEM;
	rm_res = tisci_rm->rm_ranges[RM_RANGE_TFLOW];
	if (IS_ERR(rm_res)) {
		irq_res.desc[0].start = oes->pktdma_tchan_flow;
		irq_res.desc[0].num = ud->tflow_cnt;
		i = 1;
	} else {
		for (i = 0; i < rm_res->sets; i++) {
			irq_res.desc[i].start = rm_res->desc[i].start +
						oes->pktdma_tchan_flow;
			irq_res.desc[i].num = rm_res->desc[i].num;
		}
	}
	rm_res = tisci_rm->rm_ranges[RM_RANGE_RFLOW];
	if (IS_ERR(rm_res)) {
		irq_res.desc[i].start = oes->pktdma_rchan_flow;
		irq_res.desc[i].num = ud->rflow_cnt;
	} else {
		for (j = 0; j < rm_res->sets; j++, i++) {
			irq_res.desc[i].start = rm_res->desc[j].start +
						oes->pktdma_rchan_flow;
			irq_res.desc[i].num = rm_res->desc[j].num;
		}
	}
	ret = ti_sci_inta_msi_domain_alloc_irqs(ud->dev, &irq_res);
	kfree(irq_res.desc);
	if (ret) {
		dev_err(ud->dev, "Failed to allocate MSI interrupts\n");
		return ret;
	}

	return 0;
}

static int setup_resources(struct udma_dev *ud)
{
	struct device *dev = ud->dev;
	int ch_count, ret;

	switch (ud->match_data->type) {
	case DMA_TYPE_UDMA:
		ret = udma_setup_resources(ud);
		break;
	case DMA_TYPE_BCDMA:
		ret = bcdma_setup_resources(ud);
		break;
	case DMA_TYPE_PKTDMA:
		ret = pktdma_setup_resources(ud);
		break;
	default:
		return -EINVAL;
	}

	if (ret)
		return ret;

	ch_count  = ud->bchan_cnt + ud->tchan_cnt + ud->rchan_cnt;
	if (ud->bchan_cnt)
		ch_count -= bitmap_weight(ud->bchan_map, ud->bchan_cnt);
	ch_count -= bitmap_weight(ud->tchan_map, ud->tchan_cnt);
	ch_count -= bitmap_weight(ud->rchan_map, ud->rchan_cnt);
	if (!ch_count)
		return -ENODEV;

	ud->channels = devm_kcalloc(dev, ch_count, sizeof(*ud->channels),
				    GFP_KERNEL);
	if (!ud->channels)
		return -ENOMEM;

	switch (ud->match_data->type) {
	case DMA_TYPE_UDMA:
		dev_info(dev,
			 "Channels: %d (tchan: %u, rchan: %u, gp-rflow: %u)\n",
			 ch_count,
			 ud->tchan_cnt - bitmap_weight(ud->tchan_map,
						       ud->tchan_cnt),
			 ud->rchan_cnt - bitmap_weight(ud->rchan_map,
						       ud->rchan_cnt),
			 ud->rflow_cnt - bitmap_weight(ud->rflow_gp_map,
						       ud->rflow_cnt));
		break;
	case DMA_TYPE_BCDMA:
		dev_info(dev,
			 "Channels: %d (bchan: %u, tchan: %u, rchan: %u)\n",
			 ch_count,
			 ud->bchan_cnt - bitmap_weight(ud->bchan_map,
						       ud->bchan_cnt),
			 ud->tchan_cnt - bitmap_weight(ud->tchan_map,
						       ud->tchan_cnt),
			 ud->rchan_cnt - bitmap_weight(ud->rchan_map,
						       ud->rchan_cnt));
		break;
	case DMA_TYPE_PKTDMA:
		dev_info(dev,
			 "Channels: %d (tchan: %u, rchan: %u)\n",
			 ch_count,
			 ud->tchan_cnt - bitmap_weight(ud->tchan_map,
						       ud->tchan_cnt),
			 ud->rchan_cnt - bitmap_weight(ud->rchan_map,
						       ud->rchan_cnt));
		break;
	default:
		break;
	}

	return ch_count;
}

static int udma_setup_rx_flush(struct udma_dev *ud)
{
	struct udma_rx_flush *rx_flush = &ud->rx_flush;
	struct cppi5_desc_hdr_t *tr_desc;
	struct cppi5_tr_type1_t *tr_req;
	struct cppi5_host_desc_t *desc;
	struct device *dev = ud->dev;
	struct udma_hwdesc *hwdesc;
	size_t tr_size;

	/* Allocate 1K buffer for discarded data on RX channel teardown */
	rx_flush->buffer_size = SZ_1K;
	rx_flush->buffer_vaddr = devm_kzalloc(dev, rx_flush->buffer_size,
					      GFP_KERNEL);
	if (!rx_flush->buffer_vaddr)
		return -ENOMEM;

	rx_flush->buffer_paddr = dma_map_single(dev, rx_flush->buffer_vaddr,
						rx_flush->buffer_size,
						DMA_TO_DEVICE);
	if (dma_mapping_error(dev, rx_flush->buffer_paddr))
		return -ENOMEM;

	/* Set up descriptor to be used for TR mode */
	hwdesc = &rx_flush->hwdescs[0];
	tr_size = sizeof(struct cppi5_tr_type1_t);
	hwdesc->cppi5_desc_size = cppi5_trdesc_calc_size(tr_size, 1);
	hwdesc->cppi5_desc_size = ALIGN(hwdesc->cppi5_desc_size,
					ud->desc_align);

	hwdesc->cppi5_desc_vaddr = devm_kzalloc(dev, hwdesc->cppi5_desc_size,
						GFP_KERNEL);
	if (!hwdesc->cppi5_desc_vaddr)
		return -ENOMEM;

	hwdesc->cppi5_desc_paddr = dma_map_single(dev, hwdesc->cppi5_desc_vaddr,
						  hwdesc->cppi5_desc_size,
						  DMA_TO_DEVICE);
	if (dma_mapping_error(dev, hwdesc->cppi5_desc_paddr))
		return -ENOMEM;

	/* Start of the TR req records */
	hwdesc->tr_req_base = hwdesc->cppi5_desc_vaddr + tr_size;
	/* Start address of the TR response array */
	hwdesc->tr_resp_base = hwdesc->tr_req_base + tr_size;

	tr_desc = hwdesc->cppi5_desc_vaddr;
	cppi5_trdesc_init(tr_desc, 1, tr_size, 0, 0);
	cppi5_desc_set_pktids(tr_desc, 0, CPPI5_INFO1_DESC_FLOWID_DEFAULT);
	cppi5_desc_set_retpolicy(tr_desc, 0, 0);

	tr_req = hwdesc->tr_req_base;
	cppi5_tr_init(&tr_req->flags, CPPI5_TR_TYPE1, false, false,
		      CPPI5_TR_EVENT_SIZE_COMPLETION, 0);
	cppi5_tr_csf_set(&tr_req->flags, CPPI5_TR_CSF_SUPR_EVT);

	tr_req->addr = rx_flush->buffer_paddr;
	tr_req->icnt0 = rx_flush->buffer_size;
	tr_req->icnt1 = 1;

	dma_sync_single_for_device(dev, hwdesc->cppi5_desc_paddr,
				   hwdesc->cppi5_desc_size, DMA_TO_DEVICE);

	/* Set up descriptor to be used for packet mode */
	hwdesc = &rx_flush->hwdescs[1];
	hwdesc->cppi5_desc_size = ALIGN(sizeof(struct cppi5_host_desc_t) +
					CPPI5_INFO0_HDESC_EPIB_SIZE +
					CPPI5_INFO0_HDESC_PSDATA_MAX_SIZE,
					ud->desc_align);

	hwdesc->cppi5_desc_vaddr = devm_kzalloc(dev, hwdesc->cppi5_desc_size,
						GFP_KERNEL);
	if (!hwdesc->cppi5_desc_vaddr)
		return -ENOMEM;

	hwdesc->cppi5_desc_paddr = dma_map_single(dev, hwdesc->cppi5_desc_vaddr,
						  hwdesc->cppi5_desc_size,
						  DMA_TO_DEVICE);
	if (dma_mapping_error(dev, hwdesc->cppi5_desc_paddr))
		return -ENOMEM;

	desc = hwdesc->cppi5_desc_vaddr;
	cppi5_hdesc_init(desc, 0, 0);
	cppi5_desc_set_pktids(&desc->hdr, 0, CPPI5_INFO1_DESC_FLOWID_DEFAULT);
	cppi5_desc_set_retpolicy(&desc->hdr, 0, 0);

	cppi5_hdesc_attach_buf(desc,
			       rx_flush->buffer_paddr, rx_flush->buffer_size,
			       rx_flush->buffer_paddr, rx_flush->buffer_size);

	dma_sync_single_for_device(dev, hwdesc->cppi5_desc_paddr,
				   hwdesc->cppi5_desc_size, DMA_TO_DEVICE);
	return 0;
}

#ifdef CONFIG_DEBUG_FS
static void udma_dbg_summary_show_chan(struct seq_file *s,
				       struct dma_chan *chan)
{
	struct udma_chan *uc = to_udma_chan(chan);
	struct udma_chan_config *ucc = &uc->config;

	seq_printf(s, " %-13s| %s", dma_chan_name(chan),
		   chan->dbg_client_name ?: "in-use");
	if (ucc->tr_trigger_type)
		seq_puts(s, " (triggered, ");
	else
		seq_printf(s, " (%s, ",
			   dmaengine_get_direction_text(uc->config.dir));

	switch (uc->config.dir) {
	case DMA_MEM_TO_MEM:
		if (uc->ud->match_data->type == DMA_TYPE_BCDMA) {
			seq_printf(s, "bchan%d)\n", uc->bchan->id);
			return;
		}

		seq_printf(s, "chan%d pair [0x%04x -> 0x%04x], ", uc->tchan->id,
			   ucc->src_thread, ucc->dst_thread);
		break;
	case DMA_DEV_TO_MEM:
		seq_printf(s, "rchan%d [0x%04x -> 0x%04x], ", uc->rchan->id,
			   ucc->src_thread, ucc->dst_thread);
		if (uc->ud->match_data->type == DMA_TYPE_PKTDMA)
			seq_printf(s, "rflow%d, ", uc->rflow->id);
		break;
	case DMA_MEM_TO_DEV:
		seq_printf(s, "tchan%d [0x%04x -> 0x%04x], ", uc->tchan->id,
			   ucc->src_thread, ucc->dst_thread);
		if (uc->ud->match_data->type == DMA_TYPE_PKTDMA)
			seq_printf(s, "tflow%d, ", uc->tchan->tflow_id);
		break;
	default:
		seq_printf(s, ")\n");
		return;
	}

	if (ucc->ep_type == PSIL_EP_NATIVE) {
		seq_printf(s, "PSI-L Native");
		if (ucc->metadata_size) {
			seq_printf(s, "[%s", ucc->needs_epib ? " EPIB" : "");
			if (ucc->psd_size)
				seq_printf(s, " PSDsize:%u", ucc->psd_size);
			seq_printf(s, " ]");
		}
	} else {
		seq_printf(s, "PDMA");
		if (ucc->enable_acc32 || ucc->enable_burst)
			seq_printf(s, "[%s%s ]",
				   ucc->enable_acc32 ? " ACC32" : "",
				   ucc->enable_burst ? " BURST" : "");
	}

	seq_printf(s, ", %s)\n", ucc->pkt_mode ? "Packet mode" : "TR mode");
}

static void udma_dbg_summary_show(struct seq_file *s,
				  struct dma_device *dma_dev)
{
	struct dma_chan *chan;

	list_for_each_entry(chan, &dma_dev->channels, device_node) {
		if (chan->client_count)
			udma_dbg_summary_show_chan(s, chan);
	}
}
#endif /* CONFIG_DEBUG_FS */

static enum dmaengine_alignment udma_get_copy_align(struct udma_dev *ud)
{
	const struct udma_match_data *match_data = ud->match_data;
	u8 tpl;

	if (!match_data->enable_memcpy_support)
		return DMAENGINE_ALIGN_8_BYTES;

	/* Get the highest TPL level the device supports for memcpy */
	if (ud->bchan_cnt)
		tpl = udma_get_chan_tpl_index(&ud->bchan_tpl, 0);
	else if (ud->tchan_cnt)
		tpl = udma_get_chan_tpl_index(&ud->tchan_tpl, 0);
	else
		return DMAENGINE_ALIGN_8_BYTES;

	switch (match_data->burst_size[tpl]) {
	case TI_SCI_RM_UDMAP_CHAN_BURST_SIZE_256_BYTES:
		return DMAENGINE_ALIGN_256_BYTES;
	case TI_SCI_RM_UDMAP_CHAN_BURST_SIZE_128_BYTES:
		return DMAENGINE_ALIGN_128_BYTES;
	case TI_SCI_RM_UDMAP_CHAN_BURST_SIZE_64_BYTES:
	fallthrough;
	default:
		return DMAENGINE_ALIGN_64_BYTES;
	}
}

#define TI_UDMAC_BUSWIDTHS	(BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) | \
				 BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) | \
				 BIT(DMA_SLAVE_BUSWIDTH_3_BYTES) | \
				 BIT(DMA_SLAVE_BUSWIDTH_4_BYTES) | \
				 BIT(DMA_SLAVE_BUSWIDTH_8_BYTES))

static int udma_probe(struct platform_device *pdev)
{
	struct device_node *navss_node = pdev->dev.parent->of_node;
	const struct soc_device_attribute *soc;
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

	match = of_match_node(udma_of_match, dev->of_node);
	if (!match)
		match = of_match_node(bcdma_of_match, dev->of_node);
	if (!match) {
		match = of_match_node(pktdma_of_match, dev->of_node);
		if (!match) {
			dev_err(dev, "No compatible match found\n");
			return -ENODEV;
		}
	}
	ud->match_data = match->data;

	soc = soc_device_match(k3_soc_devices);
	if (!soc) {
		dev_err(dev, "No compatible SoC found\n");
		return -ENODEV;
	}
	ud->soc_data = soc->data;

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

	if (ud->match_data->type == DMA_TYPE_UDMA) {
		ret = of_property_read_u32(dev->of_node, "ti,udma-atype",
					   &ud->atype);
		if (!ret && ud->atype > 2) {
			dev_err(dev, "Invalid atype: %u\n", ud->atype);
			return -EINVAL;
		}
	} else {
		ret = of_property_read_u32(dev->of_node, "ti,asel",
					   &ud->asel);
		if (!ret && ud->asel > 15) {
			dev_err(dev, "Invalid asel: %u\n", ud->asel);
			return -EINVAL;
		}
	}

	ud->tisci_rm.tisci_udmap_ops = &ud->tisci_rm.tisci->ops.rm_udmap_ops;
	ud->tisci_rm.tisci_psil_ops = &ud->tisci_rm.tisci->ops.rm_psil_ops;

	if (ud->match_data->type == DMA_TYPE_UDMA) {
		ud->ringacc = of_k3_ringacc_get_by_phandle(dev->of_node, "ti,ringacc");
	} else {
		struct k3_ringacc_init_data ring_init_data;

		ring_init_data.tisci = ud->tisci_rm.tisci;
		ring_init_data.tisci_dev_id = ud->tisci_rm.tisci_dev_id;
		if (ud->match_data->type == DMA_TYPE_BCDMA) {
			ring_init_data.num_rings = ud->bchan_cnt +
						   ud->tchan_cnt +
						   ud->rchan_cnt;
		} else {
			ring_init_data.num_rings = ud->rflow_cnt +
						   ud->tflow_cnt;
		}

		ud->ringacc = k3_ringacc_dmarings_init(pdev, &ring_init_data);
	}

	if (IS_ERR(ud->ringacc))
		return PTR_ERR(ud->ringacc);

	dev->msi.domain = of_msi_get_domain(dev, dev->of_node,
					    DOMAIN_BUS_TI_SCI_INTA_MSI);
	if (!dev->msi.domain) {
		dev_err(dev, "Failed to get MSI domain\n");
		return -EPROBE_DEFER;
	}

	dma_cap_set(DMA_SLAVE, ud->ddev.cap_mask);
	/* cyclic operation is not supported via PKTDMA */
	if (ud->match_data->type != DMA_TYPE_PKTDMA) {
		dma_cap_set(DMA_CYCLIC, ud->ddev.cap_mask);
		ud->ddev.device_prep_dma_cyclic = udma_prep_dma_cyclic;
	}

	ud->ddev.device_config = udma_slave_config;
	ud->ddev.device_prep_slave_sg = udma_prep_slave_sg;
	ud->ddev.device_issue_pending = udma_issue_pending;
	ud->ddev.device_tx_status = udma_tx_status;
	ud->ddev.device_pause = udma_pause;
	ud->ddev.device_resume = udma_resume;
	ud->ddev.device_terminate_all = udma_terminate_all;
	ud->ddev.device_synchronize = udma_synchronize;
#ifdef CONFIG_DEBUG_FS
	ud->ddev.dbg_summary_show = udma_dbg_summary_show;
#endif

	switch (ud->match_data->type) {
	case DMA_TYPE_UDMA:
		ud->ddev.device_alloc_chan_resources =
					udma_alloc_chan_resources;
		break;
	case DMA_TYPE_BCDMA:
		ud->ddev.device_alloc_chan_resources =
					bcdma_alloc_chan_resources;
		ud->ddev.device_router_config = bcdma_router_config;
		break;
	case DMA_TYPE_PKTDMA:
		ud->ddev.device_alloc_chan_resources =
					pktdma_alloc_chan_resources;
		break;
	default:
		return -EINVAL;
	}
	ud->ddev.device_free_chan_resources = udma_free_chan_resources;

	ud->ddev.src_addr_widths = TI_UDMAC_BUSWIDTHS;
	ud->ddev.dst_addr_widths = TI_UDMAC_BUSWIDTHS;
	ud->ddev.directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	ud->ddev.residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;
	ud->ddev.desc_metadata_modes = DESC_METADATA_CLIENT |
				       DESC_METADATA_ENGINE;
	if (ud->match_data->enable_memcpy_support &&
	    !(ud->match_data->type == DMA_TYPE_BCDMA && ud->bchan_cnt == 0)) {
		dma_cap_set(DMA_MEMCPY, ud->ddev.cap_mask);
		ud->ddev.device_prep_dma_memcpy = udma_prep_dma_memcpy;
		ud->ddev.directions |= BIT(DMA_MEM_TO_MEM);
	}

	ud->ddev.dev = dev;
	ud->dev = dev;
	ud->psil_base = ud->match_data->psil_base;

	INIT_LIST_HEAD(&ud->ddev.channels);
	INIT_LIST_HEAD(&ud->desc_to_purge);

	ch_count = setup_resources(ud);
	if (ch_count <= 0)
		return ch_count;

	spin_lock_init(&ud->lock);
	INIT_WORK(&ud->purge_work, udma_purge_desc_work);

	ud->desc_align = 64;
	if (ud->desc_align < dma_get_cache_alignment())
		ud->desc_align = dma_get_cache_alignment();

	ret = udma_setup_rx_flush(ud);
	if (ret)
		return ret;

	for (i = 0; i < ud->bchan_cnt; i++) {
		struct udma_bchan *bchan = &ud->bchans[i];

		bchan->id = i;
		bchan->reg_rt = ud->mmrs[MMR_BCHANRT] + i * 0x1000;
	}

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
		uc->bchan = NULL;
		uc->tchan = NULL;
		uc->rchan = NULL;
		uc->config.remote_thread_id = -1;
		uc->config.mapped_channel_id = -1;
		uc->config.default_flow_id = -1;
		uc->config.dir = DMA_MEM_TO_MEM;
		uc->name = devm_kasprintf(dev, GFP_KERNEL, "%s chan%d",
					  dev_name(dev), i);

		vchan_init(&uc->vc, &ud->ddev);
		/* Use custom vchan completion handling */
		tasklet_setup(&uc->vc.task, udma_vchan_complete);
		init_completion(&uc->teardown_completed);
		INIT_DELAYED_WORK(&uc->tx_drain.work, udma_check_tx_completion);
	}

	/* Configure the copy_align to the maximum burst size the device supports */
	ud->ddev.copy_align = udma_get_copy_align(ud);

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

static struct platform_driver bcdma_driver = {
	.driver = {
		.name	= "ti-bcdma",
		.of_match_table = bcdma_of_match,
		.suppress_bind_attrs = true,
	},
	.probe		= udma_probe,
};
builtin_platform_driver(bcdma_driver);

static struct platform_driver pktdma_driver = {
	.driver = {
		.name	= "ti-pktdma",
		.of_match_table = pktdma_of_match,
		.suppress_bind_attrs = true,
	},
	.probe		= udma_probe,
};
builtin_platform_driver(pktdma_driver);

/* Private interfaces to UDMA */
#include "k3-udma-private.c"
