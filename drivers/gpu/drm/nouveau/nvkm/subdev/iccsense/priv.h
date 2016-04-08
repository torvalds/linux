#ifndef __NVKM_ICCSENSE_PRIV_H__
#define __NVKM_ICCSENSE_PRIV_H__
#define nvkm_iccsense(p) container_of((p), struct nvkm_iccsense, subdev)
#include <subdev/iccsense.h>

struct nvkm_iccsense_rail {
	int (*read)(struct nvkm_iccsense *, struct nvkm_iccsense_rail *);
	struct i2c_adapter *i2c;
	u8 addr;
	u8 rail;
	u8 mohm;
};

void nvkm_iccsense_ctor(struct nvkm_device *, int, struct nvkm_iccsense *);
int nvkm_iccsense_new_(struct nvkm_device *, int, struct nvkm_iccsense **);
#endif
