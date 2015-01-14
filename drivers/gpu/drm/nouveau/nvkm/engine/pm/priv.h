#ifndef __NVKM_PM_PRIV_H__
#define __NVKM_PM_PRIV_H__

#include <engine/pm.h>

struct nouveau_perfctr {
	struct nouveau_object base;
	struct list_head head;
	struct nouveau_perfsig *signal[4];
	int slot;
	u32 logic_op;
	u32 clk;
	u32 ctr;
};

extern struct nouveau_oclass nouveau_pm_sclass[];

struct nouveau_perfctx {
	struct nouveau_engctx base;
};

extern struct nouveau_oclass nouveau_pm_cclass;

struct nouveau_specsig {
	u8 signal;
	const char *name;
};

struct nouveau_perfsig {
	const char *name;
};

struct nouveau_perfdom;
struct nouveau_perfctr *
nouveau_perfsig_wrap(struct nouveau_pm *, const char *,
		     struct nouveau_perfdom **);

struct nouveau_specdom {
	u16 signal_nr;
	const struct nouveau_specsig *signal;
	const struct nouveau_funcdom *func;
};

extern const struct nouveau_specdom nva3_pm_pwr[];
extern const struct nouveau_specdom nvc0_pm_pwr[];
extern const struct nouveau_specdom nve0_pm_pwr[];

struct nouveau_perfdom {
	struct list_head head;
	struct list_head list;
	const struct nouveau_funcdom *func;
	char name[32];
	u32 addr;
	u8  quad;
	u32 signal_nr;
	struct nouveau_perfsig signal[];
};

struct nouveau_funcdom {
	void (*init)(struct nouveau_pm *, struct nouveau_perfdom *,
		     struct nouveau_perfctr *);
	void (*read)(struct nouveau_pm *, struct nouveau_perfdom *,
		     struct nouveau_perfctr *);
	void (*next)(struct nouveau_pm *, struct nouveau_perfdom *);
};

int nouveau_perfdom_new(struct nouveau_pm *, const char *, u32,
			u32, u32, u32, const struct nouveau_specdom *);

#define nouveau_pm_create(p,e,o,d)                                        \
	nouveau_pm_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nouveau_pm_dtor(p) ({                                             \
	struct nouveau_pm *c = (p);                                       \
	_nouveau_pm_dtor(nv_object(c));                                   \
})
#define nouveau_pm_init(p) ({                                             \
	struct nouveau_pm *c = (p);                                       \
	_nouveau_pm_init(nv_object(c));                                   \
})
#define nouveau_pm_fini(p,s) ({                                           \
	struct nouveau_pm *c = (p);                                       \
	_nouveau_pm_fini(nv_object(c), (s));                              \
})

int nouveau_pm_create_(struct nouveau_object *, struct nouveau_object *,
			    struct nouveau_oclass *, int, void **);
void _nouveau_pm_dtor(struct nouveau_object *);
int  _nouveau_pm_init(struct nouveau_object *);
int  _nouveau_pm_fini(struct nouveau_object *, bool);

#endif
