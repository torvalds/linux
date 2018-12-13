#ifndef __NVKM_FAULT_PRIV_H__
#define __NVKM_FAULT_PRIV_H__
#define nvkm_fault_buffer(p) container_of((p), struct nvkm_fault_buffer, object)
#define nvkm_fault(p) container_of((p), struct nvkm_fault, subdev)
#include <subdev/fault.h>

#include <core/event.h>
#include <core/object.h>

struct nvkm_fault_buffer {
	struct nvkm_object object;
	struct nvkm_fault *fault;
	int id;
	int entries;
	u32 get;
	u32 put;
	struct nvkm_memory *mem;
	u64 addr;
};

int nvkm_fault_new_(const struct nvkm_fault_func *, struct nvkm_device *,
		    int index, struct nvkm_fault **);

struct nvkm_fault_func {
	int (*oneinit)(struct nvkm_fault *);
	void (*init)(struct nvkm_fault *);
	void (*fini)(struct nvkm_fault *);
	void (*intr)(struct nvkm_fault *);
	struct {
		int nr;
		u32 entry_size;
		void (*info)(struct nvkm_fault_buffer *);
		void (*init)(struct nvkm_fault_buffer *);
		void (*fini)(struct nvkm_fault_buffer *);
		void (*intr)(struct nvkm_fault_buffer *, bool enable);
	} buffer;
};

int gv100_fault_oneinit(struct nvkm_fault *);
#endif
