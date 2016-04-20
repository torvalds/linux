#ifndef __NV50_DISP_ROOT_H__
#define __NV50_DISP_ROOT_H__
#define nv50_disp_root(p) container_of((p), struct nv50_disp_root, object)
#include "nv50.h"
#include "channv50.h"
#include "dmacnv50.h"

struct nv50_disp_root {
	const struct nv50_disp_root_func *func;
	struct nv50_disp *disp;
	struct nvkm_object object;

	struct nvkm_gpuobj *instmem;
	struct nvkm_ramht *ramht;
};

struct nv50_disp_root_func {
	int (*init)(struct nv50_disp_root *);
	void (*fini)(struct nv50_disp_root *);
	const struct nv50_disp_dmac_oclass *dmac[3];
	const struct nv50_disp_pioc_oclass *pioc[2];
};

int  nv50_disp_root_new_(const struct nv50_disp_root_func *, struct nvkm_disp *,
			 const struct nvkm_oclass *, void *data, u32 size,
			 struct nvkm_object **);
int  nv50_disp_root_init(struct nv50_disp_root *);
void nv50_disp_root_fini(struct nv50_disp_root *);

int  gf119_disp_root_init(struct nv50_disp_root *);
void gf119_disp_root_fini(struct nv50_disp_root *);

extern const struct nvkm_disp_oclass nv50_disp_root_oclass;
extern const struct nvkm_disp_oclass g84_disp_root_oclass;
extern const struct nvkm_disp_oclass g94_disp_root_oclass;
extern const struct nvkm_disp_oclass gt200_disp_root_oclass;
extern const struct nvkm_disp_oclass gt215_disp_root_oclass;
extern const struct nvkm_disp_oclass gf119_disp_root_oclass;
extern const struct nvkm_disp_oclass gk104_disp_root_oclass;
extern const struct nvkm_disp_oclass gk110_disp_root_oclass;
extern const struct nvkm_disp_oclass gm107_disp_root_oclass;
extern const struct nvkm_disp_oclass gm200_disp_root_oclass;
#endif
