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

struct nvif_notify_req_v0 {
	__u8  version;
	__u8  reply;
	__u8  pad02[5];
#define NVIF_NOTIFY_V0_ROUTE_NVIF                                          0x00
	__u8  route;
	__u64 token;	/* must be unique */
	__u8  data[];	/* request data (below) */
};

struct nvif_notify_rep_v0 {
	__u8  version;
	__u8  pad01[6];
	__u8  route;
	__u64 token;
	__u8  data[];	/* reply data (below) */
};

struct nvif_notify_conn_req_v0 {
	/* nvif_notify_req ... */
	__u8  version;
#define NVIF_NOTIFY_CONN_V0_PLUG                                           0x01
#define NVIF_NOTIFY_CONN_V0_UNPLUG                                         0x02
#define NVIF_NOTIFY_CONN_V0_IRQ                                            0x04
#define NVIF_NOTIFY_CONN_V0_ANY                                            0x07
	__u8  mask;
	__u8  conn;
	__u8  pad03[5];
};

struct nvif_notify_conn_rep_v0 {
	/* nvif_notify_rep ... */
	__u8  version;
	__u8  mask;
	__u8  pad02[6];
};

struct nvif_notify_uevent_req {
	/* nvif_notify_req ... */
};

struct nvif_notify_uevent_rep {
	/* nvif_notify_rep ... */
};

#endif
