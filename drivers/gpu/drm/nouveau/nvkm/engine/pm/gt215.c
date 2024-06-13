/*
 * Copyright 2013 Red Hat Inc.
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
#include "nv40.h"

static const struct nvkm_specsrc
gt215_zcull_sources[] = {
	{ 0x402ca4, (const struct nvkm_specmux[]) {
			{ 0x7fff, 0, "unk0" },
			{ 0xff, 24, "unk24" },
			{}
		}, "pgraph_zcull_pm_unka4" },
	{}
};

static const struct nvkm_specdom
gt215_pm[] = {
	{ 0x20, (const struct nvkm_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0xf0, (const struct nvkm_specsig[]) {
			{ 0xcb, "pc01_gr_idle" },
			{ 0x86, "pc01_strmout_00" },
			{ 0x87, "pc01_strmout_01" },
			{ 0xe0, "pc01_trast_00" },
			{ 0xe1, "pc01_trast_01" },
			{ 0xe2, "pc01_trast_02" },
			{ 0xe3, "pc01_trast_03" },
			{ 0xe6, "pc01_trast_04" },
			{ 0xe7, "pc01_trast_05" },
			{ 0x84, "pc01_vattr_00" },
			{ 0x85, "pc01_vattr_01" },
			{ 0x46, "pc01_vfetch_00", g84_vfetch_sources },
			{ 0x47, "pc01_vfetch_01", g84_vfetch_sources },
			{ 0x48, "pc01_vfetch_02", g84_vfetch_sources },
			{ 0x49, "pc01_vfetch_03", g84_vfetch_sources },
			{ 0x4a, "pc01_vfetch_04", g84_vfetch_sources },
			{ 0x4b, "pc01_vfetch_05", g84_vfetch_sources },
			{ 0x4c, "pc01_vfetch_06", g84_vfetch_sources },
			{ 0x4d, "pc01_vfetch_07", g84_vfetch_sources },
			{ 0x4e, "pc01_vfetch_08", g84_vfetch_sources },
			{ 0x4f, "pc01_vfetch_09", g84_vfetch_sources },
			{ 0x50, "pc01_vfetch_0a", g84_vfetch_sources },
			{ 0x51, "pc01_vfetch_0b", g84_vfetch_sources },
			{ 0x52, "pc01_vfetch_0c", g84_vfetch_sources },
			{ 0x53, "pc01_vfetch_0d", g84_vfetch_sources },
			{ 0x54, "pc01_vfetch_0e", g84_vfetch_sources },
			{ 0x55, "pc01_vfetch_0f", g84_vfetch_sources },
			{ 0x56, "pc01_vfetch_10", g84_vfetch_sources },
			{ 0x57, "pc01_vfetch_11", g84_vfetch_sources },
			{ 0x58, "pc01_vfetch_12", g84_vfetch_sources },
			{ 0x59, "pc01_vfetch_13", g84_vfetch_sources },
			{ 0x5a, "pc01_vfetch_14", g84_vfetch_sources },
			{ 0x5b, "pc01_vfetch_15", g84_vfetch_sources },
			{ 0x5c, "pc01_vfetch_16", g84_vfetch_sources },
			{ 0x5d, "pc01_vfetch_17", g84_vfetch_sources },
			{ 0x5e, "pc01_vfetch_18", g84_vfetch_sources },
			{ 0x5f, "pc01_vfetch_19", g84_vfetch_sources },
			{ 0x07, "pc01_zcull_00", gt215_zcull_sources },
			{ 0x08, "pc01_zcull_01", gt215_zcull_sources },
			{ 0x09, "pc01_zcull_02", gt215_zcull_sources },
			{ 0x0a, "pc01_zcull_03", gt215_zcull_sources },
			{ 0x0b, "pc01_zcull_04", gt215_zcull_sources },
			{ 0x0c, "pc01_zcull_05", gt215_zcull_sources },
			{ 0xb2, "pc01_unk00" },
			{ 0xec, "pc01_trailer" },
			{}
		}, &nv40_perfctr_func },
	{ 0xe0, (const struct nvkm_specsig[]) {
			{ 0x64, "pc02_crop_00", gt200_crop_sources },
			{ 0x65, "pc02_crop_01", gt200_crop_sources },
			{ 0x66, "pc02_crop_02", gt200_crop_sources },
			{ 0x67, "pc02_crop_03", gt200_crop_sources },
			{ 0x00, "pc02_prop_00", gt200_prop_sources },
			{ 0x01, "pc02_prop_01", gt200_prop_sources },
			{ 0x02, "pc02_prop_02", gt200_prop_sources },
			{ 0x03, "pc02_prop_03", gt200_prop_sources },
			{ 0x04, "pc02_prop_04", gt200_prop_sources },
			{ 0x05, "pc02_prop_05", gt200_prop_sources },
			{ 0x06, "pc02_prop_06", gt200_prop_sources },
			{ 0x07, "pc02_prop_07", gt200_prop_sources },
			{ 0x80, "pc02_tex_00", gt200_tex_sources },
			{ 0x81, "pc02_tex_01", gt200_tex_sources },
			{ 0x82, "pc02_tex_02", gt200_tex_sources },
			{ 0x83, "pc02_tex_03", gt200_tex_sources },
			{ 0x3a, "pc02_tex_04", gt200_tex_sources },
			{ 0x3b, "pc02_tex_05", gt200_tex_sources },
			{ 0x3c, "pc02_tex_06", gt200_tex_sources },
			{ 0x7c, "pc02_zrop_00", nv50_zrop_sources },
			{ 0x7d, "pc02_zrop_01", nv50_zrop_sources },
			{ 0x7e, "pc02_zrop_02", nv50_zrop_sources },
			{ 0x7f, "pc02_zrop_03", nv50_zrop_sources },
			{ 0xcc, "pc02_trailer" },
			{}
		}, &nv40_perfctr_func },
	{ 0x20, (const struct nvkm_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0x20, (const struct nvkm_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0x20, (const struct nvkm_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0x20, (const struct nvkm_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0x20, (const struct nvkm_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{}
};

int
gt215_pm_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_pm **ppm)
{
	return nv40_pm_new_(gt215_pm, device, type, inst, ppm);
}
