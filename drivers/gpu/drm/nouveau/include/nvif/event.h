/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_EVENT_H__
#define __NVIF_EVENT_H__
#include <nvif/object.h>
#include <nvif/if000e.h>
struct nvif_event;

#define NVIF_EVENT_KEEP 0
#define NVIF_EVENT_DROP 1
typedef int (*nvif_event_func)(struct nvif_event *, void *repv, u32 repc);

struct nvif_event {
	struct nvif_object object;
	nvif_event_func func;
};

static inline bool
nvif_event_constructed(struct nvif_event *event)
{
	return nvif_object_constructed(&event->object);
}

int nvif_event_ctor_(struct nvif_object *, const char *, u32, nvif_event_func, bool,
		     struct nvif_event_v0 *, u32, bool, struct nvif_event *);

static inline int
nvif_event_ctor(struct nvif_object *parent, const char *name, u32 handle, nvif_event_func func,
		bool wait, struct nvif_event_v0 *args, u32 argc, struct nvif_event *event)
{
	return nvif_event_ctor_(parent, name, handle, func, wait, args, argc, true, event);
}

void nvif_event_dtor(struct nvif_event *);
int nvif_event_allow(struct nvif_event *);
int nvif_event_block(struct nvif_event *);
#endif
