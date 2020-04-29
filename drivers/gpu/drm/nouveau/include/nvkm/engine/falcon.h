/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_FLCNEN_H__
#define __NVKM_FLCNEN_H__
#define nvkm_falcon(p) container_of((p), struct nvkm_falcon, engine)
#include <core/engine.h>
struct nvkm_fifo_chan;
struct nvkm_gpuobj;

enum nvkm_falcon_dmaidx {
	FALCON_DMAIDX_UCODE		= 0,
	FALCON_DMAIDX_VIRT		= 1,
	FALCON_DMAIDX_PHYS_VID		= 2,
	FALCON_DMAIDX_PHYS_SYS_COH	= 3,
	FALCON_DMAIDX_PHYS_SYS_NCOH	= 4,
	FALCON_SEC2_DMAIDX_UCODE	= 6,
};

struct nvkm_falcon {
	const struct nvkm_falcon_func *func;
	const struct nvkm_subdev *owner;
	const char *name;
	u32 addr;

	struct mutex mutex;
	struct mutex dmem_mutex;
	bool oneinit;

	const struct nvkm_subdev *user;

	u8 version;
	u8 secret;
	bool debug;

	struct nvkm_memory *core;
	bool external;

	struct {
		u32 limit;
		u32 *data;
		u32  size;
		u8 ports;
	} code;

	struct {
		u32 limit;
		u32 *data;
		u32  size;
		u8 ports;
	} data;

	struct nvkm_engine engine;
};

/* This constructor must be called from the owner's oneinit() hook and
 * *not* its constructor.  This is to ensure that DEVINIT has been
 * completed, and that the device is correctly enabled before we touch
 * falcon registers.
 */
int nvkm_falcon_v1_new(struct nvkm_subdev *owner, const char *name, u32 addr,
		       struct nvkm_falcon **);

void nvkm_falcon_del(struct nvkm_falcon **);
int nvkm_falcon_get(struct nvkm_falcon *, const struct nvkm_subdev *);
void nvkm_falcon_put(struct nvkm_falcon *, const struct nvkm_subdev *);

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
	void (*init)(struct nvkm_falcon *);
	void (*intr)(struct nvkm_falcon *, struct nvkm_fifo_chan *);

	u32 debug;
	u32 fbif;

	void (*load_imem)(struct nvkm_falcon *, void *, u32, u32, u16, u8, bool);
	void (*load_dmem)(struct nvkm_falcon *, void *, u32, u32, u8);
	void (*read_dmem)(struct nvkm_falcon *, u32, u32, u8, void *);
	u32 emem_addr;
	void (*bind_context)(struct nvkm_falcon *, struct nvkm_memory *);
	int (*wait_for_halt)(struct nvkm_falcon *, u32);
	int (*clear_interrupt)(struct nvkm_falcon *, u32);
	void (*set_start_addr)(struct nvkm_falcon *, u32 start_addr);
	void (*start)(struct nvkm_falcon *);
	int (*enable)(struct nvkm_falcon *falcon);
	void (*disable)(struct nvkm_falcon *falcon);
	int (*reset)(struct nvkm_falcon *);

	struct {
		u32 head;
		u32 tail;
		u32 stride;
	} cmdq, msgq;

	struct nvkm_sclass sclass[];
};

static inline u32
nvkm_falcon_rd32(struct nvkm_falcon *falcon, u32 addr)
{
	return nvkm_rd32(falcon->owner->device, falcon->addr + addr);
}

static inline void
nvkm_falcon_wr32(struct nvkm_falcon *falcon, u32 addr, u32 data)
{
	nvkm_wr32(falcon->owner->device, falcon->addr + addr, data);
}

static inline u32
nvkm_falcon_mask(struct nvkm_falcon *falcon, u32 addr, u32 mask, u32 val)
{
	struct nvkm_device *device = falcon->owner->device;

	return nvkm_mask(device, falcon->addr + addr, mask, val);
}

void nvkm_falcon_load_imem(struct nvkm_falcon *, void *, u32, u32, u16, u8,
			   bool);
void nvkm_falcon_load_dmem(struct nvkm_falcon *, void *, u32, u32, u8);
void nvkm_falcon_read_dmem(struct nvkm_falcon *, u32, u32, u8, void *);
void nvkm_falcon_bind_context(struct nvkm_falcon *, struct nvkm_memory *);
void nvkm_falcon_set_start_addr(struct nvkm_falcon *, u32);
void nvkm_falcon_start(struct nvkm_falcon *);
int nvkm_falcon_wait_for_halt(struct nvkm_falcon *, u32);
int nvkm_falcon_clear_interrupt(struct nvkm_falcon *, u32);
int nvkm_falcon_enable(struct nvkm_falcon *);
void nvkm_falcon_disable(struct nvkm_falcon *);
int nvkm_falcon_reset(struct nvkm_falcon *);
#endif
