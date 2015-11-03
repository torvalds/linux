#ifndef __NVKM_DISP_OUTP_DP_H__
#define __NVKM_DISP_OUTP_DP_H__
#define nvkm_output_dp(p) container_of((p), struct nvkm_output_dp, base)
#ifndef MSG
#define MSG(l,f,a...)                                                          \
	nvkm_##l(&outp->base.disp->engine.subdev, "%02x:%04x:%04x: "f,         \
		 outp->base.index, outp->base.info.hasht,                      \
		 outp->base.info.hashm, ##a)
#define DBG(f,a...) MSG(debug, f, ##a)
#define ERR(f,a...) MSG(error, f, ##a)
#endif
#include "outp.h"

#include <core/notify.h>
#include <subdev/bios.h>
#include <subdev/bios/dp.h>

struct nvkm_output_dp {
	const struct nvkm_output_dp_func *func;
	struct nvkm_output base;

	struct nvbios_dpout info;
	u8 version;

	struct nvkm_i2c_aux *aux;

	struct nvkm_notify irq;
	struct nvkm_notify hpd;
	bool present;
	u8 dpcd[16];

	struct {
		struct work_struct work;
		wait_queue_head_t wait;
		atomic_t done;
	} lt;
};

struct nvkm_output_dp_func {
	int (*pattern)(struct nvkm_output_dp *, int);
	int (*lnk_pwr)(struct nvkm_output_dp *, int nr);
	int (*lnk_ctl)(struct nvkm_output_dp *, int nr, int bw, bool ef);
	int (*drv_ctl)(struct nvkm_output_dp *, int ln, int vs, int pe, int pc);
};

int nvkm_output_dp_train(struct nvkm_output *, u32 rate, bool wait);

int nvkm_output_dp_ctor(const struct nvkm_output_dp_func *, struct nvkm_disp *,
			int index, struct dcb_output *, struct nvkm_i2c_aux *,
			struct nvkm_output_dp *);
int nvkm_output_dp_new_(const struct nvkm_output_dp_func *, struct nvkm_disp *,
			int index, struct dcb_output *,
			struct nvkm_output **);

int nv50_pior_dp_new(struct nvkm_disp *, int, struct dcb_output *,
		     struct nvkm_output **);

int g94_sor_dp_new(struct nvkm_disp *, int, struct dcb_output *,
		   struct nvkm_output **);
int g94_sor_dp_lnk_pwr(struct nvkm_output_dp *, int);

int gf119_sor_dp_new(struct nvkm_disp *, int, struct dcb_output *,
		     struct nvkm_output **);
int gf119_sor_dp_lnk_ctl(struct nvkm_output_dp *, int, int, bool);

int  gm204_sor_dp_new(struct nvkm_disp *, int, struct dcb_output *,
		      struct nvkm_output **);
#endif
