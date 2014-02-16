#ifndef __NVKM_FB_NV04_H__
#define __NVKM_FB_NV04_H__

#include "priv.h"

struct nv04_fb_priv {
	struct nouveau_fb base;
};

int  nv04_fb_ctor(struct nouveau_object *, struct nouveau_object *,
		  struct nouveau_oclass *, void *, u32,
		  struct nouveau_object **);

struct nv04_fb_impl {
	struct nouveau_fb_impl base;
	struct {
		int regions;
		void (*init)(struct nouveau_fb *, int i, u32 addr, u32 size,
			     u32 pitch, u32 flags, struct nouveau_fb_tile *);
		void (*comp)(struct nouveau_fb *, int i, u32 size, u32 flags,
			     struct nouveau_fb_tile *);
		void (*fini)(struct nouveau_fb *, int i,
			     struct nouveau_fb_tile *);
		void (*prog)(struct nouveau_fb *, int i,
			     struct nouveau_fb_tile *);
	} tile;
};

void nv10_fb_tile_init(struct nouveau_fb *, int i, u32 addr, u32 size,
		       u32 pitch, u32 flags, struct nouveau_fb_tile *);
void nv10_fb_tile_fini(struct nouveau_fb *, int i, struct nouveau_fb_tile *);
void nv10_fb_tile_prog(struct nouveau_fb *, int, struct nouveau_fb_tile *);

void nv20_fb_tile_init(struct nouveau_fb *, int i, u32 addr, u32 size,
		       u32 pitch, u32 flags, struct nouveau_fb_tile *);
void nv20_fb_tile_fini(struct nouveau_fb *, int i, struct nouveau_fb_tile *);
void nv20_fb_tile_prog(struct nouveau_fb *, int, struct nouveau_fb_tile *);

int  nv30_fb_init(struct nouveau_object *);
void nv30_fb_tile_init(struct nouveau_fb *, int i, u32 addr, u32 size,
		       u32 pitch, u32 flags, struct nouveau_fb_tile *);

void nv40_fb_tile_comp(struct nouveau_fb *, int i, u32 size, u32 flags,
		       struct nouveau_fb_tile *);

int  nv41_fb_init(struct nouveau_object *);
void nv41_fb_tile_prog(struct nouveau_fb *, int, struct nouveau_fb_tile *);

int  nv44_fb_init(struct nouveau_object *);
void nv44_fb_tile_prog(struct nouveau_fb *, int, struct nouveau_fb_tile *);

void nv46_fb_tile_init(struct nouveau_fb *, int i, u32 addr, u32 size,
		       u32 pitch, u32 flags, struct nouveau_fb_tile *);

#endif
