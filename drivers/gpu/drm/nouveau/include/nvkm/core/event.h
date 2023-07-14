/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_EVENT_H__
#define __NVKM_EVENT_H__
#include <core/os.h>
struct nvkm_object;
struct nvkm_oclass;
struct nvkm_uevent;

struct nvkm_event {
	const struct nvkm_event_func *func;
	struct nvkm_subdev *subdev;

	int types_nr;
	int index_nr;

	spinlock_t refs_lock;
	spinlock_t list_lock;
	int *refs;

	struct list_head ntfy;
};

struct nvkm_event_func {
	void (*init)(struct nvkm_event *, int type, int index);
	void (*fini)(struct nvkm_event *, int type, int index);
};

int  __nvkm_event_init(const struct nvkm_event_func *func, struct nvkm_subdev *, int types_nr,
		       int index_nr, struct nvkm_event *);

/* Each nvkm_event needs its own lockdep class due to inter-dependencies, to
 * prevent lockdep false-positives.
 *
 * Inlining the spinlock initialisation ensures each is unique.
 */
static __always_inline int
nvkm_event_init(const struct nvkm_event_func *func, struct nvkm_subdev *subdev,
		int types_nr, int index_nr, struct nvkm_event *event)
{
	spin_lock_init(&event->refs_lock);
	spin_lock_init(&event->list_lock);
	return __nvkm_event_init(func, subdev, types_nr, index_nr, event);
}

void nvkm_event_fini(struct nvkm_event *);

#define NVKM_EVENT_KEEP 0
#define NVKM_EVENT_DROP 1
struct nvkm_event_ntfy;
typedef int (*nvkm_event_func)(struct nvkm_event_ntfy *, u32 bits);

struct nvkm_event_ntfy {
	struct nvkm_event *event;
	int id;
	u32 bits;
	bool wait;
	nvkm_event_func func;

	atomic_t allowed;
	bool running;

	struct list_head head;
};

void nvkm_event_ntfy(struct nvkm_event *, int id, u32 bits);
bool nvkm_event_ntfy_valid(struct nvkm_event *, int id, u32 bits);
void nvkm_event_ntfy_add(struct nvkm_event *, int id, u32 bits, bool wait, nvkm_event_func,
			 struct nvkm_event_ntfy *);
void nvkm_event_ntfy_del(struct nvkm_event_ntfy *);
void nvkm_event_ntfy_allow(struct nvkm_event_ntfy *);
void nvkm_event_ntfy_block(struct nvkm_event_ntfy *);

typedef int (*nvkm_uevent_func)(struct nvkm_object *, u64 token, u32 bits);

int nvkm_uevent_new(const struct nvkm_oclass *, void *argv, u32 argc, struct nvkm_object **);
int nvkm_uevent_add(struct nvkm_uevent *, struct nvkm_event *, int id, u32 bits, nvkm_uevent_func);
#endif
