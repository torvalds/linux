/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_CONN_H__
#define __NVIF_CONN_H__
#include <nvif/object.h>
struct nvif_disp;

struct nvif_conn {
	struct nvif_object object;
};

int nvif_conn_ctor(struct nvif_disp *, const char *name, int id, struct nvif_conn *);
void nvif_conn_dtor(struct nvif_conn *);
#endif
