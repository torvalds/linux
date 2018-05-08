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

struct nvkm_fault_data {
	u64  addr;
	u64  inst;
	u64  time;
	u8 engine;
	u8  valid;
	u8    gpc;
	u8    hub;
	u8 access;
	u8 client;
	u8 reason;
};

int gp100_fault_new(struct nvkm_device *, int, struct nvkm_fault **);
#endif
