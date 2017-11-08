/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVKM_NOTIFY_H__
#define __NVKM_NOTIFY_H__
#include <core/os.h>
struct nvkm_object;

struct nvkm_notify {
	struct nvkm_event *event;
	struct list_head head;
#define NVKM_NOTIFY_USER 0
#define NVKM_NOTIFY_WORK 1
	unsigned long flags;
	int block;
#define NVKM_NOTIFY_DROP 0
#define NVKM_NOTIFY_KEEP 1
	int (*func)(struct nvkm_notify *);

	/* set by nvkm_event ctor */
	u32 types;
	int index;
	u32 size;

	struct work_struct work;
	/* this is const for a *very* good reason - the data might be on the
	 * stack from an irq handler.  if you're not core/notify.c then you
	 * should probably think twice before casting it away...
	 */
	const void *data;
};

int  nvkm_notify_init(struct nvkm_object *, struct nvkm_event *,
		      int (*func)(struct nvkm_notify *), bool work,
		      void *data, u32 size, u32 reply,
		      struct nvkm_notify *);
void nvkm_notify_fini(struct nvkm_notify *);
void nvkm_notify_get(struct nvkm_notify *);
void nvkm_notify_put(struct nvkm_notify *);
void nvkm_notify_send(struct nvkm_notify *, void *data, u32 size);
#endif
