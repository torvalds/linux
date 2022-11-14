/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_HEAD_H__
#define __NVIF_HEAD_H__
#include <nvif/object.h>
#include <nvif/event.h>
struct nvif_disp;

struct nvif_head {
	struct nvif_object object;
};

int nvif_head_ctor(struct nvif_disp *, const char *name, int id, struct nvif_head *);
void nvif_head_dtor(struct nvif_head *);

static inline int
nvif_head_id(struct nvif_head *head)
{
	return head->object.handle;
}

int nvif_head_vblank_event_ctor(struct nvif_head *, const char *name, nvif_event_func, bool wait,
				struct nvif_event *);
#endif
