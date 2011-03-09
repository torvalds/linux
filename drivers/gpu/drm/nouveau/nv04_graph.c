/*
 * Copyright 2007 Stephane Marchesin
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "drmP.h"
#include "drm.h"
#include "nouveau_drm.h"
#include "nouveau_drv.h"
#include "nouveau_hw.h"
#include "nouveau_util.h"

static int  nv04_graph_register(struct drm_device *dev);
static void nv04_graph_isr(struct drm_device *dev);

static uint32_t nv04_graph_ctx_regs[] = {
	0x0040053c,
	0x00400544,
	0x00400540,
	0x00400548,
	NV04_PGRAPH_CTX_SWITCH1,
	NV04_PGRAPH_CTX_SWITCH2,
	NV04_PGRAPH_CTX_SWITCH3,
	NV04_PGRAPH_CTX_SWITCH4,
	NV04_PGRAPH_CTX_CACHE1,
	NV04_PGRAPH_CTX_CACHE2,
	NV04_PGRAPH_CTX_CACHE3,
	NV04_PGRAPH_CTX_CACHE4,
	0x00400184,
	0x004001a4,
	0x004001c4,
	0x004001e4,
	0x00400188,
	0x004001a8,
	0x004001c8,
	0x004001e8,
	0x0040018c,
	0x004001ac,
	0x004001cc,
	0x004001ec,
	0x00400190,
	0x004001b0,
	0x004001d0,
	0x004001f0,
	0x00400194,
	0x004001b4,
	0x004001d4,
	0x004001f4,
	0x00400198,
	0x004001b8,
	0x004001d8,
	0x004001f8,
	0x0040019c,
	0x004001bc,
	0x004001dc,
	0x004001fc,
	0x00400174,
	NV04_PGRAPH_DMA_START_0,
	NV04_PGRAPH_DMA_START_1,
	NV04_PGRAPH_DMA_LENGTH,
	NV04_PGRAPH_DMA_MISC,
	NV04_PGRAPH_DMA_PITCH,
	NV04_PGRAPH_BOFFSET0,
	NV04_PGRAPH_BBASE0,
	NV04_PGRAPH_BLIMIT0,
	NV04_PGRAPH_BOFFSET1,
	NV04_PGRAPH_BBASE1,
	NV04_PGRAPH_BLIMIT1,
	NV04_PGRAPH_BOFFSET2,
	NV04_PGRAPH_BBASE2,
	NV04_PGRAPH_BLIMIT2,
	NV04_PGRAPH_BOFFSET3,
	NV04_PGRAPH_BBASE3,
	NV04_PGRAPH_BLIMIT3,
	NV04_PGRAPH_BOFFSET4,
	NV04_PGRAPH_BBASE4,
	NV04_PGRAPH_BLIMIT4,
	NV04_PGRAPH_BOFFSET5,
	NV04_PGRAPH_BBASE5,
	NV04_PGRAPH_BLIMIT5,
	NV04_PGRAPH_BPITCH0,
	NV04_PGRAPH_BPITCH1,
	NV04_PGRAPH_BPITCH2,
	NV04_PGRAPH_BPITCH3,
	NV04_PGRAPH_BPITCH4,
	NV04_PGRAPH_SURFACE,
	NV04_PGRAPH_STATE,
	NV04_PGRAPH_BSWIZZLE2,
	NV04_PGRAPH_BSWIZZLE5,
	NV04_PGRAPH_BPIXEL,
	NV04_PGRAPH_NOTIFY,
	NV04_PGRAPH_PATT_COLOR0,
	NV04_PGRAPH_PATT_COLOR1,
	NV04_PGRAPH_PATT_COLORRAM+0x00,
	NV04_PGRAPH_PATT_COLORRAM+0x04,
	NV04_PGRAPH_PATT_COLORRAM+0x08,
	NV04_PGRAPH_PATT_COLORRAM+0x0c,
	NV04_PGRAPH_PATT_COLORRAM+0x10,
	NV04_PGRAPH_PATT_COLORRAM+0x14,
	NV04_PGRAPH_PATT_COLORRAM+0x18,
	NV04_PGRAPH_PATT_COLORRAM+0x1c,
	NV04_PGRAPH_PATT_COLORRAM+0x20,
	NV04_PGRAPH_PATT_COLORRAM+0x24,
	NV04_PGRAPH_PATT_COLORRAM+0x28,
	NV04_PGRAPH_PATT_COLORRAM+0x2c,
	NV04_PGRAPH_PATT_COLORRAM+0x30,
	NV04_PGRAPH_PATT_COLORRAM+0x34,
	NV04_PGRAPH_PATT_COLORRAM+0x38,
	NV04_PGRAPH_PATT_COLORRAM+0x3c,
	NV04_PGRAPH_PATT_COLORRAM+0x40,
	NV04_PGRAPH_PATT_COLORRAM+0x44,
	NV04_PGRAPH_PATT_COLORRAM+0x48,
	NV04_PGRAPH_PATT_COLORRAM+0x4c,
	NV04_PGRAPH_PATT_COLORRAM+0x50,
	NV04_PGRAPH_PATT_COLORRAM+0x54,
	NV04_PGRAPH_PATT_COLORRAM+0x58,
	NV04_PGRAPH_PATT_COLORRAM+0x5c,
	NV04_PGRAPH_PATT_COLORRAM+0x60,
	NV04_PGRAPH_PATT_COLORRAM+0x64,
	NV04_PGRAPH_PATT_COLORRAM+0x68,
	NV04_PGRAPH_PATT_COLORRAM+0x6c,
	NV04_PGRAPH_PATT_COLORRAM+0x70,
	NV04_PGRAPH_PATT_COLORRAM+0x74,
	NV04_PGRAPH_PATT_COLORRAM+0x78,
	NV04_PGRAPH_PATT_COLORRAM+0x7c,
	NV04_PGRAPH_PATT_COLORRAM+0x80,
	NV04_PGRAPH_PATT_COLORRAM+0x84,
	NV04_PGRAPH_PATT_COLORRAM+0x88,
	NV04_PGRAPH_PATT_COLORRAM+0x8c,
	NV04_PGRAPH_PATT_COLORRAM+0x90,
	NV04_PGRAPH_PATT_COLORRAM+0x94,
	NV04_PGRAPH_PATT_COLORRAM+0x98,
	NV04_PGRAPH_PATT_COLORRAM+0x9c,
	NV04_PGRAPH_PATT_COLORRAM+0xa0,
	NV04_PGRAPH_PATT_COLORRAM+0xa4,
	NV04_PGRAPH_PATT_COLORRAM+0xa8,
	NV04_PGRAPH_PATT_COLORRAM+0xac,
	NV04_PGRAPH_PATT_COLORRAM+0xb0,
	NV04_PGRAPH_PATT_COLORRAM+0xb4,
	NV04_PGRAPH_PATT_COLORRAM+0xb8,
	NV04_PGRAPH_PATT_COLORRAM+0xbc,
	NV04_PGRAPH_PATT_COLORRAM+0xc0,
	NV04_PGRAPH_PATT_COLORRAM+0xc4,
	NV04_PGRAPH_PATT_COLORRAM+0xc8,
	NV04_PGRAPH_PATT_COLORRAM+0xcc,
	NV04_PGRAPH_PATT_COLORRAM+0xd0,
	NV04_PGRAPH_PATT_COLORRAM+0xd4,
	NV04_PGRAPH_PATT_COLORRAM+0xd8,
	NV04_PGRAPH_PATT_COLORRAM+0xdc,
	NV04_PGRAPH_PATT_COLORRAM+0xe0,
	NV04_PGRAPH_PATT_COLORRAM+0xe4,
	NV04_PGRAPH_PATT_COLORRAM+0xe8,
	NV04_PGRAPH_PATT_COLORRAM+0xec,
	NV04_PGRAPH_PATT_COLORRAM+0xf0,
	NV04_PGRAPH_PATT_COLORRAM+0xf4,
	NV04_PGRAPH_PATT_COLORRAM+0xf8,
	NV04_PGRAPH_PATT_COLORRAM+0xfc,
	NV04_PGRAPH_PATTERN,
	0x0040080c,
	NV04_PGRAPH_PATTERN_SHAPE,
	0x00400600,
	NV04_PGRAPH_ROP3,
	NV04_PGRAPH_CHROMA,
	NV04_PGRAPH_BETA_AND,
	NV04_PGRAPH_BETA_PREMULT,
	NV04_PGRAPH_CONTROL0,
	NV04_PGRAPH_CONTROL1,
	NV04_PGRAPH_CONTROL2,
	NV04_PGRAPH_BLEND,
	NV04_PGRAPH_STORED_FMT,
	NV04_PGRAPH_SOURCE_COLOR,
	0x00400560,
	0x00400568,
	0x00400564,
	0x0040056c,
	0x00400400,
	0x00400480,
	0x00400404,
	0x00400484,
	0x00400408,
	0x00400488,
	0x0040040c,
	0x0040048c,
	0x00400410,
	0x00400490,
	0x00400414,
	0x00400494,
	0x00400418,
	0x00400498,
	0x0040041c,
	0x0040049c,
	0x00400420,
	0x004004a0,
	0x00400424,
	0x004004a4,
	0x00400428,
	0x004004a8,
	0x0040042c,
	0x004004ac,
	0x00400430,
	0x004004b0,
	0x00400434,
	0x004004b4,
	0x00400438,
	0x004004b8,
	0x0040043c,
	0x004004bc,
	0x00400440,
	0x004004c0,
	0x00400444,
	0x004004c4,
	0x00400448,
	0x004004c8,
	0x0040044c,
	0x004004cc,
	0x00400450,
	0x004004d0,
	0x00400454,
	0x004004d4,
	0x00400458,
	0x004004d8,
	0x0040045c,
	0x004004dc,
	0x00400460,
	0x004004e0,
	0x00400464,
	0x004004e4,
	0x00400468,
	0x004004e8,
	0x0040046c,
	0x004004ec,
	0x00400470,
	0x004004f0,
	0x00400474,
	0x004004f4,
	0x00400478,
	0x004004f8,
	0x0040047c,
	0x004004fc,
	0x00400534,
	0x00400538,
	0x00400514,
	0x00400518,
	0x0040051c,
	0x00400520,
	0x00400524,
	0x00400528,
	0x0040052c,
	0x00400530,
	0x00400d00,
	0x00400d40,
	0x00400d80,
	0x00400d04,
	0x00400d44,
	0x00400d84,
	0x00400d08,
	0x00400d48,
	0x00400d88,
	0x00400d0c,
	0x00400d4c,
	0x00400d8c,
	0x00400d10,
	0x00400d50,
	0x00400d90,
	0x00400d14,
	0x00400d54,
	0x00400d94,
	0x00400d18,
	0x00400d58,
	0x00400d98,
	0x00400d1c,
	0x00400d5c,
	0x00400d9c,
	0x00400d20,
	0x00400d60,
	0x00400da0,
	0x00400d24,
	0x00400d64,
	0x00400da4,
	0x00400d28,
	0x00400d68,
	0x00400da8,
	0x00400d2c,
	0x00400d6c,
	0x00400dac,
	0x00400d30,
	0x00400d70,
	0x00400db0,
	0x00400d34,
	0x00400d74,
	0x00400db4,
	0x00400d38,
	0x00400d78,
	0x00400db8,
	0x00400d3c,
	0x00400d7c,
	0x00400dbc,
	0x00400590,
	0x00400594,
	0x00400598,
	0x0040059c,
	0x004005a8,
	0x004005ac,
	0x004005b0,
	0x004005b4,
	0x004005c0,
	0x004005c4,
	0x004005c8,
	0x004005cc,
	0x004005d0,
	0x004005d4,
	0x004005d8,
	0x004005dc,
	0x004005e0,
	NV04_PGRAPH_PASSTHRU_0,
	NV04_PGRAPH_PASSTHRU_1,
	NV04_PGRAPH_PASSTHRU_2,
	NV04_PGRAPH_DVD_COLORFMT,
	NV04_PGRAPH_SCALED_FORMAT,
	NV04_PGRAPH_MISC24_0,
	NV04_PGRAPH_MISC24_1,
	NV04_PGRAPH_MISC24_2,
	0x00400500,
	0x00400504,
	NV04_PGRAPH_VALID1,
	NV04_PGRAPH_VALID2,
	NV04_PGRAPH_DEBUG_3
};

struct graph_state {
	uint32_t nv04[ARRAY_SIZE(nv04_graph_ctx_regs)];
};

struct nouveau_channel *
nv04_graph_channel(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int chid = dev_priv->engine.fifo.channels;

	if (nv_rd32(dev, NV04_PGRAPH_CTX_CONTROL) & 0x00010000)
		chid = nv_rd32(dev, NV04_PGRAPH_CTX_USER) >> 24;

	if (chid >= dev_priv->engine.fifo.channels)
		return NULL;

	return dev_priv->channels.ptr[chid];
}

static void
nv04_graph_context_switch(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	struct nouveau_channel *chan = NULL;
	int chid;

	nouveau_wait_for_idle(dev);

	/* If previous context is valid, we need to save it */
	pgraph->unload_context(dev);

	/* Load context for next channel */
	chid = dev_priv->engine.fifo.channel_id(dev);
	chan = dev_priv->channels.ptr[chid];
	if (chan)
		nv04_graph_load_context(chan);
}

static uint32_t *ctx_reg(struct graph_state *ctx, uint32_t reg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(nv04_graph_ctx_regs); i++) {
		if (nv04_graph_ctx_regs[i] == reg)
			return &ctx->nv04[i];
	}

	return NULL;
}

int nv04_graph_create_context(struct nouveau_channel *chan)
{
	struct graph_state *pgraph_ctx;
	NV_DEBUG(chan->dev, "nv04_graph_context_create %d\n", chan->id);

	chan->pgraph_ctx = pgraph_ctx = kzalloc(sizeof(*pgraph_ctx),
						GFP_KERNEL);
	if (pgraph_ctx == NULL)
		return -ENOMEM;

	*ctx_reg(pgraph_ctx, NV04_PGRAPH_DEBUG_3) = 0xfad4ff31;

	return 0;
}

void nv04_graph_destroy_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	struct graph_state *pgraph_ctx = chan->pgraph_ctx;
	unsigned long flags;

	spin_lock_irqsave(&dev_priv->context_switch_lock, flags);
	pgraph->fifo_access(dev, false);

	/* Unload the context if it's the currently active one */
	if (pgraph->channel(dev) == chan)
		pgraph->unload_context(dev);

	/* Free the context resources */
	kfree(pgraph_ctx);
	chan->pgraph_ctx = NULL;

	pgraph->fifo_access(dev, true);
	spin_unlock_irqrestore(&dev_priv->context_switch_lock, flags);
}

int nv04_graph_load_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct graph_state *pgraph_ctx = chan->pgraph_ctx;
	uint32_t tmp;
	int i;

	for (i = 0; i < ARRAY_SIZE(nv04_graph_ctx_regs); i++)
		nv_wr32(dev, nv04_graph_ctx_regs[i], pgraph_ctx->nv04[i]);

	nv_wr32(dev, NV04_PGRAPH_CTX_CONTROL, 0x10010100);

	tmp  = nv_rd32(dev, NV04_PGRAPH_CTX_USER) & 0x00ffffff;
	nv_wr32(dev, NV04_PGRAPH_CTX_USER, tmp | chan->id << 24);

	tmp = nv_rd32(dev, NV04_PGRAPH_FFINTFC_ST2);
	nv_wr32(dev, NV04_PGRAPH_FFINTFC_ST2, tmp & 0x000fffff);

	return 0;
}

int
nv04_graph_unload_context(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	struct nouveau_channel *chan = NULL;
	struct graph_state *ctx;
	uint32_t tmp;
	int i;

	chan = pgraph->channel(dev);
	if (!chan)
		return 0;
	ctx = chan->pgraph_ctx;

	for (i = 0; i < ARRAY_SIZE(nv04_graph_ctx_regs); i++)
		ctx->nv04[i] = nv_rd32(dev, nv04_graph_ctx_regs[i]);

	nv_wr32(dev, NV04_PGRAPH_CTX_CONTROL, 0x10000000);
	tmp  = nv_rd32(dev, NV04_PGRAPH_CTX_USER) & 0x00ffffff;
	tmp |= (dev_priv->engine.fifo.channels - 1) << 24;
	nv_wr32(dev, NV04_PGRAPH_CTX_USER, tmp);
	return 0;
}

int nv04_graph_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t tmp;
	int ret;

	nv_wr32(dev, NV03_PMC_ENABLE, nv_rd32(dev, NV03_PMC_ENABLE) &
			~NV_PMC_ENABLE_PGRAPH);
	nv_wr32(dev, NV03_PMC_ENABLE, nv_rd32(dev, NV03_PMC_ENABLE) |
			 NV_PMC_ENABLE_PGRAPH);

	ret = nv04_graph_register(dev);
	if (ret)
		return ret;

	/* Enable PGRAPH interrupts */
	nouveau_irq_register(dev, 12, nv04_graph_isr);
	nv_wr32(dev, NV03_PGRAPH_INTR, 0xFFFFFFFF);
	nv_wr32(dev, NV03_PGRAPH_INTR_EN, 0xFFFFFFFF);

	nv_wr32(dev, NV04_PGRAPH_VALID1, 0);
	nv_wr32(dev, NV04_PGRAPH_VALID2, 0);
	/*nv_wr32(dev, NV04_PGRAPH_DEBUG_0, 0x000001FF);
	nv_wr32(dev, NV04_PGRAPH_DEBUG_0, 0x001FFFFF);*/
	nv_wr32(dev, NV04_PGRAPH_DEBUG_0, 0x1231c000);
	/*1231C000 blob, 001 haiku*/
	//*V_WRITE(NV04_PGRAPH_DEBUG_1, 0xf2d91100);*/
	nv_wr32(dev, NV04_PGRAPH_DEBUG_1, 0x72111100);
	/*0x72111100 blob , 01 haiku*/
	/*nv_wr32(dev, NV04_PGRAPH_DEBUG_2, 0x11d5f870);*/
	nv_wr32(dev, NV04_PGRAPH_DEBUG_2, 0x11d5f071);
	/*haiku same*/

	/*nv_wr32(dev, NV04_PGRAPH_DEBUG_3, 0xfad4ff31);*/
	nv_wr32(dev, NV04_PGRAPH_DEBUG_3, 0xf0d4ff31);
	/*haiku and blob 10d4*/

	nv_wr32(dev, NV04_PGRAPH_STATE        , 0xFFFFFFFF);
	nv_wr32(dev, NV04_PGRAPH_CTX_CONTROL  , 0x10000100);
	tmp  = nv_rd32(dev, NV04_PGRAPH_CTX_USER) & 0x00ffffff;
	tmp |= (dev_priv->engine.fifo.channels - 1) << 24;
	nv_wr32(dev, NV04_PGRAPH_CTX_USER, tmp);

	/* These don't belong here, they're part of a per-channel context */
	nv_wr32(dev, NV04_PGRAPH_PATTERN_SHAPE, 0x00000000);
	nv_wr32(dev, NV04_PGRAPH_BETA_AND     , 0xFFFFFFFF);

	return 0;
}

void nv04_graph_takedown(struct drm_device *dev)
{
	nv_wr32(dev, NV03_PGRAPH_INTR_EN, 0x00000000);
	nouveau_irq_unregister(dev, 12);
}

void
nv04_graph_fifo_access(struct drm_device *dev, bool enabled)
{
	if (enabled)
		nv_wr32(dev, NV04_PGRAPH_FIFO,
					nv_rd32(dev, NV04_PGRAPH_FIFO) | 1);
	else
		nv_wr32(dev, NV04_PGRAPH_FIFO,
					nv_rd32(dev, NV04_PGRAPH_FIFO) & ~1);
}

static int
nv04_graph_mthd_set_ref(struct nouveau_channel *chan,
			u32 class, u32 mthd, u32 data)
{
	atomic_set(&chan->fence.last_sequence_irq, data);
	return 0;
}

int
nv04_graph_mthd_page_flip(struct nouveau_channel *chan,
			  u32 class, u32 mthd, u32 data)
{
	struct drm_device *dev = chan->dev;
	struct nouveau_page_flip_state s;

	if (!nouveau_finish_page_flip(chan, &s))
		nv_set_crtc_base(dev, s.crtc,
				 s.offset + s.y * s.pitch + s.x * s.bpp / 8);

	return 0;
}

/*
 * Software methods, why they are needed, and how they all work:
 *
 * NV04 and NV05 keep most of the state in PGRAPH context itself, but some
 * 2d engine settings are kept inside the grobjs themselves. The grobjs are
 * 3 words long on both. grobj format on NV04 is:
 *
 * word 0:
 *  - bits 0-7: class
 *  - bit 12: color key active
 *  - bit 13: clip rect active
 *  - bit 14: if set, destination surface is swizzled and taken from buffer 5
 *            [set by NV04_SWIZZLED_SURFACE], otherwise it's linear and taken
 *            from buffer 0 [set by NV04_CONTEXT_SURFACES_2D or
 *            NV03_CONTEXT_SURFACE_DST].
 *  - bits 15-17: 2d operation [aka patch config]
 *  - bit 24: patch valid [enables rendering using this object]
 *  - bit 25: surf3d valid [for tex_tri and multitex_tri only]
 * word 1:
 *  - bits 0-1: mono format
 *  - bits 8-13: color format
 *  - bits 16-31: DMA_NOTIFY instance
 * word 2:
 *  - bits 0-15: DMA_A instance
 *  - bits 16-31: DMA_B instance
 *
 * On NV05 it's:
 *
 * word 0:
 *  - bits 0-7: class
 *  - bit 12: color key active
 *  - bit 13: clip rect active
 *  - bit 14: if set, destination surface is swizzled and taken from buffer 5
 *            [set by NV04_SWIZZLED_SURFACE], otherwise it's linear and taken
 *            from buffer 0 [set by NV04_CONTEXT_SURFACES_2D or
 *            NV03_CONTEXT_SURFACE_DST].
 *  - bits 15-17: 2d operation [aka patch config]
 *  - bits 20-22: dither mode
 *  - bit 24: patch valid [enables rendering using this object]
 *  - bit 25: surface_dst/surface_color/surf2d/surf3d valid
 *  - bit 26: surface_src/surface_zeta valid
 *  - bit 27: pattern valid
 *  - bit 28: rop valid
 *  - bit 29: beta1 valid
 *  - bit 30: beta4 valid
 * word 1:
 *  - bits 0-1: mono format
 *  - bits 8-13: color format
 *  - bits 16-31: DMA_NOTIFY instance
 * word 2:
 *  - bits 0-15: DMA_A instance
 *  - bits 16-31: DMA_B instance
 *
 * NV05 will set/unset the relevant valid bits when you poke the relevant
 * object-binding methods with object of the proper type, or with the NULL
 * type. It'll only allow rendering using the grobj if all needed objects
 * are bound. The needed set of objects depends on selected operation: for
 * example rop object is needed by ROP_AND, but not by SRCCOPY_AND.
 *
 * NV04 doesn't have these methods implemented at all, and doesn't have the
 * relevant bits in grobj. Instead, it'll allow rendering whenever bit 24
 * is set. So we have to emulate them in software, internally keeping the
 * same bits as NV05 does. Since grobjs are aligned to 16 bytes on nv04,
 * but the last word isn't actually used for anything, we abuse it for this
 * purpose.
 *
 * Actually, NV05 can optionally check bit 24 too, but we disable this since
 * there's no use for it.
 *
 * For unknown reasons, NV04 implements surf3d binding in hardware as an
 * exception. Also for unknown reasons, NV04 doesn't implement the clipping
 * methods on the surf3d object, so we have to emulate them too.
 */

static void
nv04_graph_set_ctx1(struct nouveau_channel *chan, u32 mask, u32 value)
{
	struct drm_device *dev = chan->dev;
	u32 instance = (nv_rd32(dev, NV04_PGRAPH_CTX_SWITCH4) & 0xffff) << 4;
	int subc = (nv_rd32(dev, NV04_PGRAPH_TRAPPED_ADDR) >> 13) & 0x7;
	u32 tmp;

	tmp  = nv_ri32(dev, instance);
	tmp &= ~mask;
	tmp |= value;

	nv_wi32(dev, instance, tmp);
	nv_wr32(dev, NV04_PGRAPH_CTX_SWITCH1, tmp);
	nv_wr32(dev, NV04_PGRAPH_CTX_CACHE1 + (subc<<2), tmp);
}

static void
nv04_graph_set_ctx_val(struct nouveau_channel *chan, u32 mask, u32 value)
{
	struct drm_device *dev = chan->dev;
	u32 instance = (nv_rd32(dev, NV04_PGRAPH_CTX_SWITCH4) & 0xffff) << 4;
	u32 tmp, ctx1;
	int class, op, valid = 1;

	ctx1 = nv_ri32(dev, instance);
	class = ctx1 & 0xff;
	op = (ctx1 >> 15) & 7;
	tmp  = nv_ri32(dev, instance + 0xc);
	tmp &= ~mask;
	tmp |= value;
	nv_wi32(dev, instance + 0xc, tmp);

	/* check for valid surf2d/surf_dst/surf_color */
	if (!(tmp & 0x02000000))
		valid = 0;
	/* check for valid surf_src/surf_zeta */
	if ((class == 0x1f || class == 0x48) && !(tmp & 0x04000000))
		valid = 0;

	switch (op) {
	/* SRCCOPY_AND, SRCCOPY: no extra objects required */
	case 0:
	case 3:
		break;
	/* ROP_AND: requires pattern and rop */
	case 1:
		if (!(tmp & 0x18000000))
			valid = 0;
		break;
	/* BLEND_AND: requires beta1 */
	case 2:
		if (!(tmp & 0x20000000))
			valid = 0;
		break;
	/* SRCCOPY_PREMULT, BLEND_PREMULT: beta4 required */
	case 4:
	case 5:
		if (!(tmp & 0x40000000))
			valid = 0;
		break;
	}

	nv04_graph_set_ctx1(chan, 0x01000000, valid << 24);
}

static int
nv04_graph_mthd_set_operation(struct nouveau_channel *chan,
			      u32 class, u32 mthd, u32 data)
{
	if (data > 5)
		return 1;
	/* Old versions of the objects only accept first three operations. */
	if (data > 2 && class < 0x40)
		return 1;
	nv04_graph_set_ctx1(chan, 0x00038000, data << 15);
	/* changing operation changes set of objects needed for validation */
	nv04_graph_set_ctx_val(chan, 0, 0);
	return 0;
}

static int
nv04_graph_mthd_surf3d_clip_h(struct nouveau_channel *chan,
			      u32 class, u32 mthd, u32 data)
{
	uint32_t min = data & 0xffff, max;
	uint32_t w = data >> 16;
	if (min & 0x8000)
		/* too large */
		return 1;
	if (w & 0x8000)
		/* yes, it accepts negative for some reason. */
		w |= 0xffff0000;
	max = min + w;
	max &= 0x3ffff;
	nv_wr32(chan->dev, 0x40053c, min);
	nv_wr32(chan->dev, 0x400544, max);
	return 0;
}

static int
nv04_graph_mthd_surf3d_clip_v(struct nouveau_channel *chan,
			      u32 class, u32 mthd, u32 data)
{
	uint32_t min = data & 0xffff, max;
	uint32_t w = data >> 16;
	if (min & 0x8000)
		/* too large */
		return 1;
	if (w & 0x8000)
		/* yes, it accepts negative for some reason. */
		w |= 0xffff0000;
	max = min + w;
	max &= 0x3ffff;
	nv_wr32(chan->dev, 0x400540, min);
	nv_wr32(chan->dev, 0x400548, max);
	return 0;
}

static int
nv04_graph_mthd_bind_surf2d(struct nouveau_channel *chan,
			    u32 class, u32 mthd, u32 data)
{
	switch (nv_ri32(chan->dev, data << 4) & 0xff) {
	case 0x30:
		nv04_graph_set_ctx1(chan, 0x00004000, 0);
		nv04_graph_set_ctx_val(chan, 0x02000000, 0);
		return 0;
	case 0x42:
		nv04_graph_set_ctx1(chan, 0x00004000, 0);
		nv04_graph_set_ctx_val(chan, 0x02000000, 0x02000000);
		return 0;
	}
	return 1;
}

static int
nv04_graph_mthd_bind_surf2d_swzsurf(struct nouveau_channel *chan,
				    u32 class, u32 mthd, u32 data)
{
	switch (nv_ri32(chan->dev, data << 4) & 0xff) {
	case 0x30:
		nv04_graph_set_ctx1(chan, 0x00004000, 0);
		nv04_graph_set_ctx_val(chan, 0x02000000, 0);
		return 0;
	case 0x42:
		nv04_graph_set_ctx1(chan, 0x00004000, 0);
		nv04_graph_set_ctx_val(chan, 0x02000000, 0x02000000);
		return 0;
	case 0x52:
		nv04_graph_set_ctx1(chan, 0x00004000, 0x00004000);
		nv04_graph_set_ctx_val(chan, 0x02000000, 0x02000000);
		return 0;
	}
	return 1;
}

static int
nv04_graph_mthd_bind_nv01_patt(struct nouveau_channel *chan,
			       u32 class, u32 mthd, u32 data)
{
	switch (nv_ri32(chan->dev, data << 4) & 0xff) {
	case 0x30:
		nv04_graph_set_ctx_val(chan, 0x08000000, 0);
		return 0;
	case 0x18:
		nv04_graph_set_ctx_val(chan, 0x08000000, 0x08000000);
		return 0;
	}
	return 1;
}

static int
nv04_graph_mthd_bind_nv04_patt(struct nouveau_channel *chan,
			       u32 class, u32 mthd, u32 data)
{
	switch (nv_ri32(chan->dev, data << 4) & 0xff) {
	case 0x30:
		nv04_graph_set_ctx_val(chan, 0x08000000, 0);
		return 0;
	case 0x44:
		nv04_graph_set_ctx_val(chan, 0x08000000, 0x08000000);
		return 0;
	}
	return 1;
}

static int
nv04_graph_mthd_bind_rop(struct nouveau_channel *chan,
			 u32 class, u32 mthd, u32 data)
{
	switch (nv_ri32(chan->dev, data << 4) & 0xff) {
	case 0x30:
		nv04_graph_set_ctx_val(chan, 0x10000000, 0);
		return 0;
	case 0x43:
		nv04_graph_set_ctx_val(chan, 0x10000000, 0x10000000);
		return 0;
	}
	return 1;
}

static int
nv04_graph_mthd_bind_beta1(struct nouveau_channel *chan,
			   u32 class, u32 mthd, u32 data)
{
	switch (nv_ri32(chan->dev, data << 4) & 0xff) {
	case 0x30:
		nv04_graph_set_ctx_val(chan, 0x20000000, 0);
		return 0;
	case 0x12:
		nv04_graph_set_ctx_val(chan, 0x20000000, 0x20000000);
		return 0;
	}
	return 1;
}

static int
nv04_graph_mthd_bind_beta4(struct nouveau_channel *chan,
			   u32 class, u32 mthd, u32 data)
{
	switch (nv_ri32(chan->dev, data << 4) & 0xff) {
	case 0x30:
		nv04_graph_set_ctx_val(chan, 0x40000000, 0);
		return 0;
	case 0x72:
		nv04_graph_set_ctx_val(chan, 0x40000000, 0x40000000);
		return 0;
	}
	return 1;
}

static int
nv04_graph_mthd_bind_surf_dst(struct nouveau_channel *chan,
			      u32 class, u32 mthd, u32 data)
{
	switch (nv_ri32(chan->dev, data << 4) & 0xff) {
	case 0x30:
		nv04_graph_set_ctx_val(chan, 0x02000000, 0);
		return 0;
	case 0x58:
		nv04_graph_set_ctx_val(chan, 0x02000000, 0x02000000);
		return 0;
	}
	return 1;
}

static int
nv04_graph_mthd_bind_surf_src(struct nouveau_channel *chan,
			      u32 class, u32 mthd, u32 data)
{
	switch (nv_ri32(chan->dev, data << 4) & 0xff) {
	case 0x30:
		nv04_graph_set_ctx_val(chan, 0x04000000, 0);
		return 0;
	case 0x59:
		nv04_graph_set_ctx_val(chan, 0x04000000, 0x04000000);
		return 0;
	}
	return 1;
}

static int
nv04_graph_mthd_bind_surf_color(struct nouveau_channel *chan,
				u32 class, u32 mthd, u32 data)
{
	switch (nv_ri32(chan->dev, data << 4) & 0xff) {
	case 0x30:
		nv04_graph_set_ctx_val(chan, 0x02000000, 0);
		return 0;
	case 0x5a:
		nv04_graph_set_ctx_val(chan, 0x02000000, 0x02000000);
		return 0;
	}
	return 1;
}

static int
nv04_graph_mthd_bind_surf_zeta(struct nouveau_channel *chan,
			       u32 class, u32 mthd, u32 data)
{
	switch (nv_ri32(chan->dev, data << 4) & 0xff) {
	case 0x30:
		nv04_graph_set_ctx_val(chan, 0x04000000, 0);
		return 0;
	case 0x5b:
		nv04_graph_set_ctx_val(chan, 0x04000000, 0x04000000);
		return 0;
	}
	return 1;
}

static int
nv04_graph_mthd_bind_clip(struct nouveau_channel *chan,
			  u32 class, u32 mthd, u32 data)
{
	switch (nv_ri32(chan->dev, data << 4) & 0xff) {
	case 0x30:
		nv04_graph_set_ctx1(chan, 0x2000, 0);
		return 0;
	case 0x19:
		nv04_graph_set_ctx1(chan, 0x2000, 0x2000);
		return 0;
	}
	return 1;
}

static int
nv04_graph_mthd_bind_chroma(struct nouveau_channel *chan,
			    u32 class, u32 mthd, u32 data)
{
	switch (nv_ri32(chan->dev, data << 4) & 0xff) {
	case 0x30:
		nv04_graph_set_ctx1(chan, 0x1000, 0);
		return 0;
	/* Yes, for some reason even the old versions of objects
	 * accept 0x57 and not 0x17. Consistency be damned.
	 */
	case 0x57:
		nv04_graph_set_ctx1(chan, 0x1000, 0x1000);
		return 0;
	}
	return 1;
}

static int
nv04_graph_register(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (dev_priv->engine.graph.registered)
		return 0;

	/* dvd subpicture */
	NVOBJ_CLASS(dev, 0x0038, GR);

	/* m2mf */
	NVOBJ_CLASS(dev, 0x0039, GR);

	/* nv03 gdirect */
	NVOBJ_CLASS(dev, 0x004b, GR);
	NVOBJ_MTHD (dev, 0x004b, 0x0184, nv04_graph_mthd_bind_nv01_patt);
	NVOBJ_MTHD (dev, 0x004b, 0x0188, nv04_graph_mthd_bind_rop);
	NVOBJ_MTHD (dev, 0x004b, 0x018c, nv04_graph_mthd_bind_beta1);
	NVOBJ_MTHD (dev, 0x004b, 0x0190, nv04_graph_mthd_bind_surf_dst);
	NVOBJ_MTHD (dev, 0x004b, 0x02fc, nv04_graph_mthd_set_operation);

	/* nv04 gdirect */
	NVOBJ_CLASS(dev, 0x004a, GR);
	NVOBJ_MTHD (dev, 0x004a, 0x0188, nv04_graph_mthd_bind_nv04_patt);
	NVOBJ_MTHD (dev, 0x004a, 0x018c, nv04_graph_mthd_bind_rop);
	NVOBJ_MTHD (dev, 0x004a, 0x0190, nv04_graph_mthd_bind_beta1);
	NVOBJ_MTHD (dev, 0x004a, 0x0194, nv04_graph_mthd_bind_beta4);
	NVOBJ_MTHD (dev, 0x004a, 0x0198, nv04_graph_mthd_bind_surf2d);
	NVOBJ_MTHD (dev, 0x004a, 0x02fc, nv04_graph_mthd_set_operation);

	/* nv01 imageblit */
	NVOBJ_CLASS(dev, 0x001f, GR);
	NVOBJ_MTHD (dev, 0x001f, 0x0184, nv04_graph_mthd_bind_chroma);
	NVOBJ_MTHD (dev, 0x001f, 0x0188, nv04_graph_mthd_bind_clip);
	NVOBJ_MTHD (dev, 0x001f, 0x018c, nv04_graph_mthd_bind_nv01_patt);
	NVOBJ_MTHD (dev, 0x001f, 0x0190, nv04_graph_mthd_bind_rop);
	NVOBJ_MTHD (dev, 0x001f, 0x0194, nv04_graph_mthd_bind_beta1);
	NVOBJ_MTHD (dev, 0x001f, 0x0198, nv04_graph_mthd_bind_surf_dst);
	NVOBJ_MTHD (dev, 0x001f, 0x019c, nv04_graph_mthd_bind_surf_src);
	NVOBJ_MTHD (dev, 0x001f, 0x02fc, nv04_graph_mthd_set_operation);

	/* nv04 imageblit */
	NVOBJ_CLASS(dev, 0x005f, GR);
	NVOBJ_MTHD (dev, 0x005f, 0x0184, nv04_graph_mthd_bind_chroma);
	NVOBJ_MTHD (dev, 0x005f, 0x0188, nv04_graph_mthd_bind_clip);
	NVOBJ_MTHD (dev, 0x005f, 0x018c, nv04_graph_mthd_bind_nv04_patt);
	NVOBJ_MTHD (dev, 0x005f, 0x0190, nv04_graph_mthd_bind_rop);
	NVOBJ_MTHD (dev, 0x005f, 0x0194, nv04_graph_mthd_bind_beta1);
	NVOBJ_MTHD (dev, 0x005f, 0x0198, nv04_graph_mthd_bind_beta4);
	NVOBJ_MTHD (dev, 0x005f, 0x019c, nv04_graph_mthd_bind_surf2d);
	NVOBJ_MTHD (dev, 0x005f, 0x02fc, nv04_graph_mthd_set_operation);

	/* nv04 iifc */
	NVOBJ_CLASS(dev, 0x0060, GR);
	NVOBJ_MTHD (dev, 0x0060, 0x0188, nv04_graph_mthd_bind_chroma);
	NVOBJ_MTHD (dev, 0x0060, 0x018c, nv04_graph_mthd_bind_clip);
	NVOBJ_MTHD (dev, 0x0060, 0x0190, nv04_graph_mthd_bind_nv04_patt);
	NVOBJ_MTHD (dev, 0x0060, 0x0194, nv04_graph_mthd_bind_rop);
	NVOBJ_MTHD (dev, 0x0060, 0x0198, nv04_graph_mthd_bind_beta1);
	NVOBJ_MTHD (dev, 0x0060, 0x019c, nv04_graph_mthd_bind_beta4);
	NVOBJ_MTHD (dev, 0x0060, 0x01a0, nv04_graph_mthd_bind_surf2d_swzsurf);
	NVOBJ_MTHD (dev, 0x0060, 0x03e4, nv04_graph_mthd_set_operation);

	/* nv05 iifc */
	NVOBJ_CLASS(dev, 0x0064, GR);

	/* nv01 ifc */
	NVOBJ_CLASS(dev, 0x0021, GR);
	NVOBJ_MTHD (dev, 0x0021, 0x0184, nv04_graph_mthd_bind_chroma);
	NVOBJ_MTHD (dev, 0x0021, 0x0188, nv04_graph_mthd_bind_clip);
	NVOBJ_MTHD (dev, 0x0021, 0x018c, nv04_graph_mthd_bind_nv01_patt);
	NVOBJ_MTHD (dev, 0x0021, 0x0190, nv04_graph_mthd_bind_rop);
	NVOBJ_MTHD (dev, 0x0021, 0x0194, nv04_graph_mthd_bind_beta1);
	NVOBJ_MTHD (dev, 0x0021, 0x0198, nv04_graph_mthd_bind_surf_dst);
	NVOBJ_MTHD (dev, 0x0021, 0x02fc, nv04_graph_mthd_set_operation);

	/* nv04 ifc */
	NVOBJ_CLASS(dev, 0x0061, GR);
	NVOBJ_MTHD (dev, 0x0061, 0x0184, nv04_graph_mthd_bind_chroma);
	NVOBJ_MTHD (dev, 0x0061, 0x0188, nv04_graph_mthd_bind_clip);
	NVOBJ_MTHD (dev, 0x0061, 0x018c, nv04_graph_mthd_bind_nv04_patt);
	NVOBJ_MTHD (dev, 0x0061, 0x0190, nv04_graph_mthd_bind_rop);
	NVOBJ_MTHD (dev, 0x0061, 0x0194, nv04_graph_mthd_bind_beta1);
	NVOBJ_MTHD (dev, 0x0061, 0x0198, nv04_graph_mthd_bind_beta4);
	NVOBJ_MTHD (dev, 0x0061, 0x019c, nv04_graph_mthd_bind_surf2d);
	NVOBJ_MTHD (dev, 0x0061, 0x02fc, nv04_graph_mthd_set_operation);

	/* nv05 ifc */
	NVOBJ_CLASS(dev, 0x0065, GR);

	/* nv03 sifc */
	NVOBJ_CLASS(dev, 0x0036, GR);
	NVOBJ_MTHD (dev, 0x0036, 0x0184, nv04_graph_mthd_bind_chroma);
	NVOBJ_MTHD (dev, 0x0036, 0x0188, nv04_graph_mthd_bind_nv01_patt);
	NVOBJ_MTHD (dev, 0x0036, 0x018c, nv04_graph_mthd_bind_rop);
	NVOBJ_MTHD (dev, 0x0036, 0x0190, nv04_graph_mthd_bind_beta1);
	NVOBJ_MTHD (dev, 0x0036, 0x0194, nv04_graph_mthd_bind_surf_dst);
	NVOBJ_MTHD (dev, 0x0036, 0x02fc, nv04_graph_mthd_set_operation);

	/* nv04 sifc */
	NVOBJ_CLASS(dev, 0x0076, GR);
	NVOBJ_MTHD (dev, 0x0076, 0x0184, nv04_graph_mthd_bind_chroma);
	NVOBJ_MTHD (dev, 0x0076, 0x0188, nv04_graph_mthd_bind_nv04_patt);
	NVOBJ_MTHD (dev, 0x0076, 0x018c, nv04_graph_mthd_bind_rop);
	NVOBJ_MTHD (dev, 0x0076, 0x0190, nv04_graph_mthd_bind_beta1);
	NVOBJ_MTHD (dev, 0x0076, 0x0194, nv04_graph_mthd_bind_beta4);
	NVOBJ_MTHD (dev, 0x0076, 0x0198, nv04_graph_mthd_bind_surf2d);
	NVOBJ_MTHD (dev, 0x0076, 0x02fc, nv04_graph_mthd_set_operation);

	/* nv05 sifc */
	NVOBJ_CLASS(dev, 0x0066, GR);

	/* nv03 sifm */
	NVOBJ_CLASS(dev, 0x0037, GR);
	NVOBJ_MTHD (dev, 0x0037, 0x0188, nv04_graph_mthd_bind_nv01_patt);
	NVOBJ_MTHD (dev, 0x0037, 0x018c, nv04_graph_mthd_bind_rop);
	NVOBJ_MTHD (dev, 0x0037, 0x0190, nv04_graph_mthd_bind_beta1);
	NVOBJ_MTHD (dev, 0x0037, 0x0194, nv04_graph_mthd_bind_surf_dst);
	NVOBJ_MTHD (dev, 0x0037, 0x0304, nv04_graph_mthd_set_operation);

	/* nv04 sifm */
	NVOBJ_CLASS(dev, 0x0077, GR);
	NVOBJ_MTHD (dev, 0x0077, 0x0188, nv04_graph_mthd_bind_nv04_patt);
	NVOBJ_MTHD (dev, 0x0077, 0x018c, nv04_graph_mthd_bind_rop);
	NVOBJ_MTHD (dev, 0x0077, 0x0190, nv04_graph_mthd_bind_beta1);
	NVOBJ_MTHD (dev, 0x0077, 0x0194, nv04_graph_mthd_bind_beta4);
	NVOBJ_MTHD (dev, 0x0077, 0x0198, nv04_graph_mthd_bind_surf2d_swzsurf);
	NVOBJ_MTHD (dev, 0x0077, 0x0304, nv04_graph_mthd_set_operation);

	/* null */
	NVOBJ_CLASS(dev, 0x0030, GR);

	/* surf2d */
	NVOBJ_CLASS(dev, 0x0042, GR);

	/* rop */
	NVOBJ_CLASS(dev, 0x0043, GR);

	/* beta1 */
	NVOBJ_CLASS(dev, 0x0012, GR);

	/* beta4 */
	NVOBJ_CLASS(dev, 0x0072, GR);

	/* cliprect */
	NVOBJ_CLASS(dev, 0x0019, GR);

	/* nv01 pattern */
	NVOBJ_CLASS(dev, 0x0018, GR);

	/* nv04 pattern */
	NVOBJ_CLASS(dev, 0x0044, GR);

	/* swzsurf */
	NVOBJ_CLASS(dev, 0x0052, GR);

	/* surf3d */
	NVOBJ_CLASS(dev, 0x0053, GR);
	NVOBJ_MTHD (dev, 0x0053, 0x02f8, nv04_graph_mthd_surf3d_clip_h);
	NVOBJ_MTHD (dev, 0x0053, 0x02fc, nv04_graph_mthd_surf3d_clip_v);

	/* nv03 tex_tri */
	NVOBJ_CLASS(dev, 0x0048, GR);
	NVOBJ_MTHD (dev, 0x0048, 0x0188, nv04_graph_mthd_bind_clip);
	NVOBJ_MTHD (dev, 0x0048, 0x018c, nv04_graph_mthd_bind_surf_color);
	NVOBJ_MTHD (dev, 0x0048, 0x0190, nv04_graph_mthd_bind_surf_zeta);

	/* tex_tri */
	NVOBJ_CLASS(dev, 0x0054, GR);

	/* multitex_tri */
	NVOBJ_CLASS(dev, 0x0055, GR);

	/* nv01 chroma */
	NVOBJ_CLASS(dev, 0x0017, GR);

	/* nv04 chroma */
	NVOBJ_CLASS(dev, 0x0057, GR);

	/* surf_dst */
	NVOBJ_CLASS(dev, 0x0058, GR);

	/* surf_src */
	NVOBJ_CLASS(dev, 0x0059, GR);

	/* surf_color */
	NVOBJ_CLASS(dev, 0x005a, GR);

	/* surf_zeta */
	NVOBJ_CLASS(dev, 0x005b, GR);

	/* nv01 line */
	NVOBJ_CLASS(dev, 0x001c, GR);
	NVOBJ_MTHD (dev, 0x001c, 0x0184, nv04_graph_mthd_bind_clip);
	NVOBJ_MTHD (dev, 0x001c, 0x0188, nv04_graph_mthd_bind_nv01_patt);
	NVOBJ_MTHD (dev, 0x001c, 0x018c, nv04_graph_mthd_bind_rop);
	NVOBJ_MTHD (dev, 0x001c, 0x0190, nv04_graph_mthd_bind_beta1);
	NVOBJ_MTHD (dev, 0x001c, 0x0194, nv04_graph_mthd_bind_surf_dst);
	NVOBJ_MTHD (dev, 0x001c, 0x02fc, nv04_graph_mthd_set_operation);

	/* nv04 line */
	NVOBJ_CLASS(dev, 0x005c, GR);
	NVOBJ_MTHD (dev, 0x005c, 0x0184, nv04_graph_mthd_bind_clip);
	NVOBJ_MTHD (dev, 0x005c, 0x0188, nv04_graph_mthd_bind_nv04_patt);
	NVOBJ_MTHD (dev, 0x005c, 0x018c, nv04_graph_mthd_bind_rop);
	NVOBJ_MTHD (dev, 0x005c, 0x0190, nv04_graph_mthd_bind_beta1);
	NVOBJ_MTHD (dev, 0x005c, 0x0194, nv04_graph_mthd_bind_beta4);
	NVOBJ_MTHD (dev, 0x005c, 0x0198, nv04_graph_mthd_bind_surf2d);
	NVOBJ_MTHD (dev, 0x005c, 0x02fc, nv04_graph_mthd_set_operation);

	/* nv01 tri */
	NVOBJ_CLASS(dev, 0x001d, GR);
	NVOBJ_MTHD (dev, 0x001d, 0x0184, nv04_graph_mthd_bind_clip);
	NVOBJ_MTHD (dev, 0x001d, 0x0188, nv04_graph_mthd_bind_nv01_patt);
	NVOBJ_MTHD (dev, 0x001d, 0x018c, nv04_graph_mthd_bind_rop);
	NVOBJ_MTHD (dev, 0x001d, 0x0190, nv04_graph_mthd_bind_beta1);
	NVOBJ_MTHD (dev, 0x001d, 0x0194, nv04_graph_mthd_bind_surf_dst);
	NVOBJ_MTHD (dev, 0x001d, 0x02fc, nv04_graph_mthd_set_operation);

	/* nv04 tri */
	NVOBJ_CLASS(dev, 0x005d, GR);
	NVOBJ_MTHD (dev, 0x005d, 0x0184, nv04_graph_mthd_bind_clip);
	NVOBJ_MTHD (dev, 0x005d, 0x0188, nv04_graph_mthd_bind_nv04_patt);
	NVOBJ_MTHD (dev, 0x005d, 0x018c, nv04_graph_mthd_bind_rop);
	NVOBJ_MTHD (dev, 0x005d, 0x0190, nv04_graph_mthd_bind_beta1);
	NVOBJ_MTHD (dev, 0x005d, 0x0194, nv04_graph_mthd_bind_beta4);
	NVOBJ_MTHD (dev, 0x005d, 0x0198, nv04_graph_mthd_bind_surf2d);
	NVOBJ_MTHD (dev, 0x005d, 0x02fc, nv04_graph_mthd_set_operation);

	/* nv01 rect */
	NVOBJ_CLASS(dev, 0x001e, GR);
	NVOBJ_MTHD (dev, 0x001e, 0x0184, nv04_graph_mthd_bind_clip);
	NVOBJ_MTHD (dev, 0x001e, 0x0188, nv04_graph_mthd_bind_nv01_patt);
	NVOBJ_MTHD (dev, 0x001e, 0x018c, nv04_graph_mthd_bind_rop);
	NVOBJ_MTHD (dev, 0x001e, 0x0190, nv04_graph_mthd_bind_beta1);
	NVOBJ_MTHD (dev, 0x001e, 0x0194, nv04_graph_mthd_bind_surf_dst);
	NVOBJ_MTHD (dev, 0x001e, 0x02fc, nv04_graph_mthd_set_operation);

	/* nv04 rect */
	NVOBJ_CLASS(dev, 0x005e, GR);
	NVOBJ_MTHD (dev, 0x005e, 0x0184, nv04_graph_mthd_bind_clip);
	NVOBJ_MTHD (dev, 0x005e, 0x0188, nv04_graph_mthd_bind_nv04_patt);
	NVOBJ_MTHD (dev, 0x005e, 0x018c, nv04_graph_mthd_bind_rop);
	NVOBJ_MTHD (dev, 0x005e, 0x0190, nv04_graph_mthd_bind_beta1);
	NVOBJ_MTHD (dev, 0x005e, 0x0194, nv04_graph_mthd_bind_beta4);
	NVOBJ_MTHD (dev, 0x005e, 0x0198, nv04_graph_mthd_bind_surf2d);
	NVOBJ_MTHD (dev, 0x005e, 0x02fc, nv04_graph_mthd_set_operation);

	/* nvsw */
	NVOBJ_CLASS(dev, 0x506e, SW);
	NVOBJ_MTHD (dev, 0x506e, 0x0150, nv04_graph_mthd_set_ref);
	NVOBJ_MTHD (dev, 0x506e, 0x0500, nv04_graph_mthd_page_flip);

	dev_priv->engine.graph.registered = true;
	return 0;
};

static struct nouveau_bitfield nv04_graph_intr[] = {
	{ NV_PGRAPH_INTR_NOTIFY, "NOTIFY" },
	{}
};

static struct nouveau_bitfield nv04_graph_nstatus[] =
{
	{ NV04_PGRAPH_NSTATUS_STATE_IN_USE,       "STATE_IN_USE" },
	{ NV04_PGRAPH_NSTATUS_INVALID_STATE,      "INVALID_STATE" },
	{ NV04_PGRAPH_NSTATUS_BAD_ARGUMENT,       "BAD_ARGUMENT" },
	{ NV04_PGRAPH_NSTATUS_PROTECTION_FAULT,   "PROTECTION_FAULT" },
	{}
};

struct nouveau_bitfield nv04_graph_nsource[] =
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
	{}
};

static void
nv04_graph_isr(struct drm_device *dev)
{
	u32 stat;

	while ((stat = nv_rd32(dev, NV03_PGRAPH_INTR))) {
		u32 nsource = nv_rd32(dev, NV03_PGRAPH_NSOURCE);
		u32 nstatus = nv_rd32(dev, NV03_PGRAPH_NSTATUS);
		u32 addr = nv_rd32(dev, NV04_PGRAPH_TRAPPED_ADDR);
		u32 chid = (addr & 0x0f000000) >> 24;
		u32 subc = (addr & 0x0000e000) >> 13;
		u32 mthd = (addr & 0x00001ffc);
		u32 data = nv_rd32(dev, NV04_PGRAPH_TRAPPED_DATA);
		u32 class = nv_rd32(dev, 0x400180 + subc * 4) & 0xff;
		u32 show = stat;

		if (stat & NV_PGRAPH_INTR_NOTIFY) {
			if (nsource & NV03_PGRAPH_NSOURCE_ILLEGAL_MTHD) {
				if (!nouveau_gpuobj_mthd_call2(dev, chid, class, mthd, data))
					show &= ~NV_PGRAPH_INTR_NOTIFY;
			}
		}

		if (stat & NV_PGRAPH_INTR_CONTEXT_SWITCH) {
			nv_wr32(dev, NV03_PGRAPH_INTR, NV_PGRAPH_INTR_CONTEXT_SWITCH);
			stat &= ~NV_PGRAPH_INTR_CONTEXT_SWITCH;
			show &= ~NV_PGRAPH_INTR_CONTEXT_SWITCH;
			nv04_graph_context_switch(dev);
		}

		nv_wr32(dev, NV03_PGRAPH_INTR, stat);
		nv_wr32(dev, NV04_PGRAPH_FIFO, 0x00000001);

		if (show && nouveau_ratelimit()) {
			NV_INFO(dev, "PGRAPH -");
			nouveau_bitfield_print(nv04_graph_intr, show);
			printk(" nsource:");
			nouveau_bitfield_print(nv04_graph_nsource, nsource);
			printk(" nstatus:");
			nouveau_bitfield_print(nv04_graph_nstatus, nstatus);
			printk("\n");
			NV_INFO(dev, "PGRAPH - ch %d/%d class 0x%04x "
				     "mthd 0x%04x data 0x%08x\n",
				chid, subc, class, mthd, data);
		}
	}
}
