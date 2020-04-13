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
#include "curs.h"
#include "atom.h"

static void
cursc37a_update(struct nv50_wndw *wndw, u32 *interlock)
{
	if (curs507a_space(wndw))
		nvif_wr32(&wndw->wimm.base.user, 0x0200, 0x00000001);
}

static void
cursc37a_point(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	if (curs507a_space(wndw)) {
		nvif_wr32(&wndw->wimm.base.user, 0x0208, asyw->point.y << 16 |
							 asyw->point.x);
	}
}

static const struct nv50_wimm_func
cursc37a = {
	.point = cursc37a_point,
	.update = cursc37a_update,
};

int
cursc37a_new(struct nouveau_drm *drm, int head, s32 oclass,
	     struct nv50_wndw **pwndw)
{
	return curs507a_new_(&cursc37a, drm, head, oclass,
			     0x00000001 << head, pwndw);
}
