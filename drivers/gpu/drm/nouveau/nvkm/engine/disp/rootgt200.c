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

struct nvkm_oclass
gt200_disp_sclass[] = {
	{ GT200_DISP_CORE_CHANNEL_DMA, &nv50_disp_core_ofuncs.base },
	{ GT200_DISP_BASE_CHANNEL_DMA, &nv50_disp_base_ofuncs.base },
	{ GT200_DISP_OVERLAY_CHANNEL_DMA, &nv50_disp_ovly_ofuncs.base },
	{ G82_DISP_OVERLAY, &nv50_disp_oimm_ofuncs.base },
	{ G82_DISP_CURSOR, &nv50_disp_curs_ofuncs.base },
	{}
};

struct nvkm_oclass
gt200_disp_root_oclass[] = {
	{ GT200_DISP, &nv50_disp_root_ofuncs },
	{}
};
