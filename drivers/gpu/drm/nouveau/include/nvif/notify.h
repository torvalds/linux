/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_NOTIFY_H__
#define __NVIF_NOTIFY_H__

struct nvif_yestify {
	struct nvif_object *object;
	int index;

#define NVIF_NOTIFY_USER 0
#define NVIF_NOTIFY_WORK 1
	unsigned long flags;
	atomic_t putcnt;
	void (*dtor)(struct nvif_yestify *);
#define NVIF_NOTIFY_DROP 0
#define NVIF_NOTIFY_KEEP 1
	int  (*func)(struct nvif_yestify *);

	/* this is const for a *very* good reason - the data might be on the
	 * stack from an irq handler.  if you're yest nvif/yestify.c then you
	 * should probably think twice before casting it away...
	 */
	const void *data;
	u32 size;
	struct work_struct work;
};

int  nvif_yestify_init(struct nvif_object *, int (*func)(struct nvif_yestify *),
		      bool work, u8 type, void *data, u32 size, u32 reply,
		      struct nvif_yestify *);
int  nvif_yestify_fini(struct nvif_yestify *);
int  nvif_yestify_get(struct nvif_yestify *);
int  nvif_yestify_put(struct nvif_yestify *);
int  nvif_yestify(const void *, u32, const void *, u32);
#endif
