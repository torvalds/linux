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

#include "drmP.h"
#include "drm.h"
#include "nouveau_drm.h"
#include "nouveau_drv.h"

#define NV10_FIFO_NUMBER 32

struct pipe_state {
	uint32_t pipe_0x0000[0x040/4];
	uint32_t pipe_0x0040[0x010/4];
	uint32_t pipe_0x0200[0x0c0/4];
	uint32_t pipe_0x4400[0x080/4];
	uint32_t pipe_0x6400[0x3b0/4];
	uint32_t pipe_0x6800[0x2f0/4];
	uint32_t pipe_0x6c00[0x030/4];
	uint32_t pipe_0x7000[0x130/4];
	uint32_t pipe_0x7400[0x0c0/4];
	uint32_t pipe_0x7800[0x0c0/4];
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

struct graph_state {
	int nv10[ARRAY_SIZE(nv10_graph_ctx_regs)];
	int nv17[ARRAY_SIZE(nv17_graph_ctx_regs)];
	struct pipe_state pipe_state;
	uint32_t lma_window[4];
};

#define PIPE_SAVE(dev, state, addr)					\
	do {								\
		int __i;						\
		nv_wr32(dev, NV10_PGRAPH_PIPE_ADDRESS, addr);		\
		for (__i = 0; __i < ARRAY_SIZE(state); __i++)		\
			state[__i] = nv_rd32(dev, NV10_PGRAPH_PIPE_DATA); \
	} while (0)

#define PIPE_RESTORE(dev, state, addr)					\
	do {								\
		int __i;						\
		nv_wr32(dev, NV10_PGRAPH_PIPE_ADDRESS, addr);		\
		for (__i = 0; __i < ARRAY_SIZE(state); __i++)		\
			nv_wr32(dev, NV10_PGRAPH_PIPE_DATA, state[__i]); \
	} while (0)

static void nv10_graph_save_pipe(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct graph_state *pgraph_ctx = chan->pgraph_ctx;
	struct pipe_state *pipe = &pgraph_ctx->pipe_state;

	PIPE_SAVE(dev, pipe->pipe_0x4400, 0x4400);
	PIPE_SAVE(dev, pipe->pipe_0x0200, 0x0200);
	PIPE_SAVE(dev, pipe->pipe_0x6400, 0x6400);
	PIPE_SAVE(dev, pipe->pipe_0x6800, 0x6800);
	PIPE_SAVE(dev, pipe->pipe_0x6c00, 0x6c00);
	PIPE_SAVE(dev, pipe->pipe_0x7000, 0x7000);
	PIPE_SAVE(dev, pipe->pipe_0x7400, 0x7400);
	PIPE_SAVE(dev, pipe->pipe_0x7800, 0x7800);
	PIPE_SAVE(dev, pipe->pipe_0x0040, 0x0040);
	PIPE_SAVE(dev, pipe->pipe_0x0000, 0x0000);
}

static void nv10_graph_load_pipe(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct graph_state *pgraph_ctx = chan->pgraph_ctx;
	struct pipe_state *pipe = &pgraph_ctx->pipe_state;
	uint32_t xfmode0, xfmode1;
	int i;

	nouveau_wait_for_idle(dev);
	/* XXX check haiku comments */
	xfmode0 = nv_rd32(dev, NV10_PGRAPH_XFMODE0);
	xfmode1 = nv_rd32(dev, NV10_PGRAPH_XFMODE1);
	nv_wr32(dev, NV10_PGRAPH_XFMODE0, 0x10000000);
	nv_wr32(dev, NV10_PGRAPH_XFMODE1, 0x00000000);
	nv_wr32(dev, NV10_PGRAPH_PIPE_ADDRESS, 0x000064c0);
	for (i = 0; i < 4; i++)
		nv_wr32(dev, NV10_PGRAPH_PIPE_DATA, 0x3f800000);
	for (i = 0; i < 4; i++)
		nv_wr32(dev, NV10_PGRAPH_PIPE_DATA, 0x00000000);

	nv_wr32(dev, NV10_PGRAPH_PIPE_ADDRESS, 0x00006ab0);
	for (i = 0; i < 3; i++)
		nv_wr32(dev, NV10_PGRAPH_PIPE_DATA, 0x3f800000);

	nv_wr32(dev, NV10_PGRAPH_PIPE_ADDRESS, 0x00006a80);
	for (i = 0; i < 3; i++)
		nv_wr32(dev, NV10_PGRAPH_PIPE_DATA, 0x00000000);

	nv_wr32(dev, NV10_PGRAPH_PIPE_ADDRESS, 0x00000040);
	nv_wr32(dev, NV10_PGRAPH_PIPE_DATA, 0x00000008);


	PIPE_RESTORE(dev, pipe->pipe_0x0200, 0x0200);
	nouveau_wait_for_idle(dev);

	/* restore XFMODE */
	nv_wr32(dev, NV10_PGRAPH_XFMODE0, xfmode0);
	nv_wr32(dev, NV10_PGRAPH_XFMODE1, xfmode1);
	PIPE_RESTORE(dev, pipe->pipe_0x6400, 0x6400);
	PIPE_RESTORE(dev, pipe->pipe_0x6800, 0x6800);
	PIPE_RESTORE(dev, pipe->pipe_0x6c00, 0x6c00);
	PIPE_RESTORE(dev, pipe->pipe_0x7000, 0x7000);
	PIPE_RESTORE(dev, pipe->pipe_0x7400, 0x7400);
	PIPE_RESTORE(dev, pipe->pipe_0x7800, 0x7800);
	PIPE_RESTORE(dev, pipe->pipe_0x4400, 0x4400);
	PIPE_RESTORE(dev, pipe->pipe_0x0000, 0x0000);
	PIPE_RESTORE(dev, pipe->pipe_0x0040, 0x0040);
	nouveau_wait_for_idle(dev);
}

static void nv10_graph_create_pipe(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct graph_state *pgraph_ctx = chan->pgraph_ctx;
	struct pipe_state *fifo_pipe_state = &pgraph_ctx->pipe_state;
	uint32_t *fifo_pipe_state_addr;
	int i;
#define PIPE_INIT(addr) \
	do { \
		fifo_pipe_state_addr = fifo_pipe_state->pipe_##addr; \
	} while (0)
#define PIPE_INIT_END(addr) \
	do { \
		uint32_t *__end_addr = fifo_pipe_state->pipe_##addr + \
				ARRAY_SIZE(fifo_pipe_state->pipe_##addr); \
		if (fifo_pipe_state_addr != __end_addr) \
			NV_ERROR(dev, "incomplete pipe init for 0x%x :  %p/%p\n", \
				addr, fifo_pipe_state_addr, __end_addr); \
	} while (0)
#define NV_WRITE_PIPE_INIT(value) *(fifo_pipe_state_addr++) = value

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

static int nv10_graph_ctx_regs_find_offset(struct drm_device *dev, int reg)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(nv10_graph_ctx_regs); i++) {
		if (nv10_graph_ctx_regs[i] == reg)
			return i;
	}
	NV_ERROR(dev, "unknow offset nv10_ctx_regs %d\n", reg);
	return -1;
}

static int nv17_graph_ctx_regs_find_offset(struct drm_device *dev, int reg)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(nv17_graph_ctx_regs); i++) {
		if (nv17_graph_ctx_regs[i] == reg)
			return i;
	}
	NV_ERROR(dev, "unknow offset nv17_ctx_regs %d\n", reg);
	return -1;
}

static void nv10_graph_load_dma_vtxbuf(struct nouveau_channel *chan,
				       uint32_t inst)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	uint32_t st2, st2_dl, st2_dh, fifo_ptr, fifo[0x60/4];
	uint32_t ctx_user, ctx_switch[5];
	int i, subchan = -1;

	/* NV10TCL_DMA_VTXBUF (method 0x18c) modifies hidden state
	 * that cannot be restored via MMIO. Do it through the FIFO
	 * instead.
	 */

	/* Look for a celsius object */
	for (i = 0; i < 8; i++) {
		int class = nv_rd32(dev, NV10_PGRAPH_CTX_CACHE(i, 0)) & 0xfff;

		if (class == 0x56 || class == 0x96 || class == 0x99) {
			subchan = i;
			break;
		}
	}

	if (subchan < 0 || !inst)
		return;

	/* Save the current ctx object */
	ctx_user = nv_rd32(dev, NV10_PGRAPH_CTX_USER);
	for (i = 0; i < 5; i++)
		ctx_switch[i] = nv_rd32(dev, NV10_PGRAPH_CTX_SWITCH(i));

	/* Save the FIFO state */
	st2 = nv_rd32(dev, NV10_PGRAPH_FFINTFC_ST2);
	st2_dl = nv_rd32(dev, NV10_PGRAPH_FFINTFC_ST2_DL);
	st2_dh = nv_rd32(dev, NV10_PGRAPH_FFINTFC_ST2_DH);
	fifo_ptr = nv_rd32(dev, NV10_PGRAPH_FFINTFC_FIFO_PTR);

	for (i = 0; i < ARRAY_SIZE(fifo); i++)
		fifo[i] = nv_rd32(dev, 0x4007a0 + 4 * i);

	/* Switch to the celsius subchannel */
	for (i = 0; i < 5; i++)
		nv_wr32(dev, NV10_PGRAPH_CTX_SWITCH(i),
			nv_rd32(dev, NV10_PGRAPH_CTX_CACHE(subchan, i)));
	nv_mask(dev, NV10_PGRAPH_CTX_USER, 0xe000, subchan << 13);

	/* Inject NV10TCL_DMA_VTXBUF */
	nv_wr32(dev, NV10_PGRAPH_FFINTFC_FIFO_PTR, 0);
	nv_wr32(dev, NV10_PGRAPH_FFINTFC_ST2,
		0x2c000000 | chan->id << 20 | subchan << 16 | 0x18c);
	nv_wr32(dev, NV10_PGRAPH_FFINTFC_ST2_DL, inst);
	nv_mask(dev, NV10_PGRAPH_CTX_CONTROL, 0, 0x10000);
	pgraph->fifo_access(dev, true);
	pgraph->fifo_access(dev, false);

	/* Restore the FIFO state */
	for (i = 0; i < ARRAY_SIZE(fifo); i++)
		nv_wr32(dev, 0x4007a0 + 4 * i, fifo[i]);

	nv_wr32(dev, NV10_PGRAPH_FFINTFC_FIFO_PTR, fifo_ptr);
	nv_wr32(dev, NV10_PGRAPH_FFINTFC_ST2, st2);
	nv_wr32(dev, NV10_PGRAPH_FFINTFC_ST2_DL, st2_dl);
	nv_wr32(dev, NV10_PGRAPH_FFINTFC_ST2_DH, st2_dh);

	/* Restore the current ctx object */
	for (i = 0; i < 5; i++)
		nv_wr32(dev, NV10_PGRAPH_CTX_SWITCH(i), ctx_switch[i]);
	nv_wr32(dev, NV10_PGRAPH_CTX_USER, ctx_user);
}

int nv10_graph_load_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct graph_state *pgraph_ctx = chan->pgraph_ctx;
	uint32_t tmp;
	int i;

	for (i = 0; i < ARRAY_SIZE(nv10_graph_ctx_regs); i++)
		nv_wr32(dev, nv10_graph_ctx_regs[i], pgraph_ctx->nv10[i]);
	if (dev_priv->chipset >= 0x17) {
		for (i = 0; i < ARRAY_SIZE(nv17_graph_ctx_regs); i++)
			nv_wr32(dev, nv17_graph_ctx_regs[i],
						pgraph_ctx->nv17[i]);
	}

	nv10_graph_load_pipe(chan);
	nv10_graph_load_dma_vtxbuf(chan, (nv_rd32(dev, NV10_PGRAPH_GLOBALSTATE1)
					  & 0xffff));

	nv_wr32(dev, NV10_PGRAPH_CTX_CONTROL, 0x10010100);
	tmp = nv_rd32(dev, NV10_PGRAPH_CTX_USER);
	nv_wr32(dev, NV10_PGRAPH_CTX_USER, (tmp & 0xffffff) | chan->id << 24);
	tmp = nv_rd32(dev, NV10_PGRAPH_FFINTFC_ST2);
	nv_wr32(dev, NV10_PGRAPH_FFINTFC_ST2, tmp & 0xcfffffff);
	return 0;
}

int
nv10_graph_unload_context(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	struct nouveau_fifo_engine *pfifo = &dev_priv->engine.fifo;
	struct nouveau_channel *chan;
	struct graph_state *ctx;
	uint32_t tmp;
	int i;

	chan = pgraph->channel(dev);
	if (!chan)
		return 0;
	ctx = chan->pgraph_ctx;

	for (i = 0; i < ARRAY_SIZE(nv10_graph_ctx_regs); i++)
		ctx->nv10[i] = nv_rd32(dev, nv10_graph_ctx_regs[i]);

	if (dev_priv->chipset >= 0x17) {
		for (i = 0; i < ARRAY_SIZE(nv17_graph_ctx_regs); i++)
			ctx->nv17[i] = nv_rd32(dev, nv17_graph_ctx_regs[i]);
	}

	nv10_graph_save_pipe(chan);

	nv_wr32(dev, NV10_PGRAPH_CTX_CONTROL, 0x10000000);
	tmp  = nv_rd32(dev, NV10_PGRAPH_CTX_USER) & 0x00ffffff;
	tmp |= (pfifo->channels - 1) << 24;
	nv_wr32(dev, NV10_PGRAPH_CTX_USER, tmp);
	return 0;
}

void
nv10_graph_context_switch(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	struct nouveau_channel *chan = NULL;
	int chid;

	pgraph->fifo_access(dev, false);
	nouveau_wait_for_idle(dev);

	/* If previous context is valid, we need to save it */
	nv10_graph_unload_context(dev);

	/* Load context for next channel */
	chid = (nv_rd32(dev, NV04_PGRAPH_TRAPPED_ADDR) >> 20) & 0x1f;
	chan = dev_priv->channels.ptr[chid];
	if (chan && chan->pgraph_ctx)
		nv10_graph_load_context(chan);

	pgraph->fifo_access(dev, true);
}

#define NV_WRITE_CTX(reg, val) do { \
	int offset = nv10_graph_ctx_regs_find_offset(dev, reg); \
	if (offset > 0) \
		pgraph_ctx->nv10[offset] = val; \
	} while (0)

#define NV17_WRITE_CTX(reg, val) do { \
	int offset = nv17_graph_ctx_regs_find_offset(dev, reg); \
	if (offset > 0) \
		pgraph_ctx->nv17[offset] = val; \
	} while (0)

struct nouveau_channel *
nv10_graph_channel(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int chid = dev_priv->engine.fifo.channels;

	if (nv_rd32(dev, NV10_PGRAPH_CTX_CONTROL) & 0x00010000)
		chid = nv_rd32(dev, NV10_PGRAPH_CTX_USER) >> 24;

	if (chid >= dev_priv->engine.fifo.channels)
		return NULL;

	return dev_priv->channels.ptr[chid];
}

int nv10_graph_create_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct graph_state *pgraph_ctx;

	NV_DEBUG(dev, "nv10_graph_context_create %d\n", chan->id);

	chan->pgraph_ctx = pgraph_ctx = kzalloc(sizeof(*pgraph_ctx),
						GFP_KERNEL);
	if (pgraph_ctx == NULL)
		return -ENOMEM;


	NV_WRITE_CTX(0x00400e88, 0x08000000);
	NV_WRITE_CTX(0x00400e9c, 0x4b7fffff);
	NV_WRITE_CTX(NV03_PGRAPH_XY_LOGIC_MISC0, 0x0001ffff);
	NV_WRITE_CTX(0x00400e10, 0x00001000);
	NV_WRITE_CTX(0x00400e14, 0x00001000);
	NV_WRITE_CTX(0x00400e30, 0x00080008);
	NV_WRITE_CTX(0x00400e34, 0x00080008);
	if (dev_priv->chipset >= 0x17) {
		/* is it really needed ??? */
		NV17_WRITE_CTX(NV10_PGRAPH_DEBUG_4,
					nv_rd32(dev, NV10_PGRAPH_DEBUG_4));
		NV17_WRITE_CTX(0x004006b0, nv_rd32(dev, 0x004006b0));
		NV17_WRITE_CTX(0x00400eac, 0x0fff0000);
		NV17_WRITE_CTX(0x00400eb0, 0x0fff0000);
		NV17_WRITE_CTX(0x00400ec0, 0x00000080);
		NV17_WRITE_CTX(0x00400ed0, 0x00000080);
	}
	NV_WRITE_CTX(NV10_PGRAPH_CTX_USER, chan->id << 24);

	nv10_graph_create_pipe(chan);
	return 0;
}

void nv10_graph_destroy_context(struct nouveau_channel *chan)
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

void
nv10_graph_set_region_tiling(struct drm_device *dev, int i, uint32_t addr,
			     uint32_t size, uint32_t pitch)
{
	uint32_t limit = max(1u, addr + size) - 1;

	if (pitch)
		addr |= 1 << 31;

	nv_wr32(dev, NV10_PGRAPH_TLIMIT(i), limit);
	nv_wr32(dev, NV10_PGRAPH_TSIZE(i), pitch);
	nv_wr32(dev, NV10_PGRAPH_TILE(i), addr);
}

int nv10_graph_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t tmp;
	int i;

	nv_wr32(dev, NV03_PMC_ENABLE, nv_rd32(dev, NV03_PMC_ENABLE) &
			~NV_PMC_ENABLE_PGRAPH);
	nv_wr32(dev, NV03_PMC_ENABLE, nv_rd32(dev, NV03_PMC_ENABLE) |
			 NV_PMC_ENABLE_PGRAPH);

	nv_wr32(dev, NV03_PGRAPH_INTR   , 0xFFFFFFFF);
	nv_wr32(dev, NV03_PGRAPH_INTR_EN, 0xFFFFFFFF);

	nv_wr32(dev, NV04_PGRAPH_DEBUG_0, 0xFFFFFFFF);
	nv_wr32(dev, NV04_PGRAPH_DEBUG_0, 0x00000000);
	nv_wr32(dev, NV04_PGRAPH_DEBUG_1, 0x00118700);
	/* nv_wr32(dev, NV04_PGRAPH_DEBUG_2, 0x24E00810); */ /* 0x25f92ad9 */
	nv_wr32(dev, NV04_PGRAPH_DEBUG_2, 0x25f92ad9);
	nv_wr32(dev, NV04_PGRAPH_DEBUG_3, 0x55DE0830 |
				      (1<<29) |
				      (1<<31));
	if (dev_priv->chipset >= 0x17) {
		nv_wr32(dev, NV10_PGRAPH_DEBUG_4, 0x1f000000);
		nv_wr32(dev, 0x400a10, 0x3ff3fb6);
		nv_wr32(dev, 0x400838, 0x2f8684);
		nv_wr32(dev, 0x40083c, 0x115f3f);
		nv_wr32(dev, 0x004006b0, 0x40000020);
	} else
		nv_wr32(dev, NV10_PGRAPH_DEBUG_4, 0x00000000);

	/* Turn all the tiling regions off. */
	for (i = 0; i < NV10_PFB_TILE__SIZE; i++)
		nv10_graph_set_region_tiling(dev, i, 0, 0, 0);

	nv_wr32(dev, NV10_PGRAPH_CTX_SWITCH(0), 0x00000000);
	nv_wr32(dev, NV10_PGRAPH_CTX_SWITCH(1), 0x00000000);
	nv_wr32(dev, NV10_PGRAPH_CTX_SWITCH(2), 0x00000000);
	nv_wr32(dev, NV10_PGRAPH_CTX_SWITCH(3), 0x00000000);
	nv_wr32(dev, NV10_PGRAPH_CTX_SWITCH(4), 0x00000000);
	nv_wr32(dev, NV10_PGRAPH_STATE, 0xFFFFFFFF);

	tmp  = nv_rd32(dev, NV10_PGRAPH_CTX_USER) & 0x00ffffff;
	tmp |= (dev_priv->engine.fifo.channels - 1) << 24;
	nv_wr32(dev, NV10_PGRAPH_CTX_USER, tmp);
	nv_wr32(dev, NV10_PGRAPH_CTX_CONTROL, 0x10000100);
	nv_wr32(dev, NV10_PGRAPH_FFINTFC_ST2, 0x08000000);

	return 0;
}

void nv10_graph_takedown(struct drm_device *dev)
{
}

static int
nv17_graph_mthd_lma_window(struct nouveau_channel *chan, int grclass,
			   int mthd, uint32_t data)
{
	struct drm_device *dev = chan->dev;
	struct graph_state *ctx = chan->pgraph_ctx;
	struct pipe_state *pipe = &ctx->pipe_state;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	uint32_t pipe_0x0040[1], pipe_0x64c0[8], pipe_0x6a80[3], pipe_0x6ab0[3];
	uint32_t xfmode0, xfmode1;
	int i;

	ctx->lma_window[(mthd - 0x1638) / 4] = data;

	if (mthd != 0x1644)
		return 0;

	nouveau_wait_for_idle(dev);

	PIPE_SAVE(dev, pipe_0x0040, 0x0040);
	PIPE_SAVE(dev, pipe->pipe_0x0200, 0x0200);

	PIPE_RESTORE(dev, ctx->lma_window, 0x6790);

	nouveau_wait_for_idle(dev);

	xfmode0 = nv_rd32(dev, NV10_PGRAPH_XFMODE0);
	xfmode1 = nv_rd32(dev, NV10_PGRAPH_XFMODE1);

	PIPE_SAVE(dev, pipe->pipe_0x4400, 0x4400);
	PIPE_SAVE(dev, pipe_0x64c0, 0x64c0);
	PIPE_SAVE(dev, pipe_0x6ab0, 0x6ab0);
	PIPE_SAVE(dev, pipe_0x6a80, 0x6a80);

	nouveau_wait_for_idle(dev);

	nv_wr32(dev, NV10_PGRAPH_XFMODE0, 0x10000000);
	nv_wr32(dev, NV10_PGRAPH_XFMODE1, 0x00000000);
	nv_wr32(dev, NV10_PGRAPH_PIPE_ADDRESS, 0x000064c0);
	for (i = 0; i < 4; i++)
		nv_wr32(dev, NV10_PGRAPH_PIPE_DATA, 0x3f800000);
	for (i = 0; i < 4; i++)
		nv_wr32(dev, NV10_PGRAPH_PIPE_DATA, 0x00000000);

	nv_wr32(dev, NV10_PGRAPH_PIPE_ADDRESS, 0x00006ab0);
	for (i = 0; i < 3; i++)
		nv_wr32(dev, NV10_PGRAPH_PIPE_DATA, 0x3f800000);

	nv_wr32(dev, NV10_PGRAPH_PIPE_ADDRESS, 0x00006a80);
	for (i = 0; i < 3; i++)
		nv_wr32(dev, NV10_PGRAPH_PIPE_DATA, 0x00000000);

	nv_wr32(dev, NV10_PGRAPH_PIPE_ADDRESS, 0x00000040);
	nv_wr32(dev, NV10_PGRAPH_PIPE_DATA, 0x00000008);

	PIPE_RESTORE(dev, pipe->pipe_0x0200, 0x0200);

	nouveau_wait_for_idle(dev);

	PIPE_RESTORE(dev, pipe_0x0040, 0x0040);

	nv_wr32(dev, NV10_PGRAPH_XFMODE0, xfmode0);
	nv_wr32(dev, NV10_PGRAPH_XFMODE1, xfmode1);

	PIPE_RESTORE(dev, pipe_0x64c0, 0x64c0);
	PIPE_RESTORE(dev, pipe_0x6ab0, 0x6ab0);
	PIPE_RESTORE(dev, pipe_0x6a80, 0x6a80);
	PIPE_RESTORE(dev, pipe->pipe_0x4400, 0x4400);

	nv_wr32(dev, NV10_PGRAPH_PIPE_ADDRESS, 0x000000c0);
	nv_wr32(dev, NV10_PGRAPH_PIPE_DATA, 0x00000000);

	nouveau_wait_for_idle(dev);

	pgraph->fifo_access(dev, true);

	return 0;
}

static int
nv17_graph_mthd_lma_enable(struct nouveau_channel *chan, int grclass,
			   int mthd, uint32_t data)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;

	nouveau_wait_for_idle(dev);

	nv_wr32(dev, NV10_PGRAPH_DEBUG_4,
		nv_rd32(dev, NV10_PGRAPH_DEBUG_4) | 0x1 << 8);
	nv_wr32(dev, 0x004006b0,
		nv_rd32(dev, 0x004006b0) | 0x8 << 24);

	pgraph->fifo_access(dev, true);

	return 0;
}

static struct nouveau_pgraph_object_method nv17_graph_celsius_mthds[] = {
	{ 0x1638, nv17_graph_mthd_lma_window },
	{ 0x163c, nv17_graph_mthd_lma_window },
	{ 0x1640, nv17_graph_mthd_lma_window },
	{ 0x1644, nv17_graph_mthd_lma_window },
	{ 0x1658, nv17_graph_mthd_lma_enable },
	{}
};

struct nouveau_pgraph_object_class nv10_graph_grclass[] = {
	{ 0x506e, NVOBJ_ENGINE_SW, NULL }, /* nvsw */
	{ 0x0030, NVOBJ_ENGINE_GR, NULL }, /* null */
	{ 0x0039, NVOBJ_ENGINE_GR, NULL }, /* m2mf */
	{ 0x004a, NVOBJ_ENGINE_GR, NULL }, /* gdirect */
	{ 0x005f, NVOBJ_ENGINE_GR, NULL }, /* imageblit */
	{ 0x009f, NVOBJ_ENGINE_GR, NULL }, /* imageblit (nv12) */
	{ 0x008a, NVOBJ_ENGINE_GR, NULL }, /* ifc */
	{ 0x0089, NVOBJ_ENGINE_GR, NULL }, /* sifm */
	{ 0x0062, NVOBJ_ENGINE_GR, NULL }, /* surf2d */
	{ 0x0043, NVOBJ_ENGINE_GR, NULL }, /* rop */
	{ 0x0012, NVOBJ_ENGINE_GR, NULL }, /* beta1 */
	{ 0x0072, NVOBJ_ENGINE_GR, NULL }, /* beta4 */
	{ 0x0019, NVOBJ_ENGINE_GR, NULL }, /* cliprect */
	{ 0x0044, NVOBJ_ENGINE_GR, NULL }, /* pattern */
	{ 0x0052, NVOBJ_ENGINE_GR, NULL }, /* swzsurf */
	{ 0x0093, NVOBJ_ENGINE_GR, NULL }, /* surf3d */
	{ 0x0094, NVOBJ_ENGINE_GR, NULL }, /* tex_tri */
	{ 0x0095, NVOBJ_ENGINE_GR, NULL }, /* multitex_tri */
	{ 0x0056, NVOBJ_ENGINE_GR, NULL }, /* celcius (nv10) */
	{ 0x0096, NVOBJ_ENGINE_GR, NULL }, /* celcius (nv11) */
	{ 0x0099, NVOBJ_ENGINE_GR, nv17_graph_celsius_mthds }, /* celcius (nv17) */
	{}
};
