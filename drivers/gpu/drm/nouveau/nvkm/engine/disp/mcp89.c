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
 */
#include "priv.h"
#include "chan.h"
#include "head.h"
#include "ior.h"

#include <nvif/class.h>

static const struct nvkm_ior_func_dp
mcp89_sor_dp = {
	.lanes = { 3, 2, 1, 0 },
	.links = g94_sor_dp_links,
	.power = g94_sor_dp_power,
	.pattern = g94_sor_dp_pattern,
	.drive = g94_sor_dp_drive,
	.audio = gt215_sor_dp_audio,
	.audio_sym = g94_sor_dp_audio_sym,
	.activesym = g94_sor_dp_activesym,
	.watermark = g94_sor_dp_watermark,
};

static const struct nvkm_ior_func
mcp89_sor = {
	.state = g94_sor_state,
	.power = nv50_sor_power,
	.clock = nv50_sor_clock,
	.bl = &gt215_sor_bl,
	.hdmi = &gt215_sor_hdmi,
	.dp = &mcp89_sor_dp,
	.hda = &gt215_sor_hda,
};

static int
mcp89_sor_new(struct nvkm_disp *disp, int id)
{
	return nvkm_ior_new_(&mcp89_sor, disp, SOR, id, true);
}

static const struct nvkm_disp_func
mcp89_disp = {
	.oneinit = nv50_disp_oneinit,
	.init = nv50_disp_init,
	.fini = nv50_disp_fini,
	.intr = nv50_disp_intr,
	.super = nv50_disp_super,
	.uevent = &nv50_disp_chan_uevent,
	.head = { .cnt = nv50_head_cnt, .new = nv50_head_new },
	.dac = { .cnt = nv50_dac_cnt, .new = nv50_dac_new },
	.sor = { .cnt = g94_sor_cnt, .new = mcp89_sor_new },
	.pior = { .cnt = nv50_pior_cnt, .new = nv50_pior_new },
	.root = { 0,0,GT214_DISP },
	.user = {
		{{0,0,GT214_DISP_CURSOR             }, nvkm_disp_chan_new, &nv50_disp_curs },
		{{0,0,GT214_DISP_OVERLAY            }, nvkm_disp_chan_new, &nv50_disp_oimm },
		{{0,0,GT214_DISP_BASE_CHANNEL_DMA   }, nvkm_disp_chan_new, & g84_disp_base },
		{{0,0,GT214_DISP_CORE_CHANNEL_DMA   }, nvkm_disp_core_new, & g94_disp_core },
		{{0,0,GT214_DISP_OVERLAY_CHANNEL_DMA}, nvkm_disp_chan_new, & g84_disp_ovly },
		{}
	},
};

int
mcp89_disp_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_disp **pdisp)
{
	return nvkm_disp_new_(&mcp89_disp, device, type, inst, pdisp);
}
