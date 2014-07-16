#ifndef __NVKM_DISP_OUTP_DP_H__
#define __NVKM_DISP_OUTP_DP_H__

#include <subdev/bios.h>
#include <subdev/bios/dp.h>

#include "outp.h"

struct nvkm_output_dp {
	struct nvkm_output base;

	struct nvbios_dpout info;
	u8 version;

	struct nouveau_eventh *irq;
	struct nouveau_eventh *hpd;
	struct work_struct work;
	atomic_t pending;
	bool present;
	u8 dpcd[16];

	struct {
		struct work_struct work;
		wait_queue_head_t wait;
		atomic_t done;
	} lt;
};

#define nvkm_output_dp_create(p,e,c,b,i,d)                                     \
	nvkm_output_dp_create_((p), (e), (c), (b), (i), sizeof(**d), (void **)d)
#define nvkm_output_dp_destroy(d) ({                                           \
	struct nvkm_output_dp *_outp = (d);                                    \
	_nvkm_output_dp_dtor(nv_object(_outp));                                \
})
#define nvkm_output_dp_init(d) ({                                              \
	struct nvkm_output_dp *_outp = (d);                                    \
	_nvkm_output_dp_init(nv_object(_outp));                                \
})
#define nvkm_output_dp_fini(d,s) ({                                            \
	struct nvkm_output_dp *_outp = (d);                                    \
	_nvkm_output_dp_fini(nv_object(_outp), (s));                           \
})

int nvkm_output_dp_create_(struct nouveau_object *, struct nouveau_object *,
			   struct nouveau_oclass *, struct dcb_output *,
			   int, int, void **);

int  _nvkm_output_dp_ctor(struct nouveau_object *, struct nouveau_object *,
			  struct nouveau_oclass *, void *, u32,
			  struct nouveau_object **);
void _nvkm_output_dp_dtor(struct nouveau_object *);
int  _nvkm_output_dp_init(struct nouveau_object *);
int  _nvkm_output_dp_fini(struct nouveau_object *, bool);

struct nvkm_output_dp_impl {
	struct nvkm_output_impl base;
	int (*pattern)(struct nvkm_output_dp *, int);
	int (*lnk_pwr)(struct nvkm_output_dp *, int nr);
	int (*lnk_ctl)(struct nvkm_output_dp *, int nr, int bw, bool ef);
	int (*drv_ctl)(struct nvkm_output_dp *, int ln, int vs, int pe, int pc);
};

int nvkm_output_dp_train(struct nvkm_output *, u32 rate, bool wait);

#endif
