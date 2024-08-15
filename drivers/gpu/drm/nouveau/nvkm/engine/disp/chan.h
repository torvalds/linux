/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_DISP_CHAN_H__
#define __NVKM_DISP_CHAN_H__
#define nvkm_disp_chan(p) container_of((p), struct nvkm_disp_chan, object)
#include <core/object.h>
#include "priv.h"

struct nvkm_disp_chan {
	const struct nvkm_disp_chan_func *func;
	const struct nvkm_disp_chan_mthd *mthd;
	struct nvkm_disp *disp;

	struct {
		int ctrl;
		int user;
	} chid;
	int head;

	struct nvkm_object object;

	struct nvkm_memory *memory;
	u64 push;

	u32 suspend_put;
};

int nvkm_disp_core_new(const struct nvkm_oclass *, void *, u32, struct nvkm_object **);
int nvkm_disp_chan_new(const struct nvkm_oclass *, void *, u32, struct nvkm_object **);
int nvkm_disp_wndw_new(const struct nvkm_oclass *, void *, u32, struct nvkm_object **);

struct nvkm_disp_chan_func {
	int (*push)(struct nvkm_disp_chan *, u64 object);
	int (*init)(struct nvkm_disp_chan *);
	void (*fini)(struct nvkm_disp_chan *);
	void (*intr)(struct nvkm_disp_chan *, bool en);
	u64 (*user)(struct nvkm_disp_chan *, u64 *size);
	int (*bind)(struct nvkm_disp_chan *, struct nvkm_object *, u32 handle);
};

void nv50_disp_chan_intr(struct nvkm_disp_chan *, bool);
u64 nv50_disp_chan_user(struct nvkm_disp_chan *, u64 *);
extern const struct nvkm_disp_chan_func nv50_disp_pioc_func;
extern const struct nvkm_disp_chan_func nv50_disp_dmac_func;
int nv50_disp_dmac_push(struct nvkm_disp_chan *, u64);
int nv50_disp_dmac_bind(struct nvkm_disp_chan *, struct nvkm_object *, u32);
extern const struct nvkm_disp_chan_func nv50_disp_core_func;

void gf119_disp_chan_intr(struct nvkm_disp_chan *, bool);
extern const struct nvkm_disp_chan_func gf119_disp_pioc_func;
extern const struct nvkm_disp_chan_func gf119_disp_dmac_func;
void gf119_disp_dmac_fini(struct nvkm_disp_chan *);
int gf119_disp_dmac_bind(struct nvkm_disp_chan *, struct nvkm_object *, u32);
extern const struct nvkm_disp_chan_func gf119_disp_core_func;
void gf119_disp_core_fini(struct nvkm_disp_chan *);

extern const struct nvkm_disp_chan_func gp102_disp_dmac_func;

u64 gv100_disp_chan_user(struct nvkm_disp_chan *, u64 *);
int gv100_disp_dmac_init(struct nvkm_disp_chan *);
void gv100_disp_dmac_fini(struct nvkm_disp_chan *);
int gv100_disp_dmac_bind(struct nvkm_disp_chan *, struct nvkm_object *, u32);

struct nvkm_disp_chan_user {
	const struct nvkm_disp_chan_func *func;
	int ctrl;
	int user;
	const struct nvkm_disp_chan_mthd *mthd;
};

extern const struct nvkm_disp_chan_user nv50_disp_oimm;
extern const struct nvkm_disp_chan_user nv50_disp_curs;

extern const struct nvkm_disp_chan_user g84_disp_core;
extern const struct nvkm_disp_chan_user g84_disp_base;
extern const struct nvkm_disp_chan_user g84_disp_ovly;

extern const struct nvkm_disp_chan_user g94_disp_core;

extern const struct nvkm_disp_chan_user gt200_disp_ovly;

extern const struct nvkm_disp_chan_user gf119_disp_base;
extern const struct nvkm_disp_chan_user gf119_disp_oimm;
extern const struct nvkm_disp_chan_user gf119_disp_curs;

extern const struct nvkm_disp_chan_user gk104_disp_core;
extern const struct nvkm_disp_chan_user gk104_disp_ovly;

extern const struct nvkm_disp_chan_user gv100_disp_core;
extern const struct nvkm_disp_chan_user gv100_disp_curs;
extern const struct nvkm_disp_chan_user gv100_disp_wndw;
extern const struct nvkm_disp_chan_user gv100_disp_wimm;

struct nvkm_disp_mthd_list {
	u32 mthd;
	u32 addr;
	struct {
		u32 mthd;
		u32 addr;
		const char *name;
	} data[];
};

struct nvkm_disp_chan_mthd {
	const char *name;
	u32 addr;
	s32 prev;
	struct {
		const char *name;
		int nr;
		const struct nvkm_disp_mthd_list *mthd;
	} data[];
};

void nv50_disp_chan_mthd(struct nvkm_disp_chan *, int debug);

extern const struct nvkm_disp_mthd_list nv50_disp_core_mthd_base;
extern const struct nvkm_disp_mthd_list nv50_disp_core_mthd_sor;
extern const struct nvkm_disp_mthd_list nv50_disp_core_mthd_pior;
extern const struct nvkm_disp_mthd_list nv50_disp_base_mthd_image;

extern const struct nvkm_disp_chan_mthd g84_disp_core_mthd;
extern const struct nvkm_disp_mthd_list g84_disp_core_mthd_dac;
extern const struct nvkm_disp_mthd_list g84_disp_core_mthd_head;

extern const struct nvkm_disp_chan_mthd g94_disp_core_mthd;

extern const struct nvkm_disp_mthd_list gf119_disp_core_mthd_base;
extern const struct nvkm_disp_mthd_list gf119_disp_core_mthd_dac;
extern const struct nvkm_disp_mthd_list gf119_disp_core_mthd_sor;
extern const struct nvkm_disp_mthd_list gf119_disp_core_mthd_pior;
extern const struct nvkm_disp_chan_mthd gf119_disp_base_mthd;

extern const struct nvkm_disp_chan_mthd gk104_disp_core_mthd;
extern const struct nvkm_disp_chan_mthd gk104_disp_ovly_mthd;
#endif
