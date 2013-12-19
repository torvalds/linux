#ifndef __NOUVEAU_CLOCK_H__
#define __NOUVEAU_CLOCK_H__

#include <core/device.h>
#include <core/subdev.h>

struct nouveau_pll_vals;
struct nvbios_pll;

enum nv_clk_src {
	nv_clk_src_crystal,
	nv_clk_src_href,

	nv_clk_src_hclk,
	nv_clk_src_hclkm3,
	nv_clk_src_hclkm3d2,
	nv_clk_src_hclkm2d3, /* NVAA */
	nv_clk_src_hclkm4, /* NVAA */
	nv_clk_src_cclk, /* NVAA */

	nv_clk_src_host,

	nv_clk_src_sppll0,
	nv_clk_src_sppll1,

	nv_clk_src_mpllsrcref,
	nv_clk_src_mpllsrc,
	nv_clk_src_mpll,
	nv_clk_src_mdiv,

	nv_clk_src_core,
	nv_clk_src_shader,

	nv_clk_src_mem,

	nv_clk_src_gpc,
	nv_clk_src_rop,
	nv_clk_src_hubk01,
	nv_clk_src_hubk06,
	nv_clk_src_hubk07,
	nv_clk_src_copy,
	nv_clk_src_daemon,
	nv_clk_src_disp,
	nv_clk_src_vdec,

	nv_clk_src_dom6,

	nv_clk_src_max,
};

struct nouveau_cstate {
	struct list_head head;
	u8  voltage;
	u32 domain[nv_clk_src_max];
};

struct nouveau_pstate {
	struct list_head head;
	struct list_head list; /* c-states */
	struct nouveau_cstate base;
	u8 pstate;
	u8 fanspeed;
};

struct nouveau_clock {
	struct nouveau_subdev base;

	struct nouveau_clocks *domains;
	struct nouveau_pstate bstate;

	struct list_head states;
	int state_nr;

	int pstate; /* current */
	int ustate; /* user-requested (-1 disabled, -2 perfmon) */
	int astate; /* perfmon adjustment (base) */
	int tstate; /* thermal adjustment (max-) */
	int dstate; /* display adjustment (min+) */

	int  (*read)(struct nouveau_clock *, enum nv_clk_src);
	int  (*calc)(struct nouveau_clock *, struct nouveau_cstate *);
	int  (*prog)(struct nouveau_clock *);
	void (*tidy)(struct nouveau_clock *);

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

struct nouveau_clocks {
	enum nv_clk_src name;
	u8 bios; /* 0xff for none */
#define NVKM_CLK_DOM_FLAG_CORE 0x01
	u8 flags;
	const char *mname;
	int mdiv;
};

#define nouveau_clock_create(p,e,o,i,d)                                        \
	nouveau_clock_create_((p), (e), (o), (i), sizeof(**d), (void **)d)
#define nouveau_clock_destroy(p) ({                                            \
	struct nouveau_clock *clk = (p);                                       \
	_nouveau_clock_dtor(nv_object(clk));                                   \
})
#define nouveau_clock_init(p) ({                                               \
	struct nouveau_clock *clk = (p);                                       \
	_nouveau_clock_init(nv_object(clk));                                   \
})
#define nouveau_clock_fini(p,s)                                                \
	nouveau_subdev_fini(&(p)->base, (s))

int  nouveau_clock_create_(struct nouveau_object *, struct nouveau_object *,
			   struct nouveau_oclass *,
			   struct nouveau_clocks *, int, void **);
void _nouveau_clock_dtor(struct nouveau_object *);
int _nouveau_clock_init(struct nouveau_object *);
#define _nouveau_clock_fini _nouveau_subdev_fini

extern struct nouveau_oclass nv04_clock_oclass;
extern struct nouveau_oclass nv40_clock_oclass;
extern struct nouveau_oclass *nv50_clock_oclass;
extern struct nouveau_oclass *nv84_clock_oclass;
extern struct nouveau_oclass *nvaa_clock_oclass;
extern struct nouveau_oclass nva3_clock_oclass;
extern struct nouveau_oclass nvc0_clock_oclass;
extern struct nouveau_oclass nve0_clock_oclass;

int nv04_clock_pll_set(struct nouveau_clock *, u32 type, u32 freq);
int nv04_clock_pll_calc(struct nouveau_clock *, struct nvbios_pll *,
			int clk, struct nouveau_pll_vals *);
int nv04_clock_pll_prog(struct nouveau_clock *, u32 reg1,
			struct nouveau_pll_vals *);
int nva3_clock_pll_calc(struct nouveau_clock *, struct nvbios_pll *,
			int clk, struct nouveau_pll_vals *);

int nouveau_clock_ustate(struct nouveau_clock *, int req);
int nouveau_clock_astate(struct nouveau_clock *, int req, int rel);
int nouveau_clock_dstate(struct nouveau_clock *, int req, int rel);
int nouveau_clock_tstate(struct nouveau_clock *, int req, int rel);

#endif
