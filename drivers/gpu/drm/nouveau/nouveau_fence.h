#ifndef __NOUVEAU_FENCE_H__
#define __NOUVEAU_FENCE_H__

struct nouveau_fence {
	struct list_head head;
	struct kref kref;

	struct nouveau_channel *channel;
	unsigned long timeout;
	u32 sequence;

	void (*work)(void *priv, bool signalled);
	void *priv;
};

int  nouveau_fence_new(struct nouveau_channel *, struct nouveau_fence **);
struct nouveau_fence *
nouveau_fence_ref(struct nouveau_fence *);
void nouveau_fence_unref(struct nouveau_fence **);

int  nouveau_fence_emit(struct nouveau_fence *, struct nouveau_channel *);
bool nouveau_fence_done(struct nouveau_fence *);
int  nouveau_fence_wait(struct nouveau_fence *, bool lazy, bool intr);
int  nouveau_fence_sync(struct nouveau_fence *, struct nouveau_channel *);
void nouveau_fence_idle(struct nouveau_channel *);
void nouveau_fence_update(struct nouveau_channel *);

struct nouveau_fence_chan {
	struct list_head pending;
	spinlock_t lock;
	u32 sequence;
};

struct nouveau_fence_priv {
	struct nouveau_exec_engine engine;
	int (*emit)(struct nouveau_fence *);
	int (*sync)(struct nouveau_fence *, struct nouveau_channel *,
		    struct nouveau_channel *);
	u32 (*read)(struct nouveau_channel *);
};

void nouveau_fence_context_new(struct nouveau_fence_chan *);
void nouveau_fence_context_del(struct nouveau_fence_chan *);

int nv04_fence_create(struct drm_device *dev);
int nv04_fence_mthd(struct nouveau_channel *, u32, u32, u32);

int nv10_fence_create(struct drm_device *dev);
int nv84_fence_create(struct drm_device *dev);
int nvc0_fence_create(struct drm_device *dev);

#endif
