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

#include <nvif/unpack.h>
#include <nvif/class.h>

#include <subdev/fb.h>
#include <subdev/vm.h>
#include <subdev/bar.h>
#include <subdev/timer.h>
#include <subdev/mc.h>
#include <subdev/ltc.h>

#include <engine/fifo.h>
#include <engine/graph.h>

#include "fuc/os.h"

#define GPC_MAX 32
#define TPC_MAX (GPC_MAX * 8)

#define ROP_BCAST(r)      (0x408800 + (r))
#define ROP_UNIT(u, r)    (0x410000 + (u) * 0x400 + (r))
#define GPC_BCAST(r)      (0x418000 + (r))
#define GPC_UNIT(t, r)    (0x500000 + (t) * 0x8000 + (r))
#define PPC_UNIT(t, m, r) (0x503000 + (t) * 0x8000 + (m) * 0x200 + (r))
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
	int buffer;
};

struct nvc0_graph_fuc {
	u32 *data;
	u32  size;
};

struct nvc0_graph_zbc_color {
	u32 format;
	u32 ds[4];
	u32 l2[4];
};

struct nvc0_graph_zbc_depth {
	u32 format;
	u32 ds;
	u32 l2;
};

struct nvc0_graph_priv {
	struct nouveau_graph base;

	struct nvc0_graph_fuc fuc409c;
	struct nvc0_graph_fuc fuc409d;
	struct nvc0_graph_fuc fuc41ac;
	struct nvc0_graph_fuc fuc41ad;
	bool firmware;

	struct nvc0_graph_zbc_color zbc_color[NOUVEAU_LTC_MAX_ZBC_CNT];
	struct nvc0_graph_zbc_depth zbc_depth[NOUVEAU_LTC_MAX_ZBC_CNT];

	u8 rop_nr;
	u8 gpc_nr;
	u8 tpc_nr[GPC_MAX];
	u8 tpc_total;
	u8 ppc_nr[GPC_MAX];
	u8 ppc_tpc_nr[GPC_MAX][4];

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
void nvc0_graph_zbc_init(struct nvc0_graph_priv *);

int  nve4_graph_fini(struct nouveau_object *, bool);
int  nve4_graph_init(struct nouveau_object *);

int  nvf0_graph_fini(struct nouveau_object *, bool);

extern struct nouveau_ofuncs nvc0_fermi_ofuncs;

extern struct nouveau_oclass nvc0_graph_sclass[];
extern struct nouveau_omthds nvc0_graph_9097_omthds[];
extern struct nouveau_omthds nvc0_graph_90c0_omthds[];
extern struct nouveau_oclass nvc8_graph_sclass[];
extern struct nouveau_oclass nvf0_graph_sclass[];

struct nvc0_graph_init {
	u32 addr;
	u8  count;
	u8  pitch;
	u32 data;
};

struct nvc0_graph_pack {
	const struct nvc0_graph_init *init;
	u32 type;
};

#define pack_for_each_init(init, pack, head)                                   \
	for (pack = head; pack && pack->init; pack++)                          \
		  for (init = pack->init; init && init->count; init++)

struct nvc0_graph_ucode {
	struct nvc0_graph_fuc code;
	struct nvc0_graph_fuc data;
};

extern struct nvc0_graph_ucode nvc0_graph_fecs_ucode;
extern struct nvc0_graph_ucode nvc0_graph_gpccs_ucode;

extern struct nvc0_graph_ucode nvf0_graph_fecs_ucode;
extern struct nvc0_graph_ucode nvf0_graph_gpccs_ucode;

struct nvc0_graph_oclass {
	struct nouveau_oclass base;
	struct nouveau_oclass **cclass;
	struct nouveau_oclass *sclass;
	const struct nvc0_graph_pack *mmio;
	struct {
		struct nvc0_graph_ucode *ucode;
	} fecs;
	struct {
		struct nvc0_graph_ucode *ucode;
	} gpccs;
	int ppc_nr;
};

void nvc0_graph_mmio(struct nvc0_graph_priv *, const struct nvc0_graph_pack *);
void nvc0_graph_icmd(struct nvc0_graph_priv *, const struct nvc0_graph_pack *);
void nvc0_graph_mthd(struct nvc0_graph_priv *, const struct nvc0_graph_pack *);
int  nvc0_graph_init_ctxctl(struct nvc0_graph_priv *);

/* register init value lists */

extern const struct nvc0_graph_init nvc0_graph_init_main_0[];
extern const struct nvc0_graph_init nvc0_graph_init_fe_0[];
extern const struct nvc0_graph_init nvc0_graph_init_pri_0[];
extern const struct nvc0_graph_init nvc0_graph_init_rstr2d_0[];
extern const struct nvc0_graph_init nvc0_graph_init_pd_0[];
extern const struct nvc0_graph_init nvc0_graph_init_ds_0[];
extern const struct nvc0_graph_init nvc0_graph_init_scc_0[];
extern const struct nvc0_graph_init nvc0_graph_init_prop_0[];
extern const struct nvc0_graph_init nvc0_graph_init_gpc_unk_0[];
extern const struct nvc0_graph_init nvc0_graph_init_setup_0[];
extern const struct nvc0_graph_init nvc0_graph_init_crstr_0[];
extern const struct nvc0_graph_init nvc0_graph_init_setup_1[];
extern const struct nvc0_graph_init nvc0_graph_init_zcull_0[];
extern const struct nvc0_graph_init nvc0_graph_init_gpm_0[];
extern const struct nvc0_graph_init nvc0_graph_init_gpc_unk_1[];
extern const struct nvc0_graph_init nvc0_graph_init_gcc_0[];
extern const struct nvc0_graph_init nvc0_graph_init_tpccs_0[];
extern const struct nvc0_graph_init nvc0_graph_init_tex_0[];
extern const struct nvc0_graph_init nvc0_graph_init_pe_0[];
extern const struct nvc0_graph_init nvc0_graph_init_l1c_0[];
extern const struct nvc0_graph_init nvc0_graph_init_wwdx_0[];
extern const struct nvc0_graph_init nvc0_graph_init_tpccs_1[];
extern const struct nvc0_graph_init nvc0_graph_init_mpc_0[];
extern const struct nvc0_graph_init nvc0_graph_init_be_0[];
extern const struct nvc0_graph_init nvc0_graph_init_fe_1[];
extern const struct nvc0_graph_init nvc0_graph_init_pe_1[];

extern const struct nvc0_graph_init nvc4_graph_init_ds_0[];
extern const struct nvc0_graph_init nvc4_graph_init_tex_0[];
extern const struct nvc0_graph_init nvc4_graph_init_sm_0[];

extern const struct nvc0_graph_init nvc1_graph_init_gpc_unk_0[];
extern const struct nvc0_graph_init nvc1_graph_init_setup_1[];

extern const struct nvc0_graph_init nvd9_graph_init_pd_0[];
extern const struct nvc0_graph_init nvd9_graph_init_ds_0[];
extern const struct nvc0_graph_init nvd9_graph_init_prop_0[];
extern const struct nvc0_graph_init nvd9_graph_init_gpm_0[];
extern const struct nvc0_graph_init nvd9_graph_init_gpc_unk_1[];
extern const struct nvc0_graph_init nvd9_graph_init_tex_0[];
extern const struct nvc0_graph_init nvd9_graph_init_sm_0[];
extern const struct nvc0_graph_init nvd9_graph_init_fe_1[];

extern const struct nvc0_graph_init nvd7_graph_init_pes_0[];
extern const struct nvc0_graph_init nvd7_graph_init_wwdx_0[];
extern const struct nvc0_graph_init nvd7_graph_init_cbm_0[];

extern const struct nvc0_graph_init nve4_graph_init_main_0[];
extern const struct nvc0_graph_init nve4_graph_init_tpccs_0[];
extern const struct nvc0_graph_init nve4_graph_init_pe_0[];
extern const struct nvc0_graph_init nve4_graph_init_be_0[];
extern const struct nvc0_graph_pack nve4_graph_pack_mmio[];

extern const struct nvc0_graph_init nvf0_graph_init_fe_0[];
extern const struct nvc0_graph_init nvf0_graph_init_ds_0[];
extern const struct nvc0_graph_init nvf0_graph_init_sked_0[];
extern const struct nvc0_graph_init nvf0_graph_init_cwd_0[];
extern const struct nvc0_graph_init nvf0_graph_init_gpc_unk_1[];
extern const struct nvc0_graph_init nvf0_graph_init_tex_0[];
extern const struct nvc0_graph_init nvf0_graph_init_sm_0[];

extern const struct nvc0_graph_init nv108_graph_init_gpc_unk_0[];


#endif
