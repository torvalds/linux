/*
 * Copyright 2021 Red Hat Inc.
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
#define nvkm_uhead(p) container_of((p), struct nvkm_head, object)
#include "head.h"

#include <nvif/if0013.h>

static void *
nvkm_uhead_dtor(struct nvkm_object *object)
{
	struct nvkm_head *head = nvkm_uhead(object);
	struct nvkm_disp *disp = head->disp;

	spin_lock(&disp->client.lock);
	head->object.func = NULL;
	spin_unlock(&disp->client.lock);
	return NULL;
}

static const struct nvkm_object_func
nvkm_uhead = {
	.dtor = nvkm_uhead_dtor,
};

int
nvkm_uhead_new(const struct nvkm_oclass *oclass, void *argv, u32 argc, struct nvkm_object **pobject)
{
	struct nvkm_disp *disp = nvkm_udisp(oclass->parent);
	struct nvkm_head *head;
	union nvif_head_args *args = argv;
	int ret;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;
	if (!(head = nvkm_head_find(disp, args->v0.id)))
		return -EINVAL;

	ret = -EBUSY;
	spin_lock(&disp->client.lock);
	if (!head->object.func) {
		nvkm_object_ctor(&nvkm_uhead, oclass, &head->object);
		*pobject = &head->object;
		ret = 0;
	}
	spin_unlock(&disp->client.lock);
	return ret;
}
