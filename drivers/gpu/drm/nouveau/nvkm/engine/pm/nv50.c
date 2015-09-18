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

const struct nvkm_specsrc
nv50_zcull_sources[] = {
	{ 0x402ca4, (const struct nvkm_specmux[]) {
			{ 0x7fff, 0, "unk0" },
			{}
		}, "pgraph_zcull_pm_unka4" },
	{}
};

const struct nvkm_specsrc
nv50_zrop_sources[] = {
	{ 0x40708c, (const struct nvkm_specmux[]) {
			{ 0xf, 0, "sel0", true },
			{ 0xf, 16, "sel1", true },
			{}
		}, "pgraph_rop0_zrop_pm_mux" },
	{}
};

static const struct nvkm_specsrc
nv50_prop_sources[] = {
	{ 0x40be50, (const struct nvkm_specmux[]) {
			{ 0x1f, 0, "sel", true },
			{}
		}, "pgraph_tpc3_prop_pm_mux" },
	{}
};

static const struct nvkm_specsrc
nv50_crop_sources[] = {
        { 0x407008, (const struct nvkm_specmux[]) {
                        { 0x7, 0, "sel0", true },
                        { 0x7, 16, "sel1", true },
                        {}
                }, "pgraph_rop0_crop_pm_mux" },
        {}
};

static const struct nvkm_specsrc
nv50_tex_sources[] = {
	{ 0x40b808, (const struct nvkm_specmux[]) {
			{ 0x3fff, 0, "unk0" },
			{}
		}, "pgraph_tpc3_tex_unk08" },
	{}
};

static const struct nvkm_specsrc
nv50_vfetch_sources[] = {
	{ 0x400c0c, (const struct nvkm_specmux[]) {
			{ 0x1, 0, "unk0" },
			{}
		}, "pgraph_vfetch_unk0c" },
	{}
};

static const struct nvkm_specdom
nv50_pm[] = {
	{ 0x20, (const struct nvkm_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0xf0, (const struct nvkm_specsig[]) {
			{ 0xc8, "pc01_gr_idle" },
			{ 0x7f, "pc01_strmout_00" },
			{ 0x80, "pc01_strmout_01" },
			{ 0xdc, "pc01_trast_00" },
			{ 0xdd, "pc01_trast_01" },
			{ 0xde, "pc01_trast_02" },
			{ 0xdf, "pc01_trast_03" },
			{ 0xe2, "pc01_trast_04" },
			{ 0xe3, "pc01_trast_05" },
			{ 0x7c, "pc01_vattr_00" },
			{ 0x7d, "pc01_vattr_01" },
			{ 0x26, "pc01_vfetch_00", nv50_vfetch_sources },
			{ 0x27, "pc01_vfetch_01", nv50_vfetch_sources },
			{ 0x28, "pc01_vfetch_02", nv50_vfetch_sources },
			{ 0x29, "pc01_vfetch_03", nv50_vfetch_sources },
			{ 0x2a, "pc01_vfetch_04", nv50_vfetch_sources },
			{ 0x2b, "pc01_vfetch_05", nv50_vfetch_sources },
			{ 0x2c, "pc01_vfetch_06", nv50_vfetch_sources },
			{ 0x2d, "pc01_vfetch_07", nv50_vfetch_sources },
			{ 0x2e, "pc01_vfetch_08", nv50_vfetch_sources },
			{ 0x2f, "pc01_vfetch_09", nv50_vfetch_sources },
			{ 0x30, "pc01_vfetch_0a", nv50_vfetch_sources },
			{ 0x31, "pc01_vfetch_0b", nv50_vfetch_sources },
			{ 0x32, "pc01_vfetch_0c", nv50_vfetch_sources },
			{ 0x33, "pc01_vfetch_0d", nv50_vfetch_sources },
			{ 0x34, "pc01_vfetch_0e", nv50_vfetch_sources },
			{ 0x35, "pc01_vfetch_0f", nv50_vfetch_sources },
			{ 0x36, "pc01_vfetch_10", nv50_vfetch_sources },
			{ 0x37, "pc01_vfetch_11", nv50_vfetch_sources },
			{ 0x38, "pc01_vfetch_12", nv50_vfetch_sources },
			{ 0x39, "pc01_vfetch_13", nv50_vfetch_sources },
			{ 0x3a, "pc01_vfetch_14", nv50_vfetch_sources },
			{ 0x3b, "pc01_vfetch_15", nv50_vfetch_sources },
			{ 0x3c, "pc01_vfetch_16", nv50_vfetch_sources },
			{ 0x3d, "pc01_vfetch_17", nv50_vfetch_sources },
			{ 0x3e, "pc01_vfetch_18", nv50_vfetch_sources },
			{ 0x3f, "pc01_vfetch_19", nv50_vfetch_sources },
			{ 0x20, "pc01_zcull_00", nv50_zcull_sources },
			{ 0x21, "pc01_zcull_01", nv50_zcull_sources },
			{ 0x22, "pc01_zcull_02", nv50_zcull_sources },
			{ 0x23, "pc01_zcull_03", nv50_zcull_sources },
			{ 0x24, "pc01_zcull_04", nv50_zcull_sources },
			{ 0x25, "pc01_zcull_05", nv50_zcull_sources },
			{ 0xae, "pc01_unk00" },
			{ 0xee, "pc01_trailer" },
			{}
		}, &nv40_perfctr_func },
	{ 0xf0, (const struct nvkm_specsig[]) {
			{ 0x52, "pc02_crop_00", nv50_crop_sources },
			{ 0x53, "pc02_crop_01", nv50_crop_sources },
			{ 0x54, "pc02_crop_02", nv50_crop_sources },
			{ 0x55, "pc02_crop_03", nv50_crop_sources },
			{ 0x00, "pc02_prop_00", nv50_prop_sources },
			{ 0x01, "pc02_prop_01", nv50_prop_sources },
			{ 0x02, "pc02_prop_02", nv50_prop_sources },
			{ 0x03, "pc02_prop_03", nv50_prop_sources },
			{ 0x04, "pc02_prop_04", nv50_prop_sources },
			{ 0x05, "pc02_prop_05", nv50_prop_sources },
			{ 0x06, "pc02_prop_06", nv50_prop_sources },
			{ 0x07, "pc02_prop_07", nv50_prop_sources },
			{ 0x70, "pc02_tex_00", nv50_tex_sources },
			{ 0x71, "pc02_tex_01", nv50_tex_sources },
			{ 0x72, "pc02_tex_02", nv50_tex_sources },
			{ 0x73, "pc02_tex_03", nv50_tex_sources },
			{ 0x40, "pc02_tex_04", nv50_tex_sources },
			{ 0x41, "pc02_tex_05", nv50_tex_sources },
			{ 0x42, "pc02_tex_06", nv50_tex_sources },
			{ 0x6c, "pc02_zrop_00", nv50_zrop_sources },
			{ 0x6d, "pc02_zrop_01", nv50_zrop_sources },
			{ 0x6e, "pc02_zrop_02", nv50_zrop_sources },
			{ 0x6f, "pc02_zrop_03", nv50_zrop_sources },
			{ 0xee, "pc02_trailer" },
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
nv50_pm_new(struct nvkm_device *device, int index, struct nvkm_pm **ppm)
{
	return nv40_pm_new_(nv50_pm, device, index, ppm);
}
