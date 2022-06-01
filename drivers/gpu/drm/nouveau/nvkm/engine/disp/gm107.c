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
#include "priv.h"
#include "head.h"
#include "ior.h"
#include "channv50.h"

#include <nvif/class.h>

static const struct nvkm_disp_func
gm107_disp = {
	.dtor = nv50_disp_dtor_,
	.oneinit = nv50_disp_oneinit_,
	.init = gf119_disp_init,
	.fini = gf119_disp_fini,
	.intr = gf119_disp_intr,
	.intr_error = gf119_disp_intr_error,
	.super = gf119_disp_super,
	.uevent = &gf119_disp_chan_uevent,
	.head = { .cnt = gf119_head_cnt, .new = gf119_head_new },
	.dac = { .cnt = gf119_dac_cnt, .new = gf119_dac_new },
	.sor = { .cnt = gf119_sor_cnt, .new = gm107_sor_new },
	.root = { 0,0,GM107_DISP },
	.user = {
		{{0,0,GK104_DISP_CURSOR             }, gf119_disp_curs_new },
		{{0,0,GK104_DISP_OVERLAY            }, gf119_disp_oimm_new },
		{{0,0,GK110_DISP_BASE_CHANNEL_DMA   }, gf119_disp_base_new },
		{{0,0,GM107_DISP_CORE_CHANNEL_DMA   }, gk104_disp_core_new },
		{{0,0,GK104_DISP_OVERLAY_CONTROL_DMA}, gk104_disp_ovly_new },
		{}
	},
};

int
gm107_disp_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_disp **pdisp)
{
	return nv50_disp_new_(&gm107_disp, device, type, inst, pdisp);
}
