#ifndef __NV50_DISP_H__
#define __NV50_DISP_H__
#include "priv.h"
struct nvkm_output;
struct nvkm_output_dp;

#define NV50_DISP_MTHD_ struct nvkm_object *object,                            \
	struct nv50_disp_priv *priv, void *data, u32 size
#define NV50_DISP_MTHD_V0 NV50_DISP_MTHD_, int head
#define NV50_DISP_MTHD_V1 NV50_DISP_MTHD_, int head, struct nvkm_output *outp

struct nv50_disp_priv {
	struct nvkm_disp base;
	struct nvkm_oclass *sclass;

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
		void (*magic)(struct nvkm_output *);
	} sor;
	struct {
		int nr;
		int (*power)(NV50_DISP_MTHD_V1);
		u8 type[3];
	} pior;
};

struct nv50_disp_impl {
	struct nvkm_disp_impl base;
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

int nv50_disp_main_scanoutpos(NV50_DISP_MTHD_V0);
int nv50_disp_main_mthd(struct nvkm_object *, u32, void *, u32);

int gf110_disp_main_scanoutpos(NV50_DISP_MTHD_V0);

int nv50_dac_power(NV50_DISP_MTHD_V1);
int nv50_dac_sense(NV50_DISP_MTHD_V1);

int gt215_hda_eld(NV50_DISP_MTHD_V1);
int gf110_hda_eld(NV50_DISP_MTHD_V1);

int g84_hdmi_ctrl(NV50_DISP_MTHD_V1);
int gt215_hdmi_ctrl(NV50_DISP_MTHD_V1);
int gf110_hdmi_ctrl(NV50_DISP_MTHD_V1);
int gk104_hdmi_ctrl(NV50_DISP_MTHD_V1);

int nv50_sor_power(NV50_DISP_MTHD_V1);
int nv50_pior_power(NV50_DISP_MTHD_V1);

#include <core/parent.h>

struct nv50_disp_base {
	struct nvkm_parent base;
	struct nvkm_ramht *ramht;
	u32 chan;
};

struct nv50_disp_chan_impl {
	struct nvkm_ofuncs base;
	int chid;
	int  (*attach)(struct nvkm_object *, struct nvkm_object *, u32);
	void (*detach)(struct nvkm_object *, int);
};

#include <core/namedb.h>

struct nv50_disp_chan {
	struct nvkm_namedb base;
	int chid;
};

int  nv50_disp_chan_ntfy(struct nvkm_object *, u32, struct nvkm_event **);
int  nv50_disp_chan_map(struct nvkm_object *, u64 *, u32 *);
u32  nv50_disp_chan_rd32(struct nvkm_object *, u64);
void nv50_disp_chan_wr32(struct nvkm_object *, u64, u32);
extern const struct nvkm_event_func nv50_disp_chan_uevent;
int  nv50_disp_chan_uevent_ctor(struct nvkm_object *, void *, u32,
				struct nvkm_notify *);
void nv50_disp_chan_uevent_send(struct nv50_disp_priv *, int);

extern const struct nvkm_event_func gf110_disp_chan_uevent;

#define nv50_disp_chan_init(a)                                                 \
	nvkm_namedb_init(&(a)->base)
#define nv50_disp_chan_fini(a,b)                                               \
	nvkm_namedb_fini(&(a)->base, (b))

struct nv50_disp_dmac {
	struct nv50_disp_chan base;
	struct nvkm_dmaobj *pushdma;
	u32 push;
};

void nv50_disp_dmac_dtor(struct nvkm_object *);

struct nv50_disp_pioc {
	struct nv50_disp_chan base;
};

void nv50_disp_pioc_dtor(struct nvkm_object *);

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

extern struct nv50_disp_chan_impl nv50_disp_core_ofuncs;
int nv50_disp_core_ctor(struct nvkm_object *, struct nvkm_object *,
			struct nvkm_oclass *, void *, u32,
			struct nvkm_object **);
extern const struct nv50_disp_mthd_list nv50_disp_core_mthd_base;
extern const struct nv50_disp_mthd_list nv50_disp_core_mthd_sor;
extern const struct nv50_disp_mthd_list nv50_disp_core_mthd_pior;
extern struct nv50_disp_chan_impl nv50_disp_base_ofuncs;
int nv50_disp_base_ctor(struct nvkm_object *, struct nvkm_object *,
			struct nvkm_oclass *, void *, u32,
			struct nvkm_object **);
extern const struct nv50_disp_mthd_list nv50_disp_base_mthd_image;
extern struct nv50_disp_chan_impl nv50_disp_ovly_ofuncs;
int nv50_disp_ovly_ctor(struct nvkm_object *, struct nvkm_object *,
			struct nvkm_oclass *, void *, u32,
			struct nvkm_object **);
extern const struct nv50_disp_mthd_list nv50_disp_ovly_mthd_base;
extern struct nv50_disp_chan_impl nv50_disp_oimm_ofuncs;
int nv50_disp_oimm_ctor(struct nvkm_object *, struct nvkm_object *,
			struct nvkm_oclass *, void *, u32,
			struct nvkm_object **);
extern struct nv50_disp_chan_impl nv50_disp_curs_ofuncs;
int nv50_disp_curs_ctor(struct nvkm_object *, struct nvkm_object *,
			struct nvkm_oclass *, void *, u32,
			struct nvkm_object **);
extern struct nvkm_ofuncs nv50_disp_main_ofuncs;
int  nv50_disp_main_ctor(struct nvkm_object *, struct nvkm_object *,
			 struct nvkm_oclass *, void *, u32,
			 struct nvkm_object **);
void nv50_disp_main_dtor(struct nvkm_object *);
extern struct nvkm_omthds nv50_disp_main_omthds[];
extern struct nvkm_oclass nv50_disp_cclass;
void nv50_disp_mthd_chan(struct nv50_disp_priv *, int debug, int head,
			 const struct nv50_disp_mthd_chan *);
void nv50_disp_intr_supervisor(struct work_struct *);
void nv50_disp_intr(struct nvkm_subdev *);
extern const struct nvkm_event_func nv50_disp_vblank_func;

extern const struct nv50_disp_mthd_chan g84_disp_core_mthd_chan;
extern const struct nv50_disp_mthd_list g84_disp_core_mthd_dac;
extern const struct nv50_disp_mthd_list g84_disp_core_mthd_head;
extern const struct nv50_disp_mthd_chan g84_disp_base_mthd_chan;
extern const struct nv50_disp_mthd_chan g84_disp_ovly_mthd_chan;

extern const struct nv50_disp_mthd_chan g94_disp_core_mthd_chan;

extern struct nv50_disp_chan_impl gf110_disp_core_ofuncs;
extern const struct nv50_disp_mthd_list gf110_disp_core_mthd_base;
extern const struct nv50_disp_mthd_list gf110_disp_core_mthd_dac;
extern const struct nv50_disp_mthd_list gf110_disp_core_mthd_sor;
extern const struct nv50_disp_mthd_list gf110_disp_core_mthd_pior;
extern struct nv50_disp_chan_impl gf110_disp_base_ofuncs;
extern struct nv50_disp_chan_impl gf110_disp_ovly_ofuncs;
extern const struct nv50_disp_mthd_chan gf110_disp_base_mthd_chan;
extern struct nv50_disp_chan_impl gf110_disp_oimm_ofuncs;
extern struct nv50_disp_chan_impl gf110_disp_curs_ofuncs;
extern struct nvkm_ofuncs gf110_disp_main_ofuncs;
extern struct nvkm_oclass gf110_disp_cclass;
void gf110_disp_intr_supervisor(struct work_struct *);
void gf110_disp_intr(struct nvkm_subdev *);
extern const struct nvkm_event_func gf110_disp_vblank_func;

extern const struct nv50_disp_mthd_chan gk104_disp_core_mthd_chan;
extern const struct nv50_disp_mthd_chan gk104_disp_ovly_mthd_chan;

extern struct nvkm_output_dp_impl nv50_pior_dp_impl;
extern struct nvkm_oclass *nv50_disp_outp_sclass[];

extern struct nvkm_output_dp_impl g94_sor_dp_impl;
int g94_sor_dp_lnk_pwr(struct nvkm_output_dp *, int);
extern struct nvkm_oclass *g94_disp_outp_sclass[];

extern struct nvkm_output_dp_impl gf110_sor_dp_impl;
int gf110_sor_dp_lnk_ctl(struct nvkm_output_dp *, int, int, bool);
extern struct nvkm_oclass *gf110_disp_outp_sclass[];

void gm204_sor_magic(struct nvkm_output *outp);
extern struct nvkm_output_dp_impl gm204_sor_dp_impl;
#endif
