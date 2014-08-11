#ifndef __NV50_DISP_H__
#define __NV50_DISP_H__

#include <core/parent.h>
#include <core/namedb.h>
#include <core/engctx.h>
#include <core/ramht.h>
#include <core/event.h>

#include <engine/dmaobj.h>

#include "dport.h"
#include "priv.h"
#include "outp.h"
#include "outpdp.h"

#define NV50_DISP_MTHD_ struct nouveau_object *object,                         \
	struct nv50_disp_priv *priv, void *data, u32 size
#define NV50_DISP_MTHD_V0 NV50_DISP_MTHD_, int head
#define NV50_DISP_MTHD_V1 NV50_DISP_MTHD_, int head, struct nvkm_output *outp

struct nv50_disp_priv {
	struct nouveau_disp base;
	struct nouveau_oclass *sclass;

	struct work_struct supervisor;
	u32 super;

	struct nvkm_event uevent;

	struct {
		int nr;
	} head;
	struct {
		int nr;
		int (*power)(NV50_DISP_MTHD_V1);
		int (*sense)(NV50_DISP_MTHD_V1);
	} dac;
	struct {
		int nr;
		int (*power)(NV50_DISP_MTHD_V1);
		int (*hda_eld)(NV50_DISP_MTHD_V1);
		int (*hdmi)(NV50_DISP_MTHD_V1);
		u32 lvdsconf;
	} sor;
	struct {
		int nr;
		int (*power)(NV50_DISP_MTHD_V1);
		u8 type[3];
	} pior;
};

struct nv50_disp_impl {
	struct nouveau_disp_impl base;
	struct {
		const struct nv50_disp_mthd_chan *core;
		const struct nv50_disp_mthd_chan *base;
		const struct nv50_disp_mthd_chan *ovly;
		int prev;
	} mthd;
	struct {
		int (*scanoutpos)(NV50_DISP_MTHD_V0);
	} head;
};

int nv50_disp_base_scanoutpos(NV50_DISP_MTHD_V0);
int nv50_disp_base_mthd(struct nouveau_object *, u32, void *, u32);

int nvd0_disp_base_scanoutpos(NV50_DISP_MTHD_V0);

int nv50_dac_power(NV50_DISP_MTHD_V1);
int nv50_dac_sense(NV50_DISP_MTHD_V1);

int nva3_hda_eld(NV50_DISP_MTHD_V1);
int nvd0_hda_eld(NV50_DISP_MTHD_V1);

int nv84_hdmi_ctrl(NV50_DISP_MTHD_V1);
int nva3_hdmi_ctrl(NV50_DISP_MTHD_V1);
int nvd0_hdmi_ctrl(NV50_DISP_MTHD_V1);

int nv50_sor_power(NV50_DISP_MTHD_V1);

int nv94_sor_dp_train_init(struct nv50_disp_priv *, int, int, int, u16, u16,
		           u32, struct dcb_output *);
int nv94_sor_dp_train_fini(struct nv50_disp_priv *, int, int, int, u16, u16,
		           u32, struct dcb_output *);
int nv94_sor_dp_train(struct nv50_disp_priv *, int, int, u16, u16, u32,
		      struct dcb_output *);
int nv94_sor_dp_lnkctl(struct nv50_disp_priv *, int, int, int, u16, u16, u32,
		       struct dcb_output *);
int nv94_sor_dp_drvctl(struct nv50_disp_priv *, int, int, int, u16, u16, u32,
		       struct dcb_output *);

int nvd0_sor_dp_train(struct nv50_disp_priv *, int, int, u16, u16, u32,
		      struct dcb_output *);
int nvd0_sor_dp_lnkctl(struct nv50_disp_priv *, int, int, int, u16, u16, u32,
		       struct dcb_output *);
int nvd0_sor_dp_drvctl(struct nv50_disp_priv *, int, int, int, u16, u16, u32,
		       struct dcb_output *);

int nv50_pior_power(NV50_DISP_MTHD_V1);

struct nv50_disp_base {
	struct nouveau_parent base;
	struct nouveau_ramht *ramht;
	u32 chan;
};

struct nv50_disp_chan_impl {
	struct nouveau_ofuncs base;
	int chid;
	int  (*attach)(struct nouveau_object *, struct nouveau_object *, u32);
	void (*detach)(struct nouveau_object *, int);
};

struct nv50_disp_chan {
	struct nouveau_namedb base;
	int chid;
};

int  nv50_disp_chan_ntfy(struct nouveau_object *, u32, struct nvkm_event **);
int  nv50_disp_chan_map(struct nouveau_object *, u64 *, u32 *);
u32  nv50_disp_chan_rd32(struct nouveau_object *, u64);
void nv50_disp_chan_wr32(struct nouveau_object *, u64, u32);
extern const struct nvkm_event_func nv50_disp_chan_uevent;
int  nv50_disp_chan_uevent_ctor(struct nouveau_object *, void *, u32,
				struct nvkm_notify *);
void nv50_disp_chan_uevent_send(struct nv50_disp_priv *, int);

extern const struct nvkm_event_func nvd0_disp_chan_uevent;

#define nv50_disp_chan_init(a)                                                 \
	nouveau_namedb_init(&(a)->base)
#define nv50_disp_chan_fini(a,b)                                               \
	nouveau_namedb_fini(&(a)->base, (b))

struct nv50_disp_dmac {
	struct nv50_disp_chan base;
	struct nouveau_dmaobj *pushdma;
	u32 push;
};

void nv50_disp_dmac_dtor(struct nouveau_object *);

struct nv50_disp_pioc {
	struct nv50_disp_chan base;
};

void nv50_disp_pioc_dtor(struct nouveau_object *);

struct nv50_disp_mthd_list {
	u32 mthd;
	u32 addr;
	struct {
		u32 mthd;
		u32 addr;
		const char *name;
	} data[];
};

struct nv50_disp_mthd_chan {
	const char *name;
	u32 addr;
	struct {
		const char *name;
		int nr;
		const struct nv50_disp_mthd_list *mthd;
	} data[];
};

extern struct nv50_disp_chan_impl nv50_disp_mast_ofuncs;
int nv50_disp_mast_ctor(struct nouveau_object *, struct nouveau_object *,
			struct nouveau_oclass *, void *, u32,
			struct nouveau_object **);
extern const struct nv50_disp_mthd_list nv50_disp_mast_mthd_base;
extern const struct nv50_disp_mthd_list nv50_disp_mast_mthd_sor;
extern const struct nv50_disp_mthd_list nv50_disp_mast_mthd_pior;
extern struct nv50_disp_chan_impl nv50_disp_sync_ofuncs;
int nv50_disp_sync_ctor(struct nouveau_object *, struct nouveau_object *,
			struct nouveau_oclass *, void *, u32,
			struct nouveau_object **);
extern const struct nv50_disp_mthd_list nv50_disp_sync_mthd_image;
extern struct nv50_disp_chan_impl nv50_disp_ovly_ofuncs;
int nv50_disp_ovly_ctor(struct nouveau_object *, struct nouveau_object *,
			struct nouveau_oclass *, void *, u32,
			struct nouveau_object **);
extern const struct nv50_disp_mthd_list nv50_disp_ovly_mthd_base;
extern struct nv50_disp_chan_impl nv50_disp_oimm_ofuncs;
int nv50_disp_oimm_ctor(struct nouveau_object *, struct nouveau_object *,
			struct nouveau_oclass *, void *, u32,
			struct nouveau_object **);
extern struct nv50_disp_chan_impl nv50_disp_curs_ofuncs;
int nv50_disp_curs_ctor(struct nouveau_object *, struct nouveau_object *,
			struct nouveau_oclass *, void *, u32,
			struct nouveau_object **);
extern struct nouveau_ofuncs nv50_disp_base_ofuncs;
int  nv50_disp_base_ctor(struct nouveau_object *, struct nouveau_object *,
			 struct nouveau_oclass *, void *, u32,
			 struct nouveau_object **);
void nv50_disp_base_dtor(struct nouveau_object *);
extern struct nouveau_omthds nv50_disp_base_omthds[];
extern struct nouveau_oclass nv50_disp_cclass;
void nv50_disp_mthd_chan(struct nv50_disp_priv *, int debug, int head,
			 const struct nv50_disp_mthd_chan *);
void nv50_disp_intr_supervisor(struct work_struct *);
void nv50_disp_intr(struct nouveau_subdev *);
extern const struct nvkm_event_func nv50_disp_vblank_func;

extern const struct nv50_disp_mthd_chan nv84_disp_mast_mthd_chan;
extern const struct nv50_disp_mthd_list nv84_disp_mast_mthd_dac;
extern const struct nv50_disp_mthd_list nv84_disp_mast_mthd_head;
extern const struct nv50_disp_mthd_chan nv84_disp_sync_mthd_chan;
extern const struct nv50_disp_mthd_chan nv84_disp_ovly_mthd_chan;

extern const struct nv50_disp_mthd_chan nv94_disp_mast_mthd_chan;

extern struct nv50_disp_chan_impl nvd0_disp_mast_ofuncs;
extern const struct nv50_disp_mthd_list nvd0_disp_mast_mthd_base;
extern const struct nv50_disp_mthd_list nvd0_disp_mast_mthd_dac;
extern const struct nv50_disp_mthd_list nvd0_disp_mast_mthd_sor;
extern const struct nv50_disp_mthd_list nvd0_disp_mast_mthd_pior;
extern struct nv50_disp_chan_impl nvd0_disp_sync_ofuncs;
extern struct nv50_disp_chan_impl nvd0_disp_ovly_ofuncs;
extern const struct nv50_disp_mthd_chan nvd0_disp_sync_mthd_chan;
extern struct nv50_disp_chan_impl nvd0_disp_oimm_ofuncs;
extern struct nv50_disp_chan_impl nvd0_disp_curs_ofuncs;
extern struct nouveau_ofuncs nvd0_disp_base_ofuncs;
extern struct nouveau_oclass nvd0_disp_cclass;
void nvd0_disp_intr_supervisor(struct work_struct *);
void nvd0_disp_intr(struct nouveau_subdev *);
extern const struct nvkm_event_func nvd0_disp_vblank_func;

extern const struct nv50_disp_mthd_chan nve0_disp_mast_mthd_chan;
extern const struct nv50_disp_mthd_chan nve0_disp_ovly_mthd_chan;

extern struct nvkm_output_dp_impl nv50_pior_dp_impl;
extern struct nouveau_oclass *nv50_disp_outp_sclass[];

extern struct nvkm_output_dp_impl nv94_sor_dp_impl;
int nv94_sor_dp_lnk_pwr(struct nvkm_output_dp *, int);
extern struct nouveau_oclass *nv94_disp_outp_sclass[];

extern struct nvkm_output_dp_impl nvd0_sor_dp_impl;
extern struct nouveau_oclass *nvd0_disp_outp_sclass[];

#endif
