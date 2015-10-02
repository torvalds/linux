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
#include "dmacnv50.h"

#include <nvif/class.h>

static const struct nv50_disp_root_func
gm204_disp_root = {
	.init = gf119_disp_root_init,
	.fini = gf119_disp_root_fini,
	.dmac = {
		&gm204_disp_core_oclass,
		&gk110_disp_base_oclass,
		&gk104_disp_ovly_oclass,
	},
	.pioc = {
		&gk104_disp_oimm_oclass,
		&gk104_disp_curs_oclass,
	},
};

static int
gm204_disp_root_new(struct nvkm_disp *disp, const struct nvkm_oclass *oclass,
		    void *data, u32 size, struct nvkm_object **pobject)
{
	return nv50_disp_root_new_(&gm204_disp_root, disp, oclass,
				   data, size, pobject);
}

const struct nvkm_disp_oclass
gm204_disp_root_oclass = {
	.base.oclass = GM204_DISP,
	.base.minver = -1,
	.base.maxver = -1,
	.ctor = gm204_disp_root_new,
};
