#ifndef __NV50_DISP_CHAN_H__
#define __NV50_DISP_CHAN_H__
#include "nv50.h"

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

int  nv50_disp_chan_create_(struct nvkm_object *, struct nvkm_object *,
			    struct nvkm_oclass *, int, int, void **);
void nv50_disp_chan_destroy(struct nv50_disp_chan *);
int  nv50_disp_chan_ntfy(struct nvkm_object *, u32, struct nvkm_event **);
int  nv50_disp_chan_map(struct nvkm_object *, u64 *, u32 *);
u32  nv50_disp_chan_rd32(struct nvkm_object *, u64);
void nv50_disp_chan_wr32(struct nvkm_object *, u64, u32);
extern const struct nvkm_event_func nv50_disp_chan_uevent;
int  nv50_disp_chan_uevent_ctor(struct nvkm_object *, void *, u32,
				struct nvkm_notify *);
void nv50_disp_chan_uevent_send(struct nv50_disp *, int);

extern const struct nvkm_event_func gf119_disp_chan_uevent;

#define nv50_disp_chan_init(a)                                                 \
	nvkm_namedb_init(&(a)->base)
#define nv50_disp_chan_fini(a,b)                                               \
	nvkm_namedb_fini(&(a)->base, (b))

struct nv50_disp_pioc {
	struct nv50_disp_chan base;
};

int  nv50_disp_pioc_create_(struct nvkm_object *, struct nvkm_object *,
			    struct nvkm_oclass *, int, int, void **);
void nv50_disp_pioc_dtor(struct nvkm_object *);
int  nv50_disp_pioc_init(struct nvkm_object *);
int  nv50_disp_pioc_fini(struct nvkm_object *, bool);

int  gf119_disp_pioc_init(struct nvkm_object *);
int  gf119_disp_pioc_fini(struct nvkm_object *, bool);

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

void nv50_disp_mthd_chan(struct nv50_disp *, int debug, int head,
			 const struct nv50_disp_mthd_chan *);

extern const struct nv50_disp_mthd_chan nv50_disp_core_mthd_chan;
extern const struct nv50_disp_mthd_list nv50_disp_core_mthd_base;
extern const struct nv50_disp_mthd_list nv50_disp_core_mthd_sor;
extern const struct nv50_disp_mthd_list nv50_disp_core_mthd_pior;
extern const struct nv50_disp_mthd_chan nv50_disp_base_mthd_chan;
extern const struct nv50_disp_mthd_list nv50_disp_base_mthd_image;
extern const struct nv50_disp_mthd_chan nv50_disp_ovly_mthd_chan;
extern const struct nv50_disp_mthd_list nv50_disp_ovly_mthd_base;

extern const struct nv50_disp_mthd_chan g84_disp_core_mthd_chan;
extern const struct nv50_disp_mthd_list g84_disp_core_mthd_dac;
extern const struct nv50_disp_mthd_list g84_disp_core_mthd_head;
extern const struct nv50_disp_mthd_chan g84_disp_base_mthd_chan;
extern const struct nv50_disp_mthd_chan g84_disp_ovly_mthd_chan;

extern const struct nv50_disp_mthd_chan g94_disp_core_mthd_chan;

extern const struct nv50_disp_mthd_chan gt200_disp_ovly_mthd_chan;

extern const struct nv50_disp_mthd_chan gf119_disp_core_mthd_chan;
extern const struct nv50_disp_mthd_list gf119_disp_core_mthd_base;
extern const struct nv50_disp_mthd_list gf119_disp_core_mthd_dac;
extern const struct nv50_disp_mthd_list gf119_disp_core_mthd_sor;
extern const struct nv50_disp_mthd_list gf119_disp_core_mthd_pior;
extern const struct nv50_disp_mthd_chan gf119_disp_base_mthd_chan;
extern const struct nv50_disp_mthd_chan gf119_disp_ovly_mthd_chan;

extern const struct nv50_disp_mthd_chan gk104_disp_core_mthd_chan;
extern const struct nv50_disp_mthd_chan gk104_disp_ovly_mthd_chan;
#endif
