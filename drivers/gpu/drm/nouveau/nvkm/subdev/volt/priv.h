#ifndef __NVKM_VOLT_PRIV_H__
#define __NVKM_VOLT_PRIV_H__
#define nvkm_volt(p) container_of((p), struct nvkm_volt, subdev)
#include <subdev/volt.h>

void nvkm_volt_ctor(const struct nvkm_volt_func *, struct nvkm_device *,
		    int index, struct nvkm_volt *);
int nvkm_volt_new_(const struct nvkm_volt_func *, struct nvkm_device *,
		   int index, struct nvkm_volt **);

struct nvkm_volt_func {
	int (*oneinit)(struct nvkm_volt *);
	int (*volt_get)(struct nvkm_volt *);
	int (*volt_set)(struct nvkm_volt *, u32 uv);
	int (*vid_get)(struct nvkm_volt *);
	int (*vid_set)(struct nvkm_volt *, u8 vid);
	int (*set_id)(struct nvkm_volt *, u8 id, int condition);
	int (*speedo_read)(struct nvkm_volt *);
};

int nvkm_voltgpio_init(struct nvkm_volt *);
int nvkm_voltgpio_get(struct nvkm_volt *);
int nvkm_voltgpio_set(struct nvkm_volt *, u8);

int nvkm_voltpwm_init(struct nvkm_volt *volt);
int nvkm_voltpwm_get(struct nvkm_volt *volt);
int nvkm_voltpwm_set(struct nvkm_volt *volt, u32 uv);

int gf100_volt_oneinit(struct nvkm_volt *);
#endif
