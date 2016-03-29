#ifndef __NV50_DISP_DMAC_H__
#define __NV50_DISP_DMAC_H__
#define nv50_disp_dmac(p) container_of((p), struct nv50_disp_dmac, base)
#include "channv50.h"

struct nv50_disp_dmac {
	const struct nv50_disp_dmac_func *func;
	struct nv50_disp_chan base;
	u32 push;
};

struct nv50_disp_dmac_func {
	int  (*init)(struct nv50_disp_dmac *);
	void (*fini)(struct nv50_disp_dmac *);
	int  (*bind)(struct nv50_disp_dmac *, struct nvkm_object *, u32 handle);
};

int nv50_disp_dmac_new_(const struct nv50_disp_dmac_func *,
			const struct nv50_disp_chan_mthd *,
			struct nv50_disp_root *, int chid, int head, u64 push,
			const struct nvkm_oclass *, struct nvkm_object **);

extern const struct nv50_disp_dmac_func nv50_disp_dmac_func;
int nv50_disp_dmac_bind(struct nv50_disp_dmac *, struct nvkm_object *, u32);
extern const struct nv50_disp_dmac_func nv50_disp_core_func;

extern const struct nv50_disp_dmac_func gf119_disp_dmac_func;
int gf119_disp_dmac_bind(struct nv50_disp_dmac *, struct nvkm_object *, u32);
extern const struct nv50_disp_dmac_func gf119_disp_core_func;

struct nv50_disp_dmac_oclass {
	int (*ctor)(const struct nv50_disp_dmac_func *,
		    const struct nv50_disp_chan_mthd *,
		    struct nv50_disp_root *, int chid,
		    const struct nvkm_oclass *, void *data, u32 size,
		    struct nvkm_object **);
	struct nvkm_sclass base;
	const struct nv50_disp_dmac_func *func;
	const struct nv50_disp_chan_mthd *mthd;
	int chid;
};

int nv50_disp_core_new(const struct nv50_disp_dmac_func *,
		       const struct nv50_disp_chan_mthd *,
		       struct nv50_disp_root *, int chid,
		       const struct nvkm_oclass *oclass, void *data, u32 size,
		       struct nvkm_object **);
int nv50_disp_base_new(const struct nv50_disp_dmac_func *,
		       const struct nv50_disp_chan_mthd *,
		       struct nv50_disp_root *, int chid,
		       const struct nvkm_oclass *oclass, void *data, u32 size,
		       struct nvkm_object **);
int nv50_disp_ovly_new(const struct nv50_disp_dmac_func *,
		       const struct nv50_disp_chan_mthd *,
		       struct nv50_disp_root *, int chid,
		       const struct nvkm_oclass *oclass, void *data, u32 size,
		       struct nvkm_object **);

extern const struct nv50_disp_dmac_oclass nv50_disp_core_oclass;
extern const struct nv50_disp_dmac_oclass nv50_disp_base_oclass;
extern const struct nv50_disp_dmac_oclass nv50_disp_ovly_oclass;

extern const struct nv50_disp_dmac_oclass g84_disp_core_oclass;
extern const struct nv50_disp_dmac_oclass g84_disp_base_oclass;
extern const struct nv50_disp_dmac_oclass g84_disp_ovly_oclass;

extern const struct nv50_disp_dmac_oclass g94_disp_core_oclass;

extern const struct nv50_disp_dmac_oclass gt200_disp_core_oclass;
extern const struct nv50_disp_dmac_oclass gt200_disp_base_oclass;
extern const struct nv50_disp_dmac_oclass gt200_disp_ovly_oclass;

extern const struct nv50_disp_dmac_oclass gt215_disp_core_oclass;
extern const struct nv50_disp_dmac_oclass gt215_disp_base_oclass;
extern const struct nv50_disp_dmac_oclass gt215_disp_ovly_oclass;

extern const struct nv50_disp_dmac_oclass gf119_disp_core_oclass;
extern const struct nv50_disp_dmac_oclass gf119_disp_base_oclass;
extern const struct nv50_disp_dmac_oclass gf119_disp_ovly_oclass;

extern const struct nv50_disp_dmac_oclass gk104_disp_core_oclass;
extern const struct nv50_disp_dmac_oclass gk104_disp_base_oclass;
extern const struct nv50_disp_dmac_oclass gk104_disp_ovly_oclass;

extern const struct nv50_disp_dmac_oclass gk110_disp_core_oclass;
extern const struct nv50_disp_dmac_oclass gk110_disp_base_oclass;

extern const struct nv50_disp_dmac_oclass gm107_disp_core_oclass;

extern const struct nv50_disp_dmac_oclass gm200_disp_core_oclass;
#endif
