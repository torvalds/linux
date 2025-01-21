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

static int
dac507d_ctrl(struct nv50_core *core, int or, u32 ctrl,
	     struct nv50_head_atom *asyh)
{
	struct nvif_push *push = &core->chan.push;
	u32 sync = 0;
	int ret;

	if (asyh) {
		sync |= NVVAL(NV507D, DAC_SET_POLARITY, HSYNC, asyh->or.nhsync);
		sync |= NVVAL(NV507D, DAC_SET_POLARITY, VSYNC, asyh->or.nvsync);
	}

	if ((ret = PUSH_WAIT(push, 3)))
		return ret;

	PUSH_MTHD(push, NV507D, DAC_SET_CONTROL(or), ctrl,
				DAC_SET_POLARITY(or), sync);
	return 0;
}

const struct nv50_outp_func
dac507d = {
	.ctrl = dac507d_ctrl,
};
