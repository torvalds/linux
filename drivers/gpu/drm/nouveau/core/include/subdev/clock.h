#ifndef __NOUVEAU_CLOCK_H__
#define __NOUVEAU_CLOCK_H__

#include <core/device.h>
#include <core/subdev.h>

struct nouveau_pll_vals;
struct nvbios_pll;

struct nouveau_clock {
	struct nouveau_subdev base;

	/*XXX: die, these are here *only* to support the completely
	 *     bat-shit insane what-was-nouveau_hw.c code
	 */
	int (*pll_calc)(struct nouveau_clock *, struct nvbios_pll *,
			int clk, struct nouveau_pll_vals *pv);
	int (*pll_prog)(struct nouveau_clock *, u32 reg1,
			struct nouveau_pll_vals *pv);
};

static inline struct nouveau_clock *
nouveau_clock(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_SUBDEV_CLOCK];
}

#define nouveau_clock_create(p,e,o,d)                                          \
	nouveau_subdev_create((p), (e), (o), 0, "CLOCK", "clock", d)
#define nouveau_clock_destroy(p)                                               \
	nouveau_subdev_destroy(&(p)->base)
#define nouveau_clock_init(p)                                                  \
	nouveau_subdev_init(&(p)->base)
#define nouveau_clock_fini(p,s)                                                \
	nouveau_subdev_fini(&(p)->base, (s))

int  nouveau_clock_create_(struct nouveau_object *, struct nouveau_object *,
			   struct nouveau_oclass *, void *, u32, int, void **);

#define _nouveau_clock_dtor _nouveau_subdev_dtor
#define _nouveau_clock_init _nouveau_subdev_init
#define _nouveau_clock_fini _nouveau_subdev_fini

extern struct nouveau_oclass nv04_clock_oclass;
extern struct nouveau_oclass nv40_clock_oclass;
extern struct nouveau_oclass nv50_clock_oclass;
extern struct nouveau_oclass nva3_clock_oclass;
extern struct nouveau_oclass nvc0_clock_oclass;

int nv04_clock_pll_set(struct nouveau_clock *, u32 type, u32 freq);
int nv04_clock_pll_calc(struct nouveau_clock *, struct nvbios_pll *,
			int clk, struct nouveau_pll_vals *);
int nv04_clock_pll_prog(struct nouveau_clock *, u32 reg1,
			struct nouveau_pll_vals *);
int nva3_clock_pll_calc(struct nouveau_clock *, struct nvbios_pll *,
			int clk, struct nouveau_pll_vals *);

#endif
