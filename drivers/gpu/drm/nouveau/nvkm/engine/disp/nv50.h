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
	const struct nv50_disp_func *func;
	struct nvkm_disp base;

	struct work_struct supervisor;
	u32 super;

	struct nvkm_event uevent;

	struct {
		u32 lvdsconf;
	} sor;

	struct {
		u8 type[3];
	} pior;

	struct nv50_disp_chan *chan[17];
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

int nv50_disp_new_(const struct nv50_disp_func *, struct nvkm_device *,
		   int index, int heads, struct nvkm_disp **);
int gf119_disp_new_(const struct nv50_disp_func *, struct nvkm_device *,
		    int index, struct nvkm_disp **);

struct nv50_disp_func_outp {
	int (* crt)(struct nvkm_disp *, int index, struct dcb_output *,
		    struct nvkm_output **);
	int (*  tv)(struct nvkm_disp *, int index, struct dcb_output *,
		    struct nvkm_output **);
	int (*tmds)(struct nvkm_disp *, int index, struct dcb_output *,
		    struct nvkm_output **);
	int (*lvds)(struct nvkm_disp *, int index, struct dcb_output *,
		    struct nvkm_output **);
	int (*  dp)(struct nvkm_disp *, int index, struct dcb_output *,
		    struct nvkm_output **);
};

struct nv50_disp_func {
	void (*intr)(struct nv50_disp *);
	void (*intr_error)(struct nv50_disp *, int chid);

	const struct nvkm_event_func *uevent;
	void (*super)(struct work_struct *);

	const struct nvkm_disp_oclass *root;

	struct {
		void (*vblank_init)(struct nv50_disp *, int head);
		void (*vblank_fini)(struct nv50_disp *, int head);
		int (*scanoutpos)(NV50_DISP_MTHD_V0);
	} head;

	struct {
		const struct nv50_disp_func_outp internal;
		const struct nv50_disp_func_outp external;
	} outp;

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
		void (*magic)(struct nvkm_output *);
	} sor;

	struct {
		int nr;
		int (*power)(NV50_DISP_MTHD_V1);
	} pior;
};

void nv50_disp_vblank_init(struct nv50_disp *, int);
void nv50_disp_vblank_fini(struct nv50_disp *, int);
void nv50_disp_intr(struct nv50_disp *);
void nv50_disp_intr_supervisor(struct work_struct *);

void gf119_disp_vblank_init(struct nv50_disp *, int);
void gf119_disp_vblank_fini(struct nv50_disp *, int);
void gf119_disp_intr(struct nv50_disp *);
void gf119_disp_intr_supervisor(struct work_struct *);
void gf119_disp_intr_error(struct nv50_disp *, int);
#endif
