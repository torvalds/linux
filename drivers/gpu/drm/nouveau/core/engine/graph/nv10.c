/*
 * Copyright 2007 Matthieu CASTET <castet.matthieu@free.fr>
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

#include <core/os.h>
#include <core/class.h>
#include <core/handle.h>

#include <subdev/fb.h>

#include <engine/fifo.h>
#include <engine/graph.h>

#include "regs.h"

struct pipe_state {
	u32 pipe_0x0000[0x040/4];
	u32 pipe_0x0040[0x010/4];
	u32 pipe_0x0200[0x0c0/4];
	u32 pipe_0x4400[0x080/4];
	u32 pipe_0x6400[0x3b0/4];
	u32 pipe_0x6800[0x2f0/4];
	u32 pipe_0x6c00[0x030/4];
	u32 pipe_0x7000[0x130/4];
	u32 pipe_0x7400[0x0c0/4];
	u32 pipe_0x7800[0x0c0/4];
};

static int nv10_graph_ctx_regs[] = {
	NV10_PGRAPH_CTX_SWITCH(0),
	NV10_PGRAPH_CTX_SWITCH(1),
	NV10_PGRAPH_CTX_SWITCH(2),
	NV10_PGRAPH_CTX_SWITCH(3),
	NV10_PGRAPH_CTX_SWITCH(4),
	NV10_PGRAPH_CTX_CACHE(0, 0),
	NV10_PGRAPH_CTX_CACHE(0, 1),
	NV10_PGRAPH_CTX_CACHE(0, 2),
	NV10_PGRAPH_CTX_CACHE(0, 3),
	NV10_PGRAPH_CTX_CACHE(0, 4),
	NV10_PGRAPH_CTX_CACHE(1, 0),
	NV10_PGRAPH_CTX_CACHE(1, 1),
	NV10_PGRAPH_CTX_CACHE(1, 2),
	NV10_PGRAPH_CTX_CACHE(1, 3),
	NV10_PGRAPH_CTX_CACHE(1, 4),
	NV10_PGRAPH_CTX_CACHE(2, 0),
	NV10_PGRAPH_CTX_CACHE(2, 1),
	NV10_PGRAPH_CTX_CACHE(2, 2),
	NV10_PGRAPH_CTX_CACHE(2, 3),
	NV10_PGRAPH_CTX_CACHE(2, 4),
	NV10_PGRAPH_CTX_CACHE(3, 0),
	NV10_PGRAPH_CTX_CACHE(3, 1),
	NV10_PGRAPH_CTX_CACHE(3, 2),
	NV10_PGRAPH_CTX_CACHE(3, 3),
	NV10_PGRAPH_CTX_CACHE(3, 4),
	NV10_PGRAPH_CTX_CACHE(4, 0),
	NV10_PGRAPH_CTX_CACHE(4, 1),
	NV10_PGRAPH_CTX_CACHE(4, 2),
	NV10_PGRAPH_CTX_CACHE(4, 3),
	NV10_PGRAPH_CTX_CACHE(4, 4),
	NV10_PGRAPH_CTX_CACHE(5, 0),
	NV10_PGRAPH_CTX_CACHE(5, 1),
	NV10_PGRAPH_CTX_CACHE(5, 2),
	NV10_PGRAPH_CTX_CACHE(5, 3),
	NV10_PGRAPH_CTX_CACHE(5, 4),
	NV10_PGRAPH_CTX_CACHE(6, 0),
	NV10_PGRAPH_CTX_CACHE(6, 1),
	NV10_PGRAPH_CTX_CACHE(6, 2),
	NV10_PGRAPH_CTX_CACHE(6, 3),
	NV10_PGRAPH_CTX_CACHE(6, 4),
	NV10_PGRAPH_CTX_CACHE(7, 0),
	NV10_PGRAPH_CTX_CACHE(7, 1),
	NV10_PGRAPH_CTX_CACHE(7, 2),
	NV10_PGRAPH_CTX_CACHE(7, 3),
	NV10_PGRAPH_CTX_CACHE(7, 4),
	NV10_PGRAPH_CTX_USER,
	NV04_PGRAPH_DMA_START_0,
	NV04_PGRAPH_DMA_START_1,
	NV04_PGRAPH_DMA_LENGTH,
	NV04_PGRAPH_DMA_MISC,
	NV10_PGRAPH_DMA_PITCH,
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
	NV10_PGRAPH_SURFACE,
	NV10_PGRAPH_STATE,
	NV04_PGRAPH_BSWIZZLE2,
	NV04_PGRAPH_BSWIZZLE5,
	NV04_PGRAPH_BPIXEL,
	NV10_PGRAPH_NOTIFY,
	NV04_PGRAPH_PATT_COLOR0,
	NV04_PGRAPH_PATT_COLOR1,
	NV04_PGRAPH_PATT_COLORRAM, /* 64 values from 0x400900 to 0x4009fc */
	0x00400904,
	0x00400908,
	0x0040090c,
	0x00400910,
	0x00400914,
	0x00400918,
	0x0040091c,
	0x00400920,
	0x00400924,
	0x00400928,
	0x0040092c,
	0x00400930,
	0x00400934,
	0x00400938,
	0x0040093c,
	0x00400940,
	0x00400944,
	0x00400948,
	0x0040094c,
	0x00400950,
	0x00400954,
	0x00400958,
	0x0040095c,
	0x00400960,
	0x00400964,
	0x00400968,
	0x0040096c,
	0x00400970,
	0x00400974,
	0x00400978,
	0x0040097c,
	0x00400980,
	0x00400984,
	0x00400988,
	0x0040098c,
	0x00400990,
	0x00400994,
	0x00400998,
	0x0040099c,
	0x004009a0,
	0x004009a4,
	0x004009a8,
	0x004009ac,
	0x004009b0,
	0x004009b4,
	0x004009b8,
	0x004009bc,
	0x004009c0,
	0x004009c4,
	0x004009c8,
	0x004009cc,
	0x004009d0,
	0x004009d4,
	0x004009d8,
	0x004009dc,
	0x004009e0,
	0x004009e4,
	0x004009e8,
	0x004009ec,
	0x004009f0,
	0x004009f4,
	0x004009f8,
	0x004009fc,
	NV04_PGRAPH_PATTERN,	/* 2 values from 0x400808 to 0x40080c */
	0x0040080c,
	NV04_PGRAPH_PATTERN_SHAPE,
	NV03_PGRAPH_MONO_COLOR0,
	NV04_PGRAPH_ROP3,
	NV04_PGRAPH_CHROMA,
	NV04_PGRAPH_BETA_AND,
	NV04_PGRAPH_BETA_PREMULT,
	0x00400e70,
	0x00400e74,
	0x00400e78,
	0x00400e7c,
	0x00400e80,
	0x00400e84,
	0x00400e88,
	0x00400e8c,
	0x00400ea0,
	0x00400ea4,
	0x00400ea8,
	0x00400e90,
	0x00400e94,
	0x00400e98,
	0x00400e9c,
	NV10_PGRAPH_WINDOWCLIP_HORIZONTAL, /* 8 values from 0x400f00-0x400f1c */
	NV10_PGRAPH_WINDOWCLIP_VERTICAL,   /* 8 values from 0x400f20-0x400f3c */
	0x00400f04,
	0x00400f24,
	0x00400f08,
	0x00400f28,
	0x00400f0c,
	0x00400f2c,
	0x00400f10,
	0x00400f30,
	0x00400f14,
	0x00400f34,
	0x00400f18,
	0x00400f38,
	0x00400f1c,
	0x00400f3c,
	NV10_PGRAPH_XFMODE0,
	NV10_PGRAPH_XFMODE1,
	NV10_PGRAPH_GLOBALSTATE0,
	NV10_PGRAPH_GLOBALSTATE1,
	NV04_PGRAPH_STORED_FMT,
	NV04_PGRAPH_SOURCE_COLOR,
	NV03_PGRAPH_ABS_X_RAM,	/* 32 values from 0x400400 to 0x40047c */
	NV03_PGRAPH_ABS_Y_RAM,	/* 32 values from 0x400480 to 0x4004fc */
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
	NV03_PGRAPH_ABS_UCLIP_XMIN,
	NV03_PGRAPH_ABS_UCLIP_XMAX,
	NV03_PGRAPH_ABS_UCLIP_YMIN,
	NV03_PGRAPH_ABS_UCLIP_YMAX,
	0x00400550,
	0x00400558,
	0x00400554,
	0x0040055c,
	NV03_PGRAPH_ABS_UCLIPA_XMIN,
	NV03_PGRAPH_ABS_UCLIPA_XMAX,
	NV03_PGRAPH_ABS_UCLIPA_YMIN,
	NV03_PGRAPH_ABS_UCLIPA_YMAX,
	NV03_PGRAPH_ABS_ICLIP_XMAX,
	NV03_PGRAPH_ABS_ICLIP_YMAX,
	NV03_PGRAPH_XY_LOGIC_MISC0,
	NV03_PGRAPH_XY_LOGIC_MISC1,
	NV03_PGRAPH_XY_LOGIC_MISC2,
	NV03_PGRAPH_XY_LOGIC_MISC3,
	NV03_PGRAPH_CLIPX_0,
	NV03_PGRAPH_CLIPX_1,
	NV03_PGRAPH_CLIPY_0,
	NV03_PGRAPH_CLIPY_1,
	NV10_PGRAPH_COMBINER0_IN_ALPHA,
	NV10_PGRAPH_COMBINER1_IN_ALPHA,
	NV10_PGRAPH_COMBINER0_IN_RGB,
	NV10_PGRAPH_COMBINER1_IN_RGB,
	NV10_PGRAPH_COMBINER_COLOR0,
	NV10_PGRAPH_COMBINER_COLOR1,
	NV10_PGRAPH_COMBINER0_OUT_ALPHA,
	NV10_PGRAPH_COMBINER1_OUT_ALPHA,
	NV10_PGRAPH_COMBINER0_OUT_RGB,
	NV10_PGRAPH_COMBINER1_OUT_RGB,
	NV10_PGRAPH_COMBINER_FINAL0,
	NV10_PGRAPH_COMBINER_FINAL1,
	0x00400e00,
	0x00400e04,
	0x00400e08,
	0x00400e0c,
	0x00400e10,
	0x00400e14,
	0x00400e18,
	0x00400e1c,
	0x00400e20,
	0x00400e24,
	0x00400e28,
	0x00400e2c,
	0x00400e30,
	0x00400e34,
	0x00400e38,
	0x00400e3c,
	NV04_PGRAPH_PASSTHRU_0,
	NV04_PGRAPH_PASSTHRU_1,
	NV04_PGRAPH_PASSTHRU_2,
	NV10_PGRAPH_DIMX_TEXTURE,
	NV10_PGRAPH_WDIMX_TEXTURE,
	NV10_PGRAPH_DVD_COLORFMT,
	NV10_PGRAPH_SCALED_FORMAT,
	NV04_PGRAPH_MISC24_0,
	NV04_PGRAPH_MISC24_1,
	NV04_PGRAPH_MISC24_2,
	NV03_PGRAPH_X_MISC,
	NV03_PGRAPH_Y_MISC,
	NV04_PGRAPH_VALID1,
	NV04_PGRAPH_VALID2,
};

static int nv17_graph_ctx_regs[] = {
	NV10_PGRAPH_DEBUG_4,
	0x004006b0,
	0x00400eac,
	0x00400eb0,
	0x00400eb4,
	0x00400eb8,
	0x00400ebc,
	0x00400ec0,
	0x00400ec4,
	0x00400ec8,
	0x00400ecc,
	0x00400ed0,
	0x00400ed4,
	0x00400ed8,
	0x00400edc,
	0x00400ee0,
	0x00400a00,
	0x00400a04,
};

struct nv10_graph_priv {
	struct nouveau_graph base;
	struct nv10_graph_chan *chan[32];
	spinlock_t lock;
};

struct nv10_graph_chan {
	struct nouveau_object base;
	int chid;
	int nv10[ARRAY_SIZE(nv10_graph_ctx_regs)];
	int nv17[ARRAY_SIZE(nv17_graph_ctx_regs)];
	struct pipe_state pipe_state;
	u32 lma_window[4];
};


static inline struct nv10_graph_priv *
nv10_graph_priv(struct nv10_graph_chan *chan)
{
	return (void *)nv_object(chan)->engine;
}

/*******************************************************************************
 * Graphics object classes
 ******************************************************************************/

#define PIPE_SAVE(priv, state, addr)					\
	do {								\
		int __i;						\
		nv_wr32(priv, NV10_PGRAPH_PIPE_ADDRESS, addr);		\
		for (__i = 0; __i < ARRAY_SIZE(state); __i++)		\
			state[__i] = nv_rd32(priv, NV10_PGRAPH_PIPE_DATA); \
	} while (0)

#define PIPE_RESTORE(priv, state, addr)					\
	do {								\
		int __i;						\
		nv_wr32(priv, NV10_PGRAPH_PIPE_ADDRESS, addr);		\
		for (__i = 0; __i < ARRAY_SIZE(state); __i++)		\
			nv_wr32(priv, NV10_PGRAPH_PIPE_DATA, state[__i]); \
	} while (0)

static struct nouveau_oclass
nv10_graph_sclass[] = {
	{ 0x0012, &nv04_graph_ofuncs }, /* beta1 */
	{ 0x0019, &nv04_graph_ofuncs }, /* clip */
	{ 0x0030, &nv04_graph_ofuncs }, /* null */
	{ 0x0039, &nv04_graph_ofuncs }, /* m2mf */
	{ 0x0043, &nv04_graph_ofuncs }, /* rop */
	{ 0x0044, &nv04_graph_ofuncs }, /* pattern */
	{ 0x004a, &nv04_graph_ofuncs }, /* gdi */
	{ 0x0052, &nv04_graph_ofuncs }, /* swzsurf */
	{ 0x005f, &nv04_graph_ofuncs }, /* blit */
	{ 0x0062, &nv04_graph_ofuncs }, /* surf2d */
	{ 0x0072, &nv04_graph_ofuncs }, /* beta4 */
	{ 0x0089, &nv04_graph_ofuncs }, /* sifm */
	{ 0x008a, &nv04_graph_ofuncs }, /* ifc */
	{ 0x009f, &nv04_graph_ofuncs }, /* blit */
	{ 0x0093, &nv04_graph_ofuncs }, /* surf3d */
	{ 0x0094, &nv04_graph_ofuncs }, /* ttri */
	{ 0x0095, &nv04_graph_ofuncs }, /* mtri */
	{ 0x0056, &nv04_graph_ofuncs }, /* celcius */
	{},
};

static struct nouveau_oclass
nv15_graph_sclass[] = {
	{ 0x0012, &nv04_graph_ofuncs }, /* beta1 */
	{ 0x0019, &nv04_graph_ofuncs }, /* clip */
	{ 0x0030, &nv04_graph_ofuncs }, /* null */
	{ 0x0039, &nv04_graph_ofuncs }, /* m2mf */
	{ 0x0043, &nv04_graph_ofuncs }, /* rop */
	{ 0x0044, &nv04_graph_ofuncs }, /* pattern */
	{ 0x004a, &nv04_graph_ofuncs }, /* gdi */
	{ 0x0052, &nv04_graph_ofuncs }, /* swzsurf */
	{ 0x005f, &nv04_graph_ofuncs }, /* blit */
	{ 0x0062, &nv04_graph_ofuncs }, /* surf2d */
	{ 0x0072, &nv04_graph_ofuncs }, /* beta4 */
	{ 0x0089, &nv04_graph_ofuncs }, /* sifm */
	{ 0x008a, &nv04_graph_ofuncs }, /* ifc */
	{ 0x009f, &nv04_graph_ofuncs }, /* blit */
	{ 0x0093, &nv04_graph_ofuncs }, /* surf3d */
	{ 0x0094, &nv04_graph_ofuncs }, /* ttri */
	{ 0x0095, &nv04_graph_ofuncs }, /* mtri */
	{ 0x0096, &nv04_graph_ofuncs }, /* celcius */
	{},
};

static int
nv17_graph_mthd_lma_window(struct nouveau_object *object, u32 mthd,
			   void *args, u32 size)
{
	struct nv10_graph_chan *chan = (void *)object->parent;
	struct nv10_graph_priv *priv = nv10_graph_priv(chan);
	struct pipe_state *pipe = &chan->pipe_state;
	u32 pipe_0x0040[1], pipe_0x64c0[8], pipe_0x6a80[3], pipe_0x6ab0[3];
	u32 xfmode0, xfmode1;
	u32 data = *(u32 *)args;
	int i;

	chan->lma_window[(mthd - 0x1638) / 4] = data;

	if (mthd != 0x1644)
		return 0;

	nv04_graph_idle(priv);

	PIPE_SAVE(priv, pipe_0x0040, 0x0040);
	PIPE_SAVE(priv, pipe->pipe_0x0200, 0x0200);

	PIPE_RESTORE(priv, chan->lma_window, 0x6790);

	nv04_graph_idle(priv);

	xfmode0 = nv_rd32(priv, NV10_PGRAPH_XFMODE0);
	xfmode1 = nv_rd32(priv, NV10_PGRAPH_XFMODE1);

	PIPE_SAVE(priv, pipe->pipe_0x4400, 0x4400);
	PIPE_SAVE(priv, pipe_0x64c0, 0x64c0);
	PIPE_SAVE(priv, pipe_0x6ab0, 0x6ab0);
	PIPE_SAVE(priv, pipe_0x6a80, 0x6a80);

	nv04_graph_idle(priv);

	nv_wr32(priv, NV10_PGRAPH_XFMODE0, 0x10000000);
	nv_wr32(priv, NV10_PGRAPH_XFMODE1, 0x00000000);
	nv_wr32(priv, NV10_PGRAPH_PIPE_ADDRESS, 0x000064c0);
	for (i = 0; i < 4; i++)
		nv_wr32(priv, NV10_PGRAPH_PIPE_DATA, 0x3f800000);
	for (i = 0; i < 4; i++)
		nv_wr32(priv, NV10_PGRAPH_PIPE_DATA, 0x00000000);

	nv_wr32(priv, NV10_PGRAPH_PIPE_ADDRESS, 0x00006ab0);
	for (i = 0; i < 3; i++)
		nv_wr32(priv, NV10_PGRAPH_PIPE_DATA, 0x3f800000);

	nv_wr32(priv, NV10_PGRAPH_PIPE_ADDRESS, 0x00006a80);
	for (i = 0; i < 3; i++)
		nv_wr32(priv, NV10_PGRAPH_PIPE_DATA, 0x00000000);

	nv_wr32(priv, NV10_PGRAPH_PIPE_ADDRESS, 0x00000040);
	nv_wr32(priv, NV10_PGRAPH_PIPE_DATA, 0x00000008);

	PIPE_RESTORE(priv, pipe->pipe_0x0200, 0x0200);

	nv04_graph_idle(priv);

	PIPE_RESTORE(priv, pipe_0x0040, 0x0040);

	nv_wr32(priv, NV10_PGRAPH_XFMODE0, xfmode0);
	nv_wr32(priv, NV10_PGRAPH_XFMODE1, xfmode1);

	PIPE_RESTORE(priv, pipe_0x64c0, 0x64c0);
	PIPE_RESTORE(priv, pipe_0x6ab0, 0x6ab0);
	PIPE_RESTORE(priv, pipe_0x6a80, 0x6a80);
	PIPE_RESTORE(priv, pipe->pipe_0x4400, 0x4400);

	nv_wr32(priv, NV10_PGRAPH_PIPE_ADDRESS, 0x000000c0);
	nv_wr32(priv, NV10_PGRAPH_PIPE_DATA, 0x00000000);

	nv04_graph_idle(priv);

	return 0;
}

static int
nv17_graph_mthd_lma_enable(struct nouveau_object *object, u32 mthd,
			   void *args, u32 size)
{
	struct nv10_graph_chan *chan = (void *)object->parent;
	struct nv10_graph_priv *priv = nv10_graph_priv(chan);

	nv04_graph_idle(priv);

	nv_mask(priv, NV10_PGRAPH_DEBUG_4, 0x00000100, 0x00000100);
	nv_mask(priv, 0x4006b0, 0x08000000, 0x08000000);
	return 0;
}

static struct nouveau_omthds
nv17_celcius_omthds[] = {
	{ 0x1638, nv17_graph_mthd_lma_window },
	{ 0x163c, nv17_graph_mthd_lma_window },
	{ 0x1640, nv17_graph_mthd_lma_window },
	{ 0x1644, nv17_graph_mthd_lma_window },
	{ 0x1658, nv17_graph_mthd_lma_enable },
	{}
};

static struct nouveau_oclass
nv17_graph_sclass[] = {
	{ 0x0012, &nv04_graph_ofuncs }, /* beta1 */
	{ 0x0019, &nv04_graph_ofuncs }, /* clip */
	{ 0x0030, &nv04_graph_ofuncs }, /* null */
	{ 0x0039, &nv04_graph_ofuncs }, /* m2mf */
	{ 0x0043, &nv04_graph_ofuncs }, /* rop */
	{ 0x0044, &nv04_graph_ofuncs }, /* pattern */
	{ 0x004a, &nv04_graph_ofuncs }, /* gdi */
	{ 0x0052, &nv04_graph_ofuncs }, /* swzsurf */
	{ 0x005f, &nv04_graph_ofuncs }, /* blit */
	{ 0x0062, &nv04_graph_ofuncs }, /* surf2d */
	{ 0x0072, &nv04_graph_ofuncs }, /* beta4 */
	{ 0x0089, &nv04_graph_ofuncs }, /* sifm */
	{ 0x008a, &nv04_graph_ofuncs }, /* ifc */
	{ 0x009f, &nv04_graph_ofuncs }, /* blit */
	{ 0x0093, &nv04_graph_ofuncs }, /* surf3d */
	{ 0x0094, &nv04_graph_ofuncs }, /* ttri */
	{ 0x0095, &nv04_graph_ofuncs }, /* mtri */
	{ 0x0099, &nv04_graph_ofuncs, nv17_celcius_omthds },
	{},
};

/*******************************************************************************
 * PGRAPH context
 ******************************************************************************/

static struct nv10_graph_chan *
nv10_graph_channel(struct nv10_graph_priv *priv)
{
	struct nv10_graph_chan *chan = NULL;
	if (nv_rd32(priv, 0x400144) & 0x00010000) {
		int chid = nv_rd32(priv, 0x400148) >> 24;
		if (chid < ARRAY_SIZE(priv->chan))
			chan = priv->chan[chid];
	}
	return chan;
}

static void
nv10_graph_save_pipe(struct nv10_graph_chan *chan)
{
	struct nv10_graph_priv *priv = nv10_graph_priv(chan);
	struct pipe_state *pipe = &chan->pipe_state;

	PIPE_SAVE(priv, pipe->pipe_0x4400, 0x4400);
	PIPE_SAVE(priv, pipe->pipe_0x0200, 0x0200);
	PIPE_SAVE(priv, pipe->pipe_0x6400, 0x6400);
	PIPE_SAVE(priv, pipe->pipe_0x6800, 0x6800);
	PIPE_SAVE(priv, pipe->pipe_0x6c00, 0x6c00);
	PIPE_SAVE(priv, pipe->pipe_0x7000, 0x7000);
	PIPE_SAVE(priv, pipe->pipe_0x7400, 0x7400);
	PIPE_SAVE(priv, pipe->pipe_0x7800, 0x7800);
	PIPE_SAVE(priv, pipe->pipe_0x0040, 0x0040);
	PIPE_SAVE(priv, pipe->pipe_0x0000, 0x0000);
}

static void
nv10_graph_load_pipe(struct nv10_graph_chan *chan)
{
	struct nv10_graph_priv *priv = nv10_graph_priv(chan);
	struct pipe_state *pipe = &chan->pipe_state;
	u32 xfmode0, xfmode1;
	int i;

	nv04_graph_idle(priv);
	/* XXX check haiku comments */
	xfmode0 = nv_rd32(priv, NV10_PGRAPH_XFMODE0);
	xfmode1 = nv_rd32(priv, NV10_PGRAPH_XFMODE1);
	nv_wr32(priv, NV10_PGRAPH_XFMODE0, 0x10000000);
	nv_wr32(priv, NV10_PGRAPH_XFMODE1, 0x00000000);
	nv_wr32(priv, NV10_PGRAPH_PIPE_ADDRESS, 0x000064c0);
	for (i = 0; i < 4; i++)
		nv_wr32(priv, NV10_PGRAPH_PIPE_DATA, 0x3f800000);
	for (i = 0; i < 4; i++)
		nv_wr32(priv, NV10_PGRAPH_PIPE_DATA, 0x00000000);

	nv_wr32(priv, NV10_PGRAPH_PIPE_ADDRESS, 0x00006ab0);
	for (i = 0; i < 3; i++)
		nv_wr32(priv, NV10_PGRAPH_PIPE_DATA, 0x3f800000);

	nv_wr32(priv, NV10_PGRAPH_PIPE_ADDRESS, 0x00006a80);
	for (i = 0; i < 3; i++)
		nv_wr32(priv, NV10_PGRAPH_PIPE_DATA, 0x00000000);

	nv_wr32(priv, NV10_PGRAPH_PIPE_ADDRESS, 0x00000040);
	nv_wr32(priv, NV10_PGRAPH_PIPE_DATA, 0x00000008);


	PIPE_RESTORE(priv, pipe->pipe_0x0200, 0x0200);
	nv04_graph_idle(priv);

	/* restore XFMODE */
	nv_wr32(priv, NV10_PGRAPH_XFMODE0, xfmode0);
	nv_wr32(priv, NV10_PGRAPH_XFMODE1, xfmode1);
	PIPE_RESTORE(priv, pipe->pipe_0x6400, 0x6400);
	PIPE_RESTORE(priv, pipe->pipe_0x6800, 0x6800);
	PIPE_RESTORE(priv, pipe->pipe_0x6c00, 0x6c00);
	PIPE_RESTORE(priv, pipe->pipe_0x7000, 0x7000);
	PIPE_RESTORE(priv, pipe->pipe_0x7400, 0x7400);
	PIPE_RESTORE(priv, pipe->pipe_0x7800, 0x7800);
	PIPE_RESTORE(priv, pipe->pipe_0x4400, 0x4400);
	PIPE_RESTORE(priv, pipe->pipe_0x0000, 0x0000);
	PIPE_RESTORE(priv, pipe->pipe_0x0040, 0x0040);
	nv04_graph_idle(priv);
}

static void
nv10_graph_create_pipe(struct nv10_graph_chan *chan)
{
	struct nv10_graph_priv *priv = nv10_graph_priv(chan);
	struct pipe_state *pipe_state = &chan->pipe_state;
	u32 *pipe_state_addr;
	int i;
#define PIPE_INIT(addr) \
	do { \
		pipe_state_addr = pipe_state->pipe_##addr; \
	} while (0)
#define PIPE_INIT_END(addr) \
	do { \
		u32 *__end_addr = pipe_state->pipe_##addr + \
				ARRAY_SIZE(pipe_state->pipe_##addr); \
		if (pipe_state_addr != __end_addr) \
			nv_error(priv, "incomplete pipe init for 0x%x :  %p/%p\n", \
				addr, pipe_state_addr, __end_addr); \
	} while (0)
#define NV_WRITE_PIPE_INIT(value) *(pipe_state_addr++) = value

	PIPE_INIT(0x0200);
	for (i = 0; i < 48; i++)
		NV_WRITE_PIPE_INIT(0x00000000);
	PIPE_INIT_END(0x0200);

	PIPE_INIT(0x6400);
	for (i = 0; i < 211; i++)
		NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x3f800000);
	NV_WRITE_PIPE_INIT(0x40000000);
	NV_WRITE_PIPE_INIT(0x40000000);
	NV_WRITE_PIPE_INIT(0x40000000);
	NV_WRITE_PIPE_INIT(0x40000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x3f800000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x3f000000);
	NV_WRITE_PIPE_INIT(0x3f000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x3f800000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x3f800000);
	NV_WRITE_PIPE_INIT(0x3f800000);
	NV_WRITE_PIPE_INIT(0x3f800000);
	NV_WRITE_PIPE_INIT(0x3f800000);
	PIPE_INIT_END(0x6400);

	PIPE_INIT(0x6800);
	for (i = 0; i < 162; i++)
		NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x3f800000);
	for (i = 0; i < 25; i++)
		NV_WRITE_PIPE_INIT(0x00000000);
	PIPE_INIT_END(0x6800);

	PIPE_INIT(0x6c00);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0xbf800000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	PIPE_INIT_END(0x6c00);

	PIPE_INIT(0x7000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x7149f2ca);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x7149f2ca);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x7149f2ca);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x7149f2ca);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x7149f2ca);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x7149f2ca);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x7149f2ca);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x00000000);
	NV_WRITE_PIPE_INIT(0x7149f2ca);
	for (i = 0; i < 35; i++)
		NV_WRITE_PIPE_INIT(0x00000000);
	PIPE_INIT_END(0x7000);

	PIPE_INIT(0x7400);
	for (i = 0; i < 48; i++)
		NV_WRITE_PIPE_INIT(0x00000000);
	PIPE_INIT_END(0x7400);

	PIPE_INIT(0x7800);
	for (i = 0; i < 48; i++)
		NV_WRITE_PIPE_INIT(0x00000000);
	PIPE_INIT_END(0x7800);

	PIPE_INIT(0x4400);
	for (i = 0; i < 32; i++)
		NV_WRITE_PIPE_INIT(0x00000000);
	PIPE_INIT_END(0x4400);

	PIPE_INIT(0x0000);
	for (i = 0; i < 16; i++)
		NV_WRITE_PIPE_INIT(0x00000000);
	PIPE_INIT_END(0x0000);

	PIPE_INIT(0x0040);
	for (i = 0; i < 4; i++)
		NV_WRITE_PIPE_INIT(0x00000000);
	PIPE_INIT_END(0x0040);

#undef PIPE_INIT
#undef PIPE_INIT_END
#undef NV_WRITE_PIPE_INIT
}

static int
nv10_graph_ctx_regs_find_offset(struct nv10_graph_priv *priv, int reg)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(nv10_graph_ctx_regs); i++) {
		if (nv10_graph_ctx_regs[i] == reg)
			return i;
	}
	nv_error(priv, "unknow offset nv10_ctx_regs %d\n", reg);
	return -1;
}

static int
nv17_graph_ctx_regs_find_offset(struct nv10_graph_priv *priv, int reg)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(nv17_graph_ctx_regs); i++) {
		if (nv17_graph_ctx_regs[i] == reg)
			return i;
	}
	nv_error(priv, "unknow offset nv17_ctx_regs %d\n", reg);
	return -1;
}

static void
nv10_graph_load_dma_vtxbuf(struct nv10_graph_chan *chan, int chid, u32 inst)
{
	struct nv10_graph_priv *priv = nv10_graph_priv(chan);
	u32 st2, st2_dl, st2_dh, fifo_ptr, fifo[0x60/4];
	u32 ctx_user, ctx_switch[5];
	int i, subchan = -1;

	/* NV10TCL_DMA_VTXBUF (method 0x18c) modifies hidden state
	 * that cannot be restored via MMIO. Do it through the FIFO
	 * instead.
	 */

	/* Look for a celsius object */
	for (i = 0; i < 8; i++) {
		int class = nv_rd32(priv, NV10_PGRAPH_CTX_CACHE(i, 0)) & 0xfff;

		if (class == 0x56 || class == 0x96 || class == 0x99) {
			subchan = i;
			break;
		}
	}

	if (subchan < 0 || !inst)
		return;

	/* Save the current ctx object */
	ctx_user = nv_rd32(priv, NV10_PGRAPH_CTX_USER);
	for (i = 0; i < 5; i++)
		ctx_switch[i] = nv_rd32(priv, NV10_PGRAPH_CTX_SWITCH(i));

	/* Save the FIFO state */
	st2 = nv_rd32(priv, NV10_PGRAPH_FFINTFC_ST2);
	st2_dl = nv_rd32(priv, NV10_PGRAPH_FFINTFC_ST2_DL);
	st2_dh = nv_rd32(priv, NV10_PGRAPH_FFINTFC_ST2_DH);
	fifo_ptr = nv_rd32(priv, NV10_PGRAPH_FFINTFC_FIFO_PTR);

	for (i = 0; i < ARRAY_SIZE(fifo); i++)
		fifo[i] = nv_rd32(priv, 0x4007a0 + 4 * i);

	/* Switch to the celsius subchannel */
	for (i = 0; i < 5; i++)
		nv_wr32(priv, NV10_PGRAPH_CTX_SWITCH(i),
			nv_rd32(priv, NV10_PGRAPH_CTX_CACHE(subchan, i)));
	nv_mask(priv, NV10_PGRAPH_CTX_USER, 0xe000, subchan << 13);

	/* Inject NV10TCL_DMA_VTXBUF */
	nv_wr32(priv, NV10_PGRAPH_FFINTFC_FIFO_PTR, 0);
	nv_wr32(priv, NV10_PGRAPH_FFINTFC_ST2,
		0x2c000000 | chid << 20 | subchan << 16 | 0x18c);
	nv_wr32(priv, NV10_PGRAPH_FFINTFC_ST2_DL, inst);
	nv_mask(priv, NV10_PGRAPH_CTX_CONTROL, 0, 0x10000);
	nv_mask(priv, NV04_PGRAPH_FIFO, 0x00000001, 0x00000001);
	nv_mask(priv, NV04_PGRAPH_FIFO, 0x00000001, 0x00000000);

	/* Restore the FIFO state */
	for (i = 0; i < ARRAY_SIZE(fifo); i++)
		nv_wr32(priv, 0x4007a0 + 4 * i, fifo[i]);

	nv_wr32(priv, NV10_PGRAPH_FFINTFC_FIFO_PTR, fifo_ptr);
	nv_wr32(priv, NV10_PGRAPH_FFINTFC_ST2, st2);
	nv_wr32(priv, NV10_PGRAPH_FFINTFC_ST2_DL, st2_dl);
	nv_wr32(priv, NV10_PGRAPH_FFINTFC_ST2_DH, st2_dh);

	/* Restore the current ctx object */
	for (i = 0; i < 5; i++)
		nv_wr32(priv, NV10_PGRAPH_CTX_SWITCH(i), ctx_switch[i]);
	nv_wr32(priv, NV10_PGRAPH_CTX_USER, ctx_user);
}

static int
nv10_graph_load_context(struct nv10_graph_chan *chan, int chid)
{
	struct nv10_graph_priv *priv = nv10_graph_priv(chan);
	u32 inst;
	int i;

	for (i = 0; i < ARRAY_SIZE(nv10_graph_ctx_regs); i++)
		nv_wr32(priv, nv10_graph_ctx_regs[i], chan->nv10[i]);

	if (nv_device(priv)->chipset >= 0x17) {
		for (i = 0; i < ARRAY_SIZE(nv17_graph_ctx_regs); i++)
			nv_wr32(priv, nv17_graph_ctx_regs[i], chan->nv17[i]);
	}

	nv10_graph_load_pipe(chan);

	inst = nv_rd32(priv, NV10_PGRAPH_GLOBALSTATE1) & 0xffff;
	nv10_graph_load_dma_vtxbuf(chan, chid, inst);

	nv_wr32(priv, NV10_PGRAPH_CTX_CONTROL, 0x10010100);
	nv_mask(priv, NV10_PGRAPH_CTX_USER, 0xff000000, chid << 24);
	nv_mask(priv, NV10_PGRAPH_FFINTFC_ST2, 0x30000000, 0x00000000);
	return 0;
}

static int
nv10_graph_unload_context(struct nv10_graph_chan *chan)
{
	struct nv10_graph_priv *priv = nv10_graph_priv(chan);
	int i;

	for (i = 0; i < ARRAY_SIZE(nv10_graph_ctx_regs); i++)
		chan->nv10[i] = nv_rd32(priv, nv10_graph_ctx_regs[i]);

	if (nv_device(priv)->chipset >= 0x17) {
		for (i = 0; i < ARRAY_SIZE(nv17_graph_ctx_regs); i++)
			chan->nv17[i] = nv_rd32(priv, nv17_graph_ctx_regs[i]);
	}

	nv10_graph_save_pipe(chan);

	nv_wr32(priv, NV10_PGRAPH_CTX_CONTROL, 0x10000000);
	nv_mask(priv, NV10_PGRAPH_CTX_USER, 0xff000000, 0x1f000000);
	return 0;
}

static void
nv10_graph_context_switch(struct nv10_graph_priv *priv)
{
	struct nv10_graph_chan *prev = NULL;
	struct nv10_graph_chan *next = NULL;
	unsigned long flags;
	int chid;

	spin_lock_irqsave(&priv->lock, flags);
	nv04_graph_idle(priv);

	/* If previous context is valid, we need to save it */
	prev = nv10_graph_channel(priv);
	if (prev)
		nv10_graph_unload_context(prev);

	/* load context for next channel */
	chid = (nv_rd32(priv, NV04_PGRAPH_TRAPPED_ADDR) >> 20) & 0x1f;
	next = priv->chan[chid];
	if (next)
		nv10_graph_load_context(next, chid);

	spin_unlock_irqrestore(&priv->lock, flags);
}

#define NV_WRITE_CTX(reg, val) do { \
	int offset = nv10_graph_ctx_regs_find_offset(priv, reg); \
	if (offset > 0) \
		chan->nv10[offset] = val; \
	} while (0)

#define NV17_WRITE_CTX(reg, val) do { \
	int offset = nv17_graph_ctx_regs_find_offset(priv, reg); \
	if (offset > 0) \
		chan->nv17[offset] = val; \
	} while (0)

static int
nv10_graph_context_ctor(struct nouveau_object *parent,
			struct nouveau_object *engine,
			struct nouveau_oclass *oclass, void *data, u32 size,
			struct nouveau_object **pobject)
{
	struct nouveau_fifo_chan *fifo = (void *)parent;
	struct nv10_graph_priv *priv = (void *)engine;
	struct nv10_graph_chan *chan;
	unsigned long flags;
	int ret;

	ret = nouveau_object_create(parent, engine, oclass, 0, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	spin_lock_irqsave(&priv->lock, flags);
	if (priv->chan[fifo->chid]) {
		*pobject = nv_object(priv->chan[fifo->chid]);
		atomic_inc(&(*pobject)->refcount);
		spin_unlock_irqrestore(&priv->lock, flags);
		nouveau_object_destroy(&chan->base);
		return 1;
	}

	NV_WRITE_CTX(0x00400e88, 0x08000000);
	NV_WRITE_CTX(0x00400e9c, 0x4b7fffff);
	NV_WRITE_CTX(NV03_PGRAPH_XY_LOGIC_MISC0, 0x0001ffff);
	NV_WRITE_CTX(0x00400e10, 0x00001000);
	NV_WRITE_CTX(0x00400e14, 0x00001000);
	NV_WRITE_CTX(0x00400e30, 0x00080008);
	NV_WRITE_CTX(0x00400e34, 0x00080008);
	if (nv_device(priv)->chipset >= 0x17) {
		/* is it really needed ??? */
		NV17_WRITE_CTX(NV10_PGRAPH_DEBUG_4,
					nv_rd32(priv, NV10_PGRAPH_DEBUG_4));
		NV17_WRITE_CTX(0x004006b0, nv_rd32(priv, 0x004006b0));
		NV17_WRITE_CTX(0x00400eac, 0x0fff0000);
		NV17_WRITE_CTX(0x00400eb0, 0x0fff0000);
		NV17_WRITE_CTX(0x00400ec0, 0x00000080);
		NV17_WRITE_CTX(0x00400ed0, 0x00000080);
	}
	NV_WRITE_CTX(NV10_PGRAPH_CTX_USER, chan->chid << 24);

	nv10_graph_create_pipe(chan);

	priv->chan[fifo->chid] = chan;
	chan->chid = fifo->chid;
	spin_unlock_irqrestore(&priv->lock, flags);
	return 0;
}

static void
nv10_graph_context_dtor(struct nouveau_object *object)
{
	struct nv10_graph_priv *priv = (void *)object->engine;
	struct nv10_graph_chan *chan = (void *)object;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	priv->chan[chan->chid] = NULL;
	spin_unlock_irqrestore(&priv->lock, flags);

	nouveau_object_destroy(&chan->base);
}

static int
nv10_graph_context_fini(struct nouveau_object *object, bool suspend)
{
	struct nv10_graph_priv *priv = (void *)object->engine;
	struct nv10_graph_chan *chan = (void *)object;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	nv_mask(priv, NV04_PGRAPH_FIFO, 0x00000001, 0x00000000);
	if (nv10_graph_channel(priv) == chan)
		nv10_graph_unload_context(chan);
	nv_mask(priv, NV04_PGRAPH_FIFO, 0x00000001, 0x00000001);
	spin_unlock_irqrestore(&priv->lock, flags);

	return nouveau_object_fini(&chan->base, suspend);
}

static struct nouveau_oclass
nv10_graph_cclass = {
	.handle = NV_ENGCTX(GR, 0x10),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv10_graph_context_ctor,
		.dtor = nv10_graph_context_dtor,
		.init = nouveau_object_init,
		.fini = nv10_graph_context_fini,
	},
};

/*******************************************************************************
 * PGRAPH engine/subdev functions
 ******************************************************************************/

static void
nv10_graph_tile_prog(struct nouveau_engine *engine, int i)
{
	struct nouveau_fb_tile *tile = &nouveau_fb(engine)->tile.region[i];
	struct nouveau_fifo *pfifo = nouveau_fifo(engine);
	struct nv10_graph_priv *priv = (void *)engine;
	unsigned long flags;

	pfifo->pause(pfifo, &flags);
	nv04_graph_idle(priv);

	nv_wr32(priv, NV10_PGRAPH_TLIMIT(i), tile->limit);
	nv_wr32(priv, NV10_PGRAPH_TSIZE(i), tile->pitch);
	nv_wr32(priv, NV10_PGRAPH_TILE(i), tile->addr);

	pfifo->start(pfifo, &flags);
}

const struct nouveau_bitfield nv10_graph_intr_name[] = {
	{ NV_PGRAPH_INTR_NOTIFY, "NOTIFY" },
	{ NV_PGRAPH_INTR_ERROR,  "ERROR"  },
	{}
};

const struct nouveau_bitfield nv10_graph_nstatus[] = {
	{ NV10_PGRAPH_NSTATUS_STATE_IN_USE,       "STATE_IN_USE" },
	{ NV10_PGRAPH_NSTATUS_INVALID_STATE,      "INVALID_STATE" },
	{ NV10_PGRAPH_NSTATUS_BAD_ARGUMENT,       "BAD_ARGUMENT" },
	{ NV10_PGRAPH_NSTATUS_PROTECTION_FAULT,   "PROTECTION_FAULT" },
	{}
};

static void
nv10_graph_intr(struct nouveau_subdev *subdev)
{
	struct nv10_graph_priv *priv = (void *)subdev;
	struct nv10_graph_chan *chan = NULL;
	struct nouveau_namedb *namedb = NULL;
	struct nouveau_handle *handle = NULL;
	u32 stat = nv_rd32(priv, NV03_PGRAPH_INTR);
	u32 nsource = nv_rd32(priv, NV03_PGRAPH_NSOURCE);
	u32 nstatus = nv_rd32(priv, NV03_PGRAPH_NSTATUS);
	u32 addr = nv_rd32(priv, NV04_PGRAPH_TRAPPED_ADDR);
	u32 chid = (addr & 0x01f00000) >> 20;
	u32 subc = (addr & 0x00070000) >> 16;
	u32 mthd = (addr & 0x00001ffc);
	u32 data = nv_rd32(priv, NV04_PGRAPH_TRAPPED_DATA);
	u32 class = nv_rd32(priv, 0x400160 + subc * 4) & 0xfff;
	u32 show = stat;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	chan = priv->chan[chid];
	if (chan)
		namedb = (void *)nv_pclass(nv_object(chan), NV_NAMEDB_CLASS);
	spin_unlock_irqrestore(&priv->lock, flags);

	if (stat & NV_PGRAPH_INTR_ERROR) {
		if (chan && (nsource & NV03_PGRAPH_NSOURCE_ILLEGAL_MTHD)) {
			handle = nouveau_namedb_get_class(namedb, class);
			if (handle && !nv_call(handle->object, mthd, data))
				show &= ~NV_PGRAPH_INTR_ERROR;
		}
	}

	if (stat & NV_PGRAPH_INTR_CONTEXT_SWITCH) {
		nv_wr32(priv, NV03_PGRAPH_INTR, NV_PGRAPH_INTR_CONTEXT_SWITCH);
		stat &= ~NV_PGRAPH_INTR_CONTEXT_SWITCH;
		show &= ~NV_PGRAPH_INTR_CONTEXT_SWITCH;
		nv10_graph_context_switch(priv);
	}

	nv_wr32(priv, NV03_PGRAPH_INTR, stat);
	nv_wr32(priv, NV04_PGRAPH_FIFO, 0x00000001);

	if (show) {
		nv_error(priv, "");
		nouveau_bitfield_print(nv10_graph_intr_name, show);
		printk(" nsource:");
		nouveau_bitfield_print(nv04_graph_nsource, nsource);
		printk(" nstatus:");
		nouveau_bitfield_print(nv10_graph_nstatus, nstatus);
		printk("\n");
		nv_error(priv, "ch %d/%d class 0x%04x "
			       "mthd 0x%04x data 0x%08x\n",
			 chid, subc, class, mthd, data);
	}

	nouveau_namedb_put(handle);
}

static int
nv10_graph_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct nv10_graph_priv *priv;
	int ret;

	ret = nouveau_graph_create(parent, engine, oclass, true, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00001000;
	nv_subdev(priv)->intr = nv10_graph_intr;
	nv_engine(priv)->cclass = &nv10_graph_cclass;

	if (nv_device(priv)->chipset <= 0x10)
		nv_engine(priv)->sclass = nv10_graph_sclass;
	else
	if (nv_device(priv)->chipset <  0x17 ||
	    nv_device(priv)->chipset == 0x1a)
		nv_engine(priv)->sclass = nv15_graph_sclass;
	else
		nv_engine(priv)->sclass = nv17_graph_sclass;

	nv_engine(priv)->tile_prog = nv10_graph_tile_prog;
	spin_lock_init(&priv->lock);
	return 0;
}

static void
nv10_graph_dtor(struct nouveau_object *object)
{
	struct nv10_graph_priv *priv = (void *)object;
	nouveau_graph_destroy(&priv->base);
}

static int
nv10_graph_init(struct nouveau_object *object)
{
	struct nouveau_engine *engine = nv_engine(object);
	struct nouveau_fb *pfb = nouveau_fb(object);
	struct nv10_graph_priv *priv = (void *)engine;
	int ret, i;

	ret = nouveau_graph_init(&priv->base);
	if (ret)
		return ret;

	nv_wr32(priv, NV03_PGRAPH_INTR   , 0xFFFFFFFF);
	nv_wr32(priv, NV03_PGRAPH_INTR_EN, 0xFFFFFFFF);

	nv_wr32(priv, NV04_PGRAPH_DEBUG_0, 0xFFFFFFFF);
	nv_wr32(priv, NV04_PGRAPH_DEBUG_0, 0x00000000);
	nv_wr32(priv, NV04_PGRAPH_DEBUG_1, 0x00118700);
	/* nv_wr32(priv, NV04_PGRAPH_DEBUG_2, 0x24E00810); */ /* 0x25f92ad9 */
	nv_wr32(priv, NV04_PGRAPH_DEBUG_2, 0x25f92ad9);
	nv_wr32(priv, NV04_PGRAPH_DEBUG_3, 0x55DE0830 | (1 << 29) | (1 << 31));

	if (nv_device(priv)->chipset >= 0x17) {
		nv_wr32(priv, NV10_PGRAPH_DEBUG_4, 0x1f000000);
		nv_wr32(priv, 0x400a10, 0x03ff3fb6);
		nv_wr32(priv, 0x400838, 0x002f8684);
		nv_wr32(priv, 0x40083c, 0x00115f3f);
		nv_wr32(priv, 0x4006b0, 0x40000020);
	} else {
		nv_wr32(priv, NV10_PGRAPH_DEBUG_4, 0x00000000);
	}

	/* Turn all the tiling regions off. */
	for (i = 0; i < pfb->tile.regions; i++)
		engine->tile_prog(engine, i);

	nv_wr32(priv, NV10_PGRAPH_CTX_SWITCH(0), 0x00000000);
	nv_wr32(priv, NV10_PGRAPH_CTX_SWITCH(1), 0x00000000);
	nv_wr32(priv, NV10_PGRAPH_CTX_SWITCH(2), 0x00000000);
	nv_wr32(priv, NV10_PGRAPH_CTX_SWITCH(3), 0x00000000);
	nv_wr32(priv, NV10_PGRAPH_CTX_SWITCH(4), 0x00000000);
	nv_wr32(priv, NV10_PGRAPH_STATE, 0xFFFFFFFF);

	nv_mask(priv, NV10_PGRAPH_CTX_USER, 0xff000000, 0x1f000000);
	nv_wr32(priv, NV10_PGRAPH_CTX_CONTROL, 0x10000100);
	nv_wr32(priv, NV10_PGRAPH_FFINTFC_ST2, 0x08000000);
	return 0;
}

static int
nv10_graph_fini(struct nouveau_object *object, bool suspend)
{
	struct nv10_graph_priv *priv = (void *)object;
	return nouveau_graph_fini(&priv->base, suspend);
}

struct nouveau_oclass
nv10_graph_oclass = {
	.handle = NV_ENGINE(GR, 0x10),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv10_graph_ctor,
		.dtor = nv10_graph_dtor,
		.init = nv10_graph_init,
		.fini = nv10_graph_fini,
	},
};
