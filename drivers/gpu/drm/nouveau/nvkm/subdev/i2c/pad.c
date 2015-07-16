/*
 * Copyright 2014 Red Hat Inc.
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
#include "pad.h"

int
_nvkm_i2c_pad_fini(struct nvkm_object *object, bool suspend)
{
	struct nvkm_i2c_pad *pad = (void *)object;
	DBG("-> NULL\n");
	pad->port = NULL;
	return nvkm_object_fini(&pad->base, suspend);
}

int
_nvkm_i2c_pad_init(struct nvkm_object *object)
{
	struct nvkm_i2c_pad *pad = (void *)object;
	DBG("-> PORT:%02x\n", pad->next->index);
	pad->port = pad->next;
	return nvkm_object_init(&pad->base);
}

int
nvkm_i2c_pad_create_(struct nvkm_object *parent,
		     struct nvkm_object *engine,
		     struct nvkm_oclass *oclass, int index,
		     int size, void **pobject)
{
	struct nvkm_i2c *i2c = nvkm_i2c(parent);
	struct nvkm_i2c_port *port;
	struct nvkm_i2c_pad *pad;
	int ret;

	list_for_each_entry(port, &i2c->ports, head) {
		pad = nvkm_i2c_pad(port);
		if (pad->index == index) {
			atomic_inc(&nv_object(pad)->refcount);
			*pobject = pad;
			return 1;
		}
	}

	ret = nvkm_object_create_(parent, engine, oclass, 0, size, pobject);
	pad = *pobject;
	if (ret)
		return ret;

	pad->index = index;
	return 0;
}

int
_nvkm_i2c_pad_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		   struct nvkm_oclass *oclass, void *data, u32 index,
		   struct nvkm_object **pobject)
{
	struct nvkm_i2c_pad *pad;
	int ret;
	ret = nvkm_i2c_pad_create(parent, engine, oclass, index, &pad);
	*pobject = nv_object(pad);
	return ret;
}
