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

#include <core/client.h>
#include <core/handle.h>
#include <core/gpuobj.h>
#include <core/option.h>

#include <subdev/fb.h>
#include <subdev/vm.h>
#include <subdev/bar.h>
#include <subdev/timer.h>

#include <engine/fifo.h>
#include <engine/graph.h>

#define GPC_MAX 32
#define TPC_MAX (GPC_MAX * 8)

#define ROP_BCAST(r)      (0x408800 + (r))
#define ROP_UNIT(u, r)    (0x410000 + (u) * 0x400 + (r))
#define GPC_BCAST(r)      (0x418000 + (r))
#define GPC_UNIT(t, r)    (0x500000 + (t) * 0x8000 + (r))
#define TPC_UNIT(t, m, r) (0x504000 + (t) * 0x8000 + (m) * 0x800 + (r))

struct nvc0_graph_data {
	u32 size;
	u32 align;
	u32 access;
};

struct nvc0_graph_mmio {
	u32 addr;
	u32 data;
	u32 shift;
	u32 buffer;
};

struct nvc0_graph_fuc {
	u32 *data;
	u32  size;
};

struct nvc0_graph_priv {
	struct nouveau_graph base;

	struct nvc0_graph_fuc fuc409c;
	struct nvc0_graph_fuc fuc409d;
	struct nvc0_graph_fuc fuc41ac;
	struct nvc0_graph_fuc fuc41ad;
	bool firmware;

	u8 rop_nr;
	u8 gpc_nr;
	u8 tpc_nr[GPC_MAX];
	u8 tpc_total;

	struct nouveau_gpuobj *unk4188b4;
	struct nouveau_gpuobj *unk4188b8;

	struct nvc0_graph_data mmio_data[4];
	struct nvc0_graph_mmio mmio_list[4096/8];
	u32  size;
	u32 *data;

	u8 magic_not_rop_nr;
};

struct nvc0_graph_chan {
	struct nouveau_graph_chan base;

	struct nouveau_gpuobj *mmio;
	struct nouveau_vma mmio_vma;
	int mmio_nr;
	struct {
		struct nouveau_gpuobj *mem;
		struct nouveau_vma vma;
	} data[4];
};

static inline u32
nvc0_graph_class(void *obj)
{
	struct nouveau_device *device = nv_device(obj);

	switch (device->chipset) {
	case 0xc0:
	case 0xc3:
	case 0xc4:
	case 0xce: /* guess, mmio trace shows only 0x9097 state */
	case 0xcf: /* guess, mmio trace shows only 0x9097 state */
		return 0x9097;
	case 0xc1:
		return 0x9197;
	case 0xc8:
	case 0xd9:
	case 0xd7:
		return 0x9297;
	case 0xe4:
	case 0xe7:
	case 0xe6:
		return 0xa097;
	case 0xf0:
		return 0xa197;
	default:
		return 0;
	}
}

void nv_icmd(struct nvc0_graph_priv *priv, u32 icmd, u32 data);

static inline void
nv_mthd(struct nvc0_graph_priv *priv, u32 class, u32 mthd, u32 data)
{
	nv_wr32(priv, 0x40448c, data);
	nv_wr32(priv, 0x404488, 0x80000000 | (mthd << 14) | class);
}

struct nvc0_grctx {
	struct nvc0_graph_priv *priv;
	struct nvc0_graph_data *data;
	struct nvc0_graph_mmio *mmio;
	struct nouveau_gpuobj *chan;
	int buffer_nr;
	u64 buffer[4];
	u64 addr;
};

int  nvc0_grctx_generate(struct nvc0_graph_priv *);
int  nvc0_grctx_init(struct nvc0_graph_priv *, struct nvc0_grctx *);
void nvc0_grctx_data(struct nvc0_grctx *, u32, u32, u32);
void nvc0_grctx_mmio(struct nvc0_grctx *, u32, u32, u32, u32);
int  nvc0_grctx_fini(struct nvc0_grctx *);

int  nve0_grctx_generate(struct nvc0_graph_priv *);

#define mmio_data(s,a,p) nvc0_grctx_data(&info, (s), (a), (p))
#define mmio_list(r,d,s,b) nvc0_grctx_mmio(&info, (r), (d), (s), (b))

void nvc0_graph_ctxctl_debug(struct nvc0_graph_priv *);
int  nvc0_graph_ctor_fw(struct nvc0_graph_priv *, const char *,
			struct nvc0_graph_fuc *);
void nvc0_graph_dtor(struct nouveau_object *);
void nvc0_graph_init_fw(struct nvc0_graph_priv *, u32 base,
			struct nvc0_graph_fuc *, struct nvc0_graph_fuc *);
int  nvc0_graph_context_ctor(struct nouveau_object *, struct nouveau_object *,
			     struct nouveau_oclass *, void *, u32,
			     struct nouveau_object **);
void nvc0_graph_context_dtor(struct nouveau_object *);

u64 nvc0_graph_units(struct nouveau_graph *);

#endif
