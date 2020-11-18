/* SPDX-License-Identifier: MIT */
#ifndef __NV50_DISP_ROOT_H__
#define __NV50_DISP_ROOT_H__
#define nv50_disp_root(p) container_of((p), struct nv50_disp_root, object)
#include <core/object.h>
#include "nv50.h"

struct nv50_disp_root {
	const struct nv50_disp_root_func *func;
	struct nv50_disp *disp;
	struct nvkm_object object;
};

struct nv50_disp_root_func {
	int blah;
	struct nv50_disp_user {
		struct nvkm_sclass base;
		int (*ctor)(const struct nvkm_oclass *, void *argv, u32 argc,
			    struct nv50_disp *, struct nvkm_object **);
	} user[];
};

int  nv50_disp_root_new_(const struct nv50_disp_root_func *, struct nvkm_disp *,
			 const struct nvkm_oclass *, void *data, u32 size,
			 struct nvkm_object **);

int gv100_disp_caps_new(const struct nvkm_oclass *, void *, u32,
			struct nv50_disp *, struct nvkm_object **);

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
extern const struct nvkm_disp_oclass gp100_disp_root_oclass;
extern const struct nvkm_disp_oclass gp102_disp_root_oclass;
extern const struct nvkm_disp_oclass gv100_disp_root_oclass;
extern const struct nvkm_disp_oclass tu102_disp_root_oclass;
#endif
