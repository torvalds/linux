/* SPDX-License-Identifier: MIT */
#ifndef __NV50_DISP_H__
#define __NV50_DISP_H__
#define nv50_disp(p) container_of((p), struct nv50_disp, base)
#include "priv.h"
struct nvkm_head;

#include <core/enum.h>

struct nv50_disp {
	const struct nv50_disp_func *func;
	struct nvkm_disp base;

	struct workqueue_struct *wq;
	struct work_struct supervisor;
	u32 super;

	struct nvkm_event uevent;

	struct {
		unsigned long mask;
		int nr;
	} wndw, head, dac;

	struct {
		unsigned long mask;
		int nr;
		u32 lvdsconf;
	} sor;

	struct {
		unsigned long mask;
		int nr;
		u8 type[3];
	} pior;

	struct nvkm_gpuobj *inst;
	struct nvkm_ramht *ramht;

	struct nv50_disp_chan *chan[81];
};

void nv50_disp_super_1(struct nv50_disp *);
void nv50_disp_super_1_0(struct nv50_disp *, struct nvkm_head *);
void nv50_disp_super_2_0(struct nv50_disp *, struct nvkm_head *);
void nv50_disp_super_2_1(struct nv50_disp *, struct nvkm_head *);
void nv50_disp_super_2_2(struct nv50_disp *, struct nvkm_head *);
void nv50_disp_super_3_0(struct nv50_disp *, struct nvkm_head *);

int nv50_disp_new_(const struct nv50_disp_func *, struct nvkm_device *,
		   int index, struct nvkm_disp **);

struct nv50_disp_func {
	int (*init)(struct nv50_disp *);
	void (*fini)(struct nv50_disp *);
	void (*intr)(struct nv50_disp *);
	void (*intr_error)(struct nv50_disp *, int chid);

	const struct nvkm_event_func *uevent;
	void (*super)(struct work_struct *);

	const struct nvkm_disp_oclass *root;

	struct {
		int (*cnt)(struct nvkm_disp *, unsigned long *mask);
		int (*new)(struct nvkm_disp *, int id);
	} wndw, head, dac, sor, pior;

	u16 ramht_size;
};

int nv50_disp_init(struct nv50_disp *);
void nv50_disp_fini(struct nv50_disp *);
void nv50_disp_intr(struct nv50_disp *);
void nv50_disp_super(struct work_struct *);
extern const struct nvkm_enum nv50_disp_intr_error_type[];

int gf119_disp_init(struct nv50_disp *);
void gf119_disp_fini(struct nv50_disp *);
void gf119_disp_intr(struct nv50_disp *);
void gf119_disp_super(struct work_struct *);
void gf119_disp_intr_error(struct nv50_disp *, int);

void gv100_disp_fini(struct nv50_disp *);
void gv100_disp_intr(struct nv50_disp *);
void gv100_disp_super(struct work_struct *);
int gv100_disp_wndw_cnt(struct nvkm_disp *, unsigned long *);

int tu102_disp_init(struct nv50_disp *);

void nv50_disp_dptmds_war_2(struct nv50_disp *, struct dcb_output *);
void nv50_disp_dptmds_war_3(struct nv50_disp *, struct dcb_output *);
void nv50_disp_update_sppll1(struct nv50_disp *);

extern const struct nvkm_event_func nv50_disp_chan_uevent;
int  nv50_disp_chan_uevent_ctor(struct nvkm_object *, void *, u32,
				struct nvkm_notify *);
void nv50_disp_chan_uevent_send(struct nv50_disp *, int);

extern const struct nvkm_event_func gf119_disp_chan_uevent;
extern const struct nvkm_event_func gv100_disp_chan_uevent;
#endif
