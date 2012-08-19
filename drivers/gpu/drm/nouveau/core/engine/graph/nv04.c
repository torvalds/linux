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

#include <core/os.h>
#include <core/class.h>
#include <core/handle.h>
#include <core/namedb.h>

#include <subdev/fb.h>
#include <subdev/instmem.h>
#include <subdev/timer.h>

#include <engine/fifo.h>
#include <engine/graph.h>

#include "regs.h"

static u32
nv04_graph_ctx_regs[] = {
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

struct nv04_graph_priv {
	struct nouveau_graph base;
	struct nv04_graph_chan *chan[16];
	spinlock_t lock;
};

struct nv04_graph_chan {
	struct nouveau_object base;
	int chid;
	u32 nv04[ARRAY_SIZE(nv04_graph_ctx_regs)];
};


static inline struct nv04_graph_priv *
nv04_graph_priv(struct nv04_graph_chan *chan)
{
	return (void *)nv_object(chan)->engine;
}

/*******************************************************************************
 * Graphics object classes
 ******************************************************************************/

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
nv04_graph_set_ctx1(struct nouveau_object *object, u32 mask, u32 value)
{
	struct nv04_graph_priv *priv = (void *)object->engine;
	int subc = (nv_rd32(priv, NV04_PGRAPH_TRAPPED_ADDR) >> 13) & 0x7;
	u32 tmp;

	tmp  = nv_ro32(object, 0x00);
	tmp &= ~mask;
	tmp |= value;
	nv_wo32(object, 0x00, tmp);

	nv_wr32(priv, NV04_PGRAPH_CTX_SWITCH1, tmp);
	nv_wr32(priv, NV04_PGRAPH_CTX_CACHE1 + (subc<<2), tmp);
}

static void
nv04_graph_set_ctx_val(struct nouveau_object *object, u32 mask, u32 value)
{
	int class, op, valid = 1;
	u32 tmp, ctx1;

	ctx1 = nv_ro32(object, 0x00);
	class = ctx1 & 0xff;
	op = (ctx1 >> 15) & 7;

	tmp = nv_ro32(object, 0x0c);
	tmp &= ~mask;
	tmp |= value;
	nv_wo32(object, 0x0c, tmp);

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

	nv04_graph_set_ctx1(object, 0x01000000, valid << 24);
}

static int
nv04_graph_mthd_set_operation(struct nouveau_object *object, u32 mthd,
			      void *args, u32 size)
{
	u32 class = nv_ro32(object, 0) & 0xff;
	u32 data = *(u32 *)args;
	if (data > 5)
		return 1;
	/* Old versions of the objects only accept first three operations. */
	if (data > 2 && class < 0x40)
		return 1;
	nv04_graph_set_ctx1(object, 0x00038000, data << 15);
	/* changing operation changes set of objects needed for validation */
	nv04_graph_set_ctx_val(object, 0, 0);
	return 0;
}

static int
nv04_graph_mthd_surf3d_clip_h(struct nouveau_object *object, u32 mthd,
			      void *args, u32 size)
{
	struct nv04_graph_priv *priv = (void *)object->engine;
	u32 data = *(u32 *)args;
	u32 min = data & 0xffff, max;
	u32 w = data >> 16;
	if (min & 0x8000)
		/* too large */
		return 1;
	if (w & 0x8000)
		/* yes, it accepts negative for some reason. */
		w |= 0xffff0000;
	max = min + w;
	max &= 0x3ffff;
	nv_wr32(priv, 0x40053c, min);
	nv_wr32(priv, 0x400544, max);
	return 0;
}

static int
nv04_graph_mthd_surf3d_clip_v(struct nouveau_object *object, u32 mthd,
			      void *args, u32 size)
{
	struct nv04_graph_priv *priv = (void *)object->engine;
	u32 data = *(u32 *)args;
	u32 min = data & 0xffff, max;
	u32 w = data >> 16;
	if (min & 0x8000)
		/* too large */
		return 1;
	if (w & 0x8000)
		/* yes, it accepts negative for some reason. */
		w |= 0xffff0000;
	max = min + w;
	max &= 0x3ffff;
	nv_wr32(priv, 0x400540, min);
	nv_wr32(priv, 0x400548, max);
	return 0;
}

static u16
nv04_graph_mthd_bind_class(struct nouveau_object *object, u32 *args, u32 size)
{
	struct nouveau_instmem *imem = nouveau_instmem(object);
	u32 inst = *(u32 *)args << 4;
	return nv_ro32(imem, inst);
}

static int
nv04_graph_mthd_bind_surf2d(struct nouveau_object *object, u32 mthd,
			    void *args, u32 size)
{
	switch (nv04_graph_mthd_bind_class(object, args, size)) {
	case 0x30:
		nv04_graph_set_ctx1(object, 0x00004000, 0);
		nv04_graph_set_ctx_val(object, 0x02000000, 0);
		return 0;
	case 0x42:
		nv04_graph_set_ctx1(object, 0x00004000, 0);
		nv04_graph_set_ctx_val(object, 0x02000000, 0x02000000);
		return 0;
	}
	return 1;
}

static int
nv04_graph_mthd_bind_surf2d_swzsurf(struct nouveau_object *object, u32 mthd,
				    void *args, u32 size)
{
	switch (nv04_graph_mthd_bind_class(object, args, size)) {
	case 0x30:
		nv04_graph_set_ctx1(object, 0x00004000, 0);
		nv04_graph_set_ctx_val(object, 0x02000000, 0);
		return 0;
	case 0x42:
		nv04_graph_set_ctx1(object, 0x00004000, 0);
		nv04_graph_set_ctx_val(object, 0x02000000, 0x02000000);
		return 0;
	case 0x52:
		nv04_graph_set_ctx1(object, 0x00004000, 0x00004000);
		nv04_graph_set_ctx_val(object, 0x02000000, 0x02000000);
		return 0;
	}
	return 1;
}

static int
nv01_graph_mthd_bind_patt(struct nouveau_object *object, u32 mthd,
			  void *args, u32 size)
{
	switch (nv04_graph_mthd_bind_class(object, args, size)) {
	case 0x30:
		nv04_graph_set_ctx_val(object, 0x08000000, 0);
		return 0;
	case 0x18:
		nv04_graph_set_ctx_val(object, 0x08000000, 0x08000000);
		return 0;
	}
	return 1;
}

static int
nv04_graph_mthd_bind_patt(struct nouveau_object *object, u32 mthd,
			  void *args, u32 size)
{
	switch (nv04_graph_mthd_bind_class(object, args, size)) {
	case 0x30:
		nv04_graph_set_ctx_val(object, 0x08000000, 0);
		return 0;
	case 0x44:
		nv04_graph_set_ctx_val(object, 0x08000000, 0x08000000);
		return 0;
	}
	return 1;
}

static int
nv04_graph_mthd_bind_rop(struct nouveau_object *object, u32 mthd,
			 void *args, u32 size)
{
	switch (nv04_graph_mthd_bind_class(object, args, size)) {
	case 0x30:
		nv04_graph_set_ctx_val(object, 0x10000000, 0);
		return 0;
	case 0x43:
		nv04_graph_set_ctx_val(object, 0x10000000, 0x10000000);
		return 0;
	}
	return 1;
}

static int
nv04_graph_mthd_bind_beta1(struct nouveau_object *object, u32 mthd,
			   void *args, u32 size)
{
	switch (nv04_graph_mthd_bind_class(object, args, size)) {
	case 0x30:
		nv04_graph_set_ctx_val(object, 0x20000000, 0);
		return 0;
	case 0x12:
		nv04_graph_set_ctx_val(object, 0x20000000, 0x20000000);
		return 0;
	}
	return 1;
}

static int
nv04_graph_mthd_bind_beta4(struct nouveau_object *object, u32 mthd,
			   void *args, u32 size)
{
	switch (nv04_graph_mthd_bind_class(object, args, size)) {
	case 0x30:
		nv04_graph_set_ctx_val(object, 0x40000000, 0);
		return 0;
	case 0x72:
		nv04_graph_set_ctx_val(object, 0x40000000, 0x40000000);
		return 0;
	}
	return 1;
}

static int
nv04_graph_mthd_bind_surf_dst(struct nouveau_object *object, u32 mthd,
			      void *args, u32 size)
{
	switch (nv04_graph_mthd_bind_class(object, args, size)) {
	case 0x30:
		nv04_graph_set_ctx_val(object, 0x02000000, 0);
		return 0;
	case 0x58:
		nv04_graph_set_ctx_val(object, 0x02000000, 0x02000000);
		return 0;
	}
	return 1;
}

static int
nv04_graph_mthd_bind_surf_src(struct nouveau_object *object, u32 mthd,
			      void *args, u32 size)
{
	switch (nv04_graph_mthd_bind_class(object, args, size)) {
	case 0x30:
		nv04_graph_set_ctx_val(object, 0x04000000, 0);
		return 0;
	case 0x59:
		nv04_graph_set_ctx_val(object, 0x04000000, 0x04000000);
		return 0;
	}
	return 1;
}

static int
nv04_graph_mthd_bind_surf_color(struct nouveau_object *object, u32 mthd,
				void *args, u32 size)
{
	switch (nv04_graph_mthd_bind_class(object, args, size)) {
	case 0x30:
		nv04_graph_set_ctx_val(object, 0x02000000, 0);
		return 0;
	case 0x5a:
		nv04_graph_set_ctx_val(object, 0x02000000, 0x02000000);
		return 0;
	}
	return 1;
}

static int
nv04_graph_mthd_bind_surf_zeta(struct nouveau_object *object, u32 mthd,
			       void *args, u32 size)
{
	switch (nv04_graph_mthd_bind_class(object, args, size)) {
	case 0x30:
		nv04_graph_set_ctx_val(object, 0x04000000, 0);
		return 0;
	case 0x5b:
		nv04_graph_set_ctx_val(object, 0x04000000, 0x04000000);
		return 0;
	}
	return 1;
}

static int
nv01_graph_mthd_bind_clip(struct nouveau_object *object, u32 mthd,
			  void *args, u32 size)
{
	switch (nv04_graph_mthd_bind_class(object, args, size)) {
	case 0x30:
		nv04_graph_set_ctx1(object, 0x2000, 0);
		return 0;
	case 0x19:
		nv04_graph_set_ctx1(object, 0x2000, 0x2000);
		return 0;
	}
	return 1;
}

static int
nv01_graph_mthd_bind_chroma(struct nouveau_object *object, u32 mthd,
			    void *args, u32 size)
{
	switch (nv04_graph_mthd_bind_class(object, args, size)) {
	case 0x30:
		nv04_graph_set_ctx1(object, 0x1000, 0);
		return 0;
	/* Yes, for some reason even the old versions of objects
	 * accept 0x57 and not 0x17. Consistency be damned.
	 */
	case 0x57:
		nv04_graph_set_ctx1(object, 0x1000, 0x1000);
		return 0;
	}
	return 1;
}

static struct nouveau_omthds
nv03_graph_gdi_omthds[] = {
	{ 0x0184, nv01_graph_mthd_bind_patt },
	{ 0x0188, nv04_graph_mthd_bind_rop },
	{ 0x018c, nv04_graph_mthd_bind_beta1 },
	{ 0x0190, nv04_graph_mthd_bind_surf_dst },
	{ 0x02fc, nv04_graph_mthd_set_operation },
	{}
};

static struct nouveau_omthds
nv04_graph_gdi_omthds[] = {
	{ 0x0188, nv04_graph_mthd_bind_patt },
	{ 0x018c, nv04_graph_mthd_bind_rop },
	{ 0x0190, nv04_graph_mthd_bind_beta1 },
	{ 0x0194, nv04_graph_mthd_bind_beta4 },
	{ 0x0198, nv04_graph_mthd_bind_surf2d },
	{ 0x02fc, nv04_graph_mthd_set_operation },
	{}
};

static struct nouveau_omthds
nv01_graph_blit_omthds[] = {
	{ 0x0184, nv01_graph_mthd_bind_chroma },
	{ 0x0188, nv01_graph_mthd_bind_clip },
	{ 0x018c, nv01_graph_mthd_bind_patt },
	{ 0x0190, nv04_graph_mthd_bind_rop },
	{ 0x0194, nv04_graph_mthd_bind_beta1 },
	{ 0x0198, nv04_graph_mthd_bind_surf_dst },
	{ 0x019c, nv04_graph_mthd_bind_surf_src },
	{ 0x02fc, nv04_graph_mthd_set_operation },
	{}
};

static struct nouveau_omthds
nv04_graph_blit_omthds[] = {
	{ 0x0184, nv01_graph_mthd_bind_chroma },
	{ 0x0188, nv01_graph_mthd_bind_clip },
	{ 0x018c, nv04_graph_mthd_bind_patt },
	{ 0x0190, nv04_graph_mthd_bind_rop },
	{ 0x0194, nv04_graph_mthd_bind_beta1 },
	{ 0x0198, nv04_graph_mthd_bind_beta4 },
	{ 0x019c, nv04_graph_mthd_bind_surf2d },
	{ 0x02fc, nv04_graph_mthd_set_operation },
	{}
};

static struct nouveau_omthds
nv04_graph_iifc_omthds[] = {
	{ 0x0188, nv01_graph_mthd_bind_chroma },
	{ 0x018c, nv01_graph_mthd_bind_clip },
	{ 0x0190, nv04_graph_mthd_bind_patt },
	{ 0x0194, nv04_graph_mthd_bind_rop },
	{ 0x0198, nv04_graph_mthd_bind_beta1 },
	{ 0x019c, nv04_graph_mthd_bind_beta4 },
	{ 0x01a0, nv04_graph_mthd_bind_surf2d_swzsurf },
	{ 0x03e4, nv04_graph_mthd_set_operation },
	{}
};

static struct nouveau_omthds
nv01_graph_ifc_omthds[] = {
	{ 0x0184, nv01_graph_mthd_bind_chroma },
	{ 0x0188, nv01_graph_mthd_bind_clip },
	{ 0x018c, nv01_graph_mthd_bind_patt },
	{ 0x0190, nv04_graph_mthd_bind_rop },
	{ 0x0194, nv04_graph_mthd_bind_beta1 },
	{ 0x0198, nv04_graph_mthd_bind_surf_dst },
	{ 0x02fc, nv04_graph_mthd_set_operation },
	{}
};

static struct nouveau_omthds
nv04_graph_ifc_omthds[] = {
	{ 0x0184, nv01_graph_mthd_bind_chroma },
	{ 0x0188, nv01_graph_mthd_bind_clip },
	{ 0x018c, nv04_graph_mthd_bind_patt },
	{ 0x0190, nv04_graph_mthd_bind_rop },
	{ 0x0194, nv04_graph_mthd_bind_beta1 },
	{ 0x0198, nv04_graph_mthd_bind_beta4 },
	{ 0x019c, nv04_graph_mthd_bind_surf2d },
	{ 0x02fc, nv04_graph_mthd_set_operation },
	{}
};

static struct nouveau_omthds
nv03_graph_sifc_omthds[] = {
	{ 0x0184, nv01_graph_mthd_bind_chroma },
	{ 0x0188, nv01_graph_mthd_bind_patt },
	{ 0x018c, nv04_graph_mthd_bind_rop },
	{ 0x0190, nv04_graph_mthd_bind_beta1 },
	{ 0x0194, nv04_graph_mthd_bind_surf_dst },
	{ 0x02fc, nv04_graph_mthd_set_operation },
	{}
};

static struct nouveau_omthds
nv04_graph_sifc_omthds[] = {
	{ 0x0184, nv01_graph_mthd_bind_chroma },
	{ 0x0188, nv04_graph_mthd_bind_patt },
	{ 0x018c, nv04_graph_mthd_bind_rop },
	{ 0x0190, nv04_graph_mthd_bind_beta1 },
	{ 0x0194, nv04_graph_mthd_bind_beta4 },
	{ 0x0198, nv04_graph_mthd_bind_surf2d },
	{ 0x02fc, nv04_graph_mthd_set_operation },
	{}
};

static struct nouveau_omthds
nv03_graph_sifm_omthds[] = {
	{ 0x0188, nv01_graph_mthd_bind_patt },
	{ 0x018c, nv04_graph_mthd_bind_rop },
	{ 0x0190, nv04_graph_mthd_bind_beta1 },
	{ 0x0194, nv04_graph_mthd_bind_surf_dst },
	{ 0x0304, nv04_graph_mthd_set_operation },
	{}
};

static struct nouveau_omthds
nv04_graph_sifm_omthds[] = {
	{ 0x0188, nv04_graph_mthd_bind_patt },
	{ 0x018c, nv04_graph_mthd_bind_rop },
	{ 0x0190, nv04_graph_mthd_bind_beta1 },
	{ 0x0194, nv04_graph_mthd_bind_beta4 },
	{ 0x0198, nv04_graph_mthd_bind_surf2d },
	{ 0x0304, nv04_graph_mthd_set_operation },
	{}
};

static struct nouveau_omthds
nv04_graph_surf3d_omthds[] = {
	{ 0x02f8, nv04_graph_mthd_surf3d_clip_h },
	{ 0x02fc, nv04_graph_mthd_surf3d_clip_v },
	{}
};

static struct nouveau_omthds
nv03_graph_ttri_omthds[] = {
	{ 0x0188, nv01_graph_mthd_bind_clip },
	{ 0x018c, nv04_graph_mthd_bind_surf_color },
	{ 0x0190, nv04_graph_mthd_bind_surf_zeta },
	{}
};

static struct nouveau_omthds
nv01_graph_prim_omthds[] = {
	{ 0x0184, nv01_graph_mthd_bind_clip },
	{ 0x0188, nv01_graph_mthd_bind_patt },
	{ 0x018c, nv04_graph_mthd_bind_rop },
	{ 0x0190, nv04_graph_mthd_bind_beta1 },
	{ 0x0194, nv04_graph_mthd_bind_surf_dst },
	{ 0x02fc, nv04_graph_mthd_set_operation },
	{}
};

static struct nouveau_omthds
nv04_graph_prim_omthds[] = {
	{ 0x0184, nv01_graph_mthd_bind_clip },
	{ 0x0188, nv04_graph_mthd_bind_patt },
	{ 0x018c, nv04_graph_mthd_bind_rop },
	{ 0x0190, nv04_graph_mthd_bind_beta1 },
	{ 0x0194, nv04_graph_mthd_bind_beta4 },
	{ 0x0198, nv04_graph_mthd_bind_surf2d },
	{ 0x02fc, nv04_graph_mthd_set_operation },
	{}
};

static int
nv04_graph_object_ctor(struct nouveau_object *parent,
		       struct nouveau_object *engine,
		       struct nouveau_oclass *oclass, void *data, u32 size,
		       struct nouveau_object **pobject)
{
	struct nouveau_gpuobj *obj;
	int ret;

	ret = nouveau_gpuobj_create(parent, engine, oclass, 0, parent,
				    16, 16, 0, &obj);
	*pobject = nv_object(obj);
	if (ret)
		return ret;

	nv_wo32(obj, 0x00, nv_mclass(obj));
#ifdef __BIG_ENDIAN
	nv_mo32(obj, 0x00, 0x00080000, 0x00080000);
#endif
	nv_wo32(obj, 0x04, 0x00000000);
	nv_wo32(obj, 0x08, 0x00000000);
	nv_wo32(obj, 0x0c, 0x00000000);
	return 0;
}

struct nouveau_ofuncs
nv04_graph_ofuncs = {
	.ctor = nv04_graph_object_ctor,
	.dtor = _nouveau_gpuobj_dtor,
	.init = _nouveau_gpuobj_init,
	.fini = _nouveau_gpuobj_fini,
	.rd32 = _nouveau_gpuobj_rd32,
	.wr32 = _nouveau_gpuobj_wr32,
};

static struct nouveau_oclass
nv04_graph_sclass[] = {
	{ 0x0012, &nv04_graph_ofuncs }, /* beta1 */
	{ 0x0017, &nv04_graph_ofuncs }, /* chroma */
	{ 0x0018, &nv04_graph_ofuncs }, /* pattern (nv01) */
	{ 0x0019, &nv04_graph_ofuncs }, /* clip */
	{ 0x001c, &nv04_graph_ofuncs, nv01_graph_prim_omthds }, /* line */
	{ 0x001d, &nv04_graph_ofuncs, nv01_graph_prim_omthds }, /* tri */
	{ 0x001e, &nv04_graph_ofuncs, nv01_graph_prim_omthds }, /* rect */
	{ 0x001f, &nv04_graph_ofuncs, nv01_graph_blit_omthds },
	{ 0x0021, &nv04_graph_ofuncs, nv01_graph_ifc_omthds },
	{ 0x0030, &nv04_graph_ofuncs }, /* null */
	{ 0x0036, &nv04_graph_ofuncs, nv03_graph_sifc_omthds },
	{ 0x0037, &nv04_graph_ofuncs, nv03_graph_sifm_omthds },
	{ 0x0038, &nv04_graph_ofuncs }, /* dvd subpicture */
	{ 0x0039, &nv04_graph_ofuncs }, /* m2mf */
	{ 0x0042, &nv04_graph_ofuncs }, /* surf2d */
	{ 0x0043, &nv04_graph_ofuncs }, /* rop */
	{ 0x0044, &nv04_graph_ofuncs }, /* pattern */
	{ 0x0048, &nv04_graph_ofuncs, nv03_graph_ttri_omthds },
	{ 0x004a, &nv04_graph_ofuncs, nv04_graph_gdi_omthds },
	{ 0x004b, &nv04_graph_ofuncs, nv03_graph_gdi_omthds },
	{ 0x0052, &nv04_graph_ofuncs }, /* swzsurf */
	{ 0x0053, &nv04_graph_ofuncs, nv04_graph_surf3d_omthds },
	{ 0x0054, &nv04_graph_ofuncs }, /* ttri */
	{ 0x0055, &nv04_graph_ofuncs }, /* mtri */
	{ 0x0057, &nv04_graph_ofuncs }, /* chroma */
	{ 0x0058, &nv04_graph_ofuncs }, /* surf_dst */
	{ 0x0059, &nv04_graph_ofuncs }, /* surf_src */
	{ 0x005a, &nv04_graph_ofuncs }, /* surf_color */
	{ 0x005b, &nv04_graph_ofuncs }, /* surf_zeta */
	{ 0x005c, &nv04_graph_ofuncs, nv04_graph_prim_omthds }, /* line */
	{ 0x005d, &nv04_graph_ofuncs, nv04_graph_prim_omthds }, /* tri */
	{ 0x005e, &nv04_graph_ofuncs, nv04_graph_prim_omthds }, /* rect */
	{ 0x005f, &nv04_graph_ofuncs, nv04_graph_blit_omthds },
	{ 0x0060, &nv04_graph_ofuncs, nv04_graph_iifc_omthds },
	{ 0x0061, &nv04_graph_ofuncs, nv04_graph_ifc_omthds },
	{ 0x0064, &nv04_graph_ofuncs }, /* iifc (nv05) */
	{ 0x0065, &nv04_graph_ofuncs }, /* ifc (nv05) */
	{ 0x0066, &nv04_graph_ofuncs }, /* sifc (nv05) */
	{ 0x0072, &nv04_graph_ofuncs }, /* beta4 */
	{ 0x0076, &nv04_graph_ofuncs, nv04_graph_sifc_omthds },
	{ 0x0077, &nv04_graph_ofuncs, nv04_graph_sifm_omthds },
	{},
};

/*******************************************************************************
 * PGRAPH context
 ******************************************************************************/

static struct nv04_graph_chan *
nv04_graph_channel(struct nv04_graph_priv *priv)
{
	struct nv04_graph_chan *chan = NULL;
	if (nv_rd32(priv, NV04_PGRAPH_CTX_CONTROL) & 0x00010000) {
		int chid = nv_rd32(priv, NV04_PGRAPH_CTX_USER) >> 24;
		if (chid < ARRAY_SIZE(priv->chan))
			chan = priv->chan[chid];
	}
	return chan;
}

static int
nv04_graph_load_context(struct nv04_graph_chan *chan, int chid)
{
	struct nv04_graph_priv *priv = nv04_graph_priv(chan);
	int i;

	for (i = 0; i < ARRAY_SIZE(nv04_graph_ctx_regs); i++)
		nv_wr32(priv, nv04_graph_ctx_regs[i], chan->nv04[i]);

	nv_wr32(priv, NV04_PGRAPH_CTX_CONTROL, 0x10010100);
	nv_mask(priv, NV04_PGRAPH_CTX_USER, 0xff000000, chid << 24);
	nv_mask(priv, NV04_PGRAPH_FFINTFC_ST2, 0xfff00000, 0x00000000);
	return 0;
}

static int
nv04_graph_unload_context(struct nv04_graph_chan *chan)
{
	struct nv04_graph_priv *priv = nv04_graph_priv(chan);
	int i;

	for (i = 0; i < ARRAY_SIZE(nv04_graph_ctx_regs); i++)
		chan->nv04[i] = nv_rd32(priv, nv04_graph_ctx_regs[i]);

	nv_wr32(priv, NV04_PGRAPH_CTX_CONTROL, 0x10000000);
	nv_mask(priv, NV04_PGRAPH_CTX_USER, 0xff000000, 0x0f000000);
	return 0;
}

static void
nv04_graph_context_switch(struct nv04_graph_priv *priv)
{
	struct nv04_graph_chan *prev = NULL;
	struct nv04_graph_chan *next = NULL;
	unsigned long flags;
	int chid;

	spin_lock_irqsave(&priv->lock, flags);
	nv04_graph_idle(priv);

	/* If previous context is valid, we need to save it */
	prev = nv04_graph_channel(priv);
	if (prev)
		nv04_graph_unload_context(prev);

	/* load context for next channel */
	chid = (nv_rd32(priv, NV04_PGRAPH_TRAPPED_ADDR) >> 24) & 0x0f;
	next = priv->chan[chid];
	if (next)
		nv04_graph_load_context(next, chid);

	spin_unlock_irqrestore(&priv->lock, flags);
}

static u32 *ctx_reg(struct nv04_graph_chan *chan, u32 reg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(nv04_graph_ctx_regs); i++) {
		if (nv04_graph_ctx_regs[i] == reg)
			return &chan->nv04[i];
	}

	return NULL;
}

static int
nv04_graph_context_ctor(struct nouveau_object *parent,
			struct nouveau_object *engine,
			struct nouveau_oclass *oclass, void *data, u32 size,
			struct nouveau_object **pobject)
{
	struct nouveau_fifo_chan *fifo = (void *)parent;
	struct nv04_graph_priv *priv = (void *)engine;
	struct nv04_graph_chan *chan;
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

	*ctx_reg(chan, NV04_PGRAPH_DEBUG_3) = 0xfad4ff31;

	priv->chan[fifo->chid] = chan;
	chan->chid = fifo->chid;
	spin_unlock_irqrestore(&priv->lock, flags);
	return 0;
}

static void
nv04_graph_context_dtor(struct nouveau_object *object)
{
	struct nv04_graph_priv *priv = (void *)object->engine;
	struct nv04_graph_chan *chan = (void *)object;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	priv->chan[chan->chid] = NULL;
	spin_unlock_irqrestore(&priv->lock, flags);

	nouveau_object_destroy(&chan->base);
}

static int
nv04_graph_context_fini(struct nouveau_object *object, bool suspend)
{
	struct nv04_graph_priv *priv = (void *)object->engine;
	struct nv04_graph_chan *chan = (void *)object;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	nv_mask(priv, NV04_PGRAPH_FIFO, 0x00000001, 0x00000000);
	if (nv04_graph_channel(priv) == chan)
		nv04_graph_unload_context(chan);
	nv_mask(priv, NV04_PGRAPH_FIFO, 0x00000001, 0x00000001);
	spin_unlock_irqrestore(&priv->lock, flags);

	return nouveau_object_fini(&chan->base, suspend);
}

static struct nouveau_oclass
nv04_graph_cclass = {
	.handle = NV_ENGCTX(GR, 0x04),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv04_graph_context_ctor,
		.dtor = nv04_graph_context_dtor,
		.init = nouveau_object_init,
		.fini = nv04_graph_context_fini,
	},
};

/*******************************************************************************
 * PGRAPH engine/subdev functions
 ******************************************************************************/

bool
nv04_graph_idle(void *obj)
{
	struct nouveau_graph *graph = nouveau_graph(obj);
	u32 mask = 0xffffffff;

	if (nv_device(obj)->card_type == NV_40)
		mask &= ~NV40_PGRAPH_STATUS_SYNC_STALL;

	if (!nv_wait(graph, NV04_PGRAPH_STATUS, mask, 0)) {
		nv_error(graph, "idle timed out with status 0x%08x\n",
			 nv_rd32(graph, NV04_PGRAPH_STATUS));
		return false;
	}

	return true;
}

static const struct nouveau_bitfield
nv04_graph_intr_name[] = {
	{ NV_PGRAPH_INTR_NOTIFY, "NOTIFY" },
	{}
};

static const struct nouveau_bitfield
nv04_graph_nstatus[] = {
	{ NV04_PGRAPH_NSTATUS_STATE_IN_USE,       "STATE_IN_USE" },
	{ NV04_PGRAPH_NSTATUS_INVALID_STATE,      "INVALID_STATE" },
	{ NV04_PGRAPH_NSTATUS_BAD_ARGUMENT,       "BAD_ARGUMENT" },
	{ NV04_PGRAPH_NSTATUS_PROTECTION_FAULT,   "PROTECTION_FAULT" },
	{}
};

const struct nouveau_bitfield
nv04_graph_nsource[] = {
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
nv04_graph_intr(struct nouveau_subdev *subdev)
{
	struct nv04_graph_priv *priv = (void *)subdev;
	struct nv04_graph_chan *chan = NULL;
	struct nouveau_namedb *namedb = NULL;
	struct nouveau_handle *handle = NULL;
	u32 stat = nv_rd32(priv, NV03_PGRAPH_INTR);
	u32 nsource = nv_rd32(priv, NV03_PGRAPH_NSOURCE);
	u32 nstatus = nv_rd32(priv, NV03_PGRAPH_NSTATUS);
	u32 addr = nv_rd32(priv, NV04_PGRAPH_TRAPPED_ADDR);
	u32 chid = (addr & 0x0f000000) >> 24;
	u32 subc = (addr & 0x0000e000) >> 13;
	u32 mthd = (addr & 0x00001ffc);
	u32 data = nv_rd32(priv, NV04_PGRAPH_TRAPPED_DATA);
	u32 class = nv_rd32(priv, 0x400180 + subc * 4) & 0xff;
	u32 inst = (nv_rd32(priv, 0x40016c) & 0xffff) << 4;
	u32 show = stat;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	chan = priv->chan[chid];
	if (chan)
		namedb = (void *)nv_pclass(nv_object(chan), NV_NAMEDB_CLASS);
	spin_unlock_irqrestore(&priv->lock, flags);

	if (stat & NV_PGRAPH_INTR_NOTIFY) {
		if (chan && (nsource & NV03_PGRAPH_NSOURCE_ILLEGAL_MTHD)) {
			handle = nouveau_namedb_get_vinst(namedb, inst);
			if (handle && !nv_call(handle->object, mthd, data))
				show &= ~NV_PGRAPH_INTR_NOTIFY;
		}
	}

	if (stat & NV_PGRAPH_INTR_CONTEXT_SWITCH) {
		nv_wr32(priv, NV03_PGRAPH_INTR, NV_PGRAPH_INTR_CONTEXT_SWITCH);
		stat &= ~NV_PGRAPH_INTR_CONTEXT_SWITCH;
		show &= ~NV_PGRAPH_INTR_CONTEXT_SWITCH;
		nv04_graph_context_switch(priv);
	}

	nv_wr32(priv, NV03_PGRAPH_INTR, stat);
	nv_wr32(priv, NV04_PGRAPH_FIFO, 0x00000001);

	if (show) {
		nv_error(priv, "");
		nouveau_bitfield_print(nv04_graph_intr_name, show);
		printk(" nsource:");
		nouveau_bitfield_print(nv04_graph_nsource, nsource);
		printk(" nstatus:");
		nouveau_bitfield_print(nv04_graph_nstatus, nstatus);
		printk("\n");
		nv_error(priv, "ch %d/%d class 0x%04x "
			       "mthd 0x%04x data 0x%08x\n",
			 chid, subc, class, mthd, data);
	}

	nouveau_namedb_put(handle);
}

static int
nv04_graph_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct nv04_graph_priv *priv;
	int ret;

	ret = nouveau_graph_create(parent, engine, oclass, true, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00001000;
	nv_subdev(priv)->intr = nv04_graph_intr;
	nv_engine(priv)->cclass = &nv04_graph_cclass;
	nv_engine(priv)->sclass = nv04_graph_sclass;
	spin_lock_init(&priv->lock);
	return 0;
}

static int
nv04_graph_init(struct nouveau_object *object)
{
	struct nouveau_engine *engine = nv_engine(object);
	struct nv04_graph_priv *priv = (void *)engine;
	int ret;

	ret = nouveau_graph_init(&priv->base);
	if (ret)
		return ret;

	/* Enable PGRAPH interrupts */
	nv_wr32(priv, NV03_PGRAPH_INTR, 0xFFFFFFFF);
	nv_wr32(priv, NV03_PGRAPH_INTR_EN, 0xFFFFFFFF);

	nv_wr32(priv, NV04_PGRAPH_VALID1, 0);
	nv_wr32(priv, NV04_PGRAPH_VALID2, 0);
	/*nv_wr32(priv, NV04_PGRAPH_DEBUG_0, 0x000001FF);
	nv_wr32(priv, NV04_PGRAPH_DEBUG_0, 0x001FFFFF);*/
	nv_wr32(priv, NV04_PGRAPH_DEBUG_0, 0x1231c000);
	/*1231C000 blob, 001 haiku*/
	/*V_WRITE(NV04_PGRAPH_DEBUG_1, 0xf2d91100);*/
	nv_wr32(priv, NV04_PGRAPH_DEBUG_1, 0x72111100);
	/*0x72111100 blob , 01 haiku*/
	/*nv_wr32(priv, NV04_PGRAPH_DEBUG_2, 0x11d5f870);*/
	nv_wr32(priv, NV04_PGRAPH_DEBUG_2, 0x11d5f071);
	/*haiku same*/

	/*nv_wr32(priv, NV04_PGRAPH_DEBUG_3, 0xfad4ff31);*/
	nv_wr32(priv, NV04_PGRAPH_DEBUG_3, 0xf0d4ff31);
	/*haiku and blob 10d4*/

	nv_wr32(priv, NV04_PGRAPH_STATE        , 0xFFFFFFFF);
	nv_wr32(priv, NV04_PGRAPH_CTX_CONTROL  , 0x10000100);
	nv_mask(priv, NV04_PGRAPH_CTX_USER, 0xff000000, 0x0f000000);

	/* These don't belong here, they're part of a per-channel context */
	nv_wr32(priv, NV04_PGRAPH_PATTERN_SHAPE, 0x00000000);
	nv_wr32(priv, NV04_PGRAPH_BETA_AND     , 0xFFFFFFFF);
	return 0;
}

struct nouveau_oclass
nv04_graph_oclass = {
	.handle = NV_ENGINE(GR, 0x04),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv04_graph_ctor,
		.dtor = _nouveau_graph_dtor,
		.init = nv04_graph_init,
		.fini = _nouveau_graph_fini,
	},
};
