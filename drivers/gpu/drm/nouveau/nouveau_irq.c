/*
 * Copyright (C) 2006 Ben Skeggs.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

/*
 * Authors:
 *   Ben Skeggs <darktama@iinet.net.au>
 */

#include "drmP.h"
#include "drm.h"
#include "nouveau_drm.h"
#include "nouveau_drv.h"
#include "nouveau_reg.h"
#include <linux/ratelimit.h>

/* needed for hotplug irq */
#include "nouveau_connector.h"
#include "nv50_display.h"

void
nouveau_irq_preinstall(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	/* Master disable */
	nv_wr32(dev, NV03_PMC_INTR_EN_0, 0);

	if (dev_priv->card_type == NV_50) {
		INIT_WORK(&dev_priv->irq_work, nv50_display_irq_handler_bh);
		INIT_LIST_HEAD(&dev_priv->vbl_waiting);
	}
}

int
nouveau_irq_postinstall(struct drm_device *dev)
{
	/* Master enable */
	nv_wr32(dev, NV03_PMC_INTR_EN_0, NV_PMC_INTR_EN_0_MASTER_ENABLE);
	return 0;
}

void
nouveau_irq_uninstall(struct drm_device *dev)
{
	/* Master disable */
	nv_wr32(dev, NV03_PMC_INTR_EN_0, 0);
}

static int
nouveau_call_method(struct nouveau_channel *chan, int class, int mthd, int data)
{
	struct drm_nouveau_private *dev_priv = chan->dev->dev_private;
	struct nouveau_pgraph_object_method *grm;
	struct nouveau_pgraph_object_class *grc;

	grc = dev_priv->engine.graph.grclass;
	while (grc->id) {
		if (grc->id == class)
			break;
		grc++;
	}

	if (grc->id != class || !grc->methods)
		return -ENOENT;

	grm = grc->methods;
	while (grm->id) {
		if (grm->id == mthd)
			return grm->exec(chan, class, mthd, data);
		grm++;
	}

	return -ENOENT;
}

static bool
nouveau_fifo_swmthd(struct nouveau_channel *chan, uint32_t addr, uint32_t data)
{
	struct drm_device *dev = chan->dev;
	const int subc = (addr >> 13) & 0x7;
	const int mthd = addr & 0x1ffc;

	if (mthd == 0x0000) {
		struct nouveau_gpuobj_ref *ref = NULL;

		if (nouveau_gpuobj_ref_find(chan, data, &ref))
			return false;

		if (ref->gpuobj->engine != NVOBJ_ENGINE_SW)
			return false;

		chan->sw_subchannel[subc] = ref->gpuobj->class;
		nv_wr32(dev, NV04_PFIFO_CACHE1_ENGINE, nv_rd32(dev,
			NV04_PFIFO_CACHE1_ENGINE) & ~(0xf << subc * 4));
		return true;
	}

	/* hw object */
	if (nv_rd32(dev, NV04_PFIFO_CACHE1_ENGINE) & (1 << (subc*4)))
		return false;

	if (nouveau_call_method(chan, chan->sw_subchannel[subc], mthd, data))
		return false;

	return true;
}

static void
nouveau_fifo_irq_handler(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_engine *engine = &dev_priv->engine;
	uint32_t status, reassign;
	int cnt = 0;

	reassign = nv_rd32(dev, NV03_PFIFO_CACHES) & 1;
	while ((status = nv_rd32(dev, NV03_PFIFO_INTR_0)) && (cnt++ < 100)) {
		struct nouveau_channel *chan = NULL;
		uint32_t chid, get;

		nv_wr32(dev, NV03_PFIFO_CACHES, 0);

		chid = engine->fifo.channel_id(dev);
		if (chid >= 0 && chid < engine->fifo.channels)
			chan = dev_priv->fifos[chid];
		get  = nv_rd32(dev, NV03_PFIFO_CACHE1_GET);

		if (status & NV_PFIFO_INTR_CACHE_ERROR) {
			uint32_t mthd, data;
			int ptr;

			/* NV_PFIFO_CACHE1_GET actually goes to 0xffc before
			 * wrapping on my G80 chips, but CACHE1 isn't big
			 * enough for this much data.. Tests show that it
			 * wraps around to the start at GET=0x800.. No clue
			 * as to why..
			 */
			ptr = (get & 0x7ff) >> 2;

			if (dev_priv->card_type < NV_40) {
				mthd = nv_rd32(dev,
					NV04_PFIFO_CACHE1_METHOD(ptr));
				data = nv_rd32(dev,
					NV04_PFIFO_CACHE1_DATA(ptr));
			} else {
				mthd = nv_rd32(dev,
					NV40_PFIFO_CACHE1_METHOD(ptr));
				data = nv_rd32(dev,
					NV40_PFIFO_CACHE1_DATA(ptr));
			}

			if (!chan || !nouveau_fifo_swmthd(chan, mthd, data)) {
				NV_INFO(dev, "PFIFO_CACHE_ERROR - Ch %d/%d "
					     "Mthd 0x%04x Data 0x%08x\n",
					chid, (mthd >> 13) & 7, mthd & 0x1ffc,
					data);
			}

			nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_PUSH, 0);
			nv_wr32(dev, NV03_PFIFO_INTR_0,
						NV_PFIFO_INTR_CACHE_ERROR);

			nv_wr32(dev, NV03_PFIFO_CACHE1_PUSH0,
				nv_rd32(dev, NV03_PFIFO_CACHE1_PUSH0) & ~1);
			nv_wr32(dev, NV03_PFIFO_CACHE1_GET, get + 4);
			nv_wr32(dev, NV03_PFIFO_CACHE1_PUSH0,
				nv_rd32(dev, NV03_PFIFO_CACHE1_PUSH0) | 1);
			nv_wr32(dev, NV04_PFIFO_CACHE1_HASH, 0);

			nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_PUSH,
				nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_PUSH) | 1);
			nv_wr32(dev, NV04_PFIFO_CACHE1_PULL0, 1);

			status &= ~NV_PFIFO_INTR_CACHE_ERROR;
		}

		if (status & NV_PFIFO_INTR_DMA_PUSHER) {
			NV_INFO(dev, "PFIFO_DMA_PUSHER - Ch %d\n", chid);

			status &= ~NV_PFIFO_INTR_DMA_PUSHER;
			nv_wr32(dev, NV03_PFIFO_INTR_0,
						NV_PFIFO_INTR_DMA_PUSHER);

			nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_STATE, 0x00000000);
			if (nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_PUT) != get)
				nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_GET,
								get + 4);
		}

		if (status & NV_PFIFO_INTR_SEMAPHORE) {
			uint32_t sem;

			status &= ~NV_PFIFO_INTR_SEMAPHORE;
			nv_wr32(dev, NV03_PFIFO_INTR_0,
				NV_PFIFO_INTR_SEMAPHORE);

			sem = nv_rd32(dev, NV10_PFIFO_CACHE1_SEMAPHORE);
			nv_wr32(dev, NV10_PFIFO_CACHE1_SEMAPHORE, sem | 0x1);

			nv_wr32(dev, NV03_PFIFO_CACHE1_GET, get + 4);
			nv_wr32(dev, NV04_PFIFO_CACHE1_PULL0, 1);
		}

		if (status) {
			NV_INFO(dev, "PFIFO_INTR 0x%08x - Ch %d\n",
				status, chid);
			nv_wr32(dev, NV03_PFIFO_INTR_0, status);
			status = 0;
		}

		nv_wr32(dev, NV03_PFIFO_CACHES, reassign);
	}

	if (status) {
		NV_INFO(dev, "PFIFO still angry after %d spins, halt\n", cnt);
		nv_wr32(dev, 0x2140, 0);
		nv_wr32(dev, 0x140, 0);
	}

	nv_wr32(dev, NV03_PMC_INTR_0, NV_PMC_INTR_0_PFIFO_PENDING);
}

struct nouveau_bitfield_names {
	uint32_t mask;
	const char *name;
};

static struct nouveau_bitfield_names nstatus_names[] =
{
	{ NV04_PGRAPH_NSTATUS_STATE_IN_USE,       "STATE_IN_USE" },
	{ NV04_PGRAPH_NSTATUS_INVALID_STATE,      "INVALID_STATE" },
	{ NV04_PGRAPH_NSTATUS_BAD_ARGUMENT,       "BAD_ARGUMENT" },
	{ NV04_PGRAPH_NSTATUS_PROTECTION_FAULT,   "PROTECTION_FAULT" }
};

static struct nouveau_bitfield_names nstatus_names_nv10[] =
{
	{ NV10_PGRAPH_NSTATUS_STATE_IN_USE,       "STATE_IN_USE" },
	{ NV10_PGRAPH_NSTATUS_INVALID_STATE,      "INVALID_STATE" },
	{ NV10_PGRAPH_NSTATUS_BAD_ARGUMENT,       "BAD_ARGUMENT" },
	{ NV10_PGRAPH_NSTATUS_PROTECTION_FAULT,   "PROTECTION_FAULT" }
};

static struct nouveau_bitfield_names nsource_names[] =
{
	{ NV03_PGRAPH_NSOURCE_NOTIFICATION,       "NOTIFICATION" },
	{ NV03_PGRAPH_NSOURCE_DATA_ERROR,         "DATA_ERROR" },
	{ NV03_PGRAPH_NSOURCE_PROTECTION_ERROR,   "PROTECTION_ERROR" },
	{ NV03_PGRAPH_NSOURCE_RANGE_EXCEPTION,    "RANGE_EXCEPTION" },
	{ NV03_PGRAPH_NSOURCE_LIMIT_COLOR,        "LIMIT_COLOR" },
	{ NV03_PGRAPH_NSOURCE_LIMIT_ZETA,         "LIMIT_ZETA" },
	{ NV03_PGRAPH_NSOURCE_ILLEGAL_MTHD,       "ILLEGAL_MTHD" },
	{ NV03_PGRAPH_NSOURCE_DMA_R_PROTECTION,   "DMA_R_PROTECTION" },
	{ NV03_PGRAPH_NSOURCE_DMA_W_PROTECTION,   "DMA_W_PROTECTION" },
	{ NV03_PGRAPH_NSOURCE_FORMAT_EXCEPTION,   "FORMAT_EXCEPTION" },
	{ NV03_PGRAPH_NSOURCE_PATCH_EXCEPTION,    "PATCH_EXCEPTION" },
	{ NV03_PGRAPH_NSOURCE_STATE_INVALID,      "STATE_INVALID" },
	{ NV03_PGRAPH_NSOURCE_DOUBLE_NOTIFY,      "DOUBLE_NOTIFY" },
	{ NV03_PGRAPH_NSOURCE_NOTIFY_IN_USE,      "NOTIFY_IN_USE" },
	{ NV03_PGRAPH_NSOURCE_METHOD_CNT,         "METHOD_CNT" },
	{ NV03_PGRAPH_NSOURCE_BFR_NOTIFICATION,   "BFR_NOTIFICATION" },
	{ NV03_PGRAPH_NSOURCE_DMA_VTX_PROTECTION, "DMA_VTX_PROTECTION" },
	{ NV03_PGRAPH_NSOURCE_DMA_WIDTH_A,        "DMA_WIDTH_A" },
	{ NV03_PGRAPH_NSOURCE_DMA_WIDTH_B,        "DMA_WIDTH_B" },
};

static void
nouveau_print_bitfield_names_(uint32_t value,
				const struct nouveau_bitfield_names *namelist,
				const int namelist_len)
{
	/*
	 * Caller must have already printed the KERN_* log level for us.
	 * Also the caller is responsible for adding the newline.
	 */
	int i;
	for (i = 0; i < namelist_len; ++i) {
		uint32_t mask = namelist[i].mask;
		if (value & mask) {
			printk(" %s", namelist[i].name);
			value &= ~mask;
		}
	}
	if (value)
		printk(" (unknown bits 0x%08x)", value);
}
#define nouveau_print_bitfield_names(val, namelist) \
	nouveau_print_bitfield_names_((val), (namelist), ARRAY_SIZE(namelist))


static int
nouveau_graph_chid_from_grctx(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t inst;
	int i;

	if (dev_priv->card_type < NV_40)
		return dev_priv->engine.fifo.channels;
	else
	if (dev_priv->card_type < NV_50) {
		inst = (nv_rd32(dev, 0x40032c) & 0xfffff) << 4;

		for (i = 0; i < dev_priv->engine.fifo.channels; i++) {
			struct nouveau_channel *chan = dev_priv->fifos[i];

			if (!chan || !chan->ramin_grctx)
				continue;

			if (inst == chan->ramin_grctx->instance)
				break;
		}
	} else {
		inst = (nv_rd32(dev, 0x40032c) & 0xfffff) << 12;

		for (i = 0; i < dev_priv->engine.fifo.channels; i++) {
			struct nouveau_channel *chan = dev_priv->fifos[i];

			if (!chan || !chan->ramin)
				continue;

			if (inst == chan->ramin->instance)
				break;
		}
	}


	return i;
}

static int
nouveau_graph_trapped_channel(struct drm_device *dev, int *channel_ret)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_engine *engine = &dev_priv->engine;
	int channel;

	if (dev_priv->card_type < NV_10)
		channel = (nv_rd32(dev, NV04_PGRAPH_TRAPPED_ADDR) >> 24) & 0xf;
	else
	if (dev_priv->card_type < NV_40)
		channel = (nv_rd32(dev, NV04_PGRAPH_TRAPPED_ADDR) >> 20) & 0x1f;
	else
		channel = nouveau_graph_chid_from_grctx(dev);

	if (channel >= engine->fifo.channels || !dev_priv->fifos[channel]) {
		NV_ERROR(dev, "AIII, invalid/inactive channel id %d\n", channel);
		return -EINVAL;
	}

	*channel_ret = channel;
	return 0;
}

struct nouveau_pgraph_trap {
	int channel;
	int class;
	int subc, mthd, size;
	uint32_t data, data2;
	uint32_t nsource, nstatus;
};

static void
nouveau_graph_trap_info(struct drm_device *dev,
			struct nouveau_pgraph_trap *trap)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t address;

	trap->nsource = trap->nstatus = 0;
	if (dev_priv->card_type < NV_50) {
		trap->nsource = nv_rd32(dev, NV03_PGRAPH_NSOURCE);
		trap->nstatus = nv_rd32(dev, NV03_PGRAPH_NSTATUS);
	}

	if (nouveau_graph_trapped_channel(dev, &trap->channel))
		trap->channel = -1;
	address = nv_rd32(dev, NV04_PGRAPH_TRAPPED_ADDR);

	trap->mthd = address & 0x1FFC;
	trap->data = nv_rd32(dev, NV04_PGRAPH_TRAPPED_DATA);
	if (dev_priv->card_type < NV_10) {
		trap->subc  = (address >> 13) & 0x7;
	} else {
		trap->subc  = (address >> 16) & 0x7;
		trap->data2 = nv_rd32(dev, NV10_PGRAPH_TRAPPED_DATA_HIGH);
	}

	if (dev_priv->card_type < NV_10)
		trap->class = nv_rd32(dev, 0x400180 + trap->subc*4) & 0xFF;
	else if (dev_priv->card_type < NV_40)
		trap->class = nv_rd32(dev, 0x400160 + trap->subc*4) & 0xFFF;
	else if (dev_priv->card_type < NV_50)
		trap->class = nv_rd32(dev, 0x400160 + trap->subc*4) & 0xFFFF;
	else
		trap->class = nv_rd32(dev, 0x400814);
}

static void
nouveau_graph_dump_trap_info(struct drm_device *dev, const char *id,
			     struct nouveau_pgraph_trap *trap)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t nsource = trap->nsource, nstatus = trap->nstatus;

	NV_INFO(dev, "%s - nSource:", id);
	nouveau_print_bitfield_names(nsource, nsource_names);
	printk(", nStatus:");
	if (dev_priv->card_type < NV_10)
		nouveau_print_bitfield_names(nstatus, nstatus_names);
	else
		nouveau_print_bitfield_names(nstatus, nstatus_names_nv10);
	printk("\n");

	NV_INFO(dev, "%s - Ch %d/%d Class 0x%04x Mthd 0x%04x "
					"Data 0x%08x:0x%08x\n",
					id, trap->channel, trap->subc,
					trap->class, trap->mthd,
					trap->data2, trap->data);
}

static int
nouveau_pgraph_intr_swmthd(struct drm_device *dev,
			   struct nouveau_pgraph_trap *trap)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (trap->channel < 0 ||
	    trap->channel >= dev_priv->engine.fifo.channels ||
	    !dev_priv->fifos[trap->channel])
		return -ENODEV;

	return nouveau_call_method(dev_priv->fifos[trap->channel],
				   trap->class, trap->mthd, trap->data);
}

static inline void
nouveau_pgraph_intr_notify(struct drm_device *dev, uint32_t nsource)
{
	struct nouveau_pgraph_trap trap;
	int unhandled = 0;

	nouveau_graph_trap_info(dev, &trap);

	if (nsource & NV03_PGRAPH_NSOURCE_ILLEGAL_MTHD) {
		if (nouveau_pgraph_intr_swmthd(dev, &trap))
			unhandled = 1;
	} else {
		unhandled = 1;
	}

	if (unhandled)
		nouveau_graph_dump_trap_info(dev, "PGRAPH_NOTIFY", &trap);
}

static DEFINE_RATELIMIT_STATE(nouveau_ratelimit_state, 3 * HZ, 20);

static int nouveau_ratelimit(void)
{
	return __ratelimit(&nouveau_ratelimit_state);
}


static inline void
nouveau_pgraph_intr_error(struct drm_device *dev, uint32_t nsource)
{
	struct nouveau_pgraph_trap trap;
	int unhandled = 0;

	nouveau_graph_trap_info(dev, &trap);
	trap.nsource = nsource;

	if (nsource & NV03_PGRAPH_NSOURCE_ILLEGAL_MTHD) {
		if (nouveau_pgraph_intr_swmthd(dev, &trap))
			unhandled = 1;
	} else if (nsource & NV03_PGRAPH_NSOURCE_DMA_VTX_PROTECTION) {
		uint32_t v = nv_rd32(dev, 0x402000);
		nv_wr32(dev, 0x402000, v);

		/* dump the error anyway for now: it's useful for
		   Gallium development */
		unhandled = 1;
	} else {
		unhandled = 1;
	}

	if (unhandled && nouveau_ratelimit())
		nouveau_graph_dump_trap_info(dev, "PGRAPH_ERROR", &trap);
}

static inline void
nouveau_pgraph_intr_context_switch(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_engine *engine = &dev_priv->engine;
	uint32_t chid;

	chid = engine->fifo.channel_id(dev);
	NV_DEBUG(dev, "PGRAPH context switch interrupt channel %x\n", chid);

	switch (dev_priv->card_type) {
	case NV_04:
		nv04_graph_context_switch(dev);
		break;
	case NV_10:
		nv10_graph_context_switch(dev);
		break;
	default:
		NV_ERROR(dev, "Context switch not implemented\n");
		break;
	}
}

static void
nouveau_pgraph_irq_handler(struct drm_device *dev)
{
	uint32_t status;

	while ((status = nv_rd32(dev, NV03_PGRAPH_INTR))) {
		uint32_t nsource = nv_rd32(dev, NV03_PGRAPH_NSOURCE);

		if (status & NV_PGRAPH_INTR_NOTIFY) {
			nouveau_pgraph_intr_notify(dev, nsource);

			status &= ~NV_PGRAPH_INTR_NOTIFY;
			nv_wr32(dev, NV03_PGRAPH_INTR, NV_PGRAPH_INTR_NOTIFY);
		}

		if (status & NV_PGRAPH_INTR_ERROR) {
			nouveau_pgraph_intr_error(dev, nsource);

			status &= ~NV_PGRAPH_INTR_ERROR;
			nv_wr32(dev, NV03_PGRAPH_INTR, NV_PGRAPH_INTR_ERROR);
		}

		if (status & NV_PGRAPH_INTR_CONTEXT_SWITCH) {
			nouveau_pgraph_intr_context_switch(dev);

			status &= ~NV_PGRAPH_INTR_CONTEXT_SWITCH;
			nv_wr32(dev, NV03_PGRAPH_INTR,
				 NV_PGRAPH_INTR_CONTEXT_SWITCH);
		}

		if (status) {
			NV_INFO(dev, "Unhandled PGRAPH_INTR - 0x%08x\n", status);
			nv_wr32(dev, NV03_PGRAPH_INTR, status);
		}

		if ((nv_rd32(dev, NV04_PGRAPH_FIFO) & (1 << 0)) == 0)
			nv_wr32(dev, NV04_PGRAPH_FIFO, 1);
	}

	nv_wr32(dev, NV03_PMC_INTR_0, NV_PMC_INTR_0_PGRAPH_PENDING);
}

static void
nv50_pgraph_irq_handler(struct drm_device *dev)
{
	uint32_t status;

	while ((status = nv_rd32(dev, NV03_PGRAPH_INTR))) {
		uint32_t nsource = nv_rd32(dev, NV03_PGRAPH_NSOURCE);

		if (status & 0x00000001) {
			nouveau_pgraph_intr_notify(dev, nsource);
			status &= ~0x00000001;
			nv_wr32(dev, NV03_PGRAPH_INTR, 0x00000001);
		}

		if (status & 0x00000010) {
			nouveau_pgraph_intr_error(dev, nsource |
					NV03_PGRAPH_NSOURCE_ILLEGAL_MTHD);

			status &= ~0x00000010;
			nv_wr32(dev, NV03_PGRAPH_INTR, 0x00000010);
		}

		if (status & 0x00001000) {
			nv_wr32(dev, 0x400500, 0x00000000);
			nv_wr32(dev, NV03_PGRAPH_INTR,
				NV_PGRAPH_INTR_CONTEXT_SWITCH);
			nv_wr32(dev, NV40_PGRAPH_INTR_EN, nv_rd32(dev,
				NV40_PGRAPH_INTR_EN) &
				~NV_PGRAPH_INTR_CONTEXT_SWITCH);
			nv_wr32(dev, 0x400500, 0x00010001);

			nv50_graph_context_switch(dev);

			status &= ~NV_PGRAPH_INTR_CONTEXT_SWITCH;
		}

		if (status & 0x00100000) {
			nouveau_pgraph_intr_error(dev, nsource |
					NV03_PGRAPH_NSOURCE_DATA_ERROR);

			status &= ~0x00100000;
			nv_wr32(dev, NV03_PGRAPH_INTR, 0x00100000);
		}

		if (status & 0x00200000) {
			int r;

			nouveau_pgraph_intr_error(dev, nsource |
					NV03_PGRAPH_NSOURCE_PROTECTION_ERROR);

			NV_ERROR(dev, "magic set 1:\n");
			for (r = 0x408900; r <= 0x408910; r += 4)
				NV_ERROR(dev, "\t0x%08x: 0x%08x\n", r,
					nv_rd32(dev, r));
			nv_wr32(dev, 0x408900,
				nv_rd32(dev, 0x408904) | 0xc0000000);
			for (r = 0x408e08; r <= 0x408e24; r += 4)
				NV_ERROR(dev, "\t0x%08x: 0x%08x\n", r,
							nv_rd32(dev, r));
			nv_wr32(dev, 0x408e08,
				nv_rd32(dev, 0x408e08) | 0xc0000000);

			NV_ERROR(dev, "magic set 2:\n");
			for (r = 0x409900; r <= 0x409910; r += 4)
				NV_ERROR(dev, "\t0x%08x: 0x%08x\n", r,
					nv_rd32(dev, r));
			nv_wr32(dev, 0x409900,
				nv_rd32(dev, 0x409904) | 0xc0000000);
			for (r = 0x409e08; r <= 0x409e24; r += 4)
				NV_ERROR(dev, "\t0x%08x: 0x%08x\n", r,
					nv_rd32(dev, r));
			nv_wr32(dev, 0x409e08,
				nv_rd32(dev, 0x409e08) | 0xc0000000);

			status &= ~0x00200000;
			nv_wr32(dev, NV03_PGRAPH_NSOURCE, nsource);
			nv_wr32(dev, NV03_PGRAPH_INTR, 0x00200000);
		}

		if (status) {
			NV_INFO(dev, "Unhandled PGRAPH_INTR - 0x%08x\n",
				status);
			nv_wr32(dev, NV03_PGRAPH_INTR, status);
		}

		{
			const int isb = (1 << 16) | (1 << 0);

			if ((nv_rd32(dev, 0x400500) & isb) != isb)
				nv_wr32(dev, 0x400500,
					nv_rd32(dev, 0x400500) | isb);
		}
	}

	nv_wr32(dev, NV03_PMC_INTR_0, NV_PMC_INTR_0_PGRAPH_PENDING);
	nv_wr32(dev, 0x400824, nv_rd32(dev, 0x400824) & ~(1 << 31));
}

static void
nouveau_crtc_irq_handler(struct drm_device *dev, int crtc)
{
	if (crtc & 1)
		nv_wr32(dev, NV_CRTC0_INTSTAT, NV_CRTC_INTR_VBLANK);

	if (crtc & 2)
		nv_wr32(dev, NV_CRTC1_INTSTAT, NV_CRTC_INTR_VBLANK);
}

irqreturn_t
nouveau_irq_handler(DRM_IRQ_ARGS)
{
	struct drm_device *dev = (struct drm_device *)arg;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t status, fbdev_flags = 0;

	status = nv_rd32(dev, NV03_PMC_INTR_0);
	if (!status)
		return IRQ_NONE;

	if (dev_priv->fbdev_info) {
		fbdev_flags = dev_priv->fbdev_info->flags;
		dev_priv->fbdev_info->flags |= FBINFO_HWACCEL_DISABLED;
	}

	if (status & NV_PMC_INTR_0_PFIFO_PENDING) {
		nouveau_fifo_irq_handler(dev);
		status &= ~NV_PMC_INTR_0_PFIFO_PENDING;
	}

	if (status & NV_PMC_INTR_0_PGRAPH_PENDING) {
		if (dev_priv->card_type >= NV_50)
			nv50_pgraph_irq_handler(dev);
		else
			nouveau_pgraph_irq_handler(dev);

		status &= ~NV_PMC_INTR_0_PGRAPH_PENDING;
	}

	if (status & NV_PMC_INTR_0_CRTCn_PENDING) {
		nouveau_crtc_irq_handler(dev, (status>>24)&3);
		status &= ~NV_PMC_INTR_0_CRTCn_PENDING;
	}

	if (status & (NV_PMC_INTR_0_NV50_DISPLAY_PENDING |
		      NV_PMC_INTR_0_NV50_I2C_PENDING)) {
		nv50_display_irq_handler(dev);
		status &= ~(NV_PMC_INTR_0_NV50_DISPLAY_PENDING |
			    NV_PMC_INTR_0_NV50_I2C_PENDING);
	}

	if (status)
		NV_ERROR(dev, "Unhandled PMC INTR status bits 0x%08x\n", status);

	if (dev_priv->fbdev_info)
		dev_priv->fbdev_info->flags = fbdev_flags;

	return IRQ_HANDLED;
}
