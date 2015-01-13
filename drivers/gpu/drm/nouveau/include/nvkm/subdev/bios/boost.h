#ifndef __NVBIOS_BOOST_H__
#define __NVBIOS_BOOST_H__

u16 nvbios_boostTe(struct nouveau_bios *, u8 *, u8 *, u8 *, u8 *, u8 *, u8 *);

struct nvbios_boostE {
	u8  pstate;
	u32 min;
	u32 max;
};

u16 nvbios_boostEe(struct nouveau_bios *, int idx, u8 *, u8 *, u8 *, u8 *);
u16 nvbios_boostEp(struct nouveau_bios *, int idx, u8 *, u8 *, u8 *, u8 *,
		   struct nvbios_boostE *);
u16 nvbios_boostEm(struct nouveau_bios *, u8, u8 *, u8 *, u8 *, u8 *,
		   struct nvbios_boostE *);

struct nvbios_boostS {
	u8  domain;
	u8  percent;
	u32 min;
	u32 max;
};

u16 nvbios_boostSe(struct nouveau_bios *, int, u16, u8 *, u8 *, u8, u8);
u16 nvbios_boostSp(struct nouveau_bios *, int, u16, u8 *, u8 *, u8, u8,
		   struct nvbios_boostS *);

#endif
