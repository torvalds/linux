/*
 * Copyright 2013 Red Hat Inc.
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
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */

#include "nvc0.h"

/*******************************************************************************
 * PGRAPH engine/subdev functions
 ******************************************************************************/

struct nvc0_graph_init
nvc3_graph_init_unk58xx[] = {
	{ 0x405844,   1, 0x04, 0x00ffffff },
	{ 0x405850,   1, 0x04, 0x00000000 },
	{ 0x405900,   1, 0x04, 0x00002834 },
	{ 0x405908,   1, 0x04, 0x00000000 },
	{}
};

static struct nvc0_graph_init
nvc3_graph_init_tpc[] = {
	{ 0x419d08,   2, 0x04, 0x00000000 },
	{ 0x419d10,   1, 0x04, 0x00000014 },
	{ 0x419ab0,   1, 0x04, 0x00000000 },
	{ 0x419ac8,   1, 0x04, 0x00000000 },
	{ 0x419ab8,   1, 0x04, 0x000000e7 },
	{ 0x419abc,   2, 0x04, 0x00000000 },
	{ 0x41980c,   3, 0x04, 0x00000000 },
	{ 0x419844,   1, 0x04, 0x00000000 },
	{ 0x41984c,   1, 0x04, 0x00005bc5 },
	{ 0x419850,   4, 0x04, 0x00000000 },
	{ 0x419880,   1, 0x04, 0x00000002 },
	{ 0x419c98,   1, 0x04, 0x00000000 },
	{ 0x419ca8,   1, 0x04, 0x80000000 },
	{ 0x419cb4,   1, 0x04, 0x00000000 },
	{ 0x419cb8,   1, 0x04, 0x00008bf4 },
	{ 0x419cbc,   1, 0x04, 0x28137606 },
	{ 0x419cc0,   2, 0x04, 0x00000000 },
	{ 0x419bd4,   1, 0x04, 0x00800000 },
	{ 0x419bdc,   1, 0x04, 0x00000000 },
	{ 0x419d2c,   1, 0x04, 0x00000000 },
	{ 0x419c0c,   1, 0x04, 0x00000000 },
	{ 0x419e00,   1, 0x04, 0x00000000 },
	{ 0x419ea0,   1, 0x04, 0x00000000 },
	{ 0x419ea4,   1, 0x04, 0x00000100 },
	{ 0x419ea8,   1, 0x04, 0x00001100 },
	{ 0x419eac,   1, 0x04, 0x11100702 },
	{ 0x419eb0,   1, 0x04, 0x00000003 },
	{ 0x419eb4,   4, 0x04, 0x00000000 },
	{ 0x419ec8,   1, 0x04, 0x0e063818 },
	{ 0x419ecc,   1, 0x04, 0x0e060e06 },
	{ 0x419ed0,   1, 0x04, 0x00003818 },
	{ 0x419ed4,   1, 0x04, 0x011104f1 },
	{ 0x419edc,   1, 0x04, 0x00000000 },
	{ 0x419f00,   1, 0x04, 0x00000000 },
	{ 0x419f2c,   1, 0x04, 0x00000000 },
	{}
};

static struct nvc0_graph_init *
nvc3_graph_init_mmio[] = {
	nvc0_graph_init_regs,
	nvc0_graph_init_unk40xx,
	nvc0_graph_init_unk44xx,
	nvc0_graph_init_unk78xx,
	nvc0_graph_init_unk60xx,
	nvc3_graph_init_unk58xx,
	nvc0_graph_init_unk80xx,
	nvc0_graph_init_gpc,
	nvc3_graph_init_tpc,
	nvc0_graph_init_unk88xx,
	nvc0_graph_tpc_0,
	NULL
};

struct nouveau_oclass *
nvc3_graph_oclass = &(struct nvc0_graph_oclass) {
	.base.handle = NV_ENGINE(GR, 0xc3),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_graph_ctor,
		.dtor = nvc0_graph_dtor,
		.init = nvc0_graph_init,
		.fini = _nouveau_graph_fini,
	},
	.cclass = &nvc3_grctx_oclass,
	.sclass = nvc0_graph_sclass,
	.mmio = nvc3_graph_init_mmio,
	.fecs.ucode = &nvc0_graph_fecs_ucode,
	.gpccs.ucode = &nvc0_graph_gpccs_ucode,
}.base;
