#ifndef __NV50_DISP_H__
#define __NV50_DISP_H__

#include <core/parent.h>
#include <engine/disp.h>

struct nv50_disp_priv {
	struct nouveau_disp base;
	struct nouveau_oclass *sclass;
	struct {
		int nr;
	} head;
	struct {
		int nr;
	} dac;
	struct {
		int nr;
	} sor;
};

struct nv50_disp_base {
	struct nouveau_parent base;
};

struct nv50_disp_chan {
	struct nouveau_object base;
};

extern struct nouveau_ofuncs nv50_disp_mast_ofuncs;
extern struct nouveau_ofuncs nv50_disp_dmac_ofuncs;
extern struct nouveau_ofuncs nv50_disp_pioc_ofuncs;
extern struct nouveau_ofuncs nv50_disp_base_ofuncs;
extern struct nouveau_oclass nv50_disp_cclass;
void nv50_disp_intr(struct nouveau_subdev *);

#endif
