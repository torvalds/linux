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
#define nvkm_uevent(p) container_of((p), struct nvkm_uevent, object)
#include <core/event.h>
#include <core/client.h>

#include <nvif/if000e.h>

struct nvkm_uevent {
	struct nvkm_object object;
	struct nvkm_object *parent;
	nvkm_uevent_func func;
	bool wait;

	struct nvkm_event_ntfy ntfy;
	atomic_t allowed;
};

static int
nvkm_uevent_mthd_block(struct nvkm_uevent *uevent, union nvif_event_block_args *args, u32 argc)
{
	if (argc != sizeof(args->vn))
		return -ENOSYS;

	nvkm_event_ntfy_block(&uevent->ntfy);
	atomic_set(&uevent->allowed, 0);
	return 0;
}

static int
nvkm_uevent_mthd_allow(struct nvkm_uevent *uevent, union nvif_event_allow_args *args, u32 argc)
{
	if (argc != sizeof(args->vn))
		return -ENOSYS;

	nvkm_event_ntfy_allow(&uevent->ntfy);
	atomic_set(&uevent->allowed, 1);
	return 0;
}

static int
nvkm_uevent_mthd(struct nvkm_object *object, u32 mthd, void *argv, u32 argc)
{
	struct nvkm_uevent *uevent = nvkm_uevent(object);

	switch (mthd) {
	case NVIF_EVENT_V0_ALLOW: return nvkm_uevent_mthd_allow(uevent, argv, argc);
	case NVIF_EVENT_V0_BLOCK: return nvkm_uevent_mthd_block(uevent, argv, argc);
	default:
		break;
	}

	return -EINVAL;
}

static int
nvkm_uevent_fini(struct nvkm_object *object, bool suspend)
{
	struct nvkm_uevent *uevent = nvkm_uevent(object);

	nvkm_event_ntfy_block(&uevent->ntfy);
	return 0;
}

static int
nvkm_uevent_init(struct nvkm_object *object)
{
	struct nvkm_uevent *uevent = nvkm_uevent(object);

	if (atomic_read(&uevent->allowed))
		nvkm_event_ntfy_allow(&uevent->ntfy);

	return 0;
}

static void *
nvkm_uevent_dtor(struct nvkm_object *object)
{
	struct nvkm_uevent *uevent = nvkm_uevent(object);

	nvkm_event_ntfy_del(&uevent->ntfy);
	return uevent;
}

static const struct nvkm_object_func
nvkm_uevent = {
	.dtor = nvkm_uevent_dtor,
	.init = nvkm_uevent_init,
	.fini = nvkm_uevent_fini,
	.mthd = nvkm_uevent_mthd,
};

static int
nvkm_uevent_ntfy(struct nvkm_event_ntfy *ntfy, u32 bits)
{
	struct nvkm_uevent *uevent = container_of(ntfy, typeof(*uevent), ntfy);
	struct nvkm_client *client = uevent->object.client;

	if (uevent->func)
		return uevent->func(uevent->parent, uevent->object.object, bits);

	return client->event(uevent->object.object, NULL, 0);
}

int
nvkm_uevent_add(struct nvkm_uevent *uevent, struct nvkm_event *event, int id, u32 bits,
		nvkm_uevent_func func)
{
	if (WARN_ON(uevent->func))
		return -EBUSY;

	nvkm_event_ntfy_add(event, id, bits, uevent->wait, nvkm_uevent_ntfy, &uevent->ntfy);
	uevent->func = func;
	return 0;
}

int
nvkm_uevent_new(const struct nvkm_oclass *oclass, void *argv, u32 argc,
		struct nvkm_object **pobject)
{
	struct nvkm_object *parent = oclass->parent;
	struct nvkm_uevent *uevent;
	union nvif_event_args *args = argv;

	if (argc < sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;

	if (!(uevent = kzalloc(sizeof(*uevent), GFP_KERNEL)))
		return -ENOMEM;
	*pobject = &uevent->object;

	nvkm_object_ctor(&nvkm_uevent, oclass, &uevent->object);
	uevent->parent = parent;
	uevent->func = NULL;
	uevent->wait = args->v0.wait;
	uevent->ntfy.event = NULL;
	return parent->func->uevent(parent, &args->v0.data, argc - sizeof(args->v0), uevent);
}
