/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_DISP_PRIV_H__
#define __NVKM_DISP_PRIV_H__
#define nvkm_udisp(p) container_of((p), struct nvkm_disp, client.object)
#include <engine/disp.h>
#include <core/enum.h>
struct nvkm_head;
struct nvkm_outp;
struct dcb_output;

int nvkm_disp_ctor(const struct nvkm_disp_func *, struct nvkm_device *, enum nvkm_subdev_type, int,
		   struct nvkm_disp *);
int nvkm_disp_new_(const struct nvkm_disp_func *, struct nvkm_device *, enum nvkm_subdev_type, int,
		   struct nvkm_disp **);
void nvkm_disp_vblank(struct nvkm_disp *, int head);

struct nvkm_disp_func {
	int (*oneinit)(struct nvkm_disp *);
	int (*init)(struct nvkm_disp *);
	void (*fini)(struct nvkm_disp *);
	void (*intr)(struct nvkm_disp *);
	void (*intr_error)(struct nvkm_disp *, int chid);

	void (*super)(struct work_struct *);

	const struct nvkm_event_func *uevent;

	struct {
		int (*cnt)(struct nvkm_disp *, unsigned long *mask);
		int (*new)(struct nvkm_disp *, int id);
	} wndw, head, dac, sor, pior;

	u16 ramht_size;

	const struct nvkm_sclass root;

	struct nvkm_disp_user {
		struct nvkm_sclass base;
		int (*ctor)(const struct nvkm_oclass *, void *argv, u32 argc,
			    struct nvkm_object **);
		const struct nvkm_disp_chan_user *chan;
	} user[];
};

int  nvkm_disp_ntfy(struct nvkm_object *, u32, struct nvkm_event **);
int nv04_disp_mthd(struct nvkm_object *, u32, void *, u32);
int nv50_disp_root_mthd_(struct nvkm_object *, u32, void *, u32);

int nv50_disp_oneinit(struct nvkm_disp *);
int nv50_disp_init(struct nvkm_disp *);
void nv50_disp_fini(struct nvkm_disp *);
void nv50_disp_intr(struct nvkm_disp *);
extern const struct nvkm_enum nv50_disp_intr_error_type[];
void nv50_disp_super(struct work_struct *);
void nv50_disp_super_1(struct nvkm_disp *);
void nv50_disp_super_1_0(struct nvkm_disp *, struct nvkm_head *);
void nv50_disp_super_2_0(struct nvkm_disp *, struct nvkm_head *);
void nv50_disp_super_2_1(struct nvkm_disp *, struct nvkm_head *);
void nv50_disp_super_2_2(struct nvkm_disp *, struct nvkm_head *);
void nv50_disp_super_3_0(struct nvkm_disp *, struct nvkm_head *);

int gf119_disp_init(struct nvkm_disp *);
void gf119_disp_fini(struct nvkm_disp *);
void gf119_disp_intr(struct nvkm_disp *);
void gf119_disp_super(struct work_struct *);
void gf119_disp_intr_error(struct nvkm_disp *, int);

void gv100_disp_fini(struct nvkm_disp *);
void gv100_disp_intr(struct nvkm_disp *);
void gv100_disp_super(struct work_struct *);
int gv100_disp_wndw_cnt(struct nvkm_disp *, unsigned long *);
int gv100_disp_caps_new(const struct nvkm_oclass *, void *, u32, struct nvkm_object **);

int tu102_disp_init(struct nvkm_disp *);

void nv50_disp_dptmds_war_2(struct nvkm_disp *, struct dcb_output *);
void nv50_disp_dptmds_war_3(struct nvkm_disp *, struct dcb_output *);
void nv50_disp_update_sppll1(struct nvkm_disp *);

extern const struct nvkm_event_func nv50_disp_chan_uevent;
void nv50_disp_chan_uevent_send(struct nvkm_disp *, int);

extern const struct nvkm_event_func gf119_disp_chan_uevent;
extern const struct nvkm_event_func gv100_disp_chan_uevent;

int nvkm_udisp_new(const struct nvkm_oclass *, void *, u32, struct nvkm_object **);
int nvkm_uconn_new(const struct nvkm_oclass *, void *, u32, struct nvkm_object **);
int nvkm_uoutp_new(const struct nvkm_oclass *, void *, u32, struct nvkm_object **);
#endif
