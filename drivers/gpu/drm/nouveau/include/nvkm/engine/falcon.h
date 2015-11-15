#ifndef __NVKM_FALCON_H__
#define __NVKM_FALCON_H__
#define nvkm_falcon(p) container_of((p), struct nvkm_falcon, engine)
#include <core/engine.h>
struct nvkm_fifo_chan;

struct nvkm_falcon {
	const struct nvkm_falcon_func *func;
	struct nvkm_engine engine;

	u32 addr;
	u8  version;
	u8  secret;

	struct nvkm_memory *core;
	bool external;

	struct {
		u32 limit;
		u32 *data;
		u32  size;
	} code;

	struct {
		u32 limit;
		u32 *data;
		u32  size;
	} data;
};

int nvkm_falcon_new_(const struct nvkm_falcon_func *, struct nvkm_device *,
		     int index, bool enable, u32 addr, struct nvkm_engine **);

struct nvkm_falcon_func {
	struct {
		u32 *data;
		u32  size;
	} code;
	struct {
		u32 *data;
		u32  size;
	} data;
	u32 pmc_enable;
	void (*init)(struct nvkm_falcon *);
	void (*intr)(struct nvkm_falcon *, struct nvkm_fifo_chan *);
	struct nvkm_sclass sclass[];
};
#endif
