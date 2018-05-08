#ifndef __NVKM_FAULT_H__
#define __NVKM_FAULT_H__
#include <core/subdev.h>

struct nvkm_fault {
	const struct nvkm_fault_func *func;
	struct nvkm_subdev subdev;

	struct nvkm_fault_buffer *buffer[1];
	int buffer_nr;

	struct nvkm_event event;
};

int gp100_fault_new(struct nvkm_device *, int, struct nvkm_fault **);
#endif
