/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_HEAD_H__
#define __NVIF_HEAD_H__
#include <nvif/object.h>
struct nvif_disp;

struct nvif_head {
	struct nvif_object object;
};

int nvif_head_ctor(struct nvif_disp *, const char *name, int id, struct nvif_head *);
void nvif_head_dtor(struct nvif_head *);
#endif
