/*
 * Copyright 2018 Red Hat Inc.
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
#include "oimm.h"

#include <nvif/class.h>

int
nv50_oimm_init(struct nouveau_drm *drm, struct nv50_wndw *wndw)
{
	static const struct {
		s32 oclass;
		int version;
		int (*init)(struct nouveau_drm *, s32, struct nv50_wndw *);
	} oimms[] = {
		{ GK104_DISP_OVERLAY, 0, oimm507b_init },
		{ GF110_DISP_OVERLAY, 0, oimm507b_init },
		{ GT214_DISP_OVERLAY, 0, oimm507b_init },
		{   G82_DISP_OVERLAY, 0, oimm507b_init },
		{  NV50_DISP_OVERLAY, 0, oimm507b_init },
		{}
	};
	struct nv50_disp *disp = nv50_disp(drm->dev);
	int cid;

	cid = nvif_mclass(&disp->disp->object, oimms);
	if (cid < 0) {
		NV_ERROR(drm, "No supported overlay immediate class\n");
		return cid;
	}

	return oimms[cid].init(drm, oimms[cid].oclass, wndw);
}
