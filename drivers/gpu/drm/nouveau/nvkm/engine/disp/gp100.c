/*
 * Copyright 2015 Red Hat Inc.
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
#include "nv50.h"
#include "head.h"
#include "ior.h"
#include "rootnv50.h"

static const struct nv50_disp_func
gp100_disp = {
	.init = gf119_disp_init,
	.fini = gf119_disp_fini,
	.intr = gf119_disp_intr,
	.intr_error = gf119_disp_intr_error,
	.uevent = &gf119_disp_chan_uevent,
	.super = gf119_disp_super,
	.root = &gp100_disp_root_oclass,
	.head = { .cnt = gf119_head_cnt, .new = gf119_head_new },
	.sor = { .cnt = gf119_sor_cnt, .new = gp100_sor_new },
};

int
gp100_disp_new(struct nvkm_device *device, int index, struct nvkm_disp **pdisp)
{
	return nv50_disp_new_(&gp100_disp, device, index, pdisp);
}
