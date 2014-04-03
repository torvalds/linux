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

int  nvc0_grctx_generate(struct nvc0_graph_priv *);

int  nvc0_graph_context_ctor(struct nouveau_object *, struct nouveau_object *,
			     struct nouveau_oclass *, void *, u32,
			     struct nouveau_object **);
void nvc0_graph_context_dtor(struct nouveau_object *);

void nvc0_graph_ctxctl_debug(struct nvc0_graph_priv *);

u64  nvc0_graph_units(struct nouveau_graph *);
int  nvc0_graph_ctor(struct nouveau_object *, struct nouveau_object *,
		     struct nouveau_oclass *, void *data, u32 size,
		     struct nouveau_object **);
void nvc0_graph_dtor(struct nouveau_object *);
int  nvc0_graph_init(struct nouveau_object *);
int  nve4_graph_init(struct nouveau_object *);

extern struct nouveau_oclass nvc0_graph_sclass[];

extern struct nouveau_oclass nvc8_graph_sclass[];

struct nvc0_graph_init {
	u32 addr;
	u8  count;
	u8  pitch;
	u32 data;
};

struct nvc0_graph_mthd {
	u16 oclass;
	struct nvc0_graph_init *init;
};

struct nvc0_grctx {
	struct nvc0_graph_priv *priv;
	struct nvc0_graph_data *data;
	struct nvc0_graph_mmio *mmio;
	int buffer_nr;
	u64 buffer[4];
	u64 addr;
};

struct nvc0_grctx_oclass {
	struct nouveau_oclass base;
	/* main context generation function */
	void  (*main)(struct nvc0_graph_priv *, struct nvc0_grctx *);
	/* context-specific modify-on-first-load list generation function */
	void  (*mods)(struct nvc0_graph_priv *, struct nvc0_grctx *);
	void  (*unkn)(struct nvc0_graph_priv *);
	/* mmio context data */
	struct nvc0_graph_init **hub;
	struct nvc0_graph_init **gpc;
	/* indirect context data, generated with icmds/mthds */
	struct nvc0_graph_init *icmd;
	struct nvc0_graph_mthd *mthd;
};

struct nvc0_graph_ucode {
	struct nvc0_graph_fuc code;
	struct nvc0_graph_fuc data;
};

extern struct nvc0_graph_ucode nvc0_graph_fecs_ucode;
extern struct nvc0_graph_ucode nvc0_graph_gpccs_ucode;

struct nvc0_graph_oclass {
	struct nouveau_oclass base;
	struct nouveau_oclass **cclass;
	struct nouveau_oclass *sclass;
	struct nvc0_graph_init **mmio;
	struct {
		struct nvc0_graph_ucode *ucode;
	} fecs;
	struct {
		struct nvc0_graph_ucode *ucode;
	} gpccs;
};

void nvc0_graph_mmio(struct nvc0_graph_priv *, struct nvc0_graph_init *);
void nvc0_graph_icmd(struct nvc0_graph_priv *, struct nvc0_graph_init *);
void nvc0_graph_mthd(struct nvc0_graph_priv *, struct nvc0_graph_mthd *);
int  nvc0_graph_init_ctxctl(struct nvc0_graph_priv *);

extern struct nvc0_graph_init nvc0_graph_init_regs[];
extern struct nvc0_graph_init nvc0_graph_init_unk40xx[];
extern struct nvc0_graph_init nvc0_graph_init_unk44xx[];
extern struct nvc0_graph_init nvc0_graph_init_unk78xx[];
extern struct nvc0_graph_init nvc0_graph_init_unk60xx[];
extern struct nvc0_graph_init nvc0_graph_init_unk58xx[];
extern struct nvc0_graph_init nvc0_graph_init_unk80xx[];
extern struct nvc0_graph_init nvc0_graph_init_gpc[];
extern struct nvc0_graph_init nvc0_graph_init_unk88xx[];
extern struct nvc0_graph_init nvc0_graph_tpc_0[];

extern struct nvc0_graph_init nvc3_graph_init_unk58xx[];

extern struct nvc0_graph_init nvd9_graph_init_unk58xx[];
extern struct nvc0_graph_init nvd9_graph_init_unk64xx[];

extern struct nvc0_graph_init nve4_graph_init_regs[];
extern struct nvc0_graph_init nve4_graph_init_unk[];
extern struct nvc0_graph_init nve4_graph_init_unk88xx[];

extern struct nvc0_graph_init nvf0_graph_init_unk40xx[];
extern struct nvc0_graph_init nvf0_graph_init_unk70xx[];
extern struct nvc0_graph_init nvf0_graph_init_unk5bxx[];
extern struct nvc0_graph_init nvf0_graph_init_tpc[];

int  nvc0_grctx_generate(struct nvc0_graph_priv *);
void nvc0_grctx_generate_main(struct nvc0_graph_priv *, struct nvc0_grctx *);
void nvc0_grctx_generate_mods(struct nvc0_graph_priv *, struct nvc0_grctx *);
void nvc0_grctx_generate_unkn(struct nvc0_graph_priv *);
void nvc0_grctx_generate_tpcid(struct nvc0_graph_priv *);
void nvc0_grctx_generate_r406028(struct nvc0_graph_priv *);
void nvc0_grctx_generate_r4060a8(struct nvc0_graph_priv *);
void nvc0_grctx_generate_r418bb8(struct nvc0_graph_priv *);
void nve4_grctx_generate_r418bb8(struct nvc0_graph_priv *);
void nvc0_grctx_generate_r406800(struct nvc0_graph_priv *);

extern struct nouveau_oclass *nvc0_grctx_oclass;
extern struct nvc0_graph_init *nvc0_grctx_init_hub[];
extern struct nvc0_graph_init nvc0_grctx_init_base[];
extern struct nvc0_graph_init nvc0_grctx_init_unk40xx[];
extern struct nvc0_graph_init nvc0_grctx_init_unk44xx[];
extern struct nvc0_graph_init nvc0_grctx_init_unk46xx[];
extern struct nvc0_graph_init nvc0_grctx_init_unk47xx[];
extern struct nvc0_graph_init nvc0_grctx_init_unk60xx[];
extern struct nvc0_graph_init nvc0_grctx_init_unk64xx[];
extern struct nvc0_graph_init nvc0_grctx_init_unk78xx[];
extern struct nvc0_graph_init nvc0_grctx_init_unk80xx[];
extern struct nvc0_graph_init nvc0_grctx_init_gpc_0[];
extern struct nvc0_graph_init nvc0_grctx_init_gpc_1[];
extern struct nvc0_graph_init nvc0_grctx_init_tpc[];
extern struct nvc0_graph_init nvc0_grctx_init_icmd[];
extern struct nvc0_graph_init nvd9_grctx_init_icmd[]; //

extern struct nvc0_graph_mthd nvc0_grctx_init_mthd[];
extern struct nvc0_graph_init nvc0_grctx_init_902d[];
extern struct nvc0_graph_init nvc0_grctx_init_9039[];
extern struct nvc0_graph_init nvc0_grctx_init_90c0[];
extern struct nvc0_graph_init nvc0_grctx_init_mthd_magic[];

void nvc1_grctx_generate_mods(struct nvc0_graph_priv *, struct nvc0_grctx *);
void nvc1_grctx_generate_unkn(struct nvc0_graph_priv *);
extern struct nouveau_oclass *nvc1_grctx_oclass;
extern struct nvc0_graph_init nvc1_grctx_init_9097[];

extern struct nouveau_oclass *nvc3_grctx_oclass;

extern struct nouveau_oclass *nvc8_grctx_oclass;
extern struct nvc0_graph_init nvc8_grctx_init_9197[];
extern struct nvc0_graph_init nvc8_grctx_init_9297[];

extern struct nouveau_oclass *nvd7_grctx_oclass;

extern struct nouveau_oclass *nvd9_grctx_oclass;
extern struct nvc0_graph_init nvd9_grctx_init_rop[];
extern struct nvc0_graph_mthd nvd9_grctx_init_mthd[];

void nve4_grctx_generate_main(struct nvc0_graph_priv *, struct nvc0_grctx *);
void nve4_grctx_generate_unkn(struct nvc0_graph_priv *);
extern struct nouveau_oclass *nve4_grctx_oclass;
extern struct nvc0_graph_init nve4_grctx_init_unk46xx[];
extern struct nvc0_graph_init nve4_grctx_init_unk47xx[];
extern struct nvc0_graph_init nve4_grctx_init_unk58xx[];
extern struct nvc0_graph_init nve4_grctx_init_unk80xx[];
extern struct nvc0_graph_init nve4_grctx_init_unk90xx[];

extern struct nouveau_oclass *nvf0_grctx_oclass;
extern struct nvc0_graph_init nvf0_grctx_init_unk44xx[];
extern struct nvc0_graph_init nvf0_grctx_init_unk5bxx[];
extern struct nvc0_graph_init nvf0_grctx_init_unk60xx[];

extern struct nouveau_oclass *nv108_grctx_oclass;

#define mmio_data(s,a,p) do {                                                  \
	info->buffer[info->buffer_nr] = round_up(info->addr, (a));             \
	info->addr = info->buffer[info->buffer_nr++] + (s);                    \
	info->data->size = (s);                                                \
	info->data->align = (a);                                               \
	info->data->access = (p);                                              \
	info->data++;                                                          \
} while(0)

#define mmio_list(r,d,s,b) do {                                                \
	info->mmio->addr = (r);                                                \
	info->mmio->data = (d);                                                \
	info->mmio->shift = (s);                                               \
	info->mmio->buffer = (b);                                              \
	info->mmio++;                                                          \
	nv_wr32(priv, (r), (d) | ((s) ? (info->buffer[(b)] >> (s)) : 0));      \
} while(0)

#endif
