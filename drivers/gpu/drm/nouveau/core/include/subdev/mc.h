#ifndef __NOUVEAU_MC_H__
#define __NOUVEAU_MC_H__

#include <core/subdev.h>
#include <core/device.h>

struct nouveau_mc_intr {
	u32 stat;
	u32 unit;
};

struct nouveau_mc {
	struct nouveau_subdev base;
	const struct nouveau_mc_intr *intr_map;
	bool use_msi;
};

static inline struct nouveau_mc *
nouveau_mc(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_SUBDEV_MC];
}

#define nouveau_mc_create(p,e,o,m,d)                                           \
	nouveau_mc_create_((p), (e), (o), (m), sizeof(**d), (void **)d)
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
			struct nouveau_oclass *, const struct nouveau_mc_intr *,
			int, void **);
void _nouveau_mc_dtor(struct nouveau_object *);
int  _nouveau_mc_init(struct nouveau_object *);
int  _nouveau_mc_fini(struct nouveau_object *, bool);

extern struct nouveau_oclass nv04_mc_oclass;
extern struct nouveau_oclass nv44_mc_oclass;
extern struct nouveau_oclass nv50_mc_oclass;
extern struct nouveau_oclass nv98_mc_oclass;
extern struct nouveau_oclass nvc0_mc_oclass;

extern const struct nouveau_mc_intr nv04_mc_intr[];
int nv04_mc_init(struct nouveau_object *);
int nv50_mc_init(struct nouveau_object *);

#endif
