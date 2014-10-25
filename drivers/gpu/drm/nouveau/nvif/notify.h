#ifndef __NVIF_NOTIFY_H__
#define __NVIF_NOTIFY_H__

struct nvif_notify {
	struct nvif_object *object;
	int index;

#define NVIF_NOTIFY_USER 0
#define NVIF_NOTIFY_WORK 1
	unsigned long flags;
	atomic_t putcnt;
	void (*dtor)(struct nvif_notify *);
#define NVIF_NOTIFY_DROP 0
#define NVIF_NOTIFY_KEEP 1
	int  (*func)(struct nvif_notify *);

	/* this is const for a *very* good reason - the data might be on the
	 * stack from an irq handler.  if you're not nvif/notify.c then you
	 * should probably think twice before casting it away...
	 */
	const void *data;
	u32 size;
	struct work_struct work;
};

int  nvif_notify_init(struct nvif_object *, void (*dtor)(struct nvif_notify *),
		      int (*func)(struct nvif_notify *), bool work, u8 type,
		      void *data, u32 size, u32 reply, struct nvif_notify *);
int  nvif_notify_fini(struct nvif_notify *);
int  nvif_notify_get(struct nvif_notify *);
int  nvif_notify_put(struct nvif_notify *);
int  nvif_notify(const void *, u32, const void *, u32);

int  nvif_notify_new(struct nvif_object *, int (*func)(struct nvif_notify *),
		     bool work, u8 type, void *data, u32 size, u32 reply,
		     struct nvif_notify **);
void nvif_notify_ref(struct nvif_notify *, struct nvif_notify **);

#endif
