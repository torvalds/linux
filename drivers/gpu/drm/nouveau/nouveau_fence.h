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

#endif
