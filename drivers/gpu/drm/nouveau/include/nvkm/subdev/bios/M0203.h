/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVBIOS_M0203_H__
#define __NVBIOS_M0203_H__
struct nvbios_M0203T {
#define M0203T_TYPE_RAMCFG 0x00
	u8  type;
	u16 pointer;
};

u32 nvbios_M0203Te(struct nvkm_bios *, u8 *ver, u8 *hdr, u8 *cnt, u8 *len);
u32 nvbios_M0203Tp(struct nvkm_bios *, u8 *ver, u8 *hdr, u8 *cnt, u8 *len,
		   struct nvbios_M0203T *);

struct nvbios_M0203E {
#define M0203E_TYPE_DDR2   0x0
#define M0203E_TYPE_DDR3   0x1
#define M0203E_TYPE_GDDR3  0x2
#define M0203E_TYPE_GDDR5  0x3
#define M0203E_TYPE_HBM2   0x6
#define M0203E_TYPE_GDDR5X 0x8
#define M0203E_TYPE_GDDR6  0x9
#define M0203E_TYPE_SKIP   0xf
	u8 type;
	u8 strap;
	u8 group;
};

u32 nvbios_M0203Ee(struct nvkm_bios *, int idx, u8 *ver, u8 *hdr);
u32 nvbios_M0203Ep(struct nvkm_bios *, int idx, u8 *ver, u8 *hdr,
		   struct nvbios_M0203E *);
u32 nvbios_M0203Em(struct nvkm_bios *, u8 ramcfg, u8 *ver, u8 *hdr,
		   struct nvbios_M0203E *);
#endif
