/* linux/arch/arm/plat-samsung/s3c-pl330.c
 *
 * Copyright (C) 2010 Samsung Electronics Co. Ltd.
 *	Jaswinder Singh <jassi.brar@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/platform_device.h>

#include <asm/hardware/pl330.h>

#include <plat/s3c-pl330-pdata.h>

/**
 * struct s3c_pl330_dmac - Logical representation of a PL330 DMAC.
 * @busy_chan: Number of channels currently busy.
 * @peri: List of IDs of peripherals this DMAC can work with.
 * @node: To attach to the global list of DMACs.
 * @pi: PL330 configuration info for the DMAC.
 * @kmcache: Pool to quickly allocate xfers for all channels in the dmac.
 */
struct s3c_pl330_dmac {
	unsigned		busy_chan;
	enum dma_ch		*peri;
	struct list_head	node;
	struct pl330_info	*pi;
	struct kmem_cache	*kmcache;
};

/**
 * struct s3c_pl330_xfer - A request submitted by S3C DMA clients.
 * @token: Xfer ID provided by the client.
 * @node: To attach to the list of xfers on a channel.
 * @px: Xfer for PL330 core.
 * @chan: Owner channel of this xfer.
 */
struct s3c_pl330_xfer {
	void			*token;
	struct list_head	node;
	struct pl330_xfer	px;
	struct s3c_pl330_chan	*chan;
};

/**
 * struct s3c_pl330_chan - Logical channel to communicate with
 * 	a Physical peripheral.
 * @pl330_chan_id: Token of a hardware channel thread of PL330 DMAC.
 * 	NULL if the channel is available to be acquired.
 * @id: ID of the peripheral that this channel can communicate with.
 * @options: Options specified by the client.
 * @sdaddr: Address provided via s3c2410_dma_devconfig.
 * @node: To attach to the global list of channels.
 * @lrq: Pointer to the last submitted pl330_req to PL330 core.
 * @xfer_list: To manage list of xfers enqueued.
 * @req: Two requests to communicate with the PL330 engine.
 * @callback_fn: Callback function to the client.
 * @rqcfg: Channel configuration for the xfers.
 * @xfer_head: Pointer to the xfer to be next excecuted.
 * @dmac: Pointer to the DMAC that manages this channel, NULL if the
 * 	channel is available to be acquired.
 * @client: Client of this channel. NULL if the
 * 	channel is available to be acquired.
 */
struct s3c_pl330_chan {
	void				*pl330_chan_id;
	enum dma_ch			id;
	unsigned int			options;
	unsigned long			sdaddr;
	struct list_head		node;
	struct pl330_req		*lrq;
	struct list_head		xfer_list;
	struct pl330_req		req[2];
	s3c2410_dma_cbfn_t		callback_fn;
	struct pl330_reqcfg		rqcfg;
	struct s3c_pl330_xfer		*xfer_head;
	struct s3c_pl330_dmac		*dmac;
	struct s3c2410_dma_client	*client;
};

/* All DMACs in the platform */
static LIST_HEAD(dmac_list);

/* All channels to peripherals in the platform */
static LIST_HEAD(chan_list);

/*
 * Since we add resources(DMACs and Channels) to the global pool,
 * we need to guard access to the resources using a global lock
 */
static DEFINE_SPINLOCK(res_lock);

/* Returns the channel with ID 'id' in the chan_list */
static struct s3c_pl330_chan *id_to_chan(const enum dma_ch id)
{
	struct s3c_pl330_chan *ch;

	list_for_each_entry(ch, &chan_list, node)
		if (ch->id == id)
			return ch;

	return NULL;
}

/* Allocate a new channel with ID 'id' and add to chan_list */
static void chan_add(const enum dma_ch id)
{
	struct s3c_pl330_chan *ch = id_to_chan(id);

	/* Return if the channel already exists */
	if (ch)
		return;

	ch = kmalloc(sizeof(*ch), GFP_KERNEL);
	/* Return silently to work with other channels */
	if (!ch)
		return;

	ch->id = id;
	ch->dmac = NULL;

	list_add_tail(&ch->node, &chan_list);
}

/* If the channel is not yet acquired by any client */
static bool chan_free(struct s3c_pl330_chan *ch)
{
	if (!ch)
		return false;

	/* Channel points to some DMAC only when it's acquired */
	return ch->dmac ? false : true;
}

/*
 * Returns 0 is peripheral i/f is invalid or not present on the dmac.
 * Index + 1, otherwise.
 */
static unsigned iface_of_dmac(struct s3c_pl330_dmac *dmac, enum dma_ch ch_id)
{
	enum dma_ch *id = dmac->peri;
	int i;

	/* Discount invalid markers */
	if (ch_id == DMACH_MAX)
		return 0;

	for (i = 0; i < PL330_MAX_PERI; i++)
		if (id[i] == ch_id)
			return i + 1;

	return 0;
}

/* If all channel threads of the DMAC are busy */
static inline bool dmac_busy(struct s3c_pl330_dmac *dmac)
{
	struct pl330_info *pi = dmac->pi;

	return (dmac->busy_chan < pi->pcfg.num_chan) ? false : true;
}

/*
 * Returns the number of free channels that
 * can be handled by this dmac only.
 */
static unsigned ch_onlyby_dmac(struct s3c_pl330_dmac *dmac)
{
	enum dma_ch *id = dmac->peri;
	struct s3c_pl330_dmac *d;
	struct s3c_pl330_chan *ch;
	unsigned found, count = 0;
	enum dma_ch p;
	int i;

	for (i = 0; i < PL330_MAX_PERI; i++) {
		p = id[i];
		ch = id_to_chan(p);

		if (p == DMACH_MAX || !chan_free(ch))
			continue;

		found = 0;
		list_for_each_entry(d, &dmac_list, node) {
			if (d != dmac && iface_of_dmac(d, ch->id)) {
				found = 1;
				break;
			}
		}
		if (!found)
			count++;
	}

	return count;
}

/*
 * Measure of suitability of 'dmac' handling 'ch'
 *
 * 0 indicates 'dmac' can not handle 'ch' either
 * because it is not supported by the hardware or
 * because all dmac channels are currently busy.
 *
 * >0 vlaue indicates 'dmac' has the capability.
 * The bigger the value the more suitable the dmac.
 */
#define MAX_SUIT	UINT_MAX
#define MIN_SUIT	0

static unsigned suitablility(struct s3c_pl330_dmac *dmac,
		struct s3c_pl330_chan *ch)
{
	struct pl330_info *pi = dmac->pi;
	enum dma_ch *id = dmac->peri;
	struct s3c_pl330_dmac *d;
	unsigned s;
	int i;

	s = MIN_SUIT;
	/* If all the DMAC channel threads are busy */
	if (dmac_busy(dmac))
		return s;

	for (i = 0; i < PL330_MAX_PERI; i++)
		if (id[i] == ch->id)
			break;

	/* If the 'dmac' can't talk to 'ch' */
	if (i == PL330_MAX_PERI)
		return s;

	s = MAX_SUIT;
	list_for_each_entry(d, &dmac_list, node) {
		/*
		 * If some other dmac can talk to this
		 * peri and has some channel free.
		 */
		if (d != dmac && iface_of_dmac(d, ch->id) && !dmac_busy(d)) {
			s = 0;
			break;
		}
	}
	if (s)
		return s;

	s = 100;

	/* Good if free chans are more, bad otherwise */
	s += (pi->pcfg.num_chan - dmac->busy_chan) - ch_onlyby_dmac(dmac);

	return s;
}

/* More than one DMAC may have capability to transfer data with the
 * peripheral. This function assigns most suitable DMAC to manage the
 * channel and hence communicate with the peripheral.
 */
static struct s3c_pl330_dmac *map_chan_to_dmac(struct s3c_pl330_chan *ch)
{
	struct s3c_pl330_dmac *d, *dmac = NULL;
	unsigned sn, sl = MIN_SUIT;

	list_for_each_entry(d, &dmac_list, node) {
		sn = suitablility(d, ch);

		if (sn == MAX_SUIT)
			return d;

		if (sn > sl)
			dmac = d;
	}

	return dmac;
}

/* Acquire the channel for peripheral 'id' */
static struct s3c_pl330_chan *chan_acquire(const enum dma_ch id)
{
	struct s3c_pl330_chan *ch = id_to_chan(id);
	struct s3c_pl330_dmac *dmac;

	/* If the channel doesn't exist or is already acquired */
	if (!ch || !chan_free(ch)) {
		ch = NULL;
		goto acq_exit;
	}

	dmac = map_chan_to_dmac(ch);
	/* If couldn't map */
	if (!dmac) {
		ch = NULL;
		goto acq_exit;
	}

	dmac->busy_chan++;
	ch->dmac = dmac;

acq_exit:
	return ch;
}

/* Delete xfer from the queue */
static inline void del_from_queue(struct s3c_pl330_xfer *xfer)
{
	struct s3c_pl330_xfer *t;
	struct s3c_pl330_chan *ch;
	int found;

	if (!xfer)
		return;

	ch = xfer->chan;

	/* Make sure xfer is in the queue */
	found = 0;
	list_for_each_entry(t, &ch->xfer_list, node)
		if (t == xfer) {
			found = 1;
			break;
		}

	if (!found)
		return;

	/* If xfer is last entry in the queue */
	if (xfer->node.next == &ch->xfer_list)
		t = list_entry(ch->xfer_list.next,
				struct s3c_pl330_xfer, node);
	else
		t = list_entry(xfer->node.next,
				struct s3c_pl330_xfer, node);

	/* If there was only one node left */
	if (t == xfer)
		ch->xfer_head = NULL;
	else if (ch->xfer_head == xfer)
		ch->xfer_head = t;

	list_del(&xfer->node);
}

/* Provides pointer to the next xfer in the queue.
 * If CIRCULAR option is set, the list is left intact,
 * otherwise the xfer is removed from the list.
 * Forced delete 'pluck' can be set to override the CIRCULAR option.
 */
static struct s3c_pl330_xfer *get_from_queue(struct s3c_pl330_chan *ch,
		int pluck)
{
	struct s3c_pl330_xfer *xfer = ch->xfer_head;

	if (!xfer)
		return NULL;

	/* If xfer is last entry in the queue */
	if (xfer->node.next == &ch->xfer_list)
		ch->xfer_head = list_entry(ch->xfer_list.next,
					struct s3c_pl330_xfer, node);
	else
		ch->xfer_head = list_entry(xfer->node.next,
					struct s3c_pl330_xfer, node);

	if (pluck || !(ch->options & S3C2410_DMAF_CIRCULAR))
		del_from_queue(xfer);

	return xfer;
}

static inline void add_to_queue(struct s3c_pl330_chan *ch,
		struct s3c_pl330_xfer *xfer, int front)
{
	struct pl330_xfer *xt;

	/* If queue empty */
	if (ch->xfer_head == NULL)
		ch->xfer_head = xfer;

	xt = &ch->xfer_head->px;
	/* If the head already submitted (CIRCULAR head) */
	if (ch->options & S3C2410_DMAF_CIRCULAR &&
		(xt == ch->req[0].x || xt == ch->req[1].x))
		ch->xfer_head = xfer;

	/* If this is a resubmission, it should go at the head */
	if (front) {
		ch->xfer_head = xfer;
		list_add(&xfer->node, &ch->xfer_list);
	} else {
		list_add_tail(&xfer->node, &ch->xfer_list);
	}
}

static inline void _finish_off(struct s3c_pl330_xfer *xfer,
		enum s3c2410_dma_buffresult res, int ffree)
{
	struct s3c_pl330_chan *ch;

	if (!xfer)
		return;

	ch = xfer->chan;

	/* Do callback */
	if (ch->callback_fn)
		ch->callback_fn(NULL, xfer->token, xfer->px.bytes, res);

	/* Force Free or if buffer is not needed anymore */
	if (ffree || !(ch->options & S3C2410_DMAF_CIRCULAR))
		kmem_cache_free(ch->dmac->kmcache, xfer);
}

static inline int s3c_pl330_submit(struct s3c_pl330_chan *ch,
		struct pl330_req *r)
{
	struct s3c_pl330_xfer *xfer;
	int ret = 0;

	/* If already submitted */
	if (r->x)
		return 0;

	xfer = get_from_queue(ch, 0);
	if (xfer) {
		r->x = &xfer->px;

		/* Use max bandwidth for M<->M xfers */
		if (r->rqtype == MEMTOMEM) {
			struct pl330_info *pi = xfer->chan->dmac->pi;
			int burst = 1 << ch->rqcfg.brst_size;
			u32 bytes = r->x->bytes;
			int bl;

			bl = pi->pcfg.data_bus_width / 8;
			bl *= pi->pcfg.data_buf_dep;
			bl /= burst;

			/* src/dst_burst_len can't be more than 16 */
			if (bl > 16)
				bl = 16;

			while (bl > 1) {
				if (!(bytes % (bl * burst)))
					break;
				bl--;
			}

			ch->rqcfg.brst_len = bl;
		} else {
			ch->rqcfg.brst_len = 1;
		}

		ret = pl330_submit_req(ch->pl330_chan_id, r);

		/* If submission was successful */
		if (!ret) {
			ch->lrq = r; /* latest submitted req */
			return 0;
		}

		r->x = NULL;

		/* If both of the PL330 ping-pong buffers filled */
		if (ret == -EAGAIN) {
			dev_err(ch->dmac->pi->dev, "%s:%d!\n",
				__func__, __LINE__);
			/* Queue back again */
			add_to_queue(ch, xfer, 1);
			ret = 0;
		} else {
			dev_err(ch->dmac->pi->dev, "%s:%d!\n",
				__func__, __LINE__);
			_finish_off(xfer, S3C2410_RES_ERR, 0);
		}
	}

	return ret;
}

static void s3c_pl330_rq(struct s3c_pl330_chan *ch,
	struct pl330_req *r, enum pl330_op_err err)
{
	unsigned long flags;
	struct s3c_pl330_xfer *xfer;
	struct pl330_xfer *xl = r->x;
	enum s3c2410_dma_buffresult res;

	spin_lock_irqsave(&res_lock, flags);

	r->x = NULL;

	s3c_pl330_submit(ch, r);

	spin_unlock_irqrestore(&res_lock, flags);

	/* Map result to S3C DMA API */
	if (err == PL330_ERR_NONE)
		res = S3C2410_RES_OK;
	else if (err == PL330_ERR_ABORT)
		res = S3C2410_RES_ABORT;
	else
		res = S3C2410_RES_ERR;

	/* If last request had some xfer */
	if (xl) {
		xfer = container_of(xl, struct s3c_pl330_xfer, px);
		_finish_off(xfer, res, 0);
	} else {
		dev_info(ch->dmac->pi->dev, "%s:%d No Xfer?!\n",
			__func__, __LINE__);
	}
}

static void s3c_pl330_rq0(void *token, enum pl330_op_err err)
{
	struct pl330_req *r = token;
	struct s3c_pl330_chan *ch = container_of(r,
					struct s3c_pl330_chan, req[0]);
	s3c_pl330_rq(ch, r, err);
}

static void s3c_pl330_rq1(void *token, enum pl330_op_err err)
{
	struct pl330_req *r = token;
	struct s3c_pl330_chan *ch = container_of(r,
					struct s3c_pl330_chan, req[1]);
	s3c_pl330_rq(ch, r, err);
}

/* Release an acquired channel */
static void chan_release(struct s3c_pl330_chan *ch)
{
	struct s3c_pl330_dmac *dmac;

	if (chan_free(ch))
		return;

	dmac = ch->dmac;
	ch->dmac = NULL;
	dmac->busy_chan--;
}

int s3c2410_dma_ctrl(enum dma_ch id, enum s3c2410_chan_op op)
{
	struct s3c_pl330_xfer *xfer;
	enum pl330_chan_op pl330op;
	struct s3c_pl330_chan *ch;
	unsigned long flags;
	int idx, ret;

	spin_lock_irqsave(&res_lock, flags);

	ch = id_to_chan(id);

	if (!ch || chan_free(ch)) {
		ret = -EINVAL;
		goto ctrl_exit;
	}

	switch (op) {
	case S3C2410_DMAOP_START:
		/* Make sure both reqs are enqueued */
		idx = (ch->lrq == &ch->req[0]) ? 1 : 0;
		s3c_pl330_submit(ch, &ch->req[idx]);
		s3c_pl330_submit(ch, &ch->req[1 - idx]);
		pl330op = PL330_OP_START;
		break;

	case S3C2410_DMAOP_STOP:
		pl330op = PL330_OP_ABORT;
		break;

	case S3C2410_DMAOP_FLUSH:
		pl330op = PL330_OP_FLUSH;
		break;

	case S3C2410_DMAOP_PAUSE:
	case S3C2410_DMAOP_RESUME:
	case S3C2410_DMAOP_TIMEOUT:
	case S3C2410_DMAOP_STARTED:
		spin_unlock_irqrestore(&res_lock, flags);
		return 0;

	default:
		spin_unlock_irqrestore(&res_lock, flags);
		return -EINVAL;
	}

	ret = pl330_chan_ctrl(ch->pl330_chan_id, pl330op);

	if (pl330op == PL330_OP_START) {
		spin_unlock_irqrestore(&res_lock, flags);
		return ret;
	}

	idx = (ch->lrq == &ch->req[0]) ? 1 : 0;

	/* Abort the current xfer */
	if (ch->req[idx].x) {
		xfer = container_of(ch->req[idx].x,
				struct s3c_pl330_xfer, px);

		/* Drop xfer during FLUSH */
		if (pl330op == PL330_OP_FLUSH)
			del_from_queue(xfer);

		ch->req[idx].x = NULL;

		spin_unlock_irqrestore(&res_lock, flags);
		_finish_off(xfer, S3C2410_RES_ABORT,
				pl330op == PL330_OP_FLUSH ? 1 : 0);
		spin_lock_irqsave(&res_lock, flags);
	}

	/* Flush the whole queue */
	if (pl330op == PL330_OP_FLUSH) {

		if (ch->req[1 - idx].x) {
			xfer = container_of(ch->req[1 - idx].x,
					struct s3c_pl330_xfer, px);

			del_from_queue(xfer);

			ch->req[1 - idx].x = NULL;

			spin_unlock_irqrestore(&res_lock, flags);
			_finish_off(xfer, S3C2410_RES_ABORT, 1);
			spin_lock_irqsave(&res_lock, flags);
		}

		/* Finish off the remaining in the queue */
		xfer = ch->xfer_head;
		while (xfer) {

			del_from_queue(xfer);

			spin_unlock_irqrestore(&res_lock, flags);
			_finish_off(xfer, S3C2410_RES_ABORT, 1);
			spin_lock_irqsave(&res_lock, flags);

			xfer = ch->xfer_head;
		}
	}

ctrl_exit:
	spin_unlock_irqrestore(&res_lock, flags);

	return ret;
}
EXPORT_SYMBOL(s3c2410_dma_ctrl);

int s3c2410_dma_enqueue(enum dma_ch id, void *token,
			dma_addr_t addr, int size)
{
	struct s3c_pl330_chan *ch;
	struct s3c_pl330_xfer *xfer;
	unsigned long flags;
	int idx, ret = 0;

	spin_lock_irqsave(&res_lock, flags);

	ch = id_to_chan(id);

	/* Error if invalid or free channel */
	if (!ch || chan_free(ch)) {
		ret = -EINVAL;
		goto enq_exit;
	}

	/* Error if size is unaligned */
	if (ch->rqcfg.brst_size && size % (1 << ch->rqcfg.brst_size)) {
		ret = -EINVAL;
		goto enq_exit;
	}

	xfer = kmem_cache_alloc(ch->dmac->kmcache, GFP_ATOMIC);
	if (!xfer) {
		ret = -ENOMEM;
		goto enq_exit;
	}

	xfer->token = token;
	xfer->chan = ch;
	xfer->px.bytes = size;
	xfer->px.next = NULL; /* Single request */

	/* For S3C DMA API, direction is always fixed for all xfers */
	if (ch->req[0].rqtype == MEMTODEV) {
		xfer->px.src_addr = addr;
		xfer->px.dst_addr = ch->sdaddr;
	} else {
		xfer->px.src_addr = ch->sdaddr;
		xfer->px.dst_addr = addr;
	}

	add_to_queue(ch, xfer, 0);

	/* Try submitting on either request */
	idx = (ch->lrq == &ch->req[0]) ? 1 : 0;

	if (!ch->req[idx].x)
		s3c_pl330_submit(ch, &ch->req[idx]);
	else
		s3c_pl330_submit(ch, &ch->req[1 - idx]);

	spin_unlock_irqrestore(&res_lock, flags);

	if (ch->options & S3C2410_DMAF_AUTOSTART)
		s3c2410_dma_ctrl(id, S3C2410_DMAOP_START);

	return 0;

enq_exit:
	spin_unlock_irqrestore(&res_lock, flags);

	return ret;
}
EXPORT_SYMBOL(s3c2410_dma_enqueue);

int s3c2410_dma_request(enum dma_ch id,
			struct s3c2410_dma_client *client,
			void *dev)
{
	struct s3c_pl330_dmac *dmac;
	struct s3c_pl330_chan *ch;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&res_lock, flags);

	ch = chan_acquire(id);
	if (!ch) {
		ret = -EBUSY;
		goto req_exit;
	}

	dmac = ch->dmac;

	ch->pl330_chan_id = pl330_request_channel(dmac->pi);
	if (!ch->pl330_chan_id) {
		chan_release(ch);
		ret = -EBUSY;
		goto req_exit;
	}

	ch->client = client;
	ch->options = 0; /* Clear any option */
	ch->callback_fn = NULL; /* Clear any callback */
	ch->lrq = NULL;

	ch->rqcfg.brst_size = 2; /* Default word size */
	ch->rqcfg.swap = SWAP_NO;
	ch->rqcfg.scctl = SCCTRL0; /* Noncacheable and nonbufferable */
	ch->rqcfg.dcctl = DCCTRL0; /* Noncacheable and nonbufferable */
	ch->rqcfg.privileged = 0;
	ch->rqcfg.insnaccess = 0;

	/* Set invalid direction */
	ch->req[0].rqtype = DEVTODEV;
	ch->req[1].rqtype = ch->req[0].rqtype;

	ch->req[0].cfg = &ch->rqcfg;
	ch->req[1].cfg = ch->req[0].cfg;

	ch->req[0].peri = iface_of_dmac(dmac, id) - 1; /* Original index */
	ch->req[1].peri = ch->req[0].peri;

	ch->req[0].token = &ch->req[0];
	ch->req[0].xfer_cb = s3c_pl330_rq0;
	ch->req[1].token = &ch->req[1];
	ch->req[1].xfer_cb = s3c_pl330_rq1;

	ch->req[0].x = NULL;
	ch->req[1].x = NULL;

	/* Reset xfer list */
	INIT_LIST_HEAD(&ch->xfer_list);
	ch->xfer_head = NULL;

req_exit:
	spin_unlock_irqrestore(&res_lock, flags);

	return ret;
}
EXPORT_SYMBOL(s3c2410_dma_request);

int s3c2410_dma_free(enum dma_ch id, struct s3c2410_dma_client *client)
{
	struct s3c_pl330_chan *ch;
	struct s3c_pl330_xfer *xfer;
	unsigned long flags;
	int ret = 0;
	unsigned idx;

	spin_lock_irqsave(&res_lock, flags);

	ch = id_to_chan(id);

	if (!ch || chan_free(ch))
		goto free_exit;

	/* Refuse if someone else wanted to free the channel */
	if (ch->client != client) {
		ret = -EBUSY;
		goto free_exit;
	}

	/* Stop any active xfer, Flushe the queue and do callbacks */
	pl330_chan_ctrl(ch->pl330_chan_id, PL330_OP_FLUSH);

	/* Abort the submitted requests */
	idx = (ch->lrq == &ch->req[0]) ? 1 : 0;

	if (ch->req[idx].x) {
		xfer = container_of(ch->req[idx].x,
				struct s3c_pl330_xfer, px);

		ch->req[idx].x = NULL;
		del_from_queue(xfer);

		spin_unlock_irqrestore(&res_lock, flags);
		_finish_off(xfer, S3C2410_RES_ABORT, 1);
		spin_lock_irqsave(&res_lock, flags);
	}

	if (ch->req[1 - idx].x) {
		xfer = container_of(ch->req[1 - idx].x,
				struct s3c_pl330_xfer, px);

		ch->req[1 - idx].x = NULL;
		del_from_queue(xfer);

		spin_unlock_irqrestore(&res_lock, flags);
		_finish_off(xfer, S3C2410_RES_ABORT, 1);
		spin_lock_irqsave(&res_lock, flags);
	}

	/* Pluck and Abort the queued requests in order */
	do {
		xfer = get_from_queue(ch, 1);

		spin_unlock_irqrestore(&res_lock, flags);
		_finish_off(xfer, S3C2410_RES_ABORT, 1);
		spin_lock_irqsave(&res_lock, flags);
	} while (xfer);

	ch->client = NULL;

	pl330_release_channel(ch->pl330_chan_id);

	ch->pl330_chan_id = NULL;

	chan_release(ch);

free_exit:
	spin_unlock_irqrestore(&res_lock, flags);

	return ret;
}
EXPORT_SYMBOL(s3c2410_dma_free);

int s3c2410_dma_config(enum dma_ch id, int xferunit)
{
	struct s3c_pl330_chan *ch;
	struct pl330_info *pi;
	unsigned long flags;
	int i, dbwidth, ret = 0;

	spin_lock_irqsave(&res_lock, flags);

	ch = id_to_chan(id);

	if (!ch || chan_free(ch)) {
		ret = -EINVAL;
		goto cfg_exit;
	}

	pi = ch->dmac->pi;
	dbwidth = pi->pcfg.data_bus_width / 8;

	/* Max size of xfer can be pcfg.data_bus_width */
	if (xferunit > dbwidth) {
		ret = -EINVAL;
		goto cfg_exit;
	}

	i = 0;
	while (xferunit != (1 << i))
		i++;

	/* If valid value */
	if (xferunit == (1 << i))
		ch->rqcfg.brst_size = i;
	else
		ret = -EINVAL;

cfg_exit:
	spin_unlock_irqrestore(&res_lock, flags);

	return ret;
}
EXPORT_SYMBOL(s3c2410_dma_config);

/* Options that are supported by this driver */
#define S3C_PL330_FLAGS (S3C2410_DMAF_CIRCULAR | S3C2410_DMAF_AUTOSTART)

int s3c2410_dma_setflags(enum dma_ch id, unsigned int options)
{
	struct s3c_pl330_chan *ch;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&res_lock, flags);

	ch = id_to_chan(id);

	if (!ch || chan_free(ch) || options & ~(S3C_PL330_FLAGS))
		ret = -EINVAL;
	else
		ch->options = options;

	spin_unlock_irqrestore(&res_lock, flags);

	return 0;
}
EXPORT_SYMBOL(s3c2410_dma_setflags);

int s3c2410_dma_set_buffdone_fn(enum dma_ch id, s3c2410_dma_cbfn_t rtn)
{
	struct s3c_pl330_chan *ch;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&res_lock, flags);

	ch = id_to_chan(id);

	if (!ch || chan_free(ch))
		ret = -EINVAL;
	else
		ch->callback_fn = rtn;

	spin_unlock_irqrestore(&res_lock, flags);

	return ret;
}
EXPORT_SYMBOL(s3c2410_dma_set_buffdone_fn);

int s3c2410_dma_devconfig(enum dma_ch id, enum s3c2410_dmasrc source,
			  unsigned long address)
{
	struct s3c_pl330_chan *ch;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&res_lock, flags);

	ch = id_to_chan(id);

	if (!ch || chan_free(ch)) {
		ret = -EINVAL;
		goto devcfg_exit;
	}

	switch (source) {
	case S3C2410_DMASRC_HW: /* P->M */
		ch->req[0].rqtype = DEVTOMEM;
		ch->req[1].rqtype = DEVTOMEM;
		ch->rqcfg.src_inc = 0;
		ch->rqcfg.dst_inc = 1;
		break;
	case S3C2410_DMASRC_MEM: /* M->P */
		ch->req[0].rqtype = MEMTODEV;
		ch->req[1].rqtype = MEMTODEV;
		ch->rqcfg.src_inc = 1;
		ch->rqcfg.dst_inc = 0;
		break;
	default:
		ret = -EINVAL;
		goto devcfg_exit;
	}

	ch->sdaddr = address;

devcfg_exit:
	spin_unlock_irqrestore(&res_lock, flags);

	return ret;
}
EXPORT_SYMBOL(s3c2410_dma_devconfig);

int s3c2410_dma_getposition(enum dma_ch id, dma_addr_t *src, dma_addr_t *dst)
{
	struct s3c_pl330_chan *ch = id_to_chan(id);
	struct pl330_chanstatus status;
	int ret;

	if (!ch || chan_free(ch))
		return -EINVAL;

	ret = pl330_chan_status(ch->pl330_chan_id, &status);
	if (ret < 0)
		return ret;

	*src = status.src_addr;
	*dst = status.dst_addr;

	return 0;
}
EXPORT_SYMBOL(s3c2410_dma_getposition);

static irqreturn_t pl330_irq_handler(int irq, void *data)
{
	if (pl330_update(data))
		return IRQ_HANDLED;
	else
		return IRQ_NONE;
}

static int pl330_probe(struct platform_device *pdev)
{
	struct s3c_pl330_dmac *s3c_pl330_dmac;
	struct s3c_pl330_platdata *pl330pd;
	struct pl330_info *pl330_info;
	struct resource *res;
	int i, ret, irq;

	pl330pd = pdev->dev.platform_data;

	/* Can't do without the list of _32_ peripherals */
	if (!pl330pd || !pl330pd->peri) {
		dev_err(&pdev->dev, "platform data missing!\n");
		return -ENODEV;
	}

	pl330_info = kzalloc(sizeof(*pl330_info), GFP_KERNEL);
	if (!pl330_info)
		return -ENOMEM;

	pl330_info->pl330_data = NULL;
	pl330_info->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -ENODEV;
		goto probe_err1;
	}

	request_mem_region(res->start, resource_size(res), pdev->name);

	pl330_info->base = ioremap(res->start, resource_size(res));
	if (!pl330_info->base) {
		ret = -ENXIO;
		goto probe_err2;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto probe_err3;
	}

	ret = request_irq(irq, pl330_irq_handler, 0,
			dev_name(&pdev->dev), pl330_info);
	if (ret)
		goto probe_err4;

	ret = pl330_add(pl330_info);
	if (ret)
		goto probe_err5;

	/* Allocate a new DMAC */
	s3c_pl330_dmac = kmalloc(sizeof(*s3c_pl330_dmac), GFP_KERNEL);
	if (!s3c_pl330_dmac) {
		ret = -ENOMEM;
		goto probe_err6;
	}

	/* Hook the info */
	s3c_pl330_dmac->pi = pl330_info;

	/* No busy channels */
	s3c_pl330_dmac->busy_chan = 0;

	s3c_pl330_dmac->kmcache = kmem_cache_create(dev_name(&pdev->dev),
				sizeof(struct s3c_pl330_xfer), 0, 0, NULL);

	if (!s3c_pl330_dmac->kmcache) {
		ret = -ENOMEM;
		goto probe_err7;
	}

	/* Get the list of peripherals */
	s3c_pl330_dmac->peri = pl330pd->peri;

	/* Attach to the list of DMACs */
	list_add_tail(&s3c_pl330_dmac->node, &dmac_list);

	/* Create a channel for each peripheral in the DMAC
	 * that is, if it doesn't already exist
	 */
	for (i = 0; i < PL330_MAX_PERI; i++)
		if (s3c_pl330_dmac->peri[i] != DMACH_MAX)
			chan_add(s3c_pl330_dmac->peri[i]);

	printk(KERN_INFO
		"Loaded driver for PL330 DMAC-%d %s\n",	pdev->id, pdev->name);
	printk(KERN_INFO
		"\tDBUFF-%ux%ubytes Num_Chans-%u Num_Peri-%u Num_Events-%u\n",
		pl330_info->pcfg.data_buf_dep,
		pl330_info->pcfg.data_bus_width / 8, pl330_info->pcfg.num_chan,
		pl330_info->pcfg.num_peri, pl330_info->pcfg.num_events);

	return 0;

probe_err7:
	kfree(s3c_pl330_dmac);
probe_err6:
	pl330_del(pl330_info);
probe_err5:
	free_irq(irq, pl330_info);
probe_err4:
probe_err3:
	iounmap(pl330_info->base);
probe_err2:
	release_mem_region(res->start, resource_size(res));
probe_err1:
	kfree(pl330_info);

	return ret;
}

static int pl330_remove(struct platform_device *pdev)
{
	struct s3c_pl330_dmac *dmac, *d;
	struct s3c_pl330_chan *ch;
	unsigned long flags;
	int del, found;

	if (!pdev->dev.platform_data)
		return -EINVAL;

	spin_lock_irqsave(&res_lock, flags);

	found = 0;
	list_for_each_entry(d, &dmac_list, node)
		if (d->pi->dev == &pdev->dev) {
			found = 1;
			break;
		}

	if (!found) {
		spin_unlock_irqrestore(&res_lock, flags);
		return 0;
	}

	dmac = d;

	/* Remove all Channels that are managed only by this DMAC */
	list_for_each_entry(ch, &chan_list, node) {

		/* Only channels that are handled by this DMAC */
		if (iface_of_dmac(dmac, ch->id))
			del = 1;
		else
			continue;

		/* Don't remove if some other DMAC has it too */
		list_for_each_entry(d, &dmac_list, node)
			if (d != dmac && iface_of_dmac(d, ch->id)) {
				del = 0;
				break;
			}

		if (del) {
			spin_unlock_irqrestore(&res_lock, flags);
			s3c2410_dma_free(ch->id, ch->client);
			spin_lock_irqsave(&res_lock, flags);
			list_del(&ch->node);
			kfree(ch);
		}
	}

	/* Remove the DMAC */
	list_del(&dmac->node);
	kfree(dmac);

	spin_unlock_irqrestore(&res_lock, flags);

	return 0;
}

static struct platform_driver pl330_driver = {
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "s3c-pl330",
	},
	.probe		= pl330_probe,
	.remove		= pl330_remove,
};

static int __init pl330_init(void)
{
	return platform_driver_register(&pl330_driver);
}
module_init(pl330_init);

static void __exit pl330_exit(void)
{
	platform_driver_unregister(&pl330_driver);
	return;
}
module_exit(pl330_exit);

MODULE_AUTHOR("Jaswinder Singh <jassi.brar@samsung.com>");
MODULE_DESCRIPTION("Driver for PL330 DMA Controller");
MODULE_LICENSE("GPL");
