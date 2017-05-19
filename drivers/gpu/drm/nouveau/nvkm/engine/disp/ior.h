#ifndef __NVKM_DISP_IOR_H__
#define __NVKM_DISP_IOR_H__
#include "priv.h"

struct nvkm_ior {
	const struct nvkm_ior_func *func;
	struct nvkm_disp *disp;
	enum nvkm_ior_type {
		DAC,
		SOR,
		PIOR,
	} type;
	int id;
	char name[8];

	struct list_head head;

	struct nvkm_ior_state {
		enum nvkm_ior_proto {
			CRT,
			TMDS,
			LVDS,
			DP,
			UNKNOWN
		} proto:3;
	} arm, asy;

	/* Armed DP state. */
	struct {
		bool mst;
		bool ef;
		u8 nr;
		u8 bw;
	} dp;
};

struct nvkm_ior_func {
};

int nvkm_ior_new_(const struct nvkm_ior_func *func, struct nvkm_disp *,
		  enum nvkm_ior_type type, int id);
void nvkm_ior_del(struct nvkm_ior **);
struct nvkm_ior *nvkm_ior_find(struct nvkm_disp *, enum nvkm_ior_type, int id);

#define IOR_MSG(i,l,f,a...) do {                                               \
	struct nvkm_ior *_ior = (i);                                           \
	nvkm_##l(&_ior->disp->engine.subdev, "%s: "f, _ior->name, ##a);        \
} while(0)
#define IOR_WARN(i,f,a...) IOR_MSG((i), warn, f, ##a)
#define IOR_DBG(i,f,a...) IOR_MSG((i), debug, f, ##a)

int nv50_dac_new(struct nvkm_disp *, int);
int gf119_dac_new(struct nvkm_disp *, int);

int nv50_pior_new(struct nvkm_disp *, int);

int nv50_sor_new(struct nvkm_disp *, int);
int g84_sor_new(struct nvkm_disp *, int);
int g94_sor_new(struct nvkm_disp *, int);
int mcp77_sor_new(struct nvkm_disp *, int);
int gt215_sor_new(struct nvkm_disp *, int);
int mcp89_sor_new(struct nvkm_disp *, int);
int gf119_sor_new(struct nvkm_disp *, int);
int gk104_sor_new(struct nvkm_disp *, int);
int gm107_sor_new(struct nvkm_disp *, int);
int gm200_sor_new(struct nvkm_disp *, int);
#endif
