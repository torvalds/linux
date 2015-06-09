#ifndef __NVKM_BUS_HWSQ_H__
#define __NVKM_BUS_HWSQ_H__
#include <subdev/bus.h>

struct hwsq {
	struct nvkm_subdev *subdev;
	struct nvkm_hwsq *hwsq;
	int sequence;
};

struct hwsq_reg {
	int sequence;
	bool force;
	u32 addr;
	u32 stride; /* in bytes */
	u32 mask;
	u32 data;
};

static inline struct hwsq_reg
hwsq_stride(u32 addr, u32 stride, u32 mask)
{
	return (struct hwsq_reg) {
		.sequence = 0,
		.force = 0,
		.addr = addr,
		.stride = stride,
		.mask = mask,
		.data = 0xdeadbeef,
	};
}

static inline struct hwsq_reg
hwsq_reg2(u32 addr1, u32 addr2)
{
	return (struct hwsq_reg) {
		.sequence = 0,
		.force = 0,
		.addr = addr1,
		.stride = addr2 - addr1,
		.mask = 0x3,
		.data = 0xdeadbeef,
	};
}

static inline struct hwsq_reg
hwsq_reg(u32 addr)
{
	return (struct hwsq_reg) {
		.sequence = 0,
		.force = 0,
		.addr = addr,
		.stride = 0,
		.mask = 0x1,
		.data = 0xdeadbeef,
	};
}

static inline int
hwsq_init(struct hwsq *ram, struct nvkm_subdev *subdev)
{
	struct nvkm_bus *pbus = nvkm_bus(subdev);
	int ret;

	ret = nvkm_hwsq_init(pbus, &ram->hwsq);
	if (ret)
		return ret;

	ram->sequence++;
	ram->subdev = subdev;
	return 0;
}

static inline int
hwsq_exec(struct hwsq *ram, bool exec)
{
	int ret = 0;
	if (ram->subdev) {
		ret = nvkm_hwsq_fini(&ram->hwsq, exec);
		ram->subdev = NULL;
	}
	return ret;
}

static inline u32
hwsq_rd32(struct hwsq *ram, struct hwsq_reg *reg)
{
	if (reg->sequence != ram->sequence)
		reg->data = nv_rd32(ram->subdev, reg->addr);
	return reg->data;
}

static inline void
hwsq_wr32(struct hwsq *ram, struct hwsq_reg *reg, u32 data)
{
	u32 mask, off = 0;

	reg->sequence = ram->sequence;
	reg->data = data;

	for (mask = reg->mask; mask > 0; mask = (mask & ~1) >> 1) {
		if (mask & 1)
			nvkm_hwsq_wr32(ram->hwsq, reg->addr+off, reg->data);

		off += reg->stride;
	}
}

static inline void
hwsq_nuke(struct hwsq *ram, struct hwsq_reg *reg)
{
	reg->force = true;
}

static inline u32
hwsq_mask(struct hwsq *ram, struct hwsq_reg *reg, u32 mask, u32 data)
{
	u32 temp = hwsq_rd32(ram, reg);
	if (temp != ((temp & ~mask) | data) || reg->force)
		hwsq_wr32(ram, reg, (temp & ~mask) | data);
	return temp;
}

static inline void
hwsq_setf(struct hwsq *ram, u8 flag, int data)
{
	nvkm_hwsq_setf(ram->hwsq, flag, data);
}

static inline void
hwsq_wait(struct hwsq *ram, u8 flag, u8 data)
{
	nvkm_hwsq_wait(ram->hwsq, flag, data);
}

static inline void
hwsq_nsec(struct hwsq *ram, u32 nsec)
{
	nvkm_hwsq_nsec(ram->hwsq, nsec);
}
#endif
