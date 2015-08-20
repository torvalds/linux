#ifndef __NV50_DISP_H__
#define __NV50_DISP_H__
#include "priv.h"
struct nvkm_output;
struct nvkm_output_dp;

#define NV50_DISP_MTHD_ struct nvkm_object *object,                            \
	struct nv50_disp *disp, void *data, u32 size
#define NV50_DISP_MTHD_V0 NV50_DISP_MTHD_, int head
#define NV50_DISP_MTHD_V1 NV50_DISP_MTHD_, int head, struct nvkm_output *outp

struct nv50_disp {
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

int nv50_disp_root_scanoutpos(NV50_DISP_MTHD_V0);
int nv50_disp_root_mthd(struct nvkm_object *, u32, void *, u32);

int gf119_disp_root_scanoutpos(NV50_DISP_MTHD_V0);

int nv50_dac_power(NV50_DISP_MTHD_V1);
int nv50_dac_sense(NV50_DISP_MTHD_V1);

int gt215_hda_eld(NV50_DISP_MTHD_V1);
int gf119_hda_eld(NV50_DISP_MTHD_V1);

int g84_hdmi_ctrl(NV50_DISP_MTHD_V1);
int gt215_hdmi_ctrl(NV50_DISP_MTHD_V1);
int gf119_hdmi_ctrl(NV50_DISP_MTHD_V1);
int gk104_hdmi_ctrl(NV50_DISP_MTHD_V1);

int nv50_sor_power(NV50_DISP_MTHD_V1);
int nv50_pior_power(NV50_DISP_MTHD_V1);

extern struct nv50_disp_chan_impl nv50_disp_core_ofuncs;
int nv50_disp_core_ctor(struct nvkm_object *, struct nvkm_object *,
			struct nvkm_oclass *, void *, u32,
			struct nvkm_object **);
extern struct nv50_disp_chan_impl nv50_disp_base_ofuncs;
int nv50_disp_base_ctor(struct nvkm_object *, struct nvkm_object *,
			struct nvkm_oclass *, void *, u32,
			struct nvkm_object **);
extern struct nv50_disp_chan_impl nv50_disp_ovly_ofuncs;
int nv50_disp_ovly_ctor(struct nvkm_object *, struct nvkm_object *,
			struct nvkm_oclass *, void *, u32,
			struct nvkm_object **);
extern struct nv50_disp_chan_impl nv50_disp_oimm_ofuncs;
int nv50_disp_oimm_ctor(struct nvkm_object *, struct nvkm_object *,
			struct nvkm_oclass *, void *, u32,
			struct nvkm_object **);
extern struct nv50_disp_chan_impl nv50_disp_curs_ofuncs;
int nv50_disp_curs_ctor(struct nvkm_object *, struct nvkm_object *,
			struct nvkm_oclass *, void *, u32,
			struct nvkm_object **);
extern struct nvkm_ofuncs nv50_disp_root_ofuncs;
int  nv50_disp_root_ctor(struct nvkm_object *, struct nvkm_object *,
			 struct nvkm_oclass *, void *, u32,
			 struct nvkm_object **);
void nv50_disp_root_dtor(struct nvkm_object *);
extern struct nvkm_oclass nv50_disp_cclass;
void nv50_disp_intr_supervisor(struct work_struct *);
void nv50_disp_intr(struct nvkm_subdev *);
extern const struct nvkm_event_func nv50_disp_vblank_func;

extern struct nv50_disp_chan_impl gf119_disp_core_ofuncs;
extern struct nv50_disp_chan_impl gf119_disp_base_ofuncs;
extern struct nv50_disp_chan_impl gf119_disp_ovly_ofuncs;
extern struct nv50_disp_chan_impl gf119_disp_oimm_ofuncs;
extern struct nv50_disp_chan_impl gf119_disp_curs_ofuncs;
extern struct nvkm_ofuncs gf119_disp_root_ofuncs;
extern struct nvkm_oclass gf119_disp_cclass;
void gf119_disp_intr_supervisor(struct work_struct *);
void gf119_disp_intr(struct nvkm_subdev *);
extern const struct nvkm_event_func gf119_disp_vblank_func;

extern struct nvkm_output_dp_impl nv50_pior_dp_impl;
extern struct nvkm_oclass *nv50_disp_outp_sclass[];

extern struct nvkm_output_dp_impl g94_sor_dp_impl;
u32 g94_sor_dp_lane_map(struct nvkm_device *, u8 lane);
int g94_sor_dp_lnk_pwr(struct nvkm_output_dp *, int);
extern struct nvkm_oclass *g94_disp_outp_sclass[];

extern struct nvkm_output_dp_impl gf119_sor_dp_impl;
int gf119_sor_dp_lnk_ctl(struct nvkm_output_dp *, int, int, bool);
extern struct nvkm_oclass *gf119_disp_outp_sclass[];

void gm204_sor_magic(struct nvkm_output *outp);
extern struct nvkm_output_dp_impl gm204_sor_dp_impl;
#endif
