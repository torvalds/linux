#ifndef __NVKM_CE_H__
#define __NVKM_CE_H__
#include <engine/falcon.h>

void gt215_ce_intr(struct nvkm_falcon *, struct nvkm_fifo_chan *);

int gt215_ce_new(struct nvkm_device *, int, struct nvkm_engine **);
int gf100_ce_new(struct nvkm_device *, int, struct nvkm_engine **);
extern struct nvkm_oclass gk104_ce0_oclass;
extern struct nvkm_oclass gk104_ce1_oclass;
extern struct nvkm_oclass gk104_ce2_oclass;
extern struct nvkm_oclass gm204_ce0_oclass;
extern struct nvkm_oclass gm204_ce1_oclass;
extern struct nvkm_oclass gm204_ce2_oclass;
#endif
