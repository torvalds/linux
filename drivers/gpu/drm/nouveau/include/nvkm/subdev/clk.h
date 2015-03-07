#ifndef __NVKM_CLK_H__
#define __NVKM_CLK_H__
#include <core/subdev.h>
#include <core/notify.h>
struct nvbios_pll;
struct nvkm_pll_vals;

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
	nv_clk_src_core_intm,
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

struct nvkm_cstate {
	struct list_head head;
	u8  voltage;
	u32 domain[nv_clk_src_max];
};

struct nvkm_pstate {
	struct list_head head;
	struct list_head list; /* c-states */
	struct nvkm_cstate base;
	u8 pstate;
	u8 fanspeed;
};

struct nvkm_domain {
	enum nv_clk_src name;
	u8 bios; /* 0xff for none */
#define NVKM_CLK_DOM_FLAG_CORE 0x01
	u8 flags;
	const char *mname;
	int mdiv;
};

struct nvkm_clk {
	struct nvkm_subdev base;

	struct nvkm_domain *domains;
	struct nvkm_pstate bstate;

	struct list_head states;
	int state_nr;

	struct work_struct work;
	wait_queue_head_t wait;
	atomic_t waiting;

	struct nvkm_notify pwrsrc_ntfy;
	int pwrsrc;
	int pstate; /* current */
	int ustate_ac; /* user-requested (-1 disabled, -2 perfmon) */
	int ustate_dc; /* user-requested (-1 disabled, -2 perfmon) */
	int astate; /* perfmon adjustment (base) */
	int tstate; /* thermal adjustment (max-) */
	int dstate; /* display adjustment (min+) */

	bool allow_reclock;

	int  (*read)(struct nvkm_clk *, enum nv_clk_src);
	int  (*calc)(struct nvkm_clk *, struct nvkm_cstate *);
	int  (*prog)(struct nvkm_clk *);
	void (*tidy)(struct nvkm_clk *);

	/*XXX: die, these are here *only* to support the completely
	 *     bat-shit insane what-was-nvkm_hw.c code
	 */
	int (*pll_calc)(struct nvkm_clk *, struct nvbios_pll *, int clk,
			struct nvkm_pll_vals *pv);
	int (*pll_prog)(struct nvkm_clk *, u32 reg1, struct nvkm_pll_vals *pv);
};

static inline struct nvkm_clk *
nvkm_clk(void *obj)
{
	return (void *)nvkm_subdev(obj, NVDEV_SUBDEV_CLK);
}

#define nvkm_clk_create(p,e,o,i,r,s,n,d)                                  \
	nvkm_clk_create_((p), (e), (o), (i), (r), (s), (n), sizeof(**d),  \
			      (void **)d)
#define nvkm_clk_destroy(p) ({                                            \
	struct nvkm_clk *clk = (p);                                       \
	_nvkm_clk_dtor(nv_object(clk));                                   \
})
#define nvkm_clk_init(p) ({                                               \
	struct nvkm_clk *clk = (p);                                       \
	_nvkm_clk_init(nv_object(clk));                                   \
})
#define nvkm_clk_fini(p,s) ({                                             \
	struct nvkm_clk *clk = (p);                                       \
	_nvkm_clk_fini(nv_object(clk), (s));                              \
})

int  nvkm_clk_create_(struct nvkm_object *, struct nvkm_object *,
			   struct nvkm_oclass *,
			   struct nvkm_domain *, struct nvkm_pstate *,
			   int, bool, int, void **);
void _nvkm_clk_dtor(struct nvkm_object *);
int  _nvkm_clk_init(struct nvkm_object *);
int  _nvkm_clk_fini(struct nvkm_object *, bool);

extern struct nvkm_oclass nv04_clk_oclass;
extern struct nvkm_oclass nv40_clk_oclass;
extern struct nvkm_oclass *nv50_clk_oclass;
extern struct nvkm_oclass *g84_clk_oclass;
extern struct nvkm_oclass *mcp77_clk_oclass;
extern struct nvkm_oclass gt215_clk_oclass;
extern struct nvkm_oclass gf100_clk_oclass;
extern struct nvkm_oclass gk104_clk_oclass;
extern struct nvkm_oclass gk20a_clk_oclass;

int nv04_clk_pll_set(struct nvkm_clk *, u32 type, u32 freq);
int nv04_clk_pll_calc(struct nvkm_clk *, struct nvbios_pll *, int clk,
		      struct nvkm_pll_vals *);
int nv04_clk_pll_prog(struct nvkm_clk *, u32 reg1, struct nvkm_pll_vals *);
int gt215_clk_pll_calc(struct nvkm_clk *, struct nvbios_pll *,
		       int clk, struct nvkm_pll_vals *);

int nvkm_clk_ustate(struct nvkm_clk *, int req, int pwr);
int nvkm_clk_astate(struct nvkm_clk *, int req, int rel, bool wait);
int nvkm_clk_dstate(struct nvkm_clk *, int req, int rel);
int nvkm_clk_tstate(struct nvkm_clk *, int req, int rel);
#endif
