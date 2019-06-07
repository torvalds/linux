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
#include "rootnv50.h"
#include "channv50.h"

#include <nvif/class.h>

static const struct nv50_disp_root_func
g94_disp_root = {
	.user = {
		{{0,0,  G82_DISP_CURSOR             },  nv50_disp_curs_new },
		{{0,0,  G82_DISP_OVERLAY            },  nv50_disp_oimm_new },
		{{0,0,GT200_DISP_BASE_CHANNEL_DMA   },   g84_disp_base_new },
		{{0,0,GT206_DISP_CORE_CHANNEL_DMA   },   g94_disp_core_new },
		{{0,0,GT200_DISP_OVERLAY_CHANNEL_DMA}, gt200_disp_ovly_new },
		{}
	},
};

static int
g94_disp_root_new(struct nvkm_disp *disp, const struct nvkm_oclass *oclass,
		  void *data, u32 size, struct nvkm_object **pobject)
{
	return nv50_disp_root_new_(&g94_disp_root, disp, oclass,
				   data, size, pobject);
}

const struct nvkm_disp_oclass
g94_disp_root_oclass = {
	.base.oclass = GT206_DISP,
	.base.minver = -1,
	.base.maxver = -1,
	.ctor = g94_disp_root_new,
};
