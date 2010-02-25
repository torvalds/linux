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
	int nv04[ARRAY_SIZE(nv04_graph_ctx_regs)];
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

	return dev_priv->fifos[chid];
}

void
nv04_graph_context_switch(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	struct nouveau_channel *chan = NULL;
	int chid;

	pgraph->fifo_access(dev, false);
	nouveau_wait_for_idle(dev);

	/* If previous context is valid, we need to save it */
	pgraph->unload_context(dev);

	/* Load context for next channel */
	chid = dev_priv->engine.fifo.channel_id(dev);
	chan = dev_priv->fifos[chid];
	if (chan)
		nv04_graph_load_context(chan);

	pgraph->fifo_access(dev, true);
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
	struct graph_state *pgraph_ctx = chan->pgraph_ctx;

	kfree(pgraph_ctx);
	chan->pgraph_ctx = NULL;
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

	nv_wr32(dev, NV03_PMC_ENABLE, nv_rd32(dev, NV03_PMC_ENABLE) &
			~NV_PMC_ENABLE_PGRAPH);
	nv_wr32(dev, NV03_PMC_ENABLE, nv_rd32(dev, NV03_PMC_ENABLE) |
			 NV_PMC_ENABLE_PGRAPH);

	/* Enable PGRAPH interrupts */
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
nv04_graph_mthd_set_ref(struct nouveau_channel *chan, int grclass,
			int mthd, uint32_t data)
{
	chan->fence.last_sequence_irq = data;
	nouveau_fence_handler(chan->dev, chan->id);
	return 0;
}

static int
nv04_graph_mthd_set_operation(struct nouveau_channel *chan, int grclass,
			      int mthd, uint32_t data)
{
	struct drm_device *dev = chan->dev;
	uint32_t instance = (nv_rd32(dev, NV04_PGRAPH_CTX_SWITCH4) & 0xffff) << 4;
	int subc = (nv_rd32(dev, NV04_PGRAPH_TRAPPED_ADDR) >> 13) & 0x7;
	uint32_t tmp;

	tmp  = nv_ri32(dev, instance);
	tmp &= ~0x00038000;
	tmp |= ((data & 7) << 15);

	nv_wi32(dev, instance, tmp);
	nv_wr32(dev, NV04_PGRAPH_CTX_SWITCH1, tmp);
	nv_wr32(dev, NV04_PGRAPH_CTX_CACHE1 + (subc<<2), tmp);
	return 0;
}

static struct nouveau_pgraph_object_method nv04_graph_mthds_sw[] = {
	{ 0x0150, nv04_graph_mthd_set_ref },
	{}
};

static struct nouveau_pgraph_object_method nv04_graph_mthds_set_operation[] = {
	{ 0x02fc, nv04_graph_mthd_set_operation },
	{},
};

struct nouveau_pgraph_object_class nv04_graph_grclass[] = {
	{ 0x0039, false, NULL },
	{ 0x004a, false, nv04_graph_mthds_set_operation }, /* gdirect */
	{ 0x005f, false, nv04_graph_mthds_set_operation }, /* imageblit */
	{ 0x0061, false, nv04_graph_mthds_set_operation }, /* ifc */
	{ 0x0077, false, nv04_graph_mthds_set_operation }, /* sifm */
	{ 0x0030, false, NULL }, /* null */
	{ 0x0042, false, NULL }, /* surf2d */
	{ 0x0043, false, NULL }, /* rop */
	{ 0x0012, false, NULL }, /* beta1 */
	{ 0x0072, false, NULL }, /* beta4 */
	{ 0x0019, false, NULL }, /* cliprect */
	{ 0x0044, false, NULL }, /* pattern */
	{ 0x0052, false, NULL }, /* swzsurf */
	{ 0x0053, false, NULL }, /* surf3d */
	{ 0x0054, false, NULL }, /* tex_tri */
	{ 0x0055, false, NULL }, /* multitex_tri */
	{ 0x506e, true, nv04_graph_mthds_sw },
	{}
};

