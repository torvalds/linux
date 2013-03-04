#ifndef __NOUVEAU_FENCE_H__
#define __NOUVEAU_FENCE_H__

struct nouveau_drm;

struct nouveau_fence {
	struct list_head head;
	struct kref kref;

	bool sysmem;

	struct nouveau_channel *channel;
	unsigned long timeout;
	u32 sequence;
};

int  nouveau_fence_new(struct nouveau_channel *, bool sysmem,
		       struct nouveau_fence **);
struct nouveau_fence *
nouveau_fence_ref(struct nouveau_fence *);
void nouveau_fence_unref(struct nouveau_fence **);

int  nouveau_fence_emit(struct nouveau_fence *, struct nouveau_channel *);
bool nouveau_fence_done(struct nouveau_fence *);
int  nouveau_fence_wait(struct nouveau_fence *, bool lazy, bool intr);
int  nouveau_fence_sync(struct nouveau_fence *, struct nouveau_channel *);

struct nouveau_fence_chan {
	struct list_head pending;
	struct list_head flip;

	int  (*emit)(struct nouveau_fence *);
	int  (*sync)(struct nouveau_fence *, struct nouveau_channel *,
		     struct nouveau_channel *);
	u32  (*read)(struct nouveau_channel *);
	int  (*emit32)(struct nouveau_channel *, u64, u32);
	int  (*sync32)(struct nouveau_channel *, u64, u32);

	spinlock_t lock;
	u32 sequence;
};

struct nouveau_fence_priv {
	void (*dtor)(struct nouveau_drm *);
	bool (*suspend)(struct nouveau_drm *);
	void (*resume)(struct nouveau_drm *);
	int  (*context_new)(struct nouveau_channel *);
	void (*context_del)(struct nouveau_channel *);

	wait_queue_head_t waiting;
	bool uevent;
};

#define nouveau_fence(drm) ((struct nouveau_fence_priv *)(drm)->fence)

void nouveau_fence_context_new(struct nouveau_fence_chan *);
void nouveau_fence_context_del(struct nouveau_fence_chan *);

int nv04_fence_create(struct nouveau_drm *);
int nv04_fence_mthd(struct nouveau_channel *, u32, u32, u32);

int  nv10_fence_emit(struct nouveau_fence *);
int  nv17_fence_sync(struct nouveau_fence *, struct nouveau_channel *,
		     struct nouveau_channel *);
u32  nv10_fence_read(struct nouveau_channel *);
void nv10_fence_context_del(struct nouveau_channel *);
void nv10_fence_destroy(struct nouveau_drm *);
int  nv10_fence_create(struct nouveau_drm *);

int  nv17_fence_create(struct nouveau_drm *);
void nv17_fence_resume(struct nouveau_drm *drm);

int nv50_fence_create(struct nouveau_drm *);
int nv84_fence_create(struct nouveau_drm *);
int nvc0_fence_create(struct nouveau_drm *);

int nouveau_flip_complete(void *chan);

struct nv84_fence_chan {
	struct nouveau_fence_chan base;
	struct nouveau_vma vma;
	struct nouveau_vma vma_gart;
	struct nouveau_vma dispc_vma[4];
};

struct nv84_fence_priv {
	struct nouveau_fence_priv base;
	struct nouveau_bo *bo;
	struct nouveau_bo *bo_gart;
	u32 *suspend;
};

u64  nv84_fence_crtc(struct nouveau_channel *, int);
int  nv84_fence_context_new(struct nouveau_channel *);

#endif
