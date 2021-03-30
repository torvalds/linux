/*
 * Copyright 2021 Red Hat Inc.
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
 */
#include "nv50.h"
#include "head.h"
#include "ior.h"
#include "channv50.h"
#include "rootnv50.h"

static const struct nv50_disp_func
ga102_disp = {
	.init = tu102_disp_init,
	.fini = gv100_disp_fini,
	.intr = gv100_disp_intr,
	.uevent = &gv100_disp_chan_uevent,
	.super = gv100_disp_super,
	.root = &ga102_disp_root_oclass,
	.wndw = { .cnt = gv100_disp_wndw_cnt },
	.head = { .cnt = gv100_head_cnt, .new = gv100_head_new },
	.sor = { .cnt = gv100_sor_cnt, .new = ga102_sor_new },
	.ramht_size = 0x2000,
};

int
ga102_disp_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_disp **pdisp)
{
	return nv50_disp_new_(&ga102_disp, device, type, inst, pdisp);
}
