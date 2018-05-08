/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NV50_DISP_CHAN_H__
#define __NV50_DISP_CHAN_H__
#define nv50_disp_chan(p) container_of((p), struct nv50_disp_chan, object)
#include <core/object.h>
#include "nv50.h"
struct nv50_disp_root;
struct nv50_disp_dmac_func;

struct nv50_disp_chan {
	const struct nv50_disp_chan_func *func;
	const struct nv50_disp_chan_mthd *mthd;
	struct nv50_disp *disp;

	struct {
		int ctrl;
		int user;
	} chid;
	int head;

	struct nvkm_object object;
};

struct nv50_disp_chan_func {
	void *(*dtor)(struct nv50_disp_chan *);
	int (*init)(struct nv50_disp_chan *);
	void (*fini)(struct nv50_disp_chan *);
	int (*child_get)(struct nv50_disp_chan *, int index,
			 struct nvkm_oclass *);
	int (*child_new)(struct nv50_disp_chan *, const struct nvkm_oclass *,
			 void *data, u32 size, struct nvkm_object **);
};

int nv50_disp_chan_ctor(const struct nv50_disp_chan_func *,
			const struct nv50_disp_chan_mthd *,
			struct nv50_disp *, int ctrl, int user, int head,
			const struct nvkm_oclass *, struct nv50_disp_chan *);
int nv50_disp_chan_new_(const struct nv50_disp_chan_func *,
			const struct nv50_disp_chan_mthd *,
			struct nv50_disp *, int ctrl, int user, int head,
			const struct nvkm_oclass *, struct nvkm_object **);

extern const struct nv50_disp_chan_func nv50_disp_pioc_func;
extern const struct nv50_disp_chan_func gf119_disp_pioc_func;

extern const struct nvkm_event_func nv50_disp_chan_uevent;
int  nv50_disp_chan_uevent_ctor(struct nvkm_object *, void *, u32,
				struct nvkm_notify *);
void nv50_disp_chan_uevent_send(struct nv50_disp *, int);

extern const struct nvkm_event_func gf119_disp_chan_uevent;

int nv50_disp_curs_new_(const struct nv50_disp_chan_func *,
			struct nv50_disp *, int ctrl, int user,
			const struct nvkm_oclass *, void *argv, u32 argc,
			struct nvkm_object **);
int nv50_disp_oimm_new_(const struct nv50_disp_chan_func *,
			struct nv50_disp *, int ctrl, int user,
			const struct nvkm_oclass *, void *argv, u32 argc,
			struct nvkm_object **);
int nv50_disp_base_new_(const struct nv50_disp_dmac_func *,
			const struct nv50_disp_chan_mthd *,
			struct nv50_disp *, int chid,
			const struct nvkm_oclass *, void *argv, u32 argc,
			struct nvkm_object **);
int nv50_disp_ovly_new_(const struct nv50_disp_dmac_func *,
			const struct nv50_disp_chan_mthd *,
			struct nv50_disp *, int chid,
			const struct nvkm_oclass *, void *argv, u32 argc,
			struct nvkm_object **);

int nv50_disp_curs_new(const struct nvkm_oclass *, void *, u32,
		       struct nv50_disp *, struct nvkm_object **);
int nv50_disp_oimm_new(const struct nvkm_oclass *, void *, u32,
		       struct nv50_disp *, struct nvkm_object **);
int nv50_disp_base_new(const struct nvkm_oclass *, void *, u32,
		       struct nv50_disp *, struct nvkm_object **);
int nv50_disp_ovly_new(const struct nvkm_oclass *, void *, u32,
		       struct nv50_disp *, struct nvkm_object **);

int g84_disp_base_new(const struct nvkm_oclass *, void *, u32,
		      struct nv50_disp *, struct nvkm_object **);
int g84_disp_ovly_new(const struct nvkm_oclass *, void *, u32,
		      struct nv50_disp *, struct nvkm_object **);

int gt200_disp_ovly_new(const struct nvkm_oclass *, void *, u32,
			struct nv50_disp *, struct nvkm_object **);

int gf119_disp_curs_new(const struct nvkm_oclass *, void *, u32,
			struct nv50_disp *, struct nvkm_object **);
int gf119_disp_oimm_new(const struct nvkm_oclass *, void *, u32,
			struct nv50_disp *, struct nvkm_object **);
int gf119_disp_base_new(const struct nvkm_oclass *, void *, u32,
			struct nv50_disp *, struct nvkm_object **);
int gf119_disp_ovly_new(const struct nvkm_oclass *, void *, u32,
			struct nv50_disp *, struct nvkm_object **);

int gk104_disp_ovly_new(const struct nvkm_oclass *, void *, u32,
			struct nv50_disp *, struct nvkm_object **);

int gp102_disp_curs_new(const struct nvkm_oclass *, void *, u32,
			struct nv50_disp *, struct nvkm_object **);
int gp102_disp_oimm_new(const struct nvkm_oclass *, void *, u32,
			struct nv50_disp *, struct nvkm_object **);
int gp102_disp_base_new(const struct nvkm_oclass *, void *, u32,
			struct nv50_disp *, struct nvkm_object **);
int gp102_disp_ovly_new(const struct nvkm_oclass *, void *, u32,
			struct nv50_disp *, struct nvkm_object **);

struct nv50_disp_mthd_list {
	u32 mthd;
	u32 addr;
	struct {
		u32 mthd;
		u32 addr;
		const char *name;
	} data[];
};

struct nv50_disp_chan_mthd {
	const char *name;
	u32 addr;
	s32 prev;
	struct {
		const char *name;
		int nr;
		const struct nv50_disp_mthd_list *mthd;
	} data[];
};

void nv50_disp_chan_mthd(struct nv50_disp_chan *, int debug);

extern const struct nv50_disp_mthd_list nv50_disp_core_mthd_base;
extern const struct nv50_disp_mthd_list nv50_disp_core_mthd_sor;
extern const struct nv50_disp_mthd_list nv50_disp_core_mthd_pior;
extern const struct nv50_disp_mthd_list nv50_disp_base_mthd_image;

extern const struct nv50_disp_chan_mthd g84_disp_core_chan_mthd;
extern const struct nv50_disp_mthd_list g84_disp_core_mthd_dac;
extern const struct nv50_disp_mthd_list g84_disp_core_mthd_head;

extern const struct nv50_disp_chan_mthd g94_disp_core_chan_mthd;

extern const struct nv50_disp_mthd_list gf119_disp_core_mthd_base;
extern const struct nv50_disp_mthd_list gf119_disp_core_mthd_dac;
extern const struct nv50_disp_mthd_list gf119_disp_core_mthd_sor;
extern const struct nv50_disp_mthd_list gf119_disp_core_mthd_pior;
extern const struct nv50_disp_chan_mthd gf119_disp_base_mthd;

extern const struct nv50_disp_chan_mthd gk104_disp_core_chan_mthd;
extern const struct nv50_disp_chan_mthd gk104_disp_ovly_mthd;
#endif
