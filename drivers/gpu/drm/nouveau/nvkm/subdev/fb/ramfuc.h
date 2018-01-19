/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVKM_FBRAM_FUC_H__
#define __NVKM_FBRAM_FUC_H__
#include <subdev/fb.h>
#include <subdev/pmu.h>

struct ramfuc {
	struct nvkm_memx *memx;
	struct nvkm_fb *fb;
	int sequence;
};

struct ramfuc_reg {
	int sequence;
	bool force;
	u32 addr;
	u32 stride; /* in bytes */
	u32 mask;
	u32 data;
};

static inline struct ramfuc_reg
ramfuc_stride(u32 addr, u32 stride, u32 mask)
{
	return (struct ramfuc_reg) {
		.sequence = 0,
		.addr = addr,
		.stride = stride,
		.mask = mask,
		.data = 0xdeadbeef,
	};
}

static inline struct ramfuc_reg
ramfuc_reg2(u32 addr1, u32 addr2)
{
	return (struct ramfuc_reg) {
		.sequence = 0,
		.addr = addr1,
		.stride = addr2 - addr1,
		.mask = 0x3,
		.data = 0xdeadbeef,
	};
}

static noinline struct ramfuc_reg
ramfuc_reg(u32 addr)
{
	return (struct ramfuc_reg) {
		.sequence = 0,
		.addr = addr,
		.stride = 0,
		.mask = 0x1,
		.data = 0xdeadbeef,
	};
}

static inline int
ramfuc_init(struct ramfuc *ram, struct nvkm_fb *fb)
{
	int ret = nvkm_memx_init(fb->subdev.device->pmu, &ram->memx);
	if (ret)
		return ret;

	ram->sequence++;
	ram->fb = fb;
	return 0;
}

static inline int
ramfuc_exec(struct ramfuc *ram, bool exec)
{
	int ret = 0;
	if (ram->fb) {
		ret = nvkm_memx_fini(&ram->memx, exec);
		ram->fb = NULL;
	}
	return ret;
}

static inline u32
ramfuc_rd32(struct ramfuc *ram, struct ramfuc_reg *reg)
{
	struct nvkm_device *device = ram->fb->subdev.device;
	if (reg->sequence != ram->sequence)
		reg->data = nvkm_rd32(device, reg->addr);
	return reg->data;
}

static inline void
ramfuc_wr32(struct ramfuc *ram, struct ramfuc_reg *reg, u32 data)
{
	unsigned int mask, off = 0;

	reg->sequence = ram->sequence;
	reg->data = data;

	for (mask = reg->mask; mask > 0; mask = (mask & ~1) >> 1) {
		if (mask & 1)
			nvkm_memx_wr32(ram->memx, reg->addr+off, reg->data);
		off += reg->stride;
	}
}

static inline void
ramfuc_nuke(struct ramfuc *ram, struct ramfuc_reg *reg)
{
	reg->force = true;
}

static inline u32
ramfuc_mask(struct ramfuc *ram, struct ramfuc_reg *reg, u32 mask, u32 data)
{
	u32 temp = ramfuc_rd32(ram, reg);
	if (temp != ((temp & ~mask) | data) || reg->force) {
		ramfuc_wr32(ram, reg, (temp & ~mask) | data);
		reg->force = false;
	}
	return temp;
}

static inline void
ramfuc_wait(struct ramfuc *ram, u32 addr, u32 mask, u32 data, u32 nsec)
{
	nvkm_memx_wait(ram->memx, addr, mask, data, nsec);
}

static inline void
ramfuc_nsec(struct ramfuc *ram, u32 nsec)
{
	nvkm_memx_nsec(ram->memx, nsec);
}

static inline void
ramfuc_wait_vblank(struct ramfuc *ram)
{
	nvkm_memx_wait_vblank(ram->memx);
}

static inline void
ramfuc_train(struct ramfuc *ram)
{
	nvkm_memx_train(ram->memx);
}

static inline int
ramfuc_train_result(struct nvkm_fb *fb, u32 *result, u32 rsize)
{
	return nvkm_memx_train_result(fb->subdev.device->pmu, result, rsize);
}

static inline void
ramfuc_block(struct ramfuc *ram)
{
	nvkm_memx_block(ram->memx);
}

static inline void
ramfuc_unblock(struct ramfuc *ram)
{
	nvkm_memx_unblock(ram->memx);
}

#define ram_init(s,p)        ramfuc_init(&(s)->base, (p))
#define ram_exec(s,e)        ramfuc_exec(&(s)->base, (e))
#define ram_have(s,r)        ((s)->r_##r.addr != 0x000000)
#define ram_rd32(s,r)        ramfuc_rd32(&(s)->base, &(s)->r_##r)
#define ram_wr32(s,r,d)      ramfuc_wr32(&(s)->base, &(s)->r_##r, (d))
#define ram_nuke(s,r)        ramfuc_nuke(&(s)->base, &(s)->r_##r)
#define ram_mask(s,r,m,d)    ramfuc_mask(&(s)->base, &(s)->r_##r, (m), (d))
#define ram_wait(s,r,m,d,n)  ramfuc_wait(&(s)->base, (r), (m), (d), (n))
#define ram_nsec(s,n)        ramfuc_nsec(&(s)->base, (n))
#define ram_wait_vblank(s)   ramfuc_wait_vblank(&(s)->base)
#define ram_train(s)         ramfuc_train(&(s)->base)
#define ram_train_result(s,r,l) ramfuc_train_result((s), (r), (l))
#define ram_block(s)         ramfuc_block(&(s)->base)
#define ram_unblock(s)       ramfuc_unblock(&(s)->base)
#endif
