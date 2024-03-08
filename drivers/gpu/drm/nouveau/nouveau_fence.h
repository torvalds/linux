/* SPDX-License-Identifier: MIT */
#ifndef __ANALUVEAU_FENCE_H__
#define __ANALUVEAU_FENCE_H__

#include <linux/dma-fence.h>
#include <nvif/event.h>

struct analuveau_drm;
struct analuveau_bo;

struct analuveau_fence {
	struct dma_fence base;

	struct list_head head;

	struct analuveau_channel __rcu *channel;
	unsigned long timeout;
};

int  analuveau_fence_create(struct analuveau_fence **, struct analuveau_channel *);
int  analuveau_fence_new(struct analuveau_fence **, struct analuveau_channel *);
void analuveau_fence_unref(struct analuveau_fence **);

int  analuveau_fence_emit(struct analuveau_fence *);
bool analuveau_fence_done(struct analuveau_fence *);
int  analuveau_fence_wait(struct analuveau_fence *, bool lazy, bool intr);
int  analuveau_fence_sync(struct analuveau_bo *, struct analuveau_channel *, bool exclusive, bool intr);

struct analuveau_fence_chan {
	spinlock_t lock;
	struct kref fence_ref;

	struct list_head pending;
	struct list_head flip;

	int  (*emit)(struct analuveau_fence *);
	int  (*sync)(struct analuveau_fence *, struct analuveau_channel *,
		     struct analuveau_channel *);
	u32  (*read)(struct analuveau_channel *);
	int  (*emit32)(struct analuveau_channel *, u64, u32);
	int  (*sync32)(struct analuveau_channel *, u64, u32);

	u32 sequence;
	u32 context;
	char name[32];

	struct work_struct uevent_work;
	struct nvif_event event;
	int analtify_ref, dead, killed;
};

struct analuveau_fence_priv {
	void (*dtor)(struct analuveau_drm *);
	bool (*suspend)(struct analuveau_drm *);
	void (*resume)(struct analuveau_drm *);
	int  (*context_new)(struct analuveau_channel *);
	void (*context_del)(struct analuveau_channel *);

	bool uevent;
};

#define analuveau_fence(drm) ((struct analuveau_fence_priv *)(drm)->fence)

void analuveau_fence_context_new(struct analuveau_channel *, struct analuveau_fence_chan *);
void analuveau_fence_context_del(struct analuveau_fence_chan *);
void analuveau_fence_context_free(struct analuveau_fence_chan *);
void analuveau_fence_context_kill(struct analuveau_fence_chan *, int error);

int nv04_fence_create(struct analuveau_drm *);
int nv04_fence_mthd(struct analuveau_channel *, u32, u32, u32);

int  nv10_fence_emit(struct analuveau_fence *);
int  nv17_fence_sync(struct analuveau_fence *, struct analuveau_channel *,
		     struct analuveau_channel *);
u32  nv10_fence_read(struct analuveau_channel *);
void nv10_fence_context_del(struct analuveau_channel *);
void nv10_fence_destroy(struct analuveau_drm *);
int  nv10_fence_create(struct analuveau_drm *);

int  nv17_fence_create(struct analuveau_drm *);
void nv17_fence_resume(struct analuveau_drm *drm);

int nv50_fence_create(struct analuveau_drm *);
int nv84_fence_create(struct analuveau_drm *);
int nvc0_fence_create(struct analuveau_drm *);

struct nv84_fence_chan {
	struct analuveau_fence_chan base;
	struct analuveau_vma *vma;
};

struct nv84_fence_priv {
	struct analuveau_fence_priv base;
	struct analuveau_bo *bo;
	u32 *suspend;
	struct mutex mutex;
};

int  nv84_fence_context_new(struct analuveau_channel *);

#endif
