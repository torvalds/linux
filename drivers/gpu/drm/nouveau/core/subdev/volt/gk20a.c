/*
 * Copyright (c) 2014, NVIDIA CORPORATION. All rights reserved.
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

#ifdef __KERNEL__
#include <nouveau_platform.h>
#endif
#include <subdev/volt.h>

struct cvb_coef {
	int c0;
	int c1;
	int c2;
	int c3;
	int c4;
	int c5;
};

struct gk20a_volt_priv {
	struct nouveau_volt base;
	struct regulator *vdd;
};

const struct cvb_coef gk20a_cvb_coef[] = {
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
gk20a_volt_get_cvb_voltage(int speedo, int s_scale,
		const struct cvb_coef *coef)
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

static int
gk20a_volt_calc_voltage(const struct cvb_coef *coef, int speedo)
{
	int mv;

	mv = gk20a_volt_get_cvb_t_voltage(speedo, -10, 100, 10, coef);
	mv = DIV_ROUND_UP(mv, 1000);

	return mv * 1000;
}

static int
gk20a_volt_vid_get(struct nouveau_volt *volt)
{
	struct gk20a_volt_priv *priv = (void *)volt;
	int i, uv;

	uv = regulator_get_voltage(priv->vdd);

	for (i = 0; i < volt->vid_nr; i++)
		if (volt->vid[i].uv >= uv)
			return i;

	return -EINVAL;
}

static int
gk20a_volt_vid_set(struct nouveau_volt *volt, u8 vid)
{
	struct gk20a_volt_priv *priv = (void *)volt;

	nv_debug(volt, "set voltage as %duv\n", volt->vid[vid].uv);
	return regulator_set_voltage(priv->vdd, volt->vid[vid].uv, 1200000);
}

static int
gk20a_volt_set_id(struct nouveau_volt *volt, u8 id, int condition)
{
	struct gk20a_volt_priv *priv = (void *)volt;
	int prev_uv = regulator_get_voltage(priv->vdd);
	int target_uv = volt->vid[id].uv;
	int ret;

	nv_debug(volt, "prev=%d, target=%d, condition=%d\n",
			prev_uv, target_uv, condition);
	if (!condition ||
		(condition < 0 && target_uv < prev_uv) ||
		(condition > 0 && target_uv > prev_uv)) {
		ret = gk20a_volt_vid_set(volt, volt->vid[id].vid);
	} else {
		ret = 0;
	}

	return ret;
}

static int
gk20a_volt_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct gk20a_volt_priv *priv;
	struct nouveau_volt *volt;
	struct nouveau_platform_device *plat;
	int i, ret, uv;

	ret = nouveau_volt_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	volt = &priv->base;

	plat = nv_device_to_platform(nv_device(parent));

	uv = regulator_get_voltage(plat->gpu->vdd);
	nv_info(priv, "The default voltage is %duV\n", uv);

	priv->vdd = plat->gpu->vdd;
	priv->base.vid_get = gk20a_volt_vid_get;
	priv->base.vid_set = gk20a_volt_vid_set;
	priv->base.set_id = gk20a_volt_set_id;

	volt->vid_nr = ARRAY_SIZE(gk20a_cvb_coef);
	nv_debug(priv, "%s - vid_nr = %d\n", __func__, volt->vid_nr);
	for (i = 0; i < volt->vid_nr; i++) {
		volt->vid[i].vid = i;
		volt->vid[i].uv = gk20a_volt_calc_voltage(&gk20a_cvb_coef[i],
					plat->gpu_speedo);
		nv_debug(priv, "%2d: vid=%d, uv=%d\n", i, volt->vid[i].vid,
					volt->vid[i].uv);
	}

	return 0;
}

struct nouveau_oclass
gk20a_volt_oclass = {
	.handle = NV_SUBDEV(VOLT, 0xea),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = gk20a_volt_ctor,
		.dtor = _nouveau_volt_dtor,
		.init = _nouveau_volt_init,
		.fini = _nouveau_volt_fini,
	},
};
