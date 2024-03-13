/*
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
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

#include "priv.h"
#include "gk20a.h"

#include <core/tegra.h>

static const struct cvb_coef gm20b_cvb_coef[] = {
	/* KHz,             c0,      c1,   c2 */
	/*  76800 */ { 1786666,  -85625, 1632 },
	/* 153600 */ { 1846729,  -87525, 1632 },
	/* 230400 */ { 1910480,  -89425, 1632 },
	/* 307200 */ { 1977920,  -91325, 1632 },
	/* 384000 */ { 2049049,  -93215, 1632 },
	/* 460800 */ { 2122872,  -95095, 1632 },
	/* 537600 */ { 2201331,  -96985, 1632 },
	/* 614400 */ { 2283479,  -98885, 1632 },
	/* 691200 */ { 2369315, -100785, 1632 },
	/* 768000 */ { 2458841, -102685, 1632 },
	/* 844800 */ { 2550821, -104555, 1632 },
	/* 921600 */ { 2647676, -106455, 1632 },
};

static const struct cvb_coef gm20b_na_cvb_coef[] = {
	/* KHz,         c0,     c1,   c2,    c3,     c4,   c5 */
	/*  76800 */ {  814294, 8144, -940, 808, -21583, 226 },
	/* 153600 */ {  856185, 8144, -940, 808, -21583, 226 },
	/* 230400 */ {  898077, 8144, -940, 808, -21583, 226 },
	/* 307200 */ {  939968, 8144, -940, 808, -21583, 226 },
	/* 384000 */ {  981860, 8144, -940, 808, -21583, 226 },
	/* 460800 */ { 1023751, 8144, -940, 808, -21583, 226 },
	/* 537600 */ { 1065642, 8144, -940, 808, -21583, 226 },
	/* 614400 */ { 1107534, 8144, -940, 808, -21583, 226 },
	/* 691200 */ { 1149425, 8144, -940, 808, -21583, 226 },
	/* 768000 */ { 1191317, 8144, -940, 808, -21583, 226 },
	/* 844800 */ { 1233208, 8144, -940, 808, -21583, 226 },
	/* 921600 */ { 1275100, 8144, -940, 808, -21583, 226 },
	/* 998400 */ { 1316991, 8144, -940, 808, -21583, 226 },
};

static const u32 speedo_to_vmin[] = {
	/*   0,      1,      2,      3,      4, */
	950000, 840000, 818750, 840000, 810000,
};

int
gm20b_volt_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_volt **pvolt)
{
	struct nvkm_device_tegra *tdev = device->func->tegra(device);
	struct gk20a_volt *volt;
	u32 vmin;

	if (tdev->gpu_speedo_id >= ARRAY_SIZE(speedo_to_vmin)) {
		nvdev_error(device, "unsupported speedo %d\n",
			    tdev->gpu_speedo_id);
		return -EINVAL;
	}

	volt = kzalloc(sizeof(*volt), GFP_KERNEL);
	if (!volt)
		return -ENOMEM;
	*pvolt = &volt->base;

	vmin = speedo_to_vmin[tdev->gpu_speedo_id];

	if (tdev->gpu_speedo_id >= 1)
		return gk20a_volt_ctor(device, type, inst, gm20b_na_cvb_coef,
				       ARRAY_SIZE(gm20b_na_cvb_coef), vmin, volt);
	else
		return gk20a_volt_ctor(device, type, inst, gm20b_cvb_coef,
				       ARRAY_SIZE(gm20b_cvb_coef), vmin, volt);
}
