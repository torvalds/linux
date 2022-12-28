/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_CONN_H__
#define __NVIF_CONN_H__
#include <nvif/object.h>
#include <nvif/event.h>
struct nvif_disp;

struct nvif_conn {
	struct nvif_object object;
};

int nvif_conn_ctor(struct nvif_disp *, const char *name, int id, struct nvif_conn *);
void nvif_conn_dtor(struct nvif_conn *);

static inline int
nvif_conn_id(struct nvif_conn *conn)
{
	return conn->object.handle;
}

#define NVIF_CONN_HPD_STATUS_UNSUPPORTED 0 /* negative if query fails */
#define NVIF_CONN_HPD_STATUS_NOT_PRESENT 1
#define NVIF_CONN_HPD_STATUS_PRESENT     2
int nvif_conn_hpd_status(struct nvif_conn *);

int nvif_conn_event_ctor(struct nvif_conn *, const char *name, nvif_event_func, u8 types,
			 struct nvif_event *);
#endif
