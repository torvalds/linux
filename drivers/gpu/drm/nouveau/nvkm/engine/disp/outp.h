#ifndef __NVKM_DISP_OUTP_H__
#define __NVKM_DISP_OUTP_H__
#include <engine/disp.h>

#include <subdev/bios.h>
#include <subdev/bios/dcb.h>

struct nvkm_output {
	const struct nvkm_output_func *func;
	struct nvkm_disp *disp;
	int index;
	struct dcb_output info;

	// whatever (if anything) is pointed at by the dcb device entry
	struct nvkm_i2c_bus *i2c;
	int or;

	struct list_head head;
	struct nvkm_connector *conn;
};

struct nvkm_output_func {
	void *(*dtor)(struct nvkm_output *);
	void (*init)(struct nvkm_output *);
	void (*fini)(struct nvkm_output *);
};

void nvkm_output_ctor(const struct nvkm_output_func *, struct nvkm_disp *,
		      int index, struct dcb_output *, struct nvkm_output *);
int nvkm_output_new_(const struct nvkm_output_func *, struct nvkm_disp *,
		     int index, struct dcb_output *, struct nvkm_output **);
void nvkm_output_del(struct nvkm_output **);
void nvkm_output_init(struct nvkm_output *);
void nvkm_output_fini(struct nvkm_output *);

int nv50_dac_output_new(struct nvkm_disp *, int, struct dcb_output *,
			struct nvkm_output **);
int nv50_sor_output_new(struct nvkm_disp *, int, struct dcb_output *,
			struct nvkm_output **);
int nv50_pior_output_new(struct nvkm_disp *, int, struct dcb_output *,
			 struct nvkm_output **);

u32 g94_sor_dp_lane_map(struct nvkm_device *, u8 lane);

void gm200_sor_magic(struct nvkm_output *outp);

#define OUTP_MSG(o,l,f,a...) do {                                              \
	struct nvkm_output *_outp = (o);                                       \
	nvkm_##l(&_outp->disp->engine.subdev, "outp %02x:%04x:%04x: "f"\n",    \
		 _outp->index, _outp->info.hasht, _outp->info.hashm, ##a);     \
} while(0)
#define OUTP_ERR(o,f,a...) OUTP_MSG((o), error, f, ##a)
#define OUTP_DBG(o,f,a...) OUTP_MSG((o), debug, f, ##a)
#define OUTP_TRACE(o,f,a...) OUTP_MSG((o), trace, f, ##a)
#endif
