#ifndef __NVKM_ICCSENSE_H__
#define __NVKM_ICCSENSE_H__

#include <core/subdev.h>

struct nkvm_iccsense_rail;
struct nvkm_iccsense {
	struct nvkm_subdev subdev;
	u8 rail_count;
	bool data_valid;
	struct nvkm_iccsense_rail *rails;
};

int gf100_iccsense_new(struct nvkm_device *, int index, struct nvkm_iccsense **);
int nvkm_iccsense_read_all(struct nvkm_iccsense *iccsense);
#endif
