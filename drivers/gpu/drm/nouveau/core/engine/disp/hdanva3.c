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
nva3_hda_eld(struct nv50_disp_priv *priv, int or, u8 *data, u32 size)
{
	const u32 soff = (or * 0x800);
	int i;

	if (data && data[0]) {
		for (i = 0; i < size; i++)
			nv_wr32(priv, 0x61c440 + soff, (i << 8) | data[i]);
		for (; i < 0x60; i++)
			nv_wr32(priv, 0x61c440 + soff, (i << 8));
		nv_mask(priv, 0x61c448 + soff, 0x80000003, 0x80000003);
	} else
	if (data) {
		nv_mask(priv, 0x61c448 + soff, 0x80000003, 0x80000001);
	} else {
		nv_mask(priv, 0x61c448 + soff, 0x80000003, 0x80000000);
	}

	return 0;
}
