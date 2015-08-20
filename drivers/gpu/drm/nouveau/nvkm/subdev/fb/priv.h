#ifndef __NVKM_FB_PRIV_H__
#define __NVKM_FB_PRIV_H__
#include <subdev/fb.h>
struct nvkm_bios;

#define nvkm_fb_create(p,e,c,d)                                             \
	nvkm_fb_create_((p), (e), (c), sizeof(**d), (void **)d)
#define nvkm_fb_destroy(p) ({                                               \
	struct nvkm_fb *_fb = (p);                                          \
	_nvkm_fb_dtor(nv_object(_fb));                                      \
})
#define nvkm_fb_init(p) ({                                                  \
	struct nvkm_fb *_fb = (p);                                          \
	_nvkm_fb_init(nv_object(_fb));                                      \
})
#define nvkm_fb_fini(p,s) ({                                                \
	struct nvkm_fb *_fb = (p);                                          \
	_nvkm_fb_fini(nv_object(_fb), (s));                                 \
})

int nvkm_fb_create_(struct nvkm_object *, struct nvkm_object *,
		       struct nvkm_oclass *, int, void **);
void _nvkm_fb_dtor(struct nvkm_object *);
int  _nvkm_fb_init(struct nvkm_object *);
int  _nvkm_fb_fini(struct nvkm_object *, bool);

struct nvkm_fb_impl {
	struct nvkm_oclass base;
	int (*ram_new)(struct nvkm_fb *, struct nvkm_ram **);
	bool (*memtype)(struct nvkm_fb *, u32);
};

bool nv04_fb_memtype_valid(struct nvkm_fb *, u32 memtype);
bool nv50_fb_memtype_valid(struct nvkm_fb *, u32 memtype);

int  nvkm_fb_bios_memtype(struct nvkm_bios *);
#endif
