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
#include "head.h"

#include <core/client.h>

#include <nvif/cl0046.h>
#include <nvif/unpack.h>

struct nvkm_head *
nvkm_head_find(struct nvkm_disp *disp, int id)
{
	struct nvkm_head *head;
	list_for_each_entry(head, &disp->heads, head) {
		if (head->id == id)
			return head;
	}
	return NULL;
}

int
nvkm_head_mthd_scanoutpos(struct nvkm_object *object,
			  struct nvkm_head *head, void *data, u32 size)
{
	union {
		struct nv04_disp_scanoutpos_v0 v0;
	} *args = data;
	int ret = -ENOSYS;

	nvif_ioctl(object, "head scanoutpos size %d\n", size);
	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, false))) {
		nvif_ioctl(object, "head scanoutpos vers %d\n",
			   args->v0.version);

		head->func->state(head, &head->arm);
		args->v0.vtotal  = head->arm.vtotal;
		args->v0.vblanks = head->arm.vblanks;
		args->v0.vblanke = head->arm.vblanke;
		args->v0.htotal  = head->arm.htotal;
		args->v0.hblanks = head->arm.hblanks;
		args->v0.hblanke = head->arm.hblanke;

		/* We don't support reading htotal/vtotal on pre-NV50 VGA,
		 * so we have to give up and trigger the timestamping
		 * fallback in the drm core.
		 */
		if (!args->v0.vtotal || !args->v0.htotal)
			return -ENOTSUPP;

		args->v0.time[0] = ktime_to_ns(ktime_get());
		head->func->rgpos(head, &args->v0.hline, &args->v0.vline);
		args->v0.time[1] = ktime_to_ns(ktime_get());
	} else
		return ret;

	return 0;
}

void
nvkm_head_del(struct nvkm_head **phead)
{
	struct nvkm_head *head = *phead;
	if (head) {
		HEAD_DBG(head, "dtor");
		list_del(&head->head);
		kfree(*phead);
		*phead = NULL;
	}
}

int
nvkm_head_new_(const struct nvkm_head_func *func,
	       struct nvkm_disp *disp, int id)
{
	struct nvkm_head *head;
	if (!(head = kzalloc(sizeof(*head), GFP_KERNEL)))
		return -ENOMEM;
	head->func = func;
	head->disp = disp;
	head->id = id;
	list_add_tail(&head->head, &disp->heads);
	HEAD_DBG(head, "ctor");
	return 0;
}
