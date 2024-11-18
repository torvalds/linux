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
#include "core.h"

#include <nvif/push507c.h>

#include <nvhw/class/cl507d.h>
#include <nvhw/class/cl837d.h>

static int
sor507d_ctrl(struct nv50_core *core, int or, u32 ctrl,
	     struct nv50_head_atom *asyh)
{
	struct nvif_push *push = &core->chan.push;
	int ret;

	if (asyh) {
		ctrl |= NVVAL(NV507D, SOR_SET_CONTROL, HSYNC_POLARITY, asyh->or.nhsync);
		ctrl |= NVVAL(NV507D, SOR_SET_CONTROL, VSYNC_POLARITY, asyh->or.nvsync);
		ctrl |= NVVAL(NV837D, SOR_SET_CONTROL, PIXEL_DEPTH, asyh->or.depth);
	}

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_MTHD(push, NV507D, SOR_SET_CONTROL(or), ctrl);
	return 0;
}

static void
sor507d_get_caps(struct nv50_disp *core, struct nouveau_encoder *outp, int or)
{
	outp->caps.dp_interlace = true;
}

const struct nv50_outp_func
sor507d = {
	.ctrl = sor507d_ctrl,
	.get_caps = sor507d_get_caps,
};
