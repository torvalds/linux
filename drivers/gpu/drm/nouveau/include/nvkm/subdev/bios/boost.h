/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVBIOS_BOOST_H__
#define __NVBIOS_BOOST_H__
u32 nvbios_boostTe(struct nvkm_bios *, u8 *, u8 *, u8 *, u8 *, u8 *, u8 *);

struct nvbios_boostE {
	u8  pstate;
	u32 min;
	u32 max;
};

u32 nvbios_boostEe(struct nvkm_bios *, int idx, u8 *, u8 *, u8 *, u8 *);
u32 nvbios_boostEp(struct nvkm_bios *, int idx, u8 *, u8 *, u8 *, u8 *,
		   struct nvbios_boostE *);
u32 nvbios_boostEm(struct nvkm_bios *, u8, u8 *, u8 *, u8 *, u8 *,
		   struct nvbios_boostE *);

struct nvbios_boostS {
	u8  domain;
	u8  percent;
	u32 min;
	u32 max;
};

u32 nvbios_boostSe(struct nvkm_bios *, int, u32, u8 *, u8 *, u8, u8);
u32 nvbios_boostSp(struct nvkm_bios *, int, u32, u8 *, u8 *, u8, u8,
		   struct nvbios_boostS *);
#endif
