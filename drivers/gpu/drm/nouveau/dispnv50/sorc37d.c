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

static void
sorc37d_ctrl(struct nv50_core *core, int or, u32 ctrl,
	     struct nv50_head_atom *asyh)
{
	u32 *push;
	if ((push = evo_wait(&core->chan, 2))) {
		evo_mthd(push, 0x0300 + (or * 0x20), 1);
		evo_data(push, ctrl);
		evo_kick(push, &core->chan);
	}
}

static void
sorc37d_get_caps(struct nv50_disp *disp, struct nouveau_encoder *outp, int or)
{
	u32 tmp = nvif_rd32(&disp->caps, 0x000144 + (or * 8));

	outp->caps.dp_interlace = !!(tmp & 0x04000000);
}

const struct nv50_outp_func
sorc37d = {
	.ctrl = sorc37d_ctrl,
	.get_caps = sorc37d_get_caps,
};
