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
