#ifndef __NVKM_CE_H__
#define __NVKM_CE_H__

void nva3_ce_intr(struct nouveau_subdev *);

extern struct nouveau_oclass nva3_ce_oclass;
extern struct nouveau_oclass nvc0_ce0_oclass;
extern struct nouveau_oclass nvc0_ce1_oclass;
extern struct nouveau_oclass nve0_ce0_oclass;
extern struct nouveau_oclass nve0_ce1_oclass;
extern struct nouveau_oclass nve0_ce2_oclass;

#endif
