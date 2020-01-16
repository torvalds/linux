/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_NOTIFY_H__
#define __NVKM_NOTIFY_H__
#include <core/os.h>
struct nvkm_object;

struct nvkm_yestify {
	struct nvkm_event *event;
	struct list_head head;
#define NVKM_NOTIFY_USER 0
#define NVKM_NOTIFY_WORK 1
	unsigned long flags;
	int block;
#define NVKM_NOTIFY_DROP 0
#define NVKM_NOTIFY_KEEP 1
	int (*func)(struct nvkm_yestify *);

	/* set by nvkm_event ctor */
	u32 types;
	int index;
	u32 size;

	struct work_struct work;
	/* this is const for a *very* good reason - the data might be on the
	 * stack from an irq handler.  if you're yest core/yestify.c then you
	 * should probably think twice before casting it away...
	 */
	const void *data;
};

int  nvkm_yestify_init(struct nvkm_object *, struct nvkm_event *,
		      int (*func)(struct nvkm_yestify *), bool work,
		      void *data, u32 size, u32 reply,
		      struct nvkm_yestify *);
void nvkm_yestify_fini(struct nvkm_yestify *);
void nvkm_yestify_get(struct nvkm_yestify *);
void nvkm_yestify_put(struct nvkm_yestify *);
void nvkm_yestify_send(struct nvkm_yestify *, void *data, u32 size);
#endif
