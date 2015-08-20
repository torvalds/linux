#ifndef __NV50_DISP_ROOT_H__
#define __NV50_DISP_ROOT_H__
#include "nv50.h"
#include "channv50.h"
#include "dmacnv50.h"
#include <core/parent.h>

#include <nvif/class.h>

struct nv50_disp_root {
	struct nvkm_parent base;
	struct nvkm_ramht *ramht;
	u32 chan;
};

extern struct nvkm_oclass nv50_disp_root_oclass[];
extern struct nvkm_oclass nv50_disp_sclass[];
extern struct nvkm_oclass g84_disp_root_oclass[];
extern struct nvkm_oclass g84_disp_sclass[];
extern struct nvkm_oclass g94_disp_root_oclass[];
extern struct nvkm_oclass g94_disp_sclass[];
extern struct nvkm_oclass gt200_disp_root_oclass[];
extern struct nvkm_oclass gt200_disp_sclass[];
extern struct nvkm_oclass gt215_disp_root_oclass[];
extern struct nvkm_oclass gt215_disp_sclass[];
extern struct nvkm_oclass gf119_disp_root_oclass[];
extern struct nvkm_oclass gf119_disp_sclass[];
extern struct nvkm_oclass gk104_disp_root_oclass[];
extern struct nvkm_oclass gk104_disp_sclass[];
extern struct nvkm_oclass gk110_disp_root_oclass[];
extern struct nvkm_oclass gk110_disp_sclass[];
extern struct nvkm_oclass gm107_disp_root_oclass[];
extern struct nvkm_oclass gm107_disp_sclass[];
extern struct nvkm_oclass gm204_disp_root_oclass[];
extern struct nvkm_oclass gm204_disp_sclass[];
#endif
