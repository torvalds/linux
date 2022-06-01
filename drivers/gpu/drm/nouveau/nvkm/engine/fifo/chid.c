/*
 * Copyright 2020 Red Hat Inc.
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
#include "chid.h"

static void
nvkm_chid_del(struct kref *kref)
{
	struct nvkm_chid *chid = container_of(kref, typeof(*chid), kref);

	nvkm_event_fini(&chid->event);

	kvfree(chid->data);
	kfree(chid);
}

void
nvkm_chid_unref(struct nvkm_chid **pchid)
{
	struct nvkm_chid *chid = *pchid;

	if (!chid)
		return;

	kref_put(&chid->kref, nvkm_chid_del);
	*pchid = NULL;
}

struct nvkm_chid *
nvkm_chid_ref(struct nvkm_chid *chid)
{
	if (chid)
		kref_get(&chid->kref);

	return chid;
}

int
nvkm_chid_new(const struct nvkm_event_func *func, struct nvkm_subdev *subdev,
	      int nr, int first, int count, struct nvkm_chid **pchid)
{
	struct nvkm_chid *chid;
	int id;

	if (!(chid = *pchid = kzalloc(struct_size(chid, used, nr), GFP_KERNEL)))
		return -ENOMEM;

	kref_init(&chid->kref);
	chid->nr = nr;
	chid->mask = chid->nr - 1;
	spin_lock_init(&chid->lock);

	if (!(chid->data = kvzalloc(sizeof(*chid->data) * nr, GFP_KERNEL))) {
		nvkm_chid_unref(pchid);
		return -ENOMEM;
	}

	for (id = 0; id < first; id++)
		__set_bit(id, chid->used);
	for (id = first + count; id < nr; id++)
		__set_bit(id, chid->used);

	return nvkm_event_init(func, subdev, 1, nr, &chid->event);
}
