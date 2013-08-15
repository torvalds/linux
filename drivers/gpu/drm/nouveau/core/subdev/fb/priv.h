#ifndef __NVKM_FB_PRIV_H__
#define __NVKM_FB_PRIV_H__

#include <subdev/fb.h>

#define nouveau_ram_create(p,e,o,d)                                            \
	nouveau_object_create_((p), (e), (o), 0, sizeof(**d), (void **)d)
#define nouveau_ram_destroy(p)                                                 \
	nouveau_object_destroy(&(p)->base)
#define nouveau_ram_init(p)                                                    \
	nouveau_object_init(&(p)->base)
#define nouveau_ram_fini(p,s)                                                  \
	nouveau_object_fini(&(p)->base, (s))

#define _nouveau_ram_dtor nouveau_object_destroy
#define _nouveau_ram_init nouveau_object_init
#define _nouveau_ram_fini nouveau_object_fini

extern struct nouveau_oclass nv04_ram_oclass;
extern struct nouveau_oclass nv10_ram_oclass;
extern struct nouveau_oclass nv1a_ram_oclass;
extern struct nouveau_oclass nv20_ram_oclass;
extern struct nouveau_oclass nv40_ram_oclass;
extern struct nouveau_oclass nv41_ram_oclass;
extern struct nouveau_oclass nv44_ram_oclass;
extern struct nouveau_oclass nv49_ram_oclass;
extern struct nouveau_oclass nv4e_ram_oclass;
extern struct nouveau_oclass nv50_ram_oclass;
extern struct nouveau_oclass nvc0_ram_oclass;

#define nouveau_fb_create(p,e,c,r,d)                                           \
	nouveau_fb_create_((p), (e), (c), (r), sizeof(**d), (void **)d)
#define nouveau_fb_destroy(p) ({                                               \
	struct nouveau_fb *pfb = (p);                                          \
	_nouveau_fb_dtor(nv_object(pfb));                                      \
})
#define nouveau_fb_init(p) ({                                                  \
	struct nouveau_fb *pfb = (p);                                          \
	_nouveau_fb_init(nv_object(pfb));                                      \
})
#define nouveau_fb_fini(p,s) ({                                                \
	struct nouveau_fb *pfb = (p);                                          \
	_nouveau_fb_fini(nv_object(pfb), (s));                                 \
})

int nouveau_fb_create_(struct nouveau_object *, struct nouveau_object *,
		       struct nouveau_oclass *, struct nouveau_oclass *,
		       int length, void **pobject);
void _nouveau_fb_dtor(struct nouveau_object *);
int  _nouveau_fb_init(struct nouveau_object *);
int  _nouveau_fb_fini(struct nouveau_object *, bool);

struct nouveau_bios;
int  nouveau_fb_bios_memtype(struct nouveau_bios *);

bool nv04_fb_memtype_valid(struct nouveau_fb *, u32 memtype);

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

void __nv50_ram_put(struct nouveau_fb *, struct nouveau_mem *);
extern int nv50_fb_memtype[0x80];

#endif
