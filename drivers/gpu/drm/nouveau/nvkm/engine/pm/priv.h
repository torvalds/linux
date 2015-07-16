#ifndef __NVKM_PM_PRIV_H__
#define __NVKM_PM_PRIV_H__
#include <engine/pm.h>

struct nvkm_perfctr {
	struct nvkm_object base;
	struct list_head head;
	struct nvkm_perfsig *signal[4];
	int slot;
	u32 logic_op;
	u32 clk;
	u32 ctr;
};

extern struct nvkm_oclass nvkm_pm_sclass[];

#include <core/engctx.h>

struct nvkm_perfctx {
	struct nvkm_engctx base;
};

extern struct nvkm_oclass nvkm_pm_cclass;

struct nvkm_specsig {
	u8 signal;
	const char *name;
};

struct nvkm_perfsig {
	const char *name;
};

struct nvkm_perfdom;
struct nvkm_perfctr *
nvkm_perfsig_wrap(struct nvkm_pm *, const char *, struct nvkm_perfdom **);

struct nvkm_specdom {
	u16 signal_nr;
	const struct nvkm_specsig *signal;
	const struct nvkm_funcdom *func;
};

extern const struct nvkm_specdom gt215_pm_pwr[];
extern const struct nvkm_specdom gf100_pm_pwr[];
extern const struct nvkm_specdom gk104_pm_pwr[];

struct nvkm_perfdom {
	struct list_head head;
	struct list_head list;
	const struct nvkm_funcdom *func;
	char name[32];
	u32 addr;
	u8  quad;
	u32 signal_nr;
	struct nvkm_perfsig signal[];
};

struct nvkm_funcdom {
	void (*init)(struct nvkm_pm *, struct nvkm_perfdom *,
		     struct nvkm_perfctr *);
	void (*read)(struct nvkm_pm *, struct nvkm_perfdom *,
		     struct nvkm_perfctr *);
	void (*next)(struct nvkm_pm *, struct nvkm_perfdom *);
};

int nvkm_perfdom_new(struct nvkm_pm *, const char *, u32, u32, u32, u32,
		     const struct nvkm_specdom *);

#define nvkm_pm_create(p,e,o,d)                                        \
	nvkm_pm_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nvkm_pm_dtor(p) ({                                             \
	struct nvkm_pm *c = (p);                                       \
	_nvkm_pm_dtor(nv_object(c));                                   \
})
#define nvkm_pm_init(p) ({                                             \
	struct nvkm_pm *c = (p);                                       \
	_nvkm_pm_init(nv_object(c));                                   \
})
#define nvkm_pm_fini(p,s) ({                                           \
	struct nvkm_pm *c = (p);                                       \
	_nvkm_pm_fini(nv_object(c), (s));                              \
})

int nvkm_pm_create_(struct nvkm_object *, struct nvkm_object *,
			    struct nvkm_oclass *, int, void **);
void _nvkm_pm_dtor(struct nvkm_object *);
int  _nvkm_pm_init(struct nvkm_object *);
int  _nvkm_pm_fini(struct nvkm_object *, bool);
#endif
