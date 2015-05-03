#ifndef __NVKM_FB_PRIV_H__
#define __NVKM_FB_PRIV_H__
#include <subdev/fb.h>
struct nvkm_bios;

#define nvkm_ram_create(p,e,o,d)                                            \
	nvkm_object_create_((p), (e), (o), 0, sizeof(**d), (void **)d)
#define nvkm_ram_destroy(p)                                                 \
	nvkm_object_destroy(&(p)->base)
#define nvkm_ram_init(p)                                                    \
	nvkm_object_init(&(p)->base)
#define nvkm_ram_fini(p,s)                                                  \
	nvkm_object_fini(&(p)->base, (s))

#define nvkm_ram_create_(p,e,o,s,d)                                         \
	nvkm_object_create_((p), (e), (o), 0, (s), (void **)d)
#define _nvkm_ram_dtor nvkm_object_destroy
#define _nvkm_ram_init nvkm_object_init
#define _nvkm_ram_fini nvkm_object_fini

extern struct nvkm_oclass nv04_ram_oclass;
extern struct nvkm_oclass nv10_ram_oclass;
extern struct nvkm_oclass nv1a_ram_oclass;
extern struct nvkm_oclass nv20_ram_oclass;
extern struct nvkm_oclass nv40_ram_oclass;
extern struct nvkm_oclass nv41_ram_oclass;
extern struct nvkm_oclass nv44_ram_oclass;
extern struct nvkm_oclass nv49_ram_oclass;
extern struct nvkm_oclass nv4e_ram_oclass;
extern struct nvkm_oclass nv50_ram_oclass;
extern struct nvkm_oclass gt215_ram_oclass;
extern struct nvkm_oclass mcp77_ram_oclass;
extern struct nvkm_oclass gf100_ram_oclass;
extern struct nvkm_oclass gk104_ram_oclass;
extern struct nvkm_oclass gm107_ram_oclass;

int nvkm_sddr2_calc(struct nvkm_ram *ram);
int nvkm_sddr3_calc(struct nvkm_ram *ram);
int nvkm_gddr3_calc(struct nvkm_ram *ram);
int nvkm_gddr5_calc(struct nvkm_ram *ram, bool nuts);

#define nvkm_fb_create(p,e,c,d)                                             \
	nvkm_fb_create_((p), (e), (c), sizeof(**d), (void **)d)
#define nvkm_fb_destroy(p) ({                                               \
	struct nvkm_fb *pfb = (p);                                          \
	_nvkm_fb_dtor(nv_object(pfb));                                      \
})
#define nvkm_fb_init(p) ({                                                  \
	struct nvkm_fb *pfb = (p);                                          \
	_nvkm_fb_init(nv_object(pfb));                                      \
})
#define nvkm_fb_fini(p,s) ({                                                \
	struct nvkm_fb *pfb = (p);                                          \
	_nvkm_fb_fini(nv_object(pfb), (s));                                 \
})

int nvkm_fb_create_(struct nvkm_object *, struct nvkm_object *,
		       struct nvkm_oclass *, int, void **);
void _nvkm_fb_dtor(struct nvkm_object *);
int  _nvkm_fb_init(struct nvkm_object *);
int  _nvkm_fb_fini(struct nvkm_object *, bool);

struct nvkm_fb_impl {
	struct nvkm_oclass base;
	struct nvkm_oclass *ram;
	bool (*memtype)(struct nvkm_fb *, u32);
};

bool nv04_fb_memtype_valid(struct nvkm_fb *, u32 memtype);
bool nv50_fb_memtype_valid(struct nvkm_fb *, u32 memtype);

int  nvkm_fb_bios_memtype(struct nvkm_bios *);
#endif
