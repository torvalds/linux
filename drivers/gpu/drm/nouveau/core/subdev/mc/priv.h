#ifndef __NVKM_MC_PRIV_H__
#define __NVKM_MC_PRIV_H__

#include <subdev/mc.h>

#define nouveau_mc_create(p,e,o,d)                                             \
	nouveau_mc_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nouveau_mc_destroy(p) ({                                               \
	struct nouveau_mc *pmc = (p); _nouveau_mc_dtor(nv_object(pmc));        \
})
#define nouveau_mc_init(p) ({                                                  \
	struct nouveau_mc *pmc = (p); _nouveau_mc_init(nv_object(pmc));        \
})
#define nouveau_mc_fini(p,s) ({                                                \
	struct nouveau_mc *pmc = (p); _nouveau_mc_fini(nv_object(pmc), (s));   \
})

int  nouveau_mc_create_(struct nouveau_object *, struct nouveau_object *,
			struct nouveau_oclass *, int, void **);
void _nouveau_mc_dtor(struct nouveau_object *);
int  _nouveau_mc_init(struct nouveau_object *);
int  _nouveau_mc_fini(struct nouveau_object *, bool);

struct nouveau_mc_intr {
	u32 stat;
	u32 unit;
};

struct nouveau_mc_oclass {
	struct nouveau_oclass base;
	const struct nouveau_mc_intr *intr;
	void (*msi_rearm)(struct nouveau_mc *);
	void (*unk260)(struct nouveau_mc *, u32);
};

void nvc0_mc_unk260(struct nouveau_mc *, u32);

#endif
