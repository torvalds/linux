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
gm204_disp_sclass[] = {
	{ GM204_DISP_CORE_CHANNEL_DMA, &gf119_disp_core_ofuncs.base },
	{ GK110_DISP_BASE_CHANNEL_DMA, &gf119_disp_base_ofuncs.base },
	{ GK104_DISP_OVERLAY_CONTROL_DMA, &gf119_disp_ovly_ofuncs.base },
	{ GK104_DISP_OVERLAY, &gf119_disp_oimm_ofuncs.base },
	{ GK104_DISP_CURSOR, &gf119_disp_curs_ofuncs.base },
	{}
};

struct nvkm_oclass
gm204_disp_root_oclass[] = {
	{ GM204_DISP, &gf119_disp_root_ofuncs },
	{}
};
