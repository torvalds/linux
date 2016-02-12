/*
 * Copyright (c) 2014-2016, NVIDIA CORPORATION. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#define gk20a_volt(p) container_of((p), struct gk20a_volt, base)
#include "priv.h"

#include <core/tegra.h>

#include "gk20a.h"

static const struct cvb_coef gk20a_cvb_coef[] = {
	/* MHz,        c0,     c1,   c2,    c3,     c4,   c5 */
	/*  72 */ { 1209886, -36468,  515,   417, -13123,  203},
	/* 108 */ { 1130804, -27659,  296,   298, -10834,  221},
	/* 180 */ { 1162871, -27110,  247,   238, -10681,  268},
	/* 252 */ { 1220458, -28654,  247,   179, -10376,  298},
	/* 324 */ { 1280953, -30204,  247,   119,  -9766,  304},
	/* 396 */ { 1344547, -31777,  247,   119,  -8545,  292},
	/* 468 */ { 1420168, -34227,  269,    60,  -7172,  256},
	/* 540 */ { 1490757, -35955,  274,    60,  -5188,  197},
	/* 612 */ { 1599112, -42583,  398,     0,  -1831,  119},
	/* 648 */ { 1366986, -16459, -274,     0,  -3204,   72},
	/* 684 */ { 1391884, -17078, -274,   -60,  -1526,   30},
	/* 708 */ { 1415522, -17497, -274,   -60,   -458,    0},
	/* 756 */ { 1464061, -18331, -274,  -119,   1831,  -72},
	/* 804 */ { 1524225, -20064, -254,  -119,   4272, -155},
	/* 852 */ { 1608418, -21643, -269,     0,    763,  -48},
};

/**
 * cvb_mv = ((c2 * speedo / s_scale + c1) * speedo / s_scale + c0)
 */
static inline int
gk20a_volt_get_cvb_voltage(int speedo, int s_scale, const struct cvb_coef *coef)
{
	int mv;

	mv = DIV_ROUND_CLOSEST(coef->c2 * speedo, s_scale);
	mv = DIV_ROUND_CLOSEST((mv + coef->c1) * speedo, s_scale) + coef->c0;
	return mv;
}

/**
 * cvb_t_mv =
 * ((c2 * speedo / s_scale + c1) * speedo / s_scale + c0) +
 * ((c3 * speedo / s_scale + c4 + c5 * T / t_scale) * T / t_scale)
 */
static inline int
gk20a_volt_get_cvb_t_voltage(int speedo, int temp, int s_scale, int t_scale,
			     const struct cvb_coef *coef)
{
	int cvb_mv, mv;

	cvb_mv = gk20a_volt_get_cvb_voltage(speedo, s_scale, coef);

	mv = DIV_ROUND_CLOSEST(coef->c3 * speedo, s_scale) + coef->c4 +
		DIV_ROUND_CLOSEST(coef->c5 * temp, t_scale);
	mv = DIV_ROUND_CLOSEST(mv * temp, t_scale) + cvb_mv;
	return mv;
}

int
gk20a_volt_calc_voltage(const struct cvb_coef *coef, int speedo)
{
	int mv;

	mv = gk20a_volt_get_cvb_t_voltage(speedo, -10, 100, 10, coef);
	mv = DIV_ROUND_UP(mv, 1000);

	return mv * 1000;
}

int
gk20a_volt_vid_get(struct nvkm_volt *base)
{
	struct gk20a_volt *volt = gk20a_volt(base);
	int i, uv;

	uv = regulator_get_voltage(volt->vdd);

	for (i = 0; i < volt->base.vid_nr; i++)
		if (volt->base.vid[i].uv >= uv)
			return i;

	return -EINVAL;
}

int
gk20a_volt_vid_set(struct nvkm_volt *base, u8 vid)
{
	struct gk20a_volt *volt = gk20a_volt(base);
	struct nvkm_subdev *subdev = &volt->base.subdev;

	nvkm_debug(subdev, "set voltage as %duv\n", volt->base.vid[vid].uv);
	return regulator_set_voltage(volt->vdd, volt->base.vid[vid].uv, 1200000);
}

int
gk20a_volt_set_id(struct nvkm_volt *base, u8 id, int condition)
{
	struct gk20a_volt *volt = gk20a_volt(base);
	struct nvkm_subdev *subdev = &volt->base.subdev;
	int prev_uv = regulator_get_voltage(volt->vdd);
	int target_uv = volt->base.vid[id].uv;
	int ret;

	nvkm_debug(subdev, "prev=%d, target=%d, condition=%d\n",
		   prev_uv, target_uv, condition);
	if (!condition ||
		(condition < 0 && target_uv < prev_uv) ||
		(condition > 0 && target_uv > prev_uv)) {
		ret = gk20a_volt_vid_set(&volt->base, volt->base.vid[id].vid);
	} else {
		ret = 0;
	}

	return ret;
}

static const struct nvkm_volt_func
gk20a_volt = {
	.vid_get = gk20a_volt_vid_get,
	.vid_set = gk20a_volt_vid_set,
	.set_id = gk20a_volt_set_id,
};

int
_gk20a_volt_ctor(struct nvkm_device *device, int index,
		 const struct cvb_coef *coefs, int nb_coefs,
		 struct gk20a_volt *volt)
{
	struct nvkm_device_tegra *tdev = device->func->tegra(device);
	int i, uv;

	nvkm_volt_ctor(&gk20a_volt, device, index, &volt->base);

	uv = regulator_get_voltage(tdev->vdd);
	nvkm_debug(&volt->base.subdev, "the default voltage is %duV\n", uv);

	volt->vdd = tdev->vdd;

	volt->base.vid_nr = nb_coefs;
	for (i = 0; i < volt->base.vid_nr; i++) {
		volt->base.vid[i].vid = i;
		volt->base.vid[i].uv =
			gk20a_volt_calc_voltage(&coefs[i],
						tdev->gpu_speedo);
		nvkm_debug(&volt->base.subdev, "%2d: vid=%d, uv=%d\n", i,
			   volt->base.vid[i].vid, volt->base.vid[i].uv);
	}

	return 0;
}

int
gk20a_volt_new(struct nvkm_device *device, int index, struct nvkm_volt **pvolt)
{
	struct gk20a_volt *volt;

	volt = kzalloc(sizeof(*volt), GFP_KERNEL);
	if (!volt)
		return -ENOMEM;
	*pvolt = &volt->base;

	return _gk20a_volt_ctor(device, index, gk20a_cvb_coef,
				ARRAY_SIZE(gk20a_cvb_coef), volt);
}
