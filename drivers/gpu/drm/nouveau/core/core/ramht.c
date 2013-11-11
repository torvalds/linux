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
 */

#include <core/object.h>
#include <core/ramht.h>
#include <core/math.h>

#include <subdev/bar.h>

static u32
nouveau_ramht_hash(struct nouveau_ramht *ramht, int chid, u32 handle)
{
	u32 hash = 0;

	while (handle) {
		hash ^= (handle & ((1 << ramht->bits) - 1));
		handle >>= ramht->bits;
	}

	hash ^= chid << (ramht->bits - 4);
	hash  = hash << 3;
	return hash;
}

int
nouveau_ramht_insert(struct nouveau_ramht *ramht, int chid,
		     u32 handle, u32 context)
{
	struct nouveau_bar *bar = nouveau_bar(ramht);
	u32 co, ho;

	co = ho = nouveau_ramht_hash(ramht, chid, handle);
	do {
		if (!nv_ro32(ramht, co + 4)) {
			nv_wo32(ramht, co + 0, handle);
			nv_wo32(ramht, co + 4, context);
			if (bar)
				bar->flush(bar);
			return co;
		}

		co += 8;
		if (co >= nv_gpuobj(ramht)->size)
			co = 0;
	} while (co != ho);

	return -ENOMEM;
}

void
nouveau_ramht_remove(struct nouveau_ramht *ramht, int cookie)
{
	struct nouveau_bar *bar = nouveau_bar(ramht);
	nv_wo32(ramht, cookie + 0, 0x00000000);
	nv_wo32(ramht, cookie + 4, 0x00000000);
	if (bar)
		bar->flush(bar);
}

static struct nouveau_oclass
nouveau_ramht_oclass = {
	.handle = 0x0000abcd,
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = NULL,
		.dtor = _nouveau_gpuobj_dtor,
		.init = _nouveau_gpuobj_init,
		.fini = _nouveau_gpuobj_fini,
		.rd32 = _nouveau_gpuobj_rd32,
		.wr32 = _nouveau_gpuobj_wr32,
	},
};

int
nouveau_ramht_new(struct nouveau_object *parent, struct nouveau_object *pargpu,
		  u32 size, u32 align, struct nouveau_ramht **pramht)
{
	struct nouveau_ramht *ramht;
	int ret;

	ret = nouveau_gpuobj_create(parent, parent->engine ?
				    parent->engine : parent, /* <nv50 ramht */
				    &nouveau_ramht_oclass, 0, pargpu, size,
				    align, NVOBJ_FLAG_ZERO_ALLOC, &ramht);
	*pramht = ramht;
	if (ret)
		return ret;

	ramht->bits = log2i(nv_gpuobj(ramht)->size >> 3);
	return 0;
}
