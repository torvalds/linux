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

#include "nv04.h"

static int
gk20a_timer_init(struct nouveau_object *object)
{
	struct nv04_timer_priv *priv = (void *)object;
	u32 hi = upper_32_bits(priv->suspend_time);
	u32 lo = lower_32_bits(priv->suspend_time);
	int ret;

	ret = nouveau_timer_init(&priv->base);
	if (ret)
		return ret;

	nv_debug(priv, "time low        : 0x%08x\n", lo);
	nv_debug(priv, "time high       : 0x%08x\n", hi);

	/* restore the time before suspend */
	nv_wr32(priv, NV04_PTIMER_TIME_1, hi);
	nv_wr32(priv, NV04_PTIMER_TIME_0, lo);
	return 0;
}

struct nouveau_oclass
gk20a_timer_oclass = {
	.handle = NV_SUBDEV(TIMER, 0xff),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv04_timer_ctor,
		.dtor = nv04_timer_dtor,
		.init = gk20a_timer_init,
		.fini = nv04_timer_fini,
	}
};
