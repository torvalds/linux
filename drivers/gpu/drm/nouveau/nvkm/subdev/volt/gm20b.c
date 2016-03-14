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

const struct cvb_coef gm20b_cvb_coef[] = {
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

int
gm20b_volt_new(struct nvkm_device *device, int index, struct nvkm_volt **pvolt)
{
	struct gk20a_volt *volt;

	volt = kzalloc(sizeof(*volt), GFP_KERNEL);
	if (!volt)
		return -ENOMEM;
	*pvolt = &volt->base;

	return _gk20a_volt_ctor(device, index, gm20b_cvb_coef,
				ARRAY_SIZE(gm20b_cvb_coef), volt);
}
