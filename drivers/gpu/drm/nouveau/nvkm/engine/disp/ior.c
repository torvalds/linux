/*
 * Copyright 2017 Red Hat Inc.
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
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */
#include "ior.h"

static const char *
nvkm_ior_name[] = {
	[DAC] = "DAC",
	[SOR] = "SOR",
	[PIOR] = "PIOR",
};

struct nvkm_ior *
nvkm_ior_find(struct nvkm_disp *disp, enum nvkm_ior_type type, int id)
{
	struct nvkm_ior *ior;
	list_for_each_entry(ior, &disp->ior, head) {
		if (ior->type == type && (id < 0 || ior->id == id))
			return ior;
	}
	return NULL;
}

void
nvkm_ior_del(struct nvkm_ior **pior)
{
	struct nvkm_ior *ior = *pior;
	if (ior) {
		IOR_DBG(ior, "dtor");
		list_del(&ior->head);
		kfree(*pior);
		*pior = NULL;
	}
}

int
nvkm_ior_new_(const struct nvkm_ior_func *func, struct nvkm_disp *disp,
	      enum nvkm_ior_type type, int id)
{
	struct nvkm_ior *ior;
	if (!(ior = kzalloc(sizeof(*ior), GFP_KERNEL)))
		return -ENOMEM;
	ior->func = func;
	ior->disp = disp;
	ior->type = type;
	ior->id = id;
	snprintf(ior->name, sizeof(ior->name), "%s-%d",
		 nvkm_ior_name[ior->type], ior->id);
	list_add_tail(&ior->head, &disp->ior);
	IOR_DBG(ior, "ctor");
	return 0;
}
