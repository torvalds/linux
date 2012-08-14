#ifndef __NV50_DISP_H__
#define __NV50_DISP_H__

#include <core/parent.h>
#include <core/namedb.h>
#include <core/ramht.h>

#include <engine/dmaobj.h>
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
	struct nouveau_ramht *ramht;
	u32 chan;
};

struct nv50_disp_chan {
	struct nouveau_namedb base;
	int chid;
};

int  nv50_disp_chan_create_(struct nouveau_object *, struct nouveau_object *,
			    struct nouveau_oclass *, int, int, void **);
void nv50_disp_chan_destroy(struct nv50_disp_chan *);
u32  nv50_disp_chan_rd32(struct nouveau_object *, u64);
void nv50_disp_chan_wr32(struct nouveau_object *, u64, u32);

#define nv50_disp_chan_init(a)                                                 \
	nouveau_namedb_init(&(a)->base)
#define nv50_disp_chan_fini(a,b)                                               \
	nouveau_namedb_fini(&(a)->base, (b))

int  nv50_disp_dmac_create_(struct nouveau_object *, struct nouveau_object *,
			    struct nouveau_oclass *, u32, int, int, void **);
void nv50_disp_dmac_dtor(struct nouveau_object *);

struct nv50_disp_dmac {
	struct nv50_disp_chan base;
	struct nouveau_dmaobj *pushdma;
	u32 push;
};

struct nv50_disp_pioc {
	struct nv50_disp_chan base;
};

extern struct nouveau_ofuncs nv50_disp_mast_ofuncs;
extern struct nouveau_ofuncs nv50_disp_sync_ofuncs;
extern struct nouveau_ofuncs nv50_disp_ovly_ofuncs;
extern struct nouveau_ofuncs nv50_disp_oimm_ofuncs;
extern struct nouveau_ofuncs nv50_disp_curs_ofuncs;
extern struct nouveau_ofuncs nv50_disp_base_ofuncs;
extern struct nouveau_oclass nv50_disp_cclass;
void nv50_disp_intr(struct nouveau_subdev *);

#endif
