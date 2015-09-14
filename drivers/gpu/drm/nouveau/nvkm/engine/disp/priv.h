#ifndef __NVKM_DISP_PRIV_H__
#define __NVKM_DISP_PRIV_H__
#include <engine/disp.h>
#include "outp.h"
#include "outpdp.h"

int nvkm_disp_ctor(const struct nvkm_disp_func *, struct nvkm_device *,
		   int index, int heads, struct nvkm_disp *);
int nvkm_disp_new_(const struct nvkm_disp_func *, struct nvkm_device *,
		   int index, int heads, struct nvkm_disp **);
void nvkm_disp_vblank(struct nvkm_disp *, int head);

struct nvkm_disp_func_outp {
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

struct nvkm_disp_func {
	void *(*dtor)(struct nvkm_disp *);
	void (*intr)(struct nvkm_disp *);

	const struct nvkm_disp_oclass *(*root)(struct nvkm_disp *);

	struct {
		void (*vblank_init)(struct nvkm_disp *, int head);
		void (*vblank_fini)(struct nvkm_disp *, int head);
	} head;

	struct {
		const struct nvkm_disp_func_outp internal;
		const struct nvkm_disp_func_outp external;
	} outp;
};

int  nvkm_disp_ntfy(struct nvkm_object *, u32, struct nvkm_event **);

extern const struct nvkm_disp_oclass nv04_disp_root_oclass;

struct nvkm_disp_oclass {
	int (*ctor)(struct nvkm_disp *, const struct nvkm_oclass *,
		    void *data, u32 size, struct nvkm_object **);
	struct nvkm_sclass base;
};
#endif
