#ifndef __NVKM_EVENT_H__
#define __NVKM_EVENT_H__

/* return codes from event handlers */
#define NVKM_EVENT_DROP 0
#define NVKM_EVENT_KEEP 1

/* nouveau_eventh.flags bit #s */
#define NVKM_EVENT_ENABLE 0

struct nouveau_eventh {
	struct nouveau_event *event;
	struct list_head head;
	unsigned long flags;
	int index;
	int (*func)(void *, int);
	void *priv;
};

struct nouveau_event {
	spinlock_t list_lock;
	spinlock_t refs_lock;

	void *priv;
	void (*enable)(struct nouveau_event *, int index);
	void (*disable)(struct nouveau_event *, int index);

	int index_nr;
	struct {
		struct list_head list;
		int refs;
	} index[];
};

int  nouveau_event_create(int index_nr, struct nouveau_event **);
void nouveau_event_destroy(struct nouveau_event **);
void nouveau_event_trigger(struct nouveau_event *, int index);

int  nouveau_event_new(struct nouveau_event *, int index,
		       int (*func)(void *, int), void *,
		       struct nouveau_eventh **);
void nouveau_event_ref(struct nouveau_eventh *, struct nouveau_eventh **);
void nouveau_event_get(struct nouveau_eventh *);
void nouveau_event_put(struct nouveau_eventh *);

#endif
