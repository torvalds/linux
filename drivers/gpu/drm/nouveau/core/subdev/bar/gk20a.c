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

#include <subdev/bar.h>

#include "priv.h"

int
gk20a_bar_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nouveau_bar *bar;
	int ret;

	ret = nvc0_bar_ctor(parent, engine, oclass, data, size, pobject);
	if (ret)
		return ret;

	bar = (struct nouveau_bar *)*pobject;
	bar->iomap_uncached = true;

	return 0;
}

struct nouveau_oclass
gk20a_bar_oclass = {
	.handle = NV_SUBDEV(BAR, 0xea),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = gk20a_bar_ctor,
		.dtor = nvc0_bar_dtor,
		.init = nvc0_bar_init,
		.fini = _nouveau_bar_fini,
	},
};
