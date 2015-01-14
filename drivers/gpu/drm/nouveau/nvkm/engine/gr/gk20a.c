/*
 * Copyright (c) 2014, NVIDIA CORPORATION. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "nvc0.h"
#include "ctxnvc0.h"

static struct nouveau_oclass
gk20a_gr_sclass[] = {
	{ 0x902d, &nouveau_object_ofuncs },
	{ 0xa040, &nouveau_object_ofuncs },
	{ KEPLER_C, &nvc0_fermi_ofuncs, nvc0_gr_9097_omthds },
	{ KEPLER_COMPUTE_A, &nouveau_object_ofuncs, nvc0_gr_90c0_omthds },
	{}
};

struct nouveau_oclass *
gk20a_gr_oclass = &(struct nvc0_gr_oclass) {
	.base.handle = NV_ENGINE(GR, 0xea),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_gr_ctor,
		.dtor = nvc0_gr_dtor,
		.init = nve4_gr_init,
		.fini = _nouveau_gr_fini,
	},
	.cclass = &gk20a_grctx_oclass,
	.sclass = gk20a_gr_sclass,
	.mmio = nve4_gr_pack_mmio,
	.ppc_nr = 1,
}.base;
