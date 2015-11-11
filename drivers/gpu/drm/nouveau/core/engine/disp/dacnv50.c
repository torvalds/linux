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

#include <subdev/bios.h>
#include <subdev/bios/dcb.h>
#include <subdev/timer.h>

#include "nv50.h"

int
nv50_dac_power(struct nv50_disp_priv *priv, int or, u32 data)
{
	const u32 stat = (data & NV50_DISP_DAC_PWR_HSYNC) |
		         (data & NV50_DISP_DAC_PWR_VSYNC) |
		         (data & NV50_DISP_DAC_PWR_DATA) |
		         (data & NV50_DISP_DAC_PWR_STATE);
	const u32 doff = (or * 0x800);
	nv_wait(priv, 0x61a004 + doff, 0x80000000, 0x00000000);
	nv_mask(priv, 0x61a004 + doff, 0xc000007f, 0x80000000 | stat);
	nv_wait(priv, 0x61a004 + doff, 0x80000000, 0x00000000);
	return 0;
}

int
nv50_dac_sense(struct nv50_disp_priv *priv, int or, u32 loadval)
{
	const u32 doff = (or * 0x800);
	int load = -EINVAL;
	nv_mask(priv, 0x61a004 + doff, 0x807f0000, 0x80150000);
	nv_wait(priv, 0x61a004 + doff, 0x80000000, 0x00000000);
	nv_wr32(priv, 0x61a00c + doff, 0x00100000 | loadval);
	mdelay(9);
	udelay(500);
	nv_wr32(priv, 0x61a00c + doff, 0x80000000);
	load = (nv_rd32(priv, 0x61a00c + doff) & 0x38000000) >> 27;
	nv_wr32(priv, 0x61a00c + doff, 0x00000000);
	nv_mask(priv, 0x61a004 + doff, 0x807f0000, 0x80550000);
	nv_wait(priv, 0x61a004 + doff, 0x80000000, 0x00000000);
	return load;
}

int
nv50_dac_mthd(struct nouveau_object *object, u32 mthd, void *args, u32 size)
{
	struct nv50_disp_priv *priv = (void *)object->engine;
	const u8 or = (mthd & NV50_DISP_DAC_MTHD_OR);
	u32 *data = args;
	int ret;

	if (size < sizeof(u32))
		return -EINVAL;

	switch (mthd & ~0x3f) {
	case NV50_DISP_DAC_PWR:
		ret = priv->dac.power(priv, or, data[0]);
		break;
	case NV50_DISP_DAC_LOAD:
		ret = priv->dac.sense(priv, or, data[0]);
		if (ret >= 0) {
			data[0] = ret;
			ret = 0;
		}
		break;
	default:
		BUG_ON(1);
	}

	return ret;
}
