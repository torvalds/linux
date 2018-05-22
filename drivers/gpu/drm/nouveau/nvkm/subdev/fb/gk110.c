/*
 * Copyright 2017 Red Hat Inc.
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
 * Authors: Lyude Paul
 */
#include "gf100.h"
#include "gk104.h"
#include "ram.h"
#include <subdev/therm.h>
#include <subdev/fb.h>

/*
 *******************************************************************************
 * PGRAPH registers for clockgating
 *******************************************************************************
 */

static const struct nvkm_therm_clkgate_init
gk110_fb_clkgate_blcg_init_unk_0[] = {
	{ 0x100d10, 1, 0x0000c242 },
	{ 0x100d30, 1, 0x0000c242 },
	{ 0x100d3c, 1, 0x00000242 },
	{ 0x100d48, 1, 0x0000c242 },
	{ 0x100d1c, 1, 0x00000042 },
	{}
};

static const struct nvkm_therm_clkgate_pack
gk110_fb_clkgate_pack[] = {
	{ gk110_fb_clkgate_blcg_init_unk_0 },
	{ gk104_fb_clkgate_blcg_init_vm_0 },
	{ gk104_fb_clkgate_blcg_init_main_0 },
	{ gk104_fb_clkgate_blcg_init_bcast_0 },
	{}
};

static const struct nvkm_fb_func
gk110_fb = {
	.dtor = gf100_fb_dtor,
	.oneinit = gf100_fb_oneinit,
	.init = gf100_fb_init,
	.init_page = gf100_fb_init_page,
	.intr = gf100_fb_intr,
	.ram_new = gk104_ram_new,
	.default_bigpage = 17,
	.clkgate_pack = gk110_fb_clkgate_pack,
};

int
gk110_fb_new(struct nvkm_device *device, int index, struct nvkm_fb **pfb)
{
	return gf100_fb_new_(&gk110_fb, device, index, pfb);
}
