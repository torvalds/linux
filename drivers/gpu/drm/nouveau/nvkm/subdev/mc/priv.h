#ifndef __NVKM_MC_PRIV_H__
#define __NVKM_MC_PRIV_H__
#include <subdev/mc.h>

#define nvkm_mc_create(p,e,o,d)                                             \
	nvkm_mc_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nvkm_mc_destroy(p) ({                                               \
	struct nvkm_mc *pmc = (p); _nvkm_mc_dtor(nv_object(pmc));        \
})
#define nvkm_mc_init(p) ({                                                  \
	struct nvkm_mc *pmc = (p); _nvkm_mc_init(nv_object(pmc));        \
})
#define nvkm_mc_fini(p,s) ({                                                \
	struct nvkm_mc *pmc = (p); _nvkm_mc_fini(nv_object(pmc), (s));   \
})

int  nvkm_mc_create_(struct nvkm_object *, struct nvkm_object *,
			struct nvkm_oclass *, int, void **);
void _nvkm_mc_dtor(struct nvkm_object *);
int  _nvkm_mc_init(struct nvkm_object *);
int  _nvkm_mc_fini(struct nvkm_object *, bool);

struct nvkm_mc_intr {
	u32 stat;
	u32 unit;
};

struct nvkm_mc_oclass {
	struct nvkm_oclass base;
	const struct nvkm_mc_intr *intr;
	void (*msi_rearm)(struct nvkm_mc *);
	void (*unk260)(struct nvkm_mc *, u32);
};

void gf100_mc_unk260(struct nvkm_mc *, u32);
#endif
