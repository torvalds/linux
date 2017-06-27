#ifndef __NV50_DISP_H__
#define __NV50_DISP_H__
#define nv50_disp(p) container_of((p), struct nv50_disp, base)
#include "priv.h"
struct nvkm_head;

struct nv50_disp {
	const struct nv50_disp_func *func;
	struct nvkm_disp base;

	struct workqueue_struct *wq;
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

void nv50_disp_super_1(struct nv50_disp *);
void nv50_disp_super_1_0(struct nv50_disp *, struct nvkm_head *);
void nv50_disp_super_2_0(struct nv50_disp *, struct nvkm_head *);
void nv50_disp_super_2_1(struct nv50_disp *, struct nvkm_head *);
void nv50_disp_super_2_2(struct nv50_disp *, struct nvkm_head *);
void nv50_disp_super_3_0(struct nv50_disp *, struct nvkm_head *);

int nv50_disp_new_(const struct nv50_disp_func *, struct nvkm_device *,
		   int index, int heads, struct nvkm_disp **);
int gf119_disp_new_(const struct nv50_disp_func *, struct nvkm_device *,
		    int index, struct nvkm_disp **);

struct nv50_disp_func {
	void (*intr)(struct nv50_disp *);
	void (*intr_error)(struct nv50_disp *, int chid);

	const struct nvkm_event_func *uevent;
	void (*super)(struct work_struct *);

	const struct nvkm_disp_oclass *root;

	struct {
		int (*new)(struct nvkm_disp *, int id);
	} head;

	struct {
		int nr;
		int (*new)(struct nvkm_disp *, int id);
	} dac;

	struct {
		int nr;
		int (*new)(struct nvkm_disp *, int id);
	} sor;

	struct {
		int nr;
		int (*new)(struct nvkm_disp *, int id);
	} pior;
};

void nv50_disp_intr(struct nv50_disp *);
void nv50_disp_super(struct work_struct *);

void gf119_disp_intr(struct nv50_disp *);
void gf119_disp_super(struct work_struct *);
void gf119_disp_intr_error(struct nv50_disp *, int);

void nv50_disp_dptmds_war_2(struct nv50_disp *, struct dcb_output *);
void nv50_disp_dptmds_war_3(struct nv50_disp *, struct dcb_output *);
void nv50_disp_update_sppll1(struct nv50_disp *);
#endif
