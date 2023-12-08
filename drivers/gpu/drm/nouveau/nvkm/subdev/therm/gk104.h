/*
 * Copyright 2018 Red Hat Inc.
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

#ifndef __GK104_THERM_H__
#define __GK104_THERM_H__
#define gk104_therm(p) (container_of((p), struct gk104_therm, base))

#include <subdev/therm.h>
#include "priv.h"
#include "gf100.h"

struct gk104_clkgate_engine_info {
	enum nvkm_subdev_type type;
	int inst;
	u8 offset;
};

struct gk104_therm {
	struct nvkm_therm base;

	const struct gk104_clkgate_engine_info *clkgate_order;
	const struct gf100_idle_filter *idle_filter;
};

extern const struct gk104_clkgate_engine_info gk104_clkgate_engine_info[];
extern const struct gf100_idle_filter gk104_idle_filter;

#endif
