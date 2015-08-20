#ifndef __NV50_DISP_H__
#define __NV50_DISP_H__
#define nv50_disp(p) container_of((p), struct nv50_disp, base)
#include "priv.h"
struct nvkm_output;
struct nvkm_output_dp;

#define NV50_DISP_MTHD_ struct nvkm_object *object,                            \
	struct nv50_disp *disp, void *data, u32 size
#define NV50_DISP_MTHD_V0 NV50_DISP_MTHD_, int head
#define NV50_DISP_MTHD_V1 NV50_DISP_MTHD_, int head, struct nvkm_output *outp

struct nv50_disp {
	struct nvkm_disp base;

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

	struct nv50_disp_chan *chan[17];
};

struct nv50_disp_impl {
	struct nvkm_disp_impl base;
	struct {
		int (*scanoutpos)(NV50_DISP_MTHD_V0);
	} head;
};

int nv50_disp_root_scanoutpos(NV50_DISP_MTHD_V0);

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

void nv50_disp_intr_supervisor(struct work_struct *);
void nv50_disp_intr(struct nvkm_subdev *);
extern const struct nvkm_event_func nv50_disp_vblank_func;

void gf119_disp_intr_supervisor(struct work_struct *);
void gf119_disp_intr(struct nvkm_subdev *);
extern const struct nvkm_event_func gf119_disp_vblank_func;
#endif
