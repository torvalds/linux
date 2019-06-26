#ifndef __NVKM_FAULT_H__
#define __NVKM_FAULT_H__
#include <core/subdev.h>
#include <core/notify.h>

struct nvkm_fault {
	const struct nvkm_fault_func *func;
	struct nvkm_subdev subdev;

	struct nvkm_fault_buffer *buffer[2];
	int buffer_nr;

	struct nvkm_event event;

	struct nvkm_notify nrpfb;

	struct nvkm_device_oclass user;
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
int gv100_fault_new(struct nvkm_device *, int, struct nvkm_fault **);
int tu102_fault_new(struct nvkm_device *, int, struct nvkm_fault **);
#endif
