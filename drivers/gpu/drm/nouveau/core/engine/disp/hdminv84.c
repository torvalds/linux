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

#include <core/os.h>
#include <core/class.h>

#include "nv50.h"

int
nv84_hdmi_ctrl(struct nv50_disp_priv *priv, int head, int or, u32 data)
{
	const u32 hoff = (head * 0x800);

	if (!(data & NV84_DISP_SOR_HDMI_PWR_STATE_ON)) {
		nv_mask(priv, 0x6165a4 + hoff, 0x40000000, 0x00000000);
		nv_mask(priv, 0x616520 + hoff, 0x00000001, 0x00000000);
		nv_mask(priv, 0x616500 + hoff, 0x00000001, 0x00000000);
		return 0;
	}

	/* AVI InfoFrame */
	nv_mask(priv, 0x616520 + hoff, 0x00000001, 0x00000000);
	nv_wr32(priv, 0x616528 + hoff, 0x000d0282);
	nv_wr32(priv, 0x61652c + hoff, 0x0000006f);
	nv_wr32(priv, 0x616530 + hoff, 0x00000000);
	nv_wr32(priv, 0x616534 + hoff, 0x00000000);
	nv_wr32(priv, 0x616538 + hoff, 0x00000000);
	nv_mask(priv, 0x616520 + hoff, 0x00000001, 0x00000001);

	/* Audio InfoFrame */
	nv_mask(priv, 0x616500 + hoff, 0x00000001, 0x00000000);
	nv_wr32(priv, 0x616508 + hoff, 0x000a0184);
	nv_wr32(priv, 0x61650c + hoff, 0x00000071);
	nv_wr32(priv, 0x616510 + hoff, 0x00000000);
	nv_mask(priv, 0x616500 + hoff, 0x00000001, 0x00000001);

	nv_mask(priv, 0x6165d0 + hoff, 0x00070001, 0x00010001); /* SPARE, HW_CTS */
	nv_mask(priv, 0x616568 + hoff, 0x00010101, 0x00000000); /* ACR_CTRL, ?? */
	nv_mask(priv, 0x616578 + hoff, 0x80000000, 0x80000000); /* ACR_0441_ENABLE */

	/* ??? */
	nv_mask(priv, 0x61733c, 0x00100000, 0x00100000); /* RESETF */
	nv_mask(priv, 0x61733c, 0x10000000, 0x10000000); /* LOOKUP_EN */
	nv_mask(priv, 0x61733c, 0x00100000, 0x00000000); /* !RESETF */

	/* HDMI_CTRL */
	nv_mask(priv, 0x6165a4 + hoff, 0x5f1f007f, data | 0x1f000000 /* ??? */);
	return 0;
}
