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
#include "nouveau_ramht.h"
#include <linux/ratelimit.h>

/* needed for hotplug irq */
#include "nouveau_connector.h"
#include "nv50_display.h"

static DEFINE_RATELIMIT_STATE(nouveau_ratelimit_state, 3 * HZ, 20);

static int nouveau_ratelimit(void)
{
	return __ratelimit(&nouveau_ratelimit_state);
}

void
nouveau_irq_preinstall(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	/* Master disable */
	nv_wr32(dev, NV03_PMC_INTR_EN_0, 0);

	if (dev_priv->card_type >= NV_50) {
		INIT_WORK(&dev_priv->irq_work, nv50_display_irq_handler_bh);
		INIT_WORK(&dev_priv->hpd_work, nv50_display_irq_hotplug_bh);
		spin_lock_init(&dev_priv->hpd_state.lock);
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
		struct nouveau_gpuobj *gpuobj;

		gpuobj = nouveau_ramht_find(chan, data);
		if (!gpuobj)
			return false;

		if (gpuobj->engine != NVOBJ_ENGINE_SW)
			return false;

		chan->sw_subchannel[subc] = gpuobj->class;
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
			u32 dma_get = nv_rd32(dev, 0x003244);
			u32 dma_put = nv_rd32(dev, 0x003240);
			u32 push = nv_rd32(dev, 0x003220);
			u32 state = nv_rd32(dev, 0x003228);

			if (dev_priv->card_type == NV_50) {
				u32 ho_get = nv_rd32(dev, 0x003328);
				u32 ho_put = nv_rd32(dev, 0x003320);
				u32 ib_get = nv_rd32(dev, 0x003334);
				u32 ib_put = nv_rd32(dev, 0x003330);

				if (nouveau_ratelimit())
					NV_INFO(dev, "PFIFO_DMA_PUSHER - Ch %d Get 0x%02x%08x "
					     "Put 0x%02x%08x IbGet 0x%08x IbPut 0x%08x "
					     "State 0x%08x Push 0x%08x\n",
						chid, ho_get, dma_get, ho_put,
						dma_put, ib_get, ib_put, state,
						push);

				/* METHOD_COUNT, in DMA_STATE on earlier chipsets */
				nv_wr32(dev, 0x003364, 0x00000000);
				if (dma_get != dma_put || ho_get != ho_put) {
					nv_wr32(dev, 0x003244, dma_put);
					nv_wr32(dev, 0x003328, ho_put);
				} else
				if (ib_get != ib_put) {
					nv_wr32(dev, 0x003334, ib_put);
				}
			} else {
				NV_INFO(dev, "PFIFO_DMA_PUSHER - Ch %d Get 0x%08x "
					     "Put 0x%08x State 0x%08x Push 0x%08x\n",
					chid, dma_get, dma_put, state, push);

				if (dma_get != dma_put)
					nv_wr32(dev, 0x003244, dma_put);
			}

			nv_wr32(dev, 0x003228, 0x00000000);
			nv_wr32(dev, 0x003220, 0x00000001);
			nv_wr32(dev, 0x002100, NV_PFIFO_INTR_DMA_PUSHER);
			status &= ~NV_PFIFO_INTR_DMA_PUSHER;
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

		if (dev_priv->card_type == NV_50) {
			if (status & 0x00000010) {
				nv50_fb_vm_trap(dev, 1, "PFIFO_BAR_FAULT");
				status &= ~0x00000010;
				nv_wr32(dev, 0x002100, 0x00000010);
			}
		}

		if (status) {
			if (nouveau_ratelimit())
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

struct nouveau_enum_names {
	uint32_t value;
	const char *name;
};

static void
nouveau_print_enum_names_(uint32_t value,
				const struct nouveau_enum_names *namelist,
				const int namelist_len)
{
	/*
	 * Caller must have already printed the KERN_* log level for us.
	 * Also the caller is responsible for adding the newline.
	 */
	int i;
	for (i = 0; i < namelist_len; ++i) {
		if (value == namelist[i].value) {
			printk("%s", namelist[i].name);
			return;
		}
	}
	printk("unknown value 0x%08x", value);
}
#define nouveau_print_enum_names(val, namelist) \
	nouveau_print_enum_names_((val), (namelist), ARRAY_SIZE(namelist))

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

			if (inst == chan->ramin_grctx->pinst)
				break;
		}
	} else {
		inst = (nv_rd32(dev, 0x40032c) & 0xfffff) << 12;

		for (i = 0; i < dev_priv->engine.fifo.channels; i++) {
			struct nouveau_channel *chan = dev_priv->fifos[i];

			if (!chan || !chan->ramin)
				continue;

			if (inst == chan->ramin->vinst)
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

	if (dev_priv->card_type < NV_50) {
		NV_INFO(dev, "%s - nSource:", id);
		nouveau_print_bitfield_names(nsource, nsource_names);
		printk(", nStatus:");
		if (dev_priv->card_type < NV_10)
			nouveau_print_bitfield_names(nstatus, nstatus_names);
		else
			nouveau_print_bitfield_names(nstatus, nstatus_names_nv10);
		printk("\n");
	}

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
			status &= ~NV_PGRAPH_INTR_CONTEXT_SWITCH;
			nv_wr32(dev, NV03_PGRAPH_INTR,
				 NV_PGRAPH_INTR_CONTEXT_SWITCH);

			nouveau_pgraph_intr_context_switch(dev);
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

static struct nouveau_enum_names nv50_mp_exec_error_names[] =
{
	{ 3, "STACK_UNDERFLOW" },
	{ 4, "QUADON_ACTIVE" },
	{ 8, "TIMEOUT" },
	{ 0x10, "INVALID_OPCODE" },
	{ 0x40, "BREAKPOINT" },
};

static void
nv50_pgraph_mp_trap(struct drm_device *dev, int tpid, int display)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t units = nv_rd32(dev, 0x1540);
	uint32_t addr, mp10, status, pc, oplow, ophigh;
	int i;
	int mps = 0;
	for (i = 0; i < 4; i++) {
		if (!(units & 1 << (i+24)))
			continue;
		if (dev_priv->chipset < 0xa0)
			addr = 0x408200 + (tpid << 12) + (i << 7);
		else
			addr = 0x408100 + (tpid << 11) + (i << 7);
		mp10 = nv_rd32(dev, addr + 0x10);
		status = nv_rd32(dev, addr + 0x14);
		if (!status)
			continue;
		if (display) {
			nv_rd32(dev, addr + 0x20);
			pc = nv_rd32(dev, addr + 0x24);
			oplow = nv_rd32(dev, addr + 0x70);
			ophigh= nv_rd32(dev, addr + 0x74);
			NV_INFO(dev, "PGRAPH_TRAP_MP_EXEC - "
					"TP %d MP %d: ", tpid, i);
			nouveau_print_enum_names(status,
					nv50_mp_exec_error_names);
			printk(" at %06x warp %d, opcode %08x %08x\n",
					pc&0xffffff, pc >> 24,
					oplow, ophigh);
		}
		nv_wr32(dev, addr + 0x10, mp10);
		nv_wr32(dev, addr + 0x14, 0);
		mps++;
	}
	if (!mps && display)
		NV_INFO(dev, "PGRAPH_TRAP_MP_EXEC - TP %d: "
				"No MPs claiming errors?\n", tpid);
}

static void
nv50_pgraph_tp_trap(struct drm_device *dev, int type, uint32_t ustatus_old,
		uint32_t ustatus_new, int display, const char *name)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int tps = 0;
	uint32_t units = nv_rd32(dev, 0x1540);
	int i, r;
	uint32_t ustatus_addr, ustatus;
	for (i = 0; i < 16; i++) {
		if (!(units & (1 << i)))
			continue;
		if (dev_priv->chipset < 0xa0)
			ustatus_addr = ustatus_old + (i << 12);
		else
			ustatus_addr = ustatus_new + (i << 11);
		ustatus = nv_rd32(dev, ustatus_addr) & 0x7fffffff;
		if (!ustatus)
			continue;
		tps++;
		switch (type) {
		case 6: /* texture error... unknown for now */
			nv50_fb_vm_trap(dev, display, name);
			if (display) {
				NV_ERROR(dev, "magic set %d:\n", i);
				for (r = ustatus_addr + 4; r <= ustatus_addr + 0x10; r += 4)
					NV_ERROR(dev, "\t0x%08x: 0x%08x\n", r,
						nv_rd32(dev, r));
			}
			break;
		case 7: /* MP error */
			if (ustatus & 0x00010000) {
				nv50_pgraph_mp_trap(dev, i, display);
				ustatus &= ~0x00010000;
			}
			break;
		case 8: /* TPDMA error */
			{
			uint32_t e0c = nv_rd32(dev, ustatus_addr + 4);
			uint32_t e10 = nv_rd32(dev, ustatus_addr + 8);
			uint32_t e14 = nv_rd32(dev, ustatus_addr + 0xc);
			uint32_t e18 = nv_rd32(dev, ustatus_addr + 0x10);
			uint32_t e1c = nv_rd32(dev, ustatus_addr + 0x14);
			uint32_t e20 = nv_rd32(dev, ustatus_addr + 0x18);
			uint32_t e24 = nv_rd32(dev, ustatus_addr + 0x1c);
			nv50_fb_vm_trap(dev, display, name);
			/* 2d engine destination */
			if (ustatus & 0x00000010) {
				if (display) {
					NV_INFO(dev, "PGRAPH_TRAP_TPDMA_2D - TP %d - Unknown fault at address %02x%08x\n",
							i, e14, e10);
					NV_INFO(dev, "PGRAPH_TRAP_TPDMA_2D - TP %d - e0c: %08x, e18: %08x, e1c: %08x, e20: %08x, e24: %08x\n",
							i, e0c, e18, e1c, e20, e24);
				}
				ustatus &= ~0x00000010;
			}
			/* Render target */
			if (ustatus & 0x00000040) {
				if (display) {
					NV_INFO(dev, "PGRAPH_TRAP_TPDMA_RT - TP %d - Unknown fault at address %02x%08x\n",
							i, e14, e10);
					NV_INFO(dev, "PGRAPH_TRAP_TPDMA_RT - TP %d - e0c: %08x, e18: %08x, e1c: %08x, e20: %08x, e24: %08x\n",
							i, e0c, e18, e1c, e20, e24);
				}
				ustatus &= ~0x00000040;
			}
			/* CUDA memory: l[], g[] or stack. */
			if (ustatus & 0x00000080) {
				if (display) {
					if (e18 & 0x80000000) {
						/* g[] read fault? */
						NV_INFO(dev, "PGRAPH_TRAP_TPDMA - TP %d - Global read fault at address %02x%08x\n",
								i, e14, e10 | ((e18 >> 24) & 0x1f));
						e18 &= ~0x1f000000;
					} else if (e18 & 0xc) {
						/* g[] write fault? */
						NV_INFO(dev, "PGRAPH_TRAP_TPDMA - TP %d - Global write fault at address %02x%08x\n",
								i, e14, e10 | ((e18 >> 7) & 0x1f));
						e18 &= ~0x00000f80;
					} else {
						NV_INFO(dev, "PGRAPH_TRAP_TPDMA - TP %d - Unknown CUDA fault at address %02x%08x\n",
								i, e14, e10);
					}
					NV_INFO(dev, "PGRAPH_TRAP_TPDMA - TP %d - e0c: %08x, e18: %08x, e1c: %08x, e20: %08x, e24: %08x\n",
							i, e0c, e18, e1c, e20, e24);
				}
				ustatus &= ~0x00000080;
			}
			}
			break;
		}
		if (ustatus) {
			if (display)
				NV_INFO(dev, "%s - TP%d: Unhandled ustatus 0x%08x\n", name, i, ustatus);
		}
		nv_wr32(dev, ustatus_addr, 0xc0000000);
	}

	if (!tps && display)
		NV_INFO(dev, "%s - No TPs claiming errors?\n", name);
}

static void
nv50_pgraph_trap_handler(struct drm_device *dev)
{
	struct nouveau_pgraph_trap trap;
	uint32_t status = nv_rd32(dev, 0x400108);
	uint32_t ustatus;
	int display = nouveau_ratelimit();


	if (!status && display) {
		nouveau_graph_trap_info(dev, &trap);
		nouveau_graph_dump_trap_info(dev, "PGRAPH_TRAP", &trap);
		NV_INFO(dev, "PGRAPH_TRAP - no units reporting traps?\n");
	}

	/* DISPATCH: Relays commands to other units and handles NOTIFY,
	 * COND, QUERY. If you get a trap from it, the command is still stuck
	 * in DISPATCH and you need to do something about it. */
	if (status & 0x001) {
		ustatus = nv_rd32(dev, 0x400804) & 0x7fffffff;
		if (!ustatus && display) {
			NV_INFO(dev, "PGRAPH_TRAP_DISPATCH - no ustatus?\n");
		}

		/* Known to be triggered by screwed up NOTIFY and COND... */
		if (ustatus & 0x00000001) {
			nv50_fb_vm_trap(dev, display, "PGRAPH_TRAP_DISPATCH_FAULT");
			nv_wr32(dev, 0x400500, 0);
			if (nv_rd32(dev, 0x400808) & 0x80000000) {
				if (display) {
					if (nouveau_graph_trapped_channel(dev, &trap.channel))
						trap.channel = -1;
					trap.class = nv_rd32(dev, 0x400814);
					trap.mthd = nv_rd32(dev, 0x400808) & 0x1ffc;
					trap.subc = (nv_rd32(dev, 0x400808) >> 16) & 0x7;
					trap.data = nv_rd32(dev, 0x40080c);
					trap.data2 = nv_rd32(dev, 0x400810);
					nouveau_graph_dump_trap_info(dev,
							"PGRAPH_TRAP_DISPATCH_FAULT", &trap);
					NV_INFO(dev, "PGRAPH_TRAP_DISPATCH_FAULT - 400808: %08x\n", nv_rd32(dev, 0x400808));
					NV_INFO(dev, "PGRAPH_TRAP_DISPATCH_FAULT - 400848: %08x\n", nv_rd32(dev, 0x400848));
				}
				nv_wr32(dev, 0x400808, 0);
			} else if (display) {
				NV_INFO(dev, "PGRAPH_TRAP_DISPATCH_FAULT - No stuck command?\n");
			}
			nv_wr32(dev, 0x4008e8, nv_rd32(dev, 0x4008e8) & 3);
			nv_wr32(dev, 0x400848, 0);
			ustatus &= ~0x00000001;
		}
		if (ustatus & 0x00000002) {
			nv50_fb_vm_trap(dev, display, "PGRAPH_TRAP_DISPATCH_QUERY");
			nv_wr32(dev, 0x400500, 0);
			if (nv_rd32(dev, 0x40084c) & 0x80000000) {
				if (display) {
					if (nouveau_graph_trapped_channel(dev, &trap.channel))
						trap.channel = -1;
					trap.class = nv_rd32(dev, 0x400814);
					trap.mthd = nv_rd32(dev, 0x40084c) & 0x1ffc;
					trap.subc = (nv_rd32(dev, 0x40084c) >> 16) & 0x7;
					trap.data = nv_rd32(dev, 0x40085c);
					trap.data2 = 0;
					nouveau_graph_dump_trap_info(dev,
							"PGRAPH_TRAP_DISPATCH_QUERY", &trap);
					NV_INFO(dev, "PGRAPH_TRAP_DISPATCH_QUERY - 40084c: %08x\n", nv_rd32(dev, 0x40084c));
				}
				nv_wr32(dev, 0x40084c, 0);
			} else if (display) {
				NV_INFO(dev, "PGRAPH_TRAP_DISPATCH_QUERY - No stuck command?\n");
			}
			ustatus &= ~0x00000002;
		}
		if (ustatus && display)
			NV_INFO(dev, "PGRAPH_TRAP_DISPATCH - Unhandled ustatus 0x%08x\n", ustatus);
		nv_wr32(dev, 0x400804, 0xc0000000);
		nv_wr32(dev, 0x400108, 0x001);
		status &= ~0x001;
	}

	/* TRAPs other than dispatch use the "normal" trap regs. */
	if (status && display) {
		nouveau_graph_trap_info(dev, &trap);
		nouveau_graph_dump_trap_info(dev,
				"PGRAPH_TRAP", &trap);
	}

	/* M2MF: Memory to memory copy engine. */
	if (status & 0x002) {
		ustatus = nv_rd32(dev, 0x406800) & 0x7fffffff;
		if (!ustatus && display) {
			NV_INFO(dev, "PGRAPH_TRAP_M2MF - no ustatus?\n");
		}
		if (ustatus & 0x00000001) {
			nv50_fb_vm_trap(dev, display, "PGRAPH_TRAP_M2MF_NOTIFY");
			ustatus &= ~0x00000001;
		}
		if (ustatus & 0x00000002) {
			nv50_fb_vm_trap(dev, display, "PGRAPH_TRAP_M2MF_IN");
			ustatus &= ~0x00000002;
		}
		if (ustatus & 0x00000004) {
			nv50_fb_vm_trap(dev, display, "PGRAPH_TRAP_M2MF_OUT");
			ustatus &= ~0x00000004;
		}
		NV_INFO (dev, "PGRAPH_TRAP_M2MF - %08x %08x %08x %08x\n",
				nv_rd32(dev, 0x406804),
				nv_rd32(dev, 0x406808),
				nv_rd32(dev, 0x40680c),
				nv_rd32(dev, 0x406810));
		if (ustatus && display)
			NV_INFO(dev, "PGRAPH_TRAP_M2MF - Unhandled ustatus 0x%08x\n", ustatus);
		/* No sane way found yet -- just reset the bugger. */
		nv_wr32(dev, 0x400040, 2);
		nv_wr32(dev, 0x400040, 0);
		nv_wr32(dev, 0x406800, 0xc0000000);
		nv_wr32(dev, 0x400108, 0x002);
		status &= ~0x002;
	}

	/* VFETCH: Fetches data from vertex buffers. */
	if (status & 0x004) {
		ustatus = nv_rd32(dev, 0x400c04) & 0x7fffffff;
		if (!ustatus && display) {
			NV_INFO(dev, "PGRAPH_TRAP_VFETCH - no ustatus?\n");
		}
		if (ustatus & 0x00000001) {
			nv50_fb_vm_trap(dev, display, "PGRAPH_TRAP_VFETCH_FAULT");
			NV_INFO (dev, "PGRAPH_TRAP_VFETCH_FAULT - %08x %08x %08x %08x\n",
					nv_rd32(dev, 0x400c00),
					nv_rd32(dev, 0x400c08),
					nv_rd32(dev, 0x400c0c),
					nv_rd32(dev, 0x400c10));
			ustatus &= ~0x00000001;
		}
		if (ustatus && display)
			NV_INFO(dev, "PGRAPH_TRAP_VFETCH - Unhandled ustatus 0x%08x\n", ustatus);
		nv_wr32(dev, 0x400c04, 0xc0000000);
		nv_wr32(dev, 0x400108, 0x004);
		status &= ~0x004;
	}

	/* STRMOUT: DirectX streamout / OpenGL transform feedback. */
	if (status & 0x008) {
		ustatus = nv_rd32(dev, 0x401800) & 0x7fffffff;
		if (!ustatus && display) {
			NV_INFO(dev, "PGRAPH_TRAP_STRMOUT - no ustatus?\n");
		}
		if (ustatus & 0x00000001) {
			nv50_fb_vm_trap(dev, display, "PGRAPH_TRAP_STRMOUT_FAULT");
			NV_INFO (dev, "PGRAPH_TRAP_STRMOUT_FAULT - %08x %08x %08x %08x\n",
					nv_rd32(dev, 0x401804),
					nv_rd32(dev, 0x401808),
					nv_rd32(dev, 0x40180c),
					nv_rd32(dev, 0x401810));
			ustatus &= ~0x00000001;
		}
		if (ustatus && display)
			NV_INFO(dev, "PGRAPH_TRAP_STRMOUT - Unhandled ustatus 0x%08x\n", ustatus);
		/* No sane way found yet -- just reset the bugger. */
		nv_wr32(dev, 0x400040, 0x80);
		nv_wr32(dev, 0x400040, 0);
		nv_wr32(dev, 0x401800, 0xc0000000);
		nv_wr32(dev, 0x400108, 0x008);
		status &= ~0x008;
	}

	/* CCACHE: Handles code and c[] caches and fills them. */
	if (status & 0x010) {
		ustatus = nv_rd32(dev, 0x405018) & 0x7fffffff;
		if (!ustatus && display) {
			NV_INFO(dev, "PGRAPH_TRAP_CCACHE - no ustatus?\n");
		}
		if (ustatus & 0x00000001) {
			nv50_fb_vm_trap(dev, display, "PGRAPH_TRAP_CCACHE_FAULT");
			NV_INFO (dev, "PGRAPH_TRAP_CCACHE_FAULT - %08x %08x %08x %08x %08x %08x %08x\n",
					nv_rd32(dev, 0x405800),
					nv_rd32(dev, 0x405804),
					nv_rd32(dev, 0x405808),
					nv_rd32(dev, 0x40580c),
					nv_rd32(dev, 0x405810),
					nv_rd32(dev, 0x405814),
					nv_rd32(dev, 0x40581c));
			ustatus &= ~0x00000001;
		}
		if (ustatus && display)
			NV_INFO(dev, "PGRAPH_TRAP_CCACHE - Unhandled ustatus 0x%08x\n", ustatus);
		nv_wr32(dev, 0x405018, 0xc0000000);
		nv_wr32(dev, 0x400108, 0x010);
		status &= ~0x010;
	}

	/* Unknown, not seen yet... 0x402000 is the only trap status reg
	 * remaining, so try to handle it anyway. Perhaps related to that
	 * unknown DMA slot on tesla? */
	if (status & 0x20) {
		nv50_fb_vm_trap(dev, display, "PGRAPH_TRAP_UNKC04");
		ustatus = nv_rd32(dev, 0x402000) & 0x7fffffff;
		if (display)
			NV_INFO(dev, "PGRAPH_TRAP_UNKC04 - Unhandled ustatus 0x%08x\n", ustatus);
		nv_wr32(dev, 0x402000, 0xc0000000);
		/* no status modifiction on purpose */
	}

	/* TEXTURE: CUDA texturing units */
	if (status & 0x040) {
		nv50_pgraph_tp_trap (dev, 6, 0x408900, 0x408600, display,
				"PGRAPH_TRAP_TEXTURE");
		nv_wr32(dev, 0x400108, 0x040);
		status &= ~0x040;
	}

	/* MP: CUDA execution engines. */
	if (status & 0x080) {
		nv50_pgraph_tp_trap (dev, 7, 0x408314, 0x40831c, display,
				"PGRAPH_TRAP_MP");
		nv_wr32(dev, 0x400108, 0x080);
		status &= ~0x080;
	}

	/* TPDMA:  Handles TP-initiated uncached memory accesses:
	 * l[], g[], stack, 2d surfaces, render targets. */
	if (status & 0x100) {
		nv50_pgraph_tp_trap (dev, 8, 0x408e08, 0x408708, display,
				"PGRAPH_TRAP_TPDMA");
		nv_wr32(dev, 0x400108, 0x100);
		status &= ~0x100;
	}

	if (status) {
		if (display)
			NV_INFO(dev, "PGRAPH_TRAP - Unknown trap 0x%08x\n",
				status);
		nv_wr32(dev, 0x400108, status);
	}
}

/* There must be a *lot* of these. Will take some time to gather them up. */
static struct nouveau_enum_names nv50_data_error_names[] =
{
	{ 4,	"INVALID_VALUE" },
	{ 5,	"INVALID_ENUM" },
	{ 8,	"INVALID_OBJECT" },
	{ 0xc,	"INVALID_BITFIELD" },
	{ 0x28,	"MP_NO_REG_SPACE" },
	{ 0x2b,	"MP_BLOCK_SIZE_MISMATCH" },
};

static void
nv50_pgraph_irq_handler(struct drm_device *dev)
{
	struct nouveau_pgraph_trap trap;
	int unhandled = 0;
	uint32_t status;

	while ((status = nv_rd32(dev, NV03_PGRAPH_INTR))) {
		/* NOTIFY: You've set a NOTIFY an a command and it's done. */
		if (status & 0x00000001) {
			nouveau_graph_trap_info(dev, &trap);
			if (nouveau_ratelimit())
				nouveau_graph_dump_trap_info(dev,
						"PGRAPH_NOTIFY", &trap);
			status &= ~0x00000001;
			nv_wr32(dev, NV03_PGRAPH_INTR, 0x00000001);
		}

		/* COMPUTE_QUERY: Purpose and exact cause unknown, happens
		 * when you write 0x200 to 0x50c0 method 0x31c. */
		if (status & 0x00000002) {
			nouveau_graph_trap_info(dev, &trap);
			if (nouveau_ratelimit())
				nouveau_graph_dump_trap_info(dev,
						"PGRAPH_COMPUTE_QUERY", &trap);
			status &= ~0x00000002;
			nv_wr32(dev, NV03_PGRAPH_INTR, 0x00000002);
		}

		/* Unknown, never seen: 0x4 */

		/* ILLEGAL_MTHD: You used a wrong method for this class. */
		if (status & 0x00000010) {
			nouveau_graph_trap_info(dev, &trap);
			if (nouveau_pgraph_intr_swmthd(dev, &trap))
				unhandled = 1;
			if (unhandled && nouveau_ratelimit())
				nouveau_graph_dump_trap_info(dev,
						"PGRAPH_ILLEGAL_MTHD", &trap);
			status &= ~0x00000010;
			nv_wr32(dev, NV03_PGRAPH_INTR, 0x00000010);
		}

		/* ILLEGAL_CLASS: You used a wrong class. */
		if (status & 0x00000020) {
			nouveau_graph_trap_info(dev, &trap);
			if (nouveau_ratelimit())
				nouveau_graph_dump_trap_info(dev,
						"PGRAPH_ILLEGAL_CLASS", &trap);
			status &= ~0x00000020;
			nv_wr32(dev, NV03_PGRAPH_INTR, 0x00000020);
		}

		/* DOUBLE_NOTIFY: You tried to set a NOTIFY on another NOTIFY. */
		if (status & 0x00000040) {
			nouveau_graph_trap_info(dev, &trap);
			if (nouveau_ratelimit())
				nouveau_graph_dump_trap_info(dev,
						"PGRAPH_DOUBLE_NOTIFY", &trap);
			status &= ~0x00000040;
			nv_wr32(dev, NV03_PGRAPH_INTR, 0x00000040);
		}

		/* CONTEXT_SWITCH: PGRAPH needs us to load a new context */
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

		/* BUFFER_NOTIFY: Your m2mf transfer finished */
		if (status & 0x00010000) {
			nouveau_graph_trap_info(dev, &trap);
			if (nouveau_ratelimit())
				nouveau_graph_dump_trap_info(dev,
						"PGRAPH_BUFFER_NOTIFY", &trap);
			status &= ~0x00010000;
			nv_wr32(dev, NV03_PGRAPH_INTR, 0x00010000);
		}

		/* DATA_ERROR: Invalid value for this method, or invalid
		 * state in current PGRAPH context for this operation */
		if (status & 0x00100000) {
			nouveau_graph_trap_info(dev, &trap);
			if (nouveau_ratelimit()) {
				nouveau_graph_dump_trap_info(dev,
						"PGRAPH_DATA_ERROR", &trap);
				NV_INFO (dev, "PGRAPH_DATA_ERROR - ");
				nouveau_print_enum_names(nv_rd32(dev, 0x400110),
						nv50_data_error_names);
				printk("\n");
			}
			status &= ~0x00100000;
			nv_wr32(dev, NV03_PGRAPH_INTR, 0x00100000);
		}

		/* TRAP: Something bad happened in the middle of command
		 * execution.  Has a billion types, subtypes, and even
		 * subsubtypes. */
		if (status & 0x00200000) {
			nv50_pgraph_trap_handler(dev);
			status &= ~0x00200000;
			nv_wr32(dev, NV03_PGRAPH_INTR, 0x00200000);
		}

		/* Unknown, never seen: 0x00400000 */

		/* SINGLE_STEP: Happens on every method if you turned on
		 * single stepping in 40008c */
		if (status & 0x01000000) {
			nouveau_graph_trap_info(dev, &trap);
			if (nouveau_ratelimit())
				nouveau_graph_dump_trap_info(dev,
						"PGRAPH_SINGLE_STEP", &trap);
			status &= ~0x01000000;
			nv_wr32(dev, NV03_PGRAPH_INTR, 0x01000000);
		}

		/* 0x02000000 happens when you pause a ctxprog...
		 * but the only way this can happen that I know is by
		 * poking the relevant MMIO register, and we don't
		 * do that. */

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
	if (nv_rd32(dev, 0x400824) & (1 << 31))
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
	uint32_t status;
	unsigned long flags;

	status = nv_rd32(dev, NV03_PMC_INTR_0);
	if (!status)
		return IRQ_NONE;

	spin_lock_irqsave(&dev_priv->context_switch_lock, flags);

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

	spin_unlock_irqrestore(&dev_priv->context_switch_lock, flags);

	return IRQ_HANDLED;
}
