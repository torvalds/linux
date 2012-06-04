#ifndef __NOUVEAU_FIFO_H__
#define __NOUVEAU_FIFO_H__

struct nouveau_fifo_priv {
	struct nouveau_exec_engine base;
	u32 channels;
};

struct nouveau_fifo_chan {
};

bool nv04_fifo_cache_pull(struct drm_device *, bool);
void nv04_fifo_context_del(struct nouveau_channel *, int);
int  nv04_fifo_fini(struct drm_device *, int, bool);
int  nv04_fifo_init(struct drm_device *, int);
void nv04_fifo_isr(struct drm_device *);
void nv04_fifo_destroy(struct drm_device *, int);

void nv50_fifo_playlist_update(struct drm_device *);
void nv50_fifo_destroy(struct drm_device *, int);
void nv50_fifo_tlb_flush(struct drm_device *, int);

int  nv04_fifo_create(struct drm_device *);
int  nv10_fifo_create(struct drm_device *);
int  nv17_fifo_create(struct drm_device *);
int  nv40_fifo_create(struct drm_device *);
int  nv50_fifo_create(struct drm_device *);
int  nv84_fifo_create(struct drm_device *);
int  nvc0_fifo_create(struct drm_device *);
int  nve0_fifo_create(struct drm_device *);

#endif
