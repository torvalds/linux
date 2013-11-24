#ifndef __NVKM_FBRAM_FUC_H__
#define __NVKM_FBRAM_FUC_H__

#include <subdev/pwr.h>

struct ramfuc {
	struct nouveau_memx *memx;
	struct nouveau_fb *pfb;
	int sequence;
};

struct ramfuc_reg {
	int sequence;
	bool force;
	u32 addr[2];
	u32 data;
};

static inline struct ramfuc_reg
ramfuc_reg2(u32 addr1, u32 addr2)
{
	return (struct ramfuc_reg) {
		.sequence = 0,
		.addr = { addr1, addr2 },
		.data = 0xdeadbeef,
	};
}

static inline struct ramfuc_reg
ramfuc_reg(u32 addr)
{
	return ramfuc_reg2(addr, addr);
}

static inline int
ramfuc_init(struct ramfuc *ram, struct nouveau_fb *pfb)
{
	struct nouveau_pwr *ppwr = nouveau_pwr(pfb);
	int ret;

	ret = nouveau_memx_init(ppwr, &ram->memx);
	if (ret)
		return ret;

	ram->sequence++;
	ram->pfb = pfb;
	return 0;
}

static inline int
ramfuc_exec(struct ramfuc *ram, bool exec)
{
	int ret = 0;
	if (ram->pfb) {
		ret = nouveau_memx_fini(&ram->memx, exec);
		ram->pfb = NULL;
	}
	return ret;
}

static inline u32
ramfuc_rd32(struct ramfuc *ram, struct ramfuc_reg *reg)
{
	if (reg->sequence != ram->sequence)
		reg->data = nv_rd32(ram->pfb, reg->addr[0]);
	return reg->data;
}

static inline void
ramfuc_wr32(struct ramfuc *ram, struct ramfuc_reg *reg, u32 data)
{
	reg->sequence = ram->sequence;
	reg->data = data;
	if (reg->addr[0] != reg->addr[1])
		nouveau_memx_wr32(ram->memx, reg->addr[1], reg->data);
	nouveau_memx_wr32(ram->memx, reg->addr[0], reg->data);
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
	nouveau_memx_wait(ram->memx, addr, mask, data, nsec);
}

static inline void
ramfuc_nsec(struct ramfuc *ram, u32 nsec)
{
	nouveau_memx_nsec(ram->memx, nsec);
}

#define ram_init(s,p)       ramfuc_init(&(s)->base, (p))
#define ram_exec(s,e)       ramfuc_exec(&(s)->base, (e))
#define ram_have(s,r)       ((s)->r_##r.addr != 0x000000)
#define ram_rd32(s,r)       ramfuc_rd32(&(s)->base, &(s)->r_##r)
#define ram_wr32(s,r,d)     ramfuc_wr32(&(s)->base, &(s)->r_##r, (d))
#define ram_nuke(s,r)       ramfuc_nuke(&(s)->base, &(s)->r_##r)
#define ram_mask(s,r,m,d)   ramfuc_mask(&(s)->base, &(s)->r_##r, (m), (d))
#define ram_wait(s,r,m,d,n) ramfuc_wait(&(s)->base, (r), (m), (d), (n))
#define ram_nsec(s,n)       ramfuc_nsec(&(s)->base, (n))

#endif
