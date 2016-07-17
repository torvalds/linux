#ifndef __NVKM_CLK_H__
#define __NVKM_CLK_H__
#include <core/subdev.h>
#include <core/notify.h>
#include <subdev/pci.h>
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
	nv_clk_src_pmu,
	nv_clk_src_disp,
	nv_clk_src_vdec,

	nv_clk_src_dom6,

	nv_clk_src_max,
};

struct nvkm_cstate {
	struct list_head head;
	u8  voltage;
	u32 domain[nv_clk_src_max];
	u8  id;
};

struct nvkm_pstate {
	struct list_head head;
	struct list_head list; /* c-states */
	struct nvkm_cstate base;
	u8 pstate;
	u8 fanspeed;
	enum nvkm_pcie_speed pcie_speed;
	u8 pcie_width;
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
	const struct nvkm_clk_func *func;
	struct nvkm_subdev subdev;

	const struct nvkm_domain *domains;
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
	int dstate; /* display adjustment (min+) */
	u8  temp;

	bool allow_reclock;

	/*XXX: die, these are here *only* to support the completely
	 *     bat-shit insane what-was-nouveau_hw.c code
	 */
	int (*pll_calc)(struct nvkm_clk *, struct nvbios_pll *, int clk,
			struct nvkm_pll_vals *pv);
	int (*pll_prog)(struct nvkm_clk *, u32 reg1, struct nvkm_pll_vals *pv);
};

int nvkm_clk_read(struct nvkm_clk *, enum nv_clk_src);
int nvkm_clk_ustate(struct nvkm_clk *, int req, int pwr);
int nvkm_clk_astate(struct nvkm_clk *, int req, int rel, bool wait);
int nvkm_clk_dstate(struct nvkm_clk *, int req, int rel);
int nvkm_clk_tstate(struct nvkm_clk *, u8 temperature);

int nv04_clk_new(struct nvkm_device *, int, struct nvkm_clk **);
int nv40_clk_new(struct nvkm_device *, int, struct nvkm_clk **);
int nv50_clk_new(struct nvkm_device *, int, struct nvkm_clk **);
int g84_clk_new(struct nvkm_device *, int, struct nvkm_clk **);
int mcp77_clk_new(struct nvkm_device *, int, struct nvkm_clk **);
int gt215_clk_new(struct nvkm_device *, int, struct nvkm_clk **);
int gf100_clk_new(struct nvkm_device *, int, struct nvkm_clk **);
int gk104_clk_new(struct nvkm_device *, int, struct nvkm_clk **);
int gk20a_clk_new(struct nvkm_device *, int, struct nvkm_clk **);
int gm20b_clk_new(struct nvkm_device *, int, struct nvkm_clk **);
#endif
