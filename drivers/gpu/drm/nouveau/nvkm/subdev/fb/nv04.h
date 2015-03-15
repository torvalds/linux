#ifndef __NVKM_FB_NV04_H__
#define __NVKM_FB_NV04_H__
#include "priv.h"

struct nv04_fb_priv {
	struct nvkm_fb base;
};

int  nv04_fb_ctor(struct nvkm_object *, struct nvkm_object *,
		  struct nvkm_oclass *, void *, u32,
		  struct nvkm_object **);

struct nv04_fb_impl {
	struct nvkm_fb_impl base;
	struct {
		int regions;
		void (*init)(struct nvkm_fb *, int i, u32 addr, u32 size,
			     u32 pitch, u32 flags, struct nvkm_fb_tile *);
		void (*comp)(struct nvkm_fb *, int i, u32 size, u32 flags,
			     struct nvkm_fb_tile *);
		void (*fini)(struct nvkm_fb *, int i,
			     struct nvkm_fb_tile *);
		void (*prog)(struct nvkm_fb *, int i,
			     struct nvkm_fb_tile *);
	} tile;
};

void nv10_fb_tile_init(struct nvkm_fb *, int i, u32 addr, u32 size,
		       u32 pitch, u32 flags, struct nvkm_fb_tile *);
void nv10_fb_tile_fini(struct nvkm_fb *, int i, struct nvkm_fb_tile *);
void nv10_fb_tile_prog(struct nvkm_fb *, int, struct nvkm_fb_tile *);

void nv20_fb_tile_init(struct nvkm_fb *, int i, u32 addr, u32 size,
		       u32 pitch, u32 flags, struct nvkm_fb_tile *);
void nv20_fb_tile_fini(struct nvkm_fb *, int i, struct nvkm_fb_tile *);
void nv20_fb_tile_prog(struct nvkm_fb *, int, struct nvkm_fb_tile *);

int  nv30_fb_init(struct nvkm_object *);
void nv30_fb_tile_init(struct nvkm_fb *, int i, u32 addr, u32 size,
		       u32 pitch, u32 flags, struct nvkm_fb_tile *);

void nv40_fb_tile_comp(struct nvkm_fb *, int i, u32 size, u32 flags,
		       struct nvkm_fb_tile *);

int  nv41_fb_init(struct nvkm_object *);
void nv41_fb_tile_prog(struct nvkm_fb *, int, struct nvkm_fb_tile *);

int  nv44_fb_init(struct nvkm_object *);
void nv44_fb_tile_prog(struct nvkm_fb *, int, struct nvkm_fb_tile *);

void nv46_fb_tile_init(struct nvkm_fb *, int i, u32 addr, u32 size,
		       u32 pitch, u32 flags, struct nvkm_fb_tile *);
#endif
