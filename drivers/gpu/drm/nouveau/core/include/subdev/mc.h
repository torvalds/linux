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
	bool use_msi;
	unsigned int irq;
};

static inline struct nouveau_mc *
nouveau_mc(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_SUBDEV_MC];
}

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

struct nouveau_mc_oclass {
	struct nouveau_oclass base;
	const struct nouveau_mc_intr *intr;
	void (*msi_rearm)(struct nouveau_mc *);
};

extern struct nouveau_oclass *nv04_mc_oclass;
extern struct nouveau_oclass *nv40_mc_oclass;
extern struct nouveau_oclass *nv44_mc_oclass;
extern struct nouveau_oclass *nv4c_mc_oclass;
extern struct nouveau_oclass *nv50_mc_oclass;
extern struct nouveau_oclass *nv94_mc_oclass;
extern struct nouveau_oclass *nv98_mc_oclass;
extern struct nouveau_oclass *nvc0_mc_oclass;
extern struct nouveau_oclass *nvc3_mc_oclass;

#endif
