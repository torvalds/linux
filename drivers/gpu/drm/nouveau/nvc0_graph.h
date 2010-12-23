/*
 * Copyright 2010 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#ifndef __NVC0_GRAPH_H__
#define __NVC0_GRAPH_H__

#define GPC_MAX 4
#define TP_MAX 32

#define ROP_BCAST(r)   (0x408800 + (r))
#define ROP_UNIT(u,r)  (0x410000 + (u) * 0x400 + (r))
#define GPC_BCAST(r)   (0x418000 + (r))
#define GPC_UNIT(t,r)  (0x500000 + (t) * 0x8000 + (r))
#define TP_UNIT(t,m,r) (0x504000 + (t) * 0x8000 + (m) * 0x800 + (r))

struct nvc0_graph_priv {
	u8 gpc_nr;
	u8 rop_nr;
	u8 tp_nr[GPC_MAX];
	u8 tp_total;

	u32  grctx_size;
	u32 *grctx_vals;
	struct nouveau_gpuobj *unk4188b4;
	struct nouveau_gpuobj *unk4188b8;

	u8  magic_not_rop_nr;
	u32 magicgpc980[4];
	u32 magicgpc918;
};

struct nvc0_graph_chan {
	struct nouveau_gpuobj *grctx;
	struct nouveau_gpuobj *unk408004; // 0x418810 too
	struct nouveau_gpuobj *unk40800c; // 0x419004 too
	struct nouveau_gpuobj *unk418810; // 0x419848 too
	struct nouveau_gpuobj *mmio;
	int mmio_nr;
};

int nvc0_grctx_generate(struct nouveau_channel *);

#endif
