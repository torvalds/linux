/* SPDX-License-Identifier: MIT */
#ifndef __NOUVEAU_FENCE_H__
#define __NOUVEAU_FENCE_H__

#include <linux/dma-fence.h>
#include <nvif/yestify.h>

struct yesuveau_drm;
struct yesuveau_bo;

struct yesuveau_fence {
	struct dma_fence base;

	struct list_head head;

	struct yesuveau_channel __rcu *channel;
	unsigned long timeout;
};

int  yesuveau_fence_new(struct yesuveau_channel *, bool sysmem,
		       struct yesuveau_fence **);
void yesuveau_fence_unref(struct yesuveau_fence **);

int  yesuveau_fence_emit(struct yesuveau_fence *, struct yesuveau_channel *);
bool yesuveau_fence_done(struct yesuveau_fence *);
int  yesuveau_fence_wait(struct yesuveau_fence *, bool lazy, bool intr);
int  yesuveau_fence_sync(struct yesuveau_bo *, struct yesuveau_channel *, bool exclusive, bool intr);

struct yesuveau_fence_chan {
	spinlock_t lock;
	struct kref fence_ref;

	struct list_head pending;
	struct list_head flip;

	int  (*emit)(struct yesuveau_fence *);
	int  (*sync)(struct yesuveau_fence *, struct yesuveau_channel *,
		     struct yesuveau_channel *);
	u32  (*read)(struct yesuveau_channel *);
	int  (*emit32)(struct yesuveau_channel *, u64, u32);
	int  (*sync32)(struct yesuveau_channel *, u64, u32);

	u32 sequence;
	u32 context;
	char name[32];

	struct nvif_yestify yestify;
	int yestify_ref, dead;
};

struct yesuveau_fence_priv {
	void (*dtor)(struct yesuveau_drm *);
	bool (*suspend)(struct yesuveau_drm *);
	void (*resume)(struct yesuveau_drm *);
	int  (*context_new)(struct yesuveau_channel *);
	void (*context_del)(struct yesuveau_channel *);

	bool uevent;
};

#define yesuveau_fence(drm) ((struct yesuveau_fence_priv *)(drm)->fence)

void yesuveau_fence_context_new(struct yesuveau_channel *, struct yesuveau_fence_chan *);
void yesuveau_fence_context_del(struct yesuveau_fence_chan *);
void yesuveau_fence_context_free(struct yesuveau_fence_chan *);

int nv04_fence_create(struct yesuveau_drm *);
int nv04_fence_mthd(struct yesuveau_channel *, u32, u32, u32);

int  nv10_fence_emit(struct yesuveau_fence *);
int  nv17_fence_sync(struct yesuveau_fence *, struct yesuveau_channel *,
		     struct yesuveau_channel *);
u32  nv10_fence_read(struct yesuveau_channel *);
void nv10_fence_context_del(struct yesuveau_channel *);
void nv10_fence_destroy(struct yesuveau_drm *);
int  nv10_fence_create(struct yesuveau_drm *);

int  nv17_fence_create(struct yesuveau_drm *);
void nv17_fence_resume(struct yesuveau_drm *drm);

int nv50_fence_create(struct yesuveau_drm *);
int nv84_fence_create(struct yesuveau_drm *);
int nvc0_fence_create(struct yesuveau_drm *);

struct nv84_fence_chan {
	struct yesuveau_fence_chan base;
	struct yesuveau_vma *vma;
};

struct nv84_fence_priv {
	struct yesuveau_fence_priv base;
	struct yesuveau_bo *bo;
	u32 *suspend;
	struct mutex mutex;
};

int  nv84_fence_context_new(struct yesuveau_channel *);

#endif
