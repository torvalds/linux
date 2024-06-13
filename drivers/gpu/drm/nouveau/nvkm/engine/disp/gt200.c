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
#include "priv.h"
#include "chan.h"
#include "head.h"
#include "ior.h"

#include <nvif/class.h>

static const struct nvkm_disp_mthd_list
gt200_disp_ovly_mthd_base = {
	.mthd = 0x0000,
	.addr = 0x000000,
	.data = {
		{ 0x0080, 0x000000 },
		{ 0x0084, 0x6109a0 },
		{ 0x0088, 0x6109c0 },
		{ 0x008c, 0x6109c8 },
		{ 0x0090, 0x6109b4 },
		{ 0x0094, 0x610970 },
		{ 0x00a0, 0x610998 },
		{ 0x00a4, 0x610964 },
		{ 0x00b0, 0x610c98 },
		{ 0x00b4, 0x610ca4 },
		{ 0x00b8, 0x610cac },
		{ 0x00c0, 0x610958 },
		{ 0x00e0, 0x6109a8 },
		{ 0x00e4, 0x6109d0 },
		{ 0x00e8, 0x6109d8 },
		{ 0x0100, 0x61094c },
		{ 0x0104, 0x610984 },
		{ 0x0108, 0x61098c },
		{ 0x0800, 0x6109f8 },
		{ 0x0808, 0x610a08 },
		{ 0x080c, 0x610a10 },
		{ 0x0810, 0x610a00 },
		{}
	}
};

static const struct nvkm_disp_chan_mthd
gt200_disp_ovly_mthd = {
	.name = "Overlay",
	.addr = 0x000540,
	.prev = 0x000004,
	.data = {
		{ "Global", 1, &gt200_disp_ovly_mthd_base },
		{}
	}
};

const struct nvkm_disp_chan_user
gt200_disp_ovly = {
	.func = &nv50_disp_dmac_func,
	.ctrl = 3,
	.user = 3,
	.mthd = &gt200_disp_ovly_mthd,
};

static const struct nvkm_disp_func
gt200_disp = {
	.oneinit = nv50_disp_oneinit,
	.init = nv50_disp_init,
	.fini = nv50_disp_fini,
	.intr = nv50_disp_intr,
	.super = nv50_disp_super,
	.uevent = &nv50_disp_chan_uevent,
	.head = { .cnt = nv50_head_cnt, .new = nv50_head_new },
	.dac = { .cnt = nv50_dac_cnt, .new = nv50_dac_new },
	.sor = { .cnt = nv50_sor_cnt, .new = g84_sor_new },
	.pior = { .cnt = nv50_pior_cnt, .new = nv50_pior_new },
	.root = { 0,0,GT200_DISP },
	.user = {
		{{0,0,  G82_DISP_CURSOR             }, nvkm_disp_chan_new, & nv50_disp_curs },
		{{0,0,  G82_DISP_OVERLAY            }, nvkm_disp_chan_new, & nv50_disp_oimm },
		{{0,0,GT200_DISP_BASE_CHANNEL_DMA   }, nvkm_disp_chan_new, &  g84_disp_base },
		{{0,0,GT200_DISP_CORE_CHANNEL_DMA   }, nvkm_disp_core_new, &  g84_disp_core },
		{{0,0,GT200_DISP_OVERLAY_CHANNEL_DMA}, nvkm_disp_chan_new, &gt200_disp_ovly },
		{}
	},
};

int
gt200_disp_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_disp **pdisp)
{
	return nvkm_disp_new_(&gt200_disp, device, type, inst, pdisp);
}
