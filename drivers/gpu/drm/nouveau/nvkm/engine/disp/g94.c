/*
 * Copyright 2012 Red Hat Inc.
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
#include "nv50.h"
#include "head.h"
#include "ior.h"
#include "rootnv50.h"

static const struct nv50_disp_func
g94_disp = {
	.init = nv50_disp_init,
	.fini = nv50_disp_fini,
	.intr = nv50_disp_intr,
	.uevent = &nv50_disp_chan_uevent,
	.super = nv50_disp_super,
	.root = &g94_disp_root_oclass,
	.head = { .cnt = nv50_head_cnt, .new = nv50_head_new },
	.dac = { .cnt = nv50_dac_cnt, .new = nv50_dac_new },
	.sor = { .cnt = g94_sor_cnt, .new = g94_sor_new },
	.pior = { .cnt = nv50_pior_cnt, .new = nv50_pior_new },
};

int
g94_disp_new(struct nvkm_device *device, int index, struct nvkm_disp **pdisp)
{
	return nv50_disp_new_(&g94_disp, device, index, pdisp);
}
