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

#define nouveau_ram_create_(p,e,o,s,d)                                         \
	nouveau_object_create_((p), (e), (o), 0, (s), (void **)d)
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
extern struct nouveau_oclass nva3_ram_oclass;
extern struct nouveau_oclass nvaa_ram_oclass;
extern struct nouveau_oclass nvc0_ram_oclass;
extern struct nouveau_oclass nve0_ram_oclass;

int nouveau_sddr3_calc(struct nouveau_ram *ram);
int nouveau_gddr5_calc(struct nouveau_ram *ram);

#define nouveau_fb_create(p,e,c,d)                                             \
	nouveau_fb_create_((p), (e), (c), sizeof(**d), (void **)d)
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
		       struct nouveau_oclass *, int, void **);
void _nouveau_fb_dtor(struct nouveau_object *);
int  _nouveau_fb_init(struct nouveau_object *);
int  _nouveau_fb_fini(struct nouveau_object *, bool);

struct nouveau_fb_impl {
	struct nouveau_oclass base;
	struct nouveau_oclass *ram;
	bool (*memtype)(struct nouveau_fb *, u32);
};

bool nv04_fb_memtype_valid(struct nouveau_fb *, u32 memtype);
bool nv50_fb_memtype_valid(struct nouveau_fb *, u32 memtype);

struct nouveau_bios;
int  nouveau_fb_bios_memtype(struct nouveau_bios *);

#endif
