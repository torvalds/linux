#ifndef __NV50_DISP_DMAC_H__
#define __NV50_DISP_DMAC_H__
#include "channv50.h"

struct nv50_disp_dmac {
	struct nv50_disp_chan base;
	u32 push;
};

void nv50_disp_dmac_dtor(struct nvkm_object *);
int  nv50_disp_dmac_object_attach(struct nvkm_object *,
				  struct nvkm_object *, u32);
void nv50_disp_dmac_object_detach(struct nvkm_object *, int);
int  nv50_disp_dmac_create_(struct nvkm_object *, struct nvkm_object *,
			    struct nvkm_oclass *, u64, int, int, void **);
int  nv50_disp_dmac_init(struct nvkm_object *);
int  nv50_disp_dmac_fini(struct nvkm_object *, bool);

int  gf119_disp_dmac_object_attach(struct nvkm_object *,
				   struct nvkm_object *, u32);
void gf119_disp_dmac_object_detach(struct nvkm_object *, int);
int  gf119_disp_dmac_init(struct nvkm_object *);
int  gf119_disp_dmac_fini(struct nvkm_object *, bool);
#endif
